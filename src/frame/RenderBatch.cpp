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

#include "frame/RenderBatch.h"

#include "Game.h"
#include "Offsets.h"

namespace Frame::RenderBatch {

namespace {

using GetRenderable_t = void *(__fastcall *)(void *texture, int load, void *callback);
using ReserveQuads_t = void(__thiscall *)(void *array, uint32_t extra, int round);

struct QuadRecord {
    void *deviceTexture;
    const C3Vector *positions;
    const C2Vector *texCoords;
    uint32_t blend;
    const uint32_t *color;
    uint32_t colorStride;
    void *shader;
};
static_assert(sizeof(QuadRecord) == Offsets::RENDERBATCH_QUAD_SIZE, "quad record layout");

} // namespace

void QueueStrip(void *batch, void *texture, Blend blend, const C3Vector *positions,
                const C2Vector *texCoords, const uint32_t *color) {
    // Loading on demand, as the texture emit does: a texture still streaming
    // is finished here, so the first rebuild already draws it.
    void *device = reinterpret_cast<GetRenderable_t>(Offsets::FUN_TEXTURE_GET_RENDERABLE)(
        texture, 1, nullptr);
    if (device == nullptr)
        return;
    reinterpret_cast<ReserveQuads_t>(Offsets::FUN_RENDERBATCH_RESERVE_QUADS)(
        Game::Ptr<uint8_t>(batch, Offsets::OFF_RENDERBATCH_QUADS), 1, 1);
    uint32_t &count = Game::Ref<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT);
    QuadRecord &r = Game::Ref<QuadRecord *>(batch, Offsets::OFF_RENDERBATCH_QUAD_RECORDS)[count];
    r.deviceTexture = device;
    r.positions = positions;
    r.texCoords = texCoords;
    r.blend = blend;
    r.color = color;
    r.colorStride = 0;
    r.shader = nullptr;
    ++count;
    ++Game::Ref<uint32_t>(batch, Offsets::OFF_RENDERBATCH_ENTRY_COUNT);
}

} // namespace Frame::RenderBatch
