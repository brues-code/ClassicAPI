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

// World-entry observers. This module owns PLAYER_ENTERING_WORLD: it claims
// the id in `Event::SignalHook` and re-broadcasts it with its payload, and
// a claim ends the subscriber chain, so a module that must act on world
// entry subscribes here instead. Declare a file-scope
// `static const Event::EnteringWorld::AutoObserve _obs{&fn};`.
//
// Observers run after the re-broadcast, once Lua's PEW handlers have run —
// where 4.3.4's `CGGameUI::EnterWorld` runs its per-system `EnterWorld`
// calls (it signals PEW first). Login, `/reload` and every zone or
// instance transition fire PEW.

namespace Event::EnteringWorld {

using Observer = void (*)();

struct AutoObserve {
    explicit AutoObserve(Observer fn);
    Observer fn;
    const AutoObserve *next;
};

} // namespace Event::EnteringWorld
