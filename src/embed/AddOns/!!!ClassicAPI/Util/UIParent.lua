function MouseIsOver(region, topOffset, bottomOffset, leftOffset, rightOffset)
	if region:IsMouseOver(topOffset, bottomOffset, leftOffset, rightOffset) then
		return 1;
	end
end

-- Retail raises the error only on a GM client (IsGMClient). A player client
-- is never one, so the call does nothing.
function GMError(...)
end

-- Retail loads Blizzard_DamageMeter with the interface. Its port,
-- ClassicAPI_DamageMeter, is load on demand: it loads once damageMeterEnabled
-- is on, when the variables have loaded or when the cvar turns on. Once
-- loaded, the meter hides itself while the cvar is off
-- (DamageMeterMixin:ShouldBeShown).
local DAMAGE_METER_ADDON = "ClassicAPI_DamageMeter";
local DAMAGE_METER_ENABLED_CVAR = "damageMeterEnabled";

local function LoadDamageMeterIfEnabled()
	if CVarCallbackRegistry:GetCVarValueBool(DAMAGE_METER_ENABLED_CVAR) and not IsAddOnLoaded(DAMAGE_METER_ADDON) then
		UIParentLoadAddOn(DAMAGE_METER_ADDON);
	end
end

EventRegistry:RegisterFrameEventAndCallback("VARIABLES_LOADED", LoadDamageMeterIfEnabled);
CVarCallbackRegistry:RegisterCallback(DAMAGE_METER_ENABLED_CVAR, LoadDamageMeterIfEnabled);
