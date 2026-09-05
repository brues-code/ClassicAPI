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

// See `Notes.h`. Mirrors the equipment manager's storage approach
// (equipmentset/Storage.cpp): a small text file under WTF, atomic
// tmp+rename write, path re-resolved on every access so a
// logout→character-select→login (no DLL reload) picks up the new realm.

#include "Notes.h"

#include "settings/Paths.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

namespace FriendList::Notes {

namespace {

struct Entry {
    std::string name; // last-seen name, for a readable file (not the key)
    std::string note;
};

std::unordered_map<uint64_t, Entry> g_notes;
std::string g_path;
bool g_loaded = false;

// WTF\Account\<acct>\<realm>\<char>\ClassicAPI_FriendNotes.txt, or empty until
// the account + realm + character session globals are populated. Per-character
// because the friends list is per-character in vanilla.
std::string ResolveFilePath() {
    return Settings::Paths::CharacterFile("ClassicAPI_FriendNotes.txt");
}

// Notes are a single line each; the file is tab-delimited, so a note must
// carry no tab or newline. Replace any with a space and trim trailing space.
void Sanitize(std::string *s) {
    for (char &c : *s)
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    while (!s->empty() && s->back() == ' ')
        s->pop_back();
}

void Load() {
    g_notes.clear();
    std::ifstream file(g_path);
    if (!file.is_open())
        return; // first run for this character — empty store is fine

    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        // Format: `0xGUID<tab>name<tab>note`.
        const auto t1 = line.find('\t');
        if (t1 == std::string::npos)
            continue;
        const auto t2 = line.find('\t', t1 + 1);
        const uint64_t guid = std::strtoull(line.c_str(), nullptr, 0);
        if (guid == 0 || t2 == std::string::npos)
            continue;
        std::string name = line.substr(t1 + 1, t2 - (t1 + 1));
        std::string note = line.substr(t2 + 1);
        if (note.empty())
            continue;
        if (static_cast<int>(note.size()) > MAX_NOTE_LEN)
            note.resize(MAX_NOTE_LEN);
        g_notes[guid] = Entry{std::move(name), std::move(note)};
    }
}

void Save() {
    if (g_path.empty())
        return;
    const std::string tmp = g_path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return;
        out << "# ClassicAPI Friend Notes v1\n";
        out << "# <0xGUID>\\t<name>\\t<note>  (per-character, GUID-keyed)\n";
        for (const auto &kv : g_notes) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "0x%016llX",
                          static_cast<unsigned long long>(kv.first));
            out << buf << '\t' << kv.second.name << '\t' << kv.second.note << '\n';
        }
    }
    std::remove(g_path.c_str());
    std::rename(tmp.c_str(), g_path.c_str());
}

// Re-resolve the path each call; reload when it changes. The DLL survives a
// character/realm switch, so a permanent latch would keep the old character's
// file — matching EquipmentSet::Data::EnsureLoaded.
void EnsureLoaded() {
    std::string path = ResolveFilePath();
    if (path.empty()) {
        // Session globals not ready (login / char-select / loading screen).
        // Drop any cached state so a stale path from the PREVIOUS character
        // can never be read from or written to — that is the "didn't wipe
        // between characters" trap. Nothing queries notes out of world, so
        // there is nothing to lose; the next in-world call reloads cleanly.
        g_notes.clear();
        g_path.clear();
        g_loaded = false;
        return;
    }
    if (g_loaded && path == g_path)
        return;
    // Path changed (different character/account) — Load() clears the map
    // before reading, so the new character never inherits the old one's notes.
    g_path = std::move(path);
    Load();
    g_loaded = true;
}

} // namespace

const char *Get(uint64_t guid) {
    if (guid == 0)
        return nullptr;
    EnsureLoaded();
    auto it = g_notes.find(guid);
    if (it == g_notes.end() || it->second.note.empty())
        return nullptr;
    return it->second.note.c_str();
}

bool Set(uint64_t guid, const char *name, const char *note) {
    if (guid == 0)
        return false;
    EnsureLoaded();
    if (g_path.empty())
        return false; // no realm/account yet — nowhere to persist

    std::string text = (note != nullptr) ? note : "";
    Sanitize(&text);
    if (static_cast<int>(text.size()) > MAX_NOTE_LEN)
        text.resize(MAX_NOTE_LEN);

    if (text.empty()) {
        if (g_notes.erase(guid) == 0)
            return false; // nothing to clear
    } else {
        Entry e;
        e.name = (name != nullptr) ? name : "";
        Sanitize(&e.name);
        e.note = std::move(text);
        g_notes[guid] = std::move(e);
    }
    Save();
    return true;
}

} // namespace FriendList::Notes
