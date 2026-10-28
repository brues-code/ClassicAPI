-- Backport of FrameXML/FrameUtil.lua (core subset) to vanilla 1.12.
--
-- Only the dependency-light, commonly-called helpers are ported. Omitted:
--   - the fade/flash functions (UIFrameFade*/UIFrameFlash*) are already
--     native in 1.12.
--   - the scale/layout/secure helpers (rely on APIs absent here).

FrameUtil = FrameUtil or {};

function FrameUtil.RegisterFrameForEvents(frame, events)
    for i, event in ipairs(events) do
        frame:RegisterEvent(event);
    end
end

function FrameUtil.RegisterFrameForUnitEvents(frame, events, ...)
    for i, event in ipairs(events) do
        frame:RegisterUnitEvent(event, unpack(arg));
    end
end

function FrameUtil.UnregisterFrameForEvents(frame, events)
    for i, event in ipairs(events) do
        frame:UnregisterEvent(event);
    end
end

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

-- Reparents while preserving the frame's render layering.
function FrameUtil.SetParentMaintainRenderLayering(frame, parent)
    local origStrata = frame:GetFrameStrata();
    local origFrameLevel = frame:GetFrameLevel();
    frame:SetParent(parent);
    frame:SetFrameStrata(origStrata);
    frame:SetFrameLevel(origFrameLevel);
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

-- From retail's Blizzard_SharedXML/ScrollingMessageFrame.lua, where the key
-- comes from the Lua intrinsic's XML. 1.12's scrolling message frames are
-- native and have no such key.
function IsScrollingMessageFrame(object)
	-- Defined in the intrinsic's XML
	return object.isScrollingMessageFrame;
end
