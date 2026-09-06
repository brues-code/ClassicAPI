// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// The atlas system: `C_Texture.GetAtlasInfo` / `GetAtlasExists` / `GetAtlasID` /
// `GetAtlasElementID` / `GetAtlasElements`, `C_Texture.RegisterAtlas`, and the
// `texture:SetAtlas` / `texture:GetAtlas` widget methods.
//
// An atlas names a texture plus a sub-rect inside it, so `SetAtlas` is exactly the
// two calls 1.12 already has — SetTexture then SetTexCoord — driven off one name.
// Both are performed by delegating to the engine's OWN Lua handlers rather than
// the native setters, so every argument guard and the UI-scale conversion behind
// the size setters stay in force; the only thing this module adds is the name
// lookup. `AtlasData.h` explains where the built-in bindings come from.
//
// All five readers key off the same bound set, so `GetAtlasExists` is true exactly
// when `GetAtlasInfo` returns a table. There is no state where one disagrees.

#include "Atlas.h"

#include "AtlasData.h"

#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace Texture::Atlas {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

int CallScript(uintptr_t fn, void *L) { return reinterpret_cast<ScriptFn_t>(fn)(L); }

std::string Lower(const char *s) {
    std::string out;
    if (s == nullptr)
        return out;
    for (const char *p = s; *p != '\0'; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        out.push_back(c);
    }
    return out;
}

// The whole registry, keyed by lowercased name. `std::map` node storage means an
// `Info *` handed out by `Find` stays valid as later atlases are registered.
std::map<std::string, Info> &Registry() {
    static std::map<std::string, Info> registry;
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        for (int i = 0; i < AtlasData::kAtlasCount; ++i) {
            const AtlasData::Entry &e = AtlasData::kAtlases[i];
            Info info;
            info.name = e.name;
            info.file = e.file;
            info.width = static_cast<float>(e.width);
            info.height = static_cast<float>(e.height);
            info.left = e.left;
            info.right = e.right;
            info.top = e.top;
            info.bottom = e.bottom;
            info.atlasID = e.atlasID;
            info.elementID = e.elementID;
            info.tilesHorizontally = e.tilesHorizontally;
            info.tilesVertically = e.tilesVertically;
            registry[Lower(e.name)] = info;
        }
    }
    return registry;
}

// Names asked for that nothing could resolve. Bounded so a misbehaving addon
// calling SetAtlas in an OnUpdate can't grow it without limit.
constexpr size_t kMaxMisses = 512;
std::set<std::string> g_misses;

// `texture:GetAtlas` bookkeeping: which atlas a region was last set to, and the
// HTEXTURE that set installed. Comparing the stored handle against the region's
// current one on read means a plain `SetTexture` afterwards correctly clears the
// atlas — and a recycled region can't report a stale name either, which is what
// lets this module skip a `FUN_SIMPLETEXTURE_CTOR` hook. It must skip one:
// `Texture::Mask` already owns that target, and MinHook refuses a duplicate,
// which would abort the entire hook install.
// Texture coordinates in the engine's own corner order: UL, LL, UR, LR, each an
// (x, y) pair — the order GetTexCoord pushes them in. Identity selects all of
// whatever the texture points at.
constexpr float kIdentityCoords[8] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};

struct Applied {
    std::string name;
    void *hTexture = nullptr;
    // What the addon last asked SetTexCoord for, in SPRITE space. A region has
    // exactly one set of coordinates in the engine and the atlas rect occupies
    // it, so this is the only record of what was actually requested.
    float user[8] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
};
std::unordered_map<void *, Applied> g_applied;

void *RegionTexture(void *region) {
    if (region == nullptr)
        return nullptr;
    return *reinterpret_cast<void *const *>(static_cast<uint8_t *>(region) +
                                            Offsets::OFF_SIMPLETEXTURE_HTEXTURE);
}

// The atlas entry live on `region`, or null. Drops a stale one on the way, which
// is what makes a plain SetTexture afterwards clear the atlas. The empty-map
// test up front is what keeps this off the cost of every ordinary SetTexCoord:
// nothing has an atlas until an addon sets one.
Applied *LiveEntry(void *region) {
    if (region == nullptr || g_applied.empty())
        return nullptr;
    auto it = g_applied.find(region);
    if (it == g_applied.end())
        return nullptr;
    if (RegionTexture(region) != it->second.hTexture) {
        g_applied.erase(it);
        return nullptr;
    }
    return &it->second;
}

// Map sprite-space corners onto the atlas's rectangle within its file.
void Compose(const Info &info, const float user[8], float out[8]) {
    const float width = info.right - info.left;
    const float height = info.bottom - info.top;
    for (int i = 0; i < 4; ++i) {
        out[i * 2] = info.left + user[i * 2] * width;
        out[i * 2 + 1] = info.top + user[i * 2 + 1] * height;
    }
}

// SetTexCoord is co-hooked below, so every write this module makes goes through
// the trampoline: the values here are already in file space and must not be
// composed a second time.
ScriptFn_t g_setTexCoordOriginal = nullptr;
ScriptFn_t g_getTexCoordOriginal = nullptr;

int WriteTexCoord(void *L) {
    return g_setTexCoordOriginal != nullptr
               ? g_setTexCoordOriginal(L)
               : CallScript(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXCOORD, L);
}

// Pushes (self, 8 corners) and writes them straight through.
void WriteCorners(void *L, const float corners[8]) {
    Game::Lua::SetTop(L, 1); // (self)
    for (int i = 0; i < 8; ++i)
        Game::Lua::PushNumber(L, corners[i]);
    WriteTexCoord(L);
}

// Synthetic ids for addon-registered atlases, counting down from -1 so they can
// never be mistaken for one of Blizzard's (see Info's comment).
int g_nextSyntheticID = -1;

// --- C_Texture readers ------------------------------------------------------

int __fastcall Script_GetAtlasInfo(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const Info *info = Find(Game::Lua::ToString(L, 1));
    if (info == nullptr) {
        RecordMiss(Game::Lua::ToString(L, 1));
        return 0;
    }

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "elementName", info->name.c_str());
    Game::Lua::SetFieldNumber(L, "width", info->width);
    Game::Lua::SetFieldNumber(L, "height", info->height);
    Game::Lua::SetFieldNumber(L, "leftTexCoord", info->left);
    Game::Lua::SetFieldNumber(L, "rightTexCoord", info->right);
    Game::Lua::SetFieldNumber(L, "topTexCoord", info->top);
    Game::Lua::SetFieldNumber(L, "bottomTexCoord", info->bottom);
    Game::Lua::SetFieldBool(L, "tilesHorizontally", info->tilesHorizontally);
    Game::Lua::SetFieldBool(L, "tilesVertically", info->tilesVertically);
    // `filename` carries the texture. `file` is a fileDataID, and 1.12 has no
    // such id space at all, so it is left absent rather than filled with a value
    // that would not resolve — both fields are optional in the contract.
    Game::Lua::SetFieldString(L, "filename", info->file.c_str());
    return 1;
}

int __fastcall Script_GetAtlasExists(void *L) {
    const bool exists =
        Game::Lua::IsString(L, 1) && Find(Game::Lua::ToString(L, 1)) != nullptr;
    Game::Lua::PushBool(L, exists);
    return 1;
}

int __fastcall Script_GetAtlasID(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const Info *info = Find(Game::Lua::ToString(L, 1));
    if (info == nullptr)
        return 0;
    Game::Lua::PushNumber(L, info->atlasID);
    return 1;
}

int __fastcall Script_GetAtlasElementID(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const Info *info = Find(Game::Lua::ToString(L, 1));
    if (info == nullptr)
        return 0;
    Game::Lua::PushNumber(L, info->elementID);
    return 1;
}

int __fastcall Script_GetAtlasElements(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    int n = 0;
    for (const auto &pair : Registry()) {
        Game::Lua::PushNumber(L, ++n);
        Game::Lua::PushString(L, pair.second.name.c_str());
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// `C_Texture.RegisterAtlas(name, file, width, height, left, right, top, bottom
//                          [, tilesHorizontally, tilesVertically])`
// A ClassicAPI extension: it publishes an addon's own sprite sheet under atlas
// names, which is the case that needs no built-in binding to work.
int __fastcall Script_RegisterAtlas(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: C_Texture.RegisterAtlas(name, file, width, height, "
                            "left, right, top, bottom [, tilesH, tilesV])");
        return 0;
    }

    Info info;
    info.name = Game::Lua::ToString(L, 1);
    info.file = Game::Lua::ToString(L, 2);
    info.width = static_cast<float>(Game::Lua::ToNumber(L, 3));
    info.height = static_cast<float>(Game::Lua::ToNumber(L, 4));
    if (Game::Lua::IsNumber(L, 5))
        info.left = static_cast<float>(Game::Lua::ToNumber(L, 5));
    if (Game::Lua::IsNumber(L, 6))
        info.right = static_cast<float>(Game::Lua::ToNumber(L, 6));
    if (Game::Lua::IsNumber(L, 7))
        info.top = static_cast<float>(Game::Lua::ToNumber(L, 7));
    if (Game::Lua::IsNumber(L, 8))
        info.bottom = static_cast<float>(Game::Lua::ToNumber(L, 8));
    info.tilesHorizontally = Game::Lua::ToBoolean(L, 9) != 0;
    info.tilesVertically = Game::Lua::ToBoolean(L, 10) != 0;

    const std::string key = Lower(info.name.c_str());
    auto &registry = Registry();
    auto existing = registry.find(key);
    if (existing != registry.end()) {
        // Re-registering a name keeps its ids stable, so a `/reload` that re-runs
        // an addon's registration doesn't renumber anything behind a caller that
        // cached the id.
        info.atlasID = existing->second.atlasID;
        info.elementID = existing->second.elementID;
    } else {
        info.atlasID = g_nextSyntheticID;
        info.elementID = g_nextSyntheticID;
        --g_nextSyntheticID;
    }
    registry[key] = info;

    g_misses.erase(key);
    Game::Lua::PushBool(L, true);
    return 1;
}

// --- texture:SetAtlas / GetAtlas -------------------------------------------

// `texture:SetAtlas(atlas [, useAtlasSize, filterMode, resetTexCoords,
//                   wrapModeHorizontal, wrapModeVertical])`
// The trailing arguments are accepted and ignored: 1.12 texture regions have no
// filter or wrap mode to set, and dropping them silently is what lets modern call
// sites pass through unchanged.
int __fastcall Script_SetAtlas(void *L) {
    void *region = Game::Lua::ResolveObject(L, 1);
    if (region == nullptr)
        return 0;

    if (!Game::Lua::IsString(L, 2)) {
        // A nil atlas clears the binding without touching the texture, mirroring
        // the unknown-name path below.
        g_applied.erase(region);
        return 0;
    }

    const char *name = Game::Lua::ToString(L, 2);
    const bool useAtlasSize = Game::Lua::ToBoolean(L, 3) != 0;

    const Info *info = Find(name);
    if (info == nullptr) {
        // Deliberately NOT retail behaviour, which clears the texture. With few
        // built-in bindings a clear would make a ported frame vanish entirely and
        // give the author nothing to look at; leaving the previous texture in
        // place keeps the frame visible while the miss log says what to bind.
        RecordMiss(name);
        g_applied.erase(region);
        return 0;
    }

    // Reshaping the stack for each delegated handler leaves `info` untouched — it
    // points into the registry, which none of these calls can reach.
    Game::Lua::SetTop(L, 1); // (self)
    Game::Lua::PushString(L, info->file.c_str());
    CallScript(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXTURE, L);

    // Identity in sprite space is the atlas's whole rectangle in file space.
    float corners[8];
    Compose(*info, kIdentityCoords, corners);
    WriteCorners(L, corners);

    if (useAtlasSize && info->width > 0.0f && info->height > 0.0f) {
        Game::Lua::SetTop(L, 1); // (self)
        Game::Lua::PushNumber(L, info->width);
        CallScript(Offsets::FUN_SCRIPT_REGION_SETWIDTH, L);
        Game::Lua::SetTop(L, 1); // (self)
        Game::Lua::PushNumber(L, info->height);
        CallScript(Offsets::FUN_SCRIPT_REGION_SETHEIGHT, L);
    }

    Applied applied;
    applied.name = info->name;
    applied.hTexture = RegionTexture(region);
    g_applied[region] = applied;

    Game::Lua::SetTop(L, 0);
    return 0;
}

// `texture:GetAtlas()` — the atlas last set on this texture, or nil if the
// texture has been pointed somewhere else since.
int __fastcall Script_GetAtlas(void *L) {
    void *region = Game::Lua::ResolveObject(L, 1);
    if (region == nullptr)
        return 0;
    const Applied *entry = LiveEntry(region);
    if (entry == nullptr)
        return 0;
    Game::Lua::PushString(L, entry->name.c_str());
    return 1;
}

// `texture:ResetTexCoord()` — drop any sub-selection made with SetTexCoord and
// go back to showing the whole of whatever the texture is pointed at.
//
// Retail holds the source and the user's texcoords as separate state. With an
// atlas applied there, GetTexCoord reads the full 0..1 rect, because 0..1 means
// "all of the sprite" rather than "all of the sheet" — so resetting returns that
// layer to identity and the atlas stays applied. Verified in-game on 12.x:
// SetAtlas leaves GetTexCoord at 0,0,1,1, and a reset after a SetTexCoord comes
// back to it with GetAtlas still reporting the atlas.
//
// Here that is a plain restore of identity: SetTexCoord composition below keeps
// the addon's coordinates in sprite space, so identity means the whole sprite
// for an atlas'd texture and the whole file for any other.
int __fastcall Script_ResetTexCoord(void *L) {
    void *region = Game::Lua::ResolveObject(L, 1);
    if (region == nullptr)
        return 0;

    float corners[8];
    Applied *entry = LiveEntry(region);
    const Info *info = (entry != nullptr) ? Find(entry->name.c_str()) : nullptr;
    if (info != nullptr) {
        for (int i = 0; i < 8; ++i)
            entry->user[i] = kIdentityCoords[i];
        Compose(*info, kIdentityCoords, corners);
    } else {
        for (int i = 0; i < 8; ++i)
            corners[i] = kIdentityCoords[i];
    }

    WriteCorners(L, corners);
    Game::Lua::SetTop(L, 0);
    return 0;
}

// --- SetTexCoord / GetTexCoord composition ----------------------------------
//
// With an atlas applied, texture coordinates address the SPRITE rather than the
// file it sits in: 0..1 is the whole of the atlas's art. Verified against a live
// client — GetTexCoord reads the full rect immediately after SetAtlas, with the
// atlas still reported by GetAtlas — and it is the only reading under which a
// ported SetTexCoord shows the image the author meant rather than a slice of
// the surrounding sheet.
//
// The engine keeps one set of coordinates per region, and this module writes the
// atlas rect into it, so the addon's own coordinates have nowhere to live. They
// ride along with the atlas name instead: SetTexCoord records them and writes
// the composed result for the engine to draw, GetTexCoord hands them back.
//
// Both pass straight through for a texture with no atlas, which is every texture
// in the game until an addon calls SetAtlas — LiveEntry's empty-map test is the
// whole cost in that case.

// Reads the 4-argument (left, right, top, bottom) or 8-argument corner form into
// corner order. False for anything else, which is left to the engine to reject
// so its own error text is what the author sees.
bool ReadTexCoordArgs(void *L, float out[8]) {
    const int top = Game::Lua::GetTop(L);
    if (top != 5 && top != 9)
        return false;
    float a[8];
    const int count = top - 1;
    for (int i = 0; i < count; ++i) {
        if (!Game::Lua::IsNumber(L, 2 + i))
            return false;
        a[i] = static_cast<float>(Game::Lua::ToNumber(L, 2 + i));
    }
    if (count == 8) {
        for (int i = 0; i < 8; ++i)
            out[i] = a[i];
        return true;
    }
    const float left = a[0], right = a[1], upper = a[2], lower = a[3];
    out[0] = left;  out[1] = upper; // UL
    out[2] = left;  out[3] = lower; // LL
    out[4] = right; out[5] = upper; // UR
    out[6] = right; out[7] = lower; // LR
    return true;
}

int __fastcall SetTexCoord_h(void *L) {
    if (!g_applied.empty()) {
        Applied *entry = LiveEntry(Game::Lua::ResolveObject(L, 1));
        const Info *info = (entry != nullptr) ? Find(entry->name.c_str()) : nullptr;
        float user[8];
        if (info != nullptr && ReadTexCoordArgs(L, user)) {
            for (int i = 0; i < 8; ++i)
                entry->user[i] = user[i];
            float corners[8];
            Compose(*info, user, corners);
            WriteCorners(L, corners);
            Game::Lua::SetTop(L, 0);
            return 0;
        }
    }
    return g_setTexCoordOriginal(L);
}

int __fastcall GetTexCoord_h(void *L) {
    if (!g_applied.empty()) {
        const Applied *entry = LiveEntry(Game::Lua::ResolveObject(L, 1));
        if (entry != nullptr) {
            for (int i = 0; i < 8; ++i)
                Game::Lua::PushNumber(L, entry->user[i]);
            return 8;
        }
    }
    return g_getTexCoordOriginal(L);
}

// A texture pointed at a file is no longer showing an atlas. LiveEntry's handle
// comparison cannot see that on its own when the atlas names the very file being
// set — the engine hands back the same HTEXTURE — so clear it here, where the
// intent is unambiguous. The handle comparison still earns its keep for a
// recycled region, which is a different question and the reason this module
// needs no region-constructor hook.
//
// SetAtlas records its entry only after its own SetTexture call, so this running
// first costs it nothing.
ScriptFn_t g_setTextureOriginal = nullptr;

int __fastcall SetTexture_h(void *L) {
    if (!g_applied.empty())
        g_applied.erase(Game::Lua::ResolveObject(L, 1));
    return g_setTextureOriginal(L);
}

const Game::HookAutoRegister _setTextureHook{Offsets::FUN_SCRIPT_TEXTURE_SET_TEXTURE,
                                             reinterpret_cast<void *>(&SetTexture_h),
                                             reinterpret_cast<void **>(&g_setTextureOriginal)};

const Game::HookAutoRegister _setTexCoordHook{Offsets::FUN_SCRIPT_TEXTURE_SET_TEXCOORD,
                                              reinterpret_cast<void *>(&SetTexCoord_h),
                                              reinterpret_cast<void **>(&g_setTexCoordOriginal)};
const Game::HookAutoRegister _getTexCoordHook{Offsets::FUN_SCRIPT_TEXTURE_GET_TEXCOORD,
                                              reinterpret_cast<void *>(&GetTexCoord_h),
                                              reinterpret_cast<void **>(&g_getTexCoordOriginal)};

// --- diagnostics ------------------------------------------------------------

// Returns every atlas name something asked for that nothing could resolve, and
// writes the same list to the debug log. This is the growth loop for the built-in
// table: load an addon, read what it really wanted, bind those names.
int __fastcall Script_DumpAtlasMisses(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    int n = 0;
    Debug::Log::Printf("[atlas] %d unresolved atlas name(s)",
                       static_cast<int>(g_misses.size()));
    for (const std::string &name : g_misses) {
        Debug::Log::Printf("[atlas]   %s", name.c_str());
        Game::Lua::PushNumber(L, ++n);
        Game::Lua::PushString(L, name.c_str());
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// --- registration -----------------------------------------------------------

const Game::Lua::FrameMethodEntry g_textureMethods[] = {
    {"SetAtlas", &Script_SetAtlas},
    {"GetAtlas", &Script_GetAtlas},
    {"ResetTexCoord", &Script_ResetTexCoord},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_textureMethods,
        static_cast<int>(sizeof(g_textureMethods) / sizeof(g_textureMethods[0])));

    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasInfo", &Script_GetAtlasInfo);
    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasExists", &Script_GetAtlasExists);
    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasID", &Script_GetAtlasID);
    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasElementID",
                                     &Script_GetAtlasElementID);
    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasElements",
                                     &Script_GetAtlasElements);
    Game::Lua::RegisterTableFunction("C_Texture", "RegisterAtlas", &Script_RegisterAtlas);

    Game::Lua::RegisterGlobalFunction("_classicapi_DumpAtlasMisses", &Script_DumpAtlasMisses);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace

const Info *Find(const char *name) {
    if (name == nullptr || *name == '\0')
        return nullptr;
    auto &registry = Registry();
    auto it = registry.find(Lower(name));
    return (it == registry.end()) ? nullptr : &it->second;
}

void RecordMiss(const char *name) {
    if (name == nullptr || *name == '\0' || g_misses.size() >= kMaxMisses)
        return;
    g_misses.insert(Lower(name));
}

void PrepareForReload() {
    g_applied.clear();
    g_misses.clear();
}

} // namespace Texture::Atlas
