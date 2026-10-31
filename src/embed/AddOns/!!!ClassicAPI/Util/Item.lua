-- Backport of 3.3.5 FrameXML/ItemUtil.lua to vanilla 1.12 / Lua 5.0.
--
-- Implementation differences from the 3.3.5 source:
--   - Many C_Item.* calls the modern code expects are not exposed by
--     ClassicAPI.dll. We shim the missing ones at the top using the
--     stock 1.12 globals (GetItemInfo, GetInventoryItemLink,
--     GetContainerItemLink/Info). DLL-provided functions are left alone
--     via `or` guards so we never clobber a real implementation.
--   - Lua 5.0 has no select(); calls that previously did
--     `select(N, GetItemInfo(...))` use named locals to peel off the
--     return values instead.
--   - CallErrorHandler isn't in 1.12; we route through geterrorhandler().
--   - In 1.12 OnEvent scripts, event name + payload are exposed via the
--     `event`, `arg1`, `arg2`... globals rather than as function args.
--   - 1.12 has no concept of an item GUID and no public item-lock API,
--     so those return nil / no-op.

----------------------------------------------------------------------

Item = {}
ItemMixin = {}

local ItemEventListener

--[[static]] function Item:CreateFromItemLocation(itemLocation)
    if type(itemLocation) ~= "table" or type(itemLocation.HasAnyLocation) ~= "function" or not itemLocation:HasAnyLocation() then
        error("Usage: Item:CreateFromItemLocation(notEmptyItemLocation)", 2)
    end
    local item = CreateFromMixins(ItemMixin)
    item:SetItemLocation(itemLocation)
    return item
end

--[[static]] function Item:CreateFromBagAndSlot(bagID, slotIndex)
    if type(bagID) ~= "number" or type(slotIndex) ~= "number" then
        error("Usage: Item:CreateFromBagAndSlot(bagID, slotIndex)", 2)
    end
    local item = CreateFromMixins(ItemMixin)
    item:SetItemLocation(ItemLocation:CreateFromBagAndSlot(bagID, slotIndex))
    return item
end

--[[static]] function Item:CreateFromEquipmentSlot(equipmentSlotIndex)
    if type(equipmentSlotIndex) ~= "number" then
        error("Usage: Item:CreateFromEquipmentSlot(equipmentSlotIndex)", 2)
    end
    local item = CreateFromMixins(ItemMixin)
    item:SetItemLocation(ItemLocation:CreateFromEquipmentSlot(equipmentSlotIndex))
    return item
end

--[[static]] function Item:CreateFromItemLink(itemLink)
    if type(itemLink) ~= "string" then
        error("Usage: Item:CreateFromItemLink(itemLinkString)", 2)
    end
    local item = CreateFromMixins(ItemMixin)
    item:SetItemLink(itemLink)
    return item
end

--[[static]] function Item:CreateFromItemID(itemID)
    if type(itemID) ~= "number" then
        error("Usage: Item:CreateFromItemID(itemID)", 2)
    end
    local item = CreateFromMixins(ItemMixin)
    item:SetItemID(itemID)
    return item
end

--[[static]] function Item:CreateFromItemGUID(itemGUID)
	if type(itemGUID) ~= "string" then
		error("Usage: Item:CreateFromItemGUID(itemGUIDString)", 2);
	end
	local item = CreateFromMixins(ItemMixin);
	item:SetItemGUID(itemGUID);
	return item;
end

--[[static]] function Item:DoItemsMatch(item1, item2)
	if not item1 or not item2 then
		return false;
	end

	return item1:Matches(item2);
end

function ItemMixin:Matches(item)
	if not item then
		return false;
	end

	local itemID = item:GetItemID();
	if (itemID ~= nil) and (self:GetItemID() == itemID) then
		return true;
	end

	local itemLocation = item:GetItemLocation();
	if (itemLocation ~= nil) and itemLocation:IsEqualTo(self:GetItemLocation()) then
		return true;
	end

	return false;
end

function ItemMixin:SetItemLocation(itemLocation)
    self:Clear()
    self.itemLocation = itemLocation
end

function ItemMixin:SetItemLink(itemLink)
    self:Clear()
    self.itemLink = itemLink
end

function ItemMixin:SetItemID(itemID)
    self:Clear()
    self.itemID = itemID
end

function ItemMixin:SetItemGUID(itemGUID)
	self:Clear();
	self.itemGUID = itemGUID;
end

function ItemMixin:GetItemLocation()
	if self.itemLocation then
		return self.itemLocation;
	end

	if self.itemGUID then
		return C_Item.GetItemLocation(self.itemGUID);
	end
	return nil;
end

function ItemMixin:HasItemLocation()
    return self.itemLocation ~= nil
end

function ItemMixin:Clear()
    self.itemLocation = nil
    self.itemLink = nil
    self.itemID = nil
	self.itemGUID = nil
end

function ItemMixin:IsItemEmpty()
    if self:GetStaticBackingItem() then
        return not C_Item.DoesItemExistByID(self:GetStaticBackingItem())
    end

    return not self:IsItemInPlayersControl()
end

function ItemMixin:GetStaticBackingItem()
    return self.itemLink or self.itemID
end

function ItemMixin:IsItemInPlayersControl()
    local itemLocation = self:GetItemLocation()
    return itemLocation and C_Item.DoesItemExist(itemLocation)
end

-- Item API
function ItemMixin:GetItemID()
    if self:GetStaticBackingItem() then
        return (C_Item.GetItemInfoInstant(self:GetStaticBackingItem()))
    end

    if not self:IsItemEmpty() then
        return C_Item.GetItemID(self:GetItemLocation())
    end
    return nil
end

function ItemMixin:IsItemLocked()
    return self:IsItemInPlayersControl() and C_Item.IsLocked(self:GetItemLocation())
end

function ItemMixin:LockItem()
    if self:IsItemInPlayersControl() then
        C_Item.LockItem(self:GetItemLocation())
    end
end

function ItemMixin:UnlockItem()
    if self:IsItemInPlayersControl() then
        C_Item.UnlockItem(self:GetItemLocation())
    end
end

function ItemMixin:GetItemIcon() -- requires item data to be loaded
    if self:GetStaticBackingItem() then
        return C_Item.GetItemIconByID(self:GetStaticBackingItem())
    end

    if not self:IsItemEmpty() then
        return C_Item.GetItemIcon(self:GetItemLocation())
    end
end

function ItemMixin:GetItemName() -- requires item data to be loaded
    if self:GetStaticBackingItem() then
        return C_Item.GetItemNameByID(self:GetStaticBackingItem())
    end

    if not self:IsItemEmpty() then
        return C_Item.GetItemName(self:GetItemLocation())
    end
    return nil
end

function ItemMixin:GetItemLink() -- requires item data to be loaded
    if self.itemLink then
        return self.itemLink
    end

    if self.itemID then
        return (select(2, C_Item.GetItemInfo(self.itemID)));
    end

    if not self:IsItemEmpty() then
        return C_Item.GetItemLink(self:GetItemLocation())
    end
    return nil
end

function ItemMixin:GetItemQuality() -- requires item data to be loaded
    if self:GetStaticBackingItem() then
        return C_Item.GetItemQualityByID(self:GetStaticBackingItem())
    end

    if not self:IsItemEmpty() then
        return C_Item.GetItemQuality(self:GetItemLocation())
    end
    return nil
end

function ItemMixin:GetCurrentItemLevel() -- requires item data to be loaded
    if self:GetStaticBackingItem() then
        return (C_Item.GetDetailedItemLevelInfo(self:GetStaticBackingItem()))
    end

    if not self:IsItemEmpty() then
        return C_Item.GetCurrentItemLevel(self:GetItemLocation())
    end
    return nil
end

function ItemMixin:GetItemQualityColor() -- requires item data to be loaded
    local itemQuality = self:GetItemQuality()
    return ITEM_QUALITY_COLORS[itemQuality] -- may be nil if item data isn't loaded
end


function ItemMixin:GetItemQualityColorRGB() -- requires item data to be loaded
	local colorTbl = self:GetItemQualityColor()
	return colorTbl.color:GetRGB()
end

function ItemMixin:GetItemMaxStackSize() -- requires item data to be loaded
	if self:GetStaticBackingItem() then
		return C_Item.GetItemMaxStackSizeByID(self:GetStaticBackingItem());
	end

	if not self:IsItemEmpty() then
		return C_Item.GetItemMaxStackSize(self:GetItemLocation());
	end
	return nil;
end

function ItemMixin:IsStackable() -- requires item data to be loaded
	local maxStackSize = self:GetItemMaxStackSize();
	return maxStackSize and maxStackSize > 1;
end

function ItemMixin:GetInventoryType()
    if self:GetStaticBackingItem() then
        return C_Item.GetItemInventoryTypeByID(self:GetStaticBackingItem())
    end

    if not self:IsItemEmpty() then
        return C_Item.GetItemInventoryType(self:GetItemLocation())
    end
    return nil
end

function ItemMixin:GetItemGUID()
    if self:GetStaticBackingItem() then
        return nil
    end

    if not self:IsItemEmpty() then
        return C_Item.GetItemGUID(self:GetItemLocation())
    end
    return nil
end

function ItemMixin:GetInventoryTypeName()
    if not self:IsItemEmpty() then
        return select(4, C_Item.GetItemInfoInstant(self:GetItemID()));
    end
end

function ItemMixin:IsItemDataCached()
    if self:GetStaticBackingItem() then
        return C_Item.IsItemDataCachedByID(self:GetStaticBackingItem())
    end

    if not self:IsItemEmpty() then
        return C_Item.IsItemDataCached(self:GetItemLocation())
    end
    return true
end

function ItemMixin:IsDataEvictable()
    return true
end

-- Default error handler routing: stock 1.12 has no CallErrorHandler.
local function ItemUtilErrorHandler(err)
    local h = geterrorhandler and geterrorhandler()
    if h then return h(err) end
end

function ItemMixin:ContinueOnItemLoad(callbackFunction)
    if type(callbackFunction) ~= "function" or self:IsItemEmpty() then
        error("Usage: NonEmptyItem:ContinueOnLoad(callbackFunction)", 2)
    end

    ItemEventListener:AddCallback(self:GetItemID(), callbackFunction)
end

function ItemMixin:ContinueWithCancelOnItemLoad(callbackFunction)
    if type(callbackFunction) ~= "function" or self:IsItemEmpty() then
        error("Usage: NonEmptyItem:ContinueWithCancelOnItemLoad(callbackFunction)", 2)
    end

    return ItemEventListener:AddCancelableCallback(self:GetItemID(), callbackFunction)
end

--[ Item Event Listener ]

ItemEventListener = CreateFrame("Frame")
ItemEventListener.callbacks = {}

-- 1.12 OnEvent receives no function args; event name and payload live in
-- the `event`, `arg1`, ... globals.
ItemEventListener:SetScript("OnEvent", function()
    if event == "ITEM_DATA_LOAD_RESULT" then
        local itemID, success = arg1, arg2
        if success then
            ItemEventListener:FireCallbacks(itemID)
        else
            ItemEventListener:ClearCallbacks(itemID)
        end
    end
end)
ItemEventListener:RegisterEvent("ITEM_DATA_LOAD_RESULT")

local CANCELED_SENTINEL = -1

function ItemEventListener:AddCallback(itemID, callbackFunction)
    local callbacks = self:GetOrCreateCallbacks(itemID)
    table.insert(callbacks, callbackFunction)
    C_Item.RequestLoadItemDataByID(itemID)
end

function ItemEventListener:AddCancelableCallback(itemID, callbackFunction)
    local callbacks = self:GetOrCreateCallbacks(itemID)
    table.insert(callbacks, callbackFunction)
    C_Item.RequestLoadItemDataByID(itemID)

    local index = table.getn(callbacks)
    return function()
        if table.getn(callbacks) > 0 and callbacks[index] ~= CANCELED_SENTINEL then
            callbacks[index] = CANCELED_SENTINEL
            return true
        end
        return false
    end
end

function ItemEventListener:FireCallbacks(itemID)
    local callbacks = self:GetCallbacks(itemID)
    if callbacks then
        self:ClearCallbacks(itemID)
        for i, callback in ipairs(callbacks) do
            if callback ~= CANCELED_SENTINEL then
                xpcall(callback, ItemUtilErrorHandler)
            end
        end

        for i = table.getn(callbacks), 1, -1 do
            callbacks[i] = nil
        end
    end
end

function ItemEventListener:ClearCallbacks(itemID)
    self.callbacks[itemID] = nil
end

function ItemEventListener:GetCallbacks(itemID)
    return self.callbacks[itemID]
end

function ItemEventListener:GetOrCreateCallbacks(itemID)
    local callbacks = self.callbacks[itemID]
    if not callbacks then
        callbacks = {}
        self.callbacks[itemID] = callbacks
    end
    return callbacks
end
