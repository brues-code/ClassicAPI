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

// `region:GetSourceLocation()` -> "file:line" | "file.xml" | nil, and the
// per-frame record behind it.
//
// Retail (9.2.5+) answers "path\to\script.lua:10" for a frame a script
// created and the XML file and line for one XML defined. 1.12 has both
// inputs, through the engine's own means:
//   - the script: Lua 5.0's lua_getstack / lua_getinfo "Sl" on the state
//     inside Script_CreateFrame (Frame::CreateFrame holds a LuaScope across
//     the engine's builder), the way luaL_where names the caller of a C
//     function for an error message. Level 1 is the caller; a C level in
//     between (pcall, xpcall) is stepped over. The file is the chunk's name
//     as luaO_chunkid writes it, so a `/run` or loadstring chunk reads
//     `[string "..."]:1` here as in its errors — but with room for a whole
//     path, where error messages cut at LUA_IDSIZE (60).
//   - the XML file: Xml::FileStack, the file FUN_XML_LOAD_FILE is processing.
//     Vanilla's XML nodes carry no line, so an XML frame reports the file
//     alone.
// Nesting resolves by depth: a frame built while the innermost CreateFrame
// call is at the XML depth it started at is that call's; one built deeper
// belongs to an XML file loaded since (LoadAddOn from an OnLoad). A
// CreateFrame call inside an OnLoad during an XML load is innermost, so its
// frames are the script's.
//
// The record is keyed by the frame object, filled from the frame node
// loader (Xml::FrameLoad, before the engine loads the node, so OnLoad can
// already ask), and dropped by the frame destructor (Frame::Destruction).
// Textures and font strings have no record of their own; Get walks to their
// parent frame — retail's behavior for script-created regions.
//
// AddSourceLocationExclude(fileName) names a script file whose levels the
// walk steps over, so a frame a factory file makes (retail's FrameFactory.lua
// and Pools.lua exclude themselves) is the caller's. A level matches when its
// file name, the part of its path after the last separator, is fileName,
// ignoring case. The names are dropped with the Lua state that added them.
//
// Always on: retail gates the file-name lookup behind the
// enableSourceLocationLookup CVar ("UnknownFile" otherwise), a cost its Lua
// has and 5.0's two field reads do not.

#include "frame/SourceLocation.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/Destruction.h"
#include "xml/FileStack.h"
#include "xml/FrameLoad.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frame::SourceLocation {

namespace {

using GetStack_t = int(__fastcall *)(void *L, int level, void *ar);
using GetInfo_t = int(__fastcall *)(void *L, const char *what, void *ar);
using ChunkId_t = void(__fastcall *)(char *out, const char *source, int bufflen);

// Per-frame location, keyed by the frame object.
std::unordered_map<void *, std::string> g_locations;

// Open CreateFrame calls, innermost last. `xmlDepth` is Xml::FileStack's
// depth when the call began.
struct Scope {
    char location[320]; // "" when no Lua level was found
    int xmlDepth;
};
constexpr int kMaxScopes = 32; // CreateFrame -> OnLoad -> CreateFrame nests shallowly
Scope g_scopes[kMaxScopes];
int g_scopeDepth = 0;

std::vector<std::string> g_excludes;

bool Excluded(const char *source) {
    if (g_excludes.empty())
        return false;
    const char *name = source;
    for (const char *p = source; *p != '\0'; ++p)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    for (const std::string &e : g_excludes)
        if (Game::StrCmpI(name, e.c_str()) == 0)
            return true;
    return false;
}

// "file:line" of the nearest Lua function above the running C function on
// L's call stack that is not in an excluded file. False when there is none.
bool CaptureCaller(void *L, char *out, size_t size) {
    auto getStack = reinterpret_cast<GetStack_t>(Offsets::LUA_GET_STACK);
    auto getInfo = reinterpret_cast<GetInfo_t>(Offsets::LUA_GET_INFO);
    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    for (int level = 1; level < 32; ++level) {
        if (!getStack(L, level, ar) || !getInfo(L, "Sl", ar))
            return false;
        const int line = Game::Read<int>(ar, Offsets::OFF_LUA_DEBUG_CURRENTLINE);
        if (line <= 0)
            continue; // a C function: no source of its own
        const char *source = Game::Read<const char *>(ar, Offsets::OFF_LUA_DEBUG_SOURCE);
        if (source == nullptr)
            return false;
        if (Excluded(source))
            continue;
        char name[256];
        reinterpret_cast<ChunkId_t>(Offsets::LUAO_CHUNKID)(name, source, sizeof(name));
        std::snprintf(out, size, "%s:%d", name, line);
        return true;
    }
    return false;
}

// The innermost CreateFrame call's location, when the frames being built
// are its own (no XML file has been opened since it began).
const char *FromLuaScope() {
    if (g_scopeDepth <= 0 || g_scopeDepth > kMaxScopes)
        return nullptr;
    const Scope &top = g_scopes[g_scopeDepth - 1];
    if (Xml::FileStack::Depth() != top.xmlDepth || top.location[0] == '\0')
        return nullptr;
    return top.location;
}

void OnFrameNode(void *object, const void *, void *) {
    if (g_locations.find(object) != g_locations.end())
        return; // inherited nodes re-enter the loader for the same frame
    const char *location = FromLuaScope();
    if (location == nullptr)
        location = Xml::FileStack::Current();
    if (location != nullptr)
        g_locations.emplace(object, location);
}

void OnFrameDestroyed(void *frame) { g_locations.erase(frame); }

const Xml::FrameLoad::AutoSubscribeBefore _load{&OnFrameNode};
const Frame::Destruction::AutoSubscribe _destroy{&OnFrameDestroyed};

int __fastcall Script_GetSourceLocation(void *L) {
    void *self = Game::Lua::ResolveRegion(L);
    if (self == nullptr)
        return 0;
    const char *location = Get(self);
    if (location == nullptr)
        return 0;
    Game::Lua::PushString(L, location);
    return 1;
}

int __fastcall Script_AddSourceLocationExclude(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: AddSourceLocationExclude(fileName)");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    for (const std::string &e : g_excludes)
        if (Game::StrCmpI(name, e.c_str()) == 0)
            return 0;
    g_excludes.emplace_back(name);
    return 0;
}

void PrepareForReload() { g_excludes.clear(); }

const Game::ReloadAutoRegister _reload{&PrepareForReload};

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetSourceLocation", &Script_GetSourceLocation},
};

const Game::Doc::Field kExcludeArgs[] = {
    Game::Doc::Req("fileName", "string", "A script file name, such as \"FrameFactory.lua\"."),
};
const Game::Doc::Function kAddSourceLocationExclude{
    "Makes GetSourceLocation skip the named script file and report the code that called into it.",
    kExcludeArgs, {}, "FrameScriptGlobals"};

const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("location", "string", nullptr,
                   "\"file:line\" of the CreateFrame call that made the frame, or the XML file "
                   "that defines it; nil when neither did."),
};
const Game::Doc::Function kGetSourceLocation{
    "Returns where the region was created: the script file and line of the CreateFrame "
    "call, or the XML file. A texture or font string answers for its parent frame.",
    {}, kRets};
const Game::Doc::Method g_methodDocs[] = {
    {"GetSourceLocation", &kGetSourceLocation},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("AddSourceLocationExclude", &Script_AddSourceLocationExclude,
                                      &kAddSourceLocationExclude);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

LuaScope::LuaScope(void *L) {
    if (g_scopeDepth < kMaxScopes) {
        Scope &s = g_scopes[g_scopeDepth];
        if (!CaptureCaller(L, s.location, sizeof(s.location)))
            s.location[0] = '\0';
        s.xmlDepth = Xml::FileStack::Depth();
    }
    ++g_scopeDepth;
}

LuaScope::~LuaScope() {
    if (g_scopeDepth > 0)
        --g_scopeDepth;
}

const char *Get(void *object) {
    for (void *o = object; o != nullptr;) {
        const auto it = g_locations.find(o);
        if (it != g_locations.end())
            return it->second.c_str();
        if (Game::Lua::IsA(o, Offsets::VAR_FRAME_LUA_TYPE_ID))
            return nullptr; // a frame answers only for itself
        o = Game::Read<void *>(o, Offsets::OFF_REGION_PARENT);
    }
    return nullptr;
}

} // namespace Frame::SourceLocation
