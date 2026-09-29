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

// `C_GossipInfo.*` — retail-shaped wrappers around vanilla 1.12's
// flat gossip surface (`GetGossipText`, `GetGossipOptions`,
// `GetGossipAvailableQuests`, `GetGossipActiveQuests`, plus the
// `SelectGossip*` selectors).
//
// All of the underlying data lives in the engine's two gossip-state
// arrays at `VAR_GOSSIP_OPTIONS` (16 entries, stride `0x80C`) and
// `VAR_GOSSIP_QUESTS` (32 entries, stride `0x20C`), populated by the
// SMSG_GOSSIP_MESSAGE handler at `0x004E26E0` and cleared each open.
// We read the same shape the engine's own `Script_GetGossip*` reads.
//
// Vanilla has a SECOND questgiver path this namespace also covers:
// NPCs without a gossip menu send SMSG_QUESTGIVER_QUEST_LIST instead —
// the QUEST_GREETING event and the QuestFrameGreetingPanel ("Current
// Quests" / "Available Quests") — whose data lands in a storage
// entirely separate from the gossip arrays (see the VAR_GREETING_*
// block in Offsets.h). Retail keeps the two APIs disjoint, but for
// backporting it's far more useful for `C_GossipInfo` to serve
// whichever questgiver session is actually live:
//   - gossip session live (VAR_GOSSIP_NPC_GUID != 0)   → gossip arrays
//   - greeting session live (VAR_QUESTGIVER_GUID != 0
//     and == VAR_GREETING_NPC_GUID)                    → greeting arrays
//   - neither                                           → empty / ""
// The GUID gates double as staleness protection: both storages persist
// after their frames close (the engine never clears them until the
// next open), so ungated reads would report the previous NPC.
// `GetOptions`/`GetNumOptions` stay gossip-only — greetings have no
// options.
//
// Missing modern fields — vanilla 1.12's server simply never sends
// them, so there's nothing to surface:
//   - `rewards`, `spellID` (post-vanilla; merged in with the
//     quest/spell system rework)
//   - `status` (Available/Unavailable/Locked/AlreadyComplete — vanilla
//     server doesn't per-option compute this)
//   - `overrideIconID`, `selectOptionWhenOnlyOption` — modern UX hints
//
// Selectors translate the modern (`gossipOptionID` / `questID`) arg
// shape back to vanilla's 1-based slot index, then tail-call the
// engine's existing `Script_SelectGossip*` so we share the engine's
// CMSG-send path and error semantics.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Gossip::Info {

namespace {

using EngineScriptFn = int(__fastcall *)(void *L);

uint64_t ReadU64(uintptr_t va) {
    return *reinterpret_cast<const uint64_t *>(va);
}

// A gossip session is live from SMSG_GOSSIP_MESSAGE until the close
// worker (0x004E1FA0) zeroes the GUID and fires GOSSIP_CLOSED.
bool GossipSessionActive() {
    return ReadU64(Offsets::VAR_GOSSIP_NPC_GUID) != 0;
}

// A quest-greeting session is live while the quest-session GUID is set
// AND the greeting arrays were filled for that same NPC. The second
// check matters because a gossip-menu quest click opens a quest session
// at panel 1 without ever touching the greeting arrays — without it,
// leftovers from an older greeting NPC would leak through.
bool GreetingSessionActive() {
    const uint64_t giver = ReadU64(Offsets::VAR_QUESTGIVER_GUID);
    return giver != 0 && giver == ReadU64(Offsets::VAR_GREETING_NPC_GUID);
}

const uint8_t *GreetingEntry(bool active, int slot) {
    const uintptr_t base = active ? Offsets::VAR_GREETING_ACTIVE_ENTRIES
                                  : Offsets::VAR_GREETING_AVAILABLE_ENTRIES;
    return reinterpret_cast<const uint8_t *>(
        base + static_cast<uintptr_t>(slot) * Offsets::GREETING_QUESTS_STRIDE);
}

int GreetingCount(bool active) {
    const int count = *reinterpret_cast<const int *>(
        active ? Offsets::VAR_GREETING_ACTIVE_COUNT
               : Offsets::VAR_GREETING_AVAILABLE_COUNT);
    return (count < 0)                            ? 0
           : (count > Offsets::GREETING_QUESTS_MAX) ? Offsets::GREETING_QUESTS_MAX
                                                    : count;
}

// The greeting arrays drop the per-quest dialog status, but the packet
// handler's scratch arrays (packet order, zeroed per packet) keep it —
// status 4 = complete/turn-in-ready, same sentinel as the gossip rows.
bool GreetingQuestIsComplete(uint32_t questID) {
    const auto *ids = reinterpret_cast<const uint32_t *>(
        Offsets::VAR_GREETING_SCRATCH_IDS);
    const auto *status = reinterpret_cast<const uint32_t *>(
        Offsets::VAR_GREETING_SCRATCH_STATUS);
    for (int i = 0; i < Offsets::GREETING_QUESTS_MAX; ++i) {
        if (ids[i] == questID)
            return status[i] == 4;
    }
    return false;
}

const uint8_t *OptionEntry(int slot) {
    return reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_GOSSIP_OPTIONS) +
        static_cast<uintptr_t>(slot) * Offsets::GOSSIP_OPTIONS_STRIDE);
}

const uint8_t *QuestEntry(int slot) {
    return reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_GOSSIP_QUESTS) +
        static_cast<uintptr_t>(slot) * Offsets::GOSSIP_QUESTS_STRIDE);
}

int32_t OptionIndex(const uint8_t *entry) {
    return *reinterpret_cast<const int32_t *>(
        entry + Offsets::OFF_GOSSIP_OPTION_INDEX);
}

uint32_t QuestID(const uint8_t *entry) {
    return *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_GOSSIP_QUEST_ID);
}

uint32_t QuestStatus(const uint8_t *entry) {
    return *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_GOSSIP_QUEST_STATUS);
}

// Vanilla's two active-quest sentinels — anything else marks an
// available (deliverable) quest. Mirrors `FUN_004E2430`'s filter.
bool IsActiveQuest(uint32_t status) {
    return status == 3 || status == 4;
}

// `C_GossipInfo.GetText()` — the greeting string of whichever
// questgiver session is live: the gossip buffer (what
// `Script_GetGossipText` pushes) for a gossip session, the quest
// greeting buffer (what `Script_GetGreetingText` pushes) for a
// greeting session, "" when neither is open. Both buffers persist
// after close, so the gates keep stale text from the previous NPC
// out.
int __fastcall Script_C_GossipInfo_GetText(void *L) {
    uintptr_t buf = 0;
    if (GossipSessionActive())
        buf = Offsets::VAR_GOSSIP_GREETING_TEXT;
    else if (GreetingSessionActive())
        buf = Offsets::VAR_QUEST_GREETING_TEXT;
    Game::Lua::PushString(
        L, (buf != 0) ? reinterpret_cast<const char *>(buf) : "");
    return 1;
}

// `C_GossipInfo.GetOptions()` — returns an array of `GossipOptionUIInfo`
// tables in display order. Only fields the 1.12 server actually
// transmits are populated:
//
//   gossipOptionID  — vanilla `optionIndex` (the same value `SelectOption`
//                     looks up).
//   name            — option text.
//   icon            — vanilla 0..10 icon-type byte (gossip / vendor /
//                     taxi / trainer / healer / binder / banker /
//                     petition / tabard / battlemaster / auctioneer).
//                     NOT a retail-style fileID; addons that want a
//                     texture path do the lookup themselves.
//   flags           — bit 0 = boxCoded (password-protected option).
//   orderIndex      — 1-based position in the emitted list (matches
//                     `SelectGossipOption`'s expected 1-based arg).
int __fastcall Script_C_GossipInfo_GetOptions(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    if (!GossipSessionActive())
        return 1; // greeting sessions have no options; stale data stays hidden

    int outIdx = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        const uint8_t *entry = OptionEntry(slot);
        const int32_t optIdx = OptionIndex(entry);
        if (optIdx < 0)
            continue;
        outIdx += 1;

        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);

        Game::Lua::SetFieldNumber(L, "gossipOptionID",
                                  static_cast<double>(optIdx));
        Game::Lua::SetFieldString(L, "name", reinterpret_cast<const char *>(
            entry + Offsets::OFF_GOSSIP_OPTION_TEXT));
        Game::Lua::SetFieldNumber(L, "icon",
            static_cast<double>(entry[Offsets::OFF_GOSSIP_OPTION_ICON]));
        const uint8_t boxCoded = entry[Offsets::OFF_GOSSIP_OPTION_BOX_CODED];
        Game::Lua::SetFieldNumber(L, "flags",
            static_cast<double>(boxCoded ? 1u : 0u));
        Game::Lua::SetFieldNumber(L, "orderIndex",
                                  static_cast<double>(outIdx));

        Game::Lua::SetTable(L, -3);
    }

    return 1;
}

// Greeting-session variant: walks the pre-split greeting array for the
// wanted side. Same table shape as the gossip rows (questID / title /
// questLevel / isComplete) so consumers can't tell the sources apart.
int PushGreetingQuestList(void *L, bool wantActive) {
    const int count = GreetingCount(wantActive);
    for (int slot = 0; slot < count; ++slot) {
        const uint8_t *entry = GreetingEntry(wantActive, slot);
        const uint32_t questID = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_GREETING_QUEST_ID);

        Game::Lua::PushNumber(L, static_cast<double>(slot + 1));
        Game::Lua::NewTable(L);

        Game::Lua::SetFieldNumber(L, "questID",
                                  static_cast<double>(questID));
        Game::Lua::SetFieldString(L, "title", reinterpret_cast<const char *>(
            entry + Offsets::OFF_GREETING_QUEST_TITLE));
        const int32_t level = *reinterpret_cast<const int32_t *>(
            entry + Offsets::OFF_GREETING_QUEST_LEVEL);
        Game::Lua::SetFieldNumber(L, "questLevel",
                                  static_cast<double>(level));
        if (wantActive)
            Game::Lua::SetFieldBool(L, "isComplete",
                                    GreetingQuestIsComplete(questID));

        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// Shared body for `GetAvailableQuests` / `GetActiveQuests`. Both walk
// the same backing array and split by the `status` field — `wantActive`
// inverts the filter.
int PushQuestList(void *L, bool wantActive) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    if (!GossipSessionActive()) {
        if (GreetingSessionActive())
            return PushGreetingQuestList(L, wantActive);
        return 1; // no questgiver session — empty table, never stale data
    }

    int outIdx = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_QUESTS_MAX; ++slot) {
        const uint8_t *entry = QuestEntry(slot);
        const uint32_t questID = QuestID(entry);
        if (questID == 0)
            break;
        const uint32_t status = QuestStatus(entry);
        if (IsActiveQuest(status) != wantActive)
            continue;
        outIdx += 1;

        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);

        Game::Lua::SetFieldNumber(L, "questID",
                                  static_cast<double>(questID));
        Game::Lua::SetFieldString(L, "title", reinterpret_cast<const char *>(
            entry + Offsets::OFF_GOSSIP_QUEST_TITLE));
        const int32_t level = *reinterpret_cast<const int32_t *>(
            entry + Offsets::OFF_GOSSIP_QUEST_LEVEL);
        Game::Lua::SetFieldNumber(L, "questLevel",
                                  static_cast<double>(level));
        // `isComplete` is only meaningful for active quests; vanilla's
        // status==4 row corresponds to "ready to turn in" (the engine
        // also branches on this value in the gossip-icon-color path).
        if (wantActive)
            Game::Lua::SetFieldBool(L, "isComplete", status == 4);

        Game::Lua::SetTable(L, -3);
    }

    return 1;
}

int __fastcall Script_C_GossipInfo_GetAvailableQuests(void *L) {
    return PushQuestList(L, /*wantActive=*/false);
}

int __fastcall Script_C_GossipInfo_GetActiveQuests(void *L) {
    return PushQuestList(L, /*wantActive=*/true);
}

int __fastcall Script_C_GossipInfo_GetNumOptions(void *L) {
    int count = 0;
    if (GossipSessionActive()) {
        for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
            if (OptionIndex(OptionEntry(slot)) >= 0)
                count += 1;
        }
    }
    Game::Lua::PushNumber(L, static_cast<double>(count));
    return 1;
}

int CountQuests(bool wantActive) {
    if (!GossipSessionActive())
        return GreetingSessionActive() ? GreetingCount(wantActive) : 0;
    int count = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_QUESTS_MAX; ++slot) {
        const uint8_t *entry = QuestEntry(slot);
        const uint32_t questID = QuestID(entry);
        if (questID == 0)
            break;
        if (IsActiveQuest(QuestStatus(entry)) == wantActive)
            count += 1;
    }
    return count;
}

int __fastcall Script_C_GossipInfo_GetNumAvailableQuests(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(CountQuests(false)));
    return 1;
}

int __fastcall Script_C_GossipInfo_GetNumActiveQuests(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(CountQuests(true)));
    return 1;
}

// `GetNumGossipAvailableQuests()` / `GetNumGossipActiveQuests()` — the 2.0+
// globals: how many quests vanilla's GetGossipAvailableQuests /
// GetGossipActiveQuests list, from the engine counters those two size their
// lists by, so a loop over one always matches the other. Gossip menus only,
// as on later clients; a questgiver without a menu is counted by vanilla's
// own GetNumAvailableQuests / GetNumActiveQuests (QUEST_GREETING).
using GossipCount_t = uint32_t(__cdecl *)();

int __fastcall Script_GetNumGossipAvailableQuests(void *L) {
    auto count = reinterpret_cast<GossipCount_t>(Offsets::FUN_GOSSIP_NUM_AVAILABLE_QUESTS);
    Game::Lua::PushNumber(L, static_cast<double>(count()));
    return 1;
}

int __fastcall Script_GetNumGossipActiveQuests(void *L) {
    auto count = reinterpret_cast<GossipCount_t>(Offsets::FUN_GOSSIP_NUM_ACTIVE_QUESTS);
    Game::Lua::PushNumber(L, static_cast<double>(count()));
    return 1;
}

// Selectors — translate the retail-shape arg into the engine's
// 0-based slot/index and call the engine helper directly. The helpers
// are what the `Script_SelectGossip*` Lua wrappers tail-call after
// parsing their stack args; calling them ourselves skips a redundant
// Lua-stack-stomp + ftol round-trip.

using SelectOption_t = void(__fastcall *)(unsigned slot0Based, const char *password);
using SelectQuest_t = void(__fastcall *)(int idx0Based);

void EngineSelectOption(int slot0Based, const char *password) {
    auto fn = reinterpret_cast<SelectOption_t>(
        Offsets::FUN_GOSSIP_SELECT_OPTION);
    fn(static_cast<unsigned>(slot0Based), password);
}

void EngineSelectAvailableQuest(int idx0Based) {
    auto fn = reinterpret_cast<SelectQuest_t>(
        Offsets::FUN_GOSSIP_SELECT_AVAILABLE_QUEST);
    fn(idx0Based);
}

void EngineSelectActiveQuest(int idx0Based) {
    auto fn = reinterpret_cast<SelectQuest_t>(
        Offsets::FUN_GOSSIP_SELECT_ACTIVE_QUEST);
    fn(idx0Based);
}

// Greeting-session selectors — the workers vanilla's
// Script_Select{Available,Active}Quest call. Index straight into the
// corresponding greeting array (they validate the questgiver GUID
// internally before sending the CMSG).
void EngineSelectGreetingQuest(bool wantActive, int idx0Based) {
    auto fn = reinterpret_cast<SelectQuest_t>(
        wantActive ? Offsets::FUN_GREETING_SELECT_ACTIVE_QUEST
                   : Offsets::FUN_GREETING_SELECT_AVAILABLE_QUEST);
    fn(idx0Based);
}

// `C_GossipInfo.SelectOption(gossipOptionID[, text])` — wraps vanilla's
// `SelectGossipOption(slotIndex)`. `text` is the boxCoded password,
// passed through to the engine helper which gates on the
// password-required flag internally.
int __fastcall Script_C_GossipInfo_SelectOption(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_GossipInfo.SelectOption(gossipOptionID [, text])");
        return 0;
    }
    const int target = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *password =
        Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    if (!GossipSessionActive())
        return 0; // stale option slots must not send a CMSG

    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        const uint8_t *entry = OptionEntry(slot);
        const int32_t optIdx = OptionIndex(entry);
        if (optIdx < 0)
            continue;
        if (optIdx == target) {
            EngineSelectOption(slot, password);
            return 0;
        }
    }
    return 0;
}

// `C_GossipInfo.SelectOptionByIndex(orderIndex)` — `orderIndex` is
// 1-based in display order (matches what `GetOptions()` puts on each
// entry). The engine helper takes the 0-based array slot.
int __fastcall Script_C_GossipInfo_SelectOptionByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_GossipInfo.SelectOptionByIndex(orderIndex)");
        return 0;
    }
    const int slot0Based = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot0Based < 0 || slot0Based >= Offsets::GOSSIP_OPTIONS_MAX ||
        !GossipSessionActive())
        return 0;
    EngineSelectOption(slot0Based, nullptr);
    return 0;
}

// Walks `GOSSIP_QUESTS`, counting only rows whose active/available
// status matches `wantActive`. On a `questID` match, calls the right
// engine helper with the 0-based position into that filtered list —
// which is the index shape the helper's own internal walk expects.
int SelectQuestByID(void *L, bool wantActive, const char *errUsage) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, errUsage);
        return 0;
    }
    const uint32_t target = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));

    if (!GossipSessionActive()) {
        if (!GreetingSessionActive())
            return 0;
        const int count = GreetingCount(wantActive);
        for (int slot = 0; slot < count; ++slot) {
            const uint32_t questID = *reinterpret_cast<const uint32_t *>(
                GreetingEntry(wantActive, slot) +
                Offsets::OFF_GREETING_QUEST_ID);
            if (questID == target) {
                EngineSelectGreetingQuest(wantActive, slot);
                return 0;
            }
        }
        return 0;
    }

    int idx = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_QUESTS_MAX; ++slot) {
        const uint8_t *entry = QuestEntry(slot);
        const uint32_t questID = QuestID(entry);
        if (questID == 0)
            break;
        if (IsActiveQuest(QuestStatus(entry)) != wantActive)
            continue;
        if (questID == target) {
            if (wantActive)
                EngineSelectActiveQuest(idx);
            else
                EngineSelectAvailableQuest(idx);
            return 0;
        }
        idx += 1;
    }
    return 0;
}

int __fastcall Script_C_GossipInfo_SelectAvailableQuest(void *L) {
    return SelectQuestByID(L, /*wantActive=*/false,
        "Usage: C_GossipInfo.SelectAvailableQuest(questID)");
}

int __fastcall Script_C_GossipInfo_SelectActiveQuest(void *L) {
    return SelectQuestByID(L, /*wantActive=*/true,
        "Usage: C_GossipInfo.SelectActiveQuest(questID)");
}

int __fastcall Script_C_GossipInfo_CloseGossip(void *L) {
    return reinterpret_cast<EngineScriptFn>(
        Offsets::FUN_SCRIPT_CLOSE_GOSSIP)(L);
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetText",
                                     &Script_C_GossipInfo_GetText);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetOptions",
                                     &Script_C_GossipInfo_GetOptions);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetAvailableQuests",
                                     &Script_C_GossipInfo_GetAvailableQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetActiveQuests",
                                     &Script_C_GossipInfo_GetActiveQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumOptions",
                                     &Script_C_GossipInfo_GetNumOptions);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumAvailableQuests",
                                     &Script_C_GossipInfo_GetNumAvailableQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumActiveQuests",
                                     &Script_C_GossipInfo_GetNumActiveQuests);
    Game::Lua::RegisterGlobalFunction("GetNumGossipAvailableQuests",
                                      &Script_GetNumGossipAvailableQuests);
    Game::Lua::RegisterGlobalFunction("GetNumGossipActiveQuests",
                                      &Script_GetNumGossipActiveQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectOption",
                                     &Script_C_GossipInfo_SelectOption);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectOptionByIndex",
                                     &Script_C_GossipInfo_SelectOptionByIndex);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectAvailableQuest",
                                     &Script_C_GossipInfo_SelectAvailableQuest);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectActiveQuest",
                                     &Script_C_GossipInfo_SelectActiveQuest);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "CloseGossip",
                                     &Script_C_GossipInfo_CloseGossip);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Gossip::Info
