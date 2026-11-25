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
// Alpha's SetChange (4.3.4's CScriptRegion::AddAnimAlpha) on the group's
// region: the stored alpha plus `alpha`.
void AddAnimAlpha(void *region, void *target, int16_t alpha);
// Alpha's fromAlpha / toAlpha on the group's region: a frame target below
// the region takes it, and its alpha becomes `alpha`.
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
// The 3.80.2 client's other animation types, on the animation's target or
// the group's `region`. VertexColor: the region's colour becomes `bgra` and
// stays (a frame ignores it).
void SetAnimVertexColor(void *region, void *target, uint32_t bgra);
// FlipBook: the texture shows the cell for `progress` and keeps it.
void SetAnimFlipBookFrame(void *region, void *target, uint32_t rows, uint32_t columns, uint32_t frames,
                          uint32_t frameWidth, uint32_t frameHeight, float progress);
// RadialProgress: the texture's radial progress bar shows `percent` and
// keeps it (other regions ignore it).
void SetAnimRadialProgress(void *region, void *target, float percent);
// TextureCoordTranslation: (u, v) added to the stored coordinates of every
// texture it reaches (a frame passes it down).
void AddAnimTexCoord(void *region, void *target, float u, float v);
// LineTranslation / LineScale: a Line's corners moved or scaled in the
// line's own frame (a frame passes them down; other regions ignore them).
void AddAnimLineTranslation(void *region, void *target, float x, float y);
void AddAnimLineScale(void *region, void *target, int origin, float offsetX, float offsetY, float x, float y);
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
