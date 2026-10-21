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

// The soft-interact icon (`SoftTargetIconInteract`; retail defaults it to 1,
// here it is 0 so the icon is opted into, and the pick does not run every
// frame for players who never look at it): the
// cursor image of the `softinteract` object, drawn over it — a unit's above
// its name, an object's above its top (retail screenshots: the speak bubble
// over an NPC's name, the mail icon over a mailbox, grey then coloured as the
// object comes into reach).
//
// The engine pieces:
//   - the image is the hover cursor's (Cursor::Hover), Unable form included;
//   - the point is where 1.12 seats a nameplate: a unit's name position
//     raised 2/3 yard, projected (GetNamePlateScreenPos). An object with no
//     name position gets GetNamePosition's own no-attachment rule (1.25 x
//     scale x box height above its base) and the same lift, projected the
//     same way;
//   - the icon is a texture on the world frame anchored to the world frame's
//     BOTTOMLEFT at that point, the anchor SeatNamePlate (0x509EC0) gives a
//     plate (without its overlap solving — there is one icon);
//   - its size is SoftTargetNameplateSize (retail default 19), the size of
//     the icon the nameplate shows.
// Off screen (the projection fails) the icon hides, as a plate is dropped.

#include "Game.h"
#include "Offsets.h"
#include "cursor/Hover.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/SoftInteract.h"
#include "tick/FrameTick.h"
#include "unit/Position.h"

#include <cstdint>
#include <cstring>

namespace Target::SoftInteractIcon {

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

CVar::Factory::Handle g_enable = nullptr;
CVar::Factory::Handle g_size = nullptr;

void *g_parent = nullptr; // the world frame g_tex belongs to
void *g_tex = nullptr;
char g_path[Offsets::CURSOR_FILE_SIZE] = {};
float g_shownSize = 0.0f;
bool g_shown = false;

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

void SetShown(bool shown) {
    if (g_tex == nullptr || shown == g_shown)
        return;
    g_shown = shown;
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(g_tex) +
                                  Offsets::OFF_REGION_DESIRED_SHOWN) = shown ? 1 : 0;
    reinterpret_cast<ShowHide_t>(shown ? Offsets::FUN_FONTSTRING_SHOW
                                       : Offsets::FUN_FONTSTRING_HIDE)(g_tex);
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

void Update() {
    void *worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    if (worldFrame != g_parent) {
        g_parent = worldFrame;
        g_tex = nullptr;
        g_path[0] = '\0';
        g_shownSize = 0.0f;
        g_shown = false;
    }
    // The cvar first: reading the token is what runs the pick.
    const bool enabled = worldFrame != nullptr && CVar::Factory::GetInt(g_enable) != 0;
    const uint64_t guid = enabled ? Target::SoftInteract::Get() : 0;
    void *object = guid != 0 ? Object::ByGuid(kTypeMask, guid) : nullptr;
    char path[Offsets::CURSOR_FILE_SIZE];
    float screen[2];
    if (object == nullptr ||
        !Cursor::Hover::ImagePath(object, Cursor::Hover::Mode(object), path, sizeof path) ||
        !Project(guid, object, worldFrame, screen)) {
        SetShown(false);
        return;
    }
    if (g_tex == nullptr && (g_tex = CreateTexture(worldFrame)) == nullptr)
        return;
    if (std::strcmp(path, g_path) != 0) {
        reinterpret_cast<SetTexture_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE)(
            g_tex, path, 0, Game::Read<uint32_t>(Offsets::VAR_TEXTURE_BLEND_DEFAULT), 0);
        std::memcpy(g_path, path, sizeof g_path);
    }
    const float size = Game::PixelToInternal(static_cast<float>(CVar::Factory::GetInt(g_size)));
    if (size != g_shownSize) {
        reinterpret_cast<SetSize_t>(Offsets::FUN_LAYOUT_SET_WIDTH)(Anchor(g_tex), size);
        reinterpret_cast<SetSize_t>(Offsets::FUN_LAYOUT_SET_HEIGHT)(Anchor(g_tex), size);
        g_shownSize = size;
    }
    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(
        Anchor(g_tex), Offsets::FRAMEPOINT_BOTTOM, Anchor(worldFrame),
        Offsets::FRAMEPOINT_BOTTOMLEFT, screen[0], screen[1], 1);
    SetShown(true);
}

const Tick::FrameTick::AutoSubscribe _tick{&Update};

// Before /reload tears the interface down: hide the icon while its world
// frame is still alive, and let the next one get a texture of its own.
void PrepareForReload() {
    if (g_parent == Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR))
        SetShown(false);
    g_parent = nullptr;
    g_tex = nullptr;
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

void Register() {
    g_enable = CVar::Factory::Register("SoftTargetIconInteract", "0");
    g_size = CVar::Factory::Register("SoftTargetNameplateSize", "19");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Target::SoftInteractIcon
