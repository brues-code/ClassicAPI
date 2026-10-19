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

// `C_Map.GetMapOverlays([areaID])` — every WorldMapOverlay.dbc entry for
// a zone, regardless of exploration, with the tile grid fully resolved.
//
// The engine's own surface (GetNumMapOverlays / GetMapOverlayInfo) only
// exposes DISCOVERED overlays of the zone on screen — its map rebuild
// keeps a row only when one of its areas passes the explored-bit test —
// which is why map addons (pfUI's mapreveal, Cartographer …) ship
// hand-measured overlay tables to draw unexplored areas. This reads the
// DBC directly instead: live data for every zone, on any client, no
// per-server table drift.
//
// With no argument, returns overlays for the world map's CURRENT view
// (mirroring the resolution the engine's GetMapInfo does: continent /
// zone selection → WorldMapArea row). With a numeric `areaID`
// (AreaTable id — the stable zone identity), resolves the WorldMapArea
// row whose areaID column matches.
//
// ## Why the tile grid is resolved here (Octo data quirks)
//
// An overlay's art is `<textureName>1.blp, 2, …` numbered row-major on a
// 256px grid, canonically `ceil(w/256) x ceil(h/256)` tiles with only
// the last column/row allowed partial. On stock 1.12 data that formula
// is always right; on Octo it is not, in three ways (established
// empirically by the InkLab map generator, mirrored here):
//
//   1. FOREIGN TILES: unrelated art continues an overlay's number
//      sequence in the same folder (Icepoint Rock's folder carries
//      `kaneqnuun` tiles, Hyjal carries Tel Abim art). Anything derived
//      from "what files exist" draws duplicate landmass. The overlay
//      list must come ONLY from the DBC (which this module guarantees),
//      and tiles past the true grid must be dropped.
//   2. SLIVER COLUMNS: the DBC rect can round away a narrow edge column
//      (kaneqnuun's real art has a third, 8px-wide column; its DBC `w`
//      yields 2 columns). Trusting ceil(w/256) both loses the sliver
//      and — worse — shears the row-major layout into diagonal strips.
//      The art itself reveals the truth: only a row's LAST tile may be
//      narrower than 256, so the first partial-width tile (searched one
//      past the DBC's column count) marks the real column count, and
//      the first partial-height tile marks the last row.
//   3. UPSCALED RE-EXPORTS: the same rect re-exported at higher
//      resolution (Stonetalon's WindshearCrag: 4 tiles for a 1-tile
//      rect). All tiles are real and must be kept, drawn scaled to the
//      rect. Only distinguishable from quirk 1 when the art has NO
//      partial edge anywhere AND the DBC rect is a whole multiple of
//      256 in both dimensions; the grid shape is then recovered from
//      the aspect ratio (cols ≈ round(sqrt(n·w/h))).
//
// Tile dimensions come from the engine's own BLP reader (`FUN_BLP_OPEN`,
// so patch-MPQ overrides are honored). Resolution runs once per overlay
// per session (cached).
//
// ## HD map patches (uniform tile upscale)
//
// A custom `patch-*.mpq` can replace the overlay art with higher-res tiles
// (every 256px tile re-exported at 256*scale). Because we read the *file*
// dimensions, that upscale must not be assumed to be 256: the resolver
// detects the full-cell size `S` (the largest tile dimension = 256*scale)
// and expresses both the partial-tile threshold and the texcoords relative
// to it. Draw sizes/positions remain in logical map pixels (the patch changes
// texture resolution, not the map canvas), so an interior tile reads texcoord
// 1.0 at any scale and stock data (S == 256) is byte-identical to before.
// The upscale factor need not be integral; only a uniform per-overlay scale
// is assumed.
//
// ## Return shape
//
// Array of overlay tables:
//   textureName   — bare DBC name ("GOLDSHIRE")
//   texturePath   — "Interface\WorldMap\<dir>\GOLDSHIRE" (engine format)
//   areaID        — primary AreaTable id revealed (first nonzero)
//   areaIDs       — all AreaTable ids revealed (WorldMapOverlay.dbc
//                   areaID[4]; usually one, some overlays span 2-3 subzones)
//   textureWidth / textureHeight — the DBC placement rect (reliable for
//                   placement; its IMPLIED tile count is what lies)
//   offsetX / offsetY — placement on the zone canvas
//   mapPointX / mapPointY
//   hitRectTop/Left/Bottom/Right — hit rectangle in world-map canvas px
//                   (~1002x668); the tight clickable bounds of the landmass,
//                   the subzone-center source map addons scrape
//   tileCols / tileRows / upscaled — the resolved grid
//   tiles         — array of ready-to-draw tiles:
//                   { file      — texture path incl. number (SetTexture-ready)
//                     width, height        — draw size in map pixels
//                     texCoordX, texCoordY — right/bottom texcoords
//                     offsetX, offsetY }   — absolute canvas position
//   fileDataIDs   — ordered tile texture paths (retail's field name; vanilla
//                   has no numeric fileDataIDs, so these are paths). Redundant
//                   with tiles[].file — for retail-ported exploration addons.
//
// Texture-less rows (a subzone hit region with no distinct art) still appear,
// with textureName == "", zero size, and an empty `tiles` table — so
// geometry consumers see every subzone while `ipairs(ov.tiles)` draw loops
// stay safe. A consumer draws a textured overlay with a dumb loop: per tile
// SetTexture, SetWidth/Height, SetTexCoord(0, texCoordX, 0, texCoordY), SetPoint.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "map/Area.h"
#include "map/Overlays.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace Map::Overlays {

namespace {

constexpr int kMaxTiles = 64;

// Smallest power of two >= v (min 16), matching the engine's own
// WorldMapFrame tile-file-size loop. A tile's content fills content/NextPot
// of its (power-of-two) file — a fraction that's identical whether the file
// is stock or HD-upscaled, so texcoords derived from it are scale-invariant.
int NextPot(int v) {
    int p = 16;
    while (p < v)
        p <<= 1;
    return p;
}

// The engine's BLP reader, driven exactly as GetPortraitAlphaMask drives it
// (see the FUN_BLP_OPEN block in Offsets.h). Open in the constructor, decode
// on demand, free and reset in the destructor. Only one reader may be live
// at a time: the engine keeps the file in one module-static buffer.
class BlpReader {
public:
    explicit BlpReader(const char *path) {
        std::memset(rec_, 0, sizeof(rec_));
        Put(Offsets::OFF_BLP_MAGIC, 0x32504C42); // 'BLP2'
        Put(Offsets::OFF_BLP_VERSION, 1);
        rec_[Offsets::OFF_BLP_ALPHA_TYPE] = 2;
        Put(Offsets::OFF_BLP_INIT_4A4, 100);
        open_ = reinterpret_cast<Open_t>(Offsets::FUN_BLP_OPEN)(rec_, path, 1) != 0;
    }
    ~BlpReader() {
        if (decoded_)
            reinterpret_cast<FreeScratch_t>(Offsets::FUN_BLP_FREE_SCRATCH)(rec_, 0);
        reinterpret_cast<Reset_t>(Offsets::FUN_BLP_RESET)(rec_);
    }
    BlpReader(const BlpReader &) = delete;
    BlpReader &operator=(const BlpReader &) = delete;

    bool IsOpen() const { return open_; }
    uint32_t Width() const { return Game::Read<uint32_t>(rec_, Offsets::OFF_BLP_WIDTH); }
    uint32_t Height() const { return Game::Read<uint32_t>(rec_, Offsets::OFF_BLP_HEIGHT); }

    // Level 0 as BGRA8888 (alpha in byte 3), valid until destruction; null
    // on failure.
    const uint8_t *DecodeBgra() {
        if (!open_)
            return nullptr;
        void *out = nullptr;
        uint32_t size = 0;
        decoded_ = true;
        if (reinterpret_cast<DecodeLevel_t>(Offsets::FUN_BLP_DECODE_LEVEL)(
                rec_, Offsets::GX_TEX_FORMAT_BGRA8888, 0, &out, &size) == 0)
            return nullptr;
        return static_cast<const uint8_t *>(out);
    }

private:
    using Open_t = int(__thiscall *)(void *rec, const char *path, int flag);
    using DecodeLevel_t = int(__thiscall *)(void *rec, uint32_t fmt, uint32_t level,
                                            void **out, uint32_t *size);
    using FreeScratch_t = void(__thiscall *)(void *rec, uint32_t level);
    using Reset_t = void(__thiscall *)(void *rec);

    void Put(uintptr_t off, uint32_t v) { std::memcpy(rec_ + off, &v, sizeof(v)); }

    uint8_t rec_[Offsets::BLP_RECORD_SIZE];
    bool open_ = false;
    bool decoded_ = false;
};

// Where one overlay's art lives and where it sits on the zone canvas.
struct OverlaySource {
    char basePath[0x104]; // numberless tile path
    int dbcW, dbcH;       // DBC placement rect
    int offX, offY;
};

// Fills `out` for a WorldMapOverlay row. False for a texture-less row (a hit
// region with no art). The path mirrors the engine's own GetMapOverlayInfo
// format: the overlay's WorldMapArea directory, or the "World" folder when
// that row has no name.
bool SourceFor(const uint8_t *rec, OverlaySource *out) {
    const char *texName = Game::Read<const char *>(rec, Offsets::OFF_WMO_TEXTURE_NAME);
    if (texName == nullptr || texName[0] == '\0')
        return false;
    const uint8_t *wmaRec =
        DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS, Offsets::VAR_WORLDMAP_AREA_COUNT,
                    Game::Read<uint32_t>(rec, Offsets::OFF_WMO_WORLDMAP_AREA));
    const char *mapDir =
        (wmaRec != nullptr) ? Game::Read<const char *>(wmaRec, Offsets::OFF_WMA_NAME) : nullptr;
    if (mapDir != nullptr && mapDir[0] != '\0')
        std::snprintf(out->basePath, sizeof(out->basePath), "Interface\\WorldMap\\%s\\%s",
                      mapDir, texName);
    else
        std::snprintf(out->basePath, sizeof(out->basePath), "Interface\\WorldMap\\World\\%s",
                      texName);
    out->dbcW = Game::Read<int>(rec, Offsets::OFF_WMO_TEXTURE_WIDTH);
    out->dbcH = Game::Read<int>(rec, Offsets::OFF_WMO_TEXTURE_HEIGHT);
    out->offX = Game::Read<int>(rec, Offsets::OFF_WMO_OFFSET_X);
    out->offY = Game::Read<int>(rec, Offsets::OFF_WMO_OFFSET_Y);
    return true;
}

// The overlay's art on the zone canvas: one byte per canvas pixel (the
// art's alpha there) over the bounding box of its tiles. Built on the first
// position query that lands inside the overlay (see ArtAlphaAt).
struct AlphaMask {
    int x0, y0, w, h;
    uint8_t *alpha;
};

// Resolved tile grid for one overlay, cached for the session (DBC and
// MPQ contents are immutable per process).
struct ResolvedOverlay {
    uint8_t cols;
    uint8_t rows;
    uint8_t count; // tiles to draw (foreign tail already dropped)
    bool upscaled;
    uint16_t cell; // full-cell file size (256 on stock, 256*scale on an HD patch)
    uint16_t fileW[kMaxTiles]; // BLP file dimensions per tile
    uint16_t fileH[kMaxTiles];
    bool maskBuilt;
    AlphaMask mask;
};

ResolvedOverlay **g_cache = nullptr;
int g_cacheSize = 0;

// Reads one BLP's dimensions through the engine reader. False when the
// file doesn't exist (end of the tile sequence) or isn't a BLP2.
bool ReadBlpDims(const char *path, uint16_t *outW, uint16_t *outH) {
    BlpReader blp(path);
    if (!blp.IsOpen())
        return false;
    const uint32_t w = blp.Width();
    const uint32_t h = blp.Height();
    if (w == 0 || w > 4096 || h == 0 || h > 4096)
        return false;
    *outW = static_cast<uint16_t>(w);
    *outH = static_cast<uint16_t>(h);
    return true;
}

// The quirk disambiguation (see module comment). `basePath` is the
// numberless texture path; dbcW/dbcH the overlay's DBC rect.
void ResolveTiles(ResolvedOverlay *out, const char *basePath, int dbcW,
                  int dbcH) {
    char path[0x120];
    int n = 0;
    while (n < kMaxTiles) {
        std::snprintf(path, sizeof(path), "%s%d.blp", basePath, n + 1);
        if (!ReadBlpDims(path, &out->fileW[n], &out->fileH[n]))
            break;
        ++n;
    }

    int dbcCols = (dbcW + 255) / 256;
    int dbcRows = (dbcH + 255) / 256;
    if (dbcCols < 1)
        dbcCols = 1;
    if (dbcRows < 1)
        dbcRows = 1;

    // Full-cell file size. On stock data a full (interior) tile is 256px; an
    // HD map patch (patch-*.mpq) upscales every tile uniformly, so a full cell
    // is 256*scale. Full tiles are the largest; edges/slivers are smaller — so
    // the max tile dimension is the full-cell size. Partial-tile detection
    // (and the texcoords in PushTiles) must compare against this S, not a
    // hardcoded 256, or an HD edge tile (e.g. a 200px edge upscaled to 400px)
    // reads as "full" and shears the row-major grid into diagonal strips.
    int S = 256;
    for (int i = 0; i < n; ++i) {
        if (out->fileW[i] > S)
            S = out->fileW[i];
        if (out->fileH[i] > S)
            S = out->fileH[i];
    }
    out->cell = static_cast<uint16_t>(S);

    // Real column count: the first partial-width tile is a row's right
    // edge. Search one past dbcCols so a sliver column the DBC width
    // rounds off is still found.
    int cols = dbcCols;
    bool widthEdge = false;
    for (int i = 0; i < n && i <= dbcCols; ++i) {
        if (out->fileW[i] < S) {
            cols = i + 1;
            widthEdge = true;
            break;
        }
    }

    // Real row count: the row holding the first partial-height tile is
    // the last row.
    int rows = dbcRows;
    bool heightEdge = false;
    for (int i = 0; i < n; ++i) {
        if (out->fileH[i] < S) {
            rows = i / cols + 1;
            heightEdge = true;
            break;
        }
    }

    int limit = cols * rows;
    bool upscaled = false;
    // Upscale exception: no partial edge anywhere and a whole-tile DBC
    // rect means the extras are higher-res art for the same rect, not
    // foreign — keep them all; recover the grid from the aspect ratio.
    if (!widthEdge && !heightEdge && dbcW % 256 == 0 && dbcH % 256 == 0 &&
        n > limit) {
        upscaled = true;
        cols = static_cast<int>(std::floor(
            std::sqrt(static_cast<double>(n) * dbcW / dbcH) + 0.5));
        if (cols < 1)
            cols = 1;
        if (cols > n)
            cols = n;
        limit = n;
        rows = (n + cols - 1) / cols;
    }
    if (limit > n)
        limit = n;
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;

    out->cols = static_cast<uint8_t>(cols);
    out->rows = static_cast<uint8_t>(rows);
    out->count = static_cast<uint8_t>(limit);
    out->upscaled = upscaled;
}

ResolvedOverlay *CachedResolve(int overlayId, const OverlaySource &src) {
    const int count =
        Game::Read<int>(Offsets::VAR_WORLDMAP_OVERLAY_COUNT);
    if (g_cache == nullptr || g_cacheSize < count + 1) {
        auto **fresh = new ResolvedOverlay *[count + 1]();
        for (int i = 0; i < g_cacheSize; ++i)
            fresh[i] = g_cache[i];
        delete[] g_cache;
        g_cache = fresh;
        g_cacheSize = count + 1;
    }
    if (overlayId < 0 || overlayId >= g_cacheSize)
        return nullptr;
    if (g_cache[overlayId] == nullptr) {
        auto *r = new ResolvedOverlay();
        ResolveTiles(r, src.basePath, src.dbcW, src.dbcH);
        g_cache[overlayId] = r;
    }
    return g_cache[overlayId];
}

// Where tile `i` of a resolved overlay is drawn on the zone canvas, and how
// much of its file it shows.
struct TileDraw {
    double x, y, w, h;           // canvas position and draw size
    double texCoordX, texCoordY; // right / bottom texcoords
};

TileDraw TileGeometry(const ResolvedOverlay *r, int i, const OverlaySource &src) {
    const int col = i % r->cols;
    const int row = i / r->cols;
    TileDraw t;
    if (r->upscaled) {
        // Higher-res re-export of the same rect: tiles split the DBC
        // rect evenly, full texcoords.
        t.w = static_cast<double>(src.dbcW) / r->cols;
        t.h = static_cast<double>(src.dbcH) / r->rows;
        t.texCoordX = 1.0;
        t.texCoordY = 1.0;
        t.x = src.offX + col * t.w;
        t.y = src.offY + row * t.h;
        return t;
    }
    // Canonical 256px logical grid. Draw sizes/positions stay in logical
    // map pixels; the texcoord is the content fraction of the (possibly
    // HD-upscaled) file. `scale` = file px per logical px (1 on stock, 2/4
    // on an HD patch), so a full interior tile reads texCoord 1.0 regardless
    // of file size, and an edge's content (remainder * scale) is measured
    // against its actual file width.
    const double scale = (r->cell > 0) ? (r->cell / 256.0) : 1.0;
    t.w = 256.0;
    t.h = 256.0;
    t.texCoordX = 1.0;
    t.texCoordY = 1.0;
    if (col == r->cols - 1) {
        const int rem = src.dbcW - 256 * (r->cols - 1);
        if (rem <= 0) {
            // Sliver the DBC width rounded away: whole file is content
            // (its logical width is the file width de-scaled).
            t.w = r->fileW[i] / scale;
        } else {
            // Content fills rem/NextPot(rem) of the POT tile file.
            // Scale-invariant, so an HD-upscaled file (POT*scale) reads the
            // same fraction — using the raw file width here would halve the
            // texcoord on any tile the patch doubled.
            t.w = rem;
            t.texCoordX = rem / static_cast<double>(NextPot(rem));
        }
    }
    if (row == r->rows - 1) {
        const int rem = src.dbcH - 256 * (r->rows - 1);
        if (rem <= 0) {
            t.h = r->fileH[i] / scale;
        } else {
            t.h = rem;
            t.texCoordY = rem / static_cast<double>(NextPot(rem));
        }
    }
    t.x = src.offX + col * 256;
    t.y = src.offY + row * 256;
    return t;
}

// Pushes the `tiles` array for one overlay (leaves it on the stack top).
void PushTiles(void *L, const ResolvedOverlay *r, const OverlaySource &src) {
    Game::Lua::NewTable(L);
    if (r == nullptr || r->count == 0)
        return;

    char file[0x120];
    for (int i = 0; i < r->count; ++i) {
        const TileDraw t = TileGeometry(r, i, src);
        std::snprintf(file, sizeof(file), "%s%d", src.basePath, i + 1);
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(L, "file", file);
        Game::Lua::SetFieldNumber(L, "width", t.w);
        Game::Lua::SetFieldNumber(L, "height", t.h);
        Game::Lua::SetFieldNumber(L, "texCoordX", t.texCoordX);
        Game::Lua::SetFieldNumber(L, "texCoordY", t.texCoordY);
        Game::Lua::SetFieldNumber(L, "offsetX", t.x);
        Game::Lua::SetFieldNumber(L, "offsetY", t.y);
        Game::Lua::SetTable(L, -3);
    }
}

// Rasterises the overlay's art alpha onto the canvas pixels its tiles cover.
// Each canvas pixel takes the texel under its centre, placed exactly as the
// tile is drawn (TileGeometry), so the mask matches what the map shows at any
// HD-patch scale. A tile that fails to decode leaves its pixels at 0.
void BuildMask(ResolvedOverlay *r, const OverlaySource &src) {
    r->maskBuilt = true;
    if (r->count == 0)
        return;
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (int i = 0; i < r->count; ++i) {
        const TileDraw t = TileGeometry(r, i, src);
        minX = (std::min)(minX, t.x);
        minY = (std::min)(minY, t.y);
        maxX = (std::max)(maxX, t.x + t.w);
        maxY = (std::max)(maxY, t.y + t.h);
    }
    AlphaMask &m = r->mask;
    m.x0 = static_cast<int>(std::floor(minX));
    m.y0 = static_cast<int>(std::floor(minY));
    m.w = static_cast<int>(std::ceil(maxX)) - m.x0;
    m.h = static_cast<int>(std::ceil(maxY)) - m.y0;
    if (m.w <= 0 || m.h <= 0)
        return;
    m.alpha = new uint8_t[static_cast<size_t>(m.w) * m.h]();

    char path[0x120];
    for (int i = 0; i < r->count; ++i) {
        const TileDraw t = TileGeometry(r, i, src);
        std::snprintf(path, sizeof(path), "%s%d.blp", src.basePath, i + 1);
        BlpReader blp(path);
        const uint8_t *bgra = blp.DecodeBgra();
        if (bgra == nullptr)
            continue;
        const int fw = static_cast<int>(blp.Width());
        const int fh = static_cast<int>(blp.Height());
        const int cx0 = static_cast<int>(std::floor(t.x));
        const int cy0 = static_cast<int>(std::floor(t.y));
        const int cx1 = static_cast<int>(std::ceil(t.x + t.w));
        const int cy1 = static_cast<int>(std::ceil(t.y + t.h));
        for (int cy = cy0; cy < cy1; ++cy) {
            const double v = (cy + 0.5 - t.y) / t.h;
            if (v < 0.0 || v >= 1.0)
                continue;
            const int fy = (std::min)(static_cast<int>(v * t.texCoordY * fh), fh - 1);
            for (int cx = cx0; cx < cx1; ++cx) {
                const double u = (cx + 0.5 - t.x) / t.w;
                if (u < 0.0 || u >= 1.0)
                    continue;
                const int fx = (std::min)(static_cast<int>(u * t.texCoordX * fw), fw - 1);
                m.alpha[static_cast<size_t>(cy - m.y0) * m.w + (cx - m.x0)] =
                    bgra[(static_cast<size_t>(fy) * fw + fx) * 4 + 3];
            }
        }
    }
}

} // namespace

using IsAreaExplored_t = int(__thiscall *)(const void *playerInfo, int areaBit);

// The overlay loop of the engine's map rebuild (FUN_004a67a0,
// 0x004A6B87..0x004A6C07), one area at a time: an id <= 0 or without a
// record fails; an area whose gate (+0x28) is < 0 needs no exploration;
// otherwise the engine's own bit test decides.
bool IsAreaExplored(int areaID, const uint8_t *player) {
    if (areaID <= 0)
        return false;
    const uint8_t *areaRec = DBC::Record(Offsets::VAR_AREATABLE_RECORDS,
                                         Offsets::VAR_AREATABLE_COUNT,
                                         static_cast<uint32_t>(areaID));
    if (areaRec == nullptr)
        return false;
    if (Game::Read<int>(areaRec, Offsets::OFF_AREATABLE_EXPLORE_GATE) < 0)
        return true;
    if (player == nullptr)
        return false;
    auto isAreaExplored =
        reinterpret_cast<IsAreaExplored_t>(Offsets::FUN_IS_AREA_EXPLORED);
    return isAreaExplored(player + Offsets::OFF_CGPLAYER_INFO,
                          Game::Read<int>(areaRec, Offsets::OFF_AREATABLE_AREA_BIT)) != 0;
}

// An overlay is explored if ANY of its four areas is. With no player the
// engine keeps no overlays at all, so none is explored.
bool IsOverlayExplored(const uint8_t *overlayRec, const uint8_t *player) {
    if (player == nullptr)
        return false;
    for (int k = 0; k < 4; ++k) {
        if (IsAreaExplored(Game::Read<int>(overlayRec, Offsets::OFF_WMO_AREA_ID + k * 4),
                           player))
            return true;
    }
    return false;
}

int ArtAlphaAt(const uint8_t *overlayRec, double canvasX, double canvasY) {
    OverlaySource src{};
    if (!SourceFor(overlayRec, &src))
        return 0;
    ResolvedOverlay *r =
        CachedResolve(Game::Read<int>(overlayRec, 0), src);
    if (r == nullptr)
        return 0;
    if (!r->maskBuilt) {
        // Decode the art only for a point on one of its tiles, so a query
        // costs nothing for the overlays it misses.
        bool onTile = false;
        for (int i = 0; i < r->count && !onTile; ++i) {
            const TileDraw t = TileGeometry(r, i, src);
            onTile = canvasX >= t.x && canvasX < t.x + t.w && canvasY >= t.y &&
                     canvasY < t.y + t.h;
        }
        if (!onTile)
            return 0;
        BuildMask(r, src);
    }
    const AlphaMask &m = r->mask;
    if (m.alpha == nullptr)
        return 0;
    const int x = static_cast<int>(std::floor(canvasX)) - m.x0;
    const int y = static_cast<int>(std::floor(canvasY)) - m.y0;
    if (x < 0 || y < 0 || x >= m.w || y >= m.h)
        return 0;
    return m.alpha[static_cast<size_t>(y) * m.w + x];
}

namespace {

// `C_Map.GetMapOverlays([areaID])` — see the module comment. Thin wrapper
// over the shared `PushZoneOverlays` with no exploration filter.
int __fastcall Script_GetMapOverlays(void *L) {
    const int wmaRow = Game::Lua::IsNumber(L, 1)
                           ? Map::Area::RowForAreaID(static_cast<uint32_t>(
                                 Game::Lua::ToNumber(L, 1)))
                           : Map::Area::CurrentViewRow();
    Game::Lua::SetTop(L, 0);
    Map::Overlays::PushZoneOverlays(L, wmaRow, Map::Overlays::Filter::All);
    return 1;
}

} // namespace

// Shared overlay walk — see Overlays.h. Filters by exploration and builds
// the rich per-overlay table (texture + resolved tiles + hit rect + areaIDs
// + fileDataIDs). Uses the anonymous-namespace helpers above (same TU).
void PushZoneOverlays(void *L, int wmaRow, Filter filter) {
    Game::Lua::NewTable(L);
    if (wmaRow <= 0)
        return;

    // The player is only needed for the filtered variants.
    const uint8_t *player =
        (filter == Filter::All) ? nullptr : Unit::Identity::PlayerObject();

    const int count =
        Game::Read<int>(Offsets::VAR_WORLDMAP_OVERLAY_COUNT);
    int outIdx = 0;
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_OVERLAY_RECORDS,
                                         Offsets::VAR_WORLDMAP_OVERLAY_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr ||
            Game::Read<int>(rec, Offsets::OFF_WMO_WORLDMAP_AREA) != wmaRow)
            continue;

        if (filter != Filter::All) {
            const bool explored = IsOverlayExplored(rec, player);
            if (filter == Filter::Explored && !explored)
                continue;
            if (filter == Filter::Unexplored && explored)
                continue;
        }

        const auto fieldInt = [rec](uintptr_t off) {
            return *reinterpret_cast<const int32_t *>(rec + off);
        };
        // Texture placement + tile grid only exist for rows that carry art.
        // A hit-rect-only row (areaID + hit rect, no texture — a clickable
        // subzone region with no distinct BLP) still gets a result row: empty
        // textureName/path, zero size, and an empty `tiles` table (r == null →
        // PushTiles emits `{}`), so geometry consumers see it while art-draw
        // loops (`ipairs(ov.tiles)`) iterate zero times. Stock 1.12 and Octo
        // both happen to give every overlay a texture, but the DBC schema
        // allows texture-less rows, so we don't assume.
        OverlaySource src{};
        src.offX = fieldInt(Offsets::OFF_WMO_OFFSET_X);
        src.offY = fieldInt(Offsets::OFF_WMO_OFFSET_Y);
        const bool hasTexture = SourceFor(rec, &src);
        const ResolvedOverlay *r = hasTexture ? CachedResolve(id, src) : nullptr;

        outIdx += 1;
        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(
            L, "textureName",
            hasTexture ? Game::Read<const char *>(rec, Offsets::OFF_WMO_TEXTURE_NAME) : "");
        Game::Lua::SetFieldString(L, "texturePath", src.basePath);
        Game::Lua::SetFieldNumber(L, "textureWidth", src.dbcW);
        Game::Lua::SetFieldNumber(L, "textureHeight", src.dbcH);
        Game::Lua::SetFieldNumber(L, "offsetX", src.offX);
        Game::Lua::SetFieldNumber(L, "offsetY", src.offY);
        Game::Lua::SetFieldNumber(
            L, "mapPointX", fieldInt(Offsets::OFF_WMO_MAP_POINT_X));
        Game::Lua::SetFieldNumber(
            L, "mapPointY", fieldInt(Offsets::OFF_WMO_MAP_POINT_Y));

        // Hit rectangle (world-map canvas px, ~1002x668) — the tight clickable
        // bounds of the landmass, present for every row (texture or not). The
        // subzone-center source: (left+right)/2 / 1002, (top+bottom)/2 / 668.
        Game::Lua::SetFieldNumber(
            L, "hitRectTop", fieldInt(Offsets::OFF_WMO_HITRECT_TOP));
        Game::Lua::SetFieldNumber(
            L, "hitRectLeft", fieldInt(Offsets::OFF_WMO_HITRECT_LEFT));
        Game::Lua::SetFieldNumber(
            L, "hitRectBottom", fieldInt(Offsets::OFF_WMO_HITRECT_BOTTOM));
        Game::Lua::SetFieldNumber(
            L, "hitRectRight", fieldInt(Offsets::OFF_WMO_HITRECT_RIGHT));

        // AreaTable ids this overlay reveals (WorldMapOverlay.dbc areaID[4]).
        // `areaID` is the primary (first nonzero — every overlay has one);
        // `areaIDs` is the full nonzero list (52 of 707 overlays span 2-3
        // subzones, e.g. a lake straddling two areas). Same table shape as
        // `tiles` — build the array, then assign the scalar.
        int firstArea = 0, aCount = 0;
        Game::Lua::PushString(L, "areaIDs");
        Game::Lua::NewTable(L);
        for (int k = 0; k < 4; ++k) {
            const int aid = fieldInt(Offsets::OFF_WMO_AREA_ID + k * 4);
            if (aid == 0)
                continue;
            if (firstArea == 0)
                firstArea = aid;
            aCount += 1;
            Game::Lua::PushNumber(L, static_cast<double>(aCount));
            Game::Lua::PushNumber(L, static_cast<double>(aid));
            Game::Lua::SetTable(L, -3);
        }
        Game::Lua::SetTable(L, -3); // overlay.areaIDs = { ... }
        Game::Lua::SetFieldNumber(L, "areaID", firstArea);

        if (r != nullptr) {
            Game::Lua::SetFieldNumber(L, "tileCols", r->cols);
            Game::Lua::SetFieldNumber(L, "tileRows", r->rows);
            Game::Lua::SetFieldBool(L, "upscaled", r->upscaled);
        }
        Game::Lua::PushString(L, "tiles");
        PushTiles(L, r, src);
        Game::Lua::SetTable(L, -3);

        // fileDataIDs — ordered tile texture paths (retail's field name for
        // the tile list). Vanilla has no numeric fileDataIDs, so these are the
        // SetTexture-ready paths; redundant with tiles[].file, provided so
        // retail-ported exploration addons reading info.fileDataIDs work,
        // while tiles[] carries the per-tile draw geometry Octo needs.
        Game::Lua::PushString(L, "fileDataIDs");
        Game::Lua::NewTable(L);
        if (r != nullptr) {
            char file[0x120];
            for (int i = 0; i < r->count; ++i) {
                std::snprintf(file, sizeof(file), "%s%d", src.basePath, i + 1);
                Game::Lua::PushNumber(L, static_cast<double>(i + 1));
                Game::Lua::PushString(L, file);
                Game::Lua::SetTable(L, -3);
            }
        }
        Game::Lua::SetTable(L, -3); // overlay.fileDataIDs = { ... }

        Game::Lua::SetTable(L, -3);
    }
}

namespace {
void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Map", "GetMapOverlays",
                                     &Script_GetMapOverlays);
}
const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
} // namespace

} // namespace Map::Overlays
