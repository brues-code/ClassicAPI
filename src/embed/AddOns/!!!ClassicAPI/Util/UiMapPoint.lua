-- Backport of retail's Blizzard_ObjectAPI/UiMapPoint.lua. A UiMapPoint is the
-- plain struct the C_Map waypoint calls take and return:
--   { uiMapID = number, position = Vector2DMixin, z = number? }
local CreateVector2D = CreateVector2D

UiMapPoint = {};

function UiMapPoint.CreateFromCoordinates(uiMapID, x, y, z)
	return { uiMapID = uiMapID, position = CreateVector2D(x, y), z = z };
end

function UiMapPoint.CreateFromVector2D(uiMapID, position, z)
	return { uiMapID = uiMapID, position = CreateVector2D(position.x, position.y), z = z };
end

function UiMapPoint.CreateFromVector3D(uiMapID, position)
	return { uiMapID = uiMapID, position = CreateVector2D(position.x, position.y), z = position.z };
end
