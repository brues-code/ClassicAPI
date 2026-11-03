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

// `string.gsub` takes 5.1's string and table replacements.
//
// 1.12's str_gsub (0x7FD0E0) is Lua 5.0's, and differs from 5.1 in its
// replacement argument:
//   - it takes a string or a function, not a table;
//   - `%0` in a replacement string, and `%1` when the pattern has no
//     captures, raise "invalid capture index" (5.1: the whole match);
//   - a function that returns nil or false deletes the match (5.1 keeps it),
//     and one that returns a table deletes it too (5.1 raises an error).
//
// The first two only ever raised errors, so 5.1's behavior cannot break a
// 1.12 addon. The third does: SuperAPI's CombatText_AddMessage returns nil
// to delete the player's own " [0x...]" GUID from heal text. A function
// replacement therefore stays the engine's own add_s.
//
// str_gsub itself stays: its match loop, anchor, count and buffer are the
// same in both. Two parts are replaced:
//   - the argument gate (SITE_LUA_STR_GSUB_REPL_CHECK) becomes CheckReplacement;
//   - the one add_s call (CALL_LUA_STR_GSUB_ADD_S) goes to AddValue: 5.1's
//     add_s for a string or number, 5.1's table lookup for a table, and the
//     engine's add_s for a function. They use the engine's capture helpers.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace BaseLib::StringGsub {

namespace {

namespace Lua = Game::Lua;

// Lua 5.0's MatchState, as str_gsub builds it on its stack (str_format's
// spec scan reads the same layout: level at +0x0C, capture[0].len at +0x14).
struct MatchState {
    const char *srcInit;
    const char *srcEnd;
    void *L;
    int level;
    struct {
        const char *init;
        int len;
    } capture[32];
};

using add_s_t = void(__fastcall *)(MatchState *ms, void *b, const char *s, const char *e);
using push_onecapture_t = void(__fastcall *)(MatchState *ms, int i);
using luaL_addlstring_t = void(__fastcall *)(void *b, const char *s, unsigned int len);
using luaL_addvalue_t = void(__fastcall *)(void *b);

const auto EngineAddS = reinterpret_cast<add_s_t>(Offsets::FUN_LUA_STR_ADD_S);
const auto EnginePushOneCapture =
    reinterpret_cast<push_onecapture_t>(Offsets::FUN_LUA_STR_PUSH_ONECAPTURE);
const auto AddLString = reinterpret_cast<luaL_addlstring_t>(Offsets::LUAL_ADD_LSTRING);
const auto AddBufferValue = reinterpret_cast<luaL_addvalue_t>(Offsets::LUAL_ADD_VALUE);

// 5.1's push_onecapture: capture `i`, or the whole match for `i == 0` when
// the pattern has no captures.
void PushOneCapture(MatchState *ms, int i, const char *s, const char *e) {
    if (i < ms->level)
        EnginePushOneCapture(ms, i);
    else if (i == 0)
        Lua::PushLString(ms->L, s, static_cast<unsigned int>(e - s));
    else
        Lua::Error(ms->L, "invalid capture index");
}

// 5.1's add_s: a replacement string, where `%0` is the whole match and `%1`
// to `%9` are captures.
void AddString(MatchState *ms, void *b, const char *s, const char *e) {
    void *L = ms->L;
    const char *news = Lua::ToString(L, 3);
    const unsigned int l = Lua::StrLen(L, 3);
    for (unsigned int i = 0; i < l; i++) {
        if (news[i] != '%') {
            AddLString(b, news + i, 1);
            continue;
        }
        i++; // skip the escape
        const char c = news[i];
        if (c < '0' || c > '9') {
            AddLString(b, news + i, 1);
        } else if (c == '0') {
            AddLString(b, s, static_cast<unsigned int>(e - s));
        } else {
            PushOneCapture(ms, c - '1', s, e);
            AddBufferValue(b);
        }
    }
}

// 5.1's add_value for a string or table, the engine's add_s for a function.
// Called as add_s: __fastcall, ret 8.
void __fastcall AddValue(MatchState *ms, void *b, const char *s, const char *e) {
    void *L = ms->L;
    switch (Lua::Type(L, 3)) {
    case Lua::TYPE_FUNCTION:
        // A result that is not a string or number deletes the match.
        EngineAddS(ms, b, s, e);
        return;
    case Lua::TYPE_TABLE:
        break;
    default: // number or string
        AddString(ms, b, s, e);
        return;
    }
    PushOneCapture(ms, 0, s, e);
    Lua::GetTable(L, 3);
    if (!Lua::ToBoolean(L, -1)) {
        Lua::SetTop(L, -2);
        Lua::PushLString(L, s, static_cast<unsigned int>(e - s)); // keep the match
    } else if (!Lua::IsString(L, -1)) {
        Lua::Error(L, "invalid replacement value (a %s)", Lua::TypeName(L, Lua::Type(L, -1)));
    }
    AddBufferValue(b);
}

// 5.1's gate on the replacement argument. Called in place of the engine's,
// with ECX = L.
void __fastcall CheckReplacement(void *L) {
    const int t = Lua::Type(L, 3);
    if (t != Lua::TYPE_NUMBER && t != Lua::TYPE_STRING && t != Lua::TYPE_FUNCTION &&
        t != Lua::TYPE_TABLE)
        Lua::ArgError(L, 3, "string/function/table expected");
}

// Runs on every registration pass; a site already patched no longer matches
// and is skipped.
void Apply() {
    static const uint8_t kGate[60] = {
        0xE8, 0x0D, 0x5F, 0xEF, 0xFF, 0x83, 0xF8, 0x03, 0x7C, 0x21, 0xBA, 0x03,
        0x00, 0x00, 0x00, 0x8B, 0xCF, 0xE8, 0x9C, 0x63, 0xEF, 0xFF, 0x85, 0xC0,
        0x75, 0x22, 0xBA, 0x03, 0x00, 0x00, 0x00, 0x8B, 0xCF, 0xE8, 0x7C, 0x62,
        0xEF, 0xFF, 0x83, 0xF8, 0x06, 0x74, 0x11, 0x68, 0x84, 0x23, 0x88, 0x00,
        0xBA, 0x03, 0x00, 0x00, 0x00, 0x8B, 0xCF, 0xE8, 0x76, 0x76, 0xEF, 0xFF,
    };
    Common::ReplaceWithCall(Offsets::SITE_LUA_STR_GSUB_REPL_CHECK, kGate, sizeof kGate,
                            reinterpret_cast<const void *>(&CheckReplacement));
    Common::RedirectCall(Offsets::CALL_LUA_STR_GSUB_ADD_S, Offsets::FUN_LUA_STR_ADD_S,
                         reinterpret_cast<const void *>(&AddValue));
}

const Game::ModuleAutoRegister _autoreg{&Apply};
const Game::GlueModuleAutoRegister _glueAutoreg{&Apply};

} // namespace

} // namespace BaseLib::StringGsub
