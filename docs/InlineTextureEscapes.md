# Backporting inline `|T…|t` texture escape sequences to 1.12

The 1.12 text engine shows `|Tpath:height:width:…|t` markup as literal text.
This project makes it draw the icon inline in FontStrings, chat, and tooltips —
the way 4.3.4 and later clients do. The feature is complete and shipped
([src/text/InlineTexture.cpp](../src/text/InlineTexture.cpp),
[src/text/InlineTexturePool.cpp](../src/text/InlineTexturePool.cpp)).

This file is the reverse-engineering map. **Current design** below states what
ships today. Everything from **Goal & spec** down is the historical RE trail —
the 4.3.4 study, the 1.12 injection map, and the bring-up plan. It is kept for
the addresses and the reasoning, not as a description of the current code.

## Current design (shipped)

Each icon renders as an **engine-managed region** (a pooled `CSimpleTexture`)
anchored to its owning FontString. The engine draws the region every frame and
moves it with its line, so texture residency and scroll tracking are correct by
construction. (A raw-GxU-quad renderer existed during bring-up and was removed
in `56c2670`; its RE is the "REMOVED" section below.)

The DLL hooks the engine's own text pipeline — no companion addon:

- **Positioning** — a co-hook on the glyph emitter `FUN_005ccbe0` records each
  icon at its pen position and delegates the plain text runs to the engine
  (segmented delegation, below). The pen read-back needed a fix: the engine's
  pen is lazy and omits the last glyph's own advance, so a coin sat on its
  digits; the emitter now adds the true terminal advance from the engine's own
  glyph-advance helpers (`550cf5e`).
- **Rendering** — the paint-pass co-hook `FUN_005c8fe0` walks the icon records
  and queues each to `Text::InlineTexturePool`, which places the region on the
  next frame tick (never mid-render).
- **Measure** — three cold FontString-level co-hooks make measure match the
  drawn result: width (`FUN_00772890`), height (`FUN_007729B0`), and wrap
  (`FUN_005C7260`). `GetStringWidth`, `fontstring:GetStringHeight`, wrap
  breaks, and line height all count the icon.
- **Tall icons** — an icon taller than the font grows its line height, so it
  does not overlap its neighbors, and it centers in the grown line. A hyperlink
  that contains a tall icon is hoverable across the whole icon
  (`49934b0`, `b9d0705`).
- **Chat fade** — icons fade in lockstep with their chat line (`3ca3b7e`).
- **Editboxes** — an editbox keeps the raw markup, so the caret stays aligned
  (two complementary signals, below).
- **Payload** — the full
  `|Tpath:height:width:offsetX:offsetY:texW:texH:left:right:top:bottom:r:g:b|t`:
  size, pen offset, sprite-sheet crop, and vertex tint. BLP and uncompressed
  TGA.
- **Anti-spoof** — chat strips player-injected `|T` icons.

The one measure path still icon-blind is the substring width (`FUN_00772AE0`),
which nothing consumes yet. The Lua control surface is one kill switch,
`_classicapi_InlineTexEnable` (the SEH latch trips it on a flush fault); the
bring-up tune / stat / probe functions were removed (`5e5677b`, `6800a9a`).

## REMOVED (bring-up record) — the raw-GxU-quad rendering primitive

This was the FIRST renderer: each icon drawn as a raw textured quad through the
text VB path. It shipped, then was removed in `56c2670` for the engine-region
model (regions are resident by construction; the quad path needed the texture-
residency machinery below and still flickered on VRAM pressure — see
`docs/InlineTextureResidency.md`). This section is the address record for the
removed code, not a description of the current renderer. `LoadTextureByPath`,
`DrawTexturedQuad`, and the GxU device offsets it names are gone from
`Offsets.h`; the values live here and in git history.

An arbitrary texture draws through the text VB path as a coloured, alpha-blended
quad. The end-to-end recipe:

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

## SOLVED — positioning, measure/wrap, editbox exclusion, texcoords

The primitive draws at arbitrary coords; the rest is knowing WHERE and WHEN.
Everything is in [src/text/InlineTexture.cpp](../src/text/InlineTexture.cpp);
offsets under the "Inline texture escape" + positioning blocks in
[Offsets.h](../src/Offsets.h).

### The text render hierarchy (verified)

```
frame → renderNode [frame+0x118] → group [renderNode+0x18]
      → node [group+0x18]        → line [node+0x24]  ← the emitter's `this`
```

- Main UI render `FUN_007657d0(DAT_00cf0bd8)` walks strata → frames, calls
  `FUN_0076fb00(renderNode)` per frame's render node.
- `FUN_0076fb00` draws the frame's Texture regions then `FUN_005c1ef0(group)` →
  `FUN_005c8b70(group)` → `FUN_005c8fe0(node)` per node.
- `FUN_005c8fe0(node)` = the **paint pass** we co-hook: ensures each line built
  (`FUN_005cd6a0` → `FUN_005cdc20` → the emitter), then binds each font page and
  copies the line's per-page glyph batches into the dynamic VB, translated by the
  line origin `[line+0x70]/[+0x74]`.
- The **emitter** `FUN_005ccbe0(line, text, len, colorState, penXYZ, pageMask,
  linkState)` builds one wrapped line's glyph quads. `this` (the "line"/node the
  icon list keys on) owns batches `[+0xa0+page*4]` and origin `[+0x70]`. **Note
  the render node the emitter sees has NO back-pointer up to the frame** — the
  editbox discriminator had to be found another way (see below).

### Positioning — segmented delegation (co-hook the emitter)

We do NOT reimplement the emitter's intricate glyph vertex math. Instead, in the
`FUN_005ccbe0` co-hook, for a line containing an inline texture:

1. Split the line at icon boundaries. Render each **plain run** by calling the
   ORIGINAL emitter on just that substring — the engine still draws every glyph.
2. Thread the pen across runs: on return the emitter leaves the final node-local
   pen x in **`linkState[4]`**, stored with `FSTP dword` — a **FLOAT bit
   pattern**, not an int. Read it as a float (`*(float*)&linkState[4]`); an
   int-cast yields garbage (this bug hid the icon off-screen and dropped the
   trailing text).
3. Record an `IconRecord` at the pen (node-local `x`,`y`) into `g_nodeIcons[line]`
   and advance the pen by the icon width. No draw runs during the build — icons
   are recorded and **flushed** in the `FUN_005c8fe0` paint co-hook, which
   computes each icon's screen rect (line origin `[line+0x70]/[+0x74]` maps
   node-local → screen, the same translate the glyph copy `FUN_005c8710`
   applies) and queues it to `Text::InlineTexturePool` as a placement RELATIVE
   TO THE OWNING FONTSTRING. The pool applies placements on the next frame tick.
4. Restore `penXYZ[0]` on exit (the original never writes it; the draw builder
   does NOT reset it between left-justified lines, so leaving it mutated
   cascade-shifts every following line).

### FontString pipe-doubling — the escape arrives as `||T…||t`

The FontString text sanitizer **doubles any pipe that doesn't begin a recognized
escape** so it renders as a literal `|`. 1.12 doesn't know `|T`, so a caller's
`|Tpath:h|t` reaches the emitter as `||Tpath:h||t` (verified: emitter text length
+2, pipes doubled). The detector therefore matches BOTH the doubled form (the
real-world case) and a clean `|T` (should one ever arrive undoubled). Normal
doubled pipes (`|| `) never match because the next char must be `T`.

### bit-3 batch-clear mode

The emitter clears the line's page batches at its top only when node flag
`[line+0x5c] & 0x08` is set (the FontString/static case). Calling the original
per segment would then wipe all but the last run. Handled by doing the clear
ONCE per build (a `len==0` original call with bit 3 still set), then clearing
bit 3 so the per-segment calls APPEND, restoring flags on exit. Bit 5 (shadow)
isn't set for these nodes, so the only added work is a harmless colour append.

### Multi-line / multi-icon accumulation

The draw builder walks a node's wrapped lines by calling the emitter repeatedly
on the SAME node, advancing the text pointer. So the icon list is cleared ONCE
per build — on the first wrapped line, detected by `text == [node+0x48]` (the
node's text start) — and icons ACCUMULATE across lines. Multiple icons per line
fall out of the segmentation loop naturally.

### Vertical centring — font-relative

`penXYZ[1]` sits near the text TOP, so the icon centre = `penY + fontHeight *
g_centerFrac` (0.6, calibrated in-game across chat + pfUI's bubble). Font height
comes from the engine's own `FUN_005c6fa0(flag, [node+0x1c])` (the emitter's
glyph-sizing call), so centring holds across font sizes without a fixed pixel
nudge. A TALL icon (height > font) centres in its GROWN line (see the tall-icon
sections in Current design) — the string-height co-hook adds the overflow, so
the extra room sits half above and half below the text.

### Measure / wrap — co-hook the tokenizer

Line wrap is decided by the measure/fit functions (`FUN_005c6940`, `FUN_005c7470`
via `FUN_005c7260`), which all share the `|`-tokenizer `FUN_005c2810`. Left
alone, they count the hidden path text and wrap early. Fix: co-hook the
tokenizer — when it's at an inline-texture span, consume the whole span as ONE
near-zero-width token so every measure/fit caller ignores the path characters.
The emitter detects icons independently (it scans the raw bytes), so the draw
path is unaffected.

### Measure width: FIXED at the fs-level choke (`GetStringWidth` counts icons)

The tokenizer reports an icon span as a **near-zero-width** token, so at the
gxu loop level every measure caller counts the icon as ~0 pixels wide. (The
emitter still reserves the render width, so text after an icon positions
correctly.) An earlier version of this doc concluded that the gap could not
be closed cleanly, because the gxu engine has **no single cold choke**:

- `FUN_005c6940` (width), `FUN_005c7470`/`FUN_005c7260` (wrap), and the
  hit-test loop each tokenize and accrue width independently.
- The token contract has no width field.
- The per-glyph helpers (`FUN_005cabd0`/`FUN_005c6b70`) are the hottest text
  functions in the engine.

That analysis was right about the gxu level. The choke sits ONE LEVEL UP:
**`FUN_00772890` = `CSimpleFontString::GetStringWidthInternal`**
(`Offsets::FUN_FONTSTRING_STRING_WIDTH`) is cold, MinHook-safe, and the single
funnel for exactly 4 callers:

- `Script_GetStringWidth` (0x0079E510)
- the GameTooltip auto-size (0x00530640)
- the editbox caret positioner (0x0077DE70)
- the layout effective-width vmethod (`FUN_00772930` = fs+0x24 vtbl +0x1C,
  which feeds auto-width fontstring layout)

It lazily computes into the `fs+0xFC` cache (anchor units, 0.0 sentinel) via
the measure core `FUN_0044D670`, then divides the pen-space result by
`fs+0x7C`.

The co-hook lives in `src/text/InlineTexture.cpp`. It calls the original,
then adds `SumIconAdvances(text) / K` when two gates pass:

- `InlineInterceptActive(text)` is true — the **same predicate** the tokenizer
  hook stands down on, factored into one function so measure and render can
  never disagree.
- The fs is not an editbox: `fs+0x120 & 0x1000` is clear (the fs-level bit
  `FUN_0044D670` translates into the gxu editbox flag 0x40).

`K = [0x832A4C] × 1024 / [0x832A44]` (≈1468) is the anchor→pixel push factor
— the same conversion `Script_GetStringWidth` applies in reverse when it
pushes. The per-icon advance comes from `IconAdvancePen`, the **same helper**
the emitter reserves with (width + 1.5×pad + positive offsetX) — so the
measured width matches the rendered width by construction.

Unit trap (hit on first flight): escape sizes are UI PIXELS. Do NOT divide
the icon sum by `fs+0x7C` — that field is the layout UI *scale* (~0.68–1.0).
The original's own `out / fs+0x7C` applies to a value the measure core
already ran through the `FUN_0041AD80` gxu→internal converter, not to raw
pixels. The `/0x7C` version measured a 16px icon as +44k px.

The hook NEVER writes the `fs+0xFC` cache: the original can serve the cached
value, so the icon sum is re-derived and re-added per call (idempotent).

The same hook also fixes two layout consumers for free: tooltips auto-size
wide enough for icon-bearing lines, and auto-width fontstrings lay out at
their true rendered width.

### Wrap: FIXED at the shared stepper (`FUN_TEXT_WRAP_STEPPER` = 0x005C7260)

Wrap decisions also measured icons as ~0, so a chat line with prefix icons
broke as if the icons were not there and the emitter's real advances ran past
the right edge. The choke is the wrap-stepper dispatcher `FUN_005C7260`: it
lays out one wrapped line per call, and its exactly 4 callers are EVERY wrap
consumer — the draw builder `FUN_005CDC20` (render breaks), the height
measure `FUN_005C2070`, the chars-that-fit counter `FUN_005C21C0` (ellipsis),
and the break-array computer `FUN_005C2430` (behind `FUN_00772B60`).

The co-hook shrinks the `wrapWidth` argument by a **minimal feasible
shrink**, gated on the same `InlineInterceptActive` predicate plus the flags
editbox bit 0x40 the dispatcher already carries. The stepper reports each
probe's measured (icons-at-zero) line width in `outWidth`, which gives a
direct feasibility check: the line renders inside the frame iff its icons
fit in the shrink plus the spare the break left (`iconSum ≤ s + (probeW −
outWidth)`). The hook probes sandboxed (local out-params plus a COPY of the
`p10` in/out state byte, so the caller's first-line state is not consumed):
s = 0 first (feasible → no shrink at all — the common short-message case),
escalate by the measured deficit when infeasible, bisect down after the
first feasible hit; ≤8 probes, and with no feasible hit the last escalation
target wins (wraps early rather than overflowing). The feasibility check
carries a HALF-GLYPH tolerance: an auto-width fontstring (single anchor, no
width — e.g. pfUI's addon-list labels) wraps at the icon-inclusive string
width our GetStringWidth hook feeds the effective-width vmethod, so its line
fits with exactly zero slack, and cross-computation drift between the two
hooks' unit chains (plus OUTLINE extras and the trailing-glyph quirk) read
"fits exactly" as "1px over" — shrinking a zero-slack line forces a wrap
that had no business existing. Genuine icon overflow is ≥ a full icon, so
half a glyph separates drift from real overflow. `outBreak` and
`outNext − text` are byte counts; `outBreak == len` means the whole text
fit. Two rejected earlier schemes, for the record: a naive whole-remaining-
text sum shredded an 8-icon line to one word per line, and a fixed-point
iteration on the icon count oscillated (a ~55-byte zero-width escape makes
the break↔icon-count coupling discontinuous — no fixed point exists).
`text` always points at the REMAINING text, so a continuation line whose
icons are behind it gets sum = 0. Because all four consumers share the hook,
render breaks, `GetStringHeight`, truncation, and break arrays shift
together. (A `_classicapi_InlineTexWrap(n)` ring dumped the last 8
icon-bearing calls during bring-up; removed with the other diagnostics in
`5e5677b`.)

Unit trap #2 (hit on first flight, like the width hook's): each caller passes
fontH/wrapWidth in its OWN space — the draw builder passes node text units
(node+0x1C / node+0x3C), and the fs-level callers (the path chat wraps
through) pass the much smaller anchor-converted space. Subtracting a raw
pixel advance annihilated the small widths to the floor and shredded
icon-bearing chat lines into 2-glyph fragments. The space-agnostic conversion
uses the engine's own convention: the measure loops realize their fontH param
as pixels via `FUN_TEXT_FONT_HEIGHT(flag, fontH)` (see `FUN_005c6940`'s final
scale), so px → caller units is exactly `fontH / fontHPx`. The hook computes
the icon sum in true pixels (the same value the emitter reserves) and scales
by that ratio.

**Chat hyperlink hover over inline icons works (verified).** An emote wrapped in a
hyperlink — `|Htel:name|h|T…|t|h`, the TwitchEmotes pattern — pops its tooltip when
you hover the *icon*, not just adjacent text. The hover region is NOT re-measured
on hover: the engine records each hyperlink's screen rect at layout time into a
`GXUFONTHYPERLINKINFO` array (32-byte entries: rect + link data) via `FUN_005cd310`,
called from the draw builder `FUN_005cdc20` — the same layout pass that runs the
line through our co-hooked tokenizer (`FUN_005c2810`) and wrap-stepper
(`FUN_005c7260`). So the recorded rect rides the same icon-aware position
accounting the wrap fix installs; the icon lands inside the hover region for free,
and hovering it fires the frame's `OnHyperlinkEnter`. This is a consequence of the
wrap fix, not a separate feature. (Ghidra caveat: the decompile of the
`FUN_005cd310` call args is mangled — a pointer-as-float + an uninitialized temp —
so this is confirmed by the call path plus in-game test, not the byte-level
formula.)

**Still icon-blind (accepted residuals):**

- The substring measure (`FUN_00772AE0`) still sees icons as ~0. It is a
  *separate* function from the hyperlink hover rect above (which works) — nothing
  consumes it yet; revisit it (also cold) if a consumer needs icon-aware substring
  widths.
- A ~≤1px artifact when an icon is the last token: the gxu width loop ends on
  the last *glyph's* ink width rather than its advance (`FUN_005c6b70` gets the
  remaining-text pointer), and a trailing icon shifts the previous glyph's
  treatment — the source of the old −0.8px measurement.
- An fs whose `fs+0xFC` cache was filled while the feature was toggled
  differently (`_classicapi_InlineTexEnable`) keeps its old base width until
  the next `SetText`/font change re-dirties it. Debug-toggle-only.

**Editbox measure stays raw (by design).** The caret/measure path reads the
focused editbox's INPUT buffer in place, and the tokenizer stands down on it
per-editbox (`TextInFocusedEditbox` pointer-range test — not a global focus
check), so caret math measures the raw `|T…|t` literal the editbox renders.
The width hook mirrors that with the `fs+0x120 & 0x1000` editable gate. One
marginal wobble: the focused single-line chat editbox's display fontstring
carries editable=0 (only multi-line editors set the bit), so the caret
positioner's line-boundary branch (multi-line-only in practice) can see an
icon-adjusted `FUN_00772890` on it. Closing that costs a per-token content
compare, which is not worth it.

### Editbox exclusion — two COMPLEMENTARY signals (bit 6 + focus global)

Editable text must show raw, editable `|T…|t` markup, not rendered icons — you
edit macros, tweak icon fields, copy/paste texture strings. **This is
retail-correct, verified in-game on a modern client:** typing
`|TInterface\Icons\INV_Misc_Coin_01:16|t` into the chat entry box OR a macro body
shows it **raw** (no icon) — Blizzard neutralizes player-*typed* escape sequences
as anti-abuse — while addon/system text (`AddMessage`, `GetCoinTextureString`)
renders. Retail's discriminator is the text's SOURCE (trusted vs player-typed),
not editbox-vs-display; a shift-clicked item link renders in the chat box because
it's inserted through the trusted path, but a typed `|T`/`|c` does not. Our
editbox-raw + display-renders split reproduces the same observable outcome. (One
mechanism-level divergence we accept: our editbox-raw uses the GLOBAL focus signal
below, so while an editbox is focused OTHER display text that rebuilds also
briefly shows raw; retail suppresses per-source. Cosmetic, and a per-editbox fix
would need hot-path text-buffer matching — not worth it.) 1.12's tokenizer,
unlike 4.3.4's, has no per-render texture-disable bit (4.3.4 uses tokenizer flag
**0x1000** to kill only `|T`, **0x800** to kill all escapes), and editbox text
nodes are NOT distinguishable by class from the render node — the emitter can't
reach the owning frame. It takes **two signals together**, not one (an earlier
version of this note wrongly called the focus global a redundant "belt-and-
suspenders fallback" — removing it renders the chat/copy boxes' `|T` as icons):

- **Node flag `[node+0x5c]` bit 6 (0x40) = EDITABLE** — catches the **macro
  editor** (`MacroFrameText`, flags `0x4D`), which builds its layout once,
  **un-focused**, so the focus global never sees it. But single-line inputs
  (chat entry box, `pfChatCopyBox`) have bit 6 **CLEAR** — their nodes are
  `0x0D`/`0x20D`, identical to display FontStrings — so bit 6 can't catch them.
- **Focused-editbox global `DAT_00cf4dc8`** (`InputFocused()`; holds the editbox
  with keyboard focus, cleared by `ClearFocus` = `FUN_0077e410`) — catches those
  single-line inputs. It's GLOBAL (true UI-wide, not per-node), which is why,
  while an editbox is focused, other display text that *rebuilds* also briefly
  renders raw. That's an accepted tradeoff: there is no per-node signal for
  single-line inputs, and when you're in an editbox seeing raw markup is what you
  want anyway.

**Why the tokenizer's bit-6 gate is load-bearing for RENDER, not just measure:**
when the emitter suppresses an editbox it delegates to the ORIGINAL emitter,
which **re-enters the tokenizer** to lay out glyphs (`FUN_005ccbe0` →
`FUN_005c2810`). On that render path `flags` is the node's own flags, so bit
0x40 makes the tokenizer stand down and the `|T` draws as literal text. Drop the
bit-6 gate and the (un-focused) macro editor draws **BLANK** — the tokenizer eats
the span as a zero-width token and the original emitter renders nothing (verified
in-game). Focused single-line inputs get the same "don't intercept" via
`Suppressed()` → `InputFocused()`. So both the emitter and the tokenizer consult
`Suppressed() || bit-6`; the two must agree or you get blank-vs-raw mismatches.

(Dead end: **monochrome render flag `DAT_00c2b9dc`** (`FUN_005c8b70` sets it per
group) — un-set at MacroFrameText's build time and flickered as a flush gate.
Dropped. The editbox class vtable is `0x0081c8c0`, its script-slot resolver
`FUN_0077a310` with focus slots at `+0x438`/`+0x440` — useful if a frame-level
hook is ever needed.)

### Descriptor fields + the vertical flip

`ParseIcon` handles the full positional payload
`path:height:width:offsetX:offsetY:texW:texH:left:right:top:bottom:r:g:b`.
Texcoords crop a sprite sheet to one cell: `u = left/texW, right/texW`,
`v = top/texH, bottom/texH` — e.g. `UI-RaidTargetingIcons` is a 256×256 sheet,
4×2 grid of 64×64, so marker N's cell is `256:256:<col*64>:<+64>:<row*64>:<+64>`.

**Vertex colour (`r:g:b`, 0-255):** the last three positional fields tint the
icon. They pack to `0xFFrrggbb` — the same `0xAARRGGBB` order the tokenizer's
`|cAARRGGBB` colour path builds (verified from `FUN_005c2810`'s colour branch),
so the value flows straight into the quad's vertex colour and the font stage's
default MODULATE COLOROP multiplies `texture.rgb × tint`. White (`255:255:255`,
the default) = untinted. Because parsing is positional, a tint needs all the
preceding fields present; the `TintIcon` helper passes `0`s for width..bottom
(texW/texH `0` disables the texcoord crop → full texture) so the colour lands in
the trailing slots.

**Texcoord order:** the region's 4-float texcoord setter
(`FUN_SIMPLETEXTURE_SET_TEXCOORD`) takes the rect **Y-FIRST INTERLEAVED**
(`{v0, u0, v1, u1}`), matching the engine's `{yA, left, yB, right}` rect
convention — diagnosed in-game when `{u0,v0,u1,v1}` rendered a raid-mark crop as
the wrong cell. v is top-down, same as the `|T` payload's top/bottom fields.
(The removed quad path had the inverse concern: the OpenGL backend put v=0 at
the bottom, so the quad mapped the TOP screen corners to v1 — kept here as the
backend note.)

### Build / deploy / test cadence

- `set -o pipefail; cmake --build build --config Release 2>&1 | tail -5 && cp build/Release/ClassicAPI.dll "C:/WoW/Octo/dll/ClassicAPI.dll"`
  — `pipefail` stops a failed build from deploying a stale DLL.
- **The client must be fully EXITED to relink** (it loads a symlink to the
  linker's output). DLL changes need a full restart, not `/reload`.
- **The embedded `!!!ClassicAPI` addon is symlinked**, so Lua-only changes (the
  `TextureTest()` harness in `Util/AddOnCompat.lua`) need only `/reload`.

### Remaining / optional

- **Measure width, height, wrap** — all DONE (the three fs-level co-hooks in
  Current design). Vertex-colour tint (`:r:g:b`) — DONE. The bring-up
  diagnostics (`_classicapi_InlineTexSuppress/Tune/Stats/ProbeFS/RegionCal/Wrap`
  and the capture globals) were REMOVED (`5e5677b`); only
  `_classicapi_InlineTexEnable` remains.
- **Animated emotes** — no DLL work is needed: a TwitchEmotes-style animator
  re-`SetText`s each line ~30fps and rewrites the `|T` texcoords to crop the
  current film-strip frame. That is addon Lua. RLE-compressed TGAs do not
  decode in 1.12 (uncompressed only) — convert with
  `magick in.tga -compress none in.tga` (no `-orient`/`-flip`: the region
  renderer's texcoord order shows the frame right-side-up).
- **Still icon-blind** (accepted): the substring measure `FUN_00772AE0`; the
  hyperlink hit-test past a tall icon OUTSIDE the link on the same line; and a
  ≤1px trailing-icon residual in `GetStringWidth` (the measure loop ends on the
  last glyph's ink width, not its advance).

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

## Reality check on scope — how it actually landed

The plan above feared reimplementing 1.12's glyph emitter `FUN_005ccbe0` (a
long, intricate function) and the measure loop. It did not come to that. The
shipped design avoids both:

- **Positioning** does NOT reimplement the emitter — it SEGMENTS the line at
  icon boundaries and delegates each plain run to the ORIGINAL emitter, so the
  engine still lays out every glyph. We only track where the icons go.
- **Rendering** does NOT stand up a separate quad batch — icons are
  engine-managed regions anchored to their FontString (the removed quad batch
  was strategy B; regions replaced it).
- **Measure** does NOT reimplement the loops — three cold co-hooks on the
  fs-level width / height / wrap functions add the icons' contribution and call
  the originals.

The result is the largest feature in the project, but built from co-hooks that
delegate to the engine rather than from reimplemented layout code. It was
verified by in-game iteration over many build/test rounds, as expected.
