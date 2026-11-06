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

// `PlayerModel:SetUnit(unit)` → success, `CanSetUnit(unit)` and
// `GetDisplayInfo()` — retail's CharacterModelBase unit methods.
//
// Vanilla's SetUnit (FUN_SCRIPT_PLAYERMODEL_SET_UNIT) returns nothing. It
// stores the token's GUID and builds the model only when that GUID resolves
// to a unit (type mask 8, FUN_PLAYERMODEL_SET_UNIT_GUID). So SetUnit runs
// the engine's method unchanged (Turtle patches it) and reports whether the
// stored GUID resolves; CanSetUnit is the same resolve without the set
// (FUN_RESOLVE_UNIT_TOKEN = token to GUID, then type mask 8).
//
// GetDisplayInfo reads what the pane shows: the display record's ID
// (SetDisplayInfo / SetCreature), or the unit's UNIT_FIELD_DISPLAYID.

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"

#include <cstdint>

namespace Model::Subject {
namespace {

void *PaneUnit(void *pane) {
    return Object::ByGuid(Offsets::TYPEMASK_UNIT,
                          Game::Read<uint64_t>(pane, Offsets::OFF_PLAYERMODEL_UNIT_GUID));
}

int __fastcall Script_SetUnit(void *L) {
    reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_PLAYERMODEL_SET_UNIT)(L);
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    Game::Lua::PushBool(L, PaneUnit(pane) != nullptr);
    return 1;
}

int __fastcall Script_CanSetUnit(void *L) {
    if (Game::Lua::ResolvePlayerModel(L) == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: CanSetUnit(unit)");
        return 0;
    }
    Game::Lua::PushBool(L, Game::ResolveUnitToken(Game::Lua::ToString(L, 2)) != nullptr);
    return 1;
}

int __fastcall Script_GetDisplayInfo(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    uint32_t displayID = 0;
    if (const auto *record =
            Game::Read<const uint8_t *>(pane, Offsets::OFF_PLAYERMODEL_DISPLAY_RECORD)) {
        displayID = Game::Read<uint32_t>(record, Offsets::OFF_CREATURE_DISPLAYID);
    } else if (void *unit = PaneUnit(pane)) {
        displayID = Game::Read<uint32_t>(Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR),
                                         Offsets::OFF_UNIT_FIELD_DISPLAYID);
    }
    Game::Lua::PushNumber(L, displayID);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetUnit", &Script_SetUnit},
    {"CanSetUnit", &Script_CanSetUnit},
    {"GetDisplayInfo", &Script_GetDisplayInfo},
};

const Game::Doc::Field kUnitArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kSuccessRets[] = {
    Game::Doc::Req("success", "bool", "True when the unit exists."),
};
const Game::Doc::Function kSetUnit{"Shows a unit on the model.", kUnitArgs, kSuccessRets};
const Game::Doc::Field kCanSetUnitRets[] = {
    Game::Doc::Req("canSetUnit", "bool"),
};
const Game::Doc::Function kCanSetUnit{"Returns whether SetUnit can show the unit.", kUnitArgs,
                                      kCanSetUnitRets};
const Game::Doc::Field kDisplayRets[] = {
    Game::Doc::Req("displayID", "number", "The CreatureDisplayInfo ID, or 0."),
};
const Game::Doc::Function kGetDisplayInfo{"Returns the display the model shows.", {},
                                          kDisplayRets};
const Game::Doc::Method g_methodDocs[] = {
    {"SetUnit", &kSetUnit},
    {"CanSetUnit", &kCanSetUnit},
    {"GetDisplayInfo", &kGetDisplayInfo},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_PLAYERMODEL_METHOD_REGISTRY), g_methods,
        g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Model::Subject
