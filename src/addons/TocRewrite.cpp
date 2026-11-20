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

#include "addons/TocRewrite.h"

#include "addons/FlavorToc.h"
#include "addons/LoadGate.h"
#include "addons/LoadManagers.h"
#include "addons/Toc.h"
#include "baselib/Ascii.h"
#include "localization/Locale.h"

#include "Offsets.h"
#include "storm/Memory.h"
#include "turtle/Detect.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace AddOns::TocRewrite {

namespace {

using Localization::Locale::Client;

// 3.80.2's game-type names (FUN_141728630), matched case-insensitively.
struct GameTypeName {
    const char *name;
    uint32_t bit;
};
constexpr uint32_t GAME_TYPE_CLASSIC = 0x8;
constexpr uint32_t GAME_TYPE_VANILLA = 0x10;
// Not a Blizzard game type: ClassicAPI's name for a Turtle client.
constexpr uint32_t GAME_TYPE_TURTLE = 0x10000;
constexpr GameTypeName kGameTypes[] = {
    {"mainline", 0x1}, {"standard", 0x2}, {"plunderstorm", 0x4},
    {"classic", GAME_TYPE_CLASSIC}, {"vanilla", GAME_TYPE_VANILLA},
    {"tbc", 0x20}, {"wrath", 0x40}, {"cata", 0x80}, {"mists", 0x100},
    {"wowhack", 0x200}, {"turtle", GAME_TYPE_TURTLE},
};

// 3.80.2's AllowLoad words (FUN_141728530): one word, case-insensitive.
constexpr uint32_t ALLOW_LOAD_GLUE = 1;
constexpr uint32_t ALLOW_LOAD_GAME = 2;

bool IsListSeparator(char c) { return c == ' ' || c == ','; }

// Calls `fn(item, itemLen)` for every item of a space- or comma-separated
// list (FUN_141bc0300); stops early when `fn` returns true. Returns whether
// it stopped early.
template <class Fn> bool AnyItem(const char *list, size_t len, Fn &&fn) {
    size_t i = 0;
    while (i < len) {
        while (i < len && IsListSeparator(list[i])) ++i;
        const size_t start = i;
        while (i < len && !IsListSeparator(list[i])) ++i;
        if (i > start && fn(list + start, i - start)) return true;
    }
    return false;
}

bool ListContains(const char *list, size_t len, const char *token) {
    return AnyItem(list, len, [token](const char *item, size_t n) {
        return Ascii::EqualCI(item, n, token);
    });
}

uint32_t AllowLoadValue(const char *word, size_t len) {
    if (Ascii::EqualCI(word, len, "glue")) return ALLOW_LOAD_GLUE;
    if (Ascii::EqualCI(word, len, "game")) return ALLOW_LOAD_GAME;
    if (Ascii::EqualCI(word, len, "both")) return ALLOW_LOAD_GLUE | ALLOW_LOAD_GAME;
    return 0;
}

bool IsSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

void Trim(const char *&s, size_t &len) {
    while (len > 0 && IsSpace(*s)) { ++s; --len; }
    while (len > 0 && IsSpace(s[len - 1])) --len;
}

enum class Token { Unknown, AllowLoadGameType, ExcludeLoadGameType, AllowLoadTextLocale,
                   AllowLoad, AllowLoadEnvironment, LoadIntoEnvironment, Family, Game,
                   TextLocale };

// 3.80.2's token table (0x1444fab60): names compare with memcmp, so case
// matters.
Token Lookup(const char *name, size_t len) {
    struct Entry {
        const char *name;
        Token token;
    };
    static constexpr Entry kTokens[] = {
        {"ExcludeLoadGameType", Token::ExcludeLoadGameType},
        {"AllowLoadGameType", Token::AllowLoadGameType},
        {"AllowLoadEnvironment", Token::AllowLoadEnvironment},
        {"AllowLoadTextLocale", Token::AllowLoadTextLocale},
        {"AllowLoad", Token::AllowLoad},
        {"LoadIntoEnvironment", Token::LoadIntoEnvironment},
        {"Family", Token::Family},
        {"Game", Token::Game},
        {"TextLocale", Token::TextLocale},
    };
    for (const Entry &e : kTokens)
        if (std::strlen(e.name) == len && std::memcmp(e.name, name, len) == 0)
            return e.token;
    return Token::Unknown;
}

// 3.80.2's `## Interface:` value (FUN_141af3980): entries split on space or
// comma; each is an optional sign and the digits up to the first non-digit
// (none reads 0, overflow saturates). The client version when an entry
// equals it, else the largest entry below it (an unsigned compare, so a
// negative entry never counts), else 0.
uint32_t PickInterface(const char *v, size_t len, uint32_t client) {
    uint32_t best = 0;
    bool found = false;
    const bool exact = AnyItem(v, len, [&](const char *item, size_t n) {
        size_t i = 0;
        while (i < n && IsSpace(item[i])) ++i;
        bool negative = false;
        if (i < n && (item[i] == '+' || item[i] == '-')) negative = item[i++] == '-';
        uint64_t value = 0;
        for (; i < n && item[i] >= '0' && item[i] <= '9'; ++i) {
            value = value * 10 + static_cast<uint64_t>(item[i] - '0');
            if (value > 0xFFFFFFFFull) value = 0xFFFFFFFFull;
        }
        uint32_t entry = static_cast<uint32_t>(value);
        if (negative) entry = 0u - entry;
        if (entry == client) return true;
        if (entry < client && (!found || entry > best)) {
            best = entry;
            found = true;
        }
        return false;
    });
    return exact ? client : best;
}

// The client interface version the loadability check compares against
// (FUN_0051d7d0 -> 0x2BC0 = 11200). Read live so we mirror the engine's own
// value rather than hardcoding it.
uint32_t ClientInterfaceVersion() {
    using ClientIfaceVer_t = uint32_t(__cdecl *)();
    return reinterpret_cast<ClientIfaceVer_t>(
        static_cast<uintptr_t>(Offsets::FUN_ADDON_CLIENT_INTERFACE_VERSION))();
}

bool EvalCondition(Token token, const char *args, size_t argsLen, bool inAddonLoad) {
    switch (token) {
    case Token::AllowLoadGameType: {
        const uint32_t mask = GameTypeMask(args, argsLen);
        return mask == 0 || (mask & ClientGameTypes()) != 0;
    }
    case Token::ExcludeLoadGameType: {
        const uint32_t mask = GameTypeMask(args, argsLen);
        return mask == 0 || (mask & ClientGameTypes()) == 0;
    }
    case Token::AllowLoadTextLocale:
        return ListContains(args, argsLen, Client());
    case Token::AllowLoad:
        return (AllowLoadValue(args, argsLen) & ALLOW_LOAD_GAME) != 0;
    case Token::AllowLoadEnvironment:
        return inAddonLoad && ListContains(args, argsLen, "Global");
    case Token::LoadIntoEnvironment:
        return true;
    default:
        return true;
    }
}

const char *VariableValue(Token token) {
    switch (token) {
    case Token::Family: return "Classic";
    case Token::Game: return "Vanilla";
    case Token::TextLocale: return Client();
    default: return nullptr;
    }
}

// `## <key>:` at the start of a processed metadata line: returns the value
// (trimmed) via out-params. Keys compare case-insensitively, as 3.80.2's
// `_strnicmp` key matches do.
bool MetadataValue(const std::string &line, const char *key, const char **value,
                   size_t *valueLen, size_t *prefixLen) {
    size_t p = 2; // past "##"
    while (p < line.size() && Ascii::IsBlank(line[p])) ++p;
    const size_t klen = std::strlen(key);
    if (line.size() - p < klen + 1 || !Ascii::EqualCI(line.data() + p, klen, key) ||
        line[p + klen] != ':')
        return false;
    p += klen + 1;
    *prefixLen = p;
    const char *v = line.data() + p;
    size_t n = line.size() - p;
    Trim(v, n);
    *value = v;
    *valueLen = n;
    return true;
}

// The metadata 1.12's parser does not store, as 3.80.2's parser reads it.
struct TocInfo {
    uint32_t gameTypes = 0;                // `## AllowLoadGameType:` lines, OR'ed
    uint32_t allowLoad = ALLOW_LOAD_GAME;  // the last `## AllowLoad:` line
    std::vector<std::string> managers;     // `## LoadManagers:` lines, appended
    bool onlyBetaAndPtr = false;           // the last `## OnlyBetaAndPTR:` line
};

// One processed `##` line: rewrites `## Interface:` to its single value, turns
// a true `## AlwaysUpToDate:` into `## Interface: <client>` (3.80.2 sets the
// stored version to the client's), and collects the rest of TocInfo.
void ProcessMetadata(std::string &line, TocInfo &info) {
    const char *v = nullptr;
    size_t n = 0, prefix = 0;
    if (MetadataValue(line, "AlwaysUpToDate", &v, &n, &prefix)) {
        if (Toc::Truthy(v, n))
            line = "## Interface: " + std::to_string(ClientInterfaceVersion());
    } else if (MetadataValue(line, "OnlyBetaAndPTR", &v, &n, &prefix)) {
        info.onlyBetaAndPtr = Toc::Truthy(v, n);
    } else if (MetadataValue(line, "LoadManagers", &v, &n, &prefix)) {
        AnyItem(v, n, [&info](const char *item, size_t len) {
            info.managers.emplace_back(item, len);
            return false;
        });
    } else if (MetadataValue(line, "Interface", &v, &n, &prefix)) {
        const std::string value =
            std::to_string(PickInterface(v, n, ClientInterfaceVersion()));
        if (std::string(v, n) != value) {
            line.resize(prefix);
            line.push_back(' ');
            line += value;
        }
    } else if (MetadataValue(line, "AllowLoadGameType", &v, &n, &prefix)) {
        info.gameTypes |= GameTypeMask(v, n);
    } else if (MetadataValue(line, "AllowLoad", &v, &n, &prefix)) {
        info.allowLoad = AllowLoadValue(v, n);
    }
}

// Walk the whole TOC, keeping line terminators and any UTF-8 BOM.
void RebuildToc(const char *src, size_t srcLen, std::string &out, TocInfo &info) {
    out.clear();
    out.reserve(srcLen + 32);
    size_t i = 0;
    if (srcLen >= 3 && static_cast<uint8_t>(src[0]) == 0xEF &&
        static_cast<uint8_t>(src[1]) == 0xBB && static_cast<uint8_t>(src[2]) == 0xBF) {
        out.append(src, 3);
        i = 3;
    }
    std::string lineOut;
    while (i < srcLen) {
        const size_t lineStart = i;
        while (i < srcLen && src[i] != '\r' && src[i] != '\n') ++i;
        const char *line = src + lineStart;
        const size_t len = i - lineStart;

        const size_t termStart = i;
        if (i < srcLen && src[i] == '\r') ++i;
        if (i < srcLen && src[i] == '\n') ++i;

        size_t s = 0;
        while (s < len && IsSpace(line[s])) ++s;
        const bool metadata = len >= 2 && line[0] == '#' && line[1] == '#';
        if (!metadata && (s >= len || line[s] == '#')) {
            out.append(line, len); // blank or comment
        } else if (!ProcessLine(line, len, /*inAddonLoad=*/!metadata, lineOut)) {
            out.append("# ");      // dropped: a comment sets no key and loads no file
            out.append(line, len);
        } else {
            if (metadata) ProcessMetadata(lineOut, info);
            out.append(lineOut);
        }
        out.append(src + termStart, i - termStart);
    }
}

// True iff `path` is an addon `.toc` read (...\AddOns\...\*.toc). Gates the
// rewrite off every non-TOC read (a `.lua` with a `t[1]` subscript would
// otherwise be mangled) and off non-addon TOCs (FrameXML.toc, etc.).
bool IsAddonToc(const char *path) {
    const size_t len = std::strlen(path);
    if (len < 4) return false;
    if (!(path[len - 4] == '.' && Ascii::ToLower(path[len - 3]) == 't' &&
          Ascii::ToLower(path[len - 2]) == 'o' && Ascii::ToLower(path[len - 1]) == 'c'))
        return false;
    for (size_t i = 0; i + 8 <= len; ++i)
        if (Ascii::EqualCI(path + i, 8, "\\addons\\")) return true;
    return false;
}

} // namespace

bool ProcessLine(const char *line, size_t len, bool inAddonLoad, std::string &out) {
    out.clear();
    Trim(line, len);
    size_t i = 0;
    while (i < len) {
        if (line[i] != '[') {
            out.push_back(line[i++]);
            continue;
        }
        size_t close = i + 1;
        while (close < len && line[close] != ']') ++close;
        if (close >= len) { // unclosed '[': the rest is text
            out.append(line + i, len - i);
            break;
        }
        const char *inner = line + i + 1;
        const size_t innerLen = close - (i + 1);
        size_t h = 0;
        while (h < innerLen && !Ascii::IsBlank(inner[h])) ++h;
        const char *args = inner + h;
        size_t argsLen = innerLen - h;
        Trim(args, argsLen);

        const Token token = Lookup(inner, h);
        const char *value = VariableValue(token);
        if (value != nullptr && argsLen == 0) {
            out.append(value);
        } else if (token != Token::Unknown && value == nullptr) {
            if (!EvalCondition(token, args, argsLen, inAddonLoad)) return false;
        } else {
            out.append(line + i, (close + 1) - i); // unknown: stays as text
        }
        i = close + 1;
    }
    const char *s = out.data();
    size_t n = out.size();
    Trim(s, n);
    out = std::string(s, n);
    return true;
}

uint32_t GameTypeMask(const char *list, size_t len) {
    uint32_t mask = 0;
    AnyItem(list, len, [&mask](const char *item, size_t n) {
        for (const GameTypeName &g : kGameTypes)
            if (Ascii::EqualCI(item, n, g.name)) mask |= g.bit;
        return false;
    });
    return mask;
}

uint32_t ClientGameTypes() {
    return GAME_TYPE_CLASSIC | GAME_TYPE_VANILLA |
           (Turtle::Detected() ? GAME_TYPE_TURTLE : 0);
}

bool Transform(const char *path, void **outBuf, size_t *outSize) {
    if (path == nullptr || outBuf == nullptr || *outBuf == nullptr) return true;
    if (!IsAddonToc(path)) return true;

    const char *content = static_cast<const char *>(*outBuf);
    const size_t size =
        (outSize != nullptr && *outSize != 0) ? *outSize : std::strlen(content);

    std::string rebuilt;
    TocInfo info;
    RebuildToc(content, size, rebuilt, info);

    size_t prefixLen = 0;
    if (FlavorToc::BaseTocPrefix(path, &prefixLen)) {
        size_t nameStart = prefixLen;
        while (nameStart > 0 && path[nameStart - 1] != '\\' && path[nameStart - 1] != '/')
            --nameStart;
        LoadGate::Record(path + nameStart, prefixLen - nameStart, info.gameTypes,
                         info.allowLoad);
        LoadManagers::Record(path + nameStart, prefixLen - nameStart, info.managers);
    }

    // A live client (this one, always) removes an OnlyBetaAndPTR addon
    // (0x141af4b22): the TOC does not exist, so the folder never registers.
    if (info.onlyBetaAndPtr) {
        Storm::Free(*outBuf, __FILE__, __LINE__);
        *outBuf = nullptr;
        if (outSize != nullptr) *outSize = 0;
        return false;
    }

    if (rebuilt.size() == size && std::memcmp(rebuilt.data(), content, size) == 0)
        return true; // nothing changed: keep the engine's buffer

    const size_t newLen = rebuilt.size();
    void *buf = Storm::Alloc(newLen + 1, __FILE__, __LINE__); // +1 NUL (extraBytes=1)
    if (buf == nullptr) return true;                          // OOM: keep the original
    std::memcpy(buf, rebuilt.data(), newLen);
    static_cast<char *>(buf)[newLen] = '\0';

    Storm::Free(*outBuf, __FILE__, __LINE__);
    *outBuf = buf;
    if (outSize != nullptr) *outSize = newLen;
    return true;
}

} // namespace AddOns::TocRewrite
