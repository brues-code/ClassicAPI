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

// `CURSOR_CHANGED` — fires when what the cursor holds changes, carrying
// the old and new cursor types:
//
//   CURSOR_CHANGED: isDefault, newCursorType, oldCursorType, oldCursorVirtualID
//
// Plus the `Enum.UICursorType` table the two type arguments index.
//
// The trigger is a once-per-frame diff of the cursor globals, NOT the
// engine's own `CURSOR_UPDATE`, because that event does not cover the
// cursor. Its only three fire sites are:
//
//   FUN_00495190  the cursor CLEAR — fires for every type, but only
//                 after it has already zeroed the type global, so the
//                 read at that point always says "empty"
//   FUN_00494b60  bag-item set — fires only when a GUID is present
//   FUN_004950f0  generic-item set — fires only when an itemID is
//                 present
//
// So picking up money, a spell, a macro, a pet action or a stabled pet
// produces exactly one `CURSOR_UPDATE`, from the clear, describing an
// empty cursor; the write that puts the new content in place announces
// nothing. Only the two item paths fire again after the set, which is
// why an interceptor on that event reported item cursors correctly and
// nothing else. There is no common choke point on the set side — each
// type writes `VAR_CURSOR_TYPE` in its own path — so a state diff is
// what covers every type, including any path not enumerated above.
//
// `Info::ReadRaw` is the cheap half of that diff: plain global reads, no
// object-manager lookup. Only once it differs do we pay for
// `Info::Current()`, which resolves a bag item's itemID.
//
// `oldCursorVirtualID` is the one argument that is a judgement call: the
// modern client's notion of a "virtual ID" is not something this client
// has, so it carries the previous content's identifying number instead
// (see `Cursor::Info::State::virtualID`). No consumer is known to read
// it — Blizzard's own `CURSOR_CHANGED` users all treat the event as a
// bare notification and re-check state themselves.

#include "Game.h"
#include "cursor/Info.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"

#include <cstdint>

namespace Cursor::Changed {

namespace {

const Event::Custom::AutoReserve _evtCursorChanged{"CURSOR_CHANGED"};

// The full modern enum, not just the values this client can produce. An
// addon comparing against `Enum.UICursorType.BattlePet` should read a
// number it can never match, rather than index a nil field.
const Game::Lua::EnumIntegerEntry kCursorTypeEntries[] = {
    {"Default", 0},        {"Item", 1},
    {"Money", 2},          {"Spell", 3},
    {"PetAction", 4},      {"Merchant", 5},
    {"ActionBar", 6},      {"Macro", 7},
    {"Ammo", 8},           {"Pet", 9},
    {"GuildBank", 10},     {"GuildBankMoney", 11},
    {"EquipmentSet", 12},  {"Currency", 13},
    {"Flyout", 14},        {"VoidItem", 15},
    {"BattlePet", 16},     {"Mount", 17},
    {"Toy", 18},           {"ConduitCollectionItem", 19},
    {"PerksProgramVendorItem", 20},
};

// The cursor as of the last event we fired — the raw form for the cheap
// per-frame compare, and the reported form to supply the `old*` arguments
// of the next fire. Deliberately NOT reset on `/reload`: the DLL and the
// engine's cursor globals both outlive the reload, so keeping them means
// a cursor still holding something across a reload reports its real
// previous type rather than a phantom change back from `Default`.
Info::Raw g_lastRaw{};
Info::State g_lastState{0, 0};

void OnTick() {
    const Info::Raw raw = Info::ReadRaw();
    if (Info::Same(raw, g_lastRaw))
        return; // cursor untouched this frame — the common case
    g_lastRaw = raw;

    const Info::State now = Info::Current();
    const Info::State previous = g_lastState;
    g_lastState = now;

    const int slot = _evtCursorChanged.Slot();
    if (slot < 0)
        return;

    // The dispatcher has no boolean code, and pushing `%d` 0 would hand
    // Lua the number 0 — truthy. `%s` with a null string tail-jumps to
    // pushnil instead, so `if isDefault then` reads correctly either
    // way. Same trick the PLAYER_ENTERING_WORLD payload uses.
    auto *const kNil = static_cast<const char *>(nullptr);
    if (now.uiType == 0) {
        Event::Custom::Fire(slot, "%d%d%d%u", 1, now.uiType, previous.uiType,
                            previous.virtualID);
    } else {
        Event::Custom::Fire(slot, "%s%d%d%u", kNil, now.uiType,
                            previous.uiType, previous.virtualID);
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum(
        "Enum", "UICursorType", kCursorTypeEntries,
        sizeof(kCursorTypeEntries) / sizeof(kCursorTypeEntries[0]));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Cursor::Changed
