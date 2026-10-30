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

#include "Range.h"

#include "Game.h"
#include "Offsets.h"
#include "object/Corpse.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>

namespace Spell::Range {

namespace {

// FUN_SPELL_RANGE_CHECK: char __fastcall(caster, target, spellID, outFlag).
using SpellRangeCheck_t = char(__fastcall *)(void *caster, void *target,
                                             int spellID, char *outFlag);

// FUN_GET_MIN_MAX_RANGE: void __fastcall(caster, spellID, min, max, target).
using GetMinMaxRange_t = void(__fastcall *)(void *caster, uint32_t spellID,
                                            float *minRange, float *maxRange,
                                            void *target);

// The engine's own has-range notion, mirroring `ActionHasRange`
// (`0x4E75D0` → `ActionSlotHasRange`): resolve with a NULL target, then
// either bound at or above the near-zero epsilon counts as ranged. The
// null target is load-bearing: given a unit target, the resolver folds
// both units' combat reach into every non-melee row, so a self-range
// (0/0) spell like Hearthstone comes back with max ~3yd and reads as
// ranged. The negated `<` (rather than `>=`) reproduces the engine's
// `fcomp` + `test ah,5; jp` exactly — a NaN bound counts as ranged,
// matching vanilla. Unlike the raw `SpellRange.dbc` read this replaces,
// the engine call also covers `Attributes & 0x404` spells (flat 100-yard
// max with no range row), melee rows whose max folds combat reach, and
// the stationary / ranged-item / spell-mod bonuses.
void EngineMinMax(void *caster, int spellID, void *target, float &minRange,
                  float &maxRange) {
    minRange = 0.0f;
    maxRange = 0.0f;
    auto getMinMax = reinterpret_cast<GetMinMaxRange_t>(
        Offsets::FUN_GET_MIN_MAX_RANGE);
    getMinMax(caster, static_cast<uint32_t>(spellID), &minRange, &maxRange,
              target);
}

bool EngineHasRange(void *caster, int spellID) {
    float minRange;
    float maxRange;
    EngineMinMax(caster, spellID, nullptr, minRange, maxRange);
    const float epsilon = Game::Read<float>(
        static_cast<uintptr_t>(Offsets::VAR_HAS_RANGE_EPSILON));
    return !(std::fabs(minRange) < epsilon) ||
           !(std::fabs(maxRange) < epsilon);
}

// FUN_SPELL_RANGE_CHECK on two objects. It dereferences each GetPosition
// result without a null check, so an object with no position yet gets no
// answer instead.
int Compare(void *caster, void *target, int spellID) {
    float casterPos[3];
    float targetPos[3];
    if (!Unit::Position::Read(caster, casterPos) ||
        !Unit::Position::Read(target, targetPos))
        return -1;
    char minMaxFlag = 0;
    auto check =
        reinterpret_cast<SpellRangeCheck_t>(Offsets::FUN_SPELL_RANGE_CHECK);
    return check(caster, target, spellID, &minMaxFlag) != 0 ? 1 : 0;
}

// --- The target gate ---------------------------------------------------------
//
// 4.3.4's Spell_C_RangeCheckSelected answers "no answer" for a target the
// spell cannot be cast at, then range-checks the rest. 1.12's version is
// CheckSpellTargetRange (FUN_CHECK_SPELL_TARGET_RANGE), the gate the cast
// request and the action-bar range use; it folds "no answer" into "in
// range", so it is mirrored here branch for branch from its disassembly
// (0x006E4440..0x006E4704). Two steps are 4.3.4's and have no 1.12
// counterpart:
//   - a spell with an effect aimed at the pet (implicit target 5) and no
//     target-word 0x2 measures to the player's pet, with 0x2 added;
//   - a resurrect aimed at a ghost measures to the ghost's corpse. 4.3.4
//     keys it on an attribute 1.12 lacks; 1.12 marks every resurrect, and
//     only them, with target word 0x8000.
// 1.12 has no counterpart of 4.3.4's implied-target redirect
// (AttributesExE 0x800) or its 0x10000-flag override (AttributesExF), so
// neither appears.

constexpr uint32_t TARGET_WORD_UNIT = 0x2;
constexpr uint32_t TARGET_WORD_RAID = 0x4;
constexpr uint32_t TARGET_WORD_PARTY = 0x8;
constexpr uint32_t TARGET_WORD_ENEMY = 0x80;
constexpr uint32_t TARGET_WORD_FRIEND = 0x100;
constexpr uint32_t TARGET_WORD_CORPSE_ENEMY = 0x200;
constexpr uint32_t TARGET_WORD_SELF = 0x400;
constexpr uint32_t TARGET_WORD_GAMEOBJECT = 0x800;
constexpr uint32_t TARGET_WORD_GAMEOBJECT_ITEM = 0x4000;
constexpr uint32_t TARGET_WORD_CORPSE_ALLY = 0x8000;
constexpr int32_t TARGET_PET = 5;

using Relation_t = bool(__thiscall *)(void *caster, void *other);
using CreatureType_t = int(__thiscall *)(void *unit);

bool Relation(uintptr_t fn, void *caster, void *other) {
    return reinterpret_cast<Relation_t>(fn)(caster, other);
}

const uint8_t *Fields(const void *obj) {
    return Game::Read<const uint8_t *>(obj, Offsets::OFF_UNIT_DESCRIPTOR);
}

const uint8_t *Head(const void *obj) {
    return Game::Read<const uint8_t *>(obj, Offsets::OFF_UNIT_GUID_PTR);
}

void *Pet() {
    return Object::ByGuid(Offsets::TYPEMASK_UNIT,
                          Game::Read<uint64_t>(static_cast<uintptr_t>(Offsets::VAR_PET_GUID)));
}

// The target word after the effects' implicit targets edit it
// (0x006E4488..0x006E44F7). Sets `aimsAtPet` for 4.3.4's pet redirect.
uint32_t TargetWord(const uint8_t *rec, bool &aimsAtPet) {
    uint32_t word = Game::Read<uint16_t>(rec, Offsets::OFF_SPELL_RECORD_TARGETS);
    aimsAtPet = false;
    for (int i = 0; i < 3; ++i) {
        switch (Game::Read<int32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A +
                                             4 * i)) {
        case 1:
            word &= ~TARGET_WORD_SELF;
            break;
        case TARGET_PET:
            aimsAtPet = true;
            word &= ~TARGET_WORD_CORPSE_ALLY;
            break;
        case 6:
        case 53:
            word |= TARGET_WORD_ENEMY;
            break;
        case 21:
        case 45:
            word |= TARGET_WORD_FRIEND;
            break;
        case 23:
            word |= TARGET_WORD_GAMEOBJECT;
            break;
        case 25:
        case 63:
            word |= TARGET_WORD_UNIT;
            break;
        case 26:
            word |= TARGET_WORD_GAMEOBJECT_ITEM;
            break;
        case 35:
            word |= TARGET_WORD_PARTY;
            break;
        case 57:
        case 61:
            word |= TARGET_WORD_RAID;
            break;
        default:
            break;
        }
    }
    return word;
}

// A unit target (0x006E4511..0x006E469A).
bool AdmitsUnit(void *caster, const uint8_t *rec, uint32_t word, void *unit) {
    const uint8_t *fields = Fields(unit);
    const uint32_t flags = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS);
    if (flags & Offsets::UNIT_FLAG_NON_ATTACKABLE_2)
        return false;
    const uint32_t creatureTypes =
        Game::Read<uint32_t>(rec, Offsets::OFF_SPELL_RECORD_TARGET_CREATURE_TYPE);
    if (creatureTypes != 0) {
        const int type =
            reinterpret_cast<CreatureType_t>(Offsets::FUN_UNIT_CREATURE_TYPE)(unit);
        if (type == 0 || (creatureTypes & (1u << ((type - 1) & 31))) == 0)
            return false;
    }
    const bool dead = Game::Read<int32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) <= 0;
    if (dead) {
        if ((Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_DYNAMIC_FLAGS) &
             Offsets::UNIT_DYNFLAG_DEAD) == 0 &&
            (word & (TARGET_WORD_CORPSE_ALLY | TARGET_WORD_SELF | TARGET_WORD_CORPSE_ENEMY)) ==
                0 &&
            (Game::Read<uint32_t>(rec, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX2) & 1) == 0)
            return false;
    } else if (word & TARGET_WORD_SELF) {
        return false;
    }

    if ((word & TARGET_WORD_PARTY) && Relation(Offsets::FUN_UNIT_IN_SAME_PARTY, caster, unit) &&
        Relation(Offsets::FUN_UNIT_CAN_ASSIST, caster, unit))
        return true;
    if ((word & TARGET_WORD_RAID) && Relation(Offsets::FUN_UNIT_IN_SAME_RAID, caster, unit) &&
        Relation(Offsets::FUN_UNIT_CAN_ASSIST, caster, unit))
        return true;
    if ((word & TARGET_WORD_FRIEND) && Relation(Offsets::FUN_UNIT_CAN_ASSIST, caster, unit))
        return true;
    if ((word & TARGET_WORD_ENEMY) && Relation(Offsets::FUN_UNIT_CAN_ATTACK, caster, unit))
        return true;
    if ((word & TARGET_WORD_UNIT) && (flags & Offsets::UNIT_FLAG_IMMUNE_TO_PLAYER) == 0)
        return true;

    const bool isPlayer =
        (Game::Read<uint32_t>(Head(unit), Offsets::OFF_OBJECT_FIELD_TYPE) &
         Offsets::TYPEMASK_PLAYER) != 0;
    if ((word & TARGET_WORD_CORPSE_ALLY) && dead &&
        (isPlayer || Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_PETNUMBER) != 0))
        return Relation(Offsets::FUN_UNIT_CAN_ASSIST, caster, unit);
    return (word & TARGET_WORD_CORPSE_ENEMY) && dead && isPlayer &&
           !Relation(Offsets::FUN_UNIT_CAN_ASSIST, caster, unit);
}

// A corpse target (0x006E46B2..0x006E46FF).
bool AdmitsCorpse(void *caster, uint32_t word, void *corpse) {
    if (Game::Read<uint32_t>(Fields(corpse), Offsets::OFF_CORPSE_FIELD_FLAGS) &
        Offsets::CORPSE_FLAG_BONES)
        return false;
    const bool assist = Relation(Offsets::FUN_UNIT_CAN_ASSIST_CORPSE, caster, corpse);
    return ((word & TARGET_WORD_CORPSE_ENEMY) && !assist) ||
           ((word & TARGET_WORD_CORPSE_ALLY) && assist);
}

// The object to measure to, or null when the spell cannot be cast at
// `target`.
void *AdmittedTarget(void *caster, const uint8_t *rec, void *target) {
    bool aimsAtPet = false;
    uint32_t word = TargetWord(rec, aimsAtPet);
    if (aimsAtPet && (word & TARGET_WORD_UNIT) == 0) {
        target = Pet();
        if (target == nullptr)
            return nullptr;
        word |= TARGET_WORD_UNIT;
    }

    const uint32_t type = Game::Read<uint32_t>(Head(target), Offsets::OFF_OBJECT_FIELD_TYPE);
    if (type & Offsets::TYPEMASK_UNIT) {
        if ((word & TARGET_WORD_CORPSE_ALLY) && (type & Offsets::TYPEMASK_PLAYER)) {
            const auto *info = Game::Read<const uint8_t *>(target, Offsets::OFF_CGPLAYER_INFO);
            if (info != nullptr &&
                (Game::Read<uint32_t>(info, Offsets::OFF_PLAYER_INFO_FLAGS) &
                 Offsets::PLAYER_FLAG_GHOST) != 0) {
                if (void *corpse = Object::Corpse::ByOwner(Game::Read<uint64_t>(Head(target), 0)))
                    return corpse;
            }
        }
        return AdmitsUnit(caster, rec, word, target) ? target : nullptr;
    }
    if (type & Offsets::TYPEMASK_GAMEOBJECT)
        return (word & (TARGET_WORD_GAMEOBJECT | TARGET_WORD_GAMEOBJECT_ITEM)) ? target
                                                                                : nullptr;
    if (type & Offsets::TYPEMASK_CORPSE)
        return AdmitsCorpse(caster, word, target) ? target : nullptr;
    return nullptr;
}

} // namespace

bool SpellIDHasRange(int spellID) {
    if (spellID <= 0)
        return false;
    // A null caster zeroes both bounds, which reads as rangeless.
    return EngineHasRange(Unit::Position::ResolveToken("player"), spellID);
}

void CasterMinMax(void *caster, int spellID, float &minRange, float &maxRange) {
    // Null target, as 4.3.4's `Script_GetSpellInfo` passes: the engine folds
    // the caster's current target's reach, else the caster's own.
    EngineMinMax(caster, spellID, nullptr, minRange, maxRange);
}

int PlayerVsUnit(int spellID, const char *unitToken) {
    // No answer when the range check can't apply: unknown spell or no
    // unit token given.
    if (spellID <= 0 || unitToken == nullptr)
        return -1;

    void *caster = Unit::Position::ResolveToken("player");
    void *target = Unit::Position::ResolveToken(unitToken);
    if (caster == nullptr || target == nullptr)
        return -1;
    // Rangeless spell (self buff, etc.) has no meaningful "in range" answer.
    if (!EngineHasRange(caster, spellID))
        return -1;

    return Compare(caster, target, spellID);
}

int CastVsUnit(int spellID, const char *unitToken, bool petCaster) {
    if (spellID <= 0 || unitToken == nullptr)
        return -1;
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    void *caster = petCaster ? Pet() : Unit::Position::ResolveToken("player");
    void *target = Unit::Position::ResolveToken(unitToken);
    if (rec == nullptr || caster == nullptr || target == nullptr)
        return -1;
    if (!EngineHasRange(caster, spellID))
        return -1;
    target = AdmittedTarget(caster, rec, target);
    if (target == nullptr)
        return -1;
    return Compare(caster, target, spellID);
}

} // namespace Spell::Range
