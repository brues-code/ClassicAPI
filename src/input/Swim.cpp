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

// Swimming up and down while a command is held: the pairs later clients bind
// to JUMP and SITORSTAND, as the 3.80.2 client has them. 1.12's own JUMP and
// SITORSTAND bindings stay its own; ClassicAPI's Bindings.xml gives the
// pairs bindings of their own (JUMPORASCEND, SITSTANDORDESCEND), and a
// gamepad UI calls them.
//   - JumpOrAscendStart holds the ascend command; a swimmer stops there, and
//     anyone else jumps (1.12's own Jump).
//   - AscendStop lets it go.
//   - SitStandOrDescendStart (0x140D7A680) holds the descend command, then
//     toggles sit / stand. 1.12's own SitOrStand does the toggle, and its
//     stand-state setter refuses a swimmer as 3.80.2's refuses one.
//   - DescendStop (0x14125C870) lets it go.
// Holding both nets out, as 3.3.5's PlotAscendDescend does.
//
// 1.12 has no ascend or descend flag or opcode: a swimmer moves vertically
// only by its pitch times a forward or backward move, and the server allows a
// height change only while a move flag is set. So while a command is held and
// the mover swims, the climb or sink is sent as 1.12's own pitch and forward:
// no key, straight up or down; forward, 45 degrees up or down; backward, 45
// degrees back and up or down. A frame step does it with the engine's own
// calls and hooks nothing:
//   - CGUnit_C::SetPitch holds the pitch;
//   - CInputControl::SetCommands presses forward when no key moves the
//     swimmer (a second keyboard, as the gamepad stick presses its moves);
//   - a rise meets the surface at depth 0.75h (the swim decision's factor,
//     within the mover's skin): CWorld::GetEntityLight gives the cached
//     liquid surface the camera's water band reads for its target, and
//     CGUnit_C::Jump takes the swimmer out once per swim, as later clients'
//     resolver jumps an ascending swimmer at a liquid face.
// When the command goes, or the mover leaves the water, the forward it
// pressed goes up and the pitch it found comes back. Nothing runs while
// neither command is held.
//
// A mouselook motion hands the camera's pitch to the body; the next frame
// step sets the climb's again. The gamepad camera's hand-off waits instead
// (HoldingPitch).

#include "input/Swim.h"

#include "Game.h"
#include "Offsets.h"
#include "gamepad/Movement.h"
#include "tick/FrameTick.h"
#include "time/Clock.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>

namespace Input::Swim {

namespace {

constexpr float kHalfPi = 1.57079637f;    // the engine's pitch clamp
constexpr float kQuarterPi = 0.785398185f;

bool g_ascend = false;      // the ascend command
bool g_descend = false;     // the descend command
bool g_ownsForward = false; // the forward this pressed
bool g_holdsPitch = false;
float g_restorePitch = 0.0f;
bool g_surfaced = false;    // this swim's rise has jumped out

void *InputControl() { return Game::Read<void *>(Offsets::VAR_UI_INPUT_CONTROLLER); }

uint32_t Held() {
    const void *ic = InputControl();
    return ic != nullptr ? Game::Read<uint32_t>(ic, Offsets::OFF_UI_INPUT_FLAGS) : 0;
}

void SetForward(bool press, uint32_t now) {
    void *ic = InputControl();
    if (ic == nullptr)
        return;
    using SetCommands_t = void(__fastcall *)(void *ic, void *edx, uint32_t bits, uint32_t press, uint32_t time,
                                             uint32_t extra);
    reinterpret_cast<SetCommands_t>(Offsets::FUN_INPUT_SET_COMMANDS)(ic, nullptr, Offsets::INPUT_FLAG_MOVE_FORWARD,
                                                                     press ? 1 : 0, now, 0);
    g_ownsForward = press;
}

const uint8_t *Movement(const void *unit) {
    return Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
}

bool Swimming(const void *unit) {
    const uint8_t *mv = Movement(unit);
    return mv != nullptr && (Game::Read<uint32_t>(mv, Offsets::OFF_MOVEMENT_FLAGS) & Offsets::MOVEFLAG_SWIMMING) != 0;
}

float Pitch(const void *unit) {
    const uint8_t *mv = Movement(unit);
    return mv != nullptr ? Game::Read<float>(mv, Offsets::OFF_MOVEMENT_PITCH) : 0.0f;
}

void SetPitch(void *unit, uint32_t now, float pitch) {
    using SetPitch_t = void(__fastcall *)(void *unit, void *edx, uint32_t time, float pitch);
    reinterpret_cast<SetPitch_t>(Offsets::FUN_UNIT_SET_PITCH)(unit, nullptr, now, pitch);
}

// The swimmer is at the surface: its depth below the cached liquid surface
// is under 0.75h plus the mover's skin.
bool AtSurface(void *unit) {
    void *entity = Game::Read<void *>(unit, Offsets::OFF_CGOBJECT_WORLD_OBJECT);
    if (entity == nullptr)
        return false;
    uint32_t type = 0, flag = 0;
    float surface = 0.0f, out[3] = {};
    using GetLiquid_t = int(__fastcall *)(void *entity, uint32_t *type, float *surface, float *out, uint32_t *flag);
    if (reinterpret_cast<GetLiquid_t>(Offsets::FUN_WORLD_GET_ENTITY_LIQUID)(entity, &type, &surface, out, &flag) == 0)
        return false;
    float position[3];
    if (!Unit::Position::Read(unit, position))
        return false;
    const float rest = Game::Read<float>(unit, Offsets::OFF_UNIT_COLLISION_HEIGHT) *
                       Game::Read<float>(Offsets::VAR_SWIM_DEPTH_FACTOR);
    return surface - position[2] < rest + Game::Read<float>(Offsets::VAR_MOVER_SKIN);
}

// +1 rising, -1 sinking, 0 neither.
int Vertical() { return g_ascend == g_descend ? 0 : (g_ascend ? 1 : -1); }

// The forward axis the input tick sums (0x00514DA0): autorun, forward, both
// mouse buttons, less backward. A forward this pressed is not the player's.
int ForwardAxis(uint32_t held) {
    if (g_ownsForward)
        held &= ~static_cast<uint32_t>(Offsets::INPUT_FLAG_MOVE_FORWARD);
    int axis = 0;
    if (held & Offsets::INPUT_FLAG_AUTORUN)
        ++axis;
    if (held & Offsets::INPUT_FLAG_MOVE_FORWARD)
        ++axis;
    if ((held & Offsets::INPUT_FLAG_MOUSELOOK) && (held & Offsets::INPUT_FLAG_FREE_LOOK))
        ++axis;
    if (held & Offsets::INPUT_FLAG_MOVE_BACKWARD)
        --axis;
    return axis;
}

void Release(void *unit, uint32_t now) {
    if (g_ownsForward)
        SetForward(false, now);
    if (g_holdsPitch) {
        g_holdsPitch = false;
        if (unit != nullptr)
            SetPitch(unit, now, g_restorePitch);
    }
}

void OnFrame() {
    if (!g_ascend && !g_descend && !g_ownsForward && !g_holdsPitch)
        return;
    const uint32_t now = Time::Clock::NowMs();
    void *unit = GamePad::Movement::ActiveMover();
    const bool swimming = unit != nullptr && Swimming(unit);
    if (!swimming)
        g_surfaced = false;
    const int vertical = Vertical();
    if (vertical == 0 || !swimming) {
        Release(unit, now);
        return;
    }
    const int axis = ForwardAxis(Held());
    const float pitch = (axis > 0 ? kQuarterPi : axis < 0 ? -kQuarterPi : kHalfPi) * static_cast<float>(vertical);
    if (!g_holdsPitch) {
        g_holdsPitch = true;
        g_restorePitch = Pitch(unit);
    }
    if (std::fabs(Pitch(unit) - pitch) > 1e-4f)
        SetPitch(unit, now, pitch);
    // No input moves the swimmer: press forward; the player's own move
    // takes it back.
    const bool needForward = axis == 0;
    if (needForward && (!g_ownsForward || (Held() & Offsets::INPUT_FLAG_MOVE_FORWARD) == 0))
        SetForward(true, now);
    else if (!needForward && g_ownsForward)
        SetForward(false, now);
    if (vertical > 0 && !g_surfaced && AtSurface(unit)) {
        g_surfaced = true;
        using Jump_t = void(__fastcall *)(void *unit, void *edx, uint32_t time);
        reinterpret_cast<Jump_t>(Offsets::FUN_UNIT_JUMP)(unit, nullptr, now);
    }
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};

int __fastcall Script_JumpOrAscendStart(void *L) {
    g_ascend = true;
    void *unit = GamePad::Movement::ActiveMover();
    if (unit != nullptr && Swimming(unit))
        return 0;
    return reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_JUMP)(L);
}

int __fastcall Script_AscendStop(void *) {
    g_ascend = false;
    return 0;
}

int __fastcall Script_SitStandOrDescendStart(void *L) {
    g_descend = true;
    return reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_SIT_OR_STAND)(L);
}

int __fastcall Script_DescendStop(void *) {
    g_descend = false;
    return 0;
}

const Game::Doc::Function kJumpOrAscendStart{
    "Jumps; under water, swims up until AscendStop, and jumps out at the surface.", {}, {}, "InputControl"};
const Game::Doc::Function kAscendStop{"Stops swimming up.", {}, {}, "InputControl"};
const Game::Doc::Function kSitStandOrDescendStart{
    "Sits or stands; under water, swims down until DescendStop.", {}, {}, "InputControl"};
const Game::Doc::Function kDescendStop{"Stops swimming down.", {}, {}, "InputControl"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("JumpOrAscendStart", &Script_JumpOrAscendStart, &kJumpOrAscendStart);
    Game::Lua::RegisterGlobalFunction("AscendStop", &Script_AscendStop, &kAscendStop);
    Game::Lua::RegisterGlobalFunction("SitStandOrDescendStart", &Script_SitStandOrDescendStart,
                                      &kSitStandOrDescendStart);
    Game::Lua::RegisterGlobalFunction("DescendStop", &Script_DescendStop, &kDescendStop);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// A reload: the commands go, and so do the forward and pitch they held (the
// engine's command word would keep the forward pressed).
void Reset() {
    g_ascend = g_descend = false;
    Release(GamePad::Movement::ActiveMover(), Time::Clock::NowMs());
}

const Game::ReloadAutoRegister _reload{&Reset};

} // namespace

bool HoldingPitch() { return g_holdsPitch; }

} // namespace Input::Swim
