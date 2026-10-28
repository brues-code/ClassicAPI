-- Backport of retail's Blizzard_Menu/Vanilla/MenuVariants.lua.
--
-- Implementation differences from the retail source:
--   - The Classic checkmark and radio atlases are art 1.12 does not have.
--     The checkmark is the dropdown check (UI-CheckBox-Check), and the radio
--     is the radio button sheet (UI-RadioButton: unchecked, then checked,
--     16x16 each).

function MenuVariants.GetDefaultMenuMixin()
	return MenuStyle1Mixin;
end

function MenuVariants.GetDefaultContextMenuMixin()
	return MenuStyle2Mixin;
end

function MenuVariants.CreateCheckbox(text, frame, isSelected, data)
	local leftTexture1 = frame:AttachTexture();
	frame.leftTexture1 = leftTexture1;
	leftTexture1:SetPoint("LEFT");

	local texture = nil;
	if isSelected(data) then
		texture = "Interface\\Buttons\\UI-CheckBox-Check";
	else
		-- For Vanilla checkmarks, show nothing if not selected.
	end
	
	leftTexture1:SetTexture(texture);
	leftTexture1:SetSize(20, 20);

	local fontString = frame:AttachFontString();
	frame.fontString = fontString;
	fontString:SetPoint("LEFT", leftTexture1, "RIGHT", 2, 0);
	fontString:SetHeight(20);
	fontString:SetTextToFit(text);

	return leftTexture1;
end

function MenuVariants.CreateRadio(text, frame, isSelected, data)
	local leftTexture1 = frame:AttachTexture();
	frame.leftTexture1 = leftTexture1;
	leftTexture1:SetPoint("LEFT");

	leftTexture1:SetTexture("Interface\\Buttons\\UI-RadioButton");
	if isSelected(data) then
		leftTexture1:SetTexCoord(0.25, 0.5, 0, 1);
	else
		leftTexture1:SetTexCoord(0, 0.25, 0, 1);
	end
	leftTexture1:SetSize(16, 16);

	local fontString = frame:AttachFontString();
	frame.fontString = fontString;
	fontString:SetPoint("LEFT", leftTexture1, "RIGHT", 2, 0);
	fontString:SetHeight(20);
	fontString:SetTextToFit(text);

	return leftTexture1, leftTexture2;
end
