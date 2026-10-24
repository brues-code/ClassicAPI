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

// `C_XMLUtil.GetTemplates()` — enumerate every registered virtual XML frame
// template as `{ name = <string>, type = <frameType> }`.
//
// The engine keeps virtual templates (the `<Frame virtual="true">` etc. that
// `inherits=` targets) in a Storm hash table. The XML parser (FUN_006ede10)
// registers each one via FUN_006ee500, and the inherits resolver reads the
// same table via FUN_006ee6f0 — so it's the authoritative template store, and
// walking it yields exactly what modern `C_XMLUtil.GetTemplates` returns.
// Fonts are a separate registry and are (correctly) not included; non-virtual
// named frames take a different instantiate path and never enter this table.
//
// Table shape (see Offsets.h): a POINTER to a bucket array at
// VAR_XML_TEMPLATE_TABLE, a bucket-count mask at VAR_XML_TEMPLATE_MASK
// (0xFFFFFFFF until the first template registers). Each bucket is a Storm
// intrusive list; each node carries the template name (+0x14) and the parsed
// definition node (+0x18), whose element tag string (+0x8) is the frame type.
// Contents reflect the currently-loaded XML and are rebuilt on `/reload`.

#include "Game.h"
#include "Offsets.h"
#include "xml/KeyValues.h"

#include <cstdint>
#include <cstdlib>

namespace Xml::Templates {

namespace {

// ---- Node-DOM helpers (shared by GetTemplateInfo / DoesTemplateExist) -----

using TemplateLookup_t = const uint8_t *(__fastcall *)(const char *name);
using GetAttr_t = const char *(__thiscall *)(const void *node, const char *name);

// The engine's by-name template lookup (FUN_006ee6f0) — the same path
// `inherits=` resolves through. Returns the parsed definition node, or null
// if no template of that name is registered (case-insensitive, hashed).
const uint8_t *LookupTemplate(const char *name) {
    return reinterpret_cast<TemplateLookup_t>(
        Offsets::FUN_XML_TEMPLATE_LOOKUP)(name);
}

// Value of `node`'s attribute `name`, or null if absent.
const char *Attr(const uint8_t *node, const char *name) {
    return reinterpret_cast<GetAttr_t>(Offsets::FUN_XML_NODE_GET_ATTRIBUTE)(
        node, name);
}

const uint8_t *FirstChild(const uint8_t *node) {
    return Game::Read<const uint8_t *>(node, Offsets::OFF_XML_NODE_CHILD);
}
const uint8_t *NextSibling(const uint8_t *node) {
    return Game::Read<const uint8_t *>(node, Offsets::OFF_XML_NODE_SIBLING);
}

// Case-insensitive tag match, using the engine's own comparator (the same one
// FUN_006f1eb0 uses to recognize "AbsDimension" etc.).
bool TagEquals(const uint8_t *node, const char *tag) {
    const char *t = Game::Read<const char *>(node, Offsets::OFF_XML_NODE_TAG);
    if (t == nullptr)
        return false;
    return Game::StrCmpI(t, tag) == 0;
}

const uint8_t *FindChild(const uint8_t *node, const char *tag) {
    for (const uint8_t *c = FirstChild(node); c != nullptr; c = NextSibling(c))
        if (TagEquals(c, tag))
            return c;
    return nullptr;
}

// The statically-declared width/height from a `<Size>` child (0 if none).
// Handles the inline `<Size x= y=>` form and the `<Size><AbsDimension x= y=>`
// (or RelDimension) child form. Returns the RAW values from the XML — retail's
// GetTemplateInfo reports the declared value, so unlike the engine's own Size
// parser (FUN_006f1eb0) we deliberately skip the UI-scale conversion.
void ReadSize(const uint8_t *node, double *outW, double *outH) {
    *outW = 0.0;
    *outH = 0.0;
    const uint8_t *size = FindChild(node, "Size");
    if (size == nullptr)
        return;
    const char *sx = Attr(size, "x");
    const char *sy = Attr(size, "y");
    const uint8_t *dim = FirstChild(size);
    if (dim != nullptr &&
        (TagEquals(dim, "AbsDimension") || TagEquals(dim, "RelDimension"))) {
        const char *dx = Attr(dim, "x");
        const char *dy = Attr(dim, "y");
        if (dx != nullptr && *dx != '\0')
            sx = dx;
        if (dy != nullptr && *dy != '\0')
            sy = dy;
    }
    if (sx != nullptr && *sx != '\0')
        *outW = std::strtod(sx, nullptr);
    if (sy != nullptr && *sy != '\0')
        *outH = std::strtod(sy, nullptr);
}

// `C_XMLUtil.GetTemplates()` -> XMLTemplateListInfo[] of { name, type }.
int __fastcall Script_GetTemplates(void *L) {
    Game::Lua::NewTable(L); // the result array

    const uint32_t mask = Game::Read<uint32_t>(Offsets::VAR_XML_TEMPLATE_MASK);
    if (mask == 0xFFFFFFFFu)
        return 1; // no template has ever registered — empty array

    const uint8_t *base = Game::Read<const uint8_t *>(Offsets::VAR_XML_TEMPLATE_TABLE);
    if (base == nullptr)
        return 1;

    int outIdx = 0;
    for (uint32_t b = 0; b <= mask; ++b) {
        const uint8_t *bucket = base + b * Offsets::XML_TEMPLATE_BUCKET_STRIDE;
        const int linkOff = Game::Read<int>(
            bucket, Offsets::OFF_XML_TEMPLATE_BUCKET_LINKOFF);
        const uint8_t *node = Game::Read<const uint8_t *>(
            bucket, Offsets::OFF_XML_TEMPLATE_BUCKET_HEAD);

        // Walk the intrusive chain; the tail sentinel has its low bit set
        // (mirrors the engine's own traversal in FUN_006ee6f0).
        while (node != nullptr && (reinterpret_cast<uintptr_t>(node) & 1) == 0) {
            const char *name = Game::Read<const char *>(
                node, Offsets::OFF_XML_TEMPLATE_NODE_NAME);
            const uint8_t *def = Game::Read<const uint8_t *>(
                node, Offsets::OFF_XML_TEMPLATE_NODE_DEF);
            const char *type =
                (def != nullptr)
                    ? Game::Read<const char *>(def, Offsets::OFF_XML_NODE_TAG)
                    : nullptr;

            ++outIdx;
            Game::Lua::PushNumber(L, static_cast<double>(outIdx));
            Game::Lua::NewTable(L);
            Game::Lua::SetFieldString(L, "name", name != nullptr ? name : "");
            Game::Lua::SetFieldString(L, "type", type != nullptr ? type : "");
            Game::Lua::SetTable(L, -3);

            node = *reinterpret_cast<const uint8_t *const *>(node + linkOff + 4);
        }
    }
    return 1;
}

// `C_XMLUtil.GetTemplateInfo(name)` -> XMLTemplateInfo table, or nil if the
// template doesn't exist.
int __fastcall Script_GetTemplateInfo(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_XMLUtil.GetTemplateInfo(\"name\")");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    const uint8_t *node = (name != nullptr) ? LookupTemplate(name) : nullptr;
    if (node == nullptr)
        return 0; // nil — no such template

    Game::Lua::NewTable(L); // XMLTemplateInfo

    const char *type = Game::Read<const char *>(node, Offsets::OFF_XML_NODE_TAG);
    Game::Lua::SetFieldString(L, "type", type != nullptr ? type : "");

    double w = 0.0, h = 0.0;
    ReadSize(node, &w, &h);
    Game::Lua::SetFieldNumber(L, "width", w);
    Game::Lua::SetFieldNumber(L, "height", h);

    // keyValues: the template's own <KeyValues> entries, as written.
    Game::Lua::PushString(L, "keyValues");
    Xml::KeyValues::PushInfo(L, node);
    Game::Lua::SetTable(L, -3);

    // inherits: the comma-delimited `inherits=` attribute, or nil (field left
    // unset) when the template inherits nothing.
    const char *inherits = Attr(node, "inherits");
    if (inherits != nullptr && *inherits != '\0')
        Game::Lua::SetFieldString(L, "inherits", inherits);

    // sourceLocation is intentionally omitted (nil): vanilla records no
    // file/line for XML nodes — a 10.2.0 addition gated behind the
    // enableSourceLocationLookup CVar even on retail.
    return 1;
}

// `C_XMLUtil.DoesTemplateExist(name)` -> bool.
int __fastcall Script_DoesTemplateExist(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_XMLUtil.DoesTemplateExist(\"name\")");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    const uint8_t *node = (name != nullptr) ? LookupTemplate(name) : nullptr;
    Game::Lua::PushBool(L, node != nullptr);
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_XMLUtil", "GetTemplates",
                                     &Script_GetTemplates);
    Game::Lua::RegisterTableFunction("C_XMLUtil", "GetTemplateInfo",
                                     &Script_GetTemplateInfo);
    Game::Lua::RegisterTableFunction("C_XMLUtil", "DoesTemplateExist",
                                     &Script_DoesTemplateExist);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Xml::Templates
