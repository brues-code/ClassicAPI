-- ClassicAPI Replay: the map and the camera.

local _, ns = ...;

local CAMERA_ZOOM = 60;

------------------------------------------------------------------------------
-- Camera

local function Aim(x, y, z)
	ns.camera:SetTarget(x, y, z);
	ns.camera:SetZoomDistance(CAMERA_ZOOM);
	ns.camera:SnapAllInterpolatedValues();
end

-- The camera follows the followed unit's moving body (its mount while it
-- rides) as the world's camera follows a unit: at its pivot height, stopped
-- in front of the walls between it and the eye. With no unit, or one out of
-- view, it is a free orbit camera where it was.
local function FollowFocus()
	local u = ns.focus;
	local body = u and u.actor and u.actor:IsShown() and (u.seated and u.mount or u.actor) or nil;
	if ns.camera:GetFollowActor() ~= body then
		ns.camera:SetFollowActor(body);
	end
end
ns.FollowFocus = FollowFocus;

------------------------------------------------------------------------------
-- The map

-- The camera sees as far as the world's camera: the farclip setting, which
-- also sets how much ground the scene keeps loaded.
local function SetFarClip()
	local farClip = tonumber(GetCVar("farclip"));
	if farClip then
		ns.scene:SetCameraFarClip(farClip);
	end
end
ns.SetFarClip = SetFarClip;

-- The middle of where the units are over the replay: the median, not the
-- middle of all of them, since the log can hold a unit far from the rest
-- (one Stormwind log had a player 9,900 yards away, and players on flight
-- paths leave the city), which would put the middle in a mountain.

-- The median of a list, which it leaves as it was.
local function Median(values)
	local sorted = {};
	for i = 1, #values do
		sorted[i] = values[i];
	end
	table.sort(sorted);
	return sorted[math.floor((#sorted + 1) / 2)];
end

local function UnitArea()
	local xs, ys, zs = {}, {}, {};
	for i = 1, #ns.units do
		for _, time in ipairs({ 0, ns.duration / 2, ns.duration }) do
			local x, y, z = C_CombatLogReplay.GetUnitPosition(i, time);
			if x then
				tinsert(xs, x);
				tinsert(ys, y);
				tinsert(zs, z);
			end
		end
	end
	if #xs == 0 then
		return nil;
	end
	return Median(xs), Median(ys), Median(zs);
end

-- Shows the replay's map: the map's WMO when it is one, else its ground,
-- which follows the camera. Aims the camera at the middle of the units.
local function ShowMap(mapID)
	ns.scene:ClearWorldModel();
	ns.scene:ClearWorldTerrain();
	local x, y, z = UnitArea();
	if mapID < 0 then
		ns.SetStatus(REPLAY_NO_MAP);
	elseif not ns.scene:SetWorldModelByMapID(mapID) and not ns.scene:SetWorldTerrain(mapID) then
		ns.SetStatus(string.format(REPLAY_NO_GROUND, mapID));
	end
	if x then
		Aim(x, y, z);
	end
end
ns.ShowMap = ShowMap;
