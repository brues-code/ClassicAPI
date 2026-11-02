------------------------------------------------------------------------------
-- DebugTools.lua  (1.12 / Lua 5.0 backport of Blizzard_DebugTools 3.3.5)
--
-- Major changes from the 3.3.5 source:
--  * Script handlers take explicit (this, event, arg1, ...) parameters
--    from XML inline scripts.
--  * /fstack [showHidden [showRegions]] is the modern (10.x+)
--    FrameStackTooltip on ClassicAPI's SetFrameStack: Alt moves the
--    highlight, Ctrl inspects it, Shift shows texture info (see the
--    FrameStackTooltip section).
--
-- The event log (/etrace) is retail's Blizzard_EventTrace, in its own
-- addon, ClassicAPI_EventTrace. The table inspector (/tinspect, Ctrl in
-- /fstack) is retail's
-- Blizzard_TableInspector; see TableInspector.lua. The profiler (/profiler)
-- is in Profiler/, and adds its owner and CPU lines to /fstack.
------------------------------------------------------------------------------

local _, ns = ...;

------------------------------------------------------------------------------
-- FrameStackTooltip
------------------------------------------------------------------------------

function DebugTooltip_OnLoad(self)
    self:SetFrameLevel(self:GetFrameLevel() + 2);
    if (TOOLTIP_DEFAULT_COLOR and self.SetBackdropBorderColor) then
        self:SetBackdropBorderColor(TOOLTIP_DEFAULT_COLOR.r,
                                    TOOLTIP_DEFAULT_COLOR.g,
                                    TOOLTIP_DEFAULT_COLOR.b);
    end
    if (TOOLTIP_DEFAULT_BACKGROUND_COLOR and self.SetBackdropColor) then
        self:SetBackdropColor(TOOLTIP_DEFAULT_BACKGROUND_COLOR.r,
                              TOOLTIP_DEFAULT_BACKGROUND_COLOR.g,
                              TOOLTIP_DEFAULT_BACKGROUND_COLOR.b);
    end
end

-- The modern (10.x+) FrameStackTooltip on ClassicAPI's
-- `SetFrameStack(showHidden, showRegions, highlightIndexChanged)`, which
-- returns the highlighted entry and lists its anchors and source:
--  * Left Alt / Right Alt move the highlight down / up the list;
--  * Ctrl (on release) opens the highlighted entry in the TableInspector;
--  * Shift toggles texture information for the highlighted entry;
--  * `fsobj` is the highlighted entry after an Alt press.
-- The modern tooltip polls a KeyCommand table from OnUpdate; here the
-- MODIFIER_STATE_CHANGED(key, down) event carries the same per-side state.

FRAMESTACK_UPDATE_TIME = .1;

local function _WrapColor(text, colorHex)
    return "|c" .. colorHex .. text .. "|r";
end

-- Retail's frame highlight: a green cover over a frame, and with showAnchors
-- a yellow cover over each frame it is anchored to. The table inspector's
-- Highlight checkbox uses it.
local function AnchorHighlight(frame, highlight, relativePoint)
	highlight:ClearAllPoints();
	highlight:SetAllPoints(frame);
	highlight:Show();

	if highlight.AnchorPoint then
		if relativePoint then
			highlight.AnchorPoint:ClearAllPoints();
			highlight.AnchorPoint:SetPoint("CENTER", highlight, relativePoint);
			highlight.AnchorPoint:Show();
		else
			highlight.AnchorPoint:Hide();
		end
	end
end

AnchorHighlightMixin = {};

function AnchorHighlightMixin:RetrieveAnchorHighlight(pointIndex)
	if not self.AnchorHighlights then
		CreateFrame("FRAME", "FrameStackAnchorHighlightTemplate1", self, "FrameStackAnchorHighlightTemplate");
	end

	while pointIndex > #self.AnchorHighlights do
		CreateFrame("FRAME", "FrameStackAnchorHighlightTemplate"..(#self.AnchorHighlights + 1), self, "FrameStackAnchorHighlightTemplate");
	end

	return self.AnchorHighlights[pointIndex];
end

function AnchorHighlightMixin:HighlightFrame(baseFrame, showAnchors)
	AnchorHighlight(baseFrame, self);

	local pointIndex = 1;
	if (showAnchors and baseFrame.GetNumPoints and baseFrame.GetPoint) then -- TODO: Fix for lines
		while pointIndex <= baseFrame:GetNumPoints() do
			local _, anchorFrame, anchorRelativePoint = baseFrame:GetPoint(pointIndex);
			AnchorHighlight(anchorFrame, self:RetrieveAnchorHighlight(pointIndex), anchorRelativePoint);
			pointIndex = pointIndex + 1;
		end
	end

	while self.AnchorHighlights and self.AnchorHighlights[pointIndex] do
		self.AnchorHighlights[pointIndex]:Hide();
		pointIndex = pointIndex + 1;
	end
end

function FrameStackTooltip_OnLoad(self)
    DebugTooltip_OnLoad(self);
    self.nextUpdate = 0;
    self.highlightIndexChanged = 0;
    self:RegisterEvent("MODIFIER_STATE_CHANGED");
end

function FrameStackTooltip_OnEvent(self, event, key, down)
    if (event ~= "MODIFIER_STATE_CHANGED" or not self:IsShown()) then
        return;
    end
    if (key == "LALT") then
        if (down == 1) then FrameStackTooltip_ChangeHighlight(self, 1); end
    elseif (key == "RALT") then
        if (down == 1) then FrameStackTooltip_ChangeHighlight(self, -1); end
    elseif (key == "LCTRL" or key == "RCTRL") then
        -- On release, as the modern RUN_ON_UP command.
        if (down == 0) then FrameStackTooltip_InspectTable(self); end
    elseif (key == "LSHIFT" or key == "RSHIFT") then
        if (down == 1) then FrameStackTooltip_ToggleTextureInformation(self); end
    end
end

function FrameStackTooltip_ChangeHighlight(self, direction)
    self.highlightIndexChanged = direction;
    self.shouldSetFSObj = true;
end

function FrameStackTooltip_InspectTable(self)
    if (self.highlightFrame) then
        TableAttributeDisplay:InspectTable(self.highlightFrame);
        TableAttributeDisplay:Show();
    end
end

function FrameStackTooltip_ToggleTextureInformation(self)
    self.showTextureInfo = not self.showTextureInfo;
end

-- Blizzard_TextureInfoGenerator on 1.12's texture methods: the file a
-- texture shows and its coordinates when they are not the whole image.
local function _FormatTextureCoordinates(ulX, ulY, blX, blY, urX, urY, brX, brY)
    if (type(brY) ~= "number") then
        return "invalid coordinates";
    end
    if (ulX == 0 and ulY == 0 and blX == 0 and blY == 1 and
        urX == 1 and urY == 0 and brX == 1 and brY == 1) then
        return "";
    end
    return _WrapColor(string.format("UL:(%.2f, %.2f), BL:(%.2f, %.2f), UR:(%.2f, %.2f), BR:(%.2f, %.2f)",
                                    ulX, ulY, blX, blY, urX, urY, brX, brY), "ff00ffff");
end

local function _FormatTextureInfo(region)
    if (not region:IsObjectType("Texture") or not region:IsMouseOver()) then
        return nil;
    end
    local file = region:GetTexture();
    if (file == nil) then
        return nil;
    end
    return string.format("%s : %s: %s %s", region:GetDebugName(), _WrapColor("File", "ffff0000"),
                         tostring(file), _FormatTextureCoordinates(region:GetTexCoord()));
end

function FrameStackTooltip_CheckFormatTextureInfo(obj)
    local regions;
    if (obj:IsObjectType("Frame")) then
        regions = { obj:GetRegions() };
    else
        regions = { obj };
    end
    local info = {};
    for _, region in ipairs(regions) do
        local line = _FormatTextureInfo(region);
        if (line) then
            table.insert(info, line);
        end
    end
    if (table.getn(info) > 0) then
        return table.concat(info, "\n");
    end
end

function FrameStackTooltip_OnTooltipSetFrameStack(self, highlightFrame)
    self.highlightFrame = highlightFrame;

    if (self.highlightFrame and self.showTextureInfo) then
        local textureInfo = FrameStackTooltip_CheckFormatTextureInfo(self.highlightFrame);
        if (textureInfo) then
            self:AddLine(textureInfo);
        end
    end

    if (self.shouldSetFSObj) then
        fsobj = self.highlightFrame;
        self.shouldSetFSObj = nil;
    end

    if (fsobj) then
        self:AddLine(string.format("\nfsobj = %s", fsobj:GetDebugName()));
    end

    ns.AddFrameStackLines(self, self.highlightFrame);
end

function FrameStackTooltip_Show(self, showHidden, showRegions)
    self:SetOwner(UIParent, "ANCHOR_NONE");
    self:SetPoint("BOTTOMRIGHT", UIParent, "BOTTOMRIGHT", -32, 96);
    self.default = 1;
    self.showRegions = showRegions;
    self.showHidden = showHidden;
    self:SetFrameStack(showHidden, showRegions);
end

function FrameStackTooltip_Hide(self)
    self:Hide();
end

function FrameStackTooltip_Toggle(showHidden, showRegions)
    local tooltip = FrameStackTooltip;
    if (tooltip:IsVisible()) then
        FrameStackTooltip_Hide(tooltip);
    else
        FrameStackTooltip_Show(tooltip, showHidden, showRegions);
    end
end

function FrameStackTooltip_OnUpdate(self, elapsed)
    local now = GetTime();
    if (now >= self.nextUpdate or self.highlightIndexChanged ~= 0) then
        self.nextUpdate = now + FRAMESTACK_UPDATE_TIME;
        self.highlightFrame = self:SetFrameStack(self.showHidden, self.showRegions, self.highlightIndexChanged);
        self.highlightIndexChanged = 0;
    end
end

function FrameStackTooltip_OnShow(self)
    local parent = self:GetParent() or UIParent;
    local ps = parent:GetEffectiveScale();
    local px, py = parent:GetCenter();
    if (not px or not py) then return; end
    px, py = px * ps, py * ps;
    local x, y = GetCursorPosition();
    self:ClearAllPoints();
    if (x > px) then
        if (y > py) then
            self:SetPoint("BOTTOMLEFT", parent, "BOTTOMLEFT", 20, 20);
        else
            self:SetPoint("TOPLEFT", parent, "TOPLEFT", 20, -20);
        end
    else
        if (y > py) then
            self:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", -20, 20);
        else
            self:SetPoint("TOPRIGHT", parent, "TOPRIGHT", -20, -20);
        end
    end
end

FrameStackTooltip_OnEnter = FrameStackTooltip_OnShow;

-- /framestack [showHidden [showRegions]], as the modern slash
-- command reads it: each word through StringToBoolean, with the defaults
-- false, true. The modern third word, showAnchors, drives its highlight
-- frames (the green cover), which came after 4.3.4 and are not ported: the
-- tooltip's "-->" mark, ANCHORS and SOURCE lines carry that information.
function FrameStackTooltip_HandleSlashCmd(msg)
    local showHiddenArg, showRegionsArg;
    showHiddenArg, msg = string.match(msg or "", "^%s*(%S+)(.*)$");
    showRegionsArg, msg = string.match(msg or "", "^%s*(%S+)(.*)$");

    local showHidden = StringToBoolean(showHiddenArg or "", false);
    local showRegions = StringToBoolean(showRegionsArg or "", true);

    FrameStackTooltip_Toggle(showHidden, showRegions);
end
