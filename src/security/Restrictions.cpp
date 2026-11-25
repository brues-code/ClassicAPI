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

// Retail's restriction vocabulary, for Blizzard code that runs here
// unmodified (Blizzard_GamepadSmartNavigation's SecureHelper reads
// Enum.AddOnRestrictionType.Combat and calls :AddForbiddenAspects at load).
// This client has no taint and no addon restrictions, so nothing here
// restricts anything: the enums carry retail's values
// (RestrictedActionsConstantsDocumentation, ForbiddenAspectConstants-
// Documentation in _forever_), and AddForbiddenAspects takes its argument and
// does nothing. The constant-answer functions (issecure,
// C_RestrictedActions.IsAddOnRestrictionActive, ...) are in Compat.lua.

#include "Game.h"
#include "Offsets.h"

namespace Security::Restrictions {

namespace {

const Game::Lua::EnumIntegerEntry kAddOnRestrictionType[] = {
    {"Combat", 0}, {"Encounter", 1}, {"ChallengeMode", 2}, {"PvPMatch", 3}, {"Map", 4}, {"Chat", 5},
};

const Game::Lua::EnumIntegerEntry kForbiddenAspect[] = {
    {"SetToDefaults", 1},
    {"ScriptBindings", 2},
    {"UntrustedScriptExecution", 4},
    {"UntrustedLayoutScriptExecution", 8},
    {"EventRegistrations", 16},
    {"AlwaysPropagateInput", 32},
    {"ScriptedInput", 64},
    {"QueryFocus", 128},
    {"ChangeAnimationTarget", 256},
    {"RemoveSecretAspects", 512},
    {"ChangeParent", 1024},
    {"SetTexture", 2048},
    {"QueryRotation", 4096},
    {"QueryAnimationProgress", 8192},
    {"AddAnimations", 16384},
};

int __fastcall Script_AddForbiddenAspects(void *L) {
    Game::Lua::CheckNumber(L, 2);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"AddForbiddenAspects", &Script_AddForbiddenAspects},
};

const Game::Doc::Field kAspectsArg[] = {Game::Doc::Req("aspects", "ForbiddenAspect")};
const Game::Doc::Function kAddForbiddenAspects{
    "Takes forbidden aspects and keeps none: this client restricts no script object.", kAspectsArg, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"AddForbiddenAspects", &kAddForbiddenAspects},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "AddOnRestrictionType", kAddOnRestrictionType,
                                   "RestrictedActionsConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "ForbiddenAspect", kForbiddenAspect, "ForbiddenAspectConstants");
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Security::Restrictions
