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

// `debuglocals([level [, skipFunctionsAndUserdata]])` — the locals and then
// the upvalues of a call level as text, one `name=value` line each, for an
// error report. 4.3.4's Script_debuglocals and the FrameScript_PackVariable
// it formats with, in retail's spacing (probed: `n=7`, `s="hi"`,
// `t=<table>{`, ` k=1`, `}`; 4.3.4 writes `n = 7` and `t = <table> {`):
//   - Only while an error handler runs; otherwise it returns nothing. 4.3.4
//     tests a flag its error function sets around the handler call. 1.12's
//     error function sets none, but while a handler runs that function's
//     level is on the stack (ErrorCallstack::ErrorFunctionLevel).
//   - `level` counts as lua_getstack and debugstack do: 1 is the caller.
//     Less than 1 is 1.
//   - A table prints as `name=<table>{`, or with the frame's name for a
//     frame (t[0], as FrameScript_GetObject reads it), then its fields one
//     space in at the top level only, then `}`. A function prints where it
//     is defined. The text stops at 4095 bytes, each line at 1023.
// Retail's second argument, which its Blizzard_ScriptErrors passes as true,
// leaves out the function and userdata values, at the top level and in
// tables.
//
// A number key in int range prints truncated with %d, as 4.3.4's
// IsIntegerNumber tests only the range: 2.5 prints as 2. Two departures from
// 4.3.4, both where it would fail inside the handler: a number key outside
// that range is formatted from a copy, not converted in place (that breaks
// lua_next), and a key that is not a number or string prints as its type in
// angle brackets (4.3.4 formats a null string).
//
// Retail also lists `(*temporary)` lines: 5.1's lua_getlocal names each stack
// slot past a level's locals so, a C level's arguments included (an
// `error("x")` report shows `(*temporary)="x"`). 1.12's lua_getlocal
// (0x6FBB20) is 5.0's, which names declared locals only. Those slots are
// the compiler's registers, which 5.0 and 5.1 allocate differently, so they
// are left out rather than imitated.
//
// Registered on both Lua states, like the base-library helpers.

#include "Game.h"
#include "Offsets.h"
#include "baselib/ErrorCallstack.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace BaseLib::DebugLocals {

namespace {

namespace Lua = Game::Lua;

using lua_getlocal_t = const char *(__fastcall *)(void *L, const void *ar, int n);
using lua_getupvalue_t = const char *(__fastcall *)(void *L, int funcindex, int n);

const auto GetLocal = reinterpret_cast<lua_getlocal_t>(Offsets::LUA_GET_LOCAL);
const auto GetUpvalue = reinterpret_cast<lua_getupvalue_t>(Offsets::LUA_GET_UPVALUE);

constexpr size_t kTextMax = 4095; // 4.3.4's 4096-byte buffer, less its NUL
constexpr size_t kLineSize = 1024;

struct Packer {
    void *L;
    bool skipFunctionsAndUserdata;
    std::string text;

    void Line(const char *fmt, const char *indent, const char *name, const char *value = "",
              int number = 0) {
        char line[kLineSize];
        std::snprintf(line, sizeof line, fmt, indent, name, value, number);
        const size_t room = kTextMax - text.size();
        text.append(line, std::min(room, std::strlen(line)));
    }

    // The value on the stack top, as 4.3.4's FrameScript_PackVariable.
    void Pack(const char *name, int depth) {
        const char *indent = depth > 0 ? " " : "";
        switch (Lua::Type(L, -1)) {
        case Lua::TYPE_NIL:
            Line("%s%s=nil\n", indent, name);
            break;
        case Lua::TYPE_BOOLEAN:
            Line("%s%s=%s\n", indent, name, Lua::ToBoolean(L, -1) ? "true" : "false");
            break;
        case Lua::TYPE_LIGHTUSERDATA:
        case Lua::TYPE_USERDATA:
            if (!skipFunctionsAndUserdata)
                Line("%s%s=<userdata>\n", indent, name);
            break;
        case Lua::TYPE_NUMBER:
            Line("%s%s=%s\n", indent, name, Lua::ToString(L, -1)); // a copy: safe to convert
            break;
        case Lua::TYPE_STRING:
            Line("%s%s=\"%s\"\n", indent, name, Lua::ToString(L, -1));
            break;
        case Lua::TYPE_TABLE:
            PackTable(name, depth, indent);
            break;
        case Lua::TYPE_FUNCTION:
            if (!skipFunctionsAndUserdata)
                PackFunction(name, indent);
            break;
        default:
            Line("%s%s=<%s>\n", indent, name, Lua::TypeName(L, Lua::Type(L, -1)));
            break;
        }
    }

    void PackTable(const char *name, int depth, const char *indent) {
        void *object = Lua::ResolveObject(L, -1);
        Line("%s%s=%s{\n", indent, name, object ? Lua::ObjectDisplayName(object) : "<table>");
        if (depth == 0) {
            Lua::PushNil(L);
            while (Lua::Next(L, -2) != 0) {
                char key[32];
                const char *keyName = key;
                const int keyType = Lua::Type(L, -2);
                const double number = keyType == Lua::TYPE_NUMBER ? Lua::ToNumber(L, -2) : 0;
                if (keyType == Lua::TYPE_NUMBER && std::isfinite(number) && number >= -2147483648.0 &&
                    number <= 2147483647.0) {
                    std::snprintf(key, sizeof key, "%d", static_cast<int>(number));
                } else if (keyType == Lua::TYPE_STRING) {
                    keyName = Lua::ToString(L, -2);
                } else if (keyType == Lua::TYPE_NUMBER) {
                    Lua::PushValue(L, -2);
                    std::snprintf(key, sizeof key, "%s", Lua::ToString(L, -1));
                    Lua::SetTop(L, -2);
                } else {
                    std::snprintf(key, sizeof key, "<%s>", Lua::TypeName(L, keyType));
                }
                Pack(keyName, 1);
                Lua::SetTop(L, -2); // the value; the key stays for lua_next
            }
        }
        Line("%s}\n", indent, "");
    }

    void PackFunction(const char *name, const char *indent) {
        alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
        Lua::PushValue(L, -1);
        Lua::GetInfo(L, ">nS", ar); // pops the copy
        const auto field = [&ar](uintptr_t offset) { return Game::Read<const char *>(ar, offset); };
        const char *source = field(Offsets::OFF_LUA_DEBUG_SOURCE);
        const int line = Game::Read<int>(ar, Offsets::OFF_LUA_DEBUG_LINEDEFINED);
        if (const char *fn = field(Offsets::OFF_LUA_DEBUG_NAME)) {
            char named[kLineSize];
            std::snprintf(named, sizeof named, "%s() defined %s", fn, source ? source : "?");
            Line("%s%s=%s:%d\n", indent, name, named, line);
        } else {
            Line("%s%s=<function> defined %s:%d\n", indent, name, source ? source : "?", line);
        }
    }
};

int __fastcall Script_debuglocals(void *L) {
    if (ErrorCallstack::ErrorFunctionLevel(L) == 0)
        return 0;
    int level = static_cast<int>(Lua::ToNumber(L, 1));
    if (level <= 0)
        level = 1;
    Packer packer{L, Lua::ToBoolean(L, 2) != 0, {}};

    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    if (Lua::GetStack(L, level, ar)) {
        const char *name;
        for (int i = 1; (name = GetLocal(L, ar, i)) != nullptr; i++) {
            packer.Pack(name, 0);
            Lua::SetTop(L, -2);
        }
        Lua::GetInfo(L, "f", ar); // the level's function
        for (int i = 1; (name = GetUpvalue(L, -1, i)) != nullptr; i++) {
            packer.Pack(name, 0);
            Lua::SetTop(L, -2);
        }
        Lua::SetTop(L, -2);
    }
    Lua::PushLString(L, packer.text.data(), static_cast<unsigned int>(packer.text.size()));
    return 1;
}

const Game::Doc::Field kDebuglocalsArgs[] = {
    Game::Doc::Opt("level", "number", "1", "The call level, as debugstack counts it."),
    Game::Doc::Opt("skipFunctionsAndUserdata", "boolean", "false",
                   "Leaves out function and userdata values."),
};
const Game::Doc::Field kDebuglocalsRets[] = {
    Game::Doc::Opt("locals", "string", nullptr,
                   "One `name=value` line for each local and upvalue. Nothing outside an "
                   "error handler."),
};
const Game::Doc::Function kDebuglocals{
    "The local variables and upvalues of a call level as text, for an error report.",
    kDebuglocalsArgs, kDebuglocalsRets, "LuaGlobals"};

void RegisterInGame() {
    Game::Lua::RegisterGlobalFunction("debuglocals", &Script_debuglocals, &kDebuglocals);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("debuglocals", &Script_debuglocals, &kDebuglocals);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::DebugLocals
