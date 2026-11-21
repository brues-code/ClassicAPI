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
// `C_UIColor.GetColors()` — modern Blizzard exposes the contents of
// `GlobalColor.dbc` to Lua through this engine function so that
// `Blizzard_SharedXMLBase/Color.lua` can wrap each row with
// `CreateColor(r,g,b,a)` and assign the result to globals like
// `ACHIEVEMENT_COLOR`. Vanilla 1.12 has no `GlobalColor.dbc` (the
// table was added in TBC) and no `C_UIColor` namespace, so we embed
// a Blizzard-provided snapshot (build 1.60.1.70707) and
// register the function ourselves. The companion addon
// `!!!ClassicAPI/Util/Color.lua` defines `ColorMixin` and `CreateColor`
// in pure Lua — same as `SharedXMLBase/Color.lua` does on modern —
// and consumes our output identically.
//
// Each returned row is `{ baseTag = "FOO_COLOR", color = ColorMixin }`, the
// colour built as 3.80.2's GetColors (0x140E122B0) builds it: a ColorMixin
// table, channels through 0x140AD1900 (lua/MixinTable.h).

#include "ColorData.h"
#include "Game.h"
#include "lua/MixinTable.h"

#include <cstdint>

namespace UI::Color {

namespace {

int __fastcall Script_GetColors(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    for (int i = 0; i < ColorData::kColorCount; ++i) {
        const auto &entry = ColorData::kColors[i];
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(L, "baseTag", entry.baseTag);
        Game::Lua::PushString(L, "color");
        Game::Lua::MixinTable::PushColor(L, static_cast<uint32_t>(entry.argb));
        Game::Lua::SetTable(L, -3);
        Game::Lua::RawSetI(L, 1, i + 1);
    }
    return 1;
}

const Game::Doc::Field kDBColorExportFields[] = {
    Game::Doc::Req("baseTag", "cstring",
                   "The global name of the color, such as ACHIEVEMENT_COLOR."),
    Game::Doc::Req("color", "colorRGBA"),
};
const Game::Doc::Structure kDBColorExport{"DBColorExport", "UIColor", kDBColorExportFields};

const Game::Doc::Field kGetColorsRets[] = {
    Game::Doc::Req("colors", "table", "An array of DBColorExport."),
};
const Game::Doc::Function kGetColors{"Returns the named interface colors.", {}, kGetColorsRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_UIColor", "GetColors", &Script_GetColors, &kGetColors);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace UI::Color
