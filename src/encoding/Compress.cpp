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

// `C_EncodingUtil.CompressString` / `DecompressString`, full
// `CompressionMethod` parity with retail. Modern WoW's enum:
//
//   Enum.CompressionMethod = { Deflate = 0, Zlib = 1, Gzip = 2 }
//
// — verified by the ordering of the `Deflate.CompressionMethod`,
// `Zlib`, `Gzip` strings in the 1.15 binary (`Source\UIUtil\
// EncodingUtil.cpp` data section). `level` is the standard zlib
// 0..9 with -1 meaning "default" (6).
//
// Format selection rides on zlib's `windowBits` argument to
// `deflateInit2_` / `inflateInit2_`:
//
//   Zlib        windowBits =  15
//   Gzip        windowBits =  15 + 16 = 31
//   Deflate     windowBits = -15           (raw, no header/checksum)
//   auto-detect windowBits =  15 + 32 = 47 (decode only; Zlib or Gzip)

#include "encoding/Compress.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace Encoding::Compress {

namespace {

// z_stream (zlib 1.2.2 layout, 56 bytes). We never read these
// fields by name from Lua; we just pass the same struct between the
// zlib calls, and read next_out / avail_out from known offsets.
struct ZStream {
    uint8_t bytes[Offsets::ZLIB_STREAM_SIZE];
};

constexpr int OFF_NEXT_IN    = 0x00;
constexpr int OFF_AVAIL_IN   = 0x04;
constexpr int OFF_TOTAL_IN   = 0x08;
constexpr int OFF_NEXT_OUT   = 0x0C;
constexpr int OFF_AVAIL_OUT  = 0x10;
constexpr int OFF_TOTAL_OUT  = 0x14;

template <typename T>
T &Field(ZStream &s, int off) {
    return *reinterpret_cast<T *>(&s.bytes[off]);
}

using deflateInit2_t = int(__fastcall *)(ZStream *strm, int level,
                                         int method, int windowBits,
                                         int memLevel, int strategy,
                                         const char *version, int streamSize);
using deflate_t = int(__fastcall *)(ZStream *strm, int flush);
using deflateEnd_t = int(__fastcall *)(ZStream *strm);
using inflateInit2_t = int(__fastcall *)(ZStream *strm, int windowBits,
                                         const char *version, int streamSize);
using inflate_t = int(__fastcall *)(ZStream *strm, int flush);
using inflateEnd_t = int(__fastcall *)(ZStream *strm);

constexpr int Z_OK         = 0;
constexpr int Z_STREAM_END = 1;
constexpr int Z_NO_FLUSH   = 0;
constexpr int Z_FINISH     = 4;

// Compression-method enum values surfaced to Lua. Matches the
// `Enum.CompressionMethod = { Deflate = 0, Zlib = 1, Gzip = 2 }`
// ordering inferred from the modern client's data section.
enum class Method : int {
    Deflate = 0,
    Zlib = 1,
    Gzip = 2,
};

constexpr int kDefaultLevel = -1; // zlib's "default" (6)

bool WindowBitsForEncode(Method m, int &outBits) {
    switch (m) {
        case Method::Zlib:    outBits = 15;       return true;
        case Method::Gzip:    outBits = kGzipWindowBits; return true;
        case Method::Deflate: outBits = -15;      return true;
    }
    return false;
}

bool WindowBitsForDecode(Method m, int &outBits) {
    switch (m) {
        case Method::Zlib:    outBits = 15;       return true;
        case Method::Gzip:    outBits = kGzipWindowBits; return true;
        case Method::Deflate: outBits = -15;      return true;
    }
    return false;
}

// Run a full deflate stream: init, feed all input with Z_FINISH,
// collect output, end. Buffer grows as needed.
bool Deflate(const uint8_t *src, std::size_t srcLen, int level,
             int windowBits, std::vector<uint8_t> &out) {
    auto init  = reinterpret_cast<deflateInit2_t>(Offsets::FUN_ZLIB_DEFLATE_INIT2);
    auto step  = reinterpret_cast<deflate_t>(Offsets::FUN_ZLIB_DEFLATE);
    auto done  = reinterpret_cast<deflateEnd_t>(Offsets::FUN_ZLIB_DEFLATE_END);

    ZStream strm;
    std::memset(&strm, 0, sizeof(strm));
    // method = Z_DEFLATED (8), memLevel = DEF_MEM_LEVEL (8),
    // strategy = Z_DEFAULT_STRATEGY (0).
    if (init(&strm, level, /*method=*/8, windowBits,
             /*memLevel=*/8, /*strategy=*/0,
             reinterpret_cast<const char *>(
                 static_cast<uintptr_t>(Offsets::VAR_ZLIB_VERSION_STRING)),
             Offsets::ZLIB_STREAM_SIZE) != Z_OK) {
        return false;
    }

    Field<const uint8_t *>(strm, OFF_NEXT_IN) = src;
    Field<uint32_t>(strm, OFF_AVAIL_IN) = static_cast<uint32_t>(srcLen);

    // Initial output buffer — slightly above the worst-case overhead.
    out.resize(srcLen + (srcLen >> 12) + (srcLen >> 14) + (srcLen >> 25) + 64);
    Field<uint8_t *>(strm, OFF_NEXT_OUT) = out.data();
    Field<uint32_t>(strm, OFF_AVAIL_OUT) = static_cast<uint32_t>(out.size());

    for (;;) {
        const int rc = step(&strm, Z_FINISH);
        if (rc == Z_STREAM_END) break;
        if (rc == Z_OK) {
            // Output buffer filled before stream end — grow and retry.
            const std::size_t produced = out.size() -
                Field<uint32_t>(strm, OFF_AVAIL_OUT);
            out.resize(out.size() * 2);
            Field<uint8_t *>(strm, OFF_NEXT_OUT) = out.data() + produced;
            Field<uint32_t>(strm, OFF_AVAIL_OUT) =
                static_cast<uint32_t>(out.size() - produced);
            continue;
        }
        done(&strm);
        return false;
    }
    const std::size_t produced = out.size() -
        Field<uint32_t>(strm, OFF_AVAIL_OUT);
    out.resize(produced);
    done(&strm);
    return true;
}

} // namespace

bool Inflate(const uint8_t *src, std::size_t srcLen, int windowBits,
             std::vector<uint8_t> &out) {
    auto init = reinterpret_cast<inflateInit2_t>(Offsets::FUN_ZLIB_INFLATE_INIT2);
    auto step = reinterpret_cast<inflate_t>(Offsets::FUN_ZLIB_INFLATE);
    auto done = reinterpret_cast<inflateEnd_t>(Offsets::FUN_ZLIB_INFLATE_END);

    ZStream strm;
    std::memset(&strm, 0, sizeof(strm));
    if (init(&strm, windowBits,
             reinterpret_cast<const char *>(
                 static_cast<uintptr_t>(Offsets::VAR_ZLIB_VERSION_STRING)),
             Offsets::ZLIB_STREAM_SIZE) != Z_OK) {
        return false;
    }

    Field<const uint8_t *>(strm, OFF_NEXT_IN) = src;
    Field<uint32_t>(strm, OFF_AVAIL_IN) = static_cast<uint32_t>(srcLen);

    out.resize(srcLen * 4 + 64);
    Field<uint8_t *>(strm, OFF_NEXT_OUT) = out.data();
    Field<uint32_t>(strm, OFF_AVAIL_OUT) = static_cast<uint32_t>(out.size());

    constexpr std::size_t kCeiling = 64 * 1024 * 1024;
    for (;;) {
        const int rc = step(&strm, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK) {
            done(&strm);
            return false;
        }
        if (Field<uint32_t>(strm, OFF_AVAIL_OUT) > 0) {
            // Stream consumed all input without finishing — truncated.
            done(&strm);
            return false;
        }
        // Output buffer full, decompressor wants more space.
        const std::size_t produced = out.size();
        if (out.size() * 2 > kCeiling) {
            done(&strm);
            return false;
        }
        out.resize(out.size() * 2);
        Field<uint8_t *>(strm, OFF_NEXT_OUT) = out.data() + produced;
        Field<uint32_t>(strm, OFF_AVAIL_OUT) =
            static_cast<uint32_t>(out.size() - produced);
    }
    const std::size_t produced = out.size() -
        Field<uint32_t>(strm, OFF_AVAIL_OUT);
    out.resize(produced);
    done(&strm);
    return true;
}

namespace {

int OptionalIntArg(void *L, int idx, int defaultValue) {
    if (Game::Lua::Type(L, idx) == Game::Lua::TYPE_NUMBER)
        return static_cast<int>(Game::Lua::ToNumber(L, idx));
    return defaultValue;
}

int __fastcall Script_CompressString(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_EncodingUtil.CompressString(data [, method, level])");
        return 0;
    }
    const auto *src = reinterpret_cast<const uint8_t *>(
        Game::Lua::ToString(L, 1));
    const unsigned int srcLen = Game::Lua::StrLen(L, 1);
    if (src == nullptr) {
        Game::Lua::PushString(L, "");
        return 1;
    }

    const int methodInt = OptionalIntArg(L, 2, static_cast<int>(Method::Zlib));
    if (methodInt < 0 || methodInt > 2) {
        Game::Lua::Error(L,
            "C_EncodingUtil.CompressString: invalid CompressionMethod");
        return 0;
    }
    const Method method = static_cast<Method>(methodInt);
    const int level = OptionalIntArg(L, 3, kDefaultLevel);

    int windowBits;
    if (!WindowBitsForEncode(method, windowBits))
        return 0;

    std::vector<uint8_t> out;
    if (!Deflate(src, srcLen, level, windowBits, out))
        return 0;
    Game::Lua::PushLString(L, reinterpret_cast<const char *>(out.data()),
                           static_cast<unsigned int>(out.size()));
    return 1;
}

int __fastcall Script_DecompressString(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_EncodingUtil.DecompressString(data [, method])");
        return 0;
    }
    const auto *src = reinterpret_cast<const uint8_t *>(
        Game::Lua::ToString(L, 1));
    const unsigned int srcLen = Game::Lua::StrLen(L, 1);
    if (src == nullptr) {
        Game::Lua::PushString(L, "");
        return 1;
    }

    // No method arg → auto-detect Zlib or Gzip. Raw deflate has no
    // magic bytes and isn't auto-detectable, so it must be passed
    // explicitly.
    int windowBits;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_NUMBER) {
        const int methodInt = static_cast<int>(Game::Lua::ToNumber(L, 2));
        if (methodInt < 0 || methodInt > 2) {
            Game::Lua::Error(L,
                "C_EncodingUtil.DecompressString: invalid CompressionMethod");
            return 0;
        }
        if (!WindowBitsForDecode(static_cast<Method>(methodInt), windowBits))
            return 0;
    } else {
        windowBits = 15 + 32; // auto-detect Zlib/Gzip
    }

    std::vector<uint8_t> out;
    if (!Inflate(src, srcLen, windowBits, out))
        return 0;
    Game::Lua::PushLString(L, reinterpret_cast<const char *>(out.data()),
                           static_cast<unsigned int>(out.size()));
    return 1;
}

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "CompressString",
                                     &Script_CompressString);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DecompressString",
                                     &Script_DecompressString);

    static const Game::Lua::EnumIntegerEntry kCompressionMethod[] = {
        {"Deflate", static_cast<int>(Method::Deflate)},
        {"Zlib",    static_cast<int>(Method::Zlib)},
        {"Gzip",    static_cast<int>(Method::Gzip)},
    };
    Game::Lua::RegisterIntegerEnum("Enum", "CompressionMethod",
                                   kCompressionMethod, 3);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Encoding::Compress
