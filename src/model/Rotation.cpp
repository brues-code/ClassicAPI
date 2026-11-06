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

// `PlayerModel:SetRotation(rotation [, animate])` — 4.3.4's second argument.
//
// Vanilla's SetRotation (FUN_SCRIPT_PLAYERMODEL_SET_ROTATION) always turns the
// model with a shuffle animation (TurnTo 0x00505BB0). 4.3.4's RotateModel
// (CharacterModelBase.cpp) does the same when `animate` is true or absent, and
// with `animate` false only stores the facing: no shuffle, no turn latch. It
// also only stores the facing while the pane rests on an animation other than
// Stand (`m_sequence`, set by SetAnimation, see Model::Animation). Otherwise
// vanilla's method runs unchanged.

#include "Game.h"
#include "Offsets.h"
#include "model/Animation.h"

namespace Model::Rotation {
namespace {

int __fastcall Script_SetRotation(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    const bool animate =
        Game::Lua::Type(L, 3) == Game::Lua::TYPE_NONE || Game::Lua::ToBoolean(L, 3) != 0;
    if (animate && !Model::Animation::HasRestingAnimation(pane))
        return reinterpret_cast<Game::Lua::CFunction>(
            Offsets::FUN_SCRIPT_PLAYERMODEL_SET_ROTATION)(L);
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetRotation(rotation (in radians))");
        return 0;
    }
    Game::Ref<float>(pane, Offsets::OFF_SIMPLEMODEL_FACING) =
        static_cast<float>(Game::Lua::ToNumber(L, 2));
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetRotation", &Script_SetRotation},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("rotation", "number", "The facing in radians."),
    Game::Doc::Opt("animate", "bool", "true", "False turns the model without the shuffle animation."),
};
const Game::Doc::Function kSetRotation{"Turns the model to a facing.", kArgs, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"SetRotation", &kSetRotation},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_PLAYERMODEL_METHOD_REGISTRY), g_methods,
        g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Model::Rotation
