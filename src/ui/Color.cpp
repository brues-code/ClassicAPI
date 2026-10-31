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
// Each returned row is `{ baseTag = "FOO_COLOR", color = ColorMixin }`.
// We call back into Lua's `CreateColor(r,g,b,a)` per row so the
// returned `color` already carries the mixin methods (matches what
// `/dump C_UIColor.GetColors()[1]` shows on Anniversary). If
// `CreateColor` isn't yet defined (e.g. a consumer called us before
// `!!!ClassicAPI` finished loading), we fall back to a plain
// `{r,g,b,a}` table — the loop in `Util/Color.lua` only reads those
// fields anyway and immediately re-wraps via its own `CreateColor`.

#include "ColorData.h"
#include "Game.h"

#include <cstdint>

namespace UI::Color {

namespace {

struct RGBA {
    double r, g, b, a;
};

RGBA Decode(int32_t argb) {
    const uint32_t v = static_cast<uint32_t>(argb);
    return {
        static_cast<double>((v >> 16) & 0xFF) / 255.0,
        static_cast<double>((v >> 8) & 0xFF) / 255.0,
        static_cast<double>(v & 0xFF) / 255.0,
        static_cast<double>((v >> 24) & 0xFF) / 255.0,
    };
}

// Pushes a plain `{r=, g=, b=, a=}` table on top. Used only when the
// addon-side `CreateColor` isn't available yet.
void PushPlainColor(void *L, const RGBA &c) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "r", c.r);
    Game::Lua::SetFieldNumber(L, "g", c.g);
    Game::Lua::SetFieldNumber(L, "b", c.b);
    Game::Lua::SetFieldNumber(L, "a", c.a);
}

// With `CreateColor` already at stack index `createColorIdx`, duplicates
// it, pushes r/g/b/a, and `pcall`s it. On success leaves the returned
// color object on top; on any failure (pcall error, non-function value
// returned, etc.) leaves a plain `{r,g,b,a}` table instead.
void PushColorViaCreateColor(void *L, int createColorIdx, const RGBA &c) {
    Game::Lua::PushValue(L, createColorIdx);
    Game::Lua::PushNumber(L, c.r);
    Game::Lua::PushNumber(L, c.g);
    Game::Lua::PushNumber(L, c.b);
    Game::Lua::PushNumber(L, c.a);
    const int rc = Game::Lua::PCall(L, /*nargs=*/4, /*nresults=*/1, /*errfunc=*/0);
    if (rc != 0) {
        // Error object is on top — discard and fall back. We don't
        // surface CreateColor errors to the caller because the
        // intended use site (Util/Color.lua at addon load) isn't in
        // a position to handle a partial color table.
        Game::Lua::SetTop(L, -2);
        PushPlainColor(L, c);
    }
}

int __fastcall Script_GetColors(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L); // index 1: outer array

    // Resolve `CreateColor` once and keep it pinned at stack index 2.
    // Push the lookup key, GetTable on the globals pseudo-index, then
    // verify the result is callable; if not, drop a `nil` there so the
    // loop body knows to take the fallback path. Either way the slot
    // stays occupied so our negative-index math is stable.
    Game::Lua::PushString(L, "CreateColor");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX); // index 2
    const bool haveCreateColor =
        Game::Lua::Type(L, 2) == Game::Lua::TYPE_FUNCTION;

    for (int i = 0; i < ColorData::kColorCount; ++i) {
        const auto &entry = ColorData::kColors[i];
        const RGBA c = Decode(entry.argb);

        Game::Lua::PushNumber(L, static_cast<double>(i + 1)); // key
        Game::Lua::NewTable(L);                                // inner table

        Game::Lua::SetFieldString(L, "baseTag", entry.baseTag);

        Game::Lua::PushString(L, "color");
        if (haveCreateColor)
            PushColorViaCreateColor(L, 2, c);
        else
            PushPlainColor(L, c);
        Game::Lua::SetTable(L, -3);

        Game::Lua::SetTable(L, 1); // outer[i+1] = inner
    }

    // Drop the pinned `CreateColor` (or its nil placeholder) so only
    // the outer table is left as the return value.
    Game::Lua::Remove(L, 2);
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_UIColor", "GetColors", &Script_GetColors);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace UI::Color
