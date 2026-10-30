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

#include "Game.h"
#include "container/Bag.h"

namespace Container::FreeSlots {

namespace {

// `C_Container.GetContainerNumFreeSlots(bagID)` — `(numFreeSlots,
// bagFamily)`. 3.3.5's `Script_GetContainerNumFreeSlots` (`0x005D7590`):
// the backpack and, at a banker, the main bank report family 0; bags 1..4
// and bank bags 5..10 report the bag's own family. A container that isn't
// there — keyring, an empty bag slot, the bank while it's closed — returns
// only `0`, as 3.3.5 does and as retail's nilable `bagFamily` documents.
//
// `bagFamily` is the modern bitmask; see `Container::Bag::ViewFor` for how
// a vanilla bag with an empty family field gets one.
int __fastcall Script_C_Container_GetContainerNumFreeSlots(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerNumFreeSlots(bagID)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));

    Container::Bag::View view;
    if (!Container::Bag::ViewFor(bagID, &view)) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(Container::Bag::CountFree(view)));
    Game::Lua::PushNumber(L, static_cast<double>(view.family));
    return 2;
}

// `C_Container.GetContainerFreeSlots(bagID)` — which slots are empty,
// as an array of slot numbers in ascending order, rather than
// `GetContainerNumFreeSlots`'s count of them. A full bag returns an
// empty table; a bag that isn't there returns nothing, which is how
// callers tell "no free slots" from "no such bag". Takes the same
// containers as `GetContainerNumFreeSlots`.
int __fastcall Script_C_Container_GetContainerFreeSlots(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerFreeSlots(bagID)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));

    Container::Bag::View view;
    if (!Container::Bag::ViewFor(bagID, &view))
        return 0; // nothing — no such bag

    Game::Lua::NewTable(L);
    int n = 0;
    for (int i = 0; i < view.slotCount; ++i) {
        if (Container::Bag::SlotGuid(view, i) != 0)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(++n));
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::RawSet(L, -3);
    }
    return 1;
}

// `C_Container.CalculateTotalNumberOfFreeBagSlots()` — the total number of
// free slots across the player's *general-purpose* bags (backpack + equipped
// bags 0..4 whose BagFamily is 0). Specialty bags (quivers, soul bags, herb
// bags, …) are excluded, mirroring FrameXML's implementation exactly:
//
//   for bag = BACKPACK .. lastBag:
//       free, family = C_Container.GetContainerNumFreeSlots(bag)
//       if family == 0 then total = total + free end
//
// Takes no arguments.
int __fastcall Script_C_Container_CalculateTotalNumberOfFreeBagSlots(void *L) {
    int total = 0;
    for (int bagID = 0; bagID <= 4; bagID++) {
        Container::Bag::View view;
        if (Container::Bag::ViewFor(bagID, &view) && view.family == 0)
            total += Container::Bag::CountFree(view);
    }
    Game::Lua::PushNumber(L, static_cast<double>(total));
    return 1;
}

const Game::Doc::Field kBagArgs[] = {
    Game::Doc::Req("bagIndex", "BagIndex", "0 for the backpack, 1 to 4 for the equipped bags."),
};
const Game::Doc::Field kNumFreeSlotsRets[] = {
    Game::Doc::Req("numFreeSlots", "number", "0 when the bag does not exist."),
    Game::Doc::Req("bagFamily", "number",
                   "The bag's family bitmask; 0 for a bag that takes any item."),
};
const Game::Doc::Function kGetContainerNumFreeSlots{
    "Returns the number of empty slots in a bag and the kind of items the bag takes.",
    kBagArgs, kNumFreeSlotsRets};

const Game::Doc::Field kFreeSlotsRets[] = {
    Game::Doc::Opt("freeSlots", "table", nullptr,
                   "The empty slot numbers in ascending order; nil when the bag does not exist."),
};
const Game::Doc::Function kGetContainerFreeSlots{
    "Returns the numbers of the empty slots in a bag.", kBagArgs, kFreeSlotsRets};

const Game::Doc::Field kTotalRets[] = {
    Game::Doc::Req("totalFreeSlots", "number"),
};
const Game::Doc::Function kCalculateTotalNumberOfFreeBagSlots{
    "Returns the number of empty slots in the backpack and the equipped bags that take "
    "any item.",
    {}, kTotalRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerNumFreeSlots",
                                     &Script_C_Container_GetContainerNumFreeSlots,
                                     &kGetContainerNumFreeSlots);
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerFreeSlots",
                                     &Script_C_Container_GetContainerFreeSlots,
                                     &kGetContainerFreeSlots);
    Game::Lua::RegisterTableFunction("C_Container", "CalculateTotalNumberOfFreeBagSlots",
                                     &Script_C_Container_CalculateTotalNumberOfFreeBagSlots,
                                     &kCalculateTotalNumberOfFreeBagSlots);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Container::FreeSlots
