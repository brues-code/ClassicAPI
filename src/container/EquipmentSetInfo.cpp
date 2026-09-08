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

// `C_Container.GetContainerItemEquipmentSetInfo(containerIndex, slotIndex)`
// — whether the item in a bag slot belongs to an equipment set, and the
// names of the sets it belongs to:
//
//   inSet, setList = C_Container.GetContainerItemEquipmentSetInfo(bag, slot)
//
// The lookup is by the item's per-instance GUID against the GUIDs
// `EquipmentSet::Data` saved into each set, so it names the one physical
// item that was saved rather than every copy of that itemID — a spare
// of the same item sitting in the next slot reports `false`. The set's
// reserved empty / ignored slot markers can never match a real GUID,
// and `Data::SetsContainingItem` rejects them outright.
//
// `setList` is the set names joined with `", "`, in set order, which is
// the shape a caller formats straight into a tooltip line. It is `nil`
// rather than an empty string when the item is in no set, so the usual
// `if inSet then … setList … end` guard is all a caller needs.

#include "Game.h"
#include "Offsets.h"
#include "equipmentset/Data.h"
#include "equipmentset/Set.h"
#include "item/CGItem.h"
#include "item/Location.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Container::EquipmentSetInfo {

namespace {

// The separator between names in `setList`. One place, so the string a
// caller splits on is stated once.
constexpr const char *kSeparator = ", ";

uint64_t ItemGuid(const uint8_t *cgItem) {
    auto *instance = Item::InstanceBlock(cgItem);
    if (instance == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_GUID);
}

int __fastcall Script_C_Container_GetContainerItemEquipmentSetInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerItemEquipmentSetInfo(containerIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));

    // `ResolveBag` stomps stack slots 1 and 2 through the engine's
    // `PackBagSlot`, so both arguments are already in locals above and
    // nothing we need survives on the stack across it.
    const uint64_t guid =
        ItemGuid(Item::Location::ResolveBag(L, bagID, slotIndex));

    const std::vector<const EquipmentSet::Set *> sets =
        EquipmentSet::Data::SetsContainingItem(guid);

    Game::Lua::SetTop(L, 0);
    if (sets.empty()) {
        Game::Lua::PushBool(L, false);
        Game::Lua::PushNil(L);
        return 2;
    }

    std::string setList;
    for (size_t i = 0; i < sets.size(); ++i) {
        if (i > 0)
            setList += kSeparator;
        setList += sets[i]->name;
    }

    Game::Lua::PushBool(L, true);
    Game::Lua::PushString(L, setList.c_str());
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction(
        "C_Container", "GetContainerItemEquipmentSetInfo",
        &Script_C_Container_GetContainerItemEquipmentSetInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::EquipmentSetInfo
