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

#include "scrollingmessageframe/Layout.h"

#include "Game.h"
#include "Offsets.h"

namespace ScrollingMessageFrame::Layout {

namespace {

AutoSubscribe *g_head = nullptr;

using Layout_t = void(__fastcall *)(void *smf, void *edx, int newestRecord);
Layout_t g_origLayout = nullptr;

void __fastcall Layout_h(void *smf, void *edx, int newestRecord) {
    g_origLayout(smf, edx, newestRecord);
    for (const AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        s->fn(smf);
}

const Game::HookAutoRegister _hook{Offsets::FUN_SMF_DISPLAY_REFRESH, reinterpret_cast<void *>(&Layout_h),
                                   reinterpret_cast<void **>(&g_origLayout)};

} // namespace

AutoSubscribe::AutoSubscribe(Listener fn) : fn(fn), next(g_head) { g_head = this; }

} // namespace ScrollingMessageFrame::Layout
