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

// A texture drawn as more than its one quad (see Pieces.h).
//
// THE EMIT. A texture emits one quad record into its layer batch
// (FUN_SIMPLETEXTURE_APPEND_QUAD), and the batch keeps pointers to the
// texture's four positions (+0xD4), eight coordinates (+0x104) and colours,
// read on every draw. Emit runs the engine's own append once more per extra
// quad, so every quad carries the engine's handle, blend, stride and shader,
// then points the records at the owner's arrays.
//
// THE DRAW. The batch draw (FUN_FRAME_LAYER_RENDER) is hooked once here.
// Before the engine draws, every record that points at the first quad of a
// registered block has its owner work the block out again, from what the
// texture holds at that moment.

#include "texture/Pieces.h"

#include "Game.h"
#include "Offsets.h"

#include <algorithm>
#include <map>

namespace Texture::Pieces {

namespace {

using Frame::RenderBatch::QuadRecord;

// A record's colour stride for one colour per vertex (RenderBatch.h).
constexpr uint32_t kColorPerVertex = 4;

struct Entry {
    int capacity;
    void *texture;
    Refresh refresh;
    void *block;
};

// Every block by the address of its first position.
std::map<uintptr_t, Entry> g_blocks;

using Append_t = void(__thiscall *)(void *batch, void *texture);
using Notify_t = void(__thiscall *)(void *region);
using DrawBatch_t = void(__fastcall *)(void *batch);

DrawBatch_t g_origDrawBatch = nullptr;

void __fastcall DrawBatch_h(void *batch) {
    if (!g_blocks.empty()) {
        const uint32_t count = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT);
        const auto *records = Game::Read<const QuadRecord *>(batch, Offsets::OFF_RENDERBATCH_QUAD_RECORDS);
        for (uint32_t i = 0; i < count; ++i) {
            const auto it = g_blocks.find(reinterpret_cast<uintptr_t>(records[i].positions));
            if (it != g_blocks.end())
                it->second.refresh(it->second.block);
        }
    }
    g_origDrawBatch(batch);
}

const Game::HookAutoRegister _drawHook{Offsets::FUN_FRAME_LAYER_RENDER, reinterpret_cast<void *>(&DrawBatch_h),
                                       reinterpret_cast<void **>(&g_origDrawBatch)};

} // namespace

void Add(const Arrays &arrays, void *texture, Refresh refresh, void *block) {
    g_blocks[reinterpret_cast<uintptr_t>(arrays.positions)] = {arrays.capacity, texture, refresh, block};
}

void Remove(const Arrays &arrays) { g_blocks.erase(reinterpret_cast<uintptr_t>(arrays.positions)); }

Emitted Emit(void *texture, void *batch, int needed, const Arrays &arrays, bool ownColors) {
    const uint32_t first = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT) - 1;
    for (int i = 1; i < needed; ++i)
        reinterpret_cast<Append_t>(Offsets::FUN_SIMPLETEXTURE_APPEND_QUAD)(batch, texture);
    // Read after the appends: a reserve can move the records.
    const uint32_t count = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT);
    auto *records = Game::Read<QuadRecord *>(batch, Offsets::OFF_RENDERBATCH_QUAD_RECORDS);
    Emitted out;
    out.records = static_cast<int>(std::min<uint32_t>(count - first, static_cast<uint32_t>(needed)));
    out.srcColor = records[first].color;
    out.colorStride = records[first].colorStride;
    for (int k = 0; k < out.records; ++k) {
        records[first + k].positions = &arrays.positions[static_cast<size_t>(k) * 4];
        records[first + k].texCoords = &arrays.coords[static_cast<size_t>(k) * 4];
        if (out.colorStride != 0 || ownColors) {
            records[first + k].color = &arrays.colors[static_cast<size_t>(k) * 4];
            records[first + k].colorStride = kColorPerVertex;
        }
    }
    return out;
}

void *Owner(const void *positions) {
    if (g_blocks.empty())
        return nullptr;
    auto it = g_blocks.upper_bound(reinterpret_cast<uintptr_t>(positions));
    if (it == g_blocks.begin())
        return nullptr;
    --it;
    if (reinterpret_cast<uintptr_t>(positions) >=
        it->first + static_cast<uintptr_t>(it->second.capacity) * 4 * sizeof(C3Vector))
        return nullptr;
    return it->second.texture;
}

void NotifyLayer(void *texture) {
    reinterpret_cast<Notify_t>(Offsets::FUN_REGION_NOTIFY_LAYER_CHANGED)(texture);
}

C3Vector Blend(const C3Vector *c, float u, float v) {
    const float a = (1.0f - u) * (1.0f - v), b = (1.0f - u) * v, d = u * (1.0f - v), e = u * v;
    return {a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x, a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y,
            a * c[0].z + b * c[1].z + d * c[2].z + e * c[3].z};
}

C2Vector Blend(const C2Vector *c, float u, float v) {
    const float a = (1.0f - u) * (1.0f - v), b = (1.0f - u) * v, d = u * (1.0f - v), e = u * v;
    return {a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x, a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y};
}

uint32_t BlendColor(const uint32_t *c, float u, float v) {
    const float w[4] = {(1.0f - u) * (1.0f - v), (1.0f - u) * v, u * (1.0f - v), u * v};
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i)
            sum += w[i] * static_cast<float>((c[i] >> shift) & 0xFF);
        out |= static_cast<uint32_t>(std::min(255.0f, sum + 0.5f)) << shift;
    }
    return out;
}

} // namespace Texture::Pieces
