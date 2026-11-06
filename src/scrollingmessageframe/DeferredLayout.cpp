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

// One line layout per update for messages added at the bottom.
//
// 1.12's ring writer (0x00788150, AddMessage's only callee) lays the lines
// out after every message at the bottom (Offsets.h, CALL_SMF_STORE_LINE_LAYOUT),
// and the layout sets the text of every line it shows. k messages in one
// update cost k layouts of every shown line, the last of which is the only
// one drawn. Retail's mixin marks its display dirty in AddMessage and lays
// out once in OnPostUpdate; this does the same with the engine's own layout.
//
// The writer's layout call is redirected. When the layout's result is known
// apart from the line heights, it is put off: the frame is shown (a hidden
// frame's layout places no line), it had a message before this one, and its
// top flag is clear (OFF_SCROLLINGMESSAGEFRAME_AT_TOP: the lines do not reach
// the oldest message, and a newer message cannot make them reach it). The
// layout's first store, the scroll record = the newest record, is made at
// once, so GetCurrentScroll, GetScrollOffset and the writer's next check read
// what they read now; the bottom flag is already set and the top flag stays
// clear. Otherwise the layout runs as before.
//
// The put-off layout runs, with the same argument, at the first of:
//   - the frame's update, between its OnUpdate script and its fader
//     (CALL_SMF_ON_UPDATE_BASE). The fader, RearmLines and the link-button
//     rebuild read each laid-out line's record through the line
//     (OFF_SMF_LINE_RECORD), and the writer may have reused those records.
//   - the end of the top's update pass, before the layout queue's second
//     drain and the draw (CALL_TOP_UPDATE_SECOND_DRAIN), for messages added
//     after the frame's update. A message added by any OnUpdate draws in the
//     same frame, as it does when the writer lays out.
//   - RearmLines (0x00788B80), which ScrollDown, ScrollToBottom and
//     ScrollToOffset run at the bottom instead of a layout. A layout at the
//     bottom does not re-arm, so the lines are laid out first.
//   - GetNumLinesDisplayed, which reads the laid-out line count.
//   - any other layout of the frame (a scroll, a resize, Clear), which
//     replaces it.
//
// The layout runs the frame's OnMessageScrollChanged when its argument is not
// the scroll record it replaces. The writer's layout runs it for every
// message; the put-off layout is handed the scroll record from before the
// first message it covers, so it runs once for them all.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "frame/Destruction.h"
#include "scrollingmessageframe/Layout.h"

#include <algorithm>
#include <vector>

namespace ScrollingMessageFrame::DeferredLayout {

namespace {

using Layout_t = void(__fastcall *)(void *smf, void *edx, int newestRecord);
using Update_t = void(__fastcall *)(void *frame, void *edx, float elapsed);
using Drain_t = void(__cdecl *)();
using Smf_t = void(__fastcall *)(void *smf);

struct Pending {
    void *smf;
    int scrollBefore; // the scroll record before the first put-off message
};

// The frames with a put-off layout. Every frame is rebuilt on /reload, so the
// list is cleared then.
std::vector<Pending> g_pending;

std::vector<Pending>::iterator Find(void *smf) {
    return std::find_if(g_pending.begin(), g_pending.end(), [smf](const Pending &p) { return p.smf == smf; });
}

void Drop(void *smf) {
    const auto it = Find(smf);
    if (it != g_pending.end())
        g_pending.erase(it);
}

void Layout(void *smf, int newestRecord) {
    reinterpret_cast<Layout_t>(Offsets::FUN_SMF_DISPLAY_REFRESH)(smf, nullptr, newestRecord);
}

// Runs the frame's put-off layout, if it has one.
void Flush(void *smf) {
    const auto it = Find(smf);
    if (it == g_pending.end())
        return;
    const int scrollBefore = it->scrollBefore;
    g_pending.erase(it);
    Game::Ref<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_SCROLL_RECORD) = scrollBefore;
    Layout(smf, Game::Read<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_POS));
}

// The writer's layout at the bottom.
void __fastcall WriterLayout(void *smf, void *, int newestRecord) {
    const bool deferrable = Game::Read<int>(smf, Offsets::OFF_FRAME_VISIBLE) != 0 &&
                            Game::Read<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_COUNT) > 1 &&
                            Game::Read<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_AT_TOP) == 0;
    if (!deferrable) {
        Layout(smf, newestRecord);
        return;
    }
    int &scroll = Game::Ref<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_SCROLL_RECORD);
    if (Find(smf) == g_pending.end())
        g_pending.push_back({smf, scroll});
    scroll = newestRecord;
}

// The frame's OnUpdate script, then its put-off layout, before its fader.
void __fastcall UpdateThenFlush(void *smf, void *edx, float elapsed) {
    reinterpret_cast<Update_t>(Offsets::FUN_FRAME_ON_UPDATE)(smf, edx, elapsed);
    Flush(smf);
}

// Every put-off layout, then the update pass's second drain. The frames are
// taken from the list as it is when each runs, so one a layout's script drops
// is not laid out; a message a script adds waits for the next update.
void __cdecl FlushAllThenDrain() {
    for (size_t n = g_pending.size(); n > 0 && !g_pending.empty(); --n)
        Flush(g_pending.front().smf);
    reinterpret_cast<Drain_t>(Offsets::FUN_LAYOUT_QUEUE_DRAIN)();
}

Smf_t g_origRearmLines = nullptr;

void __fastcall RearmLines_h(void *smf) {
    Flush(smf);
    g_origRearmLines(smf);
}

const Game::HookAutoRegister _rearmHook{Offsets::FUN_SCROLLINGMESSAGEFRAME_REARM_LINES,
                                        reinterpret_cast<void *>(&RearmLines_h),
                                        reinterpret_cast<void **>(&g_origRearmLines)};

int __fastcall Script_GetNumLinesDisplayed(void *L) {
    void *frame = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID);
    if (frame == nullptr)
        return 0;
    Flush(frame);
    return reinterpret_cast<Game::Lua::CFunction>(
        Offsets::FUN_SCRIPT_SCROLLINGMESSAGEFRAME_GET_NUM_LINES_DISPLAYED)(L);
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"GetNumLinesDisplayed", &Script_GetNumLinesDisplayed},
};

const Game::Doc::Field kGetNumLinesDisplayedRets[] = {
    Game::Doc::Req("count", "number"),
};
const Game::Doc::Function kGetNumLinesDisplayed{"Returns how many messages the frame shows.", {},
                                                kGetNumLinesDisplayedRets};

const Game::Doc::Method kDocs[] = {
    {"GetNumLinesDisplayed", &kGetNumLinesDisplayed},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_SCROLLINGMESSAGEFRAME_METHOD_REGISTRY), kMethods,
        kDocs);
    // Every way to the put-off layout first; the writer's layout is put off
    // only when they all hold.
    const bool flushes =
        Common::RedirectCall(Offsets::CALL_SMF_ON_UPDATE_BASE, Offsets::FUN_FRAME_ON_UPDATE,
                             reinterpret_cast<const void *>(&UpdateThenFlush)) &&
        Common::RedirectCall(Offsets::CALL_TOP_UPDATE_SECOND_DRAIN, Offsets::FUN_LAYOUT_QUEUE_DRAIN,
                             reinterpret_cast<const void *>(&FlushAllThenDrain));
    if (flushes)
        Common::RedirectCall(Offsets::CALL_SMF_STORE_LINE_LAYOUT, Offsets::FUN_SMF_DISPLAY_REFRESH,
                             reinterpret_cast<const void *>(&WriterLayout));
}

void DropBeforeLayout(void *smf, int) { Drop(smf); }

void PrepareForReload() { g_pending.clear(); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};
const Frame::Destruction::AutoSubscribe _destructionSub{&Drop};
const ScrollingMessageFrame::Layout::AutoSubscribeBefore _layoutSub{&DropBeforeLayout};

} // namespace

} // namespace ScrollingMessageFrame::DeferredLayout
