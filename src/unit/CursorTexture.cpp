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

// `SetUnitCursorTexture(texture, unit [, style [, includeLowPriority]])` ->
// hasCursor — the modern C global Blizzard_NamePlates uses for the soft-target
// icon: sets `texture` to the cursor image the world cursor would show over
// the unit (speak bubble, mail, pickup, attack, …). The image and the
// decision are the hover cursor's own (Cursor::Hover). The unit may be any
// token, including `softinteract` on a game object or corpse.
//
// `style` (Enum.CursorStyle: Mouse 0, Crosshair 1) and `includeLowPriority`
// select art and icon priorities 1.12 does not have; both are read by
// nothing. No cursor, or the plain resting pointer, returns false and leaves
// the texture as it was.

#include "Game.h"
#include "Offsets.h"
#include "cursor/Hover.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::CursorTexture {

namespace {

using SetTexture_t = uint32_t(__thiscall *)(void *tex, const char *path, int a, uint32_t blend,
                                            int b);

constexpr int kTypeMask =
    Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_GAMEOBJECT | Offsets::TYPEMASK_CORPSE;

int __fastcall Script_SetUnitCursorTexture(void *L) {
    void *texture = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_TEXTURE_LUA_TYPE_ID);
    if (texture == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: SetUnitCursorTexture(texture, unit[, style[, "
                            "includeLowPriority]])");
        return 0;
    }
    const uint64_t guid = Unit::Identity::GuidForToken(Game::Lua::ToString(L, 2));
    void *object = guid ? Object::ByGuid(kTypeMask, guid) : nullptr;
    char path[Offsets::CURSOR_FILE_SIZE];
    const bool has = object != nullptr &&
                     Cursor::Hover::ImagePath(object, Cursor::Hover::Mode(object), path,
                                              sizeof path);
    if (has)
        reinterpret_cast<SetTexture_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE)(
            texture, path, 0, Game::Read<uint32_t>(Offsets::VAR_TEXTURE_BLEND_DEFAULT), 0);
    Game::Lua::PushBool(L, has);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("textureObject", "SimpleTexture", "The texture to set."),
    Game::Doc::Req("unit", "UnitToken",
                   "Any unit token, including \"softinteract\" for a game object or corpse."),
    Game::Doc::Opt("style", "CursorStyle", nullptr, "Has no effect."),
    Game::Doc::Opt("includeLowPriority", "bool", nullptr, "Has no effect."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("hasCursor", "bool",
                   "True when the unit shows a cursor other than the normal pointer."),
};
const Game::Doc::Function kDoc{
    "Sets a texture to the cursor image that the mouse shows over the unit.", kArgs, kRets,
    "Unit"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetUnitCursorTexture", &Script_SetUnitCursorTexture,
                                      &kDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::CursorTexture
