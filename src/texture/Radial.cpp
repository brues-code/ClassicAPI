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

// Radial progress bars: `texture:SetRadialProgressBarPercent` / `StartOffset`
// / `EndOffset` / `Reverse` / `Feather`, their getters,
// `ClearRadialProgressBar`, and a texture's <RadialProgressBar> element.
//
// THE CONTRACT. Retail's own (no client we can read has the feature; the
// docs, UI.xsd and its users are the source): a texture in this mode draws
// only the wedge of itself swept around its centre, from a start angle, by a
// share of a turn.
//   - The percent (0 to 1) is how much of the sweep is filled.
//   - The start offset (0 to 1 of a turn) is where the fill starts; 0 is the
//     bottom (the docs).
//   - The end offset (0 to 1) trims the sweep: a full fill covers 1 - end of
//     a turn. Blizzard_Gamepad's reticle draws a full ring with startOffset
//     0.5 and each half ring with endOffset 0.5 from the bottom, the spinner
//     tip turning with each fill: -360 from the top, -180 from the bottom on
//     the left, +180 on the right with reverse.
//   - The fill turns clockwise on screen; reverse turns it counterclockwise.
//     The start offset turns the start clockwise either way.
//   - Feather fades the leading edge, inside the fill, over feather x the
//     filled sweep.
//   - Values clamp to 0..1; a texture's getters read 1, 0, 0, false, 0 until
//     something sets them, and ClearRadialProgressBar puts those back and
//     draws the whole texture.
// The _forever_ probe (2026-11-25, a 15-case panel) confirmed each of these,
// and that the wedge turns with SetRotation and keeps its place under a
// cropped SetTexCoord (it is cut from the quad, not from the coordinates).
//
// THE GEOMETRY. Retail draws it in a shader; here the wedge is the texture's
// own quad cut into a fan of triangles from its centre: the ray at the start
// angle, every corner the sweep passes, the feather's slice edges, the ray at
// the end angle. Each triangle is one batch record (Texture::Pieces) whose
// last two corners are the same point, so its triangle strip draws one
// triangle. The feather is each slice's vertex alphas (retail's shader fades
// per pixel; slices make the same fade piecewise linear).
// The corners, coordinates and (for a gradient) colours of each point are the
// bilinear blend of the texture's own at that point of the quad, worked out at
// every draw, so a moved, scaled, rotated or animated texture carries its
// wedge along, and a mask (Texture::Mask) cuts it as it cuts the texture.
// A radial texture is not sliced (Texture::Slice).

#include "texture/Radial.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/RegionDestruction.h"
#include "lua/MethodArg.h"
#include "texture/Emit.h"
#include "texture/Pieces.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Texture::Radial {

namespace {

using Texture::Pieces::C2Vector;
using Texture::Pieces::C3Vector;

constexpr float kTwoPi = 6.28318530717959f;
// The feather's ramp, in this many slices: each slice's triangles carry its
// two edges' alphas, so the fade is piecewise linear around the turn.
constexpr int kFeatherSlices = 4;
// The fan's outline: the start ray, four corners, the feather's slice edges,
// the end ray; one triangle between each two.
constexpr int kPoints = 2 + 4 + kFeatherSlices;
constexpr int kQuads = kPoints - 1;

// What a texture shows before anything sets it.
constexpr float kDefaultPercent = 1.0f;

struct Block {
    C3Vector positions[kQuads * 4];
    C2Vector coords[kQuads * 4];
    uint32_t colors[kQuads * 4];
    void *texture = nullptr;
    int records = 0;
    const uint32_t *srcColor = nullptr;
    uint32_t colorStride = 0;
};

struct State {
    bool on = false;
    float percent = kDefaultPercent;
    float start = 0.0f;
    float end = 0.0f;
    bool reverse = false;
    float feather = 0.0f;
    Block *block = nullptr;
};

std::unordered_map<void *, State> g_states;
// Arrays a batch may still point at until its rebuild: never freed.
std::vector<Block *> g_free;

Texture::Pieces::Arrays ArraysOf(Block &b) { return {b.positions, b.coords, b.colors, kQuads}; }

void RefreshBlock(void *block);

Block *Acquire(void *texture) {
    Block *b;
    if (!g_free.empty()) {
        b = g_free.back();
        g_free.pop_back();
    } else {
        b = new Block();
    }
    b->texture = texture;
    b->records = 0;
    b->srcColor = nullptr;
    b->colorStride = 0;
    Texture::Pieces::Add(ArraysOf(*b), texture, &RefreshBlock, b);
    return b;
}

void Release(Block *b) {
    if (b == nullptr)
        return;
    Texture::Pieces::Remove(ArraysOf(*b));
    b->texture = nullptr;
    g_free.push_back(b);
}

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float Frac(float x) { return x - std::floor(x); }

struct Point {
    float u, v;
};

// Where the ray from the centre at `turn` (turns clockwise on screen from the
// bottom: u right, v down) leaves the quad.
Point Edge(float turn) {
    const float a = turn * kTwoPi;
    const float dx = -std::sin(a), dy = std::cos(a);
    const float t = 0.5f / std::max(std::fabs(dx), std::fabs(dy));
    return {0.5f + dx * t, 0.5f + dy * t};
}

// The quad's corners in the same turns: bottom-left, top-left, top-right,
// bottom-right.
constexpr float kCornerTurn[4] = {0.125f, 0.375f, 0.625f, 0.875f};

// One point of the outline: where along the sweep it is (turns from the
// start), where on the quad, and its alpha.
struct OutlinePoint {
    float along;
    Point at;
    float alpha;
};

// The fan's outline in the order the fill reaches it; its length. Retail
// (probed on _forever_): the start offset turns the start clockwise from the
// bottom, whatever `reverse` says; the fill covers percent x (1 - end) of a
// turn, clockwise or (reverse) counterclockwise, cut to the quad's square;
// the feather fades the leading edge over feather x that sweep, inside it.
int Outline(const State &s, OutlinePoint (&out)[kPoints]) {
    const float sweep = Clamp01(s.percent) * Clamp01(1.0f - s.end);
    if (!(sweep > 0.0f))
        return 0;
    const float dir = s.reverse ? -1.0f : 1.0f;
    const float width = Clamp01(s.feather) * sweep;
    float along[kPoints];
    int n = 0;
    along[n++] = 0.0f;
    for (float turn : kCornerTurn) {
        // A whole turn reaches every corner; the start ray closes it.
        const float f = Frac((turn - s.start) * dir);
        if (f > 0.0f && f < sweep)
            along[n++] = f;
    }
    if (width > 0.0f)
        for (int i = 0; i < kFeatherSlices; ++i)
            along[n++] = sweep - width + width * static_cast<float>(i) / kFeatherSlices;
    along[n++] = sweep;
    std::sort(along, along + n);
    int m = 0;
    for (int i = 0; i < n; ++i) {
        if (m > 0 && along[i] - out[m - 1].along < 1e-6f)
            continue;
        const float alpha = width > 0.0f ? Clamp01((sweep - along[i]) / width) : 1.0f;
        out[m++] = {along[i], Edge(s.start + dir * along[i]), alpha};
    }
    return m;
}

void Collapse(void *texture, Block &b, int from) {
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);
    for (int k = from; k < b.records; ++k)
        for (int v = 0; v < 4; ++v) {
            const size_t at = static_cast<size_t>(k) * 4 + v;
            b.positions[at] = corners[0];
            b.coords[at] = coords[0];
            b.colors[at] = 0;
        }
}

// The texture's colour at (u, v), its alpha scaled by `alpha`.
uint32_t ColorAt(const Block &b, Point p, float alpha) {
    const uint32_t c = b.colorStride != 0 ? Texture::Pieces::BlendColor(b.srcColor, p.u, p.v) : b.srcColor[0];
    const uint32_t a = static_cast<uint32_t>(static_cast<float>(c >> 24) * alpha + 0.5f);
    return (c & 0x00FFFFFF) | (std::min<uint32_t>(a, 255) << 24);
}

void Refresh(Block &b) {
    void *texture = b.texture;
    const auto it = g_states.find(texture);
    if (it == g_states.end() || !it->second.on || b.srcColor == nullptr) {
        Collapse(texture, b, 0);
        return;
    }
    OutlinePoint outline[kPoints];
    const int n = Outline(it->second, outline);
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);
    const int triangles = std::min(std::max(n - 1, 0), b.records);
    for (int k = 0; k < triangles; ++k) {
        OutlinePoint a = outline[k], c = outline[k + 1];
        // The winding of the texture's own first triangle (UL, LL, UR).
        if ((a.at.u - 0.5f) * (c.at.v - 0.5f) - (a.at.v - 0.5f) * (c.at.u - 0.5f) > 0.0f)
            std::swap(a, c);
        // The centre takes the slice's mean alpha.
        const Point pts[4] = {{0.5f, 0.5f}, a.at, c.at, c.at};
        const float alphas[4] = {(a.alpha + c.alpha) * 0.5f, a.alpha, c.alpha, c.alpha};
        for (int v = 0; v < 4; ++v) {
            const size_t at = static_cast<size_t>(k) * 4 + v;
            b.positions[at] = Texture::Pieces::Blend(corners, pts[v].u, pts[v].v);
            b.coords[at] = Texture::Pieces::Blend(coords, pts[v].u, pts[v].v);
            b.colors[at] = ColorAt(b, pts[v], alphas[v]);
        }
    }
    Collapse(texture, b, triangles);
}

void RefreshBlock(void *block) { Refresh(*static_cast<Block *>(block)); }

void OnEmitted(void *texture, void *batch, bool appended) {
    if (!appended || g_states.empty())
        return;
    const auto it = g_states.find(texture);
    if (it == g_states.end() || !it->second.on)
        return;
    State &state = it->second;
    // The arrays are one size, so a texture keeps its own for good.
    if (state.block == nullptr)
        state.block = Acquire(texture);
    // Its own colours, one per vertex, for the feather.
    const Texture::Pieces::Emitted emitted =
        Texture::Pieces::Emit(texture, batch, kQuads, ArraysOf(*state.block), true);
    state.block->records = emitted.records;
    state.block->srcColor = emitted.srcColor;
    state.block->colorStride = emitted.colorStride;
    Refresh(*state.block);
}

const Texture::Emit::AutoSubscribe _emitSub{&OnEmitted};

// Turns the mode on (every setter and the XML element do); the texture is
// emitted again as its fan.
State &TurnOn(void *texture) {
    State &state = g_states[texture];
    if (!state.on) {
        state.on = true;
        Texture::Pieces::NotifyLayer(texture);
    }
    return state;
}

// --- Lua -------------------------------------------------------------------

const State kDefaults{};

const State &Current(void *texture) {
    const auto it = g_states.find(texture);
    return it != g_states.end() ? it->second : kDefaults;
}

void SetValue(void *L, const char *usage, float State::*field) {
    void *texture = Game::Lua::ResolveTexture(L);
    float value;
    if (texture == nullptr || !Game::Lua::MethodArg::Number(L, 2, usage, value))
        return;
    TurnOn(texture).*field = Clamp01(value);
}

int GetValue(void *L, float State::*field) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(Current(texture).*field));
    return 1;
}

int __fastcall Script_SetPercent(void *L) {
    SetValue(L, "Usage: self:SetRadialProgressBarPercent(percent)", &State::percent);
    return 0;
}
int __fastcall Script_GetPercent(void *L) { return GetValue(L, &State::percent); }
int __fastcall Script_SetStartOffset(void *L) {
    SetValue(L, "Usage: self:SetRadialProgressBarStartOffset(offset)", &State::start);
    return 0;
}
int __fastcall Script_GetStartOffset(void *L) { return GetValue(L, &State::start); }
int __fastcall Script_SetEndOffset(void *L) {
    SetValue(L, "Usage: self:SetRadialProgressBarEndOffset(offset)", &State::end);
    return 0;
}
int __fastcall Script_GetEndOffset(void *L) { return GetValue(L, &State::end); }
int __fastcall Script_SetFeather(void *L) {
    SetValue(L, "Usage: self:SetRadialProgressBarFeather(feather)", &State::feather);
    return 0;
}
int __fastcall Script_GetFeather(void *L) { return GetValue(L, &State::feather); }

int __fastcall Script_SetReverse(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    bool reverse;
    if (texture == nullptr ||
        !Game::Lua::MethodArg::Bool(L, 2, "Usage: self:SetRadialProgressBarReverse(reverse)", reverse))
        return 0;
    TurnOn(texture).reverse = reverse;
    return 0;
}

int __fastcall Script_GetReverse(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    Game::Lua::PushBool(L, Current(texture).reverse);
    return 1;
}

int __fastcall Script_Clear(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    const auto it = g_states.find(texture);
    if (it == g_states.end())
        return 0;
    const bool wasOn = it->second.on;
    Block *block = it->second.block;
    it->second = State{};
    it->second.block = block;
    if (wasOn)
        Texture::Pieces::NotifyLayer(texture);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetRadialProgressBarPercent", &Script_SetPercent},
    {"GetRadialProgressBarPercent", &Script_GetPercent},
    {"SetRadialProgressBarStartOffset", &Script_SetStartOffset},
    {"GetRadialProgressBarStartOffset", &Script_GetStartOffset},
    {"SetRadialProgressBarEndOffset", &Script_SetEndOffset},
    {"GetRadialProgressBarEndOffset", &Script_GetEndOffset},
    {"SetRadialProgressBarReverse", &Script_SetReverse},
    {"GetRadialProgressBarReverse", &Script_GetReverse},
    {"SetRadialProgressBarFeather", &Script_SetFeather},
    {"GetRadialProgressBarFeather", &Script_GetFeather},
    {"ClearRadialProgressBar", &Script_Clear},
};

const Game::Doc::Field kPercentArg[] = {Game::Doc::Req("percent", "normalizedValue")};
const Game::Doc::Field kOffsetArg[] = {Game::Doc::Req("offset", "normalizedValue")};
const Game::Doc::Field kReverseArg[] = {Game::Doc::Req("reverse", "bool")};
const Game::Doc::Field kFeatherArg[] = {Game::Doc::Req("feather", "normalizedValue")};

const Game::Doc::Function kSetPercent{
    "Sets the fill percentage of the radial progress bar (0 to 1) and draws the texture as one.", kPercentArg, {}};
const Game::Doc::Function kGetPercent{"Returns the fill percentage of the radial progress bar (0 to 1).", {},
                                      kPercentArg};
const Game::Doc::Function kSetStartOffset{
    "Sets the start angle offset of the radial progress bar as a normalized value (0 to 1), where 0 is at the "
    "bottom.",
    kOffsetArg, {}};
const Game::Doc::Function kGetStartOffset{
    "Returns the start angle offset of the radial progress bar as a normalized value (0 to 1).", {}, kOffsetArg};
const Game::Doc::Function kSetEndOffset{
    "Sets the end angle offset of the radial progress bar as a normalized value (0 to 1): a full bar sweeps 1 - "
    "offset of a turn.",
    kOffsetArg, {}};
const Game::Doc::Function kGetEndOffset{
    "Returns the end angle offset of the radial progress bar as a normalized value (0 to 1).", {}, kOffsetArg};
const Game::Doc::Function kSetReverse{
    "Sets whether the radial progress bar fills in reverse (counterclockwise) direction.", kReverseArg, {}};
const Game::Doc::Function kGetReverse{
    "Returns whether the radial progress bar fills in reverse (counterclockwise) direction.", {}, kReverseArg};
const Game::Doc::Function kSetFeather{
    "Sets the feather/blur amount applied to the radial progress bar edge (higher = more gradual falloff).",
    kFeatherArg, {}};
const Game::Doc::Function kGetFeather{"Returns the feather/blur amount applied to the radial progress bar edge.",
                                      {}, kFeatherArg};
const Game::Doc::Function kClear{
    "Disable radial progress bar rendering and restore standard texture display.", {}, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetRadialProgressBarPercent", &kSetPercent},
    {"GetRadialProgressBarPercent", &kGetPercent},
    {"SetRadialProgressBarStartOffset", &kSetStartOffset},
    {"GetRadialProgressBarStartOffset", &kGetStartOffset},
    {"SetRadialProgressBarEndOffset", &kSetEndOffset},
    {"GetRadialProgressBarEndOffset", &kGetEndOffset},
    {"SetRadialProgressBarReverse", &kSetReverse},
    {"GetRadialProgressBarReverse", &kGetReverse},
    {"SetRadialProgressBarFeather", &kSetFeather},
    {"GetRadialProgressBarFeather", &kGetFeather},
    {"ClearRadialProgressBar", &kClear},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// --- XML -----------------------------------------------------------------------

// <RadialProgressBar barFillPercent startOffset endOffset reverse feather/>
// (UI.xsd's TextureField): the element turns the mode on; each attribute it
// gives sets its value.
void OnTextureNode(void *object, const void *node, void *) {
    for (const void *child = Xml::FirstChild(node); child != nullptr; child = Xml::NextSibling(child)) {
        if (!Xml::TagIs(child, "RadialProgressBar"))
            continue;
        State &state = TurnOn(object);
        float value;
        if (Xml::FloatAttr(child, "barFillPercent", value))
            state.percent = Clamp01(value);
        if (Xml::FloatAttr(child, "startOffset", value))
            state.start = Clamp01(value);
        if (Xml::FloatAttr(child, "endOffset", value))
            state.end = Clamp01(value);
        if (Xml::FloatAttr(child, "feather", value))
            state.feather = Clamp01(value);
        if (const char *reverse = Xml::Attr(child, "reverse"); reverse != nullptr && *reverse != '\0')
            state.reverse = Game::StringToFlag(reverse);
    }
}

const Xml::TypeLoad::AutoSubscribe _xmlSub{Xml::TypeLoad::TEXTURE, &OnTextureNode};

// --- lifetime ---------------------------------------------------------------

// The pool can hand a destroyed region's address to a new one.
void OnRegionDestroyed(void *region) {
    const auto it = g_states.find(region);
    if (it == g_states.end())
        return;
    Release(it->second.block);
    g_states.erase(it);
}

const Frame::RegionDestruction::AutoSubscribe _destructionSub{&OnRegionDestroyed};

void PrepareForReload() {
    for (auto &entry : g_states)
        Release(entry.second.block);
    g_states.clear();
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};
const Game::GlueBootAutoRegister _glueBootReg{&PrepareForReload};

} // namespace

bool Active(void *texture) {
    if (g_states.empty())
        return false;
    const auto it = g_states.find(texture);
    return it != g_states.end() && it->second.on;
}

void SetPercent(void *texture, float percent) { g_states[texture].percent = Clamp01(percent); }

} // namespace Texture::Radial
