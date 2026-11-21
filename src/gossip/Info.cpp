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

// `C_GossipInfo.*`, as the 3.80.2 client does it, over 1.12's gossip state.
//
// The data is the engine's two gossip arrays at `VAR_GOSSIP_OPTIONS` (16
// entries, stride `0x80C`) and `VAR_GOSSIP_QUESTS` (32 entries, stride
// `0x20C`), filled by the SMSG_GOSSIP_MESSAGE handler at `0x004E26E0` and
// reset at each open. These are the arrays the engine's own
// `Script_GetGossip*` read.
//
// 1.12 has a SECOND questgiver path that this namespace also covers: NPCs
// without a gossip menu send SMSG_QUESTGIVER_QUEST_LIST (QUEST_GREETING, the
// QuestFrameGreetingPanel), whose data lands in storage separate from the
// gossip arrays (the VAR_GREETING_* block in Offsets.h). 3.80.2 serves gossip
// menus only, but 1.12's greeting functions give titles and levels only, so
// this is the one place an addon gets a greeting window's quest IDs and
// completion. The lists serve whichever session is live:
//   - gossip session (VAR_GOSSIP_NPC_GUID != 0, greeting not pending)
//                                                   -> gossip arrays
//   - greeting session (VAR_QUESTGIVER_GUID != 0
//     and == VAR_GREETING_NPC_GUID)                 -> greeting arrays
//   - neither                                       -> empty
// 3.80.2 empties its lists on close (FUN_1419529f0). 1.12 keeps both
// storages until the next open, so the GUID checks keep the previous NPC
// out. Options stay gossip-only: greetings have none.
//
// 3.80.2's GossipOptionUIInfo (FUN_1419548f0): `flags` is the option record's
// flags (QuestLabelPrepend 1, PlayMovieLabelPrepend 2), which 1.12 does not
// send, so 0. `status` and `selectOptionWhenOnlyOption` come from data 1.12
// does not have: Available and false. `spellID`, `overrideIconID` and
// `failureDescription` stay nil. `icon` stays 1.12's icon type: 3.80.2's is a
// file ID, and SetTexture here takes no file IDs. 3.80.2 does not export the
// code-entry flag.
//
// 3.80.2's GossipQuestUIInfo (FUN_140c88d00) has 12 fields. `isImportant`,
// `isMeta` and `isIgnored` are always false there; `isLegendary`,
// `repeatable` and `frequency` come from flags that 1.12 does not send.
// `isTrivial` is the quest log's gray test, `questInfoID` the quest-type tag
// of the cached quest. `isComplete` is set only on active quests (nil on
// available ones, as in 3.80.2), from the packet's "ready to turn in" status.
//
// The selectors find their entry by optionID / questID (SelectOptionByIndex
// by display position) and call the engine's own selectors, so the send path
// and its checks are the engine's. A bad ID or a closed window does nothing.

#include "Game.h"
#include "Offsets.h"
#include "quest/Cache.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Gossip::Info {

namespace {

using EngineScriptFn = int(__fastcall *)(void *L);

// A gossip session is live from GOSSIP_SHOW until the close worker
// (0x004E1FA0) zeroes the GUID and fires GOSSIP_CLOSED. The GUID is
// latched on SMSG_GOSSIP_MESSAGE, before the greeting resolves, so the
// engine's own pending flag (what its option selector checks) keeps the
// not-yet-shown window out.
bool GossipSessionActive() {
    return Game::Read<uint64_t>(Offsets::VAR_GOSSIP_NPC_GUID) != 0 &&
           Game::Read<uint32_t>(Offsets::VAR_GOSSIP_TEXT_PENDING) == 0;
}

// A quest-greeting session is live while the quest-session GUID is set
// AND the greeting arrays were filled for that same NPC. The second
// check matters because a gossip-menu quest click opens a quest session
// at panel 1 without ever touching the greeting arrays; without it,
// leftovers from an older greeting NPC would leak through.
bool GreetingSessionActive() {
    const uint64_t giver = Game::Read<uint64_t>(Offsets::VAR_QUESTGIVER_GUID);
    return giver != 0 && giver == Game::Read<uint64_t>(Offsets::VAR_GREETING_NPC_GUID);
}

const uint8_t *GreetingEntry(bool active, int slot) {
    const uintptr_t base = active ? Offsets::VAR_GREETING_ACTIVE_ENTRIES
                                  : Offsets::VAR_GREETING_AVAILABLE_ENTRIES;
    return reinterpret_cast<const uint8_t *>(
        base + static_cast<uintptr_t>(slot) * Offsets::GREETING_QUESTS_STRIDE);
}

int GreetingCount(bool active) {
    const int count = Game::Read<int>(active ? Offsets::VAR_GREETING_ACTIVE_COUNT
                                             : Offsets::VAR_GREETING_AVAILABLE_COUNT);
    return (count < 0)                              ? 0
           : (count > Offsets::GREETING_QUESTS_MAX) ? Offsets::GREETING_QUESTS_MAX
                                                    : count;
}

// The greeting arrays drop the per-quest dialog status, but the packet
// handler's scratch arrays (packet order, zeroed per packet) keep it:
// status 4 = complete/turn-in-ready, same sentinel as the gossip rows.
bool GreetingQuestIsComplete(uint32_t questID) {
    const auto *ids = reinterpret_cast<const uint32_t *>(Offsets::VAR_GREETING_SCRATCH_IDS);
    const auto *status = reinterpret_cast<const uint32_t *>(Offsets::VAR_GREETING_SCRATCH_STATUS);
    for (int i = 0; i < Offsets::GREETING_QUESTS_MAX; ++i) {
        if (ids[i] == questID)
            return status[i] == 4;
    }
    return false;
}

uint32_t GreetingQuestID(bool active, int slot) {
    return Game::Read<uint32_t>(GreetingEntry(active, slot), Offsets::OFF_GREETING_QUEST_ID);
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
    return Game::Read<int32_t>(entry, Offsets::OFF_GOSSIP_OPTION_INDEX);
}

uint32_t QuestID(const uint8_t *entry) {
    return Game::Read<uint32_t>(entry, Offsets::OFF_GOSSIP_QUEST_ID);
}

uint32_t QuestStatus(const uint8_t *entry) {
    return Game::Read<uint32_t>(entry, Offsets::OFF_GOSSIP_QUEST_STATUS);
}

// Vanilla's two active-quest sentinels — anything else marks an
// available (deliverable) quest. Mirrors `FUN_004E2430`'s filter.
bool IsActiveQuest(uint32_t status) {
    return status == 3 || status == 4;
}

// The quest log's gray test (QuestLogFrame: `-levelDiff >
// GetQuestGreenRange()`): the player is more than the gray gap above the
// quest's level. The gap is the table GetQuestGreenRange (0x004E17D0) reads,
// indexed by the player's level / 5 and saturated at the last entry.
bool IsTrivial(int32_t questLevel) {
    const uint8_t *player = Unit::Identity::PlayerDescriptor();
    if (player == nullptr)
        return false;
    const int32_t playerLevel = Game::Read<int32_t>(player, Offsets::OFF_UNIT_FIELD_LEVEL);
    const uint32_t index = static_cast<uint32_t>(playerLevel / 5);
    const uint32_t clamped =
        index < Offsets::QUEST_GRAY_GAPS_COUNT ? index : Offsets::QUEST_GRAY_GAPS_COUNT - 1;
    const auto *gaps = reinterpret_cast<const int32_t *>(Offsets::VAR_QUEST_GRAY_GAPS);
    return playerLevel - questLevel > gaps[clamped];
}

// The QuestInfo.dbc id ("Group", "Dungeon", "Raid", ...) of the cached
// quest, 0 when the quest is not cached. Reads the cache only; sends no
// query.
uint32_t QuestInfoID(uint32_t questID) {
    const uint8_t *data = Quest::Cache::Peek(questID);
    return data != nullptr ? Game::Read<uint32_t>(data, Quest::Cache::OFF_QUEST_INFO_ROW) : 0;
}

// `C_GossipInfo.GetText()` — the quest greeting buffer (what
// `Script_GetGreetingText` pushes) while only a greeting session is live,
// else the gossip greeting buffer (what `Script_GetGossipText` pushes). As in
// 3.80.2 (FUN_141953220 is its only writer), nothing clears the gossip
// buffer: after close, and while the next NPC's text loads, it holds the
// previous text.
const Game::Doc::Field kGetTextRets[] = {
    Game::Doc::Req("gossipText", "cstring"),
};
const Game::Doc::Function kGetText{
    "Returns the greeting text of the gossip or quest greeting window.", {}, kGetTextRets};

int __fastcall Script_C_GossipInfo_GetText(void *L) {
    const uintptr_t buf = !GossipSessionActive() && GreetingSessionActive()
                              ? Offsets::VAR_QUEST_GREETING_TEXT
                              : Offsets::VAR_GOSSIP_GREETING_TEXT;
    Game::Lua::PushString(L, reinterpret_cast<const char *>(buf));
    return 1;
}

// `C_GossipInfo.GetOptions()` — the options in display order.
const Game::Doc::Field kGossipOptionFields[] = {
    Game::Doc::Req("gossipOptionID", "number", "The ID that SelectOption takes."),
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("icon", "number",
                   "The icon type that the server sends, 0 to 10 for the default types; not a "
                   "fileID."),
    Game::Doc::Req("status", "GossipOptionStatus", "Always Available."),
    Game::Doc::Req("flags", "number", "Always 0."),
    Game::Doc::Req("selectOptionWhenOnlyOption", "bool", "Always false."),
    Game::Doc::Req("orderIndex", "number", "The 1-based position in the list."),
};
const Game::Doc::Structure kGossipOptionUIInfo{"GossipOptionUIInfo", "GossipInfo",
                                               kGossipOptionFields, "One gossip option."};

const Game::Doc::Field kGetOptionsRets[] = {
    Game::Doc::Req("info", "table",
                   "Array of GossipOptionUIInfo in display order; empty when no gossip window "
                   "is open."),
};
const Game::Doc::Function kGetOptions{"Returns the options of the open gossip window.", {},
                                      kGetOptionsRets};

constexpr int GOSSIP_OPTION_STATUS_AVAILABLE = 0;

int __fastcall Script_C_GossipInfo_GetOptions(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    if (!GossipSessionActive())
        return 1;

    int outIdx = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        const uint8_t *entry = OptionEntry(slot);
        const int32_t optIdx = OptionIndex(entry);
        if (optIdx < 0)
            continue;
        outIdx += 1;

        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "gossipOptionID", static_cast<double>(optIdx));
        Game::Lua::SetFieldString(
            L, "name", reinterpret_cast<const char *>(entry + Offsets::OFF_GOSSIP_OPTION_TEXT));
        Game::Lua::SetFieldNumber(L, "icon",
                                  static_cast<double>(entry[Offsets::OFF_GOSSIP_OPTION_ICON]));
        Game::Lua::SetFieldNumber(L, "status", GOSSIP_OPTION_STATUS_AVAILABLE);
        Game::Lua::SetFieldNumber(L, "flags", 0);
        Game::Lua::SetFieldBool(L, "selectOptionWhenOnlyOption", false);
        Game::Lua::SetFieldNumber(L, "orderIndex", static_cast<double>(outIdx));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// Sets t[index] = one GossipQuestUIInfo on the table at the top of the
// stack. `isComplete` only for an active quest; nil on an available one.
void PushQuestRow(void *L, int index, uint32_t questID, const char *title, int32_t level,
                  bool active, bool complete) {
    Game::Lua::PushNumber(L, static_cast<double>(index));
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "title", title);
    Game::Lua::SetFieldNumber(L, "questLevel", static_cast<double>(level));
    Game::Lua::SetFieldBool(L, "isTrivial", IsTrivial(level));
    Game::Lua::SetFieldNumber(L, "frequency", 0); // Enum.QuestFrequency.Default
    Game::Lua::SetFieldBool(L, "repeatable", false);
    if (active)
        Game::Lua::SetFieldBool(L, "isComplete", complete);
    Game::Lua::SetFieldBool(L, "isLegendary", false);
    Game::Lua::SetFieldBool(L, "isIgnored", false);
    Game::Lua::SetFieldNumber(L, "questID", static_cast<double>(questID));
    Game::Lua::SetFieldBool(L, "isImportant", false);
    Game::Lua::SetFieldBool(L, "isMeta", false);
    Game::Lua::SetFieldNumber(L, "questInfoID", static_cast<double>(QuestInfoID(questID)));
    Game::Lua::SetTable(L, -3);
}

// Shared body for `GetAvailableQuests` / `GetActiveQuests`. The gossip
// arrays hold both sides, split by the `status` field; the greeting
// storage keeps one array per side.
int PushQuestList(void *L, bool wantActive) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    if (!GossipSessionActive()) {
        if (!GreetingSessionActive())
            return 1;
        const int count = GreetingCount(wantActive);
        for (int slot = 0; slot < count; ++slot) {
            const uint8_t *entry = GreetingEntry(wantActive, slot);
            const uint32_t questID = GreetingQuestID(wantActive, slot);
            PushQuestRow(
                L, slot + 1, questID,
                reinterpret_cast<const char *>(entry + Offsets::OFF_GREETING_QUEST_TITLE),
                Game::Read<int32_t>(entry, Offsets::OFF_GREETING_QUEST_LEVEL), wantActive,
                wantActive && GreetingQuestIsComplete(questID));
        }
        return 1;
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
        // status 4 is the server's "ready to turn in"; the engine branches on
        // the same value for the gossip quest icon.
        PushQuestRow(L, ++outIdx, questID,
                     reinterpret_cast<const char *>(entry + Offsets::OFF_GOSSIP_QUEST_TITLE),
                     Game::Read<int32_t>(entry, Offsets::OFF_GOSSIP_QUEST_LEVEL), wantActive,
                     status == 4);
    }
    return 1;
}

const Game::Doc::Field kGossipQuestFields[] = {
    Game::Doc::Req("title", "string"),
    Game::Doc::Req("questLevel", "number"),
    Game::Doc::Req("isTrivial", "bool",
                   "True when the quest is gray: the player is too far above its level."),
    Game::Doc::Opt("frequency", "QuestFrequency", nullptr, "Always Default."),
    Game::Doc::Opt("repeatable", "bool", nullptr, "Always false."),
    Game::Doc::Opt("isComplete", "bool", nullptr,
                   "True when the quest is ready to turn in; nil on an available quest."),
    Game::Doc::Req("isLegendary", "bool", "Always false."),
    Game::Doc::Req("isIgnored", "bool", "Always false."),
    Game::Doc::Req("questID", "number"),
    Game::Doc::Req("isImportant", "bool", "Always false."),
    Game::Doc::Req("isMeta", "bool", "Always false."),
    Game::Doc::Req("questInfoID", "number",
                   "The quest's type tag (QuestInfo), 0 when the quest data is not loaded."),
};
const Game::Doc::Structure kGossipQuestUIInfo{"GossipQuestUIInfo", "GossipInfo",
                                              kGossipQuestFields,
                                              "One quest in a questgiver window."};

const Game::Doc::Field kQuestListRets[] = {
    Game::Doc::Req("info", "table",
                   "Array of GossipQuestUIInfo in display order; empty when no questgiver "
                   "window is open."),
};
const Game::Doc::Function kGetAvailableQuests{
    "Returns the quests that the open gossip or quest greeting window offers.", {},
    kQuestListRets};
const Game::Doc::Function kGetActiveQuests{
    "Returns the quests that the open gossip or quest greeting window can take back.", {},
    kQuestListRets};

int __fastcall Script_C_GossipInfo_GetAvailableQuests(void *L) {
    return PushQuestList(L, /*wantActive=*/false);
}

int __fastcall Script_C_GossipInfo_GetActiveQuests(void *L) {
    return PushQuestList(L, /*wantActive=*/true);
}

int CountOptions() {
    int count = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        if (OptionIndex(OptionEntry(slot)) >= 0)
            count += 1;
    }
    return count;
}

const Game::Doc::Field kNumOptionsRets[] = {
    Game::Doc::Req("numOptions", "number", nullptr),
};
const Game::Doc::Function kGetNumOptions{
    "Returns the number of options that GetOptions returns.", {}, kNumOptionsRets};

int __fastcall Script_C_GossipInfo_GetNumOptions(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(GossipSessionActive() ? CountOptions() : 0));
    return 1;
}

int CountQuests(bool wantActive) {
    if (!GossipSessionActive())
        return GreetingSessionActive() ? GreetingCount(wantActive) : 0;
    int count = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_QUESTS_MAX; ++slot) {
        const uint8_t *entry = QuestEntry(slot);
        if (QuestID(entry) == 0)
            break;
        if (IsActiveQuest(QuestStatus(entry)) == wantActive)
            count += 1;
    }
    return count;
}

const Game::Doc::Field kNumQuestsRets[] = {
    Game::Doc::Req("numQuests", "number"),
};
const Game::Doc::Function kGetNumAvailableQuests{
    "Returns the number of quests that GetAvailableQuests returns.", {}, kNumQuestsRets};
const Game::Doc::Function kGetNumActiveQuests{
    "Returns the number of quests that GetActiveQuests returns.", {}, kNumQuestsRets};

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

const Game::Doc::Function kGetNumGossipAvailableQuests{
    "Returns the number of quests that GetGossipAvailableQuests returns.", {}, kNumQuestsRets,
    "GossipGlobals"};
const Game::Doc::Function kGetNumGossipActiveQuests{
    "Returns the number of quests that GetGossipActiveQuests returns.", {}, kNumQuestsRets,
    "GossipGlobals"};

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

// `GetNumGossipOptions()` — 3.3.5 pushes m_gossipOptionsCount. 1.12 keeps no
// count and has no counter like the quest ones: Script_GetGossipOptions
// (0x004E28D0) walks all 16 slots, skips a negative index, and pushes a
// (text, type) pair per remaining slot, with no session check. This counts
// exactly those slots.
int __fastcall Script_GetNumGossipOptions(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(CountOptions()));
    return 1;
}

const Game::Doc::Function kGetNumGossipOptions{
    "Returns the number of options GetGossipOptions returns.", {}, kNumOptionsRets, "GossipGlobals"};

// Selectors — the engine helpers the `Script_SelectGossip*` Lua wrappers
// call after parsing their arguments, called with the 0-based slot/index.
using SelectOption_t = void(__fastcall *)(unsigned slot0Based, const char *password);
using SelectQuest_t = void(__fastcall *)(int idx0Based);

void EngineSelectOption(int slot0Based, const char *password) {
    reinterpret_cast<SelectOption_t>(Offsets::FUN_GOSSIP_SELECT_OPTION)(
        static_cast<unsigned>(slot0Based), password);
}

// `text` is the code for an option that asks for one; the engine helper
// sends it only for such an option. 3.80.2's third argument, `confirmed`,
// answers an option's confirmation box, which 1.12 options do not have.
const char *OptionText(void *L) {
    return Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
}

const Game::Doc::Field kSelectOptionArgs[] = {
    Game::Doc::Req("gossipOptionID", "number", "The gossipOptionID of a GetOptions entry."),
    Game::Doc::Opt("text", "cstring", nullptr,
                   "The code to send with an option that asks for one."),
    Game::Doc::Opt("confirmed", "bool", nullptr, "Has no effect."),
};
const Game::Doc::Function kSelectOption{"Selects a gossip option by its ID.", kSelectOptionArgs,
                                        {}};

int __fastcall Script_C_GossipInfo_SelectOption(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_GossipInfo.SelectOption(optionID [, text, confirmed])");
        return 0;
    }
    const int target = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (!GossipSessionActive())
        return 0; // stale option slots must not send a CMSG

    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        const int32_t optIdx = OptionIndex(OptionEntry(slot));
        if (optIdx >= 0 && optIdx == target) {
            EngineSelectOption(slot, OptionText(L));
            return 0;
        }
    }
    return 0;
}

// `C_GossipInfo.SelectOptionByIndex(orderIndex [, text, confirmed])` —
// `orderIndex` is 1-based in display order, as `GetOptions()` numbers it.
const Game::Doc::Field kSelectOptionByIndexArgs[] = {
    Game::Doc::Req("orderIndex", "number", "The orderIndex of a GetOptions entry."),
    Game::Doc::Opt("text", "cstring", nullptr,
                   "The code to send with an option that asks for one."),
    Game::Doc::Opt("confirmed", "bool", nullptr, "Has no effect."),
};
const Game::Doc::Function kSelectOptionByIndex{"Selects a gossip option by its display position.",
                                               kSelectOptionByIndexArgs, {}};

int __fastcall Script_C_GossipInfo_SelectOptionByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
                         "Usage: C_GossipInfo.SelectOptionByIndex(optionID [, text, confirmed])");
        return 0;
    }
    const int target = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (!GossipSessionActive())
        return 0;

    int orderIndex = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        if (OptionIndex(OptionEntry(slot)) < 0)
            continue;
        if (++orderIndex == target) {
            EngineSelectOption(slot, OptionText(L));
            return 0;
        }
    }
    return 0;
}

// Finds `questID` among the rows of the wanted side and calls the engine
// helper with its 0-based position in that filtered list, the index shape
// the helper's own walk expects.
int SelectQuestByID(void *L, bool wantActive, const char *errUsage) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, errUsage);
        return 0;
    }
    const uint32_t target = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));

    if (!GossipSessionActive()) {
        if (!GreetingSessionActive())
            return 0;
        // The workers vanilla's Script_Select{Available,Active}Quest call:
        // they index straight into the greeting array and check the
        // questgiver GUID themselves before sending.
        const int count = GreetingCount(wantActive);
        for (int slot = 0; slot < count; ++slot) {
            if (GreetingQuestID(wantActive, slot) == target) {
                reinterpret_cast<SelectQuest_t>(wantActive
                                                    ? Offsets::FUN_GREETING_SELECT_ACTIVE_QUEST
                                                    : Offsets::FUN_GREETING_SELECT_AVAILABLE_QUEST)(
                    slot);
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
            reinterpret_cast<SelectQuest_t>(wantActive
                                                ? Offsets::FUN_GOSSIP_SELECT_ACTIVE_QUEST
                                                : Offsets::FUN_GOSSIP_SELECT_AVAILABLE_QUEST)(idx);
            return 0;
        }
        idx += 1;
    }
    return 0;
}

const Game::Doc::Field kSelectQuestArgs[] = {
    Game::Doc::Req("questID", "number"),
};
const Game::Doc::Function kSelectAvailableQuest{
    "Selects a quest that the open questgiver window offers.", kSelectQuestArgs, {}};
const Game::Doc::Function kSelectActiveQuest{
    "Selects a quest that the open questgiver window can take back.", kSelectQuestArgs, {}};

int __fastcall Script_C_GossipInfo_SelectAvailableQuest(void *L) {
    return SelectQuestByID(L, /*wantActive=*/false,
                           "Usage: C_GossipInfo.SelectAvailableQuest(optionID)");
}

int __fastcall Script_C_GossipInfo_SelectActiveQuest(void *L) {
    return SelectQuestByID(L, /*wantActive=*/true,
                           "Usage: C_GossipInfo.SelectActiveQuest(optionID)");
}

const Game::Doc::Function kCloseGossip{"Closes the gossip window.", {}, {}};

int __fastcall Script_C_GossipInfo_CloseGossip(void *L) {
    return reinterpret_cast<EngineScriptFn>(Offsets::FUN_SCRIPT_CLOSE_GOSSIP)(L);
}

// Enum.GossipOptionStatus, registered by 3.80.2's C_GossipInfo loader
// (FUN_140c8bd70).
const Game::Lua::EnumIntegerEntry kGossipOptionStatus[] = {
    {"Available", GOSSIP_OPTION_STATUS_AVAILABLE},
    {"Unavailable", 1},
    {"Locked", 2},
    {"AlreadyComplete", 3},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetText", &Script_C_GossipInfo_GetText,
                                     &kGetText);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetOptions",
                                     &Script_C_GossipInfo_GetOptions, &kGetOptions);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetAvailableQuests",
                                     &Script_C_GossipInfo_GetAvailableQuests,
                                     &kGetAvailableQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetActiveQuests",
                                     &Script_C_GossipInfo_GetActiveQuests, &kGetActiveQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumOptions",
                                     &Script_C_GossipInfo_GetNumOptions, &kGetNumOptions);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumAvailableQuests",
                                     &Script_C_GossipInfo_GetNumAvailableQuests,
                                     &kGetNumAvailableQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumActiveQuests",
                                     &Script_C_GossipInfo_GetNumActiveQuests,
                                     &kGetNumActiveQuests);
    Game::Lua::RegisterGlobalFunction("GetNumGossipAvailableQuests",
                                      &Script_GetNumGossipAvailableQuests,
                                      &kGetNumGossipAvailableQuests);
    Game::Lua::RegisterGlobalFunction("GetNumGossipActiveQuests",
                                      &Script_GetNumGossipActiveQuests,
                                      &kGetNumGossipActiveQuests);
    Game::Lua::RegisterGlobalFunction("GetNumGossipOptions", &Script_GetNumGossipOptions,
                                      &kGetNumGossipOptions);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectOption",
                                     &Script_C_GossipInfo_SelectOption, &kSelectOption);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectOptionByIndex",
                                     &Script_C_GossipInfo_SelectOptionByIndex,
                                     &kSelectOptionByIndex);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectAvailableQuest",
                                     &Script_C_GossipInfo_SelectAvailableQuest,
                                     &kSelectAvailableQuest);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectActiveQuest",
                                     &Script_C_GossipInfo_SelectActiveQuest, &kSelectActiveQuest);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "CloseGossip",
                                     &Script_C_GossipInfo_CloseGossip, &kCloseGossip);
    Game::Lua::RegisterIntegerEnum("Enum", "GossipOptionStatus", kGossipOptionStatus,
                                   "GossipInfo");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Gossip::Info
