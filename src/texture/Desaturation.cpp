// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `texture:SetDesaturation(amount)` / `texture:GetDesaturation()`, plus the
// `texture:SetDesaturated(flag)` override that keeps all three on one state —
// the modern parametric desaturation, backported.
//
// How the engine desaturates: `SetDesaturated(true)` stores ONE pixel-shader
// object (the UI shader loaded from Shaders\Pixel\Desaturate.bls) in the
// region's shader slot (OFF_SIMPLETEXTURE_SHADER). The layer draw copies that
// slot into the batch entry and binds it per region (GxRs 0x3F → D3D
// SetPixelShader). Desaturation is therefore "which shader object sits in the
// slot", so a parametric amount is a matter of having MORE shader objects.
//
// This module bakes the amount into the shader: one CGxShader per level (256
// levels = framebuffer precision — a lerp step of 1/255 moves any pixel by at
// most one unit), created lazily on first use. Each is created through the
// engine's own loader (FUN_GX_SHADER_CREATE) under a path that exists nowhere
// (Shaders\Pixel\ClassicAPI\Desaturate_ps<profile>_<level>.bls). On a missing
// file the loader still registers the node in the device's shader table, with
// valid = 0; we then supply what the file would have — the D3D bytecode — and
// compile it exactly as the loader does. The engine owns the object (table,
// refcount, destructor, process lifetime); we own nothing per region, so there
// is no reload or pooled-reuse state to track (the region ctor zeroes the slot).
//
// Shader program (both profiles): luma = dot(tex.rgb, (0.299, 0.587, 0.114));
// rgb = lerp(tex.rgb, luma, amount) * v0.rgb; a = tex.a * v0.a. Unlike the
// stock shader, which drops the vertex-colour RGB, this multiplies the tint in,
// as retail does; SetDesaturated(true) is routed through level 255 so every
// desaturation path renders the same way.
//
// Overriding SetDesaturated: FUN_REGISTER_FRAME_METHODS pushes a new node to
// the FRONT of the registry's hash bucket and the dispatcher (FUN_00702000)
// walks from the front, so re-registering the name on
// VAR_TEXTURE_METHOD_REGISTRY wins. The engine registers its Texture table as
// the first call inside FUN_LOAD_SCRIPT_FUNCTIONS and ModuleAutoRegister runs
// after the original, so ours wins on every /reload. No hook is involved.

#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Texture::Desaturation {

namespace {

constexpr int kLevels = 256; // amount = level / 255
constexpr int kFull = kLevels - 1;

// --- engine entry points ----------------------------------------------------

using ShaderCreate_t = void(__fastcall *)(int type, void **outSlot, const char *path);
using ArrayGrow_t = void(__thiscall *)(void *desc, uint32_t newCap);
using CompilePs_t = void(__thiscall *)(void *device, void *shader);
using SetShader_t = void(__thiscall *)(void *region, void *shader);
using LooseBool_t = int(__fastcall *)(void *L, int idx, int defaultValue);
using ScriptFn_t = int(__fastcall *)(void *L);

// --- shader bytecode --------------------------------------------------------
// D3D9 shader tokens, one dword each, decoded against the stock Desaturate.bls
// blobs (every token type below except `lrp` and the r1/c1 register numbers
// appears verbatim in the stock program). Parameter tokens: bit 31 set;
// register type low 3 bits at [30:28] + high 2 bits at [12:11] (r = 0,
// v = 0x10000000, c = 0x20000000, t = 0x30000000, oC0 = 0x800, s0 =
// 0x20000800); destination write mask at [19:16] (.rgb = 7, .a = 8,
// .rgba = 0xF); source swizzle at [23:16] (.xyzw = 0xE4, .wwww = 0xFF).
// ps_2_0 instruction tokens carry the operand count at [27:24]; ps_1_x leave
// it 0. `def c1` holds the amount: its four floats are dwords 9..12 in both
// programs and are patched per level.
//
// Both programs were validated against the D3D9 runtime itself (a NULLREF
// device's CreatePixelShader runs the same validator as the game's device). The
// validator REJECTS an `lrp` whose destination aliases src0 or src2 (aliasing
// src1 is allowed) with D3DERR_INVALIDCALL — the first hand-written ps_2_0
// program wrote `lrp r0.xyz, c1, r1, r0` in place and never compiled. fxc's
// disassembler does not catch this; only a real device does.

constexpr int kAmountTokenFirst = 9;
constexpr int kAmountTokenCount = 4;

constexpr uint32_t kLumaR = 0x3E991687; // 0.299f — copied from the stock blob
constexpr uint32_t kLumaG = 0x3F1645A2; // 0.587f
constexpr uint32_t kLumaB = 0x3DE978D5; // 0.114f

// Device profile index 4 (ps_2_0 or newer — every GPU the client meets). The
// instruction shape is exactly what fxc emits for the HLSL equivalent (fxc /T
// ps_2_0), with the amount moved from c0.w into its own `def c1` so it can be
// patched per level; the lerp lands in r2, not in place (see the rule above).
constexpr uint32_t kPs20[] = {
    0xFFFF0200,                                                     // ps_2_0
    0x05000051, 0xA00F0000, kLumaR, kLumaG, kLumaB, 0x00000000,     // def c0, luma
    0x05000051, 0xA00F0001, 0, 0, 0, 0,                             // def c1, amount (patched)
    0x0200001F, 0x80000000, 0xB0030000,                             // dcl t0.xy
    0x0200001F, 0x80000000, 0x900F0000,                             // dcl v0
    0x0200001F, 0x90000000, 0xA00F0800,                             // dcl_2d s0
    0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800,                 // texld r0, t0, s0
    0x03000008, 0x80080001, 0x80E40000, 0xA0E40000,                 // dp3 r1.w, r0, c0
    0x04000012, 0x80070002, 0xA0FF0001, 0x80FF0001, 0x80E40000,     // lrp r2.xyz, c1.w, r1.w, r0
    0x03000005, 0x80080000, 0x80FF0000, 0x90FF0000,                 // mul r0.w, r0.w, v0.w
    0x03000005, 0x80070000, 0x80E40002, 0x90E40000,                 // mul r0.xyz, r2, v0
    0x02000001, 0x800F0800, 0x80E40000,                             // mov oC0, r0
    0x0000FFFF,                                                     // end
};

// Device profile index 0..3 (ps_1_1 .. ps_1_4): a ps_1_1 program is valid on
// every one of them. Four arithmetic instructions of the eight allowed, one
// constant per instruction, .rgb/.a write masks only, `def` values inside the
// ps_1_x [-1, 1] constant clamp. Accepted by the runtime validator (NULLREF
// device); no hardware here selects it, so it is unverified visually.
constexpr uint32_t kPs11[] = {
    0xFFFF0101,                                                     // ps_1_1
    0x00000051, 0xA00F0000, kLumaR, kLumaG, kLumaB, 0x00000000,     // def c0, luma
    0x00000051, 0xA00F0001, 0, 0, 0, 0,                             // def c1, amount (patched)
    0x00000042, 0xB00F0000,                                         // tex t0
    0x00000008, 0x80070001, 0xB0E40000, 0xA0E40000,                 // dp3 r1.rgb, t0, c0
    0x00000012, 0x80070000, 0xA0E40001, 0x80E40001, 0xB0E40000,     // lrp r0.rgb, c1, r1, t0
    0x00000005, 0x80080000, 0xB0FF0000, 0x90FF0000,                 // mul r0.a, t0.a, v0.a
    0x00000005, 0x80070000, 0x80E40000, 0x90E40000,                 // mul r0.rgb, r0, v0
    0x0000FFFF,                                                     // end
};

constexpr uint32_t kPs20Count = sizeof(kPs20) / sizeof(kPs20[0]);
constexpr uint32_t kPs11Count = sizeof(kPs11) / sizeof(kPs11[0]);
constexpr uint32_t kMaxTokens = kPs20Count > kPs11Count ? kPs20Count : kPs11Count;

// --- state ------------------------------------------------------------------

void *g_levels[kLevels] = {}; // level -> CGxShader*, lazily created; [0] unused
bool g_failed[kLevels] = {};  // a level whose shader failed to build (latched)
bool g_enabled = true;        // kill switch: off routes everything to the engine
int g_forcedProfile = -1;     // diagnostic override of the device profile index

void *Device() { return Game::Read<void *>(Offsets::VAR_GX_DEVICE); }

// Our bytecode is D3D9-only. The OpenGL device has another layout and takes ARB
// programs, so it keeps the engine's on/off behaviour.
bool IsD3d() {
    void *dev = Device();
    return dev != nullptr &&
           Game::Read<void *>(dev, 0) == reinterpret_cast<void *>(Offsets::PTR_GXDEVICE_D3D_VTBL);
}

int Profile() {
    if (g_forcedProfile >= 0)
        return g_forcedProfile;
    void *dev = Device();
    return dev != nullptr ? Game::Read<int>(dev, Offsets::OFF_GXDEV_PS_PROFILE) : -1;
}

bool Available() { return IsD3d() && Profile() >= 0; }

void *StockShader() { return Game::Read<void *>(Offsets::VAR_UI_SHADER_DESATURATE); }

void SetRegionShader(void *region, void *shader) {
    reinterpret_cast<SetShader_t>(Offsets::FUN_SIMPLETEXTURE_SET_SHADER)(region, shader);
}

// Builds (once) the shader object for `level` in 1..kFull. Returns nullptr when
// the device can't take it; the failure is latched per level so a bad build
// costs one attempt, not one per call.
void *ShaderForLevel(int level) {
    if (level <= 0 || level >= kLevels)
        return nullptr;
    if (g_levels[level] != nullptr)
        return g_levels[level];
    if (g_failed[level] || !IsD3d())
        return nullptr;
    const int profile = Profile();
    if (profile < 0)
        return nullptr;
    void *dev = Device();
    // The device is lost (a failed Present; fullscreen alt-tab) until the engine
    // resets it. The compile gate is this same flag, so a build now can only
    // fail — report unavailable for this call and retry on the next one; only a
    // rejection by a live device is latched below.
    if (Game::Read<int>(dev, Offsets::OFF_GXDEVD3D_ALIVE) == 0)
        return nullptr;

    // The engine's loader: registers the node in the device's shader table
    // (dedup by path), tries the file, compiles what it finds. Our path has no
    // file, so the node comes back registered and empty.
    char path[80];
    std::snprintf(path, sizeof path, "Shaders\\Pixel\\ClassicAPI\\Desaturate_ps%d_%03d.bls",
                  profile, level);
    void *node = nullptr;
    reinterpret_cast<ShaderCreate_t>(Offsets::FUN_GX_SHADER_CREATE)(Offsets::GXSHADER_TYPE_PIXEL,
                                                                    &node, path);
    if (node == nullptr) {
        Debug::Log::Printf("[desat] level %d: the shader loader returned no node for %s", level,
                           path);
        g_failed[level] = true;
        return nullptr;
    }

    // Empty node: supply the bytecode the file would have carried, in the field
    // state the parser leaves (code array grown by the engine's own grow, size,
    // code type, valid), then compile the way the loader does. A node that
    // already has code was a dedup hit or a real file at our path — use it as is.
    if (Game::Read<uint32_t>(node, Offsets::OFF_GXSHADER_CODE_SIZE) == 0) {
        const uint32_t *blob = profile >= 4 ? kPs20 : kPs11;
        const uint32_t count = profile >= 4 ? kPs20Count : kPs11Count;
        uint32_t tokens[kMaxTokens];
        std::memcpy(tokens, blob, count * sizeof(uint32_t));
        const float amount = static_cast<float>(level) / static_cast<float>(kFull);
        uint32_t bits;
        std::memcpy(&bits, &amount, sizeof bits);
        for (int i = 0; i < kAmountTokenCount; ++i)
            tokens[kAmountTokenFirst + i] = bits;

        const uint32_t bytes = count * sizeof(uint32_t);
        reinterpret_cast<ArrayGrow_t>(Offsets::FUN_GX_ARRAY_GROW_BYTES)(
            Game::Ptr<uint8_t>(node, Offsets::OFF_GXSHADER_CODE_DESC), bytes);
        uint8_t *data = Game::Read<uint8_t *>(node, Offsets::OFF_GXSHADER_CODE_DATA);
        if (data == nullptr) {
            Debug::Log::Printf("[desat] level %d: the code-array grow returned no buffer", level);
            g_failed[level] = true;
            return nullptr;
        }
        std::memcpy(data, tokens, bytes);
        Game::Ref<uint32_t>(node, Offsets::OFF_GXSHADER_CODE_SIZE) = bytes;
        Game::Ref<int>(node, Offsets::OFF_GXSHADER_CODE_TYPE) = Offsets::GXSHADER_CODE_D3D;
        Game::Ref<int>(node, Offsets::OFF_GXSHADER_VALID) = 1;
        reinterpret_cast<CompilePs_t>(Offsets::FUN_GX_D3D_COMPILE_PIXEL_SHADER)(dev, node);

        // The engine swallows CreatePixelShader's HRESULT. On a failed compile
        // ask the device directly with the same bytes, so the log carries the
        // reason (D3DERR_INVALIDCALL 0x8876086C = the validator rejected it).
        if (Game::Read<void *>(node, Offsets::OFF_GXSHADER_D3D_OBJECT) == nullptr) {
            using CreatePs_t = long(__stdcall *)(void *device, const uint32_t *code, void **out);
            using Release_t = unsigned long(__stdcall *)(void *object);
            void *d3d = Game::Read<void *>(dev, Offsets::OFF_GXDEVD3D_DEVICE9);
            long hr = 0;
            if (d3d != nullptr) {
                void **vtbl = *reinterpret_cast<void ***>(d3d);
                void *ps = nullptr;
                hr = reinterpret_cast<CreatePs_t>(vtbl[106])(d3d, tokens, &ps);
                if (ps != nullptr)
                    reinterpret_cast<Release_t>(vtbl[2])(ps);
            }
            Debug::Log::Printf("[desat] level %d: CreatePixelShader rejected the %s program (hr=0x%08lX)",
                               level, profile >= 4 ? "ps_2_0" : "ps_1_1", hr);
        }
    }

    if (Game::Read<int>(node, Offsets::OFF_GXSHADER_VALID) == 0 ||
        Game::Read<void *>(node, Offsets::OFF_GXSHADER_D3D_OBJECT) == nullptr) {
        g_failed[level] = true; // the node stays inert in the table; never handed out
        return nullptr;
    }
    g_levels[level] = node;
    return node;
}

// Level 0 clears the slot; any other level binds its shader. When the level's
// shader can't be built right now (device lost) or was rejected, the engine's
// own on/off shader stands in — the texture still greys, only the tint and the
// partial amount are lost — and false is returned only when that isn't valid
// either (the engine's own "unsupported").
bool Apply(void *region, int level) {
    if (level <= 0) {
        SetRegionShader(region, nullptr);
        return true;
    }
    void *shader = ShaderForLevel(level);
    if (shader == nullptr) {
        void *stock = StockShader();
        if (stock == nullptr || Game::Read<int>(stock, Offsets::OFF_GXSHADER_VALID) == 0)
            return false;
        shader = stock;
    }
    SetRegionShader(region, shader);
    return true;
}

// The engine's own on/off behaviour, for the kill switch and non-D3D devices:
// binds the stock shader when it is valid, exactly as Script_SetDesaturated
// gates on FUN_00770620(1).
void ApplyStock(void *region, bool on) {
    if (!on) {
        SetRegionShader(region, nullptr);
        return;
    }
    void *stock = StockShader();
    if (stock != nullptr && Game::Read<int>(stock, Offsets::OFF_GXSHADER_VALID) != 0)
        SetRegionShader(region, stock);
}

// Reverse map of the region's slot. The stock shader (or any shader that isn't
// one of ours) reads as fully desaturated, consistent with IsDesaturated.
int LevelOf(void *region) {
    void *slot = Game::Read<void *>(region, Offsets::OFF_SIMPLETEXTURE_SHADER);
    if (slot == nullptr)
        return 0;
    for (int i = 1; i < kLevels; ++i)
        if (g_levels[i] == slot)
            return i;
    return kFull;
}

void *ResolveSelf(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE)
        return nullptr;
    return Game::Lua::ResolveObject(L, 1);
}

// --- Lua surface ------------------------------------------------------------

// texture:SetDesaturation(amount) — amount 0..1 (clamped). Returns nothing.
int __fastcall Script_SetDesaturation(void *L) {
    void *region = ResolveSelf(L);
    if (region == nullptr || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: texture:SetDesaturation(amount)");
        return 0;
    }
    double amount = Game::Lua::ToNumber(L, 2);
    if (amount != amount) // NaN
        amount = 0.0;
    if (amount < 0.0)
        amount = 0.0;
    else if (amount > 1.0)
        amount = 1.0;
    const int level = static_cast<int>(amount * kFull + 0.5);

    if (!g_enabled || !Available()) {
        ApplyStock(region, level > 0);
        return 0;
    }
    Apply(region, level);
    return 0;
}

// texture:GetDesaturation() -> amount (quantised to 1/255).
int __fastcall Script_GetDesaturation(void *L) {
    void *region = ResolveSelf(L);
    if (region == nullptr) {
        Game::Lua::Error(L, "Usage: texture:GetDesaturation()");
        return 0;
    }
    Game::Lua::PushNumber(L, static_cast<double>(LevelOf(region)) / kFull);
    return 1;
}

// texture:SetDesaturated(flag) — the engine method, re-routed through the
// shared state so `false` clears any amount. Same contract as the engine:
// returns 1 when desaturation is supported, nil when it isn't (FrameXML then
// falls back to a grey vertex colour).
int __fastcall Script_SetDesaturated(void *L) {
    if (!g_enabled || !IsD3d())
        return reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_TEXTURE_SET_DESATURATED)(L);

    void *region = ResolveSelf(L);
    if (region == nullptr) {
        Game::Lua::Error(L, "Usage: texture:SetDesaturated(flag)");
        return 0;
    }
    if (Profile() < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int flag = reinterpret_cast<LooseBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE)(L, 2, 1);
    if (!Apply(region, flag != 0 ? kFull : 0)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, 1.0);
    return 1;
}

// --- diagnostics ------------------------------------------------------------

// _classicapi_DesaturationInfo() -> profile, isD3d, created, failed, enabled
int __fastcall Script_DesaturationInfo(void *L) {
    int created = 0, failed = 0;
    for (int i = 1; i < kLevels; ++i) {
        created += g_levels[i] != nullptr ? 1 : 0;
        failed += g_failed[i] ? 1 : 0;
    }
    Game::Lua::PushNumber(L, Profile());
    Game::Lua::PushBool(L, IsD3d());
    Game::Lua::PushNumber(L, created);
    Game::Lua::PushNumber(L, failed);
    Game::Lua::PushBool(L, g_enabled);
    return 5;
}

// _classicapi_DesaturationEnable([on]) -> enabled. Turning it on also clears
// the per-level failure latches so a retry is possible.
int __fastcall Script_DesaturationEnable(void *L) {
    if (Game::Lua::GetTop(L) == 0)
        g_enabled = true;
    else
        g_enabled = Game::Lua::ToBoolean(L, 1) != 0;
    if (g_enabled)
        std::memset(g_failed, 0, sizeof g_failed);
    Game::Lua::PushBool(L, g_enabled);
    return 1;
}

// _classicapi_DesaturationForceProfile([index]) -> profile. Overrides the device
// profile index (nil restores the device's) so the ps_1_1 program can be
// exercised on modern hardware. Shader paths carry the profile, so the caches
// are simply dropped; the engine keeps the old nodes.
int __fastcall Script_DesaturationForceProfile(void *L) {
    g_forcedProfile = Game::Lua::IsNumber(L, 1) ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : -1;
    std::memset(g_levels, 0, sizeof g_levels);
    std::memset(g_failed, 0, sizeof g_failed);
    Game::Lua::PushNumber(L, Profile());
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetDesaturation", &Script_SetDesaturation},
    {"GetDesaturation", &Script_GetDesaturation},
    {"SetDesaturated", &Script_SetDesaturated},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
    Game::Lua::RegisterGlobalFunction("_classicapi_DesaturationInfo", &Script_DesaturationInfo);
    Game::Lua::RegisterGlobalFunction("_classicapi_DesaturationEnable",
                                      &Script_DesaturationEnable);
    Game::Lua::RegisterGlobalFunction("_classicapi_DesaturationForceProfile",
                                      &Script_DesaturationForceProfile);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Texture::Desaturation
