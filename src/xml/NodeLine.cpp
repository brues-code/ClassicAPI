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

// 1.12 links the same expat (1.95.5) as 3.80.2 and parses with it the same
// way, so the line comes from the engine's own pieces:
//   - XML_SetUserData (its one call is XMLTree_Load's) pairs the parser with
//     the handler arg the tree callbacks get;
//   - after the start handler (FUN_XML_START_ELEMENT) builds the node and
//     makes it current, expat's XML_GetCurrentLineNumber (linked, never called
//     by 1.12) reads the line of the start event — the opening tag, as in
//     3.80.2's start handler;
//   - the node destructor (FUN_XML_NODE_DTOR, which frees children and
//     siblings through itself, so it runs for every node) drops the entry.
// Only parses inside an XML file load are recorded: the file is what gives
// the line meaning. Most trees are freed when their file finishes loading;
// template nodes (RegisterXMLTemplate detaches them) keep their lines.

#include "xml/NodeLine.h"

#include "Game.h"
#include "Offsets.h"
#include "xml/FileStack.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Xml::NodeLine {

namespace {

using SetUserData_t = void(__cdecl *)(void *parser, void *data);
using GetLine_t = int(__cdecl *)(void *parser);
using StartElement_t = void(__cdecl *)(void **pair, const char *name, const char **atts);
using NodeDtor_t = void(__fastcall *)(void *node);

SetUserData_t g_origSetUserData = nullptr;
StartElement_t g_origStart = nullptr;
NodeDtor_t g_origDtor = nullptr;

// The parse in progress: its parser and the handler arg its callbacks get.
// XMLTree_Load never nests (its handlers only build nodes).
void *g_parser = nullptr;
void *g_pair = nullptr;

std::unordered_map<const void *, Location> g_lines;
// Each file path once; set elements keep their address, so a Location can
// point into one.
std::unordered_set<std::string> g_files;
// The interned copy of the last node's file: a file's nodes arrive in a row,
// so a compare saves a hash per node. Compared by content, since the engine
// passes <Include> paths in reused stack buffers.
const char *g_lastFile = nullptr;

const char *Intern(const char *path) {
    if (g_lastFile == nullptr || std::strcmp(path, g_lastFile) != 0)
        g_lastFile = g_files.emplace(path).first->c_str();
    return g_lastFile;
}

void __cdecl SetUserData_h(void *parser, void *data) {
    g_origSetUserData(parser, data);
    g_parser = parser;
    g_pair = data;
}

void __cdecl StartElement_h(void **pair, const char *name, const char **atts) {
    g_origStart(pair, name, atts);
    const char *file = Xml::FileStack::Current();
    if (pair != g_pair || file == nullptr)
        return;
    const void *node = pair[1];
    if (node != nullptr)
        g_lines[node] = {Intern(file),
                         reinterpret_cast<GetLine_t>(Offsets::FUN_XML_GET_CURRENT_LINE_NUMBER)(g_parser)};
}

void __fastcall NodeDtor_h(void *node) {
    for (AutoSubscribeFreed *s = AutoSubscribeFreed::head; s != nullptr; s = s->next)
        s->fn(node);
    if (!g_lines.empty())
        g_lines.erase(node);
    g_origDtor(node);
}

const Game::HookAutoRegister _setUserData{Offsets::FUN_XML_SET_USER_DATA,
                                          reinterpret_cast<void *>(&SetUserData_h),
                                          reinterpret_cast<void **>(&g_origSetUserData)};
const Game::HookAutoRegister _start{Offsets::FUN_XML_START_ELEMENT, reinterpret_cast<void *>(&StartElement_h),
                                    reinterpret_cast<void **>(&g_origStart)};
const Game::HookAutoRegister _dtor{Offsets::FUN_XML_NODE_DTOR, reinterpret_cast<void *>(&NodeDtor_h),
                                   reinterpret_cast<void **>(&g_origDtor)};

} // namespace

const Location *Get(const void *node) {
    const auto it = g_lines.find(node);
    return it != g_lines.end() ? &it->second : nullptr;
}

} // namespace Xml::NodeLine
