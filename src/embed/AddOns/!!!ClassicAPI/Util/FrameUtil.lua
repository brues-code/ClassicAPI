-- Backport of FrameXML/FrameUtil.lua (core subset) to vanilla 1.12.
--
-- Only the dependency-light, commonly-called helpers are ported. Omitted:
--   - the fade/flash functions (UIFrameFade*/UIFrameFlash*) are already
--     native in 1.12.
--   - the scale/layout/secure/EventRegistry helpers (rely on APIs absent
--     here).

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
