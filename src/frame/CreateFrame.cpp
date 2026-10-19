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

// `CreateFrame(frameType [, name] [, parent] [, template] [, id])` — 4.3.4's
// Script_CreateFrame (0x0083FB40) on vanilla's frame builder.
//
// 4.3.4 differs from vanilla's Script_CreateFrame (0x007060B0) in two places:
//   - `template` is a list. Each name, tokenized on " ," as `inherits=` is,
//     must resolve or CreateFrame raises "Couldn't find inherited node"; the
//     whole string then goes on the request node, and the builder reads it
//     as a list (Xml::Inherits — the same path an XML `inherits="A, B"`
//     takes). Vanilla looks the whole string up as one name.
//   - `id` (5th): a string, or a number, set as the node's `id` attribute,
//     which the frame pre-load (0x00769770) stores before OnLoad runs.
//     Vanilla ignores the 5th argument.
// The id has to be on the request node before the builder runs, and vanilla
// builds and frees that node inside its own body, so this replaces the
// function rather than wrapping it: the same steps, in the same order, with
// the engine's own node constructor, attribute setter, builder, status
// object and script registration (Offsets FUN_SCRIPT_CREATEFRAME).
//
// Cooldown: 4.3.4 has a native "Cooldown" frame type (CGCooldown) with
// SetCooldown / SetReverse / GetReverse / SetDrawEdge / GetDrawEdge.
// Vanilla draws cooldown sweeps with a Model built from FrameXML's
// CooldownFrameTemplate (the UI-Cooldown-Indicator model, driven by
// CooldownFrame_SetTimer), and its frame factory has no "Cooldown"
// ("Unknown frame type 'Cooldown'"). So a "Cooldown" request is built as
// that Model -- the node's tag is "Model", and a missing template defaults
// to CooldownFrameTemplate -- and then marked with
// `Frame::Cooldown::Mark`, which gives it 4.3.4's five methods and makes
// it report its type as "Cooldown". See src/frame/Cooldown.cpp.

#include "Game.h"
#include "Offsets.h"
#include "frame/Cooldown.h"
#include "xml/FrameLoad.h"
#include "xml/Inherits.h"

#include <cstdint>
#include <cstring>

namespace Frame::CreateFrame {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using NodeCtor_t = void *(__thiscall *)(void *node, void *parent, const char *tag);
using NodeDtor_t = void(__fastcall *)(void *node);
using SetAttr_t = void(__thiscall *)(void *node, const char *name, const char *value);
using Build_t = void *(__fastcall *)(const void *node, void *parent, void *status);
using StatusReset_t = void(__thiscall *)(void *self);

// LUA_TNONE: lua_type of an index past the top.
constexpr int kTypeNone = -1;

// 4.3.4's usage line; vanilla's (0x00872F60) has no `id`.
constexpr const char *kUsage =
    "Usage: CreateFrame(\"frameType\" [, \"name\"] [, parent] [, \"template\"] [, id])";

// Kept only because HookAutoRegister needs a trampoline slot; the
// replacement never calls it.
ScriptFn_t g_origCreateFrame = nullptr;

const char *EngineString(uintptr_t address) {
    return reinterpret_cast<const char *>(address);
}

// The XML-build status object: the 5-dword error accumulator Script_CreateFrame
// stack-builds ({vtable, 8, &self+8, (&self+8)|1, 0}, 0x0070625A..77) and
// resets with FUN_FRAMESCRIPT_STATUS_RESET.
struct BuildStatus {
    const void *vtable;
    int kind;
    void *head;
    uintptr_t tail;
    int count;
};

void InitStatus(BuildStatus *s) {
    s->vtable = reinterpret_cast<const void *>(
        static_cast<uintptr_t>(Offsets::PTR_TEXLOAD_DESC_VTBL));
    s->kind = 8;
    s->head = &s->head;
    s->tail = reinterpret_cast<uintptr_t>(&s->head) | 1u;
    s->count = 0;
}

void ResetStatus(BuildStatus *s) {
    reinterpret_cast<StatusReset_t>(
        static_cast<uintptr_t>(Offsets::FUN_FRAMESCRIPT_STATUS_RESET))(s);
}

void SetAttr(void *node, const char *name, const char *value) {
    reinterpret_cast<SetAttr_t>(Offsets::FUN_XML_NODE_SET_ATTRIBUTE)(node, name, value);
}

int __fastcall CreateFrame_h(void *L) {
    using namespace Game::Lua;

    const int parentType = Type(L, 3);
    if (!IsString(L, 1) ||
        (parentType != kTypeNone && parentType != TYPE_NIL && parentType != TYPE_TABLE)) {
        Error(L, kUsage);
        return 0;
    }
    const char *frameType = ToString(L, 1);
    const char *name = ToString(L, 2);
    const char *inherits = ToString(L, 4);

    void *parent = nullptr;
    if (parentType == TYPE_TABLE) {
        parent = ResolveObject(L, 3);
        if (parent == nullptr) {
            Error(L, EngineString(Offsets::STR_CREATEFRAME_PARENT_NO_THIS));
            return 0;
        }
        if (!IsA(parent, Offsets::VAR_FRAME_LUA_TYPE_ID)) {
            Error(L, EngineString(Offsets::STR_CREATEFRAME_WRONG_PARENT));
            return 0;
        }
    }

    if (Type(L, 4) == TYPE_STRING) {
        char missing[Xml::Inherits::kTokenSize] = {};
        Xml::Inherits::ForEach(inherits, [&](const char *t) {
            if (missing[0] == '\0' && Xml::Template(t) == nullptr)
                strcpy_s(missing, t);
        });
        if (missing[0] != '\0') {
            Error(L, EngineString(Offsets::STR_CREATEFRAME_NO_INHERITED), missing);
            return 0;
        }
    }

    const bool isCooldown = _stricmp(frameType, "Cooldown") == 0;
    if (isCooldown && (inherits == nullptr || *inherits == '\0'))
        inherits = "CooldownFrameTemplate";
    const char *id = IsString(L, 5) ? ToString(L, 5) : nullptr;

    alignas(4) uint8_t node[Offsets::XML_NODE_SIZE];
    reinterpret_cast<NodeCtor_t>(Offsets::FUN_XML_NODE_CTOR)(node, nullptr,
                                                            isCooldown ? "Model" : frameType);
    if (name != nullptr)
        SetAttr(node, "name", name);
    if (parent != nullptr)
        SetAttr(node, "parent", "");
    if (inherits != nullptr)
        SetAttr(node, "inherits", inherits);
    if (id != nullptr)
        SetAttr(node, "id", id);

    BuildStatus status;
    InitStatus(&status);
    void *frame = reinterpret_cast<Build_t>(Offsets::FUN_XML_FRAME_BUILD)(node, parent, &status);
    if (frame != nullptr) {
        PushObject(L, frame);
        if (isCooldown)
            Frame::Cooldown::Mark(frame);
    }
    ResetStatus(&status);
    reinterpret_cast<NodeDtor_t>(Offsets::FUN_XML_NODE_DTOR)(node);

    if (frame == nullptr) {
        Error(L, EngineString(Offsets::STR_CREATEFRAME_UNKNOWN_TYPE), frameType);
        return 0;
    }
    return 1;
}

const Game::HookAutoRegister _createFrameHook{
    Offsets::FUN_SCRIPT_CREATEFRAME,
    reinterpret_cast<void *>(&CreateFrame_h),
    reinterpret_cast<void **>(&g_origCreateFrame)};

} // namespace

} // namespace Frame::CreateFrame
