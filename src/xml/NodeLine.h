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

#include "AutoList.h"

// The file and opening-tag line of each XML element, for the elements of the
// XML files FUN_XML_LOAD_FILE reads (Xml::FileStack) — what 3.80.2's XMLTree
// start handler (FUN_1405C7930) stamps on every node (file id and
// XML_GetCurrentLineNumber).
// 1.12's nodes have no field for it, so this module keeps it beside them:
// recorded when the start handler builds the node, dropped when the node
// destructor frees it. Read by Xml::ScriptChunkName (handler chunk names) and
// Frame::SourceLocation (an XML region's "file:line").
namespace Xml::NodeLine {

struct Location {
    const char *file; // the path FUN_XML_LOAD_FILE was given; lives for the session
    int line;         // 1-based
};

// Where the node's element is, or nullptr for a node no XML file built
// (Script_CreateFrame's request node, a SimpleHTML or bindings tree). A
// template node keeps its own file after that file's load has ended.
const Location *Get(const void *node);

// Runs for each node the engine frees, before it frees it (its tag and text
// are still readable). Declare a file-scope
//   static const Xml::NodeLine::AutoSubscribeFreed _freed{&OnNodeFreed};
using FreedObserver = void (*)(const void *node);
using AutoSubscribeFreed = AutoList<struct AutoSubscribeFreedTag, FreedObserver>;

} // namespace Xml::NodeLine
