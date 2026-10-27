function EquipmentManager_GetLocationData(location)
	local locationData = {};
	if location < 0 then
		return locationData;
	end

	locationData.isPlayer = (bit.band(location, ITEM_INVENTORY_LOCATION_PLAYER) ~= 0);
	locationData.isBank = (bit.band(location, ITEM_INVENTORY_LOCATION_BANK) ~= 0);
	locationData.isBags = (bit.band(location, ITEM_INVENTORY_LOCATION_BAGS) ~= 0);

	locationData.slot = location;
	if locationData.isPlayer then
		locationData.slot = locationData.slot - ITEM_INVENTORY_LOCATION_PLAYER;
	elseif locationData.isBank then
		locationData.slot = locationData.slot - ITEM_INVENTORY_LOCATION_BANK;
	end

	if locationData.isBags then
		locationData.slot = locationData.slot - ITEM_INVENTORY_LOCATION_BAGS;
		locationData.bag = bit.rshift(locationData.slot, ITEM_INVENTORY_BAG_BIT_OFFSET);
		locationData.slot = locationData.slot - bit.lshift(locationData.bag, ITEM_INVENTORY_BAG_BIT_OFFSET);

		if locationData.isBank then
			locationData.bag = locationData.bag + ITEM_INVENTORY_BANK_BAG_OFFSET;
		end
	end

	return locationData;
end

function EquipmentManager_EquipSet(setID)
	if ( C_EquipmentSet.EquipmentSetContainsLockedItems(setID) or C_Spell.UnitCastingInfo("player") ) then
		UIErrorsFrame:AddMessage(ERR_CLIENT_LOCKED_OUT, 1.0, 0.1, 0.1, 1.0);
		return;
	end
	C_EquipmentSet.UseEquipmentSet(setID);
end

-- An equipment-set action button fires WEAR_EQUIPMENT_SET; equipping is the UI's job.
local EquipmentManagerFrame = CreateFrame("Frame");
EquipmentManagerFrame:SetScript("OnEvent", function()
	EquipmentManager_EquipSet(arg1);
end);
EquipmentManagerFrame:RegisterEvent("WEAR_EQUIPMENT_SET");
