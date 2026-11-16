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

#include "replay/Line.h"

#include <charconv>
#include <cstdio>
#include <memory>
#include <share.h>

namespace Replay::Line {

namespace {

// Reads digits at `p`, moving it past them. False when there are none.
bool Digits(std::string_view s, std::size_t &p, int &value) {
    const std::size_t start = p;
    value = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9')
        value = value * 10 + (s[p++] - '0');
    return p > start;
}

bool Expect(std::string_view s, std::size_t &p, char c) {
    if (p >= s.size() || s[p] != c)
        return false;
    ++p;
    return true;
}

// Days from 1970-01-01 to a civil date (Howard Hinnant's days_from_civil).
int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + doe - 719468;
}

} // namespace

namespace Detail {

bool ReadChunks(const std::string &path, bool (*sink)(void *ctx, std::string_view chunk, bool last),
                void *ctx) {
    // Shared, as the game keeps the file it writes open.
    FILE *file = _fsopen(path.c_str(), "rb", _SH_DENYNO);
    if (file == nullptr)
        return false;
    constexpr std::size_t kChunk = 1 << 20;
    const auto buffer = std::make_unique<char[]>(kChunk);
    for (;;) {
        const std::size_t read = std::fread(buffer.get(), 1, kChunk, file);
        const bool last = read < kChunk;
        if (!sink(ctx, std::string_view(buffer.get(), read), last) || last)
            break;
    }
    std::fclose(file);
    return true;
}

} // namespace Detail

bool Stamp(std::string_view line, int64_t &ms, std::string_view &body) {
    std::size_t p = 0;
    int month, day, year, hour, minute, second, milli, offset;
    if (!Digits(line, p, month) || !Expect(line, p, '/') || !Digits(line, p, day) ||
        !Expect(line, p, '/') || !Digits(line, p, year) || !Expect(line, p, ' ') ||
        !Digits(line, p, hour) || !Expect(line, p, ':') || !Digits(line, p, minute) ||
        !Expect(line, p, ':') || !Digits(line, p, second) || !Expect(line, p, '.') ||
        !Digits(line, p, milli) || p >= line.size())
        return false;
    const char sign = line[p++];
    if ((sign != '+' && sign != '-') || !Digits(line, p, offset))
        return false;
    if (sign == '-')
        offset = -offset;
    // Two spaces between the stamp and the body.
    if (!Expect(line, p, ' ') || !Expect(line, p, ' '))
        return false;
    const int64_t seconds = DaysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 +
                            second - static_cast<int64_t>(offset) * 3600;
    ms = seconds * 1000 + milli;
    body = line.substr(p);
    return true;
}

Fields::Fields(std::string_view body) : body_(body) {
    std::size_t p = 0;
    while (count_ < kMax) {
        if (p < body.size() && body[p] == '"') {
            quoted_ |= uint64_t{1} << count_;
            // A quoted field runs to the quote that is not escaped.
            std::size_t end = p + 1;
            while (end < body.size() && body[end] != '"')
                end += body[end] == '\\' ? 2 : 1;
            field_[count_++] = body.substr(p + 1, (end < body.size() ? end : body.size()) - p - 1);
            p = body.find(',', end);
        } else {
            const std::size_t end = body.find(',', p);
            field_[count_++] = body.substr(p, end == std::string_view::npos ? std::string_view::npos
                                                                            : end - p);
            p = end;
        }
        if (p == std::string_view::npos)
            break;
        ++p;
    }
}

std::string_view Fields::Rest(int i) const {
    if (i >= count_)
        return {};
    // A quoted field's view starts after its quote.
    std::size_t at = static_cast<std::size_t>(field_[i].data() - body_.data());
    if (Quoted(i))
        --at;
    return body_.substr(at);
}

int32_t Fields::Int(int i) const {
    const std::string_view f = (*this)[i];
    int32_t value = 0;
    std::from_chars(f.data(), f.data() + f.size(), value);
    return value;
}

uint32_t Fields::Hex(int i) const {
    std::string_view f = (*this)[i];
    if (f.size() > 2 && f[0] == '0' && (f[1] == 'x' || f[1] == 'X'))
        f.remove_prefix(2);
    uint32_t value = 0;
    std::from_chars(f.data(), f.data() + f.size(), value, 16);
    return value;
}

float Fields::Float(int i) const {
    const std::string_view f = (*this)[i];
    float value = 0.0f;
    std::from_chars(f.data(), f.data() + f.size(), value);
    return value;
}

std::string Fields::Text(int i) const {
    const std::string_view f = (*this)[i];
    if (f == "nil")
        return {};
    std::string text;
    text.reserve(f.size());
    for (std::size_t p = 0; p < f.size(); ++p) {
        if (f[p] == '\\' && p + 1 < f.size())
            ++p;
        text += f[p];
    }
    return text;
}

} // namespace Replay::Line
