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

// The Line region type: frame:CreateLine and XML <Line>, as the 3.80.2 client
// builds its CSimpleLine (factory 0x1428AF930).
//
// A Line is a texture region with its own copy of CSimpleTexture's two
// vtables, as 3.80.2's is the texture constructor plus its own vtables:
// GetObjectType answers "Line", IsObjectType is true for "Line" and
// "Texture", the four mask methods are not on it (3.80.2 registers them for
// Texture only, 0x140A67340), and its own methods come first.
//
// The endpoints are two bare CLayoutFrames, 1.12's own (FUN_LAYOUT_CONSTRUCTOR),
// as 3.80.2 builds two 1x1 layout frames at +0x200 and +0x2E8. SetStartPoint
// anchors the start frame's CENTER to a point of another region, and the line
// region's own TOPLEFT and BOTTOMRIGHT are anchored to the CENTERs of the two
// frames. So the engine's dependents chain moves the line: a frame an
// endpoint names moves, the endpoint resolves, the line resolves, and the
// texture's layout callback stores the corners, where this module writes the
// line's quad instead (Texture::CornerStore). Both frames take the parent's
// layout scale (3.80.2's set-scale slot 0x1428D1ED0 forwards the incoming
// scale), and they are 1x1 UI unit so a CENTER point alone gives them a rect.
//
// The quad (3.80.2's 0x1428D13C0): with S and E the centres of the two
// frames, n = (-dy, dx) normalized (when |d|^2 > 2^-22), times half the
// thickness times the effective scale. The corners are UL = S + n, LL = S - n,
// UR = E + n, LR = E - n, in the texture's corner order (UL, LL, UR, LR are
// slots 0..3 at OFF_SIMPLETEXTURE_CORNERS, in layout space, y up). So u runs
// from the start to the end. Nothing is drawn while an endpoint has no rect.
// SetRotation has no effect on a line. A vertex offset moves its corner in the
// line's frame: x along the line, +y toward the right of the line's direction
// (3.80.2's sign). 3.80.2 drops every offset of an exactly vertical line (its
// terms are multiplied by dx); that is not copied.
//
// The region's rect runs from the start to the end and is normalized (3.80.2's
// layout slot 0x1428D1C20), so it is never inverted; it leaves out the
// thickness. The layout callback normalizes it before the texture's own.
//
// Hit area (3.80.2's IsPointInside 0x1428D1880): a rectangle around the
// segment, `(L + T) / 2` along it from its midpoint and `T / 2` across, with
// T the thickness times the effective scale plus the hit-rect thickness (not
// scaled). IsMouseOver on a line uses it; its offset arguments do not apply.
//
// The endpoint frames are freed with the region (Frame::RegionDestruction),
// whose own layout still points at them then: their destructor clears that
// point, as it does for any dependent.

#include "texture/Line.h"

#include "Game.h"
#include "Offsets.h"
#include "animation/Region.h"
#include "api/Documentation.h"
#include "frame/Layout.h"
#include "frame/RegionDestruction.h"
#include "frame/Subclass.h"
#include "storm/Memory.h"
#include "texture/Construct.h"
#include "texture/CornerStore.h"
#include "texture/Transform.h"
#include "xml/Anchors.h"
#include "xml/FrameLoad.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

namespace Texture::Line {

namespace {

using Frame::Layout::OwnerOf;
using Frame::Subclass::Slot;

using LayoutCtor_t = void *(__thiscall *)(void *layout);
using LayoutDtor_t = void(__thiscall *)(void *layout);
using SetLayoutScale_t = uint32_t(__thiscall *)(void *layout, float scale, uint32_t force);
using SetExtent_t = void(__thiscall *)(void *layout, float value);
using IsAnchoredTo_t = int(__thiscall *)(void *layout, void *target);
using ByName_t = void *(__thiscall *)(void *layout, const char *name);
using GetLayoutParent_t = void *(__thiscall *)(void *layout);
using LayerFromName_t = bool(__fastcall *)(const char *name, int *outLayer);
using ReadDimension_t = int(__fastcall *)(const void *node, float *x, float *y, void *status);

// The vtable slots this module overrides, `__thiscall` as `__fastcall`.
using GetScriptMethod_t = int(__fastcall *)(void *self, void *edx, void *L, const char *name);
using IsA_t = bool(__fastcall *)(void *self, void *edx, int typeId);
using IsObjectType_t = int(__fastcall *)(void *self, void *edx, const char *name);
using OnFrameSizeChanged_t = void(__fastcall *)(void *layout, void *edx, const float *oldRect);
using SetLayoutScaleSlot_t = uint32_t(__fastcall *)(void *layout, void *edx, float scale,
                                                    uint32_t force);

// 3.80.2's change threshold for a thickness (DAT_14415B534).
constexpr float kEpsilon = 2.384185791015625e-07f;

constexpr char kCreateLineUsage[] =
    "Usage: local line = self:CreateLine([name, drawLayer, templateName, subLevel])";
constexpr char kSetThicknessUsage[] = "Usage: self:SetThickness(thickness)";
constexpr char kSetHitRectThicknessUsage[] = "Usage: self:SetHitRectThickness(thickness)";
constexpr char kEndpointArgs[] =
    "Arguments: (\"relativePoint\" [, region or nil] [, offsetX, offsetY])";
constexpr char kLineType[] = "Line";

struct State {
    void *start = nullptr;   // bare CLayoutFrame
    void *end = nullptr;     // bare CLayoutFrame
    float thickness = 0.0f;  // layout units, unscaled
    float hitThickness = 0.0f; // layout units, never scaled
};

std::unordered_map<const void *, State> g_lines;

int g_typeId = 0;
Frame::Subclass::MethodHash g_hash;
Frame::Subclass::VTable<Offsets::VTBL_SIMPLETEXTURE_SLOTS> g_vtable;
Frame::Subclass::VTable<Offsets::VTBL_SIMPLETEXTURE_LAYOUT_SLOTS> g_layoutVtable;

State *Find(const void *region) {
    auto it = g_lines.find(region);
    return it != g_lines.end() ? &it->second : nullptr;
}

// The Frame::Layout helpers take an object and step to its layout part; a
// bare layout frame is passed as the object it would be the part of.
void *AsObject(void *layout) { return OwnerOf(layout); }

void *Top() { return Game::Read<void *>(static_cast<uintptr_t>(Offsets::VAR_UI_CONTEXT_PTR)); }

void *ParentLayout(void *region) {
    void *layout = Frame::Layout::Of(region);
    return reinterpret_cast<GetLayoutParent_t>(
        Game::Read<const void *>(Game::Read<const void *>(layout, 0), Offsets::VTBL_LAYOUT_GET_LAYOUT_PARENT))(layout);
}

float ParentScale(void *region) {
    void *parent = Game::Read<void *>(region, Offsets::OFF_REGION_PARENT);
    const float scale = parent != nullptr ? Frame::Layout::EffectiveScale(parent)
                                          : Frame::Layout::EffectiveScale(region);
    return scale > 0.0f ? scale : 1.0f;
}

void SetEndpointScale(void *endpoint, float scale, uint32_t force) {
    reinterpret_cast<SetLayoutScale_t>(Offsets::FUN_LAYOUT_SET_LAYOUT_SCALE)(endpoint, scale, force);
}

void *NewEndpoint(float scale) {
    void *layout = Storm::Alloc(Offsets::LAYOUT_FRAME_SIZE, __FILE__, __LINE__, true);
    if (layout == nullptr)
        return nullptr;
    reinterpret_cast<LayoutCtor_t>(Offsets::FUN_LAYOUT_CONSTRUCTOR)(layout);
    SetEndpointScale(layout, scale, 1);
    const float unit = Game::PixelToInternal(1.0f);
    reinterpret_cast<SetExtent_t>(Offsets::FUN_LAYOUT_SET_WIDTH)(layout, unit);
    reinterpret_cast<SetExtent_t>(Offsets::FUN_LAYOUT_SET_HEIGHT)(layout, unit);
    return layout;
}

void DeleteEndpoint(void *layout) {
    if (layout == nullptr)
        return;
    reinterpret_cast<LayoutDtor_t>(Offsets::FUN_LAYOUT_DESTRUCTOR)(layout);
    Storm::Free(layout, __FILE__, __LINE__);
}

bool HasPoint(void *endpoint) {
    return Frame::Layout::PointNode(AsObject(endpoint), Offsets::FRAMEPOINT_CENTER) != nullptr;
}

// The endpoint's CENTER to `relPoint` of `relative`, offsets in layout units.
void SetEndpoint(void *endpoint, void *relative, int relPoint, float x, float y) {
    Frame::Layout::SetPoint(AsObject(endpoint), Offsets::FRAMEPOINT_CENTER, relative, relPoint, x, y);
}

// The centre of an endpoint's rect. `resolve` lays a pending one out first,
// as the edge getters do.
bool Center(void *endpoint, bool resolve, float &x, float &y) {
    float rect[4]; // bottom, left, top, right
    const bool valid = resolve ? Frame::Layout::GetResolvedRect(AsObject(endpoint), rect)
                               : Frame::Layout::GetRect(AsObject(endpoint), rect);
    if (!valid)
        return false;
    x = (rect[1] + rect[3]) * 0.5f;
    y = (rect[0] + rect[2]) * 0.5f;
    return true;
}

struct Segment {
    float sx, sy, ex, ey;
};

bool GetSegment(const State &s, bool resolve, Segment &seg) {
    return Center(s.start, resolve, seg.sx, seg.sy) && Center(s.end, resolve, seg.ex, seg.ey);
}

// --- the quad -------------------------------------------------------------------

// Transform's corner role (BL, TL, BR, TR) of each corner slot (UL, LL, UR, LR).
constexpr int kOffsetRole[4] = {1, 0, 3, 2};

void WriteQuad(void *region, const State &s, float *corners) {
    Segment seg;
    if (!GetSegment(s, false, seg)) {
        for (int k = 0; k < 4; ++k)
            corners[k * 3 + 0] = corners[k * 3 + 1] = 0.0f;
        return;
    }
    const float dx = seg.ex - seg.sx;
    const float dy = seg.ey - seg.sy;
    const float len2 = dx * dx + dy * dy;
    const float len = std::sqrt(len2);
    float nx = -dy, ny = dx;
    if (len2 > kEpsilon) {
        nx /= len;
        ny /= len;
    }
    const float half = s.thickness * 0.5f * Frame::Layout::EffectiveScale(region);
    nx *= half;
    ny *= half;
    const float x[4] = {seg.sx + nx, seg.sx - nx, seg.ex + nx, seg.ex - nx};
    const float y[4] = {seg.sy + ny, seg.sy - ny, seg.ey + ny, seg.ey - ny};

    float offX[4], offY[4];
    const bool offsets = len2 > kEpsilon && Texture::Transform::VertexOffsets(region, offX, offY);
    const float tx = len > 0.0f ? dx / len : 1.0f;
    const float ty = len > 0.0f ? dy / len : 0.0f;
    for (int k = 0; k < 4; ++k) {
        float px = x[k], py = y[k];
        if (offsets) {
            const float ox = Frame::Layout::FromLocalUnits(region, offX[kOffsetRole[k]]);
            const float oy = Frame::Layout::FromLocalUnits(region, offY[kOffsetRole[k]]);
            // ox along t, oy against n^ = (-ty, tx).
            px += ox * tx + oy * ty;
            py += ox * ty - oy * tx;
        }
        corners[k * 3 + 0] = px;
        corners[k * 3 + 1] = py;
    }
}

void OnCornersStored(void *region) {
    if (const State *s = Find(region))
        WriteQuad(region, *s, Game::Ptr<float>(region, Offsets::OFF_SIMPLETEXTURE_CORNERS));
}

const Texture::CornerStore::AutoSubscribe _cornerSub{&OnCornersStored};

// --- the vtables ------------------------------------------------------------------

int __fastcall LineGetScriptMethod(void *self, void *, void *L, const char *name) {
    if (g_hash.PushMethod(L, name))
        return 1;
    static const char *const kTextureOnly[] = {"AddMaskTexture", "RemoveMaskTexture",
                                               "GetNumMaskTextures", "GetMaskTexture"};
    for (const char *m : kTextureOnly)
        if (name != nullptr && std::strcmp(name, m) == 0)
            return 0;
    return Slot<GetScriptMethod_t>(Offsets::VTBL_SIMPLETEXTURE, Offsets::VTBL_FRAME_GET_SCRIPT_METHOD)(
        self, nullptr, L, name);
}

bool __fastcall LineIsA(void *self, void *, int typeId) {
    return (g_typeId != 0 && typeId == g_typeId) ||
           Slot<IsA_t>(Offsets::VTBL_SIMPLETEXTURE, Offsets::VTBL_FRAME_IS_A)(self, nullptr, typeId);
}

int __fastcall LineIsObjectType(void *self, void *, const char *name) {
    if (name != nullptr && Game::StrCmpI(name, kLineType) == 0)
        return 1;
    return Slot<IsObjectType_t>(Offsets::VTBL_SIMPLETEXTURE, Offsets::VTBL_FRAME_IS_OBJECT_TYPE)(
        self, nullptr, name);
}

const char *__fastcall LineGetObjectType(void *, void *) { return kLineType; }

// The rect normalized, then the texture's callback (which stores the corners).
void __fastcall LineOnFrameSizeChanged(void *layout, void *, const float *oldRect) {
    float *rect = Game::Ptr<float>(OwnerOf(layout), Offsets::OFF_REGION_RECT); // bottom, left, top, right
    if (rect[3] < rect[1])
        std::swap(rect[1], rect[3]);
    if (rect[2] < rect[0])
        std::swap(rect[0], rect[2]);
    Slot<OnFrameSizeChanged_t>(Offsets::VTBL_SIMPLETEXTURE_LAYOUT, Offsets::VTBL_LAYOUT_ON_FRAME_SIZE_CHANGED)(
        layout, nullptr, oldRect);
}

// The parent's scale reaches the line, then both endpoints.
uint32_t __fastcall LineSetLayoutScale(void *layout, void *, float scale, uint32_t force) {
    const uint32_t result = Slot<SetLayoutScaleSlot_t>(Offsets::VTBL_SIMPLETEXTURE_LAYOUT,
                                                       Offsets::VTBL_LAYOUT_SET_SCALE)(layout, nullptr, scale, force);
    if (const State *s = Find(OwnerOf(layout))) {
        SetEndpointScale(s->start, scale, force);
        SetEndpointScale(s->end, scale, force);
    }
    return result;
}

void BuildVTables() {
    if (g_vtable.Built())
        return;
    auto fn = [](auto f) { return reinterpret_cast<const void *>(f); };
    g_vtable.CopyFrom(Offsets::VTBL_SIMPLETEXTURE);
    g_vtable.Set(Offsets::VTBL_FRAME_GET_SCRIPT_METHOD, fn(&LineGetScriptMethod));
    g_vtable.Set(Offsets::VTBL_FRAME_IS_A, fn(&LineIsA));
    g_vtable.Set(Offsets::VTBL_FRAME_IS_OBJECT_TYPE, fn(&LineIsObjectType));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_OBJECT_TYPE, fn(&LineGetObjectType));
    g_layoutVtable.CopyFrom(Offsets::VTBL_SIMPLETEXTURE_LAYOUT);
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_ON_FRAME_SIZE_CHANGED, fn(&LineOnFrameSizeChanged));
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_SET_SCALE, fn(&LineSetLayoutScale));
    // A line has no size of its own: CLayoutFrame's getters answer its
    // explicit size (0), so GetWidth / GetHeight read the rect. The texture's
    // answer the image's size instead.
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_GET_WIDTH, reinterpret_cast<const void *>(Offsets::FUN_LAYOUT_GET_WIDTH));
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_GET_HEIGHT, reinterpret_cast<const void *>(Offsets::FUN_LAYOUT_GET_HEIGHT));
}

// A texture region made a line: the vtables, the two endpoint frames with
// no point, the default thickness of 4 UI units, and the line's TOPLEFT and
// BOTTOMRIGHT on the endpoints (3.80.2's factory 0x1428AF930).
State *Make(void *region) {
    if (State *s = Find(region))
        return s;
    BuildVTables();
    const float scale = ParentScale(region);
    State s;
    s.start = NewEndpoint(scale);
    s.end = NewEndpoint(scale);
    if (s.start == nullptr || s.end == nullptr) {
        DeleteEndpoint(s.start);
        DeleteEndpoint(s.end);
        return nullptr;
    }
    s.thickness = Game::PixelToInternal(4.0f);
    Game::Ref<const void *>(region, 0) = g_vtable.slots;
    Game::Ref<const void *>(region, Offsets::OFF_REGION_ANCHOR) = g_layoutVtable.slots;
    State &stored = g_lines[region] = s;
    Frame::Layout::SetPoint(region, Offsets::FRAMEPOINT_TOPLEFT, stored.start, Offsets::FRAMEPOINT_CENTER,
                            0.0f, 0.0f, false);
    Frame::Layout::SetPoint(region, Offsets::FRAMEPOINT_BOTTOMRIGHT, stored.end, Offsets::FRAMEPOINT_CENTER,
                            0.0f, 0.0f, false);
    return &stored;
}

// 3.80.2's post-load slot 0x1428D1280: an endpoint a template did not set
// goes to a corner of the parent, the start to TOPLEFT and the end to
// BOTTOMRIGHT.
void ApplyDefaults(void *region, const State &s) {
    void *parent = ParentLayout(region);
    if (parent == nullptr)
        return;
    if (!HasPoint(s.start))
        SetEndpoint(s.start, parent, Offsets::FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    if (!HasPoint(s.end))
        SetEndpoint(s.end, parent, Offsets::FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
}

void SetThickness(void *region, State &s, float thickness) {
    if (std::fabs(s.thickness - thickness) < kEpsilon)
        return;
    s.thickness = thickness;
    Rebuild(region);
}

// --- XML ------------------------------------------------------------------------

const char *DisplayName(void *object) { return Game::Lua::ObjectDisplayName(object); }

void Warn(void *status, const char *fmt, const char *a, const char *b = nullptr) {
    if (status != nullptr)
        Xml::Warn(status, fmt, a, b);
}

const void *FirstChildTagged(const void *node, const char *tag) {
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c))
        if (Xml::TagIs(c, tag))
            return c;
    return nullptr;
}

bool HasValue(const char *s) { return s != nullptr && *s != '\0'; }

// <StartAnchor> / <EndAnchor>: the attributes and children of <Anchor>
// (3.80.2's FUN_1405954C0). `relativePoint` defaults to `point`; with neither
// `relativeTo` nor `relativeKey` the anchor is on the parent.
void LoadAnchor(void *region, void *endpoint, const void *anchor, void *status) {
    const char *relativeTo = Xml::Attr(anchor, "relativeTo");
    const char *relativeKey = Xml::Attr(anchor, "relativeKey");
    const char *name = DisplayName(region);
    if (HasValue(relativeTo) && HasValue(relativeKey)) {
        Warn(status, "%s: has relativeTo AND relativeKey (bad dog, no biscuit!)", name);
        return;
    }
    int point = Offsets::FRAMEPOINT_TOPLEFT;
    if (const char *p = Xml::Attr(anchor, "point"); HasValue(p) && !Frame::Layout::StringToPoint(p, &point))
        return;
    int relPoint = point;
    if (const char *p = Xml::Attr(anchor, "relativePoint");
        HasValue(p) && !Frame::Layout::StringToPoint(p, &relPoint))
        return;

    void *layout = Frame::Layout::Of(region);
    void *relative = nullptr;
    if (HasValue(relativeKey)) {
        void *target = Xml::Anchors::ResolveKeyPath(region, relativeKey, true);
        if (target == nullptr) {
            Warn(status, "%s: Couldn't find relative key: %s", name, relativeKey);
            return;
        }
        if (!Game::Lua::IsA(target, Offsets::VAR_REGION_LUA_TYPE_ID)) {
            Warn(status, "%s: Attempt to anchor to non-region: %s", name, relativeKey);
            return;
        }
        relative = Frame::Layout::Of(target);
    } else if (HasValue(relativeTo)) {
        relative = reinterpret_cast<ByName_t>(Offsets::FUN_REGION_LAYOUT_FRAME_BY_NAME)(layout, relativeTo);
        if (relative == nullptr) {
            Warn(status, "%s: Couldn't find relative frame: %s", name, relativeTo);
            return;
        }
    } else {
        relative = ParentLayout(region);
        if (relative == nullptr)
            return;
    }
    if (relative == layout) {
        Warn(status, "%s: anchored to itself: %s", name, HasValue(relativeKey) ? relativeKey : relativeTo);
        return;
    }

    float x = 0.0f, y = 0.0f;
    const void *dimension = FirstChildTagged(anchor, "Offset");
    if (dimension == nullptr && (HasValue(Xml::Attr(anchor, "x")) || HasValue(Xml::Attr(anchor, "y")) ||
                                 FirstChildTagged(anchor, "AbsDimension") != nullptr ||
                                 FirstChildTagged(anchor, "RelDimension") != nullptr))
        dimension = anchor;
    if (dimension != nullptr)
        reinterpret_cast<ReadDimension_t>(Offsets::FUN_XML_READ_DIMENSION)(dimension, &x, &y, status);
    SetEndpoint(endpoint, relative, relPoint, x, y);
}

// One node: `thickness`, then its first <StartAnchor> and first <EndAnchor>
// (3.80.2's LoadXML 0x1428D10D0).
void LoadNode(void *region, State &s, const void *node, void *status) {
    float thickness = 0.0f;
    if (Xml::FloatAttr(node, "thickness", thickness))
        SetThickness(region, s, Game::PixelToInternal(thickness));
    if (const void *a = FirstChildTagged(node, "StartAnchor"))
        LoadAnchor(region, s.start, a, status);
    if (const void *a = FirstChildTagged(node, "EndAnchor"))
        LoadAnchor(region, s.end, a, status);
}

// The node's inherited templates first, each with its own `inherits`, then
// the node: the order the texture loader applies them in.
void LoadWithTemplates(void *region, State &s, const void *node, void *status, int depth) {
    if (depth > 16)
        return; // a template cycle; the loader reports it
    if (const char *inherits = Xml::Attr(node, "inherits"); inherits != nullptr) {
        std::string name;
        for (const char *p = inherits;; ++p) {
            if (*p == ',' || *p == '\0') {
                if (!name.empty())
                    if (const void *t = Xml::Template(name.c_str()))
                        LoadWithTemplates(region, s, t, status, depth + 1);
                name.clear();
                if (*p == '\0')
                    break;
            } else if (*p != ' ' && *p != '\t') {
                name += *p;
            }
        }
    }
    LoadNode(region, s, node, status);
}

// --- Lua ------------------------------------------------------------------------

void *ResolveLine(void *L) {
    return Game::Lua::ResolveTypedObject(L, 1, reinterpret_cast<uintptr_t>(&g_typeId));
}

// The error 3.80.2's endpoint setters store (FUN_1405AC570), with the method
// named "SetStartPoint()" so its parentheses appear twice.
void RaiseEndpointError(void *L, void *region, const char *method, const char *message) {
    Game::Lua::RaiseClientMethodError(L, Game::Lua::ObjectName(region), method, message);
}

// SetStartPoint / SetEndPoint (3.80.2's 0x1428CEC30).
int SetEndpointScript(void *L, bool start) {
    void *region = ResolveLine(L);
    if (region == nullptr)
        return 0;
    State *s = Find(region);
    if (s == nullptr)
        return 0;
    const char *method = start ? "SetStartPoint()" : "SetEndPoint()";
    if (!Game::Lua::IsString(L, 2)) {
        RaiseEndpointError(L, region, method, kEndpointArgs);
        return 0;
    }
    int relPoint = 0;
    if (!Frame::Layout::StringToPoint(Game::Lua::ToString(L, 2), &relPoint)) {
        RaiseEndpointError(L, region, method, "Unknown region point");
        return 0;
    }
    void *layout = Frame::Layout::Of(region);
    void *relative = nullptr;
    int offsetIndex = 4;
    char message[Game::Lua::kClientErrorSize];
    switch (Game::Lua::Type(L, 3)) {
    case Game::Lua::TYPE_STRING: {
        const char *name = Game::Lua::ToString(L, 3);
        relative = reinterpret_cast<ByName_t>(Offsets::FUN_REGION_LAYOUT_FRAME_BY_NAME)(layout, name);
        if (relative == nullptr) {
            std::snprintf(message, sizeof message, "Couldn't find region named '%s'", name);
            RaiseEndpointError(L, region, method, message);
            return 0;
        }
        break;
    }
    case Game::Lua::TYPE_TABLE: {
        void *object = Game::Lua::ResolveObject(L, 3);
        if (object == nullptr) {
            RaiseEndpointError(L, region, method, "Attempt to find 'this' in non-framescript object");
            return 0;
        }
        if (!Game::Lua::IsA(object, Offsets::VAR_REGION_LUA_TYPE_ID)) {
            RaiseEndpointError(L, region, method, "Wrong object type for function");
            return 0;
        }
        relative = Frame::Layout::Of(object);
        break;
    }
    case Game::Lua::TYPE_NIL:
        relative = Top();
        break;
    default: // a number, a boolean or nothing: the parent, offsets from argument 3
        relative = ParentLayout(region);
        if (relative == nullptr)
            relative = Top();
        offsetIndex = 3;
        break;
    }
    if (relative == layout) {
        RaiseEndpointError(L, region, method, "Trying to anchor to itself");
        return 0;
    }
    if (relative != Top() &&
        reinterpret_cast<IsAnchoredTo_t>(Offsets::FUN_LAYOUT_IS_ANCHORED_TO)(relative, layout) != 0) {
        const char *name = Game::Lua::ObjectName(OwnerOf(relative));
        std::snprintf(message, sizeof message, "%s is dependent on this", name != nullptr ? name : "<unnamed>");
        RaiseEndpointError(L, region, method, message);
        return 0;
    }
    float x = 0.0f, y = 0.0f;
    if (Game::Lua::IsNumber(L, offsetIndex) && Game::Lua::IsNumber(L, offsetIndex + 1)) {
        x = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, offsetIndex)));
        y = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, offsetIndex + 1)));
    }
    SetEndpoint(start ? s->start : s->end, relative, relPoint, x, y);
    return 0;
}

int __fastcall Script_SetStartPoint(void *L) { return SetEndpointScript(L, true); }
int __fastcall Script_SetEndPoint(void *L) { return SetEndpointScript(L, false); }

// GetStartPoint / GetEndPoint (3.80.2's 0x1428CF220): nothing without a
// point; else relativePoint, relativeTo (nil on the screen), offsetX, offsetY.
int GetEndpointScript(void *L, bool start) {
    void *region = ResolveLine(L);
    const State *s = region != nullptr ? Find(region) : nullptr;
    if (s == nullptr)
        return 0;
    Frame::Layout::Point p;
    if (!Frame::Layout::GetPoint(AsObject(start ? s->start : s->end), Offsets::FRAMEPOINT_CENTER, &p))
        return 0;
    if (p.relativeLayout == nullptr) {
        Game::Lua::PushString(L, "CENTER");
        Game::Lua::PushNumber(L, Game::InternalToPixel(p.x));
        Game::Lua::PushNumber(L, Game::InternalToPixel(p.y));
        return 3;
    }
    Game::Lua::PushString(L, Frame::Layout::PointToString(p.relPoint));
    if (p.relativeLayout == Top())
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushObject(L, OwnerOf(p.relativeLayout));
    Game::Lua::PushNumber(L, Game::InternalToPixel(p.x));
    Game::Lua::PushNumber(L, Game::InternalToPixel(p.y));
    return 4;
}

int __fastcall Script_GetStartPoint(void *L) { return GetEndpointScript(L, true); }
int __fastcall Script_GetEndPoint(void *L) { return GetEndpointScript(L, false); }

// Clears both endpoints; the line's own two points stay on them. A line with
// no endpoint draws nothing.
int __fastcall Script_ClearAllPoints(void *L) {
    void *region = ResolveLine(L);
    State *s = region != nullptr ? Find(region) : nullptr;
    if (s == nullptr)
        return 0;
    for (void *endpoint : {s->start, s->end}) {
        Frame::Layout::ClearAllPoints(AsObject(endpoint));
        Frame::Layout::Resize(AsObject(endpoint), true); // the rect is no longer valid
    }
    Rebuild(region);
    return 0;
}

int SetThicknessScript(void *L, bool hitRect) {
    void *region = ResolveLine(L);
    State *s = region != nullptr ? Find(region) : nullptr;
    if (s == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::ArgError(L, 2, hitRect ? kSetHitRectThicknessUsage : kSetThicknessUsage);
        return 0;
    }
    const float value = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 2)));
    if (!hitRect)
        SetThickness(region, *s, value);
    else if (std::fabs(s->hitThickness - value) >= kEpsilon)
        s->hitThickness = value;
    return 0;
}

int __fastcall Script_SetThickness(void *L) { return SetThicknessScript(L, false); }
int __fastcall Script_SetHitRectThickness(void *L) { return SetThicknessScript(L, true); }

int GetThicknessScript(void *L, bool hitRect) {
    void *region = ResolveLine(L);
    const State *s = region != nullptr ? Find(region) : nullptr;
    if (s == nullptr)
        return 0;
    Game::Lua::PushNumber(L, Game::InternalToPixel(hitRect ? s->hitThickness : s->thickness));
    return 1;
}

int __fastcall Script_GetThickness(void *L) { return GetThicknessScript(L, false); }
int __fastcall Script_GetHitRectThickness(void *L) { return GetThicknessScript(L, true); }

// The cursor over the line's hit area.
int __fastcall Script_IsMouseOver(void *L) {
    void *region = ResolveLine(L);
    const State *s = region != nullptr ? Find(region) : nullptr;
    if (s == nullptr)
        return 0;
    Segment seg;
    if (!GetSegment(*s, true, seg)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::SetTop(L, 0);
    Game::Lua::CallScript(Offsets::FUN_SCRIPT_GETCURSORPOSITION, L); // (x, y) in UI units
    const float px = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 1)));
    const float py = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 2)));
    Game::Lua::SetTop(L, 0);

    const float dx = seg.ex - seg.sx;
    const float dy = seg.ey - seg.sy;
    const float len = std::sqrt(dx * dx + dy * dy);
    const float tx = len > 0.0f ? dx / len : 1.0f;
    const float ty = len > 0.0f ? dy / len : 0.0f;
    const float rx = px - (seg.sx + seg.ex) * 0.5f;
    const float ry = py - (seg.sy + seg.ey) * 0.5f;
    const float along = rx * tx + ry * ty;
    const float across = ry * tx - rx * ty;
    const float t = s->thickness * Frame::Layout::EffectiveScale(region) + s->hitThickness;
    Game::Lua::PushBool(L, std::fabs(along) <= (len + t) * 0.5f && std::fabs(across) <= t * 0.5f);
    return 1;
}

// frame:CreateLine([name, drawLayer, templateName, subLevel]): the arguments
// as 3.80.2's 0x140A842D0 checks them, then the engine's CreateTexture (which
// reads name, layer and template off this same stack and applies the
// template), then the line. subLevel is checked and has no effect: 1.12 has
// no sublevels.
int __fastcall Script_CreateLine(void *L) {
    void *frame = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_FRAME_LUA_TYPE_ID, false);
    if (frame == nullptr) {
        Game::Lua::ArgError(L, 1, kCreateLineUsage);
        return 0;
    }
    auto optional = [L](int i) { return Game::Lua::Type(L, i) <= Game::Lua::TYPE_NIL; };
    if (!optional(2) && !Game::Lua::IsString(L, 2)) {
        Game::Lua::ArgError(L, 2, kCreateLineUsage);
        return 0;
    }
    if (!optional(3)) {
        int layer = 0;
        if (Game::Lua::Type(L, 3) != Game::Lua::TYPE_STRING ||
            !reinterpret_cast<LayerFromName_t>(Offsets::FUN_XML_DRAW_LAYER_FROM_NAME)(Game::Lua::ToString(L, 3),
                                                                                      &layer)) {
            Game::Lua::ArgError(L, 3, kCreateLineUsage);
            return 0;
        }
    }
    char templateName[128] = "";
    if (!optional(4)) {
        if (!Game::Lua::IsString(L, 4)) {
            Game::Lua::ArgError(L, 4, kCreateLineUsage);
            return 0;
        }
        std::snprintf(templateName, sizeof templateName, "%s", Game::Lua::ToString(L, 4));
    }
    if (!optional(5)) {
        if (!Game::Lua::IsNumber(L, 5)) {
            Game::Lua::ArgError(L, 5, kCreateLineUsage);
            return 0;
        }
        const double subLevel = Game::Lua::ToNumber(L, 5);
        if (!(subLevel >= -128.0 && subLevel <= 127.0)) {
            Game::Lua::ArgError(L, 5, "outside of expected range -128 to 127 - Usage: local line = "
                                      "self:CreateLine([name, drawLayer, templateName, subLevel])");
            return 0;
        }
        const int level = static_cast<int8_t>(subLevel);
        if (level < -8 || level > 7) {
            Game::Lua::RaiseClientMethodError(L, Game::Lua::ObjectName(frame), "CreateLine",
                                              "Sublevel must be between -8 and 7");
            return 0;
        }
    }
    if (Game::Lua::GetTop(L) > 4)
        Game::Lua::SetTop(L, 4);
    const int rc = Game::Lua::CallScript(Offsets::FUN_SCRIPT_CREATE_TEXTURE, L);
    if (rc != 1)
        return rc; // the engine raised an error, or pushed nothing
    void *region = Game::Lua::ResolveObject(L, Game::Lua::GetTop(L));
    State *s = region != nullptr ? Make(region) : nullptr;
    if (s != nullptr && templateName[0] != '\0') {
        if (const void *node = Xml::Template(templateName))
            LoadWithTemplates(region, *s, node, nullptr, 0);
        ApplyDefaults(region, *s);
    }
    return 1; // the region the engine pushed
}

// --- lifetime -------------------------------------------------------------------

void OnRegionDestroyed(void *region) {
    auto it = g_lines.find(region);
    if (it == g_lines.end())
        return;
    DeleteEndpoint(it->second.start);
    DeleteEndpoint(it->second.end);
    g_lines.erase(it);
}

const Frame::RegionDestruction::AutoSubscribe _destructionSub{&OnRegionDestroyed};

// A region is always destroyed first; a pooled block rebuilt with an entry
// left would be a missed destruction. The entry is dropped, not freed: its
// endpoints' dependent was that block.
void OnTextureConstructed(void *mem) { g_lines.erase(mem); }

const Texture::Construct::AutoSubscribe _constructSub{&OnTextureConstructed};

// --- registration ---------------------------------------------------------------

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetStartPoint", &Script_SetStartPoint},
    {"SetEndPoint", &Script_SetEndPoint},
    {"GetStartPoint", &Script_GetStartPoint},
    {"GetEndPoint", &Script_GetEndPoint},
    {"SetThickness", &Script_SetThickness},
    {"GetThickness", &Script_GetThickness},
    {"SetHitRectThickness", &Script_SetHitRectThickness},
    {"GetHitRectThickness", &Script_GetHitRectThickness},
    {"ClearAllPoints", &Script_ClearAllPoints},
    {"IsMouseOver", &Script_IsMouseOver},
};

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"CreateLine", &Script_CreateLine},
};

const Game::Doc::Field kSetPointArgs[] = {
    Game::Doc::Req("relativePoint", "FramePoint"),
    Game::Doc::Opt("relativeTo", "ScriptRegion", nullptr,
                   "A region or its name; nil for the screen. When left out, the parent."),
    Game::Doc::Opt("offsetX", "uiUnit", "0"),
    Game::Doc::Opt("offsetY", "uiUnit", "0"),
};
const Game::Doc::Field kGetPointRets[] = {
    Game::Doc::Req("relativePoint", "FramePoint"),
    Game::Doc::Opt("relativeTo", "ScriptRegion", nullptr, "nil when the point is on the screen."),
    Game::Doc::Req("offsetX", "uiUnit"),
    Game::Doc::Req("offsetY", "uiUnit"),
};
const Game::Doc::Function kSetStartPoint{"Anchors the start of the line to a point of a region.",
                                         kSetPointArgs, {}};
const Game::Doc::Function kSetEndPoint{"Anchors the end of the line to a point of a region.",
                                       kSetPointArgs, {}};
const Game::Doc::Function kGetStartPoint{"Returns the anchor of the start of the line, or nothing.", {},
                                         kGetPointRets};
const Game::Doc::Function kGetEndPoint{"Returns the anchor of the end of the line, or nothing.", {},
                                       kGetPointRets};

const Game::Doc::Field kThicknessArgs[] = {
    Game::Doc::Req("thickness", "uiUnit"),
};
const Game::Doc::Field kThicknessRets[] = {
    Game::Doc::Req("thickness", "uiUnit"),
};
const Game::Doc::Function kSetThickness{"Sets the width of the line.", kThicknessArgs, {}};
const Game::Doc::Function kGetThickness{"Returns the width of the line.", {}, kThicknessRets};
const Game::Doc::Function kSetHitRectThickness{
    "Sets how far the mouse area of the line extends past its width.", kThicknessArgs, {}};
const Game::Doc::Function kGetHitRectThickness{
    "Returns how far the mouse area of the line extends past its width.", {}, kThicknessRets};
const Game::Doc::Function kClearAllPoints{"Removes the anchors of both ends of the line.", {}, {}};

const Game::Doc::Field kIsMouseOverRets[] = {
    Game::Doc::Req("isMouseOver", "bool"),
};
const Game::Doc::Function kIsMouseOver{"Returns true when the cursor is over the line.", {},
                                       kIsMouseOverRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetStartPoint", &kSetStartPoint},
    {"SetEndPoint", &kSetEndPoint},
    {"GetStartPoint", &kGetStartPoint},
    {"GetEndPoint", &kGetEndPoint},
    {"SetThickness", &kSetThickness},
    {"GetThickness", &kGetThickness},
    {"SetHitRectThickness", &kSetHitRectThickness},
    {"GetHitRectThickness", &kGetHitRectThickness},
    {"ClearAllPoints", &kClearAllPoints},
    {"IsMouseOver", &kIsMouseOver},
};

const Game::Doc::Field kCreateLineArgs[] = {
    Game::Doc::Opt("name", "cstring"),
    Game::Doc::Opt("drawLayer", "DrawLayer"),
    Game::Doc::Opt("templateName", "cstring"),
    Game::Doc::Opt("subLevel", "number", "0", "From -8 to 7."),
};
const Game::Doc::Field kCreateLineRets[] = {
    Game::Doc::Req("line", "SimpleLine"),
};
const Game::Doc::Function kCreateLine{"Creates a line on the frame.", kCreateLineArgs, kCreateLineRets};

const Game::Doc::Method g_frameMethodDocs[] = {
    {"CreateLine", &kCreateLine},
};

void RegisterLuaFunctions() {
    Api::Documentation::NameWidgetSystem(g_hash.Registry(), "SimpleLineAPI");
    Game::Lua::RegisterFrameMethods(g_hash.Registry(), g_methods, g_methodDocs);
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_frameMethods,
                                    g_frameMethodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool Is(const void *region) {
    return region != nullptr && g_vtable.Built() && Game::Read<const void *>(region, 0) == g_vtable.slots;
}

void LoadFromXml(void *region, const void *node, void *status) {
    State *s = Make(region);
    if (s == nullptr)
        return;
    LoadWithTemplates(region, *s, node, status, 0);
    ApplyDefaults(region, *s);
}

void Rebuild(void *region) {
    const State *s = Find(region);
    if (s == nullptr)
        return;
    // While a position animation plays, each update draws it on top of the
    // corners it stored, so the new quad goes there.
    if (float *base = Animation::Region::TextureBaseCorners(region))
        WriteQuad(region, *s, base);
    else
        Texture::CornerStore::StoreFromRect(region); // the listener above writes the quad
}

bool TileSize(const void *region, float &width, float &height) {
    const State *s = Find(region);
    if (s == nullptr)
        return false;
    height = static_cast<float>(Game::InternalToPixel(s->thickness));
    width = 0.0f;
    Segment seg;
    if (GetSegment(*s, false, seg)) {
        const float dx = seg.ex - seg.sx;
        const float dy = seg.ey - seg.sy;
        width = static_cast<float>(Game::InternalToPixel(std::sqrt(dx * dx + dy * dy)));
    }
    return true;
}

} // namespace Texture::Line
