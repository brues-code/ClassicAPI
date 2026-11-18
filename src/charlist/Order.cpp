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

// Character list order — `SaveCharacterOrder(translationTable)`, the glue
// global newer clients use to keep the character-select order.
//
// How the newer engines do it, which this mirrors:
//   - 4.3.4 (`Script_SaveCharacterOrder`) takes a table whose i-th entry is
//     the 1-based roster index of the character shown at position i,
//     requires one valid index per roster entry, and sends the order to the
//     server. It never reorders the roster it has: the NEXT character list
//     arrives sorted.
//   - Modern clients keep the order locally in
//     `WTF\Account\<account>\character-list-order.txt`, keyed by character
//     GUID, and apply it in C. Glue Lua never sees the file. They also
//     register `lastCharacterGuid` ("Last character selected's guid") beside
//     `lastCharacterIndex` (Classic Era's binary has both strings, adjacent),
//     so the last character survives a reorder.
//
// The 1.12 server has no reorder message, so the "arrives sorted" half
// happens here: a pre-original hook on LoadCharSelectRoster sorts net's
// character array in place, before the engine copies it into the roster.
// Every index-keyed path (`GetCharacterInfo`, `SelectCharacter`,
// `DeleteCharacter`, EnterWorld) then sees the saved order. Characters with
// no saved slot (new ones) keep wire order after the saved ones, so a fresh
// character lands last, where the create flow's SELECT_LAST_CHARACTER
// expects it. Nothing else depends on net's array order: its other reader
// (`AutoComplete::Names`) merges by GUID.
//
// The last character: EnterWorld stores the roster INDEX in
// `lastCharacterIndex`, and an index goes stale when the order changes. So
// a Glue::EnterWorld subscriber also stores the GUID in `lastCharacterGuid`
// (the retail cvar; its semantics are inferred from the name and help, the
// retail code is encrypted), and the same pre-original hook points
// `lastCharacterIndex` at that GUID's sorted position before the engine's
// own restore reads it. A GUID that is not in the list (deleted, other
// realm) leaves the index alone, which the engine clamps.
//
// The file follows Blizzard's layout, `Version:` header then one
// `<realm> <guid> <slot>` line per character. Blizzard's realm column is a
// numeric realm ID that 1.12's realm list does not carry, so ours is the
// realm NAME; it may contain spaces, so a line parses from the right. The
// GUID is this project's canonical `0x` form (Guid::FormatAsString).
//
// `GetSavedCharacterOrder` / `SetSavedCharacterOrder` are this module's
// earlier name-keyed API, kept as deprecated wrappers so GlueXML written
// against them keeps working (see their comments).

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "glue/EnterWorld.h"
#include "guid/Guid.h"
#include "settings/Paths.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace Charlist::Order {

namespace {

constexpr const char *kFileName = "character-list-order.txt";
constexpr const char *kVersionLine = "Version: 1";

struct Line {
    std::string realm;
    uint64_t guid;
    int slot; // 1-based position in the list
};

// ---- File -------------------------------------------------------------------

// Every well-formed line of the file; a missing file reads as none.
std::vector<Line> ReadFile(const std::string &path) {
    std::vector<Line> lines;
    std::ifstream file(path);
    std::string text;
    while (std::getline(file, text)) {
        while (!text.empty() && (text.back() == '\r' || text.back() == ' '))
            text.pop_back();
        if (text.rfind("Version:", 0) == 0)
            continue;
        const auto slotAt = text.rfind(' ');
        if (slotAt == std::string::npos || slotAt == 0)
            continue;
        const auto guidAt = text.rfind(' ', slotAt - 1);
        if (guidAt == std::string::npos || guidAt == 0)
            continue;
        uint64_t guid = 0;
        if (!Guid::Parse(text.substr(guidAt + 1, slotAt - guidAt - 1).c_str(), &guid) ||
            guid == 0)
            continue;
        char *end = nullptr;
        const long slot = std::strtol(text.c_str() + slotAt + 1, &end, 10);
        if (*end != '\0' || slot < 1)
            continue;
        lines.push_back(Line{text.substr(0, guidAt), guid, static_cast<int>(slot)});
    }
    return lines;
}

void WriteFile(const std::string &path, const std::vector<Line> &lines) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
        return;
    file << kVersionLine << '\n';
    char guid[Guid::STRING_SIZE];
    for (const Line &l : lines)
        file << l.realm << ' ' << Guid::FormatAsString(l.guid, guid, sizeof guid) << ' '
             << l.slot << '\n';
}

// ---- Last character ---------------------------------------------------------

constexpr const char *kLastGuidCVar = "lastCharacterGuid";
constexpr const char *kLastIndexCVar = "lastCharacterIndex";

void OnEnterWorld(const uint8_t *character) {
    char text[Guid::STRING_SIZE];
    CVar::Factory::SetString(
        CVar::Factory::Find(kLastGuidCVar),
        Guid::FormatAsString(Game::Read<uint64_t>(character, Offsets::OFF_GLUE_CHAR_GUID), text,
                             sizeof text));
}

const Glue::EnterWorld::AutoSubscribe _enterWorld{&OnEnterWorld};

// ---- Applying the order -----------------------------------------------------

constexpr std::size_t kNetStride = Offsets::CLIENT_CONNECTION_CHAR_STRIDE;

uint64_t NetGuid(const uint8_t *records, int index) {
    return Game::Read<uint64_t>(records + index * kNetStride,
                                Offsets::OFF_CLIENT_CONNECTION_CHAR_GUID);
}

// Sorts net's character array by the realm's saved slots. Idempotent: the
// sort key is the GUID's slot, never the array's current order.
void SortNetCharacters(uint8_t *records, int count) {
    const std::string path = Settings::Paths::AccountFile(kFileName);
    const std::string realm = Settings::Paths::RealmName();
    if (path.empty() || realm.empty() || count < 2)
        return;

    std::unordered_map<uint64_t, int> slots;
    for (const Line &l : ReadFile(path))
        if (l.realm == realm)
            slots[l.guid] = l.slot;
    if (slots.empty())
        return;

    constexpr std::size_t stride = kNetStride;
    std::vector<int> keys(count);
    for (int i = 0; i < count; ++i) {
        const auto it = slots.find(NetGuid(records, i));
        keys[i] = it != slots.end() ? it->second : std::numeric_limits<int>::max();
    }
    std::vector<int> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return keys[a] < keys[b]; });
    if (std::is_sorted(order.begin(), order.end()))
        return;

    std::vector<uint8_t> sorted(count * stride);
    for (int p = 0; p < count; ++p)
        std::memcpy(sorted.data() + p * stride, records + order[p] * stride, stride);
    std::memcpy(records, sorted.data(), sorted.size());
}

// Points `lastCharacterIndex` at the `lastCharacterGuid` character's position
// in net's (sorted) array, which is the position the roster copies it to.
void RestoreLastCharacter(const uint8_t *records, int count) {
    uint64_t last = 0;
    if (!Guid::Parse(CVar::Factory::GetString(CVar::Factory::Find(kLastGuidCVar)), &last) ||
        last == 0)
        return;
    for (int i = 0; i < count; ++i) {
        if (NetGuid(records, i) == last) {
            CVar::Factory::Handle index = CVar::Factory::Find(kLastIndexCVar);
            if (CVar::Factory::GetInt(index) != i)
                CVar::Factory::SetInt(index, i);
            return;
        }
    }
}

using LoadRoster_t = void(__cdecl *)();
LoadRoster_t g_origLoadRoster = nullptr;

void __cdecl LoadRoster_h() {
    using GetCurrentFn = uint8_t *(__cdecl *)();
    uint8_t *conn = reinterpret_cast<GetCurrentFn>(
        static_cast<uintptr_t>(Offsets::FUN_CLIENT_SERVICES_GET_CURRENT))();
    if (conn != nullptr) {
        const int count = Game::Read<int>(conn, Offsets::OFF_CLIENT_CONNECTION_CHAR_COUNT);
        auto *records = Game::Read<uint8_t *>(conn, Offsets::OFF_CLIENT_CONNECTION_CHARS);
        if (records != nullptr && count > 0) {
            SortNetCharacters(records, count);
            RestoreLastCharacter(records, count);
        }
    }
    g_origLoadRoster();
}

const Game::HookAutoRegister _loadRosterHook{
    Offsets::FUN_LOAD_CHAR_SELECT_ROSTER, reinterpret_cast<void *>(&LoadRoster_h),
    reinterpret_cast<void **>(&g_origLoadRoster)};

// ---- Saving the order -------------------------------------------------------

int RosterCount() { return Game::Read<int>(Offsets::VAR_GLUE_ROSTER_COUNT); }

const uint8_t *RosterRecord(int index) {
    return Game::Read<const uint8_t *>(Offsets::VAR_GLUE_ROSTER_RECORDS) +
           index * Offsets::GLUE_ROSTER_STRIDE;
}

// Saves `order` — one 0-based roster index per roster entry, in display
// order — as this realm's lines. 4.3.4 also moved `lastCharacterIndex` to the
// selected character's new position here; `lastCharacterGuid` makes that
// unnecessary, and it would be wrong anyway once EnterWorld rewrites the
// index after a save.
void Save(const std::vector<int> &order) {
    const std::string path = Settings::Paths::AccountFile(kFileName);
    const std::string realm = Settings::Paths::RealmName();
    if (path.empty() || realm.empty())
        return;

    std::vector<Line> lines = ReadFile(path);
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [&](const Line &l) { return l.realm == realm; }),
                lines.end());
    for (int p = 0; p < static_cast<int>(order.size()); ++p)
        lines.push_back(Line{
            realm, Game::Read<uint64_t>(RosterRecord(order[p]), Offsets::OFF_GLUE_CHAR_GUID),
            p + 1});
    WriteFile(path, lines);
}

// ---- Lua surface ------------------------------------------------------------

// `SaveCharacterOrder(translationTable)`: entry i is the 1-based roster index
// of the character shown at position i. Like 4.3.4, it saves nothing unless
// every roster entry has a valid index. An empty roster is not saved either:
// it is the transient state between leaving the world and the next character
// list, and saving it would erase the realm's order.
int __fastcall Script_SaveCharacterOrder(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: SaveCharacterOrder(table)");
        return 0;
    }
    const int count = RosterCount();
    if (count <= 0)
        return 0;
    std::vector<int> order;
    order.reserve(count);
    for (int i = 1; i <= count; ++i) {
        Game::Lua::RawGetI(L, 1, i);
        const bool isNumber = Game::Lua::IsNumber(L, -1) != 0;
        const int index = isNumber ? static_cast<int>(Game::Lua::ToNumber(L, -1)) - 1 : -1;
        Game::Lua::SetTop(L, -2);
        if (index < 0 || index >= count)
            break;
        order.push_back(index);
    }
    if (static_cast<int>(order.size()) == count)
        Save(order);
    return 0;
}

// DEPRECATED. `GetSavedCharacterOrder(realm)` — the order now applies in C
// before the roster loads, so there is nothing left for the caller to apply:
// always "". Old GlueXML treats "" as "no saved order" and keeps the roster
// as it arrived, which is already the saved order.
int __fastcall Script_GetSavedCharacterOrder(void *L) {
    Game::Lua::PushString(L, "");
    return 1;
}

// DEPRECATED. `SetSavedCharacterOrder(realm, names)` — names joined by "|" in
// display order. Each name resolves to its roster index; roster entries the
// string leaves out follow in roster order; then it saves as
// SaveCharacterOrder does. The realm argument is ignored: the roster
// belongs to the session's realm, and that is the realm the order saves for.
int __fastcall Script_SetSavedCharacterOrder(void *L) {
    if (!Game::Lua::IsString(L, 2))
        return 0;
    const char *names = Game::Lua::ToString(L, 2);
    const int count = RosterCount();
    if (names == nullptr || names[0] == '\0' || count <= 0)
        return 0;

    std::vector<int> order;
    std::vector<bool> claimed(count, false);
    const char *p = names;
    while (*p != '\0') {
        const char *end = std::strchr(p, '|');
        const std::size_t len = end != nullptr ? end - p : std::strlen(p);
        for (int i = 0; i < count; ++i) {
            const char *name = reinterpret_cast<const char *>(RosterRecord(i) +
                                                              Offsets::OFF_GLUE_CHAR_NAME);
            if (!claimed[i] && std::strncmp(name, p, len) == 0 && name[len] == '\0') {
                claimed[i] = true;
                order.push_back(i);
                break;
            }
        }
        p += len;
        if (*p == '|')
            ++p;
    }
    for (int i = 0; i < count; ++i)
        if (!claimed[i])
            order.push_back(i);
    Save(order);
    return 0;
}

const Game::Doc::Field kSaveCharacterOrderArgs[] = {
    Game::Doc::Req("translationTable", "table",
                   "Entry i is the character index (as GetCharacterInfo takes it) of the "
                   "character shown at position i, one entry per character."),
};
const Game::Doc::Function kSaveCharacterOrder{
    "Saves the order of the character list for this realm; the list loads in this order "
    "from the next time it arrives.",
    kSaveCharacterOrderArgs, {}, "CharacterListGlobals"};

void RegisterGlue() {
    // Registered on the glue pass: both its writer (EnterWorld) and its
    // reader (the roster load) run on the glue screens. A Config.wtf line
    // has already created it by then on every launch but the first.
    CVar::Factory::Register(kLastGuidCVar, "Last character selected's guid",
                            Offsets::CVAR_CATEGORY_GAME, "");
    Game::Lua::RegisterGlueFunction("SaveCharacterOrder", &Script_SaveCharacterOrder,
                                    &kSaveCharacterOrder);
    Game::Lua::RegisterGlueFunction("GetSavedCharacterOrder", &Script_GetSavedCharacterOrder,
                                    &Game::Doc::Deprecated);
    Game::Lua::RegisterGlueFunction("SetSavedCharacterOrder", &Script_SetSavedCharacterOrder,
                                    &Game::Doc::Deprecated);
}

const Game::GlueModuleAutoRegister _autoreg{&RegisterGlue};

} // namespace

} // namespace Charlist::Order
