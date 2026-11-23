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

local WITH_LOCKED_CHOICES = {
	{ 0, CLASSICAPI_CHOICE_OFF },
	{ 1, CLASSICAPI_CHOICE_ON },
	{ 2, CLASSICAPI_KEEP_ACTION_TARGETING_ALWAYS },
};

local MATCH_LOCKED_CHOICES = {
	{ 0, CLASSICAPI_CHOICE_OFF },
	{ 1, CLASSICAPI_PREFER_YOUR_TARGET_CHOSEN },
	{ 2, CLASSICAPI_PREFER_YOUR_TARGET_SPELLS },
};

local COMBAT_LOCK_CHOICES = {
	{ 0, CLASSICAPI_CHOICE_OFF },
	{ 1, CLASSICAPI_COMBAT_LOCK_IN_COMBAT },
	{ 2, CLASSICAPI_COMBAT_LOCK_ATTACKING },
};

local PVP_PRIORITY_CHOICES = {
	{ 0, CLASSICAPI_CHOICE_OFF },
	{ 1, CLASSICAPI_PVP_PRIORITY_PLAYERS_BOSSES },
	{ 2, CLASSICAPI_PVP_PRIORITY_ALL },
	{ 3, CLASSICAPI_PVP_PRIORITY_PLAYERS },
};

local ANY_ON_SCREEN_CHOICES = {
	{ 0, CLASSICAPI_CHOICE_OFF },
	{ 1, CLASSICAPI_TAB_ANY_IGNORE_COMBAT },
	{ 2, CLASSICAPI_TAB_ANY_OUT_OF_RANGE },
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

	AddCVarCheckbox(category, "SoftTargetIconInteract", CLASSICAPI_INTERACT_ICON, CLASSICAPI_INTERACT_ICON_TOOLTIP);
	AddCVarCheckbox(category, "SoftTargetNameplateInteract", CLASSICAPI_ALWAYS_SHOW_NAMEPLATE, CLASSICAPI_INTERACT_NAMEPLATE_TOOLTIP);

	-- Retail's "Show Target Tooltip" (Accessibility): one box for the enemy and interact tooltips.
	do
		local function GetValue()
			return C_CVar.GetCVarBool("SoftTargetTooltipEnemy") and C_CVar.GetCVarBool("SoftTargetTooltipInteract");
		end

		local function SetValue(value)
			SetCVar("SoftTargetTooltipEnemy", value and "1" or "0");
			SetCVar("SoftTargetTooltipInteract", value and "1" or "0");
		end

		local setting = Settings.RegisterProxySetting(category, "PROXY_TARGET_TOOLTIP",
			Settings.VarType.Boolean, TARGET_TOOLTIP_OPTION, false, GetValue, SetValue);
		Settings.CreateCheckbox(category, setting, OPTION_TOOLTIP_TARGET_TOOLTIP);
	end

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
	AddCVarCheckbox(category, "SoftTargetNameplateFriend", CLASSICAPI_ALWAYS_SHOW_NAMEPLATE, CLASSICAPI_FRIEND_NAMEPLATE_TOOLTIP);

	AddHeader(layout, CLASSICAPI_WHEN_YOU_HAVE_A_TARGET);
	AddDropdown(category, "SoftTargetWithLocked", CLASSICAPI_KEEP_ACTION_TARGETING, CLASSICAPI_KEEP_ACTION_TARGETING_TOOLTIP, WITH_LOCKED_CHOICES);
	AddDropdown(category, "SoftTargetMatchLocked", CLASSICAPI_PREFER_YOUR_TARGET, CLASSICAPI_PREFER_YOUR_TARGET_TOOLTIP, MATCH_LOCKED_CHOICES);
	AddDropdown(category, "SoftTargetForce", CLASSICAPI_LOOSE_TARGET_FOLLOWS, CLASSICAPI_LOOSE_TARGET_FOLLOWS_TOOLTIP, FORCE_CHOICES);

	AddHeader(layout, CLASSICAPI_TARGETS_THE_GAME_SETS);
	AddCVarCheckbox(category, "TargetAutoEnemy", CLASSICAPI_TARGET_AUTO_ENEMY, CLASSICAPI_TARGET_AUTO_ENEMY_TOOLTIP);
	AddCVarCheckbox(category, "TargetAutoFriend", CLASSICAPI_TARGET_AUTO_FRIEND, CLASSICAPI_TARGET_AUTO_FRIEND_TOOLTIP);
	AddCVarCheckbox(category, "TargetEnemyAttacker", CLASSICAPI_TARGET_ENEMY_ATTACKER, CLASSICAPI_TARGET_ENEMY_ATTACKER_TOOLTIP);
	AddCVarCheckbox(category, "TargetAutoLock", CLASSICAPI_TARGET_AUTO_LOCK, CLASSICAPI_TARGET_AUTO_LOCK_TOOLTIP);

	AddHeader(layout, CLASSICAPI_TARGET_PRIORITY);
	AddDropdown(category, "TargetPriorityCombatLock", CLASSICAPI_COMBAT_LOCK, CLASSICAPI_COMBAT_LOCK_TOOLTIP, COMBAT_LOCK_CHOICES);
	AddCVarCheckbox(category, "TargetPriorityCombatLockContextualRelaxation", CLASSICAPI_COMBAT_LOCK_RELAXATION, CLASSICAPI_COMBAT_LOCK_RELAXATION_TOOLTIP);
	AddDropdown(category, "TargetPriorityPvp", CLASSICAPI_PVP_PRIORITY, CLASSICAPI_PVP_PRIORITY_TOOLTIP, PVP_PRIORITY_CHOICES);

	AddHeader(layout, CLASSICAPI_TAB_TARGETING);
	local scoredTab, scoredTabInitializer = Settings.SetupCVarCheckbox(category, "TargetNearestUseNew", CLASSICAPI_SCORED_TAB, CLASSICAPI_SCORED_TAB_TOOLTIP);
	-- The rest of the section only applies to the scored Tab.
	local function IsScoredTab() return scoredTab:GetValue(); end
	AddDropdown(category, "TargetPriorityAllowAnyOnScreen", CLASSICAPI_TAB_ANY_ON_SCREEN, CLASSICAPI_TAB_ANY_ON_SCREEN_TOOLTIP, ANY_ON_SCREEN_CHOICES):SetParentInitializer(scoredTabInitializer, IsScoredTab);
	AddCVarCheckbox(category, "TargetPriorityContinueFromManualTarget", CLASSICAPI_TAB_CONTINUE_FROM_CLICK, CLASSICAPI_TAB_CONTINUE_FROM_CLICK_TOOLTIP):SetParentInitializer(scoredTabInitializer, IsScoredTab);
	AddNumberBox(category, "TargetPriorityAutoTargetIgnoreWindow", CLASSICAPI_TAB_IGNORE_WINDOW, CLASSICAPI_TAB_IGNORE_WINDOW_TOOLTIP):SetParentInitializer(scoredTabInitializer, IsScoredTab);
	AddCVarCheckbox(category, "TargetPriorityPvpLock", CLASSICAPI_TAB_PVP_LOCK, CLASSICAPI_TAB_PVP_LOCK_TOOLTIP):SetParentInitializer(scoredTabInitializer, IsScoredTab);

	AddHeader(layout, CLASSICAPI_TARGET_TOOLTIPS);
	AddCVarCheckbox(category, "SoftTargetTooltipInteract", CLASSICAPI_TOOLTIP_INTERACT, CLASSICAPI_TOOLTIP_INTERACT_TOOLTIP);
	AddCVarCheckbox(category, "SoftTargetTooltipLocked", CLASSICAPI_TOOLTIP_TARGET, CLASSICAPI_TOOLTIP_TARGET_TOOLTIP);
	AddCVarCheckbox(category, "SoftTargetTooltipEnemy", CLASSICAPI_TOOLTIP_ENEMY, CLASSICAPI_TOOLTIP_ENEMY_TOOLTIP);
	AddCVarCheckbox(category, "SoftTargetTooltipFriend", CLASSICAPI_TOOLTIP_FRIEND, CLASSICAPI_TOOLTIP_FRIEND_TOOLTIP);
	AddNumberBox(category, "SoftTargetTooltipDurationMs", CLASSICAPI_TOOLTIP_DURATION, CLASSICAPI_TOOLTIP_DURATION_TOOLTIP);
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

	-- Retail's nameplate rows (Blizzard_SettingsDefinitions_Frame\Nameplates.lua),
	-- less "Enemy Units", which 1.12.1 keeps in ShowNameplates() with no cvar.
	-- Retail shows only the Minions rows; the kinds under them get rows here
	-- too. The Settings list indents one level, so a kind sits at the Minions
	-- row's indent and follows the Minions row.
	AddHeader(layout, CLASSICAPI_NAMEPLATES);
	do
		local enemyMinions, enemyMinionsInitializer = Settings.SetupCVarCheckbox(category, "nameplateShowEnemyMinions", CLASSICAPI_ENEMY_MINIONS, OPTION_TOOLTIP_UNIT_NAMEPLATES_SHOW_ENEMY_MINIONS);
		local function AreEnemyMinionsShown() return enemyMinions:GetValue(); end
		for _, row in ipairs({
			{ "nameplateShowEnemyPets", PETS },
			{ "nameplateShowEnemyGuardians", UNIT_NAMEPLATES_SHOW_ENEMY_GUARDIANS },
			{ "nameplateShowEnemyTotems", UNIT_NAMEPLATES_SHOW_ENEMY_TOTEMS },
		}) do
			AddCVarCheckbox(category, row[1], row[2]):SetParentInitializer(enemyMinionsInitializer, AreEnemyMinionsShown);
		end

		local friendlyPlayers, friendlyPlayersInitializer = Settings.SetupCVarCheckbox(category, "nameplateShowFriendlyPlayers", UNIT_NAMEPLATES_SHOW_FRIENDS, OPTION_TOOLTIP_UNIT_NAMEPLATES_SHOW_FRIENDS);
		local friendlyMinions, friendlyMinionsInitializer = Settings.SetupCVarCheckbox(category, "nameplateShowFriendlyPlayerMinions", UNIT_NAMEPLATES_SHOW_FRIENDLY_MINIONS, OPTION_TOOLTIP_UNIT_NAMEPLATES_SHOW_FRIENDLY_MINIONS);
		friendlyMinionsInitializer:SetParentInitializer(friendlyPlayersInitializer, function() return friendlyPlayers:GetValue(); end);
		local function AreFriendlyMinionsShown() return friendlyPlayers:GetValue() and friendlyMinions:GetValue(); end
		for _, row in ipairs({
			{ "nameplateShowFriendlyPlayerPets", PETS },
			{ "nameplateShowFriendlyPlayerGuardians", UNIT_NAMEPLATES_SHOW_FRIENDLY_GUARDIANS },
			{ "nameplateShowFriendlyPlayerTotems", UNIT_NAMEPLATES_SHOW_FRIENDLY_TOTEMS },
		}) do
			AddCVarCheckbox(category, row[1], row[2]):SetParentInitializer(friendlyMinionsInitializer, AreFriendlyMinionsShown);
		end
		AddCVarCheckbox(category, "nameplateUseClassColorForFriendlyPlayerUnitNames", UNIT_NAMEPLATES_FRIENDLY_PLAYER_NAME_USE_CLASS_COLOR, OPTION_TOOLTIP_UNIT_NAMEPLATES_FRIENDLY_PLAYER_NAME_USE_CLASS_COLOR):SetParentInitializer(friendlyPlayersInitializer, function() return friendlyPlayers:GetValue(); end);

		AddCVarCheckbox(category, "nameplateShowFriendlyNpcs", UNIT_NAMEPLATES_SHOW_FRIENDLY_NPCS, UNIT_NAMEPLATES_SHOW_FRIENDLY_NPCS_TOOLTIP);
	end
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

-- 4.3.4's chat settings "Show Class Color" column, as rows of checkboxes. A
-- row sets every chat type of its group (ToggleChatColorNamesByClassGroup)
-- and shows checked when any of them is on (IsClassColoringMessageType).
-- The rows are 4.3.4's, less the ones whose 1.12.1 line has no sender link
-- to colour (emotes) and the chat types 1.12.1 does not have.
local CLASS_COLOR_CHAT_ROWS = {
	{ "SAY", CHAT_MSG_SAY, { "SAY" } },
	{ "YELL", CHAT_MSG_YELL, { "YELL" } },
	{ "GUILD", CHAT_MSG_GUILD, { "GUILD" } },
	{ "OFFICER", CHAT_MSG_OFFICER, { "OFFICER" } },
	{ "WHISPER", WHISPER, { "WHISPER", "WHISPER_INFORM" } },
	{ "PARTY", CHAT_MSG_PARTY, { "PARTY" } },
	{ "RAID", CHAT_MSG_RAID, { "RAID", "RAID_LEADER" } },
	{ "RAID_WARNING", CHAT_MSG_RAID_WARNING, { "RAID_WARNING" } },
	{ "BATTLEGROUND", CHAT_MSG_BATTLEGROUND, { "BATTLEGROUND", "BATTLEGROUND_LEADER" } },
};

local function RegisterClassColorSetting(category, key, name, chatTypes)
	local function GetValue()
		for _, chatType in ipairs(chatTypes) do
			local info = ChatTypeInfo[chatType];
			if ( info and info.colorNameByClass ) then
				return true;
			end
		end
		return false;
	end
	local function SetValue(value)
		for _, chatType in ipairs(chatTypes) do
			SetChatColorNameByClass(chatType, value);
		end
	end
	local setting = Settings.RegisterProxySetting(category, "PROXY_CLASSICAPI_CLASS_COLOR_"..key, Settings.VarType.Boolean, name, false, GetValue, SetValue);
	return Settings.CreateCheckbox(category, setting);
end

local function RegisterSocial()
	local category, layout = Settings.RegisterVerticalLayoutCategory(SOCIAL_LABEL);
	Settings.RegisterCategory(category);

	-- C_AutoComplete reads these; the addon that shows the names does the rest.
	AddHeader(layout, CLASSICAPI_NAME_SUGGESTIONS);
	AddCVarCheckbox(category, "autoCompleteResortNamesOnRecency", CLASSICAPI_RECENT_WHISPERS_FIRST, CLASSICAPI_RECENT_WHISPERS_FIRST_TOOLTIP);
	AddCVarCheckbox(category, "autoCompleteWhenEditingFromCenter", CLASSICAPI_MATCH_INSIDE_NAMES, CLASSICAPI_MATCH_INSIDE_NAMES_TOOLTIP);
	AddCVarCheckbox(category, "autoCompleteUseContext", CLASSICAPI_MATCH_THE_COMMAND, CLASSICAPI_MATCH_THE_COMMAND_TOOLTIP);

	AddHeader(layout, SHOW_CLASS_COLOR);
	for _, row in ipairs(CLASS_COLOR_CHAT_ROWS) do
		RegisterClassColorSetting(category, row[1], row[2], row[3]);
	end
	-- The joined channels, as 4.3.4 lists them: "number. name", one row each.
	local index = 1;
	while ( ChatTypeInfo["CHANNEL"..index] ) do
		local channel = index;
		local chatType = "CHANNEL"..channel;
		local initializer = RegisterClassColorSetting(category, chatType, chatType, { chatType });
		initializer.GetName = function()
			local _, name = GetChannelName(channel);
			return channel..". "..(name or "");
		end
		initializer:AddShownPredicate(function()
			return GetChannelName(channel) > 0;
		end);
		index = index + 1;
	end
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
-- Graphics
-- ---------------------------------------------------------------------------

-- Retail's frame-rate rows (Blizzard_SettingsDefinitions_Shared\Graphics.lua):
-- a checkbox for each use cvar with a slider for its cap, 8 to 300 (retail
-- stops at 200, too low for high-refresh monitors). 1.12.1's
-- other graphics settings stay in its own Video Options.
local function RegisterGraphics()
	local category, layout = Settings.RegisterVerticalLayoutCategory(GRAPHICS_LABEL);
	Settings.RegisterCategory(category);

	local function FormatFPS(value)
		return SETTINGS_FMT_FPS:format(value);
	end

	local function AddFPSRow(useCVar, fpsCVar, checkVariable, fpsVariable, checkName, name, tooltip)
		local getValue, setValue, getDefaultValue = Settings.CreateCVarAccessorClosures(useCVar, Settings.VarType.Boolean);
		local fpsSetting = Settings.RegisterProxySetting(category, checkVariable,
			Settings.VarType.Boolean, checkName, getDefaultValue(), getValue, setValue);
		fpsSetting:SetCommitFlags(Settings.CommitFlag.KioskProtected);

		getValue, setValue, getDefaultValue = Settings.CreateCVarAccessorClosures(fpsCVar, Settings.VarType.Number);
		local fpsSliderSetting = Settings.RegisterProxySetting(category, fpsVariable,
			Settings.VarType.Number, name, getDefaultValue(), getValue, setValue);
		fpsSliderSetting:SetCommitFlags(Settings.CommitFlag.KioskProtected);

		local minValue, maxValue, step = 8, 300, 1;
		local options = Settings.CreateSliderOptions(minValue, maxValue, step);
		options:SetLabelFormatter(MinimalSliderWithSteppersMixin.Label.Right, FormatFPS);

		local initializer = CreateSettingsCheckboxSliderInitializer(
			fpsSetting, name, tooltip,
			fpsSliderSetting, options, name, tooltip);
		layout:AddInitializer(initializer);
	end

	AddFPSRow("useMaxFPS", "maxFPS", "PROXY_FOREGROUND_FPS_ENABLED", "PROXY_FOREGROUND_FPS", MAXFPS_CHECK, MAXFPS, OPTION_MAXFPS_CHECK);
	AddFPSRow("useMaxFPSBk", "maxFPSBk", "PROXY_BACKGROUND_FPS_ENABLED", "PROXY_BACKGROUND_FPS", MAXFPSBK_CHECK, MAXFPSBK, OPTION_MAXFPSBK_CHECK);
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
	RegisterGraphics();
	RegisterNetwork();
	RegisterAdvanced();
end);
