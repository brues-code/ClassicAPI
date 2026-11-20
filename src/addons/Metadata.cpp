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

// Generic TOC metadata as the 3.80.2 client keeps it.
//
// 3.80.2's parser (FUN_141bbf710) stores the `##` keys that start with
// Title, Notes, Author, Version, IconTexture, IconAtlas, AddonCompartmentFunc
// (also ...OnEnter / ...OnLeave), Category, Group or X- in one per-addon store,
// matched case-insensitively, the last line winning. `C_AddOns.GetAddOnMetadata`
// (0x140b238e0) reads only that store. The AddOn list reads IconTexture,
// IconAtlas, Group and Category through it.
//
// 1.12's parser FUN_TOC_PARSER has the same store and rules, but its key test
// is a chain of prefix compares (Title, Notes, Author, Version, X-) that ends
// with the `X-` compare at 0x0051CCFA. That call is redirected: a key that
// starts with one of 3.80.2's added prefixes counts as a match, so it goes to
// the same store (0x0051D3C6) with 1.12's own locale-suffix and last-line
// rules.
//
// An addon with no (or an empty) `## Group:` gets one from 3.80.2's list
// builder (FUN_141af4d30): walking back from the addon through the earlier
// names of the sorted list while the first letter matches, the first name that
// is a prefix of this one, where either addon lists the other as a required or
// optional dependency, is its group. Otherwise the group is its own name.
// Computed here when GetAddOnMetadata asks for Group.

#include "addons/Registry.h"
#include "baselib/Ascii.h"
#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace AddOns::Metadata {

namespace {

constexpr const char *kUsage = "Usage: local value = C_AddOns.GetAddOnMetadata(name, variable)";

// 3.80.2's generic keys that 1.12's chain does not have, as prefixes.
constexpr const char *kAddedPrefixes[] = {
    "IconTexture", "IconAtlas", "AddonCompartmentFunc", "Category", "Group",
};

using StrCmpI_t = int(__stdcall *)(const char *a, const char *b, int maxLen);

// The parser's `X-` compare: 0 (a match) for `X-` and for each added prefix.
int __stdcall MatchGenericKey(const char *key, const char *prefix, int len) {
    auto cmp = reinterpret_cast<StrCmpI_t>(Offsets::FUN_SSTR_CMP_I);
    if (cmp(key, prefix, len) == 0) return 0;
    for (const char *p : kAddedPrefixes)
        if (cmp(key, p, static_cast<int>(std::strlen(p))) == 0) return 0;
    return 1;
}

using GetMetadata_t = const char *(__fastcall *)(const char *name, const char *key);
using GetByIndex_t = const char *(__fastcall *)(int idx0Based);
using GetCount_t = int(__fastcall *)();
using ResolveReqDeps_t = uint8_t *(__fastcall *)(const char *name);

const uint8_t *RecordOf(const char *name) {
    uint8_t *reqDesc = reinterpret_cast<ResolveReqDeps_t>(Offsets::FUN_ADDON_RESOLVE_REQ_DEPS)(name);
    return reqDesc != nullptr ? reqDesc - Offsets::OFF_ADDON_REQDEPS_DESC : nullptr;
}

bool ListsName(const uint8_t *record, size_t countOff, size_t arrayOff, const char *name) {
    const uint32_t count = *reinterpret_cast<const uint32_t *>(record + countOff);
    const char *const *names = *reinterpret_cast<const char *const *const *>(record + arrayOff);
    for (uint32_t i = 0; i < count && names != nullptr; ++i)
        if (names[i] != nullptr && Ascii::EqualCI(names[i], name)) return true;
    return false;
}

bool DependsOn(const uint8_t *record, const char *name) {
    return ListsName(record, Offsets::OFF_ADDON_REQDEPS_COUNT, Offsets::OFF_ADDON_REQDEPS_ARRAY,
                     name) ||
           ListsName(record, Offsets::OFF_ADDON_OPTIONALDEPS_COUNT,
                     Offsets::OFF_ADDON_OPTIONALDEPS_ARRAY, name);
}

bool LessCI(const char *a, const char *b) {
    for (;; ++a, ++b) {
        const char x = Ascii::ToLower(*a), y = Ascii::ToLower(*b);
        if (x != y || x == '\0') return x < y;
    }
}

std::string AutoGroup(const uint8_t *record, const char *name) {
    const int count = reinterpret_cast<GetCount_t>(Offsets::FUN_ADDON_GET_COUNT)();
    auto byIndex = reinterpret_cast<GetByIndex_t>(Offsets::FUN_ADDON_GET_BY_INDEX);
    std::vector<const char *> names;
    for (int i = 0; i < count; ++i)
        if (const char *n = byIndex(i)) names.push_back(n);
    std::stable_sort(names.begin(), names.end(), LessCI);

    const auto self = std::find_if(names.begin(), names.end(),
                                   [name](const char *n) { return Ascii::EqualCI(n, name); });
    if (self == names.end()) return name;
    const char first = Ascii::ToLower(name[0]);
    for (auto it = self; it != names.begin();) {
        const char *earlier = *--it;
        if (Ascii::ToLower(earlier[0]) != first) break;
        if (!Ascii::StartsWithCI(name, earlier)) continue;
        const uint8_t *other = RecordOf(earlier);
        if (DependsOn(record, earlier) || (other != nullptr && DependsOn(other, name)))
            return earlier;
    }
    return name;
}

// `C_AddOns.GetAddOnMetadata(name, variable)`.
int __fastcall Script_GetAddOnMetadata(void *L) {
    const uint8_t *record = Addons::ResolveRecord(L, kUsage);
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, kUsage);
        return 0;
    }
    if (record == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const char *name =
        *reinterpret_cast<const char *const *>(record + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
    const char *key = Game::Lua::ToString(L, 2);
    const char *value =
        reinterpret_cast<GetMetadata_t>(Offsets::FUN_ADDON_GET_METADATA)(name, key);
    if ((value == nullptr || *value == '\0') && Ascii::EqualCI(key, "Group")) {
        const std::string group = AutoGroup(record, name);
        Game::Lua::PushString(L, group.c_str());
        return 1;
    }
    if (value == nullptr)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushString(L, value);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name."),
    Game::Doc::Req("variable", "cstring",
                   "The TOC key, such as \"Title\", \"Version\", \"IconTexture\" or "
                   "\"X-Website\". Not case-sensitive."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("value", "cstring", nullptr,
                   "The value of that key; nil when the TOC has no such key or no addon has "
                   "that name. Group, when the TOC has none, is the name of the addon that "
                   "this one belongs to, or its own name."),
};
const Game::Doc::Function kGetAddOnMetadata{"Returns a metadata value from an addon's TOC file.",
                                            kArgs, kRets};

void PatchParser() {
    Common::RedirectCall(Offsets::CALL_TOC_PARSER_MATCH_X_PREFIX, Offsets::FUN_SSTR_CMP_I,
                         reinterpret_cast<const void *>(&MatchGenericKey));
}

void RegisterLuaFunctions() {
    PatchParser();
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnMetadata", &Script_GetAddOnMetadata,
                                     &kGetAddOnMetadata);
}

// The registration scan reads TOCs at the glue screen, before the in-game
// pass.
const Game::GlueModuleAutoRegister _gluePatch{&PatchParser};
const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AddOns::Metadata
