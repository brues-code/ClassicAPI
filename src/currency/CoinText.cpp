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

// `GetCoinTextureString` / `C_CurrencyInfo.GetCoinTextureString` — the coin-icon
// money string (e.g. "12<gold> 34<silver> 56<copper>"), a C++ backport of the
// retail C function.
//
// Behaviour derived from 3.3.5's `Script_GetCoinTextureString` (FUN_00510d00 →
// formatter FUN_007e7e10): split the copper amount into gold / silver / copper,
// emit only the non-zero denominations in that order, space-separated; a zero
// amount shows "0" copper. Each denomination is built from a localized
// GlobalString — `GOLD_AMOUNT_TEXTURE` / `SILVER_AMOUNT_TEXTURE` /
// `COPPER_AMOUNT_TEXTURE` — exactly as the engine does (its formatter
// `FUN_0076f070` is a C-format over the same GlobalString). Those strings don't
// exist in stock 1.12, so they're backported in `locales/enUS.lua`
// (language-independent, so the enUS base covers every locale); the embedded
// `!!!ClassicAPI` addon guarantees they're loaded.
//
// 1.12 has no separate UI-GoldIcon / UI-SilverIcon / UI-CopperIcon textures
// (those are a later-expansion split); vanilla uses ONE sprite sheet
// `Interface\MoneyFrame\UI-MoneyIcons` cropped per coin (gold 0-0.25, silver
// 0.25-0.5, copper 0.5-0.75 across a 4-cell strip — from CoinPickupFrame.xml).
// So the format carries the inline-texture texcoord fields (texW=4 texH=1, then
// the per-coin left:right cell). The `|T` markup renders as real coin icons via
// ClassicAPI's inline-texture backport (`Text::InlineTexture`).

#include "Game.h"

#include <cstdio>
#include <string>

namespace Currency::CoinText {

namespace {

// Icon size. Retail defaults fontHeight to 0 = "use the display font's height",
// and the inline-texture renderer now honours 0 (it resolves the coin to the
// line's font height at draw time), so coins match the surrounding text instead
// of a fixed size. Callers may still pass an explicit height.
constexpr int kDefaultHeight = 0;

// Fallbacks byte-identical to the backported GlobalStrings, used only if the
// locale layer somehow isn't loaded when this is first called.
// Format: count |T sheet : height=0 : width=0 : offsetX=0 : offsetY=0 : texW=4 :
// texH=1 : left : right : top=0 : bottom=1 |t  (left:right selects the coin cell).
// height/width 0 = size to the font. offsetX stays 0: the inline-texture
// renderer's font-relative pad spaces the coin from its digits (the historical
// "coin sits on the digit" bug was the emitter's lazy pen read-back, fixed in
// the DLL). Never grow a per-coin offset here.
constexpr const char *kGoldFallback =
    "%d|TInterface\\MoneyFrame\\UI-MoneyIcons:%d:%d:0:0:4:1:0:1:0:1|t";
constexpr const char *kSilverFallback =
    "%d|TInterface\\MoneyFrame\\UI-MoneyIcons:%d:%d:0:0:4:1:1:2:0:1|t";
// The three coin arts occupy IDENTICAL pixels within their sheet cells (x 0-14,
// y 1-14 of 16 — confirmed by decoding the DXT3 alpha of the exported
// UI-MoneyIcons.blp); only the left:right cell select differs (gold 0:1,
// silver 1:2, copper 2:3).
constexpr const char *kCopperFallback =
    "%d|TInterface\\MoneyFrame\\UI-MoneyIcons:%d:%d:0:0:4:1:2:3:0:1|t";

// Formats one denomination from its localized GlobalString and appends it to
// `out`. Reads `_G[globalName]` the way retail reads the coin GlobalStrings; the
// format consumes (count, iconHeight, iconWidth). No separator between
// denominations — the renderer's symmetric per-icon pad already spaces each coin
// from the surrounding digits, so a space here would double the gap.
void AppendDenom(void *L, std::string &out, const char *globalName, const char *fallback,
                 int count, int height) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushLocalizedString(L, globalName, fallback); // [.., fmt]
    const char *fmt = Game::Lua::ToString(L, -1);
    char piece[256];
    piece[0] = '\0';
    if (fmt != nullptr)
        std::snprintf(piece, sizeof piece, fmt, count, height, height);
    Game::Lua::SetTop(L, top); // pop the format string
    out += piece;
}

// GetCoinTextureString(amount [, fontHeight]) -> string
int __fastcall Script_GetCoinTextureString(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetCoinTextureString(amount [, fontHeight])");
        return 0; // unreachable — Error long-jumps
    }
    const double amt = Game::Lua::ToNumber(L, 1);
    long long amount = static_cast<long long>(amt < 0.0 ? amt - 0.5 : amt + 0.5); // round
    if (amount < 0)
        amount = 0;
    // Vanilla money is a uint32 copper field, so 0xFFFFFFFF copper (~429,496
    // gold) is the most the game can represent. Clamp to it: an unclamped huge
    // input (e.g. 1e15) makes `gold` exceed INT_MAX, and the cast below truncates
    // it to the low 32 bits, printing a garbage count through the "%d" format.
    constexpr long long kMaxCopper = 0xFFFFFFFFLL;
    if (amount > kMaxCopper)
        amount = kMaxCopper;

    int height = kDefaultHeight;
    if (Game::Lua::IsNumber(L, 2)) {
        const int h = static_cast<int>(Game::Lua::ToNumber(L, 2));
        if (h > 0)
            height = h;
    }

    const int gold = static_cast<int>(amount / 10000);
    const int silver = static_cast<int>((amount % 10000) / 100);
    const int copper = static_cast<int>(amount % 100);

    std::string out;
    if (amount == 0) {
        AppendDenom(L, out, "COPPER_AMOUNT_TEXTURE", kCopperFallback, 0, height);
    } else {
        if (gold > 0)
            AppendDenom(L, out, "GOLD_AMOUNT_TEXTURE", kGoldFallback, gold, height);
        if (silver > 0)
            AppendDenom(L, out, "SILVER_AMOUNT_TEXTURE", kSilverFallback, silver, height);
        if (copper > 0)
            AppendDenom(L, out, "COPPER_AMOUNT_TEXTURE", kCopperFallback, copper, height);
    }

    Game::Lua::PushString(L, out.c_str());
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetCoinTextureString", &Script_GetCoinTextureString);
    Game::Lua::RegisterTableFunction("C_CurrencyInfo", "GetCoinTextureString",
                                     &Script_GetCoinTextureString);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Currency::CoinText
