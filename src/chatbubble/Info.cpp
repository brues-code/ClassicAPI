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

// `C_ChatBubbles.GetAllChatBubbles([includeForbidden])` — returns an
// array of the currently-active chat-bubble Frame objects, matching
// modern WoW's 7.2.5 signature. Addons use it to recolor / translate /
// reposition speech bubbles by reading the bubble's FontString region.
//
// Vanilla 1.12 chat bubbles are `CGChatBubbleFrame`s — full
// `CSimpleFrame`s created purely in C++ (never via `CreateFrame`), so
// they have no name and no Lua wrapper until something pushes them. This
// is exactly the default-nameplate case: we push each via the shared
// `UI::FrameObject::Push`, which delegates to the engine's own
// `FrameScript_Object::ScriptRegister` to lazily build the canonical
// wrapper. The returned frames respond to real frame methods — the
// FontString holding the spoken text (bubble + 0x31c, created with the
// bubble as its parent) shows up in `bubble:GetRegions()`, so the modern
// "iterate GetRegions, find the FontString, read GetText()" idiom works
// unchanged. This is the pre-7.2.5 "walk WorldFrame children" pattern the
// wiki describes, but handed the exact set instead of the whole world.
//
// The engine keeps active bubbles on an intrusive Storm `TSList`
// (`VAR_CHAT_BUBBLE_LIST_HEAD` / `OFF_CHAT_BUBBLE_LINK_NEXT` in Offsets.h);
// the list terminator carries the low-bit sentinel. We mirror the engine's own walk in the per-frame update
// `FUN_004b1060` / teardown `FUN_004b0f80` verbatim. Bubbles whose owner
// unit has despawned are pruned on the engine's next update tick, so a
// just-orphaned bubble can linger one frame — harmless, and addons that
// care filter on `bubble:IsShown()` (a real method here) anyway.
//
// `includeForbidden` is accepted and ignored: vanilla has no forbidden
// frames, so every bubble is returned regardless.

#include "Game.h"
#include "Offsets.h"
#include "ui/FrameObject.h"

#include <cstdint>

namespace ChatBubble::Info {

static int __fastcall Script_GetAllChatBubbles(void *L) {
    Game::Lua::NewTable(L);

    uintptr_t node = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_CHAT_BUBBLE_LIST_HEAD));
    int nextIndex = 1;
    while (node != 0 && (node & 1) == 0) {
        Game::Lua::PushNumber(L, static_cast<double>(nextIndex++));
        UI::FrameObject::Push(L, reinterpret_cast<void *>(node));
        Game::Lua::SetTable(L, -3);
        node = *reinterpret_cast<const uintptr_t *>(node + Offsets::OFF_CHAT_BUBBLE_LINK_NEXT);
    }
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_ChatBubbles", "GetAllChatBubbles",
                                     &Script_GetAllChatBubbles);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace ChatBubble::Info
