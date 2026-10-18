-- The closed world map follows the player's zone, as the 3.x UI keeps it.
--
-- GetPlayerMapPosition("player") answers for whichever map the world map is
-- set to, open or not, and returns 0,0 when the player isn't on that map.
-- The 3.x FrameXML puts the map back on the player's zone whenever it is out
-- of view: WorldMapFrame_OnHide ends with SetMapToCurrentZone(), and
-- WatchFrame calls it on PLAYER_ENTERING_WORLD and, while the map is closed,
-- on ZONE_CHANGED_NEW_AREA. Vanilla has no WatchFrame and doesn't reset the
-- map when it closes, so after the player changes zones - or browses another
-- zone and closes the map - the closed map stays on that other zone, and code
-- written for 3.x that reads the player's position without setting the map
-- first gets 0,0 (Zygor Guides Viewer's travel steps never complete).
--
-- WatchFrame skips the zone-change reset while "Show quest objectives on the
-- map" is off; vanilla has no such option (the reset fed the tracker's POIs),
-- so here it always runs.

local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("ZONE_CHANGED_NEW_AREA")
frame:SetScript("OnEvent", function(self, ev)
	ev = ev or event
	if ev == "ZONE_CHANGED_NEW_AREA" and WorldMapFrame:IsShown() then
		return
	end
	SetMapToCurrentZone()
end)

WorldMapFrame:HookScript("OnHide", function()
	SetMapToCurrentZone()
end)
