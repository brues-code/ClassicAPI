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

namespace Model::Portrait {

// A portrait texture baked from a live model, as BakePortrait (0x00524F60)
// bakes a unit's: a copy of the model without its item models, drawn with
// its portrait camera. Each bake draws into the same texture, as the engine
// re-bakes a dirty unit's portrait in place.
struct Live;

// A live portrait of the caller's, transparent until its first bake; null
// when no texture could be made.
Live *AcquireLive();
// Gives it back. Its texture lives until logout, since a region may show it.
void ReleaseLive(Live *live);
// Shows it on a texture region.
void BindLive(Live *live, void *region);
// Bakes `model`, which the caller has found ready (BakePortrait's gate);
// false when the device cannot bake this frame.
bool BakeLive(Live *live, void *model);

} // namespace Model::Portrait
