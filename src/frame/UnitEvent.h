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

#include "AutoList.h"

#include <string>
#include <vector>

namespace Frame::UnitEvent {

// The unit-token table RegisterUnitEvent takes its units from, as the 3.80.2
// client keeps it: true with the table's (lowercase) name for `s` when `s` is a
// token, matched without regard to case.
bool CanonicalUnitToken(const char *s, std::string *out);

// Told after a frame's registrations shrink: `eventName` for
// Frame::UnregisterEvent (the name as the caller gave it), nullptr for
// Frame::UnregisterAllEvents. This module owns those two hooks (one MinHook per
// address) and fans out. Declare a file-scope
//   static const Frame::UnitEvent::AutoUnregister _sub{&OnUnregister};
using UnregisterListener = void (*)(void *frame, const char *eventName);
using AutoUnregister = AutoList<struct AutoUnregisterTag, UnregisterListener>;

// The RegisterUnitEvent filter for one fire whose first argument is the
// string `arg1`: false when `frame` registered `eventName` (the event-table
// entry's own name) with units and `arg1` matches none of them. True for a
// plain registration. The engine's dispatches meet this filter at the runner;
// a dispatcher of our own that walks the subscriber chain itself
// (Event::Custom::FireValues) asks here for each frame.
bool Accepts(const void *frame, const char *eventName, const char *arg1);

// A plain `Frame::RegisterEvent(frame, eventName)` just ran (DllMain's hook):
// the registration goes plain, as 3.80.2's listener add overwrites the unit
// list. RegisterUnitEvent's own call into the engine is ignored.
void OnPlainRegister(const void *frame, const char *eventName);

// The units `frame` gave RegisterUnitEvent for `eventName` (the event-table
// entry's own name), or nullptr for a plain registration. Valid until the
// frame's registrations change.
const std::vector<std::string> *Units(const void *frame, const char *eventName);

} // namespace Frame::UnitEvent
