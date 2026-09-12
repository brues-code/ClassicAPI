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

// `C_Sound.PlayVocalErrorSound(vocalErrorSoundID)` and
// `Enum.Vocalerrorsounds` — the player character's own spoken complaint
// for an error ("my bags are full", "I have no ammo"), in their race's and
// sex's voice.
//
// VocalUISounds.dbc holds one row per (error, race), each naming a male and
// a female recording. This build carries every one of the 68 modern enum
// values across 11 races, so the enum maps onto it exactly rather than
// approximately — see Offsets.h VAR_VOCAL_UI_SOUNDS_RECORDS for the layout
// and how it was verified.
//
// The row lookup is ours because the engine has none: it loads the table
// and never reads it (the only xrefs to its globals are the loader's own
// writes). Playing the row's sound still goes through the engine's by-id
// player, so only the table indexing is reimplemented, and that is the same
// `records[id]` walk every other DBC reader here performs.
//
// Race and sex come from the login-time globals rather than the player's
// descriptor, so this answers correctly from addon-load onward, before the
// in-world unit object exists — the same reason `UnitRace("player")` works
// that early.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Sound::VocalError {

namespace {

using PlaySoundEntry_t = void *(__fastcall *)(char, char, int, int, int, int);

constexpr int kVariantRandom = -1;

// Modern `Enum.Vocalerrorsounds`. Values are the DBC's own enum column, so
// these are the client's numbers and not a mapping invented here.
const Game::Lua::EnumIntegerEntry kVocalErrorEntries[] = {
    {"Inventoryfull", 0},
    {"Outofammo", 1},
    {"NoequipLevel", 2},
    {"NoequipEver", 3},
    {"BoundNodrop", 4},
    {"Itemcooling", 5},
    {"Cantdrinkmore", 6},
    {"Canteatmore", 7},
    {"Cantinvite", 8},
    {"Inviteebusy", 9},
    {"Targettoofar", 10},
    {"Invalidtarget", 11},
    {"Spellcooling", 12},
    {"CantlearnLevel", 13},
    {"Locked", 14},
    {"Nomana", 15},
    {"Notwhiledead", 16},
    {"Cantloot", 17},
    {"Cantcreate", 18},
    {"Declinegroup", 19},
    {"Alreadyingroup", 20},
    {"Alreadyinguild", 21},
    {"Cantaffordbankslot", 22},
    {"Toomanybankslots", 23},
    {"CanteatMoving", 24},
    {"Notabag", 25},
    {"Cantputbag", 26},
    {"Wrongslot", 27},
    {"Ammoonlyinbag", 28},
    {"Bagfull", 29},
    {"Itemmaxcount", 30},
    {"CantlootDidntkill", 31},
    {"CantlootWrongfacing", 32},
    {"CantlootLocked", 33},
    {"CantlootNotstandingObsolete", 34},
    {"CantlootToofar", 35},
    {"Cantattackrongdirection", 36},
    {"CantattackNotstandingObsolete", 37},
    {"CantattackNotarget", 38},
    {"Notenoughgold", 39},
    {"Notenoughmoney", 40},
    {"Cantequip2HSkill", 41},
    {"Cantequip2Hequipped", 42},
    {"Cantequip2HNoskill", 43},
    {"Notequippable", 44},
    {"Genericnotarget", 45},
    {"CantcastOutofrange", 46},
    {"Potioncooling", 47},
    {"Proficiencyneeded", 48},
    {"Mustequippitem", 49},
    {"Abilitycooling", 50},
    {"Cantuseitem", 51},
    {"Chestinuse", 52},
    {"FoodcoolingObsolete", 53},
    {"CanttaxiNomoney", 54},
    {"Cantuselocked", 55},
    {"Noequipslotavailable", 56},
    {"Cantusetoofar", 57},
    {"Cantswap", 58},
    {"CanttradeSoulbound", 59},
    {"Cantflyhere", 60},
    {"Itemlocked", 61},
    {"Guildpermissions", 62},
    {"Norage", 63},
    {"Noenergy", 64},
    {"Noessence", 65},
    {"Invaliditemtarget", 66},
    {"ExhaustedObsolete", 67},
};

// The SoundEntries id for `vocalErrorID` in the local player's voice, or 0
// when the table has no recording for that combination — several rows are
// -1 for one or both sexes, which is data rather than an error.
int SoundForError(int vocalErrorID) {
    const int count = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_VOCAL_UI_SOUNDS_COUNT));
    auto records = *reinterpret_cast<const uint8_t *const *const *>(
        static_cast<uintptr_t>(Offsets::VAR_VOCAL_UI_SOUNDS_RECORDS));
    if (records == nullptr || count <= 0)
        return 0;

    const int race = *reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_RACE_BYTE));
    const int sex = *reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SEX_BYTE));
    if (race == 0)
        return 0; // no character yet — nothing to speak with
    const int sexIndex = (sex != 0) ? 1 : 0;

    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = records[id];
        if (rec == nullptr)
            continue;
        if (Game::Read<int32_t>(rec, Offsets::OFF_VOCAL_UI_ENUM) != vocalErrorID)
            continue;
        if (Game::Read<int32_t>(rec, Offsets::OFF_VOCAL_UI_RACE) != race)
            continue;
        const int32_t sound = Game::Read<int32_t>(
            rec, Offsets::OFF_VOCAL_UI_NORMAL_SOUND + sexIndex * 4);
        return (sound > 0) ? sound : 0;
    }
    return 0;
}

// C_Sound.PlayVocalErrorSound(vocalErrorSoundID)
//
// Plays nothing when the player's race and sex have no recording for that
// error. Returns nothing, matching the engine's other sound players.
int __fastcall Script_PlayVocalErrorSound(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Sound.PlayVocalErrorSound(vocalErrorSoundID)");
        return 0;
    }
    const int soundKitID = SoundForError(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (soundKitID <= 0)
        return 0;

    reinterpret_cast<PlaySoundEntry_t>(Offsets::FUN_SOUND_PLAY_ENTRY)(
        0, 0, 0, soundKitID, kVariantRandom, 0);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Sound", "PlayVocalErrorSound",
                                     &Script_PlayVocalErrorSound);
    Game::Lua::RegisterIntegerEnum("Enum", "Vocalerrorsounds", kVocalErrorEntries,
                                   sizeof(kVocalErrorEntries) /
                                       sizeof(kVocalErrorEntries[0]));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::VocalError
