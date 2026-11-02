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

// `UpdateAddOnMemoryUsage()` / `GetAddOnMemoryUsage(index or "name")` —
// 3.3.5's `Script_UpdateAddOnMemoryUsage` (0x00511510) and
// `Script_GetAddOnMemoryUsage` (0x00511530) over `Gc::Tag`.
//
// An addon's tag is its registry name pointer `[entry+0x14]`: the exact
// pointer `FUN_ADDON_LOAD_ONE` installs as the context word while the
// addon's files run (and the one 3.3.5's `GetAddOnName` returns), so the
// lookup is a pointer match like 3.3.5's `luaC_getsize`. Secure addons run
// under context 0, are never tagged, and read 0 — as in 3.3.5.
//
// 3.3.5's `LoadAddOn` also pins `lua_gctag` to the addon for its whole
// file / Bindings / SavedVariables window, so objects made there stay the
// addon's even when its code switches the context word (calling into another
// addon's function or table makes that addon's word current). The 1.12 loader
// has the same window; two 5-byte call patches open and close a `Gc::Tag` pin
// at its edges (SITE_ADDON_LOAD_WINDOW_OPEN / _CLOSE).

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "addons/Registry.h"
#include "gc/Tag.h"

#include <cstdint>
#include <cstdio>

namespace AddOns::MemoryUsage {

namespace {

constexpr uintptr_t addrExecContextDepth = Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH;

// The loader's own context value: the name for an insecure addon, 0 for a
// secure one (3.3.5's `addon->m_status ? addonName : nullptr`). A 0 pin means
// "no override", so a secure addon's window keeps following the context word.
void __fastcall OpenLoadWindow(const uint8_t *entry) {
    const bool insecure = Game::Read<uint32_t>(entry, Offsets::OFF_ADDON_ENTRY_STATUS) != 0;
    Gc::Tag::PushPin(insecure ? Game::Read<const char *>(entry, Offsets::OFF_ADDON_ENTRY_NAME_PTR)
                              : nullptr);
}

void CloseLoadWindow() { Gc::Tag::PopPin(); }

// Stands in for `xor eax,eax; lea ecx,[ebp-0x18]` at the window's open.
// EBX = the entry; EBP is the loader's frame.
__declspec(naked) void LoadWindowOpenThunk() {
    __asm {
        pushad
        mov ecx, ebx
        call OpenLoadWindow
        popad
        xor eax, eax
        lea ecx, [ebp - 18h]
        ret
    }
}

// Stands in for `mov eax,[VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH]` at the close.
__declspec(naked) void LoadWindowCloseThunk() {
    __asm {
        pushad
        call CloseLoadWindow
        popad
        mov eax, addrExecContextDepth
        mov eax, [eax]
        ret
    }
}

void PatchLoadWindow() {
    static const uint8_t kOpen[5] = {0x33, 0xC0, 0x8D, 0x4D, 0xE8};
    static const uint8_t kClose[5] = {0xA1, 0xC4, 0xEA, 0xCE, 0x00};
    Common::ReplaceWithCall(Offsets::SITE_ADDON_LOAD_WINDOW_OPEN, kOpen, sizeof kOpen,
                            reinterpret_cast<const void *>(&LoadWindowOpenThunk));
    Common::ReplaceWithCall(Offsets::SITE_ADDON_LOAD_WINDOW_CLOSE, kClose, sizeof kClose,
                            reinterpret_cast<const void *>(&LoadWindowCloseThunk));
}

int __fastcall Script_UpdateAddOnMemoryUsage(void *L) {
    Gc::Tag::UpdateUsage(L);
    return 0;
}

int __fastcall Script_GetAddOnMemoryUsage(void *L) {
    const uint8_t *record =
        Addons::ResolveRecord(L, "Usage: GetAddOnMemoryUsage(index or \"name\")");
    const char *tag =
        record != nullptr
            ? Game::Read<const char *>(record, Offsets::OFF_ADDON_ENTRY_NAME_PTR)
            : nullptr;
    const uint64_t bytes = tag != nullptr ? Gc::Tag::Usage(tag) : 0;
    Game::Lua::PushNumber(L, static_cast<double>(bytes) * (1.0 / 1024.0));
    return 1;
}

// `_classicapi_LuaHeapAudit()` → objectBytes, nblocks, taggedObjects.
// Sums `Gc::Tag::SizeOf` over every collectable object the engine can
// reach. `objectBytes` should sit just under `nblocks` (the engine's own
// byte count): the gap is what is not a GC object (string-table buckets,
// the main thread and its stack, buffers). A gap that grows with the heap
// means a size formula is wrong.
int __fastcall Script_LuaHeapAudit(void *L) {
    uint64_t bytes = 0;
    Gc::Tag::ForEachObject(L, [&bytes](const void *object) { bytes += Gc::Tag::SizeOf(object); });
    const uint8_t *g = Game::Read<const uint8_t *>(L, Offsets::OFF_LUA_STATE_G);
    Game::Lua::PushNumber(L, static_cast<double>(bytes));
    Game::Lua::PushNumber(L, static_cast<double>(Game::Read<uint32_t>(g, Offsets::OFF_LUA_G_NBLOCKS)));
    Game::Lua::PushNumber(L, static_cast<double>(Gc::Tag::TaggedCount()));
    return 3;
}

// `_classicapi_GcTagOf(value)` → the owner tag of a table / function /
// userdata / thread: the addon's name when the tag is an addon's, else the
// raw context word as "0x…" (e.g. RunScript's), nil when untagged.
int __fastcall Script_GcTagOf(void *L) {
    const char *tag = Gc::Tag::Of(Game::Lua::ToPointer(L, 1));
    if (tag == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const char *name = nullptr;
    Addons::ForEachEntry([&](uintptr_t entry) {
        if (Game::Read<const char *>(entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR) != tag)
            return true;
        name = tag;
        return false;
    });
    if (name != nullptr) {
        Game::Lua::PushString(L, name);
    } else {
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08X", static_cast<unsigned>(reinterpret_cast<uintptr_t>(tag)));
        Game::Lua::PushString(L, buf);
    }
    return 1;
}

const Game::Doc::Function kUpdateAddOnMemoryUsage{
    "Recounts the memory each addon uses; GetAddOnMemoryUsage returns the result of the "
    "last recount.",
    {}, {}, "AddOnsGlobals"};

const Game::Doc::Field kGetArgs[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Req("memory", "number",
                   "Kilobytes of Lua memory the addon's code allocated, as of the last "
                   "UpdateAddOnMemoryUsage."),
};
const Game::Doc::Function kGetAddOnMemoryUsage{
    "Returns the Lua memory an addon uses, in kilobytes, as of the last "
    "UpdateAddOnMemoryUsage.",
    kGetArgs, kGetRets, "AddOnsGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    // Each pass runs before the load pass; the patch is idempotent.
    PatchLoadWindow();
    Game::Lua::RegisterGlobalFunction("UpdateAddOnMemoryUsage", &Script_UpdateAddOnMemoryUsage,
                                      &kUpdateAddOnMemoryUsage);
    Game::Lua::RegisterGlobalFunction("GetAddOnMemoryUsage", &Script_GetAddOnMemoryUsage,
                                      &kGetAddOnMemoryUsage);
    Game::Lua::RegisterGlobalFunction("_classicapi_LuaHeapAudit", &Script_LuaHeapAudit);
    Game::Lua::RegisterGlobalFunction("_classicapi_GcTagOf", &Script_GcTagOf);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace AddOns::MemoryUsage
