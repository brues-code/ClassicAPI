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

// The ActionCam cvars as the 3.80.2 client registers them (names, defaults,
// help, ranges). Each reader falls back to the compiled default while the
// cvar is not registered yet, as 3.80.2's readers fall back to the static
// default beside each cvar. See docs/notes/actioncam-3802.md A §1.

namespace Camera::Settings {

bool KeepCharacterCentered();
bool ReduceUnexpectedMovement();
int ZSmooth();
bool IndirectVisibility();
float IndirectOffset();
float OverShoulder();
float DynamicPitch();
float DynamicPitchBaseFovPad();
float DynamicPitchBaseFovPadDownScale();
float DynamicPitchSmartPivotCutoffDist();
float HeadMovementStrength();
float HeadMovementRangeScale();
float HeadMovementDeadZone();
float HeadMovementStandingStrength();
float HeadMovementMovingStrength();
float HeadMovementStandingDampRate();
float HeadMovementMovingDampRate();
float HeadMovementFirstPersonDampRate();
bool TargetFocusEnemyEnable();
float TargetFocusEnemyStrengthPitch();
float TargetFocusEnemyStrengthYaw();
bool TargetFocusInteractEnable();
float TargetFocusInteractStrengthPitch();
float TargetFocusInteractStrengthYaw();

} // namespace Camera::Settings
