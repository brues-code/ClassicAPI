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

// `SetInventoryPortraitTexture(texture, unit, slot)` — puts the round
// portrait of the icon of the item a player wears in an inventory slot on a
// texture.
//
// The client already has this function, misspelled
// `SetInventoryPortaitTexture`, with the same body as 4.3.4's but one wrong
// index: it reads the unit token from argument 1, the texture, so it never
// finds a unit and only clears the texture. The index is patched to 2, which
// also fixes the misspelled global (nothing in the stock interface calls
// it), and the function is registered under the correct name. Its error text
// keeps the client's spelling.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Item::InventoryPortrait {

namespace {

// `mov edx,1` — the Lua index handed to lua_tostring for the unit token.
constexpr uint8_t kOriginal[5] = {0xBA, 0x01, 0x00, 0x00, 0x00};
constexpr uint32_t kUnitArg = 2;

// Runs on every in-game registration (each /reload too); once patched the
// bytes no longer match and the write is skipped. A client whose code
// differs there is left alone.
void FixUnitArg() {
    auto *site = reinterpret_cast<uint8_t *>(Offsets::PATCH_INVENTORY_PORTRAIT_UNIT_ARG);
    if (std::memcmp(site, kOriginal, sizeof kOriginal) != 0)
        return;
    Common::PatchBytes(site + 1, &kUnitArg, sizeof kUnitArg);
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("texture", "SimpleTexture"),
    Game::Doc::Req("unit", "UnitToken"),
    Game::Doc::Req("slot", "luaIndex"),
};
const Game::Doc::Function kSetInventoryPortraitTexture{
    "Sets a texture to the round portrait of the icon of the item a player wears in a slot.",
    kArgs, {}, "ItemGlobals"};

void RegisterLuaFunctions() {
    FixUnitArg();
    Game::Lua::RegisterGlobalFunction(
        "SetInventoryPortraitTexture",
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_SET_INVENTORY_PORTAIT_TEXTURE),
        &kSetInventoryPortraitTexture);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::InventoryPortrait
