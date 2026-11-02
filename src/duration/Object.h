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

// Retail's LuaDurationObject, as the engine-side users take it
// (StatusBar:SetTimerDuration, UnitCastingDuration). A span is shared, not
// copied: a retail bar follows a change made to the object after it was
// handed over, and each getter call returns a new Lua object for it (retail
// probe 2026-11-01). So the span is reference-counted, and every Lua object
// and every holder owns one reference.
namespace Duration::Object {

struct Span;

void Retain(Span *s);
// Null-safe.
void Release(Span *s);

// The span of the duration object at `idx`, or null when the value is not one.
Span *ToSpan(void *L, int idx);

// Pushes a new Lua object for `s`.
void Push(void *L, Span *s);

// Pushes a new duration object for `totalSec` from `startSec`, both in
// GetTime() seconds.
void PushFromStart(void *L, double startSec, double totalSec);

// The share of the span gone, or left, now: GetElapsedPercent /
// GetRemainingPercent in real time, 0 to 1.
double ElapsedPercent(const Span *s);
double RemainingPercent(const Span *s);

} // namespace Duration::Object
