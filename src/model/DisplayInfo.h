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

// The CreatureDisplayInfo row of `displayID`, and the CreatureModelData row a
// display row names (its +0x04 ModelID): the chain the engine's unit-display
// resolver FUN_0060afb0 walks. Null for a missing row or a null `display`.
const uint8_t *Record(uint32_t displayID);
const uint8_t *ModelData(const uint8_t *display);

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

// A player's own look, as CGUnit_C::BuildCharacterComponent's player branch
// reads it: race and sex from UNIT_FIELD_BYTES_0, the rest from PLAYER_BYTES
// and PLAYER_BYTES_2.
// A guild's tabard emblem, in GetGuildEmblem's order; -1 where unknown.
struct Emblem {
    int32_t style = -1;
    int32_t color = -1;
    int32_t border = -1;
    int32_t borderColor = -1;
    int32_t background = -1;
    bool Known() const { return style != -1 && color != -1 && border != -1 && borderColor != -1 && background != -1; }
};

struct Body {
    uint32_t race = 0;
    uint32_t sex = 0; // 0 male, 1 female
    uint32_t skin = 0;
    uint32_t face = 0;
    uint32_t hairStyle = 0;
    uint32_t hairColor = 0;
    uint32_t facialHair = 0;
    Emblem emblem; // its tabard's, when it wears one that shows an emblem
};

// The display a player of `body`'s race and sex is shown in (ChrRaces), or 0.
uint32_t BodyDisplay(const Body &body);

// CreateDresser for a player body: composited from its look, no bake and no
// equipment.
void *CreateDresser(void *instance, const Body &body);
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
// SetCharacterComponentEmblem (0x0047A610).
void SetDresserEmblem(void *dresser, const Emblem &emblem);

// Gives every model attached to `instance` the light and fog callback of
// `instance` itself, else the one `frame` gives its own model, so the
// children are lit like it.
void LightChildren(void *frame, void *instance);
// The same for one `child` just attached to `instance`.
void LightChild(void *frame, void *instance, void *child);

} // namespace Model::DisplayInfo
