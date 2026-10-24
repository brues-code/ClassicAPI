-- ClassicAPI's own entry in the AddOn Options window
-- (FrameXML\InterfaceOptions.lua): a ClassicAPI page with Controls, Combat
-- and Text To Speech pages under it.
--
-- Where retail's Settings shows a setting, the option copies its label,
-- tooltip and value mapping (Blizzard_SettingsDefinitions_Frame: Controls,
-- Combat, Accessibility; the TTS options use the Text To Speech window's
-- labels). Retail has no labels for the player cache, the cast settings or
-- friendly soft targeting, so those strings are ClassicAPI's own.
--
-- Changes apply at once. Cancel (and Escape) restores what each page held
-- when it was first shown since the last Okay or Cancel; Defaults restores
-- the shown page's defaults.
--
-- Lua 5.0: no `...` expression or select().

local L = {
	ABOUT = "Settings for the features that ClassicAPI adds to the game.",
	PLAYER_CACHE = "Player Cache",
	REMEMBER_PLAYERS = "Remember players",
	REMEMBER_PLAYERS_TOOLTIP = "Saves the name, class, race and sex of the players you meet, so addons can show them in later sessions. All your characters on this realm share the data.",
	SCAN_PLAYERS = "Scan nearby players",
	SCAN_PLAYERS_TOOLTIP = "Every 10 seconds, adds all the players near you to the player cache. Needs Remember players.",
	FRIEND_TARGETING = "Enable Friendly Action Targeting",
	FRIEND_TARGETING_TOOLTIP = "Picks the nearest friendly unit where you are looking. When you have no target, your helpful spells go to it.",
	AUTO_STAND = "Auto Stand",
	AUTO_STAND_TOOLTIP = "Stand up when you cast a spell while you sit.",
	AUTO_DISMOUNT = "Auto Dismount",
	AUTO_DISMOUNT_TOOLTIP = "Dismount when you cast a spell while you are mounted.",
	AUTO_UNSHIFT = "Auto Cancel Form",
	AUTO_UNSHIFT_TOOLTIP = "Leave your form when you cast a spell that you cannot cast in it.",
}

-- Enum.SoftTargetEnableFlags: retail's checkboxes switch between Any (on)
-- and Gamepad (off).
local SOFT_TARGET_GAMEPAD, SOFT_TARGET_ANY = 1, 3

-- ---------------------------------------------------------------------------
-- Settings: get / set / reset over cvars or the player cache
-- ---------------------------------------------------------------------------

local function ResetCVars(cvars)
	for _, cvar in ipairs(cvars) do
		SetCVar(cvar, GetCVarDefault(cvar))
	end
end

local function CVarSetting(cvar)
	return {
		get = function() return GetCVar(cvar) == "1" end,
		set = function(value) SetCVar(cvar, value and "1" or "0") end,
		reset = function() ResetCVars({ cvar }) end,
	}
end

local function NumberCVarSetting(cvar)
	return {
		get = function() return tonumber(GetCVar(cvar)) end,
		set = function(value) SetCVar(cvar, tostring(value)) end,
		reset = function() ResetCVars({ cvar }) end,
	}
end

-- Retail's "Enable Interact Key" / "Enable Action Targeting" proxies.
local function SoftTargetSetting(cvar)
	return {
		get = function() return tonumber(GetCVar(cvar)) == SOFT_TARGET_ANY end,
		set = function(value) SetCVar(cvar, tostring(value and SOFT_TARGET_ANY or SOFT_TARGET_GAMEPAD)) end,
		reset = function() ResetCVars({ cvar }) end,
	}
end

-- Retail's "Interact Key Icons" proxy: 1 = NPCs only, 2 = all, 3 = none.
local ICON_CVARS = { "SoftTargetIconEnemy", "SoftTargetIconInteract", "SoftTargetIconGameObject", "SoftTargetLowPriorityIcons" }
local ICON_PRESETS = {
	[1] = { "0", "1", "0", "0" },
	[2] = { "1", "1", "1", "1" },
	[3] = { "0", "0", "0", "0" },
}
local interactIconsSetting = {
	get = function()
		local on, off = true, true
		for _, cvar in ipairs(ICON_CVARS) do
			if GetCVar(cvar) == "1" then off = false else on = false end
		end
		if on then return 2 elseif off then return 3 else return 1 end
	end,
	set = function(value)
		for i, cvar in ipairs(ICON_CVARS) do
			SetCVar(cvar, ICON_PRESETS[value][i])
		end
	end,
	reset = function() ResetCVars(ICON_CVARS) end,
}

local function PlayerCacheSetting(isEnabled, setEnabled)
	return {
		get = function() return isEnabled() and true or false end,
		set = function(value) setEnabled(value) end,
		reset = function() setEnabled(false) end,
	}
end

local rememberSetting = PlayerCacheSetting(C_PlayerCache.IsEnabled, C_PlayerCache.SetEnabled)
local scanSetting = PlayerCacheSetting(C_PlayerCache.IsScanEnabled, C_PlayerCache.SetScanEnabled)

-- ---------------------------------------------------------------------------
-- Pages
-- ---------------------------------------------------------------------------

local numControls = 0
local function NextName()
	numControls = numControls + 1
	return "ClassicAPIOptionsControl" .. numControls
end

local function RefreshControls(page)
	page.updating = true
	for _, control in ipairs(page.controls) do
		control:Update()
	end
	page.updating = nil
end

local function NewPage(name, parent, title, subtitle)
	local page = CreateFrame("Frame")
	page.name = name
	page.parent = parent
	page.controls = {}

	local heading = page:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
	heading:SetPoint("TOPLEFT", page, "TOPLEFT", 16, -16)
	heading:SetText(title)
	page.y = -40
	if subtitle then
		local text = page:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
		text:SetPoint("TOPLEFT", heading, "BOTTOMLEFT", 0, -8)
		text:SetPoint("RIGHT", page, "RIGHT", -32, 0)
		text:SetJustifyH("LEFT")
		text:SetText(subtitle)
		page.y = -64
	end

	page.refresh = function()
		if not page.saved then
			page.saved = {}
			for i, control in ipairs(page.controls) do
				page.saved[i] = control.setting.get()
			end
		end
		RefreshControls(page)
	end
	page.okay = function()
		page.saved = nil
	end
	page.cancel = function()
		if not page.saved then return end
		for i, control in ipairs(page.controls) do
			if control.setting.get() ~= page.saved[i] then
				control.setting.set(page.saved[i])
			end
		end
		page.saved = nil
	end
	page.default = function()
		for _, control in ipairs(page.controls) do
			control.setting.reset()
		end
		RefreshControls(page)
	end

	InterfaceOptions_AddCategory(page)
	return page
end

local function AddControl(page, control, setting, height)
	control.setting = setting
	table.insert(page.controls, control)
	page.y = page.y - height
end

local function AddHeader(page, text)
	local header = page:CreateFontString(nil, "ARTWORK", "GameFontNormal")
	header:SetPoint("TOPLEFT", page, "TOPLEFT", 16, page.y - 8)
	header:SetText(text)
	page.y = page.y - 28
end

-- `enabledIf`, when given, greys the checkbox out while it returns false.
local function AddCheckbox(page, label, tooltip, setting, enabledIf)
	local check = CreateFrame("CheckButton", NextName(), page, "OptionsCheckButtonTemplate")
	check:SetPoint("TOPLEFT", page, "TOPLEFT", 16, page.y)
	local text = getglobal(check:GetName() .. "Text")
	text:SetText(label)
	check.tooltipText = tooltip
	check:SetScript("OnClick", function()
		this.setting.set(this:GetChecked() and true or false)
		RefreshControls(page)
	end)
	check.Update = function(self)
		self:SetChecked(self.setting.get())
		if not enabledIf or enabledIf() then
			self:Enable()
			text:SetTextColor(NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b)
		else
			self:Disable()
			text:SetTextColor(GRAY_FONT_COLOR.r, GRAY_FONT_COLOR.g, GRAY_FONT_COLOR.b)
		end
	end
	AddControl(page, check, setting, 30)
	return check
end

-- `options()` returns the entries as { value = ..., text = ... }.
local function AddDropDown(page, label, tooltip, setting, options)
	local title = page:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
	title:SetPoint("TOPLEFT", page, "TOPLEFT", 20, page.y - 6)
	title:SetText(label)

	local dropdown = CreateFrame("Frame", NextName(), page, "UIDropDownMenuTemplate")
	dropdown:SetPoint("TOPLEFT", page, "TOPLEFT", 0, page.y - 20)
	UIDropDownMenu_SetWidth(180, dropdown)
	dropdown:EnableMouse(true)
	dropdown:SetScript("OnEnter", function()
		GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
		GameTooltip:SetText(tooltip, nil, nil, nil, nil, 1)
	end)
	dropdown:SetScript("OnLeave", function() GameTooltip:Hide() end)

	local function Initialize()
		local selected = setting.get()
		for _, option in ipairs(options()) do
			local info = {}
			info.text = option.text
			info.value = option.value
			info.checked = option.value == selected and 1 or nil
			info.func = function()
				UIDropDownMenu_SetSelectedValue(dropdown, this.value)
				setting.set(this.value)
			end
			UIDropDownMenu_AddButton(info)
		end
	end
	dropdown.Update = function(self)
		UIDropDownMenu_Initialize(self, Initialize)
		UIDropDownMenu_SetSelectedValue(self, setting.get())
	end
	AddControl(page, dropdown, setting, 56)
	return dropdown
end

local function AddSlider(page, label, tooltip, setting, minValue, maxValue)
	local slider = CreateFrame("Slider", NextName(), page, "OptionsSliderTemplate")
	slider:SetPoint("TOPLEFT", page, "TOPLEFT", 24, page.y - 20)
	slider:SetWidth(200)
	slider:SetMinMaxValues(minValue, maxValue)
	slider:SetValueStep(1)
	slider.tooltipText = tooltip
	getglobal(slider:GetName() .. "Text"):SetText(label)
	getglobal(slider:GetName() .. "Low"):SetText(minValue)
	getglobal(slider:GetName() .. "High"):SetText(maxValue)

	local value = slider:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
	value:SetPoint("LEFT", slider, "RIGHT", 8, 0)

	slider:SetScript("OnValueChanged", function()
		local v = math.floor(this:GetValue() + 0.5)
		value:SetText(v)
		if not page.updating and v ~= setting.get() then
			setting.set(v)
		end
	end)
	slider.Update = function(self)
		local v = setting.get()
		self:SetValue(v)
		value:SetText(v)
	end
	AddControl(page, slider, setting, 56)
	return slider
end

-- ---------------------------------------------------------------------------
-- ClassicAPI
-- ---------------------------------------------------------------------------

local version = GetAddOnMetadata("!!!ClassicAPI", "Version")
local main = NewPage("ClassicAPI", nil, "ClassicAPI " .. version, L.ABOUT)

-- ---------------------------------------------------------------------------
-- Controls
-- ---------------------------------------------------------------------------

local controls = NewPage(CONTROLS_LABEL, main.name, CONTROLS_LABEL)

AddCheckbox(controls, ENABLE_INTERACT_TEXT, OPTION_TOOLTIP_ENABLE_INTERACT, SoftTargetSetting("SoftTargetInteract"))
AddDropDown(controls, INTERACT_ICONS_OPTION, OPTION_TOOLTIP_INTERACT_ICONS, interactIconsSetting, function()
	return {
		{ value = 1, text = INTERACT_ICONS_DEFAULT },
		{ value = 2, text = INTERACT_ICONS_SHOW_ALL },
		{ value = 3, text = INTERACT_ICONS_SHOW_NONE },
	}
end)
AddCheckbox(controls, L.AUTO_STAND, L.AUTO_STAND_TOOLTIP, CVarSetting("autoStand"))
AddCheckbox(controls, L.AUTO_DISMOUNT, L.AUTO_DISMOUNT_TOOLTIP, CVarSetting("autoDismount"))
AddCheckbox(controls, L.AUTO_UNSHIFT, L.AUTO_UNSHIFT_TOOLTIP, CVarSetting("autoUnshift"))

-- ---------------------------------------------------------------------------
-- Combat
-- ---------------------------------------------------------------------------

local combat = NewPage(COMBAT_LABEL, main.name, COMBAT_LABEL)

AddCheckbox(combat, ACTION_TARGETING_OPTION, OPTION_TOOLTIP_ACTION_TARGETING, SoftTargetSetting("SoftTargetEnemy"))
-- Retail has no friend checkbox; same mapping as its enemy one.
AddCheckbox(combat, L.FRIEND_TARGETING, L.FRIEND_TARGETING_TOOLTIP, SoftTargetSetting("SoftTargetFriend"))
AddCheckbox(combat, ACTION_BUTTON_USE_KEY_DOWN, OPTION_TOOLTIP_ACTION_BUTTON_USE_KEY_DOWN, CVarSetting("ActionButtonUseKeyDown"))
AddCheckbox(combat, STOP_AUTO_ATTACK, OPTION_TOOLTIP_STOP_AUTO_ATTACK, CVarSetting("stopAutoAttackOnTargetChange"))

-- ---------------------------------------------------------------------------
-- Text To Speech
-- ---------------------------------------------------------------------------

local tts = NewPage(TEXT_TO_SPEECH, main.name, TEXT_TO_SPEECH)

AddDropDown(tts, TEXT_TO_SPEECH_VOICE_OPTION, TEXT_TO_SPEECH_VOICE_OPTION, NumberCVarSetting("ttsVoice"), function()
	local options = {}
	for _, voice in ipairs(C_VoiceChat.GetTtsVoices()) do
		table.insert(options, { value = voice.voiceID, text = voice.name })
	end
	return options
end)
AddSlider(tts, TEXT_TO_SPEECH_ADJUST_RATE, TEXT_TO_SPEECH_ADJUST_RATE, NumberCVarSetting("ttsSpeed"), -10, 10)
AddSlider(tts, TEXT_TO_SPEECH_ADJUST_VOLUME, TEXT_TO_SPEECH_ADJUST_VOLUME, NumberCVarSetting("ttsVolume"), 0, 100)

-- ---------------------------------------------------------------------------
-- Player Cache
-- ---------------------------------------------------------------------------

local playerCache = NewPage(L.PLAYER_CACHE, main.name, L.PLAYER_CACHE)
AddCheckbox(playerCache, L.REMEMBER_PLAYERS, L.REMEMBER_PLAYERS_TOOLTIP, rememberSetting)
AddCheckbox(playerCache, L.SCAN_PLAYERS, L.SCAN_PLAYERS_TOOLTIP, scanSetting, rememberSetting.get)
