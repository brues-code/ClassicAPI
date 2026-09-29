-- WorldMapBlobFrame and WorldMapPOIFrame, the 3.3 world map's quest POI frames.
--
-- 3.3.5's WorldMapFrame.xml lays both over WorldMapDetailFrame (1002x668):
-- WorldMapPOIFrame, a plain frame that holds the numbered quest POI buttons
-- (with the `allowBlobTooltip` flag map tooltips turn off while they show),
-- and WorldMapBlobFrame, a QuestPOIFrame - the widget that shades each quest's
-- objective areas from the POI polygons a 3.3 server sends. Code written for
-- 3.3 uses them directly: Zygor Guides Viewer re-parents and hides the blob
-- frame for every combat and clears allowBlobTooltip while its map icons are
-- hovered.
--
-- Vanilla has no QuestPOIFrame widget, and its server sends no POI areas. So
-- the blob frame is a Frame carrying QuestPOIFrame's 14 methods, and they run
-- on what vanilla has, which is no area for any quest: DrawQuestBlob and
-- UpdateQuestPOI draw nothing, UpdateMouseOverTooltip finds no quest under
-- the cursor, GetNumTooltips is 0 - what a 3.3 client does for a quest with
-- no POI data. The fill, border and spline settings are kept on the frame.
--
-- 3.3.5 also gives the blob frame an OnUpdate that hides WorldMapTooltip
-- while the cursor is over the map and no quest area is under it, relying on
-- every other map tooltip to clear allowBlobTooltip first. Vanilla's own map
-- tooltips (party and raid members) don't, and with no areas that OnUpdate
-- could only hide theirs, so it isn't installed.

if WorldMapBlobFrame or WorldMapPOIFrame or not WorldMapFrame or not WorldMapDetailFrame then
	return
end

local function Place(frame)
	frame:SetWidth(1002)
	frame:SetHeight(668)
	frame:SetPoint("TOPLEFT", WorldMapDetailFrame, "TOPLEFT", 0, 0)
end

-- ---------------------------------------------------------------- WorldMapPOIFrame

local poi = CreateFrame("Frame", "WorldMapPOIFrame", WorldMapFrame)
Place(poi)
poi.allowBlobTooltip = true
-- 3.3.5 stacks it 10 levels over WorldMapButton, so POI buttons placed in it
-- sit above the map's own buttons.
if WorldMapButton then
	poi:SetFrameLevel(WorldMapButton:GetFrameLevel() + 10)
end

-- ---------------------------------------------------------------- WorldMapBlobFrame

local blob = CreateFrame("Frame", "WorldMapBlobFrame", WorldMapFrame)
Place(blob)
-- 3.3.5: just above the map art, under WorldMapButton.
blob:SetFrameLevel(WorldMapDetailFrame:GetFrameLevel() + 1)
local texture = blob:CreateTexture("WorldMapBlobFrameTexture", "ARTWORK")
texture:SetAllPoints(blob)

blob.fillAlpha, blob.borderAlpha, blob.borderScalar = 0, 0, 1
blob.smoothing, blob.merging = false, false

function blob:SetFillTexture(path) self.fillTexture = path end
function blob:SetBorderTexture(path) self.borderTexture = path end
function blob:SetFillAlpha(alpha) self.fillAlpha = alpha end
function blob:SetBorderAlpha(alpha) self.borderAlpha = alpha end
function blob:SetBorderScalar(scalar) self.borderScalar = scalar end
function blob:EnableSmoothing(enable) self.smoothing = not not enable end
function blob:EnableMerging(enable) self.merging = not not enable end
function blob:SetMergeThreshold(threshold) self.mergeThreshold = threshold end
function blob:SetNumSplinePoints(points) self.numSplinePoints = points end

-- No quest has POI areas on vanilla: nothing to draw or refresh.
function blob:DrawQuestBlob(questID, draw) end
function blob:UpdateQuestPOI() end

-- The quest whose area is under (x, y), as (questLogIndex, numObjectives) -
-- none.
function blob:UpdateMouseOverTooltip(x, y) return nil end
function blob:GetNumTooltips() return 0 end
function blob:GetTooltipIndex(index) return nil end

-- 3.3.5's WorldMapBlobFrame_OnLoad settings.
blob:SetFillTexture("Interface\\WorldMap\\UI-QuestBlob-Inside")
blob:SetBorderTexture("Interface\\WorldMap\\UI-QuestBlob-Outside")
blob:SetFillAlpha(128)
blob:SetBorderAlpha(192)
blob:SetBorderScalar(1.0)
