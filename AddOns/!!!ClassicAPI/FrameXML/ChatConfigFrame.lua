-- Backport of later clients' chat settings window, ChatConfigFrame, on
-- vanilla's chat settings.
--
-- Later clients open it from a chat tab's menu (Settings) to set what one chat
-- window shows: its message types and channels, their colors, and whether
-- sender names show their class color. Addons reach for it by name:
-- LibBetterBlizzOptions-1.0 makes it movable while loading, and Zygor Guides
-- Viewer 2.0 ships that library.
--
-- The frames, templates, tables and functions keep 3.3.5's names
-- (CHAT_CONFIG_CHAT_LEFT, ChatConfig_CreateCheckboxes, ToggleChatMessageGroup,
-- IsListeningForMessageType, ...). What they hold is vanilla's:
--   - Rows are vanilla's message groups, read from the lists the client's own
--     chat tab menu shows (ChannelMenuChatTypeGroups, OtherMenuChatTypeGroups,
--     CombatLogMenuChatTypeGroups, SpellLogMenuChatTypeGroups,
--     SpellLogOtherMenuChatTypeGroups, PeriodicLogMenuChatTypeGroups), so a
--     client's own groups, such as Hardcore, are there too.
--   - Vanilla's groups are coarser than 3.x's: Say also holds emotes, Party
--     holds raid, raid warning and battleground chat, Guild holds officer chat,
--     and a window takes or drops a group whole. A group of several chat types
--     lists them under its check box, each with its own color and class color,
--     the way vanilla's menu lists them in a submenu. (3.3.5 colors a row's
--     whole group from one swatch, which here would give raid warnings the
--     color of party chat.)
--   - Combat: 3.x sets the Blizzard_CombatLog filters there. Vanilla's combat
--     log is message groups on a chat window, so the category holds the four
--     combat lists of vanilla's chat tab menu, as tabs, for any window.
--   - Other: 3.3.5's Combat, Creature and System boxes. Its PvP box is gone:
--     vanilla's battleground messages are part of the System group.
--   - Chat Defaults and Combat Log Defaults give the engine's own default
--     message groups (the engine has no ResetChatWindows to call).
--   - Show Class Color is SetChatColorNameByClass (Util/ChatColorNameByClass.lua).
--   - The window edits the chat window whose menu opened it
--     (ChatConfigFrame.chatFrame): vanilla's FCF_GetCurrentChatFrame reads
--     whichever dropdown menu opened last, so it can't be asked again later.
-- Each chat tab's menu gets 3.3.5's Settings entry, at the end of its Filters
-- section; vanilla's submenus stay.

UIPanelWindows["ChatConfigFrame"] = { area = "center", pushable = 0, whileDead = 1 };

CHATCONFIG_CHANNELS_MAXWIDTH = 145;
CHAT_CONFIG_COMBAT_TAB_NAME = "CombatConfigTab";

local ROW_PADDING = 8;
local COLUMN_SPACING = 4;
-- How far the background box moves down while the combat tabs sit on it.
local COMBAT_TABS_HEIGHT = 40;

-- The chat window being edited.
local function CurrentChatFrame()
	return ChatConfigFrame.chatFrame or SELECTED_CHAT_FRAME or DEFAULT_CHAT_FRAME;
end

-- ---------------------------------------------------------------------------
-- Rows from vanilla's message groups
-- ---------------------------------------------------------------------------

-- The chat types of a message group that have a color, in the group's order.
-- Vanilla's menu lists the same ones (GUILD_MOTD, TIME_PLAYED_MSG and the like
-- are events of a group, not chat types).
local function GroupChatTypes(group)
	local chatTypes = {};
	for _, event in ipairs(ChatTypeGroup[group]) do
		local chatType = FCF_StripChatMsg(event);
		if ( ChatTypeInfo[chatType] ) then
			tinsert(chatTypes, chatType);
		end
	end
	return chatTypes;
end

-- A group's label as vanilla's menu shows it: the group's own name when it
-- holds several events, else its one event's.
local function GroupText(group)
	local events = ChatTypeGroup[group];
	if ( #events > 1 ) then
		return _G[group] or group;
	end
	return _G[events[1]] or group;
end

-- Chat types that take no class color of their own: an outgoing whisper's name
-- follows the Whisper setting (GetColoredName reads WHISPER for both).
local NO_CLASS_COLOR = { WHISPER_INFORM = true };

local function GroupEntry(group, text)
	local entry = {
		type = group,
		text = text or GroupText(group),
		checked = function () return IsListeningForMessageType(group); end,
		func = function (self, checked) ToggleChatMessageGroup(checked, group); end,
	};
	local chatTypes = GroupChatTypes(group);
	if ( #ChatTypeGroup[group] > 1 ) then
		entry.subTypes = {};
		for _, chatType in ipairs(chatTypes) do
			tinsert(entry.subTypes, {
				type = chatType,
				text = _G["CHAT_MSG_"..chatType] or chatType,
				noClassColor = NO_CLASS_COLOR[chatType],
			});
		end
	else
		entry.chatType = chatTypes[1];
		entry.noClassColor = entry.chatType and NO_CLASS_COLOR[entry.chatType];
	end
	return entry;
end

-- Appends an entry for each group of `groups` the client has and `used` doesn't.
local function AddGroups(list, groups, used, texts)
	if ( not groups ) then
		return;
	end
	for _, group in ipairs(groups) do
		if ( ChatTypeGroup[group] and not used[group] ) then
			used[group] = true;
			tinsert(list, GroupEntry(group, texts and texts[group]));
		end
	end
end

-- Groups placed in Other; the client's menu lists hold the rest.
local OTHER_COMBAT_GROUPS = {
	"COMBAT_XP_GAIN", "COMBAT_HONOR_GAIN", "COMBAT_FACTION_CHANGE", "SKILL", "LOOT",
	"SPELL_TRADESKILLS", "COMBAT_MISC_INFO",
};
local OTHER_SYSTEM_GROUPS = { "SYSTEM", "CHANNEL" };
local OTHER_CREATURE_GROUPS = { "CREATURE" };
local GROUP_TEXTS = { SYSTEM = SYSTEM_MESSAGES };

CHAT_CONFIG_CHAT_LEFT = {};
CHAT_CONFIG_OTHER_COMBAT = {};
CHAT_CONFIG_OTHER_SYSTEM = {};
CHAT_CONFIG_CHAT_CREATURE_LEFT = {};
CHAT_CONFIG_CHANNEL_LIST = {};

do
	local used = {};
	-- Placed first so the chat list leaves them out.
	AddGroups(CHAT_CONFIG_OTHER_SYSTEM, OTHER_SYSTEM_GROUPS, used, GROUP_TEXTS);
	AddGroups(CHAT_CONFIG_CHAT_CREATURE_LEFT, OTHER_CREATURE_GROUPS, used);
	AddGroups(CHAT_CONFIG_OTHER_COMBAT, OTHER_COMBAT_GROUPS, used);
	AddGroups(CHAT_CONFIG_CHAT_LEFT, ChannelMenuChatTypeGroups, used);
	-- A client's own additions to the Other menu.
	AddGroups(CHAT_CONFIG_OTHER_COMBAT, OtherMenuChatTypeGroups, used);
end

-- Vanilla's combat log lists, one tab each.
CHAT_CONFIG_COMBAT_TABS = {};
do
	local lists = {
		{ COMBAT_MESSAGES, CombatLogMenuChatTypeGroups },
		{ SPELL_MESSAGES, SpellLogMenuChatTypeGroups },
		{ SPELL_OTHER_MESSAGES, SpellLogOtherMenuChatTypeGroups },
		{ PERIODIC_MESSAGES, PeriodicLogMenuChatTypeGroups },
	};
	for _, value in ipairs(lists) do
		if ( value[2] ) then
			local list = {};
			AddGroups(list, value[2], {});
			tinsert(CHAT_CONFIG_COMBAT_TABS, { text = value[1], list = list });
		end
	end
end

CHAT_CONFIG_CATEGORIES = {
	[1] = "ChatConfigChatSettings",
	[2] = "ChatConfigCombatSettings",
	[3] = "ChatConfigChannelSettings",
	[4] = "ChatConfigOtherSettings",
};

-- ---------------------------------------------------------------------------
-- 3.3.5's helpers, on the chat window being edited
-- ---------------------------------------------------------------------------

function ToggleChatMessageGroup(checked, group)
	if ( checked ) then
		ChatFrame_AddMessageGroup(CurrentChatFrame(), group);
	else
		ChatFrame_RemoveMessageGroup(CurrentChatFrame(), group);
	end
end

function ToggleChatChannel(checked, channel)
	if ( checked ) then
		ChatFrame_AddChannel(CurrentChatFrame(), channel);
	else
		ChatFrame_RemoveChannel(CurrentChatFrame(), channel);
	end
end

function ToggleChatColorNamesByClassGroup(checked, group)
	local info = ChatTypeGroup[group];
	if ( info ) then
		for _, value in pairs(info) do
			SetChatColorNameByClass(FCF_StripChatMsg(value), checked);
		end
	else
		SetChatColorNameByClass(group, checked);
	end
end

function ColorClassesCheckBox_OnClick(self, checked)
	local row = self:GetParent();
	if ( row.chatType ) then
		SetChatColorNameByClass(row.chatType, checked);
	else
		ToggleChatColorNamesByClassGroup(checked, row.type);
	end
end

function IsListeningForMessageType(messageType)
	local messageTypeList = CurrentChatFrame().messageTypeList;
	if ( messageTypeList ) then
		for _, value in pairs(messageTypeList) do
			if ( strupper(value) == strupper(messageType) ) then
				return true;
			end
		end
	end
	return false;
end

function IsClassColoringMessageType(messageType)
	local groupInfo = ChatTypeGroup[messageType];
	if ( groupInfo ) then
		-- Any of the group's types coloring by class counts for the group.
		for _, value in pairs(groupInfo) do
			local info = ChatTypeInfo[FCF_StripChatMsg(value)];
			if ( info and info.colorNameByClass ) then
				return true;
			end
		end
		return false;
	end
	local info = ChatTypeInfo[messageType];
	return info and info.colorNameByClass;
end

function GetMessageTypeColor(messageType)
	local group = ChatTypeGroup[messageType];
	local chatType = messageType;
	if ( group ) then
		chatType = group[1];
	end
	local info = ChatTypeInfo[FCF_StripChatMsg(chatType)];
	return info.r, info.g, info.b, group;
end

-- A swatch colors its own chat type (`chatType`, set by this window's rows);
-- a swatch with only 3.3.5's `type` colors that whole group, as 3.3.5 does.
function MessageTypeColor_OpenColorPicker(self)
	local chatType = self.chatType or self:GetParent().chatType;
	local r, g, b, groupEvents;
	if ( chatType ) then
		local info = ChatTypeInfo[chatType];
		r, g, b = info.r or 1, info.g or 1, info.b or 1;
	else
		r, g, b, groupEvents = GetMessageTypeColor(self.type);
	end
	CHAT_CONFIG_CURRENT_COLOR_SWATCH = self;
	local swatchTexture = _G[self:GetName().."NormalTexture"];
	local function Apply(newR, newG, newB)
		if ( chatType ) then
			ChangeChatColor(chatType, newR, newG, newB);
		elseif ( groupEvents ) then
			for _, value in pairs(groupEvents) do
				ChangeChatColor(FCF_StripChatMsg(value), newR, newG, newB);
			end
		else
			ChangeChatColor(self.type, newR, newG, newB);
		end
		swatchTexture:SetVertexColor(newR, newG, newB);
	end
	ColorPickerFrame.func = function () Apply(ColorPickerFrame:GetColorRGB()); end;
	ColorPickerFrame.cancelFunc = function (previous) Apply(previous.r, previous.g, previous.b); end;
	ColorPickerFrame.hasOpacity = nil;
	ColorPickerFrame.opacityFunc = nil;
	ColorPickerFrame.opacity = nil;
	ColorPickerFrame.previousValues = { r = r, g = g, b = b };
	ColorPickerFrame:SetColorRGB(r, g, b);
	ShowUIPanel(ColorPickerFrame);
end

function ChatConfigFrame_PlayCheckboxSound(checked)
	if ( checked ) then
		PlaySound("igMainMenuOptionCheckBoxOn");
	else
		PlaySound("igMainMenuOptionCheckBoxOff");
	end
end

-- ---------------------------------------------------------------------------
-- Check box lists
-- ---------------------------------------------------------------------------

-- A row's swatch and class color, for one chat type (or hidden without one).
local function SetRowChatType(rowName, chatType, noClassColor)
	local row = _G[rowName];
	row.chatType = chatType;
	local swatch = _G[rowName.."ColorSwatch"];
	if ( swatch ) then
		swatch.chatType = chatType;
		if ( chatType ) then
			swatch:Show();
		else
			swatch:Hide();
		end
	end
	local colorClasses = _G[rowName.."ColorClasses"];
	if ( colorClasses ) then
		if ( chatType and not noClassColor ) then
			colorClasses:Show();
		else
			colorClasses:Hide();
		end
	end
end

local function UpdateRowColors(rowName)
	local row = _G[rowName];
	local info = row.chatType and ChatTypeInfo[row.chatType];
	if ( not info ) then
		return;
	end
	local swatchTexture = _G[rowName.."ColorSwatchNormalTexture"];
	if ( swatchTexture ) then
		swatchTexture:SetVertexColor(info.r or 1, info.g or 1, info.b or 1);
	end
	local colorClasses = _G[rowName.."ColorClasses"];
	if ( colorClasses ) then
		colorClasses:SetChecked(info.colorNameByClass);
	end
end

-- 3.3.5's builder, plus the sub-type rows of a multi-type group and an
-- optional column count. Rows are frame.."CheckBox"..index; a group's
-- sub-type rows are that name.."_"..k (3.3.5's tiered naming).
function ChatConfig_CreateCheckboxes(frame, checkBoxTable, checkBoxTemplate, title, columns)
	local checkBoxNameString = frame:GetName().."CheckBox";
	local subTemplate = "ChatConfigSubTypeWithSwatchTemplate";
	if ( checkBoxTemplate == "ChatConfigCheckBoxWithSwatchAndClassColorTemplate" ) then
		subTemplate = "ChatConfigSubTypeWithSwatchAndClassColorTemplate";
	end
	frame.checkBoxTable = checkBoxTable;
	if ( title ) then
		_G[frame:GetName().."Title"]:SetText(title);
	end

	columns = columns or 1;
	local perColumn = math.ceil(#checkBoxTable / columns);
	local rowWidth, columnHeight, maxHeight = 0, 0, 0;
	local columnTop, below;
	for index, value in ipairs(checkBoxTable) do
		local checkBoxName = checkBoxNameString..index;
		local checkBox = _G[checkBoxName] or CreateFrame("Frame", checkBoxName, frame, checkBoxTemplate);
		checkBox:ClearAllPoints();
		if ( index == 1 ) then
			checkBox:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -4);
			columnTop = checkBox;
		elseif ( math.mod(index - 1, perColumn) == 0 ) then
			checkBox:SetPoint("TOPLEFT", columnTop, "TOPRIGHT", COLUMN_SPACING, 0);
			columnTop = checkBox;
			columnHeight = 0;
		else
			checkBox:SetPoint("TOPLEFT", below, "BOTTOMLEFT", 0, 0);
		end
		checkBox.type = value.type;
		checkBox:Show();
		rowWidth = checkBox:GetWidth();
		columnHeight = columnHeight + checkBox:GetHeight();
		below = checkBox;

		local text = value.text or _G[value.type];
		local checkBoxFontString = _G[checkBoxName.."CheckText"];
		checkBoxFontString:SetText(text);
		local check = _G[checkBoxName.."Check"];
		check.func = value.func;
		check:SetID(index);
		check.tooltip = value.tooltip;
		check.tooltipStyle = nil;
		if ( value.maxWidth ) then
			checkBoxFontString:SetWidth(0);
			if ( checkBoxFontString:GetWidth() > value.maxWidth ) then
				checkBoxFontString:SetWidth(value.maxWidth);
				check.tooltip = text;
				check.tooltipStyle = 0;
			end
		end
		SetRowChatType(checkBoxName, value.chatType, value.noClassColor);

		if ( value.subTypes ) then
			for k, sub in ipairs(value.subTypes) do
				local subName = checkBoxName.."_"..k;
				local subRow = _G[subName] or CreateFrame("Frame", subName, checkBox, subTemplate);
				subRow:ClearAllPoints();
				subRow:SetPoint("TOPLEFT", below, "BOTTOMLEFT", 0, 0);
				subRow.type = sub.type;
				subRow:Show();
				_G[subName.."Text"]:SetText(sub.text);
				SetRowChatType(subName, sub.type, sub.noClassColor);
				columnHeight = columnHeight + subRow:GetHeight();
				below = subRow;
			end
		end
		if ( columnHeight > maxHeight ) then
			maxHeight = columnHeight;
		end
	end

	-- Rows left over from a longer list (the channel list changes).
	local index = #checkBoxTable + 1;
	while ( _G[checkBoxNameString..index] ) do
		_G[checkBoxNameString..index]:Hide();
		index = index + 1;
	end

	if ( #checkBoxTable > 0 ) then
		frame:SetWidth(columns * rowWidth + (columns - 1) * COLUMN_SPACING + ROW_PADDING);
		frame:SetHeight(maxHeight + ROW_PADDING);
	end
end

function ChatConfig_UpdateCheckboxes(frame)
	local checkBoxTable = frame.checkBoxTable;
	if ( not checkBoxTable or not CurrentChatFrame() ) then
		return;
	end
	local checkBoxNameString = frame:GetName().."CheckBox";
	for index, value in ipairs(checkBoxTable) do
		local checkBoxName = checkBoxNameString..index;
		local check = _G[checkBoxName.."Check"];
		if ( check ) then
			local checked = value.checked;
			if ( type(checked) == "function" ) then
				checked = checked();
			end
			check:SetChecked(checked);
			UpdateRowColors(checkBoxName);
			if ( value.subTypes ) then
				for k in ipairs(value.subTypes) do
					UpdateRowColors(checkBoxName.."_"..k);
				end
			end
		end
	end
end

function ChatConfig_UpdateChatSettings()
	ChatConfig_UpdateCheckboxes(ChatConfigChatSettingsLeft);
	if ( ChatConfigChannelSettingsLeft.checkBoxTable ) then
		ChatConfig_UpdateCheckboxes(ChatConfigChannelSettingsLeft);
	end
	ChatConfig_UpdateCheckboxes(ChatConfigOtherSettingsCombat);
	ChatConfig_UpdateCheckboxes(ChatConfigOtherSettingsSystem);
	ChatConfig_UpdateCheckboxes(ChatConfigOtherSettingsCreature);
	for _, value in ipairs(CHAT_CONFIG_COMBAT_TABS) do
		if ( value.frame ) then
			ChatConfig_UpdateCheckboxes(_G[value.frame]);
		end
	end
end

-- The channels the player is in: GetChannelList() returns id, name pairs.
function CreateChatChannelList(self, ...)
	local chatFrame = CurrentChatFrame();
	if ( not chatFrame ) then
		return;
	end
	local channelList = chatFrame.channelList;
	CHAT_CONFIG_CHANNEL_LIST = {};
	local count = 1;
	for i = 1, select("#", ...), 2 do
		local channelID = select(i, ...);
		local channel = select(i + 1, ...);
		local checked;
		if ( channelList ) then
			for _, value in pairs(channelList) do
				if ( strupper(value) == strupper(channel) ) then
					checked = 1;
				end
			end
		end
		local entry = {
			text = channelID.."."..channel,
			channelName = channel,
			type = "CHANNEL"..channelID,
			chatType = "CHANNEL"..channelID,
			maxWidth = CHATCONFIG_CHANNELS_MAXWIDTH,
			checked = checked,
		};
		entry.func = function (self, checked) ToggleChatChannel(checked, entry.channelName); end;
		CHAT_CONFIG_CHANNEL_LIST[count] = entry;
		count = count + 1;
	end
end

-- ---------------------------------------------------------------------------
-- The window
-- ---------------------------------------------------------------------------

function ClassColorLegend_OnLoad(self)
	self:SetBackdropBorderColor(TOOLTIP_DEFAULT_COLOR.r, TOOLTIP_DEFAULT_COLOR.g, TOOLTIP_DEFAULT_COLOR.b, 0.5);
	self:SetBackdropColor(0.27, 0.27, 0.27);
	local title = _G[self:GetName().."Title"];
	local fontStringOffset = 2;
	self.classStrings = {};
	for index, class in ipairs(CLASS_SORT_ORDER) do
		local fontString = self:CreateFontString(self:GetName().."Class"..index, "ARTWORK", "ClassColorLegendFontStringTemplate");
		if ( index > 1 ) then
			fontString:SetPoint("TOPLEFT", self.classStrings[index - 1], "BOTTOMLEFT", 0, -fontStringOffset);
		else
			fontString:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 10, -5);
		end
		local classColor = RAID_CLASS_COLORS[class];
		fontString:SetText(format("|cff%.2x%.2x%.2x%s|r", classColor.r * 255, classColor.g * 255, classColor.b * 255,
			LOCALIZED_CLASS_NAMES_MALE[class] or class));
		tinsert(self.classStrings, fontString);
	end
	self:SetHeight((self.classStrings[1]:GetHeight() + fontStringOffset) * #self.classStrings + title:GetHeight() + 31);
end

local function CreateCombatTabs()
	for index, value in ipairs(CHAT_CONFIG_COMBAT_TABS) do
		local list = CreateFrame("Frame", "ChatConfigCombatSettingsList"..index, ChatConfigCombatSettings, "ChatConfigBoxTemplate");
		list:SetPoint("TOPLEFT", ChatConfigCombatSettings, "TOPLEFT", 13, -13);
		ChatConfig_CreateCheckboxes(list, value.list, "ChatConfigCheckBoxWithSwatchTemplate", nil, 2);
		list:Hide();
		value.frame = list:GetName();

		local tabName = CHAT_CONFIG_COMBAT_TAB_NAME..index;
		local tab = CreateFrame("Button", tabName, ChatConfigBackgroundFrame, "ChatConfigTabTemplate");
		if ( index > 1 ) then
			tab:SetPoint("BOTTOMLEFT", _G[CHAT_CONFIG_COMBAT_TAB_NAME..(index - 1)], "BOTTOMRIGHT", -1, 0);
		else
			tab:SetPoint("BOTTOMLEFT", ChatConfigBackgroundFrame, "TOPLEFT", 2, -1);
		end
		_G[tabName.."Text"]:SetText(value.text);
		tab:SetID(index);
		PanelTemplates_TabResize(0, tab);
	end
end

function ChatConfigFrame_OnLoad(self)
	self:RegisterEvent("PLAYER_ENTERING_WORLD");
end

-- Built once the chat settings have loaded; the lists don't change after that.
function ChatConfigFrame_OnEvent(self, event)
	if ( event == "PLAYER_ENTERING_WORLD" ) then
		self:UnregisterEvent("PLAYER_ENTERING_WORLD");
		ChatConfig_CreateCheckboxes(ChatConfigChatSettingsLeft, CHAT_CONFIG_CHAT_LEFT, "ChatConfigCheckBoxWithSwatchAndClassColorTemplate", PLAYER_MESSAGES);
		ChatConfig_CreateCheckboxes(ChatConfigOtherSettingsCombat, CHAT_CONFIG_OTHER_COMBAT, "ChatConfigCheckBoxWithSwatchTemplate", COMBAT);
		ChatConfig_CreateCheckboxes(ChatConfigOtherSettingsSystem, CHAT_CONFIG_OTHER_SYSTEM, "ChatConfigCheckBoxWithSwatchTemplate", OTHER);
		ChatConfig_CreateCheckboxes(ChatConfigOtherSettingsCreature, CHAT_CONFIG_CHAT_CREATURE_LEFT, "ChatConfigCheckBoxWithSwatchTemplate", CREATURE_MESSAGES);
		CreateCombatTabs();
		ChatConfigCategory_OnClick(ChatConfigCategoryFrameButton1);
	end
end

function ChatConfigFrame_OnShow(self)
	if ( not self.chatFrame ) then
		self.chatFrame = SELECTED_CHAT_FRAME or DEFAULT_CHAT_FRAME;
	end
end

function ChatConfigCategoryFrame_OnShow(self)
	local chatFrame = CurrentChatFrame();
	if ( chatFrame == ChatFrame2 ) then
		ChatConfigCategory_OnClick(ChatConfigCategoryFrameButton2);
	else
		ChatConfigCategory_OnClick(ChatConfigCategoryFrameButton1);
	end
	ChatConfigFrameHeaderText:SetText(format(CHATCONFIG_HEADER, _G[chatFrame:GetName().."Tab"]:GetText() or ""));
	ChatConfigFrameHeader:SetWidth(ChatConfigFrameHeaderText:GetWidth() + 200);
end

function ChatConfigCategory_OnClick(self)
	self:UnlockHighlight();
	for index, value in ipairs(CHAT_CONFIG_CATEGORIES) do
		if ( self:GetID() == index ) then
			_G[value]:Show();
			self:LockHighlight();
		else
			_G[value]:Hide();
			_G["ChatConfigCategoryFrameButton"..index]:UnlockHighlight();
		end
	end
end

function ChatConfigChannelSettings_OnShow(self)
	-- Built here: the player's channels change.
	CreateChatChannelList(self, GetChannelList());
	ChatConfig_CreateCheckboxes(ChatConfigChannelSettingsLeft, CHAT_CONFIG_CHANNEL_LIST, "ChatConfigCheckBoxWithSwatchAndClassColorTemplate", CHANNELS);
	ChatConfig_UpdateCheckboxes(ChatConfigChannelSettingsLeft);
	ChatConfigFrameDefaultButton:Show();
	CombatLogDefaultButton:Hide();
end

function ChatConfigCombatSettings_OnShow(self)
	ChatConfigBackgroundFrame:SetPoint("TOPLEFT", ChatConfigCategoryFrame, "TOPRIGHT", 1, -COMBAT_TABS_HEIGHT);
	for index in ipairs(CHAT_CONFIG_COMBAT_TABS) do
		_G[CHAT_CONFIG_COMBAT_TAB_NAME..index]:Show();
	end
	ChatConfig_UpdateCombatTabs(self.selectedTab or 1);
	ChatConfigFrameDefaultButton:Hide();
	CombatLogDefaultButton:Show();
end

function ChatConfigCombatSettings_OnHide(self)
	ChatConfigBackgroundFrame:SetPoint("TOPLEFT", ChatConfigCategoryFrame, "TOPRIGHT", 1, 0);
	for index in ipairs(CHAT_CONFIG_COMBAT_TABS) do
		_G[CHAT_CONFIG_COMBAT_TAB_NAME..index]:Hide();
	end
end

function ChatConfig_UpdateCombatTabs(selectedTabID)
	ChatConfigCombatSettings.selectedTab = selectedTabID;
	for index, value in ipairs(CHAT_CONFIG_COMBAT_TABS) do
		local tab = _G[CHAT_CONFIG_COMBAT_TAB_NAME..index];
		local text = _G[CHAT_CONFIG_COMBAT_TAB_NAME..index.."Text"];
		local frame = _G[value.frame];
		if ( index == selectedTabID ) then
			tab:SetAlpha(1.0);
			text:SetVertexColor(HIGHLIGHT_FONT_COLOR.r, HIGHLIGHT_FONT_COLOR.g, HIGHLIGHT_FONT_COLOR.b);
			frame:Show();
			ChatConfig_UpdateCheckboxes(frame);
		else
			tab:SetAlpha(0.75);
			text:SetVertexColor(NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b);
			frame:Hide();
		end
	end
end

-- ---------------------------------------------------------------------------
-- Defaults
-- ---------------------------------------------------------------------------

-- The engine's own defaults, from its message group table (68 entries of
-- {name, on by default, version} at 0x00805FB0): its first ten groups start in
-- the first chat window, and the second window starts with every later group
-- flagged on. Turtle-lineage clients add a Hardcore group the engine doesn't
-- know; their ChatFrame.lua saves it per window (TW_HARDCORE_CHAT<n>) and turns
-- it on in every window but the combat log by default.
local DEFAULT_GENERAL_GROUPS = {
	"SYSTEM", "SAY", "YELL", "WHISPER", "PARTY", "GUILD", "CREATURE", "CHANNEL", "SKILL", "LOOT",
};
local COMBAT_LOG_GROUPS = {
	"COMBAT_MISC_INFO", "COMBAT_SELF_HITS", "COMBAT_SELF_MISSES", "COMBAT_PET_HITS", "COMBAT_PET_MISSES",
	"COMBAT_PARTY_HITS", "COMBAT_PARTY_MISSES", "COMBAT_FRIENDLYPLAYER_HITS", "COMBAT_FRIENDLYPLAYER_MISSES",
	"COMBAT_HOSTILEPLAYER_HITS", "COMBAT_HOSTILEPLAYER_MISSES", "COMBAT_CREATURE_VS_SELF_HITS",
	"COMBAT_CREATURE_VS_SELF_MISSES", "COMBAT_CREATURE_VS_PARTY_HITS", "COMBAT_CREATURE_VS_PARTY_MISSES",
	"COMBAT_CREATURE_VS_CREATURE_HITS", "COMBAT_CREATURE_VS_CREATURE_MISSES", "COMBAT_FRIENDLY_DEATH",
	"COMBAT_HOSTILE_DEATH", "COMBAT_XP_GAIN", "SPELL_SELF_DAMAGE", "SPELL_SELF_BUFF", "SPELL_PET_DAMAGE",
	"SPELL_PET_BUFF", "SPELL_PARTY_DAMAGE", "SPELL_PARTY_BUFF", "SPELL_FRIENDLYPLAYER_DAMAGE",
	"SPELL_FRIENDLYPLAYER_BUFF", "SPELL_HOSTILEPLAYER_DAMAGE", "SPELL_HOSTILEPLAYER_BUFF",
	"SPELL_CREATURE_VS_SELF_DAMAGE", "SPELL_CREATURE_VS_SELF_BUFF", "SPELL_CREATURE_VS_PARTY_DAMAGE",
	"SPELL_CREATURE_VS_PARTY_BUFF", "SPELL_CREATURE_VS_CREATURE_DAMAGE", "SPELL_CREATURE_VS_CREATURE_BUFF",
	"SPELL_TRADESKILLS", "SPELL_DAMAGESHIELDS_ON_SELF", "SPELL_DAMAGESHIELDS_ON_OTHERS", "SPELL_AURA_GONE_SELF",
	"SPELL_AURA_GONE_PARTY", "SPELL_AURA_GONE_OTHER", "SPELL_ITEM_ENCHANTMENTS", "SPELL_BREAK_AURA",
	"SPELL_PERIODIC_SELF_DAMAGE", "SPELL_PERIODIC_SELF_BUFFS", "SPELL_PERIODIC_PARTY_DAMAGE",
	"SPELL_PERIODIC_PARTY_BUFFS", "SPELL_PERIODIC_FRIENDLYPLAYER_DAMAGE", "SPELL_PERIODIC_FRIENDLYPLAYER_BUFFS",
	"SPELL_PERIODIC_HOSTILEPLAYER_DAMAGE", "SPELL_PERIODIC_HOSTILEPLAYER_BUFFS", "SPELL_PERIODIC_CREATURE_DAMAGE",
	"SPELL_PERIODIC_CREATURE_BUFFS", "SPELL_FAILED_LOCALPLAYER", "COMBAT_HONOR_GAIN", "COMBAT_FACTION_CHANGE",
	"MONEY",
};
local COMBAT_LOG_DEFAULT_OFF = {
	COMBAT_PARTY_HITS = true, COMBAT_PARTY_MISSES = true, COMBAT_FRIENDLYPLAYER_HITS = true,
	COMBAT_FRIENDLYPLAYER_MISSES = true, COMBAT_CREATURE_VS_PARTY_HITS = true, COMBAT_CREATURE_VS_PARTY_MISSES = true,
	COMBAT_CREATURE_VS_CREATURE_HITS = true, COMBAT_CREATURE_VS_CREATURE_MISSES = true, SPELL_PARTY_DAMAGE = true,
	SPELL_PARTY_BUFF = true, SPELL_FRIENDLYPLAYER_DAMAGE = true, SPELL_FRIENDLYPLAYER_BUFF = true,
	SPELL_CREATURE_VS_PARTY_DAMAGE = true, SPELL_CREATURE_VS_PARTY_BUFF = true,
	SPELL_CREATURE_VS_CREATURE_DAMAGE = true, SPELL_CREATURE_VS_CREATURE_BUFF = true,
	SPELL_DAMAGESHIELDS_ON_OTHERS = true, SPELL_AURA_GONE_PARTY = true, SPELL_AURA_GONE_OTHER = true,
	SPELL_PERIODIC_PARTY_DAMAGE = true, SPELL_PERIODIC_PARTY_BUFFS = true,
	SPELL_PERIODIC_FRIENDLYPLAYER_DAMAGE = true, SPELL_PERIODIC_FRIENDLYPLAYER_BUFFS = true,
	SPELL_FAILED_LOCALPLAYER = true,
};

-- Sets `chatFrame`'s groups among `groups` to the engine's defaults for it.
local function ResetGroups(chatFrame, groups)
	local isGroup = {};
	for _, group in ipairs(groups) do
		isGroup[group] = true;
	end
	local current = {};
	for _, value in pairs(chatFrame.messageTypeList) do
		if ( isGroup[strupper(value)] ) then
			tinsert(current, value);
		end
	end
	for _, value in ipairs(current) do
		ChatFrame_RemoveMessageGroup(chatFrame, value);
	end
	local id = chatFrame:GetID();
	for index, group in ipairs(groups) do
		local on;
		if ( groups == DEFAULT_GENERAL_GROUPS ) then
			on = id == 1;
		else
			on = id == 2 and not COMBAT_LOG_DEFAULT_OFF[group];
		end
		if ( on and ChatTypeGroup[group] ) then
			ChatFrame_AddMessageGroup(chatFrame, group);
		end
	end
end

local function ResetHardcore(chatFrame)
	if ( not ChatTypeGroup["HARDCORE"] ) then
		return;
	end
	if ( chatFrame:GetID() == 2 ) then
		ChatFrame_RemoveMessageGroup(chatFrame, "HARDCORE");
		return;
	end
	for _, value in pairs(chatFrame.messageTypeList) do
		if ( strupper(value) == "HARDCORE" ) then
			return;
		end
	end
	ChatFrame_AddMessageGroup(chatFrame, "HARDCORE");
end

-- Leaves the chat window in no channels (ChatFrame_RemoveAllChannels only
-- clears the Lua lists; the saved list needs each one removed).
local function RemoveChannels(chatFrame)
	local channels = {};
	for _, value in pairs(chatFrame.channelList) do
		tinsert(channels, value);
	end
	for _, value in ipairs(channels) do
		ChatFrame_RemoveChannel(chatFrame, value);
	end
end

-- 3.3.5's reset, on vanilla's chat frame functions.
if ( not FCF_ResetChatWindows ) then
	function FCF_ResetChatWindows()
		ChatFrame1:ClearAllPoints();
		ChatFrame1:SetPoint("BOTTOMLEFT", "UIParent", "BOTTOMLEFT", 32, 95);
		ChatFrame1:SetWidth(430);
		ChatFrame1:SetHeight(120);
		FCF_SetButtonSide(ChatFrame1, "left");
		for i = 1, NUM_CHAT_WINDOWS do
			local chatFrame = _G["ChatFrame"..i];
			FCF_SetChatWindowFontSize(chatFrame, 14);
			FCF_SetWindowName(chatFrame, "");
			FCF_SetWindowColor(chatFrame, DEFAULT_CHATFRAME_COLOR.r, DEFAULT_CHATFRAME_COLOR.g, DEFAULT_CHATFRAME_COLOR.b);
			FCF_SetWindowAlpha(chatFrame, DEFAULT_CHATFRAME_ALPHA);
			ResetGroups(chatFrame, DEFAULT_GENERAL_GROUPS);
			ResetGroups(chatFrame, COMBAT_LOG_GROUPS);
			ResetHardcore(chatFrame);
			RemoveChannels(chatFrame);
			if ( i > 2 ) then
				FCF_UnDockFrame(chatFrame);
				FCF_Close(chatFrame);
			end
		end
		-- The joined channels show in the first window, as for a new character.
		local channels = { GetChannelList() };
		for i = 2, #channels, 2 do
			ChatFrame_AddChannel(ChatFrame1, channels[i]);
		end
		FCF_DockFrame(ChatFrame1, 1);
		FCF_DockFrame(ChatFrame2, 2);
		SELECTED_CHAT_FRAME = ChatFrame1;
		UIParent_ManageFramePositions();
	end
end

if ( not StaticPopupDialogs["RESET_CHAT"] ) then
	StaticPopupDialogs["RESET_CHAT"] = {
		text = RESET_CHAT_WINDOW,
		button1 = ACCEPT,
		button2 = CANCEL,
		OnAccept = function ()
			FCF_ResetChatWindows();
			if ( ChatConfigFrame:IsShown() ) then
				ChatConfig_UpdateChatSettings();
			end
		end,
		timeout = 0,
		whileDead = 1,
		hideOnEscape = 1,
	};
end

if ( not StaticPopupDialogs["CONFIRM_COMBAT_FILTER_DEFAULTS"] ) then
	StaticPopupDialogs["CONFIRM_COMBAT_FILTER_DEFAULTS"] = {
		text = CONFIRM_COMBAT_FILTER_DEFAULTS,
		button1 = OKAY,
		button2 = CANCEL,
		OnAccept = function ()
			ResetGroups(CurrentChatFrame(), COMBAT_LOG_GROUPS);
			if ( ChatConfigFrame:IsShown() ) then
				ChatConfig_UpdateChatSettings();
			end
		end,
		timeout = 0,
		whileDead = 1,
		exclusive = 1,
		hideOnEscape = 1,
	};
end

-- ---------------------------------------------------------------------------
-- The chat tab menu's Settings entry
-- ---------------------------------------------------------------------------

local function OpenChatConfig(chatFrame)
	if ( ChatConfigFrame:IsShown() ) then
		HideUIPanel(ChatConfigFrame);
	end
	ChatConfigFrame.chatFrame = chatFrame;
	ShowUIPanel(ChatConfigFrame);
end

local function WithSettingsEntry(initialize)
	return function (...)
		initialize(...);
		if ( UIDROPDOWNMENU_MENU_LEVEL ~= 1 ) then
			return;
		end
		local info = UIDropDownMenu_CreateInfo();
		info.text = CHAT_CONFIGURATION;
		info.func = OpenChatConfig;
		info.arg1 = FCF_GetCurrentChatFrame();
		info.notCheckable = 1;
		UIDropDownMenu_AddButton(info);
	end
end

do
	local original = FCFOptionsDropDown_Initialize;
	if ( original ) then
		FCFOptionsDropDown_Initialize = WithSettingsEntry(original);
		-- Each tab's menu captured the function when it loaded.
		for i = 1, NUM_CHAT_WINDOWS do
			local dropDown = _G["ChatFrame"..i.."TabDropDown"];
			if ( dropDown and dropDown.initialize == original ) then
				dropDown.initialize = FCFOptionsDropDown_Initialize;
			end
		end
	end
end
