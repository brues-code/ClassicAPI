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

// A frame's alpha gradient (frame:SetAlphaGradient), for the batch draws that
// apply it (Frame::ClipChildren) and the texture stages others take on top of
// it (Texture::Mask).
namespace Frame::AlphaGradient {

// Some frame has a gradient.
bool Any();

// The frame, or its nearest ancestor, with a gradient; nullptr when none has.
void *Nearest(const void *frame);

// Draws that follow fade by the gradient of `frame` and, with `ancestors`, of
// every frame above it that has one. Returns the texture stages it took, for
// End.
int Begin(const void *frame, bool ancestors);
void End(int stages);

// Texture stages from 1 up that the gradients being drawn hold.
int StagesInUse();

} // namespace Frame::AlphaGradient
