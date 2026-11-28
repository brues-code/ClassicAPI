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

// `region:GetSourceLocation()` -> "file:line" | nil, and the per-region
// record behind it.
//
// Retail (9.2.5+) answers "path\to\script.lua:10" for a frame a script
// created and the XML file and line for one XML defined. 1.12 has both
// inputs, through the engine's own means:
//   - the script: Lua 5.0's lua_getstack / lua_getinfo "Sl" on the state
//     inside Script_CreateFrame (Frame::CreateFrame holds a LuaScope across
//     the engine's builder), the way luaL_where names the caller of a C
//     function for an error message. Level 1 is the caller; a C level in
//     between (pcall, xpcall) is stepped over, and so is a chunk compiled
//     from a string when a file's level is above it (CaptureCaller). The
//     file is the chunk's name as luaO_chunkid writes it, with room for a
//     whole path, where error messages cut at LUA_IDSIZE (60).
//   - the XML file and line: Xml::NodeLine, the file and opening-tag line of
//     the element, as 3.80.2 records it ("file:line").
// Nesting resolves by depth: a frame built while the innermost CreateFrame
// call is at the XML depth it started at is that call's; one built deeper
// belongs to an XML file loaded since (LoadAddOn from an OnLoad). A
// CreateFrame call inside an OnLoad during an XML load is innermost, so its
// frames are the script's.
//
// The record is keyed by the object. A frame's is filled from the frame node
// loader (Xml::FrameLoad, before the engine loads the node, so OnLoad can
// already ask). A texture's or font string's comes from its XML node
// (Xml::TypeLoad) or from the script CreateTexture / CreateFontString call
// that made it. Each is dropped by its destructor (Frame::Destruction,
// Frame::RegionDestruction). A region with neither (one the client's own C
// code makes) answers for its parent frame, as 3.80.2's constructors copy the
// parent's record (FUN_1405850A0, FUN_1405ACAE0).
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
#include "frame/RegionDestruction.h"
#include "xml/FileStack.h"
#include "xml/FrameLoad.h"
#include "xml/NodeLine.h"
#include "xml/TypeLoad.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frame::SourceLocation {

namespace {

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
// L's call stack that is not in an excluded file, preferring one loaded from
// a file. False when there is none.
//
// 3.80.2 records a location as a file id and a line (FUN_1405C7870 formats
// object +0x124), and a chunk compiled from a string has no file: a frame a
// `/run` line makes reports the slash command file that ran the line
// (SlashCommands.lua). 1.12 names a file's chunk "@path" (FUN_LUA_LOAD_FILE's
// format at 0x008716E0) and a string's after its text, so the walk takes the
// first level whose source starts with '@'. With none (an XML handler the
// engine runs, a macro's /run), the nearest level answers, as
// `[string "..."]:line`.
bool CaptureCaller(void *L, char *out, size_t size) {
    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    char nearest[320] = "";
    for (int level = 1; level < 32; ++level) {
        if (!Game::Lua::GetStack(L, level, ar) || !Game::Lua::GetInfo(L, "Sl", ar))
            break;
        const int line = Game::Read<int>(ar, Offsets::OFF_LUA_DEBUG_CURRENTLINE);
        if (line <= 0)
            continue; // a C function: no source of its own
        const char *source = Game::Read<const char *>(ar, Offsets::OFF_LUA_DEBUG_SOURCE);
        if (source == nullptr)
            break;
        if (Excluded(source))
            continue;
        char name[256];
        reinterpret_cast<ChunkId_t>(Offsets::LUAO_CHUNKID)(name, source, sizeof(name));
        if (source[0] == '@') {
            std::snprintf(out, size, "%s:%d", name, line);
            return true;
        }
        if (nearest[0] == '\0')
            std::snprintf(nearest, sizeof nearest, "%s:%d", name, line);
    }
    if (nearest[0] == '\0')
        return false;
    std::snprintf(out, size, "%s", nearest);
    return true;
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

// "file:line" of an XML element (Xml::NodeLine), or false for a node no XML
// file built.
bool NodeLocation(const void *node, char *out, size_t size) {
    const Xml::NodeLine::Location *loc = Xml::NodeLine::Get(node);
    if (loc == nullptr)
        return false;
    std::snprintf(out, size, "%s:%d", loc->file, loc->line);
    return true;
}

// 3.80.2 sets a region's location from each node it loads onto it (the
// region XML load FUN_1405938A0 calls vtable +0x80, FUN_140595170 =
// SetSourceLocation(node + 0x70)), templates first and the region's own node
// last, so the region's own element is what stays.
//
// Frames: the first node the frame loader enters for a frame is its own
// element; inherited template nodes re-enter it inside that call.
void OnFrameNode(void *object, const void *node, void *) {
    if (g_locations.find(object) != g_locations.end())
        return;
    if (const char *location = FromLuaScope()) {
        g_locations.emplace(object, location);
        return;
    }
    char location[320];
    if (NodeLocation(node, location, sizeof location))
        g_locations.emplace(object, location);
}

// Textures and font strings: the outermost load is the region's own node (a
// template's loads nest inside it), or, for a script call with a template,
// the template node, which 3.80.2's script create (FUN_1405ACDB0) also leaves
// in place of the caller's line.
template <Xml::TypeLoad::Loader K> void OnRegionNode(void *region, const void *node, void *) {
    if (Xml::TypeLoad::Nested(K))
        return;
    char location[320];
    if (NodeLocation(node, location, sizeof location))
        g_locations[region] = location;
}

// Script CreateTexture / CreateFontString (and CreateLine / CreateMaskTexture,
// which call the texture handler): the calling script's line, as 3.80.2's
// script create gives a region with no template (FUN_1405B7000, then vtable
// +0x78). A template's node, recorded inside the call, is kept.
using ScriptCreate_t = int(__fastcall *)(void *L);
ScriptCreate_t g_origCreateTexture = nullptr;
ScriptCreate_t g_origCreateFontString = nullptr;

int ScriptCreateRegion(ScriptCreate_t original, void *L) {
    char location[320];
    const bool captured = CaptureCaller(L, location, sizeof location);
    const int pushed = original(L);
    if (captured && pushed > 0) {
        if (void *region = Game::Lua::ResolveRegion(L, -1, false))
            g_locations.emplace(region, location);
    }
    return pushed;
}

int __fastcall CreateTexture_h(void *L) { return ScriptCreateRegion(g_origCreateTexture, L); }
int __fastcall CreateFontString_h(void *L) { return ScriptCreateRegion(g_origCreateFontString, L); }

void OnFrameDestroyed(void *frame) { g_locations.erase(frame); }
void OnRegionDestroyed(void *region) { g_locations.erase(region); }

const Xml::FrameLoad::AutoSubscribeBefore _load{&OnFrameNode};
const Xml::TypeLoad::AutoSubscribe _textureLoad{Xml::TypeLoad::TEXTURE, &OnRegionNode<Xml::TypeLoad::TEXTURE>};
const Xml::TypeLoad::AutoSubscribe _fontStringLoad{Xml::TypeLoad::FONT_STRING,
                                                   &OnRegionNode<Xml::TypeLoad::FONT_STRING>};
const Frame::Destruction::AutoSubscribe _destroy{&OnFrameDestroyed};
const Frame::RegionDestruction::AutoSubscribe _regionDestroy{&OnRegionDestroyed};
const Game::HookAutoRegister _createTexture{Offsets::FUN_SCRIPT_CREATE_TEXTURE,
                                            reinterpret_cast<void *>(&CreateTexture_h),
                                            reinterpret_cast<void **>(&g_origCreateTexture)};
const Game::HookAutoRegister _createFontString{Offsets::FUN_SCRIPT_CREATE_FONTSTRING,
                                               reinterpret_cast<void *>(&CreateFontString_h),
                                               reinterpret_cast<void **>(&g_origCreateFontString)};

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
                   "\"file:line\" of the call that created the region, or of the XML element "
                   "that defines it; nil when neither did."),
};
const Game::Doc::Function kGetSourceLocation{
    "Returns where the region was created: the script file and line of the call that "
    "created it, or the XML file and line of its element.",
    {}, kRets};
const Game::Doc::Method g_methodDocs[] = {
    {"GetSourceLocation", &kGetSourceLocation},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("AddSourceLocationExclude", &Script_AddSourceLocationExclude,
                                      &kAddSourceLocationExclude);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods, g_methodDocs);
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
        // Only a region's +0x9C is a parent pointer. A debuglocals() dump can
        // hand any table's [0] object here (a font, say), where the field is
        // something else entirely; walking from it reads a wild vtable.
        if (!Game::Lua::IsA(o, Offsets::VAR_REGION_LUA_TYPE_ID))
            return nullptr;
        o = Game::Read<void *>(o, Offsets::OFF_REGION_PARENT);
    }
    return nullptr;
}

} // namespace Frame::SourceLocation
