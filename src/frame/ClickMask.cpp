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

// `Button:RegisterForClicks("AnyUp" | "AnyDown" | ...)` — the two collective
// click types later clients accept alongside the ten explicit ones.
//
// The engine's RegisterForClicks (FUN_SCRIPT_BUTTON_REGISTERFORCLICKS) walks
// its string arguments, SStrCmpI's each against exactly ten literals
// (LeftButtonDown … Button5Up), treats anything else as contributing NOTHING,
// and ASSIGNS the OR of what it recognized to the button's click mask. So on
// this client `RegisterForClicks("AnyUp")` did not fall back to the default —
// it set the mask to zero and the button silently stopped responding to every
// real click (Button:Click bypasses the mask, so programmatic clicks kept
// working and hid the breakage — which is exactly how it bit the click-bracket
// test harness on 2026-09-14).
//
// We don't reimplement the parser. The override appends the five names each
// collective stands for onto the Lua stack — AnyUp -> the five *Up names,
// AnyDown -> the five *Down names — and tail-calls the engine's own function,
// which then sees the collectives (0) plus their expansions (0x1F00 / 0x001F)
// and does everything else itself: the self typecheck, the parse, the setter
// (FUN_BUTTON_SET_CLICK_MASK), the error text. Appending rather than replacing
// keeps the reshape trivially safe: the engine's loop stops at the first
// non-string, exactly as before, and its unknown-name -> 0 rule already makes
// the leftover "AnyUp" string inert.
//
// Registered on the Button method registry, where the engine's entry lives;
// the registrar pushes each new node to the front of its bucket chain and the
// dispatcher walks from the front, so the most recent registration wins (the
// Texture::Desaturation mechanism), and CheckButton inherits it through the
// type walk. Re-registration is the one binding style another DLL or addon
// can silently displace, so it is reserved for names nobody else contends —
// unlike TargetUnit (see CLAUDE.md), no loaded DLL or addon has cause to
// replace RegisterForClicks.
//
// The XML attribute `registerForClicks` is 3.3.5's CSimpleButton::LoadXML:
// Storm's tokenizer (FUN_STORM_STR_TOKENIZE, the one 3.3.5 calls) splits the
// value on spaces and commas, each name ORs in its bits
// (Frame::MouseRegistration::MaskForName, 3.3.5's MouseButtonStringToBitmask,
// which knows AnyUp / AnyDown), and the result REPLACES the mask through the
// engine's setter, as RegisterForClicks does, after the engine's Button loader
// (Xml::TypeLoad). 1.12's Button loader has no such attribute.

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/MouseRegistration.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cstdint>

namespace Frame::ClickMask {

namespace {

using Tokenize_t = void(__stdcall *)(const char **cursor, char *out, unsigned outSize,
                                     const char *delims, int *outQuoted);
using SetClickMask_t = void(__thiscall *)(void *button, uint32_t mask);

// The engine's own click-type names, in mask-bit order.
constexpr const char *kUpNames[] = {"LeftButtonUp", "MiddleButtonUp", "RightButtonUp",
                                    "Button4Up", "Button5Up"};
constexpr const char *kDownNames[] = {"LeftButtonDown", "MiddleButtonDown",
                                      "RightButtonDown", "Button4Down", "Button5Down"};
constexpr int kNameCount = 5;

int __fastcall Script_RegisterForClicks(void *L) {
    using namespace Game::Lua;
    // Bound the scan to the caller's arguments; the names appended below must
    // not be rescanned. The engine's loop stops at the first non-string too.
    const int top = GetTop(L);
    for (int i = 2; i <= top; ++i) {
        if (!IsString(L, i))
            break;
        const char *name = ToString(L, i);
        const char *const *expansion = nullptr;
        if (Ascii::EqualCI(name, "AnyUp"))
            expansion = kUpNames;
        else if (Ascii::EqualCI(name, "AnyDown"))
            expansion = kDownNames;
        if (expansion != nullptr)
            for (int k = 0; k < kNameCount; ++k)
                PushString(L, expansion[k]);
    }
    return reinterpret_cast<CFunction>(Offsets::FUN_SCRIPT_BUTTON_REGISTERFORCLICKS)(L);
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"RegisterForClicks", &Script_RegisterForClicks},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_BUTTON_METHOD_REGISTRY), g_methods);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

void OnButtonNode(void *object, const void *node, void * /*status*/) {
    const char *value = Xml::Attr(node, "registerForClicks");
    if (value == nullptr || *value == '\0')
        return;
    auto tokenize = reinterpret_cast<Tokenize_t>(Offsets::FUN_STORM_STR_TOKENIZE);
    uint32_t mask = 0;
    char token[1024];
    const char *cursor = value;
    while (cursor != nullptr && *cursor != '\0') {
        token[0] = '\0';
        tokenize(&cursor, token, sizeof token, " ,", nullptr);
        mask |= Frame::MouseRegistration::MaskForName(token);
    }
    reinterpret_cast<SetClickMask_t>(Offsets::FUN_BUTTON_SET_CLICK_MASK)(object, mask);
}

const Xml::TypeLoad::AutoSubscribe _xmlSub{Xml::TypeLoad::BUTTON, &OnButtonNode};

} // namespace

} // namespace Frame::ClickMask
