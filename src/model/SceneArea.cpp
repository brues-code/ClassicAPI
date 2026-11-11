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

// The area kit of a spell's ground area (actor:SetModelBySpellArea), as the
// world's dynamic object starts it when it is created (CGDynamicObject_C,
// 0x005D55C0) and stops it when it is deactivated or destroyed:
//
// - The kit is the visual's +0x34 SpellVisualKit. Its first procedure of
//   kind 9 starts the shard emitter (0x006ECE30 -> 0x006EB930): the shard
//   model is the type's file (the low byte of the bits of the procedure's
//   first value + 512.0, shifted right 14, into the seven paths at
//   0x00870E24), and the rate is its second value, scaled by the spell
//   detail level (x0.33 at 0, x0.66 at 1).
// - Each frame the emitter adds the frame's seconds times the rate and
//   spawns a shard for each whole 1.0 (0x006EBAD0). A shard picks a point of
//   the area (the radial draw is not square-rooted, so shards gather at the
//   centre) 16.67 yards up, traces up to it from just over the centre and
//   stops short of a ceiling, then traces 33.33 yards down to the floor
//   (0x006EBB64..0x006EBD20). It starts after 2 * a draw of 0..255 ms, plays
//   its animation once, and is freed when it ends (0x006EBEE0, 0x006EBE70).
//   Every fourth model event of the shards plays when it is a $SND
//   (0x006EBE30).
// - The kit's sound loops on the area (0x005D569C), and stops with a 3.0
//   second fade (0x005D56D0).
//
// The world has no pause; a paused scene (or actor) holds its shards still
// and silences the loop, which starts again when the pause ends.
//
// The random draws are the client's own (its random state 0x00882664). The
// traces are the scene's WMO (World::Trace); a scene without a WMO has no
// floor, so there its shards land at the area's height. The scene clock
// times the shards, so they stop while the scene is paused.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "model/M2.h"
#include "sound/Play.h"
#include "time/Clock.h"

#include <cstring>

namespace Model::Scene::Areas {

namespace {

constexpr float kLoopStopFadeSec = 3.0f; // 0x005D56DD: Sound::Stop2's fade
constexpr uint32_t kSND = 0x444E5324;      // $SND, as 0x006EBE42 compares it
uint32_t g_shardEvents = 0;                // the shards' event count (the world's 0x00CEE944)

float FromBits(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

uint32_t BitsOf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

void *RandomState() { return reinterpret_cast<void *>(static_cast<uintptr_t>(Offsets::VAR_CLIENT_RANDOM)); }

// A draw in [0, 1): the client's random uint32 as the mantissa of a float in
// [1, 2), less 1.0 (0x006EBB64..0x006EBB8B).
float Draw() {
    using Random_t = uint32_t(__thiscall *)(void *state);
    const uint32_t r = reinterpret_cast<Random_t>(Offsets::FUN_RANDOM_UINT32)(RandomState());
    return FromBits((r & 0x007FFFFFu) | 0x3F800000u) - Game::Read<float>(Offsets::VAR_ONE);
}

// The client's random unit vector, x and y (0x006EBB83).
void Direction(float (&out)[4]) {
    using Vector_t = const float *(__fastcall *)(float *out, void *state);
    reinterpret_cast<Vector_t>(Offsets::FUN_RANDOM_UNIT_VECTOR2)(out, RandomState());
}

// The byte the emitter decodes from a float: the bits of the value + 512.0,
// shifted right 14 (0x005D5665..0x005D567D, 0x006EBD87..0x006EBD96).
uint8_t Decode(float value) {
    return static_cast<uint8_t>(BitsOf(value + Game::Read<float>(Offsets::VAR_FIVE_TWELVE)) >> 14);
}

// The rate at the spell detail level (0x006EB95E..0x006EB98F).
float DetailRate(float rate) {
    const auto *record = Game::Read<const uint8_t *>(Offsets::VAR_SPELL_EFFECT_LEVEL_RECORD);
    if (record == nullptr)
        return rate;
    const auto level = Game::Read<int32_t>(record, Offsets::OFF_SPELL_EFFECT_LEVEL);
    if (level == 0)
        return rate * Game::Read<float>(Offsets::VAR_SPELL_EFFECT_LOW_SCALE);
    if (level == 1)
        return rate * Game::Read<float>(Offsets::VAR_SPELL_EFFECT_MEDIUM_SCALE);
    return rate;
}

// The shards' event callback (0x006EBE30): every fourth event, a $SND plays
// its payload at the event's point.
void __stdcall OnShardEvent(uint32_t tag, uint32_t data, const float *point, uint32_t /*unknown*/,
                            uint32_t userLow, uint32_t /*userHigh*/) {
    ++g_shardEvents;
    if ((g_shardEvents & 3u) != 0 || tag != kSND)
        return;
    auto *shard = reinterpret_cast<AreaShard *>(static_cast<uintptr_t>(userLow));
    const float *at = point != nullptr ? point : shard->position;
    const float where[3] = {at[0], at[1], at[2]};
    Sounds::Play(shard->frame, static_cast<int32_t>(data), where);
}

// The shards' done callback (0x006EBE70): a natural end (reason 0) frees the
// shard, here at the next update.
void __fastcall OnShardDone(void * /*model*/, uint32_t /*keyBone*/, uint32_t /*animID*/, uint32_t reason,
                            uint32_t /*overshoot*/, uint32_t userLow, uint32_t /*userHigh*/) {
    if (reason == 0)
        reinterpret_cast<AreaShard *>(static_cast<uintptr_t>(userLow))->done = true;
}

void FreeShard(AreaShard &shard) {
    M2::SetEventCallback(shard.model, nullptr, 0, 0);
    M2::SetAnimDoneCallback(shard.model, nullptr, 0, 0);
    ReleaseLit(shard.model, shard.light);
    shard.model = nullptr;
}

// The area kit's start (0x005D55C0): the emitter of its first kind-9
// procedure and its sound.
void Start(Actor &actor) {
    AreaEmitter &emitter = actor.areaEmitter;
    emitter = AreaEmitter{};
    emitter.running = true;
    const uint8_t *kit =
        DBC::Record(Offsets::VAR_SPELLVISUALKIT_RECORDS, Offsets::VAR_SPELLVISUALKIT_COUNT,
                    Game::Read<uint32_t>(actor.areaVisual, Offsets::OFF_SPELLVISUAL_AREA_KIT));
    if (kit == nullptr)
        return;
    for (int i = 0; i < 4; ++i) {
        if (Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_KIND + i * 4) !=
            Offsets::SPELLVISUALKIT_PROC_AREA_SHARDS)
            continue;
        // The engine reads the table with no bound; a type past it has no path.
        const uint8_t type = Decode(Game::Read<float>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_VALUE + i * 4));
        if (type < Offsets::AREA_SHARD_PATH_COUNT) {
            emitter.path = Game::Read<const char *>(Offsets::VAR_AREA_SHARD_PATHS + type * 4u);
            emitter.rate = DetailRate(Game::Read<float>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_VALUE2 + i * 4));
        }
        break;
    }
    emitter.soundID = Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_SOUND);
}

// One shard (0x006EBB30..0x006EBDA9).
void Spawn(Actor &actor) {
    void *frame = actor.SceneFrame();
    const float radius = actor.areaRadius;
    const float *centre = actor.position;
    const float u = Draw();
    float direction[4] = {};
    Direction(direction);
    const float us = u * direction[1];
    const float high[3] = {centre[0] + u * direction[0] * radius, centre[1] + us * radius,
                           centre[2] + Game::Read<float>(Offsets::VAR_AREA_SHARD_DROP_HEIGHT)};
    const float low[3] = {centre[0], centre[1], centre[2] + Game::Read<float>(Offsets::VAR_ONE_SIXTH)};
    const World::Backdrop *world = WorldOf(frame);
    float fraction = 1.0f;
    float normal[3];
    float start[3] = {high[0], high[1], high[2]};
    // Short of a ceiling between the centre and the point up there.
    if (world != nullptr && World::Trace(*world, low, high, false, fraction, normal)) {
        const float share = fraction * Game::Read<float>(Offsets::VAR_AREA_SHARD_HIT_SHARE);
        for (int i = 0; i < 3; ++i)
            start[i] = low[i] + (high[i] - low[i]) * share;
    }
    auto shard = std::make_unique<AreaShard>();
    shard->frame = frame;
    const float below[3] = {start[0], start[1], start[2] - Game::Read<float>(Offsets::VAR_AREA_SHARD_FALL)};
    fraction = 1.0f;
    if (world == nullptr) {
        shard->position[0] = start[0];
        shard->position[1] = start[1];
        shard->position[2] = centre[2];
    } else if (World::Trace(*world, start, below, false, fraction, normal)) {
        for (int i = 0; i < 3; ++i)
            shard->position[i] = start[i] + (below[i] - start[i]) * fraction;
    } else {
        std::memcpy(shard->position, centre, sizeof shard->position);
    }
    void *model = M2::CreateModel(Model::Pane::Scene(frame), actor.areaEmitter.path);
    if (model == nullptr)
        return;
    shard->model = model;
    LightAsEntity(model, shard->light, frame);
    const auto user = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shard.get()));
    M2::SetEventCallback(model, reinterpret_cast<const void *>(&OnShardEvent), user, 0);
    M2::SetAnimDoneCallback(model, reinterpret_cast<const void *>(&OnShardDone), user, 0);
    // The wait: 2 * (the bits of draw * 255.0 + 512.0, rounded once, >> 14).
    const auto wait = static_cast<float>(static_cast<double>(Draw()) * Game::Read<float>(Offsets::VAR_TWO_FIFTY_FIVE) +
                                         Game::Read<float>(Offsets::VAR_FIVE_TWELVE));
    shard->startMs = ClockMs(frame) + 2u * static_cast<uint8_t>(BitsOf(wait) >> 14);
    actor.areaShards.push_back(std::move(shard));
}

void StopLoop(AreaEmitter &emitter, float fadeSec) {
    if (emitter.loopSound != nullptr) {
        Sound::Play::Stop(emitter.loopSound, fadeSec);
        emitter.loopSound = nullptr;
    }
}

} // namespace

void Stop(Actor &actor) {
    StopLoop(actor.areaEmitter, kLoopStopFadeSec);
    actor.areaEmitter = AreaEmitter{};
}

void Update(Actor &actor, float sceneSec) {
    AreaEmitter &emitter = actor.areaEmitter;
    const bool wanted = actor.areaEffect != nullptr && actor.shown;
    if (wanted && !emitter.running)
        Start(actor);
    else if (!wanted && emitter.running)
        Stop(actor);
    if (emitter.running) {
        const bool held = IsPaused(actor.SceneFrame()) || actor.paused || actor.globalPaused;
        if (held)
            StopLoop(emitter, 0.0f);
        else if (emitter.soundID > 0 && emitter.loopSound == nullptr)
            emitter.loopSound = Sounds::Loop(actor.SceneFrame(), emitter.soundID, actor.position);
        if (emitter.path != nullptr) {
            emitter.accumulator += static_cast<double>(sceneSec) * emitter.rate;
            while (emitter.accumulator >= 1.0) {
                Spawn(actor);
                emitter.accumulator -= 1.0;
            }
        }
    }
    for (size_t i = 0; i < actor.areaShards.size();) {
        AreaShard &shard = *actor.areaShards[i];
        if (!shard.done) {
            SettleLight(shard.model, shard.light.get(), sceneSec);
            ++i;
            continue;
        }
        FreeShard(shard);
        actor.areaShards.erase(actor.areaShards.begin() + static_cast<ptrdiff_t>(i));
    }
}

// The shard's draw callback (0x006EBEE0): nothing before its start, then the
// first draw plays its animation once, and each draw shows and animates it.
void Link(Actor &actor) {
    const uint32_t now = ClockMs(actor.SceneFrame());
    for (const auto &shard : actor.areaShards) {
        if (shard->done || !M2::IsReady(shard->model, 0, 1))
            continue;
        if (!shard->started) {
            if (!Time::Clock::Reached(now, shard->startMs))
                continue;
            shard->started = true;
            M2::PlayAnimation(shard->model, -1, 0, -1, 0, 1.0f, 1, 1);
        }
        M2::SetVisible(shard->model, true);
        M2::SetAnimated(shard->model, true);
    }
}

void Place(Actor &actor) {
    const float axisZ[3] = {0.0f, 0.0f, 1.0f};
    const float scales[3] = {1.0f, 1.0f, 1.0f};
    for (const auto &shard : actor.areaShards)
        if (shard->started)
            M2::SetBaseTRS(shard->model, shard->position, 0.0f, axisZ, scales);
    if (actor.areaEmitter.loopSound != nullptr)
        Sounds::Move(actor.SceneFrame(), actor.areaEmitter.loopSound, actor.position);
}

void Drop(Actor &actor) {
    Stop(actor);
    for (const auto &shard : actor.areaShards)
        FreeShard(*shard);
    actor.areaShards.clear();
}

void Silence(Actor &actor) { StopLoop(actor.areaEmitter, 0.0f); }

} // namespace Model::Scene::Areas
