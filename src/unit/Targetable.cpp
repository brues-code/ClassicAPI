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

// `UNIT_TARGETABLE_CHANGED` — a unit became selectable or stopped being so.
//
// 4.3.4's CGUnit_C::OnFlagChanged (Unit_C.cpp), which the unit flags field's
// watcher calls with the old flags, signals it when UNIT_FLAG_NOT_SELECTABLE
// flips (Script_SendUniqueSignal, so once per token of the unit). 1.12's copy,
// CGUnit_C::OnUnitFlagsChanged, has every other branch of it and not this
// one, so this subscribes to Unit::FlagsChanged, which runs after it.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "unit/FlagsChanged.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::Targetable {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
};
const Game::Doc::Event kDoc{
    "Unit", "Fires when the unit becomes selectable or stops being selectable.", kPayload};
const Event::Custom::AutoReserve kChanged{"UNIT_TARGETABLE_CHANGED", &kDoc};

void OnFlags(void *unit, uint32_t oldFlags, uint32_t flags) {
    const int slot = kChanged.Slot();
    if (((flags ^ oldFlags) & Offsets::UNIT_FLAG_NOT_SELECTABLE) == 0 ||
        !Event::Custom::HasListeners(slot))
        return;
    char tokens[16][32];
    const int n = Unit::Identity::TokensForGUID(Unit::Identity::GuidForObject(unit), tokens, 16);
    for (int i = 0; i < n; ++i)
        Event::Custom::Fire(slot, "%s", tokens[i]);
}

const Unit::FlagsChanged::AutoSubscribe _sub{&OnFlags};

} // namespace

} // namespace Unit::Targetable
