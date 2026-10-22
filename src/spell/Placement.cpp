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

// Targeting placement and spell dispatch — see Placement.h.
//
// The commit mirrors the engine's own ground click: the world click handler
// (`0x492580`) binds the press-time pick through `FUN_COMMIT_PLACEMENT_COORDS`
// (`0x6e60f0`) only when the pick state is 1 (terrain). `0x6e60f0` returns 1
// in `al` when it bound a point, ORs the bound bit into the pending cast's
// mask, clears it from the targeting word, and sends the cast only when the
// word is then 0. A spell that wants a second target keeps the word nonzero
// and is not sent, so "landed" is "bound AND no targeting left".

#include "Placement.h"

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::Placement {

namespace {

using RefreshCursorRaycast_t = void(__fastcall *)(void *clickInfo);
using CommitPlacementCoords_t = uint8_t(__fastcall *)(const float *xyz);
using StopPlacement_t = void(__fastcall *)();

// 4th arg is the GUID high dword. Ghidra types it `float` from usage, but
// it's a plain dword on the stack (the engine assigns it the current
// selection's high dword when 0); `unsigned` puts the correct 4 bytes there.
using CastDispatch_t = void(__fastcall *)(unsigned slot, int bookType,
                                           unsigned targetGuidLo, unsigned targetGuidHi);
using NameToSlot_t = int(__fastcall *)(const char *name, void *out);

uint32_t State() {
    return *reinterpret_cast<const uint32_t *>(Offsets::VAR_SPELL_PLACEMENT_STATE);
}

bool InWorld() {
    // Player resolves only when in-world. Same gate other modules
    // (e.g. `Item::Location::ResolveBag`) use to avoid touching
    // engine state during loading screens or glue.
    return Game::ResolveUnitToken("player") != nullptr;
}

// Shared placement-readiness gate for the cursor and explicit-coords
// commits. No placement → false (no cancel); non-ground placement →
// cancel + false; not in world → false; otherwise ready → true.
bool PlacementReady() {
    const uint32_t state = State();
    if (state == 0)
        return false; // no placement active — nothing to commit

    if ((state & Offsets::SPELL_PLACEMENT_GROUND_MASK) == 0) {
        // Spell wants a unit / item / corpse / etc. — not ground.
        // Cancel so the cursor recovers; caller learns via the false
        // return that no placement happened.
        Stop();
        return false;
    }

    return InWorld();
}

// Binds `coords` and reports whether the cast went out. A spell still
// wanting a target after the bind is cancelled rather than left half-bound.
bool Commit(const float coords[3]) {
    auto commit = reinterpret_cast<CommitPlacementCoords_t>(
        Offsets::FUN_COMMIT_PLACEMENT_COORDS);
    if (commit(coords) != 0 && !IsActive())
        return true;
    Stop();
    return false;
}

void DispatchSlot(int slot0Based, int bookType, uint64_t targetGuid) {
    auto dispatch = reinterpret_cast<CastDispatch_t>(
        Offsets::FUN_SPELL_CAST_DISPATCH);
    // GUID 0 = no implicit target (engine uses current selection / placement).
    // A real GUID casts a unit-target spell straight at that unit.
    dispatch(static_cast<unsigned>(slot0Based), bookType,
             static_cast<unsigned>(targetGuid),
             static_cast<unsigned>(targetGuid >> 32));
}

} // namespace

bool IsActive() { return State() != 0; }

void Stop() {
    reinterpret_cast<StopPlacement_t>(Offsets::FUN_STOP_PLACEMENT)();
}

bool CommitAt(const float coords[3]) {
    if (!PlacementReady())
        return false;
    return Commit(coords);
}

bool GroundPending() { return PlacementReady(); }

bool CommitAtCursor() {
    if (!PlacementReady())
        return false;

    void *clickInfo = *reinterpret_cast<void *const *>(
        Offsets::VAR_WORLDFRAME_CLICK_INFO);
    if (clickInfo == nullptr)
        return false;

    // Re-raycast at the current cursor screen position. Writes the
    // new click type to `[clickInfo + OFF_CLICK_INFO_TYPE]` and the
    // hit world coords to `[clickInfo + OFF_CLICK_INFO_COORDS]`.
    auto refresh = reinterpret_cast<RefreshCursorRaycast_t>(
        Offsets::FUN_REFRESH_CURSOR_RAYCAST);
    refresh(clickInfo);

    const uint32_t hitType = *reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(clickInfo) + Offsets::OFF_CLICK_INFO_TYPE);
    if (hitType != 1) {
        // Cursor wasn't on terrain (over UI, off-screen, on a unit).
        // The engine's click binds nothing there either; we cancel so
        // the reticle doesn't stay armed for a click nobody asked for.
        Stop();
        return false;
    }

    return Commit(reinterpret_cast<const float *>(
        static_cast<const uint8_t *>(clickInfo) + Offsets::OFF_CLICK_INFO_COORDS));
}

bool FinishAtUnit(const float coords[3], bool placeGround) {
    // Unit-target / normal casts fired straight at the GUID — no placement.
    if (!IsActive())
        return true;
    if (!placeGround) {
        // Caller wants the unit as the cast target but not the aim that
        // comes with placing a ground spell there. Leave a ground reticle
        // up for the player to click, which is what casting it by hand
        // does; anything else still has to be cancelled.
        return GroundPending();
    }
    // Ground spells are sitting in placement mode; commit at the unit's
    // feet (a non-ground placement is an unaccepted unit target and is
    // cancelled).
    return CommitAt(coords);
}

bool DispatchSpellCast(int spellID, uint64_t targetGuid) {
    if (spellID <= 0)
        return false;

    // Resolve the EXACT spellID to its spellbook slot. Every rank is its own
    // slot, so we must NOT go through name resolution — that walks to the
    // highest rank and would cast Blizzard(Rank 6) for a Rank 1 (spellID 10)
    // request. `FindSpellbookSlot` returns a 1-based slot; the dispatcher
    // wants the 0-based index into `VAR_PLAYER_SPELLBOOK`.
    int bookType = 0;
    const int slot1 = Spell::Lookup::FindSpellbookSlot(spellID, &bookType);
    if (slot1 <= 0)
        return false; // not in the player's spellbook

    DispatchSlot(slot1 - 1, bookType, targetGuid);
    return true;
}

bool DispatchSpellCastByName(const char *name, uint64_t targetGuid) {
    if (name == nullptr || *name == '\0')
        return false;

    // The engine's resolver strips a trailing `(Rank N)` and returns the
    // 0-based spellbook slot (highest rank when no rank is given). This is the
    // by-name path — for an exact rank prefer the numeric spellID overload.
    auto nameToSlot = reinterpret_cast<NameToSlot_t>(
        Offsets::FUN_RESOLVE_SPELL_NAME_TO_SLOT);
    int bookType = 0;
    const int slot = nameToSlot(name, &bookType);
    if (slot < 0)
        return false; // not in the player's spellbook

    DispatchSlot(slot, bookType, targetGuid);
    return true;
}

} // namespace Spell::Placement
