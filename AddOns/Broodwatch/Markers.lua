-- Broodwatch markers: an arrow at the scene's edge for each closed chest and
-- each clutch of eggs out of sight.

local _, BW = ...
local TUNING = BW.TUNING
local EGG = BW.EGG

------------------------------------------------------------------------------
-- Each frame a target is projected onto the scene (Project3DPointTo2D). One
-- that lands inside the scene, less MARKER_MARGIN, needs no marker. One
-- outside gets an arrow on the line from the scene's centre to it, where
-- that line meets the margin, turned to point at it, with its icon just
-- inside the arrow. A clutch shows once, at where it was laid, with the
-- seconds left until it hatches.

local markers = {}
local frameNo = 0 -- marks a clutch already shown this frame

local function TakeMarker(i)
	local marker = markers[i];
	if not marker then
		marker = {
			arrow = BW.feedback:CreateTexture(nil, "OVERLAY"),
			icon = BW.feedback:CreateTexture(nil, "ARTWORK"),
			text = BW.feedback:CreateFontString(nil, "OVERLAY"),
		};
		marker.text:SetFontObject(NumberFontNormal);
		marker.arrow:SetTexture(TUNING.MARKER_ARROW);
		marker.arrow:SetWidth(TUNING.MARKER_ARROW_SIZE);
		marker.arrow:SetHeight(TUNING.MARKER_ARROW_SIZE);
		marker.icon:SetWidth(TUNING.MARKER_ICON_SIZE);
		marker.icon:SetHeight(TUNING.MARKER_ICON_SIZE);
		marker.text:SetPoint("CENTER", marker.icon, "CENTER", 0, 0);
		markers[i] = marker;
	end
	return marker;
end

local function HideMarker(marker)
	marker.arrow:Hide();
	marker.icon:Hide();
	marker.text:Hide();
end

-- Shows marker `used + 1` for the point when it is off the scene; returns
-- the count of markers used.
local function Point(used, x, y, z, icon, text)
	local scene = BW.scene;
	local sx, sy = scene:Project3DPointTo2D(x, y, z);
	if not sx then
		return used;
	end
	local scale = scene:GetEffectiveScale();
	local w, h = scene:GetWidth(), scene:GetHeight();
	local m = TUNING.MARKER_MARGIN;
	sx, sy = sx / scale, sy / scale;
	if sx >= m and sx <= w - m and sy >= m and sy <= h - m then
		return used;
	end
	local cx, cy = w / 2, h / 2;
	local dx, dy = sx - cx, sy - cy;
	-- Where the line from the centre leaves the box inset by the margin.
	local t = math.min(dx ~= 0 and (cx - m) / math.abs(dx) or math.huge,
		dy ~= 0 and (cy - m) / math.abs(dy) or math.huge);
	local ex, ey = cx + dx * t, cy + dy * t;
	local d = math.sqrt(dx * dx + dy * dy);
	local ux, uy = dx / d, dy / d;
	used = used + 1;
	local marker = TakeMarker(used);
	marker.arrow:ClearAllPoints();
	marker.arrow:SetPoint("CENTER", scene, "BOTTOMLEFT", ex, ey);
	marker.arrow:SetRotation(math.atan2(dy, dx));
	marker.arrow:Show();
	local inset = (TUNING.MARKER_ARROW_SIZE + TUNING.MARKER_ICON_SIZE) / 2;
	marker.icon:ClearAllPoints();
	marker.icon:SetPoint("CENTER", scene, "BOTTOMLEFT", ex - ux * inset, ey - uy * inset);
	marker.icon:SetTexture(icon);
	marker.icon:Show();
	if text then
		marker.text:SetText(text);
		marker.text:Show();
	else
		marker.text:Hide();
	end
	return used;
end

local function UpdateMarkers()
	frameNo = frameNo + 1;
	local used = 0;
	for _, chest in ipairs(BW.chests) do
		if chest.state == "closed" then
			used = Point(used, chest.x, chest.y, chest.z + 1, chest.kind.icon);
		end
	end
	for _, e in ipairs(BW.eggs) do
		local clutch = e.clutch;
		if e.alive and clutch.shown ~= frameNo then
			clutch.shown = frameNo;
			local left = math.max(0, math.ceil(clutch.hatchAt - BW.run.time));
			used = Point(used, clutch.x, clutch.y, clutch.z + 1, EGG.icon, tostring(left));
		end
	end
	for i = used + 1, table.getn(markers) do
		HideMarker(markers[i]);
	end
end

-- For the files that load after this one.
BW.UpdateMarkers = UpdateMarkers
