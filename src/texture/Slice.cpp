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
// When the margins of an axis do not fit it, both drawn sizes shrink in
// proportion while each keeps its whole margin of the image (see Axis).
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
// TILED MODE (Enum.UITextureSliceMode.Tiled). The edges and the centre repeat
// at the image's pixel scale, the scale the corners use, along the axes they
// would stretch: the top and bottom edges across, the left and right edges
// down, the centre both ways. Each repeat shows the part of the image between
// the margins (inside the coordinates' crop, as above). The repeats start at
// the quad's first edge, after the first margin, and the last one on each axis
// is cropped, its quad and its coordinates alike, so no repeat is squashed:
// our choice of where the tiling starts. 1.12's device wrap repeats a whole
// file, never a sprite inside a sheet, so each repeat is its own quad.
//
// So a sliced texture is a grid of (2 + n) x (2 + m) pieces, n and m the
// repeats per axis (1 and 1, nine pieces, when stretched). The grid is laid
// out from the layout rect, the margins, the image size, the coordinates and
// the mode. Past kMaxQuads pieces the texture draws stretched.
//
// THE MECHANISM. A texture emits one quad record into its layer batch
// (FUN_SIMPLETEXTURE_APPEND_QUAD), and the batch keeps pointers to the
// texture's four positions (+0xD4) and eight coordinates (+0x104), read on
// every draw. A sliced texture's emit (Texture::Emit) lays out its grid and
// runs the engine's own append once more per extra piece, so every piece
// carries the engine's handle, blend, colour, stride and shader, then points
// the records at position and coordinate arrays kept here.
//
// The batch is rebuilt only when its layer is marked changed
// (FUN_REGION_NOTIFY_LAYER_CHANGED, what SetTexture calls; only while the
// texture is registered, which showing it does with a rebuild of its own).
// It is marked here when a texture starts or stops being sliced, and when the
// grid it would lay out now differs from the one it emitted: after every quad
// write (Texture::CornerStore: a layout, so every resize and scale change)
// and every change of the margins or the mode. A change the draw finds first
// (the coordinates, written without a layout) is marked at the tail of the
// frame (Tick::FrameTick), outside the strata walk, which may clear the
// marks it is walking; until the rebuild that frame draws the grid that fits
// the records, or stretched when the new one does not.
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
// animated quad carries its pieces, repeats included, along; an animated
// scale scales the corners with it (our choice: the animation transforms
// what is drawn).
//
// Known differences:
//   - A gradient (SetGradient: one colour per vertex, stride 4) applies to
//     each piece rather than once across the texture.
//   - A texture with SetHorizTile / SetVertTile on draws unsliced: tiling
//     wins (our choice). Its pieces collapse onto the whole quad and keep
//     Tile's counts.
//   - Texture::Mask finds the texture of a piece through Owner().
//
// The arrays are kept per texture and never reallocated. A texture whose grid
// outgrows its arrays, and a destroyed texture, hand them to a free list
// (never freed), so a batch that still points at them until its next rebuild
// reads valid memory.

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
#include "tick/FrameTick.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Texture::Slice {

namespace {

using Frame::RenderBatch::C2Vector;
using Frame::RenderBatch::C3Vector;
using Frame::RenderBatch::QuadRecord;

// The stretched grid: three pieces per axis.
constexpr int kStretchedAxis = 3;

// The most pieces a texture lays out. 1.12's batch draw makes one draw call
// per record (FUN_FRAME_LAYER_RENDER: a vertex build and a draw per quad), so
// thousands of repeats cost thousands of draw calls every frame. A tiled
// texture past this many pieces (a one-pixel centre across a whole screen,
// say) draws stretched, which for so fine a repeat looks the same.
constexpr int kMaxQuads = 4096;

enum SliceMode : int {
    kStretched = 0,
    kTiled = 1,
};

const Game::Lua::EnumIntegerEntry kSliceModeEntries[] = {
    {"Stretched", kStretched},
    {"Tiled", kTiled},
};

// One texture's pieces: a grid of `cols` x `rows`, row by row from its first
// corner (UL), each piece four positions and four coordinates in the engine's
// corner order UL, LL, UR, LR. The arrays are sized once, for `capacity`
// pieces, and never reallocated: batch records point into them.
struct Pieces {
    std::vector<C3Vector> positions;
    std::vector<C2Vector> coords;
    int capacity = 0;
    void *texture = nullptr;
    int cols = 0;    // the grid the last emit laid out
    int rows = 0;
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
// Textures whose grid the draw found changed, marked at the frame's tail.
std::unordered_set<void *> g_pending;

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

// Arrays for at least `pieces` pieces: the smallest free block that holds
// them, else a new one.
Pieces *Acquire(void *texture, int pieces) {
    auto best = g_free.end();
    for (auto it = g_free.begin(); it != g_free.end(); ++it)
        if ((*it)->capacity >= pieces && (best == g_free.end() || (*it)->capacity < (*best)->capacity))
            best = it;
    Pieces *p;
    if (best != g_free.end()) {
        p = *best;
        g_free.erase(best);
    } else {
        p = new Pieces();
        p->capacity = std::max(pieces, kStretchedAxis * kStretchedAxis);
        p->positions.resize(static_cast<size_t>(p->capacity) * 4);
        p->coords.resize(static_cast<size_t>(p->capacity) * 4);
    }
    p->texture = texture;
    p->cols = p->rows = p->records = 0;
    g_blocks[reinterpret_cast<uintptr_t>(p->positions.data())] = p;
    return p;
}

void Release(Pieces *p) {
    if (p == nullptr)
        return;
    g_blocks.erase(reinterpret_cast<uintptr_t>(p->positions.data()));
    p->texture = nullptr;
    g_free.push_back(p);
}

// --- the grid ---------------------------------------------------------------

// One run of pieces along an axis: from `s0` to `s1` as fractions of the
// quad, showing `p0` to `p1` as fractions of the coordinates, both from the
// quad's first edge.
struct Segment {
    float s0, s1, p0, p1;
};

// One axis's cuts: the margins end at `s1` / `p1` and start again at `s2` /
// `p2`; `tile` is one repeat of the middle as a fraction of the quad, and
// `repeats` how many fill the middle (not yet capped).
struct Cuts {
    float s1, s2, p1, p2;
    float tile;
    double repeats;
};

// The axis's segments laid out in. Reused, so a draw allocates nothing once
// they have grown.
std::vector<Segment> g_cols;
std::vector<Segment> g_rows;

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
// image coordinate 0 and 1 in pixels, `length` the quad's length.
bool Axis(void *texture, float a, float b, float lowMargin, float highMargin, float imagePixels,
          float length, Cuts &out) {
    const float lo = std::min(a, b), hi = std::max(a, b);
    if (!(hi - lo > 1e-6f) || !(imagePixels > 0.0f) || !(length > 0.0f))
        return false;
    // The margins as image coordinates, each its own margin over the image
    // size, clamped to the image. Margins wider than the image overlap in it,
    // and each side keeps its whole margin: retail's observed behaviour. Its
    // ui-damagemeters-header-bar (margins T14 + B22 on a 28-pixel image, drawn
    // 32 tall) shows the top piece from image rows 0..14 and the bottom from
    // rows 6..28, so the overlapping rows draw twice: its middle column's
    // gradient restarts mid-height (... 7 8 10 | 6 7 8 10 12 14 17 ...). A
    // per-side render matches that screenshot with a mean absolute difference
    // of 12.3, a split at the proportional point with 16.0.
    const float cutLow = std::min(1.0f, lowMargin / imagePixels);
    const float cutHigh = std::max(0.0f, 1.0f - highMargin / imagePixels);
    // The part of each margin inside the coordinates. These overlap when the
    // margins do; the middle then runs between their inner edges reversed,
    // as given (retail's observed behaviour, as above).
    const float shownLow = std::max(0.0f, std::min(hi, cutLow) - lo);
    const float shownHigh = std::max(0.0f, hi - std::max(lo, cutHigh));
    const float shownMiddle = std::fabs((hi - lo) - shownLow - shownHigh);
    // On screen one image pixel is one unit of the texture's own scale; when
    // the two do not fit the quad, both DRAWN sizes shrink in proportion,
    // while their coordinates keep the whole margin (retail, as above).
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
    out.p1 = shownFirst / (hi - lo);
    out.p2 = 1.0f - shownLast / (hi - lo);
    out.s1 = screenFirst / length;
    out.s2 = 1.0f - screenLast / length;
    // One repeat of the middle at the same pixel scale. The small allowance
    // keeps a middle that is a whole number of repeats from gaining a sliver.
    out.tile = Frame::Layout::FromLocalUnits(texture, shownMiddle * imagePixels) / length;
    const float middle = out.s2 - out.s1;
    // A repeat at most one image pixel long is one strip of texels: repeating
    // it draws the same image as stretching it, so one stretched piece stands
    // in for the repeats (ui-hud-cooldownmanager-bar-bg's centre and edges
    // are one pixel thick, about 70 quads across a bar otherwise).
    const bool oneTexel = shownMiddle * imagePixels <= 1.0f + 1e-4f;
    out.repeats = !oneTexel && out.tile > 0.0f && middle > 0.0f
                      ? std::max(1.0, std::ceil(static_cast<double>(middle) / out.tile - 1e-4))
                      : 1.0;
    return true;
}

// The segments of an axis cut into `repeats` repeats of its middle. The last
// repeat is cropped: it ends at the far margin and shows that share of a
// repeat's coordinates.
void Lay(const Cuts &cuts, int repeats, std::vector<Segment> &out) {
    out.clear();
    out.push_back({0.0f, cuts.s1, 0.0f, cuts.p1});
    if (repeats <= 1) {
        out.push_back({cuts.s1, cuts.s2, cuts.p1, cuts.p2});
    } else {
        for (int k = 0; k < repeats; ++k) {
            const float s0 = cuts.s1 + static_cast<float>(k) * cuts.tile;
            const float s1 = k == repeats - 1 ? cuts.s2 : s0 + cuts.tile;
            const float share = std::min(1.0f, (s1 - s0) / cuts.tile);
            out.push_back({s0, s1, cuts.p1, cuts.p1 + (cuts.p2 - cuts.p1) * share});
        }
    }
    out.push_back({cuts.s2, 1.0f, cuts.p2, 1.0f});
}

// Lays out the texture's grid into g_cols / g_rows, stretched when `stretched`
// or the state asks; false when it cannot be sliced (no image size, no rect,
// coordinates with no width along an axis).
bool Plan(void *texture, const State &state, bool stretched) {
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

    // The coordinates in the image's own space.
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    float image[8];
    for (int i = 0; i < 4; ++i) {
        image[i * 2] = coords[i].x;
        image[i * 2 + 1] = coords[i].y;
    }
    Atlas::ToSprite(texture, image);

    Cuts x, y;
    if (!Axis(texture, image[0], image[4], state.margins[kLeft], state.margins[kRight], imageW, width, x) ||
        !Axis(texture, image[1], image[3], state.margins[kTop], state.margins[kBottom], imageH, height, y))
        return false;

    int across = 1, down = 1;
    if (!stretched && state.mode == kTiled && (x.repeats + 2.0) * (y.repeats + 2.0) <= kMaxQuads) {
        across = static_cast<int>(x.repeats);
        down = static_cast<int>(y.repeats);
    }
    Lay(x, across, g_cols);
    Lay(y, down, g_rows);
    return true;
}

// The grid the texture would emit now: its planned one, or the stretched
// three by three when it cannot be sliced or tiles (Texture::Tile).
void Grid(void *texture, const State &state, int &cols, int &rows) {
    cols = rows = kStretchedAxis;
    if (!Texture::Wrap::Tiles(texture) && Plan(texture, state, false)) {
        cols = static_cast<int>(g_cols.size());
        rows = static_cast<int>(g_rows.size());
    }
}

// Writes the laid-out grid into the emitted one; pieces past it collapse to a
// point and draw nothing.
void Fill(void *texture, Pieces &out) {
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);
    const int cols = static_cast<int>(g_cols.size());
    const int rows = static_cast<int>(g_rows.size());
    for (int r = 0; r < out.rows; ++r) {
        for (int c = 0; c < out.cols; ++c) {
            C3Vector *pos = &out.positions[static_cast<size_t>(r * out.cols + c) * 4];
            C2Vector *tex = &out.coords[static_cast<size_t>(r * out.cols + c) * 4];
            if (r >= rows || c >= cols) {
                for (int v = 0; v < 4; ++v) {
                    pos[v] = corners[0];
                    tex[v] = coords[0];
                }
                continue;
            }
            const Segment &sx = g_cols[c], &sy = g_rows[r];
            // UL, LL, UR, LR of the piece.
            const float xs[4] = {sx.s0, sx.s0, sx.s1, sx.s1}, ys[4] = {sy.s0, sy.s1, sy.s0, sy.s1};
            const float xp[4] = {sx.p0, sx.p0, sx.p1, sx.p1}, yp[4] = {sy.p0, sy.p1, sy.p0, sy.p1};
            for (int v = 0; v < 4; ++v) {
                pos[v] = Blend(corners, xs[v], ys[v]);
                tex[v] = Blend(coords, xp[v], yp[v]);
            }
        }
    }
}

// The texture drawn whole: the first piece is its quad, the others collapse
// to a point and draw nothing.
void Whole(void *texture, Pieces &out) {
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);
    for (int k = 0; k < out.records; ++k) {
        for (int v = 0; v < 4; ++v) {
            out.positions[static_cast<size_t>(k) * 4 + v] = k == 0 ? corners[v] : corners[0];
            out.coords[static_cast<size_t>(k) * 4 + v] = coords[v];
        }
    }
}

void Refresh(Pieces &p) {
    void *texture = p.texture;
    const auto it = g_states.find(texture);
    if (p.records != p.cols * p.rows || it == g_states.end() || !Emits(it->second) ||
        Texture::Wrap::Tiles(texture) || !Plan(texture, it->second, false)) {
        Whole(texture, p);
        return;
    }
    const int cols = static_cast<int>(g_cols.size());
    const int rows = static_cast<int>(g_rows.size());
    if (cols != p.cols || rows != p.rows) {
        // The records hold the grid of the last emit: rebuild at the frame's
        // tail, and draw this frame what fits them.
        g_pending.insert(texture);
        if ((cols > p.cols || rows > p.rows) && !Plan(texture, it->second, true)) {
            Whole(texture, p);
            return;
        }
    }
    Fill(texture, p);
}

// Marks the layer when the grid the texture would emit now is not the one in
// its batch.
void Resync(void *texture) {
    if (g_states.empty())
        return;
    const auto it = g_states.find(texture);
    if (it == g_states.end() || it->second.pieces == nullptr || !Emits(it->second))
        return;
    int cols, rows;
    Grid(texture, it->second, cols, rows);
    if (cols != it->second.pieces->cols || rows != it->second.pieces->rows)
        NotifyLayer(texture);
}

// --- the emit and the draw -------------------------------------------------

void OnEmitted(void *texture, void *batch, bool appended) {
    if (!appended || g_states.empty())
        return;
    const auto it = g_states.find(texture);
    if (it == g_states.end() || !Emits(it->second))
        return;
    State &state = it->second;
    int cols, rows;
    Grid(texture, state, cols, rows);
    const int needed = cols * rows;
    // This batch was emptied for the rebuild; another may still point at the
    // old arrays until its own, so they go to the free list, not away.
    if (state.pieces == nullptr || state.pieces->capacity < needed) {
        Release(state.pieces);
        state.pieces = Acquire(texture, needed);
    }
    Pieces &pieces = *state.pieces;

    const uint32_t first = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT) - 1;
    for (int i = 1; i < needed; ++i)
        reinterpret_cast<Append_t>(Offsets::FUN_SIMPLETEXTURE_APPEND_QUAD)(batch, texture);
    // Read after the appends: a reserve can move the records.
    const uint32_t count = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT);
    auto *records = Game::Read<QuadRecord *>(batch, Offsets::OFF_RENDERBATCH_QUAD_RECORDS);
    pieces.cols = cols;
    pieces.rows = rows;
    pieces.records = static_cast<int>(std::min<uint32_t>(count - first, static_cast<uint32_t>(needed)));
    for (int k = 0; k < pieces.records; ++k) {
        records[first + k].positions = &pieces.positions[static_cast<size_t>(k) * 4];
        records[first + k].texCoords = &pieces.coords[static_cast<size_t>(k) * 4];
    }
    g_pending.erase(texture);
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

// Every quad write: a layout (a resize, a scale change) or a cropped
// SetTexCoord.
void OnCornersStored(void *region) { Resync(region); }

const Texture::CornerStore::AutoSubscribe _cornerSub{&OnCornersStored};

// After the strata walk: the grids the draw found changed.
void OnFrame() {
    if (g_pending.empty())
        return;
    for (void *texture : g_pending)
        NotifyLayer(texture);
    g_pending.clear();
}

const Tick::FrameTick::AutoSubscribe _frameSub{&OnFrame};

// --- state ------------------------------------------------------------------

// Applies a change and marks the layer when the texture starts or stops
// being sliced, or its grid changes.
template <class Change> void Update(void *texture, Change change) {
    State &state = g_states[texture];
    const bool before = Emits(state);
    change(state);
    if (Emits(state) != before)
        NotifyLayer(texture);
    else
        Resync(texture);
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
    "Sets whether the centre and edges of a sliced texture stretch or repeat at the image's own size.",
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
    g_pending.erase(region);
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
    g_pending.clear();
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
    if (reinterpret_cast<uintptr_t>(positions) >=
        it->first + static_cast<uintptr_t>(it->second->capacity) * 4 * sizeof(C3Vector))
        return nullptr;
    return it->second->texture;
}

} // namespace Texture::Slice
