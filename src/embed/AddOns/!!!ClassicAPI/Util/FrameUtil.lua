-- Backport of retail Blizzard_SharedXMLBase/FrameUtil.lua to vanilla 1.12 / Lua 5.0.
--
-- Implementation differences from the retail source:
--   - Not ported: the fade and flash functions (UIFrameFade*, UIFrameFlash*,
--     FADEFRAMES, FLASHFRAMES). 1.12's FrameXML defines them in UIParent.lua,
--     and UIParent's OnUpdate drives them.
--   - Not ported: FrameUtil.RegisterFrameForEventCallbacks,
--     UnregisterFrameForEventCallbacks and RegisterFrameForEventCallbackFields
--     (no frame:RegisterEventCallback), and ApplyDefaultScale (no
--     GetDefaultScale).
--   - FrameUtil.SetParentMaintainRenderLayering does not carry the frame's
--     window over. 1.12 has one window and no GetWindow/SetWindow.
--   - IsScrollingMessageFrame, at the bottom, is from retail's
--     Blizzard_SharedXML/ScrollingMessageFrame.lua.

FrameUtil = {};

function FrameUtil.RegisterUpdateFunction(frame, frequencySeconds, func)
	-- Prevents the OnUpdate handler from running the same frame it was
	-- removed.
	frame.canUpdate = true;

	local elapsed = frequencySeconds;
	frame:SetScript("OnUpdate", function(self, dt)
		if self.canUpdate then
			elapsed = elapsed - dt;
			if elapsed <= 0 then
				elapsed = frequencySeconds;
				func(frame, dt);
			end
		end
	end);
end

function FrameUtil.UnregisterUpdateFunction(frame)
	frame.canUpdate = false;
	frame:SetScript("OnUpdate", nil);
end

function FrameUtil.RegisterFrameForEvents(frame, events)
	for i, event in ipairs(events) do
		frame:RegisterEvent(event);
	end
end

function FrameUtil.UnregisterFrameForEvents(frame, events)
	for i, event in ipairs(events) do
		frame:UnregisterEvent(event);
	end
end

function FrameUtil.RegisterFrameForUnitEvents(frame, events, ...)
	for i, event in ipairs(events) do
		frame:RegisterUnitEvent(event, ...);
	end
end

function FrameUtil.DialogStyleGlobalMouseDown(frame, buttonName, ...)
	local mouseFoci = GetMouseFoci();
	if DoesAncestryIncludeAny(frame, mouseFoci) then
		return;
	end

	for i = 1, select("#", ...) do
		local alternateFrame = select(i, ...);
		if DoesAncestryIncludeAny(alternateFrame, mouseFoci) then
			return;
		end
	end

	frame:Hide();
end

local StandardScriptHandlerSet = {
	OnLoad = true,
	OnShow = true,
	OnHide = true,
	OnEvent = true,
	OnEnter = true,
	OnLeave = true,
	OnClick = true,
	OnDragStart = true,
	OnReceiveDrag = true,

	-- Other scripts can/should be added here as needed.

	-- Many OnUpdates are set dynamically. Leave this off for now.
	-- OnUpdate = false,
};

-- ... is a list of tables to mixin.
function FrameUtil.SpecializeFrameWithMixins(frame, ...)
	Mixin(frame, ...);
	FrameUtil.ReflectStandardScriptHandlers(frame);
end

function FrameUtil.ReflectStandardScriptHandlers(frame)
	for scriptHandlerKey, shouldBeSet in pairs(StandardScriptHandlerSet) do
		local scriptHandler = frame[scriptHandlerKey];
		if scriptHandler ~= nil then
			frame:SetScript(scriptHandlerKey, scriptHandler);
		end
	end

	if frame.OnLoad then
		frame:OnLoad();
	end

	if frame.OnShow and frame:IsVisible() then
		frame:OnShow();
	end
end

-- This doesn't strictly speaking require a frame, but that's the likely usage of it so I'm leaving it here for now.
function FrameUtil.RegisterForVariablesLoaded(frame, loadMethod)
	if frame.savedVariablesEventRegistered then
		error("Cannot re-register for variables loaded.");
		return;
	end

	frame.savedVariablesEventRegistered = true;

	local function VariablesLoadedCallback(callbackSelf)
		loadMethod(frame);
	end

	EventUtil.ContinueOnVariablesLoaded(VariablesLoadedCallback);
end

-- Only expected to return differently in glues and in-game.
function FrameUtil.GetRootParent(frame)
	local parent = frame:GetParent();
	while parent do
		local nextParent = parent:GetParent();
		if not nextParent then
			break;
		end
		parent = nextParent;
	end
	return parent;
end

function FrameUtil.CreateFrame(name, parent, template)
	-- NOTE: This use of the template type is not strictly correct, but should mostly work.
	local templateInfo = C_XMLUtil.GetTemplateInfo(template);
	local frameType = templateInfo and templateInfo.type or "Frame";
	return CreateFrame(frameType, name, parent, template);
end

function FrameUtil.UpdateScaleForFit(frame, extraWidth, extraHeight)
	extraWidth = extraWidth or 0;
	extraHeight = extraHeight or 0;

	FrameUtil.UpdateScaleForFitSpecific(frame, frame:GetWidth() + extraWidth, frame:GetHeight() + extraHeight);
end

function FrameUtil.UpdateScaleForFitSpecific(frame, specificWidth, specificHeight)
	frame:SetScale(1);

	local topLevelParent = GetAppropriateTopLevelParent();
	local horizRatio = topLevelParent:GetWidth() / (specificWidth or frame:GetWidth());
	local vertRatio = topLevelParent:GetHeight() / (specificHeight or frame:GetHeight());

	if (horizRatio < 1 or vertRatio < 1) then
		frame:SetScale(min(horizRatio, vertRatio));
	end
end

function DoesAncestryInclude(ancestry, frame)
	if ancestry then
		local currentFrame = frame;
		while currentFrame do
			if currentFrame == ancestry then
				return true;
			end
			currentFrame = currentFrame:GetParent();
		end
	end
	return false;
end

function DoesAncestryIncludeAny(ancestry, frames)
	for _, frame in ipairs(frames) do
		if DoesAncestryInclude(ancestry, frame) then
			return true;
		end
	end
	return false;
end

function GetUnscaledFrameRect(frame, scale)
	local frameLeft, frameBottom, frameWidth, frameHeight = frame:GetScaledRect();
	if frameLeft == nil then
		-- Defaulted returned for diagnosing invalid rects in layout frames.
		local defaulted = true;
		return 1, 1, 1, 1, defaulted;
	end

	return frameLeft / scale, frameBottom / scale, frameWidth / scale, frameHeight / scale;
end

function GetScaledCenter(frame)
	local x, y = frame:GetCenter();
	local effectiveScale = frame:GetEffectiveScale();
	if x == nil or y == nil or effectiveScale == nil then
		-- Defaulted returned for diagnosing invalid rects in layout frames.
		local defaulted = true;
		return 0, 0, defaulted;
	end

	return x * effectiveScale, y * effectiveScale;
end

function FitToParent(parent, frame)
	local horizRatio = parent:GetWidth() / frame:GetWidth();
	local vertRatio = parent:GetHeight() / frame:GetHeight();

	if ( horizRatio < 1 or vertRatio < 1 ) then
		frame:SetScale(min(horizRatio, vertRatio));
		frame:SetPoint("CENTER", 0, 0);
	end

end

local alternateTopLevelParent;
function SetAlternateTopLevelParent(parent)
	alternateTopLevelParent = parent;
	EventRegistry:TriggerEvent("UI.AlternateTopLevelParentChanged", parent);
end

function ClearAlternateTopLevelParent()
	alternateTopLevelParent = nil;
	EventRegistry:TriggerEvent("UI.AlternateTopLevelParentChanged");
end

-- optionalExcludedParent: Frame to avoid returning if it is currently the alternateTopLevelParent; Useful for frames that might currently be the alternate but need the root top for scaling
function GetAppropriateTopLevelParent(optionalExcludedParent)
	if alternateTopLevelParent and alternateTopLevelParent:IsShown() and (not optionalExcludedParent or alternateTopLevelParent ~= optionalExcludedParent) then
		return alternateTopLevelParent;
	end

	return UIParent or GlueParent;
end

function SetAppropriateTopLevelParent(frame)
	local parent = GetAppropriateTopLevelParent();
	if parent then
		frame:SetParent(parent);
	end
end
function GetAppropriateTooltip()
	return UIParent and GameTooltip or GlueTooltip;
end

-- Sets a frame parent, but retains old frame strata and frame level
function FrameUtil.SetParentMaintainRenderLayering(frame, parent)
	local origStrata = frame:GetFrameStrata();
	local origFrameLevel = frame:GetFrameLevel();

	frame:SetParent(parent);
	frame:SetFrameStrata(origStrata);
	frame:SetFrameLevel(origFrameLevel);
end

-- Sets a frame to the appropriate top level parent.
function FrameUtil.RegisterForTopLevelParentChanged(frame)
	EventRegistry:RegisterCallback("UI.AlternateTopLevelParentChanged", function (callbackFrame)
		FrameUtil.UpdateTopLevelParent(frame);
	end, frame);
end

-- Sets a frame to the appropriate top level parent.
function FrameUtil.UpdateTopLevelParent(frame)
	local oldParent = frame:GetParent();
	local newParent = GetAppropriateTopLevelParent();
	if newParent and newParent ~= oldParent then
		FrameUtil.SetParentMaintainRenderLayering(frame, newParent);
	end
end

-- AnimTransitionMixin - A configurable mixin for animated transitions when showing and hiding.
-- Alpha and/or scale could be added in the future if needed.

-- Mixes in and initializes the AnimTransitionMixin to a frame to give it animated show/hide capabilities.
-- config: see DEFAULT_ANIM_TRANSITION_CONFIG for parameters.
function FrameUtil.InitFrameAnimTransition(frame, config)
	Mixin(frame, AnimTransitionMixin);
	frame:InitAnimTransition(config);
end

AnimTransitionMixinTransitionType = {
	SlideLeft = 1,
	SlideRight = 2,
	SlideTop = 3,
	SlideBottom = 4,
};

AnimTransitionMixin = {};

local DEFAULT_ANIM_TRANSITION_CONFIG = {
	distanceX = nil, -- If nil, uses frame width
	distanceY = nil, -- If nil, uses frame height
	translationType = nil, -- Value from AnimTransitionMixinTransitionType enum
	duration = 0.3, -- Animation duration in seconds
	easingFunction = nil, -- Easing function from EasingUtil
	hideOnAnimOut = false, -- Hide the frame when out animation completes
	onAnimInFinish = nil, -- Callback when in animation finishes
	onAnimOutFinish = nil, -- Callback when out animation finishes
};

-- Initialize the animation configuration for this instance
-- config: A table with optional properties matching DEFAULT_ANIM_TRANSITION_CONFIG
function AnimTransitionMixin:InitAnimTransition(config)
	config = config or {};

	local defaultConfig = CopyTable(DEFAULT_ANIM_TRANSITION_CONFIG);
	self.animTransitionConfig = Mixin(defaultConfig, config);
end

function AnimTransitionMixin:StartAnimIn()
	if not self.animTransitionConfig then
		error("AnimTransitionMixin: Must call InitAnimTransition before StartAnimIn");
		return;
	end

	self:StartAnimTransition(false);
end

function AnimTransitionMixin:StartAnimOut()
	if not self.animTransitionConfig then
		error("AnimTransitionMixin: Must call InitAnimTransition before StartAnimOut");
		return;
	end

	self:StartAnimTransition(true);
end

function AnimTransitionMixin:CancelAnimTransition()
	if self.animTransitionCancelFunc then
		self.animTransitionCancelFunc();
		self.animTransitionCancelFunc = nil;
	end
end

-- Only meant to be used internally. Call StartAnimIn or StartAnimOut instead.
function AnimTransitionMixin:StartAnimTransition(isOut)
	self:CancelAnimTransition();

	local config = self.animTransitionConfig;

	if not isOut then
		self:Show();
	end

	local distanceX, distanceY = self:GetTranslationDeltas(config.translationType, isOut);

	local needsClampRestore = false;
	if distanceX ~= 0 or distanceY ~= 0 then
		if self:IsClampedToScreen() then
			needsClampRestore = true;
			self:SetClampedToScreen(false);
		end

		-- Set starting state for "in" animation (reverse of the final state)
		if not isOut then
			self:AdjustPointsOffset(-distanceX, -distanceY);
		end
	end

	local variationCallback = ScriptAnimationUtil.GenerateEasedVariationCallback(config.easingFunction, distanceX, distanceY);

	local function OnAnimFinish()
		if isOut and config.hideOnAnimOut then
			self:Hide();
		end

		if needsClampRestore then
			self:SetClampedToScreen(true);
		end

		if isOut then
			if distanceX ~= 0 or distanceY ~= 0 then
				self:AdjustPointsOffset(-distanceX, -distanceY);
			end
		end

		self.animTransitionCancelFunc = nil;

		local callback = isOut and config.onAnimOutFinish or config.onAnimInFinish;
		if callback then
			callback(self);
		end
	end

	self.animTransitionCancelFunc = ScriptAnimationUtil.StartScriptAnimation(self, variationCallback, config.duration, OnAnimFinish);
end

function AnimTransitionMixin:GetTranslationDeltas(translationType, isOut)
	if translationType == AnimTransitionMixinTransitionType.SlideLeft then
		local width = self.animTransitionConfig.distanceX or self:GetWidth();
		return isOut and -width or width, 0;
	elseif translationType == AnimTransitionMixinTransitionType.SlideRight then
		local width = self.animTransitionConfig.distanceX or self:GetWidth();
		return isOut and width or -width, 0;
	elseif translationType == AnimTransitionMixinTransitionType.SlideTop then
		local height = self.animTransitionConfig.distanceY or self:GetHeight();
		return 0, isOut and height or -height;
	elseif translationType == AnimTransitionMixinTransitionType.SlideBottom then
		local height = self.animTransitionConfig.distanceY or self:GetHeight();
		return 0, isOut and -height or height;
	end

	return 0, 0;
end

-- From retail's Blizzard_SharedXML/ScrollingMessageFrame.lua, where the key
-- comes from the Lua intrinsic's XML. 1.12's scrolling message frames are
-- native and have no such key.
function IsScrollingMessageFrame(object)
	-- Defined in the intrinsic's XML
	return object.isScrollingMessageFrame;
end
