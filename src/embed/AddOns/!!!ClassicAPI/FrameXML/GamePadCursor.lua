-- Gamepad cursor control from the interface, as Classic Era's FrameXML asks
-- for it: opening a window, a bag or the game menu turns it on, closing
-- windows or the game menu turns it off, and Escape turns it on first.
-- Each call is gated by CanAutoSetGamePadCursorControl, which is true only
-- while the gamepad is in use, so nothing here changes the interface for the
-- mouse.
--
-- 1.12.1's functions are wrapped, not replaced, so a client's own changes to
-- them stay.

-- ShowUIPanel (UIParentPanelManager.lua): on, before anything else. The one
-- ToggleGameMenu opens at the end of its Escape chain turns cursor control
-- off instead while it is on (UIParent.lua's ToggleGameMenu, the case after
-- ClearTarget); its open sound is held until the menu really opens.
local inGameMenuToggle = false;
local heldSound = nil;

local showUIPanel = ShowUIPanel;
function ShowUIPanel(frame, ...)
	if ( inGameMenuToggle and frame == GameMenuFrame and CanAutoSetGamePadCursorControl(false) and (not IsModifierKeyDown()) ) then
		heldSound = nil;
		SetGamePadCursorControl(false);
		return;
	end
	if ( CanAutoSetGamePadCursorControl(true) ) then
		SetGamePadCursorControl(true);
	end
	if ( heldSound ) then
		local sound = heldSound;
		heldSound = nil;
		PlaySound(sound);
	end
	return showUIPanel(frame, ...);
end

local playSound = PlaySound;
local function HoldMenuSound(sound, ...)
	if ( sound == "igMainMenuOpen" ) then
		heldSound = sound;
		return;
	end
	return playSound(sound, ...);
end

-- ToggleGameMenu (UIParent.lua): with the gamepad in use, Escape first stops
-- a cast, stops spell targeting or clears the target, and otherwise turns
-- cursor control on. The main menu button's call (`clicked`) is left alone.
local toggleGameMenu = ToggleGameMenu;
function ToggleGameMenu(clicked, ...)
	if ( clicked ) then
		return toggleGameMenu(clicked, ...);
	end
	if ( CanAutoSetGamePadCursorControl(true) and (not IsModifierKeyDown()) ) then
		if ( SpellStopCasting() ) then
		elseif ( SpellStopTargeting() ) then
		elseif ( ClearTarget() and (not UnitIsCharmed("player")) ) then
		else
			SetGamePadCursorControl(true);
		end
		return;
	end
	inGameMenuToggle = true;
	PlaySound = HoldMenuSound;
	local ok, err = pcall(toggleGameMenu, clicked, ...);
	PlaySound = playSound;
	inGameMenuToggle = false;
	if ( heldSound ) then
		local sound = heldSound;
		heldSound = nil;
		PlaySound(sound);
	end
	if ( not ok ) then
		error(err, 0);
	end
end

-- CloseAllWindows (UIParentPanelManager.lua): off when anything closed.
local closeAllWindows = CloseAllWindows;
function CloseAllWindows(...)
	local anyClosed = closeAllWindows(...);
	if ( anyClosed and CanAutoSetGamePadCursorControl(false) ) then
		SetGamePadCursorControl(false);
	end
	return anyClosed;
end

-- ToggleBag (ContainerFrame_Shared.lua): on when a bag opens.
local toggleBag = ToggleBag;
function ToggleBag(id, ...)
	local wasOpen = IsBagOpen(id);
	toggleBag(id, ...);
	if ( (not wasOpen) and IsBagOpen(id) and CanAutoSetGamePadCursorControl(true) ) then
		SetGamePadCursorControl(true);
	end
end

-- GameMenuFrameMixin:OnShow / OnHide (GameMenuFrame.lua).
GameMenuFrame:HookScript("OnShow", function()
	if ( CanAutoSetGamePadCursorControl(true) ) then
		SetGamePadCursorControl(true);
	end
end);
GameMenuFrame:HookScript("OnHide", function()
	if ( CanAutoSetGamePadCursorControl(false) ) then
		SetGamePadCursorControl(false);
	end
end);
