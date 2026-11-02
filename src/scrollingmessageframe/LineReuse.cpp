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

// The lines a layout keeps on screen keep their font strings.
//
// The line layout (FUN_SMF_DISPLAY_REFRESH) shows record (position - i) in
// line i, bottom up, and sets each line's text through ShowLine (0x00788AF0).
// When the position moves - a message at the bottom, a scroll - every line
// gets the text the line below or above it had: a copy, a freed built string,
// a new measure and a new shaping each. Retail's mixin and 4.3.4 do the same.
//
// Before a layout of a shown frame, the line entries are turned by the
// distance the position moves, so each line that stays on screen is the font
// string that already holds its text. The layout then runs as it is. Its
// SetText (0x00771D80) returns at its compare when the text is the one the
// line holds, so the line keeps its built string and its measured height;
// SetVertexColor skips a repeated colour; and its forced Resize moves the
// built string with the line's rectangle (the rect callback 0x00772D40
// rebuilds only when the size changes). Only the lines that come round get a
// new text. The hyperlink buttons are rebuilt from the lines after every
// layout, and the inline textures are anchored to their line, so both follow.
//
// The entries move as whole 0x10-byte values, as the engine's own array grow
// (0x00789060) moves them with SMemReAlloc. Turning the array keeps every
// line anchored on the line below it but two: the new line 0, which was on
// the line below it, goes on the frame, and the old line 0, which was on the
// frame, goes on the line below it now. Line 0 is anchored first, so no line
// is ever anchored to one that depends on it. Both are anchored as the layout
// anchors a new line, in the frame's insert mode.
//
// The distance is the layout's record minus the scroll record, the record
// line 0 shows. That holds after a layout of a shown frame, which counts the
// lines it places; a layout of a hidden frame counts none, so a frame whose
// count is 0 is laid out as before. A distance as large as the lines is no
// reuse at all. A wrong distance costs only the reuse: the layout sets every
// text it shows either way.

#include "Game.h"
#include "Offsets.h"
#include "scrollingmessageframe/InsertMode.h"
#include "scrollingmessageframe/Layout.h"

#include <algorithm>
#include <cstdint>

namespace ScrollingMessageFrame::LineReuse {

namespace {

void BeforeLayout(void *smf, int newestRecord) {
    struct Line {
        uint8_t bytes[Offsets::SMF_LINE_STRIDE];
    };
    if (Game::Read<int>(smf, Offsets::OFF_FRAME_VISIBLE) == 0 ||
        Game::Read<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_LINES_SHOWN) <= 0 ||
        Game::Read<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_MESSAGE_COUNT) <= 0)
        return;
    const int lines = Game::Read<int>(smf, Offsets::OFF_SMF_LINE_COUNT);
    const int max = Game::Read<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_MAX_MESSAGES);
    if (lines <= 1 || max <= 0)
        return;
    // The ring wraps at the limit; the shorter way round is the distance.
    int distance = newestRecord - Game::Read<int>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_SCROLL_RECORD);
    if (2 * distance > max)
        distance -= max;
    else if (2 * distance < -max)
        distance += max;
    if (distance == 0 || distance >= lines || distance <= -lines)
        return;
    // Line i takes the entry line (i - distance) had.
    const int turn = (distance + lines) % lines;
    auto *entries = Game::Read<Line *>(smf, Offsets::OFF_SMF_LINE_ARRAY);
    std::rotate(entries, entries + (lines - turn), entries + lines);
    InsertMode::AnchorLine(smf, 0);
    InsertMode::AnchorLine(smf, turn);
}

const ScrollingMessageFrame::Layout::AutoSubscribeBefore _layoutSub{&BeforeLayout};

} // namespace

} // namespace ScrollingMessageFrame::LineReuse
