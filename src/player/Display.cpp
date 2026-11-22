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

// The local player's display, as 3.80.2's C_PlayerInfo reads it for
// ModelSceneUtil.SetUpCharacterSheetScene (the character sheet's shapeshift
// display):
//   GetDisplayID()        -> the player's display now (0x140D71A30, unit
//                            +0x12538): UNIT_FIELD_DISPLAYID.
//   GetNativeDisplayID()  -> the player's own display (0x140D71B00, unit
//                            +0x12674): UNIT_FIELD_NATIVEDISPLAYID.
//   IsDisplayRaceNative() -> whether the display now shows the player's own
//                            race (0x140D72060): the CreatureDisplayInfoExtra
//                            record of the display now (unit +0x5B8) gives
//                            its DisplayRaceID (column 1); a display with no
//                            such record (a creature: a shapeshift form, a
//                            polymorph) counts as native. 3.80.2's alternate-
//                            form race (+0x123E6, the worgen form) has no 1.12
//                            counterpart.
// IsMirrorImage (0x140D721E0, flags2 bit 0x10) and GetAlternateFormInfo
// (0x140D718F0, the race's ChrRaces byte +200) are constant answers on 1.12,
// so they are Compat.lua's.
// With no player each gives 0 or false, as 3.80.2 does when its cached active
// player is null. 3.80.2 registers them (0x140D726A0) from the game
// registrar (0x140ADB2F0) only, not the glue one.

#include "Game.h"
#include "Offsets.h"
#include "model/DisplayInfo.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Player::Display {

namespace {

uint32_t DescriptorField(uintptr_t offset) {
    const uint8_t *fields = Unit::Identity::PlayerDescriptor();
    return fields != nullptr ? Game::Read<uint32_t>(fields, offset) : 0;
}

int __fastcall Script_GetDisplayID(void *L) {
    Game::Lua::PushNumber(L, DescriptorField(Offsets::OFF_UNIT_FIELD_DISPLAYID));
    return 1;
}

int __fastcall Script_GetNativeDisplayID(void *L) {
    Game::Lua::PushNumber(L, DescriptorField(Offsets::OFF_UNIT_FIELD_NATIVEDISPLAYID));
    return 1;
}

int __fastcall Script_IsDisplayRaceNative(void *L) {
    bool native = false;
    if (const uint8_t *fields = Unit::Identity::PlayerDescriptor()) {
        const uint8_t *extra =
            Model::DisplayInfo::DressRecord(Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_DISPLAYID));
        native = extra == nullptr ||
                 Game::Read<uint32_t>(extra, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_RACE) ==
                     Unit::Identity::Race(fields);
    }
    Game::Lua::PushBool(L, native);
    return 1;
}

const Game::Doc::Field kDisplayIDRets[] = {
    Game::Doc::Req("displayID", "number", "0 when there is no player."),
};
const Game::Doc::Function kGetDisplayID{
    "Returns the creature display the player shows now: a shapeshift form or a transform changes it.", {},
    kDisplayIDRets};
const Game::Doc::Field kNativeDisplayIDRets[] = {
    Game::Doc::Req("nativeDisplayID", "number", "0 when there is no player."),
};
const Game::Doc::Function kGetNativeDisplayID{"Returns the player's own creature display, with no form or "
                                              "transform.",
                                              {}, kNativeDisplayIDRets};
const Game::Doc::Field kIsDisplayRaceNativeRets[] = {
    Game::Doc::Req("isDisplayRaceNative", "bool", "False when there is no player."),
};
const Game::Doc::Function kIsDisplayRaceNative{
    "Returns whether the player's display now shows the player's own race. A display of a creature, such as a "
    "shapeshift form, counts as the player's own race.",
    {}, kIsDisplayRaceNativeRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GetDisplayID", &Script_GetDisplayID, &kGetDisplayID);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GetNativeDisplayID", &Script_GetNativeDisplayID,
                                     &kGetNativeDisplayID);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "IsDisplayRaceNative", &Script_IsDisplayRaceNative,
                                     &kIsDisplayRaceNative);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Player::Display
