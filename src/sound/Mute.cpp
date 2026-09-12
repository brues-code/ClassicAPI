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

// `MuteSoundFile(path)` / `UnmuteSoundFile(path)` — stop a specific sound
// file from playing — plus `C_Sound.GetRecentSoundFiles()`, which reports
// what has played lately so a caller can find the path to mute.
//
// Blizzard's pair takes a file id or a path; only the path form is
// meaningful here, since file ids are a later scheme. The path is the one
// the engine itself uses, e.g. "Sound\Creature\Ragnaros\RagnarosAggro01.wav".
//
// HOW: one co-hook on FUN_SOUND_PLAY_BY_PATH, the single funnel every sound
// passes through (see Offsets.h for why there is no second route — this
// client links only FMOD's streaming API, so even a one-shot UI click is a
// stream opened by path). A muted path returns NULL, which is the engine's
// OWN "this did not play" answer: its pre-play gate `FUN_007A66A0` already
// produces NULL for a category that is at its concurrency cap, so every
// caller in the binary already handles it, and `PlaySoundFile` already
// reports it to Lua as nil. Muting therefore needs no new failure path.
//
// Suppressing the open rather than muting the opened stream is deliberate:
// nothing is decoded for a sound nobody hears. The one visible difference
// from a true mute is that `PlaySoundFile` returns nil for a muted file
// instead of 1.
//
// WHY THE RECENT LIST IS ALWAYS ON: the hook exists for muting regardless,
// and recording costs one bounded copy into a preallocated slot on a call
// that is about to hash a path, open an archive file, decompress it and
// hand it to FMOD — three or four orders of magnitude more work. Play rate
// is bounded too: the engine caps concurrent streams per category, so this
// is tens per second at worst, never per-frame. A switch would guard a cost
// that is not there, and would have to be armed BEFORE hearing the sound
// you wanted to identify, which is exactly when you cannot know to arm it.
//
// Muted entries stay in the list, flagged. Dropping them would make a sound
// vanish from the one view you would use to un-mute it.

#include "Game.h"
#include "Offsets.h"
#include "time/Clock.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>

namespace Sound::Mute {

namespace {

// Paths are at most 260 bytes elsewhere in the engine's file layer; the
// longest this client actually ships is well under half that.
constexpr size_t kMaxPath = 260;

// How far back the recent list reaches. Deep enough that a busy pull cannot
// flush the sound you just heard before you can read it, while staying a
// fixed ~17 KB with no allocation on the hook path.
constexpr int kRecentSlots = 64;

struct Recent {
    char path[kMaxPath];
    uint32_t seq;    // insertion order; 0 = slot never used
    uint32_t timeMs; // engine tick, GetTime()-comparable once scaled
    bool muted;
};

Recent g_recent[kRecentSlots];
uint32_t g_seq = 0;

// Muted paths, normalized. Empty in the overwhelmingly common case, which
// is one `empty()` test on the hook path.
std::unordered_set<std::string> g_muted;

// Lowercase, and fold '/' to '\', so a caller may pass either separator and
// any casing — the engine's own path compares are case-insensitive.
void Normalize(const char *path, std::string &out) {
    out.clear();
    for (const char *p = path; *p != '\0'; ++p) {
        char c = *p;
        if (c == '/')
            c = '\\';
        else if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
        out.push_back(c);
    }
}

bool IsMuted(const char *path) {
    if (g_muted.empty())
        return false;
    std::string key;
    Normalize(path, key);
    return g_muted.find(key) != g_muted.end();
}

// Case-insensitive equality, tolerant of separator style — the same rule
// Normalize applies, without building a string.
bool SamePath(const char *a, const char *b) {
    for (;; ++a, ++b) {
        char ca = *a, cb = *b;
        if (ca == '/') ca = '\\';
        if (cb == '/') cb = '\\';
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + ('a' - 'A'));
        if (ca != cb)
            return false;
        if (ca == '\0')
            return true;
    }
}

// Note a path as played. Re-playing a path refreshes its existing slot
// rather than taking a new one, so a looping footstep cannot push the rest
// of the window out — the list holds the last N DISTINCT sounds, which is
// what makes it usable for identifying one.
void Record(const char *path, bool muted) {
    const uint32_t now = Time::Clock::NowMs();
    int oldest = 0;
    for (int i = 0; i < kRecentSlots; ++i) {
        Recent &r = g_recent[i];
        if (r.seq != 0 && SamePath(r.path, path)) {
            r.seq = ++g_seq;
            r.timeMs = now;
            r.muted = muted;
            return;
        }
        if (r.seq < g_recent[oldest].seq)
            oldest = i; // an unused slot (seq 0) wins this outright
    }
    Recent &r = g_recent[oldest];
    const size_t n = std::strlen(path);
    const size_t copy = (n < kMaxPath) ? n : kMaxPath - 1;
    std::memcpy(r.path, path, copy);
    r.path[copy] = '\0';
    r.seq = ++g_seq;
    r.timeMs = now;
    r.muted = muted;
}

using PlayByPath_t = void *(__fastcall *)(int category, const char *path,
                                          unsigned flags, char deferStart);
PlayByPath_t g_playByPath = nullptr;

void *__fastcall PlayByPath_h(int category, const char *path, unsigned flags,
                              char deferStart) {
    if (path != nullptr && *path != '\0') {
        const bool muted = IsMuted(path);
        Record(path, muted);
        if (muted)
            return nullptr; // exactly what the engine's own gate returns
    }
    return g_playByPath(category, path, flags, deferStart);
}

// MuteSoundFile(path) -> true when the path is now muted, false for a bad
// argument. Muting a path that is already muted is a no-op, not an error.
int __fastcall Script_MuteSoundFile(void *L) {
    const char *path = Game::Lua::ToString(L, 1);
    if (path == nullptr || *path == '\0') {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    std::string key;
    Normalize(path, key);
    g_muted.insert(key);
    Game::Lua::PushBool(L, true);
    return 1;
}

// UnmuteSoundFile(path) -> true when the path had been muted.
int __fastcall Script_UnmuteSoundFile(void *L) {
    const char *path = Game::Lua::ToString(L, 1);
    if (path == nullptr || *path == '\0') {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    std::string key;
    Normalize(path, key);
    Game::Lua::PushBool(L, g_muted.erase(key) != 0);
    return 1;
}

// C_Sound.GetRecentSoundFiles() -> array of { file, time, muted }, newest
// first. `time` is milliseconds on GetTime()'s epoch, so
// `GetTime() * 1000 - time` is how long ago the sound played.
int __fastcall Script_GetRecentSoundFiles(void *L) {
    // Selection-sort the used slots by seq, newest first. At most 64
    // entries, and only when Lua asks.
    int order[kRecentSlots];
    int used = 0;
    for (int i = 0; i < kRecentSlots; ++i)
        if (g_recent[i].seq != 0)
            order[used++] = i;
    for (int i = 0; i < used; ++i) {
        int best = i;
        for (int j = i + 1; j < used; ++j)
            if (g_recent[order[j]].seq > g_recent[order[best]].seq)
                best = j;
        const int t = order[i];
        order[i] = order[best];
        order[best] = t;
    }

    Game::Lua::NewTable(L); // [list]
    for (int i = 0; i < used; ++i) {
        const Recent &r = g_recent[order[i]];
        Game::Lua::PushNumber(L, static_cast<double>(i + 1)); // [list, idx]
        Game::Lua::NewTable(L);                               // [list, idx, entry]
        Game::Lua::SetFieldString(L, "file", r.path);
        Game::Lua::SetFieldNumber(L, "time", static_cast<double>(r.timeMs));
        Game::Lua::SetFieldBool(L, "muted", r.muted);
        Game::Lua::RawSet(L, -3); // list[idx] = entry.  [list]
    }
    return 1;
}

void RegisterLuaFunctions() {
    // Blizzard's own names are plain globals, so these are too.
    Game::Lua::RegisterGlobalFunction("MuteSoundFile", &Script_MuteSoundFile);
    Game::Lua::RegisterGlobalFunction("UnmuteSoundFile", &Script_UnmuteSoundFile);
    Game::Lua::RegisterTableFunction("C_Sound", "GetRecentSoundFiles",
                                     &Script_GetRecentSoundFiles);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::HookAutoRegister _hook{Offsets::FUN_SOUND_PLAY_BY_PATH,
                                   reinterpret_cast<void *>(&PlayByPath_h),
                                   reinterpret_cast<void **>(&g_playByPath)};

} // namespace

} // namespace Sound::Mute
