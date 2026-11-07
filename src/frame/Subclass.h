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

#pragma once

// The parts every frame class this DLL adds shares: copied engine vtables with
// overrides in place, and a method hash of the class's own (Offsets.h
// "Subclassing CSimpleFrame", "Per-class method hashes"). Frame::Cooldown and
// Model::Scene are built on it.

#include "Game.h"
#include "Offsets.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Frame::Subclass {

// An MSVC vtable with its RTTI locator in front, as the engine's are laid out.
template <size_t N>
struct VTable {
    const void *locator;
    const void *slots[N];

    bool Built() const { return slots[0] != nullptr; }
    // A copy of the engine vtable at `address` (its first slot).
    void CopyFrom(uintptr_t address) {
        std::memcpy(this, reinterpret_cast<const void *>(address - 4), sizeof(*this));
    }
    // `slot` is a byte offset (Offsets::VTBL_*).
    void Set(uintptr_t slot, const void *fn) { slots[slot / 4] = fn; }
};

// The frame a layout sub-object (layout-vtable `this`) belongs to.
inline void *FrameOfLayout(void *layout) {
    return static_cast<uint8_t *>(layout) - Offsets::OFF_FRAME_LAYOUT_SUBOBJECT;
}

// The function in byte offset `slot` of the engine vtable at `address`, for
// an override's base call.
template <typename Fn>
Fn Slot(uintptr_t address, uintptr_t slot) {
    return Game::Read<Fn>(reinterpret_cast<const void *>(address), slot);
}

// A frame class's method hash, built the way the engine's static initializer
// builds one (Model's 0x0076F5F0). Every hash is released where the engine
// releases its own class hashes, at the end of CGGameUI::UnregisterScriptFunctions,
// while the game Lua state is still open. Declare one at file scope.
class MethodHash {
  public:
    MethodHash();
    MethodHash(const MethodHash &) = delete;
    MethodHash &operator=(const MethodHash &) = delete;

    // What Game::Lua::RegisterFrameMethods and the documentation take.
    void *Registry() { return words_; }
    // FrameScriptPushMethod: pushes the method and answers true when the
    // hash has it.
    bool PushMethod(void *L, const char *name);

    static void ReleaseAll();

  private:
    uint32_t words_[Offsets::METHOD_HASH_SIZE / 4];
    MethodHash *next_;
};

} // namespace Frame::Subclass
