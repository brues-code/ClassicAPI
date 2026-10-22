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

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "spell/Lookup.h"

#include <cstdint>

// `GetNumTrackingTypes()` / `GetTrackingInfo(index)` / `SetTracking(index)` —
// backport of the minimap-tracking enumeration the modern client exposes.
//
// Vanilla 1.12 never enumerates the tracking abilities the player KNOWS — it
// only stores the single currently-active tracker as a plain spellID in
// `VAR_ACTIVE_TRACKING_SPELL`, surfaced through `GetTrackingTexture()` /
// `CancelTrackingBuff()` / `GameTooltip:SetTrackingSpell()`. 3.3.5's
// `GetTrackingInfo` iterates a spellbook-derived list of tracking spells and
// reports name/texture/active/category per entry; we rebuild that list here.
//
// A tracking spell is identified exactly the way the engine's own active-
// tracker updater `FUN_004E4170` identifies one: a Spell.dbc record with an
// `EffectApplyAuraName` of Track Creatures (44), Track Resources (45), or
// Track Stealthed (151). That effect-based test reproduces pfUI's hardcoded
// spellID/icon table (Find Herbs/Minerals/Treasure/Trees, every Hunter
// Track*, Sense Undead/Demons, Track Humanoids) with no table to maintain,
// and auto-covers server-custom trackers such as Turtle's Find Trees (52917).
namespace Spell::Tracking {

// Tracking aura effect types — SPELL_AURA_TRACK_* ApplyAura names, the same
// values `FUN_004E4170` scans a spell's three effects for.
static constexpr int32_t AURA_TRACK_CREATURES = 44;
static constexpr int32_t AURA_TRACK_RESOURCES = 45;
static constexpr int32_t AURA_TRACK_STEALTHED = 151;

// True iff `spellID`'s record has any of the three Track* effects.
static bool IsTrackingSpell(int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return false;
    auto *aura = Game::Ptr<const int32_t>(
        record, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME);
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        if (aura[i] == AURA_TRACK_CREATURES || aura[i] == AURA_TRACK_RESOURCES ||
            aura[i] == AURA_TRACK_STEALTHED)
            return true;
    }
    return false;
}

// Walks the player spellbook in slot order counting tracking spells. If
// `wantNth >= 1` and an Nth tracking spell exists, writes its spellID and
// 1-based spellbook slot to `*outSpellID` / `*outSlot`. Returns the total
// number of tracking spells found (so callers detect out-of-range by
// comparing the return against `wantNth`).
//
// The spellbook array is zero-padded past the populated count (see
// `Spell::Lookup::FindSpellbookSlot`), so a 0 entry is just an empty slot —
// skip it and keep scanning the full range rather than break, matching the
// existing spellbook walkers.
static int EnumerateTracking(int wantNth, int *outSpellID, int *outSlot) {
    auto *book = reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELLBOOK));
    int found = 0;
    for (int i = 0; i < Offsets::SPELLBOOK_MAX_SLOTS; ++i) {
        const int spellID = book[i];
        if (spellID <= 0 || !IsTrackingSpell(spellID))
            continue;
        ++found;
        if (found == wantNth) {
            if (outSpellID != nullptr)
                *outSpellID = spellID;
            if (outSlot != nullptr)
                *outSlot = i + 1;
        }
    }
    return found;
}

// `GetNumTrackingTypes()` -> number of tracking spells in the player's book.
static int __fastcall Script_GetNumTrackingTypes(void *L) {
    Game::Lua::PushNumber(L,
                          static_cast<double>(EnumerateTracking(0, nullptr, nullptr)));
    return 1;
}

// `GetTrackingInfo(index)` -> name, texture, active, category, spellID.
//   - index is 1-based into the tracking list (spellbook order).
//   - texture is the SpellIcon.dbc path (nil if the icon is missing); the
//     active tracker shows its `activeIconID` when it has one, exactly as
//     3.3.5's `Script_GetTrackingInfo` picks it.
//   - active is 1 for the tracker currently in effect, nil otherwise (3.3.5
//     pushes the number, not a boolean).
//   - category is always "spell" — vanilla has no non-spell ("other")
//     trackers, unlike 3.3.5's class-masked table.
//   - spellID is a ClassicAPI extension (the modern 5th return is `nested`,
//     which has no meaning here); handy since selecting/among trackers is
//     spellID-driven.
// Returns nil for an out-of-range index.
static int __fastcall Script_GetTrackingInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTrackingInfo(index)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (index < 1)
        return 0;

    int spellID = 0;
    int slot = 0;
    if (EnumerateTracking(index, &spellID, &slot) < index)
        return 0; // index past the number of tracking types -> nil

    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    const int locale = Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    const char *name =
        Game::Read<const char *>(record, Offsets::OFF_SPELL_NAMES + locale * 4);

    const bool active =
        Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_ACTIVE_TRACKING_SPELL)) ==
        spellID;

    const char *texture = nullptr;
    const int activeIconID =
        Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_ACTIVE_ICON_ID);
    const int iconID = active && activeIconID != 0
                           ? activeIconID
                           : Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_ICON_ID);
    if (auto *iconRec = DBC::Record(Offsets::VAR_SPELL_ICON_RECORDS,
                                    Offsets::VAR_SPELL_ICON_COUNT,
                                    static_cast<uint32_t>(iconID))) {
        texture = Game::Read<const char *>(iconRec, Offsets::OFF_SPELLICON_PATH);
    }

    Game::Lua::PushString(L, name);    // 1. name (PushString handles NULL -> nil)
    Game::Lua::PushString(L, texture); // 2. texture (icon path) or nil
    if (active)                        // 3. active: 1 or nil
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushString(L, "spell"); // 4. category
    Game::Lua::PushNumber(L, static_cast<double>(spellID)); // 5. spellID (extension)
    return 5;
}

// `SetTracking(index)` — selects (activates) the tracking spell at `index`.
// Vanilla has no "set tracking" primitive: choosing a tracker IS casting its
// spell (the tracking dropdown does exactly this). So we resolve the entry's
// spellbook slot and tail-call the engine's own `Script_CastSpell(slot,
// "spell")` — reusing its slot bounds-check, target resolution, and error
// handling rather than reconstructing the cast path. Returns nil for an
// out-of-range index.
//
// With no number, 3.3.5 cancels the active tracking aura. That is vanilla's
// `CancelTrackingBuff()` (`0x004E4A80`): send CMSG_CANCEL_AURA for the
// tracking word when it is set. The word only ever holds a spell the aura
// rebuild found in Spell.dbc, so it needs no record check. The engine then
// does the rest: its aura rebuild (`0x004E4170`) zeroes the word, re-derives
// it from the live aura slots, and fires PLAYER_AURAS_CHANGED.
static int __fastcall Script_SetTracking(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        const int tracking =
            Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_ACTIVE_TRACKING_SPELL));
        if (tracking != 0) {
            using CancelAuraSend_t = void(__fastcall *)(int);
            reinterpret_cast<CancelAuraSend_t>(
                static_cast<uintptr_t>(Offsets::FUN_CANCEL_AURA_SEND))(tracking);
        }
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (index < 1)
        return 0;

    int spellID = 0;
    int slot = 0;
    if (EnumerateTracking(index, &spellID, &slot) < index)
        return 0;

    // Rebuild the stack as CastSpell(slot, "spell") and hand off to the
    // engine handler. `slot` is 1-based, matching what a Lua `CastSpell`
    // caller passes.
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, static_cast<double>(slot));
    Game::Lua::PushString(L, "spell");
    using CastSpell_t = int(__fastcall *)(void *);
    auto castSpell = reinterpret_cast<CastSpell_t>(Offsets::FUN_SCRIPT_CAST_SPELL);
    return castSpell(L);
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetNumTrackingTypes",
                                      &Script_GetNumTrackingTypes);
    Game::Lua::RegisterGlobalFunction("GetTrackingInfo", &Script_GetTrackingInfo);
    Game::Lua::RegisterGlobalFunction("SetTracking", &Script_SetTracking);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Tracking
