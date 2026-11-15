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

// `C_SettingsUtil` — the C side of the Settings window, whose Lua is the
// embedded addon's port of Blizzard_Settings_Shared (Settings\).
//
//   OpenSettingsPanel([openToCategoryID [, scrollToElementName]]) fires
//     SETTINGS_PANEL_OPEN with its two arguments; SettingsPanel opens itself
//     (to that category when one is given) on the event. Settings.OpenToCategory
//     calls it.
//   NotifySettingsLoaded() fires SETTINGS_LOADED. SettingsRegistrar calls it
//     once every registrant has run, after VARIABLES_LOADED and
//     PLAYER_ENTERING_WORLD.
//
// Retail documents openToCategoryID as a number, the category's counter ID.
// Addons set a category's ID to its name (Ace3, Auctionator) and pass that
// name, and the panel finds a category by comparing IDs, so the argument is
// passed through as given.
//
// Both events fire at once, inside the call, through the engine's event
// dispatcher (Event::Custom::FireValues).

#include "Game.h"
#include "event/Custom.h"

namespace Settings::Panel {

namespace {

// OpenSettingsPanel's arguments, which SETTINGS_PANEL_OPEN carries as its payload.
const Game::Doc::Field kOpenFields[] = {
    Game::Doc::Opt("openToCategoryID", "number", nullptr,
                   "The category to show; a category whose ID is its name is given by name."),
    Game::Doc::Opt("scrollToElementName", "string", nullptr,
                   "The setting the list scrolls to."),
};
const Game::Doc::Event kOpenDoc{"SettingsUtil",
                                "Fires when something asks the Settings window to open.",
                                kOpenFields};
const Event::Custom::AutoReserve kOpenEvent{"SETTINGS_PANEL_OPEN", &kOpenDoc};

const Game::Doc::Event kLoadedDoc{
    "SettingsUtil", "Fires once the Settings window has registered all of its settings.", {}};
const Event::Custom::AutoReserve kLoadedEvent{"SETTINGS_LOADED", &kLoadedDoc};

const Game::Doc::Function kOpenSettingsPanel{
    "Opens the Settings window, to a category when one is given.", kOpenFields, {}};

const Game::Doc::Function kNotifySettingsLoaded{
    "Fires SETTINGS_LOADED; the Settings window calls it once its settings are registered.",
    {},
    {}};

int __fastcall Script_OpenSettingsPanel(void *L) {
    // Exactly two payload values, nil where an argument is absent.
    Game::Lua::SetTop(L, 2);
    Event::Custom::FireValues(kOpenEvent.Slot(), L, 1, 2);
    return 0;
}

int __fastcall Script_NotifySettingsLoaded(void *L) {
    (void)L;
    Event::Custom::Fire(kLoadedEvent.Slot(), "");
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_SettingsUtil", "OpenSettingsPanel",
                                     &Script_OpenSettingsPanel, &kOpenSettingsPanel);
    Game::Lua::RegisterTableFunction("C_SettingsUtil", "NotifySettingsLoaded",
                                     &Script_NotifySettingsLoaded, &kNotifySettingsLoaded);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Settings::Panel
