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

// `intrinsic="true"`: a top-level frame element that defines a new frame
// type instead of a frame.
//
//   <Frame name="EventFrame" mixin="EventFrameMixin" intrinsic="true">
//
// `<EventFrame>` in XML and CreateFrame("EventFrame") then build a frame of
// the element's own type with the definition applied before anything else:
// its mixins first, then its content, then its child frames, then the
// templates and the element's own. Its <Scripts> become the frame's
// intrinsic bindings (Frame::ScriptBindings), so the frame keeps them
// whatever handlers it is given. On retail the frame reports its base type
// (GetObjectType "Frame", IsObjectType("EventFrame") false), and a
// definition may not inherit; the warnings are retail's.
//
// The builder (Xml::Inherits' hook) hands a definition node here instead of
// building it. It is kept the way the template registrar FUN_006EE500 keeps
// a virtual node: detached from its document (FUN_XML_NODE_DETACH), freed
// with FUN_XML_NODE_DTOR. Its name is registered as a frame type with the
// engine's FUN_FRAMEXML_REGISTER_FACTORY, the table the builder looks every
// tag up in, so both the XML and CreateFrame paths reach one factory. That
// factory is handed only the parent; the node being built comes from the
// builder's hook, and the factory calls the base type's own.
//
// A definition is applied once per frame, on the outermost load of the frame:
// the frame loader (Xml::FrameLoad), the child-frames step (Xml::Inherits) and
// the mixin walk (Xml::Mixin) each ask ForChain for it.
//
// The engine's frame-type table lasts the process, so a name registers once;
// the definitions are dropped with the Lua state and loaded again.

#include "xml/Intrinsics.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/ScriptBindings.h"
#include "xml/FrameLoad.h"
#include "xml/Inherits.h"

#include <cstdint>
#include <vector>

namespace Xml::Intrinsics {

namespace {

using Factory_t = void *(__fastcall *)(void *parent);
using RegisterFactory_t = int(__fastcall *)(const char *type, Factory_t factory, int unique);
using Hash_t = uint32_t(__stdcall *)(const char *s);
using NodeOp_t = void(__fastcall *)(void *node);
using SMemFree_t = void(__stdcall *)(void *ptr, const char *file, int line, int flags);

struct Def {
    const char *name; // the node's own `name` attribute
    void *node;
};

std::vector<Def> g_defs;
std::vector<const void *> g_building; // nodes the builder is building, innermost last
std::vector<void *> g_loading;        // objects being loaded, outermost first

// Deeper than any real chain; stops a template that inherits itself.
constexpr int kMaxChainDepth = 32;

const Def *Find(const char *name) {
    if (name == nullptr)
        return nullptr;
    for (const Def &d : g_defs)
        if (Game::StrCmpI(d.name, name) == 0)
            return &d;
    return nullptr;
}

// FUN_XML_FRAME_BUILD's lookup of a tag in the frame-type table.
Factory_t FactoryFor(const char *type) {
    const uint32_t mask = Game::Read<uint32_t>(Offsets::VAR_FRAME_FACTORY_MASK);
    const uint8_t *base = Game::Read<const uint8_t *>(Offsets::VAR_FRAME_FACTORY_TABLE);
    if (type == nullptr || mask == 0xFFFFFFFFu || base == nullptr)
        return nullptr;
    const uint32_t hash = reinterpret_cast<Hash_t>(Offsets::FUN_SSTR_HASH_HT)(type);
    const uint8_t *bucket = base + (hash & mask) * Offsets::XML_TEMPLATE_BUCKET_STRIDE;
    const int linkOff = Game::Read<int>(bucket, Offsets::OFF_XML_TEMPLATE_BUCKET_LINKOFF);
    for (const uint8_t *node = Game::Read<const uint8_t *>(bucket, Offsets::OFF_XML_TEMPLATE_BUCKET_HEAD);
         node != nullptr && (reinterpret_cast<uintptr_t>(node) & 1) == 0;
         node = Game::Read<const uint8_t *>(node, linkOff + 4)) {
        if (Game::Read<uint32_t>(node, Offsets::OFF_FRAME_FACTORY_NODE_HASH) == hash &&
            Game::StrCmpI(Game::Read<const char *>(node, Offsets::OFF_FRAME_FACTORY_NODE_NAME), type) == 0)
            return Game::Read<Factory_t>(node, Offsets::OFF_FRAME_FACTORY_NODE_FN);
    }
    return nullptr;
}

// Every intrinsic name's factory: the base type's, for the node being built.
void *__fastcall Factory(void *parent) {
    if (g_building.empty())
        return nullptr;
    const Def *d = Find(Tag(g_building.back()));
    if (d == nullptr)
        return nullptr;
    const Factory_t base = FactoryFor(Tag(d->node));
    if (base == nullptr || base == &Factory)
        return nullptr;
    return base(parent);
}

void Walk(const void *node, void *status, int depth, const Def **found) {
    if (depth > kMaxChainDepth)
        return;
    if (const Def *d = Find(Tag(node))) {
        if (*found == nullptr)
            *found = d;
        else if (d != *found)
            Warn(status, "Multiply inherited intrinsic node: %s", d->name);
    }
    const char *inherits = Attr(node, "inherits");
    if (inherits == nullptr || *inherits == '\0')
        return;
    Inherits::ForEach(inherits, [&](const char *name) {
        if (const void *t = Template(name))
            Walk(t, status, depth + 1, found);
    });
}

// The definitions hold nodes of the documents the Lua state loaded.
void PrepareForReload() {
    for (const Def &d : g_defs) {
        reinterpret_cast<NodeOp_t>(Offsets::FUN_XML_NODE_DTOR)(d.node);
        reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE)(d.node, __FILE__, __LINE__, 0);
    }
    g_defs.clear();
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

bool Define(const void *node, void *status) {
    const char *intrinsic = Attr(node, "intrinsic");
    if (intrinsic == nullptr || !Game::StringToFlag(intrinsic))
        return false;
    const void *parent = Game::Read<const void *>(node, Offsets::OFF_XML_NODE_PARENT);
    if (parent == nullptr || !TagIs(parent, "Ui"))
        return false;
    const char *name = Attr(node, "name");
    if (name == nullptr || *name == '\0') {
        Warn(status, "Unnamed intrinsic node", nullptr);
        return true;
    }
    if (const char *inherits = Attr(node, "inherits"); inherits != nullptr && *inherits != '\0') {
        Warn(status, "Intrinsic node %s cannot inherit from other nodes", name);
        return true;
    }
    if (Template(name) != nullptr) {
        Warn(status, "Intrinsic object named %s already exists as an virtual", name);
        return true;
    }
    if (Find(name) != nullptr)
        return true;
    reinterpret_cast<RegisterFactory_t>(Offsets::FUN_FRAMEXML_REGISTER_FACTORY)(name, &Factory, 0);
    if (FactoryFor(name) != &Factory)
        return true; // a frame type of the engine's own
    void *kept = const_cast<void *>(node);
    reinterpret_cast<NodeOp_t>(Offsets::FUN_XML_NODE_DETACH)(kept);
    g_defs.push_back({Attr(kept, "name"), kept});
    return true;
}

BuildScope::BuildScope(const void *node) { g_building.push_back(node); }

BuildScope::~BuildScope() { g_building.pop_back(); }

const void *ForChain(const void *node, void *status) {
    if (g_defs.empty() || node == nullptr)
        return nullptr;
    const Def *found = nullptr;
    Walk(node, status, 0, &found);
    return found != nullptr ? found->node : nullptr;
}

LoadScope::LoadScope(void *object, const void *node, void *status) {
    bool outer = true;
    for (void *o : g_loading)
        if (o == object)
            outer = false;
    g_loading.push_back(object);
    if (!outer)
        return;
    if (const void *def = ForChain(node, status)) {
        LoadNode(object, def, status);
        Frame::ScriptBindings::CaptureIntrinsic(object, def);
    }
}

LoadScope::~LoadScope() { g_loading.pop_back(); }

} // namespace Xml::Intrinsics
