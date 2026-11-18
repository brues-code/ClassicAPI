-- ClassicAPI's stand-in for the damage meter's Edit Mode system
-- (Blizzard_EditMode's EditModeDamageMeterSystemTemplate and
-- EditModeDamageMeterSystemMixin). This client has no Edit Mode, so:
--   - The settings start at the values of retail's preset layout
--     (EditModePresetLayouts.lua, Enum.EditModeSystem.DamageMeter), given here
--     as the display values Edit Mode turns them into
--     (EditModeSettingDisplayInfo.lua: ConvertValueDiffFromMin and
--     ConvertValueDefault). The frame starts at the preset's anchor.
--   - Style, Numbers and Visibility can be changed from each window's
--     settings dropdown (DamageMeterSessionWindowMixin:
--     InitializeSettingsDropdown) and are saved per character in
--     DamageMeterPerCharacterSettings.systemSettings. Visibility is also on
--     the Advanced settings page (!!!ClassicAPI Options\Panel.lua), because a
--     hidden window cannot show its dropdown. The other settings
--     (frame size, bar height, padding, opacity, background, text size, spec
--     icon, class color) keep the preset's values.
--   - Every window, the primary one too, moves and resizes while it is
--     unlocked (DamageMeterMixin:CanMoveOrResizeSessionWindow), and saves its
--     place and size (DamageMeterMixin:SaveSessionWindowPosition).
--   - The visibility setting has no Hidden choice: without Edit Mode nothing
--     could show the meter again. Turning damageMeterEnabled off hides it.

DamageMeterSystemSetting = {
	Style = "Style",
	Numbers = "Numbers",
	FrameWidth = "FrameWidth",
	FrameHeight = "FrameHeight",
	BarHeight = "BarHeight",
	Padding = "Padding",
	Transparency = "Transparency",
	ShowSpecIcon = "ShowSpecIcon",
	ShowClassColor = "ShowClassColor",
	TextSize = "TextSize",
	Visibility = "Visibility",
	BackgroundTransparency = "BackgroundTransparency",
};

-- Global so another window built on the session window template (the combat
-- log replay's) can use the same look.
DAMAGE_METER_SYSTEM_DEFAULT_SETTINGS = {
	[DamageMeterSystemSetting.Visibility] = Enum.DamageMeterVisibility.Always,
	[DamageMeterSystemSetting.Style] = Enum.DamageMeterStyle.Default,
	[DamageMeterSystemSetting.Numbers] = Enum.DamageMeterNumbers.Compact,
	[DamageMeterSystemSetting.FrameWidth] = 400,		-- 200 + minValue 200
	[DamageMeterSystemSetting.FrameHeight] = 140,		-- 20 + minValue 120
	[DamageMeterSystemSetting.BarHeight] = 16,			-- 1 + minValue 15
	[DamageMeterSystemSetting.Padding] = 4,				-- 2 * stepSize 1 + minValue 2
	[DamageMeterSystemSetting.Transparency] = 100,		-- 50 * stepSize 1 + minValue 50
	[DamageMeterSystemSetting.ShowSpecIcon] = 1,
	[DamageMeterSystemSetting.ShowClassColor] = 1,
	[DamageMeterSystemSetting.TextSize] = 100,			-- 5 * stepSize 10 + minValue 50
	[DamageMeterSystemSetting.BackgroundTransparency] = 50,	-- 50 * stepSize 1 + minValue 0
};

-- The settings the window's settings dropdown offers, from
-- EditModeSettingDisplayInfo.lua's Enum.EditModeSystem.DamageMeter entries.
DAMAGE_METER_SYSTEM_MENU_SETTINGS =
{
	-- Style
	{
		setting = DamageMeterSystemSetting.Style,
		name = HUD_EDIT_MODE_SETTING_DAMAGE_METER_STYLE,
		options = {
			{ value = Enum.DamageMeterStyle.Default, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_STYLE_DEFAULT },
			{ value = Enum.DamageMeterStyle.Bordered, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_STYLE_BORDERED },
			{ value = Enum.DamageMeterStyle.Thin, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_STYLE_THIN },
		},
	},

	-- Numbers
	{
		setting = DamageMeterSystemSetting.Numbers,
		name = HUD_EDIT_MODE_SETTING_DAMAGE_METER_NUMBERS,
		options = {
			{ value = Enum.DamageMeterNumbers.Minimal, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_NUMBERS_MINIMAL },
			{ value = Enum.DamageMeterNumbers.Compact, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_NUMBERS_COMPACT },
			{ value = Enum.DamageMeterNumbers.Complete, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_NUMBERS_COMPLETE },
		},
	},

	-- Visibility (no Hidden, see the top of this file)
	{
		setting = DamageMeterSystemSetting.Visibility,
		name = HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY,
		options =
		{
			{value = Enum.DamageMeterVisibility.Always, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY_ALWAYS},
			{value = Enum.DamageMeterVisibility.InCombat, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY_IN_COMBAT},
			{value = Enum.DamageMeterVisibility.InGroup, text = HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY_IN_GROUP},
		},
	},
};

DamageMeterSystemMixin = {};

local function GetSavedSystemSettings()
	if not DamageMeterPerCharacterSettings then
		DamageMeterPerCharacterSettings = { windowDataList = {} };
	end

	local systemSettings = DamageMeterPerCharacterSettings.systemSettings;
	if type(systemSettings) ~= "table" then
		systemSettings = {};
		DamageMeterPerCharacterSettings.systemSettings = systemSettings;
	end

	return systemSettings;
end

-- EditModeDamageMeterSystemMixin:OnSystemLoad, without the Edit Mode resize
-- button. The size comes from the preset here, as Edit Mode sets it from the
-- layout.
function DamageMeterSystemMixin:OnSystemLoad()
	self:SetSize(self:GetSettingValue(DamageMeterSystemSetting.FrameWidth), self:GetSettingValue(DamageMeterSystemSetting.FrameHeight));
end

function DamageMeterSystemMixin:GetSettingValue(setting)
	local value = GetSavedSystemSettings()[setting];
	if value == nil then
		return DAMAGE_METER_SYSTEM_DEFAULT_SETTINGS[setting];
	end
	return value;
end

function DamageMeterSystemMixin:GetSettingValueBool(setting)
	return self:GetSettingValue(setting) == 1;
end

-- EditModeManagerFrame:OnSystemSettingChange for this system: keep the value
-- and apply it.
function DamageMeterSystemMixin:OnSystemSettingChange(setting, value)
	GetSavedSystemSettings()[setting] = value;
	self:UpdateSystemSetting(setting);
end

function DamageMeterSystemMixin:UpdateSystemSettingVisibility()
	self.visibility = self:GetSettingValue(DamageMeterSystemSetting.Visibility);
	self:UpdateShownState();
end

function DamageMeterSystemMixin:UpdateSystemSettingStyle()
	local style = self:GetSettingValue(DamageMeterSystemSetting.Style);
	self:SetStyle(style);
end

function DamageMeterSystemMixin:UpdateSystemSettingNumberDisplayType()
	local numberDisplayType = self:GetSettingValue(DamageMeterSystemSetting.Numbers);
	self:SetNumberDisplayType(numberDisplayType);
end

function DamageMeterSystemMixin:UpdateSystemSettingBarHeight()
	local barHeight = self:GetSettingValue(DamageMeterSystemSetting.BarHeight);
	self:SetBarHeight(barHeight);
end

function DamageMeterSystemMixin:UpdateSystemSettingPadding()
	local barSpacing = self:GetSettingValue(DamageMeterSystemSetting.Padding);
	self:SetBarSpacing(barSpacing);
end

function DamageMeterSystemMixin:UpdateSystemSettingTransparency()
	local transparency = self:GetSettingValue(DamageMeterSystemSetting.Transparency);
	self:SetWindowTransparency(transparency);
end

function DamageMeterSystemMixin:UpdateSystemSettingShowSpecIcon()
	local showBarIcons = self:GetSettingValueBool(DamageMeterSystemSetting.ShowSpecIcon);
	self:SetShowBarIcons(showBarIcons);
end

function DamageMeterSystemMixin:UpdateSystemSettingShowClassColor()
	local useClassColor = self:GetSettingValueBool(DamageMeterSystemSetting.ShowClassColor);
	self:SetUseClassColor(useClassColor);
end

function DamageMeterSystemMixin:UpdateSystemSettingTextSize()
	local textSize = self:GetSettingValue(DamageMeterSystemSetting.TextSize);
	self:SetTextSize(textSize);
end

function DamageMeterSystemMixin:UpdateSystemSettingBackgroundTransparency()
	local backgroundTransparency = self:GetSettingValue(DamageMeterSystemSetting.BackgroundTransparency);
	self:SetBackgroundTransparency(backgroundTransparency);
end

local SETTING_UPDATERS = {
	[DamageMeterSystemSetting.Visibility] = DamageMeterSystemMixin.UpdateSystemSettingVisibility,
	[DamageMeterSystemSetting.Style] = DamageMeterSystemMixin.UpdateSystemSettingStyle,
	[DamageMeterSystemSetting.Numbers] = DamageMeterSystemMixin.UpdateSystemSettingNumberDisplayType,
	[DamageMeterSystemSetting.BarHeight] = DamageMeterSystemMixin.UpdateSystemSettingBarHeight,
	[DamageMeterSystemSetting.Padding] = DamageMeterSystemMixin.UpdateSystemSettingPadding,
	[DamageMeterSystemSetting.Transparency] = DamageMeterSystemMixin.UpdateSystemSettingTransparency,
	[DamageMeterSystemSetting.ShowSpecIcon] = DamageMeterSystemMixin.UpdateSystemSettingShowSpecIcon,
	[DamageMeterSystemSetting.ShowClassColor] = DamageMeterSystemMixin.UpdateSystemSettingShowClassColor,
	[DamageMeterSystemSetting.TextSize] = DamageMeterSystemMixin.UpdateSystemSettingTextSize,
	[DamageMeterSystemSetting.BackgroundTransparency] = DamageMeterSystemMixin.UpdateSystemSettingBackgroundTransparency,
};

function DamageMeterSystemMixin:UpdateSystemSetting(setting)
	local updater = SETTING_UPDATERS[setting];
	if updater then
		updater(self);
	end
end

-- Edit Mode applies a system's whole layout once the system exists
-- (EditModeSystemMixin:UpdateSystem); this applies the preset and the saved
-- values the same way, setting by setting.
function DamageMeterSystemMixin:UpdateSystem()
	for setting in pairs(SETTING_UPDATERS) do
		self:UpdateSystemSetting(setting);
	end
end
