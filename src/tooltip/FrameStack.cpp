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

// `GameTooltip:SetFrameStack([showHidden [, showRegions [, highlightIndexChanged]]])`
// -> highlightFrame — fills the tooltip with every frame under the cursor,
// grouped by strata, top first, and returns the highlighted entry. The engine
// half of Blizzard_DebugTools' /fstack (FrameStackTooltip calls it on each
// OnUpdate, and again on each Alt press with the highlight delta).
//
// The list is 4.3.4's CGTooltip::SetFrameStack (0x0046D380 in build 15595),
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
// The rest is the current client's. Its Lua passes (showHidden, showRegions,
// highlightIndexChanged) and keeps the return as the highlight frame, and
// its string table holds the method's formats in one run — "-->",
// "%s<%d> %s", "   <%d> %s %s %s %.2f %.2f", "   <%d> %s %.2f %f.2",
// "ANCHORS" — with "SOURCE" / "   %s" beside them:
//
//   - showRegions lists each listed frame's textures and font strings under
//     the cursor right after the frame, in FRAMESTACK_REGION_COLOR, at the
//     frame's level (a region has none of its own), the name indented three
//     more spaces;
//   - one entry is highlighted. Its line opens with "-->" in place of the
//     indent, and after the whole list come its anchors — one line per set
//     point, in slot order, with what GetPoint reports: the point, the
//     target's name, the target's point, the offsets in UI units — and, when
//     Frame::SourceLocation knows it, its source. highlightIndexChanged moves
//     the highlight that many entries (retail's Lua passes +1 / -1 per Alt
//     press, 0 otherwise); the index persists between calls so the highlight
//     stays put while the cursor rests, and wraps at either end.
//   - the highlighted object is the return value and the argument of
//     OnTooltipSetFrameStack (Tooltip::SetEvents), fired before Show as
//     4.3.4 does.
//
// The anchor target is read through the node's own getter, as GetPoint does,
// after Frame::GetPointGuard's dangling-target cleanup — the same guard the
// GetPoint override runs — since a node whose target frame was freed still
// holds its pointer in 1.12. The non-highlight indent and the "ANCHORS"
// header are inferred from the string table, not from a retail capture.

#include "Game.h"
#include "Offsets.h"
#include "frame/GetPointGuard.h"
#include "frame/SourceLocation.h"
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
using PointToString_t = const char *(__fastcall *)(int point);
using GetRelative_t = void *(__thiscall *)(void *node);

// 4.3.4's line colors, packed {b,g,r,a} like every add-line color; two of each
// so neighbouring frame levels alternate (s_frameStack*Colors). Regions take
// the client's FRAMESTACK_REGION_COLOR (UI::ColorData).
constexpr uint32_t kMouseColors[2] = {0xFFFF9966, 0xFFDDDD88};
constexpr uint32_t kColors[2] = {0xFF6699FF, 0xFF88DDDD};
constexpr uint32_t kHiddenColors[2] = {0xFF666666, 0xFF999999};
constexpr uint32_t kRegionColor = 0xFF88DDDD;

// One past the highest strata (TOOLTIP = 8), so the first entry always opens
// a strata header — 4.3.4's `lastStrata = 9`.
constexpr int kNoStrata = 9;

// 4.3.4's FrameStackInfo, plus the object itself (the highlight needs it)
// and whether it is a region.
struct Entry {
    void *object;
    char name[1024];
    int strata;
    int level;
    bool mouseEnabled;
    bool region;
    uint32_t visible;
};

// The highlighted entry's index into the list, kept across calls.
int g_highlightIndex = 0;

// QSortFrameStack: strata, then level, both high to low, then name.
bool Before(const Entry &a, const Entry &b) {
    if (a.strata != b.strata)
        return a.strata > b.strata;
    if (a.level != b.level)
        return a.level > b.level;
    return Game::StrCmpI(a.name, b.name) < 0;
}

// The object's display name, as 4.3.4 forms it: the global name when
// `_G[name]` is this object, else the object's Lua table as "table: %p",
// followed by "(name)" when the object has a name.
void FormatName(void *L, void *object, char *out, size_t size) {
    auto *vtable = Game::Read<uintptr_t *>(object, 0);
    const char *name = reinterpret_cast<GetName_t>(
        vtable[Offsets::VTBL_SCRIPTOBJECT_GET_NAME / sizeof(uintptr_t)])(object);

    if (name == nullptr || *name == '\0') {
        Game::Lua::PushObject(L, object);
        std::snprintf(out, size, "%s: %p", Game::Lua::TypeName(L, Game::Lua::Type(L, -1)),
                      Game::Lua::ToPointer(L, -1));
        Game::Lua::SetTop(L, -2);
        return;
    }

    Game::Lua::PushString(L, name);
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
        void *resolved = Game::Lua::ResolveObject(L, -1);
        Game::Lua::SetTop(L, -2);
        if (resolved == object) {
            std::snprintf(out, size, "%s", name);
            return;
        }
        Game::Lua::PushObject(L, object);
    }
    std::snprintf(out, size, "%s: %p (%s)", Game::Lua::TypeName(L, Game::Lua::Type(L, -1)),
                  Game::Lua::ToPointer(L, -1), name);
    Game::Lua::SetTop(L, -2);
}

// Whether the cursor (x, y) lies in the object's layout rect; false when the
// rect cannot be resolved.
bool UnderCursor(void *object, float x, float y) {
    float rect[4]; // {minY, minX, maxY, maxX}
    if (reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(
            static_cast<uint8_t *>(object) + Offsets::OFF_REGION_ANCHOR, rect) == 0)
        return false;
    return x >= rect[1] && x <= rect[3] && y <= rect[2] && y >= rect[0];
}

// The frame's regions under the cursor, after the frame's entry.
void AddRegions(void *L, const Entry &frame, float x, float y, bool showHidden,
                std::vector<Entry> &list) {
    Game::ForEachFrameListObject(frame.object, Offsets::OFF_FRAME_REGION_LIST, [&](void *region) {
        const uint32_t visible = Game::Read<uint32_t>(region, Offsets::OFF_REGION_ACTUALLY_SHOWN);
        if ((visible == 0 && !showHidden) || !UnderCursor(region, x, y))
            return;
        Entry &e = list.emplace_back();
        e.object = region;
        FormatName(L, region, e.name, sizeof(e.name));
        e.strata = frame.strata;
        e.level = frame.level;
        e.mouseEnabled = false;
        e.region = true;
        e.visible = visible;
    });
}

// The highlighted entry's detail lines, after the list: its anchors, then
// its source. Headers in the tooltip's text color, lines in the entry's.
void AddDetails(void *L, void *self, void *object, const uint32_t &color,
                const uint32_t &headerColor) {
    Frame::GetPointGuard::CleanDanglingAnchors(object);
    auto pointName = reinterpret_cast<PointToString_t>(Offsets::FUN_FRAME_POINT_TO_STRING);
    char text[1024];
    int n = 0;
    for (int slot = 0; slot < Offsets::REGION_ANCHOR_SLOT_COUNT; ++slot) {
        auto *node = Game::Read<uint8_t *>(object, Offsets::OFF_REGION_ANCHOR_ARRAY + slot * 4);
        if (node == nullptr)
            continue;
        if (n++ == 0)
            Lines::Add(self, "ANCHORS", headerColor);
        auto *vtable = Game::Read<uintptr_t *>(node, 0);
        auto *relative = static_cast<uint8_t *>(reinterpret_cast<GetRelative_t>(
            vtable[Offsets::VTBL_ANCHOR_NODE_GET_RELATIVE / sizeof(uintptr_t)])(node));
        const double x = Game::InternalToPixel(Game::Read<float>(node, Offsets::OFF_ANCHOR_NODE_X));
        const double y = Game::InternalToPixel(Game::Read<float>(node, Offsets::OFF_ANCHOR_NODE_Y));
        // A screen anchor's target is the top itself, a bare CLayoutFrame with
        // no region in front of it (Offsets VAR_UI_CONTEXT_PTR): no name to
        // show, and `relative - OFF_REGION_ANCHOR` must not be formed.
        const bool toScreen =
            relative == nullptr || relative == Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
        if (!toScreen) {
            char relativeName[1024];
            FormatName(L, relative - Offsets::OFF_REGION_ANCHOR, relativeName, sizeof(relativeName));
            std::snprintf(text, sizeof(text), "   <%d> %s %s %s %.2f %.2f", n, pointName(slot),
                          relativeName,
                          pointName(Game::Read<int>(node, Offsets::OFF_ANCHOR_NODE_RELATIVE_POINT)),
                          x, y);
        } else {
            std::snprintf(text, sizeof(text), "   <%d> %s %.2f %.2f", n, pointName(slot), x, y);
        }
        Lines::Add(self, text, color);
    }

    if (const char *location = Frame::SourceLocation::Get(object)) {
        Lines::Add(self, "SOURCE", headerColor);
        std::snprintf(text, sizeof(text), "   %s", location);
        Lines::Add(self, text, color);
    }
}

int __fastcall Script_GameTooltipSetFrameStack(void *L) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;
    const bool showHidden = Game::Lua::ToBoolean(L, 2) != 0;
    const bool showRegions = Game::Lua::ToBoolean(L, 3) != 0;
    const int highlightDelta =
        Game::Lua::IsNumber(L, 4) ? static_cast<int>(Game::Lua::ToNumber(L, 4)) : 0;

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

    std::vector<Entry> frames;
    for (; frame != nullptr && (reinterpret_cast<uintptr_t>(frame) & 1) == 0;
         frame = Game::Read<void *>(frame, Offsets::OFF_FRAME_ENUM_NEXT)) {
        const uint32_t visible = Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_VISIBLE);
        if ((visible == 0 && !showHidden) || !UnderCursor(frame, x, y))
            continue;

        Entry &e = frames.emplace_back();
        e.object = frame;
        FormatName(L, frame, e.name, sizeof(e.name));
        e.strata = Game::Read<int>(frame, Offsets::OFF_FRAME_STRATA);
        e.level = Game::Read<int>(frame, Offsets::OFF_FRAME_LEVEL);
        e.mouseEnabled = (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_ENABLED_EVENTS) &
                          (1u << Offsets::SIMPLE_EVENT_MOUSE)) != 0;
        e.region = false;
        e.visible = visible;
    }

    std::sort(frames.begin(), frames.end(), Before);

    // Display order: each frame, then (showRegions) its regions.
    std::vector<Entry> list;
    list.reserve(frames.size());
    for (Entry &e : frames) {
        list.push_back(e);
        if (showRegions)
            AddRegions(L, list.back(), x, y, showHidden, list);
    }

    const int count = static_cast<int>(list.size());
    int highlightAt = -1;
    if (count > 0) {
        g_highlightIndex = ((g_highlightIndex + highlightDelta) % count + count) % count;
        highlightAt = g_highlightIndex;
    }

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
    const uint32_t *highlightColor = nullptr;
    for (int i = 0; i < count; ++i) {
        const Entry &e = list[i];
        if (!e.region) {
            if (e.strata != lastStrata) {
                // 1.12's name table starts at BACKGROUND; 4.3.4's names 0.
                const char *name = (e.strata == Offsets::FRAME_STRATA_WORLD)
                                       ? "WORLD"
                                       : strataName(e.strata);
                Lines::AddDouble(self, name, nullptr, textColor, textColor);
                lastLevel = e.level;
                lastStrata = e.strata;
                toggle = 0;
            }
            if (e.level != lastLevel) {
                toggle = (toggle + 1) % 2;
                lastLevel = e.level;
            }
        }
        const bool highlighted = (i == highlightAt);
        std::snprintf(text, sizeof(text), "%s<%d> %s%s", highlighted ? "-->" : "   ", e.level,
                      e.region ? "   " : "", e.name);
        const uint32_t &color = !e.visible        ? kHiddenColors[toggle]
                                : e.region       ? kRegionColor
                                : e.mouseEnabled ? kMouseColors[toggle]
                                                 : kColors[toggle];
        Lines::Add(self, text, color);
        if (highlighted)
            highlightColor = &color;
    }

    void *highlight = (highlightAt >= 0) ? list[highlightAt].object : nullptr;
    if (highlight != nullptr)
        AddDetails(L, self, highlight, *highlightColor, textColor);
    SetEvents::FireFrameStack(self, highlight);

    Game::Lua::SetTop(L, 1);
    Lines::Show(L);
    if (highlight == nullptr)
        return 0;
    Game::Lua::PushObject(L, highlight);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetFrameStack", &Script_GameTooltipSetFrameStack},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("showHidden", "bool", "false", "Also list hidden frames, in grey."),
    Game::Doc::Opt("showRegions", "bool", "false",
                   "Also list the textures and font strings under the cursor, after their frame."),
    Game::Doc::Opt("highlightIndexChanged", "number", "0",
                   "Moves the highlight this many entries down the list (negative: up). The "
                   "highlight stays where it is between calls."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("highlightFrame", "ScriptRegion", nullptr,
                   "The highlighted frame or region; nil when nothing is under the cursor."),
};
const Game::Doc::Function kSetFrameStack{
    "Fills the tooltip with every frame under the cursor, grouped by strata from the top, and "
    "returns the highlighted entry. Frames that take the mouse are orange, other shown frames "
    "blue, regions cyan. The highlighted entry opens with \"-->\" and lists its anchors and, "
    "when known, its source.",
    kArgs, kRets};

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
