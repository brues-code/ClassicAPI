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

#include "Pending.h"

#include "Game.h"
#include "Locations.h"
#include "Offsets.h"
#include "Text.h"
#include "event/Custom.h"
#include "net/PacketDispatch.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"

#include <cstring>

namespace EquipmentSet::Pending {

namespace {

// How long the paperdoll may sit unchanged before a swap that never reached
// its target is given up on. Each change restarts the wait, so this only
// has to cover one server round trip, not the whole swap.
constexpr uint32_t kSettleMs = 3000;

bool g_active = false;
bool g_refused = false;
Request g_req;
uint64_t g_lastWorn[SLOT_COUNT];
uint32_t g_deadline = 0;

// 4.3.4 calls `CGActionBar::UpdateUsable` whenever the pending state flips,
// so set buttons grey out and come back.
void UpdateUsable() {
    reinterpret_cast<void(__cdecl *)()>(Offsets::FUN_ACTION_UPDATE_USABLE)();
}

// 4.3.4 `OnEquipmentSetResult`: the event, then the bags-full message, then
// the action bar.
void Finish(bool success) {
    g_active = false;
    const int evt = kEvtSwapFinished.Slot();
    if (evt >= 0) {
        const int id = static_cast<int>(g_req.setID);
        if (success)
            Event::Custom::Fire(evt, "%d%d", 1, id);
        else
            Event::Custom::Fire(evt, "%s%d", static_cast<const char *>(nullptr), id);
    }
    if (g_req.bagsFull)
        Text::UIError("ERR_EQUIPMENT_MANAGER_BAGS_FULL",
                      "Equipment swap failed. Inventory is full.");
    UpdateUsable();
}

void OnTick() {
    if (!g_active)
        return;
    if (g_refused)
        return Finish(false);

    uint64_t worn[SLOT_COUNT];
    Locations::SnapshotPaperdoll(worn);
    bool done = true;
    bool changed = false;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        changed = changed || worn[i] != g_lastWorn[i];
        done = done && (!g_req.cares[i] || worn[i] == g_req.want[i]);
    }
    if (done)
        return Finish(!g_req.bagsFull);

    const uint32_t now = Time::Clock::NowMs();
    if (changed) {
        std::memcpy(g_lastWorn, worn, sizeof(worn));
        g_deadline = now + kSettleMs;
    } else if (Time::Clock::Reached(now, g_deadline)) {
        Finish(false);
    }
}

bool Moved(uint64_t guid) {
    for (int i = 0; i < g_req.movedCount; ++i)
        if (g_req.moved[i] == guid)
            return true;
    return false;
}

// SMSG_INVENTORY_CHANGE_FAILURE — see Offsets.h for the body. The engine's
// own handler shows the message; we only note that the swap failed, and
// settle on the next tick rather than firing Lua from the packet funnel.
void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::SMSG_INVENTORY_CHANGE_FAILURE || !g_active)
        return;
    const uint8_t error = Net::Read<uint8_t>(packet);
    if (error == 0)
        return;
    if (error == 1)
        Net::Read<uint32_t>(packet); // required level
    const uint64_t item1 = Net::Read<uint64_t>(packet);
    const uint64_t item2 = Net::Read<uint64_t>(packet);
    if ((item1 != 0 && Moved(item1)) || (item2 != 0 && Moved(item2)))
        g_refused = true;
}

// 4.3.4 clears the pending swap on enter-world; a login or /reload is the
// point nothing can still be in flight for the UI that started it.
void OnInGameInit() { g_active = false; }

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};
const Net::PacketDispatch::AutoSubscribe _packet{&OnPacket};
const Game::ModuleAutoRegister _init{&OnInGameInit};

} // namespace

bool CanUse() { return !g_active; }

void Begin(const Request &req) {
    g_req = req;
    g_active = true;
    g_refused = false;
    Locations::SnapshotPaperdoll(g_lastWorn);
    g_deadline = Time::Clock::NowMs() + kSettleMs;

    const int evt = kEvtSwapPending.Slot();
    if (evt >= 0)
        Event::Custom::Fire(evt, "%d", static_cast<int>(req.setID));
    UpdateUsable();
}

} // namespace EquipmentSet::Pending
