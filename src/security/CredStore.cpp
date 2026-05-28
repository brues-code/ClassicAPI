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

#include "CredStore.h"

#include "Offsets.h"

#include <windows.h>
#include <wincred.h>

namespace Security::CredStore {

namespace {

constexpr const wchar_t kTargetPrefix[] = L"ClassicAPI/WoW/";
constexpr size_t kTargetPrefixLen =
    sizeof(kTargetPrefix) / sizeof(wchar_t) - 1;

// Cred Manager target-name max is 32767 wchars; vanilla WoW account
// names cap at 0x40 bytes (see VAR_GLUE_LOGIN_ATTEMPT prologue). We
// allow generous bounds for the realm + account, leaving the rest of
// the target-name budget unused.
constexpr size_t kMaxRealmWChars = 256;
constexpr size_t kMaxAccountWChars = 256;

using GetRealmList_t = const char *(__fastcall *)(int forceRefresh);

// Returns the current engine realmList CVar's value, or empty string
// if unset. This is the realmlist address (e.g.
// "logon.turtle-wow.org"), not the friendly display name — scoping
// credentials on the address means two private servers with the same
// display name still get distinct vault entries.
std::string CurrentRealm() {
    const char *r = reinterpret_cast<GetRealmList_t>(
        Offsets::FUN_CVAR_REALM_LIST)(0);
    if (!r || !*r) return {};
    return r;
}

// Append a UTF-8 string `s` to `out` as wide chars. No-op (and false
// return) on conversion error or empty input. `max` caps the wchar
// count we'll append.
bool AppendWide(std::wstring &out, const char *s, size_t max) {
    if (!s || !*s) return false;
    int needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (needed <= 1) return false;
    if (static_cast<size_t>(needed) > max) return false;
    size_t base = out.size();
    out.resize(base + needed - 1);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[base], needed);
    return true;
}

// Builds the per-account target name:
//     ClassicAPI/WoW/<realmName>/<accountName>
//
// Returns the bare prefix when the realm or account is missing —
// callers use `result.size() > kTargetPrefixLen` to detect that and
// abort.
std::wstring MakeTargetName(const char *accountName) {
    std::wstring out(kTargetPrefix, kTargetPrefixLen);
    std::string realm = CurrentRealm();
    if (realm.empty()) return out;
    if (!AppendWide(out, realm.c_str(), kMaxRealmWChars)) {
        out.resize(kTargetPrefixLen);
        return out;
    }
    out.push_back(L'/');
    if (!AppendWide(out, accountName, kMaxAccountWChars)) {
        out.resize(kTargetPrefixLen);
        return out;
    }
    return out;
}

// Builds the enumeration filter for the current realm:
//     ClassicAPI/WoW/<realmName>/*
//
// Returns empty wstring if no realm is set (callers treat that as
// "no entries to list").
std::wstring MakeEnumFilter() {
    std::wstring out(kTargetPrefix, kTargetPrefixLen);
    std::string realm = CurrentRealm();
    if (realm.empty()) return {};
    if (!AppendWide(out, realm.c_str(), kMaxRealmWChars)) return {};
    out.push_back(L'/');
    out.push_back(L'*');
    return out;
}

// Extracts the account-name suffix from a target-name during enum.
// Returns empty string if the structural shape doesn't match — the
// CredEnumerate filter should already restrict to our namespace, but
// hostile or stale entries from a previous version of this code could
// in theory land here.
std::string ExtractAccount(const wchar_t *targetName) {
    if (!targetName) return {};
    if (wcsncmp(targetName, kTargetPrefix, kTargetPrefixLen) != 0) {
        return {};
    }
    // After the prefix we expect `<realm>/<account>`. Skip past the
    // realm by finding the next '/'.
    const wchar_t *afterPrefix = targetName + kTargetPrefixLen;
    const wchar_t *sep = wcschr(afterPrefix, L'/');
    if (!sep || !sep[1]) return {};
    const wchar_t *account = sep + 1;
    int needed = WideCharToMultiByte(CP_UTF8, 0, account, -1,
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, account, -1, &out[0], needed,
                        nullptr, nullptr);
    return out;
}

} // namespace

bool Save(const char *accountName, const char *password) {
    if (!accountName || !*accountName) return false;
    if (!password || !*password) return false;

    std::wstring target = MakeTargetName(accountName);
    if (target.size() == kTargetPrefixLen) return false; // empty account

    size_t pwLen = strlen(password);
    if (pwLen == 0 || pwLen > CRED_MAX_CREDENTIAL_BLOB_SIZE) return false;

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(pwLen);
    cred.CredentialBlob =
        reinterpret_cast<LPBYTE>(const_cast<char *>(password));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    // UserName is required by some advapi32 builds to be non-NULL even
    // for generic credentials; reuse the target so the entry is
    // visually labeled correctly in `control /name
    // Microsoft.CredentialManager` even though we don't read it back.
    cred.UserName = const_cast<LPWSTR>(target.c_str());

    return CredWriteW(&cred, 0) != FALSE;
}

bool Delete(const char *accountName) {
    if (!accountName || !*accountName) return false;
    std::wstring target = MakeTargetName(accountName);
    if (target.size() == kTargetPrefixLen) return false;
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
}

namespace {

// FILETIME (100-ns intervals since 1601-01-01) → Unix epoch seconds.
// Returns 0 for zero/invalid input.
uint64_t FileTimeToUnix(FILETIME ft) {
    const uint64_t v = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
                       ft.dwLowDateTime;
    if (v == 0) return 0;
    // 11644473600 seconds between 1601-01-01 and 1970-01-01, expressed
    // in 100-ns ticks.
    constexpr uint64_t kEpochDiff100ns = 116444736000000000ULL;
    if (v < kEpochDiff100ns) return 0;
    return (v - kEpochDiff100ns) / 10000000ULL;
}

} // namespace

std::vector<Entry> List() {
    std::vector<Entry> result;

    // Enumeration filter is scoped to the current realm, so entries
    // for other private servers are invisible. The wildcard pattern
    // `ClassicAPI/WoW/<realm>/*` matches every account under it.
    std::wstring filter = MakeEnumFilter();
    if (filter.empty()) return result;

    DWORD count = 0;
    PCREDENTIALW *entries = nullptr;
    if (!CredEnumerateW(filter.c_str(), 0, &count, &entries)) {
        // ERROR_NOT_FOUND is the expected "no entries yet" outcome.
        return result;
    }

    result.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        if (!entries[i] || !entries[i]->TargetName) continue;
        std::string account = ExtractAccount(entries[i]->TargetName);
        if (account.empty()) continue;
        result.push_back({std::move(account),
                          FileTimeToUnix(entries[i]->LastWritten)});
    }
    CredFree(entries);
    return result;
}

bool Load(const char *accountName, std::vector<char> &outPassword) {
    outPassword.clear();
    if (!accountName || !*accountName) return false;

    std::wstring target = MakeTargetName(accountName);
    if (target.size() == kTargetPrefixLen) return false;

    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
        return false;
    }
    if (!cred || !cred->CredentialBlob || cred->CredentialBlobSize == 0) {
        if (cred) CredFree(cred);
        return false;
    }

    const DWORD len = cred->CredentialBlobSize;
    outPassword.resize(static_cast<size_t>(len) + 1, '\0');
    memcpy(outPassword.data(), cred->CredentialBlob, len);
    outPassword[len] = '\0';
    // size() should reflect the password length, not the trailing nul.
    outPassword.resize(len);

    // Zero the OS-returned buffer before freeing — CredFree itself
    // doesn't scrub. Safe even though the blob is a const-ish field,
    // because we own it until CredFree returns and the API never
    // reuses the allocation.
    SecureZeroMemory(cred->CredentialBlob, len);
    CredFree(cred);
    return true;
}

} // namespace Security::CredStore
