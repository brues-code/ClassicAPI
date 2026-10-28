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

// A script's three bindings, retail's LE_SCRIPT_BINDING_TYPE_*: the intrinsic
// precall (0), the extrinsic (1) and the intrinsic postcall (2), run in that
// order. An intrinsic frame type (Xml::Intrinsics) binds its own handlers as
// intrinsics, so a frame of that type keeps them whatever handler it is
// given. On retail (probed with CreateFrame("EventFrame")):
//   - GetScript(script) and SetScript(script, f) read and write the
//     extrinsic binding: SetScript(script, nil) leaves the intrinsics, and
//     GetScript is nil while only an intrinsic is set;
//   - GetScript(script, binding) reads one binding;
//   - HookScript(script, hook) hooks the extrinsic binding, so the hook runs
//     before the postcall; HookScript(script, hook, binding) hooks another;
//   - an intrinsic <Scripts> element without `intrinsicOrder` (its default
//     is "none") binds the precall: EventFrame's OnLoad.
//
// 1.12 keeps one {handler, context} slot per script, and runs it only when it
// holds a handler. A slot with intrinsic bindings holds a dispatcher instead:
// a C closure whose upvalues are the three handlers (nil for an empty one)
// and their context words. It runs each in its context, under the engine's
// script error handler, with the arguments the script was fired with, so an
// error in one does not stop the others. Each binding keeps its own registry
// ref here; the dispatcher is rebuilt when one changes.
//
// The engine writes the slot from three places, and each releases the old
// handler first: SetScript (FUN_SCRIPT_FRAME_SETSCRIPT), the XML <Scripts>
// loader (SetScriptObjectScript, 0x007025C0) and anything that calls
// SetScript. After each, a slot that no longer holds its dispatcher took a
// new extrinsic handler: it moves into the extrinsic binding and the
// dispatcher goes back. SetScript is co-hooked for that, and every frame
// node's load is followed (Xml::FrameLoad) for the loader.

#include "frame/ScriptBindings.h"

#include "Game.h"
#include "HookSecureFunc.h"
#include "Offsets.h"
#include "frame/Destruction.h"
#include "xml/FrameLoad.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Frame::ScriptBindings {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
// `__thiscall(frame, name) -> slot`, ret 4, modelled with a dummy edx.
using Resolver_t = uint32_t *(__fastcall *)(void *frame, void *edx, const char *name);

struct Part {
    int ref = 0; // registry ref of the handler, 0 for none
    uint32_t context = 0;
};

struct Entry {
    void *frame = nullptr;
    uint32_t *slot = nullptr;
    Part parts[BINDING_COUNT];
    int dispatcher = 0;              // the ref the slot holds while it is ours
    const void *closure = nullptr;   // the dispatcher closure itself
};

std::unordered_map<uint32_t *, Entry> g_entries;
std::unordered_map<void *, std::vector<uint32_t *>> g_byFrame;

uint32_t *Resolve(void *frame, const char *name) {
    const void *vtable = Game::Read<const void *>(frame, 0);
    return Game::Read<Resolver_t>(vtable, Offsets::VTBL_FRAME_SCRIPT_RESOLVER)(frame, nullptr, name);
}

Entry *Find(uint32_t *slot) {
    const auto it = g_entries.find(slot);
    return it != g_entries.end() ? &it->second : nullptr;
}

void Release(void *L, int ref) {
    if (ref > 0)
        Game::Lua::Unref(L, Game::Lua::REGISTRY_INDEX, ref);
}

void Install(void *L, Entry &e);
bool Holds(void *L, const Entry &e);

// The slot's entry, made on first use with the slot's handler as its
// extrinsic binding and the dispatcher in the slot.
Entry &Acquire(void *L, void *frame, uint32_t *slot) {
    auto [it, created] = g_entries.try_emplace(slot);
    Entry &e = it->second;
    if (created) {
        e.frame = frame;
        e.slot = slot;
        e.parts[EXTRINSIC] = {static_cast<int>(slot[0]), slot[1]};
        g_byFrame[frame].push_back(slot);
        Install(L, e);
    }
    return e;
}

// Puts a dispatcher over the entry's bindings into its slot, releasing the
// one the slot still holds.
void Install(void *L, Entry &e) {
    if (Holds(L, e))
        Release(L, e.dispatcher);
    for (const Part &p : e.parts) {
        if (p.ref > 0)
            Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, p.ref);
        else
            Game::Lua::PushNil(L);
    }
    for (const Part &p : e.parts)
        Game::Lua::PushNumber(L, static_cast<double>(p.context));
    Game::Lua::PushCClosure(L, &Dispatcher, 2 * BINDING_COUNT);
    e.closure = Game::Lua::ToPointer(L, -1);
    const int ref = Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
    e.dispatcher = ref > 0 ? ref : 0;
    e.slot[0] = static_cast<uint32_t>(e.dispatcher);
    e.slot[1] = 0;
}

// Whether the slot still holds the entry's dispatcher. The ref number alone
// does not say: a writer releases the dispatcher's ref and luaL_ref hands the
// same number to the handler it stores next.
bool Holds(void *L, const Entry &e) {
    if (e.dispatcher == 0 || static_cast<int>(e.slot[0]) != e.dispatcher)
        return false;
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, e.dispatcher);
    const bool ours = Game::Lua::IsCFunction(L, -1) && Game::Lua::ToPointer(L, -1) == e.closure;
    Game::Lua::SetTop(L, -2);
    return ours;
}

// The slot's handler, which its writer stored after releasing the
// dispatcher, becomes the new extrinsic.
void Adopt(void *L, Entry &e) {
    Release(L, e.parts[EXTRINSIC].ref);
    e.parts[EXTRINSIC] = {static_cast<int>(e.slot[0]), e.slot[1]};
    e.dispatcher = 0;
    Install(L, e);
}

void Normalize(void *L, Entry &e) {
    if (!Holds(L, e))
        Adopt(L, e);
}

void NormalizeFrame(void *frame) {
    const auto it = g_byFrame.find(frame);
    if (it == g_byFrame.end())
        return;
    void *L = Game::Lua::State();
    for (uint32_t *slot : it->second)
        Normalize(L, g_entries[slot]);
}

void OnFrameNode(void *object, const void * /*node*/, void * /*status*/) { NormalizeFrame(object); }

const Xml::FrameLoad::AutoSubscribe _loadSub{&OnFrameNode};

// ---- SetScript / GetScript --------------------------------------------------

ScriptFn_t g_origSetScript = nullptr;
ScriptFn_t g_origGetScript = nullptr;

uint32_t *ArgSlot(void *L, void **frame) {
    *frame = Game::Lua::ResolveObject(L, 1);
    if (*frame == nullptr || !Game::Lua::IsString(L, 2))
        return nullptr;
    return Resolve(*frame, Game::Lua::ToString(L, 2));
}

// The engine's SetScript always stores into the slot once it returns.
int __fastcall SetScript_h(void *L) {
    const int n = g_origSetScript(L);
    void *frame = nullptr;
    if (uint32_t *slot = ArgSlot(L, &frame))
        if (Entry *e = Find(slot))
            Adopt(L, *e);
    return n;
}

int BindingArg(void *L, int index) {
    if (!Game::Lua::IsNumber(L, index))
        return EXTRINSIC;
    const int b = static_cast<int>(Game::Lua::ToNumber(L, index));
    return b == INTRINSIC_PRECALL || b == INTRINSIC_POSTCALL ? b : EXTRINSIC;
}

// The engine's GetScript validates the frame and the name and pushes the
// slot; that value is then replaced with the binding asked for. The
// context adoption is the engine's (0x00774889), for the binding's context.
// The stack above the arguments is the caller's (HookScript keeps its hook
// there), so the result goes on top of it.
int __fastcall GetScript_h(void *L) {
    const int binding = BindingArg(L, 3);
    void *frame = nullptr;
    uint32_t *slot = ArgSlot(L, &frame);
    Entry *e = slot != nullptr ? Find(slot) : nullptr;
    if (e == nullptr && binding == EXTRINSIC)
        return g_origGetScript(L);
    const int n = g_origGetScript(L);
    if (n != 1)
        return n;
    Game::Lua::SetTop(L, -2);
    const Part *p = e != nullptr ? &e->parts[binding] : nullptr;
    if (p == nullptr || p->ref <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    if (p->context != 0 && Game::Read<int32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH) != 0)
        Game::Ref<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT) = p->context;
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, p->ref);
    return 1;
}

const Game::HookAutoRegister _setScriptHook{Offsets::FUN_SCRIPT_FRAME_SETSCRIPT,
                                            reinterpret_cast<void *>(&SetScript_h),
                                            reinterpret_cast<void **>(&g_origSetScript)};
const Game::HookAutoRegister _getScriptHook{Offsets::FUN_SCRIPT_FRAME_GETSCRIPT,
                                            reinterpret_cast<void *>(&GetScript_h),
                                            reinterpret_cast<void **>(&g_origGetScript)};

// ---- lifetime ---------------------------------------------------------------

void OnFrameDestroyed(void *frame) {
    const auto it = g_byFrame.find(frame);
    if (it == g_byFrame.end())
        return;
    void *L = Game::Lua::State();
    for (uint32_t *slot : it->second) {
        Entry &e = g_entries[slot];
        for (Part &p : e.parts)
            Release(L, p.ref);
        g_entries.erase(slot);
    }
    g_byFrame.erase(it);
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

// The refs die with the Lua state.
void PrepareForReload() {
    g_entries.clear();
    g_byFrame.clear();
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

void RegisterLuaFunctions() {
    void *L = Game::Lua::State();
    Game::Lua::SetGlobalNumber(L, "LE_SCRIPT_BINDING_TYPE_INTRINSIC_PRECALL", INTRINSIC_PRECALL);
    Game::Lua::SetGlobalNumber(L, "LE_SCRIPT_BINDING_TYPE_EXTRINSIC", EXTRINSIC);
    Game::Lua::SetGlobalNumber(L, "LE_SCRIPT_BINDING_TYPE_INTRINSIC_POSTCALL", INTRINSIC_POSTCALL);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

// Runs each binding with the dispatcher's arguments, inside its context as
// the engine's fire wrapper 0x00702690 does (depth up, context in, run,
// context restored while the depth is nonzero, depth down and floored at 0),
// under the engine's script error handler.
int __fastcall Dispatcher(void *L) {
    const int nargs = Game::Lua::GetTop(L);
    const int errRef = Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_FRAMESCRIPT_ERROR_HANDLER_REF));
    auto &context = Game::Ref<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    auto &depth = Game::Ref<int32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH);
    for (int b = 0; b < BINDING_COUNT; ++b) {
        if (Game::Lua::Type(L, Game::Lua::UpvalueIndex(b + 1)) != Game::Lua::TYPE_FUNCTION)
            continue;
        int errIdx = 0;
        if (errRef > 0) {
            Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, errRef);
            errIdx = nargs + 1;
        }
        Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(b + 1));
        for (int i = 1; i <= nargs; ++i)
            Game::Lua::PushValue(L, i);
        const uint32_t saved = context;
        ++depth;
        context = static_cast<uint32_t>(Game::Lua::ToNumber(L, Game::Lua::UpvalueIndex(BINDING_COUNT + b + 1)));
        Game::Lua::PCall(L, nargs, 0, errIdx);
        if (depth != 0)
            context = saved;
        if (--depth <= 0)
            depth = 0;
        Game::Lua::SetTop(L, nargs);
    }
    return 0;
}

void CaptureIntrinsic(void *frame, const void *def) {
    void *L = Game::Lua::State();
    for (const void *c = Xml::FirstChild(def); c != nullptr; c = Xml::NextSibling(c)) {
        if (!Xml::TagIs(c, "Scripts"))
            continue;
        for (const void *s = Xml::FirstChild(c); s != nullptr; s = Xml::NextSibling(s)) {
            uint32_t *slot = Resolve(frame, Xml::Tag(s));
            if (slot == nullptr || slot[0] == 0)
                continue;
            const char *order = Xml::Attr(s, "intrinsicOrder");
            const Binding b = order != nullptr && Game::StrCmpI(order, "postcall") == 0 ? INTRINSIC_POSTCALL
                                                                                        : INTRINSIC_PRECALL;
            const Part handler{static_cast<int>(slot[0]), slot[1]};
            slot[0] = 0;
            slot[1] = 0;
            Entry &e = Acquire(L, frame, slot);
            Release(L, e.parts[b].ref);
            e.parts[b] = handler;
            Install(L, e);
        }
    }
}

bool HookIntrinsic(void *L, void *frame, Binding binding) {
    uint32_t *slot = Resolve(frame, Game::Lua::ToString(L, 2));
    if (slot == nullptr)
        return false;
    Entry &e = Acquire(L, frame, slot);
    Normalize(L, e);
    Part &p = e.parts[binding];
    if (p.ref > 0) {
        Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, p.ref); // [.., old]
        Game::Lua::PushValue(L, 3);                              // [.., old, hook]
        HookSecureFunc::PushWrapper(L);                          // [.., wrapper]
        Release(L, p.ref);
    } else {
        Game::Lua::PushValue(L, 3);
        p.context = Game::Read<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    }
    const int ref = Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
    p.ref = ref > 0 ? ref : 0;
    Install(L, e);
    return true;
}

} // namespace Frame::ScriptBindings
