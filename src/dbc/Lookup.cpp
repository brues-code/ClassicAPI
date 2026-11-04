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

#include "Lookup.h"

#include "Offsets.h"

namespace DBC {

namespace {

const char *NormalizeEmpty(const char *s) {
    return (s == nullptr || *s == '\0') ? nullptr : s;
}

} // namespace

// The id is compared unsigned: a DBC column's "none" is -1, which a signed
// compare passes as less than the count and then indexes records[-1]. The
// engine rejects it with a signed `jl` before its `jg` against the count
// (0x00472797). An unloaded store's count is -1, so it takes no id.
const uint8_t *Record(uintptr_t recordsVar, uintptr_t countVar, uint32_t id) {
    const int count = *reinterpret_cast<const int *>(countVar);
    if (id == 0 || count <= 0 || id > static_cast<uint32_t>(count))
        return nullptr;
    const uint8_t *const *records =
        *reinterpret_cast<const uint8_t *const *const *>(recordsVar);
    if (records == nullptr)
        return nullptr;
    return records[id];
}

const char *LocalizedField(uintptr_t recordsVar, uintptr_t countVar,
                           uint32_t id, int offset) {
    const uint8_t *record = Record(recordsVar, countVar, id);
    if (record == nullptr)
        return nullptr;
    const int locale = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    const char *const *names = reinterpret_cast<const char *const *>(record + offset);
    return NormalizeEmpty(names[locale]);
}

const char *StringField(uintptr_t recordsVar, uintptr_t countVar,
                        uint32_t id, int offset) {
    const uint8_t *record = Record(recordsVar, countVar, id);
    if (record == nullptr)
        return nullptr;
    return NormalizeEmpty(
        *reinterpret_cast<const char *const *>(record + offset));
}

} // namespace DBC
