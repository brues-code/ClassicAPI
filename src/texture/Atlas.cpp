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
#include "frame/Region.h"
#include "frame/RegionDestruction.h"
#include "texture/Slice.h"
#include "texture/Wrap.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace Texture::Atlas {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

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
            info.transposed = e.transposed;
            info.sliced = e.slice.left != 0 || e.slice.top != 0 || e.slice.right != 0 ||
                          e.slice.bottom != 0;
            info.sliceLeft = e.slice.left;
            info.sliceTop = e.slice.top;
            info.sliceRight = e.slice.right;
            info.sliceBottom = e.slice.bottom;
            info.sliceMode = e.slice.mode;
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
// lets this module skip a `Texture::Construct` listener.
// Texture coordinates in the engine's own corner order: UL, LL, UR, LR, each an
// (x, y) pair — the order GetTexCoord pushes them in. Identity selects all of
// whatever the texture points at.
constexpr float kIdentityCoords[8] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};

struct Applied {
    std::string name;
    // The registry entry, which stays valid for the life of the process
    // (Find); a re-registration rewrites it in place.
    const Info *info = nullptr;
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

// Map sprite-space corners onto the atlas's rectangle within its file; a
// transposed sprite's x runs down the rectangle and its y across it.
void Compose(const Info &info, const float user[8], float out[8]) {
    const float width = info.right - info.left;
    const float height = info.bottom - info.top;
    for (int i = 0; i < 4; ++i) {
        const float x = user[i * 2], y = user[i * 2 + 1];
        out[i * 2] = info.left + (info.transposed ? y : x) * width;
        out[i * 2 + 1] = info.top + (info.transposed ? x : y) * height;
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
               : Game::Lua::CallScript(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXCOORD, L);
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
    if (info->sliced) {
        Game::Lua::PushString(L, "sliceData");
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "marginLeft", info->sliceLeft);
        Game::Lua::SetFieldNumber(L, "marginTop", info->sliceTop);
        Game::Lua::SetFieldNumber(L, "marginRight", info->sliceRight);
        Game::Lua::SetFieldNumber(L, "marginBottom", info->sliceBottom);
        Game::Lua::SetFieldNumber(L, "sliceMode", info->sliceMode);
        Game::Lua::SetTable(L, -3);
    }
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

// The coordinates SetAtlas keeps, in sprite space: what the addon last asked
// for. A texture showing an atlas has them on record. Any other texture holds
// them in the engine; on a tiled axis there, Texture::Tile has replaced each
// non-zero value with its repeat count, and 4.3.4 allows only 0 and 1 on a
// tiled axis, so a non-zero value is read back as 1.
void KeptCoords(void *region, float user[8]) {
    if (const Applied *entry = LiveEntry(region)) {
        for (int i = 0; i < 8; ++i)
            user[i] = entry->user[i];
        return;
    }
    const float *coords = Game::Ptr<float>(region, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    bool tileH, tileV;
    Texture::Wrap::Tiles(region, tileH, tileV);
    for (int i = 0; i < 4; ++i) {
        const float x = coords[i * 2], y = coords[i * 2 + 1];
        user[i * 2] = (tileH && x != 0.0f) ? 1.0f : x;
        user[i * 2 + 1] = (tileV && y != 0.0f) ? 1.0f : y;
    }
}

// `texture:SetAtlas(atlas [, useAtlasSize, filterMode, resetTexCoords,
//                   wrapModeHorizontal, wrapModeVertical])`
//
// Retail keeps the texture's coordinates and applies them inside the new
// sprite unless resetTexCoords is true: NineSlice sets a piece's mirrored
// coordinates before its atlas ("Change texture coordinates before applying
// atlas"), and code that reuses pooled textures passes true to drop a flip an
// earlier use set (Blizzard_OrderHallTalents.lua).
//
// It also re-applies the atlas's own wrap mode (Blizzard_LegacyChallengeButton
// .lua: "SetAtlas re-applies the atlas wrap mode, clearing the tiling set in
// XML"), so the tile flags become the atlas's tiling (Texture::Wrap::
// ForAtlas) on each axis where it can repeat (Repeats).
//
// filterMode and the wrap mode arguments are accepted and ignored: 1.12 texture
// regions have no filter to set, and dropping them silently is what lets modern
// call sites pass through unchanged.
int __fastcall Script_SetAtlas(void *L) {
    void *region = Game::Lua::ResolveTexture(L);
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
    const bool resetTexCoords = Game::Lua::ToBoolean(L, 5) != 0;

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

    Applied applied;
    applied.name = info->name;
    applied.info = info;
    if (resetTexCoords) {
        for (int i = 0; i < 8; ++i)
            applied.user[i] = kIdentityCoords[i];
    } else {
        KeptCoords(region, applied.user);
    }

    // The atlas's wrap goes in before the file loads, so the load takes it.
    Texture::Wrap::ForAtlas(region, Repeats(*info, 0), Repeats(*info, 1));

    // Reshaping the stack for each delegated handler leaves `info` untouched — it
    // points into the registry, which none of these calls can reach.
    Game::Lua::SetTop(L, 1); // (self)
    Game::Lua::PushString(L, info->file.c_str());
    Game::Lua::CallScript(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXTURE, L);
    Texture::Wrap::Settle(region);

    // On record before the coordinates are written, so Texture::Tile counts
    // them against the atlas.
    applied.hTexture = RegionTexture(region);
    g_applied[region] = applied;

    float corners[8];
    Compose(*info, applied.user, corners);
    WriteCorners(L, corners);

    if (useAtlasSize && info->width > 0.0f && info->height > 0.0f) {
        Game::Lua::SetTop(L, 1); // (self)
        Game::Lua::PushNumber(L, info->width);
        Game::Lua::CallScript(Offsets::FUN_SCRIPT_REGION_SETWIDTH, L);
        Game::Lua::SetTop(L, 1); // (self)
        Game::Lua::PushNumber(L, info->height);
        Game::Lua::CallScript(Offsets::FUN_SCRIPT_REGION_SETHEIGHT, L);
    }

    // Retail's SetAtlas loads the atlas's slice data along with its rect.
    Texture::Slice::ApplyAtlas(region, *info);

    Game::Lua::SetTop(L, 0);
    return 0;
}

// `texture:GetAtlas()` — the atlas last set on this texture, or nil if the
// texture has been pointed somewhere else since.
int __fastcall Script_GetAtlas(void *L) {
    void *region = Game::Lua::ResolveTexture(L);
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
    void *region = Game::Lua::ResolveTexture(L);
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
// Retail holds the addon's coordinates apart from the atlas rect, so they outlive
// the atlas: SetTexture(file) shows the file through them, and a later SetAtlas
// keeps them. Here the engine's one set of coordinates holds the composed rect,
// so dropping the atlas writes the addon's own back. Left in place, the next
// SetAtlas would read the old rect as the addon's and compose it a second time
// (a pooled icon going atlas -> SetTexture(nil) -> atlas shows a corner of the
// sheet), and a file set now would show only the old rect's part of itself.
// The C setter keeps the Lua stack intact for the handler that runs after it.
//
// SetAtlas records its entry only after its own SetTexture call, so this running
// first costs it nothing: the coordinates it writes back are the ones SetAtlas
// already kept, and it composes over them.
ScriptFn_t g_setTextureOriginal = nullptr;

using SetCorners_t = void(__fastcall *)(void *texture, void *edx, const float *corners);

int __fastcall SetTexture_h(void *L) {
    if (!g_applied.empty()) {
        void *region = Game::Lua::ResolveObject(L, 1);
        if (const Applied *entry = LiveEntry(region)) {
            float user[8];
            for (int i = 0; i < 8; ++i)
                user[i] = entry->user[i];
            g_applied.erase(region);
            reinterpret_cast<SetCorners_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD_CORNERS)(region, nullptr, user);
        }
    }
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

// --- documentation ----------------------------------------------------------

const Game::Doc::Field kSetAtlasArgs[] = {
    Game::Doc::Req("atlas", "textureAtlas",
                   "An unknown name leaves the texture showing what it showed before."),
    Game::Doc::Opt("useAtlasSize", "bool", "false",
                   "True also resizes the texture to the art's own size."),
    Game::Doc::Opt("filterMode", "FilterMode", nullptr, "Ignored."),
    Game::Doc::Opt("resetTexCoords", "bool", "false",
                   "True shows the whole atlas. Otherwise the texture's texture "
                   "coordinates stay and select part of the atlas."),
    Game::Doc::Opt("wrapModeHorizontal", "cstring", nullptr, "Ignored."),
    Game::Doc::Opt("wrapModeVertical", "cstring", nullptr, "Ignored."),
};
const Game::Doc::Function kSetAtlas{
    "Sets the texture to a named piece of art: its file and the rectangle inside it.",
    kSetAtlasArgs, {}};

const Game::Doc::Field kGetAtlasRets[] = {
    Game::Doc::Opt("atlas", "textureAtlas", nullptr,
                   "Nil when the texture no longer shows the atlas last set on it."),
};
const Game::Doc::Function kGetAtlas{"Returns the name of the atlas the texture shows.", {},
                                    kGetAtlasRets};

const Game::Doc::Function kResetTexCoord{
    "Clears a texture-coordinate crop so the texture shows all of its atlas or file.", {}, {}};

const Game::Doc::Field kAtlasInfoFields[] = {
    Game::Doc::Req("elementName", "cstring", "The name in its canonical spelling."),
    Game::Doc::Req("width", "number"),
    Game::Doc::Req("height", "number"),
    Game::Doc::Req("leftTexCoord", "number"),
    Game::Doc::Req("rightTexCoord", "number"),
    Game::Doc::Req("topTexCoord", "number"),
    Game::Doc::Req("bottomTexCoord", "number"),
    Game::Doc::Req("tilesHorizontally", "bool"),
    Game::Doc::Req("tilesVertically", "bool"),
    Game::Doc::Opt("sliceData", "UITextureSliceData", nullptr,
                   "Set only for an atlas that draws as a nine-slice."),
    Game::Doc::Opt("file", "fileID", nullptr, "Never set; the texture is named by filename."),
    Game::Doc::Opt("filename", "string", nullptr, "Path of the texture that holds the art."),
};
const Game::Doc::Structure kAtlasInfo{"AtlasInfo", "Texture", kAtlasInfoFields};

const Game::Doc::Field kSliceDataFields[] = {
    Game::Doc::Req("marginLeft", "number", "In the art's pixels."),
    Game::Doc::Req("marginTop", "number"),
    Game::Doc::Req("marginRight", "number"),
    Game::Doc::Req("marginBottom", "number"),
    Game::Doc::Req("sliceMode", "UITextureSliceMode"),
};
const Game::Doc::Structure kSliceData{"UITextureSliceData", "UITextureConstants", kSliceDataFields};

const Game::Doc::Field kAtlasArgs[] = {
    Game::Doc::Req("atlas", "textureAtlas"),
};
const Game::Doc::Field kGetAtlasInfoRets[] = {
    Game::Doc::Opt("info", "AtlasInfo", nullptr, "Nil when the name is not known."),
};
const Game::Doc::Function kGetAtlasInfo{"Returns a description of an atlas.", kAtlasArgs,
                                        kGetAtlasInfoRets};

const Game::Doc::Field kGetAtlasExistsRets[] = {
    Game::Doc::Req("atlasExists", "bool"),
};
const Game::Doc::Function kGetAtlasExists{"Returns whether an atlas name is known.", kAtlasArgs,
                                          kGetAtlasExistsRets};

const Game::Doc::Field kGetAtlasIDRets[] = {
    Game::Doc::Opt("atlasID", "number", nullptr,
                   "Negative for an atlas added with RegisterAtlas; nil when the name is not "
                   "known."),
};
const Game::Doc::Function kGetAtlasID{"Returns the ID of the sheet that holds an atlas.",
                                      kAtlasArgs, kGetAtlasIDRets};

const Game::Doc::Field kGetAtlasElementIDRets[] = {
    Game::Doc::Opt("elementID", "number", nullptr,
                   "Negative for an atlas added with RegisterAtlas; nil when the name is not "
                   "known."),
};
const Game::Doc::Function kGetAtlasElementID{"Returns the ID of one atlas element.", kAtlasArgs,
                                             kGetAtlasElementIDRets};

const Game::Doc::Field kGetAtlasElementsRets[] = {
    Game::Doc::Req("atlases", "table",
                   "An array of every known atlas name, in alphabetical order."),
};
const Game::Doc::Function kGetAtlasElements{"Returns the names of all known atlases.", {},
                                            kGetAtlasElementsRets};

const Game::Doc::Field kRegisterAtlasArgs[] = {
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("file", "string", "Texture path, without the file extension."),
    Game::Doc::Req("width", "number", "Size of the art in pixels, used by useAtlasSize."),
    Game::Doc::Req("height", "number"),
    Game::Doc::Opt("left", "number", "0", "Rectangle edges inside the file, from 0 to 1."),
    Game::Doc::Opt("right", "number", "1"),
    Game::Doc::Opt("top", "number", "0"),
    Game::Doc::Opt("bottom", "number", "1"),
    Game::Doc::Opt("tilesHorizontally", "bool", "false"),
    Game::Doc::Opt("tilesVertically", "bool", "false"),
};
const Game::Doc::Field kRegisterAtlasRets[] = {
    Game::Doc::Req("success", "bool", "Always true."),
};
const Game::Doc::Function kRegisterAtlas{
    "Adds or replaces an atlas name that points at an addon's own art; a replaced name keeps "
    "its IDs.",
    kRegisterAtlasArgs, kRegisterAtlasRets, nullptr, true};

const Game::Doc::Method g_textureMethodDocs[] = {
    {"SetAtlas", &kSetAtlas},
    {"GetAtlas", &kGetAtlas},
    {"ResetTexCoord", &kResetTexCoord},
};

// --- registration -----------------------------------------------------------

const Game::Lua::FrameMethodEntry g_textureMethods[] = {
    {"SetAtlas", &Script_SetAtlas},
    {"GetAtlas", &Script_GetAtlas},
    {"ResetTexCoord", &Script_ResetTexCoord},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_textureMethods,
        g_textureMethodDocs);

    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasInfo", &Script_GetAtlasInfo,
                                     &kGetAtlasInfo);
    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasExists", &Script_GetAtlasExists,
                                     &kGetAtlasExists);
    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasID", &Script_GetAtlasID,
                                     &kGetAtlasID);
    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasElementID",
                                     &Script_GetAtlasElementID, &kGetAtlasElementID);
    Game::Lua::RegisterTableFunction("C_Texture", "GetAtlasElements",
                                     &Script_GetAtlasElements, &kGetAtlasElements);
    Game::Lua::RegisterTableFunction("C_Texture", "RegisterAtlas", &Script_RegisterAtlas,
                                     &kRegisterAtlas);

    Game::Lua::RegisterGlobalFunction("_classicapi_DumpAtlasMisses", &Script_DumpAtlasMisses,
                                      nullptr);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

// The pool can hand a destroyed region's address to a new one.
void OnRegionDestroyed(void *region) { g_applied.erase(region); }

const Frame::RegionDestruction::AutoSubscribe _destructionSub{&OnRegionDestroyed};

// A <Color> under an atlas texture tints the atlas, as 4.3.4's loader tints a
// texture that already has its image (SetVertexColor) and retail tints an atlas
// (FlatPanelBackgroundTemplate's corners, which PortraitFrameFlatBaseMixin
// re-tints with SetVertexColor). 1.12's loader turned the colour into a solid
// image before the atlas replaced it, so the colour goes on here, after.
// An `alpha` attribute keeps its alpha byte: Xml::NativeSetters sets it as
// the colour's alpha, before or after this.
void TintFromColor(void *object, const void *node, void *status) {
    const void *colorNode = nullptr;
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c))
        if (Xml::TagIs(c, "Color"))
            colorNode = c;
    if (colorNode == nullptr)
        return;
    uint32_t bgra = 0;
    Xml::ReadColor(colorNode, bgra, status);
    const char *alpha = Xml::Attr(node, "alpha");
    if (alpha != nullptr && *alpha != '\0')
        bgra = (bgra & 0x00FFFFFFu) | (Frame::Region::Color(object) & 0xFF000000u);
    Frame::Region::SetColor(object, bgra);
}

// The XML `<Texture atlas="name" useAtlasSize="true">`: texture:SetAtlas(name,
// useAtlasSize), run as its own Lua call so the loader's stack is left alone.
void OnTextureNode(void *object, const void *node, void *status) {
    const char *name = Xml::Attr(node, "atlas");
    void *L = Game::Lua::State();
    if (name == nullptr || *name == '\0' || L == nullptr)
        return;
    const char *useSize = Xml::Attr(node, "useAtlasSize");
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushCClosure(L, &Script_SetAtlas, 0);
    Game::Lua::PushObject(L, object);
    Game::Lua::PushString(L, name);
    Game::Lua::PushBool(L, useSize != nullptr && Game::StringToFlag(useSize));
    Game::Lua::PCall(L, 3, 0, 0);
    Game::Lua::SetTop(L, top);
    if (Shows(object))
        TintFromColor(object, node, status);
}

const Xml::TypeLoad::AutoSubscribe _xmlSub{Xml::TypeLoad::TEXTURE, &OnTextureNode};

} // namespace

bool Shows(void *region) { return LiveEntry(region) != nullptr; }

bool ToFile(void *region, float corners[8]) {
    const Applied *entry = LiveEntry(region);
    const Info *info = entry != nullptr ? Find(entry->name.c_str()) : nullptr;
    if (info == nullptr)
        return false;
    float out[8];
    Compose(*info, corners, out);
    for (int i = 0; i < 8; ++i)
        corners[i] = out[i];
    return true;
}

bool ToSprite(void *region, float corners[8]) {
    const Info *info = Current(region);
    if (info == nullptr)
        return false;
    const float width = info->right - info->left;
    const float height = info->bottom - info->top;
    if (width == 0.0f || height == 0.0f)
        return false;
    for (int i = 0; i < 4; ++i) {
        const float x = (corners[i * 2] - info->left) / width;
        const float y = (corners[i * 2 + 1] - info->top) / height;
        corners[i * 2] = info->transposed ? y : x;
        corners[i * 2 + 1] = info->transposed ? x : y;
    }
    return true;
}

int __fastcall SetAtlas(void *L) { return Script_SetAtlas(L); }

const Info *Current(void *region) {
    const Applied *entry = LiveEntry(region);
    return entry != nullptr ? entry->info : nullptr;
}

bool Repeats(const Info &info, int axis) {
    if (info.transposed)
        return false;
    if (axis == 0)
        return info.tilesHorizontally && info.left == 0.0f && info.right == 1.0f;
    return info.tilesVertically && info.top == 0.0f && info.bottom == 1.0f;
}

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
