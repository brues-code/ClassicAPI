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
// the upvalues of a call level as text, one `name=value` line each, as the
// 3.80.2 client's Script_debuglocals (0x1405C2DF0) and its value writer
// (0x1405B5450) build it:
//   - It answers at any time. (3.80.2 refuses only while Lua finalizers run, a
//     flag 1.12's Lua 5.0 does not have.)
//   - `level` counts as lua_getstack and debugstack do: 1 is the caller; less
//     than 1, or not a number, is 1. A level that does not exist gives "".
//   - Numbers (0x1405B52D0): a whole number as a 64-bit integer (%lld of the
//     truncated value, so one past that range prints its minimum), any other
//     as %.6f, an infinity or NaN as `Infinite`. Number keys, numeric-string
//     keys included, print the same way.
//   - A string is quoted, with no escaping, and ends at its first newline.
//   - A table's header is its ToDebugString method's result (looked up with
//     __index, called unprotected), else a frame's name or, unnamed, its type
//     name, else `<table>`. At the top level its fields follow, one space in,
//     between `{` and `}`; a table with no field to show is the header alone.
//     A table inside one is the header alone. A key that is neither a number
//     nor a string prints its bare type name.
//   - A function prints where it is defined.
//   - The second argument, which retail's Blizzard_ScriptErrors passes as
//     true, leaves out function and userdata values, at the top level and in
//     tables.
//   - The text stops at 4095 bytes; lines have no limit of their own.
// A frame's header ends in where it was created, `Name <file.lua:12>` (the
// base name, from Frame::SourceLocation; an XML frame has its file alone,
// as 1.12's XML nodes carry no line).
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
#include "frame/SourceLocation.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
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

constexpr size_t kTextMax = 4095; // the 4096-byte buffer, less its NUL

struct Packer {
    void *L;
    bool skipFunctionsAndUserdata;
    std::string text;

    void Put(const char *s, size_t n) {
        const size_t room = kTextMax - text.size();
        text.append(s, std::min(room, n));
    }
    void Put(const char *s) { Put(s, std::strlen(s)); }

    void Printf(const char *fmt, ...) {
        char buf[64];
        va_list args;
        va_start(args, fmt);
        const int n = std::vsnprintf(buf, sizeof buf, fmt, args);
        va_end(args);
        if (n > 0)
            Put(buf, std::min(static_cast<size_t>(n), sizeof buf - 1));
    }

    bool Skipped(int type) const {
        return skipFunctionsAndUserdata &&
               (type == Lua::TYPE_LIGHTUSERDATA || type == Lua::TYPE_USERDATA || type == Lua::TYPE_FUNCTION);
    }

    void Number(double v) {
        if (!std::isfinite(v)) {
            Put("Infinite");
            return;
        }
        double whole = 0;
        if (std::modf(v, &whole) != 0.0) {
            Printf("%.6f", v);
            return;
        }
        // cvttsd2si: a value past the 64-bit range is its minimum
        const long long n = (v >= -9223372036854775808.0 && v < 9223372036854775808.0)
                                ? static_cast<long long>(v)
                                : INT64_MIN;
        Printf("%lld", n);
    }

    // The table on the stack top's header.
    void TableHeader() {
        const int top = Lua::GetTop(L);
        Lua::PushString(L, "ToDebugString");
        Lua::GetTable(L, top); // __index applies
        if (Lua::Type(L, -1) == Lua::TYPE_FUNCTION) {
            Lua::PushValue(L, top);
            Lua::Call(L, 1, 1); // unprotected, as 3.80.2's
            if (Lua::Type(L, -1) == Lua::TYPE_STRING) {
                const char *header = Lua::ToString(L, -1);
                Put(header, Lua::StrLen(L, -1));
            }
            Lua::SetTop(L, top);
            return;
        }
        Lua::SetTop(L, top);
        // A script object's table holds it at [0] as a lightuserdata. Any
        // table can hold a full userdata there (`{[0] = newproxy()}`), which
        // ResolveObject would also return and the vtable calls below would
        // jump through.
        Lua::RawGetI(L, top, 0);
        const bool isObject = Lua::Type(L, -1) == Lua::TYPE_LIGHTUSERDATA;
        Lua::SetTop(L, top);
        if (void *object = isObject ? Lua::ResolveObject(L, -1) : nullptr) {
            const char *name = Lua::ObjectName(object);
            Put(name != nullptr ? name : Lua::FrameObjectType(object));
            // 3.80.2's " <file:line>": the base name of where it was created.
            if (const char *where = Frame::SourceLocation::Get(object)) {
                const char *base = where;
                for (const char *c = where; *c != '\0'; ++c)
                    if (*c == '\\' || *c == '/')
                        base = c + 1;
                Put(" <");
                Put(base);
                Put(">");
            }
            return;
        }
        Put("<table>");
    }

    // The key below the value on the stack top, as text.
    std::string KeyText() {
        if (Lua::IsNumber(L, -2)) {
            Packer key{L, false, {}};
            key.Number(Lua::ToNumber(L, -2));
            return key.text;
        }
        if (Lua::Type(L, -2) == Lua::TYPE_STRING)
            return std::string(Lua::ToString(L, -2), Lua::StrLen(L, -2));
        return Lua::TypeName(L, Lua::Type(L, -2));
    }

    // The fields of the table on the stack top, when one shows.
    void TableContents() {
        bool any = false;
        Lua::PushNil(L);
        while (Lua::Next(L, -2) != 0) {
            any = any || !Skipped(Lua::Type(L, -1));
            Lua::SetTop(L, -2);
        }
        if (!any)
            return;
        Put("{\n");
        Lua::PushNil(L);
        while (Lua::Next(L, -2) != 0) {
            if (!Skipped(Lua::Type(L, -1))) {
                const std::string key = KeyText();
                Put(" ");
                Put(key.data(), key.size());
                Put("=");
                Value(false);
                Put("\n");
            }
            Lua::SetTop(L, -2); // the value; the key stays for lua_next
        }
        Put("}");
    }

    void Function() {
        alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
        Lua::PushValue(L, -1);
        Lua::GetInfo(L, ">nS", ar); // pops the copy
        const auto field = [&ar](uintptr_t offset) { return Game::Read<const char *>(ar, offset); };
        const char *source = field(Offsets::OFF_LUA_DEBUG_SOURCE);
        const int line = Game::Read<int>(ar, Offsets::OFF_LUA_DEBUG_LINEDEFINED);
        const char *fn = field(Offsets::OFF_LUA_DEBUG_NAME);
        if (fn != nullptr && source != nullptr) {
            Put(fn);
            Put("() defined ");
        } else if (source != nullptr) {
            Put("<function> defined ");
        } else {
            Put("<function>");
            return;
        }
        Put(source);
        Printf(":%d", line);
    }

    // The value on the stack top; `contents` lists a table's fields.
    void Value(bool contents) {
        switch (Lua::Type(L, -1)) {
        case Lua::TYPE_NIL:
            Put("nil");
            break;
        case Lua::TYPE_BOOLEAN:
            Put(Lua::ToBoolean(L, -1) ? "true" : "false");
            break;
        case Lua::TYPE_LIGHTUSERDATA:
        case Lua::TYPE_USERDATA:
            Put("<userdata>");
            break;
        case Lua::TYPE_NUMBER:
            Number(Lua::ToNumber(L, -1));
            break;
        case Lua::TYPE_STRING: {
            const char *str = Lua::ToString(L, -1); // ends at a NUL, as strlen
            const char *newline = std::strchr(str, '\n');
            Put("\"");
            Put(str, newline != nullptr ? static_cast<size_t>(newline - str) : std::strlen(str));
            Put("\"");
            break;
        }
        case Lua::TYPE_TABLE:
            TableHeader();
            if (contents)
                TableContents();
            break;
        case Lua::TYPE_FUNCTION:
            Function();
            break;
        default:
            Printf("<%s>", Lua::TypeName(L, Lua::Type(L, -1)));
            break;
        }
    }

    // One `name=value` line for the value on the stack top.
    void Entry(const char *name) {
        if (Skipped(Lua::Type(L, -1)))
            return;
        Put(name);
        Put("=");
        Value(true);
        Put("\n");
    }
};

int __fastcall Script_debuglocals(void *L) {
    int level = Lua::IsNumber(L, 1) ? static_cast<int>(Lua::ToNumber(L, 1)) : 0;
    if (level <= 0)
        level = 1;
    Packer packer{L, Lua::ToBoolean(L, 2) != 0, {}};

    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    if (Lua::GetStack(L, level, ar)) {
        const char *name;
        for (int i = 1; (name = GetLocal(L, ar, i)) != nullptr; i++) {
            packer.Entry(name);
            Lua::SetTop(L, -2);
        }
        Lua::GetInfo(L, "f", ar); // the level's function
        for (int i = 1; (name = GetUpvalue(L, -1, i)) != nullptr; i++) {
            packer.Entry(name);
            Lua::SetTop(L, -2);
        }
        Lua::SetTop(L, -2);
    }
    Lua::PushLString(L, packer.text.data(), static_cast<unsigned int>(packer.text.size()));
    return 1;
}

const Game::Doc::Field kDebuglocalsArgs[] = {
    Game::Doc::Opt("level", "number", "1", "The call level, as debugstack counts it."),
    Game::Doc::Opt("skipFunctionsAndUserdata", "bool", "false",
                   "Leaves out function and userdata values."),
};
const Game::Doc::Field kDebuglocalsRets[] = {
    Game::Doc::Req("locals", "string", "One `name=value` line for each local and upvalue."),
};
const Game::Doc::Function kDebuglocals{
    "Returns the local variables and upvalues of a call level as text, for an error report.",
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
