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

#pragma once

// The engine's own tooltip-building primitives, for C-built tooltips
// (SetTotem, SetEquipmentSet, SetHyperlinkCompareItem, quest links):
// the per-tooltip clear, the raw add-line, and the frame Show script.

#include "Offsets.h"

#include <cstdint>

namespace Tooltip::Lines {

// Empties the tooltip (FUN_GAMETOOLTIP_CLEAR). `__fastcall(self)`.
inline void Clear(void *self) {
    using Clear_t = void(__fastcall *)(void *self);
    reinterpret_cast<Clear_t>(Offsets::FUN_GAMETOOLTIP_CLEAR)(self);
}

// Appends a left-aligned line (FUN_GAMETOOLTIP_ADD_LINE). `color` is the
// packed 0xAARRGGBB value the line-color setter reads as {b,g,r,a} - the
// `UI::ColorData::ByTag` packing. It must outlive the call (its address is
// passed through).
inline void Add(void *self, const char *text, const uint32_t &color, bool wrap = false) {
    using AddLine_t = void(__thiscall *)(void *self, const char *left, const char *right,
                                         const void *leftColor, const void *rightColor,
                                         int wrap);
    reinterpret_cast<AddLine_t>(Offsets::FUN_GAMETOOLTIP_ADD_LINE)(
        self, text, nullptr, &color, nullptr, wrap ? 1 : 0);
}

// Shows the tooltip at Lua stack[1] (the engine's Script_Show reads self
// there).
inline void Show(void *L) {
    using Show_t = int(__fastcall *)(void *L);
    reinterpret_cast<Show_t>(Offsets::FUN_SCRIPT_FRAME_SHOW)(L);
}

} // namespace Tooltip::Lines
