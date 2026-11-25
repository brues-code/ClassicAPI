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

// Radial progress bars: a texture drawn as the wedge of a ring that fills
// around its centre (SetRadialProgressBar*, <RadialProgressBar>). See
// Radial.cpp.
namespace Texture::Radial {

// The texture draws as a radial progress bar.
bool Active(void *texture);

// The RadialProgress animation's step: the fill percentage, 0 to 1.
void SetPercent(void *texture, float percent);

} // namespace Texture::Radial
