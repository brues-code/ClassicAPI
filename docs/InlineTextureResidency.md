# Why inline icons render as engine regions

Inline `|T…|t` icons render as pooled `CSimpleTexture` regions
([src/text/InlineTexturePool.cpp](../src/text/InlineTexturePool.cpp)) — one per
visible icon, anchored to the icon's owning FontString. The engine draws each
region every frame like any UI texture, so the icon is resident by construction
and moves with its line for free. This is 4.3.4's `CSimpleEmbeddedTexture`
model, built from the parts 1.12 ships.

This file records why that design was chosen and the RE behind it. A raw-GxU-
quad renderer came first and was removed (`56c2670`).

## History — a residency misdiagnosis, corrected

The quad renderer flickered: a random subset of chat icons blanked on scroll,
and a second client showed no icons at all. The first diagnosis was **texture
residency** — the theory that a raw quad bound a texture the VRAM manager had
evicted, because a raw quad is owned by no frame object.

That diagnosis was wrong. The real cause was the Large-Address-Aware pointer
bound (`dc61f77`): a 2GB cap in the flush's `LooksReadable` check rejected valid
heap pointers above `0x80000000`, and the flush's node walk BREAKS on a rejected
node. So one high node truncated the walk, and every node after it lost its
icon. That is the "random subset blanks" pattern, and a second client shows none
because it grows the heap past 2GB sooner. With the bound fixed, the raw quads
did not flicker either.

So engine regions are kept for **4.3.4 parity**, not necessity. They are
resident by construction, which closes the residency question for good — but the
question was never the real bug. Do not re-open the residency investigation; the
D3D notes at the end are the record of what was ruled out.

## The region the pool builds

1.12 ships `CSimpleTexture` — the ordinary `Texture` widget
(`frame:CreateTexture()`). It is a managed, reference-counted, `CGxTexCache`-
backed, engine-drawn texture. The only thing 3.3.5's `CSimpleEmbeddedTexture`
adds is automatic inline layout inside the FontString, and the emitter already
computes the icon positions — so the pool supplies them. (`CSimpleEmbeddedTexture`
is compiled into 3.3.5 only; RTTI confirms it is absent from 1.12.)

Per icon, the pool takes a pooled region and sets the texture, texcoords,
colour, and two anchor points relative to the owning FontString's bottom-left.
The engine then owns load, residency, managed-pool reload, clipping, and draw.

### 1.12 entry points (used by the pool)

RTTI name strings: `.?AVCSimpleTexture@@` @ `0x00846588`,
`.?AVCSimpleFontString@@` @ `0x00846544`.

Create a `CSimpleTexture` from C (mirrors `Tooltip::LinePool`'s FontString path):

```
void* mem = FUN_00760450(&DAT_00cf4ce0, 0, /*rtti*/0x00846588, 0xFFFFFFFE); // pool alloc
void* tex = FUN_0076fc40(mem, parentFrame, /*drawLayer*/2, /*sublayer*/1);   // ctor
```

Set the texture path (loads through the engine's own loader, owns the handle):

```
FUN_00770200(tex, path, 0, DAT_00878cf0, 0);  // CSimpleTexture::SetTexture(path)
```

`FUN_00770200` builds GxTexFlags (`FUN_0058a980`), loads via `FUN_00449d90`,
stores the HTEXTURE at `tex+0xCC` (owned ref), releases the old one
(`FUN_0041aed0`), and marks the region dirty. This ownership is the whole
residency story — the engine holds the texture and reloads it on eviction.

Region methods the pool calls directly:

| method | C++ target |
|---|---|
| SetTexCoord | `FUN_00770410(tex, &{v0,u0,v1,u1})` — Y-FIRST interleaved |
| SetColor | `FUN_0077F750(tex, &BGRA)` — also the chat-fade alpha mirror |
| SetPoint | `FUN_00767C70` — internal units (see LinePool `PixelToInternal`) |
| Show / Hide | `FUN_0077FCB0` / `FUN_0077FC60` (desired-shown `+0xC4`, realized `+0xC8`) |

`CSimpleTexture` layout (from ctor `FUN_0076fc40`): vtable `0x0081c718`;
region-render vtable at `+0x24`; HTEXTURE `+0xCC`; blend `+0xD0`; per-corner
quad position/texcoord arrays `+0xD4..+0x100` (`OFF_SIMPLETEXTURE_CORNERS`);
`+0x128` a flag. The region-render vtable slot 0 (`0x00770670`) computes the
screen rect and calls `FUN_007705b0` to STORE the quad corners; the GPU draw is
deferred to the engine's batched region paint.

## The pen↔anchor bridge

The emitter computes icon positions in PEN units; the pool stores region
`SetPoint` offsets in ANCHOR units, relative to the FontString's rect. The
bridge is one scale `K` (pen units per anchor unit) plus the FontString's own
rect: the flush reads the FontString rect and derives `K` per node, converts
each icon's screen rect to FontString-relative anchor offsets, and queues that.
The pool applies the queued placement on the next frame tick — never mid-render.
`K` derivation and the FontString-relative store are in `FlushLayout`
([src/text/InlineTexture.cpp](../src/text/InlineTexture.cpp)); the
convergence pass in `Maintain`
([src/text/InlineTexturePool.cpp](../src/text/InlineTexturePool.cpp)) self-heals
any residual scale from the region's own frame-scale chain.

## What was ruled out during the residency chase (do not re-investigate)

The blank icons had zero CPU-side signature — records present, node chain
contiguous, geometry sane, index buffer ready at submit, batched draw, z-buffer
excluded twice. The full D3D9 render state at the true draw point was benign:
`zFunc=8` always, `aBlend=1`, `colorW=0xF`, scissor and stencil off, texture
stage MODULATE with a valid bound texture every draw. All of this was consistent
with the LAA-bound truncation (the node never reached the draw), not with a lost
texture surface.

D3D9 device access, if it is ever needed again:

- `IDirect3DDevice9* = *(void**)(*(void**)0x00C0ED38 + 0x38a8)` (CGxDeviceD3d).
- `GetRenderState` = device vtable `+0xE8`; `SetRenderState` `+0xE4`.
- The GxU re-applies its cached render states before every draw (`FUN_005a1b20`),
  so a raw `SetRenderState` for a GxU-managed state is overwritten; `ZENABLE` is
  not in that path, so a direct set survives.
