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

#include "frame/ScriptSlots.h"

#include "Game.h"
#include "Offsets.h"

namespace Frame::ScriptSlots {

namespace {

AutoRegister *g_head = nullptr;

// The base object's (0x00702590), the frame's (FUN_FRAME_SCRIPT_RESOLVER),
// then each type's additions; GameTooltip's last. A name the frame's type
// does not have resolves to 0.
constexpr const char *kEngineNames[] = {
    "OnEvent",
    "OnLoad", "OnSizeChanged", "OnUpdate", "OnShow", "OnHide", "OnEnter", "OnLeave",
    "OnMouseDown", "OnMouseUp", "OnMouseWheel", "OnDragStart", "OnDragStop",
    "OnReceiveDrag", "OnChar", "OnKeyDown", "OnKeyUp",
    "OnClick", "OnDoubleClick",
    "OnEnterPressed", "OnEscapePressed", "OnSpacePressed", "OnTabPressed", "OnTextChanged",
    "OnTextSet", "OnCursorChanged", "OnInputLanguageChanged", "OnEditFocusGained",
    "OnEditFocusLost",
    "OnUpdateModel", "OnAnimFinished",
    "OnHorizontalScroll", "OnVerticalScroll", "OnScrollRangeChanged",
    "OnHyperlinkEnter", "OnHyperlinkLeave", "OnHyperlinkClick",
    "OnMessageScrollChanged",
    "OnValueChanged",
    "OnColorSelect",
    "OnMovieFinished", "OnMovieShowSubtitle", "OnMovieHideSubtitle",
    "OnTooltipAddMoney", "OnTooltipCleared", "OnTooltipSetDefaultAnchor",
};

// `__thiscall(frame, name) -> slot`, ret 4, modelled with a dummy edx.
using Resolver_t = const uint32_t *(__fastcall *)(void *frame, void *edx, const char *name);

} // namespace

AutoRegister::AutoRegister(Enumerator f) : fn(f), next(g_head) { g_head = this; }

void ForEach(void *frame, Visit visit, void *ctx) {
    const void *vtable = Game::Read<const void *>(frame, 0);
    const auto resolve = Game::Read<Resolver_t>(vtable, Offsets::VTBL_FRAME_SCRIPT_RESOLVER);
    for (const char *name : kEngineNames)
        if (const uint32_t *slot = resolve(frame, nullptr, name))
            visit(slot, ctx);
    for (AutoRegister *r = g_head; r != nullptr; r = r->next)
        r->fn(frame, visit, ctx);
}

} // namespace Frame::ScriptSlots
