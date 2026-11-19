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

// Talent previews: points planned on the client and learned in one go.
//
// The plan is the client's alone. Each talent with planned points keeps a
// planned rank above the rank it has learned; the learned rank is always read
// live, by GetTalentInfo's own rule (0x004F3200): scan the rank slots from the
// last, and the first whose spell, or a later rank of it, is known
// (CGUnit_C::KnowsSpell / KnowsHigherRank) is the rank.
//
// AddPreviewTalentPoints applies 3.3.5's rules to the planned state
// (CGTalentGroup_C::ValidatePreviewTalentPoints, 0x005C73D0): free points
// left after the plan, the tier gate of five points per tier, prerequisites
// at their planned rank, a rank between the learned one and the last, and a
// removal that strands neither a planned talent that needs this one nor a
// higher tier. Every change that moves the planned total signals
// PREVIEW_TALENT_POINTS_CHANGED.
//
// LearnPreviewTalents sends 1.12's own learn request, CMSG_LEARN_TALENT
// (talent ID, 0-based rank, as LearnTalent 0x004F36A0 builds it), once per
// planned talent with the planned rank: 3.3.5 sends the same pairs in one
// CMSG_LEARN_PREVIEW_TALENTS. The server learns a rank several above the
// current one and checks each request against what it has learned so far,
// so they go in tab and tier order with a same-tier prerequisite first
// (eight Talent.dbc rows need one). The server answers a rejected request
// with nothing.
//
// 3.3.5 rebuilds its talent groups from every talent update packet, which
// drops the plan (CGTalentInfo::UpdateTalentData, 0x005C9E50): learning,
// a level, a wipe, the login. 1.12 has no such packet; the change in the
// player's free talent points is the same moment, and the engine signals it
// as CHARACTER_POINTS_CHANGED from one watcher (0x004C7000). The plan is
// dropped there, before the event, so a handler of the event sees the new
// state. A plan also belongs to the character it was made on.
//
// 1.12 has one talent group and no pet talents: `isPet` and `groupIndex` are
// read as in 3.3.5, and anything but the player's group 1 has no plan.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "event/Custom.h"
#include "net/PacketWriter.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Talent::Preview {

namespace {

constexpr uint32_t CMSG_LEARN_TALENT = 0x251;
// Points a tab needs per tier before the next one opens: 3.3.5's
// `2 * (pet == 0) + 3`, and the server's `Row * MAX_TALENT_RANK`.
constexpr int POINTS_PER_TIER = 5;
// Talent.dbc tiers run 0 to 6.
constexpr int MAX_TIERS = 16;

using Knows_t = bool(__thiscall *)(const void *unit, uint32_t spellID);
using PointsChanged_t = uint32_t(__stdcall *)(uint32_t a1, uint32_t a2, const int32_t *oldPoints,
                                              uint32_t a4);

// --- The player's talent tabs -------------------------------------------------

int TabCount() {
    return Game::Read<int>(Offsets::VAR_TALENT_TAB_COUNT);
}

// The 0-based tab's TabInfo, or null.
const uint8_t *Tab(int tab) {
    if (tab < 0 || tab >= TabCount())
        return nullptr;
    auto *tabs = Game::Read<const uint8_t *const *>(Offsets::VAR_TALENT_TAB_INFO_ARRAY);
    return tabs != nullptr ? tabs[tab] : nullptr;
}

int NumTalents(const uint8_t *tab) {
    return Game::Read<int>(tab, Offsets::OFF_TABINFO_NUM_TALENTS);
}

// The 0-based talent's row in a tab, or null.
const uint8_t *Row(const uint8_t *tab, int index) {
    if (tab == nullptr || index < 0 || index >= NumTalents(tab))
        return nullptr;
    auto *rows = Game::Read<const uint8_t *>(tab, Offsets::OFF_TABINFO_TALENT_ARRAY);
    return rows != nullptr ? rows + index * Offsets::TALENT_ENTRY_STRIDE : nullptr;
}

const uint8_t *RowByID(uint32_t talentID) {
    return DBC::Record(Offsets::VAR_TALENT_DBC_RECORDS, Offsets::VAR_TALENT_DBC_COUNT, talentID);
}

uint32_t Field(const uint8_t *row, uintptr_t offset) {
    return Game::Read<uint32_t>(row, offset);
}

uint32_t RankSpell(const uint8_t *row, int slot) {
    return Field(row, Offsets::OFF_TALENT_SPELL_RANK + slot * sizeof(uint32_t));
}

int Tier(const uint8_t *row) {
    return static_cast<int>(Field(row, Offsets::OFF_TALENT_TIER));
}

// The last rank slot with a spell, plus 1.
int MaxRank(const uint8_t *row) {
    for (int slot = Offsets::TALENT_MAX_RANKS - 1; slot >= 0; --slot) {
        if (RankSpell(row, slot) != 0)
            return slot + 1;
    }
    return 0;
}

bool Knows(const uint8_t *player, uint32_t spellID) {
    return reinterpret_cast<Knows_t>(Offsets::FUN_UNIT_KNOWS_SPELL)(player, spellID) ||
           reinterpret_cast<Knows_t>(Offsets::FUN_UNIT_KNOWS_HIGHER_RANK)(player, spellID);
}

// GetTalentInfo's rank: 0 when none is learned.
int LearnedRank(const uint8_t *player, const uint8_t *row) {
    for (int slot = Offsets::TALENT_MAX_RANKS - 1; slot >= 0; --slot) {
        const uint32_t spellID = RankSpell(row, slot);
        if (spellID != 0 && Knows(player, spellID))
            return slot + 1;
    }
    return 0;
}

// GetTalentInfo's 8th value: the talent's required spell, when it has one, is
// known.
bool MeetsRequiredSpell(const uint8_t *player, const uint8_t *row) {
    const uint32_t spellID = Field(row, Offsets::OFF_TALENT_REQUIRED_SPELL);
    return spellID == 0 || Knows(player, spellID);
}

int FreePoints(const uint8_t *player) {
    auto *info = Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
    return info != nullptr ? Game::Read<int>(info, Offsets::OFF_PLAYER_FIELD_CHARACTER_POINTS1) : 0;
}

// --- The plan -------------------------------------------------------------------

// Talent ID → planned rank, for the talents planned above their learned rank.
std::unordered_map<uint32_t, int> g_planned;
// The character the plan was made on.
uint64_t g_owner = 0;

// The local player, with the plan of another character dropped. Null out of
// the world.
const uint8_t *Player() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return nullptr;
    const uint64_t guid = Unit::Identity::PlayerGuid();
    if (guid != g_owner) {
        g_planned.clear();
        g_owner = guid;
    }
    return player;
}

// The rank the plan shows: the planned rank, else the learned one.
int PreviewRank(const uint8_t *player, const uint8_t *row) {
    const int learned = LearnedRank(player, row);
    const auto it = g_planned.find(Field(row, 0));
    return it != g_planned.end() && it->second > learned ? it->second : learned;
}

int TabPlanned(const uint8_t *player, const uint8_t *tab) {
    int points = 0;
    for (int i = 0, n = NumTalents(tab); i < n; ++i) {
        const uint8_t *row = Row(tab, i);
        if (row != nullptr && g_planned.count(Field(row, 0)) != 0)
            points += PreviewRank(player, row) - LearnedRank(player, row);
    }
    return points;
}

int GroupPlanned(const uint8_t *player) {
    int points = 0;
    for (int tab = 0, n = TabCount(); tab < n; ++tab) {
        if (const uint8_t *info = Tab(tab))
            points += TabPlanned(player, info);
    }
    return points;
}

void SetPlanned(const uint8_t *player, const uint8_t *row, int rank) {
    const uint32_t talentID = Field(row, 0);
    if (rank > LearnedRank(player, row))
        g_planned[talentID] = rank;
    else
        g_planned.erase(talentID);
}

// ValidatePreviewTalentPoints: whether `points` can be added to (or, when
// negative, taken from) the planned rank, and by how much the rank moves.
bool Validate(const uint8_t *player, int tabIndex, const uint8_t *row, int points, int *delta) {
    *delta = 0;
    if (points == 0)
        return true;
    const uint8_t *tab = Tab(tabIndex);
    if (tab == nullptr || row == nullptr)
        return false;

    if (FreePoints(player) - GroupPlanned(player) <= 0 && points > 0)
        return false;

    const int tier = Tier(row);
    const int tabSpent =
        Game::Read<int>(tab, Offsets::OFF_TABINFO_POINTS_SPENT) + TabPlanned(player, tab);
    if (tabSpent < tier * POINTS_PER_TIER)
        return false;

    for (int i = 0; i < Offsets::TALENT_MAX_PREREQS; ++i) {
        const uint32_t prereqID = Field(row, Offsets::OFF_TALENT_PREREQ_TALENT + i * 4);
        if (prereqID == 0)
            continue;
        const uint8_t *prereq = RowByID(prereqID);
        const int needed = static_cast<int>(Field(row, Offsets::OFF_TALENT_PREREQ_RANK + i * 4));
        if (prereq != nullptr && PreviewRank(player, prereq) <= needed)
            return false;
    }

    const int rank = LearnedRank(player, row);
    const int preview = PreviewRank(player, row);
    const int room = MaxRank(row) - rank;
    const int wanted = preview + points - rank;
    const int newRank = rank + (wanted < 0 ? 0 : wanted > room ? room : wanted);
    const int change = newRank - preview;
    *delta = change;
    if (change == 0)
        return false;
    if (change > 0)
        return true;

    // A planned talent that needs this one at its present rank.
    const uint32_t talentID = Field(row, 0);
    for (int t = 0, tabs = TabCount(); t < tabs; ++t) {
        const uint8_t *info = Tab(t);
        for (int i = 0, n = info != nullptr ? NumTalents(info) : 0; i < n; ++i) {
            const uint8_t *other = Row(info, i);
            if (other == nullptr || PreviewRank(player, other) <= 0)
                continue;
            for (int s = 0; s < Offsets::TALENT_MAX_PREREQS; ++s) {
                if (Field(other, Offsets::OFF_TALENT_PREREQ_TALENT + s * 4) == talentID &&
                    preview + change <=
                        static_cast<int>(Field(other, Offsets::OFF_TALENT_PREREQ_RANK + s * 4)))
                    return false;
            }
        }
    }

    // A higher tier with points that would no longer have enough below it.
    int tierSpent[MAX_TIERS] = {};
    for (int i = 0, n = NumTalents(tab); i < n; ++i) {
        const uint8_t *other = Row(tab, i);
        const int t = other != nullptr ? Tier(other) : -1;
        if (t >= 0 && t < MAX_TIERS)
            tierSpent[t] += PreviewRank(player, other);
    }
    int total = 0;
    for (int t = 0; t < MAX_TIERS; ++t) {
        total += tierSpent[t] + (t == tier ? change : 0);
        if (tierSpent[t] > 0 && t > tier && total - tierSpent[t] < POINTS_PER_TIER * t)
            return false;
    }
    return true;
}

// --- PREVIEW_TALENT_POINTS_CHANGED ---------------------------------------------

const Game::Doc::Field kEventPayload[] = {
    Game::Doc::Req("talentIndex", "number", "0 when a whole tab or the whole plan was cleared."),
    Game::Doc::Req("tabIndex", "number", "0 when the whole plan was cleared."),
    Game::Doc::Req("groupIndex", "number"),
    Game::Doc::Req("points", "number", "The change in the planned points, negative for a removal."),
};
const Game::Doc::Event kEventDoc{
    "PaperDollInfo",
    "Fires when the points planned in the talent preview change.",
    kEventPayload,
};
const Event::Custom::AutoReserve kEvent{"PREVIEW_TALENT_POINTS_CHANGED", &kEventDoc};

void FireChanged(int talentIndex, int tabIndex, int before, int after) {
    if (after != before)
        Event::Custom::Fire(kEvent.Slot(), "%d%d%d%d", talentIndex, tabIndex, 1, after - before);
}

// --- Plan dropped on a talent update -----------------------------------------------

PointsChanged_t g_origPointsChanged = nullptr;

uint32_t __stdcall PointsChanged_h(uint32_t a1, uint32_t a2, const int32_t *oldPoints,
                                   uint32_t a4) {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player != nullptr && oldPoints != nullptr && FreePoints(player) != oldPoints[0])
        g_planned.clear();
    return g_origPointsChanged(a1, a2, oldPoints, a4);
}

const Game::HookAutoRegister _pointsHook{Offsets::FUN_ON_CHARACTER_POINTS_CHANGED,
                                         reinterpret_cast<void *>(&PointsChanged_h),
                                         reinterpret_cast<void **>(&g_origPointsChanged)};

// --- Lua -----------------------------------------------------------------------------

// 3.3.5's group argument (TalentInfoGetScriptGroupIndex): a number names a
// group, anything else is the active one. Only group 1 exists.
bool IsPlayerGroup(void *L, int isPetIndex, int groupIndex) {
    if (Game::Lua::ToFlag(L, isPetIndex))
        return false;
    return !Game::Lua::IsNumber(L, groupIndex) ||
           static_cast<int>(Game::Lua::ToNumber(L, groupIndex)) == 1;
}

int Index0(void *L, int index) {
    return static_cast<int>(Game::Lua::ToNumber(L, index)) - 1;
}

int __fastcall Script_AddPreviewTalentPoints(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: AddPreviewTalentPoints(tabIndex, talentIndex, points[, isPet[, "
                            "groupIndex]])");
        return 0;
    }
    const int tab = Index0(L, 1);
    const int talent = Index0(L, 2);
    const int points = static_cast<int>(Game::Lua::ToNumber(L, 3));
    const uint8_t *player = Player();
    if (player == nullptr || !IsPlayerGroup(L, 4, 5))
        return 0;
    const uint8_t *row = Row(Tab(tab), talent);
    if (row == nullptr)
        return 0;

    const int before = GroupPlanned(player);
    int delta = 0;
    if (Validate(player, tab, row, points, &delta) && delta != 0)
        SetPlanned(player, row, PreviewRank(player, row) + delta);
    FireChanged(talent + 1, tab + 1, before, GroupPlanned(player));
    return 0;
}

int __fastcall Script_ResetPreviewTalentPoints(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: ResetPreviewTalentPoints(tabIndex[, isPet[, groupIndex]])");
        return 0;
    }
    const int tab = Index0(L, 1);
    const uint8_t *player = Player();
    const uint8_t *info = Tab(tab);
    if (player == nullptr || info == nullptr || !IsPlayerGroup(L, 2, 3))
        return 0;

    const int before = GroupPlanned(player);
    for (int i = 0, n = NumTalents(info); i < n; ++i) {
        if (const uint8_t *row = Row(info, i))
            g_planned.erase(Field(row, 0));
    }
    FireChanged(0, tab + 1, before, GroupPlanned(player));
    return 0;
}

int __fastcall Script_ResetGroupPreviewTalentPoints(void *L) {
    const uint8_t *player = Player();
    if (player == nullptr || !IsPlayerGroup(L, 1, 2))
        return 0;
    const int before = GroupPlanned(player);
    g_planned.clear();
    FireChanged(0, 0, before, 0);
    return 0;
}

int __fastcall Script_GetPreviewTalentPointsSpent(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetPreviewTalentPointsSpent(tabIndex[, isPet[, groupIndex]])");
        return 0;
    }
    const uint8_t *player = Player();
    const uint8_t *info = Tab(Index0(L, 1));
    const bool planned = player != nullptr && info != nullptr && IsPlayerGroup(L, 2, 3);
    Game::Lua::PushNumber(L, planned ? TabPlanned(player, info) : 0);
    return 1;
}

int __fastcall Script_GetGroupPreviewTalentPointsSpent(void *L) {
    const uint8_t *player = Player();
    const bool planned = player != nullptr && IsPlayerGroup(L, 1, 2);
    Game::Lua::PushNumber(L, planned ? GroupPlanned(player) : 0);
    return 1;
}

void SendLearn(uint32_t talentID, int rank) {
    Net::OutPacket packet(CMSG_LEARN_TALENT);
    packet.PutU32(talentID);
    packet.PutU32(static_cast<uint32_t>(rank - 1));
    packet.Send();
}

int __fastcall Script_LearnPreviewTalents(void *L) {
    const uint8_t *player = Player();
    if (player == nullptr || Game::Lua::ToFlag(L, 1) || GroupPlanned(player) <= 0)
        return 0;

    struct Pending {
        const uint8_t *row;
        int rank;
        bool sent;
    };
    for (int tab = 0, tabs = TabCount(); tab < tabs; ++tab) {
        const uint8_t *info = Tab(tab);
        if (info == nullptr)
            continue;
        std::vector<Pending> pending;
        for (int i = 0, n = NumTalents(info); i < n; ++i) {
            const uint8_t *row = Row(info, i);
            if (row == nullptr)
                continue;
            const int rank = PreviewRank(player, row);
            if (rank > LearnedRank(player, row))
                pending.push_back({row, rank, false});
        }
        // An unsent request in this tab for one of the row's prerequisites.
        const auto waits = [&pending](const uint8_t *row) {
            for (int s = 0; s < Offsets::TALENT_MAX_PREREQS; ++s) {
                const uint32_t prereqID = Field(row, Offsets::OFF_TALENT_PREREQ_TALENT + s * 4);
                for (const Pending &p : pending) {
                    if (prereqID != 0 && !p.sent && Field(p.row, 0) == prereqID)
                        return true;
                }
            }
            return false;
        };
        for (int tier = 0; tier < MAX_TIERS; ++tier) {
            for (bool progress = true; progress;) {
                progress = false;
                for (Pending &p : pending) {
                    if (p.sent || Tier(p.row) != tier || waits(p.row))
                        continue;
                    SendLearn(Field(p.row, 0), p.rank);
                    p.sent = true;
                    progress = true;
                }
            }
        }
    }
    return 0;
}

// The talent a TalentInfoQuery names: its specializationIndex with a
// talentIndex, or with a tier and column.
const uint8_t *QueryRow(void *L) {
    const int tab = static_cast<int>(Game::Lua::GetFieldNumber(L, 1, "specializationIndex")) - 1;
    const uint8_t *info = Tab(tab);
    if (info == nullptr)
        return nullptr;
    const int talentIndex = static_cast<int>(Game::Lua::GetFieldNumber(L, 1, "talentIndex"));
    if (talentIndex > 0)
        return Row(info, talentIndex - 1);
    const int tier = static_cast<int>(Game::Lua::GetFieldNumber(L, 1, "tier")) - 1;
    const int column = static_cast<int>(Game::Lua::GetFieldNumber(L, 1, "column")) - 1;
    for (int i = 0, n = NumTalents(info); i < n; ++i) {
        const uint8_t *row = Row(info, i);
        if (row != nullptr && Tier(row) == tier &&
            static_cast<int>(Field(row, Offsets::OFF_TALENT_COLUMN)) == column)
            return row;
    }
    return nullptr;
}

bool FieldFlag(void *L, int index, const char *key) {
    Game::Lua::PushString(L, key);
    Game::Lua::GetTable(L, index);
    const bool on = Game::Lua::ToFlag(L, -1);
    Game::Lua::SetTop(L, -2);
    return on;
}

int __fastcall Script_C_GetTalentInfo(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: local result = C_SpecializationInfo.GetTalentInfo(query)");
        return 0;
    }
    if (FieldFlag(L, 1, "isInspect") || FieldFlag(L, 1, "isPet"))
        return 0;
    const double group = Game::Lua::GetFieldNumber(L, 1, "groupIndex", 1.0);
    const uint8_t *player = Player();
    const uint8_t *row = player != nullptr && group == 1.0 ? QueryRow(L) : nullptr;
    if (row == nullptr)
        return 0;

    // GetTalentInfo names the talent after its learned rank's spell, else
    // the first rank's.
    const int rank = LearnedRank(player, row);
    const uint8_t *spell = Spell::Lookup::RecordForID(
        static_cast<int>(RankSpell(row, rank > 0 ? rank - 1 : 0)));
    if (spell == nullptr)
        return 0;

    const bool meetsPrereq = MeetsRequiredSpell(player, row);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "talentID", Field(row, 0));
    Game::Lua::SetFieldString(L, "name", Spell::Lookup::Name(spell));
    if (const char *icon = Spell::Lookup::IconPath(spell, false))
        Game::Lua::SetFieldString(L, "icon", icon);
    Game::Lua::SetFieldNumber(L, "tier", Tier(row) + 1);
    Game::Lua::SetFieldNumber(L, "column", Field(row, Offsets::OFF_TALENT_COLUMN) + 1);
    Game::Lua::SetFieldNumber(L, "rank", rank);
    Game::Lua::SetFieldNumber(L, "maxRank", MaxRank(row));
    Game::Lua::SetFieldBool(L, "meetsPrereq", meetsPrereq);
    Game::Lua::SetFieldNumber(L, "previewRank", PreviewRank(player, row));
    Game::Lua::SetFieldBool(L, "meetsPreviewPrereq", meetsPrereq);
    Game::Lua::SetFieldBool(L, "isExceptional",
                            (Field(row, Offsets::OFF_TALENT_FLAGS) &
                             Offsets::TALENT_FLAG_EXCEPTIONAL) != 0);
    return 1;
}

// 3.3.5's GetTalentPrereqs (0x005C7ED0): four values for each prerequisite,
// the tier and column of the talent needed, then 1 or nil for whether its
// learned and its planned rank reach the rank needed. 1.12's own
// GetTalentPrereqs returns the first three, so the planned answer has a name
// of its own. The learned test is 1.12's (0x004F34D0): the needed rank or a
// later one is known. Nothing is known of another group, a pet or an inspected
// player, as in 3.3.5 for a group without the talent.
int __fastcall Script_C_GetTalentPrereqs(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: C_SpecializationInfo.GetTalentPrereqs(tabIndex, talentIndex[, "
                            "isInspect[, isPet[, groupIndex]]])");
        return 0;
    }
    const uint8_t *row = Row(Tab(Index0(L, 1)), Index0(L, 2));
    if (row == nullptr)
        return 0;
    const uint8_t *player = Player();
    const bool known = player != nullptr && !Game::Lua::ToFlag(L, 3) && IsPlayerGroup(L, 4, 5);

    int count = 0;
    for (int i = 0; i < Offsets::TALENT_MAX_PREREQS; ++i) {
        const uint8_t *prereq = RowByID(Field(row, Offsets::OFF_TALENT_PREREQ_TALENT + i * 4));
        if (prereq == nullptr)
            continue;
        const int needed = static_cast<int>(Field(row, Offsets::OFF_TALENT_PREREQ_RANK + i * 4));
        Game::Lua::CheckStack(L, 4);
        Game::Lua::PushNumber(L, Tier(prereq) + 1);
        Game::Lua::PushNumber(L, Field(prereq, Offsets::OFF_TALENT_COLUMN) + 1);
        Game::Lua::PushFlag(L, known && LearnedRank(player, prereq) > needed);
        Game::Lua::PushFlag(L, known && PreviewRank(player, prereq) > needed);
        ++count;
    }
    return 4 * count;
}

// --- Documentation ---------------------------------------------------------------

const Game::Doc::Field kAddArgs[] = {
    Game::Doc::Req("tabIndex", "luaIndex"),
    Game::Doc::Req("talentIndex", "luaIndex"),
    Game::Doc::Req("points", "number", "Negative to remove planned points."),
    Game::Doc::Opt("isPet", "bool", "false"),
    Game::Doc::Opt("groupIndex", "luaIndex"),
};
const Game::Doc::Function kAddDoc{"Plans points in a talent, when the talent rules allow them.",
                                  kAddArgs, {}, "TalentGlobals"};

const Game::Doc::Field kTabArgs[] = {
    Game::Doc::Req("tabIndex", "luaIndex"),
    Game::Doc::Opt("isPet", "bool", "false"),
    Game::Doc::Opt("groupIndex", "luaIndex"),
};
const Game::Doc::Function kResetDoc{"Clears the points planned in a talent tab.", kTabArgs, {},
                                    "TalentGlobals"};

const Game::Doc::Field kGroupArgs[] = {
    Game::Doc::Opt("isPet", "bool", "false"),
    Game::Doc::Opt("groupIndex", "luaIndex"),
};
const Game::Doc::Function kResetGroupDoc{"Clears every planned talent point.", kGroupArgs, {},
                                         "TalentGlobals"};

const Game::Doc::Field kPointsRets[] = {
    Game::Doc::Req("points", "number"),
};
const Game::Doc::Function kTabSpentDoc{"Returns the points planned in a talent tab.", kTabArgs,
                                       kPointsRets, "TalentGlobals"};
const Game::Doc::Function kGroupSpentDoc{"Returns the points planned in all talent tabs.",
                                         kGroupArgs, kPointsRets, "TalentGlobals"};

const Game::Doc::Field kLearnArgs[] = {
    Game::Doc::Opt("isPet", "bool", "false"),
};
const Game::Doc::Function kLearnDoc{"Asks the server to learn the planned talent points.",
                                    kLearnArgs, {}, "TalentGlobals"};

const Game::Doc::Field kQueryFields[] = {
    Game::Doc::Opt("specializationIndex", "luaIndex"),
    Game::Doc::Opt("talentIndex", "luaIndex"),
    Game::Doc::Opt("tier", "luaIndex", nullptr, "With column, names the talent in place of talentIndex."),
    Game::Doc::Opt("column", "luaIndex"),
    Game::Doc::Opt("isInspect", "bool", "false"),
    Game::Doc::Opt("isPet", "bool", "false"),
    Game::Doc::Opt("groupIndex", "luaIndex"),
};
const Game::Doc::Structure kQuery{"TalentInfoQuery", "SpecializationInfo", kQueryFields};

const Game::Doc::Field kResultFields[] = {
    Game::Doc::Req("talentID", "number"),
    Game::Doc::Req("name", "string"),
    Game::Doc::Opt("icon", "string", nullptr, "The icon's texture path."),
    Game::Doc::Req("tier", "luaIndex"),
    Game::Doc::Req("column", "luaIndex"),
    Game::Doc::Req("rank", "number"),
    Game::Doc::Req("maxRank", "number"),
    Game::Doc::Req("meetsPrereq", "bool"),
    Game::Doc::Req("previewRank", "number", "The planned rank, else the learned one."),
    Game::Doc::Req("meetsPreviewPrereq", "bool"),
    Game::Doc::Req("isExceptional", "bool"),
};
const Game::Doc::Structure kResult{"TalentInfoResult", "SpecializationInfo", kResultFields};

const Game::Doc::Field kInfoArgs[] = {
    Game::Doc::Req("query", "TalentInfoQuery"),
};
const Game::Doc::Field kInfoRets[] = {
    Game::Doc::Opt("result", "TalentInfoResult"),
};
const Game::Doc::Function kInfoDoc{"Returns a talent's ranks, learned and planned.", kInfoArgs,
                                   kInfoRets};

const Game::Doc::Field kPrereqArgs[] = {
    Game::Doc::Req("tabIndex", "luaIndex"),
    Game::Doc::Req("talentIndex", "luaIndex"),
    Game::Doc::Opt("isInspect", "bool", "false"),
    Game::Doc::Opt("isPet", "bool", "false"),
    Game::Doc::Opt("groupIndex", "luaIndex"),
};
const Game::Doc::Field kPrereqRets[] = {
    Game::Doc::Req("tier", "luaIndex", "The tier of a talent this one needs."),
    Game::Doc::Req("column", "luaIndex"),
    Game::Doc::Opt("isLearnable", "number", nullptr,
                   "1 when that talent's learned rank is enough, else nil."),
    Game::Doc::Opt("isPreviewLearnable", "number", nullptr,
                   "1 when that talent's planned rank is enough, else nil."),
    Game::Doc::Vararg("number", "The same four values for each further talent this one needs."),
};
const Game::Doc::Function kPrereqDoc{
    "Returns the talents a talent needs, and whether their learned and planned ranks are enough.",
    kPrereqArgs, kPrereqRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("AddPreviewTalentPoints", &Script_AddPreviewTalentPoints,
                                      &kAddDoc);
    Game::Lua::RegisterGlobalFunction("ResetPreviewTalentPoints", &Script_ResetPreviewTalentPoints,
                                      &kResetDoc);
    Game::Lua::RegisterGlobalFunction("ResetGroupPreviewTalentPoints",
                                      &Script_ResetGroupPreviewTalentPoints, &kResetGroupDoc);
    Game::Lua::RegisterGlobalFunction("GetPreviewTalentPointsSpent",
                                      &Script_GetPreviewTalentPointsSpent, &kTabSpentDoc);
    Game::Lua::RegisterGlobalFunction("GetGroupPreviewTalentPointsSpent",
                                      &Script_GetGroupPreviewTalentPointsSpent, &kGroupSpentDoc);
    Game::Lua::RegisterGlobalFunction("LearnPreviewTalents", &Script_LearnPreviewTalents,
                                      &kLearnDoc);
    Game::Lua::RegisterTableFunction("C_SpecializationInfo", "GetTalentInfo",
                                     &Script_C_GetTalentInfo, &kInfoDoc);
    Game::Lua::RegisterTableFunction("C_SpecializationInfo", "GetTalentPrereqs",
                                     &Script_C_GetTalentPrereqs, &kPrereqDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Talent::Preview
