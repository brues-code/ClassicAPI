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

// Shared interceptor for the engine's no-arg event dispatcher
// `FUN_FIRE_EVENT_NO_ARGS` (`0x00703E50`) — the `__fastcall(int eventID)`
// path every no-arg event broadcast goes through (PLAYER_ENTERING_WORLD,
// LOOT_OPENED/CLOSED, zone changes, …), as opposed to the variadic
// `FUN_FIRE_EVENT` used by events that carry a printf-style payload.
//
// MinHook allows only ONE hook per target address, and more than one feature
// needs to intercept this dispatcher (loot silent-scan suppression, the
// PLAYER_ENTERING_WORLD payload). So — exactly like `Tick::WorldTick` for the
// per-frame tick — this module owns the single hook and fans out to
// subscribers. Declare a file-scope `static const
// Event::SignalHook::AutoSubscribe` in your module.
//
// An interceptor returns `true` when it has fully handled `eventID` and the
// engine's original no-arg fire must be SUPPRESSED — either because it
// swallowed the event (loot) or re-fired it a different way (the PEW payload
// re-broadcasts through the variadic `FUN_FIRE_EVENT` with args). Returning
// `false` lets the event fall through to the next subscriber and ultimately
// the original. The FIRST subscriber to return `true` wins and the original
// is skipped, so subscribers must claim disjoint event ids.

namespace Event::SignalHook {

using Interceptor = bool (*)(int eventID);

struct AutoSubscribe {
    explicit AutoSubscribe(Interceptor fn);
    Interceptor fn;
    AutoSubscribe *next;
};

// Runs `fn` each time the engine signals `eventID` through this dispatcher,
// without the power to suppress it — for a module that only needs to know the
// event happened (SPELLS_CHANGED, PLAYER_LEAVING_WORLD, …). Observers run
// before the interceptors, so a suppressed event is still observed.
struct AutoObserve {
    AutoObserve(int eventID, void (*fn)());
    int eventID;
    void (*fn)();
    AutoObserve *next;
};

} // namespace Event::SignalHook
