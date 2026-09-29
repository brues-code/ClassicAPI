-- IsActiveBattlefieldArena() -> isArena, isRegistered, the 2.0+ question
-- "is the player in an arena match, and is it a rated one".
--
-- Stock 1.12 has no arenas, so there the answer is always nil.
--
-- Turtle-lineage clients (TURTLE_WOW_VERSION, set by their FrameXML) add
-- arena matches over hidden addon messages. The matches run in instances
-- IsInInstance reports as "pvp", like battlegrounds. In a "pvp" instance the
-- client's own scoreboard asks for arena scores (TW_ARENA "C2S_SCOREBOARD"),
-- and only an arena match answers ("S2C_SCOREBOARD"). The scoreboard keeps
-- the answer in WorldStateScoreFrame.arenaData, and clears it when the world
-- states update outside any instance. Its arena view shows exactly while that
-- data is there, so this reads the same state.
--
-- isRegistered is always nil: the arena scoreboard reply carries no rated
-- flag, and the client's arena queue window can only join skirmishes (its
-- rated buttons disable Join).
--
-- Lua 5.0.

if IsActiveBattlefieldArena then return end -- a real implementation wins

function IsActiveBattlefieldArena()
	if not TURTLE_WOW_VERSION then return nil end
	local _, instanceType = IsInInstance()
	if instanceType == "pvp" and WorldStateScoreFrame and WorldStateScoreFrame.arenaData then
		return 1, nil
	end
	return nil
end
