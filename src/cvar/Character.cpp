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
// CVAR_SCOPE_CHARACTER cvars live in account-data section 1, the character's
// `config-cache.wtf`. 3.3.5 reads them with LoadAccountData and writes them
// with SaveAccountData. 1.12 has the same section and the same two calls
// (FUN_ACCOUNT_DATA_READ / FUN_ACCOUNT_DATA_WRITE), but no 1.12 code uses
// section 1, so the file is ours. All file access goes through those two
// calls:
//   - the path is the engine's own (OpenAccountDataFile: the realmName cvar,
//     the selected character, the UTF-8 name opened through CreateFileW). A
//     path built here and opened through the ANSI code page names a
//     different folder for a character or realm name outside ASCII, and
//     the saved values were never found again.
//   - the read waits for the server sync, as the engine's own section readers
//     (layout, bindings, macros, chat) do, so a newer server copy is what
//     gets applied.
//   - the write arms the section's save timer, which uploads the file and
//     stores its digest. Then the next sync sees the file as up to date.
//
// The file is parsed the way the engine reads Config.wtf (FUN_CVAR_LOAD_FILE):
// lines, then each `SET` line tokenized with the console `set` command's
// delimiters.
//
// Where 3.3.5 saves at logout, this saves on every accepted change. 1.12
// writes Config.wtf on the world→glue path and at process exit, and a quit
// from the world skips the glue boot that is our only logout signal; writing
// at the change keeps the file right without a hook on either path.
//
// Config.wtf: the registrar forces CVAR_FLAG_ARCHIVE on at every call, and
// FUN_CVAR_CONFIG_WRITE writes only flagged cvars, so the bit is cleared
// after each registration.

#include "cvar/Character.h"

#include "Offsets.h"
#include "os/File.h"
#include "storm/String.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace CVar::Character {

namespace {

// Both section calls and their callback are __fastcall with one stack
// argument (`ret 4`); see Offsets.h.
using SectionCallback = void(__fastcall *)(void *localFile, void *cacheFile, void *arg);
using SectionCall_t = void(__fastcall *)(uint32_t section, SectionCallback callback, void *arg);

struct Entry {
    const char *name;
    Factory::Handle cvar;
    Factory::ChangeCallback callback;
    void *userData;
};

// Heap entries: the engine keeps each one's address as the cvar's userData.
std::vector<std::unique_ptr<Entry>> g_entries;

// Set while values are applied; the file is written only for changes made
// outside that.
bool g_applying = false;

// Set by ApplySaved; tells Register whether the read ran at once or waits for
// the sync.
bool g_applied = false;

void Section(uintptr_t fn, SectionCallback callback, void *arg) {
    reinterpret_cast<SectionCall_t>(fn)(Offsets::ACCOUNT_DATA_SECTION_CHARACTER_CONFIG, callback,
                                        arg);
}

// The value of `name`'s `SET name "value"` line in `text`.
bool FindSaved(const std::string &text, const char *name, std::string &out) {
    const char *lineDelims = reinterpret_cast<const char *>(Offsets::VAR_MACRO_LINE_DELIMS);
    const char *setDelims = reinterpret_cast<const char *>(Offsets::VAR_CONSOLE_SET_DELIMS);
    const char *cursor = text.c_str();
    char line[0x80]; // the line length FUN_CVAR_LOAD_FILE keeps
    while (cursor != nullptr && *cursor != '\0') {
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
        return true;
    }
    return false;
}

// Sets `value` without writing the file; nothing when it already holds it.
void Apply(Factory::Handle cvar, const char *value) {
    const char *current = Factory::GetString(cvar);
    if (value == nullptr || (current != nullptr && std::strcmp(current, value) == 0))
        return;
    g_applying = true;
    Factory::SetString(cvar, value, /*persist*/ false);
    g_applying = false;
}

// The section's read callback: every registered cvar takes its saved value,
// or its default when the file has none. It runs at once when the section is
// not pending (/reload) and from the sync otherwise, and the engine keeps it
// stored afterwards, so a later server copy is applied too.
void __fastcall ApplySaved(void * /*localFile*/, void *cacheFile, void * /*arg*/) {
    g_applied = true;
    std::string text;
    Os::File::ReadOpen(cacheFile, text);
    for (const auto &e : g_entries) {
        if (e->cvar == nullptr)
            continue;
        std::string saved;
        Apply(e->cvar, FindSaved(text, e->name, saved) ? saved.c_str()
                                                       : Factory::GetDefaultString(e->cvar));
    }
}

struct Change {
    const Entry *entry;
    const char *newValue;
};

// The section's writer: the file from the live values, with the changed cvar
// about to take its new value. A value equal to its default (case-blind) is
// left out, as Config.wtf's writer does.
void __fastcall WriteValues(void * /*localFile*/, void *cacheFile, void *arg) {
    const auto *change = static_cast<const Change *>(arg);
    std::string text;
    for (const auto &e : g_entries) {
        if (e->cvar == nullptr)
            continue;
        const char *value =
            e.get() == change->entry ? change->newValue : Factory::GetString(e->cvar);
        const char *def = Factory::GetDefaultString(e->cvar);
        if (value == nullptr || *value == '\0' || (def != nullptr && _stricmp(value, def) == 0))
            continue;
        text += std::string("SET ") + e->name + " \"" + value + "\"\r\n";
    }
    Os::File::WriteOpen(cacheFile, text);
}

// While the section is pending the engine drops the write, as it does for its
// own sections; the sync's read then applies the file's values.
int __fastcall OnChange(Factory::Handle cvar, const char *prevValue, const char *newValue,
                        void *userData) {
    const auto *e = static_cast<const Entry *>(userData);
    const int accepted = e->callback ? e->callback(cvar, prevValue, newValue, e->userData) : 1;
    if (accepted && !g_applying) {
        Change change{e, newValue};
        Section(Offsets::FUN_ACCOUNT_DATA_WRITE, &WriteValues, &change);
    }
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
    g_applying = false;
    if (e->cvar == nullptr)
        return nullptr;

    auto &flags = *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(e->cvar) +
                                                Offsets::OFF_CVAR_FLAGS);
    flags &= ~static_cast<uint32_t>(Offsets::CVAR_FLAG_ARCHIVE);

    g_applied = false;
    Section(Offsets::FUN_ACCOUNT_DATA_READ, &ApplySaved, nullptr);
    // At login the section waits for the sync. Until then the cvar holds its
    // default, so the next character never inherits the last one's value.
    if (!g_applied)
        Apply(e->cvar, defaultValue);
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
