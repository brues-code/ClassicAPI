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
//
// Deterministic reproducer for the notorious 1.12.1 `GetPoint` crash
// (ERROR #132, ACCESS_VIOLATION at 0x007A2452). Debug-only.
//
// The crash: `Script_GetPoint` (Region method registry idx 15, VA
// 0x007A2340) walks a frame's anchor nodes at `object + 0x28` (9 slots of
// 4 bytes). Each populated slot is a 0x14-byte node
// `{vtable=0x0081C44C, xOfs@+0x04, yOfs@+0x08, relativeTo@+0x0C, relPoint@+0x10}`.
// `relativeTo` is the anchor TARGET's CLayoutFrame-inner pointer
// (= target CFrame base + 0x24), stored RAW with no weak reference. When
// the target frame is freed the pointer is never cleared, and GetPoint does:
//
//     007a2429  CALL [EAX+0xC]      ; node vtable[3] -> returns node.relativeTo
//     007a2430  ADD  EAX,-0x24      ; EAX = relativeTo - 0x24 = target CFrame base
//     007a244f  MOV  EBX,[EBP-4]    ; EBX = target CFrame base (DANGLING)
//     007a2452  MOV  EAX,[EBX+4]    ; <-- reads refcount at base+4 -> #132
//
// The fault address is always `relativeTo - 0x20`. This cannot be produced
// from pure Lua on a stock 1.12 client, because `CreateFrame` frames are
// never freed — the dangling pointer only arises when the engine frees a
// frame across a world transition (e.g. UIParent recreation) or another
// DLL frees one. So we arm the exact condition from C: take a frame that
// already has a live anchor (call `frame:SetPoint(...)` first), and rewrite
// that node's `relativeTo` to a pointer whose `-0x20` is unreadable.
//
// Usage — arm and fire in ONE macro, on a HIDDEN frame:
//   /run TestF=CreateFrame("Frame","TestF",UIParent); TestF:Hide(); TestF:SetPoint("CENTER"); _classicapi_ArmGetPointCrash(TestF); print(TestF:GetPoint(1))
//
// Both halves of that matter. The armed `relativeTo` is read by MORE than
// GetPoint: the same node's vtable[1]/[2] (`GetWidth`/`GetHeight`,
// FUN_007A2F90 / FUN_007A3070) deref it at `[relativeTo + 0x3C]` during the
// layout pass, with no guard of their own. Arming a VISIBLE frame and then
// firing from a second macro lets the render pass reach the anchor first —
// it crashes at 0x007A2FB8 (layout) instead of 0x007A2452 (GetPoint),
// testing the wrong reader. Hiding the frame keeps it out of the layout
// pass, and keeping arm+GetPoint in one macro leaves no frame boundary
// between them.
//
// Modes (optional 2nd arg, default 0):
//   0  decommitted page  — reads a reserved-but-uncommitted page; reproduces
//                          the exact #132 signature (fault addr ends ...FFE8,
//                          like the field dumps' 0x16C0FFE8).
//   1  low address        — relativeTo = 0x24 -> reads 0x00000004 (the other
//                          common #132 flavor, "referenced memory at 0x4").
//   2  null relativeTo    — sets relativeTo = 0; GetPoint takes its own safe
//                          "no relativeTo" branch. Does NOT crash even
//                          unpatched — the control case that proves a valid
//                          no-anchor-target read still returns cleanly.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

#include <windows.h>

namespace Debug::GetPointCrash {

namespace {

// Anchor node layout — see the file header. The vtable constant is the
// engine's anchor-object vtable in .rdata; checking it guards against
// corrupting a slot that somehow isn't a real anchor node.
constexpr uintptr_t kAnchorVtable = 0x0081C44C;
constexpr int kAnchorArrayOffset = 0x28; // object + 0x28 = slot[0]
constexpr int kAnchorSlots = 9;
constexpr int kNodeRelativeTo = 0x0C;

// Reserved-but-uncommitted address space, allocated once and reused. Any
// read into it faults with ACCESS_VIOLATION, exactly like the decommitted
// CFrame page in the real crash. 64KB granularity means the base ends in
// 0x0000, so placing the synthetic target base at +0xFFE4 makes GetPoint's
// `[relativeTo-0x20]` read land at ...FFE8 — matching the dumps.
void *g_reserved = nullptr;

uintptr_t DecommittedInner() {
    if (g_reserved == nullptr)
        g_reserved = VirtualAlloc(nullptr, 0x11000, MEM_RESERVE, PAGE_NOACCESS);
    if (g_reserved == nullptr)
        return 0x24; // fall back to the low-address flavor if reservation fails
    // target CFrame base = reserved + 0xFFE4; inner = base + 0x24.
    // GetPoint reads [inner - 0x24 + 4] = [base + 4] = reserved + 0xFFE8.
    return reinterpret_cast<uintptr_t>(g_reserved) + 0xFFE4 + 0x24;
}

// `_classicapi_ArmGetPointCrash(frame [, mode])` — arms `frame`'s first
// live anchor node so the next `frame:GetPoint(index)` faults. Returns
// (faultAddress, armedSlotIndex). Raises a Lua error (no crash) if the
// frame has no anchor yet or the slot isn't a recognizable anchor node.
int __fastcall Script_ArmGetPointCrash(void *L) {
    void *obj = Game::Lua::ResolveRegion(L, 1); // raises if arg1 isn't a region
    if (obj == nullptr)
        return 0;

    const int mode =
        Game::Lua::IsNumber(L, 2) ? static_cast<int>(Game::Lua::ToNumber(L, 2)) : 0;

    auto *base = static_cast<uint8_t *>(obj);
    uint8_t *node = nullptr;
    int slotIdx = -1;
    for (int i = 0; i < kAnchorSlots; ++i) {
        auto *n = Game::Read<uint8_t *>(base, kAnchorArrayOffset + i * 4);
        if (n != nullptr) {
            node = n;
            slotIdx = i;
            break;
        }
    }
    if (node == nullptr) {
        Game::Lua::Error(
            L, "_classicapi_ArmGetPointCrash: frame has no anchor; call SetPoint first");
        return 0;
    }
    if (Game::Read<uintptr_t>(node, 0) != kAnchorVtable) {
        Game::Lua::Error(L,
                         "_classicapi_ArmGetPointCrash: slot is not an anchor node");
        return 0;
    }

    uintptr_t badInner;
    switch (mode) {
    case 1:
        badInner = 0x24; // reads [0x24 - 0x20] = 0x00000004
        break;
    case 2:
        badInner = 0; // safe "no relativeTo" branch — control case, no crash
        break;
    default:
        badInner = DecommittedInner();
        break;
    }
    Game::Ref<uintptr_t>(node, kNodeRelativeTo) = badInner;

    // Report the address the next GetPoint will dereference (relativeTo - 0x20).
    const double faultAddr = badInner ? static_cast<double>(badInner - 0x20) : 0.0;
    Game::Lua::PushNumber(L, faultAddr);
    Game::Lua::PushNumber(L, static_cast<double>(slotIdx));
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_ArmGetPointCrash",
                                      &Script_ArmGetPointCrash);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Debug::GetPointCrash
