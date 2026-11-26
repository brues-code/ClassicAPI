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

// The soft-target icons: `SoftTargetIconEnemy` over `softenemy`,
// `SoftTargetIconFriend` over `softfriend`, `SoftTargetIconInteract` over
// `softinteract` (3.80.2 defaults 0, 0, 1). The image is the cursor the mouse
// shows over the object (Cursor::Hover), its Unable form while the object is
// out of reach; the plain pointer and the other low-priority images show only
// with `SoftTargetLowPriorityIcons` 1. One object gets one icon whatever
// roles it holds, in the order enemy, friend, interact.
//
// 3.80.2 draws the icon in one of two places (docs/notes/soft-targeting-3802.md,
// "Soft-target visuals"):
//   - a unit with a nameplate: the plate's own icon (Blizzard_NamePlates.lua,
//     UpdateSoftTargetIconInternal), SoftTargetNameplateSize (19) square, 0
//     for none. Here at the point 1.12 seats a plate, the unit's name
//     position raised 2/3 yard, projected (GetNamePlateScreenPos), anchored
//     to the world frame's BOTTOMLEFT as SeatNamePlate (0x509EC0) anchors a
//     plate;
//   - any other object: the engine's world icon (FUN_141378e70), above the
//     name. A raid marker takes that place first (FUN_141378700). Here the
//     place is 1.12's raid marker, the quad 3.80.2's icon is drawn as: the
//     unit's name position, raised by the name block and one plate scale
//     while the name shows (PLAYERNAMEDESC::UpdatePlate 0x6C7071 /
//     0x6C70D8). A game object has no name in 1.12; its point is
//     GetNamePosition's no-attachment rule, 1.25 x scale x box height above
//     its base. The icon's bottom centre sits on the point (the quad's
//     corners are (-0.5, 1) to (0.5, 0)).
// The world icon is SoftTargetWorldtextSize render pixels square at every
// distance, scaled by SoftTargetWorldtextNearScale at NearDist down to 1 at
// FarDist from the camera when NearScale is above 1, and it pops in from half
// size (the same steps as 3.80.2, below). SoftTargetIconGameObject is
// registered as 3.80.2 registers it; nothing in its engine reads it.
// The unit SetPreferredGamepadInteractTarget names shows the gamepad interact
// glyph in place of its cursor image (camelot 1.60.1, Target::GamepadInteract).
// Off screen (the projection fails) an icon hides.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cursor/Hover.h"
#include "cvar/Factory.h"
#include "frame/Layout.h"
#include "frame/Region.h"
#include "object/Resolve.h"
#include "target/GamepadInteract.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "texture/Load.h"
#include "tick/FrameTick.h"
#include "unit/Position.h"
#include "unit/RaidTarget.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace Target::SoftTargetIcon {

namespace {

using SetSize_t = void(__thiscall *)(void *anchor, float internal);
using ShowHide_t = void(__fastcall *)(void *tex);
using PlateScreenPos_t = bool(__fastcall *)(void *unit, void *worldFrame, float *screen);
using ScreenCoords_t = bool(__thiscall *)(void *worldFrame, const float *world, float *screen);
using NamePosition_t = void(__thiscall *)(void *unit, float out[3]);
using IsDeadOrGhost_t = bool(__thiscall *)(void *unit);

constexpr int kTypeMask =
    Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_GAMEOBJECT | Offsets::TYPEMASK_CORPSE;

CVar::Factory::Handle g_enemy = nullptr;
CVar::Factory::Handle g_friend = nullptr;
CVar::Factory::Handle g_interact = nullptr;
CVar::Factory::Handle g_lowPriority = nullptr;
CVar::Factory::Handle g_plateSize = nullptr;
CVar::Factory::Handle g_worldSize = nullptr;
CVar::Factory::Handle g_nearScale = nullptr;
CVar::Factory::Handle g_nearDist = nullptr;
CVar::Factory::Handle g_farDist = nullptr;

struct Icon {
    void *tex;
    char path[Offsets::CURSOR_FILE_SIZE];
    float shownSize;
    bool shown;
    uint64_t guid; // the object the pop-in belongs to
    float pop;     // the world icon's pop-in scale, 0 while it is not drawn
};

// One per soft unit: enemy, friend, interact.
constexpr int kIcons = 3;
Icon g_icons[kIcons] = {};
void *g_parent = nullptr; // the world frame the icons' textures belong to

void *Anchor(void *region) { return static_cast<uint8_t *>(region) + Offsets::OFF_REGION_ANCHOR; }

void *CreateTexture(void *parent) {
    return Frame::Region::CreateTexture(parent, Offsets::DRAWLAYER_ARTWORK, 0);
}

void SetShown(Icon &icon, bool shown) {
    if (icon.tex == nullptr || shown == icon.shown)
        return;
    icon.shown = shown;
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(icon.tex) +
                                  Offsets::OFF_REGION_DESIRED_SHOWN) = shown ? 1 : 0;
    reinterpret_cast<ShowHide_t>(shown ? Offsets::FUN_REGION_SHOW
                                       : Offsets::FUN_REGION_HIDE)(icon.tex);
}

// 3.80.2's smoothing step (FUN_142a5c1d0): toward `target` by
// (1 - e^(-rate dt)) of the gap, at least minRate dt and at most maxRate dt
// per call, landing on it once the gap is within minRate dt.
float Approach(float cur, float target, float minRate, float maxRate, float rate, float dt) {
    const float minStep = minRate * dt;
    const float gap = target - cur;
    const float sign = gap > 0.0f ? 1.0f : (gap < 0.0f ? -1.0f : 0.0f);
    if (!(minStep < sign * gap))
        return target;
    float step = (1.0f - std::exp(-(rate * dt))) * sign * gap;
    if (step < minStep)
        step = minStep;
    else if (step > maxRate * dt)
        step = maxRate * dt;
    return step * sign + cur;
}

// The pop-in (0x141379B5B-0x141379BB8): from at least half size toward 1,
// rate 35, steps of 1 to 1000 per second, while it is more than 2^-21 off.
float Pop(Icon &icon) {
    if (std::fabs(icon.pop - 1.0f) >= 4.76837158203125e-07f)
        icon.pop = Approach(std::fmax(icon.pop, 0.5f), 1.0f, 1.0f, 1000.0f, 35.0f,
                            Tick::FrameTick::Elapsed());
    return icon.pop;
}

// 1.12's plate scale (UpdatePlate 0x6C6F10-0x6C6F3C): 0.2, or d / 4 x 1.5 x
// 0.2 for a name position d > 4 above the feet.
float NameScale(void *unit, const float namePos[3]) {
    const uint32_t scaleBits = Offsets::UNIT_NAME_SCALE;
    float scale;
    std::memcpy(&scale, &scaleBits, sizeof scale);
    float feet[3];
    if (!Unit::Position::Read(unit, feet))
        return scale;
    const float d = namePos[2] - feet[2];
    const float high = Game::Read<float>(Offsets::VAR_UNIT_NAME_SCALE_HEIGHT);
    if (d > high)
        scale = d / high * Game::Read<float>(Offsets::VAR_UNIT_NAME_SCALE_GROWTH) *
                Game::Read<float>(Offsets::VAR_UNIT_NAME_SCALE_BASE);
    return scale;
}

// The world icon's point and the name position it is measured from, false
// when the place is the unit's raid marker or the object has no position.
bool WorldPoint(void *object, bool isUnit, uint64_t guid, float point[3], float namePos[3]) {
    if (!isUnit) {
        if (!Unit::Position::Read(object, namePos))
            return false;
        namePos[2] += Game::Read<float>(object, Offsets::OFF_CGOBJECT_SCALE) *
                      Game::Read<float>(object, Offsets::OFF_CGOBJECT_BOX_HEIGHT) *
                      Game::Read<float>(Offsets::VAR_NAME_HEIGHT_FACTOR);
        std::memcpy(point, namePos, 3 * sizeof(float));
        return true;
    }
    if (Unit::RaidTarget::MarkOf(guid) != 0 &&
        !reinterpret_cast<IsDeadOrGhost_t>(Offsets::FUN_UNIT_IS_DEAD_OR_GHOST)(object))
        return false;
    reinterpret_cast<NamePosition_t>(Offsets::FUN_UNIT_GET_NAME_POSITION)(object, namePos);
    std::memcpy(point, namePos, 3 * sizeof(float));
    void *desc = Game::Read<void *>(object, Offsets::OFF_UNIT_NAME_DESC);
    if (desc != nullptr && Game::Read<void *>(desc, Offsets::OFF_NAMEDESC_TEXT) != nullptr)
        point[2] += Game::Read<float>(desc, Offsets::OFF_NAMEDESC_BLOCK_HEIGHT) +
                    NameScale(object, namePos);
    return true;
}

// FUN_141378E70's size: SoftTargetWorldtextSize, or with NearScale above 1
// and NearDist below FarDist, size x NearScale at NearDist blending to size
// at FarDist by the camera's distance to the name position.
float WorldPixels(void *worldFrame, const float namePos[3]) {
    const float size = CVar::Factory::GetFloat(g_worldSize);
    const float nearScale = CVar::Factory::GetFloat(g_nearScale);
    const float nearDist = CVar::Factory::GetFloat(g_nearDist);
    const float farDist = CVar::Factory::GetFloat(g_farDist);
    if (!(1.0f < nearScale && nearDist < farDist))
        return size;
    float dist = 1.0f;
    if (void *camera = Game::Read<void *>(worldFrame, Offsets::OFF_GAME_STATE_CAMERA_PTR)) {
        const float *cam = Game::Ptr<float>(camera, Offsets::OFF_CAMERA_POSITION);
        const float dx = namePos[0] - cam[0], dy = namePos[1] - cam[1], dz = namePos[2] - cam[2];
        dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    const float t = dist < nearDist ? nearDist : (dist > farDist ? farDist : dist);
    return (t - nearDist) / (farDist - nearDist) * (size - size * nearScale) + size * nearScale;
}

// Render pixels to layout units on the vertical axis: the screen is
// VAR_COORD_DDC_HEIGHT_SCALE units and VAR_TEXT_RASTER_Y pixels high.
float RenderPixelsToInternal(float px) {
    const int rasterY = Game::Read<int>(Offsets::VAR_TEXT_RASTER_Y);
    if (rasterY <= 0)
        return 0.0f;
    return px * Game::Read<float>(Offsets::VAR_COORD_DDC_HEIGHT_SCALE) / static_cast<float>(rasterY);
}

void Hide(Icon &icon) {
    icon.pop = 0.0f;
    SetShown(icon, false);
}

void Show(Icon &icon, uint64_t guid, void *worldFrame) {
    if (guid != icon.guid) {
        icon.guid = guid;
        icon.pop = 0.0f;
    }
    void *object = guid != 0 ? Object::ByGuid(kTypeMask, guid) : nullptr;
    char path[Offsets::CURSOR_FILE_SIZE];
    const char *glyph = GamepadInteract::GlyphFor(guid);
    if (object == nullptr ||
        (glyph == nullptr &&
         !Cursor::Hover::ImagePath(object, Cursor::Hover::Mode(object),
                                   CVar::Factory::GetInt(g_lowPriority) != 0, path, sizeof path))) {
        Hide(icon);
        return;
    }
    if (glyph != nullptr)
        Common::BoundedCopy(path, glyph, sizeof path);
    const bool isUnit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) != nullptr;
    float screen[2];
    float size;
    if (isUnit && Game::Read<void *>(object, Offsets::OFF_UNIT_NAMEPLATE) != nullptr) {
        icon.pop = 0.0f;
        const int px = CVar::Factory::GetInt(g_plateSize);
        if (px <= 0 || !reinterpret_cast<PlateScreenPos_t>(Offsets::FUN_UNIT_GET_NAMEPLATE_SCREEN_POS)(
                           object, worldFrame, screen)) {
            SetShown(icon, false);
            return;
        }
        size = Game::PixelToInternal(static_cast<float>(px));
    } else {
        float point[3], namePos[3];
        if (!WorldPoint(object, isUnit, guid, point, namePos) ||
            !reinterpret_cast<ScreenCoords_t>(Offsets::FUN_WORLDFRAME_GET_SCREEN_COORDS)(
                worldFrame, point, screen)) {
            Hide(icon);
            return;
        }
        size = RenderPixelsToInternal(WorldPixels(worldFrame, namePos) * Pop(icon));
    }
    if (icon.tex == nullptr && (icon.tex = CreateTexture(worldFrame)) == nullptr)
        return;
    if (std::strcmp(path, icon.path) != 0) {
        Texture::SetFile(icon.tex, path);
        std::memcpy(icon.path, path, sizeof icon.path);
    }
    if (size != icon.shownSize) {
        reinterpret_cast<SetSize_t>(Offsets::FUN_LAYOUT_SET_WIDTH)(Anchor(icon.tex), size);
        reinterpret_cast<SetSize_t>(Offsets::FUN_LAYOUT_SET_HEIGHT)(Anchor(icon.tex), size);
        icon.shownSize = size;
    }
    Frame::Layout::SetPoint(icon.tex, Offsets::FRAMEPOINT_BOTTOM, Anchor(worldFrame),
                            Offsets::FRAMEPOINT_BOTTOMLEFT, screen[0], screen[1]);
    SetShown(icon, true);
}

void Update() {
    void *worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    if (worldFrame != g_parent) {
        g_parent = worldFrame;
        for (Icon &icon : g_icons)
            icon = {};
    }
    uint64_t guids[kIcons] = {};
    if (worldFrame != nullptr) {
        if (CVar::Factory::GetInt(g_enemy) != 0)
            guids[0] = SoftUnit::Get(SoftUnit::Side::Enemy);
        if (CVar::Factory::GetInt(g_friend) != 0)
            guids[1] = SoftUnit::Get(SoftUnit::Side::Friend);
        if (CVar::Factory::GetInt(g_interact) != 0)
            guids[2] = SoftInteract::Get();
    }
    // One icon per object, in the plate's order: enemy, friend, interact.
    for (int i = 1; i < kIcons; ++i)
        for (int j = 0; j < i; ++j)
            if (guids[i] == guids[j])
                guids[i] = 0;
    for (int i = 0; i < kIcons; ++i)
        Show(g_icons[i], guids[i], worldFrame);
}

const Tick::FrameTick::AutoSubscribe _tick{&Update};

// Before /reload tears the interface down: hide the icons while their world
// frame is still alive, and let the next one get textures of its own.
void PrepareForReload() {
    if (g_parent == Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR))
        for (Icon &icon : g_icons)
            SetShown(icon, false);
    g_parent = nullptr;
    for (Icon &icon : g_icons)
        icon = {};
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

// 3.80.2's registrations: names, defaults and help (the Worldtext cvars have
// none).
void Register() {
    const auto reg = [](const char *name, const char *help, const char *value) {
        return CVar::Factory::Register(name, help, Offsets::CVAR_CATEGORY_GAME, value);
    };
    g_enemy = reg("SoftTargetIconEnemy", "Show icon for soft enemy target", "0");
    g_friend = reg("SoftTargetIconFriend", "Show icon for soft friend target", "0");
    g_interact = reg("SoftTargetIconInteract", "Show icon for soft interact target", "1");
    reg("SoftTargetIconGameObject", "Show icon for sot interact game objects (interactable objects you cannot normally target)", "0");
    g_lowPriority = reg("SoftTargetLowPriorityIcons", "Show interact icons even when there is other visual indicators, such as quest or loot effects", "0");
    g_plateSize = reg("SoftTargetNameplateSize", "Size of soft target icon on nameplate (0 to disable)", "19");
    g_worldSize = reg("SoftTargetWorldtextSize", nullptr, "32");
    g_nearScale = reg("SoftTargetWorldtextNearScale", nullptr, "1");
    g_nearDist = reg("SoftTargetWorldtextNearDist", nullptr, "4");
    g_farDist = reg("SoftTargetWorldtextFarDist", nullptr, "40");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Target::SoftTargetIcon
