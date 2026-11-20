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
// alpha, and full alpha that length in.
//
// As in the 3.80.2 client, a gradient draws only on a render node (IsNode: a
// frame that clips its children, or a ScrollFrame's scroll child), over the
// node's own regions and everything under it, and a node nested inside fades
// by its own gradient alone (the shader gets only the nearest node's,
// 0x14059B150), so an outer fade stops at a nested clipping frame or scroll
// child. HasAlphaGradient answers whether one is set either way.
//
// SetAlphaGradient reads as 3.80.2's (0x140A8B170): `index` an integer in
// int32 range, truncated, of which only 0 and 1 store anything (another index
// still counts as a gradient); `gradient` a table whose x and y are finite
// numbers in float range.
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
// Frame::ClipChildren draws the regions under each clipping frame from batches
// of their own and calls Begin and End around each, and around each frame a
// ScrollFrame draws that is a node, its scroll child included.
//
// The OpenGL device takes no texture transform kind, so it draws without the
// fade, as it draws without a parametric desaturation.

#include "frame/AlphaGradient.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/Destruction.h"
#include "frame/Flags.h"
#include "frame/Layout.h"
#include "gx/Device.h"
#include "texture/Load.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace Frame::AlphaGradient {

namespace {

using SetProjection_t = void(__fastcall *)(const float *m);
using SetViewport_t = void(__stdcall *)(float minX, float maxX, float minY, float maxY, float minZ, float maxZ);

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
    bool muted; // a nested node is drawing; the stage fades nothing
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
    return Frame::Flags::Has(frame, Offsets::FRAME_FLAG_ALPHA_GRADIENT);
}


using Gx::IsD3d;

// The highest stage a fade may take.
int LastUnit() {
    const int cap = Game::Read<int>(Game::Read<void *>(Offsets::VAR_GX_DEVICE), Offsets::OFF_GXDEV_STAGE_CAP);
    return cap - 1 < kMaxStages ? cap - 1 : kMaxStages;
}

void *RampTexture() {
    if (g_ramp == nullptr)
        g_ramp = Texture::CreateFromPixels("ClassicAPI\\AlphaGradient", 2, 2, kRampTexels, Texture::DefaultFlags());
    return g_ramp != nullptr
               ? Texture::DeviceTexture(g_ramp)
               : nullptr;
}

// The coefficients over clip (x, y, z, w) of a ramp coordinate times w, along
// `axis` of a viewport from `vMin` to `vMax`: 1/4 at screen fraction p0 and
// 3/4 at p1, with s = vMin + (c / w + 1) / 2 * (vMax - vMin). Without a fade
// it is 1, past the opaque texel.
void RampColumn(const Stage &s, int axis, float vMin, float vMax, float col[4]) {
    col[0] = col[1] = col[2] = 0.0f;
    col[3] = 1.0f;
    if (!s.on[axis] || s.muted)
        return;
    const float a = 0.5f / (s.p1[axis] - s.p0[axis]);
    const float half = 0.5f * (vMax - vMin);
    col[axis] = a * half;
    col[3] = a * (vMin + half - s.p0[axis]) + 0.25f;
}

void Load(const Stage &s) {
    Gx::Matrix p;
    Gx::GetProjection(p);
    const Gx::Viewport vp = Gx::GetViewport();
    float u[4], v[4];
    RampColumn(s, 0, vp.minX, vp.maxX, u);
    RampColumn(s, 1, vp.minY, vp.maxY, v);
    float m[16];
    for (int r = 0; r < 4; ++r) {
        const float *row = p + r * 4;
        m[r * 4 + 0] = row[0] * u[0] + row[1] * u[1] + row[2] * u[2] + row[3] * u[3];
        m[r * 4 + 1] = row[0] * v[0] + row[1] * v[1] + row[2] * v[2] + row[3] * v[3];
        m[r * 4 + 2] = row[3];
        m[r * 4 + 3] = 0.0f;
    }
    Gx::SetTransform(s.unit, m);
}

// Appends the stages of `frame`'s gradient at g_stages[g_count + n]. Returns
// the new n.
int AddStages(const void *frame, int n, int lastUnit) {
    auto it = g_gradients.find(frame);
    if (it == g_gradients.end())
        return n;
    float rect[4];
    if (!Frame::Layout::GetRect(frame, rect))
        return n;
    const float scale = Game::Read<float>(frame, Offsets::OFF_REGION_EFFECTIVE_SCALE);
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
        s.muted = false;
        Frame::Layout::DdcToNdc(&s.p0[0], &s.p0[1], ex, ey);
        Frame::Layout::DdcToNdc(&s.p1[0], &s.p1[1], ex + dx, ey + dy);
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

// The gradient is read at each draw, and the batches follow clipping alone,
// so nothing is regrouped.
void SetFlag(void *frame, bool on) { Frame::Flags::Set(frame, Offsets::FRAME_FLAG_ALPHA_GRADIENT, on); }

void OnFrameDestroyed(void *frame) {
    if (Has(frame))
        g_gradients.erase(frame);
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

#define SET_ALPHA_GRADIENT_USAGE "Usage: self:SetAlphaGradient(index, gradient)"

// A finite number in float range, as 3.80.2's vector reader (0x140A6F5B0) takes.
bool ReadComponent(void *L, int index, float *out) {
    if (!Game::Lua::IsNumber(L, index))
        return false;
    const double v = Game::Lua::ToNumber(L, index);
    if (!std::isfinite(v) || v < -FLT_MAX || v > FLT_MAX)
        return false;
    *out = static_cast<float>(v);
    return true;
}

// gradient.x and gradient.y of the table at `index`.
bool ReadVector(void *L, int index, float *x, float *y) {
    if (Game::Lua::Type(L, index) != Game::Lua::TYPE_TABLE)
        return false;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "x");
    Game::Lua::GetTable(L, index);
    Game::Lua::PushString(L, "y");
    Game::Lua::GetTable(L, index);
    const bool ok = ReadComponent(L, -2, x) && ReadComponent(L, -1, y);
    Game::Lua::SetTop(L, top);
    return ok;
}

int __fastcall Script_SetAlphaGradient(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, SET_ALPHA_GRADIENT_USAGE);
        return 0;
    }
    const double index = Game::Lua::IsNumber(L, 2) ? Game::Lua::ToNumber(L, 2) : NAN;
    if (!(index >= -2147483648.0 && index <= 2147483647.0)) {
        Game::Lua::Error(L, "outside of expected range -2147483648 to 2147483647 - " SET_ALPHA_GRADIENT_USAGE);
        return 0;
    }
    float x = 0.0f, y = 0.0f;
    if (!ReadVector(L, 3, &x, &y)) {
        Game::Lua::Error(L, SET_ALPHA_GRADIENT_USAGE);
        return 0;
    }
    Gradient &g = g_gradients[frame]; // made even for an index that stores nothing
    const int i = static_cast<int>(index);
    if (i == 0 || i == 1) {
        g.x[i] = x;
        g.y[i] = y;
    }
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
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods, g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool IsNode(const void *frame) {
    return Frame::Flags::Has(frame, Offsets::FRAME_FLAG_CLIPS_CHILDREN) ||
           (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_SCROLL_CHILD) != 0;
}

// A token: the node's own stages in the low 4 bits, the outer stages it muted
// as a mask from bit 4.
int Begin(const void *node) {
    if (node == nullptr || !IsD3d())
        return 0;
    int muted = 0;
    for (int i = 0; i < g_count; ++i) {
        if (!g_stages[i].muted) {
            g_stages[i].muted = true;
            Load(g_stages[i]);
            muted |= 1 << i;
        }
    }
    int n = 0;
    if (Has(node) && IsNode(node))
        n = AddStages(node, 0, LastUnit());
    void *ramp = n != 0 ? RampTexture() : nullptr;
    if (ramp == nullptr)
        n = 0;
    if (n != 0) {
        Gx::PushStates();
        for (int i = g_count; i < g_count + n; ++i) {
            const int unit = g_stages[i].unit;
            Gx::SetState(Offsets::GXRS_TEXTURE0 + unit, ramp);
            Gx::SetState(Offsets::GXRS_COMBINER0 + unit, Offsets::GXRS_COMBINE_MODULATE);
            Gx::SetState(Offsets::GXRS_TEXGEN0 + unit, Offsets::GXRS_TEXGEN_CAMERA_POS);
            Gx::SetState(Offsets::GXRS_TEXXFORM0 + unit, Offsets::GXRS_TEXXFORM_PROJECTED);
            Gx::PushTransform(unit);
            Load(g_stages[i]);
        }
        g_count += n;
    }
    return n | (muted << 4);
}

void End(int token) {
    const int n = token & 0xF;
    if (n != 0) {
        for (int i = g_count - 1; i >= g_count - n; --i)
            Gx::PopTransform(g_stages[i].unit);
        g_count -= n;
        Gx::PopStates();
    }
    const int muted = token >> 4;
    for (int i = 0; i < g_count; ++i) {
        if ((muted & (1 << i)) != 0) {
            g_stages[i].muted = false;
            Load(g_stages[i]);
        }
    }
}

int StagesInUse() { return g_count; }

} // namespace Frame::AlphaGradient
