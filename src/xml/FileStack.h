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

// The XML files the engine's file processor FUN_XML_LOAD_FILE is working
// through, innermost last — a co-hook of the processor, which recurses for
// <Include>. Read by Xml::ScriptPath (what a `<Script file>` is relative to)
// and Frame::SourceLocation (which XML file built a frame).
namespace Xml::FileStack {

// The innermost file's path as the processor's caller passed it (`..` not
// collapsed), or nullptr outside a load.
const char *Current();

// How many files are open: 0 outside a load, 1 for a top-level file, more
// inside its <Include>s.
int Depth();

} // namespace Xml::FileStack
