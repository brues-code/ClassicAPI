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
using ScriptRegister_t = void(__fastcall *)(void *this_, void *edx_unused,
                                              const char *name);
using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);
using ResolveByGUID_t = void *(__fastcall *)(int type, const char *debugName,
                                              uint32_t guidLo, uint32_t guidHi,
                                              int priority);

} // namespace

static int __fastcall Script_GetNamePlates(void *L) {
    Game::Lua::NewTable(L);
    int nextIndex = 1;
    ForEachNamePlatedUnit(
        [L, &nextIndex](const uint8_t *, const uint8_t *nameplate,
                         const uint8_t *) {
            Game::Lua::PushNumber(L, static_cast<double>(nextIndex++));
            // Shared helper validates refkey freshness (defensive vs.
            // stale-across-reload) and falls back to a fresh wrapper
            // if the registry slot doesn't actually hold the frame
            // table any more.
            PushNamePlateFrame(L, const_cast<uint8_t *>(nameplate));
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

// Exported via `nameplate/Walk.h` so `Events.cpp` can reuse the same
// frame-push path. Internal callers (Script_GetNamePlates etc.) call
// it through the unqualified name (they live in the same namespace).
//
// Delegate wrapper construction to the engine's own
// `FrameScript_Object::ScriptRegister`. First call for a never-seen
// nameplate builds a `{[0] = lightuserdata}` table with the
// framescript metatable, `luaL_ref`s it into the registry, and
// writes the refkey to `nameplate + 0x08` (`OFF_COBJECT_LUA_REGISTRY_REF`).
// Subsequent calls find a populated refkey and just rawgeti.
//
// Why delegate instead of building our own wrapper:
//
// - **Single source of truth.** Every code path that pushes this
//   frame — ours via this function, addons' `CreateFrame("Type",
//   "name", plate)` extracting the parent, the engine's own internal
//   pushers — funnels through `rawgeti(REGISTRY, plate+0x08)` and
//   gets the same Lua table. Earlier "build our own wrapper +
//   `luaL_ref` it" approach kept a side cache, but pfUI's
//   `CreateFrame("Button", "name", parent)` inside `NAME_PLATE_CREATED`
//   ends up causing the engine to register the parent itself —
//   the engine's wrapper diverges from ours, and later
//   `GetNamePlateForUnit` calls return the engine's bare wrapper
//   instead of our decorated one. Addon fields on the wrapper
//   (`plate.nameplate = styledButton`) disappear from the API's
//   perspective even though they still exist on the orphaned
//   table.
//
// - **Refcount-pinning is benign here.** `ScriptRegister`
//   unconditionally increments `nameplate + 0x04`, which normally
//   keeps the engine from GC-ing the CObject. Nameplates are
//   pool-managed and never destroyed during a session, so pinning
//   doesn't actually leak anything. We call `ScriptRegister` at
//   most once per nameplate pointer (gated by `refKey <= 0`), so
//   the refcount tops out at one increment per pool slot.
//
// Defensive fallback: if the registry slot exists but isn't a table
// any more (some other code freed it), we re-register. This shouldn't
// happen in practice — engines only unref on frame destruction —
// but a stale type check keeps callers from ever seeing a non-table.
void PushNamePlateFrame(void *L, void *nameplate) {
    auto rawgeti = reinterpret_cast<LuaRawGetI_t>(
        Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT);
    auto scriptRegister = reinterpret_cast<ScriptRegister_t>(
        static_cast<uintptr_t>(Offsets::FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER));

    int refKey = *reinterpret_cast<const int *>(
        static_cast<uint8_t *>(nameplate) + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
    if (refKey <= 0) {
        scriptRegister(nameplate, nullptr, nullptr);
        refKey = *reinterpret_cast<const int *>(
            static_cast<uint8_t *>(nameplate) + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
    }

    rawgeti(L, Game::Lua::REGISTRY_INDEX, refKey);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        return;
    Game::Lua::SetTop(L, -2);

    // Slot got freed somewhere — re-register to rebuild a fresh
    // wrapper and slot. Zero the refcount so `ScriptRegister`
    // re-enters its build branch.
    *reinterpret_cast<int *>(
        static_cast<uint8_t *>(nameplate) + Offsets::OFF_COBJECT_LUA_REFCOUNT) = 0;
    scriptRegister(nameplate, nullptr, nullptr);
    refKey = *reinterpret_cast<const int *>(
        static_cast<uint8_t *>(nameplate) + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
    rawgeti(L, Game::Lua::REGISTRY_INDEX, refKey);
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
