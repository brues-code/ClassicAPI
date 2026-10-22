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

// `GetFontHeight` and `SetFontHeight(height)` on FontString and Font — the two
// classes retail gives them (SimpleFontStringAPI, SimpleFontAPI).
//
// The getters push the height GetFont pushes as its second value, through
// the same two engine calls GetFont makes, so `GetFontHeight()` equals
// `select(2, GetFont())` bit for bit. FontString's optional `calculated`
// (default true) is the `scaled` argument of the engine's own height getter
// 0x007727B0 — 4.3.4's CSimpleFontString::GetFontHeight(recalculate): with it
// set, a string drawn at its font's pixel size (bit 0x200) reports that size.
// GetFont passes 0.
//
// The setters are the engine's own SetFont with the current file and flags:
// they rearrange the Lua stack into SetFont's (self, file, height, flags)
// from GetFont's results and call the class's SetFont method. That keeps its
// unit conversion, its unchanged-font skip, its rebuild and the push to the
// strings that follow a Font. The flags survive the trip as text: GetFont
// formats all three (0x006F1AD0) and SetFont parses all three (0x006F1A90).

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace FontString::FontHeight {
namespace {

// FUN_FONTSTRING_FONT_HEIGHT — __thiscall(fs, scaled-on-stack); dummy-EDX form.
using FsFontHeight_t = float(__fastcall *)(void *fs, void *edx, int scaled);
using GetScaledScreenAspect_t = float(__cdecl *)();
// Declared double so the quotient is read from ST0 with `fstp qword`, as
// GetFont stores it, not narrowed to float.
using DivideByScreenScaleX_t = double(__stdcall *)(float);

// GetFont's push (0x0079D4A5..0x0079D4C4). `aspect * 1024` is exact, so the
// float product rounds once, as GetFont's `fstp dword` does.
double ToUiHeight(float internal) {
    const float aspect =
        reinterpret_cast<GetScaledScreenAspect_t>(Offsets::FUN_COORD_GET_SCALED_SCREEN_ASPECT)();
    const float scaled = aspect * static_cast<float>(Offsets::UI_COORD_SCALE_UNIT) * internal;
    return reinterpret_cast<DivideByScreenScaleX_t>(Offsets::FUN_COORD_DIVIDE_BY_SCREEN_SCALE_X)(scaled);
}

void *FontStringSelf(void *L) { return Game::Lua::ResolveFontString(L); }
void *FontSelf(void *L) { return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_FONT_LUA_TYPE_ID); }

int __fastcall Script_FontStringGetFontHeight(void *L) {
    void *fs = FontStringSelf(L);
    if (fs == nullptr)
        return 0;
    const bool calculated = Game::Lua::Type(L, 2) == Game::Lua::TYPE_NIL || Game::Lua::ToBoolean(L, 2) != 0;
    const float h = reinterpret_cast<FsFontHeight_t>(Offsets::FUN_FONTSTRING_FONT_HEIGHT)(
        fs, nullptr, calculated ? 1 : 0);
    Game::Lua::PushNumber(L, ToUiHeight(h));
    return 1;
}

int __fastcall Script_FontGetFontHeight(void *L) {
    void *font = FontSelf(L);
    if (font == nullptr)
        return 0;
    Game::Lua::PushNumber(L, ToUiHeight(Game::Read<float>(font, Offsets::OFF_SIMPLEFONT_HEIGHT)));
    return 1;
}

template <void *(*Self)(void *), uintptr_t GetFont, uintptr_t SetFont>
int __fastcall Script_SetFontHeight(void *L) {
    void *object = Self(L);
    if (object == nullptr)
        return 0;
    const char *name = Game::Lua::ObjectDisplayName(object);
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetFontHeight(height)", name);
        return 0;
    }
    Game::Lua::SetTop(L, 2);                                   // self, height
    reinterpret_cast<Game::Lua::CFunction>(GetFont)(L);        // self, height, file, old, flags
    if (Game::Lua::Type(L, 3) != Game::Lua::TYPE_STRING) {
        Game::Lua::Error(L, "%s:SetFontHeight(): Font not set", name);
        return 0;
    }
    Game::Lua::Remove(L, 4);                                   // self, height, file, flags
    Game::Lua::PushValue(L, 2);
    Game::Lua::Remove(L, 2);                                   // self, file, flags, height
    Game::Lua::Insert(L, 3);                                   // self, file, height, flags
    reinterpret_cast<Game::Lua::CFunction>(SetFont)(L);
    return 0;
}

const Game::Lua::FrameMethodEntry kFontStringMethods[] = {
    {"GetFontHeight", &Script_FontStringGetFontHeight},
    {"SetFontHeight", &Script_SetFontHeight<&FontStringSelf, Offsets::FUN_SCRIPT_FONTSTRING_GET_FONT,
                                            Offsets::FUN_SCRIPT_FONTSTRING_SET_FONT>},
};
const Game::Lua::FrameMethodEntry kFontMethods[] = {
    {"GetFontHeight", &Script_FontGetFontHeight},
    {"SetFontHeight",
     &Script_SetFontHeight<&FontSelf, Offsets::FUN_SCRIPT_FONT_GET_FONT, Offsets::FUN_SCRIPT_FONT_SET_FONT>},
};

const Game::Doc::Field kFontStringGetArgs[] = {
    Game::Doc::Opt("calculated", "bool", "true",
                   "When true, a string drawn at its font's own pixel size returns that size."),
};
const Game::Doc::Field kFontStringGetRets[] = {
    Game::Doc::Req("height", "uiUnit"),
};
const Game::Doc::Field kFontStringSetArgs[] = {
    Game::Doc::Req("height", "uiUnit"),
};
const Game::Doc::Field kFontGetRets[] = {
    Game::Doc::Req("height", "number"),
};
const Game::Doc::Field kFontSetArgs[] = {
    Game::Doc::Req("height", "number"),
};
const Game::Doc::Function kFontStringGet{"Returns the height of the font.", kFontStringGetArgs,
                                         kFontStringGetRets};
const Game::Doc::Function kFontStringSet{"Sets the height of the font and keeps its file and flags.",
                                         kFontStringSetArgs, {}};
const Game::Doc::Function kFontGet{"Returns the height of the font.", {}, kFontGetRets};
const Game::Doc::Function kFontSet{"Sets the height of the font and keeps its file and flags.", kFontSetArgs,
                                   {}};

const Game::Doc::Method kFontStringDocs[] = {
    {"GetFontHeight", &kFontStringGet},
    {"SetFontHeight", &kFontStringSet},
};
const Game::Doc::Method kFontDocs[] = {
    {"GetFontHeight", &kFontGet},
    {"SetFontHeight", &kFontSet},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY),
                                    kFontStringMethods, 2, kFontStringDocs, 2);
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FONT_METHOD_REGISTRY), kFontMethods,
                                    2, kFontDocs, 2);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace FontString::FontHeight
