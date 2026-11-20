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

#include "addons/LoadManagers.h"

#include "addons/Registry.h"
#include "baselib/Ascii.h"
#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace AddOns::LoadManagers {

namespace {

struct Entry {
    std::string name;
    std::vector<std::string> managers;
};

// Only addons that list managers; most TOCs have none.
std::vector<Entry> g_entries;

Entry *Find(const char *name, size_t len) {
    for (Entry &e : g_entries)
        if (e.name.size() == len && Ascii::EqualCI(name, len, e.name.c_str()))
            return &e;
    return nullptr;
}

const std::vector<std::string> *ManagersOf(const char *name) {
    if (name == nullptr) return nullptr;
    const Entry *e = Find(name, std::strlen(name));
    return e != nullptr ? &e->managers : nullptr;
}

using LoadOne_t = unsigned int(__fastcall *)(const char *name, char flag, void *progressCtx);
using CanLoad_t = uint8_t(__fastcall *)(const char *name, char fullCheck, int *outStatus,
                                       int *outDep, const char *account);

LoadOne_t LoadOne() { return reinterpret_cast<LoadOne_t>(Offsets::FUN_ADDON_LOAD_ONE); }
CanLoad_t CanLoad() { return reinterpret_cast<CanLoad_t>(Offsets::FUN_ADDON_CAN_LOAD); }

void MarkLoadOnDemand(const char *name) {
    Addons::ForEachEntry([name](uintptr_t entry) {
        const char *entryName =
            *reinterpret_cast<const char *const *>(entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
        if (entryName == nullptr || !Ascii::EqualCI(entryName, name)) return true;
        *reinterpret_cast<uint8_t *>(entry + Offsets::OFF_ADDON_ENTRY_LOAD_ON_DEMAND) = 1;
        return false;
    });
}

// The load pass's loader call. `name` is the entry's own name pointer.
unsigned int __fastcall LoadPassLoad(const char *name, char flag, void *progressCtx) {
    if (const auto *managers = ManagersOf(name)) {
        for (const std::string &manager : *managers) {
            if ((LoadOne()(manager.c_str(), flag, progressCtx) & 0xFF) != 0) {
                MarkLoadOnDemand(name);
                return 0;
            }
        }
    }
    return LoadOne()(name, flag, progressCtx);
}

// The load pass's progress sum: an addon one of whose managers can load
// adds nothing.
uint8_t __fastcall ProgressCanLoad(const char *name, char fullCheck, int *outStatus,
                                   int *outDep, const char *account) {
    if (const auto *managers = ManagersOf(name)) {
        for (const std::string &manager : *managers) {
            int status = 0, dep = 0;
            if (CanLoad()(manager.c_str(), fullCheck, &status, &dep, account) != 0) {
                *outStatus = 0;
                return 0;
            }
        }
    }
    return CanLoad()(name, fullCheck, outStatus, outDep, account);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_ADDON_LOAD_PASS_LOAD_ONE, Offsets::FUN_ADDON_LOAD_ONE,
                         reinterpret_cast<const void *>(&LoadPassLoad));
    Common::RedirectCall(Offsets::CALL_ADDON_PROGRESS_CAN_LOAD, Offsets::FUN_ADDON_CAN_LOAD,
                         reinterpret_cast<const void *>(&ProgressCanLoad));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

void Record(const char *name, size_t nameLen, const std::vector<std::string> &managers) {
    if (name == nullptr || nameLen == 0) return;
    Entry *e = Find(name, nameLen);
    if (managers.empty()) {
        if (e != nullptr) g_entries.erase(g_entries.begin() + (e - g_entries.data()));
        return;
    }
    if (e == nullptr) {
        g_entries.push_back({std::string(name, nameLen), managers});
        return;
    }
    e->managers = managers;
}

} // namespace AddOns::LoadManagers
