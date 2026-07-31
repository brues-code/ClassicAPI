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
constexpr int MAX_RESERVED = 64;
struct ReservedName {
    const char *name;
    int slot;  // -1 until claimed
};
ReservedName g_reserved[MAX_RESERVED];
int g_reservedCount = 0;
bool g_writesEnabled = false;
// One-shot latch so a genuinely full event table (no NULL slot left to
// claim) is logged once per session instead of silently dropping the
// event — the failure mode that would otherwise rot unnoticed. Re-armed on
// `/reload` (the table is rebuilt fresh). In practice this never triggers:
// SuperWoW/nampower clamp the FrameXML table to 700 and the engine leaves
// ~200 NULL gaps in the low range, dwarfing our ~46 reservations. The log is
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
    auto fn = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEMALLOC);
    return fn(size, __FILE__, __LINE__, 8 /* zero-fill */);
}
void SMemFree(void *ptr) {
    using SMemFree_t = int(__stdcall *)(void *ptr, const char *file, int line,
                                        int flags);
    auto fn = reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEMFREE);
    fn(ptr, __FILE__, __LINE__, 0);
}

// Walk the engine's event table from the LOW end looking for a NULL-name
// slot. The bulk-init at `0x0051AB30` leaves ~200 NULL gaps scattered
// through the 548-slot input names array (it only writes ~350 of the
// slots explicitly; the rest stay zero-initialized from .data) — a big
// contiguous hole around 42..106 alone dwarfs our reservation count.
//
// We go LOW (ascending) deliberately: SuperWoW and nampower own the HIGH
// range (fixed event IDs 549..655, and they destructively clamp the table
// to 700), and other claim-based DLLs (VanillaMinimapTracking) also walk
// from the tail — so the top is the contested end. Claiming from the low
// engine gaps keeps us entirely out of their way and independent of their
// clamp / hook order. The engine's ~200 low NULL gaps dwarf our ~46
// reservations, so a free slot is essentially always available; the
// skip-occupied invariant keeps this safe against future engine additions
// regardless of direction.
int TryClaim(const char *eventName) {
    if (!g_writesEnabled)
        return -1;
    auto *base = *reinterpret_cast<uint8_t **>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_COUNT);
    if (base == nullptr || count <= 0)
        return -1;
    for (int i = 0; i < count; ++i) {
        auto **namePtr = reinterpret_cast<const char **>(
            base + i * Offsets::EVENT_ENTRY_STRIDE +
            Offsets::OFF_EVENT_ENTRY_NAME);
        if (*namePtr == nullptr) {
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
// Grows only when the engine's low NULL gaps are fewer than our
// reservations — which never happens in practice (~200 gaps vs ~46
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

    const int count = *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_COUNT);
    auto *base = *reinterpret_cast<uint8_t **>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    if (base == nullptr || count <= 0 || g_reservedCount <= 0)
        return;

    int nulls = 0;
    for (int i = 0; i < count; ++i)
        if (*reinterpret_cast<const char *const *>(
                base + i * Offsets::EVENT_ENTRY_STRIDE +
                Offsets::OFF_EVENT_ENTRY_NAME) == nullptr)
            ++nulls;

    const bool force = g_forceGrowNext;
    g_forceGrowNext = false;
    if (nulls >= g_reservedCount && !force)
        return; // plenty of room — the common (only) real case

    // Growing moves the buffer, invalidating every entry's self-referential
    // list anchor, so it's safe ONLY while all chains are empty. We run
    // before the first registration, so that holds — but VERIFY it (a live
    // chain means someone registered earlier via a path we didn't intercept;
    // moving the buffer would corrupt their subscription). Abort if so and
    // fall back to claim-NULL.
    for (int i = 0; i < count; ++i) {
        const uint32_t head = *reinterpret_cast<const uint32_t *>(
            base + i * Offsets::EVENT_ENTRY_STRIDE + Offsets::OFF_EVENT_ENTRY_HEAD);
        if (head != 0 && (head & 1) == 0) { // populated chain (not 0 / sentinel)
            Debug::Log::Printf(
                "[event] grow aborted — chain live at slot %d (not first-reg)", i);
            return;
        }
    }

    constexpr int kMargin = 16;
    const int deficit = (nulls < g_reservedCount) ? (g_reservedCount - nulls) : 0;
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
    *reinterpret_cast<uint8_t **>(Offsets::VAR_EVENT_TABLE_BASE_PTR) = newBuf;
    *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_CAP) = newCount;
    *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_COUNT) = newCount;
    SMemFree(base);

    Debug::Log::Printf("[event] grew table %d -> %d (%d NULL, %d reserved%s)",
                       count, newCount, nulls, g_reservedCount,
                       force ? ", FORCED" : "");
}

} // namespace

AutoReserve::AutoReserve(const char *name) {
    if (name == nullptr || g_reservedCount >= MAX_RESERVED)
        return;
    for (int i = 0; i < g_reservedCount; ++i) {
        if (std::strcmp(g_reserved[i].name, name) == 0)
            return;
    }
    g_reserved[g_reservedCount].name = name;
    g_reserved[g_reservedCount].slot = -1;
    ++g_reservedCount;
}

int Lookup(const char *name) {
    if (name == nullptr)
        return -1;
    for (int i = 0; i < g_reservedCount; ++i) {
        if (std::strcmp(g_reserved[i].name, name) == 0)
            return g_reserved[i].slot;
    }
    return -1;
}

int LookupByName(const char *name) {
    if (name == nullptr)
        return -1;
    auto *base = *reinterpret_cast<uint8_t **>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_COUNT);
    if (base == nullptr || count <= 0)
        return -1;
    for (int i = 0; i < count; ++i) {
        const char *entryName = *reinterpret_cast<const char *const *>(
            base + i * Offsets::EVENT_ENTRY_STRIDE +
            Offsets::OFF_EVENT_ENTRY_NAME);
        if (entryName != nullptr && std::strcmp(entryName, name) == 0)
            return i;
    }
    return -1;
}

bool HasListeners(int slot) {
    if (slot < 0)
        return false;
    auto *base = *reinterpret_cast<uint8_t **>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_COUNT);
    if (base == nullptr || slot >= count)
        return false;
    // The entry's chain head (`+0x0C`): a populated subscriber chain is a
    // non-zero pointer with the low bit clear; `0` or an odd (tagged)
    // value is the empty self-sentinel — same test the grow-safety scan
    // uses. So this is true iff at least one frame registered for the event.
    const uint32_t head = *reinterpret_cast<const uint32_t *>(
        base + slot * Offsets::EVENT_ENTRY_STRIDE + Offsets::OFF_EVENT_ENTRY_HEAD);
    return head != 0 && (head & 1) == 0;
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
        g_reserved[i].slot = -1;
    g_writesEnabled = false;
    g_warnedTableFull = false;  // fresh table after the rebuild — re-arm
    g_growLatched = false;      // re-arm the one-shot grow for the new table
}

// Diagnostic Lua functions — registered via the auto-register pattern
// at the bottom of this file. Not on any hot path.
namespace {

int __fastcall Script_DumpSlot(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Debug::Log::Printf("[dumpslot] usage: _classicapi_DumpSlot(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    auto *base = *reinterpret_cast<uint8_t **>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_COUNT);
    const char *name = nullptr;
    if (base != nullptr && slot >= 0 && slot < count) {
        name = *reinterpret_cast<const char *const *>(
            base + slot * Offsets::EVENT_ENTRY_STRIDE +
            Offsets::OFF_EVENT_ENTRY_NAME);
    }
    Debug::Log::Printf("[dumpslot] base=0x%08X count=%d slot=%d name='%s'",
                       static_cast<unsigned>(reinterpret_cast<uintptr_t>(base)),
                       count, slot, name ? name : "(null)");
    return 0;
}

int __fastcall Script_DumpAllEvents(void *) {
    auto *base = *reinterpret_cast<uint8_t **>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_COUNT);
    Debug::Log::Printf("[dumpall] base=0x%08X count=%d",
                       static_cast<unsigned>(reinterpret_cast<uintptr_t>(base)),
                       count);
    if (base == nullptr)
        return 0;
    for (int i = 0; i < count; ++i) {
        const char *name = *reinterpret_cast<const char *const *>(
            base + i * Offsets::EVENT_ENTRY_STRIDE +
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
