-- Backport of templates from retail's
-- Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.lua.
--
-- Differences from the retail source:
--   - 1.12's cursor table has no UI_MOVE_CURSOR or UI_RESIZE_CURSOR, so
--     PanelDragBarMixin:UpdateCursor and PanelResizeButtonMixin's
--     OnEnter / OnLeave do nothing.
--   - 1.12 FrameXML owns UIPanelCloseButton (its own OnClick), so
--     UIPanelCloseButtonDefaultAnchors inherits UIPanelCloseButtonNoScripts
--     and runs UIPanelCloseButton_OnClick itself.
--   - DropdownWithSteppersMixin has no gamepad cursor anchors: this client
--     has no gamepad navigation.
--   - 1.12's Button:IsEnabled returns 1 or 0, and 0 is true in Lua, so
--     DropdownWithSteppersMixin, PanelResizeButtonMixin and
--     UIMenuButtonStretchMixin compare it with 1.

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

local function ButtonFrameTemplate_UpdateRegionAnchor(region, desiredOffsetX)
	-- It's unfortunate that region needs to be checked here, but there's code that uses ButtonFrameTemplate_*Portrait calls
	-- on frames that don't actually inherit from ButtonFrameTemplate.
	if region then
		local point, relativeTo, relativePoint, currentOffsetX, offsetY = region:GetPointByName("TOPLEFT");
		if point then
			region:SetPoint(point, relativeTo, relativePoint, desiredOffsetX, offsetY);
		end
	end
end

local function ButtonFrameTemplate_UpdateAnchors(self, isPortraitMode)
	ButtonFrameTemplate_UpdateRegionAnchor(self.Bg, isPortraitMode and 2 or 7);
	ButtonFrameTemplate_UpdateRegionAnchor(self.Inset, isPortraitMode and 4 or 9);

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

function ButtonFrameTemplateMinimizable_HidePortrait(self)
	self:SetBorder("ButtonFrameTemplateNoPortraitMinimizable");
	self:SetPortraitShown(false);
end

function ButtonFrameTemplateMinimizable_ShowPortrait(self)
	self:SetBorder("PortraitFrameTemplateMinimizable");
	self:SetPortraitShown(true);
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

UIMenuButtonStretchMixin = {}

function UIMenuButtonStretchMixin:SetTextures(texture)
	self.TopLeft:SetTexture(texture);
	self.TopRight:SetTexture(texture);
	self.BottomLeft:SetTexture(texture);
	self.BottomRight:SetTexture(texture);
	self.TopMiddle:SetTexture(texture);
	self.MiddleLeft:SetTexture(texture);
	self.MiddleRight:SetTexture(texture);
	self.BottomMiddle:SetTexture(texture);
	self.MiddleMiddle:SetTexture(texture);
end

function UIMenuButtonStretchMixin:OnMouseDown(button)
	if self:IsEnabled() == 1 then
		self:SetTextures("Interface\\Buttons\\UI-Silver-Button-Down");
		if self.Icon then
			self.Icon:AdjustPointsOffset(1, -1);
		end
	end
end

function UIMenuButtonStretchMixin:OnMouseUp(button)
	if self:IsEnabled() == 1 then
		self:SetTextures("Interface\\Buttons\\UI-Silver-Button-Up");
		if self.Icon then
			self.Icon:AdjustPointsOffset(-1, 1);
		end
	end
end

function UIMenuButtonStretchMixin:OnShow()
	-- we need to reset our textures just in case we were hidden before a mouse up fired
	self:SetTextures("Interface\\Buttons\\UI-Silver-Button-Up");
end

function UIMenuButtonStretchMixin:OnEnable()
	self:SetTextures("Interface\\Buttons\\UI-Silver-Button-Up");
end

function UIMenuButtonStretchMixin:OnEnter()
	if(self.tooltipText ~= nil) then
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
		GameTooltip_SetTitle(GameTooltip, self.tooltipText);
		GameTooltip:Show();
	end
end

function UIMenuButtonStretchMixin:OnLeave()
	if(self.tooltipText ~= nil) then
		GameTooltip:Hide();
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
	if self:IsEnabled() ~= 1 then
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

DropdownWithSteppersMixin = {};

function DropdownWithSteppersMixin:OnLoad()
	self.IncrementButton:SetPoint("LEFT", self.Dropdown, "RIGHT", (self.incrementOffsetX or 4), 0);
	self.IncrementButton:SetScript("OnClick", GenerateClosure(self.OnIncrementClicked, self));

	self.DecrementButton:SetPoint("RIGHT", self.Dropdown, "LEFT", (self.decrementOffsetX or -5), 0);
	self.DecrementButton:SetScript("OnClick", GenerateClosure(self.OnDecrementClicked, self));

	local function OnUpdate(o, previousRadio, nextRadio, selections)
		local canDecrement = previousRadio ~= nil;
		local canIncrement = nextRadio ~= nil;
		self:SetSteppersEnabled(canDecrement, canIncrement);
	end

	self.Dropdown:RegisterCallback(DropdownButtonMixin.Event.OnUpdate, OnUpdate);
end

function DropdownWithSteppersMixin:SetEnabled(enabled)
	self.Dropdown:SetEnabled(enabled);
	self:UpdateSteppers();
end

function DropdownWithSteppersMixin:OnIncrementClicked(button, buttonName, down)
	self:Increment();

	PlaySound(SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_ON);
end

function DropdownWithSteppersMixin:OnDecrementClicked(button, buttonName, down)
	self:Decrement();

	PlaySound(SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_ON);
end

function DropdownWithSteppersMixin:Increment()
	self.Dropdown:Increment();
end

function DropdownWithSteppersMixin:Decrement()
	self.Dropdown:Decrement();
end

function DropdownWithSteppersMixin:HideSteppers()
	self.DecrementButton:Hide();
	self.IncrementButton:Hide();
end

function DropdownWithSteppersMixin:ShowSteppers()
	self.DecrementButton:Show();
	self.IncrementButton:Show();
end

function DropdownWithSteppersMixin:SetSteppersShown(shown)
	self.DecrementButton:SetShown(shown);
	self.IncrementButton:SetShown(shown);
end

function DropdownWithSteppersMixin:SetSteppersEnabled(canDecrement, canIncrement)
	if self.Dropdown:IsEnabled() == 1 then
		self.DecrementButton:SetEnabled(canDecrement);
		self.IncrementButton:SetEnabled(canIncrement);
	else
		self.DecrementButton:SetEnabled(false);
		self.IncrementButton:SetEnabled(false);
	end
end

function DropdownWithSteppersMixin:UpdateSteppers()
	if self.Dropdown:IsEnabled() == 1 then
		local previousRadio, nextRadio, selections = self.Dropdown:CollectSelectionData();
		local canDecrement = previousRadio ~= nil;
		local canIncrement = nextRadio ~= nil;
		self:SetSteppersEnabled(canDecrement, canIncrement);
	else
		self:SetSteppersEnabled(false, false);
	end
end

DropdownWithSteppersAndLabelMixin = CreateFromMixins(DropdownWithSteppersMixin);

function DropdownWithSteppersAndLabelMixin:SetText(text)
	self.Label:SetText(text);
end

-- Allows inheriting buttons to override OnLoad and OnShow
ButtonControllerMixin = {};

function ButtonControllerMixin:OnLoad()
	if self:GetParent().InitButton then
		self:GetParent():InitButton();
	end
end

function ButtonControllerMixin:OnShow()
	if self:GetParent().UpdateButton then
		self:GetParent():UpdateButton();
	end
end

-- Retail keeps this one in SecureUIPanelTemplates.lua. FitToText reads the
-- unscaled width, where retail calls GetWidth.
UIButtonFitToTextBehaviorMixin = {};

function UIButtonFitToTextBehaviorMixin:SetTextToFit(text)
	self:SetText(text);
	self:FitToText();
end

function UIButtonFitToTextBehaviorMixin:FitToText()
	local minWidth = self.fitTextCanWidthDecrease and 0 or self:GetUnscaledWidth();
	local padding = self.fitTextWidthPadding or 0;
	self:SetWidth(math.max(minWidth, self:GetTextWidth() + padding));
end
