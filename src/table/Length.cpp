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

// Stale table-length healing — Lua 5.1 border semantics for 5.0's
// out-of-band lengths.
//
// Lua 5.0 stores a table's `table.insert`/`getn` length OUT of band (a
// `t.n` field, else the registry's weak LUA_SIZES table). Clearing a
// table's keys (`for k in pairs(t) do t[k] = nil end`) does NOT reset it —
// 5.0 code must call `table.setn(t, 0)`. Lua 5.1 removed all of that:
// lengths are computed borders, and a cleared table is length 0
// automatically.
//
// That difference breaks the standard Ace2-era dual-compatibility idiom
// the moment our 5.1 syntax backport makes their probe pass:
//
//     local lua51 = loadstring("return function(...) return ... end")
//                       and true or false
//     local table_setn = lua51 and function() end or table.setn
//
// With `lua51` true (the `...`-expression now compiles via
// LuaSyntax::Transpile), `table_setn` is a NO-OP — correct on real 5.1,
// wrong on this VM, whose table functions still read the out-of-band
// length. Cleared-and-reused tables keep stale lengths: `table.insert`
// appends past the nil'd slots and `table.getn` counts entries that no
// longer exist. Verified live: Dewdrop-2.0's menus (the SuperAPI /
// NampowerSettings minimap buttons) break exactly this way, and work
// again when the probe is forced back to 5.0.
//
// Since we made the client PASS 5.1 probes, 5.1 length semantics must
// hold. The engine funnels every length read through ONE function —
// `luaL_getn` (LUAL_GETN, consulted by table.insert / getn / remove /
// concat / sort / foreachi, plus our `unpack`) — so a single co-hook
// closes the gap for every consumer at once:
//
//   1. Reported slot populated (`t[n] ~= nil`) → return n unchanged. The
//      healthy path costs one rawgeti.
//   2. `t[n]` nil but `t.n` is a number → return n unchanged. An explicit
//      `n` field is the 5.0 vararg-table contract (`arg` = {n=3, holes}),
//      where trailing nils are intentional — healing it would corrupt
//      vararg counts.
//   3. `t[n]` nil, no `t.n`: scan down for the true border `b` and heal to
//      it (the 5.1 answer) — UNLESS the nil at slot `n` is a deliberate
//      `table.insert(t, nil)` append slot, detected by the writer-side mark
//      below, in which case keep the stored length. Read-only — no
//      write-back, so a length read never mutates state; the engine's own
//      `table.insert` calls `luaL_setn(n+1)` on the next append, which
//      re-syncs the stored length by itself.
//
// Why a writer-side mark (our `table.insert`, below) and not a state heuristic: a
// deliberate 5.0 nil append (`table.insert(t, nil)` — writes `t[n]=nil` +
// `setn(n)`; issue #36: Waterfall's `{key,val,…}` builders append a nil for
// every option without a passValue, and the next insert must land AFTER the
// reserved slot) and a cleared-and-reused table that happens to be stale by
// exactly one (a one-element table pairs-cleared, or a recycled table whose
// previous life was one slot longer — Tablet-2.0's `del()`/`copy()` pool
// recycles per tooltip refresh, so this is everyday traffic) leave the table
// in BYTE-IDENTICAL states: stored `n`, dense below, `t[n]` nil, no `t.n`.
// No inspection of the table can split them, and caller-gating can't either
// (both reach here through `table.insert`). v1.12.8 tried a staleness-gap
// heuristic (keep when `n - b == 1`) and it corrupted the second group —
// issue #39, FuBar/Tablet/Dewdrop menus and tooltips shifting by one with a
// nil hole at slot 1. Only the WRITER knows which dialect the table speaks,
// and the write is observable: our replacement `table.insert` (registered
// over the engine's — see RegisterLuaFunctions) records `t → storedN` in a weak-keyed registry
// table whenever the two-arg form appends a literal nil. Rule 3 keeps the
// stored length only for a marked table whose mark still equals `n`; every
// unmarked off-by-one table heals. A stale mark dies on its own: any later
// append/remove moves the stored length away from the recorded value, and
// the weak key lets a dead table's entry be collected.
//
// Note real Lua 5.1 gives the HEAL answer even for the deliberate append
// (`#`-based tinsert drops a trailing nil and the next insert overwrites
// it), so keeping the slot is deliberately 5.0-native behavior for
// 5.0-dialect writers — Waterfall's builder runs on the 5.0 `arg` vararg
// table and predates 5.1. 5.1-dialect code never relies on insert(t, nil)
// reserving a slot, because on real 5.1 it doesn't.
//
// Positional nil insertion (`table.insert(t, pos, nil)`) is not marked and
// heals; no consumer is known, and 5.0 itself makes it a shifted no-op.
//
// `table.setn` itself still works for code that calls it — the heal only
// changes the answer when the stored length points past the border and no
// mark vouches for it, which is the same answer real 5.1 would give.

#include "Game.h"
#include "Offsets.h"

namespace Table::Length {

namespace {

using LuaLGetN_t = int(__fastcall *)(void *L, int idx);
using RawGetI_t = void(__fastcall *)(void *L, int idx, int n);

LuaLGetN_t g_getnOriginal = nullptr;

// The engine's `luaB_tinsert`, called directly — no trampoline. Our
// `Script_TableInsert` is registered over it instead of hooking it; see
// RegisterLuaFunctions for why that reaches every call.
const auto kEngineTableInsert =
    reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_LUA_TABLE_INSERT);

// Registry key of the weak-keyed mark table: `marks[t] = storedN` recorded
// at the moment `table.insert(t, nil)` reserved slot `storedN`.
constexpr char kMarkKey[] = "ClassicAPI_TrailingNilMark";

// True if t[k] (rawgeti) is nil. Balances the stack.
bool SlotIsNil(void *L, int absIdx, int k) {
    reinterpret_cast<RawGetI_t>(Offsets::LUA_RAWGETI)(L, absIdx, k);
    const bool isNil = Game::Lua::Type(L, -1) == Game::Lua::TYPE_NIL;
    Game::Lua::SetTop(L, -2);
    return isNil;
}

// Pushes the mark table, creating `registry[kMarkKey] = setmetatable({},
// {__mode = "k"})` on first use (per Lua state — the registry survives
// `/reload` because the state is reused, and the marked tables survive with
// it). Leaves exactly one value on the stack.
void PushMarkTable(void *L) {
    Game::Lua::PushString(L, kMarkKey);
    Game::Lua::RawGet(L, Game::Lua::REGISTRY_INDEX);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        return;
    Game::Lua::SetTop(L, -2);      // pop the nil.            []
    Game::Lua::NewTable(L);        //                         [marks]
    Game::Lua::NewTable(L);        //                         [marks, meta]
    Game::Lua::PushString(L, "__mode");
    Game::Lua::PushString(L, "k");
    Game::Lua::RawSet(L, -3);      // meta.__mode = "k".      [marks, meta]
    Game::Lua::SetMetatable(L, -2);// pops meta.              [marks]
    Game::Lua::PushString(L, kMarkKey);
    Game::Lua::PushValue(L, -2);   //                         [marks, key, marks]
    Game::Lua::RawSet(L, Game::Lua::REGISTRY_INDEX); //       [marks]
}

// True when `marks[t] == n` — the nil at slot `n` was written by a
// deliberate `table.insert(t, nil)` and nothing has moved the stored
// length since. Balances the stack.
bool HasTrailingNilMark(void *L, int absIdx, int n) {
    PushMarkTable(L);                 // [marks]
    Game::Lua::PushValue(L, absIdx);  // [marks, t]
    Game::Lua::RawGet(L, -2);         // [marks, marks[t]]
    const bool match =
        Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER &&
        static_cast<int>(Game::Lua::ToNumber(L, -1)) == n;
    Game::Lua::SetTop(L, -3);         // pop both
    return match;
}

// Our `table.insert`: the engine's `luaB_tinsert` plus the writer-side mark.
// The two-arg form appending a literal nil is the 5.0 idiom that must keep
// its reserved slot; record it so rule 3 can tell it apart from an
// identically-shaped stale table. The detection runs before the engine
// function (the arg stack is caller-owned, so index 1 still holds the table
// afterwards); the mark reads the raw stored length its `luaL_setn` just
// wrote. Everything else — argument checks, the shift-up loop, errors — is
// the engine's own code, so behavior is identical for every other call.
int __fastcall Script_TableInsert(void *L) {
    const bool nilAppend = Game::Lua::GetTop(L) == 2 &&
                           Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE &&
                           Game::Lua::Type(L, 2) == Game::Lua::TYPE_NIL;
    const int ret = kEngineTableInsert(L);
    if (nilAppend) {
        const int storedN = g_getnOriginal(L, 1);
        PushMarkTable(L);              // [marks]
        Game::Lua::PushValue(L, 1);    // [marks, t]
        Game::Lua::PushNumber(L, storedN);
        Game::Lua::RawSet(L, -3);      // marks[t] = storedN.  [marks]
        Game::Lua::SetTop(L, -2);      // pop marks
    }
    return ret;
}

int __fastcall LuaLGetN_h(void *L, int idx) {
    const int n = g_getnOriginal(L, idx);
    if (n <= 0)
        return n;

    // Normalize a relative index once — the checks below push/pop around
    // the access, and the `t.n` probe has a key on the stack at its
    // access point, which would shift a negative index. Pseudo-indices
    // (registry and below, <= -10000) pass through unchanged.
    int absIdx = idx;
    if (idx < 0 && idx > -10000)
        absIdx = Game::Lua::GetTop(L) + idx + 1;

    if (!SlotIsNil(L, absIdx, n))
        return n; // healthy — the reported last slot is populated

    // Explicit `t.n` count (vararg-table contract): trailing nils are
    // intentional, keep the stored length.
    Game::Lua::PushString(L, "n");
    Game::Lua::RawGet(L, absIdx);
    const bool hasExplicitN = Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER;
    Game::Lua::SetTop(L, -2);
    if (hasExplicitN)
        return n;

    // Scan down for the true border.
    int b = n - 1;
    while (b > 0 && SlotIsNil(L, absIdx, b))
        --b;

    // Stored length exactly one past the border: either a deliberate
    // `table.insert(t, nil)` reserved slot (keep — issue #36) or a cleared /
    // recycled table stale by one (heal — issue #39). The states are
    // identical; only the writer-side mark recorded by `TableInsert_h` can
    // tell them apart.
    if (n - b == 1 && HasTrailingNilMark(L, absIdx, n))
        return n;

    // Stale — heal to the border, the 5.1 answer.
    return b;
}

// `Script_TableInsert` is registered OVER the engine's `table.insert` rather
// than hooked onto it, and that reaches every call because of three verified
// facts:
//   * `luaB_tinsert` (FUN_LUA_TABLE_INSERT) has exactly one xref — the
//     table-lib `luaL_reg` entry in .data. No engine C code calls it; every
//     call arrives through a Lua value.
//   * Two Lua values hold it: `table.insert` from the lib open, and the
//     global `tinsert`, bound by the engine's embedded compat snippet
//     (`tinsert = tab.insert`, .data 0x008722E8). That snippet runs from
//     FUN_00703b80 immediately BEFORE FUN_LOAD_SCRIPT_FUNCTIONS (calls at
//     0x0048fe97 and 0x0048fe9c in FUN_0048fbf0; on glue 0x0046a87b and
//     0x0046a880 in FUN_0046a7b0), so it has already captured the engine
//     closure when we run and `tinsert` must be re-bound here — by VALUE,
//     so `tinsert == table.insert` stays true exactly as the snippet left
//     it. FrameXML (`local tinsert = table.insert`) and every addon load
//     after this and see the replacement.
//   * Module registrations run post-original in both load hooks, and the
//     `table` library is not re-opened after them — the order every other
//     `table.*` registration (`table.wipe`, …) already relies on.
// The glue state gets the same pair: the `luaL_getn` heal is a C-level hook
// and applies there too, and the mark table lives in each state's registry.
void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("table", "insert", &Script_TableInsert);
    Game::Lua::RegisterGlobalAlias("tinsert", "table", "insert");
}

const Game::HookAutoRegister _hook{Offsets::LUAL_GETN,
                                   reinterpret_cast<void *>(&LuaLGetN_h),
                                   reinterpret_cast<void **>(&g_getnOriginal)};
const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Table::Length
