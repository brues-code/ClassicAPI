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

#include "Custom.h"

#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"
#include "frame/ScriptArgs.h"
#include "frame/UnitEvent.h"
#include "profile/ScriptTime.h"

#include <cstdint>
#include <cstring>

namespace Event::Custom {

namespace {

// Reservations registered by `AutoReserve` at static-init time, before
// any engine code runs. `RetryClaims` (triggered by the
// `Frame::RegisterEvent` hook) walks this list and claims a NULL slot
// for each unclaimed entry.
//
// MUST stay comfortably above the total number of `AutoReserve` instances
// across the codebase (file-scope + the lazily-registered TTS ones) — the
// constructor SILENTLY drops any reservation past the cap, and which ones
// overflow depends on unspecified cross-TU static-init order, so exceeding
// it makes an arbitrary subset of custom events quietly stop firing (their
// `Lookup` returns -1 and `Fire` no-ops). We hit exactly that at 32: ~40
// reservations meant a shifting handful (e.g. ITEM_DATA_LOAD_RESULT /
// GET_ITEM_INFO_RECEIVED) never claimed a slot. Grep `AutoReserve` before
// bumping the reservation count near this ceiling.
//
// Hit a SECOND time at 64: the codebase reached 65 reservations (60 at file
// scope + the 5 lazy TTS ones), so on any client without VanillaTTS — where
// the TTS five DO construct, and construct last — the 65th
// (VOICE_CHAT_TTS_VOICES_UPDATE) was dropped and its event never fired.
// Count as of the 2026-10-26 live dump: 77.
constexpr int MAX_RESERVED = 96;
struct Reservation {
    const char *name;
    int slot;  // -1 until claimed
    const Game::Doc::Event *doc;  // API documentation; nullptr = undocumented
    int fixedId;  // an `EngineName`'s engine id (its slot for good); -1 = claimed
};
Reservation g_reserved[MAX_RESERVED];
int g_reservedCount = 0;
bool g_writesEnabled = false;
// One-shot latch so a genuinely full event table (no NULL slot left to
// claim) is logged once per session instead of silently dropping the
// event — the failure mode that would otherwise rot unnoticed. Re-armed on
// `/reload` (the table is rebuilt fresh). In practice this never triggers:
// the engine leaves ~115 claimable gaps below 549 (see `TryClaim`) against
// our ~77 reservations, and `Grow` adds room past that. The log is
// purely a backstop if some future install somehow drains the gap pool.
bool g_warnedTableFull = false;

// One grow attempt per in-game table build. Reset in `PrepareForReload`
// (the table is rebuilt fresh on every /reload + logout). See `Grow`.
bool g_growLatched = false;
// Debug: force a grow on the next table build regardless of headroom, so
// the (otherwise dormant) grow path can actually be exercised in-game.
// Armed by `_classicapi_ForceEventGrow`, consumed by the next `Grow`.
bool g_forceGrowNext = false;

// Engine's SStrDup at `FUN_STORM_SSTRDUP` — uses `SMemAlloc` internally
// and is exactly what the engine itself does when filling event table
// entries. Reload teardown's `SMemFree(entry.name)` validates that the
// pointer came from `SMemAlloc`, so handing it an SStrDup'd block is
// the only safe way to inject a name.
char *SStrDup(const char *s) {
    if (s == nullptr)
        return nullptr;
    using SStrDup_t = char *(__stdcall *)(const char *src, const char *file,
                                          int line);
    auto fn = reinterpret_cast<SStrDup_t>(Offsets::FUN_STORM_SSTRDUP);
    return fn(s, __FILE__, __LINE__);
}

// Storm allocator/free — the same pair the engine uses for the event-table
// buffer (see RebuildEventTable), so its reload teardown `SMemFree`s our
// grown buffer exactly as its own. Both `__stdcall`, 4 args (`ret 0x10`).
// SMemAlloc flag bit 8 zero-fills (matching SetEventCount's fresh entries).
void *SMemAlloc(unsigned size) {
    using SMemAlloc_t = void *(__stdcall *)(unsigned size, const char *file,
                                            int line, int flags);
    auto fn = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC);
    return fn(size, __FILE__, __LINE__, 8 /* zero-fill */);
}
void SMemFree(void *ptr) {
    using SMemFree_t = int(__stdcall *)(void *ptr, const char *file, int line,
                                        int flags);
    auto fn = reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE);
    fn(ptr, __FILE__, __LINE__, 0);
}

// True when slot `id` of the live table can take one of our names: its name
// is NULL (no one owns it) AND the engine never signals it by number. A NULL
// name alone is not enough — the glue band (ids below GLUE_EVENT_COUNT) and
// the chat-type id table both fire NULL-named in-game ids, and a name claimed
// there would hear those signals (see GLUE_EVENT_COUNT in Offsets.h).
bool Claimable(const uint8_t *base, int id) {
    if (id < Offsets::GLUE_EVENT_COUNT)
        return false;
    if (Game::Read<const char *>(base + id * Offsets::EVENT_ENTRY_STRIDE,
                                 Offsets::OFF_EVENT_ENTRY_NAME) != nullptr)
        return false;
    for (int t = 0; t < Offsets::CHAT_TYPE_COUNT; ++t)
        if (Game::Read<int>(Offsets::VAR_CHAT_TYPE_EVENT_IDS + t * 4) == id)
            return false;
    return true;
}

// Reservations that need a claimed slot (an `EngineName` has its own).
int ClaimDemand() {
    int demand = 0;
    for (int i = 0; i < g_reservedCount; ++i)
        if (g_reserved[i].fixedId < 0)
            ++demand;
    return demand;
}

// Walk the engine's event table from the LOW end looking for a claimable
// slot. The bulk-init at `0x0051AB30` names 419 of the 549 slots (verified
// against both benilla-re's tables/events.tsv and this exe's initializer);
// the other 130 stay zero-initialized from .data. Less the glue band and
// chat id 540, ~115 are claimable — a contiguous hole at 42..106 alone
// covers most of our reservations.
//
// We go LOW (ascending) deliberately: the other DLLs own the HIGH range —
// nampower writes fixed ids 549..599 and 650..655, SuperWoW 600 (per
// nampower's own table), WeirdUtils scans up from 650, and both nampower and
// SuperWoW clamp the table to 700 — and VanillaMinimapTracking walks from the
// tail. Below 549 only nampower writes, at 369 and 540, both before any Lua
// runs, so skip-occupied steps over them. Claiming low keeps us out of
// everyone's way and independent of their clamp / hook order.
int TryClaim(const char *eventName) {
    if (!g_writesEnabled)
        return -1;
    auto *base = Game::Read<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = Game::Read<int>(Offsets::VAR_EVENT_TABLE_COUNT);
    if (base == nullptr || count <= 0)
        return -1;
    for (int i = 0; i < count; ++i) {
        if (Claimable(base, i)) {
            auto **namePtr = Game::Ptr<const char *>(
                base + i * Offsets::EVENT_ENTRY_STRIDE,
                Offsets::OFF_EVENT_ENTRY_NAME);
            char *copy = SStrDup(eventName);
            if (copy == nullptr)
                return -1;
            *namePtr = copy;
            return i;
        }
    }
    // Writes are enabled and the table is valid, but every slot is taken —
    // real capacity exhaustion despite `Grow`. Warn once so it's diagnosable.
    if (!g_warnedTableFull) {
        g_warnedTableFull = true;
        Debug::Log::Printf(
            "[event] table full (%d slots, no NULL left) — cannot claim '%s'"
            " and any further custom events; even Grow couldn't make room.",
            count, eventName ? eventName : "?");
    }
    return -1;
}

// On-demand event-table grow. Correctly TIMED: called once per in-game
// table build from `RetryClaims` (the `frame:RegisterEvent` hook), on the
// FIRST such call — which is AFTER `RebuildEventTable` has built the fresh
// table (frames can only register once it exists) but BEFORE any frame has
// joined a chain (it's the first registration, and we run before the
// original registers it). So every chain is still an empty self-sentinel,
// and the buffer can be moved safely.
//
// (This is what the removed `EnsureCapacity` got wrong: it ran from
// `LoadScriptFunctions_h`, BEFORE `RebuildEventTable`, so it saw the stale
// previous/glue table and always aborted — it never actually grew. The
// realloc logic itself was fine; only the timing was broken.)
//
// Grows only when the claimable slots are fewer than our reservations —
// which never happens in practice (~115 below 549 alone vs ~77
// reservations), so this is a dormant durability valve. `g_forceGrowNext`
// (debug) forces it so the path can be exercised. It grows by reallocating
// the entry buffer with the engine's own Storm allocator and swapping the
// `{cap@0, count@4, base@8}` globals at `0x00CEEF60` — bypassing
// SetEventCount's destructive `= 700` clamp. All consumers (RegisterEvent,
// the dispatcher, SuperWoW, nampower) read the base pointer fresh, so the
// swap is picked up everywhere; the engine's reload teardown `SMemFree`s
// our buffer and frees the (shared) name strings via its per-entry helper,
// exactly as its own.
void Grow() {
    if (g_growLatched || !g_writesEnabled)
        return;
    g_growLatched = true; // one attempt per build, whether or not it grows

    const int count = Game::Read<int>(Offsets::VAR_EVENT_TABLE_COUNT);
    auto *base = Game::Read<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int demand = ClaimDemand();
    if (base == nullptr || count <= 0 || demand <= 0)
        return;

    int nulls = 0;
    for (int i = 0; i < count; ++i)
        if (Claimable(base, i))
            ++nulls;

    const bool force = g_forceGrowNext;
    g_forceGrowNext = false;
    if (nulls >= demand && !force)
        return; // plenty of room — the common (only) real case

    // Growing moves the buffer, invalidating every entry's self-referential
    // list anchor, so it's safe ONLY while all chains are empty. We run
    // before the first registration, so that holds — but VERIFY it (a live
    // chain means someone registered earlier via a path we didn't intercept;
    // moving the buffer would corrupt their subscription). Abort if so and
    // fall back to claim-NULL.
    for (int i = 0; i < count; ++i) {
        const uint32_t head = Game::Read<uint32_t>(
            base + i * Offsets::EVENT_ENTRY_STRIDE, Offsets::OFF_EVENT_ENTRY_HEAD);
        if (head != 0 && (head & 1) == 0) { // populated chain (not 0 / sentinel)
            Debug::Log::Printf(
                "[event] grow aborted — chain live at slot %d (not first-reg)", i);
            return;
        }
    }

    constexpr int kMargin = 16;
    const int deficit = (nulls < demand) ? (demand - nulls) : 0;
    const int newCount = count + deficit + kMargin;
    auto *newBuf = static_cast<uint8_t *>(
        SMemAlloc(static_cast<unsigned>(newCount) * Offsets::EVENT_ENTRY_STRIDE));
    if (newBuf == nullptr)
        return; // alloc failed — keep the old table; claiming still works

    // Rebuild each entry at its NEW address, mirroring SetEventCount's own
    // per-entry init: copy the name pointer (shared string, not duplicated);
    // the `+0x04` field stays 0 (zero-filled, matching the engine); re-init
    // the anchor as an empty self-sentinel at the new location. Old entries
    // past the copy and all new entries are already zero-filled + get fresh
    // anchors. Safe because every chain is empty.
    for (int i = 0; i < newCount; ++i) {
        auto *e = reinterpret_cast<uint32_t *>(
            newBuf + i * Offsets::EVENT_ENTRY_STRIDE);
        if (i < count) // copy name; new slots stay null
            e[0] = *reinterpret_cast<const uint32_t *>(
                base + i * Offsets::EVENT_ENTRY_STRIDE);
        const uint32_t anchor = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&e[2])); // entry+0x08
        e[2] = anchor;      // self-referential list anchor
        e[3] = anchor | 1;  // empty-chain sentinel head (+0x0C)
    }

    // Publish base BEFORE count so the globals never advertise more entries
    // than the buffer holds. Then free the OLD buffer only — the name
    // strings live on in newBuf and are freed by the reload teardown.
    Game::Ref<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR) = newBuf;
    Game::Ref<int>(Offsets::VAR_EVENT_TABLE_CAP) = newCount;
    Game::Ref<int>(Offsets::VAR_EVENT_TABLE_COUNT) = newCount;
    SMemFree(base);

    Debug::Log::Printf("[event] grew table %d -> %d (%d NULL, %d reserved%s)",
                       count, newCount, nulls, demand,
                       force ? ", FORCED" : "");
}

} // namespace

AutoReserve::AutoReserve(const char *name, const Game::Doc::Event *doc) {
    if (name == nullptr)
        return;
    // Dedup: a name reserved twice resolves both instances to the one entry.
    // A descriptor on either instance documents the one reservation (an event
    // fired from several modules is declared in a shared header, but a second
    // declaration must not silently drop its documentation).
    for (int i = 0; i < g_reservedCount; ++i) {
        if (std::strcmp(g_reserved[i].name, name) == 0) {
            index_ = i;
            if (g_reserved[i].doc == nullptr)
                g_reserved[i].doc = doc;
            return;
        }
    }
    if (g_reservedCount >= MAX_RESERVED)
        return; // overflow — index_ stays -1, Slot() reports unclaimed
    g_reserved[g_reservedCount].name = name;
    g_reserved[g_reservedCount].slot = -1;
    g_reserved[g_reservedCount].doc = doc;
    g_reserved[g_reservedCount].fixedId = -1;
    index_ = g_reservedCount;
    ++g_reservedCount;
}

EngineName::EngineName(int id, const char *name, const Game::Doc::Event *doc) {
    if (name == nullptr || id < 0 || id >= Offsets::EVENT_NAME_TABLE_STATIC_COUNT ||
        g_reservedCount >= MAX_RESERVED)
        return;
    auto **slot = reinterpret_cast<const char **>(
        static_cast<uintptr_t>(Offsets::VAR_EVENT_NAME_TABLE_STATIC)) + id;
    if (*slot != nullptr)
        return; // the engine names it; never take an engine event's slot
    *slot = name;
    g_reserved[g_reservedCount] = {name, id, doc, id};
    ++g_reservedCount;
}

int AutoReserve::Slot() const {
    return index_ >= 0 ? g_reserved[index_].slot : -1;
}

int ReservedCount() { return g_reservedCount; }

const char *ReservedName(int index) {
    return (index >= 0 && index < g_reservedCount) ? g_reserved[index].name : nullptr;
}

const Game::Doc::Event *ReservedDoc(int index) {
    return (index >= 0 && index < g_reservedCount) ? g_reserved[index].doc : nullptr;
}

int LookupByName(const char *name) {
    if (name == nullptr)
        return -1;
    auto *base = Game::Read<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = Game::Read<int>(Offsets::VAR_EVENT_TABLE_COUNT);
    if (base == nullptr || count <= 0)
        return -1;
    for (int i = 0; i < count; ++i) {
        const char *entryName = Game::Read<const char *>(
            base + i * Offsets::EVENT_ENTRY_STRIDE,
            Offsets::OFF_EVENT_ENTRY_NAME);
        if (entryName != nullptr && std::strcmp(entryName, name) == 0)
            return i;
    }
    return -1;
}

bool HasListeners(int slot) {
    if (slot < 0)
        return false;
    auto *base = Game::Read<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = Game::Read<int>(Offsets::VAR_EVENT_TABLE_COUNT);
    if (base == nullptr || slot >= count)
        return false;
    // The entry's chain head (`+0x0C`): a populated subscriber chain is a
    // non-zero pointer with the low bit clear; `0` or an odd (tagged)
    // value is the empty self-sentinel — same test the grow-safety scan
    // uses. So this is true iff at least one frame registered for the event.
    const uint32_t head = Game::Read<uint32_t>(
        base + slot * Offsets::EVENT_ENTRY_STRIDE, Offsets::OFF_EVENT_ENTRY_HEAD);
    return head != 0 && (head & 1) == 0;
}

void FireValues(int slot, void *L, int firstArgIdx, int nargs) {
    // A signal, listened to or not (3.3.5's FrameScript_SignalEvent counts first).
    Profile::ScriptTime::CountSignal();
    if (L == nullptr || !HasListeners(slot))
        return;
    using namespace Game::Lua;
    const uint8_t *entry = Game::Read<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR) +
                           slot * Offsets::EVENT_ENTRY_STRIDE;
    const char *name = Game::Read<const char *>(entry, Offsets::OFF_EVENT_ENTRY_NAME);
    if (name == nullptr || CheckStack(L, 4) == 0)
        return;
    // 0x00703F50: save `event`, set it to the event's name, run each
    // listener's OnEvent slot (frame + 0xC) reading the next node before the
    // call, then restore `event`. The saved value lives on the stack.
    const int top = GetTop(L);
    PushString(L, "event");
    RawGet(L, GLOBALS_INDEX); // [.. oldEvent]
    PushString(L, "event");
    PushString(L, name);
    SetTable(L, GLOBALS_INDEX);
    // A string arg1 meets RegisterUnitEvent filters, which the engine's
    // dispatches meet at the runner this walk never calls.
    const char *unit = nargs > 0 && Type(L, firstArgIdx) == TYPE_STRING
                           ? ToString(L, firstArgIdx)
                           : nullptr;
    uintptr_t node = Game::Read<uintptr_t>(entry, Offsets::OFF_EVENT_ENTRY_HEAD);
    while (node != 0 && (node & 1) == 0) {
        const uintptr_t next = Game::Read<uintptr_t>(node + Offsets::OFF_EVENT_NODE_NEXT);
        auto *frame = Game::Read<uint8_t *>(node + Offsets::OFF_EVENT_NODE_FRAME);
        if (frame != nullptr && (unit == nullptr || Frame::UnitEvent::Accepts(frame, name, unit)))
            Frame::ScriptArgs::RunSlotWithValues(
                L, reinterpret_cast<const uint32_t *>(frame + Offsets::OFF_FRAME_ONEVENT_SLOT),
                frame, firstArgIdx, nargs);
        node = next;
    }
    PushString(L, "event");
    PushValue(L, top + 1);
    SetTable(L, GLOBALS_INDEX);
    SetTop(L, top);
}

void RetryClaims() {
    // Grow the table first if the low gap pool is short (dormant valve).
    // Runs at most once per table build; on the first call every chain is
    // still empty (this is the first registration), which is the only safe
    // time to move the buffer.
    Grow();
    for (int i = 0; i < g_reservedCount; ++i) {
        if (g_reserved[i].slot < 0)
            g_reserved[i].slot = TryClaim(g_reserved[i].name);
    }
}

void EnableWrites() { g_writesEnabled = true; }

void PrepareForReload() {
    for (int i = 0; i < g_reservedCount; ++i)
        g_reserved[i].slot = g_reserved[i].fixedId;
    g_writesEnabled = false;
    g_warnedTableFull = false;  // fresh table after the rebuild — re-arm
    g_growLatched = false;      // re-arm the one-shot grow for the new table
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

// Diagnostic Lua functions — registered via the auto-register pattern
// at the bottom of this file. Not on any hot path.
namespace {

int __fastcall Script_DumpSlot(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Debug::Log::Printf("[dumpslot] usage: _classicapi_DumpSlot(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    auto *base = Game::Read<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = Game::Read<int>(Offsets::VAR_EVENT_TABLE_COUNT);
    const char *name = nullptr;
    if (base != nullptr && slot >= 0 && slot < count) {
        name = Game::Read<const char *>(
            base + slot * Offsets::EVENT_ENTRY_STRIDE,
            Offsets::OFF_EVENT_ENTRY_NAME);
    }
    Debug::Log::Printf("[dumpslot] base=0x%08X count=%d slot=%d name='%s'",
                       static_cast<unsigned>(reinterpret_cast<uintptr_t>(base)),
                       count, slot, name ? name : "(null)");
    return 0;
}

int __fastcall Script_DumpAllEvents(void *) {
    auto *base = Game::Read<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = Game::Read<int>(Offsets::VAR_EVENT_TABLE_COUNT);
    Debug::Log::Printf("[dumpall] base=0x%08X count=%d",
                       static_cast<unsigned>(reinterpret_cast<uintptr_t>(base)),
                       count);
    if (base == nullptr)
        return 0;
    for (int i = 0; i < count; ++i) {
        const char *name = Game::Read<const char *>(
            base + i * Offsets::EVENT_ENTRY_STRIDE,
            Offsets::OFF_EVENT_ENTRY_NAME);
        if (name != nullptr && *name != '\0')
            Debug::Log::Printf("[%d] %s", i, name);
    }
    Debug::Log::Printf("[dumpall] done");
    return 0;
}

// `_classicapi_ForceEventGrow()` — arms a one-shot forced table grow for
// the NEXT in-game table build, so the (otherwise dormant) grow path can be
// exercised: call it, then `/reload`. The grow fires at the first
// `RegisterEvent` after the rebuild (chains still empty) and logs
// `[event] grew table N -> M (..., FORCED)`. Diagnostic only.
int __fastcall Script_ForceEventGrow(void *) {
    g_forceGrowNext = true;
    Debug::Log::Printf(
        "[event] force-grow armed — /reload to trigger it on the next build");
    return 0;
}

void RegisterDiagnostics() {
    Game::Lua::RegisterGlobalFunction("_classicapi_DumpSlot", &Script_DumpSlot);
    Game::Lua::RegisterGlobalFunction("_classicapi_DumpAllEvents",
                                      &Script_DumpAllEvents);
    Game::Lua::RegisterGlobalFunction("_classicapi_ForceEventGrow",
                                      &Script_ForceEventGrow);
}

const Game::ModuleAutoRegister _autoreg{&RegisterDiagnostics};

} // namespace

} // namespace Event::Custom
