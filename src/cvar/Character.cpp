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

// Character-scoped cvars, on 3.3.5's design (AccountData.cpp, GameUI.cpp):
// CVAR_SCOPE_CHARACTER cvars are saved with CVar::SaveAll(file, 0x20) into
// the per-character section's `config-cache.wtf` and loaded from it with
// CVar::Load before VARIABLES_LOADED. 1.12 already has that section — record
// 1 of the account-data table 0x0085DCF0, same file name — but nothing in
// 1.12 reads or writes the file, so the file is ours. The engine's server
// sync still sees it: at login it hashes section 1 and uploads it when the
// server's copy differs, as it does sections 2-7.
//
// The file is read the way the engine reads Config.wtf (FUN_CVAR_LOAD_FILE):
// the whole file through Storm's reader, split into lines, each `SET` line
// tokenized with the console `set` command's delimiters.
//
// Where 3.3.5 saves at logout, this saves on every accepted change. 1.12
// writes Config.wtf on the world→glue path and at process exit, and a quit
// from the world skips the glue boot that is our only logout signal; writing
// at the change keeps the file right without a hook on either path.
//
// Config.wtf: the registrar forces CVAR_FLAG_ARCHIVE on at every call, and
// FUN_CVAR_CONFIG_WRITE writes only flagged cvars, so the bit is cleared
// after each registration. A line an older build wrote to Config.wtf still
// creates the cvar at start-up, but the registration pass overwrites its
// value and the next config write drops the line.

#include "cvar/Character.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "settings/Paths.h"
#include "storm/File.h"
#include "storm/Memory.h"
#include "storm/String.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace CVar::Character {

namespace {

struct Entry {
    const char *name;
    Factory::Handle cvar;
    Factory::ChangeCallback callback;
    void *userData;
};

// Heap entries: the engine keeps each one's address as the cvar's userData.
std::vector<std::unique_ptr<Entry>> g_entries;

// Set while registration applies values; the file is written only for
// changes made afterwards.
bool g_applying = false;

std::string FilePath() {
    return Settings::Paths::CharacterFile(
        reinterpret_cast<const char *>(Offsets::VAR_STR_CONFIG_CACHE_FILE));
}

// The value of `name`'s `SET name "value"` line in the character's file.
bool FindSaved(const char *name, std::string &out) {
    const std::string path = FilePath();
    void *buf = nullptr;
    if (path.empty() || !Storm::File::Read(path.c_str(), &buf, nullptr, 1))
        return false;

    const char *lineDelims = reinterpret_cast<const char *>(Offsets::VAR_MACRO_LINE_DELIMS);
    const char *setDelims = reinterpret_cast<const char *>(Offsets::VAR_CONSOLE_SET_DELIMS);
    bool found = false;
    const char *cursor = static_cast<const char *>(buf);
    char line[0x80]; // the line length FUN_CVAR_LOAD_FILE keeps
    while (!found && cursor != nullptr && *cursor != '\0') {
        Storm::Tokenize(&cursor, line, sizeof line, lineDelims);
        if (_strnicmp(line, "SET ", 4) != 0)
            continue;
        const char *p = line + 4;
        char token[0x100];
        Storm::Tokenize(&p, token, sizeof token, setDelims);
        if (_stricmp(token, name) != 0)
            continue;
        Storm::Tokenize(&p, token, sizeof token, setDelims);
        out = token;
        found = true;
    }
    Storm::Free(buf, __FILE__, __LINE__);
    return found;
}

// Rewrites the file from the live values, with `changed` about to take
// `newValue`. A value equal to its default (case-blind) is left out, as
// Config.wtf's writer does.
void Save(const Entry *changed, const char *newValue) {
    const std::string path = FilePath();
    if (path.empty())
        return;
    Common::EnsureParentDirs(path.c_str());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto &e : g_entries) {
        if (e->cvar == nullptr)
            continue;
        const char *value = e.get() == changed ? newValue : Factory::GetString(e->cvar);
        const char *def = Factory::GetDefaultString(e->cvar);
        if (value == nullptr || *value == '\0' || (def != nullptr && _stricmp(value, def) == 0))
            continue;
        out << "SET " << e->name << " \"" << value << "\"\n";
    }
}

int __fastcall OnChange(Factory::Handle cvar, const char *prevValue, const char *newValue,
                        void *userData) {
    const auto *e = static_cast<const Entry *>(userData);
    const int accepted = e->callback ? e->callback(cvar, prevValue, newValue, e->userData) : 1;
    if (accepted && !g_applying)
        Save(e, newValue);
    return accepted;
}

Entry *EntryFor(const char *name) {
    for (const auto &e : g_entries) {
        if (_stricmp(e->name, name) == 0)
            return e.get();
    }
    g_entries.push_back(std::make_unique<Entry>(Entry{name, nullptr, nullptr, nullptr}));
    return g_entries.back().get();
}

} // namespace

Factory::Handle Register(const char *name, const char *help, int category,
                         const char *defaultValue, Factory::ChangeCallback callback,
                         void *userData) {
    if (name == nullptr)
        return nullptr;
    Entry *e = EntryFor(name);
    e->callback = callback;
    e->userData = userData;

    g_applying = true;
    e->cvar = Factory::Register(name, help, category, defaultValue, &OnChange, e);
    if (e->cvar != nullptr) {
        auto &flags = *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(e->cvar) +
                                                    Offsets::OFF_CVAR_FLAGS);
        flags &= ~static_cast<uint32_t>(Offsets::CVAR_FLAG_ARCHIVE);

        std::string saved;
        const char *value = FindSaved(name, saved) ? saved.c_str() : defaultValue;
        const char *current = Factory::GetString(e->cvar);
        if (value != nullptr && (current == nullptr || std::strcmp(current, value) != 0))
            Factory::SetString(e->cvar, value, /*persist*/ false);
    }
    g_applying = false;
    return e->cvar;
}

bool Is(Factory::Handle cvar) {
    if (cvar == nullptr)
        return false;
    for (const auto &e : g_entries) {
        if (e->cvar == cvar)
            return true;
    }
    return false;
}

} // namespace CVar::Character
