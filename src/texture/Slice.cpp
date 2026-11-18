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

// Texture slicing: `texture:SetTextureSliceMargins` / `GetTextureSliceMargins`
// / `SetTextureSliceMode` / `GetTextureSliceMode` / `ClearTextureSlice`,
// `Enum.UITextureSliceMode`, and the slice data SetAtlas applies.
//
// RETAIL CONTRACT (_forever_ SimpleTextureBaseAPIDocumentation): margins are
// in the image's pixels. The image draws in nine pieces: the corners keep
// their size, the edges stretch along one axis, the centre stretches both
// ways. SetAtlas loads the atlas's slice data (UiTextureAtlasElementSliceData;
// AtlasData.h carries the rows); ClearTextureSlice turns slicing off.
//
// What the rest of the state does, from retail's own Lua:
//   - SetTexture keeps the margins. The nameplate's classic health bar
//     (Blizzard_NamePlateUnitFrame.lua) sets a plain file on a texture the
//     other branch gives a sliced atlas, then SetTextureSliceMargins(0, 0, 0,
//     0) to undo the atlas's slicing, and the aura tooltip
//     (Blizzard_AuraContainerUtil.lua) clears the slice after SetTexture(nil).
//     Neither would be needed if a file cleared it.
//   - SetAtlas of an atlas with no slice data clears the margins: our choice.
//     Retail says only that SetAtlas loads the atlas's data; the aura tooltip
//     clears the slice before SetAtlas either way.
//   - Margins of 0 draw the texture whole. GetTextureSliceMargins and
//     GetTextureSliceMode return nothing ("MayReturnNothing") until margins
//     or a mode are set.
//
// THE PIECES. One image pixel is one unit of the texture's own UI units, so a
// corner's size on screen is its margin converted with the texture's
// effective scale (Frame::Layout::FromLocalUnits). The size the corners are
// measured against is the quad the engine's corner store writes: the layout
// rect, cropped when SetTexCoordModifiesRect is on (CornerStore::CropInput).
// When the margins of an axis do not fit it, both shrink in proportion.
// The pieces' texture coordinates interpolate inside the texture's current
// ones (+0x104, for an atlas the sprite's rect in its sheet), at the margins
// over the image's pixel size: the sprite's for an atlas (Atlas::Info width /
// height), the device texture's for a file (OFF_GXTEX_WIDTH / HEIGHT, the size
// Texture::Tile counts against). With coordinates that crop the image, each
// margin shows only the part of it inside the crop, at the same pixel scale:
// our choice, which keeps the simple case exact and lets a filling status bar
// (StatusBar::Fill crops the coordinates) show its start cap whole and its
// end cap only once the fill reaches it. Coordinates turned a quarter (an
// 8-argument SetTexCoord that maps x to y, SetRotatesTexture) have no width
// along an axis, and draw unsliced.
//
// THE MECHANISM. A texture emits one quad record into its layer batch
// (FUN_SIMPLETEXTURE_APPEND_QUAD), and the batch keeps pointers to the
// texture's four positions (+0xD4) and eight coordinates (+0x104), read on
// every draw. A sliced texture's emit (Texture::Emit) runs the engine's own
// append eight more times, so every piece carries the engine's handle, blend,
// colour, stride and shader, then points the nine records at position and
// coordinate arrays kept here. The layer is marked changed
// (FUN_REGION_NOTIFY_LAYER_CHANGED, what SetTexture calls) whenever a texture
// starts or stops emitting nine records.
//
// The pieces are worked out at each draw of a batch (FUN_FRAME_LAYER_RENDER),
// for every record that points at a texture's first piece, from what is in the
// texture then: the four drawn corners (+0xD4) and the coordinates (+0x104).
// Many writers change those without a layout: the corner store,
// Texture::Transform (rotation, vertex offsets), Animation::Region
// (translation, scale, rotation), Texture::Tile's counts, StatusBar::Fill,
// SetTexCoord on an uncropped texture (which writes no quad). Reading them at
// the draw follows every one of them with no hook of its own. The positions
// are a bilinear blend of the four drawn corners, so a rotated, warped or
// animated quad carries its pieces along; an animated scale scales the
// corners with it (our choice: the animation transforms what is drawn).
//
// Known differences:
//   - Tiled mode (Enum.UITextureSliceMode.Tiled) draws stretched. 1.12 tiles
//     only by the device's wrap, which repeats a whole file, never a sprite
//     inside a sheet; the one tiled atlas here (ui-hud-cooldownmanager-bar-bg,
//     element 30191) has a centre and edges one pixel thick, which look the
//     same tiled or stretched.
//   - A gradient (SetGradient: one colour per vertex, stride 4) applies to
//     each piece rather than once across the texture.
//   - A texture with SetHorizTile / SetVertTile on draws unsliced: tiling
//     wins (our choice). Its pieces collapse onto the whole quad and keep
//     Tile's counts.
//   - Texture::Mask finds the texture of a piece through Owner().
//
// The arrays are kept per texture for its life, and go back to a free list
// (never freed) when it is destroyed, so a batch that still points at them
// until its next rebuild reads valid memory.

#include "texture/Slice.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/Layout.h"
#include "frame/RegionDestruction.h"
#include "frame/RenderBatch.h"
#include "texture/Atlas.h"
#include "texture/CornerStore.h"
#include "texture/Emit.h"
#include "texture/Wrap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace Texture::Slice {

namespace {

using Frame::RenderBatch::C2Vector;
using Frame::RenderBatch::C3Vector;
using Frame::RenderBatch::QuadRecord;

constexpr int kPieces = 9;

enum SliceMode : int {
    kStretched = 0,
    kTiled = 1,
};

const Game::Lua::EnumIntegerEntry kSliceModeEntries[] = {
    {"Stretched", kStretched},
    {"Tiled", kTiled},
};

// The nine pieces of one texture, row by row from its first corner (UL), each
// in the engine's corner order UL, LL, UR, LR.
struct Pieces {
    C3Vector positions[kPieces][4];
    C2Vector coords[kPieces][4];
    void *texture = nullptr;
    int records = 0; // the batch records the last emit pointed here
};

enum Margin : int { kLeft = 0, kTop = 1, kRight = 2, kBottom = 3 };

struct State {
    bool hasMargins = false;
    bool hasMode = false;
    float margins[4] = {}; // image pixels
    int mode = kStretched;
    Pieces *pieces = nullptr;
};

std::unordered_map<void *, State> g_states;
// Every texture's pieces by the address of their positions, for the draw scan
// and Owner().
std::map<uintptr_t, Pieces *> g_blocks;
std::vector<Pieces *> g_free;

using Append_t = void(__thiscall *)(void *batch, void *texture);
using Notify_t = void(__thiscall *)(void *region);
using DrawBatch_t = void(__fastcall *)(void *batch);

bool Emits(const State &s) {
    return s.hasMargins &&
           (s.margins[kLeft] > 0.0f || s.margins[kTop] > 0.0f || s.margins[kRight] > 0.0f ||
            s.margins[kBottom] > 0.0f);
}

void NotifyLayer(void *texture) {
    reinterpret_cast<Notify_t>(Offsets::FUN_REGION_NOTIFY_LAYER_CHANGED)(texture);
}

Pieces *Acquire(void *texture) {
    Pieces *p;
    if (!g_free.empty()) {
        p = g_free.back();
        g_free.pop_back();
    } else {
        p = new Pieces();
    }
    p->texture = texture;
    p->records = 0;
    g_blocks[reinterpret_cast<uintptr_t>(&p->positions[0][0])] = p;
    return p;
}

void Release(Pieces *p) {
    if (p == nullptr)
        return;
    g_blocks.erase(reinterpret_cast<uintptr_t>(&p->positions[0][0]));
    p->texture = nullptr;
    g_free.push_back(p);
}

// --- the pieces -------------------------------------------------------------

// The point at (u, v) of a quad given in corner order UL, LL, UR, LR, u along
// UL->UR and v along UL->LL.
C3Vector Blend(const C3Vector *c, float u, float v) {
    const float a = (1.0f - u) * (1.0f - v), b = (1.0f - u) * v, d = u * (1.0f - v), e = u * v;
    return {a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x,
            a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y,
            a * c[0].z + b * c[1].z + d * c[2].z + e * c[3].z};
}

C2Vector Blend(const C2Vector *c, float u, float v) {
    const float a = (1.0f - u) * (1.0f - v), b = (1.0f - u) * v, d = u * (1.0f - v), e = u * v;
    return {a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x,
            a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y};
}

// The image's pixel size: the sprite's for an atlas, the device texture's for
// a file.
bool ImageSize(void *texture, float &width, float &height) {
    if (const Atlas::Info *info = Atlas::Current(texture)) {
        width = info->width;
        height = info->height;
        return width > 0.0f && height > 0.0f;
    }
    void *image = Game::Read<void *>(texture, Offsets::OFF_SIMPLETEXTURE_HTEXTURE);
    const void *device = image != nullptr ? Game::Read<void *>(image, Offsets::OFF_HTEXTURE_DEVICE) : nullptr;
    if (device == nullptr)
        return false;
    width = static_cast<float>(Game::Read<uint32_t>(device, Offsets::OFF_GXTEX_WIDTH));
    height = static_cast<float>(Game::Read<uint32_t>(device, Offsets::OFF_GXTEX_HEIGHT));
    return width > 0.0f && height > 0.0f;
}

// One axis. `a` and `b` are the image coordinates (0..1 across the image) at
// the quad's first and last edge, `lowMargin` / `highMargin` the margins at
// image coordinate 0 and 1 in pixels, `length` the quad's length. Writes the
// two cuts as fractions of the quad (`s`) and of the coordinates (`p`), from
// the first edge.
bool Axis(void *texture, float a, float b, float lowMargin, float highMargin, float imagePixels,
          float length, float (&s)[2], float (&p)[2]) {
    const float lo = std::min(a, b), hi = std::max(a, b);
    if (!(hi - lo > 1e-6f) || !(imagePixels > 0.0f) || !(length > 0.0f))
        return false;
    // The margins as image coordinates. Margins wider than the image meet at
    // their proportional split.
    float cutLow = lowMargin / imagePixels;
    float cutHigh = 1.0f - highMargin / imagePixels;
    if (cutLow > cutHigh)
        cutLow = cutHigh = lowMargin / (lowMargin + highMargin);
    // The part of each margin inside the coordinates; they cannot overlap.
    const float shownLow = std::max(0.0f, std::min(hi, cutLow) - lo);
    const float shownHigh = std::max(0.0f, hi - std::max(lo, cutHigh));
    // On screen one image pixel is one unit of the texture's own scale; when
    // the two do not fit the quad, both shrink in proportion.
    float screenLow = Frame::Layout::FromLocalUnits(texture, shownLow * imagePixels);
    float screenHigh = Frame::Layout::FromLocalUnits(texture, shownHigh * imagePixels);
    if (screenLow + screenHigh > length) {
        const float k = length / (screenLow + screenHigh);
        screenLow *= k;
        screenHigh *= k;
    }
    // The first edge shows `a`: the image's low side unless it is mirrored.
    const bool mirrored = b < a;
    const float shownFirst = mirrored ? shownHigh : shownLow;
    const float shownLast = mirrored ? shownLow : shownHigh;
    const float screenFirst = mirrored ? screenHigh : screenLow;
    const float screenLast = mirrored ? screenLow : screenHigh;
    p[0] = shownFirst / (hi - lo);
    p[1] = 1.0f - shownLast / (hi - lo);
    s[0] = screenFirst / length;
    s[1] = 1.0f - screenLast / length;
    return true;
}

bool Compute(void *texture, const State &state, Pieces &out) {
    float imageW, imageH;
    if (!ImageSize(texture, imageW, imageH))
        return false;

    // The quad the corner store writes, before any transform moves it.
    float rect[4]; // {y, left, y, right}
    for (int i = 0; i < 4; ++i)
        rect[i] = Game::Ptr<float>(texture, Offsets::OFF_REGION_RECT)[i];
    Texture::CornerStore::CropInput(texture, rect);
    const float width = std::fabs(rect[3] - rect[1]);
    const float height = std::fabs(rect[2] - rect[0]);

    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);

    // The coordinates in the image's own space.
    float image[8];
    for (int i = 0; i < 4; ++i) {
        image[i * 2] = coords[i].x;
        image[i * 2 + 1] = coords[i].y;
    }
    Atlas::ToSprite(texture, image);

    float sx[2], px[2], sy[2], py[2];
    if (!Axis(texture, image[0], image[4], state.margins[kLeft], state.margins[kRight], imageW, width, sx,
              px) ||
        !Axis(texture, image[1], image[3], state.margins[kTop], state.margins[kBottom], imageH, height,
              sy, py))
        return false;

    const float s[4] = {0.0f, sx[0], sx[1], 1.0f};
    const float t[4] = {0.0f, sy[0], sy[1], 1.0f};
    const float p[4] = {0.0f, px[0], px[1], 1.0f};
    const float q[4] = {0.0f, py[0], py[1], 1.0f};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int k = row * 3 + col;
            // UL, LL, UR, LR of the piece.
            const int cs[4] = {col, col, col + 1, col + 1};
            const int rs[4] = {row, row + 1, row, row + 1};
            for (int v = 0; v < 4; ++v) {
                out.positions[k][v] = Blend(corners, s[cs[v]], t[rs[v]]);
                out.coords[k][v] = Blend(coords, p[cs[v]], q[rs[v]]);
            }
        }
    }
    return true;
}

// The texture drawn whole: the first piece is its quad, the others collapse
// to a point and draw nothing.
void Whole(void *texture, Pieces &out) {
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);
    for (int k = 0; k < kPieces; ++k) {
        for (int v = 0; v < 4; ++v) {
            out.positions[k][v] = k == 0 ? corners[v] : corners[0];
            out.coords[k][v] = coords[v];
        }
    }
}

void Refresh(Pieces &p) {
    void *texture = p.texture;
    const auto it = g_states.find(texture);
    if (p.records == kPieces && it != g_states.end() && Emits(it->second) && !Texture::Wrap::Tiles(texture) &&
        Compute(texture, it->second, p))
        return;
    Whole(texture, p);
}

// --- the emit and the draw -------------------------------------------------

void OnEmitted(void *texture, void *batch, bool appended) {
    if (!appended || g_states.empty())
        return;
    const auto it = g_states.find(texture);
    if (it == g_states.end() || !Emits(it->second))
        return;
    State &state = it->second;
    if (state.pieces == nullptr)
        state.pieces = Acquire(texture);
    Pieces &pieces = *state.pieces;

    const uint32_t first = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT) - 1;
    for (int i = 1; i < kPieces; ++i)
        reinterpret_cast<Append_t>(Offsets::FUN_SIMPLETEXTURE_APPEND_QUAD)(batch, texture);
    // Read after the appends: a reserve can move the records.
    const uint32_t count = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT);
    auto *records = Game::Read<QuadRecord *>(batch, Offsets::OFF_RENDERBATCH_QUAD_RECORDS);
    pieces.records = static_cast<int>(std::min<uint32_t>(count - first, kPieces));
    for (int k = 0; k < pieces.records; ++k) {
        records[first + k].positions = pieces.positions[k];
        records[first + k].texCoords = pieces.coords[k];
    }
    Refresh(pieces);
}

const Texture::Emit::AutoSubscribe _emitSub{&OnEmitted};

DrawBatch_t g_origDrawBatch = nullptr;

void __fastcall DrawBatch_h(void *batch) {
    if (!g_blocks.empty()) {
        const uint32_t count = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT);
        const auto *records = Game::Read<const QuadRecord *>(batch, Offsets::OFF_RENDERBATCH_QUAD_RECORDS);
        for (uint32_t i = 0; i < count; ++i) {
            const auto it = g_blocks.find(reinterpret_cast<uintptr_t>(records[i].positions));
            if (it != g_blocks.end())
                Refresh(*it->second);
        }
    }
    g_origDrawBatch(batch);
}

const Game::HookAutoRegister _drawHook{Offsets::FUN_FRAME_LAYER_RENDER, reinterpret_cast<void *>(&DrawBatch_h),
                                       reinterpret_cast<void **>(&g_origDrawBatch)};

// --- state ------------------------------------------------------------------

// Applies a change and marks the layer when the texture starts or stops
// emitting its pieces.
template <class Change> void Update(void *texture, Change change) {
    State &state = g_states[texture];
    const bool before = Emits(state);
    change(state);
    if (Emits(state) != before)
        NotifyLayer(texture);
}

void Clear(State &state) {
    state.hasMargins = false;
    state.hasMode = false;
    for (float &m : state.margins)
        m = 0.0f;
    state.mode = kStretched;
}

// --- Lua -------------------------------------------------------------------

int __fastcall Script_SetTextureSliceMargins(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    float margins[4];
    for (int i = 0; i < 4; ++i) {
        if (!Game::Lua::IsNumber(L, 2 + i)) {
            Game::Lua::Error(L, "Usage: texture:SetTextureSliceMargins(left, top, right, bottom)");
            return 0;
        }
        // A negative margin has no piece to draw: our choice is to read it as 0.
        margins[i] = std::max(0.0f, static_cast<float>(Game::Lua::ToNumber(L, 2 + i)));
    }
    Update(texture, [&](State &s) {
        s.hasMargins = true;
        for (int i = 0; i < 4; ++i)
            s.margins[i] = margins[i];
    });
    return 0;
}

int __fastcall Script_GetTextureSliceMargins(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    const auto it = g_states.find(texture);
    if (it == g_states.end() || !it->second.hasMargins)
        return 0;
    for (float m : it->second.margins)
        Game::Lua::PushNumber(L, m);
    return 4;
}

int __fastcall Script_SetTextureSliceMode(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    const double mode = Game::Lua::IsNumber(L, 2) ? Game::Lua::ToNumber(L, 2) : -1.0;
    if (mode != kStretched && mode != kTiled) {
        Game::Lua::Error(L, "Usage: texture:SetTextureSliceMode(sliceMode)");
        return 0;
    }
    Update(texture, [&](State &s) {
        s.hasMode = true;
        s.mode = static_cast<int>(mode);
    });
    return 0;
}

int __fastcall Script_GetTextureSliceMode(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    const auto it = g_states.find(texture);
    if (it == g_states.end() || !(it->second.hasMargins || it->second.hasMode))
        return 0;
    Game::Lua::PushNumber(L, it->second.mode);
    return 1;
}

int __fastcall Script_ClearTextureSlice(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    if (g_states.find(texture) != g_states.end())
        Update(texture, Clear);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetTextureSliceMargins", &Script_SetTextureSliceMargins},
    {"GetTextureSliceMargins", &Script_GetTextureSliceMargins},
    {"SetTextureSliceMode", &Script_SetTextureSliceMode},
    {"GetTextureSliceMode", &Script_GetTextureSliceMode},
    {"ClearTextureSlice", &Script_ClearTextureSlice},
};

const Game::Doc::Field kMarginArgs[] = {
    Game::Doc::Req("left", "number", "In the image's pixels."),
    Game::Doc::Req("top", "number"),
    Game::Doc::Req("right", "number"),
    Game::Doc::Req("bottom", "number"),
};
const Game::Doc::Function kSetTextureSliceMargins{
    "Draws the texture as a nine-slice: corners that keep their size, edges and a centre that stretch.",
    kMarginArgs, {}};
const Game::Doc::Function kGetTextureSliceMargins{
    "Returns the texture's slice margins; nothing when none are set.", {}, kMarginArgs};

const Game::Doc::Field kModeArgs[] = {
    Game::Doc::Req("sliceMode", "UITextureSliceMode"),
};
const Game::Doc::Function kSetTextureSliceMode{
    "Sets whether the centre and edges of a sliced texture stretch or tile; tiled draws stretched.",
    kModeArgs, {}};
const Game::Doc::Function kGetTextureSliceMode{
    "Returns the texture's slice mode; nothing when no slice is set.", {}, kModeArgs};
const Game::Doc::Function kClearTextureSlice{
    "Turns slicing off, including the slicing SetAtlas loads from the atlas.", {}, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetTextureSliceMargins", &kSetTextureSliceMargins},
    {"GetTextureSliceMargins", &kGetTextureSliceMargins},
    {"SetTextureSliceMode", &kSetTextureSliceMode},
    {"GetTextureSliceMode", &kGetTextureSliceMode},
    {"ClearTextureSlice", &kClearTextureSlice},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
    Game::Lua::RegisterIntegerEnum("Enum", "UITextureSliceMode", kSliceModeEntries, "UITextureConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// --- lifetime ---------------------------------------------------------------

// The pool can hand a destroyed region's address to a new one.
void OnRegionDestroyed(void *region) {
    const auto it = g_states.find(region);
    if (it == g_states.end())
        return;
    Release(it->second.pieces);
    g_states.erase(it);
}

const Frame::RegionDestruction::AutoSubscribe _destructionSub{&OnRegionDestroyed};

void PrepareForReload() {
    for (auto &entry : g_states)
        Release(entry.second.pieces);
    g_states.clear();
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};
const Game::GlueBootAutoRegister _glueBootReg{&PrepareForReload};

} // namespace

void ApplyAtlas(void *texture, const Atlas::Info &info) {
    if (!info.sliced) {
        if (g_states.find(texture) != g_states.end())
            Update(texture, Clear);
        return;
    }
    Update(texture, [&](State &s) {
        s.hasMargins = true;
        s.hasMode = true;
        s.margins[kLeft] = info.sliceLeft;
        s.margins[kTop] = info.sliceTop;
        s.margins[kRight] = info.sliceRight;
        s.margins[kBottom] = info.sliceBottom;
        s.mode = info.sliceMode;
    });
}

void *Owner(const void *positions) {
    if (g_blocks.empty())
        return nullptr;
    auto it = g_blocks.upper_bound(reinterpret_cast<uintptr_t>(positions));
    if (it == g_blocks.begin())
        return nullptr;
    --it;
    if (reinterpret_cast<uintptr_t>(positions) >= it->first + sizeof(Pieces::positions))
        return nullptr;
    return it->second->texture;
}

} // namespace Texture::Slice
