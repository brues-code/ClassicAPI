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

// `GameTooltip:SetHyperlink(link)` takes a full chat link, and `spell:`,
// `quest:` and `trade:` links, as on 3.x clients.
//
// Vanilla's Script_GameTooltip_SetHyperlink knows two payloads, `item:` and
// `enchant:`, matched against the start of the argument. Anything else -
// including a full link such as
// `|cff9d9d9d|Hitem:7073:0:0:0|h[Broken Fang]|h|r`, the form 2.0+
// GetItemInfo and chat hand out - raises "Unknown link type". Ported code
// relies on the later forms: Zygor Guides Viewer shows GetItemInfo links
// with SetHyperlink, and reads quest titles through a scanning tooltip with
// `SetHyperlink("|Hquest:<id>:1|h[q]|h")`.
//
// Co-hook:
//   - A full link is reduced to its payload, the text between `|H` and `|h`.
//   - `spell:<spellID>` builds the tooltip GameTooltip:SetSpellByID builds.
//   - `enchant:<spellID>` for a trade-skill recipe (SPELL_ATTR_TRADESPELL)
//     builds the same spell tooltip, as 3.3.5's SetHyperlink does. The
//     engine's own `enchant:` test only takes Enchanting's craft spells
//     (record +0x0C == 3, which no trade-skill recipe has) and raises
//     "Unknown link type" for the rest, so the recipe links from
//     GetTradeSkillRecipeLink, and from SuperWoW's
//     `GetTradeSkillItemLink(index, 1)`, could not be shown. Craft spells
//     still go to the engine. SuperWoW's own SetHyperlink patch is a jump
//     at 0x00532210, after the engine's `item:` test, that adds `quest:`
//     and otherwise resumes at the engine's `enchant:` test; this co-hook
//     runs at the function's entry, before it.
//   - `quest:<questID>[:level]` shows the quest's title, then its objectives
//     text with the `$N`-style tokens expanded as the quest log shows them,
//     both from the quest cache. A quest the cache doesn't hold yet leaves
//     the tooltip empty and queues the server query the quest log would
//     make (once per quest - see Quest::Cache::Lookup's `unique`); the next
//     SetHyperlink after the answer shows it.
//   - `trade:` opens the linked profession in the profession window
//     (TradeSkill::Link::Open) and leaves the tooltip alone, as 3.3.5's
//     SetHyperlink does. That is how a click on a chat link opens it: the
//     SetItemRef of both 1.12 and 3.3.5 ends in ItemRefTooltip:SetHyperlink.
//   - Everything else - `item:`, other `enchant:` links, and the error for link types
//     vanilla has no data for (`achievement:`, `talent:`, ...) - is the
//     engine's own, run on the payload.
//
// Prefixes match case-insensitively, as the engine's own `item:` test does
// (Game::StrCmpI).

#include "Game.h"
#include "Offsets.h"
#include "quest/Cache.h"
#include "spell/Lookup.h"
#include "spell/Tooltip.h"
#include "tooltip/Lines.h"
#include "tradeskill/Link.h"
#include "ui/ColorData.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Tooltip::Hyperlink {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

ScriptFn_t g_origSetHyperlink = nullptr;

bool HasPrefix(const char *s, size_t len, const char *prefix) {
    const size_t n = std::strlen(prefix);
    return len > n && Game::StrCmpI(s, prefix, static_cast<int>(n)) == 0;
}

// Quest title in the white of a tooltip header, objectives in the gold of
// tooltip body text. Both are already in the 0xAARRGGBB packing the
// line-color setter consumes.
constexpr uint32_t kTitleColor = UI::ColorData::ByTag("HIGHLIGHT_FONT_COLOR");
constexpr uint32_t kTextColor = UI::ColorData::ByTag("NORMAL_FONT_COLOR");

// Empties the tooltip at stack[1], so a link with no valid ID never leaves
// the previous tooltip's lines behind.
void ClearTooltip(void *L) {
    if (void *self = Game::Lua::ResolveTooltip(L))
        Tooltip::Lines::Clear(self);
}

// The quest query is only sent with a callback to run on the answer. The
// answer lands in the quest cache, where the next SetHyperlink reads it, so
// there is nothing to do here.
void __stdcall QuestLoaded(void *userData, int success) {
    (void)userData;
    (void)success;
}

// `quest:<questID>[:level]`, self at stack[1].
void ShowQuest(void *L, uint32_t questID) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return;
    Tooltip::Lines::Clear(self);
    if (questID == 0)
        return;

    const uint8_t *quest = Quest::Cache::Peek(questID);
    if (quest == nullptr) {
        Quest::Cache::Lookup(questID, reinterpret_cast<void *>(&QuestLoaded), nullptr,
                             /*unique=*/true);
        return;
    }

    const char *title = reinterpret_cast<const char *>(quest + Quest::Cache::OFF_TITLE);
    if (title[0] == '\0')
        return;
    Tooltip::Lines::Add(self, title, kTitleColor);

    const char *objectives =
        reinterpret_cast<const char *>(quest + Quest::Cache::OFF_OBJECTIVES);
    if (objectives[0] != '\0') {
        char text[Quest::Cache::EXPANDED_TEXT_SIZE];
        Quest::Cache::ExpandText(objectives, text, sizeof(text));
        Tooltip::Lines::Add(self, text, kTextColor, /*wrap=*/true);
    }

    Tooltip::Lines::Show(L);
}

int __fastcall SetHyperlink_h(void *L) {
    if (!Game::Lua::IsString(L, 2))
        return g_origSetHyperlink(L);

    const char *link = Game::Lua::ToString(L, 2);
    const char *payload = link;
    size_t len = std::strlen(link);
    if (const char *open = std::strstr(link, "|H")) {
        payload = open + 2;
        const char *close = std::strstr(payload, "|h");
        len = close != nullptr ? static_cast<size_t>(close - payload) : std::strlen(payload);
    }

    if (HasPrefix(payload, len, "spell:")) {
        // The spell builder clears the tooltip itself; ShowByID skips it
        // for an ID <= 0.
        const int spellID = std::atoi(payload + 6);
        if (spellID > 0)
            Spell::Tooltip::ShowByID(L, spellID);
        else
            ClearTooltip(L);
        return 0;
    }
    if (HasPrefix(payload, len, "enchant:")) {
        const int spellID = std::atoi(payload + 8);
        const uint8_t *record = Spell::Lookup::RecordForID(spellID);
        if (record != nullptr &&
            (Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
             Offsets::SPELL_ATTR_TRADESPELL) != 0) {
            Spell::Tooltip::ShowByID(L, spellID);
            return 0;
        }
    }
    if (HasPrefix(payload, len, "quest:")) {
        ShowQuest(L, static_cast<uint32_t>(std::strtoul(payload + 6, nullptr, 10)));
        return 0;
    }
    if (HasPrefix(payload, len, "trade:")) {
        TradeSkill::Link::Open(std::string(payload + 6, len - 6).c_str());
        return 0;
    }

    if (payload != link) {
        // Push the payload while the full link is still on the stack (the
        // push may collect garbage), move it to stack[2], drop the link.
        Game::Lua::PushLString(L, payload, len);
        Game::Lua::Insert(L, 2);
        Game::Lua::Remove(L, 3);
    }
    return g_origSetHyperlink(L);
}

const Game::HookAutoRegister _setHyperlinkHook{
    Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_HYPERLINK,
    reinterpret_cast<void *>(&SetHyperlink_h),
    reinterpret_cast<void **>(&g_origSetHyperlink)};

} // namespace

} // namespace Tooltip::Hyperlink
