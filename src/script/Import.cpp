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

// Lua file modules: `import(name)`, as retail's addons use it. No client we
// can read has it (3.80.2 does not; retail's exe is encrypted on disk), so
// the contract is the one Blizzard's Lua relies on:
//
//   - An addon `.lua` file's chunk returns its module, a table
//     (Blizzard_GamepadSmartNavigation\SecureHelper.lua ends
//     `return SecureHelper;`).
//   - The module's name is the addon's name and the file's path in the addon
//     without `.lua`, joined with dots: `Blizzard_GamepadActionBars\
//     TargetActionBars\Shared.lua` is `Blizzard_GamepadActionBars.
//     TargetActionBars.Shared`.
//   - `import("Addon.Path")` returns a module by its full name
//     (`import("Blizzard_GamepadSmartNavigation.SecureHelper")`).
//   - A name starting with "." is relative to the folder of the file that
//     calls import: from the addon's root, `import(".TargetActionBars.Shared")`;
//     from TargetActionBars\, `import(".Shared")`.
//   - Every import in Blizzard's addons names a file listed earlier in its
//     TOC or in a dependency, so a module exists before it is imported.
//
// The engine drops a file's results: its buffer runner calls
// lua_pcall(L, 0, 0, -2) (CALL_EXECUTE_BUFFER_PCALL). The redirect runs an
// addon file's chunk with one result, keeps it as that file's module, and
// pops it, so the stack the engine sees afterwards is unchanged. Names match
// without case, as file paths do. The modules live in the Lua registry, so
// each Lua state (glue, every world load) starts with none. An unknown name
// raises an error; its wording is ours.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"

#include <cstdint>
#include <string>

namespace Script::Import {

namespace {

constexpr char kRegistryKey[] = "ClassicAPI_Modules";
constexpr char kAddOnsPrefix[] = "@Interface\\AddOns\\";
constexpr size_t kAddOnsPrefixLen = sizeof(kAddOnsPrefix) - 1;

// "@Interface\AddOns\Addon\Dir\File.lua" -> folder "addon.dir" and module
// "addon.dir.file", lower case. False for any other chunk.
bool ParseSource(const char *source, std::string *folder, std::string *module) {
    if (source == nullptr || !Ascii::StartsWithCI(source, kAddOnsPrefix))
        return false;
    std::string path(source + kAddOnsPrefixLen);
    if (path.size() <= 4 || !Ascii::EqualCI(path.c_str() + path.size() - 4, ".lua"))
        return false;
    path.resize(path.size() - 4);
    for (char &c : path) {
        if (c == '\\' || c == '/')
            c = '.';
        else
            c = Ascii::ToLower(c);
    }
    const size_t lastDot = path.rfind('.');
    if (lastDot == std::string::npos)
        return false; // a file directly in Interface\AddOns belongs to no addon
    if (folder != nullptr)
        folder->assign(path, 0, lastDot);
    if (module != nullptr)
        *module = std::move(path);
    return true;
}

// Pushes this Lua state's module table, creating it on first use.
void PushModules(void *L) {
    Game::Lua::PushString(L, kRegistryKey);
    Game::Lua::RawGet(L, Game::Lua::REGISTRY_INDEX);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        return;
    Game::Lua::SetTop(L, -2);
    Game::Lua::NewTable(L);
    Game::Lua::PushString(L, kRegistryKey);
    Game::Lua::PushValue(L, -2);
    Game::Lua::RawSet(L, Game::Lua::REGISTRY_INDEX);
}

// The buffer runner's lua_pcall: [errfunc, chunk] on top. `chunkName` is the
// name the runner compiled the buffer under, not the source of the function
// on the stack: an addon file whose body reads `...` runs through
// LuaSyntax's C closure (Script_RunAddonFile), which has no source.
int __fastcall ExecuteBufferPCall(void *L, int nargs, int nresults, int errfunc, const char *chunkName) {
    std::string module;
    if (!ParseSource(chunkName, nullptr, &module))
        return Game::Lua::PCall(L, nargs, nresults, errfunc);

    const int status = Game::Lua::PCall(L, nargs, 1, errfunc);
    if (status != 0)
        return status; // [errfunc, message], as with no results
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL) {
        PushModules(L);
        Game::Lua::PushString(L, module.c_str());
        Game::Lua::PushValue(L, -3);
        Game::Lua::RawSet(L, -3);
        Game::Lua::SetTop(L, -2);
    }
    Game::Lua::SetTop(L, -2); // the result: the engine asked for none
    return 0;
}

// The call site reaches ExecuteBufferPCall through this. ECX/EDX (L, nargs)
// and the pcall's two stack arguments pass through; it adds the runner's
// chunk name, its third argument ([ebp+8], the one it passes to
// luaL_loadbuffer at 0x00704B03), then returns as lua_pcall does (`ret 8`).
__declspec(naked) void ExecuteBufferPCallThunk() {
    __asm {
        push dword ptr [ebp + 8]   // chunkName
        push dword ptr [esp + 0Ch] // errfunc
        push dword ptr [esp + 0Ch] // nresults
        call ExecuteBufferPCall
        ret 8
    }
}

int __fastcall Script_import(void *L) {
    const char *name = Game::Lua::CheckLString(L, 1, nullptr);
    std::string full;
    if (name[0] == '.') {
        alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
        std::string folder;
        if (!Game::Lua::GetStack(L, 1, ar) || !Game::Lua::GetInfo(L, "S", ar) ||
            !ParseSource(Game::Read<const char *>(ar, Offsets::OFF_LUA_DEBUG_SOURCE), &folder, nullptr)) {
            Game::Lua::Error(L, "import: '%s' is relative, but the caller is not an addon file", name);
            return 0;
        }
        full = folder;
    }
    for (const char *c = name; *c != '\0'; ++c)
        full.push_back(Ascii::ToLower(*c));

    PushModules(L);
    Game::Lua::PushString(L, full.c_str());
    Game::Lua::RawGet(L, -2);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, "import: no module '%s'", name);
        return 0;
    }
    return 1;
}

const Game::Doc::Field kImportArgs[] = {
    Game::Doc::Req("name", "cstring",
                   "The module: the addon name and the file's path without .lua, joined with dots. A "
                   "leading dot names it from the calling file's folder."),
};
const Game::Doc::Field kImportRets[] = {Game::Doc::Req("module", "LuaValueVariant", "What the file returned.")};
const Game::Doc::Function kImport{"Returns the value an addon Lua file returned.", kImportArgs, kImportRets,
                                  "LuaGlobals"};

// Both passes, so the glue's files are modules too. Idempotent.
void PatchCode() {
    Common::RedirectCall(Offsets::CALL_EXECUTE_BUFFER_PCALL, Offsets::LUA_PCALL,
                         reinterpret_cast<const void *>(&ExecuteBufferPCallThunk));
}

void RegisterGlue() {
    PatchCode();
    Game::Lua::RegisterGlueFunction("import", &Script_import, &kImport);
}

void RegisterWorld() {
    PatchCode();
    Game::Lua::RegisterGlobalFunction("import", &Script_import, &kImport);
}

const Game::GlueModuleAutoRegister _glue{&RegisterGlue};
const Game::ModuleAutoRegister _world{&RegisterWorld};

} // namespace

} // namespace Script::Import
