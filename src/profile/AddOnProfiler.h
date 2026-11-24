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

#pragma once

#include <cstdint>

// The addon profiler as the 3.80.2 client keeps it (AddOnProfiler,
// docs/notes/addon-profiler-3802.md): per-addon script time, always on,
// charged to the addon that owns the object or callback that ran.
//
// An owner is fixed when the object or callback is made (Owner()): the
// addon being loaded, else the owner of the run being timed, else none.
// Only the outermost run is timed, never while an addon loads, and only for a
// user addon. Frames get their owner here (Frame::Construction); any other
// host of Lua callbacks keeps the Owner() it was made with and runs them
// inside a Scope.
namespace Profile::AddOnProfiler {

struct Record;

// The owner a new object or callback takes now, or nullptr.
Record *Owner();

// Gives `object` (a frame or one of our script objects) the Owner() of now;
// Forget drops it when the object dies. Frames are stamped and forgotten by
// this module itself.
void Stamp(const void *object);
void Forget(const void *object);

// Times the code inside it for `owner` (or for the stamped owner of
// `object`), when it is the outermost timed run.
class Scope {
  public:
    explicit Scope(Record *owner);
    explicit Scope(const void *object);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    Record *timed_ = nullptr;
    int64_t start_ = 0;
};

// Enum.AddOnProfilerMetric.
enum Metric : uint32_t {
    SessionAverageTime = 0,
    RecentAverageTime = 1,
    EncounterAverageTime = 2,
    LastTime = 3,
    PeakTime = 4,
    CountTimeOver1Ms = 5,
    CountTimeOver5Ms,
    CountTimeOver10Ms,
    CountTimeOver50Ms,
    CountTimeOver100Ms,
    CountTimeOver500Ms,
    CountTimeOver1000Ms,
    MetricMax = CountTimeOver1000Ms,
};

// The metric of the addon named `name` (case-sensitive, as 3.80.2's map
// key), 0 for a name with no record.
double AddOnMetric(const char *name, Metric metric);
// The whole frame's time, and every user addon's together.
double ApplicationMetric(Metric metric);
double OverallMetric(Metric metric);

// Calls `fn(name, value)` for the `k` records with the largest `metric`,
// largest first; every record when `k` is negative.
template <typename Fn> void TopK(Metric metric, int k, Fn fn);

// The profiling clock's ticks per second.
int64_t TicksPerSecond();

namespace Detail {
using TopKSink = void (*)(const char *name, double value, void *ctx);
void TopK(Metric metric, int k, TopKSink sink, void *ctx);
} // namespace Detail

template <typename Fn> void TopK(Metric metric, int k, Fn fn) {
    Detail::TopK(
        metric, k, [](const char *name, double value, void *ctx) { (*static_cast<Fn *>(ctx))(name, value); },
        &fn);
}

} // namespace Profile::AddOnProfiler
