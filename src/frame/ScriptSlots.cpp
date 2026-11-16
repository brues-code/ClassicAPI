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

#include <unordered_map>
#include <vector>

namespace Frame::ScriptSlots {

namespace {

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

// Each class's slots as byte offsets into the frame, in kEngineNames order,
// keyed by vtable. 3.3.5's GetScriptTime walks the class's script list; 1.12
// only has the resolver, an SStrCmpI chain per name that returns the frame
// plus a fixed offset (0x76A0D0: `lea eax,[edi+0x118]`, `[edi+0x120]`, ...).
// So each class is asked once. This holds because ExtraScripts, the one
// resolver hook, answers only its own names, never these; its cells come from
// its own enumerator below. Node-based map, so a reference stays valid if a
// visit ever walks another class.
std::unordered_map<const void *, std::vector<uint32_t>> g_offsets;

const std::vector<uint32_t> &OffsetsOf(void *frame) {
    const void *vtable = Game::Read<const void *>(frame, 0);
    const auto it = g_offsets.find(vtable);
    if (it != g_offsets.end())
        return it->second;
    const auto resolve = Game::Read<Resolver_t>(vtable, Offsets::VTBL_FRAME_SCRIPT_RESOLVER);
    std::vector<uint32_t> offsets;
    for (const char *name : kEngineNames)
        if (const uint32_t *slot = resolve(frame, nullptr, name))
            offsets.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(slot) -
                                                    reinterpret_cast<uintptr_t>(frame)));
    return g_offsets.emplace(vtable, std::move(offsets)).first->second;
}

} // namespace

void ForEach(void *frame, Visit visit, void *ctx) {
    for (const uint32_t offset : OffsetsOf(frame))
        visit(reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(frame) + offset), ctx);
    for (AutoRegister *r = AutoRegister::head; r != nullptr; r = r->next)
        r->fn(frame, visit, ctx);
}

} // namespace Frame::ScriptSlots
