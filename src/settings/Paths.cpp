// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// See `Paths.h`. The account / realm / character names each come from a
// different engine global, so the three reads live here together rather than
// once per module that wants a file path.

#include "Paths.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Settings::Paths {

namespace {

constexpr const char *kAccountRoot = "WTF\\Account\\";

const char *ReadAccountName() {
    return Game::Read<const char *>(Offsets::VAR_ACCOUNT_NAME_PTR);
}

const char *ReadRealmName() {
    auto *info = Game::Read<uint8_t *>(Offsets::VAR_REALM_INFO_PTR);
    if (info == nullptr)
        return nullptr;
    return Game::Read<const char *>(info, Offsets::OFF_REALM_INFO_NAME);
}

// The global is an inline buffer rather than a pointer, so "absent" reads as an
// empty string instead of null.
const char *ReadCharacterName() {
    auto *p = reinterpret_cast<const char *>(Offsets::VAR_CHARACTER_NAME);
    return (p[0] == '\0') ? nullptr : p;
}

bool Usable(const char *s) { return s != nullptr && s[0] != '\0'; }

} // namespace

std::string AccountName() {
    const char *account = ReadAccountName();
    return Usable(account) ? std::string(account) : std::string();
}

std::string AccountFile(const char *leaf) {
    const char *account = ReadAccountName();
    if (!Usable(account))
        return {};
    std::string out = kAccountRoot;
    out += account;
    out += '\\';
    out += leaf;
    return out;
}

std::string CharacterFile(const char *leaf) {
    const char *account = ReadAccountName();
    if (!Usable(account))
        return {};
    const char *realm = ReadRealmName();
    if (!Usable(realm))
        return {};
    const char *character = ReadCharacterName();
    if (!Usable(character))
        return {};
    std::string out = kAccountRoot;
    out += account;
    out += '\\';
    out += realm;
    out += '\\';
    out += character;
    out += '\\';
    out += leaf;
    return out;
}

} // namespace Settings::Paths
