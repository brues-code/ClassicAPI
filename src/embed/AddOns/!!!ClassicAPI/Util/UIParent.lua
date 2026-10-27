function MouseIsOver(region, topOffset, bottomOffset, leftOffset, rightOffset)
	if region:IsMouseOver(topOffset, bottomOffset, leftOffset, rightOffset) then
		return 1;
	end
end
