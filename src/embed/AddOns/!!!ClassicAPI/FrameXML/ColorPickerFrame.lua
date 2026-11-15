-- Backport of the ColorPickerFrameMixin methods from Classic Era's
-- Blizzard_ColorPickerFrame/Classic/ColorPickerFrame.lua, added to this
-- client's own ColorPickerFrame.
--
-- Differences from the Classic Era source:
--   - This client's frame calls ColorPickerFrame.func when the color changes
--     and when OK is clicked, where Classic Era's calls swatchFunc. So
--     SetupColorPickerAndShow sets both.
--   - The previous values are also the argument the frame gives cancelFunc,
--     as this client's own callers expect.
--   - This client's frame has no stratum of its own, so it takes UIParent's
--     MEDIUM and opens behind the HIGH Settings window. It is put in the
--     DIALOG stratum that Classic Era's frame declares.

ColorPickerFrame:SetFrameStrata("DIALOG");

function ColorPickerFrame:GetColorAlpha()
	return OpacitySliderFrame:GetValue();
end

function ColorPickerFrame:SetupColorPickerAndShow(info)
	self.swatchFunc = info.swatchFunc;
	self.func = info.swatchFunc;
	self.hasOpacity = info.hasOpacity;
	self.opacityFunc = info.opacityFunc;
	self.opacity = info.opacity;
	self.previousValues = { r = info.r, g = info.g, b = info.b, a = info.opacity };
	self.cancelFunc = info.cancelFunc;
	self.extraInfo = info.extraInfo;

	ColorSwatch:SetColorTexture(info.r, info.g, info.b);
	self:SetColorRGB(info.r, info.g, info.b);
	self:Show();
end

function ColorPickerFrame:GetExtraInfo()
	return self.extraInfo;
end

function ColorPickerFrame:GetPreviousValues()
	return self.previousValues.r, self.previousValues.g, self.previousValues.b, self.previousValues.a;
end
