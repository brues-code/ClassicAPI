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

#include "model/Camera.h"

#include <cstdint>

// The UiModelScene rows behind C_ModelInfo. Retail keeps them in four DB2s
// that 1.12 has no counterpart for; only the scenes a ClassicAPI UI shows are
// embedded: 4 (the default mount scene), 44 (the Qiraji battle tanks) and 596
// (the dressing room). Grow the list scene by scene, never the whole tables.
// Column meanings are retail's (docs/notes/model-scene.md § Data):
//   - an actor's flags are Enum.UIModelSceneActorFlag (2/4/8 use the centre
//     of the model for the X/Y/Z origin, 1 is deprecated);
//   - a camera's type 0 is "OrbitCamera", the only type any row uses, and its
//     flags are Enum.ModelSceneSetting;
//   - a scene's system type is what GetModelSceneInfoByID returns as
//     modelSceneType.
namespace Model::SceneData {

using Model::Camera::Vec3;

struct SceneRow {
    uint32_t id;
    uint32_t uiSystemType;
    uint32_t flags;
};

struct CameraRow {
    uint32_t id;
    uint32_t sceneID;
    const char *scriptTag;
    Vec3 target;
    float yaw, pitch, roll;
    float zoomDistance, minZoomDistance, maxZoomDistance;
    Vec3 zoomedTargetOffset;
    float zoomedYawOffset, zoomedPitchOffset, zoomedRollOffset;
    uint32_t flags;
};

struct ActorRow {
    uint32_t id;
    uint32_t sceneID;
    const char *scriptTag;
    Vec3 position;
    float yaw, pitch, roll;
    float normalizedScale;
    uint32_t flags;
    uint32_t displayID; // a kActorDisplays row, 0 for none
};

struct ActorDisplayRow {
    uint32_t id;
    uint32_t animation;
    uint32_t animationVariation;
    float animSpeed;
    uint32_t animKitID;       // 0 for none
    uint32_t spellVisualKitID; // 0 for none
    float alpha;
    float scale;
};

// Generated from the retail 12.1.7.70740 UiModelScene, UiModelSceneCamera,
// UiModelSceneActor and UiModelSceneActorDisplay exports (wago.tools CSV),
// keeping only the scenes ClassicAPI uses: 4, 44, 596.

// clang-format off
const SceneRow kScenes[] = {
    {4, 0, 0},
    {44, 0, 0},
    {596, 12, 0},
};

const CameraRow kCameras[] = {
    {1, 4, "primary", {0.0f, 0.0f, 2.20000004768f}, 2.61999988556f, 0.0f, 0.0f, 14.0f, 8.0f, 18.0f, {0.0f, 0.0f, -1.5f}, 0.17453292012f, 0.0f, 0.0f, 0},
    {48, 44, "primary", {0.0f, 0.0f, 2.20000004768f}, 2.61799383163f, 0.0f, 0.0f, 14.0f, 8.0f, 18.0f, {0.0f, 0.0f, -1.5f}, 0.17453292012f, 0.0f, 0.0f, 1},
    {582, 596, "primary", {0.0f, 0.0f, 1.01999998093f}, 1.83000004292f, 0.0f, 0.0f, 3.90000009537f, 2.0f, 3.90000009537f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 1},
};

const ActorRow kActors[] = {
    {1, 4, "unwrapped", {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.25f, 8, 6},
    {2, 4, "wrapped", {0.0f, 0.0f, -0.5f}, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1},
    {60, 44, "unwrapped", {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.40000000596f, 9, 6},
    {61, 44, "wrapped", {0.0f, 0.0f, -0.5f}, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1},
    {400, 4, "player-rider", {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f, 0, 170},
    {408, 44, "player-rider", {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f, 0, 170},
    {1620, 596, "player", {0.0f, 0.0f, 0.0f}, -0.95999997854f, 0.0f, 0.0f, 1.0f, 2, 170},
    {1621, 596, "tauren-male", {0.0f, 0.0f, 0.10000000149f}, -0.95999997854f, 0.0f, 0.0f, 0.28999999166f, 2, 170},
    {1622, 596, "gnome", {0.0f, 0.0f, 0.02999999933f}, -0.95999997854f, 0.0f, 0.0f, 2.04999995232f, 2, 170},
    {1623, 596, "dwarf-male", {0.0f, 0.0f, 0.03999999911f}, -0.95999997854f, 0.0f, 0.0f, 4.80000019073f, 2, 174},
    {1624, 596, "scourge-male", {0.0f, 0.0f, 0.0f}, -0.95999997854f, 0.0f, 0.0f, 19.0f, 2, 174},
    {1625, 596, "pandaren", {0.0f, 0.0f, 0.0700000003f}, -0.95999997854f, 0.0f, 0.0f, 0.37000000477f, 2, 170},
    {1626, 596, "worgen-male", {0.0f, 0.0f, -0.03999999911f}, -0.95999997854f, 0.0f, 0.0f, 0.20000000298f, 2, 170},
    {1627, 596, "draenei-male", {0.0f, 0.0f, 0.1400000006f}, -0.95999997854f, 0.0f, 0.0f, 0.49000000954f, 0, 170},
    {1628, 596, "goblin-male", {0.0f, 0.0f, 0.01999999955f}, -0.95999997854f, 0.0f, 0.0f, 6.30000019073f, 2, 170},
    {1629, 596, "goblin-female", {0.0f, 0.0f, 0.03999999911f}, -0.95999997854f, 0.0f, 0.0f, 4.0f, 2, 170},
    {1630, 596, "orc", {0.0f, 0.0f, 0.05999999866f}, -0.95999997854f, 0.0f, 0.0f, 0.33000001311f, 2, 170},
    {1631, 596, "tauren-female", {0.0f, 0.0f, 0.10000000149f}, -0.95999997854f, 0.0f, 0.0f, 0.41999998689f, 2, 170},
    {1632, 596, "troll-male", {0.0f, 0.0f, -0.05999999866f}, -0.95999997854f, 0.0f, 0.0f, 0.07999999821f, 2, 170},
    {1633, 596, "troll-female", {0.0f, 0.0f, 0.0f}, -0.95999997854f, 0.0f, 0.0f, 0.64999997616f, 2, 170},
    {1634, 596, "scourge-female", {0.0f, 0.0f, -0.00999999978f}, -0.95999997854f, 0.0f, 0.0f, 3.29999995232f, 2, 170},
    {1635, 596, "lightforgeddraenei-male", {0.0f, 0.0f, 0.12999999523f}, -0.95999997854f, 0.0f, 0.0f, 0.49000000954f, 0, 170},
    {1636, 596, "lightforgeddraenei-female", {0.0f, 0.0f, 0.03999999911f}, -0.95999997854f, 0.0f, 0.0f, 0.80000001192f, 2, 170},
    {1637, 596, "highmountaintauren-male", {0.0f, 0.0f, 0.10000000149f}, -0.95999997854f, 0.0f, 0.0f, 0.28999999166f, 2, 170},
    {1638, 596, "highmountaintauren-female", {0.0f, 0.0f, 0.10000000149f}, -0.95999997854f, 0.0f, 0.0f, 0.41999998689f, 2, 170},
    {1639, 596, "zandalaritroll", {0.0f, 0.0f, 0.05000000075f}, -0.95999997854f, 0.0f, 0.0f, 0.86000001431f, 2, 170},
    {1640, 596, "kultiran-male", {0.0f, 0.0f, 0.01999999955f}, -0.95999997854f, 0.0f, 0.0f, 0.49000000954f, 2, 170},
    {1641, 596, "magharorc-male", {0.0f, 0.0f, 0.1099999994f}, -0.95999997854f, 0.0f, 0.0f, 0.33000001311f, 2, 170},
    {1642, 596, "kultiran-female", {0.0f, 0.0f, 0.0f}, -0.95999997854f, 0.0f, 0.0f, 1.12000000477f, 2, 170},
    {1643, 596, "magharorc-female", {0.0f, 0.0f, 0.05000000075f}, -0.95999997854f, 0.0f, 0.0f, 0.18000000715f, 2, 170},
    {1644, 596, "darkirondwarf-male", {0.0f, 0.0f, 0.0700000003f}, -0.95999997854f, 0.0f, 0.0f, 4.94999980927f, 2, 174},
    {1645, 596, "worgen-female", {0.0f, 0.0f, 0.0f}, -0.95999997854f, 0.0f, 0.0f, 1.20000004768f, 2, 170},
    {1646, 596, "draenei-female", {0.0f, 0.0f, 0.05000000075f}, -0.95999997854f, 0.0f, 0.0f, 1.00999999046f, 2, 170},
    {1647, 596, "vulpera-female", {-0.12999999523f, 0.20000000298f, 0.03999999911f}, -0.95999997854f, 0.0f, 0.0f, 8.0f, 2, 174},
    {1648, 596, "vulpera-male", {-0.12999999523f, 0.20000000298f, 0.03999999911f}, -0.95999997854f, 0.0f, 0.0f, 5.0f, 2, 174},
    {1649, 596, "mechagnome-male", {0.0f, 0.0f, 0.07999999821f}, -0.95999997854f, 0.0f, 0.0f, 1.39999997616f, 2, 170},
    {1650, 596, "mechagnome-female", {0.0f, 0.0f, 0.07999999821f}, -0.95999997854f, 0.0f, 0.0f, 1.20000004768f, 2, 170},
    {1651, 596, "druid-bear-form", {0.10000000149f, 0.0f, 0.25f}, -0.95999997854f, 0.0f, 0.0f, 0.44999998808f, 0, 170},
    {1652, 596, "druid-cat-form", {1.0f, 0.0f, 0.5f}, -0.95999997854f, 0.0f, 0.0f, 0.28999999166f, 0, 170},
    {1653, 596, "dracthyr", {-0.5f, 0.80000001192f, -0.10000000149f}, -0.95999997854f, 0.0f, 0.0f, 0.12999999523f, 2, 170},
    {1654, 596, "dracthyr-alt", {0.0f, 0.0f, 0.0f}, -0.95999997854f, 0.0f, 0.0f, 1.0f, 2, 170},
    {2152, 596, "earthendwarf-male", {0.0f, 0.0f, 0.03999999911f}, -0.95999997854f, 0.0f, 0.0f, 2.70000004768f, 2, 174},
    {3606, 596, "harronir", {0.0f, 0.0f, 0.0f}, -1.08210408688f, 0.0f, 0.0f, 1.0f, 0, 0},
    {3619, 596, "harronir-female", {0.0f, 0.0f, 0.0f}, -1.08210408688f, 0.0f, 0.0f, 1.29999995232f, 0, 0},
};

const ActorDisplayRow kActorDisplays[] = {
    {1, 0, 0, 1.0f, 0, 0, 1.0f, 4.0f},
    {6, 0, 0, 1.0f, 0, 0, 1.0f, 3.5f},
    {170, 0, 0, 1.0f, 0, 0, 1.0f, 1.0f},
    {174, 0, 0, 1.0f, 0, 0, 1.0f, 2.0f},
};
// clang-format on

} // namespace Model::SceneData
