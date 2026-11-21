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

#include "Game.h"
#include "lua/Boxed.h"
#include "lua/FullUserdata.h"

#include <cstddef>
#include <string>

// The 3.80.2 client's NumericFormatter objects: the base class every
// C_StringUtil formatter derives from (NumericRuleFormatter,
// AbbreviatedNumberFormatter, SecondsFormatter), as the engine-side users take
// them (Cooldown:SetCountdownFormatter, the duration text bindings). A
// formatter is shared, not copied: retail's cooldown shows a SetBreakpoints
// made after it was handed the formatter, and each getter call returns a new
// Lua object for it (retail probe 2026-10-31). So the formatter is
// reference-counted, and every Lua object and every holder owns one reference.
//
// Each class is a full-userdata class (lua/FullUserdata.h) built by
// CreateClass, which adds the base's one method, FormatNumber: it calls the
// class's FormatValue, as 3.80.2's FormatNumber (0x140D51BD0) calls vtable
// slot 1.
namespace Text::NumericFormatter {

struct Formatter : Game::Lua::FullUserdata::Object {
    // `value`'s text, or false with the error message in `out`. `L` is the
    // state the call runs in.
    virtual bool FormatValue(void *L, double value, std::string &out) const = 0;
};

inline void Retain(Formatter *f) { Game::Lua::FullUserdata::Retain(f); }
// Null-safe.
inline void Release(Formatter *f) { Game::Lua::FullUserdata::Release(f); }

// The formatter of the object of any formatter class at `idx`, or null when
// the value is not one.
Formatter *ToFormatter(void *L, int idx);

// The formatter at `idx` when its object is of class `key`, else null.
Formatter *ToClass(void *L, int idx, const char *key);

// Pushes a new object of class `key` for `f`, which gains a reference.
void Push(void *L, const char *key, Formatter *f);

// Pushes a new object of the base class, NumericFormatter: FormatNumber only,
// what 3.80.2's cooldown getter returns (0x140C13520).
void PushBase(void *L, Formatter *f);

// Builds formatter class `key`'s metatable in this Lua state unless it
// exists: `methods` and FormatNumber. `name` is what __tostring writes.
void CreateClass(void *L, const char *key, const char *name, const Game::Lua::Boxed::Method *methods,
                 std::size_t count);
template <std::size_t N>
void CreateClass(void *L, const char *key, const char *name, const Game::Lua::Boxed::Method (&methods)[N]) {
    CreateClass(L, key, name, methods, N);
}

// Raises a formatted error cut to 199 characters, as 3.80.2's method wrappers
// copy it into char[200].
void RaiseCapped(void *L, const char *fmt, ...);

// Pushes `f`'s text for `value` and returns it. On a format error, pushes
// the formatter's message and returns null. Either way one value is pushed.
const char *Format(void *L, Formatter *f, double value);

} // namespace Text::NumericFormatter
