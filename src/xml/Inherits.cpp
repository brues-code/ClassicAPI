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

// `inherits="A, B"` on XML frames and in CreateFrame's template argument.
//
// 4.3.4 reads `inherits` as a list: CSimpleFrame::LoadXML (0x00506BA0)
// tokenizes it with SStrTokenize on " ," and loads each named template, in
// order, through the frame's own virtual LoadXML before the node's own
// content, so later templates override earlier ones; FindParent (0x004ECD80)
// walks the same list for a `parent`. Vanilla has the same three steps, each
// resolving the whole string as ONE name through FUN_XML_TEMPLATE_LOOKUP, so
// a list is "Couldn't find inherited node":
//
//   content   FUN_XML_FRAME_LOAD (0x00769820) — inherits first, through the
//             object's node loader `(*self)->vtable[+8]` (the derived type's
//             loader, which calls this one first), then the node's attributes,
//             Layers, Scripts, ...
//   children  FUN_XML_FRAME_LOAD_CHILD_FRAMES (0x0076A060) — recurses the
//             inherited node, then builds each of the node's <Frames>
//   parent    FUN_XML_FRAME_BUILD (0x006EE280) — `parent`, else the inherits
//             chain until a template has one
//
// Each step is extended the same way: names in list order through the
// engine's own step, then the node itself. A single name (no separator)
// takes the engine's path untouched.
//
//   content   each name through `(*self)->vtable[+8]`, a missing one logged
//             with the loader's own warning; then the original loader with
//             the node's `inherits` value swapped to "" for the call, so it
//             loads only the node's own content. No Lua runs inside the
//             content loader (child frames and OnLoad come later, from the
//             builder's finalize), so nothing can observe the swap.
//   children  each name through this hook (so a template's own list
//             recurses), then the node's <Frames> through the builder, as the
//             engine's loop does (0x0076A097..BF). The loop is called
//             directly instead of swapping `inherits`, because each child's
//             OnLoad runs Lua inside it, and a CreateFrame there with this
//             template must still see the whole list.
//   parent    4.3.4's FindParent: the first name, in list order, whose chain
//             has a `parent`. When the walk met a list, the hook resolves the
//             name exactly as the builder does and passes the frame in EDX;
//             the builder's own walk stops at the same list (its lookup of
//             the whole string finds nothing) and keeps EDX.
//
// Textures and font strings keep one template, as in 4.3.4.
//
// Each listed template is applied under a `Gc::Tag` pin on its owner, as
// 3.3.5's LoadXML / LoadXMLInheritance set `lua_gctag` per template;
// `Xml::TemplateTag` pins the single-name engine path the same way.

#include "xml/Inherits.h"

#include "Game.h"
#include "Offsets.h"
#include "gc/Tag.h"
#include "xml/FrameLoad.h"

#include <cstring>

namespace Xml::Inherits {

namespace {

using Tokenize_t = void(__stdcall *)(const char **cursor, char *out, unsigned outSize,
                                     const char *delims, int *outQuoted);
using ChildFrames_t = void(__fastcall *)(void *frame, void *edx, const void *node, void *status);
using Build_t = void *(__fastcall *)(const void *node, void *parent, void *status);
using GetChild_t = const void *(__thiscall *)(const void *node, const char *tag);
using ObjectByName_t = void *(__fastcall *)(const char *name, int typeId);

constexpr const char *kSeparators = " ,";

ChildFrames_t g_origChildFrames = nullptr;
Build_t g_origBuild = nullptr;

// The node's first `inherits` value swapped to "" for the guard's lifetime —
// the pair the attribute getter returns, compared the way it compares.
class SuppressInherits {
public:
    explicit SuppressInherits(const void *node) {
        const int count = Game::Read<int>(node, Offsets::OFF_XML_NODE_ATTR_COUNT);
        auto *attrs = Game::Read<uint8_t *>(node, Offsets::OFF_XML_NODE_ATTRS);
        for (int i = 0; i < count; ++i) {
            uint8_t *pair = attrs + i * Offsets::XML_NODE_ATTR_STRIDE;
            if (Game::StrCmpI(*reinterpret_cast<const char **>(pair), "inherits") == 0) {
                m_value = reinterpret_cast<const char **>(pair + sizeof(const char *));
                m_saved = *m_value;
                *m_value = "";
                return;
            }
        }
    }
    ~SuppressInherits() {
        if (m_value != nullptr)
            *m_value = m_saved;
    }
    SuppressInherits(const SuppressInherits &) = delete;
    SuppressInherits &operator=(const SuppressInherits &) = delete;

private:
    const char **m_value = nullptr;
    const char *m_saved = nullptr;
};

// ---- children --------------------------------------------------------------

void __fastcall ChildFrames_h(void *frame, void *edx, const void *node, void *status) {
    const char *inherits = node != nullptr ? Attr(node, "inherits") : nullptr;
    if (inherits == nullptr || !IsList(inherits)) {
        g_origChildFrames(frame, edx, node, status);
        return;
    }
    ForEach(inherits, [&](const char *name) {
        if (const void *t = Template(name)) {
            Gc::Tag::PinScope pin(TemplateContext(name));
            ChildFrames_h(frame, edx, t, status);
        }
    });
    const void *frames = reinterpret_cast<GetChild_t>(Offsets::FUN_XML_NODE_GET_CHILD)(node, "Frames");
    if (frames == nullptr)
        return;
    auto build = reinterpret_cast<Build_t>(Offsets::FUN_XML_FRAME_BUILD);
    for (const void *c = FirstChild(frames); c != nullptr; c = NextSibling(c))
        build(c, frame, status);
}

// ---- parent ----------------------------------------------------------------

// 4.3.4's FindParent: the node's `parent` (even ""), else the first name in
// `inherits`, in list order, whose chain has one. Sets `*sawList` when the
// walk met a list, where vanilla's single-name walk stops.
const char *FindParent(const void *node, bool *sawList) {
    if (const char *parent = Attr(node, "parent"))
        return parent;
    const char *inherits = Attr(node, "inherits");
    if (inherits == nullptr || *inherits == '\0')
        return nullptr;
    if (!IsList(inherits)) {
        const void *t = Template(inherits);
        return t != nullptr ? FindParent(t, sawList) : nullptr;
    }
    *sawList = true;
    const char *found = nullptr;
    ForEach(inherits, [&](const char *name) {
        if (found != nullptr)
            return;
        if (const void *t = Template(name))
            found = FindParent(t, sawList);
    });
    return found;
}

void *__fastcall Build_h(const void *node, void *parent, void *status) {
    if (node != nullptr && Attr(node, "parent") == nullptr) {
        bool sawList = false;
        const char *name = FindParent(node, &sawList);
        if (sawList && name != nullptr && *name != '\0') {
            parent = reinterpret_cast<ObjectByName_t>(Offsets::FUN_FRAMESCRIPT_OBJECT_BY_NAME)(
                name, Game::Lua::FrameScriptTypeId(Offsets::VAR_FRAME_LUA_TYPE_ID));
            if (parent == nullptr)
                Warn(status, reinterpret_cast<const char *>(Offsets::STR_XML_NO_FRAME_PARENT), name);
        }
    }
    return g_origBuild(node, parent, status);
}

const Game::HookAutoRegister _childFramesHook{
    Offsets::FUN_XML_FRAME_LOAD_CHILD_FRAMES, reinterpret_cast<void *>(&ChildFrames_h),
    reinterpret_cast<void **>(&g_origChildFrames)};

const Game::HookAutoRegister _buildHook{
    Offsets::FUN_XML_FRAME_BUILD, reinterpret_cast<void *>(&Build_h),
    reinterpret_cast<void **>(&g_origBuild)};

} // namespace

bool IsList(const char *inherits) {
    return std::strpbrk(inherits, kSeparators) != nullptr;
}

void NextName(const char **cursor, char *out) {
    reinterpret_cast<Tokenize_t>(Offsets::FUN_STORM_STR_TOKENIZE)(cursor, out, kTokenSize,
                                                                    kSeparators, nullptr);
}

// ---- content ---------------------------------------------------------------

uintptr_t LoadFrame(Loader original, void *self, void *edx, const void *node, void *status) {
    const char *inherits = node != nullptr ? Attr(node, "inherits") : nullptr;
    if (inherits == nullptr || !IsList(inherits))
        return original(self, edx, node, status);
    void *object = static_cast<uint8_t *>(self) - Offsets::OFF_XML_LOADER_SUBOBJECT;
    ForEach(inherits, [&](const char *name) {
        if (const void *t = Template(name)) {
            Gc::Tag::PinScope pin(TemplateContext(name));
            LoadNode(object, t, status);
        } else
            Warn(status, reinterpret_cast<const char *>(Offsets::STR_XML_NO_INHERITED_NODE), name);
    });
    SuppressInherits suppress(node);
    return original(self, edx, node, status);
}

} // namespace Xml::Inherits
