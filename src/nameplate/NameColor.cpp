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

// nameplateUseClassColorForFriendlyPlayerUnitNames: a friendly player's
// plate name in its class colour.
//
// The 3.80.2 client defines the cvar in C (FUN_140196570, default 0) and does
// the rest in Blizzard_NamePlates: UpdateNameClassColor sets
// colorNameWithClassColor when the unit is a player, UnitIsFriend("player",
// unit) and the cvar is on, and CompactUnitFrame_UpdateName then picks the
// name colour: the mouseover colour, else grey for a tapped or dead npc, else
// RAID_CLASS_COLORS[class] (white when the class has none), else white.
//
// 1.12 draws the plate in C (benilla-re ui/nameplates "Name, flash and dim")
// and picks the name colour at its own points: white at the bind, yellow on
// enter, red for 5 s when the unit is hit (Flash), and on leave or when the
// flash ends white, or yellow while still hovered. Retail's order maps onto
// that as is: the hover yellow is the mouseover colour, the hit flash stays
// above everything, and the class colour replaces the resting white. So the
// three calls that give the resting colour (CALL_NAMEPLATE_*_NAME_COLOR) are
// redirected, and a white they ask for becomes the class colour when retail's
// test passes. The test runs at each of those points, so a unit that turns
// hostile (a duel) goes back to white at the next one. Retail also re-tests
// on UNIT_FACTION; 1.12 recolours a bound plate at none of its own points
// (the bar keeps its bind-time colour too), so neither does this.
//
// The bind runs in the world tick, inside the strata walk, where no Lua may
// run; the colours come from Classes::ColorCache, which follows writes to
// RAID_CLASS_COLORS. A change of the cvar recolours every resting plate, as
// retail's cvar callback runs UpdateNameClassColor on each plate.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "classes/ColorCache.h"
#include "cvar/Factory.h"
#include "frame/Region.h"
#include "nameplate/Walk.h"
#include "object/Resolve.h"
#include "unit/Flags.h"
#include "unit/Identity.h"
#include "unit/Relation.h"

#include <cstdint>
#include <cstdlib>

namespace NamePlate::NameColor {

namespace {

constexpr uint32_t kWhite = 0xFFFFFFFF;  // the plate's resting name colour
constexpr uint32_t kYellow = 0xFFFFFF00; // its hovered name colour (OnEnter, OnUpdate)
// Script_UnitIsFriend (0x005169D7): reaction 4 or above is friendly.
constexpr int kReactionFriendly = 4;

CVar::Factory::Handle g_cvar = nullptr;

// The resting name colour of `plate`: its unit's class colour when `on` and
// the unit is a friendly player, else white.
uint32_t RestingColor(const uint8_t *plate, bool on) {
    if (!on)
        return kWhite;
    const auto *unit = static_cast<const uint8_t *>(Object::ByGuid(
        Offsets::TYPEMASK_UNIT, Game::Read<uint64_t>(plate, Offsets::OFF_NAMEPLATE_GUID)));
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (unit == nullptr || player == nullptr || !Unit::Flags::IsPlayerObject(unit) ||
        Unit::Relation::Reaction(player, unit) < kReactionFriendly)
        return kWhite;
    uint32_t color = kWhite;
    return Classes::ColorCache::Get(Unit::Identity::Class(Object::Fields(unit)), color) ? color
                                                                                       : kWhite;
}

// Stands in for FUN_REGION_SET_COLOR(nameFontString, color) at the three
// resting-colour calls, with the plate from their ESI.
void __fastcall SetNameColor(void *name, const uint8_t *plate, const uint32_t *color) {
    const uint32_t use =
        *color == kWhite ? RestingColor(plate, CVar::Factory::GetInt(g_cvar) != 0) : *color;
    Frame::Region::SetColor(name, use);
}

// The calls are `__thiscall(region, const uint32_t *color)`, ret 4: the same
// stack as SetNameColor's `__fastcall`, with the plate loaded into EDX (free
// across the call in all three).
__declspec(naked) void SetNameColorThunk() {
    __asm {
        mov edx, esi
        jmp SetNameColor
    }
}

// The colour OnUpdate gives a plate whose flash has ended, for every plate
// not flashing.
void Recolor(bool on) {
    NamePlate::Walk::ForEachNamePlatedUnit([on](const uint8_t *, const uint8_t *plate, uint64_t) {
        if (Game::Read<int>(plate, Offsets::OFF_NAMEPLATE_FLASH_MS) > 0)
            return;
        void *name = Game::Read<void *>(plate, Offsets::OFF_NAMEPLATE_NAME);
        if (name == nullptr)
            return;
        const auto *glow = Game::Read<const uint8_t *>(plate, Offsets::OFF_NAMEPLATE_GLOW);
        const bool hovered =
            glow != nullptr && Game::Read<int>(glow, Offsets::OFF_REGION_ACTUALLY_SHOWN) != 0;
        Frame::Region::SetColor(name, hovered ? kYellow : RestingColor(plate, on));
    });
}

// Registration passes a new cvar's default with no previous value, and an
// existing one's value as both; neither is a change.
int __fastcall OnChanged(CVar::Factory::Handle, const char *prevValue, const char *newValue,
                         void *) {
    if (prevValue == nullptr || *prevValue == '\0' || newValue == nullptr)
        return 1;
    const bool on = std::atoi(newValue) != 0;
    if (on != (std::atoi(prevValue) != 0))
        Recolor(on);
    return 1;
}

void Register() {
    g_cvar = CVar::Factory::Register(
        "nameplateUseClassColorForFriendlyPlayerUnitNames",
        "Used to display the class color in friendly player unit nameplate names.",
        Offsets::CVAR_CATEGORY_GAME, "0", &OnChanged);

    const auto *thunk = reinterpret_cast<const void *>(&SetNameColorThunk);
    Common::RedirectCall(Offsets::CALL_NAMEPLATE_BIND_NAME_COLOR, Offsets::FUN_REGION_SET_COLOR,
                         thunk);
    Common::RedirectCall(Offsets::CALL_NAMEPLATE_LEAVE_NAME_COLOR, Offsets::FUN_REGION_SET_COLOR,
                         thunk);
    Common::RedirectCall(Offsets::CALL_NAMEPLATE_UPDATE_NAME_COLOR, Offsets::FUN_REGION_SET_COLOR,
                         thunk);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace NamePlate::NameColor
