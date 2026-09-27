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

namespace EquipmentSet::Tooltip {

// `GameTooltip:SetEquipmentSet("setName")` — self at stack[1], the name at
// stack[2]. Exposed so `GameTooltip:SetAction` can show an equipment-set
// button the same way.
int __fastcall Script_GameTooltipSetEquipmentSet(void *L);

} // namespace EquipmentSet::Tooltip
