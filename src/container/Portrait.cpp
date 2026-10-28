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

// `SetBagPortraitTexture(texture, bagID)` and its retail home
// `C_Container.SetBagPortraitTexture` — puts the round portrait of an
// equipped bag's icon on a texture.
//
// The client already has this function, misspelled `SetBagPortaitTexture`
// (the stock ContainerFrame calls it by that name), with the same body as
// 4.3.4's. Both names here bind that engine function; its error text keeps
// the client's spelling.

#include "Game.h"
#include "Offsets.h"

namespace Container::Portrait {

namespace {

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("texture", "SimpleTexture"),
    Game::Doc::Req("bagIndex", "BagIndex"),
};
const Game::Doc::Function kSetBagPortraitTexture{
    "Sets a texture to the round portrait of an equipped bag's icon.", kArgs, {},
    "ContainerGlobals"};
const Game::Doc::Function kCContainerSetBagPortraitTexture{
    "Sets a texture to the round portrait of an equipped bag's icon.", kArgs, {}};

void RegisterLuaFunctions() {
    const auto fn =
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_SET_BAG_PORTAIT_TEXTURE);
    Game::Lua::RegisterGlobalFunction("SetBagPortraitTexture", fn, &kSetBagPortraitTexture);
    Game::Lua::RegisterTableFunction("C_Container", "SetBagPortraitTexture", fn,
                                     &kCContainerSetBagPortraitTexture);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::Portrait
