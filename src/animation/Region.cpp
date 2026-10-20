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

#include <algorithm>
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
};

std::unordered_map<void *, State> g_states;
std::unordered_map<void *, AnimData> g_data;
// Regions (not frames) with groups: their frame's update has to look at its
// regions.
int g_regionsWithGroups = 0;

using FrameUpdate_t = void(__fastcall *)(void *frame, void *edx, float elapsed);
using SetFrameAlpha_t = void(__fastcall *)(void *frame, void *edx, uint32_t alpha);
using GetColor_t = void(__fastcall *)(void *region, void *edx, uint32_t *bgra);
using SetColor_t = void(__fastcall *)(void *region, void *edx, const uint32_t *bgra);

FrameUpdate_t g_origFrameUpdate = nullptr;

bool IsFrame(void *region) { return Game::Lua::IsA(region, Offsets::VAR_FRAME_LUA_TYPE_ID); }
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

// CSimpleFrame / CSimpleRegion::AnimActivated.
void AnimActivated(void *region, bool alpha, bool position) {
    const bool frame = IsFrame(region);
    AnimData &data = g_data[region]; // AnimData_Init
    if (alpha) {
        data.hasAlpha = true;
        data.alpha = frame ? OwnAlpha(region) : static_cast<uint8_t>(VertexColor(region) >> 24);
    }
    (void)position; // Translation, Scale and Rotation are not applied yet.
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
    if (position && data != nullptr)
        data->hasPosition = false;
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
}

const Game::HookAutoRegister _frameUpdateHook{Offsets::FUN_FRAME_ON_UPDATE, reinterpret_cast<void *>(&FrameUpdate_h),
                                              reinterpret_cast<void **>(&g_origFrameUpdate)};

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
    if (frame)
        ForEachChild(region, [](void *child) { PreOnAnimUpdate(child); });
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
