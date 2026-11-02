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

namespace ScrollingMessageFrame::InsertMode {

// Anchors line `index` of the frame again as the line layout anchors it when
// it makes the line, for the frame's insert mode: line 0 on the frame, every
// other line on the line before it. Clears the line's points first; the
// caller lays the line out.
void AnchorLine(void *smf, int index);

} // namespace ScrollingMessageFrame::InsertMode
