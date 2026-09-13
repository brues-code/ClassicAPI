LinkTypes = {
	AADCOpenConfig = "aadcopenconfig",
	Action = "action",
	AddOn = "addon",
	AdventureGuide = "journal",
	APIDocumentation = "api",
	AzeriteEssence = "azessence",
	BattlegroundUI = "battleground",
	BattlePet = "battlepet",
	BattlePetAbility = "battlePetAbil",
	BNPlayer = "BNplayer",
	BNPlayerCommunity = "BNplayerCommunity",
	CalendarEvent = "calendarEvent",
	CensoredMessage = "censoredmessage",
	CensoredMessageRewrite = "censoredmessagerewrite",
	CensoredMessageConfirmSend = "censoredmessageconfirmsend",
	Channel = "channel",
	ClubFinder = "clubFinder",
	ClubTicket = "clubTicket",
	Community = "community",
	DeathRecap = "death",
	DelveCompanionConfig = "delvecompanionconfig",
	DungeonScore = "dungeonScore",
	-- EditModeLayout = "layout",
	EventPOI = "eventpoi",
	GarrisonFollower = "garrfollower",
	GarrisonFollowerAbility = "garrfollowerability",
	GarrisonMission = "garrmission",
	GMChat = "GMChat",
	GroupFinderUI = "lfd",
	Item = "item",
	LevelUpToast = "levelup",
	LFGListing = "lfglisting",
	LootHistory = "lootHistory",
	MountEquipment = "mountequipment",
	PerksActivity = "perksactivity",
	Player = "player",
	PlayerCommunity = "playerCommunity",
	PlayerGM = "playerGM",
	PvPRating = "pvpRating",
	PvPTalentsUI = "honortalent",
	PvPUI = "pvpbgs",
	RaidTargetIcon = "icon",
	ReportCensoredMessage = "reportcensoredmessage",
	SpecializationsUI = "specpane",
	Spell = "spell",
	StoreCategory = "storecategory",
	TalentBuild = "talentbuild",
	TalentsUI = "talentpane",
	TransmogAppearance = "transmogappearance",
	TransmogIllusion = "transmogillusion",
	TransmogOutfit = "outfit",
	TransmogSet = "transmogset",
	Unit = "unit",
	URLIndex = "urlIndex",
	WarbandScene = "warbandScene",
	WorldMapWaypoint = "worldmap",
	WorldQuest = "worldquest",
	Talent = "talent"
};

LinkUtil = {};

-- `assertsafe` isn't a global in stock 1.12; defined as a local fallback so
-- this file stays self-contained, the same way FunctionUtil.lua and Pools.lua
-- do. Only fires when `condition` is falsy, matching Blizzard's semantics.
local function assertsafe(condition, msg)
	if not condition then
		local handler = geterrorhandler and geterrorhandler();
		if handler then
			handler(msg or "assertion failed!");
		end
	end
end

function LinkUtil.FormatLink(linkType, linkDisplayText, ...)
	local linkFormatTable = { string.format("|H%s", linkType) };
	for i = 1, arg.n do
		linkFormatTable[i + 1] = arg[i];
	end
	local returnLink = table.concat(linkFormatTable, ":");
	if linkDisplayText then
		return returnLink .. string.format("|h%s|h", linkDisplayText);
	else
		return returnLink .. "|h";
	end
end

function LinkUtil.SplitLinkData(linkData)
	local linkType, linkOptions = string.split(":", linkData, 2);
	linkOptions = linkOptions or "";  -- Could be nil if there's no ":" in linkData.
	return linkType, linkOptions;
end

function LinkUtil.SplitLink(link) -- returns linkText and displayText
	return string.match(link, "^|H(.+)|h(.*)|h$");
end

function LinkUtil.SplitLinkOptions(linkOptions)
	return string.split(":", linkOptions);
end

-- Extract the first link from the text given, ignoring leading and trailing characters.
-- returns linkType, linkOptions, displayText
function LinkUtil.ExtractLink(text)
	-- linkType: |H([^:]*): matches everything that's not a colon, up to the first colon.
	-- linkOptions: ([^|]*)|h matches everything that's not a |, up to the first |h.
	-- displayText: (.*)|h matches everything up to the second |h.
	-- Ex: |cffffffff|Htype:a:b:c:d|htext|h|r becomes type, a:b:c:d, text
	return string.match(text, [[|H([^:]*):([^|]*)|h(.*)|h]]);
end

function LinkUtil.ExtractNydusLink(text)
	-- Extracts ex. "urlIndex:24" from strings like "|HurlIndex:24|h"
	return string.match(text, [[|H([^|]*)|h]]);
end

function LinkUtil.IsLinkType(link, matchLinkType)
	local linkType, linkOptions, displayText = LinkUtil.ExtractLink(link);
	return linkType == matchLinkType;
end


function ExtractHyperlinkString(linkString)
	local preString, hyperlinkString, postString = string.match(linkString, "^(.*)|H(.+)|h(.*)$");
	return preString ~= nil, preString, hyperlinkString, postString;
end

function ExtractQuestRewardID(linkString)
	return string.match(linkString, "^questreward:(%d+)$");
end

function GetItemInfoFromHyperlink(link)
	local strippedItemLink, itemID = string.match(link, "|Hitem:((%d+).-)|h");
	if itemID then
		return tonumber(itemID), strippedItemLink;
	end
end

function GetAchievementInfoFromHyperlink(link)
	local linkType, linkData = LinkUtil.SplitLinkData(link);
	if linkType and string.match(linkType, "|Hachievement") then
		local achievementID, _, complete = strsplit(":", linkData);
		return tonumber(achievementID), complete == "1";
	end
end

function GetPlayerLink(characterName, linkDisplayText, lineID, chatType, chatTarget)
	-- Use simplified link if possible
	if lineID or chatType or chatTarget then
		return LinkUtil.FormatLink(LinkTypes.Player, linkDisplayText, characterName, lineID or 0, chatType or 0, chatTarget or "");
	else
		return LinkUtil.FormatLink(LinkTypes.Player, linkDisplayText, characterName);
	end
end

do
	local s_linkHandlerFunctions = {};

	LinkProcessorResponse = {
		Unhandled = 1,
		Handled = 2,
	};

	function LinkUtil.ProcessLink(link, text, contextData)
		local linkType, linkOptions = LinkUtil.SplitLinkData(link);
		local linkData = { type = linkType, options = linkOptions };
		local handlerFunction = s_linkHandlerFunctions[linkType];
		local response;

		if handlerFunction then
			response = handlerFunction(link, text, linkData, contextData);

			if response == nil then
				response = LinkProcessorResponse.Handled;
			end
		else
			response = LinkProcessorResponse.Unhandled;
		end

		return response;
	end

	function LinkUtil.IsLinkHandlerRegistered(linkType)
		return s_linkHandlerFunctions[linkType] ~= nil;
	end

	function LinkUtil.RegisterLinkHandler(linkType, handlerFunction)
		if s_linkHandlerFunctions[linkType] ~= nil then
			assertsafe(false, string.format("attempted to register a duplicate link handler for '%s'", linkType));
			return;
		elseif type(linkType) ~= "string" then
			assertsafe(false, string.format("attempted to register an invalid link type '%s'", tostring(linkType)));
			return;
		end

		s_linkHandlerFunctions[linkType] = handlerFunction;
	end
end

------------------------------------------------------------------------------
-- Click dispatch. Retail's SetItemRef opens with exactly this: build the
-- context, offer the link to the registered handlers, and stop if one took it.
-- 1.12's SetItemRef knows only item and player links and would reject anything
-- else, so the dispatch wraps it here instead of living in the engine.
--
-- The chat renderer itself accepts any |H<type>:...|h[text]|h and makes it
-- clickable, so a handler is all a new link type needs.
--
-- Capture-and-replace, not hooksecurefunc: a post-hook cannot stop the engine
-- from rejecting the link first. 1.12 passes (link, text, button); `link` is
-- the payload without the |H and |h wrappers, `text` the whole decorated link.
------------------------------------------------------------------------------
local originalSetItemRef = SetItemRef;
function SetItemRef(link, text, button, chatFrame)
	local contextData = { button = button, frame = chatFrame };
	if LinkUtil.ProcessLink(link, text, contextData) == LinkProcessorResponse.Handled then
		return;
	end
	return originalSetItemRef(link, text, button, chatFrame);
end
