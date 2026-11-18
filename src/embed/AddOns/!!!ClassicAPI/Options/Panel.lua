-- ClassicAPI's own pages in the Settings window, one for each feature, at the
-- top of the window's first tab. Every cvar that ClassicAPI registers has a
-- control here, and so do the player cache and ClassicAPI's key bindings.
--
-- The pages are built the way retail builds its own game settings: each one
-- is a vertical layout of Settings controls, so the window's search finds
-- them, registered as a game category. They have no group, so the list shows
-- no group header above them, and their group comes first, so the window
-- opens on the first of them.
-- A cvar that the window shows as it is gets a cvar setting. A cvar whose
-- value the control reads another way (the soft-target enable cvars, a choice
-- of numbers) gets a proxy setting, as retail does for the same cvars. A
-- setting that takes any number gets ClassicAPI's number box
-- (Options\Controls.lua).
--
-- The settings write when the player uses a control, as retail's cvar settings
-- do, and Defaults puts back each cvar's default. A key binding changes at
-- once and is saved when the window closes.
--
-- The pages are registered once the client has loaded its variables and
-- bindings (SettingsRegistrar), as retail registers its own.
--
-- Every string is a global: the client's own, or one from locales\enUS.lua -
-- retail's GlobalStrings key where retail has the string
-- (ENABLE_INTERACT_TEXT and the like), a CLASSICAPI_ one where it does not.

-- The soft-target enable cvars: 0 off, 1 gamepad, 2 keyboard and mouse,
-- 3 always. This client has no gamepad, so 2 and 3 are on.
local function RegisterSoftTargetSetting(category, variable, cvar, name)
	local function GetValue()
		return (tonumber(GetCVar(cvar)) or 0) >= 2;
	end
	local function SetValue(value)
		SetCVar(cvar, value and "3" or "0");
	end
	local defaultValue = (tonumber(GetCVarDefault(cvar)) or 0) >= 2;
	return Settings.RegisterProxySetting(category, variable, Settings.VarType.Boolean, name, defaultValue, GetValue, SetValue);
end

local function AddHeader(layout, text)
	layout:AddInitializer(CreateSettingsListSectionHeaderInitializer(text));
end

local function AddCVarCheckbox(category, cvar, name, tooltip)
	local _, initializer = Settings.SetupCVarCheckbox(category, cvar, name, tooltip);
	return initializer;
end

local function AddNumberBox(category, cvar, name, tooltip)
	local setting = Settings.RegisterCVarSetting(category, cvar, Settings.VarType.Number, name);
	local initializer = Settings.CreateControlInitializer("ClassicAPISettingsNumberBoxControlTemplate", setting, nil, tooltip);
	SettingsPanel:GetLayout(category):AddInitializer(initializer);
	return initializer;
end

-- `choices` is a list of { value, text }, or a function that returns one.
local function AddDropdown(category, cvar, name, tooltip, choices)
	local function GetOptions()
		local container = Settings.CreateControlTextContainer();
		local list = type(choices) == "function" and choices() or choices;
		for _, choice in ipairs(list) do
			container:Add(choice[1], choice[2]);
		end
		return container:GetData();
	end
	local _, initializer = Settings.SetupCVarDropdown(category, cvar, Settings.VarType.Number, GetOptions, name, tooltip);
	return initializer;
end

local function FormatHundredths(value)
	return string.format("%.2f", value);
end

local function AddSlider(category, cvar, name, tooltip, minValue, maxValue, step, formatter)
	local options = Settings.CreateSliderOptions(minValue, maxValue, step);
	options:SetLabelFormatter(MinimalSliderWithSteppersMixin.Label.Right, formatter);
	local _, initializer = Settings.SetupCVarSlider(category, cvar, options, name, tooltip);
	return initializer;
end

local function AddKeybinding(layout, action)
	local bindingIndex = C_KeyBindings.GetBindingIndex(action);
	local initializer = CreateKeybindingEntryInitializer(bindingIndex, true);
	initializer:AddSearchTags(GetBindingName(action));
	layout:AddInitializer(initializer);
	return initializer;
end

local ARC_CHOICES = {
	{ 0, CLASSICAPI_DIRECTION_FRONT },
	{ 1, CLASSICAPI_DIRECTION_CONE },
	{ 2, CLASSICAPI_DIRECTION_ANY },
};

local FORCE_CHOICES = {
	{ 0, CLASSICAPI_LOOSE_TARGET_FOLLOWS_NONE },
	{ 1, CLASSICAPI_LOOSE_TARGET_FOLLOWS_ENEMY },
	{ 2, CLASSICAPI_LOOSE_TARGET_FOLLOWS_FRIEND },
};

-- ---------------------------------------------------------------------------
-- Interact Key
-- ---------------------------------------------------------------------------

local function RegisterInteractKey()
	local category, layout = Settings.RegisterVerticalLayoutCategory(CLASSICAPI_INTERACT_KEY);
	Settings.RegisterCategory(category);

	Settings.CreateCheckbox(category, RegisterSoftTargetSetting(category, "PROXY_ENABLE_INTERACT", "SoftTargetInteract", ENABLE_INTERACT_TEXT), OPTION_TOOLTIP_ENABLE_INTERACT);
	AddKeybinding(layout, "INTERACTTARGET");
	AddKeybinding(layout, "INTERACTMOUSEOVER");
	AddDropdown(category, "SoftTargetInteractArc", CLASSICAPI_INTERACT_DIRECTION, CLASSICAPI_INTERACT_DIRECTION_TOOLTIP, ARC_CHOICES);
	AddNumberBox(category, "SoftTargetInteractRange", CLASSICAPI_INTERACT_RANGE, CLASSICAPI_RANGE_TOOLTIP);
	AddCVarCheckbox(category, "SoftTargetInteractRangeIsHard", CLASSICAPI_STRICT_INTERACT_RANGE, CLASSICAPI_STRICT_INTERACT_RANGE_TOOLTIP);

	local interactIcon, interactIconInitializer = Settings.SetupCVarCheckbox(category, "SoftTargetIconInteract", CLASSICAPI_INTERACT_ICON, CLASSICAPI_INTERACT_ICON_TOOLTIP);
	-- The object icon is drawn only while the interact icon is on.
	local gameObjectIcon = AddCVarCheckbox(category, "SoftTargetIconGameObject", CLASSICAPI_GAME_OBJECT_ICON, CLASSICAPI_GAME_OBJECT_ICON_TOOLTIP);
	gameObjectIcon:SetParentInitializer(interactIconInitializer, function() return interactIcon:GetValue(); end);

	AddHeader(layout, CLASSICAPI_ALL_SOFT_TARGET_ICONS);
	AddCVarCheckbox(category, "SoftTargetLowPriorityIcons", CLASSICAPI_LOW_PRIORITY_ICONS, CLASSICAPI_LOW_PRIORITY_ICONS_TOOLTIP);
	AddNumberBox(category, "SoftTargetNameplateSize", CLASSICAPI_ICON_SIZE, CLASSICAPI_ICON_SIZE_TOOLTIP);
end

-- ---------------------------------------------------------------------------
-- Action Targeting
-- ---------------------------------------------------------------------------

local function RegisterActionTargeting()
	local category, layout = Settings.RegisterVerticalLayoutCategory(CLASSICAPI_ACTION_TARGETING);
	Settings.RegisterCategory(category);

	AddHeader(layout, CLASSICAPI_ENEMIES);
	Settings.CreateCheckbox(category, RegisterSoftTargetSetting(category, "PROXY_ACTION_TARGETING", "SoftTargetEnemy", ACTION_TARGETING_OPTION), OPTION_TOOLTIP_ACTION_TARGETING);
	AddDropdown(category, "SoftTargetEnemyArc", CLASSICAPI_ENEMY_DIRECTION, CLASSICAPI_UNIT_DIRECTION_TOOLTIP, ARC_CHOICES);
	AddNumberBox(category, "SoftTargetEnemyRange", CLASSICAPI_ENEMY_RANGE, CLASSICAPI_RANGE_TOOLTIP);
	AddCVarCheckbox(category, "SoftTargetIconEnemy", CLASSICAPI_ENEMY_ICON, CLASSICAPI_ENEMY_ICON_TOOLTIP);

	AddHeader(layout, CLASSICAPI_FRIENDS);
	Settings.CreateCheckbox(category, RegisterSoftTargetSetting(category, "PROXY_FRIEND_TARGETING", "SoftTargetFriend", CLASSICAPI_FRIEND_TARGETING), CLASSICAPI_FRIEND_TARGETING_TOOLTIP);
	AddDropdown(category, "SoftTargetFriendArc", CLASSICAPI_FRIEND_DIRECTION, CLASSICAPI_UNIT_DIRECTION_TOOLTIP, ARC_CHOICES);
	AddNumberBox(category, "SoftTargetFriendRange", CLASSICAPI_FRIEND_RANGE, CLASSICAPI_RANGE_TOOLTIP);
	AddCVarCheckbox(category, "SoftTargetIconFriend", CLASSICAPI_FRIEND_ICON, CLASSICAPI_FRIEND_ICON_TOOLTIP);

	AddHeader(layout, CLASSICAPI_WHEN_YOU_HAVE_A_TARGET);
	AddCVarCheckbox(category, "SoftTargetWithLocked", CLASSICAPI_KEEP_ACTION_TARGETING, CLASSICAPI_KEEP_ACTION_TARGETING_TOOLTIP);
	AddCVarCheckbox(category, "SoftTargetMatchLocked", CLASSICAPI_PREFER_YOUR_TARGET, CLASSICAPI_PREFER_YOUR_TARGET_TOOLTIP);
	AddDropdown(category, "SoftTargetForce", CLASSICAPI_LOOSE_TARGET_FOLLOWS, CLASSICAPI_LOOSE_TARGET_FOLLOWS_TOOLTIP, FORCE_CHOICES);
end

-- ---------------------------------------------------------------------------
-- Combat
-- ---------------------------------------------------------------------------

local function RegisterCombat()
	local category, layout = Settings.RegisterVerticalLayoutCategory(COMBAT_LABEL);
	Settings.RegisterCategory(category);

	AddHeader(layout, CLASSICAPI_ACTION_BARS);
	AddCVarCheckbox(category, "ActionButtonUseKeyDown", ACTION_BUTTON_USE_KEY_DOWN, OPTION_TOOLTIP_ACTION_BUTTON_USE_KEY_DOWN);
	AddCVarCheckbox(category, "countdownForCooldowns", COUNTDOWN_FOR_COOLDOWNS_TEXT, OPTION_TOOLTIP_COUNTDOWN_FOR_COOLDOWNS);

	AddHeader(layout, CLASSICAPI_CASTING_AND_ATTACKING);
	AddCVarCheckbox(category, "autoStand", CLASSICAPI_AUTO_STAND, CLASSICAPI_AUTO_STAND_TOOLTIP);
	AddCVarCheckbox(category, "autoDismount", CLASSICAPI_AUTO_DISMOUNT, CLASSICAPI_AUTO_DISMOUNT_TOOLTIP);
	AddCVarCheckbox(category, "autoUnshift", CLASSICAPI_AUTO_UNSHIFT, CLASSICAPI_AUTO_UNSHIFT_TOOLTIP);
	AddCVarCheckbox(category, "stopAutoAttackOnTargetChange", STOP_AUTO_ATTACK, OPTION_TOOLTIP_STOP_AUTO_ATTACK);
end

-- ---------------------------------------------------------------------------
-- Bags and Items
-- ---------------------------------------------------------------------------

local function RegisterBags()
	local category, layout = Settings.RegisterVerticalLayoutCategory(CLASSICAPI_BAGS_AND_ITEMS);
	Settings.RegisterCategory(category);

	AddCVarCheckbox(category, "sortBagsRightToLeft", CLASSICAPI_SORT_BAGS_RIGHT_TO_LEFT, CLASSICAPI_SORT_BAGS_RIGHT_TO_LEFT_TOOLTIP);
	AddCVarCheckbox(category, "backpackAutosortDisabled", CLASSICAPI_IGNORE_BACKPACK, CLASSICAPI_IGNORE_BACKPACK_TOOLTIP);
	AddCVarCheckbox(category, "bankAutosortDisabled", CLASSICAPI_IGNORE_BANK, CLASSICAPI_IGNORE_BANK_TOOLTIP);
	AddCVarCheckbox(category, "dontShowEquipmentSetsOnItems", CLASSICAPI_HIDE_EQUIPMENT_SETS, CLASSICAPI_HIDE_EQUIPMENT_SETS_TOOLTIP);

	AddHeader(layout, LOOT);
	AddKeybinding(layout, "AOELOOT");
end

-- ---------------------------------------------------------------------------
-- Interface
-- ---------------------------------------------------------------------------

local function RegisterInterface()
	local category, layout = Settings.RegisterVerticalLayoutCategory(CLASSICAPI_INTERFACE);
	Settings.RegisterCategory(category);

	-- The label ends with one of the cursors the setting shows, all the row has
	-- room for, and the tooltip opens with several. The DLL serves each image
	-- at its Interface\Cursor path, and the markup stays out of the translated
	-- strings.
	local function CursorIcon(name)
		return "|TInterface\\Cursor\\" .. name .. ":16|t";
	end
	local expandedCursorIcons = "";
	for _, name in ipairs({ "RepairNPC", "Innkeeper", "StableMaster", "Quest", "QuestTurnIn", "Fishing" }) do
		expandedCursorIcons = expandedCursorIcons .. CursorIcon(name) .. " ";
	end

	AddHeader(layout, CLASSICAPI_CURSOR);
	local cursors = AddCVarCheckbox(category, "expandedCursors", CLASSICAPI_EXPANDED_CURSORS .. " " .. CursorIcon("RepairNPC"), expandedCursorIcons .. "\n" .. CLASSICAPI_EXPANDED_CURSORS_TOOLTIP);
	cursors:AddSearchTags(CLASSICAPI_EXPANDED_CURSORS);

	AddHeader(layout, CLASSICAPI_NAMEPLATES);
	AddSlider(category, "nameplateSelectedAlpha", CLASSICAPI_TARGET_NAMEPLATE_ALPHA, CLASSICAPI_TARGET_NAMEPLATE_ALPHA_TOOLTIP, 0, 1, 0.05, FormatHundredths);
	AddSlider(category, "nameplateNotSelectedAlpha", CLASSICAPI_OTHER_NAMEPLATE_ALPHA, CLASSICAPI_OTHER_NAMEPLATE_ALPHA_TOOLTIP, 0, 1, 0.05, FormatHundredths);

	AddHeader(layout, CLASSICAPI_NUMBERS);
	AddCVarCheckbox(category, "breakUpLargeNumbers", CLASSICAPI_SEPARATE_LARGE_NUMBERS, CLASSICAPI_SEPARATE_LARGE_NUMBERS_TOOLTIP);

	AddHeader(layout, CLASSICAPI_MOTION);
	AddNumberBox(category, "ShakeStrengthUI", CLASSICAPI_UI_SHAKE_STRENGTH, CLASSICAPI_UI_SHAKE_STRENGTH_TOOLTIP);
end

-- ---------------------------------------------------------------------------
-- Social
-- ---------------------------------------------------------------------------

local function RegisterSocial()
	local category, layout = Settings.RegisterVerticalLayoutCategory(SOCIAL_LABEL);
	Settings.RegisterCategory(category);

	-- C_AutoComplete reads these; the addon that shows the names does the rest.
	AddHeader(layout, CLASSICAPI_NAME_SUGGESTIONS);
	AddCVarCheckbox(category, "autoCompleteResortNamesOnRecency", CLASSICAPI_RECENT_WHISPERS_FIRST, CLASSICAPI_RECENT_WHISPERS_FIRST_TOOLTIP);
	AddCVarCheckbox(category, "autoCompleteWhenEditingFromCenter", CLASSICAPI_MATCH_INSIDE_NAMES, CLASSICAPI_MATCH_INSIDE_NAMES_TOOLTIP);
	AddCVarCheckbox(category, "autoCompleteUseContext", CLASSICAPI_MATCH_THE_COMMAND, CLASSICAPI_MATCH_THE_COMMAND_TOOLTIP);
end

-- ---------------------------------------------------------------------------
-- Text To Speech
-- ---------------------------------------------------------------------------

local function RegisterTextToSpeech()
	local category = Settings.RegisterVerticalLayoutCategory(TEXT_TO_SPEECH);
	Settings.RegisterCategory(category);

	AddDropdown(category, "ttsVoice", TEXT_TO_SPEECH_VOICE_OPTION, TEXT_TO_SPEECH_VOICE_OPTION, function()
		local choices = {};
		for _, voice in ipairs(C_VoiceChat.GetTtsVoices()) do
			table.insert(choices, { voice.voiceID, voice.name });
		end
		return choices;
	end);
	AddSlider(category, "ttsSpeed", TEXT_TO_SPEECH_ADJUST_RATE, TEXT_TO_SPEECH_ADJUST_RATE, -10, 10, 1);
	AddSlider(category, "ttsVolume", TEXT_TO_SPEECH_ADJUST_VOLUME, TEXT_TO_SPEECH_ADJUST_VOLUME, 0, 100, 1);
end

-- ---------------------------------------------------------------------------
-- Network
-- ---------------------------------------------------------------------------

-- Retail's own page (Blizzard_SettingsDefinitions_Shared\Network.lua), with the
-- one control 1.12 can back, and the movement file under it.
local function RegisterNetwork()
	local category = Settings.RegisterVerticalLayoutCategory(NETWORK_LABEL);
	Settings.RegisterCategory(category);

	local advanced, advancedInitializer = Settings.SetupCVarCheckbox(category, "advancedCombatLogging", ADVANCED_COMBAT_LOGGING, OPTION_TOOLTIP_ADVANCED_COMBAT_LOGGING);
	-- The movement file is written only while advanced logging is on.
	local movement = AddCVarCheckbox(category, "advancedCombatLoggingMovement", CLASSICAPI_LOG_MOVEMENT, CLASSICAPI_LOG_MOVEMENT_TOOLTIP);
	movement:SetParentInitializer(advancedInitializer, function() return advanced:GetValue(); end);
end

-- ---------------------------------------------------------------------------
-- Advanced
-- ---------------------------------------------------------------------------

local function RegisterAdvanced()
	local category, layout = Settings.RegisterVerticalLayoutCategory(CLASSICAPI_ADVANCED);
	Settings.RegisterCategory(category);
	-- Retail's AdvancedOptions.lua names its page this way; the damage meter's
	-- Settings button opens it.
	Settings.ADVANCED_OPTIONS_CATEGORY_ID = category:GetID();

	local function GetRemember()
		return C_PlayerCache.IsEnabled() and true or false;
	end
	local function SetRemember(value)
		C_PlayerCache.SetEnabled(value);
	end
	local remember = Settings.RegisterProxySetting(category, "PROXY_CLASSICAPI_REMEMBER_PLAYERS", Settings.VarType.Boolean, CLASSICAPI_REMEMBER_PLAYERS, false, GetRemember, SetRemember);
	Settings.CreateCheckbox(category, remember, CLASSICAPI_REMEMBER_PLAYERS_TOOLTIP);

	AddNumberBox(category, "combatLogRetentionTime", CLASSICAPI_COMBAT_LOG_RETENTION, CLASSICAPI_COMBAT_LOG_RETENTION_TOOLTIP);
	AddCVarCheckbox(category, "scriptProfile", CLASSICAPI_SCRIPT_PROFILE, CLASSICAPI_SCRIPT_PROFILE_TOOLTIP);

	-- Damage Meter, as retail's AdvancedOptions.lua sets it up. Here the
	-- auto-reset checkbox is a child of the enable checkbox.
	local sectionNewTagID = "DAMAGE_METER_LABEL";
	layout:AddInitializer(CreateSettingsListSectionHeaderInitializer(DAMAGE_METER_LABEL, nil, sectionNewTagID));

	local function TooltipFn()
		local isAvailable, failureReason = C_DamageMeter.IsDamageMeterAvailable();
		if isAvailable then
			return ENABLE_DAMAGE_METER_TOOLTIP;
		else
			return format("%s|n|n%s", ENABLE_DAMAGE_METER_TOOLTIP, failureReason);
		end
	end

	local damageMeter, damageMeterInitializer = Settings.SetupCVarCheckbox(category, "damageMeterEnabled", ENABLE_DAMAGE_METER, TooltipFn);

	local function AutoResetTooltipFn()
		local isAvailable, failureReason = C_DamageMeter.IsDamageMeterAvailable();
		if isAvailable then
			return AUTO_RESET_DAMAGE_METER_TOOLTIP;
		else
			return format("%s|n|n%s", AUTO_RESET_DAMAGE_METER_TOOLTIP, failureReason);
		end
	end

	local autoReset = AddCVarCheckbox(category, "damageMeterResetOnNewInstance", AUTO_RESET_DAMAGE_METER, AutoResetTooltipFn);
	autoReset:SetParentInitializer(damageMeterInitializer, function() return damageMeter:GetValue(); end);

	-- ClassicAPI: the meter's Visibility setting, which retail changes in Edit
	-- Mode. The meter windows' settings dropdowns change it too, but a window
	-- hidden by In Group, In Combat or Hidden cannot show its dropdown, so this
	-- page keeps a way back, and so it alone offers retail's Hidden (the
	-- choice for a replacement meter that reads C_DamageMeter). The options
	-- are retail's, in its order (EditModeSettingDisplayInfo.lua). The value
	-- lives in ClassicAPI_DamageMeter (load on
	-- demand, DamageMeterSystem.lua); until that addon loads it reads as the
	-- preset's Always, and writing it loads the addon.
	local DAMAGE_METER_ADDON = "ClassicAPI_DamageMeter";
	local function GetVisibility()
		if not IsAddOnLoaded(DAMAGE_METER_ADDON) then
			return Enum.DamageMeterVisibility.Always;
		end
		return DamageMeter:GetSettingValue(DamageMeterSystemSetting.Visibility);
	end
	local function SetVisibility(value)
		if not IsAddOnLoaded(DAMAGE_METER_ADDON) then
			UIParentLoadAddOn(DAMAGE_METER_ADDON);
		end
		DamageMeter:OnSystemSettingChange(DamageMeterSystemSetting.Visibility, value);
	end
	local function GetVisibilityOptions()
		local container = Settings.CreateControlTextContainer();
		container:Add(Enum.DamageMeterVisibility.Always, HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY_ALWAYS);
		container:Add(Enum.DamageMeterVisibility.InCombat, HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY_IN_COMBAT);
		container:Add(Enum.DamageMeterVisibility.Hidden, HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY_HIDDEN);
		container:Add(Enum.DamageMeterVisibility.InGroup, HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY_IN_GROUP);
		return container:GetData();
	end
	local visibility = Settings.RegisterProxySetting(category, "PROXY_CLASSICAPI_DAMAGE_METER_VISIBILITY", Settings.VarType.Number, HUD_EDIT_MODE_SETTING_DAMAGE_METER_VISIBILITY, Enum.DamageMeterVisibility.Always, GetVisibility, SetVisibility);
	local visibilityInitializer = Settings.CreateDropdown(category, visibility, GetVisibilityOptions, CLASSICAPI_DAMAGE_METER_VISIBILITY_TOOLTIP);
	visibilityInitializer:SetParentInitializer(damageMeterInitializer, function() return damageMeter:GetValue(); end);
end

SettingsRegistrar:AddRegistrant(function()
	Settings.GetOrCreateSettingsGroup(nil, 1);

	RegisterInteractKey();
	RegisterActionTargeting();
	RegisterCombat();
	RegisterBags();
	RegisterInterface();
	RegisterSocial();
	RegisterTextToSpeech();
	RegisterNetwork();
	RegisterAdvanced();
end);
