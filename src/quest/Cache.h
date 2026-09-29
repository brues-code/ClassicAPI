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

#pragma once

#include <cstdint>

namespace Quest::Cache {

// Calls the engine's quest cache `_GetRecord` at `FUN_DBCACHE_QUEST_GET_RECORD`.
//
// With `fireRequest=false`, performs only the hash-table lookup. Returns
// the cached data block (`entry+0x18` per the engine's accessor) if the
// record is fully loaded, or nullptr if not cached / not yet loaded.
//
// With `fireRequest=true`, queues a CMSG_QUEST_QUERY when the data isn't
// already loaded and registers the supplied `callback` (called with
// `userData` and a 0/1 success byte once SMSG_QUEST_QUERY_RESPONSE
// lands). When the data is already loaded, returns immediately and does
// NOT invoke the callback — same as the item cache. Caller is responsible
// for any "synthesise success=1 ourselves on cache hit" logic.
//
// With `unique`, a request for a record that is still pending does not
// register `callback`/`userData` again when that pair is already queued, so
// a caller that re-requests on every redraw waits on one callback instead of
// adding one per call.
//
// Internal layout of the returned data block (offsets within the block):
//   +0x9C  inline `char title[N]` — null-terminated, locale-applied
const uint8_t *Lookup(uint32_t questID, void *callback, void *userData,
                      bool unique = false);

// Convenience: pure cache-state probe with no request side effect.
inline const uint8_t *Peek(uint32_t questID) {
    return Lookup(questID, nullptr, nullptr);
}

// Field offsets within the data block returned by `Lookup`.
//
// Derived from the engine's own `Script_GetQuestLog*` accessors at
// `0x004DF930..0x004E12B0`. Each accessor calls `_GetRecord` then
// reads from one of these offsets; the existing comment block on each
// constant identifies the source function. See CLAUDE.md's
// "Quest static-info cache" section for the full trace.

// Numeric metadata fields, in wire order from `SMSG_QUEST_QUERY_RESPONSE`.
//
// The cache layout mirrors the packet's field order — the deserializer
// at the SMSG handler unpacks 15 uint32 fields at +0x00..+0x38 before
// the reward/choice item arrays. Names and semantics cross-referenced
// from the in-binary serializer (`FUN_006DBCEC`) plus the canonical
// vanilla 1.12 packet structure documented by vmangos/cmangos emulator
// projects. Fields without a direct binary-side accessor (so just
// known from wire-format alignment) are marked **(hypothesis)** —
// they should be verified empirically before being relied on for
// gameplay logic.
//
// QuestId — uint32, echo of the questID this entry caches.
constexpr int OFF_QUEST_ID = 0x00;
// QuestMethod — uint32. Server-side classification (typically `2` for
// active-and-available, `0` for unavailable). Vanilla doesn't use
// this for gameplay; it's stored but no engine accessor reads it.
// **(hypothesis)**
constexpr int OFF_QUEST_METHOD = 0x04;
// Quest level — int32. Source: `FUN_004df340` (called by
// `Script_GetQuestLogTitle`) does `mov eax, [block + 0x08]` and
// pushes it as `level`.
constexpr int OFF_LEVEL = 0x08;
// ZoneOrSort — int32. The packet's `ZoneOrSort` field, used by the
// quest log UI to group entries under headers:
//   - Positive  = `AreaTable.dbc` zone areaID (e.g. 132 = Coldridge
//                 Valley, 12 = Elwynn Forest)
//   - Negative  = `-QuestSort.dbc` row (e.g. -263 = "Druid" sort)
// `FUN_004dee60` reads this directly and matches it against
// `VAR_QUEST_LOG_CATEGORY_TABLE` to find the collapsible header index.
constexpr int OFF_ZONE_OR_SORT = 0x0C;
// QuestInfo.dbc row — uint32 row index into the small DBC at
// `0x00C0D9CC` (records) / `0x00C0D9D0` (count). Each record is
// `{id@+0, char *name[locale]@+4}`; the localized name is the
// quest-type tag string ("Dungeon", "Raid", "Group", "PvP", "Elite"
// — modern WoW's `frequency`/`questTag`). Source: `FUN_004df2a0`.
constexpr int OFF_QUEST_INFO_ROW = 0x10;
// +0x14, +0x18, +0x1C, +0x20, +0x24 — five uint32 cache slots that
// SMSG_QUEST_QUERY_RESPONSE in vanilla 1.12 doesn't populate. The
// struct has them (the serializer at `0x006DBCEC` writes them as part
// of the WDB save / network shape), but the response handler leaves
// them zero-filled. Verified empirically across three diverse quests
// (race-restricted starter, dungeon, authored-timer delivery) — all
// five slots zero in every case.
//
// Wire-format docs (vmangos / cmangos emulator quest_template) name
// them SuggestedPlayers, LimitTime, RequiredRaces, RequiredSkill,
// RequiredSkillValue respectively, but the vanilla server *enforces*
// these constraints server-side and filters quests before broadcasting
// them, so the client never needs the data. Addons that want race /
// class / skill / timer info must source from an external scraped DB
// (pfQuest-style). Constants intentionally omitted — no use case.
//
// Signed money delta — int32. Positive = reward copper, negative =
// `-required` copper. The engine has two accessors that share this
// field with complementary sign:
//   - `Script_GetQuestLogRewardMoney`   returns max(0, +money)
//   - `Script_GetQuestLogRequiredMoney` returns max(0, -money)
constexpr int OFF_MONEY_DELTA = 0x28;
// Level-cap reward money bonus — int32. Added to the reward when
// the player's level is ≥ 60. Source: `Script_GetQuestLogRewardMoney`
// does `cmp [player.unit + 0x70], 0x3C` (level >= 60) then
// `add edx, [block + 0x2C]`.
constexpr int OFF_LEVEL_CAP_BONUS_MONEY = 0x2C;
// Reward spellID — int32. 0 = no spell reward. Source:
// `Script_GetQuestLogRewardSpell` does `mov eax, [block + 0x30]` then
// resolves the SpellIcon path and locale-applied name via `Spell.dbc`.
// This is a spell *learned by the player* on completion (recipes,
// taught spells); not to be confused with `OFF_SRC_ITEM_ID` which
// is the item the questgiver hands over at accept-time.
constexpr int OFF_REWARD_SPELL_ID = 0x30;
// SrcItemId — itemID given to the player when the quest is accepted.
// 0 = no source item. The questgiver hands this item over so the
// player can complete the quest objective (e.g. "Read the Verdant
// Sigil" gives item 9580; player uses it, then turns the same item
// back in).
//
// Empirically derived (the offset was originally interpreted as
// `RewardSpellCast` from one emulator wire-order variant, but a
// real-world dump of quest 3120 stored `9580` here while the quest's
// own item objective also references item 9580 — consistent with
// SrcItemId semantics, not with a spell-cast-at-turn-in).
constexpr int OFF_SRC_ITEM_ID = 0x34;
// Quest flags — uint32. Bit definitions match modern WoW's
// `QUEST_FLAGS_*` enum, of which only bit 3 (`0x08` SHARABLE) is
// confirmed-tested by the engine (`Script_GetQuestLogPushable`'s
// `test byte ptr [block + 0x38], 0x08`). Other bits are presumed
// stored but unverified.
constexpr int OFF_QUEST_FLAGS = 0x38;
// Reward and choice item arrays — up to 4 reward and 6 choice
// itemIDs stored inline (4 bytes each). 0 in any slot means
// "no item in this slot". Used by `GetQuestLogItemLink` /
// `GetQuestLogItemID`.
//
// Each itemID array is paired with a parallel uint32 count array
// (the "how many of this item" reward). Layout in cache memory:
//   +0x3C  RewardItems[4]
//   +0x4C  RewardItemsCount[4]
//   +0x5C  ChoiceItems[6]
//   +0x74  ChoiceItemsCount[6]
// Verified from `Script_GetQuestLogRewardInfo` (`fild [block + slot*4 + 0x4C]`
// for numItems) and `Script_GetQuestLogChoiceInfo` (`fild [block + slot*4 + 0x74]`).
constexpr int OFF_REWARD_ITEMS = 0x3C;
constexpr int OFF_REWARD_ITEMS_COUNT = 0x4C;
constexpr int OFF_CHOICE_ITEMS = 0x5C;
constexpr int OFF_CHOICE_ITEMS_COUNT = 0x74;
constexpr int REWARD_COUNT = 4;
constexpr int CHOICE_COUNT = 6;
// Point-of-interest marker — server-supplied map coordinate hint
// used by the questgiver objective panel to display "go here" arrows
// in modern WoW. Set on quests that have a designated location;
// 0/0/0 = no POI. Read sequentially by the serializer at
// `0x006DBE5B..0x006DBE8E` using mixed integer + float wire methods.
// **(hypothesis)**
constexpr int OFF_POI_MAP_ID = 0x8C; // uint32
constexpr int OFF_POI_X = 0x90;      // float
constexpr int OFF_POI_Y = 0x94;      // float
constexpr int OFF_POI_OPT = 0x98;    // uint32 — display option, or doubles as title length
constexpr int OFF_TITLE = 0x9C;
// Inline narrative text — both locale-applied and printf-style
// format-expanded by the engine's variable substituter at
// `0x00506F70` (handles tokens like `$N` for player name, `$C` for
// class, etc.). Source: `Script_GetQuestLogQuestText` reads both,
// pushes `description` first then `objectives`.
//
// We deliberately expose the *raw* inline bytes, not the format-
// expanded version, because the substituter wants a 1024-byte
// scratch buffer and the substitutions (player name, class) belong
// in the addon layer anyway. Addons can run `string.gsub` if they
// need the runtime values, otherwise raw text is more useful for
// guides / databases.
constexpr int OFF_OBJECTIVES = 0x29C;
constexpr int OFF_DESCRIPTION = 0xA9C;
// Completion text — the "talk to me now" body shown on the
// quest-reward UI panel after objectives are met. 0x200 (512) bytes
// inline. Modern API exposes this as `GetQuestLogCompletionText()`.
// Confirmed by the cache copy at `0x00562E1F..0x00562E36` (a 0x200-byte
// memcpy from src+0x129C to dst+0x129C) and by the serializer at
// `0x006DBEBE` (`lea eax, [edi+0x129C]` followed by the string-writer
// call). Vanilla LeaderBoard reads from this offset for some override
// cases but the engine never directly exposes the text to Lua —
// `Script_GetQuestLogQuestText` returns description + objectives only.
constexpr int OFF_COMPLETION_TEXT = 0x129C;
// Objective arrays — four uint32[4] parallel slabs carrying
// SMSG_QUEST_QUERY_RESPONSE's RequiredNPCOrGo / Count / Item / Count
// fields verbatim. NPC-or-GO is **signed**: positive = creature
// entry, negative = gameobject entry (engine strips the sign bit
// before the cache lookup). Empty slots hold 0 — `Script_GetQuestLogLeaderBoard`
// at 0x004E0110 skips zero entries when matching its 1-based objIdx
// arg, walking the NPC/GO array first then the item array (total max 8
// objectives). Verified by decoding the four `[esi + edi*4 + 0x14XC]`
// reads and the cache-instance routing in that function:
//   - 0xC0E354 (creature cache) for positive 0x149C → "monster"
//   - 0xC0E318 (gameobject cache) for negative 0x149C → "object"
//   - 0xC0E2A0 (item cache) for 0x14BC → "item"
constexpr int OFF_REQUIRED_NPC_OR_GO = 0x149C;        // int32[4]
constexpr int OFF_REQUIRED_NPC_OR_GO_COUNT = 0x14AC;  // uint32[4]
constexpr int OFF_REQUIRED_ITEM = 0x14BC;             // uint32[4]
constexpr int OFF_REQUIRED_ITEM_COUNT = 0x14CC;       // uint32[4]
constexpr int OBJECTIVE_COUNT = 4;
// Per-objective override text — array of 4 inline char buffers,
// 256 (0x100) bytes per entry, indexed in objective declaration
// order (NPC/GO slots fill first, then item slots — same order the
// LeaderBoard accessor enumerates). Empty string = no override
// (engine auto-formats as `"<NPC name>: X/Y"`); otherwise the
// engine displays the buffer verbatim. Confirmed by the
// `shl ecx, 8; mov dl, [block + ecx + 0x14DC]` access pattern in
// `Script_GetQuestLogLeaderBoard` and by the four sequential reads
// in the packet serializer at `0x006DBF17..0x006DBF41` (0x14DC,
// 0x15DC, 0x16DC, 0x17DC).
constexpr int OFF_OBJECTIVE_TEXT = 0x14DC;
constexpr int OBJECTIVE_TEXT_STRIDE = 0x100;
constexpr int OBJECTIVE_TEXT_COUNT = 4;

} // namespace Quest::Cache
