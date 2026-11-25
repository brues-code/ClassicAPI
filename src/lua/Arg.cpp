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

#include "lua/Arg.h"

#include "Game.h"
#include "Offsets.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>


namespace Game::Lua::Arg {

namespace {

// Where a read is: the stack slot, and the field it holds, if any.
struct Slot {
    int index;
    const char *field;
};

// Records the error, then raises it, or leaves it in `error` and returns false.
bool Fail(void *L, const Slot &at, const char *what, const char *usage, Error *error) {
    Error local;
    Error &e = error != nullptr ? *error : local;
    e.text[0] = '\0';
    e.arg = at.index;
    e.field = at.field;
    std::snprintf(e.what, sizeof e.what, "%s", what != nullptr ? what : "");
    e.usage = usage;
    if (error == nullptr)
        Raise(L, e);
    return false;
}

// The longest message RaiseNamed writes (it is not cut to kErrorCap). A fixed
// buffer, as the raise is a longjmp that would leak a std::string.
constexpr std::size_t kMessageMax = 512;

// `e`'s message with the function named `name`.
void Message(const Error &e, const char *name, char (&out)[kMessageMax]) {
    if (e.arg == 0) {
        std::snprintf(out, sizeof out, "%s", e.text);
        return;
    }
    char detail[kMessageMax];
    if (e.what[0] != '\0')
        std::snprintf(detail, sizeof detail, "%s - %s", e.what, e.usage);
    else
        std::snprintf(detail, sizeof detail, "%s", e.usage);
    if (e.field != nullptr)
        std::snprintf(out, sizeof out, "bad argument #%d to '%s' (Current Field: [%s], %s)", e.arg, name, e.field,
                      detail);
    else
        std::snprintf(out, sizeof out, "bad argument #%d to '%s' (%s)", e.arg, name, detail);
}

// 1 when there is a value to read, 0 when there is nothing to read (missing
// and optional), -1 when the read failed (missing and required).
int Present(void *L, const Slot &at, const char *usage, bool optional, Error *error) {
    if (Type(L, at.index) > TYPE_NIL)
        return 1;
    if (optional)
        return 0;
    Fail(L, at, nullptr, usage, error);
    return -1;
}

bool ReadStringView(void *L, const Slot &at, const char *usage, Text &out, bool optional, Error *error) {
    const int present = Present(L, at, usage, optional, error);
    if (present <= 0)
        return present == 0;
    if (!IsString(L, at.index))
        return Fail(L, at, nullptr, usage, error);
    out.data = ToString(L, at.index); // converts a number in place, as lua_tolstring does
    out.size = StrLen(L, at.index);
    return true;
}

bool ReadString(void *L, const Slot &at, const char *usage, Text &out, bool optional, Error *error) {
    if (!ReadStringView(L, at, usage, out, optional, error))
        return false;
    if (out.data != nullptr)
        out.size = std::strlen(out.data);
    return true;
}

bool ReadFiniteNumber(void *L, const Slot &at, const char *usage, double &out, bool optional, Error *error) {
    const int present = Present(L, at, usage, optional, error);
    if (present <= 0)
        return present == 0;
    const double v = IsNumber(L, at.index) ? ToNumber(L, at.index) : NAN;
    if (!std::isfinite(v))
        return Fail(L, at, "must be a finite number", usage, error);
    out = v;
    return true;
}

bool ReadFiniteFloat(void *L, const Slot &at, const char *usage, float &out, bool optional, Error *error) {
    double v = 0.0;
    if (!ReadFiniteNumber(L, at, usage, v, optional, error))
        return false;
    if (Type(L, at.index) <= TYPE_NIL)
        return true; // optional and absent
    if (!(v >= -FLT_MAX && v <= FLT_MAX)) {
        char what[96];
        std::snprintf(what, sizeof what, "outside of expected range %e to %e", -FLT_MAX, FLT_MAX);
        return Fail(L, at, what, usage, error);
    }
    out = static_cast<float>(v);
    return true;
}

bool ReadUInt32(void *L, const Slot &at, const char *usage, uint32_t &out, bool optional, Error *error) {
    const int present = Present(L, at, usage, optional, error);
    if (present <= 0)
        return present == 0;
    const double v = IsNumber(L, at.index) ? ToNumber(L, at.index) : NAN;
    if (!(v >= 0.0 && v <= 4294967295.0))
        return Fail(L, at, "outside of expected range 0 to 4294967295", usage, error);
    out = static_cast<uint32_t>(v);
    return true;
}

bool ReadBool(void *L, const Slot &at, const char *usage, bool &out, bool optional, Error *error) {
    const int present = Present(L, at, usage, optional, error);
    if (present <= 0)
        return present == 0;
    out = ToBoolean(L, at.index) != 0;
    return true;
}

// Pushes t[key], runs `read` on it and pops it.
template <typename Read>
bool ReadField(void *L, int table, const char *key, const char *path, Read read) {
    PushString(L, key);
    GetTable(L, table);
    const bool ok = read(Slot{GetTop(L), path != nullptr ? path : key});
    SetTop(L, -2);
    return ok;
}

} // namespace

void RaiseMessage(void *L, const char *text) {
    char capped[kErrorCap + 1];
    std::snprintf(capped, sizeof capped, "%s", text);
    PushString(L, capped);
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::LUA_RAW_ERROR)(L);
}

void Raise(void *L, const Error &e) {
    char text[kMessageMax];
    Message(e, "?", text);
    RaiseMessage(L, text);
}

void RaiseNamed(void *L, const Error &e) {
    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG] = {};
    const char *name = nullptr;
    if (GetStack(L, 0, ar) != 0 && GetInfo(L, "n", ar) != 0)
        name = Game::Read<const char *>(ar, Offsets::OFF_LUA_DEBUG_NAME);
    char text[kMessageMax];
    Message(e, name != nullptr ? name : "?", text);
    Game::Lua::Error(L, "%s", text);
}

void Raise(void *L, int arg, const char *what, const char *usage) { Fail(L, Slot{arg, nullptr}, what, usage, nullptr); }

bool Usage(void *L, int arg, const char *usage, Error *error, const char *field) {
    return Fail(L, Slot{arg, field}, nullptr, usage, error);
}

bool StringView(void *L, int arg, const char *usage, Text &out, bool optional, Error *error) {
    return ReadStringView(L, Slot{arg, nullptr}, usage, out, optional, error);
}

bool String(void *L, int arg, const char *usage, Text &out, bool optional, Error *error) {
    return ReadString(L, Slot{arg, nullptr}, usage, out, optional, error);
}

bool FiniteNumber(void *L, int arg, const char *usage, double &out, bool optional, Error *error) {
    return ReadFiniteNumber(L, Slot{arg, nullptr}, usage, out, optional, error);
}

bool FiniteFloat(void *L, int arg, const char *usage, float &out, bool optional, Error *error) {
    return ReadFiniteFloat(L, Slot{arg, nullptr}, usage, out, optional, error);
}

bool Int32(void *L, int arg, const char *usage, int32_t &out, bool optional, Error *error) {
    const Slot at{arg, nullptr};
    const int present = Present(L, at, usage, optional, error);
    if (present <= 0)
        return present == 0;
    const double v = IsNumber(L, arg) ? ToNumber(L, arg) : NAN;
    if (!(v >= -2147483648.0 && v <= 2147483647.0))
        return Fail(L, at, "outside of expected range -2147483648 to 2147483647", usage, error);
    out = static_cast<int32_t>(v);
    return true;
}

bool UInt32(void *L, int arg, const char *usage, uint32_t &out, bool optional, Error *error) {
    return ReadUInt32(L, Slot{arg, nullptr}, usage, out, optional, error);
}

bool Index(void *L, int arg, const char *usage, uint32_t &out, Error *error) {
    const Slot at{arg, nullptr};
    const double v = IsNumber(L, arg) ? ToNumber(L, arg) : NAN;
    if (!(v >= 0.0 && v <= 4294967295.0))
        return Fail(L, at, "outside of expected range 0 to 4294967295", usage, error);
    // cvttsd2si into 32 bits: out of int32 range is 0x80000000.
    const double zeroBased = v - 1.0;
    const int32_t i =
        zeroBased >= -2147483648.0 && zeroBased < 2147483648.0 ? static_cast<int32_t>(zeroBased) : INT32_MIN;
    out = static_cast<uint32_t>(i);
    return true;
}

bool Enum(void *L, int arg, const char *usage, uint32_t maxValue, uint32_t &out, bool optional,
          Error *error) {
    const Slot at{arg, nullptr};
    const int present = Present(L, at, usage, optional, error);
    if (present <= 0)
        return present == 0;
    if (!IsNumber(L, arg))
        return Fail(L, at, nullptr, usage, error);
    // lua_tointeger on x64: a truncating cast to int64, 0x8000000000000000
    // out of range, then compared as unsigned.
    const double v = ToNumber(L, arg);
    const int64_t i = v > -9223372036854775808.0 && v < 9223372036854775808.0 ? static_cast<int64_t>(v) : INT64_MIN;
    if (static_cast<uint64_t>(i) > maxValue)
        return Fail(L, at, nullptr, usage, error);
    out = static_cast<uint32_t>(i);
    return true;
}

bool Bool(void *L, int arg, const char *usage, bool &out, bool optional, Error *error) {
    return ReadBool(L, Slot{arg, nullptr}, usage, out, optional, error);
}

bool FieldStringView(void *L, int table, const char *key, const char *usage, Text &out, bool optional,
                     Error *error, const char *path) {
    return ReadField(L, table, key, path,
                     [&](const Slot &at) { return ReadStringView(L, at, usage, out, optional, error); });
}

bool FieldString(void *L, int table, const char *key, const char *usage, Text &out, bool optional, Error *error,
                 const char *path) {
    return ReadField(L, table, key, path,
                     [&](const Slot &at) { return ReadString(L, at, usage, out, optional, error); });
}

bool FieldFiniteNumber(void *L, int table, const char *key, const char *usage, double &out, bool optional,
                       Error *error, const char *path) {
    return ReadField(L, table, key, path,
                     [&](const Slot &at) { return ReadFiniteNumber(L, at, usage, out, optional, error); });
}

bool FieldUInt32(void *L, int table, const char *key, const char *usage, uint32_t &out, bool optional,
                 Error *error, const char *path) {
    return ReadField(L, table, key, path,
                     [&](const Slot &at) { return ReadUInt32(L, at, usage, out, optional, error); });
}

bool FieldBool(void *L, int table, const char *key, const char *usage, bool &out, bool optional, Error *error,
               const char *path) {
    return ReadField(L, table, key, path,
                     [&](const Slot &at) { return ReadBool(L, at, usage, out, optional, error); });
}

bool FieldFiniteFloat(void *L, int table, const char *key, const char *usage, float &out, bool optional,
                      Error *error, const char *path) {
    return ReadField(L, table, key, path,
                     [&](const Slot &at) { return ReadFiniteFloat(L, at, usage, out, optional, error); });
}

} // namespace Game::Lua::Arg
