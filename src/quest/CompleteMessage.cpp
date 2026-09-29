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

// "<quest> completed." in chat for every quest turn-in, chains included.
//
// On a turn-in (SMSG_QUESTGIVER_QUEST_COMPLETE) the client prints
// ERR_QUEST_COMPLETE_S as a CHAT_MSG_SYSTEM line, with the
// igQuestListComplete sound, and then the experience and money lines. But
// it prints it only for a quest with no next quest in its chain: the
// handler skips the message when the quest's cached record names a
// NextQuestInChain, and for a quest not yet cached, the callback it leaves
// makes the same test when the record arrives. A turn-in in the middle of a
// chain prints only "Experience gained" and "Received".
//
// Addons that follow turn-ins through that line miss every quest with a
// follow-up. Zygor Guides Viewer 2.0 completes a "turn in" step only on it
// (QuestTracking.lua), so its steps for chain quests stayed open until the
// next login's completed-quest query. The 3.3.5a port of the same addon
// still has the line as its only live turn-in signal, on guides full of
// chains, which works only if 3.3.5a prints it for those quests too.
//
// Both tests are dropped: each `jne` over the message becomes two NOPs. A
// chain quest's turn-in then goes through the engine's own message path, with
// the same text, sound and chat type, and in the same place before the
// experience and money lines. Chain-end quests are unchanged. The bytes are
// checked before they are written, so a client whose code differs there is
// left alone.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Quest::CompleteMessage {

namespace {

// Each site: the test and its jump as the client has them, starting two
// bytes before the `jne`. The `jne rel8` is what gets replaced.
struct Site {
    uintptr_t jump;       // address of the `jne`
    uint8_t original[4];  // `test reg,reg; jne rel8`
};

constexpr Site kSites[] = {
    // test ecx,ecx; jne 0x005DC4F1
    {Offsets::PATCH_QUEST_COMPLETE_MSG_CHAIN_SKIP, {0x85, 0xC9, 0x75, 0x13}},
    // test eax,eax; jne 0x005DC76C
    {Offsets::PATCH_QUEST_COMPLETE_MSG_CHAIN_SKIP_LATE, {0x85, 0xC0, 0x75, 0x14}},
};

constexpr uint8_t kNops[2] = {0x90, 0x90};

// Runs on every in-game registration (each /reload too); a site already
// patched no longer matches and is skipped.
void Apply() {
    for (const Site &site : kSites) {
        const auto *code = reinterpret_cast<const uint8_t *>(site.jump - 2);
        if (std::memcmp(code, site.original, sizeof site.original) != 0)
            continue; // already applied, or not the code this was derived from
        Common::PatchBytes(reinterpret_cast<void *>(site.jump), kNops, sizeof kNops);
    }
}

const Game::ModuleAutoRegister _autoreg{&Apply};

} // namespace

} // namespace Quest::CompleteMessage
