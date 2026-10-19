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

// The edit box's class table, read with the escapes its text is drawn with.
//
// Every caret move, delete, click and character count walks the class table
// (OFF_EDITBOX_CHAR_CLASS): one dword per token of the text, built by
// FUN_EDITBOX_REBUILD_CHAR_CLASS with the engine's tokenizer. The rebuild
// passes the tokenizer a decoder flag word of 0, so it reads every escape,
// while the box's font string draws and measures the same text with the
// decoder flags built from its own flags. Those differ in one case: a
// single-line box turns |n off (font string flag 0x4000, decoder 0x200).
// There the table reads "|n" as one line-break token while the box draws the
// two characters `|` and `n`, so the caret steps over both at once and sits
// one glyph off after it (benilla-re ui/editboxmarkup "Faults"). 4.3.4 builds
// its table the same way and has the same fault.
//
// The rebuild below is the engine's, with the decoder flag word its font
// string uses, so the table and the drawn text agree.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace EditBox::CharClass {

namespace {

constexpr uint32_t kClassLinkStart = 4;
constexpr uint32_t kClassLinkStop = 5;
constexpr uint32_t kInsideLink = 0x80000000u;

using Tokenize_t = uint32_t(__fastcall *)(const char *text, int *length, uint32_t *color, uint32_t decoderFlags,
                                          uint32_t *payload);
using Rebuild_t = void(__fastcall *)(void *editBox);
Rebuild_t g_origRebuild = nullptr;

// The escape bits of the decoder flag word a font string builds from its
// flags (0x0044D6E0, 0x0044D6EA, 0x0044D6F3, 0x0044D6FC).
uint32_t DecoderEscapeFlags(const void *fontString) {
    const uint32_t flags = Game::Read<uint32_t>(fontString, Offsets::OFF_FONTSTRING_FLAGS);
    uint32_t decoder = 0;
    if (flags & 0x2000)
        decoder |= 0x100;
    if (flags & 0x4000)
        decoder |= 0x200;
    if (flags & 0x8000)
        decoder |= 0x400;
    if (flags & 0x0080)
        decoder |= 0x800;
    return decoder;
}

// FUN_EDITBOX_REBUILD_CHAR_CLASS: zero the table, then store at each token's
// first byte `length | class << 16`, with bit 31 from a link's start through
// its stop.
void __fastcall Rebuild_h(void *editBox) {
    const void *fontString = Game::Read<void *>(editBox, Offsets::OFF_EDITBOX_FONTSTRING);
    const uint32_t decoderFlags = fontString != nullptr ? DecoderEscapeFlags(fontString) : 0;
    uint32_t *table = Game::Read<uint32_t *>(editBox, Offsets::OFF_EDITBOX_CHAR_CLASS);
    const uint32_t size = Game::Read<uint32_t>(editBox, Offsets::OFF_EDITBOX_CHAR_CLASS_SIZE);
    std::memset(table, 0, size * sizeof(uint32_t));
    const char *text = Game::Read<const char *>(editBox, Offsets::OFF_EDITBOX_TEXT);
    uint32_t link = 0;
    for (uint32_t at = 0; text != nullptr && *text != '\0';) {
        int length = 0;
        uint32_t payload = 0;
        const uint32_t cls =
            reinterpret_cast<Tokenize_t>(Offsets::FUN_TEXT_TOKENIZER)(text, &length, nullptr, decoderFlags, &payload);
        if (cls == kClassLinkStart)
            link |= kInsideLink;
        table[at] = (static_cast<uint32_t>(length) & 0xFFFF) | (cls << 16) | link;
        at += length;
        text += length;
        if (cls == kClassLinkStop)
            link &= ~kInsideLink;
    }
}

const Game::HookAutoRegister _hook{Offsets::FUN_EDITBOX_REBUILD_CHAR_CLASS, reinterpret_cast<void *>(&Rebuild_h),
                                   reinterpret_cast<void **>(&g_origRebuild)};

} // namespace

} // namespace EditBox::CharClass
