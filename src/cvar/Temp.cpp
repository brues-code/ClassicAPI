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

// `C_CVar.SetTempCVar(name, value)` / `C_CVar.RemoveTempCVar(name)` — change a
// cvar for this session without the change reaching Config.wtf, then put the
// saved value back. Upstream's gamepad mode flips two dozen camera and
// soft-target cvars this way so the user's own settings survive it.
//
// Two engine facts make this nearly free:
//
//   1. The setter only marks the config dirty when its last argument is
//      nonzero (FUN_0063E0B0: `if (a6) VAR_CVAR_CONFIG_DIRTY = 1`). A temp set
//      passes 0, so it applies the value and fires the change callback exactly
//      as SetCVar does, but never asks the config writer to run.
//   2. The config writer (FUN_CVAR_CONFIG_WRITE) reads each cvar's live value
//      pointer and nothing else of ours. When something ELSE dirtied the config
//      and the writer does run, a co-hook points each temp cvar's value back at
//      its saved string for the duration of the write and restores it after. So
//      the file gets exactly the line the engine would have written had the
//      temp value never been set — including no line at all when the saved
//      value is the default.
//
// A plain SetCVar on a temp cvar wins: the entry notices the live value is no
// longer the one it set and drops itself, both in the writer hook and in
// RemoveTempCVar, so the user's own set is saved and never rolled back.
//
// Validation mirrors Script_SetCVar (FUN_00488C10) word for word: a usage
// error, `Couldn't find CVar named '%s'`, and `"%s" is read only`. The lookup
// is FUN_FIND_CVAR, so a cvar Lua cannot see is refused as unknown, which is
// the contract's "valid and public". Nothing here is secure, the remaining
// documented requirement. A nil value becomes "", as it does for SetCVar.

#include "Factory.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace CVar::Temp {

namespace {

struct Entry {
    CVar::Factory::Handle cvar;
    std::string saved; // the value before the first temp set
    std::string temp;  // the value that landed (after the change callback)
};

std::vector<Entry> g_entries;

Entry *FindEntry(CVar::Factory::Handle cvar) {
    for (Entry &e : g_entries)
        if (e.cvar == cvar)
            return &e;
    return nullptr;
}

void EraseEntry(const Entry *entry) {
    g_entries.erase(g_entries.begin() + (entry - g_entries.data()));
}

const char *LiveValue(CVar::Factory::Handle cvar) {
    const char *value = CVar::Factory::GetString(cvar);
    return value != nullptr ? value : "";
}

// True while the live value is still the one SetTempCVar put there.
bool StillTemp(const Entry &e) {
    return e.temp == LiveValue(e.cvar);
}

const char **ValueSlot(CVar::Factory::Handle cvar) {
    return reinterpret_cast<const char **>(static_cast<uint8_t *>(cvar) +
                                           Offsets::OFF_CVAR_VALUE_STR);
}

// Script_SetCVar's own lookup and checks, with its error text.
CVar::Factory::Handle Resolve(void *L, const char *usage) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "%s", usage);
        return nullptr;
    }
    const char *name = Game::Lua::ToString(L, 1);
    CVar::Factory::Handle cvar = CVar::Factory::Find(name);
    if (cvar == nullptr) {
        Game::Lua::Error(L, "Couldn't find CVar named '%s'", name);
        return nullptr;
    }
    const uint32_t flags = *reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(cvar) + Offsets::OFF_CVAR_FLAGS);
    if ((flags & Offsets::CVAR_FLAG_READ_ONLY) != 0) {
        Game::Lua::Error(L, "\"%s\" is read only", name);
        return nullptr;
    }
    return cvar;
}

int __fastcall Script_SetTempCVar(void *L) {
    CVar::Factory::Handle cvar =
        Resolve(L, "Usage: C_CVar.SetTempCVar(\"cvar\", value)");
    if (cvar == nullptr)
        return 0;
    const char *value = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : "";

    Entry *entry = FindEntry(cvar);
    if (entry != nullptr && !StillTemp(*entry)) {
        // A plain SetCVar replaced the temp value; that is the saved value now.
        entry->saved = LiveValue(cvar);
    } else if (entry == nullptr) {
        g_entries.push_back({cvar, LiveValue(cvar), {}});
        entry = &g_entries.back();
    }
    CVar::Factory::SetString(cvar, value, /*persist*/ false);
    // Record what landed, not what was asked: a change callback may clamp.
    // Re-find rather than reuse `entry`, since the callback runs Lua.
    if (Entry *landed = FindEntry(cvar))
        landed->temp = LiveValue(cvar);
    return 0;
}

int __fastcall Script_RemoveTempCVar(void *L) {
    CVar::Factory::Handle cvar =
        Resolve(L, "Usage: C_CVar.RemoveTempCVar(\"cvar\")");
    if (cvar == nullptr)
        return 0;
    Entry *entry = FindEntry(cvar);
    if (entry == nullptr)
        return 0;
    if (StillTemp(*entry)) {
        // Copy first: the setter's change callback could, in principle, reach
        // back in here and move the vector.
        const std::string saved = entry->saved;
        EraseEntry(entry);
        CVar::Factory::SetString(cvar, saved.c_str(), /*persist*/ false);
    } else {
        EraseEntry(entry);
    }
    return 0;
}

// --- Config writer co-hook ------------------------------------------------

using ConfigWrite_t = int(__cdecl *)();
ConfigWrite_t s_configWrite_o = nullptr;

int __cdecl ConfigWrite_h() {
    // Only a dirty config gets written; skip the swap work otherwise.
    if (g_entries.empty() ||
        *reinterpret_cast<const uint8_t *>(Offsets::VAR_CVAR_CONFIG_DIRTY) == 0)
        return s_configWrite_o();

    for (size_t i = g_entries.size(); i-- > 0;)
        if (!StillTemp(g_entries[i]))
            g_entries.erase(g_entries.begin() + i);

    std::vector<const char *> live(g_entries.size());
    for (size_t i = 0; i < g_entries.size(); ++i) {
        const char **slot = ValueSlot(g_entries[i].cvar);
        live[i] = *slot;
        *slot = g_entries[i].saved.c_str();
    }
    const int result = s_configWrite_o();
    for (size_t i = 0; i < g_entries.size(); ++i)
        *ValueSlot(g_entries[i].cvar) = live[i];
    return result;
}

const Game::HookAutoRegister _hookConfigWrite{
    Offsets::FUN_CVAR_CONFIG_WRITE,
    reinterpret_cast<void *>(&ConfigWrite_h),
    reinterpret_cast<void **>(&s_configWrite_o)};

// --- Registration ---------------------------------------------------------

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("name", "cstring"),
    Game::Doc::Opt("value", "cstring"),
};
const Game::Doc::Function kSetTempCVar{
    "Sets a console variable for this session only; the change is not saved.",
    kSetArgs, {}};

const Game::Doc::Field kRemoveArgs[] = {
    Game::Doc::Req("name", "cstring"),
};
const Game::Doc::Function kRemoveTempCVar{
    "Restores the value a console variable had before SetTempCVar.",
    kRemoveArgs, {}};

void Register() {
    Game::Lua::RegisterTableFunction("C_CVar", "SetTempCVar", &Script_SetTempCVar,
                                     &kSetTempCVar);
    Game::Lua::RegisterTableFunction("C_CVar", "RemoveTempCVar", &Script_RemoveTempCVar,
                                     &kRemoveTempCVar);
}

// Cvar storage is process-global, so the same surface works pre-login.
const Game::ModuleAutoRegister _autoreg{&Register};
const Game::GlueModuleAutoRegister _glueAutoreg{&Register};

} // namespace

} // namespace CVar::Temp
