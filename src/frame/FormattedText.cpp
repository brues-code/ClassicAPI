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

// `fontstring:SetFormattedText(fmt, ...)` and `button:SetFormattedText(fmt, ...)`.
//
// 4.3.4 formats with FrameScript_Sprintf and hands the result to the type's own
// SetText: CSimpleFontString::SetText(text, 1), with the "Font not set" error of
// Lua SetText first, and CSimpleButton::SetText(text). 1.12 has no
// FrameScript_Sprintf; its str_format (FUN_LUA_STR_FORMAT) takes the same
// conversions and numbered arguments (`%1$s`), so the format runs through it,
// called directly under pcall rather than through `_G.string.format`, which an
// addon can replace. The text then goes to 1.12's own SetText for the type.

#include "Game.h"
#include "Offsets.h"

namespace Frame::FormattedText {

namespace {

using FontStringSetText_t = void(__fastcall *)(void *fontString, void *edx, const char *text, int expand);
using ButtonSetText_t = void(__fastcall *)(void *button, void *edx, const char *text);

// Leaves str_format's result on the stack top and returns it, or raises its
// error. The caller restores the stack.
const char *Format(void *L) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::CheckStack(L, top + 4);
    Game::Lua::PushCClosure(L, reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_LUA_STR_FORMAT), 0);
    for (int i = 2; i <= top; ++i)
        Game::Lua::PushValue(L, i);
    if (Game::Lua::PCall(L, top - 1, 1, 0) != 0) {
        const char *msg = Game::Lua::ToString(L, -1);
        Game::Lua::Error(L, "%s", (msg != nullptr) ? msg : "SetFormattedText: format failed");
        return nullptr;
    }
    const char *text = Game::Lua::ToString(L, -1);
    return (text != nullptr) ? text : "";
}

// `self` of the type, and a format string, or the usage error.
void *ResolveSelf(void *L, uintptr_t typeIdVar, const char *usage) {
    void *self = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        self = Game::Lua::ResolveTypedObject(L, 1, typeIdVar, /*raiseError=*/false);
    if (self == nullptr || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "%s", usage);
        return nullptr;
    }
    return self;
}

int __fastcall Script_FontStringSetFormattedText(void *L) {
    void *fontString = ResolveSelf(L, Offsets::VAR_FONTSTRING_LUA_TYPE_ID,
                                   "Usage: fontstring:SetFormattedText(\"format\"[, ...])");
    if (fontString == nullptr)
        return 0;
    if (Game::Read<void *>(fontString, Offsets::OFF_FONTSTRING_FONT_HANDLE) == nullptr) {
        Game::Lua::Error(L, "%s:SetFormattedText(): Font not set", Game::Lua::ObjectDisplayName(fontString));
        return 0;
    }
    const int top = Game::Lua::GetTop(L);
    const char *text = Format(L);
    reinterpret_cast<FontStringSetText_t>(Offsets::FUN_FONTSTRING_SET_TEXT)(fontString, nullptr, text, 1);
    Game::Lua::SetTop(L, top);
    return 0;
}

int __fastcall Script_ButtonSetFormattedText(void *L) {
    void *button =
        ResolveSelf(L, Offsets::VAR_BUTTON_LUA_TYPE_ID, "Usage: button:SetFormattedText(\"format\"[, ...])");
    if (button == nullptr)
        return 0;
    const int top = Game::Lua::GetTop(L);
    const char *text = Format(L);
    reinterpret_cast<ButtonSetText_t>(Offsets::FUN_BUTTON_SET_TEXT)(button, nullptr, text);
    Game::Lua::SetTop(L, top);
    return 0;
}

const Game::Lua::FrameMethodEntry kFontStringMethods[] = {
    {"SetFormattedText", &Script_FontStringSetFormattedText},
};
const Game::Lua::FrameMethodEntry kButtonMethods[] = {
    {"SetFormattedText", &Script_ButtonSetFormattedText},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("format", "string", "A format string, as for string.format."),
    Game::Doc::Opt("...", "any", nullptr, "The values for the format."),
};
const Game::Doc::Function kFontStringDoc{"Sets the text to a formatted string, as SetText(string.format(...)).",
                                         kArgs, {}};
const Game::Doc::Function kButtonDoc{"Sets the button's text to a formatted string, as SetText(string.format(...)).",
                                     kArgs, {}};

const Game::Doc::Method kFontStringDocs[] = {{"SetFormattedText", &kFontStringDoc}};
const Game::Doc::Method kButtonDocs[] = {{"SetFormattedText", &kButtonDoc}};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY),
                                    kFontStringMethods, 1, kFontStringDocs, 1);
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_BUTTON_METHOD_REGISTRY), kButtonMethods,
                                    1, kButtonDocs, 1);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::FormattedText
