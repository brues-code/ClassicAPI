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

// The TargetNearest / TargetDirection functions 1.12 lacks, following 4.3.4
// (decomp GameUI.cpp / GameUI_Scripts.cpp):
//
//   TargetNearest([reverse])              TargetDirectionEnemy(facing [, angle])
//   TargetNearestEnemyPlayer([reverse])   TargetDirectionFriend(facing [, angle])
//   TargetNearestFriendPlayer([reverse])  TargetDirectionFinished()
//
// TargetNearest*: 4.3.4 runs all seven through one routine,
// TargetNearestEnemy(reverse, type). 1.12 has that routine whole —
// FUN_TARGET_NEAREST_CYCLE with the same snapshot, timers, ranges, sort and
// predicate (see Offsets.h) — and its natives call it with modes 1..4. The
// three missing types become three more predicate modes: a co-hook on
// FUN_TARGET_CANDIDATE_VALID answers them (4.3.4's UnitMatchesTargetType
// cases 0 / 2 / 4) and passes every engine mode through. The cycle state is
// the engine's, shared with the natives, as 4.3.4's is.
//
// TargetDirection*: 4.3.4's CGGameUI::TargetDirection, which 1.12 lacks, built
// on 1.12's own pieces — the world frame's GetScreenCoordinates, the camera's
// forward vector, the predicate, and the tab search's range / creature-type /
// visibility tests. `facing` is a direction on screen relative to the camera
// (0 = ahead, pi/2 = right); the start point is the current target when it is
// on screen, else the player. A hit sets the target and locks further calls
// until TargetDirectionFinished, or until the locked unit leaves (4.3.4's
// BreakTarget: out of the object table unless a group member or pet, or
// unselectable) or the player logs out (LeaveWorld).

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cfloat>
#include <cmath>
#include <cstdint>

namespace Target::Nearest {

namespace {

using Cycle_t = void(__fastcall *)(int reverse, int mode);
using Valid_t = int(__fastcall *)(void *player, void *cand, int mode);
using ToBool_t = int(__fastcall *)(void *L, int idx, int dflt);
using SetSelection_t = void(__stdcall *)(uint32_t guidLo, uint32_t guidHi);
using EnumCallback_t = int(__fastcall *)(void *ctx, void *unusedEdx, uint64_t guid);
using EnumVisibleObjects_t = int(__fastcall *)(EnumCallback_t cb, void *ctx);
using ScreenCoords_t = bool(__thiscall *)(void *worldFrame, const float *world, float *screen);
using Transform_t = float *(__fastcall *)(float *out, const float *v, const float *m);
using Forward_t = void(__thiscall *)(void *camera, float *out);
using Visible_t = bool(__fastcall *)(void *worldObject);
using CreatureType_t = int(__fastcall *)(void *unit);
using GroupMember_t = int(__fastcall *)(const uint64_t *guid);

// 4.3.4's minimum direction cone (22.5°); `angle` can only widen it.
constexpr float kMinConeAngle = 0.39269909f;
constexpr float kTwoPi = 6.2831855f;

Valid_t g_validOriginal = nullptr;

bool IsPlayerObject(void *unit) {
    return Game::Read<int>(unit, Offsets::OFF_CGOBJECT_TYPE_ID) == Offsets::OBJECT_TYPE_PLAYER;
}

// 4.3.4's UnitMatchesTargetType for the types 1.12 lacks. The searcher is
// never its own candidate (4.3.4's enumeration skips it); the engine's modes
// already exclude it where it matters.
int __fastcall Valid_h(void *player, void *cand, int mode) {
    switch (mode) {
    case Offsets::TARGET_MODE_ANY:
        return cand != player;
    case Offsets::TARGET_MODE_ENEMY_PLAYER:
        return cand != player && IsPlayerObject(cand) &&
               g_validOriginal(player, cand, Offsets::TARGET_MODE_ENEMY);
    case Offsets::TARGET_MODE_FRIEND_PLAYER:
        return cand != player && IsPlayerObject(cand) &&
               g_validOriginal(player, cand, Offsets::TARGET_MODE_FRIEND);
    default:
        return g_validOriginal(player, cand, mode);
    }
}

const Game::HookAutoRegister _validHook{Offsets::FUN_TARGET_CANDIDATE_VALID,
                                        reinterpret_cast<void *>(&Valid_h),
                                        reinterpret_cast<void **>(&g_validOriginal)};

void Cycle(void *L, int mode) {
    const int reverse =
        reinterpret_cast<ToBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE)(L, 1, 0);
    reinterpret_cast<Cycle_t>(Offsets::FUN_TARGET_NEAREST_CYCLE)(reverse, mode);
}

void SetSelection(uint64_t guid) {
    reinterpret_cast<SetSelection_t>(Offsets::FUN_SET_SELECTION_BY_GUID)(
        static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
}

uint64_t CurrentSelection() { return Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO); }

// --- TargetDirection ------------------------------------------------------

// 4.3.4's m_directionalTarget.
uint64_t g_directional = 0;

float CVarFloat(uintptr_t slot) {
    const void *cvar = Game::Read<void *>(slot);
    return cvar != nullptr ? Game::Read<float>(cvar, Offsets::OFF_CVAR_VALUE_FLOAT) : 0.0f;
}

// C2Vector::NormalizeSafe.
void NormalizeSafe(float &x, float &y) {
    const float len = std::sqrt(x * x + y * y);
    if (len > 0.0f) {
        x /= len;
        y /= len;
    }
}

// CGWorldFrame::GetScreenCoordinates with 4.3.4's depth: x / y and the result
// from the engine's projection, z the view-projected depth it computes on the
// way. Like 4.3.4's, the point is filled even when the result is false.
bool ScreenCoords(void *worldFrame, const float *world, float *screen) {
    screen[0] = screen[1] = screen[2] = 0.0f;
    const bool onScreen = reinterpret_cast<ScreenCoords_t>(
        Offsets::FUN_WORLDFRAME_GET_SCREEN_COORDS)(worldFrame, world, screen);
    const void *camera = Game::Read<void *>(worldFrame, Offsets::OFF_GAME_STATE_CAMERA_PTR);
    const float *camPos = Game::Ptr<float>(camera, Offsets::OFF_CAMERA_POSITION);
    const float rel[4] = {world[0] - camPos[0], world[1] - camPos[1], world[2] - camPos[2], 0.0f};
    float out[4];
    reinterpret_cast<Transform_t>(Offsets::FUN_C44_TRANSFORM)(
        out, rel, Game::Ptr<float>(worldFrame, Offsets::OFF_WORLDFRAME_VIEW_PROJECTION));
    screen[2] = out[2];
    return onScreen;
}

// 4.3.4's TargetDirectionData.
struct DirectionData {
    void *worldFrame;
    void *unit;
    void *origin;
    float originPosition[2];
    float facing[2];
    float originScreen[3];
    float screenDirection[2];
    int mode;
    uint64_t bestGuid;
    float cosHalfAngle;
    bool bestInCone;
    float bestDistance;
    float playerPosition[3];
    float maxDistanceSq;
};

// The tab search's creature-type test: skip types flagged never-tab-target.
bool NotTabTarget(void *unit) {
    const int type = reinterpret_cast<CreatureType_t>(Offsets::FUN_UNIT_CREATURE_TYPE)(unit);
    if (type < 0 || type > Game::Read<int>(Offsets::VAR_CREATURETYPE_COUNT))
        return false;
    const auto *const *records = Game::Read<const uint8_t *const *>(Offsets::VAR_CREATURETYPE_RECORDS);
    const uint8_t *rec = records != nullptr ? records[type] : nullptr;
    return rec != nullptr && (Game::Read<uint32_t>(rec, Offsets::OFF_CREATURETYPE_FLAGS) &
                              Offsets::CREATURETYPE_FLAG_NOT_TAB_TARGET) != 0;
}

// 4.3.4's TargetDirectionProc.
int __fastcall DirectionProc(DirectionData *data, void * /*edx*/, uint64_t guid) {
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (unit == nullptr || unit == data->unit || unit == data->origin ||
        !reinterpret_cast<Valid_t>(Offsets::FUN_TARGET_CANDIDATE_VALID)(data->unit, unit,
                                                                         data->mode))
        return 1;

    float position[3];
    if (!Unit::Position::Read(unit, position))
        return 1;
    const float dx = position[0] - data->playerPosition[0];
    const float dy = position[1] - data->playerPosition[1];
    const float dz = position[2] - data->playerPosition[2];
    if (dx * dx + dy * dy + dz * dz > data->maxDistanceSq)
        return 1;
    if (NotTabTarget(unit) ||
        !reinterpret_cast<Visible_t>(Offsets::FUN_WORLD_OBJECT_VISIBLE)(
            Game::Read<void *>(unit, Offsets::OFF_CGOBJECT_WORLD_OBJECT)))
        return 1;

    float screen[3];
    if (!ScreenCoords(data->worldFrame, position, screen))
        return 1;

    bool inCone = true;
    float dirX = position[0] - data->originPosition[0];
    float dirY = position[1] - data->originPosition[1];
    NormalizeSafe(dirX, dirY);
    const float dot = data->facing[1] * dirY + data->facing[0] * dirX;
    float sdX = screen[0] - data->originScreen[0];
    float sdY = screen[1] - data->originScreen[1];
    NormalizeSafe(sdX, sdY);
    if (data->screenDirection[1] * sdY + data->screenDirection[0] * sdX < 0.0f)
        return 1;

    if (data->cosHalfAngle <= dot) {
        if (!data->bestInCone)
            data->bestDistance = FLT_MAX;
    } else {
        if (data->bestInCone)
            return 1;
        inCone = false;
    }

    const float ex = screen[0] - data->originScreen[0];
    const float ey = screen[1] - data->originScreen[1];
    const float ez = screen[2] - data->originScreen[2];
    float distance = ez * ez + ey * ey + ex * ex;
    if (!inCone)
        distance = distance * ((1.0f - dot) * (1.0f - dot));
    if (distance > data->bestDistance)
        return 1;

    data->bestDistance = distance;
    data->bestGuid = guid;
    data->bestInCone = inCone;
    return 1;
}

// 4.3.4's CGGameUI::TargetDirection.
void TargetDirection(float facing, float angle, int mode) {
    if (g_directional != 0)
        return;
    if (Game::Read<void *>(Offsets::VAR_LOCAL_PLAYER_PTR) == nullptr)
        return;
    void *unit = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (unit == nullptr)
        return;
    void *worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    if (worldFrame == nullptr)
        return;
    void *camera = Game::Read<void *>(worldFrame, Offsets::OFF_GAME_STATE_CAMERA_PTR);
    if (camera == nullptr)
        return;

    void *origin = Object::ByGuid(Offsets::TYPEMASK_UNIT, CurrentSelection(), nullptr, 0);
    if (origin == nullptr)
        origin = unit;
    float position[3];
    float screen[3];
    if (!Unit::Position::Read(origin, position) || !ScreenCoords(worldFrame, position, screen)) {
        origin = unit;
        if (!Unit::Position::Read(unit, position))
            return;
        ScreenCoords(worldFrame, position, screen);
    }

    // Camera forward × C33Matrix::Rotation(2pi - facing), row vector.
    float forward[3];
    auto *vtable = Game::Read<uintptr_t *>(camera, 0);
    reinterpret_cast<Forward_t>(vtable[Offsets::VTBL_CAMERA_FORWARD / sizeof(uintptr_t)])(camera,
                                                                                         forward);
    const float c = std::cos(kTwoPi - facing);
    const float s = std::sin(kTwoPi - facing);

    DirectionData data{};
    data.worldFrame = worldFrame;
    data.unit = unit;
    data.origin = origin;
    data.originPosition[0] = position[0];
    data.originPosition[1] = position[1];
    data.facing[0] = forward[0] * c - forward[1] * s;
    data.facing[1] = forward[0] * s + forward[1] * c;
    data.originScreen[0] = screen[0];
    data.originScreen[1] = screen[1];
    data.originScreen[2] = screen[2];
    data.screenDirection[0] = std::sin(facing);
    data.screenDirection[1] = std::cos(facing);
    data.mode = mode;
    data.bestGuid = 0;
    data.cosHalfAngle = std::cos((angle > kMinConeAngle ? angle : kMinConeAngle) * 0.5f);
    data.bestInCone = false;
    data.bestDistance = FLT_MAX;
    if (!Unit::Position::Read(unit, data.playerPosition))
        return;
    // 4.3.4's 41 yd; 1.12's tab search reads it from targetNearestDistance.
    const float range = CVarFloat(Offsets::VAR_CVAR_TARGET_NEAREST_DISTANCE);
    data.maxDistanceSq = range * range;

    reinterpret_cast<EnumVisibleObjects_t>(Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS)(
        reinterpret_cast<EnumCallback_t>(&DirectionProc), &data);

    if (data.bestGuid != 0) {
        SetSelection(data.bestGuid);
        g_directional = data.bestGuid;
    }
}

// 4.3.4 clears the lock in BreakTarget; these are the 1.12 cases it has.
void OnWorldTick() {
    if (g_directional == 0)
        return;
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, g_directional, nullptr, 0);
    if (unit == nullptr) {
        const uint64_t guid = g_directional;
        if (!reinterpret_cast<GroupMember_t>(Offsets::FUN_UNIT_IS_GROUP_MEMBER_OR_PET)(&guid))
            g_directional = 0;
        return;
    }
    const auto *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_CGUNIT_OBJECT_FIELDS);
    if (fields != nullptr && (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
                              Offsets::UNIT_FLAG_NOT_SELECTABLE) != 0)
        g_directional = 0;
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// LeaveWorld: the world→glue return.
void ClearOnLogout() { g_directional = 0; }
const Game::GlueModuleAutoRegister _clearOnLogout{&ClearOnLogout};

int DirectionScript(void *L, int mode, const char *usage) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, usage);
        return 0;
    }
    const float facing = static_cast<float>(Game::Lua::ToNumber(L, 1));
    const float angle =
        Game::Lua::IsNumber(L, 2) ? static_cast<float>(Game::Lua::ToNumber(L, 2)) : 0.0f;
    TargetDirection(facing, angle, mode);
    return 0;
}

// --- Lua entry points -------------------------------------------------------

int __fastcall Script_TargetNearest(void *L) {
    Cycle(L, Offsets::TARGET_MODE_ANY);
    return 0;
}
int __fastcall Script_TargetNearestEnemyPlayer(void *L) {
    Cycle(L, Offsets::TARGET_MODE_ENEMY_PLAYER);
    return 0;
}
int __fastcall Script_TargetNearestFriendPlayer(void *L) {
    Cycle(L, Offsets::TARGET_MODE_FRIEND_PLAYER);
    return 0;
}
int __fastcall Script_TargetDirectionEnemy(void *L) {
    return DirectionScript(L, Offsets::TARGET_MODE_ENEMY, "Usage: TargetDirectionEnemy(facing)");
}
int __fastcall Script_TargetDirectionFriend(void *L) {
    return DirectionScript(L, Offsets::TARGET_MODE_FRIEND, "Usage: TargetDirectionFriend(facing)");
}
int __fastcall Script_TargetDirectionFinished(void * /*L*/) {
    g_directional = 0;
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("TargetNearest", &Script_TargetNearest);
    Game::Lua::RegisterGlobalFunction("TargetNearestEnemyPlayer", &Script_TargetNearestEnemyPlayer);
    Game::Lua::RegisterGlobalFunction("TargetNearestFriendPlayer", &Script_TargetNearestFriendPlayer);
    Game::Lua::RegisterGlobalFunction("TargetDirectionEnemy", &Script_TargetDirectionEnemy);
    Game::Lua::RegisterGlobalFunction("TargetDirectionFriend", &Script_TargetDirectionFriend);
    Game::Lua::RegisterGlobalFunction("TargetDirectionFinished", &Script_TargetDirectionFinished);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Target::Nearest
