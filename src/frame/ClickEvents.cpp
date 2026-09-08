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

// `button:SetScript("PreClick"|"PostClick", fn)` — backport of the modern
// button scripts that fire immediately before and after OnClick, as real frame
// scripts settable via the standard SetScript / GetScript / HookScript. Vanilla
// 1.12 has only OnClick and OnDoubleClick on buttons.
//
// 1.12 already has the script-dispatch machinery, just not these two names.
// Two co-hooks (same shape as Tooltip::SetEvents):
//
//   - The Button script resolver FUN_BUTTON_SCRIPT_RESOLVER maps a script name
//     to its handler slot (OnClick -> button+0x4CC, …). SetScript/GetScript/
//     HookScript all go through it. We co-hook it: for PreClick/PostClick
//     (which the engine doesn't know) we hand back a pointer to an external
//     per-button cell — a button object has no free slot for two new scripts,
//     and the resolver's only contract is "return the address the script lives
//     at", so external storage works exactly like an in-object slot.
//
//   - The engine fires a button's OnClick through the arg'd script runner
//     FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT(frame, frame+0x4CC, "%s", &buttonName).
//     We intercept that runner (through `Frame::RunnerHook`, the shared single
//     hook on it) and, when the fired slot IS the OnClick slot, re-invoke the
//     runner for our PreClick cell first, then let OnClick run, then re-invoke
//     for PostClick — reusing the SAME (fmt, varargs) so PreClick/PostClick
//     receive arg1 = the button name exactly like OnClick.
//
// Why the runner and not the button click vmethod (FUN_00779540): SuperWoW
// inline-hooks that vmethod for click-casting, and a second MinHook there
// corrupts the trampoline (ERROR #132 — see Offsets.h). The runner is
// uncontested, fires at the same top-level dispatch level (so its exec-context
// stamp is valid), and the OnClick slot address is an exact gate.
//
// Limitation: because the runner only runs when a slot is non-empty, PreClick/
// PostClick fire only when the button also has an OnClick handler set. Modern
// fires them regardless; on this client that would require hooking the click
// vmethod, which SuperWoW owns. Bracketing an existing OnClick is the common
// case and the one this backport serves.

#include "frame/ClickEvents.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/RunnerHook.h"

#include <cstdint>
#include <unordered_map>

namespace Frame::ClickEvents {

namespace {

enum ScriptKind { SK_PRECLICK = 0, SK_POSTCLICK, SK_COUNT };

// Matched case-insensitively against the resolver's input name.
constexpr const char *kNamesLower[SK_COUNT] = {"preclick", "postclick"};

// Per-button storage: one 8-byte {handler, context} slot per script kind. Only
// buttons that actually had SetScript("PreClick"/"PostClick", …) called get an
// entry, so the map stays small. unordered_map nodes are pointer-stable, so a
// cell address handed to the engine stays valid until PrepareForReload clears
// the map (buttons and their handler refs die on /reload).
struct Cell {
    uint32_t slot[SK_COUNT][2]; // [kind] = {handler ref, exec context}
};
std::unordered_map<void *, Cell> g_cells;

uint32_t *SlotFor(void *button, int kind, bool create) {
    auto it = g_cells.find(button);
    if (it != g_cells.end())
        return it->second.slot[kind];
    if (!create)
        return nullptr;
    Cell &c = g_cells[button];
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

// Recursion guard: a PreClick/OnClick/PostClick handler can programmatically
// click a button again. While bracketing, further OnClick fires pass straight
// through (the nested click still runs — it just doesn't get its own nested
// PreClick/PostClick), which prevents runaway from a self-clicking handler.
int g_firing = 0;

// --- Resolver co-hook (Button script-name -> slot) ---------------------
using Resolver_t = int(__fastcall *)(void *self, void *edx, const char *name);
Resolver_t g_resolverOriginal = nullptr;

int __fastcall Resolver_h(void *self, void *edx, const char *name) {
    const int engineSlot = g_resolverOriginal(self, edx, name);
    if (engineSlot != 0) // a base-frame or existing button script — leave it
        return engineSlot;
    for (int k = 0; k < SK_COUNT; ++k)
        if (EqualsIgnoreCase(name, kNamesLower[k]))
            return reinterpret_cast<int>(SlotFor(self, k, /*create*/ true));
    return 0;
}

// --- Runner interceptor (fire PreClick before / PostClick after OnClick) ---
// Subscribed to `Frame::RunnerHook`, the single shared hook on
// FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT (MinHook allows one hook per address).

// Fire the handler (if any) for `kind` on `frame`, reusing the OnClick fire's
// own (fmt, varargs) so arg1 = the button name. We call the engine runner
// directly (its exec-context stamp is valid at this dispatch level). The runner
// balances the Lua stack itself; we snapshot/restore the top as cheap insurance
// against any imbalance leaking to the still-running button click code.
void FireCell(void *frame, int kind, const char *fmt, void *varargs) {
    uint32_t *slot = SlotFor(frame, kind, /*create*/ false);
    if (slot == nullptr || slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    const int savedTop = (L != nullptr) ? Game::Lua::GetTop(L) : 0;
    Frame::RunnerHook::Original(frame, slot, fmt, varargs);
    if (L != nullptr)
        Game::Lua::SetTop(L, savedTop);
}

// Falls through (false) for every fire that isn't a top-level OnClick;
// otherwise runs the whole bracket itself and reports it handled (true).
bool OnRun(void *frame, uint32_t *slotPtr, const char *fmt, void *varargs) {
    const auto onClickSlot = reinterpret_cast<uint32_t *>(
        reinterpret_cast<char *>(frame) + Offsets::OFF_BUTTON_ONCLICK_HANDLER);
    if (slotPtr != onClickSlot || g_firing > 0)
        return false;

    ++g_firing;
    FireCell(frame, SK_PRECLICK, fmt, varargs);
    Frame::RunnerHook::Original(frame, slotPtr, fmt, varargs); // the button's OnClick
    FireCell(frame, SK_POSTCLICK, fmt, varargs);
    --g_firing;
    return true;
}

static const Game::HookAutoRegister _resolverHook{
    Offsets::FUN_BUTTON_SCRIPT_RESOLVER,
    reinterpret_cast<void *>(&Resolver_h),
    reinterpret_cast<void **>(&g_resolverOriginal)};

static const Frame::RunnerHook::AutoSubscribe _runnerSub{&OnRun};

} // namespace

void PrepareForReload() {
    g_cells.clear();
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Frame::ClickEvents
