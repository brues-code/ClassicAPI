-- The sizes of the font objects in Fonts.xml.
--
-- Vanilla's Font loader reads <FontHeight> only together with a font file,
-- the `font` attribute (it skips the height when there's none, at
-- 0x00783D02), because a size is set with its font file (SetFont). The fonts
-- in Fonts.xml name no file: they take their typeface from the vanilla font
-- they inherit, so a localized client's font files carry over. So each size
-- is set here, on the typeface and flags the font already has. A font copies
-- what it inherits when it loads, so each size is set on the font itself,
-- the ones built on another included.

local function SetSize(name, size)
	local font = getglobal(name)
	if not (font and font.GetFont) then
		return
	end
	local file, _, flags = font:GetFont()
	if file then
		font:SetFont(file, size, flags)
	end
end

SetSize("SystemFont_Tiny", 9)
SetSize("SystemFont_Small", 10)
SetSize("NumberFont_Shadow_Small", 12)
SetSize("ChatFontSmall", 12)
