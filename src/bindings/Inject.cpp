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

// Inject ClassicAPI-owned bindings into a chosen engine section.
//
// Why a file-read hook rather than an addon-side Bindings.xml: vanilla
// stores every binding in a single linearly-indexed table, and the
// keybind UI walks that table in index order with `HEADER_*` pseudo-
// entries splitting groups. Addon Bindings.xml files get parsed
// AFTER FrameXML, so even with `header="TARGETING"` an addon binding
// receives a display index higher than every engine binding — it
// renders orphaned at the very bottom of the keybind list rather than
// under TARGETING.
//
// To land inside a specific section, the binding's display index has
// to fall between that section's `HEADER_*` entry and the next one.
// The cleanest way to arrange that is to splice the bindings into the
// FrameXML file the engine itself parses, so they take their indices
// from the same running counter every native binding does.
//
// What gets spliced is driven by `src/bindings/Bindings.xml`. The
// root `<Bindings header="X">` tag names the engine section to extend
// (e.g. `"TARGETING"`); the inner `<Binding>` children are spliced
// verbatim. The splicer locates `header="X"` in the engine file
// (which appears on the first binding of section X), then the next
// `header="..."` after that (start of the following section), and
// inserts our content just before that next-section marker — so our
// entries land at the tail of section X. If X is the last section in
// the file, the splice goes just before `</Bindings>`.
//
// The engine's binding parser is `FUN_004b6f70`. It calls
// `FUN_FILE_READ` (`0x00648620`) to load the file, then walks `<Binding>`
// elements assigning each the next display index from a running
// counter. By the time our injected bindings are processed, the
// engine has already emitted the section's `HEADER_*` and a sequence
// of native entries; ours append to that sequence and the next
// section's header follows.

#include "Inject.h"
#include "Offsets.h"

#include "embed/Resource.h"
#include "embedded_resources.h"

#include <cstdio>
#include <cstring>

namespace Bindings::Inject {

namespace {

constexpr const char *kFrameXMLBindingsPath = "Interface\\FrameXML\\Bindings.xml";

// Parsed view of `src/bindings/Bindings.xml`:
//   - `header` is the value of `header="..."` on the root `<Bindings>`
//     tag. Tells the splicer which engine section to extend.
//   - `innerData / innerSize` point at the bytes between the
//     `<Bindings ...>` opening and `</Bindings>` closing tags.
//
// All-zero fields mean "embedded buffer is malformed" — defensive,
// shouldn't happen since the source is build-time-validated. Splice
// is skipped (engine sees its original Bindings.xml unchanged).
struct EmbeddedConfig {
    char header[64];
    const char *innerData;
    size_t innerSize;
};

// Advances `p` past any `<!-- ... -->` comment block starting at the
// current position. Returns the new position (past the closing `-->`)
// if a comment was skipped, or `p` unchanged if no comment was at
// the current position. Caps at `end` if the comment is unterminated.
const char *SkipCommentIfAny(const char *p, const char *end) {
    if (p + 4 > end) return p;
    if (std::memcmp(p, "<!--", 4) != 0) return p;
    p += 4;
    while (p + 3 <= end) {
        if (std::memcmp(p, "-->", 3) == 0)
            return p + 3;
        ++p;
    }
    return end;
}

// Linear scan for `marker` in `[start, end)`, skipping any `<!--...-->`
// comment blocks (where unrelated occurrences of XML tags might live).
const char *FindOutsideComment(const char *start, const char *end, const char *marker) {
    const size_t markerLen = std::strlen(marker);
    const char *p = start;
    while (p + markerLen <= end) {
        const char *afterComment = SkipCommentIfAny(p, end);
        if (afterComment != p) {
            p = afterComment;
            continue;
        }
        if (std::memcmp(p, marker, markerLen) == 0)
            return p;
        ++p;
    }
    return nullptr;
}

// Parses `header="X"` out of the byte range `[tagStart, tagEnd)` (the
// opening `<Bindings ...>` tag). Copies the value into `out` (size
// `outSize`) and returns true on success.
bool ExtractHeaderAttr(const char *tagStart, const char *tagEnd,
                       char *out, size_t outSize) {
    static constexpr const char kAttr[] = "header=\"";
    constexpr size_t kAttrLen = sizeof(kAttr) - 1;
    for (const char *p = tagStart; p + kAttrLen < tagEnd; ++p) {
        if (std::memcmp(p, kAttr, kAttrLen) != 0) continue;
        const char *valStart = p + kAttrLen;
        const char *valEnd = valStart;
        while (valEnd < tagEnd && *valEnd != '"') ++valEnd;
        const size_t len = static_cast<size_t>(valEnd - valStart);
        if (len == 0 || len + 1 > outSize) return false;
        std::memcpy(out, valStart, len);
        out[len] = '\0';
        return true;
    }
    return false;
}

EmbeddedConfig ResolveEmbeddedConfig() {
    EmbeddedConfig out = {};
    const Embed::Bytes xml = Embed::Resource(Embed::Ids::kBindingsXml);
    if (xml.data == nullptr) return out;
    const char *content = reinterpret_cast<const char *>(xml.data);
    const char *end = content + xml.size;

    // Locate `<Bindings` opening tag (skipping any comment blocks
    // that mention the tag literally for documentation purposes).
    const char *open = FindOutsideComment(content, end, "<Bindings");
    if (open == nullptr) return out;

    // Find the closing `>` of the opening tag.
    const char *tagEnd = open;
    while (tagEnd < end && *tagEnd != '>') ++tagEnd;
    if (tagEnd == end) return out;

    // Pull out the `header="..."` attribute. Required — this tells
    // the splicer which engine section to extend.
    if (!ExtractHeaderAttr(open, tagEnd, out.header, sizeof(out.header)))
        return out;

    const char *innerStart = tagEnd + 1;
    const char *close = FindOutsideComment(innerStart, end, "</Bindings>");
    if (close == nullptr || close < innerStart) {
        out.header[0] = '\0';
        return out;
    }

    out.innerData = innerStart;
    out.innerSize = static_cast<size_t>(close - innerStart);
    return out;
}

// Computes the byte offset in the engine's Bindings.xml buffer where
// our entries should be spliced — i.e., the tail of the section
// identified by `headerName`. Strategy:
//
//   1. Find `header="<HEADER>"` in the engine's buffer (skipping
//      comments). This is the attribute the engine itself uses on
//      the FIRST binding of each section, so this finds where that
//      section *starts*.
//   2. Find the NEXT `header="` after that. That marks the start of
//      the FOLLOWING section.
//   3. Back up from that next-header position to the `<` of the
//      `<Binding` tag containing it — that's our splice point.
//   4. If there's no next header (last section in the file), splice
//      just before `</Bindings>`.
//
// Returns `nullptr` if step 1 fails (header missing from engine file
// — e.g., a heavily-modified FrameXML). Caller falls back to passing
// the original buffer through unchanged.
const char *FindSplicePosition(const char *content, size_t size,
                              const char *headerName) {
    const char *end = content + size;

    char headerNeedle[80];
    std::snprintf(headerNeedle, sizeof(headerNeedle),
                  "header=\"%s\"", headerName);

    const char *sectionStart = FindOutsideComment(content, end, headerNeedle);
    if (sectionStart == nullptr) return nullptr;

    // Walk past the matched attribute, then look for the next
    // `header="` anywhere later in the file.
    const char *afterCurrent = sectionStart + std::strlen(headerNeedle);
    const char *nextHeader = FindOutsideComment(afterCurrent, end, "header=\"");

    if (nextHeader != nullptr) {
        // Back up to the `<` of the `<Binding` tag that owns this
        // next-header attribute — that `<` is where the next section
        // visually starts, and we splice immediately before it.
        const char *p = nextHeader;
        while (p > content && *p != '<') --p;
        return p;
    }

    // Last section in the file — splice just before `</Bindings>`.
    return FindOutsideComment(content, end, "</Bindings>");
}

char NormalizeChar(char c) {
    if (c == '/') return '\\';
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c + 32);
    return c;
}

bool PathEqualsCI(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (NormalizeChar(*a) != NormalizeChar(*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

using SMemAlloc_t = void *(__stdcall *)(size_t size, const char *file, int line, int flags);
using SMemFree_t = void(__stdcall *)(void *buf, const char *file, int line, int flags);

} // namespace

bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn original) {
    if (path == nullptr || !PathEqualsCI(path, kFrameXMLBindingsPath))
        return false;

    // Pull the real file through the original reader with the caller's
    // requested `extraBytes` honored — the original allocates a Storm
    // buffer of `fileSize + extraBytes` and zero-fills the tail.
    void *origBuf = nullptr;
    size_t origSize = 0;
    const int readOk = original(unused, path, &origBuf, &origSize,
                                 extraBytes, flag1, flag2);
    if (readOk == 0 || origBuf == nullptr)
        return false;

    static const EmbeddedConfig config = ResolveEmbeddedConfig();
    if (config.innerData == nullptr || config.innerSize == 0 ||
        config.header[0] == '\0') {
        // Embedded XML is missing the `<Bindings header="...">` root
        // or the `</Bindings>` close tag. Pass the original through.
        if (outBuf != nullptr) *outBuf = origBuf;
        if (outSize != nullptr) *outSize = origSize;
        return true;
    }

    const char *content = static_cast<const char *>(origBuf);
    const char *splice = FindSplicePosition(content, origSize, config.header);
    if (splice == nullptr) {
        // Requested section isn't present in this engine file (heavily
        // modified FrameXML or different build). Pass through unchanged
        // rather than risking corruption.
        if (outBuf != nullptr) *outBuf = origBuf;
        if (outSize != nullptr) *outSize = origSize;
        return true;
    }

    const size_t splicePos = static_cast<size_t>(splice - content);
    const size_t newSize = origSize + config.innerSize;

    auto SMemAlloc = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC);
    auto SMemFree = reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE);

    void *newBuf = SMemAlloc(newSize + extraBytes, __FILE__, __LINE__, 0);
    if (newBuf == nullptr) {
        // Allocation failed — fall back to the original buffer so the
        // engine still loads its own bindings.
        if (outBuf != nullptr) *outBuf = origBuf;
        if (outSize != nullptr) *outSize = origSize;
        return true;
    }

    auto *dest = static_cast<char *>(newBuf);
    std::memcpy(dest, content, splicePos);
    std::memcpy(dest + splicePos, config.innerData, config.innerSize);
    std::memcpy(dest + splicePos + config.innerSize,
                content + splicePos, origSize - splicePos);
    if (extraBytes > 0)
        std::memset(dest + newSize, 0, extraBytes);

    SMemFree(origBuf, __FILE__, __LINE__, 0);

    if (outBuf != nullptr) *outBuf = newBuf;
    if (outSize != nullptr) *outSize = newSize;
    return true;
}

} // namespace Bindings::Inject
