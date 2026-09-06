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
struct Applied {
    std::string name;
    void *hTexture = nullptr;
};
std::unordered_map<void *, Applied> g_applied;

void *RegionTexture(void *region) {
    if (region == nullptr)
        return nullptr;
    return *reinterpret_cast<void *const *>(static_cast<uint8_t *>(region) +
                                            Offsets::OFF_SIMPLETEXTURE_HTEXTURE);
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

    Game::Lua::SetTop(L, 1); // (self)
    Game::Lua::PushNumber(L, info->left);
    Game::Lua::PushNumber(L, info->right);
    Game::Lua::PushNumber(L, info->top);
    Game::Lua::PushNumber(L, info->bottom);
    CallScript(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXCOORD, L);

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
    auto it = g_applied.find(region);
    if (it == g_applied.end())
        return 0;
    if (RegionTexture(region) != it->second.hTexture) {
        g_applied.erase(it);
        return 0;
    }
    Game::Lua::PushString(L, it->second.name.c_str());
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
// Here an atlas IS its texcoords: there is no separate layer to hold the
// sub-rect, so the two pieces of state share one slot. Identity for an atlas'd
// texture is therefore the atlas's own rect, and resetting to a literal 0,0,1,1
// would show the whole sheet where retail shows the sprite. Restoring the rect
// is what reproduces the visible result.
int __fastcall Script_ResetTexCoord(void *L) {
    void *region = Game::Lua::ResolveObject(L, 1);
    if (region == nullptr)
        return 0;

    float left = 0.0f, right = 1.0f, top = 0.0f, bottom = 1.0f;
    // Same staleness guard GetAtlas uses: a texture pointed somewhere else since
    // is no longer an atlas, so identity for it is the whole texture.
    auto it = g_applied.find(region);
    if (it != g_applied.end()) {
        if (RegionTexture(region) != it->second.hTexture) {
            g_applied.erase(it);
        } else if (const Info *info = Find(it->second.name.c_str())) {
            left = info->left;
            right = info->right;
            top = info->top;
            bottom = info->bottom;
        }
    }

    Game::Lua::SetTop(L, 1); // (self)
    Game::Lua::PushNumber(L, left);
    Game::Lua::PushNumber(L, right);
    Game::Lua::PushNumber(L, top);
    Game::Lua::PushNumber(L, bottom);
    CallScript(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXCOORD, L);
    Game::Lua::SetTop(L, 0);
    return 0;
}

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
