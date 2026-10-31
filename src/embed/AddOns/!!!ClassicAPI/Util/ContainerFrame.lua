-- Backport of the bag-count globals from retail's
-- Blizzard_UIPanels_Game/Mainline/ContainerFrame.lua.
--
-- Differences from the retail source:
--   - NUM_TOTAL_BAG_FRAMES is NUM_BAG_FRAMES: there is no reagent bag.
--   - ContainerFrame_GetContainerNumSlots gives the backpack no extra slots.
--     Retail counts 4 more for an account without an authenticator; here
--     the backpack's size does not depend on the account.

NUM_TOTAL_BAG_FRAMES = NUM_BAG_FRAMES;

function ContainerFrame_GetContainerNumSlots(bagId)
	if bagId == KEYRING_CONTAINER then
		local keyRingSize = GetKeyRingSize();
		return keyRingSize, keyRingSize;
	end

	local currentNumSlots = C_Container.GetContainerNumSlots(bagId);
	local maxNumSlots = currentNumSlots;

	return maxNumSlots, currentNumSlots;
end
