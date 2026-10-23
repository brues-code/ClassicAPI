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

// `C_GossipInfo` point of interest — the map marker a gossip option such as
// a city guard's directions places (SMSG_GOSSIP_POI):
//
//   GetPoiForUiMapID(uiMapID)            -> gossipPoiID or nil
//   GetPoiInfo(uiMapID, gossipPoiID)     -> GossipPoiInfo or nil
//
// The engine keeps the marker in static blip record 1 (see
// `VAR_GOSSIP_POI_BLIP` in Offsets.h): one marker at a time, live while its
// position is nonzero. Both functions read that record and apply the world
// map's own placement test (0x004A7360): the map's WorldMapArea row must
// carry the marker's map id and the projected point must land inside 0..1.
//
// The packet carries no POI id, and there is only ever one marker, so its
// `gossipPoiID` is the engine's slot number, 1.
//
// `DYNAMIC_GOSSIP_POI_UPDATED` fires when the marker is set or cleared. Every
// change runs the world map rebuild 0x004A67A0 (the setter after a packet, the
// 480 s expiry and the 10-yard clear in 0x006D93A0), but that rebuild also
// runs for every map view change, so the event comes from a once-per-frame
// diff of the record instead: x, y and the expiry, which each set rewrites
// and each clear zeroes.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "map/Area.h"
#include "map/Vector2.h"
#include "tick/WorldTick.h"

#include <cstdint>

namespace Gossip::Poi {

namespace {

constexpr int kGossipPoiID = 1;

template <typename T> T BlipField(uintptr_t off) {
    return *reinterpret_cast<const T *>(Offsets::VAR_GOSSIP_POI_BLIP + off);
}

const Game::Doc::Event kPoiUpdatedDoc{
    "GossipInfo", "Fires when a gossip point of interest is added to or removed from the map.",
    {}};
const Event::Custom::AutoReserve _evtPoiUpdated{"DYNAMIC_GOSSIP_POI_UPDATED",
                                                &kPoiUpdatedDoc};

struct Snapshot {
    uint32_t x, y, expiry;
};

// The record as of the last fire. DLL-lived like the record itself, so a
// /reload with a marker up does not fire a phantom change.
Snapshot g_last{};

void OnTick() {
    const Snapshot now{BlipField<uint32_t>(Offsets::OFF_STATIC_BLIP_X),
                       BlipField<uint32_t>(Offsets::OFF_STATIC_BLIP_Y),
                       *reinterpret_cast<const uint32_t *>(Offsets::VAR_GOSSIP_POI_EXPIRY)};
    if (now.x == g_last.x && now.y == g_last.y && now.expiry == g_last.expiry)
        return;
    g_last = now;
    Event::Custom::Fire(_evtPoiUpdated.Slot(), "");
}

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};

// Projects the live marker onto `uiMapID`. False when there is no marker or
// it isn't on that map.
bool ProjectOnMap(int uiMapID, double *outPx, double *outPy) {
    const float x = BlipField<float>(Offsets::OFF_STATIC_BLIP_X);
    const float y = BlipField<float>(Offsets::OFF_STATIC_BLIP_Y);
    if (x == 0.0f && y == 0.0f)
        return false;

    const int row = Map::Area::RowForUiMapID(uiMapID);
    int rowMapID = 0;
    return row > 0 &&
           Map::Area::RowRect(row, nullptr, nullptr, nullptr, nullptr, &rowMapID) &&
           rowMapID == BlipField<int32_t>(Offsets::OFF_STATIC_BLIP_MAP) &&
           Map::Area::PercentInRow(row, x, y, outPx, outPy) &&
           *outPx >= 0.0 && *outPx <= 1.0 && *outPy >= 0.0 && *outPy <= 1.0;
}

// `C_GossipInfo.GetPoiForUiMapID(uiMapID)` — the id of the gossip marker
// shown on that map, or nil.
int __fastcall Script_GetPoiForUiMapID(void *L) {
    const int uiMapID =
        Game::Lua::IsNumber(L, 1) ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    double px = 0.0, py = 0.0;
    const bool have = uiMapID != 0 && ProjectOnMap(uiMapID, &px, &py);

    Game::Lua::SetTop(L, 0);
    if (!have)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushNumber(L, static_cast<double>(kGossipPoiID));
    return 1;
}

// `C_GossipInfo.GetPoiInfo(uiMapID, gossipPoiID)` — the marker's name, icon
// index and 0..1 position on that map, or nil.
int __fastcall Script_GetPoiInfo(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1) && Game::Lua::IsNumber(L, 2);
    const int uiMapID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const int poiID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 2)) : 0;
    double px = 0.0, py = 0.0;
    const bool have = poiID == kGossipPoiID && ProjectOnMap(uiMapID, &px, &py);

    Game::Lua::SetTop(L, 0);
    if (!have) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(
        L, "name", reinterpret_cast<const char *>(Offsets::VAR_GOSSIP_POI_NAME));
    Game::Lua::SetFieldNumber(
        L, "textureIndex",
        static_cast<double>(BlipField<uint32_t>(Offsets::OFF_STATIC_BLIP_ICON)));
    Game::Lua::PushString(L, "position");
    Map::PushVector2D(L, px, py);
    Game::Lua::SetTable(L, -3);
    Game::Lua::SetFieldBool(L, "inBattleMap", false);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kForMapArgs[] = {
    Game::Doc::Req("uiMapID", "number"),
};
const Game::Doc::Field kForMapRets[] = {
    Game::Doc::Opt("gossipPoiID", "number"),
};
const Game::Doc::Function kGetPoiForUiMapID{
    "Returns the id of the gossip point of interest shown on the map, if any.",
    kForMapArgs, kForMapRets};

const Game::Doc::Field kPoiInfoFields[] = {
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("textureIndex", "number", "The icon's index in the map POI icon texture."),
    Game::Doc::Req("position", "vector2", "The point on the map, from 0 to 1 on each axis."),
    Game::Doc::Req("inBattleMap", "bool"),
};
const Game::Doc::Structure kGossipPoiInfo{
    "GossipPoiInfo", "GossipInfo", kPoiInfoFields,
    "A point of interest that a gossip option marked on the map."};

const Game::Doc::Field kInfoArgs[] = {
    Game::Doc::Req("uiMapID", "number"),
    Game::Doc::Req("gossipPoiID", "number"),
};
const Game::Doc::Field kInfoRets[] = {
    Game::Doc::Opt("gossipPoiInfo", "GossipPoiInfo"),
};
const Game::Doc::Function kGetPoiInfo{
    "Returns the gossip point of interest as it appears on the map.", kInfoArgs,
    kInfoRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetPoiForUiMapID",
                                     &Script_GetPoiForUiMapID, &kGetPoiForUiMapID);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetPoiInfo", &Script_GetPoiInfo,
                                     &kGetPoiInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Gossip::Poi
