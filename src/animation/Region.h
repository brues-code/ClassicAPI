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
class Object;
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
// Later clients' absolute alpha (Alpha fromAlpha / toAlpha): the same target
// rule as AddAnimAlpha, but the alpha becomes `alpha` instead of growing.
void SetAnimAlpha(void *region, void *target, uint8_t alpha);
// AnimationGroup setToFinalAlpha: the alpha that `target` (the group's
// `region` when null) shows now becomes the one the group's end restores.
void KeepAnimAlpha(void *region, void *target);
// CScriptRegion::AddAnimTranslation(target, translation), in layout units:
// moves the drawn textures and font strings, never a rect.
void AddAnimTranslation(void *region, void *target, float x, float y);
// CScriptRegion::AddAnimScale(target, origin, offset, scale): `x`/`y` are the
// share of the way to the origin (1 - scale at full progress).
void AddAnimScale(void *region, void *target, int origin, float offsetX, float offsetY, float x, float y);
// CScriptRegion::AddAnimRotation(target, origin, offset, radians),
// counter-clockwise.
void AddAnimRotation(void *region, void *target, int origin, float offsetX, float offsetY, float radians);
// A font string's glyphs were just baked (the draw builder): turn and scale
// them by its animation, and remember what they were baked with.
void FontStringBaked(void *node, void *fontString);
// The stored corners (TL BL TR BR {x, y, z}) of a texture a position group
// is playing on, or nullptr. Each update draws the animation on top of them,
// so a change to the texture's own shape (Texture::Transform) goes here, not
// to the drawn corners.
float *TextureBaseCorners(void *texture);
// Whether the font string's glyphs carry an animation's rotation or scale:
// then a change to its own rotation has to bake them again.
bool StringAnimated(void *fontString);
// CScriptObject::GetScriptObjectByName for a region (Animation:Play's
// target lookup): the region named `name`, or nullptr.
void *FindRegionByName(const char *name);
// The region a later-client targetKey path names, read from `from`: each
// dot-separated part is `$parent` (the object's parent) or a key on the
// current object's table. Nullptr unless the path ends on a region.
void *FindRegionByKey(Object *from, const char *path);

} // namespace Animation::Region
