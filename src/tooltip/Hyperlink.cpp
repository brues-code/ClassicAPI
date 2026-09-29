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

// `GameTooltip:SetHyperlink(link)` takes a full chat link, and `spell:` and
// `quest:` links, as on 3.x clients.
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
//   - `quest:<questID>[:level]` shows the quest's title, then its objectives
//     text with the `$N`-style tokens expanded as the quest log shows them,
//     both from the quest cache. A quest the cache doesn't hold yet leaves
//     the tooltip empty and queues the server query the quest log would
//     make (once per quest - see Quest::Cache::Lookup's `unique`); the next
//     SetHyperlink after the answer shows it.
//   - Everything else - `item:`, `enchant:`, and the error for link types
//     vanilla has no data for (`achievement:`, `talent:`, ...) - is the
//     engine's own, run on the payload.
//
// Prefixes match case-insensitively, as the engine's own `item:` test does
// (FUN_SSTR_CMP_I).

#include "Game.h"
#include "Offsets.h"
#include "quest/Cache.h"
#include "spell/Tooltip.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Tooltip::Hyperlink {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using AddLine_t = void(__thiscall *)(void *self, const char *left, const char *right,
                                     const void *leftColor, const void *rightColor, int wrap);
using ClearTooltip_t = void(__fastcall *)(void *self);
using Substitute_t = void(__fastcall *)(const char *src, char *dst, uint32_t size,
                                        const uint64_t *playerGuid);

ScriptFn_t g_origSetHyperlink = nullptr;

// Script_GetQuestLogQuestText's buffer size for the expanded text.
constexpr uint32_t kQuestTextSize = 0x400;

bool HasPrefix(const char *s, size_t len, const char *prefix) {
    const size_t n = std::strlen(prefix);
    return len > n && _strnicmp(s, prefix, n) == 0;
}

// Left-aligned line via the engine's raw add-line. Its color arg points at a
// packed 0xAARRGGBB value (see FUN_GAMETOOLTIP_ADD_LINE); the local keeps the
// address valid across the call.
void AddLine(void *self, const char *text, double r, double g, double b, bool wrap) {
    auto ch = [](double v) { return static_cast<uint32_t>(v * 255.0 + 0.5); };
    const uint32_t color = 0xFF000000u | (ch(r) << 16) | (ch(g) << 8) | ch(b);
    reinterpret_cast<AddLine_t>(Offsets::FUN_GAMETOOLTIP_ADD_LINE)(
        self, text, nullptr, &color, nullptr, wrap ? 1 : 0);
}

// The quest query is only sent with a callback to run on the answer. The
// answer lands in the quest cache, where the next SetHyperlink reads it, so
// there is nothing to do here.
void __stdcall QuestLoaded(void *userData, int success) {
    (void)userData;
    (void)success;
}

// `quest:<questID>[:level]`, self at stack[1]. Title in the white header
// line, objectives in the gold of tooltip body text.
void ShowQuest(void *L, uint32_t questID) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return;
    reinterpret_cast<ClearTooltip_t>(Offsets::FUN_GAMETOOLTIP_CLEAR)(self);
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
    AddLine(self, title, 1.0, 1.0, 1.0, false);

    const char *objectives =
        reinterpret_cast<const char *>(quest + Quest::Cache::OFF_OBJECTIVES);
    if (objectives[0] != '\0') {
        char text[kQuestTextSize];
        const uint64_t guid = Unit::Identity::PlayerGuid();
        reinterpret_cast<Substitute_t>(Offsets::FUN_QUEST_TEXT_SUBSTITUTE)(
            objectives, text, sizeof(text), &guid);
        AddLine(self, text, 1.0, 0.82, 0.0, true);
    }

    // Script_Show reads self at stack[1].
    reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_FRAME_SHOW)(L);
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
        Spell::Tooltip::ShowByID(L, std::atoi(payload + 6));
        return 0;
    }
    if (HasPrefix(payload, len, "quest:")) {
        ShowQuest(L, static_cast<uint32_t>(std::strtoul(payload + 6, nullptr, 10)));
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
