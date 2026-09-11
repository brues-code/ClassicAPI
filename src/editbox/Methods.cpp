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

// Modern EditBox method backports — cursor, selection, focus and text-state
// accessors that vanilla's 48-method EditBox registry lacks but modern addons
// call unconditionally (the caret bug that motivated this: TwitchEmotes'
// autocomplete calls SetCursorPosition after replacing the text).
//
//   SetCursorPosition / GetCursorPosition   — caret as a byte offset
//   GetUTF8CursorPosition                   — caret as a character index
//   ClearHighlightText                      — drop the selection, keep the caret
//   HasFocus                                — does this box own keyboard focus
//   HasText                                 — is the box non-empty
//   Set/GetHighlightColor                   — the text-selection highlight color
//   ClearHistory                            — empty the up/down input history
//
// The engine already stores and manipulates all of this; we just expose it.
// Every method is registered on the EditBox method registry, so — like any
// per-type frame method — only EditBox frames dispatch to them (a Button or
// Frame still gets "method not found"), so the resolved `this` is always a
// real CSimpleEditBox.
//
// The caret is a BYTE offset into the UTF-8 text buffer (field +0x36c), which
// is exactly what modern WoW's byte-based EditBox cursor uses — for ASCII text
// a byte offset equals a character index, so callers passing `#text` land at
// the end either way.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace EditBox::Methods {

namespace {

using SetCursorByte_t = void(__thiscall *)(void *editbox, int byteOffset);
using CollapseSelection_t = void(__thiscall *)(void *editbox);
using CountChars_t = int(__thiscall *)(void *editbox, int startByte, int byteCount);
using RegionSetColor_t = void(__thiscall *)(void *region, const uint32_t *colorBGRA);
using RegionGetColor_t = void(__thiscall *)(void *region, uint32_t *outBGRA);
using SetHistoryMax_t = void(__thiscall *)(void *editbox, int maxLines);

// A Lua color arg (0..1) clamped and scaled to a 0..255 byte, matching the
// engine's own float->byte color conversion (truncating, like Texture's
// SetVertexColor). `dflt` is used when the arg is absent.
uint32_t ColorByte(void *L, int idx, double dflt) {
    double v = Game::Lua::IsNumber(L, idx) ? Game::Lua::ToNumber(L, idx) : dflt;
    if (v < 0.0)
        v = 0.0;
    else if (v > 1.0)
        v = 1.0;
    return static_cast<uint32_t>(v * 255.0);
}

int __fastcall Script_SetCursorPosition(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: EditBox:SetCursorPosition(position)");
        return 0;
    }
    const int pos = static_cast<int>(Game::Lua::ToNumber(L, 2));
    reinterpret_cast<SetCursorByte_t>(
        Offsets::FUN_EDITBOX_SET_CURSOR_BYTE)(eb, pos);
    reinterpret_cast<CollapseSelection_t>(
        Offsets::FUN_EDITBOX_COLLAPSE_SELECTION)(eb);
    // COLLAPSE_SELECTION moves selStart/selEnd but doesn't flag them; set the
    // selection-dirty bit (2) so any highlight from before is cleared on the
    // next paint. SET_CURSOR_BYTE already set the cursor-dirty bit (4).
    Game::Ref<uint32_t>(eb, Offsets::OFF_EDITBOX_FLAGS) |= 2;
    return 0;
}

int __fastcall Script_GetCursorPosition(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr) {
        Game::Lua::Error(L, "Usage: EditBox:GetCursorPosition()");
        return 0;
    }
    Game::Lua::PushNumber(L, static_cast<double>(
        Game::Read<int>(eb, Offsets::OFF_EDITBOX_CURSOR_BYTE)));
    return 1;
}

// The caret as a 0-based CHARACTER index (UTF-8 codepoints before the caret),
// vs GetCursorPosition's byte offset. Equal for ASCII; differs once the text
// holds multibyte characters.
int __fastcall Script_GetUTF8CursorPosition(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr) {
        Game::Lua::Error(L, "Usage: EditBox:GetUTF8CursorPosition()");
        return 0;
    }
    const int cursorByte = Game::Read<int>(eb, Offsets::OFF_EDITBOX_CURSOR_BYTE);
    const int chars = reinterpret_cast<CountChars_t>(
        Offsets::FUN_EDITBOX_COUNT_CHARS)(eb, 0, cursorByte);
    Game::Lua::PushNumber(L, static_cast<double>(chars));
    return 1;
}

// Drop the current selection without moving the caret. Collapsing selection to
// the caret (selStart = selEnd = cursor) is exactly what the engine's own
// helper does; vanilla can only clear a selection via HighlightText(0, 0),
// which yanks it to offset 0 instead of leaving the caret in place.
int __fastcall Script_ClearHighlightText(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr) {
        Game::Lua::Error(L, "Usage: EditBox:ClearHighlightText()");
        return 0;
    }
    reinterpret_cast<CollapseSelection_t>(
        Offsets::FUN_EDITBOX_COLLAPSE_SELECTION)(eb);
    Game::Ref<uint32_t>(eb, Offsets::OFF_EDITBOX_FLAGS) |= 2;
    return 0;
}

// True when this edit box currently owns keyboard focus. The engine tracks the
// focused box in one global (written by SetFocus / cleared by ClearFocus), so
// focus is just pointer identity against it.
int __fastcall Script_HasFocus(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::PushBool(L,
        eb == Game::Read<void *>(Offsets::VAR_FOCUSED_EDITBOX));
    return 1;
}

// True when the box holds any text (its byte length is non-zero).
int __fastcall Script_HasText(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::PushBool(L,
        Game::Read<int>(eb, Offsets::OFF_EDITBOX_TEXT_LENGTH) > 0);
    return 1;
}

// SetHighlightColor(r, g, b [, a]) — the color of the text-selection highlight.
// The engine paints the selection with three Texture regions (start-line,
// middle-block, end-line); their vertex color is the highlight color, so set
// it on all three via the engine's own region color setter. Colors are 0..1;
// alpha defaults to 1. Takes effect on the next selection paint.
int __fastcall Script_SetHighlightColor(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr || !Game::Lua::IsNumber(L, 2) ||
        !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L, "Usage: EditBox:SetHighlightColor(r, g, b [, a])");
        return 0;
    }
    const uint32_t r = ColorByte(L, 2, 1.0);
    const uint32_t g = ColorByte(L, 3, 1.0);
    const uint32_t b = ColorByte(L, 4, 1.0);
    const uint32_t a = ColorByte(L, 5, 1.0);
    const uint32_t packed = b | (g << 8) | (r << 16) | (a << 24); // {b,g,r,a}
    auto setColor = reinterpret_cast<RegionSetColor_t>(
        Offsets::FUN_FONTSTRING_SET_COLOR);
    for (int off = Offsets::OFF_EDITBOX_HIGHLIGHT_REGION;
         off <= Offsets::OFF_EDITBOX_HIGHLIGHT_REGION + 8; off += 4) {
        void *region = Game::Read<void *>(eb, off);
        if (region != nullptr)
            setColor(region, &packed);
    }
    return 0;
}

// GetHighlightColor() -> r, g, b, a (each 0..1). Reads the color off the first
// highlight region; a region with no explicit color reads back white (opaque).
int __fastcall Script_GetHighlightColor(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr) {
        Game::Lua::Error(L, "Usage: EditBox:GetHighlightColor()");
        return 0;
    }
    uint32_t packed = 0xFFFFFFFF;
    void *region = Game::Read<void *>(eb, Offsets::OFF_EDITBOX_HIGHLIGHT_REGION);
    if (region != nullptr)
        reinterpret_cast<RegionGetColor_t>(Offsets::FUN_REGION_GET_COLOR)(
            region, &packed);
    const double inv = 1.0 / 255.0;
    Game::Lua::PushNumber(L, ((packed >> 16) & 0xff) * inv); // r
    Game::Lua::PushNumber(L, ((packed >> 8) & 0xff) * inv);  // g
    Game::Lua::PushNumber(L, (packed & 0xff) * inv);         // b
    Game::Lua::PushNumber(L, ((packed >> 24) & 0xff) * inv); // a
    return 4;
}

// Empties the up/down input history, keeping the line limit so the box keeps
// recording afterward. The engine's SetHistoryLines internal frees the whole
// ring at max 0, so clear = free (0) then reallocate at the saved max — which
// drops every entry and resets the write position. No-op when history was
// never enabled (max 0).
int __fastcall Script_ClearHistory(void *L) {
    void *eb = Game::Lua::ResolveEditBox(L);
    if (eb == nullptr) {
        Game::Lua::Error(L, "Usage: EditBox:ClearHistory()");
        return 0;
    }
    const int max = Game::Read<int>(eb, Offsets::OFF_EDITBOX_HISTORY_MAX);
    if (max > 0) {
        auto setMax = reinterpret_cast<SetHistoryMax_t>(
            Offsets::FUN_EDITBOX_SET_HISTORY_MAX);
        setMax(eb, 0);
        setMax(eb, max);
    }
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetCursorPosition", &Script_SetCursorPosition},
    {"GetCursorPosition", &Script_GetCursorPosition},
    {"GetUTF8CursorPosition", &Script_GetUTF8CursorPosition},
    {"ClearHighlightText", &Script_ClearHighlightText},
    {"HasFocus", &Script_HasFocus},
    {"HasText", &Script_HasText},
    {"SetHighlightColor", &Script_SetHighlightColor},
    {"GetHighlightColor", &Script_GetHighlightColor},
    {"ClearHistory", &Script_ClearHistory},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_EDITBOX_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace EditBox::Methods
