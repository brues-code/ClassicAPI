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

// The chain effects of a scene's casts: the bolts Chain Lightning draws from
// its caster to its target. The world makes them from a kit procedure of
// kind 0 or 12 (0x0060D7FC through the byte table 0x0060DC20 -> 0x006ECB90 ->
// 0x006ECBD0): the procedure's values give the SpellChainEffects row, the
// strand count (at most 3) and a persistent flag; each strand is a
// LightningObject whose hops each get a bolt of the client's lightning
// renderer (Lightning.cpp). Each frame the hop update (0x006EC460) shows a
// hop's bolt between its two ends while the hop lives, creating it with the
// row's shape the first time and moving it after, and destroys it at its
// end; the renderer then updates and draws every bolt (0x006ECA20,
// 0x006ECB20).
//
// A scene owns a renderer of its own and drives it through the engine's
// functions. A scene cast has one target, so a chain here has one hop, from
// the caster to the target, living the row's life (+0x14) from the cast; a
// script that casts again from each target draws a leaping chain. The ends
// are the world's: the caster's $CSL event point, else its position raised
// by 0.75 of its height (0x006EC6F0); the target's attachment that the
// spell visual's +0x24 names, else its position (0x006EC780). A hidden end
// hides the bolt, as a missing object does. The scene clock times the hops,
// so a paused scene holds them. A persistent chain of a channel (the
// procedure's third value, Drain Life's SoulBeam) lasts until the channel
// ends (End), as the world's LightningObject does until 0x0060D1C0 stops
// the spell's effects; one started by a cast lives the row's life.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "model/M2.h"
#include "texture/Load.h"
#include "time/Clock.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Model::Scene::Chains {

namespace {

constexpr uint32_t kCSL = 0x4C534324; // $CSL, as 0x006EC718 pushes it

struct Hop {
    Actor *from = nullptr;
    Actor *to = nullptr;
    uint32_t attachmentIndex = 0; // the spell visual's +0x24
    uint32_t endMs = 0;           // the scene clock at which it goes
    const uint8_t *record = nullptr;
    void *texture = nullptr; // the scene's reference to the row's texture
    int handle = -1;          // the renderer's bolt, or -1
    uint32_t channelCastID = 0; // a channel's persistent hop, held until End
};

struct State {
    alignas(8) uint8_t renderer[Offsets::LIGHTNING_RENDERER_SIZE] = {};
    std::vector<Hop> hops;
    std::unordered_map<uint32_t, void *> textures; // by chain id, one reference each
};

std::unordered_map<void *, std::unique_ptr<State>> g_states;

State *Find(void *frame) {
    const auto it = g_states.find(frame);
    return it != g_states.end() ? it->second.get() : nullptr;
}

State &Make(void *frame) {
    std::unique_ptr<State> &state = g_states[frame];
    if (state == nullptr) {
        state = std::make_unique<State>();
        reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_LIGHTNING_INIT)(state->renderer);
    }
    return *state;
}

int CreateBolt(State &state, const float (&start)[3], const float (&end)[3], const uint8_t *record, void *texture) {
    using Create_t = int(__thiscall *)(void *renderer, const float *start, const float *end, float segment,
                                       float width, uint32_t argb, float jitter, float period, float extra,
                                       void *texture, void *callback, void *argument);
    const float life = static_cast<float>(Game::Read<int32_t>(record, Offsets::OFF_SPELLCHAINEFFECTS_LIFE)) *
                       Game::Read<float>(Offsets::VAR_ONE_THOUSANDTH);
    return reinterpret_cast<Create_t>(Offsets::FUN_LIGHTNING_CREATE_BOLT)(
        state.renderer, start, end, Game::Read<float>(record, Offsets::OFF_SPELLCHAINEFFECTS_SEGMENT),
        Game::Read<float>(record, Offsets::OFF_SPELLCHAINEFFECTS_WIDTH), Offsets::CHAIN_BOLT_COLOR,
        Game::Read<float>(record, Offsets::OFF_SPELLCHAINEFFECTS_JITTER),
        Game::Read<float>(record, Offsets::OFF_SPELLCHAINEFFECTS_PERIOD), life, texture, nullptr, nullptr);
}

void ShowBolt(State &state, int handle, bool on) {
    reinterpret_cast<void(__thiscall *)(void *, int, int)>(Offsets::FUN_LIGHTNING_SHOW_BOLT)(state.renderer, handle,
                                                                                             on ? 1 : 0);
}

void DestroyBolt(State &state, int handle) {
    if (handle != -1)
        reinterpret_cast<void(__thiscall *)(void *, int)>(Offsets::FUN_LIGHTNING_DESTROY_BOLT)(state.renderer,
                                                                                               handle);
}

// The caster's end (0x006EC6F0).
bool CasterEnd(const Actor &actor, float (&out)[3]) {
    std::memcpy(out, actor.position, sizeof out);
    void *model = actor.model;
    if (model == nullptr || !M2::IsLoaded(model, 0, 0))
        return true;
    if (M2::HasEvent(model, kCSL)) {
        M2::GetEventPosition(model, kCSL, out);
        return true;
    }
    float m[16];
    M2::GetBaseTransform(model, m);
    float box[6];
    M2::GetBoundingBox(model, box);
    const float scale = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const uint32_t bits = Offsets::CHAIN_CASTER_HEIGHT;
    float share;
    std::memcpy(&share, &bits, sizeof share);
    out[2] += static_cast<float>(static_cast<double>(scale) * (box[5] - box[2]) * share);
    return true;
}

// The target's end (0x006EC780): the attachment the visual names, else the
// position. The engine reads the table with no bound; past it, the position.
void TargetEnd(const Actor &actor, uint32_t index, float (&out)[3]) {
    std::memcpy(out, actor.position, sizeof out);
    void *model = actor.model;
    if (model == nullptr || !M2::IsLoaded(model, 0, 0) || index >= Offsets::SPELL_VISUAL_ATTACHMENT_COUNT)
        return;
    const auto id = Game::Read<uint32_t>(static_cast<uintptr_t>(Offsets::VAR_SPELL_VISUAL_ATTACHMENTS) + index * 4);
    if (M2::HasAttachment(model, id))
        M2::GetAttachmentPosition(model, id, out);
}

void *ChainTexture(State &state, uint32_t chainID, const uint8_t *record) {
    void *&texture = state.textures[chainID];
    if (texture == nullptr) {
        const char *path = Game::Read<const char *>(record, Offsets::OFF_SPELLCHAINEFFECTS_TEXTURE);
        if (path != nullptr && *path != '\0')
            texture = ::Texture::LoadByPath(path);
    }
    return texture;
}

} // namespace

void Start(Actor &caster, Actor &target, const uint8_t *visual, const uint8_t *kit, uint32_t channelCastID) {
    if (kit == nullptr || &target == &caster)
        return;
    for (int i = 0; i < 4; ++i) {
        const auto kind = Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_KIND + i * 4);
        if (kind != Offsets::SPELLVISUALKIT_PROC_CHAIN && kind != Offsets::SPELLVISUALKIT_PROC_CHAIN_ALT)
            continue;
        const uint8_t chainID = DecodeKitByte(Game::Read<float>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_VALUE + i * 4));
        uint32_t strands = DecodeKitByte(Game::Read<float>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_VALUE2 + i * 4));
        const bool persistent =
            DecodeKitByte(Game::Read<float>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_VALUE3 + i * 4)) != 0;
        const uint8_t *record =
            DBC::Record(Offsets::VAR_SPELLCHAINEFFECTS_RECORDS, Offsets::VAR_SPELLCHAINEFFECTS_COUNT, chainID);
        if (record == nullptr || strands == 0)
            continue;
        if (strands > Offsets::CHAIN_MAX_STRANDS)
            strands = Offsets::CHAIN_MAX_STRANDS;
        void *frame = caster.SceneFrame();
        State &state = Make(frame);
        void *texture = ChainTexture(state, chainID, record);
        const uint32_t now = ClockMs(frame);
        const auto life = static_cast<uint32_t>(Game::Read<int32_t>(record, Offsets::OFF_SPELLCHAINEFFECTS_LIFE));
        for (uint32_t s = 0; s < strands; ++s) {
            Hop hop;
            hop.from = &caster;
            hop.to = &target;
            hop.attachmentIndex = Game::Read<uint32_t>(visual, Offsets::OFF_SPELLVISUAL_MISSILE_ATTACHMENT);
            hop.endMs = now + life;
            hop.record = record;
            hop.texture = texture;
            hop.channelCastID = persistent ? channelCastID : 0;
            state.hops.push_back(hop);
        }
    }
}

// The hop update (0x006EC460) for each hop, then the renderer's update.
void Update(void *frame, float sceneSec) {
    State *state = Find(frame);
    if (state == nullptr)
        return;
    const uint32_t now = ClockMs(frame);
    for (size_t i = 0; i < state->hops.size();) {
        Hop &hop = state->hops[i];
        if (hop.channelCastID == 0 && Time::Clock::Reached(now, hop.endMs)) {
            DestroyBolt(*state, hop.handle);
            state->hops.erase(state->hops.begin() + static_cast<ptrdiff_t>(i));
            continue;
        }
        if (!hop.from->shown || !hop.to->shown) {
            if (hop.handle != -1)
                ShowBolt(*state, hop.handle, false);
            ++i;
            continue;
        }
        float start[3];
        float end[3];
        CasterEnd(*hop.from, start);
        TargetEnd(*hop.to, hop.attachmentIndex, end);
        if (hop.handle == -1)
            hop.handle = CreateBolt(*state, start, end, hop.record, hop.texture);
        else
            reinterpret_cast<void(__thiscall *)(void *, int, const float *, const float *)>(
                Offsets::FUN_LIGHTNING_MOVE_BOLT)(state->renderer, hop.handle, start, end);
        ShowBolt(*state, hop.handle, true);
        ++i;
    }
    reinterpret_cast<void(__thiscall *)(void *, float)>(Offsets::FUN_LIGHTNING_UPDATE)(state->renderer, sceneSec);
}

void Render(void *frame, const float (&eye)[3]) {
    State *state = Find(frame);
    if (state == nullptr || state->hops.empty())
        return;
    reinterpret_cast<void(__thiscall *)(void *, const float *)>(Offsets::FUN_LIGHTNING_RENDER)(state->renderer, eye);
}

void End(Actor &caster, uint32_t castID) {
    State *state = Find(caster.SceneFrame());
    if (state == nullptr || castID == 0)
        return;
    for (size_t i = 0; i < state->hops.size();) {
        Hop &hop = state->hops[i];
        if (hop.from != &caster || hop.channelCastID != castID) {
            ++i;
            continue;
        }
        DestroyBolt(*state, hop.handle);
        state->hops.erase(state->hops.begin() + static_cast<ptrdiff_t>(i));
    }
}

void Forget(Actor &actor) {
    State *state = Find(actor.SceneFrame());
    if (state == nullptr)
        return;
    for (size_t i = 0; i < state->hops.size();) {
        Hop &hop = state->hops[i];
        if (hop.from != &actor && hop.to != &actor) {
            ++i;
            continue;
        }
        DestroyBolt(*state, hop.handle);
        state->hops.erase(state->hops.begin() + static_cast<ptrdiff_t>(i));
    }
}

void Destroy(void *frame) {
    const auto it = g_states.find(frame);
    if (it == g_states.end())
        return;
    State &state = *it->second;
    reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_LIGHTNING_DESTROY)(state.renderer);
    for (const auto &[id, texture] : state.textures)
        if (texture != nullptr)
            ::Texture::ReleaseHandle(texture);
    g_states.erase(it);
}

} // namespace Model::Scene::Chains
