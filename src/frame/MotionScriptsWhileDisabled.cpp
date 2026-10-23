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

// `button:SetMotionScriptsWhileDisabled(enabled)` /
// `button:GetMotionScriptsWhileDisabled()` and the XML attribute
// `motionScriptsWhileDisabled`.
//
// 1.12's button enter and leave (FUN_BUTTON_ON_ENTER / FUN_BUTTON_ON_LEAVE)
// do nothing for a disabled button but notify their C++ listener, so a
// disabled button never runs OnEnter / OnLeave and cannot show a tooltip.
// 4.3.4's CSimpleButton::OnLayerCursorEnter / Exit are the same functions
// with a per-button flag: a disabled button with the flag set shows its
// companion frame and runs ONLY the OnEnter / OnLeave script - no highlight,
// no label refresh, no drag reset. The co-hooks add that branch. 1.12's
// companion frame (+0xA4) is set by no code the class runs, so the branch
// is only the script, fired as the frame's own enter/leave fires it.
//
// The original runs first and the script after it: a script that enables
// the button would otherwise run the enabled path in the original too, and
// fire OnEnter twice.
//
// 4.3.4 keeps the flag in the button (m_motionScriptsWhileDisabled). 1.12's
// button has no room for it, so it lives here, keyed by the button, and is
// dropped when the frame is destroyed.

#include "Game.h"
#include "Offsets.h"
#include "frame/Destruction.h"
#include "xml/FrameLoad.h"

#include <cstdint>
#include <unordered_set>

namespace Frame::MotionScriptsWhileDisabled {

namespace {

using OnEnter_t = void(__fastcall *)(void *button, void *edx);
using OnLeave_t = void(__fastcall *)(void *button, void *edx, int removed);
using FireScript_t = void(__fastcall *)(void *frame, void *edx, int *slot);

std::unordered_set<const void *> g_enabled;

OnEnter_t g_origOnEnter = nullptr;
OnLeave_t g_origOnLeave = nullptr;

bool RunsWhileDisabled(void *button) {
    return Game::Read<int>(button, Offsets::OFF_BUTTON_STATE) == Offsets::BUTTON_STATE_DISABLED &&
           g_enabled.count(button) != 0;
}

void Fire(void *button, uint32_t slotOffset) {
    int *slot = &Game::Ref<int>(button, slotOffset);
    if (*slot != 0)
        reinterpret_cast<FireScript_t>(Offsets::FUN_FRAME_FIRE_SCRIPT)(button, nullptr, slot);
}

void __fastcall OnEnter_h(void *button, void *edx) {
    const bool fire = RunsWhileDisabled(button);
    g_origOnEnter(button, edx);
    if (fire)
        Fire(button, Offsets::OFF_FRAME_ONENTER_SLOT);
}

void __fastcall OnLeave_h(void *button, void *edx, int removed) {
    const bool fire = RunsWhileDisabled(button);
    g_origOnLeave(button, edx, removed);
    if (fire)
        Fire(button, Offsets::OFF_FRAME_ONLEAVE_SLOT);
}

const Game::HookAutoRegister _enterHook{Offsets::FUN_BUTTON_ON_ENTER, reinterpret_cast<void *>(&OnEnter_h),
                                        reinterpret_cast<void **>(&g_origOnEnter)};
const Game::HookAutoRegister _leaveHook{Offsets::FUN_BUTTON_ON_LEAVE, reinterpret_cast<void *>(&OnLeave_h),
                                        reinterpret_cast<void **>(&g_origOnLeave)};

void Set(const void *button, bool enabled) {
    if (enabled)
        g_enabled.insert(button);
    else
        g_enabled.erase(button);
}

// 4.3.4 sets the flag only when the node has the attribute (m_buttonFlags
// 0x4), so a template's value stays unless the element names its own.
void OnFrameNode(void *object, const void *node, void * /*status*/) {
    const char *value = Xml::Attr(node, "motionScriptsWhileDisabled");
    if (value == nullptr || *value == '\0' || !Game::Lua::IsA(object, Offsets::VAR_BUTTON_LUA_TYPE_ID))
        return;
    Set(object, Game::StringToFlag(value));
}

const Xml::FrameLoad::AutoSubscribe _xmlSub{&OnFrameNode};

void OnFrameDestroyed(void *frame) { g_enabled.erase(frame); }

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

void *ResolveButton(void *L) { return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_BUTTON_LUA_TYPE_ID); }

// 4.3.4 reads the flag with StringToBOOL(L, 2, 0): no argument clears it.
int __fastcall Script_SetMotionScriptsWhileDisabled(void *L) {
    void *button = ResolveButton(L);
    if (button == nullptr)
        return 0;
    Set(button, Game::Lua::ToFlag(L, 2));
    return 0;
}

// 4.3.4 pushes a boolean.
int __fastcall Script_GetMotionScriptsWhileDisabled(void *L) {
    void *button = ResolveButton(L);
    if (button == nullptr)
        return 0;
    Game::Lua::PushBool(L, g_enabled.count(button) != 0);
    return 1;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"SetMotionScriptsWhileDisabled", &Script_SetMotionScriptsWhileDisabled},
    {"GetMotionScriptsWhileDisabled", &Script_GetMotionScriptsWhileDisabled},
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Opt("enabled", "boolean", "false",
                   "When true, OnEnter and OnLeave also run while the button is disabled."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Req("enabled", "boolean", "True when OnEnter and OnLeave also run while the button is disabled."),
};
const Game::Doc::Function kSet{"Sets whether OnEnter and OnLeave run while the button is disabled.", kSetArgs, {}};
const Game::Doc::Function kGet{"Returns whether OnEnter and OnLeave run while the button is disabled.", {},
                               kGetRets};

const Game::Doc::Method kDocs[] = {
    {"SetMotionScriptsWhileDisabled", &kSet},
    {"GetMotionScriptsWhileDisabled", &kGet},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_BUTTON_METHOD_REGISTRY), kMethods, 2,
                                    kDocs, 2);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::MotionScriptsWhileDisabled
