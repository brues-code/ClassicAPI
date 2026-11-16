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

#pragma once

// Reading the combat log's and the movement file's lines: the stamp, and the
// comma-separated fields after it. File work only, no engine calls, so it
// runs on a worker thread.

#include <cstdint>
#include <string>
#include <string_view>

namespace Replay::Line {

// Calls `fn(line)` for each line of the file, without its line end, until
// `fn` returns false. False when the file cannot be opened.
template <typename Fn> bool ForEach(const std::string &path, Fn fn);

// The stamp at the start of `line` ("11/16/2026 11:12:48.139-6  BODY", the
// combat log's) as UTC milliseconds since 1970, and the body after it. False
// for a line without one.
bool Stamp(std::string_view line, int64_t &ms, std::string_view &body);

// The body's fields, split at commas outside quotes. A quoted field is given
// without its quotes and with its \" and \\ escapes undone.
class Fields {
public:
    explicit Fields(std::string_view body);

    int Count() const { return count_; }
    std::string_view operator[](int i) const { return i < count_ ? field_[i] : std::string_view(); }

    // A field as a number: 0 when it is not one. Hex takes "0x1f".
    int32_t Int(int i) const;
    uint32_t Hex(int i) const;
    float Float(int i) const;
    // A quoted field, unescaped; empty for nil.
    std::string Text(int i) const;
    // Whether the field was quoted (a name, not nil).
    bool Quoted(int i) const { return i < count_ && (quoted_ >> i) & 1; }
    // The body from field `i` on, for a list longer than the fields kept
    // (a path's points).
    std::string_view Rest(int i) const;

private:
    static constexpr int kMax = 64; // the fields kept; a longer line keeps its first 64
    std::string_view body_;
    std::string_view field_[kMax];
    uint64_t quoted_ = 0;
    int count_ = 0;
};

// Whether a line's body starts with `type` and a comma.
inline bool Is(std::string_view body, std::string_view type) {
    return body.size() > type.size() && body.compare(0, type.size(), type) == 0 &&
           body[type.size()] == ',';
}

namespace Detail {
bool ReadChunks(const std::string &path, bool (*sink)(void *ctx, std::string_view chunk, bool last),
                void *ctx);
}

template <typename Fn> bool ForEach(const std::string &path, Fn fn) {
    struct Ctx {
        Fn &fn;
        std::string carry;
        bool stopped = false;
    } ctx{fn};
    // Each chunk ends anywhere: the part after its last line end waits for
    // the next one.
    const auto sink = [](void *p, std::string_view chunk, bool last) -> bool {
        auto &c = *static_cast<Ctx *>(p);
        std::size_t begin = 0;
        for (;;) {
            const std::size_t end = chunk.find('\n', begin);
            if (end == std::string_view::npos)
                break;
            std::string_view line = chunk.substr(begin, end - begin);
            if (!c.carry.empty()) {
                c.carry.append(line.data(), line.size());
                line = c.carry;
            }
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            const bool go = c.fn(line);
            c.carry.clear();
            if (!go) {
                c.stopped = true;
                return false;
            }
            begin = end + 1;
        }
        c.carry.append(chunk.data() + begin, chunk.size() - begin);
        if (last && !c.carry.empty()) {
            std::string_view line = c.carry;
            if (line.back() == '\r')
                line.remove_suffix(1);
            c.fn(line);
        }
        return true;
    };
    return Detail::ReadChunks(path, sink, &ctx);
}

} // namespace Replay::Line
