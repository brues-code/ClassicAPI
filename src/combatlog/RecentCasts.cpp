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

#include "combatlog/RecentCasts.h"

#include "combatlog/Entry.h"
#include "spell/Lookup.h"
#include "time/Clock.h"

#include <cstdint>

namespace CombatLog::RecentCasts {

namespace {

// A cast is of use for the tick that follows its go; the window is a server
// tick with room for the messages of one tick to reach the client in two
// frames. A match beyond it would be a later cast of the same kind.
constexpr uint32_t kWindowMs = 1000;
constexpr int kRecords = 16;

struct Cast {
    uint32_t timeMs = 0;
    uint64_t caster = 0;
    uint32_t spellId = 0;
    int hitCount = 0;
    uint64_t hits[MAX_HITS] = {};
};

Cast g_casts[kRecords];
int g_next = 0; // the slot the next record takes; the newest is the one before

bool Hit(const Cast &c, uint64_t target) {
    for (int i = 0; i < c.hitCount; ++i) {
        if (c.hits[i] == target)
            return true;
    }
    return false;
}

// Visits the records newest first while `fn` returns false.
template <typename Fn> void ForEachRecent(Fn &&fn) {
    const uint32_t now = Time::Clock::NowMs();
    for (int i = 0; i < kRecords; ++i) {
        const Cast &c = g_casts[(g_next - 1 - i + kRecords) % kRecords];
        if (c.spellId == 0 || Time::Clock::Elapsed(c.timeMs, now) > kWindowMs)
            return;
        if (fn(c))
            return;
    }
}

} // namespace

void Record(uint64_t caster, uint32_t spellId, const uint64_t *hits, int count) {
    if (spellId == 0 || count <= 0)
        return;
    Cast &c = g_casts[g_next];
    g_next = (g_next + 1) % kRecords;
    c.timeMs = Time::Clock::NowMs();
    c.caster = caster;
    c.spellId = spellId;
    c.hitCount = count < MAX_HITS ? count : MAX_HITS;
    for (int i = 0; i < c.hitCount; ++i)
        c.hits[i] = hits[i];
}

const uint8_t *SpellByEffect(uint64_t caster, uint64_t target, int effectA, int effectB) {
    const uint8_t *found = nullptr;
    ForEachRecent([&](const Cast &c) {
        if (c.caster != caster || !Hit(c, target))
            return false;
        const uint8_t *spell = SpellRecord(static_cast<int>(c.spellId));
        if (!Spell::Lookup::HasEffect(spell, effectA) && !Spell::Lookup::HasEffect(spell, effectB))
            return false;
        found = spell;
        return true;
    });
    return found;
}

uint64_t CasterOf(uint32_t spellId, uint64_t target) {
    uint64_t found = 0;
    ForEachRecent([&](const Cast &c) {
        if (c.spellId != spellId || !Hit(c, target))
            return false;
        found = c.caster;
        return true;
    });
    return found;
}

} // namespace CombatLog::RecentCasts
