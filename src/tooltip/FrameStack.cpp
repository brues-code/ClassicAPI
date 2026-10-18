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

// `GameTooltip:SetFrameStack([showHidden])` — fills the tooltip with every
// frame under the cursor, grouped by strata, top first. The engine half of
// Blizzard_DebugTools' /fstack (FrameStackTooltip calls it on each OnUpdate).
//
// A mirror of 4.3.4's CGTooltip::SetFrameStack (0x0046D380 in build 15595),
// which 1.12 lacks but whose every input it has:
//
//   - the frame list CSimpleTop::m_frames: `*ctx + OFF_UI_CONTEXT_FRAME_LIST_HEAD`,
//     next at OFF_FRAME_ENUM_NEXT — the list Script_EnumerateFrames walks;
//   - the cursor CSimpleTop::m_mousePosition (OFF_UI_CONTEXT_MOUSE_POSITION),
//     converted with the engine's NDCToDDC into the space frame rects live in;
//   - CLayoutFrame::GetRect (FUN_REGION_GET_RECT) on the frame's layout
//     sub-object, and the frame's visible / enabled-input / strata / level
//     fields — the same fields IsVisible / IsMouseEnabled / GetFrameStrata /
//     GetFrameLevel read;
//   - FrameStrataToString, the two default tooltip colors, and the
//     DEBUG_FRAMESTACK global string (shipped in the addon's locale files).
//
// The plain layout rect is used, not the mouse hit rect (hit-rect insets,
// clipping): 4.3.4 lists what is drawn under the cursor, including frames
// that do not take the mouse — those just get the other color.
//
// Fires OnTooltipSetFrameStack (Tooltip::SetEvents) before Show, as 4.3.4 does.

#include "Game.h"
#include "Offsets.h"
#include "tooltip/Lines.h"
#include "tooltip/SetEvents.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Tooltip::FrameStack {

namespace {

using GetName_t = const char *(__thiscall *)(void *object);
using GetRect_t = int(__thiscall *)(void *layout, float *rect);
using NDCToDDC_t = void(__fastcall *)(float *outX, float *outY, float x, float y);
using StrataToString_t = const char *(__fastcall *)(int strata);
using SStrCmpI_t = int(__stdcall *)(const char *a, const char *b, int n);

constexpr int kStormMaxStr = 0x7FFFFFFF;

// 4.3.4's line colors, packed {b,g,r,a} like every add-line color; two of each
// so neighbouring frame levels alternate (s_frameStack*Colors).
constexpr uint32_t kMouseColors[2] = {0xFFFF9966, 0xFFDDDD88};
constexpr uint32_t kColors[2] = {0xFF6699FF, 0xFF88DDDD};
constexpr uint32_t kHiddenColors[2] = {0xFF666666, 0xFF999999};

// One past the highest strata (TOOLTIP = 8), so the first entry always opens
// a strata header — 4.3.4's `lastStrata = 9`.
constexpr int kNoStrata = 9;

// 4.3.4's FrameStackInfo.
struct Entry {
    char name[1024];
    int strata;
    int level;
    bool mouseEnabled;
    uint32_t visible;
};

// QSortFrameStack: strata, then level, both high to low, then name.
bool Before(const Entry &a, const Entry &b) {
    if (a.strata != b.strata)
        return a.strata > b.strata;
    if (a.level != b.level)
        return a.level > b.level;
    auto cmp = reinterpret_cast<SStrCmpI_t>(Offsets::FUN_SSTR_CMP_I);
    return cmp(a.name, b.name, kStormMaxStr) < 0;
}

// The frame's display name, as 4.3.4 forms it: the global name when
// `_G[name]` is this frame, else the frame's Lua object as "table: %p",
// followed by "(name)" when the frame has a name.
void FormatName(void *L, void *frame, char *out, size_t size) {
    auto *vtable = Game::Read<uintptr_t *>(frame, 0);
    const char *name = reinterpret_cast<GetName_t>(
        vtable[Offsets::VTBL_SCRIPTOBJECT_GET_NAME / sizeof(uintptr_t)])(frame);

    if (name == nullptr || *name == '\0') {
        Game::Lua::PushObject(L, frame);
        std::snprintf(out, size, "%s: %p", Game::Lua::TypeName(L, Game::Lua::Type(L, -1)),
                      Game::Lua::ToPointer(L, -1));
        Game::Lua::SetTop(L, -2);
        return;
    }

    Game::Lua::PushString(L, name);
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
        void *object = Game::Lua::ResolveObject(L, -1);
        Game::Lua::SetTop(L, -2);
        if (object == frame) {
            std::snprintf(out, size, "%s", name);
            return;
        }
        Game::Lua::PushObject(L, frame);
    }
    std::snprintf(out, size, "%s: %p (%s)", Game::Lua::TypeName(L, Game::Lua::Type(L, -1)),
                  Game::Lua::ToPointer(L, -1), name);
    Game::Lua::SetTop(L, -2);
}

int __fastcall Script_GameTooltipSetFrameStack(void *L) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;
    const bool showHidden = Game::Lua::ToBoolean(L, 2) != 0;

    Lines::Clear(self);

    auto *ctx = Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
    if (ctx == nullptr)
        return 0;
    const float *mouse = Game::Ptr<float>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    float x = 0.0f;
    float y = 0.0f;
    reinterpret_cast<NDCToDDC_t>(Offsets::FUN_COORD_NDC_TO_DDC)(&x, &y, mouse[0], mouse[1]);

    void *frame = Game::Read<void *>(ctx, Offsets::OFF_UI_CONTEXT_FRAME_LIST_HEAD);
    if (frame == nullptr || (reinterpret_cast<uintptr_t>(frame) & 1) != 0)
        return 0;

    std::vector<Entry> stack;
    for (; frame != nullptr && (reinterpret_cast<uintptr_t>(frame) & 1) == 0;
         frame = Game::Read<void *>(frame, Offsets::OFF_FRAME_ENUM_NEXT)) {
        float rect[4]; // {minY, minX, maxY, maxX}
        if (reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(
                static_cast<uint8_t *>(frame) + Offsets::OFF_REGION_ANCHOR, rect) == 0)
            continue;
        const uint32_t visible = Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_VISIBLE);
        if ((visible == 0 && !showHidden) || x < rect[1] || x > rect[3] || y > rect[2] ||
            y < rect[0])
            continue;

        Entry &e = stack.emplace_back();
        FormatName(L, frame, e.name, sizeof(e.name));
        e.strata = Game::Read<int>(frame, Offsets::OFF_FRAME_STRATA);
        e.level = Game::Read<int>(frame, Offsets::OFF_FRAME_LEVEL);
        e.mouseEnabled = (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_ENABLED_EVENTS) &
                          (1u << Offsets::SIMPLE_EVENT_MOUSE)) != 0;
        e.visible = visible;
    }

    std::sort(stack.begin(), stack.end(), Before);

    const uint32_t textColor = Game::Read<uint32_t>(Offsets::VAR_TOOLTIP_TEXT_COLOR);
    const uint32_t titleColor = Game::Read<uint32_t>(Offsets::VAR_TOOLTIP_TITLE_COLOR);
    using GetText_t = const char *(__fastcall *)(const char *name, int count, int gender);
    const char *header =
        reinterpret_cast<GetText_t>(Offsets::FUN_FRAMESCRIPT_GET_TEXT)("DEBUG_FRAMESTACK", -1, 0);

    // The cursor in UI pixels — the values GetCursorPosition returns.
    char text[1024];
    std::snprintf(text, sizeof(text), "(%.2f, %.2f)", Game::InternalToPixel(x),
                  Game::InternalToPixel(y));
    Lines::AddDouble(self, header, text, textColor, titleColor);

    auto strataName = reinterpret_cast<StrataToString_t>(Offsets::FUN_FRAME_STRATA_TO_STRING);
    int toggle = 1;
    int lastStrata = kNoStrata;
    int lastLevel = 0;
    for (const Entry &e : stack) {
        if (e.strata != lastStrata) {
            Lines::AddDouble(self, strataName(e.strata), nullptr, textColor, textColor);
            lastLevel = e.level;
            lastStrata = e.strata;
            toggle = 0;
        }
        if (e.level != lastLevel) {
            toggle = (toggle + 1) % 2;
            lastLevel = e.level;
        }
        std::snprintf(text, sizeof(text), " <%d> %s", e.level, e.name);
        const uint32_t &color = !e.visible        ? kHiddenColors[toggle]
                                : e.mouseEnabled ? kMouseColors[toggle]
                                                 : kColors[toggle];
        Lines::AddDouble(self, text, nullptr, color, color);
    }

    SetEvents::FireFrameStack(self);

    Game::Lua::SetTop(L, 1);
    Lines::Show(L);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetFrameStack", &Script_GameTooltipSetFrameStack},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("showHidden", "bool", "false", "Also list hidden frames, in grey."),
};
const Game::Doc::Function kSetFrameStack{
    "Fills the tooltip with every frame under the cursor, grouped by strata from the top. "
    "Frames that take the mouse are orange, other shown frames blue.",
    kArgs, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetFrameStack", &kSetFrameStack},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Tooltip::FrameStack
