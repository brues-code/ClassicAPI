-- Backport of templates from retail's
-- Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.lua.
--
-- Differences from the retail source:
--   - 1.12's cursor table has no UI_MOVE_CURSOR or UI_RESIZE_CURSOR, so
--     PanelDragBarMixin:UpdateCursor and PanelResizeButtonMixin's
--     OnEnter / OnLeave do nothing.
--   - The ButtonFrameTemplate border is the same with and without the
--     portrait, so ButtonFrameTemplate_HidePortrait / ShowPortrait move only
--     the title; the background and inset stay inside the border.
--   - 1.12 FrameXML owns UIPanelCloseButton (its own OnClick), so
--     UIPanelCloseButtonDefaultAnchors inherits UIPanelCloseButtonNoScripts
--     and runs UIPanelCloseButton_OnClick itself.

function TruncatedTooltipScript_OnEnter(self)
	local text = self.truncatedTooltipScriptText or self.Text;
	if text:IsTruncated() then
		local tooltip = GetAppropriateTooltip();
		tooltip:SetOwner(self, "ANCHOR_RIGHT");
		GameTooltip_AddNormalLine(tooltip, text:GetText());
		tooltip:Show();
	end
end

function TruncatedTooltipScript_OnLeave(self)
	local tooltip = GetAppropriateTooltip();
	if tooltip:GetOwner() == self then
		tooltip:Hide();
	end
end

PanelDragBarMixin = {};

function PanelDragBarMixin:OnLoad()
	self:RegisterForDrag("LeftButton");
	self:SetTarget(self:GetParent());
	self.suspendDrag = false;
end

function PanelDragBarMixin:SetDragSuspended(suspendDrag)
	self.suspendDrag = suspendDrag;
end

function PanelDragBarMixin:Init(target)
	self:SetTarget(target);
end

function PanelDragBarMixin:SetTarget(target)
	self.target = target;
	self.isMovingTarget = false;
end

function PanelDragBarMixin:OnDragStart()
	if self.suspendDrag then
		return;
	end

	local target = self.target;

	local continueDragStart = true;
	if target.onDragStartCallback then
		continueDragStart = target.onDragStartCallback(self);
	end

	if self.onDragStartCallback then
		continueDragStart = self.onDragStartCallback(self);
	end

	if continueDragStart then
		self.isMovingTarget = true;
		target:StartMoving();
	end

	self:UpdateCursor();
end

function PanelDragBarMixin:OnDragStop()
	if self.suspendDrag then
		return;
	end

	local target = self.target;

	local continueDragStop = true;
	if target.onDragStopCallback then
		continueDragStop = target.onDragStopCallback(self);
	end

	if self.onDragStopCallback then
		continueDragStop = self.onDragStopCallback(self);
	end

	if continueDragStop then
		self.isMovingTarget = false;
		target:StopMovingOrSizing();
	end

	self:UpdateCursor();
end

function PanelDragBarMixin:SetOnDragStartCallback(onDragStartCallback)
	self.onDragStartCallback = onDragStartCallback;
end

function PanelDragBarMixin:SetOnDragStopCallback(onDragStopCallback)
	self.onDragStopCallback = onDragStopCallback;
end

function PanelDragBarMixin:OnEnter()
	if self.showCursorOnHover then
		self:UpdateCursor();
	end
end

function PanelDragBarMixin:OnLeave()
	if self.showCursorOnHover then
		self:UpdateCursor();
	end
end

function PanelDragBarMixin:UpdateCursor()
end

local function ButtonFrameTemplate_UpdateAnchors(self, isPortraitMode)
	if self.TitleContainer then
		self.TitleContainer:SetPoint("TOPLEFT", self, "TOPLEFT", isPortraitMode and 58 or 0, -1);
		self.TitleContainer:SetPoint("TOPRIGHT", self, "TOPRIGHT", isPortraitMode and -24 or 0, -1);
	end
end

function ButtonFrameTemplate_HidePortrait(self)
	self:SetBorder("ButtonFrameTemplateNoPortrait");
	self:SetPortraitShown(false);

	local isPortraitMode = false;
	ButtonFrameTemplate_UpdateAnchors(self, isPortraitMode);
end

function ButtonFrameTemplate_ShowPortrait(self)
	self:SetBorder("PortraitFrameTemplate");
	self:SetPortraitShown(true);

	local isPortraitMode = true;
	ButtonFrameTemplate_UpdateAnchors(self, isPortraitMode);
end

UIPanelCloseButtonDefaultAnchorsMixin = {};

function UIPanelCloseButtonDefaultAnchorsMixin:OnLoad()
	self:SetSize(24, 24);
	self:ClearAllPoints();
	self:SetPoint("TOPRIGHT", 1, 0);
end

-- A bit ugly, we want the talent frame to display a dialog box in certain conditions.
function UIPanelCloseButton_OnClick(self)
	local parent = self:GetParent();
	if parent then
		local continueHide = true;
		if parent.onCloseCallback then
			continueHide = parent.onCloseCallback(self);
		end

		if continueHide then
			HideUIPanel(parent);
		end
	end
end

PanelResizeButtonMixin = {};

function PanelResizeButtonMixin:Init(target, minWidth, minHeight, maxWidth, maxHeight, rotationDegrees)
	self.target = target;
	self.minWidth = minWidth;
	self.minHeight = minHeight;
	self.maxWidth = maxWidth;
	self.maxHeight = maxHeight;

	local originalTargetOnSizeChanged = target:GetScript("OnSizeChanged") or nop;
	target:SetScript("OnSizeChanged", function(target, width, height)
		originalTargetOnSizeChanged(target, width, height);

		-- Size changes should not be enforced here unless this button caused the change.
		if not self.isActive then
			return;
		end

		local newWidth = width;
		if width < self.minWidth then
			newWidth = self.minWidth;
			target:SetWidth(newWidth);
		elseif self.maxWidth and width > self.maxWidth then
			newWidth = self.maxWidth;
			target:SetWidth(newWidth);
		end

		local newHeight = height;
		if height < self.minHeight then
			newHeight = self.minHeight;
			target:SetHeight(newHeight);
		elseif self.maxHeight and height > self.maxHeight then
			newHeight = self.maxHeight;
			target:SetHeight(newHeight);
		end

		if self.resizeCallback then
			self.resizeCallback(self, newWidth, newHeight, self.isActive);
		end
	end);

	if rotationDegrees ~= nil then
		self:SetRotationDegrees(rotationDegrees);
	end
end

function PanelResizeButtonMixin:OnEnter()
end

function PanelResizeButtonMixin:OnLeave()
end

function PanelResizeButtonMixin:OnMouseDown()
	if not self:IsEnabled() then
		return;
	end

	self.isActive = true;

	local target = self.target;
	if not target then
		return;
	end

	local continueResizeStart = true;
	if target.onResizeStartCallback then
		continueResizeStart = target.onResizeStartCallback(self);
	end

	if continueResizeStart then
		local alwaysStartFromMouse = true;
		self.target:StartSizing("BOTTOMRIGHT", alwaysStartFromMouse);
	end
end

function PanelResizeButtonMixin:OnMouseUp()
	self.isActive = false;

	local target = self.target;
	if not target then
		return;
	end

	local continueResizeStop = true;
	if target.onResizeStopCallback then
		continueResizeStop = target.onResizeStopCallback(self);
	end

	if continueResizeStop then
		target:StopMovingOrSizing();
	end

	if self.resizeStoppedCallback ~= nil then
		self.resizeStoppedCallback(self.target);
	end
end

function PanelResizeButtonMixin:IsActive()
	return not not self.isActive;
end

function PanelResizeButtonMixin:SetMinWidth(minWidth)
	self.minWidth = minWidth;
end

function PanelResizeButtonMixin:SetMinHeight(minHeight)
	self.minHeight = minHeight;
end

function PanelResizeButtonMixin:SetMaxWidth(maxWidth)
	self.maxWidth = maxWidth;
end

function PanelResizeButtonMixin:SetMaxHeight(maxHeight)
	self.maxHeight = maxHeight;
end

function PanelResizeButtonMixin:SetRotationDegrees(rotationDegrees)
	local rotationRadians = (rotationDegrees / 180) * math.pi;
	self:SetRotationRadians(rotationRadians);
end

function PanelResizeButtonMixin:SetRotationRadians(rotationRadians)
	local childRegions = { self:GetRegions() };
	for i, child in ipairs(childRegions) do
		if child.SetRotation ~= nil then
			child:SetRotation(rotationRadians);
		end
	end
end

function PanelResizeButtonMixin:SetOnResizeStoppedCallback(resizeStoppedCallback)
	self.resizeStoppedCallback = resizeStoppedCallback;
end

function PanelResizeButtonMixin:SetOnResizeCallback(resizeCallback)
	self.resizeCallback = resizeCallback;
end
