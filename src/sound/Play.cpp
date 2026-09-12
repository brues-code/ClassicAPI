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

// `C_Sound.PlaySound(soundKitID [, channel [, forceNoDuplicates
// [, runFinishCallback]]])`, `C_Sound.IsPlaying(soundHandle)`, the
// `SOUNDKIT_FINISHED` event, and the global `PlaySound` widened to take a
// SoundKitID as well as the row name it has always taken.
//
// A SoundKitID is a SoundEntries.dbc row id — the same ids the modern
// client's SOUNDKIT constants carry, because it is the same table under a
// later name (850 = igMainMenuOpen, 8959 = RaidWarning, 8960 = ReadyCheck,
// all matching). This build has 8803 rows with ids from 3 to 60769.
//
// Playing by id is the capability this adds. The engine has always resolved
// a sound by row and played it by id internally — `PlaySound(name)` hashes
// the name to a row and its tail (`FUN_00458850`) calls the by-id player —
// but the only Lua door was that name string. So an addon written against
// SOUNDKIT ids could not play a single sound here, even though every id was
// already valid.
//
// `FUN_SOUND_PLAY_ENTRY` does the work and returns the stream object, which
// is the handle. See Offsets.h for its ABI, which was taken from the
// disassembly rather than the decompile (Ghidra maps its arguments
// inconsistently against the call site). We pass variantIndex -1 so a row
// with several files picks one the same weighted-random way the engine does
// for its own sounds, and leave all three flag bytes clear, matching
// `PlaySound`.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"

#include <cstdint>
#include <vector>

namespace Sound::Play {

namespace {

// `void *__fastcall(char orFlag4, char orFlag10, int category,
//                   int soundEntryID, int variantIndex, int orFlag2)`
using PlaySoundEntry_t = void *(__fastcall *)(char, char, int, int, int, int);
using ScriptFn_t = int(__fastcall *)(void *L);

constexpr int kVariantRandom = -1; // the engine's own weighted pick

// A stream object is freed once the sound ends, so a stale handle must
// never be dereferenced. Membership of the engine's live stream list is the
// safe test — the same list its dedup pass walks (`FUN_007A66A0`): an
// intrusive list whose `next` sits at +0x04, ended by a null or a
// low-bit-tagged sentinel. Returns the node so a caller that needs a field
// off it reads only a node that is provably still there.
uintptr_t FindStreamNode(uintptr_t handle) {
    if (handle == 0)
        return 0;
    uintptr_t node = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_SOUND_STREAM_LIST_HEAD));
    for (int guard = 0; guard < 4096; ++guard) {
        if (node == 0 || (node & 1) != 0)
            return 0;
        if (node == handle)
            return node;
        node = *reinterpret_cast<const uintptr_t *>(
            node + Offsets::OFF_SOUND_STREAM_NEXT);
    }
    return 0; // cycle or corruption — never spin inside a Lua call
}

bool HandleAlive(uintptr_t handle) { return FindStreamNode(handle) != 0; }

uint32_t PathHash(uintptr_t node) {
    return *reinterpret_cast<const uint32_t *>(
        node + Offsets::OFF_SOUND_STREAM_PATH_HASH);
}

// Re-apply a row's volume to a already-started stream at `scale`. The by-id
// player runs this with 1.0 as it starts the sound, and the setter writes
// rather than accumulates, so a second call simply replaces the volume.
using SoundEntryForID_t = void *(__fastcall *)(unsigned soundEntryID);
using ApplyRowVolume_t = void(__thiscall *)(void *row, void *stream, float scale);

void ApplyVolumeScale(int soundKitID, void *handle, float scale) {
    void *row = reinterpret_cast<SoundEntryForID_t>(Offsets::FUN_SOUND_ENTRY_FOR_ID)(
        static_cast<unsigned>(soundKitID));
    if (row == nullptr)
        return;
    reinterpret_cast<ApplyRowVolume_t>(Offsets::FUN_SOUND_APPLY_ROW_VOLUME)(row, handle, scale);
}

// ---- SOUNDKIT_FINISHED ------------------------------------------------
//
// Fires with the sound handle once that sound stops, and ONLY for sounds
// started with `runFinishCallback` — which is both the documented contract
// and the reason this is affordable. Without the opt-in every footstep and
// UI click would dispatch a Lua event.
//
// Detected on the world tick rather than from FMOD's end callback: that
// callback (`FUN_007A53C0`) runs on the STREAMER thread and only defers the
// stream onto a list under a critical section. Firing a Lua event from
// there would call into the VM off the main thread. The engine notices
// completion on its own side in `FUN_007A4B60`, from the per-frame sound
// update — so a main-thread check is what the engine itself does.
//
// A tracked entry keeps the stream's path hash alongside the handle.
// Stream objects are pooled and their addresses reused, so "the handle is
// still in the list" is not enough on its own: a later sound can land on
// the same address. Comparing the hash too means only the same file
// replaying at the same address can be mistaken for the original, and that
// costs one extra dword read of a node we have already found alive.
const Event::Custom::AutoReserve _soundkitFinished{"SOUNDKIT_FINISHED"};

struct Tracked {
    uintptr_t handle;
    uint32_t pathHash;
};

// Only ever holds sounds a caller opted in for, so it is empty almost
// always and never needs a cap.
std::vector<Tracked> g_tracked;

void TrackForFinish(uintptr_t handle) {
    const uintptr_t node = FindStreamNode(handle);
    if (node == 0)
        return; // already over, or never started — nothing to report
    g_tracked.push_back({handle, PathHash(node)});
}

void OnWorldTick() {
    if (g_tracked.empty())
        return;
    const int slot = _soundkitFinished.Slot();
    size_t keep = 0;
    for (size_t i = 0; i < g_tracked.size(); ++i) {
        const Tracked &t = g_tracked[i];
        const uintptr_t node = FindStreamNode(t.handle);
        if (node != 0 && PathHash(node) == t.pathHash) {
            g_tracked[keep++] = t; // still playing
            continue;
        }
        if (slot >= 0)
            Event::Custom::Fire(slot, "%u", static_cast<unsigned>(t.handle));
    }
    g_tracked.resize(keep);
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

// Play a row by id and push `willPlay, soundHandle`.
//
// `optionalBase` is the 1-based stack index of the `channel` argument, with
// `forceNoDuplicates` and `runFinishCallback` after it — or 0 for a caller
// that takes none of them. The global `PlaySound` passes 0: it gains only
// the id form, and the fuller argument list belongs to `C_Sound.PlaySound`.
//
// `channel` is the engine's sound category (0..12; 0 is what the engine
// itself uses for UI sounds and music, 3 for a file played by path). A
// channel NAME is accepted and ignored, so code written against the modern
// string form still runs — the categories here do not correspond to it.
//
// `forceNoDuplicates` is likewise accepted and does nothing. The engine
// does have a by-path dedup, but it is bit 0 of a flag word the row itself
// supplies; the by-id player ORs in only bits 1, 2 and 4 from its
// arguments, so there is no way to ask for it without bypassing that
// player and re-deriving its weighted file pick, which would be a worse
// trade than the missing flag.
//
// `runFinishCallback` opts the sound in to SOUNDKIT_FINISHED.
// The options every play path shares, however they were spelled at the
// Lua edge. `volumeScale < 0` means "leave the row's own volume alone".
struct PlayOptions {
    int category = 0;
    bool runFinishCallback = false;
    float volumeScale = -1.0f;
};

// Start `soundKitID` with `opts` and push `willPlay, soundHandle`.
int PlayWithOptions(void *L, int soundKitID, const PlayOptions &opts) {
    if (soundKitID <= 0) {
        Game::Lua::PushBool(L, false);
        Game::Lua::PushNil(L);
        return 2;
    }

    auto play = reinterpret_cast<PlaySoundEntry_t>(Offsets::FUN_SOUND_PLAY_ENTRY);
    void *handle = play(0, 0, opts.category, soundKitID, kVariantRandom, 0);

    if (handle != nullptr) {
        if (opts.volumeScale >= 0.0f)
            ApplyVolumeScale(soundKitID, handle, opts.volumeScale);
        if (opts.runFinishCallback)
            TrackForFinish(reinterpret_cast<uintptr_t>(handle));
    }

    Game::Lua::PushBool(L, handle != nullptr);
    if (handle != nullptr)
        Game::Lua::PushNumber(L, static_cast<double>(reinterpret_cast<uintptr_t>(handle)));
    else
        Game::Lua::PushNil(L);
    return 2;
}

int PlayById(void *L, int soundKitID, int optionalBase) {
    PlayOptions opts;
    if (optionalBase > 0) {
        if (Game::Lua::Type(L, optionalBase) == Game::Lua::TYPE_NUMBER) {
            opts.category = static_cast<int>(Game::Lua::ToNumber(L, optionalBase));
            if (opts.category < 0 || opts.category > Offsets::SOUND_CATEGORY_MAX)
                opts.category = 0; // the engine rejects the rest outright
        }
        opts.runFinishCallback = Game::Lua::ToBoolean(L, optionalBase + 2) != 0;
    }
    return PlayWithOptions(L, soundKitID, opts);
}

// C_Sound.PlaySound(soundKitID [, channel [, forceNoDuplicates
//                   [, runFinishCallback]]]) -> willPlay, soundHandle
int __fastcall Script_C_Sound_PlaySound(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_NUMBER) {
        Game::Lua::Error(L,
            "Usage: C_Sound.PlaySound(soundKitID [, channel [, forceNoDuplicates "
            "[, runFinishCallback]]])");
        return 0;
    }
    return PlayById(L, static_cast<int>(Game::Lua::ToNumber(L, 1)), 2);
}

// The global `PlaySound(soundKitID | soundName)`.
//
// The engine's own `PlaySound` takes a SoundEntries row NAME
// ("igMainMenuOpen"); the modern one takes that row's id. Both now work:
// a NUMBER plays by id and returns `willPlay, soundHandle`, and anything
// else tail-calls the engine with the stack untouched, so the name form
// behaves exactly as it always has (it pushes no return values).
//
// The id form is the ONLY thing added here — the optional channel /
// forceNoDuplicates / runFinishCallback arguments live on
// `C_Sound.PlaySound` instead, which keeps this global as close to the
// one it replaces as the extension allows.
//
// Discriminating on `lua_type`, not `lua_isnumber`: the latter also
// accepts a numeric STRING, which would quietly reroute `PlaySound("123")`
// away from the name lookup. No row in this table is named with digits, so
// nothing real is lost, but the type test is the honest rule.
int __fastcall Script_PlaySound(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_NUMBER)
        return PlayById(L, static_cast<int>(Game::Lua::ToNumber(L, 1)), 0);
    return reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_PLAY_SOUND)(L);
}

// Read `name` from the params table at stack index `idx`. Uses GetTable
// rather than a raw read so a params table built from a mixin still works.
// Leaves the stack as it found it.
bool Field(void *L, int idx, const char *name) {
    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, idx);
    return true; // value is on the stack; caller pops
}

double FieldNumber(void *L, int idx, const char *name, double fallback) {
    Field(L, idx, name);
    const double v = (Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER)
                         ? Game::Lua::ToNumber(L, -1)
                         : fallback;
    Game::Lua::SetTop(L, -2);
    return v;
}

bool FieldBool(void *L, int idx, const char *name) {
    Field(L, idx, name);
    const bool v = Game::Lua::ToBoolean(L, -1) != 0;
    Game::Lua::SetTop(L, -2);
    return v;
}

// C_Sound.PlaySoundWithOptions(params) -> success, soundHandle
//
// The table form of PlaySound. Honors `soundKitID`, `runFinishCallback`
// and `volumeOverride`; accepts and ignores `uiSoundSubType`,
// `forceNoDuplicates` and `overridePriority`, which have nothing behind
// them here (see PlayById for why forceNoDuplicates cannot be asked for).
//
// `volumeOverride` scales the sound's own volume, so 1.0 is unchanged and
// 0 is silent.
int __fastcall Script_PlaySoundWithOptions(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: C_Sound.PlaySoundWithOptions(params)");
        return 0;
    }
    const int soundKitID = static_cast<int>(FieldNumber(L, 1, "soundKitID", 0));

    PlayOptions opts;
    opts.runFinishCallback = FieldBool(L, 1, "runFinishCallback");
    const double volume = FieldNumber(L, 1, "volumeOverride", -1.0);
    if (volume >= 0.0)
        opts.volumeScale = static_cast<float>(volume);

    return PlayWithOptions(L, soundKitID, opts);
}

// C_Sound.GetSoundScaledVolume(soundHandle) -> number
//
// The volume the sound is playing at: the sound's own volume after any
// scaling. Reads the field only off a stream still in the live list, since
// a finished stream's memory goes back to a pool.
int __fastcall Script_GetSoundScaledVolume(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uintptr_t node =
        FindStreamNode(static_cast<uintptr_t>(Game::Lua::ToNumber(L, 1)));
    if (node == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const float volume = *reinterpret_cast<const float *>(
        node + Offsets::OFF_SOUND_STREAM_VOLUME);
    Game::Lua::PushNumber(L, static_cast<double>(volume));
    return 1;
}

// C_Sound.IsPlaying(soundHandle) -> bool
int __fastcall Script_IsPlaying(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const uintptr_t handle =
        static_cast<uintptr_t>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(L, HandleAlive(handle));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Sound", "PlaySound", &Script_C_Sound_PlaySound);
    Game::Lua::RegisterTableFunction("C_Sound", "PlaySoundWithOptions",
                                     &Script_PlaySoundWithOptions);
    Game::Lua::RegisterTableFunction("C_Sound", "GetSoundScaledVolume",
                                     &Script_GetSoundScaledVolume);
    Game::Lua::RegisterTableFunction("C_Sound", "IsPlaying", &Script_IsPlaying);
    // A strict superset of the engine's own global — see Script_PlaySound.
    Game::Lua::RegisterGlobalFunction("PlaySound", &Script_PlaySound);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::Play
