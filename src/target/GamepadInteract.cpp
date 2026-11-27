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

// SetPreferredGamepadInteractTarget([unit]) and the gamepad interact glyph, as
// the camelot client (_forever_ 1.60.1) has them. 3.80.2 and retail have
// neither.
//
// Camelot's Blizzard_GamepadActionBars (MainActionBarFrame.lua,
// UpdateInteractIcons) resolves what its interact button would act on and
// names that unit here: the soft interact, else the target, and nil while a
// trigger or modifier set is active. SetUnitCursorTexture with
// preferGamepadIcon, and the world icon over the soft interact, then show a
// glyph in place of the cursor image for that unit.
//
// The glyph (_forever_ probe, Xbox pad, the Innkeeper as softinteract and the
// bar's preferred unit): SetUnitCursorTexture(t, "softinteract", nil, nil,
// true) gives "Cursor xbox_x_64" with no file ID; with false, the cursor
// image. Neither INTERACTTARGET ("H") nor INTERACTSOFTTARGET (unbound) is on
// a pad button, so the glyph follows no binding. Its atlas (UiTextureAtlas
// 4248, FileDataID 8355492, 512x256) holds only xbox_x, playstation_square
// and switch_y at 32..128: the left face button, PAD3, in each label style
// (camelot's own key labels: KEY_PAD3_LTR "Ltr_X", _SHP "Shp_Square", _REV
// "Rev_Y"). The 64 px cuts of that sheet ship here as
// Interface\Cursor\<name>.blp. The generic style has no glyph of its own on
// the sheet; it takes the letter one.
//
// The unit resolves to its GUID when it is named, as the engine's other
// unit-token setters do; the bar names it again on every soft interact,
// target and equipment change.

#include "target/GamepadInteract.h"

#include "Game.h"
#include "gamepad/GamePad.h"
#include "gamepad/Names.h"
#include "gamepad/State.h"
#include "lua/Arg.h"

#include <cstdint>

namespace Target::GamepadInteract {

namespace {

uint64_t g_preferred = 0;

// The generated UnitToken reader: nil clears, a string resolves to its GUID
// (an unknown token is 0), a number names no unit, anything else is the
// usage error.
int __fastcall Script_SetPreferredGamepadInteractTarget(void *L) {
    uint64_t guid = 0;
    if (!Game::Lua::Arg::UnitToken(L, 1, "Usage: SetPreferredGamepadInteractTarget([unit])", guid,
                                   true))
        return 0;
    g_preferred = guid;
    return 0;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("unit", "UnitToken", nullptr,
                   "The unit the gamepad interact button acts on. Nil for none."),
};
const Game::Doc::Function kDoc{
    "Sets the unit that shows the gamepad interact icon in place of its cursor image.", kArgs,
    {}, "TargetScript"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetPreferredGamepadInteractTarget",
                                      &Script_SetPreferredGamepadInteractTarget, &kDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// The bar names its unit again once the new interface loads.
void PrepareForReload() { g_preferred = 0; }

const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

uint64_t Preferred() { return g_preferred; }

const char *GlyphFor(uint64_t guid) {
    if (guid == 0 || guid != g_preferred)
        return nullptr;
    switch (GamePad::State::ConfigOf(GamePad::ActiveID()).labelStyle) {
    case GamePad::Names::Shapes:
        return "Interface\\Cursor\\playstation_square.blp";
    case GamePad::Names::Reverse:
        return "Interface\\Cursor\\switch_y.blp";
    default:
        return "Interface\\Cursor\\xbox_x.blp";
    }
}

} // namespace Target::GamepadInteract
