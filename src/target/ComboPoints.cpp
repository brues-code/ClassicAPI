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

// Combo points as the 3.80.2 client exposes them, over 1.12's store on the
// player: the combo-point byte (byte 1 of PLAYER_FIELD_BYTES) and the combo
// target's GUID (PLAYER_FIELD_COMBO_TARGET), both in the `+0xE68` player
// section.
//
// - The count is power type 4 (UnitPower / UnitPowerMax read Count()). A byte
//   change fires UNIT_POWER_UPDATE and UNIT_POWER_FREQUENT ("player",
//   "COMBO_POINTS") beside 1.12's own PLAYER_COMBO_POINTS, from an observer on
//   the same byte the engine's (0x005DDFF0) watches.
// - A change of the combo target fires COMBO_TARGET_CHANGED (no payload), as
//   3.80.2's field handler (0x14148ADF0) does. The server (vmangos and
//   tortoise HandleSetSelectionOpcode) zeroes a rogue's or druid's points
//   when the selection moves to another unit, but ClearComboPoints leaves
//   PLAYER_FIELD_COMBO_TARGET on the old unit, so the field — and this event —
//   changes only when the first point lands on a new unit.
// - GetComboPoints(unit, target) as 3.80.2 reads it (0x141B7DBB0) when it is
//   given a target. Without one it stays 1.12's own (the stock ComboFrame calls
//   it that way): the points when the combo target is the selection, 0 over a
//   hidden (SoftTargetForce) target. 1.12's Script_GetComboPoints (0x0051A190)
//   reports the points whenever the combo target is the raw selection
//   (0x00B4E2D8); 3.80.2 resolves the `target` token, which is empty for a
//   Force target, and answers 0. Without that, the ComboFrame (parented to
//   UIParent, not TargetFrame) and combat text showed points for a target the
//   UI does not show, and kept them across a silent Force swap. The points
//   come back on the PLAYER_TARGET_CHANGED that a promotion fires.

#include "target/ComboPoints.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "player/ActiveWatchers.h"
#include "target/Reason.h"
#include "unit/Identity.h"
#include "unit/PowerEvents.h"

namespace Target::ComboPoints {

namespace {

constexpr uint8_t kClassRogue = 4;
constexpr uint8_t kClassDruid = 11;

// The active player's `+0xE68` section, or null outside the world.
const uint8_t *PlayerSection(const void **playerOut = nullptr) {
    const void *player = Game::ResolveUnitToken("player");
    if (player == nullptr)
        return nullptr;
    if (playerOut != nullptr)
        *playerOut = player;
    return Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
}

// ---- events -----------------------------------------------------------------

const Game::Doc::Event kTargetChangedDoc{
    "Unit", "Fires when the player's combo points move to another unit.", {}};
const Event::Custom::AutoReserve kTargetChanged{"COMBO_TARGET_CHANGED", &kTargetChangedDoc};

int __fastcall CountChanged(uint32_t, uint32_t, uint32_t guidLo, uint32_t guidHi, const void *,
                            void *) {
    Unit::PowerEvents::SignalComboPoints((static_cast<uint64_t>(guidHi) << 32) | guidLo);
    return 1;
}

int __fastcall TargetChanged(uint32_t, uint32_t, uint32_t, uint32_t, const void *, void *) {
    Event::Custom::Fire(kTargetChanged.Slot(), "");
    return 1;
}

const Player::ActiveWatchers::AutoWatch _countWatch{
    {Offsets::OFF_CGPLAYER_COMBO_POINTS, 1, &CountChanged}};
const Player::ActiveWatchers::AutoWatch _targetWatch{
    {Offsets::OFF_CGPLAYER_COMBO_TARGET, 8, &TargetChanged}};

// ---- GetComboPoints -----------------------------------------------------------

Game::Lua::CFunction g_original = nullptr;

// 3.80.2's two-argument form: "player" answers when the combo target is the
// unit `target` names; any other unit answers 0 (1.12 pets have no combo
// points).
int ForUnits(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        // The binary's string, closing parenthesis missing.
        Game::Lua::Error(L, "Usage: GetComboPoints(\"unit\"[, \"target\"]");
        return 0;
    }
    uint32_t points = 0;
    const uint64_t unit = Unit::Identity::GuidForToken(Game::Lua::ToString(L, 1));
    const void *player = nullptr;
    const uint8_t *section = PlayerSection(&player);
    if (section != nullptr && unit != 0 && unit == ::Object::Guid(player) &&
        ClassUses(::Object::Fields(player))) {
        const uint64_t target = Game::Lua::IsString(L, 2)
                                    ? Unit::Identity::GuidForToken(Game::Lua::ToString(L, 2))
                                    : 0;
        if (target != 0 && ::Object::ByGuid(Offsets::TYPEMASK_UNIT, target) != nullptr &&
            Game::Read<uint64_t>(section, Offsets::OFF_CGPLAYER_COMBO_TARGET) == target)
            points = Game::Read<uint8_t>(section, Offsets::OFF_CGPLAYER_COMBO_POINTS);
    }
    Game::Lua::PushNumber(L, static_cast<double>(points));
    return 1;
}

// Only a call with a target takes 3.80.2's form: vanilla addons (pfUI
// among them) call GetComboPoints("target"), whose argument 1.12 ignores, and
// 3.80.2 would read that as the unit and answer 0.
int __fastcall GetComboPoints_h(void *L) {
    if (Game::Lua::Type(L, 2) > Game::Lua::TYPE_NIL)
        return ForUnits(L);
    if (!Reason::Hidden())
        return g_original(L);
    Game::Lua::PushNumber(L, 0.0);
    return 1;
}

const Game::HookAutoRegister _hook{Offsets::FUN_SCRIPT_GET_COMBO_POINTS,
                                   reinterpret_cast<void *>(&GetComboPoints_h),
                                   reinterpret_cast<void **>(&g_original)};

} // namespace

bool ClassUses(const uint8_t *playerFields) {
    if (playerFields == nullptr)
        return false;
    const uint8_t cls = playerFields[Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE];
    return cls == kClassRogue || cls == kClassDruid;
}

uint32_t Count() {
    const uint8_t *section = PlayerSection();
    return section != nullptr ? Game::Read<uint8_t>(section, Offsets::OFF_CGPLAYER_COMBO_POINTS)
                              : 0;
}

} // namespace Target::ComboPoints
