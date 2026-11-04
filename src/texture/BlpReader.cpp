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

#include "texture/BlpReader.h"

#include "Game.h"

#include <cstring>

namespace Texture {

namespace {

using Open_t = int(__thiscall *)(void *rec, const char *path, int flag);
using Parse_t = int(__thiscall *)(void *rec, const void *file);
using DecodeLevel_t = int(__thiscall *)(void *rec, uint32_t fmt, uint32_t level, void **out,
                                        uint32_t *size);
using FreeScratch_t = void(__thiscall *)(void *rec, uint32_t level);
using Reset_t = void(__thiscall *)(void *rec);

} // namespace

BlpReader::BlpReader(const char *path) {
    Init();
    open_ = reinterpret_cast<Open_t>(Offsets::FUN_BLP_OPEN)(rec_, path, 1) != 0;
}

BlpReader::BlpReader(const void *file, std::size_t size) {
    Init();
    // The parse copies a whole header out of the file.
    if (file != nullptr && size >= Offsets::BLP_HEADER_SIZE)
        open_ = reinterpret_cast<Parse_t>(Offsets::FUN_BLP_PARSE)(rec_, file) != 0;
}

BlpReader::~BlpReader() {
    if (decoded_)
        reinterpret_cast<FreeScratch_t>(Offsets::FUN_BLP_FREE_SCRATCH)(rec_, 0);
    reinterpret_cast<Reset_t>(Offsets::FUN_BLP_RESET)(rec_);
}

uint32_t BlpReader::Width() const { return Game::Read<uint32_t>(rec_, Offsets::OFF_BLP_WIDTH); }

uint32_t BlpReader::Height() const { return Game::Read<uint32_t>(rec_, Offsets::OFF_BLP_HEIGHT); }

const uint8_t *BlpReader::DecodeBgra() {
    if (!open_)
        return nullptr;
    // The engine has no DXT5 converter: DecodeLevel would report success
    // with the buffer unwritten (BLP_ALPHA_TYPE_DXT5 in Offsets.h).
    if (rec_[Offsets::OFF_BLP_COMPRESSION] == Offsets::BLP_COMPRESSION_DXT &&
        rec_[Offsets::OFF_BLP_ALPHA_TYPE] == Offsets::BLP_ALPHA_TYPE_DXT5)
        return nullptr;
    void *out = nullptr;
    uint32_t size = 0;
    decoded_ = true;
    if (reinterpret_cast<DecodeLevel_t>(Offsets::FUN_BLP_DECODE_LEVEL)(
            rec_, Offsets::GX_TEX_FORMAT_BGRA8888, 0, &out, &size) == 0)
        return nullptr;
    return static_cast<const uint8_t *>(out);
}

void BlpReader::Init() {
    std::memset(rec_, 0, sizeof(rec_));
    Put(Offsets::OFF_BLP_MAGIC, 0x32504C42); // 'BLP2'
    Put(Offsets::OFF_BLP_VERSION, 1);
    rec_[Offsets::OFF_BLP_ALPHA_TYPE] = 2;
    Put(Offsets::OFF_BLP_INIT_4A4, 100);
}

void BlpReader::Put(uintptr_t off, uint32_t v) { std::memcpy(rec_ + off, &v, sizeof(v)); }

} // namespace Texture
