// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

#include <cstdint>

namespace Model::DisplayInfo {

// The CreatureDisplayInfoExtra record the engine dresses `displayID` from, or
// null when the display gets no character customization.
const uint8_t *DressRecord(uint32_t displayID);

// The engine character compositor, configured to dress the LOADED model
// `instance` from `extraRec` (a DressRecord) and fed its equipment, or null
// when the engine pool is empty or rejects the info. It holds a reference on
// the model. Pump it with PumpDresser until that returns true; DestroyDresser
// releases it (null is a no-op).
void *CreateDresser(void *instance, const uint8_t *extraRec);
bool PumpDresser(void *dresser);
void DestroyDresser(void *dresser);

// Gives every model attached to `instance` the light and fog callback that
// `frame` gives its own model, so the children are lit like it.
void LightChildren(void *frame, void *instance);

} // namespace Model::DisplayInfo
