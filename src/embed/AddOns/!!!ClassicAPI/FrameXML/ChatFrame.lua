-- Sender names in their class colour, the 4.3.4 chat frame's option
-- (SetChatColorNameByClass).
--
-- The engine announces each chat type's option with
-- UPDATE_CHAT_COLOR_NAME_BY_CLASS, and the chat frame keeps it in
-- ChatTypeInfo[type].colorNameByClass (the handler is retail's, from
-- ChatFrameOverrides.lua). 4.3.4's GetColoredName then colours a CHAT_MSG
-- sender, and its ChatFrame_OnEvent writes the sender link as
-- "|Hplayer:"..arg2.."|h["..GetColoredName(event, ...).."]|h".
--
-- 1.12.1's ChatFrame_OnEvent writes "|Hplayer:"..arg2.."|h["..arg2.."]|h"
-- and has no step for the name. It is not replaced, because clients change
-- it (Turtle adds chat types). Instead, while it handles one CHAT_MSG event,
-- the frame's AddMessage gets the 4.3.4 link in place of the 1.12.1 one, so
-- an addon that hooks AddMessage sees the line 4.3.4 would give it.

local colorNameFrame = CreateFrame("Frame");
colorNameFrame:RegisterEvent("UPDATE_CHAT_COLOR_NAME_BY_CLASS");
colorNameFrame:SetScript("OnEvent", function(self, event, arg1, arg2)
	local info = ChatTypeInfo[strupper(arg1)];
	if ( info ) then
		info.colorNameByClass = arg2;
		if ( strupper(arg1) == "WHISPER" ) then
			info = ChatTypeInfo["REPLY"];
			if ( info ) then
				info.colorNameByClass = arg2;
			end
		end
	end
end);

-- 4.3.4 FrameXML/ChatFrame.lua. arg12 is the sender's GUID, "" for a sender
-- that is not a player; the nil test is for a CHAT_MSG event that an addon
-- fires by hand without it.
function GetColoredName(event, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12)
	local chatType = strsub(event, 10);
	if ( strsub(chatType, 1, 7) == "WHISPER" ) then
		chatType = "WHISPER";
	end
	if ( strsub(chatType, 1, 7) == "CHANNEL" ) then
		chatType = "CHANNEL"..arg8;
	end
	local info = ChatTypeInfo[chatType];

	if ( info and info.colorNameByClass and arg12 and arg12 ~= "" ) then
		local localizedClass, englishClass, localizedRace, englishRace, sex = GetPlayerInfoByGUID(arg12)

		if ( englishClass ) then
			local classColorTable = RAID_CLASS_COLORS[englishClass];
			if ( not classColorTable ) then
				return arg2;
			end
			return string.format("\124cff%.2x%.2x%.2x", classColorTable.r*255, classColorTable.g*255, classColorTable.b*255)..arg2.."\124r"
		end
	end

	return arg2;
end

-- The sender link of the event in progress, as 1.12.1 writes it and as
-- 4.3.4 does, and the frame AddMessage it stands in front of.
local senderLink, coloredSenderLink, frameAddMessage;
-- The frame whose AddMessage is swapped, and the field it held before
-- (nil when AddMessage came from the frame's methods).
local swappedFrame, swappedField;

local function AddColoredSenderMessage(self, text, ...)
	if ( type(text) == "string" ) then
		local first, last = strfind(text, senderLink, 1, true);
		if ( first ) then
			text = strsub(text, 1, first - 1)..coloredSenderLink..strsub(text, last + 1);
		end
	end
	return frameAddMessage(self, text, ...);
end

-- Puts the frame's AddMessage back. Runs before each event too, so an error
-- in the handler does not leave the swap in place.
local function RestoreAddMessage()
	if ( swappedFrame ) then
		swappedFrame.AddMessage = swappedField;
		swappedFrame, swappedField = nil, nil;
	end
end

local ChatFrame_OnEvent_Original = ChatFrame_OnEvent;

function ChatFrame_OnEvent(event)
	RestoreAddMessage();
	if ( strsub(event, 1, 9) ~= "CHAT_MSG_" or type(arg2) ~= "string" or arg2 == "" ) then
		return ChatFrame_OnEvent_Original(event);
	end
	local coloredName = GetColoredName(event, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12);
	if ( coloredName == arg2 ) then
		return ChatFrame_OnEvent_Original(event);
	end

	local frame = this;
	senderLink = "|Hplayer:"..arg2.."|h["..arg2.."]|h";
	coloredSenderLink = "|Hplayer:"..arg2.."|h["..coloredName.."]|h";
	frameAddMessage = frame.AddMessage;
	swappedFrame, swappedField = frame, rawget(frame, "AddMessage");
	frame.AddMessage = AddColoredSenderMessage;
	ChatFrame_OnEvent_Original(event);
	RestoreAddMessage();
end
