// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `C_NamePlate.GetNamePlates()` — returns nameplate Frame objects,
// matching modern WoW's signature.
//
// One push path covers both kinds of nameplate. Addon-created plates
// (pfUI, TidyPlates) already carry a Lua-registry refkey at `+0x08`
// from `CreateFrame`; engine-created ones start at 0 and get theirs
// lazily on first push. Both end at `rawgeti(REGISTRY, frame + 0x08)`,
// the engine's canonical wrapper, so identity is stable across calls
// and addon fields set on it (pfUI's `plate.nameplate`) survive every
// roundtrip. `UI::FrameObject::Push` owns that — see its note for the
// pfUI-divergence story that motivated delegating to the engine, and
// for the CObject refcount `ScriptRegister` bumps.
//
// `C_NamePlate.GetNamePlateGUIDs()` — companion returning the GUID
// strings of the same set of units. Cheapest enumeration when an addon
// only needs the GUIDs.
//
// Vanilla 1.12 stores each unit's nameplate pointer at `CGUnit + 0xE60`
// (verified via `FUN_006086E0`'s "ensure nameplate exists" path). The
// nameplate caches the unit's GUID at `+0x4E8` for back-lookup and
// carries a Storm link node at `+0x4DC`/`+0x4E0` (prev link-address /
// next base-address).
//
// There IS a central list of live nameplates — header `{0x00C4D928,
// 0x00C4D92C}`, with a free/recycle list at `{0x00C4D91C, 0x00C4D920}`
// — but we deliberately don't enumerate through it:
//
// - `FUN_00608870` re-sorts that list by screen depth on EVERY plate's
//   position update, unlinking and reinserting the node. It is in
//   motion throughout the render, whereas no nameplate code touches
//   the object hash table.
// - `FUN_006087F0` recycles a pooled node without clearing `+0x4E8`,
//   so a node's cached GUID is stale until `FUN_007CB6D0` rebinds it.
//   `unit + 0xE60` is the only authoritative live binding.
// - We need the unit and its instance block for the GUID regardless,
//   and the hash walk yields all three together.
//
// So we enumerate by walking the local-player-anchored object hash
// table instead (`player + 0x1C` = bucket array, `player + 0x24` =
// mask). Each bucket header stores the link-field offset at byte 0 and
// the chain-head pointer at byte 8 — Storm's intrusive-hash pattern.
// Filter by `TYPEMASK_UNIT` (`flags & 0x08` at `*(entry+8) + 8`) and
// check `+0xE60` for a non-null nameplate pointer.

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "nameplate/Walk.h"
#include "object/Resolve.h"
#include "ui/FrameObject.h"

#include <cstdint>

namespace NamePlate::Info {

namespace {

using NamePlate::Walk::ForEachNamePlatedUnit;
using NamePlate::Walk::kOffUnitNamePlate;

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);

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
// Thin wrapper over the shared `UI::FrameObject::Push` — nameplates and
// chat bubbles are both engine-created-in-C++ `CSimpleFrame`s that need
// the same lazy `ScriptRegister` → `rawgeti` push so addons get the
// engine's canonical wrapper (see that helper for the pfUI-divergence
// rationale that motivated delegating to the engine).
void PushNamePlateFrame(void *L, void *nameplate) {
    UI::FrameObject::Push(L, nameplate);
}

// GUID → nameplate Frame pushed on stack. Pushes nil if the GUID
// doesn't resolve to a visible CGUnit, or the unit has no allocated
// nameplate.
static void PushNamePlateForGUID(void *L, uint64_t guid) {
    if (guid == 0) {
        Game::Lua::PushNil(L);
        return;
    }
    auto *unit = static_cast<uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, "NamePlate", 0x172));
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
