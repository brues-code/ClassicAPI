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

// `C_EncodingUtil.EncodeHex` / `DecodeHex`, as the 3.80.2 client does them
// (EncodeHex body 0x140C1E8C0; DecodeHex body 0x140C1DCD0, decoder
// 0x140337330).
//
// EncodeHex writes two lowercase digits per byte of a binary-safe source.
// DecodeHex reads its source as a C string, so it ends at the first NUL;
// an odd length raises the error, and a character that is not a hex digit
// ends the decode while the output keeps its full length (half the source),
// the rest left as zero bytes.

#include "Game.h"
#include "encoding/Args.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace Encoding::Hex {

namespace {

constexpr const char *kEncodeUsage = "Usage: local output = C_EncodingUtil.EncodeHex(source)";
constexpr const char *kDecodeUsage = "Usage: local output = C_EncodingUtil.DecodeHex(source)";

int Nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int __fastcall Script_EncodeHex(void *L) {
    const uint8_t *src;
    std::size_t len;
    if (!Args::Source(L, 1, kEncodeUsage, &src, &len))
        return 0;
    static const char kDigits[] = "0123456789abcdef";
    std::string out(len * 2, '\0');
    for (std::size_t i = 0; i < len; ++i) {
        out[i * 2] = kDigits[src[i] >> 4];
        out[i * 2 + 1] = kDigits[src[i] & 0xF];
    }
    Game::Lua::PushLString(L, out.data(), static_cast<unsigned int>(out.size()));
    return 1;
}

int __fastcall Script_DecodeHex(void *L) {
    if (Game::Lua::Type(L, 1) <= Game::Lua::TYPE_NIL || !Game::Lua::IsString(L, 1)) {
        Game::Lua::ArgError(L, 1, kDecodeUsage);
        return 0;
    }
    const char *src = Game::Lua::ToString(L, 1);
    const std::size_t len = std::strlen(src);
    if (len % 2 != 0) {
        Args::Raise(L, "DecodeHex", "attempted to decode a string that does not have an even number of bytes");
        return 0;
    }
    std::string out(len / 2, '\0');
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = Nibble(src[i * 2]);
        const int lo = Nibble(src[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            break;
        out[i] = static_cast<char>((hi << 4) | lo);
    }
    Game::Lua::PushLString(L, out.data(), static_cast<unsigned int>(out.size()));
    return 1;
}

const Game::Doc::Field kEncodeArgs[] = {
    Game::Doc::Req("source", "stringView"),
};
const Game::Doc::Field kEncodeRets[] = {
    Game::Doc::Req("output", "stringView", "Two lowercase hex digits per byte."),
};
const Game::Doc::Function kEncodeHex{"Encodes a string as hexadecimal.", kEncodeArgs, kEncodeRets};

const Game::Doc::Field kDecodeArgs[] = {
    Game::Doc::Req("source", "string",
                   "Hex digits in either case. The source ends at its first NUL byte. An odd "
                   "number of characters is an error."),
};
const Game::Doc::Field kDecodeRets[] = {
    Game::Doc::Req("output", "stringView",
                   "Half as long as the source. When the source has a character that is not a "
                   "hex digit, the bytes from that position are zero."),
};
const Game::Doc::Function kDecodeHex{"Decodes a hexadecimal string.", kDecodeArgs, kDecodeRets};

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "EncodeHex", &Script_EncodeHex, &kEncodeHex);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DecodeHex", &Script_DecodeHex, &kDecodeHex);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Encoding::Hex
