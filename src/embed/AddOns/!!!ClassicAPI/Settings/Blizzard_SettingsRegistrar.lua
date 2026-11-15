-- Backport of retail's Blizzard_Settings_Shared/Blizzard_SettingsRegistrar.lua.
--
-- The one difference: this file loads only on the game screen, so the
-- glue-screen branch is gone.

local securecallfunction = securecallfunction;

SettingsRegistrar = {};

function SettingsRegistrar:OnLoad()
	self.registrants = {};

	local function Callback()
		self.allowCallRegistrants = true;

		local function CallRegistrant(index, registrant)
			registrant();
		end
		secureexecuterange(self.registrants, CallRegistrant);

		C_SettingsUtil.NotifySettingsLoaded();
	end

	EventUtil.ContinueAfterAllEvents(Callback, "VARIABLES_LOADED", "PLAYER_ENTERING_WORLD");
end

function SettingsRegistrar:AddRegistrant(registrant)
	if self.allowCallRegistrants then
		securecallfunction(registrant);
	else
		table.insert(self.registrants, registrant);
	end
end

SettingsRegistrar:OnLoad();
