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

// `C_EncodingUtil.CompressString` / `DecompressString`, as the 3.80.2 client
// does them (bodies 0x140C1D660 / 0x140C1DFB0, zlib helpers 0x141B977F0 /
// 0x141B979C0), on the 1.12 engine's own zlib (1.2.2).
//
//   Enum.CompressionMethod = { Deflate = 0, Zlib = 1, Gzip = 2 }
//     -> windowBits -15 (raw), 15, 31. Decompression uses the method given;
//        it does not detect the format.
//   Enum.CompressionLevel = { Default = 0, OptimizeForSpeed = 1, OptimizeForSize = 2 }
//     -> zlib level -1, 1, 9.
//
// Compression: memLevel 9, default strategy; a gzip header with every field
// zero but the OS byte, 0xFF (3.80.2 sets it through deflateSetHeader; 1.2.2's
// header is the same but for that byte, which this patches).
//
// Decompression: the output starts at 1 KiB and grows by half while zlib asks
// for room, up to exactly Args::kDecompressSizeLimit (100 MiB); input that
// runs out first is corrupt. Failures raise "<function>(): <message>":
// "internal compression error", "internal decompression error",
// "internal decompression size limit reached (cannot exceed 104857600
// bytes)", "internal memory allocation error".

#include "encoding/Compress.h"

#include "Game.h"
#include "Offsets.h"
#include "encoding/Args.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Encoding::Compress {

namespace {

// z_stream (zlib 1.2.2 layout). We only read and write the buffer fields.
struct ZStream {
    uint8_t bytes[Offsets::ZLIB_STREAM_SIZE];
};

constexpr int OFF_NEXT_IN = 0x00;
constexpr int OFF_AVAIL_IN = 0x04;
constexpr int OFF_NEXT_OUT = 0x0C;
constexpr int OFF_AVAIL_OUT = 0x10;
constexpr int OFF_TOTAL_OUT = 0x14;

template <typename T>
T &Field(ZStream &s, int off) {
    return *reinterpret_cast<T *>(&s.bytes[off]);
}

using deflateInit2_t = int(__fastcall *)(ZStream *strm, int level, int method, int windowBits,
                                         int memLevel, int strategy, const char *version,
                                         int streamSize);
using deflate_t = int(__fastcall *)(ZStream *strm, int flush);
using deflateEnd_t = int(__fastcall *)(ZStream *strm);
using inflateInit2_t = int(__fastcall *)(ZStream *strm, int windowBits, const char *version,
                                         int streamSize);
using inflate_t = int(__fastcall *)(ZStream *strm, int flush);
using inflateEnd_t = int(__fastcall *)(ZStream *strm);

constexpr int Z_OK = 0;
constexpr int Z_STREAM_END = 1;
constexpr int Z_MEM_ERROR = -4;
constexpr int Z_BUF_ERROR = -5;
constexpr int Z_FINISH = 4;

const char *ZlibVersion() {
    return reinterpret_cast<const char *>(static_cast<uintptr_t>(Offsets::VAR_ZLIB_VERSION_STRING));
}

enum Method : int {
    METHOD_DEFLATE = 0,
    METHOD_ZLIB = 1,
    METHOD_GZIP = 2,
};

enum Level : int {
    LEVEL_DEFAULT = 0,
    LEVEL_OPTIMIZE_FOR_SPEED = 1,
    LEVEL_OPTIMIZE_FOR_SIZE = 2,
};

int WindowBits(int method) {
    return method == METHOD_ZLIB ? 15 : method == METHOD_GZIP ? kGzipWindowBits : -15;
}

int ZlibLevel(int level) {
    return level == LEVEL_OPTIMIZE_FOR_SPEED ? 1 : level == LEVEL_OPTIMIZE_FOR_SIZE ? 9 : -1;
}

// A full deflate stream. Returns Z_STREAM_END, or the failing zlib code.
int Deflate(const uint8_t *src, std::size_t srcLen, int level, int windowBits,
            std::vector<uint8_t> &out) {
    auto init = reinterpret_cast<deflateInit2_t>(Offsets::FUN_ZLIB_DEFLATE_INIT2);
    auto step = reinterpret_cast<deflate_t>(Offsets::FUN_ZLIB_DEFLATE);
    auto done = reinterpret_cast<deflateEnd_t>(Offsets::FUN_ZLIB_DEFLATE_END);

    ZStream strm;
    std::memset(&strm, 0, sizeof(strm));
    const int rc = init(&strm, level, /*method=*/8, windowBits, /*memLevel=*/9, /*strategy=*/0,
                        ZlibVersion(), Offsets::ZLIB_STREAM_SIZE);
    if (rc != Z_OK)
        return rc;

    Field<const uint8_t *>(strm, OFF_NEXT_IN) = src;
    Field<uint32_t>(strm, OFF_AVAIL_IN) = static_cast<uint32_t>(srcLen);
    // zlib's own deflateBound for these parameters, plus a gzip header.
    out.resize(srcLen + (srcLen >> 12) + (srcLen >> 14) + (srcLen >> 25) + 64);
    Field<uint8_t *>(strm, OFF_NEXT_OUT) = out.data();
    Field<uint32_t>(strm, OFF_AVAIL_OUT) = static_cast<uint32_t>(out.size());

    int result;
    for (;;) {
        result = step(&strm, Z_FINISH);
        if (result != Z_OK)
            break;
        // The output filled before the end of the stream.
        const std::size_t produced = Field<uint32_t>(strm, OFF_TOTAL_OUT);
        out.resize(out.size() * 2);
        Field<uint8_t *>(strm, OFF_NEXT_OUT) = out.data() + produced;
        Field<uint32_t>(strm, OFF_AVAIL_OUT) = static_cast<uint32_t>(out.size() - produced);
    }
    out.resize(Field<uint32_t>(strm, OFF_TOTAL_OUT));
    done(&strm);
    if (result == Z_STREAM_END && windowBits > 15 && out.size() > 9)
        out[9] = 0xFF; // the gzip header's OS byte
    return result;
}

constexpr const char *kCompressUsage =
    "Usage: local output = C_EncodingUtil.CompressString(source [, method, level])";
constexpr const char *kDecompressUsage =
    "Usage: local output = C_EncodingUtil.DecompressString(source [, method])";

int __fastcall Script_CompressString(void *L) {
    const uint8_t *src;
    std::size_t len;
    int method = METHOD_DEFLATE;
    int level = LEVEL_DEFAULT;
    if (!Args::Source(L, 1, kCompressUsage, &src, &len) ||
        !Args::Enum(L, 2, METHOD_GZIP, kCompressUsage, &method) ||
        !Args::Enum(L, 3, LEVEL_OPTIMIZE_FOR_SIZE, kCompressUsage, &level))
        return 0;
    int rc;
    {
        std::vector<uint8_t> out;
        rc = Deflate(src, len, ZlibLevel(level), WindowBits(method), out);
        if (rc == Z_STREAM_END) {
            Game::Lua::PushLString(L, reinterpret_cast<const char *>(out.data()),
                                   static_cast<unsigned int>(out.size()));
            return 1;
        }
    }
    Args::Raise(L, "CompressString",
                rc == Z_MEM_ERROR ? "internal memory allocation error" : "internal compression error");
    return 0;
}

int __fastcall Script_DecompressString(void *L) {
    const uint8_t *src;
    std::size_t len;
    int method = METHOD_DEFLATE;
    if (!Args::Source(L, 1, kDecompressUsage, &src, &len) ||
        !Args::Enum(L, 2, METHOD_GZIP, kDecompressUsage, &method))
        return 0;
    int rc;
    {
        std::vector<uint8_t> out;
        rc = Inflate(src, len, WindowBits(method), out);
        if (rc == kInflateDone) {
            Game::Lua::PushLString(L, reinterpret_cast<const char *>(out.data()),
                                   static_cast<unsigned int>(out.size()));
            return 1;
        }
    }
    if (rc == kInflateTooLarge) {
        char message[96];
        std::snprintf(message, sizeof(message),
                      "internal decompression size limit reached (cannot exceed %u bytes)",
                      static_cast<unsigned>(Args::kDecompressSizeLimit));
        Args::Raise(L, "DecompressString", message);
    } else {
        Args::Raise(L, "DecompressString",
                    rc == kInflateNoMemory ? "internal memory allocation error" : "internal decompression error");
    }
    return 0;
}

const Game::Doc::Field kCompressArgs[] = {
    Game::Doc::Req("source", "stringView"),
    Game::Doc::Opt("method", "CompressionMethod", "Deflate"),
    Game::Doc::Opt("level", "CompressionLevel", "Default"),
};
const Game::Doc::Field kCompressRets[] = {
    Game::Doc::Req("output", "stringView"),
};
const Game::Doc::Function kCompressString{"Compresses a string.", kCompressArgs, kCompressRets};

const Game::Doc::Field kDecompressArgs[] = {
    Game::Doc::Req("source", "stringView",
                   "Data that is not valid for the method, or that ends too soon, is an error."),
    Game::Doc::Opt("method", "CompressionMethod", "Deflate",
                   "Must be the method the data was compressed with."),
};
const Game::Doc::Field kDecompressRets[] = {
    Game::Doc::Req("output", "stringView",
                   "Output longer than Constants.EncodingLimits.EncodingDecompressSizeLimit is an "
                   "error."),
};
const Game::Doc::Function kDecompressString{"Decompresses a string.", kDecompressArgs,
                                            kDecompressRets};

void Register() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "CompressString", &Script_CompressString,
                                     &kCompressString);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DecompressString",
                                     &Script_DecompressString, &kDecompressString);

    static const Game::Lua::EnumIntegerEntry kCompressionMethod[] = {
        {"Deflate", METHOD_DEFLATE},
        {"Zlib", METHOD_ZLIB},
        {"Gzip", METHOD_GZIP},
    };
    Game::Lua::RegisterIntegerEnum("Enum", "CompressionMethod", kCompressionMethod, "EncodingUtil");

    static const Game::Lua::EnumIntegerEntry kCompressionLevel[] = {
        {"Default", LEVEL_DEFAULT},
        {"OptimizeForSpeed", LEVEL_OPTIMIZE_FOR_SPEED},
        {"OptimizeForSize", LEVEL_OPTIMIZE_FOR_SIZE},
    };
    Game::Lua::RegisterIntegerEnum("Enum", "CompressionLevel", kCompressionLevel, "EncodingUtil");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

int Inflate(const uint8_t *src, std::size_t srcLen, int windowBits, std::vector<uint8_t> &out) {
    auto init = reinterpret_cast<inflateInit2_t>(Offsets::FUN_ZLIB_INFLATE_INIT2);
    auto step = reinterpret_cast<inflate_t>(Offsets::FUN_ZLIB_INFLATE);
    auto done = reinterpret_cast<inflateEnd_t>(Offsets::FUN_ZLIB_INFLATE_END);

    ZStream strm;
    std::memset(&strm, 0, sizeof(strm));
    const int initRc = init(&strm, windowBits, ZlibVersion(), Offsets::ZLIB_STREAM_SIZE);
    if (initRc != Z_OK)
        return initRc;

    Field<const uint8_t *>(strm, OFF_NEXT_IN) = src;
    Field<uint32_t>(strm, OFF_AVAIL_IN) = static_cast<uint32_t>(srcLen);
    out.resize(1024);
    Field<uint8_t *>(strm, OFF_NEXT_OUT) = out.data();
    Field<uint32_t>(strm, OFF_AVAIL_OUT) = static_cast<uint32_t>(out.size());

    int result;
    for (;;) {
        const int rc = step(&strm, Z_FINISH);
        if (rc == Z_STREAM_END) {
            result = kInflateDone;
            break;
        }
        if (rc != Z_BUF_ERROR && rc != Z_OK) {
            result = rc;
            break;
        }
        // zlib wants more input or more room.
        if (Field<uint32_t>(strm, OFF_AVAIL_IN) == 0) {
            result = kInflateCorrupt;
            break;
        }
        if (out.size() >= Args::kDecompressSizeLimit) {
            result = kInflateTooLarge;
            break;
        }
        std::size_t size = out.size() + out.size() / 2;
        if (size > Args::kDecompressSizeLimit)
            size = Args::kDecompressSizeLimit;
        const std::size_t produced = Field<uint32_t>(strm, OFF_TOTAL_OUT);
        out.resize(size);
        Field<uint8_t *>(strm, OFF_NEXT_OUT) = out.data() + produced;
        Field<uint32_t>(strm, OFF_AVAIL_OUT) = static_cast<uint32_t>(out.size() - produced);
    }
    out.resize(Field<uint32_t>(strm, OFF_TOTAL_OUT));
    done(&strm);
    return result;
}

} // namespace Encoding::Compress
