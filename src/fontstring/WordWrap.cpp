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

// `fontstring:SetWordWrap([wrap])` and `fontstring:CanWordWrap()`.
//
// 4.3.4 keeps the setting as style bit 0x40, set when the string does NOT
// wrap. Its setter is 1.12's own SetNonSpaceWrap (0x0079E9F0) with that bit,
// inverted: clear the bit from the settable mask so the font no longer
// overrides it, and store the new style if it changed. 1.12's text engine
// already honours the bit (FONT_STYLE_NO_WORD_WRAP). A change lays the string
// out again as SetText does, not only rebuilds it: a string sized by its text
// would keep its old height, and the engine's fit would then neither
// ellipsize the one line (wrap off) nor make room for the others (wrap on).

#include "Game.h"
#include "Offsets.h"
#include "fontstring/Layout.h"

#include <cstdint>

namespace FontString::WordWrap {

namespace {


constexpr uint32_t kNoWrap = Offsets::FONT_STYLE_NO_WORD_WRAP;

int __fastcall Script_SetWordWrap(void *L) {
    void *fontString = Game::Lua::ResolveFontString(L);
    if (fontString == nullptr)
        return 0;
    const bool wrap = Game::Lua::ToFlag(L, 2, true);
    Game::Ref<uint32_t>(fontString, Offsets::OFF_FONTSTRING_SETTABLE_STYLE) &= ~kNoWrap;
    const uint32_t flags = Game::Read<uint32_t>(fontString, Offsets::OFF_FONTSTRING_FLAGS);
    const uint32_t wanted = wrap ? flags & ~kNoWrap : flags | kNoWrap;
    if (wanted != flags) {
        Game::Ref<uint32_t>(fontString, Offsets::OFF_FONTSTRING_FLAGS) = wanted;
        FontString::Layout::Invalidate(fontString);
    }
    return 0;
}

int __fastcall Script_CanWordWrap(void *L) {
    void *fontString = Game::Lua::ResolveFontString(L);
    if (fontString == nullptr)
        return 0;
    if (Game::Read<uint32_t>(fontString, Offsets::OFF_FONTSTRING_FLAGS) & kNoWrap)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushNumber(L, 1.0);
    return 1;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"SetWordWrap", &Script_SetWordWrap},
    {"CanWordWrap", &Script_CanWordWrap},
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Opt("wrap", "boolean", "true", "When false, the text stays on one line."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Opt("wrap", "number", nullptr, "1 when the text wraps, else nil."),
};
const Game::Doc::Function kSet{"Sets whether the text wraps onto more lines.", kSetArgs, {}};
const Game::Doc::Function kGet{"Returns whether the text wraps onto more lines.", {}, kGetRets};

const Game::Doc::Method kDocs[] = {
    {"SetWordWrap", &kSet},
    {"CanWordWrap", &kGet},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY), kMethods, 2,
                                    kDocs, 2);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FontString::WordWrap
