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

// `region:GetPoint([index])` — 1.12's Script_GetPoint (FUN_GET_POINT,
// 0x007A2340) with the one check 3.3.5 added, and a pre-scan for anchors
// whose target is gone.
//
// Re-registered on the Region method registry rather than hooked:
// FUN_REGISTER_FRAME_METHODS pushes to the FRONT of the registry's hash
// bucket and the dispatcher walks front-first, so the most recent
// registration wins. The engine registers the Region table as the first call
// inside FUN_LOAD_SCRIPT_FUNCTIONS and ModuleAutoRegister runs after the
// original, so ours wins on every /reload — the `Texture::SetDesaturated`
// mechanism.
//
// THE SCREEN ANCHOR. CSimpleTop is a bare CLayoutFrame (Offsets
// VAR_UI_CONTEXT_PTR): Script SetPoint and SetAllPoints store the top itself
// as the relative layout for a nil relativeTo, and the XML loader anchors a
// parentless frame to it. The stock GetPoint turns every relative layout into
// a region with `layout - OFF_REGION_ANCHOR`, so for a screen anchor it reads
// the 36 bytes before the top object as a region header: a refcount at +4
// (0 → it registers that memory as a script object, writing a Lua ref into
// it) and a registry index at +8 (whatever registry[that] holds comes back as
// `relativeTo`). 3.3.5's Script_GetPoint (0x0049D950) and 4.3.4's test
// `relative == CSimpleTop::s_instance` and push nil; so does this. The rest
// is the engine's own walk: the index-th set slot in point order, names from
// FUN_FRAME_POINT_TO_STRING, the target through the node's getter, offsets
// through the inverse of SetPoint's conversion (Game::InternalToPixel), three
// returns for a node without a target and five otherwise.
//
// DANGLING TARGETS. A node's relativeTo is a raw pointer with no weak
// reference. The layout destructor CLayoutFrame::DestroyLayout (0x00768E20)
// clears the points of every dependent that names the dying frame
// (benilla-re ui/anchors "Dependents"), so an anchor made through SetPoint
// does not outlive its target. For a target freed by a path that skips that
// destructor, the pre-scan hands the dead target to the engine's own
// FUN_REGION_CLEAR_POINT_BY_RELATIVE — the removal SetPoint runs when a
// target is replaced — which frees the node and NULLs the slot by pointer
// comparison, never dereferencing the target. The node is deleted rather
// than its pointer NULLed because a live relative node with relativeTo == 0
// is a state the engine never creates: the layout readers GetWidth /
// GetHeight (the node's vtable[1] / [2]) would fault on `[0 + 0x3C]`. 3.3.5
// restructured the node instead (a 16-byte CFramePoint with a flags word,
// bit 0x800 set by CFramePoint::Invalidate on target destruction and tested
// by every reader); 1.12's node has no flags word, so the empty slot is its
// only representation of a dead anchor.
//
// Reproduce / verify with `_classicapi_ArmGetPointCrash` (debug/GetPointCrash.cpp):
//   /run TestF=CreateFrame("Frame","TestF",UIParent); TestF:SetPoint("CENTER"); _classicapi_ArmGetPointCrash(TestF)
//   /run print(TestF:GetPoint(1))   -- pre-fix: #132 ; post-fix: nil (anchor cleaned)

#include "frame/GetPointGuard.h"

#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"

#include <cstdint>

#include <windows.h>

namespace Frame::GetPointGuard {

namespace {

using ClearPointByRelative_t = void(__thiscall *)(void *layout, void *relativeTo,
                                                  int doRelayout);
using GetRelative_t = void *(__thiscall *)(void *node);
using PointToString_t = const char *(__fastcall *)(int point);

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

} // namespace

// Walk `region`'s anchor slots and drop any whose relativeTo target is dead,
// so the reader below (and the layout readers) only ever see live anchors.
void CleanDanglingAnchors(void *region) {
    auto *base = static_cast<uint8_t *>(region);
    auto *top = Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
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
        if (rel == nullptr || rel == top)
            continue; // the top is the screen, not a region: nothing to probe
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

int PushPoint(void *L, void *region, int point) {
    auto *node = Game::Read<uint8_t *>(region, Offsets::OFF_REGION_ANCHOR_ARRAY + point * 4);
    if (node == nullptr)
        return 0;

    auto pointName = reinterpret_cast<PointToString_t>(Offsets::FUN_FRAME_POINT_TO_STRING);
    auto *vtable = Game::Read<uintptr_t *>(node, 0);
    auto *relative = static_cast<uint8_t *>(reinterpret_cast<GetRelative_t>(
        vtable[Offsets::VTBL_ANCHOR_NODE_GET_RELATIVE / sizeof(uintptr_t)])(node));
    const double x = Game::InternalToPixel(Game::Read<float>(node, Offsets::OFF_ANCHOR_NODE_X));
    const double y = Game::InternalToPixel(Game::Read<float>(node, Offsets::OFF_ANCHOR_NODE_Y));

    Game::Lua::PushString(L, pointName(point));
    if (relative == nullptr) {
        Game::Lua::PushNumber(L, x);
        Game::Lua::PushNumber(L, y);
        return 3;
    }
    if (relative == Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR))
        Game::Lua::PushNil(L); // anchored to the screen
    else
        Game::Lua::PushObject(L, relative - Offsets::OFF_REGION_ANCHOR);
    Game::Lua::PushString(
        L, pointName(Game::Read<int>(node, Offsets::OFF_ANCHOR_NODE_RELATIVE_POINT)));
    Game::Lua::PushNumber(L, x);
    Game::Lua::PushNumber(L, y);
    return 5;
}

namespace {

int __fastcall GetPoint_h(void *L) {
    // The engine's own prologue and errors (same type id, 0x00CF0C3C).
    void *region = Game::Lua::ResolveRegion(L, 1);
    if (region == nullptr)
        return 0;
    CleanDanglingAnchors(region);

    int index = 0;
    if (Game::Lua::IsNumber(L, 2))
        index = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;

    // The index-th set slot, in point order (TOPLEFT .. BOTTOMRIGHT).
    for (int point = 0, found = 0; point < Offsets::REGION_ANCHOR_SLOT_COUNT; ++point) {
        if (Game::Read<uint8_t *>(region, Offsets::OFF_REGION_ANCHOR_ARRAY + point * 4) == nullptr)
            continue;
        if (found >= index)
            return PushPoint(L, region, point);
        ++found;
    }
    return 0;
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
