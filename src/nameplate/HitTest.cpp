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

// `C_NamePlateManager.SetNamePlateHitTestInsets(type, left, right, top,
// bottom)` / `GetNamePlateHitTestInsets(type)` — how far in from each edge a
// nameplate stops taking clicks, as the 3.80.2 client keeps it:
//   - two entries, by Enum.NamePlateType: 0 for a friendly plate, 1 for an
//     enemy one; any other type reads and writes entry 0
//     (FUN_140d4c210 / FUN_140d4c480);
//   - the insets are UI units times the plate's scale. A cursor counts on a
//     plate only inside the plate's rect AND its hit rect with each edge
//     moved in by its inset (FUN_14194efc0), so a positive inset shrinks the
//     clickable area and a negative one cannot grow it past the plate;
//   - the plate's type is taken when the cursor is tested: 1 for a unit the
//     player can attack, or with no player (FUN_1416273d0);
//   - both entries are 0 again when the UI shuts down (FUN_141692fd0), so a
//     /reload clears them. 3.80.2 blocks Set from tainted code; 1.12 has no
//     taint.
//
// 1.12's plate hit test (FUN_NAMEPLATE_HIT_TEST) is co-hooked: when it says
// yes, the cursor must also lie inside the hit rect (OFF_FRAME_HIT_RECT,
// layout units) moved in by the insets, converted the way Script SetPoint
// converts its offsets (Frame::Layout::FromLocalUnits). The side is the plate
// gate's own (NamePlate::Side).

#include "Game.h"
#include "Offsets.h"
#include "frame/Layout.h"
#include "nameplate/Side.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>

namespace NamePlate::HitTest {

namespace {

struct Insets {
    float left, right, top, bottom;
};

// By type: 0 friendly, 1 enemy.
Insets g_insets[2] = {};

int TypeIndex(int type) { return type == 1 ? 1 : 0; }

bool AnySet() {
    for (const Insets &i : g_insets)
        if (i.left != 0.0f || i.right != 0.0f || i.top != 0.0f || i.bottom != 0.0f)
            return true;
    return false;
}

// The plate's type: 0 for a unit the player cannot attack, else 1.
int PlateType(const uint8_t *plate) {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 1;
    const void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT,
                                      Game::Read<uint64_t>(plate, Offsets::OFF_NAMEPLATE_GUID));
    if (unit == nullptr)
        return 1;
    return NamePlate::Side::IsFriendly(player, unit) ? 0 : 1;
}

// Whether the cursor in `event` lies inside the plate's hit rect, each edge
// moved in by the plate type's insets.
bool InsideInsets(const uint8_t *plate, const uint8_t *event) {
    const Insets &in = g_insets[PlateType(plate)];
    if (in.left == 0.0f && in.right == 0.0f && in.top == 0.0f && in.bottom == 0.0f)
        return true;
    const Frame::Layout::Rect &rect = Frame::Layout::HitRectOf(plate);
    const float x = Game::Read<float>(event, Offsets::OFF_UI_MOUSE_EVENT_X);
    const float y = Game::Read<float>(event, Offsets::OFF_UI_MOUSE_EVENT_Y);
    return x >= rect.left + Frame::Layout::FromLocalUnits(plate, in.left) &&
           x <= rect.right - Frame::Layout::FromLocalUnits(plate, in.right) &&
           y >= rect.bottom + Frame::Layout::FromLocalUnits(plate, in.bottom) &&
           y <= rect.top - Frame::Layout::FromLocalUnits(plate, in.top);
}

using HitTest_t = int(__fastcall *)(void *plate, void *edx, void *event);
HitTest_t g_original = nullptr;

int __fastcall HitTest_h(void *plate, void *edx, void *event) {
    const int hit = g_original(plate, edx, event);
    if (hit == 0 || !AnySet() || event == nullptr)
        return hit;
    return InsideInsets(static_cast<const uint8_t *>(plate), static_cast<const uint8_t *>(event))
               ? hit
               : 0;
}

const Game::HookAutoRegister _hook{Offsets::FUN_NAMEPLATE_HIT_TEST,
                                   reinterpret_cast<void *>(&HitTest_h),
                                   reinterpret_cast<void **>(&g_original)};

int __fastcall Script_GetNamePlateHitTestInsets(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: local left, right, top, bottom = "
                            "C_NamePlateManager.GetNamePlateHitTestInsets(type)");
        return 0;
    }
    const Insets &in = g_insets[TypeIndex(static_cast<int>(Game::Lua::ToNumber(L, 1)))];
    Game::Lua::PushNumber(L, in.left);
    Game::Lua::PushNumber(L, in.right);
    Game::Lua::PushNumber(L, in.top);
    Game::Lua::PushNumber(L, in.bottom);
    return 4;
}

int __fastcall Script_SetNamePlateHitTestInsets(void *L) {
    for (int i = 1; i <= 5; ++i) {
        if (!Game::Lua::IsNumber(L, i)) {
            Game::Lua::Error(L, "Usage: C_NamePlateManager.SetNamePlateHitTestInsets(type, left, "
                                "right, top, bottom)");
            return 0;
        }
    }
    Insets &in = g_insets[TypeIndex(static_cast<int>(Game::Lua::ToNumber(L, 1)))];
    in.left = static_cast<float>(Game::Lua::ToNumber(L, 2));
    in.right = static_cast<float>(Game::Lua::ToNumber(L, 3));
    in.top = static_cast<float>(Game::Lua::ToNumber(L, 4));
    in.bottom = static_cast<float>(Game::Lua::ToNumber(L, 5));
    return 0;
}

const Game::Doc::Field kTypeArg[] = {
    Game::Doc::Req("type", "NamePlateType", "0 for friendly plates, 1 for enemy plates."),
};
const Game::Doc::Field kInsetFields[] = {
    Game::Doc::Req("left", "uiUnit"),
    Game::Doc::Req("right", "uiUnit"),
    Game::Doc::Req("top", "uiUnit"),
    Game::Doc::Req("bottom", "uiUnit"),
};
const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("type", "NamePlateType", "0 for friendly plates, 1 for enemy plates."),
    Game::Doc::Req("left", "uiUnit"),
    Game::Doc::Req("right", "uiUnit"),
    Game::Doc::Req("top", "uiUnit"),
    Game::Doc::Req("bottom", "uiUnit"),
};
const Game::Doc::Function kGetInsets{
    "Returns how far in from each edge the nameplates of a type stop taking clicks.", kTypeArg,
    kInsetFields};
const Game::Doc::Function kSetInsets{
    "Sets how far in from each edge the nameplates of a type stop taking clicks. A positive "
    "inset makes the clickable area smaller.",
    kSetArgs, {}};

void Register() {
    Game::Lua::RegisterTableFunction("C_NamePlateManager", "GetNamePlateHitTestInsets",
                                     &Script_GetNamePlateHitTestInsets, &kGetInsets);
    Game::Lua::RegisterTableFunction("C_NamePlateManager", "SetNamePlateHitTestInsets",
                                     &Script_SetNamePlateHitTestInsets, &kSetInsets);
}

void ClearOnReload() {
    for (Insets &in : g_insets)
        in = {};
}

const Game::ModuleAutoRegister _autoreg{&Register};
const Game::ReloadAutoRegister _reload{&ClearOnReload};

} // namespace

} // namespace NamePlate::HitTest
