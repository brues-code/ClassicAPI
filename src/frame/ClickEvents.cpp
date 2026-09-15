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

// The click bracket: `button:SetScript("PreClick"|"PostClick", fn)` and
// `GetMouseButtonClicked()`, both modelled on what 3.3.5's engine does around a
// click, and both placed where 1.12's engine makes the same decisions.
//
// What 3.3.5 does (Ghidra on the 3.3.5 client; class trail via the two script
// resolvers — CSimpleFrame FUN_0048e680, CSimpleButton FUN_0096f200):
//
//   - CSimpleButton::Click FUN_0096fd70(this, name, down) wraps FUN_0096f090,
//     which fires PreClick (+0x2d8), OnClick (+0x2e0) and PostClick (+0x2e8) as
//     THREE INDEPENDENT `if (slot)` blocks. PreClick/PostClick fire whether or
//     not the button has an OnClick. DoubleClick (FUN_0096fdd0 -> FUN_0096f150)
//     fires only OnDoubleClick — no Pre/Post.
//   - `GetMouseButtonClicked` (FUN_0050f950) pushes a frame-manager field,
//     [frameMgr+0x1234]. Exactly four dispatches write it, each with the same
//     C-stack bracket around the handler run:
//
//         saved = mgr->clickedButton;
//         mgr->clickedButton = name;      // this dispatch's own button string
//         ... fire the handler(s) ...
//         mgr->clickedButton = saved;
//
//     the click above, the double-click above, and — on EVERY frame type, not
//     just buttons — CSimpleFrame::OnMouseDown FUN_0048fc30 and OnMouseUp
//     FUN_0048fce0. Not OnDragStart. Confirmed live on 3.3.5 (2026-09-15):
//     `down LeftButton LeftButton`, `up LeftButton LeftButton`, `drag LeftButton
//     nil`. The C-stack save is what makes a nested click shadow the outer one
//     and restore on unwind, so the innermost dispatch wins; nothing captures
//     the OS mouse message and nothing evicts on a timer, so outside a handler
//     the answer is nil even while a button is held.
//   - CheckButton (FUN_009623c0) toggles its checked state BEFORE the click
//     dispatch, so PreClick already sees the new state.
//
// How 1.12 maps onto that:
//
//   1. Script storage. The Button resolver FUN_BUTTON_SCRIPT_RESOLVER knows only
//      OnClick (+0x4CC) and OnDoubleClick (+0x4D4). SetScript/GetScript/HookScript
//      all go through it, and its only contract is "return the address the
//      script lives at", so for the two names it doesn't know we hand back an
//      external per-button cell (a button object has no spare 8-byte slot) —
//      the Tooltip::SetEvents technique.
//
//   2. PreClick/PostClick firing — a co-hook on FUN_BUTTON_CLICK, the Button
//      click vmethod. Every Lua-creatable button's click reaches it (Button
//      directly; CheckButton's override toggles then chains to it), and it is
//      where the engine decides a click happened. The detour brackets the whole
//      click exactly like FUN_0096fd70: fire PreClick, call the original (which
//      fires OnClick iff one is set), fire PostClick. Pre/Post are fired through
//      the engine's own variadic forwarder FUN_FRAME_RUN_SCRIPT_VARIADIC with the
//      engine's own "%s" literal and name literal — the identical path and
//      identical pointers the engine uses for OnClick — so a handler cannot tell
//      the three apart. The name comes from the vmethod's button mask via the
//      six engine .data literals FUN_BUTTON_CLICK's own jump table selects
//      (unrecognized mask -> "UNKNOWN", exactly as Button:Click("Fnord") gives
//      OnClick). `down` is not supplied: 1.12's vmethod never learns whether the
//      click came from a press or a release, so Pre/Post keep 1.12 OnClick's
//      shape `(button)`.
//
//   3. GetMouseButtonClicked scope — the runner interceptor. Every one of the
//      fires 3.3.5 brackets reaches FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT as
//      (frame, slotPtr, "%s", name), so the existing shared `Frame::RunnerHook`
//      subscription matches slotPtr against the four slots — OnClick,
//      OnDoubleClick, OnMouseDown, OnMouseUp — saves/sets the name from the
//      fire's own argument, runs the original, restores. Zero hooks of its own;
//      the CSimpleFrame slot offsets are shared by every frame type, so the
//      OnMouseDown/OnMouseUp gate covers plain Frames like 3.3.5 does. The
//      click detour ALSO sets the scope around its whole bracket (so PreClick and
//      PostClick read it, and it is live for the whole dispatch as in 3.3.5);
//      for the OnClick fire inside, the runner gate re-sets the same value —
//      a harmless nested save/restore.
//
// Deliberate divergence from 3.3.5: its click dispatch drops a nested click of
// the SAME button entirely (`+0x2a8 & 0x10`). 1.12's engine has no such guard —
// a self-clicking OnClick recurses — and this module doesn't add one to the
// engine's own click; it only skips OUR PreClick/PostClick on that re-entry
// (per-button flag) so a self-clicking PreClick can't run away through us.
// A nested click of a DIFFERENT button gets its own full bracket, as in 3.3.5.
//
// Two rejected designs, for the record. (a) Bracketing at the runner only and
// requiring an OnClick handler to exist — the engine enters the runner only
// when [+0x4CC] is set (FUN_BUTTON_CLICK: `if (slot) fire`), so a button with
// no OnClick never fired Pre/Post; that shipped as a "limitation" blamed on a
// SuperWoW hook of the click vmethod that a per-DLL literal scan later showed
// does not exist (details at FUN_BUTTON_CLICK in Offsets.h). (b) Planting a
// sentinel no-op OnClick ref to satisfy the engine's gate and hiding it from
// GetScript/HookScript/SetScript(nil) — a parallel mechanism that lies to the
// engine; Rule #1. (An even earlier GetMouseButtonClicked captured the button
// from the WH_GETMESSAGE hook and evicted it on a world-tick timer, which
// reported a button for as long as one was held.)

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

// Per-button storage: one 8-byte {handler, context} slot per script kind, plus
// the re-entry flag for the click bracket. Only buttons that actually had
// SetScript("PreClick"/"PostClick", …) called get an entry, so the map stays
// small and the click detour's lookup is a miss for almost every UI button.
// unordered_map nodes are pointer-stable, so a cell address handed to the
// engine stays valid until PrepareForReload clears the map (buttons and their
// handler refs die on /reload).
struct Cell {
    uint32_t slot[SK_COUNT][2]; // [kind] = {handler ref, exec context}
    bool firing;                // this button's click bracket is on the stack
};
std::unordered_map<void *, Cell> g_cells;

Cell *CellFor(void *button, bool create) {
    auto it = g_cells.find(button);
    if (it != g_cells.end())
        return &it->second;
    if (!create)
        return nullptr;
    Cell &c = g_cells[button];
    for (auto &s : c.slot) {
        s[0] = 0;
        s[1] = 0;
    }
    c.firing = false;
    return &c;
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

// --- clicked button (GetMouseButtonClicked) --------------------------------
// The innermost bracketed dispatch's button string, null outside one — and
// Game::Lua::PushString tail-jumps to pushnil on null, so that surfaces as nil
// with no extra branch. It always points at one of the engine's own static name
// literals ("LeftButton" … "UNKNOWN"), which live for the process, so holding
// the pointer across the dispatch — and across a /reload a handler triggers —
// can never dangle.
const char *g_clickedButton = nullptr;

// The fire's first "%s" argument — the button name. Every bracketed fire is
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

// The engine's own name literal for a click-vmethod button mask — the exact
// pointer FUN_BUTTON_CLICK's jump table hands OnClick.
const char *NameForMask(uint32_t mask) {
    uintptr_t va;
    switch (mask) {
    case 0x01: va = Offsets::VAR_BUTTON_NAME_LEFT; break;
    case 0x02: va = Offsets::VAR_BUTTON_NAME_MIDDLE; break;
    case 0x04: va = Offsets::VAR_BUTTON_NAME_RIGHT; break;
    case 0x08: va = Offsets::VAR_BUTTON_NAME_4; break;
    case 0x10: va = Offsets::VAR_BUTTON_NAME_5; break;
    default:   va = Offsets::VAR_BUTTON_NAME_UNKNOWN; break;
    }
    return reinterpret_cast<const char *>(va);
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
            return reinterpret_cast<int>(CellFor(self, /*create*/ true)->slot[k]);
    return 0;
}

// --- Runner interceptor: GetMouseButtonClicked scope ------------------------
// Subscribed to `Frame::RunnerHook`, the single shared hook on
// FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT (MinHook allows one hook per address). This
// is the OnUpdate hot path too, so the gate is four pointer compares and nothing
// else.

bool IsBracketedSlot(void *frame, const uint32_t *slotPtr) {
    const char *base = reinterpret_cast<const char *>(frame);
    const auto at = [base](uintptr_t off) {
        return reinterpret_cast<const uint32_t *>(base + off);
    };
    return slotPtr == at(Offsets::OFF_BUTTON_ONCLICK_HANDLER) ||
           slotPtr == at(Offsets::OFF_BUTTON_ONDOUBLECLICK_HANDLER) ||
           slotPtr == at(Offsets::OFF_FRAME_ONMOUSEDOWN_SLOT) ||
           slotPtr == at(Offsets::OFF_FRAME_ONMOUSEUP_SLOT);
}

// Falls through (false) for every fire 3.3.5 doesn't bracket; otherwise runs it
// inside the clicked-button scope and reports it handled (true).
bool OnRun(void *frame, uint32_t *slotPtr, const char *fmt, void *varargs) {
    if (!IsBracketedSlot(frame, slotPtr))
        return false;
    const char *saved = g_clickedButton;
    g_clickedButton = FirstStringArg(fmt, varargs);
    Frame::RunnerHook::Original(frame, slotPtr, fmt, varargs);
    g_clickedButton = saved;
    return true;
}

// --- Click vmethod co-hook: the PreClick / PostClick bracket -----------------
// FUN_BUTTON_CLICK is __thiscall(button, mask, fromScript) RET 8, modelled as
// __fastcall with a dummy edx so both stack args land where the engine put
// them (same trick as the resolver above).
using Click_t = void(__fastcall *)(void *self, void *edx, uint32_t mask,
                                   int fromScript);
Click_t g_clickOriginal = nullptr;

// The engine's variadic script forwarder — the entry FUN_BUTTON_CLICK itself
// uses for OnClick. __cdecl, so the trailing name is pushed as one 4-byte arg.
using FireVariadic_t = void(__cdecl *)(void *frame, uint32_t *slotPtr,
                                       const char *fmt, ...);

// Fire the handler (if any) in `slot` with arg1 = `name`, through the engine's
// forwarder so the fire takes the identical path to the engine's OnClick fire
// (exec context stamped from slot[1], hooked runner, ScriptArgs positional
// args). The runner balances the Lua stack itself; snapshot/restore the top as
// cheap insurance against any imbalance leaking into the still-running click.
void FireSlot(void *button, uint32_t *slot, const char *name) {
    if (slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    const int savedTop = (L != nullptr) ? Game::Lua::GetTop(L) : 0;
    reinterpret_cast<FireVariadic_t>(Offsets::FUN_FRAME_RUN_SCRIPT_VARIADIC)(
        button, slot, reinterpret_cast<const char *>(Offsets::VAR_SCRIPT_FMT_S),
        name);
    if (L != nullptr)
        Game::Lua::SetTop(L, savedTop);
}

void __fastcall Click_h(void *self, void *edx, uint32_t mask, int fromScript) {
    Cell *cell = CellFor(self, /*create*/ false);
    if (cell == nullptr || cell->firing) {
        // No Pre/Post on this button, or a re-entrant click of the same button
        // from inside its own bracket: the engine's click runs untouched (its
        // OnClick fire, if any, is scoped by the runner gate above).
        g_clickOriginal(self, edx, mask, fromScript);
        return;
    }

    const char *name = NameForMask(mask);
    const char *saved = g_clickedButton;
    g_clickedButton = name;
    cell->firing = true;

    FireSlot(self, cell->slot[SK_PRECLICK], name);
    g_clickOriginal(self, edx, mask, fromScript); // the engine's click: OnClick iff set
    // The handlers may have SetScript'd Pre/Post away or re-created the cell's
    // neighbours; re-resolve rather than trust the pointer across Lua.
    cell = CellFor(self, /*create*/ false);
    if (cell != nullptr) {
        FireSlot(self, cell->slot[SK_POSTCLICK], name);
        cell->firing = false;
    }

    g_clickedButton = saved;
}

static const Game::HookAutoRegister _resolverHook{
    Offsets::FUN_BUTTON_SCRIPT_RESOLVER,
    reinterpret_cast<void *>(&Resolver_h),
    reinterpret_cast<void **>(&g_resolverOriginal)};

static const Game::HookAutoRegister _clickHook{
    Offsets::FUN_BUTTON_CLICK,
    reinterpret_cast<void *>(&Click_h),
    reinterpret_cast<void **>(&g_clickOriginal)};

static const Frame::RunnerHook::AutoSubscribe _runnerSub{&OnRun};

} // namespace

void PrepareForReload() {
    g_cells.clear();
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Frame::ClickEvents
