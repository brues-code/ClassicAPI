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

// A new model of the display in `scene`, skinned as AttachDisplayModel skins
// one (CreateModelFromPath on the CreatureModelData path, then
// ApplyCreatureDisplay), that the caller owns one reference on. Null for a
// display with no CreatureDisplayInfo or CreatureModelData row. Undressed: a
// DressRecord display still needs CreateDresser once it has loaded. `path`,
// when given, receives the model's file path (DBC memory).
void *CreateDisplayModel(void *scene, uint32_t displayID, const char **path = nullptr);

// The engine character compositor, configured to dress the LOADED model
// `instance` from `extraRec` (a DressRecord) and fed its equipment, or null
// when the engine pool is empty or rejects the info. It holds a reference on
// the model. Pump it with PumpDresser until that returns true; DestroyDresser
// releases it (null is a no-op). `force` loads every texture at once,
// ignoring the engine's per-frame load budget.
void *CreateDresser(void *instance, const uint8_t *extraRec);
bool PumpDresser(void *dresser, bool force = false);
void DestroyDresser(void *dresser);

// The compositor's own calls, for a dresser built another way. A new one
// (FUN_CHARCOMP_CREATE), null when the pool is empty.
void *NewDresser();
// FUN_CHARCOMP_CLONE: `dresser` dresses `model` as `source` dresses its own;
// the caller gives the dresser its reference on `model` first.
void CloneDresser(void *dresser, void *model, void *source);
// One component slot cleared, or set to an ItemDisplayInfo ID.
void UnequipDresserSlot(void *dresser, uint32_t slot);
void SetDresserItem(void *dresser, uint32_t slot, uint32_t displayID);

// Gives every model attached to `instance` the light and fog callback of
// `instance` itself, else the one `frame` gives its own model, so the
// children are lit like it.
void LightChildren(void *frame, void *instance);
// The same for one `child` just attached to `instance`.
void LightChild(void *frame, void *instance, void *child);

} // namespace Model::DisplayInfo
