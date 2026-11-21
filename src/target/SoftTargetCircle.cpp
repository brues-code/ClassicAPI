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

// The soft-target circles: `softenemy`, `softfriend` and a `softinteract`
// unit get retail's soft circle. No cvar of their own: they follow
// SoftTargetEnemy / SoftTargetFriend / SoftTargetInteract and the engine's
// ObjectSelectionCircle.
//
// Retail's soft circle is not the target's circle (retail screenshots, the
// same unit soft and locked, docs/notes/soft-targeting-3802.md "Soft-target
// visuals"). It is drawn with textures/unitselecttexture-full.blp
// (FileDataID 1246566: an even ring over a filled disc, no facing tip),
// embedded here at Textures\UnitSelectTexture-Full.blp. 1.12 has no file
// there; retail's DXT5 is stored palettized with 8-bit alpha, the format of
// 1.12's own ring. From three screenshot pairs (hostile, neutral twice):
//   - radius 0.75 of the target's: ring widths 0.76-0.77, scaled by where
//     each texture's bright edge sits (111/128 for the target ring, 114/128
//     for this one);
//   - colour halfway from the reaction colour to white: the added blue over
//     red is 0.54-0.58 for yellow and red units, where the target ring adds
//     none;
//   - alpha 0.55 of the target's. Against retail's target ring the fit is a
//     third (edge peaks after each texture's screen-size blur 0.31 and
//     0.32, the disc fill 0.30-0.42), but that ring is retail's new thin
//     one, about as bright on screen as the soft ring (+60 red over the
//     grass against +56). 1.12 keeps the old bright arc and draws a circle
//     dimmer than retail: at a third the soft ring added +31 red in 1.12
//     (user screenshot), so it read white. 0.55 gives retail's +56.
// 3.80.2 has no soft circle: it draws its soft slots with the target's call
// (FUN_14119d880), and loads -Full without drawing it.
//
// A loose target (Target::Reason) gets the soft circle in place of the
// target's: retail with SoftTargetForce 1 draws the forced target's circle as
// a soft one, and a click on it (locking it) gives the full target circle.
// The engine draws the target's circle only through the unit's slot 12
// (FUN_UNIT_DRAW_SELECTION_CIRCLE), so that is where the swap is made.
//
// The soft circles are drawn at the base per-frame step, where 1.12 draws the
// target's (FUN_CGOBJECT_FRAME_UPDATE, per entity inside DrawEntityModels, so
// the circle lands before the entity's blob shadow), by a mirror of the
// unit's slot 12 (0x608E00) that differs in the radius, the colour and the
// texture the circle state binds. Units only: 3.80.2 skips game objects, and
// 1.12's circle is a unit's.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/Reason.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "texture/Load.h"
#include "tick/FrameTick.h"

#include <cstdint>
#include <cstring>

namespace Target::SoftTargetCircle {

namespace {

using FrameUpdate_t = void(__fastcall *)(void *obj, void *edx, int arg);
using GetPosition_t = float *(__thiscall *)(void *obj, float out[3]);
using SelectionColor_t = void(__thiscall *)(void *obj, uint32_t *argb);
using YawMatrix_t = void(__thiscall *)(void *obj, float m[16], const float point[3]);
using CircleState_t = void(__thiscall *)(void *obj);
using DrawDecal_t = void(__fastcall *)(const float box[6], const uint32_t *argb, const float m[16],
                                       float fade, uint32_t flags);
using GxState_t = void(__cdecl *)();
using DrawTargetCircle_t = void(__fastcall *)(void *unit, void *edx);

constexpr const char *kTexture = "Textures\\UnitSelectTexture-Full.blp";
constexpr float kScale = 0.75f;

FrameUpdate_t g_origFrameUpdate = nullptr;
DrawTargetCircle_t g_origDrawTargetCircle = nullptr;

// The soft units (FrameTick): enemy, friend, interact. 0 when the side is
// off, or the interact is not a unit.
uint64_t g_soft[3] = {};

// The soft circle's texture, loaded on the first draw as 1.12 loads its ring
// (0x006147BE: filter 1, clamped) and kept: one reference for the DLL's life.
void *g_texture = nullptr;
bool g_textureTried = false;

template <typename Fn> Fn Slot(void *obj, int offset) {
    return Game::Read<Fn>(Game::Read<void *>(obj, 0), offset);
}

// The reaction colour halfway to white, at 0.55 of its alpha (140 of 255).
uint32_t SoftColor(uint32_t argb) {
    uint32_t out = (argb >> 24) * 140 / 255 << 24;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t c = argb >> shift & 0xFF;
        out |= (c + (0xFF - c) / 2) << shift;
    }
    return out;
}

// 0x608E00 with the radius scaled, the soft colour, and the circle state
// binding the soft texture: FUN_SELECTION_CIRCLE_STATE reads its stage-0
// texture from VAR_SELECTION_CIRCLE_TEXTURE when it runs, so the global holds
// the soft texture for that call only.
void DrawSoftCircle(void *unit) {
    if (!g_textureTried) {
        g_textureTried = true;
        g_texture = Texture::LoadByPath(kTexture, Texture::Flags(1, false, false));
    }
    void **stage0 = reinterpret_cast<void **>(Offsets::VAR_SELECTION_CIRCLE_TEXTURE);
    if (g_texture == nullptr || *stage0 == nullptr)
        return;

    const auto getPosition = Slot<GetPosition_t>(unit, Offsets::OFF_CGOBJECT_VTBL_GET_POSITION);
    float pos[3];
    getPosition(unit, pos);
    const float r =
        kScale * Game::Read<float>(unit, Offsets::OFF_CGUNIT_SELECTION_RADIUS) *
        Game::Read<float>(::Object::Data(unit), Offsets::OFF_OBJECT_FIELD_SCALE_X);
    const float box[6] = {pos[0] - r, pos[1] - r, pos[2] - 2.0f * r,
                          pos[0] + r, pos[1] + r, pos[2] + 2.0f * r};

    uint32_t argb = 0;
    Slot<SelectionColor_t>(unit, Offsets::OFF_CGOBJECT_VTBL_SELECTION_COLOR)(unit, &argb);
    argb = SoftColor(argb);

    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float point[3];
    reinterpret_cast<YawMatrix_t>(Offsets::FUN_BUILD_YAW_MATRIX)(unit, m,
                                                                 getPosition(unit, point));

    reinterpret_cast<GxState_t>(Offsets::FUN_GX_STATE_PUSH)();
    void *ring = *stage0;
    *stage0 = g_texture;
    reinterpret_cast<CircleState_t>(Offsets::FUN_SELECTION_CIRCLE_STATE)(unit);
    *stage0 = ring;
    const uint32_t fadeBits = Offsets::DECAL_SELECTION_FADE;
    float fade;
    std::memcpy(&fade, &fadeBits, sizeof fade);
    reinterpret_cast<DrawDecal_t>(Offsets::FUN_DRAW_DECAL)(box, &argb, m, fade,
                                                           Offsets::DECAL_SELECTION_FLAGS);
    reinterpret_cast<GxState_t>(Offsets::FUN_GX_STATE_POP)();
}

// The engine's own gate first (0x614AA5-0x614AD4: not the target, the
// cvar on), then a soft unit's circle where the target's would be drawn.
void __fastcall FrameUpdate_h(void *obj, void *edx, int arg) {
    const uint64_t guid = ::Object::Guid(obj);
    if (guid != 0 && (guid == g_soft[0] || guid == g_soft[1] || guid == g_soft[2]) &&
        guid != Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO)) {
        void *cvar = Game::Read<void *>(Offsets::VAR_CVAR_OBJECT_SELECTION_CIRCLE);
        if (cvar != nullptr && CVar::Factory::GetInt(cvar) != 0)
            DrawSoftCircle(obj);
    }
    g_origFrameUpdate(obj, edx, arg);
}

const Game::HookAutoRegister _hook{Offsets::FUN_CGOBJECT_FRAME_UPDATE,
                                   reinterpret_cast<void *>(&FrameUpdate_h),
                                   reinterpret_cast<void **>(&g_origFrameUpdate)};

// The target's circle (slot 12, called only for the target): the soft one
// for a loose target.
void __fastcall DrawTargetCircle_h(void *unit, void *edx) {
    if (Reason::IsLoose())
        DrawSoftCircle(unit);
    else
        g_origDrawTargetCircle(unit, edx);
}

const Game::HookAutoRegister _circleHook{Offsets::FUN_UNIT_DRAW_SELECTION_CIRCLE,
                                         reinterpret_cast<void *>(&DrawTargetCircle_h),
                                         reinterpret_cast<void **>(&g_origDrawTargetCircle)};

// Picked on the UI frame, after the scene: the scan takes only objects the
// scene drew this frame, so a pick from WorldTick (before the scene) found
// none (probe: softenemy resolved from Lua, the WorldTick cache stayed 0).
// The circles draw the next scene, as the icons do.
void OnFrameTick() {
    g_soft[0] = SoftUnit::Get(SoftUnit::Side::Enemy);
    g_soft[1] = SoftUnit::Get(SoftUnit::Side::Friend);
    const uint64_t interact = SoftInteract::Get();
    g_soft[2] = Object::ByGuid(Offsets::TYPEMASK_UNIT, interact) != nullptr ? interact : 0;
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

} // namespace

} // namespace Target::SoftTargetCircle
