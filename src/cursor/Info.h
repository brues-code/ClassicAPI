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

namespace Cursor::Info {

// What the cursor is holding, in the terms `CURSOR_CHANGED` reports.
struct State {
    // `Enum.UICursorType` value — `Default` (0) when the cursor is empty.
    // Agrees with the type string `GetCursorInfo` returns wherever that
    // function returns one; the pet-action and stable-pet cursors have no
    // `GetCursorInfo` string but do have an enum value, so they report
    // `PetAction` / `Pet` here while `GetCursorInfo` stays nil.
    int uiType;
    // The held content's identifying number: itemID, spellID, 1-based
    // macro slot, 1-based merchant slot, or a money amount in copper. 0
    // for an empty cursor and for the two pet cursors, whose payload the
    // engine does not keep anywhere readable.
    uint32_t virtualID;
};

// Reads the live cursor globals. Side-effect free. Resolving a bag item's
// itemID needs an object-manager lookup, so prefer `ReadRaw` when all you
// need is "did the cursor change".
State Current();

// The identity of whatever the cursor holds, as the raw stored values —
// no object-manager lookup, so this is cheap enough to read every frame.
// Two reads that compare equal mean the cursor has not changed.
//
// Every global that identifies a holding is included, so this catches a
// change of type and a change of content within one type, including
// swapping between two stacks of the same item (their GUIDs differ).
struct Raw {
    uint32_t type;
    uint32_t guidLo;
    uint32_t guidHi;
    uint32_t genericSlot;
    uint32_t money;
    uint32_t spellID;
    uint32_t macroID; // the stored macroID, not the slot — cheaper here
};

Raw ReadRaw();
bool Same(const Raw &a, const Raw &b);

// True if a GUID-bearing item is currently on the cursor —
// `VAR_CURSOR_ITEM_GUID != 0`, the same globals `GetCursorInfo`'s bag-item
// path reads and the exact signal the engine's item-pickup path guards on.
// Used to gate cursor pickups (don't clobber a held item).
bool HasItem();

} // namespace Cursor::Info
