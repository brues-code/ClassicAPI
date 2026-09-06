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

// A zero-dimension texture must not hang the client.
//
// The engine's mip-chain byte total, FUN_GX_TEXTURE_MIP_BYTES, walks a texture's
// levels by halving both dimensions until each reaches exactly 1. Every halving
// is gated by `JBE`, so a dimension of 0 never shifts and never reaches 1: the
// loop spins forever, doing no allocation and no I/O. It presents as a hard
// freeze at 100% of one core, and the client never recovers. The full
// instruction-level trail is in Offsets.h at FUN_GX_TEXTURE_MIP_BYTES.
//
// The engine already knows the right answer for a texture with no mip chain --
// one branch above the loop, `if ((flags & 7) < 2) return`, returns the base
// level's bytes and stops. A degenerate dimension deserves the same treatment,
// and that is all this guard arranges: it resolves the caller's arguments the
// way the engine does, and when either dimension would shift to zero it raises
// that one to the smallest value the accounting can express (`1 << reduce`)
// before calling the original. The loop then terminates on its first pass and
// returns the base-level size. Nothing is reimplemented -- the mip math, the
// format block tables and the reduction level all stay the engine's.
//
// Both callers are covered, because the clamp sits at the entry rather than at
// either call site: FUN_00594D90 stamps a whole texture once per frame as it is
// bound to a stage, and FUN_00594C80 accounts for a lock by passing a dirty
// RECT's width and height, which hangs identically when the rect is empty but
// still flagged.
//
// WHO FEEDS IT A ZERO IS A SEPARATE QUESTION, and the guard deliberately does
// not bury it. ClassicAPI constructs no textures -- Texture::ColorTexture is an
// alias onto an engine handler, and nothing in src/ calls the texture
// constructor -- and Texture::DimensionGate cannot admit a zero it would
// otherwise reject, since the validator's power-of-two test is
// `x & (x - 1); NEG; SBB; INC`, which already yields "is a power of two" for 0
// on a stock client. So the first occurrence is logged with everything readable
// off the texture, including whether the mask path had any stage of its own
// bound at the time. That answers "is this ours" from a single log line, which
// matters because whoever hits the freeze is not necessarily whoever can
// reproduce it on demand.
//
// A dimension can also reach zero without the texture being 0x0, by way of the
// reduction level -- which is why the guard tests the SHIFTED value rather than
// the raw one, and clamps to `1 << reduce` rather than to 1. That level is a
// caps field (the caps block is device + 0x23C per FUN_005923D0, so this is
// caps + 0xE0), so it comes from the graphics backend rather than from the
// engine. Measured 0 on a native D3D9 driver, where it makes the loop safe on
// its own; unmeasured under wined3d, where the freeze this guards was caught.
// The guard is correct either way, which is the point of clamping against the
// live value instead of assuming one.

#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"
#include "texture/Mask.h"

#include <cstdint>

namespace Texture::MipAccounting {
namespace {

// What both callers pass to mean "read the texture's own dimensions".
constexpr uint32_t kOwnDimension = 0xFFFFFFFFu;

// __thiscall wired as __fastcall with the ignored EDX slot: ECX carries the
// device and the three declared tail arguments land on the stack, matching the
// callee's RET 0xC.
using MipBytes_t = int(__fastcall *)(void *device, void *edxUnused, void *texture, uint32_t width,
                                     uint32_t height);
MipBytes_t MipBytes_o = nullptr;

bool g_reported = false;

// One line, once per process. The guard makes every later occurrence harmless,
// and a per-frame line would bury the first one.
void Report(const void *texture, uint32_t width, uint32_t height, uint32_t reduce) {
    if (g_reported)
        return;
    g_reported = true;
    const uint32_t flags = Game::Read<uint32_t>(texture, Offsets::OFF_GXTEXTURE_FLAGS);
    Debug::Log::Printf(
        "[mipaccount] degenerate texture %ux%u (reduce=%u) tex=0x%08X fmt=%u flags=0x%08X "
        "mipMode=%u maskPathActive=%d -- clamped; the engine's mip loop would not have "
        "terminated",
        width, height, reduce, static_cast<unsigned>(reinterpret_cast<uintptr_t>(texture)),
        Game::Read<uint32_t>(texture, Offsets::OFF_GXTEXTURE_FORMAT), flags, flags & 7,
        Texture::Mask::AnyMaskActive() ? 1 : 0);
}

int __fastcall MipBytes_h(void *device, void *edxUnused, void *texture, uint32_t width,
                          uint32_t height) {
    if (device != nullptr && texture != nullptr) {
        // Resolve the sentinels exactly as the original does, so the test sees
        // the same numbers the loop would.
        uint32_t w = width;
        uint32_t h = height;
        if (w == kOwnDimension)
            w = Game::Read<uint32_t>(texture, Offsets::OFF_GXTEXTURE_WIDTH);
        if (h == kOwnDimension)
            h = Game::Read<uint32_t>(texture, Offsets::OFF_GXTEXTURE_HEIGHT);
        // The shift the accounting itself applies; the CPU masks it to 5 bits.
        const uint32_t reduce =
            Game::Read<uint32_t>(device, Offsets::OFF_GXDEV_TEXTURE_REDUCE) & 31;
        if ((w >> reduce) == 0 || (h >> reduce) == 0) {
            Report(texture, w, h, reduce);
            const uint32_t smallest = 1u << reduce;
            return MipBytes_o(device, edxUnused, texture, (w >> reduce) == 0 ? smallest : w,
                              (h >> reduce) == 0 ? smallest : h);
        }
    }
    // Untouched in every ordinary case -- the original receives the caller's own
    // arguments, sentinels included.
    return MipBytes_o(device, edxUnused, texture, width, height);
}

const Game::HookAutoRegister _hook{Offsets::FUN_GX_TEXTURE_MIP_BYTES,
                                   reinterpret_cast<void *>(&MipBytes_h),
                                   reinterpret_cast<void **>(&MipBytes_o)};

} // namespace
} // namespace Texture::MipAccounting
