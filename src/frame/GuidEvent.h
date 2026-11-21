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
#include <vector>

// `frame:RegisterGUIDEvent(event, guid, ...)` — a unit event for the units
// with these GUIDs, whether or not a token names them. A ClassicAPI extension;
// no client has it. Unit events reach the listeners through Unit::Signal.
namespace Frame::GuidEvent {

// Whether a frame registered `guid` for the event in `slot`.
bool Has(int slot, uint64_t guid);

// Whether a frame registered any GUID for the event in `slot`.
bool HasAny(int slot);

// Whether a frame registered `guid` for any event. Unit::TokenEvents keeps
// such a unit watched, so its field changes fire unit events.
bool Watched(uint64_t guid);

// The GUIDs registered for the event in `slot`, each once.
std::vector<uint64_t> Guids(int slot);

// Fires the event in `slot` to the frames that registered `guid` for it, and
// to no other: arg1 is the GUID as UnitGUID writes it, then the values
// `push(L, ctx)` pushes, returning their count. Nothing when no frame did.
using Push = int (*)(void *L, void *ctx);
void Deliver(int slot, uint64_t guid, Push push, void *ctx);

} // namespace Frame::GuidEvent
