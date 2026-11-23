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

// 3.80.2's AddOnProfiler (docs/notes/addon-profiler-3802.md).
//
// Records (AddOnProfilerData, made by FUN_141b67920): one per addon name, in a
// map that lives until the game shuts down (CGlueMgr::Shutdown 0x141392150
// clears it), so /reload keeps the numbers; the loader FUN_141af7a00 finds the
// record again. Two more of the same shape: the application (the whole frame)
// and the overall (every timed run). The first byte is the record's "user
// addon" flag, the addon entry's +0x4b, which only the folder scan sets
// (0x141af4ba0 -> 0x141af3be0 param 4); a run is timed only for a record with
// it. Here a user addon is one that is not SECURE and not embedded in the DLL.
//
// Owners (0x141b68c30): the record on top of the load stack, else the record
// being timed (DAT_1468d64a8), else none. A script object copies it when it
// is made; so do the callback hosts.
//
// A run (FUN_14058fac0 and ~40 runners like it): timed only when nothing is
// being timed, the load stack is empty and the owner is a user addon. The
// elapsed ticks go to the owner's session total, recent total and this
// tick's ring slot, to its encounter total while an encounter runs
// (DAT_1446e7d85), and to the overall record the same way.
//
// The tick (FUN_141b67fb0), once a frame: the frame's elapsed seconds times
// the clock frequency go to the application record; each record's peak takes
// this tick's ring slot and its counters count the thresholds that slot
// reached (1/5/10/50/100/500/1000 ms, 0x143bb2450); the same for the overall
// and application records; then the tick index advances, and every record
// counts a tick, drops the slot it is about to reuse from its recent total and
// clears it, and counts an encounter tick while an encounter runs. Nothing
// resets the encounter totals: they cover every encounter tick of the
// session.

#include "profile/AddOnProfiler.h"

#include "Game.h"
#include "addons/Embedded.h"
#include "addons/LoadState.h"
#include "addons/Registry.h"
#include "combatlog/Encounter.h"
#include "frame/Construction.h"
#include "frame/Destruction.h"
#include "frame/ScriptArgs.h"
#include "tick/FrameTick.h"

#include <ankerl/unordered_dense.h>

#include <windows.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Profile::AddOnProfiler {

// The size of the recent ring (FUN_141b67d00 resizes it to 0x3c).
constexpr uint64_t kRingSize = 60;
constexpr int kThresholdCount = 7;
constexpr double kThresholdsMs[kThresholdCount] = {1, 5, 10, 50, 100, 500, 1000};

struct Record {
    bool user = false;
    int64_t sessionTotal = 0;
    uint64_t sessionTicks = 1;
    int64_t recentTotal = 0;
    int64_t ring[kRingSize] = {};
    int64_t encounterTotal = 0;
    uint64_t encounterTicks = 1;
    int64_t peak = 0;
    uint64_t over[kThresholdCount] = {};
};

namespace {

int64_t g_frequency = 0;
double g_msPerTick = 0.0;

// The tick index (DAT_144752010).
uint64_t g_tick = 0;

Record g_application;
Record g_overall;

// Keyed by the name as the loader gives it, compared exactly (3.80.2's map
// memcmps). Records never move: nothing is erased.
ankerl::unordered_dense::map<std::string, std::unique_ptr<Record>> g_records;

ankerl::unordered_dense::map<const void *, Record *> g_owners;

// The record being timed (DAT_1468d64a8).
Record *g_timing = nullptr;

int64_t Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int64_t &Slot(Record &r, uint64_t tick) { return r.ring[tick % kRingSize]; }
int64_t Slot(const Record &r, uint64_t tick) { return r.ring[tick % kRingSize]; }

bool IsUserAddOn(const char *name) {
    return Addons::SecurityIndex(name) != 0 && !Addons::Embedded::IsEmbedded(name);
}

// FUN_141b68300's lookup-or-create.
Record *RecordFor(const char *name) {
    auto it = g_records.find(std::string(name));
    if (it != g_records.end())
        return it->second.get();
    auto record = std::make_unique<Record>();
    record->user = IsUserAddOn(name);
    Record *r = record.get();
    g_records.emplace(name, std::move(record));
    return r;
}

void Charge(Record &r, int64_t elapsed, bool encounter) {
    r.sessionTotal += elapsed;
    r.recentTotal += elapsed;
    Slot(r, g_tick) += elapsed;
    if (encounter)
        r.encounterTotal += elapsed;
}

// Starts timing `owner` when the run is the outermost timed one, nothing is
// loading and the owner is a user addon.
bool Begin(Record *owner, int64_t &start) {
    if (g_timing != nullptr || owner == nullptr || !owner->user || AddOns::LoadState::AnyLoading())
        return false;
    g_timing = owner;
    start = Now();
    return true;
}

void Finish(int64_t start) {
    const int64_t elapsed = Now() - start;
    const bool encounter = CombatLog::Encounter::Active();
    Charge(*g_timing, elapsed, encounter);
    Charge(g_overall, elapsed, encounter);
    g_timing = nullptr;
}

Record *OwnerOf(const void *object) {
    const auto it = g_owners.find(object);
    return it != g_owners.end() ? it->second : nullptr;
}

// FUN_141b679b0: this tick's slot against the peak and the thresholds.
void TickPeak(Record &r) {
    const int64_t slot = Slot(r, g_tick);
    if (slot > r.peak)
        r.peak = slot;
    const double ms = g_msPerTick * static_cast<double>(slot);
    for (int i = 0; i < kThresholdCount && ms >= kThresholdsMs[i]; ++i)
        ++r.over[i];
}

void TickAdvance(Record &r, bool encounter) {
    ++r.sessionTicks;
    int64_t &slot = Slot(r, g_tick);
    r.recentTotal -= slot;
    slot = 0;
    if (encounter)
        ++r.encounterTicks;
}

void Tick() {
    if (g_frequency == 0)
        return;
    const bool encounter = CombatLog::Encounter::Active();
    // 3.80.2 multiplies the frequency by the frame's seconds in float.
    const float seconds = Tick::FrameTick::Elapsed();
    const int64_t frame = static_cast<int64_t>(static_cast<float>(g_frequency) * seconds);
    Charge(g_application, frame, encounter);

    for (auto &entry : g_records)
        TickPeak(*entry.second);
    TickPeak(g_overall);
    TickPeak(g_application);

    ++g_tick;
    for (auto &entry : g_records)
        TickAdvance(*entry.second, encounter);
    TickAdvance(g_overall, encounter);
    TickAdvance(g_application, encounter);
}

const Tick::FrameTick::AutoSubscribe _tick{&Tick};

// FUN_141b67af0.
double Read(const Record &r, Metric metric) {
    switch (metric) {
    case SessionAverageTime:
        return static_cast<double>(r.sessionTotal) * g_msPerTick / static_cast<double>(r.sessionTicks);
    case RecentAverageTime:
        return static_cast<double>(r.recentTotal) * g_msPerTick / static_cast<double>(kRingSize);
    case EncounterAverageTime:
        return static_cast<double>(r.encounterTotal) * g_msPerTick / static_cast<double>(r.encounterTicks);
    case LastTime:
        return g_msPerTick * static_cast<double>(Slot(r, g_tick - 1));
    case PeakTime:
        return static_cast<double>(r.peak) * g_msPerTick;
    default:
        if (metric <= MetricMax)
            return static_cast<double>(r.over[metric - CountTimeOver1Ms]);
        return 0.0;
    }
}

// --- Owners -----------------------------------------------------------------

void OnFrameConstructed(void *frame) { Stamp(frame); }
void OnFrameDestroyed(void *frame) { Forget(frame); }

const Frame::Construction::AutoSubscribe _constructed{&OnFrameConstructed};
const Frame::Destruction::AutoSubscribe _destroyed{&OnFrameDestroyed};

// --- Script runs ------------------------------------------------------------

// The observer sees every handler run, nested ones too; the run that began
// timing ends it at the same depth.
int g_runDepth = 0;
int g_timedDepth = 0;
int64_t g_runStart = 0;

void OnRun(bool begin, int /*handlerRef*/, void *frame) {
    if (begin) {
        ++g_runDepth;
        if (g_timedDepth == 0 && Begin(OwnerOf(frame), g_runStart))
            g_timedDepth = g_runDepth;
        return;
    }
    if (g_timedDepth != 0 && g_runDepth == g_timedDepth) {
        Finish(g_runStart);
        g_timedDepth = 0;
    }
    if (g_runDepth > 0)
        --g_runDepth;
}

const Frame::ScriptArgs::AutoObserve _run{&OnRun};

void InitClock() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_frequency = freq.QuadPart;
    g_msPerTick = 1000.0 / static_cast<double>(g_frequency);
}

const struct ClockInit {
    ClockInit() { InitClock(); }
} _clock;

} // namespace

Record *Owner() {
    if (const char *loading = AddOns::LoadState::Current())
        return RecordFor(loading);
    return g_timing;
}

void Stamp(const void *object) {
    Record *owner = Owner();
    if (owner != nullptr)
        g_owners[object] = owner;
    else
        g_owners.erase(object);
}

void Forget(const void *object) { g_owners.erase(object); }

Scope::Scope(Record *owner) {
    if (Begin(owner, start_))
        timed_ = owner;
}

Scope::Scope(const void *object) : Scope(OwnerOf(object)) {}

Scope::~Scope() {
    if (timed_ != nullptr)
        Finish(start_);
}

double AddOnMetric(const char *name, Metric metric) {
    const auto it = g_records.find(std::string(name));
    return it != g_records.end() ? Read(*it->second, metric) : 0.0;
}

double ApplicationMetric(Metric metric) { return Read(g_application, metric); }
double OverallMetric(Metric metric) { return Read(g_overall, metric); }

int64_t TicksPerSecond() { return g_frequency; }

namespace Detail {

// FUN_141b68ea0 keeps a k-deep heap and hands the survivors over largest
// first; a negative k keeps them all.
void TopK(Metric metric, int k, TopKSink sink, void *ctx) {
    std::vector<std::pair<const char *, double>> all;
    all.reserve(g_records.size());
    for (const auto &entry : g_records)
        all.emplace_back(entry.first.c_str(), Read(*entry.second, metric));
    std::stable_sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    if (k >= 0 && static_cast<size_t>(k) < all.size())
        all.resize(static_cast<size_t>(k));
    for (const auto &[name, value] : all)
        sink(name, value, ctx);
}

} // namespace Detail

} // namespace Profile::AddOnProfiler
