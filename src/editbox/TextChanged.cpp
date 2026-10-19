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

// `OnTextChanged(self, userInput)`.
//
// 4.3.4 runs OnTextChanged with one argument, true when the change came from
// the user and false when SetText made it. The answer is dirty bit 0x8: an
// insert or delete passed `script` = 0 sets it, an insert with `script` = 1
// clears it (a delete with 1 leaves it), and the fire clears it after the
// script ran. Only SetText (SetNumber goes through it) passes 1, for its
// delete-all and its insert; typing, pasting, deleting, the input method and
// Lua's Insert all pass 0.
//
// 1.12 fires OnTextChanged with no argument and has no `script` argument to
// carry. The bit is kept here, per edit box, at the same points: the class
// rebuild at the end of each insert that went ahead (its one call,
// redirected; EditBox::CharClass owns the rebuild itself), and each delete. An
// insert or a clear reached from SetText's internal is the `script` = 1 case,
// told apart by its return address. The fire block in the text update is
// replaced by a call that runs the script with the bit, as 4.3.4's
// RunOnTextChangedScript does, then clears it.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "frame/ScriptArgs.h"

#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <unordered_set>

namespace EditBox::TextChanged {

namespace {

// Edit boxes whose last text change came from the user (4.3.4's bit 0x8).
std::unordered_set<const void *> g_userInput;

// The edit box SetText's internal is inserting into or clearing right now.
const void *g_scripted = nullptr;

using Insert_t = void(__fastcall *)(void *editBox, void *edx, const char *text, const char *context, int runScripts,
                                    int highlight);
using Clear_t = void(__fastcall *)(void *editBox);
using DeleteRange_t = void(__fastcall *)(void *editBox, void *edx, int from, int to);
using Rebuild_t = void(__fastcall *)(void *editBox);

Insert_t g_origInsert = nullptr;
Clear_t g_origClear = nullptr;
DeleteRange_t g_origDeleteRange = nullptr;

void __fastcall Insert_h(void *editBox, void *edx, const char *text, const char *context, int runScripts,
                         int highlight) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != Offsets::RET_EDITBOX_SET_TEXT_INSERT) {
        g_origInsert(editBox, edx, text, context, runScripts, highlight);
        return;
    }
    const void *saved = g_scripted;
    g_scripted = editBox;
    g_origInsert(editBox, edx, text, context, runScripts, highlight);
    g_scripted = saved;
}

void __fastcall Clear_h(void *editBox) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != Offsets::RET_EDITBOX_SET_TEXT_CLEAR) {
        g_origClear(editBox);
        return;
    }
    const void *saved = g_scripted;
    g_scripted = editBox;
    g_origClear(editBox);
    g_scripted = saved;
}

// 4.3.4's DeleteSubstring: `if (!script) bit |= 0x8`.
void __fastcall DeleteRange_h(void *editBox, void *edx, int from, int to) {
    g_origDeleteRange(editBox, edx, from, to);
    if (editBox != g_scripted)
        g_userInput.insert(editBox);
}

// 4.3.4's Insert, after UpdateTextInfo: `script ? bit &= ~0x8 : bit |= 0x8`.
void __fastcall InsertRebuild(void *editBox) {
    reinterpret_cast<Rebuild_t>(Offsets::FUN_EDITBOX_REBUILD_CHAR_CLASS)(editBox);
    if (editBox == g_scripted)
        g_userInput.erase(editBox);
    else
        g_userInput.insert(editBox);
}

const Game::HookAutoRegister _insertHook{Offsets::FUN_EDITBOX_INSERT, reinterpret_cast<void *>(&Insert_h),
                                         reinterpret_cast<void **>(&g_origInsert)};
const Game::HookAutoRegister _clearHook{Offsets::FUN_EDITBOX_CLEAR, reinterpret_cast<void *>(&Clear_h),
                                        reinterpret_cast<void **>(&g_origClear)};
const Game::HookAutoRegister _deleteHook{Offsets::FUN_EDITBOX_DELETE_RANGE, reinterpret_cast<void *>(&DeleteRange_h),
                                         reinterpret_cast<void **>(&g_origDeleteRange)};

// 4.3.4's RunOnTextChangedScript and the clear after it.
void __fastcall FireTextChanged(void *editBox) {
    const bool userInput = g_userInput.count(editBox) != 0;
    const uint32_t *slot = Game::Ptr<uint32_t>(editBox, Offsets::OFF_EDITBOX_ONTEXTCHANGED_SLOT);
    void *L = Game::Lua::State();
    if (slot[0] != 0 && L != nullptr) {
        const int top = Game::Lua::GetTop(L);
        Game::Lua::PushBool(L, userInput);
        Frame::ScriptArgs::RunSlotWithValues(L, slot, editBox, top + 1, 1);
        Game::Lua::SetTop(L, top);
    }
    g_userInput.erase(editBox);
}

// The engine's fire block, and what replaces it: `mov ecx, esi; call
// FireTextChanged`, then NOPs to 0x0077D49D.
constexpr size_t kFireBlockSize = 24;
constexpr uint8_t kFireBlock[kFireBlockSize] = {0x8B, 0x8E, 0x18, 0x04, 0x00, 0x00, 0x85, 0xC9,
                                                0x8D, 0x86, 0x18, 0x04, 0x00, 0x00, 0x74, 0x08,
                                                0x50, 0x8B, 0xCE, 0xE8, 0xF3, 0x51, 0xF8, 0xFF};

void PatchCode() {
    Common::RedirectCall(Offsets::CALL_EDITBOX_INSERT_REBUILD_CHAR_CLASS, Offsets::FUN_EDITBOX_REBUILD_CHAR_CLASS,
                         reinterpret_cast<const void *>(&InsertRebuild));
    auto *site = reinterpret_cast<uint8_t *>(Offsets::PATCH_EDITBOX_TEXT_CHANGED_FIRE);
    if (std::memcmp(site, kFireBlock, kFireBlockSize) != 0)
        return; // already patched, or not the code this was derived from
    uint8_t bytes[kFireBlockSize];
    std::memset(bytes, 0x90, sizeof bytes);
    bytes[0] = 0x8B; // mov ecx, esi
    bytes[1] = 0xCE;
    bytes[2] = 0xE8; // call rel32
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&FireTextChanged) -
                                             (Offsets::PATCH_EDITBOX_TEXT_CHANGED_FIRE + 7));
    std::memcpy(bytes + 3, &rel, sizeof rel);
    Common::PatchBytes(site, bytes, sizeof bytes);
}

const Game::ModuleAutoRegister _autoreg{&PatchCode};

void PrepareForReload() {
    g_userInput.clear();
    g_scripted = nullptr;
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace EditBox::TextChanged
