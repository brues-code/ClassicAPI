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

// C_AddOnProfiler's readers, as 3.80.2 registers them (FUN_140b21fe0):
// IsEnabled always answers true (FUN_140b21f10); the metric is the generated
// enum reader (FUN_140B27620, 0 to 11), `name` a cstring and `k` an int32.
// GetAddOnMetric answers 0 for a name with no record.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "lua/Arg.h"
#include "profile/AddOnProfiler.h"

namespace Profile::AddOnProfilerApi {

namespace {

namespace Arg = Game::Lua::Arg;
using namespace Profile::AddOnProfiler;

constexpr const char *kAddOnUsage = "Usage: local result = C_AddOnProfiler.GetAddOnMetric(name, metric)";
constexpr const char *kApplicationUsage = "Usage: local result = C_AddOnProfiler.GetApplicationMetric(metric)";
constexpr const char *kOverallUsage = "Usage: local result = C_AddOnProfiler.GetOverallMetric(metric)";
constexpr const char *kTopKUsage = "Usage: local results = C_AddOnProfiler.GetTopKAddOnsForMetric(metric, k)";

Metric ReadMetric(void *L, int arg, const char *usage) {
    uint32_t metric = 0;
    Arg::Enum(L, arg, usage, MetricMax, metric);
    return static_cast<Metric>(metric);
}

int __fastcall Script_GetAddOnMetric(void *L) {
    Arg::Text name;
    Arg::String(L, 1, kAddOnUsage, name);
    const Metric metric = ReadMetric(L, 2, kAddOnUsage);
    Game::Lua::PushNumber(L, AddOnMetric(name.data, metric));
    return 1;
}

int __fastcall Script_GetApplicationMetric(void *L) {
    Game::Lua::PushNumber(L, ApplicationMetric(ReadMetric(L, 1, kApplicationUsage)));
    return 1;
}

int __fastcall Script_GetOverallMetric(void *L) {
    Game::Lua::PushNumber(L, OverallMetric(ReadMetric(L, 1, kOverallUsage)));
    return 1;
}

int __fastcall Script_GetTopKAddOnsForMetric(void *L) {
    const Metric metric = ReadMetric(L, 1, kTopKUsage);
    int32_t k = 0;
    Arg::Int32(L, 2, kTopKUsage, k);
    Game::Lua::CreateTable(L, 0, 0);
    int index = 0;
    TopK(metric, k, [L, &index](const char *name, double value) {
        Game::Lua::CreateTable(L, 0, 2);
        Game::Lua::SetFieldString(L, "addOnName", name);
        Game::Lua::SetFieldNumber(L, "metricValue", value);
        Game::Lua::RawSetI(L, -2, ++index);
    });
    return 1;
}

int __fastcall Script_GetTicksPerSecond(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(TicksPerSecond()));
    return 1;
}

int __fastcall Script_IsEnabled(void *L) {
    Game::Lua::PushBoolean(L, true);
    return 1;
}

const Game::Doc::Field kMetricArg[] = {
    Game::Doc::Req("metric", "AddOnProfilerMetric"),
};
const Game::Doc::Field kAddOnArgs[] = {
    Game::Doc::Req("name", "cstring", "An addon's folder name, matched exactly."),
    Game::Doc::Req("metric", "AddOnProfilerMetric"),
};
const Game::Doc::Field kResultRet[] = {
    Game::Doc::Req("result", "number", "Milliseconds, or a count of ticks for the CountTimeOver metrics."),
};
const Game::Doc::Function kGetAddOnMetric{"Returns a metric of the time an addon's scripts have run.", kAddOnArgs,
                                          kResultRet};
const Game::Doc::Function kGetApplicationMetric{"Returns a metric of the time each frame takes.", kMetricArg,
                                                kResultRet};
const Game::Doc::Function kGetOverallMetric{"Returns a metric of the time all addons' scripts have run together.",
                                            kMetricArg, kResultRet};

const Game::Doc::Field kTopKArgs[] = {
    Game::Doc::Req("metric", "AddOnProfilerMetric"),
    Game::Doc::Req("k", "number", "How many addons to return; a negative number returns every addon."),
};
const Game::Doc::Field kTopKRets[] = {
    Game::Doc::Req("results", "table", "AddOnProfilerResult entries, largest value first."),
};
const Game::Doc::Function kGetTopKAddOnsForMetric{"Returns the addons with the largest values of a metric.",
                                                  kTopKArgs, kTopKRets};

const Game::Doc::Field kResultFields[] = {
    Game::Doc::Req("addOnName", "cstring"),
    Game::Doc::Req("metricValue", "number"),
};
const Game::Doc::Structure kResultStructure{"AddOnProfilerResult", "AddOnProfiler", kResultFields};

const Game::Doc::Field kTicksRets[] = {
    Game::Doc::Req("frequency", "BigInteger", "Ticks of the profiling clock per second."),
};
const Game::Doc::Function kGetTicksPerSecond{"Returns the speed of the profiling clock.", {}, kTicksRets};

const Game::Doc::Field kEnabledRets[] = {
    Game::Doc::Req("enabled", "bool"),
};
const Game::Doc::Function kIsEnabled{"Returns whether addon profiling is on; it always is.", {}, kEnabledRets};

constexpr Game::Lua::EnumIntegerEntry kMetricEntries[] = {
    {"SessionAverageTime", SessionAverageTime},
    {"RecentAverageTime", RecentAverageTime},
    {"EncounterAverageTime", EncounterAverageTime},
    {"LastTime", LastTime},
    {"PeakTime", PeakTime},
    {"CountTimeOver1Ms", CountTimeOver1Ms},
    {"CountTimeOver5Ms", CountTimeOver5Ms},
    {"CountTimeOver10Ms", CountTimeOver10Ms},
    {"CountTimeOver50Ms", CountTimeOver50Ms},
    {"CountTimeOver100Ms", CountTimeOver100Ms},
    {"CountTimeOver500Ms", CountTimeOver500Ms},
    {"CountTimeOver1000Ms", CountTimeOver1000Ms},
};

void RegisterLuaFunctions() {
    // The addon list's warning threshold, a fraction of the frame's time (3.80.2
    // registers it beside the profiler's data, FUN_1401aa160). 0 shows no
    // warning.
    CVar::Factory::Register("addonPerformanceMsgWarning",
                            "Threshold for when a performance warning is shown for a specific AddOn as a "
                            "percentage of application performance.",
                            Offsets::CVAR_CATEGORY_GAME, "0");
    Game::Lua::RegisterTableFunction("C_AddOnProfiler", "GetAddOnMetric", &Script_GetAddOnMetric,
                                     &kGetAddOnMetric);
    Game::Lua::RegisterTableFunction("C_AddOnProfiler", "GetApplicationMetric", &Script_GetApplicationMetric,
                                     &kGetApplicationMetric);
    Game::Lua::RegisterTableFunction("C_AddOnProfiler", "GetOverallMetric", &Script_GetOverallMetric,
                                     &kGetOverallMetric);
    Game::Lua::RegisterTableFunction("C_AddOnProfiler", "GetTopKAddOnsForMetric", &Script_GetTopKAddOnsForMetric,
                                     &kGetTopKAddOnsForMetric);
    Game::Lua::RegisterTableFunction("C_AddOnProfiler", "GetTicksPerSecond", &Script_GetTicksPerSecond,
                                     &kGetTicksPerSecond);
    Game::Lua::RegisterTableFunction("C_AddOnProfiler", "IsEnabled", &Script_IsEnabled, &kIsEnabled);
    Game::Lua::RegisterIntegerEnum("Enum", "AddOnProfilerMetric", kMetricEntries, "AddOnProfiler");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Profile::AddOnProfilerApi
