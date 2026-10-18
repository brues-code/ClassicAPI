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

// `%f` in sent chat: the focus's name, as `%t` is the target's.
//
// SendChatMessage runs its text through FUN_CHAT_PARSE_TEXT (4.3.4's
// ChatParserParseText) before the drunk filter. 1.12's knows `%t` / `%n` only;
// 4.3.4's adds `%f` / `%F` (QuestTextParser.cpp): the focus unit's name, else
// its name-cache name, prefixed `{rtN}` when it carries a raid marker, or
// FOCUS_TOKEN_NOT_FOUND with no focus. The co-hook replaces `%f` in a copy of
// the text and hands that to the original, which then does `%t` as always.
// The copy walks `%` codes the way the original does — a `%t` / `%n` pair is
// one code, any other `%` consumes only itself — so text after a `%` is never
// read differently. The `{rtN}` prefix renders through Chat::RaidMarkers.

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "unit/Focus.h"
#include "unit/Identity.h"
#include "unit/RaidTarget.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace Chat::FocusToken {

namespace {

using Parse_t = bool(__fastcall *)(const char *text, char *out, int size);
Parse_t g_parseOriginal = nullptr;

const char *GlobalText(const char *name) {
    using GetText_t = const char *(__fastcall *)(const char *name, int edx, int gender);
    return reinterpret_cast<GetText_t>(
        static_cast<uintptr_t>(Offsets::FUN_FRAMESCRIPT_GET_TEXT))(name, -1, 0);
}

// The focus's name the way the `%t` case finds the target's: the unit
// object's name, else the name cache.
const char *FocusName(uint64_t guid) {
    if (void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0)) {
        using GetName_t = const char *(__thiscall *)(void *obj, int *outFlags);
        const char *name =
            reinterpret_cast<GetName_t>(static_cast<uintptr_t>(Offsets::FUN_OBJECT_GET_NAME))(
                unit, nullptr);
        if (name != nullptr && *name != '\0')
            return name;
    }
    return Unit::Identity::NameForGuid(guid);
}

void AppendFocus(std::string &out) {
    const uint64_t guid = Unit::Focus::Get();
    const char *name = (guid != 0) ? FocusName(guid) : nullptr;
    if (name == nullptr) {
        out += GlobalText("FOCUS_TOKEN_NOT_FOUND");
        return;
    }
    for (int mark = 1; mark <= Unit::RaidTarget::kMarkCount; ++mark) {
        if (Unit::RaidTarget::GetGUIDByMark(mark) == guid) {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "{rt%d}", mark);
            out += prefix;
            break;
        }
    }
    out += name;
}

bool IsFocusCode(char c) { return c == 'f' || c == 'F'; }

bool __fastcall Parse_h(const char *text, char *out, int size) {
    if (text == nullptr)
        return g_parseOriginal(text, out, size);
    bool any = false;
    for (const char *p = std::strchr(text, '%'); p != nullptr; p = std::strchr(p + 1, '%'))
        if (IsFocusCode(p[1])) {
            any = true;
            break;
        }
    if (!any)
        return g_parseOriginal(text, out, size);

    std::string s;
    for (const char *p = text; *p != '\0';) {
        if (*p != '%') {
            s += *p++;
            continue;
        }
        const char c = p[1];
        if (IsFocusCode(c)) {
            AppendFocus(s);
            p += 2;
        } else if (c == 't' || c == 'T' || c == 'n' || c == 'N') {
            s.append(p, 2); // the original's own code — keep the pair together
            p += 2;
        } else {
            s += *p++; // the original consumes only the '%' here
        }
    }
    return g_parseOriginal(s.c_str(), out, size);
}

const Game::HookAutoRegister _parseHook{Offsets::FUN_CHAT_PARSE_TEXT,
                                        reinterpret_cast<void *>(&Parse_h),
                                        reinterpret_cast<void **>(&g_parseOriginal)};

} // namespace

} // namespace Chat::FocusToken
