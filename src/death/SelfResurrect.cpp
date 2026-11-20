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

// `C_DeathInfo.GetSelfResurrectOptions()`, `C_DeathInfo.UseSelfResurrectOption
// (optionType, id)` and `SELF_RES_SPELL_CHANGED` — the self-resurrection that
// 1.12 serves through HasSoulstone (0x0048AC80) and UseSoulstone (0x0048AD70).
//
// 1.12 offers at most one way for a dead player to raise themselves, and both
// functions pick it the same way: PLAYER_SELF_RES_SPELL when the server set it
// (Soulstone Resurrection, Reincarnation), else the first carried item whose
// on-use spell is a self-resurrect. So the list holds that one option, under
// HasSoulstone's gate (a living player has none), and using it calls
// UseSoulstone itself. Nothing in 1.12 limits a self-resurrection, so every
// option can be used and none is limited.
//
// SELF_RES_SPELL_CHANGED is 4.3.4's: CGPlayer_C::SetActiveMirrorHandlers
// watches PLAYER_SELF_RES_SPELL with OnUpdatePlayerScriptEvent. 1.12 watches
// no such field, so the same watcher joins 1.12's AddActiveWatchers and
// leaves with its removal, the lifetime of the engine's own player watchers.

#include "Game.h"
#include "Offsets.h"
#include "player/ActiveWatchers.h"
#include "event/Custom.h"
#include "item/Link.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Death::SelfResurrect {

namespace {

constexpr int OPTION_TYPE_SPELL = 0;
constexpr int OPTION_TYPE_ITEM = 1;

const Game::Doc::Event kChangedDoc{
    "DeathInfo",
    "Fires when the player's ways to resurrect themselves change.",
    {}};
const Event::Custom::AutoReserve kChanged{"SELF_RES_SPELL_CHANGED", &kChangedDoc};

struct Option {
    const char *name;
    int optionType;
    int id;
};

// HasSoulstone's answer: the option, or false while the player lives or has
// none. `itemName` holds an item option's name.
bool CurrentOption(Option &out, char *itemName, size_t itemNameSize) {
    const uint8_t *player = Unit::Identity::PlayerObject();
    const uint8_t *desc = Unit::Identity::PlayerDescriptor();
    if (player == nullptr || desc == nullptr ||
        Game::Read<int32_t>(desc, Offsets::OFF_UNIT_FIELD_HEALTH) > 0)
        return false;
    const auto *info = Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
    const int spellID =
        info != nullptr ? Game::Read<int32_t>(info, Offsets::OFF_CGPLAYER_INFO_SELF_RES_SPELL) : 0;
    if (spellID != 0) {
        const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
        const char *name = Spell::Lookup::Name(rec);
        out = {rec == nullptr ? "UNKNOWN" : name != nullptr ? name : "", OPTION_TYPE_SPELL,
               spellID};
        return true;
    }
    using SelfResItem_t = const uint8_t *(__fastcall *)(const void *player);
    const uint8_t *item =
        reinterpret_cast<SelfResItem_t>(Offsets::FUN_PLAYER_SELF_RES_ITEM)(player);
    if (item == nullptr)
        return false;
    Item::Link::NameFromCGItem(item, itemName, itemNameSize);
    out = {itemName, OPTION_TYPE_ITEM, static_cast<int32_t>(::Object::Entry(item))};
    return true;
}

int __fastcall Script_GetSelfResurrectOptions(void *L) {
    if (Unit::Identity::PlayerObject() == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    char itemName[0x400];
    Option option;
    if (CurrentOption(option, itemName, sizeof(itemName))) {
        Game::Lua::PushNumber(L, 1);
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(L, "name", option.name);
        Game::Lua::SetFieldNumber(L, "optionType", option.optionType);
        Game::Lua::SetFieldNumber(L, "id", option.id);
        Game::Lua::SetFieldBool(L, "canUse", true);
        Game::Lua::SetFieldBool(L, "isLimited", false);
        Game::Lua::SetFieldNumber(L, "priority", 0);
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

int __fastcall Script_UseSelfResurrectOption(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: C_DeathInfo.UseSelfResurrectOption(optionType, id)");
        return 0;
    }
    char itemName[0x400];
    Option option;
    if (CurrentOption(option, itemName, sizeof(itemName)) &&
        option.optionType == static_cast<int>(Game::Lua::ToNumber(L, 1)) &&
        option.id == static_cast<int>(Game::Lua::ToNumber(L, 2)))
        Game::Lua::CallScript(Offsets::FUN_SCRIPT_USE_SOULSTONE, L);
    return 0;
}

// Observer callback ABI per the FUN_DESC_OBSERVER_REGISTER block in
// Offsets.h: __fastcall(fieldOffset /*ecx*/, size /*edx*/, guidLo, guidHi,
// oldValues*, userArg), returns 1, callee cleans 0x10. It runs only when the
// field's bytes changed.
int __fastcall SelfResSpellChanged(uint32_t /*fieldOffset*/, uint32_t /*size*/,
                                   uint32_t /*guidLo*/, uint32_t /*guidHi*/,
                                   const void * /*oldValues*/, void * /*userArg*/) {
    Event::Custom::Fire(kChanged.Slot(), "");
    return 1;
}

const Player::ActiveWatchers::AutoWatch _watch{
    {Offsets::OFF_CGPLAYER_INFO_SELF_RES_SPELL, 4, &SelfResSpellChanged}};

const Game::Lua::EnumIntegerEntry kOptionTypeEntries[] = {
    {"Spell", OPTION_TYPE_SPELL},
    {"Item", OPTION_TYPE_ITEM},
};

const Game::Doc::Field kOptionFields[] = {
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("optionType", "SelfResurrectOptionType"),
    Game::Doc::Req("id", "number", "The spell ID or item ID."),
    Game::Doc::Req("canUse", "bool"),
    Game::Doc::Req("isLimited", "bool"),
    Game::Doc::Req("priority", "number", "Lower comes first."),
};
const Game::Doc::Structure kOption{"SelfResurrectOption", "DeathInfo", kOptionFields,
                                   "A way for the dead player to resurrect themselves."};

const Game::Doc::Field kGetRets[] = {
    Game::Doc::Req("options", "table", "Array of SelfResurrectOption; empty while alive."),
};
const Game::Doc::Function kGetSelfResurrectOptions{
    "Returns the ways the dead player can resurrect themselves.", {}, kGetRets};

const Game::Doc::Field kUseArgs[] = {
    Game::Doc::Req("optionType", "SelfResurrectOptionType"),
    Game::Doc::Req("id", "number"),
};
const Game::Doc::Function kUseSelfResurrectOption{
    "Resurrects the dead player with one of the options from GetSelfResurrectOptions.",
    kUseArgs, {}};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_DeathInfo", "GetSelfResurrectOptions",
                                     &Script_GetSelfResurrectOptions, &kGetSelfResurrectOptions);
    Game::Lua::RegisterTableFunction("C_DeathInfo", "UseSelfResurrectOption",
                                     &Script_UseSelfResurrectOption, &kUseSelfResurrectOption);
    Game::Lua::RegisterIntegerEnum(
        "Enum", "SelfResurrectOptionType", kOptionTypeEntries, "DeathInfo");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Death::SelfResurrect
