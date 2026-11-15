-- Backport of retail's Blizzard_Settings_Shared/Blizzard_Registration.lua,
-- with the two other places retail joins the Settings window to the game's
-- UI: the Escape handler (retail registers SettingsPanel_EscapePressed in
-- Blizzard_SettingsDefinitions_Frame/Interface.lua) and the game menu entry
-- (Blizzard_GameMenu).
--
-- Differences from the retail source:
--   - 1.12 has no RegisterUIPanel. A panel's attributes are its entry in
--     UIPanelWindows, which ShowUIPanel reads. 1.12's panel manager has no
--     checkFit attribute.
--   - This file loads only on the game screen, so the glue-screen test is gone.
--   - 1.12 has no game menu Escape handlers. Escape reaches ToggleGameMenu,
--     so ToggleGameMenu closes the window, after any dialog over it, the way
--     retail's handler order does.
--   - The game menu entry is an AddOn Options button: the client keeps its
--     own option buttons, which open its own option windows.

UIPanelWindows["SettingsPanel"] =
{
	area = "center",
	pushable = 0,
	whileDead = 1,
};

local ToggleGameMenu_Orig = ToggleGameMenu;
function ToggleGameMenu(clicked)
	if not clicked and SettingsPanel:IsShown() then
		if not StaticPopup_EscapePressed() then
			SettingsPanel_EscapePressed();
		end
		return;
	end
	ToggleGameMenu_Orig(clicked);
end

-- Goes right above Key Bindings. Stock anchors Key Bindings under Interface
-- Options and Turtle under its single Options button, so the new button takes
-- over whatever anchor Key Bindings has and Key Bindings moves under it.
--
-- The splice waits for the menu's first show. Other addons insert their own
-- buttons the same way -- ShaguTweaks anchors "Advanced Options" under
-- Interface Options and moves Key Bindings under it -- and they load after
-- us. Splicing at load time would claim the slot first, and the later addon
-- would anchor its button on the same spot as ours. By the first show every
-- addon has placed its buttons, so ours goes into the chain as it stands.
local menuButton = CreateFrame("Button", "GameMenuButtonAddOnOptions", GameMenuFrame, "GameMenuButtonTemplate");
menuButton:SetText(ADDON_OPTIONS);
local spliced;
GameMenuFrame:HookScript("OnShow", function()
	if spliced then return end
	spliced = true;
	local point, relativeTo, relativePoint, x, y = GameMenuButtonKeybindings:GetPoint();
	menuButton:SetPoint(point, relativeTo, relativePoint, x, y);
	GameMenuButtonKeybindings:ClearAllPoints();
	GameMenuButtonKeybindings:SetPoint(point, menuButton, relativePoint, x, y);
	GameMenuFrame:SetHeight(GameMenuFrame:GetUnscaledHeight() + menuButton:GetUnscaledHeight() - y);
end);
menuButton:SetScript("OnClick", function()
	PlaySound("igMainMenuOption");
	SettingsPanel:Open();
end);
