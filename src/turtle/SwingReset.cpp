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

// Turtle WoW: Slam interrupts the caster's own melee swing timer.
//
// Every "Slam" row in Turtle's OWN client Spell.dbc with RangeIndex 2 (the
// real player-castable melee ranks — a handful of RangeIndex-6 rows share
// the name but are unrelated proc/visual entries, not castable) carries
// `AttributesEx2 & SPELL_ATTR_EX2_NOT_RESET_AUTO_ACTIONS`, which per the
// stock `Combat::Swing` rule (mirroring tortoise-wow's
// `Spell::cast`/`IsMeleeAttackResetSpell`) should EXEMPT it from resetting
// the caster's melee swing timer — the documented vanilla behavior where
// Slam "weaves" with white swings instead of delaying them.
//
// Verified wrong in-game by packet timestamps: casting Slam delayed BOTH
// the next main-hand AND off-hand `SMSG_ATTACKERSTATEUPDATE` well past
// their normal cadence (main-hand gap ~3.6s vs a normal ~1.78s cycle),
// exactly the signature of a full swing-timer reset. So on Turtle's actual
// server, Slam behaves like the OLD pre-1.10 vanilla bug/feature where it
// locked the weapon for its cast time — contradicting both the client DBC
// data and the public tortoise-wow reference source, which either lags
// Turtle's live server or was never accurate for this spell specifically.
//
// Ranks below are every RangeIndex-2 "Slam" row in the client Spell.dbc
// (vanilla ranks 1-8 plus a few Turtle-itemization variants); listed
// explicitly rather than matched structurally (e.g. "any spell named
// Slam") to keep the override an auditable, fixed set.
//
// Gated on Turtle::Detected() — unlike Turtle::ComboDuration's Rip
// override, there's no client-visible "bug condition" to gate on instead
// (the DBC row here is well-formed; it's the SERVER that diverges from it),
// so a stock/other-realm client must not get this Turtle-specific behavior.

#include "combat/Swing.h"
#include "turtle/Detect.h"

#include <cstdint>

namespace Turtle::SwingReset {

namespace {

constexpr uint32_t kSlamRanks[] = {
    1464, 8820, 11430, 11604, 11605, 45599, 45960, 45961, 45963, 45964, 53214,
};

bool IsSlam(uint32_t spellId) {
    if (!Turtle::Detected())
        return false;
    for (uint32_t id : kSlamRanks) {
        if (id == spellId)
            return true;
    }
    return false;
}

const Combat::Swing::AutoSwingResetOverride _register{&IsSlam};

} // namespace

} // namespace Turtle::SwingReset
