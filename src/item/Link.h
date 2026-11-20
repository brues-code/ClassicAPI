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

#include <cstddef>
#include <cstdint>

namespace Item::Link {

// Builds the fully-decorated per-instance item hyperlink for `cgItem`
// via the engine's internal link builder at `FUN_GAMETOOLTIP_BUILD_ITEM_LINK`
// (`0x0052AE00`). The returned string sits in the engine's long-lived
// scratch buffer; safe to `PushString` immediately (Lua copies the
// content). Returns nullptr if `cgItem` is null or the engine can't
// resolve the instance block. The link includes enchant ID, random
// suffix factor, unique ID, and any random-suffix-decorated name —
// same output `GetInventoryItemLink` / `GetContainerItemLink` would
// produce for the same slot.
const char *FromCGItem(const uint8_t *cgItem);

// Writes the item's per-instance decorated *display name* into `out` —
// the same `[Name]` the engine stamps into `FromCGItem`'s link, e.g.
// "Iridium Chain of the Owl" (or the base name when the instance carries
// no random suffix). Reads the suffix ID off the CGItem descriptor
// (`+0x98`) and formats it through the engine's own name builder
// (`FUN_ITEM_BUILD_INSTANCE_NAME`). Returns false (and leaves `out`
// empty) for a null item or an empty build result, so callers can fall
// back to the base ItemStats name. Use whenever a name is returned
// alongside a `FromCGItem` link, so the two agree.
bool NameFromCGItem(const uint8_t *cgItem, char *out, size_t outSize);

// Builds a basic itemID-only hyperlink (`"|cffRRGGBB|Hitem:N:0:0:0|h[Name]|h|r"`)
// into `out` from the cached ItemStats record for `itemID`. No
// enchant / random-suffix decoration — use this when the caller has
// only an itemID and no live CGItem (mail inbox attachments, cursor
// drag-source items, vendor roster previews). Returns true on
// success, false if the item isn't cached or the buffer is too small.
//
// `outSize` should be at least 256 to comfortably fit the longest
// possible link (`item:` payload + colored prefix + bracketed name).
bool BasicFromItemID(uint32_t itemID, char *out, size_t outSize);

// Writes the suffix-decorated display name for `(itemID, suffixID)` into
// `out` WITHOUT a live item instance — the by-string analog of
// NameFromCGItem. `suffixID` is the 3rd `item:` field (random property /
// suffix); 0 = base name. Applies the "... of the Owl" suffix via the
// engine's inner builder (FUN_ITEM_BUILD_NAME_FROM_ID) when the item is
// cached and the suffix is a valid ItemRandomProperties row. Returns false
// (out empty) if the item isn't cached or has no name — warm and retry.
bool NameFromIDSuffix(uint32_t itemID, int suffixID, char *out, size_t outSize);

// Like BasicFromItemID but preserves the random `suffixID` in both the
// `item:` payload (`item:N:0:suffix:0`) and the bracketed, suffix-decorated
// name. `suffixID` 0 reduces exactly to BasicFromItemID.
bool BasicFromIDSuffix(uint32_t itemID, int suffixID, char *out, size_t outSize);

// The link the engine's GetLootRollItemLink builds for a roll: the quality
// colour, `item:id:0:suffix:unique`, and the suffix-decorated name, through
// the engine's own formatter (FUN_ITEM_FORMAT_LINK). Points into the engine's
// link buffer, valid until the next link is built; nullptr while the item is
// not cached.
const char *FromIDSuffixUnique(uint32_t itemID, int suffixID, uint32_t unique);

} // namespace Item::Link
