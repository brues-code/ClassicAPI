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

#include <cstdint>

namespace Faction::Info {

namespace {

// __fastcall(ecx = 0-based displayed index) → factionID, or 0 for OOB.
// MSVC's free-function __fastcall puts the first int arg in ECX, matching
// the engine's calling convention for this helper.
using ResolveIndex_t = int(__fastcall *)(int idx);
using GetReactionBand_t = unsigned char(__fastcall *)(int factionID);
using GetStanding_t = int(__fastcall *)(int factionID);

ResolveIndex_t Resolver() {
    return reinterpret_cast<ResolveIndex_t>(Offsets::FUN_RESOLVE_FACTION_INDEX);
}

// Returns the Faction.dbc record pointer for `factionID`, or nullptr if
// the ID is out of range or the slot is empty.
const uint8_t *FactionRecord(int factionID) {
    if (factionID <= 0)
        return nullptr;
    return DBC::Record(Offsets::VAR_FACTION_DBC_RECORDS,
                       Offsets::VAR_FACTION_DBC_COUNT,
                       static_cast<uint32_t>(factionID));
}

// Reads `record[offset + locale*4]` as a localized C string pointer.
const char *LocalizedString(const uint8_t *record, int offset) {
    const int locale = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    auto *strings = reinterpret_cast<const char *const *>(record + offset);
    return strings[locale];
}

// Pushes 1.0 for true and nil for false — the convention
// Script_GetFactionInfo uses for atWar/canToggleAtWar/etc. (engine
// pushes lua_pushnumber(1.0) or lua_pushnil at 0x004D65F2 /
// 0x004D6600 etc.). The engine has lua_pushboolean (we use it via
// Game::Lua::PushBoolean for C_* namespace functions); we match
// the number-or-nil shape here so encountered and unencountered
// factions return identically and addons can use a single
// comparison path.
void PushFlag(void *L, bool value) {
    if (value)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
}

// Resolves the local player's CGPlayer-side info sub-struct at
// `[player + 0xE68]`. Returns nullptr if "player" isn't resolvable
// or the sub-struct is uninitialized (pre-login / glue).
const uint8_t *PlayerInfo() {
    auto *player = static_cast<const uint8_t *>(Game::ResolveUnitToken("player"));
    if (player == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        player + Offsets::OFF_CGPLAYER_INFO);
}

// Returns the player's RepListID for the currently-watched faction,
// or -1 if no faction is watched.
int WatchedRepListID(const uint8_t *playerInfo) {
    return *reinterpret_cast<const int *>(
        playerInfo + Offsets::OFF_CGPLAYER_INFO_WATCHED_REP_LIST_ID);
}

// Snapshot of everything `Script_GetFactionInfo` derives for one
// faction. All fields are populated engine-direct — no
// `Script_GetFactionInfo` round-trip, no `lua_pcall` into any other
// Lua-side accessor. Used by:
//   - `GetFactionInfoByID` (pushes positional 11 returns)
//   - `C_Reputation.GetWatchedFactionData` (builds a modern table)
//   - `C_Reputation.GetFactionDataByIndex` (builds a modern table)
struct FactionData {
    int factionID;
    int repListIndex; // -1 if the faction has no rep slot (header/etc.)
    const char *name;
    const char *description;
    int reaction; // 1..8 (Hated..Exalted)
    int currentReactionThreshold;
    int nextReactionThreshold;
    int currentStanding;
    bool atWarWith;
    bool canToggleAtWar;
    bool isHeader;
    bool isCollapsed;
    bool isWatched;
};

// Fills `out` with the engine's view of `factionID`. Returns false
// only when the factionID doesn't resolve to a `Faction.dbc` record
// (out-of-range or empty slot) — callers should treat that as "no
// such faction". Unencountered factions (record exists, no rep slot)
// fill cleanly with currentStanding=0, atWar=false, etc.
//
// Reads, in order:
//   - Faction.dbc record  → name, description, repListIndex
//   - FUN_REPUTATION_GET_REACTION_BAND → reaction band (0..7)
//   - VAR_REACTION_MIN/MAX_TABLE       → bar thresholds for that band
//   - FUN_REPUTATION_GET_STANDING      → currentStanding
//   - rep slot flags (when repListIndex >= 0) → atWar, canToggleAtWar
//   - VAR_FACTION_HEADER_LIST / COLLAPSED_BITMASK → isHeader, isCollapsed
//   - player's watched repListID       → isWatched
//
// `canToggleAtWar` mirrors `Script_GetFactionInfo`'s logic exactly:
// false when `currentStanding < -3000` OR the rep slot's bit `0x10`
// is set (the "peace-forced" / "permanent allegiance" flag — true
// for your own faction's leader cities, false for togglable factions
// like the Goblin cartels).
bool ReadFactionData(int factionID, FactionData *out) {
    *out = {};
    if (factionID <= 0)
        return false;

    const uint8_t *record = FactionRecord(factionID);
    if (record == nullptr)
        return false;

    out->factionID = factionID;
    out->repListIndex = *reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_FACTION_REP_LIST_INDEX);
    out->name = LocalizedString(record, Offsets::OFF_FACTION_NAMES);
    out->description = LocalizedString(record, Offsets::OFF_FACTION_DESCRIPTIONS);
    if (out->description == nullptr)
        out->description = "";

    auto getBand = reinterpret_cast<GetReactionBand_t>(
        Offsets::FUN_REPUTATION_GET_REACTION_BAND);
    auto getStanding = reinterpret_cast<GetStanding_t>(
        Offsets::FUN_REPUTATION_GET_STANDING);

    const int band = getBand(factionID);
    out->reaction = band + 1;
    out->currentReactionThreshold = *reinterpret_cast<const int32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_REACTION_MIN_TABLE) +
        static_cast<uintptr_t>(band) * 4);
    out->nextReactionThreshold = *reinterpret_cast<const int32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_REACTION_MAX_TABLE) +
        static_cast<uintptr_t>(band) * 4);
    out->currentStanding = getStanding(factionID);

    if (out->repListIndex >= 0 && out->repListIndex < Offsets::MAX_REP_SLOTS) {
        auto *slot = reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_PLAYER_REP_SLOTS) +
            static_cast<uintptr_t>(out->repListIndex) * Offsets::REP_SLOT_STRIDE);
        const uint8_t flags = *(slot + Offsets::OFF_REP_SLOT_FLAGS);
        out->atWarWith = (flags & Offsets::REP_SLOT_FLAG_AT_WAR) != 0;
        // canToggleAtWar matches `Script_GetFactionInfo`'s composite
        // check: standing not below -3000 AND not peace-forced.
        out->canToggleAtWar = (out->currentStanding >= -3000) &&
                              (flags & Offsets::REP_SLOT_FLAG_PEACE_FORCED) == 0;
    }

    // Header / collapsed: factionID's position in the displayed-list
    // header array determines both. Same loop `Script_GetFactionInfo`
    // walks at `0x004D6638`.
    const int headerCount = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_FACTION_HEADER_COUNT));
    auto *headerList = reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_FACTION_HEADER_LIST));
    const int cap = headerCount < Offsets::MAX_FACTION_HEADERS
                        ? headerCount : Offsets::MAX_FACTION_HEADERS;
    for (int i = 0; i < cap; ++i) {
        if (headerList[i] == factionID) {
            out->isHeader = true;
            const uint32_t mask = *reinterpret_cast<const uint32_t *>(
                static_cast<uintptr_t>(Offsets::VAR_FACTION_COLLAPSED_BITMASK));
            // Bit SET = expanded, bit CLEAR = collapsed.
            out->isCollapsed = (mask & (1u << i)) == 0;
            break;
        }
    }

    // isWatched: faction's repListIndex matches player's watched slot.
    if (out->repListIndex >= 0) {
        if (const uint8_t *info = PlayerInfo()) {
            out->isWatched = (out->repListIndex == WatchedRepListID(info));
        }
    }

    return true;
}

// Builds the modern `FactionData`-shape table on the Lua stack from
// a populated `FactionData`. Caller can override `isWatched` (e.g.
// `GetWatchedFactionData` forces it true; nothing else does).
void PushFactionDataTable(void *L, const FactionData &d) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "factionID", static_cast<double>(d.factionID));
    Game::Lua::SetFieldString(L, "name", d.name);
    Game::Lua::SetFieldString(L, "description", d.description);
    Game::Lua::SetFieldNumber(L, "reaction", static_cast<double>(d.reaction));
    Game::Lua::SetFieldNumber(L, "currentReactionThreshold",
        static_cast<double>(d.currentReactionThreshold));
    Game::Lua::SetFieldNumber(L, "nextReactionThreshold",
        static_cast<double>(d.nextReactionThreshold));
    Game::Lua::SetFieldNumber(L, "currentStanding",
        static_cast<double>(d.currentStanding));
    Game::Lua::SetFieldBool(L, "atWarWith", d.atWarWith);
    Game::Lua::SetFieldBool(L, "canToggleAtWar", d.canToggleAtWar);
    Game::Lua::SetFieldBool(L, "isHeader", d.isHeader);
    // Vanilla doesn't have parent factions with their own rep
    // aggregation, so a header never has rep. Always false.
    Game::Lua::SetFieldBool(L, "isHeaderWithRep", false);
    Game::Lua::SetFieldBool(L, "isCollapsed", d.isCollapsed);
    Game::Lua::SetFieldBool(L, "isWatched", d.isWatched);
    // canSetInactive: vanilla has the SetFactionInactive /
    // SetFactionActive Lua surface and the engine's underlying
    // SetInactiveFlag at `0x004D60F0`. It accepts any real factionID
    // (not pseudo-rows, not headers), so the predicate is "this
    // faction has a real rep slot and isn't a category header".
    Game::Lua::SetFieldBool(L, "canSetInactive",
                            !d.isHeader && d.repListIndex >= 0);
    // Modern flags with no vanilla source — stubbed for API parity.
    // isChild (Cataclysm+), hasBonusRepGain (MoP+), isAccountWide
    // (Dragonflight+).
    Game::Lua::SetFieldBool(L, "isChild", false);
    Game::Lua::SetFieldBool(L, "hasBonusRepGain", false);
    Game::Lua::SetFieldBool(L, "isAccountWide", false);
}

} // namespace

static int __fastcall Script_GetFactionIDByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetFactionIDByIndex(factionIndex)");
        return 0;
    }
    const int idx = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (idx < 0)
        return 0; // nil for non-positive indices

    // Match Script_GetFactionInfo's acceptance: we bound against
    // `[VAR_FACTION_VISIBLE_MAX_INDEX]` (the same value the resolver
    // checks internally), which is wider than `GetNumFactions()` because
    // it also covers the "Inactive" / collapsed-category rows.
    const int maxIdx = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_FACTION_VISIBLE_MAX_INDEX));
    if (idx > maxIdx)
        return 0; // nil for out-of-range

    // Resolver returns the factionID for real entries; for header /
    // category rows it returns 0 ("Other") or -1 ("Inactive"-style
    // pseudo-row). Modern WoW (Classic Era 1.15.x) normalizes both to 0
    // in the `factionID` slot of `GetFactionInfo`, so we do the same.
    // Final convention: 0 for any header, nil for OOB, real factionID
    // for real factions. Matches GetQuestIDForLogIndex's headers-are-0
    // convention and modern WoW's GetFactionInfo[14] semantics.
    const int factionID = Resolver()(idx);
    Game::Lua::PushNumber(L, static_cast<double>(factionID > 0 ? factionID : 0));
    return 1;
}

// `GetFactionInfoByID(factionID)` — vanilla-positional 11-tuple
// matching `GetFactionInfo(index)`'s return shape. Built engine-direct
// via `ReadFactionData` — no Lua-side round-trip through
// `Script_GetFactionInfo`.
//
// 11th return matches what `Script_GetFactionInfo` pushes (the engine
// pushes 1.0 when the faction is the currently-watched one and nil
// otherwise — i.e. `isWatched`, not `hasRep` as some older docs
// describe it). For unencountered factions this is always nil.
static int __fastcall Script_GetFactionInfoByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetFactionInfoByID(factionID)");
        return 0;
    }
    const int factionID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    FactionData d;
    if (!ReadFactionData(factionID, &d))
        return 0;
    if (d.name == nullptr || *d.name == '\0')
        return 0;

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushString(L, d.name);                              // 1
    Game::Lua::PushString(L, d.description);                       // 2
    Game::Lua::PushNumber(L, static_cast<double>(d.reaction));     // 3
    Game::Lua::PushNumber(L,
        static_cast<double>(d.currentReactionThreshold));          // 4
    Game::Lua::PushNumber(L,
        static_cast<double>(d.nextReactionThreshold));             // 5
    Game::Lua::PushNumber(L,
        static_cast<double>(d.currentStanding));                   // 6
    PushFlag(L, d.atWarWith);                                       // 7
    PushFlag(L, d.canToggleAtWar);                                  // 8
    PushFlag(L, d.isHeader);                                        // 9
    PushFlag(L, d.isCollapsed);                                     // 10
    PushFlag(L, d.isWatched);                                       // 11
    return 11;
}

// `GetFactionParentID(factionID)` — returns the parent factionID for a
// faction in a hierarchy (e.g. Stormwind's parent is Alliance Forces;
// The Defilers's parent is Horde Forces). Returns `0` if the faction
// is top-level (no parent), or `nil` if the factionID is invalid.
//
// Reads `Faction.dbc` `ParentFactionID` at `+0x48`. Modern WoW returns
// this as the 13th value of `GetFactionInfoByID`; we expose it as its
// own getter since 1.12's `GetFactionInfo` doesn't have the slot.
static int __fastcall Script_GetFactionParentID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetFactionParentID(factionID)");
        return 0;
    }
    const int factionID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const uint8_t *record = FactionRecord(factionID);
    if (record == nullptr)
        return 0;
    const int parent = *reinterpret_cast<const int *>(
        record + Offsets::OFF_FACTION_PARENT_ID);
    Game::Lua::PushNumber(L, static_cast<double>(parent));
    return 1;
}

// Returns the rep-slot pointer for `repListID`, or nullptr if out of
// range. Slot layout is documented at `VAR_PLAYER_REP_SLOTS` in
// `Offsets.h`. Used by `GetFactionStandings`'s direct slot-array walk.
const uint8_t *RepSlot(int repListID) {
    if (repListID < 0 || repListID >= Offsets::MAX_REP_SLOTS)
        return nullptr;
    return reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_REP_SLOTS) +
        static_cast<uintptr_t>(repListID) * Offsets::REP_SLOT_STRIDE);
}

// `C_Reputation.GetFactionStandings()` — returns a flat
// `{ [factionID] = currentStanding }` table covering every faction
// in the player's reputation list (i.e. every populated rep slot).
//
// `currentStanding` is `base + delta` from the rep slot — same value
// `FUN_REPUTATION_GET_STANDING` returns, same as the `barValue` /
// `currentStanding` field in `GetFactionInfo` / `GetWatchedFactionData`.
//
// Unlike a displayed-list walk (`GetNumFactions` + `GetFactionInfo`),
// this skips the header rows entirely and doesn't depend on the
// player having opened the reputation pane recently — it reads
// straight out of the per-faction rep-slot array, which the engine
// keeps populated for every faction the player has rep with.
//
// Always returns a table (possibly empty); never nil.
static int __fastcall Script_C_Reputation_GetFactionStandings(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    for (int i = 0; i < Offsets::MAX_REP_SLOTS; i++) {
        const uint8_t *slot = RepSlot(i);
        if (slot == nullptr)
            continue;
        const int factionID = *reinterpret_cast<const int *>(
            slot + Offsets::OFF_REP_SLOT_FACTION_ID);
        if (factionID <= 0)
            continue;
        const int base = *reinterpret_cast<const int *>(
            slot + Offsets::OFF_REP_SLOT_BASE_STANDING);
        const int delta = *reinterpret_cast<const int *>(
            slot + Offsets::OFF_REP_SLOT_DELTA_STANDING);
        Game::Lua::PushNumber(L, static_cast<double>(factionID));
        Game::Lua::PushNumber(L, static_cast<double>(base + delta));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// `C_Reputation.GetWatchedFactionData()` — modern-style table for
// the faction shown above the XP bar, or nil when no faction is
// watched. Engine-direct: resolves the watched repListID off the
// player, finds the slot's factionID, and runs the shared
// `ReadFactionData` chain. `isWatched` is forced true since this
// IS the watched faction by definition.
static int __fastcall Script_C_Reputation_GetWatchedFactionData(void *L) {
    const uint8_t *info = PlayerInfo();
    if (info == nullptr)
        return 0;
    const uint8_t *slot = RepSlot(WatchedRepListID(info));
    if (slot == nullptr)
        return 0;
    const int factionID = *reinterpret_cast<const int *>(
        slot + Offsets::OFF_REP_SLOT_FACTION_ID);

    FactionData d;
    if (!ReadFactionData(factionID, &d))
        return 0;
    d.isWatched = true;

    Game::Lua::SetTop(L, 0);
    PushFactionDataTable(L, d);
    return 1;
}

// `C_Reputation.GetFactionDataByID(factionID)` — modern table-shaped
// accessor keyed by faction ID rather than by displayed-list position.
// Returns nil when the ID has no `Faction.dbc` record.
//
// Unlike the index form, this doesn't need the faction to be in the
// player's displayed reputation list: `ReadFactionData` fills an
// unencountered faction cleanly (currentStanding 0, not at war), so a
// lookup works for any real faction.
static int __fastcall Script_C_Reputation_GetFactionDataByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Reputation.GetFactionDataByID(factionID)");
        return 0;
    }
    const int factionID = static_cast<int>(Game::Lua::ToNumber(L, 1));

    FactionData d;
    if (!ReadFactionData(factionID, &d))
        return 0; // nil — no such faction

    Game::Lua::SetTop(L, 0);
    PushFactionDataTable(L, d);
    return 1;
}

// `C_Reputation.GetFactionDataByIndex(factionSortIndex)` — modern
// table-shaped accessor over the displayed reputation list. 1-based
// index covering the same range as vanilla's `GetFactionInfo(index)`
// (real factions + category header rows). Returns nil for OOB or
// for the "Other"/"Inactive" pseudo-rows that don't have a
// `Faction.dbc` record.
//
// Engine-direct: resolves index → factionID, then runs the shared
// `ReadFactionData` chain (no Lua round-trip through
// `Script_GetFactionInfo`).
static int __fastcall Script_C_Reputation_GetFactionDataByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Reputation.GetFactionDataByIndex(factionSortIndex)");
        return 0;
    }
    const int idx = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (idx < 0)
        return 0;
    const int maxIdx = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_FACTION_VISIBLE_MAX_INDEX));
    if (idx > maxIdx)
        return 0;

    FactionData d;
    if (!ReadFactionData(Resolver()(idx), &d))
        return 0;

    Game::Lua::SetTop(L, 0);
    PushFactionDataTable(L, d);
    return 1;
}

// `C_Reputation.ToggleFactionAtWarByID(factionID)` — ClassicAPI
// extension. Flips a faction's at-war state by ID rather than by
// displayed-list position.
//
// Mirrors the stock `FactionToggleAtWar(index)` body exactly, minus the
// index resolve: read the current at-war flag, negate it, hand it to the
// engine's own setter. Every rule therefore stays with the engine —
// refusing to make peace below -3000 standing, honouring the
// peace-forced flag, the loot-session bail, and sending
// `CMSG_SET_FACTION_ATWAR`. Going through the engine's setter also means
// `Faction::UnitFactionPolyfill`'s hook on it fires `UNIT_FACTION`
// ("any future C++ caller of the setter inherits the polyfill"), and its
// change-detection keeps that silent when the engine refuses the toggle.
//
// Non-positive IDs are ignored (the stock path likewise skips
// factionID 0, which is what its resolver returns for header rows).
static int __fastcall Script_C_Reputation_ToggleFactionAtWarByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Reputation.ToggleFactionAtWarByID(factionID)");
        return 0;
    }
    const int factionID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (factionID <= 0)
        return 0;

    // `newState` is declared `int`, not `char`, to match the detour
    // `Faction::UnitFactionPolyfill` installs on this setter: a `char`
    // argument only defines the low byte of EDX, leaving the detour's
    // `int` parameter with undefined high bits.
    using GetAtWar_t = unsigned int(__fastcall *)(int factionID);
    using SetAtWar_t = void(__fastcall *)(int factionID, int newState);
    auto getAtWar = reinterpret_cast<GetAtWar_t>(
        static_cast<uintptr_t>(Offsets::FUN_FACTION_GET_AT_WAR));
    auto setAtWar = reinterpret_cast<SetAtWar_t>(
        static_cast<uintptr_t>(Offsets::FUN_FACTION_SET_AT_WAR));

    setAtWar(factionID, (getAtWar(factionID) != 0) ? 0 : 1);
    return 0;
}

// `factionID` → its 0..63 reputation-slot index, or -1 when the faction
// has no slot (no record, or a header category). Same `Faction.dbc`
// `RepListIndex` field `ReadFactionData` reads.
static int RepSlotForFaction(int factionID) {
    if (factionID <= 0)
        return -1;
    const uint8_t *record = FactionRecord(factionID);
    if (record == nullptr)
        return -1;
    const int repListIndex = *reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_FACTION_REP_LIST_INDEX);
    if (repListIndex < 0 || repListIndex >= Offsets::MAX_REP_SLOTS)
        return -1;
    return repListIndex;
}

// Whether a reputation slot carries the INACTIVE flag.
static bool RepSlotIsInactive(int repSlot) {
    auto *slot = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_REP_SLOTS) +
        static_cast<uintptr_t>(repSlot) * Offsets::REP_SLOT_STRIDE);
    return (*(slot + Offsets::OFF_REP_SLOT_FLAGS) &
            Offsets::REP_SLOT_FLAG_INACTIVE) != 0;
}

// `C_Reputation.IsFactionActive(factionSortIndex)` — whether the faction
// at a 1-based displayed-list position is NOT filed under "Inactive".
//
// Runs the same chain as the engine's own inactive check (resolve the
// index to a faction id, then read the slot's INACTIVE bit) and returns
// the inverse as a real boolean. A position that names no faction — out
// of range, or a category header — reports `false`.
static int __fastcall Script_C_Reputation_IsFactionActive(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Reputation.IsFactionActive(factionSortIndex)");
        return 0;
    }
    const int idx = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    const int maxIdx = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_FACTION_VISIBLE_MAX_INDEX));

    const int factionID = (idx >= 0 && idx <= maxIdx) ? Resolver()(idx) : 0;
    const int repSlot = RepSlotForFaction(factionID);

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushBool(L, repSlot >= 0 && !RepSlotIsInactive(repSlot));
    return 1;
}

// `C_Reputation.IsFactionActiveByID(factionID)` — ClassicAPI extension.
// The same answer keyed by faction id. Reports `false` for a faction
// with no reputation slot.
static int __fastcall Script_C_Reputation_IsFactionActiveByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Reputation.IsFactionActiveByID(factionID)");
        return 0;
    }
    const int repSlot =
        RepSlotForFaction(static_cast<int>(Game::Lua::ToNumber(L, 1)));

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushBool(L, repSlot >= 0 && !RepSlotIsInactive(repSlot));
    return 1;
}

// Shared body for the two by-ID inactive setters below. Hands the id to
// the engine's own inactive setter, which flips the rep slot's INACTIVE
// bit, tells the server, and rebuilds the displayed faction list so the
// faction moves into or out of the "Inactive" category. That setter is
// also what `Faction::UnitFactionPolyfill` hooks, so `UNIT_FACTION`
// fires when the flag actually changes.
//
// `newState` is `int` rather than `char` to match that detour's
// signature — see the note in ToggleFactionAtWarByID.
static int SetFactionInactiveByID(void *L, int newState, const char *usage) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, usage);
        return 0;
    }
    const int factionID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (factionID <= 0)
        return 0;

    using SetInactive_t = void(__fastcall *)(int factionID, int newState);
    auto fn = reinterpret_cast<SetInactive_t>(
        static_cast<uintptr_t>(Offsets::FUN_FACTION_SET_INACTIVE));
    fn(factionID, newState);
    return 0;
}

// `C_Reputation.SetFactionInactiveByID(factionID)` — ClassicAPI
// extension. Moves a faction into the "Inactive" category by ID rather
// than by displayed-list position.
static int __fastcall Script_C_Reputation_SetFactionInactiveByID(void *L) {
    return SetFactionInactiveByID(
        L, 1, "Usage: C_Reputation.SetFactionInactiveByID(factionID)");
}

// `C_Reputation.SetFactionActiveByID(factionID)` — the inverse: takes a
// faction back out of the "Inactive" category.
static int __fastcall Script_C_Reputation_SetFactionActiveByID(void *L) {
    return SetFactionInactiveByID(
        L, 0, "Usage: C_Reputation.SetFactionActiveByID(factionID)");
}

// `C_Reputation.SetSelectedFactionByID(factionID)` — ClassicAPI
// extension. Selects a faction in the reputation pane by ID rather
// than by displayed-list position, the same convenience
// `SetWatchedFactionByID` gives over `SetWatchedFactionIndex`.
//
// The engine already stores the selection AS a factionID
// (`VAR_FACTION_SELECTED_ID`) — the stock index form only resolves its
// argument to an id before storing it — so this writes the id straight
// through, with no list walk and no dependence on the faction being
// currently listed. `GetSelectedFaction()` maps it back to an index,
// reporting 0 while the faction isn't in the displayed list.
//
// Negative IDs are ignored; 0 clears the selection.
static int __fastcall Script_C_Reputation_SetSelectedFactionByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Reputation.SetSelectedFactionByID(factionID)");
        return 0;
    }
    const int factionID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (factionID < 0)
        return 0;

    *reinterpret_cast<int32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_FACTION_SELECTED_ID)) = factionID;
    return 0;
}

// `C_Reputation.SetWatchedFactionByID(factionID)` — sets the faction
// shown above the XP bar by ID rather than by displayed-list index.
// Modern API; vanilla 1.12 only exposes `SetWatchedFactionIndex(idx)`,
// which forces addons to walk the index list themselves.
//
// Calls the engine's inner watched-faction setter at
// `FUN_PLAYER_SET_WATCHED_FACTION` directly, bypassing the
// engine's index-based wrapper (which round-trips through the
// resolver and rejects unencountered factions). Passing factionID
// 0 clears the watched faction.
//
// Negative IDs are silently ignored.
static int __fastcall Script_C_Reputation_SetWatchedFactionByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Reputation.SetWatchedFactionByID(factionID)");
        return 0;
    }
    const int factionID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (factionID < 0)
        return 0;

    using SetWatched_t = void(__fastcall *)(int factionID);
    auto fn = reinterpret_cast<SetWatched_t>(
        Offsets::FUN_PLAYER_SET_WATCHED_FACTION);
    fn(factionID);
    return 0;
}

const Game::Doc::Field kFactionDataFields[] = {
    Game::Doc::Req("factionID", "number"),
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("description", "string", "Empty when the faction has none."),
    Game::Doc::Req("reaction", "luaIndex", "The rank, from 1 for Hated to 8 for Exalted."),
    Game::Doc::Req("currentReactionThreshold", "number",
                   "The standing at the bottom of the current rank."),
    Game::Doc::Req("nextReactionThreshold", "number",
                   "The standing at the top of the current rank."),
    Game::Doc::Req("currentStanding", "number"),
    Game::Doc::Req("atWarWith", "bool"),
    Game::Doc::Req("canToggleAtWar", "bool",
                   "False below -3000 standing, when the war state is fixed, or with no "
                   "reputation slot."),
    Game::Doc::Req("isChild", "bool", "Always false."),
    Game::Doc::Req("isHeader", "bool", "True when the faction is a header in the reputation list."),
    Game::Doc::Req("isHeaderWithRep", "bool", "Always false."),
    Game::Doc::Req("isCollapsed", "bool", "True for a collapsed header."),
    Game::Doc::Req("isWatched", "bool", "True when the faction is shown above the experience bar."),
    Game::Doc::Req("hasBonusRepGain", "bool", "Always false."),
    Game::Doc::Req("canSetInactive", "bool",
                   "True when the faction has a reputation slot and is not a header."),
    Game::Doc::Req("isAccountWide", "bool", "Always false."),
};
const Game::Doc::Structure kFactionData{"FactionData", "Reputation", kFactionDataFields,
                                        "A faction's reputation data."};

const Game::Doc::Field kFactionIDArgs[] = {
    Game::Doc::Req("factionID", "number"),
};
const Game::Doc::Field kSortIndexArgs[] = {
    Game::Doc::Req("factionSortIndex", "luaIndex", "A position in the reputation list."),
};

const Game::Doc::Field kFactionIDByIndexArgs[] = {
    Game::Doc::Req("factionIndex", "luaIndex", "A position in the reputation list."),
};
const Game::Doc::Field kFactionIDByIndexRets[] = {
    Game::Doc::Opt("factionID", "number", nullptr,
                   "0 for a header with no faction of its own, such as Other or Inactive; nil "
                   "when the index is out of range."),
};
const Game::Doc::Function kGetFactionIDByIndex{
    "Returns the faction ID of the entry at a position in the reputation list.",
    kFactionIDByIndexArgs, kFactionIDByIndexRets, "ReputationGlobals", true};

const Game::Doc::Field kFactionInfoByIDRets[] = {
    Game::Doc::Opt("name", "string", nullptr, "Nil, with the rest, when no faction has the ID."),
    Game::Doc::Opt("description", "string", nullptr, "Empty when the faction has none."),
    Game::Doc::Opt("standingID", "number", nullptr, "The rank, from 1 for Hated to 8 for Exalted."),
    Game::Doc::Opt("barMin", "number", nullptr, "The standing at the bottom of the current rank."),
    Game::Doc::Opt("barMax", "number", nullptr, "The standing at the top of the current rank."),
    Game::Doc::Opt("barValue", "number", nullptr, "The current standing."),
    Game::Doc::Opt("atWarWith", "number", nullptr,
                   "1 when the player is at war with the faction, else nil."),
    Game::Doc::Opt("canToggleAtWar", "number", nullptr,
                   "1 when the at-war state can be changed, else nil."),
    Game::Doc::Opt("isHeader", "number", nullptr,
                   "1 when the faction is a header in the reputation list, else nil."),
    Game::Doc::Opt("isCollapsed", "number", nullptr, "1 for a collapsed header, else nil."),
    Game::Doc::Opt("isWatched", "number", nullptr,
                   "1 when the faction is shown above the experience bar, else nil."),
};
const Game::Doc::Function kGetFactionInfoByID{
    "Returns the same values as GetFactionInfo for a faction ID, whether or not the faction "
    "is in the reputation list.",
    kFactionIDArgs, kFactionInfoByIDRets, "ReputationGlobals"};

const Game::Doc::Field kFactionParentIDRets[] = {
    Game::Doc::Opt("parentFactionID", "number", nullptr,
                   "0 for a faction with no parent; nil when no faction has the ID."),
};
const Game::Doc::Function kGetFactionParentID{
    "Returns the ID of a faction's parent faction, such as Alliance for Stormwind.",
    kFactionIDArgs, kFactionParentIDRets, "ReputationGlobals", true};

const Game::Doc::Field kSetWatchedArgs[] = {
    Game::Doc::Req("factionID", "number",
                   "0, or an ID with no reputation slot, clears the watched faction; a negative "
                   "ID does nothing."),
};
const Game::Doc::Function kSetWatchedFactionByID{
    "Sets the faction shown above the experience bar.", kSetWatchedArgs, {}};

const Game::Doc::Field kSetSelectedByIDArgs[] = {
    Game::Doc::Req("factionID", "number", "0 clears the selection; a negative ID does nothing."),
};
const Game::Doc::Function kSetSelectedFactionByID{
    "Selects a faction in the reputation pane by ID, even when the faction is not listed.",
    kSetSelectedByIDArgs, {}, nullptr, true};

const Game::Doc::Function kToggleFactionAtWarByID{
    "Toggles whether the player is at war with a faction, by ID; the same limits apply as "
    "for C_Reputation.ToggleFactionAtWar.",
    kFactionIDArgs, {}, nullptr, true};

const Game::Doc::Field kIsActiveRets[] = {
    Game::Doc::Req("isActive", "bool",
                   "False under the Inactive header, and for anything with no reputation slot."),
};
const Game::Doc::Function kIsFactionActive{
    "Returns whether the faction at a position in the reputation list is active, that is, "
    "not under the Inactive header.",
    kSortIndexArgs, kIsActiveRets};
const Game::Doc::Function kIsFactionActiveByID{
    "Returns whether a faction is active, that is, not under the Inactive header.",
    kFactionIDArgs, kIsActiveRets, nullptr, true};

const Game::Doc::Field kIsInactiveRets[] = {
    Game::Doc::Opt("isInactive", "number", nullptr,
                   "1 when the faction is under the Inactive header, else nil."),
};
const Game::Doc::Function kIsFactionInactive{
    "Returns whether the faction at a position in the reputation list is under the Inactive "
    "header, as 1 or nil.",
    kSortIndexArgs, kIsInactiveRets, nullptr, true};

const Game::Doc::Function kSetFactionInactiveByID{
    "Moves a faction under the Inactive header of the reputation list, by ID.",
    kFactionIDArgs, {}, nullptr, true};
const Game::Doc::Function kSetFactionActiveByID{
    "Moves a faction out from under the Inactive header of the reputation list, by ID.",
    kFactionIDArgs, {}, nullptr, true};
const Game::Doc::Function kSetFactionInactive{
    "Moves the faction at a position in the reputation list under the Inactive header.",
    kSortIndexArgs, {}, nullptr, true};
const Game::Doc::Function kSetFactionActive{
    "Moves the faction at a position in the reputation list out from under the Inactive "
    "header.",
    kSortIndexArgs, {}};

const Game::Doc::Function kToggleFactionAtWar{
    "Toggles whether the player is at war with the faction at a position in the reputation "
    "list.",
    kSortIndexArgs, {}};
const Game::Doc::Function kSetSelectedFaction{
    "Selects the faction at a position in the reputation list.", kSortIndexArgs, {}};

const Game::Doc::Field kWatchedFactionDataRets[] = {
    Game::Doc::Opt("watchedFactionData", "FactionData", nullptr,
                   "Nil when no faction is watched."),
};
const Game::Doc::Function kGetWatchedFactionData{
    "Returns the faction shown above the experience bar.", {}, kWatchedFactionDataRets};

const Game::Doc::Field kStandingsRets[] = {
    Game::Doc::Req("standings", "table",
                   "Keyed by faction ID; each value is the faction's standing."),
};
const Game::Doc::Function kGetFactionStandings{
    "Returns the standing of every faction the character has reputation with, keyed by "
    "faction ID.",
    {}, kStandingsRets, nullptr, true};

const Game::Doc::Field kFactionDataByIDRets[] = {
    Game::Doc::Opt("factionData", "FactionData", nullptr, "Nil when no faction has the ID."),
};
const Game::Doc::Function kGetFactionDataByID{
    "Returns a faction's reputation data by ID, whether or not the faction is in the "
    "reputation list.",
    kFactionIDArgs, kFactionDataByIDRets};

const Game::Doc::Field kFactionDataByIndexRets[] = {
    Game::Doc::Opt("factionData", "FactionData", nullptr,
                   "Nil when the index is out of range or names the Other or Inactive header."),
};
const Game::Doc::Function kGetFactionDataByIndex{
    "Returns the reputation data of the faction at a position in the reputation list.",
    kSortIndexArgs, kFactionDataByIndexRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetFactionIDByIndex", &Script_GetFactionIDByIndex,
                                      &kGetFactionIDByIndex);
    Game::Lua::RegisterGlobalFunction("GetFactionInfoByID", &Script_GetFactionInfoByID,
                                      &kGetFactionInfoByID);
    Game::Lua::RegisterGlobalFunction("GetFactionParentID", &Script_GetFactionParentID,
                                      &kGetFactionParentID);
    Game::Lua::RegisterTableFunction("C_Reputation", "SetWatchedFactionByID",
                                     &Script_C_Reputation_SetWatchedFactionByID,
                                     &kSetWatchedFactionByID);
    Game::Lua::RegisterTableFunction("C_Reputation", "SetSelectedFactionByID",
                                     &Script_C_Reputation_SetSelectedFactionByID,
                                     &kSetSelectedFactionByID);
    Game::Lua::RegisterTableFunction("C_Reputation", "ToggleFactionAtWarByID",
                                     &Script_C_Reputation_ToggleFactionAtWarByID,
                                     &kToggleFactionAtWarByID);
    Game::Lua::RegisterTableFunction("C_Reputation", "IsFactionActive",
                                     &Script_C_Reputation_IsFactionActive, &kIsFactionActive);
    Game::Lua::RegisterTableFunction("C_Reputation", "IsFactionActiveByID",
                                     &Script_C_Reputation_IsFactionActiveByID,
                                     &kIsFactionActiveByID);
    Game::Lua::RegisterTableFunction(
        "C_Reputation", "IsFactionInactive",
        reinterpret_cast<Game::Lua::CFunction>(
            static_cast<uintptr_t>(Offsets::FUN_SCRIPT_IS_FACTION_INACTIVE)),
        &kIsFactionInactive);
    Game::Lua::RegisterTableFunction("C_Reputation", "SetFactionInactiveByID",
                                     &Script_C_Reputation_SetFactionInactiveByID,
                                     &kSetFactionInactiveByID);
    Game::Lua::RegisterTableFunction("C_Reputation", "SetFactionActiveByID",
                                     &Script_C_Reputation_SetFactionActiveByID,
                                     &kSetFactionActiveByID);
    Game::Lua::RegisterTableFunction(
        "C_Reputation", "SetFactionInactive",
        reinterpret_cast<Game::Lua::CFunction>(
            static_cast<uintptr_t>(Offsets::FUN_SCRIPT_SET_FACTION_INACTIVE)),
        &kSetFactionInactive);
    Game::Lua::RegisterTableFunction(
        "C_Reputation", "SetFactionActive",
        reinterpret_cast<Game::Lua::CFunction>(
            static_cast<uintptr_t>(Offsets::FUN_SCRIPT_SET_FACTION_ACTIVE)),
        &kSetFactionActive);
    Game::Lua::RegisterTableFunction(
        "C_Reputation", "ToggleFactionAtWar",
        reinterpret_cast<Game::Lua::CFunction>(
            static_cast<uintptr_t>(Offsets::FUN_SCRIPT_FACTION_TOGGLE_AT_WAR)),
        &kToggleFactionAtWar);
    // The stock global, mirrored into the namespace — the engine's own
    // handler already has the Lua C ABI, so it registers as-is.
    Game::Lua::RegisterTableFunction(
        "C_Reputation", "SetSelectedFaction",
        reinterpret_cast<Game::Lua::CFunction>(
            static_cast<uintptr_t>(Offsets::FUN_SCRIPT_SET_SELECTED_FACTION)),
        &kSetSelectedFaction);
    Game::Lua::RegisterTableFunction("C_Reputation", "GetWatchedFactionData",
                                     &Script_C_Reputation_GetWatchedFactionData,
                                     &kGetWatchedFactionData);
    Game::Lua::RegisterTableFunction("C_Reputation", "GetFactionStandings",
                                     &Script_C_Reputation_GetFactionStandings,
                                     &kGetFactionStandings);
    Game::Lua::RegisterTableFunction("C_Reputation", "GetFactionDataByID",
                                     &Script_C_Reputation_GetFactionDataByID,
                                     &kGetFactionDataByID);
    Game::Lua::RegisterTableFunction("C_Reputation", "GetFactionDataByIndex",
                                     &Script_C_Reputation_GetFactionDataByIndex,
                                     &kGetFactionDataByIndex);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Faction::Info
