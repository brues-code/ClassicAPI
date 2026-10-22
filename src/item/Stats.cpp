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

// `C_Item.GetItemStats(itemLink)` and `C_Item.GetItemStatDelta(link1,
// link2)` — read an item's stats and return them as a table keyed by
// FrameXML global-string names, the same shape modern WoW's GetItemStats
// uses (so `_G[key]` yields the display label). `GetItemStatDelta` is the
// per-stat difference of two items.
//
// The stat derivation (base record + on-equip spell auras + random
// suffix) lives in the shared `Item::StatAccum` engine
// ([src/item/StatAccum.cpp]); this file is just the Lua bindings that
// turn an accumulator into a returned table.

#include "Game.h"
#include "item/Arg.h"
#include "item/Record.h"
#include "item/StatAccum.h"

#include <cstdint>

namespace Item::Stats {

namespace {

using StatAccum::Accum;

// True when the optional `statTable` argument at `idx` is absent, nil, or
// a table — the engine's own `[, statTable]` check.
bool IsStatTableArg(void *L, int idx) {
    const int type = Game::Lua::Type(L, idx);
    return type <= Game::Lua::TYPE_NIL || type == Game::Lua::TYPE_TABLE;
}

// Leaves the result table on the stack top: the caller's `statTable` at
// `tableIdx` when one was passed (filled in place and not wiped, as the
// engine does — a reused table keeps keys from earlier calls), else a
// fresh one. Writes each non-zero accumulator entry as `t[key] = value`,
// plus the weapon DPS under `ITEM_MOD_DAMAGE_PER_SECOND_SHORT` when `dps`
// is non-zero. Zero entries are omitted (matches GetItemStats' sparse
// output and GetItemStatDelta showing only differences). DPS is a float,
// kept separate from the integer accumulator. The epsilon guards against
// float-subtraction noise producing a spurious near-zero delta.
void PushAccumTable(void *L, int tableIdx, const Accum *acc, double dps) {
    if (Game::Lua::Type(L, tableIdx) == Game::Lua::TYPE_TABLE)
        Game::Lua::SetTop(L, tableIdx);
    else
        Game::Lua::NewTable(L);
    for (int i = 0; i < StatAccum::kCount; ++i)
        if (acc[i].value != 0)
            Game::Lua::SetFieldNumber(L, acc[i].key,
                                      static_cast<double>(acc[i].value));
    if (dps > 1e-6 || dps < -1e-6)
        Game::Lua::SetFieldNumber(L, "ITEM_MOD_DAMAGE_PER_SECOND_SHORT", dps);
}

} // namespace

// `C_Item.GetItemStats(itemLink[, statTable])` → statTable. Keys are
// FrameXML global-string names; values are the item's stat magnitudes
// including on-equip-spell bonuses (crit/AP/spell power/…) and any
// random-suffix bonus. Only stats the item actually carries are present.
// A passed `statTable` is filled instead of allocating one.
//
// Accepts a full chat link, an `item:N…` string, or a bare itemID (a
// superset of retail, which is link-only). A bare itemID or a link with
// no suffix field yields the item's stats without a suffix. Returns
// nothing if the item isn't cached yet (the engine's own uncached
// return) — warm it via GetItemInfo and retry.
int __fastcall Script_GetItemStats(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0 || !IsStatTableArg(L, 2)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemStats(itemLink[, statTable])");
        return 0;
    }
    const int suffixID = StatAccum::ParseRandomSuffixFromLink(L, 1);
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr)
        return 0;
    Accum acc[StatAccum::kCount];
    StatAccum::Init(acc);
    StatAccum::AccumulateRecord(acc, record, +1);
    StatAccum::ApplyRandomSuffix(acc, suffixID, +1);
    PushAccumTable(L, 2, acc, StatAccum::ComputeDPS(record));
    return 1;
}

// `C_Item.GetItemStatDelta(itemLink1, itemLink2[, statTable])` →
// statTable of the per-stat difference `item1 - item2` (positive = item1
// has more of that stat) — the engine's `first - second`, verified in the
// 3.3.5 (`0x00608760`) and 4.3.4 binaries. On-equip-spell and
// random-suffix bonuses on either link are included. Only stats whose
// delta is non-zero appear. Same key set, input forms, `statTable`, and
// caching caveat as `C_Item.GetItemStats`; returns nothing if either item
// is uncached.
int __fastcall Script_GetItemStatDelta(void *L) {
    const int id1 = Item::Arg::ResolveItemID(L, 1);
    const int id2 = Item::Arg::ResolveItemID(L, 2);
    if (id1 <= 0 || id2 <= 0 || !IsStatTableArg(L, 3)) {
        Game::Lua::Error(
            L, "Usage: C_Item.GetItemStatDelta(itemLink1, itemLink2[, statTable])");
        return 0;
    }
    const int suffix1 = StatAccum::ParseRandomSuffixFromLink(L, 1);
    const int suffix2 = StatAccum::ParseRandomSuffixFromLink(L, 2);
    const uint8_t *r1 = Item::PeekRecord(static_cast<uint32_t>(id1));
    const uint8_t *r2 = Item::PeekRecord(static_cast<uint32_t>(id2));
    if (r1 == nullptr || r2 == nullptr)
        return 0;
    Accum acc[StatAccum::kCount];
    StatAccum::Init(acc);
    StatAccum::AccumulateRecord(acc, r1, +1);
    StatAccum::ApplyRandomSuffix(acc, suffix1, +1);
    StatAccum::AccumulateRecord(acc, r2, -1);
    StatAccum::ApplyRandomSuffix(acc, suffix2, -1);
    PushAccumTable(L, 3, acc, StatAccum::ComputeDPS(r1) - StatAccum::ComputeDPS(r2));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemStats", &Script_GetItemStats);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemStatDelta",
                                     &Script_GetItemStatDelta);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Stats
