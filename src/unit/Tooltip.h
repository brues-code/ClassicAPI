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

#include <cstdint>

namespace Unit::Tooltip {

// Called by the unit-builder co-hook (Tooltip::SetEvents owns the one hook on
// FUN_GAMETOOLTIP_BUILD_UNIT) BEFORE the original runs, so the token is bound
// before the build clears the tooltip and before OnTooltipSetUnit fires —
// a handler calling `self:GetUnit()` sees the token of the unit being built.
// `guid` is the builder's own argument (lo, hi).
void BeforeBuild(void *tooltip, const uint32_t *guid);

// Name the token the next unit build of `tooltip` binds to, for a caller that
// drives a build itself (our SetUnit, Frame::Attributes' unit-frame hover).
// Always pair with ClearStagedToken after the call that may build — a call
// that ends up building nothing must not leave the token for a later,
// unrelated build. Without a stage, a build is a world mouseover.
void StageToken(void *tooltip, const char *token);
void ClearStagedToken();

} // namespace Unit::Tooltip
