# Backporting inline `|T…|t` texture escape sequences to 1.12

Design + reverse-engineering map for teaching the 1.12 text engine to render
inline texture markup (`|Tpath:height:width:…|t`) in FontStrings / chat /
tooltips — the way 4.3.4+ does. The **rendering primitive is working**
([src/text/InlineTexture.cpp](../src/text/InlineTexture.cpp)); the remaining
work is inline positioning (parser + measure/emit). This file is the blueprint
so the multi-session build survives a context reset.

## SOLVED — the working rendering primitive (verified in-game: a coin icon renders in full colour)

An arbitrary texture draws through the text VB path as a coloured, alpha-blended
quad. The end-to-end recipe (all offsets in [Offsets.h](../src/Offsets.h) under
the "Inline texture escape" block):

1. **Load by path** — `LoadTextureByPath` replicates `FUN_00770200`'s load: build
   flags via `FUN_0058a980(&flags, DAT_00878cf0, 0,0, 0,0,0, 1, 0)`, then
   `FUN_00449d90(path, &desc, flags, 0, 1)` with a byte-identical 5-dword desc
   (`{PTR_FUN_007ffa10, 8, &self8, (&self8)|1, 0}`). Returns the **HTEXTURE**
   (0x15c-byte struct; path@+0xc, flags@+0x110, refcount@+4).
2. **THE KEY — residency + correct bind via `FUN_0044acf0(HTEXTURE, 1, 0)`.**
   This is the engine's own "get renderable texture" call (what the widget
   render uses every frame). It returns the bindable **CGxTexture** at
   `[HTEXTURE+0x140]` and, with force=1, drives the streaming load
   (`FUN_0044ad50` submits/prioritises it via `[HTEXTURE+0x138]`). **Calling it
   every frame is the live reference that makes the texture resident** — no
   hidden widget, no manual BLP decode. This was the entire multi-session
   blocker: WoW streams textures and only loads ones a live frame references; a
   standalone `FUN_00449d90` texture never loads on its own. Binding the raw
   HTEXTURE is `SetTexture([HTEXTURE+0x48]=0)` = a **white** quad; the D3D
   handle lives on the CGxTexture (`[CGxTexture+0x48]`), not the HTEXTURE.
   Dead ends that wasted time: manually invoking the decode callback
   `FUN_0044a260` (fills `[+0x120]` but not the bindable `+0x140`, and faults);
   binding `[HTEXTURE+0x48]` or `[HTEXTURE+0x120]` (both wrong offsets); forcing
   the streamable flag `tex[0x3c]|=0x80` (crashes in d3d9 — the bind's stream-in
   vfunc corrupts the resource).
3. **Bind** — `FUN_00589e80(0x17, cgxTex)` (selector 0x17 = text texture stage).
4. **Colour** — the font stage's default stage-0 COLOROP is already MODULATE, so
   `texture.rgb × diffuse(white)` = the icon's true colours; texture alpha works
   too. No colorop override is needed for a plain white vertex. (If a tint is
   wanted later: COLOROP is D3DTSS internal state `0x42` via `FUN_005a2570`; the
   engine leaves COLORARG1 at the D3D default = TEXTURE. Do NOT poke it directly
   mid-draw — it leaks `MODULATE` to the whole UI when a fault skips the restore.
   Use the engine's selector `0x1f` (value→COLOROP/ALPHAOP preset table at
   `DAT_0080a25c`/`DAT_0080a274`; value 1 = MODULATE/MODULATE).)
5. **Quad** — lock the shared dynamic VB `FUN_0058a140(0, 0x18, 0x800)`, get the
   write ptr `FUN_0058a080`, write **4 verts** of the format
   `{float x,y,z; u32 colorBGRA; float u,v}` (stride 0x18) in corner order
   **TL, TR, BL, BR** (matches the shared quad index buffer `{0,1,2, 2,1,3}` and
   the device cull winding), submit `FUN_005c8f40(&handle, 4)`, unlock
   `FUN_0058a0a0(handle, 0)`.
6. **Where** — co-hook the text paint pass `FUN_005c8fe0` and draw after the
   original glyph flush (device is in the correct textured-UI state there).
   Coordinate space is **per-layout pixels, y-down**; glyph verts live at small
   local coords (a probed glyph was at ~(9,33)–(17,50)). Draw coords must be
   within the layout's rendered area (a fixed (100,100) test lands in chat).

The build-side gotcha: the D3D device (default backend, `FUN_0058dd70`) is
actually **OpenGL** (`glMaterialfv`/`glFogi`) — the D3D backend is `FUN_0058ba70`
(`FUN_00598ce0`, vtable `PTR_FUN_00809ef8`); its state applier is `vtable[1]` =
`FUN_005a2f00`, which maps engine selectors to D3D via `FUN_005a2570`
(SetRenderState `+0xe4`, SetTextureStageState `+0x10c`, SetSamplerState `+0x114`,
SetTexture `+0x104`). Selector map (shared across GL/D3D backends): 7=blend,
0x10=depthtest, 0x14=cullface, 0x17-0x1e=texture bind (8 units),
0x1f-0x26=colorop/alphaop preset (unit 0-7).

## Remaining work — inline positioning

The primitive draws at arbitrary `(x0,y0,x1,y1)`. To finish: parse
`|Tpath:height[:width…]|t` (port the 4.3.4 field parser `FUN_00617ba0` → 0x40
descriptor, below), walk the text to compute each `|T`'s pen x within its line,
and draw the icon there. Measure loop `FUN_005c6940` and draw builder/emitter
`FUN_005cdc20`/`FUN_005ccbe0` are the integration points (segment approach:
delegate non-`|T` runs to the originals, advance the pen, insert an icon quad).

## Resume context (post-compaction) — current code state + how to continue

**Everything lives in [src/text/InlineTexture.cpp](../src/text/InlineTexture.cpp)**
(a new `src/text/` dir; CMake globs it). Offsets are in
[Offsets.h](../src/Offsets.h) under the "Inline texture escape" block +
`FUN_TEXTURE_GET_RENDERABLE` (0x0044ACF0). The module:
- `LoadTextureByPath(path)` — real, cached, returns the HTEXTURE (working).
- `DrawTexturedQuad(...)` — real, working: resolves the bindable CGxTexture via
  `FUN_0044acf0(tex, 1, 0)`, binds it, writes 4 verts (order TL,TR,BL,BR),
  submits. `order` param selects vertex permutation (kOrders) — order 0 is
  correct; the others were winding-calibration and can be dropped.
- Paint co-hook on `FUN_005c8fe0` (`Paint_h`) — draws the armed test quad after
  the original. **Off by default**; only draws once armed from Lua.
- SEH-wrapped `SafeDraw` + `g_drawStage`/`CaptureFault` capture fault
  stage/address into `Stats` — scaffolding, remove once positioning is wired.

**Lua diagnostic surface (all scaffolding — strip when the real feature lands):**
- `_classicapi_InlineTexDraw(x0,y0,x1,y1[,z[,colorARGB[,order[,colorOp]]]])` —
  arm the test quad (auto-loads a coin if nothing loaded). `"off"` disables.
- `_classicapi_InlineTexLoad(path)` → texPtr, vtable, flagByte.
- `_classicapi_InlineTexStats()` → drawCalls, faults, faultStage, texPtr,
  enabled, d3dHandle, texGen, devGen, faultAddr, faultCode.
- `_classicapi_InlineTexProbe()` — captures a real glyph's 4 VB corners (used to
  calibrate the coordinate space; the space is per-layout pixels, y-down).
- `_classicapi_InlineTexUseWidget(region)` — point the draw at an existing
  Texture widget's `[region+0xcc]` HTEXTURE (was used to debug residency).

**In-game verification:** `/script _classicapi_InlineTexDraw(100,100,150,150)`
renders a coloured coin at chat-top. This is the proof the primitive works.

**Build / deploy (learned this session):**
- `set -o pipefail; cmake --build build --config Release 2>&1 | tail -5 && cp build/Release/ClassicAPI.dll "C:/WoW/Octo/dll/ClassicAPI.dll"`.
  The `set -o pipefail` matters — without it a failed build still runs the `cp`
  and deploys a stale DLL.
- **The client must be fully EXITED to link** — it loads
  `dll_local\ClassicAPI.dll` which is a **symlink to `build/Release/ClassicAPI.dll`**,
  so the running game locks the linker's output. (The `cp` to `dll\` is
  belt-and-suspenders; the symlinked build output is what actually loads.)
- **DLL changes need a full client restart** (not `/reload`). Every test round =
  exit game → build → relaunch. Expect this cadence.

**Immediate next step (slice 1):** port the 4.3.4 field parser `FUN_00617ba0`
(→ 0x40 descriptor, layout in the 4.3.4 reference section above) as a standalone
C++ function (needs 1.12 strchr `FUN_0040fa50`-equivalents / atoi / atof — verify
addresses in 1.12). Then co-hook the measure loop `FUN_005c6940` + draw builder
`FUN_005cdc20`: for a `|T`-containing line, delegate non-`|T` runs to the
original (call `g_orig` on plain segments, reading back the pen advance) and
insert an icon quad (via the working `DrawTexturedQuad`, but recorded per-layout
and flushed in the `FUN_005c8fe0` co-hook so it draws at the real pen x/y). Start
with fixed-size `|Tpath:height|t` (slice 1 in Incremental slices).

## Goal & spec

Render the modern inline-texture escape (warcraft.wiki.gg/wiki/UI_escape_sequences#Textures):

```
|Tpath:height[:width[:offsetX:offsetY[:texWidth:texHeight:left:right:top:bottom[:rV:gV:bV]]]]|t
```

Atlas markup (`|A:atlas:…|a`) is Legion+ (needs an atlas DB 1.12 lacks) — out
of scope. FileDataID paths are Legion+ — 1.12 resolves texture PATH strings
only.

## Confirmed: 1.12 has ZERO inline-texture support

In-game test on the target client — all of these render as literal text, no
icon:

```
|TInterface\Icons\INV_Misc_Coin_01:16|t
|TInterface\Icons\INV_Misc_Coin_01:16:16|t
|TInterface\Icons\INV_Misc_Coin_01:16:16:0:0|t
```

Root cause (see 1.12 map below): the shared escape tokenizer has no `T`/`t`
case, so `|T` falls through to "literal `|` + ordinary glyphs".

## Reference: 4.3.4 (`C:\WoW\Proudmoore\apwow.exe`, build 15595)

4.3.4's text pipeline is a shared `|`-tokenizer feeding per-purpose loops that
`switch` on the returned token type. It DOES support `|T`.

- **Tokenizer `FUN_00613980`** — reads one token, returns a type code, bytes
  consumed (`*param2`), payload char (`*param5`). `|`-switch token types:
  `1`=color(`|c`+8 hex), `2`=reset(`|r`), `3`=break(`|n`/newline), `4`=`||`,
  `5`=`|H`, `6`=`|h`, **`7`=texture(`|T`…`|t`)**, `8`=`|t`, `9`/`10`=`|K`/`|k`
  callback, `0xb`/`0xc`=`|W`/`|w`, `0`=ordinary glyph. The `T` case scans
  forward to the closing `|t` and returns 7 with the whole span's byte length.
- **Field parser `FUN_00617ba0(tokenStart, &desc, size)`** — splits the `|T`
  payload on `:` (helpers: `FUN_0040fa50`=strchr, `FUN_004b3d00`=atof,
  `FUN_004b3480`=atoi) into a **0x40-byte descriptor**:

  | off | field |
  |----|-------|
  | `+0x10`/`+0x14` | path ptr / length (up to first `:`) |
  | `+0x18` | height (font-scaled) — required |
  | `+0x1C` | width (defaults to height) |
  | `+0x20`/`+0x24` | offsetX / offsetY |
  | `+0x28`–`+0x34` | 4 normalized texcoords (texel ÷ texW/texH) |
  | `+0x3C` | hasTexCoords flag |
  | `+0x38`–`+0x3B` | vertex B/G/R/A |
  | `+0x3D` | hasColor flag |

- **Measure loop `FUN_00618430`** — walks tokens; `case 7` calls
  `FUN_00617ba0` and adds the texture width to the pen.
- **Draw emitter `FUN_0061ea10`** (analog of 1.12 `FUN_005ccbe0`) — its
  `case 7` is the texture draw:
  1. `FUN_00617ba0(tokenStart, &desc)` — parse fields into the descriptor
     (stored inline on the line-state struct at `param7+0xb`, 0x10 dwords).
  2. Compute pen x, width (`desc.width[+0x18 dword = param7[0x12]] * scale`),
     and y from the justify mode `[obj+0x58]` (0=baseline, 1=center, 2=top).
  3. Copy the descriptor to a stack struct and call **`FUN_0061e820`** — the
     inline-texture quad emit. (`FUN_0061dd50` handles a region-state 1→2
     transition when a texture interrupts a hyperlink capture.)
- **`FUN_0061e820`** — normalizes the 4 quad corners (÷ screen w/h from
  `DAT_00dd9ea0`/`DAT_00dd9ea4`) and calls `FUN_0061e410(1, &verts)`.
- **`FUN_0061e410(count=1, &verts)`** (`__thiscall`, ECX = a vertex-buffer
  object) — appends `count` quads of **0x40 bytes** (one quad = pos + texcoords)
  to a **dedicated inline-texture vertex buffer**, growing it as needed
  (`FUN_0061db40`). So inline textures accumulate in their OWN batch, separate
  from the per-font-page glyph batches, each quad carrying its own texture
  (bound at flush). This is the concrete confirmation of the "separate quad
  path" — the backport must add such a batch + flush on the 1.12 side.

## Target: 1.12 (`C:\WoW\Octo\WoW.exe`, build 5875) injection map

Same architecture as 4.3.4, different addresses and token numbering. Reached
from `Script_GetStringWidth` (`FUN_0079e510`) → `FUN_00772890` →
`FUN_0044d670` → `FUN_005c2050` → the loops.

- **Shared tokenizer `FUN_005c2810`** — `~11 callers`. `|`-switch token types:
  `0`=color(`|c`), `1`=reset(`|r`), `2`=break(`|n`/newline), `3`=`||`,
  `4`=`|H`, `5`=`|h`, `6`=ordinary glyph. **No `T`/`t` case** → `|T` hits the
  fall-through (`*param5=0x7c; return 6`) = literal `|` then glyphs. Adding a
  texture token means picking an unused code (e.g. `7`).
- **Measure loop `FUN_005c6940`** — `switch`: `0/1/4/5` zero-width, `2` break,
  default glyph (advance via `FUN_005c6b70`; kerning `FUN_005ca2d0`/
  `FUN_005ca4b0`). No texture case.
- **Fit/width loops** `FUN_005c6c50` (chars-into-width + per-char x, for
  cursor/wrap) and `FUN_005c7470` (truncation/ellipsis). Same switch shape.
- **Draw builder `FUN_005cdc20`** — reads the layout object's text at `[+0x48]`,
  handles line-wrap / justify (`[+0x54]`) / color state, per line calls the
  glyph emitter and `FUN_005cd310` (hyperlink rect registration).
- **Glyph emitter `FUN_005ccbe0`** — the real quad emitter. Per glyph:
  `FUN_005cabd0(fontFace,char)` → glyph record (atlas **page** at `rec[10]`,
  advance `rec[0x12]`, texcoords `rec[0x18..0x1b]`), then appends 4 verts to a
  **per-atlas-page** batch at `[obj + page*4 + 0xa0]`. `case 0/1` set/reset
  color, `4/5` open/close hyperlink region, default = glyph.

### The architectural wrinkle

Glyph quads batch **per font-atlas page**. An inline `|T` texture is an
arbitrary texture, not a font page, so it cannot join that batch — it needs a
**separate textured-quad draw** (its own draw call, correctly ordered against
the glyph batches so it appears inline). This is the substantive work, not the
tokenizing. 4.3.4 solves it exactly this way — captured below: its draw `case 7`
appends the quad to a dedicated inline-texture vertex buffer at
`renderObject+0x9c`, separate from the per-font-page glyph batches (see the
`FUN_0061ea10` / `FUN_0061e820` items under Open items).

## Strategy

The tokenizer is shared by ~11 callers, so DON'T just make it return a new
type — the other callers' `switch`es would hit `default` and mis-handle it.
Instead:

1. **Reimplement (co-hook) the measure loop `FUN_005c6940` and the draw
   builder `FUN_005cdc20`/emitter `FUN_005ccbe0`** — copy existing logic
   (fully decompiled), delegate every non-texture token to the original
   handling, and add an inline `|T…|t` case: measure → advance pen by texture
   width; draw → emit the arbitrary-texture quad.
2. **Port `FUN_00617ba0`** (field parse → 0x40 descriptor). Standalone, safe to
   write now from the 4.3.4 decompilation; needs 1.12 equivalents of strchr/
   atof/atoi (trivial or already in `Offsets.h`).
3. **Leave the shared tokenizer `FUN_005c2810` and the other ~9 callers
   untouched** — they keep rendering `|T` literally (wrap/cursor math slightly
   off inside a texture in v1 — cosmetic, extend later).
4. **Texture load by path** — reuse the engine's `Texture:SetTexture` loader;
   **quad emit** — the primitive 4.3.4's draw case uses (to pin).

Risk: the hot text path (every string every frame). But these are
deterministic C functions, NOT the contended nampower/SuperWoW targets, so the
MinHook-collision concern is low. Reimplementing two complex loops faithfully
is the real cost.

## Incremental slices

1. Fixed-size `|Tpath:height|t` — render + width only (ignore width/offset/
   texcoord/color). Proves the end-to-end quad path.
2. width, offsetX/Y.
3. texcoords (`texW:texH:l:r:t:b`).
4. vertex color.
5. correct wrap/cursor (extend `FUN_005c6c50`/`FUN_005c7470`).

## Open items / next steps

- [x] **4.3.4 draw-side `case 7`** — captured: `FUN_0061ea10` case 7 →
      `FUN_00617ba0` (parse) + `FUN_0061e820`→`FUN_0061e410` (append quad to a
      dedicated inline-texture vertex buffer). Separate batch, per-quad texture.
- [x] **4.3.4 inline-texture buffer located** — `FUN_0061e820` (`RET 0x40`,
      i.e. a 0x40-byte quad struct by value) does `ADD ECX,0x9c` before
      `FUN_0061e410`, so the dedicated inline-texture vertex buffer is at
      **renderObject+0x9c** (glyph batches are the `+0xa0 + page*4` array — the
      two are adjacent but distinct). Quad struct: positions at `+0x8..+0x14`,
      texcoords at `+0x20..+0x2c` (rest = z / texture handle / color). Appended
      one quad at a time via `FUN_0061e410(this=buf@+0x9c, count, &quad)`.
- [x] **Flush + path→texture load** — NOT traced on the 4.3.4 side by design;
      its purpose (the flush + per-quad texture bind + path→load mechanism) is
      fully answered on the **1.12 side** below: paint/flush `FUN_005c8fe0`,
      bind `FUN_00589e80(0x17,tex)` + draw `FUN_005c8f40`, load `FUN_00449d90`.
      4.3.4's own `+0x9c`-buffer flush is not needed — we implement against 1.12.
- [x] **1.12 path→texture loader** — `FUN_00770200(textureObj, path, flag,
      blend=DAT_00878cf0, flag)`. It's the string-path branch of
      `Script_Texture_SetTexture` (`FUN_0079bb40`, method-table entry at
      `0x0087c1a0`). This is how a `|T` path becomes a bound texture.
- [x] **1.12 batch render + textured-quad GPU primitive — FOUND.** The paint
      pass is `FUN_005c8fe0(layoutObj)`: it ensures each line built
      (`FUN_005cd6a0`), then per font page (stride 0x18, indices 0..7 over
      `[obj+0x18c+page*0x18]` = the page's texture object) does:
      - **bind texture:** `FUN_00589e80(0x17, texturePage)` (0x17 = the texture
        render-state selector),
      - **lock dynamic VB:** `FUN_0058a140(0, stride=0x18, 0x800)` → handle;
        `FUN_0058a080(handle)` → vertex data ptr,
      - copy the page's built verts from the per-page batch at
        `[lineObj + 0xa0 + page*4]` (`FUN_005ce090`=count, `FUN_005ce0c0`=copy),
      - **draw:** `FUN_005c8f40(&handle, vertCount)` (flush/submit),
      - **unlock:** `FUN_0058a0a0(handle, 0)`.

      **Vertex stride = 0x18** (24 bytes = pos + uv + packed color). So an inline
      `|T` quad = 4 (or 6) such verts, bound to its own texture. Reuse path for
      the `|T` draw: `FUN_00589e80(0x17, inlineTex)` → write quad verts →
      `FUN_005c8f40`. **`FUN_005c8fe0` is the co-hook site for strategy B** —
      after the glyph batches are drawn, draw the frame's collected `|T` quads.
- [x] **Path → bindable texture handle — RESOLVED.** `FUN_00770200`
      (SetTexture-by-path) internally calls **`FUN_00449d90(path, &desc, flags,
      0, 1)`** — the general path→CGxTexture loader (normalizes path, by-name
      cache, creates on miss via `FUN_0044a140`/`FUN_0044a310`, returns a
      **fallback texture on failure so never null**) — and stores the handle at
      `textureObj+0xcc`. That handle is the SAME CGxTexture type
      `FUN_00589e80(0x17, tex)` binds (the `tex[0x3c]` streamable flag +
      non-null `*tex` that `FUN_00593840` checks are CGxTexture fields; font
      pages and Texture widgets bind identically). So a `|T` draws as:
      `tex = FUN_00449d90(path,&desc,flags,0,1)` → `FUN_00589e80(0x17,tex)` →
      write 4 verts (stride 0x18) → `FUN_005c8f40(&vb,n)`. No scratch widget
      needed if we replicate `FUN_00770200`'s descriptor setup
      (`FUN_0058a980(&desc, blend, …)` then `FUN_00449d90`); or, simplest, keep
      one scratch Texture object, `FUN_00770200(scratch,path,…)`, read
      `[scratch+0xcc]`. The internal by-name cache makes repeats cheap.

Two implementation strategies once the primitive is pinned:
- **A (mirror 4.3.4):** own a separate inline-texture vertex buffer, flush it
  in the paint pass alongside the glyph batches. Faithful, more integration.
- **B (immediate):** at paint time, draw each collected `|T` quad immediately
  via the GxU primitive (bind `FUN_00770200`'d texture, draw one quad). Simpler;
  inline textures are few, so per-quad draw calls are fine. Likely the better
  first cut.
- [ ] Confirm 1.12's tokenizer flag bits (`param4 & 0x100/0x200/0x400/0x800`)
      that gate color/escape handling, so the reimplemented loops pass them
      through unchanged.

## Reality check on scope

This is the largest feature in the project by a wide margin: faithfully
reimplementing 1.12's glyph emitter `FUN_005ccbe0` (a long, intricate function)
+ the measure loop, plus standing up a separate inline-texture batch + flush,
all on the per-frame text path, verifiable only by in-game iteration. Land it
in the slices above, smallest first, and expect multiple build/test rounds.
