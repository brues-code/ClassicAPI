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

// The `trade:` link: a player's profession and the recipes they know, built
// and read as 3.3.5 does.
//
//     |cffffd000|Htrade:<spellID>:<rank>:<maxRank>:<GUID>:<bits>|h[<spell name>]|h|r
//
// Mirrors 3.3.5's CGTradeSkillInfo::GetTradeSkillListLink (0x005DB060) and
// SetLink (0x005DE300):
//   - `spellID` is the profession spell the window was opened with (Effect[0]
//     SPELL_EFFECT_TRADE_SKILL). The reader maps it to a skill line through
//     the SkillLineAbility row for the linker's race and class.
//   - `rank` / `maxRank` are the linker's, as GetTradeSkillLine gives them.
//   - `GUID` is the linker's, `%I64X`. The reader takes the linker's name,
//     race and class from the name cache, and asks the server for a linker
//     it does not hold, opening the link when the answer arrives.
//   - `bits` hold one bit per row of the skill line's link rows (Abilities),
//     six per base64 character, low bit first. A bit is set when the linker
//     knows the row's spell and the row's race and class masks take the
//     linker; 3.3.5's encoder ignores the rows' exclude flags.
//
// The link rows are 3.3.5's SpellTableGetSkillLineLinkInfo: the run of
// SkillLineAbility records of the skill line. 3.3.5's file holds each
// profession in a single run, so the run is every row of the line in file
// order (checked against ChromieCraft's SkillLineAbility.dbc: one run per
// linkable line). 1.12's file is sorted by row ID and scatters a line
// (Blacksmithing over 73 runs), so here the rows are every row of the line in
// ID order: the same set, in an order every client with this DBC agrees on.
// The reader rejects bits whose length does not match its own row count.
//
// What links: a profession spell, on both ends. 3.3.5 also requires the
// skill line's canLink column, which 1.12's SkillLine.dbc does not have, so
// every profession spell links: Smelting, Poisons and Beast Training too.
//
// The reader shows the link in the engine's own profession windows
// (Window.cpp). GetTradeSkillListLink() is 3.3.5's, for the TradeSkill
// window. GetCraftListLink() is its twin for the Craft window (Enchanting),
// which 3.3.5 does not have.

#include "Link.h"

#include "Game.h"
#include "Offsets.h"
#include "Window.h"
#include "dbc/Lookup.h"
#include "spell/Known.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace TradeSkill::Link {

namespace {

using AbilityLookup_t = const uint8_t *(__fastcall *)(uint32_t race, uint32_t classId,
                                                      uint32_t spellID);
using SlotForSkill_t = int(__thiscall *)(const void *player, uint32_t skillLine);
using NameCacheGet_t = const uint8_t *(__thiscall *)(void *cache, uint32_t lo, uint32_t hi,
                                                     const uint64_t *owner, void *callback,
                                                     void *userData, char dedupe);

constexpr int kRowsPerChar = 6;

// 3.3.5's s_base64Encode (0x00A1B640).
constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 3.3.5's s_base64Decode (0x00A1B688): an unknown character reads as 0.
int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return 0;
}

// 3.3.5's SpellTableMatchRaceClass with no exclusions, as its encoder calls it.
bool MatchRaceClass(uint32_t race, uint32_t classId, uint32_t raceMask, uint32_t classMask) {
    return (raceMask == 0 || (raceMask & (1u << (race - 1))) != 0) &&
           (classMask == 0 || (classMask & (1u << (classId - 1))) != 0);
}

// The player's rank and maximum in `skillLine` as GetTradeSkillLine pushes
// them: each plus the permanent bonus when it is not 0, both 0 without a
// slot (3.3.5's GetSkillValue / GetSkillMaxRank, which its encoder writes).
void PlayerRank(const uint8_t *player, uint32_t skillLine, int *rank, int *maxRank) {
    *rank = 0;
    *maxRank = 0;
    const int slot =
        reinterpret_cast<SlotForSkill_t>(Offsets::FUN_SKILL_LINE_TO_SLOT)(player, skillLine);
    if (slot < 0)
        return;
    const uint8_t *skills = Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO) +
                            Offsets::OFF_SKILL_INFO_TABLE + slot * Offsets::SKILL_INFO_STRIDE;
    const int bonus = Game::Read<uint16_t>(skills, Offsets::OFF_SKILL_INFO_BONUS);
    const int cur = Game::Read<uint16_t>(skills, Offsets::OFF_SKILL_INFO_CUR);
    const int max = Game::Read<uint16_t>(skills, Offsets::OFF_SKILL_INFO_MAX);
    *rank = cur != 0 ? cur + bonus : 0;
    *maxRank = max != 0 ? max + bonus : 0;
}

// 3.3.5's CGTradeSkillInfo::GetTradeSkillListLink: the local player's link
// for profession `spellID` into `buf`, or false.
bool Build(int spellID, char *buf, size_t size) {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return false;
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr ||
        Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_EFFECT) !=
            Offsets::SPELL_EFFECT_TRADE_SKILL)
        return false;

    const uint8_t *desc = Unit::Identity::PlayerDescriptor();
    if (desc == nullptr)
        return false;
    const uint32_t race = desc[Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE];
    const uint32_t classId = desc[Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE];
    const uint8_t *ability = reinterpret_cast<AbilityLookup_t>(
        Offsets::FUN_SKILL_LINE_ABILITY_LOOKUP)(race, classId, static_cast<uint32_t>(spellID));
    if (ability == nullptr)
        return false;
    const uint32_t skillLine = Game::Read<uint32_t>(ability, Offsets::OFF_SLA_SKILL_ID);
    if (DBC::Record(Offsets::VAR_SKILL_LINE_RECORDS, Offsets::VAR_SKILL_LINE_COUNT, skillLine) ==
        nullptr)
        return false;

    const uint8_t *rows[MAX_ABILITIES];
    const int count = Abilities(skillLine, rows);
    if (count == 0)
        return false;

    int rank, maxRank;
    PlayerRank(player, skillLine, &rank, &maxRank);
    const int prefix = std::snprintf(buf, size, "|cffffd000|Htrade:%d:%d:%d:%I64X:", spellID,
                                     rank, maxRank, Unit::Identity::PlayerGuid());
    if (prefix < 0 || static_cast<size_t>(prefix + BitChars(count) + 1) >= size)
        return false;

    char *out = buf + prefix;
    unsigned bits = 0;
    int bit = 0;
    for (int i = 0; i < count; ++i) {
        if (Spell::Known::IsKnown(Game::Read<uint32_t>(rows[i], Offsets::OFF_SLA_SPELL_ID)) &&
            MatchRaceClass(race, classId, Game::Read<uint32_t>(rows[i], Offsets::OFF_SLA_RACE_MASK),
                           Game::Read<uint32_t>(rows[i], Offsets::OFF_SLA_CLASS_MASK)))
            bits |= 1u << bit;
        if (++bit == kRowsPerChar) {
            *out++ = kBase64[bits];
            bits = 0;
            bit = 0;
        }
    }
    if (bit != 0)
        *out++ = kBase64[bits];

    const char *name = DBC::LocalizedField(Offsets::VAR_SPELL_RECORDS,
                                           Offsets::VAR_SPELL_RECORD_COUNT,
                                           static_cast<uint32_t>(spellID), Offsets::OFF_SPELL_NAMES);
    std::snprintf(out, static_cast<size_t>(buf + size - out), "|h[%s]|h|r",
                  name != nullptr ? name : "");
    return true;
}

// 3.3.5's TradeSkillLinkNameCallback: the name query for a link's linker was
// answered; `data` is the copy of the link Open made.
void __stdcall LinkerNameLoaded(void *data, char success) {
    if (success)
        Open(static_cast<const char *>(data));
    std::free(data);
}

int PushLink(void *L, int spellID) {
    char link[256];
    if (Build(spellID, link, sizeof link))
        Game::Lua::PushString(L, link);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// `GetTradeSkillListLink()` — 3.3.5's Script_GetTradeSkillListLink.
int __fastcall Script_GetTradeSkillListLink(void *L) {
    return PushLink(L, Window::TradeSkillSpell());
}

// `GetCraftListLink()` — the Craft window's twin.
int __fastcall Script_GetCraftListLink(void *L) { return PushLink(L, Window::CraftSpell()); }

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kListLinkRets[] = {
    Game::Doc::Opt("link", "string", nullptr,
                   "A trade: link; nil when the window is closed or its profession cannot be "
                   "linked."),
};
const Game::Doc::Function kGetTradeSkillListLink{
    "Returns a link to the player's profession in the trade-skill window, with the recipes the "
    "player knows.",
    {}, kListLinkRets, "TradeSkillGlobals"};
const Game::Doc::Function kGetCraftListLink{
    "Returns a link to the player's profession in the craft window, with the recipes the player "
    "knows.",
    {}, kListLinkRets, "TradeSkillGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTradeSkillListLink", &Script_GetTradeSkillListLink,
                                      &kGetTradeSkillListLink);
    Game::Lua::RegisterGlobalFunction("GetCraftListLink", &Script_GetCraftListLink,
                                      &kGetCraftListLink);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

int Abilities(uint32_t skillLine, const uint8_t **out) {
    auto *const *records =
        Game::Read<const uint8_t *const *>(Offsets::VAR_SKILL_LINE_ABILITY_RECORDS);
    const int count = Game::Read<int>(Offsets::VAR_SKILL_LINE_ABILITY_COUNT);
    if (records == nullptr)
        return 0;
    int n = 0;
    for (int id = 1; id <= count && n < MAX_ABILITIES; ++id) {
        const uint8_t *row = records[id];
        if (row != nullptr && Game::Read<uint32_t>(row, Offsets::OFF_SLA_SKILL_ID) == skillLine)
            out[n++] = row;
    }
    return n;
}

int BitChars(int count) { return (count + kRowsPerChar - 1) / kRowsPerChar; }

bool IsSet(const char *bits, int index) {
    return (Base64Value(bits[index / kRowsPerChar]) & (1 << (index % kRowsPerChar))) != 0;
}

void Open(const char *data) {
    if (data == nullptr || *data == '\0')
        return;
    const int spellID = std::atoi(data);
    if (spellID == 0)
        return;
    const char *fields = std::strchr(data, ':');
    if (fields == nullptr)
        return;
    ++fields;
    // 3.3.5 reads `%d:%d:%I64X:`; a field after a malformed one stays 0.
    int rank = 0;
    int maxRank = 0;
    uint64_t guid = 0;
    char *end = nullptr;
    rank = static_cast<int>(std::strtol(fields, &end, 10));
    if (*end == ':') {
        maxRank = static_cast<int>(std::strtol(end + 1, &end, 10));
        if (*end == ':')
            guid = std::strtoull(end + 1, nullptr, 16);
    }
    if (guid == 0)
        return;

    // A miss keeps the copy with the query and opens the link on the answer.
    char *copy = _strdup(data);
    if (copy == nullptr)
        return;
    const uint64_t none = 0;
    const uint8_t *linker = reinterpret_cast<NameCacheGet_t>(Offsets::FUN_PLAYER_INFO_LOOKUP)(
        reinterpret_cast<void *>(Offsets::VAR_PLAYER_NAME_CACHE), static_cast<uint32_t>(guid),
        static_cast<uint32_t>(guid >> 32), &none, reinterpret_cast<void *>(&LinkerNameLoaded),
        copy, 1);
    if (linker == nullptr)
        return;
    std::free(copy);
    if (const char *bits = std::strrchr(fields, ':'))
        Window::ShowLinked(spellID, guid, bits + 1, rank, maxRank);
}

} // namespace TradeSkill::Link
