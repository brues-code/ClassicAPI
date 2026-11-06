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

// Typed calls into the engine's M2 model instances and the Model frames
// ("panes") that show them, shared by every model/ module.

#pragma once

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Model::M2 {

inline bool IsLoaded(void *model, int tryLoad, int recurseChildren) {
    using Fn = int(__thiscall *)(void *, int, int);
    return reinterpret_cast<Fn>(Offsets::FUN_MODEL_INSTANCE_LOADED)(model, tryLoad,
                                                                    recurseChildren) != 0;
}

// The draw gate: also makes the device textures when `wait` is set.
inline bool IsReady(void *model, int wait, int recurseChildren) {
    using Fn = int(__thiscall *)(void *, int, int);
    return reinterpret_cast<Fn>(Offsets::FUN_MODEL_INSTANCE_READY)(model, wait,
                                                                   recurseChildren) != 0;
}

inline void AddRef(void *model) {
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_MODEL_INSTANCE_ADDREF)(model);
}

inline void Release(void *model) {
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_MODEL_INSTANCE_RELEASE)(model);
}

// A new model in `scene` that the caller owns one reference on, or null.
inline void *CreateModel(void *scene, const char *path) {
    using Fn = void *(__thiscall *)(void *, const char *, uint32_t);
    return reinterpret_cast<Fn>(Offsets::FUN_M2SCENE_CREATE_MODEL)(scene, path, 0);
}

inline void PlayAnimation(void *model, int keyBone, uint32_t id, int variation, int offset,
                          float speed, int blend, int primary) {
    using Fn = void(__thiscall *)(void *, int, uint32_t, int, int, float, int, int);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_PLAY_ANIMATION)(model, keyBone, id, variation,
                                                              offset, speed, blend, primary);
}

inline void SetAnimDoneCallback(void *model, const void *callback, uint32_t userLow,
                                uint32_t userHigh) {
    using Fn = void(__thiscall *)(void *, const void *, uint32_t, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_ANIM_DONE_CALLBACK)(model, callback, userLow,
                                                                      userHigh);
}

inline void SetVisible(void *model, bool visible) {
    using Fn = void(__thiscall *)(void *, int);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_VISIBLE)(model, visible ? 1 : 0);
}

inline void SetAnimated(void *model, bool animated) {
    using Fn = void(__thiscall *)(void *, int);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_ANIMATED)(model, animated ? 1 : 0);
}

inline void AnimateScene(void *scene, const float *cameraPos) {
    using Fn = void(__thiscall *)(void *, const float *);
    reinterpret_cast<Fn>(Offsets::FUN_M2SCENE_ANIMATE)(scene, cameraPos);
}

inline void AttachItemVisuals(void *model, uint32_t visualID) {
    using Fn = void(__fastcall *)(void *, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_ATTACH_ITEM_VISUALS)(model, visualID);
}

inline void SetRenderCallback(void *model, const void *callback, void *userData) {
    using Fn = void(__thiscall *)(void *, const void *, void *);
    reinterpret_cast<Fn>(Offsets::FUN_MODEL_INSTANCE_SET_RENDER_CB)(model, callback, userData);
}

// The model's shared M2 data (the file's header and tables).
inline const uint8_t *Data(void *model) {
    return Game::Read<const uint8_t *>(Game::Read<const uint8_t *>(model, Offsets::OFF_M2MODEL_SHARED),
                                       Offsets::OFF_M2SHARED_DATA);
}

// The index `id` maps to in one of the data's lookup tables
// (OFF_M2DATA_*_LOOKUP); the caller bounds it by that table's count.
inline uint16_t LookupIndex(void *model, uintptr_t tableOffset, uint32_t id) {
    using Fn = uint16_t(__fastcall *)(uint32_t, const void *);
    return reinterpret_cast<Fn>(Offsets::FUN_M2_LOOKUP_INDEX)(id, Data(model) + tableOffset);
}

} // namespace Model::M2

namespace Model::Pane {

// The model instance a Model frame shows, or null.
inline void *Model(const void *pane) {
    return Game::Read<void *>(pane, Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE);
}

inline void *Scene(void *pane) {
    using Fn = void *(__thiscall *)(void *);
    return reinterpret_cast<Fn>(Offsets::FUN_SIMPLEMODEL_GET_SCENE)(pane);
}

// The frame's SetModelInstance virtual; null drops its model.
inline void SetModel(void *pane, void *model) {
    using Fn = void(__thiscall *)(void *, void *);
    Game::Read<Fn>(Game::Read<const void *>(pane, 0),
                   Offsets::VTBL_SIMPLEMODEL_SET_MODEL_INSTANCE)(pane, model);
}

// PlayerModel's SetDisplay: stores the display record the pane builds from.
inline void SetDisplay(void *pane, const uint8_t *record) {
    using Fn = void(__thiscall *)(void *, const uint8_t *);
    reinterpret_cast<Fn>(Offsets::FUN_PLAYERMODEL_SET_DISPLAY)(pane, record);
}

inline bool IsVisible(const void *pane) {
    return Game::Read<int>(pane, Offsets::OFF_FRAME_VISIBLE) != 0;
}

// The display record the pane shows (SetDisplay's), or null.
inline const uint8_t *DisplayRecordOf(const void *pane) {
    return Game::Read<const uint8_t *>(pane, Offsets::OFF_PLAYERMODEL_DISPLAY_RECORD);
}

// A display record of our own for SetDisplay. The pane reads only its
// display-ID field, keeps the pointer and builds from it on every show, so
// the record lives as long as the pane.
struct DisplayRecord {
    uint8_t bytes[Offsets::OFF_CREATURE_DISPLAYID + sizeof(uint32_t)] = {};
};

} // namespace Model::Pane
