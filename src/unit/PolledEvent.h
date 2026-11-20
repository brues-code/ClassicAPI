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

#include "event/Custom.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// A unit event that the client polls, `EVENT: unitTarget, bool`, as the 3.80.2
// client polls UNIT_IN_RANGE_UPDATE and UNIT_DISTANCE_CHECK_UPDATE: one world
// system (0x146AD78A0) runs each every frame (0x141AE3140 →
// 0x141AE2B90):
//   - The units are the tokens frames named with RegisterUnitEvent for the
//     event. A plain RegisterEvent names none, so with no unit filter
//     nothing is checked.
//   - Each token's GUID is checked. The answer is kept per GUID: a GUID's
//     first check fires, and later ones fire when the answer changes. Nothing
//     is dropped when a frame stops wanting the unit.
//   - A fire goes out once for every token that names the GUID then, to the
//     frames whose filter takes that token (all of them for a plain
//     registration).
//   - The answers are cleared when the world is torn down (0x141AE2B70 from
//     0x141691B50). Here they are cleared while the player is not in the world
//     (VAR_IN_WORLD), which is so behind every loading screen.
namespace Unit::PolledEvent {

class Poller {
  public:
    using Check = bool (*)(uint64_t guid);

    // `name` is the event-table name the frames register; `check` answers for
    // one GUID.
    Poller(const char *name, const Event::Custom::AutoReserve &reserve, Check check)
        : name_(name), reserve_(reserve), check_(check) {}

    // Runs one frame's checks; call from a world-tick subscription.
    void Tick();

  private:
    void Want(const std::string &token);

    const char *name_;
    const Event::Custom::AutoReserve &reserve_;
    Check check_;
    std::unordered_map<uint64_t, bool> answers_;
    std::vector<std::string> tokens_;
};

} // namespace Unit::PolledEvent
