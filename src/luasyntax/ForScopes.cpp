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

// Gives each pass of a `for` loop its own loop variables, as far as a function
// made in the loop can tell -- the Lua 5.1 behavior addons are written against.
//
// THE DIFFERENCE. Lua 5.0's loop variables ARE the loop's control registers:
// OP_FORLOOP increments the numeric index in place, OP_TFORLOOP writes the
// generic variables in place. A function made in the body captures that
// register as an open upvalue, and nothing closes it until forstat's outer
// block ends after the loop -- so every such function shares one variable,
// which by then holds nil (generic) or the value past the limit (numeric).
// Zygor Guides Viewer 2.0's recent-guides menu gives each entry
// `checked = function() return ZGV.CurrentGuideName == guide.full end`, which
// indexes a nil `guide` when the menu opens.
//
// THE FIX IS ONE INSTRUCTION. Close the loop's upvalues at the end of every
// pass, just before OP_FORLOOP / OP_TFORLOOP: each function keeps the value its
// pass ended with, and the next pass captures a fresh upvalue. That is the
// engine's own mechanism -- leaveblock emits exactly this OP_CLOSE for a body
// local, and breakstat for a `break` -- applied to the one set of variables
// 5.0 never closes per pass. Iteration is untouched: the variables are still the
// control registers, so a body that assigns to one still steers the loop, as
// stock 5.0 code may rely on (5.1 copies them, so there it would not).
//
// ONLY WHEN SOMETHING WAS CAPTURED. The parser already knows. forstat wraps the
// statement in its own block whose nactvar is `base`, and forbody declares the
// loop variables before entering the loop block, so markupval -- run when a
// nested function names a loop variable -- walks past the loop block (nactvar
// base + nvars) and sets upval on forstat's block. Nothing else lands there:
// body locals mark deeper blocks, outer locals shallower ones, and the
// variables are not yet active while the header's expressions parse. A loop
// whose variables nothing captures compiles to the engine's exact bytes.
//
// THE APPROACH. forbody is 11 engine calls with nowhere to splice an
// instruction after the fact (patching emitted code would mean relocating
// jumps), so the hook is forbody itself, step for step through the engine's
// own helpers, with the OP_CLOSE added between the body and the loop
// instruction. It goes BEFORE patchtohere(prep - 1): the entry jump then
// targets the loop instruction and skips it, while any jump the body left
// pending on fs->jpc (an `if` ending the body) is discharged onto the
// OP_CLOSE, so every path that finishes a pass runs it. Compile-time only.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace LuaSyntax::ForScopes {

namespace {

using ForBody_t = void(__fastcall *)(void *ls, int base, int line, int nvars, int isnum);
using LsInt_t = void(__fastcall *)(void *ls, int value);
using Ls_t = void(__fastcall *)(void *ls);
using EnterBlock_t = void(__fastcall *)(void *fs, void *bl, int isbreakable);
using Fs_t = void(__fastcall *)(void *fs);
using FsPc_t = int(__fastcall *)(void *fs);
using FsInt_t = void(__fastcall *)(void *fs, int value);
using PatchList_t = void(__fastcall *)(void *fs, int list, int target);
using CodeABx_t = int(__fastcall *)(void *fs, int op, int a, int bx);
using CodeABC_t = int(__fastcall *)(void *fs, int op, int a, int b, int c);

ForBody_t ForBody_o = nullptr;

template <typename T> T *At(void *base, uintptr_t offset) {
    return reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

template <typename T> T Fn(uintptr_t address) { return reinterpret_cast<T>(address); }

void __fastcall ForBody_h(void *ls, int base, int line, int nvars, int isnum) {
    void *fs = *At<void *>(ls, Offsets::OFF_LUA_LEXSTATE_FS);
    // forstat's block, the one a captured loop variable marks (see above).
    void *scope = *At<void *>(fs, Offsets::OFF_LUA_FUNCSTATE_BL);

    alignas(4) uint8_t bl[Offsets::SIZEOF_LUA_BLOCKCNT];
    Fn<LsInt_t>(Offsets::FUN_LUA_PARSER_ADJUSTLOCALVARS)(ls, nvars);
    Fn<LsInt_t>(Offsets::FUN_LUA_PARSER_CHECK)(ls, Offsets::LUA_TK_DO);
    Fn<EnterBlock_t>(Offsets::FUN_LUA_PARSER_ENTERBLOCK)(fs, bl, 1);
    const int prep = Fn<FsPc_t>(Offsets::FUN_LUA_K_GETLABEL)(fs);
    Fn<Ls_t>(Offsets::FUN_LUA_PARSER_BLOCK)(ls);

    if (scope != nullptr && *At<int>(scope, Offsets::OFF_LUA_BLOCKCNT_UPVAL) != 0)
        Fn<CodeABC_t>(Offsets::FUN_LUA_K_CODEABC)(fs, Offsets::LUA_OP_CLOSE, base, 0, 0);

    Fn<FsInt_t>(Offsets::FUN_LUA_K_PATCHTOHERE)(fs, prep - 1);
    const int endfor =
        isnum ? Fn<CodeABx_t>(Offsets::FUN_LUA_K_CODEABX)(fs, Offsets::LUA_OP_FORLOOP, base,
                                                          Offsets::LUA_NO_JUMP_SBX)
              : Fn<CodeABC_t>(Offsets::FUN_LUA_K_CODEABC)(fs, Offsets::LUA_OP_TFORLOOP, base, 0,
                                                          nvars - 3);
    Fn<FsInt_t>(Offsets::FUN_LUA_K_FIXLINE)(fs, line);
    const int back = isnum ? endfor : Fn<FsPc_t>(Offsets::FUN_LUA_K_JUMP)(fs);
    Fn<PatchList_t>(Offsets::FUN_LUA_K_PATCHLIST)(fs, back, prep);
    Fn<Fs_t>(Offsets::FUN_LUA_PARSER_LEAVEBLOCK)(fs);
}

const Game::HookAutoRegister _forBodyHook{Offsets::FUN_LUA_PARSER_FORBODY,
                                          reinterpret_cast<void *>(&ForBody_h),
                                          reinterpret_cast<void **>(&ForBody_o)};

} // namespace

} // namespace LuaSyntax::ForScopes
