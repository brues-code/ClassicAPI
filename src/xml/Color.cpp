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

// A colour element naming a global colour: `<Color color="PANEL_BACKGROUND_COLOR"/>`,
// optionally with `a` overriding its alpha (`<Color color="BLACK_FONT_COLOR" a="0.5"/>`).
//
// The engine's reader (FUN_XML_READ_COLOR) reads only `r`, `g`, `b` (default
// 0) and `a` (default 1) and packs them as 0xAARRGGBB; every <Color> the
// loaders read (texture, font string, backdrop, gradient, fog) goes through
// it. The hook lets it run, then, when `color` names a colour of the global
// colour table (UI::ColorData, the same table the Lua colour globals come
// from), replaces the RGB with that colour's, and the alpha too unless the
// node sets `a`. A name the table lacks keeps what the reader read.

#include "Game.h"
#include "Offsets.h"
#include "ui/ColorData.h"
#include "xml/FrameLoad.h"

#include <cstdint>
#include <cstring>

namespace Xml::Color {

namespace {

using ReadColor_t = int(__fastcall *)(const void *node, uint32_t *bgra, void *status);

ReadColor_t g_orig = nullptr;

bool Find(const char *name, uint32_t &argb) {
    for (const auto &e : UI::ColorData::kColors) {
        if (_stricmp(e.baseTag, name) == 0) {
            argb = static_cast<uint32_t>(e.argb);
            return true;
        }
    }
    return false;
}

int __fastcall ReadColor_h(const void *node, uint32_t *bgra, void *status) {
    const int result = g_orig(node, bgra, status);
    const char *name = node != nullptr ? Attr(node, "color") : nullptr;
    uint32_t argb;
    if (name == nullptr || *name == '\0' || !Find(name, argb))
        return result;
    const char *alpha = Attr(node, "a");
    if (alpha != nullptr && *alpha != '\0')
        argb = (argb & 0x00FFFFFFu) | (*bgra & 0xFF000000u);
    *bgra = argb;
    return result;
}

const Game::HookAutoRegister _hook{Offsets::FUN_XML_READ_COLOR, reinterpret_cast<void *>(&ReadColor_h),
                                   reinterpret_cast<void **>(&g_orig)};

} // namespace

} // namespace Xml::Color
