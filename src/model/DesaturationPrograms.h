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

// The ps_2_0 programs a desaturated model batch draws with, as D3D9 token
// streams. No engine dependency, so an offline NULLREF device can validate
// every program (the runtime validator is the one that matters: it refuses
// streams fxc disassembles happily, see texture-desaturation.md).
//
// Every program ends the same way: luma = dot(rgb, (0.299, 0.587, 0.114)),
// rgb = lerp(rgb, luma, amount), alpha kept. The weights are the stock
// Desaturate.bls constants, which Texture::Desaturation uses too.

#include <cstdint>
#include <cstring>
#include <vector>

namespace Model::Desaturation::Programs {

constexpr uint32_t kLumaR = 0x3E991687; // 0.299f
constexpr uint32_t kLumaG = 0x3F1645A2; // 0.587f
constexpr uint32_t kLumaB = 0x3DE978D5; // 0.114f

// The combine presets of a fixed-function stage (gx/renderstate.md, texture
// stage combine; Offsets GXRS_COMBINER0): colour D3DTOP 4, 4, 13, 7, 5, 12 and
// alpha 3, 4, 3, 7, 5, 3, with the arguments left at their defaults (texture,
// current; current is the vertex colour on stage 0).
constexpr int kPresets = 6;
// Stage 0 off, or on with a preset and stage 1 off or on with a preset.
constexpr int kFixedPrograms = 1 + kPresets * (1 + kPresets);

namespace Token {
constexpr uint32_t R = 0x80000000, V = 0x90000000, C = 0xA0000000, T = 0xB0000000, S = 0xA0000800,
                   OC = 0x80000800;
constexpr uint32_t XYZW = 0xE4 << 16, WWWW = 0xFF << 16;
constexpr uint32_t RGB = 7 << 16, A = 8 << 16, RGBA = 0xF << 16, XY = 3 << 16;
constexpr uint32_t SAT = 1 << 20;
constexpr uint32_t Op(uint32_t opcode, uint32_t operands) { return opcode | (operands << 24); }
constexpr uint32_t MOV = 1, ADD = 2, MUL = 5, DP3 = 8, LRP = 0x12, DCL = 0x1F, TEXLD = 0x42, DEF = 0x51;
constexpr uint32_t VERSION_PS20 = 0xFFFF0200, END = 0x0000FFFF;
} // namespace Token

inline uint32_t FloatBits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

// def c<luma>, luma; def c<amount>, amount.
inline void Defines(std::vector<uint32_t> &out, uint32_t luma, uint32_t amountReg, float amount) {
    using namespace Token;
    const uint32_t a = FloatBits(amount);
    out.insert(out.end(), {Op(DEF, 5), C | RGBA | luma, kLumaR, kLumaG, kLumaB, 0});
    out.insert(out.end(), {Op(DEF, 5), C | RGBA | amountReg, a, a, a, a});
}

// rgb = lerp(rgb of `src`, luma, amount), alpha of `src`, to oC0, through
// the temps `luma` and `out` (an lrp may not write its src0 or src2).
inline void Finish(std::vector<uint32_t> &out, uint32_t src, uint32_t lumaReg, uint32_t amountReg, uint32_t tmpLuma,
                   uint32_t tmpOut) {
    using namespace Token;
    out.insert(out.end(), {Op(DP3, 3), R | A | tmpLuma, R | XYZW | src, C | XYZW | lumaReg});
    out.insert(out.end(),
               {Op(LRP, 4), R | RGB | tmpOut, C | WWWW | amountReg, R | WWWW | tmpLuma, R | XYZW | src});
    out.insert(out.end(), {Op(MOV, 2), R | A | tmpOut, R | WWWW | src});
    out.insert(out.end(), {Op(MOV, 2), OC | RGBA | 0, R | XYZW | tmpOut});
    out.push_back(END);
}

// The fixed-function batch: `p0` the preset of stage 0, -1 with no texture
// there; `p1` stage 1's, -1 when it has none (or stage 0 is off, which ends
// the cascade). r0 = current, r1 = the stage's texel, r2 = the stage's result.
inline std::vector<uint32_t> Fixed(int p0, int p1, float amount) {
    using namespace Token;
    std::vector<uint32_t> out{VERSION_PS20};
    Defines(out, 0, 1, amount);
    const int stages = p0 < 0 ? 0 : (p1 < 0 ? 1 : 2);
    out.insert(out.end(), {Op(DCL, 2), 0x80000000, V | RGBA | 0});
    for (int k = 0; k < stages; ++k) {
        out.insert(out.end(), {Op(DCL, 2), 0x80000000, T | XY | static_cast<uint32_t>(k)});
        out.insert(out.end(), {Op(DCL, 2), 0x90000000, S | RGBA | static_cast<uint32_t>(k)});
    }
    out.insert(out.end(), {Op(MOV, 2), R | RGBA | 0, V | XYZW | 0});
    for (int k = 0; k < stages; ++k) {
        const int p = k == 0 ? p0 : p1;
        const auto stage = static_cast<uint32_t>(k);
        out.insert(out.end(), {Op(TEXLD, 3), R | RGBA | 1, T | XYZW | stage, S | XYZW | stage});
        switch (p) {
        case 0:
        case 1: // MODULATE
            out.insert(out.end(), {Op(MUL, 3), R | RGB | SAT | 2, R | XYZW | 1, R | XYZW | 0});
            break;
        case 2: // BLENDTEXTUREALPHA
            out.insert(out.end(), {Op(LRP, 4), R | RGB | SAT | 2, R | WWWW | 1, R | XYZW | 1, R | XYZW | 0});
            break;
        case 3: // ADD
            out.insert(out.end(), {Op(ADD, 3), R | RGB | SAT | 2, R | XYZW | 1, R | XYZW | 0});
            break;
        case 4: // MODULATE2X
            out.insert(out.end(), {Op(MUL, 3), R | RGB | 2, R | XYZW | 1, R | XYZW | 0});
            out.insert(out.end(), {Op(ADD, 3), R | RGB | SAT | 2, R | XYZW | 2, R | XYZW | 2});
            break;
        default: // 5, BLENDDIFFUSEALPHA: the vertex colour's alpha on any stage
            out.insert(out.end(), {Op(LRP, 4), R | RGB | SAT | 2, V | WWWW | 0, R | XYZW | 1, R | XYZW | 0});
            break;
        }
        switch (p) {
        case 1: // MODULATE
            out.insert(out.end(), {Op(MUL, 3), R | A | SAT | 2, R | WWWW | 1, R | WWWW | 0});
            break;
        case 3: // ADD
            out.insert(out.end(), {Op(ADD, 3), R | A | SAT | 2, R | WWWW | 1, R | WWWW | 0});
            break;
        case 4: // MODULATE2X
            out.insert(out.end(), {Op(MUL, 3), R | A | 2, R | WWWW | 1, R | WWWW | 0});
            out.insert(out.end(), {Op(ADD, 3), R | A | SAT | 2, R | WWWW | 2, R | WWWW | 2});
            break;
        default: // 0, 2, 5: SELECTARG2, the current alpha
            out.insert(out.end(), {Op(MOV, 2), R | A | 2, R | WWWW | 0});
            break;
        }
        out.insert(out.end(), {Op(MOV, 2), R | RGBA | 0, R | XYZW | 2});
    }
    Finish(out, 0, 0, 1, 3, 4);
    return out;
}

// Index of a fixed program, 0..kFixedPrograms-1.
inline int FixedIndex(int p0, int p1) { return p0 < 0 ? 0 : 1 + p0 * (1 + kPresets) + (p1 < 0 ? 0 : 1 + p1); }

// The engine's Model2 pixel program (Shaders\Pixel\Model2.bls, ps_2_0) with
// its closing `mov oC0, r0` replaced by the desaturation. Every one of the 32
// ends that way and reads only c0..c9 and r0..r5 (census of the file); a
// program that doesn't fit is refused (empty result) rather than guessed at.
inline std::vector<uint32_t> Patched(const uint32_t *code, uint32_t count, float amount) {
    using namespace Token;
    constexpr uint32_t kLuma = 10, kAmount = 11, kTmpLuma = 6, kTmpOut = 7;
    static constexpr uint32_t kTail[] = {Op(MOV, 2), OC | RGBA | 0, R | XYZW | 0, END};
    constexpr uint32_t kTailCount = sizeof kTail / sizeof kTail[0];
    if (count < 1 + kTailCount || code[0] != VERSION_PS20 ||
        std::memcmp(code + count - kTailCount, kTail, sizeof kTail) != 0)
        return {};
    // Walk the instructions (ps_2_0 tokens carry their operand count) and
    // refuse one that touches the registers the tail takes.
    const uint32_t body = count - kTailCount;
    for (uint32_t i = 1; i < body;) {
        const uint32_t op = code[i] & 0xFFFF;
        const uint32_t n = (code[i] >> 24) & 0xF;
        if (i + 1 + n > body)
            return {};
        if (op != DEF && op != DCL) {
            for (uint32_t j = 1; j <= n; ++j) {
                const uint32_t t = code[i + j];
                const uint32_t type = ((t >> 28) & 7) | (((t >> 11) & 3) << 3);
                const uint32_t reg = t & 0x7FF;
                if ((type == 0 && (reg == kTmpLuma || reg == kTmpOut)) ||
                    (type == 2 && (reg == kLuma || reg == kAmount)))
                    return {};
            }
        } else if (op == DEF && (code[i + 1] & 0x7FF) >= kLuma) {
            return {};
        }
        i += 1 + n;
    }
    std::vector<uint32_t> out{VERSION_PS20};
    Defines(out, kLuma, kAmount, amount);
    out.insert(out.end(), code + 1, code + body);
    Finish(out, 0, kLuma, kAmount, kTmpLuma, kTmpOut);
    return out;
}

} // namespace Model::Desaturation::Programs
