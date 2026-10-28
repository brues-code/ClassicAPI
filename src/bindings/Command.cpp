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

// The SPELL / ITEM / MACRO / CLICK binding commands — 4.3.4's
// CGUIBindings::ExecKey branches (decomp UIBindings.cpp), on 1.12's own
// primitives:
//
//   SPELL x  on release: the spellbook slot named x (FUN_RESOLVE_SPELL_NAME_TO_SLOT)
//            cast through FUN_SPELL_CAST_DISPATCH at you when SELFCAST is held,
//            at your focus when FOCUSCAST is, else with no explicit target.
//   ITEM x   on release: the item named x you carry (equipment, then bags)
//            used through FUN_ITEM_USE with the same target.
//   MACRO x  on release: macro index x when x is all digits, else the macro
//            named x, run through FUN_MACRO_RUN_BODY with the button
//            "LeftButton", as 4.3.4 runs it.
//   CLICK f:b press and release: the frame named f gets a mouse down and a
//            mouse up for button b.
//
// The release timing is 4.3.4's (and vanilla action buttons'): the command
// runs when the key comes back up.
//
// CLICK: 4.3.4's frames take a `btn` argument on OnLayerMouseDown / Up that
// marks a virtual click — no hit test, and a button clicks for ANY press
// (down) or release (up) bit it is registered for. 1.12's take only the mouse
// event and hit-test it, so the virtual branch is built from the button's own
// pieces, in 4.3.4's order: the frame's OnMouseDown / OnMouseUp script with
// the button name, then the Button state machine (click / double-click
// vmethods, SetButtonState). Only frames that are Buttons get the second part,
// as only CSimpleButton overrides the handlers 4.3.4 runs.

#include "bindings/Command.h"

#include "Game.h"
#include "Offsets.h"
#include "bindings/ModifiedClick.h"
#include "frame/ClickFrame.h"
#include "item/Arg.h"
#include "item/Location.h"
#include "item/Use.h"
#include "macro/Execute.h"
#include "spell/Placement.h"
#include "time/Clock.h"
#include "unit/Focus.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace Bindings::Command {

namespace {

constexpr const char kSpell[] = "SPELL ";
constexpr const char kItem[] = "ITEM ";
constexpr const char kMacro[] = "MACRO ";
constexpr const char kClick[] = "CLICK ";

bool HasPrefix(const char *command, const char *prefix) {
    return _strnicmp(command, prefix, std::strlen(prefix)) == 0;
}

// The target 4.3.4 picks for SPELL / ITEM: you for SELFCAST, your focus for
// FOCUSCAST, else none (the engine then uses your target).
uint64_t ModifiedTarget() {
    if (ModifiedClick::IsModifiedClick("SELFCAST"))
        return Unit::Identity::PlayerGuid();
    if (ModifiedClick::IsModifiedClick("FOCUSCAST"))
        return Unit::Focus::Get();
    return 0;
}

void CastSpell(const char *name) {
    Spell::Placement::DispatchSpellCastByName(name, ModifiedTarget());
}

void UseItem(const char *name) {
    const Item::Arg::Resolved byName{0, 0, name};
    Item::Location::ByGUIDResult found{};
    if (!Item::Location::FindByArgNoLua(byName, &found) || found.item == nullptr)
        return;
    Item::Use::UseItem(found.item, ModifiedTarget());
}

// StringToMouseButton: the click mask for a button name, 0 for any other
// (the click vmethod then names it "UNKNOWN").
uint32_t MaskForButton(const char *button) {
    static const struct {
        uintptr_t name;
        uint32_t mask;
    } kButtons[] = {
        {Offsets::VAR_BUTTON_NAME_LEFT, 0x01},  {Offsets::VAR_BUTTON_NAME_RIGHT, 0x04},
        {Offsets::VAR_BUTTON_NAME_MIDDLE, 0x02}, {Offsets::VAR_BUTTON_NAME_4, 0x08},
        {Offsets::VAR_BUTTON_NAME_5, 0x10},
    };
    for (const auto &entry : kButtons)
        if (_stricmp(reinterpret_cast<const char *>(entry.name), button) == 0)
            return entry.mask;
    return 0;
}

// The frame's OnMouseDown / OnMouseUp, fired as the engine fires them.
void FireMouseScript(void *frame, uintptr_t slotOffset, const char *button) {
    using FireVariadic_t = void(__cdecl *)(void *frame, uint32_t *slotPtr, const char *fmt, ...);
    uint32_t *slot = Game::Ptr<uint32_t>(frame, slotOffset);
    if (slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    const int top = L != nullptr ? Game::Lua::GetTop(L) : 0;
    reinterpret_cast<FireVariadic_t>(Offsets::FUN_FRAME_RUN_SCRIPT_VARIADIC)(
        frame, slot, reinterpret_cast<const char *>(Offsets::VAR_SCRIPT_FMT_S), button);
    if (L != nullptr)
        Game::Lua::SetTop(L, top);
}

using ButtonMethod_t = void(__thiscall *)(void *button, uint32_t arg, int flag);

void CallButton(void *button, uintptr_t slot, uint32_t arg) {
    auto *vtable = Game::Read<uintptr_t *>(button, 0);
    reinterpret_cast<ButtonMethod_t>(vtable[slot / sizeof(uintptr_t)])(button, arg, 0);
}

int &ButtonState(void *button) { return Game::Ref<int>(button, Offsets::OFF_BUTTON_STATE); }
bool StateLocked(void *button) {
    return Game::Read<int>(button, Offsets::OFF_BUTTON_STATE_LOCKED) != 0;
}

// The button's click under the engine's "in click" flag.
template <typename Fn> void InClick(Fn fn) {
    auto &flag = Game::Ref<int>(Offsets::VAR_BUTTON_IN_CLICK);
    flag = 1;
    fn();
    flag = 0;
}

// CSimpleButton::OnLayerMouseDown with a `btn`.
void VirtualMouseDown(void *frame, const char *button) {
    FireMouseScript(frame, Offsets::OFF_FRAME_ONMOUSEDOWN_SLOT, button);
    if (!Game::Lua::IsA(frame, Offsets::VAR_BUTTON_LUA_TYPE_ID) ||
        ButtonState(frame) == Offsets::BUTTON_STATE_DISABLED)
        return;
    if (Game::Read<uint32_t>(frame, Offsets::OFF_BUTTON_CLICK_MASK) & 0xFF)
        InClick([&] { CallButton(frame, Offsets::VTBL_BUTTON_CLICK, MaskForButton(button)); });
    if (!StateLocked(frame) && ButtonState(frame) != Offsets::BUTTON_STATE_DISABLED)
        CallButton(frame, Offsets::VTBL_BUTTON_SET_STATE, Offsets::BUTTON_STATE_PUSHED);
}

// CSimpleButton::OnLayerMouseUp with a `btn`.
void VirtualMouseUp(void *frame, const char *button) {
    FireMouseScript(frame, Offsets::OFF_FRAME_ONMOUSEUP_SLOT, button);
    if (!Game::Lua::IsA(frame, Offsets::VAR_BUTTON_LUA_TYPE_ID) ||
        ButtonState(frame) != Offsets::BUTTON_STATE_PUSHED)
        return;
    if (Game::Read<uint32_t>(frame, Offsets::OFF_BUTTON_CLICK_MASK) & 0xFF00) {
        InClick([&] {
            const uint32_t now = Time::Clock::NowMs();
            auto &last = Game::Ref<uint32_t>(frame, Offsets::OFF_BUTTON_LAST_CLICK_MS);
            const uint32_t mask = MaskForButton(button);
            if (Game::Read<uint32_t>(frame, Offsets::OFF_BUTTON_ONDOUBLECLICK_HANDLER) != 0 &&
                last != 0 && Time::Clock::Elapsed(last, now) <= Offsets::BUTTON_DOUBLE_CLICK_MS) {
                CallButton(frame, Offsets::VTBL_BUTTON_DOUBLE_CLICK, mask);
                last = 0;
            } else {
                CallButton(frame, Offsets::VTBL_BUTTON_CLICK, mask);
                last = now;
            }
        });
    }
    if (!StateLocked(frame) && ButtonState(frame) != Offsets::BUTTON_STATE_DISABLED)
        CallButton(frame, Offsets::VTBL_BUTTON_SET_STATE, Offsets::BUTTON_STATE_NORMAL);
}

int Click(const char *target, int isDown) {
    const char *separator = std::strchr(target, ':');
    if (separator == nullptr)
        return 0;
    void *frame = Frame::ClickFrame::Find(Game::Lua::State(), target,
                                          static_cast<size_t>(separator - target));
    if (frame == nullptr || frame == Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR))
        return 0; // no such frame, or the world frame
    const char *button = separator + 1;
    if (isDown)
        VirtualMouseDown(frame, button);
    else
        VirtualMouseUp(frame, button);
    return 1;
}

} // namespace

bool IsDirectAction(const char *command) {
    return command != nullptr && (HasPrefix(command, kSpell) || HasPrefix(command, kItem) ||
                                  HasPrefix(command, kMacro) || HasPrefix(command, kClick));
}

int RunKey(const char *command, int isDown) {
    if (HasPrefix(command, kSpell)) {
        if (!isDown)
            CastSpell(command + sizeof kSpell - 1);
        return 1;
    }
    if (HasPrefix(command, kItem)) {
        if (!isDown)
            UseItem(command + sizeof kItem - 1);
        return 1;
    }
    if (HasPrefix(command, kMacro)) {
        if (!isDown)
            Macro::Execute::Saved(command + sizeof kMacro - 1, "LeftButton");
        return 1;
    }
    return Click(command + sizeof kClick - 1, isDown);
}

} // namespace Bindings::Command
