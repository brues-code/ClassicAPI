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

// Input-method composition: `editBox:IsInIMECompositionMode()` and the
// `OnCharComposition(self, text)` script.
//
// 1.12's edit box already handles input-method events the way 4.3.4's
// OnLayerIme does, and keeps the same composition flag
// (OFF_EDITBOX_IME_COMPOSITION), so IsInIMECompositionMode reads it as
// 4.3.4's does: 1 or nil.
//
// When the composition string changes, both clients insert it into the box;
// 4.3.4 then runs OnCharComposition with that string. 1.12 has no such
// script. It is an EditBox Frame::ExtraScripts script, and the input-method
// slot's one insert call is redirected to a function that makes the same
// insert and then runs the script.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "frame/ExtraScripts.h"
#include "frame/ScriptArgs.h"

#include <cstdint>

namespace EditBox::Ime {

namespace {

const Frame::ExtraScripts::Script g_onCharComposition{"oncharcomposition", Offsets::VAR_EDITBOX_LUA_TYPE_ID};

using Insert_t = void(__fastcall *)(void *editBox, void *edx, const char *text, const char *context, int runScripts,
                                    int highlight);

void __fastcall CompositionInsert(void *editBox, void *edx, const char *text, const char *context, int runScripts,
                                  int highlight) {
    reinterpret_cast<Insert_t>(Offsets::FUN_EDITBOX_INSERT)(editBox, edx, text, context, runScripts, highlight);
    const uint32_t *slot = g_onCharComposition.Slot(editBox);
    if (slot == nullptr || slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, text);
    Frame::ScriptArgs::RunSlotWithValues(L, slot, editBox, top + 1, 1);
    Game::Lua::SetTop(L, top);
}

int __fastcall Script_IsInIMECompositionMode(void *L) {
    void *editBox = Game::Lua::ResolveEditBox(L);
    if (editBox == nullptr)
        return 0;
    if (Game::Read<int>(editBox, Offsets::OFF_EDITBOX_IME_COMPOSITION) != 0)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"IsInIMECompositionMode", &Script_IsInIMECompositionMode},
};

const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("composing", "number", nullptr, "1 while an input method composes text in the box, else nil."),
};
const Game::Doc::Function kIsInIMECompositionMode{
    "Returns whether an input method is composing text in the edit box.", {}, kRets};

const Game::Doc::Method g_methodDocs[] = {
    {"IsInIMECompositionMode", &kIsInIMECompositionMode},
};

void RegisterLuaFunctions() {
    Common::RedirectCall(Offsets::CALL_EDITBOX_IME_COMPOSITION_INSERT, Offsets::FUN_EDITBOX_INSERT,
                         reinterpret_cast<const void *>(&CompositionInsert));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_EDITBOX_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace EditBox::Ime
