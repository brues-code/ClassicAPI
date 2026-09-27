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

// `GameTooltip:HookScript("OnTooltipSet{Item,Spell,Unit,GameObject}", fn)` —
// backport of the modern tooltip scripts that fire whenever a tooltip's item /
// spell / unit / gameobject is set, so addons can annotate tooltips by hooking
// one script instead of wrapping every Set* method. Fully native — real frame
// scripts, settable via the standard SetScript / GetScript / HookScript.
//
// 1.12 already has the machinery for tooltip scripts, just not these ones:
//
//   - The CGGameTooltip vtable resolver FUN_GAMETOOLTIP_SCRIPT_RESOLVER maps a
//     script name to its handler slot on the tooltip (OnTooltipCleared etc.,
//     each an 8-byte {handler, context} pair). SetScript/GetScript/HookScript
//     all go through it. We co-hook it: for our OnTooltipSet* names (which the
//     engine doesn't know) we hand back a pointer to a C-side per-tooltip cell
//     — the tooltip object (alloc size 0x460) has no free 8-byte slot, and the
//     resolver's only contract is "return the address the script lives at", so
//     external storage works exactly like an in-object slot.
//
//   - Each object type has a single builder funnel that every Set*/mouseover
//     path converges on (item FUN_GAMETOOLTIP_BUILD_ITEM, spell _BUILD_SPELL,
//     unit _BUILD_UNIT, gameobject _BUILD_GAMEOBJECT). We co-hook each and,
//     after the build, fire the matching handler via FUN_FRAME_INVOKE_SCRIPT
//     (the same no-arg runner the clear uses for OnTooltipCleared) with the
//     tooltip as `self`.
//
// Suppressed during our own Tooltip::Compare equipped-item build (see
// Suppressor), and guarded against a handler that re-enters a builder.

#include "SetEvents.h"

#include "Game.h"
#include "Offsets.h"
#include "unit/Tooltip.h"

#include <cstdint>

namespace Tooltip::SetEvents {

namespace {

// One entry per script kind. Order is arbitrary but the cell slot array and
// the builder hooks index by it.
enum ScriptKind { SK_ITEM = 0, SK_SPELL, SK_UNIT, SK_GAMEOBJECT, SK_COUNT };

// Script names, matched case-insensitively against the resolver's input.
constexpr const char *kNamesLower[SK_COUNT] = {
    "ontooltipsetitem",
    "ontooltipsetspell",
    "ontooltipsetunit",
    "ontooltipsetgameobject",
};

// Per-tooltip storage: one 8-byte {handler, context} slot per script kind.
// Tooltips are few (GameTooltip, ItemRefTooltip, ShoppingTooltip1/2,
// AtlasLootTooltip, WorldMapTooltip, plus the odd addon tooltip), so a small
// fixed table with a linear scan is plenty and gives each cell a stable
// address to hand out.
//
// Few — but NOT permanent: every /reload (and logout) destroys and recreates
// the tooltip objects, usually at new addresses. `PrepareForReload` clears the
// table at each UI teardown; without it the table gained ~2 dead cells per
// reload until full (≈18 reloads — SetScript then failed with "doesn't have a
// 'OnTooltipSetItem' script") and a stale cell whose address the allocator
// recycled into a new tooltip fired its dead handler ref → crash (issue #33).
// The clear is also semantically right: handler refs never survive the Lua
// reset, so post-reload every cell held garbage regardless.
struct Cell {
    void *tooltip;
    uint32_t slot[SK_COUNT][2]; // [kind] = {handler, exec context}
};
constexpr int kMaxTooltips = 32;
Cell g_cells[kMaxTooltips];
int g_cellCount = 0;

uint32_t *SlotFor(void *tooltip, int kind, bool create) {
    for (int i = 0; i < g_cellCount; ++i)
        if (g_cells[i].tooltip == tooltip)
            return g_cells[i].slot[kind];
    if (!create || g_cellCount >= kMaxTooltips)
        return nullptr;
    Cell &c = g_cells[g_cellCount++];
    c.tooltip = tooltip;
    for (auto &s : c.slot) {
        s[0] = 0;
        s[1] = 0;
    }
    return c.slot[kind];
}

bool EqualsIgnoreCase(const char *s, const char *literal) {
    if (s == nullptr)
        return false;
    for (;; ++s, ++literal) {
        unsigned char a = static_cast<unsigned char>(*s);
        const unsigned char b = static_cast<unsigned char>(*literal);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a + 32);
        if (a != b)
            return false;
        if (b == 0)
            return true;
    }
}

// --- Suppression / recursion state -------------------------------------
int g_suppress = 0; // > 0 while our own compare build runs
int g_firing = 0;   // recursion guard around any handler fire

// The engine's real script invoker: binds the global `this` = frame and runs
// the handler under its own protected lua_pcall. We call it directly rather
// than the FUN_00702690 wrapper, whose exec-context mutation is unsafe from the
// deep nested Lua->C->Lua stack our co-hook fires within — see Offsets.h.
using InvokeScript_t = void(__fastcall *)(uint32_t handler, void *frame);

// Fire the handler (if any) for `kind` on `self`.
//
// We fire from inside a builder, which is itself mid-execution of the engine's
// Script_Set* (and, in the wild, nested under addon Set* wrappers + several
// other DLLs' hooks). The invoker pushes onto the Lua stack and does NOT
// restore the top to where the outer, still-running C code left it — harmless
// from the engine's top-level dispatch, but from here it shifts the stack and
// the outer code (and other DLLs' hooks) then index garbage → deref → crash.
// Snapshot and restore the stack top around the fire so the outer code sees
// exactly the stack it left.
void FireScript(void *self, int kind) {
    if (g_suppress > 0 || g_firing > 0)
        return;
    uint32_t *slot = SlotFor(self, kind, /*create*/ false);
    if (slot == nullptr || slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    const int savedTop = (L != nullptr) ? Game::Lua::GetTop(L) : 0;
    ++g_firing;
    reinterpret_cast<InvokeScript_t>(Offsets::FUN_FRAME_INVOKE_SCRIPT)(slot[0], self);
    --g_firing;
    if (L != nullptr)
        Game::Lua::SetTop(L, savedTop);
}

// --- Resolver co-hook (CGGameTooltip vtable script-name → slot) ---------
using Resolver_t = int(__fastcall *)(void *self, void *edx, const char *name);
Resolver_t g_resolverOriginal = nullptr;

int __fastcall Resolver_h(void *self, void *edx, const char *name) {
    const int engineSlot = g_resolverOriginal(self, edx, name);
    if (engineSlot != 0) // a base-frame or existing tooltip script — leave it
        return engineSlot;
    for (int k = 0; k < SK_COUNT; ++k)
        if (EqualsIgnoreCase(name, kNamesLower[k]))
            return reinterpret_cast<int>(SlotFor(self, k, /*create*/ true));
    return 0;
}

// --- Builder co-hooks --------------------------------------------------
// Each builder is a __thiscall funnel modelled as __fastcall(self /*ecx*/,
// edx /*unused reg placeholder*/, ...real stack args...). The declared stack
// arg count MUST match the callee's RET N or the caller's stack is corrupted.
// Builders whose callers use the return value (spell, unit) are declared to
// return it so the co-hook forwards it.

// Item — FUN_GAMETOOLTIP_BUILD_ITEM, 9 stack args, return unused.
using BuildItem_t = void(__fastcall *)(void *self, void *edx, uint32_t itemID,
                                       const void *guid, const void *guid2, int a4,
                                       int a5, int a6, int headerFlag, int a8, int a9);
BuildItem_t g_buildItemOriginal = nullptr;

void __fastcall BuildItem_h(void *self, void *edx, uint32_t itemID, const void *guid,
                            const void *guid2, int a4, int a5, int a6, int headerFlag,
                            int a8, int a9) {
    g_buildItemOriginal(self, edx, itemID, guid, guid2, a4, a5, a6, headerFlag, a8, a9);
    FireScript(self, SK_ITEM);
}

// Spell — FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP, 7 stack args, returns an int.
using BuildSpell_t = int(__fastcall *)(void *self, void *edx, int spellID, int a2,
                                       int a3, int isPet, int a5, int a6, int a7);
BuildSpell_t g_buildSpellOriginal = nullptr;

int __fastcall BuildSpell_h(void *self, void *edx, int spellID, int a2, int a3,
                            int isPet, int a5, int a6, int a7) {
    const int ret = g_buildSpellOriginal(self, edx, spellID, a2, a3, isPet, a5, a6, a7);
    FireScript(self, SK_SPELL);
    return ret;
}

// Unit — FUN_GAMETOOLTIP_BUILD_UNIT, 1 stack arg (guid), returns an int the
// caller (Script_GameTooltip_SetUnit) tests.
using BuildUnit_t = int(__fastcall *)(void *self, void *edx, const void *guid);
BuildUnit_t g_buildUnitOriginal = nullptr;

int __fastcall BuildUnit_h(void *self, void *edx, const void *guid) {
    Unit::Tooltip::BeforeBuild(self, static_cast<const uint32_t *>(guid));
    const int ret = g_buildUnitOriginal(self, edx, guid);
    FireScript(self, SK_UNIT);
    return ret;
}

// GameObject — FUN_GAMETOOLTIP_BUILD_GAMEOBJECT, 1 stack arg (guid), void.
using BuildGO_t = void(__fastcall *)(void *self, void *edx, const void *guid);
BuildGO_t g_buildGOOriginal = nullptr;

void __fastcall BuildGO_h(void *self, void *edx, const void *guid) {
    g_buildGOOriginal(self, edx, guid);
    FireScript(self, SK_GAMEOBJECT);
}

static const Game::HookAutoRegister _resolverHook{
    Offsets::FUN_GAMETOOLTIP_SCRIPT_RESOLVER,
    reinterpret_cast<void *>(&Resolver_h),
    reinterpret_cast<void **>(&g_resolverOriginal)};

static const Game::HookAutoRegister _buildItemHook{
    Offsets::FUN_GAMETOOLTIP_BUILD_ITEM,
    reinterpret_cast<void *>(&BuildItem_h),
    reinterpret_cast<void **>(&g_buildItemOriginal)};

static const Game::HookAutoRegister _buildSpellHook{
    Offsets::FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP,
    reinterpret_cast<void *>(&BuildSpell_h),
    reinterpret_cast<void **>(&g_buildSpellOriginal)};

static const Game::HookAutoRegister _buildUnitHook{
    Offsets::FUN_GAMETOOLTIP_BUILD_UNIT,
    reinterpret_cast<void *>(&BuildUnit_h),
    reinterpret_cast<void **>(&g_buildUnitOriginal)};

static const Game::HookAutoRegister _buildGOHook{
    Offsets::FUN_GAMETOOLTIP_BUILD_GAMEOBJECT,
    reinterpret_cast<void *>(&BuildGO_h),
    reinterpret_cast<void **>(&g_buildGOOriginal)};

} // namespace

Suppressor::Suppressor() { ++g_suppress; }
Suppressor::~Suppressor() {
    if (g_suppress > 0)
        --g_suppress;
}

void PrepareForReload() {
    // Forget every cell — count gates the scan, so resetting it is a full
    // clear; the create path re-initializes a cell's slots on reuse.
    g_cellCount = 0;
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Tooltip::SetEvents
