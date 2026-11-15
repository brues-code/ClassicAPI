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

// Restores `UNIT_FACTION("player")` event semantics that vanilla 1.12.1
// silently dropped from four engine paths. In 3.3.5 each of these calls
// `FUN_0071F8F0(player, 0) → FUN_0060BF10(playerGUID, UNIT_FACTION_id)`
// which broadcasts the event for every unit token referencing the
// local player; vanilla just returns. We hook each path and fire
// `UNIT_FACTION("player")` post-original via the engine's printf-style
// dispatcher.
//
// Why one file and not four: every hook does the same shape work
// (snapshot rep-slot state, run original, compare, fire). The shared
// `LookupEngineEvent` + `FireUnitFactionPlayer` helpers are the value;
// splitting four 30-line hooks into four files would just spread that
// helper around.
//
// Why hook the inner setters (vs. `Script_*` wrappers): same coverage,
// and any future C++ caller of the setter inherits the polyfill.
//
// Coverage map:
//
//   1. `FUN_FACTION_SET_AT_WAR` — `FactionToggleAtWar` toggles
//      AT_WAR. Hook fires UNIT_FACTION if the flag byte changed.
//
//   2. `FUN_FACTION_SET_INACTIVE` — `SetFactionInactive` /
//      `SetFactionActive` toggle INACTIVE. Same change-detection
//      shape as at-war.
//
//   3. `FUN_SMSG_SET_FACTION_STANDINGS` — server rep update.
//      The handler mutates AT_WAR / VISIBLE bits as standing crosses
//      thresholds (recovery from below-Hated clears the force-set
//      AT_WAR). Snapshots the 64-entry flag-byte array before/after
//      and fires once if any byte changed.
//
//   4. `FUN_SMSG_INITIALIZE_FACTIONS` — server initial sync at login.
//      Fires unconditionally after the engine populates the rep table.
//      Lets addons that observe UNIT_FACTION (rather than
//      PLAYER_LOGIN / VARIABLES_LOADED) see the initial state.
//
//   5. `FUN_SMSG_SET_FACTION_ATWAR` — server force-change of AT_WAR
//      (e.g., faction reset, server-side state change). Fires
//      unconditionally after the engine writes the slot byte, matching
//      3.3.5's behavior — server-pushed packets are infrequent and
//      always indicate a state change worth signaling.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "event/Custom.h"

#include <cstdint>
#include <cstring>

namespace Faction::UnitFactionPolyfill {

namespace {

// Helpers ────────────────────────────────────────────────────────────

void FireUnitFactionPlayer() {
    Event::Custom::Fire(Offsets::EVENT_UNIT_FACTION, "%s", "player");
}

uint8_t ReadRepSlotFlags(int repListID) {
    return *reinterpret_cast<uint8_t *>(
        Offsets::VAR_PLAYER_REP_SLOTS +
        repListID * Offsets::REP_SLOT_STRIDE +
        Offsets::OFF_REP_SLOT_FLAGS);
}

// Resolves `factionID → repListID` (0..63 rep-slot index in
// `VAR_PLAYER_REP_SLOTS`) by reading the `+0x04` field on the
// `Faction.dbc` record directly. Same value the engine's helper at
// `0x004D5600` returns — it just dereferences the same DBC field after
// its own bounds check. Returns -1 for factionIDs that don't resolve
// to a record (out of range / empty slot) or are header categories
// (record stores -1 in the field).
int FactionIDToRepListID(int factionID) {
    if (factionID <= 0)
        return -1;
    const auto *record = DBC::Record(Offsets::VAR_FACTION_DBC_RECORDS,
                                     Offsets::VAR_FACTION_DBC_COUNT,
                                     static_cast<uint32_t>(factionID));
    if (record == nullptr)
        return -1;
    return *reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_FACTION_REP_LIST_INDEX);
}

// Hook 1 — at-war setter ──────────────────────────────────────────────

using SetAtWar_t = void(__fastcall *)(int factionID, int newState);
SetAtWar_t s_setAtWar_o = nullptr;

void __fastcall SetAtWar_h(int factionID, int newState) {
    const int repListID = FactionIDToRepListID(factionID);
    const bool slotValid = repListID >= 0 && repListID < Offsets::MAX_REP_SLOTS;
    const uint8_t beforeFlags = slotValid ? ReadRepSlotFlags(repListID) : 0;

    s_setAtWar_o(factionID, newState);

    if (!slotValid)
        return;
    if (ReadRepSlotFlags(repListID) == beforeFlags)
        return; // engine bailed (looting / PEACE_FORCED no-op / standing block)

    FireUnitFactionPlayer();
}

const Game::HookAutoRegister _hookAtWar{
    Offsets::FUN_FACTION_SET_AT_WAR,
    reinterpret_cast<void *>(&SetAtWar_h),
    reinterpret_cast<void **>(&s_setAtWar_o)};

// Hook 2 — inactive setter ────────────────────────────────────────────

using SetInactive_t = void(__fastcall *)(int factionID, int newState);
SetInactive_t s_setInactive_o = nullptr;

void __fastcall SetInactive_h(int factionID, int newState) {
    const int repListID = FactionIDToRepListID(factionID);
    const bool slotValid = repListID >= 0 && repListID < Offsets::MAX_REP_SLOTS;
    const uint8_t beforeFlags = slotValid ? ReadRepSlotFlags(repListID) : 0;

    s_setInactive_o(factionID, newState);

    if (!slotValid)
        return;
    if (ReadRepSlotFlags(repListID) == beforeFlags)
        return; // no-op (already in requested state)

    FireUnitFactionPlayer();
}

const Game::HookAutoRegister _hookInactive{
    Offsets::FUN_FACTION_SET_INACTIVE,
    reinterpret_cast<void *>(&SetInactive_h),
    reinterpret_cast<void **>(&s_setInactive_o)};

// Hook 3 — SMSG_SET_FACTION_STANDINGS ─────────────────────────────────

// Snapshot all 64 rep-slot flag bytes into a packed buffer. The engine's
// per-rep-change handler mutates AT_WAR / VISIBLE bits as standing
// crosses thresholds, so we need a delta-detect across the whole table
// (single rep change may flip flags on an unrelated slot — and the SMSG
// can carry multiple rep updates in one packet).
struct RepFlagsSnapshot {
    uint8_t flags[Offsets::MAX_REP_SLOTS];
};

void TakeSnapshot(RepFlagsSnapshot *out) {
    for (int i = 0; i < Offsets::MAX_REP_SLOTS; ++i)
        out->flags[i] = ReadRepSlotFlags(i);
}

bool SnapshotsDiffer(const RepFlagsSnapshot *a, const RepFlagsSnapshot *b) {
    return std::memcmp(a->flags, b->flags, sizeof(a->flags)) != 0;
}

using SmsgHandler_t = int(__stdcall *)(uint32_t opcode, void *packet);

SmsgHandler_t s_smsgSetStandings_o = nullptr;

int __stdcall SmsgSetStandings_h(uint32_t opcode, void *packet) {
    RepFlagsSnapshot before;
    TakeSnapshot(&before);

    const int result = s_smsgSetStandings_o(opcode, packet);

    RepFlagsSnapshot after;
    TakeSnapshot(&after);
    if (SnapshotsDiffer(&before, &after))
        FireUnitFactionPlayer();

    return result;
}

const Game::HookAutoRegister _hookSetStandings{
    Offsets::FUN_SMSG_SET_FACTION_STANDINGS,
    reinterpret_cast<void *>(&SmsgSetStandings_h),
    reinterpret_cast<void **>(&s_smsgSetStandings_o)};

// Hook 4 — SMSG_INITIALIZE_FACTIONS ──────────────────────────────────

SmsgHandler_t s_smsgInitFactions_o = nullptr;

int __stdcall SmsgInitFactions_h(uint32_t opcode, void *packet) {
    const int result = s_smsgInitFactions_o(opcode, packet);
    // Initial sync — engine has just populated all 64 rep slots from
    // server state. Always fire so addons listening for UNIT_FACTION
    // (rather than PLAYER_LOGIN / VARIABLES_LOADED) see the initial
    // load. Modern WoW's behavior.
    FireUnitFactionPlayer();
    return result;
}

const Game::HookAutoRegister _hookInitFactions{
    Offsets::FUN_SMSG_INITIALIZE_FACTIONS,
    reinterpret_cast<void *>(&SmsgInitFactions_h),
    reinterpret_cast<void **>(&s_smsgInitFactions_o)};

// Hook 5 — SMSG_SET_FACTION_ATWAR ─────────────────────────────────────

SmsgHandler_t s_smsgSetAtWar_o = nullptr;

int __stdcall SmsgSetAtWar_h(uint32_t opcode, void *packet) {
    const int result = s_smsgSetAtWar_o(opcode, packet);
    // Server-pushed AT_WAR change — packet is rare and always carries
    // intent to update. Match 3.3.5's behavior: fire unconditionally
    // after the byte write, without delta-detecting against the slot
    // (the engine writes the byte regardless of whether it differs, so
    // a delta check would conflate "value didn't change" with "server
    // had nothing to say" — but the server only sends this when
    // something did).
    FireUnitFactionPlayer();
    return result;
}

const Game::HookAutoRegister _hookSetAtWar{
    Offsets::FUN_SMSG_SET_FACTION_ATWAR,
    reinterpret_cast<void *>(&SmsgSetAtWar_h),
    reinterpret_cast<void **>(&s_smsgSetAtWar_o)};

} // namespace

} // namespace Faction::UnitFactionPolyfill
