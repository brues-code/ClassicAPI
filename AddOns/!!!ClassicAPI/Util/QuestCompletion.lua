-- Completed-quest queries on Turtle-lineage servers: QueryQuestsCompleted /
-- GetQuestsCompleted / QUEST_QUERY_COMPLETE (the 3.3 API) and
-- C_QuestLog.IsQuestFlaggedCompleted / C_QuestLog.GetAllCompletedQuestIDs.
--
-- Vanilla's protocol has no completed-quests query and the client keeps no
-- history, so there is nothing to read locally. Turtle WoW, and servers built
-- on it, answer the chat command `.queststatus` with the list as hidden addon
-- messages under the prefix "TWQUEST": quest IDs separated by spaces, split
-- over several messages. The request is only sent on a Turtle-lineage client
-- (TURTLE_WOW_VERSION, set by Turtle's FrameXML), whose server takes the
-- command; elsewhere a `.` command would reach chat, so nothing here is
-- defined and the functions stay absent.
--
-- Implementation notes:
--   - QueryQuestsCompleted() is asynchronous, as on 3.3 clients. The list is
--     taken as complete once no TWQUEST message has arrived for QUIET seconds
--     after the first; GetQuestsCompleted() then returns it and
--     QUEST_QUERY_COMPLETE fires. A query that gets no reply within TIMEOUT
--     seconds is dropped - no event, the previous list stays.
--   - A request made before the world has finished loading is sent
--     ENTER_DELAY seconds after PLAYER_ENTERING_WORLD; the server does not
--     answer that early in the login sequence.
--   - The synchronous C_QuestLog functions answer from the last list, which
--     one automatic query fills after login. Quests turned in since
--     (QUEST_TURNED_IN) are added: completions are only ever added.
--   - Lua 5.0: no `...` expression or select().

if not TURTLE_WOW_VERSION then return end
if QueryQuestsCompleted or GetQuestsCompleted then return end -- a real implementation wins

local PREFIX = "TWQUEST"
local QUIET = 0.5
local TIMEOUT = 5
local ENTER_DELAY = 3

local completed = {} -- questID -> true, from the last finished query
local pending        -- questID -> true while a query is being answered
local requested = false
local wantQuery = false
local sentAt, lastPacketAt
local worldReadyAt

local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:RegisterEvent("QUEST_TURNED_IN")

local function Send()
	requested = true
	wantQuery = false
	pending = {}
	sentAt = GetTime()
	lastPacketAt = nil
	SendChatMessage(".queststatus", IsInGuild() and "GUILD" or "SAY")
end

local function Finish()
	for id in pairs(pending) do completed[id] = true end
	pending = nil
	requested = false
	if _classicapi_FireQuestQueryComplete then
		_classicapi_FireQuestQueryComplete()
	end
end

frame:SetScript("OnEvent", function()
	if event == "CHAT_MSG_ADDON" then
		if arg1 ~= PREFIX or not requested then return end
		for id in string.gfind(tostring(arg2 or ""), "%d+") do
			pending[tonumber(id)] = true
		end
		lastPacketAt = GetTime()
	elseif event == "QUEST_TURNED_IN" then
		local id = tonumber(arg1)
		if id then completed[id] = true end
	elseif event == "PLAYER_ENTERING_WORLD" then
		if not worldReadyAt then
			worldReadyAt = GetTime() + ENTER_DELAY
			wantQuery = true -- fill the list the C_QuestLog functions answer from
		end
	end
end)

frame:SetScript("OnUpdate", function()
	local now = GetTime()
	if requested then
		if lastPacketAt then
			if now - lastPacketAt >= QUIET then Finish() end
		elseif now - sentAt >= TIMEOUT then
			requested = false -- no answer: keep the previous list
			pending = nil
		end
	elseif wantQuery and worldReadyAt and now >= worldReadyAt then
		Send()
	end
end)

function QueryQuestsCompleted()
	if requested then return end
	if worldReadyAt and GetTime() >= worldReadyAt then
		Send()
	else
		wantQuery = true
	end
end

function GetQuestsCompleted(t)
	if type(t) ~= "table" then t = {} end
	for id in pairs(completed) do t[id] = true end
	return t
end

C_QuestLog = C_QuestLog or {}

if not C_QuestLog.IsQuestFlaggedCompleted then
	function C_QuestLog.IsQuestFlaggedCompleted(questID)
		questID = tonumber(questID)
		return questID ~= nil and completed[questID] == true
	end
end

if not C_QuestLog.GetAllCompletedQuestIDs then
	function C_QuestLog.GetAllCompletedQuestIDs()
		local ids = {}
		for id in pairs(completed) do table.insert(ids, id) end
		table.sort(ids)
		return ids
	end
end
