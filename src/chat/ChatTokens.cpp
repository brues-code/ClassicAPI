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

// The `%` codes in sent chat, brought to 4.3.4's set.
//
// SendChatMessage runs its text through FUN_CHAT_PARSE_TEXT (4.3.4's
// ChatParserParseText) before the drunk filter. 1.12's knows `%t` / `%n`
// only: the target's name — the unit object's name, else its name-cache
// name, else TARGET_TOKEN_NOT_FOUND. 4.3.4's (QuestTextParser.cpp) has two
// cases, both resolved the same way and both prefixed `{rtN}` when the unit
// carries a raid marker and a name was found:
//
//   - `%t` / `%T` / `%n` / `%N` — the target (1.12 lacks only the prefix);
//   - `%f` / `%F` — the focus, or FOCUS_TOKEN_NOT_FOUND (1.12 lacks the code).
//
// The co-hook rewrites a copy of the text and hands it to the original: `%f`
// becomes the focus text, and a `%t` / `%n` pair gets the target's `{rtN}` in
// front of it, so the engine still produces the target's name. The copy walks
// `%` codes the way the original does — a code pair stays together, any other
// `%` consumes only itself — so text after a `%` is never read differently.
// The `{rtN}` prefix renders through Chat::RaidMarkers.

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

namespace Chat::ChatTokens {

namespace {

using Parse_t = bool(__fastcall *)(const char *text, char *out, int size);
Parse_t g_parseOriginal = nullptr;

const char *GlobalText(const char *name) {
    using GetText_t = const char *(__fastcall *)(const char *name, int edx, int gender);
    return reinterpret_cast<GetText_t>(
        static_cast<uintptr_t>(Offsets::FUN_FRAMESCRIPT_GET_TEXT))(name, -1, 0);
}

// A unit's name the way the `%t` case finds the target's: the unit object's
// name, else the name cache. Null when neither has one.
const char *UnitName(uint64_t guid) {
    if (guid == 0)
        return nullptr;
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

// `{rtN}` for a unit that carries raid marker N, else nothing.
void AppendMarker(std::string &out, uint64_t guid) {
    for (int mark = 1; mark <= Unit::RaidTarget::kMarkCount; ++mark) {
        if (Unit::RaidTarget::GetGUIDByMark(mark) == guid) {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "{rt%d}", mark);
            out += prefix;
            return;
        }
    }
}

bool IsMarked(uint64_t guid) {
    if (guid == 0)
        return false;
    for (int mark = 1; mark <= Unit::RaidTarget::kMarkCount; ++mark)
        if (Unit::RaidTarget::GetGUIDByMark(mark) == guid)
            return true;
    return false;
}

bool IsFocusCode(char c) { return c == 'f' || c == 'F'; }
bool IsTargetCode(char c) { return c == 't' || c == 'T' || c == 'n' || c == 'N'; }

uint64_t TargetGuid() {
    return Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO);
}

bool __fastcall Parse_h(const char *text, char *out, int size) {
    if (text == nullptr)
        return g_parseOriginal(text, out, size);

    // Leave the original's input untouched unless a code needs us: any `%f`,
    // or a `%t` / `%n` while the target is marked and named.
    const uint64_t target = TargetGuid();
    const bool targetPrefix = IsMarked(target) && UnitName(target) != nullptr;
    bool any = false;
    for (const char *p = std::strchr(text, '%'); p != nullptr; p = std::strchr(p + 1, '%'))
        if (IsFocusCode(p[1]) || (targetPrefix && IsTargetCode(p[1]))) {
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
            const uint64_t focus = Unit::Focus::Get();
            if (const char *name = UnitName(focus)) {
                AppendMarker(s, focus);
                s += name;
            } else {
                s += GlobalText("FOCUS_TOKEN_NOT_FOUND");
            }
            p += 2;
        } else if (IsTargetCode(c)) {
            if (targetPrefix)
                AppendMarker(s, target);
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

} // namespace Chat::ChatTokens
