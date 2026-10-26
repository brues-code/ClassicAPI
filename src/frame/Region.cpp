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

#include "frame/Region.h"

#include "Offsets.h"

#include <cstdint>

namespace Frame::Region {

namespace {

using PoolAlloc_t = void *(__thiscall *)(void *pool, int zeroInit, const char *tag, int line);
using Ctor_t = void *(__thiscall *)(void *mem, void *parent, int layer, int sublayer);

void *Create(uintptr_t pool, uintptr_t classTag, uintptr_t ctor, void *parent, int layer,
             int sublayer) {
    void *mem = reinterpret_cast<PoolAlloc_t>(Offsets::FUN_REGION_POOL_ALLOC)(
        reinterpret_cast<void *>(pool), 0, reinterpret_cast<const char *>(classTag), -2);
    if (mem == nullptr)
        return nullptr;
    return reinterpret_cast<Ctor_t>(ctor)(mem, parent, layer, sublayer);
}

} // namespace

void *CreateTexture(void *parent, int layer, int sublayer) {
    return Create(Offsets::VAR_SIMPLETEXTURE_POOL, Offsets::VAR_SIMPLETEXTURE_CLASS_TAG,
                  Offsets::FUN_SIMPLETEXTURE_CTOR, parent, layer, sublayer);
}

void *CreateFontString(void *parent, int layer, int sublayer) {
    return Create(Offsets::VAR_SIMPLEFONTSTRING_POOL, Offsets::VAR_SIMPLEFONTSTRING_CLASS_TAG,
                  Offsets::FUN_SIMPLEFONTSTRING_CTOR, parent, layer, sublayer);
}

} // namespace Frame::Region
