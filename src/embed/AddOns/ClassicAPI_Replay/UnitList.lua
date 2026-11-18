-- ClassicAPI Replay: the unit list, the followed unit and the units' names.

local _, ns = ...;

-- A name's color: a player's class color, else these; grey once dead. The
-- unit list fades the name of a unit out of view.
local PLAYER_NAME_COLOR = { 0.4, 0.8, 1 };
local CREATURE_NAME_COLOR = { 1, 0.82, 0 };
local DEAD_NAME_COLOR = { 0.5, 0.5, 0.5 };
local OUT_OF_VIEW_ALPHA = 0.4;

------------------------------------------------------------------------------
-- The unit list

-- A row shows its unit's name in its color (grey once dead), faded while the
-- unit is out of view, and marked while the camera follows it.
local function UpdateUnitRow(row)
	local u = row.unit;
	local color = u.dead and DEAD_NAME_COLOR or u.nameColor;
	row.Text:SetTextColor(color[1], color[2], color[3]);
	row:SetAlpha(u.inView and 1 or OUT_OF_VIEW_ALPHA);
	if u.mark then
		SetRaidTargetIconTexture(row.Mark, u.mark);
		row.Mark:Show();
	else
		row.Mark:Hide();
	end
	if u == ns.focus then
		row.Selected:Show();
	else
		row.Selected:Hide();
	end
end

local function UpdateUnitList()
	ns.frame.UnitList.ScrollBox:ForEachFrame(UpdateUnitRow);
end
ns.UpdateUnitList = UpdateUnitList;

local function SetFocus(u)
	if ns.focus and ns.focus.actor then
		ns.focus.actor:SetHighlight("target", false);
	end
	ns.focus = u;
	if ns.focus and ns.focus.actor then
		ns.focus.actor:SetHighlight("target", true);
	end
	ns.FollowFocus();
	UpdateUnitList();
	ns.UpdateUnitFrame(ns.now);
	if ns.focus then
		ns.frame.UnitList.ScrollBox:ScrollToElementData(ns.focus, ScrollBoxConstants.AlignNearest);
	end
end
ns.SetFocus = SetFocus;

local function OnUnitRowClick(row)
	SetFocus(row.unit);
end

local function InitUnitRow(row, u)
	row.unit = u;
	row.Text:SetText(u.name ~= "" and u.name or UNKNOWN);
	row:SetScript("OnClick", OnUnitRowClick);
	UpdateUnitRow(row);
end
ns.InitUnitRow = InitUnitRow;

-- Players first, then creatures, each by name; units without a name last.
local function UnitOrder(a, b)
	if (a.name == "") ~= (b.name == "") then
		return b.name == "";
	end
	if not a.isPlayer ~= not b.isPlayer then
		return not b.isPlayer;
	end
	if a.name ~= b.name then
		return a.name < b.name;
	end
	return a.index < b.index;
end

local function FillUnitList()
	local list = {};
	for i = 1, #ns.units do
		list[i] = ns.units[i];
	end
	table.sort(list, UnitOrder);
	ns.frame.UnitList.ScrollBox:SetDataProvider(CreateDataProvider(list), ScrollBoxConstants.RetainScrollPosition);
	ns.unitListChanged = false;
end
ns.FillUnitList = FillUnitList;

-- A creature the log does not name and the client has not cached is asked
-- for; NameCreatures names its units when it arrives.
local function RequestName(unit)
	if unit.name == "" and unit.creatureID then
		C_CreatureInfo.RequestLoadCreatureByID(unit.creatureID);
	end
end
ns.RequestName = RequestName;

local function NameCreatures(creatureID)
	local info = C_CreatureInfo.GetCreatureInfoByID(creatureID);
	if not info or not info.name or info.name == "" then
		return;
	end
	for i = 1, #ns.units do
		local u = ns.units[i];
		if u.creatureID == creatureID and u.name == "" then
			u.name = info.name;
			u.named = nil;
			ns.unitListChanged = true;
		end
	end
end
ns.NameCreatures = NameCreatures;

-- A player's class color, else the name color of its kind.
local function NameColor(isPlayer, appearances)
	local appearance = appearances and appearances[1];
	local info = appearance and C_CreatureInfo.GetClassInfo(appearance.classID);
	local color = info and RAID_CLASS_COLORS[info.classFile];
	if color then
		return { color.r, color.g, color.b };
	end
	return isPlayer and PLAYER_NAME_COLOR or CREATURE_NAME_COLOR;
end
ns.NameColor = NameColor;
