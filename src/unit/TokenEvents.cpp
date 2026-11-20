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

// Every unit event the engine fires reaches the tokens 1.12's token map
// lacks — `nameplateN`, `targettarget`, `focus`, `focustarget`,
// `softenemy`, `softfriend`, `softinteract`, and ClassicAPI's `markN` and
// `totemN` — as it reaches `target`. The 3.80.2 client fires every unit event
// for all of these but the last two (its token builder 0x141B90E10).
//
// This is how 4.3.4 makes its focus a full unit token (decomp: GameUI.cpp
// SetFocusTarget, ScriptEvents.cpp token maps), in two steps: the unit gets a
// script-registration bit, so the engine watches its fields, and the
// GUID→token maps return the token, so whatever the engine fires for that unit
// fires for the token too. 1.12 has both pieces, less these tokens:
//
// Watching. The engine rebuilds a unit's registration mask from scratch in
// FUN_UNIT_SCRIPT_RECOMPUTE on every target / group / pet change and at unit
// post-init. A co-hook keeps UNIT_SCRIPT_REG_TOKENS (one bit for all the
// tokens) on any unit that has one, through the engine's own
// FUN_UNIT_SCRIPT_REGISTER / _UNREGISTER, which start and stop the field
// watch on 0 ↔ nonzero. Post-init covers a unit coming back into range — a
// case 4.3.4's own recompute forgets for its focus. A token's owner calls
// Refresh when it gives or takes the token.
//
// Tokens. 1.12 fires every unit event through one of the two callers of the
// token map: the broadcast FUN_UNIT_EVENT_BROADCAST (field changes, names,
// portraits, classification, group stats, auras, …) and the combat feedback
// FUN_UNIT_COMBAT_FEEDBACK (UNIT_COMBAT). nampower replaces both functions
// with detours that never call the original, so neither can be co-hooked —
// the last DLL to patch wins. Their CALL / JMP sites, which nobody patches,
// point at the functions below instead: each calls the original address
// (running nampower's detour or the engine, whichever is there), then fires
// the event once more for each ClassicAPI token of the unit. The sites are
// found by scanning `.text` for direct rel32 calls to the two addresses and
// patched only when the counts match Ghidra's xref lists, so a different
// binary is left alone. The token map itself is SuperWoW's patch site.
//
// All of a token's engine unit events come from here, none from a field
// observer of our own: SuperWoW sets a registration bit on every unit, so
// every unit's field changes already reach the broadcast, and an observer
// would fire them a second time. ClassicAPI's own unit events reach
// the same tokens: Unit::Identity::TokensForGUID merges TokensOf's list with
// the engine's into 3.80.2's order.

#include "TokenEvents.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"
#include "event/Custom.h"
#include "nameplate/Walk.h"
#include "object/Resolve.h"
#include "target/Reason.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "tick/WorldTick.h"
#include "totem/Tracker.h"
#include "unit/Focus.h"
#include "unit/Identity.h"
#include "unit/RaidTarget.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Unit::TokenEvents {

namespace {

using UnitFn_t = void(__fastcall *)(void *unit);
using ScriptBits_t = void(__thiscall *)(void *unit, uint32_t bits);
using Broadcast_t = void(__fastcall *)(const uint64_t *guid, uint32_t eventId);
using CombatFeedback_t = void(__fastcall *)(const uint64_t *guid, const char *action, int amount,
                                            int school, uint32_t hitInfo);

// At most one nameplate, targettarget, focus, focustarget, softenemy,
// softfriend, softinteract, mark and totem per unit.
constexpr int kMaxTokens = 9;
using TokenList = char[kMaxTokens][32];

// The unit `guid`'s target, or 0 when the unit is not in view.
uint64_t TargetOf(uint64_t guid) {
    const auto *fields = Object::Fields(Object::ByGuid(Offsets::TYPEMASK_UNIT, guid));
    return fields != nullptr ? Game::Read<uint64_t>(fields, Offsets::OFF_UNIT_FIELD_TARGET) : 0;
}

// The 3.80.2 token builder's test for a targettarget / focustarget
// candidate (0x141B9136B): a unit in view, and selectable unless it is a
// player.
uint64_t Eligible(uint64_t guid) {
    if (guid == 0)
        return 0;
    const auto *fields = Object::Fields(Object::ByGuid(Offsets::TYPEMASK_UNIT, guid));
    if (fields == nullptr)
        return 0;
    if ((Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
         Offsets::UNIT_FLAG_NOT_SELECTABLE) != 0 &&
        Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid) == nullptr)
        return 0;
    return guid;
}

// The target the `target` token names: none for a hidden Force target.
uint64_t TargetGuid() {
    return Target::Reason::Hidden() ? 0 : Target::Reason::Selection();
}

// `targettarget`: your target's target, or you when you target yourself.
uint64_t TargetTarget() {
    const uint64_t target = TargetGuid();
    if (target == 0)
        return 0;
    return Eligible(target == Unit::Identity::PlayerGuid() ? target : TargetOf(target));
}

// `focustarget`: your focus's target, or your target when you are the focus.
uint64_t FocusTarget() {
    const uint64_t focus = Unit::Focus::Get();
    if (focus == 0)
        return 0;
    return Eligible(focus == Unit::Identity::PlayerGuid() ? TargetGuid() : TargetOf(focus));
}

// The ClassicAPI tokens `guid` has right now, in the 3.80.2 client's unit
// event order (0x141B90E10), then the ClassicAPI extensions, at most `max`.
int ListTokens(uint64_t guid, char (*out)[32], int max) {
    if (guid == 0)
        return 0;
    int n = 0;
    for (int i = 1, count = NamePlate::Events::GetSlotCount(); n < max && i <= count; ++i)
        if (NamePlate::Events::GetGUIDByIndex(i) == guid) {
            std::snprintf(out[n++], sizeof out[0], "nameplate%d", i);
            break;
        }
    if (n < max && guid == TargetTarget())
        std::snprintf(out[n++], sizeof out[0], "targettarget");
    if (n < max && guid == Unit::Focus::Get())
        std::snprintf(out[n++], sizeof out[0], "focus");
    if (n < max && guid == FocusTarget())
        std::snprintf(out[n++], sizeof out[0], "focustarget");
    if (n < max && guid == Target::SoftUnit::Current(Target::SoftUnit::Side::Enemy))
        std::snprintf(out[n++], sizeof out[0], "softenemy");
    if (n < max && guid == Target::SoftUnit::Current(Target::SoftUnit::Side::Friend))
        std::snprintf(out[n++], sizeof out[0], "softfriend");
    if (n < max && guid == Target::SoftInteract::Get())
        std::snprintf(out[n++], sizeof out[0], "softinteract");
    if (const int m = Unit::RaidTarget::MarkOf(guid); n < max && m != 0)
        std::snprintf(out[n++], sizeof out[0], "mark%d", m);
    for (int s = 1; n < max && s <= Totem::Tracker::kSlotCount; ++s)
        if (Totem::Tracker::GetGUIDBySlot(s) == guid) {
            std::snprintf(out[n++], sizeof out[0], "totem%d", s);
            break;
        }
    return n;
}

// targettarget and focustarget change with no event (UNIT_FIELD_TARGET has
// no event-table slot), so the world tick re-reads them and moves the
// registration bit when either changes.
uint64_t g_targetTarget = 0;
uint64_t g_focusTarget = 0;

void Follow(uint64_t &last, uint64_t now) {
    if (now == last)
        return;
    const uint64_t old = last;
    last = now;
    Refresh(old);
    Refresh(now);
}

void OnWorldTick() {
    if (Unit::Identity::PlayerObject() == nullptr)
        return;
    Follow(g_targetTarget, TargetTarget());
    Follow(g_focusTarget, FocusTarget());
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

bool HasTokens(uint64_t guid) {
    TokenList t;
    return ListTokens(guid, t, kMaxTokens) > 0;
}


// --- watching: the registration bit -----------------------------------------

UnitFn_t g_recomputeOriginal = nullptr;

void __fastcall Recompute_h(void *unit) {
    g_recomputeOriginal(unit);
    if (unit == nullptr)
        return;
    const uint32_t mask = Game::Read<uint32_t>(unit, Offsets::OFF_CGUNIT_SCRIPT_REGISTRATION);
    const bool has = (mask & Offsets::UNIT_SCRIPT_REG_TOKENS) != 0;
    const bool want = HasTokens(::Object::Guid(unit));
    if (want && !has)
        reinterpret_cast<ScriptBits_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_SCRIPT_REGISTER))(
            unit, Offsets::UNIT_SCRIPT_REG_TOKENS);
    else if (!want && has)
        reinterpret_cast<ScriptBits_t>(
            static_cast<uintptr_t>(Offsets::FUN_UNIT_SCRIPT_UNREGISTER))(
            unit, Offsets::UNIT_SCRIPT_REG_TOKENS);
}

const Game::HookAutoRegister _recomputeHook{Offsets::FUN_UNIT_SCRIPT_RECOMPUTE,
                                            reinterpret_cast<void *>(&Recompute_h),
                                            reinterpret_cast<void **>(&g_recomputeOriginal)};

// --- tokens: the redirected call sites ---------------------------------------

// FUN_UNIT_COMBAT_FEEDBACK's modifier pick, exactly as the engine makes it.
const char *CombatModifier(int amount, uint32_t hitInfo) {
    uintptr_t s = Offsets::VAR_STR_EMPTY;
    if (amount > 0) {
        if (hitInfo & 0x80u)
            s = Offsets::VAR_STR_COMBAT_CRITICAL;
        else if (hitInfo & 0x4000u)
            s = Offsets::VAR_STR_COMBAT_GLANCING;
        else if (hitInfo & 0x8000u)
            s = Offsets::VAR_STR_COMBAT_CRUSHING;
    } else if (hitInfo & 0x20u) {
        s = Offsets::VAR_STR_COMBAT_ABSORB;
    } else if (hitInfo & 0x800u) {
        s = Offsets::VAR_STR_COMBAT_BLOCK;
    } else if (hitInfo & 0x40u) {
        s = Offsets::VAR_STR_COMBAT_RESIST;
    }
    return reinterpret_cast<const char *>(s);
}

void __fastcall CombatFeedbackSite(const uint64_t *guid, const char *action, int amount,
                                   int school, uint32_t hitInfo) {
    const uint64_t g = (guid != nullptr) ? *guid : 0;
    reinterpret_cast<CombatFeedback_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_COMBAT_FEEDBACK))(
        guid, action, amount, school, hitInfo);
    TokenList tokens;
    const int n = TokensOf(g, tokens, kMaxTokens);
    if (n == 0)
        return;
    const char *modifier = CombatModifier(amount, hitInfo);
    const auto *fmt = reinterpret_cast<const char *>(Offsets::VAR_STR_UNIT_COMBAT_FORMAT);
    for (int i = 0; i < n; ++i)
        Event::Custom::Fire(Offsets::UNIT_EVENT_UNIT_COMBAT, fmt, tokens[i], action, modifier,
                            amount, school);
}

// Direct `E8` / `E9` rel32 instructions in `.text` that reach `target`.
std::vector<uint8_t *> FindCallSites(uintptr_t target) {
    std::vector<uint8_t *> sites;
    auto *p = reinterpret_cast<uint8_t *>(Offsets::SECTION_TEXT_START);
    auto *end = reinterpret_cast<uint8_t *>(Offsets::SECTION_TEXT_END) - 5;
    for (; p <= end; ++p) {
        if (*p != 0xE8 && *p != 0xE9)
            continue;
        int32_t rel;
        std::memcpy(&rel, p + 1, sizeof rel);
        if (reinterpret_cast<uintptr_t>(p) + 5 + rel == target)
            sites.push_back(p);
    }
    return sites;
}

void Redirect(const std::vector<uint8_t *> &sites, const void *to) {
    for (uint8_t *site : sites) {
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) -
                                                 (reinterpret_cast<uintptr_t>(site) + 5));
        Common::PatchBytes(site + 1, &rel, sizeof rel);
    }
}

// Once per process; every in-game registration runs it, and patched sites no
// longer reach the original address, so a second scan would find none.
bool g_redirected = false;

void Apply() {
    if (g_redirected)
        return;
    g_redirected = true;
    const auto broadcast = FindCallSites(Offsets::FUN_UNIT_EVENT_BROADCAST);
    const auto combat = FindCallSites(Offsets::FUN_UNIT_COMBAT_FEEDBACK);
    if (broadcast.size() != Offsets::UNIT_EVENT_BROADCAST_CALL_SITES ||
        combat.size() != Offsets::UNIT_COMBAT_FEEDBACK_CALL_SITES) {
        Debug::Log::Printf("TokenEvents: call sites differ (broadcast %u, combat %u); "
                           "ClassicAPI tokens get no engine unit events",
                           static_cast<unsigned>(broadcast.size()),
                           static_cast<unsigned>(combat.size()));
        return;
    }
    Redirect(broadcast, reinterpret_cast<const void *>(&Broadcast));
    Redirect(combat, reinterpret_cast<const void *>(&CombatFeedbackSite));
}

const Game::ModuleAutoRegister _autoreg{&Apply};

} // namespace

int TokensOf(uint64_t guid, char (*out)[32], int maxTokens) {
    // The token map answers nothing before the player exists; neither does
    // this.
    if (Unit::Identity::PlayerObject() == nullptr)
        return 0;
    return ListTokens(guid, out, maxTokens);
}

void __fastcall Broadcast(const uint64_t *guid, uint32_t eventId) {
    const uint64_t g = (guid != nullptr) ? *guid : 0;
    reinterpret_cast<Broadcast_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_EVENT_BROADCAST))(
        guid, eventId);
    TokenList tokens;
    const int n = TokensOf(g, tokens, kMaxTokens);
    for (int i = 0; i < n; ++i)
        Event::Custom::Fire(static_cast<int>(eventId), "%s", tokens[i]);
    for (const AutoObserve *o = AutoObserve::head; o != nullptr; o = o->next)
        o->fn(g, eventId);
}

void Refresh(uint64_t guid) {
    // The engine's watch start / stop walks the player-rooted observer
    // registry, NULL out of world — and nameplates drop during world teardown.
    if (guid == 0 || !Object::RegistryReady())
        return;
    if (void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, "TokenEvents", 0))
        reinterpret_cast<UnitFn_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_SCRIPT_RECOMPUTE))(
            unit);
}

} // namespace Unit::TokenEvents
