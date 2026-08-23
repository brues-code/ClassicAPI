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

enum Offsets {
    FUN_FRAME_SCRIPT_INITIALIZE = 0x7039E0,
    FUN_INVALID_FUNCTION_PTR_CHECK = 0x42A320,
    FUN_LOAD_SCRIPT_FUNCTIONS = 0x490250,

    // Fatal-error dispatcher. `__fastcall(uint code)`. Writes `code`
    // to `DAT_00882738` and chains into the process-teardown path;
    // `FUN_00403BC0` reads the code at exit and shows a localized
    // popup keyed off it. Code `10` = "interface files are corrupt"
    // — the engine fires this from FrameXML's `FUN_0048FBF0` hash
    // mismatch and per-addon `FUN_0051F240` hash mismatch (plus
    // other call sites we haven't pinned down by byte pattern).
    // Hooked by `Security::FrameXMLBypass` to suppress code 10 so
    // our in-memory file modifications (Bindings.xml splice +
    // embedded `!!!ClassicAPI` addon) don't trigger termination.
    FUN_FATAL_ERROR = 0x00401560,

    // Master glue Lua init — clean linear caller of all 5 glue batch
    // trampolines (60 main + 11 char-select + 24 char-create + 10 realm
    // + 4 frame globals = 109 total). Body: 0x0046ABB0..0x0046ABD2,
    // 35 bytes, single caller (FUN_0046A7B0). Runs once per glue boot:
    // initial launch and every world→glue return (log out). Post-hook
    // is the glue analog of `FUN_LOAD_SCRIPT_FUNCTIONS` — by the time
    // it returns, `VAR_LUA_STATE` points at the freshly populated glue
    // state, so `FrameScript_RegisterFunction` writes land on it.
    FUN_LOAD_GLUE_SCRIPT_FUNCTIONS = 0x0046ABB0,

    // Binding manager internals used by the direct-action/override binding backport.
    //
    // Stock Lua `SetBinding` wrapper (`int __fastcall(lua_State *)`).
    // The SetBindingSpell/Item/Macro/Click wrappers delegate to it after
    // constructing their action command, preserving punctuation-key
    // normalization, current-set selection, the Boolean return value, and the
    // native UPDATE_BINDINGS notification.
    FUN_SCRIPT_SET_BINDING = 0x004B8000,

    // Hardware-key dispatch, before the normal binding table is resolved:
    //   int __thiscall(manager, key, isDown)
    // Hooking here lets owner-scoped override bindings remain a separate
    // layer rather than temporarily mutating the character/account binding
    // table. The normal dispatcher is called unchanged when no override wins.
    FUN_BINDING_KEY_DISPATCH = 0x004B7990,

    // Executes a resolved binding command:
    //   int __thiscall(manager, command, isDown)
    // Vanilla has no SPELL/ITEM/MACRO/CLICK direct-action command handlers. The
    // hook recognizes those four families and delegates every native command
    // to the client.
    FUN_BINDING_COMMAND_EXECUTE = 0x004B7B50,

    // Binding-manager `this`-relative flag, set to 1 while a resolved binding
    // command runs. FUN_BINDING_KEY_DISPATCH writes 1 immediately before its
    // call to FUN_BINDING_COMMAND_EXECUTE and 0 immediately after. Override
    // commands dispatched outside that engine path must mirror it so native
    // command handlers observe the same manager state a real keypress produces.
    // Verified in the FUN_BINDING_KEY_DISPATCH disassembly.
    OFF_BINDING_MANAGER_EXECUTING = 0xD8,

    // Frame-script execution context and its nesting depth (the `DAT_00ceeac0`
    // dance referenced below). Shared by the binding dispatcher and the
    // tooltip/frame script invokers. FUN_BINDING_KEY_DISPATCH saves the context
    // pointer, zeroes it for the nested command, restores it afterward, and
    // floors the depth back to 0. `Bindings::Api` mirrors this when it runs an
    // override command outside the engine's own dispatch. Verified in that
    // disassembly.
    VAR_FRAMESCRIPT_EXEC_CONTEXT = 0x00CEEAC0,
    VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH = 0x00CEEAC4,

    // GameTooltip script-method prologue helpers (used to resolve self → CFrameScriptObject*).
    // Underlying Lua C API names per [[docs/LuaCAPI.md]] are `lua_rawgeti`
    // (0x6F3BC0) and `lua_touserdata` (0x6F3740). The Set* method prologue's
    // call sequence
    //   PushObject(L, idx, 0);  // == lua_rawgeti(L, idx, 0) — pushes the
    //                             // lightuserdata at table[0]
    //   GetObject(L, -1);       // == lua_touserdata(L, -1) — extracts
    //                             // the raw CFrameScriptObject *
    // is the canonical "Lua frame table → C++ object" path. The reverse
    // (`GameTooltip:GetOwner` etc.) is just `lua_rawgeti(L, REGISTRY,
    // [cobj+OFF_COBJECT_LUA_REGISTRY_REF])` — the engine pre-allocates
    // the registry handle when the frame is created from Lua.
    FUN_FRAMESCRIPT_PUSH_OBJECT = 0x6F3BC0,
    FUN_FRAMESCRIPT_GET_OBJECT = 0x6F3740,

    // CFrameScriptObject layout: the registry refkey allocated when the
    // engine first exposes the C++ object to Lua. Pushing a frame back
    // to Lua is `lua_rawgeti(L, LUA_REGISTRYINDEX, [cobj+0x08])`.
    OFF_COBJECT_LUA_REGISTRY_REF = 0x08,

    // Within a CGFrame, the embedded LayoutFrame sub-object starts here.
    // Engine code that wants to invoke LayoutFrame methods polymorphically
    // (anchoring, positioning) stores the LayoutFrame* — i.e.
    // `(Frame*)obj + 0x24` — instead of the bare Frame*. Reverse
    // lookups (e.g. GameTooltip:GetOwner) subtract this back out.
    OFF_FRAME_LAYOUT_SUBOBJECT = 0x24,

    // Inner spell-tooltip builder, called from Script_GameTooltip_SetSpell at 0x00532E92.
    // __thiscall(spellID, 0, 0, isPet, 0, 0, 0); we always pass isPet=0.
    FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP = 0x0052E610,

    // Existing GameTooltip method-table entries we dispatch to from
    // backported convenience methods. Each is `int __fastcall(void *L)`
    // expecting the standard self+args layout on the Lua stack.
    // (Slot numbers are the method-registry index per `docs/raw_methods.txt`.)
    FUN_SCRIPT_GAMETOOLTIP_SET_HYPERLINK = 0x00531FD0, // slot 12
    FUN_SCRIPT_GAMETOOLTIP_SET_INVENTORY_ITEM = 0x00532EE0, // slot 19
    FUN_SCRIPT_GAMETOOLTIP_SET_UNIT_BUFF = 0x00534AC0, // slot 32
    FUN_SCRIPT_GAMETOOLTIP_SET_UNIT_DEBUFF = 0x00534E30, // slot 33
    FUN_SCRIPT_GAMETOOLTIP_SET_TALENT = 0x00535170, // slot 34

    // Iterator that registers an array of frame-method bindings on a per-frame-type
    // method registry (e.g. VAR_GAMETOOLTIP_METHOD_REGISTRY for GameTooltip).
    // __fastcall(ecx = MethodEntry table, edx = count, [stack] = context).
    FUN_REGISTER_FRAME_METHODS = 0x00701D80,
    VAR_GAMETOOLTIP_METHOD_REGISTRY = 0x00C0CF20,

    // CGameTooltip line pool (see TODO #97). The engine caps AddLine
    // (FUN_00530270) at `numLinesAllocated`, which the XML-OnLoad scan
    // (FUN_00529650, vtable slot 9) sets to the count of contiguous
    // `<name>TextLeftN`/`TextRightN` FontStrings the template declares
    // (30). Tooltip::LinePool grows it: it appends its own C++-created
    // FontStrings to the three parallel arrays and bumps this count. Each
    // array is a {count@+0x0, cap@+0x4, data@+0x8} descriptor; the
    // reallocators realloc `data` to newCount*4 and copy `cap` old elements.
    VAR_GAMETOOLTIP_VTABLE = 0x00808F60,          // type guard for a resolved tooltip object
    OFF_GAMETOOLTIP_NUM_LINES_ALLOC = 0x320,      // int — pool size AddLine gates on
    OFF_GAMETOOLTIP_TEXTLEFT_DESC = 0x324,        // {count,cap,data}; data (+0x8) = CSimpleFontString*[]
    OFF_GAMETOOLTIP_TEXTRIGHT_DESC = 0x330,       // symmetric right column
    OFF_GAMETOOLTIP_WRAPFLAG_DESC = 0x33C,        // per-line wrap int[]
    FUN_TOOLTIP_REALLOC_PTR_ARRAY = 0x00536C80,   // __thiscall(desc, newCount) — pointer arrays
    FUN_TOOLTIP_REALLOC_INT_ARRAY = 0x004368C0,   // __thiscall(desc, newCount) — wrap-flag array

    // GameTooltip:SetHyperlinkCompareItem (Tooltip::Compare) — builds the
    // equipped item's tooltip natively (no Lua roundtrip) and appends
    // colored stat deltas. FUN_GAMETOOLTIP_BUILD_ITEM is the engine's own
    // item-tooltip builder that SetInventoryItem/SetHyperlink call:
    //   __thiscall(self, itemID, guid*, guid*, a4, a5, a6, headerFlag, a8, a9)
    // Proven arg shape from FUN_005353b0/FUN_00535700 (all flags 0 → a clean
    // item tooltip). The builder's own headerFlag (param_7) path merges the
    // "Currently Equipped" text into the name line and force-wraps it, so we
    // build clean (flag 0) and prepend the grey header ourselves by shifting
    // the line FontStrings — see FUN_FONTSTRING_SET_* / OFF_FONTSTRING_*.
    // FUN_GAMETOOLTIP_ADD_LINE is the raw add-line body (see LinePool notes);
    // its color args are pointers to a 4-byte packed color {b,g,r,a} — the
    // Lua AddLine handler (FUN_00531630) builds it as 0xFF<rr><gg><bb>, i.e.
    // uint32 `0xFF000000 | r<<16 | g<<8 | b` in little-endian memory.
    FUN_GAMETOOLTIP_BUILD_ITEM = 0x0052B650,
    // OnTooltipSetItem support. FUN_GAMETOOLTIP_SCRIPT_RESOLVER is the
    // CGGameTooltip vtable method (vtable 0x00808F60) that maps a script name
    // to its handler slot; __thiscall(self, const char *name) -> int* slot,
    // 0 if the name isn't a tooltip script. It first delegates to the base
    // frame resolver, then checks OnTooltipSetDefaultAnchor(+0x444) /
    // OnTooltipCleared(+0x44c) / OnTooltipAddMoney(+0x454) — each an 8-byte
    // {handler, context} slot. Vanilla has no OnTooltipSetItem, and the object
    // (alloc size 0x460) has no free 8-byte slot, so we co-hook this to hand
    // out a C-side per-tooltip cell for that name. FUN_FRAME_INVOKE_SCRIPT is
    // the engine's real script invoker __fastcall(handler /*ecx = slot[0]*/,
    // frame /*edx*/): it binds the global `this` = frame and runs the handler
    // under its own protected lua_pcall (0 args). We call it directly from the
    // item-builder co-hook to fire OnTooltipSetItem.
    //   NOTE: the clear fires OnTooltipCleared via the wrapper FUN_00702690
    //   (self, &slot), which additionally stamps the global exec-context
    //   (DAT_00ceeac0) from slot[1]. That context mutation is only valid from
    //   the engine's top-level frame-script dispatch — firing it from deep in a
    //   nested Lua->C->Lua stack (e.g. an addon's SetAuctionItem wrapper)
    //   corrupts the outer script's context and faults. So we skip the wrapper
    //   and call the inner invoker, which is self-contained.
    FUN_GAMETOOLTIP_SCRIPT_RESOLVER = 0x005295D0,
    FUN_FRAME_INVOKE_SCRIPT = 0x00704D50,
    // The arg-passing sibling of FUN_FRAME_INVOKE_SCRIPT — every frame
    // script that carries values (OnClick(button), OnMouseWheel(delta),
    // OnUpdate(elapsed), OnValueChanged(value), OnKeyDown(key), OnEvent's
    // args, …) funnels through here. __cdecl(int handlerRef, void *frame,
    // const char *fmt, void *vaPtr): fmt is a printf-style spec string
    // (`%d`/`%u`/`%f`/`%s`), vaPtr points at the packed vararg buffer
    // (4-byte stride for d/u/s, 8 for f). It sets the `arg1..argN` globals
    // from the format (saving/restoring the previous values), sets the
    // `this` global to `frame`, then runs the handler under a protected
    // lua_pcall with **zero Lua args** and the engine error handler
    // (VAR_FRAMESCRIPT_ERROR_HANDLER_REF) as errfunc. FUN_FRAME_INVOKE_SCRIPT
    // is the same shape for the no-arg scripts (OnShow/OnHide/OnEnter/…).
    // `Frame::ScriptArgs` co-hooks both to additionally pass the handler
    // modern positional args (self, arg1..argN). The frame's own Lua object
    // ref lives at frame+OFF_COBJECT_LUA_REF (refcount at
    // OFF_COBJECT_LUA_REFCOUNT); FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER
    // lazily creates it when the refcount is 0.
    FUN_FRAME_RUN_SCRIPT_ARGS = 0x00704F10,
    // The base ScriptObject's OnEvent handler slot — an 8-byte
    // {handler, context} pair at frame+0x0C (from FUN_00702590, the base
    // resolver every frame-type resolver chains through, and confirmed by
    // the event dispatchers FUN_00703E50 / FUN_00703F50 passing `frame+0xC`
    // as the slot). `*(int*)(frame + OFF_FRAME_ONEVENT_SLOT)` is the OnEvent
    // handler ref; `Frame::ScriptArgs` compares it against the handler ref
    // the runner is invoking to detect an OnEvent dispatch (exact — each
    // SetScript makes a distinct ref, so a function bound to two scripts
    // still differs per slot) and prepend the `event` positional.
    OFF_FRAME_ONEVENT_SLOT = 0x0C,
    // Registry ref (an int, not a pointer) to the frame-script message
    // handler the engine passes as the `errfunc` to every script pcall —
    // read as `*(int*)VAR_FRAMESCRIPT_ERROR_HANDLER_REF`, pushed via
    // lua_rawgeti(REGISTRY, ref). Set once at engine init.
    VAR_FRAMESCRIPT_ERROR_HANDLER_REF = 0x008722C8,
    // The base Frame script-name resolver — __thiscall(frame, const char *name)
    // -> int* slot, 0 for an unknown name. Maps the standard base-frame scripts
    // to their 8-byte {handler, context} slots on the frame (OnLoad@+0x118,
    // OnUpdate@+0x128, OnEnter@+0x140, … OnKeyUp@+0x190) after delegating to the
    // ScriptObject base FUN_00702590 (OnEvent@+0xC). Every frame TYPE's resolver
    // chains through this (the tooltip resolver above calls it first), so
    // co-hooking it here lets `Frame::Attributes` make `OnAttributeChanged`
    // SetScript/GetScript/HookScript-able on ALL frames — for that one name the
    // co-hook hands back an external per-frame cell (frames are immortal in 1.12,
    // so a pointer-keyed cell never goes stale). Same pattern + ABI modeling as
    // FUN_GAMETOOLTIP_SCRIPT_RESOLVER.
    FUN_FRAME_SCRIPT_RESOLVER = 0x0076A0D0,
    // The other per-object tooltip builders, co-hooked the same way as
    // FUN_GAMETOOLTIP_BUILD_ITEM to back OnTooltipSetSpell / OnTooltipSetUnit /
    // OnTooltipSetGameObject (see Tooltip::SetEvents). Each is the single funnel
    // its Set*/mouseover paths converge on and clears the tooltip
    // (FUN_00530050) before repopulating, so firing after it covers every way
    // that object type gets set:
    //   - Spell FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP (0x0052E610, 7 stack args,
    //     RET 0x1c) — SetSpell/SetSpellByID/SetTalent/SetShapeshift funnel.
    //   - Unit  (0x00529fe0, __thiscall(self, guid*), RET 4) — SetUnit +
    //     the two engine mouseover paths (FUN_004919d0 / FUN_00492890).
    //     Returns an int the caller (Script_GameTooltip_SetUnit) tests, so the
    //     co-hook must forward the original's return value.
    //   - GameObject (0x0052aa20, __thiscall(self, guid*), RET 4) — the GO
    //     hover populator; resolves the GO (TYPEMASK_GAMEOBJECT) and writes the
    //     GO field at +0x370.
    FUN_GAMETOOLTIP_BUILD_UNIT = 0x00529FE0,
    FUN_GAMETOOLTIP_BUILD_GAMEOBJECT = 0x0052AA20,
    // Pointer to the GameTooltip CFrameScriptObject the engine's mouseover
    // setter builds into (`DAT_00b4b3c4`; the value at this address is the
    // tooltip object). `Frame::Attributes` reads it to build the unit tooltip
    // for offline/out-of-range party & raid members, which FUN_00492890 skips.
    VAR_GAMETOOLTIP_OBJECT_PTR = 0x00B4B3C4,
    // The tooltip's OnTooltipSetDefaultAnchor handler slot (the `{handler,
    // context}` pair the script resolver maps that name to). The engine
    // mouseover setter fires this before building to (re)establish the
    // tooltip owner/anchor via FrameXML's GameTooltip_SetDefaultAnchor —
    // without it a rebuild after the tooltip was hidden has no owner and
    // stays invisible. `Frame::Attributes` fires it (via the self-contained
    // FUN_FRAME_INVOKE_SCRIPT) before the offline roster build.
    OFF_TOOLTIP_SET_DEFAULT_ANCHOR_HANDLER = 0x444,
    FUN_GAMETOOLTIP_ADD_LINE = 0x00530270,        // __thiscall(self, left, right, lColorBGRA*, rColorBGRA*, wrap)
    OFF_GAMETOOLTIP_NUM_LINES = 0x31C,            // int — live line count (AddLine index; +0x320 is the cap)
    // The displayed item's identity is read via the existing
    // OFF_TOOLTIP_ITEM_ID (+0x398) / OFF_TOOLTIP_ITEM_GUID_LO (+0x380) —
    // link paths set the itemID, CGItem paths (SetInventoryItem/…) set only
    // the GUID (see Tooltip::Compare::TooltipItemID and item/Tooltip.cpp).

    // Line-shift primitives for prepending the grey "Currently Equipped"
    // header. The per-line CSimpleFontString objects live in the parallel
    // arrays at OFF_GAMETOOLTIP_TEXTLEFT/RIGHT_DESC (+8 = data). Each stores
    // its text buffer pointer at +0xF0 and its 4-byte {b,g,r,a} color behind
    // the pointer at +0xB8 (both verified from the setters below). We read a
    // line's text/color and re-apply them one slot down, then set line 0.
    FUN_FONTSTRING_SET_TEXT = 0x00771D80,         // __thiscall(fs, text, flag=0)
    FUN_FONTSTRING_SET_COLOR = 0x0077F750,        // __thiscall(fs, colorBGRA*)
    OFF_FONTSTRING_TEXT = 0xF0,                   // char* — current text buffer
    OFF_FONTSTRING_COLOR_PTR = 0xB8,              // ptr → 4-byte {b,g,r,a} color storage
    // A line's FontString is only positioned once shown: set the desired
    // flag at +0xC4 then call SHOW (FUN_0077fcb0) / HIDE (FUN_0077fc60) —
    // the exact pair AddLine (FUN_00530270) and the clear use. Moving text
    // into a previously-empty (hidden) cell without this leaves it
    // unpositioned, so the line-shift must replicate it.
    FUN_FONTSTRING_SHOW = 0x0077FCB0,             // __fastcall(fs) — realizes +0xC4
    FUN_FONTSTRING_HIDE = 0x0077FC60,             // __fastcall(fs)
    OFF_FONTSTRING_SHOWN_FLAG = 0xC4,             // int — desired-shown flag
    // The per-tooltip clear only HIDES line FontStrings (sets +0xC8 = 0)
    // and clears the left text; right-column text buffers are left intact.
    // So a shift must decide a cell's content by its actually-shown flag
    // (+0xC8), not by whether +0xF0 still holds (stale) text.
    OFF_FONTSTRING_VISIBLE = 0xC8,                // int — actually-shown flag

    // FontString creation primitives used by Tooltip::LinePool to build the
    // extra lines entirely in C++ (no addon Lua). Calling conventions and
    // offsets mirrored from Script_CreateFontString (0x00773C30),
    // Script_SetFontObject (0x0079D1A0 → 0x00770C60), and Script_SetPoint
    // (0x007A2540 → 0x00767C70) — see TODO #97's investigation update.
    FUN_GAMETOOLTIP_SETUP_LINES = 0x00529650,     // vtable slot 9 — the pool scan we co-hook
    VAR_SIMPLEFONTSTRING_POOL = 0x00CF4D10,       // CSimpleFontString free-list pool (allocator `this`)
    VAR_SIMPLEFONTSTRING_CLASS_TAG = 0x00846544,  // ".?AVCSimpleFontString@@" (alloc debug tag)
    FUN_REGION_POOL_ALLOC = 0x00760450,           // __thiscall(pool, zeroInit=0, tag, line=-2) -> raw mem
    FUN_SIMPLEFONTSTRING_CTOR = 0x00770D30,       // __thiscall(mem, parent, layer, sublayer) -> fs
    FUN_REGION_SET_NAME = 0x0076C650,             // __thiscall(region, name) — registers _G[name]
    FUN_FONTSTRING_SET_FONT = 0x00770C60,         // __thiscall(fs + OFF_FONTSTRING_FONT_HOLDER, fontObject)
    FUN_REGION_SET_POINT = 0x00767C70,            // __thiscall(region+OFF_REGION_ANCHOR, point, relAnchor, relPoint, x, y, flag=1)
    OFF_FONTSTRING_FONT_HOLDER = 0xCC,            // font-reference sub-object (SetFont `this`)
    OFF_FONTSTRING_FONT_OBJECT = 0xD0,            // font object pointer (holder + 4)
    OFF_REGION_ANCHOR = 0x24,                     // LayoutFrame anchor sub-object (SetPoint `this` / relativeTo base)
    DRAWLAYER_ARTWORK = 2,
    FRAMEPOINT_TOPLEFT = 0,
    FRAMEPOINT_TOPRIGHT = 2,
    FRAMEPOINT_LEFT = 3,
    FRAMEPOINT_RIGHT = 5,
    FRAMEPOINT_BOTTOMLEFT = 6,
    FRAMEPOINT_BOTTOMRIGHT = 8,

    // FUN_REGION_SET_POINT stores offsets in *internal* coordinates, not
    // pixels. Script_SetPoint converts: `internal = pixel * [0x00832A44] /
    // ([0x00832A4C] * 1024)` (FUN_0041ae60's factor × input ÷
    // (FUN_0041ad70's return × DAT_007ffd68)). Both globals are runtime
    // UI-scale floats; passing raw pixels makes offsets ~1000× too large.
    // The REVERSE conversion (internal → UI pixels) is what the Script_*
    // measure getters push: Script_GetStringWidth (0x0079E510) — and our
    // GetStringHeight backport — compute `px = internal * [0x00832A4C] *
    // 1024 / [0x00832A44]` (FUN_0041AE40(FUN_0041AD70() × DAT_007FFD68 × v)).
    VAR_UI_COORD_SCALE_MUL = 0x00832A44,   // float numerator
    VAR_UI_COORD_SCALE_DIV = 0x00832A4C,   // float denominator base
    UI_COORD_SCALE_UNIT = 1024,            // DAT_007ffd68
    // Semantically, [0x832A44] is the SCREEN WIDTH IN ANCHOR UNITS (the
    // full-screen x extent of the internal layout space) — that's why it's
    // the px→internal numerator above — and [0x832A48] is its Y sibling
    // (screen HEIGHT in anchor units). Verified via the gxu text position
    // converter FUN_0041ade0: block/node positions are stored NORMALIZED,
    // x = anchor/[0x832A44], y = anchor/[0x832A48] (the y global was
    // previously unmapped — the x/y pair sits at +0x00/+0x04, the SetPoint
    // denominator base at +0x08).
    VAR_UI_ANCHOR_SCREEN_H = 0x00832A48,
    // gxu text-raster dimensions — INT dwords holding the live render-target
    // size in PIXELS ({0,0,640,480} fallback), written by FUN_005c2b50 from
    // the render-target rect (FUN_0058a240) whenever it changes; every font
    // page re-rasterizes on change (the FUN_005ca6f0 walk). These are the
    // normalized→pen multipliers: the origin finalize FUN_005cdf70 computes
    // node origin (+0x70/+0x74) = round(normalizedPos × [raster]) after
    // folding the justify shift (right: +widthLimit, centre: +half) and
    // vertical align; the emitter's font-height helper FUN_005C6FA0 is
    // round(fontSize × [VAR_TEXT_RASTER_Y]) the same way (x sibling
    // FUN_005C7010 uses _X). So TEXT PEN UNITS ARE RENDER-TARGET PIXELS, and
    // pen-per-anchor = [VAR_TEXT_RASTER_*] / [VAR_UI_ANCHOR_SCREEN_*] per
    // axis — the exact conversion Text::InlineTexture's flush uses to place
    // icon regions (verified against a live probe: rasterX/anchorW matched
    // the measured origin÷rect-edge quotient to four digits).
    VAR_TEXT_RASTER_Y = 0x00C2B9A0,
    VAR_TEXT_RASTER_X = 0x00C2B9A4,
    // gxu text-node invalidate/reset — `__fastcall(node)`. Zeroes the
    // built-line count (+0x9C, the draw builder's early-out gate), resets the
    // link-rect state (+0x80/+0x90) and wrap cache (+0x68/+0x6C), and frees
    // all 8 page buffers — the next paint's pre-pass re-runs the builder, so
    // the emitter re-bakes the node from scratch. This is what the node
    // colour setter FUN_005CCB40 calls on a colour change for accumulation
    // (bit-3-clear) nodes; InlineTexture's flush calls it for a SEGMENTED
    // bit-3 node whose BASE colour RGB changed (glue AddonList toggle), since
    // baked per-glyph RGB can't be patched in place (|c runs own theirs).
    FUN_TEXT_NODE_INVALIDATE = 0x005CDEF0,
    // gxu font-face flags word. Bit 0 (0x1) = thin outline, bit 3 (0x8) =
    // thick outline: the emitter's prologue (FUN_005CCBE0) reads
    // *(fontFace+0x180) and grows the line height by [FLOAT_OUTLINE_EXTRA_*]
    // pen px for outlined faces, because outline INK extends past the glyph
    // metrics; the rebuild/color-sync shadow paths test the same bits. The
    // inline-texture lead/trail pads read the same flags so an icon clears an
    // outlined neighbour's ink (the coin-into-digits clip).
    OFF_FONTFACE_FLAGS = 0x180,
    // The engine's outline ink allowance, .rdata float constants (4.0 / 2.0
    // pen px total, i.e. ~2 / ~1 px per side) — the exact values the emitter
    // adds to line height for bit 3 / bit 0 outlined faces. Read live so we
    // stay bit-identical with the engine's own compensation. The 2.0 constant
    // double-duties as the MINIMUM font size numerator in the node ctor
    // (FUN_005cd6d0 / FUN_005ccb80: fontSize floored at [0x801628]/rasterY =
    // 2 pen px).
    FLOAT_OUTLINE_EXTRA_THICK = 0x0080306C,
    FLOAT_OUTLINE_EXTRA_THIN = 0x00801628,
    // The node's BUILT line count — incremented once per emitted wrapped line
    // by the draw builder (FUN_005CDC20, including the \n break case) and the
    // builder's own early-out gate (`if (node+0x9C != 0) return`); zeroed by
    // the node invalidate (FUN_TEXT_NODE_INVALIDATE). The render truth for
    // FontString:GetNumLines once the node has painted.
    OFF_TEXT_NODE_BUILT_LINES = 0x9C,
    // Wrap break-array computer: `__thiscall(fs, const char *text, float
    // wrapWidth /*fs-internal units*/, int *outBreaks, int cap)` → segment
    // count (byte offsets into text, [0]=0). Routes through FUN_005C2430 →
    // the wrap stepper, so it is icon-aware via the stepper co-hook. Verified
    // from the GameTooltip auto-size (FUN_00530640), which fills a 20-entry
    // array and measures each segment via FUN_FONTSTRING_MEASURE_SUBSTRING.
    FUN_FONTSTRING_BREAK_ARRAY = 0x00772B60,
    // Substring width measure: `__thiscall(fs, const char *text, int len)` →
    // ST0 (len 0 = strlen). Same measure-core call + `out / fs+0x7C` shape as
    // GetStringWidthInternal, but for an ARBITRARY string in the fs's font —
    // no cache. Callers (xrefs): the GameTooltip auto-size FUN_00530640, which
    // measures each WRAPPED SEGMENT of a wrap-enabled line between the
    // FUN_00772B60 break positions and takes the max as the tooltip width —
    // the icon-relevant consumer (an earlier note claimed nothing consumed
    // this function; the xref list refutes it) — plus the editbox
    // caret/selection cluster (FUN_0077DA80, FUN_0077DE70, FUN_0077D0D0),
    // which must stay raw and is excluded by the editable/focused-buffer
    // gates in the co-hook.
    FUN_FONTSTRING_MEASURE_SUBSTRING = 0x00772AE0,
    // FontString → gxu face resolution, for fs-level (measure-hook) callers
    // that need the face the RENDER will use. The rebuild (FUN_007724a0)
    // passes [fs+0xE0] (the font HANDLE) to the block creator FUN_0044d420,
    // which hands [handle+0x20] to FUN_005c1c30 → FUN_005cd6d0, which stores
    // it at node+0x44 — the emitter's face (`this` for every glyph call, and
    // the +0x180 outline-flags carrier). So face = [[fs+0xE0]+0x20].
    OFF_FONTSTRING_FONT_HANDLE = 0xE0,
    OFF_FONT_HANDLE_FACE = 0x20,

    // CSimpleTexture (`Texture` widget) creation + operate primitives, used by
    // Text::InlineTexturePool to render inline |T icons as engine-owned,
    // managed-pool, resident-kept textures (the residency fix — see
    // docs/InlineTextureResidency.md). Mirrors Script_CreateTexture
    // (FUN_00773A20) and the Texture frame-method handlers; the sibling of the
    // CSimpleFontString path above. Same FUN_REGION_POOL_ALLOC / SetPoint /
    // Show / Hide / SetColor (FUN_FONTSTRING_SET_COLOR) as the FontString pool.
    VAR_SIMPLETEXTURE_POOL = 0x00CF4CE0,      // &DAT_00cf4ce0 CSimpleTexture free-list pool (`this`)
    VAR_SIMPLETEXTURE_CLASS_TAG = 0x00846588, // ".?AVCSimpleTexture@@" (alloc debug tag)
    FUN_SIMPLETEXTURE_CTOR = 0x0076FC40,      // __thiscall(mem, parent, layer, sublayer) -> tex
    // --- MaskTexture object API (frame:CreateMaskTexture / Texture:AddMaskTexture) ---
    // CreateMaskTexture mints its mask region by calling the engine's own
    // Script_CreateTexture (reads (frame,name,layer,template) off the Lua stack and
    // pushes the new CSimpleTexture), then hides it — the mask is a source, never drawn.
    // The draw hook reads the mask's HTEXTURE (OFF_SIMPLETEXTURE_HTEXTURE, +0xCC, set
    // by SetTexture and valid while hidden) + its rect (FUN_REGION_GET_RECT); the
    // +0xC4 shown flag is OFF_REGION_DESIRED_SHOWN (defined below).
    FUN_SCRIPT_CREATE_TEXTURE = 0x00773A20,   // Script frame:CreateTexture, __fastcall(L)->int
    // CSimpleTexture::SetTexture(path). Loads via FUN_00449D90, stores the owned
    // HTEXTURE at +0xCC (releases the old via DecRef FUN_0041AED0), marks dirty.
    // `__thiscall(tex, path, 0, *VAR_TEXTURE_BLEND_DEFAULT, 0) -> u32`. The
    // ownership + engine batched region-draw is the residency win. Same-path
    // early-out makes pooled reuse cheap.
    FUN_SIMPLETEXTURE_SET_TEXTURE = 0x00770200,
    // Script_Texture_SetTexture — the Lua `texture:SetTexture(...)` handler. Its
    // NUMERIC branch (arg 2 a number) clamps r/g/b/a to [0,1] and fills the
    // texture with a solid colour via FUN_00770360; its string branch loads a
    // path through FUN_00770200 above. texture/ColorTexture.cpp aliases this whole
    // handler as the 7.0 `SetColorTexture` (which IS exactly the numeric form), so
    // the clamp and opaque-alpha default come straight from the engine.
    FUN_SCRIPT_TEXTURE_SET_TEXTURE = 0x0079BB40,
    // CSimpleTexture::SetTexCoord — `__thiscall(tex, float[4])`. Struct field
    // order is {top, left, bottom, right} = {v0, u0, v1, u1} (verified from the
    // SetTexCoord handler 0x0079BEB0's Lua-arg → struct mapping). Natural
    // texcoords (no v-flip — the engine region path is top-left origin).
    FUN_SIMPLETEXTURE_SET_TEXCOORD = 0x00770410,
    OFF_SIMPLETEXTURE_HTEXTURE = 0xCC,        // owned HTEXTURE ref (diagnostic)
    // The four drawn-quad corner POSITIONS: 4 vertices of {x, y, z} floats (0xC
    // stride), order BL, TL, BR, TR (from FUN_REGION_STORE_CORNERS's writes). The
    // region draw enqueue FUN_00772fd0 (0x00772fd0) hands region+0xD4 straight to
    // the vertex batch as FOUR INDEPENDENT vertices — and region+0x104 as the
    // matching 4 per-corner texcoords — never re-deriving an axis-aligned rect.
    // So writing ROTATED x,y here draws a rotated quad with NO corner clipping
    // (unlike 4.3.4's SetRotation, which rotates the texcoords instead). Used by
    // texture/Rotation.cpp for Texture:SetRotation.
    OFF_SIMPLETEXTURE_CORNERS = 0xD4,
    // Region corner-store: `__thiscall(region, const float rect[4])` writes the
    // drawn quad corners into region+0xD4..+0x100 from the rect ({yA,left,yB,right}
    // screen px). The renderer only draws a region whose +0xD4 corners are
    // populated (verified: the draw gate tests region+0xD4 != 0), and normally
    // SetPoint→layout-resolve calls this. We call it directly to place an icon by
    // its screen rect with NO anchors (anchoring 100+ textures/frame corrupts the
    // UI-manager pending-layout list — the FUN_00765650 crash). Co-hooked by
    // texture/Rotation.cpp, which re-applies rotation right after the engine
    // restores the axis-aligned corners (layout resolve / SetTexCoord).
    FUN_REGION_STORE_CORNERS = 0x007705B0,
    // Texcoord crop applied to a {top,left,bottom,right} screen rect before the
    // corner-store: shrinks right→left and bottom→top by the region's per-corner
    // texcoord span (ABS of the +0x104 U/V differences). No-op for full 0..1
    // texcoords. `__thiscall(region, float rect[4])`, rect in/out. The crop
    // itself is unconditional; every engine call site gates it on
    // OFF_REGION_TEXCOORD_MODIFIES_RECT below — with the flag clear (the
    // default) a partial SetTexCoord draws at the region's FULL size, so
    // callers replicating an engine store must apply the same gate.
    FUN_REGION_TEXCOORD_CROP = 0x00770570,
    // The SetTexCoordModifiesRect flag (int). Written from the Lua boolean by
    // Script_SetTexCoordModifiesRect (0x0079C080, `piVar4[0x49]`), read back
    // 1/nil by Script_GetTexCoordModifiesRect (0x0079C120). All three
    // FUN_REGION_TEXCOORD_CROP call sites test it: the SetTexCoord writers
    // (FUN_00770410 4-arg, FUN_007704C0 8-arg corner form) crop+store only
    // when set, and the layout-resolve store path (FUN_00770670) crops when
    // set / stores the raw anchor rect (+0x40..+0x4C) when clear. Zero by
    // default (region ctor), so the crop never runs unless an addon opts in.
    OFF_REGION_TEXCOORD_MODIFIES_RECT = 0x124,
    // Get the region's resolved screen rect: `__thiscall(region+OFF_REGION_ANCHOR,
    // float out[4]) -> int` (1 = valid, 0 = not laid out yet). Reads the anchor
    // sub-object's +0x40..+0x4C = {top, left, bottom, right}, gated on the
    // rect-valid bit [+0x3c]&1. This is the engine's own source for the corner
    // rect (see FUN_00770410's SetTexCoord path); texture/Rotation.cpp reads it to
    // rebuild axis-aligned corners before rotating.
    FUN_REGION_GET_RECT = 0x00768320,
    // __thiscall(region+OFF_REGION_ANCHOR) -> int (nonzero = layout dirty, needs a
    // resolve). GetLeft gates its resolve on this. Paired with the flag-1 resolve
    // below to read a hidden region's rect synchronously.
    FUN_REGION_LAYOUT_DIRTY = 0x00768310,

    // __thiscall(region+OFF_REGION_ANCHOR, int flag /*stack; pass 0*/) — force a
    // synchronous layout resolve: recomputes the region's rect (+0x64) from its
    // anchors right now, instead of waiting for the render's pending-layout pass.
    // Verified from Show (FUN_0077FCB0 at 0x0077FCE1: PUSH 0; LEA ECX,[ESI+0x24];
    // CALL). Load-bearing for tick-time icon placement: SetTexCoord
    // (FUN_00770410) stores the DRAW CORNERS (+0xD4) from the rect as a side
    // effect, so without a realize between SetPoint and SetTexCoord the corners
    // are stored from the STALE pre-anchor rect — the region's rect then resolves
    // correctly (diagnostics look perfect) but the renderer draws the corners,
    // which stay zero/stale → invisible icons (and the old build's frozen
    // top-left icons: corners stored from the zero rect at first apply).
    FUN_REGION_LAYOUT_REALIZE = 0x00768060,

    // CSimpleFontString::RebuildString — destroys the old gxu text node and
    // creates the fresh one (FUN_0044d420), storing it at fs+0xF8. Gated on the
    // fs dirty bit (+0x60 & 1), so it fires on TEXT CHANGES, not per frame.
    // Co-hooked by Text::InlineTexture to map text node → owning fontstring —
    // the key that lets inline-icon regions anchor to their owning line
    // (1.12 chat lines ARE CSimpleFontStrings: the ScrollingMessageFrame's
    // display refresh FUN_00788750 SetTexts/anchors/shows one fs per visible
    // line — verified against the 4.3.4 CSimpleEmbeddedTexture model, which
    // anchors its icon regions to the owning fontstring the same way).
    FUN_FONTSTRING_REBUILD_STRING = 0x007724A0,
    // fs+0xF8 holds an HTEXTBLOCK handle, NOT the node itself: FUN_0044d420
    // allocates the 12-byte handle {vtbl, refcount, node}, FUN_005c1c30 writes
    // the created text node into handle+8, and FUN_0041af10 AddRefs and returns
    // the handle. The layout's node (what the emitter/paint hooks see) is
    // therefore *( *(fs+0xF8) + 8 ).
    OFF_FONTSTRING_TEXT_BLOCK = 0xF8, // HTEXTBLOCK handle (0 when dirty/empty)
    // CSimpleFontString live text color: count at +0xB4, array pointer at +0xB8
    // (uint32 BGRA per slot; slot 0 = the base color, alpha = byte 3; a parallel
    // per-glyph alpha-byte array hangs off +0xA8/+0xA4). Written by the fs
    // SetColor (FUN_FONTSTRING_SET_COLOR 0x0077F750: stores the dword at
    // (*(u32**)(fs+0xB8))[0] + the alpha byte at (*(u8**)(fs+0xA8))[0], then
    // fires the color-update vmethod +0x20). Count 0 = never colored = default
    // opaque white. THE CHAT-FADE SIGNAL: the ScrollingMessageFrame's fader
    // (FUN_00788460, gated on the SetFading flag at smf+0x360; per-line state
    // {rgba@+4, shown@+8, timeVisibleLeft@+0xC, fadeLeft@+0x10} in the
    // stride-0x10 line array at smf+0x3A4) animates each visible line
    // fontstring's alpha byte through this same SetColor every frame, then
    // hides the fs at fade end. Text::InlineTexturePool mirrors this byte onto
    // the line's icon regions each tick so inline icons fade with their line
    // (regions parent to the CHAT FRAME, so the frame's own alpha byte —
    // frame+0xC8, per Script_GetAlpha 0x00774DC0 — already modulates them via
    // the engine's parent×region product; only the fs's own component needs
    // mirroring).
    OFF_FONTSTRING_COLOR_COUNT = 0xB4,
    OFF_FONTSTRING_COLOR_ARRAY = 0xB8,
    OFF_TEXTBLOCK_NODE = 0x8,         // the gxu text node inside the handle
    // Byte of fontstring state flags; bit 1 = "text block needs rebuild".
    // RebuildString (0x7724A0) gates on it at entry, releases the old block
    // (+0xF8 = 0), and only CREATES a new one if the fs rect is resolved —
    // a SetText during an unresolved rect leaves the fs blockless with the
    // refcounted zombie node still painting. If the bit is also clear at that
    // point, nothing ever rebuilds (the stuck-blockless state InlineTexture's
    // flush nudges by re-setting this bit).
    OFF_FONTSTRING_DIRTY_FLAGS = 0x60,
    // The per-node draw builder: walks a node's wrapped lines and calls the
    // glyph emitter (FUN_TEXT_EMITTER) once per line. Called from the paint's
    // per-node pre-pass FUN_005cd6a0 (`__thiscall(node)`, no stack args). We
    // co-hook it to stamp exact BUILD BOUNDARIES for the emitter's first-line
    // detection — the old `text == node+text-ptr` heuristic silently failed on
    // pfUI-processed chat lines (stale/preprocessed pointer), leaving inherited
    // records on reused node addresses (ghost icons) or never clearing them.
    FUN_TEXT_DRAW_BUILDER = 0x005CDC20,
    // The gxu text-node FREE: unlinks the node from its layout lists and pushes
    // it onto the node free list (DAT_00c2b98c) for reuse. Single caller —
    // FUN_005c1d00, the HTEXTBLOCK handle release (handle vtbl 0x008026e4 dtor
    // 0x0044D5C0 → 0x005c1d00 → here). This is the choke point where every
    // text node dies and its address becomes reusable: InlineTexture hooks it
    // to erase ALL per-node state exactly at death, making stale-record and
    // stale-owner bugs (ghost icons, orphaned records) structurally impossible
    // instead of heuristically guarded.
    FUN_TEXT_NODE_FREE = 0x005CD950,
    // Ensure a text node is laid out: `__fastcall(node)` -> FUN_005cd3f0 + the
    // draw builder FUN_005CDC20 (runs the glyph emitter and finalizes the node
    // origin +0x70/+0x74), then clears node+0xc0. Same "ensure built" the paint
    // pass (FUN_005c8fe0) calls per node; safe to call directly (the builder
    // re-lays the node — a clean node just re-emits the same glyphs). Verified
    // callers: the paint pass and FUN_005cd4d0 (the width/resize helper).
    // Text::InlineTexture drives it PRE-RENDER from the SMF-refresh early-apply
    // so a chat line's icons are recorded before the frame draws.
    FUN_TEXT_ENSURE_BUILT = 0x005CD6A0,

    // CScrollingMessageFrame display refresh — `__thiscall(smf, int newestIdx)`.
    // Rebuilds the visible line set bottom-up: per slot it SetTexts the line
    // fontstring (FUN_00788af0 -> SetText FUN_00771d80 + enqueue-resolve
    // FUN_007680e0 flag 1), (re)anchors newly-allocated lines, shows/hides by
    // message. Every new message shifts the newest index, so EVERY visible slot
    // is re-SetText'd -> its inline icons must be recomputed; the engine builds
    // the line lazily at paint, so icons land one frame after the glyphs (the
    // one-frame lag). Text::InlineTexture co-hooks this and, post-refresh,
    // forces each icon-bearing line to resolve+build and applies its icon
    // placement synchronously — pre-render (the refresh runs from the SMF's
    // Lua/event-driven methods, never mid-render), so icons draw with their
    // glyphs the same frame. Line-entry layout verified from the decompile:
    // count@+0x3A0, stride-0x10 entry array@+0x3A4, entry+8 = the line fs.
    FUN_SMF_DISPLAY_REFRESH = 0x00788750,
    OFF_SMF_LINE_COUNT = 0x3A0,     // int: allocated visible-line count
    OFF_SMF_LINE_ARRAY = 0x3A4,     // base of the stride-0x10 line-entry array
    SMF_LINE_STRIDE = 0x10,         // per-line-entry stride
    OFF_SMF_LINE_FONTSTRING = 0x8,  // entry+8 = the line's CSimpleFontString
    // THE pen↔anchor unit bridge. RebuildString (0x7724A0) multiplies every
    // text-unit quantity by region+0x7C when crossing into node creation:
    // nodePos = inset×s + rect corner, nodeFontH = fontPx×s, spacing = +0xF4×s,
    // and divides rect extents by s for the text-unit width/height. SetParent
    // (FUN_0076AB10) propagates the parent's +0x7C down the frame tree — it's
    // the per-object layout/UI-scale chain. This single scalar is what every
    // earlier "derive the scale" attempt (13/16, ownerH/fontH) was guessing at.
    OFF_LAYOUT_SCALE = 0x7C,          // float: anchor units per text/pen unit
    OFF_FONTSTRING_INSET_X = 0x110,   // float, text units (RebuildString posX term)
    OFF_FONTSTRING_INSET_Y = 0x114,   // float, text units (RebuildString posY term)
    // CSimpleRegion::SetParentAndLayer — __thiscall(region, parentFrame, layer,
    // show). Handles old-parent unlink + new-parent region-registry insert +
    // conditional Show. Verified from the region base ctor FUN_0077F640 and the
    // message frame's per-line setup (FUN_00788750 calls it with (frame, 2, 1)).
    FUN_REGION_SET_PARENT_AND_LAYER = 0x0077FD10,
    OFF_REGION_PARENT = 0x9C,         // region's parent frame ptr (read by Show's gates)
    OFF_REGION_DESIRED_SHOWN = 0xC4,  // Show (FUN_0077FCB0) NO-OPS unless this is set;
                                      // engine callers always write it before show/hide
    OFF_REGION_ACTUALLY_SHOWN = 0xC8, // 1 after Show completed (the realize latch)

    // Owning-frame recovery for the inline-icon pool (the 3.3.5 ownership model,
    // no overlay). Chat renders through the strata walker FUN_007657d0 →
    // per-frame draw-list rebuild FUN_00765920 → per-layer render FUN_0076FB00 →
    // text paint (NOT through the child-frame render FUN_0076B3F0). So
    // Text::InlineTexturePool co-hooks the two below:
    //   • FUN_FRAME_DRAWLIST_REBUILD — `__fastcall(frame)`, receives the frame
    //     cleanly and owns its 5 inline draw layers at frame + OFF_FRAME_LAYER_BASE
    //     + i*FRAME_LAYER_STRIDE. We record layer→frame so the layer-render hook
    //     can recover the owning frame with no offset-scan / vtable guessing.
    //   • FUN_FRAME_LAYER_RENDER — `__fastcall(layer)`, the per-layer paint that
    //     directly wraps the chat text paint. We bracket the icon pool's pass here
    //     (the flush runs nested) and look the frame up in the layer→frame map.
    FUN_FRAME_DRAWLIST_REBUILD = 0x00765920,
    FUN_FRAME_LAYER_RENDER = 0x0076FB00,
    OFF_FRAME_LAYER_BASE = 0x1C,   // first inline draw layer, region-relative
    FRAME_LAYER_STRIDE = 0x30,     // per-layer stride
    FRAME_LAYER_COUNT = 5,         // BACKGROUND..OVERLAY
    // Resolved on-screen rect of a region: 4 floats at regionBase+0x64 (=
    // LayoutFrame+0x40), in GxU SCREEN PIXELS — verified: FUN_00770670 reads
    // them and FUN_007705b0 stores them straight as GxU vertex corners. Layout is
    // {yA, left, yB, right} (x at [1]/[3], y at [0]/[2]); use min/max to get
    // left/top without pinning which y index is top.
    OFF_REGION_RECT = 0x64,

    // "Currently displayed thing" state fields on a GameTooltip frame
    // instance. Each Set* path writes one of these (and zero or two
    // others), and the per-tooltip Clear at FUN_00530050 zeroes all of
    // them on Hide/before-redraw. The Get* methods are simple reads —
    // whichever field is non-zero tells us what kind of tooltip is up.
    //
    // Verified by decoding the builder functions:
    //   - BuildItemTooltip  (0x0052B650) writes +0x380/+0x384 (item
    //                       GUID, only when there's a real CGItem) and
    //                       +0x398 (itemID) at 0x0052B6CE / 0x0052B6FE.
    //   - BuildSpellTooltip (0x0052E610) writes +0x39C (spellID) at
    //                       0x0052E6D5 (param_7==0 branch — skipped for
    //                       the next-rank tooltip side-build).
    //   - BuildUnitTooltip  (FUN_00529FE0) writes +0x368/+0x36C (unit
    //                       GUID) — see SetUnit block below.
    //   - BuildGameObjectTooltip (0x0052AA20) writes +0x370/+0x374
    //                       (gameobject GUID) at 0x0052AA52 / 0x0052AA59.
    //                       Only call site is the in-world hover handler
    //                       FUN_00492890; no Lua `SetGameObject` method.
    //   - Clear (FUN_GAMETOOLTIP_CLEAR) zeroes unit(+0x368)/GO(+0x370)/
    //     itemID(+0x398)/spell(+0x39c) — but NOT the item GUID +0x380/+0x384.
    //     That omission means the item GUID goes stale across a switch to a
    //     tooltip that shows no item (SetUnitAura, SetEquipmentSet, …), so
    //     GameTooltip:GetItem would resolve the *previous* item. We co-hook
    //     the clear (Tooltip::ClearItemGuid) to zero +0x380/+0x384 too, so
    //     every fresh tooltip resets it and only the item builder re-sets it.
    //     All 14 Set*/builder paths route through this clear, so the co-hook
    //     covers them uniformly. __fastcall(self).
    FUN_GAMETOOLTIP_CLEAR = 0x00530050,
    OFF_TOOLTIP_ITEM_GUID_LO = 0x380, // 0 for SetItemByID (no CGItem); stale-safe via the clear co-hook
    OFF_TOOLTIP_ITEM_GUID_HI = 0x384,
    OFF_TOOLTIP_ITEM_ID = 0x398,
    OFF_TOOLTIP_SPELL_ID = 0x39C,
    // Auction/compare descriptor. Script_GameTooltip_SetAuctionItem
    // (0x00535810) has no CGItem instance, so it stashes the listing's
    // random-property (suffix) id at tooltip+0x424 and marks the
    // compare-descriptor valid by writing the builder's compare flag to
    // this[0x110] (tooltip+0x440) = 1 (SetAuctionItem passes param_6=1;
    // every other Set* path passes 0, and the builder writes it on each
    // param_7==0 build — so +0x440 is a reliable per-build gate, not
    // stale). This is 1.12's analog of 3.3.5's `tooltip[0x130]`-gated
    // `piVar5[0x2d]` random-property read in GameTooltip:GetItem. GetItem
    // reads +0x424 (only when +0x440 is set) to put the suffix in the link.
    OFF_TOOLTIP_COMPARE_FLAG = 0x440,
    OFF_TOOLTIP_COMPARE_SUFFIX = 0x424,
    // Unit GUID written by the inner unit-tooltip builder
    // (FUN_00529FE0) when `tooltip:SetUnit(token)` resolves the token
    // to a non-zero GUID. Cleared by the same `FUN_00530050` clear
    // that handles the item/spell IDs above, so `(lo|hi) == 0` means
    // "no unit currently displayed" — same gating pattern HasSpell /
    // HasItem use.
    OFF_TOOLTIP_UNIT_GUID_LO = 0x368,
    OFF_TOOLTIP_UNIT_GUID_HI = 0x36C,
    // GameObject GUID written by the in-world hover tooltip populator
    // (FUN_0052AA20) — there is no Lua-callable `SetGameObject` method
    // in vanilla, so this slot only ever fills when the player mouses
    // over a gameobject in the world (nodes, chests, doors, etc.).
    // Same clear/gating semantics as the unit GUID slot.
    OFF_TOOLTIP_GAMEOBJECT_GUID_LO = 0x370,
    OFF_TOOLTIP_GAMEOBJECT_GUID_HI = 0x374,
    // Owner frame stored by `tooltip:SetOwner(frame, anchor)` and
    // compared by `IsOwned`. Holds `owner_CObject + OFF_FRAME_LAYOUT_SUBOBJECT`
    // (i.e. the LayoutFrame*, not the bare Frame*), or 0 if unowned.
    // Verified via the helper at 0x0052FFE0 invoked by SetOwner's tail
    // (writes `[this+0x314] = arg+0x24`) and Script_GameTooltip_IsOwned
    // at 0x00530FE0 (reads `[edi+0x314]` and compares against the same
    // `+0x24`-offset value).
    OFF_TOOLTIP_OWNER = 0x314,

    // CGItem → fully-dressed item link string. __fastcall(ecx = CGItem *)
    // → const char *. Reads the item's itemID, quality, permanent
    // enchant ID, random-properties seed/factor, and unique ID off the
    // CGItem's instance block + descriptor, builds the dressed name via
    // FUN_005D8BC0 (handles random-suffix decoration like "Foo of the
    // Bear"), then sprintf's into the global buffer at DAT_00C0CF68 and
    // returns its address. The returned pointer is to a long-lived
    // engine global, safe to read until the next call.
    //
    // Same helper Script_GetContainerItemLink (0x004F9930) and
    // Script_GetInventoryItemLink (0x004C8C10) call after resolving
    // their slot-form args. Bypassing them lets us build dressed links
    // for tooltips set via SetMerchantItem / SetLootItem / etc. where
    // the item isn't in the player's bag/equipment.
    FUN_GAMETOOLTIP_BUILD_ITEM_LINK = 0x0052AE00,

    // CGItem → decorated instance display name. __thiscall(ecx = CGItem *,
    // char *outBuf, uint outSize). Reads the instance's random-suffix ID
    // off the descriptor (+0x98, gated on the broken flag) and writes the
    // plain — uncolored, unbracketed — display name into outBuf:
    // "Ethereum Torque of the Sorcerer" when a suffix is present, the base
    // ItemStats name otherwise (via the item cache + ItemRandomProperties
    // localized suffix at record +0x1c). This is the inner name builder
    // FUN_GAMETOOLTIP_BUILD_ITEM_LINK wraps in brackets, so the name it
    // produces matches GetItemLink's bracketed name and modern
    // C_Item.GetItemName exactly. Inner formatter FUN_005D8B00.
    FUN_ITEM_BUILD_INSTANCE_NAME = 0x005D8BC0,

    // Inner name formatter behind FUN_ITEM_BUILD_INSTANCE_NAME, callable
    // WITHOUT a CGItem: `__fastcall(char *out /*ecx*/, uint outSize /*edx*/,
    // uint32 itemID, int suffixID)`. Fetches the ItemStats record for itemID
    // and, when suffixID is a valid ItemRandomProperties row with a localized
    // suffix name (record +0x1c + locale*4), formats base + suffix via
    // ITEM_SUFFIX_TEMPLATE; otherwise the base name. Lets the by-STRING item
    // paths (GetItemNameByID, C_Item.GetItemInfo) apply an item link's random
    // suffix without a live item instance.
    FUN_ITEM_BUILD_NAME_FROM_ID = 0x005D8B00,

    // Engine's inventory swap-and-send. Same primitive
    // `Script_EquipCursorItem` (0x00489660) uses after the cursor's
    // source location has been resolved. Sends opcode 0x10D
    // (CMSG_SWAP_INV_ITEM) for same-container swaps or 0x10C
    // (CMSG_AUTOEQUIP_ITEM) for cross-container, then runs the
    // packet through the engine's own send pipeline at FUN_005AB630.
    //
    // Signature:
    //   void __thiscall(
    //     CGPlayer *this,
    //     u32 srcItemGuidLo, u32 srcItemGuidHi,
    //     u32 srcContainerGuidLo, u32 srcContainerGuidHi,
    //     u32 srcLinearSlot,
    //     u32 dstContainerGuidLo, u32 dstContainerGuidHi,
    //     u32 dstLinearSlot,
    //     int flag);   // 0 = normal path
    //
    // Linear-slot encoding for sources/dests in player invMgr:
    //   0..18  paperdoll (1-based slot - 1)
    //   19..22 equipped bag containers (bag IDs 1..4 themselves)
    //   23..38 backpack contents (1-based bag-0 slot S → 22 + S)
    // For sources in a CGContainer (equipped bag B = 1..4), the
    // container GUID is the bag's own GUID and srcLinearSlot is
    // 0-based within that bag (1-based Lua slot - 1).
    //
    // This call neither reads nor writes the cursor-state globals at
    // [0xBE0810] / [0xBE0814]; cursor visibility is purely a side
    // effect of the cursor-pickup path that normally precedes it.
    // Calling this directly produces a server-side swap with no
    // client-side cursor manipulation.
    FUN_INVENTORY_SWAP = 0x005E0C40,

    // Sister helper to FUN_INVENTORY_SWAP — packet builder for
    // `CMSG_SPLIT_ITEM` (opcode 0x10E). Same __thiscall ABI shape;
    // the item-GUID args (param_1, param_2) are unused padding for
    // ABI parity. Packet wire format:
    //   [0x10E, srcBag, srcSlot, dstBag, dstSlot, count]
    // where srcBag/dstBag are byte-converted from container GUIDs by
    // the same `FUN_005e13b0` helper the swap function uses
    // (`0xFF` = INVENTORY_SLOT_BAG_0 / player, `19..22` = equipped bags).
    //
    // Server semantics are all-or-nothing: any failure
    // (insufficient source, dest has different item, dest would
    // overflow maxStack) leaves source untouched and emits
    // `SMSG_INVENTORY_CHANGE_FAILURE`. No cursor involvement on send
    // or response.
    //
    // Signature:
    //   void __thiscall(
    //     CGPlayer *this,
    //     u32 unused1, u32 unused2,             // ABI padding
    //     u32 srcContainerLo, u32 srcContainerHi,
    //     u32 srcLinearSlot,                    // only low byte hits the wire
    //     u32 dstContainerLo, u32 dstContainerHi,
    //     u32 dstLinearSlot,                    // only low byte
    //     u32 count);                           // only low byte
    FUN_INVENTORY_SPLIT = 0x005E1210,

    // Registers a single global Lua function. __fastcall(name, func).
    FUN_FRAMESCRIPT_REGISTER_FUNCTION = 0x00704120,

    // `FrameScript_Object::ScriptRegister(this, name)` — `__thiscall`,
    // `this` = a `CFrameScriptObject *`. On first call (when `this+0x04`
    // is zero) builds a Lua wrapper table `{[0] = lightuserdata(this)}`
    // with `_G["__framescript_meta"]` as metatable, `luaL_ref`s it into
    // the registry, stores the refkey at `this+0x08`. Always increments
    // `this+0x04` (the Lua-side refcount). Optional `name` argument
    // installs `_G[name] = wrapper` for engine-named frames.
    //
    // We call this in `PushNamePlateFrame` so the engine and our own
    // C_NamePlate getters operate on the **same** wrapper table —
    // every push through `lua_rawgeti(REGISTRY, this+0x08)` (the
    // canonical engine path) lands on the same Lua object pfUI
    // received in `NAME_PLATE_CREATED`, so addon-set fields
    // (`plate.nameplate = decoratedButton`) survive engine-side
    // re-fetches. Earlier note in `Info.cpp` warned about pinning the
    // refcount; for pool-managed nameplates the engine never
    // un-registers them anyway, so the pin is benign.
    FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER = 0x00701BD0,

    // `this+0x04` Lua refcount, incremented by `ScriptRegister`. We
    // read it as a "has the engine ever exposed this CObject to Lua"
    // probe — equivalent to checking `this+0x08 > 0` but more direct.
    OFF_COBJECT_LUA_REFCOUNT = 0x04,

    // `this+0x08` — the CObject's Lua-registry ref (an int key into the
    // registry table). `lua_rawgeti(REGISTRY, this[+0x08])` pushes the
    // frame's Lua-side object. Lazily populated by ScriptRegister when the
    // refcount above is 0.
    OFF_COBJECT_LUA_REF = 0x08,

    // Direct cvar lookup — `__fastcall(const char *name) → CVar* | NULL`.
    // Hash-table by-name lookup over the CVar registry; same call
    // `Script_GetCVar` makes internally before the engine wraps the
    // result in lua_pushstring + a "CVar doesn't exist" error path.
    // Calling it directly lets us skip both the Lua roundtrip and the
    // unknown-cvar error — we coerce NULL to false instead, matching
    // modern `C_CVar.GetCVarBool` semantics.
    //
    // The returned struct holds the value string at `+0x20` (read by
    // `Script_GetCVar` at `0x00488BF9` as `mov edx, [eax+0x20]; call
    // lua_pushstring`). The value is the raw `char *` the engine
    // stores; reading it directly is safe for the lifetime of the
    // cvar (vanilla cvars don't get re-allocated outside `/console
    // set` flows, which we don't race here).
    FUN_FIND_CVAR = 0x0063DEC0,
    OFF_CVAR_VALUE_STR = 0x20,

    // Internal CVar registrar — what `Script_RegisterCVar` calls after a
    // `FindCVar` miss (the call at `0x00488B8A`). `__fastcall`; ECX=name,
    // EDX is a second string slot the script path leaves 0, then six
    // stack args: flags, defaultValue, changeCallback, categoryId,
    // hiddenBool, userData. Dedups by name (re-registering OR-merges
    // flags + updates the callback), so it's idempotent across `/reload`.
    // The new-cvar branch forces flags bit 0 on (`puVar3[7] = flags | 1`),
    // which is what makes script-registered cvars persist to Config.wtf.
    // See `CVar::Factory` for the wrapper.
    FUN_REGISTER_CVAR = 0x0063DB90,
    // Internal "apply value" — `__fastcall(cvar /*ecx*/, value, a3, a4,
    // a5, a6)`, called by `Script_SetCVar` at `0x00488CA8`. Fires the
    // change callback at `[cvar+0xBC]` as
    // `char __fastcall(cvar /*ecx*/, prevValue /*edx*/, newValue /*stack*/,
    // userData /*stack*/)` — return 0 to REJECT the change, nonzero to
    // accept (verified at `0x0063DF7D`).
    FUN_SET_CVAR_VALUE = 0x0063DF50,
    // CVar struct fields (beyond the value string at +0x20 above).
    OFF_CVAR_FLAGS = 0x1C,
    OFF_CVAR_CALLBACK = 0xBC,
    OFF_CVAR_USERDATA = 0xC0,
    // The categoryId the script path passes as the registrar's 6th stack
    // arg (the `PUSH 9` in `Script_RegisterCVar`). Cosmetic console
    // grouping; we reuse it so our cvars behave like script-registered ones.
    CVAR_SCRIPT_CATEGORY = 9,

    // Engine-side Lua C functions backing the in-game CVar globals.
    // Standard `int __fastcall(void *L)` ABI. We don't register them
    // ourselves in-game — the engine already does at boot — but we
    // re-register the same pointers on the glue Lua state so login /
    // char-select GlueXML can read and write CVars too (vanilla 1.12
    // exposes none of these in glue by default). CVar storage is
    // process-global, so a write from either state is visible to the
    // other.
    FUN_SCRIPT_REGISTER_CVAR = 0x00488B00,
    FUN_SCRIPT_GET_CVAR = 0x00488BA0,
    FUN_SCRIPT_SET_CVAR = 0x00488C10,
    FUN_SCRIPT_GET_CVAR_DEFAULT = 0x00488CF0,

    // `Script_RunScript` — the C function backing `_G.RunScript(code)`
    // in-game. `int __fastcall(void *L)`. Mirrored onto the glue Lua
    // state in `src/script/Run.cpp` so GlueXML can also `RunScript("...")`
    // — useful for slash-command-style debug helpers at the login /
    // realm / char-select screens.
    FUN_SCRIPT_RUN_SCRIPT = 0x0048B980,

    // Game::ResolveUnitToken — __fastcall(ecx = const char *token) → CGUnit_C *.
    // Returns the unit pointer for "player", "target", "party1", etc. Use this
    // rather than the global at 0x00B41414 — that global holds something
    // related (its +0xC0 has the player GUID) but is NOT the same CGPlayer_C
    // pointer the inventory routines expect.
    FUN_RESOLVE_UNIT_TOKEN = 0x00515940,

    // Set the player's current target to a GUID —
    // `__fastcall(uint64_t *guid)`. Validates the GUID resolves to a unit
    // (typemask 8) then commits the selection (CMSG_SET_SELECTION +
    // client-side target). This is what `Script_TargetUnit` (0x004899D0)
    // calls after resolving its token→GUID; we call it directly with a
    // GUID we already have (e.g. a totem creature). NOTE: reads `guid[0]`
    // (lo) and `guid[1]` (hi) through the pointer.
    FUN_TARGET_BY_GUID = 0x00489A40,

    // Tab-targeting internals, shared with our backported TargetNearest* /
    // TargetDirection* family (`target/Nearest.cpp`).
    //
    // `FUN_TARGET_CANDIDATE_VALID` (0x00493E40) — the engine's per-mode
    // candidate validity predicate used by the native `TargetNearestEnemy`
    // / `TargetNearestFriend` cycle. `__fastcall(CGUnit *player /*ecx*/,
    // CGUnit *candidate /*edx*/, int mode) → int` (nonzero = valid). Modes:
    // 1 = enemy (attackable + alive + not-critter), 2 = friend (assistable +
    // alive), 3 = party, 4 = raid. Wraps the reaction cores
    // `FUN_00606980` (can-attack) / `FUN_006066f0` (can-assist), so calling
    // it gives us the engine's exact hostility semantics for free.
    FUN_TARGET_CANDIDATE_VALID = 0x00493E40,
    // `FUN_SET_SELECTION_BY_GUID` (0x00493540) — canonical target setter the
    // native tab-cycle commits through. `__cdecl(uint32_t guidLo, uint32_t
    // guidHi)`. Validates + fires CMSG_SET_SELECTION + client target +
    // event 0xC3, and maintains the current-selection globals below.
    // Passing {0,0} clears the target.
    FUN_SET_SELECTION_BY_GUID = 0x00493540,
    // Current target-selection GUID (lo/hi), written by
    // `FUN_SET_SELECTION_BY_GUID`. We read it to detect whether the player's
    // target changed out from under an in-progress tab-cycle.
    VAR_CURRENT_SELECTION_GUID_LO = 0x00B4E2D8,
    VAR_CURRENT_SELECTION_GUID_HI = 0x00B4E2DC,

    // Local-player CGObject-like global. Not the same pointer as
    // ResolveUnitToken("player") returns — that one's the canonical
    // CGPlayer_C used by inventory etc. This pointer's +0xC0 field
    // holds the local player's 64-bit GUID, and the visible-object
    // iterator at FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS walks its
    // +0xAC list. Useful for "is this GUID me?" checks without
    // round-tripping through Lua. Also a NULL-check proxy for "is
    // the object manager initialised" — engine iterators deref this
    // unconditionally on entry, so callers must skip on the glue /
    // character-select screen.
    VAR_LOCAL_PLAYER_PTR = 0x00B41414,
    OFF_LOCAL_PLAYER_GUID = 0xC0,
    // `__fastcall(ecx = const char *token) → uint64_t GUID` — the
    // inner token-to-GUID step that `FUN_RESOLVE_UNIT_TOKEN` calls
    // before doing the active-object lookup. Returns the unit's
    // GUID without depending on the unit being visible / loaded as
    // a CGObject, so `partyN` / `raidN` resolve correctly even when
    // the member is out of range or on a different continent
    // (`Script_UnitName` uses this path too; that's why `UnitName`
    // already works for OOR party members in vanilla).
    //
    // Internal dispatch:
    //   - "player"             → `[localPlayer + 0x08]` (GUID ptr)
    //   - "target"             → `DAT_00B4E2D8`/`DAT_00B4E2DC` globals
    //   - "mouseover"          → `DAT_00B4E2C8`/`DAT_00B4E2CC` globals
    //   - "partyN"             → `FUN_004E81A0(slot)` → `[VAR_PARTY_GUIDS + slot*8]`
    //   - "raidN"              → `FUN_00491940(slot)` → raid GUID array
    //   - "partypetN", "raidpetN" similar (pet-slot variants)
    //   - "<arbitrary name>"   → raises "Unknown unit name: %s" via
    //                            the engine's error helper (so this
    //                            still errors on bad tokens — same
    //                            semantics as the existing
    //                            `UnitGUID` documented behavior).
    //
    // Don't use this from code paths that need to handle literal
    // character names — see CLAUDE.md "Resolving input to a name"
    // for the `lua_pcall(UnitName)` workaround. For pure unit-token
    // input it's the right primitive.
    FUN_TOKEN_TO_GUID = 0x00515970,

    // `__fastcall(const uint64_t *guid /*ecx*/, int *outCount /*edx*/) ->
    // char** tokenArray` — the engine's GUID → unit-token REVERSE map. Fills
    // a reused static buffer array (the returned pointer) with the name
    // string of every ENGINE-NATIVE token currently pointing at `guid` and
    // writes the count to `*outCount`. Checks, in order: player, pet, target,
    // party1..4, partypet1..4, raid1..40, raidpet1..40, npc, mouseover — the
    // same slots the per-token unit-event broadcast `FUN_00515e50` fans out
    // to (that function is just this map + a fire-per-token loop). Does NOT
    // know ClassicAPI's synthetic `focus` / `nameplateN` tokens (the engine
    // has no concept of them) — callers add those separately. Non-throwing:
    // returns count 0 pre-world (resolves the active player internally and
    // bails if absent). The returned array is a shared static reused on the
    // next call, so snapshot the strings before doing anything that could
    // re-enter. nampower names this `GetNamesFromGUID` (left as an unfilled
    // TODO in its offsets.hpp). Used by `Unit::Identity::TokensForGUID` for
    // the phase-2 `UNIT_SPELLCAST_*` remote-unit fan-out.
    FUN_UNIT_TOKENS_FROM_GUID = 0x00515C50,

    // Mouseover-unit GUID globals — the `"mouseover"` token resolves to
    // these (see the token-dispatch list above). Written ONLY by
    // `FUN_SET_MOUSEOVER_UNIT`; zero when nothing is moused over. Used by
    // `Unit::Mouseover` to detect the loss transition.
    VAR_MOUSEOVER_GUID_LO = 0x00B4E2C8,
    VAR_MOUSEOVER_GUID_HI = 0x00B4E2CC,

    // The engine's single mouseover set/clear chokepoint —
    // `__stdcall(guidLo, guidHi, arg3, arg4)` (early returns are all
    // `ret 0x10`, and the caller pushes 4 without cleaning). It is the
    // ONLY writer of `VAR_MOUSEOVER_GUID_*`, so every mouseover
    // gain/loss/change flows through it. On a unit gain it fires
    // `UPDATE_MOUSEOVER_UNIT` itself (via `FUN_FIRE_EVENT_NO_ARGS(0x150)`
    // in the friendly/hostile-unit tooltip case); on loss it clears the
    // GUID but fires nothing — the gap `Unit::Mouseover` closes.
    // Change-gated by its callers (if it ran per-frame the gain fire
    // would spam every frame while hovering, which vanilla doesn't), so
    // it's a cool hook target.
    FUN_SET_MOUSEOVER_UNIT = 0x00492890,

    // Event ID of `UPDATE_MOUSEOVER_UNIT` (name-table slot
    // `0x00BE16D8` → `(0x16D8 - 0x1198) / 4`). Fired with no args via
    // `FUN_FIRE_EVENT_NO_ARGS`; Lua handlers read `UnitExists("mouseover")`.
    EVENT_UPDATE_MOUSEOVER_UNIT = 0x150,

    // `SStrCmpI(a, b, n)` — Storm's case-insensitive memcmp-style
    // comparator. **`int __stdcall(const char *a, const char *b, int n)`**
    // — the function ends with `ret 0xc`, so the callee pops the
    // 3-arg stack frame. Declaring it as `__cdecl` and calling makes
    // MSVC emit a redundant `add esp, 12` post-call, drifting ESP
    // upward by 12 per call and corrupting the caller's stack frame
    // — manifested as a deep-Lua crash whose `L` pointer landed in
    // `.text`. Returns 0 when the first `n` characters match
    // (ignoring case) or both strings end before `n`.
    FUN_SSTR_CMP_I = 0x0064A4C0,

    // UNIT_FIELD_TARGET within `m_objectFields` — 64-bit GUID at byte
    // offsets +0x28 (lo) / +0x2C (hi). Verified by disassembling
    // `FUN_TOKEN_TO_GUID`'s suffix walker at `0x00515A1C-A2C`:
    // `mov eax, [obj + 0x110]; mov edi, [eax + 0x28]; mov ebx, [eax + 0x2c]`
    // — reads the target GUID to chain `targettarget`-style tokens.
    // Distinct from the higher field offsets in the
    // `OFF_UNIT_FIELD_*` block; UNIT_FIELD_TARGET is one of the few
    // 1.12 offsets that matches the CMaNGOS-documented vanilla
    // layout.
    OFF_UNIT_FIELD_TARGET = 0x28,

    // Owner/controller GUID fields within `m_objectFields`, each a 64-bit
    // GUID (lo/hi). Both byte-verified from engine readers:
    //   CHARMEDBY +0x10 — `Script_UnitIsCharmed`
    //                     (`mov ecx,[eax+0x10]; or ecx,[eax+0x14]; jz`).
    //   CREATEDBY +0x20 — the unit owner/title builder `FUN_0052FD30`
    //                     reads `[desc+0x20]`/`[+0x24]` as the summoner GUID.
    // A pet/guardian/totem's owner lives in CREATEDBY; a charmed unit's in
    // CHARMEDBY. `Unit::Pet` reads these to find a minion's player owner.
    // (The engine uses only these two for ownership — there's a GUID slot
    // at +0x18 too, but nothing reads it as an owner, so it's left out.)
    OFF_UNIT_FIELD_CHARMEDBY = 0x10,
    OFF_UNIT_FIELD_CREATEDBY = 0x20,

    // Pet-vs-minion discriminator for an owned unit: `int __fastcall(unit)`.
    // `0x00605570` is really the engine's **creature-type resolver** — it's
    // `Script_UnitCreatureType`'s (`0x0051A280`) inner helper and returns the
    // `CreatureType.dbc` id (1=Beast, 3=Demon, 7=Humanoid, …), via three
    // paths in order: a display-override at descriptor `+0x212`, then the
    // creature-cache type at `[unit+0xB30]+0x18`, then the player-race type
    // at `ChrRaces[[unit+0x110]+0x78]+0x24`. `FUN_UNIT_CREATURE_TYPE` is the
    // canonical name; `FUN_UNIT_PET_MINION_CLASS` is a historical alias for
    // the same address.
    //
    // The pet/minion use: the unit-title builder `FUN_0052FD30` calls it as
    // `(fn(unit) != 1) + 1` → 1 = "X's Pet" title, 2 = "X's Minion". That
    // works because it's really testing "is this a Beast (type 1)": hunter
    // pets are Beasts, warlock minions are Demons (type 3) — so `!= 1`
    // cleanly separates them, which neither the GUID (pets and guardians
    // share the 0xF14… prefix) nor UNIT_FLAG_PLAYER_CONTROLLED reliably does.
    FUN_UNIT_PET_MINION_CLASS = 0x00605570,
    FUN_UNIT_CREATURE_TYPE = 0x00605570,

    // Party / raid roster counts and the party GUID array referenced
    // in the `FUN_TOKEN_TO_GUID` dispatch comment above. Used by
    // `UnitTokenFromGUID` to cap its candidate iteration — solo
    // players skip all 88 group tokens; a 5-person party scans 4
    // slots instead of 40.
    //
    // - `VAR_PARTY_GUIDS` — 4-slot QWORD array (one GUID per party
    //   member, low+high dwords). Walked by `Script_GetNumPartyMembers`
    //   (`FUN_004E86D0`) at `0x004E86D0` to compute the count.
    // - `VAR_RAID_MEMBER_COUNT` — single int maintained by the
    //   engine's raid-roster handler. Read directly by
    //   `Script_GetNumRaidMembers` (`FUN_004BB530`).
    VAR_PARTY_GUIDS = 0x00BC6F48,
    PARTY_MAX_SLOTS = 4,
    VAR_RAID_MEMBER_COUNT = 0x00B713E0,
    RAID_MAX_SLOTS = 40,

    // Chat-event dispatcher — single choke point through which all
    // CHAT_MSG_* events fire after the SMSG_MESSAGECHAT packet handler
    // (opcode 0x96 → FUN_0049D560) parses the wire data. Called with
    // the sender GUID as stack args 9 and 10 (lo, hi).
    //
    // Calling convention: `__fastcall`, `RET 0x28` — ECX = the MESSAGE
    // text, EDX = chat type, then 10 stack args ending in the sender
    // GUID pair (lo, hi). (ECX was previously mislabeled "sender name";
    // verified from the call-site disassembly in FUN_0049D560 —
    // `MOV ECX,[EBP-0x10]` is the raw/processed message. The message is
    // copied into one buffer that feeds BOTH the chat frame and the
    // say/yell bubble spawn FUN_00608AC0.) Called from:
    //   - FUN_0049D560 directly for live (non-throttled) chat
    //   - The pending-chat queue processor (`__AUPENDINGCHAT`) for
    //     messages buffered via FUN_0049CAE0 when the engine flag at
    //     0x008435FC is set
    //   - Many synthetic chat synthesizers throughout the codebase
    //     (system notifications, arena team membership changes, etc.)
    //     which pass 0 / NULL for the GUID args
    //
    // Hooked (once) by `Chat::Dispatch`, which orchestrates the concerns
    // sharing this choke point: `Chat::CurrentGUID` (publish the sender
    // GUID for `GetCurrentChatGUID()`), `Chat::IconFilter` (strip
    // player-injected `|T` icon spoofs from chat + speech bubbles), and
    // `Chat::RaidMarkers` (expand `{rtN}`/`{skull}`/… into inline markers).
    FUN_CHAT_DISPATCH = 0x0049A870,
    // Per-player inventory manager lives at this offset on the player object.
    // +0x00 = u32 slot count (OFF_INVMGR_SLOT_COUNT), +0x04 = u64* GUID
    // array (see OFF_INVMGR_GUID_ARRAY below for the slot-range map).
    OFF_PLAYER_INVENTORY_MANAGER = 0x1D38,
    OFF_INVMGR_SLOT_COUNT = 0x00,

    // --- Descriptor-field observer system --------------------------------
    // The engine's generic "watch a descriptor field range for changes"
    // mechanism (nodes tagged "__AUCMirrorHandler__", 0x30 bytes). The
    // registrar attaches a node to the object's per-field observer anchors;
    // each node keeps a private MIRROR of the watched bytes, seeded from
    // the live value at registration. During SMSG_UPDATE_OBJECT processing
    // the dispatcher FUN_00465570 memcmps live vs mirror and invokes the
    // callback only on a real change (the mirror is then re-synced by
    // FUN_004667A0). Firing engine events from the callback is sanctioned —
    // the engine's own bag observer does exactly that.
    //
    // Registrar — __fastcall(int bank /*ecx*/, uint32_t fieldOffset /*edx*/,
    //   uint32_t guidLo, uint32_t guidHi, int size, const void *callback,
    //   void *userArg1, void *userArg2). `guid` selects the watched OBJECT
    //   (player, item, container, …); `bank` selects its field bank
    //   (1 = ITEM, 2 = CONTAINER, 4 = PLAYER — the engine's per-bag
    //   manager FUN_004F91A0 registers bank-2 observers on each equipped
    //   bag's CGContainer); `fieldOffset` is BANK-relative (container
    //   SLOT_1 = 8, not its descriptor-absolute 0x20). The engine's
    //   inventory setup FUN_004F8CC0 registers the player's bag/backpack/
    //   bank/keyring GUID ranges this way — but NOT equipment.
    FUN_DESC_OBSERVER_REGISTER = 0x00467E70,

    // Unregister — __fastcall(int bank /*ecx*/, uint32_t fieldOffset
    //   /*edx*/, uint32_t guidLo, uint32_t guidHi, const void *callback,
    //   void *userArg1). Removes the node matching (callback, userArg1)
    //   from that object+field's observer list; harmless no-op when
    //   nothing matches. What FUN_004F91A0 calls for an unequipped bag.
    FUN_DESC_OBSERVER_UNREGISTER = 0x00467FB0,

    // Observer callback ABI (verified from the dispatcher's call site at
    // 0x004655DC + the engine callback FUN_004F8DB0's RET 0x10):
    //   int __fastcall cb(uint32_t fieldOffset /*ecx, as registered*/,
    //                     uint32_t size /*edx*/,
    //                     uint32_t guidLo, uint32_t guidHi,
    //                     const uint32_t *oldValue /*the node's mirror*/,
    //                     void *userArg1)   → return 1, callee cleans 0x10.
    // The live (new) value is NOT passed — read it from the object (the
    // engine's bag callback reads the invMgr GUID array).

    // ---- Unit-event token observers (Unit::TokenObserver) ---------------
    // The engine makes target/party/raid/pet/mouseover units fire per-token
    // unit events (UNIT_HEALTH, UNIT_MANA, UNIT_AURA, …) by watching bank-UNIT
    // descriptor fields: FUN_0051bbb0 loops event index i in
    // [0, EVENT_NAME_UNIT_MAX); for each whose static name slot is non-null it
    // registers a bank-3 observer on field i*4 (per-index size below) with
    // callback FUN_0051bd50 → the broadcast FUN_00515e50(guid, i), which fires
    // event i once per token referencing the guid. The FIELD INDEX IS THE EVENT
    // ID (health field +0x40 → event 16 = UNIT_HEALTH). Unit::TokenObserver
    // replicates that exact loop with a caller's own callback to make synthetic
    // tokens (focus, nameplateN) first-class; the registrar always appends a
    // fresh node (FUN_00467f00, no dedup), so ours coexists with the engine's —
    // a unit that is also the target fires both "target" and our token. The
    // broadcast always fires with format "%s" + the token (DAT_0082e280).
    //   Per-index watched size (FUN_0051bbb0's switch): i∈{0,2,4,10}=8,
    //   0x29=0xD8 (UNIT_FIELD_AURA), 0x6B=0x30, 0xA7/0xAE=0x1C, else 4.
    VAR_EVENT_NAME_TABLE_STATIC = 0x00BE1198, // char*[] boot name array; slot i (base+i*4) non-null ⇒ real event i
    EVENT_NAME_UNIT_MAX = 0xB6,               // FUN_0051bbb0's loop bound

    // The engine's inventory observer setup — registers the bag-slot
    // GUID-field observers above, once per enter-world (sole caller is the
    // enter-world initializer FUN_004908C0, latched by DAT_00B4B424).
    // Player::Equipment co-hooks it to register the equipment-slot
    // observers the engine never installs; the co-hook inherits the exact
    // engine timing + lifetime (nodes die with the player object).
    FUN_INV_OBSERVER_SETUP = 0x004F8CC0,

    // PLAYER_FIELD_INV_SLOT_HEAD — first of the 19 equipment-slot u64 GUID
    // fields in the player descriptor (0x4A8..0x538, stride 8; 0x540 is
    // the first equipped-bag slot). 0-based slot = (offset - 0x4A8) >> 3;
    // Lua inventory slot (GetInventoryItemLink etc.) = that + 1.
    OFF_DESC_PLAYER_EQUIP_FIRST = 0x4A8,
    DESC_PLAYER_EQUIP_SLOTS = 19,
    DESC_OBSERVER_BANK_PLAYER = 4,
    DESC_OBSERVER_BANK_UNIT = 3, // CGUnit descriptor bank (FUN_0051bbb0's watch loop)

    // ITEM_FIELD_DURABILITY, ITEM-bank-relative: descriptor-absolute
    // +0xA0 (field index 0x28, which counts the 6 OBJECT fields) minus
    // the 0x18-byte OBJECT prefix. u32.
    //
    // DEAD END — bank-1 (ITEM field) observers register without error but
    // are NEVER invoked: item values-updates demonstrably arrive
    // (verified by probing the values pre-pass FUN_00465330) and
    // player-bank observers fire in the same session, yet item-bank
    // callbacks stay silent. Registration arithmetic was fully verified
    // (bank base FUN_00465690(1)=0x18, next-bank walk FUN_004656E0
    // item: 0→1→2, anchors match dispatch), so the suspect is the
    // registrar's internal wrapper lookup (FUN_00464890 — the observer
    // registry, NOT the object manager) silently no-op'ing for item
    // objects. UPDATE_INVENTORY_DURABILITY is instead backed by
    // FUN_INVENTORY_ALERTS_RECOMPUTE below. Constants kept for the
    // investigation trail.
    DESC_OBSERVER_BANK_ITEM = 1,
    OFF_DESC_ITEM_DURABILITY = 0x88,

    // The engine's inventory-alerts recompute — walks the equipment
    // slots, reads each item's ITEM_FIELD_DURABILITY / MAXDURABILITY /
    // broken flag from the descriptor, fills the per-slot alert table at
    // [DAT_00B71F60], and UNCONDITIONALLY fires UPDATE_INVENTORY_ALERTS
    // (event 501) at the end. The engine calls it whenever an owned
    // item's fields change (the item post-update handler FUN_005D9A90 —
    // which also fires UNIT_INVENTORY_CHANGED and the bag-update chain —
    // routes every item values-update through it) plus equip and
    // enter-world paths. That makes it the engine's own "durability may
    // have changed" signal: Player::Equipment co-hooks it and diffs a
    // {guid, durability} snapshot per slot to back
    // UPDATE_INVENTORY_DURABILITY. void __fastcall(), no args.
    FUN_INVENTORY_ALERTS_RECOMPUTE = 0x004C7EE0,
    // ItemMgr::GetItemBySlot — __thiscall(this, slot) → CGItem* (NULL if empty).
    // Slot is the engine's linearized slot index, not bagID/slot tuple.
    FUN_ITEMMGR_GET_ITEM_BY_SLOT = 0x006228A0,
    // CGUnit visible-items helper used by `Script_GetInventoryItemLink` for
    // non-player units (target/party/inspect targets). __thiscall(this=unit,
    // int 0-based slot) → visible-item entry*, or NULL if slot is out of
    // [0, 18]. Reads `[unit + 0xE68]` (visible-items array base for the
    // unit) and indexes `base + 0x118 + slot * 0x30`. Each 0x30-byte entry
    // holds the itemID at `+0x08`; the engine reads it back exactly that
    // way before feeding it to `_GetRecord` for hyperlink construction
    // (verified in `Script_GetInventoryItemLink` at `0x004C8D05`-`0x4C8D34`).
    //
    // **Crash hazard**: `[unit + 0xE68]` is uninitialized for NPCs
    // (CGCreature_C objects). The helper has no NULL check — it computes
    // `garbage + 0x118 + slot*0x30` and returns that as a valid pointer.
    // The engine relies on callers to gate this with `UnitPlayerControlled`
    // first; we do the same in `Item::InventoryID`.
    FUN_UNIT_GET_VISIBLE_ITEM = 0x005F0D60,
    OFF_VISIBLE_ITEM_ITEM_ID = 0x08,

    // CGUnit m_objectFields pointer offset. Different from CGItem's
    // descriptor at +0x114 — these are sibling classes under CGObject
    // with class-specific descriptor offsets.
    OFF_UNIT_DESCRIPTOR = 0x110,

    // CGCreature client-side creature-data cache row. Populated by the
    // engine when an NPC GUID becomes visible (the same row the
    // `CreatureCache` SMSG fills with the templated NPC's name,
    // subname, etc.). NULL for unsynced units or for player CGUnits
    // (players don't have a row in this cache — their name comes from
    // the SMSG_NAME_QUERY_RESPONSE-fed name cache at `0x00C0E228`).
    //
    // Layout of the cache row:
    //   +0x00  ?
    //   +0x0C  name C-string ptr (alias of UNIT_NAME for creatures)
    //   +0x10  subName C-string ptr  ("Innkeeper", "<Master Trainer>", etc.)
    //   +0x1C  CreatureFamily id (→ CreatureFamily.dbc), 0 = none
    //
    // Used by `Script_UnitName`'s creature path and by VanillaMinimapTracking's
    // subname-based blip filter (`src/Blips.cpp::ExtractUnitSubName`).
    OFF_UNIT_CREATURE_CACHE_ROW = 0xB30,
    OFF_CREATURE_CACHE_SUB_NAME = 0x10,
    // CreatureFamily id within the +0xB30 cache row — the value the engine's
    // `Script_UnitCreatureFamily` (`0x0051A310`) reads via its family helper
    // `FUN_006055e0` (`[unit+0xB30] ? [row+0x1C] : 0`) before mapping it
    // through `CreatureFamily.dbc` for the localized name. `UnitCreatureFamilyID`
    // returns the raw id. Same field as `OFF_CREATURE_FAMILY` in the creature
    // cache data block, reached through the unit here.
    OFF_CREATURE_CACHE_FAMILY = 0x1C,
    // Pointer to the CGUnit's 8-byte GUID at `*(CGUnit + 0x08)`. Verified
    // in `Script_GetInventoryItemLink` at `0x004C8CB0`-`0x004C8CB5`:
    // `mov eax, [esi+8]; mov edi, [eax]; mov ecx, [eax+4]` reads the
    // GUID's lo+hi dwords through this indirection. CGItem uses the same
    // offset for its instance block (also containing a GUID + itemID),
    // so the layout is consistent across CGObject subclasses — but the
    // contents differ per class.
    OFF_UNIT_GUID_PTR = 0x08,
    // UNIT_FIELD_FLAGS within m_objectFields. Bit 3 (`0x08`) is
    // `UNIT_FLAG_PLAYER_CONTROLLED`, which `Script_UnitPlayerControlled`
    // (`0x00516410`) tests via `mov eax, [m_objectFields + 0xA0];
    // shr eax, 3; test al, 1`.
    // CGUnit m_objectFields current/max stat offsets, **verified
    // empirically** on Turtle WoW (1.12.1) by `_classicapi_DescDump`
    // searching for the live `UnitMana` value at descriptor offsets:
    //
    //   +0x40  HEALTH       (current)
    //   +0x44  POWER1       (current mana — verified at multiple values)
    //   +0x48  POWER2       (current rage)
    //   +0x4C  POWER3       (current focus)
    //   +0x50  POWER4       (current energy)
    //   +0x54  POWER5       (current happiness)
    //   +0x58  MAXHEALTH    (= 807 at full HP in test data)
    //   +0x5C  MAXPOWER1    (max mana — stays at 1435 even when current = 443)
    //   +0x60..+0x6C  MAXPOWER2..5
    //   +0x70  LEVEL        (= 26 in test data)
    //   +0xA0  FLAGS        (verified separately via `Script_UnitPlayerControlled`)
    //
    // **The 1.12.1 layout is offset 0x18 (= 6 fields) earlier than the
    // CMaNGOS-documented vanilla layout** (which puts HEALTH at field
    // 0x16 = +0x58). My initial implementation read MAXHEALTH/MAXMANA
    // when I wanted current values — caused `IsUsableSpell` to falsely
    // return usable even when mana was below cost. Trust the binary
    // (and `_classicapi_DescDump` if you ever need to re-verify), not
    // external emulator field tables.
    OFF_UNIT_FIELD_HEALTH = 0x40,
    OFF_UNIT_FIELD_POWER1 = 0x44,
    // POWER1..5 are 4 bytes each, contiguous from +0x44:
    //   +0x44 mana / +0x48 rage / +0x4C focus / +0x50 energy / +0x54 happiness.
    OFF_UNIT_FIELD_MAXHEALTH = 0x58,
    OFF_UNIT_FIELD_MAXPOWER1 = 0x5C,
    // MAXPOWER1..5 are 4 bytes each, contiguous from +0x5C (same
    // mana/rage/focus/energy/happiness order as POWER1..5). Vanilla
    // 1.12 has 5 power types; the WotLK additions (Runes / Runic Power)
    // don't exist in this descriptor layout.

    // UNIT_MOD_CAST_SPEED — the cast-time multiplier float (1.0 = normal,
    // <1.0 = faster). The server folds it into `SpellEntry::GetCastTime`
    // (`castTime *= GetFloatValue(UNIT_MOD_CAST_SPEED)`); the client reads it
    // in `FUN_006e3340` at descriptor `+0x22c`. Backs `UnitSpellHaste`
    // (`haste% = (1/mult - 1) * 100`). nampower surfaces the raw float as its
    // `modCastSpeed` unit field.
    OFF_UNIT_MOD_CAST_SPEED = 0x22C,

    UNIT_POWER_MIN_TYPE = 0,
    UNIT_POWER_MAX_TYPE = 4, // happiness; types 5/6 only valid post-WotLK

    // Per-power-type display divisor. Raw descriptor values get
    // divided by this before they're surfaced through Lua. Vanilla
    // 1.12 stores rage as 0..1000 (internally) and divides by 10 to
    // show 0..100; happiness is stored at 1000x scale and divides
    // by 1000. Engine reads from `Script_UnitMana` at
    // `0x006E7130 + type * 4` (= `0x0086F978`). Same trick the
    // 3.3.5/4.x clients use; the table is per-client, the design is
    // stable.
    //
    //   [0] = 1     MANA
    //   [1] = 10    RAGE
    //   [2] = 1     FOCUS
    //   [3] = 1     ENERGY
    //   [4] = 1000  HAPPINESS
    VAR_UNIT_POWER_DIVISOR_TABLE = 0x0086F978,
    OFF_UNIT_FIELD_LEVEL = 0x70,
    // UNIT_FIELD_BYTES_0 dword at +0x78; byte 3 (= +0x7B) is the unit's
    // primary power type (one of UNIT_POWER_MIN_TYPE..MAX_TYPE).
    // Verified at `0x005179E6` in vanilla's `Script_UnitPowerType`:
    //   mov edx, [eax + 0x110]
    //   movzx eax, byte ptr [edx + 0x7B]
    OFF_UNIT_DESCRIPTOR_POWER_TYPE_BYTE = 0x7B,
    // UNIT_FIELD_STAT0..4 — the fully-computed (base+item+buff) primary
    // stats, broadcast in the descriptor. Order Str/Agi/Sta/Int/Spirit;
    // STAT4 (Spirit) = fieldIndex 0x94 → +0x250 (from the UpdateField name
    // table; cross-checked in-game: descriptor +0x250 read 173→182 when a
    // +9-Spirit item was equipped). What GetStat(STAT_SPIRIT) returns
    // server-side; used by GetSpellBonusHealing for the Spiritual-Guidance
    // (aura 175) spirit→healing conversion.
    OFF_UNIT_FIELD_STAT_SPIRIT = 0x250,
    // UNIT_FIELD_RESISTANCES[0] = armor (the fully-computed total), fieldIndex
    // 0x95 → +0x254 (7 resistances: armor, then the 6 magic schools). What
    // GetArmor() / GetResistance(SPELL_SCHOOL_NORMAL) returns; used by
    // GetSpellBonusHealing for Turtle's Ironclad (aura 199) armor→healing.
    OFF_UNIT_FIELD_RESISTANCE_ARMOR = 0x254,
    OFF_UNIT_FIELD_FLAGS = 0xA0,
    UNIT_FLAG_PLAYER_CONTROLLED = 0x08,
    // Bit 19 of UNIT_FIELD_FLAGS — `Script_UnitAffectingCombat` at
    // `0x00517E4A`-`0x517E5C` tests it via `mov eax, [fields+0xA0];
    // shr eax, 19; test al, 1`. We use it for `InCombatLockdown`,
    // which in 1.12 collapses to "is the local player in combat" — no
    // secure-frame system here, so the modern lockdown semantics
    // reduce to a plain combat flag check.
    UNIT_FLAG_IN_COMBAT = 0x00080000,
    // Bit 29 of UNIT_FIELD_FLAGS — set by the engine when the player is
    // feigning death (Hunter's `Feign Death`). Standard vanilla
    // `UNIT_FLAG_FEIGN_DEATH = 0x20000000` per emulator source. Tested
    // by `UnitIsFeignDeath(unit)` — works on any unit since
    // UNIT_FIELD_FLAGS is broadcast in object updates.
    UNIT_FLAG_FEIGN_DEATH = 0x20000000,
    // Bit 24 of UNIT_FIELD_FLAGS — set by the engine when a player is
    // possessed (priest's `Mind Control`, warlock's `Subjugate Demon`).
    // Standard vanilla `UNIT_FLAG_POSSESSED = 0x01000000` per emulator
    // source (CMaNGOS/TrinityCore). Read by `UnitIsPossessed(unit)`.
    UNIT_FLAG_POSSESSED = 0x01000000,

    // `UNIT_FIELD_CHANNEL_OBJECT` lo/hi (64-bit GUID of the object the
    // unit is currently channeling onto — bobber for fishing, lock for
    // lockpicking, etc.). CMaNGOS vanilla field 20 (= 0x50) minus the
    // 1.12.1 -0x18 unit-fields shift = 0x38. Verified empirically by
    // diffing the player descriptor during fishing: 0x38/0x3C went
    // `0 → bobber GUID` (high half `0xF110xxxx` = the vanilla
    // GameObject-GUID prefix), back to 0 on cast end.
    //
    // Note: not every channel populates this. The warlock's Ritual of
    // Summoning channel leaves it at 0 — that channel binds to the
    // participants via the spell, not via UNIT_CHANNEL_OBJECT.
    OFF_UNIT_FIELD_CHANNEL_OBJECT = 0x38,

    // `UNIT_FIELD_CHANNEL_SPELL` (u32 spell ID the unit is currently
    // channeling, or 0). CMaNGOS vanilla field 144 (= 0x240) minus the
    // -0x18 shift = 0x228. Verified empirically: the warlock's diff
    // shows `0x228 = 0 → 0x2BA` (698 = Ritual of Summoning) for the
    // duration of his channel; same value appears on every clicker
    // participating in the ritual; fishing populates it with 7620
    // (Fishing spell ID). Broadcast UpdateField, so it's readable for
    // any visible unit, not just the local player.
    OFF_UNIT_FIELD_CHANNEL_SPELL = 0x228,

    // Aura arrays in the unit's `m_objectFields` descriptor (at `unit
    // + OFF_CGUNIT_OBJECT_FIELDS`). 48 total auras packed as two
    // parallel sub-ranges (32 buffs, then 16 debuffs) sharing the
    // flags / levels / applications side-arrays indexed by absolute
    // slot (0..47). Derived by decompiling `Script_UnitBuff`
    // (`0x00519500`) and `Script_UnitDebuff` (`0x005198F0`):
    //
    //   Script_UnitBuff   iterates desc[+0xA4 .. +0x124)  step 4 (slots  0..31)
    //   Script_UnitDebuff iterates desc[+0x124 .. +0x164) step 4 (slots 32..47)
    //   Both read flag nibble at desc[+0x164 + slot/2] >> ((slot&1)*4) & 0xF
    //                and gate on `(nibble & 0x0E) != 0` (visibility)
    //   Both read stacks at desc[+0x1AC + slot] and display as `byte + 1`
    //
    // For our purposes we don't decode individual flag bits — we
    // distinguish helpful vs harmful by absolute slot range, and we
    // derive `dispelName` from `Spell.dbc[+0x10]` (see
    // `OFF_SPELL_DISPEL_TYPE` below), not from the flags nibble.
    OFF_UNIT_FIELD_AURA = 0xA4,                // u32 spell ID per slot, 48 slots total
    // UNIT_AURA event id for FUN_UNIT_EVENT_BROADCAST. The per-token unit
    // events use field-index == event-id; the aura field is index 0x29
    // (OFF_UNIT_FIELD_AURA 0xA4 >> 2), per Unit::TokenObserver's watched set.
    UNIT_EVENT_UNIT_AURA = 0x29,
    OFF_UNIT_FIELD_AURAFLAGS = 0x164,          // 4 bits per aura, 2 per byte, covers all 48
    OFF_UNIT_FIELD_AURALEVELS = 0x17C,         // u8 caster level per aura, 48 bytes
    OFF_UNIT_FIELD_AURAAPPLICATIONS = 0x1AC,   // u8 (stacks-1) per aura, display value = byte+1
    UNIT_AURA_BUFF_COUNT = 32,                 // slot range 0..31 (helpful)
    UNIT_AURA_DEBUFF_COUNT = 16,               // slot range 32..47 (harmful)
    UNIT_AURA_TOTAL = 48,
    UNIT_AURA_VISIBLE_MASK = 0x0E,             // nibble mask used by the engine's visibility gate

    // PLAYER_FIELD_MOD_DAMAGE_DONE_POS/NEG — the player's per-school spell
    // damage bonus. Offsets from the engine's own UpdateField name/index
    // table in `.rdata` (20-byte entries `{char* name, u32 fieldIndex, u32
    // count, ...}` at VA 0x83B6C8): POS = fieldIndex 0x3F5 count 7 → byte
    // offset fieldIndex*4 = 0xFD4; NEG = 0x3FC → 0xFF0. Seven int32 each,
    // one per school (0 physical .. 6 arcane); net bonus = POS − NEG.
    //
    // **Read these off `[player + OFF_CGPLAYER_INFO]` (the +0xE68 CGPlayer
    // sub-struct), NOT the broadcast descriptor at +0x110.** The descriptor
    // copy is never sent to the client and reads zero (verified in-game: the
    // whole post-skills PLAYER descriptor region is zero on a geared L60 —
    // vanilla never displayed spell power, so the server doesn't broadcast
    // it). The +0xE68 struct holds the client's fully-computed value
    // (gear + enchants + buffs + talents + sets baked in). Same base +
    // offsets nampower's GetSpellPower uses; verified 613 generic / 647 Fire
    // on a geared mage. Read by GetSpellBonusDamage in [src/spell/BonusDamage.cpp].
    // (No MOD_HEALING_DONE equivalent — vanilla lacks the field entirely;
    // GetSpellBonusHealing is derived instead.)
    OFF_PLAYER_FIELD_MOD_DAMAGE_DONE_POS = 0xFD4,
    OFF_PLAYER_FIELD_MOD_DAMAGE_DONE_NEG = 0xFF0,

    // Reusable "is this spell record a user-visible aura" predicate.
    // `__fastcall(spellRecord*) -> bool`. Checks Spell.dbc Attributes
    // (high bit of byte at +0x18 must be clear), AttributesEx bit
    // 0x10000000 must be clear, has a non-trivial effect (effects at
    // +0x16C..+0x174 must contain at least one not in {0x2C, 0x2D,
    // 0x97}), and a mechanic-vs-player-immunity-bitmap gate via
    // `+0x2B0`. Both `Script_UnitBuff` and `Script_UnitDebuff` call
    // it to filter their aura iteration; we call it the same way.
    FUN_SPELL_IS_VISIBLE_AURA = 0x00519860,

    // Player-only aura timing tables. Unlike UNIT_FIELD_AURA (which is
    // populated for any unit but has no timing info), these are the
    // local player's own buffs/debuffs with full duration data — the
    // server broadcasts cast/duration only for the player's own auras.
    //
    // VAR_PLAYER_BUFF_TABLE: 48-entry array, 16 bytes each.
    //   +0x00 (int)   slotCode: -1 = empty; 0..31 = buff entry;
    //                 32..47 = debuff entry. Doubles as the index
    //                 into the expiration table.
    //   +0x04 (int)   spellID — verified via `Script_GetPlayerBuffTexture`
    //                 at `0x004E4740`, which reads `[entry+0x4]` and
    //                 bounds-checks against `[VAR_SPELL_RECORD_COUNT]`.
    //   +0x0A (byte)  flags: bit 0x1 = cancelable
    //   +0x0C (int)   untalented rank — the value `GetPlayerBuff`
    //                 returns as its second result. Not the spellID.
    //
    // VAR_PLAYER_BUFF_EXPIRATION_TABLE: parallel `int[]` of absolute
    // expiration timestamps in ms. Same epoch as `FUN_OS_TICKCOUNT_MS`
    // and (verified) Lua's `GetTime()` (which multiplies tickcount by
    // 0.001) — so converting `expirationMs * 0.001` gives an absolute
    // seconds-value that's directly comparable to `GetTime()` on the
    // Lua side. No epoch reconciliation needed.
    //
    // Sourced from `Script_GetPlayerBuff` (`0x004E45D0`) /
    // `Script_GetPlayerBuffTimeLeft` (`0x004E4930`); the
    // `FUN_004E4450(buffID)` inner helper does the
    // `expirationTable[entry.slotCode] - currentMs` math we mirror.
    VAR_PLAYER_BUFF_TABLE = 0x00BC6040,
    VAR_PLAYER_BUFF_EXPIRATION_TABLE = 0x00BC5F68,
    PLAYER_BUFF_TABLE_COUNT = 48,
    PLAYER_BUFF_ENTRY_STRIDE = 16,
    OFF_PLAYER_BUFF_SLOT_CODE = 0x00,
    OFF_PLAYER_BUFF_SPELL_ID = 0x04,

    // CMSG_CANCEL_AURA sender — `__fastcall(int spellID)`. Builds an
    // opcode-0x136 packet (`{opcode=0x136, spellID}`) and ships it.
    // Used directly by `CancelSpellByID` / `CancelSpellByName` to
    // skip `Script_CancelPlayerBuff`'s client-side gates (the
    // per-entry cancelable flag at `[+0x0A]` and the fallback
    // AttributesEx `0x04` check) and let the server be the source
    // of truth for what's cancelable.
    //
    // **Crash hazard**: bounds-checks `spellID` against Spell.dbc
    // count and sets the record pointer to NULL on OOR, then
    // unconditionally dereferences `[record + 0x1C]`. Callers MUST
    // pre-validate via `Spell::Lookup::RecordForID(spellID)` (which
    // also catches empty record slots) before calling.
    FUN_CANCEL_AURA_SEND = 0x006E7040,

    // `GetTickCount`-style millisecond counter the engine uses as its
    // time source. Same value Lua's `GetTime()` reads (scaled by
    // 0.001 to seconds). `__fastcall void → uint32_t` (no args, ms
    // tick count in EAX).
    FUN_OS_TICKCOUNT_MS = 0x0042B790,

    // Effective cast time (ms) for a spell, fully resolved:
    // `__fastcall uint32_t(int spellID, int unit /*0 = local player*/,
    // int flag /*0 = clamp negatives to 0*/)`. Computes
    // SpellCastTimes base + level scaling, applies the cast-time
    // SpellMod (op 10, via FUN_006e6af0), then multiplies by the
    // caster's cast-speed/haste mod (descriptor +0x22c). This is the
    // exact helper the engine's own cast-start path (FUN_006ec910)
    // uses for the cast-bar end time, so reading it keeps our
    // UnitCastingInfo end time in lockstep with the engine. 0 for
    // instant / no-cast-time spells.
    FUN_GET_CAST_TIME = 0x006E3340,

    // Effective power cost (mana/rage/energy/…) for a spell:
    // `__fastcall uint32_t(int spellID, int unit /*0 = local player*/)`.
    // Computes ManaCost base + per-level + ManaCostPercent-of-resource,
    // applies the descriptor power-cost modifiers, then the cost
    // SpellMod (op 0xE / 14). 0xFFFFFFFF on invalid input / no player.
    // Read by C_Spell.GetSpellPowerCost (the full helper, like the cast
    // time one — beats replicating op 14 via Spell::Mod since it also
    // folds in the descriptor cost mods).
    FUN_GET_SPELL_COST = 0x006E31B0,

    // Full spell-castability check for the LOCAL player:
    // `char __fastcall(const uint8_t *spellRecord /*ecx*/,
    //                  int *outNoMana /*edx*/)`.
    // Resolves the local player internally (no unit arg), then gates
    // on casting state, stun/confuse flags, mechanic immunities,
    // shapeshift/form + required stances, aura-state requirements,
    // combo points (finishers), spell RequiredSkill, and finally the
    // power check: `cost(FUN_GET_SPELL_COST) <= currentPower` → usable,
    // else sets `*outNoMana = 1`. Returns nonzero (low byte) when
    // usable. Does NOT check spell knowledge — so it's valid to feed
    // an item's on-use spell record (which the player never "knows").
    // This is the helper the action-usability recompute uses for
    // player spell slots; `Item::Usable` reuses it for item on-use
    // spells. Returns 0 cleanly pre-world (no player).
    FUN_SPELL_IS_USABLE = 0x006E3D60,

    // Effective spell/channel duration (ms):
    // `__fastcall int(const void *spellRecord, int unit /*0 = player,
    // nonzero = pet*/, int skipMod /*0 = apply mods*/)`. SpellDuration
    // base + level scaling (capped at the row's max), then the duration
    // SpellMod (op 1). Same `unit` selector as the cast-time/cost helpers.
    // Used for UnitChannelInfo's channel end time. Note: takes the spell
    // RECORD pointer, not a spellID.
    FUN_GET_SPELL_DURATION = 0x006EA000,

    // Pure spell range check against a target:
    // `char __fastcall(void *caster /*ecx*/, void *target /*edx*/,
    //                  int spellID, char *outMinMaxFlag)`.
    // Computes the spell's min/max range via `FUN_006e3480` (which folds
    // in the target's bounding radius), reads both objects' world
    // positions (GetPosition, vtable+0x14), and compares center-to-center
    // squared distance. Returns low byte 1 = in range, 0 = out (too close
    // OR too far); `*outMinMaxFlag` is set to 1 on a max-range (too far)
    // failure so the caller can pick a "too close" vs "out of range" error
    // string — we ignore it. Both objects must be non-null (it dereferences
    // the GetPosition result without a null check). This is the geometric
    // core the action-button range coloring uses: `Script_IsActionInRange`
    // (`0x004E7550`) → `FUN_004E56F0` → `FUN_006E4440` → here. We call it
    // directly for `C_Spell.IsSpellInRange` rather than `FUN_006E4440`,
    // which additionally gates on target type and folds "range N/A" into
    // the same truthy return as "in range" (so it can't yield true/false/
    // nil cleanly).
    FUN_SPELL_RANGE_CHECK = 0x006E47B0,

    // Spell.dbc `m_durationIndex` field — pointer into SpellDuration.dbc.
    // Verified via `FUN_004E44B0` (`0x004e44b0`) and `FUN_006EA000`
    // (`0x006ea000`), both of which read `[spellRec + 0x78]` and use
    // the result as a SpellDuration.dbc index.
    OFF_SPELL_DURATION_INDEX = 0x78,
    // Spell.dbc `m_baseLevel` — the spell level used as the anchor in
    // `(effLevel - baseLevel) * perLevel + base` scaling. Same offset
    // `0x70` as CGUnit's UNIT_FIELD_LEVEL but in a different struct.
    OFF_SPELL_BASE_LEVEL = 0x70,

    // SpellDuration.dbc — class instance at `0x00C0D820`, records at
    // `0x00C0D828`, count at `0x00C0D82C`. Record layout matches the
    // SpellCastTimes.dbc shape:
    //   +0x00 (int)  id
    //   +0x04 (int)  base duration ms (signed; negative + perLevel=0
    //                means "no real duration" / infinite aura)
    //   +0x08 (int)  per-level duration scaling ms
    //   +0x0C (int)  max duration ms (cap)
    VAR_SPELLDURATION_RECORDS = 0x00C0D828,
    VAR_SPELLDURATION_COUNT = 0x00C0D82C,
    OFF_SPELLDURATION_BASE_MS = 0x04,
    OFF_SPELLDURATION_PER_LEVEL_MS = 0x08,
    OFF_SPELLDURATION_MAX_MS = 0x0C,

    // `SMSG_SPELL_GO` inner handler — `void __fastcall(uint64_t *itemGUID,
    // uint64_t *casterGUID, uint32_t spellId, CDataStore *packet)`. The
    // engine has already decoded item/caster/spell into args; the packet
    // read cursor sits at the post-header body (castFlags, hit/missed
    // target lists, target mask). This is the ONLY place the client sees
    // an aura's *caster* — the unit aura array stores spell IDs but never
    // who applied them. `Aura::Source` co-hooks this (alongside nampower,
    // which detours the same function) to cache `(targetGuid, spellId) →
    // casterGuid` for `C_UnitAuras` sourceUnit + non-player expiration.
    // Packet layout mirrored from nampower's SpellGoHook (verified on the
    // same Octo/Turtle 1.12.1 client). Per-cast frequency, not per-frame.
    FUN_SPELL_GO = 0x006E7A70,

    // SMSG packet handler that processes `SMSG_SPELL_START` (opcode
    // `0x131`) among others — `int __fastcall(uint32_t unk, uint32_t
    // opCode, uint32_t unk2, CDataStore *packet)`. For `0x131` the body is
    // `itemGuid(packed), casterGuid(packed), spellId(u32), castFlags(u16),
    // castTime(u32), targetMask, …`. This is the only place the client sees
    // *another* unit's cast with a server-authoritative cast time, so
    // `Spell::Cast` co-hooks it (gated on `opCode == 0x131`) to back
    // remote-unit `UnitCastingInfo` / `UnitChannelInfo`. Mirrored from
    // nampower's SpellStartHandlerHook.
    FUN_SPELL_START_HANDLER = 0x006E7640,

    // `SMSG_SPELL_DELAYED` handler — `int __stdcall(uint32_t *opCode,
    // CDataStore *packet)`. Body: `guid(u64), delayMs(u32)`. The server
    // only sends this to the affected caster — empirically confirmed on
    // Turtle (melee-pushback test: only the victim's client received it) —
    // so remote units' pushback is NOT recoverable client-side; only the
    // local player's cast pushback is trackable. `Spell::Cast` co-hooks it
    // to extend the tracked cast's end time so the bar reflects pushback.
    // Mirrored from nampower's SpellDelayedHook.
    FUN_SPELL_DELAYED = 0x006E74F0,

    // `MSG_CHANNEL_START` handler — `int __stdcall(uint32_t *opCode,
    // CDataStore *packet)`. Body: `spellId(u32), durationMs(u32)`. Sent
    // only to the caster when their channel begins, with the server-
    // authoritative duration. `Spell::Cast` co-hooks it to stamp the
    // player's channel — required for cast-then-channel spells (Mind
    // Control: 3 s cast then 60 s channel), whose SMSG_SPELL_START only
    // covers the cast phase. Address + layout mirrored from nampower's
    // SpellChannelStartHandlerHook (they hook it too — accepted co-hook
    // collision, per-channel-start frequency).
    FUN_SPELL_CHANNEL_START = 0x006E7550,

    // `MSG_CHANNEL_UPDATE` handler (`FUN_006e75f0`) — same `int __stdcall(
    // uint32_t *opCode, CDataStore *packet)` shape. Body: a single u32 =
    // the channel's new REMAINING time (ms). Sent to the caster on damage
    // pushback (which shortens a vanilla channel) and once more at the
    // channel's end (remaining == 0). Verified at 0x006e75f0: reads the u32,
    // then fires vanilla's `SPELLCAST_CHANNEL_UPDATE(remaining)` (event
    // 0x157) on pushback or `SPELLCAST_CHANNEL_STOP` (0x158) at end — but
    // stores the new end NOWHERE, so `Spell::Cast`'s g_channel.endMs
    // (computed once at start) never reflects pushback. Co-hooked to
    // re-anchor endMs (and fire UNIT_SPELLCAST_CHANNEL_UPDATE). nampower
    // hooks it too (SpellChannelUpdateHandlerHook — accepted co-hook, per-
    // pushback frequency, same as the START sibling).
    FUN_SPELL_CHANNEL_UPDATE = 0x006E75F0,

    // `SMSG_SPELL_FAILED_OTHER` handler — `int __stdcall(uint32_t *opCode,
    // CDataStore *packet)`, same shape as FUN_SPELL_DELAYED. Body:
    // `casterGuid(u64, plain), spellId(u32)`. In (v)mangos cores this is
    // broadcast to observers when a started cast aborts (interrupt, death,
    // movement, fizzle) — but Turtle's core does NOT send it (verified
    // in-game: interrupts arrive with neither this nor SMSG_SPELL_FAILURE;
    // observers learn of the abort via SuperWoW instead — see
    // FUN_UNIT_CLEAR_CASTING_SPELL). `Spell::Cast` co-hooks it anyway as a
    // backstop for servers that do send it, invalidating the matching
    // remote-cast cache entry (and the player's own server-stamped chained
    // recast). Address + packet layout mirrored from nampower's
    // SpellFailedOtherHandlerHook.
    FUN_SPELL_FAILED_OTHER = 0x006E8E40,

    // `SMSG_SPELL_FAILURE` handler — same `int __stdcall(uint32_t *opCode,
    // CDataStore *packet)` shape. Body: `casterGuid(u64, plain),
    // spellId(u32), result(u8)` — a superset of FAILED_OTHER's with a
    // trailing result byte. Decompiled at 0x006E8D80: resolves the caster
    // unit (FUN_00468460 typemask 8) and stops its cast visual via the SAME
    // unit methods FAILED_OTHER's handler calls (FUN_0060d040 +
    // FUN_00614150), plus extra channel/visual cleanup. Servers derived
    // from (v)mangos broadcast BOTH packets from Spell::SendInterrupted,
    // but which one a given core actually emits per abort path varies —
    // `Spell::Cast` co-hooks this one alongside FAILED_OTHER so a remote
    // interrupt evicts the cast cache regardless of which sibling arrives.
    // nampower calls this SpellFailedHandler (offsets.hpp) but doesn't
    // hook it.
    FUN_SPELL_FAILURE = 0x006E8D80,

    // `Spell_C_SpellFailed` — the CLIENT-side cast-failure entry, distinct
    // from the two SMSG_SPELL_FAILURE/_OTHER packet handlers above. Fires
    // for the local player's own cast rejections (out of range, no mana,
    // "spell not ready", LoS, interrupted, …). `__fastcall(uint32_t
    // spellId, uint8_t spellResult /*edx*/, int unk1, int unk2, bool
    // failedByServer)` — signature per nampower's Spell_C_SpellFailedT.
    // Backs `UNIT_SPELLCAST_FAILED` (`Spell::CastEvents`). Not hooked
    // elsewhere in this project (we hook the packet handlers, not this).
    FUN_SPELL_C_SPELL_FAILED = 0x006E1A00,

    // `Spell_C_SpellFailed` result code for a fake/suppressed failure
    // (Unleashed Potential and similar) — the engine's SPELL_FAILED_DONT_
    // REPORT. nampower filters it out of its failure event; so do we.
    SPELL_FAILED_DONT_REPORT = 23,

    // `CGUnit_C::ClearCastingSpell` — `__thiscall void(CGUnit *unit,
    // int spellID, char notify, char cleanup)` (nampower names the offset
    // but never hooks it). THE engine choke point for "this unit stopped
    // casting": gated on `spellID == [unit+0xC8C]` (the unit's live
    // current-cast spellID, set by SMSG_SPELL_START processing), it zeros
    // that field and clears the casting flag (bit 0x400 at [unit+0xD58]).
    // Ten callers converge here: both failure-packet handlers, the
    // SPELL_GO handler (natural completion), the movement handler
    // (0x006018f0 — moving units aren't casting), the health-diff handler
    // (0x006046f0, death), the unit animation-event dispatcher
    // (0x0060e0a0), the local cancel path (0x006e7040, sends
    // CMSG_CANCEL_CAST), and Spell_C_SpellFailed. Hooking THIS instead of
    // individual packets makes remote-cast eviction independent of which
    // packet the server actually sends — load-bearing on Turtle, whose
    // core does NOT broadcast SMSG_SPELL_FAILURE / SMSG_SPELL_FAILED_OTHER
    // on interrupt (verified: neither handler fires while the victim's
    // cast visual demonstrably stops).
    FUN_UNIT_CLEAR_CASTING_SPELL = 0x0060D040,

    // CGUnit field: the unit's current-cast spellID (0 = not casting).
    // The regular-cast analog of OFF_UNIT_FIELD_CHANNEL_SPELL — but a
    // plain CGUnit member, NOT a descriptor UpdateField (client-derived
    // from SMSG_SPELL_START, not server-broadcast state).
    OFF_UNIT_CAST_SPELL = 0xC8C,

    // NetClient message dispatch — `__thiscall void(void *conn, uint32_t
    // arg, CDataStore *packet)`. Reads the u16 opcode off the packet
    // cursor, then invokes `[conn + opcode*4 + 0x74]` (the handler table
    // FUN_005ab650 / FUN_00537a60 register into; per-handler userParam
    // parallel table at +0xD64). EVERY server message the client processes
    // funnels through here (the raw-ingress sibling at 0x00537B10 only
    // special-cases 0x1DD compressed-moves before queueing into this).
    // Useful as a temporary whole-protocol sniff point: co-hook, peek the
    // u16 at the cursor, restore, log. `Net::PacketDispatch` co-hooks it as
    // the single fan-out point for incoming-packet subscribers — the
    // collision-avoiding alternative to each feature MinHook-ing its
    // per-opcode SMSG handler (which nampower/SuperWoW also hook). nampower
    // does NOT hook this funnel (it detours the per-opcode handlers), so
    // moving our parsing here removes those shared prologues.
    FUN_NET_MESSAGE_DISPATCH = 0x00537AA0,

    // Incoming spell-subsystem SMSG opcodes — the u16 message ids the engine
    // dispatch table (see FUN_006e7150) maps to per-opcode handlers. Used by
    // the `Net::PacketDispatch` subscribers that replaced the per-handler
    // co-hooks. Verified from the registrar: 0x131/0x132 both route to
    // FUN_006E7640 (it branches on the opcode; the 0x132 path decodes and
    // calls the SpellGo builder FUN_006E7A70).
    SMSG_SPELL_START = 0x131,
    SMSG_SPELL_GO = 0x132,
    SMSG_SPELL_FAILURE = 0x133,
    SMSG_SPELL_COOLDOWN = 0x134,
    SMSG_SPELL_CHANNEL_START = 0x139,
    SMSG_SPELL_CHANNEL_UPDATE = 0x13A,
    SMSG_SPELL_DELAYED = 0x1E2,
    SMSG_SPELL_FAILED_OTHER = 0x2A6,

    // NetClient send — `__thiscall void(void *conn, CDataStore *packet)`.
    // The outgoing counterpart of FUN_NET_MESSAGE_DISPATCH: every CMSG the
    // client sends funnels through it (the argless wrapper `FUN_005AB630`,
    // nampower's ClientServices_Send, resolves the connection singleton
    // with the packet in ECX and calls this). The packet's leading u32 is
    // the opcode; `m_size - m_read` is what goes on the wire.
    // `Aura::ComboDuration` co-hooks it to snapshot combo points the
    // moment CMSG_CAST_SPELL leaves — before the server consumes them —
    // which also covers nampower-queued casts (they re-enter the engine's
    // cast path and send normally, unlike a Spell_C_CastSpell co-hook,
    // which nampower's trampoline re-entry would bypass).
    FUN_NET_SEND = 0x005379A0,

    // Outgoing opcode: CMSG_CAST_SPELL. Body: `spellId(u32),
    // targetFlags(u16), <target per flags>`.
    OP_CMSG_CAST_SPELL = 0x12E,

    // Spell.dbc DurationIndex (column 30) — row into SpellDuration.dbc.
    // Offset = 30*4, consistent with the verified neighbors
    // (CastingTimeIndex col 18 = +0x48, PowerType col 31 = +0x7C).
    OFF_SPELL_RECORD_DURATION_INDEX = 0x78,

    // SpellDuration.dbc — records/count slots per docs/DBCs.md. Record:
    // `{id@+0, baseMs@+4 (i32), perLevelMs@+8 (i32), maxMs@+0xC (i32)}`.
    // Combo-point finishers are exactly the rows with base != max: the
    // server computes `base + (max - base) * comboPoints / 5`
    // ((v)mangos CalculateSpellDuration — verified against the DBC:
    // Rupture 6000/16000 = 6+2*CP, Kidney Shot 1000/6000 = 1+1*CP,
    // Slice and Dice 6000/21000 = 6+3*CP, Expose Armor 30000/30000 =
    // fixed). NOTE: rows can dangle — Turtle's client DBC has no record
    // for Rip's DurationIndex 87 (records[87] == NULL), which is why
    // Rip reads duration 0 client-side there.
    VAR_SPELL_DURATION_RECORDS = 0x00C0D828,
    VAR_SPELL_DURATION_COUNT = 0x00C0D82C,

    // Combo points, within the CGPlayer `+0xE68` sub-struct (see
    // OFF_CGPLAYER_INFO). Verified from Script_GetComboPoints
    // (0x0051A190): CP count byte at `[player+0xE68]+0x1029`, the combo
    // TARGET's guid (u64) at `[player+0xE68]+0x838` (compared against
    // the current-target global 0x00B4E2D8 before the Lua function
    // reports a non-zero count).
    OFF_CGPLAYER_COMBO_POINTS = 0x1029,
    OFF_CGPLAYER_COMBO_TARGET = 0x838,

    // ---- Frame-method registries beyond GameTooltip's ------------------
    // Same per-type registry mechanism as VAR_GAMETOOLTIP_METHOD_REGISTRY
    // (docs/BlizzardScriptAPI.md "Frame types & their methods" has the
    // full 23-table catalogue). The dispatcher resolves methods through
    // the type hierarchy, so a method added to the REGION base registry
    // (the 19-entry core-layout table: GetWidth/SetPoint/…) is callable
    // on every region type — frames, buttons, textures, fontstrings.
    // `Frame::Modern` uses these to backport modern API (SetSize,
    // SetShown, SetResizeBounds).
    VAR_REGION_METHOD_REGISTRY = 0x00CF54B4,
    VAR_FRAME_METHOD_REGISTRY = 0x00CF4D38,
    VAR_TEXTURE_METHOD_REGISTRY = 0x00CF5434,
    VAR_FONTSTRING_METHOD_REGISTRY = 0x00CF5400,

    // Model frame — backs `Model:SetDisplayInfo(creatureDisplayID)`
    // (`Model::DisplayInfo`). Registry ctx per docs/raw_methods.txt (table
    // 0x00878948, 23 methods: SetModel/SetSequence/ReplaceIconTexture/…).
    VAR_MODEL_METHOD_REGISTRY = 0x00CF0C8C,
    // Model frame-script type id, lazily assigned by the engine on the first
    // Model method call (`Script_SetModel` FUN_0076d950 prologue:
    // `if (id == 0) id = ++counter`). Mirror the lazy-assign so a Model
    // type-check works even before any stock Model method has run; the IsA
    // check itself is the CFrameScriptObject vmethod at vtable+0x10, shared by
    // every frame type.
    VAR_MODEL_LUA_TYPE_ID = 0x00CF0C5C,
    VAR_FRAMESCRIPT_TYPE_ID_COUNTER = 0x00CEEF6C,
    // FUN_0076cfe0(model /*ecx*/, int replaceableType, const char *path) —
    // loads `path` as a texture and binds it to the model's replaceable-texture
    // slot `replaceableType`. Worker behind `Model:ReplaceIconTexture` (which
    // passes type 14); creature skins use MONSTER_1/2/3 = 11/12/13.
    FUN_MODEL_SET_REPLACEABLE_TEXTURE = 0x0076CFE0,
    // The loaded model instance a CSimpleModelFFX keeps at +0x318. It is the
    // SAME CModel class CGUnits keep at unit+0xD8 — verified via
    // FUN_007110d0 (the geoset-range setter), whose submesh walk and +0x98
    // visibility-array writes match the instance layout probed in-game.
    OFF_SIMPLEMODELFFX_MODEL_INSTANCE = 0x318,

    // --- Character-model compositor ("CharComponent", `Model::DisplayInfo`) --
    // The engine object that dresses a character-based model: resolves the
    // hair / facial-hair geosets (CharHairGeosets, CharacterFacialHairStyles —
    // beards, earrings, teeth), loads the baked NPC body texture or composites
    // the player one, and applies equipment geosets (sleeves / robe→trousers /
    // boots / tabard / cape, FUN_00477520). Units keep one at unit+0xD30,
    // driven by CGUnit_C::UpdateCharacterCustomization (FUN_005fb200) — the
    // single caller-of-record this whole API was derived from — behind the
    // poll gate FUN_00607da0 (dress only once FUN_MODEL_INSTANCE_LOADED says
    // the model is in). The portrait renderer (Script_SetPortraitTexture →
    // FUN_00524f60) waits on FUN_CHARCOMP_PUMP before rendering, which is why
    // portraits always show the finished appearance.
    FUN_CHARCOMP_CREATE = 0x00475FB0,  // () -> builder from the engine pool at 0x00B42720; 0 on exhaustion
    FUN_CHARCOMP_DESTROY = 0x00476000, // (builder /*ecx*/) — releases refs (FUN_00476ac0), returns node to pool
    // __thiscall(builder, CharCompInfo*) -> bool. Copies 0x5B dwords
    // (0x16C bytes) to builder+0x18, validates race against ChrRaces, resolves
    // skin/hair/facial sections + geosets. Caller must AddRef the model first
    // (FUN_005fb200 does); the builder owns that ref from then on.
    FUN_CHARCOMP_SET_INFO = 0x00476B90,
    // __thiscall(builder, slot 0..9, itemDisplayInfoID) — feed one NPC
    // equipment piece (validated against ItemDisplayInfo.dbc, no-op for 0 /
    // out-of-range). Units pass CreatureDisplayInfoExtra +0x20..+0x44.
    FUN_CHARCOMP_SET_ITEM = 0x00478AA0,
    // __thiscall(builder, int *unused /*engine passes 0*/) -> bool done.
    // The per-frame pump: composites pending texture regions; the baked-NPC
    // path short-circuits to the geoset apply (FUN_00477520) and returns 1 on
    // the first call. Keep calling until it returns 1.
    FUN_CHARCOMP_PUMP = 0x00477860,
    // Model-instance API used around the compositor:
    FUN_MODEL_INSTANCE_ADDREF = 0x00710390, // (model /*ecx*/)
    // __thiscall(model, tryLoad, recurseChildren) -> bool loaded. The engine's
    // dress gate (FUN_00607da0) passes (0, 0) — a passive check.
    FUN_MODEL_INSTANCE_LOADED = 0x007103D0,
    // Attached-child list on a model instance (helm/shoulder item models the
    // compositor attaches via FUN_00712f70). Head + sibling-next verified from
    // FUN_MODEL_INSTANCE_LOADED's recurseChildren walk.
    OFF_MODEL_INSTANCE_CHILD_HEAD = 0x1DC,
    OFF_MODEL_INSTANCE_CHILD_NEXT = 0x1E4,
    // __thiscall(model, callback, userData) — stores the instance's pre-render
    // callback pair (instance+0x3BC/+0x3C0). CSimpleModelFFX::SetModelInstance
    // (FUN_0076cd30) registers FUN_SIMPLEMODELFFX_LIGHT_FOG_CB with the frame
    // as userData on ITS instance only; attached children have an empty slot
    // and render UNLIT (pure black — verified in-game: a light tint colored
    // the body but not the equipment). `Model::DisplayInfo` fills each child's
    // slot with the same pair so equipment lights like the body.
    FUN_MODEL_INSTANCE_SET_RENDER_CB = 0x007134B0,
    // __fastcall-shaped engine callback (never called by us directly): applies
    // the owning frame's fog (+0x3A4/+0x3A8) and light struct (+0x324, written
    // by Script_SetLight via FUN_0076cf30) to the render context each time the
    // instance is drawn.
    FUN_SIMPLEMODELFFX_LIGHT_FOG_CB = 0x0076D680,

    // Engine Script_* method implementations `Frame::Modern` delegates to
    // (each `int __fastcall(void *L)`, reading self at stack index 1).
    // Addresses from the docs/raw_methods.txt catalogue. Show/Hide are
    // PER-BRANCH (Frame vs Texture vs FontString each register their own
    // script function with its own object typecheck), so SetShown is
    // registered per branch with the matching delegates.
    FUN_SCRIPT_REGION_SETWIDTH = 0x007A1F20,
    FUN_SCRIPT_REGION_SETHEIGHT = 0x007A2150,
    FUN_SCRIPT_REGION_GETWIDTH = 0x007A1E00,
    FUN_SCRIPT_REGION_GETHEIGHT = 0x007A2030,
    // Region rect getters (return a number, or nil when the region has no
    // resolved rect). All on the REGION base registry → callable on any
    // region type. Used by Frame::Modern's IsMouseOver.
    FUN_SCRIPT_REGION_GETLEFT = 0x007A1980,
    FUN_SCRIPT_REGION_GETRIGHT = 0x007A1AA0,
    FUN_SCRIPT_REGION_GETTOP = 0x007A1BC0,
    FUN_SCRIPT_REGION_GETBOTTOM = 0x007A1CE0,
    // In-game GetCursorPosition (0x0046DAD0 is the glue-state copy). Pushes
    // (x, y) in the same k = FUN_0041ad70()*DAT_007ffd68 space the region
    // rect getters use — so dividing by the region's effective scale puts
    // the cursor in the region's logical coordinate space.
    FUN_SCRIPT_GETCURSORPOSITION = 0x0048B820,
    // Region effective-scale field: `piVar[0x1f]` (float) read by both
    // GetEffectiveScale (0x00774FC0) and internally by GetLeft/Top/etc. —
    // present on every region object, so it's the all-region way to get the
    // effective scale without the Frame-only GetEffectiveScale method.
    OFF_REGION_EFFECTIVE_SCALE = 0x7C,

    // Global UI/screen context pointer (every frame's +0xA0 field mirrors
    // it; GetCursorPosition reads the cursor from *ptr + 0x1118). Its
    // +0xCFC slot holds the frame currently being moved/sized via
    // StartMoving/StartSizing (0 when idle) — the drag target. Backs
    // Frame::Modern's IsDragging: StopMovingOrSizing (0x00776990) gates its
    // clear on `*(context+0xCFC) == self`, i.e. the IsDragging predicate.
    VAR_UI_CONTEXT_PTR = 0x00CF0BD8,
    OFF_UI_CONTEXT_DRAG_TARGET = 0xCFC,
    // Frame currently under the mouse (the CFrameScriptObject* that
    // `GetMouseFocus` returns): `*(*VAR_UI_CONTEXT_PTR + 0x7C)`. 0 when the
    // cursor is over no mouse-enabled frame. `Frame::Attributes` polls this to
    // drive the `mouseover` override from the hovered frame's `unit` attribute.
    OFF_UI_CONTEXT_MOUSE_FOCUS = 0x7C,
    // Head of the engine's frame list (`*(*VAR_UI_CONTEXT_PTR + 0xCC4)`),
    // and each frame CObject's next-in-enumeration pointer at +0x308 — the
    // list `Script_EnumerateFrames` (0x00705F60) walks. A node with its low
    // bit set (or null) is the end sentinel. `Frame::MouseFoci` walks it to
    // build the GetMouseFoci stack.
    OFF_UI_CONTEXT_FRAME_LIST_HEAD = 0xCC4,
    OFF_FRAME_ENUM_NEXT = 0x308,
    // Frame level, a plain int at CObject +0xC4 (the value `Script_GetFrameLevel`
    // at 0x007744A0 pushes as `frame[0x31]`). Read directly for stack ordering.
    OFF_FRAME_LEVEL = 0xC4,
    FUN_SCRIPT_FRAME_SHOW = 0x00775750,
    FUN_SCRIPT_FRAME_HIDE = 0x00775810,
    FUN_SCRIPT_FRAME_SETMINRESIZE = 0x00776020,
    FUN_SCRIPT_FRAME_SETMAXRESIZE = 0x007762A0,
    FUN_SCRIPT_FRAME_GETSCRIPT = 0x00774780,
    FUN_SCRIPT_FRAME_SETSCRIPT = 0x007748D0,
    // `frame:EnableMouse(enable)` (Frame registry) — `Frame::Attributes` calls
    // it so a unit-attributed frame registers as the mouse-focus (bare frames
    // otherwise never hover). Standard `int __fastcall(void *L)` Script_* shape.
    FUN_SCRIPT_FRAME_ENABLEMOUSE = 0x00777070,
    // Frame predicates used by `Frame::MouseFoci` to filter the enumeration to
    // the interactive stack (visible + mouse-enabled). Standard Script_* shape
    // `int __fastcall(void *L)`, self at Lua idx 1, result pushed at idx 2.
    FUN_SCRIPT_FRAME_IS_VISIBLE = 0x007758D0,
    FUN_SCRIPT_FRAME_IS_MOUSE_ENABLED = 0x00777130,
    // `frame:GetFrameStrata()` — pushes the strata name string; ranked for
    // stack ordering (`Frame::MouseFoci`).
    FUN_SCRIPT_FRAME_GET_STRATA = 0x007742A0,
    // Button OnClick dispatcher — `__thiscall(button, buttonCode)` at
    // 0x00779540, invoking the button's OnClick slot `[button+0x4CC]`. NOTE: do
    // NOT MinHook it — SuperWoW's click-casting inline-hooks the same prologue
    // and a second hook corrupts the trampoline (ERROR #132). `Frame::Attributes`
    // instead installs a normal chained OnClick on the opted-in frame. Kept as
    // the verified dispatch reference only.
    // Frame GetAlpha (own alpha, 0..1) + Region GetParent — walked by
    // Frame::Modern's GetEffectiveAlpha up the parent chain.
    FUN_SCRIPT_FRAME_GETALPHA = 0x00774DC0,
    FUN_SCRIPT_REGION_GETPARENT = 0x007A1460,
    FUN_SCRIPT_TEXTURE_SHOW = 0x0079B770,
    FUN_SCRIPT_TEXTURE_HIDE = 0x0079B830,
    FUN_SCRIPT_FONTSTRING_SHOW = 0x0079CDB0,
    FUN_SCRIPT_FONTSTRING_HIDE = 0x0079CE70,

    // ---- World map overlay data (WorldMapOverlay.dbc + WorldMapArea.dbc)
    // Backs `C_Map.GetMapOverlays`. Instance VAs per docs/DBCs.md;
    // schemas verified against the extracted DBCs AND the engine's own
    // reader `Script_GetMapOverlayInfo` (0x004A8A00), which reads the
    // same fields for its 7 returns.
    //
    // WorldMapArea.dbc record (8 fields, 0x20 bytes):
    //   {ID@+0, mapID@+4, areaID@+8 (AreaTable, 0 for continent rows),
    //    name@+0xC (char*, the map directory name GetMapInfo returns),
    //    locLeft/locRight/locTop/locBottom floats}
    VAR_WORLDMAP_AREA_RECORDS = 0x00C0D5BC,
    VAR_WORLDMAP_AREA_COUNT = 0x00C0D5C0,
    OFF_WMA_MAP_ID = 0x04,
    OFF_WMA_AREA_ID = 0x08,
    OFF_WMA_NAME = 0x0C,
    OFF_WMA_LOC_LEFT = 0x10,   // world Y at the map's left edge (max Y)
    OFF_WMA_LOC_RIGHT = 0x14,  // world Y at the map's right edge (min Y)
    OFF_WMA_LOC_TOP = 0x18,    // world X at the map's top edge (max X)
    OFF_WMA_LOC_BOTTOM = 0x1C, // world X at the map's bottom edge (min X)

    // AreaTrigger.dbc record (10 fields, 0x28 bytes) — instance cataloged
    // in docs/DBCs.md (records slot 0x00C0E034, count 0x00C0E038). The
    // client loads it but exposes NO geometry to Lua, which is why map
    // addons (pfQuest et al.) ship hand-scraped trigger tables. Schema:
    //   {ID@+0, mapID@+4 (Map.dbc — continent or instance),
    //    x@+8, y@+0xC, z@+0x10 (continent-space world coords),
    //    radius@+0x14 (sphere trigger; 0 => box trigger),
    //    boxLength@+0x18, boxWidth@+0x1C, boxHeight@+0x20, boxYaw@+0x24}
    VAR_AREATRIGGER_RECORDS = 0x00C0E034,
    VAR_AREATRIGGER_COUNT = 0x00C0E038,
    OFF_AT_MAP_ID = 0x04,
    OFF_AT_X = 0x08,
    OFF_AT_Y = 0x0C,
    OFF_AT_Z = 0x10,
    OFF_AT_RADIUS = 0x14,
    OFF_AT_BOX_LENGTH = 0x18,
    OFF_AT_BOX_WIDTH = 0x1C,
    OFF_AT_BOX_HEIGHT = 0x20,
    OFF_AT_BOX_YAW = 0x24,

    // WorldMapOverlay.dbc record (17 fields, 0x44 bytes):
    //   {ID@+0, worldMapAreaID@+4 (-> WorldMapArea row),
    //    areaID[4]@+8..+0x14 (AreaTable ids the overlay reveals),
    //    mapPointX@+0x18, mapPointY@+0x1C, textureName@+0x20 (char*,
    //    bare name), textureWidth@+0x24, textureHeight@+0x28,
    //    offsetX@+0x2C, offsetY@+0x30, hitRect@+0x34..+0x40}
    // The engine's Lua surface only exposes EXPLORED overlays: the
    // discovered-overlay ID list at 0x00B6E630 (count 0x00B6E6B4) gates
    // Script_GetMapOverlayInfo. Reading the DBC directly bypasses the
    // exploration gate — every overlay of a zone, explored or not.
    VAR_WORLDMAP_OVERLAY_RECORDS = 0x00C0D594,
    VAR_WORLDMAP_OVERLAY_COUNT = 0x00C0D598,
    OFF_WMO_WORLDMAP_AREA = 0x04,
    OFF_WMO_AREA_ID = 0x08, // areaID[4] @ +0x08..+0x14 (AreaTable ids revealed)
    OFF_WMO_MAP_POINT_X = 0x18,
    OFF_WMO_MAP_POINT_Y = 0x1C,
    OFF_WMO_TEXTURE_NAME = 0x20,
    OFF_WMO_TEXTURE_WIDTH = 0x24,
    OFF_WMO_TEXTURE_HEIGHT = 0x28,
    OFF_WMO_OFFSET_X = 0x2C,
    OFF_WMO_OFFSET_Y = 0x30,
    OFF_WMO_HITRECT_TOP = 0x34,    // hit rect in world-map canvas px (~1002x668):
    OFF_WMO_HITRECT_LEFT = 0x38,   // the tight clickable bounds of the landmass,
    OFF_WMO_HITRECT_BOTTOM = 0x3C, // used for subzone-center placement (pfQuest's
    OFF_WMO_HITRECT_RIGHT = 0x40,  // pfDB["zones"] was scraped from these)

    // Current world-map view selection — the globals the engine's
    // current-map-name resolver (FUN_004a6cf0, behind GetMapInfo) reads:
    // continent index, zone index (-1 = whole continent), the default
    // WorldMapArea row used when no continent is selected, and the
    // per-continent data blob (base ptr; stride 0x10024 per continent,
    // continent's own WMA row at +0x04, per-zone WMA row array ptr at
    // +0x10).
    VAR_WORLDMAP_CONTINENT_INDEX = 0x0084506C,
    VAR_WORLDMAP_ZONE_INDEX = 0x00845070,
    VAR_WORLDMAP_DEFAULT_AREA_ROW = 0x00845074,
    VAR_WORLDMAP_CONTINENT_DATA = 0x00B6E668,
    WORLDMAP_CONTINENT_STRIDE = 0x10024,

    // `Script_SetPoint` — the ONE SetPoint implementation, registered in
    // the Region base table and inherited by every region type. Vanilla's
    // parser accepts (point, region), (point, region, relPoint[, x, y]),
    // (point, x, y), and even an explicit nil region — but NOT the bare
    // one-arg form (`fs:SetPoint("RIGHT")`): a fully-omitted arg 3 is
    // LUA_TNONE, which fails the string/table/number/nil check and raises
    // the usage error. Modern clients accept it (anchor to parent at the
    // same point, zero offsets). `Frame::Modern` co-hooks this and
    // normalizes the one-arg call to (point, 0, 0) — the vanilla
    // (point, x, y) form, which is parent-relative and thus exactly the
    // modern semantic.
    FUN_SCRIPT_REGION_SETPOINT = 0x007A2540,

    // `CGUnit_C::OnAuraAdded` — `__thiscall void(CGUnit *unit, uint32_t
    // slot, uint32_t spellId)` (i.e. `__fastcall(unit /*ecx*/, edx_unused,
    // slot, spellId)`). Fires for EVERY aura that lands on a tracked unit,
    // including proc/triggered auras that never produce an `SMSG_SPELL_GO`
    // (Shadow Weaving, etc.). `Aura::Source` co-hooks it to stamp
    // expiration for those — it carries no caster, so it fills timing only
    // and never overwrites an entry `SpellGo` already owns. Per-aura-change
    // frequency. nampower hooks the same function (its BUFF/DEBUFF events).
    FUN_ON_AURA_ADDED = 0x006123F0,

    // `CGUnit_C::OnAuraStacksChanged` — `__thiscall void(CGUnit *unit, int
    // slot, uint8_t stackCount)` (`__fastcall(unit, edx_unused, slot,
    // stackCount)`). Fires when an existing aura's stack count changes
    // without a fresh add — e.g. Shadow Weaving climbing 2→3. The slot's
    // spellID is read from the unit aura array (`OnAuraAdded` gets it as an
    // arg; here only the slot is given). `Aura::Source` co-hooks it to
    // re-stamp expiration so stacking-DoT refreshes are picked up.
    FUN_ON_AURA_STACKS_CHANGED = 0x00612450,

    // `CGUnit_C::OnAuraRemoved` — same ABI as `OnAuraAdded`
    // (`__fastcall(unit /*ecx*/, edx_unused, slot, spellId)`; the disassembly
    // of the diff dispatcher `FUN_00604d00` at the call site `0x00604dc4`
    // shows `ECX = unit`, stack `[slot, spellId]`). The sibling of
    // `OnAuraAdded`: `FUN_00604d00` fires this when a descriptor aura slot
    // goes from occupied to empty — a genuine removal (fell off, dispelled,
    // or replaced by a higher rank, which fires Removed(old)+Added(new)).
    // `Aura::Source` co-hooks it to EVICT the cached entry so the
    // `GetAuraDataByIndex` fallback can't resurrect a superseded/dispelled
    // aura that's still inside its computed duration window.
    FUN_ON_AURA_REMOVED = 0x00612320,

    // Group-loot roll packet handlers (the inner display functions in
    // LootRoll.cpp). Each is `__fastcall`; the deserialized 0x48-byte
    // PendingLootRoll message struct is fully populated at entry and freed
    // at exit, so read its fields BEFORE calling the original. The register
    // the message arrives in differs per handler (matched in
    // loot/History.cpp): ROLL passes it in ECX, WON / ALL_PASSED in EDX.
    // Backs the C_LootHistory backport (loot/History.cpp) — 1.12 has no
    // loot-history store, so we reconstruct one from these packets.
    FUN_LOOT_ROLL_HANDLER = 0x0061C0B0,       // SMSG_LOOT_ROLL       (msg=ECX)
    FUN_LOOT_ROLL_WON_HANDLER = 0x0061B9E0,   // SMSG_LOOT_ROLL_WON   (msg=EDX)
    FUN_LOOT_ALL_PASSED_HANDLER = 0x0061B640, // SMSG_LOOT_ALL_PASSED (msg=EDX)

    // SMSG_LOOT_START_ROLL (opcode 0x2A1) handler — `__fastcall(ECX = packet)`,
    // RET 0. The result packets (ROLL/WON/PASSED) carry 0 for the item's
    // random suffix/seed; only START_ROLL does. It deserializes a fresh
    // PendingLootRoll node (same 0x20/0x24 field offsets as the message) and
    // publishes it as the store head at VAR_LOOTROLL_STORE_HEAD, so we read
    // the suffix straight off that node right after the original runs.
    FUN_LOOT_START_ROLL_HANDLER = 0x0061B310,
    VAR_LOOTROLL_STORE_HEAD = 0x00C4DC00, // PendingLootRoll* — newest node

    // PendingLootRoll message field offsets, derived from the SMSG_LOOT_ROLL
    // deserializer FUN_0061BFA0 (reads GUID/u32/byte fields off the packet).
    OFF_LOOTROLL_SOURCE_GUID = 0x10, // u64 looted-object GUID (roll identity)
    OFF_LOOTROLL_SLOT = 0x18,        // u32 loot slot
    OFF_LOOTROLL_ITEM_ID = 0x1C,     // u32 itemID
    // Random-enchant fields for the item link `item:id:enchant:suffix:unique`.
    // +0x20 is the suffix/random-property ID the engine's item-name builder
    // FUN_005D8B00 uses (indexes the random-properties DBC for the "of the X"
    // name); +0x24 is the paired seed/unique value.
    OFF_LOOTROLL_ITEM_SUFFIX = 0x20, // i32 random suffix / property ID
    OFF_LOOTROLL_ITEM_SEED = 0x24,   // u32 random seed (link unique field)
    OFF_LOOTROLL_ROLLER_GUID = 0x30, // u64 roller / winner GUID
    OFF_LOOTROLL_ROLL_NUM = 0x38,    // u8 roll (1..100); high bit = didn't roll
    OFF_LOOTROLL_ROLL_TYPE = 0x40,   // u32 rollType: 0 = need, 2 = greed

    // CGPlayer-side sub-struct, allocated for any player-controlled
    // unit (local self, target, party, raid, inspect targets — all of
    // them). Holds player-specific data that's *not* in the broadcast
    // UpdateField descriptor:
    //
    //   +0x08             uint32  PLAYER_FLAGS  (AFK = bit 1, DND = bit 2,
    //                                            RESTING = bit 5)
    //   +0x118 + slot*0x30        visible-items table (slots 0..18,
    //                                            walked by FUN_UNIT_GET_VISIBLE_ITEM)
    //
    // Same offset works for any player; the visible-items helper and
    // `UnitIsAFK`-style flag readers share the same `[unit + 0xE68]`
    // pointer. NPCs / CGCreature_C objects have this slot
    // *uninitialized* — reading without a `UNIT_FLAG_PLAYER_CONTROLLED`
    // gate is a known crash path (helper does `garbage + 0x118 +
    // slot*0x30` then derefs).
    //
    // Flag bits verified by:
    //   - `Script_IsResting` (`0x00516EA0`): `[CGPlayer + 0xE68] +
    //     0x08`, `shr 5; test 1` → bit 5 = RESTING (0x20).
    //   - Nameplate AFK renderer (`0x005EC9E0`): `[unit + 0xE68] +
    //     0x08`, `test [+8], 2` → bit 1 = AFK (0x02). Works for ANY
    //     unit, not just local player — confirmed by user testing
    //     `<AFK>` rendering above other players' heads on stock 1.12.
    //   - DND bit by symmetry with chat-flag protocol; confirmed in-game.
    //
    // PLAYER_FLAGS being out-of-band (in this sub-struct rather than a
    // broadcast UpdateField) is a vanilla-only quirk — modern WoW
    // (3.0+) added PLAYER_FLAGS as a UpdateField at descriptor +0x228.
    // In 1.12, descriptor +0x228 is repurposed as UNIT_CHANNEL_SPELL
    // (see OFF_UNIT_FIELD_CHANNEL_SPELL below); PLAYER_FLAGS only
    // exists on the +0xE68 sub-struct here.
    OFF_CGPLAYER_INFO = 0xE68,
    // Explored-areas bitfield inside the CGPlayer info sub-struct
    // (`[player+0xE68] + 0xE6C`): bit `AreaBit` set == that AreaTable area
    // is explored. 0x100 bytes. Read by the discovered-overlay rebuild
    // FUN_004a67a0 as `bits[AreaBit>>3] & (1 << (AreaBit&7))`.
    OFF_PLAYER_EXPLORED_BITS = 0xE6C,

    // PvP rank fields within the CGPlayer +0xE68 sub-struct (siblings of
    // the combo-point byte at +0x1029). Read by the item tooltip's
    // "Requires <rank>" / PVP_MEDAL red-text logic (FUN_0052B650) and
    // mirrored by C_PlayerInfo.CanUseItem:
    //   +0x102B  u8   current PvP honor rank (compared >= item
    //                 RequiredHonorRank).
    //   +0x1034  u32  earned PvP-medal/rank bitmask (item RequiredCityRank
    //                 R passes when bit (R-1) is set).
    OFF_CGPLAYER_HONOR_RANK = 0x102B,
    OFF_CGPLAYER_PVP_MEDALS = 0x1034,

    // CGObject's `GetPosition` virtual — vtable slot 5 (offset 0x14).
    // Signature: `__thiscall(this, float outBuf[3]) → float *position`.
    // The returned float* may equal outBuf (the object filled it
    // directly) or point at a cached position field on the object
    // (CGPlayer keeps its own copy). Either way, dereference the
    // pointer for x/y/z. Used by `Script_CheckInteractDistance` for
    // its squared-distance compute; same vtable layout for CGUnit,
    // CGPlayer, CGGameObject, etc. since they share the CGObject
    // base. Bytes verified via the call-site disassembly at
    // `0x0048BACF` / `0x0048BADC`.
    OFF_CGOBJECT_VTBL_GET_POSITION = 0x14,

    // CGUnit movement-block orientation (facing) in radians, 0..2*pi. Lives
    // right after the {x,y,z} position block (at +0x9B8), i.e. pos+0xC. The
    // `GetPosition` virtual copies only x/y/z into the caller's buffer, so
    // read facing off the object directly. Backs `GetPlayerFacing`. Empirically
    // located by probing the player object across a known 180-degree turn
    // (value flipped by ~pi, stayed in range); mirrored at +0x9F8 / +0xC98
    // (last-sent / render copies) — +0x9C4 is the authoritative one.
    OFF_UNIT_FACING = 0x9C4,

    // CGObject `m_objectType` field — 0 = (base) object, 1 = item,
    // 2 = container, 3 = unit, 4 = player, 5 = gameobject, … Read as
    // `*(int *)(obj + 0x14)`. (Distinct from GET_POSITION above, which
    // is a *vtable* byte-offset that happens to also be 0x14.)
    OFF_CGOBJECT_TYPE_ID = 0x14,
    OBJECT_TYPE_UNIT = 3,
    OBJECT_TYPE_PLAYER = 4,

    // World line-segment collision test (client-side LoS core):
    // `char __fastcall(const Vec3 *start /*ecx*/, const Vec3 *end /*edx*/,
    //                  int unused, Vec3 *hitOut, float *tOut, uint32 flags)`.
    // Returns nonzero if the ray hit world geometry; `*tOut` is the hit
    // fraction along start→end (t in [0,1] ⇒ hit lies between the two
    // points ⇒ blocked; t outside [0,1] or no hit ⇒ clear). `flags`
    // selects geometry: 0x100111 = terrain + WMO (the LoS combo;
    // 0x100171 also tests M2 doodads but crashes in some dungeons per
    // UnitXP_SP3). Thin `__fastcall` shim that tail-calls the real
    // intersect `FUN_0069BFF0`; verified in-binary and cross-checked
    // against UnitXP_SP3's `CWorld_Intersect` (same VA + flags).
    FUN_CWORLD_INTERSECT = 0x00672170,
    // Collision-box height of a unit, read from its CMovement sub-struct
    // (`[unit + OFF_UNIT_MOVEMENT_INFO_PTR]`) at `+0xB4`. Used as the
    // eye-height offset for LoS so foot-to-foot traces don't false-block
    // on terrain. (CMovement pointer offset 0x118 = OFF_UNIT_MOVEMENT_INFO_PTR.)
    OFF_CMOVEMENT_COLLISION_BOX_HEIGHT = 0xB4,

    // Quest list array inside the `+0xE68` sub-struct. 20 fixed
    // slots of 0xC (12) bytes each — `questID` at slot+0x00, and
    // some flags/state in the remaining 8 bytes. Walked by
    // `Script_IsUnitOnQuest` (`0x004DFE10`) which iterates by byte
    // offset over `[0, 0xF0)` with `0xC` stride and compares each
    // entry's `+0` against the target questID. The engine writes
    // this from `SMSG_QUESTGIVER_QUEST_DETAILS` / quest sync packets,
    // so it covers any synced player-controlled unit (self + nearby
    // party / raid in sync range), not just the local player.
    //
    // **Crash hazard**: same as visible items at `+0x118` — for NPCs
    // (CGCreature_C objects) the `+0xE68` slot is uninitialized.
    // Callers MUST gate on `UNIT_FLAG_PLAYER_CONTROLLED` before
    // dereferencing. The engine's `Script_IsUnitOnQuest` is protected
    // because its GUID-by-type filter (`FUN_00468460(0x10, ...)`)
    // rejects non-player GUIDs upstream; `FUN_RESOLVE_UNIT_TOKEN`
    // doesn't have that filter, so we gate explicitly.
    OFF_CGPLAYER_INFO_QUEST_LIST = 0x28,
    CGPLAYER_INFO_QUEST_LIST_STRIDE = 0xC,
    CGPLAYER_INFO_QUEST_LIST_MAX = 20,
    OFF_PLAYER_INFO_FLAGS = 0x08,
    PLAYER_FLAG_AFK = 0x02,
    PLAYER_FLAG_DND = 0x04,

    // Guild-key field on the CGPlayer sub-struct. Verified by
    // disassembling `Script_GetGuildInfo` at `0x004C9330`: after
    // resolving the unit and checking the GUID-is-player bit, the
    // function reads `mov ecx, [edi+0xE68]; mov ecx, [ecx+0x0C]` —
    // if the value is 0, returns nil; otherwise passes it as the
    // first arg to the guild cache lookup at `0x00560D30`. Two
    // players in the same guild end up with the same value here
    // (the cache key uniquely identifies a guild record), so
    // `UnitIsInMyGuild` can compare this field between `"player"`
    // and the input unit when the data is loaded — sidestepping the
    // roster fetch requirement entirely for visible units.
    //
    // Populated immediately for the local player on guild join, and
    // for any other player-controlled unit whose `+0xE68` sub-struct
    // the engine has synced (party members, raid members, the
    // current target, etc.). For unsynced units it reads 0.
    OFF_PLAYER_INFO_GUILD_KEY = 0x0C,

    // Guild roster — the per-character cache populated by
    // SMSG_GUILD_ROSTER. `[VAR_GUILD_ROSTER_PTR]` is `GuildMember **`
    // (a heap-allocated array of pointers), indexed `[0..total-1]` where
    // total = `[VAR_GUILD_ROSTER_TOTAL_COUNT]`. Includes offline
    // members — the "show offline" UI toggle controls what
    // `GetNumGuildMembers()` returns by default and how the roster
    // panel renders, but the underlying array always holds every
    // member the server sent. Entries can still be NULL for other
    // reasons (e.g. pending refresh), so null-check per entry.
    //
    // Verified by disassembling `Script_GetGuildRosterInfo` at
    // `0x004D1200`:
    //   mov ecx, [0x00B73118]         ; count
    //   cmp eax, ecx; jae bail
    //   mov edx, [0x00B72704]         ; roster base
    //   mov edi, [edx+eax*4]          ; entry = roster[idx]
    //   test edi, edi; je bail
    //   lea edx, [edi+8]; call lua_pushstring   ; name at +0x08 (inline char[])
    //
    // Other GuildMember fields the engine pushes from the same loop:
    //   +0x38  int level
    //   +0x3C  int classIndex
    //   +0x44  int rankIndex
    // (We don't read these, but they're verified in the same function.)
    //
    // The companion `[0x00B7311C]` global is the online-only count
    // (returned by `GetNumGuildMembers()` when called with no args).
    // The roster array spans the full total — both counts share the
    // same backing storage; online vs. total only affects the iteration
    // bound the Lua API uses, not the underlying array.
    VAR_GUILD_ROSTER_PTR = 0x00B72704,
    VAR_GUILD_ROSTER_TOTAL_COUNT = 0x00B73118,
    OFF_GUILD_MEMBER_NAME = 0x08,

    // PackBagSlot — __fastcall(L, void **outInvMgr, int *outLinearSlot, int *outUnused) → bool.
    // Reads bagID at Lua stack[1] and slot at stack[2], validates them, and
    // returns the inventory manager + linear slot ready to feed into GetItemBySlot.
    FUN_PACK_BAG_SLOT = 0x004F9820,
    // Equipped-bag container-GUID getter — `uint64 __fastcall(uint bagIndex0)`
    // where `bagIndex0` is 0-based (Lua bagID 1..4 → 0..3; 4..9 are bank bags,
    // gated on the bank-open globals). Returns the CGContainer GUID of the bag
    // equipped in that slot, or 0 if none. This is the internal PackBagSlot
    // uses to resolve bags 1..4: get the bag GUID here, resolve the container
    // via FUN_OBJECT_RESOLVE_BY_GUID(TYPEMASK_CONTAINER, guid), then call the
    // container's vtable[+OFF_CONTAINER_GET_INVENTORY] to get the inventory
    // object GetItemBySlot indexes. Lets us enumerate bag contents in pure C++
    // without PackBagSlot's Lua-stack coupling. (Reads GUID arrays at
    // 0x00BDD060 lo / 0x00BDD064 hi, stride 8.)
    FUN_GET_EQUIPPED_BAG_GUID = 0x004F93E0,
    // CGContainer vtable slot (byte offset into the vtable) whose method
    // returns the container's inventory object — `void* __thiscall(container)`.
    // The inventory object's slot count lives at its +0x00 (OFF_INVMGR_SLOT_COUNT).
    OFF_CONTAINER_GET_INVENTORY = 0x10,
    // Backpack (bagID 0) linear-slot base in the player invMgr GUID/item
    // arrays: a 1-based backpack slot S maps to linear (S - 1) + 23. Matches
    // PackBagSlot's `*param_3 += 0x17` for the backpack branch.
    BACKPACK_LINEAR_BASE = 23,
    // Player inventory manager layout — used for direct GUID-array reads
    // that bypass `GetItemBySlot`'s bank gate at `0x006228C1`.
    //   +0x00  uint32  max slot count
    //   +0x04  uint64* pointer to a flat GUID array, indexed by linear slot
    //                   (8 bytes per slot — low dword + high dword)
    //   +0x10  uint8   "bank-aware mode" flag (engine-internal; gates the
    //                   slot-range checks in `GetItemBySlot`'s validation
    //                   path)
    // Slot ranges, matching `PackBagSlot`'s linearization:
    //   0..22   equipment / paperdoll
    //   23..38  backpack (16 slots)
    //   39..62  main bank (24 slots)
    //   63..68  bank bag SLOTS (the bag items themselves; the bag CONTENTS
    //           live in each equipped bag's own invMgr, reachable via the
    //           bag's vtable +0x10)
    //   81+     keyring
    //
    // **Bank slots are populated from server data at login** (verified
    // empirically on Turtle WoW: fresh login + cleared WDB folder shows
    // bank GUIDs already populated in slots 39..62, with the gate at
    // `VAR_BANK_GATE_GUID` = 0). The gate doesn't hide data missing —
    // it hides data that's present from boot. Reading the GUID array
    // directly recovers it without ever opening the bank window.
    OFF_INVMGR_GUID_ARRAY = 0x04,
    INVMGR_BANK_MAIN_FIRST_SLOT = 39,
    INVMGR_BANK_MAIN_LAST_SLOT = 62,
    INVMGR_BANK_BAG_FIRST_SLOT = 63,
    INVMGR_BANK_BAG_LAST_SLOT = 68,
    // Engine's `ObjectMgr::Get`-style resolver — given a type and GUID,
    // returns the resolved CGObject pointer (or null). Same function the
    // engine itself uses inside `GetItemBySlot` (called at `0x00622904`)
    // and `PackBagSlot` (called at `0x004F98E2`). We invoke it directly
    // for bank slots so we sidestep the banker-GUID gate that
    // `GetItemBySlot` applies for slots 39..68 — the GUIDs in
    // `invMgr+0x04` are populated at login, only the gate gets toggled
    // by bank-window state.
    //
    //   __fastcall(int type, const char *debugName, u32 guidLo,
    //              u32 guidHi, int priority) → void *
    //
    // Type values: 2 = item (returns CGItem*), 4 = container/bag
    // (returns CGContainer*). Engine passes `"ItemMgr"` as debugName
    // and `0x172` as priority for both call sites we've decoded. Some engine
    // sites type it as `(u32 typeMask, void *unused, u64 guid, int)` —
    // ABI-equivalent (the u64 occupies the same two stack dwords as
    // guidLo/guidHi). All our callers go through `Object::ByGuid`
    // (object/Resolve.h), which wraps this single address.
    FUN_OBJECT_RESOLVE_BY_GUID = 0x00468460,
    // The type arg is a bitmask of object-type bits (see the TYPEMASK_*
    // block below), not an enum index.

    // `CGObject::GetName` — returns the display-name `const char *` for
    // a resolved CGObject (CGUnit / CGPlayer / CGCreature). Internally
    // does a NameCache lookup for players and reads the creature-info
    // cache for NPCs; falls back to `"UNKNOWNOBJECT"` / `"Unknown Being"`
    // when neither resolves. The same path `FUN_00609370` (the wrapped
    // "name + PvP-rank decoration" helper used by the SetUnit tooltip
    // builder) calls internally.
    //
    //   __thiscall const char *(void *obj, int *outFlags /* may be 0 */)
    //
    // Caller-owned `outFlags` receives metadata about the lookup; we
    // pass `nullptr` since we just want the name.
    FUN_OBJECT_GET_NAME = 0x00609210,
    // `CGGameObject_C::GetName` — gameobject-specific name getter.
    // Returns `*(obj+0x214)+0x08` (the cached GameObjectStats_C record's
    // name field) if the record has loaded, or the engine's empty-string
    // sentinel `DAT_00882748` otherwise. Used by the hover tooltip
    // populator at FUN_0052AA20.
    //
    //   __fastcall const char *(void *gameObject)
    //
    // The polymorphic `FUN_OBJECT_GET_NAME` above is unit-cache-only
    // (routes through NameCache / creature cache) and returns the
    // "UNKNOWNOBJECT" sentinel for gameobjects.
    FUN_GAMEOBJECT_GET_NAME = 0x005F8100,
    // Bank gate. The engine writes the active banker NPC's GUID here
    // when the bank window opens (8-byte qword) and zeroes it on close.
    // `GetItemBySlot` returns null for slots 39..68 if this GUID is zero,
    // even though the slot data in `invMgr+0x04` remains populated.
    // Bypassed in the direct-read bank path; informational only otherwise.
    VAR_BANK_GATE_GUID = 0x00BDD038,
    // `Script_UseContainerItem` Lua C function — `__fastcall(void *L)`.
    // Reads bagID at Lua stack[1] and slot at stack[2], dispatches to the
    // engine's item-use machinery (same path the secure
    // `tooltip:Click()` /  `UseContainerItem(...)` flow takes from
    // Lua). We invoke this from `C_Container.UseHearthstone` after
    // locating the hearthstone in bags.
    FUN_SCRIPT_USE_CONTAINER_ITEM = 0x004FA0E0,
    // Vanilla 1.12 hearthstone is always itemID 6948 and uses spell 8690
    // ("Hearthstone" - 10-second cast, teleport to bind). Modern WoW
    // recognizes many hearthstone-equivalent toys (Garrison, Dalaran,
    // etc.) but those items don't exist in 1.12. Custom servers
    // (Turtle WoW and similar) sometimes ship alternate hearthstone
    // items that reuse spell 8690 as their on-use, so the hearthstone
    // matcher OR's `itemID == HEARTHSTONE_ITEM_ID` against
    // `OnUseSpellIDForItemID(itemID) == HEARTHSTONE_SPELL_ID` to catch
    // both shapes.
    HEARTHSTONE_ITEM_ID = 6948,
    HEARTHSTONE_SPELL_ID = 8690,
    // `Script_PickupContainerItem` Lua C function — `__fastcall(void *L)`.
    // Reads bagID at Lua stack[1] and slot at stack[2]; if an item is
    // present there, it goes onto the cursor (or, if cursor already holds
    // an item, swaps with the bag slot). Used by `EquipItemByName` to
    // pick up the source item before dispatching to the equip helpers.
    FUN_SCRIPT_PICKUP_CONTAINER_ITEM = 0x004F9B30,
    // Container-content cursor-pickup PRIMITIVE — the plain-pickup branch
    // inside `Script_PickupContainerItem`'s state machine, called directly
    // (no Lua stack, no state-machine modes).
    //   void __fastcall(uint linearSlot, int count, uint itemGuidLo,
    //                   uint itemGuidHi, uint containerGuidLo,
    //                   uint containerGuidHi, int flag)
    // Self-resolves the player, sets the cursor-state globals
    // (VAR_CURSOR_ITEM_GUID_*, cursor type = 1) and fires the cursor-update
    // event itself. Does NOT apply the cursor-empty / item-lock guards the
    // Script wrapper does — callers replicate those (Item::Cursor::PickupBagItem).
    FUN_CURSOR_PICKUP_ITEM = 0x00494B60,
    // `CGItem::UseItem` — the engine's actual "use this item" primitive.
    // It's the fallback dispatch in `Script_UseContainerItem` (the call
    // site at 0x004FA430 after every special-cursor-mode branch is
    // skipped), and the function `Script_UseContainerItem` ends up at
    // for normal hearthstone-style / potion / food / scroll use. We
    // call it directly from `UseItemByName` so we don't pay a
    // Lua-stack roundtrip just to re-find the item the engine already
    // points us at.
    //
    // Internally dispatches by item flags / fields:
    //   - off-target-pickup items (flag 0x200)       → FUN_005EDEA0/FUN_005EDD60
    //   - bind-on-pickup confirmation needed          → FUN_004E32E0
    //   - food items (flag 0x4)                       → FUN_005EDC80
    //   - quiver/ammo (flag 0x2000)                   → FUN_005EEF40
    //   - default (potions, hearthstone, scrolls, …)  → FUN_006E5A90
    //
    //   __thiscall uint(CGItem *item, const uint64_t *targetGuid,
    //                   int flag)
    //
    // `targetGuid` is a pointer to a 64-bit GUID — for self-use items
    // the engine substitutes the player itself even if a different
    // target is passed, so passing zero (no target) is fine for most
    // items. `flag` is 0 in every Script_UseContainerItem call site.
    FUN_ITEM_USE = 0x005D8D00,
    // `CGPlayer::AutoEquipCursorItem` — `__thiscall(CGPlayer *this, int flag)`.
    // The engine-internal helper that `Script_AutoEquipCursorItem`
    // (`0x0048A040`) is a thin wrapper around: that wrapper just
    // resolves the local player and calls this with `flag = 0`.
    // Equips the cursor item to its natural slot (engine picks based
    // on inventory type) and clears the cursor. No-op if cursor is
    // empty.
    FUN_AUTO_EQUIP_CURSOR_ITEM = 0x005E1480,
    // Per-item descriptor block (object/item-field array) lives at this offset
    // on the CGItem instance.
    OFF_ITEM_DESCRIPTOR = 0x114,
    // Within the descriptor, ITEM_FIELD_FLAGS is at +0x3C (a single dword).
    // Bit 0 = soulbound, bit 3 = broken (see GetInventoryItemBroken at 0x4C8626
    // which tests `[descriptor+0x3C] & 0x08`). Confirmed empirically by dumping
    // descriptor bytes for a worn-and-bound item.
    OFF_DESCRIPTOR_FLAGS = 0x3C,
    ITEM_FLAG_SOULBOUND = 0x01,
    // Within the descriptor, the item's random-property index
    // (ITEM_FIELD_RANDOM_PROPERTIES_ID) is at +0x98 — this is the live
    // instance's suffix, indexing ItemRandomProperties.dbc the same way
    // StatAccum::ApplyRandomSuffix expects. Derived from the item-tooltip
    // builder's `[descriptor + 0x26*4]` read (FUN_0052B650 @ ~0x52b7xx),
    // which feeds it straight into `ItemRandomProperties[idx]`.
    OFF_DESCRIPTOR_RANDOM_PROPERTY = 0x98,
    // Bit 2 — instance is "unlocked", i.e. the lock referenced by
    // `ItemStats.LockID` has been opened (rogue Pick Lock, key item,
    // etc.). For unlockable items, the engine gates the
    // `<Right Click to Open>` tooltip on this bit: a fresh lockbox
    // has it clear (priest sees no openable affordance), a picked
    // lockbox has it set. Items with `LockID == 0` (clams, simple
    // sacks) never need the bit.
    ITEM_FLAG_UNLOCKED = 0x04,

    // Client-side "in-transaction" lock — set when the engine puts an
    // item on the cursor / mail-attaches it / trade-attaches it, cleared
    // when the SMSG_UPDATE_OBJECT post-processor (`FUN_005D8440`)
    // confirms the transaction OR a cursor-cancel sweep
    // (`FUN_00495460`) clears all locks.
    //
    // This is NOT in m_objectFields — it's a per-CGItem instance flag
    // at byte offset 0x314, lowest bit (mask 0x01). Bit 1 of the same
    // dword is a separate "cache-coherence" flag set by the SMSG
    // post-processor. We only expose bit 0 here.
    //
    // The matching setter / clearer the engine uses:
    //   FUN_004953E0(guidLo, guidHi)  — set:   `[item+0x314] |= 1` + fire ITEM_LOCK_CHANGED
    //   FUN_00495420(guidLo, guidHi)  — clear: `[item+0x314] &= ~1` + fire ITEM_LOCK_CHANGED
    //   FUN_00495460()                — sweep: walk all bags/items, clear all locks
    //
    // Vanilla fires `ITEM_LOCK_CHANGED` (engine event 0xBC = 188) on
    // every set / clear; no payload.
    OFF_ITEM_CLIENT_LOCK = 0x314,
    ITEM_CLIENT_LOCK_BIT = 0x01,

    // `__stdcall(uint guidLo, int guidHi)` — sets bit 0 at `[item+0x314]`
    // for the resolved item and fires ITEM_LOCK_CHANGED. Mirror of
    // FUN_ITEM_UNLOCK_BY_GUID below. Engine's normal trigger is the
    // optimistic-lock step inside pickup/equip/attach paths (cursor
    // packets are sent immediately after).
    FUN_ITEM_LOCK_BY_GUID = 0x004953E0,

    // `__stdcall(uint guidLo, int guidHi)` — clears bit 0 at
    // `[item+0x314]` for the resolved item and fires ITEM_LOCK_CHANGED.
    // Resolves the item by GUID internally via FUN_00468460; safe to
    // call with the GUID of an item that's already gone (event still
    // fires but no-op on state). The engine's normal trigger for this
    // is the SMSG_UPDATE_OBJECT post-processor (`FUN_005D8440`) when
    // the server confirms a transaction.
    FUN_ITEM_UNLOCK_BY_GUID = 0x00495420,

    // `__cdecl()` — walks every CGItem the engine knows about (bag
    // containers + their contents) and clears the lock bit on each.
    // Fires a single ITEM_LOCK_CHANGED at the end. The engine itself
    // only calls this from the PLAYER_LEAVING_WORLD cleanup path
    // (`FUN_005FEF70` → fires event 0x112 right after); we expose it
    // to addons as a stuck-lock recovery primitive.
    FUN_ITEM_UNLOCK_ALL = 0x00495460,
    // ITEM_FIELD_STACK_COUNT — single dword. Verified by decoding
    // `Script_GetContainerItemInfo` (`0x004F9670`): after resolving the
    // descriptor, `mov eax, [esi+0x114]; fild [eax+0x20]` is the count
    // return. Field index 0x8 in 1.12's item-field layout, which puts
    // STACK_COUNT *before* the contained/creator GUIDs (0x9..0xE) —
    // different from the more common documented layout that places
    // STACK_COUNT after them. Trust the binary, not external docs.
    OFF_DESCRIPTOR_STACK_COUNT = 0x20,
    // ITEM_FIELD_SPELL_CHARGES[0] — first of five signed dwords. Field
    // indices 10..14 (fields 8..9 are STACK_COUNT and DURATION; field 15
    // is FLAGS at +0x3C, which gives a tight sandwich around the
    // SPELL_CHARGES range). Derived from the descriptor-field name table
    // builder at `FUN_0047f840`, which consumes a list of `{ name_ptr,
    // ?, count, ?, ? }` entries starting at `0x0083a328` (14 items) —
    // summing the `count` field in declaration order gives each name's
    // starting dword index. STACK_COUNT/FLAGS/DURABILITY/MAX_DURABILITY
    // landing on their known offsets cross-checks the derivation. The
    // tooltip "X Charges" text in `FUN_0052b650` reads from the
    // *enchantment*-charges range at +0x48 (slot N + 0x8) which is a
    // different field (ITEM_FIELD_ENCHANTMENT, fields 16..36).
    //
    // Value is signed: positive = rechargeable (wand recovers charges
    // on use? no — they decrement; positive means "doesn't destroy on
    // last use"). Negative = single-use, destroyed when count hits 0.
    // `GetItemCount(includeUses=true)` uses abs() for the multiplier.
    OFF_DESCRIPTOR_SPELL_CHARGES_0 = 0x28,
    // ITEM_FIELD_ENCHANTMENT block (fields 16..36, per the +0x48
    // enchant-charges note above) — 7 enchant slots, 3 dwords each
    // `{enchantID, duration, charges}`. Slot 0 = permanent enchant,
    // slot 1 = temporary (weapon oils / sharpening stones). The
    // enchant ID indexes SpellItemEnchantment.dbc. Block base = field
    // 16 = +0x40 (field 15 FLAGS at +0x3C precedes it); slot stride
    // 0x0C, enchant ID at slot+0x00. Used by spell/BonusDamage.cpp to
    // fold enchant-granted spell power into GetSpellBonusDamage.
    OFF_DESCRIPTOR_ENCHANTMENT_ID = 0x40,
    DESCRIPTOR_ENCHANTMENT_SLOT_STRIDE = 0x0C,
    // ITEM_FIELD_DURABILITY (current) and ITEM_FIELD_MAXDURABILITY (max) live
    // adjacent to each other in the descriptor as plain dwords. Verified in
    // `Script_GetInventoryItemBroken` (`0x004C8590`): after resolving the
    // descriptor, it reads `[ecx+0xA4]` for max and `[eax+0xA0]` for cur,
    // and treats the item as broken when `max > 0 && cur == 0`. Items with
    // no durability concept (consumables, materials) have both fields 0.
    OFF_DESCRIPTOR_DURABILITY = 0xA0,
    OFF_DESCRIPTOR_MAX_DURABILITY = 0xA4,
    // Per-instance "readable text" id (letters / mail-scroll items carry a
    // server-assigned text id here). Drives the `readable` return of
    // `Script_GetContainerItemInfo` (`0x004F9670`): at `0x004F97C1` it reads
    // `[descriptor+0x9C]` and, when non-zero, reports the slot as readable
    // even when the static ItemStats page-text is 0. Field index 0x27.
    OFF_DESCRIPTOR_READABLE_TEXT_ID = 0x9C,

    // Per-item repair-cost helper used by Script_GameTooltip_SetInventoryItem
    // (3rd return value). __fastcall(ecx = CGItem *) -> int copperCost.
    // Returns 0 for null/broken/no-durability/fully-repaired items, otherwise
    // the cost in copper, with the merchant discount applied IFF a merchant
    // window is currently open.
    //
    // Internals: FUN_005DA330 is the raw calculator; FUN_004FAF30 is the
    // discount wrapper that calls it. The raw side:
    //   1. Look up the item's ItemStats record via DBCache_ItemStats_C_GetRecord
    //      (callback=NULL → returns NULL for uncached items).
    //   2. Index DurabilityCosts.dbc by `subClass` to get the per-class row.
    //   3. Branch on inventoryType: weapon (2) uses cols at row+4..row+0x54,
    //      armor (4) uses cols at row+0x58..row+0x78.
    //   4. Multiply by DurabilityQuality.dbc's quality multiplier (indexed
    //      by `quality * 2 + 1`).
    //   5. Round to nearest int; clamp 0 → 1 if there was any cost at all.
    //
    // The discount wrapper:
    //   - Resolves the local player object via FUN_00468550 (returns the
    //     player's GUID from [0x00B41414 + 0xC0]).
    //   - Resolves the current-merchant object via the GUID stored in
    //     DAT_00BDDFA0 / DAT_00BDDFA4. Those globals are set by
    //     FUN_004FACF0 on SMSG_LIST_INVENTORY (merchant frame opens) and
    //     zeroed by FUN_004FAC50 when the merchant frame closes.
    //   - If both lookups succeed, calls FUN_00612B80(merchant, player)
    //     to compute (factionDiscount + pvpRankDiscount). Faction
    //     contribution: 0% below Friendly, +base% (5%) at Friendly and
    //     above. PvP rank contribution: +5% for PvP rank > 5
    //     (Sergeant Major+), another +5% for rank > 7
    //     (Knight-Lieutenant+).
    //   - Otherwise (no merchant context) the multiplier stays at 1.0
    //     and the raw cost is returned undiscounted.
    //
    // So this helper produces the merchant's displayed cost ONLY when
    // the player has a vendor open; called from anywhere else (HUD
    // refresh, idle check) it returns the raw base cost.
    FUN_ITEM_REPAIR_COST = 0x004FAF30,

    // CGItem has TWO descriptor-like pointers and they hold different things:
    //   +0x114 = m_objectFields (the UpdateField array we read FLAGS from at
    //            +0x3C). For item instances, OBJECT_FIELD_ENTRY in this array
    //            is empirically 0 — don't try to read itemID from here.
    //   +0x08  = a separate identification block. Contains the item's GUID at
    //            +0x00 (qword) and the itemID at +0x0C (dword).
    // The canonical "look up the cache record for this item" sequence is:
    //   item = GetItemBySlot(invMgr, slot)
    //   id   = *(uint32_t *)(*(void **)(item + 0x08) + 0x0C)
    //   _GetRecord(cache, id, ...)
    // Verified at 0x004C8B1F-2D (inventory→cache lookup right after a
    // GetItemBySlot call). The same shape appears in many other inventory
    // sites that call _GetRecord.
    OFF_ITEM_INSTANCE_BLOCK = 0x08,
    OFF_INSTANCE_BLOCK_ITEM_ID = 0x0C,

    // Item stats cache (the client-side cache of ItemSparse-style records that
    // gets populated by SMSG_ITEM_QUERY_SINGLE_RESPONSE). The cache instance
    // lives directly at this VA — `_GetRecord`'s `this` argument is the literal
    // address `0xC0E2A0`, not a pointer to it. See Script_GetItemInfo at
    // 0x0048E070 which calls `mov ecx, 0xC0E2A0` before the call.
    VAR_ITEMDB_CACHE = 0x00C0E2A0,

    // `Script_GetItemInfo` Lua C function. We hook this to auto-warm the
    // item cache on miss — vanilla 1.12's stock behavior is "return nil
    // for uncached items and don't fire any query"; modern WoW (5.x+)
    // auto-fires the load and emits `GET_ITEM_INFO_RECEIVED` when data
    // arrives. The hook makes `GetItemInfo(uncached_id)` return nil
    // first call but kick off the query, so subsequent calls work.
    FUN_SCRIPT_GET_ITEM_INFO = 0x0048E070,
    // ItemStats_C *(__thiscall *)(void *cache, uint32_t itemID,
    //                             const uint64_t *guid /*may point to zero*/,
    //                             void *callback, void *userData,
    //                             bool requestIfMissing).
    // With `requestIfMissing=false`, returns NULL if not in cache (no server
    // round-trip) — exactly the "instant" semantics we want.
    FUN_DBCACHE_ITEMSTATS_GET_RECORD = 0x0055BA30,
    // Item-cache response handler — the function called from
    // `0x00555140` (success path for `SMSG_ITEM_QUERY_SINGLE_RESPONSE`)
    // and `0x00555160` (failure path). Parses one or more itemIDs out
    // of the packet, looks up (or creates) the cache entry per ID,
    // copies the item data block via `FUN_007c9640` to `entry+0x18`,
    // sets `[entry+0x1F0]=1` (the loaded byte), then walks the entry's
    // pending callback list at `[entry+0x1FC]` and invokes each with
    // `(userData, success)`. High-bit-on itemID in the packet signals
    // a per-item failure for the batch.
    //
    // We hook this to observe any item-cache fill the engine performs
    // — covers the engine's natural inventory prefetch and any
    // addon-triggered queries without having to register our own
    // engine callbacks. Lets `RequestLoadItemData` be purely passive:
    // we track itemIDs, the engine (or whoever) issues the query, we
    // see the fill via this hook and fire `ITEM_DATA_LOAD_RESULT`.
    //
    // Signature: `__thiscall(void *cache, void *packetReader, int flag)`.
    // `cache` is the same `VAR_ITEMDB_CACHE` we already use.
    FUN_ITEMSTATS_CACHE_RESPONSE = 0x0055BDB0,
    // ItemStats_C field offsets we read. Full struct layout in
    // VanillaHelpers's `Game.h` (`struct ItemStats_C`); we only need these.
    OFF_ITEMSTATS_CLASS = 0x00,
    OFF_ITEMSTATS_SUBCLASS = 0x04,
    // `char *m_name[4]` per VanillaHelpers's `struct ItemStats_C`. Only
    // m_name[0] is the primary localized item name (e.g., "Linen Cloth");
    // m_name[1..3] are random-property suffix slots that are typically
    // null for non-randomized items. Used by `C_Item.IsEquippedItem`'s
    // name-string match form.
    OFF_ITEMSTATS_NAME = 0x08,
    OFF_ITEMSTATS_DISPLAY_INFO_ID = 0x18,
    OFF_ITEMSTATS_QUALITY = 0x1C,    // u32 — 0=Poor … 5=Legendary

    // Item-quality color-prefix table. Array of 7 `const char *`
    // pointers, indexed by quality 0..6 (POOR..ARTIFACT); each
    // points to a `"|cffRRGGBB"` markup string used by the engine's
    // own link builder. Discovered via the inner color helper at
    // `0x0052AD90` which does `(&table)[quality]` with a fallback
    // to white (`"|cffffffff"`, the index-1 entry) for quality > 6.
    // Quality 7 (Heirloom) doesn't exist in vanilla; consumers
    // should mirror the engine's fallback for safety.
    VAR_QUALITY_COLOR_TABLE = 0x00854124,
    QUALITY_COLOR_TABLE_SIZE = 7,
    // `m_flags` — item-class flags bitfield. Bits we read:
    //   `ITEMSTATS_FLAG_OPENABLE` (`0x4`, bit 2): freely right-click-
    //     openable container (sack, clam, simple chest, quest box).
    //     The tooltip line `<Right Click to Open>` (`ITEM_OPENABLE`
    //     string) renders for items with this bit set, gated on the
    //     player's lock skill when the item also carries a non-zero
    //     LockID. Same bit modern WoW's `C_Item.IsItemOpenable`
    //     surfaces.
    //   Other documented bits (`0x1` = BIND_ON_PICKUP, `0x2` = CONJURED,
    //     `0x10` = LOOTABLE, `0x200` = WRAPPER, etc.) live in the same
    //     field but we don't read them here. Add named constants as
    //     consumers materialize.
    // Reader at `0x0052E323` (the tooltip-builder's openable branch in
    // `FUN_0052B650`) does `(ItemStats[+0x20] & 0x4)` against this
    // field; verified by checking the field offset matches the
    // 5-DWORD-per-row layout `puVar4[8]` resolves to in the decompile.
    OFF_ITEMSTATS_FLAGS = 0x20,
    ITEMSTATS_FLAG_OPENABLE = 0x4,
    OFF_ITEMSTATS_SELL_PRICE = 0x28,
    // `m_lockID` — references `Lock.dbc`. Zero means "no lock"; any
    // nonzero value gates the openable interaction on the player
    // successfully unlocking the item (instance flag bit 2 in
    // `ITEM_FIELD_FLAGS`). Same field the tooltip builder reads as
    // `ItemStats[+0x1AC]` (puVar4[0x6b]) when deciding whether the
    // `<Right Click to Open>` line should require the lock to be
    // picked first.
    OFF_ITEMSTATS_LOCK_ID = 0x1AC,
    OFF_ITEMSTATS_INVENTORY_TYPE = 0x2C,
    OFF_ITEMSTATS_ITEM_LEVEL = 0x38, // u32 — base ilvl from ItemSparse
    // `m_stackable` — max stack size for this item type (1 for non-
    // stackable; 5/20/200/etc. for stackable). The instance-level
    // current stack count is a different field (`OFF_DESCRIPTOR_STACK_COUNT`
    // at +0x20 on the CGItem's m_objectFields descriptor — see
    // `Item::Count.cpp`).
    OFF_ITEMSTATS_STACKABLE = 0x60,
    // Bag-only fields. `m_containerSlots` (slot count) and `m_bagFamily`
    // (the BagFamily bitfield — quiver=1, ammo pouch=2, soul bag=4 in
    // vanilla) live deep in the record. Offsets derived from
    // VanillaHelpers's `struct ItemStats_C` (full layout, sizeof =
    // ~0x1D4); cross-check by counting fields up through `m_stackable`
    // at +0x60 (which we already use) — `m_containerSlots` is the next
    // u32 at +0x64. `m_bagFamily` is the last u32, at +0x1D0.
    OFF_ITEMSTATS_CONTAINER_SLOTS = 0x64,
    // `m_bagFamily` is a **raw 1-based ID** in 1.12, not the modern
    // bitmask. Vanilla data: arrow=1, bullet=2, soul shard=3, herb=6,
    // etc. Wrath flipped the encoding to `1 << (ID-1)` (so soul shard
    // became 4, herb became 32, …) and addons from Wrath onward expect
    // the bitmask form. Reader functions must convert via
    // `bitmask = id ? (1 << (id - 1)) : 0` to maintain modern API
    // parity. Verified empirically on Turtle WoW: Soul Shard (6265)
    // returns raw 3 here, which is `1 << 2 = 0x4` in modern encoding.
    //
    // Field offset verified by decoding the engine's own
    // `GetItemBagFamily(itemID)` helper at `0x005DA050` — calls
    // `_GetRecord` (`0x0055BA30`) on the item cache (`0x00C0E2A0`),
    // tests the result for null, then `mov eax, [eax+0x1D0]; ret`.
    // (Found via xref scan for `mov reg, [reg+0x1D0]` in `.text`.)
    OFF_ITEMSTATS_BAG_FAMILY = 0x1D0,

    // `m_itemSet` — ItemSet.dbc row ID for items that belong to an
    // armor / equipment set. 0 for items not part of any set.
    // Position derived by counting fields up through `m_block` per
    // VanillaHelpers's `ItemStats_C` (m_block at +0x1BC; m_itemSet
    // is the next u32). Verified against `m_bagFamily` already
    // confirmed at +0x1D0 — m_itemSet sits 16 bytes earlier with
    // `m_maxDurability`, `m_area`, `m_map` between the two.
    OFF_ITEMSTATS_ITEM_SET = 0x1C0,

    // Item-spell tables — five parallel arrays starting at +0x11C in
    // each `ItemStats_C` record. Each item has up to 5 spell slots
    // (vanilla server data uses ≤2 for most items: e.g. Hearthstone
    // 6948 uses slot 0 for spell 8690 with trigger 0 = `ON_USE`).
    //
    // Field offsets computed by summing struct members in
    // VanillaHelpers's `ItemStats_C` definition; verified empirically
    // against the Hearthstone case (spellID 8690, trigger 0).
    //
    // Trigger enum (CMaNGOS `ITEM_SPELLTRIGGER_*`):
    //   0 = ON_USE        (potions, trinkets, scrolls, hearthstone)
    //   1 = ON_EQUIP      (passive proc auras on equipped gear)
    //   2 = CHANCE_ON_HIT (weapon proc effects)
    //   4 = SOULSTONE     (soulstone-style on-death effect)
    //   5 = ON_USE_NO_DELAY
    //   6 = LEARN_SPELL   (recipe items)
    //
    // `GetItemSpell` matches modern WoW semantics — returns the
    // ON_USE (trigger=0) entry only, ignoring ON_EQUIP procs / weapon
    // procs / recipes. Walks all 5 slots since the ON_USE entry isn't
    // always at index 0 (some Turtle WoW custom items put it later).
    OFF_ITEMSTATS_SPELL_ID = 0x11C,        // u32[5] — spell ID per slot
    OFF_ITEMSTATS_SPELL_TRIGGER = 0x130,   // u32[5] — trigger code per slot
    OFF_ITEMSTATS_SPELL_CHARGES = 0x144,   // i32[5] — charges per slot (-1 = infinite)
    OFF_ITEMSTATS_SPELL_COOLDOWN = 0x158,  // u32[5] — cooldown ms per slot
    OFF_ITEMSTATS_SPELL_CATEGORY = 0x16C,  // u32[5] — category (groups shared cooldowns)
    OFF_ITEMSTATS_SPELL_CATEGORY_CD = 0x180, // u32[5] — category cooldown ms
    ITEMSTATS_SPELL_SLOT_COUNT = 5,
    ITEM_SPELLTRIGGER_ON_USE = 0,

    // `ItemStats_C.m_maxCount` — max number of this item the player can
    // hold in inventory at once. `0` = unlimited, `1` = unique, higher
    // values are caps like "5x bandage type". Vanilla 1.12 doesn't have
    // modern "unique-equipped category" semantics; this is the only
    // uniqueness signal the client has. Derived by summing struct
    // members in VanillaHelpers's `ItemStats_C` definition.
    OFF_ITEMSTATS_MAX_COUNT = 0x5C,

    // Remaining `ItemStats_C` fields used by `C_Item.GetItemData(ByID)`.
    // Offsets derived from VanillaHelpers's `struct ItemStats_C`
    // definition (`C:\Git\VanillaHelpers\src\Game.h`), sized to match
    // the verified anchors above (`+0x60 m_stackable`, `+0x1D0
    // m_bagFamily`, `+0x1AC m_lockID`, etc.). Block follows the same
    // ordering as the struct so it's easy to cross-reference.
    OFF_ITEMSTATS_BUY_PRICE = 0x24,        // u32
    OFF_ITEMSTATS_ALLOWABLE_CLASS = 0x30,  // u32 — class bitmask, -1 = all
    OFF_ITEMSTATS_ALLOWABLE_RACE = 0x34,   // u32 — race bitmask, -1 = all
    OFF_ITEMSTATS_REQUIRED_LEVEL = 0x3C,   // u32
    OFF_ITEMSTATS_REQUIRED_SKILL = 0x40,   // u32 — SkillLine.dbc row
    OFF_ITEMSTATS_REQUIRED_SKILL_RANK = 0x44, // u32
    OFF_ITEMSTATS_REQUIRED_SPELL = 0x48,   // u32 — Spell.dbc row
    OFF_ITEMSTATS_REQUIRED_HONOR_RANK = 0x4C, // u32
    OFF_ITEMSTATS_REQUIRED_CITY_RANK = 0x50,  // u32
    OFF_ITEMSTATS_REQUIRED_FACTION = 0x54, // u32 — Faction.dbc row
    OFF_ITEMSTATS_REQUIRED_FACTION_RANK = 0x58, // u32 — Friendly/Honored/etc.
    OFF_ITEMSTATS_STAT_TYPE = 0x68,        // u32[10] — ItemModType enum per slot
    OFF_ITEMSTATS_STAT_VALUE = 0x90,       // i32[10] — magnitude per slot
    ITEMSTATS_STAT_SLOT_COUNT = 10,
    OFF_ITEMSTATS_DAMAGE_MIN = 0xB8,       // f32[5]
    OFF_ITEMSTATS_DAMAGE_MAX = 0xCC,       // f32[5]
    OFF_ITEMSTATS_DAMAGE_TYPE = 0xE0,      // u32[5] — schools per damage slot
    ITEMSTATS_DAMAGE_SLOT_COUNT = 5,
    OFF_ITEMSTATS_ARMOR = 0xF4,            // u32
    OFF_ITEMSTATS_RES_HOLY = 0xF8,         // u32 (six resists contiguous)
    OFF_ITEMSTATS_RES_FIRE = 0xFC,
    OFF_ITEMSTATS_RES_NATURE = 0x100,
    OFF_ITEMSTATS_RES_FROST = 0x104,
    OFF_ITEMSTATS_RES_SHADOW = 0x108,
    OFF_ITEMSTATS_RES_ARCANE = 0x10C,
    OFF_ITEMSTATS_DELAY = 0x110,           // u32 — weapon swing time (ms)
    OFF_ITEMSTATS_AMMO_TYPE = 0x114,       // u32 — ammo subclass for ranged
    OFF_ITEMSTATS_RANGED_MOD_RANGE = 0x118, // f32 — range modifier on bows/guns
    OFF_ITEMSTATS_BONDING = 0x194,         // u32 — 0=none, 1=BoP, 2=BoE, 3=BoU, 4=Quest
    OFF_ITEMSTATS_DESCRIPTION = 0x198,     // char * — flavor text (locale-applied)
    OFF_ITEMSTATS_PAGE_TEXT = 0x19C,       // u32 — PageText.dbc row (readable books)
    OFF_ITEMSTATS_LANGUAGE_ID = 0x1A0,     // u32 — language for book text
    OFF_ITEMSTATS_PAGE_MATERIAL = 0x1A4,   // u32 — book material
    OFF_ITEMSTATS_START_QUEST = 0x1A8,     // u32 — questID started by right-click
    OFF_ITEMSTATS_MATERIAL = 0x1B0,        // i32 — material type
    OFF_ITEMSTATS_SHEATH = 0x1B4,          // u32 — weapon sheath style
    OFF_ITEMSTATS_RANDOM_PROPERTY = 0x1B8, // i32 — random property template
    OFF_ITEMSTATS_BLOCK = 0x1BC,           // u32 — shield block value
    OFF_ITEMSTATS_MAX_DURABILITY = 0x1C4,  // u32
    OFF_ITEMSTATS_AREA = 0x1C8,            // u32 — bound area (AreaTable.dbc)
    OFF_ITEMSTATS_MAP = 0x1CC,             // u32 — bound map (Map.dbc)

    // `m_flags` bits we surface as decoded booleans. The OPENABLE bit
    // (0x4) is verified by the tooltip-builder's reader at 0x0052E323;
    // the others are documented in the standard 1.12 protocol
    // (CMaNGOS / TrinityCore `ITEM_FLAG_*`) and exposed as-is. Add
    // verification anchors as consumers materialize.
    ITEM_FLAG_CONJURED = 0x2,
    ITEM_FLAG_LOOTABLE = 0x10,
    ITEM_FLAG_WRAPPER = 0x200,

    // `Spell.dbc` `Name[9]` field at record +0x1E0 (9-locale string
    // array, indexed by `[VAR_LOCALE_INDEX]`). Used by `GetItemSpell`
    // to push the spell name half of its return.
    OFF_SPELL_NAMES = 0x1E0,

    // First character-pane bag-slot index. INVSLOT_BAG1 = 20, BAG2 = 21,
    // BAG3 = 22, BAG4 = 23. Used to map a Lua bagID (1..4) onto the
    // equipment slot the bag occupies: `equipSlot = INVSLOT_BAG1 + bagID - 1`.
    INVSLOT_BAG1 = 20,
    BACKPACK_NUM_SLOTS = 16,

    // ItemClass.dbc — standard 5-DWORD class shape. Records is an array of
    // record pointers indexed directly by classID. Each record has a 9-slot
    // localized name array at +0x0C (4 bytes per locale).
    VAR_ITEMCLASS_RECORDS = 0x00C0DC24,
    VAR_ITEMCLASS_COUNT = 0x00C0DC28,
    OFF_ITEMCLASS_NAMES = 0x0C,

    // ItemSubClass.dbc — class instance at 0x00C0DB90 has the standard 5-DWORD
    // shape (records at +0x08, count at +0x0C, per the reset at 0x53B700), but
    // the engine ALSO maintains a parallel (records, count) pair in the first
    // two unused slots (+0x00, +0x04) for fast compound-key iteration.
    // Script_GetItemInfo and three other callers all read from this parallel
    // pair, NOT the standard slots — verified by greping every reader of
    // 0xC0DB94. Records there is a flat array of 0x74-byte structs (not the
    // standard array-of-pointers); linear scan keyed by (classID, subClassID)
    // at record +0x00 / +0x04.
    //
    // Each record has TWO localized name string arrays:
    //   +0x28  short name (e.g. "Sword")          — 9 locale ptrs, stride 4
    //   +0x4C  verbose/display name (e.g. "One-Handed Swords") — same shape
    // Engine pattern: try verbose first; if that locale's slot is empty,
    // fall back to short. Many subclasses (e.g. Miscellaneous→Junk) only
    // populate the short array, so the fallback matters.
    VAR_ITEMSUBCLASS_RECORDS = 0x00C0DB90,
    VAR_ITEMSUBCLASS_COUNT = 0x00C0DB94,
    OFF_ITEMSUBCLASS_RECORD_STRIDE = 0x74,
    OFF_ITEMSUBCLASS_NAME = 0x28,
    OFF_ITEMSUBCLASS_DISPLAY_NAME = 0x4C,
    // record +0x00 = classID, +0x04 = subClassID (the compound key the
    // linear scan matches on). Flags field at +0x10; bit 0x200 marks the
    // subclasses whose items display by inventory slot rather than by
    // subclass name — set on exactly the armor material types (Misc / Cloth
    // / Leather / Mail / Plate), clear on weapons, shields, librams, etc.
    // This is `C_Item.GetItemSubClassInfo`'s `subClassUsesInvType` return.
    // Derived by dumping ItemSubClass.dbc (see docs/DBCs.md dump oracle).
    OFF_ITEMSUBCLASS_CLASS_ID = 0x00,
    OFF_ITEMSUBCLASS_SUBCLASS_ID = 0x04,
    OFF_ITEMSUBCLASS_FLAGS = 0x10,
    ITEMSUBCLASS_FLAG_USES_INVTYPE = 0x200,

    // ItemDisplayInfo.dbc — standard layout. Icon path char* at record +0x14
    // (per the helper at 0x005D88B0 that Script_GetItemInfo uses).
    VAR_ITEMDISPLAYINFO_RECORDS = 0x00C0DC10,
    VAR_ITEMDISPLAYINFO_COUNT = 0x00C0DC14,
    OFF_ITEMDISPLAYINFO_ICON = 0x14,

    // ItemSet.dbc — armor/equipment set definitions referenced by
    // `ItemStats_C::m_itemSet`. Standard 5-DWORD class shape at
    // 0x00C0DBB8 (records-ptr at +0x08, count at +0x0C). 45 columns,
    // 0xB4-byte record stride. Field layout derived from the per-
    // record reader at `FUN_0057BBA0`:
    //
    //   +0x00 (4)    ID
    //   +0x04 (32)   Name[8]   — localized strings, 8 LOCALES (not 9
    //                            like Spell.dbc / ChrRaces.dbc — clamp
    //                            VAR_LOCALE_INDEX to 0..7 before reading)
    //   +0x24 (4)    unused / flags
    //   +0x28 (68)   ItemID[17]      — items in the set (zero = empty slot)
    //   +0x6C (32)   SetSpellID[8]   — bonus spells granted by the set
    //   +0x8C (32)   SetThreshold[8] — pieces needed for each SetSpellID
    //                                  (e.g. {2, 4, 6, 8, 0, 0, 0, 0})
    //   +0xAC (4)    RequiredSkill   — skill line ID, 0 for none
    //   +0xB0 (4)    RequiredSkillRank
    VAR_ITEMSET_RECORDS = 0x00C0DBC0,
    VAR_ITEMSET_COUNT = 0x00C0DBC4,
    OFF_ITEMSET_NAMES = 0x04,
    OFF_ITEMSET_ITEM_IDS = 0x28,
    OFF_ITEMSET_SPELL_IDS = 0x6C,
    OFF_ITEMSET_THRESHOLDS = 0x8C,
    OFF_ITEMSET_REQUIRED_SKILL = 0xAC,
    OFF_ITEMSET_REQUIRED_SKILL_RANK = 0xB0,
    ITEMSET_MAX_ITEMS = 17,
    ITEMSET_MAX_BONUSES = 8,
    ITEMSET_LOCALE_COUNT = 8,

    // INVTYPE_* string table — array of char* indexed by m_inventoryType.
    // Index 0 is empty (0=INVTYPE_NON_EQUIP), indices 1..28 are valid
    // INVTYPE_HEAD..INVTYPE_RELIC, index 29 is empty (sentinel), and indices
    // 30+ are unrelated combat-log strings (MISS/WOUND/DODGE/...). Always
    // bound-check before indexing.
    VAR_INVTYPE_STRING_TABLE = 0x0083DDB0,
    INVTYPE_TABLE_MAX_INDEX = 28,

    // Faction "displayed list" — the engine maintains a sorted/visible list
    // of factions the player has rep with. `Script_GetNumFactions` (at
    // 0x004D64C0) returns `[VAR_FACTION_DISPLAY_COUNT]` (the primary list
    // count). `Script_GetFactionInfo` (at 0x004D64F0) maps its 1-based
    // index by calling the resolver below, which accepts a slightly wider
    // 0-based range bounded by `[VAR_FACTION_VISIBLE_MAX_INDEX]`. The two
    // ranges differ because the displayed list also exposes the "Inactive"
    // / collapsed-category rows past the primary count.
    //
    // The resolver `FUN_RESOLVE_FACTION_INDEX` is __fastcall(ecx = 0-based
    // index) → factionID (Faction.dbc record ID), or 0 for headers and
    // empty entries. The internal bounds-check is `cmp ecx, [maxIdx]; jbe`
    // — i.e., 0..maxIdx inclusive is valid. Out of range returns 0 too,
    // so we must bound-check ourselves to distinguish "header" (0 in
    // range) from "no such index" (out of range → nil).
    //
    // We re-use the resolver for `GetFactionIDByIndex` directly, and for
    // `GetFactionInfoByID` we walk it to find the displayed-index of a
    // given factionID, then replace Lua arg 1 and tail-call
    // `Script_GetFactionInfo` to produce all 11 returns.
    // ChrClasses.dbc — class metadata. Standard 5-DWORD class shape.
    // Records is a flat array of record pointers indexed by classID
    // (records[0] unused, records[1..count]).
    FUN_RESOLVE_FACTION_INDEX = 0x004D5FA0,
    FUN_SCRIPT_GET_FACTION_INFO = 0x004D64F0,
    //
    // Per-record fields used by `FillLocalizedClassList`, derived from
    // `Script_GetSelectedClass` (`0x004716E0`):
    //   +0x14   char *Name[9]    — localized class names (locale-indexed)
    //   +0x38   char *Filename   — class token ("WARRIOR", "MAGE", etc.)
    //
    // Vanilla 1.12 has no separate female-name array — `Name[9]` is
    // exactly the 36 bytes between `+0x14` and `+0x38`, so the
    // `isFemale` arg of `FillLocalizedClassList` is a no-op for this
    // client (and matches the male names that English/most locales
    // use anyway).
    VAR_CHRCLASSES_RECORDS = 0x00C0DEF4,
    VAR_CHRCLASSES_COUNT = 0x00C0DEF8,
    OFF_CHRCLASSES_NAMES = 0x14,
    OFF_CHRCLASSES_FILENAME = 0x38,
    // SpellFamilyName for the class — the gate the SpellMod system
    // matches a spell's OFF_SPELL_RECORD_FAMILY_NAME against. Read by
    // FUN_006e6ca0 (`DAT_00cecaac = ChrClasses[classID] + 0x3c`).
    OFF_CHRCLASSES_SPELL_FAMILY = 0x3C,

    // ChrRaces.dbc — standard 5-DWORD class shape at 0x00C0DED8,
    // records-pointer at +0x08, count at +0x0C. 29 columns,
    // 0x74-byte record stride. Field offsets verified by decoding
    // `Script_UnitRace` (registration entry at 0x00850580, function at
    // 0x00518200): the function loads `[VAR_CHRRACES_RECORDS]` →
    // `records[raceID]` → reads `+0x44 + locale*4` for the localized
    // race name and `+0x3C` for the non-localized file string (e.g.
    // `"Human"`, `"NightElf"`, `"Scourge"`), pushes both as Lua
    // returns. The two offsets are what we mirror to populate
    // `GetPlayerInfoByGUID`'s `localizedRace` / `englishRace` slots.
    VAR_CHRRACES_RECORDS = 0x00C0DEE0,
    VAR_CHRRACES_COUNT = 0x00C0DEE4,
    OFF_CHRRACES_FILENAME = 0x3C,
    OFF_CHRRACES_NAMES = 0x44,

    // CreatureFamily.dbc — pet/beast family metadata. Standard 5-DWORD
    // class shape (instance 0x00C0DE74; records 0x00C0DE7C, count
    // 0x00C0DE80 — the same globals `Script_UnitCreatureFamily`
    // (`0x0051A310`) bounds-checks/indexes). 72-byte stride, familyID-keyed
    // and sparse (10/13/14/22/… are unused). Localized `Name[]` at +0x20
    // (the engine reads `record + 0x20 + locale*4`), `IconFile` texture-path
    // string at +0x44 (last field; empty for warlock-pet families). Backs
    // `C_CreatureInfo.GetCreatureFamilyInfo` / `GetCreatureFamilyIDs`.
    VAR_CREATUREFAMILY_RECORDS = 0x00C0DE7C,
    VAR_CREATUREFAMILY_COUNT = 0x00C0DE80,
    OFF_CREATUREFAMILY_NAMES = 0x20,
    OFF_CREATUREFAMILY_ICON = 0x44,

    // CreatureType.dbc — pet/NPC classification (1=Beast … 11=Totem;
    // contiguous ids, 11 rows). Records 0x00C0DE2C, count 0x00C0DE30 (the
    // globals `Script_UnitCreatureType` (`0x0051A280`) bounds-checks/indexes),
    // localized `Name[]` at +0x04 (engine reads `record + 0x04 + locale*4`).
    // Backs `C_CreatureInfo.GetCreatureTypeInfo` / `GetCreatureTypeIDs`;
    // `UnitCreatureTypeID` returns the id `FUN_UNIT_CREATURE_TYPE` resolves.
    VAR_CREATURETYPE_RECORDS = 0x00C0DE2C,
    VAR_CREATURETYPE_COUNT = 0x00C0DE30,
    OFF_CREATURETYPE_NAMES = 0x04,

    // CreatureDisplayInfo.dbc / CreatureModelData.dbc — the
    // creatureDisplayID → model-file chain behind `Model:SetDisplayInfo`.
    // Both standard pointer-array DBCs (records[id], sparse). Column offsets
    // verified two independent ways: parsing the extracted 1.12 DBCs
    // (C:\WoW\Octo\DBFilesClient) AND the engine's own unit-display resolver
    // FUN_0060afb0, which reads displayID from `[descriptor+0x1F4]` →
    // CreatureDisplayInfo[id] +0x04 ModelID → CreatureModelData[ModelID].
    //   CreatureDisplayInfo: +0x04 ModelID (col 1 → CreatureModelData id);
    //     +0x18/+0x1C/+0x20 TextureVariation[3] (cols 6/7/8 — creature skin,
    //     bare filenames applied as replaceable-texture MONSTER_1/2/3).
    //   CreatureModelData: +0x08 ModelPath (col 2, e.g. "Creature\Rat\Rat.mdx").
    VAR_CREATUREDISPLAYINFO_RECORDS = 0x00C0DE90,
    VAR_CREATUREDISPLAYINFO_COUNT = 0x00C0DE94,
    OFF_CREATUREDISPLAYINFO_MODEL_ID = 0x04,
    OFF_CREATUREDISPLAYINFO_TEXTURE_VARIATION = 0x18, // char*[3] @ +0x18/+0x1C/+0x20
    VAR_CREATUREMODELDATA_RECORDS = 0x00C0DE68,
    VAR_CREATUREMODELDATA_COUNT = 0x00C0DE6C,
    OFF_CREATUREMODELDATA_MODEL_PATH = 0x08,

    // CreatureDisplayInfo.extendedDisplayInfoID (col 3) — nonzero for a
    // CHARACTER-based display (shared Character\Race\Sex base model). It indexes
    // CreatureDisplayInfoExtra.dbc, whose LAST field (col 18 in the 19-col
    // vanilla layout, +0x48) is the pre-composited body-skin filename. That
    // texture lives under Textures\BakedNpcTextures\ and applies to the M2
    // body-skin replaceable types (1/8) — NOT the monster slots, which a
    // character model has no texture units for (hence "renders white" without
    // this). The baked skin already carries the equipped body armor. Verified by
    // parsing the extracted DBC; C:\Git\Inklab resolves the same chain.
    OFF_CREATUREDISPLAYINFO_EXTENDED_ID = 0x0C,
    VAR_CREATUREDISPLAYINFOEXTRA_RECORDS = 0x00C0DEA4,
    VAR_CREATUREDISPLAYINFOEXTRA_COUNT = 0x00C0DEA8,
    OFF_CREATUREDISPLAYINFOEXTRA_BAKE_NAME = 0x48,  // col 18 (last), string
    OFF_CREATUREDISPLAYINFOEXTRA_RACE = 0x04,       // col 1 (ChrRaces id)
    OFF_CREATUREDISPLAYINFOEXTRA_SEX = 0x08,        // col 2 (0=male, 1=female)
    OFF_CREATUREDISPLAYINFOEXTRA_HAIR_COLOR = 0x18, // col 6 (CharSections color)
    // Cols 3/4/5/7 + the NPC equipment block — field order verified from
    // CGUnit_C::UpdateCharacterCustomization (FUN_005fb200), which reads
    // +0x04..+0x1C into the compositor info struct and walks +0x20..+0x44
    // as 10 ItemDisplayInfo ids for FUN_CHARCOMP_SET_ITEM.
    OFF_CREATUREDISPLAYINFOEXTRA_SKIN = 0x0C,        // col 3
    OFF_CREATUREDISPLAYINFOEXTRA_FACE = 0x10,        // col 4
    OFF_CREATUREDISPLAYINFOEXTRA_HAIR_STYLE = 0x14,  // col 5
    OFF_CREATUREDISPLAYINFOEXTRA_FACIAL_HAIR = 0x1C, // col 7
    OFF_CREATUREDISPLAYINFOEXTRA_EQUIP = 0x20,       // cols 8..17: u32[10] ItemDisplayInfo ids

    // CharSections.dbc — per-race/sex character texture layers. Scanned by
    // Model:SetDisplayInfo to resolve a character display's HAIR texture (applied
    // to the M2 hair replaceable type 6, else the hair geoset renders white).
    // Standard pointer-array DBC; match rows on race/sex/baseSection (3 = Hair) +
    // color, then read the first texture column. Verified against the extracted
    // DBC: Human Female (race 1, sex 1) hairColor 0 -> "Character\Human\Hair00_00.blp".
    VAR_CHARSECTIONS_RECORDS = 0x00C0DF6C,
    VAR_CHARSECTIONS_COUNT = 0x00C0DF70,
    OFF_CHARSECTIONS_RACE = 0x04,          // col 1
    OFF_CHARSECTIONS_SEX = 0x08,           // col 2
    OFF_CHARSECTIONS_BASE_SECTION = 0x0C,  // col 3 (3 = Hair)
    OFF_CHARSECTIONS_COLOR = 0x14,         // col 5
    OFF_CHARSECTIONS_TEXTURE = 0x18,       // col 6 (texture[0] path)
    CHARSECTIONS_BASE_SECTION_HAIR = 3,

    // Race → faction group, mirroring the player branch of
    // `Script_UnitFactionGroup` (`0x00516630`) — backs
    // `C_CreatureInfo.GetFactionInfo(raceID)`. Chain:
    //   ChrRaces[race] + 0x08          → FactionTemplate id
    //   FactionTemplate[id] + 0x0C     → group mask (ourMask)
    //   scan FactionGroup for the row whose bit is set (and whose
    //     localized name is non-empty — that non-empty test is what
    //     skips the always-set "Player" bit and lands on Alliance/Horde)
    //   push { groupTag = row+0x08 (english), name = row+0x0C+locale*4 }
    // FactionTemplate is a pointer-array DBC (records[id]); FactionGroup
    // is stored CONTIGUOUSLY (records-base + i*stride, direct field reads,
    // NOT a pointer array), so iterate it by hand rather than via DBC::Record.
    // FactionTemplate globals match docs/DBCs.md; the FactionGroup
    // records-base/count globals are the ones the engine actually
    // dereferences per the disassembly (0x00C0DD5C / 0x00C0DD60), which
    // differ from DBCs.md's standard-shape tabulation for this table.
    OFF_CHRRACES_FACTION_TEMPLATE = 0x08,
    VAR_FACTIONTEMPLATE_RECORDS = 0x00C0DD3C,
    VAR_FACTIONTEMPLATE_COUNT = 0x00C0DD40,
    OFF_FACTIONTEMPLATE_GROUP_MASK = 0x0C,
    VAR_FACTIONGROUP_RECORDS = 0x00C0DD5C,
    VAR_FACTIONGROUP_COUNT = 0x00C0DD60,
    FACTIONGROUP_STRIDE = 0x30,
    OFF_FACTIONGROUP_BIT = 0x04,
    OFF_FACTIONGROUP_ENGLISH = 0x08,
    OFF_FACTIONGROUP_NAMES = 0x0C,

    // AreaTable.dbc — used by `Script_GetCharacterInfo` to resolve
    // each character's last-known-area to a localized zone name.
    // Same shape as the other DBCs (records pointer + count globals,
    // 9-slot locale-string array at the per-record offset).
    VAR_AREATABLE_RECORDS = 0x00C0E048,
    VAR_AREATABLE_COUNT = 0x00C0E04C,
    OFF_AREATABLE_NAMES = 0x2C,
    // Parent AreaTable id @ +0x08 — a sub-area's enclosing zone (e.g. Goldshire
    // → Elwynn Forest). GetFriendInfo resolves it to name the zone, not the
    // sub-area. 0 for a top-level zone.
    OFF_AREATABLE_PARENT_ID = 0x08,
    // AreaBit @ +0x0C: index into the player's explored-areas bitfield
    // (OFF_PLAYER_EXPLORED_BITS). Gate @ +0x28: when < 0 the area needs no
    // exploration (always counts explored); when >= 0 the AreaBit check
    // applies. Source: the discovered-overlay rebuild FUN_004a67a0.
    OFF_AREATABLE_AREA_BIT = 0x0C,
    OFF_AREATABLE_EXPLORE_GATE = 0x28,

    // TaxiNodes.dbc (16 fields, 0x40 bytes) — flight points. records/count
    // per docs/DBCs.md. mapID @ +0x04 is the Map.dbc continent; x/y/z floats;
    // Name[8] localized @ +0x14; the two MountCreatureID columns are
    // [Horde @ +0x38, Alliance @ +0x3C] (0 = that faction has no flight
    // master at this node — both 0 = a transport/system waypoint).
    VAR_TAXINODES_RECORDS = 0x00C0D6C0,
    VAR_TAXINODES_COUNT = 0x00C0D6C4,
    OFF_TAXINODE_MAP_ID = 0x04,
    OFF_TAXINODE_X = 0x08,
    OFF_TAXINODE_Y = 0x0C,
    OFF_TAXINODE_Z = 0x10,
    OFF_TAXINODE_NAME = 0x14,
    OFF_TAXINODE_HORDE_MOUNT = 0x38,
    OFF_TAXINODE_ALLIANCE_MOUNT = 0x3C,

    // Live taxi session — the open flight master's reachable node list,
    // populated between TAXIMAP_OPENED/CLOSED (count 0 otherwise). Backs the
    // legacy NumTaxiNodes/TaxiNodeName/TaxiNodePosition/TaxiNodeGetType Lua
    // globals. Node slots are 0x20-stride records at VAR_TAXI_NODE_ARRAY:
    // +0x00 = pointer to the node's TaxiNodes.dbc record (id @ +0, name @
    // +0x14), +0x04 = flight-map x (0-1 float), +0x08 = y. Count at
    // VAR_TAXI_NODE_COUNT. Derived from Script_TaxiNode* (FUN_004dc380 name,
    // FUN_004dc3b0 position); the reachability type comes from
    // FUN_TAXI_NODE_TYPE.
    VAR_TAXI_NODE_COUNT = 0x00BB4AA8,
    VAR_TAXI_NODE_ARRAY = 0x00BB4F10,
    TAXI_NODE_STRIDE = 0x20,
    OFF_TAXI_SLOT_RECORD = 0x00,
    OFF_TAXI_SLOT_X = 0x04,
    OFF_TAXI_SLOT_Y = 0x08,
    // __fastcall(uint slot0Based) -> const char* ("CURRENT" / "REACHABLE" /
    // "DISTANT" / other) — the node's reachability for the open session.
    FUN_TAXI_NODE_TYPE = 0x004DC4E0,

    // Per-slot computed route (the chain that TakeTaxiNode sends to the
    // server). Derived from FUN_004dc8d0 (the TakeTaxiNode worker) and the
    // route builder FUN_004dbce0:
    //   +0x0C (byte)  nonzero when the multi-hop route array below is built
    //   +0x14 (int*)  array of TaxiNodes.dbc IDs, current->dest in flight
    //                 order, length at +0x18
    //   +0x18 (int)   route length (node count; hops = count - 1)
    // A direct hop (a TaxiPath edge exists current->dest) is flown as just
    // {current, dest} and does NOT populate +0x14 — see VAR_TAXI_ADJACENCY.
    OFF_TAXI_SLOT_MULTIHOP = 0x0C,
    OFF_TAXI_SLOT_ROUTE = 0x14,
    OFF_TAXI_SLOT_ROUTE_COUNT = 0x18,

    // Pointer to the CURRENT flight master's TaxiNodes.dbc record (id @ +0);
    // the origin of every route. Used by FUN_004dc8d0 / FUN_004dbce0.
    VAR_TAXI_CURRENT_REC = 0x00BB4A80,

    // Node x node -> pathID adjacency matrix (256-wide rows, int cells; cell
    // [from*0x100 + to] is the direct TaxiPath id, or 0 if none). Backs the
    // direct-hop test FUN_004dbad0. IDs are 1..256.
    VAR_TAXI_ADJACENCY = 0x00B7365C,
    TAXI_ADJACENCY_DIM = 0x100,

    // TaxiPath.dbc (4 fields, 0x10 bytes): {id, fromNode@+0x04, toNode@+0x08,
    // cost}. A real flight master is a node some flight path connects to;
    // Northshire Abbey / spurious duplicate rows / standalone markers are not
    // endpoints and so are filtered out of GetTaxiNodesForMap.
    VAR_TAXIPATH_RECORDS = 0x00C0D698,
    VAR_TAXIPATH_COUNT = 0x00C0D69C,
    OFF_TAXIPATH_FROM = 0x04,
    OFF_TAXIPATH_TO = 0x08,
    OFF_TAXIPATH_COST = 0x0C,

    // TaxiPathNode.dbc — the per-path flight waypoints. 1-based
    // record-pointer array (records/count slots per docs/DBCs.md). Record
    // (verified against the extracted DBC + the loading-screen spline
    // reader FUN_004075A0, which reads mapID@+0xC, X@+0x10, Y@+0x14):
    //   {ID@+0, pathID@+4, nodeIndex@+8, mapID@+0xC,
    //    X@+0x10, Y@+0x14, Z@+0x18 (float), flags@+0x1C, delay@+0x20}
    // Waypoints of a path share pathID and order by nodeIndex.
    VAR_TAXIPATHNODE_RECORDS = 0x00C0D6AC,
    VAR_TAXIPATHNODE_COUNT = 0x00C0D6B0,
    OFF_TAXIPATHNODE_PATH_ID = 0x04,
    OFF_TAXIPATHNODE_INDEX = 0x08,
    OFF_TAXIPATHNODE_MAP = 0x0C,
    OFF_TAXIPATHNODE_X = 0x10,
    OFF_TAXIPATHNODE_Y = 0x14,
    OFF_TAXIPATHNODE_Z = 0x18,

    // Engine player-info cache — populated by the
    // SMSG_NAME_QUERY_RESPONSE handler (opcode 0x51) at 0x005551A0,
    // which reads (GUID, name[48], realm[256], race, sex, class) from
    // the packet and writes them into an entry via the cache method
    // at 0x0055F310. Used by chat, raid frames, guild events — any
    // engine code that needs name/class/race for a GUID. NOT limited
    // to visible objects (an earlier note in TODO #35 was wrong — the
    // visible-only cache at 0x00CE870C is a different smaller one
    // used for chat name resolution).
    //
    // Lookup primitive at `FUN_PLAYER_INFO_LOOKUP`:
    //   `__thiscall uint8_t *(this=cache, guid_lo, guid_hi, &cookie,
    //                          callback, userData, retryFlag)`
    //
    // Returns:
    //   - `nullptr` if the GUID isn't loaded (cache miss, or pending
    //     response from a prior request).
    //   - Pointer to the entry's data block (= entry +0x20) if loaded.
    //
    // Side effect: if `callback != nullptr` AND the entry isn't yet
    // allocated, the engine builds a CDataStore, writes the GUID, and
    // calls `FUN_005AB630` to send CMSG_NAME_QUERY (opcode 0x50). The
    // callback fires with `(userData, success)` when the response
    // arrives (`__stdcall void(void *, int)`, same shape as the item
    // cache callback).
    //
    // For pure cache reads (our `GetPlayerInfoByGUID`), pass
    // `callback=nullptr, userData=nullptr, retryFlag=0` and a dummy
    // 8-byte cookie buffer. We don't fire the request from the read
    // path — addons that want to trigger one can layer a
    // `C_PlayerInfo.RequestLoadPlayerByID`-style helper on top later.
    VAR_PLAYER_NAME_CACHE = 0x00C0E228,
    FUN_PLAYER_INFO_LOOKUP = 0x0055F080,

    // NameCache write entry — `__thiscall(this = cache, packetData,
    // guidLo, guidHi)` at 0x0055F310. Called once per
    // SMSG_NAME_QUERY_RESPONSE after the engine's opcode handler
    // builds a `packetData` buffer with the layout:
    //   +0x000  name[48]
    //   +0x030  realm[256]
    //   +0x130  unknown[8]   (probably (charGuidLo, charGuidHi))
    //   +0x138  race  (u32)
    //   +0x13C  sex   (u32, 0/1 wire)
    //   +0x140  class (u32)
    // Writes that data into the cache entry and dispatches any
    // pending callbacks. Decoded by Ghidra; field offsets verified
    // against the `*(uint*)(param_2 + N)` reads in the function body.
    //
    // Hooking here gives us a single chokepoint covering every name-
    // query response the engine processes — chat, group/raid sync,
    // guild roster, visible-object resolution. The hook runs after
    // the engine has settled the entry, so reading from the cache
    // post-hook is safe.
    FUN_PLAYER_INFO_CACHE_WRITE = 0x0055F310,

    // Entry-data offsets. The lookup returns `entry + 0x20`, so these
    // are relative to that pointer (not to the entry's hash-table
    // header). Field layout verified by decoding the cache writer
    // FUN_0055F310 at 0x0055F32D-0x0055F365:
    //   - copy 48 bytes from packet[+0]   into entry+0x20   (name)
    //   - copy 256 bytes from packet[+0x30] into entry+0x50 (realm)
    //   - copy 4 bytes from packet[+0x138] into entry+0x158 (race)
    //   - copy 4 bytes from packet[+0x13C] into entry+0x15C (sex)
    //   - copy 4 bytes from packet[+0x140] into entry+0x160 (class)
    OFF_PLAYER_INFO_NAME = 0x000,   // 48-byte inline C string
    OFF_PLAYER_INFO_REALM = 0x030,  // 256-byte inline C string
    OFF_PLAYER_INFO_RACE = 0x138,   // u32 (race ID 1..8)
    OFF_PLAYER_INFO_SEX = 0x13C,    // u32 (0=male, 1=female on wire)
    OFF_PLAYER_INFO_CLASS = 0x140,  // u32 (class ID 1..11)

    // Visible-object iterator. Walks the engine's linked list of
    // currently-in-range CGObjects (the same list the minimap blip
    // renderer uses). __fastcall(ecx = callback, edx = context).
    // Callback signature: __fastcall(ecx = context, edx unused, [stack] = uint64_t guid).
    // Callback returns 0 to stop iteration, 1 to continue.
    // Verified by disassembling the iterator body at 0x00468380 —
    // ECX/EDX preserved into ESI/EDI, ECX restored to context before
    // each callback invocation, guidLo/guidHi pushed as 8 stack bytes.
    FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS = 0x00468380,

    // Type-mask flags accepted by FUN_OBJECT_RESOLVE_BY_GUID (and the SetUnit
    // path FUN_00529FE0, which passes ECX=8 for UNIT). A bitmask of
    // object-type bits, not an enum index — single-bit flags OR together.
    // PLAYER (0x10) is what the bag observer FUN_004F8DB0 passes; GAMEOBJECT
    // (0x20) what the hover-tooltip populator FUN_0052AA20 passes.
    TYPEMASK_OBJECT        = 0x01,
    TYPEMASK_ITEM          = 0x02,
    TYPEMASK_CONTAINER     = 0x04,
    TYPEMASK_UNIT          = 0x08,
    TYPEMASK_PLAYER        = 0x10,
    TYPEMASK_GAMEOBJECT    = 0x20,
    TYPEMASK_DYNAMICOBJECT = 0x40,
    TYPEMASK_CORPSE        = 0x80,

    // CGUnit_C.m_objectFields slot. Different from CGItem's +0x114 —
    // sibling classes use class-specific descriptor offsets. Inside the
    // descriptor at +0x79 lives the class byte (UNIT_FIELD_BYTES_0
    // field's second byte). Verified at 0x005183FE-0x00518404 in
    // Script_UnitClass's general-unit-token path:
    //   mov edx, [eax + 0x110]      ; descriptor
    //   movzx eax, byte [edx + 0x79] ; class
    OFF_CGUNIT_OBJECT_FIELDS = 0x110,
    // UNIT_FIELD_BYTES_0 packs race/class/sex/powerType into one dword
    // at descriptor +0x78. Individual bytes:
    OFF_UNIT_DESCRIPTOR_RACE_BYTE = 0x78,
    OFF_UNIT_DESCRIPTOR_CLASS_BYTE = 0x79,
    OFF_UNIT_DESCRIPTOR_SEX_BYTE = 0x7A,

    // The local player's UNIT_FIELD_BYTES_0 race/class/sex bytes, mirrored
    // into a login-time global that is populated at character-enter —
    // BEFORE the in-world CGUnit/descriptor exists. Script_UnitClass
    // (0x00518350) and Script_UnitRace special-case the literal "player"
    // token to read these (Script_UnitClass calls FUN_005abde0 →
    // `return DAT_00c27e81`), which is why UnitClass("player") resolves at
    // addon-load while a descriptor read returns nil until the player
    // object spawns. Getters: race FUN_005abdd0, class FUN_005abde0,
    // sex FUN_005abdf0.
    VAR_PLAYER_RACE_BYTE = 0x00C27E80,
    VAR_PLAYER_CLASS_BYTE = 0x00C27E81,
    VAR_PLAYER_SEX_BYTE = 0x00C27E82,

    // SkillLineAbility.dbc — maps each (class, race) pair to its
    // learnable spell list with skill-rank gating. Indexed by record
    // ID; reads via the standard `records[id]` pattern. Used by
    // `GetCurrentLevelSpells` to filter the global spell set down to
    // entries our class/race can learn.
    //
    // Class instance VA `0x00C0D94C` per docs/DBCs.md. Standard 5-DWORD
    // class shape so the records-array pointer slot sits at +0x08 and
    // the count slot at +0x0C.
    VAR_SKILL_LINE_ABILITY_RECORDS = 0x00C0D954,
    VAR_SKILL_LINE_ABILITY_COUNT = 0x00C0D958,

    // Record-internal offsets, vanilla 1.12 layout (CMaNGOS-canonical):
    //   +0x00 id
    //   +0x04 skillId       (→ SkillLine.dbc; e.g. 44 = "Swords")
    //   +0x08 spellId       (→ Spell.dbc — the spell taught by this entry)
    //   +0x0C raceMask      (bit `1 << (race - 1)`; 0 = all races)
    //   +0x10 classMask     (bit `1 << (class - 1)`; 0 = all classes)
    //   +0x14 excludeRace   (bitmask of races this entry does NOT apply to)
    //   +0x18 excludeClass  (bitmask of classes this entry does NOT apply to)
    //   +0x1C minSkillRank
    //   ... (more fields)
    OFF_SLA_SKILL_ID = 0x04,
    OFF_SLA_SPELL_ID = 0x08,
    OFF_SLA_RACE_MASK = 0x0C,
    OFF_SLA_CLASS_MASK = 0x10,
    OFF_SLA_EXCLUDE_RACE = 0x14,
    OFF_SLA_EXCLUDE_CLASS = 0x18,
    // Skill-up threshold ranks (grey / green) at record fields 10/11.
    // A row with either nonzero is a craftable recipe; profession
    // rank-up / passive rows have both zero. Empirically: the
    // Blacksmithing skill line has 343 SLA rows, 334 with a nonzero
    // threshold — the 9 excluded are the Apprentice..Artisan rank
    // spells and skill grants. This is the discriminator
    // `TradeSkill::Link` uses to build the canonical recipe universe.
    OFF_SLA_TRIVIAL_SKILL_HIGH = 0x28,
    OFF_SLA_TRIVIAL_SKILL_LOW = 0x2C,

    // SkillLine.dbc — the skill/category each SLA row points into via
    // its `skillId` field. Each record carries a 9-locale localized
    // name array (the user-facing tab name in the spellbook, profession
    // header, weapon-skill name, etc.) plus a SpellIcon ID at +0x54.
    // Standard 5-DWORD class instance at `0x00C0D924`; records-array
    // pointer at `0x00C0D92C`, count at `0x00C0D930`.
    //
    // Record layout (verified against `Script_GetSpellTabInfo` at
    // `0x004B3CE0`):
    //   +0x00 id
    //   +0x0C `Name[9]` — locale string pointers, indexed via the
    //         global locale at `0x00C0E080` (read as
    //         `*(char **)(record + 0x0C + locale * 4)`)
    //   +0x54 SpellIcon.dbc ID (looked up via `SpellIcon` for the
    //         spellbook tab/profession icon)
    VAR_SKILL_LINE_RECORDS = 0x00C0D92C,
    VAR_SKILL_LINE_COUNT = 0x00C0D930,
    OFF_SKILL_LINE_NAME = 0x0C,

    // Player's known-skills table — separate from the DBC. Populated
    // by the engine on login from server data; entries are pointers
    // to per-skill records. Each record's `+0x00` is the
    // `SkillLine.dbc` row (or `0` for header / category rows), and
    // `+0x10` is the current skill rank (other rank/modifier fields
    // follow). Source: `Script_GetSkillLineInfo` at `0x004D3610`,
    // which walks this array indexed by the Lua arg.
    VAR_PLAYER_SKILL_LIST = 0x00B7318C,
    VAR_PLAYER_SKILL_COUNT = 0x00B731B8,
    OFF_PLAYER_SKILL_LINE_ID = 0x00,
    OFF_PLAYER_SKILL_RANK = 0x10,

    // Live per-skill current/max rank. These are NOT in
    // `VAR_PLAYER_SKILL_LIST` (that entry's `+0x10` is numTempPoints) —
    // the real ranks live in the CGPlayer `+0xE68` sub-struct
    // (`OFF_CGPLAYER_INFO`). `FUN_SKILL_LINE_TO_SLOT`
    // (`__thiscall(player, skillLineID) -> slot`) maps a SkillLine.dbc
    // row to its slot; the slot indexes a `SKILL_INFO_STRIDE`-byte
    // record at `[player+0xE68] + OFF_SKILL_INFO_TABLE + slot*stride`:
    // current `+0x0`, max `+0x2`, rank bonus `+0x6` (all u16 — add the
    // bonus when the base value is nonzero). Source: `Script_GetTradeSkillLine`
    // (`0x004FDD40`) / `Script_GetSkillLineInfo` (`0x004D3610`). Verified
    // in-game vs. those functions (Tailoring 261/300, Enchanting 186/240).
    FUN_SKILL_LINE_TO_SLOT = 0x005EA3F0,
    OFF_SKILL_INFO_TABLE = 0x84C,
    SKILL_INFO_STRIDE = 0xC,
    OFF_SKILL_INFO_CUR = 0x0,
    OFF_SKILL_INFO_MAX = 0x2,
    OFF_SKILL_INFO_BONUS = 0x6,

    // Per-itemClass proficiency bitmap maintained by the engine.
    // 17 u32 entries (indexed by Item.dbc `m_class`, 0..16). Each
    // entry is a bitmask where bit N is set if the player has
    // proficiency in subclass N of that item class — i.e.
    // `table[4] & (1 << 3)` = "can wear Mail" (armor=class 4,
    // mail=subclass 3).
    //
    // Populated by SMSG_SET_PROFICIENCY's handler at
    // `FUN_005E7B70`, which writes `table[itemClass] = mask`
    // verbatim from the packet. Same source the engine itself uses
    // for equip eligibility (the tooltip "Mail" red-text logic
    // and the cursor-drop-on-paperdoll check both read this). Note:
    // covers **weapons too** (class 2), making it strictly more
    // general than walking the per-skill-line table for individual
    // armor proficiencies.
    VAR_PROFICIENCY_TABLE = 0x00C4D4A0,

    // Spell.dbc `BaseLevel` — the level a spell becomes available
    // (trainer offers it, quest reward grants it, etc.). Distinct
    // from `SpellLevel` at +0x74 which is the effective level used
    // for damage scaling and resistance math. Per CMaNGOS-canonical
    // 1.12 Spell.dbc layout; cross-checked against known fields at
    // ±a few offsets (PowerType at +0x7C is verified).
    OFF_SPELL_RECORD_BASE_LEVEL = 0x70,

    // `UNIT_BYTES_1` (CMaNGOS field 132 in the 1.12.1 layout). 32-bit
    // composite field; the low byte is the standstate (standing /
    // sitting / sleeping / kneeling / etc.), the upper bytes hold
    // PetTalents / VisFlag / AnimTier per CMaNGOS docs (uncalibrated
    // for 1.12). Broadcast field, so the standstate works for any
    // synced unit (player, target, party/raid, inspect targets) —
    // not local-only.
    //
    // Verified via the `IsAssistingRitual` development session by
    // observing `0xEE00 → 0xEE05` on `/sit` over a chair (the medium-
    // chair sit value 5).
    //
    // Byte 3 (the AnimTier / misc-flags byte, mask 0x02000000 on the
    // u32) is the **stealth / sneak flag**: set while the unit is in
    // the sneaking anim tier (Rogue Stealth, Druid Prowl, Shadowmeld),
    // which is what drives the client's semi-transparent render of a
    // stealthed unit. Verified via `_classicapi_DiffAll`: a Prowl
    // toggle from a Cat-Form baseline flips byte 3 `0x00 → 0x02` while
    // every other descriptor change is aura-array slot metadata — so
    // this is the only standalone broadcast bit that means "stealthed".
    // Read by `IsStealthed()`.
    OFF_UNIT_FIELD_BYTES_1 = 0x210,
    UNIT_BYTES_1_STEALTH_FLAG = 0x02000000,

    // `__fastcall(void)` engine helper that refreshes the bonus
    // action bar from the local player's current shapeshift form
    // (byte 2 of `UNIT_BYTES_1`, descriptor `+0x212`). Reads the form
    // byte, looks up SpellShapeshiftForm.dbc's `bonusActionBar`
    // column, stores it into `DAT_00BC6E88`, and fires
    // `UPDATE_BONUS_ACTIONBAR` (event id `0xD9`).
    //
    // Called from two sites: once during post-login init from
    // `FUN_004908C0`, and on every relevant UNIT_BYTES_1 broadcast
    // update (the unrooted call at `0x005DE01B`, inside the
    // local-player UpdateField dispatch). Hooked to synthesize
    // modern `UPDATE_SHAPESHIFT_FORM` — the cached-byte gate in our
    // hook filters out the non-form recomputes (this fires for any
    // UNIT_BYTES_1 update, not just form changes).
    FUN_BONUS_ACTIONBAR_UPDATE = 0x004E4FC0,
    // Inner watched-faction setter — `__fastcall(ecx = factionID) → void`.
    // The engine's `Script_SetWatchedFactionIndex` (0x004D6B60) is a
    // thin Lua-side wrapper that takes a 1-based displayed-list index,
    // resolves it to a factionID via FUN_RESOLVE_FACTION_INDEX, and
    // forwards to this. Going through the wrapper requires a round-
    // trip through the resolver (and excludes unencountered factions),
    // so for `C_Reputation.SetWatchedFactionByID` we call this
    // directly with the user-supplied factionID. factionID == 0
    // clears the watched faction (the engine handles 0 as "no
    // watched"). Updates `[player+0xE68]+0x10C4` and persists the
    // value via the engine's CVar / event machinery.
    FUN_PLAYER_SET_WATCHED_FACTION = 0x004D6240,
    VAR_FACTION_DISPLAY_COUNT = 0x00B73764,
    VAR_FACTION_VISIBLE_MAX_INDEX = 0x00B73760,

    // Faction.dbc — standard 5-DWORD class shape, records pointer at +0x08
    // and count at +0x0C of the class instance at 0x00C0DD48. Records is an
    // array of `FactionRec *` indexed directly by factionID (1-based;
    // records[0] is unused). We read it on the slow path of
    // `GetFactionInfoByID` for factions the player has never encountered
    // (i.e. not in the displayed reputation list), where the engine's
    // `Script_GetFactionInfo` can't help us.
    //
    // Record name/description offsets verified by disassembling
    // `Script_GetFactionInfo` at 0x004D6562 / 0x004D6573:
    //   mov edx, [edi+ecx*4+0x4C] ; Name[locale]
    //   mov edx, [edi+edx*4+0x70] ; Description[locale]
    // Each is a 9-entry array of locale string pointers, indexed by
    // `[VAR_LOCALE_INDEX]` (0..8).
    VAR_FACTION_DBC_RECORDS = 0x00C0DD50,
    VAR_FACTION_DBC_COUNT = 0x00C0DD54,
    // RepListIndex on the Faction.dbc record. Maps factionID to the
    // 0..63 slot in `VAR_PLAYER_REP_SLOTS`. Read by the engine's
    // factionID→repListID helper at `0x004D5600` (called by
    // FUN_FACTION_GET_AT_WAR / GET_CAN_TOGGLE_AT_WAR / IS_WATCHED).
    // -1 means the faction has no rep slot (it's a category header).
    OFF_FACTION_REP_LIST_INDEX = 0x04,
    OFF_FACTION_PARENT_ID = 0x48,
    OFF_FACTION_NAMES = 0x4C,
    OFF_FACTION_DESCRIPTIONS = 0x70,

    // Displayed-list category headers. `VAR_FACTION_HEADER_LIST` is a
    // flat int32 array of header factionIDs (max 32 — engine bounds
    // it at `0x1f`). `VAR_FACTION_HEADER_COUNT` is the live count.
    // Special sentinel factionIDs `0` (FACTION_OTHER) and `-1`
    // (FACTION_INACTIVE) may appear in this list for the "Other" and
    // "Inactive" pseudo-rows.
    //
    // `VAR_FACTION_COLLAPSED_BITMASK` is a 32-bit mask where bit `i`
    // SET means the header at `VAR_FACTION_HEADER_LIST[i]` is
    // **expanded** (NOT collapsed) — bit CLEAR = collapsed. Default
    // state at login is all-clear (all categories collapsed).
    VAR_FACTION_HEADER_LIST = 0x00B736C0,
    VAR_FACTION_HEADER_COUNT = 0x00B736B0,
    VAR_FACTION_COLLAPSED_BITMASK = 0x0084A0A4,
    MAX_FACTION_HEADERS = 32,

    // The social singleton (0x00C28168) — one object shared by the friend
    // list, ignore list, and /who system. Layout: friend entries inline from
    // offset 0 (stride 0x20, up to 50; each is {connected@+0x00, name char*
    // @+0x04, GUID u64 @+0x08, level@+0x10, area@+0x14, class@+0x18}), the
    // 25-entry ignore-GUID table at +0x650, and the who-query state. Read by
    // GetNumFriends/GetFriendInfo, GetNumIgnores, and Script_SendWho.
    //
    // `Script_SendWho` (0x005AD3B0) is a 32-byte wrapper:
    //   - validate arg1 is a string via lua_isstring
    //   - load the singleton from `[VAR_SOCIAL_SYSTEM]`
    //   - if non-NULL, lua_tostring(L, 1) for the query string
    //   - tail-call `FUN_WHO_SYSTEM_SEND_QUERY` with
    //     `__thiscall(this = WhoSystem, queryStr)`
    //
    // The query string follows the engine's standard /who filter syntax
    // (`"n-Bob"`, `"r-Alliance"`, `"c-Warlock"`, etc.); the inner
    // function at `FUN_WHO_SYSTEM_SEND_QUERY` (~1.5KB) does the
    // parse + CMSG_WHO build + network send.
    //
    // `Script_SetWhoToUI` (0x005AD870) is even smaller — 13 bytes that
    // store arg1 (as int, default 0) to `[VAR_WHO_TO_UI_FLAG]`. The
    // SMSG_WHO response handler at ~0x005ADF80 reads the flag at
    // `0x005ADF9C` to decide whether the results buffer into the
    // engine's WhoList (flag != 0 → list path, friends-frame fires
    // WHO_LIST_UPDATE; flag == 0 → results go to chat as
    // `"Found N players matching..."`).
    //
    // Server-side cooldown for CMSG_WHO is ~5 seconds — a faster
    // client gets silent-dropped, so any client-side gating just
    // matches that.
    VAR_SOCIAL_SYSTEM = 0x00C28168,
    VAR_WHO_TO_UI_FLAG = 0x00C2A12C,
    FUN_WHO_SYSTEM_SEND_QUERY = 0x005AEBB0,
    // Friend count: `__fastcall(socialSingleton) -> uint` — walks the inline
    // entries and returns the number of populated friend slots. Backs
    // GetNumFriends and C_FriendList.IsFriend.
    FUN_FRIEND_LIST_COUNT = 0x005AE490,

    // SMSG_WHO opcode handler — opcode 0x63 (99) per the registration
    // in `FUN_005adc50`: `FUN_005ab650(99, FUN_005adf60, 0)`. Reads
    // `VAR_WHO_TO_UI_FLAG` mid-function (at `0x005ADF9C`) to decide
    // between WhoList + WHO_LIST_UPDATE vs the chat-count printer.
    //
    // We hook post-original to restore the flag after each response
    // so the routing override only affects responses we initiated.
    // Otherwise a user-typed `/who` inheriting our flag=1 stays
    // silent (no chat-count message, no FriendsFrame popup) when the
    // user actually wanted both.
    FUN_SMSG_WHO_RESPONSE = 0x005ADF60,

    // /who result list — the buffer `Script_GetWhoInfo` (0x005AD6E0) and
    // `Script_GetNumWhoResults` (0x005AD690) read. An array of fixed-size
    // entries at `VAR_WHO_RESULTS`, stride `WHO_RESULT_STRIDE`; the field
    // offsets below are all relative to an entry. `name`/`guildName` are
    // inline C strings; `race`/`class` index ChrRaces/ChrClasses.dbc and
    // `zone` indexes AreaTable.dbc (own name, not parent-resolved).
    // `VAR_WHO_RESULT_COUNT` is the displayed count (the first of the two
    // values GetNumWhoResults returns; the second, total matches, is at +4).
    // Verified by decompiling both accessors.
    VAR_WHO_RESULTS = 0x00C281B8,
    VAR_WHO_RESULT_COUNT = 0x00C2A120,
    WHO_RESULT_STRIDE = 0xA0,
    OFF_WHO_NAME = 0x00,   // char[] inline
    OFF_WHO_GUILD = 0x30,  // char[] inline
    OFF_WHO_LEVEL = 0x90,  // i32
    OFF_WHO_RACE = 0x94,   // i32 → ChrRaces.dbc
    OFF_WHO_CLASS = 0x98,  // i32 → ChrClasses.dbc
    OFF_WHO_ZONE = 0x9C,   // i32 → AreaTable.dbc

    // Per-faction current standing — `__fastcall(ecx = factionID) → int`.
    // Returns `base + delta` where the two values are stored at
    // `+0x08` / `+0x0C` of the player's rep slot (slot stride 0x10,
    // 64 slots at `0x00B73290`). Returns 0 for factionIDs not in the
    // player's reputation list (i.e. unencountered). Used by the
    // FACTION_STANDING_CHANGED event firing to push the live total
    // after the engine's setter has written the new delta.
    FUN_REPUTATION_GET_STANDING = 0x004D6370,

    // Reaction-band classifier — `__fastcall(ecx = factionID) → int`.
    // Internally calls `FUN_REPUTATION_GET_STANDING` and maps the total
    // through the 7-band threshold ladder (Hated..Exalted). Returns
    // 0..7 (0=Hated, 7=Exalted); `Script_GetWatchedFactionInfo` pushes
    // this `+ 1` to produce the 1..8 standingID Lua addons see.
    FUN_REPUTATION_GET_REACTION_BAND = 0x004D63A0,

    // Reputation-list slots — the player's 64-entry per-faction rep
    // store. Stride `0x10`; layout per entry:
    //   +0x00  i32  factionID (0 if slot unused)
    //   +0x04  u8   flags (bit 1 = atWar, bit 4 = canToggleAtWar)
    //   +0x08  i32  base standing
    //   +0x0C  i32  delta standing
    // Slot index = RepListID = Faction.dbc record `+0x04`. Indexed by
    // `FUN_004D5600(factionID) → repListID`. Reverse direction is
    // `FUN_004D5620(repListID) → factionID`.
    VAR_PLAYER_REP_SLOTS = 0x00B73290,
    REP_SLOT_STRIDE = 0x10,
    OFF_REP_SLOT_FACTION_ID = 0x00,
    OFF_REP_SLOT_FLAGS = 0x04,
    OFF_REP_SLOT_BASE_STANDING = 0x08,
    OFF_REP_SLOT_DELTA_STANDING = 0x0C,
    REP_SLOT_FLAG_AT_WAR = 0x02,
    // "Peace forced" — when SET, the player CAN'T toggle this faction
    // to/from at-war (e.g. your own home cities, certain quest
    // factions). Decoded from `FUN_FACTION_GET_CAN_TOGGLE_AT_WAR` at
    // `0x004D61E0`, which reads bit `0x10` of the flags byte and
    // `Script_GetFactionInfo` pushes `canToggleAtWar=false` when set.
    // Note: the bit's MEANING is "can't toggle" — `canToggleAtWar` is
    // its inverse, NOT its value.
    REP_SLOT_FLAG_PEACE_FORCED = 0x10,
    // INACTIVE flag — set by `Script_SetFactionInactive` (the "Inactive"
    // collapsible category in FactionFrame). Toggles via
    // `FUN_FACTION_SET_INACTIVE`; same byte as AT_WAR and PEACE_FORCED.
    REP_SLOT_FLAG_INACTIVE = 0x20,
    MAX_REP_SLOTS = 64,

    // Static reaction-band threshold tables, indexed by reaction band
    // 0..7 (returned by `FUN_REPUTATION_GET_REACTION_BAND`). The pair
    // gives the `[min, max)` standing range that defines each band —
    // currentReactionThreshold / nextReactionThreshold for modern
    // `C_Reputation.GetWatchedFactionData`-style table fields.
    VAR_REACTION_MIN_TABLE = 0x0080928C,
    VAR_REACTION_MAX_TABLE = 0x00809290,

    // `[player +0xE68] + 0x10C4` — the RepListID (rep-slot index) of
    // the faction currently being watched (the one shown above the XP
    // bar). Written by `FUN_PLAYER_SET_WATCHED_FACTION` after
    // translating the user-supplied factionID via
    // `FUN_004D5600 → RepListID`. Negative or out-of-range means "no
    // watched faction", which `Script_GetWatchedFactionInfo` checks
    // against `FUN_004D5620(slot)`'s return.
    OFF_CGPLAYER_INFO_WATCHED_REP_LIST_ID = 0x10C4,

    // `FactionSetAtWar(factionID, newState)` — `__fastcall(ecx = factionID,
    // edx = char newState)`. The sole worker behind
    // `Script_FactionToggleAtWar` (`0x004D6950`); the script wrapper just
    // reads the current at-war flag, negates it, and tail-calls here.
    //
    // Validates (only when turning OFF) that current standing ≥ -3000,
    // resolves factionID → repListID, bails if the player is in a loot
    // session (UNIT_FIELD_FLAGS bit `0x80000`, error code `0x1A1`),
    // mutates the rep-slot flag byte (`AT_WAR` gated by `PEACE_FORCED`),
    // and sends `CMSG_SET_FACTION_ATWAR` (opcode `0x125`).
    //
    // **Does NOT fire `UNIT_FACTION`** — a vanilla engine omission. 3.3.5
    // added a `FUN_0071F8F0(player, 0)` call here, whose inner
    // `FUN_0060BF10(playerGUID, UNIT_FACTION_id)` dispatches the event
    // for every unit token referencing the player. We polyfill by
    // hooking this function and firing `UNIT_FACTION("player")` post-
    // original (see `src/faction/UnitFactionPolyfill.cpp`).
    FUN_FACTION_SET_AT_WAR = 0x004D5FD0,

    // `FactionSetInactive(factionID, newState)` — `__fastcall(ecx = factionID,
    // edx = char newState)`. Inner setter behind `Script_SetFactionInactive`
    // (`0x004D69B0`, sets INACTIVE bit) and `Script_SetFactionActive`
    // (`0x004D6A00`, clears it) — both script wrappers tail-call here.
    //
    // Resolves factionID → repListID via `FUN_004D5600`, mutates the
    // rep-slot flag byte (sets/clears `INACTIVE` = `0x20`), writes via
    // `FUN_004D5FC0`, sends `CMSG_SET_FACTION_INACTIVE` (opcode `0x317`),
    // then calls `FUN_004D5C40` (recompute displayed-list, fire
    // `UPDATE_FACTION` event `0x174`).
    //
    // **Does NOT fire `UNIT_FACTION`** — same engine omission as the at-
    // war setter. Polyfilled in `src/faction/UnitFactionPolyfill.cpp`.
    FUN_FACTION_SET_INACTIVE = 0x004D60F0,

    // SMSG_SET_FACTION_ATWAR handler — `__stdcall(uint32_t opcode,
    // void *packet)`. Fires when the server force-changes the player's
    // at-war state on a faction (e.g., a flag the player can't toggle
    // changes server-side, or faction reset). Reads `(u32 repListID,
    // u8 flags)`, mutates AT_WAR on the slot accordingly, writes via
    // `FUN_004D5FC0`, then fires `UPDATE_FACTION` (event `0x174`).
    //
    // **Does NOT fire `UNIT_FACTION`** — 3.3.5 does (unconditionally
    // after the byte write). Polyfilled in
    // `src/faction/UnitFactionPolyfill.cpp`.
    FUN_SMSG_SET_FACTION_ATWAR = 0x004D56B0,

    // SMSG_INITIALIZE_FACTIONS handler — `__stdcall(uint32_t opcode,
    // void *packet)`. Fires once at login from the SMSG dispatcher. Reads
    // a count, then loops `(u8 flags, i32 standing)` pairs into the
    // 64-entry rep slot array: `FUN_004D5FC0(slot, flags)` +
    // `FUN_004D6330(slot, standing, 0 /*notify*/)`. Trailing
    // `FUN_004D5430` rebuilds the displayed-list snapshot, then fires
    // `UPDATE_FACTION` (event `0x174`).
    //
    // **Does NOT fire `UNIT_FACTION`** — modern WoW does. Addons that
    // observe UNIT_FACTION miss the initial sync entirely in vanilla.
    // Polyfilled in `src/faction/UnitFactionPolyfill.cpp`.
    FUN_SMSG_INITIALIZE_FACTIONS = 0x004D5640,

    // SMSG_SET_FACTION_STANDINGS handler — `__stdcall(uint32_t opcode,
    // void *packet)`. Fires once per server-pushed rep change (kills,
    // quest turn-ins, etc.). Reads count, loops `(flags, standing)`
    // pairs, calling `FUN_004D6330(slot, standing, 1 /*notify*/)` which
    // routes through `FUN_REPUTATION_FIRE_NOTIFY` for the
    // `CHAT_MSG_COMBAT_FACTION_CHANGE` dispatch.
    //
    // Also mutates AT_WAR / VISIBLE flag bits as standing crosses
    // thresholds (e.g., recovering to -2999 clears the force-set AT_WAR
    // from "below Hated"). **Vanilla doesn't fire `UNIT_FACTION` for
    // these side-effect flag flips**; modern 3.3.5 does. Polyfilled in
    // `src/faction/UnitFactionPolyfill.cpp` — snapshots the 64-entry
    // flag-byte array before/after and fires a single
    // `UNIT_FACTION("player")` if any bit changed.
    FUN_SMSG_SET_FACTION_STANDINGS = 0x004D5760,

    // `__fastcall(ecx = factionID, edx = signedDelta)`. This is the
    // engine's "reputation changed, fire the chat event" notify
    // helper — it formats the localized chat message and dispatches
    // `CHAT_MSG_COMBAT_FACTION_CHANGE`. Called from `FUN_004D6330`
    // (the per-slot setter) at `0x004D635C`, only when:
    //   1. The stored standing value actually changed for this slot.
    //   2. The setter's `notify` arg was non-zero (i.e. the call
    //      came from the per-update SMSG handler, not the bulk init
    //      handler that runs at login).
    // Hooking here matches modern WoW's `FACTION_STANDING_CHANGED`
    // semantics: fires once per real reputation change, skipping the
    // initial faction sync at login. ECX/EDX layout means the hook
    // can read factionID + delta in registers; for the modern
    // `(factionID, newStanding)` payload, call
    // `FUN_REPUTATION_GET_STANDING(factionID)` from inside the hook
    // (the setter has already written the new value by this point,
    // so it returns the updated total).
    FUN_REPUTATION_FIRE_NOTIFY = 0x0062C5F0,

    // Quest static-info cache (the client-side cache of QUEST_QUERY_RESPONSE
    // payloads — descriptions, objectives, reward text). Same five-arg
    // `_GetRecord` shape as the item cache, keyed by questID. Verified by
    // tracing `Script_GetQuestLogQuestText` (`0x004DFF20`):
    //   1. `[VAR_QUEST_LOG_SELECTED_QUEST_ID]` is the *questID* of the
    //      selected entry (not a pointer) — populated from
    //      `mov eax, [esi + 0x00BB71C0]` which reads field +0 of a
    //      `VAR_QUEST_LOG_ENTRIES` row, i.e. the questID itself.
    //   2. That value is passed as the `key` arg to `_GetRecord`, then
    //      hashed via `key & cache.mask` and matched with `cmp [bucket], key`.
    //
    // Cache-hit-already-loaded shortcut: if `[entry+0x18F8]` is set, the
    // function returns `entry+0x18` immediately and does NOT invoke the
    // queued callback. Modules that want a notification regardless of
    // cache state must fire it themselves (see `Item::Data` for the same
    // pattern).
    //
    // Callback shape verified at the dispatch sites near 0x00562EEB et al.:
    //   `mov ecx, [entry+0x18]; push 1; push ecx; call [entry+8]`
    //   → `__stdcall(void *userData, int success)` where `userData` is
    //   `arg4` we passed to `_GetRecord` (engine stores it at
    //   `entry+0x18` and replays it without dereferencing).
    VAR_QUEST_CACHE = 0x00C0E1B0,
    FUN_DBCACHE_QUEST_GET_RECORD = 0x00562A40,
    VAR_QUEST_LOG_SELECTED_QUEST_ID = 0x00BB7480,

    // Creature cache (creaturecache.wdb, SMSG_CREATURE_QUERY_RESPONSE).
    // One instance of the generic client query-cache class; constructed
    // by the cache factory (FUN_00554b40 -> FUN_00556060 with sig
    // 'BOMW', "creaturecache.wdb", recordSize 0x60).
    VAR_CREATURE_CACHE = 0x00C0E354,
    // Creature-cache `_GetRecord` — `__thiscall(cache, id, const u64 *guid,
    // void *callback, void *userData, char dedup)`. Returns the data
    // block (entry+0x18) when loaded; with `callback==0` it only peeks
    // (no query), with a non-null callback it fires the network query on
    // a miss. Class-specific (reads the loaded flag at entry+0x50);
    // gameobjects are a sibling class with a different layout and their
    // own `_GetRecord` (FUN_GAMEOBJECT_GET_RECORD). Verified via
    // FUN_00604600 (`FUN_00556aa0(creatureID, &guid, cb, 0, 0)`).
    FUN_CREATURE_GET_RECORD = 0x00556AA0,
    // Creature-cache response parser — `__thiscall(cache, packetReader,
    // flag)`. Fills the entry data block, sets the loaded flag (entry+0x50),
    // and walks the entry's pending-callback list. The creature SMSG
    // handler `FUN_005550E0` calls it with ecx=the creature cache.
    // `Cache::QueryLoad` hooks it to fire CREATURE_DATA_LOAD_RESULT. The
    // item/quest caches have their own parsers, so it's conflict-free.
    FUN_CREATURE_QUERY_RESPONSE = 0x00556E20,
    // Creature-query record field offsets, within the data block returned
    // by FUN_CREATURE_GET_RECORD. Anchored by rank@+0x20 (read by the
    // classification helper FUN_00605620 as `[block+0x20]`) and the
    // wire/WDB field order (Name[4], SubName, typeFlags, type, family,
    // rank, unk, petSpellDataId, displayId), cross-checked against real
    // creaturecache.wdb rows (Misthoof Stag 61332 -> type 1; Nordrassil
    // Nymph 61582 -> type 7, rank 1). Strings are char* (the fixed 0x60
    // block holds pointers, not the WDB's inline strings).
    OFF_CREATURE_NAME = 0x00,      // char *Name[4]; [0] is the primary name
    OFF_CREATURE_SUBNAME = 0x10,   // char *SubName (title); "" if none
    OFF_CREATURE_TYPE = 0x18,      // CreatureType (1=Beast, 7=Humanoid, ...)
    OFF_CREATURE_FAMILY = 0x1C,    // CreatureFamily
    OFF_CREATURE_RANK = 0x20,      // 0=normal,1=elite,2=rareelite,3=worldboss,4=rare
    OFF_CREATURE_DISPLAYID = 0x2C, // creature model display ID

    // GameObject cache (gameobjectcache.wdb, SMSG_GAMEOBJECT_QUERY_RESPONSE).
    // Sibling cache class to creature (constructed by FUN_00554B90 ->
    // FUN_00557bd0, sig 'BOGW', recordSize 0x5E) — same struct shape but a
    // different vtable (0x0080913C) and a LARGER entry (loaded flag at
    // entry+0x98, not +0x50), so it has its own `_GetRecord` and parser.
    VAR_GAMEOBJECT_CACHE = 0x00C0E318,
    // GO `_GetRecord`, same signature as the creature one. Verified via
    // FUN_0062dc10 (`FUN_00558560(goEntry, &guid, 0,0,0)` then reads the
    // GO name at `[block+0x08]`).
    FUN_GAMEOBJECT_GET_RECORD = 0x00558560,
    // GO response parser — `__thiscall(cache, packetReader, flag)`. The GO
    // SMSG handler `FUN_00555100` calls it with ecx=the GO cache. Distinct
    // from the creature parser (sets loaded flag at entry+0x98), so
    // `Cache::QueryLoad` hooks it separately to fire GAMEOBJECT_DATA_LOAD_RESULT.
    FUN_GAMEOBJECT_QUERY_RESPONSE = 0x005588E0,
    // GO-query record field offsets, within the data block returned by
    // FUN_GAMEOBJECT_GET_RECORD. Wire/WDB order: type, displayId, Name[4],
    // then a 24-int data[] array. name@+0x08 verified (FUN_0062dc10 reads
    // `[block+0x08]` as the name); type@+0x00 / displayId@+0x04 follow from
    // the field order, cross-checked against real gameobjectcache.wdb rows
    // (Windrunner type 15 displayId 7087; Mesa Elevator 47297 type 11
    // displayId 360).
    OFF_GAMEOBJECT_TYPE = 0x00,      // GameObjectType (0=door,3=chest,…)
    OFF_GAMEOBJECT_DISPLAYID = 0x04, // model display ID
    OFF_GAMEOBJECT_NAME = 0x08,      // char *Name[4]; [0] is the primary name

    // Action bar slot table — `uint[120]` (max slot index `< 0x78`).
    // Each entry is a packed action descriptor; top 4 bits are the type
    // tag, low 28 bits are the payload. Tags:
    //
    //   0x00000000  spell action; payload = spellID
    //   0x40000000  bag-item or macro action; payload `entry & 0xBFFFFFFF`
    //               is a 36-entry slot key. Disambiguate by checking the
    //               macro-slot map at `VAR_MACRO_SLOT_MAP` — if the
    //               payload matches any entry there, the slot is a macro
    //               (and the matching index is its 1-based macro slot);
    //               otherwise it's a bag-item reference. SuperWoWhook's
    //               modified `GetActionText` uses exactly this walk to
    //               surface macros to Lua.
    //   0x80000000  item-by-ID action; payload (entry & 0x7FFFFFFF) IS
    //               the itemID. Queries the global item cache directly.
    //
    // `entry == 0` means empty. Engine bounds-checks `slot < 0x78` in
    // every reader (verified at `Script_HasAction = 0x004E70D0`, helpers
    // at `0x004E5A50`, `0x004E6A50`).
    VAR_ACTION_TABLE = 0x00BC6980,
    ACTION_TABLE_MAX_SLOTS = 0x78,
    // Engine helper used by every action reader to extract the spellID
    // associated with a slot (spells return directly; bag-items return
    // their triggered spell; item-by-ID returns the first valid
    // m_spells[N].SpellID from the item cache). Pet/spell discriminator
    // returned via the second arg.
    //   __fastcall uint(uint slot0Based, uint *outPetFlag)
    FUN_ACTION_SLOT_TO_SPELL = 0x004E5A50,
    ACTION_TYPE_MASK = 0xF0000000,
    ACTION_TYPE_SPELL = 0x00000000,
    // 0x40000000 entries are ambiguous: they're either a bag item or a
    // macro. Discriminator: walk the 36-entry macro-slot map and check
    // whether `entry & 0xBFFFFFFF` matches one of those macro IDs. This
    // is exactly what SuperWoWhook's modified `GetActionText` does to
    // surface macros to Lua. Engine helper `FUN_004F0EC0` does the same
    // walk and returns slot-index or -1.
    ACTION_TYPE_BAG_OR_MACRO = 0x40000000,
    ACTION_TYPE_ITEM_BY_ID = 0x80000000,
    ACTION_PAYLOAD_MASK_BAG_OR_MACRO = 0xBFFFFFFF,
    ACTION_PAYLOAD_MASK_ITEM_BY_ID = 0x7FFFFFFF,

    // Per-character macro-slot map (uint[36]). Entry N holds the macroID
    // of the macro in slot N (0 = empty slot). The same memory is used
    // to look up macros by hash in `FUN_004F0E40` (Script_GetMacroInfo)
    // and by slot in `FUN_004F0EC0` (linear search for "is this entry
    // value a known macro?").
    VAR_MACRO_SLOT_MAP = 0x00BDCC60,
    MACRO_SLOT_MAP_COUNT = 0x24,

    // Macro slot → `MacroEntry *` resolver — `__fastcall(uint slot0Based)`.
    // Returns the MacroEntry struct pointer for the given 0-based slot,
    // or NULL when the slot is empty / out of range. Same accessor
    // `Script_GetMacroInfo` uses; we call it directly to read the
    // engine's cached primary-spell ID at `entry + OFF_MACRO_PRIMARY_SPELL`
    // without re-parsing the body. See `Macro::Spell::Script_GetMacroSpell`.
    FUN_MACRO_SLOT_TO_ENTRY = 0x004F0E40,

    // Macro create/edit workers — back `C_Macro.CreateMacro` /
    // `C_Macro.EditMacro` (see [[src/macro/Edit.cpp]]). Both store the
    // icon as a BARE basename at `entry + OFF_MACRO_ICON` (256 bytes,
    // no validation); `Script_GetMacroInfo` re-prefixes it as
    // `Interface\Icons\%s` on read, which is why any texture that lives
    // under `Interface\Icons\` — including index-less `INV_*` item icons
    // — round-trips even though vanilla's index-based UI can't pick it.
    //
    // Create — `__fastcall(char *name /*ECX*/, char *iconBasename /*EDX,
    // may be null*/, char *body /*may be null*/, uint local, int
    // perCharacter) -> uint macroID`. SStrCopys name→`+0x24` (0x40),
    // icon→`+0x64` (0x100) when non-null, body via `FUN_004f1550`
    // (`+0x164`), runs the primary-spell parser `FUN_004EFE00`, sets the
    // dirty flag `DAT_00bdcbf8=1`, and refreshes the UI. Returns 0 on
    // failure (empty name, or the 18-per-scope list is full). Pass
    // `local=0` to match the legacy `CreateMacro` default.
    FUN_MACRO_CREATE = 0x004F1010,

    // Edit — `__fastcall(uint macroID, char *name, char *iconBasename,
    // char *body, uint flag)`. Hash-finds the entry by macroID (no-op if
    // absent); each of name/icon/body is written only when its pointer is
    // non-null (so nil = leave that field unchanged), then reparse + dirty
    // flag + UI refresh, exactly like the create worker. It ALWAYS writes
    // `entry + OFF_MACRO_LOCAL_FLAG = flag`, so read the current flag and
    // pass it back to leave it unchanged. Chosen over replicating the
    // field writes because it also fires the action-button refresh
    // (`FUN_004e5cc0`/`FUN_004e5c00`) that direct writes would miss.
    FUN_MACRO_EDIT = 0x004F1280,

    // macroID → 0-based slot (linear scan of VAR_MACRO_SLOT_MAP);
    // `0xFFFFFFFF` on miss. `slot = FUN_MACRO_ID_TO_SLOT(macroID) + 1`
    // is how the legacy `CreateMacro` wrapper turns the worker's return
    // into the 1-based index it hands back to Lua.
    FUN_MACRO_ID_TO_SLOT = 0x004F0EC0,

    // Macro name → 0-based slot; `0xFFFFFFFF` on miss (the resolver
    // behind `GetMacroIndexByName`). Feeding its miss value straight to
    // `FUN_MACRO_SLOT_TO_ENTRY` yields NULL (bounds-rejected), so the
    // by-name and by-index EditMacro paths share one validation.
    FUN_MACRO_NAME_TO_SLOT = 0x004F0FA0,

    // MacroEntry field offsets. Name/body are handled by the workers; the
    // icon offset is documentation, and the local flag must be read to be
    // preserved across an EditMacro that doesn't mean to change it.
    OFF_MACRO_NAME = 0x24,        // char[0x40] inline
    OFF_MACRO_ICON = 0x64,        // char[0x100] inline bare basename
    OFF_MACRO_LOCAL_FLAG = 0x20,  // uint32 `local` flag (echoed by GetMacroInfo)

    // Macro-icon database. Populated lazily by `FUN_LOAD_MACRO_ICONS`
    // on the first `GetNumMacroIcons` call — enumerates `Interface\Icons\`
    // for `*.blp` files plus a wildcard match, sorts, and de-dupes. Each
    // entry is the basename (e.g. `"Ability_Kick"`) without the
    // `Interface\Icons\` prefix; vanilla's `Script_GetMacroIconInfo`
    // joins the prefix via sprintf before pushing. Verified by reading
    // `Script_GetNumMacroIcons` (`0x004F19F0`) and
    // `Script_GetMacroIconInfo` (`0x004F1A30`).
    //
    // Vanilla's loader has 3 enumeration passes, each with a per-file
    // callback. The first two (`FUN_MACRO_ICON_CB_DISK`,
    // `FUN_MACRO_ICON_CB_USER_MPQ`) prefix-filter on `"Ability_"` and
    // `"Spell_"` — anything else (including `INV_*` item icons) is
    // rejected. The third (`FUN_MACRO_ICON_CB_INSTALL_MPQ`) reads as
    // extension-only filter in the disassembly (any `.blp`/`.tga` is
    // accepted), but the engine's main icon DB ends up with zero
    // `INV_*` entries regardless (`GetNumMacroIcons() == 746`, all
    // `Ability_*`/`Spell_*`). Best guess: a check inside the
    // `SStrDup`/array-append helpers downstream of all three callbacks
    // filters them out — but the per-file callbacks themselves DO see
    // `INV_*` filenames (verified by hook capture: 5,226 unique
    // `INV_*` basenames flow through the callbacks per session).
    //
    // For `C_Macro::GetMacroItemIcons` we hook each callback at its
    // entry, capture any `INV_*` filename into a DLL-owned side array,
    // then forward to the original — dodges whichever downstream
    // filter the engine applies and matches the parallel item-icon
    // array 4.3.4 exposes (via `Script_GetMacroItemIcons`).
    VAR_MACRO_ICON_COUNT = 0x00BDCC1C,          // uint32 count of loaded icons
    VAR_MACRO_ICON_ARRAY = 0x00BDCC20,          // char ** — pointer to flat array of icon-name C strings (4-byte stride)
    FUN_LOAD_MACRO_ICONS = 0x004F0090,          // `__cdecl()` lazy populate; no-op if already loaded
    FUN_MACRO_ICON_CB_DISK = 0x004F0220,        // disk enumerator callback — `__fastcall(const char *fullPath)` — prefix-filtered
    FUN_MACRO_ICON_CB_USER_MPQ = 0x004F0350,    // user-MPQ enumerator callback — `__fastcall(MpqRecord *r)` — prefix-filtered
    FUN_MACRO_ICON_CB_INSTALL_MPQ = 0x004F04F0, // install-MPQ enumerator callback — `__fastcall(MpqRecord *r)` — extension-only filter

    // Quest log: 16-byte-stride entry array and active count.
    // Field +0 of each entry is the questID for real quests (a category index
    // for headers); field +8 is the header indicator: non-NULL = header,
    // NULL = real quest. Verified by Script_GetQuestLogTitle's isHeader push
    // at 0x004DF9A9 and by the helper at 0x004DF150 used by IsUnitOnQuest.
    VAR_QUEST_LOG_ENTRIES = 0x00BB71C0,
    VAR_QUEST_LOG_ENTRY_COUNT = 0x00BB7478,
    OFF_QUEST_LOG_ENTRY_STRIDE = 0x10,
    OFF_QUEST_LOG_ENTRY_QUEST_ID = 0x0,
    OFF_QUEST_LOG_ENTRY_HEADER_PTR = 0x8,

    // The single chokepoint that rebuilds the quest log from the
    // player's authoritative quest-slot data at `[CGPlayer + 0xE68 +
    // 0x28]` (a 0xF0-byte block holding the engine's quest sync state).
    // Called by the engine after every quest state change that needs
    // the Lua-visible log refreshed — accept, abandon, completion,
    // objective updates, login bulk sync. Signature:
    //
    //   __fastcall void FUN_QUEST_LOG_REBUILD(int param_1)
    //
    // If `param_1 == 0`, the function just fires QUEST_LOG_UPDATE
    // (event ID 0x134) via `FUN_00703E50` and returns — a no-op
    // rebuild signal. If non-zero, it `memset`s the entry array,
    // walks the player's slot data, repopulates `VAR_QUEST_LOG_ENTRIES`
    // / `VAR_QUEST_LOG_ENTRY_COUNT`, then fires QUEST_LOG_UPDATE.
    //
    // Hooked by `Quest::LogEvents` to snapshot the log pre-/post-rebuild
    // and fire `QUEST_ACCEPTED(logIndex, questID)` / `QUEST_REMOVED(questID)`
    // for the diff. Single quiet target in the `0x004DExxx` quest region —
    // no known DLL collisions.
    FUN_QUEST_LOG_REBUILD = 0x004DE510,

    // Player and pet spellbooks — flat int32 arrays indexed by 0-based slot.
    // Each entry is a spellID (0 for unused slots). Engine bounds-checks
    // each slot against an absolute max of `SPELLBOOK_MAX_SLOTS = 0x400`
    // before indexing (see `Script_GetSpellName` at `0x004B3FE0` and the
    // `cmp eax, 0x400; jb` guard at `0x004B4018`); the actual populated
    // count is much smaller, but slots past the populated range simply
    // hold 0, which we surface to Lua as nil.
    //
    // Used by `Spell::Lookup::SpellbookSlotToID` to support the
    // `GetSpellInfo(slot, bookType)` overload.
    VAR_PLAYER_SPELLBOOK = 0x00B700F0,
    VAR_PET_SPELLBOOK = 0x00B6F098,
    SPELLBOOK_MAX_SLOTS = 0x400,

    // Player-spell-knowledge bitmap — `[VAR_PLAYER_SPELL_BITMAP]` is a
    // pointer to a dword bitmap covering all spellIDs the player has
    // learned, including talent passives, racials, profession recipes,
    // and anything else granted via SMSG_LEARNED_SPELL / set in
    // SMSG_INITIAL_SPELLS. One bit per spellID:
    //
    //   bit set ⟺ player knows this spellID
    //   bitmap[spellID >> 5] & (1 << (spellID & 31))
    //
    // Verified by decoding the engine helper at `0x0060C740` (called
    // from `Script_GetTalentInfo`'s currentRank-derivation loop). Matches
    // the same bitmap pattern 5.4.8 uses for its `IsPlayerSpell`
    // (instance moved to `[0x011C25D8]` in that build).
    //
    // Bitmap covers spellIDs 0..VAR_SPELL_RECORD_COUNT inclusive — the
    // size matches Spell.dbc's row count. Pre-login the slot is NULL.
    VAR_PLAYER_SPELL_BITMAP = 0x00B710FC,

    // Player spell-knowledge writers — the two functions that mutate the
    // bitmap above. Learn (`FUN_004b25b0`) sets the bit, rebuilds the
    // spellbook arrays, and fires SPELLS_CHANGED; unlearn (`FUN_004b2c50`)
    // clears the bit and removes the spell. Both `__fastcall`: learn is
    // (uint spellID, int notify, uint replacedSpellID), unlearn is
    // (uint spellID, int). Co-hooked (spell/Info.cpp) to bump
    // Player::StatSignal so a talent respec invalidates GetSpellBonusHealing's
    // talent-conversion cache. They fire at login (SMSG_INITIAL_SPELLS) and on
    // each learn/unlearn — never per-frame, so cool hook targets.
    FUN_LEARN_SPELL = 0x004B25B0,
    FUN_UNLEARN_SPELL = 0x004B2C50,

    // Spell-rank-chain knowledge check — `char __thiscall(player /*ecx*/,
    // spellID)`. Walks `spellID`'s forward rank chain (the class/race-
    // resolved chain via `0x0060C7C0`) and returns nonzero if the player
    // knows any *higher* rank than `spellID` (each node tested through the
    // bitmap helper `0x0060C740`). The item tooltip's "Requires <spell>"
    // gate is `bitmap(spellID) OR this(spellID)`; C_PlayerInfo.CanUseItem
    // mirrors that so a RequiredSpell satisfied by a higher known rank
    // still passes. Does NOT test `spellID` itself — that's the caller's
    // direct bitmap check.
    FUN_SPELL_RANK_CHAIN_KNOWN = 0x0060C8D0,

    // Per-spell cooldown query. `__fastcall(int spellID, int bookType,
    // int *outDuration, int *outStart, int *outEnable)`. Same path
    // `Script_GetSpellCooldown` (`0x004B40A0`) uses internally after
    // resolving the (slot, bookType) Lua args to a spellID — we
    // bypass the slot resolution and pass spellID directly with
    // `bookType=0` (player) since that's the standard
    // `IsUsableSpell(spellID)` use case. `outDuration > 0` means the
    // spell is currently on cooldown.
    //
    // **Argument order**: verified via `Script_C_Spell_GetSpellCooldown`
    // round-trip test (Blink, spellID 1953) — the third arg receives
    // the cooldown duration (e.g. 15000 ms for Blink), the fourth
    // receives the engine tick count at start (`FUN_OS_TICKCOUNT_MS`).
    // Both are 0 when no cooldown is active. The CMaNGOS-leaning
    // documentation in other emulator projects has these reversed —
    // don't trust it for this entry.
    FUN_SPELL_QUERY_COOLDOWN = 0x006E2EA0,

    // Per-item cooldown query — `bool __fastcall(uint itemID,
    // int *outDuration, int *outStart, uint *outEnable)`. Looks up
    // the item's `ItemStats_C` record via the cache, finds the slot
    // with trigger = ON_USE (`OFF_ITEMSTATS_SPELL_TRIGGER[i] == 0`),
    // and queries that spell's cooldown via the shared
    // `FUN_006E13E0` against the player-spell manager
    // (`DAT_00CECAEC`). Returns `true` when a cooldown is active.
    //
    // Same `(*outDuration, *outStart, *outEnable)` order as
    // `FUN_SPELL_QUERY_COOLDOWN`, so the unit conversion is
    // identical: multiply both by 0.001 to convert engine ms →
    // `GetTime()`-compatible seconds.
    //
    // **Calling convention gotcha**: the Ghidra decomp labels the
    // first arg `uint *` because the ItemStats cache lookup uses
    // it both as a hash key and as the cached-entry field-0 match
    // target (a `uint *` typed slot). The actual fastcall value is
    // the itemID integer itself, NOT a pointer to it — passing
    // `&itemID` makes ItemStats interpret the stack address as the
    // itemID and the lookup silently fails.
    FUN_ITEM_QUERY_COOLDOWN = 0x006E2ED0,

    // Spell.dbc reagent fields. Reagent[8] (itemIDs) at +0xA8;
    // ReagentCount[8] (counts) at +0xC8. Verified empirically by
    // decompiling `BuildSpellTooltip` at 0x0052E610 — its reagent-
    // loop iterates `localSpellRec + 0x2A` (dword arithmetic, byte
    // +0xA8) as itemIDs and indexes `[+8]` (dword 0x32, byte +0xC8)
    // for counts. Walked by both the tradeskill / craft scraper's
    // `Spell::Lookup::NthRecipeReagentItemID` and `IsUsableSpell`'s
    // reagent gate.
    //
    // **Don't trust CMaNGOS for these offsets.** A prior version of
    // this entry placed reagents at +0x110 / +0x130 per CMaNGOS
    // vanilla `SpellEntry`, but 1.12.1's actual record layout
    // diverges from CMaNGOS for reagent fields — those offsets read
    // unrelated data that's always 0 for normal spells, so
    // IsUsableSpell's reagent check silently passed every spell.
    // The "verified previously" PowerType=+0x7C / ManaCost=+0x80
    // CMaNGOS alignment held for earlier fields but breaks before
    // the reagent block.
    OFF_SPELL_REAGENT_ID = 0xA8,
    OFF_SPELL_REAGENT_COUNT = 0xC8,
    SPELL_MAX_REAGENTS = 8,

    // Per-spell *runtime* state cache, indexed by spellID via a hash
    // table (mask at `[VAR_SPELL_STATE_HASH_MASK]`, base at
    // `[VAR_SPELL_STATE_HASH_BASE]`). The engine maintains the cache as
    // player state changes — cooldown, silence, GCD, mana balance, etc.
    // each update flips the relevant byte. The action-bar usability
    // path at `0x004E5BA0` reads `+0x564` (usable) and `+0x568`
    // (noMana) directly off this cache; we do the same to back
    // `IsUsableSpell` / `C_Spell.IsSpellUsable`.
    //
    // `FUN_SPELL_LOOKUP_STATE` is `__fastcall(int spellID) → void *`
    // — the hash-walking helper. Returns null for spells the player
    // doesn't know (cache only holds known spellIDs) or pre-login
    // (when `[VAR_SPELL_STATE_HASH_MASK]` is `-1`).
    FUN_SPELL_LOOKUP_STATE = 0x004F0F40,
    OFF_SPELL_STATE_USABLE = 0x564,
    OFF_SPELL_STATE_NO_MANA = 0x568,

    // Spell description format helper. Reads the locale-resolved description
    // string from `record[+0x228 + locale*4]` and walks it character-by-
    // character substituting `$s1`/`$d`/`$o`/etc. placeholders with values
    // computed from the spell record. Engine has 8 callers (the spell
    // tooltip builder at 0x52F717, the talent UI, the trainer, ...). The
    // global cursor at `0x00BE0B80` is the helper's parser state; it sets
    // and walks it internally, so callers don't manage it.
    //
    // Calling convention (verified at 0x52F717 et al.):
    //   void __fastcall(
    //     void  *spellRecord,      // ecx — Spell.dbc record ptr
    //     char  *outputBuffer,     // edx — caller-provided, null-terminated on return
    //     int   bufLen,            // [ebp+0x08]
    //     int   contextFlag,       // [ebp+0x0C] — small int the talent/trainer paths
    //                              //   compute from a UI-state global; 0 is the safe
    //                              //   "no scaling context" default
    //     int   reserved3,         // [ebp+0x10] — always 0 across the 8 callers
    //     int   useToolTipText,    // [ebp+0x14] — 0=description (+0x228),
    //                              //   non-zero=ToolTip (+0x24C with fallback to +0x228)
    //     int   reserved5,         // [ebp+0x18] — always 1 across the 8 callers
    //     int   reserved6          // [ebp+0x1C] — always 0 across the 8 callers
    //   );
    FUN_FORMAT_SPELL_DESCRIPTION = 0x005075F0,

    // The currently-auto-repeating spellID (0 when nothing is
    // auto-repeating). Same dword backs Shoot/Wand/Auto-Shot — no
    // per-class branching. Written by the cast-start path at
    // `FUN_006E54F0` (line `DAT_00ceac30 = *piVar5;` where `piVar5` is
    // the Spell.dbc record) and cleared on logout/world unload. Read by
    // `Script_IsAutoRepeatAction`'s inner helper at `0x004E55B0` and
    // exposed as a 1-line getter at `0x006E9FD0`.
    VAR_ACTIVE_AUTO_REPEAT_SPELL = 0x00CEAC30,

    // Currently-casting spellID. Written by the cast-start helper at
    // `FUN_006E4AD0` (`__fastcall(spellID, targetState)`) — when a
    // new cast begins, the function pushes the previous active
    // spellID into `VAR_QUEUED_CAST_SPELL` and writes the new one
    // here. Read by `Script_SpellStopCasting` via its
    // `is-currently-casting` predicate at `FUN_006E3D30` (returns
    // `DAT_00CECA88 != 0`) and the spellID-returning getter at
    // `FUN_006E3D10`. Non-zero means "the cast bar is showing this
    // spell"; cleared to 0 when the cast finishes or is cancelled.
    VAR_CURRENT_CAST_SPELL = 0x00CECA88,

    // Ground/reticle-targeting flags — non-zero while a spell is awaiting a
    // target click (the AoE reticle / target cursor). `SpellIsTargeting()`
    // (`0x006E6CD0`) returns true iff this != 0 (via its predicate
    // `FUN_006E48A0`, `return DAT_00CECAC0 != 0`); `Spell_C_TargetSpell`
    // (`0x006E5250`) sets the flag bits from the spell's target type, and the
    // clear helper `FUN_006E4900` (from `SpellStopTargeting`) / a ground
    // placement zeroes it. Polled by `Spell::CastEvents::PollReticle` to fire
    // UNIT_SPELLCAST_RETICLE_TARGET / _CLEAR.
    VAR_SPELL_TARGETING_FLAGS = 0x00CECAC0,
    // The pending cast spellID — written by `Spell_C_CastSpell` (`0x006E4B60`)
    // to the spell being cast, and used elsewhere as a `Spell.dbc` index (e.g.
    // `CancelSpell`'s `records[this]`). While `VAR_SPELL_TARGETING_FLAGS` is
    // set it's the spell whose reticle is up, so the RETICLE events read the
    // spellID from here.
    VAR_PENDING_CAST_SPELL = 0x00CEAC58,

    // The cast-start state writer — `__fastcall(int spellID, int
    // targetState)`. Sets `VAR_CURRENT_CAST_SPELL` (pushing any current
    // spell into `VAR_QUEUED_CAST_SPELL`; `spellID == 0` restores the queued
    // one). `Spell::Cast` co-hooks it for lag-free, client-side detection of
    // NORMAL player casts (stamps from the spellID arg before the original).
    // It does NOT fire for a chained same-spell recast: the caller
    // `Spell_C_CastSpell` (`0x006E4B60`) short-circuits at `spellID ==
    // VAR_CURRENT_CAST_SPELL` *before* reaching here, so those never run the
    // client cast path — they're caught by the `SMSG_SPELL_START` backstop
    // instead. nampower hooks the higher Spell_C_CastSpell, not this inner
    // writer, so contention is low.
    FUN_CAST_START_SET = 0x006E4AD0,
    // Previous / queued cast — holds the spellID that was active
    // before the latest one superseded it (mid-GCD queueing in
    // vanilla). When the current cast ends, `FUN_006E4AD0(0, ...)`
    // restores this into `VAR_CURRENT_CAST_SPELL`. Modern
    // `C_Spell.IsCurrentSpell` covers both "casting now" and "queued
    // to cast next", so we check this slot too.
    VAR_QUEUED_CAST_SPELL = 0x00CECAA8,

    // Spell-placement state bitmask. Non-zero when the engine is
    // waiting on user input for an in-progress cast — the AoE reticle,
    // unit-target cursor, trade-target cursor, etc. Bits encode what
    // kind of input is needed; the engine's click handlers read this
    // to decide whether a click was relevant. Read by
    // `Script_SpellIsTargeting` via `FUN_006E48A0` (returns
    // `state != 0`). Bits the engine OR-sets via `FUN_006E5250` when
    // entering placement (subset relevant to us):
    //   - `0x20` source world location needed
    //   - `0x40` dest world location needed
    // The `0x60` mask therefore identifies "ground-target placement"
    // (vs. unit-target, trade-target, etc.). See `Spell::AtCursor`.
    VAR_SPELL_PLACEMENT_STATE = 0x00CECAC0,
    SPELL_PLACEMENT_GROUND_MASK = 0x60,

    // `FUN_006E60F0(float *xyz)` — commits the in-flight ground-target
    // placement at the given world position. Reads three contiguous
    // floats from `xyz`; checks the placement-state mask
    // (`VAR_SPELL_PLACEMENT_STATE`) to decide whether this is a
    // source-coord (`0x20`) or dest-coord (`0x40`) commit; clears the
    // matching bit and stores the coords into engine globals which the
    // cast-finalize path then reads. Self-gates when neither bit is
    // set — safe no-op for unit-target placement. Called by the engine
    // itself when a player left-clicks ground while the reticle is up.
    FUN_COMMIT_PLACEMENT_COORDS = 0x006E60F0,

    // `FUN_006E4900()` — cancels in-flight placement mode. Internally
    // checks `VAR_SPELL_PLACEMENT_STATE != 0` and, if so, fires the
    // cast-cancel path via `FUN_006E4940(0x1C)`. Safe to call when no
    // placement is active (no-ops). Wraps the action behind
    // `Script_SpellStopTargeting`.
    FUN_STOP_PLACEMENT = 0x006E4900,

    // `*VAR_WORLDFRAME_CLICK_INFO` is a pointer to the engine's
    // CWorldFrame "click info" struct — the data block where the input
    // dispatcher stashes "what is the cursor pointed at right now"
    // after every mouse motion. Allocated at world enter, populated
    // continuously by the rendering pipeline (viewport bounds and
    // camera position at fixed offsets) and the OS message pump
    // (cursor screen pos). NULL when not in world.
    //   - `+0x350` (uint32) click type after last raycast:
    //       `0` = no hit
    //       `1` = world / terrain hit
    //       `2` = unit / object hit
    //   - `+0x360..+0x368` (3 floats) hit world position (when type == 1)
    VAR_WORLDFRAME_CLICK_INFO = 0x00B4B2BC,
    OFF_CLICK_INFO_TYPE = 0x350,
    OFF_CLICK_INFO_COORDS = 0x360,

    // `FUN_00481F00(void *clickInfo)` — refreshes the cursor's "what
    // am I pointed at?" state on demand. Internally reads cursor
    // screen pos from input state at `[clickInfo+0xA0]+0x1118/+0x111C`,
    // calls the screen→world ray builder at `FUN_004813B0` (uses
    // viewport bounds at `[clickInfo+0x390..+0x39C]` plus camera
    // position at `[clickInfo+0x65B8]+8/+0xC/+0x10`), then raycasts
    // via `FUN_0069BFF0` (terrain + WMO intersection). Writes click
    // type to `[clickInfo+0x350]` and hit coords to
    // `[clickInfo+0x360..+0x368]`. Safe to call from arbitrary code
    // as long as the player is in-world.
    FUN_REFRESH_CURSOR_RAYCAST = 0x00481F00,

    // `Script_CastSpellByName` — engine's Lua wrapper for the
    // `CastSpellByName(name [, onSelf])` global. `int __fastcall(void *L)`
    // — standard Lua C function ABI. Reads `name` from stack[1] (string)
    // and `onSelf` from stack[2] (boolean), resolves the name (with
    // optional `(Rank N)` suffix) through `FUN_004B3950`, then dispatches
    // to the engine's cast path at `FUN_004B3300`. Returns 0 (no Lua
    // results). Callable directly from C++ — push args on stack, call.
    FUN_SCRIPT_CAST_SPELL_BY_NAME = 0x004B4AB0,

    // Inner spell-cast dispatcher — `__fastcall(slot, bookType, targetGuidLo,
    // targetGuidHi)`. `Script_CastSpellByName` calls this after resolving
    // the spell name to a spellbook slot. For ground-target spells, the
    // engine enters placement mode (sets `VAR_SPELL_PLACEMENT_STATE`)
    // and returns; placement is resolved later by mouse-click or
    // programmatically via `FUN_COMMIT_PLACEMENT_COORDS`. Passing (0, 0)
    // for the GUID is the "no implicit target" form — engine falls back
    // to the player's current selected target.
    FUN_SPELL_CAST_DISPATCH = 0x004B3300,

    // Macro "primary spell" parser — `__fastcall(MacroEntry *entry)`.
    // Walks the macro body (stored at `entry + OFF_MACRO_BODY`) line
    // by line, looking for `/cast <name>`, `/castsequence <name>`,
    // etc. (via the engine's `SLASH_CAST%d` registry) and the
    // `CastSpellByName("<name>")` Lua pattern. On first hit, resolves
    // the name to a spellID via `FUN_004B3BC0` and writes it to
    // `entry + OFF_MACRO_PRIMARY_SPELL`. Writes `0xFFFFFFFF` when a
    // pattern matched but the name didn't resolve (unknown spell);
    // writes `0` when no line matched any pattern. Called from macro
    // create/edit/refresh paths.
    //
    // Hooked by `Spell::CastNoToggle` to teach the engine to recognize
    // `CastSpellNoToggle("<name>")` as a primary-spell pattern, so
    // macros using it get tagged in the spell-state cache the same
    // way `CastSpellByName` macros do. Without this, `IsAutoRepeatAction`
    // returns false for macro slots whose only cast is via our function
    // (the engine's parser doesn't know to look for our identifier),
    // breaking action-bar highlighting in pfUI etc.
    FUN_MACRO_PARSE_PRIMARY_SPELL = 0x004EFE00,

    // Engine's "spell name → spellbook-resolved spellID" helper.
    // `__fastcall(const char *name, int *outIsPet)`. Internally calls
    // `FUN_004B3950` (the rank-stripping name-with-`(Rank N)` parser)
    // to get a spellbook slot, then returns
    // `[VAR_PLAYER_SPELLBOOK][slot]` (or `[VAR_PET_SPELLBOOK][slot]`
    // when `*outIsPet` is set on entry). Returns 0 if the name doesn't
    // resolve to a known spellbook entry. This is the resolver the
    // engine's macro parser uses — name → spellID lookup that respects
    // the player's known spell list.
    FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID = 0x004B3BC0,

    // The single name → spellbook-slot resolver underneath everything
    // spell-cast related. `__fastcall(const char *name, void *out) ->
    // int slot` (or `-1`). Strips a trailing `(Rank N)` suffix, then
    // calls `FUN_004B3A10` for the actual lookup. Called by
    // `Script_CastSpellByName` (every Lua cast) AND by
    // `FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID` (which the macro parser
    // uses). Hooking here makes one change to both the runtime cast
    // path AND the macro-tagging path — useful for accepting numeric
    // spellID input as if it were a name (the `Spell::CastByID`
    // module does this to enable `/cast 5019`-style macros).
    FUN_RESOLVE_SPELL_NAME_TO_SLOT = 0x004B3950,

    // `__fastcall(uint slot, int bookType) -> int isActive`. Returns 1
    // if the spell at the given (slot, bookType) is currently producing
    // an active aura on the player (bookType=0) or pet (bookType=1).
    // Walks the unit descriptor's aura-spellID array at `+0xA4` (48
    // slots) and checks the active-bit at `+0x164`. The engine's
    // toggle-aware cast path (`FUN_004B3300`) consults this to decide
    // whether `/cast Shoot` should send CMSG_CAST_SPELL or
    // CMSG_CANCEL_AURA. Used by `Spell::CastNoToggle` to suppress the
    // toggle-off side of `CastSpellNoToggle` on shapeshift / aspect /
    // stance forms — anything that creates a self-aura.
    FUN_SPELL_IS_TOGGLE_AURA_ACTIVE = 0x004B36F0,

    // MacroEntry struct offsets (verified by tracing FUN_004EFE00).
    // The macro body is an inline null-terminated string at `+0x164`;
    // line breaks are `\n`. The primary-spell cache (what
    // `IsAutoRepeatAction` ultimately reads via the spell-state hash
    // lookup at `[0x00C0E2A0]`) is at `+0x564`.
    OFF_MACRO_BODY = 0x164,
    OFF_MACRO_PRIMARY_SPELL = 0x564,

    // Spell.dbc and friends. Pointer-to-records-array + record-count pairs.
    // Used by Script_GetSpellName/Texture and BuildSpellTooltip.
    VAR_SPELL_RECORDS = 0x00C0D788,            // SpellRecord *records[spellID]
    VAR_SPELL_RECORD_COUNT = 0x00C0D78C,       // max spellID

    // SpellMechanic.dbc — maps a SpellMechanic ID to its localized
    // name. Standard 5-DWORD DBC instance at 0x00C0D7BC; records ptr at
    // +0x08, count at +0x0C (see docs/DBCs.md). Records are 1-based
    // (records[mechanicID]); the engine only ever uses the table for
    // existence/bounds checks (FUN_006e9ca0, FUN_00612df0, FUN_006e8eb0
    // case 0x8d all do `records[id] != 0`), never reading the name — so
    // the name offset below follows the canonical 2-column
    // (ID + localized Name) DBC schema rather than an engine-reader anchor.
    VAR_SPELLMECHANIC_RECORDS = 0x00C0D7C4,    // SpellMechanicRecord *records[mechanicID]
    VAR_SPELLMECHANIC_COUNT = 0x00C0D7C8,      // max mechanic ID
    OFF_SPELLMECHANIC_NAME = 0x04,             // char *name[9], locale-indexed

    // SpellItemEnchantment.dbc — the table a weapon/item enchant ID
    // (e.g. the enchantID from `C_Item.GetWeaponEnchantInfo`) indexes.
    // Records-ptr at instance +0x08, count at +0x0C (instance 0x00C0D7D0).
    // 24-DWORD records (recordSize 0x60). Layout fully verified by
    // parsing the on-disk SpellItemEnchantment.dbc against known records
    // (Crusader 1900: Type[0]=1, Arg[0]=20007 "Holy Strength";
    // Sharpened+3 id 13: Type[0]=2, Amount[0]=3; Reinforced Armor+8 id 15:
    // Type[0]=4, Amount[0]=8). OFF_..._NAME (+0x34) is cross-confirmed by
    // two engine readers (FUN_00496170 / FUN_00495d60) that push
    // `[rec + 0x34 + VAR_LOCALE_INDEX*4]` as the display name.
    //   Type[i]   : ITEM_ENCHANTMENT_TYPE — 1=combat-proc spell,
    //               2=damage, 3=equip spell, 4=resistance, 5=stat,
    //               6=totem, 7=use spell. 0 = empty slot.
    //   Amount[i] : magnitude for damage/resist/stat types (min; vanilla
    //               min==max). 0 for spell types (value lives in the spell).
    //   Arg[i]    : spellID for spell types (1/3/7); stat index for STAT.
    VAR_SPELLITEMENCHANT_RECORDS = 0x00C0D7D8, // SpellItemEnchantmentRecord *records[enchantID]
    VAR_SPELLITEMENCHANT_COUNT = 0x00C0D7DC,   // max enchant ID
    OFF_SPELLITEMENCHANT_TYPE = 0x04,          // int Type[3]
    OFF_SPELLITEMENCHANT_AMOUNT = 0x10,        // int Amount[3] (EffectPointsMin)
    OFF_SPELLITEMENCHANT_ARG = 0x28,           // int EffectArg[3]
    OFF_SPELLITEMENCHANT_NAME = 0x34,          // char *name[8], locale-indexed

    // ItemRandomProperties.dbc — the table a random-suffix ID (the third
    // field of an `item:id:enchant:suffix:unique` link) indexes to get
    // the SpellItemEnchantment IDs that apply the "of the Bear"-style
    // bonus. Standard 5-DWORD DBC instance at 0x00C0DBCC; records-ptr at
    // +0x08, count at +0x0C (see docs/DBCs.md). Verified by parsing the
    // on-disk ItemRandomProperties.dbc: 16-field / 0x40 records, field 0
    // = ID, field 1 = internal name string, and the enchant IDs occupy
    // fields 2..6 (offset +0x08, five contiguous u32 slots — only the
    // first few are used; the rest are 0). Field 7 duplicates the name
    // string offset, so it is NOT an enchant slot — do not read past
    // slot 5.
    VAR_ITEMRANDOMPROP_RECORDS = 0x00C0DBD4,   // ItemRandomPropertiesRecord *records[suffixID]
    VAR_ITEMRANDOMPROP_COUNT = 0x00C0DBD8,     // max suffix ID
    OFF_ITEMRANDOMPROP_ENCHANT = 0x08,         // u32 Enchantment[5]
    ITEMRANDOMPROP_ENCHANT_SLOT_COUNT = 5,

    // Spell.dbc School field — 0-based integer at record +0x04.
    // Verified empirically against Fireball (133) → School=2 (Fire)
    // and Frostbolt (116) → School=4 (Frost) on Octo 1.12.1. Vanilla
    // doesn't use the multi-bit SchoolMask form (TBC+); a spell
    // belongs to exactly one school. Mapping:
    //   0 = Physical  1 = Holy  2 = Fire    3 = Nature
    //   4 = Frost     5 = Shadow 6 = Arcane
    OFF_SPELL_SCHOOL = 0x04,
    SPELL_SCHOOL_COUNT = 7,

    // Spell.dbc DispelType field — u32 at record +0x10, indexes into
    // `SpellDispelType.dbc` (`VAR_SPELLDISPEL_RECORDS` below). 0 =
    // not dispellable. Verified by decompiling `Script_UnitDebuff`,
    // which reads `*(int*)(spellRecord + 0x10)` and feeds it into
    // the DispelType DBC to produce the third return value (the
    // dispel-type name string).
    OFF_SPELL_DISPEL_TYPE = 0x10,

    VAR_SPELL_RANGE_RECORDS = 0x00C0D79C,      // SpellRange.dbc
    VAR_SPELL_RANGE_COUNT = 0x00C0D7A0,
    VAR_SPELL_ICON_RECORDS = 0x00C0D7EC,       // SpellIcon.dbc
    VAR_SPELL_ICON_COUNT = 0x00C0D7F0,
    VAR_SPELL_CAST_TIMES_RECORDS = 0x00C0D878, // SpellCastTimes.dbc
    VAR_SPELL_CAST_TIMES_COUNT = 0x00C0D87C,

    // SpellRadius.dbc — AOE radius records, indexed by a spell's
    // EffectRadiusIndex (see OFF_SPELL_RECORD_EFFECT_RADIUS_INDEX). Record
    // shape: { id@+0, radius@+4 (f32), radiusPerLevel@+8 (f32), ... }.
    // Verified by decompiling FUN_006e6350 (the engine's effective-radius
    // helper), which reads `radiusRec[+4]` as the base radius and
    // `radiusRec[+8] * casterLevel` as the per-level term. Read by
    // GetSpellRadius (base radius only — no level scaling).
    VAR_SPELL_RADIUS_RECORDS = 0x00C0D7B0,
    VAR_SPELL_RADIUS_COUNT = 0x00C0D7B4,
    OFF_SPELL_RADIUS_VALUE = 0x04,             // base radius, f32

    // SpellMod (talent / item) accumulators for the LOCAL PLAYER. Two
    // parallel tables of int32, indexed [familyFlagBit][op]: flat adds at
    // VAR_SPELLMOD_FLAT_TABLE, percent adds at VAR_SPELLMOD_PCT_TABLE.
    // Per-bit stride is 0x74 (29 ops); the op is the dword index (op*4
    // within a slot). A spell's per-effect value is modified by summing,
    // over the bits set in its SpellFamilyFlags, flat[bit][op] and
    // pct[bit][op], then `value = (value + flat) * (100 + pct) * 0.01`.
    // Gated on the spell's SpellFamilyName matching the player's class
    // family. Decompiled from FUN_006e6b30 / FUN_006e6bf0 (the engine's
    // ApplySpellMod), which the radius helper FUN_006e6350 invokes with
    // op 6. These are single global instances = the local player's mods
    // (vanilla only tracks spell mods for the local player).
    VAR_SPELLMOD_FLAT_TABLE = 0x00CEAD60,
    VAR_SPELLMOD_PCT_TABLE = 0x00CECB30,
    SPELLMOD_SLOT_STRIDE = 0x74,               // bytes per familyFlagBit row
    SPELLMOD_SLOT_COUNT = 64,                  // SpellFamilyFlags is 64-bit
    SPELLMOD_OP_RADIUS = 6,                    // op index for spell radius
    SPELLMOD_OP_DURATION = 1,                  // op index for aura duration

    // SpellDispelType.dbc — maps dispel-type IDs (1..N) to localized
    // names like "Magic", "Curse", "Disease", "Poison". Used by
    // `Script_UnitDebuff` to fill its third return value. Standard
    // 5-DWORD class instance shape; records array at +0x08, count at
    // +0x0C of the instance. Records themselves have a "has-name"
    // gate at +0x28 (non-zero means the name slot is populated) and
    // the locale-applied string pointer at +0x2C. The engine's
    // accessor does the locale lookup before exposing the pointer
    // here, so we get a ready-to-use string with no further indexing.
    VAR_SPELLDISPEL_RECORDS = 0x00C0D83C,
    VAR_SPELLDISPEL_COUNT = 0x00C0D840,
    OFF_SPELLDISPEL_HAS_NAME = 0x28,
    OFF_SPELLDISPEL_NAME = 0x2C,

    // Map.dbc — instance/zone metadata. Standard 5-DWORD class instance
    // shape; records and count are catalogued in `docs/DBCs.md`. Record
    // schema (vanilla, derived from `FUN_00495c90` IsInInstance helper
    // and `FUN_0049e1c0` raid-instance warning handler):
    //   +0x08  uint32 instance type (0=world, 1=dungeon, 2=raid, 3=BG)
    //   +0x10..+0x30  9 locale-string ptrs (localized display name)
    // No max-players column in vanilla — that data was added in TBC via
    // MapDifficulty.dbc.
    VAR_MAP_RECORDS = 0x00C0DAA8,
    VAR_MAP_COUNT = 0x00C0DAAC,
    OFF_MAP_INSTANCE_TYPE = 0x08,
    OFF_MAP_NAME = 0x10,

    // Currently-loaded map ID (the Map.dbc ID for the zone/instance the
    // player is in). Written by the map-transition handler
    // `FUN_00495d10`; read by `FUN_00495c90` (the helper backing
    // `Script_IsInInstance`).
    VAR_CURRENT_MAP_ID = 0x00B4E378,

    // "World is live" flag — 1 while the player is fully in the world, 0 during
    // any loading screen (initial login, zone/instance transition, logout to
    // glue). Set to 1 by the enter-world init `FUN_004908C0` (guarded
    // `if (==0){=1;...}`) and back to 0 by the world-teardown `FUN_00490A80`
    // (`if(!=0){=0;...}`) that runs at the start of every transition. Byte flag.
    // The clean pollable gate for "can this happen in the world right now" —
    // e.g. suppressing server resync packets that arrive behind a loading
    // screen. Verified via the enter/leave pair in Ghidra.
    //
    // (NOTE: 0x00B4E37C is NOT this flag — an earlier note wrongly claimed it
    // was. That address is instance grace-period state, read only by the
    // "recently left an instance" 90-second check in `FUN_00495c90`.)
    VAR_IN_WORLD = 0x00B4B424,

    // Local player's current AreaTable.dbc area ID (u32 storage; only
    // the low 16 bits are used — the value is broadcast over the wire
    // as a u16 in `SMSG_PARTY_MEMBER_STATS`). Written by the zone-change
    // handler `FUN_00494780(areaID, parentAreaID, zoneName, subzone,
    // realZone, eventID)`; read by `Script_GetRealZoneText` and the
    // outbound party-stats builder `FUN_005f0880`.
    VAR_PLAYER_AREA_ID = 0x00B4E314,

    // Unified party/raid member stats lookup —
    // `__fastcall(GUID *guidPair) -> uint8_t *`. Returns a pointer to
    // the member's stats data on hit, NULL on miss. Tail-jumps from
    // party storage (`FUN_004e8820`, 4 slots at `0x00BC70B0`) to raid
    // storage (`FUN_004bb0f0`, 40-slot pointer array at `0x00B712A8`).
    // Both sub-calls return pointers where the field at `+0x14` is the
    // u16 areaID (AreaTable.dbc ID) — for party that's at struct +0x14
    // directly, for raid it's at `*raidArr[i] + 0x10 + 0x14`. Caller
    // doesn't need to distinguish the two cases.
    //
    // Within the returned stats block: `+0x08` status flags, `+0x09`
    // powerType, `+0x0A..+0x10` HP/maxHP/power/maxPower, `+0x12` level,
    // `+0x14` u16 areaID, `+0x16/+0x18` position. Verified from the
    // outbound stats builder `FUN_005f0880` (mirrors inbound packet
    // shape from SMSG_PARTY_MEMBER_STATS_FULL).
    FUN_GROUP_MEMBER_STATS_LOOKUP = 0x00496400,
    OFF_GROUP_MEMBER_AREA_ID = 0x14,
    // `+0x08` status-flags byte of the stats block (see field map above);
    // bit 0 is the online flag `Script_UnitIsConnected` tests. Sibling
    // `FUN_GROUP_MEMBER_SLOT_LOOKUP` (`0x00496420`, `__fastcall(u64 *guid)
    // → member slot*`) is the presence fallback UnitIsConnected consults
    // when no stats block exists — a non-null slot means the member is
    // present (and thus connected). Both take a pointer to the 64-bit GUID.
    OFF_GROUP_MEMBER_STATUS_FLAGS = 0x08,
    GROUP_MEMBER_STATUS_ONLINE = 0x1,
    FUN_GROUP_MEMBER_SLOT_LOOKUP = 0x00496420,

    // Health/power fields within the two group-member blocks — the
    // out-of-range fallback the engine's UnitHealth/UnitMana (and their Max
    // variants) use when a groupmate has no live CGUnit (different map / out
    // of range), sourced from SMSG_PARTY_MEMBER_STATS. The two blocks have
    // DIFFERENT layouts (all u16):
    //
    //   Stats block (FUN_GROUP_MEMBER_STATS_LOOKUP): statusFlags@+8,
    //     powerType@+9, health@+0xa, maxHealth@+0xc, power@+0xe, maxPower@+0x10,
    //     level@+0x12, areaId@+0x14. The full record.
    //   Raid slot (FUN_GROUP_MEMBER_SLOT_LOOKUP): powerType@+0x58,
    //     health@+0x5c, maxHealth@+0x5e, power@+0x60, maxPower@+0x62. Leaner —
    //     no level/area/flags.
    //
    // Power values are raw (divide by the power divisor for display). Verified
    // from Script_UnitHealth (0x005174d0) / Script_UnitHealthMax (0x005175b0)
    // / Script_UnitMana (0x00517670) / Script_UnitManaMax (0x005177e0).
    OFF_GROUP_MEMBER_STATS_HEALTH = 0xa,
    OFF_GROUP_MEMBER_STATS_MAX_HEALTH = 0xc,
    OFF_GROUP_MEMBER_STATS_POWER_TYPE = 0x9,
    OFF_GROUP_MEMBER_STATS_POWER = 0xe,
    OFF_GROUP_MEMBER_STATS_MAX_POWER = 0x10,
    OFF_GROUP_MEMBER_STATS_LEVEL = 0x12,
    OFF_RAID_SLOT_POWER_TYPE = 0x58,
    OFF_RAID_SLOT_HEALTH = 0x5c,
    OFF_RAID_SLOT_MAX_HEALTH = 0x5e,
    OFF_RAID_SLOT_POWER = 0x60,
    OFF_RAID_SLOT_MAX_POWER = 0x62,
    // Remaining raid-member-struct fields, mapped from Script_GetRaidRosterInfo
    // (0x004bb560). Same struct FUN_GROUP_MEMBER_SLOT_LOOKUP returns; all
    // object-independent (readable for out-of-range raiders). Name + class are
    // NOT here — GetRaidRosterInfo reads those from the NameCache by GUID.
    OFF_RAID_SLOT_SUBGROUP = 0x08,   // u32, 0-based (roster UI shows +1)
    OFF_RAID_SLOT_RANK = 0x0c,       // u32
    OFF_RAID_SLOT_FLAGS = 0x18,      // u32
    OFF_RAID_SLOT_LEVEL = 0x22,      // u16
    OFF_RAID_SLOT_AREA_ID = 0x24,    // u16 -> AreaTable.dbc
    RAID_SLOT_FLAG_ONLINE = 0x1,
    RAID_SLOT_FLAG_DEAD = 0x4,

    // Group-member aura spell-ID arrays — the out-of-range fallback the
    // engine's Script_UnitBuff (0x00519500) / Script_UnitDebuff (0x005198f0)
    // read when a groupmate has no live CGUnit. The server transmits a
    // member's current auras (spell IDs only — no caster/duration/stacks) in
    // SMSG_PARTY_MEMBER_STATS via GROUP_UPDATE_FLAG_AURAS (buffs, slots 0..31)
    // + GROUP_UPDATE_FLAG_AURAS_NEGATIVE (debuffs, slots 32..47); the client
    // keeps them in the two group-member blocks. Both arrays are 48 u16
    // entries (UNIT_AURA_TOTAL) indexed by the same absolute slot the
    // descriptor uses. Spell IDs are truncated to u16 on the wire, so custom
    // IDs > 65535 come through wrong (same limitation as the built-in
    // UnitBuff/UnitDebuff). Read the party array only when the member's online
    // flag (OFF_GROUP_MEMBER_STATUS_FLAGS bit 0) is set — the engine does.
    OFF_GROUP_MEMBER_STATS_AURAS = 0x1a,  // party stats block (FUN_00496400)
    OFF_RAID_SLOT_AURAS = 0x64,           // raid slot (FUN_00496420)

    // Per-slot GUID tables — used to map party/raid token strings
    // ("party1", "raid17") to GUIDs without going through
    // `FUN_RESOLVE_UNIT_TOKEN`, which only resolves to a local CGUnit
    // and returns NULL for group members in other zones.
    //
    // Party: 4 entries × 8 bytes (lo + hi). `guids[i]` corresponds to
    // "partyN" where N = i+1.
    //
    // Raid: 40 entries × 4 bytes — each is a `RaidMember *`. GUID is
    // at `*member + 0` (lo) and `*member + 4` (hi). Verified from
    // `FUN_004bb0f0`. Slot may be NULL (empty / offline raid member).
    VAR_PARTY_MEMBER_GUIDS = 0x00BC6F48,
    VAR_RAID_MEMBER_PTRS = 0x00B712A8,

    // Raid-target-marker GUID table — 8 contiguous `uint64` slots, one
    // per raid icon (slot 0 = mark1/star … slot 7 = mark8/skull), `0`
    // when that marker is unassigned. Written by the
    // `SMSG_RAID_TARGET_UPDATE` handler cluster (`FUN_004ba0b0` /
    // `FUN_004ba220` / `FUN_004ba550`); the reverse lookup
    // `FUN_004bb190(guidLo, guidHi)` walks it as `(&table)[i*2]` for
    // `i` in 0..7 and returns the mark index or 8. `Unit::RaidTarget`
    // reads it to resolve the `markN` token and to drive the per-mark
    // unit-event observers. Matches nampower's `CGRaidInfo_m_raidtargets`.
    VAR_RAID_TARGET_GUIDS = 0x00B71368,

    VAR_LOCALE_INDEX = 0x00C0E080,             // 0..8, picks one of the 9 localized strings

    // Locale-name string table — a parallel `const char *[8]` indexed by
    // VAR_LOCALE_INDEX, holding the exact codes `GetLocale()` returns:
    // enUS / koKR / frFR / deDE / zhCN / zhTW / esES / xxYY. This is the
    // source `Script_GetLocale` (FUN_0048d8b0) reads:
    //   lua_pushstring(L, (&PTR_DAT_008558a4)[DAT_00c0e080]);
    // Verified by resolving all 8 entries from the binary. Index range is
    // 0..7 (VAR_LOCALE_INDEX is documented 0..8, but slot 8 overreads into
    // the string pool — clamp to 0..7 before reading). Used by the TOC
    // `[TextLocale]` / `[AllowLoadTextLocale]` directives (AddOns::TocRewrite).
    VAR_LOCALE_NAME_TABLE = 0x008558A4,

    LUA_IS_NUMBER = 0x6F34D0,
    LUA_IS_STRING = 0x6F3510,
    LUA_TO_NUMBER = 0x6F3620,
    LUA_TO_BOOLEAN = 0x6F3660,
    LUA_TO_STRING = 0x6F3690,
    LUA_PUSH_NUMBER = 0x6F3810,
    LUA_PUSH_NIL = 0x6F37F0,
    LUA_PUSH_BOOLEAN = 0x6F39F0,
    LUA_PUSH_STRING = 0x6F3890,
    // `lua_pushlstring(L, ptr, len)` — binary-safe push. Same TValue
    // tag as `pushstring` but takes an explicit length so the string
    // can contain NULs.
    LUA_PUSH_LSTRING = 0x6F3840,
    // `lua_strlen(L, idx)` — returns the byte length of the string
    // value at `idx`. Pairs with `lua_tostring` for binary-safe
    // string reads.
    LUA_STR_LEN = 0x6F36E0,
    // Verified empirically via `_classicapi_PushValueProbe` —
    // pushes a number then dups via PushValue, checks GetTop tracks.
    // **Doc note:** `docs/LuaCAPI.md` reports `0x6F30D0` as lua_pushvalue
    // and `0x6F32B0` as lua_remove; both are wrong. `0x6F30D0` is
    // actually `lua_remove` (has the shift-down loop and `decr_top`),
    // and `0x6F3350` is `lua_pushvalue` (index-resolve → TValue copy
    // to L->top → `add [ecx+8], 0x10` incr_top). `0x6F32B0` is
    // `lua_replace` (single TValue copy + decr_top).
    LUA_PUSH_VALUE = 0x6F3350,
    // `luaL_ref(L, t)` — pops the top, stores it in the table at `t`
    // at a freshly-allocated integer key, returns the key. Use with
    // `LUA_REGISTRY_INDEX` to stash Lua values across C-side scopes;
    // pair with `LUA_REF_UNREF` to release. Verified by decompiling
    // `FUN_006f5310`: classic luaL_ref shape — nil-top early-out
    // returning `LUA_REFNIL = -1`, FREELIST_REF chain pop (pushvalue +
    // tonumber on table[0]), else objlen-based new slot, finally
    // table[ref] = popped value.
    LUA_REF_REF = 0x6F5310,
    // `luaL_unref(L, t, ref)` — releases a ref previously returned
    // by `luaL_ref`, freeing the slot for future allocations. Verified
    // by decompiling `FUN_006f5400`: `ref >= 0` guard (skips
    // `LUA_NOREF = -2` / `LUA_REFNIL = -1`), reads current FREELIST_REF
    // head, writes it to `table[ref]`, then updates FREELIST_REF = ref
    // — the canonical freelist-link operation.
    LUA_REF_UNREF = 0x6F5400,
    LUA_PUSH_CCLOSURE = 0x6F3920,
    LUA_NEW_TABLE = 0x6F3C90,
    LUA_GET_TABLE = 0x6F3A40,     // (was 0x6F3EA0, which is lua_rawset)
    LUA_RAW_GET = 0x6F3B00,
    // `lua_rawgeti(L, idx, n)` — pushes `table_at_idx[n]` without invoking
    // metamethods. __fastcall(L /*ecx*/, idx /*edx*/, n /*stack*/). The
    // frame-script runner uses it with `idx = REGISTRY` to push a value
    // stored under an integer ref (handler, frame object, message handler,
    // and luaL_ref'd saved globals). Note it also carries the WoW
    // frame-script exec-context stamp (the `DAT_00ceeac0` dance) for pushed
    // CObjects — which is exactly why the engine pushes handler/frame
    // through it rather than a plain rawget.
    LUA_RAWGETI = 0x6F3BC0,
    // `lua_getmetatable(L, objindex)` — pushes the metatable of the value at
    // objindex and returns 1, or pushes nothing and returns 0 when it has none.
    // Verified in docs/LuaCAPI.md and by the getfenv/getmetatable base-lib
    // functions that call it. Used by the `__environment` env-protection hook.
    LUA_GET_METATABLE = 0x6F3CF0,
    // `lua_getfenv(L, idx)` — pushes the environment table of the function
    // (or userdata/thread) at `idx`. The getfenv/setfenv base-lib code calls
    // it to read a function's environment before the protection check.
    LUA_GET_FENV = 0x6F3D50,
    LUA_SET_TABLE = 0x6F3E20,
    LUA_RAW_SET = 0x6F3EA0,
    LUA_INSERT = 0x6F31A0,
    LUA_REMOVE = 0x6F30D0,        // (was 0x6F32B0, which is lua_replace)
    LUA_REPLACE = 0x6F32B0,
    LUA_TYPE = 0x6F3400,
    LUA_GET_TOP = 0x6F3070,
    LUA_SET_TOP = 0x6F3080,
    LUA_CALL = 0x6F4180,
    LUA_PCALL = 0x6F41A0,
    // `lua_next(L, idx)` — pops a key, pushes (newKey, value) for the
    // next pair in the table at `idx`. Returns 0 when iteration is
    // done. Documented in `docs/LuaCAPI.md`; calls `luaH_next` at
    // `0x006FA2A0` internally.
    LUA_NEXT = 0x6F4450,
    LUA_ERROR = 0x6F4940,

    // `lua_xmove(from, to, n)` — moves the top `n` TValues from one
    // thread to another. Decrements `from->top` by `n*16`, copies each
    // 16-byte TValue, increments `to->top`. Needed by the `coroutine.*`
    // trampolines to shuttle args/returns between the main state and a
    // suspended coroutine.
    LUA_X_MOVE = 0x6F2F80,
    // `lua_newthread(L)` — does GC check, calls inner `luaE_newthread`
    // at `0x006F6B10` to allocate a new state, pushes it onto L's stack
    // with type tag 8 (LUA_TTHREAD). Ghidra reports the return as
    // `void` (decompiler limitation — the inner call's eax flows
    // through), so callers should retrieve the new state via
    // `ToPointer(L, -1)` rather than relying on the return value.
    LUA_NEW_THREAD = 0x6F3030,
    // `lua_resume(L_co, nargs)` — runs a coroutine. Zero internal
    // callers in the engine (coroutine library was stripped from the
    // standard-library registration tables at build time); linker
    // kept the symbol so it's callable from C. Goes through
    // `luaD_rawrunprotected` at `0x006F5DAC` for setjmp/longjmp
    // safety; inner static `resume` helper lives at `0x006F67B0`.
    LUA_RESUME = 0x6F6620,
    // `lua_yield(L, nresults)` — flags the current CallInfo to yield
    // and returns -1. Sets a `0x10` bit on the CI->state field rather
    // than writing `L->status` directly in this build. Zero internal
    // callers (same reason as `lua_resume`). Errors with
    // "attempt to yield across metamethod/C-call boundary" or
    // "cannot yield a C function" when called from the wrong context.
    LUA_YIELD = 0x6F6860,
    // `lua_topointer(L, idx)` — returns the pointer value of a
    // pointer-valued TValue: type 5 (table), 6 (function), 8 (thread)
    // return `tvalue[2]` directly; type 2 (lightuserdata) returns its
    // value; type 7 (userdata) returns the userdata payload (record +
    // 0x10). Returns 0 for anything else. We use it as `lua_tothread`
    // — for a known-thread TValue, returns the embedded `lua_State *`.
    LUA_TO_POINTER = 0x6F3790,
    // `luaL_argerror(L, narg, msg)` — formats "bad argument #%d to
    // '%s' (%s)" (or "calling '%s' on bad self (%s)" for method-style
    // calls) and calls `lua_error`. Doesn't return.
    LUAL_ARG_ERROR = 0x6F4810,
    // `luaL_setn(L, t, n)` — Lua 5.0 auxlib table-length setter
    // (verified: `FUN_006f4ea0`, __fastcall L/ecx, t/edx, n/stack,
    // RET 4). Matches 5.0.2 `luaL_setn`: sets `t.n = n` if a numeric
    // `n` field exists, else `sizes[t] = n` in the registry's weak
    // LUA_SIZES table. Found via `table.insert` (`FUN_007fb6b0`), which
    // calls it to record the new length; `luaL_getn` is `FUN_006f5050`.
    // Needed by `table.wipe` to reset the length after clearing keys —
    // the length is out-of-band from the keys, so a `lua_next` +
    // `rawset(nil)` clear leaves it stale and subsequent `table.insert`
    // appends past the wiped slots.
    LUAL_SETN = 0x6F4EA0,
    // `luaL_getn(L, tableIndex)` — Lua 5.0 auxlib table-length getter,
    // `int __fastcall(L /*ecx*/, idx /*edx*/)`, plain RET (no stack
    // args), length in eax. Verified by disassembly: a negative idx is
    // normalized via lua_gettop; tries the `t.n` field (rawget "n"),
    // then the registry LUA_SIZES path, then falls back to a linear
    // rawgeti scan for the first nil. Counterpart of LUAL_SETN above
    // (named there since the table.insert trace). Used by the 5.1
    // `unpack(list, i, j)` upgrade for the default range end.
    LUAL_GETN = 0x6F5050,
    // `luaV_gettable(L, t, key, loop)` — the VM's generic index resolver.
    // __fastcall(L /*ecx*/, t /*edx*/, key /*stack*/, loop /*stack*/),
    // RET 8. Returns the result TValue* in eax (Ghidra types it void; the
    // table-hit path demonstrably reaches the epilogue with eax =
    // luaH_get's return — `CALL 0x006fa7a0; CMP [EAX],0; JNZ epilogue` at
    // 0x006F7D1D..25). Table case: luaH_get (0x006FA7A0); nil result →
    // the metatable continuation FUN_006F7D60, which reads the TABLE'S
    // OWN metatable (hvalue+0x08, fast-TM flag byte hvalue+0x06 — 5.0
    // fasttm) and returns &nilobject (0x00811BC0) when there is none
    // (miss = nil, never an error). Non-table case → the continuation
    // below, loop+1. Depth-guarded at 100 ("loop in gettable").
    FUN_LUA_V_GETTABLE = 0x006F7CF0,
    // The non-table branch of luaV_gettable — stock Lua 5.0's "index a
    // non-table value" continuation. Same convention/return as
    // FUN_LUA_V_GETTABLE (result TValue* in eax, RET 8). Body: __index
    // tag method via luaT_gettmbyobj (0x006F7BD0 — which HARDCODES
    // &nilobject for anything but table/userdata; stock 5.0, nothing
    // stripped); nil → luaG_typeerror "attempt to index" (0x006FC620);
    // function TM → call it; table TM → recurse FUN_LUA_V_GETTABLE with
    // `loop` passed through UNCHANGED (0x006F7F23). Exactly 3 call
    // sites: luaV_gettable's else-branch + luaV_execute's OP_GETTABLE /
    // OP_SELF non-table fallbacks (0x006F8A14 / 0x006F8C17 in
    // FUN_006F8720) — so it fires ONLY when code indexes a non-table,
    // which in stock 5.0 is always about to raise "attempt to index"
    // (table `__index` dispatch goes through FUN_006F7D60's fasttm and
    // never lands here). A cold, uncontested hook site. Prologue is
    // plain PUSH/MOV — MinHook-relocatable. Co-hooked by
    // LuaSyntax::StringMethods to backport Lua 5.1 string methods
    // (`("x"):upper()`): strings get the `string` library table as their
    // __index, exactly 5.1's shared-string-metatable mechanism.
    FUN_LUA_INDEX_NONTABLE = 0x006F7EE0,
    // `luaL_loadbuffer(L, buff, size, chunkname)` — compiles a source
    // buffer into a Lua function on the stack (does NOT run it). Returns
    // an int status (0 = ok) in eax; Ghidra types it void because the
    // status flows through `lua_load`. __fastcall(L /*ecx*/, buff /*edx*/,
    // size /*stack*/, chunkname /*stack*/); strips a leading UTF-8 BOM.
    // This is the UNIVERSAL Lua compile chokepoint — every path that turns
    // source text into bytecode funnels through it: `luaL_loadstring`
    // (0x006F57C0, backs the `loadstring` global) and the three FrameScript
    // execute/load funnels (0x00704AE0 / 0x00704C70 / 0x00703280, backing
    // `RunScript`, XML `<OnLoad>`/`<OnClick>` handlers, and file-loaded
    // addon chunks). Verified via xrefs to `lua_load` (0x006F4320) and its
    // two callers (this = loadbuffer, 0x006F5490 = loadfile). Hooking here
    // lets a source-level transform see every chunk before the 5.0 parser.
    FUN_LUAL_LOADBUFFER = 0x006F5690,
    // Return address of the `call luaL_loadbuffer` inside the compile-AND-run
    // funnel FUN_00704AE0 (the CALL is at 0x00704B0C; next instruction, and so
    // the pushed return address, is 0x00704B11). FUN_00704AE0 compiles a buffer
    // then IMMEDIATELY `lua_pcall`s it (0x00704B42) — it is the funnel every
    // addon/FrameXML FILE load reaches (FUN_006ede10 -> FUN_00704bc0 -> here)
    // AND what `RunScript` runs through. LuaSyntax's `luaL_loadbuffer` hook
    // reads `_ReturnAddress()` and arms the addon-args grant ONLY when the
    // caller is THIS site — i.e. a chunk that will be run before anything else,
    // so its preamble consumes the grant immediately. `loadstring` (0x00703280,
    // returns to 0x007032B5) compiles WITHOUT running and takes a
    // caller-forgeable chunkname, so it must NOT arm; the call-site check
    // excludes it even when nested inside a RunScript body.
    RET_LUA_FILE_COMPILE = 0x00704B11,
    // Shared environment-protection predicate for `getfenv`/`setfenv`
    // (`bool __fastcall(L /*ecx*/)`). Pushes the function-at-top's environment
    // via `lua_getfenv(L, -1)`, then pushes the protection marker
    // `env["__fenv"]` (a RAW field on the env table) and returns whether it is
    // non-nil — net stack effect +2 (env, marker). `getfenv` (`0x00702AC0`)
    // returns the marker in place of the real env when set; `setfenv`
    // (`0x00702BF8`) raises "cannot change a protected environment" when set.
    // We co-hook this ONE predicate to read `getmetatable(env).__environment`
    // (raw) instead — the Lua 5.1 form (verified against 3.3.5's
    // `FUN_0084F2F0`) — with the raw `__fenv` field kept as a fallback, so both
    // consumers gain the 5.1 semantics with the 1.12 behavior preserved.
    FUN_LUA_ENV_PROTECT_PREDICATE = 0x00702BA0,
    // `lua_checkstack(L, n)` — `int __fastcall(L /*ecx*/, n /*edx*/)`.
    // Ensures room for `n` more stack values, growing if needed; returns 0
    // (without growing) when `(top-base)/16 + n` would exceed LUA_MAXCSTACK
    // (0x800 = 2048), 1 otherwise. Verified by the `cmp reg, 0x800` overflow
    // test at 0x006F2F43. Needed by any C function that pushes an unbounded
    // number of results (e.g. `strsplit`) — pushing past the ~20-slot
    // C-call guarantee without growing corrupts the stack.
    LUA_CHECK_STACK = 0x006F2F30,
    // `str_find` — the Lua 5.0 string-library `string.find` C function
    // (`int __fastcall(void *L)`), entry in the strlib luaL_reg table at
    // `0x00822dd0`. Returns `1` (nil pushed, no match), `2` (start, end),
    // or `2 + ncaptures`. `string.match` (a Lua 5.1 addition, missing here)
    // shares the exact same pattern engine — it's `find` that yields the
    // captures / whole match instead of the indices — so `BaseLib::StringLib`
    // calls this and transforms the result rather than reimplementing the
    // matcher. Verified by decoding the function at this address.
    FUN_LUA_STR_FIND = 0x007FC3B0,
    // `str_gfind` — the Lua 5.0 `string.gfind` C function (strlib entry at
    // `0x00822dd8`). 5.1 renamed it to `string.gmatch` with identical
    // behaviour (returns a stateful iterator closure over the matches), so
    // `BaseLib::StringLib` registers `string.gmatch` as a direct alias of
    // this function pointer — no wrapper.
    FUN_LUA_STR_GFIND = 0x007FCFA0,
    // `str_gsub` — the Lua 5.0 `string.gsub` C function (strlib entry
    // at `0x00822DE0`, name string "gsub" at `0x00882230` — the last
    // entry before the registration table's NULL terminator, matching
    // stock 5.0 order). Verified by decode: args (s, pat, repl, maxN),
    // `^` anchor pre-strip, and the luaL_argerror(3) "string or
    // function expected" gate. Its add_value helper (`FUN_007fd2a0`)
    // matches stock 5.0: a FUNCTION replacement's non-string result is
    // popped and NOTHING is appended (match → empty) — 5.1 instead
    // keeps the original match for nil/false. That difference is why
    // `BaseLib::StringLib`'s 5.1 table-replacement shim wraps the
    // pattern in an outer capture (arg 1 = whole match) so its adapter
    // closure can hand the match back for missing keys.
    FUN_LUA_STR_GSUB = 0x007FD0E0,
    // `str_format` — the Lua 5.0 `string.format` C function (`int
    // __fastcall(void *L)`), strlib entry at `0x00822dc0`. Verified by decode:
    // reads the format at arg 1, handles `%%`, `%d/i/c`, `%e/E/f/g/G`,
    // `%o/u/x/X`, `%s`, `%q`, with the "invalid option to 'format'" / "invalid
    // format (width or precision too long)" errors. FontString:SetFormattedText
    // pushes THIS directly and pcalls it, rather than resolving `_G.string.format`
    // — so an addon replacing `string.format` cannot hijack or break it (matches
    // retail, whose SetFormattedText formats in C).
    FUN_LUA_STR_FORMAT = 0x007FD3D0,
    // `math_fmod` — the Lua 5.0 `math.mod` C function (mathlib entry at
    // `0x00822cb0`): `luaL_checknumber(1/2)` → `fmod(x, y)` → `lua_pushnumber`.
    // 5.1 renamed the Lua-facing name to `math.fmod` with the identical C
    // implementation, so `BaseLib::MathLib` registers `math.fmod` as a direct
    // alias of this function pointer — no wrapper. Verified by decode.
    FUN_LUA_MATH_FMOD = 0x007FAFD0,
    // `lua_iscfunction(L, idx)` — returns 1 if value is a function
    // closure with the `isC` byte set (`Closure.c.isC == 1`), 0
    // otherwise. Used by `coroutine.create` to reject C functions
    // (Lua 5.0 only allows Lua-defined functions as coroutine bodies).
    LUA_IS_C_FUNCTION = 0x6F34A0,
    // `lua_tothread(L, idx)` — returns the embedded `lua_State *` for
    // type-8 (LUA_TTHREAD) TValues, NULL for anything else. Used by
    // the `coroutine` library to extract the coroutine pointer from a
    // stack slot.
    LUA_TO_THREAD = 0x6F3770,

    // Global `lua_State *`. The engine keeps one main thread state here; we
    // read it on demand in helpers that run outside a Lua callback (e.g.
    // RegisterTableFunction during LoadScriptFunctions).
    VAR_LUA_STATE = 0x00CEEF74,

    // zlib 1.2.2, statically linked into WoW.exe (version string at
    // `0x008745F0`, copyright banners at `0x00815528`/`0x00815608`).
    //
    // One-shot helpers (always Zlib-format with adler32 trailer):
    //   int compress2(dest, *destLen, source, sourceLen, level)
    //   int uncompress(dest, *destLen, source, sourceLen)
    //
    // Lower-level streaming API — needed for Gzip and raw Deflate
    // formats since the one-shots only do Zlib format. The `Init2_`
    // variants take an explicit `windowBits` which selects format:
    //   +15        = Zlib (default)
    //   +15 + 16   = Gzip
    //   -15        = raw Deflate (no header, no checksum)
    //   +15 + 32   = auto-detect (decode only; Zlib or Gzip)
    //
    // Call signatures (all `__fastcall`, callee cleans the stack):
    //   int deflateInit2_(strm[ECX], level[EDX],
    //                     stack: method, windowBits, memLevel,
    //                            strategy, version, stream_size)
    //   int deflate(strm[ECX], flush[EDX])
    //   int deflateEnd(strm[ECX])
    //   int inflateInit2_(strm[ECX], windowBits[EDX],
    //                     stack: version, stream_size)
    //   int inflate(strm[ECX], flush[EDX])
    //   int inflateEnd(strm[ECX])
    //
    // Return codes match zlib's: 0 = Z_OK, 1 = Z_STREAM_END,
    // -3 = Z_DATA_ERROR, -4 = Z_MEM_ERROR, -5 = Z_BUF_ERROR.
    FUN_ZLIB_COMPRESS2 = 0x00730BC0,
    FUN_ZLIB_UNCOMPRESS = 0x00734810,
    FUN_ZLIB_DEFLATE_INIT2 = 0x00732A70,
    FUN_ZLIB_DEFLATE = 0x00733040,
    FUN_ZLIB_DEFLATE_END = 0x00733650,
    FUN_ZLIB_INFLATE_INIT2 = 0x00730D60,
    FUN_ZLIB_INFLATE = 0x00730EA0,
    FUN_ZLIB_INFLATE_END = 0x00732320,
    // The version string deflateInit2_/inflateInit2_ checks against
    // (compile-time mismatch returns Z_VERSION_ERROR). Same string the
    // one-shot helpers pass through.
    VAR_ZLIB_VERSION_STRING = 0x008745F0,
    ZLIB_STREAM_SIZE = 0x38,  // sizeof(z_stream) in 1.2.2 build

    // Static event-name "table" at runtime VA `0x00BE11D8` — this is what
    // `C_EventUtils.IsEventValid` walks. It is NOT the structure that
    // `frame:RegisterEvent` and the dispatcher actually use; that lives at
    // `[VAR_EVENT_TABLE_BASE_PTR]` (see below) with 16-byte entries. The
    // table at 0x00BE11D8 is sufficient for an existence check, but firing
    // or registering custom events requires the real structure.
    VAR_EVENT_NAME_TABLE = 0x00BE11D8,
    EVENT_NAME_TABLE_MAX_SLOTS = 1024,

    // The REAL event registration data structure used by the engine. An
    // `EventEntry` array, 16 bytes per entry:
    //   +0x00 char *name             event name string ptr
    //   +0x04 ?                      used by the chain allocator at 0x7052D0
    //   +0x08 ?
    //   +0x0C SubscriberNode *head   head of the subscribed-frames chain
    // Allocated/populated at engine boot. `[VAR_EVENT_TABLE_BASE_PTR]`
    // dereferences to the array base; `[VAR_EVENT_TABLE_COUNT]` is the
    // entry count. `Frame::RegisterEvent` at 0x00702140 case-insensitively
    // strcmps the input event name against each entry's `+0x00` and inserts
    // the frame into the matching entry's chain.
    VAR_EVENT_TABLE_BASE_PTR = 0x00CEEF68,
    VAR_EVENT_TABLE_COUNT = 0x00CEEF64,
    // The FrameScript_EventObject is a `{cap@+0, count@+4, data@+8}` at
    // 0x00CEEF60, so COUNT/BASE above are its +4/+8. `Event::Custom::Grow`
    // writes all three (base, cap, count) when it enlarges the table.
    VAR_EVENT_TABLE_CAP = 0x00CEEF60,
    EVENT_ENTRY_STRIDE = 0x10,
    OFF_EVENT_ENTRY_NAME = 0x00,
    OFF_EVENT_ENTRY_HEAD = 0x0C,

    // Subscriber-chain node layout — the linked list hanging off each
    // entry's +0x0C head, derived from Frame::RegisterEvent
    // (FUN_00702140): each node is `{ ?, next@+0x04, frame@+0x08 }`, where
    // `frame` is the CFrameScriptObject* subscriber. A node value with its
    // low bit set (or null) is the end/empty sentinel. Walked by
    // Frame:IsEventRegistered to test membership.
    OFF_EVENT_NODE_NEXT = 0x04,
    OFF_EVENT_NODE_FRAME = 0x08,

    // `Frame::RegisterEvent` — the C++ helper called by the Lua
    // `frame:RegisterEvent(eventName)` method (`Script_RegisterEvent` at
    // `0x00774A40`). `__thiscall` with `(this=frame, eventName)`. Walks
    // the entry array at `[VAR_EVENT_TABLE_BASE_PTR]`, case-insensitively
    // strcmps against each entry's name, and appends `frame` to the
    // matching entry's chain.
    FUN_FRAME_REGISTER_EVENT = 0x00702140,

    // `RebuildEventTable` — the engine's bulk event-table population
    // routine. `__fastcall(const char **names, int count) -> void`
    // (ECX = names array, EDX = count). Tears down any existing table
    // (calling the per-entry teardown helper at 0x00701A40 + freeing
    // the base pointer via SMemFree), allocates a fresh table of size
    // `count * 16`, then loops `names[0..count-1]` and for each
    // non-empty entry calls `SStrDup(name, file, line)` to allocate
    // an engine-owned copy at `entry+0x00`.
    //
    // Called twice during boot (with two different name arrays — 26
    // entries at 0x00B41E70, then 549 entries at 0x00BE1198) and once
    // per `/reload`. We **don't hook this** — third-party DLLs
    // (SuperWoWhook, transmogfix, nampower, etc.) all hook here too, and
    // chaining their modifications to the `(names, count)` args makes the
    // final table size unreliable (crashes seen with count → buffer-size
    // mismatch). Instead, `Event::Custom` claims NULL slots from the live
    // table after the rebuild settles. Kept for reference.
    FUN_REBUILD_EVENT_TABLE = 0x00703D90,

    // Event-table allocator — `__thiscall(EventObject *this, uint32 count)`.
    // Called by `RebuildEventTable` to (re)size the entry array. SuperWoW and
    // nampower co-hook it to destructively clamp the FrameXML count (`> 200`;
    // glue is smaller) to 700. We deliberately DON'T hook it: our default is
    // claim-NULL into the engine's ~200 low gaps (which always suffices), and
    // when a grow IS needed `Event::Custom::Grow` reallocates the buffer +
    // swaps the globals directly (bypassing this clamp) rather than routing a
    // size through here. Also documents the empty-entry init the grow mirrors
    // ({name=0, +4=0, +8=&self, +0xC=(&self)|1}). Kept for reference.
    FUN_FRAMESCRIPT_SET_EVENT_COUNT = 0x007053B0,

    // `FireEvent` — the engine's event dispatcher. 149 callers in the
    // binary. `__cdecl void(int eventID, const char *format, ...)`.
    // Indexes into `[VAR_EVENT_TABLE_BASE_PTR] + eventID * 0x10` and walks
    // the subscriber chain at `+0x0C`. Format codes match printf:
    //   %s = const char *
    //   %d = int
    //   %u = unsigned int
    //   %f = float (promoted to double on the stack)
    // No `%b` for boolean — pass `%d` with 0/1; the engine has no native
    // bool concept here. No bounds check on eventID; pass valid indices.
    FUN_FIRE_EVENT = 0x00703F50,
    // Per-token unit-event broadcast. Fires unit event `eventCode` for every
    // unit token currently mapping to `*guid` (target / party / raid /
    // nameplateN / mouseover / focus), via the engine's own GUID->token reverse
    // map -- the same path the descriptor-field observers drive on a real field
    // change. `void __fastcall(uint64_t *guid, uint32_t eventCode)`; verified
    // against nampower's SendUnitSignal (0x00515e50). `eventCode` is the unit
    // field index, which doubles as the event id (see UNIT_EVENT_UNIT_AURA).
    FUN_UNIT_EVENT_BROADCAST = 0x00515E50,
    // Event name strings live in `.data` (mostly clustered around
    // 0x00851000..0x00855000); event-name string pointers also reach into
    // `.rdata` (starts at 0x007FF000) for some entries. Bound the dereference
    // range conservatively to "static-data section" — addresses outside this
    // window cannot be valid string pointers in this binary, so we treat
    // them as NULL during the walk.
    EVENT_TABLE_VALID_PTR_LO = 0x007FF000,
    EVENT_TABLE_VALID_PTR_HI = 0x00C00000,

    // Storm allocator — Blizzard's internal C utility library. `SMemAlloc`
    // wraps every block with bookkeeping (header magic + size + caller
    // file:line, footer magic, plus a global registered-block list); the
    // matching `SMemFree` validates against all of that and panics with
    // `ERROR #124 SMem3: Pointer does not refer to a valid allocated block
    // of memory` if the pointer didn't come from `SMemAlloc`.
    //
    // Both are `__stdcall`, 4 args, `ret 0x10`. Verified via disassembly
    // at the function bodies (push order matches arg order; `ret 0x10`
    // pops the 4 stack args). The Storm allocator instance both wrap is
    // the singleton at `0x00C51C58`.
    //
    //   void *__stdcall SMemAlloc(size_t size, const char *file, int line,
    //                             int flags); // flags: bit 3 (0x08) = zero-fill
    //   int   __stdcall SMemFree (void *ptr,    const char *file, int line,
    //                             int flags); // returns 1
    //
    // We use `SMemAlloc` for event-name storage in `Event::Custom` so the
    // engine's reload teardown loop (which `SMemFree`s every event entry's
    // name) doesn't trip its safety check on our injected pointers.
    FUN_STORM_SMEM_ALLOC = 0x006462E0,
    FUN_STORM_SMEM_FREE = 0x00646430,

    // Engine's main file-content reader. Used by the TOC parser at
    // `FUN_TOC_PARSER` to slurp addon `.toc` files and by the addon
    // load pass for `.lua` source. Allocates a Storm buffer via
    // `FUN_STORM_SMEM_ALLOC`, writes the file content + `extraBytes`
    // trailing zeros, hands the buffer to the caller. Caller frees
    // via `FUN_STORM_SMEM_FREE`.
    //
    //   __stdcall int FileRead(    // callee cleans 28 bytes (RET 0x1C);
    //                              // NOT __cdecl — see src/addons/Embedded.cpp
    //       int          archive,        // 0 = merged VFS; or an archive
    //                                    //   handle to read from one MPQ
    //                                    //   (e.g. FUN_00648FB0 reads its
    //                                    //   "(listfile)" this way)
    //       const char  *path,           // "Interface\AddOns\X\Y.lua"
    //       void       **outBuf,         // Storm-allocated content
    //       size_t      *outSize,        // optional, may be NULL
    //       size_t       extraBytes,     // trailing zero-fill (1 for nul-term)
    //       int          flag1,          // always 1 from TOC parser
    //       int          flag2);         // 0
    //   Returns 1 on success, 0 on failure.
    //
    // We hook this to redirect reads of `Interface\AddOns\!!!ClassicAPI\*`
    // to embedded buffers (see src/addons/Embedded.cpp). Allocating via
    // the same Storm allocator the engine uses means the caller's
    // normal `SMemFree` works without us tracking lifetime.
    FUN_FILE_READ = 0x00648620,

    // MPQ file enumerator — walks every mounted archive's `(listfile)`
    // and invokes a callback for each entry whose path begins with a
    // given prefix. This is the engine's flat, recursive name walk (the
    // `SFileEnumFiles` analog), NOT a per-directory find. It's the
    // function the macro-icon loader `FUN_LOAD_MACRO_ICONS` uses to
    // surface MPQ-baked icons under `Interface\Icons\`.
    //
    //   void __fastcall MpqEnumFiles(
    //       int         archiveSelector, // ecx — archive-set selector;
    //                                     //       the loader passes 6.
    //                                     //       Covers the primary
    //                                     //       mounted archives plus
    //                                     //       one selected extra.
    //       const char *pathPrefix,      // edx — e.g. "Interface\" —
    //                                     //       only listfile entries
    //                                     //       under this prefix are
    //                                     //       reported.
    //       FileCb      callback,         // per-file callback (below)
    //       void       *userParam);       // loader passes 0
    //
    // The callback is `int __fastcall(const char *fullPath /*ecx*/)`;
    // `fullPath` is the entry's full archive-relative path (e.g.
    // "Interface\FrameXML\Foo.lua"). Returning 0 STOPS enumeration —
    // return non-zero to keep going (note: the OPPOSITE of the disk
    // find at `FindFirstFileW`). The continue/stop sense is verified in
    // the per-archive walker `FUN_00648FB0`, whose loop does
    // `if (... && callback() == 0) break;`.
    //
    // Internally: `FUN_00401470` loops the archive-handle array
    // (`DAT_008826BC`, count `DAT_00882740`) calling the per-archive
    // listfile walker `FUN_00648FB0`, which reads each archive's
    // `(listfile)` via `FUN_FILE_READ`, splits on CR/LF, prefix-filters
    // via a trampoline, and dispatches the callback. Used by
    // `Interface::Export` (`ExportInterfaceFiles`).
    //
    // NB: the sibling `FUN_0042AD10` is a DISK-only walker (it resolves
    // to kernel32 `FindFirstFileW` and never sees MPQ contents) — don't
    // confuse the two. The macro `Icons.cpp` callback labels have the
    // disk/MPQ roles swapped; this entry is the authoritative note.
    FUN_MPQ_ENUM_FILES = 0x00401470,

    // Console-command registrar — the vanilla equivalent of 4.3.4's
    // `FUN_00654c90`. Registers a developer-console command (the `~`
    // console you get when launching with `-console`).
    //
    //   int __fastcall RegisterConsoleCommand(
    //       const char *name,        // ecx — command name; stored BY
    //                                 //       POINTER (no copy), so must
    //                                 //       have static lifetime
    //       Handler     handler,     // edx — see below
    //       int         category,    // help-grouping category id
    //       const char *description);// help text, or NULL
    //   Returns 1 on success, 0 if a command with that name already
    //   exists (dedup-safe — repeat calls are harmless no-ops). The
    //   command node is never freed for a static-literal name, so we
    //   don't need SStrDup the way the event table does.
    //
    // The handler is `int __fastcall(void *unused /*ecx*/, const char
    // *args /*edx*/)`; `args` is the text after the command name
    // (`*args == '\0'` when invoked bare). Return 1. Categories seen in
    // the binary: graphics=1, help=2, misc=5 (we use 5, matching
    // 4.3.4's `ExportInterfaceFiles`); the value is cosmetic (only
    // affects `help` grouping) and unvalidated by the registrar.
    //
    // Self-initializes the command hash table on first use, so it is
    // safe to call any time after the engine's own console subsystem
    // boots (well before our glue / load-script hooks fire). Verified
    // via the bulk graphics registrar `FUN_0066F6C0` and the `help`
    // command's own registration in `FUN_006400E0`.
    FUN_CONSOLE_COMMAND_REGISTER = 0x0063F9E0,

    // `ConsoleWrite(const char *line /*ecx*/, int colorFlag /*edx*/)` —
    // appends a line to the developer console's output buffer. No-ops
    // cleanly when the console isn't active (gated on the console-init
    // globals `DAT_00C4EC1C` / `DAT_00C4EC34`), so it's safe to call
    // unconditionally from a console-command handler. `colorFlag` 0 =
    // default. Used by `Interface::Export` for "wrote N files" feedback.
    FUN_CONSOLE_WRITE = 0x0063CB50,

    // TOC parser — `__fastcall(this=addonName)`. Already documented in
    // CLAUDE.md under "AddOn registry & hot-reload". Dedup-safe: if
    // the addon is already in the hash table, returns immediately
    // without re-parsing. Reads the TOC file via `FUN_FILE_READ`. We
    // call this from the post-`FUN_ADDON_INIT` hook to register the
    // embedded `!!!ClassicAPI` addon — the dedup behavior means our
    // call is a no-op when the user has the addon installed on disk.
    FUN_TOC_PARSER = 0x0051C9B0,

    // Client interface version — `__cdecl() -> uint32` (no args), a bare
    // `return 0x2BC0` (11200). The addon loadability resolver `FUN_0051e780`
    // compares an addon's parsed `## Interface:` number (entry+0x1C) against
    // this; a mismatch is the OUT_OF_DATE reason (code 7) that blocks loading
    // while the `checkAddonVersion` cvar is on. `AddOns::TocRewrite` calls it
    // to recognize the client version inside a multi-flavor comma-list.
    FUN_ADDON_CLIENT_INTERFACE_VERSION = 0x0051D7D0,

    // Per-addon file loader — `__fastcall(char *tocPath, int *bindingsCtx,
    // int *progress) -> uint32`. `FUN_0051f240` calls it once per addon with
    // `Interface\AddOns\<Name>\<Name>.toc`; it reads the TOC and runs each
    // referenced Lua/XML file. Vanilla runs it BEFORE the addon's
    // SavedVariables are loaded (files execute with SV nil), so
    // `Addons::SavedVarsFirst` co-hooks it to honor `## LoadSavedVariablesFirst`
    // by loading the SV first for flagged addons.
    FUN_ADDON_LOAD_FILES = 0x006EDB90,

    // Read + run a Lua file — `__fastcall(const char *path, void *checksumOut,
    // void *errCtx) -> uint32`. Reads via `FUN_FILE_READ`, then compiles +
    // pcalls; returns 0 (and fires errCtx's callback only when errCtx != null)
    // if the file is missing, so it is safe to call on an absent path. How the
    // engine loads each SavedVariables `.lua` (and how addon files run).
    FUN_LUA_LOAD_FILE = 0x00704BC0,

    // File-exists probe — `__stdcall(const char *path, int mode)` (RET 8);
    // nonzero when the file exists. `mode = 1` on the addon / SavedVariables
    // paths. Used to mirror the engine's SV path fallback (prefer the
    // realm-scoped per-character file, else the realm-less one).
    FUN_FILE_EXISTS = 0x00648A30,

    // `realmName` CVar value reader — no-arg, `-> const char *` (last-connected
    // realm's display name; lazily registers the cvar). The realm segment of
    // the per-character SavedVariables path. (The character segment is
    // FUN_GET_LOGIN_ACCOUNT_NAME, whose label is a misnomer — it returns the
    // 0x00C27D88 buffer, NULL until a character is logged in.)
    FUN_GET_REALM_NAME = 0x005AB7D0,

    // AddOn registry init — `__fastcall(accountName)`. Documented in
    // CLAUDE.md ("AddOn registry & hot-reload"). Hooked post-call so
    // we can splice the embedded addon entry into the freshly-populated
    // registry before the load pass.
    FUN_ADDON_INIT = 0x0051C740,

    // Generic intrusive-list insert/move helper used by `FUN_TOC_PARSER`
    // to thread new addon entries into the registry's doubly-linked
    // list. Signature (`__thiscall`):
    //   `(ListCtrl *this, void *entry, int position, int anchor)`
    // Where:
    //   - `this`        — list-control struct (for the addon registry,
    //                      this is `&VAR_ADDON_LIST_CTRL`).
    //   - `entry`       — entry to insert. If already linked, the
    //                      function removes it from its current
    //                      position first.
    //   - `position`    — `1` = insert at head, anything else (engine
    //                      passes `2`) = insert at tail.
    //   - `anchor`      — `0` = anchor against the list sentinel
    //                      (default), nonzero = anchor relative to
    //                      another entry's link field at that offset.
    //
    // Used by `Addons::Embedded` to move our injected `!!!ClassicAPI`
    // entry from the tail (where `FUN_TOC_PARSER` puts it) to the head
    // — vanilla's addon load pass at `FUN_0051F600` walks the linked
    // list in insertion order, so without this fix our addon would
    // load *last*, breaking dependent addons that expect our globals
    // to be ready in their main chunk.
    FUN_INTRUSIVE_LIST_INSERT = 0x00521AD0,

    // Addon-registry intrusive-list control. Layout
    // (`__thiscall`-friendly, generic across all intrusive lists in the
    // binary):
    //   +0x00 (= `0x00BE1B64`) — `int` next-pointer offset within each
    //                              entry (= `0xC` at runtime; entries
    //                              store their next-link at
    //                              `entry+0xC..0x10`).
    //   +0x04 (= `0x00BE1B68`) — sentinel `next` field
    //                              (points back into &sentinel+1 when
    //                              the list is empty).
    //   +0x08 (= `0x00BE1B6C`) — list head (alias for sentinel `prev`
    //                              when treated as circular).
    //
    // `VAR_ADDON_LIST_CTRL` is just the address of the control struct
    // (i.e., `0x00BE1B64`). Pass `&VAR_ADDON_LIST_CTRL` as the `this`
    // arg to `FUN_INTRUSIVE_LIST_INSERT`.
    VAR_ADDON_LIST_CTRL = 0x00BE1B64,

    // `SStrDup(const char *src, const char *file, int line)`. `__stdcall`.
    // Storm's string-copy wrapper around `SMemAlloc` — used by the engine
    // itself to populate event entry names. We use it from `Event::Custom`
    // so the engine's reload-teardown `SMemFree` sees blocks that came
    // from `SMemAlloc` (which it requires).
    FUN_STORM_SSTRDUP = 0x0064A620,

    // Storm allocator/free — the pair the engine uses for the event-table
    // buffer, so its reload teardown `SMemFree`s our grown buffer as its own.
    // Both `__stdcall`, 4 args (`ret 0x10`); SMemAlloc flag bit 8 (`& 8`)
    // zero-fills. `Event::Custom::Grow` uses them to reallocate the event
    // buffer. (Same addresses as FUN_STORM_SMEM_ALLOC / _FREE above.)
    //   void*  __stdcall SMemAlloc(uint size, const char *file, int line, int flags)
    //   char   __stdcall SMemFree (void *ptr, const char *file, int line, int flags)
    FUN_STORM_SMEMALLOC = 0x006462E0,
    FUN_STORM_SMEMFREE = 0x00646430,

    // Talent system — per-player talent state populated at login from
    // (class, race) + Talent.dbc / TalentTab.dbc. The engine maintains
    // one `TabInfo *` per talent tab in the array at
    // `[VAR_TALENT_TAB_INFO_ARRAY]`, with the count at
    // `[VAR_TALENT_TAB_COUNT]` (= 3 for all vanilla classes).
    // Verified by `Script_GetTalentTabInfo` (`0x004F3040`),
    // `Script_GetNumTalents` (`0x004F3160`), and `Script_GetTalentInfo`
    // (`0x004F3200`) — all three read the same pair of globals.
    //
    // TabInfo struct:
    //   +0x00  int           TalentTab.dbc rowID
    //   +0x04  int           pointsSpent
    //   +0x08  int           numTalents (in this tab)
    //   +0x0C  TalentEntry * talents (array, 0-indexed)
    //
    // TalentEntry, stride 0x54:
    //   +0x00         uint32 talentID (Talent.dbc primary key)
    //   +0x04..+0x0F  other fields (tier, column, ...)
    //   +0x10..+0x33  uint32 SpellRank[9] (rank-N spellID at index N-1)
    //   +0x34..+0x53  prereqs / flags / other
    //
    // Vanilla 1.12 talents have at most 5 ranks, so SpellRank[5..8] are
    // always zero; engine reserves 9 slots, we expose the same range.
    //
    // SpellRank offset verified by tracing the engine's currentRank-
    // derivation loop in `Script_GetTalentInfo`: it `lea edx, [ebx+0x30]`
    // (cur = highest slot) then walks **downward** with `sub ecx, 4`
    // (visible at 0x004F3305) for 9 iterations, ending at +0x10. So
    // SpellRank[0] (rank 1's spellID) is at +0x10, SpellRank[8] (rank 9
    // sentinel) at +0x30. Confirmed empirically: a debug build reading
    // `[entry+0x30]` returned 0 for Inner Focus (rank 1, 14751); the
    // value is at `[entry+0x10]`.
    VAR_TALENT_TAB_COUNT = 0x00BDCD24,
    VAR_TALENT_TAB_INFO_ARRAY = 0x00BDCD28,
    OFF_TABINFO_NUM_TALENTS = 0x08,
    OFF_TABINFO_TALENT_ARRAY = 0x0C,
    OFF_TALENT_SPELL_RANK = 0x10,
    TALENT_ENTRY_STRIDE = 0x54,
    TALENT_MAX_RANKS = 9,

    // Talent.dbc — standard 5-DWORD class shape, records pointer at
    // `+0x08` and count at `+0x0C` of the class instance at
    // `0x00C0D6E0`. Records is an array of `TalentRec *` indexed
    // directly by talentID (sparse — many slots NULL since talent IDs
    // skip across classes). Used by `GameTooltip:SetTalentByID` to
    // resolve cross-class talents that aren't in the local player's
    // loaded TabInfo arrays.
    //
    // Record layout (verified standard vanilla Talent.dbc schema):
    //   +0x00  uint32  ID
    //   +0x04  uint32  TabID
    //   +0x08  uint32  TierID (row, 0-based)
    //   +0x0C  uint32  ColumnIndex (0-based)
    //   +0x10  uint32  SpellRank[0..8]   — rank-1 spellID at +0x10
    //
    // The per-player `TalentEntry` (in `TabInfo->talents[]`) shares
    // the same SpellRank offset, which is why we can reuse
    // `OFF_TALENT_SPELL_RANK` here without redefining it.
    VAR_TALENT_DBC_RECORDS = 0x00C0D6E8,
    VAR_TALENT_DBC_COUNT = 0x00C0D6EC,

    // Flat (row-order) DBC arrays used by the talent-tree builder
    // `FUN_004f2c00` — DISTINCT from the sparse by-ID index above. The builder
    // walks TalentTab.dbc in row order, filtering each tab by
    // `FUN_004f2e50(race, class, raceMask@+0x2c, classMask@+0x30)` (a plain
    // bitmask test; mask 0 = all), then walks Talent.dbc in row order grouping
    // contiguous TabID runs — so `GetTalentInfo(tab, idx)` ordering IS just
    // "TalentTab rows for the class in row order" × "Talent.dbc rows for that
    // TabID in row order". Reading these arrays with the same walk (but an
    // arbitrary class-mask) reproduces the engine's ordering for any class,
    // which is how `GetTalentIDByIndex`'s optional classID arg works.
    //
    // Each global holds a base POINTER (deref) to the contiguous record array;
    // the sibling +4 holds the record count. Strides match the by-ID records:
    // TalentTab 0x3c, Talent 0x54 (TALENT_ENTRY_STRIDE).
    //   TalentTab record: ID@+0x00, raceMask@+0x2c, classMask@+0x30
    //   Talent record:    ID@+0x00, TabID@+0x04 (rest per the by-ID layout)
    VAR_TALENTTAB_DBC_FLAT_RECORDS = 0x00C0D6CC,
    VAR_TALENTTAB_DBC_FLAT_COUNT = 0x00C0D6D0,
    TALENTTAB_ENTRY_STRIDE = 0x3c,
    OFF_TALENTTAB_ID = 0x00,
    OFF_TALENTTAB_CLASS_MASK = 0x30,
    VAR_TALENT_DBC_FLAT_RECORDS = 0x00C0D6E0,
    VAR_TALENT_DBC_FLAT_COUNT = 0x00C0D6E4,
    OFF_TALENT_DBC_TAB_ID = 0x04,

    // Engine's `Script_GetTalentInfo` Lua C function. We call it from
    // `GetTalentSpellID` to derive the player's currentRank without
    // re-implementing the spell-knowledge checks at `0x0060C740` /
    // `0x0060C8D0`. Signature: `int __fastcall(void *L)` — reads tab and
    // talent index from Lua stack[1] and stack[2], pushes 6 returns
    // (name, icon, tier, column, currentRank, maxRank). Errors (calls
    // lua_error) on invalid input or pre-login state, so callers must
    // pre-validate.
    FUN_SCRIPT_GET_TALENT_INFO = 0x004F3200,

    // Game-time struct populated by SMSG_LOGIN_VERIFY_WORLD /
    // SMSG_LOGIN_SETTIMESPEED. The engine maintains it as the current
    // server clock, advanced by an internal tick handler. `Script_GetGameTime`
    // (`0x00515EE0`) reads `[+0x04]` and `[+0x00]` directly via `fild`,
    // and the engine's "to days-since-epoch" helper at `0x00642320`
    // builds a `struct tm` from `[+0x0C..+0x14]` and calls `_mkgmtime`,
    // confirming the field layout.
    //
    //   +0x00  int  minute     0..59
    //   +0x04  int  hour       0..23
    //   +0x08  ?               (uncertain — possibly seconds, treated as
    //                            0 by the engine's known consumers)
    //   +0x0C  int  day        0-based; engine `inc`s before storing as
    //                            tm_mday (which is 1-based)
    //   +0x10  int  month      0-based (0=Jan, matches tm_mon directly)
    //   +0x14  int  year       full year (e.g. 2026), engine normalizes
    //                            via `(year % 100) + 100` for tm_year
    //
    // Pre-login the struct is BSS-zero; year=0 is the "uninitialized"
    // sentinel we check for.
    VAR_GAMETIME_STRUCT = 0x00CE8538,
    OFF_GAMETIME_MINUTE = 0x00,
    OFF_GAMETIME_HOUR = 0x04,
    OFF_GAMETIME_DAY = 0x0C,
    OFF_GAMETIME_MONTH = 0x10,
    OFF_GAMETIME_YEAR = 0x14,

    // AddOn registry. The engine keeps a flat 4-byte-stride array of
    // `AddOnEntry *` at `[VAR_ADDON_ARRAY]`, with the in-use count at
    // `[VAR_ADDON_COUNT]`. Each entry's first 12 bytes are an inline
    // null-terminated name buffer, so an `AddOnEntry *` can be passed
    // directly to `lua_pushstring` to push the addon's name (which is
    // exactly what `Script_GetAddOnInfo` does for ret1).
    //
    // Per-field accessors (all `__fastcall(ecx=name) → char* or NULL`):
    // each one looks up the addon by name in the engine's hash table,
    // then reads a single hardcoded field from the addon's metadata
    // hash table and returns its string value (NULL if the addon
    // doesn't exist or the field is missing). The `name` arg can be
    // either a real C string OR an `AddOnEntry *` — the entry's
    // first 12 bytes are an inline null-terminated name, so it
    // doubles as a valid `const char *`.
    //
    // We use these directly in the `C_AddOns.*` wrappers to avoid
    // dispatching to `Script_GetAddOnInfo` (which pushes a 7-tuple
    // when we only want one field, and worse, errors via `lua_error`
    // for out-of-range numeric indices).
    FUN_ADDON_GET_BY_INDEX = 0x0051DF00,        // (idx0Based) → AddOnEntry* or NULL
    FUN_ADDON_GET_TITLE_BY_NAME = 0x0051DF20,   // (name) → title string or NULL
    FUN_ADDON_GET_NOTES_BY_NAME = 0x0051E050,   // (name) → notes string or NULL

    // `__fastcall(const char *nameOrEntry) → AddOnEntry*+0x48 or NULL`.
    // Hash-table lookup by name (accepts an entry pointer too — its
    // first 12 bytes are the inline name), returning a pointer to the
    // matched record's RequiredDeps descriptor (`record + 0x48`), or
    // NULL on a miss. Stock `Script_GetAddOnDependencies` (`0x0048E5E0`)
    // calls this then reads count@+4 / data@+8 off the return to walk
    // the required deps. We reuse it only to recover the record base for
    // string input: `record = resolve(name) - OFF_ADDON_REQDEPS_DESC`.
    FUN_ADDON_RESOLVE_REQ_DEPS = 0x0051E350,

    // AddOnEntry dependency-array descriptors are `{cap, count, data,
    // quantum}` (each field 4 bytes) laid out 0x10 apart, parsed by the
    // TOC parser `FUN_0051c9b0`:
    //     OptionalDeps  desc@+0x38  (count@+0x3C, data@+0x40)
    //     RequiredDeps  desc@+0x48  (count@+0x4C, data@+0x50) ← stock walks
    //     LoadWith      desc@+0x58
    // Optional offsets verified against the parser's append site at
    // `0x0051CDBA` (`mov esi,[rec+0x3C]` count, `lea edi,[rec+0x38]`);
    // required desc@+0x48 confirmed by `FUN_ADDON_RESOLVE_REQ_DEPS`'s
    // `lea eax,[rec+0x48]` return.
    OFF_ADDON_REQDEPS_DESC = 0x48,
    OFF_ADDON_OPTIONALDEPS_COUNT = 0x3C,
    OFF_ADDON_OPTIONALDEPS_ARRAY = 0x40,

    // Per-field accessors `Script_GetAddOnInfo` calls internally for
    // returns 5 (loadable), 6 (reason string), 7 (security category).
    // We dispatch to these directly so the wrappers don't need to
    // bounce through `Script_GetAddOnInfo` and unwind 7 returns.
    //
    //   IS_LOADED `__fastcall(entry) → byte`
    //     Hash-table lookup in the addon registry. Returns the byte
    //     at `entry+0x18`, which the addon loader (`FUN_0051F240`)
    //     writes `1` to when the addon has finished loading. Non-zero
    //     ⇒ the addon is loaded.
    //
    //   CAN_LOAD `__fastcall(entry, char fullCheck=1, int *outStatus,
    //                        int *outDepHandle, const char *accountName) → byte`
    //     Recursive load-check that resolves all forward `## Dependencies`.
    //     On `0` return, writes one of 8 status codes into `*outStatus`
    //     (see `VAR_ADDON_LOADABLE_REASON_TABLE` for the index→string
    //     mapping). `outDepHandle` carries a dep-pointer when status is 0
    //     ("DEP: %s" path), unused otherwise. We pass `&zeroDep` for
    //     `outDepHandle` — we don't render the dep-name into the reason
    //     since the modern API gives just the status enum.
    //
    //   GET_REASON_STRING `__thiscall(int statusCode, char *buf, uint cap)`
    //     Copies the status-code's static string into `buf`, or formats
    //     `"DEP: <name>"` when `this == 0`. We bypass this entirely and
    //     index the table directly (see `VAR_ADDON_LOADABLE_REASON_TABLE`).
    //
    //   GET_SECURITY_INDEX `__fastcall(entry) → uint`
    //     Hash-table lookup that returns the security category index
    //     (0=SECURE, 1=INSECURE, 2=BANNED). Defaults to `1` (INSECURE)
    //     when the entry isn't in the override table.
    FUN_ADDON_IS_LOADED = 0x0051E6F0,
    FUN_ADDON_CAN_LOAD = 0x0051E780,
    FUN_ADDON_GET_SECURITY_INDEX = 0x0051E990,

    // Per-character enable-state resolver — the inner check
    // `FUN_ADDON_CAN_LOAD` (`0x0051E780`) consults to decide whether an
    // addon is enabled: `__fastcall(const char *name /*ecx*/,
    // const char *account /*edx*/, char resolveAll /*stack*/) -> uint`.
    // Returns 0 = disabled (load pass then skips the addon, reason
    // DISABLED), non-zero = enabled (2 = "enabled for all"). Walks the
    // per-character override list at `0x00BE1BD8`; with no override it
    // falls back to the entry's DefaultState byte (`+0x2b`):
    // `-(uint)(entry[+0x2b] != 0) & 2`. `Addons::Embedded` co-hooks this
    // to force-enable the embedded `!!!ClassicAPI` unconditionally — so a
    // stale WTF disable-override (from back when the addon was still
    // shown in the character-select list) can't leave it hidden AND
    // unloaded. Cold: runs only during the login/reload load pass, once
    // per addon.
    FUN_ADDON_ENABLE_RESOLVE = 0x0051E470,

    // Returns a pointer to the inline-stored login account name buffer
    // at `0x00C27D88`, or NULL if no character is logged in yet.
    // `Script_GetAddOnInfo` feeds this into the loadable + enabled
    // checks; same convention.
    FUN_GET_LOGIN_ACCOUNT_NAME = 0x005ABDC0,

    // Two parallel `const char *[]` tables in `.data` covering every
    // string `Script_GetAddOnInfo`'s returns 6 + 7 can produce.
    //
    //   LOADABLE_REASON_TABLE — 8 entries indexed by the status code
    //     `FUN_ADDON_CAN_LOAD` writes:
    //       0 LOADABLE          (only emitted for the DEP fallback)
    //       1 MISSING
    //       2 DISABLED
    //       3 BANNED
    //       4 CORRUPT
    //       5 INSECURE
    //       6 NOT_DEMAND_LOADED
    //       7 INTERFACE_VERSION
    //
    //   SECURITY_TABLE — 3 entries indexed by the value
    //     `FUN_ADDON_GET_SECURITY_INDEX` returns. Lives at
    //     `LOADABLE_REASON_TABLE + 8 * sizeof(char *)` — the two
    //     tables are physically contiguous in the binary.
    //       0 SECURE
    //       1 INSECURE
    //       2 BANNED
    VAR_ADDON_LOADABLE_REASON_TABLE = 0x0085365C,
    VAR_ADDON_SECURITY_TABLE = 0x0085367C,
    VAR_ADDON_ARRAY = 0x00BE1B94,
    VAR_ADDON_COUNT = 0x00BE1B90,

    // NOTE: an earlier revision defined OFF_PLAYER_FIELD_VIS_BYTES /
    // PLAYER_VIS_BIT_STEALTH here at 0x17C, believing bit 0x02 was a
    // "stealth bit". That was wrong: 0x17C is UNIT_FIELD_AURALEVELS
    // (per-slot caster-level bytes; see OFF_UNIT_FIELD_AURALEVELS above),
    // and the old read only appeared to work by coincidence (a caster-
    // level byte that happened to have bit 0x02 set on the test char;
    // at level 60 = 0x3C it doesn't, so it read false). The real stealth
    // flag is UNIT_FIELD_BYTES_1 byte 3 (UNIT_BYTES_1_STEALTH_FLAG); see
    // that field above. IsStealthed reads it.

    // Mount display ID — the creature display ID the engine renders
    // under the mounted player. Zero when not mounted, non-zero
    // (typically a 4-digit creature display ID like 0x4306) when
    // mounted. Broadcast in UpdateFields, so works for any unit's
    // descriptor — though we only expose `IsMounted()` for the
    // player to match modern API semantics.
    OFF_UNIT_FIELD_MOUNTDISPLAYID = 0x1FC,

    // Local player movement-flags u32. Lives directly on the
    // CGPlayer object (not the broadcast descriptor) — this is
    // client-side state the engine maintains for outbound
    // MSG_MOVE_* packets, never visible for remote units. Found
    // empirically by diffing _classicapi_PlayerLog snapshots
    // (standing/swimming/falling).
    //
    // Bit values match vanilla MOVEFLAG_* protocol constants —
    // verified by matching observed values to the documented
    // 1.12 movement-flag set:
    //   0x0001 FORWARD     0x2000  FALLING (in-air, moving down)
    //   0x4000 FALLING_FAR (extended fall)
    //   0x8000 PENDING_STOP
    //   0x200000 SWIMMING
    //   0x1000000 FLYING (no flying mounts in vanilla, but the bit
    //                     exists for druid Flight Form etc.)
    OFF_PLAYER_MOVEMENT_FLAGS = 0x9E8,
    MOVEFLAG_FORWARD = 0x1,
    MOVEFLAG_BACKWARD = 0x2,
    MOVEFLAG_STRAFE_LEFT = 0x4,
    MOVEFLAG_STRAFE_RIGHT = 0x8,
    // 0x10 / 0x20 are TURN_LEFT / TURN_RIGHT — turn-in-place, not
    // translational movement. Excluded from "moving" semantics
    // because modern `PLAYER_STARTED_MOVING` doesn't fire for them.
    MOVEFLAG_JUMPING = 0x1000,
    MOVEFLAG_FALLING = 0x2000,
    MOVEFLAG_FALLING_FAR = 0x4000,
    MOVEFLAG_SWIMMING = 0x200000,
    // The player-movement bits that drop an in-progress cast — the exact mask
    // the engine's CheckCast (FUN_006094f0) tests (0x200f). Spell::Cast uses it
    // to tell a spurious movement drop of a movement-immune cast (grenades)
    // from a real cancel/interrupt, which happens with the player stationary.
    MOVEFLAG_MASK_CAST_DROP = MOVEFLAG_FORWARD | MOVEFLAG_BACKWARD |
                              MOVEFLAG_STRAFE_LEFT | MOVEFLAG_STRAFE_RIGHT |
                              MOVEFLAG_FALLING, // = 0x200f

    // CGPlayer-local pointer to the GameObject the current spell cast
    // is targeting. Holds a heap pointer (high bits in the user-mode
    // range, e.g. `0x42908708` / `0x52FA1A08`) while the player is
    // casting/channeling onto a GameObject; cleared on cast end or
    // movement break. Local-only — does NOT broadcast.
    //
    // Empirically distinct from UNIT_FIELD_CHANNEL_OBJECT — the engine
    // uses one or the other depending on the cast shape:
    //
    //   - **Set** during: clicking a warlock summoning portal (channel-
    //     spell that doesn't populate UNIT_CHANNEL_OBJECT); mining
    //     (regular spell cast on an ore node).
    //   - **0** during: warlock channeling a spell (no GO target);
    //     fishing (uses UNIT_CHANNEL_OBJECT for the bobber GUID);
    //     herb gathering (no spell, pure GO-loot mechanism); mailbox /
    //     gossip / sit / NPC interaction (instant, no animation).
    //
    // Used together with UNIT_FIELD_CHANNEL_SPELL to detect ritual
    // participation — see `IsAssistingRitual()` in `unit/State.cpp`.
    // The combined check `channelSpell != 0 && castGameObject != 0`
    // uniquely identifies the portal-clicker state in 1.12 (mining
    // fails `channelSpell != 0`; warlock/fishing fail
    // `castGameObject != 0`).
    OFF_CGPLAYER_CAST_GAMEOBJECT_PTR = 0xB4,

    // Indoor/outdoor query for a unit — backs `IsIndoors()` / `IsOutdoors()`
    // (`unit/State.cpp`). `__fastcall(unit) -> uint` where **nonzero =
    // outdoors**, 0 = indoors. A 2-instruction thunk: reads the unit's
    // world/collision-context pointer at `unit + 0xE0` and tail-jumps to the
    // WMO query `FUN_006706f0`. That query is fully null-safe — null context
    // or a unit not inside a WMO group returns 1 (outdoors); otherwise it
    // resolves the current WMO group via `FUN_0069d4f0` and returns bit 15
    // (`0x8000`) of the group's runtime flags word (the OUTDOOR bit).
    //
    // Derived from SuperWoW 2.2's `IsIndoors` handler (which does exactly
    // `unit = ObjMgr(0x10, PlayerGUID()); push (query(unit)==0)`), then
    // confirmed against the vanilla binary. Vanilla ships no Lua binding for
    // this; the 3.3.5 client's `IsIndoors` (`0x00612300`) uses the identical
    // shape (`unit + 0xB8` → WMO group flag test), so the semantics are
    // cross-checked across both clients.
    FUN_UNIT_IS_OUTDOORS = 0x00612cc0,

    // No `MOVEFLAG_FLYING` constant intentionally. Vanilla 1.12
    // doesn't flip a flying bit during taxi flights (verified
    // empirically: `IsFlying()` checking 0x01000000 stayed false
    // throughout a flightpath on which `UnitOnTaxi("player")`
    // correctly returned 1). Server-driven splines drive taxi
    // animation directly without touching the local movement-
    // flags word. Use `UnitOnTaxi` for taxi detection.

    // Equipment-slot index for the off-hand. Slots 1..19 are the
    // character-pane equipment slots (1=head … 19=tabard); 17 is
    // the off-hand slot the engine uses when looking up shields,
    // off-hand weapons, and held items via `ItemMgr::GetItemBySlot`.
    INVSLOT_OFFHAND = 17,

    // INVTYPE values that count as "weapon" for `OffhandHasWeapon`.
    // INVTYPE_WEAPON (13) covers any one-handed weapon equippable in
    // either hand (sword, axe, mace, dagger, fist). INVTYPE_WEAPONOFFHAND
    // (22) covers weapons that can ONLY go in the off-hand. Shields
    // (14), Holdables (23 — tomes/orbs), and empty slots all return
    // false. INVTYPE_2HWEAPON (17) is excluded because two-handers
    // occupy the main-hand slot exclusively.
    INVTYPE_WEAPON = 13,
    INVTYPE_WEAPONOFFHAND = 22,

    // Character-pane equipment slot range: 1..19 (head=1, neck=2, …,
    // tabard=19). Used by `C_Item.IsEquippedItem` to walk every slot
    // looking for a matching item.
    EQUIPMENT_SLOT_FIRST = 1,
    EQUIPMENT_SLOT_LAST = 19,

    // CGItem instance block GUID — qword at +0x00 of the instance block
    // (which sits behind the pointer at CGItem +0x08). Sibling of
    // OFF_INSTANCE_BLOCK_ITEM_ID at +0x0C in the same block.
    OFF_INSTANCE_BLOCK_GUID = 0x00,

    // `Script_ClearCursor` / `Script_PickupInventoryItem` Lua C
    // functions — both `__fastcall(void *L)`. ClearCursor takes no
    // args, drops anything held on the cursor. PickupInventoryItem
    // reads a 1-based character-pane slot at Lua stack[1] and lifts
    // that equipped item onto the cursor (or swaps with the existing
    // cursor contents). Used by `C_EquipmentSet.UseEquipmentSet` to
    // assemble the pickup→equip chain for each set item.
    FUN_SCRIPT_CLEAR_CURSOR = 0x004895B0,
    FUN_SCRIPT_PICKUP_INVENTORY_ITEM = 0x004C8DA0,
    // Equipment/bag-slot pickup PRIMITIVE behind Script_PickupInventoryItem
    // (`FUN_004C8DA0` is just `FUN_004C7300(arg - 1)`). Called directly:
    //   void __fastcall(uint slot0Based)
    // Self-contained — resolves the player, applies its own cursor/lock
    // guards, and handles the drop case (slot == 0xFFFFFFFF). Covers the
    // 0-based equipment (0..18) and equipped-bag (19..22) slots; container
    // contents go through FUN_CURSOR_PICKUP_ITEM instead. Modern
    // `equipmentSlotIndex` is 1-based, so pass `equipmentSlotIndex - 1`.
    FUN_PICKUP_INVENTORY_SLOT = 0x004C7300,

    // Bag-update fire sites — see TODO #67 for the full Ghidra
    // investigation. Hook these three to detect BAG_UPDATE fires and
    // emit BAG_UPDATE_DELAYED once per frame in which any fired.
    // Pattern-search confirms only three fire sites for `0x148`
    // (the BAG_UPDATE event ID) exist in 1.12.1's `.text`, and we
    // cover all three.
    //
    //   FUN_BAG_SLOT_DIFF (`0x004F91A0`) — __cdecl `void()` (zero
    //   args, ends with plain `RET`). Walks linear slots `0x13..0x16`
    //   (player bags 1..4) and `0x3B..0x44` (bank bags 5..10),
    //   comparing each against a cached GUID snapshot at
    //   `DAT_00BDD060`. Fires `BAG_UPDATE(bagID)` for each slot
    //   whose GUID changed. Called from per-session init
    //   (FUN_004F8CC0) and a packet handler (FUN_004F8EC0) — 2
    //   callers total, very quiet hook target.
    //
    //   FUN_BAG_ITEM_TO_BAG (`0x004F9370`) — __stdcall
    //   `void(int guidLo, int guidHi)` (ends with `RET 0x8`).
    //   Given an item GUID, searches the bag cache for which bag
    //   contains it. Fires `BAG_UPDATE(0)` for backpack,
    //   `BAG_UPDATE(N)` for bag N, returns silently if not in any
    //   bag. **Most common BAG_UPDATE path** — every per-item
    //   field-change goes through here. 3 callers, all within the
    //   bag subsystem at `0x004F9xxx`.
    //
    //   FUN_BAG_KEYRING_DESC_CHANGE (`0x004F8DB0`) — __thiscall
    //   `unsigned(void *bagDesc, uint a1, uint a2, uint a3, uint a4)`.
    //   Ends with `RET 0x10` — four 4-byte stack args, callee cleans
    //   16 bytes. (Ghidra showed three stack args; the Octo binary
    //   actually uses four. Mismatching this leaks 4 bytes of stack
    //   per call and crashes after a few descriptor updates.)
    //   Bag descriptor update callback. Computes the slot index
    //   from `((int)this - 0x4a8) >> 3`; for slots `0x51..0x70` (the
    //   keyring range) fires `BAG_UPDATE` directly. For non-keyring
    //   slots it delegates to `FUN_BAG_ITEM_TO_BAG`, so we'd
    //   double-count if we naively hooked both — the keyring branch
    //   is the only path here that doesn't already go through our
    //   other hook. Hooking is safe because the `g_pending` drain
    //   is idempotent (the flag stays true until WorldTick clears it).
    FUN_BAG_SLOT_DIFF = 0x004F91A0,
    FUN_BAG_ITEM_TO_BAG = 0x004F9370,
    FUN_BAG_KEYRING_DESC_CHANGE = 0x004F8DB0,
    // World-subsystem per-frame update — runs exactly once per frame
    // as part of the main world tick (`FUN_00482EA0`). Only one caller
    // in the entire binary; deep in the rendering/world pipeline,
    // nowhere near the chat/cast/transmog code regions that other
    // DLLs (SuperWoWhook / nampower / transmogfix) hook. We hook the
    // tail of this function to drain `BAG_UPDATE_DELAYED`'s pending
    // flag — exact per-frame coalescing matching modern WoW.
    //
    // Calling convention: `__fastcall(int ecx, int edx, int stackArg)`,
    // `RET 0x4` (callee cleans the one stack arg).
    FUN_WORLD_TICK = 0x0066FD50,

    // UI render root — the CSimpleTop singleton's (`DAT_00cf0bd8`) per-frame
    // render callback. Registered at priority 1.0 into the global HLAYER list
    // (`FUN_00442800`) and drained every frame by the master scene-render pump
    // `FUN_00442350`; body is just `FUN_00765650(root, ...)` (UI layout pass)
    // then `FUN_007657d0(root)` (the strata walk that draws every UI frame).
    // Unlike FUN_WORLD_TICK this fires in BOTH glue and world: the root ctor
    // `FUN_00764180` runs from the world-init path (`FUN_0048FBF0`) AND the
    // glue-boot path (`FUN_0046A7B0`), so the UI renders — and this callback
    // fires — on the login/character-select screens too. This is the tick to
    // drive UI-object upkeep that must also run on glue (inline-texture icon
    // regions), where FUN_WORLD_TICK is silent. `__stdcall(void *bounds,
    // int flag)`, `RET 0x8`; clean PUSH EBP / MOV EBP,ESP prologue (MinHook-
    // safe). Single caller (the HLAYER drain), quiet render region — not a
    // known collision target for the other Octo DLLs.
    FUN_UI_RENDER_ROOT = 0x00764330,

    // SMSG_BINDPOINTUPDATE handler. The server sends this packet on
    // initial login (to sync the player's current bind) and again
    // every time the player binds at a new innkeeper. The handler
    // deserializes `float x, y, z; uint32 mapID; uint32 areaID` into
    // the corresponding globals below and sets `VAR_BIND_POINT_VALID`
    // to 1.
    //
    //   __fastcall(void *packetBuffer)
    //
    // Single caller (the packet dispatcher's jump table for opcode
    // 0x155) — quiet hook target, no contention with other DLLs.
    // Hooked by `Player::HearthstoneBound` to fire `HEARTHSTONE_BOUND`.
    // Two packets must be suppressed so the event reflects only a real
    // innkeeper bind: (1) the initial post-login/char-switch sync
    // (gated on VAR_BIND_POINT_VALID being 0 before the handler runs),
    // and (2) Turtle's per-map-transition resync, which re-sends the
    // packet behind a loading screen — gated on VAR_IN_WORLD (the
    // player can't bind mid-load). See `Player::HearthstoneBound`.
    FUN_BINDPOINT_UPDATE_HANDLER = 0x005ED3C0,
    // "Bind point is valid" flag. Set to 1 by the BINDPOINTUPDATE
    // handler after the packet has been fully parsed. Zeroed by
    // `FUN_005E2510` (the per-character-entry init routine).
    // Reading this BEFORE the handler runs distinguishes initial
    // sync (`== 0`) from a later packet (`== 1`).
    VAR_BIND_POINT_VALID = 0x00C4D4E0,

    // SMSG_QUESTGIVER_QUEST_COMPLETE handler (opcode 0x191). Server
    // sends this after the client claims a quest reward via CMSG_QUESTGIVER_CHOOSE_REWARD
    // (opcode 0x18E, sent from `Script_GetQuestReward` → `FUN_005EADC0`).
    // Packet body, read in order via `FUN_PACKET_READ_UINT32`:
    //   [0] questID
    //   [1] unknown / flags
    //   [2] xpReward
    //   [3] moneyReward
    //   [4] numItems  (plus per-item itemID/count for each)
    //
    //   __thiscall(void *player, void *packetStream)
    //
    // Registered via `FUN_005AB650(0x191, ...)` alongside 8 other quest
    // opcodes that all dispatch through `FUN_005E59B0`'s switch — we
    // hook the per-opcode handler directly to keep our gate narrow.
    // Hooked by `Quest::TurnedIn` to fire `QUEST_TURNED_IN(questID, xp, money)`.
    FUN_QUEST_GIVER_QUEST_COMPLETE_HANDLER = 0x005DC400,

    // Packet-stream uint32 reader. `__thiscall(stream, uint32 *out)` —
    // reads 4 bytes from `stream + base + cursor`, advances cursor by
    // 4. Cursor lives at `stream + OFF_PACKET_STREAM_CURSOR`; save and
    // restore it to peek without consuming. Same reader the engine's
    // own packet handlers (FUN_005DC400, FUN_005ED3C0, etc.) use.
    FUN_PACKET_READ_UINT32 = 0x00418E30,
    OFF_PACKET_STREAM_CURSOR = 0x14,

    // Engine session globals used by `EquipmentSet::Storage` to build
    // the per-character WTF path. Same layout VanillaMinimapTracking
    // uses for its own persistence file. Account is the value WoW
    // writes the per-character WTF directory under — NOT the saved-
    // credentials CVAR. Character name is an inline buffer (NUL byte
    // at index 0 means "no active character yet"). Realm name lives
    // inside a struct one indirection in.
    VAR_ACCOUNT_NAME_PTR = 0x00BE1C0C,
    VAR_CHARACTER_NAME = 0x00C27D88,
    VAR_REALM_INFO_PTR = 0x00C28130,
    OFF_REALM_INFO_NAME = 0x20,

    // Player body yaw (orientation in radians, 0..2π) — the
    // character's facing direction in the world. Inline float on
    // CGPlayer; mirrored at `+0x9F8` (current vs last-broadcast).
    // Verified empirically: a clean 180° RMB-drag swings this by
    // ~π rad, and LMB-only camera orbits leave it untouched.
    // Used by `PLAYER_STARTED_TURNING` to detect actual character
    // rotation (vs the mouselook button merely being held).
    OFF_PLAYER_BODY_YAW = 0x9C4,

    // Camera object pointer chain. The game-state singleton lives
    // at `*0x00B4B2BC`; the camera object hangs off it at `+0x65B8`.
    // Verified via `Script_CameraZoomIn` (`0x0050B400`) which loads
    // this chain to call `FUN_0050FC60` on the camera. Returns the
    // camera struct or `nullptr` if the game state isn't yet
    // initialized (pre-login).
    VAR_GAME_STATE_PTR = 0x00B4B2BC,
    OFF_GAME_STATE_CAMERA_PTR = 0x65B8,

    // Camera yaw, relative to the character (radians). `0` =
    // camera-behind-character default. Accumulates as the user
    // LMB-orbits; stays at `0` during RMB-mouselook (camera rotates
    // *with* the character, so relative offset doesn't change).
    // Verified empirically: clean 180° LMB orbit moves this from
    // `0` to `~π`. Mirrored at `+0x210` and `+0x214` (smoothing /
    // last-applied buffers); we read the primary at `+0xF0`.
    OFF_CAMERA_RELATIVE_YAW = 0xF0,

    // UI input controller — heap-allocated struct holding the
    // engine's master input-state bitfield (mouselook / free-look /
    // turn-key / autorun / strafe-modifier / etc.). The slot at
    // `0x00BE1148` is a pointer; deref it to reach the controller,
    // which lives until the client tears down (rare in practice).
    // NULL during pre-login.
    VAR_UI_INPUT_CONTROLLER = 0x00BE1148,
    // Master input flags. Single u32 at `+0x04` of the controller,
    // touched by the engine's button-press/release handlers
    // (`FUN_00514840` / `FUN_00514B70`) on every mouse-button or
    // input-key transition.
    OFF_UI_INPUT_FLAGS = 0x04,
    // Bit 0 — `Script_IsMouselooking` checks this. Set when the
    // user is in mouselook mode (RMB-drag, or `MouselookStart`
    // called from Lua). Turning the character follows mouselook.
    INPUT_FLAG_MOUSELOOK = 0x01,
    // Bit 1 — set when the user is in free-look mode (LMB-drag).
    // Camera-only rotation; character doesn't turn.
    INPUT_FLAG_FREE_LOOK = 0x02,
    // Bits 4-7 — WASD movement-key state. Each `MoveForwardStart`
    // / `StrafeLeftStart` / etc. pushes one of these as the bit
    // mask into the engine's button-press handler. Bit `0x1000` is
    // the autorun-active flag (set by `ToggleAutoRun`).
    // `INPUT_FLAGS_MOVING_ANY` is the engine's own "is the user
    // currently inputting translational movement via keys/autorun"
    // mask, used to drive `PLAYER_STARTED_MOVING` — matches modern's
    // "STOPPED fires on key release, even mid-air" semantics because
    // no physics/airborne bit is involved.
    //
    // NOTE: the "hold both mouse buttons to run forward" gesture is
    // NOT in this mask. The button handler (`FUN_00514840`) drives
    // that forward move directly via `FUN_005103e0` and explicitly
    // *clears* the autorun bit when both buttons go down. Detect it
    // from `INPUT_FLAG_MOUSELOOK & INPUT_FLAG_FREE_LOOK` both held
    // instead — see `Player::InputEvents`.
    INPUT_FLAG_MOVE_FORWARD = 0x10,
    INPUT_FLAG_MOVE_BACKWARD = 0x20,
    INPUT_FLAG_STRAFE_LEFT = 0x40,
    INPUT_FLAG_STRAFE_RIGHT = 0x80,
    INPUT_FLAG_AUTORUN = 0x1000,
    INPUT_FLAGS_MOVING_ANY =
        INPUT_FLAG_MOVE_FORWARD | INPUT_FLAG_MOVE_BACKWARD |
        INPUT_FLAG_STRAFE_LEFT | INPUT_FLAG_STRAFE_RIGHT |
        INPUT_FLAG_AUTORUN,

    // CGUnit -> MovementInfo pointer. Holds the unit's per-instance
    // position / orientation / movement-flags / per-direction-speed
    // block. Both the local player and synced remote units carry
    // valid pointers here (NPCs included, when they exist as
    // CGUnits). Verified at multiple call sites that pass
    // `[unit + 0x118]` to `FUN_MOVEMENT_GET_EFFECTIVE_SPEED` — e.g.
    // `0x00511920` (sprint-jump motion calculator).
    OFF_UNIT_MOVEMENT_INFO_PTR = 0x118,

    // Inside the MovementInfo block:
    OFF_MOVEMENT_FLAGS = 0x40,        // u32 — engine's MOVEFLAG_* bits
    OFF_MOVEMENT_WALK_SPEED = 0x88,
    OFF_MOVEMENT_RUN_SPEED = 0x8C,
    OFF_MOVEMENT_RUN_BACK_SPEED = 0x90,
    OFF_MOVEMENT_SWIM_SPEED = 0x94,
    OFF_MOVEMENT_SWIM_BACK_SPEED = 0x98,

    // Effective-speed selector. `__thiscall float(this = MovementInfo,
    // int forceWalk) → float`. Returns the speed the engine would
    // apply to this frame's movement step — accounts for movement
    // flags (walking, swimming, backward), taxi paths, and the
    // walk/run/swim selection. Returns 0 when the unit isn't
    // moving (movement-flag bits 0..3 all clear). Used by 1.12's
    // movement-prediction loop; we call it to provide modern's
    // `currentSpeed` first return from `GetUnitSpeed`.
    FUN_MOVEMENT_GET_EFFECTIVE_SPEED = 0x007C4C90,

    // ------------------------------------------------------------------
    // Get*ItemID companions to the engine's Get*ItemLink functions.
    // Each offset block is the data path the corresponding
    // `Script_Get*ItemLink` reads from before calling
    // `FUN_DBCACHE_ITEMSTATS_GET_RECORD` — same source, just stop at
    // the itemID instead of building the link string.
    // ------------------------------------------------------------------

    // Loot rolls: an intrusive linked list of in-progress group loot
    // rolls. `FUN_LOOT_ROLL_FROM_ID(rollID) → entry *` walks it.
    // `Script_GetLootRollItemLink` reads the itemID at `entry+0x1C`.
    FUN_LOOT_ROLL_FROM_ID = 0x0061B2E0,
    OFF_LOOT_ROLL_ITEM_ID = 0x1C,

    // Loot slots: flat array indexed by `slot-1`, 0x1C-byte stride.
    // `LOOTABLE_FLAG` mirrors the engine's `[VAR_LOOT_LOOTABLE]` test
    // that drives whether slot indices are 1-based or 0-based; we
    // match the engine's `dec` behavior. itemID is at `entry+0`.
    VAR_LOOT_SLOTS = 0x00B7196C,
    VAR_LOOT_GUID_LO = 0x00B71B48,
    VAR_LOOT_GUID_HI = 0x00B71B4C,
    VAR_LOOT_LOOTABLE = 0x00B71BA0,
    LOOT_SLOT_STRIDE = 0x1C,
    LOOT_MAX_SLOTS = 0x10,
    // Per-slot field offsets within a `VAR_LOOT_SLOTS + slot * 0x1C`
    // entry. Decoded from `Script_GetLootSlotInfo` and the populator
    // at `FUN_004C1CB0`:
    //   +0x00  itemID         (Script_GetLootSlotInfo's first push)
    //   +0x04  displayInfoID  (icon lookup via DBC index)
    //   +0x08  count          (FUN_004C22E0 reads `[base+0x8]`)
    //   +0x0C  ?
    //   +0x10  ?
    //   +0x14  wire-slot byte (the server-side slot index — what
    //          `CMSG_LOOT_ITEM` expects; differs from the local
    //          densely-packed index when the response had gaps)
    OFF_LOOT_SLOT_COUNT = 0x08,
    OFF_LOOT_SLOT_WIRE_INDEX = 0x14,

    // `__fastcall(uint userFacingSlot, char *outBuf, int bufSize) →
    // const char *outBuf` — builds the loot-slot hyperlink string
    // (e.g. `|cff1eff00|Hitem:12345:0:0:0|h[Item Name]|h|r`) and
    // copies it into `outBuf`. Returns the buffer pointer on success,
    // `NULL` if no loot window is open, the slot is empty, or the
    // item's cache record isn't loaded yet. Used by
    // `Script_GetLootSlotLink` at `0x004C2D20`.
    //
    // `userFacingSlot` is the post-`__ftol`-decrement slot index — the
    // same shape Lua's `LootSlot(slot)` produces internally after
    // `dec eax`:
    //   - When coin is present (`VAR_LOOT_LOOTABLE != 0`), input `0`
    //     is the coin slot itself and returns NULL; subsequent
    //     internal items live at `userFacingSlot = internal_slot + 1`
    //     (the function dec's again inside).
    //   - When no coin, `userFacingSlot = internal_slot` directly.
    // The internal `_GetRecord` call inside uses `callback = NULL`, so
    // the link is a pure cache hit — no network request gets queued
    // when the cache is cold, just a NULL return.
    FUN_LOOT_SLOT_LINK_BUILDER = 0x004C2670,

    // `__stdcall(uint8_t slotByte)` — sends `CMSG_LOOT_ITEM` (opcode
    // `0x108`) carrying the wire-protocol slot byte for the currently-
    // open loot window. Built directly from the engine's packet
    // primitives — bypasses the `Script_LootSlot` Lua wrapper at
    // `FUN_004C2790`, which routes through a `param_2 != 0`
    // BoP-confirm-after-dialog branch that's of no use to us. The
    // server cheerfully takes whatever slot you send for the current
    // loot session; no client-side BoP confirmation gate.
    //
    // **Calling convention matters.** The callee reads its arg from
    // `[ebp+8]` (stack) and ends with `ret 4`. Mis-typedef'ing as
    // `__fastcall` produces a stack-corrupting caller — symptoms are
    // delayed crashes on unrelated vtable reads.
    FUN_CMSG_LOOT_ITEM = 0x005E1AD0,

    // `__fastcall(player)` — sends `CMSG_LOOT_MONEY` (the coin/money
    // request) for the player's currently-open loot window. Vanilla has
    // no `LootMoney` Lua global; coin is virtual loot slot 0 and the Lua
    // `LootSlot` handler `FUN_004C2790` routes the coin slot here (its
    // `param_1 == 0 && VAR_LOOT_LOOTABLE != 0` branch calls this with the
    // resolved local player). Guards internally on the player's active
    // loot GUID at `[player + 0x1D28]` — which `FUN_CMSG_LOOT_UNIT` writes
    // when it sends the loot request — so it silently no-ops if no window
    // is open. Pass the SAME player pointer used for `FUN_CMSG_LOOT_UNIT`
    // so the `+0x1D28` guard it just wrote is the one read here.
    // `Loot::Scan`'s `LootAllCorpses` path uses it to grab coin alongside
    // the per-slot `CMSG_LOOT_ITEM` sends.
    FUN_CMSG_LOOT_MONEY = 0x005EA8C0,

    // UNIT_DYNAMIC_FLAGS within `m_objectFields`. Standard
    // server-driven dynamic-state bitmask. Empirically verified by
    // cross-referencing three `Script_*` accessors that all read
    // bytes at `[m_objectFields + 0x224]`:
    //   - `Script_UnitIsTapped` (`0x00519C90`): `test [eax+0x224], 0x04`
    //   - `Script_UnitIsTappedByPlayer` (`0x00519D00`): `test [eax+0x224], 0x08`
    //   - `Script_UnitIsDead` (`0x00517AC0`): `[eax+0x224] >> 5 & 1`
    // Bit values match CMaNGOS conventions: 0x01 LOOTABLE,
    // 0x02 TRACK_UNIT, 0x04 TAPPED, 0x08 TAPPED_BY_PLAYER,
    // 0x10 SPECIALINFO, 0x20 DEAD, 0x40 REFER_A_FRIEND_LINKED,
    // 0x80 ZONE_OUT. The +0x224 offset is significantly later than
    // CMaNGOS-doc'd vanilla (which puts DYNAMIC_FLAGS at field 0x46
    // = +0x118) — yet another non-uniform shift in 1.12.1's actual
    // UNIT_FIELD layout. Direct empirical read is the authority.
    OFF_UNIT_FIELD_DYNAMIC_FLAGS = 0x224,
    UNIT_DYNFLAG_LOOTABLE = 0x01,
    UNIT_DYNFLAG_DEAD = 0x20,

    // UNIT_FIELD_BOUNDINGRADIUS within m_objectFields — single
    // float, the unit's interact-radius in world yards. Both
    // `FUN_005EC110` (interact-state check) and `FUN_005DF130`
    // (CMSG_LOOT sender) compute the interaction-range threshold
    // as `target_radius + player_radius + INTERACT_REACH`, clamped
    // to a floor of `MIN_INTERACT_RANGE`. Verified at `0x005EC1A4`
    // where the function reads
    // `[(target_m_objectFields) + 0x1F0]`.
    OFF_UNIT_FIELD_BOUNDING_RADIUS = 0x1F0,

    // `__thiscall(player, target, useDistanceCheck)` — engine's
    // CMSG_LOOT sender for **dead mobs** (CGCreature_C with
    // DYNFLAG_LOOTABLE set). Pre-checks: `FUN_005EC110` interact
    // validity + `FUN_006003A0` lootability (target dead + bit 0 of
    // `m_objectFields[+0x224]` set) + local player not in a state
    // that forbids interaction. Releases any open loot window before
    // sending the new request.
    //
    // `useDistanceCheck` is the auto-walk gate:
    //   - `1` (engine default): if target out of range, start
    //     walking toward it and *don't* send CMSG_LOOT. If in range,
    //     send normally.
    //   - `0`: skip the range check; send immediately. The server
    //     enforces server-side range, so OOR sends silently fail
    //     rather than walking the player toward distant corpses —
    //     the right pick for AoE-loot-style callers.
    //
    // Engine callsite at `FUN_0060FA20` (right-click world handler)
    // resolves the target via
    // `ClntObjMgrObjectPtr(TYPEMASK_UNIT, ..., guid)` first, same as
    // we do.
    //
    // Distinct from the sibling sender at `0x005DF130`, which gates
    // on `FUN_CGCORPSE_IS_LOOTABLE` (player corpses only) instead of
    // the mob path. The CGCorpse helper is documented immediately
    // below.
    FUN_CMSG_LOOT_UNIT = 0x005DF2A0,

    // `__fastcall(int loot_response_struct, undefined4 coin, int unk)`
    // — master loot-window state controller. Called by the
    // `SMSG_LOOT_RESPONSE` handler tail (via `FUN_0048F3A0`) and by
    // `CloseLoot`-inner (via `FUN_0048F200(0, 0, 0)`). Two phases:
    //   - **Close prior**: if `VAR_LOOT_GUID_LO/HI != 0`, clears
    //     them, releases item-cache callbacks for the prior slots,
    //     fires `LOOT_CLOSED` (event id `0x10D`).
    //   - **Open new**: if `param_1 != 0`, copies the GUID from
    //     `[param_1 + 8]` into `VAR_LOOT_GUID_LO/HI`, zeroes
    //     `VAR_LOOT_SLOTS`, walks 16 slots populating itemID via
    //     `FUN_005EBC90(slot)` and sibling helpers, sets
    //     `VAR_LOOT_LOOTABLE = param_2` (coin), then fires
    //     `LOOT_OPENED` (event id `0x10B`).
    // Both event fires go through `FUN_FIRE_EVENT_NO_ARGS` —
    // intercepting that function lets a caller swallow the events
    // without touching this one.
    FUN_LOOT_CONTROLLER = 0x004C1CB0,

    // `__fastcall(int eventID)` — engine's no-args event dispatcher,
    // the lighter-weight sibling of `FUN_FIRE_EVENT` (variadic).
    // Called from ~100 sites across the engine, including the
    // `LOOT_OPENED` / `LOOT_CLOSED` fires inside
    // `FUN_LOOT_CONTROLLER`. Hooking it lets us conditionally swallow
    // specific event ids while leaving the rest untouched — the
    // mechanism we use to do silent scan loops without flickering
    // the `LootFrame`. Fan-in is wide but call frequency is
    // event-driven (state changes only), not per-frame, so per-call
    // overhead from a hooked predicate is negligible.
    FUN_FIRE_EVENT_NO_ARGS = 0x00703E50,

    // `__fastcall(int sendRelease, char earlyReturnOnGameObject,
    // char showError)` — engine's "close the current loot window"
    // inner. Behavior keyed off args:
    //   - `sendRelease` (ECX): if non-zero, builds and sends
    //     `CMSG_LOOT_RELEASE` (opcode `0x15F`) for the current loot
    //     target before clearing client state.
    //   - `earlyReturnOnGameObject` (EDX): if non-zero, bails early
    //     when the loot target is a game object that's been
    //     interacted with in a particular way (fishing-style?).
    //   - `showError` (stack): if non-zero, calls `FUN_00496720(0x87)`
    //     to display a "cannot close loot" error.
    // Side effects: clears `[player + 0x1D28/+0x1D2C]` (the player's
    // prior-interact GUID slots), then calls `FUN_004C1CB0(0, 0, 0)`
    // which clears `VAR_LOOT_GUID_LO/HI`, zeroes `VAR_LOOT_SLOTS`,
    // and fires the `LOOT_CLOSED` event (`0x10D`). Use
    // `(1, 0, 0)` for a clean release + clear.
    FUN_CLOSE_LOOT_INNER = 0x0048F200,

    // `CGCorpse::IsLootable(this)` — `__fastcall(corpse) → uint`,
    // tests bit 0 of `[corpse_m_objectFields + 0x78]`. Specific to
    // `TYPEMASK_CORPSE` (player corpses) — `+0x78` of a CGCorpse_C's
    // m_objectFields holds dynamic flags with bit 0 = LOOTABLE. The
    // same offset in CGUnit_C / CGPlayer_C's m_objectFields holds
    // BYTES_0 (race / class / gender / power type), so this helper
    // returns nonsense for live units (e.g. it returns 1 for Human
    // players because race=1 has bit 0 = 1). Used by the engine's
    // right-click-on-corpse handler at `FUN_005D6BF0` to gate
    // CMSG_LOOT for player corpses, and from `FUN_005DF130` (one of
    // two CMSG_LOOT senders) for the same corpse path. Don't reuse
    // for live-unit lootability — use `OFF_UNIT_FIELD_DYNAMIC_FLAGS`
    // bit 0 against a CGUnit instead.
    FUN_CGCORPSE_IS_LOOTABLE = 0x005D6E20,

    // Merchant inventory: flat array at `0xBDD118`, stride 0x1C,
    // itemID at `entry+4`. The "flag" globals are actually the
    // merchant NPC's 64-bit GUID lo/hi — both zero when no merchant
    // is open, which is the engine's gate (`mov a, [LO]; or a, [HI];
    // jz no_merchant`). Same globals also drive whether the buyback
    // slots are populated.
    VAR_MERCHANT_ITEMS = 0x00BDD118,
    VAR_MERCHANT_NPC_GUID_LO = 0x00BDDFA0,
    VAR_MERCHANT_NPC_GUID_HI = 0x00BDDFA4,
    VAR_MERCHANT_COUNT = 0x00BDDFA8,
    MERCHANT_STRIDE = 0x1C,
    OFF_MERCHANT_ITEM_ID = 0x04,
    // Additional 28-byte-entry offsets decoded from
    // `Script_GetMerchantItemInfo` at `0x004FB150`:
    //   +0x0C  uint32  numAvailable (-1 = unlimited stock)
    //   +0x10  uint32  price in copper
    //   +0x18  uint32  stack quantity per purchase
    OFF_MERCHANT_NUM_AVAILABLE = 0x0C,
    OFF_MERCHANT_PRICE = 0x10,
    OFF_MERCHANT_STACK_COUNT = 0x18,

    // Buyback storage. `VAR_BUYBACK_SLOTS` is a 4-byte-stride array
    // of inventory-slot indices into the player's invMgr GUID array
    // (the engine keeps sold items alive in a per-character buyback
    // pool indexed through normal inventory storage). 12 fixed slots
    // in vanilla; `VAR_BUYBACK_COUNT` is the live count of populated
    // entries. Same merchant-open gate as `VAR_MERCHANT_NPC_GUID_*`.
    VAR_BUYBACK_SLOTS = 0x00BDDF24,
    VAR_BUYBACK_COUNT = 0x00BDDFAC,

    // CMSG_SELL_ITEM dispatcher. Engine's
    // `Script_UseContainerItem` merchant branch reaches it with this
    // shape; calling it directly bypasses the use-vs-sell dispatch
    // and any addon hooks on `UseContainerItem`.
    //
    //   __fastcall(count[ECX],
    //              merchantGuidLo, merchantGuidHi,
    //              itemGuidLo, itemGuidHi)
    //
    // `count = 0` sells the whole stack — same as what
    // `Script_UseContainerItem` passes. Callee cleans (`ret 0x10`).
    FUN_MERCHANT_SELL_ITEM = 0x005E1D50,

    // Quest item resolver — used by `GetQuestItem`. Walks the active
    // quest-offer "reward"/"choice" arrays. Returns itemID directly
    // (no follow-up cache lookup needed by the caller).
    // `__fastcall(ECX = const char *type, EDX = int index0) → itemID`.
    // Returns 0 on bad type, OOR index, or empty slot.
    FUN_QUEST_ITEM_RESOLVER = 0x00501920,

    // Quest log record fields (within the data block returned by
    // `Quest::Cache::Lookup`). Reward and choice arrays each hold up
    // to N inline itemIDs (4 bytes per entry).
    OFF_QUEST_RECORD_REWARD_ITEMS = 0x3C,    // 4 entries (0..3)
    OFF_QUEST_RECORD_CHOICE_ITEMS = 0x5C,    // 6 entries (0..5)
    QUEST_RECORD_REWARD_COUNT = 4,
    QUEST_RECORD_CHOICE_COUNT = 6,

    // Trade window — local "player" side and remote "target" side.
    // Player side is a flat int32 array (just itemIDs). Target side
    // is a flat `{?, itemID}` array (8-byte stride). Both cap at 7
    // slots (vanilla's 6 trade slots + 1 non-tradeable slot).
    VAR_TRADE_PLAYER_SLOTS = 0x00B714F4,
    VAR_TRADE_TARGET_SLOTS = 0x00B716D0,
    TRADE_TARGET_STRIDE = 0x08,
    OFF_TRADE_TARGET_ITEM_ID = 0x04,
    TRADE_MAX_SLOTS = 7,

    // Auction house — three independent arrays of entry-pointers
    // covering the three views (list / owner / bidder). Each entry
    // carries the itemID at `+0x08`.
    VAR_AUCTION_LIST_ENTRIES = 0x00B72278,
    VAR_AUCTION_LIST_COUNT = 0x00B7261C,
    VAR_AUCTION_OWNER_ENTRIES = 0x00B72340,
    VAR_AUCTION_OWNER_COUNT = 0x00B72624,
    VAR_AUCTION_BIDDER_ENTRIES = 0x00B72470,
    VAR_AUCTION_BIDDER_COUNT = 0x00B7262C,
    OFF_AUCTION_ENTRY_ITEM_ID = 0x08,

    // Auction sell slot — holds a single CGItem GUID for the item
    // currently sitting in the "sell" frame. Engine resolves it via
    // a typed object lookup; pass `OBJECT_TYPE_ITEM` and the GUID.
    // `Script_StartAuction` posts whatever GUID is here, so writing an
    // arbitrary bag item's GUID + calling it posts that item — no cursor,
    // no ClickAuctionSellItemButton. Used by `AuctionHouse::PostItem`.
    VAR_AUCTION_SELL_GUID_LO = 0x00B72608,
    VAR_AUCTION_SELL_GUID_HI = 0x00B7260C,
    // Auctioneer NPC GUID — set while an auction house window is open;
    // `Script_StartAuction` writes it as the packet target and bails if
    // it's zero (so this doubles as an "AH is open" probe).
    VAR_AUCTION_AUCTIONEER_GUID_LO = 0x00B725F8,
    VAR_AUCTION_AUCTIONEER_GUID_HI = 0x00B725FC,
    // `Script_StartAuction` (`0x004CE770`) — `int __fastcall(void *L)`;
    // reads (minBid, buyout, runTime) from L[1..3], validates the sell
    // slot + auctioneer + runTime, and sends CMSG_AUCTION_SELL_ITEM
    // (opcode 0x256 = { auctioneerGuid, sellItemGuid, bid, buyout,
    // runTime }). Reused verbatim by `AuctionHouse::PostItem` per stack.
    FUN_SCRIPT_START_AUCTION = 0x004CE770,
    // Valid `runTime` values `Script_StartAuction` accepts, in minutes:
    // {120, 480, 1440} = 2h / 8h / 24h (vanilla's three durations). The
    // modern `duration` arg (1/2/3) indexes this table.
    VAR_AUCTION_DURATION_TABLE = 0x00807028,
    AUCTION_DURATION_TABLE_COUNT = 3,
    // `__fastcall(ECX = typeCode, EDX = unused, [stack] = guidLo,
    //             [stack] = guidHi, [stack] = anyInt) → CGObject *`.
    // Returns NULL when no object matches or the type doesn't match.
    // The third stack arg (originally a `__FILE__/__LINE__` debug
    // hint) is read by debug paths only — anything works at runtime.
    FUN_GET_OBJECT_BY_GUID = 0x00468460,
    OBJECT_TYPE_ITEM = 2,

    // Inbox — array of mail-entry pointers behind a slot indirection.
    // The address itself holds a heap pointer (`MOV ECX, [imm32]` in
    // the engine), so reading the array requires one extra deref
    // beyond plain `(uint8_t **)`. itemID inline on each entry at
    // `+0x120`. (The engine's `Script_GetInboxItem` passes a per-
    // entry completion callback at `0x004AF0A0` to GetRecord; for
    // ID-only retrieval we skip the callback.)
    //
    // Entries also carry per-instance modifiers at `+0x12C`
    // (enchantID), `+0x134` (suffix factor), `+0x138` (random
    // property ID), etc. — `Script_GameTooltip_SetInboxItem` at
    // `0x005354C0` copies them onto the tooltip object to render a
    // fully-decorated tooltip. We don't expose them in
    // `GetInboxItemLink` because 3.3.5's same function only emits
    // basic itemID-only links (`FUN_0061E290(itemID)`); per-instance
    // data only fully manifests when the player takes the item and
    // the engine spawns a real CGItem.
    VAR_INBOX_ENTRIES = 0x00B6EF54,
    VAR_INBOX_COUNT = 0x00B6EFC0,
    OFF_INBOX_ENTRY_ITEM_ID = 0x120,

    // SendMail attached item — the engine stores the attached item's
    // 64-bit GUID at these globals when the player drops an item onto
    // the SendMail slot (`SetSendMailItem` / `ClickSendMailItemButton`).
    // Cleared to zero by `ClearSendMail` and on `MAIL_CLOSED`. Vanilla
    // allows exactly one attachment per outgoing mail (modern WoW lifted
    // this to 12+; we ignore the modern `attachmentIndex` arg since only
    // index 1 is meaningful).
    //
    // The attached item still exists as a live CGItem in the player's
    // inventory until the mail is actually sent, so the GUID resolves
    // via `FUN_GET_OBJECT_BY_GUID(OBJECT_TYPE_ITEM, ...)`. That lets
    // `GetSendMailItemLink` return the full per-instance link (enchant
    // / random suffix preserved) by feeding the resolved CGItem to
    // `Item::Link::FromCGItem`.
    VAR_SEND_MAIL_ITEM_GUID_LO = 0x00B6EF90,
    VAR_SEND_MAIL_ITEM_GUID_HI = 0x00B6EF94,

    // Craft window (engineering, alchemy, etc. before the "trade
    // skill" overhaul). Same slot-indirection shape as inbox: the
    // address holds a pointer to the heap-allocated entry-pointer
    // array, NOT the array itself. Each entry's `+0x00` is a
    // spellID (the recipe spell); the recipe's reagent list lives
    // on the spell record at `+0xA8`.
    VAR_CRAFT_ENTRIES = 0x00BDCF78,
    VAR_CRAFT_COUNT = 0x00BDCFC0,
    OFF_CRAFT_ENTRY_SPELL_ID = 0x00,

    // Trade skill window — different storage from craft, same shape
    // (slot-indirected entry-ptr array with a spellID inside). The
    // recipe's created item lives on the spell record at `+0x19C`
    // (the "produced item" field), and reagents at `+0xA8` (shared
    // with craft).
    VAR_TRADESKILL_ENTRIES = 0x00BDDFC0,
    VAR_TRADESKILL_COUNT = 0x00BDE04C,
    // SkillLine.dbc row ID of the currently-open trade skill window
    // (the profession the player has open). Bounds-checked against
    // `VAR_SKILL_LINE_COUNT` and used as `records[id]` by
    // `Script_GetTradeSkillLine` (`0x004FDD40`). <= 0 / stale when no
    // window is open. `FUN_004FC870` zeroes it on close (and fires
    // event `0x13B`), so `> 0` reliably means "trade window open".
    VAR_CURRENT_TRADESKILL_LINE = 0x00BDE040,
    // Craft window (Enchanting, pet training) open-state flag — nonzero
    // (3 for the enchanting-style craft) while open; `FUN_004F5FE0`
    // zeroes it on close (fires event `0x169`). The Craft window has no
    // skill-line-ID global — `TradeSkill::Link` derives the skill line
    // from the first craft recipe — so this is the "craft open" gate.
    VAR_CRAFT_WINDOW_STATE = 0x00BDCFB8,
    // Spell.dbc "produced item" field for crafting recipes — the
    // itemID this spell creates when cast. Reagent itemIDs and
    // counts live at `OFF_SPELL_REAGENT_ID` / `OFF_SPELL_REAGENT_COUNT`
    // above (shared with all spells that consume reagents, not just
    // recipes).
    OFF_SPELL_RECORD_CREATED_ITEM = 0x19C,
    SPELL_RECIPE_MAX_REAGENTS = 8,

    // Spell.dbc effect arrays — three effects per spell. Derived
    // from `Script_CastShapeshiftForm` (`0x004B4810`): the form-toggle
    // check reads `spellRec[+0x16C + i*4]` for the per-effect aura
    // type, and `spellRec[+0x1A8 + i*4]` for that effect's misc
    // value. Cross-checked against 3.3.5's `FUN_00726CE0`
    // (`Script_CancelShapeshiftForm`'s inner), which does the same
    // pair of reads against `0x24` (= `SPELL_AURA_MOD_SHAPESHIFT`)
    // and a form-id compare.
    //
    // `EffectApplyAuraName[i]` is the SPELL_AURA_* code (0 = none /
    // not an aura effect); `EffectMiscValue[i]` is effect-specific
    // (form ID for shapeshift, channel index for periodic, etc.).
    OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME = 0x16C,  // int32[3]
    OFF_SPELL_RECORD_EFFECT_MISC_VALUE = 0x1A8,       // int32[3]
    // Per-effect SpellMechanic ids (EffectMechanic[3]). One int32[3] (0xC)
    // after EffectBasePoints (0x130); cross-checked against EffectRadiusIndex
    // (0x160) and EffectApplyAuraName (0x16C). Vanilla often stores a spell's
    // mechanic on an effect rather than the spell-level Mechanic (0x14), so
    // bleed / DoT detection needs this array in addition to that field.
    OFF_SPELL_RECORD_EFFECT_MECHANIC = 0x13C,         // int32[3]
    SPELL_RECORD_EFFECT_COUNT = 3,

    // SPELL_EFFECT_* id array (`Effect[3]`) — five arrays before
    // EffectBasePoints (`+0x130`), so `0x130 - 5*0xC`. Used to tell craft
    // recipes from profession abilities (Disenchant etc.) that also carry
    // skill-up thresholds. Recipe-producing effects: CREATE_ITEM (items),
    // ENCHANT_ITEM / _TEMPORARY (enchants), and LEARN_SPELL (Turtle's custom
    // recipes, e.g. "Steel Plate Boots" = effect 36).
    OFF_SPELL_RECORD_EFFECT = 0xF4,                   // int32[3]
    SPELL_EFFECT_SCHOOL_DAMAGE = 2,                    // direct damage (verified vs server SpellDefines.h)
    SPELL_EFFECT_CREATE_ITEM = 24,
    SPELL_EFFECT_LEARN_SPELL = 36,
    SPELL_EFFECT_ENCHANT_ITEM = 53,
    SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY = 54,

    // EffectBasePoints[3] — the base magnitude of each effect, stored as
    // (value - 1) for the fixed-die spells that back item stats (the
    // engine adds a 1..dieSides roll; dieSides == 1 makes it value = base
    // + 1). Verified empirically: the "Increased Intellect 01/02/03"
    // random-property spells (7468/7469/7470) hold base 0/1/2 for the
    // +1/+2/+3 tiers. Read by Item::Stats when resolving random-suffix
    // stat auras. Parallel [3] array to APPLY_AURA_NAME / MISC_VALUE.
    OFF_SPELL_RECORD_EFFECT_BASE_POINTS = 0x130,      // int32[3]

    // EffectBaseDice[3] — added to EffectBasePoints for the effect's fixed
    // magnitude (mangos `CalculateSimpleValue = BasePoints + BaseDice`).
    // For every item-stat aura BaseDice == 1, so value = BasePoints + 1;
    // reading it directly matches the engine exactly. Verified at field 67
    // (Effect@61, DieSides@64, BaseDice@67, BasePoints@76) against 7468.
    OFF_SPELL_RECORD_EFFECT_BASE_DICE = 0x10C,        // int32[3]

    // Per-effect EffectRadiusIndex[3] → SpellRadius.dbc. Verified by
    // FUN_006e6350, which reads spellRec[+0x160] (effect 0) and
    // spellRec[+0x164] (effect 1) as radius indices. 0 = effect has no
    // radius. Read by GetSpellRadius.
    OFF_SPELL_RECORD_EFFECT_RADIUS_INDEX = 0x160,     // int32[3]

    // SpellFamily classification, used by the SpellMod system (see
    // VAR_SPELLMOD_*). Verified from FUN_006e6b30: SpellFamilyName at
    // +0x280 (gates whether the player's class mods apply), 64-bit
    // SpellFamilyFlags at +0x284 (selects which familyFlagBit rows to
    // sum). AttributesEx3 (+0x24) bit 0x20000000 (IGNORE_CASTER_MODIFIERS)
    // disables spell mods — the client reads [record + 0x24] in FUN_006e6b30.
    OFF_SPELL_RECORD_FAMILY_NAME = 0x280,             // u32
    OFF_SPELL_RECORD_FAMILY_FLAGS = 0x284,            // u64
    OFF_SPELL_RECORD_ICON_ID = 0x1D4,                 // u32 SpellIconID (→ SpellIcon.dbc)
    OFF_SPELL_RECORD_ATTRIBUTES = 0x18,               // u32 (column 6, base Attributes)
    SPELL_ATTR_PASSIVE = 0x40,                        // bit 6 — always-on aura
    OFF_SPELL_RECORD_ATTRIBUTES_EX = 0x1C,            // u32 (column 7)
    OFF_SPELL_RECORD_ATTRIBUTES_EX2 = 0x20,           // u32 (column 8)
    OFF_SPELL_RECORD_ATTRIBUTES_EX3 = 0x24,           // u32 (column 9)
    // The client's spell-mod core (FUN_006e6b30) skips all mods when this bit
    // is set: it reads [record + 0x24] & 0x20000000 — AttributesEx3 bit 29,
    // SPELL_ATTR_EX3_IGNORE_CASTER_MODIFIERS (verified via disassembly; the
    // server enum agrees). NOT AttributesEx2's bit 29 (that's CANT_CRIT).
    SPELL_ATTR_EX3_IGNORE_CASTER_MODIFIERS = 0x20000000,
    // AttributesEx channel bits (CHANNELED_1 0x4 | CHANNELED_2 0x40) and the
    // AttributesEx2 autorepeat flag (bit 5, Auto Shot / Shoot) — each shared
    // by more than one module, so kept here rather than redefined locally.
    SPELL_ATTR_EX_CHANNELED = 0x4 | 0x40,
    SPELL_ATTR_EX2_AUTOREPEAT_FLAG = 0x20,
    OFF_SPELL_RECORD_INTERRUPT_FLAGS = 0x54,          // u32 (column 21)
    // Bit 0 of InterruptFlags: the cast is interrupted when the caster moves.
    // Both the server (HandleMovementOpcodes → InterruptSpellsWithInterruptFlags,
    // tortoise-wow) and the client's own CheckCast (FUN_006094f0 gates its MOVING
    // failure on this bit) use it; thrown items (grenades — e.g. spell 4068)
    // lack it, so movement must not end their cast. See Spell::Cast (issue #23).
    SPELL_INTERRUPT_FLAG_MOVEMENT = 0x1,

    // Remaining Spell.dbc record fields — the single source of truth for the
    // record layout (byte = column * 4). Modules must use these rather than
    // redefining local copies.
    OFF_SPELL_RECORD_CASTING_TIME_INDEX = 0x48,       // u32 → SpellCastTimes.dbc
    OFF_SPELL_RECORD_MAX_LEVEL = 0x6C,                // u32 (scaling cap level)
    OFF_SPELL_RECORD_SPELL_LEVEL = 0x74,              // u32 (this rank's effective level)
    OFF_SPELL_RECORD_POWER_TYPE = 0x7C,               // i32 (0=mana,1=rage,2=focus,3=energy,4=happiness)
    OFF_SPELL_RECORD_MANA_COST = 0x80,                // u32 base cost
    OFF_SPELL_RECORD_RANGE_INDEX = 0x90,              // u32 → SpellRange.dbc
    OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A = 0x148,// i32[3] implicit target A
    OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_B = 0x154,// i32[3] implicit target B
    OFF_SPELL_RECORD_MANA_COST_PERCENT = 0x270,       // i32 % of base resource (0 = flat)
    OFF_SPELL_RECORD_RANK = 0x204,                    // char*[9] localized rank text
    OFF_SPELL_RECORD_TOTEM = 0xA0,                    // i32[2] required totem/tool item IDs

    // SpellIcon.dbc record's path string field (char* at +0x04). Distinct
    // struct from Spell.dbc — reached via SpellIcon.dbc[Spell.SpellIconID].
    OFF_SPELLICON_PATH = 0x04,

    // Spell.dbc Mechanic field — the spell-level SpellMechanic ID (→
    // SpellMechanic.dbc), 0 = none. Field 5 of the record
    // (ID, School, Category, castUI, Dispel, Mechanic), which lands at
    // +0x14 given Attributes (field 6) is verified at +0x18. Cross-
    // confirmed by FUN_006e9ca0 (the mechanic-immunity check), which
    // reads `spellRec[+0x14]` as the spell's mechanic when the per-
    // effect EffectMiscValue is 0. Read by C_Spell.GetSpellMechanicByID.
    OFF_SPELL_RECORD_MECHANIC = 0x14,
    SPELL_AURA_MOD_STEALTH = 16,
    SPELL_AURA_MOD_SHAPESHIFT = 36,
    SPELL_AURA_MOUNTED = 78,

    // Control-loss aura effect types (EffectApplyAuraName values), used by
    // C_LossOfControl to classify a debuff. Values are authoritative from the
    // Turtle server's indexed SPELL_AURA proc-handler table (SharedDefines).
    SPELL_AURA_MOD_POSSESS = 2,
    SPELL_AURA_MOD_CONFUSE = 5,
    SPELL_AURA_MOD_CHARM = 6,
    SPELL_AURA_MOD_FEAR = 7,
    SPELL_AURA_MOD_STUN = 12,
    // Movement slow / snare. Not a loss-of-control (C_LossOfControl omits it),
    // but the C_UnitAuras CROWD_CONTROL filter counts it — see
    // Spell::CrowdControl::IsCrowdControl.
    SPELL_AURA_MOD_DECREASE_SPEED = 33,
    SPELL_AURA_MOD_PACIFY = 25,
    SPELL_AURA_MOD_ROOT = 26,
    SPELL_AURA_MOD_SILENCE = 27,
    SPELL_AURA_MOD_PACIFY_SILENCE = 60,
    SPELL_AURA_MOD_DISARM = 67,

    // SMSG_SPELL_COOLDOWN (0x134) handler, registered by the spell-opcode
    // boot registrar FUN_006e7150 exactly like the failure/channel handlers
    // Spell::Cast co-hooks — same `int __stdcall(uint32_t *opCode,
    // CDataStore *packet)` shape. Body: GUID(u64) then (spellID, durationMs)
    // u32 pairs. A player-GUID packet carrying >=2 spells that all share one
    // duration is a school-interrupt lockout (Player::ProhibitSpellSchool is
    // the only sender of that shape); see src/lossofcontrol/Info.cpp.
    FUN_SPELL_COOLDOWN_HANDLER = 0x006E9460,

    // Gossip menu data. Populated by the SMSG_GOSSIP_MESSAGE packet
    // handler at 0x004E26E0 and reset by FUN_004E26A0 each time a new
    // gossip window opens. Two parallel storage arrays:
    //   Options: 16 slots, stride 0x80C, sentinel = signed `index` == -1.
    //   Quests:  32 slots, stride 0x20C, sentinel = u32 `questID` == 0.
    //            Available vs. active is partitioned by the +0x008
    //            `status` field — values 3 or 4 mark an active-quest
    //            row, anything else is a deliverable.
    VAR_GOSSIP_OPTIONS = 0x00BBBE90,
    GOSSIP_OPTIONS_STRIDE = 0x80C,
    GOSSIP_OPTIONS_MAX = 16,
    OFF_GOSSIP_OPTION_TEXT = 0x000,          // char[0x800] inline
    OFF_GOSSIP_OPTION_INDEX = 0x800,         // int32; -1 = unused slot
    OFF_GOSSIP_OPTION_BOX_CODED = 0x804,     // u8 (1 = needs password)
    OFF_GOSSIP_OPTION_ICON = 0x808,          // u8 icon type (0..10)

    VAR_GOSSIP_QUESTS = 0x00BB74C0,
    GOSSIP_QUESTS_STRIDE = 0x20C,
    GOSSIP_QUESTS_MAX = 32,
    OFF_GOSSIP_QUEST_ID = 0x000,             // u32 questID; 0 = unused slot
    OFF_GOSSIP_QUEST_LEVEL = 0x004,          // i32 questLevel
    OFF_GOSSIP_QUEST_STATUS = 0x008,         // u32 status; 3|4 = active
    OFF_GOSSIP_QUEST_TITLE = 0x00C,          // char[0x200] inline

    // Inline greeting buffer pushed by Script_GetGossipText. Populated
    // by the engine after the NPC_TEXT.dbc query for the gossip giver
    // resolves the textID embedded in SMSG_GOSSIP_MESSAGE.
    VAR_GOSSIP_GREETING_TEXT = 0x00BBB678,

    // Engine Lua C functions for the selector half of the gossip
    // surface. The `FUN_GOSSIP_SELECT_*` helpers are the engine's
    // internal selectors that the `Script_SelectGossip*` Lua wrappers
    // call after parsing their args. We call them directly with the
    // 0-based slot/index to skip the Lua-stack-stomp round-trip.
    //
    //   FUN_GOSSIP_SELECT_OPTION — __fastcall(uint slot0Based,
    //                                          const char *password)
    //     Sends CMSG_GOSSIP_SELECT_OPTION with `OPTIONS[slot].index`.
    //     Handles the boxCoded/password-required gate internally —
    //     pass nullptr if no password.
    //   FUN_GOSSIP_SELECT_{AVAILABLE,ACTIVE}_QUEST — __fastcall(int idx)
    //     Walks `GOSSIP_QUESTS`, skipping rows of the wrong status,
    //     matches by 0-based position into the filtered list, sends
    //     CMSG with the matching questID.
    FUN_GOSSIP_SELECT_OPTION = 0x004E2320,
    FUN_GOSSIP_SELECT_AVAILABLE_QUEST = 0x004E24B0,
    FUN_GOSSIP_SELECT_ACTIVE_QUEST = 0x004E2600,
    FUN_SCRIPT_CLOSE_GOSSIP = 0x004E2B20,

    // Current gossip NPC GUID (u64). Set by the SMSG_GOSSIP_MESSAGE
    // handler, cleared by the gossip-close worker at 0x004E1FA0 (which
    // then fires GOSSIP_CLOSED, event 0x12B). Non-zero = a gossip
    // session is live — the gate that keeps `C_GossipInfo` from serving
    // the previous NPC's stale arrays/text after close.
    VAR_GOSSIP_NPC_GUID = 0x00BC3F58,

    // --- Quest-greeting state (SMSG_QUESTGIVER_QUEST_LIST) -------------
    // Vanilla's SECOND questgiver path: NPCs without a gossip menu send
    // SMSG_QUESTGIVER_QUEST_LIST, whose handler (FUN_005dbb10) fills a
    // storage entirely separate from the gossip arrays and fires
    // QUEST_GREETING (event 300 = 0x12C) — the QuestFrameGreetingPanel
    // ("Current Quests" / "Available Quests"). Vanilla Lua reads it via
    // GetGreetingText / Get{Available,Active}Title / etc.; C_GossipInfo
    // merges it in so modern-addon ports work on greeting NPCs too.
    //
    // Shared quest-session state, written by the panel-begin helper
    // FUN_00500bd0(panelType, textID, guidLo, guidHi, ...):
    //   VAR_QUESTGIVER_GUID  — u64, the current questgiver. Cleared by
    //     the session-close worker FUN_00501130 (fires QUEST_FINISHED,
    //     event 0x130). Non-zero = quest session live.
    //   VAR_QUEST_PANEL_TYPE — i32 panel: 0 greeting, 1 detail,
    //     2 progress, 3 reward. begin(0) also zeroes both greeting
    //     arrays + counts + the text buffers.
    VAR_QUESTGIVER_GUID = 0x00BE0810,
    VAR_QUEST_PANEL_TYPE = 0x00BE0818,

    // GUID the greeting arrays were filled for — written ONLY by the
    // SMSG_QUESTGIVER_QUEST_LIST handler. The greeting arrays are valid
    // iff this equals VAR_QUESTGIVER_GUID (and that is non-zero);
    // otherwise they're leftovers from an older NPC (e.g. the current
    // quest session came from a gossip-menu quest click, which begins
    // at panel 1 without ever clearing the greeting arrays).
    VAR_GREETING_NPC_GUID = 0x00C4D740,

    // Greeting text (0x200 inline, $N/$C-expanded) — what vanilla's
    // Script_GetGreetingText pushes.
    VAR_QUEST_GREETING_TEXT = 0x00BDE068,

    // The two greeting quest arrays: 32 entries, stride 0x4C =
    // { u32 questID @ +0, i32 level @ +4, char title[0x40] @ +8,
    //   u32 extra @ +0x48 (available: status==0 flag; active: always
    //   0 — the appender FUN_00500e90 never stores the status, see
    //   the scratch arrays below for isComplete) }.
    // Appenders: FUN_00500e40 (available), FUN_00500e90 (active).
    // Readers: Script_Get{Available,Active}{Title,Level} @ 0x00501AC0/
    // 0x00501B30/0x00501BA0/0x00501C20.
    VAR_GREETING_AVAILABLE_ENTRIES = 0x00BDFE60,
    VAR_GREETING_ACTIVE_ENTRIES = 0x00BDE690,
    GREETING_QUESTS_STRIDE = 0x4C,
    GREETING_QUESTS_MAX = 32,
    OFF_GREETING_QUEST_ID = 0x000,
    OFF_GREETING_QUEST_LEVEL = 0x004,
    OFF_GREETING_QUEST_TITLE = 0x008,
    VAR_GREETING_AVAILABLE_COUNT = 0x00BE0834,
    VAR_GREETING_ACTIVE_COUNT = 0x00BE0838,

    // Scratch arrays the SMSG_QUESTGIVER_QUEST_LIST handler fills in
    // packet order (32 × u32 each, zeroed per packet): questIDs and the
    // per-quest dialog status. Status 3|4 = active (same sentinel as
    // the gossip quest rows), 4 = complete/turn-in-ready. The canonical
    // greeting arrays drop the status, so `isComplete` for an active
    // greeting quest is recovered by questID lookup here.
    VAR_GREETING_SCRATCH_IDS = 0x00C4D2B0,
    VAR_GREETING_SCRATCH_STATUS = 0x00C4D340,

    // Engine greeting-quest selectors — the workers vanilla's
    // Script_Select{Available,Active}Quest call: `__fastcall(int
    // idx0Based)`, index straight into the corresponding greeting
    // array. They validate VAR_QUESTGIVER_GUID internally and send
    // CMSG_QUESTGIVER_{QUERY_QUEST,COMPLETE_QUEST}.
    FUN_GREETING_SELECT_AVAILABLE_QUEST = 0x005012A0,
    FUN_GREETING_SELECT_ACTIVE_QUEST = 0x00501320,

    // CDataStore primitive readers — `__thiscall void(this, void *out)`.
    // Read at the current cursor (`+0x14`) and advance it by the value
    // size. We call these directly from `Unit::MirrorTimer` to peek
    // packet contents before the SMSG handler consumes them, then
    // restore the cursor so the original handler reads the same bytes.
    FUN_DATASTORE_READ_U32 = 0x00418E30,
    FUN_DATASTORE_READ_U8 = 0x00418CB0,
    OFF_DATASTORE_CURSOR = 0x14,

    // Engine millisecond tick (returns `ulonglong` but truncating to
    // `uint32_t` is fine for the time deltas we need — the high
    // 32 bits only matter after 49 days of uptime). Same source the
    // Lua `GetTime()` function reads, just in milliseconds rather
    // than the seconds Lua sees. Returns either `GetTickCount()` or
    // a higher-resolution timer depending on the engine's clock-
    // selection flag at `0x00884C80`.
    FUN_ENGINE_TICK_MS = 0x0042B750,

    // Mirror timer (BREATH / FATIGUE-style) SMSG handler. Dispatches
    // opcodes `0x1D9` (START), `0x1DA` (PAUSE), `0x1DB` (STOP) — but
    // the vanilla engine doesn't cache any timer state, just reads
    // the packet and immediately fires the corresponding
    // MIRROR_TIMER_* Lua event. Our hook (`Unit::MirrorTimer`) peeks
    // the packet via cursor save/restore on the CDataStore and
    // builds a side cache that `GetMirrorTimerInfo` /
    // `GetMirrorTimerProgress` can read back.
    //
    //   __fastcall void(?, int opcode, ?, void *dataStore)
    //
    // The helper at `FUN_MIRROR_TIMER_TYPE_NAME` returns the
    // engine's static type-name string for IDs 0..2 (`"EXHAUSTION"`,
    // `"BREATH"`, `"FEIGNDEATH"`); `FUN_MIRROR_TIMER_LABEL` builds
    // the localized display label by looking up `{TYPE}_LABEL` in
    // the engine's localized strings, or substituting the spell
    // name when the type is FEIGNDEATH and a spellID is non-zero.
    FUN_SMSG_MIRROR_TIMER_HANDLER = 0x005E7990,
    FUN_MIRROR_TIMER_TYPE_NAME = 0x005E7AE0,
    FUN_MIRROR_TIMER_LABEL = 0x005E7B10,
    SMSG_OPCODE_MIRROR_TIMER_START = 0x1D9,
    SMSG_OPCODE_MIRROR_TIMER_PAUSE = 0x1DA,
    SMSG_OPCODE_MIRROR_TIMER_STOP = 0x1DB,
    MIRROR_TIMER_SLOT_COUNT = 3,

    // `realmList` CVar reader — returns the configured realmlist
    // address (e.g. "logon.turtle-wow.org") as a `const char *`, or
    // NULL when unset. `__fastcall(int forceRefresh)`; pass 0 to read
    // the cached value (callers in the engine pass non-zero only
    // around CMSG_AUTH_SESSION reconnects). Lazily registers the CVar
    // on first call.
    //
    // This is the realmlist *address*, not the display name (the
    // friendly server name visible in the realm-select dialog lives
    // in the `realmName` CVar at `FUN_005AB7D0`, read by
    // `Script_GetServerName`). Credentials are scoped on this value
    // because two different private servers may share the same
    // friendly name but never the same realmlist address.
    FUN_CVAR_REALM_LIST = 0x005AB680,

    // Engine login entry — the C function `Script_DefaultServerLogin`
    // (the glue Lua binding at `0x0046D160`) tail-calls into here with
    // the account/password as raw C strings. We invoke it directly from
    // C++ when dispatching a saved-credential login, bypassing the
    // round-trip through Lua so the plaintext never crosses the
    // Lua boundary on the way out.
    //
    //   __fastcall void(ecx = const char *account, edx = char *password)
    //
    // The function:
    //   1. Gates on three globals — VAR_GLUE_LOGIN_READY1,
    //      VAR_GLUE_LOGIN_READY2 (both must be nonzero) and
    //      VAR_GLUE_LOGIN_INPROGRESS (must be zero). All set up by the
    //      normal glue boot flow; a call made while the AccountLogin
    //      screen is showing is safe, an off-screen call silently no-ops.
    //   2. Copies the account into a 0x40-byte buffer at `0x00B41DB0`
    //      and uppercases it in place.
    //   3. Fires the "connecting" status event.
    //   4. Hands the (uppercased account, password) pair to the SRP
    //      initiator at `FUN_005AB4B0`.
    //   5. **Zeros the caller's password buffer in place** via an
    //      unrolled `REP STOSD` loop counting strlen bytes — so the
    //      decrypted-on-stack buffer we feed in gets scrubbed for free
    //      after the SRP step consumes it.
    //   6. Calls into the AddOn-registry init (`FUN_0051C740`) with the
    //      uppercased account name. Same call documented in CLAUDE.md
    //      under "AddOn registry & hot-reload".
    FUN_GLUE_LOGIN_ATTEMPT = 0x0046AFB0,

    // Glue-state readiness flags read by `FUN_GLUE_LOGIN_ATTEMPT`'s
    // prologue. Names reflect the gate-condition logic, not anything
    // intrinsic to the engine — the engine just checks `R1 && R2 && !IP`.
    VAR_GLUE_LOGIN_READY1 = 0x00B41DFC,
    VAR_GLUE_LOGIN_READY2 = 0x00B41E04,
    VAR_GLUE_LOGIN_INPROGRESS = 0x00B41DA0,
    // The same `VAR_GLUE_LOGIN_INPROGRESS` global doubles as a glue
    // state-code: `FUN_GLUE_ENTER_WORLD` sets it to `8` ("entering world")
    // on a successful commit, which `Unit::Identity`'s enter-world co-hook
    // gates on (the rename / char-locked bail paths return before it).
    GLUE_STATE_ENTERING_WORLD = 8,

    // `FUN_0046B500` — the enter-world worker behind `Script_EnterWorld` (the
    // glue "Enter World" button: `Script_EnterWorld` is just `call this; ret`).
    // Computes the selected character's struct into VAR_GLUE_SELECTED_CHAR and,
    // on success, sets VAR_GLUE_LOGIN_INPROGRESS = 8. `Unit::Identity` co-hooks
    // it to capture the selected character's GUID *before* the engine creates
    // the in-world player object — so `UnitGUID("player")` / `PlayerGuid()`
    // answer from addon-load time instead of returning nil/0 until the
    // SMSG_UPDATE_OBJECT that populates the player object (≈PLAYER_LOGIN).
    FUN_GLUE_ENTER_WORLD = 0x0046B500,

    // Selected-character struct pointer (glue): char-array base +
    // selectedIndex*0x120, written by FUN_GLUE_ENTER_WORLD. The character's
    // 64-bit login GUID is at offset 0x00 (name at +0x08, race +0x100, class
    // +0x101, level +0x108 — verified against `Script_GetCharacterInfo` at
    // `0x004732A0`). Entries are filled from SMSG_CHAR_ENUM.
    VAR_GLUE_SELECTED_CHAR = 0x00B41DF4,
    OFF_GLUE_CHAR_GUID = 0x00,

    // Cursor state — single global type-tag at VAR_CURSOR_TYPE
    // dispatches reads through one of several payload slots depending
    // on what's picked up. The engine's `CursorHasItem` /
    // `CursorHasSpell` / `GetCursorMoney` and the various `Pickup*`
    // functions all read/write through these. Discovered by tracing
    // `Script_CursorHasItem` (`0x004895D0`) and the writers in
    // `FUN_00494B60` (item), `FUN_00494CC0` (money), `FUN_00494D20`
    // (spell), `FUN_00494E20` (pet action), `FUN_00494F80` (macro),
    // `FUN_004950F0` (action/inv/etc.), `FUN_00495010` (merchant).
    //
    // Type values:
    //   0  empty (nothing on cursor)
    //   1  bag item — GUID at `VAR_CURSOR_ITEM_GUID_LO/_HI`,
    //      itemID resolved by feeding the GUID to
    //      `FUN_OBJECT_FROM_GUID` (filter 0x2).
    //   2  money — copper at `VAR_CURSOR_MONEY_COPPER`
    //   3  player spell — spellID at `VAR_CURSOR_SPELL_ID`
    //   4  pet action — packed at `VAR_CURSOR_PETACTION_PACKED`
    //   5  merchant item — itemID at `VAR_CURSOR_GENERIC_SLOT`,
    //      0-based merchant slot at `VAR_CURSOR_GENERIC_SOURCE`.
    //      Written by `Script_PickupMerchantItem` (`0x004FB760`) →
    //      `FUN_004950F0(itemID, ?, slot0, 5)`.
    //   6/7 other drag-source items — itemID at
    //      `VAR_CURSOR_GENERIC_SLOT`. Type 7 is action-bar pickup
    //      (`FUN_004E6130`); type 6 is another drag path (mail/etc.).
    //   8  macro — 1-based index at `VAR_CURSOR_MACRO_INDEX`
    //   9  inventory item (equipped slot) — itemID stored at
    //      `VAR_CURSOR_GENERIC_SLOT`. Bare itemID rather than a GUID
    //      because the item is out of the descriptor stream while
    //      on the cursor.
    //   10 stable pet — 1-based index at `VAR_CURSOR_STABLEPET_INDEX`.
    //      Written by `Script_ClickStablePet` (`FUN_004CB420`) →
    //      `FUN_00495010(slot)`.
    //
    // `CursorHasItem` returns true for types 1 *and* 9 only.
    VAR_CURSOR_TYPE = 0x00B4D900,
    VAR_CURSOR_ITEM_GUID_LO = 0x00B4E248,
    VAR_CURSOR_ITEM_GUID_HI = 0x00B4E24C,
    VAR_CURSOR_MONEY_COPPER = 0x00B4E2F0,
    VAR_CURSOR_SPELL_ID = 0x00B4E2F4,
    VAR_CURSOR_PETACTION_PACKED = 0x00B4E2F8,
    VAR_CURSOR_MACRO_INDEX = 0x00B4E2FC,
    VAR_CURSOR_STABLEPET_INDEX = 0x00B4E300,
    VAR_CURSOR_GENERIC_SLOT = 0x00B4B41C,
    VAR_CURSOR_GENERIC_DISPLAY = 0x00B4D8EC,
    VAR_CURSOR_GENERIC_SOURCE = 0x00B4B420,

    // GUID → CGObject resolver. `__fastcall(type_filter, guid_lo,
    // guid_hi) → CGObject *` at `0x00468460`. Type filter 0x2 = item,
    // matching what `FUN_00494B60` and the trade-attached paths pass
    // when re-resolving the cursor GUID to a `CGItem *`.
    FUN_OBJECT_FROM_GUID = 0x00468460,

    // Virtual XML-template registry (a Storm hash table) — the store that
    // backs `inherits=` and `C_XMLUtil.GetTemplates`. The XML parser
    // (`FUN_006ede10`) registers every `virtual="true"` frame into it via
    // `FUN_006ee500`; the inherits resolver reads it via `FUN_006ee6f0`.
    // Both operate on these globals, so it's the authoritative template list.
    // See src/xml/Templates.cpp.
    //
    // VAR_XML_TEMPLATE_TABLE holds a POINTER to the bucket array (0 / the
    // array unallocated) and VAR_XML_TEMPLATE_MASK the bucket-count mask;
    // mask == 0xFFFFFFFF means no template has ever registered (walk = empty).
    VAR_XML_TEMPLATE_TABLE = 0x00CEEA1C,
    VAR_XML_TEMPLATE_MASK = 0x00CEEA24,
    // Bucket struct (0xC bytes): { linkOffset@+0x0, ?@+0x4, chainHead@+0x8 }.
    // A node's next pointer is at `*(node + linkOffset + 4)`; the tail
    // sentinel has its low bit set (chain ends on `(ptr & 1) != 0`).
    XML_TEMPLATE_BUCKET_STRIDE = 0xC,
    OFF_XML_TEMPLATE_BUCKET_LINKOFF = 0x0,
    OFF_XML_TEMPLATE_BUCKET_HEAD = 0x8,
    // Registry node payload: template name string @+0x14, parsed definition
    // XML node @+0x18 (hash @+0x0, virtual flag @+0x1C — unused here).
    OFF_XML_TEMPLATE_NODE_NAME = 0x14,
    OFF_XML_TEMPLATE_NODE_DEF = 0x18,
    // Parsed XML element node: its tag-name string ("Frame" / "Button" /
    // "CheckButton" / …) lives at +0x8 — this is the frame "type".
    OFF_XML_NODE_TAG = 0x8,
    // The definition node retains its full parsed DOM subtree — used by
    // GetTemplateInfo to read `<Size>` / `inherits=`. First child @+0x4, next
    // sibling @+0x1C. Attributes are read by name via the node method
    // FUN_006f2cf0 (`__thiscall(node, name) -> value string`, null if absent).
    OFF_XML_NODE_CHILD = 0x4,
    OFF_XML_NODE_SIBLING = 0x1C,
    FUN_XML_NODE_GET_ATTRIBUTE = 0x006F2CF0,
    // By-name template lookup — the same one `inherits=` resolves through.
    // `__fastcall(const char *name) -> definition node`, 0 if unregistered
    // (case-insensitive, hashed). See FUN_006ee6f0 in Templates.cpp notes.
    FUN_XML_TEMPLATE_LOOKUP = 0x006EE6F0,

    // `CreateFrame(type [, name, parent, template])` — the global FrameScript
    // function at table 0x00872E74 slot 3. Standard `int __fastcall(void *L)`
    // shape. Vanilla accepts only a SINGLE template name; the hook in
    // `Frame::CreateFrame` splits comma-separated inherits strings so modern
    // multi-template calls work (e.g. "UIPanelButtonTemplate,
    // SecureActionButtonTemplate"). It builds the frame with the first
    // resolved template through the engine, then applies each remaining
    // template onto the created object with the engine's OWN inherit
    // primitive: `frameObj->vtable[+8](templateDefNode, status)` for content
    // (FUN_00769820) and `(frameObj+0x24)->vtable[+8](...)` for the template's
    // `<Frames>` children (FUN_0076a060) — exactly the two `__thiscall`
    // recursion calls the builder FUN_006ee280 emits when resolving an
    // `inherits=`. Finalize/OnLoad (`vtable[+0x24]`) runs once during the
    // engine build and is NOT re-run.
    FUN_SCRIPT_CREATEFRAME = 0x007060B0,

    // XML-build "status" object — a printf-style error/warning accumulator the
    // appliers log through (`status->vtable[+0xc](status, level, fmt, ...)`,
    // __cdecl varargs). Script_CreateFrame builds one on its stack as the
    // 5-dword `{PTR_TEXLOAD_DESC_VTBL, 8, &self+8, (&self+8)|1, 0}` (same shape
    // the texture loader reuses; see PTR_TEXLOAD_DESC_VTBL). Reset/emptied by
    // `FUN_FRAMESCRIPT_STATUS_RESET` (`__thiscall(this)`), which frees any
    // accumulated message nodes and re-stamps the vtable.
    FUN_FRAMESCRIPT_STATUS_RESET = 0x00419E30,

    // --- Inline texture escape (`|Tpath:h:w:...|t`) backport -------------------
    // See src/text/InlineTexture.cpp and docs/InlineTextureEscapes.md. The
    // 1.12 text pipeline is a shared `|`-tokenizer feeding per-purpose loops.
    // Slice 1 injects an inline-texture quad into the text PAINT pass.

    // Text paint pass — flushes each text line's per-font-page glyph vertex
    // batches to the GPU every frame. `__fastcall(layoutObj)`; gated on
    // DAT_00c2b9d4. Co-hooked post-original: the flush walks the layout's node
    // list and queues each recorded inline icon as a fontstring-anchored region
    // placement (Text::InlineTexturePool). See FUN_005c8fe0.
    // (The GxU raw-quad primitives this site once drew with — dynamic-VB
    // lock/write/submit, texture bind/load/force-decode, colorop state — were
    // removed with the quad render mode; VAs are preserved in
    // docs/InlineTextureEscapes.md and git history if ever needed again.)
    FUN_TEXT_PAINT = 0x005C8FE0,

    // Default texture-load blend arg (DAT_00878cf0) — passed to the engine's
    // SetTexture-by-path (FUN_SIMPLETEXTURE_SET_TEXTURE) exactly as
    // Script_Texture_SetTexture's own call site does.
    VAR_TEXTURE_BLEND_DEFAULT = 0x00878CF0,

    // --- Texture masking (`Texture:SetMask` backport) ---------------------------
    // Mirrors the engine's own canonical masked-quad draw: the minimap BLIP mask
    // FUN_004eae10 (a sibling of the disc FUN_004ec440; both fed by the mask that
    // Minimap:SetMaskTexture at 0x004EE4A0 loads into DAT_00bc7968). FUN_004eae10
    // is the exact template — bind base on unit 0, mask on unit 1, force each
    // unit's combiner to MODULATE, submit with SEPARATE uv0/uv1, then unbind
    // unit 1:
    //   GxRs(0x17, base);  GxRs(0x1F, 1);      // unit 0: bind + MODULATE preset
    //   GxRs(0x18, mask);  GxRs(0x20, 1);      // unit 1: bind + MODULATE preset
    //   FUN_0058a2a0(4, corners, …, uv0, uv1); // explicit second UV stream
    //   GxRs(0x18, 0);                         // unbind unit 1
    // Unit 1's MODULATE combiner multiplies the mask's alpha into the base's
    // alpha, so a white-with-alpha mask clips the base to the mask's shape.
    //
    // The recipe was also pinned by an in-game probe (git history's
    // MaskProbe.cpp) on the live backend — findings, load-bearing:
    //   * The COMBINER PRESET selectors are 0x1F+unit (GXRS_COMBINER0), value 1 =
    //     MODULATE/MODULATE (the .rdata preset tables at 0x0080a25c/0x0080a274,
    //     consumed by the D3D9 combiner apply FUN_005a2dd0: COLOROP table
    //     {4,4,13,7,5,12}, ALPHAOP {3,4,3,7,5,3} — value 1 is the only preset
    //     that MODULATEs the alpha channel too, which is what masking rides).
    //     Binding a texture on unit 1 alone happens to give MODULATE by default,
    //     but FUN_004eae10 sets it EXPLICITLY — we mirror that.
    //   * The SECOND vertex UV stream (uv1) DOES feed unit 1 (FUN_004eae10
    //     passes a distinct uv1 = this+0x50), so uv1 = uv0 makes unit 1 sample
    //     the mask at the base's own texcoords — exactly SetMask semantics.
    //     uv1 is the LAST explicit UV slot that exists: the vertex-format
    //     component table at 0x008097A8 (13 formats × 13 components) has no
    //     format with a third UV set, so masks past the first CANNOT ride an
    //     explicit stream — they use texgen (below).
    GXRS_TEXTURE0 = 0x17,  // texture bind, +unit 0..7 (via FUN_GX_RS_SET_PTR)
    GXRS_COMBINER0 = 0x1F, // combiner preset, +unit 0..7 (via FUN_GX_RS_SET)
    GXRS_COMBINE_MODULATE = 1, // preset value: COLOROP=MODULATE, ALPHAOP=MODULATE
    // GxRs setters — `__fastcall(selector, value)`. The int form (0x00589E60)
    // writes combiner presets and the like; the pointer form (0x00589E80) writes
    // texture binds.
    FUN_GX_RS_SET = 0x00589E60,
    FUN_GX_RS_SET_PTR = 0x00589E80,

    // --- Texgen + per-unit texture matrices (multi-mask / rotated masks) -------
    // Decoded from the D3D9 backend's draw-time state flush FUN_005a2f00 (the
    // selector → SetTextureStageState/SetTransform Rosetta stone) and the
    // minimap DISC draw FUN_004ec440, which is the engine's own texgen-mask
    // template (it submits positions+colors ONLY and feeds BOTH units via
    // texgen + a per-unit texture matrix). Selector namespace is five per-unit
    // families, each 8 units wide (device supports 8 fixed-function stages;
    // live cap at [VAR_GX_DEVICE]+OFF_GXDEV_STAGE_CAP):
    //   0x17+u bind   0x1F+u combiner   0x27+u mip-bias   0x2F+u texgen
    //   0x37+u coord-transform mode
    // Texgen values (FUN_005a2b40): 0 = passthrough (stage reads vertex UV set
    // #u — the default; why stage 1 sees the explicit uv1 stream), 1 = OBJECT-
    // space position (D3D TCI_CAMERASPACEPOSITION + an auto inv(view)·inv(model)
    // basis matrix — UV becomes a pure function of the RAW vertex position,
    // immune to whatever model/view the pass set), 2 = world-space position,
    // 3 = camera-space position (identity basis; what the disc draw uses),
    // 5/4/6 = normal/reflection.
    // Coord-transform values (FUN_005a14e0): 0 = off, 1 = apply the unit's
    // texture matrix (basis × user matrix, D3DTTFF_COUNT3), 2 = projected.
    // Per-unit texture-matrix STACKS (4 levels, base ctx+0xFD0+unit*0x118;
    // slot 8 = model, 10 = view) — the disc draw's push/translate/scale/pop:
    GXRS_TEXGEN0 = 0x2F,      // texgen mode, +unit (int form)
    GXRS_TEXGEN_OBJECT_POS = 1,
    GXRS_TEXXFORM0 = 0x37,    // coord-transform mode, +unit (int form)
    GXRS_TEXXFORM_MATRIX = 1,
    // Texture-matrix stack ops — `__fastcall(int unit, ...)`. PUSH duplicates
    // the top (max depth 4); PUSH_LOAD pushes then loads an arbitrary 4×4
    // (row-vector D3D layout: out = pos·M, translation in row 3). POP restores.
    // (PUSH/POP were previously mislabeled FUN_GX_TEXUNIT_ENABLE/_DISABLE —
    // they never enabled anything; binding a texture is what enables a stage,
    // binding null disables it, per FUN_005a29d0.)
    FUN_GX_TEXMTX_PUSH = 0x0058B200,      // (unit)
    FUN_GX_TEXMTX_PUSH_LOAD = 0x0058B210, // (unit, const float m[16])
    FUN_GX_TEXMTX_POP = 0x0058B220,       // (unit)
    // Gx render-state journal push/pop — brackets a state scope; pop replays
    // every selector written since the push back to its prior value. The engine
    // wraps each region-layer draw (FUN_0076fb00) and the disc draw in a pair.
    FUN_GX_STATE_PUSH = 0x00589F40,
    FUN_GX_STATE_POP = 0x00589F50,
    // The indexed submit that actually queues the draw for the verts staged by
    // FUN_GX_PRIM_STREAMS — `__fastcall(int primType, int indexCount, const
    // void *indices)`. Every FUN_GX_PRIM_STREAMS caller invokes it immediately
    // after (region layer: FUN_0076fb00; disc: FUN_004ec440). State/matrix
    // RESTORES must run AFTER this call, not after the stream call — the disc
    // draw pops its matrices/state only past this point, and restoring earlier
    // reverts the state the queued draw will be flushed with.
    FUN_GX_PRIM_INDEXED = 0x0058A2E0,
    // The device/context singleton the Gx wrappers route through (set at
    // backend creation, FUN_00589ac0). +0x23C holds the live texture-stage cap
    // the per-stage applies clamp against (FUN_005a29d0/FUN_005a2b40).
    VAR_GX_DEVICE = 0x00C0ED38,
    OFF_GXDEV_STAGE_CAP = 0x23C,
    // The vertex-stream primitive every textured region draws through
    // (FUN_0076fb00 calls it per region). Decompiler signature (13 params):
    // (count, positions, posStride, s3, s3Stride, colors, colorStride, drop8,
    // drop9, uv0, uv0Stride, uv1, uv1Stride); RET 0x2C. The inner FUN_0058a3d0
    // builds an interleaved vertex from whichever of {s3, colors, uv0, uv1} are
    // non-null — so uv1 is a real, buildable second UV slot no engine caller
    // populates. `positions` for a region draw is the region's corner array
    // (region + OFF_SIMPLETEXTURE_CORNERS), which is how the mask co-hook
    // recovers the owning region: region = positions - OFF_SIMPLETEXTURE_CORNERS
    // (verified: FUN_00772fd0 appends the batch entry with positions =
    // region+0xD4, and FUN_0076fb00 forwards that pointer here).
    FUN_GX_PRIM_STREAMS = 0x0058A2A0,
    // Path → HTEXTURE loader trio (re-adds of the quad-era offsets removed in
    // 56c2670, with the same derivations — see docs/InlineTextureEscapes.md):
    // FUN_TEXTURE_LOAD_BY_PATH(path, &desc, flags, 0, 1) __fastcall, desc =
    // the 5-dword {PTR_TEXLOAD_DESC_VTBL, 8, &self8, (&self8)|1, 0} on-stack
    // node FUN_00770200 builds; flags via FUN_GX_TEXFLAGS_INIT(&flags, blend,
    // 0,0, 0,0,0, 1, 0) __thiscall. Never returns null (engine substitutes a
    // fallback texture). FUN_TEXTURE_GET_RENDERABLE(hTex, 1, 0) __fastcall
    // returns the bindable CGxTex at [hTex+0x140] and drives the streaming
    // load — calling it each frame IS the residency reference (what the
    // minimap's masked draw does for its mask at 0x004ec65c).
    FUN_TEXTURE_LOAD_BY_PATH = 0x00449D90,
    PTR_TEXLOAD_DESC_VTBL = 0x007FFA10,
    FUN_GX_TEXFLAGS_INIT = 0x0058A980,
    FUN_TEXTURE_GET_RENDERABLE = 0x0044ACF0,

    // --- Inline-texture positioning (slice 1: measure/emit integration) --------
    // The 1.12 text pipeline builds a layout as a list of render "nodes" (one
    // per wrapped line); each node owns per-font-page glyph vertex batches and a
    // screen origin, and the paint pass (FUN_005c8fe0) walks the layout's node
    // list drawing each node's batches translated by that origin. See
    // docs/InlineTextureEscapes.md and the decompiled map in InlineTexture.cpp.

    // Per-line glyph emitter. `__thiscall(node, byte *text, int len, uint
    // *colorState, float *penXYZ, uint *pageMask, int *linkState)`. Reads the
    // pen start from penXYZ[0..2] (node-local), appends glyph quads to the
    // node's page batches, and on return writes the final pen x (node-local,
    // as a FLOAT via `FSTP dword` — read linkState[4] as float, not int) into
    // linkState[4]. We co-hook it and, for a line that
    // contains an inline `|T…|t`, render the plain runs by delegating to the
    // original per segment (threading the pen via linkState[4]) and record an
    // icon quad at the pen between segments. Safe to call repeatedly per line
    // because the batch-clear at its top is gated on OFF_TEXT_NODE_FLAGS bit 3,
    // which is clear during normal accumulation draw. See FUN_005ccbe0.
    FUN_TEXT_EMITTER = 0x005CCBE0,
    // Node flags. Bit 3 (0x08) gates the emitter's per-call batch-clear (set
    // only in a rebuild mode we don't take); we segment only when it's clear.
    // Bit 6 (0x40) = EDITABLE text (set on editbox content; verified in-game:
    // the macro editbox is 0x4D, chat display 0x20D, FontStrings 0x0D). It rides
    // in the tokenizer's flags argument, so we suppress inline rendering per node
    // for editboxes — they show raw, editable `|T…|t` markup. This is the 1.12
    // analog of 4.3.4's per-render texture-disable flag (tokenizer bit 0x1000).
    OFF_TEXT_NODE_FLAGS = 0x5C,
    // Node horizontal justify (int): 0 = left, 1 = centre, 2 = right — the draw
    // builder's encoding. The emitter pre-shifts an inline icon's pen by the
    // line's justify offset for centre/right nodes (left needs no shift).
    // Verified in-game against centred/right-aligned text layout.
    OFF_TEXT_NODE_JUSTIFY = 0x54,
    // Node screen origin (float x, float y). The paint pass translates the
    // node's node-local glyph verts by this; an inline icon recorded in
    // node-local pen coords is drawn at (localX + originX, localY + originY).
    OFF_TEXT_NODE_ORIGIN_X = 0x70,
    OFF_TEXT_NODE_ORIGIN_Y = 0x74,
    // Node → per-font-page vertex buffers. The glyph emitter (FUN_TEXT_EMITTER)
    // bakes each glyph as a 4-vertex quad into one of up to TEXT_NODE_PAGE_COUNT
    // font-page buffers hanging off the node; the paint copy-out (FUN_005c8710)
    // reads them back TRANSLATE-ONLY (gpu.xy = local.xy + node origin). So a
    // rotation baked into these node-local verts survives to the screen — the
    // basis for fontstring:SetRotation (Texture::Transform::RotateFontStringNode).
    //   node + OFF_TEXT_NODE_PAGE_BUFFERS + page*4 -> page buffer ptr (may be 0)
    //   pageBuf + OFF_TEXT_PAGE_VERT_COUNT          -> int   vertex count
    //   pageBuf + OFF_TEXT_PAGE_VERTS               -> float* vertex array
    //   each vertex = 5 floats {x, y, z, u, v}, stride TEXT_VERT_STRIDE bytes.
    // Cross-verified: the emitter (writes verts here), FUN_005c8710 (copies them
    // to the GPU buffer), and FUN_005ce0c0 (indexes the page array) all agree.
    OFF_TEXT_NODE_PAGE_BUFFERS = 0xA0,
    TEXT_NODE_PAGE_COUNT = 8,
    OFF_TEXT_PAGE_VERT_COUNT = 0xC,
    OFF_TEXT_PAGE_VERTS = 0x10,
    TEXT_VERT_STRIDE = 0x14, // bytes; 5 floats per vertex (x, y, z, u, v)
    // Per-page baked PER-GLYPH colours (BGRA dword per vertex), parallel to the
    // vertex array above. Written by the emitter's bit-3-clear path
    // (`[pageBuf+0x20] + ([pageBuf+0x1C] + i)*4 = colorState` at 0x005CCC0x);
    // the paint copy-out FUN_005c8710 uses them VERBATIM — but only when the
    // colour count equals the vertex count, else it falls back to the node's
    // uniform colour (OFF_TEXT_NODE_COLOR). No later modulation: whatever alpha
    // is baked here is final, which is why InlineTexture's flush mirrors the
    // node's live alpha into these entries per frame.
    OFF_TEXT_PAGE_COLOR_COUNT = 0x1C,
    OFF_TEXT_PAGE_COLORS = 0x20,
    // Node uniform colour (BGRA). Seeded from fs colors[0] at block creation
    // (FUN_007724a0 → FUN_0044d420) and rewritten on every colour/alpha change
    // via the fs colour-changed vmethod (FUN_00772180 → FUN_0044d650 →
    // FUN_005ccb40). Its alpha byte (+0x2F) is the FOLDED live opacity —
    // SetTextColor alpha (fs+0xA8 array) × the owning frame's effective alpha
    // (*(fs+0x9C)+0xC8), maintained by FUN_0077fac0 — so it tracks parent
    // SetAlpha per change. The paint reads it per frame for uniform text; the
    // emitter's |c splice (case 0) and the draw builder's colorState seed both
    // copy it at bake time. NOTE: FUN_005ccb40 only invalidates (re-bakes) a
    // node on colour change when bit 3 is CLEAR — a bit-3 (standalone
    // FontString) node with baked per-glyph colours goes stale instead, hence
    // the flush-side alpha mirror.
    OFF_TEXT_NODE_COLOR = 0x2C,
    // Layout node list — intrusive singly-linked list of render nodes. Head at
    // [layout+0x24]; next = *(node + [layout+0x1c] + 4); the tail sentinel has
    // its low bit set (mirrors the paint pass's own walk in FUN_005c8fe0).
    OFF_TEXT_LAYOUT_NODE_HEAD = 0x24,
    OFF_TEXT_LAYOUT_NODE_LINK = 0x1C,
    // Node source-text pointer. The draw builder FUN_005cdc20 starts from
    // [node+0x48] and advances it per wrapped line, calling the emitter once
    // per line on the SAME node — so `emitterText == [node+0x48]` identifies
    // the first wrapped line (used to clear the node's icon list once per
    // build, then accumulate across lines). This buffer is a node-owned COPY of
    // the DISPLAY text (SStrCopy'd in the node init FUN_005cd6d0, capacity
    // [node+0x4c]) — the full source when it fits, or the ellipsized
    // "<prefix>..." form when the display-text resolver truncated it. So
    // comparing it against the fs source (OFF_FONTSTRING_TEXT) is the render-
    // truth "displayed vs full" signal FontString:IsTruncated uses.
    OFF_TEXT_NODE_TEXT = 0x48,
    // Node font-size field (float), fed to the font-height helper.
    OFF_TEXT_NODE_FONT_SIZE = 0x1C,
    // Node's gxu font-face pointer. The emitter FUN_005ccbe0 reads *(node+0x44)
    // as the `this` for every glyph call (glyph lookup FUN_005cabd0, the pair
    // advance, the native-height getter below).
    OFF_TEXT_NODE_FONT_FACE = 0x44,
    // Font pixel-height helper. `__fastcall(int flag /*ecx = (nodeFlags>>7)&1*/,
    // float fontSize /*stack = [node+0x1c]*/) -> float` (returns in ST0). The
    // emitter calls it to size glyphs; we call it to derive the text's vertical
    // extent so an inline icon can be centred on the line independent of font
    // size. See FUN_005c6fa0.
    FUN_TEXT_FONT_HEIGHT = 0x005C6FA0,
    // Font-face NATIVE pixel height getter: `__fastcall(void *fontFace) -> int`
    // (returns *(font+0x24C)). The emitter's pen scale is
    // FUN_TEXT_FONT_HEIGHT(...) / (float)nativeHeight — glyph-record advances
    // are in native-font units and multiply by that scale into pen units.
    // See FUN_005cae90.
    FUN_TEXT_FONT_NATIVE_HEIGHT = 0x005CAE90,
    // Glyph PAIR advance: `__thiscall(void *fontFace, uint prevCh, uint curCh)
    // -> float (ST0)`, native-font units = baseAdvance(prevCh) +
    // kern(prevCh,curCh)·[font+0x188], memoized per pair. The emitter computes
    // this at the top of each token iteration and places the CURRENT glyph at
    // pen + pairAdvance(prev, cur) — i.e. the pen is LAZY: a glyph's own
    // advance is only consumed when the NEXT token lands. Consequence
    // (verified in FUN_005ccbe0's tail): the emitter's final pen write
    // (linkState[4]) is x(lastGlyph) + pairAdvance(secondLast, last) — the
    // last pair RE-ADDED as a stand-in for the last glyph's own advance. For a
    // single-glyph run that stand-in is 0 (no pair ever computed) → the
    // read-back reports the run as zero-width (the money-string "coin sits on
    // the lone digit" bug). The emitter co-hook corrects the read-back with
    // these helpers: pen = linkState[4] − pairAdvance(prev,last)·scale +
    // baseAdvance(last)·scale. See FUN_005ca2d0.
    FUN_TEXT_GLYPH_PAIR_ADVANCE = 0x005CA2D0,
    // The flags&0x10 variant of the pair advance (same shape; the emitter
    // selects it when node flags bit 4 is set). See FUN_005ca4b0.
    FUN_TEXT_GLYPH_PAIR_ADVANCE_ALT = 0x005CA4B0,
    // Glyph BASE advance: `__thiscall(void *fontFace, uint ch) -> float (ST0)`,
    // native-font units, from the font's glyph cache (present for any glyph the
    // emitter just drew; returns a default for uncached). This is the pair
    // advance's first term — the terminal-glyph advance with no following
    // kern, exactly what an inline icon following the glyph needs. See
    // FUN_005ca240.
    FUN_TEXT_GLYPH_BASE_ADVANCE = 0x005CA240,
    // Hyperlink hit-rect registration (the GXUFONTHYPERLINKINFO append).
    // `__thiscall(node, float yA, float xLeft, float yB, float xRight,
    // char *linkStart, uint linkLen, char *escStart, uint escLen)` — 8 stack
    // dwords, RET 0x20. Appends a 0x20-byte record to the node's link array
    // (cap +0x7C, count +0x80, data +0x84): the four floats normalized to
    // screen space ({yA,left,yB,right} — the engine's y-first rect order; the
    // emitter's |H open writes linkState[2]=xLeft, |h close linkState[4]=
    // xRight, the draw builder provides the line's yA/yB), then the four link
    // dwords raw. Called from the emitter's |h close (case 5). The y extent is
    // the TEXT band (fontH tall) — co-hooked so a link containing a tall
    // inline icon gets its hit band expanded by the icon's overflow (the
    // line-height-growth counterpart; without it only the text-high slice of
    // a 32px emote link was hoverable). See FUN_005cd310.
    FUN_TEXT_LINK_RECT_ADD = 0x005CD310,

    // Shared `|`-escape tokenizer. `__fastcall(byte *text /*ecx*/, int
    // *bytesConsumed /*edx*/, uint *colorOut, uint flags, uint *payloadOut) ->
    // tokenType`. ~11 callers (measure loops, line-fit/wrap, the emitter, the
    // draw builder). It has NO `|T` case, so `|T` falls through to a literal
    // `|`. We co-hook it so an inline-texture span (`||T…||t` after the
    // FontString pipe-doubling, or a clean `|T…|t`) is consumed as ONE
    // near-zero-width token — this stops every measure/wrap caller from
    // counting the path text and wrapping early. The emitter detects icons on
    // its own (and delegates plain segments that never contain `|T`), so the
    // draw path is unaffected by this hook. See FUN_005c2810.
    FUN_TEXT_TOKENIZER = 0x005C2810,

    // Shared wrap-stepper dispatcher — lays out ONE wrapped line per call.
    // `__fastcall(gxuFont /*ecx*/, byte *text /*edx*/, float fontH, float
    // wrapWidth, int *outBreak, float *outWidth, void *outNext, float indent,
    // uint flags, byte *p10)`, callee cleans 0x20 (verified RET 0x20; prologue
    // PUSH EBP; MOV EBP,ESP; FLD [EBP+8] — MinHook-safe). Thin validator that
    // routes flags&0x80 to the no-wrap path (FUN_005C7300, ignores wrapWidth)
    // and everything else to the real stepper FUN_005C7470. Its exactly 4
    // callers are EVERY wrap consumer: the draw builder FUN_005CDC20 (render
    // breaks), the height measure FUN_005C2070 (GetStringHeight), the
    // chars-that-fit counter FUN_005C21C0 (ellipsis truncation), and the
    // break-array computer FUN_005C2430 (behind FUN_00772B60) — so a co-hook
    // here keeps render, height, truncation, and break arrays mutually
    // consistent. Text::InlineTexture shrinks the wrapWidth arg by the line's
    // inline-icon advances (the tokenizer hook hides them from the measure, so
    // breaks otherwise land as if icons were 0 wide and the emitter's real
    // advances overflow the right edge). UNITS: each caller passes
    // fontH/wrapWidth in its OWN space (node text units from the builder, the
    // small anchor-converted space from the fs-level callers — the path chat
    // wraps through); a raw-pixel subtraction annihilates the small spaces
    // (shredded chat lines into 2-glyph fragments on first flight). Convert
    // px→caller units by (fontH / FUN_TEXT_FONT_HEIGHT(flag, fontH)) — the
    // engine's own pixel realization of the fontH param (FUN_005C6940's final
    // scale uses exactly that call), so the ratio is valid in every caller's
    // space.
    FUN_TEXT_WRAP_STEPPER = 0x005C7260,

    // Focused-editbox global — holds the CSimpleEditBox that currently has
    // keyboard focus (the one showing a cursor), or 0 when no input field is
    // active. Written by EditBox SetFocus (`mov [0xcf4dc8],esi` @0x0077e3f8),
    // cleared by ClearFocus (FUN_0077e410). We read it (plus the buffer offsets
    // below) to identify the focused editbox's OWN text and suppress inline-icon
    // rendering for JUST that text, so the input field shows raw, editable
    // `|T…|t` markup while everything else (chat history icons/emotes) keeps
    // rendering. See the editbox vtable at 0x0081c8c0 / FUN_0077e410.
    VAR_FOCUSED_EDITBOX = 0x00CF4DC8,
    // CSimpleEditBox text-buffer layout — the editbox lays out and measures its
    // text IN PLACE from these buffers (no internal copy), so its layout node's
    // text pointer ([node+0x48]) and every wrapped-line text pointer point INTO
    // this buffer. Verified from the caret positioner FUN_0077da80: it selects
    // the buffer via `(*(byte*)(fe+0x318) & 8) ? *(char**)(fe+0x334) :
    // *(char**)(fe+0x32C)` and measures substrings of it directly with
    // FUN_00772ae0(fs, feBuf+off, n). Used by Text::InlineTexture to correlate a
    // node/token to the focused editbox by pointer range.
    OFF_EDITBOX_BUFFER_SELECT = 0x318, // byte; bit 3 (0x08) picks the masked buffer
    OFF_EDITBOX_BUFFER = 0x32C,        // char* — normal text buffer
    OFF_EDITBOX_BUFFER_MASKED = 0x334, // char* — masked/password variant buffer
    // The editbox's DISPLAY FontString ([fe+0x328], param_1[0xca] in FUN_0077da80).
    // Its rendered text is a COPY of the input buffer, stored at
    // *(fs + OFF_FONTSTRING_TEXT) — the editbox lays out (renders) from this copy,
    // NOT from the input buffer above (which only the caret/measure path reads in
    // place). So identifying the focused editbox's own render nodes needs BOTH
    // buffers. Verified: FUN_00771d80 (FontString SetText) writes the copy to
    // *(this+0xF0) via SStrDup.
    OFF_EDITBOX_TEXT_FONTSTRING = 0x328, // CSimpleFontString* — the display text object

    // --- FontString measure internals (GetStringWidth icon fix + GetStringHeight) ---
    // CSimpleFontString::GetStringWidthInternal — `float(__fastcall)(fs /*ecx*/)`,
    // returns in x87 ST0. Lazily computes into the fs+0xFC cache (ANCHOR units,
    // sentinel 0.0f = DAT_007ffd74): display-text getter FUN_00771EC0, then measure
    // core FUN_0044D670(fontHandle fs+0xE0, text, len, fontH*[fs+0x7C], &out,
    // fs+0x108, 0, flags fs+0x120), then `fs+0xFC = out / [fs+0x7C]`. Exactly 4
    // callers: Script_GetStringWidth (0x0079E510), GameTooltip auto-size
    // (0x00530640), the editbox caret positioner (0x0077DE70), and the layout
    // effective-width vmethod (FUN_00772930 = fs+0x24 vtbl slot +0x1C: explicit
    // rect width else string width). Prologue PUSH EBX; PUSH ESI; MOV ESI,ECX;
    // FLD [ESI+0xFC] — MinHook-safe. Co-hooked by Text::InlineTexture to add
    // inline |T icon advances; the hook must NEVER write the fs+0xFC cache (the
    // original may serve the cached value — the icon sum is re-added per call).
    FUN_FONTSTRING_STRING_WIDTH = 0x00772890,
    // CSimpleFontString::GetStringHeightInternal — `float(__fastcall)(fs)`, ST0.
    // Wrap-aware: FUN_0044D750 → FUN_005C2070 → wrap engine FUN_005C7260 (the
    // same wrap math the render uses); returns lines*fontH + (lines-1)*spacing
    // (spacing = fs+0xF4), cached at fs+0x100 (anchor units, 0.0 sentinel; empty
    // text → 0). Wrap width = the effective-width vmethod (fs+0x24 vtbl +0x1C).
    // Used internally by the tooltip auto-size (0x00530640), multi-line editbox
    // height (0x0077D4D0), and ScrollingMessageFrame line stacking (0x00788750)
    // — FontString::Metrics only CALLS it (the GetStringHeight backport), no hook.
    FUN_FONTSTRING_STRING_HEIGHT = 0x007729B0,
    // Per-fontstring font-height getter, ANCHOR units (reads fs+0xE4; with
    // mode=1 and measure-flag 0x200 set, re-derives from the gxu font's native
    // height). ECX = fs, mode on the STACK (`PUSH 0x1; MOV ECX,ESI; CALL` at
    // 0x007728F4) → declare `__fastcall(fs, edx_unused, int mode)`. The ENGINE's
    // width path multiplies its return by [fs+0x7C] (the layout UI SCALE) before
    // handing it to the measure core. Our width hook instead converts it to UI
    // PIXELS (× the anchor→px push factor VAR_UI_COORD_SCALE_DIV × 1024 /
    // VAR_UI_COORD_SCALE_MUL, ≈1468) to resolve `:0` auto-sized icon dims —
    // escape sizes are PIXELS, not fs+0x7C-scaled pen units; dividing a pixel
    // advance by fs+0x7C (~0.68) inflated a 16px icon to +44k measured px on
    // first flight. Distinct from FUN_TEXT_FONT_HEIGHT (0x005C6FA0), the
    // node-level gxu helper.
    FUN_FONTSTRING_FONT_HEIGHT = 0x007727B0,
    // FontString measure-flags word (the `flags` arg the measure paths pass to
    // FUN_0044D670/FUN_0044D750, and the justify field: bits 0-2). The measure
    // core translates fs-level bits to gxu/tokenizer flags; bit 0x1000 → gxu
    // bit 0x40 (OFF_TEXT_NODE_FLAGS bit 6 — the bit the tokenizer co-hook stands
    // down on and the width hook tests).
    //
    // NOTE — bit 0x1000 is NON-SPACE-WRAP, not a dedicated "editable" flag: it is
    // exactly what FontString:SetNonSpaceWrap toggles (verified in its setter
    // FUN_0079E9F0: `fs+0x120 ^= 0x1000`). Text::InlineTexture uses gxu 0x40 as an
    // EDITBOX PROXY — it works because the macro editor (the multi-line editbox
    // the focused-buffer pointer test misses) enables non-space-wrap while chat/
    // display text does not (observed node flags: macro editor 0x4D, chat 0x20D/
    // 0x205). The correlation is not a guarantee: a DISPLAY fontstring with
    // SetNonSpaceWrap(true) + inline |T icons would have its icons wrongly
    // suppressed. Rare and never observed; the focused-buffer pointer test remains
    // the primary editbox guard.
    OFF_FONTSTRING_MEASURE_FLAGS = 0x120,

    // CSimpleFontString::GetDisplayText — the engine's truncation MECHANISM (the
    // reason FontString:IsTruncated has anything to detect). `char* __thiscall(fs
    // /*ecx*/, float availW, float availH)` (dummy-EDX __fastcall form; RET 0x8).
    // Reads the raw text at fs+0xF0 and, when the box is BOUNDED (availW>0 AND
    // (maxLines fs+0x128 != 0 OR availH>0)), measures how much fits via the
    // chars-that-fit counter FUN_0044D960; on overflow it writes "<prefix>...\0"
    // into the shared scratch buffer VAR_TEXT_ELLIPSIS_BUFFER and returns THAT,
    // else the fs+0xF0 pointer (or NULL). The rebuild FUN_007724A0 calls it with
    // availW=(rect.right−rect.left)/[fs+0x7C], availH=(rect.bottom−rect.top)/
    // [fs+0x7C] and lays out whatever it returns — the node then COPIES that
    // string into OFF_TEXT_NODE_TEXT. IsTruncated does NOT call this (the rect at
    // fs+0x64 is zero between frames, so a Lua-time re-call always sees an
    // unbounded box); it reads the node copy instead. Kept for the availW/availH
    // derivation + the bounded-box gate. Verified in the Octo binary: prologue
    // 55 8B EC 83 EC 0C 53 56 8B F1, ellipsis literal "..." at 0x00800188.
    FUN_FONTSTRING_DISPLAY_TEXT = 0x00771EC0,
    // Shared 0x1000-byte BSS scratch buffer the display-text resolver above writes
    // the truncated, "..."-suffixed string into (memset FUN_0064A5A0 at 0x00772054,
    // suffix append at 0x00772086) and returns on truncation. Transient — every
    // layout rebuild overwrites it, and the node keeps its OWN copy (see
    // OFF_TEXT_NODE_TEXT), so it is not read directly by any live path.
    VAR_TEXT_ELLIPSIS_BUFFER = 0x00CF0CC0,

    // Per-fontstring MAX LINE COUNT. 0 = no cap. When non-zero, the display-text
    // resolver forces availH = maxLines × lineHeight (verified in FUN_00771EC0:
    // `if (fs+0x128 != 0) param2 = fs+0x128 * lineHeight`), so text past that
    // many wrapped lines is ellipsized. Backs FontString:SetMaxLines/GetMaxLines
    // — the truncation control 1.12 never exposed for fontstrings (SetMaxLines is
    // ScrollingMessageFrame-only). Vanilla fontstrings always word-wrap, so this
    // is the way to force a single-line, ellipsized label: SetMaxLines(1).
    OFF_FONTSTRING_MAX_LINES = 0x128,
    // Lazily-computed measure caches (anchor units, 0.0f = dirty sentinel):
    // fs+0xFC = string width (GetStringWidth), fs+0x100 = string height
    // (GetStringHeight). SetText (FUN_00771D80) zeroes BOTH when the content
    // changes; SetMaxLines mirrors that so the getters recompute after the cap.
    OFF_FONTSTRING_WIDTH_CACHE = 0xFC,
    OFF_FONTSTRING_HEIGHT_CACHE = 0x100,
    // Ref-counted handle DecRef (resolve via FUN_0041af30 → --refcount → dtor at
    // 0). `__fastcall(void *handle)`. SetText releases the text-block handle
    // (fs+0xF8) through it before nulling the field; SetMaxLines does the same to
    // force a fresh node rebuild with the new line cap.
    FUN_HANDLE_RELEASE = 0x0041AED0,
    // VisibleRegion layout invalidate — marks the region (the fs+0x24 layout
    // subobject) dirty and propagates to parent/children so it re-lays-out.
    // SetText's tail calls it `FUN_007680e0(fs+0x24, 0)`. `__thiscall(region,
    // int)` (dummy-EDX form; the int arg is on the stack).
    FUN_FONTSTRING_LAYOUT_INVALIDATE = 0x007680E0,
};
