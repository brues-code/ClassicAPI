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

// `Model:SetGlow(value)` — 4.3.4's Model method (CSimpleModel_Scripts.cpp).
//
// In 4.3.4 it calls the virtual CSimpleModel::SetGlow, which does nothing on a
// Model; only the glue screen's ModelFFX overrides it, to set the strength of
// its glow effect. In the game every Model is a plain Model, so the method
// checks its arguments the way 4.3.4 does and changes nothing.

#include "Game.h"
#include "Offsets.h"

namespace Model::Glow {
namespace {

int __fastcall Script_SetGlow(void *L) {
    void *model = Game::Lua::ResolveModel(L);
    if (model == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2))
        Game::Lua::Error(L, "Usage: %s:SetGlow(value)", Game::Lua::ObjectDisplayName(model));
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetGlow", &Script_SetGlow},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("value", "number", "The glow strength."),
};
const Game::Doc::Function kSetGlow{"Sets the glow strength of the model. It has no effect in the game.",
                                   kArgs, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"SetGlow", &kSetGlow},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_MODEL_METHOD_REGISTRY),
                                    g_methods, 1, g_methodDocs, 1);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Model::Glow
