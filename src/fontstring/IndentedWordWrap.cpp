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

// `SetIndentedWordWrap([indent])` and `GetIndentedWordWrap()` on Font,
// FontString, EditBox, MessageFrame, ScrollingMessageFrame and SimpleHTML
// (whose pair takes an optional "h1".."p" first).
//
// 4.3.4 keeps the setting as style bit 0x20000. A font string sets its own
// bit (and clears it from its settable mask, so its font no longer
// overrides it); every other class sets it on its font through
// CSimpleFont_SetIndentedWordWrap, which then updates the font strings that
// follow the font. The getters return 1 or nil. 1.12 has the same style
// dword, settable mask and font setter shape (Offsets.h, IndentedWordWrap),
// so these are 4.3.4's setters on 1.12's fields; Text::IndentedWordWrap
// makes the text engine honour the bit.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace FontString::IndentedWordWrap {

namespace {

using LooseBool_t = int(__fastcall *)(void *L, int idx, int defaultValue);
using SetStyleFlags_t = void(__fastcall *)(void *fontString, void *edx, uint32_t flags);
using UpdateObjects_t = void(__fastcall *)(void *font);
using ReadTextType_t = int(__fastcall *)(void *L);

constexpr uint32_t kIndented = Offsets::FONT_STYLE_INDENTED_WORD_WRAP;

bool ReadIndent(void *L) {
    return reinterpret_cast<LooseBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE)(L, 2, 1) != 0;
}

int PushIndented(void *L, uint32_t styleFlags) {
    if (styleFlags & kIndented)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// ---- Font objects: 4.3.4's CSimpleFont_Set/GetIndentedWordWrap ------------

void SetFontIndented(void *font, bool indented) {
    if (font == nullptr)
        return;
    const uint32_t style = Game::Read<uint32_t>(font, Offsets::OFF_SIMPLEFONT_STYLE_FLAGS);
    const uint32_t wanted = indented ? style | kIndented : style & ~kIndented;
    uint32_t &attrFlags = Game::Ref<uint32_t>(font, Offsets::OFF_SIMPLEFONT_ATTR_FLAGS);
    if (wanted != style || (attrFlags & 0x200) == 0) {
        attrFlags |= 0x202;
        Game::Ref<uint32_t>(font, Offsets::OFF_SIMPLEFONT_STYLE_FLAGS) = wanted;
    }
    Game::Ref<uint32_t>(font, Offsets::OFF_SIMPLEFONT_FONTABLE_FLAGS) &= ~2u;
    void **vtable = *static_cast<void ***>(font);
    reinterpret_cast<UpdateObjects_t>(vtable[Offsets::VTBL_SIMPLEFONT_UPDATE_OBJECTS / sizeof(void *)])(font);
}

int GetFontIndented(void *L, const void *font) {
    return PushIndented(L, font != nullptr ? Game::Read<uint32_t>(font, Offsets::OFF_SIMPLEFONT_STYLE_FLAGS) : 0);
}

// The font each class hands CSimpleFont_Set*.
template <uintptr_t TypeIdVar, uintptr_t FontOffset> void *FontOf(void *L) {
    void *object = Game::Lua::ResolveTypedObject(L, 1, TypeIdVar);
    return object != nullptr ? Game::Read<void *>(object, FontOffset) : nullptr;
}

void *FontObject(void *L) { return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_FONT_LUA_TYPE_ID); }

void *HtmlFont(void *L) {
    void *html = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_SIMPLEHTML_LUA_TYPE_ID);
    if (html == nullptr)
        return nullptr;
    const int textType = reinterpret_cast<ReadTextType_t>(Offsets::FUN_SIMPLEHTML_READ_TEXT_TYPE)(L);
    return Game::Read<void *>(html, Offsets::OFF_SIMPLEHTML_FONTS + 4 * textType);
}

template <void *(*Font)(void *)> int __fastcall Script_SetFontIndented(void *L) {
    void *font = Font(L);
    if (font != nullptr)
        SetFontIndented(font, ReadIndent(L));
    return 0;
}

template <void *(*Font)(void *)> int __fastcall Script_GetFontIndented(void *L) {
    void *font = Font(L);
    if (font == nullptr)
        return 0;
    return GetFontIndented(L, font);
}

constexpr auto EditBoxFont = &FontOf<Offsets::VAR_EDITBOX_LUA_TYPE_ID, Offsets::OFF_EDITBOX_FONT>;
constexpr auto MessageFrameFont = &FontOf<Offsets::VAR_MESSAGEFRAME_LUA_TYPE_ID, Offsets::OFF_MESSAGEFRAME_FONT>;
constexpr auto ScrollingMessageFrameFont =
    &FontOf<Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID, Offsets::OFF_SCROLLINGMESSAGEFRAME_FONT>;

// ---- Font strings: 4.3.4's CSimpleFontString Script_Set/GetIndentedWordWrap -

int __fastcall Script_SetFontStringIndented(void *L) {
    void *fontString = Game::Lua::ResolveFontString(L);
    if (fontString == nullptr)
        return 0;
    const bool indented = ReadIndent(L);
    Game::Ref<uint32_t>(fontString, Offsets::OFF_FONTSTRING_SETTABLE_STYLE) &= ~kIndented;
    const uint32_t flags = Game::Read<uint32_t>(fontString, Offsets::OFF_FONTSTRING_FLAGS);
    reinterpret_cast<SetStyleFlags_t>(Offsets::FUN_FONTSTRING_SET_STYLE_FLAGS)(
        fontString, nullptr, indented ? flags | kIndented : flags & ~kIndented);
    return 0;
}

int __fastcall Script_GetFontStringIndented(void *L) {
    void *fontString = Game::Lua::ResolveFontString(L);
    if (fontString == nullptr)
        return 0;
    return PushIndented(L, Game::Read<uint32_t>(fontString, Offsets::OFF_FONTSTRING_FLAGS));
}

// ---- Registration -----------------------------------------------------------

template <int (__fastcall *Set)(void *), int (__fastcall *Get)(void *)> struct Pair {
    static constexpr Game::Lua::FrameMethodEntry methods[] = {
        {"SetIndentedWordWrap", Set},
        {"GetIndentedWordWrap", Get},
    };
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Opt("indent", "boolean", "true", "When true, every line after the first starts 15 pixels in."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Opt("indent", "number", nullptr, "1 when lines after the first are indented, else nil."),
};
const Game::Doc::Function kSet{"Sets whether wrapped text indents every line after the first.", kSetArgs, {}};
const Game::Doc::Function kGet{"Returns whether wrapped text indents every line after the first.", {}, kGetRets};

const Game::Doc::Field kHtmlSetArgs[] = {
    Game::Doc::Opt("textType", "string", "\"p\"", "\"h1\", \"h2\", \"h3\" or \"p\": the element whose font to change."),
    Game::Doc::Opt("indent", "boolean", "true", "When true, every line after the first starts 15 pixels in."),
};
const Game::Doc::Field kHtmlGetArgs[] = {
    Game::Doc::Opt("textType", "string", "\"p\"", "\"h1\", \"h2\", \"h3\" or \"p\"."),
};
const Game::Doc::Function kHtmlSet{"Sets whether an element's wrapped text indents every line after the first.",
                                   kHtmlSetArgs, {}};
const Game::Doc::Function kHtmlGet{"Returns whether an element's wrapped text indents every line after the first.",
                                   kHtmlGetArgs, kGetRets};

const Game::Doc::Method kDocs[] = {
    {"SetIndentedWordWrap", &kSet},
    {"GetIndentedWordWrap", &kGet},
};
const Game::Doc::Method kHtmlDocs[] = {
    {"SetIndentedWordWrap", &kHtmlSet},
    {"GetIndentedWordWrap", &kHtmlGet},
};

template <typename P> void Register(uintptr_t registry, const Game::Doc::Method *docs) {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(registry), P::methods, 2, docs, 2);
}

// A font string takes the bit from its font only when its settable mask
// has it; 4.3.4's constructor mask (0x2107F) does, 1.12's (0x3F) does not.
// Widen the constructor's immediate so font strings made from now on
// inherit it, as 4.3.4's do.
void WidenSettableMask() {
    constexpr uint8_t kStore[6] = {0xC7, 0x86, 0x24, 0x01, 0x00, 0x00}; // mov dword [esi+0x124], imm32
    const auto *code = reinterpret_cast<const uint8_t *>(Offsets::PATCH_FONTSTRING_SETTABLE_STYLE_INIT);
    for (size_t i = 0; i < sizeof kStore; ++i)
        if (code[i] != kStore[i])
            return;
    auto *imm = reinterpret_cast<uint32_t *>(Offsets::PATCH_FONTSTRING_SETTABLE_STYLE_INIT + sizeof kStore);
    if (*imm != 0x3F)
        return; // already widened, or not the code this was derived from
    const uint32_t widened = 0x3F | kIndented;
    Common::PatchBytes(imm, &widened, sizeof widened);
}

void RegisterLuaFunctions() {
    WidenSettableMask();
    Register<Pair<&Script_SetFontStringIndented, &Script_GetFontStringIndented>>(
        Offsets::VAR_FONTSTRING_METHOD_REGISTRY, kDocs);
    Register<Pair<&Script_SetFontIndented<&FontObject>, &Script_GetFontIndented<&FontObject>>>(
        Offsets::VAR_FONT_METHOD_REGISTRY, kDocs);
    Register<Pair<&Script_SetFontIndented<EditBoxFont>, &Script_GetFontIndented<EditBoxFont>>>(
        Offsets::VAR_EDITBOX_METHOD_REGISTRY, kDocs);
    Register<Pair<&Script_SetFontIndented<MessageFrameFont>, &Script_GetFontIndented<MessageFrameFont>>>(
        Offsets::VAR_MESSAGEFRAME_METHOD_REGISTRY, kDocs);
    Register<Pair<&Script_SetFontIndented<ScrollingMessageFrameFont>,
                  &Script_GetFontIndented<ScrollingMessageFrameFont>>>(
        Offsets::VAR_SCROLLINGMESSAGEFRAME_METHOD_REGISTRY, kDocs);
    Register<Pair<&Script_SetFontIndented<&HtmlFont>, &Script_GetFontIndented<&HtmlFont>>>(
        Offsets::VAR_SIMPLEHTML_METHOD_REGISTRY, kHtmlDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FontString::IndentedWordWrap
