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

// `frame:SetAlphaGradient(index, gradient)`, `frame:ClearAlphaGradient()` and
// `frame:HasAlphaGradient()`.
//
// A gradient fades what the frame draws toward its edges. Index 0 holds the
// left and top edges, index 1 the right and bottom; the gradient's x is the
// length of the fade in from the left or right edge, its y the length in from
// the top or bottom, in the frame's own units. What is drawn at an edge has no
// alpha, and full alpha that length in. As on retail (probed), the fade
// applies only while the frame clips its children (SetClipsChildren), and then
// to its own regions too; HasAlphaGradient answers whether one is set either way.
//
// The fade is a texture stage per index on top of what each draw binds: a 2x2
// texture, opaque white in one texel and clear in the other three, filtered,
// clamped, and combined with preset 1, which multiplies the draw's alpha by
// its own. With texel centres at 1/4 and 3/4, the filtered alpha is the product
// of two clamped linear ramps, one in u and one in v, so one stage holds an
// index's horizontal and vertical fades. Texture::Mask makes masks the same
// way, stages from 1 up, and takes its own above the ones held here.
//
// The coordinates come from the camera-space position (texgen mode 3) through
// the stage's matrix with transform kind 2, which divides by the third
// coordinate. The matrix takes a position to clip space through the current
// projection, then to the screen through the current viewport, then to the
// ramps; the third coordinate is clip w. Text draws in pixels under its own
// projection (CGxStringBatch::Draw sets one through SnapViewportRect), and a
// clipped draw narrows the viewport, so while a gradient is drawn every
// projection and viewport set reloads the matrices. The fade then lands on
// the same screen spot for textures, text and models alike.
//
// Frame::ClipChildren draws the regions of a frame with a gradient, and of all
// frames under it, from batches of their own, and calls Begin and End around
// each. A frame a ScrollFrame draws is drawn through its own draw, which
// ClipChildren also wraps.
//
// The OpenGL device takes no texture transform kind, so it draws without the
// fade, as it draws without a parametric desaturation.

#include "frame/AlphaGradient.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/ClipChildren.h"
#include "frame/Destruction.h"
#include "texture/Load.h"

#include <cstdint>
#include <unordered_map>

namespace Frame::AlphaGradient {

namespace {

using Matrix_t = void(__fastcall *)(float *m);
using SetProjection_t = void(__fastcall *)(const float *m);
using GetViewport_t = void(__fastcall *)(float *minX, float *maxX, float *minY, float *maxY, float *minZ,
                                         float *maxZ);
using SetViewport_t = void(__stdcall *)(float minX, float maxX, float minY, float maxY, float minZ, float maxZ);
using Convert_t = void(__fastcall *)(float *outX, float *outY, float x, float y);
using GetRect_t = int(__fastcall *)(void *layout, void *edx, float *out);
using GetRenderable_t = void *(__fastcall *)(void *hTex, int force, void *);
using RsSet_t = void(__fastcall *)(int selector, int value);
using RsSetPtr_t = void(__fastcall *)(int selector, const void *value);
using StateOp_t = void(__fastcall *)();
using TexMtxLoad_t = void(__fastcall *)(int unit, const float *m);
using TexMtxOp_t = void(__fastcall *)(int unit);
using SetFrameFlag_t = void(__fastcall *)(void *frame, void *edx, uint32_t mask, int on);

// A rect: {bottom, left, top, right}.
enum { BOTTOM, LEFT, TOP, RIGHT };

// Fade lengths in the frame's own units, per index.
struct Gradient {
    float x[2];
    float y[2];
};

std::unordered_map<const void *, Gradient> g_gradients;

// A stage being drawn: per axis (x, y), whether it fades and the screen
// fractions where its ramp is 0 and 1.
struct Stage {
    int unit;
    bool on[2];
    float p0[2];
    float p1[2];
};

constexpr int kMaxStages = 7;
Stage g_stages[kMaxStages];
int g_count = 0;

// Opaque white at (1, 1), clear white elsewhere. The engine reads these
// whenever it uploads the texture.
const uint32_t kRampTexels[4] = {0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0xFFFFFFFF};
void *g_ramp = nullptr;

bool Has(const void *frame) {
    return (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_ALPHA_GRADIENT) != 0;
}

// The frame's gradient is drawn: it has one and clips its children.
bool Applies(const void *frame) {
    constexpr uint32_t both = Offsets::FRAME_FLAG_ALPHA_GRADIENT | Offsets::FRAME_FLAG_CLIPS_CHILDREN;
    return (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & both) == both;
}

void *Parent(const void *frame) { return Game::Read<void *>(frame, Offsets::OFF_REGION_PARENT); }

bool IsD3d() {
    void *dev = Game::Read<void *>(Offsets::VAR_GX_DEVICE);
    return dev != nullptr &&
           Game::Read<void *>(dev, 0) == reinterpret_cast<void *>(Offsets::PTR_GXDEVICE_D3D_VTBL);
}

// The highest stage a fade may take.
int LastUnit() {
    const int cap = Game::Read<int>(Game::Read<void *>(Offsets::VAR_GX_DEVICE), Offsets::OFF_GXDEV_STAGE_CAP);
    return cap - 1 < kMaxStages ? cap - 1 : kMaxStages;
}

void *RampTexture() {
    if (g_ramp == nullptr)
        g_ramp = Texture::CreateFromPixels("ClassicAPI\\AlphaGradient", 2, 2, kRampTexels, Texture::DefaultFlags());
    return g_ramp != nullptr
               ? reinterpret_cast<GetRenderable_t>(Offsets::FUN_TEXTURE_GET_RENDERABLE)(g_ramp, 1, nullptr)
               : nullptr;
}

// The coefficients over clip (x, y, z, w) of a ramp coordinate times w, along
// `axis` of a viewport from `vMin` to `vMax`: 1/4 at screen fraction p0 and
// 3/4 at p1, with s = vMin + (c / w + 1) / 2 * (vMax - vMin). Without a fade
// it is 1, past the opaque texel.
void RampColumn(const Stage &s, int axis, float vMin, float vMax, float col[4]) {
    col[0] = col[1] = col[2] = 0.0f;
    col[3] = 1.0f;
    if (!s.on[axis])
        return;
    const float a = 0.5f / (s.p1[axis] - s.p0[axis]);
    const float half = 0.5f * (vMax - vMin);
    col[axis] = a * half;
    col[3] = a * (vMin + half - s.p0[axis]) + 0.25f;
}

void Load(const Stage &s) {
    float p[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_PROJECTION)(p);
    float minX, maxX, minY, maxY, minZ, maxZ;
    reinterpret_cast<GetViewport_t>(Offsets::FUN_GX_GET_VIEWPORT)(&minX, &maxX, &minY, &maxY, &minZ, &maxZ);
    float u[4], v[4];
    RampColumn(s, 0, minX, maxX, u);
    RampColumn(s, 1, minY, maxY, v);
    float m[16];
    for (int r = 0; r < 4; ++r) {
        const float *row = p + r * 4;
        m[r * 4 + 0] = row[0] * u[0] + row[1] * u[1] + row[2] * u[2] + row[3] * u[3];
        m[r * 4 + 1] = row[0] * v[0] + row[1] * v[1] + row[2] * v[2] + row[3] * v[3];
        m[r * 4 + 2] = row[3];
        m[r * 4 + 3] = 0.0f;
    }
    reinterpret_cast<TexMtxLoad_t>(Offsets::FUN_GX_TEXMTX_LOAD)(s.unit, m);
}

// Appends the stages of `frame`'s gradient at g_stages[g_count + n]. Returns
// the new n.
int AddStages(const void *frame, int n, int lastUnit) {
    auto it = g_gradients.find(frame);
    if (it == g_gradients.end())
        return n;
    float rect[4];
    void *layout = const_cast<uint8_t *>(static_cast<const uint8_t *>(frame) + Offsets::OFF_REGION_ANCHOR);
    if (reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(layout, nullptr, rect) == 0)
        return n;
    const float scale = Game::Read<float>(frame, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    const auto toNdc = reinterpret_cast<Convert_t>(Offsets::FUN_COORD_DDC_TO_NDC);
    for (int index = 0; index < 2; ++index) {
        const float lx = it->second.x[index], ly = it->second.y[index];
        if (!(lx > 0.0f) && !(ly > 0.0f))
            continue;
        if (g_count + n >= kMaxStages || 1 + g_count + n > lastUnit)
            return n;
        // Index 0 fades in from the left and top, index 1 from the right and bottom.
        const float ex = index == 0 ? rect[LEFT] : rect[RIGHT];
        const float ey = index == 0 ? rect[TOP] : rect[BOTTOM];
        const float dx = Game::PixelToInternal(lx * scale) * (index == 0 ? 1.0f : -1.0f);
        const float dy = Game::PixelToInternal(ly * scale) * (index == 0 ? -1.0f : 1.0f);
        Stage &s = g_stages[g_count + n];
        s.unit = 1 + g_count + n;
        toNdc(&s.p0[0], &s.p0[1], ex, ey);
        toNdc(&s.p1[0], &s.p1[1], ex + dx, ey + dy);
        s.on[0] = lx > 0.0f && s.p1[0] != s.p0[0];
        s.on[1] = ly > 0.0f && s.p1[1] != s.p0[1];
        if (s.on[0] || s.on[1])
            ++n;
    }
    return n;
}

SetProjection_t g_origSetProjection = nullptr;
SetViewport_t g_origSetViewport = nullptr;

// Every projection or viewport set while a gradient is drawn reloads its
// stages' matrices.
void __fastcall SetProjection_h(const float *m) {
    g_origSetProjection(m);
    for (int i = 0; i < g_count; ++i)
        Load(g_stages[i]);
}

void __stdcall SetViewport_h(float minX, float maxX, float minY, float maxY, float minZ, float maxZ) {
    g_origSetViewport(minX, maxX, minY, maxY, minZ, maxZ);
    for (int i = 0; i < g_count; ++i)
        Load(g_stages[i]);
}

const Game::HookAutoRegister _projectionHook{Offsets::FUN_GX_SET_PROJECTION,
                                             reinterpret_cast<void *>(&SetProjection_h),
                                             reinterpret_cast<void **>(&g_origSetProjection)};
const Game::HookAutoRegister _viewportHook{Offsets::FUN_GX_SET_VIEWPORT, reinterpret_cast<void *>(&SetViewport_h),
                                           reinterpret_cast<void **>(&g_origSetViewport)};

void SetFlag(void *frame, bool on) {
    if (Has(frame) == on)
        return;
    reinterpret_cast<SetFrameFlag_t>(Offsets::FUN_FRAME_SET_FRAME_FLAG)(frame, nullptr,
                                                                       Offsets::FRAME_FLAG_ALPHA_GRADIENT, on);
    Frame::ClipChildren::Regroup(frame);
}

void OnFrameDestroyed(void *frame) {
    if (Has(frame))
        g_gradients.erase(frame);
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

// gradient.x and gradient.y of the table at `index`.
bool ReadVector(void *L, int index, float *x, float *y) {
    if (Game::Lua::Type(L, index) != Game::Lua::TYPE_TABLE)
        return false;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "x");
    Game::Lua::GetTable(L, index);
    Game::Lua::PushString(L, "y");
    Game::Lua::GetTable(L, index);
    const bool ok = Game::Lua::IsNumber(L, -2) && Game::Lua::IsNumber(L, -1);
    if (ok) {
        *x = static_cast<float>(Game::Lua::ToNumber(L, -2));
        *y = static_cast<float>(Game::Lua::ToNumber(L, -1));
    }
    Game::Lua::SetTop(L, top);
    return ok;
}

int __fastcall Script_SetAlphaGradient(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    float x = 0.0f, y = 0.0f;
    const double index = Game::Lua::IsNumber(L, 2) ? Game::Lua::ToNumber(L, 2) : -1.0;
    if ((index != 0.0 && index != 1.0) || !ReadVector(L, 3, &x, &y)) {
        Game::Lua::Error(L, "Usage: %s:SetAlphaGradient(index, gradient)", Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    Gradient &g = g_gradients[frame];
    g.x[static_cast<int>(index)] = x;
    g.y[static_cast<int>(index)] = y;
    SetFlag(frame, true);
    return 0;
}

int __fastcall Script_ClearAlphaGradient(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    g_gradients.erase(frame);
    SetFlag(frame, false);
    return 0;
}

int __fastcall Script_HasAlphaGradient(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, Has(frame));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetAlphaGradient", &Script_SetAlphaGradient},
    {"ClearAlphaGradient", &Script_ClearAlphaGradient},
    {"HasAlphaGradient", &Script_HasAlphaGradient},
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("index", "number", "0 for the left and top edges, 1 for the right and bottom edges."),
    Game::Doc::Req("gradient", "vector2",
                   "x is the length of the fade in from the left or right edge, y from the top or bottom edge."),
};
const Game::Doc::Field kHasRets[] = {
    Game::Doc::Req("hasAlphaGradient", "bool"),
};
const Game::Doc::Function kSetAlphaGradient{"Fades what the frame draws toward two of its edges.", kSetArgs, {}};
const Game::Doc::Function kClearAlphaGradient{"Removes the frame's alpha gradient.", {}, {}};
const Game::Doc::Function kHasAlphaGradient{"Returns whether the frame has an alpha gradient.", {}, kHasRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetAlphaGradient", &kSetAlphaGradient},
    {"ClearAlphaGradient", &kClearAlphaGradient},
    {"HasAlphaGradient", &kHasAlphaGradient},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool Any() { return !g_gradients.empty(); }

void *Nearest(const void *frame) {
    if (g_gradients.empty())
        return nullptr;
    for (const void *p = frame; p != nullptr; p = Parent(p))
        if (Applies(p))
            return const_cast<void *>(p);
    return nullptr;
}

int Begin(const void *frame, bool ancestors) {
    if (g_gradients.empty() || frame == nullptr || !IsD3d())
        return 0;
    const int lastUnit = LastUnit();
    int n = 0;
    for (const void *p = frame; p != nullptr; p = ancestors ? Parent(p) : nullptr)
        if (Applies(p))
            n = AddStages(p, n, lastUnit);
    if (n == 0)
        return 0;
    void *ramp = RampTexture();
    if (ramp == nullptr)
        return 0;
    const auto rs = reinterpret_cast<RsSet_t>(Offsets::FUN_GX_RS_SET);
    const auto rsPtr = reinterpret_cast<RsSetPtr_t>(Offsets::FUN_GX_RS_SET_PTR);
    reinterpret_cast<StateOp_t>(Offsets::FUN_GX_STATE_PUSH)();
    for (int i = g_count; i < g_count + n; ++i) {
        const int unit = g_stages[i].unit;
        rsPtr(Offsets::GXRS_TEXTURE0 + unit, ramp);
        rs(Offsets::GXRS_COMBINER0 + unit, Offsets::GXRS_COMBINE_MODULATE);
        rs(Offsets::GXRS_TEXGEN0 + unit, Offsets::GXRS_TEXGEN_CAMERA_POS);
        rs(Offsets::GXRS_TEXXFORM0 + unit, Offsets::GXRS_TEXXFORM_PROJECTED);
        reinterpret_cast<TexMtxOp_t>(Offsets::FUN_GX_TEXMTX_PUSH)(unit);
        Load(g_stages[i]);
    }
    g_count += n;
    return n;
}

void End(int stages) {
    if (stages == 0)
        return;
    for (int i = g_count - 1; i >= g_count - stages; --i)
        reinterpret_cast<TexMtxOp_t>(Offsets::FUN_GX_TEXMTX_POP)(g_stages[i].unit);
    g_count -= stages;
    reinterpret_cast<StateOp_t>(Offsets::FUN_GX_STATE_POP)();
}

int StagesInUse() { return g_count; }

} // namespace Frame::AlphaGradient
