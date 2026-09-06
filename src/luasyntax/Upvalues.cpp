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

// Lifts the parser's limit of 32 upvalues per function to 255.
//
// Lua 5.0 refuses the 33rd upvalue with "too many upvalues (limit=32)". Lua 5.1
// allows 60, so a function written against a modern client can be perfectly
// valid there and unloadable here -- and it fails at compile time, as a syntax
// error on the whole file, which is the one class of 5.1 difference the source
// transpiler cannot paper over because the limit sits in the parser itself.
//
// WHY THE IMMEDIATE CANNOT SIMPLY BE RAISED. The limit is the size of a fixed
// array. The parser's FuncState carries `expdesc upvalues[32]` at +0x38, and the
// next field -- actvar[200], the stack of locals currently in scope -- begins at
// +0x2B8, which is exactly 0x38 + 32 * 0x14. Patch the `PUSH 0x20` and the 33rd
// upvalue is written over actvar[0]: the function then compiles with the wrong
// idea of which locals exist, and nothing raises. See Offsets.h at
// FUN_LUA_PARSER_INDEXUPVALUE for the derivation.
//
// WHAT IS ACTUALLY LOAD-BEARING. That array has exactly two readers in 5.0, both
// located in this binary: indexupvalue, which scans it for an existing entry and
// appends new ones, and pushclosure, which walks it once the function body is
// complete to emit one OP_MOVE / OP_GETUPVAL per entry after OP_CLOSURE.
// Everything downstream is sized dynamically -- f->nups and LClosure.nupvalues
// are bytes, OP_GETUPVAL's B field is 9 bits, closures are allocated per
// instance, the debug-name array grows on demand -- so the byte is the real
// ceiling and the array is the only obstacle.
//
// THE APPROACH. Co-hook both readers and keep entries 33 and up in side storage
// keyed by the FuncState's address. A function that stays within 32 upvalues
// never leaves engine code: both hooks call the original for it, so the
// ordinary case is the engine's own bytes. Only a function the engine would
// have rejected takes the reimplemented path, and that path calls the same
// engine helpers the original does -- luaY_checklimit with the byte's ceiling
// in place of 32, so an overflow still raises the engine's own message;
// luaM_growaux for the name array; luaK_codeABx / luaK_codeABC for emission.
// No bytecode format or VM behaviour changes: a closure with 40 upvalues is
// exactly the closure 5.0's own VM already knows how to build and run.
//
// STALENESS. A parse error longjmps out of the parser, so side storage keyed by
// a C-stack address can outlive its function, and the next chunk may put a new
// FuncState at the same address. Two facts make this self-correcting without a
// hook on open_func: side entries exist only while nups > 32, and indexupvalue
// is the only writer. So a FuncState arriving at indexupvalue with nups <= 32
// cannot own any side entries, and whatever is filed under its address is a
// leftover -- cleared there, on the new function's first upvalue, before
// anything could read it. pushclosure clears too, since a completed FuncState
// is dead once it has emitted its closure.
//
// Compile-time only, so no per-frame cost, and nothing else hooks lparser
// internals -- LuaSyntax's transpiler hooks luaL_loadbuffer, a different
// function, and the other Octo DLLs do not touch the compiler.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace LuaSyntax::Upvalues {

namespace {

// lparser.h: { expkind k; int info; int aux; int t; int f; }
struct Expdesc {
    int32_t k;
    int32_t info;
    int32_t aux;
    int32_t t;
    int32_t f;
};
static_assert(sizeof(Expdesc) == Offsets::SIZEOF_LUA_EXPDESC,
              "expdesc must match the parser's 20-byte stride");

constexpr int kNativeSlots = Offsets::LUA_PARSER_NATIVE_UPVALUE_SLOTS;
constexpr int kLimit = 255; // f->nups and LClosure.nupvalues are lu_byte

constexpr int32_t VLOCAL = 5;
constexpr int32_t VRELOCABLE = 10;
constexpr int32_t OP_MOVE = 0;
constexpr int32_t OP_GETUPVAL = 4;
constexpr int32_t OP_CLOSURE = 0x22;

// Entries 33+ of every FuncState currently being compiled, keyed by its address.
std::unordered_map<const void *, std::vector<Expdesc>> g_spill;

using IndexUpvalue_t = int(__fastcall *)(void *fs, void *name, Expdesc *v);
using PushClosure_t = void(__fastcall *)(void *ls, void *func, Expdesc *v);
using CheckLimit_t = void(__fastcall *)(void *ls, int value, int limit, const char *what);
using GrowAux_t = void *(__fastcall *)(void *L, void *block, int *size, int elemSize, int limit,
                                       const char *errMsg);
using CodeABx_t = int(__fastcall *)(void *fs, int op, int a, int bx);
using CodeABC_t = int(__fastcall *)(void *fs, int op, int a, int b, int c);
using InitExp_t = void(__fastcall *)(Expdesc *v, int k, int info);

IndexUpvalue_t IndexUpvalue_o = nullptr;
PushClosure_t PushClosure_o = nullptr;

template <typename T> T *At(void *base, uintptr_t offset) {
    return reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

uint8_t &Nups(void *proto) { return *At<uint8_t>(proto, Offsets::OFF_LUA_PROTO_NUPS); }

Expdesc *NativeSlots(void *fs) {
    return At<Expdesc>(fs, Offsets::OFF_LUA_FUNCSTATE_UPVALUES);
}

bool SameUpvalue(const Expdesc &a, const Expdesc &b) { return a.k == b.k && a.info == b.info; }

int __fastcall IndexUpvalue_h(void *fs, void *name, Expdesc *v) {
    void *proto = *At<void *>(fs, Offsets::OFF_LUA_FUNCSTATE_F);
    const int nups = Nups(proto);

    // See STALENESS above: with nups <= 32 nothing filed under this address can
    // be ours, so drop it before it can be read.
    if (nups <= kNativeSlots)
        g_spill.erase(fs);
    // Within the engine's own array: the engine's own code, untouched.
    if (nups < kNativeSlots)
        return IndexUpvalue_o(fs, name, v);

    // From here the original would raise. Same scan it performs, over both
    // halves of the array.
    const Expdesc *native = NativeSlots(fs);
    for (int i = 0; i < kNativeSlots; ++i)
        if (SameUpvalue(native[i], *v))
            return i;
    std::vector<Expdesc> &spill = g_spill[fs];
    for (size_t i = 0; i < spill.size(); ++i)
        if (SameUpvalue(spill[i], *v))
            return kNativeSlots + static_cast<int>(i);

    // The engine's own limit check with the byte's ceiling: over it, this raises
    // "too many upvalues (limit=255)" exactly as the original raises at 32, and
    // does not return.
    void *ls = *At<void *>(fs, Offsets::OFF_LUA_FUNCSTATE_LS);
    reinterpret_cast<CheckLimit_t>(Offsets::FUN_LUA_PARSER_CHECKLIMIT)(ls, nups + 1, kLimit,
                                                                       "upvalues");

    // Grow the debug-name array the way the original does, then record the name.
    int *sizeUpvalues = At<int>(proto, Offsets::OFF_LUA_PROTO_SIZEUPVALUES);
    void **&names = *At<void **>(proto, Offsets::OFF_LUA_PROTO_UPVALUES);
    if (*sizeUpvalues < nups + 1) {
        void *L = *At<void *>(fs, Offsets::OFF_LUA_FUNCSTATE_L);
        names = static_cast<void **>(reinterpret_cast<GrowAux_t>(Offsets::FUN_LUA_M_GROWAUX)(
            L, names, sizeUpvalues, sizeof(void *), 0x7FFFFFFD, ""));
    }
    names[nups] = name;

    spill.push_back(*v);
    Nups(proto) = static_cast<uint8_t>(nups + 1);
    return nups;
}

void __fastcall PushClosure_h(void *ls, void *func, Expdesc *v) {
    void *funcProto = *At<void *>(func, Offsets::OFF_LUA_FUNCSTATE_F);
    const int nups = Nups(funcProto);
    if (nups <= kNativeSlots) {
        PushClosure_o(ls, func, v);
        g_spill.erase(func); // finished with; cannot own entries anyway
        return;
    }

    // Every spilled entry must be present: indexupvalue is the only thing that
    // grows nups past 32 and it always spills as it does so. If they are not,
    // fall back to the engine's own refusal rather than read past the array.
    auto it = g_spill.find(func);
    if (it == g_spill.end() || it->second.size() < static_cast<size_t>(nups - kNativeSlots)) {
        reinterpret_cast<CheckLimit_t>(Offsets::FUN_LUA_PARSER_CHECKLIMIT)(ls, nups, kNativeSlots,
                                                                           "upvalues");
        return; // unreachable: checklimit raised
    }

    // The original, step for step, with the emission loop reading both halves.
    void *fs = *At<void *>(ls, Offsets::OFF_LUA_LEXSTATE_FS);
    void *proto = *At<void *>(fs, Offsets::OFF_LUA_FUNCSTATE_F);
    int *np = At<int>(fs, Offsets::OFF_LUA_FUNCSTATE_NP);
    int *sizep = At<int>(proto, Offsets::OFF_LUA_PROTO_SIZEP);
    void **&protos = *At<void **>(proto, Offsets::OFF_LUA_PROTO_P);
    if (*sizep < *np + 1) {
        void *L = *At<void *>(ls, Offsets::OFF_LUA_LEXSTATE_L);
        protos = static_cast<void **>(reinterpret_cast<GrowAux_t>(Offsets::FUN_LUA_M_GROWAUX)(
            L, protos, sizep, sizeof(void *), 0x3FFFF, "constant table overflow"));
    }
    protos[*np] = funcProto;
    const int index = (*np)++;
    const int pc =
        reinterpret_cast<CodeABx_t>(Offsets::FUN_LUA_K_CODEABX)(fs, OP_CLOSURE, 0, index);
    reinterpret_cast<InitExp_t>(Offsets::FUN_LUA_PARSER_INIT_EXP)(v, VRELOCABLE, pc);

    const Expdesc *native = NativeSlots(func);
    const std::vector<Expdesc> &spill = it->second;
    auto codeABC = reinterpret_cast<CodeABC_t>(Offsets::FUN_LUA_K_CODEABC);
    for (int i = 0; i < nups; ++i) {
        const Expdesc &e = (i < kNativeSlots) ? native[i] : spill[i - kNativeSlots];
        codeABC(fs, e.k == VLOCAL ? OP_MOVE : OP_GETUPVAL, 0, e.info, 0);
    }
    g_spill.erase(it);
}

const Game::HookAutoRegister _indexUpvalueHook{Offsets::FUN_LUA_PARSER_INDEXUPVALUE,
                                               reinterpret_cast<void *>(&IndexUpvalue_h),
                                               reinterpret_cast<void **>(&IndexUpvalue_o)};
const Game::HookAutoRegister _pushClosureHook{Offsets::FUN_LUA_PARSER_PUSHCLOSURE,
                                              reinterpret_cast<void *>(&PushClosure_h),
                                              reinterpret_cast<void **>(&PushClosure_o)};

} // namespace

} // namespace LuaSyntax::Upvalues
