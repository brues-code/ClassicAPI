-- ClassicAPI's own entry in the AddOn Options window
-- (FrameXML\InterfaceOptions.lua): a ClassicAPI page with one page under it
-- for each feature. Every cvar that ClassicAPI registers has a control here,
-- and so does the player cache.
--
-- Each control reads and writes exactly one setting, and writes it only when
-- the player uses that control. So the pages have no okay, cancel or default:
-- Okay, Cancel and Escape only close the window, and Defaults does nothing
-- here. A page reads its settings again each time it is shown.
--
-- A page's controls scroll under its heading in a ScrollBox, so a page holds
-- as many as its feature has. Dropdowns and number boxes sit on the same row
-- as their label.
--
-- Every string is a global from locales\enUS.lua: retail's GlobalStrings key
-- where retail has the string (ENABLE_INTERACT_TEXT and the like), a
-- CLASSICAPI_ one where it does not.
--
-- Lua 5.0: no `...` expression or select().

local ARC_CHOICES = {
	{ value = 0, text = CLASSICAPI_DIRECTION_FRONT },
	{ value = 1, text = CLASSICAPI_DIRECTION_CONE },
	{ value = 2, text = CLASSICAPI_DIRECTION_ANY },
}

local FORCE_CHOICES = {
	{ value = 0, text = CLASSICAPI_LOOSE_TARGET_FOLLOWS_NONE },
	{ value = 1, text = CLASSICAPI_LOOSE_TARGET_FOLLOWS_ENEMY },
	{ value = 2, text = CLASSICAPI_LOOSE_TARGET_FOLLOWS_FRIEND },
}

-- ---------------------------------------------------------------------------
-- Settings: get / set over one cvar or the player cache
-- ---------------------------------------------------------------------------

-- On for any nonzero number.
local function CVarSetting(cvar)
	return {
		get = function() return (tonumber(GetCVar(cvar)) or 0) ~= 0 end,
		set = function(value) SetCVar(cvar, value and "1" or "0") end,
	}
end

local function NumberCVarSetting(cvar)
	return {
		get = function() return tonumber(GetCVar(cvar)) end,
		set = function(value) SetCVar(cvar, tostring(value)) end,
	}
end

-- The soft-target enable cvars: 0 off, 1 gamepad, 2 keyboard and mouse,
-- 3 always. 1.12 has no gamepad, so 2 and 3 are on.
local function SoftTargetSetting(cvar)
	return {
		get = function() return (tonumber(GetCVar(cvar)) or 0) >= 2 end,
		set = function(value) SetCVar(cvar, value and "3" or "0") end,
	}
end

local rememberSetting = {
	get = function() return C_PlayerCache.IsEnabled() and true or false end,
	set = function(value) C_PlayerCache.SetEnabled(value) end,
}

-- ---------------------------------------------------------------------------
-- Pages
-- ---------------------------------------------------------------------------

-- Row labels line up with the checkbox labels; row controls start at
-- CONTROL_X.
local LABEL_X = 48
local CONTROL_X = 200

-- pfUI's Interface Options skin finds the controls by these names.
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

-- The controls go on `page.content`, the one frame in the page's ScrollBox,
-- `page.y` down from its top. The clipping of the ScrollBox also keeps the
-- mouse off the controls that are scrolled out of view.
-- `title` is the heading, the category name when nil.
local function NewPage(name, parent, title, subtitle)
	local page = CreateFrame("Frame")
	page.name = name
	page.parent = parent
	page.controls = {}

	local heading = page:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
	heading:SetPoint("TOPLEFT", page, "TOPLEFT", 16, -16)
	heading:SetText(title or name)
	local top = -40
	if subtitle then
		local text = page:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
		text:SetPoint("TOPLEFT", heading, "BOTTOMLEFT", 0, -8)
		text:SetPoint("RIGHT", page, "RIGHT", -32, 0)
		text:SetJustifyH("LEFT")
		text:SetText(subtitle)
		top = -64
	end

	local scrollBox = CreateFrame("Frame", nil, page, "WowScrollBox")
	scrollBox:SetPoint("TOPLEFT", page, "TOPLEFT", 0, top)
	scrollBox:SetPoint("BOTTOMRIGHT", page, "BOTTOMRIGHT", -20, 8)
	scrollBox:EnableMouseWheel(true)
	local scrollBar = CreateFrame("EventFrame", nil, page, "MinimalScrollBar")
	scrollBar:SetPoint("TOPLEFT", scrollBox, "TOPRIGHT", 4, 0)
	scrollBar:SetPoint("BOTTOMLEFT", scrollBox, "BOTTOMRIGHT", 4, 0)

	local content = CreateFrame("Frame", nil, scrollBox)
	content:SetHeight(1)
	content.scrollable = true
	ScrollUtil.InitScrollBoxWithScrollBar(scrollBox, scrollBar, CreateScrollBoxLinearView())
	ScrollUtil.AddManagedScrollBarVisibilityBehavior(scrollBox, scrollBar)

	page.scrollBox = scrollBox
	page.content = content
	page.y = 0
	page.refresh = RefreshControls

	InterfaceOptions_AddCategory(page)
	return page
end

-- Moves `page.y` down past a row and makes the content that tall. The
-- ScrollBox measures it again on its next update (a hidden page's waits for
-- the page to be shown).
local function Grow(page, height)
	page.y = page.y - height
	page.content:SetHeight(8 - page.y)
	page.scrollBox:FullUpdate(ScrollBoxConstants.UpdateQueued)
end

local function AddControl(page, control, setting, height)
	control.setting = setting
	table.insert(page.controls, control)
	Grow(page, height)
end

local function AddHeader(page, text)
	local header = page.content:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
	header:SetPoint("TOPLEFT", page.content, "TOPLEFT", 16, page.y - 6)
	header:SetText(text)
	Grow(page, 24)
end

local function AddRowLabel(page, label)
	local text = page.content:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
	text:SetPoint("TOPLEFT", page.content, "TOPLEFT", LABEL_X, page.y - 10)
	text:SetText(label)
end

local function SetTooltip(frame, tooltip)
	frame:SetScript("OnEnter", function()
		GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
		GameTooltip:SetText(tooltip, nil, nil, nil, nil, 1)
	end)
	frame:SetScript("OnLeave", function() GameTooltip:Hide() end)
end

-- `enabledIf`, when given, greys the checkbox out while it returns false.
local function AddCheckbox(page, label, tooltip, setting, enabledIf)
	local check = CreateFrame("CheckButton", NextName(), page.content, "OptionsCheckButtonTemplate")
	check:SetPoint("TOPLEFT", page.content, "TOPLEFT", 16, page.y)
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
end

-- `choices` is a list of { value = ..., text = ... }, or a function that
-- returns one. A value with no choice shows as the number itself.
local function AddDropDown(page, label, tooltip, setting, choices)
	AddRowLabel(page, label)

	local function List()
		if type(choices) == "function" then return choices() end
		return choices
	end

	-- The template's art has a 16 pixel margin on the left.
	local dropdown = CreateFrame("Frame", NextName(), page.content, "UIDropDownMenuTemplate")
	dropdown:SetPoint("TOPLEFT", page.content, "TOPLEFT", CONTROL_X - 16, page.y)
	UIDropDownMenu_SetWidth(150, dropdown)
	dropdown:EnableMouse(true)
	SetTooltip(dropdown, tooltip)

	local function Initialize()
		local selected = setting.get()
		for _, choice in ipairs(List()) do
			local info = {}
			info.text = choice.text
			info.value = choice.value
			info.checked = choice.value == selected and 1 or nil
			info.func = function()
				UIDropDownMenu_SetSelectedValue(dropdown, this.value)
				setting.set(this.value)
			end
			UIDropDownMenu_AddButton(info)
		end
	end
	dropdown.Update = function(self)
		UIDropDownMenu_Initialize(self, Initialize)
		local selected = setting.get()
		UIDropDownMenu_SetSelectedValue(self, selected)
		local text = tostring(selected or "")
		for _, choice in ipairs(List()) do
			if choice.value == selected then text = choice.text end
		end
		UIDropDownMenu_SetText(text, self)
	end
	AddControl(page, dropdown, setting, 32)
end

-- Writes when Enter is pressed or the box loses focus; Escape puts the
-- current value back. Text that is not a number is not written.
local function AddNumberBox(page, label, tooltip, setting)
	AddRowLabel(page, label)

	local box = CreateFrame("EditBox", NextName(), page.content, "InputBoxTemplate")
	box:SetPoint("TOPLEFT", page.content, "TOPLEFT", CONTROL_X + 6, page.y - 6)
	box:SetWidth(70)
	box:SetHeight(20)
	box:SetAutoFocus(false)
	SetTooltip(box, tooltip)

	local function ShowValue(self)
		self:SetText(tostring(setting.get() or ""))
	end
	box:SetScript("OnEnterPressed", function() this:ClearFocus() end)
	box:SetScript("OnEscapePressed", function()
		ShowValue(this)
		this:ClearFocus()
	end)
	box:SetScript("OnEditFocusLost", function()
		this:HighlightText(0, 0)
		local value = tonumber(this:GetText())
		if value and value ~= setting.get() then
			setting.set(value)
		end
		ShowValue(this)
	end)
	box.Update = ShowValue
	AddControl(page, box, setting, 32)
end

local function AddSlider(page, label, tooltip, setting, minValue, maxValue, step)
	local slider = CreateFrame("Slider", NextName(), page.content, "OptionsSliderTemplate")
	slider:SetPoint("TOPLEFT", page.content, "TOPLEFT", 24, page.y - 20)
	slider:SetWidth(200)
	slider:SetMinMaxValues(minValue, maxValue)
	slider:SetValueStep(step)
	slider.tooltipText = tooltip
	getglobal(slider:GetName() .. "Text"):SetText(label)
	getglobal(slider:GetName() .. "Low"):SetText(minValue)
	getglobal(slider:GetName() .. "High"):SetText(maxValue)

	local value = slider:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
	value:SetPoint("LEFT", slider, "RIGHT", 8, 0)
	local function Format(v)
		if step < 1 then return string.format("%.2f", v) end
		return v
	end

	slider:SetScript("OnValueChanged", function()
		local v = math.floor(this:GetValue() / step + 0.5) * step
		value:SetText(Format(v))
		if not page.updating and v ~= setting.get() then
			setting.set(v)
		end
	end)
	slider.Update = function(self)
		local v = setting.get() or minValue
		self:SetValue(v)
		value:SetText(Format(v))
	end
	AddControl(page, slider, setting, 56)
end

-- ---------------------------------------------------------------------------
-- ClassicAPI
-- ---------------------------------------------------------------------------

local version = GetAddOnMetadata("!!!ClassicAPI", "Version")
local main = NewPage("ClassicAPI", nil, "ClassicAPI " .. version, CLASSICAPI_OPTIONS_ABOUT)

-- ---------------------------------------------------------------------------
-- Interact Key
-- ---------------------------------------------------------------------------

local interact = NewPage(CLASSICAPI_INTERACT_KEY, main.name)

AddCheckbox(interact, ENABLE_INTERACT_TEXT, OPTION_TOOLTIP_ENABLE_INTERACT, SoftTargetSetting("SoftTargetInteract"))
AddDropDown(interact, CLASSICAPI_INTERACT_DIRECTION, CLASSICAPI_INTERACT_DIRECTION_TOOLTIP, NumberCVarSetting("SoftTargetInteractArc"), ARC_CHOICES)
AddNumberBox(interact, CLASSICAPI_INTERACT_RANGE, CLASSICAPI_RANGE_TOOLTIP, NumberCVarSetting("SoftTargetInteractRange"))
AddCheckbox(interact, CLASSICAPI_STRICT_INTERACT_RANGE, CLASSICAPI_STRICT_INTERACT_RANGE_TOOLTIP, CVarSetting("SoftTargetInteractRangeIsHard"))
local interactIcon = CVarSetting("SoftTargetIconInteract")
AddCheckbox(interact, CLASSICAPI_INTERACT_ICON, CLASSICAPI_INTERACT_ICON_TOOLTIP, interactIcon)
-- The object icon is drawn only while the interact icon is on.
AddCheckbox(interact, CLASSICAPI_GAME_OBJECT_ICON, CLASSICAPI_GAME_OBJECT_ICON_TOOLTIP, CVarSetting("SoftTargetIconGameObject"), interactIcon.get)

AddHeader(interact, CLASSICAPI_ALL_SOFT_TARGET_ICONS)
AddCheckbox(interact, CLASSICAPI_LOW_PRIORITY_ICONS, CLASSICAPI_LOW_PRIORITY_ICONS_TOOLTIP, CVarSetting("SoftTargetLowPriorityIcons"))
AddNumberBox(interact, CLASSICAPI_ICON_SIZE, CLASSICAPI_ICON_SIZE_TOOLTIP, NumberCVarSetting("SoftTargetNameplateSize"))

-- ---------------------------------------------------------------------------
-- Action Targeting
-- ---------------------------------------------------------------------------

local targeting = NewPage(CLASSICAPI_ACTION_TARGETING, main.name)

AddHeader(targeting, CLASSICAPI_ENEMIES)
AddCheckbox(targeting, ACTION_TARGETING_OPTION, OPTION_TOOLTIP_ACTION_TARGETING, SoftTargetSetting("SoftTargetEnemy"))
AddDropDown(targeting, CLASSICAPI_ENEMY_DIRECTION, CLASSICAPI_UNIT_DIRECTION_TOOLTIP, NumberCVarSetting("SoftTargetEnemyArc"), ARC_CHOICES)
AddNumberBox(targeting, CLASSICAPI_ENEMY_RANGE, CLASSICAPI_RANGE_TOOLTIP, NumberCVarSetting("SoftTargetEnemyRange"))
AddCheckbox(targeting, CLASSICAPI_ENEMY_ICON, CLASSICAPI_ENEMY_ICON_TOOLTIP, CVarSetting("SoftTargetIconEnemy"))

AddHeader(targeting, CLASSICAPI_FRIENDS)
AddCheckbox(targeting, CLASSICAPI_FRIEND_TARGETING, CLASSICAPI_FRIEND_TARGETING_TOOLTIP, SoftTargetSetting("SoftTargetFriend"))
AddDropDown(targeting, CLASSICAPI_FRIEND_DIRECTION, CLASSICAPI_UNIT_DIRECTION_TOOLTIP, NumberCVarSetting("SoftTargetFriendArc"), ARC_CHOICES)
AddNumberBox(targeting, CLASSICAPI_FRIEND_RANGE, CLASSICAPI_RANGE_TOOLTIP, NumberCVarSetting("SoftTargetFriendRange"))
AddCheckbox(targeting, CLASSICAPI_FRIEND_ICON, CLASSICAPI_FRIEND_ICON_TOOLTIP, CVarSetting("SoftTargetIconFriend"))

AddHeader(targeting, CLASSICAPI_WHEN_YOU_HAVE_A_TARGET)
AddCheckbox(targeting, CLASSICAPI_KEEP_ACTION_TARGETING, CLASSICAPI_KEEP_ACTION_TARGETING_TOOLTIP, CVarSetting("SoftTargetWithLocked"))
AddCheckbox(targeting, CLASSICAPI_PREFER_YOUR_TARGET, CLASSICAPI_PREFER_YOUR_TARGET_TOOLTIP, CVarSetting("SoftTargetMatchLocked"))
AddDropDown(targeting, CLASSICAPI_LOOSE_TARGET_FOLLOWS, CLASSICAPI_LOOSE_TARGET_FOLLOWS_TOOLTIP, NumberCVarSetting("SoftTargetForce"), FORCE_CHOICES)

-- ---------------------------------------------------------------------------
-- Combat
-- ---------------------------------------------------------------------------

local combat = NewPage(COMBAT_LABEL, main.name)

AddHeader(combat, CLASSICAPI_ACTION_BARS)
AddCheckbox(combat, ACTION_BUTTON_USE_KEY_DOWN, OPTION_TOOLTIP_ACTION_BUTTON_USE_KEY_DOWN, CVarSetting("ActionButtonUseKeyDown"))
AddCheckbox(combat, COUNTDOWN_FOR_COOLDOWNS_TEXT, OPTION_TOOLTIP_COUNTDOWN_FOR_COOLDOWNS, CVarSetting("countdownForCooldowns"))

AddHeader(combat, CLASSICAPI_CASTING_AND_ATTACKING)
AddCheckbox(combat, CLASSICAPI_AUTO_STAND, CLASSICAPI_AUTO_STAND_TOOLTIP, CVarSetting("autoStand"))
AddCheckbox(combat, CLASSICAPI_AUTO_DISMOUNT, CLASSICAPI_AUTO_DISMOUNT_TOOLTIP, CVarSetting("autoDismount"))
AddCheckbox(combat, CLASSICAPI_AUTO_UNSHIFT, CLASSICAPI_AUTO_UNSHIFT_TOOLTIP, CVarSetting("autoUnshift"))
AddCheckbox(combat, STOP_AUTO_ATTACK, OPTION_TOOLTIP_STOP_AUTO_ATTACK, CVarSetting("stopAutoAttackOnTargetChange"))

-- ---------------------------------------------------------------------------
-- Bags and Items
-- ---------------------------------------------------------------------------

local bags = NewPage(CLASSICAPI_BAGS_AND_ITEMS, main.name)

AddCheckbox(bags, CLASSICAPI_SORT_BAGS_RIGHT_TO_LEFT, CLASSICAPI_SORT_BAGS_RIGHT_TO_LEFT_TOOLTIP, CVarSetting("sortBagsRightToLeft"))
AddCheckbox(bags, CLASSICAPI_IGNORE_BACKPACK, CLASSICAPI_IGNORE_BACKPACK_TOOLTIP, CVarSetting("backpackAutosortDisabled"))
AddCheckbox(bags, CLASSICAPI_IGNORE_BANK, CLASSICAPI_IGNORE_BANK_TOOLTIP, CVarSetting("bankAutosortDisabled"))
AddCheckbox(bags, CLASSICAPI_HIDE_EQUIPMENT_SETS, CLASSICAPI_HIDE_EQUIPMENT_SETS_TOOLTIP, CVarSetting("dontShowEquipmentSetsOnItems"))

-- ---------------------------------------------------------------------------
-- Interface
-- ---------------------------------------------------------------------------

local interface = NewPage(CLASSICAPI_INTERFACE, main.name)

AddHeader(interface, CLASSICAPI_NAMEPLATES)
AddSlider(interface, CLASSICAPI_TARGET_NAMEPLATE_ALPHA, CLASSICAPI_TARGET_NAMEPLATE_ALPHA_TOOLTIP, NumberCVarSetting("nameplateSelectedAlpha"), 0, 1, 0.05)
AddSlider(interface, CLASSICAPI_OTHER_NAMEPLATE_ALPHA, CLASSICAPI_OTHER_NAMEPLATE_ALPHA_TOOLTIP, NumberCVarSetting("nameplateNotSelectedAlpha"), 0, 1, 0.05)

AddHeader(interface, CLASSICAPI_NUMBERS)
AddCheckbox(interface, CLASSICAPI_SEPARATE_LARGE_NUMBERS, CLASSICAPI_SEPARATE_LARGE_NUMBERS_TOOLTIP, CVarSetting("breakUpLargeNumbers"))

AddHeader(interface, CLASSICAPI_MOTION)
AddNumberBox(interface, CLASSICAPI_UI_SHAKE_STRENGTH, CLASSICAPI_UI_SHAKE_STRENGTH_TOOLTIP, NumberCVarSetting("ShakeStrengthUI"))

-- ---------------------------------------------------------------------------
-- Text To Speech
-- ---------------------------------------------------------------------------

local tts = NewPage(TEXT_TO_SPEECH, main.name)

AddDropDown(tts, TEXT_TO_SPEECH_VOICE_OPTION, TEXT_TO_SPEECH_VOICE_OPTION, NumberCVarSetting("ttsVoice"), function()
	local choices = {}
	for _, voice in ipairs(C_VoiceChat.GetTtsVoices()) do
		table.insert(choices, { value = voice.voiceID, text = voice.name })
	end
	return choices
end)
AddSlider(tts, TEXT_TO_SPEECH_ADJUST_RATE, TEXT_TO_SPEECH_ADJUST_RATE, NumberCVarSetting("ttsSpeed"), -10, 10, 1)
AddSlider(tts, TEXT_TO_SPEECH_ADJUST_VOLUME, TEXT_TO_SPEECH_ADJUST_VOLUME, NumberCVarSetting("ttsVolume"), 0, 100, 1)

-- ---------------------------------------------------------------------------
-- Advanced
-- ---------------------------------------------------------------------------

local advanced = NewPage(CLASSICAPI_ADVANCED, main.name)

AddCheckbox(advanced, CLASSICAPI_REMEMBER_PLAYERS, CLASSICAPI_REMEMBER_PLAYERS_TOOLTIP, rememberSetting)
AddNumberBox(advanced, CLASSICAPI_COMBAT_LOG_RETENTION, CLASSICAPI_COMBAT_LOG_RETENTION_TOOLTIP, NumberCVarSetting("combatLogRetentionTime"))
AddCheckbox(advanced, CLASSICAPI_SCRIPT_PROFILE, CLASSICAPI_SCRIPT_PROFILE_TOOLTIP, CVarSetting("scriptProfile"))
