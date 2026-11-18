-- ClassicAPI Replay: the damage meter in the window. It is a session window
-- of ClassicAPI_DamageMeter (the port of retail's Blizzard_DamageMeter), made
-- from its template, that reads the replay's meter at the replay's time
-- (C_CombatLogReplay.GetDamageMeterSession / GetDamageMeterSessionSource)
-- in place of C_DamageMeter's sessions.
--
-- The window opens ClassicAPI_DamageMeter whatever damageMeterEnabled says.
-- The live meter keeps itself hidden while that cvar is off
-- (DamageMeterMixin:ShouldBeShown); this window is the replay's own.
--
-- - It sits in the scene's top right corner, clear of the followed unit's
--   frame (top left), the log's lines (bottom left), the spell tooltip
--   (bottom right) and the unit list (right of the scene). It is a child of
--   the scene, so it draws above the scene's models as the scene's other
--   frames do.
-- - Its owner is ReplayMeterOwner below, with only the methods the session
--   window calls once its settings dropdown is hidden: one window, which does
--   not move, resize or hide. The settings dropdown is hidden because each of
--   its actions belongs to the live meter: resetting C_DamageMeter's data,
--   the live meter's windows, and the live meter's saved settings.
-- - It looks as the live meter's preset does (DamageMeterSystem.lua).
-- - It shows the replay as one Current session up to the replay's time. The
--   live meter's DAMAGE_METER_* events say nothing about the replay, so the
--   window has no OnEvent; it refreshes from the playback instead: every
--   REFRESH_INTERVAL seconds of real time while the replay's time moves, at
--   once on a jump (Seek), and from the top for a new replay.

local _, ns = ...;

local DAMAGE_METER_ADDON = "ClassicAPI_DamageMeter";
local WIDTH, HEIGHT = 260, 220;
local INSET = 8;
-- Real seconds between refreshes while the replay plays.
local REFRESH_INTERVAL = 0.25;

local meter;           -- the session window, once made
local session;         -- the session it showed last
local sinceRefresh = 0;
local refreshedTime;   -- the replay time it showed last
local discardNext = false;

------------------------------------------------------------------------------
-- The owner

-- The session window's owner, for this one window (see the top of the file).
local ReplayMeterOwner = {};

function ReplayMeterOwner:CanMoveOrResizeSessionWindow(sessionWindow)
	return false;
end

function ReplayMeterOwner:SetSessionWindowDamageMeterType(sessionWindow, damageMeterType)
	sessionWindow:SetDamageMeterType(damageMeterType);
end

function ReplayMeterOwner:SetSessionWindowSessionID(sessionWindow, sessionType, sessionID)
	sessionWindow:SetSession(sessionType, sessionID);
end

function ReplayMeterOwner:SetSessionWindowMinimized(sessionWindow, minimized)
	sessionWindow:SetMinimized(minimized);
end

------------------------------------------------------------------------------
-- The window's data: the replay at its time

local function GetCombatSession(self)
	session = C_CombatLogReplay.GetDamageMeterSession(self:GetDamageMeterType(), ns.now);
	return session;
end

-- The replay is one session, not a list of them.
local function GetAvailableCombatSessions(self)
	return {};
end

-- The session as last read: the timer reads it every frame.
local function GetSessionDurationSeconds(self, sessionType)
	return session and session.durationSeconds;
end

-- The timer shows whenever the window shows the replay's session, not
-- while the player is in combat in the world.
local function IsPlayerInCombat(self)
	return #ns.units > 0;
end

local function GetCombatSessionSource(self)
	return C_CombatLogReplay.GetDamageMeterSessionSource(self:GetDamageMeterType(), ns.now, self.sourceGUID,
		self.sourceCreatureID);
end

------------------------------------------------------------------------------
-- Making the window

local function CreateMeter()
	local settings = DAMAGE_METER_SYSTEM_DEFAULT_SETTINGS;
	local setting = DamageMeterSystemSetting;

	meter = CreateFrame("Frame", nil, ns.scene, "DamageMeterSessionWindowTemplate");
	meter:SetScript("OnEvent", nil);
	meter.GetCombatSession = GetCombatSession;
	meter.GetAvailableCombatSessions = GetAvailableCombatSessions;
	meter.GetSessionDurationSeconds = GetSessionDurationSeconds;
	meter.IsPlayerInCombat = IsPlayerInCombat;
	meter:GetSourceWindow().GetCombatSessionSource = GetCombatSessionSource;
	-- The replay's window is in DIALOG; the source window's own HIGH would
	-- put it behind the replay.
	meter:GetSourceWindow():SetFrameStrata("FULLSCREEN_DIALOG");
	meter:GetSettingsDropdown():Hide();

	meter:SetDamageMeterOwner(ReplayMeterOwner, 1);
	meter:SetDamageMeterType(Enum.DamageMeterType.DamageDone);
	meter:SetSession(Enum.DamageMeterSessionType.Current, nil);
	meter:SetUseClassColor(settings[setting.ShowClassColor] == 1);
	meter:SetBarHeight(settings[setting.BarHeight]);
	meter:SetBarSpacing(settings[setting.Padding]);
	meter:SetTextScale(settings[setting.TextSize] * DAMAGE_METER_TEXT_SIZE_TO_SCALE_MULTIPLIER);
	meter:SetAlpha(settings[setting.Transparency] * DAMAGE_METER_TRANSPARENCY_TO_ALPHA_MULTIPLIER);
	meter:SetShowBarIcons(settings[setting.ShowSpecIcon] == 1);
	meter:SetBackgroundAlpha(settings[setting.BackgroundTransparency] * DAMAGE_METER_TRANSPARENCY_TO_ALPHA_MULTIPLIER);
	meter:SetStyle(settings[setting.Style]);
	meter:SetNumberDisplayType(settings[setting.Numbers]);

	meter:ClearAllPoints();
	meter:SetPoint("TOPRIGHT", ns.scene, "TOPRIGHT", -INSET, -INSET);
	meter:SetSize(WIDTH, HEIGHT);
	meter:Show();
end

-- Opens the meter with the window: loads ClassicAPI_DamageMeter the first
-- time and makes the meter.
local function ShowMeter()
	if meter then
		return;
	end
	if not IsAddOnLoaded(DAMAGE_METER_ADDON) then
		UIParentLoadAddOn(DAMAGE_METER_ADDON);
	end
	if IsAddOnLoaded(DAMAGE_METER_ADDON) then
		CreateMeter();
	end
end
ns.ShowMeter = ShowMeter;

------------------------------------------------------------------------------
-- Refreshing

local function RefreshMeter()
	if not meter then
		return;
	end
	sinceRefresh = 0;
	refreshedTime = ns.now;
	if discardNext then
		discardNext = false;
		meter:Refresh(ScrollBoxConstants.DiscardScrollPosition);
	else
		meter:Refresh(ScrollBoxConstants.RetainScrollPosition);
	end
end
ns.RefreshMeter = RefreshMeter;

-- A new replay: its first refresh starts the list from the top.
local function ResetMeter()
	discardNext = true;
	if meter then
		meter:HideSourceWindow();
	end
end
ns.ResetMeter = ResetMeter;

-- Every frame: refreshes every REFRESH_INTERVAL real seconds while the
-- replay's time moves.
local function UpdateMeter(elapsed)
	if not meter then
		return;
	end
	sinceRefresh = sinceRefresh + elapsed;
	if sinceRefresh >= REFRESH_INTERVAL and ns.now ~= refreshedTime then
		RefreshMeter();
	end
end
ns.UpdateMeter = UpdateMeter;
