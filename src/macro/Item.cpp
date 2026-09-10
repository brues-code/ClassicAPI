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

// `GetMacroItem(macroSlot)` — `(name, link)` of the item a macro's
// `#showtooltip` / `#show` directive resolved to (the item counterpart of
// `GetMacroSpell`). The carried instance's decorated name + link when the
// item is in equipment or bags, the plain cached name + `item:` link when it
// isn't. Nothing when the macro has no directive or it resolved to a spell.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Link.h"
#include "item/Location.h"
#include "macro/ShowTooltip.h"

#include <cstdint>

namespace Macro::Item {

namespace {

int __fastcall Script_GetMacroItem(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetMacroItem(macroSlot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (slot < 1 || slot > Offsets::MACRO_SLOT_MAP_COUNT)
        return 0;
    const uint32_t macroID = Game::Read<uint32_t>(
        static_cast<uintptr_t>(Offsets::VAR_MACRO_SLOT_MAP) + static_cast<uintptr_t>(slot - 1) * 4);

    Macro::ShowTooltip::Info info;
    if (!Macro::ShowTooltip::Lookup(macroID, &info) ||
        info.target != Macro::ShowTooltip::Target::Item)
        return 0;

    char name[128];
    ::Item::Arg::Resolved arg{info.itemID, 0, nullptr};
    ::Item::Location::ByGUIDResult found;
    if (::Item::Location::FindByArgNoLua(arg, &found)) {
        const char *link = ::Item::Link::FromCGItem(found.item);
        if (link != nullptr && ::Item::Link::NameFromCGItem(found.item, name, sizeof(name))) {
            Game::Lua::PushString(L, name);
            Game::Lua::PushString(L, link);
            return 2;
        }
    }
    char link[256];
    if (::Item::Link::NameFromIDSuffix(static_cast<uint32_t>(info.itemID), 0, name, sizeof(name)) &&
        ::Item::Link::BasicFromItemID(static_cast<uint32_t>(info.itemID), link, sizeof(link))) {
        Game::Lua::PushString(L, name);
        Game::Lua::PushString(L, link);
        return 2;
    }
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMacroItem", &Script_GetMacroItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::Item
