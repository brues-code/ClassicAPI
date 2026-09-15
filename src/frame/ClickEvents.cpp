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
//
// `GetMouseButtonClicked()` — the button string of the innermost click dispatch
// currently on the stack, nil outside one. It lives in this module because that
// is precisely what it is. 3.3.5 keeps it in a frame-manager field (the getter
// FUN_0050f950 pushes `[frameMgr + 0x1234]`, nil when null), and the ONLY code
// that ever writes that field is the four click dispatches — button OnClick
// FUN_0048fc30, OnDoubleClick FUN_0048fce0, and the two secure click paths
// FUN_0096fd70 / FUN_0096fdd0 that wrap PreClick -> OnClick -> PostClick — each
// with the same bracket around the handler:
//
//     saved = mgr->clickedButton;
//     mgr->clickedButton = buttonName;   // this dispatch's own argument
//     ... run the handler ...
//     mgr->clickedButton = saved;
//
// Saving on the C stack is what makes a nested click shadow the outer one and
// restore on unwind, so the innermost dispatch wins. Nothing captures the OS
// mouse message and nothing evicts on a timer: outside a click dispatch the
// answer is nil, and it is nil in OnMouseDown / OnMouseUp / OnDragStart too.
//
// 1.12 has no such field, but it has the same bracket point. Both click
// dispatches (FUN_00779540 OnClick, FUN_00779650 OnDoubleClick) fire their
// handler through the runner this module already intercepts, passing the button
// name as the fire's only "%s" argument. So we mirror the engine's bracket
// around that fire and read the name straight out of it — which also inherits
// the engine's own naming for free: the vmethods take a button BITMASK, and
// Button:Click (FUN_007826C0) folds its optional string argument down to one, so
// a bare `Click()` arrives as "LeftButton" and an unrecognized name as
// "UNKNOWN". (An earlier version captured the button from the WH_GETMESSAGE
// hook behind GLOBAL_MOUSE_DOWN/UP and evicted it a couple of world ticks later
// — which reported a button for as long as one was held, and for ~2 frames after
// every click, making the value useless as the "am I in a click handler" test it
// exists to be.)

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
// It guards ONLY the Pre/PostClick pair — a nested click still gets its own
// clicked-button scope, since that one must track the innermost dispatch.
int g_firing = 0;

// --- clicked button (GetMouseButtonClicked) --------------------------------
// The innermost click dispatch's button string, null outside one — and
// Game::Lua::PushString tail-jumps to pushnil on null, so that surfaces as nil
// with no extra branch. It always points at one of the engine's own static name
// literals ("LeftButton" … "UNKNOWN"), which live for the process, so holding
// the pointer across the dispatch — and across a /reload a click handler
// triggers — can never dangle.
const char *g_clickedButton = nullptr;

// The fire's first "%s" argument — the button name. A click fire is always
// exactly ("%s", name); any other shape isn't a name, so it reads null.
const char *FirstStringArg(const char *fmt, const void *varargs) {
    if (fmt == nullptr || varargs == nullptr)
        return nullptr;
    const char *p = fmt;
    while (*p != '\0' && *p != '%')
        ++p;
    if (p[0] != '%' || p[1] != 's')
        return nullptr;
    return *reinterpret_cast<const char *const *>(varargs);
}

enum ClickKind { CK_NONE, CK_CLICK, CK_DOUBLECLICK };

// Which click dispatch this fire is, by slot address. Exact: no other fire
// passes a button's OnClick / OnDoubleClick slot.
ClickKind ClickKindOf(void *frame, const uint32_t *slotPtr) {
    const char *base = reinterpret_cast<const char *>(frame);
    if (slotPtr == reinterpret_cast<const uint32_t *>(
                       base + Offsets::OFF_BUTTON_ONCLICK_HANDLER))
        return CK_CLICK;
    if (slotPtr == reinterpret_cast<const uint32_t *>(
                       base + Offsets::OFF_BUTTON_ONDOUBLECLICK_HANDLER))
        return CK_DOUBLECLICK;
    return CK_NONE;
}

int __fastcall Script_GetMouseButtonClicked(void *L) {
    Game::Lua::PushString(L, g_clickedButton);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMouseButtonClicked",
                                      &Script_GetMouseButtonClicked);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

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

// Falls through (false) for every fire that isn't a click dispatch; otherwise
// runs the dispatch itself, inside the engine's clicked-button bracket, and
// reports it handled (true). A top-level OnClick additionally gets its
// PreClick/PostClick pair, which run inside the same bracket so they read the
// button too — exactly like the secure click path they mirror.
bool OnRun(void *frame, uint32_t *slotPtr, const char *fmt, void *varargs) {
    const ClickKind kind = ClickKindOf(frame, slotPtr);
    if (kind == CK_NONE)
        return false;

    const char *saved = g_clickedButton;
    g_clickedButton = FirstStringArg(fmt, varargs);

    if (kind == CK_CLICK && g_firing == 0) {
        ++g_firing;
        FireCell(frame, SK_PRECLICK, fmt, varargs);
        Frame::RunnerHook::Original(frame, slotPtr, fmt, varargs); // the OnClick
        FireCell(frame, SK_POSTCLICK, fmt, varargs);
        --g_firing;
    } else {
        Frame::RunnerHook::Original(frame, slotPtr, fmt, varargs);
    }

    g_clickedButton = saved;
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
