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

// `GetCoinText` / `C_CurrencyInfo.GetCoinText` — the plain-text money string
// ("12 Gold, 34 Silver, 56 Copper"), 4.3.4's `Script_GetCoinText` →
// `GetCurrencyText` (LootFrame.cpp): the non-zero denominations, gold first,
// each from its `GOLD_AMOUNT` / `SILVER_AMOUNT` / `COPPER_AMOUNT` GlobalString,
// joined by the separator (default ", "); zero copper is the empty string.
// 1.12's only money formatter (`GetLootRowName`, 0x004C2550) hard-codes
// "%d %s" over GOLD / SILVER / COPPER with "\n", so it can't carry the
// separator or the localized formats; the strings are backported in
// `locales/*.lua`.
//
// `GetCoinTextureString` / `C_CurrencyInfo.GetCoinTextureString` — the coin-icon
// money string (e.g. "12<gold> 34<silver> 56<copper>"), a C++ backport of the
// retail C function.
//
// Behaviour mirrors 3.3.5's `Script_GetCoinTextureString` (FUN_00510d00 →
// formatter FUN_007e7e10, `GetCurrencyTextureString` in the decomp): the icon
// height defaults to 14 and is taken as given otherwise; split the copper
// amount into gold / silver / copper, emit only the non-zero denominations in
// that order, separated by one space; a zero amount shows "0" copper. Each denomination is built from a localized
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

#include <cstdint>
#include <cstdio>
#include <string>

namespace Currency::CoinText {

namespace {

// Icon size when the caller passes none — 3.3.5's `int32_t height = 14`. The
// inline-texture renderer scales a written size by the line's font against a
// 14-unit reference (4.3.4's ParseEmbeddedTexture), so this follows the text.
constexpr int kDefaultHeight = 14;

// Fallbacks byte-identical to the backported GlobalStrings, used only if the
// locale layer somehow isn't loaded when this is first called.
// Format: count |T sheet : height : width : offsetX=2 : offsetY=0 : texW=4 :
// texH=1 : left : right : top=0 : bottom=1 |t  (left:right selects the coin cell).
// offsetX 2 is retail's (3.3.5 / 4.3.4 GlobalStrings: `UI-GoldIcon:%d:%d:2:0`):
// it shifts only the drawn coin, leaving room against the digits before it.
constexpr const char *kGoldFallback =
    "%d|TInterface\\MoneyFrame\\UI-MoneyIcons:%d:%d:2:0:4:1:0:1:0:1|t";
constexpr const char *kSilverFallback =
    "%d|TInterface\\MoneyFrame\\UI-MoneyIcons:%d:%d:2:0:4:1:1:2:0:1|t";
// The three coin arts occupy IDENTICAL pixels within their sheet cells (x 0-14,
// y 1-14 of 16 — confirmed by decoding the DXT3 alpha of the exported
// UI-MoneyIcons.blp); only the left:right cell select differs (gold 0:1,
// silver 1:2, copper 2:3).
constexpr const char *kCopperFallback =
    "%d|TInterface\\MoneyFrame\\UI-MoneyIcons:%d:%d:2:0:4:1:2:3:0:1|t";

// Formats one denomination from its localized GlobalString and appends it to
// `out`. Reads `_G[globalName]` the way retail reads the coin GlobalStrings; the
// format consumes (count, iconHeight, iconWidth). Denominations are separated
// by one space, as the engine's formatter packs them.
void AppendDenom(void *L, std::string &out, const char *globalName, const char *fallback,
                 int count, int height) {
    if (!out.empty())
        out += ' ';
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

// Fallbacks byte-identical to the backported enUS GlobalStrings.
constexpr const char *kGoldTextFallback = "%d Gold";
constexpr const char *kSilverTextFallback = "%d Silver";
constexpr const char *kCopperTextFallback = "%d Copper";

// 4.3.4's GetCurrencyText default separator.
constexpr const char *kDefaultSeparator = ", ";

void AppendText(void *L, std::string &out, const char *separator, const char *globalName,
                const char *fallback, int count) {
    if (count == 0)
        return;
    if (!out.empty())
        out += separator;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushLocalizedFormatInt(L, globalName, fallback, count);
    if (const char *piece = Game::Lua::ToString(L, -1))
        out += piece;
    Game::Lua::SetTop(L, top);
}

// GetCoinText(amount [, separator]) -> string
int __fastcall Script_GetCoinText(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetCoinText(amount [,separator])");
        return 0;
    }
    // 4.3.4 takes the amount as uint32 (`static_cast<uint32_t>(lua_tonumber)`),
    // the width of the money field.
    const double amt = Game::Lua::ToNumber(L, 1);
    const uint32_t amount = amt <= 0.0          ? 0u
                            : amt >= 4294967295.0 ? 0xFFFFFFFFu
                                                  : static_cast<uint32_t>(amt);
    const char *separator =
        Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : kDefaultSeparator;

    std::string out;
    AppendText(L, out, separator, "GOLD_AMOUNT", kGoldTextFallback,
               static_cast<int>(amount / 10000));
    AppendText(L, out, separator, "SILVER_AMOUNT", kSilverTextFallback,
               static_cast<int>((amount % 10000) / 100));
    AppendText(L, out, separator, "COPPER_AMOUNT", kCopperTextFallback,
               static_cast<int>(amount % 100));

    Game::Lua::PushString(L, out.c_str());
    return 1;
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
    if (Game::Lua::IsNumber(L, 2))
        height = static_cast<int>(Game::Lua::ToNumber(L, 2));

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

const Game::Doc::Field kCoinTextArgs[] = {
    Game::Doc::Req("amount", "number", "The amount of money, in copper."),
    Game::Doc::Opt("separator", "string", "\", \"",
                   "The text between the gold, silver and copper parts."),
};
const Game::Doc::Field kCoinTextRets[] = {
    Game::Doc::Req("result", "string",
                   "For example \"12 Gold, 34 Silver, 56 Copper\". Empty for 0."),
};
const Game::Doc::Function kGetCoinText{
    "Formats an amount of money as text, with a word for each coin.", kCoinTextArgs,
    kCoinTextRets, "CurrencyInfo"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetCoinText", &Script_GetCoinText, &kGetCoinText);
    Game::Lua::RegisterTableFunction("C_CurrencyInfo", "GetCoinText", &Script_GetCoinText,
                                     &kGetCoinText);
    Game::Lua::RegisterGlobalFunction("GetCoinTextureString", &Script_GetCoinTextureString);
    Game::Lua::RegisterTableFunction("C_CurrencyInfo", "GetCoinTextureString",
                                     &Script_GetCoinTextureString);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Currency::CoinText
