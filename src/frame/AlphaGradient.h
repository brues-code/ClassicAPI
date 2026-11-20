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

// The frame draws as one of 3.80.2's render nodes (the part of its predicate,
// 0x14058D3B0, that 1.12 has): it clips its children, or it is a ScrollFrame's
// scroll child. A gradient draws only on a node.
bool IsNode(const void *frame);

// Draws that follow are the content of `node`: the gradients being drawn are
// muted, as 3.80.2 renders a nested node with only its own gradient, and
// `node`'s own applies when it has one. Returns a token for End.
int Begin(const void *node);
void End(int token);

// Texture stages from 1 up that the gradients being drawn hold, muted or not.
int StagesInUse();

} // namespace Frame::AlphaGradient
