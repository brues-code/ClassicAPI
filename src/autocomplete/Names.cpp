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

// C_AutoComplete: the player names an edit box can complete.
//
// 4.3.4's CGAutoCompleteName keeps its own list of names, each with flag bits
// that the party, raid, guild, friend list and chat code add and remove as
// those lists change. 1.12 has no such list, but it keeps every list the bits
// come from, so a query reads them where the engine keeps them:
//   - party: the four VAR_PARTY_GUIDS, the name from the name cache, online
//     from the member stats block (Group::MemberStats) - InGroup;
//   - raid: the VAR_RAID_MEMBER_PTRS slots and their online bit - InGroup;
//   - guild roster: each GuildMemberInfo's GUID, name and online word, read
//     only while the player is in a guild, the test Script_IsInGuild
//     (0x00516DE0) makes, because the engine keeps the old roster after a
//     /gquit - InGuild;
//   - friends list: the social singleton's slots (FriendList.h) - Friend.
// The one list the engine does not keep is ours: the players this character
// whispered or was whispered by (Chat::Dispatch reports each line), in the
// order they were last heard from - InteractedWith. Logging out clears it,
// as 4.3.4's ShutdownGame clears its list.
//
// A player on several lists is one entry with every list's bit. A list that
// holds the player decides Online; a player known only from a whisper is
// Online, as 4.3.4 sets Online together with InteractedWith.
//
// Matching is 4.3.4's NodeMatchesParams (the same in 3.3.5's binary,
// 0x0057ACA0), with two differences:
//   - a cursorPosition outside 1..length means the end of the text. Retail's
//     tell-target extraction (ChatFrameEditBox.lua) passes 0 and expects a
//     whole-text match;
//   - the edit-from-center search also tries the last place. The engine's
//     loop stops one place early, so it never finds the text after the
//     cursor at the end of a name.
// Order is 3.3.5's SortNameNode (0x0057B130): with
// autoCompleteResortNamesOnRecency on, the most recent whisper first, then by
// name. Retail's client cannot be read, so each result's priority is the
// highest Enum.AutoCompletePriority its bits reach, in the enum's own order:
// Friend, Guild, InGroup, Interacted, else Other.

#include "autocomplete/Names.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "friendlist/FriendList.h"
#include "group/MemberStats.h"
#include "guid/Guid.h"
#include "text/Utf8.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace AutoComplete::Names {

namespace {

// Enum.AutoCompleteEntryFlag. 1.12 has nothing to fill Bnet, InAOI,
// AccountCharacter or RecentPlayer from.
enum EntryFlag : uint32_t {
    FLAG_IN_GROUP = 0x1,
    FLAG_IN_GUILD = 0x2,
    FLAG_FRIEND = 0x4,
    FLAG_BNET = 0x8,
    FLAG_INTERACTED_WITH = 0x10,
    FLAG_ONLINE = 0x20,
    FLAG_IN_AOI = 0x40,
    FLAG_ACCOUNT_CHARACTER = 0x80,
    FLAG_RECENT_PLAYER = 0x100,
};

// Enum.AutoCompletePriority.
enum Priority {
    PRIORITY_OTHER = 0,
    PRIORITY_INTERACTED = 1,
    PRIORITY_IN_GROUP = 2,
    PRIORITY_GUILD = 3,
    PRIORITY_FRIEND = 4,
    PRIORITY_ACCOUNT_CHARACTER = 5,
    PRIORITY_ACCOUNT_CHARACTER_SAME_REALM = 6,
};

constexpr int CHAT_MSG_WHISPER = 6;
constexpr int CHAT_MSG_WHISPER_INFORM = 7;
constexpr int LANGUAGE_ADDON = -1; // the dispatch's addon-message arm

CVar::Factory::Handle g_resortOnRecency = nullptr;
CVar::Factory::Handle g_editFromCenter = nullptr;
CVar::Factory::Handle g_useContext = nullptr;

bool CVarOn(CVar::Factory::Handle cvar) { return CVar::Factory::GetInt(cvar, 1) != 0; }

// A whisper partner. `order` grows with every whisper, so a larger one was
// heard from more recently.
struct Interaction {
    uint64_t guid;
    std::string name;
    uint32_t order;
};

std::vector<Interaction> g_interactions;
uint32_t g_lastOrder = 0;

// One player as one list knows them, or, after Merge, as all lists do.
struct Entry {
    uint64_t guid;
    const char *name; // the list's own storage, valid until Lua runs
    uint32_t flags;   // EntryFlag bits, Online excluded
    bool listed;      // a group, guild or friends list holds the player
    bool online;      // what those lists say
    uint32_t order;   // the Interaction's order, 0 for none
};

bool InGuild() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return false;
    const auto *info = Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
    return info != nullptr && Game::Read<uint32_t>(info, Offsets::OFF_PLAYER_INFO_GUILD_KEY) != 0;
}

// Every player whose name `match` accepts, one entry per list that has them.
template <typename Match>
std::vector<Entry> Collect(Match match) {
    std::vector<Entry> found;
    const uint64_t self = Unit::Identity::PlayerGuid();
    const auto add = [&](uint64_t guid, const char *name, uint32_t flag, bool listed, bool online,
                         uint32_t order) {
        if (guid == 0 || guid == self || name == nullptr || *name == '\0' || !match(name))
            return;
        found.push_back({guid, name, flag, listed, online, order});
    };

    const auto *party =
        reinterpret_cast<const uint64_t *>(static_cast<uintptr_t>(Offsets::VAR_PARTY_GUIDS));
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i) {
        const uint64_t guid = party[i];
        if (guid != 0)
            add(guid, Unit::Identity::NameForGuid(guid), FLAG_IN_GROUP, true,
                Group::MemberStats::Lookup(guid).online, 0);
    }

    int raidCount = Game::Read<int>(Offsets::VAR_RAID_MEMBER_COUNT);
    if (raidCount > Offsets::RAID_MAX_SLOTS)
        raidCount = Offsets::RAID_MAX_SLOTS;
    const auto *slots = reinterpret_cast<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_RAID_MEMBER_PTRS));
    for (int i = 0; i < raidCount; ++i) {
        if (slots[i] == nullptr)
            continue;
        const uint64_t guid = Game::Read<uint64_t>(slots[i], 0);
        const uint32_t flags = Game::Read<uint32_t>(slots[i], Offsets::OFF_RAID_SLOT_FLAGS);
        add(guid, Unit::Identity::NameForGuid(guid), FLAG_IN_GROUP, true,
            (flags & Offsets::RAID_SLOT_FLAG_ONLINE) != 0, 0);
    }

    if (InGuild()) {
        auto **roster = Game::Read<const uint8_t **>(Offsets::VAR_GUILD_ROSTER_PTR);
        const int total = Game::Read<int>(Offsets::VAR_GUILD_ROSTER_TOTAL_COUNT);
        for (int i = 0; roster != nullptr && i < total; ++i) {
            const uint8_t *member = roster[i];
            if (member == nullptr)
                continue;
            add(Game::Read<uint64_t>(member, Offsets::OFF_GUILD_MEMBER_GUID),
                reinterpret_cast<const char *>(member + Offsets::OFF_GUILD_MEMBER_NAME),
                FLAG_IN_GUILD, true,
                Game::Read<uint32_t>(member, Offsets::OFF_GUILD_MEMBER_ONLINE) != 0, 0);
        }
    }

    const int friends = FriendList::Count();
    for (int i = 0; i < friends; ++i) {
        const uint8_t *entry = FriendList::EntryByIndex(i);
        if (entry == nullptr)
            break;
        add(Game::Read<uint64_t>(entry, FriendList::OFF_FRIEND_GUID),
            Game::Read<const char *>(entry, FriendList::OFF_FRIEND_NAME), FLAG_FRIEND, true,
            Game::Read<uint8_t>(entry, FriendList::OFF_FRIEND_CONNECTED) != 0, 0);
    }

    for (const Interaction &i : g_interactions)
        add(i.guid, i.name.c_str(), FLAG_INTERACTED_WITH, false, false, i.order);

    return found;
}

// One entry per player, with every list's bits and Online decided; then the
// include / exclude test, which autoCompleteUseContext turns off.
std::vector<Entry> Merge(std::vector<Entry> found, uint32_t include, uint32_t exclude) {
    std::sort(found.begin(), found.end(),
              [](const Entry &a, const Entry &b) { return a.guid < b.guid; });
    std::vector<Entry> merged;
    for (const Entry &e : found) {
        if (!merged.empty() && merged.back().guid == e.guid) {
            Entry &m = merged.back();
            m.flags |= e.flags;
            m.listed = m.listed || e.listed;
            m.online = m.online || e.online;
            m.order = std::max(m.order, e.order);
        } else {
            merged.push_back(e);
        }
    }

    const bool useContext = CVarOn(g_useContext);
    std::vector<Entry> kept;
    for (Entry &e : merged) {
        if (e.listed ? e.online : (e.flags & FLAG_INTERACTED_WITH) != 0)
            e.flags |= FLAG_ONLINE;
        if (!useContext || ((e.flags & include) != 0 && (e.flags & exclude) == 0))
            kept.push_back(e);
    }
    return kept;
}

int PriorityOf(uint32_t flags) {
    if (flags & FLAG_FRIEND)
        return PRIORITY_FRIEND;
    if (flags & FLAG_IN_GUILD)
        return PRIORITY_GUILD;
    if (flags & FLAG_IN_GROUP)
        return PRIORITY_IN_GROUP;
    if (flags & FLAG_INTERACTED_WITH)
        return PRIORITY_INTERACTED;
    return PRIORITY_OTHER;
}

// Past `chars` code points of `s`, continuation bytes included, stopping at
// the terminator. (Storm's SStrAdvanceCharsUTF8 stops one byte into a
// multi-byte character.)
const char *Advance(const char *s, size_t chars) {
    for (; *s != '\0' && chars != 0; --chars) {
        ++s;
        while ((static_cast<uint8_t>(*s) & 0xC0) == 0x80)
            ++s;
    }
    return s;
}

// What the edit box holds: the text and the cursor in it, in code points.
struct Typed {
    const char *text;
    size_t length;
    size_t cursor;
    bool allowFullMatch;
    bool editFromCenter;
};

bool Completes(const Typed &t, const char *name) {
    if (!t.allowFullMatch && Text::Utf8::CompareI(t.text, name) == 0)
        return false;
    if (!t.editFromCenter || t.cursor == t.length)
        return Text::Utf8::CompareI(t.text, name, t.length) == 0;

    // The text before the cursor starts the name, and the text after it
    // follows somewhere later in the name.
    if (Text::Utf8::CompareI(t.text, name, t.cursor) != 0)
        return false;
    const size_t nameLength = Text::Utf8::Length(name);
    if (nameLength < t.length)
        return false;
    const char *tail = Advance(t.text, t.cursor);
    const size_t tailLength = t.length - t.cursor;
    const char *at = Advance(name, t.cursor);
    for (size_t places = nameLength - t.length + 1; places != 0; --places) {
        if (Text::Utf8::CompareI(at, tail, tailLength) == 0)
            return true;
        at = Advance(at, 1);
    }
    return false;
}

// A flag mask from Lua: bit.bor results above 0x7FFFFFFF arrive negative.
uint32_t FlagArg(void *L, int index) {
    return static_cast<uint32_t>(static_cast<int64_t>(Game::Lua::ToNumber(L, index)));
}

// C_AutoComplete.GetAutoCompleteResults(name, numResults, cursorPosition,
//     allowFullMatch, includeFlags, excludeFlags) -> results
// A numResults of 0 or less returns every match, as the engine's count test
// (`++count == numReturns`) does.
int __fastcall Script_GetAutoCompleteResults(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3) ||
        !Game::Lua::IsNumber(L, 5) || !Game::Lua::IsNumber(L, 6)) {
        Game::Lua::Error(L, "Usage: C_AutoComplete.GetAutoCompleteResults(name, numResults, "
                            "cursorPosition, allowFullMatch, includeFlags, excludeFlags)");
        return 0;
    }
    Typed typed;
    typed.text = Game::Lua::ToString(L, 1);
    typed.length = Text::Utf8::Length(typed.text);
    const double cursor = Game::Lua::ToNumber(L, 3);
    typed.cursor = cursor >= 1 && cursor <= static_cast<double>(typed.length)
                       ? static_cast<size_t>(cursor)
                       : typed.length;
    typed.allowFullMatch = Game::Lua::ToBoolean(L, 4) != 0;
    typed.editFromCenter = CVarOn(g_editFromCenter);
    const int numResults = static_cast<int>(Game::Lua::ToNumber(L, 2));

    std::vector<Entry> matches =
        Merge(Collect([&](const char *name) { return Completes(typed, name); }), FlagArg(L, 5),
              FlagArg(L, 6));
    const bool recency = CVarOn(g_resortOnRecency);
    std::sort(matches.begin(), matches.end(), [recency](const Entry &a, const Entry &b) {
        if (recency && a.order != b.order)
            return a.order > b.order;
        return Text::Utf8::CompareI(a.name, b.name) < 0;
    });
    if (numResults > 0 && matches.size() > static_cast<size_t>(numResults))
        matches.resize(numResults);

    Game::Lua::NewTable(L);
    for (size_t i = 0; i < matches.size(); ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(L, "name", matches[i].name);
        Game::Lua::SetFieldNumber(L, "priority", PriorityOf(matches[i].flags));
        Game::Lua::SetFieldNumber(L, "bnetID", 0);
        Game::Lua::RawSet(L, -3);
    }
    return 1;
}

// C_AutoComplete.IsRecognizedName(name, includeFlags, excludeFlags) -> bool
int __fastcall Script_IsRecognizedName(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L,
                         "Usage: C_AutoComplete.IsRecognizedName(name, includeFlags, excludeFlags)");
        return 0;
    }
    const char *text = Game::Lua::ToString(L, 1);
    const std::vector<Entry> matches =
        Merge(Collect([text](const char *name) { return Text::Utf8::CompareI(text, name) == 0; }),
              FlagArg(L, 2), FlagArg(L, 3));
    Game::Lua::PushBool(L, !matches.empty());
    return 1;
}

void ClearOnLogout() {
    g_interactions.clear();
    g_lastOrder = 0;
}

const Game::GlueModuleAutoRegister _clearOnLogout{&ClearOnLogout};

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kResultFields[] = {
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("priority", "AutoCompletePriority",
                   "The closest tie to the player: friend, guild, group, whisper, or none."),
    Game::Doc::Req("bnetID", "number", "Always 0."),
};
const Game::Doc::Structure kResult{"AutoCompleteResult", "AutoComplete", kResultFields};

const Game::Doc::Field kResultsArgs[] = {
    Game::Doc::Req("name", "string", "The text typed so far."),
    Game::Doc::Req("numResults", "number", "The most results to return; 0 returns all."),
    Game::Doc::Req("cursorPosition", "number", "The cursor's place in the text, in characters."),
    Game::Doc::Req("allowFullMatch", "bool", "Whether a name equal to the text can be a result."),
    Game::Doc::Req("includeFlags", "number",
                   "Enum.AutoCompleteEntryFlag bits; a result has at least one."),
    Game::Doc::Req("excludeFlags", "number",
                   "Enum.AutoCompleteEntryFlag bits; a result has none."),
};
const Game::Doc::Field kResultsRets[] = {
    Game::Doc::Req("results", "table", "Array of AutoCompleteResult."),
};
const Game::Doc::Function kGetAutoCompleteResults{
    "The known players whose names complete the typed text.", kResultsArgs, kResultsRets};

const Game::Doc::Field kRecognizedArgs[] = {
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("includeFlags", "number",
                   "Enum.AutoCompleteEntryFlag bits; the player has at least one."),
    Game::Doc::Req("excludeFlags", "number",
                   "Enum.AutoCompleteEntryFlag bits; the player has none."),
};
const Game::Doc::Field kRecognizedRets[] = {
    Game::Doc::Req("isRecognizedName", "bool"),
};
const Game::Doc::Function kIsRecognizedName{"Whether a known player has exactly this name.",
                                            kRecognizedArgs, kRecognizedRets};

const Game::Lua::EnumIntegerEntry kEntryFlagEntries[] = {
    {"InGroup", FLAG_IN_GROUP},
    {"InGuild", FLAG_IN_GUILD},
    {"Friend", FLAG_FRIEND},
    {"Bnet", FLAG_BNET},
    {"InteractedWith", FLAG_INTERACTED_WITH},
    {"Online", FLAG_ONLINE},
    {"InAOI", FLAG_IN_AOI},
    {"AccountCharacter", FLAG_ACCOUNT_CHARACTER},
    {"RecentPlayer", FLAG_RECENT_PLAYER},
};

const Game::Lua::EnumIntegerEntry kPriorityEntries[] = {
    {"Other", PRIORITY_OTHER},
    {"Interacted", PRIORITY_INTERACTED},
    {"InGroup", PRIORITY_IN_GROUP},
    {"Guild", PRIORITY_GUILD},
    {"Friend", PRIORITY_FRIEND},
    {"AccountCharacter", PRIORITY_ACCOUNT_CHARACTER},
    {"AccountCharacterSameRealm", PRIORITY_ACCOUNT_CHARACTER_SAME_REALM},
};

} // namespace

void OnChat(int chatType, int languageID, const char *name, uint64_t guid) {
    if (chatType != CHAT_MSG_WHISPER && chatType != CHAT_MSG_WHISPER_INFORM)
        return;
    if (languageID == LANGUAGE_ADDON || guid == 0 || !Guid::IsPlayer(guid) || name == nullptr ||
        *name == '\0')
        return;
    ++g_lastOrder;
    for (Interaction &i : g_interactions) {
        if (i.guid == guid) {
            i.name = name;
            i.order = g_lastOrder;
            return;
        }
    }
    g_interactions.push_back({guid, name, g_lastOrder});
}

static void RegisterLuaFunctions() {
    // Retail's help lines and category (Ketho's CVars.lua dump).
    g_resortOnRecency = CVar::Factory::Register(
        "autoCompleteResortNamesOnRecency",
        "Shows people you recently spoke with higher up on the AutoComplete list.",
        Offsets::CVAR_CATEGORY_GAME, "1");
    g_editFromCenter = CVar::Factory::Register(
        "autoCompleteWhenEditingFromCenter",
        "If you edit a name by inserting characters into the center, a smarter auto-complete "
        "will occur.",
        Offsets::CVAR_CATEGORY_GAME, "1");
    g_useContext = CVar::Factory::Register(
        "autoCompleteUseContext",
        "The system will, for example, only show people in your guild when you are typing "
        "/gpromote. Names will also never be removed.",
        Offsets::CVAR_CATEGORY_GAME, "1");

    Game::Lua::RegisterTableFunction("C_AutoComplete", "GetAutoCompleteResults",
                                     &Script_GetAutoCompleteResults, &kGetAutoCompleteResults);
    Game::Lua::RegisterTableFunction("C_AutoComplete", "IsRecognizedName", &Script_IsRecognizedName,
                                     &kIsRecognizedName);
    Game::Lua::RegisterIntegerEnum(
        "Enum", "AutoCompleteEntryFlag", kEntryFlagEntries, "AutoComplete");
    Game::Lua::RegisterIntegerEnum(
        "Enum", "AutoCompletePriority", kPriorityEntries, "AutoComplete");
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace AutoComplete::Names
