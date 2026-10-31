-- Backport of retail's Blizzard_FrameXMLUtil/ItemUtil.lua: the ItemUtil table.
--
-- Differences from the retail source:
--   - No ItemButtonUtil and no ItemUtil.DoesAnyItemSlotMatchItemContext.
--     Retail sets the item context from frames this client does not have
--     (scrapping, runeforging, soulbinds, keystones, item upgrades,
--     recrafting, bank tabs), and the stock bag and character frames never
--     ask for it.
--   - No ItemUtil.CreateItemTransmogInfo or ItemTransmogInfoMixin: there is
--     no transmogrification.
--   - ItemUtil.GetItemDetails has no currency path: there are no currencies.
--   - No ItemUtil.GetCraftingReagentCount: it picks between the reagent bank
--     and the account bank, and neither exists.

ItemUtil = {};
function ItemUtil.GetItemDetails(itemLink, quantity)
	local itemName, itemRarity, itemTexture, _;
	itemName, itemLink, itemRarity, _, _, _, _, _, _, itemTexture = C_Item.GetItemInfo(itemLink);
	return itemName, itemTexture, quantity, itemRarity, itemLink;
end

function ItemUtil.GetItemHyperlink(itemID)
	local _, link = C_Item.GetItemInfo(itemID);
	return link;
end

function ItemUtil.PickupBagItem(itemLocation)
	local bag, slot = itemLocation:GetBagAndSlot();
	if bag and slot then
		C_Container.PickupContainerItem(bag, slot);
	end
end

function ItemUtil.IterateBagSlots(bag, callback)
	-- Only includes the backpack and held bag slots.
	for slot = 1, ContainerFrame_GetContainerNumSlots(bag) do
		local itemLocation = ItemLocation:CreateFromBagAndSlot(bag, slot);
		if C_Item.DoesItemExist(itemLocation) then
			if callback(itemLocation) then
				return true;
			end
		end
	end
	return false;
end

function ItemUtil.IterateInventorySlots(firstSlot, lastSlot, callback)
	for slot = firstSlot, lastSlot do
		local itemLocation = ItemLocation:CreateFromEquipmentSlot(slot);
		if C_Item.DoesItemExist(itemLocation) then
			if callback(itemLocation) then
				return;
			end
		end
	end
end

function ItemUtil.IteratePlayerInventory(callback)
	-- Only includes the backpack and held bag slots.
	for bag = Enum.BagIndex.Backpack, NUM_TOTAL_BAG_FRAMES do
		if ItemUtil.IterateBagSlots(bag, callback) then
			return true;
		end
	end

	if(C_ActionBar.ShouldShowKeyring()) then
		if ItemUtil.IterateBagSlots(KEYRING_CONTAINER, callback) then
			return true;
		end
	end

	return false;
end

function ItemUtil.IteratePlayerInventoryAndEquipment(callback)
	local found = ItemUtil.IteratePlayerInventory(callback);
	if not found then
		ItemUtil.IterateInventorySlots(INVSLOT_FIRST_EQUIPPED, INVSLOT_LAST_EQUIPPED, callback);
	end
end

function ItemUtil.FilterOwnedItems(itemIDs)
	local found = {};
	ItemUtil.IteratePlayerInventory(function(itemLocation)
		local itemID = C_Item.GetItemID(itemLocation);
		TableUtil.TrySet(found, itemID);
	end);

	local filtered = {};
	for index, itemID in ipairs(itemIDs) do
		if found[itemID] then
			table.insert(filtered, itemID);
		end
	end
	return filtered;
end

function ItemUtil.TransformItemIDsToItems(itemIDs)
	local items = {};
	for index, itemID in ipairs(itemIDs) do
		table.insert(items, Item:CreateFromItemID(itemID));
	end
	return items;
end

function ItemUtil.TransformItemLocationsToItems(itemLocations)
	local items = {};
	for index, itemLocation in ipairs(itemLocations) do
		table.insert(items, Item:CreateFromItemLocation(itemLocation));
	end
	return items;
end

function ItemUtil.TransformItemGUIDsToItems(itemGUIDs)
	local items = {};
	for index, itemGUID in ipairs(itemGUIDs) do
		local item = Item:CreateFromItemGUID(itemGUID);
		table.insert(items, item);
	end
	return items;
end

function ItemUtil.TransformItemLocationItemsToGUIDItems(itemLocations)
	local items = {};
	for index, itemLocation in ipairs(itemLocations) do
		table.insert(items, Item:CreateFromItemLocation(itemLocation));
	end
	return items;
end

function ItemUtil.GetValidatedItemLocation(equipSlot)
	if equipSlot then
		local itemLocation = ItemLocation:CreateFromEquipmentSlot(equipSlot);
		if itemLocation:IsValid() then
			return itemLocation;
		end
	end

	return nil;
end

function ItemUtil.GetEmptyEquipSlotTooltipForSlotName(equipSlotName)
	if equipSlotName then
		return _G[strupper(equipSlotName)];
	end

	return nil;
end

function ItemUtil.GetEmptyEquipSlotTooltip(equipSlot)
	if equipSlot then
		local _, _, _, slotName = C_PaperDollInfo.GetInventorySlotInfoForInvSlot(equipSlot);
		return ItemUtil.GetEmptyEquipSlotTooltipForSlotName(slotName);
	end

	return nil;
end

function ItemUtil.DisplayEquipSlotTooltip(frame, tooltip, equipSlot, suppressComparison, checkRelic)
	if suppressComparison then
		GameTooltip_SuppressAutomaticCompareItem(tooltip);
	end

	local hasItem, hasCooldown, repairCost = tooltip:SetInventoryItem("player", equipSlot);
	if not hasItem then
		-- This SetOwner is needed because calling SetInventoryItem now hides tooltip if there is no item
		tooltip:SetOwner(frame, "ANCHOR_RIGHT");

		local asRelic = checkRelic and UnitHasRelicSlot("player");
		if asRelic then
			tooltip:SetText(RELICSLOT);
		else
			local text = ItemUtil.GetEmptyEquipSlotTooltip(equipSlot);
			tooltip:SetText(text);
			tooltip:Show();
		end
	end
end

function ItemUtil.GetEquipSlotTexture(equipSlot)
	-- First try to look up the equipped item's texture from the equipped item in the slot
	-- If there's no equipment there, then use the paper doll slot texture for an empty slot
	-- If this cooldown item doesn't refer to equipment, don't return a texture.
	if equipSlot then
		local itemLocation = ItemUtil.GetValidatedItemLocation(equipSlot);
		if itemLocation then
			local icon = C_Item.GetItemIcon(itemLocation);
			return icon;
		end

		local _, icon = C_PaperDollInfo.GetInventorySlotInfoForInvSlot(equipSlot);
		return icon;
	end

	return nil;
end
