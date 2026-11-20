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

// `C_EncodingUtil.EncodeBase64` / `DecodeBase64`, as the 3.80.2 client does
// them (EncodeBase64 0x140C1E490, core 0x142B87970; DecodeBase64 0x140C1D850,
// core 0x142B876C0).
//
// Encoding pads with `=` in both variants. Decoding never fails and ignores
// the variant: `+` or `-` is 62 and `/` or `_` is 63, any other character
// outside the alphabet is skipped, the first `=` ends the decode (writing the
// one or two bytes its pending group holds), a group left without `=` at the
// end is dropped, and a source whose first byte is NUL or a line feed
// decodes to nothing.

#include "Game.h"
#include "encoding/Args.h"

#include <cstdint>
#include <string>

namespace Encoding::Base64 {

namespace {

enum Variant : int {
    VARIANT_STANDARD = 0,
    VARIANT_URL_SAFE = 1,
};

constexpr const char kStandardAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr const char kUrlSafeAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

constexpr const char *kEncodeUsage = "Usage: local output = C_EncodingUtil.EncodeBase64(source [, variant])";
constexpr const char *kDecodeUsage = "Usage: local output = C_EncodingUtil.DecodeBase64(source [, variant])";

std::string Encode(const uint8_t *src, std::size_t len, const char *alphabet) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const uint32_t v = (src[i] << 16) | (src[i + 1] << 8) | src[i + 2];
        out += alphabet[(v >> 18) & 0x3F];
        out += alphabet[(v >> 12) & 0x3F];
        out += alphabet[(v >> 6) & 0x3F];
        out += alphabet[v & 0x3F];
    }
    if (i < len) {
        const uint32_t v = (src[i] << 16) | (i + 1 < len ? src[i + 1] << 8 : 0);
        out += alphabet[(v >> 18) & 0x3F];
        out += alphabet[(v >> 12) & 0x3F];
        out += i + 1 < len ? alphabet[(v >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

// The 6-bit value of a Base64 character in either alphabet, or -1.
int Sextet(uint8_t c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+' || c == '-')
        return 62;
    if (c == '/' || c == '_')
        return 63;
    return -1;
}

std::string Decode(const uint8_t *src, std::size_t len) {
    std::string out;
    if (len == 0 || src[0] == '\0' || src[0] == '\n')
        return out;
    out.reserve(len / 4 * 3);
    uint8_t group[4];
    int pending = 0;
    for (std::size_t i = 0; i < len; ++i) {
        if (src[i] == '=') {
            if (pending >= 2)
                out += static_cast<char>((group[0] << 2) | ((group[1] >> 4) & 3));
            if (pending == 3)
                out += static_cast<char>((group[1] << 4) | ((group[2] >> 2) & 0xF));
            return out;
        }
        const int v = Sextet(src[i]);
        if (v < 0)
            continue;
        group[pending++] = static_cast<uint8_t>(v);
        if (pending == 4) {
            out += static_cast<char>((group[0] << 2) | ((group[1] >> 4) & 3));
            out += static_cast<char>((group[1] << 4) | ((group[2] >> 2) & 0xF));
            out += static_cast<char>((group[2] << 6) | (group[3] & 0x3F));
            pending = 0;
        }
    }
    return out;
}

int __fastcall Script_EncodeBase64(void *L) {
    const uint8_t *src;
    std::size_t len;
    int variant = VARIANT_STANDARD;
    if (!Args::Source(L, 1, kEncodeUsage, &src, &len) ||
        !Args::Enum(L, 2, VARIANT_URL_SAFE, kEncodeUsage, &variant))
        return 0;
    const std::string out =
        Encode(src, len, variant == VARIANT_URL_SAFE ? kUrlSafeAlphabet : kStandardAlphabet);
    Game::Lua::PushLString(L, out.data(), static_cast<unsigned int>(out.size()));
    return 1;
}

int __fastcall Script_DecodeBase64(void *L) {
    const uint8_t *src;
    std::size_t len;
    int variant = VARIANT_STANDARD;
    if (!Args::Source(L, 1, kDecodeUsage, &src, &len) ||
        !Args::Enum(L, 2, VARIANT_URL_SAFE, kDecodeUsage, &variant))
        return 0;
    const std::string out = Decode(src, len);
    Game::Lua::PushLString(L, out.data(), static_cast<unsigned int>(out.size()));
    return 1;
}

const Game::Doc::Field kEncodeArgs[] = {
    Game::Doc::Req("source", "stringView"),
    Game::Doc::Opt("variant", "Base64Variant", "Standard"),
};
const Game::Doc::Field kEncodeRets[] = {
    Game::Doc::Req("output", "stringView", "Padded with = in both variants."),
};
const Game::Doc::Function kEncodeBase64{"Encodes a string as Base64.", kEncodeArgs, kEncodeRets};

const Game::Doc::Field kDecodeArgs[] = {
    Game::Doc::Req("source", "stringView",
                   "Characters of both alphabets are read. Other characters are skipped, and "
                   "the first = ends the source."),
    Game::Doc::Opt("variant", "Base64Variant", "Standard"),
};
const Game::Doc::Field kDecodeRets[] = {
    Game::Doc::Req("output", "stringView",
                   "Characters at the end that do not make a full group and have no = are "
                   "dropped."),
};
const Game::Doc::Function kDecodeBase64{"Decodes a Base64 string.", kDecodeArgs, kDecodeRets};

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "EncodeBase64", &Script_EncodeBase64,
                                     &kEncodeBase64);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DecodeBase64", &Script_DecodeBase64,
                                     &kDecodeBase64);

    static const Game::Lua::EnumIntegerEntry kBase64Variant[] = {
        {"Standard", VARIANT_STANDARD},
        {"StandardUrlSafe", VARIANT_URL_SAFE},
    };
    Game::Lua::RegisterIntegerEnum("Enum", "Base64Variant", kBase64Variant, "EncodingUtil");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Encoding::Base64
