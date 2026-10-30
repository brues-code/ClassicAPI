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

// Retail's scroll offset and insert mode on ScrollingMessageFrame:
// `SetScrollOffset(offset)`, `GetScrollOffset()`, `GetMaxScrollRange()`,
// `ScrollByAmount(amount)`, `SetInsertMode(mode)`, `GetInsertMode()` and the
// SCROLLING_MESSAGE_FRAME_INSERT_MODE_* globals.
//
// Retail's offset (Blizzard_SharedXML/ScrollingMessageFrame.lua) counts the
// lines the frame is scrolled up from the newest message, 0 at the bottom, up
// to GetMaxScrollRange() = max(GetNumMessages() - 1, 0), where the oldest
// message is the bottom line - the place 1.12's own ScrollToTop goes. 1.12
// keeps the same offset: ScrollToOffset and ScrollOffset (Offsets.h) are its
// two directions, and GetScrollOffset is the engine's GetCurrentScroll under
// retail's name. Retail clamps the offset to its range; the engine wraps one
// outside it, so the setters clamp first. SetScrollOffset to the offset the
// frame has does nothing, as on retail. ScrollByAmount always calls the
// engine, which re-arms the shown lines' fade when the offset stays: retail's
// ResetAllFadeTimes.
//
// 1.12's line layout stacks the lines from the bottom up, retail's BOTTOM
// mode, and has no field for another; TOP is not supported.

#include "Game.h"
#include "Offsets.h"
#include "scrollingmessageframe/DisplayRefreshed.h"

namespace ScrollingMessageFrame::Scroll {

namespace {

// SCROLLING_MESSAGE_FRAME_INSERT_MODE_TOP / _BOTTOM.
constexpr int kInsertModeTop = 1;
constexpr int kInsertModeBottom = 2;

using ScrollToOffset_t = void(__fastcall *)(void *smf, void *edx, int offset);
using ScrollOffset_t = int(__fastcall *)(void *smf);

int ScrollOffset(void *smf) {
    return reinterpret_cast<ScrollOffset_t>(Offsets::FUN_SCROLLINGMESSAGEFRAME_SCROLL_OFFSET)(smf);
}

int MaxScrollRange(void *smf) {
    const int count = Game::Read<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_COUNT);
    return count > 1 ? count - 1 : 0;
}

// `offset` clamped to 0..MaxScrollRange (NaN to 0).
int Clamped(void *smf, double offset) {
    const int max = MaxScrollRange(smf);
    return offset >= 0 ? (offset < max ? static_cast<int>(offset) : max) : 0;
}

void ScrollTo(void *smf, int offset) {
    reinterpret_cast<ScrollToOffset_t>(Offsets::FUN_SCROLLINGMESSAGEFRAME_SCROLL_TO_OFFSET)(smf, nullptr, offset);
}

void *ResolveSmf(void *L) {
    return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID);
}

int __fastcall Script_SetScrollOffset(void *L) {
    void *frame = ResolveSmf(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetScrollOffset(offset)", Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    const int offset = Clamped(frame, Game::Lua::ToNumber(L, 2));
    if (offset != ScrollOffset(frame))
        ScrollTo(frame, offset);
    return 0;
}

int __fastcall Script_GetMaxScrollRange(void *L) {
    void *frame = ResolveSmf(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, MaxScrollRange(frame));
    return 1;
}

int __fastcall Script_ScrollByAmount(void *L) {
    void *frame = ResolveSmf(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:ScrollByAmount(amount)", Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    ScrollTo(frame, Clamped(frame, ScrollOffset(frame) + Game::Lua::ToNumber(L, 2)));
    DisplayRefreshed::MarkDirty(frame);
    return 0;
}

int __fastcall Script_SetInsertMode(void *L) {
    void *frame = ResolveSmf(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || Game::Lua::ToNumber(L, 2) != kInsertModeBottom)
        Game::Lua::Error(L, "%s:SetInsertMode(): only SCROLLING_MESSAGE_FRAME_INSERT_MODE_BOTTOM is supported",
                         Game::Lua::ObjectDisplayName(frame));
    return 0;
}

int __fastcall Script_GetInsertMode(void *L) {
    if (ResolveSmf(L) == nullptr)
        return 0;
    Game::Lua::PushNumber(L, kInsertModeBottom);
    return 1;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"SetScrollOffset", &Script_SetScrollOffset},
    {"GetScrollOffset",
     reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_SCROLLINGMESSAGEFRAME_GET_CURRENT_SCROLL)},
    {"GetMaxScrollRange", &Script_GetMaxScrollRange},
    {"ScrollByAmount", &Script_ScrollByAmount},
    {"SetInsertMode", &Script_SetInsertMode},
    {"GetInsertMode", &Script_GetInsertMode},
};

const Game::Doc::Field kOffsetArgs[] = {
    Game::Doc::Req("offset", "number", "Lines up from the newest message; clamped to 0..GetMaxScrollRange()."),
};
const Game::Doc::Field kOffsetRets[] = {
    Game::Doc::Req("offset", "number", "Lines up from the newest message; 0 at the bottom."),
};
const Game::Doc::Field kRangeRets[] = {
    Game::Doc::Req("maxScrollRange", "number"),
};
const Game::Doc::Field kAmountArgs[] = {
    Game::Doc::Req("amount", "number", "Positive scrolls up, toward older messages."),
};
const Game::Doc::Field kModeArgs[] = {
    Game::Doc::Req("mode", "number", "SCROLLING_MESSAGE_FRAME_INSERT_MODE_BOTTOM; other modes raise an error."),
};
const Game::Doc::Field kModeRets[] = {
    Game::Doc::Req("mode", "number", "SCROLLING_MESSAGE_FRAME_INSERT_MODE_BOTTOM."),
};
const Game::Doc::Function kSetScrollOffset{"Scrolls the frame to a number of lines up from the newest message.",
                                           kOffsetArgs, {}};
const Game::Doc::Function kGetScrollOffset{"Returns how many lines the frame is scrolled up from the newest message.",
                                           {}, kOffsetRets};
const Game::Doc::Function kGetMaxScrollRange{"Returns the largest scroll offset: one less than the message count.",
                                             {}, kRangeRets};
const Game::Doc::Function kScrollByAmount{"Scrolls the frame by a number of lines and shows the faded lines again.",
                                          kAmountArgs, {}};
const Game::Doc::Function kSetInsertMode{"Sets where the newest message shows.", kModeArgs, {}};
const Game::Doc::Function kGetInsertMode{"Returns where the newest message shows.", {}, kModeRets};

const Game::Doc::Method kDocs[] = {
    {"SetScrollOffset", &kSetScrollOffset},     {"GetScrollOffset", &kGetScrollOffset},
    {"GetMaxScrollRange", &kGetMaxScrollRange}, {"ScrollByAmount", &kScrollByAmount},
    {"SetInsertMode", &kSetInsertMode},         {"GetInsertMode", &kGetInsertMode},
};

void RegisterLuaFunctions() {
    void *L = Game::Lua::State();
    Game::Lua::SetGlobalNumber(L, "SCROLLING_MESSAGE_FRAME_INSERT_MODE_TOP", kInsertModeTop);
    Game::Lua::SetGlobalNumber(L, "SCROLLING_MESSAGE_FRAME_INSERT_MODE_BOTTOM", kInsertModeBottom);
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_SCROLLINGMESSAGEFRAME_METHOD_REGISTRY),
                                    kMethods, 6, kDocs, 6);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace ScrollingMessageFrame::Scroll
