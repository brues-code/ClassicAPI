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

#include "fontstring/Layout.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace FontString::Layout {

namespace {

// FUN_HANDLE_RELEASE — DecRef a ref-counted handle. __fastcall(handle).
using HandleRelease_t = void(__fastcall *)(void *handle);
// FUN_LAYOUT_RESIZE — region layout resize (0 = invalidate). __thiscall(region,
// int); dummy-EDX form.
using LayoutInvalidate_t = void(__fastcall *)(void *region, void *edx, int arg);

} // namespace

void Invalidate(void *fs) {
    Game::Ref<float>(fs, Offsets::OFF_FONTSTRING_WIDTH_CACHE) = 0.0f;
    Game::Ref<float>(fs, Offsets::OFF_FONTSTRING_HEIGHT_CACHE) = 0.0f;
    void *block = Game::Read<void *>(fs, Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (block != nullptr) {
        reinterpret_cast<HandleRelease_t>(Offsets::FUN_HANDLE_RELEASE)(block);
        Game::Ref<void *>(fs, Offsets::OFF_FONTSTRING_TEXT_BLOCK) = nullptr;
    }
    Game::Ref<uint32_t>(fs, Offsets::OFF_FONTSTRING_DIRTY_FLAGS) &= ~1u;
    void *region = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(fs) + 0x24);
    reinterpret_cast<LayoutInvalidate_t>(Offsets::FUN_LAYOUT_RESIZE)(region, nullptr, 0);
}

} // namespace FontString::Layout
