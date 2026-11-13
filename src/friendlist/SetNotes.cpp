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

// `C_FriendList.SetFriendNotes(name, notes)` / `SetFriendNotesByIndex(index,
// notes)` — write a client-side note for a friend (see `Notes.h`). Passing an
// empty / nil note clears it. Matches retail: the target must already be on
// the friends list, and there is no return value. Fires FRIENDLIST_UPDATE on
// change so the friends UI and note-aware addons refresh.

#include "FriendList.h"
#include "Notes.h"

#include "Game.h"
#include "event/Custom.h"

#include <cstdint>
#include <cstring>

namespace FriendList::SetNotes {

namespace {

// The friend entry whose name matches (case-insensitive), or null.
const uint8_t *FindByName(const char *name) {
    if (name == nullptr || *name == '\0')
        return nullptr;
    const int count = FriendList::Count();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = FriendList::EntryByIndex(i);
        if (entry == nullptr)
            break;
        const char *fname = *reinterpret_cast<const char *const *>(
            entry + FriendList::OFF_FRIEND_NAME);
        if (fname != nullptr && _stricmp(fname, name) == 0)
            return entry;
    }
    return nullptr;
}

void ApplyNote(const uint8_t *entry, const char *note) {
    if (entry == nullptr)
        return;
    const uint64_t guid = *reinterpret_cast<const uint64_t *>(
        entry + FriendList::OFF_FRIEND_GUID);
    const char *name = *reinterpret_cast<const char *const *>(
        entry + FriendList::OFF_FRIEND_NAME);
    if (Notes::Set(guid, name, note))
        Event::Custom::Fire(Event::Custom::LookupByName("FRIENDLIST_UPDATE"), "");
}

const Game::Doc::Field kSetFriendNotesArgs[] = {
    Game::Doc::Req("name", "cstring", "The friend's name; case does not matter."),
    Game::Doc::Opt("notes", "cstring", nullptr, "Empty or nil clears the note."),
};
const Game::Doc::Function kSetFriendNotes{
    "Sets the note for a friend by name; does nothing when the name is not a friend.",
    kSetFriendNotesArgs, {}};

int __fastcall Script_SetFriendNotes(void *L) {
    const char *name = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    const char *note = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    ApplyNote(FindByName(name), note);
    return 0;
}

const Game::Doc::Field kSetFriendNotesByIndexArgs[] = {
    Game::Doc::Req("index", "luaIndex"),
    Game::Doc::Opt("notes", "cstring", nullptr, "Empty or nil clears the note."),
};
const Game::Doc::Function kSetFriendNotesByIndex{
    "Sets the note for a friend by position in the friends list.", kSetFriendNotesByIndexArgs,
    {}};

int __fastcall Script_SetFriendNotesByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)); // 1-based
    const char *note = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    if (index >= 1 && index <= FriendList::Count())
        ApplyNote(FriendList::EntryByIndex(index - 1), note);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "SetFriendNotes",
                                     &Script_SetFriendNotes, &kSetFriendNotes);
    Game::Lua::RegisterTableFunction("C_FriendList", "SetFriendNotesByIndex",
                                     &Script_SetFriendNotesByIndex, &kSetFriendNotesByIndex);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FriendList::SetNotes
