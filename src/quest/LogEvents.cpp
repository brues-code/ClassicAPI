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

// `QUEST_ACCEPTED(questLogIndex, questID)` and `QUEST_REMOVED(questID)`,
// sourced from the engine's own per-quest-slot accept/remove gate.
//
// Hook target: `FUN_QUEST_SLOT_ANNOUNCE_OBSERVER` — the bank-PLAYER
// descriptor observer the engine registers once per quest slot
// (`FUN_005DD8A0`, 20 nodes at OFF_DESC_PLAYER_QUEST_LOG_FIRST + N *
// DESC_PLAYER_QUEST_LOG_SLOT_SIZE). It is where the engine decides a slot
// just took a new quest — and announces "Quest accepted: <name>" — or lost
// one.
//
// ## Why this function and not the log rebuild
//
// 3.3.5 fires QUEST_ACCEPTED from the exact analogue of this observer
// (`FUN_006DF370` there; 25 slots x 0x14 instead of 20 x 0xC), in this
// order:
//
//     FUN_005E6940(1)                      ; rebuild the quest log FIRST
//     slot = (fieldOffset - 0x28) / 0x14
//     if (accept gate) {
//         rec = questCache(newID, cb)
//         if (rec) {
//             <announce "Quest accepted: <name>">
//             idx = <add to watch list>     ; returns 1-based log index
//             FireEvent(QUEST_ACCEPTED, "%d", idx)
//         }
//     }
//     if (remove gate) <drop from watch list>   ; no event in 3.3.5
//
// Three things follow, and we mirror all three:
//
//   1. The message comes BEFORE the event. An addon filtering the accept
//      line out of CHAT_MSG_SYSTEM sees a current quest log; an addon
//      listening for QUEST_ACCEPTED sees the line already printed.
//   2. The "is this a new quest" decision is the ENGINE's, read off the
//      slot's old-vs-new bytes — not a set diff over the log. That gets
//      the login bulk sync right for free (the observer's mirror is seeded
//      from live, so a resync diffs to nothing and neither the engine nor
//      we announce), and it catches the re-announce the engine does when a
//      completed quest goes back to incomplete, which a questID set diff
//      cannot see.
//   3. The log must be current before the announce, and 3.3.5 guarantees
//      that by calling the rebuild at the TOP of the observer rather than
//      by ordering observer nodes. We do the same. On 1.12 this also fixes
//      a real defect: the announcer's slot-0 node and the log-rebuild node
//      share descriptor anchor OFF_DESC_PLAYER_QUEST_LOG_FIRST (the
//      rebuild watches all 20 slots from that one anchor), and the
//      announcer is registered first (both callers run FUN_005DD8A0 before
//      FUN_004908C0, which registers the rebuild node). So for slot 0 —
//      i.e. accepting into an EMPTY quest log — the engine announced a
//      quest that no quest-log API could see yet. Slots 1..19 sit in later
//      field buckets and were already fine. Rebuilding here is immune to
//      that ordering, and to any other DLL perturbing it.
//
// We gate the rebuild on the accepted quest not already being in the log,
// which makes it a no-op whenever the engine's own node already ran (the
// common case) and self-correcting when it hasn't. 3.3.5 rebuilds
// unconditionally; this is strictly cheaper and equally correct, since the
// only thing the announce needs is that THIS quest is resolvable.
//
// ## Cold quest cache
//
// `FUN_QUEST_LOG_REBUILD` silently drops a quest whose static data is not
// cached yet, and the announcer likewise announces nothing and queues a
// deferred announce on the quest cache. So on a cold `questcache.wdb` the
// first accept of a quest resolves a round trip later. We ride the same
// queue: post-original we append our own cache callback, which lands
// AFTER the engine's two (the rebuild's, queued by our pre-original call,
// and the deferred announce's, queued by the original). When
// SMSG_QUEST_QUERY_RESPONSE arrives the order is therefore rebuild ->
// announce -> our event, matching the warm-cache order exactly.
//
// Known gap, not worth machinery: `FUN_QUEST_LOG_REBUILD` no-ops entirely
// while a previous rebuild still has an outstanding query, so an accept
// landing inside that window can be announced with the quest absent from
// the log. The cache is a hit, so no callback is queued and we skip the
// event rather than report a bogus index. It needs a quest accepted in the
// few hundred ms after a cold-cache login resync; 3.3.5 has the same
// window.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "quest/Cache.h"
#include "quest/Log.h"

#include <cstddef>
#include <cstdint>

namespace Quest::LogEvents {

namespace {

constexpr const char *kAcceptedEventName = "QUEST_ACCEPTED";
constexpr const char *kRemovedEventName = "QUEST_REMOVED";

const Event::Custom::AutoReserve _reserveAccepted{kAcceptedEventName};
const Event::Custom::AutoReserve _reserveRemoved{kRemovedEventName};

// One 12-byte quest slot, in both the live descriptor mirror at
// `[CGPlayer + OFF_CGPLAYER_INFO] + OFF_CGPLAYER_INFO_QUEST_LIST` and the
// observer's `oldValues` snapshot — same layout, which is what lets the
// engine's gate compare them field for field.
struct QuestSlot {
    int32_t questID;
    uint8_t pad[3];
    uint8_t state; // +0x07, bit QUEST_SLOT_STATE_COMPLETE
    uint8_t rest[4];
};
static_assert(sizeof(QuestSlot) == Offsets::CGPLAYER_INFO_QUEST_LIST_STRIDE,
              "quest slot record must match the engine's stride");
static_assert(offsetof(QuestSlot, state) == Offsets::OFF_QUEST_SLOT_STATE,
              "quest slot state byte offset");

bool IsComplete(const QuestSlot &s) {
    return (s.state & Offsets::QUEST_SLOT_STATE_COMPLETE) != 0;
}

// The engine's own accept gate, verbatim: a slot that just took a quest,
// or one whose quest went from complete back to incomplete (which the
// engine re-announces).
bool IsAccept(const QuestSlot &now, const QuestSlot &was) {
    if (now.questID == 0)
        return false;
    if (was.questID == 0)
        return true;
    return now.questID == was.questID && !IsComplete(now) && IsComplete(was);
}

// The engine's own remove gate: the slot held a quest and no longer holds
// that one (cleared, or replaced by a different quest).
bool IsRemove(const QuestSlot &now, const QuestSlot &was) {
    return was.questID != 0 && now.questID != was.questID;
}

// The slot record currently live on the player object. Resolved from the
// GUID the observer hands us via the NON-THROWING object resolver, exactly
// as the engine's sibling callbacks do — this runs inside
// SMSG_UPDATE_OBJECT processing, where FUN_RESOLVE_UNIT_TOKEN's Lua raise
// would unwind through raw engine code.
const QuestSlot *LiveSlot(uint32_t guidLo, uint32_t guidHi, int slot) {
    if (slot < 0 || slot >= Offsets::CGPLAYER_INFO_QUEST_LIST_MAX)
        return nullptr;
    auto *player = static_cast<const uint8_t *>(Object::ByGuid(
        Offsets::TYPEMASK_PLAYER,
        (static_cast<uint64_t>(guidHi) << 32) | guidLo, "ClassicAPI"));
    if (player == nullptr)
        return nullptr;
    auto *info = *reinterpret_cast<const uint8_t *const *>(
        player + Offsets::OFF_CGPLAYER_INFO);
    if (info == nullptr)
        return nullptr;
    return reinterpret_cast<const QuestSlot *>(
        info + Offsets::OFF_CGPLAYER_INFO_QUEST_LIST +
        slot * Offsets::CGPLAYER_INFO_QUEST_LIST_STRIDE);
}

// Fire QUEST_ACCEPTED if the quest is resolvable in the log; report
// whether it was. `Quest::Log::IndexForQuestID` succeeding is exactly
// equivalent to the cache lookup the announcer just made — the rebuild
// admits precisely the cache-resolved quests — so this doubles as "did
// the engine actually announce".
bool FireAcceptedIfInLog(int questID) {
    const int index = Quest::Log::IndexForQuestID(questID);
    if (index < 0)
        return false;
    const int slot = _reserveAccepted.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "%d%d", index + 1, questID);
    return true;
}

// Deferred accept, for a quest whose static data had to be fetched. Shape
// per `Quest::Data::QuestLoadCallback` — the engine invokes it as
// `__stdcall(userData, success)` with `ret 8`; the questID rides in
// `userData`.
void __stdcall AcceptLoaded(void *userData, int success) {
    if (success == 0)
        return;
    FireAcceptedIfInLog(static_cast<int>(reinterpret_cast<uintptr_t>(userData)));
}

// Observer callback ABI per the FUN_DESC_OBSERVER_REGISTER block in
// Offsets.h: __fastcall(fieldOffset /*ecx, as registered*/, size /*edx*/,
// guidLo, guidHi, oldValues*, userArg), returns 1, callee cleans 0x10.
using QuestSlotObserver_t = int(__fastcall *)(uint32_t fieldOffset, uint32_t size,
                                              uint32_t guidLo, uint32_t guidHi,
                                              const void *oldValues, void *userArg);
using QuestLogRebuild_t = void(__fastcall *)(int mode);

QuestSlotObserver_t g_origObserver = nullptr;

int __fastcall QuestSlotObserver_h(uint32_t fieldOffset, uint32_t size,
                                   uint32_t guidLo, uint32_t guidHi,
                                   const void *oldValues, void *userArg) {
    const int slot =
        static_cast<int>(fieldOffset - Offsets::OFF_DESC_PLAYER_QUEST_LOG_FIRST) /
        Offsets::DESC_PLAYER_QUEST_LOG_SLOT_SIZE;
    const QuestSlot *live = LiveSlot(guidLo, guidHi, slot);
    if (live == nullptr || oldValues == nullptr)
        return g_origObserver(fieldOffset, size, guidLo, guidHi, oldValues, userArg);

    // Copy both sides before the original runs — it is free to mutate the
    // slot, and the mirror is re-synced out from under us afterwards.
    const QuestSlot now = *live;
    const QuestSlot was = *static_cast<const QuestSlot *>(oldValues);
    const bool accepted = IsAccept(now, was);
    const bool removed = IsRemove(now, was);

    // Make the log current before the engine announces. No-op once this
    // quest is already in it, which is the usual case.
    if (accepted && Quest::Log::IndexForQuestID(now.questID) < 0) {
        reinterpret_cast<QuestLogRebuild_t>(Offsets::FUN_QUEST_LOG_REBUILD)(1);
    }

    const int rc = g_origObserver(fieldOffset, size, guidLo, guidHi, oldValues, userArg);

    if (accepted && !FireAcceptedIfInLog(now.questID)) {
        // Cold cache: the original announced nothing and queued a deferred
        // announce. Append ours behind it so the event still trails the
        // message when the response lands.
        Quest::Cache::Lookup(
            static_cast<uint32_t>(now.questID),
            reinterpret_cast<void *>(&AcceptLoaded),
            reinterpret_cast<void *>(static_cast<uintptr_t>(now.questID)));
    }
    if (removed) {
        const int evt = _reserveRemoved.Slot();
        if (evt >= 0)
            Event::Custom::Fire(evt, "%d", was.questID);
    }
    return rc;
}

} // namespace

static const Game::HookAutoRegister _observerHook{
    Offsets::FUN_QUEST_SLOT_ANNOUNCE_OBSERVER,
    reinterpret_cast<void *>(&QuestSlotObserver_h),
    reinterpret_cast<void **>(&g_origObserver)};

} // namespace Quest::LogEvents
