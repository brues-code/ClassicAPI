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

// `length = CopyToClipboard(text [, removeMarkup])` — copy `text` to the
// Windows clipboard and return the number of bytes copied. Vanilla has no
// clipboard access at all (Blizzard never exposed one), so this is a pure
// ClassicAPI addition backed by the Win32 clipboard API.
//
//   - `text`         the string to copy (numbers are coerced, like Lua does).
//   - `removeMarkup` optional; when truthy, strip WoW UI escape sequences
//                    before copying, so the clipboard gets plain text. This is
//                    the engine's own GxuStripControlCodes with 4.3.4's copy
//                    mask — exactly what an edit box's Ctrl+C does: colours
//                    and hyperlink markers go (the visible text stays),
//                    `|T..|t` / `|A..|a` icons go (the strip co-hook in
//                    Text::InlineTexture), `|n` becomes a newline and `||` a
//                    single `|`.
//   - returns        byte length of the copied (possibly stripped) string —
//                    consistent with Lua's `#s`. 0 on an empty string or if
//                    the clipboard couldn't be opened.
//
// WoW's in-memory strings are UTF-8 here (same assumption the credential and
// TTS modules make), so we convert to UTF-16 and set CF_UNICODETEXT; Windows
// synthesizes CF_TEXT for legacy consumers.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>
#include <string>

#include <windows.h>

namespace Clipboard::Copy {

namespace {

using StripCodes_t = int(__fastcall *)(const char *text, int length, uint32_t mask, char *out,
                                       int capacity);

// The engine's strip never lengthens a text (every code it rewrites gets
// shorter), so the input length plus the terminator is enough room. It
// returns a pointer, not a count, and always terminates `out`.
std::string StripMarkup(const char *s, unsigned len) {
    std::string out(static_cast<size_t>(len) + 1, '\0');
    reinterpret_cast<StripCodes_t>(static_cast<uintptr_t>(Offsets::FUN_GXU_STRIP_CONTROL_CODES))(
        s, static_cast<int>(len), Offsets::GXU_STRIP_MASK_COPY, out.data(),
        static_cast<int>(out.size()));
    out.resize(std::strlen(out.c_str()));
    return out;
}

// Place a UTF-8 byte range on the clipboard as CF_UNICODETEXT. Returns true on
// success. Does not free the allocation on success — ownership passes to the
// clipboard.
bool SetClipboardUtf8(const char *data, int byteLen) {
    if (!OpenClipboard(nullptr))
        return false;

    bool ok = false;
    if (EmptyClipboard()) {
        // Empty string still writes a valid (empty) clipboard entry.
        const int wlen =
            byteLen > 0 ? MultiByteToWideChar(CP_UTF8, 0, data, byteLen, nullptr, 0) : 0;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (static_cast<SIZE_T>(wlen) + 1) * sizeof(wchar_t));
        if (h != nullptr) {
            auto *dst = static_cast<wchar_t *>(GlobalLock(h));
            if (dst != nullptr) {
                if (wlen > 0)
                    MultiByteToWideChar(CP_UTF8, 0, data, byteLen, dst, wlen);
                dst[wlen] = L'\0';
                GlobalUnlock(h);
                if (SetClipboardData(CF_UNICODETEXT, h) != nullptr)
                    ok = true; // clipboard owns h now
            }
            if (!ok)
                GlobalFree(h);
        }
    }

    CloseClipboard();
    return ok;
}

int __fastcall Script_CopyToClipboard(void *L) {
    if (!Game::Lua::IsString(L, 1) && !Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: CopyToClipboard(text [, removeMarkup])");
        return 0; // unreachable
    }

    const char *text = Game::Lua::ToString(L, 1);
    const unsigned len = Game::Lua::StrLen(L, 1);
    const bool removeMarkup = Game::Lua::ToBoolean(L, 2) != 0;

    int copiedLen;
    if (removeMarkup) {
        const std::string stripped = StripMarkup(text, len);
        copiedLen = static_cast<int>(stripped.size());
        SetClipboardUtf8(stripped.data(), copiedLen);
    } else {
        copiedLen = static_cast<int>(len);
        SetClipboardUtf8(text, copiedLen);
    }

    Game::Lua::PushNumber(L, static_cast<double>(copiedLen));
    return 1;
}

void RegisterFns() {
    Game::Lua::RegisterGlobalFunction("CopyToClipboard", &Script_CopyToClipboard);
}

const Game::ModuleAutoRegister _autoreg{&RegisterFns};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterFns};

} // namespace

} // namespace Clipboard::Copy
