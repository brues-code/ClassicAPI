// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `GetItemCooldown(itemInfo)` — direct-by-ID item cooldown query.
// Vanilla ships `GetContainerItemCooldown(bag, slot)` and
// `GetInventoryItemCooldown(unit, slot)` which both require a
// physical slot reference; this surfaces the same data keyed by
// itemID, matching modern's signature.
//
// Returns `(startTime, duration, enable)` as three numbers — same
// shape as 1.12's existing item-cooldown getters, NOT the modern
// SpellCooldownInfo table shape. `startTime` and `duration` are
// `GetTime()`-compatible seconds; `enable` is `1` for "ready or
// counting down" / `0` for "used but cooldown is on hold" (potion-
// in-combat case).
//
// `PushCooldown` is the shared helper, exposed via `item/Cooldown.h`
// so `Container::Cooldown` can reuse it for `C_Container.GetItemCooldown`.

#include "item/Cooldown.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"

#include <cstdint>

namespace Item::Cooldown {

namespace {

// `__fastcall(uint itemID, int *outDuration, int *outStart,
// uint *outEnable)` — see Offsets.h `FUN_ITEM_QUERY_COOLDOWN`.
// First arg is the itemID value itself, despite Ghidra labeling it
// `uint *` (the ItemStats cache lookup uses the integer both as a
// hash key and as the field-zero match target, which is how it
// inherits the pointer typing in the decomp).
//
// `outDuration` / `outStart` are *unsigned* millisecond ticks. Reading
// the start into a signed `int` makes every timestamp negative once the
// machine has been up past 2^31 ms (~24.9 days), which puts
// `startTime + duration` in the past and makes every item read as off
// cooldown. `Aura::Data` already stores these as `uint32_t`.
using QueryItemCooldown_t = bool(__fastcall *)(uint32_t itemID,
                                                uint32_t *outDuration,
                                                uint32_t *outStart,
                                                uint32_t *outEnable);

} // namespace

void PushCooldown(void *L, int itemID) {
    uint32_t durationMs = 0;
    uint32_t startMs = 0;
    uint32_t enable = 0;
    if (itemID > 0) {
        auto fn = reinterpret_cast<QueryItemCooldown_t>(
            static_cast<uintptr_t>(Offsets::FUN_ITEM_QUERY_COOLDOWN));
        fn(static_cast<uint32_t>(itemID), &durationMs, &startMs, &enable);
    }
    // ms → seconds, matching GetTime()'s scale. Same conversion the
    // engine's own `Script_GetContainerItemCooldown` does at the
    // Lua boundary.
    Game::Lua::PushNumber(L, static_cast<double>(startMs) * 0.001);
    Game::Lua::PushNumber(L, static_cast<double>(durationMs) * 0.001);
    Game::Lua::PushNumber(L, static_cast<double>(enable));
}

// Global `GetItemCooldown(itemInfo)` — accepts itemID, item link, or
// numeric string, via the shared `Item::Arg::ResolveItemID`.
static int __fastcall Script_GetItemCooldown(void *L) {
    PushCooldown(L, Item::Arg::ResolveItemID(L, 1));
    return 3;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetItemCooldown", &Script_GetItemCooldown);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Cooldown
