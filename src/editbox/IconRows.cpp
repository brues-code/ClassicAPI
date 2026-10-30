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

// Tall inline icons move the edit box's rows.
//
// A multi-line edit box places its caret, its selection pieces and a click's
// cursor by rows of one height — the label's font height plus its spacing,
// pixel-aligned (FUN_EDITBOX_PLACE_CARET, FUN_EDITBOX_PLACE_SELECTION_PIECE,
// FUN_EDITBOX_INDEX_AT_POINT; 4.3.4's UpdateVisibleCursor, UpdateHighlightArea
// and ConvertCoordinateToIndex step the same rows). The label's text leaves
// those rows once a line holds an icon taller than the font: the render moves
// that line and every line after it down by the overflow (4.3.4's
// InitializeText; the emitter co-hook in Text::InlineTexture), and the box
// grows by the same amount through the string-height co-hook. So below the
// first tall-icon line the caret and the highlight sat above their text by
// the summed overflow, and OnCursorChanged reported that y, which
// ScrollingEditBoxMixin:ScrollCursorIntoView scrolls by — pfUI's chat copy
// box with faction-emblem icons. 4.3.4 has the same fault; nothing it ships
// puts a tall icon in an edit box.
//
// Each placer keeps its engine code and gets the move the render makes, from
// Text::InlineTexture::LineIconShift over the box's own line table (the
// breaks the render lays out), split by the label's vertical justify as the
// emitter splits it:
//   - the click: its y is moved onto the uniform row that the icon-aware row
//     under it has, before the engine maps it;
//   - a selection piece: re-anchored after the engine placed it, with the
//     engine's own x, lower by the move;
//   - the caret: the same re-anchor, then OnCursorChanged fired once with the
//     moved y through the engine's own forwarder. The engine's fire is muted
//     for the call by emptying the script slot: it reads the row y before
//     anything can correct it.
// A single-line box has one row and needs nothing: its label is justified
// MIDDLE in a rect of fixed height, so its text stays centred on the caret.

#include "Game.h"
#include "Offsets.h"
#include "text/InlineTexture.h"

#include <cstdint>

namespace EditBox::IconRows {

namespace {

using PlaceCaret_t = void(__fastcall *)(void *editBox);
using PlaceSelectionPiece_t = void(__fastcall *)(void *editBox, void *edx, void *piece, int from, int to);
using IndexAtPoint_t = void(__fastcall *)(void *editBox, void *edx, float x, float y, int *index);
using FontHeight_t = float(__fastcall *)(void *fs, void *edx, int mode);
using MeasureRun_t = float(__fastcall *)(void *fs, void *edx, const uint8_t *text, int len);
using PixelAlignY_t = float(__stdcall *)(float length);
using GetRect_t = int(__fastcall *)(void *anchor, void *edx, float *out);
using ClearAllPoints_t = void(__fastcall *)(void *anchor);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint, float x, float y,
                                      int flag);
using Resize_t = void(__fastcall *)(void *anchor, void *edx, int force);
using Extent_t = float(__fastcall *)(void *anchor, void *edx);
using ScreenAspect_t = float(__cdecl *)();
using DivideByScreenScaleX_t = float(__stdcall *)(float value);
using FireVariadic_t = void(__cdecl *)(void *frame, uint32_t *slotPtr, const char *fmt, ...);

PlaceCaret_t g_origPlaceCaret = nullptr;
PlaceSelectionPiece_t g_origPlaceSelectionPiece = nullptr;
IndexAtPoint_t g_origIndexAtPoint = nullptr;

void *AnchorOf(void *region) { return static_cast<uint8_t *>(region) + Offsets::OFF_REGION_ANCHOR; }

// A multi-line box's rows: its label, the buffer the line table indexes (the
// '*' copy of a password box) and the table — each line's first byte, the
// last entry the end.
struct Rows {
    void *fs = nullptr;
    const uint8_t *text = nullptr;
    const uint32_t *starts = nullptr;
    int count = 0;      // entries; lines = count - 1
    float scale = 1.0f; // the box's OFF_LAYOUT_SCALE
};

bool MultiLineRows(void *editBox, Rows &rows) {
    const uint8_t mode = Game::Read<uint8_t>(editBox, Offsets::OFF_EDITBOX_MODE_FLAGS);
    if ((mode & Offsets::EDITBOX_MODE_MULTILINE) == 0)
        return false;
    rows.fs = Game::Read<void *>(editBox, Offsets::OFF_EDITBOX_FONTSTRING);
    rows.text = Game::Read<const uint8_t *>(editBox, (mode & Offsets::EDITBOX_MODE_PASSWORD)
                                                         ? Offsets::OFF_EDITBOX_TEXT_MASKED
                                                         : Offsets::OFF_EDITBOX_TEXT);
    rows.starts = Game::Read<const uint32_t *>(editBox, Offsets::OFF_EDITBOX_LINE_STARTS);
    rows.count = Game::Read<int>(editBox, Offsets::OFF_EDITBOX_LINE_COUNT);
    rows.scale = Game::Read<float>(editBox, Offsets::OFF_LAYOUT_SCALE);
    return rows.fs != nullptr && rows.text != nullptr && rows.starts != nullptr && rows.count >= 2 &&
           rows.scale > 0.0f;
}

// The line holding byte `pos` — the placers' walk down the table.
int LineOf(const Rows &rows, uint32_t pos) {
    int line = 0;
    while (pos >= rows.starts[line + 1]) {
        if (line >= rows.count - 2)
            break;
        ++line;
    }
    return line;
}

bool LineShift(const Rows &rows, int line, float &before, float &after) {
    before = 0.0f;
    after = 0.0f;
    const uint32_t start = rows.starts[line];
    const uint32_t end = rows.starts[line + 1];
    return end > start &&
           Text::InlineTexture::LineIconShift(rows.fs, rows.text + start, static_cast<int>(end - start), before, after);
}

// How far the render moves line `line`'s text below its uniform row: the
// moves of every line above it, and its own text's move.
float ShiftOf(const Rows &rows, int line) {
    float shift = 0.0f;
    for (int i = 0; i <= line; ++i) {
        float before = 0.0f;
        float after = 0.0f;
        if (!LineShift(rows, i, before, after))
            continue;
        shift += before;
        if (i < line)
            shift += after;
    }
    return shift;
}

// The placers' row in the box's parent units: the label's font height and
// spacing, each pixel-aligned at the box's scale (PlaceCaret
// 0x0077DAEB..0x0077DB0E).
float RowHeight(const Rows &rows) {
    const float fontH = reinterpret_cast<FontHeight_t>(Offsets::FUN_FONTSTRING_FONT_HEIGHT)(rows.fs, nullptr, 1);
    const float spacing = Game::Read<float>(rows.fs, Offsets::OFF_FONTSTRING_SPACING);
    const auto align = reinterpret_cast<PixelAlignY_t>(Offsets::FUN_PIXEL_ALIGN_Y);
    return align(fontH * rows.scale) + align(spacing * rows.scale);
}

float MeasureRun(const Rows &rows, uint32_t from, uint32_t to) {
    return reinterpret_cast<MeasureRun_t>(Offsets::FUN_FONTSTRING_MEASURE_SUBSTRING)(rows.fs, nullptr, rows.text + from,
                                                                                  static_cast<int>(to - from));
}

// The x the placers anchor byte `pos` of `line` at, and the label point they
// anchor to, by the label's horizontal justify (OFF_FONTSTRING_FLAGS & 7).
// False for none, which anchors nothing.
bool AnchorX(const Rows &rows, int line, uint32_t pos, float &x, int &relPoint) {
    const uint32_t start = rows.starts[line];
    const uint32_t end = rows.starts[line + 1];
    switch (Game::Read<uint32_t>(rows.fs, Offsets::OFF_FONTSTRING_FLAGS) & 7u) {
    case 1: // LEFT
        x = (pos == start) ? 0.0f : MeasureRun(rows, start, pos);
        relPoint = Offsets::FRAMEPOINT_TOPLEFT;
        return true;
    case 2: // CENTER
        x = (pos == start) ? 0.0f : MeasureRun(rows, start, pos);
        x -= MeasureRun(rows, start, end) * 0.5f;
        relPoint = Offsets::FRAMEPOINT_TOP;
        return true;
    case 4: // RIGHT
        x = (pos == end) ? 0.0f : -MeasureRun(rows, pos, end);
        relPoint = Offsets::FRAMEPOINT_TOPRIGHT;
        return true;
    default:
        return false;
    }
}

// Anchors `region` as the placer did, at `y`.
void Anchor(const Rows &rows, void *region, int relPoint, float x, float y, int flag) {
    void *anchor = AnchorOf(region);
    reinterpret_cast<ClearAllPoints_t>(Offsets::FUN_LAYOUT_CLEAR_ALL_POINTS)(anchor);
    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(anchor, Offsets::FRAMEPOINT_TOPLEFT, AnchorOf(rows.fs),
                                                                relPoint, x, y, flag);
}

// --- the click ---------------------------------------------------------------

void __fastcall IndexAtPoint_h(void *editBox, void *edx, float x, float y, int *index) {
    Rows rows;
    if (!MultiLineRows(editBox, rows))
        return g_origIndexAtPoint(editBox, edx, x, y, index);
    float rect[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // bottom, left, top, right
    if (reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(AnchorOf(rows.fs), nullptr, rect) == 0)
        return g_origIndexAtPoint(editBox, edx, x, y, index);
    // The engine walks rows down from the label's top while the click is
    // below the row (strictly), stopping at the last line. Walk the rows as
    // the render lays them out: each taller by its line's moves.
    const float row = RowHeight(rows);
    const float below = rect[2] - y;
    int line = 0;
    float top = 0.0f; // the icon-aware top of `line`, below the label's top
    bool moved = false;
    for (;;) {
        float before = 0.0f;
        float after = 0.0f;
        if (LineShift(rows, line, before, after))
            moved = true;
        const float height = row + (before + after) * rows.scale;
        if (!(below > top + height) || line >= rows.count - 2)
            break;
        top += height;
        ++line;
    }
    // Without a move the rows are the engine's own; with one, the click is
    // moved onto the middle of the uniform row the engine maps to `line`.
    const float yMapped = moved ? rect[2] - (static_cast<float>(line) + 0.5f) * row : y;
    g_origIndexAtPoint(editBox, edx, x, yMapped, index);
}

// --- a selection piece --------------------------------------------------------

void __fastcall PlaceSelectionPiece_h(void *editBox, void *edx, void *piece, int from, int to) {
    Rows rows;
    if (!MultiLineRows(editBox, rows) || from < 0)
        return g_origPlaceSelectionPiece(editBox, edx, piece, from, to);
    const int line = LineOf(rows, static_cast<uint32_t>(from));
    const float shift = ShiftOf(rows, line);
    g_origPlaceSelectionPiece(editBox, edx, piece, from, to);
    if (!(shift > 0.0f) || piece == nullptr)
        return;
    // The piece starts at `from`, clamped to its line.
    uint32_t pos = static_cast<uint32_t>(from);
    if (pos < rows.starts[line])
        pos = rows.starts[line];
    float x = 0.0f;
    int relPoint = 0;
    if (!AnchorX(rows, line, pos, x, relPoint))
        return;
    const float y = -(RowHeight(rows) / rows.scale) * static_cast<float>(line) - shift;
    Anchor(rows, piece, relPoint, x, y, 0);
    reinterpret_cast<Resize_t>(Offsets::FUN_LAYOUT_RESIZE)(AnchorOf(piece), nullptr, 1);
}

// --- the caret ----------------------------------------------------------------

void __fastcall PlaceCaret_h(void *editBox) {
    Rows rows;
    if (!MultiLineRows(editBox, rows))
        return g_origPlaceCaret(editBox);
    const int cursor = Game::Read<int>(editBox, Offsets::OFF_EDITBOX_CURSOR_BYTE);
    const int visibleStart = Game::Read<int>(editBox, Offsets::OFF_EDITBOX_VISIBLE_START);
    const int visibleLength = Game::Read<int>(editBox, Offsets::OFF_EDITBOX_VISIBLE_LENGTH);
    // Outside the window the engine hides the caret and fires nothing.
    if (cursor < visibleStart || cursor > visibleStart + visibleLength)
        return g_origPlaceCaret(editBox);
    const int line = LineOf(rows, static_cast<uint32_t>(cursor));
    const float shift = ShiftOf(rows, line);
    if (!(shift > 0.0f))
        return g_origPlaceCaret(editBox);
    uint32_t *slot = Game::Ptr<uint32_t>(editBox, Offsets::OFF_EDITBOX_ONCURSORCHANGED_SLOT);
    const uint32_t handler = slot[0];
    slot[0] = 0;
    g_origPlaceCaret(editBox);
    slot[0] = handler;
    void *caret = Game::Read<void *>(editBox, Offsets::OFF_EDITBOX_CARET);
    if (caret == nullptr)
        return;
    const float y = -(RowHeight(rows) / rows.scale) * static_cast<float>(line) - shift;
    float x = 0.0f;
    int relPoint = 0;
    if (AnchorX(rows, line, static_cast<uint32_t>(cursor), x, relPoint))
        Anchor(rows, caret, relPoint, x, y, 1);
    if (handler == 0)
        return;
    // OnCursorChanged(x, y, width, height) as the engine fires it: each
    // value by the anchor→script factor, pushed as a double.
    void *anchor = AnchorOf(caret);
    void **vtable = *static_cast<void ***>(anchor);
    const float width = reinterpret_cast<Extent_t>(vtable[7])(anchor, nullptr);  // +0x1C GetWidth
    const float height = reinterpret_cast<Extent_t>(vtable[8])(anchor, nullptr); // +0x20 GetHeight
    const auto aspect = reinterpret_cast<ScreenAspect_t>(Offsets::FUN_COORD_GET_SCALED_SCREEN_ASPECT);
    const auto divide = reinterpret_cast<DivideByScreenScaleX_t>(Offsets::FUN_COORD_DIVIDE_BY_SCREEN_SCALE_X);
    const float factor = Game::Read<float>(Offsets::FLOAT_SCRIPT_COORD_SCALE);
    const auto push = [&](float value) { return static_cast<double>(divide(aspect() * factor * value)); };
    const double args[4] = {push(x), push(y), push(width), push(height)};
    void *L = Game::Lua::State();
    const int savedTop = (L != nullptr) ? Game::Lua::GetTop(L) : 0;
    reinterpret_cast<FireVariadic_t>(Offsets::FUN_FRAME_RUN_SCRIPT_VARIADIC)(
        editBox, slot, reinterpret_cast<const char *>(Offsets::VAR_SCRIPT_FMT_FFFF), args[0], args[1], args[2],
        args[3]);
    if (L != nullptr)
        Game::Lua::SetTop(L, savedTop);
}

const Game::HookAutoRegister _caretHook{Offsets::FUN_EDITBOX_PLACE_CARET, reinterpret_cast<void *>(&PlaceCaret_h),
                                        reinterpret_cast<void **>(&g_origPlaceCaret)};
const Game::HookAutoRegister _pieceHook{Offsets::FUN_EDITBOX_PLACE_SELECTION_PIECE,
                                        reinterpret_cast<void *>(&PlaceSelectionPiece_h),
                                        reinterpret_cast<void **>(&g_origPlaceSelectionPiece)};
const Game::HookAutoRegister _clickHook{Offsets::FUN_EDITBOX_INDEX_AT_POINT,
                                        reinterpret_cast<void *>(&IndexAtPoint_h),
                                        reinterpret_cast<void **>(&g_origIndexAtPoint)};

} // namespace

} // namespace EditBox::IconRows
