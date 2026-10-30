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

// `Constants.MoneyFormattingConstants`, retail's MoneyConstants system
// (Blizzard_APIDocumentationGenerated/MoneyConstantsDocumentation.lua):
// GOLD_REWARD_THRESHOLD_TO_HIDE_COPPER = 10, the gold amount above which
// FormattingUtil's GetMoneyString(..., checkGoldThreshold) drops the copper.

#include "Game.h"

namespace Currency::MoneyConstants {

namespace {

const Game::Lua::EnumIntegerEntry kMoneyFormattingConstants[] = {
    {"GOLD_REWARD_THRESHOLD_TO_HIDE_COPPER", 10},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterConstants("MoneyFormattingConstants", kMoneyFormattingConstants,
                                 sizeof(kMoneyFormattingConstants) /
                                     sizeof(kMoneyFormattingConstants[0]),
                                 "MoneyConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Currency::MoneyConstants
