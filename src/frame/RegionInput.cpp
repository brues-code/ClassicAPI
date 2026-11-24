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

// Mouse input on textures and font strings, as 3.80.2 has it
// (docs/notes/region-mouse-3802.md): `region:EnableMouse`,
// `EnableMouseMotion`, `SetMouseMotionEnabled`, `SetMouseClickEnabled`,
// `EnableMouseWheel`, their getters, `SetPropagateMouseMotion`,
// `SetPropagateMouseClicks`, `SetPassThroughButtons` and `IsMouseMotionFocus`.
//
// 3.80.2 puts a region in the same input lists as the frames and sorts them
// all by one draw-order key, so a region is a hit candidate in its own right:
// the motion focus, the press target and the wheel target can each be a
// region, which then runs its own OnEnter / OnLeave (self, motion),
// OnMouseDown (self, button), OnMouseUp (self, button, upInside) and
// OnMouseWheel (self, delta). Its hit test is its own rect, every edge
// inside, through the clipping frames above it (FUN_140594EE0).
//
// 1.12's lists hold frames only, so the regions are kept here, and the hover,
// press and wheel passes (Frame::MotionFocus, Frame::MouseInput,
// Frame::MouseWheel) merge them with the frames the engine's walks find. The
// merge order is 3.80.2's key (FUN_14058D6B0): strata, then level, then the
// frame under every region of its level, then the region's draw layer. 3.80.2
// orders the regions inside a layer by sublevel; 1.12 has none, and draws a
// level's layer as one batch: every texture of the level, sorted by its
// device texture, then every font string, in frame order and then in the
// order each frame registered them (benilla-re ui/draworder). The key follows
// that batch: a font string over a texture, two textures by device texture,
// and ties by the level's frame order and the frame's own region order.
// (Frame::ClipChildren draws a clipped region right after the level's
// unclipped ones of its layer; the key does not see that.)
//
// A region takes input only while it is visible (Frame::RegionVisibility),
// as 3.80.2 registers a region's input only then. Hiding it, turning its
// motion off or destroying it takes it out of the motion focus at once
// (OnLeave with motion false, none while destroying), and out of a press in
// progress, which then gets no OnMouseUp.
//
// The setters' rules are the frame ones' (Frame::MouseInput): a missing
// enable argument reads as false, the propagate setters require theirs,
// SetPassThroughButtons takes button names only. SetScript turns nothing on;
// a `<Scripts>` handler does, unless it says `autoEnableInput="false"`:
// OnEnter, OnLeave, OnMouseDown and OnMouseUp turn on motion and clicks,
// OnMouseWheel the wheel (3.80.2's FUN_140593D20). IsMouseEnabled answers a
// boolean, 3.80.2's form; 1.12's frames keep their 1 / nil.

#include "frame/RegionInput.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/ClickEvents.h"
#include "frame/ClipChildren.h"
#include "frame/Layout.h"
#include "frame/MotionFocus.h"
#include "frame/MouseInput.h"
#include "frame/RegionDestruction.h"
#include "frame/RegionScripts.h"
#include "frame/RegionVisibility.h"
#include "storm/List.h"
#include "xml/FrameLoad.h"

#include <algorithm>
#include <unordered_map>

namespace Frame::RegionInput {

namespace {

using GxTexture_t = void *(__fastcall *)(const void *texture);

struct State {
    bool motion = false;
    bool click = false;
    bool wheel = false;
    bool propagatesMotion = false;
    bool propagatesClicks = false;
    uint32_t passThrough = 0;
};

// Regions with any input setting; one with none is erased.
std::unordered_map<const void *, State> g_state;

const State *Find(const void *region) {
    const auto it = g_state.find(region);
    return it != g_state.end() ? &it->second : nullptr;
}

bool Blank(const State &s) {
    return !s.motion && !s.click && !s.wheel && !s.propagatesMotion && !s.propagatesClicks && s.passThrough == 0;
}

uint8_t *Context() { return Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR); }

void Repick() {
    if (uint8_t *ctx = Context())
        Game::Ref<int>(ctx, Offsets::OFF_UI_CONTEXT_REPICK) = 1;
}

// Applies `next` to the region: a region that loses motion leaves the motion
// focus, one that loses clicks leaves the press in progress; one that gains
// motion is looked for at the next pass.
void Set(void *region, const State &next) {
    const State *cur = Find(region);
    const bool hadMotion = cur != nullptr && cur->motion;
    const bool hadClick = cur != nullptr && cur->click;
    if (Blank(next))
        g_state.erase(region);
    else
        g_state[region] = next;
    if (hadClick && !next.click)
        Frame::MouseInput::DropRegion(region);
    if (hadMotion && !next.motion)
        Frame::MotionFocus::Drop(region);
    else if (!hadMotion && next.motion)
        Repick();
}

State Get(const void *region) {
    const State *s = Find(region);
    return s != nullptr ? *s : State{};
}

bool Hit(const void *region, const float *point) {
    if (!Frame::RegionVisibility::Visible(region))
        return false;
    Frame::Layout::Rect rect;
    if (!Frame::Layout::GetRect(region, rect))
        return false;
    if (point[0] < rect.left || rect.right < point[0] || point[1] < rect.bottom || rect.top < point[1])
        return false;
    return Frame::ClipChildren::Inside(Game::Read<void *>(region, Offsets::OFF_REGION_PARENT), point);
}

// ---- Draw order -------------------------------------------------------------

void *Owner(const void *object) {
    return Frame::RegionScripts::IsRegion(object) ? Game::Read<void *>(object, Offsets::OFF_REGION_PARENT)
                                                  : const_cast<void *>(object);
}

// 0 for a frame, the draw layer + 1 for a region (3.80.2's layer slot).
int LayerSlot(const void *object) {
    return Frame::RegionScripts::IsRegion(object) ? Game::Read<int>(object, Offsets::OFF_REGION_DRAW_LAYER) + 1 : 0;
}

bool IsText(const void *region) {
    return Game::Lua::IsA(const_cast<void *>(region), Offsets::VAR_FONTSTRING_LUA_TYPE_ID);
}

// The frame's place in its level's draw walk, -1 when it is not in it.
int FrameOrder(const void *frame) {
    const uint8_t *ctx = Context();
    if (ctx == nullptr)
        return -1;
    const int strata = Game::Read<int>(frame, Offsets::OFF_FRAME_STRATA);
    const int level = Game::Read<int>(frame, Offsets::OFF_FRAME_LEVEL);
    if (strata < 0 || strata >= Offsets::FRAME_STRATA_COUNT || level < 0)
        return -1;
    const uint8_t *record = Game::Read<const uint8_t *>(ctx, Offsets::OFF_UI_CONTEXT_STRATA_RECORDS + strata * 4);
    if (record == nullptr || static_cast<uint32_t>(level) >= Game::Read<uint32_t>(record, Offsets::OFF_STRATA_RECORD_LEVEL_COUNT))
        return -1;
    const uint8_t *node = Game::Read<const uint8_t *const *>(record, Offsets::OFF_STRATA_RECORD_LEVELS)[level];
    if (node == nullptr)
        return -1;
    const uint32_t link = Game::Read<uint32_t>(node, Offsets::OFF_LEVEL_NODE_LINK_OFFSET);
    int index = 0;
    for (const uint8_t *f = Game::Read<const uint8_t *>(node, Offsets::OFF_LEVEL_NODE_FIRST_FRAME); Storm::IsNode(f);
         f = Game::Read<const uint8_t *>(f, link + 4), ++index)
        if (f == frame)
            return index;
    return -1;
}

// The region's place in its owner's list for its layer, in draw order.
int RegionOrder(const void *region) {
    const void *owner = Game::Read<void *>(region, Offsets::OFF_REGION_PARENT);
    const int layer = Game::Read<int>(region, Offsets::OFF_REGION_DRAW_LAYER);
    if (owner == nullptr || layer < 0 || layer >= Offsets::FRAME_LAYER_COUNT)
        return -1;
    const uintptr_t list = Offsets::OFF_FRAME_LAYER_LISTS + layer * Offsets::FRAME_LAYER_LIST_STRIDE;
    int index = 0;
    for (const uint8_t *n = Game::Read<const uint8_t *>(owner, list + Offsets::OFF_LAYER_LIST_FIRST); Storm::IsNode(n);
         n = Game::Read<const uint8_t *>(n, Offsets::OFF_LAYER_NODE_NEXT), ++index)
        if (Game::Read<const void *>(n, Offsets::OFF_LAYER_NODE_REGION) == region)
            return index;
    return -1;
}

// ---- Lifetime -------------------------------------------------------------------

void OnVisibility(void *region, bool visible) {
    const State *s = Find(region);
    if (s == nullptr)
        return;
    if (visible) {
        if (s->motion)
            Repick();
        return;
    }
    Frame::MouseInput::DropRegion(region);
    if (s->motion)
        Frame::MotionFocus::Drop(region);
}

const Frame::RegionVisibility::AutoSubscribe _visibilitySub{&OnVisibility};

// Not gated on g_state: a reload clears it before the engine destroys the
// old regions, and a region still in the motion focus or the press must
// leave them all the same.
void OnRegionDestroyed(void *region) {
    g_state.erase(region);
    Frame::MotionFocus::Forget(region);
    Frame::MouseInput::DropRegion(region);
}

const Frame::RegionDestruction::AutoSubscribe _destructionSub{&OnRegionDestroyed};

void PrepareForReload() { g_state.clear(); }

const Game::ReloadAutoRegister _reload{&PrepareForReload};
const Game::GlueBootAutoRegister _glueBoot{&PrepareForReload};

// ---- Lua ------------------------------------------------------------------------

void *Self(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    return region != nullptr && Frame::RegionScripts::IsRegion(region) ? region : nullptr;
}

template <bool State::*Field> int __fastcall Script_SetFlag(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    State next = Get(region);
    next.*Field = Game::Lua::ToFlag(L, 2);
    Set(region, next);
    return 0;
}

template <bool State::*Field> int __fastcall Script_GetFlag(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    Game::Lua::PushBool(L, Get(region).*Field);
    return 1;
}

int __fastcall Script_EnableMouse(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    State next = Get(region);
    next.motion = next.click = Game::Lua::ToFlag(L, 2);
    Set(region, next);
    return 0;
}

int __fastcall Script_IsMouseEnabled(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    const State s = Get(region);
    Game::Lua::PushBool(L, s.motion && s.click);
    return 1;
}

template <bool State::*Field> int __fastcall Script_SetPropagate(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, Field == &State::propagatesMotion ? "Usage: self:SetPropagateMouseMotion(propagate)"
                                                              : "Usage: self:SetPropagateMouseClicks(propagate)");
        return 0;
    }
    State next = Get(region);
    next.*Field = Game::Lua::ToFlag(L, 2);
    Set(region, next);
    return 0;
}

int __fastcall Script_SetPassThroughButtons(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    uint32_t mask = 0;
    const int top = Game::Lua::GetTop(L);
    for (int i = 2; i <= top; ++i) {
        if (Game::Lua::Type(L, i) != Game::Lua::TYPE_STRING) {
            Game::Lua::Error(L, "Usage: self:SetPassThroughButtons([button1, ...])");
            return 0;
        }
        mask |= Frame::ClickEvents::MaskForButtonName(Game::Lua::ToString(L, i));
    }
    State next = Get(region);
    next.passThrough = mask;
    Set(region, next);
    return 0;
}

int __fastcall Script_IsMouseMotionFocus(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    Game::Lua::PushBool(L, Frame::MotionFocus::IsFocus(region));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"EnableMouse", &Script_EnableMouse},
    {"IsMouseEnabled", &Script_IsMouseEnabled},
    {"EnableMouseMotion", &Script_SetFlag<&State::motion>},
    {"SetMouseMotionEnabled", &Script_SetFlag<&State::motion>},
    {"IsMouseMotionEnabled", &Script_GetFlag<&State::motion>},
    {"SetMouseClickEnabled", &Script_SetFlag<&State::click>},
    {"IsMouseClickEnabled", &Script_GetFlag<&State::click>},
    {"EnableMouseWheel", &Script_SetFlag<&State::wheel>},
    {"IsMouseWheelEnabled", &Script_GetFlag<&State::wheel>},
    {"SetPropagateMouseMotion", &Script_SetPropagate<&State::propagatesMotion>},
    {"CanPropagateMouseMotion", &Script_GetFlag<&State::propagatesMotion>},
    {"SetPropagateMouseClicks", &Script_SetPropagate<&State::propagatesClicks>},
    {"CanPropagateMouseClicks", &Script_GetFlag<&State::propagatesClicks>},
    {"SetPassThroughButtons", &Script_SetPassThroughButtons},
    {"IsMouseMotionFocus", &Script_IsMouseMotionFocus},
};

const Game::Doc::Field kEnableArgs[] = {
    Game::Doc::Opt("enable", "bool", "false"),
};
const Game::Doc::Field kEnabledRets[] = {
    Game::Doc::Req("enabled", "bool"),
};
const Game::Doc::Field kPropagateArgs[] = {
    Game::Doc::Req("propagate", "bool"),
};
const Game::Doc::Field kPropagateRets[] = {
    Game::Doc::Req("propagate", "bool"),
};
const Game::Doc::Field kPassThroughArgs[] = {
    Game::Doc::Vararg("string",
                      "Any number of button names: LeftButton (Button1), RightButton (Button2), MiddleButton "
                      "(Button3), Button4 to Button31."),
};
const Game::Doc::Field kFocusRets[] = {
    Game::Doc::Req("isMouseMotionFocus", "bool"),
};

const Game::Doc::Function kEnableMouse{"Sets whether the region takes mouse clicks and motion.", kEnableArgs, {}};
const Game::Doc::Function kIsMouseEnabled{"Returns whether the region takes both mouse clicks and motion.", {},
                                          kEnabledRets};
const Game::Doc::Function kSetMotion{"Sets whether the region takes mouse motion: OnEnter and OnLeave.", kEnableArgs,
                                     {}};
const Game::Doc::Function kIsMotion{"Returns whether the region takes mouse motion.", {}, kEnabledRets};
const Game::Doc::Function kSetClick{"Sets whether the region takes mouse clicks: OnMouseDown and OnMouseUp.",
                                    kEnableArgs, {}};
const Game::Doc::Function kIsClick{"Returns whether the region takes mouse clicks.", {}, kEnabledRets};
const Game::Doc::Function kSetWheel{"Sets whether the region takes the mouse wheel: OnMouseWheel.", kEnableArgs, {}};
const Game::Doc::Function kIsWheel{"Returns whether the region takes the mouse wheel.", {}, kEnabledRets};
const Game::Doc::Function kSetPropagateMotion{
    "Sets whether the region passes mouse motion on to the next frame or region under the cursor.", kPropagateArgs,
    {}};
const Game::Doc::Function kCanPropagateMotion{"Returns whether the region passes mouse motion on.", {},
                                              kPropagateRets};
const Game::Doc::Function kSetPropagateClicks{
    "Sets whether a click on the region also goes to the next frame or region under the cursor.", kPropagateArgs, {}};
const Game::Doc::Function kCanPropagateClicks{"Returns whether the region passes mouse clicks on.", {},
                                              kPropagateRets};
const Game::Doc::Function kSetPassThrough{
    "Sets the mouse buttons the region lets through to the frame or region under it. No names clears the list.",
    kPassThroughArgs, {}};
const Game::Doc::Function kIsFocus{"Returns whether the region gets mouse motion at the cursor.", {}, kFocusRets};

const Game::Doc::Method g_methodDocs[] = {
    {"EnableMouse", &kEnableMouse},
    {"IsMouseEnabled", &kIsMouseEnabled},
    {"EnableMouseMotion", &kSetMotion},
    {"SetMouseMotionEnabled", &kSetMotion},
    {"IsMouseMotionEnabled", &kIsMotion},
    {"SetMouseClickEnabled", &kSetClick},
    {"IsMouseClickEnabled", &kIsClick},
    {"EnableMouseWheel", &kSetWheel},
    {"IsMouseWheelEnabled", &kIsWheel},
    {"SetPropagateMouseMotion", &kSetPropagateMotion},
    {"CanPropagateMouseMotion", &kCanPropagateMotion},
    {"SetPropagateMouseClicks", &kSetPropagateClicks},
    {"CanPropagateMouseClicks", &kCanPropagateClicks},
    {"SetPassThroughButtons", &kSetPassThrough},
    {"IsMouseMotionFocus", &kIsFocus},
};

void RegisterLuaFunctions() {
    for (uintptr_t registry : {uintptr_t{Offsets::VAR_TEXTURE_METHOD_REGISTRY},
                               uintptr_t{Offsets::VAR_FONTSTRING_METHOD_REGISTRY}})
        Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(registry), g_methods, g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void AutoEnable(void *region, const void *scripts) {
    State next = Get(region);
    bool changed = false;
    for (const void *e = Xml::FirstChild(scripts); e != nullptr; e = Xml::NextSibling(e)) {
        const char *flag = Xml::Attr(e, "autoEnableInput");
        if (flag != nullptr && !Game::StringToFlag(flag))
            continue;
        if (Xml::TagIs(e, "OnEnter") || Xml::TagIs(e, "OnLeave") || Xml::TagIs(e, "OnMouseDown") ||
            Xml::TagIs(e, "OnMouseUp")) {
            changed |= !next.motion || !next.click;
            next.motion = next.click = true;
        } else if (Xml::TagIs(e, "OnMouseWheel")) {
            changed |= !next.wheel;
            next.wheel = true;
        }
    }
    if (changed)
        Set(region, next);
}

bool Any() { return !g_state.empty(); }

bool Under(const void *region, const float *point) { return Hit(region, point); }

bool MotionEnabled(const void *region) {
    const State *s = Find(region);
    return s != nullptr && s->motion;
}

bool PropagatesMotion(const void *region) {
    const State *s = Find(region);
    return s != nullptr && s->propagatesMotion;
}

bool PropagatesClicks(const void *region) {
    const State *s = Find(region);
    return s != nullptr && s->propagatesClicks;
}

void Candidates(Kind kind, const float *point, uint32_t button, std::vector<void *> &out) {
    out.clear();
    for (const auto &[region, s] : g_state) {
        const bool takes = kind == MOTION ? s.motion : kind == CLICK ? s.click && (s.passThrough & button) == 0 : s.wheel;
        if (takes && Hit(region, point))
            out.push_back(const_cast<void *>(region));
    }
    std::stable_sort(out.begin(), out.end(), [](const void *a, const void *b) { return Above(a, b); });
}

bool Above(const void *a, const void *b) {
    const void *ownerA = Owner(a);
    const void *ownerB = Owner(b);
    if (ownerA == nullptr || ownerB == nullptr)
        return false;
    const int strataA = Game::Read<int>(ownerA, Offsets::OFF_FRAME_STRATA);
    const int strataB = Game::Read<int>(ownerB, Offsets::OFF_FRAME_STRATA);
    if (strataA != strataB)
        return strataA > strataB;
    const int levelA = Game::Read<int>(ownerA, Offsets::OFF_FRAME_LEVEL);
    const int levelB = Game::Read<int>(ownerB, Offsets::OFF_FRAME_LEVEL);
    if (levelA != levelB)
        return levelA > levelB;
    const int slotA = LayerSlot(a);
    const int slotB = LayerSlot(b);
    if (slotA != slotB)
        return slotA > slotB;
    if (slotA == 0)
        return false; // two frames
    const bool textA = IsText(a);
    const bool textB = IsText(b);
    if (textA != textB)
        return textA;
    if (!textA) {
        const auto gx = reinterpret_cast<GxTexture_t>(Offsets::FUN_SIMPLETEXTURE_GX_TEXTURE);
        const uintptr_t gxA = reinterpret_cast<uintptr_t>(gx(a));
        const uintptr_t gxB = reinterpret_cast<uintptr_t>(gx(b));
        if (gxA != gxB)
            return gxA > gxB;
    }
    if (ownerA != ownerB)
        return FrameOrder(ownerA) > FrameOrder(ownerB);
    return RegionOrder(a) > RegionOrder(b);
}

void Cursor(const uint8_t *ctx, float *point) {
    const float *mouse = Game::Ptr<float>(const_cast<uint8_t *>(ctx), Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    Frame::Layout::NdcToDdc(&point[0], &point[1], mouse[0], mouse[1]);
}

} // namespace Frame::RegionInput
