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

// A real "met" flag for a recipe's spell focus in GetTradeSkillTools(index)
// and GetCraftSpellFocus(index), as retail's
// C_TradeSkillUI.GetRecipeRequirements gives for a SpellFocus requirement.
//
// Both engine functions push (name, flag) pairs, the spell focus first. The
// totem flags come from an inventory search, but the focus flag is a
// constant 1 (FUN_SCRIPT_GET_TRADE_SKILL_TOOLS), so "Requires: Cooking Fire"
// never turns red, although the stock UI's BuildColoredListString colours
// any pair with a nil flag. Every client up to 4.3.4 pushes the same
// constant.
//
// The hooks run the original, then replace the focus flag with nil when no
// matching focus object is in range. They mirror the original's own test
// for whether it pushed a focus pair: a row in range, a Spell.dbc record, a
// nonzero RequiresSpellFocus and a SpellFocusObject.dbc row. So the flag is
// always at the first pair and the stack is otherwise untouched.
//
// "In range" is the server's rule, since the server decides the cast
// (tortoise Spell::CheckItems -> GameObjectFocusCheck, GridNotifiers.h): a
// visible game object of type spellFocus whose spellFocusType matches, at a
// 3D distance below its radius plus the caster's bounding radius plus the
// server's fixed game-object size. The client reads the type, the focus id
// and the radius through the engine's own typed-data accessors
// (FUN_GAMEOBJECT_FIND_TYPE_COLUMN / FUN_GAMEOBJECT_GET_DATA_COLUMN). A
// focus object the server never sends (serverOnly) is invisible to the
// client and reads as not in range; profession focuses (fires, anvils,
// forges, moonwells) are all sent.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cstdint>

namespace TradeSkill::Tools {

namespace {

// GAMEOBJECT_TYPE_SPELL_FOCUS, and its two property ids. 1.12's column list
// for type 8 (0x0080D840: count 4 at +4, ids at +8) is {14, 24, 30, 37},
// 3.3.5's spellFocusType, radius, linkedTrap, serverOnly: the server's
// data0 focusId and data1 dist.
constexpr uint32_t kTypeSpellFocus = 8;
constexpr uint32_t kPropSpellFocusType = 14;
constexpr uint32_t kPropRadius = 24;

// DEFAULT_WORLD_OBJECT_SIZE, the server's bounding radius for every game
// object (tortoise GameObject::GetObjectBoundingRadius: 1.12's
// GameObjectDisplayInfo.dbc has no size).
constexpr float kGameObjectBoundingRadius = 0.389f;

using Script_t = int(__fastcall *)(void *L);

// Data column `property` of game object `go` of type `type`, the engine's
// way: column -1 reads as 0.
uint32_t DataProperty(void *go, uint32_t type, uint32_t property) {
    using FindColumn_t = int(__fastcall *)(uint32_t type, uint32_t property);
    using GetColumn_t = uint32_t(__thiscall *)(void *go, int column);
    const int column = reinterpret_cast<FindColumn_t>(
        static_cast<uintptr_t>(Offsets::FUN_GAMEOBJECT_FIND_TYPE_COLUMN))(type, property);
    return reinterpret_cast<GetColumn_t>(
        static_cast<uintptr_t>(Offsets::FUN_GAMEOBJECT_GET_DATA_COLUMN))(go, column);
}

// Whether a visible spell-focus object of `focusID` is in casting range of
// the player.
bool FocusInRange(uint32_t focusID) {
    void *player = Object::ByGuid(Offsets::TYPEMASK_PLAYER, Unit::Identity::PlayerGuid());
    const uint8_t *playerFields = Object::Fields(player);
    float playerPos[3];
    if (playerFields == nullptr || !Unit::Position::Read(player, playerPos))
        return false;
    const float playerRadius =
        Game::Read<float>(playerFields, Offsets::OFF_UNIT_FIELD_BOUNDING_RADIUS);

    bool found = false;
    Object::ForEachVisible([&](uint64_t guid) {
        if (Guid::Classify(guid) != Guid::Type::GameObject)
            return true;
        void *go = Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT, guid, nullptr, 0);
        const uint8_t *fields = Object::Fields(go);
        if (fields == nullptr)
            return true;
        const uint32_t type = Game::Read<uint32_t>(fields, Offsets::OFF_GAMEOBJECT_FIELD_TYPE_ID);
        if (type != kTypeSpellFocus || DataProperty(go, type, kPropSpellFocusType) != focusID)
            return true;
        float pos[3];
        if (!Unit::Position::Read(go, pos))
            return true;
        const float reach = static_cast<float>(DataProperty(go, type, kPropRadius)) +
                            playerRadius + kGameObjectBoundingRadius;
        if (Game::DistanceSq(pos, playerPos) < reach * reach) {
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

// After the original pushed `ret` values for row arg 1 of the list at
// `entriesVar`/`countVar`: nils the spell-focus flag when the focus is not
// in range. The original truncates the index, subtracts 1 and compares
// unsigned against the count; compared as a double first, since casting
// NaN or an out-of-range double to an integer is undefined in C++.
void CorrectFocusFlag(void *L, int ret, uintptr_t entriesVar, uintptr_t countVar) {
    if (ret < 2)
        return;
    const double n = Game::Lua::ToNumber(L, 1);
    const uint32_t count = Game::Read<uint32_t>(countVar);
    if (!(n >= 1.0 && n < static_cast<double>(count) + 1.0))
        return;
    const uint8_t *spell = Spell::Lookup::RecordForID(
        Spell::Lookup::RecipeSlotSpellID(entriesVar, countVar, static_cast<int>(n) - 1));
    if (spell == nullptr)
        return;
    const uint32_t focusID =
        Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_REQUIRES_SPELL_FOCUS);
    if (focusID == 0 ||
        DBC::Record(Offsets::VAR_SPELL_FOCUS_OBJECT_RECORDS, Offsets::VAR_SPELL_FOCUS_OBJECT_COUNT,
                    focusID) == nullptr)
        return;
    if (FocusInRange(focusID))
        return;
    Game::Lua::PushNil(L);
    Game::Lua::Replace(L, Game::Lua::GetTop(L) - ret + 1);
}

Script_t g_origTradeSkillTools = nullptr;
Script_t g_origCraftSpellFocus = nullptr;

int __fastcall GetTradeSkillTools_h(void *L) {
    const int ret = g_origTradeSkillTools(L);
    CorrectFocusFlag(L, ret, Offsets::VAR_TRADESKILL_ENTRIES, Offsets::VAR_TRADESKILL_COUNT);
    return ret;
}

int __fastcall GetCraftSpellFocus_h(void *L) {
    const int ret = g_origCraftSpellFocus(L);
    CorrectFocusFlag(L, ret, Offsets::VAR_CRAFT_ENTRIES, Offsets::VAR_CRAFT_COUNT);
    return ret;
}

const Game::HookAutoRegister _tradeSkillToolsHook{
    Offsets::FUN_SCRIPT_GET_TRADE_SKILL_TOOLS, reinterpret_cast<void *>(&GetTradeSkillTools_h),
    reinterpret_cast<void **>(&g_origTradeSkillTools)};
const Game::HookAutoRegister _craftSpellFocusHook{
    Offsets::FUN_SCRIPT_GET_CRAFT_SPELL_FOCUS, reinterpret_cast<void *>(&GetCraftSpellFocus_h),
    reinterpret_cast<void **>(&g_origCraftSpellFocus)};

} // namespace

} // namespace TradeSkill::Tools
