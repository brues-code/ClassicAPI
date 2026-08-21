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

#include "Offsets.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace AddOns::TocRewrite {

namespace {

// Storm allocator — the same buffers FUN_FILE_READ hands out, so the
// caller's normal SMemFree reclaims our replacement. `__stdcall` per the
// functions' RET 0x10 epilogue (matches addons/Embedded.cpp).
using SMemAlloc_t = void *(__stdcall *)(size_t size, const char *file, int line, int flags);
using SMemFree_t = void(__stdcall *)(void *buf, const char *file, int line, int flags);

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
bool IsSpace(char c) { return c == ' ' || c == '\t'; }

// Case-insensitive equality of [s, s+n) against the NUL-terminated `lit`.
bool EqCI(const char *s, size_t n, const char *lit) {
    for (size_t i = 0; i < n; ++i)
        if (lit[i] == '\0' || Lower(s[i]) != Lower(lit[i])) return false;
    return lit[n] == '\0';
}

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

// Evaluate a `[keyword args]` condition. `*known` reports whether the
// keyword is recognized; the return is whether the line should load. An
// unrecognized keyword returns false so the caller drops the line (a
// condition we cannot confirm must not load — fail safe).
bool EvalCondition(const char *head, size_t headLen, const char *args,
                   size_t argsLen, bool *known) {
    *known = true;
    if (EqCI(head, headLen, "AllowLoadGameType"))
        return ListContains(args, argsLen, "vanilla");
    if (EqCI(head, headLen, "AllowLoadTextLocale"))
        return ListContains(args, argsLen, ClientLocale());
    if (EqCI(head, headLen, "AllowLoad"))
        return ListContains(args, argsLen, "game"); // addon files: in-game only
    *known = false;
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
            bool known = false;
            keep = keep && EvalCondition(head, headLen, args, argsLen, &known);
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
        if (s >= len || line[s] == '#') {
            out.append(line, len); // blank / comment / metadata
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

    // Fast path: no '[' means no directives — leave the engine buffer alone.
    if (std::memchr(content, '[', size) == nullptr) return;

    std::string rebuilt;
    RebuildToc(content, size, rebuilt);
    if (rebuilt.size() == size &&
        std::memcmp(rebuilt.data(), content, size) == 0)
        return; // the `[` was only in metadata/comments — nothing changed

    auto SMemAlloc = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC);
    auto SMemFree = reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE);
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
