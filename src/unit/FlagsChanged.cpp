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

// The flags field's type watcher (OnFlagsFieldChanged, 0x006044B0) resolves
// the unit and calls CGUnit_C::OnUnitFlagsChanged with the old flags from
// one E8 call (CALL_FLAGS_FIELD_FLAGS_CHANGED). That call points here
// instead: the function runs the engine's handler, then each subscriber with
// the flags before and after. A call site can be redirected once, so every
// module that acts on a flag change subscribes here.

#include "FlagsChanged.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

namespace Unit::FlagsChanged {

namespace {

AutoSubscribe *g_head = nullptr;

using FlagsChanged_t = void(__thiscall *)(void *unit, uint32_t oldFlags);

void __fastcall FlagsChangedSite(void *unit, void *, uint32_t oldFlags) {
    reinterpret_cast<FlagsChanged_t>(Offsets::FUN_UNIT_FLAGS_CHANGED)(unit, oldFlags);
    const auto *desc = Game::Read<const uint8_t *>(unit, Offsets::OFF_CGUNIT_OBJECT_FIELDS);
    const uint32_t flags = Game::Read<uint32_t>(desc, Offsets::OFF_UNIT_FIELD_FLAGS);
    for (auto *node = g_head; node != nullptr; node = node->next)
        node->cb(unit, oldFlags, flags);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_FLAGS_FIELD_FLAGS_CHANGED, Offsets::FUN_UNIT_FLAGS_CHANGED,
                         reinterpret_cast<const void *>(&FlagsChangedSite));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

AutoSubscribe::AutoSubscribe(Callback cb) : cb(cb), next(g_head) {
    g_head = this;
}

} // namespace Unit::FlagsChanged
