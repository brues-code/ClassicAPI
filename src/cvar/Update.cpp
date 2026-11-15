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

// `CVAR_UPDATE(cvarName, value)` whenever a cvar that Lua can see changes, as
// retail fires it. Retail's Settings window and CVarCallbackRegistry listen
// for it by the cvar's name.
//
// 1.12 fires CVAR_UPDATE (event 0x138) only from Script_SetCVar
// (FUN_00488C10), and only with the event token a caller passes as the third
// argument: `SetCVar(name, value, "TOKEN")` -> CVAR_UPDATE("TOKEN", value).
// That path stays as it is. The stock frames that listen (OptionsFrame,
// UIOptionsFrame, SoundOptionsFrame, TextStatusBar) key on upper-case tokens
// such as "SHOW_BUFF_DURATIONS", which no cvar name matches, so a fire by name
// is new to them; SoundOptionsFrame reloads its check boxes on any fire, which
// changes nothing. ReputationWatchBar (ReputationFrame.xml) is the one that
// keys on a cvar name: a fire for statusBarText now locks or unlocks the
// watched reputation's text, as that handler means to, and any other fire runs
// ReputationWatchBar_Update, as every UPDATE_FACTION already does.
//
// The hook is on CVar::Set (FUN_SET_CVAR_VALUE), which every write goes
// through: Script_SetCVar, C_CVar.SetCVar, the console `set` command and the
// engine's own writes. A change is fired at the head of the next UI frame,
// where no frame is mid-draw and no engine code is mid-way through its work.
// It is never fired inside the setter: the engine sets cvars from inside its
// own code while a script runs further up the stack, and a listener run
// there would run in the middle of engine code that does not expect a
// script. That a script is running somewhere does not make the setter's
// caller a script.
//
// Only a cvar that Lua can see (CVAR_FLAG_SCRIPT_VISIBLE) fires, only when its
// value text changed, and only while a frame listens, so the Config.wtf load
// and the cvar registrations at startup fire nothing.
//
// A cvar that changed fires once per frame, with the value it has when the
// event fires, not the value of each change. Retail fires inside the setter,
// so the value it gives is always the current one, and its listeners rely on
// that: the Settings window writes the value it is given back into its
// setting. A value from an earlier change of the same frame would be written
// back over the later one, change the cvar again and fire again, so two
// changes in one frame would swap the cvar between them every frame. The value
// is copied before the fire: a listener that sets the cvar frees the engine's
// string while the dispatcher still hands it to the listeners after it.

#include "event/Custom.h"
#include "tick/FrameTick.h"

#include "Game.h"
#include "Offsets.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace CVar::Update {

namespace {

// CVar::Set — __thiscall(cvar, value, apply, setReset, setDefault, force),
// RET 0x14; declared __fastcall with the unused EDX so the hook matches it.
using Set_t = int(__fastcall *)(void *cvar, void *edx, const char *value, int apply, int setReset,
                                int setDefault, int force);

Set_t g_origSet = nullptr;

// Each cvar that changed since the last drain, once, in the order of its first
// change. A cvar record lives as long as the client: the engine never frees
// one.
std::vector<void *> g_pending;

int ListeningSlot() {
    const int slot = Event::Custom::LookupByName("CVAR_UPDATE");
    return Event::Custom::HasListeners(slot) ? slot : -1;
}

const char *ValueOf(void *cvar) {
    const char *value = Game::Read<const char *>(cvar, Offsets::OFF_CVAR_VALUE_STR);
    return value != nullptr ? value : "";
}

int __fastcall Set_h(void *cvar, void *edx, const char *value, int apply, int setReset,
                     int setDefault, int force) {
    const bool visible =
        (Game::Read<uint32_t>(cvar, Offsets::OFF_CVAR_FLAGS) & Offsets::CVAR_FLAG_SCRIPT_VISIBLE) != 0;
    const int slot = visible ? ListeningSlot() : -1;
    if (slot < 0)
        return g_origSet(cvar, edx, value, apply, setReset, setDefault, force);

    const std::string before = ValueOf(cvar);
    const int result = g_origSet(cvar, edx, value, apply, setReset, setDefault, force);
    if (before == ValueOf(cvar))
        return result;

    if (std::find(g_pending.begin(), g_pending.end(), cvar) == g_pending.end())
        g_pending.push_back(cvar);
    return result;
}

void Drain() {
    if (g_pending.empty())
        return;
    std::vector<void *> changed;
    changed.swap(g_pending);
    const int slot = ListeningSlot();
    if (slot < 0)
        return;
    for (void *cvar : changed) {
        const std::string value = ValueOf(cvar);
        Event::Custom::Fire(slot, "%s%s", Game::Read<const char *>(cvar, Offsets::OFF_CVAR_NAME),
                            value.c_str());
    }
}

// A change queued under the old interface is not fired into the new one.
void PrepareForReload() { g_pending.clear(); }

const Game::HookAutoRegister _setHook{Offsets::FUN_SET_CVAR_VALUE, reinterpret_cast<void *>(&Set_h),
                                      reinterpret_cast<void **>(&g_origSet)};
const Tick::FrameTick::AutoSubscribeBefore _tick{&Drain};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace CVar::Update
