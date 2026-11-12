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

#include "frame/Subclass.h"

namespace Frame::Subclass {

namespace {

// Constant-initialized, so every MethodHash's constructor can link in during
// static initialization whatever the order of the translation units.
MethodHash *g_hashes = nullptr;

using RegisterFactory_t = int(__fastcall *)(const char *type, Factory_t factory, int unique);
using PushMethod_t = int(__fastcall *)(void *L, const char *name, void *hash);
using UnregisterMethods_t = void(__fastcall *)(void *hash);
using UnregisterScriptFunctions_t = void(__cdecl *)();
UnregisterScriptFunctions_t UnregisterScriptFunctions_o = nullptr;

// The hashes' functions are references into the game Lua state.
void __cdecl UnregisterScriptFunctions_h() {
    UnregisterScriptFunctions_o();
    MethodHash::ReleaseAll();
}

const Game::HookAutoRegister _unregisterHook{
    Offsets::FUN_GAMEUI_UNREGISTER_SCRIPT_FUNCTIONS,
    reinterpret_cast<void *>(&UnregisterScriptFunctions_h),
    reinterpret_cast<void **>(&UnregisterScriptFunctions_o)};

} // namespace

void RegisterFactory(const char *type, Factory_t factory) {
    reinterpret_cast<RegisterFactory_t>(Offsets::FUN_FRAMEXML_REGISTER_FACTORY)(type, factory, 0);
}

MethodHash::MethodHash() : words_{}, next_(g_hashes) {
    words_[0] = Offsets::VTBL_METHOD_HASH_ALLOCATOR;
    words_[1] = 0xC; // node link offset
    words_[2] = reinterpret_cast<uintptr_t>(&words_[2]);
    words_[3] = reinterpret_cast<uintptr_t>(&words_[2]) | 1;
    words_[9] = 0xFFFFFFFF; // no buckets yet
    g_hashes = this;
}

bool MethodHash::PushMethod(void *L, const char *name) {
    return reinterpret_cast<PushMethod_t>(Offsets::FUN_FRAMESCRIPT_PUSH_METHOD)(L, name, words_) !=
           0;
}

void MethodHash::ReleaseAll() {
    for (MethodHash *h = g_hashes; h != nullptr; h = h->next_)
        reinterpret_cast<UnregisterMethods_t>(Offsets::FUN_FRAMESCRIPT_UNREGISTER_METHODS)(h->words_);
}

} // namespace Frame::Subclass
