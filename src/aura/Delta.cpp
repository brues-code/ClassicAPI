// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// UNIT_AURA_DELTA(unitTarget, updateInfo) — retail's UNIT_AURA payload under
// its own name, so the engine's UNIT_AURA stays exactly as it is.
//
// Once per frame, from the world tick, each unit whose auras changed fires
// once per unit token it has (`Unit::Identity::TokensForGUID`), the way the
// engine fires UNIT_AURA. The change set is `Aura::Instance`'s.
//
// The payload is built to cost no garbage. It carries instance IDs only —
// never AuraData; a handler reads the auras it cares about with
// `C_UnitAuras.GetAuraDataByAuraInstanceID(unit, id, buf)` — and the
// `updateInfo` table and its three ID lists are the same four tables on every
// fire, refilled each time. So they are valid only during the handler; a
// handler that keeps one must copy it.
//
// With nothing registered for the event, the tick reads one pointer and stops:
// `Aura::Instance` records no change set, and nothing here runs.

#include "Game.h"
#include "aura/Instance.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Aura::Delta {

namespace {

const Game::Doc::Field kInfoFields[] = {
    Game::Doc::Req("isFullUpdate", "bool",
                   "True when the unit's auras must be read again in full; the lists are then nil."),
    Game::Doc::Opt("addedAuraInstanceIDs", "table", nullptr,
                   "Instance IDs of the auras added to the unit."),
    Game::Doc::Opt("updatedAuraInstanceIDs", "table", nullptr,
                   "Instance IDs of the unit's auras that changed in place."),
    Game::Doc::Opt("removedAuraInstanceIDs", "table", nullptr,
                   "Instance IDs of the auras removed from the unit."),
};
const Game::Doc::Structure kInfo{
    "UnitAuraDeltaInfo", "UnitAuras", kInfoFields,
    "What changed in a unit's auras. Valid only while the event handler runs."};

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
    Game::Doc::Req("updateInfo", "UnitAuraDeltaInfo"),
};
const Game::Doc::Event kDoc{
    "UnitAuras",
    "Fires when a unit's auras change, with the instance IDs of the auras that "
    "were added, changed, or removed.",
    kPayload};
const Event::Custom::AutoReserve _evt{"UNIT_AURA_DELTA", &kDoc};

// The reused payload tables, anchored in the registry of the Lua state that
// made them. LUA_NOREF until first use in each state.
constexpr int kNoRef = -2;
int g_infoRef = kNoRef;
int g_addedRef = kNoRef;
int g_updatedRef = kNoRef;
int g_removedRef = kNoRef;

// Pushes the table anchored at `ref`, making it on first use.
void PushTable(void *L, int &ref) {
    if (ref == kNoRef) {
        Game::Lua::NewTable(L);
        ref = Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
    }
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, ref);
}

// Sets `key` on the table at the top to the list at `ref` refilled with `ids`,
// or to nil when there are none.
void SetList(void *L, const char *key, int &ref, const uint32_t *ids, int count) {
    Game::Lua::PushString(L, key);
    if (count == 0) {
        Game::Lua::PushNil(L);
        Game::Lua::SetTable(L, -3);
        return;
    }
    PushTable(L, ref);
    for (int i = 0; i < count; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::PushNumber(L, static_cast<double>(ids[i]));
        Game::Lua::RawSet(L, -3);
    }
    // Clear what lies past the new end: a longer earlier list, or anything a
    // handler appended.
    for (int i = count + 1;; ++i) {
        Game::Lua::RawGetI(L, -1, i);
        const bool end = Game::Lua::Type(L, -1) == Game::Lua::TYPE_NIL;
        Game::Lua::SetTop(L, -2);
        if (end)
            break;
        Game::Lua::PushNumber(L, static_cast<double>(i));
        Game::Lua::PushNil(L);
        Game::Lua::RawSet(L, -3);
    }
    Game::Lua::SetN(L, -1, count);
    Game::Lua::SetTable(L, -3);
}

void Fire(const Aura::Instance::Delta &d) {
    char tokens[16][32];
    const int n = Unit::Identity::TokensForGUID(d.guid, tokens, 16);
    void *L = Game::Lua::State();
    if (n == 0 || L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    for (int i = 0; i < n; ++i) {
        // Refilled for each token: a handler may have changed the tables.
        Game::Lua::PushString(L, tokens[i]);
        PushTable(L, g_infoRef);
        Game::Lua::SetFieldBool(L, "isFullUpdate", d.fullUpdate);
        SetList(L, "addedAuraInstanceIDs", g_addedRef, d.added, d.addedCount);
        SetList(L, "updatedAuraInstanceIDs", g_updatedRef, d.updated, d.updatedCount);
        SetList(L, "removedAuraInstanceIDs", g_removedRef, d.removed, d.removedCount);
        Event::Custom::FireValues(_evt.Slot(), L, top + 1, 2);
        Game::Lua::SetTop(L, top);
    }
}

void OnWorldTick() {
    const bool listening = Event::Custom::HasListeners(_evt.Slot());
    Aura::Instance::SetRecording(listening);
    if (listening)
        Aura::Instance::Drain(&Fire);
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

// Each registration pass is a new Lua state; the old state's refs are gone.
void ResetRefs() {
    g_infoRef = g_addedRef = g_updatedRef = g_removedRef = kNoRef;
}

const Game::ModuleAutoRegister _autoreg{&ResetRefs};

} // namespace

} // namespace Aura::Delta
