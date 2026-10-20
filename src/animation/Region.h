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

// The region side of the animation system: 4.3.4's CScriptRegion animation
// members (m_animGroups, m_playingAnimGroups, the CLayoutFrame animation
// data) and their virtuals on CSimpleFrame and CSimpleRegion, for 1.12's
// frames, textures and font strings, which have no room for them.
namespace Animation {
class Group;
}

namespace Animation::Region {

// CScriptRegion::RegisterAnimGroup - the region owns the group from now on.
void RegisterGroup(void *region, Group *group);
// CScriptRegion::NotifyAnimBegin / NotifyAnimEnd.
void NotifyBegin(void *region, Group *group);
void NotifyEnd(void *region, Group *group);
// CScriptRegion::PreOnAnimUpdate: put the animated state back to its base
// before the animations apply themselves for this update.
void PreOnAnimUpdate(void *region);
// CScriptRegion::AddAnimAlpha(target, alpha) on the group's region.
void AddAnimAlpha(void *region, void *target, int16_t alpha);
// CScriptObject::GetScriptObjectByName for a region (Animation:Play's
// target lookup): the region named `name`, or nullptr.
void *FindRegionByName(const char *name);

} // namespace Animation::Region
