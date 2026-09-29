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

// `<Script file="sub\dir\File.lua"/>` relative to the XML file, as later
// clients resolve it.
//
// The XML file processor FUN_XML_LOAD_FILE joins an `<Include file>` onto the
// including file's directory unconditionally (0x006EDFBA..0x006EE00D), but a
// `<Script file>` only when the value has NO backslash: it runs
// strchr(file, '\\') at 0x006EE06E and, on a hit, loads the value as given
// (0x006EE079 -> 0x006EE0C7), i.e. relative to the game root. So the common
// `<Script file="widgets\AceGUIWidget-Button.lua"/>` inside
// Libs\AceGUI-3.0\AceGUI-3.0.xml loads "widgets\AceGUIWidget-Button.lua",
// which does not exist, and FrameXML.log reports "Error loading
// widgets\AceGUIWidget-Button.lua" - every AceGUI-3.0 widget in every addon
// that embeds it, plus embeds.xml lines like `libs\LibStub\LibStub.lua`.
//
// Track the XML file being processed (co-hook of FUN_XML_LOAD_FILE, which
// recurses for <Include>, hence a stack) and, at the <Script> call into
// FUN_LUA_LOAD_FILE (return address RET_XML_SCRIPT_FILE_LOAD), retry a
// backslash path that does not exist as given against that file's directory.
// The retry is used only when the joined file exists, so a root-relative
// `<Script file="Interface\AddOns\X\y.lua"/>` that works on vanilla keeps
// working, and a genuinely missing file still fails with the engine's own
// message. FUN_LUA_LOAD_FILE itself is co-hooked by AddOns::SavedVarsFirst,
// which calls Resolve() first.

#include "xml/ScriptPath.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"

#include <cstring>

namespace Xml::ScriptPath {

namespace {

using XmlLoadFile_t = uintptr_t(__fastcall *)(const char *path, void *ctx, void *status);

XmlLoadFile_t g_origXmlLoadFile = nullptr;

// XML files currently being processed, innermost last. <Include> nesting is
// shallow; deeper levels still count so pops stay balanced, they just are not
// consulted.
constexpr int kMaxDepth = 16;
const char *g_xmlStack[kMaxDepth];
int g_depth = 0;

bool FileExists(const char *path) {
    return reinterpret_cast<AddOns::EngineIO::FileExistsFn>(
               static_cast<uintptr_t>(Offsets::FUN_FILE_EXISTS))(path, 1) != 0;
}

uintptr_t __fastcall XmlLoadFile_h(const char *path, void *ctx, void *status) {
    if (g_depth < kMaxDepth)
        g_xmlStack[g_depth] = path;
    ++g_depth;
    const uintptr_t r = g_origXmlLoadFile(path, ctx, status);
    --g_depth;
    return r;
}

const Game::HookAutoRegister _xmlLoadFileHook{
    Offsets::FUN_XML_LOAD_FILE,
    reinterpret_cast<void *>(&XmlLoadFile_h),
    reinterpret_cast<void **>(&g_origXmlLoadFile)};

} // namespace

const char *Resolve(const char *path, uintptr_t retAddr, char *buf, size_t bufSize) {
    if (path == nullptr || retAddr != Offsets::RET_XML_SCRIPT_FILE_LOAD)
        return path;
    if (g_depth <= 0 || g_depth > kMaxDepth)
        return path;
    if (std::strchr(path, '\\') == nullptr)
        return path; // the engine already joined it onto the XML's directory
    if (FileExists(path))
        return path; // root-relative, as vanilla loads it

    const char *xml = g_xmlStack[g_depth - 1];
    const char *slash = xml ? std::strrchr(xml, '\\') : nullptr;
    if (slash == nullptr)
        return path;
    const size_t dirLen = static_cast<size_t>(slash - xml) + 1;
    const size_t pathLen = std::strlen(path);
    if (dirLen + pathLen + 1 > bufSize)
        return path;
    std::memcpy(buf, xml, dirLen);
    std::memcpy(buf + dirLen, path, pathLen + 1);
    return FileExists(buf) ? buf : path;
}

} // namespace Xml::ScriptPath
