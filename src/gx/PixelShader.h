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

#include <cstdint>

// Pixel shaders of our own, as engine shader objects: the engine's loader
// registers a node under a path that has no file, and we give it the D3D9
// bytecode the file would have held. The engine owns the node (its shader
// table, refcount and destructor) and binds it like its own (GxRs 0x3F).
namespace Gx::PixelShader {

// The device's pixel shader profile (OFF_GXDEV_PS_PROFILE): 4 = ps_2_0 or
// newer, 3..0 = ps_1_4..ps_1_1, -1 = none.
int Profile();

enum class Result {
    Built,    // the node is valid and has its D3D object
    Retry,    // the device is lost now; nothing was registered or latched
    Rejected, // the device refused the bytecode; the node stays inert
};

// The shader for `path`, built from `tokens` (a D3D9 token stream ending in
// 0x0000FFFF). A node the path already names keeps its code. Failures are
// logged under `what`; the caller latches a Rejected build.
void *Build(const char *path, const uint32_t *tokens, uint32_t count, const char *what, Result &result);

} // namespace Gx::PixelShader
