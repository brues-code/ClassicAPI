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

#include "CGItem.h"

#include "Offsets.h"

namespace Item {

const uint8_t *InstanceBlock(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        cgItem + Offsets::OFF_ITEM_INSTANCE_BLOCK);
}

uint64_t Guid(const uint8_t *cgItem) {
    const uint8_t *instance = InstanceBlock(cgItem);
    if (instance == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_GUID);
}

const uint8_t *ObjectFields(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        cgItem + Offsets::OFF_ITEM_DESCRIPTOR);
}

uint32_t StackCount(const uint8_t *cgItem) {
    const uint8_t *fields = ObjectFields(cgItem);
    if (fields == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(fields + Offsets::OFF_DESCRIPTOR_STACK_COUNT);
}

uint32_t Flags(const uint8_t *cgItem) {
    const uint8_t *fields = ObjectFields(cgItem);
    if (fields == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(fields + Offsets::OFF_DESCRIPTOR_FLAGS);
}

bool IsLocked(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return false;
    return (*reinterpret_cast<const uint32_t *>(cgItem + Offsets::OFF_ITEM_CLIENT_LOCK) &
            Offsets::ITEM_CLIENT_LOCK_BIT) != 0;
}

void SetLocked(uint64_t guid, bool locked) {
    // `__stdcall(guidLo, guidHi)`, both ending `RET 0x8`: declaring them
    // `__cdecl` once left ESP 8 bytes off per call until a later Lua dispatch
    // read a smashed pointer and crashed.
    using LockByGuid_t = void(__stdcall *)(uint32_t guidLo, uint32_t guidHi);
    const auto fn = reinterpret_cast<LockByGuid_t>(static_cast<uintptr_t>(
        locked ? Offsets::FUN_ITEM_LOCK_BY_GUID : Offsets::FUN_ITEM_UNLOCK_BY_GUID));
    fn(static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
}

} // namespace Item
