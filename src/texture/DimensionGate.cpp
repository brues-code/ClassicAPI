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

// Texture dimension gates — let any texture the GPU can hold load, whatever its
// size or shape, without ever letting the decoder outrun its buffer.
//
// THE INVARIANT. Every texture decode in this engine writes into ONE global
// scratch buffer (VAR_TEXTURE_DECODE_SCRATCH), allocated once at boot for a
// single square texture of side S (0x200 stock; 0x400 once VanillaHelpers has
// patched the boot immediates) and written with no bounds check. So the rule
// every client version upholds is: nothing may reach the decoder larger than
// S on either axis. They just enforce it in different places:
//
//   1.12   allocator FUN_00448450: `< 1024` AND power-of-two, together => <= 512.
//          The create validator's device-caps max is raw hardware (thousands) and
//          protects nothing — a 2560x1080 sailed through it and faulted in the
//          decoder's pixel writer at 0x004492E5.
//   3.3.5  scratch 1024; the allocator's size test is demoted to pool
//          eligibility ([32,512]) and the VALIDATOR carries the bound, because
//          3.3.5 clamps its caps max to 1024. Verified in FUN_004b7f80 /
//          FUN_00681d90 / FUN_004b5bb0. Not grow-on-demand — the same fixed
//          buffer, one size up. VanillaHelpers' patch set is, in effect, this
//          exact backport (size 0x400, scratch 0x400, pool 6x6).
//
// Two consequences this module is built around. The POT rule was PART of 1.12's
// size bound, so relaxing it alone exposes a 600x600 overrun on a stock client.
// And the POT jumps sit ABOVE the size CMPs in the allocator, so retargeting a
// POT failure straight to the pool-skip tail jumps OVER the bound.
//
// DESIGN — no numbers of our own. A fixed cap only exists because the buffer is
// sized up front; size it on demand and the cap disappears. Every dimension
// failure in the allocator — POT or size — is routed into a 24-byte code cave in
// a page we own, which hands the texture's width and height to EnsureScratch:
//
//     PUSHAD ; MOV ECX,EBX ; MOV EDX,EAX ; CALL EnsureScratch
//     TEST AL,AL ; POPAD ; JZ reject ; JMP pool-skip
//
// EnsureScratch refuses anything over the device's own max texture dimension —
// the caps field the create validator compares against, so we never grow for a
// texture the engine would refuse a moment later — and otherwise replaces the
// scratch with one that fits, through the engine's own decode-buffer
// constructor FUN_TEXTURE_DECODE_BUFFER_ALLOC (the routine both decoders use
// when the scratch is NULL). The scratch is a high-water mark: someone who never
// loads anything over 1024 pays nothing. The in-function size CMPs decide only
// "poolable" (the smaller of what the recycle pool can index and the boot
// scratch). That is 3.3.5's allocator/validator split, with the validator's
// bound honoured up front instead of relying on a caps clamp 1.12 doesn't have.
//
// Layers:
//   1. Pool eligibility follows the engine (always on). The pool limit is read
//      back from the boot immediates and the pool-count immediate, so it is
//      right on any DLL stack — the stock bucket table ends exactly at the
//      scratch pointer, so an index it can't hold reads the pointer as a list.
//   2. VanillaHelpers' pool-index stride (auto, when its 6x6 table is
//      detected). Their table is 6 wide but the index arithmetic stayed at
//      stride 5, so a 1024 dimension collides with a 32-high bucket and the
//      wrong-size texture is recycled: 32x1024 rendered nothing, 64x1024
//      worked by luck. Both index sites are rewritten to stride 6.
//   3. Grow on demand (always on) — the cave above. This is enough for TGA,
//      which allocates first and decodes later at upload.
//   4. BLP grows BEFORE it decompresses (always on). The .blp loader writes
//      the decoded image into the scratch and only then calls the allocator, so
//      the cave is too late for it — a 2048x2048 BLP has always overrun the
//      scratch on this client. A pre-hook on the decompressor FUN_BLP_DECOMPRESS,
//      which carries the dimensions itself, grows the scratch first — and
//      repoints the caller's already-copied slot at the new buffer, since the
//      decompressor writes through that copy — or reports "decompression
//      failed" for a texture the GPU can't hold. It acts only when the slot IS
//      the scratch: the IMG path decompresses into a private, right-sized
//      buffer of its own and is left untouched.
//   6. The recycle-pool free follows the same rule (always on). The free path
//      files a texture into a bucket with no upper size check and no
//      power-of-two check; the cave lets both kinds exist. Freeing an oversized
//      one overran the bucket table (a 2048x2048 crashed at teardown); freeing
//      an NPOT one filed it under its power-of-two factor, where the next POT
//      request of that size was handed the wrong-sized texture. A hook routes
//      anything the allocator would not have pooled to the non-pooled free —
//      the mirror of the allocator's cave, and the missing half of the gate.
//   7. The decoded-buffer cache follows it too (always on). Its alloc and free
//      bucket by power-of-two factor the same way, so an NPOT BLP on the model /
//      character cache path could be handed a smaller buffer and decode past
//      it. NPOT sizes take the engine's own uncached alloc and free, as sizes
//      outside the cache's 8..256 window already do.
//   Minimum side (always on). The create validator refuses sides below 8; it
//      now refuses only 0, so a 1x1 or 4x4 texture loads.
//   5. The async fallback guard (always on). FUN_TEXTURE_FORCE_LOAD shoves a
//      read that never fit the shared async buffer into a 512 KiB static
//      fallback with no size check — a pre-existing engine bug the stock size
//      gate merely hid, and the actual mechanism behind "big BLPs crash". The
//      hook refuses to force a request larger than the fallback; the texture
//      stays blank. So a BLP FILE larger than the shared buffer (2 MiB stock,
//      32 MiB with VanillaHelpers) does not load — an uncompressed BLP only;
//      DXT stays far under, and TGA is synchronous. Growing the fallback to fit
//      was tried and reverted (a reader-thread fault, not yet isolated).
//   NPOT (always on). POT failures go to the cave (fit, then fresh allocation
//      — never the pool, whose log2 index is meaningless for NPOT), and the
//      validator's own POT re-test is forced true. Verified in-game for the
//      TGA/UI path (clamped UVs, single mip); NPOT BLPs with DXT blocks or mip
//      chains are untested.
//
// Applied from module registration — glue boot and every FrameXML init — which
// is after every other DLL's load-time patching, so the values read back are
// the ones actually in force. The guard hook installs with the rest of the
// DLL's hooks.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace Texture::DimensionGate {

namespace {

// ---- byte helpers ------------------------------------------------------------

uint32_t ReadImm32(uintptr_t addr) { return *reinterpret_cast<const uint32_t *>(addr); }

void Put32(uint8_t *dst, uint32_t v) { std::memcpy(dst, &v, sizeof v); }

// rel32 for a jump/call whose NEXT instruction begins at `next`.
uint32_t Rel32(uintptr_t next, uintptr_t target) {
    return static_cast<uint32_t>(static_cast<int32_t>(target - next));
}

bool WriteImm32(uintptr_t site, uint32_t v) {
    return Common::PatchBytes(reinterpret_cast<void *>(site), &v, sizeof v);
}

// Rewrite a 6-byte `0F <cond> rel32` near jump in engine code. The condition is
// written explicitly, never preserved — VanillaHelpers may have flipped it.
bool WriteNearJump(uintptr_t site, uint8_t cond, uintptr_t target) {
    uint8_t bytes[6] = {0x0F, cond, 0, 0, 0, 0};
    Put32(bytes + 2, Rel32(site + 6, target));
    return Common::PatchBytes(reinterpret_cast<void *>(site), bytes, sizeof bytes);
}

// 0 counts as a power of two, as in every engine test of `v & (v - 1)`.
bool IsPowerOfTwo(uint32_t v) { return (v & (v - 1)) == 0; }

// ---- what the engine was actually configured with ----------------------------

// Side of the decode scratch. Read back from the two immediates FUN_00448BD0
// feeds FUN_GX_FORMAT_IMAGE_BYTES, so this is what the buffer is REALLY sized
// to, whoever last set them — the engine at boot, VanillaHelpers, or
// GrowScratch below. A mismatch is someone else's bug; the smaller is the only
// safe answer.
uint32_t ScratchDimension() {
    const uint32_t w = ReadImm32(Offsets::PATCH_TEXDECODE_SCRATCH_DIM_W + 1);
    const uint32_t h = ReadImm32(Offsets::PATCH_TEXDECODE_SCRATCH_DIM_H + 1);
    return w < h ? w : h;
}

// Recycle-pool side, from its init-count immediate: 25 = stock 5x5, 36 =
// VanillaHelpers' 6x6. 0 = a layout we don't recognise.
int PoolSide() {
    switch (ReadImm32(Offsets::PATCH_TEXPOOL_INIT_COUNT + 1)) {
    case 25:
        return 5;
    case 36:
        return 6;
    default:
        return 0;
    }
}

// Largest dimension the pool can index: buckets are keyed by log2(dim >> 5)
// over 0..side-1. An unknown layout is treated as stock.
uint32_t PoolMaxDimension(int side) { return 32u << ((side > 0 ? side : 5) - 1); }

// The GPU's own maximum texture dimension for 2D textures — the field the
// create validator compares against. 0 if the device isn't up.
uint32_t DeviceMaxDimension() {
    const auto *device = *reinterpret_cast<const uint8_t *const *>(Offsets::VAR_GX_DEVICE);
    if (device == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(device + Offsets::OFF_GXDEV_CAPS_MAX_TEX_DIM);
}

// ---- layer 3: the scratch grows to fit ----------------------------------------

constexpr const char *kAllocTag = "ClassicAPI/texture/DimensionGate.cpp";

using DecodeBufferAlloc_t = void *(__fastcall *)(int format, uint32_t width, uint32_t height,
                                                 const char *file, uint32_t line);
using SMemFree_t = void(__stdcall *)(void *buf, const char *file, int line, int flags);

void *g_retiredScratch = nullptr; // the previous scratch, freed at the next growth

// Replace the scratch with one sized for `side` x `side` (format 2, what the
// boot alloc uses), through the engine's own decode-buffer constructor. Safe
// from any main-thread context that is not itself a texture decode: every
// reader re-reads VAR_TEXTURE_DECODE_SCRATCH per decode, and the old buffer's
// only other reference is the exit teardown, which SMemFree's whatever the slot
// holds. Never shrinks. Records the new side in the boot immediates so
// ScratchDimension() — and every later registration — reads it back.
bool GrowScratch(uint32_t side) {
    if (side <= ScratchDimension())
        return true;
    auto alloc = reinterpret_cast<DecodeBufferAlloc_t>(Offsets::FUN_TEXTURE_DECODE_BUFFER_ALLOC);
    void *fresh = alloc(2, side, side, kAllocTag, __LINE__);
    if (fresh == nullptr)
        return false;
    auto **slot = reinterpret_cast<void **>(Offsets::VAR_TEXTURE_DECODE_SCRATCH);
    void *old = *slot;
    *slot = fresh;
    // Retire the old buffer one generation late. Every consumer re-reads the
    // global per decode, and BlpDecompress_h repoints the one slot that was
    // copied before the growth — but keeping exactly one retired buffer alive
    // means any holder not accounted for reads stale bytes instead of unmapped
    // memory. Bounded: one buffer, freed at the next growth.
    if (g_retiredScratch != nullptr)
        reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE)(g_retiredScratch, kAllocTag,
                                                                  __LINE__, 0);
    g_retiredScratch = old;
    WriteImm32(Offsets::PATCH_TEXDECODE_SCRATCH_DIM_W + 1, side);
    WriteImm32(Offsets::PATCH_TEXDECODE_SCRATCH_DIM_H + 1, side);
    return true;
}

// Called from the cave for every texture the allocator's own tests turned away
// (not a power of two, or larger than the pool can index). Returns false to
// reject — only for what the GPU itself can't hold — otherwise makes sure the
// scratch can take it and lets it through to a fresh allocation. Runs on the
// main thread inside FUN_00448450, which is never a decode in progress.
bool __fastcall EnsureScratch(uint32_t width, uint32_t height) {
    const uint32_t deviceMax = DeviceMaxDimension();
    if (deviceMax == 0 || width > deviceMax || height > deviceMax)
        return false;
    return GrowScratch(width > height ? width : height);
}

// ---- the code cave -----------------------------------------------------------
//
//   +0   60             PUSHAD
//   +1   8B CB          MOV ECX,EBX         ; EBX = width  at every entry site
//   +3   8B D0          MOV EDX,EAX         ; EAX = height
//   +5   E8 rel32       CALL EnsureScratch
//   +10  84 C0          TEST AL,AL
//   +12  61             POPAD               ; does not touch EFLAGS
//   +13  0F 84 rel32    JZ  reject
//   +19  E9 rel32       JMP pool-skip       ; fresh allocation, descriptor is complete
//
// Entered only by jumps from inside FUN_00448450, where EBX/EAX still hold the
// dimensions, EDI is 0 and [EBP-0x2C] is the finished descriptor — everything
// the pool-skip tail expects. PUSHAD/POPAD keep every register the engine still
// needs; flags are clobbered and nothing downstream reads them. Lives for the
// process: engine code points into it.

uint8_t *g_cave = nullptr;
constexpr size_t kCaveSize = 24;

bool BuildCave() {
    if (g_cave == nullptr) {
        g_cave = static_cast<uint8_t *>(
            VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (g_cave == nullptr)
            return false;
    }
    const auto base = reinterpret_cast<uintptr_t>(g_cave);
    uint8_t c[kCaveSize];
    c[0] = 0x60;
    c[1] = 0x8B, c[2] = 0xCB;
    c[3] = 0x8B, c[4] = 0xD0;
    c[5] = 0xE8;
    Put32(c + 6, Rel32(base + 10, reinterpret_cast<uintptr_t>(&EnsureScratch)));
    c[10] = 0x84, c[11] = 0xC0;
    c[12] = 0x61;
    c[13] = 0x0F, c[14] = 0x84;
    Put32(c + 15, Rel32(base + 19, Offsets::VA_TEXALLOC_REJECT));
    c[19] = 0xE9;
    Put32(c + 20, Rel32(base + 24, Offsets::VA_TEXALLOC_POOL_SKIP));
    std::memcpy(g_cave, c, kCaveSize);
    FlushInstructionCache(GetCurrentProcess(), g_cave, kCaveSize);
    return true;
}

uintptr_t Cave() { return reinterpret_cast<uintptr_t>(g_cave); }

// ---- state -------------------------------------------------------------------

uint32_t g_poolMax = 0; // what the in-function CMPs admit to the pool
int g_poolSide = 0;
bool g_strideFixed = false;

// ---- layer 1: pool eligibility follows the engine -----------------------------

bool ApplySizeGate() {
    g_poolSide = PoolSide();
    const uint32_t indexable = PoolMaxDimension(g_poolSide);
    const uint32_t scratch = ScratchDimension();
    // A texture that enters the pool never reaches the cave, so it must also
    // fit the scratch as it stands. (A VanillaHelpers install whose scratch
    // patch failed but whose pool patch didn't would otherwise let 1024 into a
    // 512 buffer.)
    g_poolMax = scratch < indexable ? scratch : indexable;
    if (!BuildCave())
        return false;
    bool ok = WriteImm32(Offsets::PATCH_TEXALLOC_SIZE_W_IMM, g_poolMax);
    ok = WriteImm32(Offsets::PATCH_TEXALLOC_SIZE_H_IMM, g_poolMax) && ok;
    // JA: strictly greater than the pool max leaves the pool path — so exactly
    // poolMax (1024 on a VanillaHelpers stack) remains poolable.
    ok = WriteNearJump(Offsets::PATCH_TEXALLOC_SIZE_W_JMP, 0x87, Cave()) && ok;
    ok = WriteNearJump(Offsets::PATCH_TEXALLOC_SIZE_H_JMP, 0x87, Cave()) && ok;
    return ok;
}

// ---- layer 2: VanillaHelpers' pool-index stride -------------------------------

// `IMUL r,r,6 ; ADD r,other` in place of `LEA t,[other + r*4] ; ADD r,t`:
// same length, same operand roles, stride 5 -> 6. See Offsets.h.
constexpr uint8_t kStride6Alloc[5] = {0x6B, 0xF6, 0x06, 0x03, 0xF3}; // IMUL ESI,ESI,6; ADD ESI,EBX
constexpr uint8_t kStride6Free[5] = {0x6B, 0xDB, 0x06, 0x03, 0xD8};  // IMUL EBX,EBX,6; ADD EBX,EAX

bool ApplyStrideFix() {
    if (g_poolSide != 6) {
        g_strideFixed = false;
        return true; // stock 5x5 (or unknown): stride 5 is correct, leave it
    }
    bool ok = Common::PatchBytes(reinterpret_cast<void *>(Offsets::PATCH_TEXPOOL_INDEX_STRIDE_ALLOC),
                                 kStride6Alloc, sizeof kStride6Alloc);
    ok = Common::PatchBytes(reinterpret_cast<void *>(Offsets::PATCH_TEXPOOL_INDEX_STRIDE_FREE),
                            kStride6Free, sizeof kStride6Free) &&
         ok;
    g_strideFixed = ok;
    return ok;
}

// ---- layer 4: BLP grows the scratch before it decompresses ---------------------

using BlpDecompress_t = int(__fastcall *)(void *reader, void *edxUnused, int format,
                                          int *scratchSlot, uint32_t mipBase, int flag);
BlpDecompress_t BlpDecompress_o = nullptr;

// __thiscall wired as __fastcall with the ignored EDX slot. The reader holds the
// level-0 dimensions; `mipBase` is how many levels the BLP pre-scale skipped, so
// the level actually written is `>> mipBase` (floored at 1, as the engine does).
// Returning 0 is the decompressor's own failure code: the loader logs
// "decompression failed" and the texture stays blank.
int __fastcall BlpDecompress_h(void *reader, void *edxUnused, int format, int *scratchSlot,
                               uint32_t mipBase, int flag) {
    // Only a caller decompressing into the SHARED scratch concerns us. The .blp
    // path (FUN_0044A560) copies VAR_TEXTURE_DECODE_SCRATCH into its slot before
    // calling in; the IMG path (FUN_00449840) hands in a private buffer it already
    // sized for the image through FUN_0044AF90, and must be left alone — its
    // release path frees whatever the slot holds.
    const auto *scratchVar = reinterpret_cast<const int *>(Offsets::VAR_TEXTURE_DECODE_SCRATCH);
    if (scratchSlot != nullptr && *scratchSlot != 0 && *scratchSlot == *scratchVar) {
        const auto *r = static_cast<const uint8_t *>(reader);
        uint32_t w = *reinterpret_cast<const uint32_t *>(r + Offsets::OFF_BLPREADER_WIDTH) >>
                     (mipBase & 31);
        uint32_t h = *reinterpret_cast<const uint32_t *>(r + Offsets::OFF_BLPREADER_HEIGHT) >>
                     (mipBase & 31);
        if (w == 0)
            w = 1;
        if (h == 0)
            h = 1;
        if (!EnsureScratch(w, h))
            return 0;
        // The decompressor — and the upload after it — work through the caller's
        // copy, not the global. If the scratch just grew, the copy still names
        // the retired buffer; repoint it.
        *scratchSlot = *scratchVar;
    }
    return BlpDecompress_o(reader, edxUnused, format, scratchSlot, mipBase, flag);
}

const Game::HookAutoRegister _blpDecompressHook{Offsets::FUN_BLP_DECOMPRESS, &BlpDecompress_h,
                                                reinterpret_cast<void **>(&BlpDecompress_o)};

// ---- layer 5: the async fallback guard ----------------------------------------

using ForceLoad_t = void(__fastcall *)(void *hTexture);
ForceLoad_t ForceLoad_o = nullptr;

// A request that never fit the shared async buffer is about to be forced into
// the 512 KiB static fallback with no size check (see Offsets.h) — the engine
// bug behind "big BLPs crash". Refuse when it cannot fit: the request stays
// pending and the texture stays blank, the normal not-loaded state every caller
// already handles, instead of overrunning the pool table.
//
// Growing the fallback to fit — the symmetric move to the scratch — was tried
// and reverted: the read runs on the engine's async reader thread, which has no
// crash handler, and a read into a right-sized heap buffer froze then killed the
// process where the same read into the oversized shared buffer does not. The
// fault is on that thread and not yet isolated, so this stops at the safe
// refusal. Consequence: a BLP FILE larger than the shared async buffer (2 MiB
// stock, 32 MiB with VanillaHelpers) does not load. That is only an
// UNCOMPRESSED BLP over ~1.4 MB / ~22 MB of image; DXT stays far under the
// buffer, and TGA is synchronous and never on this path — so it costs only a
// pathological input.
void __fastcall ForceLoad_h(void *hTexture) {
    const auto *req = *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(hTexture) + Offsets::OFF_HTEXTURE_ASYNC_REQUEST);
    if (req != nullptr && req[Offsets::OFF_ASYNCREQ_IN_SHARED] == 0 &&
        *reinterpret_cast<const uint32_t *>(req + Offsets::OFF_ASYNCREQ_SIZE) >
            Offsets::ASYNC_FALLBACK_BUFFER_SIZE)
        return;
    ForceLoad_o(hTexture);
}

const Game::HookAutoRegister _forceLoadHook{Offsets::FUN_TEXTURE_FORCE_LOAD, &ForceLoad_h,
                                            reinterpret_cast<void **>(&ForceLoad_o)};

// ---- layer 6: the recycle-pool free follows the same bound ---------------------

using PoolFree_t = void(__fastcall *)(void *tex);
PoolFree_t PoolFree_o = nullptr;
using TexGetDesc_t = void(__fastcall *)(void *tex, uint32_t *out);
using TexFreeRaw_t = void(__fastcall *)(void *tex);

// The pool free files a texture into a bucket keyed by each dimension's
// power-of-two factor, with no upper bound and no power-of-two test (see
// Offsets.h). The allocator's cave lets both oversized and NPOT textures exist:
// an oversized one files past the bucket table and writes through garbage, an
// NPOT one files under a POT size and is recycled for it. Pool here exactly
// what the allocator pools — a power of two on both sides, no larger than
// g_poolMax — and route the rest to the engine's own non-pooled free, the
// guard's else-branch for too-small / too-many-mip textures. Cold path (texture
// teardown), so a MinHook here carries no per-frame concern, and nothing else
// patches this function's code.
void __fastcall PoolFree_h(void *tex) {
    if (g_poolMax != 0 && tex != nullptr) {
        uint32_t desc[16] = {0};
        reinterpret_cast<TexGetDesc_t>(Offsets::FUN_TEXTURE_GET_DESC)(tex, desc);
        const uint32_t w = desc[Offsets::OFF_TEXDESC_WIDTH / 4];
        const uint32_t h = desc[Offsets::OFF_TEXDESC_HEIGHT / 4];
        if (!IsPowerOfTwo(w) || !IsPowerOfTwo(h) || w > g_poolMax || h > g_poolMax) {
            reinterpret_cast<TexFreeRaw_t>(Offsets::FUN_TEXTURE_FREE_RAW)(tex);
            return;
        }
    }
    PoolFree_o(tex);
}

const Game::HookAutoRegister _poolFreeHook{Offsets::FUN_TEXTURE_POOL_FREE, &PoolFree_h,
                                           reinterpret_cast<void **>(&PoolFree_o)};

// ---- layer 7: the decoded-buffer cache follows the same rule -------------------

using AllocMips_t = void *(__fastcall *)(uint32_t format, uint32_t width, uint32_t height);
using FreeMips_t = void(__fastcall *)(void *buffer, uint32_t format, uint32_t width,
                                      uint32_t height);
AllocMips_t AllocMips_o = nullptr;
FreeMips_t FreeMips_o = nullptr;

// The cache buckets by power-of-two factor (see Offsets.h), so an NPOT size is
// neither cached nor served from it: it takes the same uncached alloc and free
// the engine uses outside the cache's 8..256 window. Both halves must agree, or
// a buffer allocated here would be filed by the original free. Once per texture
// load and unload.
void *__fastcall AllocMips_h(uint32_t format, uint32_t width, uint32_t height) {
    if (!IsPowerOfTwo(width) || !IsPowerOfTwo(height))
        return reinterpret_cast<DecodeBufferAlloc_t>(Offsets::FUN_TEXTURE_DECODE_BUFFER_ALLOC)(
            static_cast<int>(format), width, height, kAllocTag, __LINE__);
    return AllocMips_o(format, width, height);
}

void __fastcall FreeMips_h(void *buffer, uint32_t format, uint32_t width, uint32_t height) {
    if (!IsPowerOfTwo(width) || !IsPowerOfTwo(height)) {
        if (buffer != nullptr)
            reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE)(buffer, kAllocTag, __LINE__,
                                                                      0);
        return;
    }
    FreeMips_o(buffer, format, width, height);
}

const Game::HookAutoRegister _allocMipsHook{Offsets::FUN_TEXTURE_ALLOC_MIPS, &AllocMips_h,
                                            reinterpret_cast<void **>(&AllocMips_o)};
const Game::HookAutoRegister _freeMipsHook{Offsets::FUN_TEXTURE_FREE_MIPS, &FreeMips_h,
                                           reinterpret_cast<void **>(&FreeMips_o)};

// ---- non-power-of-two ----------------------------------------------------------

// The validator computes "is a power of two" as
//     EAX = x & (x-1); NEG EAX; SBB EAX,EAX; INC EAX; TEST EAX,EAX; JZ fail
// so replacing NEG/SBB with `XOR EAX,EAX` leaves the INC to produce 1 and the
// test always passes. Patching the arithmetic rather than the shared JZ keeps
// the type-2 branch — which reaches the same TEST through its own SBB — intact.
constexpr uint8_t kPotTestForced[4] = {0x31, 0xC0, 0x90, 0x90}; // XOR EAX,EAX; NOP; NOP

bool ApplyNonPowerOfTwo() {
    // JNZ (not a power of two) -> the cave: fit the scratch, then a fresh
    // allocation — never the pool, whose log2 index cannot describe NPOT.
    bool ok = WriteNearJump(Offsets::PATCH_TEXALLOC_POT_W_JMP, 0x85, Cave());
    ok = WriteNearJump(Offsets::PATCH_TEXALLOC_POT_H_JMP, 0x85, Cave()) && ok;
    ok = Common::PatchBytes(reinterpret_cast<void *>(Offsets::PATCH_TEXVALIDATE_POT_W),
                            kPotTestForced, sizeof kPotTestForced) &&
         ok;
    ok = Common::PatchBytes(reinterpret_cast<void *>(Offsets::PATCH_TEXVALIDATE_POT_H),
                            kPotTestForced, sizeof kPotTestForced) &&
         ok;
    return ok;
}

// ---- minimum side --------------------------------------------------------------

// The validator's `CMP side,8 ; JC fail` per axis, lowered to 1 so only a 0
// side is refused. See Offsets.h for why nothing downstream depends on 8.
bool ApplyMinimumSide() {
    const uint8_t one = 1;
    bool ok = Common::PatchBytes(reinterpret_cast<void *>(Offsets::PATCH_TEXVALIDATE_MIN_W_IMM),
                                 &one, sizeof one);
    ok = Common::PatchBytes(reinterpret_cast<void *>(Offsets::PATCH_TEXVALIDATE_MIN_H_IMM), &one,
                            sizeof one) &&
         ok;
    return ok;
}

// Re-asserted on every registration so the code segment and our recorded state
// cannot disagree. Order matters: the cave must exist before anything is
// pointed at it.
void ApplyGates() {
    if (!ApplySizeGate())
        return;
    ApplyStrideFix();
    ApplyNonPowerOfTwo();
    ApplyMinimumSide();
}

// ---- Lua ---------------------------------------------------------------------

// `_classicapi_TextureLimits()` -> scratchSide, poolMax, poolSide, deviceMax, strideFixed
// What the gate is actually working with, for /dump. scratchSide is the
// high-water mark of what has been loaded; deviceMax is the GPU's own ceiling
// and the only one enforced.
int __fastcall Script_TextureLimits(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(ScratchDimension()));
    Game::Lua::PushNumber(L, static_cast<double>(g_poolMax));
    Game::Lua::PushNumber(L, static_cast<double>(g_poolSide));
    Game::Lua::PushNumber(L, static_cast<double>(DeviceMaxDimension()));
    Game::Lua::PushBool(L, g_strideFixed);
    return 5;
}

void RegisterLuaFunctions() {
    ApplyGates();
    Game::Lua::RegisterGlobalFunction("_classicapi_TextureLimits", &Script_TextureLimits);
}

// The glue screen loads textures too, and this is the first registration that
// runs after every other DLL's load-time patching.
void RegisterGlue() { ApplyGates(); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlue};

} // namespace

} // namespace Texture::DimensionGate
