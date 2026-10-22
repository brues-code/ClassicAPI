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

#pragma once

#include <cstdint>

namespace Item::Charges {

// Pushes the total uses available in this single slot — the
// per-slot counterpart of `GetItemCount(_, _, includeCharges=true)`.
// Returns `1` (pushed one value) on success, `0` (pushes nothing,
// caller surfaces as `nil`) when the item is missing or the
// descriptor is null.
//
// Math is `stack * usesPerItem` where `usesPerItem` is:
//   - `SPELL_CHARGES[0] >= 0` → `1` (no destroy-on-zero behavior;
//     stack count alone reflects "how many uses").
//   - `SPELL_CHARGES[0] < 0`  → `abs(value)` (consume-on-use items;
//     wand at 50 has `-50` so each item contributes 50).
//
// Worked examples:
//   - Wand at 50 charges (stack=1, raw=-50): `1 * 50 = 50`
//   - Hearthstone     (stack=1, raw=0):   `1 * 1  = 1` (cooldown-
//     only, no charges concept; we still surface "1 use").
//   - Water stack of 20 (stack=20, raw=-1): `20 * 1 = 20`
//   - Equipped sword  (stack=1, raw=0):    `1 * 1  = 1`
//
// Caller can `return PushChargesForItem(L, item);` to forward the
// count directly.
int PushChargesForItem(void *L, const uint8_t *item);

} // namespace Item::Charges
