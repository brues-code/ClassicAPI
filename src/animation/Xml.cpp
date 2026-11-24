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

// 4.3.4's animation XML (CScriptRegion::LoadXML_Animations,
// CSimpleAnimGroup::LoadXML, CSimpleAnim::LoadXML and the subclasses',
// LoadXML_AnimOrigin; the attributes BinaryXML.cpp parses), on 1.12 nodes.
// A script body compiles through the engine's own SetScriptObjectScript, so
// it is named "*<file>:<line>_<script>" (Xml::ScriptChunkName) and handled
// like a frame's.
//
// Later clients' attributes go to the same fields their methods set:
// setToFinalAlpha and parentArray on a group; targetKey and childKey on any animation;
// fromAlpha / toAlpha on Alpha; fromScaleX / fromScaleY / toScaleX /
// toScaleY on Scale. Each applies only when present and non-empty.

#include "animation/Xml.h"

#include "animation/Classes.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/Layout.h"
#include "frame/ScriptSlots.h"
#include "xml/FrameLoad.h"
#include "xml/KeyValues.h"
#include "xml/Mixin.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace Animation::Xml {

namespace {

using SetScriptText_t = bool(__fastcall *)(void *object, void *edx, const char *name, const char *text);

const void *FindChild(const void *node, const char *tag) {
    for (const void *c = ::Xml::FirstChild(node); c != nullptr; c = ::Xml::NextSibling(c))
        if (::Xml::TagIs(c, tag))
            return c;
    return nullptr;
}

bool FloatAttr(const void *node, const char *name, float &out) {
    const char *v = ::Xml::Attr(node, name);
    if (v == nullptr || *v == '\0')
        return false;
    out = static_cast<float>(std::atof(v));
    return true;
}

} // namespace

// object[parentKey] = child, as 4.3.4's LoadXML does for both.
void BindParentKey(void *object, Object *child, const char *key) {
    if (key == nullptr || *key == '\0')
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    if (::Xml::PushWrapper(L, object)) {
        Game::Lua::PushString(L, key);
        Push(L, child);
        Game::Lua::SetTable(L, -3);
    }
    Game::Lua::SetTop(L, top);
}

// Later clients' parentArray on a group: appended to object[key], as a
// frame or region is.
void BindParentArray(void *object, Object *child, const char *key) {
    if (key == nullptr || *key == '\0')
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    if (::Xml::PushWrapper(L, object)) {
        Push(L, child);
        ::Xml::AppendToArray(L, top + 1, key);
    }
    Game::Lua::SetTop(L, top);
}

// <Scripts>: each element's body, its `function` global, or its `method`
// field on the object.
void LoadScripts(Object *object, const void *node) {
    const void *scripts = FindChild(node, "Scripts");
    if (scripts == nullptr)
        return;
    for (const void *s = ::Xml::FirstChild(scripts); s != nullptr; s = ::Xml::NextSibling(s)) {
        const char *script = ::Xml::Tag(s);
        if (script == nullptr)
            continue;
        const char *function = ::Xml::Attr(s, "function");
        const char *method = ::Xml::Attr(s, "method");
        const bool byFunction = function != nullptr && *function != '\0';
        if (byFunction || (method != nullptr && *method != '\0')) {
            uint32_t *slot = object->ScriptSlot(script);
            void *L = Game::Lua::State();
            if (slot == nullptr || L == nullptr)
                continue;
            if (byFunction) {
                Game::Lua::PushString(L, function);
                Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
            } else {
                Push(L, object);
                Game::Lua::PushString(L, method);
                Game::Lua::GetTable(L, -2);
                Game::Lua::Remove(L, -2);
            }
            if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
                Game::Lua::SetTop(L, -2);
                continue;
            }
            Frame::ScriptSlots::Release(slot);
            slot[0] = static_cast<uint32_t>(Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX));
            slot[1] = 0;
            continue;
        }
        const char *body = Game::Read<const char *>(s, Offsets::OFF_XML_NODE_TEXT);
        if (body != nullptr && *body != '\0')
            reinterpret_cast<SetScriptText_t>(Offsets::FUN_SCRIPT_OBJECT_SET_SCRIPT_TEXT)(object, nullptr, script,
                                                                                         body);
    }
}

// <KeyValues> on a group or an animation, as on later clients' schema.
void LoadKeyValues(Object *object, const void *node, void *status) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Push(L, object);
    ::Xml::KeyValues::Apply(L, top + 1, node, status, object->DisplayName());
    Game::Lua::SetTop(L, top);
}

namespace {

// LoadXML_AnimOrigin: <Origin point="..."><Offset x= y=/></Origin>.
void LoadOrigin(Anim *anim, const void *node) {
    const void *origin = FindChild(node, "Origin");
    if (origin == nullptr)
        return;
    int point = Offsets::FRAMEPOINT_CENTER;
    float x = 0.0f, y = 0.0f;
    const char *pointName = ::Xml::Attr(origin, "point");
    if (pointName != nullptr &&
        Frame::Layout::StringToPoint(pointName, &point)) {
        if (const void *offset = FindChild(origin, "Offset")) {
            const void *abs = FindChild(offset, "AbsDimension");
            const void *dim = abs != nullptr ? abs : offset;
            FloatAttr(dim, "x", x);
            FloatAttr(dim, "y", y);
        }
    } else {
        point = Offsets::FRAMEPOINT_CENTER;
    }
    anim->originPoint = point;
    anim->originX = Game::PixelToInternal(x);
    anim->originY = Game::PixelToInternal(y);
}

void LoadAnimChildren(Group *group, const void *node, void *status) {
    for (const void *child = ::Xml::FirstChild(node); child != nullptr; child = ::Xml::NextSibling(child)) {
        const char *tag = ::Xml::Tag(child);
        if (tag == nullptr || ::Xml::TagIs(child, "Scripts"))
            continue;
        // 3.80.2's element switch (FUN_1405A65E0): 4.3.4's six and its five.
        static const char *const kTags[] = {"Animation",   "Translation",     "Rotation",
                                            "Scale",       "Alpha",           "Path",
                                            "VertexColor", "FlipBook",        "TextureCoordTranslation",
                                            "LineScale",   "LineTranslation"};
        bool known = false;
        for (const char *t : kTags)
            known = known || ::Xml::TagIs(child, t);
        if (!known)
            continue;
        Anim *anim = new Anim(group, Anim::KindOfTag(tag), ::Xml::Attr(child, "name"));
        anim->flags |= 1;
        LoadAnim(anim, child, status);
    }
}

} // namespace

const void *Template(const char *name) { return ::Xml::Template(name); }

namespace {

// LoadGroup re-enters itself for each inherited template; the mixins of the
// whole chain go in once, from the outermost call, before any node's
// KeyValues or scripts (see Xml::Mixin).
int g_groupDepth = 0;

void LoadGroupNode(Group *group, const void *node, void *status);

} // namespace

void LoadGroup(Group *group, const void *node, void *status) {
    if (g_groupDepth == 0) {
        if (void *L = Game::Lua::State()) {
            const int top = Game::Lua::GetTop(L);
            Push(L, group);
            ::Xml::Mixin::ApplyChain(L, top + 1, node, status, group->DisplayName(), false);
            Game::Lua::SetTop(L, top);
        }
    }
    ++g_groupDepth;
    LoadGroupNode(group, node, status);
    --g_groupDepth;
}

namespace {

void LoadGroupNode(Group *group, const void *node, void *status) {
    group->flags |= GROUP_LOADING;
    if (const char *inherits = ::Xml::Attr(node, "inherits"); inherits != nullptr && *inherits != '\0') {
        const void *base = ::Xml::Template(inherits);
        if (base == nullptr)
            ::Xml::Warn(status, "Couldn't find inherited node: %s", inherits);
        else
            LoadGroupNode(group, base, status);
    }
    BindParentKey(group->RegionObject(), group, ::Xml::Attr(node, "parentKey"));
    BindParentArray(group->RegionObject(), group, ::Xml::Attr(node, "parentArray"));
    if (const char *looping = ::Xml::Attr(node, "looping"); looping != nullptr && *looping != '\0') {
        LoopType type;
        if (StringToLoopType(looping, type))
            group->loopType = type;
        else
            ::Xml::Warn(status, "AnimationGroup %s: Invalid looping value: %s", group->DisplayName(), looping);
    }
    if (const char *keep = ::Xml::Attr(node, "setToFinalAlpha"); keep != nullptr && *keep != '\0')
        group->setToFinalAlpha = Game::StringToFlag(keep);
    LoadScripts(group, node);
    LoadKeyValues(group, node, status);
    LoadAnimChildren(group, node, status);
}

} // namespace

void LoadAnim(Anim *anim, const void *node, void *status) {
    // The subclass reads its values, then CSimpleAnim::LoadXML (inherits
    // first, then the shared attributes) - the order 4.3.4's overrides use.
    float x = 0.0f, y = 0.0f;
    switch (anim->kind) {
    case KIND_ALPHA:
        break; // the alpha is read after the base
    case KIND_TRANSLATION:
    case KIND_LINE_TRANSLATION: {
        // CSimpleTranslationAnim::LoadXML sets the offset, 0 for a missing axis.
        FloatAttr(node, "offsetX", x);
        FloatAttr(node, "offsetY", y);
        const float ox = Game::PixelToInternal(x), oy = Game::PixelToInternal(y);
        const bool zero = ox * ox + oy * oy <= 0.00000023841858f;
        anim->offsetX = zero ? 0.0f : ox;
        anim->offsetY = zero ? 0.0f : oy;
        break;
    }
    case KIND_ROTATION:
        if (FloatAttr(node, "degrees", x))
            anim->radians = x * 0.017453292f;
        if (FloatAttr(node, "radians", y))
            anim->radians = y;
        break;
    case KIND_TEXCOORD: {
        // 3.80.2's 0x1405B1BE0: both, 0 for a missing one, a near-zero pair 0.
        FloatAttr(node, "offsetU", x);
        FloatAttr(node, "offsetV", y);
        const bool zero = x * x + y * y <= 0.00000023841858f;
        anim->texOffsetU = zero ? 0.0f : x;
        anim->texOffsetV = zero ? 0.0f : y;
        break;
    }
    case KIND_FLIPBOOK: {
        // strtoul, so 0x is hex; a 0 grid is reported and not stored.
        const std::pair<const char *, uint32_t Anim::*> values[] = {{"flipBookRows", &Anim::flipRows},
                                                                    {"flipBookColumns", &Anim::flipColumns},
                                                                    {"flipBookFrames", &Anim::flipFrames},
                                                                    {"flipBookFrameWidth", &Anim::flipFrameWidth},
                                                                    {"flipBookFrameHeight", &Anim::flipFrameHeight}};
        for (const auto &[attr, field] : values) {
            const char *v = ::Xml::Attr(node, attr);
            if (v == nullptr || *v == 0)
                continue;
            const uint32_t value = static_cast<uint32_t>(std::strtoul(v, nullptr, 0));
            const bool grid = field == &Anim::flipRows || field == &Anim::flipColumns;
            if (grid && value == 0) {
                char message[256];
                std::snprintf(message, sizeof message, "%s: Invalid %s value: %u. Value must be greater than 0.",
                              anim->DisplayName(), attr, value);
                ::Xml::Warn(status, "%s", message);
                continue;
            }
            anim->*field = value;
        }
        break;
    }
    case KIND_SCALE:
    case KIND_LINE_SCALE: {
        float sx = 1.0f, sy = 1.0f;
        if (FloatAttr(node, "scaleX", sx) && sx < 0.001f)
            ::Xml::Warn(status, "%s: Invalid scaleX value: %s. Value must be at least 0.001.", anim->DisplayName(),
                        ::Xml::Attr(node, "scaleX"));
        if (FloatAttr(node, "scaleY", sy) && sy < 0.001f)
            ::Xml::Warn(status, "%s: Invalid scaleY value: %s. Value must be at least 0.001.", anim->DisplayName(),
                        ::Xml::Attr(node, "scaleY"));
        anim->scaleToX = sx >= 0.001f ? sx : 0.001f;
        anim->scaleToY = sy >= 0.001f ? sy : 0.001f;
        // Later clients' from and to scale, each axis when given; any of
        // them turns from / to on.
        const bool fx = FloatAttr(node, "fromScaleX", anim->scaleFromX);
        const bool fy = FloatAttr(node, "fromScaleY", anim->scaleFromY);
        const bool tx = FloatAttr(node, "toScaleX", anim->scaleToX);
        const bool ty = FloatAttr(node, "toScaleY", anim->scaleToY);
        if (fx || fy || tx || ty)
            anim->scaleFromTo = true;
        break;
    }
    default:
        break;
    }

    // CSimpleAnim::LoadXML.
    anim->flags |= 1;
    if (const char *inherits = ::Xml::Attr(node, "inherits"); inherits != nullptr && *inherits != '\0') {
        const void *base = ::Xml::Template(inherits);
        if (base == nullptr)
            ::Xml::Warn(status, "Couldn't find inherited node: %s", inherits);
        else
            LoadAnim(anim, base, status);
    }
    BindParentKey(anim->GetGroup(), anim, ::Xml::Attr(node, "parentKey"));
    if (const char *target = ::Xml::Attr(node, "target"); target != nullptr && *target != '\0') {
        const std::string name = CreateName(target, anim);
        if (name.size() + 1 > sizeof(anim->targetName))
            ::Xml::Warn(status, "target key too long: %s", name.c_str());
        std::snprintf(anim->targetName, sizeof(anim->targetName), "%s", name.c_str());
        anim->targetKey.clear();
    }
    // Later clients' key paths, looked up on Play like `target`.
    if (const char *key = ::Xml::Attr(node, "targetKey"); key != nullptr && *key != '\0') {
        anim->targetName[0] = '\0';
        anim->targetKey = key;
    }
    if (const char *key = ::Xml::Attr(node, "childKey"); key != nullptr && *key != '\0') {
        anim->targetName[0] = '\0';
        anim->targetKey = std::string("$parent.$parent.") + key;
    }
    if (FloatAttr(node, "startDelay", x))
        anim->startDelay = x < 0.0f ? 0.0f : x;
    if (FloatAttr(node, "endDelay", x))
        anim->endDelay = x < 0.0f ? 0.0f : x;
    if (FloatAttr(node, "duration", x))
        anim->duration = x;
    if (const char *smoothing = ::Xml::Attr(node, "smoothing"); smoothing != nullptr && *smoothing != '\0') {
        float in, out;
        if (StringToSmoothWeights(smoothing, in, out))
            anim->SetSmoothing(in, out);
        else
            ::Xml::Warn(status, "%s: Invalid smoothing value: %s", anim->DisplayName(), smoothing);
    }
    // 3.80.2's 0x1405B0ED0: the order as written, 0..99; its message names
    // 1 and 100 for the same check.
    if (const char *order = ::Xml::Attr(node, "order"); order != nullptr && *order != '\0') {
        const int value = std::atoi(order);
        if (value < 0 || value > 99) {
            char message[256];
            std::snprintf(message, sizeof message, "%s: Invalid order value: %d. Order must be between %d and %d.",
                          anim->DisplayName(), value, 1, 100);
            ::Xml::Warn(status, "%s", message);
        }
        anim->order = value < 0 ? 0 : (value > 99 ? 99 : value);
    }
    LoadScripts(anim, node);
    LoadKeyValues(anim, node, status);

    // The subclasses' reads after the base.
    // fromAlpha / toAlpha, 0..1, kept as bytes.
    if (anim->kind == KIND_ALPHA) {
        const std::pair<const char *, uint8_t Anim::*> ends[] = {{"fromAlpha", &Anim::fromAlpha},
                                                                 {"toAlpha", &Anim::toAlpha}};
        for (const auto &[attr, end] : ends) {
            if (!FloatAttr(node, attr, x))
                continue;
            if (x < 0.0f || x > 1.0f) {
                char message[256];
                std::snprintf(message, sizeof message,
                              "%s: Invalid %s value: %f. Value must be between %d and %d, inclusive.",
                              anim->DisplayName(), attr, x, 0, 1);
                ::Xml::Warn(status, "%s", message);
            }
            x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
            anim->*end = static_cast<uint8_t>(static_cast<int>(x * 255.0f));
            anim->alphaRelative = false;
        }
    }
    if (anim->kind == KIND_SCALE || anim->kind == KIND_LINE_SCALE || anim->kind == KIND_ROTATION)
        LoadOrigin(anim, node);
    // VertexColor: <StartColor> / <EndColor>, each a colour element.
    if (anim->kind == KIND_VERTEX_COLOR) {
        if (const void *c = FindChild(node, "StartColor"))
            ::Xml::ReadColor(c, anim->startColor, status);
        if (const void *c = FindChild(node, "EndColor"))
            ::Xml::ReadColor(c, anim->endColor, status);
    }
    // CSimplePathAnim::LoadXML: the curve, then <ControlPoints>.
    if (anim->kind == KIND_PATH) {
        if (const char *curve = ::Xml::Attr(node, "curve"); curve != nullptr && *curve != '\0') {
            if (Ascii::EqualCI(curve, "NONE"))
                anim->curve = false;
            else if (Ascii::EqualCI(curve, "SMOOTH"))
                anim->curve = true;
            else
                ::Xml::Warn(status, "Path %s: Invalid curve value: %s", anim->DisplayName(), curve);
        }
        if (const void *points = FindChild(node, "ControlPoints")) {
            for (const void *child = ::Xml::FirstChild(points); child != nullptr; child = ::Xml::NextSibling(child)) {
                if (!::Xml::TagIs(child, "ControlPoint")) {
                    ::Xml::Warn(status, "Path %s: Error loading Origin element", anim->DisplayName());
                    continue;
                }
                LoadControlPoint(new ControlPoint(anim, ::Xml::Attr(child, "name")), child, status);
            }
        }
    }
}

void LoadControlPoint(ControlPoint *point, const void *node, void *status) {
    // CSimpleControlPoint::LoadXML: inherits first, then parentKey (on the
    // path), then the offset - 0 on an axis the node does not give.
    if (const char *inherits = ::Xml::Attr(node, "inherits"); inherits != nullptr && *inherits != '\0') {
        const void *base = ::Xml::Template(inherits);
        if (base == nullptr)
            ::Xml::Warn(status, "Couldn't find inherited node: %s", inherits);
        else
            LoadControlPoint(point, base, status);
    }
    BindParentKey(point->GetPath(), point, ::Xml::Attr(node, "parentKey"));
    float x = 0.0f, y = 0.0f;
    FloatAttr(node, "offsetX", x);
    FloatAttr(node, "offsetY", y);
    point->SetOffset(Game::PixelToInternal(x), Game::PixelToInternal(y));
}

void LoadAnimations(void *region, const void *node, void *status) {
    const void *animations = FindChild(node, "Animations");
    if (animations == nullptr)
        return;
    for (const void *g = ::Xml::FirstChild(animations); g != nullptr; g = ::Xml::NextSibling(g)) {
        if (!::Xml::TagIs(g, "AnimationGroup"))
            continue;
        Group *group = new Group(region, ::Xml::Attr(g, "name"));
        LoadGroup(group, g, status);
        group->PostLoadXML();
    }
}

} // namespace Animation::Xml
