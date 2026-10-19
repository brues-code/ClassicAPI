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

// Raid-target icons in speech bubbles — 4.3.4's CGChatBubbleFrame::
// ParseRaidIcons. 4.3.4's bubble Initialize strips the line's hyperlinks,
// expands `{rt1}`/`{skull}`/… into icon markup, then sets the text. 1.12's
// bubble builder (FUN_004B1600) strips into a stack buffer and sets it with
// one CALL; that call is redirected here, so the tags are expanded after the
// strip, exactly where 4.3.4 does it. The strip has to come first: it drops
// every `|` code it does not know, an icon included.
//
// The message reached the builder through Chat::Dispatch, so the `|T`
// anti-spoof sanitize already ran; only the fixed marker escapes are added.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "chat/RaidMarkers.h"
#include "debug/Log.h"

#include <cstdint>
#include <cstring>

namespace ChatBubble::RaidIcons {

namespace {

using SetText_t = void(__fastcall *)(void *fs, void *edx, const char *text, int expand);

// The redirected call: `__thiscall(fs, text, expand)`, RET 8 — the dummy-EDX
// __fastcall shape matches its registers and stack.
void __fastcall SetBubbleText(void *fs, void *edx, const char *text, int expand) {
    // The builder's own buffer is 0x800 bytes.
    char buf[0x800];
    const char *withIcons = Chat::RaidMarkers::Substitute(text, buf, sizeof buf);
    reinterpret_cast<SetText_t>(static_cast<uintptr_t>(Offsets::FUN_FONTSTRING_SET_TEXT))(
        fs, edx, withIcons, expand);
}

// Once per process; a patched site no longer reaches FUN_FONTSTRING_SET_TEXT,
// so a second pass would refuse anyway.
bool g_applied = false;

void Apply() {
    if (g_applied)
        return;
    g_applied = true;
    auto *site = reinterpret_cast<uint8_t *>(Offsets::PATCH_CHAT_BUBBLE_SET_TEXT_CALL);
    int32_t rel;
    std::memcpy(&rel, site + 1, sizeof rel);
    if (site[0] != 0xE8 ||
        reinterpret_cast<uintptr_t>(site) + 5 + rel != Offsets::FUN_FONTSTRING_SET_TEXT) {
        Debug::Log::Printf("ChatBubble::RaidIcons: the bubble SetText call is not the stock "
                           "CALL; bubbles keep raid tags as text");
        return;
    }
    rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&SetBubbleText) -
                               (reinterpret_cast<uintptr_t>(site) + 5));
    Common::PatchBytes(site + 1, &rel, sizeof rel);
}

const Game::ModuleAutoRegister _autoreg{&Apply};

} // namespace

} // namespace ChatBubble::RaidIcons
