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

// `AddOrRemoveFriend(name[, notes])` — 4.3.4's Script_AddOrRemoveFriend
// (0x004D6910) → FriendList::AddOrRemoveFriend: remove the friend whose name
// matches without case, else add `name` with `notes`. No usage error; a
// non-string name does nothing.
//
// 4.3.4 matches each slot's GUID through the name cache; 1.12 keeps the name
// inline in the slot, and its own RemoveFriendByName (0x005AE6F0) matches that
// with SStrCmpI over all 50 slots, skipping null names — mirrored here. The
// two outcomes are the engine's own senders: SendRemoveFriend(guid), where
// 4.3.4 calls RemoveFriend(guid), and FriendList::AddFriend(name), what Lua
// AddFriend calls.
//
// 1.12's CMSG_ADD_FRIEND carries no note, so the note goes to the client-side
// store (`Notes.h`), which is keyed by GUID — and the GUID only exists once the
// server adds the friend. The note waits by name until the engine names a
// friend slot: FriendList::SetFriendName is the one writer of a slot's name, and
// its two call sites (cached name, and the name-cache callback) are redirected
// here. The add reply itself can't be paired with the request: tortoise-wow
// sends none for an unknown name, so a reply queue would drift.

#include "FriendList.h"
#include "Notes.h"

#include "Common.h"
#include "Game.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace FriendList::AddOrRemove {

namespace {

// {name, note} for adds whose friend has no name yet. Cleared at glue boot.
std::vector<std::pair<std::string, std::string>> g_pendingNotes;

const uint8_t *FindByName(const char *name) {
    for (int i = 0; i < MAX_FRIENDS; ++i) {
        const uint8_t *entry = EntryByIndex(i);
        if (entry == nullptr)
            return nullptr;
        const char *fname = *reinterpret_cast<const char *const *>(entry + OFF_FRIEND_NAME);
        if (fname != nullptr && Game::StrCmpI(fname, name) == 0)
            return entry;
    }
    return nullptr;
}

void QueueNote(const char *name, const char *notes) {
    for (auto &pending : g_pendingNotes) {
        if (Game::StrCmpI(pending.first.c_str(), name) == 0) {
            pending.second = notes;
            return;
        }
    }
    g_pendingNotes.emplace_back(name, notes);
}

void __fastcall OnSetFriendName(void *social, void *edx, uint64_t guid, const char *name) {
    using SetNameFn = void(__fastcall *)(void *social, void *edx, uint64_t guid, const char *name);
    reinterpret_cast<SetNameFn>(static_cast<uintptr_t>(Offsets::FUN_FRIEND_LIST_SET_FRIEND_NAME))(
        social, edx, guid, name);

    if (g_pendingNotes.empty() || name == nullptr || !IsGuidFriend(guid))
        return;
    for (auto it = g_pendingNotes.begin(); it != g_pendingNotes.end(); ++it) {
        if (Game::StrCmpI(it->first.c_str(), name) == 0) {
            // The engine sorts and signals FRIENDLIST_UPDATE after this.
            Notes::Set(guid, name, it->second.c_str());
            g_pendingNotes.erase(it);
            return;
        }
    }
}

int __fastcall Script_AddOrRemoveFriend(void *L) {
    const char *name = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (name == nullptr)
        return 0;

    if (const uint8_t *entry = FindByName(name)) {
        using SendRemoveFn = void(__fastcall *)(const void *social, void *edx, uint64_t guid);
        reinterpret_cast<SendRemoveFn>(static_cast<uintptr_t>(Offsets::FUN_FRIEND_LIST_SEND_REMOVE_FRIEND))(
            Social(), nullptr, *reinterpret_cast<const uint64_t *>(entry + OFF_FRIEND_GUID));
        return 0;
    }

    const char *notes = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    if (notes != nullptr && *notes != '\0')
        QueueNote(name, notes);
    using AddFn = void(__fastcall *)(const void *social, void *edx, const char *name);
    reinterpret_cast<AddFn>(static_cast<uintptr_t>(Offsets::FUN_FRIEND_LIST_ADD_FRIEND))(
        Social(), nullptr, name);
    return 0;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("name", "string", "Character name."),
    Game::Doc::Opt("notes", "string", nullptr, "Note for the friend when added."),
};
const Game::Doc::Function kAddOrRemoveFriend{
    "Removes the friend with this name, or adds the name as a friend when it is not on the list.",
    kArgs, {}, "FriendListGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("AddOrRemoveFriend", &Script_AddOrRemoveFriend,
                                      &kAddOrRemoveFriend);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Every glue boot: pending notes belong to the character that queued them.
void OnGlueBoot() {
    g_pendingNotes.clear();
    Common::RedirectCall(Offsets::SITE_FRIEND_STATUS_SET_NAME, Offsets::FUN_FRIEND_LIST_SET_FRIEND_NAME,
                         reinterpret_cast<const void *>(&OnSetFriendName));
    Common::RedirectCall(Offsets::SITE_FRIEND_NAME_LOOKUP_SET_NAME, Offsets::FUN_FRIEND_LIST_SET_FRIEND_NAME,
                         reinterpret_cast<const void *>(&OnSetFriendName));
}

const Game::GlueModuleAutoRegister _glueAutoreg{&OnGlueBoot};

} // namespace

} // namespace FriendList::AddOrRemove
