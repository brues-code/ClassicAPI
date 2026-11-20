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

// `button:RegisterForMouse(...buttons)` — which mouse buttons run the
// button's OnMouseDown / OnMouseUp scripts. The 3.80.2 client's
// (FUN_140ACB390, mask at button +0x280): AnyUp + AnyDown by default, each
// call replaces the last, no argument registers nothing, a name that is not
// a string or not a click name raises the usage error, and OnMouseUp needs
// its button's down registered too. Only those two scripts are affected
// here — clicks keep RegisterForClicks, drags RegisterForDrag.
//
// 1.12 has no such filter: the frame's button-down / button-up slots
// (0x0076BF70 / 0x0076C040, shared by every frame type and called first by
// the button's own pair) run OnMouseDown / OnMouseUp for any button, through
// the variadic forwarder with ("%s", the engine's name literal). That fire is
// the one place the script — and only the script — can be refused while the
// drag arming before it and the click logic after it run untouched, and
// `Frame::ClickEvents` already owns both slots on the shared runner hook, so
// it asks `Accepts` there. No hook of our own.
//
// The mask is the engine's own click-mask encoding (down bits 0x1F, up bits
// the same << 8; FUN_BUTTON_CLICK's 1/2/4/8/0x10 order); the names are
// 3.80.2's, Button6 to Button31 included, which add no bit: 1.12 has no
// such buttons. Only buttons that called RegisterForMouse with something
// other than the default get an entry; Frame::Destruction drops it with the
// button. MaskForName also parses the XML `registerForClicks` names
// (Frame::ClickMask), where an unknown name adds nothing.

#include "frame/MouseRegistration.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/ClickEvents.h"
#include "frame/Destruction.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace Frame::MouseRegistration {

namespace {

constexpr uint32_t kAnyDown = 0x001F;
constexpr uint32_t kAnyUp = 0x1F00;
constexpr uint32_t kDefault = kAnyDown | kAnyUp;


std::unordered_map<const void *, uint32_t> g_masks;

// The down bit of the button a fire names; 0 for "UNKNOWN".
uint32_t DownBitFor(const char *name) {
    for (uint32_t bit = 0x01; bit <= 0x10; bit <<= 1)
        if (name == Frame::ClickEvents::ButtonNameForMask(bit))
            return bit;
    return 0;
}

int __fastcall Script_RegisterForMouse(void *L) {
    using namespace Game::Lua;
    void *button = ResolveTypedObject(L, 1, Offsets::VAR_BUTTON_LUA_TYPE_ID);
    if (button == nullptr)
        return 0;
    uint32_t mask = 0;
    const int top = GetTop(L);
    for (int i = 2; i <= top; ++i) {
        uint32_t bits = 0;
        if (Type(L, i) != TYPE_STRING || !ParseName(ToString(L, i), bits)) {
            Error(L, "Usage: self:RegisterForMouse([button1, ...])");
            return 0;
        }
        mask |= bits;
    }
    if (mask == kDefault)
        g_masks.erase(button);
    else
        g_masks[button] = mask;
    return 0;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"RegisterForMouse", &Script_RegisterForMouse},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Vararg("string", "The mouse buttons and actions, such as \"LeftButtonDown\" or \"AnyUp\"."),
};
const Game::Doc::Function kDoc{"Sets which mouse buttons run the OnMouseDown and OnMouseUp scripts.", kArgs, {}};
const Game::Doc::Method kDocs[] = {{"RegisterForMouse", &kDoc}};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_BUTTON_METHOD_REGISTRY), kMethods, kDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

void OnFrameDestroyed(void *frame) { g_masks.erase(frame); }

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

} // namespace

bool ParseName(const char *name, uint32_t &mask) {
    mask = 0;
    if (Ascii::EqualCI(name, "AnyDown")) {
        mask = kAnyDown;
        return true;
    }
    if (Ascii::EqualCI(name, "AnyUp")) {
        mask = kAnyUp;
        return true;
    }
    const size_t len = std::strlen(name);
    uint32_t bit = 0;
    if (len > 4 && Ascii::EqualCI(name + len - 4, "Down") &&
        Frame::ClickEvents::ParseButtonName(name, len - 4, bit)) {
        mask = bit;
        return true;
    }
    if (len > 2 && Ascii::EqualCI(name + len - 2, "Up") &&
        Frame::ClickEvents::ParseButtonName(name, len - 2, bit)) {
        mask = bit << 8;
        return true;
    }
    return false;
}

uint32_t MaskForName(const char *name) {
    uint32_t mask = 0;
    return ParseName(name, mask) ? mask : 0;
}

bool Accepts(void *frame, bool down, const char *name) {
    if (g_masks.empty())
        return true;
    const auto it = g_masks.find(frame);
    if (it == g_masks.end())
        return true;
    const uint32_t bit = DownBitFor(name);
    if ((it->second & bit) == 0) // OnMouseUp needs the down registered too
        return false;
    return down || (it->second & (bit << 8)) != 0;
}

} // namespace Frame::MouseRegistration
