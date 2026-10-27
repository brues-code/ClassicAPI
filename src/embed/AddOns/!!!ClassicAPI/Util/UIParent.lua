function MouseIsOver(region, topOffset, bottomOffset, leftOffset, rightOffset)
	if region:IsMouseOver(topOffset, bottomOffset, leftOffset, rightOffset) then
		return 1;
	end
end

-- Retail raises the error only on a GM client (IsGMClient). A player client
-- is never one, so the call does nothing.
function GMError(...)
end
