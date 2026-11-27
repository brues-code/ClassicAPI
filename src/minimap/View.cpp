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

// `C_Minimap.GetViewRadius()` — retail's (MinimapDocumentation: no args,
// `yards` non-nilable). 3.80.2 does not register it; its C_Minimap is the
// four tracking/POI functions. The answer is the engine's own minimap radius,
// GetZoomRadius (Offsets::FUN_MINIMAP_GET_ZOOM_RADIUS) — the value the ping,
// the blips and the dots are placed with — so it follows the indoor/outdoor
// switch and both zoom cvars exactly as the minimap does.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Minimap::View {

namespace {

using GetZoomRadius_t = float(__cdecl *)();

int __fastcall Script_GetViewRadius(void *L) {
    auto fn = reinterpret_cast<GetZoomRadius_t>(
        static_cast<uintptr_t>(Offsets::FUN_MINIMAP_GET_ZOOM_RADIUS));
    Game::Lua::PushNumber(L, fn());
    return 1;
}

const Game::Doc::Field kViewRadiusRets[] = {
    Game::Doc::Req("yards", "number"),
};
const Game::Doc::Function kGetViewRadius{
    "Returns the distance in yards from the center of the minimap to its edge at the current "
    "zoom level, indoors or outdoors.",
    {}, kViewRadiusRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Minimap", "GetViewRadius", &Script_GetViewRadius,
                                     &kGetViewRadius);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Minimap::View
