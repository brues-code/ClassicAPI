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

// `C_FriendList.GetWhoInfo(index)` — the modern table form of vanilla's
// `GetWhoInfo(index)`. The vanilla global returns positional values and, most
// notably, no class *token* — only the localized class name. This returns a
// table that includes `filename` (the "MAGE"/"WARRIOR" token), so a /who list
// can be class-colored via RAID_CLASS_COLORS without reverse-mapping the
// localized name.
//
// Reads the same /who result buffer the engine's Script_GetWhoInfo
// (0x005AD6E0) reads (see the WHO_RESULT_* offsets in Offsets.h): an array of
// stride-0xA0 entries with inline name/guild strings and race/class/zone DBC
// indices. `C_FriendList.GetNumWhoResults()` is the companion count.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"

#include <cstdint>

namespace FriendList::WhoInfo {

namespace {

int WhoResultCount() {
    return Game::Read<int>(Offsets::VAR_WHO_RESULT_COUNT);
}

// `C_FriendList.GetNumWhoResults()` → `numWhos, totalCount`. The first is the
// number of entries you can index with GetWhoInfo; the second is the server's
// total-match count (it can exceed the displayed list). Mirrors the vanilla
// global's two returns.
const Game::Doc::Field kGetNumWhoResultsRets[] = {
    Game::Doc::Req("numWhos", "number", "The number of results that GetWhoInfo can return."),
    Game::Doc::Req("totalNumWhos", "number",
                   "The total that the server reports; its meaning depends on the server."),
};
const Game::Doc::Function kGetNumWhoResults{"Returns the number of results of the last /who query.",
                                            {}, kGetNumWhoResultsRets};

int __fastcall Script_GetNumWhoResults(void *L) {
    const int displayed = WhoResultCount();
    const int total = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_WHO_RESULT_COUNT) + 4);
    Game::Lua::PushNumber(L, static_cast<double>(displayed));
    Game::Lua::PushNumber(L, static_cast<double>(total));
    return 2;
}

// `C_FriendList.GetWhoInfo(index)` → WhoInfo table, or nil for a bad index.
const Game::Doc::Field kWhoInfoFields[] = {
    Game::Doc::Req("fullName", "string"),
    Game::Doc::Req("fullGuildName", "string", "Empty when the player has no guild."),
    Game::Doc::Req("level", "number"),
    Game::Doc::Opt("raceStr", "string"),
    Game::Doc::Opt("classStr", "string"),
    Game::Doc::Opt("area", "string"),
    Game::Doc::Opt("filename", "string", nullptr, "The class token, such as \"MAGE\"."),
};
const Game::Doc::Structure kWhoInfo{"WhoInfo", "FriendList", kWhoInfoFields,
                                    "One player in the /who results."};

const Game::Doc::Field kGetWhoInfoArgs[] = {
    Game::Doc::Req("index", "luaIndex"),
};
const Game::Doc::Field kGetWhoInfoRets[] = {
    Game::Doc::Opt("info", "WhoInfo", nullptr, "Nil when the index is out of range."),
};
const Game::Doc::Function kGetWhoInfo{"Returns one result of the last /who query.",
                                      kGetWhoInfoArgs, kGetWhoInfoRets};

int __fastcall Script_GetWhoInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)); // 1-based
    if (index < 1 || index > WhoResultCount()) {
        Game::Lua::PushNil(L);
        return 1;
    }

    const uint8_t *entry = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_WHO_RESULTS) +
        static_cast<size_t>(index - 1) * Offsets::WHO_RESULT_STRIDE);

    const char *name = Game::Ptr<const char>(entry, Offsets::OFF_WHO_NAME);
    const char *guild = Game::Ptr<const char>(entry, Offsets::OFF_WHO_GUILD);
    const int level = Game::Read<int>(entry, Offsets::OFF_WHO_LEVEL);
    const uint32_t raceID =
        Game::Read<uint32_t>(entry, Offsets::OFF_WHO_RACE);
    const uint32_t classID =
        Game::Read<uint32_t>(entry, Offsets::OFF_WHO_CLASS);
    const uint32_t zoneID =
        Game::Read<uint32_t>(entry, Offsets::OFF_WHO_ZONE);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "fullName", name);       // inline; "" if empty
    Game::Lua::SetFieldString(L, "fullGuildName", guild); // "" when guildless
    Game::Lua::SetFieldNumber(L, "level", static_cast<double>(level));
    // Optional strings stay nil (unset) when the DBC id doesn't resolve, so an
    // `if info.filename then` check works — SetFieldString would coerce null
    // to "" (truthy).
    if (const char *raceStr = DBC::RaceName(raceID))
        Game::Lua::SetFieldString(L, "raceStr", raceStr);
    if (const char *classStr = DBC::ClassName(classID))
        Game::Lua::SetFieldString(L, "classStr", classStr);
    if (const char *filename = DBC::ClassToken(classID))
        Game::Lua::SetFieldString(L, "filename", filename);
    // /who zones are already zone-level, so use the area's own name (no parent
    // walk) — matches the engine's own GetWhoInfo.
    if (const char *area = DBC::AreaName(zoneID, /*resolveToParent=*/false))
        Game::Lua::SetFieldString(L, "area", area);
    // No `gender`: vanilla's /who result carries no sex field (the entry ends
    // at the zone index), so modern's gender return has no source here.
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "GetWhoInfo",
                                     &Script_GetWhoInfo, &kGetWhoInfo);
    Game::Lua::RegisterTableFunction("C_FriendList", "GetNumWhoResults",
                                     &Script_GetNumWhoResults, &kGetNumWhoResults);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FriendList::WhoInfo
