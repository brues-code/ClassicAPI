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

// `UNIT_IN_RANGE_UPDATE` — the first return of UnitInRange changed for a
// unit.
//
// Nothing in the client reacts to a distance: the player moves every frame
// while it moves, and the client moves each other unit in view a little
// every frame between its movement packets. So the units are checked every
// world tick, and only while a frame is registered for the event, as retail
// does (its CompactUnitFrame registers the event only while shown, "since
// C++ does extra work to send these events while any frame is registered
// for them").
//
// The units are the ones the registered frames named with RegisterUnitEvent.
// A plain RegisterEvent adds the player's pet and each party and raid member
// and their pets. A token fires when Unit::Range::InRange, UnitInRange's own
// check, changes for it; its first check fires nothing. A filter unit that
// is not a unit token is dropped once (Unit::TokenResolve::IsUnitToken), as
// the engine's resolver raises a Lua error for it.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "frame/UnitEvent.h"
#include "tick/WorldTick.h"
#include "unit/Range.h"
#include "unit/TokenResolve.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Unit::InRangeEvent {

namespace {

constexpr const char *kName = "UNIT_IN_RANGE_UPDATE";

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
    Game::Doc::Req("isInRange", "bool"),
};
const Game::Doc::Event kDoc{
    "Unit", "Fires when the unit moves into or out of the range that UnitInRange checks.",
    kPayload};
const Event::Custom::AutoReserve kEvent{kName, &kDoc};

// One token's state, keyed by the lower-case token.
struct Watch {
    uint32_t pass = 0;   // the last pass that wanted the token
    bool seeded = false; // checked once
    bool valid = false;  // a unit token
    bool inRange = false;
};

std::unordered_map<std::string, Watch> g_watch;
// This pass's tokens, in the order they were wanted. Map nodes keep their
// address while the map grows.
std::vector<std::pair<const std::string *, Watch *>> g_wanted;
uint32_t g_pass = 0;

void Want(const char *token) {
    char key[32];
    size_t n = 0;
    for (; token[n] != '\0'; ++n) {
        if (n == sizeof key - 1)
            return; // no unit token is this long
        key[n] = static_cast<char>(std::tolower(static_cast<unsigned char>(token[n])));
    }
    key[n] = '\0';
    auto it = g_watch.try_emplace(key).first;
    if (it->second.pass == g_pass)
        return;
    it->second.pass = g_pass;
    g_wanted.emplace_back(&it->first, &it->second);
}

void WantGroup() {
    char token[16];
    Want("pet");
    const auto *party =
        reinterpret_cast<const uint64_t *>(static_cast<uintptr_t>(Offsets::VAR_PARTY_GUIDS));
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i) {
        if (party[i] == 0)
            continue;
        std::snprintf(token, sizeof token, "party%d", i + 1);
        Want(token);
        std::snprintf(token, sizeof token, "partypet%d", i + 1);
        Want(token);
    }
    int count = Game::Read<int>(Offsets::VAR_RAID_MEMBER_COUNT);
    if (count > Offsets::RAID_MAX_SLOTS)
        count = Offsets::RAID_MAX_SLOTS;
    for (int i = 1; i <= count; ++i) {
        std::snprintf(token, sizeof token, "raid%d", i);
        Want(token);
        std::snprintf(token, sizeof token, "raidpet%d", i);
        Want(token);
    }
}

void OnWorldTick() {
    const int slot = kEvent.Slot();
    if (!Event::Custom::HasListeners(slot)) {
        if (!g_watch.empty())
            g_watch.clear();
        return;
    }
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;

    // The tokens first: firing runs scripts, which can change the chain.
    ++g_pass;
    g_wanted.clear();
    bool everyGroupUnit = false;
    Event::Custom::ForEachListener(slot, [&](uint8_t *frame) {
        const std::vector<std::string> *units = Frame::UnitEvent::Units(frame, kName);
        if (units == nullptr) {
            everyGroupUnit = true;
            return;
        }
        for (const std::string &unit : *units)
            Want(unit.c_str());
    });
    if (everyGroupUnit)
        WantGroup();
    for (auto it = g_watch.begin(); it != g_watch.end();)
        it = it->second.pass == g_pass ? std::next(it) : g_watch.erase(it);

    const int top = Game::Lua::GetTop(L);
    for (auto &[token, watch] : g_wanted) {
        if (!watch->seeded) {
            watch->seeded = true;
            watch->valid = Unit::TokenResolve::IsUnitToken(token->c_str());
            if (watch->valid)
                Unit::Range::InRange(token->c_str(), &watch->inRange);
            continue;
        }
        if (!watch->valid)
            continue;
        bool inRange = false;
        Unit::Range::InRange(token->c_str(), &inRange);
        if (inRange == watch->inRange)
            continue;
        watch->inRange = inRange;
        Game::Lua::PushString(L, token->c_str());
        Game::Lua::PushBool(L, inRange);
        Event::Custom::FireValues(slot, L, top + 1, 2);
        Game::Lua::SetTop(L, top);
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace Unit::InRangeEvent
