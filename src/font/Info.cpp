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

// `GetFonts()` and `GetFontInfo(font or name)`, as the 3.80.2 client has them
// (0x141B6CA40 / 0x141B6CB20, system "Font").
//
// GetFonts walks the engine's named-font table (VAR_FONT_TABLE, the one XML
// <Font name> and CreateFont fill) in its own list order, as 3.80.2 walks
// its table, and returns the names as an array, or nil when the table is
// empty.
//
// GetFontInfo takes a Font object (resolved with the engine's own type gate
// and errors) or a name (FindOrCreateFontObject without create; an unknown
// name returns nil) and returns 3.80.2's plain table, no mixins:
//   { height, outline, color = {r,g,b,a}, shadow = {x, y, color} }
// - height: 3.80.2's integer `int(aspect * h * 1024 / widthScale)`, in float
//   steps as its MULSS/DIVSS/CVTTSS2SI do. It is 1.12's GetFont conversion
//   (FUN_COORD_*) truncated, not rounded.
// - outline: the engine's own flag formatter, which GetFont calls. Its table
//   lists the bits in 3.80.2's order (1, 4, 2) and joins them with ", ".
// - colours: each byte times 1/255.0f, 3.80.2's constant. 1.12's own
//   GetTextColor uses 0.00392156 and reads white as 0.9999978.
// - shadow: nil while both stored offsets are 0, even with a shadow colour,
//   as in 3.80.2. Otherwise x and y come from the engine's GetShadowOffset
//   pusher, which converts them to UI units.
// Retail's FontScriptInfo also has fontObject and canBeUserScaled; 3.80.2's
// builder sets neither, so neither is set here.

#include "Game.h"
#include "Offsets.h"
#include "storm/HashTable.h"

#include <cstdint>

namespace Font::Info {
namespace {

using FindOrCreateFont_t = void *(__fastcall *)(const char *name, int create);
using FlagsToString_t = const char *(__fastcall *)(uint32_t flags);
using PushShadowOffset_t = int(__fastcall *)(int unused, void *font, void *L);
using GetScaledScreenAspect_t = float(__cdecl *)();

int HeightPixels(const void *font) {
    const float widthScale = Game::Read<float>(Offsets::VAR_COORD_DDC_WIDTH_SCALE);
    if (widthScale == 0.0f)
        return 0; // scale globals unset, as Game::InternalToPixel answers
    float v = reinterpret_cast<GetScaledScreenAspect_t>(Offsets::FUN_COORD_GET_SCALED_SCREEN_ASPECT)();
    v *= Game::Read<float>(font, Offsets::OFF_SIMPLEFONT_HEIGHT);
    v *= 1024.0f;
    v /= widthScale;
    return static_cast<int>(v);
}

// Pushes {r, g, b, a} of a colour packed b | g << 8 | r << 16 | a << 24.
void PushColor(void *L, uint32_t argb) {
    const auto channel = [argb](int shift) {
        return static_cast<float>((argb >> shift) & 0xFF) * (1.0f / 255.0f);
    };
    Game::Lua::CreateTable(L, 0, 4);
    Game::Lua::SetFieldNumber(L, "r", channel(16));
    Game::Lua::SetFieldNumber(L, "g", channel(8));
    Game::Lua::SetFieldNumber(L, "b", channel(0));
    Game::Lua::SetFieldNumber(L, "a", channel(24));
}

// Pushes the shadow table, or nil for a zero offset.
void PushShadow(void *L, void *font) {
    if (Game::Read<float>(font, Offsets::OFF_SIMPLEFONT_SHADOW_OFFSET_X) == 0.0f &&
        Game::Read<float>(font, Offsets::OFF_SIMPLEFONT_SHADOW_OFFSET_Y) == 0.0f) {
        Game::Lua::PushNil(L);
        return;
    }
    Game::Lua::CreateTable(L, 0, 3);                     // s
    Game::Lua::PushString(L, "x");                       // s, "x"
    reinterpret_cast<PushShadowOffset_t>(Offsets::FUN_FONT_PUSH_SHADOW_OFFSET)(0, font, L); // s, "x", x, y
    Game::Lua::Insert(L, -3);                            // s, y, "x", x
    Game::Lua::SetTable(L, -4);                          // s, y
    Game::Lua::PushString(L, "y");
    Game::Lua::Insert(L, -2);                            // s, "y", y
    Game::Lua::SetTable(L, -3);                          // s
    Game::Lua::PushString(L, "color");
    PushColor(L, Game::Read<uint32_t>(font, Offsets::OFF_SIMPLEFONT_SHADOW_COLOR));
    Game::Lua::SetTable(L, -3);
}

int __fastcall Script_GetFonts(void *L) {
    int count = 0;
    Storm::HashTable::ForEachListed(Offsets::VAR_FONT_TABLE, [&](const uint8_t *record) {
        if (count == 0)
            Game::Lua::NewTable(L);
        Game::Lua::PushString(L, Storm::HashTable::NodeName(record));
        Game::Lua::RawSetI(L, -2, ++count);
        return true;
    });
    if (count == 0)
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_GetFontInfo(void *L) {
    void *font;
    const int type = Game::Lua::Type(L, 1);
    if (type == Game::Lua::TYPE_TABLE) {
        font = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_FONT_LUA_TYPE_ID);
        if (font == nullptr)
            return 0;
    } else if (type == Game::Lua::TYPE_STRING) {
        font = reinterpret_cast<FindOrCreateFont_t>(Offsets::FUN_FONT_FIND_OR_CREATE)(
            Game::Lua::ToString(L, 1), 0);
        if (font == nullptr) {
            Game::Lua::PushNil(L);
            return 1;
        }
    } else {
        Game::Lua::Error(L, "Usage: GetFontInfo(font or name);");
        return 0;
    }

    Game::Lua::CreateTable(L, 0, 4);
    Game::Lua::SetFieldNumber(L, "height", HeightPixels(font));
    Game::Lua::SetFieldString(L, "outline",
                              reinterpret_cast<FlagsToString_t>(Offsets::FUN_FONT_FLAGS_TO_STRING)(
                                  Game::Read<uint32_t>(font, Offsets::OFF_SIMPLEFONT_FLAGS)));
    Game::Lua::PushString(L, "color");
    PushColor(L, Game::Read<uint32_t>(font, Offsets::OFF_SIMPLEFONT_TEXT_COLOR));
    Game::Lua::SetTable(L, -3);
    Game::Lua::PushString(L, "shadow");
    PushShadow(L, font);
    Game::Lua::SetTable(L, -3);
    return 1;
}

const Game::Doc::Field kGetFontsRets[] = {
    Game::Doc::Opt("fontNames", "table", nullptr,
                   "The names of every named font object, in creation order; nil when there are none."),
};
const Game::Doc::Function kGetFonts{"Returns the names of all named font objects.", {}, kGetFontsRets,
                                    "Font"};

const Game::Doc::Field kGetFontInfoArgs[] = {
    Game::Doc::Req("fontObject", "SimpleFont", "A font object, or the name of one."),
};
const Game::Doc::Field kGetFontInfoRets[] = {
    Game::Doc::Opt("info", "FontScriptInfo", nullptr, "Nil when no font has that name."),
};
const Game::Doc::Function kGetFontInfo{"Returns the height, outline, colour and shadow of a font object.",
                                       kGetFontInfoArgs, kGetFontInfoRets, "Font"};

const Game::Doc::Field kShadowFields[] = {
    Game::Doc::Req("color", "colorRGBA"),
    Game::Doc::Req("x", "number"),
    Game::Doc::Req("y", "number"),
};
const Game::Doc::Structure kShadow{"FontScriptShadowInfo", "Font", kShadowFields};

const Game::Doc::Field kInfoFields[] = {
    Game::Doc::Req("color", "colorRGBA"),
    Game::Doc::Req("height", "number", "The font height in UI units, truncated to an integer."),
    Game::Doc::Req("outline", "string", "The font flags joined with \", \", or \"\"."),
    Game::Doc::Opt("shadow", "FontScriptShadowInfo", nullptr, "Nil when the shadow offset is 0, 0."),
};
const Game::Doc::Structure kInfo{"FontScriptInfo", "Font", kInfoFields};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetFonts", &Script_GetFonts, &kGetFonts);
    Game::Lua::RegisterGlobalFunction("GetFontInfo", &Script_GetFontInfo, &kGetFontInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Font::Info
