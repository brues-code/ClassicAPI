# Lua 5.0 C API in vanilla 1.12.1

Catalogue of the Lua C API functions linked into the client. Vanilla WoW is
built against **Lua 5.0** (not 5.1), and the entire API plus the auxiliary
library lives in a contiguous block of `.text` from roughly `0x006F2000` to
`0x006F5100`. ~120 functions; the public C API is the core of that. The
thread / coroutine machinery (`lua_resume`, `lua_yield`, the inner
`luaE_newthread`) lives slightly higher at `0x006F6000`–`0x006F6900`.

**Cross-verified against two binaries:**

* `C:\WoW\Octo\WoW.exe` — the Turtle WoW build this project hooks (4.9 MB).
* `C:\WoW\WoW 1.12.1 Tiny\origWoW.exe` — an unpatched 1.12.1 client (4.6 MB).

Both share `ImageBase 0x00400000` and identical section VAs (`.text 0x401000`,
`.rdata 0x7FF000`, `.data 0x827000`). All 60 Lua-C-API entry points listed
below are **byte-identical** in their first 32 bytes between the two binaries
— Turtle adds ~128 KB of new code at higher VAs without touching the engine.
Same goes for the DBC class infrastructure and the FrameScript registration
loop. So every offset in this doc transfers verbatim to a clean 1.12.1
client.

`L` (the `lua_State *`) is passed in `ecx` for almost every API entry — the
build uses the MSVC fastcall convention. Each `TValue` is **16 bytes** here
(tag + 12-byte value union), which is why you see a lot of `shl edx, 4` and
`add [ecx+8], 0x10` in the bodies.

## `lua_State` shape (as observed)

| Offset | Field           | Notes                                       |
|--------|-----------------|---------------------------------------------|
| `+0x08`| `top`           | `TValue *` — top of stack (next free slot)  |
| `+0x0C`| `base`          | `TValue *` — base of the current frame      |
| `+0x10`| `ci`            | `CallInfo *` — current call info            |
| `+0x18`| `stack`         | bottom of stack array                       |
| `+0x24`| `gcthreshold`   | bytes; `lua_setgcthreshold` writes here     |
| `+0x28`| `nblocks`       | live byte count (read by `lua_getgccount`)  |

A `TValue` is laid out:

| Offset | Field   | Notes                                 |
|--------|---------|---------------------------------------|
| `+0x0` | `tag`   | type tag (see below)                  |
| `+0x4` | `value` | 12-byte value union                   |

Tag values (Lua 5.0):
```
LUA_TNONE          -1
LUA_TNIL            0
LUA_TBOOLEAN        1
LUA_TLIGHTUSERDATA  2
LUA_TNUMBER         3
LUA_TSTRING         4
LUA_TTABLE          5
LUA_TFUNCTION       6
LUA_TUSERDATA       7
LUA_TTHREAD         8
```

Pseudo-indices in this build:
```
LUA_REGISTRYINDEX  -10000  (0xFFFFD8F0)
LUA_GLOBALSINDEX   -10001  (0xFFFFD8EF)
LUA_UPVALUEINDEX(i) negative further down
```
You'll see `81 FA F0 D8 FF FF` (`cmp edx, -10000`) and friends in every
function that accepts a stack index.

## Public C API

Confidence column: ✓ = directly verified (CLAUDE.md or via cross-call from
`FrameScript_RegisterFunction`); ★ = high (body pattern is unambiguous —
right tag, right helper calls, matches Lua source); ◦ = inferred from
context but not byte-by-byte verified.

### State manipulation

| VA           | Function                | Conv.    | Notes                                                       | Conf |
|--------------|-------------------------|----------|--------------------------------------------------------------|------|
| `0x006F2F80` | `lua_xmove`             | fastcall | `(from, to, n)` — decr `from->top` by `n*0x10`, copy each 16-byte TValue, incr `to->top`. Used by `coroutine.*` to shuttle args/returns between threads | ★ |
| `0x006F3030` | `lua_newthread`         | fastcall | GC check then calls inner `luaE_newthread` at `0x006F6B10`; pushes the new state onto L with tag 8 (LUA_TTHREAD). Returns `lua_State *` in eax — Ghidra reports `void` (decompiler artifact) | ★ |
| `0x006F3070` | `lua_gettop`            | fastcall | `(top - base) >> 4`                                          | ★   |
| `0x006F3080` | `lua_settop`            | fastcall |                                                              | ✓   |
| `0x006F30D0` | `lua_remove`            | fastcall | shift-down loop + decr_top; previously misidentified as `lua_pushvalue` | ★ |
| `0x006F31A0` | `lua_insert`            | fastcall | called as `lua_insert(L, -2)` from `FrameScript_RegisterFunction` | ✓   |
| `0x006F32B0` | `lua_replace`           | fastcall | single TValue copy + decr_top; previously misidentified as `lua_remove` | ★ |
| `0x006F3350` | `lua_pushvalue`         | fastcall | index-resolve → TValue copy → `add [ecx+8], 0x10` incr_top; resolves pseudo-indices (`-10000`, `-10001`); previously misidentified as `lua_replace` | ★   |

### Access (type & conversion)

| VA           | Function                | Conv.    | Notes                                                                | Conf |
|--------------|-------------------------|----------|-----------------------------------------------------------------------|------|
| `0x006F3400` | `lua_type`              | fastcall | thin wrapper: `index2adr(L,idx)` → `*ptr`                             | ✓   |
| `0x006F3410` | *internal* `index2adr`  | fastcall | resolves stack index (incl. pseudo-indices) to `TValue *`             | ★   |
| `0x006F3480` | `lua_typename`          | fastcall | string array at `0x00811CD0`; `-1` → `"no value"` at `0x00871624`     | ★   |
| `0x006F34A0` | `lua_iscfunction`       | fastcall | tag == 6 and func-flag at `+6`                                        | ★   |
| `0x006F34D0` | `lua_isnumber`          | fastcall | tag == 3 OR coercible (`luaV_tonumber` at `0x006F7C20`)               | ✓   |
| `0x006F3510` | `lua_isstring`          | fastcall | tag == 4 OR tag == 3                                                  | ★   |
| `0x006F3530` | `lua_isuserdata`        | fastcall | tag == 7 (full) OR tag == 2 (light)                                   | ★   |
| `0x006F3550` | `lua_equal`             | fastcall | calls `equalobj` at `0x006F58B0`                                      | ★   |
| `0x006F3590` | `lua_rawequal`          | fastcall |                                                                       | ★   |
| `0x006F35E0` | `lua_lessthan`          | fastcall | calls `luaV_lessthan` at `0x006F8190`                                 | ★   |
| `0x006F3620` | `lua_tonumber`          | fastcall | returns double in `st0`                                               | ✓   |
| `0x006F3660` | `lua_toboolean`         | fastcall | tag != nil AND not (tag==1 && value==0)                               | ★   |
| `0x006F3690` | `lua_tostring`          | fastcall | calls `luaV_tostring` at `0x006F7C80` if number                       | ★   |
| `0x006F36E0` | `lua_strlen`            | fastcall | reads `TString.len` from interned string                              | ★   |
| `0x006F3720` | `lua_tocfunction`       | fastcall | returns `f` from `Closure.c.f`                                        | ★   |
| `0x006F3740` | `lua_touserdata`        | fastcall | full-userdata → `data + 0x10`; light-userdata → raw pointer; else 0   | ★   |
| `0x006F3770` | `lua_tothread`          | fastcall | tag == 8 → `[+0x08]`                                                  | ★   |
| `0x006F3790` | `lua_topointer`         | fastcall | jump-table on `tag-2` for indices 2..7                                | ★   |

CLAUDE.md refers to `0x006F3740` as `FrameScript_GetObject` because the
client uses it to retrieve `CFrameScriptObject *` from light/full userdata
slots. It's the standard `lua_touserdata`.

### Push

| VA           | Function                  | Conv.    | Notes                                                  | Conf |
|--------------|---------------------------|----------|---------------------------------------------------------|------|
| `0x006F37F0` | `lua_pushnil`             | fastcall | tag 0; copies "nil value" from `0x00CEEAC0`             | ✓   |
| `0x006F3810` | `lua_pushnumber`          | fastcall | tag 3; expects double on stack (8-byte arg)             | ✓   |
| `0x006F3840` | `lua_pushlstring`         | fastcall | tag 4; calls `luaS_newlstr` at `0x006F9D00`             | ★   |
| `0x006F3890` | `lua_pushstring`          | fastcall | tail-jumps to `lua_pushnil` if `s==NULL`; otherwise `strlen` then `pushlstring` | ✓ |
| `0x006F38C0` | `lua_pushvfstring`        | fastcall | calls `luaO_pushvfstring` at `0x006F5990`               | ★   |
| `0x006F38F0` | `lua_pushfstring`         | cdecl    | varargs — wraps `pushvfstring`                          | ★   |
| `0x006F3920` | `lua_pushcclosure`        | fastcall | tag 6; called from `FrameScript_RegisterFunction`       | ✓   |
| `0x006F39F0` | `lua_pushboolean`         | fastcall | tag 1; normalises bool via `setnz`                      | ✓   |
| `0x006F3A20` | `lua_pushlightuserdata`   | fastcall | tag 2; raw pointer in slot                              | ★   |

### Get / Set (table / metatable / fenv)

| VA           | Function                | Conv.    | Notes                                              | Conf |
|--------------|-------------------------|----------|-----------------------------------------------------|------|
| `0x006F3A40` | `lua_gettable`          | fastcall | calls `luaV_gettable` at `0x006F7CF0`               | ★   |
| `0x006F3B00` | `lua_rawget`            | fastcall | calls `luaH_get` at `0x006FA7A0`                    | ★   |
| `0x006F3BC0` | `lua_rawgeti`           | fastcall | calls `luaH_getnum` at `0x006FA700`                 | ★   |
| `0x006F3C90` | `lua_newtable`          | fastcall | tag 5; calls `luaH_new` at `0x006FA4F0`             | ★   |
| `0x006F3CF0` | `lua_getmetatable`      | fastcall | returns 0 or 1                                      | ★   |
| `0x006F3D50` | `lua_getfenv`           | fastcall |                                                     | ◦   |
| `0x006F3E20` | `lua_settable`          | fastcall | tail-jumped from `FrameScript_RegisterFunction` with `edx = LUA_GLOBALSINDEX` | ✓ |
| `0x006F3EA0` | `lua_rawset`            | fastcall | calls `luaH_set` at `0x006FA840`                    | ★   |
| `0x006F3F60` | `lua_rawseti`           | fastcall | calls `luaH_setnum` at `0x006FAD80`                 | ★   |
| `0x006F4020` | `lua_setmetatable`      | fastcall | returns 1                                           | ★   |
| `0x006F40D0` | `lua_setfenv`           | fastcall | returns 0/1                                         | ◦   |

CLAUDE.md refers to `0x006F3BC0` as `FrameScript_PushObject`; it's
`lua_rawgeti` — the client uses it to read indexed userdata slots. The
behaviour the comment describes (`push 0; mov edx, 1; mov ecx, L`) is
`lua_rawgeti(L, 1, 0)` on the table-at-1 with key 0.

### Calls / errors / coroutine

| VA           | Function                 | Conv.    | Notes                                                    | Conf |
|--------------|--------------------------|----------|----------------------------------------------------------|------|
| `0x006F4180` | `lua_call`               | fastcall | wrapper — calls `luaD_call` at `0x006F65A0`              | ★   |
| `0x006F41A0` | `lua_pcall`              | fastcall | uses pcall thunk at `0x006F6960`                         | ★   |
| `0x006F4250` | *internal pcall thunk*   |          |                                                          | ◦   |
| `0x006F4260` | `lua_cpcall`             | fastcall | wraps a C function for pcall                             | ★   |
| `0x006F4290` | *internal CFunction trampoline* |   | called via `lua_cpcall`                                  | ★   |
| `0x006F4320` | `lua_load`               | fastcall | reader / loader entry                                    | ★   |
| `0x006F4370` | `lua_dump`               | fastcall | requires tag 6 + `Closure.isC == 0`                      | ★   |
| `0x006F4940` | `lua_error`              | **cdecl** varargs | `(L, fmt, ...)` — printf-style; engine prepends `luaL_where` then `lua_concat`. Single-string callers OK (cdecl tolerates extra-arg-less calls); user-controlled strings should use `Error(L, "%s", msg)` to avoid format-string interpretation | ✓ |
| `0x006F6620` | `lua_resume`             | fastcall | `(L_co, nargs) → int`. Zero internal callers (coroutine lib was stripped from registration tables); linker kept the symbol. Stores nargs in a local, calls `luaD_rawrunprotected(L, &resume_inner, &nargs)` at `0x006F5DAC`; inner static helper at `0x006F67B0` | ★ |
| `0x006F6860` | `lua_yield`              | fastcall | `(L, nresults) → int (-1)`. Sets `0x10` flag on CallInfo state field rather than directly writing `L->status` like later Lua versions. Errors with "attempt to yield across metamethod/C-call boundary" or "cannot yield a C function" when called from the wrong context | ★ |
| `0x006F6B10` | *internal* `luaE_newthread` | fastcall | Inner allocator called by `lua_newthread` at `0x006F3030`. Allocates the new state, sets tag 8 via the push helper at `0x006F7B20`, inits via `0x006F6C40`, copies global-state fields from the parent. Returns `lua_State *` | ★ |

### Misc / GC / version

| VA           | Function                 | Conv.    | Notes                                            | Conf |
|--------------|--------------------------|----------|---------------------------------------------------|------|
| `0x006F43C0` | `lua_pushupvalues`(?) / internal | | writes `[ecx+0x60] = 1`                          | ◦   |
| `0x006F43D0` | *paired internal*        |          | writes `[ecx+0x60] = 0`                          | ◦   |
| `0x006F43E0` | `lua_getgcthreshold`     | fastcall | `[L+0x10][+0x24] >> 10` (kibibytes)               | ★   |
| `0x006F43F0` | `lua_getgccount`         | fastcall | `[L+0x10][+0x28] >> 10`                           | ★   |
| `0x006F4400` | `lua_setgcthreshold`     | fastcall | clamp / `<<10` / write `+0x24`                    | ★   |
| `0x006F4430` | `lua_version`            | cdecl    | `mov eax, 0x0087163C; ret` (returns Lua version string) | ★ |
| `0x006F4440` | `lua_atpanic`            | fastcall | calls `luaG_runerror` at `0x006FC780`            | ★   |
| `0x006F4450` | `lua_next`               | fastcall | calls `luaH_next` at `0x006FA2A0`                | ★   |
| `0x006F44E0` | `lua_concat`             | fastcall | calls `luaV_concat` at `0x006F84D0`              | ★   |
| `0x006F4560` | `lua_newuserdata`        | fastcall | tag 7; calls `luaS_newudata` at `0x006F9E30`     | ★   |
| `0x006F45B0` | `lua_pushupvalues`       | fastcall | pushes the running C closure's upvalues, returns how many | ★ |
| `0x006F4660` | `lua_getupvalue`         | fastcall | `(L, funcindex, n)`: pushes upvalue n, returns its name or null | ★ |
| `0x006F46E0` | `aux_upvalue`            |          | finds upvalue n of a function, returns its name   | ★   |

## Auxiliary library (`luaL_*`)

These wrap the public API and live in the same code block. Several call
back into the public API table above and into formatting helpers.

| VA           | Function                | Notes                                                    | Conf |
|--------------|-------------------------|-----------------------------------------------------------|------|
| `0x006F47B0` | `luaL_newmetatable` (or close) | calls upvalue helper                              | ◦   |
| `0x006F4810` | `luaL_argerror`         | format buffer + `lua_error`                                | ★   |
| `0x006F48A0` | `luaL_typerror`         | composes `expected X got Y` message                        | ★   |
| `0x006F48E0` | `luaL_where` / similar  | reads call stack info into a `luaL_Buffer`                 | ◦   |
| `0x006F4980` | `luaL_findstring` / `optstring` |                                                    | ◦   |
| `0x006F49E0` | `luaL_check*` (entry)   | composes prefix + `argerror`                               | ◦   |
| `0x006F4A70` | `luaL_getmetatable` (registry-keyed) | `lua_pushstring(name); lua_rawget(L, REGISTRY)` | ★ |
| `0x006F4A90` | `luaL_getmetafield`     |                                                            | ◦   |
| `0x006F4B20` | `luaL_argerror` (overloaded) | wraps with check                                      | ★   |
| `0x006F4B50` | `luaL_callmeta` / similar |                                                          | ◦   |
| `0x006F4BB0` | `luaL_buffinit`         | initialises `luaL_Buffer`                                  | ◦   |
| `0x006F4BE0` | `luaL_addlstring` / similar |                                                        | ◦   |
| `0x006F4C20` | `luaL_pushresult`       |                                                            | ◦   |

## Internal helpers (called by API but not part of public surface)

These are tagged in the call lists above; recording them here for
reference. Confidence is "structural" — names match the standard `l*.c`
filenames in Lua 5.0 by behaviour.

| VA           | Likely function          | Notes                                            |
|--------------|--------------------------|---------------------------------------------------|
| `0x006F3140` | `setobj` / TValue copy   | 16-byte memcpy                                    |
| `0x006F58B0` | `equalobj`               | called by `lua_equal`                             |
| `0x006F6050` | `luaD_precall`           | frame setup for a call; Lua closure: reads Proto (`+0xc` of the closure), `is_vararg` at Proto `+0x46` → `adjust_varargs`, `numparams` `+0x45`, `maxstacksize` `+0x47`; C closure: reserves 0x14 slots and calls the C function |
| `0x006F6200` | `adjust_varargs`         | stock 5.0: pads missing fixed params with nil, then `luaH_new(L, nvar, 1)` + `luaH_setnum` per extra arg + sets field `"n"` and pushes the table. So EVERY call to a vararg Lua function allocates one table, whether the body uses `arg` or `...` — keep vararg helpers off per-frame paths |
| `0x006F65A0` | `luaD_call`              | the actual call dispatcher                        |
| `0x006F6960` | `luaD_pcall`             | protected call dispatcher                         |
| `0x006F7340` | `luaD_growstack` (or checkstack) | invoked when stack needs space            |
| `0x006F7C20` | `luaV_tonumber`          | string→number coercion                            |
| `0x006F7C80` | `luaV_tostring`          | number→string coercion                            |
| `0x006F7CF0` | `luaV_gettable`          | metamethod-aware get                              |
| `0x006F7F40` | `luaV_settable`          |                                                   |
| `0x006F8190` | `luaV_lessthan`          |                                                   |
| `0x006F8330` | (rawequal helper)        |                                                   |
| `0x006F84D0` | `luaV_concat`            |                                                   |
| `0x006F9D00` | `luaS_newlstr`           | string interning                                  |
| `0x006F9E30` | `luaS_newudata`          | full-userdata allocator                           |
| `0x006F9E70` | `luaF_newCclosure`       | C-closure allocator                               |
| `0x006FA2A0` | `luaH_next`              | table iteration                                   |
| `0x006FA4F0` | `luaH_new`               | table allocator                                   |
| `0x006FA700` | `luaH_getnum`            |                                                   |
| `0x006FA7A0` | `luaH_get`               |                                                   |
| `0x006FA840` | `luaH_set`               |                                                   |
| `0x006FAD80` | `luaH_setnum`            |                                                   |
| `0x006FBAA0` | format helper for error  | used by `luaL_typerror` etc.                      |
| `0x006FC780` | `luaG_runerror`          | reached from `lua_atpanic`                        |

## Calling convention quick reference

```
fastcall       ecx = L     edx = 2nd arg (idx, etc.)   stack = rest
cdecl varargs  push args RTL; explicit `add esp, n` after call
```

`lua_pushnumber` is fastcall but takes a `double` — the double sits **on
the stack** (8 bytes) immediately after the return address, so the call
site looks like:

```asm
push high32             ; second dword of double
push low32              ; first dword of double
mov  ecx, L
call lua_pushnumber     ; cleans up its own 8 bytes (ret 8)
```

The same trick (callee-clean of the double) shows up at the end of the
function (`5E 5D C2 08 00` = `pop esi; pop ebp; ret 8`).

## How to use this when writing a hook

If you're adding a new Lua-callable function and don't want to roll your
own typedefs, prefer the helpers already exposed in this project's
[`Game::Lua` namespace](../src/Game.h) — `RegisterGlobalFunction`,
`RegisterFrameMethods`, and the `Push*` / `To*` wrappers. Those wrap the
public API entries listed above so you don't have to import the offsets
file-by-file.

If you do need a function that isn't wrapped yet, the typedef pattern is:

```cpp
namespace Game::Lua {
    using Type_t        = int  (__fastcall *)(void *L, int idx);          // 0x006F3400
    using ToString_t    = const char* (__fastcall *)(void *L, int idx);   // 0x006F3690
    using PushNumber_t  = void (__fastcall *)(void *L, double n);         // 0x006F3810
    using Error_t       = int  (__cdecl *)   (void *L, const char *fmt, ...); // 0x006F4940
    // pick fastcall vs cdecl per the table above
}
```

then point at the offset.

## Methodology, briefly

1. Find the registration plumbing first. `FrameScript_RegisterFunction`
   (`0x00704120`) is the entry point for every global Lua function
   registration; its body uses `lua_pushcclosure`, `lua_pushstring`,
   `lua_insert`, and `lua_settable` — each of those gives you a verified
   anchor address.
2. The reset/getter pair pattern from the DBC catalogue doesn't apply
   here, but the `LUA_GLOBALSINDEX` constant `0xFFFFD8EF` is unique enough
   that grepping for `81 FA EF D8 FF FF` (`cmp edx, -10001`) finds nearly
   every API function that takes a stack-index argument.
3. The `TValue` tag dispatch — `8B 00; 83 F8 N; je …` — paired with the
   tag values 0..8 gives you most of the `lua_isX` and `lua_toX` family.
4. Push functions are tiny and recognisable: `mov eax, [ecx+8]; mov dword
   [eax], <tag>; ...; add [ecx+8], 0x10; ret`.
5. For internal helpers (`luaV_*`, `luaH_*`, `luaS_*`, `luaD_*`): walk the
   call lists above. The `lua_*` public function in column 1 gives a name
   to the unknown helper it dispatches to.

Raw extracted dump kept as `raw_lua_funcs.txt` for cross-checking VAs.
