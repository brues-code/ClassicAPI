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
// `softinteract`. Retail defaults Interact to 1, here it is 0 so the icon is
// opted into, and the pick does not run every frame for players who never
// look at it; Enemy and Friend are 0 in retail too. A game object's icon also
// needs `SoftTargetIconGameObject` (retail's Accessibility "Interact Key
// Icons" option: Default sets Enemy 0, Interact 1, GameObject 0; Show All
// sets all three 1).
//
// Retail's nameplate driver (Blizzard_NamePlates.lua,
// UpdateSoftTargetIconInternal) gives a unit's plate the icon when the unit is
// softenemy, softfriend or softinteract with that side's cvar on, and the
// image is SetUnitCursorTexture's: the cursor the mouse shows over the unit.
// So one unit gets one icon whatever roles it holds, and each soft unit gets
// its own. The plain pointer (a party member: nothing to do with it) is a
// low-priority image, shown only with `SoftTargetLowPriorityIcons` 1 (retail
// probe: no icon over a party member as softfriend until it was 1; the
// Accessibility option's Show All sets it). The interact icon for an object with no plate is the engine's
// (retail screenshots: the speak bubble over an NPC's name, the mail icon over
// a mailbox, grey then coloured as the object comes into reach).
//
// The engine pieces:
//   - the image is the hover cursor's (Cursor::Hover), Unable form included;
//   - the point is where 1.12 seats a nameplate: a unit's name position
//     raised 2/3 yard, projected (GetNamePlateScreenPos). An object with no
//     name position gets GetNamePosition's own no-attachment rule (1.25 x
//     scale x box height above its base) and the same lift, projected the
//     same way;
//   - each icon is a texture on the world frame anchored to the world frame's
//     BOTTOMLEFT at that point, the anchor SeatNamePlate (0x509EC0) gives a
//     plate (without its overlap solving);
//   - its size is SoftTargetNameplateSize (retail default 19), the size of
//     the icon the nameplate shows.
// Off screen (the projection fails) an icon hides, as a plate is dropped.

#include "Game.h"
#include "Offsets.h"
#include "cursor/Hover.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "tick/FrameTick.h"
#include "unit/Position.h"

#include <cstdint>
#include <cstring>

namespace Target::SoftTargetIcon {

namespace {

using PoolAlloc_t = void *(__thiscall *)(void *pool, int zeroInit, const char *tag, int line);
using TexCtor_t = void *(__thiscall *)(void *mem, void *parent, int layer, int sublayer);
using SetTexture_t = uint32_t(__thiscall *)(void *tex, const char *path, int a, uint32_t blend,
                                            int b);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);
using SetSize_t = void(__thiscall *)(void *anchor, float internal);
using ShowHide_t = void(__fastcall *)(void *tex);
using PlateScreenPos_t = bool(__fastcall *)(void *unit, void *worldFrame, float *screen);
using ScreenCoords_t = bool(__thiscall *)(void *worldFrame, const float *world, float *screen);

constexpr int kTypeMask =
    Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_GAMEOBJECT | Offsets::TYPEMASK_CORPSE;

CVar::Factory::Handle g_enemy = nullptr;
CVar::Factory::Handle g_friend = nullptr;
CVar::Factory::Handle g_interact = nullptr;
CVar::Factory::Handle g_gameObject = nullptr;
CVar::Factory::Handle g_lowPriority = nullptr;
CVar::Factory::Handle g_size = nullptr;

struct Icon {
    void *tex;
    char path[Offsets::CURSOR_FILE_SIZE];
    float shownSize;
    bool shown;
};

// One per soft unit: enemy, friend, interact.
constexpr int kIcons = 3;
Icon g_icons[kIcons] = {};
void *g_parent = nullptr; // the world frame the icons' textures belong to

void *Anchor(void *region) { return static_cast<uint8_t *>(region) + Offsets::OFF_REGION_ANCHOR; }

void *CreateTexture(void *parent) {
    auto alloc = reinterpret_cast<PoolAlloc_t>(Offsets::FUN_REGION_POOL_ALLOC);
    void *mem = alloc(reinterpret_cast<void *>(Offsets::VAR_SIMPLETEXTURE_POOL), 0,
                      reinterpret_cast<const char *>(Offsets::VAR_SIMPLETEXTURE_CLASS_TAG), -2);
    if (mem == nullptr)
        return nullptr;
    return reinterpret_cast<TexCtor_t>(Offsets::FUN_SIMPLETEXTURE_CTOR)(
        mem, parent, Offsets::DRAWLAYER_ARTWORK, 0);
}

void SetShown(Icon &icon, bool shown) {
    if (icon.tex == nullptr || shown == icon.shown)
        return;
    icon.shown = shown;
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(icon.tex) +
                                  Offsets::OFF_REGION_DESIRED_SHOWN) = shown ? 1 : 0;
    reinterpret_cast<ShowHide_t>(shown ? Offsets::FUN_FONTSTRING_SHOW
                                       : Offsets::FUN_FONTSTRING_HIDE)(icon.tex);
}

// The screen point the icon's bottom sits on, false when it is off screen.
bool Project(uint64_t guid, void *object, void *worldFrame, float screen[2]) {
    if (Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) != nullptr)
        return reinterpret_cast<PlateScreenPos_t>(Offsets::FUN_UNIT_GET_NAMEPLATE_SCREEN_POS)(
            object, worldFrame, screen);
    float world[3];
    if (!Unit::Position::Read(object, world))
        return false;
    world[2] += Game::Read<float>(object, Offsets::OFF_CGOBJECT_SCALE) *
                    Game::Read<float>(object, Offsets::OFF_CGOBJECT_BOX_HEIGHT) *
                    Game::Read<float>(Offsets::VAR_NAME_HEIGHT_FACTOR) +
                Game::Read<float>(Offsets::VAR_NAMEPLATE_LIFT);
    return reinterpret_cast<ScreenCoords_t>(Offsets::FUN_WORLDFRAME_GET_SCREEN_COORDS)(
        worldFrame, world, screen);
}

void Show(Icon &icon, uint64_t guid, void *worldFrame) {
    void *object = guid != 0 ? Object::ByGuid(kTypeMask, guid) : nullptr;
    char path[Offsets::CURSOR_FILE_SIZE];
    float screen[2];
    if (object == nullptr ||
        !Cursor::Hover::ImagePath(object, Cursor::Hover::Mode(object),
                                  CVar::Factory::GetInt(g_lowPriority) != 0, path, sizeof path) ||
        !Project(guid, object, worldFrame, screen)) {
        SetShown(icon, false);
        return;
    }
    if (icon.tex == nullptr && (icon.tex = CreateTexture(worldFrame)) == nullptr)
        return;
    if (std::strcmp(path, icon.path) != 0) {
        reinterpret_cast<SetTexture_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE)(
            icon.tex, path, 0, Game::Read<uint32_t>(Offsets::VAR_TEXTURE_BLEND_DEFAULT), 0);
        std::memcpy(icon.path, path, sizeof icon.path);
    }
    const float size = Game::PixelToInternal(static_cast<float>(CVar::Factory::GetInt(g_size)));
    if (size != icon.shownSize) {
        reinterpret_cast<SetSize_t>(Offsets::FUN_LAYOUT_SET_WIDTH)(Anchor(icon.tex), size);
        reinterpret_cast<SetSize_t>(Offsets::FUN_LAYOUT_SET_HEIGHT)(Anchor(icon.tex), size);
        icon.shownSize = size;
    }
    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(
        Anchor(icon.tex), Offsets::FRAMEPOINT_BOTTOM, Anchor(worldFrame),
        Offsets::FRAMEPOINT_BOTTOMLEFT, screen[0], screen[1], 1);
    SetShown(icon, true);
}

uint64_t InteractGuid() {
    const uint64_t guid = Target::SoftInteract::Get();
    if (guid != 0 && CVar::Factory::GetInt(g_gameObject) == 0 &&
        Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT, guid) != nullptr)
        return 0;
    return guid;
}

void Update() {
    void *worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    if (worldFrame != g_parent) {
        g_parent = worldFrame;
        for (Icon &icon : g_icons)
            icon = {};
    }
    // The cvars first: reading a token is what runs its pick.
    uint64_t guids[kIcons] = {};
    if (worldFrame != nullptr) {
        if (CVar::Factory::GetInt(g_enemy) != 0)
            guids[0] = SoftUnit::Get(SoftUnit::Side::Enemy);
        if (CVar::Factory::GetInt(g_friend) != 0)
            guids[1] = SoftUnit::Get(SoftUnit::Side::Friend);
        if (CVar::Factory::GetInt(g_interact) != 0)
            guids[2] = InteractGuid();
    }
    // One icon per unit, in the plate's order: enemy, friend, interact.
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

void Register() {
    g_enemy = CVar::Factory::Register("SoftTargetIconEnemy", "Show icon for soft enemy target", Offsets::CVAR_CATEGORY_GAME, "0");
    g_friend = CVar::Factory::Register("SoftTargetIconFriend", "Show icon for soft friend target", Offsets::CVAR_CATEGORY_GAME, "0");
    g_interact = CVar::Factory::Register("SoftTargetIconInteract", "Show icon for soft interact target", Offsets::CVAR_CATEGORY_GAME, "0");
    g_gameObject = CVar::Factory::Register("SoftTargetIconGameObject", "Show icon for sot interact game objects (interactable objects you cannot normally target)", Offsets::CVAR_CATEGORY_GAME, "0");
    g_lowPriority = CVar::Factory::Register("SoftTargetLowPriorityIcons", "Show interact icons even when there is other visual indicators, such as quest or loot effects", Offsets::CVAR_CATEGORY_GAME, "0");
    g_size = CVar::Factory::Register("SoftTargetNameplateSize", "Size of soft target icon on nameplate (0 to disable)", Offsets::CVAR_CATEGORY_GAME, "19");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Target::SoftTargetIcon
