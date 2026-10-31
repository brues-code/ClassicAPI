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

// Retail's NumericFormatter objects, as the engine-side users take them
// (Cooldown:SetCountdownFormatter). The one kind ported is
// C_StringUtil.CreateNumericRuleFormatter's. A formatter is shared, not
// copied: retail's cooldown shows a SetBreakpoints made after it was handed
// the formatter, and each getter call returns a new Lua object for it
// (retail probe 2026-10-31). So the formatter is reference-counted, and every
// Lua object and every holder owns one reference.
namespace Text::NumericFormatter {

struct Formatter;

void Retain(Formatter *f);
// Null-safe.
void Release(Formatter *f);

// The formatter of the NumericFormatter object at `idx`, or null when the
// value is not one.
Formatter *ToFormatter(void *L, int idx);

// Pushes a new Lua object for `f`.
void Push(void *L, Formatter *f);

// Pushes `f`'s text for `value` and returns it. On a format error, pushes
// str_format's message and returns null. Either way one value is pushed.
const char *Format(void *L, Formatter *f, double value);

} // namespace Text::NumericFormatter
