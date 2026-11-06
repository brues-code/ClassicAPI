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

// `PlayerModel:SetKeepModelOnHide(keep)` / `GetKeepModelOnHide()` — retail's
// CharacterModelBase methods.
//
// A vanilla pane drops its model when it hides (FUN_PLAYERMODEL_HIDE: slot 36
// with null) and builds a new one from its unit or display record when it
// shows (FUN_PLAYERMODEL_SHOW). That is retail's behavior with keep off, the
// default. With keep on:
//   - Hide skips the drop and runs only the base hide.
//   - Show skips the build while the pane still holds its model, so the
//     model comes back as it was (tried-on items, animation).
//   - SetUnit, RefreshUnit, Dress (FUN_PLAYERMODEL_SET_UNIT_GUID) and
//     SetDisplay (FUN_PLAYERMODEL_SET_DISPLAY) only store the new subject
//     while the pane is hidden. A kept model would then be the old subject,
//     so they drop it first, and the next show builds the new one as it
//     does with keep off.
//   - Turning keep off while hidden drops the kept model.

#include "Game.h"
#include "Offsets.h"
#include "model/M2.h"

#include <cstdint>
#include <unordered_set>

namespace Model::KeepOnHide {
namespace {

using PaneFn_t = void(__thiscall *)(void *pane);
using SetUnitGuid_t = void(__thiscall *)(void *pane, uint64_t guid);
using SetDisplay_t = void(__thiscall *)(void *pane, const uint8_t *record);

PaneFn_t g_origHide = nullptr;
PaneFn_t g_origShow = nullptr;
SetUnitGuid_t g_origSetUnitGuid = nullptr;
SetDisplay_t g_origSetDisplay = nullptr;

std::unordered_set<void *> g_keeping; // panes with SetKeepModelOnHide(true)

bool Keeps(void *pane) {
    return g_keeping.count(pane) != 0;
}

// Hide's own drop (slot 36 with null), for a hidden pane still holding a
// model.
void DropIfHidden(void *pane) {
    if (!Pane::IsVisible(pane) && Pane::Model(pane) != nullptr)
        Pane::SetModel(pane, nullptr);
}

void DropIfKeptWhileHidden(void *pane) {
    if (Keeps(pane))
        DropIfHidden(pane);
}

void __fastcall Hide_h(void *pane, void * /*edx*/) {
    if (Keeps(pane)) {
        reinterpret_cast<PaneFn_t>(Offsets::FUN_SIMPLEFRAME_HIDE)(pane);
        return;
    }
    g_origHide(pane);
}

void __fastcall Show_h(void *pane, void * /*edx*/) {
    if (Keeps(pane) && Pane::Model(pane) != nullptr) {
        reinterpret_cast<PaneFn_t>(Offsets::FUN_SIMPLEFRAME_SHOW)(pane);
        return;
    }
    g_origShow(pane);
}

void __fastcall SetUnitGuid_h(void *pane, void * /*edx*/, uint64_t guid) {
    DropIfKeptWhileHidden(pane);
    g_origSetUnitGuid(pane, guid);
}

void __fastcall SetDisplay_h(void *pane, void * /*edx*/, const uint8_t *record) {
    DropIfKeptWhileHidden(pane);
    g_origSetDisplay(pane, record);
}

const Game::HookAutoRegister _hideHook{Offsets::FUN_PLAYERMODEL_HIDE,
                                       reinterpret_cast<void *>(&Hide_h),
                                       reinterpret_cast<void **>(&g_origHide)};
const Game::HookAutoRegister _showHook{Offsets::FUN_PLAYERMODEL_SHOW,
                                       reinterpret_cast<void *>(&Show_h),
                                       reinterpret_cast<void **>(&g_origShow)};
const Game::HookAutoRegister _setUnitHook{Offsets::FUN_PLAYERMODEL_SET_UNIT_GUID,
                                          reinterpret_cast<void *>(&SetUnitGuid_h),
                                          reinterpret_cast<void **>(&g_origSetUnitGuid)};
const Game::HookAutoRegister _setDisplayHook{Offsets::FUN_PLAYERMODEL_SET_DISPLAY,
                                             reinterpret_cast<void *>(&SetDisplay_h),
                                             reinterpret_cast<void **>(&g_origSetDisplay)};

int __fastcall Script_SetKeepModelOnHide(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    if (Game::Lua::ToBoolean(L, 2) != 0) {
        g_keeping.insert(pane);
        return 0;
    }
    if (g_keeping.erase(pane) != 0)
        DropIfHidden(pane);
    return 0;
}

int __fastcall Script_GetKeepModelOnHide(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    Game::Lua::PushBool(L, Keeps(pane));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetKeepModelOnHide", &Script_SetKeepModelOnHide},
    {"GetKeepModelOnHide", &Script_GetKeepModelOnHide},
};

const Game::Doc::Field kKeepArgs[] = {
    Game::Doc::Req("keepModelOnHide", "bool"),
};
const Game::Doc::Function kSetKeep{
    "Sets whether the model stays loaded while the frame is hidden.", kKeepArgs, {}};
const Game::Doc::Field kKeepRets[] = {
    Game::Doc::Req("keepModelOnHide", "bool"),
};
const Game::Doc::Function kGetKeep{
    "Returns whether the model stays loaded while the frame is hidden.", {}, kKeepRets};
const Game::Doc::Method g_methodDocs[] = {
    {"SetKeepModelOnHide", &kSetKeep},
    {"GetKeepModelOnHide", &kGetKeep},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_PLAYERMODEL_METHOD_REGISTRY), g_methods,
        g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Frames are rebuilt on /reload and a new frame can reuse an old address.
void PrepareForReload() {
    g_keeping.clear();
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace
} // namespace Model::KeepOnHide
