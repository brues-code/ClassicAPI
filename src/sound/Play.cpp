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

// `C_Sound.PlaySound(soundKitID [, channel])`,
// `C_Sound.IsPlaying(soundHandle)`, and the global `PlaySound` widened to
// take a SoundKitID as well as the row name it has always taken.
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

#include <cstdint>

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
// low-bit-tagged sentinel.
bool HandleAlive(uintptr_t handle) {
    if (handle == 0)
        return false;
    uintptr_t node = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_SOUND_STREAM_LIST_HEAD));
    for (int guard = 0; guard < 4096; ++guard) {
        if (node == 0 || (node & 1) != 0)
            return false;
        if (node == handle)
            return true;
        node = *reinterpret_cast<const uintptr_t *>(
            node + Offsets::OFF_SOUND_STREAM_NEXT);
    }
    return false; // cycle or corruption — never spin inside a Lua call
}

// Play a row by id and push `willPlay, soundHandle`. `channel` is the
// engine's sound category (0..12; 0 is what the engine itself uses for UI
// sounds and music, 3 for a file played by path). A channel NAME is
// accepted and ignored, so code written against the modern string form
// still runs — the categories here do not correspond to it.
int PlayById(void *L, int soundKitID, int argChannel) {
    if (soundKitID <= 0) {
        Game::Lua::PushBool(L, false);
        Game::Lua::PushNil(L);
        return 2;
    }

    int category = 0;
    if (Game::Lua::Type(L, argChannel) == Game::Lua::TYPE_NUMBER) {
        category = static_cast<int>(Game::Lua::ToNumber(L, argChannel));
        if (category < 0 || category > Offsets::SOUND_CATEGORY_MAX)
            category = 0; // the engine rejects the rest outright
    }

    auto play = reinterpret_cast<PlaySoundEntry_t>(Offsets::FUN_SOUND_PLAY_ENTRY);
    void *handle = play(0, 0, category, soundKitID, kVariantRandom, 0);

    Game::Lua::PushBool(L, handle != nullptr);
    if (handle != nullptr)
        Game::Lua::PushNumber(L, static_cast<double>(reinterpret_cast<uintptr_t>(handle)));
    else
        Game::Lua::PushNil(L);
    return 2;
}

// C_Sound.PlaySound(soundKitID [, channel]) -> willPlay, soundHandle
int __fastcall Script_C_Sound_PlaySound(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_NUMBER) {
        Game::Lua::Error(L, "Usage: C_Sound.PlaySound(soundKitID [, channel])");
        return 0;
    }
    return PlayById(L, static_cast<int>(Game::Lua::ToNumber(L, 1)), 2);
}

// The global `PlaySound(soundKitID | soundName [, channel])`.
//
// The engine's own `PlaySound` takes a SoundEntries row NAME
// ("igMainMenuOpen"); the modern one takes that row's id. Both now work:
// a NUMBER plays by id and returns `willPlay, soundHandle`, and anything
// else tail-calls the engine with the stack untouched, so the name form
// behaves exactly as it always has (it pushes no return values).
//
// Discriminating on `lua_type`, not `lua_isnumber`: the latter also
// accepts a numeric STRING, which would quietly reroute `PlaySound("123")`
// away from the name lookup. No row in this table is named with digits, so
// nothing real is lost, but the type test is the honest rule.
int __fastcall Script_PlaySound(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_NUMBER)
        return PlayById(L, static_cast<int>(Game::Lua::ToNumber(L, 1)), 2);
    return reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_PLAY_SOUND)(L);
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
    Game::Lua::RegisterTableFunction("C_Sound", "IsPlaying", &Script_IsPlaying);
    // A strict superset of the engine's own global — see Script_PlaySound.
    Game::Lua::RegisterGlobalFunction("PlaySound", &Script_PlaySound);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::Play
