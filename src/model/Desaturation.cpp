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

// Model desaturation, 3.80.2's actor:SetDesaturation. 3.80.2 puts a model
// effect (type 4, model +0x738) with the strength on the instance and flags its
// batches and attached children (0x14194A9F0 -> 0x140EEB890); its renderer then
// draws them with a desaturating shader.
//
// 1.12's model draw has two lanes, and we desaturate inside both by binding
// our own pixel shader for the batch, through the engine's own render state
// (GxRs 0x3F), around the batch's one draw call:
//   * the pixel lane (M2UsePixelShaders): the batch draw binds one of the 32
//     Model2 programs of the cache. We bind that program with its closing
//     `mov oC0, r0` replaced by the desaturation (Programs::Patched), built
//     from the code the engine loaded.
//   * the fixed lane: the batch binds no shader and runs stages 0 and 1 by
//     their combine presets. We bind a ps_2_0 program that computes the same
//     cascade (Programs::Fixed) from the presets and textures the batch set.
// The amount is baked into each program (256 levels, created lazily through
// the engine's loader, as Texture::Desaturation does), so nothing per draw is
// uploaded. After the draw the engine's own shader goes back into the state,
// which is what the batch draw's next-element cache assumes is bound.
//
// The two sites: M2SceneDrawList's kind-0 call of M2DrawBatch (to know the
// draw state, which M2SceneDraw keeps on its stack) and M2DrawBatch's one
// GxPrimRenderOne. Particles, ribbons, projected and batched elements draw
// elsewhere and keep their colour. ps_2_0 and D3D only: on another device the
// amount is kept but the model draws in colour.

#include "model/Desaturation.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "gx/Device.h"
#include "gx/PixelShader.h"
#include "model/DesaturationPrograms.h"

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace Model::Desaturation {

namespace {

namespace P = Programs;

constexpr int kLevels = 256; // amount = level / 255
constexpr int kFull = kLevels - 1;

struct Slot {
    void *shader = nullptr;
    bool failed = false;
};

std::vector<std::pair<void *, int>> g_models; // model -> level 1..kFull
Slot g_fixed[P::kFixedPrograms][kLevels];
Slot g_patched[Offsets::M2_PIXEL_PROGRAM_COUNT][kLevels];
void *g_drawState = nullptr; // the draw state of the batch being drawn

int Level(float amount) {
    if (!(amount > 0.0f))
        return 0;
    const int level = static_cast<int>(std::lround(amount * kFull));
    return level < 1 ? 1 : (level > kFull ? kFull : level);
}

// The nearest entry up the attach chain: an attached model (a weapon, a
// rider) draws as the model it hangs under, as 3.80.2 flags the children.
int LevelOf(void *model) {
    for (; model != nullptr; model = Game::Read<void *>(model, Offsets::OFF_M2MODEL_PARENT))
        for (const auto &[m, level] : g_models)
            if (m == model)
                return level;
    return 0;
}

void *Build(Slot &slot, const char *path, const std::vector<uint32_t> &tokens) {
    if (slot.shader != nullptr || slot.failed)
        return slot.shader;
    if (tokens.empty()) {
        slot.failed = true;
        return nullptr;
    }
    Gx::PixelShader::Result result;
    slot.shader = Gx::PixelShader::Build(path, tokens.data(), static_cast<uint32_t>(tokens.size()), "model desat",
                                         result);
    slot.failed = result == Gx::PixelShader::Result::Rejected;
    return slot.shader;
}

float Amount(int level) { return static_cast<float>(level) / static_cast<float>(kFull); }

// The engine's Model2 program `bound`, desaturated.
void *PatchedShader(void *drawState, void *bound, int level) {
    void *cache = Game::Read<void *>(drawState, Offsets::OFF_M2DRAWSTATE_CACHE);
    void **programs = cache != nullptr ? Game::Read<void **>(cache, Offsets::OFF_M2CACHE_PIXEL_SHADERS) : nullptr;
    if (programs == nullptr)
        return nullptr;
    int index = 0;
    while (index < Offsets::M2_PIXEL_PROGRAM_COUNT && programs[index] != bound)
        ++index;
    if (index == Offsets::M2_PIXEL_PROGRAM_COUNT)
        return nullptr; // not a Model2 program: leave the draw alone
    Slot &slot = g_patched[index][level];
    if (slot.shader != nullptr || slot.failed)
        return slot.shader;
    const auto *code = Game::Read<const uint32_t *>(bound, Offsets::OFF_GXSHADER_CODE_DATA);
    const uint32_t size = Game::Read<uint32_t>(bound, Offsets::OFF_GXSHADER_CODE_SIZE);
    char path[80];
    std::snprintf(path, sizeof path, "Shaders\\Pixel\\ClassicAPI\\M2Desaturate_m%02d_%03d.bls", index, level);
    return Build(slot, path, code != nullptr ? P::Patched(code, size / 4, Amount(level)) : std::vector<uint32_t>{});
}

// The fixed lane's cascade, from the stage states the batch draw set.
void *FixedShader(int level) {
    const bool tex0 = Gx::GetState(Offsets::GXRS_TEXTURE0) != 0;
    const bool tex1 = tex0 && Gx::GetState(Offsets::GXRS_TEXTURE0 + 1) != 0;
    const int p0 = tex0 ? Gx::GetState(Offsets::GXRS_COMBINER0) : -1;
    const int p1 = tex1 ? Gx::GetState(Offsets::GXRS_COMBINER0 + 1) : -1;
    if (p0 >= P::kPresets || p1 >= P::kPresets || p0 < -1 || p1 < -1)
        return nullptr;
    const int index = P::FixedIndex(p0, p1);
    Slot &slot = g_fixed[index][level];
    if (slot.shader != nullptr || slot.failed)
        return slot.shader;
    char path[80];
    std::snprintf(path, sizeof path, "Shaders\\Pixel\\ClassicAPI\\M2Desaturate_f%02d_%03d.bls", index, level);
    return Build(slot, path, P::Fixed(p0, p1, Amount(level)));
}

using DrawBatch_t = void(__fastcall *)(void *drawState);
using RenderOne_t = int(__fastcall *)(void *prim, int count);

void __fastcall DrawBatch(void *drawState) {
    g_drawState = drawState;
    reinterpret_cast<DrawBatch_t>(Offsets::FUN_M2_DRAW_BATCH)(drawState);
    g_drawState = nullptr;
}

int __fastcall RenderBatch(void *prim, int count) {
    const auto render = reinterpret_cast<RenderOne_t>(Offsets::FUN_GX_PRIM_RENDER_ONE);
    if (g_models.empty() || g_drawState == nullptr)
        return render(prim, count);
    const int level = LevelOf(Game::Read<void *>(g_drawState, Offsets::OFF_M2DRAWSTATE_MODEL));
    if (level == 0 || !Gx::IsD3d() || Gx::PixelShader::Profile() < 4)
        return render(prim, count);
    auto *bound = reinterpret_cast<void *>(static_cast<uintptr_t>(Gx::GetState(Offsets::GXRS_PIXEL_SHADER)));
    void *shader = bound != nullptr ? PatchedShader(g_drawState, bound, level) : FixedShader(level);
    if (shader == nullptr)
        return render(prim, count);
    Gx::SetState(Offsets::GXRS_PIXEL_SHADER, shader);
    const int result = render(prim, count);
    Gx::SetState(Offsets::GXRS_PIXEL_SHADER, bound);
    return result;
}

void Install() {
    Common::RedirectCall(Offsets::CALL_M2_DRAW_LIST_BATCH, Offsets::FUN_M2_DRAW_BATCH,
                         reinterpret_cast<const void *>(&DrawBatch));
    Common::RedirectCall(Offsets::CALL_M2_DRAW_BATCH_RENDER, Offsets::FUN_GX_PRIM_RENDER_ONE,
                         reinterpret_cast<const void *>(&RenderBatch));
}

const Game::ModuleAutoRegister _autoreg{&Install};

} // namespace

void Set(void *model, float amount) {
    if (model == nullptr)
        return;
    const int level = Level(amount);
    for (auto it = g_models.begin(); it != g_models.end(); ++it) {
        if (it->first != model)
            continue;
        if (level == 0)
            g_models.erase(it);
        else
            it->second = level;
        return;
    }
    if (level != 0)
        g_models.emplace_back(model, level);
}

} // namespace Model::Desaturation
