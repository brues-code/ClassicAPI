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

#pragma once

#include <cstdint>

namespace Unit::FlagsChanged {

// Runs after 1.12's CGUnit_C::OnUnitFlagsChanged (FUN_UNIT_FLAGS_CHANGED),
// the engine's reaction to a changed UNIT_FIELD_FLAGS, for every unit: the
// place 4.3.4's CGUnit_C::OnFlagChanged acts on the bits 1.12 ignores.
// `unit` is the CGUnit_C, `oldFlags` / `flags` the field before and after.
using Callback = void (*)(void *unit, uint32_t oldFlags, uint32_t flags);

// Place a static instance at file scope:
//
//   static const Unit::FlagsChanged::AutoSubscribe _sub{&OnFlags};
//
// The constructor chains the callback at static init; order across
// subscribers is unspecified.
struct AutoSubscribe {
    explicit AutoSubscribe(Callback cb);
    Callback cb;
    AutoSubscribe *next;
};

} // namespace Unit::FlagsChanged
