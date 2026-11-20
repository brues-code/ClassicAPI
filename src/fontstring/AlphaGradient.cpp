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

// `fontString:SetAlphaGradient(start, length)`, `GetAlphaGradient()` and
// `ClearAlphaGradient()`: the text fades out from glyph `start` over `length`
// (the quest text reveal).
//
// 1.12 already draws it. CSimpleFontString::SetAlphaGradient
// (FUN_FONTSTRING_SET_ALPHA_GRADIENT) stores the pair at
// OFF_FONTSTRING_GRADIENT_START / _LENGTH and hands it to the built string's
// CGxString, which writes the fade into the glyph vertex colours and answers
// whether `start` is still inside the text; RebuildString re-applies the pair
// while `start` is 0 or more (0x00772711). The stock Lua method (0x0079CC80)
// pushes 1 or nil and checks only that both arguments are numbers.
//
// What this adds is the 3.80.2 client's Lua surface:
//   - SetAlphaGradient (0x140A80810): both arguments required, finite and in
//     int32 range, truncated; `length < 1` raises "length must be greater
//     than 0" (the engine divides the text alpha by it); the answer is a
//     boolean.
//   - GetAlphaGradient (0x140A7E590): the stored pair as 16-bit values, as
//     3.80.2 keeps them; `start` is -1 until one is set (the constructor's
//     0x00770DC9).
//   - ClearAlphaGradient (0x140A7E130): the pair to -1, 0, and the string is
//     rebuilt the way SetText rebuilds it (FontString::Layout::Invalidate), so
//     the rebuild leaves the fade out.
// Unlike 3.80.2, whose UpdateString drops the fade on SetText while keeping the
// pair, a rebuild keeps it, as 1.12 and 3.3.5 do.

#include "Game.h"
#include "Offsets.h"
#include "fontstring/Layout.h"

#include <cmath>
#include <cstdint>

namespace FontString::AlphaGradient {

namespace {

#define SET_ALPHA_GRADIENT_USAGE "Usage: local isWithinText = self:SetAlphaGradient(start, length)"

using SetAlphaGradient_t = bool(__thiscall *)(void *fs, int start, int length);

// An integer argument as 3.80.2's reader (0x140A91460) takes it: present, a
// finite number in int32 range, truncated. Raises otherwise.
bool ReadInt(void *L, int idx, int *out) {
    if (Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, SET_ALPHA_GRADIENT_USAGE);
        return false;
    }
    const double v = Game::Lua::IsNumber(L, idx) ? Game::Lua::ToNumber(L, idx) : NAN;
    if (!(v >= -2147483648.0 && v <= 2147483647.0)) {
        Game::Lua::Error(L, "outside of expected range -2147483648 to 2147483647 - " SET_ALPHA_GRADIENT_USAGE);
        return false;
    }
    *out = static_cast<int>(v);
    return true;
}

int __fastcall Script_SetAlphaGradient(void *L) {
    void *fs = Game::Lua::ResolveFontString(L);
    if (fs == nullptr)
        return 0;
    int start = 0, length = 0;
    if (!ReadInt(L, 2, &start) || !ReadInt(L, 3, &length))
        return 0;
    if (length < 1) {
        Game::Lua::RaiseClientMethodError(L, Game::Lua::ObjectName(fs), "SetAlphaGradient",
                                          "length must be greater than 0");
        return 0;
    }
    const bool within =
        reinterpret_cast<SetAlphaGradient_t>(Offsets::FUN_FONTSTRING_SET_ALPHA_GRADIENT)(fs, start, length);
    Game::Lua::PushBool(L, within);
    return 1;
}

int __fastcall Script_GetAlphaGradient(void *L) {
    void *fs = Game::Lua::ResolveFontString(L);
    if (fs == nullptr)
        return 0;
    const auto start = static_cast<int16_t>(Game::Read<int32_t>(fs, Offsets::OFF_FONTSTRING_GRADIENT_START));
    const auto length = static_cast<int16_t>(Game::Read<int32_t>(fs, Offsets::OFF_FONTSTRING_GRADIENT_LENGTH));
    Game::Lua::PushNumber(L, start);
    Game::Lua::PushNumber(L, length);
    return 2;
}

int __fastcall Script_ClearAlphaGradient(void *L) {
    void *fs = Game::Lua::ResolveFontString(L);
    if (fs == nullptr)
        return 0;
    const bool had = Game::Read<int32_t>(fs, Offsets::OFF_FONTSTRING_GRADIENT_START) >= 0;
    Game::Ref<int32_t>(fs, Offsets::OFF_FONTSTRING_GRADIENT_START) = -1;
    Game::Ref<int32_t>(fs, Offsets::OFF_FONTSTRING_GRADIENT_LENGTH) = 0;
    if (had)
        FontString::Layout::Invalidate(fs); // the fade is in the built string; rebuild it without
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetAlphaGradient", &Script_SetAlphaGradient},
    {"GetAlphaGradient", &Script_GetAlphaGradient},
    {"ClearAlphaGradient", &Script_ClearAlphaGradient},
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("start", "number", "The first glyph that fades, counted from 0."),
    Game::Doc::Req("length", "number", "1 or more; the fade ends about length / 2 glyphs after start."),
};
const Game::Doc::Field kSetRets[] = {
    Game::Doc::Req("isWithinText", "bool", "False once start is past the last glyph."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Req("start", "number", "-1 when no gradient is set."),
    Game::Doc::Req("length", "number"),
};
const Game::Doc::Function kSetAlphaGradient{"Fades the text out from one glyph on.", kSetArgs, kSetRets};
const Game::Doc::Function kGetAlphaGradient{"Returns the text's alpha gradient.", {}, kGetRets};
const Game::Doc::Function kClearAlphaGradient{"Removes the text's alpha gradient.", {}, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetAlphaGradient", &kSetAlphaGradient},
    {"GetAlphaGradient", &kGetAlphaGradient},
    {"ClearAlphaGradient", &kClearAlphaGradient},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FontString::AlphaGradient
