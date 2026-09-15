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
// Neuters the notorious 1.12 `GetPoint` crash (ERROR #132 at 0x007A2452).
//
// The bug: an anchor node (`region+0x28 + point*4`, vtable
// VAR_ANCHOR_VTABLE_WITH_RELATIVE) stores its `relativeTo` target as a RAW
// pointer at +0x0C with no weak reference. When the target frame is freed,
// the engine cleans the DYING frame's own anchors but never the anchors on
// OTHER frames that point AT it — so the pointer dangles. `GetPoint` then
// does `MOV EAX,[relativeTo-0x24+4]` (read the target's refcount) and faults
// on the freed/decommitted page. The layout readers `GetWidth`/`GetHeight`
// (the same node's vtable[1]/[2]) deref the same dangling `relativeTo`
// without a guard too, so nulling the pointer only moves the crash — the
// node has to go.
//
// Fix (no MinHook): re-register `GetPoint` on the Region method registry.
// FUN_REGISTER_FRAME_METHODS pushes to the FRONT of the registry's hash
// bucket and the dispatcher walks front-first, so the most recent
// registration wins; the engine registers the Region table as the first
// call inside FUN_LOAD_SCRIPT_FUNCTIONS and ModuleAutoRegister runs after
// the original, so ours wins on every /reload — same mechanism as the
// `Texture::SetDesaturated` override.
//
// Our GetPoint pre-scans the region's anchor nodes; any whose `relativeTo`
// target is no longer readable is handed to the engine's OWN
// FUN_REGION_CLEAR_POINT_BY_RELATIVE — the cleanup the engine runs when an
// anchor target is legitimately removed, and precisely the call that is
// MISSING on target destruction. It frees the node and NULLs the slot by
// pointer comparison, never dereferencing the dead target. Then we delegate
// to the stock GetPoint, which now walks only live anchors. This also
// protects the layout readers, since the dead node is gone for good.
//
// Why DELETE the node rather than just NULL its relativeTo — confirmed against
// 3.3.5 (Frostmourne, Script_GetPoint 0x0049D950). Blizzard fixed this by
// restructuring the anchor: 3.3.5's `CFramePoint` is a 16-byte POD with no
// vtable — {xOfs@0, yOfs@4, relativeTo@8, relPoint-byte + FLAGS @0xC} — and
// on destruction `FUN_0048B130` walks the dying frame's dependents and calls
// `CFramePoint::Invalidate` (`FUN_0049C7F0`) on each referencing node, which
// zeroes relativeTo and sets flag bit 0x800 IN PLACE, leaving the node
// allocated. Every reader then gates on 0x800 before touching relativeTo
// (verified in GetPoint 0x0049D9BF, GetNumPoints 0x0049D8B0,
// FUN_0048A200 and FUN_0048B130 itself), so a destroyed target degrades to
// "anchor ignored" instead of a wild deref. 3.3.5 also makes every "no
// target" state first-class: the CSimpleTop singleton ([0x00B499A8]) for
// screen-anchored (GetPoint pushes nil), flag 0x100 for no-relativeTo.
//
// 1.12 CANNOT copy that: its node has no flags word, so there is nowhere to
// put 0x800 and no bit for readers to check. Its only representation for a
// dead anchor is the EMPTY SLOT — which is exactly what
// FUN_REGION_CLEAR_POINT_BY_RELATIVE produces. That is also why nulling
// relativeTo here would be wrong rather than merely incomplete: a live 0x44C
// node with relativeTo == 0 is a state the engine never creates, and the
// layout readers would fault on `[0 + 0x3C]`.
//
// Reproduce / verify with `_classicapi_ArmGetPointCrash` (debug/GetPointCrash.cpp):
//   /run TestF=CreateFrame("Frame","TestF",UIParent); TestF:SetPoint("CENTER"); _classicapi_ArmGetPointCrash(TestF)
//   /run print(TestF:GetPoint(1))   -- pre-fix: #132 ; post-fix: nil (anchor cleaned)

#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"

#include <cstdint>

#include <windows.h>

namespace Frame::GetPointGuard {

namespace {

using GetPoint_t = int(__fastcall *)(void *L);
using ClearPointByRelative_t = void(__thiscall *)(void *layout, void *relativeTo,
                                                  int doRelayout);

// SEH-guarded readability probe. Standalone with only raw pointers and no
// C++ objects in scope, per the MSVC __try/__except constraint. A fault on
// any of the probed bytes means the region behind the pointer is gone.
bool ProbeReadable(const void *p, int n) {
    __try {
        const volatile uint8_t *q = static_cast<const volatile uint8_t *>(p);
        for (int i = 0; i < n; ++i)
            (void)q[i];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// GetPoint reads the target CFrame at base+0 (vtable), +4 (refcount) and
// +8 (lua ref index); probe that header. base = relativeTo - OFF_REGION_ANCHOR.
constexpr int kTargetHeaderProbe = 0x10;

// Walk `region`'s anchor slots and drop any whose relativeTo target is dead,
// so the stock GetPoint (and the layout readers) only ever see live anchors.
void CleanDanglingAnchors(void *region) {
    auto *base = static_cast<uint8_t *>(region);
    for (int i = 0; i < Offsets::REGION_ANCHOR_SLOT_COUNT; ++i) {
        auto *node = Game::Read<uint8_t *>(
            base, Offsets::OFF_REGION_ANCHOR_ARRAY + i * 4);
        if (node == nullptr)
            continue;
        // Only relativeTo-carrying nodes can dangle; the rel-less node type
        // has no target field and its readers never deref one. Checking the
        // vtable also guards against corrupting an unrecognized slot.
        if (Game::Read<uintptr_t>(node, 0) != Offsets::VAR_ANCHOR_VTABLE_WITH_RELATIVE)
            continue;
        auto *rel =
            Game::Read<uint8_t *>(node, Offsets::OFF_ANCHOR_NODE_RELATIVE_TO);
        // rel == 0 never occurs on a live 0x44C node (SetPoint/SetAllPoints
        // always store a real target and error otherwise). Leave it to the
        // engine's own null-relativeTo path; do NOT run the cleanup with a
        // NULL target, which would also match — and delete — legit rel-less
        // anchors (whose GetRelativeTo likewise returns 0).
        if (rel == nullptr)
            continue;
        auto *target = rel - Offsets::OFF_REGION_ANCHOR; // inner - 0x24 = CFrame base
        if (ProbeReadable(target, kTargetHeaderProbe))
            continue; // live target — anchor is fine

        // Dangling. Hand the dead target to the engine's own removal path; it
        // frees every anchor on this region pointing at `rel` and NULLs the
        // slot(s). doRelayout = 0: don't kick a layout cascade from inside a
        // getter — the frame relayouts on the next natural pass.
        auto ClearByRel = reinterpret_cast<ClearPointByRelative_t>(
            Offsets::FUN_REGION_CLEAR_POINT_BY_RELATIVE);
        ClearByRel(base + Offsets::OFF_REGION_ANCHOR, rel, 0);
        Debug::Log::Printf(
            "[GetPointGuard] cleaned dangling anchor: region=%p slot=%d relativeTo=%p",
            region, i, static_cast<void *>(rel));
    }
}

int __fastcall GetPoint_h(void *L) {
    // Resolve exactly as the engine does (same type-id var 0x00CF0C3C), but
    // without raising — let the stock GetPoint emit the canonical error text
    // for a bad `self`. Clean only when we actually hold a region.
    void *region = Game::Lua::ResolveRegion(L, 1, /*raiseError=*/false);
    if (region != nullptr)
        CleanDanglingAnchors(region);

    auto engineGetPoint = reinterpret_cast<GetPoint_t>(Offsets::FUN_GET_POINT);
    return engineGetPoint(L);
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetPoint", &GetPoint_h},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::GetPointGuard
