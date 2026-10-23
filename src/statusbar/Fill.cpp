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

// `bar:SetReverseFill` / `GetReverseFill`, `SetRotatesTexture` /
// `GetRotatesTexture` and `SetFillStyle` / `GetFillStyle`.
//
// 1.12 draws the fill by cropping: the bar texture is installed with
// OFF_REGION_TEXCOORD_MODIFIES_RECT, and OnFrameUpdate sets its coordinates to
// the filled part, which shrinks its rectangle from the bottom-left corner.
// That crop can only grow a fill from the left or the bottom.
//
// 4.3.4's CSimpleStatusBar::OnLayerUpdate (CSimpleStatusBar.cpp:248) does not
// crop: it sets the coordinates and moves the texture's four points in from
// the bar's edges by the empty part, from the right / top or, reversed, from
// the left / bottom (`m_reverseFill`), and with `m_rotatesTexture` maps the
// image turned a quarter. A bar with any of these modes on takes that layout
// here: the co-hook on OnFrameUpdate runs the base frame update and lays the
// texture out as 4.3.4 does, with the crop flag cleared on the texture. A bar
// with them all off runs 1.12's own update untouched. Turning the last mode
// off pins the texture to the bar again and gives it back its crop flag, as
// InstallBarTexture and SetBarTexture leave it.
//
// The retail fill styles (Enum.StatusBarFillStyle) fit the same layout:
// Reverse is the reversed fill; Center insets both ends by half the empty
// part and shows the middle of the image; StandardNoRangeFill draws a bar
// with an empty range full unless min and max are both 0.
//
// 4.3.4 lays out again when m_changed is set, and its SetWidth sets it; a
// bar's height change re-lays nothing there. Here a size change in either
// axis does, since the offsets are lengths in the bar's units.

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/Destruction.h"

#include <cstdint>
#include <unordered_map>

namespace StatusBar::Fill {

namespace {

enum FillStyle : int {
    kStandard = 0,
    kStandardNoRangeFill = 1,
    kCenter = 2,
    kReverse = 3,
};

const Game::Lua::EnumIntegerEntry kFillStyleEntries[] = {
    {"Standard", kStandard},
    {"StandardNoRangeFill", kStandardNoRangeFill},
    {"Center", kCenter},
    {"Reverse", kReverse},
};

// The names retail's XML `fillStyle` attribute takes, for SetFillStyle("...").
const char *const kFillStyleTokens[] = {"STANDARD", "STANDARD_NO_RANGE_FILL", "CENTER", "REVERSE"};

struct State {
    bool reverseFill = false;
    bool rotatesTexture = false;
    int fillStyle = kStandard;
    // The texture's crop flag and points are ours until Restore.
    bool owned = false;
    bool laidOut = false;
    float width = 0.0f;
    float height = 0.0f;

    bool Active() const { return reverseFill || rotatesTexture || fillStyle != kStandard; }
};

// Bars with a mode on, or laid out by us and not yet restored. Dropped when the
// bar is destroyed.
std::unordered_map<const void *, State> g_state;

using Update_t = void(__fastcall *)(void *bar, void *edx, float elapsed);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relative, int relativePoint, float x,
                                      float y, int doResize);
using SetAllPoints_t = void(__thiscall *)(void *anchor, void *relative, int doResize);
using SetTexCoordRect_t = void(__thiscall *)(void *texture, const float *rect);
using SetTexCoordCorners_t = void(__thiscall *)(void *texture, const float *corners);

Update_t g_origUpdate = nullptr;

uint32_t &Flags(void *bar) { return Game::Ref<uint32_t>(bar, Offsets::OFF_VALUE_FLAGS); }

void *BarTexture(void *bar) { return Game::Read<void *>(bar, Offsets::OFF_STATUSBAR_BAR_TEXTURE); }

bool IsVertical(void *bar) { return Game::Read<int>(bar, Offsets::OFF_STATUSBAR_ORIENTATION) != 0; }

// The bar's size in its own units, as CSimpleSlider::OnFrameUpdate measures
// its track: the resolved rect's extent over the effective scale.
void BarSize(void *bar, float &width, float &height) {
    const float scale = Game::Read<float>(bar, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    const float *rect = Game::Ptr<float>(bar, Offsets::OFF_REGION_RECT); // bottom, left, top, right
    width = (rect[3] - rect[1]) / scale;
    height = (rect[2] - rect[0]) / scale;
}

// 4.3.4's GetAnimValue, with StandardNoRangeFill's full bar for an empty range.
float FillFraction(void *bar, int fillStyle) {
    const float min = Game::Read<float>(bar, Offsets::OFF_VALUE_MIN);
    const float max = Game::Read<float>(bar, Offsets::OFF_STATUSBAR_MAX);
    const float range = max - min;
    if (range > 0.0f)
        return (Game::Read<float>(bar, Offsets::OFF_VALUE_VALUE) - min) / range;
    if (fillStyle == kStandardNoRangeFill && (min != 0.0f || max != 0.0f))
        return 1.0f;
    return 0.0f;
}

void SetPoint(void *texture, int point, void *bar, float x, float y) {
    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(
        Game::Ptr<uint8_t>(texture, Offsets::OFF_REGION_ANCHOR), point,
        Game::Ptr<uint8_t>(bar, Offsets::OFF_REGION_ANCHOR), point, x, y, 1);
}

// The image's full coordinates, as 4.3.4's SetRotatesTexture sets them.
void SetFullTexCoord(void *texture, bool rotates) {
    if (rotates) {
        const float corners[8] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        reinterpret_cast<SetTexCoordCorners_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD_CORNERS)(texture,
                                                                                                corners);
    } else {
        const float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f}; // top, left, bottom, right
        reinterpret_cast<SetTexCoordRect_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD)(texture, rect);
    }
}

// 4.3.4's OnLayerUpdate body. [s0, s1] is the filled part of the bar along the
// fill axis, from its start edge; [t0, t1] the part of the image drawn there.
void LayOut(void *bar, State &state) {
    void *texture = BarTexture(bar);
    float width, height;
    BarSize(bar, width, height);
    const float v = FillFraction(bar, state.fillStyle);

    float s0 = 0.0f, s1 = v, t0 = 0.0f, t1 = v;
    if (state.fillStyle == kCenter) {
        s0 = t0 = (1.0f - v) * 0.5f;
        s1 = t1 = (1.0f + v) * 0.5f;
    } else if (state.reverseFill || state.fillStyle == kReverse) {
        s0 = 1.0f - v;
        s1 = 1.0f;
    }

    Game::Ref<int>(texture, Offsets::OFF_REGION_TEXCOORD_MODIFIES_RECT) = 0; // a new texture is installed with it
    state.owned = true;

    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    if (IsVertical(bar)) {
        if (state.rotatesTexture) {
            const float corners[8] = {t1, 0.0f, t0, 0.0f, t1, 1.0f, t0, 1.0f}; // UL, LL, UR, LR
            reinterpret_cast<SetTexCoordCorners_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD_CORNERS)(texture,
                                                                                                    corners);
        } else {
            const float rect[4] = {1.0f - t1, 0.0f, 1.0f - t0, 1.0f};
            reinterpret_cast<SetTexCoordRect_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD)(texture, rect);
        }
        bottom = s0 * height;
        top = -((1.0f - s1) * height);
    } else {
        if (state.rotatesTexture) {
            const float corners[8] = {0.0f, 1.0f - t0, 1.0f, 1.0f - t0, 0.0f, 1.0f - t1, 1.0f, 1.0f - t1};
            reinterpret_cast<SetTexCoordCorners_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD_CORNERS)(texture,
                                                                                                    corners);
        } else {
            const float rect[4] = {0.0f, t0, 1.0f, t1};
            reinterpret_cast<SetTexCoordRect_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD)(texture, rect);
        }
        left = s0 * width;
        right = -((1.0f - s1) * width);
    }

    SetPoint(texture, Offsets::FRAMEPOINT_TOPLEFT, bar, left, top);
    SetPoint(texture, Offsets::FRAMEPOINT_TOPRIGHT, bar, right, top);
    SetPoint(texture, Offsets::FRAMEPOINT_BOTTOMLEFT, bar, left, bottom);
    SetPoint(texture, Offsets::FRAMEPOINT_BOTTOMRIGHT, bar, right, bottom);
    state.laidOut = true;
    state.width = width;
    state.height = height;
}

// Hands the texture back to 1.12's crop: pinned to the bar, crop flag on, and
// the fill marked for 1.12's next update.
void Restore(void *bar) {
    if (void *texture = BarTexture(bar)) {
        reinterpret_cast<SetAllPoints_t>(Offsets::FUN_LAYOUT_SET_ALL_POINTS)(
            Game::Ptr<uint8_t>(texture, Offsets::OFF_REGION_ANCHOR),
            Game::Ptr<uint8_t>(bar, Offsets::OFF_REGION_ANCHOR), 1);
        Game::Ref<int>(texture, Offsets::OFF_REGION_TEXCOORD_MODIFIES_RECT) = 1;
    }
    Flags(bar) |= Offsets::VALUE_FLAG_CHANGED;
}

void __fastcall Update_h(void *bar, void *edx, float elapsed) {
    const auto it = g_state.find(bar);
    if (it == g_state.end()) {
        g_origUpdate(bar, edx, elapsed);
        return;
    }
    State &state = it->second;
    reinterpret_cast<Update_t>(Offsets::FUN_FRAME_ON_UPDATE)(bar, edx, elapsed);

    constexpr uint32_t kReady = Offsets::VALUE_FLAG_RANGE_SET | Offsets::VALUE_FLAG_VALUE_SET;
    const uint32_t flags = Flags(bar);
    if ((flags & kReady) != kReady || BarTexture(bar) == nullptr)
        return;
    bool changed = (flags & Offsets::VALUE_FLAG_CHANGED) != 0;
    if (!changed && state.laidOut) { // a resize moves the offsets' lengths
        float width, height;
        BarSize(bar, width, height);
        changed = width != state.width || height != state.height;
    }
    if (!changed)
        return;
    LayOut(bar, state);
    Flags(bar) &= ~Offsets::VALUE_FLAG_CHANGED;
}

const Game::HookAutoRegister _updateHook{Offsets::FUN_STATUSBAR_ON_FRAME_UPDATE,
                                         reinterpret_cast<void *>(&Update_h),
                                         reinterpret_cast<void **>(&g_origUpdate)};

void OnFrameDestroyed(void *frame) { g_state.erase(frame); }

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

// Applies a mode change. A bar with a mode on takes its texture off the crop
// at once - a turned image's coordinates would crop it to nothing - and is laid
// out on its next update; one with none goes back to 1.12's fill.
template <typename Change> void Modify(void *bar, Change change) {
    State &state = g_state[bar];
    change(state);
    Flags(bar) |= Offsets::VALUE_FLAG_CHANGED;
    if (state.Active()) {
        if (void *texture = BarTexture(bar)) {
            Game::Ref<int>(texture, Offsets::OFF_REGION_TEXCOORD_MODIFIES_RECT) = 0;
            state.owned = true;
        }
        return;
    }
    if (state.owned || state.laidOut)
        Restore(bar);
    g_state.erase(bar);
}

const State *Find(void *bar) {
    const auto it = g_state.find(bar);
    return it != g_state.end() ? &it->second : nullptr;
}

void *ResolveBar(void *L) { return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_STATUSBAR_LUA_TYPE_ID); }



int __fastcall Script_SetReverseFill(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const bool reverse = Game::Lua::ToFlag(L, 2);
    Modify(bar, [reverse](State &s) { s.reverseFill = reverse; });
    return 0;
}

int __fastcall Script_GetReverseFill(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const State *state = Find(bar);
    Game::Lua::PushBool(L, state != nullptr && state->reverseFill);
    return 1;
}

int __fastcall Script_SetRotatesTexture(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const bool rotates = Game::Lua::ToFlag(L, 2);
    Modify(bar, [rotates](State &s) { s.rotatesTexture = rotates; });
    // 4.3.4 sets the full coordinates at once, so a bar with no value yet
    // shows its image turned, or not, before the first layout. Full
    // coordinates crop nothing, so a texture back on the crop takes them too.
    if (void *texture = BarTexture(bar))
        SetFullTexCoord(texture, rotates);
    return 0;
}

int __fastcall Script_GetRotatesTexture(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const State *state = Find(bar);
    Game::Lua::PushBool(L, state != nullptr && state->rotatesTexture);
    return 1;
}

int __fastcall Script_SetFillStyle(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    int style = -1;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_NUMBER) {
        const double n = Game::Lua::ToNumber(L, 2);
        if (n >= kStandard && n <= kReverse && n == static_cast<int>(n))
            style = static_cast<int>(n);
    } else if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING) {
        const char *name = Game::Lua::ToString(L, 2);
        for (int i = 0; i < static_cast<int>(sizeof(kFillStyleTokens) / sizeof(kFillStyleTokens[0])); ++i)
            if (Ascii::EqualCI(name, kFillStyleTokens[i]))
                style = i;
    }
    if (style < 0) {
        Game::Lua::Error(L, "Usage: %s:SetFillStyle(fillStyle)", Game::Lua::ObjectDisplayName(bar));
        return 0;
    }
    Modify(bar, [style](State &s) { s.fillStyle = style; });
    return 0;
}

int __fastcall Script_GetFillStyle(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const State *state = Find(bar);
    Game::Lua::PushNumber(L, state != nullptr ? state->fillStyle : kStandard);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetReverseFill", &Script_SetReverseFill},     {"GetReverseFill", &Script_GetReverseFill},
    {"SetRotatesTexture", &Script_SetRotatesTexture}, {"GetRotatesTexture", &Script_GetRotatesTexture},
    {"SetFillStyle", &Script_SetFillStyle},         {"GetFillStyle", &Script_GetFillStyle},
};

const Game::Doc::Field kSetReverseFillArgs[] = {Game::Doc::Req("isReverseFill", "bool")};
const Game::Doc::Field kGetReverseFillRets[] = {Game::Doc::Req("isReverseFill", "bool")};
const Game::Doc::Field kSetRotatesTextureArgs[] = {Game::Doc::Req("rotatesTexture", "bool")};
const Game::Doc::Field kGetRotatesTextureRets[] = {Game::Doc::Req("rotatesTexture", "bool")};
const Game::Doc::Field kSetFillStyleArgs[] = {Game::Doc::Req("fillStyle", "StatusBarFillStyle")};
const Game::Doc::Field kGetFillStyleRets[] = {Game::Doc::Req("fillStyle", "StatusBarFillStyle")};

const Game::Doc::Function kSetReverseFill{"Sets whether the bar fills from the right, or from the top when vertical.",
                                          kSetReverseFillArgs, {}};
const Game::Doc::Function kGetReverseFill{"Returns whether the bar fills from the right, or from the top when vertical.",
                                          {}, kGetReverseFillRets};
const Game::Doc::Function kSetRotatesTexture{"Sets whether the bar texture is drawn turned a quarter.",
                                             kSetRotatesTextureArgs, {}};
const Game::Doc::Function kGetRotatesTexture{"Returns whether the bar texture is drawn turned a quarter.", {},
                                             kGetRotatesTextureRets};
const Game::Doc::Function kSetFillStyle{"Sets how the bar fills as its value grows.", kSetFillStyleArgs, {}};
const Game::Doc::Function kGetFillStyle{"Returns how the bar fills as its value grows.", {}, kGetFillStyleRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetReverseFill", &kSetReverseFill},       {"GetReverseFill", &kGetReverseFill},
    {"SetRotatesTexture", &kSetRotatesTexture}, {"GetRotatesTexture", &kGetRotatesTexture},
    {"SetFillStyle", &kSetFillStyle},           {"GetFillStyle", &kGetFillStyle},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_STATUSBAR_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
    Game::Lua::RegisterIntegerEnum("Enum", "StatusBarFillStyle", kFillStyleEntries,
                                   static_cast<int>(sizeof(kFillStyleEntries) / sizeof(kFillStyleEntries[0])),
                                   "SimpleStatusBarConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace StatusBar::Fill
