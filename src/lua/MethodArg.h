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

// The argument readers of the 3.80.2 client's generated frame-method bindings
// (the table in its section 2.0: FUN_140A6DF60 finite float, FUN_140A91460
// int, FUN_140A6CB40 bool, FUN_140A6DC70 string, FUN_140A92C90 vector2). A
// bad argument raises luaL_argerror at its slot with the method's usage
// string, "<detail> - <usage>" when the reader names what is wrong. Each
// returns false after raising, so a caller returns at once.

#include "Game.h"

#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>

namespace Game::Lua::MethodArg {

inline bool Missing(void *L, int idx) { return Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL; }

inline bool Fail(void *L, int idx, const char *usage, const char *detail = nullptr) {
    if (detail == nullptr) {
        Game::Lua::ArgError(L, idx, usage);
    } else {
        char message[512];
        std::snprintf(message, sizeof message, "%s - %s", detail, usage);
        Game::Lua::ArgError(L, idx, message);
    }
    return false;
}

// A finite number within the float range.
inline bool Number(void *L, int idx, const char *usage, float &out) {
    if (!Game::Lua::IsNumber(L, idx))
        return Fail(L, idx, usage);
    const double v = Game::Lua::ToNumber(L, idx);
    if (!std::isfinite(v))
        return Fail(L, idx, usage, "must be a finite number");
    if (v < -static_cast<double>(FLT_MAX) || v > static_cast<double>(FLT_MAX)) {
        char detail[96];
        std::snprintf(detail, sizeof detail, "outside of expected range %e to %e", -static_cast<double>(FLT_MAX),
                      static_cast<double>(FLT_MAX));
        return Fail(L, idx, usage, detail);
    }
    out = static_cast<float>(v);
    return true;
}

// Number, or `out` unchanged when the argument is nil or missing.
inline bool OptNumber(void *L, int idx, const char *usage, float &out) {
    return Missing(L, idx) || Number(L, idx, usage, out);
}

// Any number, unchecked (the UI-unit and vector2 readers).
inline bool RawNumber(void *L, int idx, const char *usage, float &out) {
    if (!Game::Lua::IsNumber(L, idx))
        return Fail(L, idx, usage);
    out = static_cast<float>(Game::Lua::ToNumber(L, idx));
    return true;
}

// luaL_optnumber: `fallback` when the argument is nil or missing, else a
// number, or the argument error "number expected, got <type>".
inline bool OptLuaNumber(void *L, int idx, float fallback, float &out) {
    if (Missing(L, idx)) {
        out = fallback;
        return true;
    }
    if (!Game::Lua::IsNumber(L, idx)) {
        char message[96];
        std::snprintf(message, sizeof message, "number expected, got %s",
                      Game::Lua::TypeName(L, Game::Lua::Type(L, idx)));
        Game::Lua::ArgError(L, idx, message);
        return false;
    }
    out = static_cast<float>(Game::Lua::ToNumber(L, idx));
    return true;
}

// An int32, truncated.
inline bool Integer(void *L, int idx, const char *usage, int &out) {
    if (!Game::Lua::IsNumber(L, idx))
        return Fail(L, idx, usage);
    const double v = Game::Lua::ToNumber(L, idx);
    if (!(v >= static_cast<double>(INT_MIN) && v <= static_cast<double>(INT_MAX))) {
        char detail[96];
        std::snprintf(detail, sizeof detail, "outside of expected range %i to %i", INT_MIN, INT_MAX);
        return Fail(L, idx, usage, detail);
    }
    out = static_cast<int>(v);
    return true;
}

// Any value but nil, as lua_toboolean reads it.
inline bool Bool(void *L, int idx, const char *usage, bool &out) {
    if (Missing(L, idx))
        return Fail(L, idx, usage);
    out = Game::Lua::ToBoolean(L, idx) != 0;
    return true;
}

inline bool OptBool(void *L, int idx, bool fallback) {
    return Missing(L, idx) ? fallback : Game::Lua::ToBoolean(L, idx) != 0;
}

// A string (a number reads as one), or nullptr after raising.
inline const char *String(void *L, int idx, const char *usage) {
    if (!Game::Lua::IsString(L, idx)) {
        Fail(L, idx, usage);
        return nullptr;
    }
    return Game::Lua::ToString(L, idx);
}

// String, or nullptr without an error when the argument is nil or missing;
// `ok` is false after raising.
inline const char *OptString(void *L, int idx, const char *usage, bool &ok) {
    ok = true;
    if (Missing(L, idx))
        return nullptr;
    const char *s = String(L, idx, usage);
    ok = s != nullptr;
    return s;
}

} // namespace Game::Lua::MethodArg
