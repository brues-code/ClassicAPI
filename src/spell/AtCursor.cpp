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

// `C_Spell.CastAtCursor(spellID)` — cast a ground-target spell at
// the player's current cursor world position, bypassing the manual
// click on the AoE reticle.
//
// Implementation chains the engine's existing primitives:
//
//   1. Initiate the cast: resolve the *exact* `spellID` to its spellbook
//      slot (`Spell::Lookup::FindSpellbookSlot` — NOT name resolution, which
//      would cast the highest rank) and dispatch through
//      `FUN_SPELL_CAST_DISPATCH`. For a ground-target spell, the engine
//      enters placement mode at this point (sets `VAR_SPELL_PLACEMENT_STATE`);
//      for a normal cast, it fires immediately.
//
//   2. `Spell::AtCursor::Resolve()` then refreshes the cursor's world
//      raycast via `FUN_REFRESH_CURSOR_RAYCAST` and commits the
//      placement at the resulting world coords via
//      `FUN_COMMIT_PLACEMENT_COORDS`. Self-cancelling when the spell
//      isn't ground-target (unit-target placement → cancel) or when
//      the cursor isn't on terrain (over UI / off-screen → cancel).
//
// Returns `true` from `Resolve()` when we successfully placed at
// cursor; `false` otherwise (no placement was needed, or cursor was
// invalid). The cast itself proceeds either way — `Resolve()`'s
// return tells the caller whether the cursor-placement leg landed.

#include "AtCursor.h"

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"
#include "spell/MacroPrimarySpell.h"

#include <cstdint>
#include <cstring>

namespace Spell::AtCursor {

namespace {

using RefreshCursorRaycast_t = void(__fastcall *)(void *clickInfo);
using CommitPlacementCoords_t = unsigned(__fastcall *)(const float *xyz);
using StopPlacement_t = void(__fastcall *)();

bool InWorld() {
    // Player resolves only when in-world. Same gate other modules
    // (e.g. `Item::Location::ResolveBag`) use to avoid touching
    // engine state during loading screens or glue.
    return Game::ResolveUnitToken("player") != nullptr;
}

void CancelPlacement() {
    auto fn = reinterpret_cast<StopPlacement_t>(Offsets::FUN_STOP_PLACEMENT);
    fn();
}

void Commit(const float coords[3]) {
    auto commit = reinterpret_cast<CommitPlacementCoords_t>(
        Offsets::FUN_COMMIT_PLACEMENT_COORDS);
    commit(coords);
}

// Shared placement-readiness gate for both the cursor and explicit-
// coords commit paths. Decision tree (preserved from the original
// Resolve): no placement → false (no cancel); non-ground placement →
// cancel + false; not in world → false; otherwise ready → true.
bool PlacementReady() {
    const uint32_t state = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_SPELL_PLACEMENT_STATE);
    if (state == 0)
        return false; // no placement active — nothing to commit

    if ((state & Offsets::SPELL_PLACEMENT_GROUND_MASK) == 0) {
        // Spell wants a unit / item / corpse / etc. — not ground.
        // Cancel so the cursor recovers; caller learns via the false
        // return that no placement happened.
        CancelPlacement();
        return false;
    }

    return InWorld();
}

} // namespace

bool CommitAtCoords(const float coords[3]) {
    if (!PlacementReady())
        return false;
    Commit(coords);
    return true;
}

bool GroundPlacementPending() { return PlacementReady(); }

bool Resolve() {
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
        // Cursor wasn't on terrain (over UI, off-screen, on a unit
        // we didn't ask for). Modern `[@cursor]` would error; we
        // chose silent cancel-and-return-false.
        CancelPlacement();
        return false;
    }

    const float *coords = reinterpret_cast<const float *>(
        static_cast<const uint8_t *>(clickInfo) + Offsets::OFF_CLICK_INFO_COORDS);

    Commit(coords);
    return true;
}

namespace {

// 4th arg is the GUID high dword. Ghidra types it `float` from usage, but
// it's a plain dword on the stack (the engine assigns it the current
// selection's high dword when 0); `unsigned` puts the correct 4 bytes there.
using CastDispatch_t = void(__fastcall *)(unsigned slot, int bookType,
                                           unsigned targetGuidLo, unsigned targetGuidHi);
using NameToSlot_t = int(__fastcall *)(const char *name, void *out);

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

bool IsPlacementActive() {
    return *reinterpret_cast<const uint32_t *>(
               Offsets::VAR_SPELL_PLACEMENT_STATE) != 0;
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

namespace {

int __fastcall Script_C_Spell_CastAtCursor(void *L) {
    // Accept a numeric spellID (exact rank) or a spell name (the engine's
    // resolver parses a trailing "(Rank N)"). IsNumber is checked first so a
    // numeric string like "10" still takes the exact-spellID path.
    bool dispatched = false;
    if (Game::Lua::IsNumber(L, 1)) {
        dispatched = DispatchSpellCast(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    } else if (Game::Lua::IsString(L, 1)) {
        dispatched = DispatchSpellCastByName(Game::Lua::ToString(L, 1));
    }

    // Bad input / spell not in the player's spellbook → match
    // `CastSpellByName`'s silent failure.
    if (!dispatched) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    const bool placed = Resolve();
    Game::Lua::PushBool(L, placed);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kCastAtCursorArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier",
                   "A spell ID, which casts that exact rank, or a spell name, "
                   "which casts the highest rank you know unless it ends in \"(Rank N)\"."),
};
const Game::Doc::Field kCastAtCursorRets[] = {
    Game::Doc::Req("placed", "bool",
                   "True when the spell landed at the cursor; false when the cursor was "
                   "not on the ground, or the spell needs no ground target."),
};
const Game::Doc::Function kCastAtCursor{
    "Casts a ground-target spell at the cursor's world position, with no reticle click.",
    kCastAtCursorArgs, kCastAtCursorRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "CastAtCursor",
                                     &Script_C_Spell_CastAtCursor, &kCastAtCursor);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Macro-parser pattern: tag macros that call
// `C_Spell.CastAtCursor(<spellID>)` with the spellID, so action-bar
// UIs highlight the slot the same way they do for `/cast SpellName`.
// The Lua call only accepts a numeric spellID, so the extractor just
// reads a numeric literal — no name resolution needed.
int ExtractCastAtCursorArg(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t'))
        ++p;
    int value = 0;
    const char *digitStart = p;
    while (p < end && *p >= '0' && *p <= '9') {
        if (value > 100000000) // sanity cap before overflow
            return 0;
        value = value * 10 + (*p - '0');
        ++p;
    }
    return (p > digitStart && value > 0) ? value : 0;
}

const Spell::MacroPrimarySpell::PatternAutoRegister _patreg{
    "C_Spell.CastAtCursor(", &ExtractCastAtCursorArg};

} // namespace

} // namespace Spell::AtCursor
