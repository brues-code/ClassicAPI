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

// `UNIT_TARGET` — a unit's target changed. 4.3.4 has no code of its own for
// it: ScriptEventsRegisterUnit (Ui/ScriptEvents.cpp) watches every named unit
// field of a script-registered unit, and the field index is the event id.
// UNIT_FIELD_TARGET is index 12 there, named UNIT_TARGET, watched 8 bytes, and
// UnitFieldChangedHandler signals it once per token of the unit (focus
// included).
//
// 1.12 keeps the same machinery: FUN_0051bbb0 watches unit field i when slot i
// of the static name array is named, and FUN_0051bd50 signals event i through
// the token broadcast FUN_00515e50. UNIT_FIELD_TARGET is index 10
// (OFF_UNIT_FIELD_TARGET / 4), and the watcher's size selector still gives it
// the 8 bytes of a GUID. Only the name is missing, so naming slot 10 is the
// whole backport. Every token path then follows on its own: the engine's
// tokens, nampower's replacement broadcast (it signals any event code it is
// given), Unit::TokenEvents for focus / nameplateN / markN, and
// Unit::TokenObserver for focustarget, which reads the same array.
//
// Id 10 is in the glue band, so Event::Custom never claims it. The one place
// that signals 10 by number is the realm-list rebuild (0x0046E73D, glue's
// GET_PREFERRED_REALM_INFO), which only CGlueMgr::Idle reaches. The glue table
// is a separate array (0x00B41E70), so this name never reaches glue.

#include "Offsets.h"
#include "event/Custom.h"

namespace Unit::TargetEvent {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
};
const Game::Doc::Event kDoc{"Unit", "Fires when the unit's target changes.", kPayload};
const Event::Custom::EngineName _name{Offsets::OFF_UNIT_FIELD_TARGET / 4, "UNIT_TARGET", &kDoc};

} // namespace

} // namespace Unit::TargetEvent
