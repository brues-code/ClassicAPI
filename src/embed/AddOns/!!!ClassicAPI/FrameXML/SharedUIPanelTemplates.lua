-- Backport of two templates from retail's
-- Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.lua.
--
-- Difference from the retail source: PanelDragBarMixin:UpdateCursor does
-- nothing. 1.12's cursor table has no UI_MOVE_CURSOR.

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
