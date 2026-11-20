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

// Death recap — 3.80.2's, which the client builds from its own combat log; the
// server sends nothing for it. 4.3.4 and 3.3.5 have none.
//
// FUN_1414407d0 (the UNIT_DIED handler), when the unit that died is the local
// player: walk the kept log newest first over the entries whose target is the
// player, stopping at the player's previous death (UNIT_DIED, UNIT_DESTROYED,
// UNIT_DISSIPATES). Keep at most 5 entries of the damage events in the mask
// 0x85640000320B whose amount past the overkill is above 0, and every
// SPELL_INSTAKILL. An entry with overkill starts the list over: the hit that
// killed comes first. Each death takes the next recap ID (DAT_1468cb974);
// a death with entries is kept, with the player's maximum health, in the next
// of 15 slots (DAT_144c89570, 0x880 bytes each). The UNIT_DIED entry carries
// the ID, -1 when nothing was kept.
//
// The C_DeathRecap functions (0x140c04290, 043c0, 04650, 04790) find a recap
// by ID; with none given, the latest. GetRecapEvents returns nothing for an
// unknown ID. Leaving the world empties the slots and restarts the IDs
// (FUN_14143d830, with the combat log), which here is the glue pass that
// empties History.
//
// Ours differs in one case: when an older overkill restarts the list, 3.80.2
// overwrites its 5-entry buffer from the start and leaves the newer entries it
// had not overwritten yet; ours drops them.

#include "combatlog/DeathRecap.h"

#include "Game.h"
#include "Offsets.h"
#include "combatlog/Entry.h"
#include "combatlog/History.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdio>

namespace CombatLog::DeathRecap {

namespace {

constexpr int kRecaps = 15;
constexpr int kEvents = 5;

// The damage events a recap keeps, by Subevent bit: ENVIRONMENTAL_DAMAGE,
// SWING_DAMAGE, RANGE_DAMAGE, SPELL_DAMAGE, SPELL_DRAIN, SPELL_LEECH,
// SPELL_PERIODIC_DAMAGE, _DRAIN, _LEECH, DAMAGE_SHIELD, DAMAGE_SPLIT, and 47,
// which 1.12 never logs.
constexpr uint64_t kDamageEvents = 0x85640000320BULL;

// 3.80.2's UNIT_DISSIPATES, which 1.12 never logs (Deaths.cpp).
constexpr int UNIT_DISSIPATES = 49;

struct Recap {
    int id = 0; // 0: an empty slot (IDs start at 1)
    double maxHealth = 0;
    int count = 0;
    Entry events[kEvents];
};

Recap g_recaps[kRecaps];
int g_lastID = 0;
int g_slot = 0;

// The values FUN_1414407d0 reads from every entry it weighs: the amount at
// +0x18c and the overkill at +0x194. In a damage suffix those are its amount
// and overkill; in a drain's they are its power type and alternate power type,
// so a drain counts only when its power type is above 0.
int Amount(const Entry &e) { return (e.info & (INFO_DAMAGE | INFO_DRAIN)) ? e.v[1] : 0; }

int Overkill(const Entry &e) {
    if (e.info & INFO_DAMAGE)
        return e.v[2];
    return (e.info & INFO_DRAIN) ? e.v[3] : 0;
}

bool IsDamageEvent(int event) {
    return event >= 0 && event < 64 && ((kDamageEvents >> event) & 1) != 0;
}

const Recap *Find(int id) {
    if (id <= 0)
        return nullptr;
    for (const Recap &r : g_recaps) {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

// The recap an optional ID argument names (FUN_140ab34c0's read): none given,
// the latest; -1, none.
const Recap *ArgRecap(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Find(g_lastID);
    return Find(static_cast<int>(Game::Lua::ToNumber(L, 1)));
}

void Reset() {
    for (Recap &r : g_recaps)
        r = Recap{};
    g_lastID = 0;
    g_slot = 0;
}

const Game::GlueModuleAutoRegister _gluereg{&Reset};

// C_DeathRecap.GetRecapEvents([recapID]) -> events | nothing
int __fastcall Script_GetRecapEvents(void *L) {
    const Recap *r = ArgRecap(L);
    if (r == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    for (int i = 0; i < r->count; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        PushTable(L, r->events[i]);
        Game::Lua::SetFieldNumber(L, "currentHP", r->events[i].currentHP);
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// C_DeathRecap.GetRecapLink([recapID]) -> link (FUN_1414406f0)
int __fastcall Script_GetRecapLink(void *L) {
    const Recap *r = ArgRecap(L);
    if (r == nullptr) {
        Game::Lua::PushString(L, Game::Lua::GlobalTextOr("DEATH_RECAP_TEXT", ""));
        return 1;
    }
    char link[256];
    std::snprintf(link, sizeof link, Game::Lua::GlobalTextOr("DEATH_RECAP_LINK", ""), r->id);
    Game::Lua::PushString(L, link);
    return 1;
}

// C_DeathRecap.GetRecapMaxHealth([recapID]) -> maxHealth
int __fastcall Script_GetRecapMaxHealth(void *L) {
    const Recap *r = ArgRecap(L);
    Game::Lua::PushNumber(L, r != nullptr ? r->maxHealth : 0);
    return 1;
}

// C_DeathRecap.HasRecapEvents([recapID]) -> hasEvents
int __fastcall Script_HasRecapEvents(void *L) {
    Game::Lua::PushBool(L, ArgRecap(L) != nullptr);
    return 1;
}

const Game::Doc::Field kRecapArgs[] = {
    Game::Doc::Opt("recapID", "number", nullptr, "The latest recap when nil."),
};
const Game::Doc::Field kEventsRets[] = {
    Game::Doc::Opt("events", "table", nullptr,
                   "Up to 5 combat log events, newest first, each with currentHP. Nothing is "
                   "returned for an unknown ID."),
};
const Game::Doc::Function kGetRecapEvents{"Returns the hits that a death recap holds.",
                                          kRecapArgs, kEventsRets};
const Game::Doc::Field kLinkRets[] = {
    Game::Doc::Req("link", "string",
                   "A death link to the recap, or DEATH_RECAP_TEXT when no recap has the ID."),
};
const Game::Doc::Function kGetRecapLink{"Returns a chat link to a death recap.", kRecapArgs,
                                        kLinkRets};
const Game::Doc::Field kMaxHealthRets[] = {
    Game::Doc::Req("maxHealth", "number", "0 when no recap has the ID."),
};
const Game::Doc::Function kGetRecapMaxHealth{
    "Returns your maximum health when the death in a recap happened.", kRecapArgs,
    kMaxHealthRets};
const Game::Doc::Field kHasEventsRets[] = {
    Game::Doc::Req("hasEvents", "bool"),
};
const Game::Doc::Function kHasRecapEvents{"Returns whether a death recap with the ID exists.",
                                          kRecapArgs, kHasEventsRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_DeathRecap", "GetRecapEvents", &Script_GetRecapEvents,
                                     &kGetRecapEvents);
    Game::Lua::RegisterTableFunction("C_DeathRecap", "GetRecapLink", &Script_GetRecapLink,
                                     &kGetRecapLink);
    Game::Lua::RegisterTableFunction("C_DeathRecap", "GetRecapMaxHealth",
                                     &Script_GetRecapMaxHealth, &kGetRecapMaxHealth);
    Game::Lua::RegisterTableFunction("C_DeathRecap", "HasRecapEvents", &Script_HasRecapEvents,
                                     &kHasRecapEvents);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

int OnPlayerDied(uint64_t player) {
    Entry kept[kEvents];
    int count = 0;
    History::ForEachNewest([&](const Entry &e) {
        if (e.dest != player)
            return true;
        if (e.event == UNIT_DIED || e.event == UNIT_DESTROYED || e.event == UNIT_DISSIPATES)
            return false;
        bool keep = e.event == SPELL_INSTAKILL;
        if (IsDamageEvent(e.event)) {
            const int overkill = Overkill(e);
            const int net = overkill < 1 ? Amount(e) : Amount(e) - overkill;
            if (net >= 1) {
                if (overkill > 0)
                    count = 0;
                keep = true;
            }
        }
        if (keep)
            kept[count++] = e;
        return count < kEvents;
    });

    const int id = ++g_lastID;
    if (count == 0)
        return -1;
    g_slot = (g_slot + 1) % kRecaps;
    Recap &r = g_recaps[g_slot];
    r.id = id;
    const uint8_t *fields = Unit::Identity::PlayerDescriptor();
    r.maxHealth =
        fields != nullptr ? Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_MAXHEALTH) : 0;
    r.count = count;
    for (int i = 0; i < count; ++i)
        r.events[i] = kept[i];
    return id;
}

} // namespace CombatLog::DeathRecap
