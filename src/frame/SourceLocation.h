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

// Where each frame was created — 9.2.5's `ScriptRegion:GetSourceLocation()`
// and the SOURCE lines of `GameTooltip:SetFrameStack`.
//
// A frame, texture or font string a script creates takes the calling
// script's "file:line"; one the XML loader builds takes its element's
// "file:line" (Xml::NodeLine). A region with neither answers for its parent
// frame.
namespace Frame::SourceLocation {

// Marks a Lua CreateFrame call for the frames it builds: Script_CreateFrame
// holds one across the engine's builder. The caller's location is read from
// the Lua call stack when the scope opens.
class LuaScope {
public:
    explicit LuaScope(void *L);
    ~LuaScope();
    LuaScope(const LuaScope &) = delete;
    LuaScope &operator=(const LuaScope &) = delete;
};

// The location recorded for `object` (its own, else its parent frame's), or
// nullptr when no script or XML file built it. Valid until the object is
// destroyed.
const char *Get(void *object);

} // namespace Frame::SourceLocation
