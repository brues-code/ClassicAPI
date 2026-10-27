-- On-demand, throttled cache warmer (/warmcache). A brute-force scan of the
-- whole ID space floods the client's network buffers and panics in
-- WDataStoreSmallBuffer ("not enough memory") — the engine can't hold that
-- many pending queries. So this warms a *bounded* range, started/stopped
-- manually, at a low fixed rate. Tune WARM_RATE / WARM_INTERVAL if needed,
-- but keep it modest. Note: IDs the server doesn't know (gaps in the range)
-- get no response, so their pending entries linger — warm ranges you expect
-- to be mostly real, not arbitrary millions.
--
--   /warmcache item 1 40000     warm item IDs 1..40000
--   /warmcache <type> <a> <b>   type = item | quest | creature | object | all
--   /warmcache stop             abort an in-progress warm
local WARM_RATE = 20      -- IDs queued per tick
local WARM_INTERVAL = 0.2 -- seconds between ticks  (-> ~20 IDs/sec)

local WARM_FN = {
	item     = function(id) C_Item.RequestLoadItemDataByID(id) end,
	quest    = function(id) C_QuestLog.RequestLoadQuestByID(id) end,
	creature = function(id) C_CreatureInfo.RequestLoadCreatureByID(id) end,
	object   = function(id) C_GameObjectInfo.RequestLoadGameObjectByID(id) end,
	all      = function(id)
					C_QuestLog.RequestLoadQuestByID(id)
					C_Item.RequestLoadItemDataByID(id)
					C_CreatureInfo.RequestLoadCreatureByID(id)
					C_GameObjectInfo.RequestLoadGameObjectByID(id)
			end,
}

local warm = { running = false, cancel = false }

local function warm_msg(s)
	DEFAULT_CHAT_FRAME:AddMessage("|cff66ccff[WarmCache]|r " .. s)
end

local function warm_step(fn, id, stop, lastMsg)
	if warm.cancel then
		warm.running = false
		warm_msg("stopped at " .. (id - 1))
		return
	end
	local n = 0
	while n < WARM_RATE and id <= stop do
		fn(id)
		n = n + 1
		id = id + 1
	end
	if id > stop then
		warm.running = false
		warm_msg("done (through " .. stop .. ")")
	else
		if (id - 1) - lastMsg >= 1000 then
			warm_msg((id - 1) .. " / " .. stop .. " ...")
			lastMsg = id - 1
		end
		C_Timer.After(WARM_INTERVAL, function() warm_step(fn, id, stop, lastMsg) end)
	end
end

SLASH_WARMCACHE1 = "/warmcache"
SlashCmdList["WARMCACHE"] = function(msg)
	local args = {}
	for tok in string.gmatch(msg or "", "%S+") do
		table.insert(args, string.lower(tok))
	end
	local cmd = args[1]

	if cmd == "stop" then
		if warm.running then warm.cancel = true else warm_msg("not running") end
		return
	end
	if warm.running then
		warm_msg("already running - /warmcache stop first")
		return
	end

	local fn = WARM_FN[cmd]
	local a, b = tonumber(args[2]), tonumber(args[3])
	if not fn or not a or not b or a < 1 or b < a then
		warm_msg("usage: /warmcache item|quest|creature|object|all <startID> <stopID>")
		warm_msg("       /warmcache stop")
		return
	end

	warm.running, warm.cancel = true, false
	warm_msg(string.format("warming %s %d..%d (~%d/sec, /warmcache stop to abort)",
		cmd, a, b, math.floor(WARM_RATE / WARM_INTERVAL)))
	warm_step(fn, a, b, a - 1)
end
