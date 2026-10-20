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

// A texture's desaturation, the state behind texture:SetDesaturation /
// GetDesaturation, for other classes that desaturate a texture they own (the
// StatusBar's bar texture).
namespace Texture::Desaturation {

// `amount` 0..1, clamped (NaN is 0); quantised to 1/255.
void SetAmount(void *region, double amount);

// The amount the region draws with: 0, or level / 255.
double GetAmount(void *region);

} // namespace Texture::Desaturation
