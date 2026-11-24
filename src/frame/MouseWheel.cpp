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

// The mouse wheel on textures and font strings (Frame::RegionInput).
//
// 1.12's wheel handler (ADDR_UI_WHEEL_HIT_TEST, ADDR_UI_WHEEL_TAIL) offers the
// wheel to each frame of the wheel lists under the cursor, from the top, until
// one answers; a frame answers when it is visible and has an OnMouseWheel
// handler (its slot 0x0076C180). 3.80.2 sorts the regions that take the wheel
// in with the frames by draw order. So the frame walk's hit test first offers
// the wheel to the regions under the cursor that draw over that frame
// (RegionInput::Above), and the walk's tail to the ones left; a region
// answers as the frame does, when it has an OnMouseWheel handler, which runs
// with the frame's delta, 1 or -1. A region that answers ends the walk with
// the wheel taken.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "frame/RegionInput.h"
#include "frame/RegionScripts.h"

#include <cstdint>
#include <vector>

namespace Frame::MouseWheel {

namespace {

using HitTest_t = int(__fastcall *)(void *frame, void *edx, const float *point);

// The regions of the walk in progress, top-first, and how many were offered.
std::vector<void *> g_regions;
size_t g_next = 0;
bool g_walking = false;

void Begin() {
    if (g_walking)
        return;
    g_walking = true;
    g_next = 0;
    g_regions.clear();
    uint8_t *ctx = Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
    if (ctx == nullptr || !Frame::RegionInput::Any())
        return;
    float point[2];
    Frame::RegionInput::Cursor(ctx, point);
    Frame::RegionInput::Candidates(Frame::RegionInput::WHEEL, point, 0, g_regions);
}

// Offers the wheel to the regions not yet offered that draw over `frame`
// (all of them for null); true when one took it.
bool OfferRegions(const void *frame, const void *event) {
    while (g_next < g_regions.size() && (frame == nullptr || Frame::RegionInput::Above(g_regions[g_next], frame))) {
        void *region = g_regions[g_next++];
        if (!Frame::RegionScripts::Has(region, Frame::RegionScripts::ON_MOUSE_WHEEL))
            continue;
        const double delta = Game::Read<int>(event, Offsets::OFF_UI_MOUSE_EVENT_WHEEL_DELTA) >= 0 ? 1.0 : -1.0;
        Frame::RegionScripts::Run(region, Frame::RegionScripts::ON_MOUSE_WHEEL, [delta](void *L) {
            Game::Lua::PushNumber(L, delta);
            return 1;
        });
        return true;
    }
    return false;
}

// The walk's hit test of `frame`: 2 when a region over it took the wheel,
// else the engine's answer.
int __stdcall HitTest(const void *event, void *frame, const float *point) {
    Begin();
    if (OfferRegions(frame, event))
        return 2;
    return reinterpret_cast<HitTest_t>(Offsets::FUN_FRAME_HIT_TEST)(frame, nullptr, point);
}

// The walk's end: the regions under every frame, when no frame answered.
int __stdcall Tail(int answered, const void *event) {
    Begin();
    if (answered == 0 && OfferRegions(nullptr, event))
        answered = 1;
    g_walking = false;
    g_regions.clear();
    return answered;
}

// Replaces `call FUN_FRAME_HIT_TEST`: the frame in ecx, the point pushed
// (`ret 4` takes it), the event copy at ebp-0x40. A region's answer sets ebx,
// the walk's answer, and reports a miss, so the loop ends.
__declspec(naked) void HitTestThunk() {
    __asm {
        push dword ptr [esp + 4]
        push ecx
        lea eax, [ebp - 40h]
        push eax
        call HitTest
        cmp eax, 2
        jne done
        mov ebx, 1
        xor eax, eax
    done:
        ret 4
    }
}

// Replaces the tail's `pop edi; xor eax,eax; test ebx,ebx`, doing them after
// the call so the `sete` that follows reads ebx.
__declspec(naked) void TailThunk() {
    __asm {
        lea eax, [ebp - 40h]
        push eax
        push ebx
        call Tail
        mov ebx, eax
        pop ecx
        pop edi
        xor eax, eax
        test ebx, ebx
        push ecx
        ret
    }
}

const uint8_t kTail[] = {0x5F, 0x33, 0xC0, 0x85, 0xDB};

void RegisterLuaFunctions() {
    Common::RedirectCall(Offsets::ADDR_UI_WHEEL_HIT_TEST, Offsets::FUN_FRAME_HIT_TEST,
                         reinterpret_cast<const void *>(&HitTestThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_WHEEL_TAIL, kTail, sizeof kTail, reinterpret_cast<const void *>(&TailThunk));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::MouseWheel
