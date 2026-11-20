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

// TOC directives in XML `file` attributes, as the 3.80.2 client applies them.
//
// 3.80.2 runs the TOC line processor (AddOns::TocRewrite::ProcessLine) on the
// `file` value of every `<Include>` and `<Script>` (FUN_1405c9f20 ->
// FUN_1405c9dd0): variables expand, and a failed condition skips that file.
// `[AllowLoadEnvironment]` passes only inside an addon's load.
//
// 1.12's XML file processor FUN_XML_LOAD_FILE reads both values with the node
// attribute getter FUN_XML_NODE_GET_ATTRIBUTE:
//   - <Include> (0x006EDFAF): a null value logs "Element 'Include' without
//     file attribute" (0x006EE017), so a skipped include returns the value
//     unchanged and the include's load call (0x006EE00D) is skipped instead.
//     Nothing runs between the two calls but the path join.
//   - <Script> (0x006EE051): a null value skips the file and still compiles
//     the element's inline text (0x006EE0D9), so a skipped script returns
//     null. Xml::ScriptPath then resolves the processed value as usual.
// Both processed values live in a static buffer; the engine copies each one
// into a local path buffer before anything can call the getter again.

#include "addons/LoadState.h"
#include "addons/TocRewrite.h"
#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstring>
#include <string>

namespace Xml::FileDirectives {

namespace {

using GetAttr_t = const char *(__thiscall *)(void *node, const char *name);
using LoadFile_t = void(__fastcall *)(const char *path, void *ctx, void *status);

const char *GetAttr(void *node, const char *name) {
    return reinterpret_cast<GetAttr_t>(Offsets::FUN_XML_NODE_GET_ATTRIBUTE)(node, name);
}

std::string g_value;
bool g_skipInclude = false;

// Runs the processor on `value`. Returns false when a condition fails;
// otherwise `*out` is the value to use.
bool Process(const char *value, const char **out) {
    *out = value;
    if (value == nullptr || std::strchr(value, '[') == nullptr) return true;
    if (!AddOns::TocRewrite::ProcessLine(value, std::strlen(value),
                                         AddOns::LoadState::AnyLoading(), g_value))
        return false;
    *out = g_value.c_str();
    return true;
}

// `__thiscall` getter call sites, as `__fastcall` with the unused EDX.
const char *__fastcall IncludeFile(void *node, void *, const char *name) {
    const char *value = GetAttr(node, name);
    const char *out = nullptr;
    g_skipInclude = !Process(value, &out);
    return g_skipInclude ? value : out;
}

void __fastcall IncludeLoad(const char *path, void *ctx, void *status) {
    if (g_skipInclude) {
        g_skipInclude = false;
        return;
    }
    reinterpret_cast<LoadFile_t>(Offsets::FUN_XML_LOAD_FILE)(path, ctx, status);
}

const char *__fastcall ScriptFile(void *node, void *, const char *name) {
    const char *out = nullptr;
    return Process(GetAttr(node, name), &out) ? out : nullptr;
}

void Register() {
    Common::RedirectCall(Offsets::CALL_XML_INCLUDE_GET_FILE,
                         Offsets::FUN_XML_NODE_GET_ATTRIBUTE,
                         reinterpret_cast<const void *>(&IncludeFile));
    Common::RedirectCall(Offsets::CALL_XML_INCLUDE_LOAD, Offsets::FUN_XML_LOAD_FILE,
                         reinterpret_cast<const void *>(&IncludeLoad));
    Common::RedirectCall(Offsets::CALL_XML_SCRIPT_GET_FILE,
                         Offsets::FUN_XML_NODE_GET_ATTRIBUTE,
                         reinterpret_cast<const void *>(&ScriptFile));
}

// GlueXML loads before the in-game registration pass; both passes patch.
const Game::GlueModuleAutoRegister _gluePatch{&Register};
const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Xml::FileDirectives
