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
// overrides in place, a method hash of the class's own, its type id and
// type-name overrides, and the factory (Offsets.h "Subclassing CSimpleFrame",
// "Per-class method hashes"). Frame::Cooldown and Model::Scene are built on it.

#include "Game.h"
#include "Offsets.h"
#include "frame/Layout.h"

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

// CSimpleFrame's own slot, for the base call of an override that answers as
// a Frame whatever engine class the frame extends.
template <typename Fn>
Fn FrameBase(uintptr_t slot) {
    return Slot<Fn>(Offsets::VTBL_SIMPLEFRAME, slot);
}

// A frame class this DLL adds: what it is and what it extends, then the
// state the engine and the shared overrides below keep for it. Declare one
// at file scope; the overrides take it as their template argument.
template <size_t Slots, size_t LayoutSlots>
struct Class {
    const char *name;            // what GetObjectType gives and IsObjectType takes
    uintptr_t engineVtable;      // the engine class it extends: its vtable,
    uintptr_t engineLayoutVtable; // its layout vtable,
    uintptr_t constructor;       // and its constructor, __thiscall (frame, parent)
    size_t size;                 // the whole object, the engine class's part and ours
    void (*destroy)(void *frame); // releases our part, before the engine's destructor

    int typeId = 0; // the engine's type id for the class, once it names one
    MethodHash hash;
    VTable<Slots> vtable;
    VTable<LayoutSlots> layoutVtable;

    // What Game::Lua::ResolveTypedObject and Frame::ExtraScripts take.
    uintptr_t TypeIdVar() { return reinterpret_cast<uintptr_t>(&typeId); }
    // The engine class's own slot, for the base call of an override.
    template <typename Fn>
    Fn Base(uintptr_t slot) const {
        return Slot<Fn>(engineVtable, slot);
    }
    template <typename Fn>
    Fn LayoutBase(uintptr_t slot) const {
        return Slot<Fn>(engineLayoutVtable, slot);
    }
};

// ---- the overrides every class has --------------------------------------------

using DeletingDestructor_t = void *(__fastcall *)(void *self, void *edx, uint32_t flags);
using GetScriptMethod_t = int(__fastcall *)(void *self, void *edx, void *L, const char *name);
using IsA_t = bool(__fastcall *)(void *self, void *edx, int typeId);
using IsObjectType_t = bool(__fastcall *)(void *self, void *edx, const char *name);

// Our part released, then the engine class's deleting destructor.
template <auto &C>
void *__fastcall DeletingDestructor(void *self, void *, uint32_t flags) {
    C.destroy(self);
    return C.template Base<DeletingDestructor_t>(Offsets::VTBL_FRAME_DELETING_DESTRUCTOR)(
        self, nullptr, flags);
}

template <auto &C>
void *__fastcall LayoutDeletingDestructor(void *layout, void *, uint32_t flags) {
    return DeletingDestructor<C>(Layout::OwnerOf(layout), nullptr, flags);
}

// The class's methods, then a Frame's: none of the engine class's own
// (ModelScene has no Model methods).
template <auto &C>
int __fastcall GetScriptMethod(void *self, void *, void *L, const char *name) {
    if (C.hash.PushMethod(L, name))
        return 1;
    return FrameBase<GetScriptMethod_t>(Offsets::VTBL_FRAME_GET_SCRIPT_METHOD)(self, nullptr, L,
                                                                               name);
}

// The class, then what a Frame is (retail's ModelScene is not a Model).
template <auto &C>
bool __fastcall IsA(void *self, void *, int typeId) {
    return (C.typeId != 0 && typeId == C.typeId) ||
           FrameBase<IsA_t>(Offsets::VTBL_FRAME_IS_A)(self, nullptr, typeId);
}

template <auto &C>
bool __fastcall IsObjectType(void *self, void *, const char *name) {
    return (name != nullptr && Game::StrCmpI(name, C.name) == 0) ||
           FrameBase<IsObjectType_t>(Offsets::VTBL_FRAME_IS_OBJECT_TYPE)(self, nullptr, name);
}

template <auto &C>
const char *__fastcall GetObjectType(void *, void *) {
    return C.name;
}

// Copies the engine class's two vtables and puts in the overrides above, with
// CSimpleFrame's script names (+0x0C) to match its methods. False when they
// are built already; on true the caller puts in its own overrides.
template <auto &C>
bool BuildVTables() {
    if (C.vtable.Built())
        return false;
    C.vtable.CopyFrom(C.engineVtable);
    C.layoutVtable.CopyFrom(C.engineLayoutVtable);
    auto fn = [](auto f) { return reinterpret_cast<const void *>(f); };
    C.vtable.Set(Offsets::VTBL_FRAME_DELETING_DESTRUCTOR, fn(&DeletingDestructor<C>));
    C.vtable.Set(Offsets::VTBL_FRAME_GET_SCRIPT_METHOD, fn(&GetScriptMethod<C>));
    C.vtable.Set(Offsets::VTBL_FRAME_SCRIPT_RESOLVER,
                 FrameBase<const void *>(Offsets::VTBL_FRAME_SCRIPT_RESOLVER));
    C.vtable.Set(Offsets::VTBL_FRAME_IS_A, fn(&IsA<C>));
    C.vtable.Set(Offsets::VTBL_FRAME_IS_OBJECT_TYPE, fn(&IsObjectType<C>));
    C.vtable.Set(Offsets::VTBL_FRAME_GET_OBJECT_TYPE, fn(&GetObjectType<C>));
    C.layoutVtable.Set(Offsets::VTBL_LAYOUT_DELETING_DESTRUCTOR,
                       fn(&LayoutDeletingDestructor<C>));
    return true;
}

// The allocation an engine factory makes (the "Frame" one, 0x006EEC10), sized
// for the whole object and zero-filled; null when it fails.
void *Allocate(size_t size, const char *file, int line);

// A new frame of the class, shaped like the engine's factories: the block
// (our part zero), the engine class's constructor, then our vtables, which
// BuildVTables must have built. Null when the allocation fails.
template <auto &C>
void *New(void *parent) {
    using Constructor_t = void(__fastcall *)(void *self, void *edx, void *parent);
    void *frame = Allocate(C.size, __FILE__, __LINE__);
    if (frame == nullptr)
        return nullptr;
    reinterpret_cast<Constructor_t>(C.constructor)(frame, nullptr, parent);
    Game::Ref<const void *>(frame, 0) = C.vtable.slots;
    Game::Ref<const void *>(frame, Offsets::OFF_FRAME_LAYOUT_SUBOBJECT) = C.layoutVtable.slots;
    return frame;
}

using Factory_t = void *(__fastcall *)(void *parent);

// Adds the frame type to CreateFrame and XML (FUN_FRAMEXML_REGISTER_FACTORY).
// The engine's type table outlives the Lua state, so on a /reload the name
// is there already and the engine keeps the first factory.
void RegisterFactory(const char *type, Factory_t factory);

} // namespace Frame::Subclass
