-- Verbatim backport of retail's Blizzard_SharedXML/InputUtil.lua.

InputUtil = {};

function InputUtil.GetCursorPosition(parent)
	local x, y = GetCursorPosition();
	local scale = parent:GetEffectiveScale();
	return x / scale, y / scale;
end

function InputUtil.GetCursorDelta(parent)
	local x, y = GetCursorDelta();
	local scale = parent:GetEffectiveScale();
	return x / scale, y / scale;
end

-- 3.80.2's Blizzard_UIParent/Shared/UIParent.lua: the cursor's move in
-- UIParent's units, which its OrbitCameraMixin reads.
function GetScaledCursorDelta()
	local uiScale = UIParent:GetEffectiveScale();
	local x, y = GetCursorDelta();
	return x / uiScale, y / uiScale;
end

function InputUtil.AnchorRegionToCursor(region, point)
	local parent = GetAppropriateTopLevelParent();
	local x, y = InputUtil.GetCursorPosition(parent);
	
	-- Accounts for the letterboxing that causes the UI origin to be shifted
	-- closer to the position of the cursor.
	local _, _, _, pointX, _ = parent:GetPointByName("TOPLEFT");
	if pointX then
		x = x - pointX;
	end

	region:ClearAllPoints();
	region:SetPoint(point, parent, "BOTTOMLEFT", x, y);
end
