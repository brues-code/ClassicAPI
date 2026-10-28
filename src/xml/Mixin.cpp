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

// `mixin="A, B"` on frames, textures, font strings and animation groups, the
// later-client attribute: each name is a global table whose fields are
// copied onto the element's Lua table, as `Mixin(object, A, B)` does.
//
// ORDER. A mixin carries methods and default fields; <KeyValues> and the
// element's scripts build on them, so every mixin must be in place before
// any of those load. The engine's loaders load a template by re-entering the
// same loader for the template node (FUN_XML_FRAME_LOAD through
// Xml::Inherits, the texture and font string loaders through
// `(*self)->vtable[+8]` at their top), and the KeyValues for a template node
// are set when that inner load returns. So mixins cannot be applied per
// node: an element's own mixin would land after its template's KeyValues and
// overwrite their defaults. Instead the OUTERMOST load of an object walks
// the whole inherits chain once, before the engine runs, and mixes in each
// template's mixins (templates first, in inherits order) and then the
// element's own. Inner loads of the same object apply nothing. A frame
// whose chain builds on an intrinsic type (Xml::Intrinsics) takes the
// definition's mixins before all of them.
//
// A name that is not a global table is skipped with a load warning.

#include "xml/Mixin.h"

#include "Game.h"
#include "baselib/Mixin.h"
#include "xml/FrameLoad.h"
#include "xml/Inherits.h"
#include "xml/Intrinsics.h"

#include <vector>

namespace Xml::Mixin {

namespace {

// Objects whose load is in progress, outermost first. Nesting is the
// inherits chain plus anything a Before subscriber loads, so this stays a
// handful of entries deep.
std::vector<void *> g_loading;

// Deeper than any real chain; stops a template that inherits itself.
constexpr int kMaxChainDepth = 32;

void ApplyNode(void *L, int index, const void *node, void *status, const char *owner,
               bool listInherits, int depth) {
    if (depth > kMaxChainDepth)
        return;
    const char *inherits = Attr(node, "inherits");
    if (inherits != nullptr && *inherits != '\0') {
        const auto applyTemplate = [&](const char *name) {
            if (const void *t = Template(name))
                ApplyNode(L, index, t, status, owner, listInherits, depth + 1);
        };
        if (listInherits)
            Inherits::ForEach(inherits, applyTemplate);
        else
            applyTemplate(inherits);
    }

    const char *mixins = Attr(node, "mixin");
    if (mixins == nullptr || *mixins == '\0')
        return;
    Inherits::ForEach(mixins, [&](const char *name) {
        Game::Lua::PushString(L, name);
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX); // [mixin]
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
            BaseLib::Mixin::Into(L, index, Game::Lua::GetTop(L));
        else
            Warn(status, "%s: Couldn't find mixin %s", owner, name);
        Game::Lua::SetTop(L, -2);
    });
}

bool HasMixinInChain(const void *node, bool listInherits, int depth) {
    if (depth > kMaxChainDepth)
        return false;
    if (const char *m = Attr(node, "mixin"); m != nullptr && *m != '\0')
        return true;
    const char *inherits = Attr(node, "inherits");
    if (inherits == nullptr || *inherits == '\0')
        return false;
    bool found = false;
    const auto check = [&](const char *name) {
        if (!found)
            if (const void *t = Template(name))
                found = HasMixinInChain(t, listInherits, depth + 1);
    };
    if (listInherits)
        Inherits::ForEach(inherits, check);
    else
        check(inherits);
    return found;
}

} // namespace

void ApplyChain(void *L, int index, const void *node, void *status, const char *owner,
                bool listInherits) {
    ApplyNode(L, index, node, status, owner, listInherits, 0);
}

void Begin(void *object, const void *node, void *status, bool listInherits, const void *first) {
    bool outer = true;
    for (void *o : g_loading)
        if (o == object)
            outer = false;
    g_loading.push_back(object);
    if (!outer || (!HasMixinInChain(node, listInherits, 0) &&
                   (first == nullptr || !HasMixinInChain(first, listInherits, 0))))
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    if (PushWrapper(L, object)) {
        const char *owner = Game::Lua::ObjectDisplayName(object);
        if (first != nullptr)
            ApplyChain(L, top + 1, first, status, owner, listInherits);
        ApplyChain(L, top + 1, node, status, owner, listInherits);
    }
    Game::Lua::SetTop(L, top);
}

void End(void *object) {
    if (!g_loading.empty() && g_loading.back() == object)
        g_loading.pop_back();
}

namespace {

void BeginFrame(void *object, const void *node, void *status) {
    Begin(object, node, status, true, Intrinsics::ForChain(node, nullptr));
}
void EndFrame(void *object, const void *, void *) { End(object); }

const FrameLoad::AutoSubscribeBefore _frameBegin{&BeginFrame};
const FrameLoad::AutoSubscribe _frameEnd{&EndFrame};

} // namespace

} // namespace Xml::Mixin
