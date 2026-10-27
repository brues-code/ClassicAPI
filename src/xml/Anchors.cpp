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

// Later-client <Anchor> attributes:
//
//   <Anchor point="LEFT" x="4" y="-2"/>                     offsets as attributes
//   <Anchor point="TOPLEFT" relativeKey="$parent.Icon"/>    target by key path
//
// The engine's anchor loader (0x00767800, the LayoutFrame XML load) reads
// `point`, `relativePoint`, `relativeTo` and an <Offset> child only. It sets
// an anchor with neither of the above against the default target (the parent)
// with no offset, so Apply re-sets that same point afterwards:
//   - the target is the `relativeKey` path when there is one, else the target
//     the engine's own anchor node resolved (`relativeTo`, or the parent);
//   - the offsets come from XmlReadDimension on the <Anchor> itself when it
//     has `x` or `y`, else on its <Offset> child — the reader and conversion
//     the loader uses for <Offset>;
//   - the point is set through the engine's setter (FUN_REGION_SET_POINT).
//
// `relativeKey` is a dot path: `$parent` steps to the current object's parent
// (OFF_REGION_PARENT), any other part indexes the current object's Lua table,
// and a leading part that is not `$parent` is a global. Callers run Apply from
// the post-load observers in ParentKey.cpp, where earlier siblings' parentKeys
// are bound. A path may also name something built later: a frame loads its
// layers before its child <Frames>, so a layer anchored to `$parent.Key`, a
// child frame, cannot resolve yet. Such an anchor is queued and retried when
// a frame's children are built (RetryUnder, before its OnLoad) and once more
// when the outermost frame build returns (Flush), which reports what is left.

#include "xml/Anchors.h"

#include "Game.h"
#include "Offsets.h"
#include "xml/FrameLoad.h"

#include <cstring>
#include <string>
#include <vector>

namespace Xml::Anchors {

namespace {

using StringToPoint_t = int(__fastcall *)(const char *name, int *point);
using ReadDimension_t = int(__fastcall *)(const void *node, float *x, float *y, void *status);
using SetAttribute_t = void(__thiscall *)(void *node, const char *name, const char *value);
using GetRelative_t = void *(__thiscall *)(void *anchorNode);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);

bool HasValue(const char *s) { return s != nullptr && *s != '\0'; }

struct Pending {
    void *object;
    const void *anchor;
};
std::vector<Pending> g_pending;

const void *FindChild(const void *node, const char *tag) {
    for (const void *c = FirstChild(node); c != nullptr; c = NextSibling(c))
        if (TagIs(c, tag))
            return c;
    return nullptr;
}

// The object `path` names, starting from `object`, or nullptr.
void *ResolveKeyPath(void *object, const char *path) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return nullptr;
    const int top = Game::Lua::GetTop(L);
    void *current = object;
    bool first = true;
    for (const char *p = path; current != nullptr;) {
        const char *dot = std::strchr(p, '.');
        const std::string part = dot != nullptr ? std::string(p, dot) : std::string(p);
        if (Game::StrCmpI(part.c_str(), "$parent") == 0) {
            current = Game::Read<void *>(current, Offsets::OFF_REGION_PARENT);
        } else {
            if (first)
                Game::Lua::PushValue(L, Game::Lua::GLOBALS_INDEX);
            else if (!PushWrapper(L, current))
                current = nullptr;
            if (current != nullptr) {
                Game::Lua::PushString(L, part.c_str());
                Game::Lua::GetTable(L, -2);
                current = Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE
                              ? Game::Lua::ResolveObject(L, -1)
                              : nullptr;
            }
            Game::Lua::SetTop(L, top);
        }
        first = false;
        if (dot == nullptr)
            break;
        p = dot + 1;
    }
    Game::Lua::SetTop(L, top);
    return current;
}

// The relative target the engine's loader gave `point` (its anchor node's
// LayoutFrame inner, the setter's relAnchor), or nullptr when the point is
// unset or has no target.
void *EngineTarget(void *object, int point) {
    void *anchorNode = Game::Read<void *>(
        object, Offsets::OFF_REGION_ANCHOR_ARRAY + point * static_cast<int>(sizeof(void *)));
    if (anchorNode == nullptr)
        return nullptr;
    const void *vtable = Game::Read<const void *>(anchorNode, 0);
    return Game::Read<GetRelative_t>(vtable, Offsets::VTBL_ANCHOR_NODE_GET_RELATIVE)(anchorNode);
}

// Sets one anchor. False only when its relativeKey does not resolve (yet).
bool ApplyAnchor(void *object, const void *anchor, void *status) {
    const char *relativeKey = Xml::Attr(anchor, "relativeKey");
    const char *xs = Xml::Attr(anchor, "x");
    const char *ys = Xml::Attr(anchor, "y");
    const bool inlineOffset = HasValue(xs) || HasValue(ys);
    if (!HasValue(relativeKey) && !inlineOffset)
        return true;

    auto toPoint = reinterpret_cast<StringToPoint_t>(Offsets::FUN_STRING_TO_FRAME_POINT);
    int point = 0;
    const char *pointName = Xml::Attr(anchor, "point");
    if (!HasValue(pointName) || !toPoint(pointName, &point))
        return true; // the engine's loader has reported it
    int relativePoint = point;
    if (const char *rp = Xml::Attr(anchor, "relativePoint"); HasValue(rp) && !toPoint(rp, &relativePoint))
        return true;

    void *relAnchor = nullptr;
    if (HasValue(relativeKey)) {
        void *target = ResolveKeyPath(object, relativeKey);
        if (target == nullptr || target == object)
            return false;
        relAnchor = Game::Ptr<void>(target, Offsets::OFF_REGION_ANCHOR);
    } else {
        relAnchor = EngineTarget(object, point);
        if (relAnchor == nullptr)
            return true;
    }

    // An attribute the anchor leaves out is 0; giving it "0" keeps the reader
    // from reporting a missing dimension when the other one is present.
    float x = 0.0f, y = 0.0f;
    const void *dimension = inlineOffset ? anchor : FindChild(anchor, "Offset");
    if (dimension != nullptr) {
        if (inlineOffset) {
            auto setAttr = reinterpret_cast<SetAttribute_t>(Offsets::FUN_XML_NODE_SET_ATTRIBUTE);
            if (!HasValue(xs))
                setAttr(const_cast<void *>(anchor), "x", "0");
            if (!HasValue(ys))
                setAttr(const_cast<void *>(anchor), "y", "0");
        }
        reinterpret_cast<ReadDimension_t>(Offsets::FUN_XML_READ_DIMENSION)(dimension, &x, &y, status);
    }

    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(
        Game::Ptr<void>(object, Offsets::OFF_REGION_ANCHOR), point, relAnchor, relativePoint, x,
        y, 1);
    return true;
}

bool IsUnder(void *object, void *root) {
    for (void *o = object; o != nullptr; o = Game::Read<void *>(o, Offsets::OFF_REGION_PARENT))
        if (o == root)
            return true;
    return false;
}

} // namespace

void Apply(void *object, const void *node, void *status) {
    if (object == nullptr || node == nullptr)
        return;
    const void *anchors = FindChild(node, "Anchors");
    if (anchors == nullptr)
        return;
    for (const void *a = FirstChild(anchors); a != nullptr; a = NextSibling(a))
        if (TagIs(a, "Anchor") && !ApplyAnchor(object, a, status))
            g_pending.push_back({object, a});
}

void RetryUnder(void *root, void *status) {
    for (size_t i = 0; i < g_pending.size();) {
        const Pending p = g_pending[i];
        if (IsUnder(p.object, root) && ApplyAnchor(p.object, p.anchor, status))
            g_pending.erase(g_pending.begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
}

void Flush(void *status) {
    for (const Pending &p : g_pending)
        if (!ApplyAnchor(p.object, p.anchor, status))
            Xml::Warn(status, "Couldn't find relativeKey: %s", Xml::Attr(p.anchor, "relativeKey"));
    g_pending.clear();
}

} // namespace Xml::Anchors
