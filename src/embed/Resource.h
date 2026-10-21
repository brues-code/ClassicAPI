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

#include <cstddef>

namespace Embed {

struct Bytes {
    const unsigned char *data;
    size_t size;
};

// The bytes of RCDATA resource `id` in this DLL (IDs come from the generated
// `embedded_resources.h`). The memory is part of the mapped image, so it stays
// valid for the life of the DLL and must not be freed. `{nullptr, 0}` when the
// resource is missing.
Bytes Resource(int id);

} // namespace Embed
