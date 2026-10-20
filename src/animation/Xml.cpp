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
// it is named "<object>:<script>" and handled like a frame's.

#include "animation/Xml.h"

#include "animation/Classes.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "xml/FrameLoad.h"
#include "xml/KeyValues.h"
#include "xml/Mixin.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace Animation::Xml {

namespace {

using SetScriptText_t = bool(__fastcall *)(void *object, void *edx, const char *name, const char *text);
using StringToPoint_t = int(__fastcall *)(const char *name, int *point);
using LuaRef_t = int(__fastcall *)(void *L, int t);

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
            reinterpret_cast<void(__stdcall *)(uint32_t *)>(Offsets::FUN_SCRIPT_RELEASE)(slot);
            slot[0] = static_cast<uint32_t>(reinterpret_cast<LuaRef_t>(Offsets::LUA_REF_REF)(L, Game::Lua::REGISTRY_INDEX));
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

// LoadXML_AnimOrigin: <Origin point="..."><Offset x= y=/></Origin>.
void LoadOrigin(Anim *anim, const void *node) {
    const void *origin = FindChild(node, "Origin");
    if (origin == nullptr)
        return;
    int point = Offsets::FRAMEPOINT_CENTER;
    float x = 0.0f, y = 0.0f;
    const char *pointName = ::Xml::Attr(origin, "point");
    if (pointName != nullptr &&
        reinterpret_cast<StringToPoint_t>(Offsets::FUN_STRING_TO_FRAME_POINT)(pointName, &point)) {
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
        // 4.3.4 knows Animation, Translation, Rotation, Scale, Path and Alpha.
        if (!::Xml::TagIs(child, "Animation") && !::Xml::TagIs(child, "Translation") &&
            !::Xml::TagIs(child, "Rotation") && !::Xml::TagIs(child, "Scale") && !::Xml::TagIs(child, "Alpha") &&
            !::Xml::TagIs(child, "Path"))
            continue;
        Anim *anim = new Anim(group, Anim::KindOf(tag), ::Xml::Attr(child, "name"));
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
    if (const char *looping = ::Xml::Attr(node, "looping"); looping != nullptr && *looping != '\0') {
        LoopType type;
        if (StringToLoopType(looping, type))
            group->loopType = type;
        else
            ::Xml::Warn(status, "AnimationGroup %s: Invalid looping value: %s", group->DisplayName(), looping);
    }
    float x = 0.0f, y = 0.0f;
    FloatAttr(node, "initialOffsetX", x);
    FloatAttr(node, "initialOffsetY", y);
    x = Game::PixelToInternal(x);
    y = Game::PixelToInternal(y);
    if (x * x + y * y <= 0.00000023841858f) {
        group->initialOffsetX = 0.0f;
        group->initialOffsetY = 0.0f;
    } else {
        group->initialOffsetX = x;
        group->initialOffsetY = y;
    }
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
        break; // CSimpleAlphaAnim::LoadXML reads `change` after the base.
    case KIND_TRANSLATION: {
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
    case KIND_SCALE: {
        float sx = 1.0f, sy = 1.0f;
        if (FloatAttr(node, "scaleX", sx) && sx < 0.001f)
            ::Xml::Warn(status, "%s: Invalid scaleX value: %s. Value must be at least 0.001.", anim->DisplayName(),
                        ::Xml::Attr(node, "scaleX"));
        if (FloatAttr(node, "scaleY", sy) && sy < 0.001f)
            ::Xml::Warn(status, "%s: Invalid scaleY value: %s. Value must be at least 0.001.", anim->DisplayName(),
                        ::Xml::Attr(node, "scaleY"));
        anim->scaleChangeX = 1.0f - (sx >= 0.001f ? sx : 0.001f);
        anim->scaleChangeY = 1.0f - (sy >= 0.001f ? sy : 0.001f);
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
    if (const char *order = ::Xml::Attr(node, "order"); order != nullptr && *order != '\0') {
        const int value = std::atoi(order) - 1;
        if (value < 0 || value > 99)
            ::Xml::Warn(status, "%s: Invalid order value: %s. Order must be between 1 and 100.", anim->DisplayName(),
                        order);
        anim->order = value < 0 ? 0 : (value > 99 ? 99 : value);
    }
    LoadScripts(anim, node);
    LoadKeyValues(anim, node, status);

    // The subclasses' reads after the base.
    // CSimpleAlphaAnim::LoadXML sets the change, 0 when it is missing.
    if (anim->kind == KIND_ALPHA) {
        x = 0.0f;
        if (FloatAttr(node, "change", x) && (x < -1.0f || x > 1.0f)) {
            ::Xml::Warn(status, "%s: Invalid change value: %s. Value must be between -1 and 1, inclusive.",
                        anim->DisplayName(), ::Xml::Attr(node, "change"));
            x = 0.0f;
        }
        anim->change = static_cast<int16_t>((x < -1.0f ? -1.0f : (x >= 1.0f ? 1.0f : x)) * 255.0f);
    }
    if (anim->kind == KIND_SCALE || anim->kind == KIND_ROTATION)
        LoadOrigin(anim, node);
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
