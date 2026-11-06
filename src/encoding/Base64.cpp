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

// `C_EncodingUtil.EncodeBase64` / `DecodeBase64`, with `Base64Variant`
// parity. Modern WoW's enum:
//
//   Enum.Base64Variant = { Standard = 0, UrlSafe = 1 }
//
// - Standard (RFC 4648 §4): `+`/`/` alphabet, `=` padding required.
// - UrlSafe (RFC 4648 §5): `-`/`_` alphabet, `=` padding omitted on
//   encode but tolerated on decode.

#include "Game.h"

#include <cstdint>
#include <vector>

namespace Encoding::Base64 {

namespace {

enum class Variant : int {
    Standard = 0,
    UrlSafe = 1,
};

constexpr const char kStandardAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr const char kUrlSafeAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

const char *AlphabetFor(Variant v) {
    return v == Variant::UrlSafe ? kUrlSafeAlphabet : kStandardAlphabet;
}

// Decode a single base64 character to its 6-bit value, or -1 if it's
// not in the requested alphabet (or not in either, when `accept` is
// nullopt). `strict` toggles whether to require the variant's exact
// alphabet vs accept both — used to give the no-variant decode form
// lenient input handling.
int DecodeChar(char c, Variant v, bool strict) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + (c - 'a');
    if (c >= '0' && c <= '9') return 52 + (c - '0');
    if (c == '+') return (strict && v != Variant::Standard) ? -1 : 62;
    if (c == '-') return (strict && v != Variant::UrlSafe)  ? -1 : 62;
    if (c == '/') return (strict && v != Variant::Standard) ? -1 : 63;
    if (c == '_') return (strict && v != Variant::UrlSafe)  ? -1 : 63;
    return -1;
}

int OptionalVariantArg(void *L, int idx, Variant def, bool &outProvided) {
    outProvided = false;
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_NUMBER)
        return static_cast<int>(def);
    outProvided = true;
    return static_cast<int>(Game::Lua::ToNumber(L, idx));
}

int __fastcall Script_EncodeBase64(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_EncodingUtil.EncodeBase64(data [, variant])");
        return 0;
    }
    const auto *src = reinterpret_cast<const uint8_t *>(
        Game::Lua::ToString(L, 1));
    const unsigned int len = Game::Lua::StrLen(L, 1);
    if (src == nullptr || len == 0) {
        Game::Lua::PushString(L, "");
        return 1;
    }

    bool _;
    const int variantInt = OptionalVariantArg(L, 2, Variant::Standard, _);
    if (variantInt < 0 || variantInt > 1) {
        Game::Lua::Error(L,
            "C_EncodingUtil.EncodeBase64: invalid Base64Variant");
        return 0;
    }
    const Variant variant = static_cast<Variant>(variantInt);
    const char *alphabet = AlphabetFor(variant);
    const bool padded = (variant == Variant::Standard);

    std::vector<char> out;
    out.reserve(((len + 2) / 3) * 4);
    unsigned int i = 0;
    for (; i + 3 <= len; i += 3) {
        const uint32_t v = (static_cast<uint32_t>(src[i]) << 16) |
                           (static_cast<uint32_t>(src[i + 1]) << 8) |
                           static_cast<uint32_t>(src[i + 2]);
        out.push_back(alphabet[(v >> 18) & 0x3F]);
        out.push_back(alphabet[(v >> 12) & 0x3F]);
        out.push_back(alphabet[(v >> 6) & 0x3F]);
        out.push_back(alphabet[v & 0x3F]);
    }
    const unsigned int rem = len - i;
    if (rem == 1) {
        const uint32_t v = static_cast<uint32_t>(src[i]) << 16;
        out.push_back(alphabet[(v >> 18) & 0x3F]);
        out.push_back(alphabet[(v >> 12) & 0x3F]);
        if (padded) { out.push_back('='); out.push_back('='); }
    } else if (rem == 2) {
        const uint32_t v = (static_cast<uint32_t>(src[i]) << 16) |
                           (static_cast<uint32_t>(src[i + 1]) << 8);
        out.push_back(alphabet[(v >> 18) & 0x3F]);
        out.push_back(alphabet[(v >> 12) & 0x3F]);
        out.push_back(alphabet[(v >> 6) & 0x3F]);
        if (padded) out.push_back('=');
    }

    Game::Lua::PushLString(L, out.data(),
                           static_cast<unsigned int>(out.size()));
    return 1;
}

int __fastcall Script_DecodeBase64(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_EncodingUtil.DecodeBase64(data [, variant])");
        return 0;
    }
    const char *src = Game::Lua::ToString(L, 1);
    const unsigned int len = Game::Lua::StrLen(L, 1);
    if (src == nullptr || len == 0) {
        Game::Lua::PushString(L, "");
        return 1;
    }

    bool variantProvided;
    const int variantInt = OptionalVariantArg(L, 2, Variant::Standard,
                                              variantProvided);
    if (variantInt < 0 || variantInt > 1) {
        Game::Lua::Error(L,
            "C_EncodingUtil.DecodeBase64: invalid Base64Variant");
        return 0;
    }
    const Variant variant = static_cast<Variant>(variantInt);

    // Strict when the caller named a variant; lenient (accept both
    // alphabets, optional padding) when no variant was passed.
    const bool strict = variantProvided;

    // Strip a trailing run of `=` to normalize. Standard variant must
    // have produced length-multiple-of-4 before strip; UrlSafe is
    // allowed unpadded.
    unsigned int effective = len;
    while (effective > 0 && src[effective - 1] == '=')
        --effective;
    const unsigned int padCount = len - effective;

    if (strict && variant == Variant::Standard) {
        if ((len & 3) != 0)
            return 0; // Standard requires length multiple of 4
        if (padCount > 2)
            return 0;
    } else {
        if (padCount > 2)
            return 0;
    }

    std::vector<char> out;
    out.reserve((effective / 4) * 3 + 3);

    unsigned int i = 0;
    for (; i + 4 <= effective; i += 4) {
        const int v0 = DecodeChar(src[i],     variant, strict);
        const int v1 = DecodeChar(src[i + 1], variant, strict);
        const int v2 = DecodeChar(src[i + 2], variant, strict);
        const int v3 = DecodeChar(src[i + 3], variant, strict);
        if ((v0 | v1 | v2 | v3) < 0)
            return 0;
        out.push_back(static_cast<char>((v0 << 2) | (v1 >> 4)));
        out.push_back(static_cast<char>((v1 << 4) | (v2 >> 2)));
        out.push_back(static_cast<char>((v2 << 6) | v3));
    }
    // Tail: 0, 2, or 3 unpadded characters.
    const unsigned int tail = effective - i;
    if (tail == 1)
        return 0; // never valid
    if (tail >= 2) {
        const int v0 = DecodeChar(src[i],     variant, strict);
        const int v1 = DecodeChar(src[i + 1], variant, strict);
        if ((v0 | v1) < 0)
            return 0;
        out.push_back(static_cast<char>((v0 << 2) | (v1 >> 4)));
        if (tail == 3) {
            const int v2 = DecodeChar(src[i + 2], variant, strict);
            if (v2 < 0)
                return 0;
            out.push_back(static_cast<char>((v1 << 4) | (v2 >> 2)));
        }
    }

    Game::Lua::PushLString(L, out.data(),
                           static_cast<unsigned int>(out.size()));
    return 1;
}

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "EncodeBase64",
                                     &Script_EncodeBase64);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DecodeBase64",
                                     &Script_DecodeBase64);

    static const Game::Lua::EnumIntegerEntry kBase64Variant[] = {
        {"Standard", static_cast<int>(Variant::Standard)},
        {"UrlSafe",  static_cast<int>(Variant::UrlSafe)},
    };
    Game::Lua::RegisterIntegerEnum("Enum", "Base64Variant", kBase64Variant);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Encoding::Base64
