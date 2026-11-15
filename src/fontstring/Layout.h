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

namespace FontString::Layout {

// Lay a font string out again exactly as SetText (FUN_00771D80) does after a
// content change, so a layout-affecting property (max lines, word wrap) takes
// effect on the next draw: drop the cached measures, release the built text,
// clear the layout-dirty bit and resize the region. A string sized by its
// text gets its new height this way; a rebuild alone keeps the old one.
// Re-calling SetText would NOT work - it returns early on unchanged text.
void Invalidate(void *fontString);

// The font string's font height in layout units (FUN_FONTSTRING_FONT_HEIGHT):
// its stored height (fs+0xE4), or with `calculated` and the measure flag 0x200
// set, the gxu font's native height re-derived. The engine's own width path
// passes `calculated` (0x007728F4) and scales the result by fs+0x7C.
float FontHeight(void *fontString, bool calculated);

} // namespace FontString::Layout
