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

// The region side of the animation system, and the region methods
// CreateAnimationGroup / GetAnimationGroups / StopAnimating.
//
// 4.3.4 (wowdecomp15595 CScriptRegion.cpp, CSimpleFrame.cpp, CSimpleRegion.cpp,
// CSimpleRender.cpp, CLayoutFrame.cpp):
//   - a region owns its groups (m_animGroups) and lists the playing ones
//     (m_playingAnimGroups);
//   - CSimpleFrame::OnLayerUpdate runs OnUpdate, then every group of the
//     frame, then every group of each of its regions;
//   - when a group starts, the region stores the state its animations change
//     (AnimActivated: a frame its own alpha, a texture or font string its
//     vertex alpha), unless an earlier group already did; a frame does this
//     for every frame and region below it too;
//   - every update first puts that state back (PreOnAnimUpdate), then the
//     animations add to it (AddAnimAlpha: a frame's own alpha becomes
//     change + its drawn alpha; a region's vertex alpha change + its own);
//   - when the last group with that kind of animation on the region or any
//     ancestor ends, the stored state goes back (AnimDeactivated).
// So, as in 4.3.4, GetAlpha returns the animated alpha while it plays.
//
// Position animations (Translation, Scale, Rotation) move only what
// is DRAWN, never a frame's rect: a frame hands them to every texture and font
// string below it. A texture moves the four corners its draw record points at
// (+0xD4, TL BL TR BR, the order of 4.3.4's m_positions); a font string moves
// its text block's string position, and is turned and scaled about its origin
// when its glyphs are baked (4.3.4 bakes them again whenever the string's
// rotation or scale changes; so does this, through the engine's node
// invalidate). The glyphs are scaled, not drawn at a larger font size: one
// size per update would fill the glyph cache. Their stored base is what the engine
// itself lays out (4.3.4's StoreAnimPosition): a texture's corners from its
// rect through the engine's corner store, a font string's inset x layout
// scale + its rect's min corner. When the layout moves a region mid-animation,
// the co-hooks on the two OnFrameSizeChanged callbacks move the base and keep
// the animation's offset, as 4.3.4's own OnFrameSizeChanged legs do.
//
// 1.12's regions have no room for these, so they are kept here, keyed by the
// region. A frame's own alpha is Frame::Alpha's (3.x's m_alpha), set through
// the frame's vtable setter as SetAlpha sets it; a region's vertex alpha is the
// engine's region color (FUN_REGION_GET_COLOR / FUN_FONTSTRING_SET_COLOR).
// The groups update from a co-hook of CSimpleFrame::OnFrameUpdate, which the
// engine calls for every shown frame after OnUpdate, so a hidden frame's
// groups wait, as in 4.3.4. Groups die with their frame (Frame::Destruction).

#include "animation/Region.h"

#include "animation/Classes.h"
#include "animation/Xml.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/Alpha.h"
#include "frame/Destruction.h"
#include "texture/Transform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Animation::Region {

namespace {

struct State {
    std::vector<Group *> groups;  // m_animGroups (owned)
    std::vector<Group *> playing; // m_playingAnimGroups
};

// CAnimationData.
struct AnimData {
    bool hasAlpha = false;
    uint8_t alpha = 0xFF;
    bool hasPosition = false;
    // m_position[4]: a texture's four corners {x, y, z}; a font string uses
    // the first as its string position.
    float position[12] = {};
    // A font string's animated rotation and scale (4.3.4's m_rotation /
    // m_scale over the animation base), and what its glyphs were last baked
    // with.
    float rotation = 0.0f;
    float scale = 1.0f;
    float bakedRotation = 0.0f;
    float bakedScale = 1.0f;
};

std::unordered_map<void *, State> g_states;
std::unordered_map<void *, AnimData> g_data;
// Font strings whose rotation or scale an update reset: checked against what
// they were baked with once the update has run.
std::vector<void *> g_touchedStrings;
// Regions (not frames) with groups: their frame's update has to look at its
// regions.
int g_regionsWithGroups = 0;

using FrameUpdate_t = void(__fastcall *)(void *frame, void *edx, float elapsed);
using SetFrameAlpha_t = void(__fastcall *)(void *frame, void *edx, uint32_t alpha);
using GetColor_t = void(__fastcall *)(void *region, void *edx, uint32_t *bgra);
using SetColor_t = void(__fastcall *)(void *region, void *edx, const uint32_t *bgra);
using StoreCorners_t = void(__fastcall *)(void *region, void *edx, float *rect);
using Crop_t = void(__fastcall *)(void *region, void *edx, float *rect);
using SetStringPos_t = void(__fastcall *)(void *block, const float *position);
using NdcToDdc_t = void(__fastcall *)(float *outX, float *outY, float x, float y);
using SizeChanged_t = void(__fastcall *)(void *layout, void *edx, const float *oldRect);
using InvalidateNode_t = void(__fastcall *)(void *node);

FrameUpdate_t g_origFrameUpdate = nullptr;
SizeChanged_t g_origTextureSizeChanged = nullptr;
SizeChanged_t g_origFontStringSizeChanged = nullptr;

bool IsFrame(void *region) { return Game::Lua::IsA(region, Offsets::VAR_FRAME_LUA_TYPE_ID); }
bool IsTexture(void *region) { return Game::Lua::IsA(region, Offsets::VAR_TEXTURE_LUA_TYPE_ID); }
bool IsFontString(void *region) { return Game::Lua::IsA(region, Offsets::VAR_FONTSTRING_LUA_TYPE_ID); }
void *ParentOf(void *region) { return Game::Read<void *>(region, Offsets::OFF_REGION_PARENT); }

State *Find(void *region) {
    const auto it = g_states.find(region);
    return it != g_states.end() ? &it->second : nullptr;
}

AnimData *DataOf(void *region) {
    const auto it = g_data.find(region);
    return it != g_data.end() ? &it->second : nullptr;
}

template <typename Fn> void ForEachChild(void *frame, Fn fn) {
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_CHILD_LIST, fn);
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_REGION_LIST, fn);
}

// ---- The state alpha animations change -----------------------------------------

uint8_t OwnAlpha(void *frame) { return Frame::Alpha::OwnAlpha(frame); }

void SetOwnAlpha(void *frame, uint8_t alpha) {
    void **vtable = *static_cast<void ***>(frame);
    reinterpret_cast<SetFrameAlpha_t>(vtable[Offsets::VTBL_FRAME_SET_FRAME_ALPHA / sizeof(void *)])(frame, nullptr,
                                                                                                 alpha);
}

uint8_t DrawnParentAlpha(void *frame) {
    void *parent = ParentOf(frame);
    return parent != nullptr ? Game::Read<uint8_t>(parent, Offsets::OFF_FRAME_ALPHA) : 0xFF;
}

uint32_t VertexColor(void *region) {
    uint32_t bgra = 0xFFFFFFFF;
    reinterpret_cast<GetColor_t>(Offsets::FUN_REGION_GET_COLOR)(region, nullptr, &bgra);
    return bgra;
}

void SetVertexAlpha(void *region, uint8_t alpha) {
    const uint32_t bgra = (VertexColor(region) & 0x00FFFFFF) | (static_cast<uint32_t>(alpha) << 24);
    reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(region, nullptr, &bgra);
}

// ---- The state position animations change --------------------------------------

float *Corners(void *texture) { return Game::Ptr<float>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS); }

// CSimpleTexture::SetPosition(m_rect, m_positions): the corners from the rect,
// written the way the texture's layout callback (FUN_TEXTURE_ON_FRAME_SIZE_CHANGED)
// writes them - cropped when SetTexCoordModifiesRect is on - so the corner
// store's co-hooks (Texture::Transform) shape them as they do on a layout.
void StoreTextureCorners(void *texture) {
    float rect[4];
    std::memcpy(rect, Game::Ptr<float>(texture, Offsets::OFF_REGION_RECT), sizeof(rect));
    if (Game::Read<int>(texture, Offsets::OFF_REGION_TEXCOORD_MODIFIES_RECT) != 0)
        reinterpret_cast<Crop_t>(Offsets::FUN_REGION_TEXCOORD_CROP)(texture, nullptr, rect);
    reinterpret_cast<StoreCorners_t>(Offsets::FUN_REGION_STORE_CORNERS)(texture, nullptr, rect);
}

void *StringBlock(void *fontString) { return Game::Read<void *>(fontString, Offsets::OFF_FONTSTRING_TEXT_BLOCK); }

// Where the engine lays the string out: inset x layout scale + the rect's min
// corner (the position FUN_FONTSTRING_ON_FRAME_SIZE_CHANGED and SetTextOffset
// pass), as 4.3.4's StoreAnimPosition computes it.
void StringBase(void *fontString, float out[3]) {
    const float *rect = Game::Ptr<const float>(fontString, Offsets::OFF_REGION_RECT);
    const float scale = Game::Read<float>(fontString, Offsets::OFF_LAYOUT_SCALE);
    out[0] = Game::Read<float>(fontString, Offsets::OFF_FONTSTRING_INSET_X) * scale + rect[1];
    out[1] = Game::Read<float>(fontString, Offsets::OFF_FONTSTRING_INSET_Y) * scale + rect[0];
    out[2] = 0.0f;
}

// TextBlockGetStringPos: the string node's position, back in DDC.
bool GetStringPos(void *block, float out[3]) {
    const void *node = Game::Read<void *>(block, Offsets::OFF_TEXTBLOCK_NODE);
    if (node == nullptr)
        return false;
    const float *position = Game::Ptr<const float>(node, Offsets::OFF_TEXT_NODE_POSITION);
    reinterpret_cast<NdcToDdc_t>(Offsets::FUN_COORD_NDC_TO_DDC)(&out[0], &out[1], position[0], position[1]);
    out[2] = position[2];
    return true;
}

void SetStringPos(void *block, const float position[3]) {
    reinterpret_cast<SetStringPos_t>(Offsets::FUN_TEXTBLOCK_SET_STRING_POS)(block, position);
}

// CSimpleTexture / CSimpleFontString::StoreAnimPosition.
void StoreAnimPosition(void *region, AnimData &data) {
    if (IsTexture(region)) {
        StoreTextureCorners(region);
        std::memcpy(data.position, Corners(region), sizeof(data.position));
        data.hasPosition = true;
    } else if (IsFontString(region)) {
        StringBase(region, data.position);
        data.hasPosition = true;
    }
}

void *StringNode(void *fontString) {
    void *block = StringBlock(fontString);
    return block != nullptr ? Game::Read<void *>(block, Offsets::OFF_TEXTBLOCK_NODE) : nullptr;
}

// GxuFontStringInvalidate after GxuFontSetRotation / GxuFontSetScale: the
// glyphs are baked again on the next paint, where FontStringBaked turns and
// scales them. Only when the values differ from the baked ones.
void RebakeIfChanged(void *fontString, const AnimData &data) {
    if (data.rotation == data.bakedRotation && data.scale == data.bakedScale)
        return;
    if (void *node = StringNode(fontString))
        reinterpret_cast<InvalidateNode_t>(Offsets::FUN_TEXT_NODE_INVALIDATE)(node);
}

// AnimData_ResetPositionData: the region goes back to where the layout puts it.
void ResetPosition(void *region, AnimData &data) {
    data.hasPosition = false;
    if (IsTexture(region)) {
        StoreTextureCorners(region);
    } else if (IsFontString(region)) {
        // 4.3.4 leaves the string where the last update put it until the next
        // layout; it goes back now, as the corners of a texture do.
        if (void *block = StringBlock(region)) {
            float base[3];
            StringBase(region, base);
            SetStringPos(block, base);
        }
        data.rotation = 0.0f;
        data.scale = 1.0f;
        RebakeIfChanged(region, data);
    }
}

// CSimpleFrame / CSimpleRegion::AnimActivated.
void AnimActivated(void *region, bool alpha, bool position) {
    const bool frame = IsFrame(region);
    AnimData &data = g_data[region]; // AnimData_Init
    if (alpha) {
        data.hasAlpha = true;
        data.alpha = frame ? OwnAlpha(region) : static_cast<uint8_t>(VertexColor(region) >> 24);
    }
    // A frame stores no position of its own, but it is marked, so the end of
    // its last position group resets everything below it (NotifyEnd reads
    // the mark), as the end of an alpha group does.
    if (position) {
        if (frame)
            data.hasPosition = true;
        else
            StoreAnimPosition(region, data);
    }
    if (frame)
        ForEachChild(region, [alpha, position](void *child) { AnimActivated(child, alpha, position); });
}

// CSimpleFrame / CSimpleRegion::AnimDeactivated.
void AnimDeactivated(void *region, bool alpha, bool position) {
    const bool frame = IsFrame(region);
    AnimData *data = DataOf(region);
    if (alpha) {
        const uint8_t stored = data != nullptr ? data->alpha : 0xFF; // AnimData_GetAlpha
        if (frame) {
            if (stored != OwnAlpha(region))
                SetOwnAlpha(region, stored);
        } else {
            SetVertexAlpha(region, stored);
        }
        if (data != nullptr) {
            data->hasAlpha = false;
            data->alpha = 0xFF;
        }
    }
    if (position && data != nullptr) {
        if (frame)
            data->hasPosition = false;
        else
            ResetPosition(region, *data);
    }
    if (frame)
        ForEachChild(region, [alpha, position](void *child) { AnimDeactivated(child, alpha, position); });
}

// CScriptRegion::HasActiveAlphaAnim / HasActivePositionAnim: a playing group
// with that kind of animation on the region or an ancestor.
template <typename Has> bool HasActiveAnim(void *region, Has has) {
    for (void *r = region; r != nullptr; r = ParentOf(r)) {
        const State *state = Find(r);
        if (state == nullptr)
            continue;
        for (const Group *group : state->playing)
            if (has(group))
                return true;
    }
    return false;
}

// ---- The update ----------------------------------------------------------------------

void UpdateGroups(void *region, float elapsed) {
    State *state = Find(region);
    if (state == nullptr)
        return;
    const std::vector<Group *> groups = state->groups;
    for (Group *group : groups)
        group->OnUpdate(elapsed);
}

void __fastcall FrameUpdate_h(void *frame, void *edx, float elapsed) {
    g_origFrameUpdate(frame, edx, elapsed);
    if (g_states.empty())
        return;
    UpdateGroups(frame, elapsed);
    if (g_regionsWithGroups > 0)
        Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_REGION_LIST,
                                     [elapsed](void *region) { UpdateGroups(region, elapsed); });
    // The font strings this update turned or scaled, or put back.
    for (void *fontString : g_touchedStrings)
        if (const AnimData *data = DataOf(fontString))
            RebakeIfChanged(fontString, *data);
    g_touchedStrings.clear();
}

const Game::HookAutoRegister _frameUpdateHook{Offsets::FUN_FRAME_ON_UPDATE, reinterpret_cast<void *>(&FrameUpdate_h),
                                              reinterpret_cast<void **>(&g_origFrameUpdate)};

// ---- A layout during a position animation -----------------------------------------
//
// The layout writes the region's unanimated position. As in 4.3.4's
// OnFrameSizeChanged legs, the stored base becomes that new position and the
// drawn position keeps the animation's offset from it.

AnimData *PositionData(void *region) {
    if (g_data.empty())
        return nullptr;
    AnimData *data = DataOf(region);
    return data != nullptr && data->hasPosition ? data : nullptr;
}

void __fastcall TextureSizeChanged_h(void *layout, void *edx, const float *oldRect) {
    void *texture = static_cast<char *>(layout) - Offsets::OFF_REGION_ANCHOR;
    AnimData *data = PositionData(texture);
    if (data == nullptr) {
        g_origTextureSizeChanged(layout, edx, oldRect);
        return;
    }
    float animated[12];
    std::memcpy(animated, Corners(texture), sizeof(animated));
    g_origTextureSizeChanged(layout, edx, oldRect);
    float *corners = Corners(texture);
    for (int i = 0; i < 12; ++i) {
        const float base = corners[i];
        corners[i] = animated[i] + (base - data->position[i]);
        data->position[i] = base;
    }
}

void __fastcall FontStringSizeChanged_h(void *layout, void *edx, const float *oldRect) {
    void *fontString = static_cast<char *>(layout) - Offsets::OFF_REGION_ANCHOR;
    AnimData *data = PositionData(fontString);
    if (data == nullptr) {
        g_origFontStringSizeChanged(layout, edx, oldRect);
        return;
    }
    float animated[3];
    void *before = StringBlock(fontString);
    const bool hadString = before != nullptr && GetStringPos(before, animated);
    g_origFontStringSizeChanged(layout, edx, oldRect);
    float base[3];
    StringBase(fontString, base);
    void *block = StringBlock(fontString);
    if (hadString && block != nullptr) {
        float position[3];
        for (int i = 0; i < 3; ++i)
            position[i] = animated[i] + (base[i] - data->position[i]);
        SetStringPos(block, position);
    }
    std::memcpy(data->position, base, sizeof(base));
}

const Game::HookAutoRegister _textureSizeHook{Offsets::FUN_TEXTURE_ON_FRAME_SIZE_CHANGED,
                                              reinterpret_cast<void *>(&TextureSizeChanged_h),
                                              reinterpret_cast<void **>(&g_origTextureSizeChanged)};
const Game::HookAutoRegister _fontStringSizeHook{Offsets::FUN_FONTSTRING_ON_FRAME_SIZE_CHANGED,
                                                 reinterpret_cast<void *>(&FontStringSizeChanged_h),
                                                 reinterpret_cast<void **>(&g_origFontStringSizeChanged)};

// ---- Lifetime ------------------------------------------------------------------------

void DestroyGroupsOf(void *region) {
    const auto it = g_states.find(region);
    if (it != g_states.end()) {
        if (!it->second.groups.empty() && !IsFrame(region))
            --g_regionsWithGroups;
        const std::vector<Group *> groups = std::move(it->second.groups);
        g_states.erase(it);
        for (Group *group : groups)
            delete group;
    }
    g_data.erase(region);
}

// The frame and its regions die here; its child frames run their own
// destructors.
void OnFrameDestroyed(void *frame) {
    if (g_states.empty() && g_data.empty())
        return;
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_REGION_LIST, [](void *region) { DestroyGroupsOf(region); });
    DestroyGroupsOf(frame);
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

// Groups on frames that outlived a /reload: their tables and handlers were
// in the old Lua state, so they are dropped without touching it.
void DropLeftovers() {
    Object::s_detached = true;
    for (auto &entry : g_states)
        for (Group *group : entry.second.groups)
            delete group;
    Object::s_detached = false;
    g_states.clear();
    g_data.clear();
    g_regionsWithGroups = 0;
}

// ---- Region methods (CScriptRegion_Scripts.cpp) -------------------------------------------

void StopAnimating(void *region) {
    if (State *state = Find(region)) {
        const std::vector<Group *> playing = state->playing;
        for (Group *group : playing)
            group->Stop(true);
    }
    // CSimpleFrame::StopAnimating: and everything below it.
    if (IsFrame(region))
        ForEachChild(region, [](void *child) { StopAnimating(child); });
}

int __fastcall Script_CreateAnimationGroup(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    if (region == nullptr)
        return 0;
    const char *name = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    const void *templateNode = nullptr;
    if (Game::Lua::Type(L, 3) == Game::Lua::TYPE_STRING) {
        const char *inherits = Game::Lua::ToString(L, 3);
        templateNode = Xml::Template(inherits);
        if (templateNode == nullptr) {
            Game::Lua::Error(L, "%s:CreateAnimationGroup(): Couldn't find inherited node \"%s\"",
                             Game::Lua::ObjectDisplayName(region), inherits);
            return 0;
        }
    }
    Group *group = new Group(region, name);
    if (templateNode != nullptr) {
        Xml::LoadGroup(group, templateNode, nullptr);
        group->PostLoadXML();
    }
    Push(L, group);
    return 1;
}

int __fastcall Script_GetAnimationGroups(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    if (region == nullptr)
        return 0;
    const State *state = Find(region);
    if (state == nullptr)
        return 0;
    const int count = static_cast<int>(state->groups.size());
    if (!Game::Lua::CheckStack(L, count)) {
        Game::Lua::Error(L, "%s:GetAnimationGroups(): Stack overflow", Game::Lua::ObjectDisplayName(region));
        return 0;
    }
    for (Group *group : state->groups)
        Push(L, group);
    return count;
}

int __fastcall Script_StopAnimating(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    if (region != nullptr)
        StopAnimating(region);
    return 0;
}

const Game::Lua::FrameMethodEntry kRegionMethods[] = {
    {"CreateAnimationGroup", &Script_CreateAnimationGroup},
    {"GetAnimationGroups", &Script_GetAnimationGroups},
    {"StopAnimating", &Script_StopAnimating},
};

const Game::Doc::Field kCreateArgs[] = {
    Game::Doc::Opt("name", "string", nullptr, "The group's name; \"$parent\" is the region's."),
    Game::Doc::Opt("templateName", "string", nullptr, "A virtual <AnimationGroup> to inherit."),
};
const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Req("animationGroup", "AnimationGroup"),
};
const Game::Doc::Field kGroupsRets[] = {
    Game::Doc::Vararg("AnimationGroup", "Every animation group of the region, in creation order."),
};
const Game::Doc::Function kCreateAnimationGroup{"Creates an animation group on the region.", kCreateArgs,
                                                kCreateRets};
const Game::Doc::Function kGetAnimationGroups{"Returns the region's animation groups.", {}, kGroupsRets};
const Game::Doc::Function kStopAnimating{
    "Stops every playing animation group of the region and, for a frame, of everything inside it.", {}, {}};
const Game::Doc::Method kRegionDocs[] = {
    {"CreateAnimationGroup", &kCreateAnimationGroup},
    {"GetAnimationGroups", &kGetAnimationGroups},
    {"StopAnimating", &kStopAnimating},
};

void RegisterLuaFunctions() {
    DropLeftovers();
    RegisterClass(Group::s_class);
    RegisterClass(Anim::s_class);
    RegisterClass(Anim::s_alphaClass);
    RegisterClass(Anim::s_translationClass);
    RegisterClass(Anim::s_scaleClass);
    RegisterClass(Anim::s_rotationClass);
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), kRegionMethods,
                                    static_cast<int>(sizeof(kRegionMethods) / sizeof(kRegionMethods[0])),
                                    kRegionDocs, static_cast<int>(sizeof(kRegionDocs) / sizeof(kRegionDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

// ---- The CScriptRegion animation members ---------------------------------------------

void RegisterGroup(void *region, Group *group) {
    State &state = g_states[region];
    if (state.groups.empty() && !IsFrame(region))
        ++g_regionsWithGroups;
    state.groups.push_back(group);
}

void NotifyBegin(void *region, Group *group) {
    if (group == nullptr)
        return;
    State &state = g_states[region];
    const AnimData *data = DataOf(region);
    const bool alpha = !(data != nullptr && data->hasAlpha) && group->HasAlphaAnim();
    const bool position = !(data != nullptr && data->hasPosition) && group->HasPositionAnim();
    AnimActivated(region, alpha, position);
    state.playing.push_back(group);
}

void NotifyEnd(void *region, Group *group) {
    State *state = Find(region);
    if (group == nullptr || state == nullptr)
        return;
    state->playing.erase(std::remove(state->playing.begin(), state->playing.end(), group), state->playing.end());
    const AnimData *data = DataOf(region);
    const bool alpha = data != nullptr && data->hasAlpha &&
                       !HasActiveAnim(region, [](const Group *g) { return g->HasAlphaAnim(); });
    const bool position = data != nullptr && data->hasPosition &&
                          !HasActiveAnim(region, [](const Group *g) { return g->HasPositionAnim(); });
    AnimDeactivated(region, alpha, position);
}

void PreOnAnimUpdate(void *region) {
    const bool frame = IsFrame(region);
    const AnimData *data = DataOf(region);
    if (data != nullptr && data->hasAlpha) {
        if (frame) {
            if (data->alpha != OwnAlpha(region))
                SetOwnAlpha(region, data->alpha);
        } else {
            SetVertexAlpha(region, data->alpha);
        }
    }
    if (data != nullptr && data->hasPosition) {
        if (IsTexture(region)) {
            std::memcpy(Corners(region), data->position, sizeof(data->position));
        } else if (IsFontString(region)) {
            if (void *block = StringBlock(region))
                SetStringPos(block, data->position);
            // CSimpleFontString::PreOnAnimUpdate: back to the base rotation
            // and scale.
            AnimData *own = DataOf(region);
            own->rotation = 0.0f;
            own->scale = 1.0f;
            g_touchedStrings.push_back(region);
        }
    }
    if (frame)
        ForEachChild(region, [](void *child) { PreOnAnimUpdate(child); });
}

void AddAnimTranslation(void *region, void *target, float x, float y) {
    if (IsFrame(region)) {
        // CSimpleFrame::AddAnimTranslation: the frame itself does not move;
        // everything below it does, or only the target below it.
        if (target == region)
            target = nullptr;
        ForEachChild(region, [target, x, y](void *child) { AddAnimTranslation(child, target, x, y); });
        return;
    }
    if (target != nullptr && target != region)
        return;
    // Only a region whose base is stored: PreOnAnimUpdate puts it back before
    // the next add, so nothing builds up.
    const AnimData *data = DataOf(region);
    if (data == nullptr || !data->hasPosition)
        return;
    if (IsTexture(region)) {
        // ApplyTranslationAnim(m_positions, translation).
        float *corners = Corners(region);
        for (int i = 0; i < 4; ++i) {
            corners[i * 3 + 0] += x;
            corners[i * 3 + 1] += y;
        }
    } else if (void *block = StringBlock(region)) {
        float position[3];
        if (!GetStringPos(block, position))
            return;
        position[0] += x;
        position[1] += y;
        SetStringPos(block, position);
    }
}

void AddAnimAlpha(void *region, void *target, int16_t alpha) {
    if (IsFrame(region)) {
        // CSimpleFrame::AddAnimAlpha: a target below the frame takes it.
        if (target != nullptr && target != region) {
            ForEachChild(region, [target, alpha](void *child) { AddAnimAlpha(child, target, alpha); });
            return;
        }
        const uint8_t own = OwnAlpha(region);
        const int value = alpha + static_cast<uint8_t>(own * DrawnParentAlpha(region) / 255);
        const uint8_t newAlpha = static_cast<uint8_t>(value > 255 ? 255 : (value < 0 ? 0 : value));
        if (newAlpha != own)
            SetOwnAlpha(region, newAlpha);
        return;
    }
    // CSimpleRegion::AddAnimAlpha.
    if (target != nullptr && target != region)
        return;
    const int value = alpha + static_cast<int>(VertexColor(region) >> 24);
    SetVertexAlpha(region, static_cast<uint8_t>(value > 255 ? 255 : (value < 0 ? 0 : value)));
}

namespace {

// The corners (TL, BL, TR, BR) as 2D points.
float CornerX(const float *corners, int i) { return corners[i * 3 + 0]; }
float CornerY(const float *corners, int i) { return corners[i * 3 + 1]; }

// GetOriginOffset (4.3.4 CSimpleRegion.cpp): the origin point on the drawn
// quad, then the offset along the quad's bottom edge direction, so the
// offset turns with a quad that is already turned.
void GetOriginOffset(int origin, const float *c, float offsetX, float offsetY, float &x, float &y) {
    const auto mid = [c](int a, int b, float &mx, float &my) {
        mx = (CornerX(c, b) - CornerX(c, a)) * 0.5f + CornerX(c, a);
        my = (CornerY(c, b) - CornerY(c, a)) * 0.5f + CornerY(c, a);
    };
    switch (origin) {
    case Offsets::FRAMEPOINT_TOPLEFT: x = CornerX(c, 0); y = CornerY(c, 0); break;
    case Offsets::FRAMEPOINT_TOP: mid(0, 2, x, y); break;
    case Offsets::FRAMEPOINT_TOPRIGHT: x = CornerX(c, 2); y = CornerY(c, 2); break;
    case Offsets::FRAMEPOINT_LEFT: mid(0, 1, x, y); break;
    case Offsets::FRAMEPOINT_CENTER: mid(0, 3, x, y); break;
    case Offsets::FRAMEPOINT_RIGHT: mid(2, 3, x, y); break;
    case Offsets::FRAMEPOINT_BOTTOMLEFT: x = CornerX(c, 1); y = CornerY(c, 1); break;
    case Offsets::FRAMEPOINT_BOTTOM: mid(1, 3, x, y); break;
    case Offsets::FRAMEPOINT_BOTTOMRIGHT: x = CornerX(c, 3); y = CornerY(c, 3); break;
    default: x = 0.0f; y = 0.0f; break;
    }
    if (offsetX * offsetX + offsetY * offsetY > 0.00000011920929f) {
        float dx = CornerX(c, 3) - CornerX(c, 1);
        float dy = CornerY(c, 3) - CornerY(c, 1);
        const float length = dy * dy + dx * dx;
        if (length > 0.00000023841858f) {
            const float scale = 1.0f / std::sqrt(length);
            dx *= scale;
            dy *= scale;
        }
        x = offsetX * dx - offsetY * dy + x;
        y = offsetY * dx + offsetX * dy + y;
    }
}

// The font string that takes a position animation, or nullptr.
AnimData *AnimatedString(void *region, void *target) {
    if (target != nullptr && target != region)
        return nullptr;
    AnimData *data = DataOf(region);
    if (data == nullptr || !data->hasPosition || !IsFontString(region))
        return nullptr;
    return data;
}

// The texture that takes a position animation (the target filter of the
// region overrides), or nullptr.
float *AnimatedCorners(void *region, void *target) {
    if (target != nullptr && target != region)
        return nullptr;
    const AnimData *data = DataOf(region);
    if (data == nullptr || !data->hasPosition || !IsTexture(region))
        return nullptr;
    return Corners(region);
}

} // namespace

void AddAnimScale(void *region, void *target, int origin, float offsetX, float offsetY, float x, float y) {
    if (IsFrame(region)) {
        // CSimpleFrame::AddAnimScale.
        if (target == region)
            target = nullptr;
        ForEachChild(region, [=](void *child) { AddAnimScale(child, target, origin, offsetX, offsetY, x, y); });
        return;
    }
    // CSimpleFontString::AddAnimScale: the text grows from its origin by the
    // x scale; the last scale animation of the update wins.
    if (AnimData *data = AnimatedString(region, target)) {
        data->scale = 1.0f - x;
        return;
    }
    // CSimpleTexture::AddAnimScale -> ApplyScaleAnim.
    float *c = AnimatedCorners(region, target);
    if (c == nullptr)
        return;
    float cx, cy;
    GetOriginOffset(origin, c, offsetX, offsetY, cx, cy);
    for (int i = 0; i < 4; ++i) {
        c[i * 3 + 0] = (cx - c[i * 3 + 0]) * x + c[i * 3 + 0];
        c[i * 3 + 1] = (cy - c[i * 3 + 1]) * y + c[i * 3 + 1];
    }
}

void AddAnimRotation(void *region, void *target, int origin, float offsetX, float offsetY, float radians) {
    if (IsFrame(region)) {
        // CSimpleFrame::AddAnimRotation.
        if (target == region)
            target = nullptr;
        ForEachChild(region, [=](void *child) { AddAnimRotation(child, target, origin, offsetX, offsetY, radians); });
        return;
    }
    // CSimpleFontString::AddAnimRotation: the text turns about its origin;
    // the last rotation animation of the update wins.
    if (AnimData *data = AnimatedString(region, target)) {
        data->rotation = radians;
        return;
    }
    // CSimpleTexture::AddAnimRotation -> ApplyRotationAnim.
    float *c = AnimatedCorners(region, target);
    if (c == nullptr)
        return;
    float cx, cy;
    GetOriginOffset(origin, c, offsetX, offsetY, cx, cy);
    const float cs = std::cos(radians);
    const float sn = std::sin(radians);
    for (int i = 0; i < 4; ++i) {
        const float x = c[i * 3 + 0] - cx;
        const float y = c[i * 3 + 1] - cy;
        c[i * 3 + 0] = x * cs - y * sn + cx;
        c[i * 3 + 1] = x * sn + y * cs + cy;
    }
}

void FontStringBaked(void *node, void *fontString) {
    if (g_data.empty())
        return;
    AnimData *data = DataOf(fontString);
    if (data == nullptr)
        return;
    if (data->rotation != 0.0f || data->scale != 1.0f)
        Texture::Transform::ScaleRotateFontStringNode(node, data->scale, data->rotation);
    data->bakedRotation = data->rotation;
    data->bakedScale = data->scale;
}

void *FindRegionByName(const char *name) {
    void *L = Game::Lua::State();
    if (L == nullptr || name == nullptr)
        return nullptr;
    Game::Lua::PushString(L, name);
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    void *object = Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE ? Game::Lua::ResolveObject(L, -1) : nullptr;
    Game::Lua::SetTop(L, -2);
    if (object == nullptr || IsOurs(object) || !Game::Lua::IsA(object, Offsets::VAR_REGION_LUA_TYPE_ID))
        return nullptr;
    return object;
}

} // namespace Animation::Region
