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

// Whose profession the TradeSkill and Craft windows show: the player's own,
// or another player's from a `trade:` link, as 3.3.5's trade-skill window
// does.
//
// 3.3.5's CGTradeSkillInfo keeps whose profession the window shows
// (m_playerGuid, m_spellID, m_skillRank / m_skillMaxRank, m_linkData), and its
// list build RefreshList (0x005DD5A0) takes the rows from the link when
// m_linkData is set. 1.12's BuildTradeSkillList (0x004FCA20) is the same build
// without the link branch. This module adds the branch at the build's three
// per-row reads, and the same for the Craft window (Enchanting), which 3.3.5
// does not have:
//   - The spell list: the link's rows instead of the player's list
//     (CGPlayer_C::FindSkillLineSpells, or GetTabSpells for the Craft
//     window). The rows are the set bits whose spell would be in that list
//     had the linker learned it: the engine's learn function (0x005E9C20)
//     files a SPELL_ATTR_TRADESPELL spell under its skill line and any other
//     under its castUI tab. RefreshList keeps the TRADESPELL rows, the same
//     filter.
//   - The ability row: looked up for the linker's race and class. RefreshList
//     takes the link's row itself.
//   - The rank: the linker's, unless the link is the player's own.
// Everything else in the builds runs as for the player's own window:
// available counts from the player's bags, headers, filters, order, events.
//
// Opening (3.3.5 SetTradeSkill, 0x005DE120): a link opens the window its
// spell opens when cast (TryCastSpell routes on EffectMiscValue[0]), through
// the engine's own toggle, with the window marked closed first so that a link
// to the line already open replaces it instead of closing it. Casting a
// profession while a link shows opens the player's own list, again without
// closing. Both casts reach the toggles from one call each in TryCastSpell,
// whose cast spell (ESI) is 3.3.5's m_spellID. As in 3.3.5, closing keeps the
// link, so IsTradeSkillLinked answers until the next open, and the return to
// the character screen drops it (CGTradeSkillInfo::ShutdownGame; here
// CGGameUI::ShutdownGame's calls of the two window shutdowns).
//
// The Lua getters that answer for the player answer for the linker while a
// link shows: GetTradeSkillLine (3.3.5's), and the Craft window's
// GetCraftDisplaySkillLine and GetCraftName. IsTradeSkillLinked is 3.3.5's;
// IsCraftLinked is its Craft window twin.
//
// Each window takes links only when every one of its seams is patched, so a
// seam another DLL holds can't show a link half-way.

#include "Window.h"

#include "Common.h"
#include "Game.h"
#include "Link.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace TradeSkill::Window {

namespace {

using Toggle_t = void(__fastcall *)(uint32_t key);
using Void_t = void(__cdecl *)();
using AbilityLookup_t = const uint8_t *(__fastcall *)(uint32_t race, uint32_t classId,
                                                      uint32_t spellID);
using PlayerList_t = const void *(__thiscall *)(void *player, uint32_t key);
using UnitAbility_t = const uint8_t *(__thiscall *)(void *unit, uint32_t spellID);
using SkillTotal_t = int(__thiscall *)(void *player, uint32_t skillLine);
using SlotForSkill_t = int(__thiscall *)(const void *player, uint32_t skillLine);
using Script_t = int(__fastcall *)(void *L);

// The only Craft type GetCraftDisplaySkillLine answers for (Enchanting).
constexpr uint32_t kCraftTypeEnscribe = 3;

// The engine's growable array, as the list builds read it.
struct SpellArray {
    uint32_t capacity;
    uint32_t count;
    const int *data;
    uint32_t granularity;
};
static_assert(offsetof(SpellArray, count) == 0x04 && offsetof(SpellArray, data) == 0x08,
              "the builds read the count at +0x04 and the spells at +0x08");

// What a window shows: 3.3.5's CGTradeSkillInfo state.
struct Shown {
    int spellID = 0;   // the profession spell (m_spellID)
    uint64_t guid = 0; // whose profession (m_playerGuid)
    bool linked = false;
    uint32_t opened = 0; // what the window opened on: the skill line, or the Craft type
    uint32_t skillLine = 0;
    uint32_t race = 0; // the linker's
    uint32_t classId = 0;
    int rank = 0; // the linker's (m_skillRank / m_skillMaxRank)
    int maxRank = 0;
    std::vector<int> spells; // the link's rows
    SpellArray list{};
};

Shown g_tradeSkill;
Shown g_craft;
bool g_tradeSkillReady = false;
bool g_craftReady = false;

uint32_t TradeSkillOpen() { return Game::Read<uint32_t>(Offsets::VAR_CURRENT_TRADESKILL_LINE); }
uint32_t CraftOpen() { return Game::Read<uint32_t>(Offsets::VAR_CRAFT_WINDOW_STATE); }

const uint8_t *Ability(uint32_t race, uint32_t classId, uint32_t spellID) {
    return reinterpret_cast<AbilityLookup_t>(Offsets::FUN_SKILL_LINE_ABILITY_LOOKUP)(race, classId,
                                                                                    spellID);
}

const char *SpellName(int spellID) {
    return DBC::LocalizedField(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT,
                               static_cast<uint32_t>(spellID), Offsets::OFF_SPELL_NAMES);
}

// The window open on `open` builds its rows from the link.
bool BuildsLink(const Shown &s, uint32_t open) { return s.linked && open == s.opened; }

// The window open on `open` shows another player's link, whose ranks it
// answers with (3.3.5: m_playerGuid is not the active player).
bool ShowsOtherLink(const Shown &s, uint32_t open) {
    return open != 0 && BuildsLink(s, open) && s.guid != Unit::Identity::PlayerGuid();
}

void Unlink(Shown &s) {
    s.linked = false;
    s.spells.clear();
    s.list = {};
}

// Whether the player's own list for the window holds `spellID` once learned:
// the TradeSkill window's list takes TRADESPELL spells, a Craft window's the
// other spells of its castUI tab.
bool InPlayerList(int spellID, uint32_t craftType) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return false;
    const bool tradeSpell = (Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
                             Offsets::SPELL_ATTR_TRADESPELL) != 0;
    if (craftType == 0)
        return tradeSpell;
    return !tradeSpell && Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_CAST_UI) == craftType;
}

// --- Opening ------------------------------------------------------------------

// The player cast profession `spellID`, which opens the window on `key`.
void Cast(Shown &s, uint32_t key, int spellID, uintptr_t openVar, uintptr_t toggle) {
    if (s.linked) {
        Unlink(s);
        if (Game::Read<uint32_t>(openVar) == key)
            Game::Ref<uint32_t>(openVar) = 0;
    }
    s.spellID = spellID;
    s.guid = Unit::Identity::PlayerGuid();
    s.opened = key;
    reinterpret_cast<Toggle_t>(toggle)(key);
}

void __fastcall CastTradeSkill(uint32_t skillLine, int spellID) {
    Cast(g_tradeSkill, skillLine, spellID, Offsets::VAR_CURRENT_TRADESKILL_LINE,
         Offsets::FUN_TOGGLE_TRADE_SKILL);
}

void __fastcall CastCraft(uint32_t craftType, int spellID) {
    Cast(g_craft, craftType, spellID, Offsets::VAR_CRAFT_WINDOW_STATE, Offsets::FUN_TOGGLE_CRAFT);
}

// Stand in for TryCastSpell's toggle calls: the toggle's key in ECX, the cast
// spell in ESI.
__declspec(naked) void CastTradeSkillThunk() {
    __asm {
        mov edx, esi
        jmp CastTradeSkill
    }
}

__declspec(naked) void CastCraftThunk() {
    __asm {
        mov edx, esi
        jmp CastCraft
    }
}

// --- The list builds ----------------------------------------------------------

// CALL_BUILD_TRADE_SKILL_SPELLS: CGPlayer_C::FindSkillLineSpells.
const void *__fastcall TradeSkillSpells(void *player, void *, uint32_t skillLine) {
    if (BuildsLink(g_tradeSkill, skillLine))
        return &g_tradeSkill.list;
    return reinterpret_cast<PlayerList_t>(Offsets::FUN_PLAYER_FIND_SKILL_LINE_SPELLS)(player,
                                                                                     skillLine);
}

// CALL_BUILD_TRADE_SKILL_ABILITY: the ability lookup for the player's race
// and class.
const uint8_t *__fastcall TradeSkillAbility(uint32_t race, uint32_t classId, uint32_t spellID) {
    const Shown &s = g_tradeSkill;
    if (BuildsLink(s, TradeSkillOpen()))
        return Ability(s.race, s.classId, spellID);
    return Ability(race, classId, spellID);
}

// SITE_BUILD_TRADE_SKILL_RANK: the player's raw rank in the row's line, 0
// without a slot, as the 33 bytes it replaces read it.
int __fastcall TradeSkillRank(const uint8_t *player, void *, uint32_t skillLine) {
    if (ShowsOtherLink(g_tradeSkill, TradeSkillOpen()))
        return g_tradeSkill.rank;
    const int slot =
        reinterpret_cast<SlotForSkill_t>(Offsets::FUN_SKILL_LINE_TO_SLOT)(player, skillLine);
    if (slot < 0)
        return 0;
    return Game::Read<uint16_t>(Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO) +
                                    Offsets::OFF_SKILL_INFO_TABLE + slot * Offsets::SKILL_INFO_STRIDE,
                                Offsets::OFF_SKILL_INFO_CUR);
}

// CALL_BUILD_CRAFT_SPELLS: CGPlayer_C::GetTabSpells.
const void *__fastcall CraftSpells(void *player, void *, uint32_t tab) {
    if (BuildsLink(g_craft, tab))
        return &g_craft.list;
    return reinterpret_cast<PlayerList_t>(Offsets::FUN_PLAYER_GET_TAB_SPELLS)(player, tab);
}

// CALL_BUILD_CRAFT_ABILITY: CGUnit_C::FindSkillLineAbility on the player.
const uint8_t *__fastcall CraftAbility(void *unit, void *, uint32_t spellID) {
    const Shown &s = g_craft;
    if (BuildsLink(s, CraftOpen()))
        return Ability(s.race, s.classId, spellID);
    return reinterpret_cast<UnitAbility_t>(Offsets::FUN_UNIT_FIND_SKILL_LINE_ABILITY)(unit,
                                                                                     spellID);
}

// CALL_BUILD_CRAFT_RANK: CGPlayer_C::GetSkillTotal.
int __fastcall CraftRank(void *player, void *, uint32_t skillLine) {
    if (ShowsOtherLink(g_craft, CraftOpen()))
        return g_craft.rank;
    return reinterpret_cast<SkillTotal_t>(Offsets::FUN_PLAYER_GET_SKILL_TOTAL)(player, skillLine);
}

// CGGameUI::ShutdownGame's calls of the window shutdowns.
void __cdecl ShutdownTradeSkill() {
    g_tradeSkill = Shown{};
    reinterpret_cast<Void_t>(Offsets::FUN_SHUTDOWN_TRADE_SKILL)();
}

void __cdecl ShutdownCraft() {
    g_craft = Shown{};
    reinterpret_cast<Void_t>(Offsets::FUN_SHUTDOWN_CRAFT)();
}

// --- Lua ----------------------------------------------------------------------

int PushLinked(void *L, const Shown &s) {
    if (!s.linked) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }
    Game::Lua::PushNumber(L, 1.0);
    if (const char *name = Unit::Identity::NameForGuid(s.guid))
        Game::Lua::PushString(L, name);
    else
        Game::Lua::PushNil(L);
    return 2;
}

// `IsTradeSkillLinked()` — 3.3.5's Script_IsTradeSkillLinked (0x005DA310).
int __fastcall Script_IsTradeSkillLinked(void *L) { return PushLinked(L, g_tradeSkill); }

// `IsCraftLinked()` — the Craft window's twin.
int __fastcall Script_IsCraftLinked(void *L) { return PushLinked(L, g_craft); }

// `GetTradeSkillLine()`: 3.3.5 answers with the linker's ranks.
int __fastcall Script_GetTradeSkillLine(void *L) {
    const int pushed = reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GET_TRADE_SKILL_LINE)(L);
    if (ShowsOtherLink(g_tradeSkill, TradeSkillOpen())) {
        Game::Lua::SetTop(L, -3); // the player's rank and maximum
        Game::Lua::PushNumber(L, g_tradeSkill.rank);
        Game::Lua::PushNumber(L, g_tradeSkill.maxRank);
    }
    return pushed;
}

// `GetCraftDisplaySkillLine()`: the engine reads the line from the player's
// own list, which may not hold it.
int __fastcall Script_GetCraftDisplaySkillLine(void *L) {
    const Shown &s = g_craft;
    const uint32_t open = CraftOpen();
    const char *name = open == kCraftTypeEnscribe && ShowsOtherLink(s, open)
                           ? DBC::LocalizedField(Offsets::VAR_SKILL_LINE_RECORDS,
                                                 Offsets::VAR_SKILL_LINE_COUNT, s.skillLine,
                                                 Offsets::OFF_SKILL_LINE_NAME)
                           : nullptr;
    if (name == nullptr)
        return reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GET_CRAFT_DISPLAY_SKILL_LINE)(L);
    Game::Lua::PushString(L, name);
    Game::Lua::PushNumber(L, s.rank);
    Game::Lua::PushNumber(L, s.maxRank);
    return 3;
}

// `GetCraftName()`: the engine names the spell the player keeps for the type,
// which may be none.
int __fastcall Script_GetCraftName(void *L) {
    const char *name = ShowsOtherLink(g_craft, CraftOpen()) ? SpellName(g_craft.spellID) : nullptr;
    if (name == nullptr)
        return reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GET_CRAFT_NAME)(L);
    Game::Lua::PushString(L, name);
    return 1;
}

// --- Documentation ------------------------------------------------------------

const Game::Doc::Field kLinkedRets[] = {
    Game::Doc::Opt("isLinked", "number", nullptr,
                   "1 when the window shows a profession from a link, else nil."),
    Game::Doc::Opt("linkedPlayerName", "string", nullptr,
                   "The name of the player whose profession the window shows."),
};
const Game::Doc::Function kIsTradeSkillLinked{
    "Returns whether the trade-skill window shows a profession from a link, and whose.", {},
    kLinkedRets, "TradeSkillGlobals"};
const Game::Doc::Function kIsCraftLinked{
    "Returns whether the craft window shows a profession from a link, and whose.", {},
    kLinkedRets, "TradeSkillGlobals", true};

void Register() {
    static const uint8_t kRankRead[33] = {
        0xE8, 0xC3, 0xD7, 0x0E, 0x00,             // call FindSkillIndex
        0x85, 0xC0,                               // test eax, eax
        0x7C, 0x16,                               // jl -> xor eax, eax
        0x8D, 0x94, 0x40, 0x13, 0x02, 0x00, 0x00, // lea edx, [eax+eax*2+0x213]
        0x8B, 0x45, 0xF8,                         // mov eax, [ebp-8] (player)
        0x8B, 0x88, 0x68, 0x0E, 0x00, 0x00,       // mov ecx, [eax+0xE68]
        0x0F, 0xB7, 0x04, 0x91,                   // movzx eax, word [ecx+edx*4]
        0xEB, 0x02,                               // jmp past
        0x33, 0xC0,                               // xor eax, eax
    };

    g_tradeSkillReady =
        Common::RedirectCall(Offsets::CALL_GAME_SHUTDOWN_TRADE_SKILL,
                             Offsets::FUN_SHUTDOWN_TRADE_SKILL,
                             reinterpret_cast<const void *>(&ShutdownTradeSkill)) &&
        Common::RedirectCall(Offsets::CALL_TRY_CAST_TOGGLE_TRADE_SKILL,
                             Offsets::FUN_TOGGLE_TRADE_SKILL,
                             reinterpret_cast<const void *>(&CastTradeSkillThunk)) &&
        Common::RedirectCall(Offsets::CALL_BUILD_TRADE_SKILL_SPELLS,
                             Offsets::FUN_PLAYER_FIND_SKILL_LINE_SPELLS,
                             reinterpret_cast<const void *>(&TradeSkillSpells)) &&
        Common::RedirectCall(Offsets::CALL_BUILD_TRADE_SKILL_ABILITY,
                             Offsets::FUN_SKILL_LINE_ABILITY_LOOKUP,
                             reinterpret_cast<const void *>(&TradeSkillAbility)) &&
        Common::ReplaceWithCall(Offsets::SITE_BUILD_TRADE_SKILL_RANK, kRankRead, sizeof kRankRead,
                                reinterpret_cast<const void *>(&TradeSkillRank));

    g_craftReady =
        Common::RedirectCall(Offsets::CALL_GAME_SHUTDOWN_CRAFT, Offsets::FUN_SHUTDOWN_CRAFT,
                             reinterpret_cast<const void *>(&ShutdownCraft)) &&
        Common::RedirectCall(Offsets::CALL_TRY_CAST_TOGGLE_CRAFT, Offsets::FUN_TOGGLE_CRAFT,
                             reinterpret_cast<const void *>(&CastCraftThunk)) &&
        Common::RedirectCall(Offsets::CALL_BUILD_CRAFT_SPELLS, Offsets::FUN_PLAYER_GET_TAB_SPELLS,
                             reinterpret_cast<const void *>(&CraftSpells)) &&
        Common::RedirectCall(Offsets::CALL_BUILD_CRAFT_ABILITY,
                             Offsets::FUN_UNIT_FIND_SKILL_LINE_ABILITY,
                             reinterpret_cast<const void *>(&CraftAbility)) &&
        Common::RedirectCall(Offsets::CALL_BUILD_CRAFT_RANK, Offsets::FUN_PLAYER_GET_SKILL_TOTAL,
                             reinterpret_cast<const void *>(&CraftRank));

    Game::Lua::RegisterGlobalFunction("IsTradeSkillLinked", &Script_IsTradeSkillLinked,
                                      &kIsTradeSkillLinked);
    Game::Lua::RegisterGlobalFunction("IsCraftLinked", &Script_IsCraftLinked, &kIsCraftLinked);
    Game::Lua::RegisterGlobalFunction("GetTradeSkillLine", &Script_GetTradeSkillLine);
    Game::Lua::RegisterGlobalFunction("GetCraftDisplaySkillLine", &Script_GetCraftDisplaySkillLine);
    Game::Lua::RegisterGlobalFunction("GetCraftName", &Script_GetCraftName);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

int TradeSkillSpell() { return TradeSkillOpen() != 0 ? g_tradeSkill.spellID : 0; }

int CraftSpell() { return CraftOpen() != 0 ? g_craft.spellID : 0; }

void ShowLinked(int spellID, uint64_t guid, const char *bits, int rank, int maxRank) {
    if (Unit::Identity::PlayerObject() == nullptr)
        return;
    const uint8_t *linker = Unit::Identity::PlayerInfoRecord(guid);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (linker == nullptr || record == nullptr ||
        Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_EFFECT) !=
            Offsets::SPELL_EFFECT_TRADE_SKILL)
        return;
    const uint32_t race = Game::Read<uint32_t>(linker, Offsets::OFF_PLAYER_INFO_RACE);
    const uint32_t classId = Game::Read<uint32_t>(linker, Offsets::OFF_PLAYER_INFO_CLASS);
    const uint8_t *ability = Ability(race, classId, static_cast<uint32_t>(spellID));
    if (ability == nullptr)
        return;
    const uint32_t skillLine = Game::Read<uint32_t>(ability, Offsets::OFF_SLA_SKILL_ID);
    if (DBC::Record(Offsets::VAR_SKILL_LINE_RECORDS, Offsets::VAR_SKILL_LINE_COUNT, skillLine) ==
        nullptr)
        return;
    const uint8_t *rows[Link::MAX_ABILITIES];
    const int count = Link::Abilities(skillLine, rows);
    if (count == 0 || static_cast<size_t>(Link::BitChars(count)) != std::strlen(bits))
        return;

    const uint32_t craftType =
        Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT_MISC_VALUE);
    if (!(craftType == 0 ? g_tradeSkillReady : g_craftReady))
        return;
    Shown &s = craftType == 0 ? g_tradeSkill : g_craft;
    s.spells.clear();
    for (int i = 0; i < count; ++i) {
        const int spell = Game::Read<int>(rows[i], Offsets::OFF_SLA_SPELL_ID);
        if (Link::IsSet(bits, i) && InPlayerList(spell, craftType))
            s.spells.push_back(spell);
    }
    const uint32_t n = static_cast<uint32_t>(s.spells.size());
    s.list = {n, n, s.spells.data(), 0};
    s.spellID = spellID;
    s.guid = guid;
    s.linked = true;
    s.opened = craftType == 0 ? skillLine : craftType;
    s.skillLine = skillLine;
    s.race = race;
    s.classId = classId;
    s.rank = rank;
    s.maxRank = maxRank;

    if (craftType == 0) {
        reinterpret_cast<Void_t>(Offsets::FUN_TRADE_SKILL_STOP_RECAST)();
        if (TradeSkillOpen() == skillLine)
            Game::Ref<uint32_t>(Offsets::VAR_CURRENT_TRADESKILL_LINE) = 0;
        reinterpret_cast<Toggle_t>(Offsets::FUN_TOGGLE_TRADE_SKILL)(skillLine);
    } else {
        if (CraftOpen() == craftType)
            Game::Ref<uint32_t>(Offsets::VAR_CRAFT_WINDOW_STATE) = 0;
        reinterpret_cast<Toggle_t>(Offsets::FUN_TOGGLE_CRAFT)(craftType);
    }
}

} // namespace TradeSkill::Window
