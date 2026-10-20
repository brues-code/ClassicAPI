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

// <Animations>, <AnimationGroup> and the animation elements: 4.3.4's
// CScriptRegion::LoadXML_AnimGroup, CSimpleAnimGroup::LoadXML and
// CSimpleAnim::LoadXML (and the subclasses'), on 1.12's XML nodes. A
// virtual top-level <AnimationGroup> or animation element is a template:
// 1.12's file loader registers every virtual named top-level node
// (RegisterXMLTemplate), whatever its tag, so `inherits` resolves through
// Xml::Template as 4.3.4's XMLHashNodeRef does.
namespace Animation {
class Group;
class Anim;
} // namespace Animation

namespace Animation::Xml {

// The <Animations> child of a region's node, if any: each <AnimationGroup>
// becomes a group of `region`, loaded and post-loaded (OnLoad runs).
void LoadAnimations(void *region, const void *node, void *status);
// CSimpleAnimGroup::LoadXML / CSimpleAnim::LoadXML of one node, its
// `inherits` first.
void LoadGroup(Group *group, const void *node, void *status);
void LoadAnim(Anim *anim, const void *node, void *status);
// The template node `name` resolves to, or nullptr.
const void *Template(const char *name);

} // namespace Animation::Xml
