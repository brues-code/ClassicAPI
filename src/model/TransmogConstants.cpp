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

// `Constants.Transmog`, retail's TransmogConstants system
// (Blizzard_APIDocumentationGenerated/TransmogConstantsDocumentation.lua):
// the values ItemTransmogInfoMixin (Util/ItemUtil.lua) and the
// ModelSceneActor ItemTransmogInfo methods use for "no appearance".

#include "Game.h"

namespace Model::TransmogConstants {

namespace {

const Game::Lua::EnumIntegerEntry kTransmog[] = {
    {"NoTransmogID", 0},
    {"MainHandTransmogIsIndividualWeapon", -1},
    {"MainHandTransmogIsPairedWeapon", 0},
};

void RegisterLuaFunctions() { Game::Lua::RegisterConstants("Transmog", kTransmog, "TransmogConstants"); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Model::TransmogConstants
