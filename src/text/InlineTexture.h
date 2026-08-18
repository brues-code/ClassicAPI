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

namespace Text::InlineTexture {

// Called from FrameScript_Initialize_h before the /reload teardown: the reload
// frees every fontstring and text node, so the node→owner map and the per-node
// icon records are dropped (they rebuild as the reloaded UI re-emits its text).
void PrepareForReload();

} // namespace Text::InlineTexture
