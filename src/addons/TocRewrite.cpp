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

#include "addons/EngineIO.h"
#include "addons/TocRewrite.h"
#include "addons/Toc.h"

#include "Offsets.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace AddOns::TocRewrite {

namespace {

using AddOns::EngineIO::SMemAllocFn;
using AddOns::EngineIO::SMemFreeFn;
using AddOns::Toc::EqCI;
using AddOns::Toc::Lower;

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

// The client locale code ("enUS", "frFR", …) — the exact string
// GetLocale() returns (Offsets.h VAR_LOCALE_NAME_TABLE derivation).
const char *ClientLocale() {
    uint32_t idx = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    if (idx > 7) idx = 0; // slot 8 overreads the string pool — clamp
    const char *const *table = reinterpret_cast<const char *const *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_NAME_TABLE));
    const char *s = table[idx];
    return (s != nullptr) ? s : "enUS";
}

// True iff the comma-separated `list` (spaces allowed) contains `token` (CI).
bool ListContains(const char *list, size_t listLen, const char *token) {
    size_t i = 0;
    while (i < listLen) {
        while (i < listLen && (IsSpace(list[i]) || list[i] == ',')) ++i;
        const size_t start = i;
        while (i < listLen && list[i] != ',') ++i;
        size_t end = i; // [start, end) is one item, maybe with trailing space
        while (end > start && IsSpace(list[end - 1])) --end;
        if (end > start && EqCI(list + start, end - start, token)) return true;
    }
    return false;
}

// A single-word `[Token]` variable expansion, or nullptr if `head` is not
// a known variable. This 1.12 + ClassicAPI client is the Classic family,
// the Vanilla game type, and whatever the client text locale is.
const char *VariableValue(const char *head, size_t headLen) {
    if (EqCI(head, headLen, "Family")) return "Classic";
    if (EqCI(head, headLen, "Game")) return "Vanilla";
    if (EqCI(head, headLen, "TextLocale")) return ClientLocale();
    return nullptr;
}

// The client interface version the loadability check compares against
// (FUN_0051d7d0 -> 0x2BC0 = 11200). Read live so we mirror the engine's own
// value rather than hardcoding it.
using ClientIfaceVer_t = uint32_t(__cdecl *)();
uint32_t ClientInterfaceVersion() {
    return reinterpret_cast<ClientIfaceVer_t>(
        static_cast<uintptr_t>(Offsets::FUN_ADDON_CLIENT_INTERFACE_VERSION))();
}

// If `line` is a `## Interface:` directive whose value is a comma-list that
// contains the client version but does NOT lead with it, append a normalized
// `<prefix>: <clientVersion>` line to `out` and return true. Vanilla's parser
// reads only the first number, so without this a later 11200 is never seen and
// the addon is marked out of date. Returns false for any other line, and for
// an interface line that needs no change (single value, no client version, or
// client version already first) — the caller then copies the line verbatim.
bool TryRewriteInterfaceLine(const char *line, size_t len, std::string &out) {
    size_t s = 0;
    while (s < len && IsSpace(line[s])) ++s;
    if (s + 1 >= len || line[s] != '#' || line[s + 1] != '#') return false;
    size_t p = s + 2;
    while (p < len && IsSpace(line[p])) ++p;
    static const char kKey[] = "interface"; // matched case-insensitively
    const size_t klen = 9;
    if (p + klen > len) return false;
    for (size_t i = 0; i < klen; ++i)
        if (Lower(line[p + i]) != kKey[i]) return false;
    p += klen;
    while (p < len && IsSpace(line[p])) ++p;
    if (p >= len || line[p] != ':') return false;
    ++p;                       // past ':'
    const size_t prefixEnd = p; // "## Interface:" (author spacing preserved)

    const uint32_t client = ClientInterfaceVersion();
    long first = -1;
    bool hasComma = false, hasClient = false;
    size_t q = p;
    while (q < len) {
        while (q < len && (IsSpace(line[q]) || line[q] == ',')) {
            if (line[q] == ',') hasComma = true;
            ++q;
        }
        long v = 0;
        bool digit = false;
        while (q < len && line[q] >= '0' && line[q] <= '9') {
            v = v * 10 + (line[q] - '0');
            ++q;
            digit = true;
        }
        if (digit) {
            if (first < 0) first = v;
            if (static_cast<uint32_t>(v) == client) hasClient = true;
        } else if (q < len) {
            ++q; // skip a stray non-digit so the scan always advances
        }
    }
    if (!hasComma || !hasClient || first == static_cast<long>(client))
        return false; // single value / no client version / already first

    out.append(line, prefixEnd);
    out.push_back(' ');
    out += std::to_string(client);
    return true;
}

// Evaluate a `[keyword args]` condition — the return is whether the line should
// load. An unrecognized keyword returns false so the caller drops the line (a
// condition we cannot confirm must not load — fail safe).
bool EvalCondition(const char *head, size_t headLen, const char *args,
                   size_t argsLen) {
    if (EqCI(head, headLen, "AllowLoadGameType"))
        return ListContains(args, argsLen, "vanilla");
    if (EqCI(head, headLen, "AllowLoadTextLocale"))
        return ListContains(args, argsLen, ClientLocale());
    if (EqCI(head, headLen, "AllowLoad"))
        return ListContains(args, argsLen, "game"); // addon files: in-game only
    return false;
}

// Rewrite one file-reference line into `out`: expand `[Variable]` tokens,
// evaluate and strip `[Condition]` directives. Returns true to KEEP the
// line (all conditions passed); on true `out` holds the cleaned,
// right-trimmed path.
bool ProcessFileLine(const char *line, size_t len, std::string &out) {
    out.clear();
    bool keep = true;
    size_t i = 0;
    while (i < len) {
        const char c = line[i];
        if (c != '[') {
            out.push_back(c);
            ++i;
            continue;
        }
        size_t close = i + 1;
        while (close < len && line[close] != ']') ++close;
        if (close >= len) { // unterminated '[' — copy the rest verbatim
            out.append(line + i, len - i);
            break;
        }
        const char *inner = line + i + 1;
        const size_t innerLen = close - (i + 1);
        size_t h = 0;
        while (h < innerLen && !IsSpace(inner[h])) ++h; // [0,h) = head word
        size_t a = h;
        while (a < innerLen && IsSpace(inner[a])) ++a;  // skip separating ws
        const char *head = inner;
        const size_t headLen = h;
        const char *args = inner + a;
        const size_t argsLen = innerLen - a;

        if (argsLen == 0) {
            // Single word: a variable to expand, else leave verbatim (an
            // unknown token becomes a path the engine cannot open — the
            // file just does not load).
            const char *val = VariableValue(head, headLen);
            if (val != nullptr) out.append(val);
            else out.append(line + i, (close + 1) - i);
        } else {
            // Keyword + args: a condition. Strip it from the path (drop a
            // space we already emitted before it so nothing dangles).
            keep = keep && EvalCondition(head, headLen, args, argsLen);
            if (!out.empty() && out.back() == ' ') out.pop_back();
        }
        i = close + 1;
    }
    while (!out.empty() && IsSpace(out.back())) out.pop_back();
    return keep;
}

// Walk the whole TOC, preserving line terminators and any UTF-8 BOM.
// `#` comment and `##` metadata lines pass through verbatim; only file
// reference lines are rewritten. A gated-out line becomes a `#` comment
// so the load loop skips it (and a dumped TOC shows why it was dropped).
void RebuildToc(const char *src, size_t srcLen, std::string &out) {
    out.clear();
    out.reserve(srcLen + 32);
    size_t i = 0;
    if (srcLen >= 3 && static_cast<uint8_t>(src[0]) == 0xEF &&
        static_cast<uint8_t>(src[1]) == 0xBB &&
        static_cast<uint8_t>(src[2]) == 0xBF) {
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
        if (s >= len) {
            out.append(line, len); // blank
        } else if (line[s] == '#') {
            // Comment / `##` metadata — verbatim, except a multi-flavor
            // `## Interface:` list that hides the client version.
            if (!TryRewriteInterfaceLine(line, len, out))
                out.append(line, len);
        } else if (ProcessFileLine(line, len, lineOut)) {
            out.append(lineOut);
        } else {
            out.push_back('#'); // gated out — comment it so the loader skips it
            out.append(line, len);
        }
        out.append(src + termStart, i - termStart);
    }
}

// True iff `path` is an addon `.toc` read (…\AddOns\…\*.toc). Gates the
// rewrite off every non-TOC read (a `.lua` with a `t[1]` subscript would
// otherwise be mangled) and off non-addon TOCs (FrameXML.toc, etc.).
bool IsAddonToc(const char *path) {
    const size_t len = std::strlen(path);
    if (len < 4) return false;
    if (!(path[len - 4] == '.' && Lower(path[len - 3]) == 't' &&
          Lower(path[len - 2]) == 'o' && Lower(path[len - 1]) == 'c'))
        return false;
    for (size_t i = 0; i + 8 <= len; ++i)
        if (EqCI(path + i, 8, "\\addons\\")) return true;
    return false;
}

} // namespace

void Transform(const char *path, void **outBuf, size_t *outSize) {
    if (path == nullptr || outBuf == nullptr || *outBuf == nullptr) return;
    if (!IsAddonToc(path)) return;

    const char *content = static_cast<const char *>(*outBuf);
    const size_t size = (outSize != nullptr && *outSize != 0)
                            ? *outSize
                            : std::strlen(content);
    if (size == 0) return;

    // Fast path: no '[' (directives) and no ',' (a possible `## Interface:`
    // list) means nothing to do — leave the engine buffer alone.
    if (std::memchr(content, '[', size) == nullptr &&
        std::memchr(content, ',', size) == nullptr)
        return;

    std::string rebuilt;
    RebuildToc(content, size, rebuilt);
    if (rebuilt.size() == size &&
        std::memcmp(rebuilt.data(), content, size) == 0)
        return; // brackets/commas were only in unaffected lines — nothing changed

    auto SMemAlloc = reinterpret_cast<SMemAllocFn>(Offsets::FUN_STORM_SMEM_ALLOC);
    auto SMemFree = reinterpret_cast<SMemFreeFn>(Offsets::FUN_STORM_SMEM_FREE);
    const size_t newLen = rebuilt.size();
    void *buf = SMemAlloc(newLen + 1, __FILE__, __LINE__, 0); // +1 NUL (extraBytes=1)
    if (buf == nullptr) return;                               // OOM — keep the original
    std::memcpy(buf, rebuilt.data(), newLen);
    static_cast<char *>(buf)[newLen] = '\0';

    SMemFree(*outBuf, __FILE__, __LINE__, 0);
    *outBuf = buf;
    if (outSize != nullptr) *outSize = newLen;
}

} // namespace AddOns::TocRewrite
