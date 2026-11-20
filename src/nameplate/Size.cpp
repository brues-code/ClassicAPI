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

// `C_NamePlate.SetNamePlateSize(width, height)` / `GetNamePlateSize()` — the
// size of every nameplate, as the 3.80.2 client keeps it: Set stores the two
// numbers as given, with no clamp (FUN_140d4b420); every plate takes them
// when it is acquired (FUN_1419b51b0); a change resizes the live plates at
// once; Get returns the stored pair (FUN_140d4b330). Blizzard_NamePlates
// passes the plate's width and height in UI units, what Frame:SetSize takes.
// 3.80.2 blocks the call from tainted code; 1.12 has no taint.
//
// 1.12's bind (FUN_NAMEPLATE_BIND) sizes the plate from two constants, 0.1
// by 0.025 layout units, and sizes and places the health bar and the level
// text from the same two, so they scale with the plate. Its eight reads of
// them are pointed at two floats of ours, which hold the constants until a
// Set. A Set converts the UI units to layout units the way Lua's SetWidth
// does (Game::PixelToInternal) and binds each live plate again to its unit.
// The conversion follows the screen's aspect, so a new display size
// (DISPLAY_SIZE_CHANGED) converts and binds again.
//
// Before any Set, Get returns 1.12's size in UI units. A Set lasts until the
// client closes, /reload included, as in 3.80.2.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/SignalHook.h"
#include "nameplate/Walk.h"

#include <cstdint>
#include <initializer_list>

namespace NamePlate::Size {

namespace {

// The size the bind reads, in layout units. Initialized from the engine's
// constants at registration.
float g_width = 0.0f;
float g_height = 0.0f;

// The size a script set, in UI units.
bool g_set = false;
float g_uiWidth = 0.0f;
float g_uiHeight = 0.0f;

float EngineWidth() { return Game::Read<float>(Offsets::VAR_NAMEPLATE_WIDTH); }
float EngineHeight() { return Game::Read<float>(Offsets::VAR_NAMEPLATE_HEIGHT); }

// Binds every live plate to its unit again, so it takes the current size.
void Rebind() {
    using Bind_t = void(__thiscall *)(void *plate, void *unit);
    const auto bind = reinterpret_cast<Bind_t>(Offsets::FUN_NAMEPLATE_BIND);
    NamePlate::Walk::ForEachNamePlatedUnit([&](const uint8_t *unit, const uint8_t *plate, uint64_t) {
        bind(const_cast<uint8_t *>(plate), const_cast<uint8_t *>(unit));
    });
}

// The set size in layout units, for the current screen. Not before the
// screen's scale is known: the conversion reads 0 then.
void Convert() {
    if (!g_set || Game::PixelToInternal(1.0f) == 0.0f)
        return;
    g_width = Game::PixelToInternal(g_uiWidth);
    g_height = Game::PixelToInternal(g_uiHeight);
}

void OnDisplaySizeChanged() {
    if (!g_set)
        return;
    Convert();
    Rebind();
}

int __fastcall Script_GetNamePlateSize(void *L) {
    if (g_set) {
        Game::Lua::PushNumber(L, g_uiWidth);
        Game::Lua::PushNumber(L, g_uiHeight);
    } else {
        Game::Lua::PushNumber(L, Game::InternalToPixel(EngineWidth()));
        Game::Lua::PushNumber(L, Game::InternalToPixel(EngineHeight()));
    }
    return 2;
}

int __fastcall Script_SetNamePlateSize(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: C_NamePlate.SetNamePlateSize(width, height)");
        return 0;
    }
    const auto width = static_cast<float>(Game::Lua::ToNumber(L, 1));
    const auto height = static_cast<float>(Game::Lua::ToNumber(L, 2));
    if (g_set && width == g_uiWidth && height == g_uiHeight)
        return 0;
    g_set = true;
    g_uiWidth = width;
    g_uiHeight = height;
    Convert();
    Rebind();
    return 0;
}

const Game::Doc::Field kSizeFields[] = {
    Game::Doc::Req("width", "uiUnit"),
    Game::Doc::Req("height", "uiUnit"),
};
const Game::Doc::Function kGetNamePlateSize{"Returns the size of every nameplate.", {},
                                            kSizeFields};
const Game::Doc::Function kSetNamePlateSize{
    "Sets the size of every nameplate, which is also the area that takes clicks. The health "
    "bar and the level text scale with it.",
    kSizeFields, {}};

void Register() {
    if (g_width == 0.0f && g_height == 0.0f) {
        g_width = EngineWidth();
        g_height = EngineHeight();
    }
    for (uintptr_t site : {Offsets::SITE_NAMEPLATE_BIND_WIDTH_OPERAND_1,
                           Offsets::SITE_NAMEPLATE_BIND_WIDTH_OPERAND_2,
                           Offsets::SITE_NAMEPLATE_BIND_WIDTH_OPERAND_3,
                           Offsets::SITE_NAMEPLATE_BIND_WIDTH_OPERAND_4})
        Common::RedirectSlot(site, Offsets::VAR_NAMEPLATE_WIDTH, &g_width);
    for (uintptr_t site : {Offsets::SITE_NAMEPLATE_BIND_HEIGHT_OPERAND_1,
                           Offsets::SITE_NAMEPLATE_BIND_HEIGHT_OPERAND_2,
                           Offsets::SITE_NAMEPLATE_BIND_HEIGHT_OPERAND_3,
                           Offsets::SITE_NAMEPLATE_BIND_HEIGHT_OPERAND_4})
        Common::RedirectSlot(site, Offsets::VAR_NAMEPLATE_HEIGHT, &g_height);

    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateSize", &Script_GetNamePlateSize,
                                     &kGetNamePlateSize);
    Game::Lua::RegisterTableFunction("C_NamePlate", "SetNamePlateSize", &Script_SetNamePlateSize,
                                     &kSetNamePlateSize);
}

const Game::ModuleAutoRegister _autoreg{&Register};
const Event::SignalHook::AutoObserve _display{Offsets::EVENT_DISPLAY_SIZE_CHANGED,
                                              &OnDisplaySizeChanged};

} // namespace

} // namespace NamePlate::Size
