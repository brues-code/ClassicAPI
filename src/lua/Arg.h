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

#pragma once

#include <cstddef>
#include <cstdint>

// The 3.80.2 client's script argument readers, for the functions it generates
// from API documentation. Each reads one argument and, when it is wrong,
// raises luaL_argerror from a function Lua knows no name for:
//   "bad argument #<n> to '?' (<usage>)", or "(<what> - <usage>)"
// where `usage` is the function's whole "Usage: ..." text. A field of a table
// argument (FUN_140A604E0 with a field path) is read from the stack slot it
// is pushed to, and says which:
//   "bad argument #<slot> to '?' (Current Field: [<field>], <what - usage>)"
// The generated wrapper then copies the message into char[200], so every
// message is cut to 199 characters (kErrorCap).
//
// A missing argument (nil or none) is the plain usage error unless the reader
// is told it is optional, when it leaves `out` as it was. Secret values (the
// readers' other checks) do not exist here.
//
// The raise is a longjmp, which runs no destructors. A reader raises at once
// unless it is given an Error, which it fills instead, returning false: the
// caller then raises the message once whatever owns memory is gone.
namespace Game::Lua::Arg {

constexpr std::size_t kErrorCap = 199;

// A failed read: an argument error by its parts, or (`arg` 0) a whole
// message in `text`.
struct Error {
    char text[kErrorCap + 1] = {};
    int arg = 0;                   // the stack slot
    const char *field = nullptr;   // the Current Field path, or null
    char what[96] = {};            // the problem in front of the usage, or ""
    const char *usage = nullptr;
};

// Raises `text` cut to kErrorCap as the error value, with no position in
// front: 3.80.2 raises the message its pcall caught (LUA_RAW_ERROR), not
// luaL_error, which would add the Lua caller's "file:line:".
void RaiseMessage(void *L, const char *text);

// Raises `e`'s message, as RaiseMessage, the function named '?'.
void Raise(void *L, const Error &e);

// Raises `e` as luaL_argerror does in a function 3.80.2 does not wrap in a
// pcall (C_CurveUtil's Evaluate*): the running function's name in place of
// '?', the Lua caller's "file:line:" in front, not cut.
void RaiseNamed(void *L, const Error &e);

// Raises "bad argument #<arg> to '?' (<what> - <usage>)", or without `what`
// when it is null.
void Raise(void *L, int arg, const char *what, const char *usage);

struct Text {
    const char *data = nullptr;
    std::size_t size = 0;
};

// A stringView argument (FUN_140B27EE0): anything lua_isstring takes, numbers
// included, kept whole with any NUL in it.
bool StringView(void *L, int arg, const char *usage, Text &out, bool optional = false,
                Error *error = nullptr);

// A string argument (FUN_140BEC2B0): the same, copied up to its first NUL.
bool String(void *L, int arg, const char *usage, Text &out, bool optional = false, Error *error = nullptr);

// A number argument (FUN_140ACEB10): a number, or a string that reads as one,
// that is finite. Otherwise "must be a finite number - <usage>".
bool FiniteNumber(void *L, int arg, const char *usage, double &out, bool optional = false,
                  Error *error = nullptr);

// An int32 argument (FUN_140A91460, range check FUN_140A65EE0): a number from
// -2147483648 to 2147483647, truncated toward zero. Otherwise "outside of
// expected range -2147483648 to 2147483647 - <usage>".
bool Int32(void *L, int arg, const char *usage, int32_t &out, bool optional = false,
           Error *error = nullptr);

// A uint32 argument (FUN_140A6E2D0): a number from 0 to 4294967295, truncated.
// Otherwise "outside of expected range 0 to 4294967295 - <usage>".
bool UInt32(void *L, int arg, const char *usage, uint32_t &out, bool optional = false,
            Error *error = nullptr);

// A bool argument (FUN_140A6CB40): lua_toboolean of any value present, so
// every value but false counts as true.
bool Bool(void *L, int arg, const char *usage, bool &out, bool optional = false, Error *error = nullptr);

// A float argument (FUN_140A6DF60): a finite number within +-FLT_MAX. Else
// "must be a finite number" or "outside of expected range -3.402823e+38 to
// 3.402823e+38".
bool FiniteFloat(void *L, int arg, const char *usage, float &out, bool optional = false, Error *error = nullptr);

// A luaIndex argument (FUN_140A6BF10): a number from 0 to 4294967295, else
// "outside of expected range 0 to 4294967295" (also for a non-number). `out`
// is the zero-based index, cvttsd2si32(v - 1), so 0 gives 0xFFFFFFFF and 1.9
// gives 0.
bool Index(void *L, int arg, const char *usage, uint32_t &out, Error *error = nullptr);

// The usage error (no `what`) at `arg`, as the readers give it.
bool Usage(void *L, int arg, const char *usage, Error *error = nullptr, const char *field = nullptr);

// The same readers for field `key` of the table at stack `table`, pushed to
// the top for the read and popped after (lua_getfield, so __index runs).
// `path` is what Current Field names, `key` itself when null.
bool FieldStringView(void *L, int table, const char *key, const char *usage, Text &out, bool optional = false,
                     Error *error = nullptr, const char *path = nullptr);
bool FieldString(void *L, int table, const char *key, const char *usage, Text &out, bool optional = false,
                 Error *error = nullptr, const char *path = nullptr);
bool FieldFiniteNumber(void *L, int table, const char *key, const char *usage, double &out,
                       bool optional = false, Error *error = nullptr, const char *path = nullptr);
bool FieldBool(void *L, int table, const char *key, const char *usage, bool &out, bool optional = false,
               Error *error = nullptr, const char *path = nullptr);
bool FieldFiniteFloat(void *L, int table, const char *key, const char *usage, float &out, bool optional = false,
                      Error *error = nullptr, const char *path = nullptr);

} // namespace Game::Lua::Arg
