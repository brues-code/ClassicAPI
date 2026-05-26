// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `C_NamePlate.GetNamePlates()` — returns nameplate Frame objects,
// matching modern WoW's signature. Two paths because vanilla has two
// kinds of nameplates:
//
// 1. **Addon-created** (pfUI, TidyPlates, etc.) — already registered
//    with Lua via `CreateFrame`. Their Lua-registry ref-key sits at
//    `+0x08`; we push `registry[refKey]` to return the cached
//    wrapper. Identity stable across calls.
//
// 2. **Default vanilla nameplates** — created internally without ever
//    calling `CreateFrame`, so `+0x08` is 0. We build a fresh wrapper
//    table per call: `{[0] = lightuserdata(frame)}` with the global
//    `__framescript_meta` metatable. Methods work
//    (`:GetWidth()` / `:GetAlpha()` / etc.) but the wrapper isn't
//    cached engine-side — calling `GetNamePlates()` again returns a
//    different table for the same frame. Don't compare wrappers by
//    identity, and don't cache them across the unit going out of
//    range (the underlying frame may be freed).
//
// We deliberately don't call the engine's frame-registration helper
// (`FUN_00701BD0`) for the unregistered case — it increments a Lua
// refcount on the frame that's never decremented, pinning the frame
// in memory.
//
// `C_NamePlate.GetNamePlateGUIDs()` — companion returning the GUID
// strings of the same set of units, regardless of registration
// state. Cheapest enumeration when an addon only needs the GUIDs.
//
// Vanilla 1.12 stores each unit's nameplate pointer at `CGUnit + 0xE60`
// (verified via `FUN_006086E0`'s "ensure nameplate exists" path). The
// nameplate also caches the unit's GUID at `+0x4E8` for back-lookup.
// There's no central "active nameplates" list — the engine maintains
// per-unit pointers updated by per-unit state-change handlers.
//
// To enumerate, we walk the local-player-anchored object hash table
// (`player + 0x1C` = bucket array, `player + 0x24` = mask). Each
// bucket header stores the link-field offset at byte 0 and the
// chain-head pointer at byte 8 — Storm's intrusive-hash pattern.
// Filter by `TYPEMASK_UNIT` (`flags & 0x08` at `*(entry+8) + 8`) and
// check `+0xE60` for a non-null nameplate pointer.

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "nameplate/Walk.h"

#include <cstdint>

namespace NamePlate::Info {

namespace {

using NamePlate::Walk::ForEachNamePlatedUnit;
using NamePlate::Walk::kOffUnitNamePlate;

using LuaRawGetI_t = void(__fastcall *)(void *L, int idx, int n);
using LuaPushLightUserdata_t = void(__fastcall *)(void *L, void *p);
using LuaSetMetatable_t = int(__fastcall *)(void *L, int idx);
using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);
using ResolveByGUID_t = void *(__fastcall *)(int type, const char *debugName,
                                              uint32_t guidLo, uint32_t guidHi,
                                              int priority);

constexpr uintptr_t kFunLuaPushLightUserdata = 0x006F3A20;
constexpr uintptr_t kFunLuaSetMetatable = 0x006F4020;
constexpr int kLuaGlobalsIndex = -10001;
constexpr const char *kFrameMetatableGlobal = "__framescript_meta";

// Build a fresh frame wrapper on the Lua stack: `{[0] = frame}` with
// `_G["__framescript_meta"]` as metatable. Same shape the engine's
// frame-registration helper builds, minus the registry-cache step
// (which we skip to avoid pinning the frame's refcount).
void PushFreshFrameWrapper(void *L, void *frame) {
    auto pushLight = reinterpret_cast<LuaPushLightUserdata_t>(
        kFunLuaPushLightUserdata);
    auto setMetatable = reinterpret_cast<LuaSetMetatable_t>(
        kFunLuaSetMetatable);

    Game::Lua::NewTable(L);
    Game::Lua::PushNumber(L, 0);
    pushLight(L, frame);
    Game::Lua::RawSet(L, -3);

    Game::Lua::PushString(L, kFrameMetatableGlobal);
    Game::Lua::GetTable(L, kLuaGlobalsIndex);
    setMetatable(L, -2);
}

} // namespace

static int __fastcall Script_GetNamePlates(void *L) {
    Game::Lua::NewTable(L);
    auto rawgeti = reinterpret_cast<LuaRawGetI_t>(
        Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT);
    int nextIndex = 1;
    ForEachNamePlatedUnit(
        [L, rawgeti, &nextIndex](const uint8_t *, const uint8_t *nameplate,
                                  const uint8_t *) {
            Game::Lua::PushNumber(L, static_cast<double>(nextIndex++));
            // The engine initializes `OFF_COBJECT_LUA_REGISTRY_REF` to
            // `LUA_NOREF` (`-2`) for internally-created frames; a real
            // refkey is always a positive integer from `luaL_ref`.
            // Treat anything `<= 0` as unregistered.
            const int refKey = *reinterpret_cast<const int *>(
                nameplate + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
            if (refKey > 0) {
                rawgeti(L, Game::Lua::REGISTRY_INDEX, refKey);
            } else {
                PushFreshFrameWrapper(L, const_cast<uint8_t *>(nameplate));
            }
            Game::Lua::SetTable(L, -3);
        });
    return 1;
}

static int __fastcall Script_GetNamePlateGUIDs(void *L) {
    Game::Lua::NewTable(L);
    int nextIndex = 1;
    ForEachNamePlatedUnit(
        [L, &nextIndex](const uint8_t *, const uint8_t *,
                         const uint8_t *instance) {
            const uint64_t guid = *reinterpret_cast<const uint64_t *>(instance);
            if (guid == 0)
                return;
            char buf[Guid::STRING_SIZE];
            Game::Lua::PushNumber(L, static_cast<double>(nextIndex++));
            Game::Lua::PushString(L,
                Guid::FormatAsString(guid, buf, sizeof buf));
            Game::Lua::SetTable(L, -3);
        });
    return 1;
}

// Pushes a nameplate Frame onto the stack — registered or fresh
// wrapper depending on whether the engine assigned a real refKey.
static void PushNamePlateFrame(void *L, void *nameplate) {
    const int refKey = *reinterpret_cast<const int *>(
        static_cast<uint8_t *>(nameplate) + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
    if (refKey > 0) {
        auto rawgeti = reinterpret_cast<LuaRawGetI_t>(
            Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT);
        rawgeti(L, Game::Lua::REGISTRY_INDEX, refKey);
    } else {
        PushFreshFrameWrapper(L, nameplate);
    }
}

// GUID → nameplate Frame pushed on stack. Pushes nil if the GUID
// doesn't resolve to a visible CGUnit, or the unit has no allocated
// nameplate.
static void PushNamePlateForGUID(void *L, uint64_t guid) {
    if (guid == 0) {
        Game::Lua::PushNil(L);
        return;
    }
    auto resolve = reinterpret_cast<ResolveByGUID_t>(
        static_cast<uintptr_t>(Offsets::FUN_OBJECT_RESOLVE_BY_GUID));
    auto *unit = static_cast<uint8_t *>(
        resolve(Offsets::OBJ_TYPE_UNIT, "NamePlate",
                static_cast<uint32_t>(guid),
                static_cast<uint32_t>(guid >> 32),
                0x172));
    if (unit == nullptr) {
        Game::Lua::PushNil(L);
        return;
    }
    auto *nameplate = *reinterpret_cast<uint8_t *const *>(
        unit + kOffUnitNamePlate);
    if (nameplate == nullptr) {
        Game::Lua::PushNil(L);
        return;
    }
    PushNamePlateFrame(L, nameplate);
}

// `C_NamePlate.GetNamePlateForUnit(unitToken)` — returns the
// nameplate Frame for the given unit, or `nil` if the unit has no
// nameplate (out of range, hidden, etc.). Resolves the token to a
// GUID via the engine's `FUN_TOKEN_TO_GUID` so distant party/raid
// members can be queried.
static int __fastcall Script_GetNamePlateForUnit(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    auto tokenToGuid = reinterpret_cast<TokenToGUID_t>(
        static_cast<uintptr_t>(Offsets::FUN_TOKEN_TO_GUID));
    PushNamePlateForGUID(L, tokenToGuid(token));
    return 1;
}

// `C_NamePlate.GetNamePlateForGUID(guidString)` — same as
// `GetNamePlateForUnit` but takes a `"0xHHHHHHHHHHHHHHHH"` GUID
// string. Useful for handling the `NAME_PLATE_UNIT_ADDED` /
// `_REMOVED` events whose payload is a GUID, not a unit token.
static int __fastcall Script_GetNamePlateForGUID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const char *guidStr = Game::Lua::ToString(L, 1);
    uint64_t guid = 0;
    if (guidStr == nullptr || !Guid::Parse(guidStr, &guid)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushNamePlateForGUID(L, guid);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlates",
                                     &Script_GetNamePlates);
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateGUIDs",
                                     &Script_GetNamePlateGUIDs);
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateForUnit",
                                     &Script_GetNamePlateForUnit);
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateForGUID",
                                     &Script_GetNamePlateForGUID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace NamePlate::Info
