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

#include "Account.h"

#include "Paths.h"

#include <cstdint>
#include <cstdio>
#include <fstream>

namespace Settings::Account {

namespace {

AutoRegister *g_head = nullptr;
std::string g_loadedAccount;
bool g_anyLoadAttempted = false;

void ResetAllSections() {
    for (auto *s = g_head; s != nullptr; s = s->next)
        s->reset();
}

// Distributes a parsed `key=value` pair to whichever section claims it
// first. Foreign keys (no section returns true) are dropped silently —
// matches the historical behavior of NameCache's settings parser.
void DispatchLine(const std::string &key, const std::string &value) {
    for (auto *s = g_head; s != nullptr; s = s->next)
        if (s->parse(key, value))
            return;
}

void RightTrim(std::string *s) {
    while (!s->empty()) {
        const char c = s->back();
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t')
            break;
        s->pop_back();
    }
}

} // namespace

AutoRegister::AutoRegister(ResetFn r, SerializeFn s, ParseFn p)
    : reset(r), serialize(s), parse(p), next(g_head) {
    g_head = this;
}

std::string Path() { return Paths::AccountFile("ClassicAPI.txt"); }

bool EnsureLoaded() {
    const std::string account = Paths::AccountName();
    if (account.empty())
        return false;
    if (g_anyLoadAttempted && g_loadedAccount == account)
        return true; // already current

    // Reset every section's state before re-populating — guarantees
    // the new account's defaults take effect even if its file is
    // silent on keys the previous account had set.
    ResetAllSections();
    g_loadedAccount = account;
    g_anyLoadAttempted = true;

    std::ifstream file(Path());
    if (!file.is_open())
        return true; // first run on this account; defaults stand

    std::string line;
    while (std::getline(file, line)) {
        RightTrim(&line);
        if (line.empty() || line[0] == '#')
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (key.empty())
            continue;
        DispatchLine(key, value);
    }
    return true;
}

bool Save() {
    const std::string path = Path();
    if (path.empty())
        return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return false;
        out << "# ClassicAPI account settings — auto-generated.\n";
        out << "# Hand-edits survive, though sections may be re-ordered on next save.\n";
        for (auto *s = g_head; s != nullptr; s = s->next)
            s->serialize(out);
        if (!out)
            return false;
    }
    // Win32: rename fails if destination exists. Remove first; the
    // file is small so the brief window without a valid file is
    // acceptable, and a crash here leaves the .tmp for the next
    // save's recovery path to potentially use.
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
        return false;
    return true;
}

} // namespace Settings::Account
