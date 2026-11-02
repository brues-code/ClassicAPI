// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `C_ActionBar.EnableActionRangeCheck(slot, enable)` and
// `ACTION_RANGE_CHECK_UPDATE(slot, isInRange, checksRange)`.
//
// Nothing in the client reacts to a distance (see Unit::InRangeEvent), so the
// enabled slots are checked every world tick, while a frame is registered for
// the event, and a slot fires when its answer changes. The answer is
// IsActionInRange's (Action::Range::Answer), macro unit included, so a slot
// whose action changes fires when the new action's answer differs.
//
// Enabling is counted per slot, as retail's ActionBarButtonRangeCheckFrame
// expects: it enables once for each button on a slot and disables once as
// each button leaves. Retail's buttons never call IsActionInRange; they take
// their first state from this event. So each enable makes the slot's next
// check fire, changed or not. A new Lua state (`/reload`) starts with no slot
// enabled.

#include "Game.h"
#include "Offsets.h"
#include "action/Range.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"

#include <cstdint>

namespace Action::RangeCheck {

namespace {

enum class State : uint8_t { NoCheck, OutOfRange, InRange };

struct Slot {
    int refs = 0;
    bool pending = false; // fire on the next check, changed or not
    State last = State::NoCheck;
};

Slot g_slots[Offsets::ACTION_TABLE_MAX_SLOTS];

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("slot", "luaIndex"),
    Game::Doc::Req("isInRange", "bool",
                   "Whether the action's target is in range. Should not be used if checksRange "
                   "is false."),
    Game::Doc::Req("checksRange", "bool",
                   "False when no range check was made, for example when there is no target."),
};
const Game::Doc::Event kDoc{
    "ActionBar",
    "Fires when an action that has EnableActionRangeCheck on goes in or out of range of its "
    "target.",
    kPayload};
const Event::Custom::AutoReserve kEvent{"ACTION_RANGE_CHECK_UPDATE", &kDoc};

State Evaluate(int slot0) {
    int inRange = 0;
    if (Action::Range::Answer(slot0, &inRange) == 0)
        return State::NoCheck;
    return inRange != 0 ? State::InRange : State::OutOfRange;
}

void OnWorldTick() {
    const int event = kEvent.Slot();
    if (!Event::Custom::HasListeners(event))
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    for (int slot0 = 0; slot0 < Offsets::ACTION_TABLE_MAX_SLOTS; ++slot0) {
        Slot &slot = g_slots[slot0];
        if (slot.refs == 0)
            continue;
        const State state = Evaluate(slot0);
        if (!slot.pending && state == slot.last)
            continue;
        slot.pending = false;
        slot.last = state;
        Game::Lua::PushNumber(L, static_cast<double>(slot0 + 1));
        Game::Lua::PushBool(L, state == State::InRange);
        Game::Lua::PushBool(L, state != State::NoCheck);
        Event::Custom::FireValues(event, L, top + 1, 3);
        Game::Lua::SetTop(L, top);
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

// --- C_ActionBar.EnableActionRangeCheck(slot, enable) -----------------------

int __fastcall Script_EnableActionRangeCheck(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_ActionBar.EnableActionRangeCheck(slot, enable)");
        return 0;
    }
    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS)
        return 0;
    Slot &slot = g_slots[slot0];
    if (Game::Lua::ToBoolean(L, 2) != 0) {
        ++slot.refs;
        slot.pending = true;
    } else if (slot.refs > 0) {
        --slot.refs;
    }
    return 0;
}

const Game::Doc::Field kEnableArgs[] = {
    Game::Doc::Req("slot", "luaIndex"),
    Game::Doc::Req("enable", "bool",
                   "True if changes in range for the action should dispatch "
                   "ACTION_RANGE_CHECK_UPDATE. False if the action no longer needs the event."),
};
const Game::Doc::Function kEnableDoc{
    "Used with ACTION_RANGE_CHECK_UPDATE to be informed when an action goes in or out of range "
    "of its target.",
    kEnableArgs};

void RegisterLuaFunctions() {
    for (Slot &slot : g_slots)
        slot = Slot{};
    Game::Lua::RegisterTableFunction("C_ActionBar", "EnableActionRangeCheck",
                                     &Script_EnableActionRangeCheck, &kEnableDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Action::RangeCheck
