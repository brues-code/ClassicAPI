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

// Authoritative "a bag changed this frame" subscription. `Bag::UpdateDelayed`
// co-hooks the three (and only three) engine `BAG_UPDATE` fire sites and
// coalesces them into a single per-frame signal — the same signal that backs
// the `BAG_UPDATE_DELAYED` event. Subscribe here to run C++ work exactly once
// per frame in which any bag actually changed, instead of polling every frame.
//
// Callbacks fire from the world tick's subscription, after the frame's UI draw
// and after every BAG_UPDATE for the frame has already fired (so bag contents
// are settled). No firing on frames
// where nothing changed. Order across subscribers is unspecified.
//
// Module pattern, matching `Tick::WorldTick::AutoSubscribe`: declare a
// file-scope `static const Bag::UpdateDelayed::AutoSubscribe _s{&Callback};`.

namespace Bag::UpdateDelayed {

using Callback = void (*)();

struct AutoSubscribe {
    explicit AutoSubscribe(Callback cb);
    Callback cb;
    AutoSubscribe *next;
};

} // namespace Bag::UpdateDelayed
