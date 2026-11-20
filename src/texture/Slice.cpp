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
// THE STATE, as the 3.80.2 client keeps it: one slice block per texture
// (texture+0x1F8, allocated by 0x140579DF0), four uint32 margins in image
// pixels and a mode byte, absent until a margin or a mode is set.
//   - SetTextureSliceMargins (0x140A6AC40) reads each margin as a number
//     from 0 to 2^32 - 1, truncated; SetTextureSliceMode (0x140A6AEB0) reads
//     the mode as `(uint8)(int)value`, at most 1. Either creates the block,
//     so the margins of a texture given only a mode read back as 0, 0, 0, 0.
//   - The Get functions return nothing while there is no block;
//     ClearTextureSlice and SetToDefaults (0x140579050) free it.
//   - SetAtlas (0x140579380) sets the margins and mode of an atlas with slice
//     data and frees the block for any other atlas. SetTexture,
//     SetTexture(nil) and SetColorTexture leave it.
//   - Margins of 0 draw the texture whole.
//
// THE GEOMETRY. 3.80.2 draws a sliced texture as one quad and slices it in a
// pixel shader; its CPU side (0x14059ADA0) works out what the shader gets,
// and this file lays out the same slicing as pieces:
//   - The image is the file's pixels times the coordinates' span
//     (0x14059CD60): for an atlas the sprite's pixels in its sheet, for a
//     cropped SetTexCoord the cropped pixels. The margins count from the
//     coordinates' edges, so a filling status bar (StatusBar::Fill crops the
//     coordinates) keeps its end cap at the end of the fill.
//   - The drawn size is the drawn quad's edge lengths (0x14059CC40), UL->UR
//     and UL->LL, in the texture's own units: corners keep their pixel size
//     whatever moves the quad (vertex offsets, animations).
//   - An axis drawn no larger than its image is not sliced: the whole image
//     scales down along it.
//   - An axis with no margins (given or so gated) stretches the other one in
//     proportion: the other axis's drawn length is scaled by image / drawn of
//     the unsliced axis, X first, so a three-slice bar drawn twice as tall has
//     caps twice as wide. An axis with no margins is drawn in one piece and is
//     never tiled.
//   - A gradient (SetGradient, one colour per vertex) spans the whole
//     texture: each piece's colours are blended from the texture's four.
//
// What 3.80.2's shader does is not in the client's code, and stays as it was:
//   - Margins wider than the drawn size: both drawn sizes shrink in
//     proportion while each keeps its whole margin of the image. Checked
//     against a retail screenshot of ui-damagemeters-header-bar (T14 + B22 on
//     a 28-pixel image, drawn 32 tall): the overlapping image rows draw in
//     both pieces, and its middle column's gradient restarts mid-height (a
//     per-side render matches with a mean absolute difference of 12.3, a split
//     at the proportional point with 16.0).
//   - Tiled mode (Enum.UITextureSliceMode.Tiled): the edges and the centre
//     repeat at the image's pixel scale along the axes they would stretch,
//     starting after the first margin; the last repeat on each axis is
//     cropped, its quad and its coordinates alike. 1.12's device wrap repeats
//     a whole file, never a sprite inside a sheet, so each repeat is its own
//     quad; past kMaxQuads pieces the texture draws stretched.
//   - Coordinates turned a quarter (an 8-argument SetTexCoord that maps x to
//     y, SetRotatesTexture) have no width along an axis, and draw unsliced.
//   - A texture with SetHorizTile / SetVertTile on draws unsliced: 3.80.2
//     samples with the wrap and slices in the shader at once, which pieces
//     cannot do.
//   - Texture::Mask finds the texture of a piece through Owner(); a mask's
//     own slice data is not read.
//
// THE MECHANISM. A texture emits one quad record into its layer batch
// (FUN_SIMPLETEXTURE_APPEND_QUAD), and the batch keeps pointers to the
// texture's four positions (+0xD4), eight coordinates (+0x104) and colours,
// read on every draw. A sliced texture's emit (Texture::Emit) lays out its
// grid and runs the engine's own append once more per extra piece, so every
// piece carries the engine's handle, blend, stride and shader, then points
// the records at position, coordinate and (for a gradient) colour arrays kept
// here.
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
// texture then: the four drawn corners (+0xD4), the coordinates (+0x104) and
// the colours. Many writers change those without a layout: the corner store,
// Texture::Transform (rotation, vertex offsets), Animation::Region
// (translation, scale, rotation), Texture::Tile's counts, StatusBar::Fill,
// SetTexCoord on an uncropped texture (which writes no quad). Reading them at
// the draw follows every one of them with no hook of its own. The positions
// are a bilinear blend of the four drawn corners, so a rotated, warped or
// animated quad carries its pieces, repeats included, along.
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
// corner (UL), each piece four positions, four coordinates and four colours
// in the engine's corner order UL, LL, UR, LR. The arrays are sized once, for
// `capacity` pieces, and never reallocated: batch records point into them.
struct Pieces {
    std::vector<C3Vector> positions;
    std::vector<C2Vector> coords;
    std::vector<uint32_t> colors;
    int capacity = 0;
    void *texture = nullptr;
    int cols = 0;    // the grid the last emit laid out
    int rows = 0;
    int records = 0; // the batch records the last emit pointed here
    // The texture's own colours, as its first record had them: one colour
    // (stride 0) shared by every piece, or one per corner.
    const uint32_t *srcColor = nullptr;
    uint32_t colorStride = 0;
};

enum Margin : int { kLeft = 0, kTop = 1, kRight = 2, kBottom = 3 };

struct State {
    bool present = false;    // 3.80.2's slice block exists
    uint32_t margins[4] = {}; // image pixels
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
    return s.present && (s.margins[kLeft] != 0 || s.margins[kTop] != 0 || s.margins[kRight] != 0 ||
                         s.margins[kBottom] != 0);
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
        p->colors.resize(static_cast<size_t>(p->capacity) * 4);
    }
    p->texture = texture;
    p->cols = p->rows = p->records = 0;
    p->srcColor = nullptr;
    p->colorStride = 0;
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

// The colour at (u, v) of four BGRA colours, channel by channel.
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

float Length(const C3Vector &a, const C3Vector &b) {
    return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
}

float Length(const C2Vector &a, const C2Vector &b) {
    return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
}

// The file's pixel size: the device texture's (OFF_GXTEX_WIDTH / HEIGHT, the
// size Texture::Tile counts against), for an atlas its sheet's.
bool FileSize(void *texture, float &width, float &height) {
    void *image = Game::Read<void *>(texture, Offsets::OFF_SIMPLETEXTURE_HTEXTURE);
    const void *device = image != nullptr ? Game::Read<void *>(image, Offsets::OFF_HTEXTURE_DEVICE) : nullptr;
    if (device == nullptr)
        return false;
    width = static_cast<float>(Game::Read<uint32_t>(device, Offsets::OFF_GXTEX_WIDTH));
    height = static_cast<float>(Game::Read<uint32_t>(device, Offsets::OFF_GXTEX_HEIGHT));
    return width > 0.0f && height > 0.0f;
}

// One axis: `low` / `high` its margins in image pixels (after the gate),
// `image` its image pixels, `drawn` its drawn length in the same units (after
// the rescale). An axis with no margins is one piece.
void Axis(float low, float high, float image, float drawn, Cuts &out) {
    if (low <= 0.0f && high <= 0.0f) {
        out = {0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0};
        return;
    }
    // The margins as coordinate fractions, from the coordinates' edges,
    // clamped to the image. Margins wider than the image overlap in it, and
    // each side keeps its whole margin (retail's observed behaviour; see the
    // header).
    const float shownLow = std::min(low, image);
    const float shownHigh = std::min(high, image);
    out.p1 = shownLow / image;
    out.p2 = 1.0f - shownHigh / image;
    // One image pixel is one unit of the texture's own size; when the two do
    // not fit the drawn length, both drawn sizes shrink in proportion.
    float screenLow = shownLow, screenHigh = shownHigh;
    if (screenLow + screenHigh > drawn) {
        const float k = drawn / (screenLow + screenHigh);
        screenLow *= k;
        screenHigh *= k;
    }
    out.s1 = screenLow / drawn;
    out.s2 = 1.0f - screenHigh / drawn;
    // One repeat of the middle at the same pixel scale. The small allowance
    // keeps a middle that is a whole number of repeats from gaining a sliver.
    const float middlePixels = std::fabs(image - shownLow - shownHigh);
    out.tile = middlePixels / drawn;
    const float middle = out.s2 - out.s1;
    // A repeat at most one image pixel long is one strip of texels: repeating
    // it draws the same image as stretching it, so one stretched piece stands
    // in for the repeats (ui-hud-cooldownmanager-bar-bg's centre and edges
    // are one pixel thick, about 70 quads across a bar otherwise).
    const bool oneTexel = middlePixels <= 1.0f + 1e-4f;
    out.repeats = !oneTexel && out.tile > 0.0f && middle > 0.0f
                      ? std::max(1.0, std::ceil(static_cast<double>(middle) / out.tile - 1e-4))
                      : 1.0;
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
// or the state asks; false when it draws whole: no image size, no drawn size,
// coordinates with no width along an axis, or no margins left after the gate.
bool Plan(void *texture, const State &state, bool stretched) {
    float fileW, fileH;
    if (!FileSize(texture, fileW, fileH))
        return false;
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);
    // Coordinates turned a quarter have no width along an axis.
    if (!(std::fabs(coords[2].x - coords[0].x) > 1e-6f) || !(std::fabs(coords[1].y - coords[0].y) > 1e-6f))
        return false;

    // 0x14059CD60: the file's pixels times the coordinates' span.
    const float imageW = fileW * Length(coords[0], coords[2]);
    const float imageH = fileH * Length(coords[0], coords[1]);
    // 0x14059CC40: the drawn quad's edges in the texture's own units.
    float drawnW = static_cast<float>(Frame::Layout::ToLocalUnits(texture, Length(corners[0], corners[2])));
    float drawnH = static_cast<float>(Frame::Layout::ToLocalUnits(texture, Length(corners[0], corners[1])));
    if (!(imageW > 0.0f) || !(imageH > 0.0f) || !(drawnW > 0.0f) || !(drawnH > 0.0f))
        return false;

    // 0x14059ADA0: an axis drawn no larger than its image is not sliced; an
    // axis with no margins scales the other's drawn length, X first.
    float left = static_cast<float>(state.margins[kLeft]), right = static_cast<float>(state.margins[kRight]);
    float top = static_cast<float>(state.margins[kTop]), bottom = static_cast<float>(state.margins[kBottom]);
    if (drawnW <= imageW)
        left = right = 0.0f;
    if (drawnH <= imageH)
        top = bottom = 0.0f;
    if (left == 0.0f && right == 0.0f)
        drawnH *= imageW / drawnW;
    if (top == 0.0f && bottom == 0.0f)
        drawnW *= imageH / drawnH;
    if (left == 0.0f && right == 0.0f && top == 0.0f && bottom == 0.0f)
        return false;

    Cuts x, y;
    Axis(left, right, imageW, drawnW, x);
    Axis(top, bottom, imageH, drawnH, y);

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
// three by three when it draws whole or tiles (Texture::Tile).
void Grid(void *texture, const State &state, int &cols, int &rows) {
    cols = rows = kStretchedAxis;
    if (!Texture::Wrap::Tiles(texture) && Plan(texture, state, false)) {
        cols = static_cast<int>(g_cols.size());
        rows = static_cast<int>(g_rows.size());
    }
}

// The four colours of the texture, for a gradient.
const uint32_t *SourceColors(const Pieces &p) {
    return p.colorStride != 0 ? p.srcColor : nullptr;
}

// Writes the laid-out grid into the emitted one; pieces past it collapse to a
// point and draw nothing.
void Fill(void *texture, Pieces &out) {
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);
    const uint32_t *colors = SourceColors(out);
    const int cols = static_cast<int>(g_cols.size());
    const int rows = static_cast<int>(g_rows.size());
    for (int r = 0; r < out.rows; ++r) {
        for (int c = 0; c < out.cols; ++c) {
            const size_t at = static_cast<size_t>(r * out.cols + c) * 4;
            C3Vector *pos = &out.positions[at];
            C2Vector *tex = &out.coords[at];
            uint32_t *col = &out.colors[at];
            if (r >= rows || c >= cols) {
                for (int v = 0; v < 4; ++v) {
                    pos[v] = corners[0];
                    tex[v] = coords[0];
                    if (colors != nullptr)
                        col[v] = colors[0];
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
                if (colors != nullptr)
                    col[v] = BlendColor(colors, xs[v], ys[v]);
            }
        }
    }
}

// The texture drawn whole: the first piece is its quad, the others collapse
// to a point and draw nothing.
void Whole(void *texture, Pieces &out) {
    const auto *coords = Game::Ptr<C2Vector>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    const auto *corners = Game::Ptr<C3Vector>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS);
    const uint32_t *colors = SourceColors(out);
    for (int k = 0; k < out.records; ++k) {
        for (int v = 0; v < 4; ++v) {
            const size_t at = static_cast<size_t>(k) * 4 + v;
            out.positions[at] = k == 0 ? corners[v] : corners[0];
            out.coords[at] = coords[v];
            if (colors != nullptr)
                out.colors[at] = colors[v];
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
    pieces.srcColor = records[first].color;
    pieces.colorStride = records[first].colorStride;
    for (int k = 0; k < pieces.records; ++k) {
        records[first + k].positions = &pieces.positions[static_cast<size_t>(k) * 4];
        records[first + k].texCoords = &pieces.coords[static_cast<size_t>(k) * 4];
        if (pieces.colorStride != 0)
            records[first + k].color = &pieces.colors[static_cast<size_t>(k) * 4];
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
    state.present = false;
    for (uint32_t &m : state.margins)
        m = 0;
    state.mode = kStretched;
}

// --- Lua -------------------------------------------------------------------

constexpr const char *kMarginsUsage = "Usage: self:SetTextureSliceMargins(left, top, right, bottom)";
constexpr const char *kModeUsage = "Usage: self:SetTextureSliceMode(sliceMode)";

int __fastcall Script_SetTextureSliceMargins(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    uint32_t margins[4];
    for (int i = 0; i < 4; ++i) {
        // 0x140A65F50: a number from 0 to 2^32 - 1, truncated.
        const double v = Game::Lua::IsNumber(L, 2 + i) ? Game::Lua::ToNumber(L, 2 + i) : -1.0;
        if (!(v >= 0.0 && v <= 4294967295.0)) {
            Game::Lua::ArgError(L, 2 + i, kMarginsUsage);
            return 0;
        }
        margins[i] = static_cast<uint32_t>(v);
    }
    Update(texture, [&](State &s) {
        s.present = true;
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
    if (it == g_states.end() || !it->second.present)
        return 0;
    for (uint32_t m : it->second.margins)
        Game::Lua::PushNumber(L, static_cast<double>(m));
    return 4;
}

int __fastcall Script_SetTextureSliceMode(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::ArgError(L, 2, kModeUsage);
        return 0;
    }
    // 0x140A6E580: `(uint8)(int)value`, at most 1. The int conversion of a
    // value out of its range gives 0x80000000, as the x86 conversion does.
    const double v = Game::Lua::ToNumber(L, 2);
    const int whole = v > -2147483649.0 && v < 2147483648.0 ? static_cast<int>(v) : INT32_MIN;
    const uint8_t mode = static_cast<uint8_t>(whole);
    if (mode > kTiled) {
        Game::Lua::ArgError(L, 2, kModeUsage);
        return 0;
    }
    Update(texture, [&](State &s) {
        s.present = true;
        s.mode = mode;
    });
    return 0;
}

int __fastcall Script_GetTextureSliceMode(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    const auto it = g_states.find(texture);
    if (it == g_states.end() || !it->second.present)
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
    Game::Doc::Req("left", "uiUnit", "Whole image pixels, from the edge of the texture coordinates."),
    Game::Doc::Req("top", "uiUnit"),
    Game::Doc::Req("right", "uiUnit"),
    Game::Doc::Req("bottom", "uiUnit"),
};
const Game::Doc::Function kSetTextureSliceMargins{
    "Draws the texture as a nine-slice: corners that keep their size, edges and a centre that stretch. "
    "An axis drawn no larger than the image is not sliced.",
    kMarginArgs, {}};
const Game::Doc::Function kGetTextureSliceMargins{
    "Returns the texture's slice margins; nothing when neither margins nor a mode are set.", {}, kMarginArgs};

const Game::Doc::Field kModeArgs[] = {
    Game::Doc::Req("sliceMode", "UITextureSliceMode"),
};
const Game::Doc::Function kSetTextureSliceMode{
    "Sets whether the centre and edges of a sliced texture stretch or repeat at the image's own size.",
    kModeArgs, {}};
const Game::Doc::Function kGetTextureSliceMode{
    "Returns the texture's slice mode; nothing when neither margins nor a mode are set.", {}, kModeArgs};
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
        s.present = true;
        s.margins[kLeft] = static_cast<uint32_t>(info.sliceLeft);
        s.margins[kTop] = static_cast<uint32_t>(info.sliceTop);
        s.margins[kRight] = static_cast<uint32_t>(info.sliceRight);
        s.margins[kBottom] = static_cast<uint32_t>(info.sliceBottom);
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
