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

// `inherits="A, B"` — a list of templates on one frame, as 4.3.4 reads it.
// See Inherits.cpp for the engine steps this extends.

#include <cstdint>

namespace Xml::Inherits {

// 4.3.4 tokenizes `inherits` with SStrTokenize on " ," into a 1024-byte
// buffer (CSimpleFrame::LoadXML, FindParent, Script_CreateFrame).
constexpr unsigned kTokenSize = 1024;

// True when `inherits` holds a separator, i.e. vanilla's whole-string
// lookup would not find it but 4.3.4's tokenizer reads names out of it.
bool IsList(const char *inherits);

// Copies the next name at `*cursor` into `out` (empty at the end) and
// advances the cursor — the engine's own Storm tokenizer.
void NextName(const char **cursor, char *out);

// Calls `fn(name)` for each name in list order.
template <class Fn>
void ForEach(const char *inherits, Fn &&fn) {
    char name[kTokenSize];
    const char *cursor = inherits;
    while (cursor != nullptr && *cursor != '\0') {
        NextName(&cursor, name);
        if (name[0] == '\0')
            break;
        fn(static_cast<const char *>(name));
    }
}

// The frame node loader FUN_XML_FRAME_LOAD with list support. `Xml::FrameLoad`
// owns that hook and routes the engine's loader through here.
using Loader = uintptr_t(__fastcall *)(void *self, void *edx, const void *node, void *status);
uintptr_t LoadFrame(Loader original, void *self, void *edx, const void *node, void *status);

} // namespace Xml::Inherits
