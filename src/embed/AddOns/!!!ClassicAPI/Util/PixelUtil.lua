-- Backport of FrameXML/PixelUtil.lua to vanilla 1.12 / Lua 5.0.
--
-- PixelUtil snaps frame sizes/points to whole physical pixels so
-- borders and textures stay crisp at fractional UI scales. The table
-- ports verbatim — its only math deps (Round, Lerp,
-- ClampedPercentageBetween) all live in MathUtil, which loads first.
--
-- The one piece vanilla lacks is the native `GetPhysicalScreenSize()`
-- (added in 7.0). The ClassicAPI DLL provides it (see
-- src/screen/Info.cpp) by reading the engine's `gxResolution` CVar, so
-- it's a real global registered at engine boot — no Lua polyfill
-- needed here.

PixelUtil = {};

function PixelUtil.GetPixelToUIUnitFactor()
    local physicalWidth, physicalHeight = GetPhysicalScreenSize();
    return 768.0 / physicalHeight;
end

function PixelUtil.GetNearestPixelSize(uiUnitSize, layoutScale, minPixels)
    if uiUnitSize == 0 and (not minPixels or minPixels == 0) then
        return 0;
    end

    local uiUnitFactor = PixelUtil.GetPixelToUIUnitFactor();
    local numPixels = Round((uiUnitSize * layoutScale) / uiUnitFactor);
    if minPixels then
        if uiUnitSize < 0.0 then
            if numPixels > -minPixels then
                numPixels = -minPixels;
            end
        else
            if numPixels < minPixels then
                numPixels = minPixels;
            end
        end
    end

    return numPixels * uiUnitFactor / layoutScale;
end

function PixelUtil.ConvertPixelsToUI(desiredPixels, layoutScale)
    return PixelUtil.GetNearestPixelSize(desiredPixels, layoutScale);
end

function PixelUtil.ConvertPixelsToUIForRegion(desiredPixels, region)
    return PixelUtil.GetNearestPixelSize(desiredPixels, region:GetEffectiveScale());
end

function PixelUtil.SetWidth(region, width, minPixels)
    region:SetWidth(PixelUtil.GetNearestPixelSize(width, region:GetEffectiveScale(), minPixels));
end

function PixelUtil.SetHeight(region, height, minPixels)
    region:SetHeight(PixelUtil.GetNearestPixelSize(height, region:GetEffectiveScale(), minPixels));
end

function PixelUtil.SetSize(region, width, height, minWidthPixels, minHeightPixels)
    PixelUtil.SetWidth(region, width, minWidthPixels);
    PixelUtil.SetHeight(region, height, minHeightPixels);
end

function PixelUtil.SetPoint(region, point, relativeTo, relativePoint, offsetX, offsetY, minOffsetXPixels, minOffsetYPixels)
    region:SetPoint(point, relativeTo, relativePoint,
        PixelUtil.GetNearestPixelSize(offsetX, region:GetEffectiveScale(), minOffsetXPixels),
        PixelUtil.GetNearestPixelSize(offsetY, region:GetEffectiveScale(), minOffsetYPixels)
    );
end

function PixelUtil.SetStatusBarValue(statusBar, value)
    local width = statusBar:GetWidth();
    if width and width > 0.0 then
        local min, max = statusBar:GetMinMaxValues();
        local percent = ClampedPercentageBetween(value, min, max);
        if percent == 0.0 or percent == 1.0 then
            statusBar:SetValue(value);
        else
            local numPixels = PixelUtil.GetNearestPixelSize(statusBar:GetWidth() * percent, statusBar:GetEffectiveScale());
            local roundedValue = Lerp(min, max, numPixels / width);
            statusBar:SetValue(roundedValue);
        end
    else
        statusBar:SetValue(value);
    end
end
