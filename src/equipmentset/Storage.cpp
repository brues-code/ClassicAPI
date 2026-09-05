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

#include "Storage.h"

#include "settings/Paths.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace EquipmentSet::Storage {

namespace {

// Removes trailing CR/LF/whitespace. Names from the engine are ASCII
// only (the chat input filter rejects high bytes) so we don't have to
// think about multi-byte trailing whitespace.
void RightTrim(std::string *s) {
    while (!s->empty()) {
        const char c = s->back();
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t')
            break;
        s->pop_back();
    }
}

// Parses `"key=value"` from the rest of the line. Returns true on
// match, fills `*value`. Whitespace around `=` is tolerated.
bool ParseField(const std::string &line, const std::string &key, std::string *value) {
    auto eq = line.find('=');
    if (eq == std::string::npos)
        return false;
    std::string k = line.substr(0, eq);
    while (!k.empty() && (k.back() == ' ' || k.back() == '\t'))
        k.pop_back();
    if (k != key)
        return false;
    std::string v = line.substr(eq + 1);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
        v.erase(v.begin());
    *value = v;
    return true;
}

} // namespace

std::string ResolveFilePath() {
    return Settings::Paths::CharacterFile("ClassicAPI_EquipmentSets.txt");
}

bool Load(const std::string &path, std::vector<Set> *outSets) {
    outSets->clear();
    std::ifstream file(path);
    if (!file.is_open())
        return true; // first run on this character — empty list is OK

    std::string line;
    Set *current = nullptr;
    while (std::getline(file, line)) {
        RightTrim(&line);
        // Trim leading whitespace too (we indent slot/name/icon lines
        // under their parent set for readability).
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
            ++start;
        if (start > 0)
            line.erase(0, start);

        if (line.empty() || line[0] == '#')
            continue;

        // `set N` — start a new set with ID N.
        if (line.rfind("set ", 0) == 0) {
            outSets->emplace_back();
            current = &outSets->back();
            const long id = std::strtol(line.c_str() + 4, nullptr, 10);
            current->setID = static_cast<uint32_t>(id > 0 ? id : 0);
            current->icon = DefaultIcon();
            for (int i = 0; i < SLOT_COUNT; ++i) {
                current->items[i] = GUID_EMPTY;
                current->itemIDs[i] = 0;
            }
            continue;
        }

        if (current == nullptr)
            continue;

        std::string value;
        if (ParseField(line, "name", &value)) {
            if (value.size() > MAX_NAME_LEN)
                value.resize(MAX_NAME_LEN);
            current->name = std::move(value);
            continue;
        }
        if (ParseField(line, "icon", &value)) {
            current->icon = std::move(value);
            continue;
        }

        // Slot line: `slot N guid=0xHEX [item=ITEMID]` or `slot N ignored`.
        // The `item=` field is optional — pre-itemID files don't have it.
        // Backward compat: missing `item=` leaves itemIDs[idx] = 0, which
        // the tooltip's missing-items path treats as "name unknown".
        if (line.rfind("slot ", 0) == 0) {
            char *endp = nullptr;
            const long slot1Based = std::strtol(line.c_str() + 5, &endp, 10);
            if (slot1Based < 1 || slot1Based > SLOT_COUNT)
                continue;
            const int idx = static_cast<int>(slot1Based) - 1;
            const char *rest = endp;
            while (*rest == ' ' || *rest == '\t')
                ++rest;
            if (std::string(rest).rfind("ignored", 0) == 0) {
                current->items[idx] = GUID_IGNORED;
                continue;
            }
            // Parse `guid=0x...` and optional `item=N`. Both keys can
            // appear in any order — `ParseField` finds the first `=`,
            // so we extract one key at a time by clipping the substring
            // around each one.
            const std::string r(rest);
            const auto guidPos = r.find("guid=");
            if (guidPos != std::string::npos) {
                const char *p = r.c_str() + guidPos + 5;
                current->items[idx] = std::strtoull(p, nullptr, 0);
            }
            const auto itemPos = r.find("item=");
            if (itemPos != std::string::npos) {
                const char *p = r.c_str() + itemPos + 5;
                current->itemIDs[idx] =
                    static_cast<uint32_t>(std::strtoul(p, nullptr, 0));
            }
        }
    }
    return true;
}

bool Save(const std::string &path, const std::vector<Set> &sets) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return false;
        out << "# ClassicAPI Equipment Sets v1\n";
        out << "# Written by ClassicAPI. Hand-edits survive but lose comments.\n";
        for (const auto &s : sets) {
            out << "set " << s.setID << "\n";
            out << "  name=" << s.name << "\n";
            out << "  icon=" << s.icon << "\n";
            for (int i = 0; i < SLOT_COUNT; ++i) {
                const uint64_t g = s.items[i];
                if (g == GUID_EMPTY)
                    continue;
                out << "  slot " << (i + 1) << " ";
                if (g == GUID_IGNORED) {
                    out << "ignored\n";
                } else {
                    char buf[24];
                    std::snprintf(buf, sizeof(buf), "guid=0x%016llX",
                                  static_cast<unsigned long long>(g));
                    out << buf;
                    if (s.itemIDs[i] != 0)
                        out << " item=" << s.itemIDs[i];
                    out << "\n";
                }
            }
        }
        if (!out)
            return false;
    }
    // Win32: rename fails if destination exists. Remove first; the
    // file is small so the brief window without a valid file on disk
    // is acceptable (and crashing here just preserves the prior copy
    // via the .tmp on the next save).
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
        return false;
    return true;
}

} // namespace EquipmentSet::Storage
