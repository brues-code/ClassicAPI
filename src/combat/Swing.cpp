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

// `PLAYER_SWING(swingDuration, swingType)` and `Enum.PlayerSwingType` — the
// Classic-beta swing-timer events, backported from packet data 1.12 already
// receives. See CLAUDE.md's "Backport the Classic swing-timer API" entry for
// the full derivation; short version here.
//
// This module keeps its own swing-timer STATE rather than relaying packets
// 1:1, because the server mutates the swing timer from several places that
// don't map to one packet each:
//
//   - a white hit lands                    -> SMSG_ATTACKERSTATEUPDATE, attacker == us
//   - an on-next-swing ability replaces it  -> SMSG_SPELL_GO, Attributes & ON_NEXT_SWING
//   - a cast-time spell is cast             -> SMSG_SPELL_GO, InterruptFlags & AUTOATTACK
//   - autorepeat (Auto Shot / Shoot) fires  -> SMSG_SPELL_GO, AttributesEx2 & AUTOREPEAT
//   - starting a fresh melee attack         -> SMSG_ATTACKSTART (off-hand only)
//   - a weapon is swapped mid-combat        -> no packet; polled off the invMgr GUIDs
//   - the LOCAL PLAYER parries an attack    -> SMSG_ATTACKERSTATEUPDATE, victim == us,
//                                              targetState == PARRY (no separate packet —
//                                              the server just clips OUR remaining time)
//
// Extra attacks (Windfury, Sword Specialization) send N packets for one
// logical reset; firing straight off the packet would emit N `PLAYER_SWING`s
// for the same instant. Instead every mutation above just marks a swing type
// "dirty" and a WorldTick subscriber fires at most one event per type per
// frame, with the CURRENT remaining time — so N packets in one frame still
// produce exactly one event, and a parry-haste correction that lands in the
// same frame as the original reset collapses into it for free.

#include "combat/Swing.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "event/Custom.h"
#include "item/CGItem.h"
#include "item/Location.h"
#include "item/Record.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "net/SendObserver.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cstdint>

namespace Combat::Swing {

namespace {

using Net::CDataStore;
using Time::Clock::NowMs;

// ---- Per-swing-type state -----------------------------------------------

struct State {
    uint32_t endMs = 0;
    bool active = false; // at least one reset has happened this session
    bool dirty = false;  // needs a PLAYER_SWING fire on the next WorldTick
};
State g_swing[3];

void MarkReset(SwingType type, uint32_t durationMs) {
    if (durationMs == 0)
        return; // no weapon for this hand right now -- nothing to report
    State &s = g_swing[type];
    s.endMs = NowMs() + durationMs;
    s.active = true;
    s.dirty = true;
}

bool PlayerInCombat() {
    const uint8_t *desc = Unit::Identity::PlayerDescriptor();
    if (desc == nullptr)
        return false;
    const uint32_t flags =
        *reinterpret_cast<const uint32_t *>(desc + Offsets::OFF_UNIT_FIELD_FLAGS);
    return (flags & Offsets::UNIT_FLAG_IN_COMBAT) != 0;
}

} // namespace

uint32_t AttackTimeMs(SwingType type) {
    const uint8_t *desc = Unit::Identity::PlayerDescriptor();
    if (desc == nullptr)
        return 0;
    const int off = type == MAIN_HAND
                        ? Offsets::OFF_UNIT_FIELD_BASEATTACKTIME
                    : type == OFF_HAND
                        ? Offsets::OFF_UNIT_FIELD_OFFHANDATTACKTIME
                        : Offsets::OFF_UNIT_FIELD_RANGEDATTACKTIME;
    return *reinterpret_cast<const uint32_t *>(desc + off);
}

namespace {

// Reads a unit's bounding radius (the same field `Unit::Range`'s
// `UnitInRange`/`UnitDistanceSquared` and `Combat::SwingRange` read) off its
// descriptor. 0 for a null object or an unpopulated descriptor.
float BoundingRadius(void *obj) {
    if (obj == nullptr)
        return 0.0f;
    auto *fields = *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(obj) + Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr)
        return 0.0f;
    return *reinterpret_cast<const float *>(
        fields + Offsets::OFF_UNIT_FIELD_BOUNDING_RADIUS);
}

} // namespace

bool InMeleeRange(void *player, void *target) {
    float playerPos[3];
    float targetPos[3];
    if (!Unit::Position::Read(player, playerPos) || !Unit::Position::Read(target, targetPos))
        return false;

    const float leeway =
        Game::Read<float>(static_cast<uintptr_t>(Offsets::VAR_MELEE_REACH_LEEWAY));
    const float floorRange =
        Game::Read<float>(static_cast<uintptr_t>(Offsets::VAR_MELEE_REACH_MIN));
    float maxRange = BoundingRadius(player) + BoundingRadius(target) + leeway;
    if (maxRange < floorRange)
        maxRange = floorRange;

    const float dx = playerPos[0] - targetPos[0];
    const float dy = playerPos[1] - targetPos[1];
    return (dx * dx + dy * dy) < (maxRange * maxRange);
}

namespace {
AutoSwingResetOverride *g_overrides = nullptr;
} // namespace

AutoSwingResetOverride::AutoSwingResetOverride(SwingResetOverride fn)
    : fn(fn), next(g_overrides) {
    g_overrides = this;
}

namespace {

// ---- Non-triggered-cast gate ---------------------------------------------
//
// The server's swing-reset rule is `!triggered && InterruptFlags & AUTOATTACK`.
// SMSG_SPELL_GO carries no triggered bit, so we approximate "not triggered"
// by "we ourselves sent CMSG_CAST_SPELL for this id recently" — a proc/aura
// trigger never originates from a client packet. Mirrors the capture-ring
// pattern in `Aura::ComboDuration` (co-hooking the same NetClient send
// funnel via `Net::SendObserver`), simplified: presence within a TTL is
// all that's needed here, no payload to carry.
struct SentCast {
    uint32_t spellId;
    uint32_t tMs;
};
constexpr int kSentCastCount = 8;
SentCast g_sentCasts[kSentCastCount];
int g_sentCastCursor = 0;
constexpr uint32_t kSentCastTtlMs = 3000; // worst-case send -> SPELL_GO roundtrip

void RememberSentCast(uint32_t spellId) {
    g_sentCasts[g_sentCastCursor] = {spellId, NowMs()};
    g_sentCastCursor = (g_sentCastCursor + 1) % kSentCastCount;
}

bool WasNonTriggeredCast(uint32_t spellId) {
    const uint32_t now = NowMs();
    for (auto &c : g_sentCasts) {
        if (c.spellId == spellId && now - c.tMs < kSentCastTtlMs) {
            c.spellId = 0; // consume -- a queued re-cast needs its own capture
            return true;
        }
    }
    return false;
}

void OnSend(uint32_t opcode, CDataStore *packet) {
    if (opcode != Offsets::OP_CMSG_CAST_SPELL)
        return;
    const uint32_t spellId = Net::Read<uint32_t>(packet);
    if (spellId != 0)
        RememberSentCast(spellId);
}

const Net::SendObserver::AutoSubscribe _sendSub{&OnSend};

// ---- Parry haste ----------------------------------------------------------

constexpr uint32_t kDefaultUnarmedDelayMs = 2000;
constexpr int kInvSlotMainHand1Based = 16;
constexpr int kInvSlotOffHand1Based = 17;

// The UNHASTED weapon swing time for a hand. The server's own
// `Unit::GetAttackTime()` unwinds the live percent-mod to recover this, but
// the percent-mod factor never crosses the wire — the descriptor field only
// ever carries the CURRENT (already-hasted) value. Read it from the weapon's
// own item data instead: a static property of the item, independent of any
// haste tracking. `slot1Based` is 16 (main hand) or 17 (off hand); on an
// item-cache miss (rare — see the item-cache-race notes in CLAUDE.md) we
// fall back to the live descriptor value rather than guessing.
uint32_t UnhastedWeaponDelayMs(int slot1Based, SwingType fallbackType) {
    const uint8_t *cgItem = Item::Location::ResolveEquipmentSlot(slot1Based);
    if (cgItem != nullptr) {
        const uint8_t *instance = Item::InstanceBlock(cgItem);
        if (instance != nullptr) {
            const uint32_t itemID = *reinterpret_cast<const uint32_t *>(
                instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID);
            const uint8_t *stats = Item::PeekRecord(itemID);
            if (stats != nullptr) {
                const uint32_t delay = *reinterpret_cast<const uint32_t *>(
                    stats + Offsets::OFF_ITEMSTATS_DELAY);
                if (delay > 0)
                    return delay;
            }
        }
        const uint32_t live = AttackTimeMs(fallbackType);
        if (live > 0)
            return live;
    }
    return kDefaultUnarmedDelayMs;
}

// Mirrors the server's parry-haste correction (tortoise-wow's
// `VICTIMSTATE_PARRY` block, reached from the ATTACKER's damage path when
// WE are the victim and parried): whichever of our hands has less time
// remaining is cut to 20% of its unhasted weapon delay if it was between
// 20% and 60% remaining, or has 40% of the delay subtracted if it was past
// 60% remaining. Under 20% remaining, the hand is left alone (already about
// to swing) — matches the server's `if/else if`, no `else` branch.
void ApplyParryHaste() {
    State &main = g_swing[MAIN_HAND];
    State &off = g_swing[OFF_HAND];
    const uint32_t now = NowMs();
    const uint32_t mainRemaining = Time::Clock::Remaining(now, main.endMs);
    const uint32_t offRemaining = Time::Clock::Remaining(now, off.endMs);

    const bool useOff = off.active && offRemaining < mainRemaining;
    State &target = useOff ? off : main;
    if (!target.active)
        return;

    const uint32_t remaining = useOff ? offRemaining : mainRemaining;
    const uint32_t unhastedDelay = UnhastedWeaponDelayMs(
        useOff ? kInvSlotOffHand1Based : kInvSlotMainHand1Based,
        useOff ? OFF_HAND : MAIN_HAND);

    const float p20 = static_cast<float>(unhastedDelay) * 0.20f;
    const float p60 = 3.0f * p20;
    const auto r = static_cast<float>(remaining);

    uint32_t newRemaining = remaining;
    if (r > p20 && r <= p60)
        newRemaining = static_cast<uint32_t>(p20);
    else if (r > p60)
        newRemaining = static_cast<uint32_t>(r - 2.0f * p20);

    if (newRemaining != remaining) {
        target.endMs = now + newRemaining;
        target.dirty = true;
    }
}

// ---- Ranged wind-up compensation ------------------------------------------

using GetCastTime_t = int(__fastcall *)(int spellID, int unit, int flag);

// Auto Shot / Shoot are re-cast by the server as TRIGGERED spells, so the
// per-shot SMSG_SPELL_START is skipped and SMSG_SPELL_GO is the only packet
// — arriving at the END of the ~0.5s wind-up, well after the server actually
// reset the timer. Subtract the engine's own cast-time helper (the same one
// `Spell::Cast` uses for the cast bar) so the ranged bar still ends on time
// instead of running ~0.5s long.
void HandleAutorepeatReset(uint32_t spellId) {
    const uint32_t rangedTime = AttackTimeMs(RANGED);
    if (rangedTime == 0)
        return;
    auto getCastTime = reinterpret_cast<GetCastTime_t>(Offsets::FUN_GET_CAST_TIME);
    int castTime = getCastTime(static_cast<int>(spellId), 0, 0);
    if (castTime < 0)
        castTime = 0;
    const auto windUp = static_cast<uint32_t>(castTime);
    MarkReset(RANGED, windUp >= rangedTime ? 0 : rangedTime - windUp);
}

// ---- SMSG_SPELL_GO dispatch -----------------------------------------------

bool ForcedSwingReset(uint32_t spellId) {
    for (auto *r = g_overrides; r != nullptr; r = r->next)
        if (r->fn(spellId))
            return true;
    return false;
}

void OnSpellGo(uint32_t spellId) {
    if (spellId == 0)
        return;
    const uint8_t *rec = DBC::Record(Offsets::VAR_SPELL_RECORDS,
                                     Offsets::VAR_SPELL_RECORD_COUNT, spellId);
    if (rec == nullptr)
        return;

    const uint32_t attr =
        *reinterpret_cast<const uint32_t *>(rec + Offsets::OFF_SPELL_RECORD_ATTRIBUTES);
    if (attr & Offsets::SPELL_ATTR_ON_NEXT_SWING) {
        // Replaces the next white hit -- not also evaluated as a cast-reset
        // spell below (an on-next-swing ability's InterruptFlags is unrelated).
        MarkReset(MAIN_HAND, AttackTimeMs(MAIN_HAND));
        return;
    }

    const uint32_t attrEx2 = *reinterpret_cast<const uint32_t *>(
        rec + Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX2);
    if (attrEx2 & Offsets::SPELL_ATTR_EX2_AUTOREPEAT_FLAG) {
        HandleAutorepeatReset(spellId);
        return;
    }

    const uint32_t interrupt = *reinterpret_cast<const uint32_t *>(
        rec + Offsets::OFF_SPELL_RECORD_INTERRUPT_FLAGS);
    if ((interrupt & Offsets::SPELL_INTERRUPT_FLAG_AUTOATTACK) &&
        (ForcedSwingReset(spellId) ||
         !(attrEx2 & Offsets::SPELL_ATTR_EX2_NOT_RESET_AUTO_ACTIONS)) &&
        WasNonTriggeredCast(spellId)) {
        MarkReset(MAIN_HAND, AttackTimeMs(MAIN_HAND));
        MarkReset(OFF_HAND, AttackTimeMs(OFF_HAND));
    }
}

// ---- Incoming packets -------------------------------------------------

void OnPacket(uint32_t opcode, CDataStore *packet) {
    if (packet == nullptr)
        return;
    const uint64_t player = Unit::Identity::PlayerGuid();
    if (player == 0)
        return;

    if (opcode == Offsets::SMSG_ATTACKERSTATEUPDATE) {
        const uint32_t hitInfo = Net::Read<uint32_t>(packet);
        const uint64_t attacker = Net::ReadPackedGuid(packet);
        const uint64_t victim = Net::ReadPackedGuid(packet);
        Net::Read<uint32_t>(packet); // totalDamage -- unused here
        const uint8_t subCount = Net::Read<uint8_t>(packet);
        for (uint8_t i = 0; i < subCount; ++i) {
            Net::Read<uint32_t>(packet); // school
            Net::Read<float>(packet);    // coefficient
            Net::Read<uint32_t>(packet); // damage
            Net::Read<uint32_t>(packet); // absorb
            Net::Read<int32_t>(packet);  // resist
        }
        const uint32_t targetState = Net::Read<uint32_t>(packet);

        if (attacker == player) {
            const SwingType hand =
                (hitInfo & Offsets::HITINFO_LEFTSWING) ? OFF_HAND : MAIN_HAND;
            MarkReset(hand, AttackTimeMs(hand));
        } else if (victim == player && targetState == Offsets::VICTIMSTATE_PARRY) {
            ApplyParryHaste();
        }
        return;
    }

    if (opcode == Offsets::SMSG_ATTACKSTART) {
        const uint64_t attacker = Net::Read<uint64_t>(packet);
        const uint64_t victim = Net::Read<uint64_t>(packet);
        if (attacker == player) {
            // Unit::Attack() resets the off-hand timer the INSTANT an
            // attack is declared, even out of range -- the server just
            // silently re-arms it every 100ms (DelayAutoAttacks(), no
            // packet) until the target is actually reachable. Firing here
            // unconditionally would promise "swing lands in Xs" while
            // stuck out of range, which never comes true until the real
            // first swing's SMSG_ATTACKERSTATEUPDATE resets it for real.
            // Gate on actually being in melee range so this only fires
            // when the promised time is meaningful.
            auto *victimObj = Object::ByGuid(
                Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_PLAYER, victim);
            auto *playerObj = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
            if (InMeleeRange(playerObj, victimObj))
                MarkReset(OFF_HAND, AttackTimeMs(OFF_HAND));
        }
        return;
    }

    if (opcode == Offsets::SMSG_SPELL_GO) {
        Net::ReadPackedGuid(packet); // itemGuid -- unused here
        const uint64_t caster = Net::ReadPackedGuid(packet);
        const uint32_t spellId = Net::Read<uint32_t>(packet);
        if (caster == player)
            OnSpellGo(spellId);
        return;
    }
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

// ---- Weapon swap mid-combat (polled -- no packet, no observer needed) ---
//
// A weapon equipped while `IsInCombat()` resets that hand's timer
// server-side (`Player::_ApplyWeaponDependentAuraMods` -> `SetAttackTime`
// with `resetTimer = IsInCombat()`). There's no dedicated packet for this;
// `Player::Equipment` already tracks the same invMgr GUIDs via a real
// descriptor observer for `PLAYER_EQUIPMENT_CHANGED`, but that's a one-way
// Lua event fire with nothing to subscribe to in C++. Only 3 slots matter
// here, so a plain 3-GUID compare on the existing WorldTick pass is simpler
// than standing up a second observer registration for the same fields.

uint64_t g_lastMainGuid = 0;
uint64_t g_lastOffGuid = 0;
uint64_t g_lastRangedGuid = 0;
bool g_weaponSnapValid = false;

void PollWeaponSwap() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr) {
        g_weaponSnapValid = false;
        return;
    }
    const uint8_t *invMgr = player + Offsets::OFF_PLAYER_INVENTORY_MANAGER;
    const uint32_t count =
        *reinterpret_cast<const uint32_t *>(invMgr + Offsets::OFF_INVMGR_SLOT_COUNT);
    if (count < 18) {
        g_weaponSnapValid = false;
        return;
    }
    const uint64_t *guids = *reinterpret_cast<const uint64_t *const *>(
        invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guids == nullptr) {
        g_weaponSnapValid = false;
        return;
    }

    // 0-based invMgr slots: 15 = main hand (Lua 16), 16 = off hand (17),
    // 17 = ranged (18) -- same `slot0 = luaSlot - 1` convention documented
    // in Player::Equipment.
    const uint64_t mainGuid = guids[15];
    const uint64_t offGuid = guids[16];
    const uint64_t rangedGuid = guids[17];

    if (g_weaponSnapValid && PlayerInCombat()) {
        if (mainGuid != g_lastMainGuid)
            MarkReset(MAIN_HAND, AttackTimeMs(MAIN_HAND));
        if (offGuid != g_lastOffGuid)
            MarkReset(OFF_HAND, AttackTimeMs(OFF_HAND));
        if (rangedGuid != g_lastRangedGuid)
            MarkReset(RANGED, AttackTimeMs(RANGED));
    }

    g_lastMainGuid = mainGuid;
    g_lastOffGuid = offGuid;
    g_lastRangedGuid = rangedGuid;
    g_weaponSnapValid = true;
}

// ---- PLAYER_SWING fire (WorldTick, at most once per type per frame) -----

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("swingDuration", "number",
                   "Seconds from now until this swing type's next attack."),
    Game::Doc::Req("swingType", "PlayerSwingType", "Which attack this reset applies to."),
};
const Game::Doc::Event kPlayerSwingDoc{
    "SwingTimer",
    "Fires whenever the player's melee or ranged auto-attack timer resets, "
    "with the seconds remaining until the next swing.",
    kPayload};
const Event::Custom::AutoReserve _evt{"PLAYER_SWING", &kPlayerSwingDoc};

void OnWorldTick() {
    PollWeaponSwap();

    const int slot = _evt.Slot();
    const uint32_t now = NowMs();
    for (int t = 0; t < 3; ++t) {
        State &s = g_swing[t];
        if (!s.dirty)
            continue;
        s.dirty = false;
        if (slot < 0)
            continue;
        const uint32_t remaining = Time::Clock::Remaining(now, s.endMs);
        if (remaining == 0)
            continue; // elapsed before we got to fire it -- nothing to show
        Event::Custom::Fire(slot, "%f%d", static_cast<double>(remaining) * 0.001, t);
    }
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// ---- Enum.PlayerSwingType --------------------------------------------------

const Game::Lua::EnumIntegerEntry kSwingTypeEntries[] = {
    {"MainHand", MAIN_HAND},
    {"OffHand", OFF_HAND},
    {"Ranged", RANGED},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum(
        "Enum", "PlayerSwingType", kSwingTypeEntries,
        sizeof(kSwingTypeEntries) / sizeof(kSwingTypeEntries[0]), "SwingTimer");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Combat::Swing
