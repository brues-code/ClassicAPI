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

#include <cstdint>

// The gamepad system, as the 3.80.2 client runs it: started and stopped by
// the GamePadEnable cvar, polled once a frame on glue and in the world, and
// its devices, configs, LED and rumble driven from C_GamePad.
namespace GamePad {

// C_GamePad.IsEnabled: GamePadEnable is on and SDL started (0x144C95BBD).
bool Started();

// True once the gamepad has started this session. The first start points the
// button config names at the binding names (0x1446EC868), so from then on
// ButtonIndexToConfigName returns "PAD1" where it returned "Face1".
bool EverStarted();

// GetActiveDeviceID: GamePadSingleActiveID, 0 as 5 (the combined device).
uint32_t ActiveID();

// ApplyConfigs: layer 3 over layer 2, then every device resolves again.
void ApplyConfigs();

// SetVibration / StopVibration on the active device, or every physical
// device when the combined one is active.
void SetVibration(int type, float intensity);
void StopVibration();

// The LED colour 0xRRGGBB: GetLedColor's override when set, else the last
// colour applied; SetLedColor / ClearLedColor set and drop the override.
uint32_t LedColor();
void SetLedOverride(uint32_t rgb);
void ClearLedOverride();

} // namespace GamePad
