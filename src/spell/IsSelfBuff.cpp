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

// `C_Spell.IsSelfBuff(spellID)` — true iff every active effect on the
// spell targets the caster and only the caster. Read directly from
// `Spell.dbc.EffectImplicitTargetA[3]` and `EffectImplicitTargetB[3]`
// — an effect is "self-only" when both targets are either
// `TARGET_NONE` (0) or `TARGET_SELF` (1). Effects with a zero
// `Effect` code are skipped (unused effect slots). The record-scan core
// is `Spell::IsSelfBuff::IsSelfBuff` (see the header) so the aura layer
// can share it for sourceGUID inference.

#include "spell/IsSelfBuff.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstdint>

namespace Spell::IsSelfBuff {

namespace {

// Spell.dbc per-effect arrays. Each is an int32[3] in the record.
// Field-index derivation against the known `EffectApplyAuraName` at
// field 91 / +0x16C — the implicit-target pair sits at field 82/85.
constexpr int EFFECT_COUNT = 3;

// Vanilla 1.12 implicit-target codes (CMaNGOS `Targets` enum).
constexpr int TARGET_NONE = 0;
constexpr int TARGET_SELF = 1;

bool IsSelfOnlyTarget(int target) {
    return target == TARGET_NONE || target == TARGET_SELF;
}

} // namespace

bool IsSelfBuff(uint32_t spellID) {
    if (spellID == 0)
        return false;
    const uint8_t *rec = DBC::Record(Offsets::VAR_SPELL_RECORDS,
                                     Offsets::VAR_SPELL_RECORD_COUNT, spellID);
    if (rec == nullptr)
        return false;

    bool sawEffect = false;
    for (int i = 0; i < EFFECT_COUNT; ++i) {
        const int effect = *reinterpret_cast<const int *>(
            rec + Offsets::OFF_SPELL_RECORD_EFFECT + i * 4);
        if (effect == 0)
            continue;
        sawEffect = true;
        const int targetA = *reinterpret_cast<const int *>(
            rec + Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A + i * 4);
        const int targetB = *reinterpret_cast<const int *>(
            rec + Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_B + i * 4);
        if (!IsSelfOnlyTarget(targetA) || !IsSelfOnlyTarget(targetB))
            return false;
    }
    return sawEffect;
}

static int __fastcall Script_IsSelfBuff(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(
        L, spellID > 0 && IsSelfBuff(static_cast<uint32_t>(spellID)));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spellID", "number", "The spell to examine."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isSelfBuff", "bool",
                   "True when every effect of the spell targets the caster alone."),
};
const Game::Doc::Function kIsSelfBuff{
    "Whether the spell only ever applies to the caster.", kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "IsSelfBuff",
                                     &Script_IsSelfBuff, &kIsSelfBuff);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::IsSelfBuff
