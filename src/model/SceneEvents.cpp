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

// An actor model's animation events. Each actor model gets the engine's event
// callback, as a unit's model does (CGUnit_C::SetModelCallbacks, 0x00607800),
// and the events reach this file the way a unit's reach its dispatcher
// (CGUnit_C::OnModelEvent, 0x005FFBD0, keyed by the four-character tag):
//
// - $CSL / $CSR / $CST release a PlaySpellVisual missile (model/SceneSpell.cpp),
//   always; so does $BWR, from the bow or rifle point, after the weapon's
//   release animation. $BWP draws the bow and nocks the ammo while a ranged
//   spell is cast.
// - With SetPlayAnimationSounds (a ClassicAPI extension, off by default so
//   the stock scenes stay as quiet as before), the sounds a unit plays from
//   its own data, here from the CreatureSoundData of the display the actor
//   shows (GetSoundData 0x0060AFB0: the display's row, else its model
//   data's):
//     $FD1..$FD4  the fidget sounds, +0x38 (0x006232C0; $FD5..$FD9 play none)
//     $FDX        the stand vocal, +0x20: 41 in 101, at most once in 10 s
//                 for all units, never for the active player (0x006236B0,
//                 0x00623490)
//     $FSD        the footstep: +0x24 by the ground's terrain through the
//                 engine's GetFootstepSound, with FootstepSounds on
//                 (0x00623390); the ground is the scene's SetGroundTerrain,
//                 else the terrain type of the WMO face under the actor
//     $DTH        the death thud by the size class and the ground, from the
//                 engine's own table (0x006236E0)
//     $WNG, $WGG  the wing flap +0x2C and glide +0x30 (vocal kinds 7 and 10,
//                 which have no chance or rate limit)
//     $CSD        the event's payload as a SoundEntries id, with EmoteSounds
//                 on (0x00623C10)
//     $TRD        the precast sound (visual +0x38) of the spell being
//                 prepared (0x0062FAA0)
//   The world plays these at the unit's position in 3D; a frame plays them
//   at the actor, heard from the scene camera. An actor with neither ground
//   has no footsteps and no death thud, as nothing tells it what it stands on.
//
// Not played: the combat tags $CSS, $CAH and $AH0..$AH3 present a pending
// swing (0x006247D0) and play nothing without one, and an actor never swings
// at anything; footprints, splashes and camera shakes are world effects.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "dbc/Lookup.h"
#include "model/M2.h"
#include "sound/Play.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace Model::Scene::Events {

namespace {

// The tags, as the engine compares them: four characters in a dword.
constexpr uint32_t Tag(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}
constexpr uint32_t kCSL = Tag('$', 'C', 'S', 'L');
constexpr uint32_t kCSR = Tag('$', 'C', 'S', 'R');
constexpr uint32_t kCST = Tag('$', 'C', 'S', 'T');
constexpr uint32_t kBWR = Tag('$', 'B', 'W', 'R');
constexpr uint32_t kBWP = Tag('$', 'B', 'W', 'P');
constexpr uint32_t kFD1 = Tag('$', 'F', 'D', '1');
constexpr uint32_t kFD4 = Tag('$', 'F', 'D', '4');
constexpr uint32_t kFDX = Tag('$', 'F', 'D', 'X');
constexpr uint32_t kFSD = Tag('$', 'F', 'S', 'D');
constexpr uint32_t kDTH = Tag('$', 'D', 'T', 'H');
constexpr uint32_t kWNG = Tag('$', 'W', 'N', 'G');
constexpr uint32_t kWGG = Tag('$', 'W', 'G', 'G');
constexpr uint32_t kCSD = Tag('$', 'C', 'S', 'D');
constexpr uint32_t kTRD = Tag('$', 'T', 'R', 'D');
static_assert(kCSR == 0x52534324 && kWNG == 0x474E5724, "tags compare as the engine's dwords");

constexpr int kStandChance = 40;            // 0x008626D4[5]: pass at <= 40 of 0..100
constexpr uint32_t kStandIntervalMs = 10000; // 0x00623290
uint32_t g_lastStandMs = 0;
bool g_standPlayed = false;

// The display's sound data row, else its model data's.
const uint8_t *SoundData(uint32_t displayID) {
    const uint8_t *display = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_RECORDS,
                                         Offsets::VAR_CREATUREDISPLAYINFO_COUNT, displayID);
    if (display == nullptr)
        return nullptr;
    auto row = [](uint32_t id) {
        return DBC::Record(Offsets::VAR_CREATURESOUNDDATA_RECORDS,
                           Offsets::VAR_CREATURESOUNDDATA_COUNT, id);
    };
    if (const uint8_t *own = row(Game::Read<uint32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_SOUND_ID)))
        return own;
    const uint8_t *modelData =
        DBC::Record(Offsets::VAR_CREATUREMODELDATA_RECORDS, Offsets::VAR_CREATUREMODELDATA_COUNT,
                    Game::Read<uint32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_MODEL_ID));
    return modelData != nullptr
               ? row(Game::Read<uint32_t>(modelData, Offsets::OFF_CREATUREMODELDATA_SOUND_ID))
               : nullptr;
}

// GetUnitSoundSize: the display's size class unless -1, else its model
// data's; -1 when neither is known.
int SizeClass(uint32_t displayID) {
    const uint8_t *display = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_RECORDS,
                                         Offsets::VAR_CREATUREDISPLAYINFO_COUNT, displayID);
    if (display == nullptr)
        return -1;
    const auto size = Game::Read<int32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_SIZE_CLASS);
    if (size != -1)
        return size;
    const uint8_t *modelData =
        DBC::Record(Offsets::VAR_CREATUREMODELDATA_RECORDS, Offsets::VAR_CREATUREMODELDATA_COUNT,
                    Game::Read<uint32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_MODEL_ID));
    return modelData != nullptr
               ? Game::Read<int32_t>(modelData, Offsets::OFF_CREATUREMODELDATA_SIZE_CLASS)
               : -1;
}

bool CVarOn(const char *name) { return CVar::Factory::GetInt(CVar::Factory::Find(name), 0) != 0; }

// A unit's animation sounds play at the unit (heard from the scene camera).
void Play(const Actor &actor, int32_t soundID) {
    if (soundID > 0)
        Sounds::Play(actor.SceneFrame(), soundID, actor.position);
}

void PlayStand(const Actor &actor, const uint8_t *soundData) {
    if (actor.unitGuid != 0 && actor.unitGuid == Unit::Identity::PlayerGuid())
        return;
    if (std::rand() % 101 > kStandChance)
        return;
    const uint32_t now = Time::Clock::NowMs();
    if (g_standPlayed && Time::Clock::Elapsed(g_lastStandMs, now) < kStandIntervalMs)
        return;
    g_standPlayed = true;
    g_lastStandMs = now;
    Play(actor, Game::Read<int32_t>(soundData, Offsets::OFF_CREATURESOUNDDATA_STAND));
}

// The ground the actor stands on: the scene's SetGroundTerrain, else the
// terrain type of the WMO face under it (the unit's +0xC60).
int Ground(const Actor &actor) {
    void *frame = actor.SceneFrame();
    const int set = GroundTerrain(frame);
    return set >= 0 ? set : World::Ground(WorldOf(frame), *actor.ambience);
}

void PlayFootstep(const Actor &actor, const uint8_t *soundData) {
    using GetFootstepSound_t = int(__fastcall *)(int footstepID, int terrainTypeID, int wet);
    const int terrain = Ground(actor);
    if (terrain < 0 || !CVarOn("FootstepSounds"))
        return;
    Play(actor, reinterpret_cast<GetFootstepSound_t>(Offsets::FUN_GET_FOOTSTEP_SOUND)(
        Game::Read<int32_t>(soundData, Offsets::OFF_CREATURESOUNDDATA_FOOTSTEP), terrain, 0));
}

// PlayDeathThud's dry thud: the engine's table by size class and the
// terrain's TerrainTypeSounds id, bounded as it bounds it.
void PlayDeathThud(const Actor &actor) {
    const int terrain = Ground(actor);
    const int size = SizeClass(actor.displayID);
    if (terrain < 0 || size < 0 || size >= Offsets::DEATH_THUD_SIZE_CLASSES)
        return;
    const uint8_t *terrainRow = DBC::Record(Offsets::VAR_TERRAINTYPE_RECORDS,
                                            Offsets::VAR_TERRAINTYPE_COUNT,
                                            static_cast<uint32_t>(terrain));
    if (terrainRow == nullptr)
        return;
    const auto soundIndex = Game::Read<int32_t>(terrainRow, Offsets::OFF_TERRAINTYPE_SOUND_ID);
    const uintptr_t table = static_cast<uintptr_t>(Offsets::VAR_DEATH_THUD_SOUNDS) +
                            static_cast<uintptr_t>(size) * Offsets::DEATH_THUD_STRIDE;
    if (soundIndex < 0 || soundIndex >= Game::Read<int32_t>(table + Offsets::OFF_DEATH_THUD_COUNT))
        return;
    const auto *pairs = Game::Read<const int32_t *>(table + Offsets::OFF_DEATH_THUD_PAIRS);
    Play(actor, pairs[soundIndex * 2]);
}

void PlayPrecast(const Actor &actor) {
    for (const SpellCast &cast : actor.spellCasts)
        if (cast.phase == SpellCast::Phase::Precast) {
            Play(actor, Game::Read<int32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_PRECAST_SOUND));
            return;
        }
}

void PlaySound(Actor &actor, uint32_t tag, uint32_t data) {
    if (tag == kCSD) {
        if (CVarOn("EmoteSounds"))
            Play(actor, static_cast<int32_t>(data));
        return;
    }
    if (tag == kTRD) {
        PlayPrecast(actor);
        return;
    }
    if (tag == kDTH) {
        PlayDeathThud(actor);
        return;
    }
    const uint8_t *soundData = SoundData(actor.displayID);
    if (soundData == nullptr)
        return;
    if (tag >= kFD1 && tag <= kFD4 && (tag & 0x00FFFFFF) == (kFD1 & 0x00FFFFFF))
        Play(actor, Game::Read<int32_t>(soundData,
                                 Offsets::OFF_CREATURESOUNDDATA_FIDGET + ((tag >> 24) - '1') * 4));
    else if (tag == kFDX)
        PlayStand(actor, soundData);
    else if (tag == kFSD)
        PlayFootstep(actor, soundData);
    else if (tag == kWNG)
        Play(actor, Game::Read<int32_t>(soundData, Offsets::OFF_CREATURESOUNDDATA_WING_FLAP));
    else if (tag == kWGG)
        Play(actor, Game::Read<int32_t>(soundData, Offsets::OFF_CREATURESOUNDDATA_WING_GLIDE));
}

// $BWR's point (0x00600159): a bow animation releases from the body's
// attachment 35, a rifle animation from 36, any other from the event's point.
void BowReleasePoint(const Actor &actor, float (&point)[3]) {
    using IsAnim_t = int(__fastcall *)(int anim);
    const int anim = actor.CurrentAnimation();
    uint32_t attachment = 0;
    if (reinterpret_cast<IsAnim_t>(Offsets::FUN_IS_BOW_ANIMATION)(anim) != 0)
        attachment = Offsets::ATTACH_BOW_RELEASE;
    else if (reinterpret_cast<IsAnim_t>(Offsets::FUN_IS_RIFLE_ANIMATION)(anim) != 0)
        attachment = Offsets::ATTACH_RIFLE_RELEASE;
    if (attachment != 0 && actor.model != nullptr && M2::HasAttachment(actor.model, attachment))
        M2::GetAttachmentPosition(actor.model, attachment, point);
}

} // namespace

void __stdcall OnModelEvent(uint32_t tag, uint32_t data, const float *point, uint32_t /*unknown*/,
                            uint32_t userLow, uint32_t /*userHigh*/) {
    auto *actor = reinterpret_cast<Actor *>(static_cast<uintptr_t>(userLow));
    if (tag == kCSL || tag == kCSR || tag == kCST || tag == kBWR) {
        // The unit's callback drops a point that is not a number (0x00607840).
        if (point == nullptr || std::isnan(point[0]) || std::isnan(point[1]) || std::isnan(point[2]))
            return;
        actor->releaseEvent = true;
        for (int i = 0; i < 3; ++i)
            actor->releasePoint[i] = point[i];
        if (tag == kBWR) {
            BowReleasePoint(*actor, actor->releasePoint);
            Casts::BowRelease(*actor);
        }
        return;
    }
    if (tag == kBWP) {
        Casts::BowPull(*actor);
        return;
    }
    if (actor->playAnimationSounds)
        PlaySound(*actor, tag, data);
}

} // namespace Model::Scene::Events
