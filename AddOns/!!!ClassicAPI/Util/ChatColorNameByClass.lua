-- Chat sender names in their class color, per chat type, as later clients have
-- it: SetChatColorNameByClass(chatType, colorNameByClass), the chat type's
-- ChatTypeInfo[chatType].colorNameByClass, the UPDATE_CHAT_COLOR_NAME_BY_CLASS
-- event and GetColoredName(event, arg1, ..., arg12). The chat settings window's
-- Show Class Color boxes (FrameXML/ChatConfigFrame.lua) set it.
--
-- Later clients save the setting per character with the chat settings; here
-- it's this addon's per-character ClassicAPI_ChatColorNameByClass
-- ({ [chatType] = true }). Their chat frames color the name through
-- GetColoredName, from the sender GUID in arg12. Vanilla's chat events carry no
-- GUID and its ChatFrame_OnEvent writes the line itself, so the chat frames'
-- AddMessage colors the sender in the finished line instead, while the
-- CHAT_MSG_* event is being dispatched (GetCurrentChatGUID() has the sender):
-- inside the sender's |Hplayer| link, or the first mention of the name in an
-- emote, where vanilla writes it unlinked. The class is the one the client has
-- cached for that GUID (GetPlayerInfoByGUID); without one the name stays as it
-- is, as on later clients. Nothing changes until a chat type is set.

local ADDON_NAME = "!!!ClassicAPI";

local saved = {};
local numColored = 0;
local hooked = false;

local ColorSender;

-- The chat frames' AddMessage is wrapped the first time a chat type is set.
local function HookChatFrames()
	hooked = true;
	for i = 1, NUM_CHAT_WINDOWS do
		local chatFrame = _G["ChatFrame"..i];
		local addMessage = chatFrame.AddMessage;
		chatFrame.AddMessage = function (self, text, a1, a2, a3, a4, a5, a6)
			if ( numColored > 0 ) then
				text = ColorSender(text);
			end
			return addMessage(self, text, a1, a2, a3, a4, a5, a6);
		end
	end
end

local function Apply(chatType, colorNameByClass)
	local info = ChatTypeInfo[chatType];
	if ( not info ) then
		return;
	end
	if ( colorNameByClass and not info.colorNameByClass ) then
		numColored = numColored + 1;
		if ( not hooked ) then
			HookChatFrames();
		end
	elseif ( not colorNameByClass and info.colorNameByClass ) then
		numColored = numColored - 1;
	end
	info.colorNameByClass = colorNameByClass and true or false;
end

function SetChatColorNameByClass(chatType, colorNameByClass)
	if ( type(chatType) ~= "string" ) then
		error("Usage: SetChatColorNameByClass(\"chatType\", colorNameByClass)", 2);
	end
	chatType = strupper(chatType);
	local on = colorNameByClass and true or false;
	if ( on ) then
		saved[chatType] = true;
	else
		saved[chatType] = nil;
	end
	Apply(chatType, on);
	_classicapi_FireUpdateChatColorNameByClass(chatType, on);
end

-- 3.3.5's GetColoredName. Vanilla's chat events have no arg12; the sender GUID
-- comes from GetCurrentChatGUID() while the event is dispatched.
function GetColoredName(event, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12)
	local chatType = strsub(event, 10);
	if ( strsub(chatType, 1, 7) == "WHISPER" ) then
		chatType = "WHISPER";
	end
	if ( strsub(chatType, 1, 7) == "CHANNEL" ) then
		chatType = "CHANNEL"..(arg8 or "");
	end
	local info = ChatTypeInfo[chatType];
	local guid = arg12;
	if ( guid == nil ) then
		guid = GetCurrentChatGUID();
	end
	if ( info and info.colorNameByClass and guid and guid ~= "" ) then
		local _, englishClass = GetPlayerInfoByGUID(guid);
		if ( englishClass ) then
			local classColorTable = RAID_CLASS_COLORS[englishClass];
			if ( not classColorTable ) then
				return arg2;
			end
			return format("|cff%.2x%.2x%.2x", classColorTable.r * 255, classColorTable.g * 255, classColorTable.b * 255)..arg2.."|r";
		end
	end
	return arg2;
end

-- The line vanilla's ChatFrame_OnEvent wrote for the CHAT_MSG_* event now
-- dispatched (the event, arg2 and arg8 globals), with the sender colored.
function ColorSender(text)
	local name = arg2;
	if ( type(text) ~= "string" or type(event) ~= "string" or strsub(event, 1, 9) ~= "CHAT_MSG_"
			or type(name) ~= "string" or name == "" ) then
		return text;
	end
	local colored = GetColoredName(event, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
	if ( colored == name ) then
		return text;
	end
	local link = "|Hplayer:"..name.."|h["..name.."]|h";
	local first, last = strfind(text, link, 1, true);
	if ( first ) then
		return strsub(text, 1, first - 1).."|Hplayer:"..name.."|h["..colored.."]|h"..strsub(text, last + 1);
	end
	local chatType = strsub(event, 10);
	if ( chatType == "EMOTE" or chatType == "TEXT_EMOTE" ) then
		first, last = strfind(text, name, 1, true);
		if ( first ) then
			return strsub(text, 1, first - 1)..colored..strsub(text, last + 1);
		end
	end
	return text;
end

local loader = CreateFrame("Frame");
loader:RegisterEvent("ADDON_LOADED");
loader:RegisterEvent("PLAYER_LOGIN");
loader:SetScript("OnEvent", function ()
	if ( event == "ADDON_LOADED" and arg1 == ADDON_NAME ) then
		if ( type(ClassicAPI_ChatColorNameByClass) == "table" ) then
			for chatType, on in pairs(ClassicAPI_ChatColorNameByClass) do
				if ( on ) then
					saved[chatType] = true;
				end
			end
		end
		ClassicAPI_ChatColorNameByClass = saved;
		for chatType in pairs(saved) do
			Apply(chatType, true);
		end
	elseif ( event == "PLAYER_LOGIN" ) then
		-- 3.3.5's FrameXML sets colorNameByClass only from this event, so its
		-- client sends the saved settings through it; code written for it
		-- learns them the same way here.
		for chatType in pairs(saved) do
			_classicapi_FireUpdateChatColorNameByClass(chatType, true);
		end
		this:UnregisterEvent("PLAYER_LOGIN");
	end
end);
