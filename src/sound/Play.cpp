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
// [, runFinishCallback]]])`, `C_Sound.PlaySoundWithOptions`,
// `C_Sound.IsPlaying`, `C_Sound.GetSoundScaledVolume`, `StopSound`, the
// `SOUNDKIT_FINISHED` event, the global `PlaySound` widened to take a
// SoundKitID as well as the row name it has always taken, and
// `PlaySoundFile` widened to take a channel and return a handle.
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
// `FUN_SOUND_PLAY_ENTRY` (InternalPlaySound) does the work and returns the
// Sound record, which is the handle. See Offsets.h for its ABI, which was
// taken from the disassembly rather than the decompile. We pass
// variantIndex -1 so a row with several files picks one the same
// weighted-random way the engine does for its own sounds.
//
// CHANNELS are the volume categories, picked the way 4.3.4's
// `_PickScriptSoundBus` picks them: "Master", "Music" and "Ambience"
// (case-insensitive), anything else or nothing is "SFX". Here a category
// is a flag on the sound (Offsets.h, SOUND_FLAG_*), not the bus, which only
// caps concurrency. InternalPlaySound takes the Music and Master flags as
// arguments but has no Ambience argument, so a SoundKit on "Ambience"
// plays on the Sound slider; a FILE on "Ambience" is exact, because
// Play2D takes the flags whole.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace Sound::Play {

namespace {

using PlaySoundEntry_t = void *(__fastcall *)(char keep, char noCategory, int bus,
                                              int soundEntryID, int variantIndex, int music);
using Play2D_t = void *(__fastcall *)(int bus, const char *path, unsigned flags, char held);
using Stop_t = void(__thiscall *)(void *sound, float seconds);
using ScriptFn_t = int(__fastcall *)(void *L);

constexpr int kVariantRandom = -1; // the engine's own weighted pick

// A Sound record is freed once the sound ends, so a stale handle must
// never be dereferenced. Membership of the engine's live list is the safe
// test — the same list its dedup pass walks (`FUN_007A66A0`): an
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

uint32_t PathHash(uintptr_t node) {
    return *reinterpret_cast<const uint32_t *>(
        node + Offsets::OFF_SOUND_STREAM_PATH_HASH);
}

// ---- Sounds we started -------------------------------------------------
//
// Every sound one of our play functions starts is remembered with its
// path hash until it ends. Two things need it:
//
// - StopSound may only stop these. Stopping releases the Sound record, and
//   the engine keeps records of its own in slots (music, ambience, kept
//   sounds with flag 0x4); freeing one of those under its slot would be a
//   use-after-free. A handle we returned was started with no keep flag and
//   is held by nobody but the caller.
// - SOUNDKIT_FINISHED fires for the ones started with `runFinishCallback`.
//
// Sound records are pooled and their addresses reused, so "the handle is
// still in the list" is not enough on its own: a later sound can land on
// the same address. Comparing the path hash too means only the same file
// replaying at the same address can be mistaken for the original.
//
// Ends are detected on the world tick rather than from FMOD's end
// callback: that callback (`FUN_007A53C0`) runs on the STREAMER thread and
// only defers the stream onto a list under a critical section. The engine
// notices completion in `FUN_007A4B60`, from the per-frame sound update —
// so a main-thread check is what the engine itself does.
struct Owned {
    uintptr_t handle;
    uint32_t pathHash;
    bool notifyFinish;
};

// Holds only sounds an addon started that are still playing, so it is a
// handful of entries at most.
std::vector<Owned> g_owned;

void Own(void *sound, bool notifyFinish) {
    const uintptr_t node = FindStreamNode(reinterpret_cast<uintptr_t>(sound));
    if (node == 0)
        return; // already over — nothing to stop or report
    g_owned.push_back({node, PathHash(node), notifyFinish});
}

bool StillPlaying(const Owned &o) {
    const uintptr_t node = FindStreamNode(o.handle);
    return node != 0 && PathHash(node) == o.pathHash;
}

const Event::Custom::AutoReserve _soundkitFinished{"SOUNDKIT_FINISHED"};

void OnWorldTick() {
    if (g_owned.empty())
        return;
    const int slot = _soundkitFinished.Slot();
    size_t keep = 0;
    for (size_t i = 0; i < g_owned.size(); ++i) {
        const Owned &o = g_owned[i];
        if (StillPlaying(o)) {
            g_owned[keep++] = o;
            continue;
        }
        if (o.notifyFinish && slot >= 0)
            Event::Custom::Fire(slot, "%u", static_cast<unsigned>(o.handle));
    }
    g_owned.resize(keep);
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

// ---- Channels ----------------------------------------------------------

enum class Channel { SFX, Master, Music, Ambience };

// 4.3.4 `_PickScriptSoundBus`: a string argument picks by name, anything
// else is SFX.
Channel ChannelArg(void *L, int idx) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING)
        return Channel::SFX;
    const char *name = Game::Lua::ToString(L, idx);
    if (name == nullptr)
        return Channel::SFX;
    if (_stricmp(name, "Master") == 0)
        return Channel::Master;
    if (_stricmp(name, "Music") == 0)
        return Channel::Music;
    if (_stricmp(name, "Ambience") == 0)
        return Channel::Ambience;
    return Channel::SFX;
}

unsigned ChannelFlags(Channel c) {
    switch (c) {
    case Channel::Master:
        return Offsets::SOUND_FLAG_NO_CATEGORY;
    case Channel::Music:
        return Offsets::SOUND_FLAG_MUSIC;
    case Channel::Ambience:
        return Offsets::SOUND_FLAG_AMBIENCE;
    default:
        return 0;
    }
}

// ---- Playing a SoundKit ------------------------------------------------

using SoundEntryForID_t = void *(__fastcall *)(unsigned soundEntryID);
using ApplyRowVolume_t = void(__thiscall *)(void *def, void *sound, float scale);

// The options every play path shares, however they were spelled at the
// Lua edge. `volumeScale < 0` means "leave the definition's own volume".
struct PlayOptions {
    Channel channel = Channel::SFX;
    bool noDuplicates = false;
    bool runFinishCallback = false;
    float volumeScale = -1.0f;
};

void *PlayKit(int soundKitID, const PlayOptions &opts) {
    if (soundKitID <= 0)
        return nullptr;
    // `PlaySound(name)` plays nothing while the UI loads or the world is
    // entered or left; the id form keeps the same quiet.
    if (Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_INTERFACE_SOUND_SUPPRESS)) > 0)
        return nullptr;
    auto *def = static_cast<uint8_t *>(reinterpret_cast<SoundEntryForID_t>(
        Offsets::FUN_SOUND_ENTRY_FOR_ID)(static_cast<unsigned>(soundKitID)));
    if (def == nullptr)
        return nullptr;

    // The duplicate check is the definition's own bit, which the player
    // turns into the open's flag 0x1. Lend it for this one call. The check
    // compares the picked FILE, so a kit with several files can still
    // start a second copy on a different pick.
    auto &defFlags = *reinterpret_cast<uint32_t *>(def + Offsets::OFF_SOUND_DEFINITION_FLAGS);
    const uint32_t savedFlags = defFlags;
    if (opts.noDuplicates)
        defFlags |= Offsets::SOUND_DEFINITION_FLAG_NO_DUPLICATES;

    void *sound = reinterpret_cast<PlaySoundEntry_t>(Offsets::FUN_SOUND_PLAY_ENTRY)(
        0, opts.channel == Channel::Master ? 1 : 0, 0, soundKitID, kVariantRandom,
        opts.channel == Channel::Music ? 1 : 0);
    defFlags = savedFlags;

    if (sound == nullptr)
        return nullptr;
    // The player applies the definition's volume at scale 1.0 as it
    // starts; the applicator writes rather than accumulates, so a second
    // call replaces it.
    if (opts.volumeScale >= 0.0f)
        reinterpret_cast<ApplyRowVolume_t>(Offsets::FUN_SOUND_APPLY_ROW_VOLUME)(
            def, sound, opts.volumeScale);
    Own(sound, opts.runFinishCallback);
    return sound;
}

// Push `willPlay, soundHandle`.
int PushResult(void *L, void *sound) {
    Game::Lua::PushBool(L, sound != nullptr);
    if (sound != nullptr)
        Game::Lua::PushNumber(L, static_cast<double>(reinterpret_cast<uintptr_t>(sound)));
    else
        Game::Lua::PushNil(L);
    return 2;
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
    PlayOptions opts;
    opts.channel = ChannelArg(L, 2);
    opts.noDuplicates = Game::Lua::ToBoolean(L, 3) != 0;
    opts.runFinishCallback = Game::Lua::ToBoolean(L, 4) != 0;
    return PushResult(L, PlayKit(static_cast<int>(Game::Lua::ToNumber(L, 1)), opts));
}

// The global `PlaySound(soundKitID | soundName)`.
//
// The engine's own `PlaySound` takes a SoundEntries row NAME
// ("igMainMenuOpen"); the modern one takes that row's id. Both now work:
// a NUMBER plays by id and returns `willPlay, soundHandle`, and anything
// else tail-calls the engine with the stack untouched, so the name form
// behaves exactly as it always has (it pushes no return values).
//
// The id form is the ONLY thing added here — the optional arguments live
// on `C_Sound.PlaySound` instead, which keeps this global as close to the
// one it replaces as the extension allows.
//
// Discriminating on `lua_type`, not `lua_isnumber`: the latter also
// accepts a numeric STRING, which would quietly reroute `PlaySound("123")`
// away from the name lookup.
int __fastcall Script_PlaySound(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_NUMBER)
        return PushResult(L, PlayKit(static_cast<int>(Game::Lua::ToNumber(L, 1)), {}));
    return reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_PLAY_SOUND)(L);
}

// Read `name` from the params table at stack index `idx`. Uses GetTable
// rather than a raw read so a params table built from a mixin still works.
// Leaves the stack as it found it.
double FieldNumber(void *L, int idx, const char *name, double fallback) {
    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, idx);
    const double v = (Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER)
                         ? Game::Lua::ToNumber(L, -1)
                         : fallback;
    Game::Lua::SetTop(L, -2);
    return v;
}

bool FieldBool(void *L, int idx, const char *name) {
    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, idx);
    const bool v = Game::Lua::ToBoolean(L, -1) != 0;
    Game::Lua::SetTop(L, -2);
    return v;
}

// C_Sound.PlaySoundWithOptions(params) -> success, soundHandle
//
// The table form of PlaySound. Honors `soundKitID`, `forceNoDuplicates`,
// `runFinishCallback` and `volumeOverride`; accepts and ignores
// `uiSoundSubType` and `overridePriority`, which have nothing behind them
// here. `volumeOverride` scales the sound's own volume, so 1.0 is
// unchanged and 0 is silent.
int __fastcall Script_PlaySoundWithOptions(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: C_Sound.PlaySoundWithOptions(params)");
        return 0;
    }
    const int soundKitID = static_cast<int>(FieldNumber(L, 1, "soundKitID", 0));
    PlayOptions opts;
    opts.noDuplicates = FieldBool(L, 1, "forceNoDuplicates");
    opts.runFinishCallback = FieldBool(L, 1, "runFinishCallback");
    const double volume = FieldNumber(L, 1, "volumeOverride", -1.0);
    if (volume >= 0.0)
        opts.volumeScale = static_cast<float>(volume);
    return PushResult(L, PlayKit(soundKitID, opts));
}

// PlaySoundFile(file [, channel]) -> willPlay, soundHandle
//
// The engine's own (`0x00458780`) is `Play2D(3, file, 0, 0)` returning
// 1 or nil. This is the same open on the same bus (its cap of 2 still
// refuses a third file at once), with the channel's category flag and
// the handle returned so StopSound can end it.
int __fastcall Script_PlaySoundFile(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L,
            "Usage: PlaySoundFile(\"soundfile\", optional[\"SFX\",\"Music\",\"Ambience\" "
            "or \"Master\"])");
        return 0;
    }
    const unsigned flags = ChannelFlags(ChannelArg(L, 2));
    void *sound = reinterpret_cast<Play2D_t>(Offsets::FUN_SOUND_PLAY_2D)(
        Offsets::SOUND_BUS_PLAY_SOUND_FILE, Game::Lua::ToString(L, 1), flags, 0);
    if (sound != nullptr)
        Own(sound, false);
    return PushResult(L, sound);
}

// StopSound(soundHandle [, fadeoutTime])
//
// `fadeoutTime` is in milliseconds. Sound::Stop2 fades the sound out and
// releases it at silence, or releases it at once below 0.1 s. Only a
// handle one of our play functions returned is stopped (see Owned).
int __fastcall Script_StopSound(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_NUMBER)
        return 0;
    const auto handle = static_cast<uintptr_t>(Game::Lua::ToNumber(L, 1));
    const double fadeMs =
        Game::Lua::Type(L, 2) == Game::Lua::TYPE_NUMBER ? Game::Lua::ToNumber(L, 2) : 0.0;
    for (const Owned &o : g_owned) {
        if (o.handle != handle || !StillPlaying(o))
            continue;
        reinterpret_cast<Stop_t>(Offsets::FUN_SOUND_STOP)(
            reinterpret_cast<void *>(handle), static_cast<float>(fadeMs * 0.001));
        break;
    }
    return 0;
}

// C_Sound.GetSoundScaledVolume(soundHandle) -> number
//
// The volume the sound is playing at: the sound's own volume after any
// scaling. Reads the field only off a sound still in the live list, since
// a finished sound's memory goes back to a pool.
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
    Game::Lua::PushBool(L, FindStreamNode(handle) != 0);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Sound", "PlaySound", &Script_C_Sound_PlaySound);
    Game::Lua::RegisterTableFunction("C_Sound", "PlaySoundWithOptions",
                                     &Script_PlaySoundWithOptions);
    Game::Lua::RegisterTableFunction("C_Sound", "GetSoundScaledVolume",
                                     &Script_GetSoundScaledVolume);
    Game::Lua::RegisterTableFunction("C_Sound", "IsPlaying", &Script_IsPlaying);
    // Strict supersets of the engine's own globals — see each Script_*.
    Game::Lua::RegisterGlobalFunction("PlaySound", &Script_PlaySound);
    Game::Lua::RegisterGlobalFunction("PlaySoundFile", &Script_PlaySoundFile);
    Game::Lua::RegisterGlobalFunction("StopSound", &Script_StopSound);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::Play
