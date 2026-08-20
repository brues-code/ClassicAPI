# ClassicAPI — TODO

Candidates for new DLL functions, prioritized. All are things the
[!!!ClassicAPI](C:\WoW\FrostmourneClient\Interface\AddOns\!!!ClassicAPI) Lua
addon polyfills badly, hackily, or not at all, and where engine access is the
right answer.

## ~~1. `C_QuestLog.IsQuestFlaggedCompleted(questID)`~~ — NOT FEASIBLE in 1.12

The original TODO note ("engine maintains a completed-quests bitfield in
client memory") was based on 3.3.5 / WotLK semantics. Vanilla 1.12 has no
equivalent: neither `QueryQuestsCompleted` nor `GetQuestsCompleted`
exists, the protocol has no `CMSG_QUERY_QUESTS_COMPLETED`, and the
client doesn't track historical completions. The polyfill at
`Util/C_QuestLog.lua:5-8` works on Frostmourne (3.3.5a) where those
functions exist; on the 1.12 Octo client they're absent.

Servers that want to expose this in 1.12 (notably Turtle WoW) do it
via the `CHAT_MSG_ADDON` channel — the addon sends `.queststatus` over
guild/whisper chat and the server replies with addon-channel messages
the addon parses. Fundamentally async, server-specific, and requires
SavedVariables-style persistence across sessions to amortize the
round-trip — all of which belong on the Lua side, not in this DLL.
There's nothing the DLL can usefully add here.

## ~~2. `C_Spell.GetSpellDescription(spellID)`~~ — DONE

Calls the engine's own spell-description formatter at `0x005075F0`,
which reads the description string from `Spell.dbc` (record `+0x228`,
locale-applied) and resolves `$s1`/`$s2`/`$o1`/`$d`-style placeholders
character-by-character. 8 callers in the engine (spell tooltip builder,
talent UI, trainer, etc.); we mirror the simplest one's args
(`contextFlag=0` = "no caster scaling, base-rank values"). Output goes
into a 1024-byte caller-provided buffer that the helper null-terminates.

The interesting find: I expected the description to need a CGUnit
context for substitution, but the engine has a "no scaling" code path
that produces `"14 to 22 Fire damage"` (Fireball Rank 1's actual base
range) without any unit reference. That's the right semantic for
"describe this spell" — modern WoW behaves the same when called outside
a unit context.

Side note: a global at `0x00BE0B80` is the formatter's parser cursor
(it walks the description string char-by-char, advancing the cursor in
place). It's set and managed entirely inside the helper — callers don't
touch it. Lua C functions are single-threaded, so the global state is
safe to share with whatever else might call the formatter (it's all on
the same thread).

See [src/spell/Description.cpp](src/spell/Description.cpp) and the
`FUN_FORMAT_SPELL_DESCRIPTION` block in [src/Offsets.h](src/Offsets.h)
for the full calling convention (8-arg `__fastcall`).

## ~~3. `C_Item.IsBound` / `isBound` from `GetContainerItemInfo`~~ — DONE

Implemented as `C_Item.IsBound(itemLocation)`. Reads the soulbound bit
(`ITEM_FIELD_FLAGS` bit 0) directly off the CGItem's UpdateField
descriptor — no scan-tooltip hack, no string comparison. Accepts the
modern `{equipmentSlotIndex=N}` and `{bagID=B, slotIndex=S}` location
shapes. See [src/item/Bound.cpp](src/item/Bound.cpp) for the soulbound
read; the slot-resolution chain it depends on lives in
[src/item/Location.cpp](src/item/Location.cpp).

## ~~4. `GetSpellInfo` for unlearned spells~~ — DONE

Implemented as the global `GetSpellInfo(spellID)`. Returns the full 9-tuple
matching 3.3.5 semantics, including `true`/`false` for `isFunnel` (verified
against the 3.3.5 client). See [src/SpellInfo.cpp](src/SpellInfo.cpp).

## ~~5. `GetServerTime()`~~ — DONE

Reads year/month/day/hour/minute from the engine's game-time struct at
`0x00CE8538` (populated by `SMSG_LOGIN_VERIFY_WORLD` /
`SMSG_LOGIN_SETTIMESPEED` and advanced by the internal tick handler)
and converts via `_mkgmtime` to a Unix epoch timestamp.

The interesting find: the engine has its own "to seconds since epoch"
helper at `0x00642320` that I almost reused — but decoding the magic
constant `0xC22E4507` after the `_mkgmtime` call revealed the helper
divides the result by `86400` (seconds in a day), so it returns
**days-since-epoch**, not seconds. Rolled our own `_mkgmtime` call
instead.

The 1.12 wire protocol carries time at minute granularity — there are
no seconds in `SMSG_LOGIN_SETTIMESPEED`'s packed gametime field — so
the engine's stored hour/minute steps once per minute. To make the
returned timestamp tick every second (which is what `GetServerTime`
callers actually need), we interpolate within the minute using
`GetTickCount`: whenever the engine's minute changes, we anchor to the
current tick and add `(now - anchor) / 1000` for subsequent calls. The
cold-start value is off by 0..59 seconds since we have no way to know
how far into a minute the engine is the first time we observe it, but
after the first minute rollover the anchor lands at the boundary and
the timestamp is accurate for the rest of the session.

See [src/time/Server.cpp](src/time/Server.cpp) and
`VAR_GAMETIME_STRUCT` / `OFF_GAMETIME_*` in
[src/Offsets.h](src/Offsets.h).

## ~~6. `C_QuestLog.GetTitleForQuestID(questID)`~~ — DONE

Reads the title out of the quest static-info cache at offset `+0x9C`
(inline null-terminated C string within the data block returned by the
cache's `_GetRecord`). Title-offset derivation: traced from the engine's
own quest-log title path at `0x004DF180`, which calls `_GetRecord` and
then does `add eax, 0x9C; ret` — the result is fed directly to
`lua_pushstring`, so `+0x9C` is the C string itself, not a pointer.

The original TODO note "engine has the title in `Quest.dbc`" turned out
to be wrong — there is no `Quest.dbc` in 1.12 (only `QuestInfo.dbc` for
reward types and `QuestSort.dbc` for category headers). All quest static
data is server-authoritative, fetched via `SMSG_QUEST_QUERY_RESPONSE` and
held in the runtime cache at `0x00C0E1B0`. The "async dance" the polyfill
does is the unavoidable cost of that — though now it's just one call to
`C_QuestLog.RequestLoadQuestByID` plus an `OnEvent` listener instead of
a hidden tooltip and a 180-second timeout.

See [src/quest/Title.cpp](src/quest/Title.cpp) and the shared cache
helper at [src/quest/Cache.h](src/quest/Cache.h). The data-block field
offsets (currently just `+0x9C` for title) live in `Cache.h` alongside
the `Lookup`/`Peek` wrappers, so additional cached fields like
description and objectives can be added there as we trace them.

## ~~7. `UnitAuraBySlot` / `UnitAuraSlots`~~ — DROPPED (obviated by `C_UnitAuras`)

Modern's slot system was a Shadowlands-era continuation-token pattern
for huge aura sets — pointless on vanilla where the per-unit aura
array caps at 48 and `C_UnitAuras.GetUnitAuras(unit, [filter])`
returns all of them in one call. `GetAuraDataByIndex` /
`GetUnitAuraBySpellID` / `GetPlayerAuraBySpellID` cover the
lookup-by-index and lookup-by-spellID consumer patterns. The
`Util/Aura.lua` / `Util/AuraUtil.lua` files referenced by the
original TODO no longer exist in the repo.

## ~~8. `GetItemInfo` for uncached items~~ — DONE (via #12 + #13)

The "cache-fill path with a proper async callback so addons stop polling
on a frame timer" goal landed as the request/event pair below:

- **#12** ships `C_Item.IsItemDataCachedByID(item)` /
  `C_Item.RequestLoadItemDataByID(item)` (and the ItemLocation
  variants), wrapping the engine's existing
  `DBCache_ItemStats_C_GetRecord` at `0x55BA30` with a non-NULL callback
  to trigger the SMSG_ITEM_QUERY_SINGLE network fetch.
- **#13** ships `ITEM_DATA_LOAD_RESULT(itemID, success)`, fired from our
  `__stdcall` callback when the engine drops the response into the
  cache (or synchronously if it was already loaded).

Together those replace the polyfill's 180-second polling frame
(`ItemEventListener` in `Util/ItemUtil.lua:254-372`) — addons can now
fire `RequestLoadItemDataByID(id)` and listen for
`ITEM_DATA_LOAD_RESULT` instead.

A single `GetItemInfo(item)` that "returns nil and triggers a load" in
one call (matching modern WoW's `GetItemInfo` semantics for uncached
items) would be a thin convenience wrapper on top, but the modern
preferred pattern is exactly the request/event pair we have, so it's
not pursued separately.

## ~~9. `C_Item.GetItemInfoInstant(item)`~~ — DONE (not in original list)

The always-available subset of `GetItemInfo` — fields that depend only on
classification, not on player-specific state. Accepts itemID or
`item:NNN` link strings (full chat links work too); returns nil for
uncached items rather than blocking on a server query. See
[src/item/Info.cpp](src/item/Info.cpp). Reading `ItemSubClass.dbc` was the
fiddly bit (parallel-storage hack at `+0x00`/`+0x04` for compound-key
lookup, plus a verbose→short locale-name fallback chain) — full notes in
[CLAUDE.md](CLAUDE.md) and [src/Offsets.h](src/Offsets.h).

## ~~10. `C_Item.GetItemID(itemLocation)`~~ — DONE (not in original list)

Returns the itemID for the item at a given equipment slot or bag/slot
tuple. Useful as the input to `GetItemInfoInstant`/`GetItemInfo` when you
only know which slot an item came from. Required factoring out the shared
slot-resolution code from `IsBound` into [src/item/Location.cpp](src/item/Location.cpp);
the GetItemID-specific bit (one extra deref) lives in
[src/item/ID.cpp](src/item/ID.cpp). The non-obvious part: the itemID
isn't in the CGItem's UpdateField descriptor (its `OBJECT_FIELD_ENTRY`
slot is empirically 0), but in a separate instance block at `CGItem+0x08`.

## ~~11. `C_EventUtils.IsEventValid(eventName)`~~ — DONE (not in original list)

Walks the engine's static event-name table at `0x00BE11D8` and returns
`true` if any populated slot contains the queried name. See
[src/event/Util.cpp](src/event/Util.cpp). The interesting bit: third-party
DLLs (notably SuperWoWhook on Turtle clients) inject custom events by
**overwriting `.rdata` event-name strings in place** rather than appending
to the table — so a runtime walk picks up their additions for free. Full
notes in [CLAUDE.md](CLAUDE.md).

## ~~12. `C_Item.IsItemDataCached*` / `RequestLoadItemData*`~~ — DONE (not in original list)

Wraps the engine's existing client-side item cache (`_GetRecord` at
`0x55BA30`, `requestIfMissing` flag). Both `ByID` and ItemLocation
variants ship. See [src/item/Data.cpp](src/item/Data.cpp).

## ~~13. `ITEM_DATA_LOAD_RESULT` event~~ — DONE

Fires with `(itemID, success)` when an item finishes loading after a
`RequestLoadItemData*` call (or synchronously when the data was already
cached). See [src/event/Custom.cpp](src/event/Custom.cpp) for the
custom-event plumbing and [src/item/Data.cpp](src/item/Data.cpp) for
the wiring to the cache callback.

The investigation needed three engine pieces nobody had documented:

- **Dispatcher** at `0x00703F50` — `__cdecl void(int eventID, const char *fmt, ...)`,
  149 callers, indexes the table at `[0xCEEF68] + id*16`, walks the
  subscriber chain. Format is printf-style (`%s`, `%d`, `%u`, `%f`); no
  bool code, pass `%d` with 0/1.
- **Registration table** at `[0xCEEF68]` (count at `[0xCEEF64]`),
  16-byte entries, name at `+0x00`, subscriber chain head at `+0x0C`.
  `Frame::RegisterEvent` at `0x00702140` strcmps event names against
  this table.
- **Teardown landmine.** `FrameScript_Initialize`'s reload path
  (`0x00701A40`) calls `SMemFree(entry.name)` on every entry; our static
  literals are not Storm-allocated and trip the safety check. There's a
  NULL-name shortcut (`jz` at `0x00701A45`) so we null our entries
  before the engine sees them.

Full notes in [CLAUDE.md](CLAUDE.md) under "Firing a custom event
end-to-end".

## ~~14. `C_QuestLog.RequestLoadQuestByID` + `QUEST_DATA_LOAD_RESULT`~~ — DONE (not in original list)

Fires `QUEST_DATA_LOAD_RESULT(questID, success)` when quest static info
(description, objectives, reward text) finishes loading. Synchronously
fired when the questID was already in the client cache; asynchronously
fired after `SMSG_QUEST_QUERY_RESPONSE` when the engine has to round-trip.

Implemented as a near-direct mirror of `Item::Data`: the quest cache
`_GetRecord` at `0x00562A40` has the same five-arg shape and the same
`callback != NULL` gate as the item cache `_GetRecord`. The interesting
investigation was confirming that `[0x00BB7480]` (passed as the cache
key in `Script_GetQuestLogQuestText`) is a *questID*, not a struct
pointer — verified by tracing the load site at `0x004DEF5D` back through
`mov eax, [esi + 0x00BB71C0]`, which reads field +0 of a quest log
entry (= questID per the quest log struct). The cache is then keyed by
questID just like the item cache is keyed by itemID.

See [src/quest/Data.cpp](src/quest/Data.cpp) and "Quest static-info
cache" in [CLAUDE.md](CLAUDE.md). The custom-event plumbing
([src/event/Custom.cpp](src/event/Custom.cpp)) is shared with
`ITEM_DATA_LOAD_RESULT` — no new infrastructure needed.

## ~~15. `GetFactionInfoByID` / `GetFactionIDByIndex`~~ — DONE (not in original list)

`GetFactionInfoByID(factionID)` returns the same 11 values as
`GetFactionInfo(factionIndex)` keyed by factionID — implemented as a thin
wrapper that walks the engine's index→ID resolver at `0x004D5FA0` to find
the displayed-list index for the requested ID, then replaces Lua arg 1 and
tail-calls `Script_GetFactionInfo` (`0x004D64F0`). Only works for factions
the player has rep with, matching modern WoW's semantics.

`GetFactionIDByIndex(factionIndex)` exposes the same resolver directly —
returns the factionID for real entries, `0` for headers (normalizing the
engine's mix of `0` for "Other" and `-1` for "Inactive"), `nil` for OOB.
Modern WoW (5.0+, including Classic Era 1.15.x) returns this as the 14th
value of `GetFactionInfo`, but 1.12-3.3.5 don't expose it at all.

See [src/faction/Info.cpp](src/faction/Info.cpp) and "Faction lookups" in
[CLAUDE.md](CLAUDE.md) for the resolver/dual-count details.

## ~~16. `C_Container.GetContainerItemID(bagIndex, slotIndex)`~~ — DONE

Shipped under the modern `C_Container.*` namespace (matching post-MoP
WoW). Wraps the existing bag→CGItem path: a new public
`Item::Location::ResolveBag(L, bagID, slotIndex)` exposes the bag
resolver that was previously private inside the table-shape
`Resolve()`, and a small `ItemIDFromCGItem` helper in
[src/item/ID.cpp](src/item/ID.cpp) factors out the CGItem → instance
block → itemID step now used by both `C_Item.GetItemID` and
`C_Container.GetContainerItemID`.

Note: ships under `C_Container` rather than as the historical legacy
global `GetContainerItemID` since the modern namespace is the canonical
home for bag/container APIs (the global was deprecated in 10.0). If
addons need the legacy global it's a one-line addition.

## ~~17. `GetItemIcon(itemID)` / `C_Item.GetItemIcon(itemLocation)` / `C_Item.GetItemIconByID(item)`~~ — DONE

Three Lua entry points that all share a `PushIconForItemID(L, itemID)`
helper in [src/item/Info.cpp](src/item/Info.cpp). The helper routes
through the existing `FetchItemRecord` (item cache) and `BuildIconPath`
(`ItemDisplayInfo.dbc` lookup at `+0x14`) — same code path
`GetItemInfoInstant` already runs, just stripped to the icon-only return.

The refactor that fell out: extracted `Item::ID::FromCGItem` (the
CGItem → instance-block → itemID step) into [src/item/ID.h](src/item/ID.h)
so both `Item::ID` (for `C_Item.GetItemID` and
`C_Container.GetContainerItemID`) and `Item::Info` (for
`C_Item.GetItemIcon`'s ItemLocation form) can share it. Was static-dup
across two files before this; now it's one definition.

Returns the path string (deviation from modern's `fileID:number` —
1.12 has no fileID system; same call-out as
`C_Spell.GetSpellTexture`'s docs).

## ~~18. `UnitGUID(unit)`~~ — DONE

Returns the unit's 64-bit GUID as `"0xHHHHHHHHLLLLLLLL"` (16 hex
digits, hi dword first). Reads `*(unit + OFF_UNIT_GUID_PTR)` to get
the 8-byte struct, formats with `snprintf`. Lives in
[src/unit/Identity.cpp](src/unit/Identity.cpp).

Two behavioral notes worth flagging in docs:

- **Vanilla format, not modern's prefix shape.** 1.12 GUIDs are plain
  64-bit integers (the `"Player-1234-..."` / `"Creature-0-..."`
  formatted GUIDs were a 6.0 addition). Addons backporting modern
  GUID parsers need to accept the `"0x..."` form.
- **Errors on totally-unknown unit tokens.** `ResolveUnitToken` itself
  raises `"Unknown unit name: <token>"` for strings outside the known
  unit-ID list. We don't suppress it — matches how every other 1.12
  unit-token function (`UnitName`, `UnitAffectingCombat`, etc.)
  behaves. Tokens that resolve to "no current unit" (e.g. `"target"`
  with nothing targeted) cleanly return nil via the GUID-is-zero
  short-circuit.

## ~~19. `IsPassiveSpell` / `C_Spell.IsSpellPassive`~~ — DONE

Reads `Attributes` (+0x18) bit 6 (`SPELL_ATTR_PASSIVE = 0x40`) off the
`Spell.dbc` record — same pattern [src/spell/Info.cpp](src/spell/Info.cpp)
already used for `isFunnel` (AttributesEx bit 6, on a different field).

Shipped both forms with shared `PushIsPassive` back-end:

- `IsPassiveSpell(spellID)` / `IsPassiveSpell(slot, bookType)` — the
  3.0-era global, supports both arg shapes via `ResolveLuaArgsToSpellID`
  (mirrors `GetSpellInfo` / `GetSpellLink`).
- `C_Spell.IsSpellPassive(spellID)` — the modern 10.0 form (word order
  flipped during the `C_Spell` namespace migration). spellID-only.

## ~~20. `GetSpellLink(spellID)` / `C_Spell.GetSpellLink(spellID)`~~ — DONE

Builds `|cff71d5ff|Hspell:ID:0|h[Name]|h|r` via `snprintf` after a
single `Spell.dbc` name read. The trailing `:0` after the spellID
matches modern's hyperlink shape so addons backporting from later
expansions can `gsub` with the standard `|Hspell:(%d+):` pattern; 1.12
ignores it during link parsing.

Both forms shipped:

- `GetSpellLink(spellID)` and `GetSpellLink(slot, bookType)` (global)
  return `(link, spellID)`. The slot-and-bookType overload reuses the
  same `Spell::Lookup::ResolveLuaArgsToSpellID` helper as
  `GetSpellInfo`.
- `C_Spell.GetSpellLink(spellID)` (table) returns just the link string
  — the caller already had the spellID on hand.

See [src/spell/Info.cpp](src/spell/Info.cpp) `Script_GetSpellLink` /
`Script_C_GetSpellLink` and the `BuildSpellLink` helper.

## ~~21. `InCombatLockdown()`~~ — DONE

Reads `UNIT_FLAG_IN_COMBAT` (bit 19, `0x00080000`) of the player's
UNIT_FIELD_FLAGS — same bit `Script_UnitAffectingCombat` at
`0x00517E4A`-`0x517E5C` tests via `mov eax, [fields+0xA0]; shr eax, 19;
test al, 1`. Modern WoW's "lockdown" gates secure-frame UI changes;
1.12 has no secure-frame system, so the function reduces to a plain
"is the player in combat" check (equivalent to
`UnitAffectingCombat("player")` but skips the unit-token resolution).

Lives in [src/unit/Combat.cpp](src/unit/Combat.cpp) — first occupant of
the new `src/unit/` directory. Future unit-flag accessors
(`UnitIsAFK`, `UnitIsDND`, `UnitIsFeignDeath`, `UnitClassBase`, etc.)
can land alongside without polluting the per-domain Lua-namespace
directories.

## ~~22. `UnitClassBase(unit)`~~ — DONE

Locale-independent class file string (`"WARRIOR"`, `"MAGE"`, etc.).
Resolves the unit via `FUN_RESOLVE_UNIT_TOKEN`, reads the class
byte from `UNIT_FIELD_BYTES_0` (descriptor `+0x79`, same byte
`Script_UnitClass`'s general-token branch uses), and looks up
`ChrClasses.dbc::Filename` (record `+0x38`) for the english token.

Works for any synced unit — both fields are broadcast UpdateField /
static-DBC data. Returns nil for unresolvable units (empty
`partyN` slot, `target` with no target, out-of-range class byte
for non-player units). Implementation uses the existing
`DBC::StringField` helper so the bounds-checking and locale-array
hairiness stays in one place.

See [src/unit/ClassBase.cpp](src/unit/ClassBase.cpp).

## ~~23. `GetItemSpell(item)`~~ — DONE

Returns `(spellName, spellID)` for the on-use spell attached to an
item (potions, trinkets, scrolls, hearthstone, food/drink), or nil
for items without one. Reads the cached `ItemStats_C` record's
`m_spellId[5]` array at `+0x11C` and `m_spellTrigger[5]` at `+0x130`,
matching the first slot with `trigger == 0` (`ITEM_SPELLTRIGGER_ON_USE`).
Combines with `Spell.dbc.Name[locale]` at record `+0x1E0` for the
spell name.

Offsets computed from VanillaHelpers's fully-decoded
`ItemStats_C` struct; verified empirically against Hearthstone
(itemID 6948, slot-0 spell 8690, trigger 0). Other trigger codes
(`ON_EQUIP`=1, `CHANCE_ON_HIT`=2, `SOULSTONE`=4, `LEARN_SPELL`=6)
are deliberately ignored — matches modern WoW's `GetItemSpell`,
which only reports the on-use spell.

Same auto-warmup pattern as the other cache-backed accessors:
returns nil for uncached items and silently fires the cache fill,
so a follow-up call after `GET_ITEM_INFO_RECEIVED` lands the data.

See [src/item/Spell.cpp](src/item/Spell.cpp).

## ~~24. `GetInventoryItemDurability(invSlot)` / `C_Container.GetContainerItemDurability(containerIndex, slotIndex)`~~ — DONE

Returns `(current, max)` durability for the player's equipped item or
bag/bank slot, or nothing if the slot is empty or the item has no
durability concept (consumables, materials, rings, trinkets, shirts,
tabards). Both forms shipped:

- `GetInventoryItemDurability(invSlot)` — character-pane equipment slot
  (1-based, 1..19). Player-only by design, matches modern API; the
  original TODO line had `(unit, slot)` but modern
  `GetInventoryItemDurability` takes only the slot.
- `C_Container.GetContainerItemDurability(containerIndex, slotIndex)` —
  bag/bank slot. Goes through `Item::Location::ResolveBag` →
  `PackBagSlot` → `GetItemBySlot`, same chain
  `C_Container.GetContainerItemID` already used.

Both share the inner `PushDurabilityForItem` helper — only the
CGItem-resolution path differs. Reads ITEM_FIELD_DURABILITY (+0xA0) and
ITEM_FIELD_MAXDURABILITY (+0xA4) off the CGItem descriptor at `+0x114`
— the same descriptor [src/item/Bound.cpp](src/item/Bound.cpp) reads
FLAGS from. Field offsets verified by decoding
`Script_GetInventoryItemBroken` at `0x004C8590`, which tests
`[descriptor+0x3C] & 0x08` for the broken flag bit AND
`[descriptor+0xA4] > 0 && [descriptor+0xA0] == 0` as a fallback.
Modern semantics confirmed against 3.3.5's
`Script_GetInventoryItemDurability` at `0x005EA170`: returns nothing
(0 values) when max is 0.

See [src/item/Durability.cpp](src/item/Durability.cpp).

## ~~25. `FindSpellBookSlotByID(spellID)`~~ — DONE

Walks the player spellbook at `0x00B700F0` first, then the pet
spellbook at `0x00B6F098`, returning `(slot, bookType)` for the first
match. Bounds at `SPELLBOOK_MAX_SLOTS = 0x400` (the engine's own cap
in `Script_GetSpellName`); empty/zero entries past the populated count
are skipped naturally since spellID 0 doesn't match any positive
input. Result feeds directly into the slot-and-bookType API surface
(`GetSpellName`, `GameTooltip:SetSpell`, etc.).

Logic added to [src/spell/Lookup.cpp](src/spell/Lookup.cpp) as
`Spell::Lookup::FindSpellbookSlot`; the Lua C function lives next to
the other spell registrations in [src/spell/Info.cpp](src/spell/Info.cpp).

## ~~26. `C_Item.IsEquippableItem(item)`~~ — DONE

Cache-record read of `m_inventoryType` at `OFF_ITEMSTATS_INVENTORY_TYPE`.
Non-zero = equippable. Lives in `item/Equipment.cpp` alongside
`OffhandHasWeapon` (which uses the same field) and `IsEquippedItem`.
Synchronous; returns `false` on cache miss without firing a load.
Namespaced under `C_Item` to match Classic Era 1.15.x.

## ~~27. `IsEquippedItem(item)`~~ — DONE

Shipped as `C_Item.IsEquippedItem` in
[src/item/Equipment.cpp](src/item/Equipment.cpp). Walks equipment
slots 1..19, reads each CGItem's itemID, returns true on first match
against the input (accepts itemID, `item:NNN` link, or chat link).

## ~~28. `C_Item.GetItemCount(itemInfo, [includeBank], [includeUses])`~~ — DONE

Total count of an item across the player's bags (and optionally bank).
Walks bags 0..4 always (live walk via `Item::Location::ResolveBag`).
Stack count read directly from `ITEM_FIELD_STACK_COUNT` at descriptor
+0x20 — verified by decoding `Script_GetContainerItemInfo`
(`0x004F9670`), which does `mov eax, [esi+0x114]; fild [eax+0x20]` for
the count return.

**Bank works cold** — no banker visit required. The 1.12 server sends
bank inventory at login (verified empirically on Turtle WoW: fresh
WoW.exe launch + cleared WDB folder shows GUIDs already populated in
main bank slots 39..62 with the gate at `VAR_BANK_GATE_GUID = 0`).
The engine's `GetItemBySlot` (`0x006228A0`) gates bank slots 39..68
on a non-zero banker-GUID at `0x00BDD038` — set when bank window
opens, cleared when it closes — but the underlying GUID data in
`invMgr+0x04` is always present from session start.

Bank-walk path bypasses the gate by reading GUIDs directly out of
the player invMgr's slot array and resolving each via
`FUN_OBJECT_RESOLVE_BY_GUID = 0x00468460` (same function the engine
itself calls inside `GetItemBySlot`, just without the gate wrapper).
Bank bags (slots 63..68) are followed via the bag's vtable +0x10 to
get its own invMgr, then walked by the same direct-read path. Cold
count of a bank-only item now works from login without ever
approaching a banker — confirmed in-game.

The 1.12 STACK_COUNT field index (0x8) puts it *before* the contained/
creator GUIDs, **opposite** the more commonly documented vanilla
layout (which has STACK_COUNT at index 0xE = +0x38). Trust the
binary: 0x20 is what the engine itself reads, and our test (cross-
checked against a manual `GetContainerItemInfo` walk) confirms it.

`includeUses` reads `ITEM_FIELD_SPELL_CHARGES[0]` (signed dword at
descriptor +0x28) and multiplies each match by `max(abs(charges), 1)`
— so a wand with 50 charges contributes 50 instead of 1, while
non-charge items pass their plain stack count through unchanged. The
offset was derived from the descriptor field-name table builder at
`FUN_0047f840`, which iterates `{ name_ptr, ?, count, ?, ? }` entries
at `0x0083a328` — summing the counts in declaration order puts
SPELL_CHARGES at field index 10 (fields 10..14, +0x28..+0x38).
STACK_COUNT/FLAGS/DURABILITY landing on their known offsets
cross-checks the derivation.

See [src/item/Count.cpp](src/item/Count.cpp).

## ~~29. `C_Item.GetItemFamily(item)`~~ — DONE

Returns the BagFamily bitmask for the item, or `nil` if it isn't in
the cache. Reads `m_bagFamily` at +0x1D0 on the cached `ItemStats_C`
record (offset verified by decoding the engine's own
`GetItemBagFamily(itemID)` helper at `0x005DA050`, which calls
`_GetRecord` then `mov eax, [eax+0x1D0]; ret`).

**Encoding-shift gotcha.** 1.12 stores the raw 1-based BagFamily ID
(`arrow=1, bullet=2, soul shard=3, herb=6, …`); modern WoW (Wrath+)
flipped to the bitmask form `1 << (ID-1)` (`arrow=0x1, bullet=0x2,
soul shard=0x4, herb=0x20, …`). Addons backporting from later
expansions expect the bitmask, so we convert on the way out via
`bitmask = id ? (1 << (id - 1)) : 0`. Verified empirically on Turtle
WoW: Soul Shard (item 6265) reads raw 3 from the cache, returns 4
(`1 << 2`) to Lua callers — matches modern.

**Vanilla data sparsity.** 1.12 server data (at least on Turtle WoW)
populates `m_bagFamily` reliably on **individual loot items** but
leaves it 0 on most **bag containers** themselves. Soul Pouch,
Enchanting Bag, Herb Pouch all return 0 even though their
classification clearly implies a family. Only Light Quiver is
populated among the bags we tested. This means
`GetContainerNumFreeSlots`'s second return is reliable only for
quivers; for general bag categorization, addons should fall back to
checking `(class, subClass)` from `GetItemInfoInstant`. The
`GetItemFamily` *item* path is solid for routing loot to the right
specialty bag.

See [src/item/Info.cpp](src/item/Info.cpp).

## ~~30. `IsSpellKnown(spellID, [isPet])`~~ — DONE

Strict spellbook walk via `Spell::Lookup::FindSpellbookSlot`, filtered
by the requested book type. Returns true only when the spell is in
the player's (or pet's) spellbook UI arrays — does NOT cover talent
passives, profession recipes, or anything else that's "known" but not
displayable as a spellbook button.

### How we caught the bug pre-ship

Initially implemented `IsSpellKnown` using the same bitmap as
`IsPlayerSpell`, on the (wrong) assumption that the two functions
were equivalent in 1.12 since the bitmap was the engine's
authoritative knowledge store. Caught in review — `IsSpellKnown`
is meant to be the stricter check, distinct from `IsPlayerSpell`.

Cross-binary lookup confirmed: 3.3.5's `Script_IsSpellKnown` at
`0x0053C3A0` calls an inner helper at `0x0053B4E0` that does a strict
**spellbook array walk** (`[0x00BE6D88]` for player, `[0x00BE7D98]`
for pet) — not a bitmap. Modern WoW deliberately splits the two
functions: `IsSpellKnown` is the strict "spellbook button" check,
`IsPlayerSpell` (added in 5.0) is the broad "any kind of known"
query. We need both with their respective semantics.

### Behavior table (verified in-game)

| Spell type             | `IsPlayerSpell` | `IsSpellKnown` |
|------------------------|-----------------|----------------|
| Trained class ability  | true            | true           |
| Active talent grant    | true            | true           |
| Passive talent         | true            | **false**      |
| Profession recipe      | true            | **false**      |

Same split as modern WoW. The cross-binary technique paid off again
— caught the wrong implementation before it shipped.

See [src/spell/Info.cpp](src/spell/Info.cpp) and the worked example
in [CLAUDE.md](CLAUDE.md#cross-binary-reference--finding-112-implementations-via-newer-clients).

## ~~31. Unit flag bundle: `UnitIsAFK` / `UnitIsDND` / `UnitIsFeignDeath`~~ — DONE

All three shipped in [src/unit/Flags.cpp](src/unit/Flags.cpp):

- **`UnitIsAFK(unit)`** / **`UnitIsDND(unit)`** — read PLAYER_FLAGS at
  `[unit + 0xE68] + 0x08`, bits 1 (AFK = `0x02`) and 2 (DND = `0x04`).
  Works for any player-controlled unit (local self, target, party,
  raid, inspect targets). Gated on `UNIT_FLAG_PLAYER_CONTROLLED` first
  to avoid the crash hazard on creatures (where `+0xE68` is uninitialized
  garbage).
- **`UnitIsFeignDeath(unit)`** — reads `UNIT_FIELD_FLAGS` bit 29
  (`0x20000000`) at `[m_objectFields + 0xA0]`. Verified in-game with
  a Hunter using Feign Death — the function returns 1 while the
  feign-death buff is active, 0 otherwise.

**Where PLAYER_FLAGS lives — and the misleading first guess.** The
field index `0x8A` (= byte offset `0x228` in m_objectFields) is the
"natural" place to look (matches modern WoW's broadcast UpdateField),
but vanilla 1.12 *doesn't* broadcast PLAYER_FLAGS as a UpdateField —
reading `[descriptor + 0x228]` returns a different field entirely.
The actual storage is in a CGPlayer-side sub-struct at `[unit + 0xE68]`,
shared with the visible-items table at `+0x118`. Verified by
disassembling 1.12's nameplate AFK-prefix renderer at `0x005EC9E0`,
which is the *only* C-level code in stock 1.12 that decorates
nameplates with `<AFK>` — and which works for any unit, contrary to
the initial reading where I thought the JNE went the other way.

**Vanilla nameplate display.** Stock 1.12 shows `<AFK>` over **any**
nearby player's head, not just yourself — the function at `0x005EC9E0`
checks `[unit + 0xE68] + 0x08` bit 1 universally and only special-cases
the local player when a global at `[0x00B6E5CC]` is set (probably a
"hide my own AFK indicator" preference toggle). Verified in-game
on a non-Turtle stock 1.12.1 server.

## ~~32. `IsCurrentSpell(spellID)`~~ — DONE (as `C_Spell.IsCurrentSpell`)

Shipped in [src/spell/IsCurrent.cpp](src/spell/IsCurrent.cpp) under
the modern `C_Spell.IsCurrentSpell(spellIdentifier)` name. Checks
three slots: `VAR_CURRENT_CAST_SPELL` (`0x00CECA88` — cast bar),
`VAR_QUEUED_CAST_SPELL` (`0x00CECAA8` — mid-GCD queue), and the
player's `UNIT_FIELD_CHANNEL_SPELL` (`+0x228` — channels). Accepts
the full spellIdentifier shape (number / link / known-spellbook
name) via `Spell::Arg::ResolveSpellID`.

## 33. `C_Reputation.*` cluster — partial DONE

Modern table-shape and ID-based variants of the existing reputation
API. Most are thin wrappers around what we already have — useful for
addons backporting modern Lua-side code that prefers the C_Reputation
namespace.

- ✅ **`C_Reputation.SetWatchedFactionByID(factionID)`** — DONE.
  Calls the engine's inner watched-faction setter at
  `FUN_PLAYER_SET_WATCHED_FACTION = 0x004D6240` (`__fastcall(ecx =
  factionID) → void`) directly, bypassing
  `Script_SetWatchedFactionIndex`'s displayed-index round-trip.
  Works for unencountered factions too (the rep bar shows nothing
  until the player gains rep, then displays normally). factionID 0
  clears. Verified: Darnassus → 72 (Stormwind) → 0 (clear) all
  reflected by `GetWatchedFactionInfo()` immediately. Lives in
  `src/faction/Info.cpp`.
- **`C_Reputation.SetWatchedFactionByIndex(index)`** — modern alias
  for `SetWatchedFactionIndex`. One-line registration that points to
  the engine function via our existing wrapper.
- **`C_Reputation.GetFactionDataByIndex(index)`** /
  **`GetFactionDataByID(factionID)`** — table-returning variants of
  `GetFactionInfo` / `GetFactionInfoByID`. Pack the existing 11-tuple
  into named fields (`name`, `description`, `reaction`, `barMin`,
  `barMax`, `currentStanding`, `atWarWith`, `canToggleAtWar`,
  `isHeader`, `isCollapsed`, `hasRep`, `factionID`).
- **`C_Reputation.GetWatchedFactionData()`** — table form of
  `GetWatchedFactionInfo` (which 1.12 has natively). The watched
  factionID is stored at `[player + 0xE68] + 0x10C4` per the
  inner setter's compare-and-skip path — useful if we add a
  `IsWatchedFaction(id)` getter too (engine has one at
  `0x004D62F0` already).

## ~~34. `C_DateAndTime.GetSecondsUntilDailyReset()`~~ — DONE (see #76)

Shipped as part of the full `C_DateAndTime.*` backport — see #76.
Uses `86400 - (Time::Server::CurrentEpoch() % 86400)` for server-
midnight semantics.

## ~~35. `GetPlayerInfoByGUID(guid)`~~ — DONE

Modern 7-tuple `localizedClass, englishClass, localizedRace,
englishRace, sex, name, realm` for any GUID the engine has cached.
Implementation in [src/player/Info.cpp](src/player/Info.cpp).

### What we ended up using

The original investigation thought the engine's NameCache at
`0x00CE870C` was the only client-side cache, and that it was fed
only by visible objects (which would mean offline friends/guildies
miss). Re-investigation with Ghidra MCP found a **different** cache
— a dedicated player-info cache at `VAR_PLAYER_NAME_CACHE`
(`0x00C0E228`) with lookup helper `FUN_PLAYER_INFO_LOOKUP`
(`0x0055F080`). This one **is fed by SMSG_NAME_QUERY_RESPONSE**
(opcode `0x51` handler at `0x005551A0`), which the engine
auto-fires for any GUID surfacing in chat / raid / guild / etc.

Cache hits give us name@+0x00, realm@+0x30, race@+0x138,
sex@+0x13C, class@+0x140. Race goes through `ChrRaces.dbc`
records (instance `0x00C0DEE0` / count `0x00C0DEE4`,
`Filename@+0x3C` for englishRace and `Name[locale]@+0x44` for
localizedRace). Class similar via `ChrClasses.dbc`.

Sex value: engine stores 0/1, modern Lua API returns 2/3 — we
add 2 to match.

### Coverage

Anyone who's shown up in chat, your raid/party, your guild, or as
a visible nameplate will be in the cache. Cold-target lookups for
"never seen this character" return `nil`. The earlier-investigated
"Path B" (active CMSG_NAME_QUERY firing for cache misses) wasn't
needed because the existing cache populates from passive traffic
broadly enough that the typical "addon wants info for someone in
chat" case Just Works.

### Refactor side effect

`Race::EnglishRaceForID` and the class-string maps in this module
read from DBCs at runtime rather than hardcoding the strings — so
custom-server race additions (Turtle's Goblin/etc.) are picked up
without code changes.

## ~~36. `GetTalentSpellID(tabIndex, talentIndex[, rank])`~~ — DONE

Bridges the talent system to the spell system: once you have the
spellID, every spell API chains naturally (`GetSpellInfo`,
`C_Spell.GetSpellDescription`, `GameTooltip:SetSpellByID`,
`IsPassiveSpell`, `C_Spell.GetSpellLink`, …). 1.12's `GetTalentInfo`
returns `(name, icon, tier, column, currentRank, maxRank, ...)` — no
spellID — so addons used to have to maintain their own
`(class, tab, idx) → spellID` lookup tables.

Reads from the engine's per-tab talent arrays at `[0x00BDCD28]`
(populated at login from `Talent.dbc` filtered by the player's class),
not Talent.dbc directly. Each `TabInfo *` exposes:

- `+0x00` TalentTab.dbc rowID
- `+0x04` pointsSpent
- `+0x08` numTalents
- `+0x0C` `TalentEntry *` array (stride `0x54`)

Each `TalentEntry`:

- `+0x00` talentID (Talent.dbc primary key)
- `+0x10..+0x33` `SpellRank[9]` (rank-N spellID at index N-1; vanilla
  uses 0..4, higher slots stay zero)

When `rank` is omitted, default = player's currentRank, derived by
delegating to `Script_GetTalentInfo` and reading its 5th return —
piggybacking on the engine's spell-knowledge logic. If currentRank is
0, falls back to rank 1.

### Two gotchas worth noting

1. **The engine's currentRank loop walks SpellRank backwards** — `lea
   edx, [ebx+0x30]` then `sub ecx, 4` per iteration, ending at +0x10.
   I initially read +0x30 as the start of `SpellRank[]`; it's actually
   the END (slot 9, always zero in vanilla). Confirmed empirically by
   a debug build returning 0 from +0x30 vs the right spellID from
   +0x10.

2. **Stock 1.12.1's `GetTalentInfo` returns 8 values, not the 6 I
   expected** based on naive disassembly. Reading the 5th return uses
   `stack[-(n - 4)]` (dynamic from the actual return count) instead of
   a hardcoded `stack[-2]`.

See [src/talent/Info.cpp](src/talent/Info.cpp) and the talent block
(`VAR_TALENT_TAB_*` / `OFF_TABINFO_*` / `OFF_TALENT_SPELL_RANK`) in
[src/Offsets.h](src/Offsets.h).

## ~~37. `GetSpellSchool(spellID)`~~ — DONE

Shipped as [src/spell/School.cpp](src/spell/School.cpp). Returns
`(schoolID, schoolName)` where schoolID is 1-based (1=Physical,
7=Arcane) and schoolName is the locale-independent English name.

### Spell.dbc School field — VERIFIED OFFSET

Vanilla 1.12 stores School as a **0-based integer at record `+0x04`**
(NOT the SchoolMask form the original TODO note speculated — that's
TBC+). Verified empirically via the probe Lua functions:

```
/dump _classicapi_FindSpellField(133, 116, 4, 16)   -- mask form: empty
/dump _classicapi_FindSpellField(133, 116, 2, 4)    -- 0-based int: offset 4 ✓
/dump _classicapi_FindSpellField(133, 116, 3, 5)    -- 1-based int: empty
```

Fireball (133) → School=2 (Fire); Frostbolt (116) → School=4 (Frost).

The probe helpers (`_classicapi_ProbeSpellRecord`,
`_classicapi_FindSpellField`) were temporarily exposed to derive the
offset and removed in the same commit that shipped `GetSpellSchool`.

## ~~38. `GetSpellRadius(spellID)`~~ — DONE

Shipped in [src/spell/Radius.cpp](src/spell/Radius.cpp) as
`C_Spell.GetSpellRadius(spellID)` plus the vanilla-positional
`GetSpellRadius(slot, bookType)`. Returns `(baseRadius, modifiedRadius)`,
or `nil` for non-AOE spells. `modifiedRadius` applies the local player's
talent/item radius mods (e.g. Arctic Reach) via the shared `Spell::Mod`
helper — verified in-game: Frost Nova (10230) → `10, 10` without Arctic
Reach, `10, 12` with rank 2 (+20%).

The radius is **per-effect**, not a single spell-level field: `Spell.dbc`
has `EffectRadiusIndex[3]` at record `+0x160` (verified by decompiling
`FUN_006e6350`, which reads `spellRec[+0x160]`/`[+0x164]` for effects
0/1). Each non-zero index resolves into `SpellRadius.dbc` (records
`0x00C0D7B0`, count `0x00C0D7B4`), record shape
`{ id@+0, radius@+4 (f32), radiusPerLevel@+8 (f32), ... }`. We return the
largest base radius across the three effects. Base radius only — the
engine adds `radiusPerLevel * casterLevel`, but that needs a unit context
and modern `GetSpellRadius` reports the base value.

See `OFF_SPELL_RECORD_EFFECT_RADIUS_INDEX`, `VAR_SPELL_RADIUS_RECORDS`,
`OFF_SPELL_RADIUS_VALUE` in [src/Offsets.h](src/Offsets.h).

## ~~39. `GetFactionParentID(factionID)`~~ — DONE

Reads `Faction.dbc` `ParentFactionID` at record `+0x48` via the
existing `Faction::Info::FactionRecord` helper. Returns `0` for
top-level factions, `nil` for invalid IDs. Verified in-game:
`GetFactionParentID(72)` → 469 (Stormwind → Alliance Forces),
`GetFactionParentID(469)` → 0 (Alliance is top-level),
`GetFactionParentID(99999)` → nil.

See [src/faction/Info.cpp](src/faction/Info.cpp) and
`OFF_FACTION_PARENT_ID` in [src/Offsets.h](src/Offsets.h).

## ~~40. `UnitRaceBase(unit)`~~ — DONE

Shipped as sibling to `UnitClassBase` in
[src/unit/RaceBase.cpp](src/unit/RaceBase.cpp). Reads byte 0 of
`UNIT_FIELD_BYTES_0` (descriptor `+0x78`) and looks up
`ChrRaces.dbc::Filename`. Returns `(raceFile, raceID)`.

## 41. `GetActiveTalentGroup()` / `GetCurrentSpec()` — easy

Vanilla has no formal "spec" system but every class has 3 talent tabs
and the player always invests in one primarily. Returns the
1-based tab index (1, 2, or 3) of the tab with the most points spent,
or `0` if the player has no points spent at all (low-level
characters, or after a respec).

Trivial implementation: walk `GetNumTalentTabs()` (= 3), call
`GetTalentTabInfo(tab)` for each, compare `pointsSpent`. Pick max.
No DBC reads, no engine internals — pure Lua-equivalent computation
that's just easier to call than write inline every time.

Modern WoW's `GetSpecialization()` returns 1..4 for the player's
active spec. This is the closest 1.12 analog, useful for class
addons that want to render different UI per spec ("am I tanking?
healing? DPS?") without having to do the talent-counting dance
themselves.

## ~~42. `GetTalentIDByIndex(tabIndex, talentIndex)`~~ — DONE

Reads `TalentEntry+0x00` from the per-tab talent arrays. The
talentID-at-+0x00 offset was already confirmed empirically during
the GetTalentSpellID debug pass (`entryFirstDword=174` matched
Inner Focus's row ID), so this was a 5-line addition piggybacking
on the existing struct walk. Verified in-game:
`GetTalentIDByIndex(1, 9)` → 174,
`GetTalentIDByIndex(1, 1)` → 166.

Unblocks #43 `GameTooltip:SetTalentByID` whenever someone wants the
modern by-ID tooltip dispatcher.

See [src/talent/Info.cpp](src/talent/Info.cpp).

## ~~43. `GameTooltip:SetTalentByID(talentID)`~~ — DONE

Shipped as [src/talent/Tooltip.cpp](src/talent/Tooltip.cpp). Works
for **any class's** talents, not just the player's. Two-tier
resolution:

1. **Player-class fast path** — walks the local player's TabInfo
   arrays for a matching talentID (same arrays #42 reads from,
   inverted). On hit, dispatches to `Script_GameTooltip_SetTalent`
   (registry slot 34, `0x00535170`) with the resolved `(tab, idx)`.
   Renders the full talent tooltip with rank counter, prereqs, and
   the "click to learn next rank" prompt.
2. **Cross-class fallback via `Talent.dbc`** — vanilla 1.12 only
   loads the local player's class talent data into TabInfo, so
   other classes' talents aren't findable in tier 1. Tier 2 looks
   up the talent record directly in `Talent.dbc` (records pointer
   `0x00C0D6E8`, count `0x00C0D6EC`, indexed by talentID), reads
   the rank-1 spellID at `+0x10`, and dispatches the spell tooltip
   via the same `Spell::Tooltip::ShowByID` helper that
   `SetSpellByID` uses. Renders the spell info (cast time, range,
   mana cost, description) without talent-specific decorations
   (rank counter, prereq lines).

Modern WoW's cross-class tooltip is slightly richer (talent name +
"Rank 0/N" header before the spell description). Building that
would require Lua-level `GameTooltip:AddLine` work; deferred since
the spell tooltip already answers "what does this talent do?".

### Refactoring done as part of this

Extracted `Spell::Tooltip::ShowByID(L, spellID)` into
[src/spell/Tooltip.h](src/spell/Tooltip.h) so the resolve-self +
`BuildSpellTooltip` chain is shared between
`GameTooltip:SetSpellByID` and the cross-class fallback in
`SetTalentByID`. Saves duplicating the
`FrameScript_PushObject`/`GetObject` inline-asm sequence.

### `Talent.dbc` record layout (verified standard vanilla schema)

| Offset | Field |
|--------|-------|
| `+0x00` | `uint32 ID` |
| `+0x04` | `uint32 TabID` |
| `+0x08` | `uint32 TierID` (row, 0-based) |
| `+0x0C` | `uint32 ColumnIndex` (0-based) |
| `+0x10` | `uint32 SpellRank[0..8]` — rank-1 spellID at `+0x10` |

The per-player `TalentEntry` (in `TabInfo->talents[]`) shares the
same `SpellRank` offset, so we reuse `OFF_TALENT_SPELL_RANK = 0x10`
for both. The `TalentEntry` stride (`0x54`) and the Talent.dbc
record stride differ — Talent.dbc records may be larger — but for
our purpose only the rank-1 spellID at `+0x10` matters.

### Verified in-game

Both tiers tested — own-class talents render the full talent
tooltip, cross-class IDs render the corresponding spell tooltip.

## ~~44. `GameTooltip:SetItemByID(itemID)`~~ — DONE

snprintf the hyperlink string and dispatch to the existing
`Script_GameTooltip_SetHyperlink`. Same registration pattern as
`SetSpellByID`.

### Caching gotcha caught in testing

Initial test showed only the item *name*, not the full tooltip, for
arbitrary itemIDs. Cause: 1.12 lazy-loads item data into the
client-side cache (the same cache `C_Item.GetItemInfoInstant` reads).
Items the player has never encountered show only what's in the link
itself (the name) until the cache is populated via
`SMSG_ITEM_QUERY_SINGLE_RESPONSE`. This isn't a bug in our
implementation — it's the same behavior as modern's `SetItemByID`,
which also requires `C_Item.RequestLoadItemDataByID` first for
arbitrary IDs.

Documented the caveat in [docs/API.md](docs/API.md) with the
recommended `IsItemDataCachedByID` / `RequestLoadItemDataByID` /
`ITEM_DATA_LOAD_RESULT` pattern callers should follow.

See [src/item/Tooltip.cpp](src/item/Tooltip.cpp).

## ~~45. `GameTooltip:SetUnitAura(unit, index, [filter])`~~ — DONE

Pure dispatch wrapper. Branches on filter string (`"HARMFUL"` →
`SetUnitDebuff`, anything else → `SetUnitBuff`) and forwards via
the existing script function. `filter` defaults to helpful when
omitted, matching modern.

See [src/unit/Tooltip.cpp](src/unit/Tooltip.cpp) and the
`FUN_SCRIPT_GAMETOOLTIP_SET_UNIT_*` block in
[src/Offsets.h](src/Offsets.h).

## 46. `GameTooltip:SetFrameStack([showHidden])` — deferred (Lua-side fix in place)

Renders a tooltip listing the chain of frames at the mouse cursor,
sorted by strata + level. The 3.0+ debug-tooling primitive used by
Blizzard's `Blizzard_DebugTools` and every layout-debugging addon.

### Current state in this project

Our bundled `DebugTools/` backport ships its own `FrameStackTooltip`
in [DebugTools/DebugTools.lua](DebugTools/DebugTools.lua), driven
by `_CollectFramesUnderMouse`. As of the EnumerateFrames switch,
the iterator walks the engine's own frame list — fast (typically
~hundreds of entries) and includes anonymous frames the engine
sees but a `_G` walk would miss.

What's still missing relative to the real engine `SetFrameStack`:

- **Anonymous-frame display name.** We currently show
  `<anonymous>` for frames where `frame:GetName()` returns nil.
  The engine's method shows the frame's pointer + the Lua-side
  registry-table address. To get there we'd need a Lua-callable
  shim that exposes a numeric handle per frame (vftable + Lua-ref
  lookup), which is non-trivial.
- **Single-tooltip semantics.** Modern addons can call
  `GameTooltip:SetFrameStack(...)` directly on any tooltip;
  ours requires the `FrameStackTooltip` we ship in `DebugTools/`.
  Most consumers (`Blizzard_DebugTools` ports, layout addons via
  `/framestack`) don't care, but a strict polyfill would.

### C++ port from 3.3.5 — kept as a research note

What `Script_GameTooltip_SetFrameStack`
actually does in 3.3.5 (Frostmourne client, registration entry at
VA 0x00AD2CF0 → function VA 0x00626560 → inner builder
`FUN_006230d0`):

- Pulls cursor (in frame space) via `FUN_0047BFF0` from a render-
  state global at `[DAT_00B499A8 + 0x1224 / +0x1228]`.
- Walks an intrusive linked list at `[DAT_00B499A8 + 0xCD4]`. Each
  frame node reads:
  - `[+0x00]` — vftable; `vftable[+0x10]` = "is the frame visible
    enough to hit-test" predicate.
  - `[+0x0E0]` (`node[0x38]`) = strata; gates `(strata != 0 ||
    showHidden)`.
  - `[+0x0D0]..[+0x0DC]` (`node[0x34..0x37]`) = frame rect for
    hit-test.
  - `[+0x0D8]` (`node[0x36]`) flags, bit 2 → visible.
  - `[+0x288]` (`node[0xA2]`) = next pointer.
- Per-frame name via `vftable[+0x04]` (a `GetDebugName()`-style
  virtual). If the frame is Lua-side (`vftable[+0x10]` returns
  type code 5), an extra `lua_rawgeti(L, REGISTRY, frame->luaRef)`
  pulls the Lua-table reference for the `"%s: %p (%s)"` format;
  otherwise it's `"%s: %p"`.
- Builds into a scratch sort array at `DAT_00C5D370` (entry stride
  `0x410`), `qsort` against comparator `FUN_0061A5B0`.
- Header line via `FUN_00819D40("DEBUG_FRAMESTACK", ...)` (Lua
  `GetGlobalString`) then per-entry `FUN_0061FEC0` (tooltip
  `AddLine` equivalent, takes color codes from a 4-byte-stride
  table at `0xAD2DC0` / `0xAD2DB8` / `0xAD2DB0` for
  visible/hidden/special states).

For a 1.12 port we'd need to derive:

- The frame-manager global (3.3.5's `DAT_00B499A8` — different VA
  in 1.12, different field offsets likely).
- Frame struct offsets for rect, strata, flags, next, name vftable
  slot — the layout shifted between 1.12 and 3.3.5 (same way unit
  field offsets did, per the `OFF_UNIT_FIELD_HEALTH` discovery in
  CLAUDE.md).
- AddTooltipLine engine address (1.12 has it — every
  `GameTooltip:SetX` method calls it internally).
- Lua-side reference lookup helper (whatever 1.12 uses to map
  frame → Lua table; the `FrameScript_GetObject` chain at
  `0x6F3740` is part of this).

### Recommendation

The Lua-side iterator switch is done. The C++ port stays deferred
unless we need the engine frame list for something else
(`GetMouseFocus` polyfill, anonymous-frame inspector with extra
fields beyond what Lua exposes, single-tooltip strict polyfill,
etc.) that would amortize the RE cost.

## ~~47. `IsPlayerSpell(spellID)`~~ — DONE

Single bitmap lookup at `[VAR_PLAYER_SPELL_BITMAP] = [0x00B710FC]`.
The engine maintains a dword bitmap with one bit per spellID — set
when the player learns a spell (any source: training, talent
investment, profession recipe, racial unlock, etc.) and cleared on
unlearn. We just consult the same bit the engine itself reads via
the helper at `0x0060C740`:

```cpp
return (bitmap[spellID >> 5] & (1 << (spellID & 31))) != 0;
```

### How we found it

The naive walk-based implementation (originally drafted) failed for
profession recipes — turns out vanilla 1.12's `VAR_PLAYER_SPELLBOOK`
arrays DON'T contain profession recipe spellIDs (verified
empirically: a tailor with Bolt of Linen Cloth fails
`FindSpellBookSlotByID(2963)`). Surfaced during in-game testing.

To confirm whether 1.12 had a unified spell-knowledge data structure,
I dumped 5.4.8's `Script_IsPlayerSpell` (5.4 = the version where the
function was first added) and saw a clean bitmap pattern:

```
mov ecx, eax
and ecx, 0x1F            ; bitPos = spellID & 31
shl edx, cl              ; mask = 1 << bitPos
mov ecx, [imm32]         ; bitmap base ptr
shr eax, 5               ; idx = spellID / 32
and eax, [ecx + eax*4]   ; bitmap[idx] & mask
```

Searched 1.12 for the same pattern → matched at the helper
`0x0060C740`, which `Script_GetTalentInfo` calls for currentRank
derivation. Same shape, same purpose, just at a different bitmap
address (`[0x00B710FC]`).

### Modern-parity semantics

Verified against 1.15.x: only the player's currentRank spellID has
its bit set; lower-rank IDs return false. A 5/5 Unbreakable Will
player sees `IsPlayerSpell(14791)` (rank 5) = true but rank 4 / 3 /
2 / 1 = false — same in 1.12 with our implementation, same in 1.15.

This means our implementation matches modern exactly, including the
"only current rank counts" quirk. Addons backporting from later
expansions that key on currentRank's spellID work unmodified.

### Spillover wins for the rest of TODO

The same bitmap makes #30 `IsSpellKnown(spellID, isPet)` trivial —
ship it as a thin wrapper using the same lookup (or split: player
scan via bitmap, pet via existing pet spellbook walk). The
`Spell::Lookup::FindSpellbookSlot` walk is now only needed for the
slot-and-bookType return shape, not for knowledge queries.

See [src/spell/Info.cpp](src/spell/Info.cpp) and
`VAR_PLAYER_SPELL_BITMAP` in [src/Offsets.h](src/Offsets.h).

## ~~48. `GetSpellCooldown(spellID)` overload~~ — DONE (via `C_Spell.GetSpellCooldown`)

Shipped as the modern table-shape variant in
[src/spell/Cooldown.cpp](src/spell/Cooldown.cpp): `C_Spell.GetSpellCooldown(spellIdentifier)`
accepts number / name / name(rank) / `|Hspell:N|h` link and returns
a `SpellCooldownInfo` table. The original goal — "query cooldowns by
spellID without forcing the caller to resolve a spellbook slot first"
— is covered.

Reads the engine's per-spell cooldown manager at
`FUN_SPELL_QUERY_COOLDOWN` (`0x006E2EA0`), which `Script_GetSpellCooldown`
also goes through internally after its slot-resolution step. Fixed
the `(*outDuration, *outStart)` argument order while doing this —
the typedef in `Spell::Usable` had them reversed (worked anyway
because both fields are 0 off-cooldown, but the var names were
backward).

## ~~49. `IsUsableSpell(spell)` / `C_Spell.IsSpellUsable(spellID)`~~ — DONE

Both shipped. `IsUsableSpell` accepts spellID or `(slot, bookType)`
via the same `Spell::Lookup::SpellbookSlotToID` resolver other
spell-API functions use. Returns `(1, nil)` / `(nil, 1)` /
`(nil, nil)` per legacy convention. `C_Spell.IsSpellUsable` is the
modern shape — same logic, returns proper booleans.

**Two engine-cache approaches investigated and discarded:**

1. **Per-spell cache at `0x004F0F40` returning fields at +0x564
   / +0x568** — initially looked promising (the action-bar
   dispatcher at `0x004E5BA0` reads these), but the writers
   (`0x004EFF17` cluster) showed they're parsed config integers,
   not real-time usability bools. False trail.
2. **Action-bar per-slot caches at `0x00BC67A0` (noMana) and
   `0x00BC6B60` (usable)** — these ARE real-time, but only updated
   for the 120 action-bar slots. Spells off the bar stay
   uninitialized. Action-bar-only is the wrong abstraction since
   modern `IsUsableSpell(spellID)` is action-bar-agnostic.

Settled on a **manual five-check approach**:
1. Player knows the spell (`VAR_PLAYER_SPELL_BITMAP` lookup —
   covers profession recipes, talents, racials, etc.).
2. Player is alive (`HEALTH > 0`).
3. Spell is not on cooldown (`FUN_SPELL_QUERY_COOLDOWN` —
   the engine helper `Script_GetSpellCooldown` calls internally
   after slot resolution, queried with `bookType=0` for player).
4. Player has ≥ `ManaCost` of the spell's `PowerType` —
   only this failure flips `noMana=true`.
5. Player has all required reagents in bags
   (`Spell.dbc` Reagent[8] / ReagentCount[8] at `+0x110` /
   `+0x130`, walked via `Item::Location::ResolveBag`).

**Not checked** (deliberate, different concerns): silence, GCD,
stance/form, range, target type, line-of-sight, casting state.

**Verified on Turtle WoW:**
- Mana check: Renew rank 3 (cost 105) reports usable at 144 mana
  and unusable at 39 mana, transitioning exactly at the cost
  boundary.
- Reagent check: SHIPPED BUT UNVERIFIED — the Reagent[8] /
  ReagentCount[8] offsets at `+0x110` / `+0x130` are the
  CMaNGOS-documented vanilla layout, and we know spell records
  match CMaNGOS docs (PowerType=+0x7C and ManaCost=+0x80 both
  match), so high confidence — but no in-game verification yet.
  Should test with a reagent spell (e.g., Mage Teleport spell 3565
  needs Rune of Teleportation 17031). Failure mode if offsets are
  wrong: reagent check becomes a no-op (always passes).
- Cooldown check: SHIPPED BUT UNVERIFIED in this implementation —
  the helper signature was traced via `Script_GetSpellCooldown` at
  `0x004B40A0` but not exercised in-game. Should test with a
  cooldown'd spell.

**Important offset correction along the way.** Initial implementation
read POWER1 at descriptor `+0x5C` based on the CMaNGOS-documented
field index `0x17`. That was *max* mana — current was at `+0x44`
(field index `0x11`, six fields earlier than the standard layout).
The whole UNIT_FIELD layout in 1.12.1 is shifted 0x18 / 6 fields
earlier than CMaNGOS documents:

| Field         | CMaNGOS docs | 1.12.1 actual |
|---------------|-------------:|--------------:|
| HEALTH        | +0x58        | +0x40         |
| POWER1 (mana) | +0x5C        | +0x44         |
| POWER2..5     | +0x60..+0x6C | +0x48..+0x54  |
| MAXHEALTH     | +0x70        | +0x58         |
| MAXPOWER1..5  | +0x74..+0x80 | +0x5C..+0x6C  |
| LEVEL         | +0x88        | +0x70         |
| FLAGS         | +0xA0        | +0xA0 ✓       |

FLAGS still lands at +0xA0 (verified separately by
`Script_UnitPlayerControlled` and `Script_UnitAffectingCombat`).
Trust the binary, verify with `_classicapi_DescDump` if you need
to derive new ones.

See [src/spell/Usable.cpp](src/spell/Usable.cpp).

## 50. Cross-binary technique applications worth picking up — meta

The `IsPlayerSpell` discovery proved out the cross-binary disassembly
technique (documented in [CLAUDE.md](CLAUDE.md#cross-binary-reference--finding-112-implementations-via-newer-clients)).
Other pending TODOs that look like good fits for the same approach:

- **#7 `UnitAuraBySlot` / `UnitAuraSlots`** — modern slot-based aura
  iteration. Aura state is in CGUnit's UpdateField data; modern
  `UnitAura` reads slot offsets we can decode from 5.4.8 and use in
  1.12.
- **#32 `IsCurrentSpell(spellID)`** — runtime cast-state global. The
  engine has a "currently casting spellID" global (the cast-bar UI
  reads it). Modern's `IsCurrentSpell` reads the same value.
- **#42 `GetTalentIDByIndex`** — already easy with the talent struct
  we now know (talentID at TalentEntry+0x00); cross-binary not needed
  here but could verify against 5.4.8's `GetTalentInfoByID` for
  semantics.

Tag here so we remember the technique exists when picking up these
items.

## ~~51. `C_Container.GetContainerNumFreeSlots(bagID)`~~ — DONE

Returns `(numberOfFreeSlots, bagType)` for the player's backpack
(`bagID = 0`) or one of the four equipped bag slots (`bagID = 1..4`).
`bagType` is the BagFamily bitfield — vanilla 1.12 has only three
non-zero families (quiver=1, ammo pouch=2, soul shard bag=4); modern
expansions added 20+ more, but reading the bitfield gives addons a
forward-compatible shape regardless. Always returns two values, falling
back to `(0, 0)` for unsupported bag IDs (bank/keyring/out-of-range) —
matches 3.3.5's behavior.

Implementation walks the bag and counts empties via
`Item::Location::ResolveBag`. Bag metadata (slot count + BagFamily) for
real bags 1..4 comes from the equipped bag's `ItemStats_C` cache
record: `m_containerSlots` at +0x64, `m_bagFamily` at +0x1D0. Backpack
short-circuits to `(16, 0)` since vanilla's main backpack is fixed-size
and unfamilied. Field offsets derived from VanillaHelpers's
`struct ItemStats_C` layout, cross-checked against the existing
`m_stackable` at +0x60 (already in `Offsets.h`).

The slot-walk uses ResolveBag, which clobbers Lua stack[1]/[2] each
call (a requirement of the engine's `PackBagSlot`). That's safe here —
we own the stack inside the callback and the user's bagID arg is
copied to a local before the loop. Cross-checked in-game against a
manual `GetContainerItemID` walk; counts match for all five bags.

**bagType return is sparse on vanilla data.** Empirically, only Light
Quiver carries a non-zero `m_bagFamily` field among the bags we tested
on Turtle WoW; Soul Pouch, Enchanting Bag, Herb Pouch all return 0.
The same field on individual items (arrows, shards, herbs) is
reliable — see TODO #29's notes. Addons that need bag-category
discrimination should look at `(class, subClass)` from
`GetItemInfoInstant` rather than `bagType`. The conversion from
1.12's raw-ID encoding to the modern bitmask
(`1 << (ID-1)`) still applies for the rare populated cases.

See [src/item/Bag.cpp](src/item/Bag.cpp).

## ~~52. `C_Container.PlayerHasHearthstone()` / `C_Container.UseHearthstone()`~~ — DONE

Modern convenience pair for the most-used bag walk in addons. Walks
bags 0..4 looking for the hearthstone (vanilla itemID 6948 — the only
one in 1.12; modern WoW recognizes many hearthstone-toy itemIDs but
none exist in this client). Stops on first match.

`PlayerHasHearthstone()` returns the itemID (always 6948 or nil).
`UseHearthstone()` invokes the engine's `Script_UseContainerItem`
(`0x004FA0E0`) with `(bagID, slot)` set up on the Lua stack — same
secure-action path a regular `UseContainerItem(bag, slot)` Lua call
would take, so cooldown / combat / movement-cancel handling is
identical. Returns `true` if a hearthstone was found and the call
dispatched, `false` otherwise (the cast itself can still fail
downstream — same caveat as manually calling `UseContainerItem`).

Walking helper is shared between the two via `FindHearthstone(L,
*outBag, *outSlot)`. Slot counts come from delegating to the engine's
own `Script_GetContainerNumSlots` (`0x004F9560`) rather than a
hardcoded vanilla cap, so custom servers with larger-than-24-slot
bags work without code changes.

See [src/item/Hearthstone.cpp](src/item/Hearthstone.cpp).

## ~~53. `LE_EXPANSION_*` constants~~ — DONE

Twelve expansion-level enum globals (`LE_EXPANSION_CLASSIC` through
`LE_EXPANSION_MIDNIGHT`) plus `LE_EXPANSION_LEVEL_CURRENT`, all
matching the canonical retail `Enum.ExpansionLevel` table values.
On 1.12, `LE_EXPANSION_LEVEL_CURRENT` resolves to
`LE_EXPANSION_CLASSIC` (= 0). Lets addons backported from later
expansions use the modern `if LE_EXPANSION_LEVEL_CURRENT < ...`
gating idiom without conditionally defining the constants
themselves.

Set via the same Lua C API path `CLASSIC_API_VERSION` uses (no
`FrameScript_Execute` source). One per entry, written to
`_G[name] = value` from the module's `RegisterLuaFunctions` hook
which fires after the engine's `LoadScriptFunctions` boot phase.

See [src/expansion/Constants.cpp](src/expansion/Constants.cpp).

## ~~54. `C_AddOns.*` namespace — convenience wrappers~~ — DONE (Tier 2)

Modern WoW (10.x) moved most addon API into the `C_AddOns` namespace.
1.12 already exposes most of the underlying functions as globals;
we deliberately do *not* duplicate them under `C_AddOns` (Tier 1 in
the prior version of this entry — pure namespace mirror, low value).
What's worth adding is the post-vanilla *new* functionality.

### Tier 1 — DROPPED

Bulk wrapping of 17 existing 1.12 globals (`IsAddOnLoaded`,
`LoadAddOn`, etc.) under `C_AddOns.*`. Skipped — addons that need
the namespace can `C_AddOns = C_AddOns or {} ; C_AddOns.IsAddOnLoaded
= IsAddOnLoaded` themselves in two lines.

### ~~Tier 2 — convenience wrappers around `GetAddOnInfo`~~ — DONE

Shipped as `src/addons/Info.cpp`. Six wrappers, all bypassing
`Script_GetAddOnInfo` and reading directly from the engine's
addon-registry helpers we found at `0x0051DF00..0x0051E050`:

- `C_AddOns.GetAddOnName(indexOrName)` — direct via `GetByIndex`
  (numeric: returns the entry's inline name buffer; string:
  echoes the input after validating existence).
- `C_AddOns.GetAddOnTitle(indexOrName)` — calls
  `GetTitleByName` (`0x0051DF20`) directly.
- `C_AddOns.GetAddOnNotes(indexOrName)` — calls
  `GetNotesByName` (`0x0051E050`) directly.
- `C_AddOns.IsAddOnLoadable(arg [, character, demandLoaded])` —
  returns `(loadable, reason)`. Still dispatches through
  `Script_GetAddOnInfo` (5th + 6th return) because the engine's
  per-field accessor is buried behind a locale-derived field key.
  Gated on a `ResolveAddOnName` existence check first to avoid
  the `lua_error` `Script_GetAddOnInfo` raises for out-of-range
  numeric indices.
- `C_AddOns.GetAddOnSecurity(indexOrName)` — same dispatch
  shape as `IsAddOnLoadable`.
- `C_AddOns.DoesAddOnExist(indexOrName)` — direct via
  `ResolveAddOnName`; correctly returns `false` for unknown
  names rather than the dispatch-based heuristic which
  false-positived on garbage strings (engine echoes the input
  back as ret1 even on miss).

The underlying `FUN_ADDON_GET_BY_INDEX` / `_TITLE_BY_NAME` /
`_NOTES_BY_NAME` accessors share a hash-table walk: they take a
char* name (entry pointer doubles as a name string thanks to
the inline 12-byte buffer), hash it to find the registry entry,
then walk a per-entry metadata hash table keyed by the field
name (`"Title"`, `"Notes"`) to extract the value. We piggyback
on the same name → entry lookup for all wrappers.

Notable bug caught in testing: the original implementation used
`PushValue` + `Insert(L, 1)` + `SetTop(L, 1)` to pull the
desired return down to the bottom of the stack. That combo
silently returned the LAST pushed value of `Script_GetAddOnInfo`
regardless of `returnPos` — same family of broken-stack-state
issues as the `RegisterTableFunction` gotcha in CLAUDE.md.
Replacing with `SetTop(L, 1 + returnPos)` (truncate to keep
the desired return on top, return 1 to Lua) fixed it.

### Tier 3 — research / new work

- **`GetAddOnInterfaceVersion(index)`** — reads `## Interface:` from
  the .toc. The engine parses this at addon-load time; we'd need
  to find where it stores the per-addon value. (Note: vanilla also
  exposes `GetAddOnMetadata(name, "Interface")` natively, which
  may already cover this — verify before implementing.)
- **`GetAddOnLocalTable(index)`** — NOT FEASIBLE in 1.12. The
  modern API returns the addon's private namespace passed as the
  second `...` arg to each addon file (`local name, addon = ...`).
  Verified empirically: vanilla 1.12 calls `lua_pcall(L, 0, 0, -2)`
  on every addon chunk — `nargs = 0`, so `...` is always empty.
  Confirmed at four pcall sites in the addon-loader region
  (0x704B68, 0x704D22, 0x704E79, 0x705199), all with the same
  `xor edx, edx; mov ecx, L; call lua_pcall` pattern. The
  convention was added in 3.0+ when WoW moved to Lua 5.1; the
  underlying engine machinery (per-addon table allocation,
  vararg injection) doesn't exist in 1.12. Adding it would
  require hooking the pcall sites to inject args AND maintaining
  a per-addon-name table map ourselves — a feature add, not just
  an API exposure. Skip unless an addon explicitly needs it.
- **`DoesAddOnHaveLoadError(index)`** — load-error tracking. May
  not exist in 1.12 in a queryable form.
- **`IsAddOnDefaultEnabled(index)`** — initial-enabled state from
  the .toc `## DefaultState:` tag (not in 1.12 .toc syntax).

### Skip

- `GetScriptsDisallowedForBeta` — beta-specific; no 1.12 analogue.

### Why this is worth doing

Almost every modern addon released in the last few years uses
`C_AddOns.IsAddOnLoaded` instead of the global. Backporting an
arbitrary modern addon to 1.12 currently means find/replace
across the codebase. Tier 1 alone (one afternoon) makes that
unnecessary for the most common 17 entry points.

### Investigation findings — direct AddOnInfo struct reads (deferred)

**Goal:** make Tier 2 convenience wrappers (GetAddOnName/Title/
Notes/Loadable/Security/DoesAddOnExist) read directly from the
engine's per-addon struct instead of dispatching to
`Script_GetAddOnInfo` and discarding 6 of 7 returns. **Status:
deferred** — struct layout is more complex than a clean inline-
string packing, and time-to-derive exceeds the perf upside for
the typical caller.

**What we learned:**

- Addon **array** lives at `[0x00BE1B94]` (pointer to array of
  4-byte pointers) and **count** at `[0x00BE1B90]`. Confirmed
  via `Script_GetAddOnInfo` disassembly: lookup is
  `if (idx >= [0x00BE1B90]) return null; return ((void**)
  [0x00BE1B94])[idx];`.
- Each entry points to a per-addon struct (heap-allocated, sized
  per-addon).
- **Struct fields we identified (relative to struct base):**

| Offset      | Content                                            |
|-------------|---------------------------------------------------|
| `+0x00..+0x0B` | Name buffer — inline, 12 bytes, null-terminated. Both 8-char "Atlas-TW" and 9-char "aux-addon" fit. |
| `+0x0C`     | Heap pointer (Storm allocator addr like `0x29B4D908`) **OR** null. Non-null for Atlas-TW; null for aux-addon. Derefs to non-printable bytes — likely a struct/array, not a direct string. |
| `+0x10..+0x17` | Often zeros across multiple addons. |
| `+0x18`     | Constant `0x909` across all addons probed. Format/version marker? |
| `+0x1C`     | Constant `5` across all addons probed. Schema version? |
| `+0x20+`    | Variable — for shorter addons this is unrelated adjacent heap memory (saw "ItemSync" appear once and "Notes" appear in another session for aux-addon, both clearly not part of aux-addon's data). |

- **Title and Notes are NOT inline-packed** at the start of the
  struct as I initially hypothesized. Atlas-TW's actual title is
  `"Atlas-TW"` (matches name) and notes is the long
  `"Atlas-TW: instance Map Browser..."` string. Neither appears
  in the first 0x60 bytes of the struct except where the inline
  *name* field happens to look like the title.

- **The pointer at `+0x0C`** is the most likely indirection to
  the title/notes data, but it points to a binary blob (non-
  ASCII first bytes), suggesting another layer of indirection
  (struct of pointers, or a tagged section list). Would require
  a second-level scan to chase.

- **Engine helper functions** for field extraction live around
  `0x0051DF00`–`0x0051E0xx` (LookupByIndex, GetTitle, GetNotes,
  etc.), reachable via call-target arithmetic from
  `Script_GetAddOnInfo`. Initial decoding was confused by
  off-by-one arithmetic on my part — the targets land on what
  appear to be mid-instruction addresses, suggesting the
  helpers may use non-standard prologues or are tail-call
  thunks. Worth tracing more carefully.

- **Diagnostic that worked:** `_classicapi_AddonStructDump(idx)`
  with SEH-wrapped pointer deref (the bare pointer-deref
  approach crashes with `0xC0000005` when interpreting inline
  string bytes as pointers — `"s-TW"` happened to fall in
  `0x10000000..0xF0000000` range and AVed). The
  `SafeReadAsciiString` helper using `__try`/`__except` is the
  pattern to keep if we resume this work.

**Recommended next step** if revisiting: extend the diagnostic
to recursively chase the `+0x0C` pointer (read 32 bytes there,
check if any look like further string pointers), and trace
1.12's `Script_GetAddOnMetadata` (`0x0048E530`) which is more
self-contained than `GetAddOnInfo` and might reveal a cleaner
field-access pattern.

Tier 3 (interface version, addon local table, etc.) still
deferred — none of those have a proven 1.12 storage location.

## ~~55. `IsMounted` / `IsStealthed` / `IsFalling` / `IsSwimming`~~ — DONE

Shipped as `src/unit/State.cpp`. Modern globals that 1.12
doesn't bind to Lua, despite the underlying state being tracked
by the engine. All four read directly off the local player
struct or its descriptor — no dispatch:

- `IsMounted()` — checks `UNIT_FIELD_MOUNTDISPLAYID` at
  descriptor `+0x1FC`. Non-zero means the engine is rendering
  a mount.
- `IsStealthed()` — checks bit `0x02` of the player visibility
  byte at descriptor `+0x17C`, AND-gated with `MountDisplayID
  == 0` (mount also sets that bit). May false-positive on
  shapeshift/possess/etc. — untested, fix by walking the
  player's aura list looking for the actual stealth/prowl
  spell if it turns up in practice.
- `IsFalling()` — checks `MOVEFLAG_FALLING | MOVEFLAG_FALLING_FAR`
  (`0x2000 | 0x4000`) on the local player's movement-flags u32
  at `+0x9E8`. This is client-side state (outbound MSG_MOVE_*
  data), not broadcast — only meaningful for the local player.
- `IsSwimming()` — same movement-flags word, `MOVEFLAG_SWIMMING`
  (`0x200000`).

### How we found the offsets

A general-purpose probe system in `src/debug/` (Probe.cpp +
Log.cpp + Log.h) writes labeled descriptor / player-object
snapshots to `<WoW dir>\Logs\classicapi_debug.log` (alongside
the engine's `FrameXML.log`, `Sound.log`, etc.). Path is
resolved at runtime via `GetModuleFileNameA(nullptr)` so the
DLL works against any client install. User runs
`_classicapi_DescLog("baseline", 0, 0x100)` then
`_classicapi_DescLog("mounted", 0, 0x100)` etc.; agent reads
the log file directly and diffs.

Findings:

- **MountDisplayID** found by diffing baseline vs mounted
  descriptor — only `+0x1FC` changed from 0 to a creature
  display ID like `0x4306`.
- **Stealth bit** found by diffing baseline vs stealthed —
  `+0x17C` byte 0 went `0 → 0x02`. Mounted state went
  `0 → 0x1A` (= bits 1, 3, 4). Bit 1 is shared, bits 3+4
  are mount-specific.
- **Movement flags** found by diffing standing/swimming/falling
  player-object dumps. `+0x9E8` went `0 → 0x200000` (swimming)
  and `0 → 0xE001` (falling: bits 0x1 FORWARD + 0x2000 FALLING
  + 0x4000 FALLING_FAR + 0x8000 PENDING_STOP). Bit values
  match documented vanilla `MOVEFLAG_*` constants exactly.

The probe helpers (`_classicapi_DescDump`, `_classicapi_PlayerDump`,
`_classicapi_DescLog`, `_classicapi_PlayerLog`,
`_classicapi_LogClear`, `_classicapi_LogAppend`,
`_classicapi_LogPrintf`, `_classicapi_HexDump`) are kept in
the build as a reusable offset-finding scaffold. They write
labeled blocks to `<WoW dir>\Logs\classicapi_debug.log`,
sharing the location convention engine logs and other
injected DLLs (nampower, etc.) use. Path is computed at
runtime — no hardcoded install location.

The C++ `Debug::Log` API (in `src/debug/Log.h`) backs the
Lua bindings and is available to any module via
`#include "debug/Log.h"`:

- `Debug::Log::Clear()` — truncate the file.
- `Debug::Log::Append(label, content)` — labeled block, content
  written verbatim. Best for snapshot diffs.
- `Debug::Log::Printf(fmt, ...)` — printf-style one-line trace.
  Auto-newlines if the caller didn't include one.
- `Debug::Log::HexDump(label, ptr, len, [base])` — xxd-style
  hex+ASCII dump of a memory range, 16 bytes per row, with
  the offset column showing `base + i`. SEH-wrapped: if a row
  faults (page boundary, freed memory) the dump stops cleanly
  and notes the failed offset rather than crashing the host.

`HexDump` is the most useful for arbitrary-VA inspection — call
it on a struct pointer to scan layouts, or from Lua via
`_classicapi_HexDump(label, addr, len)` to inspect engine
globals without a per-investigation probe module.

## ~~56. `OffhandHasWeapon()`~~ — DONE

Shipped as `src/item/Equipment.cpp`. Resolves the off-hand
equipment slot (slot 17) via `Item::Location::ResolveEquipmentSlot`,
reads the cached `ItemStats_C` record's `m_inventoryType` at
`+0x2C`, and returns true iff it's `INVTYPE_WEAPON` (13) or
`INVTYPE_WEAPONOFFHAND` (22). Shields, holdables (tomes/orbs),
empty slots, and uncached items all return false.

No async load is fired — modern API behavior matches; if the
off-hand item isn't cached the function silently returns false
until something else warms the cache. Typically only matters on
first login before the player's own gear is in the cache, which
in practice is loaded eagerly anyway.

## 57. `IsFlying()` — TESTED, DROPPED

Modern `IsFlying()` returns true when on a flying mount. Vanilla
1.12 has no flying mounts, so the closest semantic mapping is
"on a flightpath" (i.e., `UnitOnTaxi("player") == 1`).

Initial implementation tested `MOVEFLAG_FLYING = 0x01000000` on
the local movement-flags word at `[CGPlayer + 0x9E8]`. The bit
is documented in CMaNGOS as `MOVEMENTFLAG_FLYING`, but verified
empirically that 1.12 does NOT set it during taxi flights —
`/dump IsFlying()` stayed false through an entire flightpath
on which `/dump UnitOnTaxi("player")` correctly returned 1.

Vanilla taxi rides are server-driven splines that animate the
mount/character without flipping a local movement-flag bit;
the FLYING bit only ever gets set on flying mounts (which
don't exist) or possibly GM fly mode (untested). Bit 0x01000000
in vanilla may instead be `MOVEMENTFLAG_SPLINE_ENABLED` per
some references, but if so, that bit also stays clear during
normal taxi.

Conclusion: there is no useful interpretation of `IsFlying()`
in vanilla. `UnitOnTaxi("player")` covers the only flight-like
state the client tracks. Function and `MOVEFLAG_FLYING`
constant both removed; `Offsets.h` carries a comment noting
the empirical result so this doesn't get re-attempted.

## ~~58. `FillLocalizedClassList(table [, isFemale])`~~ — DONE

Shipped as `src/classes/Info.cpp`. Iterates `ChrClasses.dbc`
records and writes `tbl[classToken] = localizedClassName` for
each, returning the same table for chaining.

ChrClasses record layout (derived from `Script_GetSelectedClass`
at `0x004716E0`):

- `+0x14` — `char *Name[9]` localized class names (one per locale)
- `+0x38` — `char *Filename` class token (`"WARRIOR"`, `"MAGE"`, etc.)

Vanilla 1.12 has no separate female-name array — `Name[9]` is
exactly the 36 bytes between `+0x14` and `+0x38` (= 9 locales × 4
byte ptr), with the token immediately after. The modern
`isFemale` arg is accepted for signature parity but ignored;
same names returned regardless. Most locales (English included)
don't differentiate gender forms anyway, so callers won't typically
notice. Sparse classIDs (vanilla skips classID 6 — Death Knight
didn't exist) have NULL records and are silently skipped.

Stack discipline: takes the table at idx 1, drops any extra args
via `SetTop(L, 1)`, then loops `PushString(token); PushString(name);
SetTable(L, 1)` per record. `SetTable` consumes both stack values
so the table sits alone at idx 1 throughout the loop. Returns 1
to push the (same) table back to Lua.

## ~~59. `C_Item.EquipItemByName` cursor-free path~~ — DONE

The current implementation in
[src/item/Equipment.cpp](src/item/Equipment.cpp) is a cursor-state
guard: refuses to operate when `CursorHasItem()` is true. Modern
WoW's `EquipItemByName` silently preserves the cursor by routing
through an engine-internal "swap by GUID" helper. With Ghidra on
the right binary we found that helper.

### The primitive: `FUN_005E0C40`

`Script_EquipCursorItem` at `0x00489660` calls `FUN_005E0C40` —
the engine's own swap-and-send function. Signature decoded by
reading the call site and the function body:

```c
void __thiscall FUN_005E0C40(
    CGPlayer *this,
    u32 srcItemGuidLo, u32 srcItemGuidHi,
    u32 srcContainerGuidLo, u32 srcContainerGuidHi,
    u32 srcLinearSlot,
    u32 dstContainerGuidLo, u32 dstContainerGuidHi,
    u32 dstLinearSlot,
    int flag);                 // 0 = normal path
```

Internal behavior:

1. Validates source/dest containers (player invMgr vs CGContainer
   bag) — looks up `FUN_00468460` to resolve container by GUID.
2. Picks the opcode based on swap type:
   - **`0x10D` (`CMSG_SWAP_INV_ITEM`)** — same-container swap.
     Payload `{src_slot, dst_slot}`. Used when both src and dst
     resolve to the player's direct invMgr.
   - **`0x10C` (`CMSG_AUTOEQUIP_ITEM`)** — cross-container swap.
     Payload `{dst_bag, dst_slot, src_bag, src_slot}`. Used when
     src or dst is in a CGContainer (equipped bag).
3. Writes the packet via `FUN_00418190(opcode)` + `FUN_00418070(byte)…`
   into the same buffer at `PTR_LAB_007FF9E4` that every real
   network packet flows through.
4. Sends via `FUN_005AB630` — **the actual send call the previous
   investigation couldn't find**.

### Why this bypasses the cursor

`Script_EquipCursorItem` reads cursor state via `FUN_00494C80`
before passing it to `FUN_005E0C40`, but `FUN_005E0C40` itself
neither reads nor writes the cursor globals at `[0xBE0810]` /
`[0xBE0814]`. The dead-code packet builder at `0x005E0B50` was
unrelated; the real production path is just `FUN_005E0C40`. We
hand it the right args and it sends the packet — no cursor touch.

The previously-identified gate at `0x501130` was the **right-click
"use item from cursor"** path (opcode `0x276`), not the swap path.
Different concern entirely.

### Linear-slot encoding

The engine's linear slot:

| Linear slot | Container | What |
|---:|---|---|
| `0..18`   | player invMgr | paperdoll slots 1..19 (subtract 1 from Lua slot) |
| `19..22`  | player invMgr | equipped bag containers (bag IDs 1..4) |
| `23..38`  | player invMgr | backpack (bag ID 0) contents, 1-based slot S → linear = 22+S |
| `0..N-1`  | bag CGContainer | slot inside the bag (subtract 1 from Lua slot) |

For destination paperdoll equip: `dstContainer = player`, `dstSlot = paperdollSlot - 1`.

For source:

- Item in paperdoll slot N → `srcContainer = player`, `srcSlot = N - 1`
- Item in backpack slot S (bagID 0) → `srcContainer = player`, `srcSlot = 22 + S`
- Item in equipped bag B (bagID 1..4) slot S → `srcContainer = bag GUID`,
  `srcSlot = S - 1`. Bag GUID read off its CGItem instance block at `+0x8`.

### Risk assessment

The previous TODO worried about hand-built packets disconnecting
the client. **We never build a packet** — we hand off high-level
args and the engine's own builder/sender runs verbatim. Same code
path drag-and-drop equip uses. Only risk is bad GUIDs / slot
indices, validated client-side before the call.

### Cross-reference

2.4.3 has `ItemMgr::EquipByGUID` at `0x5E8310` (`__thiscall(this=invMgr,
guidLo, guidHi, dstSlot, 0)`) which bundles find-by-GUID + swap.
1.12's equivalent is split: find-by-GUID is our existing
`Item::Location::FindByGUID` + `Locations::FindGUID`, swap is
`FUN_005E0C40`.

### Implementation plan

[src/item/Equipment.cpp](src/item/Equipment.cpp) `Script_C_Item_EquipItemByName`:

1. Find the item by name (existing `FindItemInBags`).
2. Resolve source location via the table above → `(containerGuid, linearSlot)`.
3. Compute target paperdoll slot (explicit arg, or auto-pick from
   `m_inventoryType` for the no-slot form).
4. Read player GUID off the CGPlayer instance block at `+0x8`.
5. Call `FUN_005E0C40(player, itemGuid, srcContainer, srcSlot,
   playerGuid, playerGuid, dstSlot-1, 0)`.
6. Drop the cursor-guard. Cursor state is now irrelevant.

## 60. `GameTooltip:AddSpellByID(spellID)` — easy/medium

Modern method that **appends** spell info to the current tooltip
without clearing existing lines — natural pair to `SetSpellByID`,
which clears + rebuilds. Lets callers compose tooltips like
"talent name (line 1) + AddSpellByID(rankSpell) (rest)".

Direct upgrade for our cross-class `GameTooltip:SetTalentByID`
fallback (#43): the current tier-2 path renders just the spell
tooltip, losing the talent name. With `AddSpellByID` we could do
`ClearLines + AddLine(talentName) + AddDoubleLine("Rank 0/N", "")
+ AddSpellByID(spellID)` to match modern's richer cross-class
tooltip exactly.

Implementation question: 1.12's `BuildSpellTooltip` (the inner
helper at `0x0052E610` we use for `SetSpellByID`) almost certainly
clears the buffer first. We'd need a different entry point that
appends, OR find where the line-emission helper sits inside
`BuildSpellTooltip` and call that directly.

Approach: disassemble `Script_GameTooltip_SetSpell` (`0x00532D10`)
and `BuildSpellTooltip` to identify the "ClearLines" prologue and
the "emit description / cost / range / cooldown" loop. If those
are factored apart, the loop is callable standalone and `AddSpellByID`
is a thin wrapper. If they're inlined, we'd either need to skip
the clear ourselves (call after some no-op state setup) or
manually call the line-emission primitives.

## ~~61. `GameTooltip:GetItem()` / `GetSpell()` / `GetUnit()`~~ — `GetItem` + `GetSpell` DONE; `GetUnit` skipped

Modern query methods that return what the tooltip is currently
showing:

- `GetItem()` → `(name, link, itemID)` — DONE
- `GetSpell()` → `(name, rank, spellID)` — DONE
- `GetUnit()` → `(name, unitToken)` — skipped intentionally

Each Set* path stashes a "currently displayed" field on the tooltip
frame instance, and the per-tooltip Clear at `FUN_00530050` zeroes
the full set on Hide/before-redraw. Verified offsets:

| Field | Offset | Written by | Cleared by |
|---|---:|---|---|
| Item ID | `+0x398` | `BuildItemTooltip` (0x0052B6FE) | `FUN_00530050` |
| Spell ID | `+0x39C` | `BuildSpellTooltip` (0x0052E6D5) | `FUN_00530050` |

`GetSpell` reads `+0x39C`, looks up the Spell.dbc record, and pushes
the localized name + rank + spellID. Returns nothing if the slot is
zero (no spell tooltip displayed). See [src/spell/Tooltip.cpp](src/spell/Tooltip.cpp).

`GetItem` reads two fields: itemID at `+0x398`, item GUID at
`+0x380/+0x384`. When the GUID is non-zero (any path with a real
CGItem — `SetBagItem`, `SetInventoryItem`, `SetLootItem`,
`SetMerchantItem`, etc.) we resolve to a CGItem and dispatch to the
engine's own per-instance link builder at `FUN_0052AE00` — same
helper `Script_GetContainerItemLink` (0x004F9930) uses after
resolving its slot-form args. That produces the fully dressed link
with enchant ID, random-suffix factor, unique ID, and decorated
name. When the GUID is zero (`SetItemByID` / `SetHyperlink` with no
instance data), we fall back to a basic colored link built from
itemID + cached quality + cached name. Returns `(name, link,
itemID)` — the third return is non-modern but saves callers a
gsub. See [src/item/Tooltip.cpp](src/item/Tooltip.cpp).

`GetUnit` was prototyped two ways (token-walk over 92 hardcoded
tokens; MinHook on `Script_GameTooltip_SetUnit` + side-map) and
both scrapped. The 1.12 engine drops the token string immediately
after converting it to a GUID at `+0x368/+0x36C`, so there's no
clean way to return the original token. The walk-list approach is
brittle (misses any future-added token); the hook approach works
but adds collision surface with other Octo DLLs on a high-traffic
function. Modern WoW (3.3.5+) added a dedicated "current unit
token" field on the tooltip frame for exactly this reason — 1.12
just doesn't have the storage. Re-investigate if a low-collision
path emerges (e.g. a write site we haven't seen in 1.12 that
already preserves the string).

## ~~65. `hooksecurefunc(name|table, [name,] callback)`~~ — DONE

Shipped as [src/HookSecureFunc.cpp](src/HookSecureFunc.cpp). Pure C
implementation using a `lua_pushcclosure` with two upvalues
(`orig`, `callback`). The wrapper calls orig with `LUA_MULTRET`,
then callback (returns discarded), then returns orig's full result
list with no count cap.

### Why pure C, not embedded Lua

Originally drafted as a Lua source string executed via
`FrameScript_Execute` at boot. Reworked to pure C to keep all
implementation in one language and avoid embedded source strings;
the refactor added three Lua C API offsets we hadn't wrapped before
(`lua_gettop` at `0x6F3070`, `lua_remove`, `lua_call` at
`0x6F4180`), plus the `LUA_UPVALUEINDEX(i) = GLOBALS_INDEX - i`
helper.

### Lua API offset corrections discovered along the way

While diagnosing why `hooksecurefunc("GetSpellInfo", fn)` was returning
"target field is not a function", a series of `_classicapi_*Probe`
helpers traced the failure to **three misidentified offsets** in
`docs/LuaCAPI.md` (and `Offsets.h`):

| VA | Doc'd as | Actually is |
|----|----------|-------------|
| `0x006F30D0` | `lua_pushvalue` | **`lua_remove`** (shift-down loop + decr_top) |
| `0x006F32B0` | `lua_remove`    | **`lua_replace`** (TValue copy + decr_top)   |
| `0x006F3350` | `lua_replace`   | **`lua_pushvalue`** (TValue copy → top + incr_top) |

How we caught it: `_classicapi_PushValueProbe` showed `GetTop` going
from `1 → 0` after `PushValue(L, 1)` — the supposed `lua_pushvalue`
was actually popping (lua_remove behavior). Disassembly of `0x6F30D0`
confirmed the shift-down loop + `add [ecx+8], -0x10`, vs `0x6F3350`
which has the standard `add [ecx+8], 0x10` incr_top after a single
TValue copy.

`Offsets.h` and `docs/LuaCAPI.md` both updated. The misidentified
offsets had no observable effect previously because no code in the
project used `PushValue` outside `HookSecureFunc.cpp` — every
existing call path uses `PushString`/`PushNumber` for fresh pushes
and `SetTable`/`GetTable` for stack manipulation.

### Why 1.12 needs this even without taint

Modern's "secure" label is about taint propagation in 3.x+ protected
frames. 1.12 has no taint system; the function is functionally a
plain after-hook with return preservation. We ship it for **modern
API parity** — addons being backported from later expansions
frequently use `hooksecurefunc(GameTooltip, "SetInventoryItem", ...)`
and similar patterns, expecting the original to run first and their
callback to fire afterward. Both forms (global by name, table+method)
are supported.

### Unhookable globals

Modern WoW rejects `hooksecurefunc("<core lua/secure fn>", ...)` outright
to keep callers from clobbering language primitives or breaking taint
propagation. We mirror that with a name blacklist in `Script_HookSecureFunc`
that fires `lua_error` with `"hooksecurefunc: function is unhookable"`
when the two-arg form targets one of: `getfenv`, `getmetatable`,
`hooksecurefunc`, `ipairs`, `issecurevalue`, `issecurevariable`, `next`,
`pairs`, `pcall`, `pcallwithenv`, `rawget`, `rawset`, `scrub`,
`securecall`, `securecallfunction`, `secureexecuterange`, `select`,
`setfenv`, `setmetatable`, `type`, `unpack`, `wipe`, `xpcall`.

Three-arg form (table + name + callback) is exempt: hooking
`MyLib.pairs` is fine even if `_G.pairs` is not — the blacklist is
specifically about replacing functions on the globals table. A user who
passes `_G` explicitly as the table target is opting in deliberately.

### Cross-version reference

2.4.3 has the strings `hooksecurefunc`/`securecall`/`issecure`
clustered together at `FO=0x4D2CF8`/`0x4D2D08`/`0x4D2D28` in `.rdata`,
suggesting they're a "secure environment helpers" string table for
the engine's taint-propagation code. The `hooksecurefunc` Lua
implementation itself is in `FrameXML/RestrictedFrames.lua` even in
modern WoW — the engine never had an internal C version we could
mirror.

## ~~63. `GameTooltip:SetInventoryItemByID(itemID)`~~ — DONE

Shipped in [src/item/Tooltip.cpp](src/item/Tooltip.cpp).
Confirmed empirically distinct from `SetItemByID`: with run-speed
enchanted boots equipped, `SetInventoryItemByID` shows the
enchant line; `SetItemByID` shows the base item only.

Implementation walks character-pane slots 1..19 looking for an
equipped item matching `itemID`; on hit, dispatches to the
engine's existing `Script_GameTooltip_SetInventoryItem` (registry
slot 19, `0x00532EE0`) with `("player", slot)` rewritten onto the
Lua stack. Silent no-op when the item isn't currently equipped —
caller falls back to `SetItemByID` for unworn items.

### Duplicate-item slot ordering — verified

When the player has duplicates of the same itemID equipped, modern
client renders the **lower-numbered slot** first: MAINHAND (16)
before OFFHAND (17), FINGER1 (11) before FINGER2 (12), TRINKET1
(13) before TRINKET2 (14). Our ascending 1..19 walk + first-match-
break naturally produces the same ordering — lucky, since the
INVSLOT constants happen to be ordered with the "primary" slot
numbered lower than its peer. Locked in with a code comment so
the loop direction doesn't accidentally drift in future refactors.
## 64. `OnTooltipSetItem` / `OnTooltipSetSpell` / `OnTooltipSetUnit` script handlers — DEFERRED, scoped

Modern WoW fires `OnTooltipSetItem` / `Spell` / `Unit` script handlers
on `GameTooltip` after each `SetX` finishes filling the tooltip. The
canonical addon pattern:

```lua
GameTooltip:HookScript("OnTooltipSetItem", function(self)
    local _, link = self:GetItem()  -- depends on TODO #61
    -- augment tooltip
end)
```

1.12 has the tooltip script-handler infrastructure but only fires
**three** specific names: `OnTooltipAddMoney`, `OnTooltipCleared`,
`OnTooltipSetDefaultAnchor`. The modern set is missing.

### Architecture (verified)

- 1.12 has **`GetScript` (`0x00774780`) and `SetScript` (`0x007748D0`)**
  natively, but **no native `HookScript`**. Addons polyfill `HookScript`
  Lua-side — confirmed via:
  - `!!!ClassicAPI/api/api.lua:458`
  - `pfUI/modules/eqcompare.lua:224`

  Both polyfills compose through `GetScript` + `SetScript`, so
  hooking those two makes every existing `HookScript` polyfill in
  the wild work for our new names. We don't need to write one
  ourselves.

- The script-name → handler-storage mapping lives in a virtual
  method on `GameTooltip` (resolved via `vtable[?]`), pointed at by
  one entry in `.rdata` at `FO=0x408F6C` → matcher function at
  `0x005295D0`. The matcher's body:

  ```
  push 0x7FFFFFFF; push "OnTooltipSetDefaultAnchor"; push scriptName
  call SStrCmpI → if match: lea eax, [frame+0x444]; ret
  push 0x7FFFFFFF; push "OnTooltipCleared"; push scriptName
  call SStrCmpI → if match: lea eax, [frame+0x44C]; ret
  push 0x7FFFFFFF; push "OnTooltipAddMoney"; push scriptName
  call SStrCmpI → if match: lea eax, [frame+0x454]; ret
  xor eax, eax; ret  // unknown name
  ```

  Frame storage layout: 8 bytes per script handler slot (probably
  `{lua_func_ref, flags}`).

- `GetScript`'s "doesn't have a 'X' script" error fires when this
  matcher returns 0 AND the name isn't in the standard frame
  scripts (OnEvent, OnEnter, etc.).

### Implementation plan

**Phase 1 — Script handler intercept** (~half-day)

- MinHook on `Script_GetScript` (`0x00774780`) and
  `Script_SetScript` (`0x007748D0`). Both are real functions
  (already addressable, not just registry slots).
- For each:
  1. Read scriptName from Lua stack.
  2. If name ∈ `{"OnTooltipSetItem", "OnTooltipSetSpell", "OnTooltipSetUnit"}`:
     - Validate stack[1] is a frame.
     - For Set: store handler in our registry table.
     - For Get: push from our registry table or `nil`.
     - Return without invoking original.
  3. Else: tail-call original.
- Storage: Lua registry table (allocated once at boot, kept alive
  via a known registry index). Keys: `frame_va * "/" * scriptName`.
  Handler lookup is `lua_gettable(L, our_table_idx)`.
- Lua ref management: 5.0 uses `lua_ref`/`lua_getref`/`lua_unref`.
  Need to find these in the binary (probably near the existing
  Lua C API offsets at `0x6F3xxx`); else use a pure Lua-table
  approach (set `our_table[key] = function`, retrieve with
  `lua_gettable`).

**Phase 2 — Fire from our existing entry points** (~1 hour)

After `Spell::Tooltip::ShowByID` and `Item::Tooltip::Set*ByID`,
call a `FireTooltipScript(L, frame, "OnTooltipSetItem")` helper
that does `lua_gettable` for the handler and `lua_pcall(L, 1, 0)`
with `frame` as arg.

This gives a working but incomplete OnTooltipSetItem — fires for
our two ByID functions but not for the engine's
`SetInventoryItem`/`SetBagItem`/etc.

**Phase 3 — Cover engine `Set*Item` functions** (~2-4 hours)

Either hook each individually (~19 `Script_*Item` functions in 1.12,
list in `docs/raw_methods.txt`) or find a unified inner builder:

- Item: no unified `BuildItemTooltip` apparent — each
  `Script_GameTooltip_Set*Item` builds independently. Hooking each
  via MinHook is straightforward but adds 19 hook points.
  Alternative: find the deeper inner helper they all converge on.
  The Set*Item functions all have similar prologues; trace one
  thoroughly to find the shared call.
- Spell: **`BuildSpellTooltip` at `0x0052E610`** is the unified
  builder (we already use it from `SetSpellByID`). Single hook.
- Unit: find inner of `Script_GameTooltip_SetUnit`
  (slot 31, `0x005349B0`).

**Phase 4 — GetItem/GetSpell/GetUnit** (TODO #61, related)

For handlers to be useful, addons typically need to know *what*
was set. Find frame state offsets where the engine stores the
current item/spell/unit. Approach:

1. Set known value via `SetItemByID(6948)` / `SetSpellByID(133)` /
   `SetUnit("player")`.
2. Dump GameTooltip frame instance memory.
3. Search for `6948` / `133` / player GUID.
4. Each match reveals the offset for that state field.

Once located, `GetItem()`, `GetSpell()`, `GetUnit()` are trivial
deref+push. Implementation roughly the same as `OffhandHasWeapon`
or `IsEquippedItem` patterns.

### Risks

- **Hot-path hooks:** `GetScript` and `SetScript` are called by
  every frame for every script registration. Mistakes in our hooks
  can break the entire UI. Test extensively before shipping.
- **Lua ref leaks:** if a frame is destroyed while we hold a ref
  to its handler, we leak. Need a "frame destroyed" hook (or
  rely on garbage collection of a weak-keyed table).
- **Engine internals path:** finding the unified item builder
  (Phase 3) might require deep RE; falling back to 19 individual
  hooks is safe but ugly.

### Alternative deferred design — Option B (in case Phase 1 turns
out unworkable in practice)

Skip the SetScript/GetScript intercept entirely. Provide:

```lua
GameTooltip.OnTooltipSetItem = function(self) ... end
```

(field-assignment, no HookScript polyfill needed). We fire from
all the same places as Phase 2-3, but instead of `lua_gettable`
on our registry table, we do `lua_getfield(frame, "OnTooltipSetItem")`
directly off the frame's Lua table. Simpler, but doesn't compose
with `HookScript` polyfills automatically (each addon would
clobber the previous).

## ~~66. Left/right modifier keys + `MODIFIER_STATE_CHANGED` event~~ — DONE

Shipped in [src/input/Modifier.cpp](src/input/Modifier.cpp). Adds
the seven query functions (`IsLeftShiftKeyDown`, `IsRightShiftKeyDown`,
`IsLeftControlKeyDown`, `IsRightControlKeyDown`, `IsLeftAltKeyDown`,
`IsRightAltKeyDown`, `IsModifierKeyDown`) plus the 2.4.3+
`MODIFIER_STATE_CHANGED(key, down)` event. All seven match the
modern WoW return shape (`1` for down, `nil` for not-down).

### Why this needed a Win32 hook

The engine has **no L/R distinction anywhere in its state**. 1.12's
own `IsShiftKeyDown` chain bottoms out at `GetKeyState(VK_SHIFT)` —
the merged virtual key `0x10` — at `0x00436024`, never the L/R-aware
`VK_LSHIFT` / `VK_RSHIFT` (`0xA0` / `0xA1`). Helper3 at `0x00436D80`
returns a struct with only 3 modifier slots (shift/ctrl/alt), confirmed
by inspection. No amount of digging in engine memory finds L/R state
because **it never existed there**.

There IS a 6-bit field at `[*[0x00C0ED38] + 0x269C]` that looks
structurally identical to 2.4.3's L/R modifier bitmask at `[+0x130]`
— same shift-and-mask helper at `0x005936C0`, same 6-bit dispatch
limit. We chased it for a while. Empirically the field is something
else entirely (some kind of input-device-flags bitmask) — it stays at
`0x3BF` regardless of which modifier is held. The 2.4.3 → 1.12
structural similarity is misleading; the field's meaning evolved.

The OS-level keystate **does** have the L/R distinction (VK_LSHIFT /
VK_RSHIFT etc.), and 1.12 already uses Win32 `GetKeyState` for its own
modifier checks — so calling `GetAsyncKeyState(VK_LSHIFT)` ourselves
is the same primitive the engine already uses, just with the L/R-aware
VK codes.

### Why a `WH_GETMESSAGE` hook over `SetWindowLongPtr` subclass

We initially subclassed WoW's `WNDPROC` (via `EnumWindows` to find the
`GxWindowClassD3d`/`GxWindowClassOpenGl` HWND, then `SetWindowLongPtrA`
to install a wrapper). Worked for L/R queries and the event. Then the
user toggled vsync and the event went silent — WoW destroys and
recreates its main window on some renderer state changes, so the
subclass was left dangling on a destroyed HWND.

Switched to `SetWindowsHookExA(WH_GETMESSAGE, ..., GetCurrentThreadId())`.
The hook is per-thread, not per-HWND, so it survives any number of
window recreations. Decodes `WM_KEY{,SYS}{DOWN,UP}` messages,
maintains a 6-bit cached bitmap that the queries read, and fires
`MODIFIER_STATE_CHANGED` on transitions.

### L/R decoding from WM_KEY messages

- `VK_SHIFT` (the merged virtual key the OS delivers for either key)
  → `MapVirtualKeyA(scancode, MAPVK_VSC_TO_VK_EX)` returns
  `VK_LSHIFT` or `VK_RSHIFT` based on the hardware scancode.
- `VK_CONTROL` / `VK_MENU` → the `KF_EXTENDED` flag (bit 24) of
  `lParam` discriminates: extended = right, plain = left.
- `VK_L*` / `VK_R*` delivered directly (some keyboards, SendInput) →
  accepted as-is.

### `MODIFIER_STATE_CHANGED` and the custom-event saga

Documented as part of the broader event-table investigation in
CLAUDE.md's "Events" section. Short version: we tried three approaches
to inject custom event names into the engine's event table.

**Approach A — slot-claim (original Event::Custom)**: walk the live
table for NULL slots, write our `SMemAlloc`+memcpy'd name. Works.
Downside: first NULL slots were near the head of the table (slot 1
got `MINIMAP_BLIP_TRACKING_CHANGED` from VanillaMinimapTracking,
which felt off).

**Approach B — rebuild-hook + tail-injection**: hook
`RebuildEventTable` at `0x00703D90`, pad the input array to 1024
entries, place our names at the tail (slots 1020+). Engine's own
SStrDup handles allocation, so we never write to engine memory.
Worked for a single DLL. **Crashed when SuperWoWhook, nampower,
transmogfix, and VanillaMinimapTracking all hooked the same
function**: their layered modifications to `(names, count)` produced
a count→buffer-size mismatch, and the engine's fill loop wrote off
the end of the allocated buffer on its last iteration.

**Approach C — slot-claim + backward walk (final)**: same as A but
walk the table from `count-1` downward looking for NULL slots, so
high indices are claimed first. Each DLL works on the table
independently — no hook chaining, no argument modification, no
crashes. Custom events naturally group at the tail (verified: slots
695..699 with all five custom events when ClassicAPI +
VanillaMinimapTracking are both loaded). Also swapped
`SMemAlloc + memcpy` for the engine's own `SStrDup` at `0x0064A620`
— cleaner allocator, exactly matches what the engine's bulk
rebuild does.

The intermediate rebuild-hook approach also uncovered the engine's
**hardcoded post-rebuild slot writes**: the bulk rebuild populates
slots 0..548 from its `0x00BE1198` name array, then engine code
writes specific events to hardcoded slot indices in the 549..699
range (`SPELL_DAMAGE_EVENT_SELF` at slot 549,
`SPELL_DAMAGE_EVENT_OTHER` at 550, etc.). Documented in CLAUDE.md.
This is why claiming slots from low indices early-on would have
gotten clobbered — the slot-claim approach only fires after Lua's
first `RegisterEvent` call, by which point the engine's writes have
settled.

## ~~67. `BAG_UPDATE_DELAYED` event~~ — DONE (2026-05-11 second attempt)

Modern 5.4.8+ event that fires once per game frame in which any
`BAG_UPDATE` fired. The 5.4.8 engine sets an
`m_bagUpdateDelayedPending` flag from the bag-change pipeline and
drains it from `CContainerMgr::Update` (its per-frame routine),
producing exactly-one coalesced fire per frame.

### What we tried

A pure C++ hook chain:

- Naked thunk over `FUN_FIRE_EVENT` (`0x00703F50`) — peek at the
  event ID, set a pending flag on match against the cached
  `BAG_UPDATE` slot, `jmp` to the trampoline so the variadic stack
  passes through untouched.
- Normal `__thiscall`-style hook over the per-frame OnUpdate
  dispatcher at `0x00772890` — drain the pending flag and fire
  `BAG_UPDATE_DELAYED` once per frame.

Reservation via the existing `Event::Custom::AutoReserve` slot-claim
machinery. The DLL compiled cleanly and registered the event;
`IsEventValid("BAG_UPDATE_DELAYED")` returned `true`.

### Why we reverted

The DLL crashed during the login-screen XML load — `EIP=0x15FF075B`
inside MinHook trampoline space, with zero bytes at that address,
`ESP=0x001AEFEB` (misaligned), and stack ret-addr `0x00772954` inside
the patched function. Classic signature of a corrupted MinHook
trampoline.

The user's Octo install loads SuperWoWhook, nampower, transmogfix,
UnitXP_SP3, VanillaHelpers, VanillaMinimapTracking, and
VanillaMultiMonitorFix alongside ours. Each links its own MinHook
copy. Hooking high-traffic engine functions like the event dispatcher
or per-frame OnUpdate sits squarely in the path where trampoline
regions can collide between DLLs — same class of problem as the
`RebuildEventTable` hook-chaining failure we already documented in
CLAUDE.md.

We removed `src/bag/UpdateDelayed.{cpp,h}`, the two `HOOK_FUNCTION`
calls in `DllMain.cpp`, and `FUN_FRAMESCRIPT_FRAME_ONUPDATE` from
`Offsets.h`.

### Earlier dead end: Lua-source bootstrap from C++

Before the hook attempt, I shipped a version that used
`FUN_FRAME_SCRIPT_EXECUTE` (`0x00704CD0`) to inject a Lua snippet
creating a hidden frame with `RegisterEvent("BAG_UPDATE")` /
`SetScript("OnUpdate", ...)`. The user pushed back: embedding Lua
source in the DLL is architecturally wrong for this project. The
right answer is always a C++ engine hook. `FUN_FRAME_SCRIPT_EXECUTE`
is gone from the codebase; pretend it doesn't exist. (See the
saved feedback memory.)

### Ghidra investigation (2026-05-11) — actual fire-site map

Disassembling 1.12 reveals **three call sites** of
`FUN_00703F50(0x148, "%d", bagID)` (event ID `0x148` = 328 =
BAG_UPDATE — derived from the BAG_UPDATE name pointer's position
in the input array at `0x00BE16B8`, offset `0x520` from array
base, divided by stride 4):

1. **`FUN_004F91A0`** — bag-slot diff loop (2 callers — quiet).
   Walks linear slots `0x13..0x16` (player bags 1..4) and
   `0x3B..0x44` (bank bags 5..10), comparing each against a cached
   GUID snapshot at `DAT_00BDD060`. Fires `BAG_UPDATE(bagID)` for
   each slot whose GUID changed. Also fires event `0x149`
   (BAG_CLOSED) when a bag is removed. Called from the per-session
   init (`FUN_004F8CC0`) and from packet-handler `FUN_004F8EC0`.

2. **`FUN_004F8DB0`** — item-descriptor-change callback (4
   callback registrations, all in init helpers). Direct
   `FUN_00703F50(0x148, "%d", -2)` for slot range `0x51..0x70`
   (keyring). Other slot ranges delegate to `FUN_004F9370`.

3. **`FUN_004F9370`** — item→bag resolver (3 callers, all inside
   the bag subsystem). Given an item GUID, searches `DAT_00BDD060`
   for a match. Fires `BAG_UPDATE(0)` if the item belongs to the
   player (backpack), `BAG_UPDATE(N)` if it's in bag N, returns
   silently if not found in any bag. This is the **most common
   path** — every "item moved within bags" event flows through
   here.

All three targets are quiet (≤4 callers each, all inside the bag
code region `0x004F8DB0..0x004F94D0`). None overlap with the
hot-loop hook targets the previous attempt collided on
(`FUN_FIRE_EVENT`, the per-frame OnUpdate).

### Revised implementation plan

**Approach: hook all three fire sites + read engine's per-frame
counter to coalesce.**

The engine maintains a monotonic frame counter at `0x00C7B2C8`,
incremented once per world tick from `FUN_0066FD50` (the world-
subsystem update). Only **two writers** in the binary —
`FUN_0066F6C0` zeros it on world load, `FUN_0066FD50`
increments it +1 per frame. Reading it tells us, exactly,
"are we in a new frame since the last observation?"

```cpp
constexpr uintptr_t VAR_WORLD_FRAME_COUNTER = 0x00C7B2C8;
uint32_t g_lastSeenFrame = 0;

void EmitDelayedOnFrameBoundary() {
    const uint32_t now = *reinterpret_cast<uint32_t *>(VAR_WORLD_FRAME_COUNTER);
    if (now == g_lastSeenFrame) return;  // same frame — coalesce
    g_lastSeenFrame = now;
    const int slot = Event::Custom::Lookup(kBagDelayedEvent);
    if (slot >= 0) Event::Custom::Fire_None(slot);
}

void __cdecl Bag_004F91A0_h() {
    g_004F91A0_orig();
    EmitDelayedOnFrameBoundary();
}

// Same wrapper pattern for FUN_004F8DB0 (__thiscall taking
// `(int slotID, GUIDlo, GUIDhi, int *cachedGUID)` per the
// decompile) and FUN_004F9370 (cdecl `(int lo, int hi)`).
```

Properties:
- **Exact frame coalescing** — multiple BAG_UPDATEs in the same
  frame produce exactly one BAG_UPDATE_DELAYED, regardless of how
  close they are in wall-clock time.
- **No time math, no arbitrary threshold** — the engine's own
  frame counter is the source of truth.
- **No new hook on a per-frame routine** — we *read* the counter,
  not hook the writer. Zero MinHook footprint outside the three
  bag-subsystem targets.
- **No glue-screen activity** — `DAT_00C7B2C8` doesn't increment
  before the player enters world, which is fine since BAG_UPDATE
  doesn't fire there either.

**Why this avoids the previous attempt's crash:** none of these
three targets are common hook destinations for other DLLs. The
problem before was hooking `FUN_FIRE_EVENT` (100+ callers,
trafficked by every event) and `FUN_FRAMESCRIPT_FRAME_ONUPDATE`
(per-frame dispatcher) — both prime collision targets for
SuperWoWhook / nampower / transmogfix. The bag subsystem
internals at `0x004F9xxx` aren't touched by those DLLs (none of
them care about bag-update timing), and the per-frame counter
at `0x00C7B2C8` is a pure memory read with no hook at all.

**Semantic match vs modern:** modern coalesces at exact end-of-
frame; we coalesce within a single world-tick frame using the
same counter the engine itself uses to drive the framerate
sampler. Equivalent in practice — addons doing "rescan
inventory on BAG_UPDATE_DELAYED" see exactly one fire per frame
in which any bag activity occurred.

### Fallback if even three quiet hooks collide

Drop to hooking only `FUN_004F9370` (the most common path).
Misses bag-equip-itself and keyring cases but covers the 90%
case of "user picked up an item / moved an item in bag." Single
hook, lowest collision risk.

### Honest fallback

If both hook plans collide, document the limitation — addons
that want this behavior on Octo-style multi-DLL clients can
fall back to debouncing `BAG_UPDATE` themselves via an
`OnUpdate` timer.

## ~~68. `UnitChannelInfo(unit)` / `UnitCastingInfo(unit)`~~ — DONE

Shipped in [src/spell/Cast.cpp](src/spell/Cast.cpp): `UnitCastingInfo`,
`CastingInfo`, `UnitChannelInfo`, `ChannelInfo`. Verified in-game —
Blizzard channel `(…, 907894314, 907902314, …, 10187)` (8s), Conjure Food
cast `(…, 907928268, 907931268, …, 28612)` (3s), `GetTime()*1000` falls
between start/end.

**The start/end-time hunt resolved with a firm negative**, confirmed via
the cross-binary technique. 3.3.5's `Script_UnitCastingInfo`
(`0x00611DF0`) / `Script_UnitChannelInfo` (`0x00612090`) read cast times
from **per-unit CGUnit fields** (cast `+0xa6c/+0xa78/+0xa7c`, channel
`+0xa80/+0xa84/+0xa88`) — TBC/WotLK additions that don't exist in the
1.12 CGUnit (the tell: 1.12's *player* cast spellID is a global,
`VAR_CURRENT_CAST_SPELL`, not a unit field). 1.12 stores no cast times
anywhere readable; the cast bar is Lua-driven off `SPELLCAST_START`, and
remote units' casts are transient spell-visual objects
(`FUN_0060d7c0`), not a queryable record. The cast-timer globals at
`0x00cee8d4..e4` are a shared visual-system slot (written for *any*
channeling unit via `FUN_00612a30 → FUN_0060d7c0`), so they're not
player-safe.

So timing is **self-tracked for the local player** on `WorldTick`:
- cast: `VAR_CURRENT_CAST_SPELL` 0→nonzero → `startMs = FUN_OS_TICKCOUNT_MS`,
  `endMs = start + SpellCastTimes base ms`. Instant casts (cast time 0)
  and channels are filtered out of the cast path.
- channel: player's broadcast `UNIT_FIELD_CHANNEL_SPELL` (`+0x228`),
  `endMs = start + SpellDuration base ms`.

`FUN_OS_TICKCOUNT_MS` shares `GetTime()`'s epoch (×0.001), so the ms
values feed addon progress math directly. Other units: channel
spellID/name/texture from the broadcast `+0x228` (no times); regular
casts return nil (no vanilla data).

`isTradeskill` is real — `SPELL_ATTR_TRADESPELL` (Attributes bit 0x20,
the version-stable bit 3.3.5's Script_UnitCastingInfo reads). Verified:
Rough Blasting Powder (Engineering) → true, Conjure Food → false.

Cast time is the **effective** time, not base: we call the engine's
cast-time helper `FUN_006e3340` (base + level scaling + cast-time SpellMod
op 10 + the caster's haste/cast-speed multiplier at descriptor +0x22c) —
the same helper the engine's own cast-start uses, so our end time matches
the cast bar. Verified: talented Frostbolt (Improved Frostbolt -0.5s)
reads 2.5s, not the 3.0s base. (Calling the helper beats replicating op 10
via `Spell::Mod`, which would miss the cast-speed multiplier.)

**Follow-up:** `delayTimeMs` pushback tracking (currently 0).

Companion field (`desc 0x1320`, prior channeled spell) — see entry #71.

## 69. `IsGathering()` — easy (local player only)

No-arg local-player boolean that fires while the player is
mid-gather: herb pick, ore mine, fishing-bobber-loot, lockpicking,
skinning. Modern WoW doesn't expose this exact concept but addons
that care (auto-attack suppression, mouse-look reversion, autorun
toggles) reverse-engineer it from `LOOT_OPENED` /
`UNIT_SPELLCAST_*` events. A direct read is cleaner.

State machine from the `IsAssistingRitual` session, verified by
diffing the player while gathering:

- `[player + 0x1D28]` / `[player + 0x1D2C]` — 64-bit GUID of the
  GameObject the player is gathering from. `0xF110xxxx` high half
  = vanilla GameObject prefix. Set when the gather animation
  starts, cleared when it ends or breaks.
- `[player + 0xD48]` — small int, `0` baseline, `1` during gather.
  Confirmed for herb gather; mining sets the same field. Cleanest
  single-bit signal we have.
- `UNIT_FIELD_FLAGS` (descriptor `+0xA0`) bit `0x1000` is cleared
  during gather — unclear what the bit semantically means (other
  emulator sources don't agree on this bit's name in vanilla), but
  it's another way to detect the state.

Three implementation options worth picking between:

1. **`IsGathering()`** boolean, `[player+0xD48] != 0`. One line.
2. **`GatheringObjectGUID()`** returning `(lo, hi)` from
   `[player+0x1D28]/+0x1D2C`. Lets addons distinguish herb-from-
   herb / chest-from-chest. Modest extra surface.
3. **Family of `IsHerbing()` / `IsMining()` / `IsFishing()`** —
   requires looking up the GameObject's type/sub-class from its
   GUID, which means walking the engine's GO cache. Significantly
   more work; defer unless someone has a use case.

Fishing is the trickiest case — `/cast Fishing` is a regular spell
cast (sets UNIT_CHANNEL_SPELL = 7620), the bobber sits in water
during a separate wait phase (UNIT_CHANNEL_OBJECT = bobber GUID),
then the right-click bobber action triggers the gather/loot
machinery (sets `[player+0x1D28]/+0x1D2C` to the bobber GUID, but
NOT `0xD48`). So `[player+0xD48]` is "gather-cast active"
(herbing/mining), and `[player+0x1D28]` is the broader "engaged
with a lootable GO" pointer. Pick which semantic you want before
naming.

## ~~70. `UnitStandState(unit)`~~ — DONE

Modern `GetUnitStandStateValue(unit)` or `UnitStandState(unit)` (the
name varies by expansion) returns an integer describing whether
the unit is standing / sitting / sleeping / kneeling. Vanilla 1.12
exposes only `IsSitOrStanding()` (local boolean) and lacks any
"is target sitting" check.

Verified directly via the `IsAssistingRitual` session: descriptor
`+0x210` is `UNIT_BYTES_1` (CMaNGOS field 132 in the 1.12.1
layout). The standstate is the **low byte** of that u32. Observed
transition during a `/sit` on a chair: `0xEE00 → 0xEE05`,
i.e. low byte 0 → 5 (`STANDSTATE_SIT_MEDIUM_CHAIR`).

Vanilla standstate enum (from emulator sources, low byte of
`UNIT_BYTES_1`):

| Value | Meaning             |
|------:|---------------------|
| 0     | STANDING            |
| 1     | SITTING             |
| 2     | SITTING_CHAIR       |
| 3     | SLEEPING            |
| 4     | SITTING_LOW_CHAIR   |
| 5     | SITTING_MEDIUM_CHAIR|
| 6     | SITTING_HIGH_CHAIR  |
| 7     | DEAD                |
| 8     | KNEELING            |

Implementation: resolve unit → descriptor → `[+0x210] & 0xFF`.
Broadcast field, so it works for any synced unit (player, target,
party/raid, inspect targets) — not local-only.

While we're documenting `UNIT_BYTES_1`, the other bytes of the same
u32 are worth deriving when needed (CMaNGOS docs say byte 1 =
`PetTalents`, byte 2 = `VisFlag`, byte 3 = `AnimTier`), but none
have an obvious modern-API hook in vanilla.

## ~~72. `C_EquipmentSet.*` namespace — DONE

Full backport of modern's equipment-set API on top of a client-side
persistent store at `WTF\Account\<acct>\<realm>\<char>\
ClassicAPI_EquipmentSets.txt`. 18 functions including `Create`,
`Save`, `Modify`, `Delete`, `Use`, `GetItemLocations`, plus the
ignored-slot quartet. Identity is by item GUID — `Locations::FindGUID`
walks the player invMgr's flat GUID array for paperdoll / backpack /
main bank, and reaches bag contents (player bags 1..4 and bank bags
5..10) via each bag's CGContainer→invMgr through `vtable[+0x10]`.
Same direct-read technique `C_Item.GetItemCount` uses, so bank items
resolve without the bank window being open.

Also exposes the `ITEM_INVENTORY_LOCATION_PLAYER` / `_BAGS` / `_BANK`
/ `_BAG_BIT_OFFSET` / `_BANK_BAG_OFFSET` constants as Lua globals so
addon code copy-pasted from modern FrameXML's
`EquipmentManager_UnpackLocation` works unchanged. Fires
`EQUIPMENT_SETS_CHANGED` (no payload) on any mutation and
`EQUIPMENT_SWAP_FINISHED(success, setID)` at the end of
`UseEquipmentSet`. See `docs/API.md` and `src/equipmentset/`.

## ~~73. `C_EquipmentSet.UseEquipmentSet` — proper swap-cycle resolution~~ — DONE

`UseEquipmentSet` currently walks slots in order and does a
pickup→equip pair per item. If two items in the set want each other's
slots (e.g. ring A in slot 11 wants slot 12, ring B in slot 12 wants
slot 11), the first pickup succeeds, the equip drops it correctly,
but the second pickup picks up B and `EquipCursorItem` swaps it
into the now-occupied slot — leaving one ring stashed on the cursor
which our cleanup `ClearCursor` either drops back or routes to a free
bag slot. Re-running `UseEquipmentSet` finishes the job, but it
shouldn't take two calls.

### Solution: same primitive as #59

`FUN_005E0C40` (decoded in #59) is the engine's atomic swap-and-send.
Replacing the pickup+equip pair with a direct `FUN_005E0C40` call
fixes the cycle case naturally: each swap is one server packet that
atomically exchanges two slots. The classic 2-cycle "A in 11, B in
12, want A in 12, B in 11" resolves in a single call —
`FUN_005E0C40(player, A, player, 10, player, player, 11, 0)` sends
opcode `0x10D {src=10, dst=11}` and the server swaps both slots.

Longer chains (3+ items rotating) are rare with 1.12's equipment
slot set — the realistic conflicts are 2-cycles between paired
slots (rings 11/12, trinkets 13/14, weapons 16/17). One swap each.

For sequential non-cycle moves (item from bag → empty slot), the
swap is still atomic and doesn't touch the cursor. The catch is
that consecutive `FUN_005E0C40` calls in the same frame queue at
the server before any inventory update arrives — so subsequent
iterations see stale local state. For independent moves this is
fine (each swap target is different); for chains it can produce
no-op or undo behavior.

### Implementation plan

[src/equipmentset/Api.cpp](src/equipmentset/Api.cpp)
`Script_UseEquipmentSet`:

1. Drop the `ClearCursor()` calls at start and end — irrelevant now.
2. Replace the per-slot pickup+equip pair with a single
   `FUN_005E0C40` call:
   - srcItem GUID: read off the CGItem at the resolved
     `Locations::FindGUID` location
   - srcContainer + srcLinearSlot: from `loc` decode per the table
     in #59
   - dstContainer = player, dstSlot = `targetSlot - 1`
3. Keep the existing skip-conditions: GUID empty/ignored, already
   in target slot, in bank, etc.

The bank-skip stays: vanilla server rejects equip-from-bank, and
no client-side workaround changes that.

Same shared helper between #59 and #73: an `Item::Swap::EquipByGuid`
or similar that takes (CGItem*, dstPaperdollSlot) and handles the
container-guid + linear-slot resolution. Implement once, use in
both call sites.

## ~~74. `EQUIPMENT_SWAP_PENDING` event~~ — DONE

Fires `(setID)` at the start of `UseEquipmentSet` right after the
set-exists check, before any pickup/equip work. Skipped when the
set doesn't exist (only FINISHED with `success=false` fires
there). Added `Event::Custom::Fire_D` (single-int variant) to
support the one-arg signature.

## 75. Bank-bag walk — extract shared helper

`Item::Count::CountInBankBags` and `EquipmentSet::Locations` both
walk bank-bag contents via the same pattern: read GUID at linear
slot 63..68 of player invMgr → `FUN_OBJECT_RESOLVE_BY_GUID` with
`OBJ_TYPE_CONTAINER` → invoke `vtable[+0x10]` to get the bag's own
invMgr → walk that invMgr's flat GUID array at +0x04. Same
boilerplate twice. If a third caller needs it, extract to
`Item::Inventory::WalkBagsByGUID(callback)` or similar in a shared
header. Not worth the refactor for two callers yet.

## 77. `IsLoggedIn()` — skipped

WotLK addition. Returns `true` if the player is in the world,
`false` at character select / login screen.

Prototyped twice and pulled both times:

1. **World-only registration** (via the regular module-register
   hook) — implementation was trivial (`FUN_RESOLVE_UNIT_TOKEN("player")
   != nullptr`), but the function only existed in the world Lua
   state. Glue-screen Lua couldn't reach it, so addons that gate
   on the world transition would see `attempt to call a nil value`
   pre-login.
2. **Glue + world** (added a MinHook on the glue script registration
   at `FUN_0046DDF0`) — crashed on the "Tiny" 1.12.1 build with an
   access violation at the patched bytes. The glue-register
   function is too small (~9 byte prologue) for a stable 5-byte
   JMP across binary variants and likely overlaps with another
   loose-loader DLL's hook.

For now, addons that need login-state gating can use the
`PLAYER_LOGIN` / `PLAYER_ENTERING_WORLD` events (both fire
in-world reliably). Revisit if a safer glue registration path
surfaces.

## ~~83. `C_Item.GetWeaponEnchantInfo()`~~ — DONE

Vanilla 1.12's global `GetWeaponEnchantInfo` returns 8 values
(has/expire/charges per weapon slot) but **no enchant IDs**.
WotLK+ extended the signature to 12 returns by inserting the
temp-enchant ID after each weapon's data. We provide the modern
12-tuple in the `C_Item` namespace, leaving the vanilla global
intact for addons that depend on its position layout.

Reads `ITEM_FIELD_ENCHANTMENT` slot 1 (descriptor `+0x4C..+0x54`,
the TEMPORARY enchantment slot) for mainhand / offhand / ranged
equipment slots — the same slot the engine drains as the
oil/stone/poison expires. The permanent enchant (Crusader,
Mongoose, etc. in slot 0 at `+0x40`) isn't reported here; modern's
`GetWeaponEnchantInfo` is specifically about temp data.
See [src/item/WeaponEnchant.cpp](src/item/WeaponEnchant.cpp).

## ~~78. `GetCVarBool(cvar)`~~ — DONE

Dispatches the engine's `Script_GetCVar` at `0x00488BA0` to read
the cvar's string value, then coerces: non-zero numeric or
`"true"` (case-insensitive) → `true`; `"0"` / empty / nil /
unknown cvar → `false`. Exposed as `C_CVar.GetCVarBool(cvar)`.
See [src/cvar/Bool.cpp](src/cvar/Bool.cpp).

## 79. `IsTrialAccount()` / `IsVeteranTrialAccount()` — trivial

WotLK additions. Always returns `false` on 1.12 — vanilla
predates trial accounts (introduced 5.x). Modern addons gate
features on these and crash without them. One-liner.

## ~~80. `IsHelpfulSpell(spell)` / `IsHarmfulSpell(spell)`~~ — DONE

Reads `Spell.dbc.AttributesEx` (`+0x1C`) bit `0x80` —
`SPELL_ATTR_EX_NEGATIVE` (CMaNGOS' `SPELL_ATTR_EX_NEGATIVE_1`).
Same bit the engine uses to classify auras into the debuff slot
range, so it's reliable for combat spells.

The earlier TODO note about `AttributesEx2` bits `0x10` / `0x20`
turned out to be wrong — those bits are `CANT_REFLECTED` and
`AUTO_REPEAT` per CMaNGOS, not helpful/harmful. The right flag is
the single `NEGATIVE` bit in `AttributesEx`.

Helpful side: "spell exists in Spell.dbc and isn't marked negative"
— vanilla has no dedicated positive-spell flag, so utility spells
that aren't harmful read as helpful. Approximation; close enough
for most addon use cases. Strict modern semantics (where some
spells return false for both) would require parsing every effect's
implicit target — deferred.

Four Lua surfaces: `IsHelpfulSpell(spell)`, `IsHarmfulSpell(spell)`
(global, accepts `spellID` or `(slot, bookType)`), and
`C_Spell.IsSpellHelpful(spellID)` / `C_Spell.IsSpellHarmful(spellID)`
(C_Spell namespace, takes numeric ID only).
See [src/spell/Info.cpp](src/spell/Info.cpp).

## 81. `GetItemUniqueness(item)` — easy

Returns `(limitCategory, maxCount)` — used by stack-aware addons
(BankItems, ARL) to know whether an item is "unique" (max 1 in
inventory) or "unique-equipped" (max 1 equipped).

Reads from ItemStats record's flag field. Vanilla items have a
simpler binary "unique" flag; the modern Limit Category system
(`ItemLimitCategory.dbc`) was added in TBC and may not exist in
1.12. Implementation: return `(0, 1)` for items with the unique
flag set, `(0, 0)` otherwise.

## 82. `GetItemCooldown(itemID)` — medium

Returns `(startTime, duration, enable)` for an item's
on-use cooldown — the standard cooldown tuple. Different from
`GetContainerItemCooldown` (which takes bag+slot); modern addons
use the itemID form because they don't always know where the
item lives.

Implementation: items with on-use spells map to spell cooldowns
via `Item::Spell::OnUseSpellIDForItemID` (already wired for
`Hearthstone`/`GetItemSpell`). From the spellID, query the engine's
spell-cooldown machinery — same function `Script_GetSpellCooldown`
uses internally.

## 83. `GetItemStats(itemLink, [statTable])` — medium-hard

Returns a `{ ITEM_MOD_STAMINA_SHORT = 5, ITEM_MOD_STRENGTH_SHORT = 10, ... }`
table for the item. Heavy addon dependency — Pawn, AtlasLoot,
GearScore, and every gear-comparison TSM-style addon use it.

ItemStats record carries up to 10 stat-type/stat-value pairs at
fixed offsets. 4.3.4 implementation at `0x0044C200` parses the
itemLink (for enchant + gem deltas) then walks those 10 slots and
emits a table keyed by stat-type-id-string.

In vanilla, enchant IDs from itemLinks need lookup via
`SpellItemEnchantment.dbc` to add their stat deltas. Gems are N/A
(no socket system in vanilla — drop that branch). The base
ItemStats walk is the main work.

## 84. `IsUsableItem(item)` — medium

Returns `true` if the item is currently usable by the player —
considers level requirement, class restriction, race restriction.
Used by tooltip code and "use item" buttons to gray themselves
out for unusable items.

Vanilla itemstats has `m_requiredLevel`, `m_allowableClass`, and
`m_allowableRace` fields. Implementation: peek the cache, check
those three against player's level/class/race (race is in
`UNIT_FIELD_BYTES_0`, already read in `GetPlayerInfoByGUID`).

## 85. `IsSpellInRange(spell, unit)` / `SpellHasRange(spell)` — medium

Range-check predicates. `SpellHasRange` is a Spell.dbc
`RangeIndex != 1` check (range index 1 is "Self Only"). 
`IsSpellInRange(spell, unit)` requires:
1. Resolving spell name to spellID (lookup is already in
   `Spell::Lookup`)
2. Reading `RangeIndex` from Spell.dbc (already wired in
   `Spell::Info` for the range column)
3. Looking up `minRange`/`maxRange` from `SpellRange.dbc` (already
   in `OFF_SPELLRANGE_*` constants per `Offsets.h`)
4. Computing 3D distance between player and unit
5. Returning `1` if inside range, `0` if outside, `nil` for
   invalid unit (can't get position)

Step 4 needs the engine's unit position read primitive — we have
no offset for that yet. CGUnit has a position field at some
descriptor offset; finding it is an hour's work.

## 86. `GetMirrorTimerInfo(index)` / `GetMirrorTimerProgress(label)` — DONE

Implemented in [src/unit/MirrorTimer.cpp](src/unit/MirrorTimer.cpp).
The vanilla engine fires `MIRROR_TIMER_START` / `_PAUSE` / `_STOP`
events with the full payload but doesn't cache the state, so we hook
the SMSG handler at `FUN_005E7990`, peek the packet via cursor
save/restore on the CDataStore at `+0x14`, and build a 3-slot side
cache. `GetMirrorTimerInfo` returns the snapshot; `GetMirrorTimerProgress`
interpolates against engine ticks for the live value. Type names are
`"EXHAUSTION"` / `"BREATH"` / `"FEIGNDEATH"` (engine wording — modern
uses `"FATIGUE"` for the off-map one; we preserve what the engine
actually returns).

## 87. `GetMacroSpell(macroIndex)` — DONE

Implemented in [src/macro/Spell.cpp](src/macro/Spell.cpp). Turned out to
be a one-read job: the vanilla engine already walks every macro body
at create / edit / refresh via `FUN_MACRO_PARSE_PRIMARY_SPELL`
(`0x004EFE00`) and caches the resolved spellID on the macro struct at
`+OFF_MACRO_PRIMARY_SPELL` (`+0x564`). `GetMacroSpell` reads the cache
via `FUN_MACRO_SLOT_TO_ENTRY` (`0x004F0E40`), translates `0` /
`0xFFFFFFFF` sentinels to "no match," and looks up name + rank from
`Spell.dbc` for the resolved ID. Returns `(name, rank, spellID)`
matching modern's 3-tuple. `CastSpellNoToggle("<name>")` macros are
also recognized because `Spell::CastNoToggle` already hooks the same
parser to register that pattern.

## ~~88. `PLAYER_EQUIPMENT_CHANGED(equipmentSlot, hasCurrent)` event~~ — DONE

**Resolution (2026-07-04, src/player/Equipment.cpp):** the 2026-05-22
investigation kept trying to hook a FIRER of UNIT_INVENTORY_CHANGED.
The winning move was the opposite: the engine has a generic
descriptor-field observer system (the "__AUCMirrorHandler__" nodes —
registrar `FUN_00467E70`, dispatcher `FUN_00465570` with per-node
mirror + memcmp change detection). `FUN_004F8CC0` registers observers
for the bag/backpack/bank/keyring GUID ranges but never for equipment
`0x4A8..0x538` — so we co-hook it and REGISTER OUR OWN observer per
equipment field, mirroring the engine's registration exactly (bank 4,
size 8, player GUID). Callback ABI verified from the dispatcher call
site at `0x004655DC` + `FUN_004F8DB0`'s `RET 0x10`:
`__fastcall(fieldOffset /*ecx*/, size /*edx*/, guidLo, guidHi,
oldValue*, userArg)`. hasCurrent read from the invMgr GUID array (the
same "current truth" the engine's bag callback reads). No polling, no
hot hook, engine-managed lifetime (setup re-runs each enter-world).
Offsets documented at `FUN_DESC_OBSERVER_REGISTER` in Offsets.h.

Original notes below kept for the investigation trail.

WotLK-era event that fires every time a paperdoll slot changes
(equip, unequip, swap). Heavily used by gear-tracking,
tooltip-decoration, and stat-sheet addons. **The single highest-
value event missing from vanilla** — backporting it would
unblock dozens of addons that gate refresh logic on it.

Vanilla fires `UNIT_INVENTORY_CHANGED` for the player when any
slot changes, but doesn't say WHICH slot. Implementation: cache
the player invMgr's GUID array for slots 0..18 at our hook
point, diff against last snapshot, and fire
`PLAYER_EQUIPMENT_CHANGED(slot, hasItem)` once per changed slot
via `Event::Custom`.

### Investigation history (2026-05-22)

User strongly prefers an event-driven hook over per-frame
polling. Multiple promising targets tried; **none fired on
user-driven equip/unequip actions in the Octo build**.

**`FUN_004F8DB0`** (the bag/inventory descriptor-change callback)
— our existing `BAG_UPDATE_DELAYED` hook target. Empirically only
called for backpack contents (offsets `0x560..0x5D9`, slots
23..38) and keyring (offsets `0x730..0x829`, slots 81..112). The
`FUN_004F8CC0` registration setup confirms there is **no
registered per-slot descriptor-change callback for equipment
slots 0..18** (offsets `0x4A8..0x53F`). Equipment slot descriptor
writes happen silently during SMSG_UPDATE_OBJECT processing.

**Three direct `push 0xBA; call FUN_00703F50` sites** found for
`UNIT_INVENTORY_CHANGED`:
- `0x004C710B` and `0x004C72DB` (both in `FUN_004C7180`,
  `__cdecl(uint guidLo, uint guidHi)`, 5 callers in the
  `0x5D8xxx-0x5D9Bxx` SMSG region).
- `0x004C8504` (in `FUN_004C84F0`, 0 direct callers — vtable or
  dead).

**Two `mov edx, 0xBA; call FUN_00515E50` sites** at `0x0051BD73`
and `0x005D85FE`. `FUN_00515E50(guid, eventID)` is the
**unit-event firer** — walks the GUID→token reverse resolver
(`FUN_00515C50`) and fires `(eventID, "%s", token)` for each
matching unit token. 37 callers across many event IDs — direct
hook is too generic / high traffic.

`FUN_005D8440` (containing `0x5D85FE`) is the item-update
post-processor — `__fastcall(void *cgItem)`, 2 callers, fires
UNIT_INVENTORY_CHANGED via `FUN_00515E50(itemGUID, 0xBA)` for
the affected item.

**Hook attempts that did NOT fire on equip/unequip:**
- `FUN_004F8DB0` via `Bag::InvDescChange` dispatcher subscription
  — slot range is backpack + keyring only, never equipment.
- `FUN_004C7180` direct hook — function's preconditions
  (visible-item filter, slot-range gate) bail before FIRE_EVENT
  on plain bag↔equipment swaps.
- `FUN_005D8440` direct hook — debug log never printed despite
  the user confirming `UNIT_INVENTORY_CHANGED` fires.

**Open question:** if all three known 0xBA-firing functions are
hooked and none of our hooks trigger on equip, then either:
(a) the hooks aren't being installed for some reason (collision?
some Octo modification?), or (b) the event fires through a
mechanism we haven't found — possibly via the LUA-side
`FrameScript_FireEvent("UNIT_INVENTORY_CHANGED", "player")`
called from another function we haven't traced.

**Next steps to try:**
- Add `_classicapi_DumpHook` or similar diagnostic that confirms
  the hook is actually in place after `MH_EnableHook` returns.
  Maybe Octo or another DLL is unhooking us.
- Search the binary for `"UNIT_INVENTORY_CHANGED"` string
  references — if the event name is referenced in code (not just
  in the boot-time table), there's a string-based firing path
  we missed.
- Investigate the `mov ecx, 0xBA` sites at `0x60341B` and
  `0x604F77` — both call `FUN_005AB650`/`FUN_005AB670` (packet
  handler registration), so `0xBA` there is a server OPCODE, not
  an event ID. Confirm and rule out.
- Try hooking one tier higher: `FUN_005AB650(opcode, handler)`
  callers in the SMSG_UPDATE_OBJECT region might give us the
  actual update path.
- Alternative architecture: skip event-driven entirely and use
  `Tick::WorldTick` polling. Per-frame cost is 19 itemID reads
  + 19 compares (~negligible). User pushback on polling was
  philosophical, not perf-based; might be acceptable as a
  fallback if a clean hook can't be found.

### Side benefit shipped during this investigation

`Bag::InvDescChange` shared dispatcher (`src/bag/InvDescChange.{h,cpp}`)
modeled on `Tick::WorldTick::AutoSubscribe`. `Bag::UpdateDelayed`
now subscribes to it instead of owning the `FUN_004F8DB0` hook
directly. **Currently has a single subscriber so adds no value
on its own — revert if not needed for resumed work.**

### Bonus (deferred until base event works)

Same diff feeds `EQUIPMENT_SETS_CHANGED` /
`EQUIPMENT_SWAP_FINISHED` for our existing `C_EquipmentSet`
module — currently those fire only on user-side mutations, not
on inventory changes that affect a set's "equipped count".

## ~~89. `UPDATE_INVENTORY_DURABILITY` event~~ — DONE

**Resolution (2026-07-04, src/player/Equipment.cpp):** co-hook the
engine's inventory-alerts recompute `FUN_004C7EE0` — the routine that
walks equipment slots, reads each item's durability/broken state from
the descriptor, and unconditionally fires vanilla's
`UPDATE_INVENTORY_ALERTS` (event 501). The engine calls it whenever an
owned item's fields change (the item post-update handler `FUN_005D9A90`
routes every item values-update through it, alongside
UNIT_INVENTORY_CHANGED and the bag-update chain) plus equip and
enter-world paths — i.e., it IS the engine's own "durability may have
changed" signal. Post-original we diff a per-slot {guid, durability}
snapshot (seeded silently on the first recompute per enter-world) and
fire once per recompute with a real change. No polling, no hot hook.

**Dead end tried first:** bank-1 (ITEM field) descriptor observers per
equipped item, via the TODO #88 observer system. They register without
error but are NEVER invoked — item values-updates demonstrably arrive
(probed the values pre-pass `FUN_00465330`) and player-bank observers
fire in the same session; registration arithmetic fully verified (bank
base `FUN_00465690(1)=0x18`, bank walk item 0→1→2, anchors match
dispatch). Prime suspect: the registrar's internal wrapper lookup
(`FUN_00464890`, the observer registry — NOT the object manager)
silently no-ops for item objects. See the DEAD END note at
`OFF_DESC_ITEM_DURABILITY` in Offsets.h.

Original notes below kept for the investigation trail.

Fires when any equipped item's durability changes — picks up
combat damage, repairs, item destruction. Used by Repair-O-Matic
type addons and durability HUDs to avoid polling.

### Field layout (verified)

CGItem descriptor `[item + 0x114]` has:
- `+0xA0` — `ITEM_FIELD_DURABILITY` (field index `0x28`)
- `+0xA4` — `ITEM_FIELD_MAXDURABILITY` (field index `0x29`)

Both already read by `Script_GetInventoryItemBroken`
(`0x004C8590`) and by ClassicAPI's existing
`Script_GetInventoryItemDurability` in
[src/item/Durability.cpp](src/item/Durability.cpp) — so the
**read path** is mature; only the **change-detection path**
remains.

Field name strings `"ITEM_FIELD_DURABILITY"` (`0x0083D9FC`) and
`"ITEM_FIELD_MAXDURABILITY"` (`0x0083D9E0`) each have exactly
one xref — into a metadata table at `0x0083A414` (see below).

### 4.3.4 firing mechanism (won't transfer directly)

Confirmed by reading the 4.3.4 binary: there is **exactly one**
firing site for `UPDATE_INVENTORY_DURABILITY` (eventID `0x1BA`)
in Cataclysm, and it's purely server-driven:

```c
// FUN_00551580 — SMSG packet handler:
read_u32(&ctx);
read_u32(&selector);
if (ctx == 0) {
    switch (selector) {
        case 1: dispatch(0x1BA);  // UPDATE_INVENTORY_DURABILITY
        case 2: dispatch(0x1BB);  // UPDATE_TRADESKILL_RECAST
        case 3: dispatch(0x1BC);  // OPEN_MASTER_LOOT_LIST
        case 4: dispatch(0x1BD);  // UPDATE_MASTER_LOOT_LIST
    }
}
```

The 1.12 server doesn't send this opcode, so we can't mirror
it. The event's semantics are just "refresh durability UI" with
no payload — addons handle it by iterating equipment slots and
calling `GetInventoryItemDurability`. So we don't need to know
*which* item changed; we just have to fire when *any* equipped
item's durability changes.

### How 1.12 fires similar per-field events (open question)

`ITEM_LOCK_CHANGED` (eventID 188, slot `0xBE1488` in the input
names array) is the closest model — it fires when
`ITEM_FIELD_FLAGS` changes. Searched the binary for both `push
188 (imm8)` and `push 188 (imm32)` followed by call to the
event dispatcher at `0x00703F50` — **zero hits**. Confirms
1.12 has a generic per-field event-fire mechanism that doesn't
push the eventID as a literal.

The per-field metadata table at `0x0083A414` looks like the
right structure:

| Field           | Idx    | Leading dword | Other bytes |
|-----------------|-------:|--------------:|------------:|
| `ITEM_FIELD_DURABILITY`     | `0x28` | `0x04` | `1, 1`      |
| `ITEM_FIELD_MAXDURABILITY`  | `0x29` | `0x14` | `1, 1`      |
| (idx `0x27`)                | `0x27` | `0x04` | `1, 1`      |
| (idx `0x26`)                | `0x26` | `0x01` | `1, 1`      |
| (idx `0x25`)                | `0x25` | `0x01` | `1, 1`      |

Entry layout: `{leading_dword, name_ptr, field_idx, ?, ?}`
(stride `0x14` = 20 bytes). The leading dword varies per
entry — could be an event-fire flag, broadcast scope, or
network-stream type code. Need to find the consumer function
(table base `0x0083A414` has zero direct VA refs, so it's
consumed via base+index relative to a CGItem `m_objectFields`
metadata pointer somewhere — we don't have that pointer's
addr yet).

### Three implementation paths

1. **Per-frame snapshot polling.** Hook a per-frame engine
   tick, walk 19 equipped slots each frame, hash durability,
   compare, fire on change. Modest cost (19×2 reads/frame).
   Hardest part: finding a clean per-frame hook point that's
   not in the JIT-collision-risk class. Candidate: hook the
   network packet-flush boundary (one-off per game tick,
   cheaper than per-render-frame).

2. **`SMSG_UPDATE_OBJECT` handler hook.** Hook the packet
   handler that applies UpdateField changes to objects.
   Snapshot durabilities before, check after; fire when
   different. More targeted than (1) — only fires when there's
   a network reason for state to have changed. Needs the
   opcode handler VA; not yet pinned down.

3. **Decode the per-field metadata table.** Reverse-engineer
   the `0x0083A414` table layout to understand what the
   leading dword means. If it's the event-fire mechanism the
   engine already uses for `ITEM_LOCK_CHANGED` / `UNIT_HEALTH`
   etc., we patch the `ITEM_FIELD_DURABILITY` /
   `ITEM_FIELD_MAXDURABILITY` entries to fire our reserved
   `UPDATE_INVENTORY_DURABILITY` slot. Cleanest end state,
   deepest spelunking — may hit a dead end if the table's
   consumer isn't where we expect.

The MinHook collision-risk memory note ("hooking high-traffic
engine functions risks JIT/trampoline corruption from
SuperWoWhook/nampower/etc.") argues against (2) since
`SMSG_UPDATE_OBJECT` is one of the hottest opcode handlers.
That pushes us toward (1) or (3). Of those, (3) is the right
end state — no per-frame overhead, fires exactly when the
engine sees a field change — but (1) is a defensible ship-
now answer with a clear migration path to (3) later.

### Cross-binary reference

When picking this back up, the 5.4.8 client (`C:\WoW\World of
Warcraft 5.4.8\Wow.exe`) is the cleanest reference — it has
the same client-side detection (UpdateField → event) but with
post-vanilla code structure. The 4.3.4 packet-driven path is
a Cataclysm-specific oddity and won't reflect what 1.15.x or
modern does.

## 90. Revisit `BAG_UPDATE_DELAYED` (TODO #67 was REVERTED)

Modern event that fires once after a storm of `BAG_UPDATE`s
settles (typically inside a single frame). Lets addons debounce
inventory-scan work without each one rolling its own timer.

Previous attempt: see #67. Reverted because the implementation
hooked something hot and caused issues. Re-attack with the
hook installed at `Frame::FireEvent` (the dispatcher we already
know) — count consecutive `BAG_UPDATE` fires per frame, and on
the first frame that has zero, fire `BAG_UPDATE_DELAYED`.

Lower risk than the previous approach since we're observing
fires rather than hooking the update path. `Event::Custom`
machinery is more mature now and matches what the EquipmentSet
module already does.

## 91. `UNIT_SPELLCAST_SENT(unit, target, castGUID, spellID)` event

The modern per-cast notification with unit + target + spellID.
Vanilla ships `SPELLCAST_START` for the player only, with just
the spell *name* — modern addons want spellID and unit info.

Player-only backport is feasible without wire-protocol work:
hook `SPELLCAST_START` fires (or the underlying engine path that
sends them), translate name→spellID via `Spell::Lookup`, pass
through with `"player"` and the current target token. Cross-unit
(party member casting) requires SMSG_SPELL_GO observation —
defer that to a "Phase 2" or skip permanently.

If we ship the player-only version, document the limitation
prominently — addons that rely on `unit ~= "player"` events
won't get them, but everything that only cares about the local
player works.

## 92. `IsItemInRange(itemID, unit)` — medium

5.4.8-era addition. Range check based on the item's "range
modifier" — long-range items (rifles, throwing weapons) report
true when the target is in firing range. Used by hunter / rogue
addons to gate ability use on item range.

Implementation: read ItemStats range index, look up in
SpellRange.dbc (already wired in `Spell::Info`), compute 3D
distance to `unit`. Same machinery as TODO #85 (IsSpellInRange)
but item-keyed instead of spell-keyed.

If we wire up the unit-position read primitive for either of
these, the other comes nearly free.

## ~~76. `C_DateAndTime.*` namespace — DONE (7 of 8)

Backport of modern's date-math namespace. Six functions are pure
`<ctime>` arithmetic over a CalendarTime table; the seventh
(`GetSecondsUntilDailyReset`) reads `Time::Server::CurrentEpoch()`
and returns `86400 - (epoch % 86400)`. Server midnight semantics
work naturally because `CurrentEpoch` treats the engine's server-
time components as UTC for the epoch conversion — day boundaries
in epoch math align with server-clock midnight.

`GetSecondsUntilWeeklyReset` deliberately omitted — vanilla has no
server-broadcast weekly reset schedule, and Turtle realms vary, so
any hardcoded weekday/hour would be wrong somewhere. Addons that
need it can compute their own value (e.g. "next Tuesday at server
midnight") via `GetCurrentCalendarTime` + `AdjustTimeByDays`.

Side effect: refactored `Time::Server::CurrentEpoch()` out of
`Script_GetServerTime` so the C++ helper is reusable.

## 71. Descriptor `+0x1320` — "last channeled spell" parking lot

During the `IsAssistingRitual` investigation, the player's
descriptor showed `+0x1320` flip from `0` to `0x2BA` (= 698 =
Ritual of Summoning) **immediately after** the channel ended,
where the active channel was at `+0x228`. So `+0x228` =
UNIT_CHANNEL_SPELL (live), and `+0x1320` looks like a "previously
channeled" stash that's populated on transition-to-zero.

Not enough data to commit to a semantic yet. Open questions:

- Does it also stash for fishing / lockpicking / mining channels?
  We have one data point (the portal click) showing the behavior.
- Does it clear, and if so when? (`/reload`? Next channel? Never?)
- Is it a broadcast UpdateField or local-only?

If it turns out to be a stable "last completed channel" field, it
unlocks polyfills for the modern `UNIT_SPELLCAST_CHANNEL_STOP`
event payload (which carries the spellID that just ended) without
hooking the channel-stop code path. Lower-priority; useful but
not load-bearing for any specific API in the polyfill addon
today.

Investigation plan when picked up: snap `+0x1320` before any
channeled action, perform a clean channel, snap again immediately
after channel end (use `_classicapi_DescDump(0x1320, 4)` for a
targeted read). Repeat across several channel types and confirm
the pattern.

## 93. `C_MerchantFrame.SellAllJunkItems` — explicit cursor clear when holding junk — low

Currently, if the player has a junk item picked up on their cursor
when calling `SellAllJunkItems`, the item still sells (the CGItem
remains in its bag slot internally; we walk and find it, the sell
fires, and the engine clears the cursor as a side effect of the
item being destroyed). For non-junk on the cursor, the cursor is
left alone. The incidental behavior is actually pretty good
already — junk gets cleared, non-junk preserved.

The "clean" version would explicitly call `Script_ClearCursor`
(`0x004895B0`) at the start of `SellAllJunkItems` *iff* the cursor
is currently holding a junk item. The cursor-type global at
`[0x00B4D900]` (1 = item, 3 = ?, 9 = ?) gates "is the cursor
holding anything," but the cursor's actual itemID / GUID / bag-slot
reference isn't sitting in an obvious adjacent global — the
neighboring globals at `+0x04` / `+0x08` are unrelated floats and
pointers, not item identifiers.

Vanilla has no `GetCursorInfo` (only `CursorHasItem`), so there's
no Lua-visible accessor to crib from. Finding the cursor item
state means tracing through `Script_PickupContainerItem`'s 200+
bytes of arg-validation to locate where it stashes the picked-up
(bag, slot) tuple after pickup. Probably an hour of recon.

Defer until either:
- a user actively reports the current behavior as annoying, or
- the cursor state needs to be read for some other purpose
  anyway (e.g. a `GetCursorInfo` polyfill).

## ~~94. Glue-side bindings: `GetSavedCharacterOrder` / `SetSavedCharacterOrder`~~ — DONE

Two glue-only globals for persisting the character-select reorder UI
(GlueXML port of Blizzard's anniversary drag-to-reorder). Vanilla 1.12
glue has no general-purpose persistence API — only
`GetSavedAccountName` / `SetSavedAccountName`, which is saturated by
the autologin system.

### As shipped

API (registered on the **glue** Lua state only — in-world Lua sees a
nil global, by design):

```
GetSavedCharacterOrder(realm) -> string   -- "" when missing, never nil
SetSavedCharacterOrder(realm, order)      -- order="" clears the entry
```

Storage: `WTF\Account\<account>\ClassicAPI.txt`, one `CharacterOrder.<realm>=<order>`
line per realm. Account scope is implicit via the file path; realm scope
is explicit via the line key. The `<order>` payload is opaque (the
GlueXML side uses pipe-delimited names). Implementation lives in
[src/charlist/Order.cpp](src/charlist/Order.cpp).

### Registration mechanics

Required three pieces of new infrastructure, all in their own commits:

1. **Glue Lua state hook** — `FUN_0046ABB0` is the master glue init
   (linear caller of all 5 glue batch trampolines, single caller,
   35-byte body). Hooked in [src/DllMain.cpp](src/DllMain.cpp). Fires
   once per glue boot (launch + every world→glue return). Earlier
   attempt at inner trampoline `FUN_0046DDF0` crashed on Octo —
   smaller target, sibling-DLL collision. The linear caller is
   structurally identical to our existing in-game `FUN_LOAD_SCRIPT_FUNCTIONS`
   hook and has none of those problems.

2. **`Game::GlueModuleAutoRegister`** ([src/Game.h](src/Game.h)) —
   parallel of the in-game `ModuleAutoRegister`. Modules opt into
   glue exposure with a file-scope static. `Game::Lua::RegisterGlueFunction`
   is an alias of `RegisterGlobalFunction` (the engine reads
   `VAR_LUA_STATE` to route, and that pointer is the glue state for
   the duration of our hook).

3. **`Settings::Account` registry** ([src/settings/Account.h](src/settings/Account.h))
   — shared per-account persistence file. NameCache and Order both
   register reset/serialize/parse sections; the registry handles load,
   save, and account-change detection (re-loads on autologin switch
   from the glue screen) in one place. Avoids each module managing
   its own `WTF\Account\<acct>\*.txt` file.

### Notes for callers

- `FUN_GET_LOGIN_ACCOUNT_NAME` (`0x005ABDC0`) is misnamed in
  `Offsets.h` — it actually returns `VAR_CHARACTER_NAME`. The real
  account name pointer is `VAR_ACCOUNT_NAME_PTR` (`0x00BE1C0C`), which
  is what Order and NameCache use.
- WoW.cfg was considered for persistence and rejected: extending the
  cfg parser is more invasive than a self-managed file under WTF,
  and we already have the `Settings::Account` shape.

## 95. `C_CharacterList.*` — removed; notes for a future pickup

We shipped `C_CharacterList.GetNumCharacters` /
`GetCharacterInfo(index)` / `GetCharacters()` for a while —
exposing the realm's character list (name, class, race, level,
area, sex, GUID) from anywhere in the game, not just the glue
char-select screen. The implementation was removed because the
single addon driving the need moved off it; keeping a binding
nobody calls is dead weight, and the engine-hook surface (the
char-list teardown function) is the kind of thing better owned by
whichever DLL actually uses it. The history is preserved in git
(see commits around `src/charlist/Cache.cpp`).

If another project picks this up, the work is straightforward —
no novel reversing needed:

### What the binding looked like

Three Lua entry points, all under `C_CharacterList`:

- `GetNumCharacters() -> n`
- `GetCharacterInfo(index) -> name, localizedRace, localizedClass, level, areaName, englishRace, sex, guid`
- `GetCharacters() -> { { name=, localizedRace=, englishRace=, localizedClass=, level=, areaName=, sex=, guid= }, ... }`

`guid` was the 64-bit character GUID formatted with our
`Guid::FormatAsString` helper (16 hex digits, `"0x"` prefix —
matches `UnitGUID`'s convention). `areaName` was nil when the
character's last-known area wasn't in `AreaTable.dbc`.

### Why it needed a DLL hook

Vanilla 1.12's `GetCharacterInfo` / `GetNumCharacters` are
registered on the **glue** FrameScript engine and the underlying
data at `VAR_CHARLIST_ARRAY` (`0x00B42144`) /
`VAR_CHARLIST_COUNT` (`0x00B42140`) is freed by the
char-list teardown function during the glue→world transition. By
the time addons run in-world, the array is gone and the globals
are zeroed.

The DLL hook took a snapshot of the records before the engine
freed them.

### Implementation summary

1. **Hook the teardown.** `FUN_CHARLIST_FREE` at `0x00472090` —
   `__cdecl void()`, single caller (`FUN_0046AC90`, the
   glue-shutdown wrapper that runs on world transition). Very
   quiet hook target. Pre-hook (snapshot before the original runs);
   the original then zeros the globals.

2. **Iterate the record array.** Flat array, stride `0x120`
   bytes. Field offsets within each record (derived from
   `Script_GetCharacterInfo` disassembly at `0x004732A0`):

   | Offset | Field | Type / notes |
   |--------|-------|---------------|
   | `+0x00` | GUID | u64 (verified via `Script_RenameCharacter` at `0x00473520`) |
   | `+0x08` | name | inline `char[]`, max 12 + NUL |
   | `+0x3C` | areaID | u32, indexes `AreaTable.dbc` |
   | `+0x100` | raceID | u8, indexes `ChrRaces.dbc` |
   | `+0x101` | classID | u8, indexes `ChrClasses.dbc` |
   | `+0x102` | sex | u8 (0/1) |
   | `+0x108` | level | u8 |

3. **Cache memory-only.** Every game launch hits char-select
   before world-enter, so the cache is naturally rebuilt every
   session. No filesystem persistence, no staleness across
   restarts, no deleted-character bookkeeping (a server-side
   delete drops the entry from the next `SMSG_CHAR_ENUM`).

4. **Edge case.** If the DLL is injected mid-session
   post-world-enter, the cache stays empty until the player
   returns to char-select. Under normal VanillaFixes load order
   that's not a concern (DLL loads before any Lua runs).

### Why we used a memory-only cache, not the persistent file

`Settings::Account` (the per-account WTF-file mechanism that backs
`GetSavedCharacterOrder`) is the wrong shape: it persists across
sessions, which would mean stale entries surviving server-side
deletes, and the data is already authoritative on each launch.
Memory-only is simpler and self-correcting.

## 96. EditBox `OnArrowPressed` script handler (+ `SetCursorPosition`) — medium

The 1.12 EditBox widget offers two arrow-key modes via the
`ignoreArrows` attribute, and both are bad for a developer console:

- `ignoreArrows="true"` — widget consumes all four arrows and uses
  UP/DOWN to walk its internal `AddHistoryLine` ring. LEFT/RIGHT
  then can't move the text cursor. This is what
  `ChatFrameEditBoxTemplate` does, which is why you can't move the
  cursor inside a WoW chat input.
- `ignoreArrows="false"` — LEFT/RIGHT move the cursor; UP/DOWN do
  nothing, and there's no Lua-visible event when UP/DOWN are
  pressed. The focused EditBox consumes key events before the
  parent's `OnKeyDown` can fire, and no `OnArrowPressed` handler
  exists on the 1.12 widget (verified in
  `Blizzard-WoW-Interface/1.12.1/FrameXML/UI.xsd` — no element,
  and zero hits across all of Blizzard's 1.12 FrameXML).

So Lua-driven history navigation is impossible: there's no way to
intercept UP/DOWN without also losing LEFT/RIGHT.

### Driving use case

`Octo/Interface/GlueXML/GlueConsole.lua` — the in-glue Lua console
used during glue-side development. Wants the obvious developer-
console UX: UP/DOWN to walk through recently-submitted commands,
LEFT/RIGHT to edit. An in-game Lua console (probable future
addition) wants the same thing.

### What the binding should expose

Mirror modern WoW's `OnArrowPressed` script handler:

```lua
editBox:SetScript("OnArrowPressed", function(self, key)
  -- key = "UP" | "DOWN" | "LEFT" | "RIGHT"
end)
```

Fires when an arrow key is pressed while the EditBox has focus,
regardless of `ignoreArrows`. The widget's normal handling still
runs after — for `ignoreArrows="false"`, LEFT/RIGHT continue to
move the cursor; for `ignoreArrows="true"`, the built-in history
walk still happens (or we can drop the built-in history entirely
once Lua can drive it). The XML element also needs to be
registered under `EditBoxType` so `<OnArrowPressed>...</OnArrowPressed>`
parses alongside `SetScript`.

### Pair with `SetCursorPosition` / `GetCursorPosition`

After UP swaps a history line into the editbox, the Lua side
wants to drop the cursor at end-of-text rather than position 0.
3.3.5 exposes `editBox:SetCursorPosition(n)` and
`editBox:GetCursorPosition()`
([Blizzard-WoW-Interface/3.3.5/FrameXML/AutoComplete.lua:249,289](file:///c:/WoW/Blizzard-WoW-Interface/3.3.5/FrameXML/AutoComplete.lua));
1.12 has neither (zero hits anywhere in
`Blizzard-WoW-Interface/1.12.1`). Same widget, presumably the
same internal cursor field — worth exposing in the same change as
`OnArrowPressed` since the navigation UX is broken without it
(history line appears with cursor at column 0).

### Engine side — not yet researched

Likely path: hook the EditBox key-handler dispatch (whatever
function decides "is this key consumed by the widget? does it
affect the text? move the cursor?"). When key ∈ {UP, DOWN, LEFT,
RIGHT} and the EditBox has focus, fire the new `OnArrowPressed`
script before the default behavior runs.

`SetCursorPosition` is presumably a thin write to a cursor-index
field on the EditBox struct (plus whatever invalidation forces
the caret to redraw at the new position). `GetCursorPosition` is
the matching read. Both should be straightforward once the field
offset is identified — 3.3.5's `Script_*` equivalents are a good
starting point for what the parameter shape and bounds-clamp
behavior should be.

## ~~97. Lift GameTooltip's 30-line cap~~ — DONE

Shipped as [src/tooltip/LinePool.cpp](src/tooltip/LinePool.cpp): pure C++,
no addon Lua. Co-hooks the XML-OnLoad region scan (FUN_00529650, vtable
slot 9) via `Game::HookAutoRegister`; after the engine sets up the
template's 30 lines it creates `TextLeft/Right 31..60` in C++ (alloc →
ctor → name → font-copied-from-prev → anchor, offsets scaled pixel →
internal like Script_SetPoint) and appends them to the tooltip's three
line arrays, bumping `numLinesAllocated`. Runs once per GameTooltip-family
frame at creation + per `/reload`, so it auto-covers GameTooltip,
ShoppingTooltip1/2, ItemRefTooltip, AtlasLootTooltip, WorldMapTooltip, …
Verified in-game: AtlasLoot tooltip rendered a 31st line correctly. Cap is
`kMaxLines = 60`. The design notes below record the RE trail; Approach A
(hook AddLine) / Approach B (addon Lua) were both explored and dropped in
favor of the scan-hook.

Vanilla 1.12's `GameTooltip:AddLine` silently drops any line past the
pool of `TextLeftN`/`TextRightN` FontStrings the XML template
pre-declares (30). Modern (3.3.5+) engines grow the pool on demand.
Addons that append comparison summaries, item-info footers, or aura
details to a full item tooltip get their tail chopped. Practical
example — pfUI's eqcompare block (base-stat deltas inline + extended
stats appended at the bottom) hits the cap on stat-heavy items where
the vendor tooltip already has ~22 lines and the compare block adds
another 10; extended-stat rows past line 30 disappear silently.

Also affects `ShoppingTooltip1`/`ShoppingTooltip2` (same
`GameTooltipTemplate`).

### Root cause (verified in Ghidra on the 1.12 Octo binary)

`AddLine` internal impl at `FUN_00530270` — the shared body called
from both `Script_GameTooltipAddLine` (`FUN_00531630`, method ptr
stored in the GameTooltip method table alongside the `"AddLine"`
string at `0x00854560`) and `Script_GameTooltipAddDoubleLine`:

```c
void FUN_00530270(int this, char *leftText, char *rightText,
                  colorLeft, colorRight, wrapFlag) {
    if ((*(int *)(this + 0x31c) != *(int *)(this + 0x320)) && anyHasText) {
        // set leftText on textLeft[numLines_used],
        // set rightText on textRight[numLines_used],
        // wrapFlag[numLines_used] = wrapFlag,
        // ++numLines_used
    }
    // else: silently drop the line
}
```

`0x320 == 800` decimal, matches decompile. Pool exhausted ⇒ nothing
happens.

### CGameTooltip struct offsets (1.12)

Derived from `FUN_00530270` and the pool-init at `FUN_00529650`:

| Offset | Field                                              |
|--------|----------------------------------------------------|
| `+0x31c` | `numLines` — current write index (0-based)       |
| `+0x320` | `numLinesAllocated` — pool size (30 default)     |
| `+0x328..+0x32c` | `textLeft[]` — array of `CSimpleFontString *`, count `+0x320` |
| `+0x334..+0x338` | `textRight[]` — same, symmetric right column |
| `+0x340..+0x344` | `wrapFlag[]` — per-line wrap bool           |

Actual layout of the array slots: `+0x328` is the first ptr in an
external heap block whose base is `param_1[0xcb]` = `+0x32c`. The
init function reallocates all three arrays whenever `numLinesAllocated`
would change — see `FUN_00536c80` (the reallocator) and `FUN_004368c0`
(a matching alloc for the wrap-flag array of a different element size).

### The pool-init function

`FUN_00529650` — walks `%sTextLeftN` / `%sTextRightN` FontString names
from the FrameXML-declared pool, counts them, reallocates the three
arrays, stores the pointers. Called via vtable slot at `0x00808f84`
(only xref) — likely `SetOwner` or an equivalent virtual reset method
runs it. It's the natural growth path — if we can force it to see a
larger pool of Lua-visible FontStrings and trigger it, the arrays
resize themselves.

### The 3.3.5 reference (already REd)

3.3.5's `GameTooltip::AddLine` at `0x0061fec0` has the grow-on-demand
branch we want to backport. Shape:

```c
if (currentLine == numLinesAllocated - 1) {
    format(buf, 256, "%sTextLeft%d", tooltipName, currentLine + 1);
    auto *fs = SimpleFontString::create(this, /*layer*/2, /*subLayer*/1);
    fs->setName(buf);
    fs->setFontObject(prevLeft->fontObject);
    fs->anchor(TOPLEFT, prevLeft, BOTTOMLEFT, 0.0, calculatedYOffset);
    // ... mirror for TextRight ...
    // ... append fs to textLeft[]/textRight[] at [numLinesAllocated],
    //     then ++numLinesAllocated.
}
// fall through to the existing "set text on line[currentLine]" path
```

3.3.5 helpers (offsets 3.3.5-specific — 1.12 equivalents need to be
re-derived):
- `FUN_00485240` — SimpleFontString constructor `(parent, layer, sublayer)`
- `FUN_0048b6c0` — set-name helper (registers under global `_G[name]`)
- `FUN_0048a260` — anchor helper `(this, anchorPoint, relativeTo, relativePoint, xoff, yoff)`

Struct offsets in 3.3.5 were `+0x2a4` (numLines) / `+0x2a8` (numAlloc) /
`+0x2b4` (textLeft) / `+0x2c0` (textRight) / `+0x2cc` (wrapFlag). All
shifted vs. 1.12 (see above), unsurprising given TBC/WotLK bloat.

### Implementation plan

**Approach A — pre-hook AddLine with a grow-if-needed prelude
(preferred)**:

1. Locate the 1.12 equivalents of the three 3.3.5 helpers. Trace them
   from existing `CSimpleFontString`-creating sites in the engine —
   the CreateFontString API dispatch is a good starting point (it's
   Lua-visible and calls the same constructor).
2. Locate the array reallocators used by `FUN_00529650`
   (`FUN_00536c80` for the two pointer arrays, `FUN_004368c0` for the
   wrap-flag byte array).
3. MinHook `FUN_00530270`. Pre-hook body:
   - If `numLines != numLinesAllocated`, call original — done.
   - Otherwise: realloc the three arrays to `numLinesAllocated + 1`,
     create two new `CSimpleFontString` (left + right), name them
     `"%sTextLeft%d"` / `"%sTextRight%d"` with `tooltip->GetName()` and
     the new index, anchor each below the prior-slot FontString on the
     same side, use the prior FontString's `fontObject` (`+0xd8` in the
     3.3.5 layout — confirm on 1.12) as the default, write the pointers
     into `textLeft[numLinesAllocated]` / `textRight[numLinesAllocated]`,
     zero-init `wrapFlag[numLinesAllocated]`, and bump
     `numLinesAllocated`.
   - Then tail-call the original — it now succeeds normally.

**Approach B — pre-declare a wider pool via Lua** (fallback if the
constructor helpers turn out to be too tangled):

1. In `!!!ClassicAPI` addon, at load time, call
   `GameTooltip:CreateFontString("GameTooltipTextLeft" .. i, "ARTWORK",
   "GameTooltipText")` for `i = 31..60`, plus `TextRightN`, plus set
   anchors matching the pattern in the template.
2. Force the engine's pool-init to re-scan. If we can't reach the
   vtable trigger cleanly, expose a small C entrypoint that walks the
   pool the same way `FUN_00529650` does — takes `tooltip` as arg,
   reallocates arrays, refills.

Approach B is less code but ties the cap to a fixed number (60 lines
is enough for pfUI's use case but arbitrary). Approach A grows on
demand like retail and has no ceiling.

### Testing

- Hover a random-suffix weapon with several equip-spell bonuses (Krol
  Blade, Ironfoe, any ZG/AQ trinket) — tooltip should render >30 lines
  without truncation.
- Hover an item that produces a 40+ line tooltip when pfUI's eqcompare
  block is enabled (Priest Judgement Set chest against another
  Judgement chest: item tooltip ~22 lines + compare block ~10 lines).
- Verify no crash / no visual glitches on:
  - ShoppingTooltip1/2 (same class, same `GameTooltipTemplate`)
  - Rapid re-hovers (create/destroy pressure on the added FontStrings)
  - Item tooltips that stay under 30 lines (pre-existing behavior unchanged)

### Risks

- If the array reallocation logic in `FUN_00536c80` doesn't null-out
  the tail on grow, `textLeft[N]`/`textRight[N]` might contain
  garbage for a frame — anchor before write, or memset to zero.
- FontString lifetime: 3.3.5 leaks the extras for the tooltip
  lifetime (they stay hidden when unused). GameTooltip is a
  singleton, so leak is bounded. Fine.
- Interaction with `pfUI`'s existing tooltip skinning (frame borders,
  status bar reposition, etc.): the layer we're modifying is beneath
  the skinning; shouldn't touch anything visible unless anchor math
  is wrong.

### Related

If we ever consider dropping the whole "GameTooltipTemplate declares
30 FontStrings in XML" scheme and instead having the engine start
with 0 and grow entirely on demand — that would be even cleaner, but
requires patching every consumer of the initial pool (there aren't
many, but they need audit). Filed for future if the incremental grow
turns out to be fragile.

### Investigation update (resolved unknowns)

Traced the pool machinery end-to-end. The open questions above are now
answered, and they point away from both the AddLine hook (Approach A)
and the "trigger the engine's scan" idea — a **hybrid** is cleaner than
either.

**The scan is virtual slot 9, run only at XML-template instantiation.**
`FUN_00529650` is slot 9 (`vtable+0x24`) of the CGameTooltip vtable
(base `0x00808f60`; the null at `0x00808f5c` is the boundary, and slot 2
`0x005368d0` is the `tooltip:method()` dispatcher — confirms the class).
The two vtable-base xrefs are the constructors `FUN_00529240` /
`FUN_00529480`. The scan's prologue calls
`FUN_0076a2f0(node) → FUN_0076a060(node)`, which reads the XML
`"inherits"` attribute and child `"Frames"` — i.e. the scan is the
frame's **XML OnLoad / region-setup** virtual. It runs once when
`GameTooltip` is built from `GameTooltipTemplate` (and again per
`/reload`), **not** on `SetOwner`/show. So we can't cheaply re-trigger
it — the `node` arg is a live XML-parse handle we don't have, and
calling it with a bogus node runs the inherits/Frames path on garbage.

**The scan finds lines by `_G[name]`, so Lua-created FontStrings count.**
`numLinesAllocated` (`+0x320`) is literally "how many contiguous
`%sTextLeftN` **and** `%sTextRightN` exist by name": pass 0 counts (stop
at the first missing index), pass 1 reallocs the three arrays to that
count and fills the pointers. The name lookup `FUN_0076c760` does
`lua_pushstring(name); gettable(_G); if type==TABLE: FrameScript_GetObject`
— a plain `_G[name]` resolve, so a FontString made with
`GameTooltip:CreateFontString("GameTooltipTextLeft31", …)` **is**
visible to it. (Names must be contiguous and exist in *both* columns.)

**The array reallocators are simple and callable from C.**
`FUN_00536c80` (pointer arrays) and `FUN_004368c0` (wrap-flag array) are
the same routine with different SMem tags:
`__thiscall(descriptor, newCount)` where the descriptor is
`{ count@+0x0, cap@+0x4, data@+0x8 }` — reallocs `data` to `newCount*4`
and copies the old contents. In the tooltip the three descriptors are:

| Descriptor `this` | count / cap / data | array |
|-------------------|--------------------|-------|
| `tooltip + 0x324` | `+0x324 / +0x328 / +0x32c` | `textLeft[]`  (`CSimpleFontString*`) |
| `tooltip + 0x330` | `+0x330 / +0x334 / +0x338` | `textRight[]` |
| `tooltip + 0x33c` | `+0x33c / +0x340 / +0x344` | `wrapFlag[]`  (int, 4-byte) |

`numLinesAllocated` (`+0x320`) is a separate field the scan sets equal
to the count; AddLine (`FUN_00530270`) gates on
`numLines(+0x31c) != numLinesAllocated(+0x320)`.

**Recommended approach — Lua creates FontStrings, C grows the arrays
(no hook, no XML re-run).** Replicate the scan's pass 1 directly:

1. **Lua (`!!!ClassicAPI`), at load / `PLAYER_LOGIN`:** for each frame on
   `GameTooltipTemplate` (`GameTooltip`, `ShoppingTooltip1/2`,
   `ItemRefTooltip`, …), create `<name>TextLeft31..N` and
   `<name>TextRight31..N` via `frame:CreateFontString(nm, "ARTWORK",
   "GameTooltipText")`, each anchored `TOPLEFT`/`TOPRIGHT` to the prior
   line's `BOTTOMLEFT`/`BOTTOMRIGHT` (mirror the template's chain).
   This avoids re-deriving the 1.12 FontString ctor / setName / anchor
   helpers that Approach A needed — Lua already wraps them.
2. **C entrypoint** `GrowTooltipPool(tooltipObj, newCount)` (expose as a
   Lua global, called once per frame right after step 1):
   - `FUN_00536c80(tooltip+0x324, newCount)`; set `+0x328 = newCount`.
   - `FUN_00536c80(tooltip+0x330, newCount)`; set `+0x334 = newCount`.
   - `FUN_004368c0(tooltip+0x33c, newCount)`; set `+0x340 = newCount`.
   - for `i` in `[oldAlloc, newCount)`: resolve `<name>TextLeftN`/
     `RightN` via the `_G[name]` dance (same as `FUN_0076c760`), and if
     either is missing stop at `i`; else write the two pointers into the
     new `textLeft`/`textRight` data slots and zero `wrapFlag[i]`.
   - set `numLinesAllocated (+0x320) = i`.
   Resolve `tooltipObj` from the Lua frame the same way our other
   tooltip methods do (`FrameScript_GetObject`).

Runs once per frame per session (re-runs on `/reload`, which recreates
the frames → scan resets to 30 → our addon re-creates + re-grows). No
per-line hook (dodges the MinHook-collision risk on a moderately hot
function), no XML-node fabrication.

Remaining risk to validate in-game: whether AddLine's downstream layout
/ `Show` sizing walks `numLines` FontStrings by their anchors (expected)
vs. any hidden fixed-30 assumption elsewhere; and that the Lua-created
lines inherit height/spacing identical to the template's 1..30 so the
tooltip grows seamlessly. Both are observable on the first >30-line
hover.

## 98. `UnitThreatSituation` / `UnitDetailedThreatSituation` / `GetThreatStatusColor` — NOT FEASIBLE in a client DLL

Modern (WotLK+) exposes three threat APIs:
[`UnitThreatSituation`](https://warcraft.wiki.gg/wiki/API_UnitThreatSituation)
(unit → 0..3 aggro status),
[`UnitDetailedThreatSituation`](https://warcraft.wiki.gg/wiki/API_UnitDetailedThreatSituation)
(isTanking, status, threatPct, rawThreatPct, threatValue), and
[`GetThreatStatusColor`](https://warcraft.wiki.gg/wiki/API_GetThreatStatusColor)
(status → RGB). None of them can be backed by ClassicAPI, because
**the underlying data does not exist on the 1.12 client** — not in a
form a DLL can read, and not by any amount of RE.

### Threat is 100% server-authoritative in vanilla; no threat opcode exists

Vanilla 1.12's threat lives entirely inside the server's
`ThreatManager` (per-creature `ThreatList` of `HostileReference`s —
see Turtle's
[`src/game/Threat/ThreatManager.cpp`](C:\Git\turtle-wow\Dumps\Source%20Code\16%20-%20Development_server\patch_1172\src\game\Threat\ThreatManager.cpp)).
The 3.3.5 client can show a threat meter because WotLK added
`SMSG_THREAT_UPDATE` / `SMSG_HIGHEST_THREAT_UPDATE` (and the client
keeps a per-unit threat table those packets feed). **1.12 has no such
opcode** — the protocol never transmits per-unit threat values, so
there is nothing in client memory to read. This is the same class of
limitation as `C_QuestLog.IsQuestFlaggedCompleted` (TODO #1): the
client simply isn't told. Unlike cast timing (TODO #91, recoverable
from `SMSG_SPELL_START`) or aura casters (recoverable from
`SMSG_SPELL_GO`), there is **no incidental packet** that leaks threat
either — the server computes tanking/aggro decisions internally and
broadcasts none of the numbers.

### How Turtle actually does it — a *server-side* addon-channel protocol

TWThreat (`C:\WoW\Octo\Interface\Addons\TWThreat`) is not reading
client memory; it round-trips through a **custom server patch**:

1. Client → server: the addon `SendAddonMessage("TWT_UDTSv4"[.._TM],
   "limit=NN", "PARTY"/"RAID")` — a normal `CMSG_MESSAGECHAT` addon
   message (`TWThreat.lua:1207` `TWT.UnitDetailedThreatSituation`).
2. Server intercepts the prefix in
   [`ChatHandler.cpp:1051`](turtle-wow\Dumps\Source%20Code\16%20-%20Development_server\patch_1172\src\game\Handlers\ChatHandler.cpp)
   (`strstr(msg, "TWT_UDTSv4")`), validates
   `GetSelectedCreature()->CanHaveThreatList()`, and calls
   `ThreatManager::UnitDetailedThreatSituation(creature, player, limit,
   tankMode)`.
3. That serializer (`ThreatManager.cpp:441`) walks the real
   `getThreatList()` and builds a `TWTv4=name:pct:threat;…` string,
   then `SendAddonMessage`s it back.
4. Client parses it in `CHAT_MSG_ADDON` → `TWT.handleThreatPacket`
   (`TWThreat.lua:790`).

Every threat number the meter shows originated on the server and was
put on the wire by a server-side code change ClassicAPI has no
counterpart to. A client DLL cannot manufacture data the server never
sends.

### Even where a server *does* expose it, the transport is async

The Turtle path is a request/response over the addon chat channel —
fundamentally asynchronous (poll on `OnUpdate`, handle the reply on a
later frame). It cannot back a **synchronous** `UnitThreatSituation()`
that returns a value in the same call, which is exactly why TWThreat is
structured around events and a cached `TWT.threats` table rather than a
blocking query. A ClassicAPI shim would at best have to fake a
synchronous return from a stale cache, and the cache only exists if a
Turtle-style server is feeding it — i.e. the shim would add nothing the
addon isn't already doing in Lua.

### `GetThreatStatusColor` is the only client-pure piece — and it's trivial Lua

`GetThreatStatusColor(status)` is just a fixed 0..3 → RGB map (grey /
yellow / orange / red); no engine data involved. But it's meaningless
without a real `status` to feed it, and it's a two-line Lua table
lookup — no reason to spend a DLL global on it. If we ever ship a
threat polyfill it belongs beside the (Lua, server-dependent) status
source, not in the DLL.

**Verdict:** skip all three. The data is server-only with no vanilla
opcode, the only working implementation (Turtle) is a server patch plus
an async addon-channel protocol that lives correctly in Lua, and the
one client-pure function is a trivial color table. Nothing here is a
client-DLL problem.

### If we synthesized it anyway

"Can't read it" and "can't estimate it" are separate claims. We can't
read threat (above); *estimating* it is possible, but it splits into two
tiers, and neither one is a DLL feature.

**Tier 1 — client-only proxy from the mob's target (small, honest,
nearly useless).** The one threat-correlated fact vanilla *does* sync to
the client is the creature's current target (`UNIT_FIELD_TARGET` in the
descriptor — the same field `"<mob>target"` / `targettarget` reads). A
mob attacks its highest-threat valid target, so `mobTarget == unit` ≈
"unit is tanking". That yields:

- `UnitThreatSituation(unit, mob)` → `3` when the mob targets `unit`,
  else `nil`. It **cannot** produce `1` ("higher than tank, about to
  pull") or distinguish `2` from `3` — those are numeric comparisons and
  we have no numbers.
- `UnitDetailedThreatSituation` → `isTanking` only; `status` degenerate,
  `threatpct` / `rawthreatpct` / `threatvalue` all unavailable.
- Laggy: it changes only when the mob switches target (a descriptor
  update), so it *trails* real threat and gives zero predictive warning
  — the entire reason a threat meter exists.

This needs no DLL: `UnitIsUnit("<mob>target", unit)` already does it in
pure Lua. Shipping it under the real API names would be actively
misleading (callers expect the 0–3 gradient plus percentages), so it's
not worth it even as a stub.

**Tier 2 — a real threat model (accurate-ish, huge, cooperative, ~95%
Lua).** This is what KTM / KLHThreatMeter / vanilla-Omen actually do, and
the only synthesis that fills the percentage/prediction dimensions the
API is *for*:

1. **Model your own threat.** Parse your outgoing combat events and
   convert them to threat via a per-spell coefficient database (damage ≈
   1 threat per point, healing ≈ 0.5/HP applied to *every* mob in combat,
   flat-threat abilities like Sunder Armor / Demo Shout, per-spell
   bonus/penalty coefficients), then apply your active multipliers —
   stance/form (Defensive 1.3×, Berserker 0.8×, Bear/Cat), Righteous Fury
   (+Holy threat), Blessing of Salvation (0.7×), threat-reduction
   trinkets — and handle the sets/wipes: taunt (sets you to current top),
   Feign Death, Vanish, Fade, Cower.
2. **Broadcast** your per-mob estimate over `SendAddonMessage` and
   **aggregate** everyone else's broadcasts into a per-mob table.
3. Derive `isTanking` / `status` / the percentages by comparing each
   member to the current tank against the **110% melee / 130% ranged**
   pull threshold — the exact constants both Turtle's server serializer
   (`ThreatManager.cpp:494`, `threatValue*100 / (tankThreat * (isMelee ?
   1.1 : 1.3))`) and TWThreat's client fallback (`TWThreat.lua:932`)
   use. Worth stealing verbatim so a synthesized `threatpct` lines up
   with what real-threat clients show.

Why it isn't ours to build:

- The coefficient DB is **game-knowledge, not in the binary** — you
  cannot RE it out of the client; it's the classic hand-maintained
  Threat-1.0/KTM data. The parsing, broadcast, and aggregation are all
  Lua. The talent/aura/stance *inputs* are already exposed by existing
  ClassicAPI functions (`GetTalentInfo`, the aura APIs, `IsPlayerSpell`,
  the spell-mod reads). **None of it is a client-DLL problem.**
- **Cooperative-only.** You can precisely model only members running the
  same addon and broadcasting; a non-participating tank is invisible (the
  original KTM caveat). The intuitive objection — "I can see everyone's
  damage in the combat log, why can't I model them from one client?" —
  fails for three compounding reasons, none liftable by the DLL:
  1. **You don't actually see everyone's events.** Vanilla's combat log
     is visibility/range-gated — the server only broadcasts combat events
     for units near you, not the whole raid. A stacked 5-man logs most of
     it (so a single-observer model degrades *gracefully* in dungeons); a
     spread 40-man leaves you blind to the ranged camp, the back-line
     healers, anyone outside your bubble. The DLL can't recover these —
     the server never sends the packets, so there's nothing to hook.
  2. **Threat ≠ damage, and the missing factor is hidden.** Threat is
     `damage × stance × talents × buffs`, not 1:1. The talent factor
     (Defiance, Subtlety, Feral Instinct, Shadow Affinity, Frost
     Channeling — the passive threat-mod talents) is **unreadable for
     other players in vanilla**; there's no inspect path to it. Stance /
     Blessing of Salvation / Fade are at least observable auras when the
     unit is in range, but talents are pure hidden state, so an observer
     can't derive a stranger's coefficient from their damage.
  3. **Much threat has no attributable log line.** In-combat healing
     generates threat on *every* mob the healer is engaged with (not the
     mob the heal touched); buffs / HoTs / PW:Fortitude, flat-threat
     abilities, and pet threat either don't appear in the log or can't be
     pinned to the right mob from a single line.

  Broadcasting sidesteps all three because each client computes threat
  from *private, authoritative* knowledge — its own exact casts, its own
  talents, its own active stance/buffs — then shares the finished number.
  The Tier-1 mob-target read is worth keeping here *only* as a cross-check
  — it reveals who actually holds aggro regardless of who's broadcasting.
- **It's an estimate that drifts.** Addon threat models desync from the
  server's true numbers (rounding, unmodeled effects, out-of-range or
  missed events). That drift is precisely why Blizzard later added
  `SMSG_THREAT_UPDATE` and why Turtle chose a server serializer over
  blessing an addon guess.

**The only DLL-shaped angle — and it's an *input*, not threat.** The one
place a DLL beats Lua is the feed: vanilla's combat log is localized text
keyed by unit *name* (ambiguous with duplicate names, breaks per-locale).
Hooking the damage/heal packet handlers (`SMSG_SPELLNONMELEEDAMAGELOG`,
`SMSG_ATTACKERSTATEUPDATE`, `SMSG_PERIODICAURALOG`) to emit precise,
GUID-keyed, unlocalized damage/heal events would make any Lua threat
model far more robust. But (a) that's a **separate feature** — a
`COMBAT_LOG_EVENT`-style feed, useful well beyond threat — that should be
filed on its own merits; (b) it still only *feeds* the Lua model — threat
stays an estimate, computed cooperatively in Lua; and (c) on the actual
target environment SuperWoW already exposes GUID-keyed combat info +
`UNIT_CASTEVENT`, so even this niche is largely pre-filled.

**Bottom line on synthesis:** Tier 1 is a Lua one-liner too degenerate to
wear the API's name; Tier 2 is a full KTM/Omen reimplementation that is
almost entirely Lua data + logic, cooperative-only, and inherently an
estimate. Anyone who wants vanilla threat should run an existing threat
addon. ClassicAPI's only worthwhile contribution in this space is an
unrelated precise-combat-log event feed — filed as its own thing, not as
"threat".

## 99. `C_AddOns.GetAddOnLocalTable(name)` — gate on loaded + opt-in TOC flag — DONE

Shipped gated (branch `feat/lua-syntax-transpile`,
[src/addons/LocalTable.cpp](src/addons/LocalTable.cpp)). Returns the addon's
private namespace table **only** when BOTH hold, else `nil`:
1. the addon `name` is **loaded** (`FUN_ADDON_IS_LOADED`, by-name), and
2. its `.toc` declares `## AllowAddOnTableAccess: <nonzero>` (scanned via
   `FUN_FILE_READ` on `Interface\AddOns\<name>\<name>.toc`, read as a TOC
   boolean flag; approach (a) from the notes below — no parser hook).

The two paths stay separate, as required:
- **Internal** `__addonns` (the `local name, ns = ...` preamble) stays
  **ungated** — an addon always gets its own namespace. Still in
  `luasyntax/Transpile.cpp`.
- **Public** `C_AddOns.GetAddOnLocalTable` carries the gate, in
  `addons/LocalTable.cpp`.

Both resolve to the SAME table per addon via `LuaSyntax::PushAddonNamespace`
(the one owner of the registry-backed name→table map, exposed by
[src/luasyntax/AddonNamespace.h](src/luasyntax/AddonNamespace.h)) — so an addon
reads back exactly what it (or its files' `...`) stored.

Directive spelling + function name verified present in the 1.15.8 Classic Era
binary (`AllowAddOnTableAccess:` TOC key, `GetAddOnLocalTable`).

Deliberate scope choices (revisit only if an addon needs otherwise):
- **Name (string) input only.** A numeric index returns `nil` — cross-addon
  table access is by name, and the map is name-keyed. Every other `C_AddOns.*`
  wrapper accepts name-or-index; this one does not.
- **`nil` on denial, not a Lua error.** Matches the project's modern-API
  convention (nil over `lua_error`). Retail may raise instead; switch the
  denial branches to `Game::Lua::Error` if strict parity is wanted.
- **No per-name cache** on the TOC scan — GetAddOnLocalTable is not a hot path.

Not shipped by default: no addon opts in yet. The embedded `!!!ClassicAPI`
addon could add `## AllowAddOnTableAccess: 1` to its `.toc` to expose its own
namespace as a worked example, but that's a product/security decision left to
the maintainer.
