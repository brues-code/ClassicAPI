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

// XML handler chunks named by file and line, as 3.80.2 names them, so an
// error in an <OnLoad> body reads
//
//   [string "*PlayerFrame.xml:42_OnLoad"]:3: attempt to ...
//
// where 1.12 wrote `[string "PlayerFrame:OnLoad"]:3:`.
//
// 3.80.2 (WowClassicGM-China.exe):
//   - its XMLTree start handler (FUN_1405C7930) stamps every node with the
//     file id and XML_GetCurrentLineNumber (FUN_142A84430) at the start
//     event: the line of the element's opening tag;
//   - the script element compile (FUN_1405BE610) formats "%s:%d" from that
//     record (FUN_1405C7870), keeps the part after the last '/' or '\'
//     (FUN_1403310E0), and names the chunk "*%s_%s" with the element name;
//   - its luaO_chunkid (FUN_142A023E0) treats only '=' and '@' specially, so
//     the '*' name prints inside `[string "..."]`. The line after it counts
//     from the body's first line: the body is wrapped on one line
//     ("return function(self) %s end"), which our transpiler's preamble
//     also keeps newline-free.
//
// 1.12 names the chunk "<object>:<script>" in SetScriptObjectScript. Here:
//   - when a child of <Scripts> ends, "*<file>:<line>_<element>" is keyed by
//     its text pointer, final only now (the character handler reallocates
//     it). The line is Xml::NodeLine's, the file Xml::FileStack's;
//   - FrameScript_CompileFunction swaps that name in for a text it knows.
//     Every handler body loader passes the node's own text (the frame and
//     region loaders, animations, RegionScripts), and templates keep their
//     nodes, so a frame that inherits a handler names the template's file
//     and line, as 3.80.2's per-node compile cache does;
//   - the key goes with the node (Xml::NodeLine's freed observers).
// An inline <Script> keeps "<file>:<Scripts>", which both clients use.

#include "Game.h"
#include "Offsets.h"
#include "xml/FileStack.h"
#include "xml/FrameLoad.h"
#include "xml/NodeLine.h"

#include <string>
#include <unordered_map>

namespace Xml::ScriptChunkName {

namespace {

using EndElement_t = void(__cdecl *)(void **pair);
using Compile_t = int(__fastcall *)(const char *text, const char *chunkName);

EndElement_t g_origEnd = nullptr;
Compile_t g_origCompile = nullptr;

// Handler text -> chunk name.
std::unordered_map<const char *, std::string> g_names;

void __cdecl EndElement_h(void **pair) {
    const void *node = pair[1];
    const int line = node != nullptr ? Xml::NodeLine::Get(node) : 0;
    if (line > 0) {
        const void *parent = Game::Read<const void *>(node, Offsets::OFF_XML_NODE_PARENT);
        const char *text = Game::Read<const char *>(node, Offsets::OFF_XML_NODE_TEXT);
        const char *file = Xml::FileStack::Current();
        const char *script = Xml::Tag(node);
        if (parent != nullptr && Xml::TagIs(parent, "Scripts") && text != nullptr && *text != '\0' &&
            file != nullptr && script != nullptr) {
            const char *base = file;
            for (const char *p = file; *p != '\0'; ++p)
                if (*p == '\\' || *p == '/')
                    base = p + 1;
            std::string chunk = "*";
            chunk += base;
            chunk += ':';
            chunk += std::to_string(line);
            chunk += '_';
            chunk += script;
            g_names[text] = std::move(chunk);
        }
    }
    g_origEnd(pair);
}

void OnNodeFreed(const void *node) {
    if (g_names.empty())
        return;
    if (const char *text = Game::Read<const char *>(node, Offsets::OFF_XML_NODE_TEXT))
        g_names.erase(text);
}

int __fastcall Compile_h(const char *text, const char *chunkName) {
    if (text != nullptr && !g_names.empty()) {
        const auto it = g_names.find(text);
        if (it != g_names.end())
            return g_origCompile(text, it->second.c_str());
    }
    return g_origCompile(text, chunkName);
}

const Xml::NodeLine::AutoSubscribeFreed _freed{&OnNodeFreed};

const Game::HookAutoRegister _end{Offsets::FUN_XML_END_ELEMENT, reinterpret_cast<void *>(&EndElement_h),
                                  reinterpret_cast<void **>(&g_origEnd)};
const Game::HookAutoRegister _compile{Offsets::FUN_FRAMESCRIPT_COMPILE_FUNCTION,
                                      reinterpret_cast<void *>(&Compile_h),
                                      reinterpret_cast<void **>(&g_origCompile)};

} // namespace

} // namespace Xml::ScriptChunkName
