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

// What a StatusBar's fill shows when it is not its stored value: the share of
// a timer (bar:SetTimerDuration) and the ease of Enum.StatusBarInterpolation
// .ExponentialEaseOut.
namespace StatusBar::Display {

// Runs first in every StatusBar update (StatusBar::Fill's co-hook on
// CSimpleStatusBar::OnFrameUpdate) with the frame's `elapsed`: moves the shown
// fraction and marks the fill changed when it moved.
void Advance(void *bar, float elapsed);

// Drops the bar's timer. Every range change does (retail probe:
// SetMinMaxValues ends the timer, SetValue does not).
void ClearTimer(void *bar);

} // namespace StatusBar::Display
