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

// The motion focus as a list, and `frame:SetPropagateMouseMotion(propagate)`.
//
// Retail's contract (probed, docs/notes/mouse-motion-probe.md): the focus pass
// walks the mouse-enabled frames under the cursor top-down, takes the first
// that hits, and keeps going while the last one taken propagates motion. The
// next frame taken is the next one UNDER THE CURSOR, not the parent. Every
// frame in the list is focus (`IsMouseMotionFocus`, `GetMouseFoci`); a frame
// that stays in the list across a change gets neither leave nor enter; the
// ones that drop out leave, then the ones that join enter, each top-first.
//
// 1.12 holds one focus frame at +0x7C of the UI context, written by three
// "store the focus, notify" blocks (ADDR_UI_FOCUS_PASS_COMMIT, _SETTER_STORE,
// ADDR_UI_INPUT_REMOVAL_FOCUS). Each is replaced with a call into this module,
// which keeps the list and keeps +0x7C as its first frame, so the engine's
// readers (GetMouseFocus, the press target, the world pick) see what they
// always saw. Without propagating frames the list is the one frame the
// engine picked, and the notifies are the engine's own.
//
// - The pass's commit gets the frame the engine's walk picked. When it
//   propagates, the walk resumes after it in the same mouse lists, through
//   the same per-frame probe (vtable +0x3C), re-reading each list as the
//   engine's loop does.
// - The setter (entering mouselook) makes its frame the whole list.
// - The removal takes any listed frame out, not just the first: it leaves
//   with 1 unless it is being destroyed, and asks for a re-pick, since the
//   frames below it may now be reached. A removed first frame hands +0x7C to
//   the next listed frame, which keeps `GetMouseFoci()[1] == GetMouseFocus()`.
//   Hiding a frame removes it before its children, so the hidden frame
//   leaves first and its focused children after it, as retail orders it.

#include "frame/MotionFocus.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/Destruction.h"
#include "ui/FrameObject.h"
#include "xml/FrameLoad.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Frame::MotionFocus {

namespace {

using Probe_t = int(__thiscall *)(void *frame, const void *event);
using Enter_t = void(__thiscall *)(void *frame);
using Leave_t = void(__thiscall *)(void *frame, int removal);

std::unordered_set<void *> g_propagates;

// The motion focus, top-first; its first frame is the context's +0x7C.
std::vector<void *> g_list;

void *g_notifying = nullptr;

// The leave/enter lists of each change in progress, so a frame destroyed by a
// script in the middle of one is not notified afterwards.
std::vector<std::vector<void *> *> g_pending;

bool Contains(const std::vector<void *> &list, const void *frame) {
    return std::find(list.begin(), list.end(), frame) != list.end();
}

bool Propagates(void *frame) { return g_propagates.count(frame) != 0; }

uint8_t *Context() { return Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR); }

void *&Focus(uint8_t *ctx) { return Game::Ref<void *>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS); }

// Starts the list over from +0x7C when something wrote it behind our back:
// the context's state before these blocks were patched in.
void Sync(uint8_t *ctx) {
    void *focus = Focus(ctx);
    if ((g_list.empty() ? nullptr : g_list.front()) == focus)
        return;
    g_list.clear();
    if (focus != nullptr)
        g_list.push_back(focus);
}

void Notify(void *frame, bool enter, int removal, void *notifying) {
    void *const saved = g_notifying;
    g_notifying = notifying;
    const void *vtable = Game::Read<const void *>(frame, 0);
    if (enter)
        Game::Read<Enter_t>(vtable, Offsets::VTBL_FRAME_ON_ENTER)(frame);
    else
        Game::Read<Leave_t>(vtable, Offsets::VTBL_FRAME_ON_LEAVE)(frame, removal);
    g_notifying = saved;
}

struct PendingScope {
    explicit PendingScope(std::vector<void *> *list) { g_pending.push_back(list); }
    ~PendingScope() { g_pending.pop_back(); }
};

// Makes `list` the motion focus. The new state is in place before any
// script runs; then the frames that drop out leave and the frames that join
// enter, each top-first. A frame a script took out of the focus meanwhile is
// not entered.
void Apply(uint8_t *ctx, std::vector<void *> list) {
    std::vector<void *> left;
    std::vector<void *> entered;
    for (void *f : g_list)
        if (!Contains(list, f))
            left.push_back(f);
    for (void *f : list)
        if (!Contains(g_list, f))
            entered.push_back(f);
    g_list = std::move(list);
    Focus(ctx) = g_list.empty() ? nullptr : g_list.front();

    const PendingScope leftScope(&left);
    const PendingScope enteredScope(&entered);
    for (void *f : left)
        if (f != nullptr)
            Notify(f, false, 0, f);
    for (void *f : entered)
        if (f != nullptr && Contains(g_list, f))
            Notify(f, true, 0, f);
}

// Appends the frames the walk reaches past `primary` while the last one
// taken propagates: the rest of the mouse lists in the pass's order, each
// probed as the pass probes it.
void ContinueWalk(uint8_t *ctx, void *primary, const void *event, std::vector<void *> &list) {
    bool past = false;
    for (int strata = Offsets::FRAME_STRATA_COUNT - 1; strata >= 0; --strata) {
        const uint8_t *queue = ctx + Offsets::OFF_UI_CONTEXT_EVENT_QUEUES +
                               (strata + Offsets::FRAME_STRATA_COUNT * Offsets::SIMPLE_EVENT_MOUSE) *
                                   Offsets::EVENT_QUEUE_STRIDE;
        for (uint32_t i = 0; i < Game::Read<uint32_t>(queue, Offsets::OFF_EVENT_QUEUE_COUNT); ++i) {
            auto *const *nodes = Game::Read<void *const *>(queue, Offsets::OFF_EVENT_QUEUE_DATA);
            void *frame = Game::Read<void *>(nodes[i], 0);
            if (!past) {
                past = frame == primary;
                continue;
            }
            const void *vtable = Game::Read<const void *>(frame, 0);
            if (Game::Read<Probe_t>(vtable, Offsets::VTBL_FRAME_ON_MOUSE_MOVE)(frame, event) == 0)
                continue;
            list.push_back(frame);
            if (!Propagates(frame))
                return;
        }
    }
}

void __stdcall CommitPass(uint8_t *ctx, void *primary, const void *event) {
    Sync(ctx);
    std::vector<void *> list;
    if (primary != nullptr) {
        list.push_back(primary);
        if (Propagates(primary))
            ContinueWalk(ctx, primary, event, list);
    }
    Apply(ctx, std::move(list));
}

void __stdcall SetFocus(uint8_t *ctx, void *frame) {
    Sync(ctx);
    std::vector<void *> list;
    if (frame != nullptr)
        list.push_back(frame);
    Apply(ctx, std::move(list));
}

void __stdcall Remove(uint8_t *ctx, void *frame) {
    Sync(ctx);
    const auto it = std::find(g_list.begin(), g_list.end(), frame);
    if (it == g_list.end())
        return;
    g_list.erase(it);
    Focus(ctx) = g_list.empty() ? nullptr : g_list.front();
    Game::Ref<int>(ctx, Offsets::OFF_UI_CONTEXT_REPICK) = 1;
    if (Game::Read<int>(frame, Offsets::OFF_FRAME_OBJECT_STATE) != Offsets::FRAME_OBJECT_STATE_DESTROYING)
        Notify(frame, false, 1, nullptr);
}

// Replaces the pass's commit block: context [ebp-8], picked frame [ebp-4],
// the event's copy at ebp-0x44. The NOPs after the call run on to 0x0076623C,
// which reads [ebp-4] for the pass's return value.
__declspec(naked) void PassCommitThunk() {
    __asm {
        lea eax, [ebp - 44h]
        push eax
        push dword ptr [ebp - 4]
        push dword ptr [ebp - 8]
        call CommitPass
        ret
    }
}

// Replaces the setter's store and notifies: context in esi, frame in edi.
__declspec(naked) void SetterThunk() {
    __asm {
        push edi
        push esi
        call SetFocus
        ret
    }
}

// Replaces the removal's focus tail: context in eax, frame in edi.
__declspec(naked) void RemovalThunk() {
    __asm {
        push edi
        push eax
        call Remove
        ret
    }
}

const uint8_t kPassCommit[] = {0x8B, 0x4D, 0xF0, 0x3B, 0xC1, 0x74, 0x1D, 0x85, 0xC9, 0x8B, 0x55, 0xF8,
                               0x89, 0x42, 0x7C, 0x74, 0x07, 0x8B, 0x01, 0x6A, 0x00, 0xFF, 0x50, 0x50,
                               0x8B, 0x4D, 0xFC, 0x85, 0xC9, 0x74, 0x05, 0x8B, 0x11, 0xFF, 0x52, 0x4C};
const uint8_t kSetterStore[] = {0x3B, 0xF9, 0x74, 0x19, 0x85, 0xC9, 0x89, 0x7E, 0x7C, 0x74,
                                0x07, 0x8B, 0x01, 0x6A, 0x00, 0xFF, 0x50, 0x50, 0x85, 0xFF,
                                0x74, 0x07, 0x8B, 0x17, 0x8B, 0xCF, 0xFF, 0x52, 0x4C};
const uint8_t kRemovalFocus[] = {0x3B, 0x78, 0x7C, 0x75, 0x1F, 0x89, 0x70, 0x7C, 0xC7, 0x80, 0x00, 0x11,
                                 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x83, 0xBF, 0xAC, 0x00, 0x00, 0x00,
                                 0x03, 0x74, 0x09, 0x8B, 0x17, 0x6A, 0x01, 0x8B, 0xCF, 0xFF, 0x52, 0x50};

void OnFrameDestroyed(void *frame) {
    g_propagates.erase(frame);
    g_list.erase(std::remove(g_list.begin(), g_list.end(), frame), g_list.end());
    for (std::vector<void *> *pending : g_pending)
        std::replace(pending->begin(), pending->end(), frame, static_cast<void *>(nullptr));
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

void SetPropagates(void *frame, bool on) {
    if (on == Propagates(frame))
        return;
    if (on)
        g_propagates.insert(frame);
    else
        g_propagates.erase(frame);
    if (uint8_t *ctx = Context())
        Game::Ref<int>(ctx, Offsets::OFF_UI_CONTEXT_REPICK) = 1;
}

// `propagateMouseInput="Motion"` or `"Both"`; "Clicks" is the click half.
void OnFrameNode(void *object, const void *node, void * /*status*/) {
    const char *value = Xml::Attr(node, "propagateMouseInput");
    if (Ascii::EqualCI(value, "Motion") || Ascii::EqualCI(value, "Both"))
        SetPropagates(object, true);
}

const Xml::FrameLoad::AutoSubscribe _xmlSub{&OnFrameNode};

int __fastcall Script_SetPropagateMouseMotion(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    SetPropagates(frame, Game::Lua::ToFlag(L, 2));
    return 0;
}

int __fastcall Script_CanPropagateMouseMotion(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, Propagates(frame));
    return 1;
}

int __fastcall Script_IsMouseMotionFocus(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    uint8_t *ctx = Context();
    if (ctx != nullptr)
        Sync(ctx);
    Game::Lua::PushBool(L, ctx != nullptr && Contains(g_list, frame));
    return 1;
}

int __fastcall Script_GetMouseFoci(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    uint8_t *ctx = Context();
    if (ctx == nullptr)
        return 1;
    Sync(ctx);
    for (size_t i = 0; i < g_list.size(); ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        UI::FrameObject::Push(L, g_list[i]);
        Game::Lua::RawSet(L, 1);
    }
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetPropagateMouseMotion", &Script_SetPropagateMouseMotion},
    {"CanPropagateMouseMotion", &Script_CanPropagateMouseMotion},
    {"IsMouseMotionFocus", &Script_IsMouseMotionFocus},
};

const Game::Doc::Field kPropagateArgs[] = {
    Game::Doc::Req("propagate", "bool",
                   "When true, the frame passes mouse motion on to the next frame under the cursor."),
};
const Game::Doc::Field kPropagateRets[] = {
    Game::Doc::Req("propagate", "bool", "True when the frame passes mouse motion on."),
};
const Game::Doc::Field kFocusRets[] = {
    Game::Doc::Req("isMouseMotionFocus", "bool", "True when the frame is one of the frames under the cursor that get motion."),
};
const Game::Doc::Field kFociRets[] = {
    Game::Doc::Req("region", "table", "The frames that get mouse motion, from top to bottom."),
};
const Game::Doc::Function kSetPropagate{
    "Sets whether the frame passes mouse motion on to the next frame under the cursor.", kPropagateArgs, {}};
const Game::Doc::Function kCanPropagate{"Returns whether the frame passes mouse motion on.", {}, kPropagateRets};
const Game::Doc::Function kIsFocus{"Returns whether the frame gets mouse motion at the cursor.", {}, kFocusRets};
const Game::Doc::Function kGetMouseFoci{"Returns the frames that get mouse motion at the cursor.", {}, kFociRets,
                                        "InputGlobals"};

const Game::Doc::Method g_methodDocs[] = {
    {"SetPropagateMouseMotion", &kSetPropagate},
    {"CanPropagateMouseMotion", &kCanPropagate},
    {"IsMouseMotionFocus", &kIsFocus},
};

void RegisterLuaFunctions() {
    Common::ReplaceWithCall(Offsets::ADDR_UI_FOCUS_PASS_COMMIT, kPassCommit, sizeof kPassCommit,
                            reinterpret_cast<const void *>(&PassCommitThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_FOCUS_SETTER_STORE, kSetterStore, sizeof kSetterStore,
                            reinterpret_cast<const void *>(&SetterThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_INPUT_REMOVAL_FOCUS, kRemovalFocus, sizeof kRemovalFocus,
                            reinterpret_cast<const void *>(&RemovalThunk));
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
                                    static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
                                    static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
    Game::Lua::RegisterGlobalFunction("GetMouseFoci", &Script_GetMouseFoci, &kGetMouseFoci);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void *Notifying() { return g_notifying; }

} // namespace Frame::MotionFocus
