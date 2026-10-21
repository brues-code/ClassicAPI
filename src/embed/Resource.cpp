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

#include "embed/Resource.h"

#include <windows.h>

// The linker-defined base of this module — our HMODULE without a lookup.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace Embed {

Bytes Resource(int id) {
    const HMODULE self = reinterpret_cast<HMODULE>(&__ImageBase);
    HRSRC info = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (info == nullptr)
        return {nullptr, 0};
    HGLOBAL handle = LoadResource(self, info);
    if (handle == nullptr)
        return {nullptr, 0};
    return {static_cast<const unsigned char *>(LockResource(handle)),
            static_cast<size_t>(SizeofResource(self, info))};
}

} // namespace Embed
