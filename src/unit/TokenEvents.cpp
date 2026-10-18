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

// Every unit event the engine fires reaches ClassicAPI's unit tokens —
// `focus`, `nameplateN`, `markN` — as it reaches `target`.
//
// This is how 4.3.4 makes its focus a full unit token (decomp: GameUI.cpp
// SetFocusTarget, ScriptEvents.cpp token maps), in two steps: the unit gets a
// script-registration bit, so the engine watches its fields, and the
// GUID→token maps return the token, so whatever the engine fires for that unit
// fires for the token too. 1.12 has both pieces, less these tokens:
//
// Watching. The engine rebuilds a unit's registration mask from scratch in
// FUN_UNIT_SCRIPT_RECOMPUTE on every target / group / pet change and at unit
// post-init. A co-hook keeps UNIT_SCRIPT_REG_TOKENS (one bit for all three
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
// All of a token's unit events come from here, none from Unit::TokenObserver:
// SuperWoW sets a registration bit on every unit, so every unit's field
// changes already reach the broadcast, and an observer of our own would fire
// them a second time. Only `focustarget` (no unit has it in any token map)
// stays on TokenObserver.

#include "TokenEvents.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"
#include "nameplate/Walk.h"
#include "object/Resolve.h"
#include "unit/Focus.h"
#include "unit/Identity.h"
#include "unit/RaidTarget.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Unit::TokenEvents {

namespace {

using Fire_t = void(__cdecl *)(int eventId, const char *fmt, ...);
using UnitFn_t = void(__fastcall *)(void *unit);
using ScriptBits_t = void(__thiscall *)(void *unit, uint32_t bits);
using Broadcast_t = void(__fastcall *)(const uint64_t *guid, uint32_t eventId);
using CombatFeedback_t = void(__fastcall *)(const uint64_t *guid, const char *action, int amount,
                                            int school, uint32_t hitInfo);

// At most one focus, one nameplate and one mark per unit.
constexpr int kMaxTokens = 3;
using TokenList = char[kMaxTokens][24];

// The ClassicAPI tokens `guid` has right now, in UnitTokenFromGUID's order.
int TokensOf(uint64_t guid, TokenList &out) {
    if (guid == 0)
        return 0;
    int n = 0;
    for (int i = 1, count = NamePlate::Events::GetSlotCount(); i <= count; ++i)
        if (NamePlate::Events::GetGUIDByIndex(i) == guid) {
            std::snprintf(out[n++], sizeof out[0], "nameplate%d", i);
            break;
        }
    if (guid == Unit::Focus::Get())
        std::snprintf(out[n++], sizeof out[0], "focus");
    for (int m = 1; m <= Unit::RaidTarget::kMarkCount; ++m)
        if (Unit::RaidTarget::GetGUIDByMark(m) == guid) {
            std::snprintf(out[n++], sizeof out[0], "mark%d", m);
            break;
        }
    return n;
}

bool HasTokens(uint64_t guid) {
    TokenList t;
    return TokensOf(guid, t) > 0;
}

// The tokens to fire for, empty before the world is up (the token map
// answers nothing before the player exists; neither does this).
int FireTokensOf(uint64_t guid, TokenList &out) {
    if (guid == 0 || Unit::Identity::PlayerObject() == nullptr)
        return 0;
    return TokensOf(guid, out);
}

uint64_t UnitGuid(const void *unit) {
    const auto *block = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_GUID_PTR);
    return (block != nullptr) ? Game::Read<uint64_t>(block, Offsets::OFF_INSTANCE_BLOCK_GUID) : 0;
}

// --- watching: the registration bit -----------------------------------------

UnitFn_t g_recomputeOriginal = nullptr;

void __fastcall Recompute_h(void *unit) {
    g_recomputeOriginal(unit);
    if (unit == nullptr)
        return;
    const uint32_t mask = Game::Read<uint32_t>(unit, Offsets::OFF_CGUNIT_SCRIPT_REGISTRATION);
    const bool has = (mask & Offsets::UNIT_SCRIPT_REG_TOKENS) != 0;
    const bool want = HasTokens(UnitGuid(unit));
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
    const int n = FireTokensOf(g, tokens);
    if (n == 0)
        return;
    const char *modifier = CombatModifier(amount, hitInfo);
    const auto fire = reinterpret_cast<Fire_t>(static_cast<uintptr_t>(Offsets::FUN_FIRE_EVENT));
    const auto *fmt = reinterpret_cast<const char *>(Offsets::VAR_STR_UNIT_COMBAT_FORMAT);
    for (int i = 0; i < n; ++i)
        fire(Offsets::UNIT_EVENT_UNIT_COMBAT, fmt, tokens[i], action, modifier, amount, school);
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
                           "focus/nameplate/mark tokens get no engine unit events",
                           static_cast<unsigned>(broadcast.size()),
                           static_cast<unsigned>(combat.size()));
        return;
    }
    Redirect(broadcast, reinterpret_cast<const void *>(&Broadcast));
    Redirect(combat, reinterpret_cast<const void *>(&CombatFeedbackSite));
}

const Game::ModuleAutoRegister _autoreg{&Apply};

} // namespace

void __fastcall Broadcast(const uint64_t *guid, uint32_t eventId) {
    const uint64_t g = (guid != nullptr) ? *guid : 0;
    reinterpret_cast<Broadcast_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_EVENT_BROADCAST))(
        guid, eventId);
    TokenList tokens;
    const int n = FireTokensOf(g, tokens);
    const auto fire = reinterpret_cast<Fire_t>(static_cast<uintptr_t>(Offsets::FUN_FIRE_EVENT));
    for (int i = 0; i < n; ++i)
        fire(static_cast<int>(eventId), "%s", tokens[i]);
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
