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
//      OnClick).
//
//   2a. `down`. 3.3.5 passes OnClick/PreClick/PostClick `(button, down)`; its
//      press handler clicks with down = 1, its release handler with 0, and
//      Click(button, down) reads StringToBOOL(L, 3, 0). 1.12's click vmethod
//      gets `(mask, fromScript)` and never learns which — but its callers do.
//      The six that 3.3.5 maps onto (press, release, Script_Click's four name
//      branches — SITE_BUTTON_*_CLICK / SITE_SCRIPT_CLICK_*) call it through
//      `call [edx+0x94]`; each is replaced with a call to a thunk that records
//      {button, down} and jumps on through the same vtable slot. The click
//      detour takes that record for its own button (anything else clicks with
//      down = false) and offers it to Frame::ScriptArgs as the trailing boolean
//      of this button's OnClick/PreClick/PostClick runs.
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

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "frame/MouseRegistration.h"
#include "frame/RunnerHook.h"
#include "frame/ScriptArgs.h"
#include "frame/ScriptSlots.h"

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

int __fastcall Script_GetMouseButtonClicked(void *L) {
    Game::Lua::PushString(L, g_clickedButton);
    return 1;
}

// --- down: the click's callers record it ------------------------------------
// The last {button, down} a patched call site recorded; the click detour takes
// it (TakeDown) for its own button.
void *g_pendingButton = nullptr;
int g_pendingDown = 0;

// The thunks jump through `[edx + 94h]`. MSVC inline asm reads a C++ name
// inside an address expression as that variable's ADDRESS, not its value
// (`[edx + vtblButtonClick]` jumped through vtable + &vtblButtonClick and
// crashed), so the displacement is a literal held to Offsets here.
static_assert(Offsets::VTBL_BUTTON_CLICK == 0x94, "click thunks hard-code [edx + 94h]");

// Script_Click's `down`: StringToBOOL(L, 3, 0), as 3.3.5's Click reads it.
void __cdecl RecordScriptDown(void *button) {
    void *L = Game::Lua::State();
    g_pendingButton = button;
    g_pendingDown = (L != nullptr && Game::Lua::ToFlag(L, 3)) ? 1 : 0;
}

// Each stands in for `call [edx+0x94]` (ECX = the button, EDX = its vtable):
// record, then jump to the same slot with the caller's return address and
// arguments untouched.
__declspec(naked) void PressClickThunk() {
    __asm {
        mov g_pendingButton, ecx
        mov g_pendingDown, 1
        jmp dword ptr [edx + 94h]
    }
}

__declspec(naked) void ReleaseClickThunk() {
    __asm {
        mov g_pendingButton, ecx
        mov g_pendingDown, 0
        jmp dword ptr [edx + 94h]
    }
}

__declspec(naked) void ScriptClickThunk() {
    __asm {
        push ecx
        push edx
        push ecx
        call RecordScriptDown
        add esp, 4
        pop edx
        pop ecx
        jmp dword ptr [edx + 94h]
    }
}

void PatchClickCallers() {
    static const uint8_t kCallClick[6] = {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00};
    const auto patch = [](uintptr_t site, void (*thunk)()) {
        Common::ReplaceWithCall(site, kCallClick, sizeof kCallClick,
                                reinterpret_cast<const void *>(thunk));
    };
    patch(Offsets::SITE_BUTTON_PRESS_CLICK, &PressClickThunk);
    patch(Offsets::SITE_BUTTON_RELEASE_CLICK, &ReleaseClickThunk);
    patch(Offsets::SITE_SCRIPT_CLICK_LEFT, &ScriptClickThunk);
    patch(Offsets::SITE_SCRIPT_CLICK_MIDDLE, &ScriptClickThunk);
    patch(Offsets::SITE_SCRIPT_CLICK_RIGHT, &ScriptClickThunk);
    patch(Offsets::SITE_SCRIPT_CLICK_DEFAULT, &ScriptClickThunk);
}

// The recorded `down` when it was recorded for `button`; false for a click
// no patched caller made. Clears the record either way.
bool TakeDown(void *button) {
    const bool down = g_pendingButton == button && g_pendingDown != 0;
    g_pendingButton = nullptr;
    return down;
}

void RegisterLuaFunctions() {
    PatchClickCallers();
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
// inside the clicked-button scope and reports it handled (true). An
// OnMouseDown / OnMouseUp fire for a button its RegisterForMouse excludes is
// handled by not running it — this module owns those slots on the runner.
bool OnRun(void *frame, uint32_t *slotPtr, const char *fmt, void *varargs) {
    if (!IsBracketedSlot(frame, slotPtr))
        return false;
    const char *name = FirstStringArg(fmt, varargs);
    const char *base = reinterpret_cast<const char *>(frame);
    const auto *slot = reinterpret_cast<const char *>(slotPtr);
    if (slot == base + Offsets::OFF_FRAME_ONMOUSEDOWN_SLOT ||
        slot == base + Offsets::OFF_FRAME_ONMOUSEUP_SLOT) {
        const bool down = slot == base + Offsets::OFF_FRAME_ONMOUSEDOWN_SLOT;
        if (!Frame::MouseRegistration::Accepts(frame, down, name))
            return true;
    }
    const char *saved = g_clickedButton;
    g_clickedButton = name;
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

// The innermost click in progress, for its OnClick/PreClick/PostClick `down`.
struct ClickScope {
    void *button;
    bool down;
};
ClickScope g_click = {nullptr, false};

bool ClickDown(int handlerRef, void *frame, bool *value) {
    if (frame == nullptr || frame != g_click.button)
        return false;
    bool isClick = Game::Read<int>(frame, Offsets::OFF_BUTTON_ONCLICK_HANDLER) == handlerRef;
    if (!isClick) {
        const Cell *cell = CellFor(frame, /*create*/ false);
        isClick = cell != nullptr &&
                  (static_cast<int>(cell->slot[SK_PRECLICK][0]) == handlerRef ||
                   static_cast<int>(cell->slot[SK_POSTCLICK][0]) == handlerRef);
    }
    if (!isClick)
        return false;
    *value = g_click.down;
    return true;
}

const Frame::ScriptArgs::AutoTrailingBool _clickDown{&ClickDown};

void ClickBracket(void *self, void *edx, uint32_t mask, int fromScript) {
    Cell *cell = CellFor(self, /*create*/ false);
    if (cell == nullptr || cell->firing) {
        // No Pre/Post on this button, or a re-entrant click of the same button
        // from inside its own bracket: the engine's click runs untouched (its
        // OnClick fire, if any, is scoped by the runner gate above).
        g_clickOriginal(self, edx, mask, fromScript);
        return;
    }

    const char *name = ButtonNameForMask(mask);
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

void __fastcall Click_h(void *self, void *edx, uint32_t mask, int fromScript) {
    const ClickScope saved = g_click;
    g_click = {self, TakeDown(self)};
    ClickBracket(self, edx, mask, fromScript);
    g_click = saved;
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

void ForEachSlot(const void *frame, Frame::ScriptSlots::Visit visit, void *ctx) {
    const auto it = g_cells.find(const_cast<void *>(frame));
    if (it == g_cells.end())
        return;
    for (const auto &slot : it->second.slot)
        visit(slot, ctx);
}

static const Frame::ScriptSlots::AutoRegister _slots{&ForEachSlot};

} // namespace

void PrepareForReload() {
    g_cells.clear();
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

const char *CurrentButton() { return g_clickedButton; }

const char *ButtonNameForMask(uint32_t mask) {
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

} // namespace Frame::ClickEvents
