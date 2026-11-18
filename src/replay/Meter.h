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

// The damage meter of a loaded replay: the recording's combat lines fed to
// the same totals the live meter keeps (damagemeter/Totals.h), up to a time
// of the replay. See Meter.cpp.

#include "damagemeter/Totals.h"
#include "replay/Recording.h"

#include <cstdint>

namespace Replay::Meter {

// The meter fed with the recording's events up to `t` (milliseconds from the
// stretch's start). Going back in time feeds it again from the start.
const DamageMeter::Meter &At(const Recording &r, int32_t t);

// Drops what was fed: the recording it was fed from is gone.
void Forget();

} // namespace Replay::Meter
