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

// `ACTION_USABLE_CHANGED(changes)` — the slots whose usable / noMana pair
// changed, as `{ slot, usable, noMana }` entries.
//
// The engine's bulk refresh FUN_ACTION_UPDATE_USABLE already finds them: it
// computes each slot with FUN_ACTION_SLOT_USABLE, compares the pair against
// the caches IsUsableAction reads, stores a changed pair, and signals the
// argument-less ACTIONBAR_UPDATE_USABLE once when any changed. Two of its
// calls are redirected. The per-slot call records the slots whose pair differs
// from the caches, the same compare the engine makes next. The signal sends
// ACTIONBAR_UPDATE_USABLE unchanged, then this event with the recorded slots.
//
// The per-slot refresh in FUN_ACTION_SLOT_CHANGED_NOTIFY stores its pair
// without this event; it signals ACTIONBAR_SLOT_CHANGED for the slot instead.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdint>
#include <cstring>

namespace Action::UsableChanged {

namespace {

using SlotUsable_t = uint32_t(__fastcall *)(uint32_t slot0, uint32_t *outNoMana);
using FireEvent_t = void(__fastcall *)(int eventID);

struct Change {
    uint32_t slot0;
    bool usable;
    bool noMana;
};

// This refresh's changes. The loop runs slots 0..119 in order, so slot 0
// starts a new list.
Change g_changes[Offsets::ACTION_TABLE_MAX_SLOTS];
int g_count = 0;

const Game::Doc::Field kStateFields[] = {
    Game::Doc::Req("slot", "luaIndex"),
    Game::Doc::Req("usable", "bool"),
    Game::Doc::Req("noMana", "bool"),
};
const Game::Doc::Structure kState{"ActionUsableState", "ActionBar", kStateFields};

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("changes", "table", "An ActionUsableState for each slot that changed."),
};
const Game::Doc::Event kDoc{
    "ActionBar", "Fires when the usable or noMana state of one or more action slots changes.",
    kPayload};
const Event::Custom::AutoReserve kEvent{"ACTION_USABLE_CHANGED", &kDoc};

uint32_t __fastcall SlotUsable_h(uint32_t slot0, uint32_t *outNoMana) {
    const uint32_t usable =
        reinterpret_cast<SlotUsable_t>(Offsets::FUN_ACTION_SLOT_USABLE)(slot0, outNoMana);
    if (slot0 == 0)
        g_count = 0;
    const auto *usableCache =
        reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(Offsets::VAR_ACTION_USABLE));
    const auto *noManaCache =
        reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(Offsets::VAR_ACTION_NO_MANA));
    if (slot0 < static_cast<uint32_t>(Offsets::ACTION_TABLE_MAX_SLOTS) &&
        g_count < Offsets::ACTION_TABLE_MAX_SLOTS &&
        (usable != usableCache[slot0] || *outNoMana != noManaCache[slot0]))
        g_changes[g_count++] = {slot0, usable != 0, *outNoMana != 0};
    return usable;
}

void __fastcall FireUsable_h(int eventID) {
    // Copied first: the handlers below can run another refresh.
    Change changes[Offsets::ACTION_TABLE_MAX_SLOTS];
    const int count = g_count;
    std::memcpy(changes, g_changes, sizeof(Change) * count);
    g_count = 0;

    reinterpret_cast<FireEvent_t>(Offsets::FUN_FIRE_EVENT_NO_ARGS)(eventID);

    const int event = kEvent.Slot();
    void *L = Game::Lua::State();
    if (count == 0 || !Event::Custom::HasListeners(event) || L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::NewTable(L);
    for (int i = 0; i < count; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "slot", static_cast<double>(changes[i].slot0 + 1));
        Game::Lua::SetFieldBool(L, "usable", changes[i].usable);
        Game::Lua::SetFieldBool(L, "noMana", changes[i].noMana);
        Game::Lua::RawSet(L, -3);
    }
    Game::Lua::SetN(L, -1, count);
    Event::Custom::FireValues(event, L, top + 1, 1);
    Game::Lua::SetTop(L, top);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_UPDATE_USABLE_SLOT_USABLE, Offsets::FUN_ACTION_SLOT_USABLE,
                         reinterpret_cast<const void *>(&SlotUsable_h));
    Common::RedirectCall(Offsets::CALL_UPDATE_USABLE_FIRE, Offsets::FUN_FIRE_EVENT_NO_ARGS,
                         reinterpret_cast<const void *>(&FireUsable_h));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Action::UsableChanged
