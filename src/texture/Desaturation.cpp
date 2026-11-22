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
//
// The Texture attribute `desaturated` is 4.3.4's loader: when present it sets
// the flag as SetDesaturated does, after the engine's texture loader
// (Xml::TypeLoad).

#include "texture/Desaturation.h"

#include "Game.h"
#include "Offsets.h"
#include "gx/Device.h"
#include "gx/PixelShader.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Texture::Desaturation {

namespace {

constexpr int kLevels = 256; // amount = level / 255
constexpr int kFull = kLevels - 1;

// --- engine entry points ----------------------------------------------------

using SetShader_t = void(__thiscall *)(void *region, void *shader);
using GetUiPixelShader_t = void *(__fastcall *)(int index);

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

// Our bytecode is D3D9-only (Gx::IsD3d).
using Gx::IsD3d;

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

    const uint32_t *blob = profile >= 4 ? kPs20 : kPs11;
    const uint32_t count = profile >= 4 ? kPs20Count : kPs11Count;
    uint32_t tokens[kMaxTokens];
    std::memcpy(tokens, blob, count * sizeof(uint32_t));
    const float amount = static_cast<float>(level) / static_cast<float>(kFull);
    uint32_t bits;
    std::memcpy(&bits, &amount, sizeof bits);
    for (int i = 0; i < kAmountTokenCount; ++i)
        tokens[kAmountTokenFirst + i] = bits;

    char path[80];
    std::snprintf(path, sizeof path, "Shaders\\Pixel\\ClassicAPI\\Desaturate_ps%d_%03d.bls", profile, level);
    // A lost device reports unavailable for this call and retries on the next;
    // only a rejection by a live device is latched.
    Gx::PixelShader::Result result;
    void *node = Gx::PixelShader::Build(path, tokens, count, "desat", result);
    if (result == Gx::PixelShader::Result::Rejected)
        g_failed[level] = true;
    if (node != nullptr)
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

} // namespace

void SetAmount(void *region, double amount) {
    if (amount != amount) // NaN
        amount = 0.0;
    if (amount < 0.0)
        amount = 0.0;
    else if (amount > 1.0)
        amount = 1.0;
    // 3.80.2 stores the float and IsDesaturated is `amount > 0`
    // (0x140A68E40), so any amount above 0 takes at least the first level.
    int level = static_cast<int>(amount * kFull + 0.5);
    if (level == 0 && amount > 0.0)
        level = 1;

    if (!g_enabled || !Available()) {
        ApplyStock(region, level > 0);
        return;
    }
    Apply(region, level);
}

double GetAmount(void *region) { return static_cast<double>(LevelOf(region)) / kFull; }

namespace {

void *ResolveSelf(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE)
        return nullptr;
    return Game::Lua::ResolveTexture(L, 1, /*raiseError=*/false);
}

// --- Lua surface ------------------------------------------------------------

// texture:SetDesaturation(amount) — amount 0..1 (clamped), a number and not a
// numeric string (3.80.2's reader 0x140A6D0D0 checks the Lua type). Returns
// nothing.
int __fastcall Script_SetDesaturation(void *L) {
    void *region = ResolveSelf(L);
    if (region == nullptr || Game::Lua::Type(L, 2) != Game::Lua::TYPE_NUMBER) {
        Game::Lua::Error(L, "Usage: self:SetDesaturation(desaturation)");
        return 0;
    }
    SetAmount(region, Game::Lua::ToNumber(L, 2));
    return 0;
}

// texture:GetDesaturation() -> amount (quantised to 1/255).
int __fastcall Script_GetDesaturation(void *L) {
    void *region = ResolveSelf(L);
    if (region == nullptr) {
        Game::Lua::Error(L, "Usage: local desaturation = self:GetDesaturation()");
        return 0;
    }
    Game::Lua::PushNumber(L, GetAmount(region));
    return 1;
}

// texture:SetDesaturated([desaturated]) — the engine method, re-routed through
// the shared state so `false` clears any amount. The flag is read exactly as
// the stock method reads it — FUN_LUA_TO_BOOLEAN_LOOSE(L, 2, 1) — so nil and
// the number 0 are false. 1.12 addons write SetDesaturated(0) to clear it
// (TrinketMenu, GearMenu); plain lua_toboolean reads that 0 as true and greys
// every icon. The return stays the engine's, which 1.12 FrameXML reads: 1 when
// desaturation is supported, nil when it isn't (FrameXML then falls back to a
// grey vertex colour).
int __fastcall Script_SetDesaturated(void *L) {
    if (!g_enabled || !IsD3d())
        return Game::Lua::CallScript(Offsets::FUN_SCRIPT_TEXTURE_SET_DESATURATED, L);

    void *region = ResolveSelf(L);
    if (region == nullptr) {
        Game::Lua::Error(L, "Usage: self:SetDesaturated([desaturated])");
        return 0;
    }
    if (Profile() < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    if (!Apply(region, Game::Lua::ToFlag(L, 2, true) ? kFull : 0)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, 1.0);
    return 1;
}

// texture:IsDesaturated() -> bool: true for any amount above 0, as 3.80.2's
// (0x140A68E40). The engine's (FUN_SCRIPT_TEXTURE_IS_DESATURATED) answers 1 or
// nil from the same shader slot; it still raises the usage error.
int __fastcall Script_IsDesaturated(void *L) {
    void *region = ResolveSelf(L);
    if (region == nullptr)
        return Game::Lua::CallScript(Offsets::FUN_SCRIPT_TEXTURE_IS_DESATURATED, L);
    Game::Lua::PushBool(L, Game::Read<void *>(region, Offsets::OFF_SIMPLETEXTURE_SHADER) != nullptr);
    return 1;
}

// 4.3.4 applies the attribute whenever it is present (an empty value reads as
// false), last in the texture's load.
void OnTextureNode(void *object, const void *node, void * /*status*/) {
    if (const char *value = Xml::Attr(node, "desaturated"))
        SetAmount(object, Game::StringToFlag(value) ? 1.0 : 0.0);
}

const Xml::TypeLoad::AutoSubscribe _xmlSub{Xml::TypeLoad::TEXTURE, &OnTextureNode};

// IsDesaturateSupported() -> 1, or nothing. The engine's own support test, as
// 3.3.5's Script_IsDesaturateSupported (0x00511FB0) makes it: the stock UI
// Desaturate shader (index 1) is loaded and valid - the same gate the engine's
// Script_SetDesaturated applies.
int __fastcall Script_IsDesaturateSupported(void *L) {
    if (reinterpret_cast<GetUiPixelShader_t>(Offsets::FUN_UI_GET_PIXEL_SHADER)(1) == nullptr)
        return 0;
    Game::Lua::PushNumber(L, 1.0);
    return 1;
}

const Game::Doc::Field kIsDesaturateSupportedRets[] = {Game::Doc::Opt("supported", "number")};
const Game::Doc::Function kIsDesaturateSupported{
    "Returns 1 when textures can be desaturated, and nothing when they cannot.", {}, kIsDesaturateSupportedRets,
    "TextureGlobals"};

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
    {"IsDesaturated", &Script_IsDesaturated},
};

const Game::Doc::Field kSetDesaturationArgs[] = {
    Game::Doc::Req("desaturation", "normalizedValue",
                   "0 is full color and 1 is grayscale; values outside are clamped."),
};
const Game::Doc::Function kSetDesaturation{"Sets how much color is removed from the texture.",
                                           kSetDesaturationArgs, {}};

const Game::Doc::Field kGetDesaturationRets[] = {
    Game::Doc::Req("desaturation", "normalizedValue", "Stored in 255 steps."),
};
const Game::Doc::Function kGetDesaturation{
    "Returns how much color is removed from the texture.", {}, kGetDesaturationRets};

const Game::Doc::Field kSetDesaturatedArgs[] = {
    Game::Doc::Opt("desaturated", "bool", "false",
                   "True is SetDesaturation(1) and false is SetDesaturation(0)."),
};
const Game::Doc::Field kSetDesaturatedRets[] = {
    Game::Doc::Opt("supported", "number", nullptr,
                   "1 when textures can be desaturated, nil when they cannot."),
};
const Game::Doc::Function kSetDesaturated{"Sets whether the texture is drawn in grayscale.",
                                          kSetDesaturatedArgs, kSetDesaturatedRets};

const Game::Doc::Field kIsDesaturatedRets[] = {
    Game::Doc::Req("desaturated", "bool", "True for any desaturation above 0."),
};
const Game::Doc::Function kIsDesaturated{"Returns whether the texture is desaturated.", {},
                                         kIsDesaturatedRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetDesaturation", &kSetDesaturation},
    {"GetDesaturation", &kGetDesaturation},
    {"SetDesaturated", &kSetDesaturated},
    {"IsDesaturated", &kIsDesaturated},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods, g_methodDocs);
    Game::Lua::RegisterGlobalFunction("IsDesaturateSupported", &Script_IsDesaturateSupported,
                                      &kIsDesaturateSupported);
    Game::Lua::RegisterGlobalFunction("_classicapi_DesaturationInfo", &Script_DesaturationInfo,
                                      nullptr);
    Game::Lua::RegisterGlobalFunction("_classicapi_DesaturationEnable",
                                      &Script_DesaturationEnable, nullptr);
    Game::Lua::RegisterGlobalFunction("_classicapi_DesaturationForceProfile",
                                      &Script_DesaturationForceProfile, nullptr);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Texture::Desaturation
