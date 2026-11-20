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

#include "addons/LoadGate.h"

#include "addons/TocRewrite.h"
#include "baselib/Ascii.h"
#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace AddOns::LoadGate {

namespace {

// 3.80.2's reason codes and names (table 0x144016160). 1.12's own codes stop
// at 7, so these do not collide.
constexpr int REASON_WRONG_ACTIVE_INTERFACE = 11;
constexpr int REASON_WRONG_GAME_TYPE = 12;

constexpr uint32_t ALLOW_LOAD_GAME = 2;

struct Gate {
    std::string name;
    uint32_t gameTypes;
    uint32_t allowLoad;
};

// Only addons with a gate; most TOCs have none.
std::vector<Gate> g_gates;

Gate *Find(const char *name, size_t len) {
    for (Gate &g : g_gates)
        if (g.name.size() == len && Ascii::EqualCI(name, len, g.name.c_str()))
            return &g;
    return nullptr;
}

// The 3.80.2 reason that refuses a real load of `name`, or 0.
int ReasonFor(const char *name) {
    if (name == nullptr) return 0;
    const Gate *g = Find(name, std::strlen(name));
    if (g == nullptr) return 0;
    if ((g->allowLoad & ALLOW_LOAD_GAME) == 0) return REASON_WRONG_ACTIVE_INTERFACE;
    if (g->gameTypes != 0 && (g->gameTypes & TocRewrite::ClientGameTypes()) == 0)
        return REASON_WRONG_GAME_TYPE;
    return 0;
}

const char *ReasonName(int code) {
    switch (code) {
    case REASON_WRONG_ACTIVE_INTERFACE: return "WRONG_ACTIVE_INTERFACE";
    case REASON_WRONG_GAME_TYPE: return "WRONG_GAME_TYPE";
    default: return nullptr;
    }
}

// FUN_ADDON_CAN_LOAD.
using CanLoad_t = uint8_t(__fastcall *)(const char *name, char fullCheck, int *outStatus,
                                       int *outDep, const char *account);
CanLoad_t CanLoad() { return reinterpret_cast<CanLoad_t>(Offsets::FUN_ADDON_CAN_LOAD); }

// Real-load checks in progress, so their dependency recursion is gated too.
int g_realLoadDepth = 0;

// The loader's and LoadAddOn's check: a real load.
uint8_t __fastcall CanLoadForLoad(const char *name, char fullCheck, int *outStatus,
                                  int *outDep, const char *account) {
    if (const int reason = ReasonFor(name)) {
        *outStatus = reason;
        *outDep = 0;
        return 0;
    }
    ++g_realLoadDepth;
    const uint8_t ok = CanLoad()(name, fullCheck, outStatus, outDep, account);
    --g_realLoadDepth;
    return ok;
}

// The check's own required-dependency recursion. Both out pointers are the
// parent's dependency slot, so only the status is written: the parent then
// reports `DEP_<reason>`.
uint8_t __fastcall CanLoadForDependency(const char *name, char fullCheck, int *outStatus,
                                        int *outDep, const char *account) {
    if (g_realLoadDepth > 0) {
        if (const int reason = ReasonFor(name)) {
            *outStatus = reason;
            return 0;
        }
    }
    return CanLoad()(name, fullCheck, outStatus, outDep, account);
}

// FUN_ADDON_FORMAT_REASON at Script_LoadAddOn's call: names the new codes,
// directly or as `DEP_<name>` (the engine's own "DEP_%s" at 0x853900).
using FormatReason_t = char *(__fastcall *)(int code, int depCode, char *buf, unsigned cap);
char *__fastcall FormatReason(int code, int depCode, char *buf, unsigned cap) {
    if (const char *name = ReasonName(code)) {
        Common::BoundedCopy(buf, name, cap);
        return buf;
    }
    if (code == 0) {
        if (const char *name = ReasonName(depCode)) {
            std::snprintf(buf, cap, "DEP_%s", name);
            return buf;
        }
    }
    return reinterpret_cast<FormatReason_t>(Offsets::FUN_ADDON_FORMAT_REASON)(code, depCode,
                                                                             buf, cap);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_ADDON_LOAD_ONE_CAN_LOAD, Offsets::FUN_ADDON_CAN_LOAD,
                         reinterpret_cast<const void *>(&CanLoadForLoad));
    Common::RedirectCall(Offsets::CALL_LOAD_ADDON_CAN_LOAD, Offsets::FUN_ADDON_CAN_LOAD,
                         reinterpret_cast<const void *>(&CanLoadForLoad));
    Common::RedirectCall(Offsets::CALL_ADDON_CAN_LOAD_DEPENDENCY, Offsets::FUN_ADDON_CAN_LOAD,
                         reinterpret_cast<const void *>(&CanLoadForDependency));
    Common::RedirectCall(Offsets::CALL_LOAD_ADDON_FORMAT_REASON,
                         Offsets::FUN_ADDON_FORMAT_REASON,
                         reinterpret_cast<const void *>(&FormatReason));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

void Record(const char *name, size_t nameLen, uint32_t gameTypes, uint32_t allowLoad) {
    if (name == nullptr || nameLen == 0) return;
    Gate *g = Find(name, nameLen);
    if (gameTypes == 0 && (allowLoad & ALLOW_LOAD_GAME) != 0) { // no gate
        if (g != nullptr) g_gates.erase(g_gates.begin() + (g - g_gates.data()));
        return;
    }
    if (g == nullptr) {
        g_gates.push_back({std::string(name, nameLen), gameTypes, allowLoad});
        return;
    }
    g->gameTypes = gameTypes;
    g->allowLoad = allowLoad;
}

} // namespace AddOns::LoadGate
