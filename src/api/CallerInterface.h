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

namespace Api::CallerInterface {

// The `## Interface:` of the addon whose code called the running C function.
//
// The caller is the nearest Lua frame up the stack (C frames such as pcall
// are skipped). Its chunk name - `@Interface\AddOns\<Name>\<file>` for an
// addon file - names the addon folder, whose TOC is read once and cached.
// Returns 0 when that frame is not in an addon file (FrameXML, an XML
// handler body, whose chunk is named `<Frame>:<Script>`, a loadstring chunk
// with no addon path, the console), or when the TOC has no Interface line.
//
// For a script function whose return layout differs between client
// generations, this tells which layout its caller was written for.
int Get(void *L);

} // namespace Api::CallerInterface
