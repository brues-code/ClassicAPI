// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `texture:SetSpriteSheetCell(cell, numRows, numColumns [, cellWidth, cellHeight])`
// — crops the texture to one cell of an evenly divided grid.
//
// A sprite sheet holds several images in one file, so drawing one of them means
// working out its share of the texture and calling SetTexCoord. This does that
// arithmetic, which is otherwise repeated at every call site. It is the primitive
// behind raid target markers, whose eight icons share a single 4x4 sheet.
//
// `cell` is 1-based (Blizzard types the argument `luaIndex`) and counts left to
// right, then top to bottom, so cell 1 is the top-left image.
//
// `cellWidth` and `cellHeight` are accepted and ignored. Every caller in
// Blizzard's own UI passes the three-argument form, and the grid alone already
// fixes each cell's coordinates, so there is nothing this module can do with them
// that would not be a guess at their meaning.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Texture::SpriteSheet {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

int __fastcall Script_SetSpriteSheetCell(void *L) {
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3) ||
        !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L, "Usage: texture:SetSpriteSheetCell(cell, numRows, numColumns)");
        return 0;
    }

    const int cell = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int rows = static_cast<int>(Game::Lua::ToNumber(L, 3));
    const int columns = static_cast<int>(Game::Lua::ToNumber(L, 4));
    if (rows <= 0 || columns <= 0)
        return 0;
    if (cell < 1 || cell > rows * columns)
        return 0; // outside the grid — leave the texture as it is

    const int index = cell - 1;
    const double cellW = 1.0 / columns;
    const double cellH = 1.0 / rows;
    const double left = (index % columns) * cellW;
    const double top = (index / columns) * cellH;

    // Delegate to the engine's own SetTexCoord handler so its argument handling
    // and both coordinate forms stay in force — this module only supplies the
    // grid arithmetic.
    Game::Lua::SetTop(L, 1); // (self)
    Game::Lua::PushNumber(L, left);
    Game::Lua::PushNumber(L, left + cellW);
    Game::Lua::PushNumber(L, top);
    Game::Lua::PushNumber(L, top + cellH);
    reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXCOORD)(L);

    Game::Lua::SetTop(L, 0);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetSpriteSheetCell", &Script_SetSpriteSheetCell},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Texture::SpriteSheet
