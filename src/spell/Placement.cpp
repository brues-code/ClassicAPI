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
#include "cvar/Factory.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"
#include "spell/Range.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>
#include <cstring>

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

uint32_t State() {
    return *reinterpret_cast<const uint32_t *>(Offsets::VAR_SPELL_TARGETING_FLAGS);
}

bool InWorld() {
    // Player resolves only when in-world. Same gate other modules
    // (e.g. `Item::Location::ResolveBag`) use to avoid touching
    // engine state during loading screens or glue.
    return const_cast<uint8_t *>(Unit::Identity::PlayerObject()) != nullptr;
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

// ---- 3.80.2's at-cursor range clamp (inside FUN_1411948b0) -------------------
//
// Gated by spellClampingEnable. A hit past the spell's max range is pulled
// back toward the caster along the line from the camera ray at the caster's
// depth to the hit (FUN_141195430): from max range - 0.001, a vertical trace
// 50 yd down finds the ground; no ground, or ground still out of range, steps
// back 1 yd; ground within [min + 0.001, max - 0.001] is then moved forward in
// 0.05 yd steps until it is within 0.1 yd of max; 15 steps at most. A hit
// nearer than the spell's min range, but not nearer than spellClampingMinRange
// (1.75 when 0), is pushed out to min range (FUN_141194f10). Help, category
// and defaults are 3.80.2's (FUN_1411934f0).

using ReportCastFailure_t = void(__fastcall *)(uint32_t spellID, uint32_t reason, int32_t arg3,
                                               int32_t arg4, uint32_t fromServer);
using TraceSegment_t = uint8_t(__fastcall *)(const float *start, const float *end, float *hit,
                                             float *t, uint32_t mask);

CVar::Factory::Handle g_clampEnable = nullptr;
CVar::Factory::Handle g_clampMinRange = nullptr;

float Distance(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The ground under `p`: a trace from `p` to 50 yd below it.
bool GroundBelow(const float p[3], float out[3]) {
    const float end[3] = {p[0], p[1], p[2] - 50.0f};
    float t = 1.0f;
    return reinterpret_cast<TraceSegment_t>(Offsets::FUN_TRACE_WORLD_SEGMENT)(
               p, end, out, &t, Offsets::TRACE_MASK_GROUND) != 0;
}

void ClampToRange(const float rayNear[3], float point[3]) {
    if (CVar::Factory::GetInt(g_clampEnable) == 0)
        return;
    const uint8_t *player = Unit::Identity::PlayerObject();
    float caster[3];
    if (player == nullptr || !Unit::Position::Read(const_cast<uint8_t *>(player), caster))
        return;
    float minRange = 0.0f, maxRange = 0.0f;
    Spell::Range::CasterMinMax(const_cast<uint8_t *>(player),
                               static_cast<int>(Game::Read<uint32_t>(
                                   Offsets::VAR_PENDING_CAST_SPELL)),
                               minRange, maxRange);
    if (maxRange <= 0.0f)
        return;
    const float dist = Distance(point, caster);

    if (dist > maxRange) {
        // Q: the camera ray at the caster's depth, its plane taken square to
        // the ray (3.80.2 squares it to the camera's forward).
        float ray[3] = {point[0] - rayNear[0], point[1] - rayNear[1], point[2] - rayNear[2]};
        const float len2 = ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2];
        if (len2 <= 0.0f)
            return;
        const float k = ((caster[0] - rayNear[0]) * ray[0] + (caster[1] - rayNear[1]) * ray[1] +
                         (caster[2] - rayNear[2]) * ray[2]) /
                        len2;
        const float q[3] = {rayNear[0] + ray[0] * k, rayNear[1] + ray[1] * k,
                            rayNear[2] + ray[2] * k};
        float u[3] = {point[0] - q[0], point[1] - q[1], point[2] - q[2]};
        const float ulen = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        if (ulen <= 0.0f)
            return;
        for (float &c : u)
            c /= ulen;

        auto groundAt = [&](float s, float out[3]) {
            const float p[3] = {q[0] + u[0] * s, q[1] + u[1] * s, q[2] + u[2] * s};
            return GroundBelow(p, out);
        };
        float s = maxRange - 0.001f;
        for (int step = 0; step < 15 && s > 0.0f; ++step) {
            float ground[3];
            if (!groundAt(s, ground)) {
                s -= 1.0f;
                continue;
            }
            const float d = Distance(ground, caster);
            if (d > maxRange - 0.001f) {
                s -= 1.0f;
                continue;
            }
            if (d < minRange + 0.001f)
                return;
            std::memcpy(point, ground, sizeof(ground));
            while (++step < 15 && maxRange - Distance(point, caster) > 0.1f) {
                float next[3];
                if (!groundAt(s + 0.05f, next) || Distance(next, caster) > maxRange - 0.001f)
                    break;
                s += 0.05f;
                std::memcpy(point, next, sizeof(next));
            }
            return;
        }
        return;
    }

    float floor = CVar::Factory::GetFloat(g_clampMinRange);
    if (floor == 0.0f)
        floor = 1.75f;
    if (minRange > 0.0f && dist < minRange && dist >= floor) {
        // Out along the caster-to-hit line, level, to just past min range.
        const float dx = point[0] - caster[0], dy = point[1] - caster[1];
        const float flat = std::sqrt(dx * dx + dy * dy);
        if (flat <= 0.0f)
            return;
        const float r = (minRange + 0.001f) / flat;
        const float above[3] = {caster[0] + dx * r, caster[1] + dy * r, point[2] + 25.0f};
        float ground[3];
        if (GroundBelow(above, ground))
            std::memcpy(point, ground, sizeof(ground));
    }
}

void RegisterLuaFunctions() {
    g_clampEnable = CVar::Factory::Register("spellClampingEnable",
                                            "Enable/Disable Spell Reticle Clamping",
                                            Offsets::CVAR_CATEGORY_DEBUG, "1");
    g_clampMinRange = CVar::Factory::Register(
        "spellClampingMinRange", "Set the range Min min Reticle Clamping starts at",
        Offsets::CVAR_CATEGORY_DEBUG, "0.0f");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

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

    auto *clickInfo = *reinterpret_cast<uint8_t *const *>(Offsets::VAR_GAME_STATE_PTR);
    if (clickInfo == nullptr)
        return false;

    // Re-pick at the current cursor screen position, as a press does. With
    // a ground placement pending the pick's flag word is 3, which tests no
    // object (benilla-re ui/worldpick#casts-that-want-a-place): a cursor on a
    // unit reaches the ground behind it, and no UI frame is consulted, as
    // 3.80.2's at-cursor trace (FUN_1411948b0) does not consult one either.
    auto refresh = reinterpret_cast<RefreshCursorRaycast_t>(Offsets::FUN_REFRESH_CURSOR_RAYCAST);
    refresh(clickInfo);

    const uint32_t hitType = Game::Read<uint32_t>(clickInfo, Offsets::OFF_CLICK_INFO_TYPE);
    if (hitType == 0) {
        // Nothing under the cursor (sky, off the world frame): 3.80.2 fails
        // the cast with SPELL_FAILED_NOT_HERE. 1.12's reporter shows the same
        // reason with its text, log line and events.
        const uint32_t spellID = Game::Read<uint32_t>(Offsets::VAR_PENDING_CAST_SPELL);
        Stop();
        reinterpret_cast<ReportCastFailure_t>(Offsets::FUN_SPELL_C_SPELL_FAILED)(
            spellID, Offsets::SPELL_FAILED_NOT_HERE, -1, -1, 0);
        return false;
    }
    if (hitType != 1) {
        // An object pick (a spell that also takes a unit): no ground to bind.
        Stop();
        return false;
    }

    float point[3];
    std::memcpy(point, clickInfo + Offsets::OFF_CLICK_INFO_COORDS, sizeof(point));
    ClampToRange(reinterpret_cast<const float *>(clickInfo + Offsets::OFF_CLICK_INFO_RAY_NEAR),
                 point);
    return Commit(point);
}

bool IsCursorTarget(const char *target) {
    return target != nullptr && _stricmp(target, "cursor") == 0;
}

bool CastAtCursor(int spellID) {
    Stop();
    if (!DispatchSpellCast(spellID))
        return false;
    CommitAtCursor();
    return true;
}

bool CastAtCursorByName(const char *name) {
    Stop();
    if (!DispatchSpellCastByName(name))
        return false;
    CommitAtCursor();
    return true;
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
    int bookType = 0;
    const int slot = Spell::Arg::NameToSlot(name, &bookType);
    if (slot < 0)
        return false; // not in the player's spellbook

    DispatchSlot(slot, bookType, targetGuid);
    return true;
}

} // namespace Spell::Placement
