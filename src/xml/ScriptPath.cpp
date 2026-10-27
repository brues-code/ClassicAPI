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
// 3.3.5's FrameXML_ProcessFile (0x00813EE0) joins `<Script file>` onto the
// XML file's directory exactly as it joins `<Include file>`, always.
//
// At the <Script> call into FUN_LUA_LOAD_FILE (return address
// RET_XML_SCRIPT_FILE_LOAD, seen through Script::LoadFile's shared hook),
// retry a backslash path that does not exist as given against the directory
// of the XML file being processed (Xml::FileStack). The retry is used only
// when the joined file exists, so a root-relative
// `<Script file="Interface\AddOns\X\y.lua"/>` that works on vanilla keeps
// working, and a genuinely missing file still fails with the engine's own
// message.
//
// The directory is taken from the XML path the way the engine takes it for
// its own joins: FUN_XML_LOAD_FILE first collapses each `\..\` in the path
// (dropping the directory before it) into a local copy, and joins onto that.
// The tracked pointer is the caller's uncollapsed path, so Resolve runs the
// same collapse on the directory before joining.

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"
#include "script/LoadFile.h"
#include "xml/FileStack.h"

#include <cstring>

namespace Xml::ScriptPath {

namespace {

bool FileExists(const char *path) {
    return reinterpret_cast<AddOns::EngineIO::FileExistsFn>(
               static_cast<uintptr_t>(Offsets::FUN_FILE_EXISTS))(path, 1) != 0;
}

bool IsSep(char c) { return c == '\\' || c == '/'; }

// The engine's `..` collapse (FUN_XML_LOAD_FILE's prologue; 3.3.5's
// FrameXML_ProcessFile is the same loop): while the path holds a `..` with a
// separator on both sides, drop it and the directory before it. A `..` not
// between separators ends the loop, as in the engine.
void CollapseDotDot(char *s) {
    for (char *dots = std::strstr(s, ".."); dots != nullptr; dots = std::strstr(s, "..")) {
        if (dots == s || !IsSep(dots[-1]) || !IsSep(dots[2]))
            break;
        char *dir = dots - 2;
        while (dir >= s && !IsSep(*dir))
            --dir;
        std::memmove(dir + 1, dots + 3, std::strlen(dots + 3) + 1);
    }
}

// For the XML `<Script file>` call site only: a `file=` value that contains a
// subdirectory and does not exist as given is retried relative to the XML
// file being processed; the joined path is returned when that file exists.
const char *Resolve(const char *path, uintptr_t retAddr, char *buf, size_t bufSize) {
    if (retAddr != Offsets::RET_XML_SCRIPT_FILE_LOAD || path == buf)
        return path;
    if (std::strchr(path, '\\') == nullptr)
        return path; // the engine already joined it onto the XML's directory
    if (FileExists(path))
        return path; // root-relative, as vanilla loads it

    const char *xml = Xml::FileStack::Current();
    const char *slash = xml ? std::strrchr(xml, '\\') : nullptr;
    if (slash == nullptr)
        return path;
    const size_t dirLen = static_cast<size_t>(slash - xml) + 1;
    const size_t pathLen = std::strlen(path);
    if (dirLen + pathLen + 1 > bufSize)
        return path;
    std::memcpy(buf, xml, dirLen);
    buf[dirLen] = '\0';
    CollapseDotDot(buf);
    std::memcpy(buf + std::strlen(buf), path, pathLen + 1);
    return FileExists(buf) ? buf : path;
}

const Script::LoadFile::AutoRewrite _rewrite{&Resolve};

} // namespace

} // namespace Xml::ScriptPath
