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

// The water effects the world shows around its liquids, for a scene's camera
// (benilla terrain/waterfx, terrain/liquid#drift-cloud).
//
// The drift cloud: the motes the camera sees in a liquid. The world builds one
// at start-up (0x0066F971: 0xFA40 bytes, CreateDriftCloud 0x0068E5A0 with the
// size base 1/36, the edge 30.0 and Textures\WaterPoop02.blp) and drives it
// from its camera: CameraEnvironment's change leg (0x00680AD9) scatters it in
// the new liquid's mode and sets its size base when the camera's liquid
// changes to water or ocean (1/36) or magma (1/9), and disables it in slime;
// each world frame advects it with the camera's step and its gust
// (0x0066FEE0 to AdvectDriftCloud 0x0068E930), and the world stage draws it
// after the liquids (0x006701E0 to DrawDriftCloud 0x0068EFE0), both while
// the camera is in a liquid and world flag 0x2000000 (waterParticulates) is
// set. A scene owns a cloud of its own, made the same way the first time its
// camera enters a liquid, and runs the same steps from its own camera: the
// advect reads the camera position and the frame seconds from the world's
// globals, so the scene's are staged in them around it. The draw works in
// the device's view, which the scene's paint has set.
//
// The ripples: the foam rings and wakes of units in shallow liquid. The world
// keeps them in one pool of 128 records that its own liquid query fills and
// its liquid pass draws (benilla terrain/waterfx); a scene keeps a pool of
// its own with the same records, filled from the scene's liquid triangles
// (World::LiquidTriangles) and drawn in the scene's liquid phase. The steps
// are transcribed: CGUnit_C::EmitWaterRipple (0x005FA760) for an actor,
// AddSplash's immediate leg (0x0068BDA0), AddRipple (0x0068F9F0), EmitRipple
// (0x0068F5C0), and UpdateRipples (0x0068FAE0) split into the scene's
// update (the ageing) and its paint (the draw). The scene's clock times them,
// so a paused scene holds its ripples.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "gx/Device.h"
#include "gx/Matrix.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Model::Scene::Water {

struct alignas(16) DriftMemory {
    uint8_t bytes[Offsets::DRIFT_CLOUD_SIZE];
};

// A ripple record (0x50 bytes in the world's pool).
struct Ripple {
    float position[3] = {}; // +0x00
    float heading = 0.0f;   // +0x0C
    float size = 0.0f;      // +0x10
    float growth = 0.0f;    // +0x14
    float alpha = 0.0f;     // +0x18
    float peak = 0.0f;      // +0x1C
    float rise = 0.0f;      // +0x20
    float decay = 0.0f;     // +0x24
    double death = 0.0;     // +0x28, on the scene's clock
    uint32_t category = 0;  // +0x2C: 0 a ring, 1 a wake
    std::vector<float> polygon; // +0x44/+0x48: the liquid triangles, nine floats each
};

struct Effects {
    DriftMemory *drift = nullptr; // made by CreateDriftCloud, or null
    std::array<Ripple, Offsets::RIPPLE_POOL> ripples;
    std::vector<uint32_t> live; // the pool's active list, oldest first
    uint32_t nextSelf = 0;
    uint32_t nextOther = Offsets::RIPPLE_POOL_SELF;
    double clock = 0.0; // the scene's clock at the last update, seconds
};

namespace {

double D(float v) { return static_cast<double>(v); }
double F(uintptr_t address) { return static_cast<double>(Game::Read<float>(address)); }

// NTempest::CRandom::uint32 on the client stream, and its mantissa-filled
// float in [1, 2).
uint32_t Draw() {
    return reinterpret_cast<uint32_t(__thiscall *)(void *)>(Offsets::FUN_RANDOM_UINT32)(
        reinterpret_cast<void *>(static_cast<uintptr_t>(Offsets::VAR_CLIENT_RANDOM)));
}
double Unit(uint32_t bits) {
    bits = (bits & 0x7FFFFFu) | 0x3F800000u;
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

Effects &Ensure(Effects *&effects, double clock) {
    if (effects == nullptr) {
        effects = new Effects();
        effects->clock = clock;
    }
    return *effects;
}

using This_t = void(__thiscall *)(void *);
using Word_t = void(__thiscall *)(void *, uint32_t);

bool Particulates() {
    return (Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS) & Offsets::WORLD_FLAG_PARTICULATES) != 0;
}

void *Drift(Effects &e) {
    if (e.drift == nullptr) {
        using Create_t = void(__thiscall *)(void *, uint32_t sizeBase, uint32_t edge, const char *texture);
        e.drift = new DriftMemory();
        reinterpret_cast<Create_t>(Offsets::FUN_CREATE_DRIFT_CLOUD)(
            e.drift, Offsets::DRIFT_SIZE_BASE_WATER, Offsets::DRIFT_EDGE,
            reinterpret_cast<const char *>(static_cast<uintptr_t>(Offsets::VAR_DRIFT_TEXTURE_PATH)));
    }
    return e.drift;
}

} // namespace

namespace {

// UpdateRipples' ageing (0x0068FB30): a record past its death time or whose
// alpha has fallen to 0 or less leaves the list; a live one grows, its alpha
// rises to the peak at the rise rate and then falls at the decay rate, the
// rise's overshoot taken from the step.
void Age(Effects &e, double clock) {
    const float dt = static_cast<float>(clock - e.clock);
    e.clock = clock;
    for (auto it = e.live.begin(); it != e.live.end();) {
        Ripple &r = e.ripples[*it];
        bool alive = false;
        if (e.clock < r.death) {
            r.size = static_cast<float>(D(dt) * r.growth + r.size);
            float step = dt;
            if (r.rise != 0.0f) {
                const float a = static_cast<float>(D(dt) * r.rise + r.alpha);
                r.alpha = a;
                if (r.peak < a) {
                    r.alpha = r.peak;
                    const float rate = r.rise;
                    r.rise = 0.0f;
                    step = static_cast<float>(D(step) - (D(a) - r.peak) / rate);
                }
            }
            if (r.rise == 0.0f)
                r.alpha = static_cast<float>(D(step) * r.decay + r.alpha);
            alive = !(r.alpha <= 0.0f);
        }
        if (alive) {
            ++it;
        } else {
            r.polygon.clear();
            it = e.live.erase(it);
        }
    }
}

// AddSplash's immediate leg, AddRipple and EmitRipple.
void AddSplash(Effects &e, void *frame, const float (&point)[3], uint32_t category, float heading, float size,
               float lifetime, float peakShare, float growth, bool self) {
    if (Game::Read<int32_t>(Offsets::VAR_WATER_RIPPLES) == 0 || Game::Read<int32_t>(Offsets::VAR_RIPPLE_MODE) != 0)
        return;
    float peak = 1.0f;
    if (D(peakShare) < F(Offsets::VAR_ONE_SIXTH))
        peak = static_cast<float>(D(peakShare) * F(Offsets::VAR_RIPPLE_PEAK_SCALE));
    // AddRipple: the next record of the mover's range or the others'.
    uint32_t index;
    if (self) {
        index = e.nextSelf;
        if (++e.nextSelf >= Offsets::RIPPLE_POOL_SELF)
            e.nextSelf = 0;
    } else {
        index = e.nextOther;
        if (++e.nextOther >= Offsets::RIPPLE_POOL)
            e.nextOther = Offsets::RIPPLE_POOL_SELF;
    }
    Ripple &r = e.ripples[index];
    // EmitRipple: the record, then its box's liquid triangles.
    r.category = category != 0 ? 1 : 0;
    const float final = static_cast<float>(D(lifetime) * growth + size);
    std::memcpy(r.position, point, sizeof r.position);
    r.heading = -heading;
    r.size = size;
    r.peak = peak;
    r.alpha = 0.0f;
    r.growth = static_cast<float>((D(final) - size) / lifetime);
    const double share = F(Offsets::VAR_RIPPLE_RISE_SHARES + r.category * sizeof(float));
    r.rise = static_cast<float>(D(peak) / (D(lifetime) * share));
    r.decay = static_cast<float>(-(D(peak) / ((F(Offsets::VAR_ONE) - share) * lifetime)));
    r.death = e.clock + lifetime;
    const float one = Game::Read<float>(Offsets::VAR_ONE);
    const float box[6] = {static_cast<float>(D(point[0]) - final), static_cast<float>(D(point[1]) - final),
                          static_cast<float>(D(point[2]) - one),   static_cast<float>(D(point[0]) + final),
                          static_cast<float>(D(point[1]) + final), static_cast<float>(D(point[2]) + one)};
    r.polygon.clear();
    World::LiquidTriangles(WorldOf(frame), TerrainOf(frame), box, r.polygon);
    e.live.erase(std::remove(e.live.begin(), e.live.end(), index), e.live.end());
    e.live.push_back(index);
}

} // namespace

void EmitUnitRipple(Effects *&effects, void *frame, const RippleSource &source, uint32_t &nextMs, uint32_t nowMs,
                    double clock, bool splash) {
    // CGUnit_C::EmitWaterRipple (0x005FA760): the kind.
    uint32_t kind = 0;
    if (splash) {
        nextMs = 0;
        kind = Offsets::RIPPLE_KIND_SPLASH_INDEX;
    } else if ((source.moveFlags & Offsets::RIPPLE_MOVE_FLAGS) != 0) {
        kind = Offsets::RIPPLE_KIND_MOVING_INDEX;
    } else if ((source.moveFlags & Offsets::RIPPLE_TURN_FLAGS) != 0) {
        kind = Offsets::RIPPLE_KIND_TURNING_INDEX;
    }
    // The depth under twice the collision height, at least 1.0 (0x005FA7FC).
    const double twice = D(source.height) + source.height;
    const float limit = twice > F(Offsets::VAR_ONE) ? static_cast<float>(twice) : 1.0f;
    const float half = static_cast<float>(D(limit) * F(Offsets::VAR_HALF));
    const float depth = static_cast<float>(D(source.surface) - source.position[2]);
    if (D(depth) >= limit)
        return;
    if (nextMs != 0 && static_cast<int32_t>(nowMs - nextMs) < 0)
        return;
    const float point[3] = {source.position[0], source.position[1], source.surface};
    const bool wake = Game::Read<uint8_t>(Offsets::VAR_RIPPLE_KIND_WAKE + kind) != 0;
    float moveScale = 1.0f;
    if (wake && D(source.speed) > F(Offsets::VAR_CAMERA_PORTAL_EPSILON)) {
        const double cap = F(Offsets::VAR_RIPPLE_SPEED_CAP);
        const double v = D(source.speed) < cap ? D(source.speed) : cap;
        moveScale = static_cast<float>(F(Offsets::VAR_RIPPLE_SPEED_SCALE) / v);
    }
    const float interval = static_cast<float>(F(Offsets::VAR_ONE) / moveScale);
    float heading = source.facing;
    const float base = static_cast<float>(D(source.scale) * F(Offsets::VAR_ONE_THIRD));
    // The size: 0.9 to 1.1 of a third of the scale, kept in [1/3, 5/3].
    const double sized = ((Unit(Draw()) - F(Offsets::VAR_ONE)) * F(Offsets::VAR_UNIT_NAME_SCALE_BASE) -
                          F(Offsets::VAR_ONE_TENTH) + F(Offsets::VAR_ONE)) *
                         base;
    float size = static_cast<float>(sized);
    if (sized < F(Offsets::VAR_ONE_THIRD))
        size = Game::Read<float>(Offsets::VAR_ONE_THIRD);
    else if (!(D(size) < F(Offsets::VAR_RIPPLE_SIZE_MAX)))
        size = Game::Read<float>(Offsets::VAR_RIPPLE_SIZE_MAX);
    float lifetime = static_cast<float>((Unit(Draw()) - F(Offsets::VAR_ONE)) * F(Offsets::VAR_ONE_TENTH) -
                                        F(Offsets::VAR_RIPPLE_LIFE_SPREAD) + F(Offsets::VAR_RIPPLE_LIFE_BASE));
    float growth = static_cast<float>(((Unit(Draw()) - F(Offsets::VAR_ONE)) * F(Offsets::VAR_RIPPLE_GROWTH_SPAN) -
                                       F(Offsets::VAR_THREE_QUARTERS) + F(Offsets::VAR_RIPPLE_GROWTH_BASE)) *
                                      F(Offsets::VAR_RIPPLE_GROWTH_SCALE) * F(Offsets::VAR_ONE_THIRTY_SIXTH) *
                                      interval);
    float weight = 1.0f;
    float peakShare = Game::Read<float>(Offsets::VAR_ONE_SIXTH);
    // Deeper than half the limit, the ripple fades toward the limit
    // (0x005FA9E6).
    if (D(depth) > half) {
        const double w = (D(limit) - depth) / (D(limit) - half) * F(Offsets::VAR_HALF) + F(Offsets::VAR_HALF);
        weight = static_cast<float>(w);
        peakShare = static_cast<float>(w * F(Offsets::VAR_ONE_SIXTH));
        lifetime = static_cast<float>(D(weight) * lifetime);
        size = static_cast<float>(D(weight) * size);
    }
    // A still unit's (0x005FAA2F).
    if (kind == 0) {
        const uintptr_t scales = Offsets::VAR_RIPPLE_STILL_SCALES;
        peakShare = static_cast<float>(F(scales) * peakShare);
        lifetime = static_cast<float>(F(scales + 4) * lifetime);
        growth = static_cast<float>(F(scales + 8) * growth);
        size = static_cast<float>(F(scales + 12) * size);
    }
    // A ring at a random heading; a wake along the motion (0x005FAA5F).
    if (!wake) {
        heading = static_cast<float>((Unit(Draw()) - F(Offsets::VAR_ONE)) * F(Offsets::VAR_HEADING_TWO_PI));
    } else {
        const uint32_t f = source.moveFlags;
        const double quarter = F(Offsets::VAR_QUARTER_PI), threeQuarters = F(Offsets::VAR_RIPPLE_THREE_QUARTER_PI),
                     halfPi = F(Offsets::VAR_HALF_PI);
        if ((f & Offsets::MOVEFLAG_STRAFE_LEFT) != 0)
            heading = static_cast<float>(D(heading) + ((f & Offsets::MOVEFLAG_FORWARD) != 0    ? quarter
                                                       : (f & Offsets::MOVEFLAG_BACKWARD) != 0 ? threeQuarters
                                                                                               : halfPi));
        else if ((f & Offsets::MOVEFLAG_STRAFE_RIGHT) != 0)
            heading = static_cast<float>(D(heading) - ((f & Offsets::MOVEFLAG_FORWARD) != 0    ? quarter
                                                       : (f & Offsets::MOVEFLAG_BACKWARD) != 0 ? threeQuarters
                                                                                               : halfPi));
        else if ((f & Offsets::MOVEFLAG_BACKWARD) != 0)
            heading = static_cast<float>(D(heading) + F(Offsets::VAR_RIPPLE_BACK_TURN));
    }
    Effects &e = Ensure(effects, clock);
    // The scene's actor is the active mover: the first range of the pool.
    AddSplash(e, frame, point, wake ? 1 : 0, heading, size, lifetime, peakShare, growth, true);
    // The next time (0x005FAB46): a wake by its weight and speed, else 400 ms
    // and a draw's share of 50.
    if (wake) {
        const double ms = D(weight) * moveScale * F(Offsets::VAR_RIPPLE_NEXT_SHARE) * F(Offsets::VAR_RIPPLE_NEXT_MS);
        nextMs = nowMs - static_cast<uint32_t>(static_cast<int32_t>(ms));
    } else {
        const uint32_t spread = static_cast<uint32_t>((uint64_t{Offsets::RIPPLE_STILL_DELAY_SPREAD} * Draw()) >> 32);
        nextMs = spread + Offsets::RIPPLE_STILL_DELAY_MS + nowMs;
    }
}

void DrawRipples(const Effects *effects, const float (&eye)[3]) {
    using Create_t = void *(__fastcall *)(int kind, int itemSize, int count);
    using Lock_t = void *(__fastcall *)(void *buffer);
    using Unlock_t = void(__fastcall *)(void *buffer, int unused);
    using VertexBind_t = void(__fastcall *)(void *buffer, int format);
    using IndexBind_t = void(__fastcall *)(void *buffer);
    using Texture_t = void *(__fastcall *)(void *handle, int load, void *callback);
    using Build_t = void(__fastcall *)(float *tex0, float *tex1, const float *box, const float *m, float fade,
                                       int absolute);
    struct Prim {
        uint32_t type, base;
        uint16_t count, min, max, unused;
    };
    using Render_t = void(__fastcall *)(const Prim *prim, int count);
    if (effects == nullptr)
        return;
    // UpdateRipples' draw (0x0068FCB1): the vertices of each category's live
    // records.
    uint32_t counts[2] = {};
    for (uint32_t index : effects->live) {
        const Ripple &r = effects->ripples[index];
        counts[r.category] += static_cast<uint32_t>(r.polygon.size() / 3);
    }
    if (counts[0] == 0 && counts[1] == 0)
        return;
    const auto *bias = Game::Read<const uint8_t *>(Offsets::VAR_FOOTSTEP_BIAS_CVAR);
    const float biasValue = bias != nullptr ? Game::Read<float>(bias, Offsets::OFF_CVAR_VALUE_FLOAT) : 0.0f;
    Gx::PushStates();
    Gx::SetState(Offsets::GXRS_BLEND, Offsets::GXRS_BLEND_ADD);
    Gx::SetState(Offsets::GXRS_FOG, 0);
    Gx::SetState(Offsets::GXRS_DEPTH_WRITE, 0);
    Gx::SetState(Offsets::GXRS_CULL, 0);
    Gx::SetState(Offsets::GXRS_LIGHTING, 0);
    Gx::SetStateFloat(Offsets::GXRS_DEPTH_BIAS, static_cast<float>(D(biasValue) * F(Offsets::VAR_RIPPLE_BIAS_SCALE)));
    Gx::Matrix world;
    Gx::Identity(world);
    Gx::Translate(world, {-eye[0], -eye[1], -eye[2]});
    Gx::PushTransform(Offsets::GX_XFORM_WORLD, world);
    const bool swap = Game::Read<int32_t>(reinterpret_cast<void *(__fastcall *)()>(Offsets::FUN_GX_DEVICE_CAPS)(),
                                          Offsets::OFF_GXCAPS_COLOR_SWAP) == 1;
    for (uint32_t category = 0; category < 2; ++category) {
        if (counts[category] == 0)
            continue;
        void *handle = Game::Read<void *>(Offsets::VAR_RIPPLE_TEXTURES + category * sizeof(void *));
        void *texture = handle != nullptr ? reinterpret_cast<Texture_t>(Offsets::FUN_TEXTURE_GET_RENDERABLE)(
                                                handle, 1, nullptr)
                                          : nullptr;
        if (texture == nullptr)
            continue;
        const int n = static_cast<int>(counts[category]);
        void *vertices = reinterpret_cast<Create_t>(Offsets::FUN_GX_BUF_CREATE)(Offsets::GX_BUF_VERTICES,
                                                                                 Offsets::GX_PNCT_SIZE, n);
        auto *v = static_cast<float *>(reinterpret_cast<Lock_t>(Offsets::FUN_GX_BUF_LOCK)(vertices));
        void *indices = reinterpret_cast<Create_t>(Offsets::FUN_GX_BUF_CREATE)(Offsets::GX_BUF_INDICES,
                                                                                sizeof(uint16_t), n);
        auto *ix = static_cast<uint16_t *>(reinterpret_cast<Lock_t>(Offsets::FUN_GX_BUF_LOCK)(indices));
        uint16_t written = 0;
        for (uint32_t index : effects->live) {
            const Ripple &r = effects->ripples[index];
            if (r.category != category || r.polygon.empty())
                continue;
            // The texture matrix of the ripple's box turned by its heading
            // (0x0068FFD0 to 0x0069014B).
            Gx::Matrix turn, tex0, tex1;
            Gx::Identity(turn);
            Gx::Rotate(turn, r.heading, {0.0f, 0.0f, 1.0f});
            Gx::Identity(tex0);
            Gx::Identity(tex1);
            const float one = Game::Read<float>(Offsets::VAR_ONE);
            const float box[6] = {static_cast<float>(D(r.position[0]) - r.size),
                                  static_cast<float>(D(r.position[1]) - r.size),
                                  static_cast<float>(D(r.position[2]) - one),
                                  static_cast<float>(D(r.size) + r.position[0]),
                                  static_cast<float>(D(r.size) + r.position[1]),
                                  static_cast<float>(D(r.position[2]) + one)};
            reinterpret_cast<Build_t>(Offsets::FUN_BUILD_DECAL_TRANSFORMS)(tex0, tex1, box, turn,
                                                                           Game::Read<float>(Offsets::VAR_HALF), 1);
            // The colour: white under the alpha's byte (0x00690150).
            const float packed = static_cast<float>(D(r.alpha) * F(Offsets::VAR_TWO_FIFTY_FIVE) +
                                                    F(Offsets::VAR_FIVE_TWELVE));
            uint32_t bits;
            std::memcpy(&bits, &packed, sizeof bits);
            uint32_t colour = ((bits >> 14) & 0xFFu) << 24 | 0xFFFFFFu;
            if (swap)
                colour = (colour & 0xFF00FF00u) | ((colour >> 16) & 0xFFu) | ((colour & 0xFFu) << 16);
            for (size_t k = 0; k + 2 < r.polygon.size(); k += 3) {
                const float p[3] = {r.polygon[k], r.polygon[k + 1], r.polygon[k + 2]};
                float uv[3];
                Gx::TransformPoint(p, tex0, uv);
                v[0] = p[0];
                v[1] = p[1];
                v[2] = p[2];
                v[3] = 0.0f;
                v[4] = 0.0f;
                v[5] = 1.0f;
                std::memcpy(&v[6], &colour, sizeof colour);
                v[7] = uv[0];
                v[8] = uv[1];
                v += Offsets::GX_PNCT_SIZE / sizeof(float);
                *ix++ = written++;
            }
        }
        reinterpret_cast<Unlock_t>(Offsets::FUN_GX_BUF_UNLOCK)(indices, 0);
        reinterpret_cast<Unlock_t>(Offsets::FUN_GX_BUF_UNLOCK)(vertices, 0);
        if (written == 0)
            continue;
        reinterpret_cast<VertexBind_t>(Offsets::FUN_GX_VERTEX_BUF_BIND)(vertices, Offsets::GX_FORMAT_PNCT);
        reinterpret_cast<IndexBind_t>(Offsets::FUN_GX_INDEX_BUF_BIND)(indices);
        Gx::SetState(Offsets::GXRS_TEXTURE0, texture);
        const Prim prim{Offsets::GX_PRIM_TRIANGLES, 0, written, 0, static_cast<uint16_t>(written - 1), 0};
        reinterpret_cast<Render_t>(Offsets::FUN_GX_PRIM_RENDER_ONE)(&prim, 1);
    }
    Gx::PopTransform(Offsets::GX_XFORM_WORLD);
    Gx::PopStates();
}

void Update(Effects *&effects, uint32_t before, uint32_t now, const float (&eye)[3], float elapsed, double clock) {
    if (effects != nullptr)
        Age(*effects, clock);
    // CameraEnvironment's change leg (0x00680AD9): by the new liquid & 3.
    if (now != Offsets::LIQUID_DRY) {
        const uint32_t kind = now & Offsets::LIQUID_TYPE_MASK;
        if (kind == Offsets::LIQUID_SLIME) {
            if (effects != nullptr && effects->drift != nullptr)
                Game::Ref<uint8_t>(effects->drift, Offsets::OFF_DRIFT_CLOUD_ENABLED) = 0;
        } else if (now != before && Particulates()) {
            if (effects == nullptr)
                effects = new Effects();
            void *drift = Drift(*effects);
            reinterpret_cast<Word_t>(Offsets::FUN_SCATTER_DRIFT_CLOUD)(drift, now);
            reinterpret_cast<Word_t>(Offsets::FUN_SET_DRIFT_CLOUD_SIZE_BASE)(
                drift, kind == Offsets::LIQUID_MAGMA ? Offsets::DRIFT_SIZE_BASE_MAGMA : Offsets::DRIFT_SIZE_BASE_WATER);
            Game::Ref<uint8_t>(drift, Offsets::OFF_DRIFT_CLOUD_ENABLED) = 1;
        }
    }
    // The world frame's advect (0x0066FEE0), from the scene's camera and
    // frame time.
    if (effects == nullptr || effects->drift == nullptr || !Particulates() || now == Offsets::LIQUID_DRY)
        return;
    float *camera = &Game::Ref<float>(Offsets::VAR_CAMERA_POSITION);
    float &seconds = Game::Ref<float>(Offsets::VAR_FRAME_SECONDS);
    const float savedCamera[3] = {camera[0], camera[1], camera[2]};
    const float savedSeconds = seconds;
    for (int i = 0; i < 3; ++i)
        camera[i] = eye[i];
    seconds = elapsed;
    reinterpret_cast<This_t>(Offsets::FUN_ADVECT_DRIFT_CLOUD)(effects->drift);
    for (int i = 0; i < 3; ++i)
        camera[i] = savedCamera[i];
    seconds = savedSeconds;
}

void DrawDrift(const Effects *effects, uint32_t cameraLiquid) {
    // The world stage's particulates (0x006701E0).
    if (effects == nullptr || effects->drift == nullptr || !Particulates() || cameraLiquid == Offsets::LIQUID_DRY)
        return;
    // The draw's quads are in view space; the world's stage reaches it with
    // the world transform identity.
    Gx::Matrix identity;
    Gx::Identity(identity);
    Gx::PushTransform(Offsets::GX_XFORM_WORLD, identity);
    reinterpret_cast<This_t>(Offsets::FUN_DRAW_DRIFT_CLOUD)(effects->drift);
    Gx::PopTransform(Offsets::GX_XFORM_WORLD);
}

void Free(Effects *effects) {
    if (effects == nullptr)
        return;
    if (effects->drift != nullptr) {
        reinterpret_cast<This_t>(Offsets::FUN_FREE_DRIFT_CLOUD_TEXTURE)(effects->drift);
        delete effects->drift;
    }
    delete effects;
}

} // namespace Model::Scene::Water
