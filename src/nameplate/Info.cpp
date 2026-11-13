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
// Enumeration walks the engine's list of shown nameplates and resolves each
// plate's unit by the GUID it carries (`NamePlate::Walk`, see Offsets.h
// `VAR_NAMEPLATE_LIST_HEAD` for the engine functions behind it). An earlier
// version walked the whole object hash table every call instead, on the
// belief that the list was unsafe to read; it is re-sorted only by plain
// engine code on the main thread, and every plate on it is bound.

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

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);

} // namespace

static int __fastcall Script_GetNamePlates(void *L) {
    Game::Lua::NewTable(L);
    int nextIndex = 1;
    ForEachNamePlatedUnit(
        [L, &nextIndex](const uint8_t *, const uint8_t *nameplate, uint64_t) {
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
        [L, &nextIndex](const uint8_t *, const uint8_t *, uint64_t guid) {
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
    // During `NAME_PLATE_UNIT_REMOVED` the answer is the frame being removed,
    // as on retail. Live state can't give it: the unit→plate binding is gone
    // (or already names the unit's new frame), and the unit itself is often
    // out of the object table.
    const void *nameplate = NamePlate::Events::PlateBeingRemoved(guid);
    if (nameplate == nullptr) {
        auto *unit = static_cast<uint8_t *>(
            Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, "NamePlate", 0x172));
        if (unit != nullptr)
            nameplate = *reinterpret_cast<uint8_t *const *>(unit + Offsets::OFF_UNIT_NAMEPLATE);
    }

    if (nameplate == nullptr) {
        Game::Lua::PushNil(L);
        return;
    }
    PushNamePlateFrame(L, const_cast<void *>(nameplate));
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

static const Game::Doc::Field kGetNamePlatesArgs[] = {
    Game::Doc::Opt("includeForbidden", "bool", "false", "Ignored."),
};
static const Game::Doc::Field kGetNamePlatesRets[] = {
    Game::Doc::Req("nameplates", "table",
                   "An array of the nameplate frames that addons can reach, in no set order."),
};
static const Game::Doc::Function kGetNamePlates{"Returns the nameplate frames now in use.",
                                                kGetNamePlatesArgs, kGetNamePlatesRets};

static const Game::Doc::Field kGetNamePlateGUIDsRets[] = {
    Game::Doc::Req("guids", "table",
                   "An array of the GUIDs of every unit with a nameplate, in no set order."),
};
static const Game::Doc::Function kGetNamePlateGUIDs{
    "Returns the GUIDs of the units that have a nameplate.", {}, kGetNamePlateGUIDsRets,
    nullptr, true};

static const Game::Doc::Field kGetNamePlateForUnitArgs[] = {
    Game::Doc::Req("unitToken", "UnitToken"),
    Game::Doc::Opt("includeForbidden", "bool", "false", "Ignored."),
};
static const Game::Doc::Field kNamePlateRets[] = {
    Game::Doc::Opt("nameplate", "SimpleFrame", nullptr, "Nil when the unit has no nameplate."),
};
static const Game::Doc::Function kGetNamePlateForUnit{"Returns the nameplate frame of a unit.",
                                                      kGetNamePlateForUnitArgs, kNamePlateRets};

static const Game::Doc::Field kGetNamePlateForGUIDArgs[] = {
    Game::Doc::Req("guid", "WOWGUID"),
};
static const Game::Doc::Function kGetNamePlateForGUID{
    "Returns the nameplate frame of the unit with a GUID.", kGetNamePlateForGUIDArgs,
    kNamePlateRets, nullptr, true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlates",
                                     &Script_GetNamePlates, &kGetNamePlates);
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateGUIDs",
                                     &Script_GetNamePlateGUIDs, &kGetNamePlateGUIDs);
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateForUnit",
                                     &Script_GetNamePlateForUnit, &kGetNamePlateForUnit);
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateForGUID",
                                     &Script_GetNamePlateForGUID, &kGetNamePlateForGUID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace NamePlate::Info
