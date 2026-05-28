// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Thin C++ wrapper over the Win32 Credential Manager API (advapi32.dll:
// CredWriteW / CredReadW / CredEnumerateW / CredDeleteW). Entries are
// stored under per-user, DPAPI-encrypted blobs that other Windows users
// on the same machine cannot read; any process running as the current
// user can decrypt them (DPAPI is per-user, not per-process), so this
// is "secure at rest", not "secure against same-user attackers".
//
// Every operation is scoped by the current engine `realmList` CVar
// (the realmlist address from `realmlist.wtf` — e.g.
// "logon.turtle-wow.org", not the friendly display name). The same
// account name on two different private servers therefore gets two
// distinct vault entries:
//     ClassicAPI/WoW/logon.turtle-wow.org/MYACCT
//     ClassicAPI/WoW/logon.other-server.com/MYACCT
// Users can inspect/wipe entries via
// `control /name Microsoft.CredentialManager` → Generic Credentials.
//
// When no realmList is set, every operation returns false / empty —
// there's no meaningful key to scope under.

namespace Security::CredStore {

// Save (or overwrite) the password for `accountName` under the current
// engine realm. Returns false if no realm is set, the account name is
// empty, the password is empty, or the OS rejects the write.
bool Save(const char *accountName, const char *password);

// Delete the saved entry for `accountName` under the current engine
// realm. Returns true if a matching entry existed and was deleted.
bool Delete(const char *accountName);

// One entry returned by `List()` — the account name plus the
// Windows-tracked `LastWritten` timestamp converted to Unix epoch
// seconds. The engine refreshes `LastWritten` on every `CredWriteW`,
// so `LoginWithSavedAccount` re-saves the credential after use to
// keep the timestamp current ("last written" == "last used").
struct Entry {
    std::string name;
    uint64_t lastUsedUnix;
};

// Enumerate every saved account for the current engine realm. Empty
// list if no realm is set or no entries exist.
std::vector<Entry> List();

// Load the password for `accountName` under the current engine realm
// into `outPassword`. The output is nul-terminated and the vector's
// `size()` reflects the password length (not including the trailing
// nul). Returns false if no realm is set or no such entry exists. The
// caller should `SecureZeroMemory(outPassword.data(), outPassword.size())`
// after use.
bool Load(const char *accountName, std::vector<char> &outPassword);

} // namespace Security::CredStore
