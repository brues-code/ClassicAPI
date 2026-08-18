# ClassicAPI — Lua Reference

Per-function reference for the calls ClassicAPI adds to the WoW 1.12.1 Lua
environment. See the [project README](../README.md) for installation and
build instructions.

## Contents

- [Account](#account)
  - [`SaveAccount(name, password)` / `DeleteAccount(name)` / `GetSavedAccounts()` / `LoginWithSavedAccount(name)` — GlueXML only](#saveaccountname-password--deleteaccountname--getsavedaccounts--loginwithsavedaccountname--gluexml-only)

- [Action](#action)
  - [`GetActionInfo(slot)`](#getactioninfoslot)

- [AddOns](#addons)
  - [`C_AddOns.GetAddOnName(indexOrName)`](#c_addonsgetaddonnameindexorname)
  - [`C_AddOns.GetAddOnTitle(indexOrName)`](#c_addonsgetaddontitleindexorname)
  - [`C_AddOns.GetAddOnNotes(indexOrName)`](#c_addonsgetaddonnotesindexorname)
  - [`C_AddOns.IsAddOnLoadable(indexOrName)`](#c_addonsisaddonloadableindexorname)
  - [`C_AddOns.IsAddOnLoaded(indexOrName)`](#c_addonsisaddonloadedindexorname)
  - [`C_AddOns.GetAddOnSecurity(indexOrName)`](#c_addonsgetaddonsecurityindexorname)
  - [`C_AddOns.DoesAddOnExist(indexOrName)`](#c_addonsdoesaddonexistindexorname)
  - [`C_AddOns.GetAddOnOptionalDependencies(indexOrName)`](#c_addonsgetaddonoptionaldependenciesindexorname)

- [AuctionHouse](#auctionhouse)
  - [`C_AuctionHouse.PostItem(itemLocation, duration, quantity, numStacks, bid, buyout)`](#c_auctionhousepostitemitemlocation-duration-quantity-numstacks-bid-buyout)

- [CharacterList](#characterlist)
  - [`GetSavedCharacterOrder(realm)` / `SetSavedCharacterOrder(realm, order)` — GlueXML only](#getsavedcharacterorderrealm--setsavedcharacterorderrealm-order--gluexml-only)

- [Chat](#chat)
  - [`GetCurrentChatGUID()`](#getcurrentchatguid)

- [Class](#class)
  - [`FillLocalizedClassList(table [, isFemale])`](#filllocalizedclasslisttable-isfemale)

- [ColorUtil](#colorutil)
  - [`C_ColorUtil.ConvertRGBToHSV(r, g, b)`](#c_colorutilconvertrgbtohsvr-g-b)
  - [`C_ColorUtil.ConvertHSVToRGB(h, s, v)`](#c_colorutilconverthsvtorgbh-s-v)
  - [`C_ColorUtil.ConvertHSVToHSL(h, s, v)`](#c_colorutilconverthsvtohslh-s-v)
  - [`C_ColorUtil.ConvertHSLToHSV(h, s, l)`](#c_colorutilconverthsltohsvh-s-l)
  - [`C_ColorUtil.ConvertHSLToRGB(h, s, l)`](#c_colorutilconverthsltorgbh-s-l)
  - [`C_ColorUtil.GenerateTextColorCode(color)`](#c_colorutilgeneratetextcolorcodecolor)
  - [`C_ColorUtil.WrapTextInColor(text, color)`](#c_colorutilwraptextincolortext-color)
  - [`C_ColorUtil.WrapTextInColorCode(text, colorCode)`](#c_colorutilwraptextincolorcodetext-colorcode)

- [Combat](#combat)
  - [`InCombatLockdown()`](#incombatlockdown)

- [Console](#console)
  - [`ExportInterfaceFiles art|code` (console command)](#exportinterfacefiles-artcode-console-command)
  - [`ExportDBCFiles` (console command)](#exportdbcfiles-console-command)

- [Container](#container)
  - [`C_Container.GetContainerItemID(bagIndex, slotIndex)`](#c_containergetcontaineritemidbagindex-slotindex)
  - [`C_Container.GetContainerItemInfo(containerIndex, slotIndex)`](#c_containergetcontaineriteminfocontainerindex-slotindex)
  - [`C_Container.HasContainerItem(bagIndex, slotIndex)`](#c_containerhascontaineritembagindex-slotindex)
  - [`GetItemCooldown(itemInfo)` / `C_Container.GetItemCooldown(itemID)`](#getitemcooldowniteminfo--c_containergetitemcooldownitemid)
  - [`C_Container.GetContainerItemDurability(containerIndex, slotIndex)`](#c_containergetcontaineritemdurabilitycontainerindex-slotindex)
  - [`C_Container.GetContainerItemRepairCost(containerIndex, slotIndex)`](#c_containergetcontaineritemrepaircostcontainerindex-slotindex)
  - [`C_Container.GetContainerItemCharges(containerIndex, slotIndex)`](#c_containergetcontaineritemchargescontainerindex-slotindex)
  - [`C_Container.GetContainerNumFreeSlots(bagID)`](#c_containergetcontainernumfreeslotsbagid)
  - [`C_Container.CalculateTotalNumberOfFreeBagSlots()`](#c_containercalculatetotalnumberoffreebagslots)
  - [`C_Container.IsContainerItemOpenable(containerIndex, slotIndex)`](#c_containeriscontaineritemopenablecontainerindex-slotindex)
  - [`C_Container.PlayerHasHearthstone()`](#c_containerplayerhashearthstone)
  - [`C_Container.UseHearthstone()`](#c_containerusehearthstone)
  - [`C_Container.SwapItems(srcBag, srcSlot, dstBag, dstSlot)`](#c_containerswapitemssrcbag-srcslot-dstbag-dstslot)
  - [`C_Container.MoveItem(srcBag, srcSlot, dstBag, dstSlot, count)`](#c_containermoveitemsrcbag-srcslot-dstbag-dstslot-count)

- [Creature](#creature)
  - [`C_CreatureInfo.GetCreatureID(guid)`](#c_creatureinfogetcreatureidguid)
  - [`C_CreatureInfo.GetCreatureInfoByID(creatureID)`](#c_creatureinfogetcreatureinfobyidcreatureid)
  - [`C_CreatureInfo.RequestLoadCreatureByID(creatureID)`](#c_creatureinforequestloadcreaturebyidcreatureid)
  - [`C_CreatureInfo.GetRaceInfo(raceID)`](#c_creatureinfogetraceinforaceid)
  - [`C_CreatureInfo.GetClassInfo(classID)`](#c_creatureinfogetclassinfoclassid)
  - [`C_CreatureInfo.GetCreatureFamilyInfo(creatureFamilyID)`](#c_creatureinfogetcreaturefamilyinfocreaturefamilyid)
  - [`C_CreatureInfo.GetCreatureFamilyIDs()`](#c_creatureinfogetcreaturefamilyids)
  - [`C_CreatureInfo.GetFactionInfo(raceID)`](#c_creatureinfogetfactioninforaceid)
  - [`C_CreatureInfo.GetCreatureTypeInfo(creatureTypeID)`](#c_creatureinfogetcreaturetypeinfocreaturetypeid)
  - [`C_CreatureInfo.GetCreatureTypeIDs()`](#c_creatureinfogetcreaturetypeids)

- [Currency](#currency)
  - [`GetCoinTextureString(amount [, fontHeight])` / `C_CurrencyInfo.GetCoinTextureString(amount [, fontHeight])`](#getcointexturestringamount--fontheight--c_currencyinfogetcointexturestringamount--fontheight)

- [CVar](#cvar)
  - [`C_CVar.GetCVarBool(cvar)`](#c_cvargetcvarboolcvar)

- [Cursor](#cursor)
  - [`GetCursorInfo()`](#getcursorinfo)

- [EquipmentSet](#equipmentset)
  - [Overview & file format](#overview--file-format)
  - [`C_EquipmentSet.CanUseEquipmentSets()`](#c_equipmentsetcanuseequipmentsets)
  - [`C_EquipmentSet.GetNumEquipmentSets()`](#c_equipmentsetgetnumequipmentsets)
  - [`C_EquipmentSet.GetEquipmentSetIDs()`](#c_equipmentsetgetequipmentsetids)
  - [`C_EquipmentSet.GetEquipmentSetID(name)`](#c_equipmentsetgetequipmentsetidname)
  - [`C_EquipmentSet.GetEquipmentSetInfo(setID)`](#c_equipmentsetgetequipmentsetinfosetid)
  - [`C_EquipmentSet.GetIgnoredSlots(setID)`](#c_equipmentsetgetignoredslotssetid)
  - [`C_EquipmentSet.GetItemIDs(setID)`](#c_equipmentsetgetitemidssetid)
  - [`C_EquipmentSet.GetItemLocations(setID)`](#c_equipmentsetgetitemlocationssetid)
  - [`C_EquipmentSet.CreateEquipmentSet(name [, icon])`](#c_equipmentsetcreateequipmentsetname--icon)
  - [`C_EquipmentSet.SaveEquipmentSet(setID [, icon])`](#c_equipmentsetsaveequipmentsetsetid--icon)
  - [`C_EquipmentSet.ModifyEquipmentSet(setID, newName)`](#c_equipmentsetmodifyequipmentsetsetid-newname)
  - [`C_EquipmentSet.DeleteEquipmentSet(setID)`](#c_equipmentsetdeleteequipmentsetsetid)
  - [`C_EquipmentSet.IgnoreSlotForSave(slot)` / `UnignoreSlotForSave` / `IsSlotIgnoredForSave` / `ClearIgnoredSlotsForSave`](#c_equipmentsetignoreslotforsaveslot--unignoreslotforsave--isslotignoredforsave--clearignoredslotsforsave)
  - [`C_EquipmentSet.EquipmentSetContainsLockedItems(setID)`](#c_equipmentsetequipmentsetcontainslockeditemssetid)
  - [`C_EquipmentSet.UseEquipmentSet(setID)`](#c_equipmentsetuseequipmentsetsetid)

- [Events](#events)
  - [`C_EventUtils.IsEventValid(eventName)`](#c_eventutilsiseventvalideventname)
  - [`GetFramesRegisteredForEvent(event)`](#getframesregisteredforeventevent)
  - [`PLAYER_ENTERING_WORLD` payload (`isInitialLogin`, `isReloadingUi`)](#player_entering_world-payload-isinitiallogin-isreloadingui)
  - [`PLAYER_TOTEM_UPDATE` event](#player_totem_update-event)
  - [`BAG_UPDATE_DELAYED` event](#bag_update_delayed-event)
  - [`PLAYER_EQUIPMENT_CHANGED` event](#player_equipment_changed-event)
  - [`UPDATE_INVENTORY_DURABILITY` event](#update_inventory_durability-event)
  - [`HEARTHSTONE_BOUND` event](#hearthstone_bound-event)
  - [Player input-state events (`PLAYER_STARTED_MOVING` / `LOOKING` / `TURNING` + `STOPPED_*`)](#player-input-state-events)
  - [`GLOBAL_MOUSE_DOWN` / `GLOBAL_MOUSE_UP` events](#global_mouse_down--global_mouse_up-events)
  - [`AUCTION_MULTISELL_START` / `AUCTION_MULTISELL_UPDATE` / `AUCTION_MULTISELL_FAILURE` events](#c_auctionhousepostitemitemlocation-duration-quantity-numstacks-bid-buyout)
  - [`EQUIPMENT_SETS_CHANGED` event](#equipment_sets_changed-event)
  - [`EQUIPMENT_SWAP_PENDING` event](#equipment_swap_pending-event)
  - [`EQUIPMENT_SWAP_FINISHED` event](#equipment_swap_finished-event)
  - [`FACTION_STANDING_CHANGED` event](#faction_standing_changed-event)
  - [`LOOT_HISTORY_ROLL_CHANGED` / `LOOT_HISTORY_ROLL_COMPLETE` / `LOOT_HISTORY_FULL_UPDATE` events](#loot_history_roll_changed--loot_history_roll_complete--loot_history_full_update-events)
  - [`LOOT_SCAN_COMPLETED` event](#loot_scan_completed-event)
  - [`LOSS_OF_CONTROL_ADDED` / `LOSS_OF_CONTROL_UPDATE` events](#loss_of_control_added--loss_of_control_update-events)
  - [`MODIFIER_STATE_CHANGED` event](#modifier_state_changed-event)
  - [`NAME_PLATE_CREATED` / `NAME_PLATE_UNIT_ADDED` / `NAME_PLATE_UNIT_REMOVED` events](#name_plate_created--name_plate_unit_added--name_plate_unit_removed-events)
  - [`PLAYER_FOCUS_CHANGED` event](#player_focus_changed-event)
  - [`QUEST_ACCEPTED` event](#quest_accepted-event)
  - [`QUEST_REMOVED` event](#quest_removed-event)
  - [`QUEST_TURNED_IN` event](#quest_turned_in-event)
  - [`UNIT_FACTION` event (fire-coverage fix)](#unit_faction-event-fire-coverage-fix)
  - [`UPDATE_MOUSEOVER_UNIT` event (loss-fire fix)](#update_mouseover_unit-event-loss-fire-fix)
  - [`UPDATE_SHAPESHIFT_FORM` event](#update_shapeshift_form-event)
  - [`UNIT_SPELLCAST_*` events](#unit_spellcast_-events)

- [Expansion](#expansion)
  - [`GetClassicExpansionLevel()`](#getclassicexpansionlevel)
  - [`ClassicExpansionAtLeast(expansionLevel)`](#classicexpansionatleastexpansionlevel)
  - [`ClassicExpansionAtMost(expansionLevel)`](#classicexpansionatmostexpansionlevel)

- [Faction](#faction)
  - [`GetFactionIDByIndex(factionIndex)`](#getfactionidbyindexfactionindex)
  - [`GetFactionInfoByID(factionID)`](#getfactioninfobyidfactionid)
  - [`GetFactionParentID(factionID)`](#getfactionparentidfactionid)
  - [`C_Reputation.GetFactionStandings()`](#c_reputationgetfactionstandings)
  - [`C_Reputation.GetWatchedFactionData()`](#c_reputationgetwatchedfactiondata)
  - [`C_Reputation.GetFactionDataByIndex(factionSortIndex)`](#c_reputationgetfactiondatabyindexfactionsortindex)
  - [`C_Reputation.SetWatchedFactionByID(factionID)`](#c_reputationsetwatchedfactionbyidfactionid)
  - [`C_Reputation.GetLastStandingChange()`](#c_reputationgetlaststandingchange)

- [Focus](#focus)
  - [`FocusUnit(unit)`](#focusunitunit)
  - [`ClearFocus()`](#clearfocus)
  - [Unit token (`focus` / `focustarget`)](#unit-token-focus--focustarget)
  - [Bindings (`FOCUSTARGET` / `TARGETFOCUS`)](#bindings-focustarget--targetfocus)

- [Frame](#frame)
  - [`region:SetPoint("point")` (one-argument form)](#regionsetpointpoint-one-argument-form)
  - [`region:SetSize(width, height)` / `region:GetSize()`](#regionsetsizewidth-height--regiongetsize)
  - [`region:IsMouseOver([topOffset, bottomOffset, leftOffset, rightOffset])`](#regionismouseovertopoffset-bottomoffset-leftoffset-rightoffset)
  - [`region:GetRect()`](#regiongetrect)
  - [`region:IsDragging()`](#regionisdragging)
  - [`GetMouseFoci()`](#getmousefoci)
  - [`frame:SetShown(shown)`](#framesetshownshown)
  - [`fontstring:GetStringHeight()`](#fontstringgetstringheight)
  - [`texture:SetRotation(angle [, cx, cy])`](#texturesetrotationangle--cx-cy)
  - [`texture:SetVertexOffset(vertexIndex, offsetX, offsetY)`](#texturesetvertexoffsetvertexindex-offsetx-offsety)
  - [`texture:SetColorTexture(colorR, colorG, colorB [, a])`](#texturesetcolortexturecolorr-colorg-colorb--a)
  - [`frame:SetResizeBounds(minWidth, minHeight [, maxWidth, maxHeight])`](#framesetresizeboundsminwidth-minheight--maxwidth-maxheight)
  - [`frame:HookScript(scriptType, handler)`](#framehookscriptscripttype-handler)
  - [`frame:IsEventRegistered(event)`](#frameiseventregisteredevent)
  - [`frame:GetEffectiveAlpha()`](#framegeteffectivealpha)
  - [`frame:SetAttribute` / `SetAttributeNoHandler` / `ClearAttribute` / `GetAttribute` (+ unit-frame mouseover)](#framesetattributename-value--framesetattributenohandlername-value--frameclearattributename--framegetattribute)
  - [`SetModernScriptArgs(enable)` / `GetModernScriptArgs()`](#setmodernscriptargsenable--getmodernscriptargs)

- [FriendList](#friendlist)
  - [`C_FriendList.SendWhoQueryByName(name)`](#c_friendlistsendwhoquerybynamename)
  - [`C_FriendList.IsWhoQueryPending()`](#c_friendlistiswhoquerypending)
  - [`C_FriendList.GetNumWhoResults()`](#c_friendlistgetnumwhoresults)
  - [`C_FriendList.GetWhoInfo(index)`](#c_friendlistgetwhoinfoindex)
  - [`C_FriendList.IsFriend(guid)`](#c_friendlistisfriendguid)
  - [`C_FriendList.IsIgnored(token)`](#c_friendlistisignoredtoken)
  - [`C_FriendList.IsIgnoredByGuid(guid)`](#c_friendlistisignoredbyguidguid)
  - [`C_FriendList.GetNumFriends()`](#c_friendlistgetnumfriends)
  - [`C_FriendList.GetNumOnlineFriends()`](#c_friendlistgetnumonlinefriends)
  - [`C_FriendList.GetFriendInfo(name)`](#c_friendlistgetfriendinfoname)
  - [`C_FriendList.GetFriendInfoByIndex(index)`](#c_friendlistgetfriendinfobyindexindex)
  - [`C_FriendList.SetFriendNotes(name, notes)`](#c_friendlistsetfriendnotesname-notes)
  - [`C_FriendList.SetFriendNotesByIndex(index, notes)`](#c_friendlistsetfriendnotesbyindexindex-notes)

- [GameObject](#gameobject)
  - [`C_GameObjectInfo.GetGameObjectInfoByID(gameObjectID)`](#c_gameobjectinfogetgameobjectinfobyidgameobjectid)
  - [`C_GameObjectInfo.RequestLoadGameObjectByID(gameObjectID)`](#c_gameobjectinforequestloadgameobjectbyidgameobjectid)
  - [`ClosestGameObjectPosition(gameObjectID)`](#closestgameobjectpositiongameobjectid)

- [GameTooltip](#gametooltip)
  - [`GameTooltip:SetSpellByID(spellID)`](#gametooltipsetspellbyidspellid)
  - [`GameTooltip:AddSpellByID(spellID)`](#gametooltipaddspellbyidspellid)
  - [`GameTooltip:SetTalentByID(talentID)`](#gametooltipsettalentbyidtalentid)
  - [`GameTooltip:SetInventoryItemByID(itemID)`](#gametooltipsetinventoryitembyiditemid)
  - [`GameTooltip:SetHyperlinkCompareItem("itemLink" [, offset, shiftButton, comparisonTooltip])`](#gametooltipsethyperlinkcompareitemitemlink--offset-shiftbutton-comparisontooltip)
  - [`GameTooltip:IsEquippedItem()`](#gametooltipisequippeditem)
  - [`OnTooltipSet*` scripts (Item / Spell / Unit / GameObject)](#ontooltipset-scripts)
  - [`GameTooltip:SetItemByGUID(itemGUID)`](#gametooltipsetitembyguiditemguid)
  - [`GameTooltip:SetEquipmentSet(name)`](#gametooltipsetequipmentsetname)
  - [`GameTooltip:SetTotem(slot)`](#gametooltipsettotemslot)
  - [`GameTooltip:GetItem()`](#gametooltipgetitem)
  - [`GameTooltip:GetSpell()`](#gametooltipgetspell)
  - [`GameTooltip:HasItem()` / `GameTooltip:HasSpell()`](#gametooltiphasitem--gametooltiphasspell)
  - [`GameTooltip:GetUnitGUID()` / `GameTooltip:HasUnit()`](#gametooltipgetunitguid--gametooltiphasunit)
  - [`GameTooltip:GetGameObject()` / `GameTooltip:HasGameObject()`](#gametooltipgetgameobject--gametooltiphasgameobject)
  - [`GameTooltip:GetOwner()`](#gametooltipgetowner)

- [Globals](#globals)
  - [`CLASSIC_API_VERSION`](#classic_api_version)
  - [`LE_EXPANSION_*`](#le_expansion_)
  - [`LE_ITEM_QUALITY_*`](#le_item_quality_)
  - [`LE_UNIT_STAT_*`](#le_unit_stat_)
  - [`Enum.AddOnSecurityStatus`](#enumaddonsecuritystatus)
  - [`Enum.InventoryType`](#enuminventorytype)
  - [`Enum.ItemClass`](#enumitemclass)
  - [`Enum.ItemQuality`](#enumitemquality)
  - [`Enum.PowerType`](#enumpowertype)
  - [`Enum.SpellBookSpellBank`](#enumspellbookspellbank)
  - [`Enum.SpellBookItemType`](#enumspellbookitemtype)

- [Glue](#glue)
  - [`C_Glue.IsFirstLoadThisSession()`](#c_glueisfirstloadthissession)
  - [`C_Glue.IsOnGlueScreen()`](#c_glueisongluescreen)

- [Gossip](#gossip)
  - [`C_GossipInfo.GetText()`](#c_gossipinfogettext)
  - [`C_GossipInfo.GetOptions()`](#c_gossipinfogetoptions)
  - [`C_GossipInfo.GetAvailableQuests()`](#c_gossipinfogetavailablequests)
  - [`C_GossipInfo.GetActiveQuests()`](#c_gossipinfogetactivequests)
  - [`C_GossipInfo.GetNumOptions()` / `GetNumAvailableQuests()` / `GetNumActiveQuests()`](#c_gossipinfogetnumoptions--getnumavailablequests--getnumactivequests)
  - [`C_GossipInfo.SelectOption(gossipOptionID)` / `SelectOptionByIndex(orderIndex)`](#c_gossipinfoselectoptiongossipoptionid--selectoptionbyindexorderindex)
  - [`C_GossipInfo.SelectAvailableQuest(questID)`](#c_gossipinfoselectavailablequestquestid)
  - [`C_GossipInfo.SelectActiveQuest(questID)`](#c_gossipinfoselectactivequestquestid)
  - [`C_GossipInfo.CloseGossip()`](#c_gossipinfoclosegossip)

- [Hooks](#hooks)
  - [`hooksecurefunc(name, callback)` / `hooksecurefunc(table, name, callback)`](#hooksecurefuncname-callback--hooksecurefunctable-name-callback)

- [Input](#input)
  - [`IsLeftShiftKeyDown()` / `IsRightShiftKeyDown()`](#isleftshiftkeydown--isrightshiftkeydown)
  - [`IsLeftControlKeyDown()` / `IsRightControlKeyDown()`](#isleftcontrolkeydown--isrightcontrolkeydown)
  - [`IsLeftAltKeyDown()` / `IsRightAltKeyDown()`](#isleftaltkeydown--isrightaltkeydown)
  - [`IsModifierKeyDown()`](#ismodifierkeydown)
  - [`IsMouseButtonDown([button])`](#ismousebuttondownbutton)
  - [`GetMouseButtonClicked()`](#getmousebuttonclicked)

- [Instance](#instance)
  - [`GetInstanceInfo()`](#getinstanceinfo)

- [Item](#item)
  - [`C_Item.DoesItemExist(itemLocation)` / `C_Item.DoesItemExistByID(item)`](#c_itemdoesitemexititemlocation--c_itemdoesitemexistbyiditem)
  - [`C_Item.EquipItemByName(itemInfo [, dstSlot])`](#c_itemequipitembynameiteminfo--dstslot)
  - [`C_Item.GetCurrentItemLevel(itemLocation)` / `C_Item.GetDetailedItemLevelInfo(item)`](#c_itemgetcurrentitemlevelitemlocation--c_itemgetdetaileditemlevelinfoitem)
  - [`C_Item.GetItemCount(itemInfo, [includeBank], [includeUses])`](#c_itemgetitemcountiteminfo-includebank-includeuses)
  - [`C_Item.GetItemData(itemLocation)` / `C_Item.GetItemDataByID(item)`](#c_itemgetitemdataitemlocation--c_itemgetitemdatabyiditem)
  - [`C_Item.GetItemFamily(item)`](#c_itemgetitemfamilyitem)
  - [`C_Item.GetItemGUID(itemLocation)`](#c_itemgetitemguiditemlocation)
  - [`C_Item.GetItemID(itemLocation)`](#c_itemgetitemiditemlocation)
  - [`C_Item.GetItemInfo(itemInfo)`](#c_itemgetiteminfoiteminfo)
  - [`C_Item.GetItemInfoInstant(item)`](#c_itemgetiteminfoinstantitem)
  - [`C_Item.GetItemInventorySlotInfo(inventorySlot)`](#c_itemgetiteminventoryslotinfoinventoryslot)
  - [`C_Item.GetItemInventorySlotKey(inventorySlot)`](#c_itemgetiteminventoryslotkeyinventoryslot)
  - [`C_Item.GetItemInventoryType(itemLocation)` / `C_Item.GetItemInventoryTypeByID(item)`](#c_itemgetiteminventorytypeitemlocation--c_itemgetiteminventorytypebyiditem)
  - [`C_Item.GetItemLink(itemLocation)`](#c_itemgetitemlinkitemlocation)
  - [`C_Item.GetItemLocation(itemGUID)`](#c_itemgetitemlocationitemguid)
  - [`C_Item.GetItemMaxStackSize(itemLocation)` / `C_Item.GetItemMaxStackSizeByID(item)`](#c_itemgetitemmaxstacksizeitemlocation--c_itemgetitemmaxstacksizebyiditem)
  - [`C_Item.GetItemName(itemLocation)` / `C_Item.GetItemNameByID(item)`](#c_itemgetitemnameitemlocation--c_itemgetitemnamebyiditem)
  - [`C_Item.GetItemQuality(itemLocation)` / `C_Item.GetItemQualityByID(item)`](#c_itemgetitemqualityitemlocation--c_itemgetitemqualitybyiditem)
  - [`C_Item.GetItemSellPrice(itemLocation)` / `C_Item.GetItemSellPriceByID(item)`](#c_itemgetitemsellpriceitemlocation--c_itemgetitemsellpricebyiditem)
  - [`C_Item.GetItemSetID(itemLocation)` / `C_Item.GetItemSetIDByID(item)`](#c_itemgetitemsetiditemlocation--c_itemgetitemsetidbyiditem)
  - [`C_Item.GetItemSetInfo(setID)`](#c_itemgetitemsetinfosetid)
  - [`C_Item.GetItemSpell(item)`](#c_itemgetitemspellitem)
  - [`C_Item.GetItemStatDelta(itemLink1, itemLink2)`](#c_itemgetitemstatdeltaitemlink1-itemlink2)
  - [`C_Item.GetItemStats(itemLink)`](#c_itemgetitemstatsitemlink)
  - [`GetItemClassInfo(classID)`](#getitemclassinfoclassid)
  - [`GetItemSubClassInfo(classID, subClassID)` / `C_Item.GetItemSubClassInfo(classID, subClassID)`](#getitemsubclassinfoclassid-subclassid--c_itemgetitemsubclassinfoclassid-subclassid)
  - [`C_Item.GetItemUniqueness(itemLocation)` / `C_Item.GetItemUniquenessByID(item)`](#c_itemgetitemuniquenessitemlocation--c_itemgetitemuniquenessbyiditem)
  - [`C_Item.GetStackCount(itemLocation)`](#c_itemgetstackcountitemlocation)
  - [`C_Item.IsBound(itemLocation)`](#c_itemisbounditemlocation)
  - [`IsConsumableItem(item)` / `C_Item.IsConsumableItem(item)`](#isconsumableitemitem--c_itemisconsumableitemitem)
  - [`C_Item.IsEquippableItem(item)`](#c_itemisequippableitemitem)
  - [`IsUsableItem(item)` / `C_Item.IsUsableItem(item)`](#isusableitemitem--c_itemisusableitemitem)
  - [`C_Item.IsEquippedItem(item)`](#c_itemisequippeditemitem)
  - [`C_Item.IsItemDataCachedByID(item)` / `C_Item.IsItemDataCached(itemLocation)`](#c_itemisitemdatacachedbyiditem--c_itemisitemdatacacheditemlocation)
  - [`C_Item.IsItemGUIDInInventory(itemGUID)`](#c_itemisitemguidininventoryitemguid)
  - [`C_Item.IsItemInRange(item, targetUnit)`](#c_itemisiteminrangeitem-targetunit)
  - [`C_Item.IsItemOpenable(itemLocation)`](#c_itemisitemopenableitemlocation)
  - [`C_Item.IsLocked(itemLocation)`](#c_itemislockeditemlocation)
  - [`C_Item.LockItem(itemLocation)`](#c_itemlockitemitemlocation)
  - [`C_Item.LockItemByGUID(itemGUID)`](#c_itemlockitembyguiditemguid)
  - [`C_Item.PickupItem(itemInfo)`](#c_itempickupitemiteminfo)
  - [`C_Item.RequestLoadItemDataByID(item)` / `C_Item.RequestLoadItemData(itemLocation)`](#c_itemrequestloaditemdatabyiditem--c_itemrequestloaditemdataitemlocation)
  - [`C_Item.UnlockAllItems()`](#c_itemunlockallitems)
  - [`C_Item.UnlockItem(itemLocation)`](#c_itemunlockitemitemlocation)
  - [`C_Item.UseAtCursor(itemInfo)`](#c_itemuseatcursoriteminfo)
  - [`C_Item.UseAtUnit(itemInfo, unit)`](#c_itemuseatunititeminfo-unit)
  - [`C_Item.UseItemByName(itemInfo [, unit])`](#c_itemuseitembynameiteminfo--unit)
  - [`Get*ItemID` — companions to the engine's `Get*ItemLink` family](#getitemid--companions-to-the-engines-getitemlink-family)
  - [`GetAverageItemLevel()`](#getaverageitemlevel)
  - [`GetInventoryItemDurability(invSlot)`](#getinventoryitemdurabilityinvslot)
  - [`GetInventoryItemID(unit, slot)`](#getinventoryitemidunit-slot)
  - [`GetInventoryItemRepairCost(invSlot)`](#getinventoryitemrepaircostinvslot)
  - [`GetInventoryItemsForSlot(slot, returnTable [, transmogrify])`](#getinventoryitemsforslotslot-returntable--transmogrify)
  - [`GetItemIcon(itemID)` / `C_Item.GetItemIcon(itemLocation)` / `C_Item.GetItemIconByID(item)`](#getitemiconitemid--c_itemgetitemiconitemlocation--c_itemgetitemiconbyiditem)
  - [`OffhandHasWeapon()`](#offhandhasweapon)

- [Loot](#loot)
  - [`C_Loot.GetNearbyLootableUnits()`](#c_lootgetnearbylootableunits)
  - [`C_Loot.LootUnit(guid)`](#c_lootlootunitguid)
  - [`C_Loot.LootUnitItem(guid, itemID)`](#c_lootlootunititemguid-itemid)
  - [`C_Loot.ScanNearbyLoot()`](#c_lootscannearbyloot)
  - [`C_Loot.LootAllCorpses([max])`](#c_lootlootallcorpsesmax)
  - [`C_Loot.IsScanInProgress()`](#c_lootisscaninprogress)
  - [`C_Loot.GetLastScanResults()`](#c_lootgetlastscanresults)

- [LootHistory](#loothistory)
  - [`C_LootHistory.GetNumItems()`](#c_loothistorygetnumitems)
  - [`C_LootHistory.GetItem(itemIndex)`](#c_loothistorygetitemitemindex)
  - [`C_LootHistory.GetPlayerInfo(itemIndex, playerIndex)`](#c_loothistorygetplayerinfoitemindex-playerindex)

- [LossOfControl](#lossofcontrol)
  - [`C_LossOfControl.GetActiveLossOfControlDataCount()`](#c_lossofcontrolgetactivelossofcontroldatacount)
  - [`C_LossOfControl.GetActiveLossOfControlData(index)`](#c_lossofcontrolgetactivelossofcontroldataindex)

- [Lua](#lua)
  - [`select(index, ...)`](#selectindex-)
  - [`table.wipe(t)`](#tablewipet)
  - [`table.count(tbl)`](#tablecounttbl)
  - [`Mixin(object, ...)` / `CreateFromMixins(...)`](#mixinobject--createfrommixins)
  - [`string.match` / `string.gmatch`](#stringmatch--stringgmatch)
  - [`strsplit(sep, str [, pieces])`](#strsplitsep-str--pieces)
  - [`strjoin(delimiter, ...)`](#strjoindelimiter-)
  - [`strtrim(str [, chars])`](#strtrimstr--chars)
  - [`strreplace(str, find, replace)`](#strreplacestr-find-replace)
  - [`string.reverse(s)` / `strrev(s)`](#stringreverses--strrevs)
  - [`math.fmod(x, y)`](#mathfmodx-y)
  - [`math.modf(x)`](#mathmodfx)
  - [`math.huge`](#mathhuge)
  - [`coroutine.create(fn)`](#coroutinecreatefn)
  - [`coroutine.resume(co, ...)`](#coroutineresumeco-)
  - [`coroutine.yield(...)`](#coroutineyield)
  - [`coroutine.status(co)`](#coroutinestatusco)
  - [`coroutine.wrap(fn)`](#coroutinewrapfn)
  - [Async pattern (RunAsync + C_Timer.After)](#async-pattern-runasync--c_timerafter)

- [Macros](#macros)
  - [Numeric spellIDs in `/cast` and `CastSpellByName`](#numeric-spellids-in-cast-and-castspellbyname)
  - [`CastSpellNoToggle` as a macro cast line](#castspellnotoggle-as-a-macro-cast-line)
  - [`GetMacroSpell(macroSlot)`](#getmacrospellmacroslot)
  - [`GetMacroIcons` / `GetMacroItemIcons` / `GetLooseMacroIcons` / `GetLooseMacroItemIcons`](#getmacroicons--getmacroitemicons--getloosemacroicons--getloosemacroitemicons)
  - [`C_Macro.CreateMacro` / `C_Macro.EditMacro`](#c_macrocreatemacro--c_macroeditmacro)

- [Mail](#mail)
  - [`GetSendMailItemLink([attachmentIndex])`](#getsendmailitemlinkattachmentindex)
  - [`GetInboxItemLink(messageIndex[, attachmentIndex])`](#getinboxitemlinkmessageindex-attachmentindex)

- [Map](#map)
  - [`C_Map.GetAreaInfo(areaID)`](#c_mapgetareainfoareaid)
  - [`C_Map.GetAreas()`](#c_mapgetareas)
  - [`C_Map.GetAreaTriggerInfo(triggerID)` / `C_Map.GetAreaTriggers([mapID])`](#c_mapgetareatriggerinfotriggerid--c_mapgetareatriggersmapid)
  - [`C_Map.GetBestMapForUnit(unitToken)`](#c_mapgetbestmapforunitunittoken)
  - [`C_Map.GetMapAreaIDs()`](#c_mapgetmapareaids)
  - [`C_Map.GetMapOverlays([areaID])`](#c_mapgetmapoverlaysareaid)
  - [`C_Map.GetMapWorldSize([areaID])`](#c_mapgetmapworldsizeareaid)

- [MapExplorationInfo](#mapexplorationinfo)
  - [`C_MapExplorationInfo.GetExploredMapTextures([areaID])`](#c_mapexplorationinfogetexploredmaptexturesareaid)
  - [`C_MapExplorationInfo.GetUnexploredMapTextures([areaID])`](#c_mapexplorationinfogetunexploredmaptexturesareaid)

- [MerchantFrame](#merchantframe)
  - [`C_MerchantFrame.GetItemInfo(slot)`](#c_merchantframegetiteminfoslot)
  - [`C_MerchantFrame.GetBuybackItemID(slot)`](#c_merchantframegetbuybackitemidslot)
  - [`C_MerchantFrame.GetNumJunkItems()`](#c_merchantframegetnumjunkitems)
  - [`C_MerchantFrame.SellAllJunkItems()`](#c_merchantframesellalljunkitems)
  - [`C_MerchantFrame.IsMerchantItemRefundable(slot)`](#c_merchantframeismerchantitemrefundableslot)
  - [`C_MerchantFrame.IsSellAllJunkEnabled()`](#c_merchantframeissellalljunkenabled)

- [ChatBubbles](#chatbubbles)
  - [`C_ChatBubbles.GetAllChatBubbles([includeForbidden])`](#c_chatbubblesgetallchatbubblesincludeforbidden)

- [NamePlate](#nameplate)
  - [`C_NamePlate.GetNamePlates()`](#c_nameplategetnameplates)
  - [`C_NamePlate.GetNamePlateGUIDs()`](#c_nameplategetnameplateguids)
  - [`C_NamePlate.GetNamePlateForUnit(unitToken)`](#c_nameplategetnameplateforunitunittoken)
  - [`C_NamePlate.GetNamePlateForGUID(guidString)`](#c_nameplategetnameplateforguidguidstring)
  - [Unit tokens (`nameplateN`)](#unit-tokens-nameplaten)
  - [Unit tokens (`markN`)](#unit-tokens-markn)

- [NameCache](#namecache)
  - [`GetPlayerInfoByGUID(guid)`](#getplayerinfobyguidguid)
  - [`UnitNameFromGUID(guid)`](#unitnamefromguidguid)
  - [`C_PlayerCache.GetPlayerInfoByName(name)`](#c_playercachegetplayerinfobynamename)
  - [`C_PlayerCache.RememberPlayer(guid, name, classToken)`](#c_playercacherememberplayerguid-name-classtoken)
  - [`C_PlayerCache.SetEnabled(enabled)`](#c_playercachesetenabledenabled)
  - [`C_PlayerCache.IsEnabled()`](#c_playercacheisenabled)
  - [`C_PlayerCache.SetScanEnabled(enabled)`](#c_playercachesetscanenabledenabled)
  - [`C_PlayerCache.IsScanEnabled()`](#c_playercacheisscanenabled)

- [NewItems](#newitems)
  - [`C_NewItems.IsNewItem(bagID, slotIndex)`](#c_newitemsisnewitembagid-slotindex)
  - [`C_NewItems.RemoveNewItem(bagID, slotIndex)`](#c_newitemsremovenewitembagid-slotindex)
  - [`C_NewItems.ClearAll()`](#c_newitemsclearall)

- [PlayerInfo](#playerinfo)
  - [`C_PlayerInfo.CanUseItem(itemID)`](#c_playerinfocanuseitemitemid)
  - [`C_PlayerInfo.GUIDIsPlayer(guid)` / `GUIDIsCreature` / `GUIDIsPet` / `GUIDIsGameObject`](#c_playerinfoguidisplayerguid--guidiscreature--guidispet--guidisgameobject)
  - [`C_PlayerInfo.GetName / GetClass / GetRace / GetSex / IsConnected(playerLocation)`](#c_playerinfogetname--getclass--getrace--getsex--isconnectedplayerlocation)
- [Quest](#quest)
  - [`C_QuestLog.GetQuestIDForLogIndex(index)`](#c_questlogGetQuestIDForLogIndexindex)
  - [`C_QuestLog.GetLogIndexForQuestID(questID)`](#c_questloggetlogindexforquestidquestid)
  - [`C_QuestLog.GetHeaderIndexForQuest(questID)`](#c_questloggetheaderindexforquestquestid)
  - [`C_QuestLog.RequestLoadQuestByID(questID)`](#c_questlogrequestloadquestbyidquestid)
  - [`C_QuestLog.IsOnQuest(questID)`](#c_questlogisonquestquestid)
  - [`C_QuestLog.IsUnitOnQuest(unit, questID)`](#c_questlogisunitonquestunit-questid)
  - [`C_QuestLog.GetTitleForQuestID(questID)`](#c_questloggettitleforquestidquestid)
  - [`C_QuestLog.GetQuestDetails(questID)`](#c_questloggetquestdetailsquestid)
  - [`C_QuestLog.GetNumQuestObjectives(questID)`](#c_questloggetnumquestobjectivesquestid)
  - [`C_QuestLog.IsQuestDataCachedByID(questID)`](#c_questlogisquestdatacachedbyidquestid)
  - [`GetQuestLogLeaderBoardID(objectiveIndex [, questIndex])`](#getquestlogleaderboardidobjectiveindex--questindex)

- [Spell](#spell)
  - [`C_Spell.DoesSpellExist(spellID)`](#c_spelldoesspellexistspellid)
  - [`C_Spell.GetSchoolString(schoolMask)`](#c_spellgetschoolstringschoolmask)
  - [`GetSpellInfo(spellID)` / `GetSpellInfo(slot, bookType)`](#getspellinfospellid--getspellinfoslot-booktype)
  - [`C_Spell.GetSpellInfo(spellID)`](#c_spellgetspellinfospellid)
  - [`C_Spell.GetSpellName(spellID)`](#c_spellgetspellnamespellid)
  - [`C_Spell.GetSpellTexture(spellID)`](#c_spellgetspelltexturespellid)
  - [`GetSpellLink(spellID)` / `GetSpellLink(slot, bookType)`](#getspelllinkspellid--getspelllinkslot-booktype)
  - [`C_Spell.GetSpellLink(spellID)`](#c_spellgetspelllinkspellid)
  - [`C_Spell.GetSpellDescription(spellID)`](#c_spellgetspelldescriptionspellid)
  - [`C_Spell.GetSpellMechanicByID(spellID)`](#c_spellgetspellmechanicbyidspellid)
  - [`C_Spell.GetSpellEffectMechanics(spellID)`](#c_spellgetspelleffectmechanicsspellid)
  - [`C_Spell.GetSpellDispelType(spellID)`](#c_spellgetspelldispeltypespellid)
  - [`C_Spell.GetSpellRadius(spellID)` / `GetSpellRadius(slot, bookType)`](#c_spellgetspellradiusspellid--getspellradiusslot-booktype)
  - [`C_Spell.GetSpellPowerCost(spellIdentifier)`](#c_spellgetspellpowercostspellidentifier)
  - [`C_Spell.GetSpellReagents(spellID)`](#c_spellgetspellreagentsspellid)
  - [`C_Spell.GetSpellSubtext(spellIdentifier)`](#c_spellgetspellsubtextspellidentifier)
  - [`IsPassiveSpell(spellID)` / `IsPassiveSpell(slot, bookType)`](#ispassivespellspellid--ispassivespellslot-booktype)
  - [`C_Spell.IsSpellPassive(spellID)`](#c_spellisspellpassivespellid)
  - [`IsPlayerSpell(spellID)`](#isplayerspellspellid)
  - [`CanDualWield()`](#candualwield)
  - [`IsSpellKnown(spellID, [isPet])`](#isspellknownspellid-ispet)
  - [`GetSpellBonusDamage(school)`](#getspellbonusdamageschool)
  - [`GetSpellBonusHealing()`](#getspellbonushealing)
  - [`IsUsableSpell(spell)` / `IsUsableSpell(slot, bookType)`](#isusablespellspell--isusablespellslot-booktype)
  - [`C_Spell.IsSpellUsable(spellID)`](#c_spellisspellusablespellid)
  - [`C_Spell.GetSpellCooldown(spellIdentifier)`](#c_spellgetspellcooldownspellidentifier)
  - [`C_Spell.IsCurrentSpell(spellIdentifier)`](#c_spelliscurrentspellspellidentifier)
  - [`C_Spell.IsSelfBuff(spellID)`](#c_spellisselfbuffspellid)
  - [`C_Spell.SpellHasRange(spellIdentifier)` / `SpellHasRange(slot, bookType)`](#c_spellspellhasrangespellidentifier--spellhasrangeslot-booktype)
  - [`C_Spell.IsSpellInRange(spellIdentifier, targetUnit)`](#c_spellisspellinrangespellidentifier-targetunit)
  - [`C_Spell.IsAutoAttackSpell(spellID)`](#c_spellisautoattackspellspellid)
  - [`C_Spell.IsRangedAutoAttackSpell(spellID)`](#c_spellisrangedautoattackspellspellid)
  - [`C_Spell.IsNextMeleeSpell(spellID)`](#c_spellisnextmeleespellspellid)
  - [`C_Spell.ResetsMeleeSwing(spellID)`](#c_spellresetsmeleeswingspellid)
  - [`IsHarmfulSpell(spell)` / `IsHelpfulSpell(spell)`](#isharmfulspellspell--ishelpfulspellspell)
  - [`C_Spell.IsSpellHarmful(spellID)` / `C_Spell.IsSpellHelpful(spellID)`](#c_spellisspellharmfulspellid--c_spellisspellhelpfulspellid)
  - [`GetSpellSchool(spellID)`](#getspellschoolspellid)
  - [`CastSpellNoToggle(name | spellID)`](#castspellnotogglename--spellid)
  - [`C_Spell.CastAtCursor(spellIDOrName)`](#c_spellcastatcursorspellidorname)
  - [`C_Spell.CastAtUnit(spellIDOrName, unit)`](#c_spellcastatunitspellidorname-unit)
  - [`C_Spell.CancelSpellByID(spellID)` / `CancelSpellByName(name)`](#c_spellcancelspellbyidspellid--cancelspellbynamename)
  - [`C_Spell.UnitCastingInfo(unit)` / `C_Spell.CastingInfo()`](#c_spellunitcastinginfounit--c_spellcastinginfo)
  - [`C_Spell.UnitChannelInfo(unit)` / `C_Spell.ChannelInfo()`](#c_spellunitchannelinfounit--c_spellchannelinfo)
  - [`C_Spell.GetSpellLevelInfo(spellID)`](#c_spellgetspelllevelinfospellid)
  - [`GetSpellRequiredTargetLevel(spellID)`](#getspellrequiredtargetlevelspellid)

- [SpellBook](#spellbook)
  - [`FindSpellBookSlotByID(spellID)`](#findspellbookslotbyidspellid)
  - [`C_SpellBook.GetSpellBookItemInfo(slotIndex, spellBank)`](#c_spellbookgetspellbookiteminfoslotindex-spellbank)
  - [`C_SpellBook.GetSpellLevelLearned(spellID)`](#c_spellbookgetspelllevellearnedspellid)
  - [`C_SpellBook.GetCurrentLevelSpells([level])`](#c_spellbookgetcurrentlevelspellslevel)
  - [`C_SpellBook.GetSkillLineName(skillLineID)`](#c_spellbookgetskilllinenameskilllineid)
  - [`C_SpellBook.GetSkillLineRank(skillLineID)`](#c_spellbookgetskilllinerankskilllineid)
  - [`C_SpellBook.GetSpellSkillLine(spellID)`](#c_spellbookgetspellskilllinespellid)
  - [`C_SpellBook.IsAutoAttackSpellBookItem(slot, bookType)`](#c_spellbookisautoattackspellbookitemslot-booktype)
  - [`C_SpellBook.IsRangedAutoAttackSpellBookItem(slot, bookType)`](#c_spellbookisrangedautoattackspellbookitemslot-booktype)

- [State](#state)
  - [`IsMounted()`](#ismounted)
  - [`Dismount()`](#dismount)
  - [`IsStealthed()`](#isstealthed)
  - [`IsFalling()`](#isfalling)
  - [`IsSwimming()`](#isswimming)
  - [`IsIndoors()`](#isindoors)
  - [`IsOutdoors()`](#isoutdoors)
  - [`IsAssistingRitual()`](#isassistingritual)
  - [`IsInGroup()`](#isingroup)
  - [`IsInRaid()`](#isinraid)
  - [`GetMirrorTimerInfo(index)` / `GetMirrorTimerProgress(label)`](#getmirrortimerinfoindex--getmirrortimerprogresslabel)
  - [`GetShapeshiftFormID()`](#getshapeshiftformid)
  - [`CancelShapeshiftForm()`](#cancelshapeshiftform)

- [System](#system)
  - [`GetPhysicalScreenSize()`](#getphysicalscreensize)
  - [`CopyToClipboard(text [, removeMarkup])`](#copytoclipboardtext--removemarkup)

- [Talent](#talent)
  - [`GetTalentSpellID(tabIndex, talentIndex, [rank[, classID]])`](#gettalentspellidtabindex-talentindex-rank-classid)
  - [`GetTalentIDByIndex(tabIndex, talentIndex[, classID])`](#gettalentidbyindextabindex-talentindex-classid)

- [Targeting](#targeting)
  - [`GetPlayerFacing()`](#getplayerfacing)
  - [`TargetDirectionEnemy(facing [, coneAngle])`](#targetdirectionenemyfacing--coneangle)
  - [`TargetDirectionFriend(facing [, coneAngle])`](#targetdirectionfriendfacing--coneangle)
  - [`TargetNearest([reverse])`](#targetnearestreverse)
  - [`TargetNearestEnemyPlayer([reverse])`](#targetnearestenemyplayerreverse)
  - [`TargetNearestFriendPlayer([reverse])`](#targetnearestfriendplayerreverse)

- [TaxiMap](#taximap)
  - [`C_TaxiMap.GetTaxiNodesForMap([mapID])`](#c_taximapgettaxinodesformapmapid)
  - [`C_TaxiMap.GetAllTaxiNodes([uiMapID])`](#c_taximapgetalltaxinodesuimapid)
  - [`C_TaxiMap.GetTaxiPaths()`](#c_taximapgettaxipaths)
  - [`C_TaxiMap.GetTaxiPathWaypoints(pathID)`](#c_taximapgettaxipathwaypointspathid)
  - [`C_TaxiMap.GetTaxiRoute(slotIndex)`](#c_taximapgettaxiroutepslotindex)

- [Time](#time)
  - [`GetServerTime()`](#getservertime)
  - [`GetTimeCached()`](#gettimecached)
  - [`C_Timer.After(seconds, callback)`](#c_timeraftersseconds-callback)
  - [`C_Timer.NewTimer(seconds, callback)`](#c_timernewtimerseconds-callback)
  - [`C_Timer.NewTicker(seconds, callback, [iterations])`](#c_timernewtickerseconds-callback-iterations)
  - [`C_DateAndTime` overview](#c_dateandtime-overview)
  - [`C_DateAndTime.GetCurrentCalendarTime()`](#c_dateandtimegetcurrentcalendartime)
  - [`C_DateAndTime.GetCalendarTimeFromEpoch(epoch)`](#c_dateandtimegetcalendartimefromepochepoch)
  - [`C_DateAndTime.AdjustTimeByDays(date, days)` / `AdjustTimeByMinutes(date, minutes)`](#c_dateandtimeadjusttimebydaysdate-days--adjusttimebyminutesdate-minutes)
  - [`C_DateAndTime.CompareCalendarTime(lhs, rhs)`](#c_dateandtimecomparecalendartimelhs-rhs)
  - [`C_DateAndTime.GetServerTimeLocal()`](#c_dateandtimegetservertimelocal)
  - [`C_DateAndTime.GetSecondsUntilDailyReset()`](#c_dateandtimegetsecondsuntildailyreset)

- [Totem](#totem)
  - [`GetTotemInfo(slot)`](#gettoteminfoslot)
  - [`GetTotemTimeLeft(slot)`](#gettotemtimeleftslot)
  - [`GetTotemDuration(slot)`](#gettotemdurationslot)
  - [`TargetTotem(slot)`](#targettotemslot)

- [TradeSkillUI](#tradeskillui)
  - [`C_TradeSkillUI.GetTradeSkillListLink()`](#c_tradeskilluigettradeskilllistlink)
  - [`C_TradeSkillUI.GetCraftListLink()`](#c_tradeskilluigetcraftlistlink)
  - [`C_TradeSkillUI.GetTradeSkillListRecipes(skillLineID, bits)`](#c_tradeskilluigettradeskilllistrecipesskilllineid-bits)

- [UIColor](#uicolor)
  - [`C_UIColor.GetColors()`](#c_uicolorgetcolors)

- [Unit](#unit)
  - [`UnitGUID(unit)`](#unitguidunit)
  - [`UnitTokenFromGUID(guid)`](#unittokenfromguidguid)
  - [`UnitSubName(unit)`](#unitsubnameunit)
  - [`UnitCreatureFamilyID(unit)`](#unitcreaturefamilyidunit)
  - [`UnitCreatureTypeID(unit)`](#unitcreaturetypeidunit)
  - [`GetUnitSpeed(unit)`](#getunitspeedunit)
  - [`UnitClassBase(unit)`](#unitclassbaseunit)
  - [`UnitRaceBase(unit)`](#unitracebaseunit)
  - [`UnitIsAFK(unit)`](#unitisafkunit)
  - [`UnitIsDND(unit)`](#unitisdndunit)
  - [`UnitIsFeignDeath(unit)`](#unitisfeigndeathunit)
  - [`UnitIsInMyGuild(unitOrName)`](#unitisinmyguildunitorname)
  - [`UnitIsPossessed(unit)`](#unitispossessedunit)
  - [`UnitIsMinion(unit)`](#unitisminionunit)
  - [`UnitIsPet(unit)`](#unitispetunit)
  - [`UnitIsOtherPlayersPet(unit)`](#unitisotherplayerspetunit)
  - [`UnitOwnerGUID(unit)`](#unitownerguidunit)
  - [`UnitStandState(unit)`](#unitstandstateunit)
  - [`UnitInRange(unit)`](#unitinrangeunit)
  - [`UnitDistanceSquared(unit)`](#unitdistancesquaredunit)
  - [`UnitPosition(unit)`](#unitpositionunit)
  - [`UnitInLineOfSight(unit)`](#unitinlineofsightunit)
  - [`ClosestUnitPosition(creatureID)`](#closestunitpositioncreatureid)
  - [`UnitHealthMissing(unit)`](#unithealthmissingunit)
  - [`UnitPower(unit [, powerType [, unmodified]])` / `UnitPowerMax(unit [, powerType [, unmodified]])`](#unitpowerunit--powertype--unmodified--unitpowermaxunit--powertype--unmodified)
  - [`UnitPowerMissing(unit [, powerType [, unmodified]])`](#unitpowermissingunit--powertype--unmodified)
  - [`UnitPowerType(unit)`](#unitpowertypeunit)
  - [`UnitSpellHaste(unit)`](#unitspellhasteunit)

- [UnitAuras](#unitauras)
  - [`C_UnitAuras.GetAuraDataByIndex(unit, index [, filter])`](#c_unitaurasgetauradatabyindexunit-index--filter)
  - [`C_UnitAuras.GetBuffDataByIndex(unit, index)` / `GetDebuffDataByIndex(unit, index)`](#c_unitaurasgetbuffdatabyindexunit-index--getdebuffdatabyindexunit-index)
  - [`C_UnitAuras.GetUnitAuraBySpellID(unit, spellID [, filter])`](#c_unitaurasgetunitaurabyspellidunit-spellid--filter)
  - [`C_UnitAuras.GetPlayerAuraBySpellID(spellID)`](#c_unitaurasgetplayeraurabyspellidspellid)
  - [`C_UnitAuras.GetAuraDataBySpellName(unit, spellName [, filter])`](#c_unitaurasgetauradatabyspellnameunit-spellname--filter)
  - [`C_UnitAuras.RegisterComboDuration(spellID, baseSeconds, maxSeconds)`](#c_unitaurasregistercombodurationspellid-baseseconds-maxseconds)
  - [`C_UnitAuras.RegisterAuraDurationModifierByTrigger(triggerFamily, triggerSchool, affectedFamily, affectedFamilyFlags, affectedIcon, op [, valueSeconds])`](#c_unitaurasregisterauradurationmodifierbytriggertriggerfamily-triggerschool-affectedfamily-affectedfamilyflags-affectedicon-op--valueseconds)
  - [`C_UnitAuras.GetUnitAuras(unit [, filter])`](#c_unitaurasgetunitaurasunit--filter)
  - [`C_UnitAuras.GetAuraDispelTypeColor(dispelName)`](#c_unitaurasgetauradispeltypecolordispelname)

- [VoiceChat](#voicechat)
  - [`C_VoiceChat.GetTtsVoices()` / `C_VoiceChat.GetRemoteTtsVoices()`](#c_voicechatgetttsvoices--c_voicechatgetremotettsvoices)
  - [`C_VoiceChat.SpeakText(voiceID, text [, destination, rate, volume])`](#c_voicechatspeaktextvoiceid-text--destination-rate-volume)
  - [`C_VoiceChat.StopSpeakingText()`](#c_voicechatstopspeakingtext)

- [XMLUtil](#xmlutil)
  - [`C_XMLUtil.GetTemplates()`](#c_xmlutilgettemplates)
  - [`C_XMLUtil.GetTemplateInfo(name)`](#c_xmlutilgettemplateinfoname)
  - [`C_XMLUtil.DoesTemplateExist(name)`](#c_xmlutildoestemplateexistname)
  - [`C_TTSSettings` — getters & setters](#c_ttssettings--getters--setters)
  - [TTS events](#tts-events)
  - [TTS CVars](#tts-cvars)

## Account

### `SaveAccount(name, password)` / `DeleteAccount(name)` / `GetSavedAccounts()` / `LoginWithSavedAccount(name)` — GlueXML only

Persists account credentials in Windows Credential Manager (per-user,
DPAPI-encrypted) and dispatches login from C so the plaintext password
never crosses the C↔Lua boundary on the way out. Designed for GlueXML
rewrites of `AccountLogin.lua` that want a selection list of remembered
accounts without persisting plaintext passwords to SavedVariables.

> **Only callable from the login screen** (the "glue" Lua state).
> Calling them from an in-world addon errors with "attempt to call nil"
> — they aren't registered there.

- `SaveAccount(name, password)` — encrypt and save under the current
  realmlist. Returns `true` on success, `false` if name or password is
  empty, no realmlist is set, or the OS rejects the write.
- `DeleteAccount(name)` — remove the saved entry. Returns `true` if a
  matching entry existed and was deleted.
- `GetSavedAccounts()` — returns a numeric-keyed table of entries for
  the current realmlist. Other realmlists' entries are invisible. Empty
  table if no realmlist is set. Each entry is a table:

  | Field | Type | Notes |
  |---|---|---|
  | `name` | string | Account name as saved. |
  | `lastUsed` | number | Unix epoch seconds of the last write to the vault entry. Refreshed automatically by `LoginWithSavedAccount`, so this functions as a "last used" timestamp. `0` if Windows didn't supply a timestamp (extremely rare, e.g. credentials manually injected). |

  ```lua
  for i, entry in ipairs(GetSavedAccounts()) do
      print(entry.name, date("%Y-%m-%d %H:%M", entry.lastUsed))
  end
  ```

- `LoginWithSavedAccount(name)` — decrypt internally and feed the
  credentials to the engine's login function. Returns `true` if
  credentials were found and dispatched, `false` if no such entry
  exists. **Plaintext is never returned to Lua.** Equivalent to what
  `DefaultServerLogin(name, password)` does, but with the password
  fetched from the vault rather than passed in. Also re-saves the
  credential (same value) to refresh the `lastUsed` timestamp.

#### Scoping per realmlist

Every operation is scoped by the current `realmList` CVar (the address
from `realmlist.wtf`, e.g. `"logon.turtle-wow.org"`), not the friendly
display name. The same account name on two different private servers
therefore gets two distinct vault entries:

```
ClassicAPI/WoW/logon.turtle-wow.org/MYACCT
ClassicAPI/WoW/logon.other-server.com/MYACCT
```

Switching realmlists changes the visible account list automatically;
no separate "select realm" step. If you need a refresh while still on
the login screen, call `Autologin_Load()` (or `GetSavedAccounts()`
directly) after the realmlist changes.

#### Storage

Entries live in the OS Credential Manager under the
`ClassicAPI/WoW/<realmlist>/` namespace. Users can inspect and wipe
them via Windows' `control /name Microsoft.CredentialManager` →
Generic Credentials, the same way they'd manage any other Windows-
stored credential.

#### Security caveats

Realistic about what this defends against:

- **Defeats**: casual file inspection of WTF, sharing the WTF folder,
  cloud-syncing it, another Windows user on the same machine, malware
  running as a different Windows user.
- **Does not defeat**: any process running as the current Windows user
  can decrypt these via DPAPI — it's per-user, not per-process. No
  "decrypt and return plaintext" path is exposed to Lua, so a hostile
  addon can trigger a login with a saved account but can't extract the
  password. Plaintext is also unavoidably in process memory while the
  engine sends SRP.

#### Addon example

```lua
-- GlueXML, e.g. AccountLogin.lua: save credentials on first manual
-- login, one-click login on subsequent shows.

local function OnLoginClicked()
    local name = AccountLoginAccountEdit:GetText()
    local password = AccountLoginPasswordEdit:GetText()
    if not name or name == "" then return end

    if password and password ~= "" then
        -- Fresh login or password change: persist + log in.
        SaveAccount(name, password)
        LoginWithSavedAccount(name)
    else
        -- Reuse stored credentials.
        LoginWithSavedAccount(name)
    end
end

-- Populate a selection list from the vault, sorted by recency.
local accounts = GetSavedAccounts()
table.sort(accounts, function(a, b) return a.lastUsed > b.lastUsed end)
for i = 1, table.getn(accounts) do
    local e = accounts[i]
    print(i, e.name, "last used:", date("%m/%d/%Y", e.lastUsed))
end

-- Forget an account.
DeleteAccount("OLDACCT")
```

## Action

### `GetActionInfo(slot)`

Returns the action descriptor for a 1-based action-bar slot, in the
shape `actionType, id, subType`. Returns `nil` for empty slots.

```lua
local actionType, id, subType = GetActionInfo(1)
```

| Slot contents             | Returns                            |
|---------------------------|------------------------------------|
| empty                     | `nil`                              |
| spell                     | `"spell", spellID, "spell"`        |
| macro                     | `"macro", macroSlot`               |
| item (by-itemID)          | `"item", itemID`                   |
| item (by bag-instance)    | `"item", nil`                      |

The 120-slot action table at `0x00BC6980` packs each entry as a
`uint32` where the top 4 bits are the type tag:

- **`0x0` — spell action.** Entry is the spellID. The third return is
  always `"spell"` for entries on this table; the engine helper
  hardcodes pet flag to 0 here. Pet-bar actions live in a separate
  table — use `PetHasActionBar` + the pet-action helpers for pet
  slots.
- **`0x4` — macro or bag-item.** Ambiguous tag, disambiguated by
  walking the 36-entry macro-slot map at `0x00BDCC60` and checking
  whether `entry & 0xBFFFFFFF` matches any of those macro IDs. On a
  hit, the slot is a macro and the matching index becomes the
  1-based `macroSlot` return. On a miss, the slot is a bag-item
  reference; we surface the type as `"item"` but return `nil` for
  the `id` — extracting the itemID from the bag-action hash struct
  needs more reversing than we've done.
- **`0x8` — item by itemID.** `entry & 0x7FFFFFFF` is the itemID.

This is the same discrimination [SuperWoWhook][superwowhook] uses in
its replacement of `GetActionText` (which returns `name, type, id`).
Without SuperWoWhook the engine's stock `GetActionText` only handles
items in bags; ClassicAPI's `GetActionInfo` provides the modern-WoW
shape independently of any other DLL patches.

Equivalent to the function of the same name in retail. Subtype is
always `"spell"` for spell entries (no pet differentiation on this
table) and the bag-item itemID path is currently incomplete — see
the comment in [src/action/Info.cpp](../src/action/Info.cpp).

[superwowhook]: https://github.com/balakethelock/SuperWoW

## AddOns

Modern `C_AddOns.*` getters that splat the legacy
`GetAddOnInfo(arg)` 7-tuple `(name, title, notes, enabled,
loadable, reason, security)` into single-field accessors, plus a
getter for the addon's optional dependencies. Most bypass
`GetAddOnInfo` entirely and call the engine's per-field helpers
directly.

All accept either a 1-based index (`1..GetNumAddOns()`) or an
addon directory name string.

### `C_AddOns.GetAddOnName(indexOrName)`

Returns the addon's directory name as the engine sees it (the
folder name on disk). For numeric input, returns the engine's
canonical casing; for string input, echoes the input verbatim
once existence is confirmed. Returns `nil` for missing addons.

```lua
C_AddOns.GetAddOnName(1)            -- "Atlas-TW"
C_AddOns.GetAddOnName("DebugTools") -- "DebugTools"
C_AddOns.GetAddOnName("garbage")    -- nil
```

### `C_AddOns.GetAddOnTitle(indexOrName)`

Returns the `## Title:` from the addon's `.toc` file, with WoW
color-code escapes applied. `nil` for missing addons or addons
without a title field.

```lua
C_AddOns.GetAddOnTitle("DebugTools") -- "UI Debug Tools"
```

### `C_AddOns.GetAddOnNotes(indexOrName)`

Returns the `## Notes:` from the `.toc`. `nil` for missing
addons or addons without notes.

```lua
C_AddOns.GetAddOnNotes("DebugTools")
-- "Tools for developing addons (backport of Blizzard_DebugTools 3.3.5 to 1.12.1 / Lua 5.0)"
```

### `C_AddOns.IsAddOnLoadable(indexOrName)`

Returns `loadable, reason` — a real boolean and a status string
(or `nil`).

`reason` comes from a small status table the engine consults when
populating `GetAddOnInfo`'s 6th return: `"DISABLED"`, `"BANNED"`,
`"CORRUPT"`, `"INSECURE"`, `"NOT_DEMAND_LOADED"`,
`"INTERFACE_VERSION"`, `"MISSING"`. `nil` when the addon is
loadable. The full modern signature accepts optional `character`
and `demandLoaded` arguments — those are ignored here since vanilla
1.12 has no per-character addon enable state.

```lua
C_AddOns.IsAddOnLoadable("DebugTools")        -- true, nil
C_AddOns.IsAddOnLoadable("HardcoreTooltips")  -- false, "DISABLED"
C_AddOns.IsAddOnLoadable("garbage")           -- false, nil
```

### `C_AddOns.IsAddOnLoaded(indexOrName)`

Returns `loadedOrLoading, loaded` — two booleans.

```lua
C_AddOns.IsAddOnLoaded("DebugTools")    -- true, true
C_AddOns.IsAddOnLoaded("garbage")       -- false, false
C_AddOns.IsAddOnLoaded(1)               -- true, true   (first addon by index)
```

Modern WoW splits the two returns to distinguish "load-in-progress"
from "fully loaded" — the difference matters for `LoadOnDemand`
addons whose load is split across multiple `LoadAddOn` callbacks.
Vanilla 1.12's addon loader (`FUN_0051F240`) is fully synchronous:
the `loaded` byte flips inside a single call, so the in-flight
state is never observable from Lua. Both returns are always the
same boolean here. We surface the two-return shape so consumer
code written against modern API doesn't need to special-case
vanilla.

Unknown addons (numeric index past `GetNumAddOns()`, or
string name not in the registry) return `false, false`.

### `C_AddOns.GetAddOnSecurity(indexOrName)`

Returns an `Enum.AddOnSecurityStatus` integer (not a string — modern
shape):

| Value | `Enum.AddOnSecurityStatus.*` | When |
|------:|------------------------------|------|
| `0`   | `Secure`                     | Blizzard-signed addons (`Blizzard_*`). |
| `1`   | `Insecure`                   | Every user addon loaded from `Interface/AddOns/`. The default for any registered addon not in the secure or banned override sets. |
| `2`   | `Banned`                     | Entries the engine has explicitly disqualified — set by server addon-banlist responses, rare in practice. |
| `3`   | `NotAvailable`               | No addon by that name / index exists. |

```lua
C_AddOns.GetAddOnSecurity("Blizzard_TalentUI") -- 0 (Secure)
C_AddOns.GetAddOnSecurity("DebugTools")        -- 1 (Insecure)
C_AddOns.GetAddOnSecurity("Nonexistent")       -- 3 (NotAvailable)

if status == Enum.AddOnSecurityStatus.Secure then ...
```

### `C_AddOns.DoesAddOnExist(indexOrName)`

Returns `true` iff the engine's addon registry has a matching
entry, `false` otherwise. Cheap existence probe used by addons
doing soft-dependency checks.

The implementation goes through the registry directly rather than
dispatching to `GetAddOnInfo` — the engine echoes its input name
back as ret1 unconditionally (before the lookup), so a
`GetAddOnInfo("garbage") ~= nil` heuristic returns true for any
string. This wrapper avoids that.

```lua
C_AddOns.DoesAddOnExist("DebugTools")  -- true
C_AddOns.DoesAddOnExist("garbage")     -- false
```

### `C_AddOns.GetAddOnOptionalDependencies(indexOrName)`

Returns the addon's `## OptionalDeps:` names as multiple return
values, in declared order — the counterpart to the stock
`GetAddOnDependencies` (required deps), which vanilla exposes but
never gave an optional-deps equivalent. An addon with no
`## OptionalDeps:` field returns nothing.

Arg and error handling match stock `GetAddOnDependencies`: a
numeric index out of range (or a non-string/non-number argument)
raises the usage error; an unknown *name* string returns nothing
without erroring.

```lua
-- ## OptionalDeps: Atlas, pfQuest
C_AddOns.GetAddOnOptionalDependencies("MyAddon")  -- "Atlas", "pfQuest"
C_AddOns.GetAddOnOptionalDependencies("Atlas")    -- (nothing — no OptionalDeps)
```

## AuctionHouse

### `C_AuctionHouse.PostItem(itemLocation, duration, quantity, numStacks, bid, buyout)`

Posts `numStacks` auctions of `quantity` each, from a single source stack,
in one call — a ClassicAPI extension modelled on the retail multi-sell flow.
Vanilla's `StartAuction` only ever posts one whole item object, so this
handles the splitting and the sequence of posts for you.

- `itemLocation` — a `{ bagID = B, slotIndex = S }` table identifying the
  source stack (0 = backpack, 1–4 = equipped bags; `slotIndex` 1-based).
- `duration` — `1`/`2`/`3` for 2h/8h/24h (vanilla's three durations); the raw
  minute values `120`/`480`/`1440` are also accepted.
- `quantity` — items per auction (1–255).
- `numStacks` — number of auctions to post.
- `bid` — minimum bid **per stack**, in copper (≥ 1).
- `buyout` — buyout **per stack**, in copper (`0` / omitted = no buyout).

Returns `true` when the job is accepted and posting has started, or
`false, reason` on immediate rejection (AH not open, source empty, not enough
items for `numStacks × quantity`, no free general bag slot to split into, a
job already in progress, or bad arguments).

**It's asynchronous.** Vanilla's `CMSG_AUCTION_SELL_ITEM` has no count field
(unlike WotLK, where the server splits) — the server posts the *entire* item
object a GUID points at. So to post a partial `quantity` the DLL first splits
that amount into a bag slot (a server round-trip), then posts the resulting
stack (another round-trip), repeating for each stack. Progress is reported
through events rather than the return value:

| Event | Payload | When |
|-------|---------|------|
| `AUCTION_MULTISELL_START` | — | job accepted, posting begins |
| `AUCTION_MULTISELL_UPDATE` | `postedCount, totalCount` | after each stack lands (final one has `postedCount == totalCount`) |
| `AUCTION_MULTISELL_FAILURE` | — | aborted (source changed under it, no free slot, AH closed, or a step timed out ~10s) |

Splitting reuses **one** general-purpose bag slot (each post empties it for
the next split); the final stack posts the source object directly with no
split. Posting a whole existing stack (`quantity` == the source count,
`numStacks` 1) needs no free slot at all. Every individual post goes through
the engine's own `StartAuction`, so its validation (damaged-item reject,
buyout normalization, duration check) applies to each stack.

```lua
-- Watch progress
local f = CreateFrame("Frame")
for _, e in ipairs({"AUCTION_MULTISELL_START", "AUCTION_MULTISELL_UPDATE",
                    "AUCTION_MULTISELL_FAILURE"}) do f:RegisterEvent(e) end
f:SetScript("OnEvent", function() print(event, arg1, arg2) end)

-- At an open auction house: post 3 stacks of 5, 2h, 100 bid / 200 buyout each
C_AuctionHouse.PostItem({ bagID = 0, slotIndex = 1 }, 1, 5, 3, 100, 200)
```

Limitations (v1): the source is the single stack at `itemLocation` (stacks
aren't aggregated across bags — `quantity × numStacks` must fit in that one
stack); temp splits use general-purpose bags only, so family-restricted items
(arrows/shards that live in specialty bags) aren't supported; one job runs at
a time.

## CharacterList

### `GetSavedCharacterOrder(realm)` / `SetSavedCharacterOrder(realm, order)` — GlueXML only

Persists a user-chosen ordering for the character-select list, per realm,
per account. Designed for GlueXML mods that add drag-to-reorder to the
character-select screen — but since the storage is a plain text file
you can also pre-populate it by hand.

> **Only callable from the login / character-select screens** (the
> "glue" Lua state). Calling them from an in-world addon errors with
> "attempt to call nil" — they aren't registered there.

- `GetSavedCharacterOrder(realm)` — returns the saved order for
  `realm` as a string, or `""` if none has been saved. Never `nil`.
- `SetSavedCharacterOrder(realm, order)` — writes the order string.
  Pass `""` to clear the saved entry for that realm.

The `order` value is a pipe-delimited list of character names, e.g.
`"Thrall|Jaina|Sylvanas"`. Names are matched case-sensitively against
what `GetCharacterInfo` returns.

#### Storage file

Saved to `WTF\Account\<ACCOUNT>\ClassicAPI.txt`, one line per realm:

```ini
CharacterOrder.Octo=Thrall|Jaina|Sylvanas
CharacterOrder.AnotherRealm=Foo|Bar
```

`<ACCOUNT>` is the account name the launcher logged in with
(uppercase, the same folder name WoW uses for SavedVariables). The
realm key on each line is whatever string was passed as `realm` —
typically the result of `GetServerName()`. This file is shared with
other ClassicAPI account-scoped settings, so don't be surprised to
see unrelated `Key=Value` lines in it; leave them alone.

You can pre-populate the file by hand if you don't have a GlueXML mod
that calls `SetSavedCharacterOrder`. Just create or edit
`ClassicAPI.txt` in the correct account folder before launching the
game — any GlueXML code that reads `GetSavedCharacterOrder(realm)`
will see your saved order.

#### Addon example

```lua
-- GlueXML, e.g. CharacterSelect.lua: persist the user's drag-reordered list
local realm = GetServerName() or ""
local names = {}
for i = 1, GetNumCharacters() do
    table.insert(names, (GetCharacterInfo(translationTable[i])))
end
SetSavedCharacterOrder(realm, table.concat(names, "|"))

-- On the next CHARACTER_LIST_UPDATE: read the saved order back
local saved = GetSavedCharacterOrder(realm)
if saved ~= "" then
    for name in string.gfind(saved, "([^|]+)") do
        -- reconcile against the current GetCharacterInfo names
    end
end
```

## Chat

### `GetCurrentChatGUID()`

Returns the sender's GUID for whichever `CHAT_MSG_*` event is
currently being dispatched, or `nil` if called outside a chat
event or for a synthetic chat with no real sender (e.g.
`CHAT_MSG_SYSTEM`).

Format matches `UnitGUID`: `"0xHHHHHHHHLLLLLLLL"` (16 hex digits,
hi dword first).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("CHAT_MSG_CHANNEL")
f:RegisterEvent("CHAT_MSG_SAY")
f:RegisterEvent("CHAT_MSG_WHISPER")
f:SetScript("OnEvent", function()
    local guid = GetCurrentChatGUID()
    if guid then
        local _, class, _, race = GetPlayerInfoByGUID(guid)
        DEFAULT_CHAT_FRAME:AddMessage(string.format(
            "%s: %s [%s %s]", arg2, arg1, class or "?", race or "?"))
    end
end)
```

Vanilla 1.12's `CHAT_MSG_*` events don't include the sender GUID
in their payload — that was added in 3.0+ as `arg12`. Addons that
need to identify chatters reliably (rather than by sender name,
which is locale-fragile and ambiguous across realms) currently
strcmp against an addon-maintained name cache. This function lets
them skip that work.

**Implementation**: hooks the engine's chat dispatcher at
`FUN_CHAT_DISPATCH` (`0x0049A870`), which receives the sender
GUID as args 11/12 of a 12-arg `__fastcall`. The hook stashes the
GUID into a static global before forwarding to the original
function; the engine then fires the appropriate `CHAT_MSG_*`
event synchronously inside that window, so `GetCurrentChatGUID()`
called from the addon's OnEvent sees the right GUID. The global
is restored to its prior value (not cleared) on hook return, so
nested chat dispatch (e.g. an addon calling `SendChatMessage`
from its handler) doesn't lose the outer context's GUID.

The function returns `nil` rather than `"0x0000000000000000"`
for synthetic chat (system messages, login banners, etc.) where
the GUID args are zero — matches the idiomatic
`if GetCurrentChatGUID() then` check.

## Class

### `FillLocalizedClassList(table [, isFemale])`

Fills the passed-in table with `[classToken] = localizedClassName`
pairs for every class in `ChrClasses.dbc`, and returns the same
table for chaining.

The table is mutated in place. Existing keys are overwritten;
unrelated keys are preserved.

```lua
local classes = FillLocalizedClassList({})
-- classes.WARRIOR = "Warrior"
-- classes.MAGE    = "Mage"
-- classes.PRIEST  = "Priest"
-- ...
```

Modern API supports an optional `isFemale` boolean to fetch female-
form names. Vanilla 1.12 has no separate female-name array in
`ChrClasses.dbc` — `Name[9]` (one localized string per locale)
sits exactly between offsets `+0x14` and `+0x38`, with the class
token immediately after, leaving no room. The arg is accepted for
signature parity but ignored; the same names are returned either way.
Most locales (English included) wouldn't differentiate the two
anyway, so callers won't typically notice.

Sparse class IDs (vanilla skips classID 6 — Death Knight didn't
exist yet — and a few others) have NULL records and are silently
skipped.

## ColorUtil

The modern `C_ColorUtil` color-space and text-color-code helpers. All are
pure functions (no engine state); conventions match a retail-family client
that ships them natively, verified in-game.

**Ranges:** hue is in **degrees**, `[0, 360)`; saturation / value / lightness
are `[0, 1]`. RGB channels are `[0, 1]` (not 0–255). Achromatic (gray) inputs
return a hue of **`-1`** — a sentinel, not `0` — which the reverse conversions
accept as "no hue" (equivalent to `saturation == 0`).

The `color` argument is a table with `r`, `g`, `b` fields in `[0, 1]`
(`ColorMixin`-shaped). An optional `a` is honored (default `1`), though the
color code always encodes 8 hex digits with alpha.

### `C_ColorUtil.ConvertRGBToHSV(r, g, b)`

Returns `(h, s, v)`. Achromatic input → `h == -1`.

```lua
C_ColorUtil.ConvertRGBToHSV(0, 1, 0)         -- 120, 1, 1   (pure green)
C_ColorUtil.ConvertRGBToHSV(0.5, 0.5, 0.5)   -- -1, 0, 0.5  (gray)
```

### `C_ColorUtil.ConvertHSVToRGB(h, s, v)`

Returns `(r, g, b)`. A negative hue (or `s == 0`) is treated as achromatic
and yields `r == g == b == v`.

```lua
C_ColorUtil.ConvertHSVToRGB(120, 1, 1)   -- 0, 1, 0
```

### `C_ColorUtil.ConvertHSVToHSL(h, s, v)`

Returns `(h, s, l)`. Hue passes through unchanged.

### `C_ColorUtil.ConvertHSLToHSV(h, s, l)`

Returns `(h, s, v)`. Hue passes through unchanged.

### `C_ColorUtil.ConvertHSLToRGB(h, s, l)`

Returns `(r, g, b)`. A negative hue (or `s == 0`) is achromatic → `r == g ==
b == l`.

### `C_ColorUtil.GenerateTextColorCode(color)`

Returns the bare 8-hex color string `"AARRGGBB"` (alpha first), **not**
including the `|c` escape.

```lua
C_ColorUtil.GenerateTextColorCode({r = 1, g = 0, b = 0})   -- "ffff0000"
```

### `C_ColorUtil.WrapTextInColor(text, color)`

Wraps `text` in the color's escape sequence: `"|c" .. AARRGGBB .. text ..
"|r"`.

```lua
C_ColorUtil.WrapTextInColor("Hi", {r = 1, g = 0, b = 0})   -- "|cffff0000Hi|r"
```

### `C_ColorUtil.WrapTextInColorCode(text, colorCode)`

Wraps `text` in the given bare color code: `"|c" .. colorCode .. text ..
"|r"`. Pair with `GenerateTextColorCode` (which produces `colorCode`).

```lua
local code = C_ColorUtil.GenerateTextColorCode({r = 1, g = 0, b = 0})
C_ColorUtil.WrapTextInColorCode("Hi", code)   -- "|cffff0000Hi|r"
```

## Combat

### `InCombatLockdown()`

Always returns `false`. Combat lockdown gates secure-frame UI
manipulation in modern WoW, and the secure-frame system didn't exist
in 1.12 — there's nothing in vanilla to lock down. This function is
provided as a no-op stub purely so addons backported from later
expansions can call it without erroring on a missing global.

For "is the player actually in combat?", use vanilla's own
`UnitAffectingCombat("player")`, which the stock 1.12 engine ships.

```lua
if not InCombatLockdown() then
    -- always true in 1.12 — falls through unconditionally
end

if UnitAffectingCombat("player") then
    -- this is the real check
end
```

## Console

### `ExportInterfaceFiles art|code` (console command)

A **developer-console command** (not a Lua function) — type it into the
`~` console, which is available when the client is launched with
`-console`. It extracts Blizzard's stock UI files out of the game's MPQ
archives onto disk, mirroring the same-named command from later clients
(4.3.4). Useful for reading the FrameXML / GlueXML source you're
backporting addons against, or pulling out the default art.

- `ExportInterfaceFiles code` — writes `.lua` / `.xml` / `.toc` /
  `.xsd` files to `BlizzardInterfaceCode\…`
- `ExportInterfaceFiles art` — writes `.blp` / `.tga` files to
  `BlizzardInterfaceArt\…`

Files are written relative to the client's working directory (next to
`WoW.exe`), preserving their `Interface\…` subtree under the
destination folder — e.g. `Interface\FrameXML\FrameXML.toc` becomes
`BlizzardInterfaceCode\FrameXML\FrameXML.toc`. On completion the command
prints a `wrote N file(s)` line back to the console.

```
> ExportInterfaceFiles code
ExportInterfaceFiles: wrote 1234 code file(s) to BlizzardInterfaceCode\
```

Notes:

- It enumerates the mounted MPQs' `(listfile)`, so it surfaces whatever
  the archives actually ship — including Blizzard's own UI addons under
  `Interface\AddOns\Blizzard_*` (AuctionUI, TalentUI, TradeSkillUI,
  etc.), which are part of the stock UI source. Files present in
  multiple archives (base + patches) are written once.
- Your own loose, on-disk addons are *not* exported — they aren't in
  any archive's listfile, so they're never enumerated.
- It runs synchronously and briefly freezes the client while it walks
  `Interface\` — expected for a one-shot extraction.

### `ExportDBCFiles` (console command)

A ClassicAPI-original companion to `ExportInterfaceFiles` that dumps
every `.dbc` table the client loads to `DBFilesClient\` under the working
directory (next to `WoW.exe`). No subcommand — there's only one thing to
export.

```
> ExportDBCFiles
ExportDBCFiles: wrote 155 .dbc file(s) to DBFilesClient\
```

Unlike the MPQ `(listfile)` alone (which omits ~18 of the tables the
client actually loads — mostly cosmetic animation/sound/lookup DBCs that
Blizzard left out of the index), this unions two sources for a complete
set:

1. the **MPQ `(listfile)`** under `DBFilesClient\` — catches files that
   ship in the archives without a loader (e.g. `wowerror_strings.dbc`);
2. a **`.text` scan for the DBC path-getter pattern**
   (`mov eax, &"DBFilesClient\X.dbc"; ret`) — the authoritative list of
   what this build loads. The master DBC init (`FUN_0053f8b0`) is
   straight-line with no iterable name table, so the per-DBC getters
   *are* the registry; scanning for them is the same technique that
   built [docs/DBCs.md](DBCs.md).

Results are deduped case-insensitively. Handy for a local DBC dump to
inspect schemas/values directly — e.g. verifying a record's column
layout against a known row, the way `C_Item.GetEnchantInfo`'s effect
columns were confirmed — without a standalone MPQ extraction tool.

## Container

### `C_Container.GetContainerItemID(bagIndex, slotIndex)`

Returns the itemID at the given bag/slot, or `nil` if the slot is empty
or the indices are out of range. Modern positional-arg form of the same
lookup `C_Item.GetItemID({bagID=B, slotIndex=S})` performs.

- `bagIndex = 0` — the player's main backpack.
- `bagIndex = 1..4` — the player's equipped bag slots.
- `slotIndex` — 1-based, capped at the bag's actual slot count (the
  engine's `PackBagSlot` rejects out-of-range slots and returns nil
  cleanly).

```lua
for slot = 1, 16 do
    local id = C_Container.GetContainerItemID(0, slot)
    if id then
        local _, type, subtype = C_Item.GetItemInfoInstant(id)
        -- ...
    end
end
```

### `C_Container.GetContainerItemInfo(containerIndex, slotIndex)`

Returns a `ContainerItemInfo` table for the item in the given bag slot, or
`nil` if the slot is empty / the indices are out of range. The modern
structured-table form (namespaced + table-returning since Patch 10.0.2) of
vanilla's flat global `GetContainerItemInfo`, which only returned
`texture, itemCount, locked, quality, readable`.

- `containerIndex = 0` — main backpack; `1..4` — equipped bag slots.
- `slotIndex` — 1-based.

Table fields:

| Field | Type | Notes |
|-------|------|-------|
| `iconFileID` | string | Icon **path** (1.12 has no fileID system, same as `GetItemIcon`). Absent if the item's static data isn't cached yet. |
| `stackCount` | number | Current stack size. |
| `isLocked` | boolean | Item is in a pending transaction (pickup/trade/mail in flight). |
| `quality` | number\|nil | `Enum.ItemQuality` (0=Poor … 5=Legendary); `nil` until the item is cached. |
| `isReadable` | boolean | Has readable page text (books/letters). |
| `hasLoot` | boolean | Static LOOTABLE flag (best-effort — vanilla can't track post-loot emptiness client-side). |
| `hyperlink` | string | Fully-decorated per-instance item link (enchant + random suffix), same as `GetContainerItemLink`. |
| `isFiltered` | boolean | Always `false` — vanilla has no bag search-filter system. |
| `hasNoValue` | boolean | `true` when the item's vendor sell price is 0. |
| `itemID` | number | Item ID. |
| `isBound` | boolean | Soulbound. |
| `itemName` | string | Per-instance decorated display name (falls back to the base name). |

Live per-instance fields (`stackCount`, `isLocked`, `isBound`, `itemID`,
`hyperlink`) are always present; cache-derived fields (`iconFileID`,
`quality`, `hasLoot`, `hasNoValue`, `itemName`) are omitted/`nil` until the
item's static data arrives (listen for `GET_ITEM_INFO_RECEIVED`). Passive —
does not warm the item cache.

```lua
local info = C_Container.GetContainerItemInfo(0, 1)
if info then
    print(info.itemName, info.stackCount, info.quality, info.hyperlink)
end
```

### `C_Container.HasContainerItem(bagIndex, slotIndex)`

Returns `true` if the given bag slot currently holds an item, `false`
otherwise (empty slot or out-of-range indices). The occupancy sibling of
`GetContainerItemID` — same slot resolution, but a plain presence check.

- `bagIndex = 0` — the player's main backpack; `1..4` — equipped bag slots.
- `slotIndex` — 1-based.

Presence only: it does **not** require the item's static data to be cached,
so it returns `true` for a just-looted item even before `GetContainerItemID`
/ `GetItemInfo` resolve for it.

```lua
if C_Container.HasContainerItem(0, 1) then
    -- backpack slot 1 is occupied
end
```

### `GetItemCooldown(itemInfo)` / `C_Container.GetItemCooldown(itemID)`

Returns `(startTime, duration, enable)` for the cooldown of the
spell triggered by the item's ON_USE effect. Direct-by-ID variant of
vanilla's `GetContainerItemCooldown(bag, slot)` — no slot reference
required.

```lua
-- Hearthstone (6948), right after using it:
/dump GetItemCooldown(6948)
-- 732120.014, 3600, 1  (startTime in GetTime() seconds, 60 min duration, ready)

-- Item with no ON_USE effect (or invalid itemID) returns all zeros.
/dump GetItemCooldown(2589)   -- Linen Cloth → 0, 0, 0
```

| Field | Notes |
|-------|-------|
| `startTime` | `GetTime()`-compatible seconds when the cooldown started. `0` if no cooldown. |
| `duration` | Cooldown length in seconds. `0` if no cooldown. |
| `enable` | `1` for "ready or counting down" (normal state); `0` for "used but cooldown hasn't started yet" (the potion-in-combat case). |

**Input shapes**:

- `GetItemCooldown(itemInfo)` — accepts itemID, `item:N` / chat-link
  hyperlink, numeric string, or item name (resolved via the shared
  `Item::Arg` helper, same chain `C_Item.GetItemCount` etc. use).
- `C_Container.GetItemCooldown(itemID)` — modern signature accepts
  number / hyperlink but **not** spell name (per Blizzard's spec
  "will not accept an itemlink or name", but link parsing falls out
  of the shared `Item::Arg::Resolve` for free, so we accept it).

Routes through `FUN_ITEM_QUERY_COOLDOWN` (`0x006E2ED0`) which finds
the item's ON_USE spell slot in its `ItemStats_C` record and queries
that spell's cooldown via the same manager player-spell cooldowns
use.

### `C_Container.GetContainerItemDurability(containerIndex, slotIndex)`

Bag/bank variant of [`GetInventoryItemDurability`](#getinventoryitemdurabilityinvslot).
Same `(current, maximum)` return shape and the same "nothing for items
without durability" rule.

```
current, maximum = C_Container.GetContainerItemDurability(containerIndex, slotIndex)
```

- `containerIndex = 0` — the player's main backpack.
- `containerIndex = 1..4` — the player's equipped bag slots.
- `containerIndex = -1, 5..11` — bank-frame slots, only addressable
  while the bank window is open.
- `slotIndex` — 1-based, capped at the bag's actual slot count.

```lua
for slot = 1, GetContainerNumSlots(0) do
    local cur, max = C_Container.GetContainerItemDurability(0, slot)
    if cur then
        -- item at backpack slot has durability
    end
end
```

Goes through the same bag-resolve chain
[`C_Container.GetContainerItemID`](#c_containergetcontaineritemidbagindex-slotindex)
uses (engine's `PackBagSlot` → `GetItemBySlot`), then reads the same
durability fields off the descriptor.

### `C_Container.GetContainerItemRepairCost(containerIndex, slotIndex)`

Bag/bank variant of
[`GetInventoryItemRepairCost`](#getinventoryitemrepaircostinvslot).
Same `copperCost` single-int return and the same "0 for nothing to
repair" semantics.

```
copperCost = C_Container.GetContainerItemRepairCost(containerIndex, slotIndex)
```

`containerIndex` accepts the same values as
[`C_Container.GetContainerItemDurability`](#c_containergetcontaineritemdurabilitycontainerindex-slotindex)
(`0` = backpack, `1..4` = equipped bags, `-1`/`5..11` = bank, etc.).

```lua
for slot = 1, GetContainerNumSlots(0) do
    local cost = C_Container.GetContainerItemRepairCost(0, slot)
    if cost > 0 then
        -- broken/damaged item sitting in the backpack
    end
end
```

Useful for "repair only items above N copper" smart-repair logic
without scanning tooltips. ClassicAPI addition; modern WoW has no
direct equivalent.

### `C_Container.GetContainerItemCharges(containerIndex, slotIndex)`

Per-slot equivalent of
[`C_Item.GetItemCount`](#c_itemgetitemcountitem-includebank-includecharges)'s
`includeCharges=true` mode — returns the total uses available in
*this single slot*, where `GetItemCount` totals the same value
across every matching slot.

```
uses = C_Container.GetContainerItemCharges(containerIndex, slotIndex)
```

`containerIndex` accepts the same values as
[`C_Container.GetContainerItemDurability`](#c_containergetcontaineritemdurabilitycontainerindex-slotindex)
(`0` = backpack, `1..4` = equipped bags, `-1`/`5..11` = bank, etc.).
`slotIndex` is 1-based.

Math is `stack * usesPerItem`, where `usesPerItem` is
`abs(SPELL_CHARGES[0])` for items with a negative charges field
(consume-on-use: wands, healthstones, mana gems, sapper charges)
and `1` for everything else. Worked examples:

| Slot contents | Stack | Raw charges | Returns |
|---|---:|---:|---:|
| Wand of Decay at 50 charges | 1 | -50 | **50** |
| Wand at 1 charge | 1 | -1 | **1** |
| Healthstone | 1 | -1 | **1** |
| Stack of 20 water | 20 | -1 | **20** |
| Stack of 5 mana potions | 5 | -1 | **5** |
| Hearthstone (cooldown-only) | 1 | 0 | **1** |
| Any other item in the slot | 1 | 0 | **1** |

Returns `nil` only when the slot is empty.

```lua
-- "how many charges does my wand have left?"
local charges = C_Container.GetContainerItemCharges(0, 1)
print("Wand charges remaining: " .. charges)

-- "how many drinks across the bags?" — same as GetItemCount(water, false, true)
local total = 0
for bag = 0, 4 do
    for slot = 1, GetContainerNumSlots(bag) do
        if C_Container.GetContainerItemID(bag, slot) == waterID then
            total = total + C_Container.GetContainerItemCharges(bag, slot)
        end
    end
end
```

ClassicAPI addition; modern WoW has no direct equivalent (modern
addons read charges off tooltip text). Useful when you need a
per-slot number rather than `GetItemCount`'s rollup — e.g. to
display "X charges" on the slot UI for a wand without re-walking
every other matching item.

### `C_Container.GetContainerNumFreeSlots(bagID)`

Returns the number of empty slots in the given bag, plus the bag's
`BagFamily` bitfield (the type of items the bag is restricted to).

```
numberOfFreeSlots, bagType = C_Container.GetContainerNumFreeSlots(bagID)
```

- `bagID = 0` — the player's main backpack. Always reports
  `(freeCount, 0)` — the backpack is unfamilied (general-purpose).
- `bagID = 1..4` — the player's equipped bag slots. Slot count and
  bagType come from the bag item's cached `ItemStats_C` record —
  `m_containerSlots` and `m_bagFamily` respectively. If no bag is
  equipped (or the item somehow isn't cached): `(0, 0)`.
- Other `bagID` values (bank, keyring, out-of-range): `(0, 0)`. Always
  returns two values, never nil.

```lua
local free, bagType = C_Container.GetContainerNumFreeSlots(0)
-- free = number of empty slots in backpack, bagType = 0

for bag = 0, 4 do
    local free = C_Container.GetContainerNumFreeSlots(bag)
    -- ...
end
```

> **`bagType` is derived for specialty bags.** Bags leave the raw
> BagFamily field empty in Turtle's data (Soul Bag, Herb Bag, quiver,
> ammo pouch, … all read 0). We fall back to deriving the family from the
> bag's class + subclass, so a Soul Bag reports `0x4` here, a quiver
> `0x1`, an ammo pouch `0x2` — the same values
> [`C_Item.GetItemFamily`](#c_itemgetitemfamilyitem) reports for both
> the bag and the items it holds. This also means
> `CalculateTotalNumberOfFreeBagSlots` now correctly excludes specialty
> bags from the general-purpose free-slot total (it previously counted
> them, since the raw field was 0). Note the Turtle custom families
> (Meat/Fish/Leather/Mining, `0x200`–`0x1000`) collide with retail's
> bit meanings — see the `GetItemFamily` family table for the caveat.

> **The bitmask encoding matches modern.** `bagType` is the bit
> position (`1 << (familyID - 1)`), not the raw 1.12-stored familyID,
> so callers can bitwise-AND with itemFamily values from
> [`C_Item.GetItemFamily`](#c_itemgetitemfamilyitem) directly. We
> convert internally — see that function's notes for the
> encoding-shift story.

Implementation walks the bag using
[`Item::Location::ResolveBag`](#c_containergetcontaineritemidbagindex-slotindex)
internally and counts slots that resolve to a null `CGItem *` (i.e.,
empty). Cross-checked in-game against a manual
`C_Container.GetContainerItemID` walk; counts match.

### `C_Container.CalculateTotalNumberOfFreeBagSlots()`

Returns the total number of free slots across the player's
**general-purpose** bags — backpack plus any equipped bags 0..4 whose
`BagFamily` is 0. Specialty bags (quivers, soul bags, herb bags, …) are
excluded, matching FrameXML's implementation:

```lua
local total = 0
for bag = 0, 4 do
    local free, family = C_Container.GetContainerNumFreeSlots(bag)
    if family == 0 then total = total + free end
end
-- total == C_Container.CalculateTotalNumberOfFreeBagSlots()
```

Takes no arguments.

> **Same sparse-`BagFamily` caveat** as
> [`GetContainerNumFreeSlots`](#c_containergetcontainernumfreeslotsbagid):
> because vanilla server data leaves most bags' family field at 0,
> specialty bags that retail would exclude are counted here. The result
> stays consistent with the per-bag `GetContainerNumFreeSlots` values it
> sums.

### `C_Container.IsContainerItemOpenable(containerIndex, slotIndex)`

Positional-arg wrapper for [`C_Item.IsItemOpenable`](#c_itemisitemopenableitemlocation--c_itemisitemopenablebyiditem)
against a bag/slot pair. Same `(isOpenable, canOpen)` tuple — see
the linked section for full semantics. Both returns are `nil` for
empty slots or items whose data hasn't been cached yet.

```lua
-- Sweep main backpack for openables
for slot = 1, GetContainerNumSlots(0) do
    if C_Container.IsContainerItemOpenable(0, slot) then
        -- ...
    end
end
```

Same cache-warming caveat as the underlying `C_Item.IsItemOpenable` —
a freshly-seen item may report `false` for one call while the
`SMSG_ITEM_QUERY_SINGLE` round-trips. Bag-resident items are almost
always already cached.

### `C_Container.PlayerHasHearthstone()`

Returns the itemID of the hearthstone if one is in the player's bags,
or `nil` otherwise.

```
itemID = C_Container.PlayerHasHearthstone()
```

```lua
if C_Container.PlayerHasHearthstone() then
    -- player can hearth
end
```

**Match logic.** An item counts as a hearthstone if **either**:
1. Its itemID is `6948` (the vanilla Hearthstone — fast path, no
   cache lookup needed), **or**
2. Its on-use spell is spell `8690` (the "Hearthstone" cast itself).

Rule 2 lets custom servers (Turtle WoW, etc.) ship reskinned
hearthstone items with different itemIDs and still have them
recognized — as long as their on-use is the Hearthstone spell.
The return is the **actual matched itemID**, not a hardcoded
constant, so code that wants to do something with the found id
(`SetItemRef` for chat-linking, `GetItemInfo` for the item's
name, etc.) gets the right value for any variant.

> **Cache dependency for rule 2.** The on-use-spell check requires
> the item's `ItemStats_C` record to be in the local cache. Items
> currently in the player's bags are always cached (the engine
> pre-fills the cache during bag sync), so the check is reliable
> for this code path. The fallback to rule 1 (vanilla itemID
> equality) covers the moment-of-login window before the cache is
> fully populated.

Walks bags 0..4 via the same chain
[`C_Container.GetContainerItemID`](#c_containergetcontaineritemidbagindex-slotindex)
uses internally. Stops on first match.

### `C_Container.UseHearthstone()`

Locates the hearthstone in bags and uses it. Returns `true` if the
hearthstone was found and the use call dispatched, `false` if no
hearthstone is in bags.

```
used = C_Container.UseHearthstone()
```

```lua
if not C_Container.UseHearthstone() then
    print("No hearthstone in bags!")
end
```

> **`true` doesn't guarantee the cast started.** The return reflects
> "we had a hearthstone to try with and called the engine's
> `UseContainerItem` on it." Whether the cast actually starts depends
> on cooldown, combat, movement, etc. — same downstream rules as
> calling `UseContainerItem(bag, slot)` manually. If you need to
> know whether the hearth completed, listen for the appropriate cast
> events (`SPELLCAST_START` / `SPELLCAST_STOP`).

Internally locates the hearthstone with the same walk
`PlayerHasHearthstone` uses and hands the resulting `CGItem *` pointer
to the engine's by-pointer use primitive at `0x005D8D00` — the same
fallback path `Script_UseContainerItem` ends up at after every special
cursor-mode branch (repair vendor, spell-cast targeting, drop-on-bag)
is skipped. Bypassing the dispatcher avoids the Lua-stack roundtrip
of pushing `(bag, slot)` only for `Script_UseContainerItem` to re-parse
and re-resolve the same item.

### `C_Container.SwapItems(srcBag, srcSlot, dstBag, dstSlot)`

Atomically swaps two bag slots on the server, no cursor involved.
Returns `true` on send, `false` for bad args (missing bag, out-of-range
slot, empty source).

```lua
-- Move slot 3 of bag 1 to slot 7 of the backpack
C_Container.SwapItems(1, 3, 0, 7)

-- Swap two backpack slots
C_Container.SwapItems(0, 1, 0, 2)
```

Bag IDs match the rest of `C_Container.*`:

- `0` — backpack
- `1..4` — equipped bags
- `-1` — main bank (24 slots; **requires the bank window to be open**)
- `5..10` — equipped bank bags (**requires the bank window to be open**)

Slots are 1-based. The engine accepts both directions:

- Same container (e.g. backpack ↔ backpack, bag1 ↔ bag1, bank ↔ bank)
  sends opcode `0x10D` `CMSG_SWAP_INV_ITEM` with the two linear slot bytes.
- Cross-container (e.g. backpack ↔ bag1, bag2 ↔ bank, bank ↔ bank bag)
  sends opcode `0x10C` `CMSG_SWAP_ITEM` with `(srcBag, srcSlot, dstBag, dstSlot)`.

Destination empty becomes a move; destination occupied becomes an
atomic swap.

> **Bank slots require the bank window to be open.** The engine
> doesn't sync bank-side `CGContainer`s or bank inventory entries
> before the first bank interaction, so any bank `EncodeBagSlot`
> path returns false until then. Same constraint as
> `C_Container.GetContainerItemInfo` on bank IDs.

> **ClassicAPI-only.** Modern Classic Era has no direct swap-two-slots
> call — addons there drive the cursor with two `PickupContainerItem`
> calls in sequence. This single-call form bypasses the cursor
> entirely (same path
> [`C_EquipmentSet.UseEquipmentSet`](#c_equipmentsetuseequipmentsetsetid)
> uses for its swap chain).

Send is fire-and-forget. Server confirms via the normal
`BAG_UPDATE` / `SMSG_INVENTORY_CHANGE_FAILURE` flow; this call does not
wait for that round-trip.

### `C_Container.MoveItem(srcBag, srcSlot, dstBag, dstSlot, count)`

Atomic split-and-place: move exactly `count` items from src to dst,
no cursor involvement. Returns `true` on send, `false` on bad args
(missing bag, out-of-range slot, empty source, `count < 1`, or
`count > 255`).

```lua
-- Split 5 items off backpack slot 1 into backpack slot 2
C_Container.MoveItem(0, 1, 0, 2, 5)

-- Top up a partial stack by moving 3 items from another stack
C_Container.MoveItem(1, 4, 0, 7, 3)
```

Server semantics are all-or-nothing — vanilla has no partial-move
form:

- `dst` empty → places `count` there (clean split).
- `dst` holds the same item & `dstCount + count ≤ maxStack` → merges
  `count` into dst.
- `dst` holds a different item, or the merge would overflow → server
  rejects entirely (source untouched, `SMSG_INVENTORY_CHANGE_FAILURE`
  fires).

Use this instead of `C_Container.SwapItems` when you want to
consolidate partial stacks — `SwapItems` swaps the whole slots, which
isn't what you want when both slots hold the same item but one has a
partial stack.

Bank IDs (`-1`, `5..10`) work here too with the same bank-window
constraint as `SwapItems`.

> **vs. the modern equivalent.** Modern Classic Era has no direct
> one-call move — addons there string `SplitContainerItem(bag, slot,
> count)` + `PickupContainerItem(dstBag, dstSlot)` together to drive
> the cursor. This bundles the same two-step into one packet
> (`CMSG_SPLIT_ITEM`, opcode 0x10E), so the cursor is never touched.

Send is fire-and-forget (same as `SwapItems`).

## Creature

### `C_CreatureInfo.GetCreatureID(guid)`

Extracts the creature template / NPC ID from a unit GUID. Vanilla
1.12 packs the entry ID directly into bits 24-47 of the 64-bit
GUID for the types that carry one; this function does the shift
and mask so addons don't have to.

```lua
C_CreatureInfo.GetCreatureID(UnitGUID("target"))   -- 1842 for Hogger
C_CreatureInfo.GetCreatureID(UnitGUID("pet"))      -- pet's creature template ID
C_CreatureInfo.GetCreatureID(UnitGUID("player"))   -- nil (no entry on players)
```

Accepts creature GUIDs (`0xF130xxxx…`) and pet GUIDs
(`0xF140xxxx…`). Returns `nil` for:
- non-string input or malformed GUIDs
- player GUIDs — the low 32 bits hold a player ID, not a template
- game-object / dynamic-object / corpse / item GUIDs — modern's
  `C_CreatureInfo` doesn't surface entry IDs for these even though
  the bits are in the same range; addons that need them can shift
  the raw GUID themselves (`(guid >> 24) & 0xFFFFFF`)
- entry IDs of 0 — the engine never assigns 0; treated as "no info"

The standard 16-digit (`"0xHHHHHHHHLLLLLLLL"`) and 8-digit
(`"0xLLLLLLLL"`) GUID-string forms are both accepted, the same forms
the `C_PlayerInfo.GUIDIs*` functions take.

The creatureID this returns is exactly what
[`C_CreatureInfo.GetCreatureInfoByID`](#c_creatureinfogetcreatureinfobyidcreatureid)
takes — `GetCreatureInfoByID(GetCreatureID(UnitGUID("target")))` gives
the target's cached name/type/family/rank.

### `C_CreatureInfo.GetCreatureInfoByID(creatureID)`

Reads the client-side **creature cache** (`creaturecache.wdb`, fed by
`SMSG_CREATURE_QUERY_RESPONSE`) for an NPC/creature by ID and returns a
table — or `nil` if it isn't cached:

```lua
local info = C_CreatureInfo.GetCreatureInfoByID(61332)
-- {
--   creatureID = 61332,
--   name       = "Misthoof Stag",
--   subName    = "",     -- title/subtitle ("" if none)
--   type       = 1,      -- CreatureType: 1=Beast, 7=Humanoid, …
--   family     = 0,      -- CreatureFamily
--   rank       = 0,      -- 0=normal, 1=elite, 2=rareelite, 3=worldboss, 4=rare
--   displayID  = 10957,  -- model display ID
-- }
```

This is a synchronous **peek** — it returns data only for creatures
already cached (seen this session, or loaded from `creaturecache.wdb`
at login), and `nil` otherwise. Unlike `UnitCreatureType`/
`UnitClassification`, which only work on a *live unit token*, this
answers for any creatureID you have cached — even one not currently in
the world.

Reads through the engine's generic cache `_GetRecord` (with no callback,
so it's a pure lookup — no network query) and pulls the fields straight
off the cached data block. Field offsets verified against the binary
(rank read by the engine's classification helper at `[block+0x20]`) and
real `creaturecache.wdb` rows (Misthoof Stag→type 1, Nordrassil
Nymph→type 7 rank 1, Greathorn Hunter→family 26).

For a creature that isn't cached yet, call
[`RequestLoadCreatureByID`](#c_creatureinforequestloadcreaturebyidcreatureid)
first and read it once `CREATURE_DATA_LOAD_RESULT` fires.

### `C_CreatureInfo.RequestLoadCreatureByID(creatureID)`

Asynchronously fetches a creature that isn't in the cache yet — issues
`SMSG_CREATURE_QUERY` and fires **`CREATURE_DATA_LOAD_RESULT(creatureID,
success)`** when the response lands, at which point
[`GetCreatureInfoByID`](#c_creatureinfogetcreatureinfobyidcreatureid)
returns its data. The same shape as
[`C_Item.RequestLoadItemData`](#c_itemrequestloaditemdatabyiditem--c_itemrequestloaditemdataitemlocation).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("CREATURE_DATA_LOAD_RESULT")
f:SetScript("OnEvent", function()
    -- 1.12 passes event args as globals: arg1 = creatureID, arg2 = success
    if arg2 then
        local info = C_CreatureInfo.GetCreatureInfoByID(arg1)
        -- info.name, info.type, info.rank, …
    end
end)

C_CreatureInfo.RequestLoadCreatureByID(10184)   -- Onyxia → fires the event,
                                                --  then GetCreatureInfoByID works
```

Returns `true` if the request was accepted (`false` only on bad input
or a full pending set). If the creature is **already** cached, the
event fires synchronously with `success = true`. On a query that never
resolves it fires once with `success = false` after a timeout.

Implementation: a shared `Cache::QueryLoad` dispatcher hooks the engine's
generic cache response parser (`FUN_00556E20`) **once** and routes by
cache instance — the same hook serves the gameobject cache too. The
creature/gameobject caches use this generic parser; the item and quest
caches have their own, so there's no hook collision. Verified in-game: `RequestLoadCreatureByID(10184)` on
an uncached Onyxia fired the event and populated the cache (type 2
Dragonkin, rank 3 world boss).

### `C_CreatureInfo.GetRaceInfo(raceID)`

Localized race name + non-localized token for a race id, read from
`ChrRaces.dbc` (always loaded, so this works for any id at any time —
no unit token required, unlike `UnitRace`). Returns a `RaceInfo` table,
or `nil` for a non-numeric / non-positive id or one with no row.

```lua
local info = C_CreatureInfo.GetRaceInfo(4)
-- { raceName = "Night Elf", clientFileString = "NightElf", raceID = 4 }
```

| Field | Type | Notes |
|-------|------|-------|
| `raceName` | string | Localized display name (client's active locale). |
| `clientFileString` | string | Non-localized token (`"Human"`, `"NightElf"`, `"Scourge"`) — the same string `UnitRace` returns as its 2nd value, used for texture paths. |
| `raceID` | number | Echo of the input id. |

### `C_CreatureInfo.GetClassInfo(classID)`

Localized class name + locale-independent token for a class id, read
from `ChrClasses.dbc`. Returns a `ClassInfo` table, or `nil` for a
non-numeric / non-positive id or one with no row.

```lua
local info = C_CreatureInfo.GetClassInfo(1)
-- { className = "Warrior", classFile = "WARRIOR", classID = 1 }
```

| Field | Type | Notes |
|-------|------|-------|
| `className` | string | Localized display name (client's active locale). |
| `classFile` | string | Locale-independent token (`"WARRIOR"`, `"MAGE"`) — the same string `UnitClass` returns as its 2nd value, and the key used for class colors / icon coords. |
| `classID` | number | Echo of the input id. |

### `C_CreatureInfo.GetCreatureFamilyInfo(creatureFamilyID)`

Info for a pet/beast family (`CreatureFamily.dbc` row), read from the
always-loaded client DBC. Returns a `CreatureFamilyInfo` table, or `nil`
for a non-numeric / non-positive / unused id.

```lua
local info = C_CreatureInfo.GetCreatureFamilyInfo(27)
-- { id = 27, name = "Wind Serpent",
--   iconFile = "Interface\\Icons\\Ability_Hunter_Pet_WindSerpent" }
```

| Field | Type | Notes |
|-------|------|-------|
| `id` | number | Echo of the input id. |
| `name` | string | Localized family name (client's active locale). |
| `iconFile` | string | Icon **texture path**. Retail returns a numeric fileID here; vanilla's DBC stores the path, so this is a string usable directly with `texture:SetTexture(...)`. `""` for families with no icon (warlock pets: Imp, Voidwalker, Succubus, Felhunter, …). |

### `C_CreatureInfo.GetCreatureFamilyIDs()`

Array of every populated `CreatureFamily.dbc` id, in ascending order.
The id space is sparse (unused rows like 10, 13, 14, 22 are skipped), so
this is the load-bearing piece for iterating families — each element
round-trips with
[`GetCreatureFamilyInfo`](#c_creatureinfogetcreaturefamilyinfocreaturefamilyid).

```lua
local ids = C_CreatureInfo.GetCreatureFamilyIDs()
-- { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 15, 16, ... }
for _, id in ipairs(ids) do
    local fam = C_CreatureInfo.GetCreatureFamilyInfo(id)
    -- fam.name, fam.iconFile
end
```

### `C_CreatureInfo.GetFactionInfo(raceID)`

Faction-group info for a **race** (Alliance or Horde), read from the
client DBCs. Returns a `FactionInfo` table, or `nil` for a non-numeric /
non-positive / unknown race, or a race with no named group.

```lua
local info = C_CreatureInfo.GetFactionInfo(1)   -- Human
-- { name = "Alliance", groupTag = "Alliance" }
C_CreatureInfo.GetFactionInfo(2)   -- Orc  → { name = "Horde", groupTag = "Horde" }
```

| Field | Type | Notes |
|-------|------|-------|
| `name` | string | Localized faction-group name (`"Alliance"`, `"Horde"`, or the locale's translation). |
| `groupTag` | string | Locale-independent tag: `"Alliance"` or `"Horde"`. |

This is the by-`raceID` form of vanilla's existing `UnitFactionGroup(unit)`
(which returns `groupTag, name` for a live unit) — same resolution, no unit
required. It mirrors that engine function's chain exactly: race →
`FactionTemplate` group mask → `FactionGroup.dbc`, selecting the row whose
bit the mask sets and whose localized name is non-empty (that non-empty test
is how the engine skips the always-present `"Player"` bit and the nameless
`"Monster"` row to land on Alliance / Horde).

### `C_CreatureInfo.GetCreatureTypeInfo(creatureTypeID)`

Info for a creature type (`CreatureType.dbc` row), read from the
always-loaded client DBC. Returns a `CreatureTypeInfo` table, or `nil`
for a non-numeric / non-positive / unused id.

```lua
local info = C_CreatureInfo.GetCreatureTypeInfo(7)
-- { id = 7, name = "Humanoid" }
```

| Field | Type | Notes |
|-------|------|-------|
| `id` | number | Echo of the input id. |
| `name` | string | Localized type name (client's active locale). |

The type ids: `1` Beast, `2` Dragonkin, `3` Demon, `4` Elemental, `5`
Giant, `6` Undead, `7` Humanoid, `8` Critter, `9` Mechanical, `10` Not
specified, `11` Totem. Pairs with
[`UnitCreatureTypeID`](#unitcreaturetypeidunit) (id → name) and
[`GetCreatureTypeIDs`](#c_creatureinfogetcreaturetypeids).

### `C_CreatureInfo.GetCreatureTypeIDs()`

Array of every `CreatureType.dbc` id (contiguous `1`..`11` in vanilla),
each round-tripping with
[`GetCreatureTypeInfo`](#c_creatureinfogetcreaturetypeinfocreaturetypeid).

```lua
local ids = C_CreatureInfo.GetCreatureTypeIDs()
-- { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }
```

## Currency

### `GetCoinTextureString(amount [, fontHeight])` / `C_CurrencyInfo.GetCoinTextureString(amount [, fontHeight])`

Returns the amount of copper as a money string with real coin icons —
for example `"12<gold> 34<silver> 56<copper>"`. The icons are inline
`|T…|t` markup over vanilla's `UI-MoneyIcons` sprite sheet, rendered by
ClassicAPI's inline-texture backport. Both names call the same C
function, like retail.

Only the non-zero denominations appear, in gold → silver → copper
order. An `amount` of `0` returns `"0<copper>"`. A negative or
fractional `amount` is clamped to `0` / rounded.

`fontHeight` sets the icon height in UI pixels. When omitted (or `0`),
each coin sizes itself to the font of the fontstring that shows the
string — the retail default.

```lua
GetCoinTextureString(123456)   -- "12g 34s 56c" with coin icons
GetCoinTextureString(5)        -- "5c" with a copper icon
GetCoinTextureString(5, 24)    -- same, with a 24px coin
```

The per-denomination formats are the GlobalStrings
`GOLD_AMOUNT_TEXTURE` / `SILVER_AMOUNT_TEXTURE` /
`COPPER_AMOUNT_TEXTURE`, backported in the embedded `!!!ClassicAPI`
addon's locale layer — a locale can override them.

## CVar

### `C_CVar.GetCVarBool(cvar)`

Returns the cvar's value coerced to a boolean, or `nil` if no cvar
by that name is registered. The `C_CVar` namespace was added in
10.x; vanilla 1.12 only has `GetCVar` which returns a string.

```lua
C_CVar.GetCVarBool("gxMaximize")    -- true if the window is set fullscreen
C_CVar.GetCVarBool("CombatDamage")  -- false if floating damage text is off
C_CVar.GetCVarBool("doesNotExist")  -- nil — distinguishable from false
```

Coercion rules (only applied when the cvar exists):

| Cvar string value | Result |
|---|---|
| `"1"` or any non-zero numeric (`"42"`, `"-3"`, …) | `true` |
| `"true"` (case-insensitive) | `true` |
| `"0"` or empty | `false` |
| Non-numeric, non-`"true"` (`"on"`, `"yes"`) | `false` |
| Cvar name not registered | `nil` |

Implementation reads the cvar registry directly (engine's
`FUN_FIND_CVAR` at `0x0063DEC0`), bypassing `GetCVar`'s Lua-error
path for unknown cvars. The `nil` return lets callers distinguish
"the cvar exists and is falsy" from "no such cvar."

#### Glue-state availability

`C_CVar.GetCVarBool` is also registered on the glue Lua state, and
the engine's stock `GetCVar` / `SetCVar` / `RegisterCVar` /
`GetCVarDefault` — which vanilla 1.12 only exposes in-game — are
mirrored onto glue too. GlueXML patches at the login / realm /
char-select screens can now read and write CVars directly. CVar
storage is process-global, so writes on either state are
immediately visible on the other (a `SetCVar("foo", "1")` from a
GlueXML script will read back as `"1"` on the in-world side, and
vice versa).

## Cursor

### `GetCursorInfo()`

Returns a tuple describing whatever the player has picked up on the
cursor (item, money, spell, macro, merchant slot), or nothing when
the cursor is empty. Reads the same engine globals `CursorHasItem` /
`CursorHasSpell` / `GetCursorMoney` consult, so the answer always
matches what the rest of the engine sees.

Return shape per cursor state:

| Cursor holds | Returns |
|---|---|
| nothing | *nil* |
| a bag item (PickupContainerItem, mail/trade detach with GUID) | `"item"`, `itemID`, `itemLink` |
| a drag-source item (equipment slot, action bar, trade, mail) | `"item"`, `itemID` |
| money (gold-split UI, `PickupPlayerMoney`) | `"money"`, `copperAmount` |
| a spell from the spellbook | `"spell"`, `spellbookSlot`, `bookType`, `spellID` |
| a macro | `"macro"`, `macroIndex` |
| a merchant slot | `"merchant"`, `merchantIndex` |

`bookType` is `"spell"` for player spells, `"pet"` for pet spells.
`spellbookSlot` is the 1-based slot the spell lives at in the
spellbook arrays, found by walking the player/pet books. `0` if the
spellID isn't in either book (rare — passive talent-granted spells).

```lua
local kind, a, b, c = GetCursorInfo()
if kind == "item" then
    print(("cursor item: %d"):format(a))
elseif kind == "money" then
    print(("cursor money: %d copper"):format(a))
elseif kind == "spell" then
    print(("cursor spell: id=%d slot=%d (%s book)"):format(c, a, b))
elseif kind == "macro" then
    print(("cursor macro index: %d"):format(a))
end
```

#### `itemLink` availability

Returned only when the cursor holds a **bag item** (type 1) —
`PickupContainerItem` / `PickupInventoryItem` from a bag slot, mail
or trade detach. The cursor stores the item's GUID in this case, so
we resolve back to the live `CGItem *` and feed it to the engine's
link builder, which produces the full per-instance form including
enchant ID and random-suffix decoration:

```
|cff1eff00|Hitem:16791:255:0:0|h[Silkstream Cuffs]|h|r
|cff1eff00|Hitem:9877:0:767:0|h[Sorcerer Cloak of the Owl]|h|r
```

Not returned for drag-source items (equipment slot, action bar
slot, trade slot, mail attachment — types 5/6/7/9). In those cases
the cursor only stores the itemID (no GUID, no live `CGItem *`), so
the engine's link builder isn't reachable. Derive a basic link from
the itemID via `GetItemInfo` if you need one for display:

```lua
local kind, itemID, link = GetCursorInfo()
if kind == "item" and not link then
    link = (select(2, GetItemInfo(itemID))) or ("item:" .. itemID)
end
```

#### Types we don't surface

The engine has cursor types beyond what `GetCursorInfo` advertises in
modern WoW — pet action drag, trade-slot drag, mail-attach drag,
ability-bar drag from `PickupAction`, etc. These don't map cleanly to
any modern `GetCursorInfo` string, so we return `nil` for them.
Consumers needing to detect those can use the engine-native
`CursorHasItem` / `CursorHasSpell` / `CursorHasMoney` (which already
exist in vanilla) and the source-side `PickupX` calls.

## EquipmentSet

Backports the modern `C_EquipmentSet.*` namespace on top of a
client-side persistent store. Vanilla 1.12 had no equipment-set
functionality at all (Blizzard introduced it in 3.1.2 as
`SaveEquipmentSet` etc., then namespaced it into `C_EquipmentSet` in
Legion) — and even when it shipped natively, the data lived
server-side, synced via `SMSG_EQUIPMENT_SET_LIST`. Vanilla servers
don't speak that opcode and won't ever, so each character's sets are
kept in a per-character file under `WTF\Account\...`. The format
matches what `VanillaMinimapTracking` does for its tracking config.

### Overview & file format

Sets are stored in
`WTF\Account\<account>\<realm>\<character>\ClassicAPI_EquipmentSets.txt`
in a line-oriented, human-readable format:

```
# ClassicAPI Equipment Sets v1
set 1
  name=Tanking
  icon=INV_Shield_06
  slot 1 guid=0x0000000040000123
  slot 2 ignored
  slot 5 guid=0x0000000040000789
set 2
  name=Healing
  icon=Spell_Holy_HolyBolt
  ...
```

Identity is by **item GUID**, snapshotted at `CreateEquipmentSet` /
`SaveEquipmentSet` time. `UseEquipmentSet` searches every player-owned
container for those GUIDs and dispatches pickup→equip pairs for items
that aren't already where they belong. The search reads the
underlying GUID arrays directly (same trick `C_Item.GetItemCount`
uses for its `includeBank=true` path), so bank items resolve **even
without the bank window being open** — the bank gate at
`VAR_BANK_GATE_GUID` only suppresses `GetItemBySlot`, not the
underlying data.

One limitation worth knowing about:

- **Equipping from the bank** is not supported by vanilla's protocol
  — `UseEquipmentSet` skips bank items rather than try and fail.
  Retrieve the items first, then re-run.

Modern's signatures take a numeric `iconFileID`; we accept icon path
strings (e.g. `"INV_Shield_06"`) since vanilla has no fileDataID
system. Same string-or-default fallback semantic as 4.3.4 native.

No cap on the number of sets per character. The full list re-
serializes on every mutation; a corrupted file is harmless (parse
errors leave the in-memory list empty and the next save rewrites the
file from scratch).

> Note: this is a **fresh client-side namespace**, not a polyfill of
> some specific Blizzard build's behavior. The shape mirrors Classic
> Era 1.15.x's `C_EquipmentSet.*` where it can, but anything that
> requires server-side state (cross-character sharing, the
> "Equipment Manager" specialization tab) isn't supported.

### `C_EquipmentSet.CanUseEquipmentSets()`

Returns `true` unconditionally. Vanilla has no banker/feature gate
on equipment-set storage; we ship the feature for every character.
Equivalent to Classic Era's behavior.

### `C_EquipmentSet.GetNumEquipmentSets()`

Returns the count of sets stored for the current character. Loads
the file on first call after login.

### `C_EquipmentSet.GetEquipmentSetIDs()`

Returns a numeric-keyed table of every setID in storage order
(insertion-order; not alphabetical). Empty table when nothing's
saved.

### `C_EquipmentSet.GetEquipmentSetID(name)`

Returns the numeric setID for a set with the given name, or `nil` if
no set by that name exists. Names are exact (case-sensitive, no
trimming).

### `C_EquipmentSet.GetEquipmentSetInfo(setID)`

Returns the nine values modern ships:

```
name, icon, setID, isEquipped,
numItems, numEquipped, numInInventory, numMissing, numIgnored
```

`isEquipped` is `true` when every resolvable item in the set is in
its target slot (missing items don't disqualify — useful so a set
that includes a bank-stored cloak still shows as equipped after you
swap in the rest). `numItems` excludes ignored slots; `numIgnored`
counts them separately. Returns nothing for an unknown setID.

### `C_EquipmentSet.GetIgnoredSlots(setID)`

Returns a numeric-keyed table of slot indices (1..19) that the set
has flagged ignored. Empty table when no slots are ignored. Ignored
slots are recorded **per-set at save time**, by reading the global
`IgnoreSlotForSave` state — not retroactively editable on a saved set.

### `C_EquipmentSet.GetItemIDs(setID)`

Returns a hash table `{ [slot] = itemID }` for every set slot whose
item is currently resolvable. Missing items (GUID stored but client
can't find a CGItem) are omitted because vanilla doesn't keep an
itemID separate from the live CGItem record.

### `C_EquipmentSet.GetItemLocations(setID)`

Returns a hash table `{ [slot] = locationCode }`. Location codes use
the same bit-packed encoding Blizzard's FrameXML
`EquipmentManager_UnpackLocation` decodes (constants in
`Blizzard_FrameXMLBase/Classic/Constants.lua`):

| Bit/field | Meaning |
|-----------|---------|
| `0x00100000` (PLAYER) | Item is in a player slot (equipped or in a player bag) |
| `0x00200000` (BAGS)   | Item is inside a bag (player or bank) |
| `0x00400000` (BANK)   | Item is in the bank (main or bank bag) |
| bits 0..7             | Slot (1-based) within the container |
| bits 8..15            | Bag index — present only when BAGS bit is set |

PLAYER and BANK are **mutually exclusive** in the encoding (the
unpack uses `if player elseif bank`). Bank bags subtract 4 from the
bagID before storing so the field fits cleanly; unpack reverses this
to give back bag IDs 5..10. Composition:

| Location | Bits | Example |
|----------|------|---------|
| Equipped (paperdoll slot 1..19) | `PLAYER | slot` | `0x100007` = legs (slot 7) |
| Backpack / player bag 1..4 | `PLAYER | BAGS | (bag<<8) | slot` | `0x300205` = bag 2 slot 5 |
| Main bank slot 1..28 | `BANK | slot` | `0x400003` = bank slot 3 |
| Bank bag 5..10 | `BANK | BAGS | ((bag-4)<<8) | slot` | `0x600101` = bank bag 5 slot 1 |

Special values returned in the table entry:
- `1` — slot is ignored (`GetIgnoredSlots` lists these)
- `-1` — item is missing (was in the set, can't find now)

The packed codes pass cleanly through Blizzard's
`EquipmentManager_UnpackLocation` if you want to use the
shared-FrameXML helpers in your addon UI.

### `C_EquipmentSet.CreateEquipmentSet(name [, icon])`

Snapshots the player's currently-equipped items into a new set and
returns its setID. Honors `IgnoreSlotForSave` — slots flagged ignored
at call time get the ignored marker instead of the equipped item's
GUID.

Returns the new setID on success. Returns nothing if the name is
empty or already in use.

`icon` defaults to `"INV_Misc_QuestionMark"` if omitted.

### `C_EquipmentSet.SaveEquipmentSet(setID [, icon])`

Overwrites an existing set's contents with the player's currently-
equipped items. Same ignored-slot handling as `CreateEquipmentSet`.
If `icon` is provided it replaces the set's previous icon; otherwise
the icon is left unchanged.

### `C_EquipmentSet.ModifyEquipmentSet(setID, newName)`

Renames an existing set. Fails silently if the new name is empty,
already in use by a different set, or the setID doesn't exist.

### `C_EquipmentSet.DeleteEquipmentSet(setID)`

Removes the set. SetIDs are not reused — the next `Create` call
allocates one higher than any seen.

### `C_EquipmentSet.IgnoreSlotForSave(slot)` / `UnignoreSlotForSave` / `IsSlotIgnoredForSave` / `ClearIgnoredSlotsForSave`

Global "skip this slot the next time `CreateEquipmentSet` or
`SaveEquipmentSet` runs" state, indexed by 1-based slot (1..19).
Persists for the rest of the session; not written to the WTF file.
Use it when building a set that should leave (say) the tabard slot
free — set the ignore flag, then call `CreateEquipmentSet`, then
optionally `ClearIgnoredSlotsForSave()` afterward.

### `C_EquipmentSet.EquipmentSetContainsLockedItems(setID)`

Returns `true` if any item in the set is currently flagged "locked"
by the engine — a pending pickup or use is in flight that
`UseEquipmentSet` would race with. Reads bit 2 (`0x04`) of
`ITEM_FIELD_FLAGS` for each resolvable item in the set.

### `C_EquipmentSet.UseEquipmentSet(setID)`

Walks the set and dispatches one **atomic server-side swap** per item
that isn't already in its target slot. Items in the bank are skipped
silently (vanilla can't equip from bank). Missing items are skipped
silently. Returns `true` if the call ran (the set existed), `false`
otherwise.

Implementation uses the same `FUN_INVENTORY_SWAP` primitive
[`C_Item.EquipItemByName`](#c_itemequipitembynameiteminfo--dstslot)
uses for its explicit-slot path. Each swap is a single
CMSG_SWAP_INV_ITEM (or CMSG_AUTOEQUIP_ITEM) packet that the server
applies atomically — the two-cycle "ring A in slot 11, ring B in
slot 12, set swaps them" case resolves in one packet because the
opcode swaps both slots in a single server transaction. The cursor
is never touched; an item held on the cursor when
`UseEquipmentSet` is called stays on the cursor.

Longer dependency chains (3+ items rotating) are rare with 1.12's
slot set — the realistic conflicts are paired slots (rings 11/12,
trinkets 13/14, weapons 16/17), all 2-cycles.

> **`CURSOR_UPDATE` fires per item moved.** The engine's swap
> primitive (`FUN_005E0C40`) runs a generic cursor-state cleanup at
> the end of each call, which fires `CURSOR_UPDATE` regardless of
> whether the cursor was actually touched. So one
> `UseEquipmentSet` call that moves N items will fire N
> `CURSOR_UPDATE` events in quick succession. Addons that react to
> `CURSOR_UPDATE` (cursor-attached tooltips, drag-state tracking)
> should debounce — or check `CursorHasItem()` before doing work —
> since most of those fires won't reflect a real cursor change.
> This is engine behavior, not a bug in the implementation; the
> old cursor-based path actually fired more (one per pickup, one
> per equip, one per cursor-clear).

## Events

### `C_EventUtils.IsEventValid(eventName)`

Returns `true` if the named event exists in the engine's event-name table
and can be registered for, `false` otherwise. Equivalent to the modern
function of the same name; useful for addons that gate code behind feature
detection (e.g. registering for events that exist in some client builds
but not others).

```lua
if C_EventUtils.IsEventValid("PLAYER_LOGIN") then ... end       -- true
if C_EventUtils.IsEventValid("COMBAT_LOG_EVENT") then ... end   -- false (added in 3.x)
```

The check walks the engine's static event-name table at runtime, so it
sees events added by **any** loaded DLL that injects names by overwriting
`.rdata` strings in place — for example, on a Turtle WoW client running
SuperWoWhook, `IsEventValid("UNIT_CASTEVENT")` returns `true` because
SuperWoWhook patches the binary's event names with its own. This matches
what `frame:RegisterEvent` will actually accept, which is what addon code
needs to know.

### `GetFramesRegisteredForEvent(event)`

Returns the frames currently registered for `event`, as multiple return
values (matching the modern signature — not a table). Returns nothing when
no frame is registered or the event name is unknown.

```lua
local f1, f2 = GetFramesRegisteredForEvent("PLAYER_LOGIN")
for i = 1, select("#", GetFramesRegisteredForEvent("BAG_UPDATE")) do
    local frame = select(i, GetFramesRegisteredForEvent("BAG_UPDATE"))
    print(frame:GetName())
end
```

Reads the engine's own subscriber chain — the exact list
`frame:RegisterEvent` appends to — so it stays in sync with real
registrations, including our `AutoReserve`-backed custom events. Frames
created purely in C++ (nameplates, etc.) come back as their canonical
wrapper, so `:GetName()` and other methods work; an anonymous frame's
`:GetName()` is `nil`, same as on retail.

### `PLAYER_ENTERING_WORLD` payload (`isInitialLogin`, `isReloadingUi`)

Backports the 8.0.1 payload onto vanilla's (natively arg-less)
`PLAYER_ENTERING_WORLD`:

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_ENTERING_WORLD")
f:SetScript("OnEvent", function()
    local isInitialLogin, isReloadingUi = arg1, arg2
    if isInitialLogin then       -- fresh character login (from char-select)
    elseif isReloadingUi then    -- /reload
    else                          -- zone / instance transition
    end
end)
```

- `isInitialLogin` — `true` on a fresh login (including logging out to
  char-select and back in), `nil` otherwise.
- `isReloadingUi` — `true` when the fire is caused by a `/reload`, `nil`
  otherwise.
- Both `nil` on an instance/zone transition — matching retail.

Delivered as real event args (`arg1`/`arg2`), so retail-style handlers work
unchanged. Values are `true`→`1` / false→`nil` (the engine dispatcher has no
boolean), so `if isInitialLogin then …` reads naturally. Existing handlers
that ignore the args are unaffected.

PEW is a no-arg event, so the engine broadcasts it through the
`FUN_FIRE_EVENT_NO_ARGS` dispatcher; we intercept that (via the shared
`Event::SignalHook`) and re-broadcast through the variadic dispatcher with
the payload. The flags are derived from whether the in-game Lua state was
just (re)built (login/`reload`) and whether we just came through the
glue/login screen — no `PLAYER_ENTERING_WORLD`-specific engine hook.

### `PLAYER_TOTEM_UPDATE` event

Backports the TBC event (absent from vanilla's table, so reserved as a
custom event). Fires with `arg1 = totemSlot` (1 Fire, 2 Earth, 3 Water,
4 Air) whenever a totem is dropped, expires, or is destroyed early
(killed / Totemic Recall) — the companion event to
[`GetTotemInfo`](#gettoteminfoslot).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_TOTEM_UPDATE")
f:SetScript("OnEvent", function()
    local slot = arg1
    local have, name, start, duration, icon = GetTotemInfo(slot)
    -- re-read the changed slot
end)
```

Driven by the same [`GetTotemInfo`](#gettoteminfoslot) tracker: the drop
is detected from `SMSG_SPELL_GO`, removal from the WorldTick object-manager
scan, and the event is flushed from the tick (so handlers run in a safe
context, not mid-packet). Removal detection carries up to the tracker's
scan interval (~250 ms) of latency; drops fire on the next frame.

### `BAG_UPDATE_DELAYED` event

Fires (with no payload) once per frame in which any `BAG_UPDATE`
fired. Matches modern WoW's coalescing semantic exactly — register
for `BAG_UPDATE_DELAYED` instead of `BAG_UPDATE` and rescan once
per frame regardless of how many updates fired.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("BAG_UPDATE_DELAYED")
f:SetScript("OnEvent", function() RescanMyInventory() end)
```

A trade with 6 stacks (12 `BAG_UPDATE`s, all processed in one
frame) produces exactly 1 `BAG_UPDATE_DELAYED` at the end of the
frame — matches Classic Era 1.15.x's observed behavior.

Implemented by three hooks, none in regions other DLLs touch:
- `FUN_004F91A0` / `FUN_004F9370` (bag subsystem at `0x004F9xxx`)
  — each just sets a `g_pending` flag, no fire
- `FUN_0066FD50` (engine's per-frame world-subsystem update at
  `0x0066xxxx`, deep in the rendering pipeline, only one caller in
  the entire binary) — drains the flag at the tail of the world
  tick

The world-tick hook is a tail hook (run original first, then fire
DELAYED), so the fire happens after every other per-frame world
work completes. Addon callbacks see DELAYED after their normal
event handlers.

> **Coverage limitation.** The keyring `BAG_UPDATE(-2)` path goes
> through a separate function (`FUN_004F8DB0`) that uses
> `__thiscall` with an awkward register-arg shape. Keyring updates
> currently won't trigger `BAG_UPDATE_DELAYED`. Player bag (0..4)
> and bank (5..10) updates work normally — the 95% case.

### `PLAYER_EQUIPMENT_CHANGED` event

Backport of the WotLK-era event that fires once per paperdoll slot
change — equip, unequip, or swap:

```
PLAYER_EQUIPMENT_CHANGED: equipmentSlot, hasCurrent
```

- **`equipmentSlot`** (number) — the 1-based inventory slot that
  changed (`1` = head … `19` = tabard; same numbering as
  `GetInventoryItemLink("player", slot)`).
- **`hasCurrent`** — `1` when the slot now holds an item, `nil` when
  it's now empty (write `if hasCurrent then` as in modern code —
  vanilla's event dispatcher can't push real booleans).

Vanilla only fires `UNIT_INVENTORY_CHANGED("player")`, which doesn't
say *which* slot changed; this event lets gear trackers, stat sheets,
and tooltip decorators refresh exactly one slot instead of rescanning
all 19.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_EQUIPMENT_CHANGED")
f:SetScript("OnEvent", function()
    print("slot", arg1, arg2 and "equipped" or "now empty")
end)
```

Fully event-driven — no polling. The DLL registers change observers
for the 19 equipment GUID fields of the player descriptor through the
engine's own field-observer system (the same mechanism the engine
uses to watch bag slots), so the event fires exactly when the
server's update packet writes a new item GUID into a slot, once per
changed slot. Swapping two items (e.g. weapon ⇄ bag) fires once per
affected equipment slot.

### `UPDATE_INVENTORY_DURABILITY` event

Backport of the modern event that fires — with no payload — when an
equipped item's durability changes: combat damage, resurrection
penalty, repairs. The standard consumer pattern works unchanged:
rescan the 19 slots with `GetInventoryItemDurability` on each fire.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("UPDATE_INVENTORY_DURABILITY")
f:SetScript("OnEvent", RefreshMyDurabilityHUD)
```

Event-driven, no polling: piggybacks on the engine's own
inventory-alerts recompute — the routine behind vanilla's
`UPDATE_INVENTORY_ALERTS` (the red/yellow "broken armor man"), which
the engine runs whenever an owned item's fields change, on equip
changes, and at enter-world. On each recompute the DLL diffs a
per-slot `{item, durability}` snapshot and fires once when anything
actually changed — a death dinging all your armor produces one event,
not nineteen. Also fires when the *set* of equipped items changes
(swapping to a differently-damaged item updates durability UI, so
consumers want the refresh anyway). Bag-carried items don't trigger
it: vanilla renders durability UI for equipped gear only.

### `HEARTHSTONE_BOUND` event

Fires (with no payload) every time the player binds their
hearthstone at an innkeeper — including a rebind at the same inn.
Polyfills modern WoW's event of the same name — addons listen to
`HEARTHSTONE_BOUND` and re-read `GetBindLocation()` to refresh
whatever bind-location UI they show.

> Note: the server re-sends the bind packet on map/zone transitions
> (behind the loading screen). Those resyncs are suppressed by gating
> on the engine's in-world flag, so the event fires only for an actual
> innkeeper bind, never on a map change.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("HEARTHSTONE_BOUND")
f:SetScript("OnEvent", function()
    local newLocation = GetBindLocation()
    -- update UI...
end)
```

Fires **every time** the player confirms a bind, including when the
new bind is at the same inn as before. The event is driven by the
bind ACTION (server's SMSG_BINDPOINTUPDATE), not by the area name
changing — matches modern semantics.

Implemented by hooking the BINDPOINTUPDATE packet handler at
`FUN_005ED3C0`. The handler runs in two distinct cases:

1. **Initial post-login sync** — the engine has just zeroed the
   "bind valid" flag during character-entry init (`FUN_005E2510`),
   and this packet repopulates it. We detect this by reading the
   flag *before* the original handler runs; if it's `0`, this is
   the sync and we suppress the event.
2. **Innkeeper rebind** — the flag is already `1`. Fire.

Character switch is handled automatically: the character-entry init
re-zeroes the flag, so each new character's first update is also
treated as sync and suppressed.

### Player input-state events

Three pairs of zero-payload events fired from a single per-frame
`Tick::WorldTick` callback that reads input state once and edge-
detects each pair.

| Event | Fires on | Source |
|-------|----------|--------|
| `PLAYER_STARTED_MOVING` / `PLAYER_STOPPED_MOVING` | WASD or autorun toggle | UI-input controller flags at `*0x00BE1148 + 0x04` (`0x10` W \| `0x20` S \| `0x40` A \| `0x80` D \| `0x1000` autorun) |
| `PLAYER_STARTED_TURNING` / `PLAYER_STOPPED_TURNING` | Mouselook bit held AND character body yaw is changing | Mouselook bit `0x01` + per-frame delta on `CGPlayer + 0x9C4` (body yaw, radians) |
| `PLAYER_STARTED_LOOKING` / `PLAYER_STOPPED_LOOKING` | Free-look bit held AND camera-relative yaw is changing | Free-look bit `0x02` + per-frame delta on `[*0x00B4B2BC + 0x65B8] + 0xF0` (camera yaw, radians) |

```lua
local f = CreateFrame("Frame")
for _, ev in ipairs({
    "PLAYER_STARTED_MOVING", "PLAYER_STOPPED_MOVING",
    "PLAYER_STARTED_LOOKING", "PLAYER_STOPPED_LOOKING",
    "PLAYER_STARTED_TURNING", "PLAYER_STOPPED_TURNING",
}) do
    f:RegisterEvent(ev)
end
f:SetScript("OnEvent", function(_, event) print(event) end)
```

**Key-state semantics for MOVING**: `STOPPED_MOVING` fires the
instant the movement keys release, even if the character is still
airborne from a jump. Matches retail (verified empirically). For
an "is the character actually displacing this frame" signal, use
`GetUnitSpeed("player")` and watch the first return.

**Latched semantics for TURNING and LOOKING**: each pair fires
exactly once per RMB / LMB hold. STARTED waits until both the
mouse-button bit is held AND the relevant yaw has actually
changed (matches retail's "camera has moved" semantics — clicking
without dragging doesn't fire). STOPPED fires when the button bit
clears (RMB / LMB release). The latch stays on through any drag-
stop-drag motion within a single hold; no spurious flapping.

The camera-relative yaw at `[camera + 0xF0]` stays at 0 during
RMB-mouselook (camera rotates *with* the character, so its offset
from the character doesn't change), so TURNING and LOOKING are
cleanly separable: RMB only fires TURNING, LMB only fires LOOKING.

Implementation: single subscriber on the shared `Tick::WorldTick`
registry (same `MinHook` on `FUN_0066FD50` shared with
`BAG_UPDATE_DELAYED`). When any of the input controller / player /
camera pointers is null (pre-login, loading screen, character
select), all states reset so the next valid tick doesn't fire
spurious STOPPED transitions.

### `GLOBAL_MOUSE_DOWN` / `GLOBAL_MOUSE_UP` events

Fires on every raw mouse-button press or release while WoW has
focus. Payload is the button identifier string — matches modern
WoW's signature exactly.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("GLOBAL_MOUSE_DOWN")
f:RegisterEvent("GLOBAL_MOUSE_UP")
f:SetScript("OnEvent", function(_, event, button)
    print(event, button)  -- "GLOBAL_MOUSE_DOWN", "LeftButton"
end)
```

| Payload | Source |
|---------|--------|
| `"LeftButton"` | `VK_LBUTTON` |
| `"RightButton"` | `VK_RBUTTON` |
| `"MiddleButton"` | `VK_MBUTTON` |
| `"Button4"` | `VK_XBUTTON1` |
| `"Button5"` | `VK_XBUTTON2` |

Distinct from per-frame `OnMouseDown` scripts — these fire even
when the click misses every UI frame (clicks in the world / on
nothing). Distinct from `PLAYER_STARTED_LOOKING` /
`PLAYER_STARTED_TURNING` — those are camera-rotation events;
these are raw input events.

**Focus-gated.** Polls Win32 `GetAsyncKeyState` per frame on the
shared `Tick::WorldTick` registry. DOWN transitions are only
honored when WoW is the foreground window — clicking in another
app while alt-tabbed doesn't fire. UP transitions always fire,
even if the user alt-tabbed mid-click, so an addon never gets
left in a "button is held" state.

### `EQUIPMENT_SETS_CHANGED` event

Fires (with no payload) after any mutation: `Create`, `Save`,
`Modify`, `Delete`, and the four `*IgnoredSlot*` calls. Addon UI
should re-read its set list / button state when this fires.

### `EQUIPMENT_SWAP_PENDING` event

Fires with a single payload arg — `setID` — at the **start** of
`UseEquipmentSet`, right after the set-exists check passes and
before any pickup/equip work begins. Modern addons use this to
gate "swap in progress" UI state (grey out the set button, show
a spinner, etc.) until `EQUIPMENT_SWAP_FINISHED` arrives.

Doesn't fire if `UseEquipmentSet` is called with an unknown
setID — in that case only `EQUIPMENT_SWAP_FINISHED(false, setID)`
fires.

### `EQUIPMENT_SWAP_FINISHED` event

Fires at the end of every `UseEquipmentSet` call with two payload
args: `success` (1 if the set existed and we dispatched the swap, 0
if the setID was unknown) and `setID`. Note this is "we ran the
dispatch" success — not "every item ended up in its target slot."
Items that were in the bank or that couldn't complete a swap cycle
in one pass still report success=1. Listen for this if you want to
re-paint the character pane / refresh tooltips after a swap.

### `FACTION_STANDING_CHANGED` event

Fires once per reputation change with `(factionID, newStanding, repGained)`:

| arg1 (`factionID`)    | Faction.dbc row ID of the faction whose standing changed |
| arg2 (`newStanding`)  | New total standing value (post-change `barValue`)        |
| arg3 (`repGained`)    | Signed delta — positive on gain, negative on loss        |

Polyfills the modern event of the same name. Vanilla 1.12 exposes only
`CHAT_MSG_COMBAT_FACTION_CHANGE`, whose `arg1` is the localized chat
string (`"Your Stormwind reputation has increased by 100."`) — addons
have to parse the text and reverse-resolve the faction name back to
an ID. This event lets addons skip that work.

Does **not** fire for the initial faction sync at login or `/reload`
— matches modern semantics. Only fires when a real reputation gain or
loss arrives from the server (`SMSG_SET_FACTION_STANDING`).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("FACTION_STANDING_CHANGED")
f:SetScript("OnEvent", function()
    -- in vanilla 1.12 OnEvent receives no args; engine sets `event`
    -- and `arg1..argN` as globals before invoking the handler.
    if event == "FACTION_STANDING_CHANGED" then
        local name = GetFactionInfoByID(arg1)
        print(string.format("%s: %+d  (now %d)", name, arg3, arg2))
    end
end)
```

`repGained` is signed: positive for gains, negative for the rare loss
case (e.g. killing a Goblin Commodity Exchange NPC drops your rep
with Booty Bay).

**Implementation notes**

Hooks the engine's per-rep-change notify dispatcher at `0x0062C5F0`
— the chokepoint called once per `(factionID, signedDelta)` from
the per-slot setter at `0x004D6330`, gated by "value-actually-changed
AND notify-flag-set". The setter has already written the new delta
into the per-slot storage by the time the hook fires, so we read the
total back via `FUN_REPUTATION_GET_STANDING(factionID)` (`0x004D6370`)
to produce the `newStanding` payload.

The hook calls the original before firing our event so the engine's
`CHAT_MSG_COMBAT_FACTION_CHANGE` still fires first — no behavior
change for addons that depended on the chat text. A per-call
snapshot of `(factionID, newStanding, repGained)` is captured before
forwarding, which [`C_Reputation.GetLastStandingChange`](#c_reputationgetlaststandingchange)
exposes — so addons can read the structured payload from inside the
chat event without re-parsing the localized string.

### `LOOT_HISTORY_ROLL_CHANGED` / `LOOT_HISTORY_ROLL_COMPLETE` / `LOOT_HISTORY_FULL_UPDATE` events

Fire as group-loot rolls progress, so a loot-history UI can update without
polling. Register like any engine event
(`frame:RegisterEvent("LOOT_HISTORY_ROLL_COMPLETE")`), then read the item via
[`C_LootHistory.GetItem`](#c_loothistorygetitemitemindex) /
[`C_LootHistory.GetPlayerInfo`](#c_loothistorygetplayerinfoitemindex-playerindex).

| Event | Args | When |
|---|---|---|
| `LOOT_HISTORY_ROLL_CHANGED` | `itemIndex`, `playerIndex` (numbers) | A player rolled or passed on an item. |
| `LOOT_HISTORY_ROLL_COMPLETE` | `itemIndex` (number) | The item was decided — won or all-passed. |
| `LOOT_HISTORY_FULL_UPDATE` | — | The item set changed structurally — a new item appeared (roll opened) or the 128-item ring evicted the oldest (every index shifted). Re-read the whole list. Also fired by `C_LootHistory.Clear()`. |

See the [LootHistory](#loothistory) section for the reconstruction and read API.

### `LOOT_SCAN_COMPLETED` event

Fires (no payload) once a [`C_Loot.ScanNearbyLoot()`](#c_lootscannearbyloot)
call has finished walking every reachable corpse. Handler should call
[`C_Loot.GetLastScanResults()`](#c_lootgetlastscanresults) to read the
collected `{ guid, coin, items }` array.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("LOOT_SCAN_COMPLETED")
f:SetScript("OnEvent", function()
    local results = C_Loot.GetLastScanResults()
    -- ... process results
end)
C_Loot.ScanNearbyLoot()
```

Fires exactly once per `ScanNearbyLoot()` call regardless of
outcome — even if every corpse timed out and the results table is
empty. Doesn't fire spuriously for scans triggered by other addons
(only one scan runs at a time; concurrent attempts return `false`
from `ScanNearbyLoot`). See the Loot section for the full scan flow.

### `LOSS_OF_CONTROL_ADDED` / `LOSS_OF_CONTROL_UPDATE` events

Fire as loss-of-control effects change, so a UI can react without polling.
Register like any engine event
(`frame:RegisterEvent("LOSS_OF_CONTROL_ADDED")`).

| Event | Args | When |
|---|---|---|
| `LOSS_OF_CONTROL_ADDED` | `eventIndex` (number) | A new effect was applied — `eventIndex` is its 1-based index for [`C_LossOfControl.GetActiveLossOfControlData`](#c_lossofcontrolgetactivelossofcontroldataindex). |
| `LOSS_OF_CONTROL_UPDATE` | `unitToken` (string) | The active set changed — an effect was added, fell off, or expired. Re-scan with `GetActiveLossOfControlData`. Always `"player"` (vanilla only tracks the local player), but passed so the event shape matches modern WoW and leaves room for per-unit tracking. |

Detection is a per-frame diff of the active set (effect *expiry* has no engine
packet to hook), so events land within a frame of the change and coalesce
multiple simultaneous changes into a single `LOSS_OF_CONTROL_UPDATE`. A
counterspelled cast, for instance, fires two `ADDED` (the school lockout and,
with Improved Counterspell, the silence) then an `UPDATE` as each expires.
See the [LossOfControl](#lossofcontrol) section for the effect data.

### `MODIFIER_STATE_CHANGED` event

Fires on every modifier-key press and release with `(key, down)`:

| arg1 (`key`) | `LSHIFT`, `RSHIFT`, `LCTRL`, `RCTRL`, `LALT`, `RALT` |
| arg2 (`down`) | `1` on press, `0` on release |

Only transitions fire — key autorepeat does not. Matches 2.4.3+
semantics.

Releases that happen while WoW is **not the focused window** are also
handled: keyboard messages only reach WoW while it has focus, so a
modifier let go in the background is never seen live. On regaining focus
the cached state is reconciled against the true physical key state and
any missed transition fires then (see the focus-regain note below).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("MODIFIER_STATE_CHANGED")
f:SetScript("OnEvent", function()
    -- in vanilla 1.12 OnEvent receives no args; engine sets `event` and
    -- `arg1..argN` as globals before invoking the handler.
    if event == "MODIFIER_STATE_CHANGED" and arg1 == "LSHIFT" then
        print("left shift", arg2 == 1 and "down" or "up")
    end
end)
```

**Implementation notes**

L/R modifier distinction doesn't exist anywhere in the 1.12 engine's
own state — its `IsShiftKeyDown` chain bottoms out at
`GetKeyState(VK_SHIFT)` (the merged virtual key, `0x10`), never the
L/R-aware `VK_LSHIFT` / `VK_RSHIFT`. The OS-level keystate *does* have
the distinction; we capture it by installing a `WH_GETMESSAGE` Win32
thread hook on the engine's message-pump thread, decoding each
`WM_KEY{,SYS}{DOWN,UP}` message (using `MapVirtualKeyA(scancode,
MAPVK_VSC_TO_VK_EX)` to resolve `VK_SHIFT` to L/R and the `KF_EXTENDED`
bit in `lParam` for `VK_CONTROL` / `VK_MENU`), and maintaining a
6-bit cached bitmap that the seven query functions read.

The thread-message hook is per-thread, not per-`HWND` — it survives
renderer-state changes that recreate WoW's main window (e.g. toggling
vertical sync), where an `SetWindowLongPtr`-style `WNDPROC` subclass
would be left dangling.

**Focus-regain reconciliation.** `WM_KEY*` messages are only delivered
while WoW is the foreground window, so a modifier released while WoW is
in the background (alt-tabbed away) produces no message — the cached bit
would stay stuck down and the release event would never fire. A second
thread hook (`WH_CALLWNDPROC`, needed because `WM_ACTIVATEAPP` is *sent*
rather than *posted* and so is invisible to `WH_GETMESSAGE`) watches for
app activation; on regaining focus it polls `GetAsyncKeyState` for each
L/R modifier — which reads global physical key state regardless of focus
— and fires `MODIFIER_STATE_CHANGED` for any bit that changed while WoW
was away. So the release surfaces the moment you tab back, and
`IsLeft/RightShiftKeyDown` etc. read correct state immediately.

> Note: the engine's own `IsShiftKeyDown` / `IsControlKeyDown` /
> `IsAltKeyDown` bottom out at the synchronous `GetKeyState`, which has
> the same background-release blind spot and is *not* reconciled by this
> hook — only ClassicAPI's L/R query functions and the event reflect the
> corrected state. Prefer `IsModifierKeyDown` / the L/R functions if you
> need the un-stuck value.

### `NAME_PLATE_CREATED` / `NAME_PLATE_UNIT_ADDED` / `NAME_PLATE_UNIT_REMOVED` events

Fire when nameplate state actually changes. Payloads:

| Event | `arg1` | Notes |
|-------|--------|-------|
| `NAME_PLATE_CREATED` | nameplate **Frame** | Matches modern WoW. Fires once per unique `CGNamePlateFrame` pointer — same frame re-used via pool recycle does NOT refire. |
| `NAME_PLATE_UNIT_ADDED` | `"nameplateN"` **unit token** | Matches modern WoW. Pass straight to `UnitName` / `UnitGUID` / `UnitClass` / etc., or to [`GetNamePlateForUnit`](#c_nameplategetnameplateforunitunittoken) for the frame. The token is positional — see [Unit tokens](#unit-tokens-nameplaten) for ordering semantics. |
| `NAME_PLATE_UNIT_REMOVED` | `"nameplateN"` **unit token** | Same as above. Computed from the plate's slot *before* it shifts out of the ordered list, so the token still resolves to the leaving unit during the event handler. |

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("NAME_PLATE_CREATED")
f:RegisterEvent("NAME_PLATE_UNIT_ADDED")
f:RegisterEvent("NAME_PLATE_UNIT_REMOVED")
f:SetScript("OnEvent", function()
    if event == "NAME_PLATE_CREATED" then
        -- arg1 = the nameplate Frame itself
        arg1:SetAlpha(0.8)
    elseif event == "NAME_PLATE_UNIT_ADDED" then
        -- arg1 = "nameplate1" / "nameplate2" / ...
        local name = UnitName(arg1)
        local plate = C_NamePlate.GetNamePlateForUnit(arg1)
        -- ... style based on the unit ...
    end
end)
```

> **CREATED timing with nameplate addons (pfUI / TidyPlates / etc).**
> The event fires when the *engine* allocates the underlying
> `CGNamePlateFrame`. Nameplate-mod addons typically decorate the
> frame on their own per-frame update — so `arg1` at `CREATED` time
> is a bare frame with no addon-side decorations yet. For
> unit-specific work after the addon has decorated, use
> `NAME_PLATE_UNIT_ADDED` (fires next tick at the latest) or fetch
> the current frame on-demand via `GetNamePlateForUnit(arg1)`.

> **Token stability gotcha.** Like modern WoW, the `arg1` token is
> positional — `"nameplate3"` today may resolve to a different unit
> after the slot vacates and shifts. If you need a per-unit hash key
> for cross-event bookkeeping, call `UnitGUID(arg1)` and store the
> GUID instead. See [Unit tokens](#unit-tokens-nameplaten) for the
> ordering rules.

**Implementation notes**

Detected by per-frame polling, not engine hooks. Each world tick we
walk the object hash for nameplated units and diff against the
previous tick's snapshot. Modern WoW also synthesizes these via
diffing (the underlying engine has no event for "plate state
changed"). The cost is ~20-50µs/frame even in busy raids — well
below noise.

The diff approach absorbs the engine's transient hide/reshow cycle:
vanilla has ~7 code paths that briefly zero `unit + 0xE60` (z-order
rebuilds, anchor changes, flag-change re-eval) and the next frame's
show path re-allocates from the pool. Those transient zeroes never
become events because the unit appears in both the previous and
current tick's snapshot.

### `PLAYER_FOCUS_CHANGED` event

Fires whenever the player's focus changes — assignment via
[`FocusUnit`](#focusunitunit), clear via [`ClearFocus`](#clearfocus),
or a `FocusUnit(token)` call where the token resolves to no GUID
(implicit clear). No payload args; the new focus GUID is read off
[`UnitGUID("focus")`](#unitguidunit) in the handler.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_FOCUS_CHANGED")
f:SetScript("OnEvent", function()
    local guid = UnitGUID("focus")
    if guid then
        print("focusing", UnitName("focus"))
    else
        print("focus cleared")
    end
end)
```

**Identity-checked**: assigning the same GUID twice is a no-op
(no event refires), matching 3.3.5's `FUN_0051FF20` behavior.
Mirrors modern's documented semantics — "fired whenever the
player's focus target is changed, including when the focus target
is lost or cleared".

### `QUEST_ACCEPTED` event

Fires once per quest the player just accepted, with two payload args:
the 1-based quest log index and the questID. Matches the Cata/WotLK
signature `QUEST_ACCEPTED(questLogIndex, questID)`. Polyfills modern
WoW's event of the same name (added in 3.1.0).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("QUEST_ACCEPTED")
f:SetScript("OnEvent", function()
    -- 1.12: event payload is in `arg1`, `arg2`, ... globals
    if event == "QUEST_ACCEPTED" then
        local questLogIndex, questID = arg1, arg2
        -- ...
    end
end)
```

Fires for every path that adds a quest to the local log — NPC accept,
party-shared quest accept, auto-grant from quest items — by hooking
the single engine chokepoint (`FUN_QUEST_LOG_REBUILD` at `0x004DE510`)
that rebuilds the Lua-visible quest log from the player's
authoritative slot data after any quest state change.

**Does not fire on initial login / character entry**, even though the
same engine function runs the bulk-sync there. Suppression is
heuristic: if a single rebuild call adds more than one quest, it's
treated as a resync and skipped. Human input speed can't accept two
quests within the same engine tick, so single-add is always a real
user accept. A brand-new character's very first quest accept
(`0 → 1` entries) fires correctly.

### `QUEST_REMOVED` event

Fires once per quest leaving the local quest log, with the questID as
the single payload arg. Covers **both turn-ins and abandons** — the
two are distinguishable by whether a `QUEST_TURNED_IN` accompanies
the removal. Polyfills modern WoW's event of the same name (added in
8.0.1; the Classic-era signature is the bare questID, which is what
we match).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("QUEST_REMOVED")
f:SetScript("OnEvent", function()
    if event == "QUEST_REMOVED" then
        local questID = arg1
        -- ...
    end
end)
```

Synthesized from the same `FUN_QUEST_LOG_REBUILD` pre-/post-snapshot
diff as `QUEST_ACCEPTED` — the removal side of the delta. For
turn-ins, `QUEST_REMOVED` fires **after** `QUEST_TURNED_IN`, matching
retail ordering: the SMSG_QUESTGIVER_QUEST_COMPLETE packet doesn't
touch the log itself; the removal arrives in the follow-up quest-log
update packets, whose rebuild triggers the diff. The observed turn-in
sequence is `QUEST_TURNED_IN` → `UNIT_QUEST_LOG_CHANGED` →
`QUEST_LOG_UPDATE` (fired inside the rebuild) → `QUEST_REMOVED`.

**Does not fire on login / character-switch resyncs.** Same
suppression rule as `QUEST_ACCEPTED` (a user action removes at most
one quest per rebuild), plus one extra gate: a rebuild that removes
one quest while adding several is a cross-character resync shape, not
gameplay, and stays silent.

### `QUEST_TURNED_IN` event

Fires when the server confirms a quest turn-in
(SMSG_QUESTGIVER_QUEST_COMPLETE, opcode `0x191`). Payload
`(questID, xpReward, moneyReward)` matches modern WoW exactly —
`xpReward` is the experience awarded (0 at max level or for
non-XP-bearing quests), `moneyReward` is in copper.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("QUEST_TURNED_IN")
f:SetScript("OnEvent", function()
    if event == "QUEST_TURNED_IN" then
        local questID, xp, money = arg1, arg2, arg3
        -- ...
    end
end)
```

Polyfilled by hooking the per-opcode SMSG handler at `FUN_005DC400`
(found via the CMSG side: `Script_GetQuestReward` →
`FUN_005015B0` → `FUN_005EADC0` sends opcode `0x18E`
CMSG_QUESTGIVER_CHOOSE_REWARD; SMSG response is opcode `0x191`).
The hook saves the packet stream's read cursor (`stream+0x14`),
peeks the first 4 uint32s of the body (questID, unknown, xp,
money), restores the cursor, then calls the original — which
re-reads the same data without seeing our peek. Side-effect-free.

Does NOT fire on quest abandon, on `QUEST_FINISHED` window
transitions (which fire on Detail → Progress → Reward → Close —
not a clean turn-in signal), or on server-reject paths (bag-full,
quest invalidated, etc.). Only fires on a real successful turn-in.

> **Why not key off `QUEST_FINISHED`?** Native vanilla `QUEST_FINISHED`
> fires on every quest-window state transition, including reads,
> aborts, and player-side close. The SMSG_QUESTGIVER_QUEST_COMPLETE
> packet, by contrast, is server-authoritative — the server only
> sends it after committing the turn-in (XP / money / item awards
> done, quest removed from log). Hooking the packet handler gives
> us the same signal modern WoW's event uses.

### `UNIT_FACTION` event (fire-coverage fix)

`UNIT_FACTION` already exists in the vanilla event table — addons can
register for it — but the 1.12 engine **never fires it** on several
paths where modern WoW (3.3.5+) does. We restore the modern firing
semantics so addons that observe `UNIT_FACTION` stop missing state
changes.

Payload is unchanged from modern: `arg1` is the unit token string,
always `"player"` for these paths (you can only toggle / sync rep on
the local player).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("UNIT_FACTION")
f:SetScript("OnEvent", function()
    if event == "UNIT_FACTION" and arg1 == "player" then
        RefreshFactionUI()
    end
end)
```

**Paths that now fire `UNIT_FACTION("player")`:**

| Path | Trigger | Detection |
|------|---------|-----------|
| `FactionToggleAtWar` | Lua call to toggle AT_WAR | Slot's flag byte changed (skips PEACE_FORCED no-ops, looting-blocked attempts, standing-blocked attempts) |
| `SetFactionInactive` / `SetFactionActive` | Lua call to toggle INACTIVE | Slot's flag byte changed |
| `SMSG_SET_FACTION_STANDINGS` | Server rep-change push (kills, quest turn-ins, etc.) | Any byte in the 64-entry flag array changed — catches AT_WAR side-effect flips when standing crosses the -3000 threshold |
| `SMSG_SET_FACTION_ATWAR` | Server force-change of AT_WAR (faction reset, server-side state push) | Unconditional after byte write |
| `SMSG_INITIALIZE_FACTIONS` | Server initial faction sync at login | Unconditional after table populated |

The last one fixes the case where addons relying on `UNIT_FACTION`
(rather than `PLAYER_LOGIN` / `VARIABLES_LOADED`) miss the initial
load entirely on vanilla.

**Not yet covered:**
- Unit-faction-template changes for *non-player* units (mind control,
  charm, server-side faction scripts). Modern WoW fires
  `UNIT_FACTION` on the affected unit token; the vanilla broad helper
  isn't hooked here yet. Mostly affects nameplate / threat addons in
  rare encounter mechanics.

**Implementation notes**

`FactionToggleAtWar` / `SetFactionInactive` / `SetFactionActive`'s
inner setters (`FUN_004D5FD0`, `FUN_004D60F0`) write to the rep slot's
flag byte and send the corresponding CMSG, but never call the
engine's "fire UNIT_FACTION on this unit" dispatcher. 3.3.5 added a
`FUN_0071F8F0(player, 0)` call to both paths whose inner
`FUN_0060BF10(playerGUID, UNIT_FACTION_id)` broadcasts the event for
every unit token referencing the local player.

Since `FactionToggleAtWar` only ever runs for the player and the only
unit token resolving to the player is `"player"`, we fire
`UNIT_FACTION("player")` directly via the engine's printf-style
dispatcher (`FUN_FIRE_EVENT`) — same observable result without
re-deriving the vanilla broad helper. We resolve `UNIT_FACTION`'s
event-table slot lazily by name (`Event::Custom::LookupByName`) so we
stay correct against any DLL combination that reshuffles the table.

For the two SMSG-bound flag-flip paths (`SetFactionStandings`,
`SetFactionInactive`-but-from-server), we snapshot the 64-entry rep
slot flag-byte array before the original handler runs and compare
after — single fire if any byte changed. The bulk snapshot is 64
byte reads (cheap) and naturally handles the case where one packet
carries multiple rep updates whose threshold crossings ripple AT_WAR
state across unrelated slots.

### `UPDATE_MOUSEOVER_UNIT` event (loss-fire fix)

`UPDATE_MOUSEOVER_UNIT` exists in the vanilla event table and fires when a
mouseover unit is **gained**, but the 1.12 engine **never fires it on
loss** — moving the cursor off a unit clears the mouseover and signals
nothing. Modern WoW fires it for both gain and loss, so addons observing
`UnitExists("mouseover")` can react when it becomes false. We restore the
loss fire.

Payload is unchanged (none in vanilla — same as retail): the event
carries no args; handlers read `UnitExists("mouseover")`, which is
`nil`/false at fire time on the loss path.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("UPDATE_MOUSEOVER_UNIT")
f:SetScript("OnEvent", function()
    if event == "UPDATE_MOUSEOVER_UNIT" then
        if UnitExists("mouseover") then
            -- gained (or changed to) a mouseover unit
        else
            -- mouseover unit lost (the vanilla gap this fills)
        end
    end
end)
```

**Transitions and who fires them:**

| From | To | Fires? | Source |
|------|----|:------:|--------|
| unit | nothing | yes | ClassicAPI (silent in stock 1.12) |
| unit | gameobject | yes | ClassicAPI (mouseover unit genuinely lost) |
| gameobject | nothing | no | — (never fired a gain; not a unit) |
| anything | unit | yes | engine's own inline fire (gain / unit→unit) |

**Implementation notes**

The mouseover GUID lives in two globals written **only** by the engine's
mouseover set/clear chokepoint (`0x00492890`), so every gain / loss /
change flows through it — on a unit gain it fires `UPDATE_MOUSEOVER_UNIT`
itself, on loss it clears the GUID silently. We co-hook that function:
resolve whether the mouseover is a *unit* before and after the original,
and fire on the unit→non-unit transition. Gating on unit *type* (not just
"GUID present → absent") is load-bearing — the same GUID slot also holds
gameobjects and items, which never fire a gain, so the type check is what
keeps moving off a herb node or chest from spuriously firing. The
`!hasUnit` half prevents a double-fire on any →unit transition the engine
already covered.

The target is change-gated by its callers (if it ran per-frame the gain
event would spam while hovering), so it's a cool hook — no per-frame
overhead.

### `UPDATE_SHAPESHIFT_FORM` event

Fires whenever the local player's shapeshift form changes — entering
or leaving cat / bear / travel / stance / shadowform / stealth /
ghost wolf / etc. No payload; call
[`GetShapeshiftFormID()`](#getshapeshiftformid) from the handler to
read the new form.

Polyfills the modern singular event. Vanilla 1.12 has only the plural
`UPDATE_SHAPESHIFT_FORMS` (fires when the *list* of available forms
changes — learning a new form, not changing into one). The singular
"current form changed" event was added in a later expansion.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("UPDATE_SHAPESHIFT_FORM")
f:SetScript("OnEvent", function()
    if event == "UPDATE_SHAPESHIFT_FORM" then
        print("now in form", GetShapeshiftFormID())
    end
end)
```

**Implementation notes**

Hooks the engine's bonus-action-bar refresh helper at `0x004E4FC0`,
which the engine calls every time the local player's `UNIT_BYTES_1`
descriptor field is broadcast and from `FUN_004908C0` at post-login
init. After the original runs, the post-hook reads byte 2 of
`UNIT_BYTES_1` (descriptor `+0x212` — the form ID) and fires
`UPDATE_SHAPESHIFT_FORM` only when the value differs from the last
seen — filtering out the spurious recomputes for other `UNIT_BYTES_1`
bytes (standstate, etc.) the engine also routes through this helper.

The cached "last form" sentinel uses `-1` for "player descriptor not
yet resolvable" so transient resolution failures during early login
don't get misread as leaving a form.

### `UNIT_SPELLCAST_*` events

Backport of the TBC+ cast/channel events to 1.12, for the **local player and
other units**. Ported cast-bar / rotation addons (anything written against
the modern signature) register these instead of vanilla's arg-less
`SPELLCAST_*` events and read `unit, castGUID, spellID` directly. Thirteen
events are provided; six also fire for non-player units:

| Event | Fires when | Units | Args |
|-------|-----------|-------|------|
| `UNIT_SPELLCAST_SENT` | `CMSG_CAST_SPELL` leaves the client (earliest point) | player | `unit, target, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_START` | a cast-time spell begins | all | `unit, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_STOP` | a cast-time spell ends (any reason) | all | same |
| `UNIT_SPELLCAST_DELAYED` | pushback extends the cast | player | same |
| `UNIT_SPELLCAST_SUCCEEDED` | the spell goes off (`SMSG_SPELL_GO`) — incl. instants | all | same |
| `UNIT_SPELLCAST_INTERRUPTED` | a started **cast** is interrupted (kick, movement, LoS) — never for channels | all | same |
| `UNIT_SPELLCAST_FAILED` | a cast is rejected before it starts (range, mana, cooldown) with an error shown | player | same |
| `UNIT_SPELLCAST_FAILED_QUIET` | a cast fails with **no** error shown (spammy retry, reticle cancel, …) | player | same |
| `UNIT_SPELLCAST_CHANNEL_START` | a channel begins | all | same |
| `UNIT_SPELLCAST_CHANNEL_UPDATE` | pushback shortens a channel | player | same |
| `UNIT_SPELLCAST_CHANNEL_STOP` | a channel ends | all | same |
| `UNIT_SPELLCAST_RETICLE_TARGET` | a ground-target reticle appears (AoE placement — Blizzard, Flare, …) | player | `unit, "", spellID, spellName, rank` |
| `UNIT_SPELLCAST_RETICLE_CLEAR` | the reticle is placed or cancelled | player | `unit, "", spellID, spellName, rank` |

`unit` (arg1) is the token of the casting unit — `"player"` for your own
casts, or a unit token (`"target"`, `"focus"`, `"party3"`, `"nameplate2"`,
`"pet"`, `"mouseover"`, …) for another unit. `spellName` / `rank` are
ClassicAPI tail extensions (modern stops at `spellID`); addons reading only
the first three positional args are unaffected.

**Per-token fan-out.** A caster GUID can map to several tokens at once (your
`target` is also `party2` and `nameplate1`). Like retail, the event fires
**once per token** currently pointing at the caster, so a `target`-frame
cast bar, a `party2` frame, and a nameplate cast bar each get their own
event with the same castGUID. Tokens are resolved fresh at fire time (they
shift frame-to-frame). If you run **SuperWoW**, its raw-GUID token (`"0x…"`)
is deliberately filtered out — only standard tokens are fanned out.

**Non-player limits.** Only the six events above fire for other units, and
they're **best-effort** — driven purely by the packets an observer receives:
- `SENT` never fires (only your own outgoing casts are visible).
- `DELAYED` / `CHANNEL_UPDATE` never fire (pushback is sent only to the
  caster, so another unit's bar can't stretch/shrink from damage).
- `FAILED` never fires (a pre-cast requirement failure is client-local).
- A remote cast/channel only has timing from the moment its
  `SMSG_SPELL_START` was observed; casters who were already casting when
  they came into range have no start time.

**Channels never fire `INTERRUPTED`.** Retail emits only `CHANNEL_STOP` when a
channel ends, whether it completed or was cut short (verified against retail),
so ClassicAPI matches that for both the player and other units. `INTERRUPTED`
is a cast-only event.

**Reticle events** fire for ground-targeted (AoE) spells only, always for the
player: `RETICLE_TARGET` when the placement reticle comes up, `RETICLE_CLEAR`
when it's placed or cancelled. There's no cast yet, so the castGUID slot
(arg2) is empty — retail pushes `nil` there, but the engine's event
dispatcher can't emit a `nil` mid-argument-list, so ClassicAPI pushes `""`
instead. `unit` (arg1) and `spellID` (arg3) are exact; arg2 is the only
difference and is inconsequential for a reticle.

**castGUID.** A synthesized string in the modern shape
`Cast-<type>-<serverID>-<instanceID>-<zoneUID>-<spellID>-<castUID>`. Vanilla
can't know server / instance / zone, so those three fields are `0`; the
load-bearing parts are the `spellID` (field 6, which addons `strsplit("-")`
out) and a unique-per-cast `castUID` (field 7). **Every event of one cast
carries the same castGUID**, so `SENT` → `START`/`CHANNEL_START` →
`SUCCEEDED` → `STOP`/`CHANNEL_STOP` all pair up — including across the caster
and observers (they converge on the same value), and a chained same-spell
recast gets its own castUID. The `type` and `castUID` follow the
[spell-cast-GUID spec](https://warcraft.wiki.gg/wiki/GUID#Cast):
- **Type 3** (real casts — the common case): `castUID` is time-based — the
  low 23 bits are the cast's UNIX-epoch second, the higher bits a per-second
  counter.
- **Type 2** (`UNIT_SPELLCAST_FAILED` — a local-only cast that never reached
  the server): `castUID` is a plain locally-incrementing integer.

**Ordering** matches modern:

- Cast-time spell: `SENT → START → SUCCEEDED → STOP`.
- Channel: `SENT → CHANNEL_START → SUCCEEDED → CHANNEL_STOP` (CHANNEL_START
  before SUCCEEDED, as on retail).
- Instant: `SENT → SUCCEEDED`.

**INTERRUPTED vs FAILED vs FAILED_QUIET** follow modern's split: a spell that
never started (out of range, not enough mana, on cooldown, LoS to a target)
fires `FAILED`, except for a fixed whitelist of "quiet" `SpellCastResult`
codes that fire `FAILED_QUIET` instead — `SPELL_IN_PROGRESS` (casting while
already casting / a spell-queue rejection), `DONT_REPORT` (fake fails, a
cancelled ground reticle), and `CHARMED`. That whitelist mirrors the 3.3.5
client's own unit-spellcast dispatch, mapped to vanilla's `SpellCastResult`
enum. A spell that was *already casting* and gets stopped (an enemy kick,
moving to cancel, breaking LoS mid-cast) fires `INTERRUPTED`. Holding
the cast key while running fires `INTERRUPTED` repeatedly (once per retry),
each reusing the interrupted cast's castGUID — matching retail.

**Channel pushback (player).** Taking damage while channeling shortens the
channel in vanilla; `CHANNEL_UPDATE` fires on each hit and
[`C_Spell.UnitChannelInfo`](#c_spellunitchannelinfounit--c_spellchannelinfo)'s
`endTimeMs` re-anchors to the server's new remaining time, so cast bars
shrink correctly. (The event carries no time — like retail it's a "re-read
now" trigger; timing is read back from `UnitChannelInfo`.)

Every fire is gated on whether any frame is registered for that event, so
the whole system costs one pointer-compare per state transition when no
addon uses it (no arg synthesis, no DBC lookups, no per-token fan-out).

```lua
local f = CreateFrame("Frame")
for _, e in ipairs({
    "UNIT_SPELLCAST_START", "UNIT_SPELLCAST_STOP",
    "UNIT_SPELLCAST_SUCCEEDED", "UNIT_SPELLCAST_CHANNEL_START",
}) do f:RegisterEvent(e) end
f:SetScript("OnEvent", function()
    -- vanilla passes event/arg1/... as globals, not function params
    if arg1 == "target" then print(event, arg3) end  -- arg3 = spellID
end)
```

> **Additive to the vanilla `SPELLCAST_*` events.** The engine's own arg-less
> `SPELLCAST_START` / `SPELLCAST_CHANNEL_UPDATE` / … still fire as before;
> these `UNIT_`-prefixed events are the modern layer on top. The empowered-cast
> events (`UNIT_SPELLCAST_EMPOWER_*`, a Dragonflight addition) are not
> implemented — vanilla has no empowered casts.

## Expansion

Helpers shipped by modern Classic Era / Cata Classic for addons that
want to gate code on which expansion the client targets. We always
answer as `LE_EXPANSION_CLASSIC` (`0`) — the DLL is built against
1.12 offsets, so there's nothing to detect. The matching number
constants (`LE_EXPANSION_*`) are in the [Globals section](#le_expansion_).

### `GetClassicExpansionLevel()`

Returns the live expansion level as a number. Always `0`
(`LE_EXPANSION_CLASSIC`) here.

```lua
if GetClassicExpansionLevel() >= LE_EXPANSION_BURNING_CRUSADE then
    -- never taken on 1.12
end
```

### `ClassicExpansionAtLeast(expansionLevel)`

Returns `true` iff `GetClassicExpansionLevel() >= expansionLevel`.
On 1.12 that reduces to `expansionLevel <= 0`, so only
`ClassicExpansionAtLeast(LE_EXPANSION_CLASSIC)` (and any negative
argument) are true; every later expansion answers `false`.

Errors if `expansionLevel` is missing or non-numeric — matches the
modern signature.

```lua
if ClassicExpansionAtLeast(LE_EXPANSION_WRATH_OF_THE_LICH_KING) then
    -- WotLK+ code path; never taken on 1.12
end
```

### `ClassicExpansionAtMost(expansionLevel)`

Returns `true` iff `GetClassicExpansionLevel() <= expansionLevel`.
On 1.12 that reduces to `expansionLevel >= 0`, so the only `false`
answer is for negative input.

Errors if `expansionLevel` is missing or non-numeric.

```lua
if ClassicExpansionAtMost(LE_EXPANSION_CLASSIC) then
    -- vanilla / Classic Era only code path
end
```

## Faction

### `GetFactionIDByIndex(factionIndex)`

Returns the factionID (Faction.dbc row ID) for the entry at the given 1-based
displayed-faction index. Modern WoW (5.0+, including Classic Era 1.15.x)
returns this as the 14th value of `GetFactionInfo`; older clients (1.12
through 3.3.5) don't expose it from Lua at all, even though the engine
uses it internally to look up `Faction.dbc`.

- Returns the factionID for real factions.
- Returns `0` for header / category rows (`"Other"`, `"Inactive"`, etc.) —
  matching the modern Classic Era client, which puts `0` in
  `GetFactionInfo`'s `factionID` slot for those rows.
- Returns `nil` if the index is out of range.

The "headers normalize to `0`" rule deliberately matches modern WoW
(5.0+) behavior. The 1.12 engine actually returns `0` for some header
types (`"Other"`) and `-1` for others (`"Inactive"`-style pseudo-rows);
we collapse both to `0` so the user-facing convention is consistent.

```lua
for i = 1, GetNumFactions() do
    local name, _, _, _, _, _, _, _, isHeader = GetFactionInfo(i)
    if not isHeader then
        local factionID = GetFactionIDByIndex(i)
        -- ...
    end
end
```

### `GetFactionInfoByID(factionID)`

Returns the same eleven values as `GetFactionInfo(factionIndex)`, keyed by
factionID instead of displayed index:

```
name, description, standingID, barMin, barMax, barValue,
atWarWith, canToggleAtWar, isHeader, isCollapsed, hasRep
```

Works for any factionID present in `Faction.dbc`, not just factions the
player has rep with:

- **In the player's reputation list** — full data, identical to
  `GetFactionInfo(displayedIndex)`.
- **Not in the reputation list** — name and description from `Faction.dbc`,
  Neutral defaults for the rep fields: `standingID = 4`, `barMin = 0`,
  `barMax = 3000`, `barValue = 0`, all flags `nil`. Matches what 3.3.5's
  `GetFactionInfoByID` returns for unencountered factions.
- **Invalid factionID** (out of range or empty DBC slot) — `nil`.

```lua
local name, _, standing = GetFactionInfoByID(69)  -- Darnassus
-- name = "Darnassus", standing = 5 (Friendly), etc. (encountered)

local name = GetFactionInfoByID(574)  -- Caer Darrow (faction that can't be encountered in standard Vanilla)
-- name = "Caer Darrow" — works even if you never had rep with it
```

### `GetFactionParentID(factionID)`

Returns the parent factionID for a faction in a hierarchy (e.g.
Stormwind's parent is Alliance Forces; The Defilers's parent is
Horde Forces). Returns `0` for top-level factions with no parent,
or `nil` for invalid factionIDs.

```lua
GetFactionParentID(72)     -- 469 (Stormwind → Alliance)
GetFactionParentID(469)    -- 0   (Alliance is top-level)
GetFactionParentID(99999)  -- nil
```

Modern WoW returns this as the 13th value of `GetFactionInfoByID`;
we expose it as its own getter since 1.12's `GetFactionInfo` doesn't
have the slot.

Reads `Faction.dbc` `ParentFactionID` at record `+0x48` directly —
no displayed-list dependency, works for any faction in the DBC
regardless of whether the player has rep with it.

### `C_Reputation.GetFactionStandings()`

Returns a flat `{ [factionID] = currentStanding }` table covering
every faction in the player's reputation list. `currentStanding` is
the same value `GetFactionInfo` puts in its `barValue` slot — `base
+ delta` from the rep slot, signed.

Always returns a table (possibly empty); never nil.

```lua
local standings = C_Reputation.GetFactionStandings()
for factionID, standing in pairs(standings) do
    print(GetFactionInfoByID(factionID), standing)
end
```

Unlike a `GetNumFactions` + `GetFactionInfo` walk, this skips header
rows entirely and doesn't depend on the player having opened the
reputation pane recently — it reads straight out of the per-faction
rep-slot array, which the engine keeps populated for every faction
the player has rep with.

If you need names instead of IDs, layer `GetFactionInfoByID` on top:

```lua
local byName = {}
for factionID, standing in pairs(C_Reputation.GetFactionStandings()) do
    byName[GetFactionInfoByID(factionID)] = standing
end
```

This is a ClassicAPI-only call; modern WoW's closest equivalent is
the 11.x `C_Reputation.GetFactions()`, which returns an array of
struct tables rather than a flat map.

### `C_Reputation.GetWatchedFactionData()`

Returns a table describing the faction shown above the XP bar, or
`nil` if no faction is being watched. Backports the modern
struct-style accessor — vanilla's `GetWatchedFactionInfo()` returns
the same data as a 5-tuple without the factionID, which is the field
modern callers rely on most.

Returns the same `FactionData` table shape as
[`GetFactionDataByIndex`](#c_reputationgetfactiondatabyindexfactionsortindex)
— see that section for the full field list. `isWatched` is forced
`true` (this faction IS the watched one by definition).

```lua
local data = C_Reputation.GetWatchedFactionData()
if data then
    print(string.format("%s (%d): %d / %d",
        data.name, data.factionID,
        data.currentStanding - data.currentReactionThreshold,
        data.nextReactionThreshold - data.currentReactionThreshold))
end
```

Implementation reads the watched `RepListID` from the player's
`+0xE68` sub-struct, indexes the per-faction rep slot array at
`0x00B73290` to recover the factionID, then runs the shared
`ReadFactionData` chain (Faction.dbc lookup, reaction band, rep slot
flags, header/collapsed checks).

### `C_Reputation.GetFactionDataByIndex(factionSortIndex)`

Returns a `FactionData` table for the faction at the given 1-based
displayed-list index, or `nil` for an out-of-range index or a
pseudo-row ("Other" / "Inactive" placeholders that don't have a
`Faction.dbc` record). The index range matches what `GetNumFactions`
covers — real factions plus category headers.

Fields:

| Field                      | Type    | Notes |
|----------------------------|---------|-------|
| `factionID`                | number  | `Faction.dbc` record ID. |
| `name`                     | string  | Locale-applied. |
| `description`              | string  | Locale-applied (may be `""`). |
| `reaction`                 | number  | `1`=Hated .. `8`=Exalted. |
| `currentReactionThreshold` | number  | Band min standing value. |
| `nextReactionThreshold`    | number  | Band max standing value. |
| `currentStanding`          | number  | Current standing (`base + delta`). |
| `atWarWith`                | boolean | Rep slot flags bit `0x02`. |
| `canToggleAtWar`           | boolean | `currentStanding ≥ -3000` AND not peace-forced (flags bit `0x10`). |
| `isHeader`                 | boolean | Faction is a category header in the displayed list. |
| `isHeaderWithRep`          | boolean | Always `false` in vanilla — parent factions don't aggregate rep. |
| `isCollapsed`              | boolean | UI state: user has collapsed this header. |
| `isWatched`                | boolean | This faction is shown above the XP bar. |
| `canSetInactive`           | boolean | True when `!isHeader && repListIndex ≥ 0` — i.e. the engine's `SetFactionInactive`/`SetFactionActive` will accept this faction. |
| `isChild`                  | boolean | Always `false` (parent-child rep introduced post-vanilla). |
| `hasBonusRepGain`          | boolean | Always `false` (added in MoP). |
| `isAccountWide`            | boolean | Always `false` (added in Dragonflight). |

```lua
for i = 1, GetNumFactions() do
    local d = C_Reputation.GetFactionDataByIndex(i)
    if d and not d.isHeader and d.currentStanding < d.nextReactionThreshold then
        -- still grindable rep
    end
end
```

Implementation: resolves the 0-based index to a factionID via the
engine's `FUN_RESOLVE_FACTION_INDEX` at `0x004D5FA0`, then runs the
shared `ReadFactionData` chain — no Lua-side round-trip through
`Script_GetFactionInfo`. `isHeader` / `isCollapsed` come from the
displayed-list header array at `0x00B736C0` (count at `0x00B736B0`)
and the per-character bitmask at `0x0084A0A4`.

### `C_Reputation.SetWatchedFactionByID(factionID)`

Sets the faction shown above the XP bar by ID. The vanilla
`SetWatchedFactionIndex(displayedIndex)` accepts only a 1-based
displayed-list index, which forces addons to walk the index list
themselves. This wrapper takes a `factionID` directly.

- `factionID > 0` — sets the watched faction. Works even for
  factions the player hasn't yet encountered (the rep bar will
  show nothing until the player gains rep, then displays
  normally).
- `factionID == 0` — clears the watched faction.
- `factionID < 0` — silent no-op.

Returns nothing. The engine's UI / event machinery refreshes
automatically — `GetWatchedFactionInfo()` reflects the new state
on the next call.

```lua
C_Reputation.SetWatchedFactionByID(72)  -- Stormwind
print(GetWatchedFactionInfo())          -- "Stormwind", ...

C_Reputation.SetWatchedFactionByID(0)   -- clear
print(GetWatchedFactionInfo())          -- (empty)
```

Implementation calls the engine's inner watched-faction setter
directly, bypassing `Script_SetWatchedFactionIndex`'s
displayed-index round-trip.

### `C_Reputation.GetLastStandingChange()`

Returns `factionID, newStanding, repGained` for the rep change
currently being fired, or `nil` if called outside that window.

Companion to [`FACTION_STANDING_CHANGED`](#faction_standing_changed-event).
The triple is the same payload as the event's `arg1, arg2, arg3`, but
the getter is also live during the engine's
`CHAT_MSG_COMBAT_FACTION_CHANGE` dispatch on the same hook — useful
for addons that want to enrich the chat line with the factionID
(which vanilla's chat-event payload doesn't carry) without
double-registering for `FACTION_STANDING_CHANGED`:

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("CHAT_MSG_COMBAT_FACTION_CHANGE")
f:SetScript("OnEvent", function()
    local factionID, newStanding, repGained = C_Reputation.GetLastStandingChange()
    if factionID then
        -- factionID is the real ID, not a parsed-out name string
        print(factionID, newStanding, repGained)
    end
end)
```

Outside the in-flight `FireNotify` hook stack (i.e. anywhere except
inside a `CHAT_MSG_COMBAT_FACTION_CHANGE` or `FACTION_STANDING_CHANGED`
handler), returns nothing. There is no "last change" memory — the
state is cleared as soon as the dispatch returns.

This is a ClassicAPI-only call; modern WoW has no equivalent (modern
addons get the factionID from `FACTION_STANDING_CHANGED` directly, so
they don't need a separate getter).

## Focus

Polyfills modern WoW's focus-target system: a single sticky GUID
that addons can pin to a unit and address via the `"focus"` unit
token across the entire `UnitX` API surface. Backed by a hook on
`FUN_TOKEN_TO_GUID` (shared with the [`nameplateN`](#unit-tokens-nameplaten)
tokens — see `unit/TokenExtensions.cpp`).

State is **session-only** — drops on `/reload`, `/logout`, and zone
loads that recreate Lua state. Modern Classic Era behaves the same;
addons that want persistence have to re-`FocusUnit` from `SavedVariables`
at `ADDON_LOADED`.

**Auto-clear on despawn.** When the focused unit leaves the client's
object table — out of rendering range, full despawn, dies and
decays — focus drops and [`PLAYER_FOCUS_CHANGED`](#player_focus_changed-event)
fires. The unit re-entering range does NOT auto-refocus. Matches
modern WoW's documented behavior. Implementation: per-tick
`ObjectByGUID(g_focusGUID)` probe; on null result, `Set(0)`.

### `FocusUnit(unit)`

Sets focus to the given unit. Argument is a unit token —
`"target"`, `"mouseover"`, `"party1"`, `"nameplate1"`, anything the
resolver accepts. Calling with no argument is shorthand for
`FocusUnit("target")` (matches modern's `/focus` slash command
default).

```lua
FocusUnit("target")        -- pin the current target
FocusUnit("nameplate1")    -- pin the unit behind nameplate1
FocusUnit()                -- same as FocusUnit("target")
```

Fires [`PLAYER_FOCUS_CHANGED`](#player_focus_changed-event) if the
resolved GUID differs from the current focus. No-op (no event) if
the same unit is already focused — matches the
identity-check-first behavior of 3.3.5's `FUN_0051FF20`.

Passing a token that doesn't resolve to a unit (e.g. `"target"`
with nothing targeted, or `"party5"` solo) clears focus — same as
calling `ClearFocus()`.

### `ClearFocus()`

Drops the focus. Fires `PLAYER_FOCUS_CHANGED` if there was one;
no-op otherwise.

```lua
ClearFocus()
```

### Unit token (`focus` / `focustarget`)

`"focus"` resolves to whatever GUID `FocusUnit` last stashed.
Accepted by every `UnitX` function:

```lua
local name = UnitName("focus")             -- nil if no focus
local hp   = UnitHealth("focus")
local _, class = UnitClass("focus")
```

Chains compose through the engine's standard suffix walker:
`"focustarget"`, `"focustargettarget"`, etc. — same behavior as
`"targettarget"`, mirrored instruction-for-instruction in our hook
so addons get the engine semantics they expect.

Returns `nil` cleanly when no focus is set; doesn't raise the
"Unknown unit name" error.

**Unit events fire for `"focus"`.** `UNIT_HEALTH`, `UNIT_MANA`,
`UNIT_AURA`, `UNIT_LEVEL`, `UNIT_MODEL_CHANGED`, … fire with
`arg1 == "focus"` whenever the focused unit's corresponding descriptor
field changes — even when it isn't your target, so a focus frame keeps
updating while you fight something else. Backed by `Unit::TokenObserver`,
which registers the engine's own unit-event descriptor-field observers
for the focus unit (the exact watch the engine gives target/party/raid).
A unit that is simultaneously your target and focus fires both
`"target"` and `"focus"`. (Vanilla `OnEvent` handlers read the `arg1`
global, not a function parameter.)

[`UnitTokenFromGUID`](#unittokenfromguidguid) scans `"focus"` right
after `"target"` (matching retail order), so a focused unit's GUID
reverse-resolves to `"focus"` only if it isn't already addressable
as `"player"` / `"party*"` / `"raid*"` / `"nameplate*"` / `"target"`.

## Bindings

Backports the direct-action and temporary override binding APIs added in WoW
2.0. The permanent helpers write later-client action command strings into
vanilla's native binding table; the override helpers add a separate,
session-only layer in front of that table.

### Permanent direct-action bindings

| Function | Equivalent command |
|----------|--------------------|
| `ok = SetBindingSpell(key, spell)` | `SetBinding(key, "SPELL " .. spell)` |
| `ok = SetBindingItem(key, item)` | `SetBinding(key, "ITEM " .. item)` |
| `ok = SetBindingMacro(key, macroNameOrIndex)` | `SetBinding(key, "MACRO " .. macroNameOrIndex)` |
| `ok = SetBindingClick(key, buttonName [, mouseButton])` | `SetBinding(key, "CLICK " .. buttonName .. (mouseButton and ":" .. mouseButton or ""))` |

`key` is any binding string accepted by the client, such as `"CTRL-F"`,
`"SHIFT-BUTTON4"`, or `"F8"`. `spell`, `item`, `macroNameOrIndex`, and
`buttonName` identify the action to perform. `macroNameOrIndex` accepts either
a saved macro's name or its decimal index. `mouseButton` is passed to the
button's click handler and defaults to `"LeftButton"`; when supplied, it is
stored as the `:MouseButton` suffix of the `CLICK` command.

Each helper returns `1` if vanilla's `SetBinding` changed the binding and
`nil` otherwise. It changes whichever binding set is currently loaded, fires
vanilla's normal `UPDATE_BINDINGS` notification, and is not persisted until
the addon calls `SaveBindings`. Use vanilla `SetBinding(key)` to unbind a key;
the typed helpers always require an action argument.

The command hook also understands manually constructed `SPELL`, `ITEM`,
`MACRO`, and `CLICK` strings passed directly to vanilla `SetBinding`.

### Temporary override bindings

```lua
SetOverrideBinding(owner, isPriority, key [, command])
SetOverrideBindingSpell(owner, isPriority, key, spell)
SetOverrideBindingItem(owner, isPriority, key, item)
SetOverrideBindingMacro(owner, isPriority, key, macroNameOrIndex)
SetOverrideBindingClick(owner, isPriority, key, buttonName [, mouseButton])
ClearOverrideBindings(owner)
```

`owner` must be a frame and `isPriority` must be a Boolean. The typed helpers
construct the same four action strings shown above. The generic
`SetOverrideBinding` also accepts ordinary Bindings.xml command names, which
are delegated to vanilla's command executor. Override setters and
`ClearOverrideBindings` return no values.

Passing no `command` (or `nil`) to `SetOverrideBinding` removes that owner's
override for `key`. `ClearOverrideBindings(owner)` removes every override
belonging to that owner. Overrides never change or save the native binding
table and are cleared whenever the FrameScript Lua state is recreated, such
as on `/reload` or logout.

Resolution order is:

1. Priority override bindings.
2. Non-priority override bindings.
3. The normal native binding.

When multiple owners set the same key at the same priority, the most recently
set override wins. Removing it immediately reveals the next matching override
or the unchanged native binding.

`key` is matched case-insensitively, and its modifiers are accepted in any
order: `"ctrl-alt-F"`, `"ALT-CTRL-F"`, and `"Alt-Ctrl-F"` all bind the same
key. The override layer normalizes the key to the engine's own keypress form
(`ALT-CTRL-SHIFT-` before the base key) before matching, so the override fires
regardless of how the caller spelled it.

A generic `SetOverrideBinding` command that is an ordinary Bindings.xml command
runs through the engine's own command executor with the same execution context
a native keypress establishes, so it behaves like the equivalent permanent
binding.

### Action execution

| Command | Execution path |
|---------|----------------|
| `SPELL name` | Calls vanilla `CastSpellByName(name)`. The usual `/cast` spell-name syntax is accepted. |
| `ITEM item` | Calls `C_Item.UseItemByName(item)`, accepting the names, item strings, and item IDs supported by that API. |
| `MACRO nameOrIndex` | Resolves a saved macro by name or decimal index. An addon-provided `RunMacro` is used when available; otherwise ClassicAPI resolves it with `GetMacroInfo` and dispatches its body through `ChatEdit_ParseText`. |
| `CLICK ButtonName[:MouseButton]` | Resolves the named global frame and calls its ordinary `:Click(mouseButton)` method. The default is `LeftButton`. |

`SetBindingMacro` and `SetOverrideBindingMacro` take a saved macro identifier,
not literal macro text. To bind literal macro text, bind a named button whose
ClassicAPI attributes describe a macro action:

```lua
local button = CreateFrame("Button", "MyMacroTextBindingButton")
button:SetAttribute("type", "macro")
button:SetAttribute("macrotext", "/say Bound macro text")

SetBindingClick("CTRL-F7", button:GetName())
```

### 1.12 compatibility notes

- There is no combat lockdown or secure execution. These functions remain
  callable in combat, and `CLICK` invokes the frame's ordinary click path.
- Direct actions run on key-down. The corresponding key-up is consumed; a
  click binding does not emulate later clients' separate mouse-down and
  mouse-up delivery.
- The override layer is not reflected by vanilla `GetBindingAction`.
  In particular, the later-client `GetBindingAction(key, true)` override query
  is not backported.
- Override ownership is explicit. Call `ClearOverrideBindings(owner)` when the
  addon's owner is no longer needed; all remaining overrides are also cleared
  when the Lua state is recreated.

### Predefined focus bindings (`FOCUSTARGET` / `TARGETFOCUS`)

Two key bindings appear in the keybind UI (`Esc` → Key Bindings)
under the **Targeting Functions** group, between `PETATTACK` and the
Interface section:

| Binding | Lua action | Use |
|---------|------------|-----|
| `FOCUSTARGET` | `FocusUnit("target")` | Pin current target as focus |
| `TARGETFOCUS` | `TargetUnit("focus")` | Switch target to the focus |

No default key — assign one in the keybind UI like any other binding.
The two together give you a quick "set focus → swap to focus" loop
common in PvP / heal-tank-while-killing-add scenarios.

Implementation note: addon-side `Bindings.xml` files always render at
the bottom of the keybind list (orphaned from any `header="..."` they
declare) because the engine's binding registry is linearly indexed by
insertion order and addons load after FrameXML. To land inside the
TARGETING block, the DLL splices the two `<Binding>` entries into the
engine's `Interface\FrameXML\Bindings.xml` at file-read time via a
hook on `FUN_FILE_READ` — see [`src/bindings/Inject.cpp`](../src/bindings/Inject.cpp).

## Frame

Modern Region/Frame method backports. Modern addon ports routinely
call these methods unconditionally — they're the C-level frame surface
later clients take for granted. This section is that surface on 1.12.
Methods are registered on the engine's own per-frame-type method
registries
(`SetSize`/`GetSize` on the Region base, so they resolve on frames,
buttons, textures and fontstrings alike), and each delegates to the
engine's own implementations — UI-scale conversion, object resolution
and type checking are all the engine's code.

### `region:SetPoint("point")` (one-argument form)

Vanilla's `SetPoint` parser accepts every modern call shape —
`(point, region)`, `(point, region, relativePoint [, x, y])`,
`(point, x, y)`, even an explicit `nil` region — **except** the bare
one-argument form, which raises the usage error (a fully-omitted third
argument fails its type validation). A co-hook on the engine's
`Script_SetPoint` normalizes `region:SetPoint("RIGHT")` to
`(point, 0, 0)` — vanilla's parent-relative form, which is exactly the
modern semantic (anchor to the parent's same point, zero offsets).

### `region:SetSize(width, height)` / `region:GetSize()`

The modern combined setter/getter for `SetWidth`+`SetHeight` /
`GetWidth`+`GetHeight`. Works on any region type — frames, buttons,
textures (including engine-created ones like
`button:GetNormalTexture()`), fontstrings.

### `region:IsMouseOver([topOffset, bottomOffset, leftOffset, rightOffset])`

`true` when the mouse cursor is within the region's rectangle, `false`
otherwise. The four optional offsets shift the corresponding edge before
the test (added to that edge, matching modern semantics: positive
`topOffset` / `rightOffset` and negative `bottomOffset` / `leftOffset`
enlarge the hit area). Registered on the Region base, so it works on any
region type — frames, buttons, textures, fontstrings.

Uses the engine's own rect getters and cursor position, dividing the
cursor by the region's effective scale — the exact computation vanilla
addons have long open-coded as `MouseIsOver()`. Returns `false` for a
region with no resolved rect (unpositioned / zero-size).

```lua
ItemRefTooltip:IsMouseOver()                 -- true while hovering the tooltip
Minimap:IsMouseOver(20, -20, -20, 20)        -- true within a 20px halo around it
```

### `region:GetRect()`

Returns the region's `left, bottom, width, height` in one call (the
modern combined form of `GetLeft`+`GetBottom`+`GetWidth`+`GetHeight`).
Composed from the engine's own getters, so all UI-scale conversion is its
code. On the Region base — works on any region type. Returns nothing for
a region with no resolved rect (unpositioned), matching retail.

```lua
UIParent:GetRect()   -- 0, 0, <screenWidth>, <screenHeight>
Minimap:GetRect()    -- <left>, <bottom>, 140, 140
```

### `region:IsDragging()`

`true` while the region is the frame currently being moved or resized by
the mouse — i.e. after `StartMoving`/`StartSizing` and before
`StopMovingOrSizing` — `false` otherwise. Reads the engine's single global
drag target (the same slot `StopMovingOrSizing` checks to decide whether
it owns the active drag). On the Region base; non-frame regions (textures,
fontstrings) always report `false`, since only frames can be dragged.

```lua
-- in a frame's OnUpdate while the user drags it:
if self:IsDragging() then ... end
```

### `GetMouseFoci()`

Returns a table of the mouse-enabled frames under the cursor, from top
to bottom. Index 1 is the frame that `GetMouseFocus()` returns, so
`GetMouseFocus() == GetMouseFoci()[1]` is always true.

```lua
local foci = GetMouseFoci()
-- foci[1] == GetMouseFocus()
for i = 1, table.getn(foci) do
    print(foci[i]:GetName() or "<anonymous>")
end
```

### `frame:SetShown(shown)`

`Show()` if `shown` is truthy, `Hide()` otherwise. Registered for
frames, textures and fontstrings (each branch has its own engine
Show/Hide implementation).

### `fontstring:GetStringHeight()`

This is the companion to vanilla's `GetStringWidth`. Blizzard first
shipped it in 2.3.0. It returns the height of the rendered text, in
UI pixels. The result includes word wrap: wrapped text measures
`lines × fontHeight + (lines − 1) × spacing` (the `SetSpacing`
value). The height comes from the same wrap math that the renderer
uses. Empty or unset text returns `0`. The vanilla engine already
computes this height internally, for tooltip auto-sizing and chat
line stacking. This method only adds the missing Lua binding.

```lua
local f = frame:CreateFontString(nil, "ARTWORK", "GameFontNormal")
f:SetWidth(100)
f:SetText("a long line that wraps a few times")
frame:SetHeight(f:GetStringHeight() + 16)  -- size the box to the text
```

Related: the stock `fontstring:GetStringWidth()` now **includes inline
`|T…|t` icon widths**. It reports the same advance that the renderer
reserves for each icon, so the measured width matches the rendered
width. Editbox text is not adjusted: an editbox shows raw markup, and
its caret math must measure that same raw text.

### `texture:SetRotation(angle [, cx, cy])`

Rotates a texture by `angle` radians. A positive angle turns the texture
counter-clockwise. Later clients added this method. Vanilla has no texture
rotation of its own.

The optional `cx, cy` set the pivot point, as a normalized position inside the
texture from `0` to `1`. The default pivot is the center, `(0.5, 0.5)`. An
angle of `0` clears the rotation and returns the texture to upright.

The method turns the four corners of the drawn quad, so the whole texture stays
visible and no corner is clipped. It works on any texture, including
engine-created ones. The rotation holds across moves and resizes, and costs
nothing per frame while it stays still. `GetRotation()` returns the current
angle in radians.

```lua
local t = frame:CreateTexture(nil, "ARTWORK")
t:SetAllPoints(frame)
t:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
t:SetRotation(math.rad(45))      -- 45 degrees counter-clockwise
t:SetRotation(math.pi, 0, 1)     -- half turn around the top-left corner
```

### `texture:SetVertexOffset(vertexIndex, offsetX, offsetY)`

Moves one corner of a texture by `offsetX, offsetY` pixels. `vertexIndex` picks
the corner: `UPPER_LEFT_VERTEX`, `LOWER_LEFT_VERTEX`, `UPPER_RIGHT_VERTEX`, or
`LOWER_RIGHT_VERTEX` (values 1 to 4). `+x` is right and `+y` is up. Vanilla has
no way to move a texture's corners. This backport matches retail.

Moving the corners warps the whole quad, so the texture and its background move
together. It suits skew, trapezoid, and fake-perspective effects, and waving
animations. It composes with `SetRotation`: the texture rotates first, then the
offsets apply. The offset holds across moves, resizes, and `SetTexture`, and
costs nothing per frame while it stays still.

`GetVertexOffset(vertexIndex)` returns the current `offsetX, offsetY` for a
corner (`0, 0` if unset).

Note: an offset that turns the quad inside-out — mirroring it past its opposite
edge — is not drawn. The engine skips a back-facing quad, so keep the warp
within a non-mirrored shape.

```lua
local t = frame:CreateTexture(nil, "ARTWORK")
t:SetAllPoints(frame)
t:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
-- pull the top two corners inward: a trapezoid (fake perspective)
t:SetVertexOffset(UPPER_LEFT_VERTEX, 20, 0)
t:SetVertexOffset(UPPER_RIGHT_VERTEX, -20, 0)
```

### `texture:SetColorTexture(colorR, colorG, colorB [, a])`

Fills the texture with a solid color. Each channel is `0` to `1`. The alpha `a`
is optional and defaults to `1` (opaque). This is the 7.0 name for a fill that
vanilla already does — `SetTexture(r, g, b [, a])` with numbers instead of a
path. The backport is the same engine call under a second name, so the clamping
and the opaque default match the engine.

```lua
local t = frame:CreateTexture(nil, "BACKGROUND")
t:SetAllPoints(frame)
t:SetColorTexture(0.1, 0.6, 1.0, 0.8)  -- semi-transparent blue fill
```

### `frame:SetResizeBounds(minWidth, minHeight [, maxWidth, maxHeight])`

The modern rename of vanilla's `SetMinResize` / `SetMaxResize` pair.
The max pair is applied only when both values are given.

### `frame:HookScript(scriptType, handler)`

Vanilla has no `HookScript` at all. This backport chains: the
previously-set handler runs first, vanilla-style (no arguments — it
reads the `this`/`event`/`argN` globals like every 1.12 handler), then
`handler` is invoked **modern-style** with positional arguments:
`(frame, event, arg1..arg9)` for `OnEvent` scripts and
`(frame, arg1..arg9)` for everything else. That matches the compat
convention modern addon ports expect, so handlers written as
`function(self, ...) ... end` work unmodified. Vanilla-style handlers
passed to `HookScript` also keep working — the globals are set as
usual and extra arguments are simply ignored.

```lua
button:HookScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText("hello")
    GameTooltip:Show()
end)
```

### `frame:IsEventRegistered(event)`

Vanilla ships `RegisterEvent` / `UnregisterEvent` but not the query.
Returns `true` if the frame is currently registered for `event`,
`false` otherwise (including for an unknown event name). Reuses the
engine's own subscriber-chain membership check — the same walk
`RegisterEvent` performs before appending — so the answer is exactly
what the event dispatcher sees. Works on any frame.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_LOGIN")
f:IsEventRegistered("PLAYER_LOGIN")   -- true
f:IsEventRegistered("PLAYER_LOGOUT")  -- false
```

### `frame:GetEffectiveAlpha()`

Returns the frame's **effective** alpha — its own `GetAlpha()` multiplied
by every ancestor's, i.e. the opacity WoW actually composites at render
time (fading `UIParent` fades all of its children). Vanilla exposes only
`GetAlpha` (a frame's own alpha); this walks `self → parent → …` via the
engine's own `GetAlpha`/`GetParent`, so it sees exactly what the engine
does — including the 8-bit quantization of alpha (`SetAlpha(0.5)` stores
`127/255 ≈ 0.498`, and effective alpha is the product of those).

```lua
UIParent:GetEffectiveAlpha()   -- 1
-- with UIParent at 0.5 and Minimap's own alpha 1:
Minimap:GetEffectiveAlpha()    -- ~0.498 (0.5 truncates to 127/255)
```

### `frame:SetAttribute(name, value)` / `frame:SetAttributeNoHandler(name, value)` / `frame:ClearAttribute(name)` / `frame:GetAttribute(...)`

Backports the frame **attribute** system — a per-frame, case-insensitive
key→value store — to 1.12 as native methods on every frame. Attributes were
added in 2.0 with secure frames and don't exist in vanilla at all; `value`
can be any Lua type and round-trips exactly.

`cleared = frame:ClearAttribute(name)` (retail 11.2.0) removes an attribute and
returns whether it was set. Unlike `SetAttribute`, it does **not** fire
`OnAttributeChanged` — matching retail (verified against a live 12.0 client).

```lua
f:SetAttribute("unit", "party1")
f:GetAttribute("unit")            -- "party1"
f:ClearAttribute("unit")          -- true  (was set; now removed)
f:ClearAttribute("unit")          -- false (nothing to clear)
f:SetAttribute("count", 3)
f:GetAttribute("count")           -- 3
f:GetAttribute("missing")         -- nil
```

`GetAttribute` also has the modifier form `GetAttribute(prefix, name, suffix)`,
which tries, in order, and returns the first match (the same precedence retail
uses for `type1`/`*type1`-style resolution):

1. `prefix..name..suffix`
2. `"*"..name..suffix`
3. `prefix..name.."*"`
4. `"*"..name.."*"`
5. `name`

`SetAttributeNoHandler` sets the value **without** firing `OnAttributeChanged`;
`SetAttribute` fires it (see below).

**Unit-frame mouseover — the headline use.** Setting a **string `unit`
attribute** makes the frame a mouseover source: while the cursor is over it,
the `mouseover` unit token resolves to that frame's unit. This is the piece of
SecureUnitButton behavior modern unit-frame addons rely on — and since 1.12 has
no combat lockdown or taint, no secure machinery is needed to provide it.

```lua
local f = CreateFrame("Button", "MyUnitFrame", UIParent)
f:SetWidth(120); f:SetHeight(40)
f:SetPoint("CENTER")
f:SetAttribute("unit", "party1")
-- Hover it → the `mouseover` token resolves to party1:
--   UnitName("mouseover"), UnitHealth("mouseover"), GameTooltip:SetUnit("mouseover"),
--   mouseover-cast, and UPDATE_MOUSEOVER_UNIT all work.
```

How it works, and why it's robust: rather than installing `OnEnter`/`OnLeave`
on the frame (which an addon's own `SetScript("OnEnter", …)` would overwrite —
pfUI sets `unit` *before* its scripts, for instance), this mirrors retail and
SuperWoW's `SetMouseoverUnit`. The engine's mouse-focus frame is watched once
per frame; when a hovered frame carries a `unit` attribute, the engine's **real
mouseover setter** is invoked for that unit — **1:1 with hovering the unit's 3D
model**: the model highlights, the mouseover tooltip builds, and
`UPDATE_MOUSEOVER_UNIT` fires, in addition to the GUID slot being set. So
everything that reads mouseover — the resolver's `mouseover` branch,
`GameTooltip:SetUnit("mouseover")`, `UnitX("mouseover")`, mouseover-cast — sees
it natively. Because nothing touches the frame's scripts, an addon setting its
own handlers can't break it; and it's stomp-proof (the engine's 3D-hover setter
is event-driven, so while the cursor is over UI nothing overwrites the slot). It
follows live token changes (a `unit="target"` frame tracks your current target
while hovered). Setting a string `unit` also `EnableMouse`s the frame so a bare
frame becomes hoverable at all (real unit frames already are). The `unit` value
may be any token the resolver understands — `"party1"`, `"target"`, `"focus"`,
`"nameplateN"`, or a raw GUID literal. Set `unit` to a non-string (e.g. `nil`)
to stop the binding.

**Click actions (`type1` / `type2` / …).** A `type` attribute makes clicking the
frame perform one action on its `unit` — the retail secure-button model: exactly
**one verb per click**, resolved from the attributes.

```lua
f:SetAttribute("type1", "target")        -- left-click targets the unit
f:SetAttribute("type2", "focus")         -- right-click sets ClassicAPI focus to it
-- click-casting, expressed purely as attributes:
f:SetAttribute("shift-type1", "spell")
f:SetAttribute("shift-spell1", "Flash Heal")   -- shift-left-click heals the unit
```

**Resolution.** The verb is read from `[prefix]type[suffix]`, where the prefix is
the held modifiers (`alt-`, `ctrl-`, `shift-`, in that order) and the suffix is
the button number (`1`=Left, `2`=Right, `3`=Middle, `4`/`5`=side). Precedence is
`prefix..type..suffix` → `type..suffix` → `type`, so `type1` applies under any
modifier unless a modifier-specific attribute (`shift-type1`) overrides it — and
`type` (no suffix) is the catch-all.

**Verbs.** The resolved verb reads its own extra attributes (also
modifier/button-qualified, same precedence as `type`):

| Verb | Extra attributes | Action |
|------|------------------|--------|
| `target` | — | Targets the `unit` (or clears the target if `unit` is `"none"`). Respects the engine's default-interaction precedence: with a spell on the cursor it casts on the unit, with an item on the cursor it drops it on the unit, instead of switching target. |
| `assist` | — | Targets the `unit`'s target. |
| `focus` | — | Sets the ClassicAPI focus to the `unit`. |
| `spell` | `spell` | Casts the `spell` on the `unit` via [`C_Spell.CastAtUnit`](#c_spellcastatunitspellidorname-unit) — the unit's GUID goes straight to the cast dispatcher (no target juggling), and ground-target spells land at the unit's feet. |
| `item` | `item`, or `bag`+`slot` | Uses an item on the `unit`. `item` may be a name / itemID / link (used via `C_Item.UseItemByName`, unit as the target) or a `"bag slot"` string like `"0 1"` (used via `UseContainerItem`). The deprecated `bag`+`slot` attributes are used when `item` is unset. |
| `macro` | `macrotext` or `macro` | Runs the macro text. To give the macro a unit, the handler selects the clicked `unit`, runs the macro, then restores the target from before the click. It prefers an addon `RunMacro` (for example, SuperCleveRoidMacros — named macros and extended conditionals). If no addon `RunMacro` exists, it runs the text natively, line by line, through the stock `ChatEdit_ParseText`. |
| `stopcasting` | — | Stops the current cast. |
| `menu` / `togglemenu` | — | Pops the standard unit dropdown at the cursor (whisper / inspect / trade / invite / …, the same menu `PlayerFrame` / `TargetFrame` / `PartyMemberFrame` show). |

Every verb that acts on a unit uses the frame's `unit` attribute (resolved with
the same modifier/button precedence).

**One verb per click.** Setting a `type*` attribute installs a **chained
`OnClick` on that frame only** (nothing global). When a verb resolves, our
handler *owns* the click and does **not** run the frame's previous `OnClick` —
so a configured `type1` never fires alongside the addon's own click handler.
Unconfigured clicks (no matching `type`, or an unrecognized verb) fall through
to the frame's own `OnClick`, so an addon can keep custom behavior there. The
frame must be a **Button** registered for the click
(left is the Button default; right needs `RegisterForClicks("RightButtonUp")`).

**Clobbering.** The handler self-heals: addons that re-`SetScript("OnClick", …)`
(pfUI on every raid relayout) replace our closure, so re-set a `type*` attribute
afterward to reinstall — re-wiring detects and skips its own closure, so it never
double-chains.

**`OnAttributeChanged`** is a real `SetScript` / `GetScript` / `HookScript`-able
frame script, on **every** frame — `SetAttribute` fires it after setting the
value, `SetAttributeNoHandler` doesn't. As with all 1.12 frame scripts, the
handler takes no parameters and reads its context from globals: `this` = the
frame, `arg1` = the (lowercased) attribute name, `arg2` = the new value.

```lua
f:SetScript("OnAttributeChanged", function()
    if arg1 == "unit" then
        MyFrame_Update(this, arg2)   -- react to unit changes
    end
end)
f:SetAttribute("unit", "party1")     -- fires the handler (arg1="unit", arg2="party1")
f:SetAttributeNoHandler("unit", "party2")  -- sets it silently, no handler
```

It's implemented the same way as the tooltip-side `OnTooltipSet*` scripts — a
co-hook on the base-frame script-name resolver that hands out an external
per-frame handler slot for this one name (1.12 frames are never destroyed, so
the slot never goes stale). Recursion-guarded, so a handler may itself call
`SetAttribute`.

### `SetModernScriptArgs(enable)` / `GetModernScriptArgs()`

Global toggle (not a frame method) for **modern positional script arguments**.
Vanilla always invokes a frame-script handler with **zero** Lua arguments — the
handler reads `this` / `arg1..argN` / `event` as globals. With this enabled, every
handler additionally receives its values as real positional arguments, so modern
addon ports written as `function(self, delta) … end` work unmodified:

- most scripts: `(self, arg1..argN)` — e.g. `OnMouseWheel(self, delta)`,
  `OnClick(self, button)`, `OnValueChanged(self, value)`, `OnUpdate(self, elapsed)`.
- `OnEvent`: `(self, event, arg1..argN)`.

It's purely **additive** — the `this` / `arg1` / `event` globals are still set, so
vanilla-style handlers keep working; a handler declaring no parameters just ignores
the extras. `SetModernScriptArgs(enable)` sets the state and returns it;
`GetModernScriptArgs()` returns the current state.

**Default OFF (opt-in).** It reimplements the tail of the engine's hottest Lua
path (the runner that fires for every `OnUpdate`, every frame), so it stays off
unless you ask for it; while off the dispatch path is exactly vanilla. Enable it
once at load if your addon relies on the modern handler signature.

```lua
SetModernScriptArgs(true)   -- returns true

local f = CreateFrame("Frame")
f:EnableMouseWheel(true)
f:SetScript("OnMouseWheel", function(self, delta)
    -- `self` and `delta` are bound; `this` / `arg1` still work too
end)

GetModernScriptArgs()       -- true
```

## FriendList

### `C_FriendList.SendWhoQueryByName(name)`

Issues a /who name-filter query for a specific player. Results
buffer into the engine's WhoList so a normal `WHO_LIST_UPDATE` +
`GetWhoInfo(i)` flow can read them — no chat output, no
`"Found N players matching..."` system message.

Vanilla 1.12 exposes `SendWho(query)` and `SetWhoToUI(flag)` only as
separate primitives, so addons that want to silently look up a
single player's class/level/zone (e.g. to color an unknown name in
chat) have to manage state, cooldown timing, and friends-panel
suppression themselves. This collapses the invocation half of that
dance to one call with a clean true/false return.

Returns `true` if the query was sent, `false` if any of:

- the name was empty / nil
- the call is within the 5-second cooldown of a previous send
- the engine's WhoSystem isn't initialized yet (pre-login)

A `false` return is a no-op — the engine state isn't touched, no
pending flag is set, and the cooldown isn't extended. Safe to call
on every chat line that mentions an unknown player; the call will
naturally rate-limit.

The cooldown matches the server's: vanilla's CMSG_WHO is silent-
dropped server-side at roughly 5-second granularity, so a faster
client just wastes queries that won't get a response.

```lua
if C_FriendList.SendWhoQueryByName("Bob") then
    -- query sent; result will arrive via WHO_LIST_UPDATE within ~1s
end
```

This call alone does **not** suppress the friends-panel popup on
response — for that, wrap `FriendsFrame_OnEvent` and gate on
[`C_FriendList.IsWhoQueryPending()`](#c_friendlistiswhoquerypending):

```lua
local original = FriendsFrame_OnEvent
_G.FriendsFrame_OnEvent = function()
    if event == "WHO_LIST_UPDATE" and C_FriendList.IsWhoQueryPending() then
        return
    end
    return original()
end
```

(Auto-suppressing the popup from C++ would require hooking the
engine's `FrameScript_SignalEvent` or the SMSG_WHO opcode handler —
both high-traffic / high-risk sites. The wrap above is reliable and
takes 5 lines, so it stays addon-side for now.)

Implementation reads the WhoSystem singleton from `0x00C28168` and
calls the inner sender at `0x005AEBB0`
(`__thiscall(this = WhoSystem, queryStr)`), the same chokepoint
`Script_SendWho` tail-calls. The `whoToUI` flag at `0x00C2A12C` is
flipped to `1` first so the SMSG_WHO handler routes results into the
list (`WHO_LIST_UPDATE` path) instead of printing to chat.

### `C_FriendList.IsWhoQueryPending()`

Returns `true` within ~5 seconds of the most recent
[`SendWhoQueryByName`](#c_friendlistsendwhoquerybynamename) that
returned `true`; otherwise `false`. Time-based — the engine doesn't
expose a "response arrived" hook to C++ yet, so the window is a
conservative upper bound on the response RTT, which in practice
lands within a few hundred ms.

The intended use is gating an addon-side `FriendsFrame_OnEvent`
wrap so the user-issued `/who` window still works while addon-
issued silent queries don't pop the panel — see the example under
`SendWhoQueryByName`.

Concurrent callers see a shared pending flag — if pfUI and another
addon both call `SendWhoQueryByName` close in time, both queries
contribute to the same pending window. This is acceptable for the
"suppress popup for any in-flight DLL-issued query" use case;
addons that need per-call tracking should manage their own ticket
state on top.

### `C_FriendList.GetNumWhoResults()`

Returns two numbers: how many /who results you can read, and a
server-reported count.

```lua
local shown, serverCount = C_FriendList.GetNumWhoResults()
```

`shown` is the number of entries you can index with
[`GetWhoInfo`](#c_friendlistgetwhoinfoindex). The server caps the list
(50, or 49 on some cores), so `shown` is at most that. Run a `/who` (or
[`SendWhoQueryByName`](#c_friendlistsendwhoquerybynamename)) first to
populate the list.

`serverCount` is the second header value the server sends, and its
meaning depends on the server core:

- Mangos and cmangos derived cores send the number of characters that
  matched your query.
- Turtle WoW (tortoise-wow) sends the whole realm's online population
  when more than 49 players are online, ignoring your filter. Below
  that count it sends the match count.

Do not rely on one meaning across servers. On Turtle it is a live
population counter for any query. On other cores it is a match total.

### `C_FriendList.GetWhoInfo(index)`

Returns a table for the /who result at `index` (1-based), or `nil` when
the index is out of range.

```lua
local info = C_FriendList.GetWhoInfo(1)
-- {
--   fullName = "Nadamage", fullGuildName = "",
--   level = 26, raceStr = "Troll",
--   classStr = "Mage", filename = "MAGE",
--   area = "Hillsbrad Foothills",
-- }
```

Table fields:

- `fullName` — the character name.
- `fullGuildName` — the guild name, or `""` when the player has no
  guild.
- `level` — the character level.
- `raceStr` — localized race name, or `nil` when unknown.
- `classStr` — localized class name, or `nil` when unknown.
- `filename` — locale-independent class token (`"WARRIOR"`, `"MAGE"`,
  …), or `nil` when unknown. This is the key for `RAID_CLASS_COLORS`.
  Vanilla's own `GetWhoInfo` does not provide it, which is the main
  reason to use this table form.
- `area` — the zone name.

There is no `gender` field — vanilla's /who result stores no sex.

Vanilla also keeps the global `GetWhoInfo(index)`, which returns
positional values and no class token. This namespaced form returns a
table and adds `filename`.

### `C_FriendList.IsFriend(guid)`

Returns `true` if the player with the given GUID is on your friends
list, `false` if not.

```lua
C_FriendList.IsFriend(UnitGUID("target"))    -- true if the target is a friend
C_FriendList.IsFriend("0x00000000000ABCDE")
```

`guid` is a GUID string — the `"0x…"` form that `UnitGUID` returns.
You can also pass a plain character name, because vanilla's friends
list is keyed by name (`AddFriend(name)`).

The check reads the engine's friends list directly. It matches the
input GUID against each friend's stored GUID. The server sends that
GUID for both online and offline friends. As a fallback it also
compares character names, so the answer holds for any friend the
client saw this session.

### `C_FriendList.IsIgnored(token)`

Returns `true` if the player is on your ignore list, `false` if not.

```lua
C_FriendList.IsIgnored("Bob")
C_FriendList.IsIgnored("0x00000000000ABCDE")
```

`token` is a character name or a GUID string. The ignore list stores
GUIDs, not names. A GUID always matches. A name matches only a player
the client has seen this session — the same players `GetIgnoreName` can
name — because the name comes from the client's name cache.

### `C_FriendList.IsIgnoredByGuid(guid)`

Returns `true` if the player with the given GUID is on your ignore
list, `false` if not.

```lua
C_FriendList.IsIgnoredByGuid(UnitGUID("target"))
C_FriendList.IsIgnoredByGuid("0x00000000000ABCDE")
```

`guid` is a GUID string — the `"0x…"` form that `UnitGUID` returns.
This is the exact key the ignore list uses, so it works for every
ignored player.

### `C_FriendList.GetNumFriends()`

Returns the number of players on your friends list. The vanilla global
`GetNumFriends()` returns the same count — this is the modern
namespaced form.

```lua
C_FriendList.GetNumFriends()   -- e.g. 12
```

### `C_FriendList.GetNumOnlineFriends()`

Returns how many friends on your list are online. This is a subset of
[`GetNumFriends`](#c_friendlistgetnumfriends), which counts online and
offline friends together.

```lua
C_FriendList.GetNumOnlineFriends()   -- e.g. 3
```

Vanilla stores no online count, so the value is the number of friends
whose `connected` flag is set — the same flag
[`GetFriendInfo`](#c_friendlistgetfriendinfoname) reports.

### `C_FriendList.GetFriendInfo(name)`

Returns a `FriendInfo` table for the friend with the given name, or
`nil` if that name is not on your list. The name match is
case-insensitive.

```lua
local info = C_FriendList.GetFriendInfo("Sarahnity")
-- {
--   name = "Sarahnity", connected = true, level = 60,
--   className = "Druid", classFilename = "DRUID", area = "Stratholme",
--   guid = "0x000000003B9CB7BF",
--   afk = false, dnd = false,
--   mobile = false, referAFriend = false, rafLinkType = 0,
-- }
```

Table fields:

- `name` — the friend's character name.
- `connected` — `true` when the friend is online.
- `level` — the friend's level, or `0` when unknown.
- `className` — localized class name, or `nil` when unknown.
- `classFilename` — locale-independent class token (`"WARRIOR"`,
  `"MAGE"`, …), or `nil` when unknown. Present whenever `className`
  is. This is the key for `RAID_CLASS_COLORS` and other class tables,
  so you can color a friend's name without matching the localized
  class name.
- `area` — localized zone name, resolved to the parent zone, or `nil`
  when unknown.
- `guid` — the friend's GUID string.
- `notes` — your note for this friend, or `nil` if none. Set it with
  [`SetFriendNotes`](#c_friendlistsetfriendnotesname-notes).
- `afk` / `dnd` — the friend's status flags.
- `mobile` / `referAFriend` / `rafLinkType` — always `false` / `false`
  / `0`. Vanilla 1.12 has no mobile app, Recruit-A-Friend, or RAF link
  data.

Notes are a ClassicAPI addition — vanilla has no note field. ClassicAPI
persists them per character in
`WTF\Account\<acct>\<realm>\<char>\ClassicAPI_FriendNotes.txt`, local to
this client (not stored on the server, not shared with other machines).

### `C_FriendList.GetFriendInfoByIndex(index)`

The same `FriendInfo` table, addressed by a 1-based list index instead
of a name. Returns `nil` for an index below 1 or above
[`GetNumFriends`](#c_friendlistgetnumfriends).

```lua
for i = 1, C_FriendList.GetNumFriends() do
    local info = C_FriendList.GetFriendInfoByIndex(i)
    print(info.name, info.connected, info.className, info.area)
end
```

See [`C_FriendList.GetFriendInfo`](#c_friendlistgetfriendinfoname) for
the field list.

### `C_FriendList.SetFriendNotes(name, notes)`

Sets your note for the friend with the given name. Pass an empty string
or `nil` to clear it. There is no return value, and the name must
already be on your friends list.

```lua
C_FriendList.SetFriendNotes("Sarahnity", "raid healer")
C_FriendList.SetFriendNotes("Sarahnity", "")   -- clears the note
```

The note is stored client-side and read back through the `notes` field
of [`GetFriendInfo`](#c_friendlistgetfriendinfoname). Setting a note
fires `FRIENDLIST_UPDATE` so the friends UI and note-aware addons
refresh. See the persistence note under `GetFriendInfo`.

### `C_FriendList.SetFriendNotesByIndex(index, notes)`

The same as
[`SetFriendNotes`](#c_friendlistsetfriendnotesname-notes), addressed by
a 1-based list index instead of a name. An out-of-range index does
nothing.

```lua
C_FriendList.SetFriendNotesByIndex(1, "tank")
```

## GameObject

The gameobject analog of [`C_CreatureInfo`](#creature) — reads the
client-side gameobject cache (`gameobjectcache.wdb`, fed by
`SMSG_GAMEOBJECT_QUERY_RESPONSE`) by GO entry ID.

### `C_GameObjectInfo.GetGameObjectInfoByID(gameObjectID)`

Synchronous **peek** — returns a table for a cached gameobject, `nil`
otherwise:

```lua
local info = C_GameObjectInfo.GetGameObjectInfoByID(47297)
-- { gameObjectID = 47297, name = "Mesa Elevator", type = 11, displayID = 360 }
```

`type` is the `GameObjectType` (0=door, 1=button, 3=chest, 5=generic,
6=trap, 10=goober, …). Field offsets verified against the binary
(name `[block+0x08]`) and real `gameobjectcache.wdb` rows (Mesa
Elevator 47297→type 11; Windrunner 176250→type 15 displayID 7087).

### `C_GameObjectInfo.RequestLoadGameObjectByID(gameObjectID)`

Asynchronously fetches an uncached gameobject — issues
`SMSG_GAMEOBJECT_QUERY` and fires **`GAMEOBJECT_DATA_LOAD_RESULT(
gameObjectID, success)`** when it lands, after which
`GetGameObjectInfoByID` returns its data. Returns `true` if accepted;
fires synchronously on a cache hit. Same shape as
[`C_CreatureInfo.RequestLoadCreatureByID`](#c_creatureinforequestloadcreaturebyidcreatureid),
and rides the same shared `Cache::QueryLoad` dispatcher (the gameobject
cache is a sibling class of the creature cache with its own
`_GetRecord`/parser, hooked separately).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("GAMEOBJECT_DATA_LOAD_RESULT")
f:SetScript("OnEvent", function()  -- arg1 = gameObjectID, arg2 = success
    if arg2 then local info = C_GameObjectInfo.GetGameObjectInfoByID(arg1) end
end)
C_GameObjectInfo.RequestLoadGameObjectByID(7000032)
```

### `ClosestGameObjectPosition(gameObjectID)`

Returns `xPos, yPos, distance` — the world position of the nearest game
object with the given GO-template ID, and its distance from the player in
yards. Returns nothing (nil) when no matching object is in range.

```lua
local x, y, dist = ClosestGameObjectPosition(1617)   -- nearest Herb Bush, etc.
if x then
    -- x, y = world coords; dist = yards from the player
end
```

The game-object counterpart of
[`ClosestUnitPosition`](#closestunitpositioncreatureid) — same visible-
object-manager scan, filtered to game objects (GUID prefix `0xF110`).
Same retail-vs-vanilla caveat: this finds the nearest **currently-visible**
object of that entry rather than reading retail's static starting-zone
database (which vanilla ships no equivalent of).

## GameTooltip

### `GameTooltip:SetItemByID(itemID)`

Modern method that renders an item tooltip from just an itemID. The
1.12 workaround was constructing an item hyperlink and calling
`SetHyperlink` — `tooltip:SetHyperlink("item:" .. id .. ":0:0:0:0:0:0:0")`
— which works but forces every caller to know the hyperlink format.

```lua
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
GameTooltip:SetItemByID(6948)  -- Hearthstone
GameTooltip:Show()
```

Implementation: snprintf the hyperlink string and dispatch to the
existing `Script_GameTooltip_SetHyperlink` (registry slot 12).

> **Item cache caveat.** 1.12 lazy-loads item data into the
> client-side cache at `0x00C0E2A0` — the cache is fed by
> `SMSG_ITEM_QUERY_SINGLE_RESPONSE` only when the player encounters
> an item. For an itemID the player has never seen, the tooltip
> renders only the name; full data appears once the cache is warm.
>
> The fix is to ensure the item is cached before opening the tooltip:
>
> ```lua
> if C_Item.IsItemDataCachedByID(itemID) then
>     GameTooltip:SetItemByID(itemID)
> else
>     C_Item.RequestLoadItemDataByID(itemID)
>     -- register for ITEM_DATA_LOAD_RESULT(loadedItemID, success)
>     -- and call SetItemByID once the matching itemID arrives
> end
> ```
>
> This caching behavior matches what `C_Item.GetItemInfoInstant`
> documents — same underlying cache. Modern WoW (5.0+) has the same
> caveat, just with `C_Item.RequestLoadItemData(itemLocation)` /
> `Item:OnItemLoad`-style continuation.

### `GameTooltip:SetItemByGUID(itemGUID)`

Renders the tooltip for a specific item *instance* identified by
GUID — same string `C_Item.GetItemGUID` returns
(`"0xHHHHHHHHLLLLLLLL"`). Distinct from `SetItemByID`: this path
goes through the live CGItem, so the tooltip includes enchant
lines, random-suffix-decorated name + bonuses, and locked/broken
state — the same depth `SetBagItem` / `SetInventoryItem` would
produce for that specific copy.

```lua
local guid = C_Item.GetItemGUID({ equipmentSlotIndex = 10 }) -- legs
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
GameTooltip:SetItemByGUID(guid)
GameTooltip:Show()
```

Resolves through the engine's own `FUN_OBJECT_RESOLVE_BY_GUID`
(same path `C_Item.GetItemLocation` uses), builds the full
`item:N:enchant:gem:gem:gem:gem:suffix:unique`-style hyperlink via
the engine's link builder, then dispatches to
`Script_GameTooltip_SetHyperlink`. The link round-trip is what
gives `GameTooltip:GetItem()` the dressed link back when called
after — same shape `GetInventoryItemLink` / `GetContainerItemLink`
emit for the same slot.

Silent no-op when the GUID is malformed, the item isn't currently
loaded in the client (e.g. items only the server knows about), or
the GUID resolves to a non-item object (creature/gameobject GUIDs
go to a different resolver and won't match).

### `GameTooltip:SetUnitAura(unit, index, [filter])`

Modern unified-aura method. 1.12 splits this into `SetUnitBuff` and
`SetUnitDebuff`; we dispatch to the right one based on the `filter`
string (`"HARMFUL"` → `SetUnitDebuff`, anything else → `SetUnitBuff`).
`filter` defaults to helpful when omitted, matching modern.

```lua
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
GameTooltip:SetUnitAura("player", 1, "HELPFUL")  -- first buff
GameTooltip:SetUnitAura("player", 1, "HARMFUL")  -- first debuff
GameTooltip:SetUnitAura("player", 1)              -- defaults to HELPFUL
GameTooltip:Show()
```

Pure dispatcher — no engine changes; the underlying logic is whatever
1.12's `SetUnitBuff` / `SetUnitDebuff` already does. Just lets you use
the modern call shape (which most aura libraries backport from)
without conditionally splitting on filter.

### `GameTooltip:SetSpellByID(spellID)`

Renders a spell tooltip for any `spellID`, including spells the player has
not learned. The stock 1.12 `GameTooltip:SetSpell(slot, bookType)` only works
for entries in the player's spellbook (or pet book). `SetSpellByID` bypasses
the spellbook indirection and calls WoW's internal tooltip builder directly.

```lua
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
GameTooltip:SetSpellByID(133)  -- Fireball
GameTooltip:Show()
```

### `GameTooltip:AddSpellByID(spellID)`

The append counterpart to [`SetSpellByID`](#gametooltipsetspellbyidspellid):
adds a spell's full tooltip (name, cast time, cost, range, description) to
the *current* tooltip **without clearing** the existing lines. Works for any
`spellID`, learned or not. Lets you compose tooltips — e.g. a header line
plus a spell block.

```lua
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
GameTooltip:AddLine("Rank 3 grants:")
GameTooltip:AddSpellByID(133)  -- Fireball, appended below the header
GameTooltip:Show()
```

Unlike `SetSpellByID`, this does **not** update what
[`GetSpell`](#gametooltipgetspell) reports — the appended spell isn't the
tooltip's "primary" spell, so `GetSpell` keeps reflecting whatever `SetX`
call (if any) built the base tooltip. Silent no-op for `spellID <= 0` or an
unknown spell.

> Vanilla's tooltip builder only exposes "append" via its internal talent
> "next rank" preview, which emits a `Next rank:` header instead of the
> spell name; we overwrite that header line with the real name so the
> appended block reads normally.

### `GameTooltip:GetItem()`

Returns `(name, link, itemID)` for whichever item the tooltip is
currently displaying, or nothing if it isn't showing an item. Modern
WoW returns only `(name, link)`; we extend with `itemID` as a third
return so callers don't have to gsub-extract it from the link.

The engine stashes two fields per Set* item call:
- `tooltip+0x398` ← itemID (always populated)
- `tooltip+0x380/+0x384` ← item GUID (only when there's a real
  CGItem — `SetBagItem`, `SetInventoryItem`, `SetLootItem`,
  `SetMerchantItem`, etc. Zero for `SetItemByID` / `SetHyperlink`
  which have no instance.)

Both fields are zeroed by the per-tooltip Clear on Hide / before the
next Set*.

```lua
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")

-- SetItemByID — no per-instance data, basic link returned
GameTooltip:SetItemByID(6948)
local name, link, id = GameTooltip:GetItem()
-- name = "Hearthstone"
-- link = "|cffffffff|Hitem:6948:0:0:0:0:0:0:0|h[Hearthstone]|h|r"
-- id   = 6948

-- SetInventoryItem — full dressed link with enchant + random suffix
GameTooltip:SetInventoryItem("player", INVSLOT_BACK)
local name, link, id = GameTooltip:GetItem()
-- name = "Superior Cloak of the Eagle"   -- decorated name, agrees with the link
-- link = "|cff1eff00|Hitem:9805:247:843:0|h[Superior Cloak of the Eagle]|h|r"
-- id   = 9805
```

Two link paths:

| Set* path | Link form | Dressing |
|---|---|---|
| `SetBagItem`, `SetInventoryItem`, `SetLootItem`, `SetMerchantItem`, … | Full dressed link via the engine's own builder at `0x0052AE00` | Enchant ID, random-suffix factor, unique ID, suffix-decorated name |
| `SetItemByID`, `SetHyperlink` (item:N with no instance data) | Basic colored link | itemID + cached quality + cached name only |

The dressed-link path works for items not in player inventory
(merchant, loot, mailbox, trade window, etc.) — the engine's
resolver finds any CGItem the client has loaded.

Returns nothing for: non-item tooltip, uncached itemID on the
no-GUID path (fires a background cache warmup), or empty name.

### `GameTooltip:GetSpell()`

Returns `(name, rank, spellID)` for whichever spell the tooltip is
currently displaying, or nothing if it isn't showing a spell. The
engine stashes the spellID on the tooltip frame at `+0x39C` whenever
any `SetX` spell path runs (`SetSpell`, `SetSpellByID`,
`SetUnitBuff`/`SetUnitDebuff`, `SetTalent`, etc.) and zeroes it on
the next `Clear` / `Hide`.

```lua
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
GameTooltip:SetSpellByID(25306)
local name, rank, spellID = GameTooltip:GetSpell()
-- name = "Fireball"
-- rank = "Rank 12"
-- spellID = 25306
```

`rank` is the empty string (not nil) for spells whose Spell.dbc rank
slot is blank — most racials, talent passives, and proc-triggered
spells. This matches the modern semantics where the rank position is
always populated.

### `GameTooltip:HasItem()` / `GameTooltip:HasSpell()`

Boolean companions to `GetItem` / `GetSpell`. Return `true` if the
tooltip is currently displaying an item / spell, `false` otherwise.
Cheaper than `GetItem` / `GetSpell` when all you need is the
predicate — single `uint32` read against `OFF_TOOLTIP_ITEM_ID` /
`OFF_TOOLTIP_SPELL_ID`, no DBC lookup or link-build.

```lua
if GameTooltip:HasItem() then
    local _, link = GameTooltip:GetItem()
    -- only do the GetItem work when we actually need it
end
```

### `GameTooltip:GetUnitGUID()` / `GameTooltip:HasUnit()`

`GetUnitGUID()` returns `(name, guidString)` for whichever unit the
tooltip is currently displaying, or nothing if it isn't showing a
unit. Return order mirrors modern's `GameTooltip:GetUnit()` (name
first) — so addons porting from
`local name, unit = ttip:GetUnit()` can swap to `GetUnitGUID` and
keep their existing destructuring. `name` is the unit's display name
— the same string that appears in the tooltip header, or one of the
engine's `"UNKNOWNOBJECT"` / `"Unknown Being"` fallbacks for a remote
unit whose info hasn't been queried yet. `guidString` is the
canonical `"0xHHHHHHHHLLLLLLLL"` format returned by
[`UnitGUID(unit)`](#unitguidunit).

`HasUnit()` is a boolean companion — returns `true` if the tooltip is
currently displaying a unit.

```lua
GameTooltip:SetUnit("target")
local name, guid = GameTooltip:GetUnitGUID()
-- name = "Hogger"
-- guid = "0xF130001234..." (Creature) or "0x000000...ABC123" (Player)

if GameTooltip:HasUnit() then
    -- cheap predicate, no name-resolution work
end
```

> **Why not match modern's `GetUnit()` signature?** Modern WoW's
> `GetUnit()` returns `(name, unitToken)` where `unitToken` is the
> exact `"target"` / `"focus"` / `"mouseover"` / etc. string passed
> to `SetUnit`. Vanilla 1.12 drops the token at the
> `Script_GameTooltip_SetUnit` boundary — it converts the token to a
> 64-bit GUID and discards the original string. Reconstructing a
> plausible token by walking known tokens and reverse-matching by
> GUID is possible but lossy (multiple tokens can refer to the same
> GUID — `"target"` and `"raid1"` simultaneously, for instance), so
> we expose the GUID directly instead, which is what addons actually
> need for cross-referencing with `UnitGUID`, the NameCache, etc.

### `GameTooltip:GetGameObject()` / `GameTooltip:HasGameObject()`

`GetGameObject()` returns `(name, id, guid)` for whichever gameobject
the tooltip is currently displaying, or nothing if it isn't showing
one. `name` is the cached display name (or `""` until the gameobject
cache has loaded the record). `id` is the gameobject's template /
entry — same key `gameobjectcache.wdb` uses, and what the server
sent in the spawn packet. `guid` is the canonical
`"0xHHHHHHHHLLLLLLLL"` format.

`HasGameObject()` is the boolean companion — returns `true` if the
tooltip is currently displaying a gameobject (single `uint32` read,
no resolution work).

```lua
-- Mouse over a chest, vein, signpost, etc. then:
local name, id, guid = GameTooltip:GetGameObject()
-- name = "Iron Deposit"
-- id   = 1731
-- guid = "0xF110000006C30000..."

if GameTooltip:HasGameObject() then
    -- cheap predicate
end
```

There is **no Lua-callable `SetGameObject` method** in vanilla 1.12
— gameobjects only populate the tooltip via in-world mouseover. The
engine's hover handler at `FUN_00492890` dispatches to a tooltip
populator (`0x0052AA20`) that writes the GUID into
`tooltip+0x370/+0x374`, right between the unit slot (`+0x368`) and
the item slot (`+0x380`). The shared tooltip-clear zeroes the same
slot on every subsequent `SetX`, so these methods follow the same
gating pattern as `HasUnit` / `HasSpell` / `HasItem`.

Returns nothing for: tooltip not showing a gameobject, or the
gameobject has left the engine's visibility window (object manager
evicted it). `HasGameObject()` stays true in the latter case —
the tooltip-frame slot still has the GUID, even though the live
object has been freed.

### `GameTooltip:GetOwner()`

Returns the frame that called `tooltip:SetOwner(frame, anchor)`, or
`nil` if the tooltip hasn't been owned by anyone since its last
`Clear` / `Hide`. Vanilla 1.12 ships `SetOwner` and `IsOwned` but
never added the matching reader; modern Classic Era's signature is
backported here for parity.

```lua
GameTooltip:SetOwner(SomeFrame, "ANCHOR_TOPLEFT")
local owner = GameTooltip:GetOwner()
-- owner == SomeFrame
```

Returns only the owner frame, not `(owner, anchorPoint)` like modern
Classic. The anchor string is reachable via vanilla's native
[`GameTooltip:GetAnchorType()`](https://wowwiki-archive.fandom.com/wiki/API_GameTooltip_GetAnchorType)
(slot 5 in the GameTooltip method registry).

### `GameTooltip:SetTalentByID(talentID)`

Renders a tooltip for the talent identified by `Talent.dbc` primary
key — the natural pair to
[`GetTalentIDByIndex`](#gettalentidbyindextabindex-talentindex-classid).
Works for any class's talents, not just the player's.

Two-tier resolution:

| Tier | When it applies | Tooltip rendered |
|------|-----------------|------------------|
| Player class (rich) | `talentID` belongs to one of the player's loaded tabs | Full talent tooltip — name, "Rank N/M", description, prereqs, "click to learn" prompts |
| Cross-class (fallback) | `talentID` is from another class | Spell tooltip for the talent's rank-1 spellID — name, cast time, range, mana cost, description |

The fallback exists because vanilla 1.12 only loads the local
player's class talent data into the engine's per-player TabInfo
arrays. For other classes, we look up the talent in `Talent.dbc`
directly and dispatch the rank-1 spell tooltip — functionally
"what does this talent do?" without the rank counter. Modern WoW
adds talent name and "Rank 0/N" decorations on top of the spell
description for cross-class; we don't replicate that here yet.

Silent no-op (no tooltip change) when:

- `talentID` doesn't match any record in `Talent.dbc`
- `talentID` is `nil`, non-numeric, or non-positive

```lua
-- Player's own class — rich tooltip
local talentID = GetTalentIDByIndex(1, 9)        -- player's tab 1, talent 9
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
GameTooltip:SetTalentByID(talentID)
GameTooltip:Show()

-- Other class — spell tooltip for the talent's primary ability
GameTooltip:SetTalentByID(2065)                   -- works regardless of player class
GameTooltip:Show()
```

### `GameTooltip:SetInventoryItemByID(itemID)`

Renders the tooltip for the **equipped instance** of `itemID` —
walks character-pane slots 1..19, finds the matching item, and
shows it with its actual enchants, random-suffix stats, and
broken/locked state. Distinct from
[`SetItemByID`](#gametooltipsetitembyiditemid), which shows the
clean ItemSparse data with no instance-specific decorations.

For example, on a pair of boots with a run-speed enchant equipped:

| Method | Renders |
|--------|---------|
| `SetItemByID(<bootsID>)` | Base boots tooltip — name, armor, durability, level req. **No enchant.** |
| `SetInventoryItemByID(<bootsID>)` | Same plus `Enchanted: Minor Speed` and any random-suffix lines. |

Silent no-op if the item isn't currently equipped — fall back to
`SetItemByID` for unworn items, or check via
[`C_Item.IsEquippedItem`](#c_itemisequippeditemitem) first.

When the player has duplicates of the same itemID equipped
(matched MH/OH weapons, identical rings, identical trinkets), the
**lower-numbered slot wins** — MAINHAND before OFFHAND, FINGER1
before FINGER2, TRINKET1 before TRINKET2. Matches modern client
behavior (verified empirically).

```lua
local _, _, _, _, _, _, _, _, _, _, _, _, _, link = GetItemInfo(itemID)
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
if C_Item.IsEquippedItem(itemID) then
    GameTooltip:SetInventoryItemByID(itemID)  -- shows enchants/suffix
else
    GameTooltip:SetItemByID(itemID)            -- shows base stats
end
GameTooltip:Show()
```

### `GameTooltip:SetHyperlinkCompareItem("itemLink" [, offset, shiftButton, comparisonTooltip])`

Fills the tooltip with the item **currently equipped** in the slot the
given item would occupy — a grey `Currently Equipped` header, the
equipped item's own tooltip, then (when `shiftButton` is set) a
green/red per-stat delta breakdown of how the given item compares.
This is the method behind item-comparison ("shopping") tooltips;
`FrameXML`'s `GameTooltip_ShowCompareItem` drives it.

Call it on the tooltip that should show the comparison (typically
`ShoppingTooltip1`/`ShoppingTooltip2`, but any `GameTooltipTemplate`
frame works).

| Arg | Meaning |
|-----|---------|
| `itemLink` | The item being compared (chat link, `item:` string, or bare itemID). Optional if `comparisonTooltip` is given. |
| `offset` | 1-based slot selector for two-slot items — rings, trinkets, and one-hand weapons expose `offset` 1 and 2 (Finger1/2, Trinket1/2, MainHand/OffHand). Default 1. |
| `shiftButton` | Gates the stat-change breakdown. Deltas show by default (`true`/`1`/`nil`/omitted) and are hidden only for an explicit `false`/`0` (header + equipped item only). Both the boolean and vanilla `1`/`nil` conventions are accepted. |
| `comparisonTooltip` | Optional. When `itemLink` is omitted, the compared item is taken from whatever this tooltip is displaying (matches the retail tooltip-to-tooltip call). |

**Returns** the number of comparison slots for the item (1, or 2 for
rings/trinkets/one-hand weapons; 0 if it isn't equippable, isn't
cached, or nothing is equipped in the chosen slot) — so a caller knows
whether a second `offset` is worth querying.

The stat deltas cover the same keys as
[`C_Item.GetItemStats`](#c_itemgetitemstatsitemlink) (base stats,
resistances, weapon DPS, and on-equip-spell bonuses like crit / attack
power / spell power), colored with the client's
`INCREASE_STAT_COLOR` / `DECREASE_STAT_COLOR`. The method only
populates the tooltip; the caller shows/anchors it (as retail's
`SetHyperlinkCompareItem` does).

```lua
-- Compare a hovered bag item against what's equipped:
local link = GetContainerItemLink(bag, slot)
ShoppingTooltip1:SetOwner(GameTooltip, "ANCHOR_NONE")
local slots = ShoppingTooltip1:SetHyperlinkCompareItem(link, 1, true)
ShoppingTooltip1:Show()
if slots == 2 then  -- ring/trinket/1H: also compare the other slot
    ShoppingTooltip2:SetOwner(GameTooltip, "ANCHOR_NONE")
    ShoppingTooltip2:SetHyperlinkCompareItem(link, 2, true)
    ShoppingTooltip2:Show()
end
```

### `GameTooltip:IsEquippedItem()`

Returns `true` when the item the tooltip is **currently displaying** is
equipped in one of the player's character-pane slots (1..19). Backports
the 3.3.5 `GameTooltip:IsEquippedItem` method.

Works regardless of how the tooltip was populated — link paths
(`SetItemByID`, `SetHyperlink`) and CGItem paths (`SetInventoryItem`,
`SetBagItem`, `SetItemByGUID`, …) both resolve to the displayed item.
Returns `false` for a tooltip that isn't showing an item (e.g. a spell
tooltip) with no false positives.

```lua
GameTooltip:SetInventoryItem("player", INVSLOT_HAND)
if GameTooltip:IsEquippedItem() then ... end  -- true

GameTooltip:SetBagItem(0, 1)
if GameTooltip:IsEquippedItem() then ... end  -- false unless that item is also worn
```

### `OnTooltipSet*` scripts

`OnTooltipSetItem` / `OnTooltipSetSpell` / `OnTooltipSetUnit` /
`OnTooltipSetGameObject` — real frame scripts, settable with the standard
`SetScript` / `GetScript` /
`HookScript` — that fire whenever a tooltip's **item**, **spell**, **unit**, or
**gameobject** is set. Backport the modern tooltip scripts so addons annotate
tooltips by hooking one script per object type instead of wrapping every
`Set*` method. Available on all GameTooltip-type frames (`GameTooltip`,
`ItemRefTooltip`, `ShoppingTooltip1/2`, `AtlasLootTooltip`, …).

| Script | Fires after any of |
|---|---|
| `OnTooltipSetItem` | `SetBagItem`, `SetInventoryItem`, `SetHyperlink` (`item:`), `SetMerchantItem`, `SetAuctionItem`, `SetItemByID`, … |
| `OnTooltipSetSpell` | `SetSpell`, `SetSpellByID`, `SetTalent`, `SetShapeshift` |
| `OnTooltipSetUnit` | `SetUnit` and unit mouseover |
| `OnTooltipSetGameObject` | gameobject mouseover (herb/ore nodes, chests, mailboxes, signs) |

Vanilla only shipped `OnTooltipAddMoney` / `OnTooltipCleared` /
`OnTooltipSetDefaultAnchor`; these four are the backported set.

The handler receives the tooltip as the **global `this`**, the 1.12
frame-script convention — *not* a `self` argument (like every built-in vanilla
script: `OnShow`, `OnEvent`, `OnTooltipCleared`, …). Modern-style
`function(self) self:… end` handlers will see `self == nil`; use `this`.

```lua
GameTooltip:HookScript("OnTooltipSetItem", function()
    local name, link, id = this:GetItem()   -- the tooltip is the global `this`
    if id then this:AddLine("ID: " .. id, 0.6, 0.6, 0.6) end
end)

GameTooltip:HookScript("OnTooltipSetUnit", function()
    local name, unit = this:GetUnit()
    if unit then this:AddLine(UnitClassBase(unit), 0.6, 0.6, 0.6) end
end)
```

**Auras don't fire `OnTooltipSetSpell`** — `SetUnitBuff` / `SetUnitDebuff` /
`SetPlayerBuff` / `SetUnitAura` use a separate aura-tooltip builder, so hovering
a buff/debuff does not trigger the spell script. This matches retail, where unit
auras are a distinct tooltip data type rather than a spell.

**Caveat — the event fires from inside the tooltip build.** Lightweight handler
work is fine: reading `this:GetItem()` / `this:GetUnit()`, `this:AddLine(...)`,
printing, etc. But avoid *re-entrant* tooltip rebuilds from the handler — e.g.
`GameTooltip_ShowCompareItem()`, or a `Set*` call on another tooltip. Because the
handler runs mid-build, re-entering the tooltip / FrameScript machinery can
collide with other DLLs that hook the same Lua paths (nampower, SuperWoW,
weirdutils, …) and crash the client. If you need heavy work like that, defer it
to the next frame:

```lua
GameTooltip:HookScript("OnTooltipSetItem", function()
    C_Timer.After(0, function() GameTooltip_ShowCompareItem() end)
end)
```

### `GameTooltip:SetEquipmentSet(name)`

Fills the tooltip with a summary of the named equipment set: header,
total item count, and per-bucket counts (equipped / in inventory /
ignored / missing). Each missing item is listed on its own line by
name. Mirrors 4.3.4's native `GameTooltip:SetEquipmentSet` at
`0x0046E690`.

```lua
GameTooltip:SetOwner(UIParent, "ANCHOR_CURSOR")
GameTooltip:SetEquipmentSet("MyTank")
GameTooltip:Show()
```

```
MyTank                     (header, white)
14 items
12 equipped                (green)
1 in inventory
1 slots ignored            (gray)
Missing: Crown of the Endless Conqueror   (red)
```

Silent no-op if no set with that name exists. The owner anchor must
be set before the call — `SetEquipmentSet` only fills the lines, it
doesn't re-anchor the tooltip frame.

Item classification reuses `Locations::FindGUID` (the same walk
`GetItemCount` uses), so items in the bank count as "in inventory"
without requiring the bank window to be open. The 4.3.4 binary did
the same.

**Localization.** The count lines are formatted through
[`Game::Lua::PushLocalizedFormatInt`](#) — Blizzard's `ITEMS_VARIABLE_QUANTITY`,
`ITEMS_EQUIPPED`, `ITEMS_IN_INVENTORY`, `ITEM_SLOTS_IGNORED`, and
`ITEM_MISSING` FrameXML globals are tried first, with English C-string
fallbacks for servers stripped of standard GlobalStrings. The
companion `!!!ClassicAPI` addon ships English defaults for these keys
so the tooltip works on bare-bones builds where FrameXML hasn't
populated them.

**Missing-item names.** The on-disk equipment-set format
(`WTF\Account\<acct>\<realm>\<char>\ClassicAPI_EquipmentSets.txt`)
persists each slot as both a 64-bit GUID and the itemID at save
time:

```
set 1
  name=MyTank
  icon=INV_Helmet_03
  slot 1 guid=0xC00000000A1B2C3D item=51220
  slot 5 ignored
```

The GUID is the live handle for items currently in inventory; the
itemID is the type identifier used when the live `CGItem` isn't
findable (item sold, mailed, on another character, etc.) — without
it we couldn't recover the name for missing items. Sets saved
before the itemID field was added load fine but render their
missing slots with the count summary `%d missing` instead of named
lines; re-saving a set repopulates itemIDs.

### `GameTooltip:SetTotem(slot)`

Fills the tooltip with the shaman totem currently active in `slot`
(`1` Fire, `2` Earth, `3` Water, `4` Air). A TBC (2.4.0) tooltip method
backported to 1.12; mirrors retail's `Script_GameTooltip_SetTotem`.

```lua
GameTooltip:SetOwner(TotemButton, "ANCHOR_BOTTOMRIGHT")
GameTooltip:SetTotem(1)   -- Fire slot; shows itself, no :Show() needed
```

```
Searing Totem              (name, yellow)
45 Sec                     (time remaining, white)
```

Two lines, matching retail exactly: the totem name (`NORMAL_FONT_COLOR`)
and the time remaining (white). Built natively with the engine's own
per-tooltip clear and raw add-line — the same path `SetSpellByID` and
`SetHyperlinkCompareItem` use — and **shows itself** at the end, so no
trailing `:Show()` is required (retail's `TotemFrame.xml` OnEnter calls
`SetOwner` then `SetTotem` with no show, and the C method shows). Set the
owner/anchor before the call.

Silent no-op when the slot has no active totem, so a totem-button
`OnEnter` can call it unconditionally.

**Localization.** The time line mirrors retail's `SecondsToTimeAbbrev`
(raw seconds under a minute, minutes rounded up above) and formats
through the FrameXML GlobalStrings `SPELL_TIME_REMAINING_SEC` /
`SPELL_TIME_REMAINING_MIN` (English `%d Sec` / `%d Min` fallbacks for
servers stripped of those keys). The totem name comes from the summon
spell's localized `Spell.dbc` name. Slot data is the same tracker behind
[`GetTotemInfo`](#gettoteminfoslot).

## Globals

### `CLASSIC_API_VERSION`

Defined once FrameScript has booted. Addons can use this to detect
that the DLL is loaded and which version is in use. The value is
`X*10000 + Y*100 + Z` for a tag of `vX.Y.Z` passed to CMake at
configure time via `-DCLASSICAPI_TAG=vX.Y.Z`.

Untagged builds (local dev, CI without a release tag) get a
**sentinel value of `99999999`** — encoded as `v9999.99.99`, chosen
to be higher than every plausible real release so addon-side
feature gates like `CLASSIC_API_VERSION >= 10200` don't reject the
dev build.

```lua
if CLASSIC_API_VERSION and CLASSIC_API_VERSION >= 10200 then
    -- ClassicAPI v1.2.0 or newer is loaded (or untagged dev build)
end
```

If you specifically want to detect a dev build:

```lua
if CLASSIC_API_VERSION == 99999999 then
    -- running against a locally-built, untagged DLL
end
```

### `LE_EXPANSION_*`

The retail / Classic Era expansion-level enum, exposed as Lua globals
so addons backporting from later expansions don't have to gate on
`if LE_EXPANSION_CLASSIC then` (the constant being defined is itself
the version probe). Values match the modern `Enum.ExpansionLevel`
table. The matching helper functions
(`GetClassicExpansionLevel` / `ClassicExpansionAtLeast` /
`ClassicExpansionAtMost`) live in the [Expansion section](#expansion).

| Constant                              | Value |
|---------------------------------------|------:|
| `LE_EXPANSION_LEVEL_CURRENT`          | `0` *(this is Classic)* |
| `LE_EXPANSION_CLASSIC`                | `0` |
| `LE_EXPANSION_BURNING_CRUSADE`        | `1` |
| `LE_EXPANSION_WRATH_OF_THE_LICH_KING` | `2` |
| `LE_EXPANSION_CATACLYSM`              | `3` |
| `LE_EXPANSION_MISTS_OF_PANDARIA`      | `4` |
| `LE_EXPANSION_WARLORDS_OF_DRAENOR`    | `5` |
| `LE_EXPANSION_LEGION`                 | `6` |
| `LE_EXPANSION_BATTLE_FOR_AZEROTH`     | `7` |
| `LE_EXPANSION_SHADOWLANDS`            | `8` |
| `LE_EXPANSION_DRAGONFLIGHT`           | `9` |
| `LE_EXPANSION_WAR_WITHIN`             | `10` |
| `LE_EXPANSION_MIDNIGHT`               | `11` |

```lua
-- Classic version check using modern idiom
if LE_EXPANSION_LEVEL_CURRENT < LE_EXPANSION_BURNING_CRUSADE then
    -- pure-1.x (vanilla / Classic Era) code path
end

-- Probe for ClassicAPI presence
if LE_EXPANSION_LEVEL_CURRENT then
    -- the constants are defined → ClassicAPI is loaded
end
```

### `LE_ITEM_QUALITY_*`

The item-quality enum (modern `Enum.ItemQuality`), exposed as Lua
globals so addons backporting modern code can do
`if quality >= LE_ITEM_QUALITY_RARE then ...` against the integer
quality returned by `GetItemInfo` / `C_Item.GetItemInfoInstant`.

| Constant                       | Value | Color in tooltip |
|--------------------------------|------:|------------------|
| `LE_ITEM_QUALITY_POOR`         | `0`   | gray (junk) |
| `LE_ITEM_QUALITY_COMMON`       | `1`   | white |
| `LE_ITEM_QUALITY_UNCOMMON`     | `2`   | green |
| `LE_ITEM_QUALITY_RARE`         | `3`   | blue |
| `LE_ITEM_QUALITY_EPIC`         | `4`   | purple |
| `LE_ITEM_QUALITY_LEGENDARY`    | `5`   | orange |
| `LE_ITEM_QUALITY_ARTIFACT`     | `6`   | gold *(TBC+ only)* |
| `LE_ITEM_QUALITY_HEIRLOOM`     | `7`   | light blue *(WotLK+ only)* |
| `LE_ITEM_QUALITY_WOWTOKEN`     | `8`   | orange *(WoD+ only)* |

Values 0..5 (POOR..LEGENDARY) correspond to actual qualities present
in vanilla 1.12. Higher values are exposed for source compatibility
with modern addons — vanilla items will never carry those quality
values, so a comparison like `quality == LE_ITEM_QUALITY_HEIRLOOM`
trivially never matches and the rest of the code path is unreachable.

```lua
local _, _, quality = GetItemInfo(itemID)
if quality and quality >= LE_ITEM_QUALITY_RARE then
    -- highlight in UI
end
```

### `LE_UNIT_STAT_*`

The primary-stat enum (modern `Enum.UnitStat`), exposed as Lua globals
so addons can index `UnitStat(unit, statIndex)` symbolically:

| Constant                | Value | Stat |
|-------------------------|------:|------|
| `LE_UNIT_STAT_STRENGTH`  | `1`  | Strength |
| `LE_UNIT_STAT_AGILITY`   | `2`  | Agility |
| `LE_UNIT_STAT_STAMINA`   | `3`  | Stamina |
| `LE_UNIT_STAT_INTELLECT` | `4`  | Intellect |
| `LE_UNIT_STAT_SPIRIT`    | `5`  | Spirit |

Values are stable across every WoW expansion — `UnitStat("player", 1)`
has always returned strength.

```lua
local _, effective = UnitStat("player", LE_UNIT_STAT_AGILITY)
```

### `Enum.AddOnSecurityStatus`

The integer enum `C_AddOns.GetAddOnSecurity` returns. Matches
Blizzard's `Enum.AddOnSecurityStatus`:

| Value | Field          | Notes |
|------:|----------------|-------|
| `0`   | `Secure`       | Blizzard-signed addons. |
| `1`   | `Insecure`     | User addons; default for any registered addon. |
| `2`   | `Banned`       | Server-disqualified entries. |
| `3`   | `NotAvailable` | No addon by that name / index. |

```lua
if C_AddOns.GetAddOnSecurity(name) == Enum.AddOnSecurityStatus.Secure then
    -- it's a Blizzard_* addon
end
```

### `Enum.InventoryType`

The equip-type enum — the numeric `inventoryType` reported by
[`C_Item.GetItemInfoInstant`](#c_itemgetiteminfoinstantitem) /
[`C_Item.GetItemInventoryType`](#c_itemgetiteminventorytypeitemlocation--c_itemgetiteminventorytypebyiditem),
and the argument the `C_Item.GetItemInventorySlot*` functions take.

| Value | Field | Value | Field |
|------:|-------|------:|-------|
| 0 | `IndexNonEquipType` | 15 | `IndexRangedType` |
| 1 | `IndexHeadType` | 16 | `IndexCloakType` |
| 2 | `IndexNeckType` | 17 | `Index2HweaponType` |
| 3 | `IndexShoulderType` | 18 | `IndexBagType` |
| 4 | `IndexBodyType` | 19 | `IndexTabardType` |
| 5 | `IndexChestType` | 20 | `IndexRobeType` |
| 6 | `IndexWaistType` | 21 | `IndexWeaponmainhandType` |
| 7 | `IndexLegsType` | 22 | `IndexWeaponoffhandType` |
| 8 | `IndexFeetType` | 23 | `IndexHoldableType` |
| 9 | `IndexWristType` | 24 | `IndexAmmoType` |
| 10 | `IndexHandType` | 25 | `IndexThrownType` |
| 11 | `IndexFingerType` | 26 | `IndexRangedrightType` |
| 12 | `IndexTrinketType` | 27 | `IndexQuiverType` |
| 13 | `IndexWeaponType` | 28 | `IndexRelicType` |
| 14 | `IndexShieldType` | | |

Values `29..34` (`IndexProfessionToolType`, `IndexProfessionGearType`,
`IndexEquipablespell{Offensive,Utility,Defensive,Weapon}Type`) are
post-vanilla and included for parity — vanilla items never report them.

```lua
if C_Item.GetItemInventoryType(loc) == Enum.InventoryType.IndexHeadType then ...
```

### `Enum.ItemClass`

The item-class enum — the numeric `classID` reported as the 12th return
of [`GetItemInfo`](#c_itemgetiteminfoiteminfo) and taken by
[`GetItemClassInfo`](#getitemclassinfoclassid) /
[`GetItemSubClassInfo`](#getitemsubclassinfoclassid-subclassid). The
numeric values match retail; the obsolete slots keep their modern key
names even though `ItemClass.dbc` labels them `"…(OBSOLETE)"`.

| Value | Field | Value | Field |
|------:|-------|------:|-------|
| 0 | `Consumable` | 8 | `ItemEnhancement` |
| 1 | `Container` | 9 | `Recipe` |
| 2 | `Weapon` | 10 | `CurrencyTokenObsolete` |
| 3 | `Gem` | 11 | `Quiver` |
| 4 | `Armor` | 12 | `Questitem` |
| 5 | `Reagent` | 13 | `Key` |
| 6 | `Projectile` | 14 | `PermanentObsolete` |
| 7 | `Tradegoods` | 15 | `Miscellaneous` |

Values `16..19` (`Glyph`, `Battlepet`, `WoWToken`, `Profession`) are
post-vanilla and included for parity — vanilla items never report them.

```lua
if select(12, GetItemInfo(id)) == Enum.ItemClass.Weapon then ...
```

### `Enum.ItemQuality`

The item-quality enum — the numeric `quality` reported by
[`GetItemInfo`](#c_itemgetiteminfoiteminfo) /
[`C_Item.GetItemQuality`](#c_itemgetitemqualityitemlocation--c_itemgetitemqualitybyiditem).

| Value | Field | Value | Field |
|------:|-------|------:|-------|
| 0 | `Poor` | 4 | `Epic` |
| 1 | `Common` | 5 | `Legendary` |
| 2 | `Uncommon` | 6 | `Artifact` |
| 3 | `Rare` | | |

The post-vanilla `Heirloom` (7) and `WoWToken` (8) tiers are omitted —
no such items exist on 1.12.

```lua
if select(3, GetItemInfo(id)) == Enum.ItemQuality.Epic then ...
```

### `Enum.PowerType`

The integer enum `UnitPowerType` returns and `UnitPower` /
`UnitPowerMax` accept. Vanilla 1.12 only defines slots 0..4 — the
WotLK additions (Runes, Runic Power) and post-WotLK extensions
aren't included. Slot 4 is `Happiness` (vanilla pet happiness),
not modern's `ComboPoints` reuse of the same number.

| Value | Field        | Notes |
|------:|--------------|-------|
| `-2`  | `HealthCost` | Sentinel for "use HEALTH instead of POWER". |
| `-1`  | `None`       | Sentinel for "unit's primary power" (= omit the arg). |
| `0`   | `Mana`       | |
| `1`   | `Rage`       | |
| `2`   | `Focus`      | |
| `3`   | `Energy`     | |
| `4`   | `Happiness`  | Pet happiness — vanilla-specific. |

```lua
local rage = UnitPower("player", Enum.PowerType.Rage)
```

### `Enum.SpellBookSpellBank`

Selects which spellbook a `C_SpellBook.*` slot query reads. Passed as the
`spellBank` argument to
[`C_SpellBook.GetSpellBookItemInfo`](#c_spellbookgetspellbookiteminfoslotindex-spellbank).

| Value | Field    |
|------:|----------|
| `0`   | `Player` |
| `1`   | `Pet`    |

### `Enum.SpellBookItemType`

Categorizes a spellbook slot. Vanilla's spellbook only ever holds real
spells, so `C_SpellBook.GetSpellBookItemInfo` returns `Spell` for a
player-book slot and `PetAction` for a pet-book slot. The other values
exist for signature parity with retail but never occur in 1.12.

| Value | Field         | Notes |
|------:|---------------|-------|
| `0`   | `None`        | Empty slot. Never returned — an empty slot yields `nil` instead. |
| `1`   | `Spell`       | A player-book spell. |
| `2`   | `FutureSpell` | A not-yet-learned trainer spell. Never occurs in 1.12. |
| `3`   | `PetAction`   | A pet-book spell. |
| `4`   | `Flyout`      | A flyout group — a later-expansion concept. Never occurs. |

## Glue

Backports of two `C_Glue` session helpers. `C_Glue` is registered on the glue
(login / character-select) Lua state; `IsOnGlueScreen` is *also* registered
in-game, so code that runs in either environment can branch on it. Neither
needs an engine flag — both answers fall out of which glue boot / Lua state
is asking.

### `C_Glue.IsFirstLoadThisSession()`

Returns `true` only while the **first** glue screen since the process
launched is showing, `false` on every later return to the glue screen. Login
↔ character-select transitions stay within the first glue session (they never
enter the world), so it stays `true` across them — it flips `false` only once
you've entered the world and logged back out. Glue state only.

```lua
if C_Glue.IsFirstLoadThisSession() then
    -- one-time-per-launch startup (intro cinematic, news, …)
end
```

Backed by a process-static count of glue boots (the glue script-registration
pass fires once per boot: initial launch + each world→glue logout); boot 1 is
the first load.

### `C_Glue.IsOnGlueScreen()`

Returns `true` when a GlueXML screen is showing (login or character select —
no character in the world), `false` in the world. Registered on **both** Lua
states, so it's callable from glue and in-game code alike. The two states are
mutually exclusive (the glue state exists only when no character is in the
world), so the answer is fixed per state — `true` from the glue registration,
`false` from the in-game one.

```lua
if C_Glue.IsOnGlueScreen() then
    -- at login / character select
end
```

## Gossip

Retail-shaped wrappers around vanilla's flat `GetGossipText` /
`GetGossipOptions` / `GetGossipAvailableQuests` / `GetGossipActiveQuests`
/ `SelectGossipOption` / `SelectGossipAvailableQuest` /
`SelectGossipActiveQuest` / `CloseGossip` surface. The data is the
same — these calls just read the engine's two gossip-state arrays
(`0x00BBBE90` for options, `0x00BB74C0` for quests, both filled by the
SMSG_GOSSIP_MESSAGE handler at `0x004E26E0`) and shape it into the
modern struct-tables that addons ported from retail expect.

**Fields the 1.12 server simply doesn't send** — and therefore aren't
in any of the returned tables — include `rewards` and `spellID` (added
after the post-vanilla quest/spell system rework), per-option
`status` (Available / Unavailable / Locked / AlreadyComplete — vanilla
servers don't compute this), and modern UX hints like `overrideIconID`
and `selectOptionWhenOnlyOption`.

The `icon` field is the raw icon-type byte from SMSG_GOSSIP_MESSAGE
(server-extensible — pservers can add custom NPC types past the
Blizzard 0..10 range). Default Blizzard types resolve to
`Interface\GossipFrame\<Type>GossipIcon`:

| Value | Type | Value | Type |
|------:|:-----|------:|:-----|
| `0` | Gossip       | `6`  | Banker       |
| `1` | Vendor       | `7`  | Petition     |
| `2` | Taxi         | `8`  | Tabard       |
| `3` | Trainer      | `9`  | BattleMaster |
| `4` | Healer       | `10` | Auctioneer   |
| `5` | Binder (innkeeper) | | |

### `C_GossipInfo.GetText()`

Returns the gossip greeting string the engine resolved from the NPC's
referenced `NPC_TEXT.dbc` row. Same value `GetGossipText()` returns;
provided so addons that prefer the `C_GossipInfo` namespace don't have
to mix the two surfaces.

```lua
print(C_GossipInfo.GetText())
```

### `C_GossipInfo.GetOptions()`

Returns an array of gossip-option tables in display order. Each entry
contains:

| Field            | Type    | Notes |
|------------------|---------|-------|
| `gossipOptionID` | number  | Vanilla `optionIndex`. The same value `C_GossipInfo.SelectOption` expects; arbitrary integer assigned by the server. |
| `name`           | string  | Option text (locale-applied by the server). |
| `icon`           | number  | Raw icon-type byte from SMSG_GOSSIP_MESSAGE — passed through unmapped so pserver-added types (anything past the default `0..10` range) survive. See the type table above for the default Blizzard categories and the `Interface\GossipFrame\<Type>GossipIcon` path each one resolves to. |
| `flags`          | number  | Bit 0 set = `boxCoded` (the option asks for confirmation text — `Are you sure?` boxes). |
| `orderIndex`     | number  | 1-based position in the emitted list. Matches the index `SelectGossipOption` (legacy) expects. |

```lua
for _, opt in ipairs(C_GossipInfo.GetOptions()) do
    print(opt.gossipOptionID, opt.name, "icon", opt.icon)
end
```

### `C_GossipInfo.GetAvailableQuests()`

Returns an array of deliverable-quest tables (quests the giver is
offering to start), in display order. Filter mirrors vanilla's
`GetGossipAvailableQuests` — status field at `+0x008` not in `{3, 4}`.

| Field        | Type   | Notes |
|--------------|--------|-------|
| `questID`    | number | Quest.dbc row ID. |
| `title`      | string | Localized quest title. |
| `questLevel` | number | Quest level. |

### `C_GossipInfo.GetActiveQuests()`

Returns an array of active-quest tables (quests the giver wants you
to turn in, in-progress or complete). Status field at `+0x008` in
`{3, 4}`.

| Field        | Type    | Notes |
|--------------|---------|-------|
| `questID`    | number  | Quest.dbc row ID. |
| `title`      | string  | Localized quest title. |
| `questLevel` | number  | Quest level. |
| `isComplete` | boolean | `true` when the engine's status byte is `4` (ready to turn in); `false` when it's `3` (still in progress). |

### `C_GossipInfo.GetNumOptions()` / `GetNumAvailableQuests()` / `GetNumActiveQuests()`

Convenience counters — return the length of each of the three lists
above without building the table.

### `C_GossipInfo.SelectOption(gossipOptionID)` / `SelectOptionByIndex(orderIndex)`

Picks a gossip option. `SelectOption` resolves the modern
`gossipOptionID` to vanilla's 1-based slot, then tail-calls the
engine's native `SelectGossipOption`. `SelectOptionByIndex` is a thin
passthrough for callers that already have the slot index (e.g. from
`opt.orderIndex` on a `GetOptions()` entry, or from a UI button bound
directly to the slot).

Both end in the same CMSG; the option-ID variant exists so addons can
drive selections off the modern `gossipOptionID` key without keeping
their own slot mapping. Returns nothing.

Vanilla's `SelectGossipOption` doesn't accept a confirmation-text
argument, so for `boxCoded` options the engine's own confirm dialog
runs as usual — there is no way to send the password from the script.

### `C_GossipInfo.SelectAvailableQuest(questID)`

Picks a deliverable quest by `questID`. Walks the active-vs-available
filter the same way `GetAvailableQuests` does, locates the matching
1-based slot, and tail-calls the engine's `SelectGossipAvailableQuest`.
Returns nothing.

### `C_GossipInfo.SelectActiveQuest(questID)`

Picks an in-progress quest by `questID`. Same shape as
`SelectAvailableQuest` but walks the active filter and tail-calls
`SelectGossipActiveQuest`. Returns nothing.

### `C_GossipInfo.CloseGossip()`

Closes the gossip window. Direct passthrough to the engine's
`CloseGossip`. Returns nothing.

## Hooks

### `hooksecurefunc(name, callback)` / `hooksecurefunc(table, name, callback)`

Modern post-call hook: the original function runs first, then
`callback` runs with the same args (return values discarded). The
original's return values propagate to the caller.

```lua
hooksecurefunc("GetSpellInfo", function(spellID)
    print("GetSpellInfo called with " .. tostring(spellID))
end)

hooksecurefunc(GameTooltip, "SetInventoryItem", function(self, unit, slot)
    -- runs after the engine fills the tooltip
    print("inventory tooltip:", unit, slot)
end)
```

The "secure" label refers to taint-propagation behavior introduced in
2.0 for protected-frame manipulation. Vanilla 1.12 has no taint
system, so the function is functionally equivalent to a plain
"after-hook" — just preserves modern API parity for addons being
backported from later expansions.

Implemented in pure C: builds a Lua C closure with `(orig, callback)`
as upvalues; the wrapper calls orig with `LUA_MULTRET`, then callback,
then returns orig's full result list. No return-count cap — works
correctly for functions returning any number of values.

Errors via `lua_error` on:
- Non-string `name`
- Non-function `callback`
- `target[name]` not resolvable to a function (covers typos and
  hooking unknown frame methods)

## Input

1.12 ships `IsShiftKeyDown` / `IsControlKeyDown` / `IsAltKeyDown` but
they only report "any shift/ctrl/alt" — there's no built-in way to tell
left from right. These seven functions add the missing distinction
plus an `IsModifierKeyDown()` rollup.

### `IsLeftShiftKeyDown()` / `IsRightShiftKeyDown()`
### `IsLeftControlKeyDown()` / `IsRightControlKeyDown()`
### `IsLeftAltKeyDown()` / `IsRightAltKeyDown()`

Each returns `1` when the corresponding key is physically down, `nil`
otherwise — matching the convention 1.12's own `IsShiftKeyDown` etc.
use.

```lua
if IsLeftShiftKeyDown() and not IsRightShiftKeyDown() then
    -- left-shift-only binding
end
```

### `IsModifierKeyDown()`

Returns `1` if **any** of the six modifier keys is down, `nil`
otherwise. Equivalent to
`IsShiftKeyDown() or IsControlKeyDown() or IsAltKeyDown()` but in one
call.

### `IsMouseButtonDown([button])`

Returns `true` if the given mouse button is currently held, `false`
otherwise. With no argument (or `nil`), returns `true` if **any**
mouse button is held.

`button` is either a 1-based ID or a name string:

| ID  | Name |
|-----|------|
| `1` | `"LeftButton"` |
| `2` | `"RightButton"` |
| `3` | `"MiddleButton"` |
| `4` | `"Button4"` (XBUTTON1 / first side button) |
| `5` | `"Button5"` (XBUTTON2 / second side button) |

Unrecognized IDs / names return `false` (matches modern semantics:
the named button just isn't held — bad input doesn't fall through
to the any-button check).

State is maintained from the same `WH_GETMESSAGE` hook that drives
`GLOBAL_MOUSE_DOWN` / `GLOBAL_MOUSE_UP`, so press/release transitions
register at exactly the moment the corresponding event fires. Hook
scopes to WoW's message queue — clicks made while the game is
alt-tabbed out don't deliver, which means the state can stay
"down" if a button was held while focus left the window. Most uses
of `IsMouseButtonDown` are gated behind a frame that just received
mouse focus, so this rarely matters in practice.

```lua
if IsMouseButtonDown("RightButton") then
    -- right-button-held bindings
end
```

### `GetMouseButtonClicked()`

Returns the name of the button responsible for the mouse handler
currently running (`"LeftButton"`, `"RightButton"`, `"MiddleButton"`,
`"Button4"`, `"Button5"`), or `nil` when no click is being handled.

Modern addons call this inside a mouse handler (`OnClick`,
`OnMouseDown`, `OnMouseUp`, `OnDragStart`, …) to learn which button
drove it — an alternative to reading vanilla's `arg1`, and readable
from nested helper functions where `arg1` isn't in scope. The value
is captured by the same `WH_GETMESSAGE` hook behind
`GLOBAL_MOUSE_DOWN` / `GLOBAL_MOUSE_UP`, then evicted a couple of
frames later so — matching the real API — it reads `nil` outside a
handler rather than lingering as the last click forever.

The button stays readable for the whole frame its click lands in (so
multiple addons hooking the same `OnClick` all see it), and while a
button is physically held (so `OnDragStart` reads it). It is a
best-effort replica: WoW dispatches these handlers deferred from the
OS message, so eviction is time-based (per-frame) rather than exactly
bracketed around the handler as on retail. Values can therefore
linger ~2 frames past a click — harmless, since it's only meaningful
inside a mouse handler.

```lua
button:SetScript("OnClick", function()
    if GetMouseButtonClicked() == "RightButton" then
        -- right-click behavior
    end
end)
```

## Instance

### `GetInstanceInfo()`

Returns the same 9-value tuple modern WoW does (TBC and later), with
vanilla-degenerate values for the fields the 1.12 client doesn't
actually track:

```
name, instanceType, difficultyID, difficultyName, maxPlayers,
dynamicDifficulty, isDynamic, instanceID, instanceGroupSize
```

- `name` — localized instance/zone name from `Map.dbc`.
- `instanceType` — `"none"` (open world), `"party"` (5-man dungeon),
  `"raid"`, `"pvp"` (battleground), or `"arena"` (unused in vanilla).
- `difficultyID` — always `1`. No heroic mode pre-TBC.
- `difficultyName` — always `"Normal"`.
- `maxPlayers` — type-default cap: `5` for dungeons, `40` for raids,
  `40` for battlegrounds, `0` for open world. **See caveat below.**
- `dynamicDifficulty` — always `0` (dynamic difficulty was a Cataclysm
  addition).
- `isDynamic` — always `false`.
- `instanceID` — current map ID. Modern API calls this "instanceID" but
  it's really the `Map.dbc` row ID — both vanilla and modern WoW put
  the same value here.
- `instanceGroupSize` — mirrors `maxPlayers` (no per-group-config
  variants in vanilla).

```lua
/dump GetInstanceInfo()
-- In Stormwind:    "Kalimdor",   "none", 1, "Normal",  0, 0, false,   1,  0
-- In Deadmines:    "Deadmines",  "party",1, "Normal",  5, 0, false,  36,  5
-- In Molten Core:  "Molten Core","raid", 1, "Normal", 40, 0, false, 409, 40
```

**Caveat on `maxPlayers`.** Vanilla genuinely has no per-instance cap
data client-side — `MapDifficulty.dbc` was a TBC addition. The server
enforces caps via `SMSG_TRANSFER_ABORTED` when entry is denied, but
that information never reaches the client otherwise. So we return the
type's canonical max. **Zul'Gurub and AQ20 return `40` instead of
their true `20`; non-AV battlegrounds (WSG `10`, AB `15`) return `40`
instead of their true cap; any custom raid on a private server
(e.g. Turtle WoW) returns `40` regardless of its real cap.** Addons
that need exact caps must supply their own per-mapID table.

## Item

> ### `itemLocation` argument shapes
>
> Every `C_Item.*` function on this page that takes `itemLocation` accepts
> any of three forms, matching the modern `ItemLocation` mixin plus a
> GUID-string convenience form:
>
> ```lua
> { equipmentSlotIndex = N }     -- 1-based, character pane
> { bagID = B, slotIndex = S }   -- both required
> "0xHHHHHHHHLLLLLLLL"           -- a GUID string from C_Item.GetItemGUID
> ```
>
> Table forms are O(1) — the engine knows the slot. The GUID-string form
> is O(~80) — the function walks equipment slots 1..19 and bags 0..4
> comparing each CGItem's stored GUID against the requested one. Bank and
> keyring are not walked. Fine for sporadic addon use; avoid for per-frame
> polling.

### `C_Item.DoesItemExist(itemLocation)` / `C_Item.DoesItemExistByID(item)`

Existence checks straight off the engine's inventory manager (location
form) and item cache (ID form). No `GetItemInfo` chaining — both
functions read directly from the structures they need.

- `C_Item.DoesItemExist(itemLocation)` — `true` iff the equipment-slot
  or `(bagID, slotIndex)` location resolves to a populated inventory
  slot on the active player. Empty slots, missing bags, or malformed
  tables return `false`.
- `C_Item.DoesItemExistByID(item)` — `true` iff the cache currently
  has data for `item`. Cache miss returns `false` and kicks off the
  network query in the background; a follow-up call after
  [`GET_ITEM_INFO_RECEIVED`](#c_itemrequestloaditemdatabyiditem--c_itemrequestloaditemdataitemlocation)
  will succeed. Accepts a numeric `itemID`, a bare `"item:NNN..."`
  string, or a full chat link.

```lua
if C_Item.DoesItemExist({equipmentSlotIndex = INVSLOT_HEAD}) then ... end
if C_Item.DoesItemExistByID(6948) then ... end
```

### `C_Item.EquipItemByName(itemInfo [, dstSlot])`

Finds the first item in the player's bags matching `itemInfo` and
equips it. With `dstSlot` (a 1-based character-pane slot, 1..19),
equips to that specific slot; without, the engine auto-picks based on
the item's inventory type.

`itemInfo` accepts the same shapes as
[`C_Item.IsEquippedItem`](#c_itemisequippeditemitem) — itemID number,
bare `"item:N"` string, full chat link, or a localized item name. Name
matching is case-insensitive against each candidate's *decorated* name
(random suffix included — a suffixed item matches its full name, not the
base), the same shared predicate `C_Item.IsEquippedItem` uses.

Returns nothing. Silently no-ops when:

- the input is `nil`, an empty string, or otherwise unparseable
- no matching item is in bags (already-equipped items aren't moved —
  matches modern API behavior)
- the engine refuses the equip — combat, locked item, type mismatch
  with `dstSlot`, locked equipment slot, etc.

Two paths based on `dstSlot`:

- **Explicit `dstSlot` (1..19): cursor-free direct swap.** Calls the
  engine's own `FUN_INVENTORY_SWAP` (`0x005E0C40`) — the same
  primitive `Script_EquipCursorItem` dispatches to after resolving
  cursor state. We hand it the source item GUID, source container
  GUID, source linear slot, and target paperdoll slot; the engine
  builds and sends the CMSG_SWAP_INV_ITEM (or CMSG_AUTOEQUIP_ITEM
  for cross-container) packet through its normal pipeline. **The
  cursor is never read or written.** An item already on the cursor
  stays on the cursor.
- **No `dstSlot` (engine auto-picks slot from inventory type):**
  falls back to the cursor-pickup + `AutoEquipCursorItem` path
  because 1.12's auto-pick logic reads off cursor state. For this
  path only, the function refuses to operate (no-op) when
  `CursorHasItem()` is already true, to avoid clobbering whatever's
  held.

```lua
-- By itemID, auto-pick slot:
C_Item.EquipItemByName(2589)

-- By name, force into off-hand (slot 17):
C_Item.EquipItemByName("Linen Cloth", 17)

-- From a chat link:
C_Item.EquipItemByName(itemLink)
```

### `C_Item.GetCurrentItemLevel(itemLocation)` / `C_Item.GetDetailedItemLevelInfo(item)`

Returns the item's base ilvl from `m_itemLevel` (cache record `+0x38`).
Vanilla 1.12 has no per-instance scaling (no upgrades, no warforging),
so "current" and "base" item level are always identical — both APIs
return the same single value. Modern `GetDetailedItemLevelInfo` is
spec'd to return `(current, isPreview, base)`; we push only the
current level, callers that care about the extra returns will see
`nil` for them.

```lua
local ilvl = C_Item.GetCurrentItemLevel({equipmentSlotIndex = INVSLOT_HEAD})
```

### `C_Item.GetItemCount(itemInfo, [includeBank], [includeUses])`

Returns the player's total count of `itemInfo` — equipped items +
bags, and optionally bank.

```
count = C_Item.GetItemCount(itemInfo [, includeBank [, includeUses]])
```

- `itemInfo` — numeric `itemID` or string containing `"item:NNN"`
  (full chat links work). Item names are NOT accepted (vanilla has
  no name → ID resolver).
- `includeBank` *(optional, default false)* — also walk bank slots
  (bag `-1` for the main bank, bags `5..10` for bank-bag slots).
- `includeUses` *(optional, default false)* — when `true`, multiplies
  each match by the item's spell-charges count. A wand with 50 charges
  contributes 50 instead of 1. Items without charges pass their plain
  stack count through unchanged, so the flag is a no-op for them.

```lua
local n = C_Item.GetItemCount(2589)               -- Linen Cloth in bags + equipped
local n = C_Item.GetItemCount(2589, true)         -- + bank
local n = C_Item.GetItemCount("item:2589")        -- string form works too

-- Equipped items count toward the total:
local trinketID = GetInventoryItemID("player", INVSLOT_TRINKET1)
C_Item.GetItemCount(trinketID)                    -- 1

-- includeUses multiplies by charges for charged items:
C_Item.GetItemCount(wandID, false, true)          -- 50 for one 50-charge wand
C_Item.GetItemCount(linenID, false, true)         -- same as stack count (no charges)
```

> **Bank works cold — no banker visit required.** The 1.12 server
> sends bank inventory at login alongside the rest of the player's
> data; only the engine's own `GetItemBySlot` gates bank slots until
> the window opens. We bypass that gate by reading the GUID array
> directly out of the player invMgr and resolving each entry via the
> engine's object resolver — same path `GetItemBySlot` would take
> internally if the gate let us through. Counts are correct from
> session start.

Walks the 19 equipment slots via
`Item::Location::ResolveEquipmentSlot`, then bags via the same
`Item::Location::ResolveBag` chain
[`C_Container.GetContainerItemID`](#c_containergetcontaineritemidbagindex-slotindex)
uses. Slot counts come from the engine's `Script_GetContainerNumSlots`
so custom server bag sizes work transparently. Stack counts read
directly off the CGItem's m_objectFields descriptor at +0x20
(`ITEM_FIELD_STACK_COUNT`, verified by decoding
`Script_GetContainerItemInfo` at `0x004F9670`).

Equivalent to the legacy global `GetItemCount` (since 3.0) and the
modern `C_Item.GetItemCount` introduced in 10.x.

### `C_Item.GetItemData(itemLocation)` / `C_Item.GetItemDataByID(item)`

ClassicAPI-only kitchen-sink reader: returns a single table with every
field we can extract from the cached `ItemStats_C` record, so addons
that need more than a couple of fields don't have to chain a dozen
`C_Item.*` calls. Saves both stack churn and the cost of redoing the
cache lookup for every accessor.

```
data = C_Item.GetItemDataByID(item)        -- numeric id / "item:NNN" / chat link
data = C_Item.GetItemData(itemLocation)    -- {bagID,slotIndex} or {equipmentSlotIndex}
```

Returns `nil` if the input doesn't resolve to a valid itemID or the
item isn't cached yet. Does **not** warm the cache — same passive-
reader contract as
[`C_Item.GetItemInfoInstant`](#c_itemgetiteminfoinstantitem).
Callers that need to wait for cache fill should call
[`C_Item.RequestLoadItemDataByID`](#c_itemrequestloaditemdatabyiditem--c_itemrequestloaditemdataitemlocation)
and listen for `ITEM_DATA_LOAD_RESULT`.

Returned table:

| Key | Type | Source / notes |
|-----|------|---------------|
| `itemID` | number | Echoed from input. |
| `name` | string | `m_name[0]` — localized display name. |
| `description` | string \| nil | `m_description`. Omitted when empty. |
| `icon` | string \| nil | `"Interface\\Icons\\<name>"`. Omitted when no icon. |
| `displayInfoID` | number | `m_displayInfoID` → `ItemDisplayInfo.dbc` row. |
| `quality` | number | 0..5 — see `LE_ITEM_QUALITY_*` enum. |
| `classID` | number | `m_class` — `ItemClass.dbc` row. |
| `subclassID` | number | `m_subClass` — `ItemSubClass.dbc` row. |
| `className` | string | Localized class name (e.g. `"Weapon"`). |
| `subclassName` | string | Localized subclass name (e.g. `"One-Handed Swords"`). |
| `inventoryType` | number | `m_inventoryType` — raw integer. |
| `equipLoc` | string | `"INVTYPE_*"` constant or `""` for non-equippable. |
| `bindType` | number | `m_bonding` — 0=none, 1=BoP, 2=BoE, 3=BoU, 4=Quest. |
| `flags` | number | Raw `m_flags` u32. |
| `isConjured` | bool | Flag bit `0x2`. |
| `isOpenable` | bool | Flag bit `0x4`. Same bit `C_Item.IsItemOpenable` reads. |
| `isLootable` | bool | Flag bit `0x10` — has loot generators (right-click loot). |
| `isWrapper` | bool | Flag bit `0x200` — gift-wrappable. |
| `maxStackSize` | number | `m_stackable`. |
| `maxCount` | number | `m_maxCount` — 0=unlimited, 1=unique, otherwise per-character cap. |
| `containerSlots` | number | `m_containerSlots` — bag slot count, 0 for non-bags. |
| `bagFamily` | number | `m_bagFamily` converted to modern bitmask (`1 << (id-1)`). |
| `buyPrice` | number | Vendor buy price in copper. |
| `sellPrice` | number | Vendor sell price in copper. |
| `itemLevel` | number | Base ilvl from `ItemSparse`. |
| `requiredLevel` | number | Minimum character level. |
| `requiredSkill` | number | `SkillLine.dbc` row, 0=none. |
| `requiredSkillRank` | number | Skill rank required. |
| `requiredSpell` | number | `Spell.dbc` row required to learn / use, 0=none. |
| `requiredHonorRank` | number | PvP honor rank required, 0=none. |
| `requiredCityRank` | number | Reserved; 0 for vanilla. |
| `requiredFaction` | number | `Faction.dbc` row, 0=none. |
| `requiredFactionRank` | number | Reputation tier (Friendly/Honored/…). |
| `allowableClass` | number | Class bitmask, `-1` = all classes. |
| `allowableRace` | number | Race bitmask, `-1` = all races. |
| `armor` | number | Armor value. |
| `block` | number | Shield block value. |
| `maxDurability` | number | 0 for items without durability. |
| `stats` | table | Sparse `{ [statType] = statValue, … }`. Empty for items without stat allocations. |
| `resistanceHoly` | number | Holy resistance. |
| `resistanceFire` | number | Fire resistance. |
| `resistanceNature` | number | Nature resistance. |
| `resistanceFrost` | number | Frost resistance. |
| `resistanceShadow` | number | Shadow resistance. |
| `resistanceArcane` | number | Arcane resistance. |
| `damageMin` | table | 5-element float array — weapon min damage per damage slot. |
| `damageMax` | table | 5-element float array. |
| `damageType` | table | 5-element integer array — damage school per slot. |
| `delay` | number | Weapon swing time in ms. |
| `ammoType` | number | Ammo subclass for ranged weapons. |
| `rangedModRange` | number | Range modifier for bows/guns. |
| `spells` | table | Sparse array of `{id, trigger, charges, cooldown, category, categoryCooldown}` records — one per non-empty spell slot (up to 5). |
| `useSpellID` | number \| nil | Convenience — the spellID of the first slot with trigger=ON_USE (0). Omitted when the item has none. Same value [`C_Item.GetItemSpell`](#c_itemgetitemspellitem) returns. |
| `lockID` | number | `Lock.dbc` row for the item's lock, 0=none. |
| `itemSet` | number | `ItemSet.dbc` row, 0=not part of a set. |
| `pageText` | number | `PageText.dbc` row for readable items. |
| `pageMaterial` | number | Book page material. |
| `languageID` | number | Language for in-game-readable books. |
| `startQuest` | number | `Quest` row started by right-clicking this item. |
| `material` | number | Material type (cloth/leather/metal etc. for hit sounds). |
| `sheath` | number | Weapon sheath style. |
| `randomProperty` | number | Random property template id. |
| `area` | number | `AreaTable.dbc` — bound area for area-locked items, 0=none. |
| `map` | number | `Map.dbc` — bound map for map-locked items, 0=none. |

```lua
local data = C_Item.GetItemDataByID(6948)  -- Hearthstone
-- data.name == "Hearthstone"
-- data.quality == 1, data.classID == 15, data.subclassID == 0
-- data.bindType == 1   (BoP)
-- data.useSpellID == 8690   (the recall spell)
-- data.maxStackSize == 1, data.maxCount == 1

local sword = C_Item.GetItemDataByID(2092)  -- Worn Shortsword
-- sword.equipLoc == "INVTYPE_WEAPONMAINHAND"
-- sword.damageMin[1] == 1.0, sword.damageMax[1] == 2.0, sword.delay == 1900
-- sword.stats == {}   (no stat allocations on the starter sword)
```

**Cache state.** The function reads the same `DBCache_ItemStats`
network cache every other `C_Item.*` reader consults. Items already in
the player's inventory are typically populated by the engine's natural
prefetch before `PLAYER_ENTERING_WORLD` returns; for other items
(quest rewards, AH items, link hovers, etc.) the data lands once a
`SMSG_ITEM_QUERY_SINGLE_RESPONSE` arrives. Use
`C_Item.RequestLoadItemDataByID` if you specifically need to trigger
the load.

**Stats encoding.** The `stats` table is keyed by the **ItemModType**
enum (vanilla's encoding):

| Key | Meaning |
|-----|---------|
| 0 | Mana |
| 1 | Health |
| 3 | Agility |
| 4 | Strength |
| 5 | Intellect |
| 6 | Spirit |
| 7 | Stamina |

Higher-numbered slots (consumable per-second-regen, defense rating,
hit/crit/dodge/parry chance, weapon/spell-power) only appear on items
the server actually carries — vanilla 1.12 mostly uses 0..7. Empty
slots (both type and value zero) are omitted, so iterating `stats`
yields only the stats actually allocated.

No equivalent in modern WoW — `GetItemInfo` returns the 14-tuple
piecemeal and `C_Item.GetItemInfo` adds a few more. ClassicAPI's
`GetItemData` is a single-call superset useful for backporting tooltip
addons and item-data caches.

### `C_Item.GetItemFamily(item)`

Returns the BagFamily bitmask for an item — i.e., what kind of
specialty bag it belongs in. `0` means "general-purpose" (no specialty
bag preference). Returns `nil` if the item isn't in the cache.

```
familyBitmask = C_Item.GetItemFamily(item)
```

`item` accepts a numeric `itemID` or a string containing `"item:NNN"`
(the bare shorthand and full chat links both parse correctly), same as
[`C_Item.GetItemInfoInstant`](#c_itemgetiteminfoinstantitem).

```lua
C_Item.GetItemFamily(2512)   -- 1   (Wooden Arrow → quiver-family)
C_Item.GetItemFamily(2516)   -- 2   (Light Shot → ammo pouch-family)
C_Item.GetItemFamily(6265)   -- 4   (Soul Shard → soul bag-family)
C_Item.GetItemFamily(2447)   -- 32  (Peacebloom → herb bag-family)
C_Item.GetItemFamily(6948)   -- 0   (Hearthstone → general-purpose)
```

Bitmask values follow the vanilla `ItemBagFamily.dbc` IDs converted to
the modern bitmask form (`1 << (familyID - 1)`). The IDs 1–9 match
retail exactly; **10–13 are Turtle WoW custom families** (retail vanilla
1.12 stops at Keys):

| Bit | Value  | Raw ID | Family                | Notes |
|-----|--------|--------|-----------------------|-------|
| 0   | 1      | 1      | Quiver (arrows)       | |
| 1   | 2      | 2      | Ammo Pouch (bullets)  | |
| 2   | 4      | 3      | Soul Bag              | |
| 5   | 32     | 6      | Herb Bag              | |
| 6   | 64     | 7      | Enchanting Bag        | |
| 7   | 128    | 8      | Engineering Bag       | |
| 8   | 256    | 9      | Keyring               | |
| 9   | 512    | 10     | **Meat Bag** (Turtle) | ⚠ collides with retail's Gem Bag (`0x200`) |
| 10  | 1024   | 11     | **Fish Bag** (Turtle) | ⚠ collides with retail's Mining Bag (`0x400`) |
| 11  | 2048   | 12     | **Leather Bag** (Turtle) | no retail equivalent bit |
| 12  | 4096   | 13     | **Mining Bag** (Turtle)  | ⚠ not retail's Mining Bag bit (`0x400`) |

> **Turtle custom families & the retail collision.** Turtle added four
> cooking/gathering bag families (Meat, Fish, Leather, Mining) as
> `ItemBagFamily.dbc` rows 10–13, rather than reusing retail's later
> IDs 4 (Leatherworking) / 5 (Inscription), which vanilla left unused.
> Because we convert the raw ID straight through `1 << (rawID - 1)`,
> these land on bits 9–12 — the same numeric values retail assigned to
> **Gem Bag** (`0x200`) and **Mining Bag** (`0x400`). So an addon
> backported from retail that hard-codes `family == 0x200` to mean "Gem
> Bag" will misread a Turtle Meat Bag. The values are self-consistent
> *within* Turtle (a Meat Bag and the meat items it holds both report
> `0x200`, which is all the "does this item fit this bag" test needs) —
> just don't assume retail's bit meanings for anything above `0x100`.

> **Encoding deviation under the hood.** 1.12 actually stores the raw
> 1-based BagFamily ID (`arrow=1, bullet=2, soul shard=3, herb=6, …`).
> Modern WoW (Wrath+) flipped to the bitmask form for the same field.
> We convert on the way out via `bitmask = 1 << (rawID - 1)`, so
> callers backporting from modern see the encoding they expect — addons
> can `band(family, FAMILY_BAG_HERB_BAG)` directly.

> **Legacy Blizzard bags derive from `(class, subClass)`.** The stock
> vanilla bags shipped with an empty `m_bagFamily` field — the four
> profession bags (Soul Bag, Herb Bag, Enchanting Bag, Engineering Bag,
> all Container class) *and* quivers/ammo pouches (Quiver class). For a
> bag whose field is `0` we recover the family from its class + subclass
> — the same signal the `"24 Slot Soul Bag"` tooltip line is built from.
> A Felcloth Bag reports `0x4` (Soul Shards), a quiver `0x1` (arrows), an
> ammo pouch `0x2` (bullets) — each matching the items it holds. Keyed on
> class because subclass numbers repeat across classes (Container
> subclass 2 = Herb Bag, Quiver subclass 2 = Quiver). This is automatic;
> you no longer need to read `subClass` yourself. (Turtle's own custom
> bags — Meat/Fish/Leather/Mining — populate the field directly; a
> Turtle-specific resolver module supplies their mapping as a safety net,
> kept separate from the stock table.)

> **`nil` vs `0`.** Modern WoW returns `0` for items the cache lookup
> fails on; we return `nil` so callers can distinguish "item not
> cached, retry after the event lands" from "item exists but has no
> family preference." Both are safe to treat as `0` for routing-logic
> purposes; the distinction helps debugging.

> **Auto-warmup on cache miss.** First call for an uncached item
> returns `nil` AND triggers the engine's cache fill in the
> background. Listen for `GET_ITEM_INFO_RECEIVED(itemID, success)` and
> retry once the matching `itemID` arrives. This matches Classic Era
> 1.15's observed behavior of "nil first call, value second call." We
> diverge from 1.15 in one detail: 1.15's `GetItemFamily` doesn't fire
> any event when the cache lands (silent fill), whereas we fire
> `GET_ITEM_INFO_RECEIVED` to stay consistent with our other implicit-
> warmup paths (`GetItemInfo`, `SetItemByID`). Addons that already
> listen for that event get the notification for free.

Equivalent to the legacy global `GetItemFamily` (since 3.0) and the
modern `C_Item.GetItemFamily` introduced in 10.x.

### `C_Item.GetItemGUID(itemLocation)`

Returns the per-instance 64-bit GUID of the item at the location,
formatted as `"0xHHHHHHHHLLLLLLLL"` (16 hex digits, hi dword first).
Same format `UnitGUID` uses — 1.12 GUIDs are plain qwords with no
`"Item-Server-..."`-style prefix scheme (modern's prefix format
arrived in 6.x). Returns `nil` for empty / invalid locations.

```lua
local guid = C_Item.GetItemGUID({equipmentSlotIndex = INVSLOT_HEAD})
-- "0x40000000000DEFCA"
```

Reads CGItem instance block at `+0x08` → GUID qword at `+0x00`.
The GUID is stable per-character-session and survives moves between
bags / character pane, so it's the right identifier for "this exact
item instance" — equipment-set tracking, soulbind matching, or any
addon code that needs to follow a single item across slot moves.

### `C_Item.GetItemID(itemLocation)`

Returns the itemID of the item at the given location, or `nil` if the slot
is empty or the location is malformed. Useful as the input to
`GetItemInfoInstant`/`GetItemInfo` when you only know which slot an item
came from (rather than its link or ID).

Accepts the same `itemLocation` shapes as `IsBound`:

```lua
local id = C_Item.GetItemID({equipmentSlotIndex = INVSLOT_HEAD})
if id then
    local _, type, subtype = C_Item.GetItemInfoInstant(id)
    -- ...
end
```

### `C_Item.GetItemInfo(itemInfo)`

The full modern `GetItemInfo` tuple, sourced from the client-side item cache
plus the class/subclass/inventory-type DBC name lookups. The namespaced
counterpart to the stock global `GetItemInfo` (which returns only vanilla's
short tuple); this returns the wide modern set backported addons expect.

Accepts a numeric `itemID`, an `"item:NNN"` string, or a full chat link.

Returns 18 values:

| # | Return | Notes |
|---|--------|-------|
| 1 | `itemName` | |
| 2 | `itemLink` | basic `item:ID` hyperlink (colored, bracketed name) |
| 3 | `itemQuality` | 0–5 |
| 4 | `itemLevel` | |
| 5 | `itemMinLevel` | required level |
| 6 | `itemType` | localized class name (e.g. "Weapon") |
| 7 | `itemSubType` | localized subclass name (e.g. "Sword") |
| 8 | `itemStackCount` | max stack size |
| 9 | `itemEquipLoc` | `INVTYPE_*` token, `""` for non-equippable |
| 10 | `itemTexture` | icon **path** (1.12 has no fileID system) |
| 11 | `sellPrice` | vendor sell price in copper |
| 12 | `classID` | |
| 13 | `subclassID` | |
| 14 | `bindType` | 0 none / 1 BoP / 2 BoE / 3 BoU / 4 quest |
| 15 | `expansionID` | always `0` (classic) |
| 16 | `setID` | item-set ID, `nil` if none |
| 17 | `isCraftingReagent` | always `false` (no such flag in 1.12 data) |
| 18 | `itemDescription` | item flavor/description text (`""` if none) |

**Asynchronous on a cache miss:** if the item isn't cached yet it returns
nothing (nil) and warms the cache; the value lands on a retry after
`GET_ITEM_INFO_RECEIVED` fires — same contract as the stock `GetItemInfo`
and modern clients. Fields 4–18 are the ones the vanilla global never
returned.

```lua
local name, link, quality, ilvl, minLevel, itype, isub, stack, equipLoc,
      tex, sell, classID, subID, bind, expac, setID, reagent, desc
    = C_Item.GetItemInfo(6948)   -- Hearthstone
```

### `C_Item.GetItemInfoInstant(item)`

Modern-style accessor for the always-available subset of item info — the
fields that depend only on classification, not on player-specific state.
Synchronous, side-effect-free: peeks the client-side item cache and
returns whatever it has without warming or queueing.

Accepts a numeric `itemID` or a string containing `"item:NNN"` (matches both
the bare `"item:1234"` shorthand and full chat links like
`"|cff...|Hitem:1234:...|h[Name]|h|r"`). Item names are not accepted —
vanilla itself has no name → ID resolver, and it's rarely the form addon code
actually has on hand.

Returns seven values:

```
itemID, itemType, itemSubType, itemEquipLoc, icon, classID, subClassID
```

The `itemID` return is always populated for any input that resolves to
a positive integer (so it's safe to use the function as an ID extractor
on a link without first warming the cache). The remaining six fields
come from the cache record when present — when the item isn't cached
yet they're all `nil`, but the call still returns a 7-tuple so the
positional shape is preserved.

- `itemType` / `itemSubType` are the localized class / subclass names
  (e.g. `"Weapon"` / `"One-Handed Swords"`), read from `ItemClass.dbc` and
  `ItemSubClass.dbc`.
- `itemEquipLoc` is the `"INVTYPE_*"` constant (e.g. `"INVTYPE_HEAD"`), or
  `""` for non-equippable items.
- `icon` is a path string (`"Interface\\Icons\\..."`), matching what the
  rest of the 1.12 API returns. Modern WoW returns a numeric fileID here,
  but 1.12 has no fileID system, so a path is the only meaningful value.
- `classID` / `subClassID` are the raw enum integers (e.g. `2`, `7` for
  one-handed swords).

```lua
local id, type, subtype, equipLoc, icon, classID, subClassID
    = C_Item.GetItemInfoInstant(6948)  -- Hearthstone, cached
-- type="Miscellaneous", subtype="Junk", equipLoc="",
-- icon="Interface\\Icons\\INV_Misc_Rune_01", classID=15, subClassID=0

local id = C_Item.GetItemInfoInstant("|cff...|Hitem:6948:0:0:0|h[Hearthstone]|h|r")
-- id == 6948 even when the cache is cold — the call parses the link
-- to extract the ID without consulting the cache.
```

The actual class/subclass values reflect 1.12.1's data, which differs from
modern WoW. For example, vanilla had no Cloth subclass under Trade Goods —
Silk Cloth lives at `(7, 0)` in this client, not the modern `(7, 5)`.

> **No auto-warmup.** Unlike `GetItemInfo` or `C_Item.GetItemNameByID`,
> `GetItemInfoInstant` does not trigger a network query on a cache
> miss. The "Instant" name is contractual — callers that need the
> cache populated should use
> [`C_Item.RequestLoadItemDataByID`](#c_itemrequestloaditemdatabyiditem--c_itemrequestloaditemdataitemlocation)
> explicitly, or just call `GetItemInfo` (which warms via the
> `Script_GetItemInfo` hook).

### `C_Item.GetItemInventorySlotInfo(inventorySlot)`

Returns the localized display name for an `Enum.InventoryType` value — e.g.
`C_Item.GetItemInventorySlotInfo(1)` → `"Head"`. Returns `nil` for the
non-equip slot (`0`) or out-of-range values.

The engine's INVTYPE table stores the equipLoc *key* (`"INVTYPE_HEAD"`); the
display name is the FrameXML global string of that key (`INVTYPE_HEAD =
"Head"`), so the result is correctly localized on non-English clients.

### `C_Item.GetItemInventorySlotKey(inventorySlot)`

Returns the equipLoc key string for an `Enum.InventoryType` value — e.g.
`C_Item.GetItemInventorySlotKey(1)` → `"INVTYPE_HEAD"`. This is the same
`INVTYPE_*` token [`C_Item.GetItemInfoInstant`](#c_itemgetiteminfoinstantitem)
returns as its `itemEquipLoc`. Returns `nil` for the non-equip slot (`0`) or
out-of-range values. Pair it with `GetItemInventorySlotInfo` to turn the key
into a localized label.

### `C_Item.GetItemInventoryType(itemLocation)` / `C_Item.GetItemInventoryTypeByID(item)`

Returns the numeric `Enum.InventoryType` straight off the cache
record's `m_inventoryType` field (`+0x2C`) — the integer sibling of
`GetItemInfoInstant`'s 4th return (which gives the `INVTYPE_*` string).

| Value | Constant              | Slot                        |
|------:|-----------------------|-----------------------------|
| 0     | `INVTYPE_NON_EQUIP_IGNORE` | non-equippable        |
| 1     | `INVTYPE_HEAD`        | head                         |
| 2     | `INVTYPE_NECK`        | neck                         |
| …     | …                     | …                            |
| 20    | `INVTYPE_ROBE`        | chest (full-body robes)      |
| 26    | `INVTYPE_RANGEDRIGHT` | ranged                       |
| 27    | `INVTYPE_QUIVER`      | quiver/ammo pouch            |

Vanilla items only produce values `0..28`; the higher modern
constants (`INVTYPE_PROFESSION_*`, `INVTYPE_EQUIPABLESPELL_*`, etc.)
were introduced post-vanilla and are never returned. Modern
backport code that compares against the higher enum values still
resolves correctly because vanilla items just don't carry those
types.

```lua
local t = C_Item.GetItemInventoryTypeByID(19019)  -- Thunderfury: 17 = INVTYPE_2HWEAPON
if t == 1 then -- head
    ...
end
```

### `C_Item.GetItemLink(itemLocation)`

Returns the fully-decorated per-instance hyperlink for the item at
the location — same string `GetContainerItemLink(bag, slot)` or
`GetInventoryItemLink("player", slot)` would return for the same
slot. Enchant ID, random-suffix, and any other per-instance data
attached to the CGItem are preserved.

```lua
local link = C_Item.GetItemLink({bagID = 0, slotIndex = 1})
-- "|cffa335ee|Hitem:16539:911:::::::70::::::::::|h[General's Silk Boots]|h|r"
--                          ^^^ — enchant ID (Speed +15%) preserved
```

Implemented by reading the location table's fields and tail-calling
the engine's link script function (`Script_GetContainerItemLink` at
`0x004F9930` for bag locations, `Script_GetInventoryItemLink` at
`0x004C8C10` for equipment slots). The link string is built by the
engine off the live CGItem, so it always matches what other
addons see via the older positional-arg APIs.

### `C_Item.GetItemLocation(itemGUID)`

Reverse lookup from a GUID string (the format `C_Item.GetItemGUID`
returns) to an `itemLocation` table identifying the slot currently
holding that item. Returns `nil` if the GUID isn't resident in the
walked scope.

```lua
local guid = C_Item.GetItemGUID({equipmentSlotIndex = INVSLOT_HEAD})
-- player moves item to bag, drops it, etc. — guid stays valid across
-- bag/equipment rearrangements until the item leaves the session
local loc = C_Item.GetItemLocation(guid)
-- loc might now be { bagID = 1, slotIndex = 4 }, or nil if sold
```

Modern WoW returns an `ItemLocation` mixin object; we return a plain
table with the same field shape every other `C_Item.*` API in
ClassicAPI accepts as input (`{equipmentSlotIndex=N}` or
`{bagID=B, slotIndex=S}`), so the result pipes straight into
`C_Item.GetItemQuality(loc)`, `C_Item.GetItemLink(loc)`, etc.

**Walked scope.** Character-pane equipment (slots 1..19) + backpack
(bagID 0) + the four equipped bags (bagIDs 1..4). Bank and keyring
are NOT walked — same scope as the rest of the `C_Container.*`
family. Worst case is ~80 slot lookups; fine for sporadic use, not
appropriate for per-frame polling.

**Implementation.** We don't go through the engine's typed
object-by-GUID helper at `0x00468460` (the one used for the auction-
sell slot) because that path asserts on bad input — stale GUIDs
from addon-side caching would crash the client. Instead we walk
equipment + bag slots, read each CGItem's GUID, and compare. Misses
return nil; hits never need a fallback.

### `C_Item.GetItemMaxStackSize(itemLocation)` / `C_Item.GetItemMaxStackSizeByID(item)`

Returns the item type's max stack size — what you'd find as the 7th
return of `GetItemInfo(item)`. `1` for non-stackable items; `5`, `20`,
`200`, etc. for stackables. Different from `C_Item.GetStackCount` /
the engine's `GetContainerItemInfo`, which return the **current**
count in a specific slot.

```lua
local cap = C_Item.GetItemMaxStackSizeByID(2589)  -- Linen Cloth → 20
```

Single `uint32` read at cache record `+0x60` (`m_stackable`). By-ID
form fires a background cache fill on miss and returns nil; re-call
after `GET_ITEM_INFO_RECEIVED`.

### `C_Item.GetItemName(itemLocation)` / `C_Item.GetItemNameByID(item)`

Returns the item's display name as a string, or `nil` for empty /
uncached / invalid inputs.

The **location** form points at a live item instance, so it returns the
*decorated* name — random suffix included (`"Iridium Chain of the Owl"`,
not the base `"Iridium Chain"`) — built off the `CGItem` via the engine's
own name builder, matching modern WoW and the bracketed name in
[`C_Item.GetItemLink`](#c_itemgetitemlinkitemlocation). (It falls back to
the base `ItemStats_C.m_name[0]` name if the instance build yields
nothing.)

The **ByID** form has no instance — an itemID can't carry a random suffix
— so it returns the base `m_name[0]` name only. Cache miss on the `ByID`
form returns `nil` and fires the cache fill so the next call (after
`GET_ITEM_INFO_RECEIVED`) succeeds.

```lua
-- location form → decorated name (random suffix included)
local name = C_Item.GetItemName({equipmentSlotIndex = 2})  -- "Iridium Chain of the Owl"
-- ByID form → base name only (no instance → no suffix)
local name = C_Item.GetItemNameByID(6948)                  -- "Hearthstone"
```

### `C_Item.GetItemQuality(itemLocation)` / `C_Item.GetItemQualityByID(item)`

Returns the item's quality as an integer (0=Poor, 1=Common, 2=Uncommon,
3=Rare, 4=Epic, 5=Legendary), matching the `LE_ITEM_QUALITY_*`
constants. Single `uint32` read at cache record `+0x1C`.

```lua
if C_Item.GetItemQualityByID(itemID) >= LE_ITEM_QUALITY_RARE then
    -- highlight rare-or-better drop
end
```

### `C_Item.GetItemSellPrice(itemLocation)` / `C_Item.GetItemSellPriceByID(item)`

Returns the vendor sell price in copper, **per unit** (multiply by
stack count for the per-stack value). Matches the 11th return of
modern WoW's `GetItemInfo`. Returns `nil` on cache miss / invalid
input. Cache miss fires a background fill so a follow-up call after
`GET_ITEM_INFO_RECEIVED` returns the value.

```lua
local unit = C_Item.GetItemSellPriceByID(2589)   -- Linen Cloth → 25 (copper)
local stack = unit * C_Item.GetItemMaxStackSizeByID(2589)
```

Single `uint32` read at cache record `+0x28` (`m_sellPrice`). Vanilla
1.12 doesn't surface this in tooltips — the field is populated on
every sellable item but the engine's tooltip code never reads it —
so this function exposes data that's been sitting in the cache
unused.

### `C_Item.GetItemSetID(itemLocation)` / `C_Item.GetItemSetIDByID(item)`

Returns the `ItemSet.dbc` ID for the item if it's part of an
equipment set (e.g., `181` for any Magister's Regalia piece), or
`nil` if the item isn't part of any set / the item isn't cached /
the slot is empty.

```lua
local setID = C_Item.GetItemSetIDByID(16682)   -- Magister's Regalia: 181
local setID = C_Item.GetItemSetIDByID(6948)    -- Hearthstone: nil

-- by itemLocation (worn or in-bag)
local chestSet = C_Item.GetItemSetID({ equipmentSlotIndex = 5 })
```

This is the 16th return of modern WoW's `GetItemInfo` (the `setID`
field). Single `uint32` read at cache record `+0x1C0`
(`m_itemSet`). Same cache-warm pattern as the other `*ByID` getters
— cache misses return `nil` and fire a background load; call
`C_Item.RequestLoadItemDataByID(itemID)` if you need synchronous-
after-event resolution.

### `C_Item.GetItemSetInfo(setID)`

Returns a table describing the item set, or `nil` if `setID`
doesn't resolve to an `ItemSet.dbc` row.

```lua
local info = C_Item.GetItemSetInfo(181)
-- {
--   setID = 181,
--   name = "Magister's Regalia",
--   requiredSkill = 0,
--   requiredSkillRank = 0,
--   items = { 16685, 16683, 16686, 16684, 16687, 16689, 16688, 16682 },
--   bonuses = {
--     { spellID = 29091, threshold = 2 },   -- 2-piece bonus
--     { spellID = 27867, threshold = 6 },   -- 6-piece bonus
--     { spellID = 18679, threshold = 8 },   -- 8-piece bonus
--     { spellID = 30777, threshold = 4 },   -- 4-piece bonus
--   },
-- }
```

| Field | Type | Notes |
|-------|------|-------|
| `setID` | number | Echo of the input. |
| `name` | string | Localized set name. |
| `requiredSkill` | number | Skill line ID required to use the set; `0` if none. |
| `requiredSkillRank` | number | Required rank in that skill line; `0` if none. |
| `items` | array | itemIDs in the set. Empty slots filtered, so `#info.items` is the real item count. Order is the engine's own (matches the set's order in the DBC; not necessarily slot-sorted). |
| `bonuses` | array | `{ spellID, threshold }` tables for each non-empty set-bonus slot. `threshold` is the number of equipped set pieces required to grant `spellID`. Order is the DBC's; bonuses with the same threshold are not normalized. |

Reads `ItemSet.dbc` directly — no cache warm-up needed (DBCs load at
boot). Returns `nil` for `setID == 0`, out-of-range IDs, or rows
that don't exist.

### `C_Item.GetItemSpell(item)`

Returns `(spellName, spellID)` for the on-use spell attached to an
item (potions, trinkets, scrolls, hearthstone, food/drink, etc.),
or `nil` for items without one (vendor trash, regular gear, weapons
with passive procs).

`item` accepts the same input shapes as `GetItemInfo` — a numeric
itemID, a chat-link `"|cffffffff|Hitem:6948:0:0:0|h[...]|h|r"`
fragment, or a `"item:NNN"` short form.

```lua
C_Item.GetItemSpell(6948)
-- "Hearthstone", 8690

C_Item.GetItemSpell(13442)
-- "Mighty Rage Potion", 17528

C_Item.GetItemSpell(11288)  -- a soulstone (trigger=4, not ON_USE)
-- nil

C_Item.GetItemSpell(2589)   -- Linen Cloth, no spell
-- nil
```

Returns the **ON_USE** spell only. Vanilla items can carry up to 5
spell entries in their `ItemStats_C` record, each with its own
trigger code:

| Trigger | Meaning | Surfaced by `GetItemSpell`? |
|---|---|---|
| 0 | `ON_USE` | **yes** |
| 1 | `ON_EQUIP` (passive aura on gear) | no |
| 2 | `CHANCE_ON_HIT` (weapon procs) | no |
| 4 | `SOULSTONE` (on-death) | no |
| 5 | `ON_USE_NO_DELAY` | no (TODO: should we add this?) |
| 6 | `LEARN_SPELL` (recipes) | no |

This matches modern WoW's `GetItemSpell`, which only reports on-use
triggers. Addons that need the other trigger types (proc auras,
recipe targets) should reach into the cache directly — the spell
slots are at `ItemStats +0x11C` (5 spell IDs) and `+0x130` (5 trigger
codes).

**Auto-warmup on cache miss.** Items not yet in the local cache
return `nil` and silently kick off an `SMSG_ITEM_QUERY_SINGLE`
request. A second call after `GET_ITEM_INFO_RECEIVED` lands the
data. Same warmup pattern as `C_Item.GetItemFamily` and the rest of
our cache-backed accessors.

### `C_Item.GetItemStatDelta(itemLink1, itemLink2)`

Returns a table of the per-stat **difference** between two items,
computed as `item2 - item1` — a positive value means the second item has
more of that stat. Only stats whose delta is non-zero appear.

```lua
-- Compare an equipped ring against one in your bags:
local delta = C_Item.GetItemStatDelta(
    C_Item.GetItemLink({equipmentSlotIndex = 11}),
    C_Item.GetItemLink({bagID = 0, slotIndex = 3}))
-- delta = { ITEM_MOD_INTELLECT_SHORT = 6, ITEM_MOD_SPIRIT_SHORT = 2 }
-- (the bag ring has +6 Int / +2 Spirit relative to the equipped one)
```

Same key set, accepted input forms, random-suffix handling, and caching
caveat as [`C_Item.GetItemStats`](#c_itemgetitemstatsitemlink) — see
there for the details. Returns `nil` if **either** item is uncached.

### `C_Item.GetItemStats(itemLink)`

Returns a table of an item's stats, keyed by the FrameXML global-string
names WoW's own `GetItemStats` uses (so `_G[key]` yields the display
label — `_G["ITEM_MOD_STRENGTH_SHORT"]` → `"Strength"`). Only stats the
item actually carries are present.

```lua
C_Item.GetItemStats("item:2244")            -- Krol Blade
-- { ITEM_MOD_STRENGTH_SHORT = 7, ITEM_MOD_STAMINA_SHORT = 5,
--   ITEM_MOD_DAMAGE_PER_SECOND_SHORT = 40.89,
--   ITEM_MOD_CRIT_MELEE_RATING = 1, ITEM_MOD_CRIT_RANGED_RATING = 1 }

C_Item.GetItemStats("item:12022:0:769")     -- "of the Owl" random suffix
-- { ITEM_MOD_INTELLECT_SHORT = 6, ITEM_MOD_SPIRIT_SHORT = 6 }
```

**Sources.** The table is assembled from three places, so it reflects
what the item actually grants — not just its stored stat slots:

1. **Base record** — the stored attributes, armor, resistances, and (for
   weapons) DPS.
2. **On-equip spells** (`SpellTrigger == ON_EQUIP`) — vanilla stores the
   "special" bonuses (crit, attack power, spell power, hit, mp5, defense,
   …) as an equip spell whose aura effect carries the value, not as a
   stat slot. Their auras are decoded into the keys below.
3. **Random suffix** — the link's third `item:id:enchant:SUFFIX:unique`
   field (`ItemRandomProperties.dbc` → `SpellItemEnchantment.dbc`): stat
   suffixes ("of the Owl") are themselves equip spells, armor suffixes
   ("of Toughness") are direct resistance enchants. A bare itemID or a
   suffix-less link contributes nothing here.

**Keys.**

| Key | Stat | Source |
|-----|------|--------|
| `ITEM_MOD_STRENGTH_SHORT` / `_AGILITY_` / `_STAMINA_` / `_INTELLECT_` / `_SPIRIT_SHORT` | Base attributes | record / `MOD_STAT` |
| `ITEM_MOD_MANA_SHORT` / `ITEM_MOD_HEALTH_SHORT` | Mana / health on equip | record / `MOD_INCREASE_ENERGY` (35) / `MOD_INCREASE_HEALTH` (34) |
| `RESISTANCE0_NAME` | Armor | record / `MOD_RESISTANCE` bit 0 |
| `RESISTANCE1_NAME` … `RESISTANCE6_NAME` | Holy / Fire / Nature / Frost / Shadow / Arcane resistance | record / `MOD_RESISTANCE` / type-4 enchant |
| `ITEM_MOD_DAMAGE_PER_SECOND_SHORT` | Weapon DPS (float) | avg damage ÷ swing time |
| `ITEM_MOD_ATTACK_POWER_SHORT` | Attack power | `MOD_ATTACK_POWER` (99) |
| `ITEM_MOD_RANGED_ATTACK_POWER_SHORT` | Ranged attack power (ranged-only items) | `MOD_RANGED_ATTACK_POWER` (124) |
| `ITEM_MOD_CRIT_MELEE_RATING` + `ITEM_MOD_CRIT_RANGED_RATING` | Crit % | weapon-crit aura (52) |
| `ITEM_MOD_HIT_MELEE_RATING` + `ITEM_MOD_HIT_RANGED_RATING` | Hit % | hit aura (54) |
| `ITEM_MOD_HIT_SPELL_RATING` | Spell hit % | spell-hit aura (55) |
| `ITEM_MOD_CRIT_SPELL_RATING` | Spell crit % | spell-crit aura (57 / 71) |
| `ITEM_MOD_SPELL_DAMAGE_DONE_SHORT` | Spell damage | `MOD_DAMAGE_DONE` (13) |
| `ITEM_MOD_SPELL_HEALING_DONE_SHORT` | Healing | `MOD_HEALING_DONE` (135) |
| `ITEM_MOD_MANA_REGENERATION` | mp5 (mana) | `MOD_POWER_REGEN` (85) |
| `ITEM_MOD_DEFENSE_SKILL_RATING` | Defense skill | `MOD_SKILL` (30, misc 95) |
| `ITEM_MOD_DODGE_RATING` | Dodge % | `MOD_DODGE_PERCENT` (49) |
| `ITEM_MOD_PARRY_RATING` | Parry % | `MOD_PARRY_PERCENT` (47) |
| `ITEM_MOD_BLOCK_RATING` | Block chance % | `MOD_BLOCK_PERCENT` (51) |
| `ITEM_MOD_BLOCK_VALUE` | Shield block value | record (`m_block`) |

> **Values are vanilla-native, not ratings.** Vanilla has no rating
> system, so the percent-based stats (crit / hit / defense) report the raw
> **percentage** under the modern `*_RATING` key — e.g. Krol Blade's
> "+1% crit" is `ITEM_MOD_CRIT_MELEE_RATING = 1`, where TBC/Era would
> show the level-scaled rating (`13`). A native percent is the only
> value that's honest and level-independent. Flat stats (attack power,
> spell damage, healing, mp5) are the item's actual magnitudes.
>
> Rating stats that didn't exist in 1.12 at all (haste, mastery,
> versatility, expertise, resilience) never appear. A generic "+N attack
> power" item carries both a melee and a ranged AP aura in vanilla; the
> generic `ITEM_MOD_ATTACK_POWER_SHORT` key subsumes it, so
> `ITEM_MOD_RANGED_ATTACK_POWER_SHORT` shows only for ranged-only items
> (scopes, ranged weapons). Rarer auras with no clean single key —
> percent attack power, per-weapon-line skill, damage-taken, on-hit /
> on-use procs — are skipped.

**Input.** Accepts a full chat hyperlink, an `item:N…` link string, or a
bare itemID (a superset of retail, which is link-only). Returns `nil` if
the item isn't cached yet — warm it via `GetItemInfo` and retry.

### `GetItemClassInfo(classID)`

Returns `className` — the localized `ItemClass.dbc` name for an item
class ID (the `classID` from `Enum.ItemClass` / the 12th `GetItemInfo`
return). Also available as `C_Item.GetItemClassInfo(classID)`.

```lua
GetItemClassInfo(2)   -- "Weapon"
GetItemClassInfo(4)   -- "Armor"
GetItemClassInfo(1)   -- "Container"
```

Returns `nil` for an unknown class. Obsolete slots return the client's
literal string (`GetItemClassInfo(3)` → `"Jewelry(OBSOLETE)"`), which is
the real vanilla data — use [`Enum.ItemClass`](#enumitemclass) for the
modern key names.

### `GetItemSubClassInfo(classID, subClassID)` / `C_Item.GetItemSubClassInfo(classID, subClassID)`

Returns `subClassName, subClassUsesInvType` for an item class/subclass pair
(the global form documents the second value as `isArmorType` — same value).

```lua
GetItemSubClassInfo(2, 7)   -- "One-Handed Swords", false
GetItemSubClassInfo(4, 1)   -- "Cloth", true
```

- `subClassName` (string) — the localized `ItemSubClass.dbc` name (verbose
  form, e.g. `"One-Handed Swords"`; falls back to the short form for
  subclasses that only populate it, e.g. `"Consumable"`).
- `subClassUsesInvType` (boolean) — true for subclasses whose items are
  labeled by inventory slot rather than by subclass name. On vanilla data
  that's exactly the armor material types (Miscellaneous, Cloth, Leather,
  Mail, Plate); false for weapons, shields, librams, idols, totems, and
  non-equipment. Read from the `ItemSubClass.dbc` flags bit `0x200`
  (verified against the 1.15 client).

Returns `nil` if the `(classID, subClassID)` pair has no row.

### `C_Item.GetItemUniqueness(itemLocation)` / `C_Item.GetItemUniquenessByID(item)`

The two functions have **different signatures** in modern (Classic
Era 1.15.x) WoW — we mirror them exactly:

`C_Item.GetItemUniqueness(itemLocation)` returns
`(limitCategory, limitMax)`:

| Field | Vanilla source |
|-------|----------------|
| `limitCategory` | Always `0` — vanilla has no `LimitCategory.dbc` (TBC addition). |
| `limitMax` | `ItemStats_C.m_maxCount` — `0` = unlimited, `1` = "Unique", higher = inventory cap. |

`C_Item.GetItemUniquenessByID(itemID)` returns
`(isUnique, limitCategoryName, limitCategoryCount, limitCategoryID)`:

| Field | Vanilla source |
|-------|----------------|
| `isUnique` | `m_maxCount > 0` — true for any unique-tagged item. |
| `limitCategoryName` | Always `nil` — no categories in vanilla. |
| `limitCategoryCount` | `m_maxCount` when `isUnique`, else `nil`. |
| `limitCategoryID` | Always `nil`. |

Both functions return nothing (zero Lua return values, matching
modern's `MayReturnNothing` annotation) if the item record isn't
cached. The by-ID variant fires a background cache fill on miss;
re-call after `GET_ITEM_INFO_RECEIVED`.

```lua
local _, max = C_Item.GetItemUniqueness({equipmentSlotIndex = 13})
-- max = 1 for typical trinkets (Unique)
local isUnique, _, count = C_Item.GetItemUniquenessByID(81013)
-- isUnique = true, count = 10  (Southern Sand Crawler Leg, "Unique (10)")
```

Modern WoW populates the category half for items like Brewfest Mug
or Heart of Azeroth ("Unique-Equipped: Eye of Azshara"). Vanilla
has no such items, so the category fields stay nil/0.

### `C_Item.GetStackCount(itemLocation)`

Returns the **current** stack count in a specific slot — distinct
from `C_Item.GetItemCount(item)`, which sums every stack of that
itemID across the player's inventory. Useful when an addon needs to
know "this specific stack has 13/20", not "I have 47 total".

```lua
local n = C_Item.GetStackCount({bagID = 0, slotIndex = 1})
-- 13 (the bag's first slot has 13 of whatever item)
```

Reads `ITEM_FIELD_STACK_COUNT` directly off the item's
`m_objectFields` — same field `GetContainerItemInfo` returns as
`itemCount`. Returns `0` for empty / unresolvable locations.

### `C_Item.IsBound(itemLocation)`

Returns `true` if the item at the given location is soulbound, `false` otherwise
(including when the slot is empty or the location is malformed). The 1.12
client tracks the soulbound bit on each item instance directly; previously
the only way to read it from Lua was a scan-tooltip hack
(`SetBagItem` + string-compare against the localized `ITEM_SOULBOUND`
constant) — slow, locale-fragile, and one of the hottest paths during bag
updates.

```lua
if C_Item.IsBound({equipmentSlotIndex = INVSLOT_HEAD}) then ... end
if C_Item.IsBound({bagID = 0, slotIndex = 1}) then ... end
if C_Item.IsBound(itemGUID) then ... end
```

### `IsConsumableItem(item)` / `C_Item.IsConsumableItem(item)`

Returns `true` if the item is a consumable, `false` otherwise. An item
counts as consumable when its **class is `Consumable` (0)** *or* its
**inventory type is `INVTYPE_AMMO` (24) or `INVTYPE_THROWN` (25)** — ammo
and thrown weapons being consumed in use. Registered as both the bare
global and the namespaced `C_Item` form, like `IsUsableItem`.

`item` is an itemID number or `"item:N..."` link. Item names aren't
accepted (vanilla has no name→ID resolver). Returns `false` for uncached
items (no async load fired) — same cache contract as
`C_Item.IsEquippableItem`.

```lua
IsConsumableItem(118)              -- Minor Healing Potion → true  (class Consumable)
C_Item.IsConsumableItem(1251)     -- Linen Bandage → true          (class Consumable)
C_Item.IsConsumableItem(2512)     -- Rough Arrow → true            (INVTYPE_AMMO)
C_Item.IsConsumableItem(6948)     -- Hearthstone → false
C_Item.IsConsumableItem(18820)    -- Talisman of Ephemeral Power → false (on-use trinket)
```

This is a class/ammo check, **not** a "has a usable effect" check.
3.3.5's `IsConsumableItem` walked the item's on-use spells and returned
true for any with a real effect (which would include on-use trinkets),
but that heuristic didn't survive into the modern client: verified in-game
that an on-use trinket with a numeric `Use:` effect (Talisman of Ephemeral
Power) returns `false`, while class-`Consumable` items (potions, bandages,
the class-0 "Faintly Glowing Skull") and ammo return `true`. We match the
modern contract.

### `C_Item.IsEquippableItem(item)`

Returns `true` if `item` can be equipped in any character-pane slot,
`false` otherwise. Reads `m_inventoryType` from the cached ItemStats
record — INVTYPE_NON_EQUIP (value `0`) is the only "not equippable"
value, so any non-zero inventory type passes (head, neck, weapon,
shield, holdable, …).

`item` is an itemID number or `"item:N..."` link. Item names aren't
accepted — vanilla has no name→ID resolver, and equippability is an
itemID-keyed property anyway.

Returns `false` for uncached items (no async load fired). If you
need it to wait for the cache, call `C_Item.RequestLoadItemDataByID`
first and re-check on `ITEM_DATA_LOAD_RESULT`.

```lua
C_Item.IsEquippableItem(12640)  -- Lionheart Helm → true
C_Item.IsEquippableItem(6948)   -- Hearthstone → false
```

### `IsUsableItem(item)` / `C_Item.IsUsableItem(item)`

Returns `(usable, noMana)` for an item's **on-use** ability — the item
analog of [`IsUsableSpell`](#isusablespellspell--isusablespellslot-booktype).
This asks "can I click this item right now?", **not** "can I equip it".
Armor proficiency, class-restricted gear, and the like are *not* this
function's concern (that's the tooltip's red requirement lines); only an
item's usable/right-click effect matters here.

The global `IsUsableItem(item)` returns `1`/`nil` pairs (matching stock
`IsUsableAction`); `C_Item.IsUsableItem(item)` returns proper booleans
per the `C_Item.*` convention. `item` is an itemID number or
`"item:N..."` link.

```lua
IsUsableItem(6948)                     -- Hearthstone → 1, nil
local usable, noMana = C_Item.IsUsableItem(13446)  -- Major Healing Potion
-- usable=true,  noMana=false → click it
-- usable=false, noMana=false → no on-use effect, below required level,
--                              class/race-restricted, dead, on a form
--                              that blocks it, etc.
```

> **What this function checks:**
>
> 1. The item has an **on-use** spell (an `ItemStats` spell slot with
>    trigger `ON_USE`). Items with no usable effect — plain gear,
>    materials, on-equip procs — return `(nil, nil)`.
> 2. The item's **use requirements** are met: RequiredLevel, and the
>    AllowableClass / AllowableRace masks. (These gate *activation* —
>    a too-low-level potion, a class-specific trinket — so they belong
>    here, unlike equip-only requirements.)
> 3. The on-use spell is castable now — delegated to the engine's own
>    spell-castability helper (`FUN_SPELL_IS_USABLE`, `0x006E3D60`),
>    which folds in shapeshift/form, mechanic immunities, aura state,
>    and the power check. Insufficient power is the *only* condition
>    that sets `noMana=true`.
>
> Like `C_Item.IsEquippableItem`, this is a synchronous cache peek:
> uncached items return `(nil/false, nil/false)` with no async load
> fired. Call `C_Item.RequestLoadItemDataByID` first and re-check on
> `ITEM_DATA_LOAD_RESULT` if you need it to wait.
>
> The on-use spell lookup is shared with
> [`C_Item.GetItemSpell`](#c_itemgetitemspellitem)
> (`Item::Spell::FindOnUseSpellIDInRecord`).

### `C_Item.IsEquippedItem(item)`

Returns `true` if `item` is currently equipped in any of the 19
character-pane slots, `false` otherwise. Walks slots 1..19 in order and
short-circuits on the first match.

`item` accepts the same shapes as `GetItemInfo`:

| Form | Example | Match strategy |
|------|---------|----------------|
| itemID | `2589` | exact `itemID` equality |
| bare link | `"item:2589:0:0:0"` | parses the first numeric field |
| chat link | `\124cffffffff\124Hitem:2589:0:0:0\124h[Linen Cloth]\124h\124r` | extracts itemID after `\124Hitem:` |
| name | `"Linen Cloth"` | case-insensitive match against each equipped item's *decorated* name (random suffix included) |

Returns `false` (no Lua error) for invalid input — `nil`, empty string,
or a string that doesn't parse as any of the above.

Name matching is against the item's **decorated** name: a random-suffix
item matches only its full name (`"Krol Blade of the Bear"`), not the
base (`"Krol Blade"`) — the same behavior as modern WoW (verified
in-game). Unsuffixed items match their plain name. The candidate must be
in the client item cache; equipped items always are, so that's a non-issue
in practice. This match logic is shared with the by-name action APIs
(`C_Item.UseItemByName` / `EquipItemByName`) — one predicate, one set of
semantics.

```lua
if C_Item.IsEquippedItem(2589) then
    -- Linen Cloth is equipped (silly example — pick a wearable item)
end

-- From a chat link click:
if C_Item.IsEquippedItem(itemLink) then ...

-- By localized name:
if C_Item.IsEquippedItem("Thunderfury, Blessed Blade of the Windseeker") then ...
```

### `C_Item.IsItemDataCachedByID(item)` / `C_Item.IsItemDataCached(itemLocation)`

Returns `true` if the item's static data is currently in the client-side
item cache, `false` otherwise. The "ByID" variant takes an itemID or
"item:NNN"-style string; the location variant takes the modern
`{equipmentSlotIndex=}` / `{bagID=, slotIndex=}` table.

These read the cache without firing a server query — pair with
`RequestLoadItemData(ByID)` if you need to ensure the data is loaded
before checking.

```lua
if not C_Item.IsItemDataCachedByID(itemID) then
    C_Item.RequestLoadItemDataByID(itemID)
    -- (poll IsItemDataCachedByID on a timer until true)
end
```

### `C_Item.IsItemGUIDInInventory(itemGUID)`

Returns whether the item with the given `"0x…"` GUID is currently carried
by the player — the 19 equipment slots plus the bags (backpack + the four
equipped bags). Equipped items count (verified against retail); the bank
does not.

```lua
local g = C_Item.GetItemGUID({bagID = 0, slotIndex = 1})
C_Item.IsItemGUIDInInventory(g)   -- true
C_Item.IsItemGUIDInInventory(
    C_Item.GetItemGUID({equipmentSlotIndex = 1}))   -- true (equipped counts)
```

Accepts the GUID string [`C_Item.GetItemGUID`](#c_itemgetitemguiditemlocation)
/ `UnitGUID` return; returns `false` for a malformed or zero GUID. To also
cover the bank, use `C_Item.GetItemCount(itemID, true) > 0` instead.

### `C_Item.IsItemInRange(item, targetUnit)`

Returns `true` if the item is in range of `targetUnit`, `false` if out
of range, and `nil` when the range check doesn't apply — the item has no
on-use spell, its on-use spell is rangeless, or the item / unit can't be
resolved. Matches retail, which likewise returns `nil` for items with no
range restriction.

`item` is an itemID, `"item:NNN"` string, or item link (item *names*
aren't resolvable — vanilla has no name→ID map — and return `nil`).
`targetUnit` is a unit token.

```lua
-- a targeted on-use item (net / bomb / thrown / targeted quest item):
C_Item.IsItemInRange(itemID, "target")   -- true / false
-- rangeless / self-use item:
C_Item.IsItemInRange(118, "target")       -- Minor Healing Potion → nil
```

An item's range comes from the spell it fires on use, so this resolves
the item to its on-use spell ([`C_Item.GetItemSpell`](#c_itemgetitemspellitem)'s
spell) and runs the exact same range test as
[`C_Spell.IsSpellInRange`](#c_spellisspellinrangespellidentifier-targetunit)
— the two agree for the same underlying spell. Range-only like the spell
version: it ignores line of sight and doesn't reject wrong-faction
targets. Passive reader — an item not yet in the client item cache
returns `nil` with no background fetch (items you'd range-check are
normally in bags / on the action bar and already cached). Absent tokens
(e.g. `"target"` with no target) return `nil`; a genuinely unrecognized
token string raises a Lua error, same contract as `C_Spell.IsSpellInRange`.

### `C_Item.IsItemOpenable(itemLocation)`

Returns two values: `(isOpenable, canOpen)`.

- **`isOpenable`** — `true` if the item type is right-click-openable
  (sack, clam, simple chest, quest box, lockbox, etc. — anything
  whose tooltip *could* show `<Right Click to Open>`). Intrinsic to
  the item type. From `ItemStats.Flags & 0x4`.
- **`canOpen`** — `true` if the player can right-click *this specific
  instance* and trigger the open action right now. For unlocked-by-
  default items (clams, sacks) this matches `isOpenable`. For items
  with a lock (lockboxes), it's only true after the lock has been
  removed (rogue Pick Lock, key item, etc.) — so a priest looking at
  a fresh lockbox sees `(true, false)`, while a rogue who just
  picked it sees `(true, true)`.

Both values are `nil` when the item data isn't cached yet (background
`SMSG_ITEM_QUERY_SINGLE` fired) or when the location is empty /
invalid. Lets callers distinguish "data unknown" from "definitely
not openable".

There's no `ByID` variant — the `canOpen` check depends on a specific
instance's `ITEM_FIELD_FLAGS`, which you can only have via an
itemLocation. If you only have an itemID, use
`C_Item.GetItemInfoInstant` to inspect the item type instead.

```lua
-- Loop a bag and gather every openable the player can act on now
for slot = 1, GetContainerNumSlots(0) do
    local _, canOpen = C_Container.IsContainerItemOpenable(0, slot)
    if canOpen then
        -- pickup + use; this slot is good to open
    end
end

-- Show a different hint depending on the lock state
local isOpenable, canOpen = C_Item.IsItemOpenable({bagID = 0, slotIndex = 1})
if isOpenable and not canOpen then
    print("This item is locked.")
end
```

Implementation: `isOpenable` reads `ItemStats.Flags & 0x4` from the
client-side ItemSparse cache. `canOpen` adds a check that either
`ItemStats.LockID == 0` (no lock) or the CGItem instance's
`ITEM_FIELD_FLAGS & 0x4` (UNLOCKED) bit is set. Same conditions the
engine's tooltip builder uses at `0x0052E323` to gate the
`ITEM_OPENABLE` line.

### `C_Item.IsLocked(itemLocation)`

Returns `true` if the item is in the client-side "in-transaction"
lock state — picked up onto the cursor, mail-attached, trade-attached,
mid-swap, etc. Reads the per-CGItem instance flag at `+0x314` bit 0
(not the `ITEM_FIELD_FLAGS` descriptor field — vanilla's actual lock
is on the instance, not in `m_objectFields`).

```lua
if C_Item.IsLocked({bagID = 0, slotIndex = 1}) then
    -- skip the action; slot is mid-transaction
end
```

The lock is **client-managed, server-confirmed**:

- Engine sets the bit *optimistically* in pickup/equip/attach paths,
  before the transaction packet goes to the server. UI greys
  immediately, no round-trip wait.
- Engine clears the bit when the matching `SMSG_UPDATE_OBJECT`
  confirms the transaction.

Listen for the vanilla-native `ITEM_LOCK_CHANGED` event (no payload —
the engine fires it on every set/clear and addons re-poll the items
they care about) to react without timed polling.

### `C_Item.LockItem(itemLocation)`

Set the client-side lock on a single item by location. Calls the same
engine primitive (`FUN_ITEM_LOCK_BY_GUID = 0x004953E0`) the engine
itself uses optimistically before sending pickup/equip transactions.
Fires `ITEM_LOCK_CHANGED` the same way.

```lua
C_Item.LockItem({bagID = 0, slotIndex = 1})
```

> **Setting the lock doesn't make anything real happen on the server.**
> It's a pure client-side flag — the player's UI greys the item out
> until something clears it. No transaction packet is sent, no
> cancel-on-failure mechanism kicks in. Treat this as a UI hint for
> custom workflows (batch-equip preview, drag-target highlighting,
> etc.), not real transaction state.
>
> The lock stays until you call `UnlockItem` / `UnlockAllItems`, OR
> the engine receives an SMSG_UPDATE_OBJECT for the item from the
> server, which clears it as a side effect.

### `C_Item.LockItemByGUID(itemGUID)`

Same as [`LockItem`](#c_itemlockitemitemlocation) but takes a GUID
string in the canonical `"0xHHHHHHHHLLLLLLLL"` format
[`C_Item.GetItemGUID`](#c_itemgetitemguiditemlocation) returns. The
engine resolves through the object manager, so this can mark items
the player doesn't currently have in bags/equipment — trade window
contents, mail attachments, freshly looted but not yet bagged — as
long as the CGItem is loaded.

```lua
local guid = C_Item.GetItemGUID({equipmentSlotIndex = INVSLOT_HEAD})
C_Item.LockItemByGUID(guid)
```

Same caveats as `LockItem` — purely client-side, doesn't affect any
real engine transaction state.

### `C_Item.PickupItem(itemInfo)`

Finds the item matching `itemInfo` and picks it up onto the cursor —
the C-side equivalent of clicking it. From there the held item can be
dropped into a bag/equipment slot, deleted (`DeleteCursorItem`), sold
at a merchant, etc.

`itemInfo` accepts the same shapes as
[`C_Item.EquipItemByName`](#c_itemequipitembynameiteminfo--dstslot) —
itemID number, bare `"item:N"` string, full chat link, or a localized
item name (matched case-insensitively against each candidate's
*decorated* name, random suffix included). Unlike the by-name equip/use
functions, the search covers **equipment (slots 1..19, checked first)
and then bags (0..4)**, so `PickupItem` picks up a worn item as readily
as a bagged one.

Returns nothing. Silently no-ops when:

- the input is `nil`, an empty string, or otherwise unparseable
- no matching item is found in equipment or bags
- the cursor is already holding something (`CursorHasItem()` is true)
- the matched item is locked (mid-transaction)

Drives the engine's cursor primitives directly — the paperdoll-slot
pickup (`FUN_004C7300`) for an equipped match, the container pickup
(`FUN_00494B60` + client-side item lock) for a bag match — not the Lua
`PickupInventoryItem` / `PickupContainerItem` globals. Same shared
`Item::Cursor` wrappers `EquipItemByName`'s auto-slot path uses.

```lua
-- Pick up a bagged item by name, then drop it into the off-hand slot:
C_Item.PickupItem("Linen Cloth")
PickupInventoryItem(17)  -- or EquipCursorItem(), etc.

-- Pick up a worn item by ID (unequips onto the cursor):
C_Item.PickupItem(2589)

-- From a chat link:
C_Item.PickupItem(itemLink)
```

### `C_Item.RequestLoadItemDataByID(item)` / `C_Item.RequestLoadItemData(itemLocation)`

Asks the engine to fetch the item's data from the server if not already
cached. Returns `true` if the request was initiated (or the input was
parseable to an itemID), `false` for malformed input. Fire-and-forget —
the engine handles the round-trip; the data lands in the cache when the
server responds.

Fires `ITEM_DATA_LOAD_RESULT(itemID, success)` when the data lands in
the cache, matching the modern API. Synchronously fired when the item
was already cached (so polling code paths still work), asynchronously
fired when the engine's SMSG response handler completes a network
fetch.

> **`success` is `1` or `nil`.** The engine's printf-style event
> dispatcher has no `%b` token, so we encode the boolean as `1` for
> success / `nil` for failure (leaning on `lua_pushstring(NULL)` →
> `lua_pushnil`). `if success then …` distinguishes them correctly —
> `nil` is falsy, `1` is truthy. Same encoding as `GET_ITEM_INFO_RECEIVED`
> and `QUEST_DATA_LOAD_RESULT`.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("ITEM_DATA_LOAD_RESULT")
f:SetScript("OnEvent", function()
    -- vanilla 1.12: event payload is in `arg1`, `arg2`, ... globals
    if event == "ITEM_DATA_LOAD_RESULT" and arg2 then
        local _, type = C_Item.GetItemInfoInstant(arg1)
        -- ...
    end
end)
C_Item.RequestLoadItemDataByID(2589)
```

> **Engine-aligned cache strategy.** `RequestLoadItemData` does NOT
> eagerly create a cache entry. Empirically (e.g. pfQuest calling it
> at addon-load / PLAYER_ENTERING_WORLD against a cleared WDB), pre-
> creating the entry races the engine's natural inventory prefetch
> and can leave the item permanently pending — the engine sees the
> entry exists and skips its own `SMSG_ITEM_QUERY_SINGLE`, while our
> early query gets dropped. Instead we track the itemID and hook the
> engine's `SMSG_ITEM_QUERY_SINGLE_RESPONSE` handler at `0x0055BDB0`:
> when the engine fills any entry, we sweep tracked items and fire
> `ITEM_DATA_LOAD_RESULT` for matches. For items the engine doesn't
> prefetch (quest rewards, AH browse targets, etc.) we escalate after
> ~60 ticks by calling `CacheFetch` ourselves — by then engine state
> has settled and our query goes through normally. Hard timeout at
> ~1200 ticks (~20–40s) fires `ITEM_DATA_LOAD_RESULT(itemID, nil)`.

### `C_Item.UnlockAllItems()`

Sweep clear of every CGItem the engine knows about — same primitive
the engine itself runs on `PLAYER_LEAVING_WORLD`. One
`ITEM_LOCK_CHANGED` fires at the end.

```lua
C_Item.UnlockAllItems()
```

Cheapest recovery if you don't know which item is stuck. Same caveat
as `UnlockItem`: this is purely client-side state — it won't cancel
any pending server-side transactions.

### `C_Item.UnlockItem(itemLocation)`

Force-clear the client-side lock on one item. Recovery primitive —
fires `ITEM_LOCK_CHANGED` so the UI refreshes.

```lua
C_Item.UnlockItem({bagID = 0, slotIndex = 1})
```

Useful when a transaction packet was sent but the server's
confirmation never arrived: the item stays visually greyed
indefinitely (vanilla's only built-in trigger for the unlock-all
sweep is logout). This call gives you an in-session escape hatch.

> **Cursor / server state isn't touched.** Unlocking doesn't tell the
> server "cancel my transaction" — it only clears the local visual
> lock. If the item is genuinely mid-flight, the server's next
> update will set the lock right back. For cursor-cancel semantics,
> pair with vanilla-native `ClearCursor()`.

### `C_Item.UseAtCursor(itemInfo)`

Uses `itemInfo` at the player's current cursor world position —
ClassicAPI's `[@cursor]` analog for ground-target on-use items
(Iron Grenade, Bombling, demolition charges, etc.). Returns `true`
when the cursor-placement leg landed (the item fires at terrain);
`false` for items that aren't ground-target (the item still fires
normally with no implicit target), unparseable input, items not in
bags, cursor over UI / off-screen, etc.

`itemInfo` accepts the same shapes as
[`C_Item.UseItemByName`](#c_itemuseitembynameiteminfo--unit) — itemID,
bare `"item:N"`, full chat link, or localized name.

```lua
C_Item.UseAtCursor(4068)            -- Iron Grenade at cursor
C_Item.UseAtCursor("Iron Grenade")
```

Implementation chains the existing item-use path
(`Item::Location::FindByArgInBags` + `FUN_ITEM_USE`) with
[`Spell::AtCursor::Resolve`](#c_spellcastatcursorspellidorname) — same
cursor-resolution helper `C_Spell.CastAtCursor` uses. When the item
fires a non-ground-target spell, the cursor leg no-ops and returns
`false`; the item still uses normally (any implicit target — current
selection, etc. — applies).

Cancels placement automatically when the cursor isn't on terrain —
the item-use packet is never sent, so an off-screen click doesn't
waste the grenade.

### `C_Item.UseAtUnit(itemInfo, unit)`

Unit-position analog of
[`C_Item.UseAtCursor`](#c_itemuseatcursoriteminfo): uses `itemInfo` at
`unit`'s feet rather than the cursor. ClassicAPI's `[@unit]` for
ground-target on-use items. `itemInfo` accepts the same forms as
`UseAtCursor` (itemID, bare `"item:N"`, full chat link, localized
name); `unit` is any unit token (`"player"`, `"target"`,
`"mouseover"`, `"party1"`, …).

```lua
C_Item.UseAtUnit(4068, "target")            -- Iron Grenade at the target's feet
C_Item.UseAtUnit("Iron Grenade", "player")
```

Same chain as `UseAtCursor` (`Item::Location::FindByArgInBags` +
`FUN_ITEM_USE`) but committing the placement at the unit's world
position via
[`Spell::AtCursor::CommitAtCoords`](#c_spellcastatunitspellidorname-unit)
instead of the cursor raycast. Returns `true` when the placement
landed at the unit; `false` for non-ground-target items (the item
still fires with any implicit target), unparseable input,
item-not-in-bags, or an unresolvable unit. The unit is resolved
before the item fires, so an absent unit fails without consuming the
item.

### `C_Item.UseItemByName(itemInfo [, unit])`

Finds the first item in the player's bags matching `itemInfo` and
uses it. Returns nothing; silently no-ops when:

- the input is `nil`, an empty string, or otherwise unparseable
- no matching item is in bags
- the engine refuses the use — cooldown, locked item, level
  requirement, etc.

`itemInfo` accepts the same shapes as
[`C_Item.EquipItemByName`](#c_itemequipitembynameiteminfo--dstslot) —
itemID number, bare `"item:N"` string, full chat link, or a localized
item name. Name matching is case-insensitive against each candidate's
*decorated* name (random suffix included), the same shared predicate
`C_Item.IsEquippedItem` uses.

The optional `unit` argument is a unit token (`"player"`, `"target"`,
`"focus"`, `"partyN"`, `"raidN"`, `"nameplateN"`, …) used as the cast
target for items that fire a spell (scrolls, traps, on-use targeted
effects). For self-use items (hearthstone, potions, food) the engine
overwrites the target with the item's own GUID before dispatch, so
passing a `unit` to those is harmless and has no effect. Unrecognized
strings are treated as "no target" rather than raising, matching the
silently-no-op contract of `itemInfo`.

```lua
C_Item.UseItemByName("Hearthstone")                       -- hearth home
C_Item.UseItemByName(6948)                                -- same thing, by ID
C_Item.UseItemByName("Major Healing Potion")
C_Item.UseItemByName("Scroll of Stamina IV", "target")    -- buff your tank
```

Mirrors 3.3.5's `Script_UseItemByName` structure: locate the item
directly, then hand the `CGItem *` to the engine's by-pointer use
primitive at `0x005D8D00`. That primitive dispatches internally based
on item type (food, potion, on-use spell, scroll, quiver, ...) so a
single call covers every item category. We skip
`Script_UseContainerItem` entirely — its branches for repair vendor,
spell-cast targeting, and drop-on-bag cursor modes don't apply to an
addon-issued call from a clean cursor.

### `Get*ItemID` — companions to the engine's `Get*ItemLink` family

The 1.12 engine ships ~14 `Get*ItemLink` functions covering every
frame that lets you mouse over an item (loot, merchant, quests,
auction, trade, mail, tradeskill, craft). To get the itemID, the
standard pattern is to call the `Link` function and scrape the
number out of the returned string with `gsub` / `match`. Modern
WoW only has direct-ID accessors for a handful of these
(`GetLootSlotItemID`, `GetInboxItemID`, `GetQuestItemID`,
`GetMerchantItemID`, …), and the rest of the addon ecosystem still
scrapes the link.

These backport the modern ones where they exist and fill in the
gaps for the rest, so the whole `Get*ItemLink` family has a
1-to-1 ID companion. Each reads the same itemID the engine reads
when building the link, with no string parsing required.

All return `nil` for an empty slot, an out-of-range index, or when
the relevant UI frame isn't open.

| Function | Companion to | Notes |
|----------|--------------|-------|
| `GetLootRollItemID(rollID)` | `GetLootRollItemLink` | Group-loot roll ID (from `START_LOOT_ROLL`). Walks the engine's in-progress roll list. |
| `GetLootSlotItemID(slot)` | `GetLootSlotLink` | 1-based slot. Returns `nil` for coin slots. |
| `GetMerchantItemID(index)` | `GetMerchantItemLink` | 1-based; the active merchant's inventory. |
| `GetQuestItemID(type, index)` | `GetQuestItemLink` | `type` is `"reward"` or `"choice"`; active quest-offer / completion UI. |
| `GetQuestLogItemID(type, index)` | `GetQuestLogItemLink` | Same args; reads the currently selected quest-log entry. |
| `GetTradePlayerItemID(slot)` | `GetTradePlayerItemLink` | 1..7; your side of the trade window. |
| `GetTradeTargetItemID(slot)` | `GetTradeTargetItemLink` | 1..7; the other player's side. |
| `GetAuctionItemID(type, index)` | `GetAuctionItemLink` | `type` is `"list"`, `"owner"`, or `"bidder"`. |
| `GetAuctionSellItemID()` | `GetAuctionSellItemInfo` | The item currently in the sell slot. No args. |
| `GetInboxItemID(mailID)` | `GetInboxItem` | 1-based mail index; nil for gold-only mail. |
| `GetTradeSkillItemID(index)` | `GetTradeSkillItemLink` | The recipe's *created item* itemID. |
| `GetTradeSkillReagentItemID(index, reagentIndex)` | `GetTradeSkillReagentItemLink` | reagentIndex is 1-based; reagents are densely packed (no skipping empty slots). |
| `GetCraftReagentItemID(craftIndex, reagentIndex)` | `GetCraftReagentItemLink` | Craft frame (enchanting / beast training) — same shape as tradeskill reagents. |
| `GetCraftSpellID(craftIndex)` | `GetCraftItemLink` | The craft frame's link is `Hspell:`, not `Hitem:` — so the companion is a **spellID**, not an itemID. Addons scraping for itemID get nil today; this returns the actual identifier. |

```lua
-- Drop-in for the typical scrape:
--   local id = tonumber((GetLootRollItemLink(rollID)):match("item:(%d+)"))
local id = GetLootRollItemID(rollID)

-- Plays nice with the existing data APIs:
if id then
    local _, _, _, _, _, classID = C_Item.GetItemInfoInstant(id)
    -- ...
end
```

> **Quest UI distinction.** `GetQuestItemID` reads the *active quest
> offer* (the QuestFrame you see when accepting/turning in a quest);
> `GetQuestLogItemID` reads the currently-selected entry in the
> *quest log*. The two cover different states intentionally — modern
> WoW does the same split with `GetQuestItemLink` /
> `GetQuestLogItemLink`.

> **`GetTradeSkillItemID` vs `GetCraftSpellID`.** Vanilla splits
> recipes across two UI frames: TradeSkill (smithing / alchemy /
> tailoring / etc., which always produce a finished item) and Craft
> (enchanting formulas / beast-training tomes, where the spell IS
> the deliverable). `GetTradeSkillItemID` returns the produced
> itemID; `GetCraftSpellID` returns the recipe spellID — because
> there's no produced item to point at.

### `GetAverageItemLevel()`

Returns `(avgItemLevel, avgItemLevelEquipped)` — modern WoW's
2-tuple shape. `avgItemLevelEquipped` is the arithmetic mean over
the player's currently-worn equipment slots; `avgItemLevel` is the
same metric but extended to include best-per-slot upgrades found
anywhere in the player's bags and bank.

Slots considered: `INVSLOT_HEAD..INVSLOT_TABARD` (1..19) minus
shirt (4) and tabard (19) = 17 candidate slots. Bag-walk uses the
same `invType → slotMask` mapping as `GetInventoryItemsForSlot`.

```lua
local overall, equipped = GetAverageItemLevel()
-- overall:  best-per-slot ilvl across equipped + bags + bank
-- equipped: ilvl of currently-worn items only
```

**Fixed denominator.** Empty slots count toward the denominator
(contributing 0 to the sum). Removing a piece of gear always
lowers `avgItemLevelEquipped` — matches modern's behavior
(verified against retail Wow.exe at `552.4375 × 16 = 8839`
exactly). Vanilla addons of the GearScore era used populated-only
count; this implementation deliberately differs.

**Max-of-two-divisors fairness.** A 2H weapon wielder has an
intentionally empty offhand (slot 17) — counting it as 0 in a
17-divisor sum would unfairly penalize them. We compute a second
candidate excluding slot 17 from both numerator and denominator
(16 slots), and return the max. Sword+shield characters typically
win the all-17 path; 2H wielders win the no-OH path. Same trick
the 4.3.4 client uses at `FUN_0097E0F0`'s tail.

**No double-counting.** Each bag/bank item is assigned to **one**
candidate slot via greedy fit — a trinket in your bag fills one
trinket slot (the empty/lower-best one), not both. Without this
gate, moving a trinket from equipped to bag would inflate the
"overall" count and drag the average down (because the same item
would count for both trinket slots).

**Bank is walked** even if the bank window has never been opened
this session — the GUID array at `invMgr + 0x04` is populated
from server data at login. Bypasses the bank-window gate on
`GetItemBySlot` the same way `C_Item.GetItemCount` does.

Limitations:
- Items not yet in the ItemStats cache are skipped from the
  running totals and a warmup is queued; the next call after
  `ITEM_DATA_LOAD_RESULT` lands picks them up.
- The full 4.3.4 weapon `qsort` dance — handling 2H-equipped vs.
  1H + OH-in-bag comparisons — is not replicated. Edge case in
  vanilla where you'd notice a difference is narrow.

### `GetInventoryItemDurability(invSlot)`

Returns `(current, maximum)` durability for the player's equipped item
at `invSlot` (1-based, character-pane slots 1..19), or nothing if the
slot is empty or the item has no durability concept (rings, trinkets,
necks, backs, shirts, tabards, etc.).

```
current, maximum = GetInventoryItemDurability(invSlot)
```

```lua
local cur, max = GetInventoryItemDurability(INVSLOT_CHEST)
if cur then
    -- cur, max are positive integers (e.g. 65, 65 for full chest)
end

-- Items without durability return nothing — both locals are nil:
local cur, max = GetInventoryItemDurability(INVSLOT_FINGER1)
-- cur == nil, max == nil
```

> **Player-only.** Matches modern API: 3.3.5+ `GetInventoryItemDurability`
> takes only the slot, no unit token. Inspect targets / party members'
> equipment durability isn't broadcast in 1.12, so we couldn't expose it
> for other units even if we wanted to.

> **`(0, max)` vs nothing.** Items that have a durability concept but
> are currently broken (`current == 0`, `max > 0`) still return
> `(0, max)`. The "nothing" return is reserved for items that have no
> durability fields populated at all — `max` is the discriminator,
> matching the engine's own `GetInventoryItemBroken` logic.

Reads ITEM_FIELD_DURABILITY (+0xA0) and ITEM_FIELD_MAXDURABILITY
(+0xA4) directly off the CGItem's m_objectFields descriptor at `+0x114`
— same descriptor [`C_Item.IsBound`](#c_itemisbounditemlocation) reads
FLAGS from. No DBC indirection.

### `GetInventoryItemID(unit, slot)`

Returns the itemID of the item equipped at `slot` (1-based) on `unit`,
or `nil` if the slot is empty / the unit isn't valid / the unit doesn't
expose its equipment to the local client. Same arg shape as 1.12's
`GetInventoryItemLink(unit, slot)` — drop-in for code that just needs
the ID and would otherwise have to parse the link string.

- For `"player"` (and any token resolving to the local player, e.g.
  `"target"` when self-targeted): walks the private inventory manager.
  Supports the full slot range (equipment 1..19, bag slots 20..23,
  bank slots, etc.) — same range `GetItemBySlot` accepts internally.
- For other player-controlled units (`"target"`, `"party1"..party4"`,
  inspect targets): reads the unit's broadcast visible-items array.
  Equipment slots 1..19 only.
- For NPCs / creatures: returns `nil`. The visible-items array isn't
  populated for non-player-controlled units in 1.12, so we gate this
  on `UnitPlayerControlled` to avoid the engine crash that
  `GetInventoryItemLink` itself would trigger on the same input.

```lua
local id = GetInventoryItemID("player", INVSLOT_HEAD)
if id then
    local _, type, subtype = C_Item.GetItemInfoInstant(id)
    -- ...
end

-- Inspect a party member without parsing a hyperlink:
local headID = GetInventoryItemID("party1", INVSLOT_HEAD)
```

### `GetInventoryItemRepairCost(invSlot)`

Returns the cost in copper to repair the player's equipped item at
`invSlot`. Same value `GameTooltip:SetInventoryItem` returns as its
third out-parameter.

```
copperCost = GetInventoryItemRepairCost(invSlot)
```

```lua
local cost = GetInventoryItemRepairCost(INVSLOT_CHEST)
if cost > 0 then
    -- e.g. cost == 12345
end
```

Returns `0` for slots that are empty, items without a durability
concept, fully-repaired items, or items whose stats aren't cached yet
(rare — happens briefly after login before SMSG_ITEM_QUERY_RESPONSE
arrives for newly-seen items). The "no return" path is reserved for
invalid input (non-numeric slot).

> **Player-only**, same constraint as
> [`GetInventoryItemDurability`](#getinventoryitemdurabilityinvslot)
> — other units' durability isn't broadcast in 1.12.

> **Discount is vendor-context-dependent.** The faction-rep + PvP
> rank discount is only applied when the player has a merchant
> window open (i.e. has received `SMSG_LIST_INVENTORY`). Called from
> anywhere else, you get the raw, undiscounted base cost.
>
> The engine tracks the current merchant via globals
> `DAT_00BDDFA0/A4` (the merchant's GUID), set when the merchant
> frame opens and zeroed when it closes. The helper short-circuits
> the discount path when those globals are zero.
>
> For consistent "what will I pay" semantics inside addons, only
> trust this value when `MerchantFrame:IsShown()` is true — or call
> it once per merchant-visit and cache.

Calls the engine's own per-item repair-cost helper at `0x004FAF30`,
which is the same function `Script_GameTooltip_SetInventoryItem`
calls for its repairCost return. The raw cost comes from
`DurabilityCosts.dbc` (indexed by item subclass and slot type) ×
`DurabilityQuality.dbc` (indexed by item quality); the discount —
when applicable — stacks faction reputation with the merchant
(Friendly+ unlocks a base 5%) and PvP rank (Sergeant Major+ adds
another 5%, Knight-Lieutenant+ stacks one more).

This is a ClassicAPI addition — modern WoW has no standalone Lua
function for per-item repair cost; the only way to read it there is
the tooltip's third return value. We expose it directly because the
underlying calculation is already in the engine and a Lua function
is the natural surface.

### `GetInventoryItemsForSlot(slot, returnTable [, transmogrify])`

Populates `returnTable` with every item in the player's equipment
and bags that's eligible to be equipped in `slot` (1..19). Returned
table is keyed by the packed [ItemLocation
bitfield](#equipmentset) — same encoding
`C_EquipmentSet.GetItemLocations` uses — with the itemLink as the
value. Returns the same `returnTable` reference for chaining.

```lua
local t = {}
GetInventoryItemsForSlot(16, t)   -- mainhand
for loc, link in pairs(t) do
    -- loc is a number like 0x00300103; link is "|c…|Hitem:…|h[…]|h|r"
end
```

| slot | Eligible InventoryTypes |
|------|-------------------------|
| 1 HEAD, 2 NECK, …, 10 HANDS | The slot's matching InventoryType |
| 11, 12 FINGER | INVTYPE_FINGER (11) — items appear under both ring slots |
| 13, 14 TRINKET | INVTYPE_TRINKET (12) |
| 15 BACK | INVTYPE_CLOAK (16) |
| 16 MAINHAND | INVTYPE_WEAPON (13), INVTYPE_2HWEAPON (17), INVTYPE_WEAPONMAINHAND (21) |
| 17 OFFHAND | INVTYPE_WEAPON (13), INVTYPE_SHIELD (14), INVTYPE_WEAPONOFFHAND (22), INVTYPE_HOLDABLE (23) |
| 18 RANGED | INVTYPE_RANGED (15), INVTYPE_THROWN (25), INVTYPE_RANGEDRIGHT (26) |
| 5 CHEST | INVTYPE_CHEST (5), INVTYPE_ROBE (20) |
| 19 TABARD | INVTYPE_TABARD (19) |

Compatibility check is a single bitwise AND against a static
`invType → slotMask` table — same shape and values as 3.3.5's table
at `DAT_00A2D288` (used by `Script_GetInventoryItemsForSlot` via
`FUN_007082B0`), with two adjustments for vanilla:

- 2H weapons (`INVTYPE_2HWEAPON`) confined to the mainhand slot
  only. 3.3.5 allowed offhand for the titanic-grip era; vanilla has
  no such mechanic and the server would reject the equip anyway.
- `INVTYPE_WEAPONMAINHAND` / `WEAPONOFFHAND` confined to their
  literal slot rather than the looser "either hand" 3.3.5 allowed.

The `transmogrify` arg is accepted and ignored. Stock 1.12 has no
transmog system to query against, and the modern flag's effect
(broader cross-eligibility for visual swaps) reduces to the
regular equip check here. Private servers like Turtle WoW layer
their own transmog systems on top of vanilla; this function
doesn't currently plumb the flag through to any of them.

> **Bank items aren't included.** Vanilla's `GetItemBySlot` gates
> bank slots on a banker GUID that's only set while the bank window
> is open; modern WoW behaves the same way (bank shows up only when
> the bank UI is open). We don't walk bank slots — addons that want
> bank inclusion need their own bank scan with the bank UI open.

### `GetItemIcon(itemID)` / `C_Item.GetItemIcon(itemLocation)` / `C_Item.GetItemIconByID(item)`

Three accessors that all return the icon path for an item, differing only in
how you address the item:

| Function                             | Input                     |
|--------------------------------------|---------------------------|
| `GetItemIcon(itemID)`                | numeric itemID (global)   |
| `C_Item.GetItemIcon(itemLocation)`   | `{equipmentSlotIndex=N}` or `{bagID=B, slotIndex=S}` |
| `C_Item.GetItemIconByID(item)`       | numeric itemID OR `"item:NNN"` string (full chat links work) |

All three return the icon path string (e.g. `"Interface\\Icons\\INV_Misc_Rune_01"`),
or `nil` if the item isn't in the client-side cache, the slot is empty, or
the display info record is missing. The path is suitable for direct use
with `texture:SetTexture(...)`.

```lua
local path = GetItemIcon(6948)                                -- "Interface\\Icons\\INV_Misc_Rune_01"
local path = C_Item.GetItemIcon({equipmentSlotIndex = INVSLOT_HEAD})
local path = C_Item.GetItemIconByID("|cff...|Hitem:6948:0:0:0|h[Hearthstone]|h|r")
```

> **`iconID`-vs-path deviation.** Modern WoW returns these as a
> `fileID:number` (specifically `iconFileID` in Classic Era 1.15.x). 1.12
> has no fileID system — same situation as
> [`C_Spell.GetSpellTexture`](#c_spellgetspelltexturespellid) and
> [`C_Spell.GetSpellInfo`](#c_spellgetspellinfospellid)'s `iconID` field.
> We surface the path string everywhere for consistency.

Equivalent to the legacy global `GetItemIcon` (since 1.x) and the
`C_Item.GetItemIcon` / `GetItemIconByID` family added in 10.x.

### `OffhandHasWeapon()`

Returns `true` if the player has a one-handed weapon (or off-hand-only
weapon) equipped in the off-hand slot, `false` otherwise. Used by
dual-wield checks and any addon backporting modern weapon-equipment
logic.

Returns `false` for:

- Empty off-hand
- Shields (`INVTYPE_SHIELD`)
- Held items — tomes, orbs, librams (`INVTYPE_HOLDABLE`)
- Off-hand item data not yet cached (typically only on first login,
  before the engine has the item record; warms up after a peek)

Returns `true` for `INVTYPE_WEAPON` (any one-handed weapon — sword,
axe, mace, dagger, fist) and `INVTYPE_WEAPONOFFHAND` (off-hand-only
weapons). Two-handers occupy the main-hand slot exclusively, so they
never apply.

```lua
if OffhandHasWeapon() then
    -- Apply mainhand+offhand poison, refresh dual-wield rotation, etc.
end
```

## Loot

Programmatic loot operations that vanilla 1.12's Lua surface doesn't
expose: enumerate nearby lootable corpses, open a loot session against
one by GUID, take a specific item out of one without going through the
visible `LootFrame`, and pre-scan every reachable corpse to build a
picker table before any cursor work. Foundation for AoE-loot,
auto-loot-by-filter, and similar addons that 1.12's stock
`LootSlot` / `LootSlotInfo` flow can't drive.

All `guid` args are hex-string GUIDs in the same form `UnitGUID`
returns (`"0xF130..."`). All `itemID` args are integers.

### `C_Loot.GetNearbyLootableUnits()`

Returns a 1-indexed array of `{ guid = "0x..." }` subtables, one per
visible unit that the server has flagged lootable AND that's within
the engine's right-click-loot interact range. Empty table pre-login,
when no lootable units are in range, or when the local player has no
loot rights on visible corpses (the server only sets the lootable
flag for clients with rights).

```lua
for _, unit in ipairs(C_Loot.GetNearbyLootableUnits()) do
    C_Loot.LootUnit(unit.guid)
end
```

The interact-range check matches what the engine uses for the
hover-to-loot cursor — slightly tighter than melee range, looser than
"on top of." Server still enforces real range on the resulting
`CMSG_LOOT`; in-client OOR requests just silently get no response.

### `C_Loot.LootUnit(guid)`

Opens a loot session against the given corpse. The loot window
appears asynchronously via the normal `LOOT_OPENED` event once
`SMSG_LOOT_RESPONSE` arrives — same flow as right-clicking the
corpse, just dispatched programmatically.

No return value. Silent no-op when:
- The object manager is NULL (glue / character-select).
- The GUID doesn't resolve to a visible unit (out of render range,
  despawned, wrong typemask).
- The local player pointer can't be resolved.

Errors only on bad Lua usage (missing arg, unparseable GUID).

```lua
-- Loot everything in range, one corpse at a time. With autoloot on,
-- the engine drains each window automatically; without it, you
-- still get the visible loot frame per corpse.
for _, unit in ipairs(C_Loot.GetNearbyLootableUnits()) do
    C_Loot.LootUnit(unit.guid)
end
```

`useDistanceCheck` is hardcoded off — the engine won't try to
auto-walk the player toward a corpse just out of range. That'd be a
surprise side effect for an AoE-loot loop.

### `C_Loot.LootUnitItem(guid, itemID)`

Takes a specific item from a specific corpse without using the
visible loot frame's slot list. Dispatches `CMSG_LOOT_ITEM` directly.

Two paths, picked automatically:

1. **Fast path** — the corpse's loot window is already open for the
   requested target. Synchronously finds the slot, sends the take
   packet, returns `true` if the slot was found / `false` if not.
2. **Async path** — opens the corpse's window first (same wire as
   `C_Loot.LootUnit`), waits for the response, then sends the take
   packet automatically. Returns `true` once the request is queued.
   No "did it succeed" event; observe `BAG_UPDATE` / `ITEM_PUSH` to
   confirm.

`false` immediately when:
- Args missing or wrong types (raises an error, not returns false).
- Another `LootUnitItem` is mid-flight (one at a time).
- The world isn't loaded.
- The target GUID doesn't resolve to a visible unit.
- (sync path only) No slot in the open window holds the itemID.

The send bypasses vanilla's BoP-confirmation dialog. Server still
enforces all real permissions (loot rights, distance, master-loot
rules); the BoP prompt is purely a client-side courtesy that callers
of a programmatic API have already opted out of.

```lua
-- Scan, then take just one specific item out of one specific corpse.
C_Loot.ScanNearbyLoot()  -- LOOT_SCAN_COMPLETED → results below
local results = C_Loot.GetLastScanResults()
local guid = results[1].guid
local itemID = results[1].items[1].itemID
C_Loot.LootUnitItem(guid, itemID)
```

### `C_Loot.ScanNearbyLoot()`

Initiates a pre-scan: opens each lootable corpse in range one at a
time, scrapes its slot contents (itemID, count, item link), closes
the window, advances. The `LOOT_OPENED` / `LOOT_CLOSED` events are
suppressed for the duration of the scan so `LootFrame` and other
listeners don't react — the scan is invisible to addons that aren't
opted in.

Returns `true` if the scan was queued, `false` if another scan is
already in progress or the world isn't loaded. Completion is signaled
via the [`LOOT_SCAN_COMPLETED`](#loot_scan_completed-event) event;
results are read via [`C_Loot.GetLastScanResults()`](#c_lootgetlastscanresults).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("LOOT_SCAN_COMPLETED")
f:SetScript("OnEvent", function()
    for _, corpse in ipairs(C_Loot.GetLastScanResults()) do
        for _, item in ipairs(corpse.items) do
            ChatFrame1:AddMessage(item.link or ("item:"..item.itemID))
        end
    end
end)
C_Loot.ScanNearbyLoot()
```

Per-corpse step timeout is ~3-6 seconds (180 WorldTick frames) so a
single dropped server response doesn't hang the whole scan. Failed
corpses simply don't appear in the results.

### `C_Loot.LootAllCorpses([max])`

Loots **every** nearby lootable corpse in sequence — the same corpse
walk as [`ScanNearbyLoot`](#c_lootscannearbyloot), but it actually takes
the loot: coin (`CMSG_LOOT_MONEY`) plus every item slot (`CMSG_LOOT_ITEM`)
from each window before releasing and advancing to the next corpse. The
optional `max` caps how many corpses are visited (default: all in range).

Returns `true` if the loot walk was queued, `false` if a walk (this or
`ScanNearbyLoot`) is already in progress or the world isn't loaded — the
two share one state machine, so [`IsScanInProgress`](#c_lootisscaninprogress)
reports either, and only one runs at a time.

Like the scan, the walk is **silent**: `LOOT_OPENED` / `LOOT_CLOSED` are
suppressed so `LootFrame` never flickers open per corpse. The normal
item-received and money events still fire (`CHAT_MSG_LOOT`,
`CHAT_MSG_MONEY`, inventory updates), so the player sees what they got.

```lua
-- Loot everything within interact range
C_Loot.LootAllCorpses()

-- Loot at most 5 corpses
C_Loot.LootAllCorpses(5)
```

Completion is signaled by [`LOOT_SCAN_COMPLETED`](#loot_scan_completed-event);
afterward [`GetLastScanResults()`](#c_lootgetlastscanresults) reports what
each corpse held — i.e. what was looted.

> **Range and permissions.** Only corpses inside the engine's own
> right-click-loot range are queued (same distance test as
> `GetNearbyLootableUnits`), and only those the server flagged lootable
> for you. The item sends bypass the client-side BoP/unique **confirm
> dialog** (appropriate for a programmatic loot-all), but the server still
> enforces every real rule — loot rights, distance, master-loot. Unlike a
> right-click, this does **not** change your target or auto-walk you toward
> out-of-range corpses.

### `C_Loot.IsScanInProgress()`

Returns `true` while a `ScanNearbyLoot` call is mid-flight, `false`
otherwise. Use to gate re-scan attempts so addons don't spam-call
during the engine's per-corpse round-trip.

### `C_Loot.GetLastScanResults()`

Returns the most-recent completed-scan results as a 1-indexed array:

```lua
{
    [1] = {
        guid = "0x...",    -- corpse GUID
        coin = 1234,       -- copper amount; 0 if no coin
        items = {
            [1] = {
                itemID = 2589,
                count  = 4,
                link   = "|cff...|Hitem:2589:0:0:0|h[Linen Cloth]|h|r",
                                   -- omitted if cache entry wasn't loaded
            },
            ...
        },
    },
    ...
}
```

Empty table before the first scan completes. Replaced wholesale by
each new scan — re-read after `LOOT_SCAN_COMPLETED` rather than
caching the table.

The `link` field is the engine's full hyperlink (color + payload +
display name + reset). Random-suffix items like "Stringy Wolf Meat
of the Bear" survive round-trip — the link encodes the per-instance
enchant / suffix / unique fields, not just the base itemID. For
tooltip display, extract the payload form via
`string.match(link, "|H(item:[^|]+)|h")` — vanilla's `SetHyperlink`
requires the literal `"item:"` prefix and rejects full `|cff...|Hitem...|h`
input.

## LootHistory

Backport of the MoP `C_LootHistory` namespace — group-loot roll history.
Vanilla 1.12 has no loot-history store, but it does receive the live
group-loot roll traffic (`SMSG_LOOT_ROLL` per player, `SMSG_LOOT_ROLL_WON`,
`SMSG_LOOT_ALL_PASSED`) — which the stock UI only turns into chat lines. This
reconstructs the history client-side: packet co-hooks accumulate a ring of
rolled items with per-player Need/Greed/Pass results and the winner, exposed
through the namespace. Rollers' **name and class come from the engine
NameCache** captured at roll time, so they resolve even for a player who has
left the group by the time their roll arrives.

History is in-memory (last 128 rolled items) and resets on `/reload` or logout.
The MoP master-loot management functions (`SetExpiration` / `GiveMasterLoot` /
`CanMasterLoot`) are intentionally omitted — they drive server support 1.12
doesn't have.

Progress is signalled by the `LOOT_HISTORY_ROLL_CHANGED` /
`LOOT_HISTORY_ROLL_COMPLETE` events — see the
[Events](#loot_history_roll_changed--loot_history_roll_complete--loot_history_full_update-events)
section.

### `C_LootHistory.GetNumItems()`

Returns the number of rolled items currently in the history.

### `C_LootHistory.GetItem(itemIndex)`

`1`-based. Returns `rollID, itemLink, numPlayers, isDone, winnerIdx, timestamp`
(matches the MoP signature, with `timestamp` as a ClassicAPI extension):
- `rollID` — a **stable** monotonic ID for the roll. Unlike `itemIndex` (which
  shifts as the 128-item ring saturates), `rollID` never changes, so key
  "keep this row expanded" / "highlight this roll" state on it.
- `itemLink` — `item:<id>:0:<suffix>:<unique>` (valid for `GetItemInfo` /
  `GameTooltip:SetHyperlink`), carrying the roll's random suffix so
  "of the X" items keep their affix.
- `numPlayers` — how many players have rolled/passed so far.
- `isDone` — `true` once the item is decided (won or all-passed).
- `winnerIdx` — the winner's `1`-based **player index**, for
  `C_LootHistory.GetPlayerInfo(itemIndex, winnerIdx)` (name, class token,
  winning roll). `nil` if undecided / all passed. The winner is guaranteed to
  be in the player list even if their individual roll packet never arrived.
- `timestamp` — the roll's creation time in seconds, directly comparable to
  `GetTime()` (e.g. `GetTime() - timestamp` for "N seconds ago"). ClassicAPI
  extension — no MoP equivalent.

Returns nothing for an out-of-range index.

### `C_LootHistory.GetPlayerInfo(itemIndex, playerIndex)`

Both `1`-based. Returns `name, class, rollType, roll, isWinner, isMe`:
- `name` — the roller's name (from the NameCache).
- `class` — the class token (`"WARRIOR"`, `"MAGE"`, …) for
  `RAID_CLASS_COLORS`, or `nil` if unknown.
- `rollType` — `0` = pass, `1` = need, `2` = greed.
- `roll` — the `1`–`100` roll, or `0` if they passed.
- `isWinner` — `true` if this player won the item.
- `isMe` — `true` if this roller is the local player.

Returns nothing for an out-of-range item or player index.

### `C_LootHistory.Clear()`

Wipes the accumulated roll history (returns nothing). A ClassicAPI extension
with no MoP equivalent, for a "clear history" button. Fires
`LOOT_HISTORY_FULL_UPDATE` so any open display rebuilds empty.

## LossOfControl

Backports `C_LossOfControl` — the active crowd-control / interrupt effects on
the **local player**. Vanilla has no such aggregation layer (it's a Mists-era
addition), so it's synthesized: CC types from the player's debuffs, and the
school-interrupt lockout from the `SMSG_SPELL_COOLDOWN` the server sends on a
Counterspell / Kick.

### `C_LossOfControl.GetActiveLossOfControlDataCount()`

Returns the number of active loss-of-control effects on the player (`0` when
none).

### `C_LossOfControl.GetActiveLossOfControlData(index)`

Returns a `LossOfControlData` table for the `index`-th active effect (1-based,
up to the count above), or `nil` if the index is out of range.

```lua
for i = 1, C_LossOfControl.GetActiveLossOfControlDataCount() do
    local d = C_LossOfControl.GetActiveLossOfControlData(i)
    print(d.locType, d.displayText, d.timeRemaining)
end
```

| Field | Type | Notes |
|---|---|---|
| `locType` | string | `"STUN"`, `"FEAR"`, `"ROOT"`, `"CONFUSE"`, `"CHARM"`, `"POSSESS"`, `"SILENCE"`, `"PACIFY"`, `"PACIFYSILENCE"`, `"DISARM"`, or `"SCHOOL_INTERRUPT"`. |
| `spellID` | number | The spell causing the effect. `0` for `SCHOOL_INTERRUPT` (the interrupting spell isn't in the lockout packet). |
| `displayText` | string | Effect name — the spell name for CC, `"Interrupted"` for `SCHOOL_INTERRUPT`. |
| `iconTexture` | string | The spell's icon path (vanilla has no FileDataIDs; the path is the functional equivalent). `""` for `SCHOOL_INTERRUPT`. |
| `startTime` | number? | `GetTime()`-epoch seconds. Present for `SCHOOL_INTERRUPT` and for CC whose applying cast ClassicAPI observed; `nil` otherwise. |
| `timeRemaining` | number? | Seconds remaining. |
| `duration` | number? | Effect duration, in seconds. |
| `lockoutSchool` | number | The locked spell-school mask for `SCHOOL_INTERRUPT` (feed to `C_Spell.GetSchoolString` for the name); `0` for CC effects. |
| `priority` | number | Always `0` — no vanilla equivalent. |
| `displayType` | number | Always `2` (show for the effect's full duration). |

Fidelity vs. retail:

- **`SCHOOL_INTERRUPT`** is derived from the server's own lockout packet
  (`Player::ProhibitSpellSchool` sends a single `SMSG_SPELL_COOLDOWN` listing
  every spell of the interrupted school at one uniform duration), so the school
  and duration are authoritative. The interrupting spell itself isn't
  transmitted, hence `spellID = 0` and `displayText = "Interrupted"`.
- **CC timing** is best-effort — `startTime` / `timeRemaining` / `duration` are
  filled when ClassicAPI observed the cast that applied the aura (the
  `Aura::Source` cache) and are `nil` otherwise. The fields are nullable in the
  modern contract.
- **`auraInstanceID`** is omitted (`nil`) — a Dragonflight concept with no
  vanilla equivalent.

The `LOSS_OF_CONTROL_ADDED` / `LOSS_OF_CONTROL_UPDATE` events fire as these
effects change — see [Events](#loss_of_control_added--loss_of_control_update-events).

## Lua

Standard-library functions that later Lua versions (or Blizzard's FrameXML)
added and that 1.12's Lua 5.0 is missing — restored by ClassicAPI so
backported addons find them. Most are single-function additions (several are
just the 5.0→5.1 renames — `string.gmatch`←`gfind`, `math.fmod`←`math.mod`);
`coroutine.*` restores the whole stripped coroutine library.

### `select(index, ...)`

The Lua 5.1 vararg helper, backported to 1.12's Lua 5.0. Two forms:

- `select('#', ...)` → the number of extra arguments passed after the
  index.
- `select(n, ...)` → all arguments from position `n` onward. `n` may be
  negative to count back from the end (`-1` is the last argument).

Errors on a non-numeric, non-`'#'` first argument or an index of 0 /
below the negative range (matching Lua 5.1's `luaL_checkint` +
`index out of range` behavior). Registered on **both** Lua states, so it's
available to in-game addons and glue-screen (login/char-select) code alike.

```lua
select('#', 'a', 'b', 'c')   -- 3
select(2, 'a', 'b', 'c')     -- 'b', 'c'
select(-1, 'a', 'b', 'c')    -- 'c'

-- Idiomatic vararg count inside a function:
local function log(...)
    for i = 1, select('#', ...) do
        print(i, (select(i, ...)))
    end
end
```

Ported from Lua 5.1's `luaB_select` (lbaselib.c). The numeric form pushes
nothing — the varargs are already on the stack, so it just returns the
count of the slice from `n+1` onward.

### `table.wipe(t)`

Removes every key from `t`, leaving it empty but preserving its
internal hash and array capacity. Returns `t` for chaining.

```lua
local cache = {}
-- ... fill it up ...
table.wipe(cache)              -- cache is now {}
local also_t = table.wipe(t2)  -- also_t === t2 after wipe
```

Port of Blizzard's 3.3.5 implementation at VA `0x00852180` — same
pattern, just using 1.12's Lua 5.0 entry points instead of 5.1's.
Both Lua versions' `lua_next` walks the hash node array linearly,
so the canonical `lua_next` + `rawset(k, nil)` "during-iteration
removal" pattern works in practice even though it's technically
undefined per the Lua reference manual.

Errors on non-table input.

### `table.count(tbl)`

Returns `(numTableNodes, numArrayNodes, maxArrayIndex)` describing how a table
is populated (a modern WoW diagnostic, added retail 11.2.5). These are counts
of live entries, **not** the table's allocated capacity:

- **`numTableNodes`** — total number of key/value pairs.
- **`numArrayNodes`** — how many of those have an integer key in the range
  `[1..numTableNodes]` (the "contiguous array part" heuristic).
- **`maxArrayIndex`** — the largest positive integral key (`>= 1`), or `0` if
  there is none. Negative/zero integral keys don't count.

```lua
table.count({ 10, 20, 30 })           -- 3, 3, 3
table.count({ a = 1, b = 2 })         -- 2, 0, 0
table.count({ [1] = "x", foo = "y" }) -- 2, 1, 1
table.count({ [100] = "x" })          -- 1, 0, 100
table.count({ [-3] = "x" })           -- 1, 0, 0
```

Pure iteration over the table (`lua_next`), so no dependency on Lua's internal
storage layout. Errors on non-table input.

### `Mixin(object, ...)` / `CreateFromMixins(...)`

The FrameXML table-mixin primitives, provided as engine C functions.

- **`Mixin(object, ...)`** — shallow-copies every key/value from each table
  argument onto `object` (later arguments win on key collisions) and returns
  `object`. Non-table arguments are skipped.
- **`CreateFromMixins(...)`** — `Mixin({}, ...)`: returns a new table seeded
  from the given mixins.

```lua
local Greeter = {}
function Greeter:Hello() return "hi, "..self.name end
local obj = CreateFromMixins(Greeter)
obj.name = "Bob"
obj:Hello()   -- "hi, Bob"
```

Modern WoW provides these as C functions in `TableUtil`; ClassicAPI does the
same (rather than leaving them in the `!!!ClassicAPI` addon) so they exist
whenever the DLL is injected — even if the addon is disabled. The composite
`CreateAndInitFromMixin(mixin, ...)` (which calls a `:Init` method) remains a
Lua helper in the addon, mirroring retail's `Mixin.lua`. The taint-based
`SecureMixin` family is intentionally omitted — vanilla 1.12 has no execution
taint system, so there is nothing for it to guard.

### `string.match` / `string.gmatch`

The Lua 5.1 pattern helpers, backported to 1.12's Lua 5.0 (which ships only
`find` / `gfind` / `gsub`).

- **`string.match(s, pattern [, init])`** — returns the captures of the
  first match, the whole match when the pattern has no captures, or `nil`
  when it doesn't match. Same pattern engine as `string.find` — it's `find`
  returning the captures instead of the `start, end` indices, so it reuses
  the engine's own matcher rather than reimplementing it.
- **`string.gmatch(s, pattern)`** — the match iterator, for
  `for x in string.gmatch(s, pat) do ... end`. This is exactly Lua 5.0's
  `string.gfind` (renamed in 5.1); it's registered as a direct alias.

```lua
local y, m, d = string.match("2024-01-15", "(%d+)-(%d+)-(%d+)")  -- "2024","01","15"
string.match("no digits", "%d+")                                  -- nil

for word in string.gmatch("a,bb,ccc", "[^,]+") do print(word) end -- a / bb / ccc
```

> **Only the `string.foo(s, ...)` call form works — not the `("x"):foo(...)`
> method sugar.** WoW's Lua VM strips type-metatables for every value except
> tables and userdata (both `lua_setmetatable` and the VM's own
> `luaT_gettmbyobj` hard-return "no metamethod" for strings), so string values
> have no `__index` and there's no client-side way to add one short of hooking
> a hot VM-core function. This is the long-standing vanilla 1.12 constraint —
> always write `string.match(s, p)`, never `s:match(p)`.

### `strsplit(sep, str [, pieces])`

The WoW global (backported from 3.3.5), splits `str` on **any** character in
`sep` and returns the pieces as multiple values.

- `sep` — a set of delimiter *characters* (not a pattern); each character is
  a delimiter. `","` splits on commas; `" -"` splits on spaces and dashes.
- `pieces` (optional) — caps the number of results. After `pieces - 1`
  splits the rest of the string (delimiters and all) is returned as the final
  piece. `0` or omitted = unlimited. `1` returns the whole string unsplit.

Consecutive/trailing delimiters produce empty pieces, matching retail.

```lua
strsplit(",", "a,b,c")        -- "a", "b", "c"
strsplit(",", "a,b,c,d", 2)   -- "a", "b,c,d"     (capped at 2)
strsplit(" -", "a b-c")       -- "a", "b", "c"     (space OR dash)
strsplit(",", "a,,b")         -- "a", "", "b"      (empty middle piece)
local zone, x, y = strsplit(":", "Durotar:52:38")
```

Errors `strsplit(): Stack overflow` if a string splits into more pieces than
the Lua stack can grow to hold (`lua_checkstack` guards every push) — the
same guard and message 3.3.5 uses.

### `strjoin(delimiter, ...)`

The inverse of `strsplit`: concatenate the vararg pieces with `delimiter`
between each.

```lua
strjoin(",", "a", "b", "c")   -- "a,b,c"
strjoin("", "a", "b", "c")    -- "abc"
strjoin(",")                  -- ""   (no pieces)
```

A `nil` / non-string-coercible piece is treated as an empty string (so the
delimiters still line up) rather than erroring.

### `strtrim(str [, chars])`

Remove any of the characters in `chars` from both ends of `str`. `chars` is a
literal **set of characters** (not a Lua pattern); it defaults to whitespace
(space, tab, `\n`, `\v`, `\f`, `\r`).

```lua
strtrim("  hello  ")        -- "hello"
strtrim("xxhelloxx", "x")   -- "hello"
strtrim("[a]", "[]")        -- "a"
```

### `strreplace(str, find, replace)`

Replace every occurrence of the literal substring `find` with `replace`,
returning `(result, count)`. This is a **plain-text** replace — no pattern
magic-character escaping, unlike `string.gsub`. An empty `find` returns `str`
unchanged with count `0`.

```lua
strreplace("a.b.c", ".", "-")        -- "a-b-c", 2   (no pattern escaping)
strreplace("hello world", "o", "0")  -- "hell0 w0rld", 2
```

> Not a stock WoW global — a ClassicAPI convenience. For pattern-based
> replacement use `string.gsub`.

### `string.reverse(s)` / `strrev(s)`

Returns `s` with its bytes reversed. Lua 5.1 added `string.reverse`; 5.0's
string library lacks it. Byte-wise (not UTF-8 aware), matching stock Lua.
Registered both on the `string` table and as the `strrev` global.

```lua
string.reverse("abc")   -- "cba"
strrev("hello")         -- "olleh"
```

### `math.fmod(x, y)`

The floating-point remainder of `x / y` (the quotient truncated toward zero),
same as C's `fmod`. A pure 5.0→5.1 rename: Lua 5.0 ships the identical C
function as `math.mod`, and 5.1 renamed the Lua-facing name to `math.fmod`.
Registered as a direct alias — `math.mod` remains available too, so code
written for either name works.

```lua
math.fmod(7, 3)     -- 1
math.fmod(-7, 3)    -- -1   (result takes the sign of the dividend)
math.fmod(5.5, 2)   -- 1.5
```

### `math.modf(x)`

Returns `(integral, fractional)` — the integral part truncated toward zero
and the fractional part carrying `x`'s sign. Lua 5.1 function, genuinely
missing from 1.12's math library (not just renamed like `fmod`).

```lua
math.modf(3.7)    -- 3, 0.7
math.modf(-3.7)   -- -3, -0.7
math.modf(5)      -- 5, 0
```

### `math.huge`

Positive infinity, as the numeric constant `math.huge` (Lua 5.1). Handy as a
"larger than anything" sentinel in min/max scans.

```lua
local best = math.huge
for _, v in ipairs(costs) do best = math.min(best, v) end
```

### `coroutine.*`

Restores Lua 5.0's stripped `coroutine.*` library. The C-level
coroutine machinery (`lua_resume`, `lua_yield`, `lua_newthread`,
and the `thread` type at tag 8) is linked into the engine, but
the script-facing library was never registered as a global table.
ClassicAPI rewires the standard five entries on `coroutine`,
matching Lua 5.0 semantics with two WoW-specific quirks called
out below.

#### `coroutine.create(fn)`

Creates a new coroutine with `fn` as its body and returns the
resulting `thread`. `fn` must be a **Lua** function — passing a
C function (any engine global like `GetTime` or `UnitName`)
raises `bad argument #1 to 'create' (Lua function expected)`.

The new coroutine starts suspended; nothing runs until
`coroutine.resume` is called on it. The args passed to the first
resume become the body's arguments.

#### `coroutine.resume(co, ...)`

Resumes a suspended coroutine, passing the trailing args to either
the body (first resume) or back as the return values of the
`coroutine.yield` that paused the coroutine.

Returns:

- `true, ...values` on success, where `values` are whatever the
  coroutine yielded (mid-execution) or returned (on completion).
- `false, errMsg` on error. Two error sources:
  - Coroutine isn't suspended — e.g. resuming a dead one;
    `errMsg` is a fixed string like `"cannot resume dead coroutine"`.
  - The body hit a Lua VM error; `errMsg` is the formatted error
    string with file:line prefix.

**Caveat — `error()` doesn't propagate.** WoW replaces Lua's
`error()` global with a soft handler that calls
`geterrorhandler()(msg)` and returns normally instead of
longjmp'ing. So `error("msg")` inside a coroutine body looks like
a clean return to `resume` — the message gets printed to chat as
a side effect and `resume` returns `true` with no values. Real VM
errors (calling `nil`, indexing `nil`, etc.) still propagate
through the standard `false, msg` path.

#### `coroutine.yield(...)`

Suspends the current coroutine. Whatever's passed to `yield`
becomes the return values of the matching `coroutine.resume`
call. When the coroutine is next resumed, the args passed to
`resume` become the return values of `yield`.

Raises an error if called outside a coroutine
(`attempt to yield across metamethod/C-call boundary`) or from
a C function (`cannot yield a C function`).

#### `coroutine.status(co)`

Returns the coroutine's state as a string:

| Value | Meaning |
|-------|---------|
| `"running"` | `co` is the currently-running coroutine (i.e. the same thread that called `status`). |
| `"suspended"` | `co` is paused — either initial (never resumed) or mid-yield. |
| `"dead"` | `co`'s body has returned or errored. |

The standard Lua `"normal"` state — a coroutine that resumed a
deeper coroutine and is now an ancestor on the call chain — is
collapsed into `"suspended"`. Detecting it cleanly requires
walking the global thread list, and the case is near-useless for
addon-level logic.

#### `coroutine.wrap(fn)`

Equivalent to `coroutine.create(fn)` followed by returning a
closure that calls `coroutine.resume(co, ...)` on each invocation
— but propagates errors as raised exceptions (via `lua_error`)
instead of returning `false, msg`. Lets you use a coroutine as
if it were an ordinary function:

```lua
local gen = coroutine.wrap(function()
    for i = 1, 5 do coroutine.yield(i) end
end)
print(gen())  -- 1
print(gen())  -- 2
print(gen())  -- 3
```

Calling the wrapped function after the body has completed raises
`cannot resume dead coroutine`. The canonical "iterator that
terminates cleanly through `for ... in`" idiom is to yield an
explicit `nil` at the end so the for-loop's nil sentinel fires:

```lua
local function range(from, to)
    return coroutine.wrap(function()
        for i = from, to do coroutine.yield(i) end
        coroutine.yield(nil)
    end)
end

for n in range(1, 5) do print(n) end
```

#### Async pattern (`RunAsync` + `C_Timer.After`)

Coroutines pair naturally with `C_Timer.After` for chunked
"do some work, wait a frame, do more" loops. The canonical
scheduler:

```lua
local function RunAsync(fn)
    local co = coroutine.create(fn)
    local function step()
        local ok, delay = coroutine.resume(co)
        if not ok or coroutine.status(co) == "dead" then return end
        C_Timer.After(delay or 0, step)
    end
    step()
end

RunAsync(function()
    for i = 1, 100 do
        DoExpensiveWork(i)
        coroutine.yield(0)     -- next frame
    end
    coroutine.yield(0.5)        -- pause half a second
    DoFinalWork()
end)
```

`coroutine.yield(n)` returns `n` to the scheduler, which schedules
the next `step` after that many seconds. `yield(0)` is the
"resume on the next frame" form (since `C_Timer.After(0, ...)`
fires on the next OnUpdate tick). `RunNextFrame(cb)` in
[`AddOns/!!!ClassicAPI/Util/FunctionUtil.lua`](../AddOns/!!!ClassicAPI/Util/FunctionUtil.lua)
wraps the same primitive for the non-coroutine case.

## Macros

Engine-level extensions to how macros are parsed and dispatched. These
don't add new Lua functions — they teach the engine to recognize input
forms it didn't accept in stock 1.12. Macro authors get them for free
once `ClassicAPI.dll` is loaded.

Vanilla 1.12 doesn't support `[target=...]`-style macro conditionals
natively; we don't add those. If you have a separate DLL/addon that
does (nampower's conditional macros, SuperWoWhook, etc.), the
extensions below compose with it — that layer strips the bracket
clause and forwards the cleaned tail to `CastSpellByName`, which then
flows through our additions.

### Numeric spellIDs in `/cast` and `CastSpellByName`

Pure-numeric input is now accepted anywhere a spell name would be:

```
/cast 5019                     -- in a macro line; casts Shoot if known
CastSpellByName("5019")        -- same effect from Lua / /run / chat
```

The spellID is resolved through `Spell.dbc` to the locale-resolved
name, then handed to the engine's existing name resolver — so all the
normal downstream behavior applies: the spellbook lookup gates on
"player knows this spell," action-bar UI sees the cast as if it had
been named, macros containing `/cast 5019` get tagged with the right
spellID in the engine's spell-state cache (so `IsCurrentAction(slot)`
and `IsAutoRepeatAction(slot)` work for the macro slot).

| Input        | Outcome                                                |
|--------------|--------------------------------------------------------|
| `/cast 5019` | Casts Shoot if you know it (any rank, highest known)   |
| `/cast 1234` | Falls through as "unknown spell," same as `/cast Foo`  |
| `/cast Shoot`| Unchanged from vanilla — names still work              |
| `/cast 5019(Rank 1)` | Falls through — rank suffix with a numeric stem isn't supported (use the name form if you need a specific rank) |

Implemented as a single hook on the engine's name → spellbook-slot
resolver at `0x004B3950` — covers `/cast`, `/castsequence` aliases,
`CastSpellByName`, the macro parser's tagging path, and anywhere
else the engine resolves a spell name. Numeric input with garbage
trailing characters falls through unchanged.

### `CastSpellNoToggle` as a macro cast line

Putting `CastSpellNoToggle("Shoot")` in a macro body now tags the
macro with Shoot's spellID the same way `/cast Shoot` or
`CastSpellByName("Shoot")` would. Without this, the macro casts
correctly but vanilla's macro parser doesn't know to associate the
slot with any spell — so action-bar UIs that call
`IsCurrentAction(slot)` / `IsAutoRepeatAction(slot)` (pfUI's
actionbar, ShaguTweaks, etc.) never light up the slot.

Recognized forms in a macro body:

```
CastSpellNoToggle("Shoot")        -- by name (string)
CastSpellNoToggle("Cat Form")     -- shapeshift / aspect / stance all tag the same way
CastSpellNoToggle(5019)           -- not yet — macro parser only recognizes the string form
```

The numeric form **works for the cast itself** (via the function's
runtime resolution), but the macro parser only scans for the string
form when tagging. To get the slot to highlight when using the
spellID, write `CastSpellNoToggle("Shoot")` instead.

The engine's first-match-wins rule still applies — if your macro is:

```
/cast Wand
CastSpellNoToggle("Shoot")
```

…the macro is tagged with Wand (the engine recognized `/cast Wand`
on line 1; our additional pattern is only consulted when the engine
didn't find any of its own patterns).

Macro tagging happens at macro edit/save time. Existing macros need
to be opened in the Macro UI and re-saved once after dropping in the
new DLL to pick up the new parser behavior.

### `GetMacroSpell(macroSlot)`

Returns `(name, rank, spellID)` for the spell a macro's first `/cast`
/ `/castsequence` / `CastSpellByName(...)` directive resolves to, or
nothing when the macro slot is empty, contains no cast directive, or
the directive's name doesn't resolve to a spell the player knows.

```lua
-- macro slot 1's body: "/cast Fireball(Rank 5)"
local name, rank, id = GetMacroSpell(1)
-- name = "Fireball", rank = "Rank 5", id = 25306

-- macro slot 2's body: "/cast Fireball"  (no explicit rank)
local name, rank, id = GetMacroSpell(2)
-- name = "Fireball", rank = "Rank 5", id = 25306  (the highest known rank)

-- macro slot 3's body: only /script lines or no cast
GetMacroSpell(3)
-- (no returns)
```

No body parsing happens at call time — the vanilla engine already
walks every macro body at create / edit / refresh and caches the
resolved spellID on the macro struct. We just read the cache and
look the name + rank up in `Spell.dbc`. Result: O(1) per call.

`CastSpellNoToggle("<name>")` macros are also recognized — the
parser hook from the [`CastSpellNoToggle`](#castspellnotoggle-as-a-macro-cast-line)
section tags them with the same spellID a `/cast` line would, so
`GetMacroSpell` sees them too.

> **Re-save existing macros once after dropping in the DLL.** The
> spellID cache is populated at edit time. Macros that existed
> before ClassicAPI loaded (or that use `CastSpellNoToggle` and were
> last edited under stock 1.12) will have a stale `0` cache —
> opening them in the Macro UI and clicking Okay re-runs the parser
> and the new behavior takes effect.

### `GetMacroIcons` / `GetMacroItemIcons` / `GetLooseMacroIcons` / `GetLooseMacroItemIcons`

Modern Classic Era's 4-function append-to-table icon enumeration
surface. Each takes a Lua table as its only argument and appends icon
basenames; returns nothing (mutation is the contract).

```lua
local spellList, itemList = {}, {}

-- Modern's canonical call order (mirrors IconDataProvider.lua):
GetLooseMacroIcons(spellList)
GetLooseMacroItemIcons(itemList)
GetMacroIcons(spellList)
GetMacroItemIcons(itemList)

-- spellList now has `Ability_*` / `Spell_*` basenames;
-- itemList now has `INV_*` basenames.
```

The split is `loose` (icons the user dropped into `Interface\Icons\`
on disk) vs `mpq` (icons baked into the game's MPQ archives), crossed
with `Spell` (basenames starting with `Ability_` / `Spell_`) vs
`Item` (basenames starting with `INV_`). Modern engines maintain four
parallel arrays in their loader; we replicate the scheme by hooking
vanilla's three scan callbacks
(`FUN_MACRO_ICON_CB_DISK` / `*_USER_MPQ` / `*_INSTALL_MPQ`) and tagging
each captured filename by source + prefix.

**Vanilla quirk worth noting**: the engine's main icon DB (what
`GetMacroIconInfo(i)` returns) is filtered down to `Ability_*` /
`Spell_*` only — `INV_*` filenames flow through the scan callbacks
(~2,500+ per session in the Octo client) but never land in the DB
because something downstream of the callbacks rejects them. We
capture before that rejection, so `GetMacroItemIcons` works even
though no engine-level `GetMacroItemIconInfo(i)` exists.

Each appended entry is the uppercase basename stripped of the
`Interface\Icons\` prefix and any `.blp`/`.tga` extension (e.g.
`"INV_SWORD_25"`, `"ABILITY_KICK"`). Callers concat the prefix
themselves before `texture:SetTexture(...)`. Output is alphabetically
sorted within each function (sorted lazily on first read).

Capture-time dedup ensures each unique basename only appears once
per source/prefix combination even though most files flow through
multiple scan callbacks. There is **no cross-dedup** between loose
and mpq, or between spell and item — matches modern engine
behavior (a file that exists both as a loose drop-in and inside an
MPQ will appear in both the loose and mpq lists).

The vanilla legacy globals `GetNumMacroIcons` / `GetMacroIconInfo`
remain available unchanged.

### `C_Macro.CreateMacro` / `C_Macro.EditMacro`

These functions create or edit a macro. You give the icon as a texture
string — a full `Interface\Icons\<name>` path or a bare `<name>`
basename — instead of a `GetMacroIconInfo` index.

```lua
C_Macro.CreateMacro(name, iconTexture, body, isCharacterMacro) -> index
C_Macro.EditMacro(index, name, iconTexture, body) -> index
```

Why this exists: the vanilla `CreateMacro` / `EditMacro` globals take an
icon index into the `GetMacroIconInfo` list. That list does not include
`INV_*` item icons. As a result, the stock API cannot set an item icon,
even though the engine stores and shows it correctly. These two functions
write the icon basename directly. Any texture under `Interface\Icons\`,
including an item icon, can be saved.

`CreateMacro(name, iconTexture, body, isCharacterMacro)`:
- `name` — the macro name. This argument is necessary.
- `iconTexture` — a string, for example `"INV_Sword_25"` or
  `"Interface\\Icons\\INV_Sword_25"`. The function removes the
  `Interface\Icons\` prefix, and adds it again when a caller reads the
  icon. A nil value sets no icon.
- `body` — the macro text. A nil value sets an empty body.
- `isCharacterMacro` — a true value puts the macro in the per-character
  tab. A false or nil value uses the general tab.
- The function returns the 1-based index of the new macro. It returns nil
  when the list is full (18 macros per tab).

`EditMacro(index, name, iconTexture, body)`:
- `index` — a 1-based slot number or a macro name.
- `name` / `iconTexture` / `body` — a nil argument keeps that field
  unchanged.
- The function returns the 1-based index of the macro. It returns nil when
  `index` does not match a macro that exists.

```lua
-- item icon that legacy CreateMacro cannot set:
local i = C_Macro.CreateMacro("Bag", "INV_Misc_Bag_08", "/say hi", false)
-- change only the icon; name and body stay:
C_Macro.EditMacro(i, nil, "INV_Potion_51", nil)
-- edit by name:
C_Macro.EditMacro("Bag", nil, nil, "/wave")
```

The `iconTexture` argument must be a string. `CreateMacro` reads a number
as no icon. `EditMacro` reads a number as no change. For the numeric-index
path, use the legacy globals.

The legacy `CreateMacro` / `EditMacro` globals do not change. They stay
index-only. String-icon callers use the `C_Macro` namespace instead.
Edits persist across sessions, the same as edits in the Macro UI.

## Mail

Backports of modern WoW's mail-attachment link getters. Both follow
the modern signature including the optional `attachmentIndex` arg
(added with TBC's multi-attachment mail), even though vanilla 1.12
only supports one attachment per message — pass-through compatibility
for addons originally written against later API versions. Any
`attachmentIndex` other than `1` is treated as no-op.

Both functions return `(link, itemID)` — modern returns just `link`,
but `itemID` is cheap to surface at the same lookup site and saves
callers a `string.match` on the link.

### `GetSendMailItemLink([attachmentIndex])`

Returns the **fully-decorated per-instance** hyperlink for the item
currently attached to the outgoing-mail slot, plus that item's
`itemID`. Same enchant / random-suffix / unique-ID decoration as
`GetContainerItemLink` would return for the same item back in the
player's bag — the attached item still exists as a live CGItem until
the mail is sent, so the link builder has full instance state to
draw from.

Returns nothing when:
- No item is attached.
- `attachmentIndex` is anything other than `nil` or `1`.
- The attachment's CGItem can't be resolved (shouldn't normally
  happen — engine writes the GUID on attach and clears it on detach).

```lua
local link, itemID = GetSendMailItemLink()
if link then
    ChatFrame1:AddMessage("Attached: " .. link .. " (id " .. itemID .. ")")
end
```

### `GetInboxItemLink(messageIndex[, attachmentIndex])`

Returns the **basic itemID-only** hyperlink (`|cff…|Hitem:N:0:0:0|h[Name]|h|r`)
for the item attached to inbox message `messageIndex` (1-based),
plus the `itemID`.

**Why basic-link instead of per-instance:** vanilla inbox entries do
store per-instance modifiers inline (enchant at `+0x12C`, suffix
factor at `+0x134`, random property at `+0x138` — the same fields
`GameTooltip:SetInboxItem` copies onto the tooltip to render a fully-
decorated preview). We intentionally ignore them in the link to match
modern 3.3.5's behavior — its `GetInboxItemLink` calls the
itemID-only link builder (`FUN_0061E290(itemID)`) and ignores the
per-instance fields. Per-instance data only fully manifests on the
client side once the player takes the item out of the mail and the
engine spawns a real CGItem; the inbox-entry fields are display-only
hints used by the tooltip builder.

Errors on missing or non-numeric `messageIndex`. Returns nothing when:
- `messageIndex` is out of range (`< 1` or `> GetInboxNumItems()`).
- The message slot is empty / has no attached item.
- `attachmentIndex` is anything other than `nil` or `1`.

```lua
for i = 1, GetInboxNumItems() do
    local link, itemID = GetInboxItemLink(i)
    if link then
        ChatFrame1:AddMessage(i .. ": " .. link .. " (id " .. itemID .. ")")
    end
end
```

## Map

### `C_Map.GetAreaInfo(areaID)`

Backport of the retail `C_Map.GetAreaInfo`. Returns the localized
`AreaTable.dbc` name for an area id — zones and subzones alike — or `nil`
for non-numeric / non-positive input or an id with no row. `AreaTable.dbc`
is a client DBC (always loaded, synchronous, localized), so the name is
available for any id with no async/cache step — unlike creature/item names.

```lua
C_Map.GetAreaInfo(12)   -- "Elwynn Forest"
C_Map.GetAreaInfo(9)    -- "Northshire Valley"
```

The name is in the client's active locale — the same source as
`GetRealZoneText` and the paperdoll zone name — so it matches what the
game displays everywhere.

### `C_Map.GetAreas()`

ClassicAPI extension. Returns `{ [areaID] = name, … }` — every
`AreaTable.dbc` row with a non-empty localized name, as an id→name map.
No retail equivalent (modern WoW enumerates the `uiMapID` tree, not raw
`AreaTable`); same "surface the hidden DBC" pattern as
[`C_Map.GetAreaTriggers`](#c_mapgetareatriggerinfotriggerid--c_mapgetareatriggersmapid).

The enumeration — not the per-id `GetAreaInfo` — is what name databases
need: name→id resolution, reverse search, and iterating the full id set.
Every key round-trips with `GetAreaInfo`; empty-name rows are omitted. The
map includes *every* named area (technical/internal ones too), so
consumers wanting only "real" zones should filter.

```lua
local areas = C_Map.GetAreas()
print(areas[12])                 -- "Elwynn Forest"

-- build a name -> id index
local byName = {}
for id, name in pairs(areas) do byName[name] = id end
```

Lets a name-DB addon drop its shipped zone-name tables (pfQuest's
`db/<locale>/zones.lua`): repoint `pfDB["zones"]["loc"] = C_Map.GetAreas()`
(merging so the fork's overrides still apply). Live client data in the
active locale — no locale/rename drift vs a scraped table.

### `C_Map.GetBestMapForUnit(unitToken)`

Returns the `AreaTable.dbc` area ID the given unit is currently in,
or `nil` if the unit isn't trackable. Vanilla 1.12 has no UiMap.db2
concept, so the closest equivalent to modern WoW's "best map" is the
zone-level AreaTable ID — exactly what the engine itself tracks for
the local player (`GetRealZoneText` reads it) and what gets broadcast
for party/raid members via `SMSG_PARTY_MEMBER_STATS_FULL`.

Coverage:

- `"player"` — always works.
- `"party1".."party4"` — works even for members in other zones / on
  other continents (slot-indexed GUID table stays populated regardless
  of local CGUnit availability).
- `"raid1".."raid40"` — same; covers the full raid roster.
- `"target"`, `"mouseover"`, `"focus"` and any other token that
  resolves to the player or a group member — works.
- NPCs, other players not in your group — `nil`.

```lua
/dump C_Map.GetBestMapForUnit("player")
-- 1519                                            (Stormwind City)
/dump C_Map.GetBestMapForUnit("party3")
-- 876                                             (party member on GM Island)
/dump C_Map.GetBestMapForUnit("target")
-- nil                                             (target is an NPC)
```

**The returned ID is a vanilla `AreaTable.dbc` area ID, not a modern
UI map ID.** Stormwind City in this backport is `1519`, not retail's
`84`. Addons that hardcode modern UI map IDs need a translation table
(or, simpler: compare against IDs this same function produces).

### `C_Map.GetMapAreaIDs()`

ClassicAPI extension. Returns `{ [mapName] = areaID, … }` — each
`WorldMapArea.dbc` map's name (the dir the engine uses for its map
textures) mapped to its `AreaTable` areaID.

Vanilla has no `GetCurrentMapAreaID`, and `GetMapZones()` enumerates only a
continent's *outdoor* zones — so a browsed **city or instance map**
(Orgrimmar, Undercity, Alterac Valley) can't be resolved to a zone id, which
means addons can't match zone-keyed content (a flight master, a POI) to it.
This maps those names to their zone ids:

```lua
-- resolve a browsed map to an areaID, cities included
local areaID = C_Map.GetMapAreaIDs()[GetMapInfo()]
```

> The key is the `WorldMapArea` name, which matches `GetMapInfo()`'s returned
> dir for the vast majority of maps — but a few Blizzard map folders are
> spelled differently from the area name (the Orgrimmar map's folder is
> `"Ogrimmar"`), so `GetMapAreaIDs()[GetMapInfo()]` can miss for those. For
> those, resolve by zone name instead (`GetMapZones` → name → areaID).

Continent-spanning rows (areaID 0) and unnamed rows are omitted. Pairs with
[`C_Map.GetAreas`](#c_mapgetareas) (areaID → name) and
[`C_Map.GetMapWorldSize`](#c_mapgetmapworldsizeareaid) (areaID → yards).

### `C_Map.GetMapOverlays([areaID])`

ClassicAPI extension. Returns every `WorldMapOverlay.dbc` entry for a
zone — **explored or not** — with the tile grid fully resolved. The
engine's own `GetNumMapOverlays` / `GetMapOverlayInfo` only expose
overlays the character has discovered (the exploration gate is a
server-fed list), which is why map-reveal addons traditionally ship
hand-measured overlay tables; this replaces those with a live DBC
read that works for every zone on any client.

With no argument, returns the overlays of the world map's currently
**viewed** zone (the same continent/zone selection `GetMapInfo`
resolves — so it drops straight into a `WorldMapFrame_Update` hook).
With a numeric `areaID` (`AreaTable.dbc` id, the same identity
`C_Map.GetBestMapForUnit` returns), returns that zone's overlays from
anywhere.

Each overlay table:

| field | meaning |
|---|---|
| `textureName` | bare DBC name (`"GOLDSHIRE"`) |
| `texturePath` | `Interface\WorldMap\<dir>\GOLDSHIRE` (engine format) |
| `areaID` | primary `AreaTable` id the overlay reveals (first nonzero) |
| `areaIDs` | array of all `AreaTable` ids it reveals (usually one; 52 of 707 overlays span 2–3 subzones) |
| `textureWidth` / `textureHeight` | the DBC placement rect |
| `offsetX` / `offsetY` | placement on the zone canvas |
| `mapPointX` / `mapPointY` | DBC map-point fields |
| `hitRectTop` / `hitRectLeft` / `hitRectBottom` / `hitRectRight` | hit rectangle in world-map canvas px (≈1002×668); the tight clickable bounds of the landmass |
| `tileCols` / `tileRows` / `upscaled` | the **resolved** tile grid |
| `tiles` | ready-to-draw tile array (below) |
| `fileDataIDs` | ordered tile texture paths (retail's field name; redundant with `tiles[].file`) |

The `hitRect*` fields are the source addons use to place a **subzone
center** (the texture rect includes transparent padding, so its center is
off; the hit rect is the tight landmass bounds). Normalize against the
≈1002×668 world-map canvas:

```lua
local cx = (ov.hitRectLeft + ov.hitRectRight) / 2 / 1002 * 100  -- center X %
local cy = (ov.hitRectTop  + ov.hitRectBottom) / 2 /  668 * 100  -- center Y %
local w  = (ov.hitRectRight  - ov.hitRectLeft) / 1002 * 100      -- width  %
local h  = (ov.hitRectBottom - ov.hitRectTop)  /  668 * 100      -- height %
```

Overlays with `areaID > 0` cover the [subzone → parent-zone + center/size]
mapping that map addons hand-scrape (pfQuest's `pfDB["zones"]["data"]`).
Reading it live matches *this* client's maps — on a modified client
(Turtle) those differ from the scraped vanilla numbers, which is the point.

A row may be **texture-less** — a clickable subzone region with no distinct
art. It still appears, with `textureName == ""`, zero `textureWidth`/
`textureHeight`, and an empty `tiles` table (so `ipairs(ov.tiles)` iterates
zero times), carrying its `areaID`/`areaIDs` + `hitRect*`. Filter art
consumers on `ov.textureName ~= ""`. (Stock 1.12 and Octo happen to give
every overlay a texture, but the schema permits texture-less rows.)

Each `tiles` entry: `file` (texture path including the tile number,
`SetTexture`-ready), `width` / `height` (draw size in map pixels),
`texCoordX` / `texCoordY` (right/bottom texcoords), `offsetX` /
`offsetY` (absolute canvas position). Drawing an overlay is a dumb
loop:

```lua
for _, ov in ipairs(C_Map.GetMapOverlays()) do
    for _, t in ipairs(ov.tiles) do
        local tex = pool:Acquire()
        tex:SetTexture(t.file)
        tex:SetWidth(t.width)  tex:SetHeight(t.height)
        tex:SetTexCoord(0, t.texCoordX, 0, t.texCoordY)
        tex:SetPoint("TOPLEFT", WorldMapDetailFrame, "TOPLEFT", t.offsetX, -t.offsetY)
        tex:Show()
    end
end
```

> **Why `tiles` exists (don't derive the grid yourself).** The
> canonical layout is `ceil(w/256) × ceil(h/256)` tiles numbered
> row-major, and on stock 1.12 data that's always right — but Octo's
> shipped data breaks it in three distinct ways: DBC rects that round
> away a narrow sliver column (Icepoint's Kaneq'nuun has a real 8px
> third column; trusting `ceil` shears the whole overlay into
> misplaced strips), unrelated foreign tiles appended to an overlay's
> number sequence (drawing by file count scatters duplicate landmass),
> and same-rect re-exports at higher resolution (Stonetalon's
> Windshear Crag: 4 real tiles for a 1-tile rect, all of which must be
> kept and scaled). ClassicAPI disambiguates per overlay from the
> actual BLP header dimensions via the engine's own VFS (a sweep of
> all 707 overlays found 108 with such art/DBC mismatches), caches the
> result for the session, and hands back tiles that are simply
> correct. `textureWidth`/`textureHeight` remain the raw DBC rect —
> reliable for placement; it's only the tile count they imply that
> lies.

### `C_Map.GetMapWorldSize([areaID])`

Returns a zone's world size in yards as `width, height` — the physical extent the
map represents, read straight from the `WorldMapArea.dbc` placement rect.
Addons use it to turn map-relative percents into yard distances (range
rings, "N yards away" readouts, proximity checks).

Retail takes a `uiMapID`; here it's an `areaID` (`AreaTable.dbc` id — the
same identity
[`C_Map.GetBestMapForUnit`](#c_mapgetbestmapforunitunittoken) returns and
[`C_Map.GetMapOverlays`](#c_mapgetmapoverlaysareaid) accepts), returns that
zone's size; with no argument, the world map's currently **viewed** zone
(same resolution `GetMapOverlays()` uses with no arg). Returns `nil, nil`
when the zone can't be resolved.

`width` is the east-west span (world Y), `height` the north-south span
(world X) — the same denominators behind the
[`C_Map.GetAreaTriggerInfo`](#c_mapgetareatriggerinfotriggerid--c_mapgetareatriggersmapid)
map-percent transform, so `mapX/100 * width` and `mapY/100 * height` give a
point's offset in yards within the zone.

```lua
local w, h = C_Map.GetMapWorldSize(85)   -- Tirisfal: ~4518.7, 3012.5
```

### `C_Map.GetAreaTriggerInfo(triggerID)` / `C_Map.GetAreaTriggers([mapID])`

ClassicAPI extension. Exposes `AreaTrigger.dbc` — the client's 517
static trigger volumes (subzone-entry, exploration, and teleport
triggers). Vanilla loads the DBC but exposes **none** of its geometry
to Lua, which is why map addons (pfQuest et al.) ship hand-scraped
trigger tables; this is a live DBC read, same "surface the hidden data"
pattern as [`C_Map.GetMapOverlays`](#c_mapgetmapoverlaysareaid).

- `GetAreaTriggerInfo(triggerID)` — one trigger's info table, or `nil`
  for a missing / out-of-range id.
- `GetAreaTriggers([mapID])` — an array of every trigger's info table,
  optionally filtered to a single `Map.dbc` id (`0` Eastern Kingdoms,
  `1` Kalimdor, `30` Alterac Valley, `489` Warsong Gulch, …).

Each info table carries both the **authoritative raw geometry** and a
**derived, ready-to-draw form**:

| field | meaning |
|---|---|
| `id` | `AreaTrigger.dbc` row id |
| `mapID` | `Map.dbc` id (continent, or an instance map) |
| `x` / `y` / `z` | continent-space **world** coordinates of the center |
| `radius` | sphere-trigger radius (`0` for a box trigger) |
| `isBox` | `true` when the trigger is an oriented box |
| `boxLength` / `boxWidth` / `boxHeight` / `boxYaw` | box dims + yaw (all `0` for a sphere) |
| `areaID` | `AreaTable` zone the point falls in — **absent** if unresolved |
| `mapX` / `mapY` | position as a `0..100` zone-relative percent — **absent** alongside `areaID` |

The raw `x`/`y`/`z` come straight from the DBC. `areaID`/`mapX`/`mapY`
are derived from the `WorldMapArea.dbc` zone rect (WoW's world axes:
`+X` north, `+Y` west). `mapX` is the **horizontal** map axis (off
world Y), `mapY` the **vertical** (off world X) — the standard WoW /
pfQuest / Astrolabe convention:

```
mapX% = (locLeft - y) / (locLeft - locRight)  * 100   -- horizontal
mapY% = (locTop  - x) / (locTop  - locBottom) * 100   -- vertical
```

The zone is chosen by which one's **drawn landmass** the point sits on:
`WorldMapArea` rects are loose overlapping boxes, so a point near a
border falls in several; the `WorldMapOverlay` hit rects trace the real
landmass, and the zone the point is deepest inside an overlay of wins
(the runtime stand-in for an ADT area lookup). When the point is on no
zone's overlay it falls back to the most-interior containing rect; when
no rect contains it at all (open sea, an instance with no `WorldMapArea`
row, a degenerate rect), the three derived fields are simply omitted —
the raw world coords are
always present.

```lua
-- One trigger, its position as a world-map percent point:
local t = C_Map.GetAreaTriggerInfo(2)
if t.areaID then
    print(t.areaID, t.mapX, t.mapY)  -- 85  21.89  67.87  (Tirisfal)
end

-- Every trigger in Warsong Gulch:
for _, t in ipairs(C_Map.GetAreaTriggers(489)) do
    -- t.x/y/z world coords, t.radius or t.isBox geometry, t.mapX/mapY
end
```

> **Matches the pfQuest-turtle trigger table 1:1.** Keyed by trigger
> id, the resolved `areaID` + `mapX`/`mapY` line up with
> `pfQuest-turtle`'s scraped `areatrigger-turtle.lua` (id 2 →
> `{21.89, 67.87, 85}` Tirisfal; id 45 → `{68, 17, 796}`). One
> difference in kind: a scraped table may list a trigger under several
> maps at once (a subzone *and* its parent continent); this resolves to
> the single zone whose **drawn landmass** the point sits on (via the
> WorldMapOverlay hit rects — the runtime stand-in for an ADT area
> lookup). An addon can drop `GetAreaTriggers()` straight in place of a
> shipped table.

Backports the six `C_MerchantFrame.*` calls retail addons use when
interacting with a vendor. All entry points read the engine's
merchant/buyback storage directly — no Lua-roundtrip through
`GetMerchantItemInfo` / `GetBuybackItemLink` etc. — and the
`SellAllJunkItems` dispatcher bypasses `UseContainerItem` entirely
by calling the engine's internal `MerchantSellItem` packet builder.

A "merchant frame is currently open" gate (`VAR_MERCHANT_NPC_GUID_*`
non-zero) applies to every function — the same gate retail enforces.
`GetNumJunkItems` returns `0` away from a vendor and the sell
functions are no-ops; the per-slot getters return `nil`.

### `C_MerchantFrame.GetItemInfo(slot)`

Returns a table describing the merchant's item at the given 1-based
slot, or `nil` if no merchant is open / the slot is out of range.

Table fields:

| Field             | Type    | Notes |
|-------------------|---------|-------|
| `itemID`          | number  | Item record key, suitable for `C_Item.GetItemInfoInstant` / cache lookups. |
| `price`           | number  | Copper cost per stack (multiply by `stackCount` for unit price). |
| `stackCount`      | number  | Units delivered per purchase (1 for most equipment, e.g. 5 for stacks of cloth). |
| `numAvailable`    | number  | Limited-supply count, or `-1` for unlimited stock. |
| `isPurchasable`   | boolean | Always `true` in this build — vanilla has no "blocked from buying" flag. |
| `isThrottled`     | boolean | Always `false` — modern's anti-spam concept doesn't apply in vanilla. |
| `hasExtendedCost` | boolean | Always `false` — currency/honor-cost merchants don't exist in vanilla. |

```lua
local info = C_MerchantFrame.GetItemInfo(1)
if info then
    print(info.itemID, info.price, info.stackCount)
end
```

Reads directly from the 28-byte merchant entry at
`VAR_MERCHANT_ITEMS + (slot-1) * MERCHANT_STRIDE` (the same flat
array `Script_GetMerchantItemInfo` walks). Compared to the existing
[`GetMerchantItemID`](#getitemid--companions-to-the-engines-getitemlink-family),
this returns the wider modern struct shape in one call.

### `C_MerchantFrame.GetBuybackItemID(slot)`

Returns the itemID of the merchant's buyback slot at the given
1-based index (1..12), or `nil` if the slot is empty / no merchant
is open.

```lua
for slot = 1, 12 do
    local id = C_MerchantFrame.GetBuybackItemID(slot)
    if id then
        -- buybackable item at slot
    end
end
```

Resolution chain (engine-direct, no Lua-side `GetBuybackItemLink`
roundtrip):

1. Read the buyback slot's stored invMgr index from
   `VAR_BUYBACK_SLOTS + (slot-1)*4`.
2. Look up that index in the player invMgr's flat GUID array (the
   engine keeps sold items alive in the player's inventory storage,
   not in a separate buyback pool).
3. Resolve the GUID to a `CGItem*` via
   `Item::Location::ResolveByGUID` — same engine helper
   `C_EquipmentSet.GetItemLocations` uses.
4. Read the itemID from the CGItem's instance block at `+0x08 + 0x0C`.

### `C_MerchantFrame.GetNumJunkItems()`

Returns the count of grey-quality (`LE_ITEM_QUALITY_POOR`) items in
the player's bags 0..4 that `SellAllJunkItems` would sell. Returns
`0` when no merchant frame is open — matching retail's behavior of
gating the count on merchant context, since the count is meant as
a "what would the sell-junk button do right now" signal rather than
a passive inventory query.

```lua
-- Inside a MERCHANT_SHOW handler:
local junk = C_MerchantFrame.GetNumJunkItems()
if junk > 0 then
    print("Selling " .. junk .. " junk item(s)")
    C_MerchantFrame.SellAllJunkItems()
end
```

Quality is read from each item's `ItemStats` cache record at the
`m_quality` field (`OFF_ITEMSTATS_QUALITY = 0x1C`). Items with
unloaded cache records (rare for items in your own bags) are
skipped — same conservative behavior the sell path uses.

### `C_MerchantFrame.SellAllJunkItems()`

Sells every quality-0 item in the player's bags to the open
merchant. No-op when no merchant frame is open.

Sells are dispatched **one per frame** via the shared
`WorldTick` subscriber, not in a tight loop within the call —
vanilla's network path drops packets when CMSG_SELL_ITEM is
flooded (a 10-item burst consistently lost the 2nd-to-last sell
in testing). Calling pace matches click-by-click selling. For
10 junk items, expect the queue to drain in ~10 frames (~150ms
at 60fps).

```lua
C_MerchantFrame.SellAllJunkItems()
```

If the player closes the merchant frame or opens a different
vendor mid-drain, the remaining queue is discarded rather than
mis-routed to the new merchant.

Each sell is delivered via the engine's internal
`MerchantSellItem` helper (`FUN_MERCHANT_SELL_ITEM`, opcode
`CMSG_SELL_ITEM`/`0x1A0`) called with `count = 0` ("sell whole
stack"). Bypasses `Script_UseContainerItem` entirely — addons
hooking `UseContainerItem` will not see these sells, and there's
no risk of the use-not-sell dispatch branch firing if the merchant
state changes mid-loop.

### `C_MerchantFrame.IsMerchantItemRefundable(slot)`

Always returns `false`. Vanilla 1.12 has no refund mechanic
(retail's 2-hour buy-back-for-full-price system was introduced
post-vanilla); the function exists for API parity so retail
addons that gate behavior on refundability don't break.

```lua
if not C_MerchantFrame.IsMerchantItemRefundable(slot) then
    -- always taken in this build
end
```

### `C_MerchantFrame.IsSellAllJunkEnabled()`

Always returns `true`. Retail exposes an optional client setting to
disable the sell-all-junk button; vanilla has no such setting, so
the feature is always on. Function exists so retail addons that
gate `SellAllJunkItems` on this don't no-op silently.

## MapExplorationInfo

`C_MapExplorationInfo.GetExploredMapTextures` (retail backport) and
`GetUnexploredMapTextures` (ClassicAPI extension) are the
exploration-filtered views of a zone's `WorldMapOverlay.dbc` overlays.
Together with [`C_Map.GetMapOverlays`](#c_mapgetmapoverlaysareaid) (which
returns **all** overlays) they form the three-way split:

| function | returns |
|---|---|
| `C_MapExplorationInfo.GetExploredMapTextures(areaID)` | only overlays the player has **discovered** |
| `C_MapExplorationInfo.GetUnexploredMapTextures(areaID)` | only overlays **not** yet discovered |
| `C_Map.GetMapOverlays(areaID)` | **all** overlays, explored or not |

All three return the identical per-overlay table shape (see
`GetMapOverlays`); these two just filter it. "Explored" is computed
per-areaID from the player's explored-areas bitfield (the same source the
engine's own `GetNumMapOverlays`/`GetMapOverlayInfo` gate on), so the
`areaID` argument works for any zone — not only the one currently on the
world map. With no argument, both default to the current map view.

### `C_MapExplorationInfo.GetExploredMapTextures([areaID])`

Backport of the retail call (retail added it in 8.0). Returns an array of
the overlay tables for overlays the local player **has discovered** in the
zone — same shape as [`C_Map.GetMapOverlays`](#c_mapgetmapoverlaysareaid),
including `fileDataIDs` (retail's field name for the tile list; here it
holds `SetTexture`-ready texture *paths*, since vanilla has no numeric
fileDataIDs). Empty before the player is resident (character select).

```lua
for _, tex in ipairs(C_MapExplorationInfo.GetExploredMapTextures(12)) do
    -- tex.fileDataIDs[i] -> SetTexture path; tex.offsetX/offsetY placement
end
```

### `C_MapExplorationInfo.GetUnexploredMapTextures([areaID])`

ClassicAPI extension — the complement: overlays the player has **not**
discovered, the pieces a map-reveal addon draws over the fogged base map.
No retail equivalent (retail ships only the explored getter; its inverse,
`C_Map.GetMapOverlays`, returns *everything*). Same table shape and
`areaID` semantics as the explored getter.

## ChatBubbles

### `C_ChatBubbles.GetAllChatBubbles([includeForbidden])`

Returns a 1-based table of the currently-active chat-bubble `Frame`
objects (the speech bubbles above characters who `/say` or `/yell`).
Modern WoW added this in 7.2.5 to replace the old "iterate over
`WorldFrame` children and guess which are bubbles" idiom — we hand you
the exact set instead.

```lua
for _, bubble in ipairs(C_ChatBubbles.GetAllChatBubbles()) do
    -- The spoken text lives in the bubble's FontString region.
    for _, region in ipairs({bubble:GetRegions()}) do
        if region.GetText and region:GetText() then
            print(region:GetText())
        end
    end
end
```

Vanilla 1.12 chat bubbles are `CGChatBubbleFrame`s — full frames the
engine creates in C++ without ever calling `CreateFrame` (same as
default nameplates). The returned frames are the engine's **canonical
wrappers**: real, method-capable (`:GetRegions()`, `:IsShown()`,
`:GetPoint()`, …), and identical to the object any other addon that
touches the bubble sees, so decorations survive. The spoken-text
FontString is parented to the bubble, so it shows up in
`:GetRegions()`.

`includeForbidden` is accepted for signature parity and ignored —
vanilla has no forbidden frames, so every active bubble is returned
regardless. Returns an empty table when no bubbles are showing. A
bubble whose owner just despawned can linger for one frame before the
engine prunes it; filter on `bubble:IsShown()` if that matters.

## NamePlate

Modern `C_NamePlate.*` returns nameplate `Frame` objects keyed off
unit data. Vanilla 1.12 doesn't ship the API at all — but the
underlying data (per-unit nameplate pointer at `CGUnit + 0xE60`)
exists. We enumerate visible units via the local-player-anchored
object hash table, filter by `TYPEMASK_UNIT`, and return matches.

Modern's `"nameplateN"` unit-token family is also supported — see
[Unit tokens](#unit-tokens-nameplaten) below. `NAME_PLATE_UNIT_ADDED`
/ `_REMOVED` / `_CREATED` events fire via a per-tick visible-plate
diff (see [the events section](#name_plate_created--name_plate_unit_added--name_plate_unit_removed-events)).

### `C_NamePlate.GetNamePlates()`

Returns a 1-based table of nameplate `Frame` objects — one per
CGUnit that currently has an allocated **Lua-registered** nameplate.
The frames are real Lua tables with methods (`:GetName()`,
`:GetWidth()`, `:SetAlpha()`, etc.) and any addon-added decorations
on them.

```lua
local plates = C_NamePlate.GetNamePlates()
for i, plate in ipairs(plates) do
    print(i, plate:GetName(), plate:GetWidth())
end
```

Two kinds of plates can show up:

- **Addon-created plates** (pfUI, TidyPlates, NamePlateMod, etc.):
  registered with Lua via `CreateFrame`, so each has a real
  registry ref. We push `registry[plate + 0x08]`. Identity is
  stable across calls — caching is safe while the frame is alive.

- **Default vanilla plates**: created internally by the engine
  without ever calling `CreateFrame`. Their `+0x08` field holds the
  sentinel `LUA_NOREF` (`-2`), not a real registry key. We build a
  fresh wrapper table per call (`{[0] = lightuserdata(plate)}` with
  the global `__framescript_meta` metatable) so addons get the
  same method surface. The wrapper isn't cached engine-side, so
  identity isn't stable across calls — don't compare wrappers, and
  don't store them across the unit going out of range (the
  underlying frame may be freed). Call `GetNamePlates()` fresh
  each time you need plates.

### Reading region content from a default nameplate

Vanilla plates have six child regions in stable positions. Walk them
with `:GetRegions()`:

```lua
local plates = C_NamePlate.GetNamePlates()
for _, plate in ipairs(plates) do
    local regions = {plate:GetRegions()}
    local name  = regions[3]:GetText()              -- e.g. "Joseph Dalton"
    local level = tonumber(regions[4]:GetText())    -- e.g. 60
    -- regions[1], [2], [5], [6] are textures (border, healthbar,
    -- glow, raid-icon — order depends on the engine's draw order)
end
```

Lua 5.0 has no `select()`, so collect into a table via
`{plate:GetRegions()}` and index. Addon-created plates have
different region layouts — those frames inherit whatever shape the
addon built, not this one.

### `C_NamePlate.GetNamePlateForUnit(unitToken)`

Returns the nameplate Frame for a single unit (resolved via the
engine's token-to-GUID path, so out-of-range party/raid members
work too), or `nil` if the unit has no allocated nameplate.

```lua
local plate = C_NamePlate.GetNamePlateForUnit("target")
if plate then
    local regions = {plate:GetRegions()}
    print("targeting:", regions[3]:GetText())   -- e.g. "Santora"
end
```

Same registered-vs-fresh-wrapper behavior as `GetNamePlates()` —
addon-created plates return their cached wrapper, default vanilla
plates get a fresh per-call wrapper. Don't cache the result across
the unit going out of range.

### `C_NamePlate.GetNamePlateForGUID(guidString)`

Same as `GetNamePlateForUnit` but takes the `"0xHHHHHHHHHHHHHHHH"`
GUID-string form. Useful when you've stored a unit GUID across
events (e.g., converted the positional `"nameplateN"` token to a
GUID via `UnitGUID(arg1)` at `NAME_PLATE_UNIT_ADDED` time) and need
the frame later.

```lua
local platesByGuid = {}

local f = CreateFrame("Frame")
f:RegisterEvent("NAME_PLATE_UNIT_ADDED")
f:SetScript("OnEvent", function()
    -- arg1 = "nameplateN" token; convert to stable GUID for storage
    local guid = UnitGUID(arg1)
    platesByGuid[guid] = C_NamePlate.GetNamePlateForGUID(guid)
end)
```

Returns `nil` if the GUID doesn't parse, doesn't resolve to a
visible CGUnit, or the unit has no allocated nameplate.

### `C_NamePlate.GetNamePlateGUIDs()`

Returns a 1-based table of GUID strings (modern
`"0xHHHHHHHHHHHHHHHH"` format) — one per CGUnit with an allocated
nameplate, **regardless** of whether the frame has been registered
with Lua. Catches default vanilla nameplates that
[`GetNamePlates`](#c_nameplategetnameplates) can't surface as
frames.

```lua
/dump C_NamePlate.GetNamePlateGUIDs()
-- { "0xF13000C36C26FD02", "0xF130000009276912", ... }
```

Walks the local-player-anchored object hash table for `TYPEMASK_UNIT`
entries, filters by `*(unit + 0xE60) != nullptr`. The per-unit
nameplate pointer is set by `FUN_006086E0`'s "show nameplate" path
regardless of which nameplate system rendered it. Order follows
hash-bucket iteration and isn't stable across calls.

### Unit tokens (`nameplateN`)

`"nameplate1"`, `"nameplate2"`, … work as unit tokens against every
`UnitX` function: `UnitName`, `UnitGUID`, `UnitClass`, `UnitHealth`,
`UnitHealthMax`, `UnitLevel`, `UnitFaction`, `UnitReaction`,
`UnitExists`, `UnitIsPlayer`, `UnitIsEnemy`, `UnitIsDead`, etc. —
~30 functions for free.

```lua
for i = 1, 40 do
    if not UnitExists("nameplate" .. i) then break end
    print(i, UnitName("nameplate" .. i), UnitClass("nameplate" .. i))
end
```

Indices are **assigned slots**, matching modern WoW exactly: a plate keeps
its slot for its entire lifetime, so surviving plates are **never
renumbered** when another plate is removed. A removed plate frees its slot,
and the next new plate reuses the lowest free slot. So a middle plate
vanishing leaves the others' `nameplateN` tokens unchanged (that slot simply
becomes vacant until a new plate reuses it) — `UnitExists("nameplateN")`
returns `false` for a currently-free slot. Stable for the lifetime of a
single plate; no reordering, ever.

Token chains work too — `"nameplate1target"`, `"nameplate1targettarget"`,
etc. — by mirroring the engine's own `targettarget`-style suffix
walker (read `UNIT_FIELD_TARGET` off `m_objectFields`, loop). Other
suffixes (`pet`, `master`) aren't supported by the vanilla engine's
own walker either, so they don't compose.

Out-of-range indices return `nil` cleanly without raising "Unknown
unit name" — `UnitExists("nameplate99")` just returns `false`.

**Unit events fire for `"nameplateN"`.** Like party/raid tokens,
`UNIT_HEALTH` / `UNIT_AURA` / `UNIT_LEVEL` / … fire with
`arg1 == "nameplateN"` when a nameplated unit's descriptor field changes
(vanilla only watched its own target/party/raid units). Backed by
`Unit::TokenObserver` — observers are registered per plate on
`NAME_PLATE_UNIT_ADDED` and torn down on `_REMOVED`; the changed GUID is
resolved to its *current* index at fire time, since indices shift as
plates vanish. Note the per-plate observer cost in very large scenes.

**Implementation note.** We hook `FUN_TOKEN_TO_GUID` (the central
token→GUID resolver) so the entire `Script_Unit*` surface gains the
new token form transparently. The hook is gated by an `SStrCmpI`
prefix check against `"nameplate"`; non-nameplate tokens
(`"player"`, `"target"`, `"partyN"`, etc.) fall straight through to
the unmodified resolver. The ordered list is maintained alongside
the existing `NAME_PLATE_UNIT_ADDED` / `_REMOVED` diff in the
per-tick nameplate walker.

### Unit tokens (`markN`)

`"mark1"` … `"mark8"` work as unit tokens against every `UnitX`
function, resolving to whichever unit currently wears that raid-target
marker (`mark1` = star, `mark2` = circle, … `mark8` = skull — the same
1–8 order as `SetRaidTarget` / `GetRaidTargetIndex`):

```lua
if UnitExists("mark8") then                       -- something is skull-marked
    print(UnitName("mark8"), UnitHealth("mark8"))
end
```

`UnitExists("markN")` returns `false` when that marker is unset, when the
marked unit is out of range / not synced, or when the marker is on a
non-unit (a marked corpse, gameobject, or loot). Token chains compose —
`"mark1target"`, `"mark1targettarget"` — via the same suffix walker as the
other families.

**Unit events fire for `"markN"`.** `UNIT_HEALTH` / `UNIT_MANA` /
`UNIT_AURA` / `UNIT_LEVEL` / … fire with `arg1 == "markN"` when the marked
unit's descriptor field changes — vanilla only watched its own
target/party/raid units, so a marked mob that isn't otherwise one of those
fired nothing:

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("UNIT_HEALTH")
f:SetScript("OnEvent", function()
    if arg1 == "mark8" then
        -- skull's health changed; UnitHealth("mark8") is fresh here
    end
end)
```

Backed by `Unit::TokenObserver` (the same mechanism as `focus` /
`nameplateN`). A per-frame watcher diffs the engine's 8-slot marker table
and attaches/detaches an observer per marked *unit* — deferring until the
unit is in range, since observers can only bind to a live object and a
marked unit may be marked while out of range and enter later. The idle
case (no markers set) costs a handful of comparisons per frame and no
object lookups. A marker on a non-unit is simply never observed.

### Unit tokens (GUID literals)

A raw 64-bit GUID literal — `"0x"` followed by up to 16 hex digits
(e.g. `"0xF130000000000A5"`) — works as a unit token against the same
`UnitX` surface, so a GUID can be passed anywhere a token is expected:

```lua
local guid = UnitGUID("target")        -- e.g. "0xF13000..."
print(UnitName(guid), UnitHealth(guid), UnitClass(guid))
```

Hex parsing is case-insensitive; the parsed GUID is handed to the engine's
own object-manager lookup (the identical path `"player"` takes), so a GUID
for a unit that isn't currently loaded (out of range, not synced) resolves
to `nil` exactly like any absent unit — no error. Suffix chains compose:
`"0x…target"` walks to the referenced unit's target.

This is the **input** direction only — to obtain a token's GUID use
[`UnitGUID`](#unitguidunit).

**Compatible with SuperWoW.** SuperWoW provides the same GUID-input support,
so when it's loaded we detect it and defer to its resolver (no double
handling); when it isn't, this fills the gap. Either way GUID-token input
behaves identically, so addons needn't care whether SuperWoW is present.

## NameCache

GUID-keyed cache of player names and classes. The engine itself
maintains an in-memory `NameCache` at `0x00C0E228`, populated by
`SMSG_NAME_QUERY_RESPONSE` — but vanilla doesn't expose it to Lua,
and it doesn't survive `/reload`. This module surfaces it as
`GetPlayerInfoByGUID`, adds an opt-in **persistent** layer that
survives `/reload` and full client restarts, and (separately
toggleable) sweeps the engine's visible-object list to populate
entries the SMSG path would never reach on its own.

Two toggles, each independent:

- **`C_PlayerCache.SetEnabled`** — turn on the on-disk cache.
  Off by default. Without this, the module is read-only against
  the engine's transient in-memory cache.
- **`C_PlayerCache.SetScanEnabled`** — turn on the visible-object
  sweep. Off by default. Requires the on-disk cache to be enabled
  to have any effect.

Three population sources feed the cache when fully enabled:

1. **`SMSG_NAME_QUERY_RESPONSE` hook** (always active when the
   on-disk cache is on). Every name-query response the engine
   processes is mirrored to disk. Covers chat, group/raid sync,
   guild updates, visible-object resolution — anything the engine
   itself issues a name query for.
2. **`C_PlayerCache.RememberPlayer`** (always available, no-op
   when the cache is off). Lets addons feed in sources the engine
   never sees: `/who` results, etc.
3. **Visible-object sweep** (active when both toggles are on).
   Walks the engine's live visible-object list every ~10 seconds
   on the `Frame::RegisterEvent` hook, resolving each player and
   feeding name + class. Picks up nearby players whether or not
   the engine ever queried them.

Storage:

- `WTF\Account\<account>\ClassicAPI.txt` — account-level settings
  (`PersistentNameCacheEnabled`, `NameCacheScanEnabled`). The
  opt-in toggles stay per-account.
- `WTF\ClassicAPI\<realmList>\<realm>\ClassicAPI_NameCache.txt` —
  per-realm cache, **shared across every account on the same
  server**. Keyed by the realmlist address (unambiguous server
  identity, can't collide across unrelated private servers) plus the
  realm name — so alts on different accounts pool into one file
  rather than each rebuilding their own. Tab-separated text,
  ~30 bytes/entry, hand-editable. On first load with no shared file
  yet, a pre-existing per-account cache
  (`WTF\Account\<account>\<realm>\ClassicAPI_NameCache.txt`) is
  migrated in automatically.

### `GetPlayerInfoByGUID(guid)`

Returns
`localizedClass, englishClass, localizedRace, englishRace, sex, name, realm`
for a player GUID the engine has cached, or `nil` on a cache miss.

```lua
local _, class, _, race, sex, name = GetPlayerInfoByGUID(UnitGUID("target"))
-- e.g. "WARRIOR", "NightElf", 2, "Sylphir"

GetPlayerInfoByGUID("0x0000000000000777")  -- same shape for a literal GUID
```

`guid` is the `"0xHHHHHHHHLLLLLLLL"` string returned by `UnitGUID`.
Bare 8-hex `"0xLLLLLLLL"` (hi-dword zero) is also accepted.

Returns:

| 1 `localizedClass` | `"Warrior"` / `"Krieger"` etc. — from `ChrClasses.dbc` indexed by locale. |
| 2 `englishClass`   | `"WARRIOR"` (uppercase tag, same value `UnitClass` returns as 2nd return on modern clients) — `ChrClasses.dbc` filename field. |
| 3 `localizedRace`  | `"Human"` / `"Mensch"` / `"Humain"` etc. — from `ChrRaces.dbc` indexed by locale. |
| 4 `englishRace`    | `"Human"`, `"Orc"`, `"Dwarf"`, `"NightElf"`, `"Scourge"` (vanilla's filename for what addons call Undead), `"Tauren"`, `"Gnome"`, `"Troll"` — from `ChrRaces.dbc` filename field. |
| 5 `sex`            | `2` = male, `3` = female. Matches `UnitSex` convention (the cache stores `0`/`1`; we add `2`). |
| 6 `name`           | Character name. |
| 7 `realm`          | Realm name. Single-realm in vanilla, so usually the local realm. |

**Cache coverage**: the engine populates entries from
`SMSG_NAME_QUERY_RESPONSE`. Anything the client has already seen
populates the cache: chat (whispers, says, party chat), raid/group
events, guild updates, visible objects (target, party, raid in
zone). Names of offline friends never seen in chat are *not*
cached — `GetPlayerInfoByGUID` returns `nil` for them until the
client does something that triggers a name query. This module
deliberately does not trigger queries from a passive getter; a
future `C_PlayerInfo.RequestLoadPlayerByID` would do that
explicitly and fire a load-result event.

**Persistent fallback**: when
[`C_PlayerCache.SetEnabled(true)`](#c_playercachesetenabledenabled)
has been opted into, a per-realm on-disk cache extends coverage
across sessions. On engine cache miss, `GetPlayerInfoByGUID` falls
back to the persistent cache and returns `name`, `class`, `race`,
and `sex` from storage (`realm` comes back as `""` since vanilla
is single-realm and we don't carry per-player realm names).
Returns `nil` only when both the engine and persistent caches miss.

**Implementation**: calls the engine's get-or-fetch primitive at
`0x0055F080` with a NULL callback (pure cache read). The cache
instance lives at `0x00C0E228`; entry layout (name, realm, race,
sex, class) was reverse-engineered from the
`SMSG_NAME_QUERY_RESPONSE` write path at `0x0055F310`.

### `UnitNameFromGUID(guid)`

Returns `name, realm` for the player identified by `guid`, or `nil`
if no player with that GUID has been encountered yet.

It tries three sources in order:

1. The object manager — any currently-synced unit (a player you can
   see, your target, a party or raid member, an NPC in range).
2. Your friends list — it keeps name and GUID for online and offline
   friends, so a friend resolves even when not synced and never seen
   in chat. Always on.
3. The persistent on-disk name cache, when you have enabled it.

```lua
local name, realm = UnitNameFromGUID(UnitGUID("target"))
if name then
    ChatFrame1:AddMessage("Target: " .. name)
end

-- Resolving a chat link
local name, realm = UnitNameFromGUID("0x0000000000000777")
```

`realm` is always `""` in vanilla — the engine doesn't populate
per-player realm names and 1.12 has no cross-realm interaction.
We push the empty string rather than `nil` to match the convention
of the other player-info accessors; addons can gate on `realm == ""`.

Doesn't trigger a network query on miss. To populate an unknown
GUID, either let the engine receive a `SMSG_NAME_QUERY_RESPONSE`
through normal chat/target interaction, or call
[`C_PlayerCache.RememberPlayer`](#c_playercacherememberplayerguid-name-classtoken)
explicitly.

### `C_PlayerCache.GetPlayerInfoByName(name)`

Returns
`localizedClass, englishClass, localizedRace, englishRace, sex, name, realm, guid`
for a player in the persistent NameCache, or `nil` if the name
isn't cached. Companion to `GetPlayerInfoByGUID` for the case where
an addon has a player's name but not their GUID — e.g., when
`GetCurrentChatGUID()` returns nil for a chat event the engine
didn't tag with the sender GUID (system notifications, some channel
messages, etc.).

```lua
local _, class, _, race, _, _, _, guid = C_PlayerCache.GetPlayerInfoByName("Gedwyr")
-- "MAGE", "Human", "0x000000003B9ADAA7"

C_PlayerCache.GetPlayerInfoByName("NeverSeen")  -- nil (not cached)
```

Match is **case-sensitive, exact** — vanilla server-stored names
are case-stable (`"Gedwyr"` won't match `"gedwyr"`).

Returns (vs. `GetPlayerInfoByGUID`):

| Slot | Value | Notes |
|------|-------|-------|
| 1–7  | same as `GetPlayerInfoByGUID` | see above |
| 8    | `guid` | `"0xHHHHHHHHLLLLLLLL"` — the cached player's full GUID, so name-based callers can chain into GUID-keyed APIs. Absent from `GetPlayerInfoByGUID` since the caller already has it. |

Only reads from the **persistent NameCache** — does *not* hit the
engine's in-memory NAME_QUERY cache (that's keyed by GUID and has
no name index). So this requires `C_PlayerCache.SetEnabled(true)`
to have been opted into; entries get there via the engine's
NAME_QUERY response hook, the visible-object scan, and
`C_PlayerCache.RememberPlayer`.

**Implementation**: O(1) lookup via a `name → guid` index map
maintained in lockstep with the GUID-keyed entry map. Eviction
keeps the index consistent: when a name's character is deleted and
the name is recycled to a different GUID, the prior entry is
removed from both maps.

### `C_PlayerCache.RememberPlayer(guid, name, classToken)`

Adds a `(guid → name, classID, raceID, sex)` entry to the persistent
name cache. For the engine-driven coverage (chat / group / guild /
visible objects), no addon-side feeding is needed — those flow into
the cache automatically through the engine's
`SMSG_NAME_QUERY_RESPONSE` write path. This call exists for the
*other* sources libunitscan-style addons harvest from but the engine
NameCache doesn't see directly: `/who` results, mouseover, target
snapshots, etc.

```
C_PlayerCache.RememberPlayer(guid, name, classToken [, raceToken [, sex]])
```

Returns `true` on success, `false` if the persistent cache isn't
enabled or the required args are malformed.

- `guid` — `"0xHHHHHHHHLLLLLLLL"` string (same format `UnitGUID`
  returns). 8-hex `"0xLLLLLLLL"` is also accepted (hi-dword zero).
- `name` — 1–12 ASCII chars (vanilla character-name range). Tabs,
  newlines, and high bytes are stripped.
- `classToken` — uppercase token like `"WARRIOR"`, `"MAGE"`. Looked
  up against `ChrClasses.dbc` filename field, case-insensitive.
  Passing an unknown token keeps the entry's prior class (so a
  name-only sighting doesn't erase good class data).
- `raceToken` *(optional)* — uppercase token like `"NIGHTELF"`,
  `"SCOURGE"` (vanilla's filename for Undead). Same resolution as
  classToken, against `ChrRaces.dbc`. Omitted/unknown → leaves
  prior race alone.
- `sex` *(optional)* — `0` (male) or `1` (female), matching the
  wire-format convention the engine cache stores. Modern WoW's
  `UnitSex` uses `2`/`3`; pass `UnitSex - 2` if you're forwarding
  that value. Omitted/`0` → leaves prior sex alone (so this call
  can't be used to flip a stored value back to male; the
  `SMSG_NAME_QUERY_RESPONSE` hook handles direct assignment).

```lua
-- Harvest /who results into the persistent cache
local function OnWhoUpdate()
    for i = 1, GetNumWhoResults() do
        local name, _, _, _, class, _, _, _, _, _, _, sex = GetWhoInfo(i)
        -- class/race come back localized from GetWhoInfo; the
        -- locale-token tables in pfUI/AceLocale handle the
        -- inverse mapping. RememberPlayer expects engine tokens
        -- ("WARRIOR" etc.).
    end
end
```

Each non-zero field is updated on the existing entry; zeros are
treated as "caller doesn't know" and preserve prior real data. A
`name`-only update (classToken passed but unknown) refreshes the
name without erasing class.

The "deleted character recreated with same name, different class"
collision case that name-keyed caches suffer from doesn't apply
here: GUIDs are permanent for the life of a vanilla character, so
the new character has a different GUID and gets a different cache
entry.

### `C_PlayerCache.SetEnabled(enabled)`

Opts into (or out of) the persistent name cache. `enabled` is a
boolean (numeric `0`/`1` also accepted). Persists to
`WTF\Account\<account>\ClassicAPI.txt` as
`PersistentNameCacheEnabled=1`/`0`, so the choice survives
client restarts.

When enabled:

- Every `SMSG_NAME_QUERY_RESPONSE` the engine processes is also
  written to `WTF\Account\<account>\<realm>\ClassicAPI_NameCache.txt`.
- Lua-side `C_PlayerCache.RememberPlayer` calls become effective
  (they're no-ops when the cache is disabled).
- `GetPlayerInfoByGUID` gains the cross-session fallback path
  documented above.
- [`SetScanEnabled`](#c_playercachesetscanenabledenabled)
  becomes effective if also turned on.

When disabled, the on-disk file is left in place (re-enabling later
restores the prior contents); future writes are simply suppressed.

```lua
C_PlayerCache.SetEnabled(true)
```

Returns nothing.

### `C_PlayerCache.IsEnabled()`

Returns the current state of the persistent name cache as a
boolean. `false` until
[`SetEnabled`](#c_playercachesetenabledenabled)
has been called (or its prior call survived in the on-disk settings
file).

```lua
if C_PlayerCache.IsEnabled() then
    -- persistent fallback is active for GetPlayerInfoByGUID
end
```

### `C_PlayerCache.SetScanEnabled(enabled)`

Opts into the **visible-object sweep**: an opportunistic walk of
the engine's live visible-object list that feeds every player in
render range (whose object is currently loaded by the client) into
the persistent name cache. Throttled to once per ~10 seconds, on
the existing `Frame::RegisterEvent` hook — no per-frame overhead,
no extra hooks installed.

`enabled` is a boolean (numeric `0`/`1` also accepted). Persists to
`WTF\Account\<account>\ClassicAPI.txt` as
`NameCacheScanEnabled=1`/`0`.

```lua
C_PlayerCache.SetEnabled(true)        -- prerequisite
C_PlayerCache.SetScanEnabled(true)
-- Now: standing in Stormwind for a minute pre-populates the cache
-- with every visible player's name and class.
```

**Independent of the cache toggle.** Turning this on alone has no
effect — the sweep relies on `Remember()` which silently no-ops
when the on-disk cache is disabled. Turning the cache off (without
also turning the scan off) preserves the scan setting for the next
time you re-enable the cache.

**Players only.** NPCs and pets aren't cached — their GUIDs are
ephemeral and can get reused across sessions, so caching them is
worse than useless.

**Implementation**: walks `ClntObjMgrEnumVisibleObjects`
(`0x00468380`) — the same engine iterator
[VanillaMinimapTracking](https://github.com/Brues/VanillaMinimapTracking)
uses for blip rendering — filtering each GUID through
`ClntObjMgrObjectPtr` (`0x00468460`) with `TYPEMASK_PLAYER`. Name
comes from the CGObject vftable's `GetName` slot; class is the
byte at `[m_objectFields + 0x79]` (UNIT_FIELD_BYTES_0 byte 1, same
field `Script_UnitClass` reads).

### `C_PlayerCache.IsScanEnabled()`

Returns the current visible-object scan setting as a boolean.
Independent of the cache toggle — this only reflects the scan-
specific setting, not whether the scan is *effectively* running
(which also requires the cache to be on).

```lua
if C_PlayerCache.IsScanEnabled()
    and C_PlayerCache.IsEnabled() then
    -- visible-object sweeps are running every ~10s
end
```

## NewItems

Backports the modern "new item glow" bookkeeping — the sparkle the retail
bag UI shows on items you've just picked up but haven't looked at yet.
Vanilla 1.12 never had this feature (no DBC, no server data, no engine
bit); it's pure client-side tracking over your bag contents, so ClassicAPI
adds it.

Newness is keyed on each item's **instance GUID** (read off the CGItem),
not on `(bagID, slotIndex)`. A flag therefore survives the player
rearranging a bag — exactly like retail, and impossible for a pure-Lua
addon (vanilla exposes no per-instance GUID to Lua). Only **bag** items are
tracked (bags 0–4); equipment and bank are out of scope, matching retail.

The feed is entirely client-side C++, the same as retail (no addon Lua), and
event-driven rather than polled: it hangs off the engine's own `BAG_UPDATE`
fire sites, so a bag rescan runs only on frames where a bag actually
changed. Each rescan diffs the resident item GUIDs against the previous one,
flags anything newly acquired, and prunes flags for items that leave.
`BAG_NEW_ITEMS_UPDATED` (no payload) fires whenever the new-item set
changes. A one-time baseline taken ~1.5s after login (re-armed on character
switch) means items already owned at login aren't flagged.

### `C_NewItems.IsNewItem(bagID, slotIndex)`

Returns `true` if the item currently in bag `bagID` slot `slotIndex` is
flagged new, `false` otherwise (empty slot, unflagged item, or non-numeric
args). `bagID` 0 is the backpack; 1–4 are the equipped bags. Slot indices
are 1-based, matching `GetContainerItemInfo`.

```lua
if C_NewItems.IsNewItem(0, 3) then
    -- show the "new" glow on backpack slot 3
end
```

### `C_NewItems.RemoveNewItem(bagID, slotIndex)`

Clears the new flag for the item in that slot (e.g. once the player has
seen it). No-op if the slot is empty or the item wasn't flagged. Fires
`BAG_NEW_ITEMS_UPDATED` when it actually removes a flag. Because the flag is
GUID-keyed, the item stays un-flagged even if it's later moved to another
slot.

### `C_NewItems.ClearAll()`

Clears every new flag at once. Fires `BAG_NEW_ITEMS_UPDATED` if anything was
flagged.

## PlayerInfo

### `C_PlayerInfo.CanUseItem(itemID)`

Returns `true` if the local player meets the item's **use/equip
requirements**, `false` otherwise. This is the "would the item be red in
the tooltip" gate, and rounds out the item-usability trio:

| Function | Question it answers |
|----------|---------------------|
| `C_Item.IsEquippableItem(item)` | Does the item fit *some* slot? (static, player-independent) |
| `IsUsableItem(item)` | Is the item's *on-use* ability castable right now? |
| `C_PlayerInfo.CanUseItem(itemID)` | May *this* player equip/use it at all? |

So this is what answers "a Mail chest on a Mage" (armor proficiency), "a
level-40 item at level 20" (RequiredLevel), and class/race-restricted gear.

`itemID` is a number (item links are also accepted). Checks performed:

1. **Proficiency** — for weapons/armor, the engine keeps a per-item-class
   subclass-proficiency bitmask (fed by `SMSG_SET_PROFICIENCY`); the item's
   subclass bit must be set. Item classes with no proficiency concept
   (consumables, trade goods, …) are unrestricted.
2. **RequiredLevel** ≤ player level.
3. **AllowableClass / AllowableRace** masks include the player.
4. **RequiredSkill** — the player must have the item's skill line at an
   effective rank ≥ `RequiredSkillRank` (e.g. a mount that "Requires
   Riding (150)", a tool that "Requires Engineering (200)").
5. **RequiredSpell** — the player must know the item's prerequisite spell
   (or a higher rank of it): a crafting specialization ("Requires
   Armorsmith") or a proficiency spell.
6. **RequiredHonorRank / RequiredCityRank** — PvP-rank gates (the player's
   honor rank and earned PvP-medal bitmask).
7. **RequiredReputation** — current standing with the item's faction must
   reach the required reaction band (e.g. "Requires Honored with …").

```lua
C_PlayerInfo.CanUseItem(12640)  -- Lionheart Helm (plate) → true on a Warrior, false on a Mage
C_PlayerInfo.CanUseItem(6948)   -- Hearthstone → true (no restrictions)
C_PlayerInfo.CanUseItem(19872)  -- a mount → false without Riding (150)
C_PlayerInfo.CanUseItem(12717)  -- Plans: Lionheart Helm → false without the Armorsmith spec
```

> Synchronous, like `C_Item.IsEquippableItem`: an uncached item returns
> `false` with no async load fired. Warm the cache
> (`C_Item.RequestLoadItemDataByID`) and re-check on `ITEM_DATA_LOAD_RESULT`
> if needed.

### `C_PlayerInfo.GUIDIsPlayer(guid)` / `GUIDIsCreature` / `GUIDIsPet` / `GUIDIsGameObject`

Type checks on the raw 1.12 GUID format. Vanilla GUIDs encode the
entity type in the high 16 bits of the qword — players have
`0x0000` (low dword = player ID), creatures `0xF130xxxx`, pets
`0xF140xxxx`, game objects (herbs / chests / etc.) `0xF110xxxx`,
items `0x4000xxxx`. Each function returns `true` only for its
specific prefix; the `"0x0000000000000000"` sentinel returns
`false` from all four.

```lua
if C_PlayerInfo.GUIDIsPlayer(UnitGUID("target")) then
    -- target is a player
end

-- Combat-log row triage: was that a player kill or a mob kill?
local function OnCombatLogEvent(_, _, eventType, srcGUID, ...)
    if eventType == "PARTY_KILL" then
        if C_PlayerInfo.GUIDIsPlayer(srcGUID) then ... end
    end
end
```

`GUIDIsPlayer` matches modern WoW's signature exactly; the other
three are companions for the most common type-distinction needs.
For other types (corpse, dynamic object, transport, item) the
underlying classifier exists internally — let us know if you need
one exposed.

Accepts either the 16-digit `"0xHHHHHHHHLLLLLLLL"` form or the
8-digit `"0xLLLLLLLL"` shortcut (high dword implicitly zero).
Malformed input returns `false` rather than raising — matching
modern's tolerance for stale GUIDs from addon-side caches.

### `C_PlayerInfo.GetName / GetClass / GetRace / GetSex / IsConnected(playerLocation)`

The `PlayerLocation`-taking player accessors. A `PlayerLocation` (see
`PlayerLocation` / `PlayerLocationMixin`) wraps one way to identify a player;
these resolve it to a unit and return the requested attribute.

| Function | Returns |
|----------|---------|
| `C_PlayerInfo.GetName(playerLocation)` | `name` |
| `C_PlayerInfo.GetClass(playerLocation)` | `className, classFilename, classID` |
| `C_PlayerInfo.GetRace(playerLocation)` | `raceID` |
| `C_PlayerInfo.GetSex(playerLocation)` | `sex` (1 neutral / 2 male / 3 female) |
| `C_PlayerInfo.IsConnected([playerLocation])` | `isConnected` |

```lua
local loc = PlayerLocation:CreateFromUnit("target")
local className, classFile, classID = C_PlayerInfo.GetClass(loc)
local name = C_PlayerInfo.GetName(loc)
local raceID = C_PlayerInfo.GetRace(loc)          -- feed into C_CreatureInfo.GetRaceInfo
local sex = C_PlayerInfo.GetSex(loc)
```

- **Supported location kinds: `unit` and `guid`.** Unit locations use their
  token directly; GUID locations resolve only while the GUID maps to a
  currently-visible unit (target, party/raid member, someone in range). The
  chat-line / who / battlefield-score / community / voice kinds have no
  client-side unit resolution in 1.12 and return nil.
- Everything is read straight from the engine's own data — no `Unit*` Lua
  wrappers are called. `GetName` uses the polymorphic object name getter (the
  same path as `UnitNameFromGUID`); `GetSex` reads the `UNIT_FIELD_BYTES_0`
  gender byte and reports it as `UnitSex`'s 2/3; `IsConnected` treats a
  currently-synced object as online and otherwise consults the group-member
  roster's online bit, exactly as `UnitIsConnected` does. `IsConnected` with
  no argument defaults to the local player.
- **`GetRace` / `GetClass`** return the numeric ids that vanilla's
  `UnitRace` / `UnitClass` don't (those give only names). `classID` round-trips
  with [`C_CreatureInfo.GetClassInfo`](#c_creatureinfogetclassinfoclassid) and
  `raceID` with [`C_CreatureInfo.GetRaceInfo`](#c_creatureinfogetraceinforaceid);
  `GetClass`'s `className` / `classFilename` are the same localized name +
  non-localized token (`"WARRIOR"`) those return.
- An unresolvable or unsupported location returns nil. A malformed unit token
  raises (the engine's token→GUID resolver rejects it), same as passing it to
  `UnitName`/`UnitClass` directly.

## Quest

### `C_QuestLog.GetQuestIDForLogIndex(index)`

Returns the questID (Quest.dbc row ID) for the entry at the given 1-based
quest log index. In 3.3.5 this came as the 9th return of `GetQuestLogTitle`;
in 1.12 it isn't returned at all, even though the engine has it internally.

- Returns the questID for real quests.
- Returns `0` for header rows (zone / category dividers).
- Returns `nil` if the index is out of range.

```lua
for i = 1, GetNumQuestLogEntries() do
    local title, level, questTag, isHeader, isCollapsed, isComplete
        = GetQuestLogTitle(i)
    local questID = C_QuestLog.GetQuestIDForLogIndex(i)  -- 0 for headers
    -- ...
end
```

### `C_QuestLog.GetLogIndexForQuestID(questID)`

The inverse of `GetQuestIDForLogIndex`: returns the 1-based quest log index
of `questID`, or `nil` if the quest isn't in the log. The index spans the
full entry list (headers included), so it can be passed straight to
`GetQuestLogTitle(index)` / `GetQuestLogLeaderBoardID(...)` and the other
index-based quest-log accessors.

```lua
local index = C_QuestLog.GetLogIndexForQuestID(questID)
if index then
    local title = GetQuestLogTitle(index)
    -- ...
end
```

### `C_QuestLog.GetHeaderIndexForQuest(questID)`

Returns the 1-based log index of the collapsible **header** (zone /
`"Dungeon"` / class-sort category, …) that `questID` sits under, or `nil` if
the quest isn't in the log. The quest log is laid out header-then-its-quests,
so this locates the quest and walks back to the nearest preceding header row.
Pass the result to `GetQuestLogTitle(headerIndex)` for the category name.

```lua
local headerIndex = C_QuestLog.GetHeaderIndexForQuest(questID)
if headerIndex then
    local zone = GetQuestLogTitle(headerIndex)  -- e.g. "Westfall"
end
```

### `C_QuestLog.RequestLoadQuestByID(questID)`

Asks the engine to fetch the static data for `questID` (title, description,
objectives, reward text) from the server if not already cached. Returns no
values — fire-and-forget, matching modern WoW's signature.

Fires `QUEST_DATA_LOAD_RESULT(questID, success)` when the data lands in the
cache. Synchronously fired when the data was already cached
(so polling code paths still work), asynchronously fired after
`SMSG_QUEST_QUERY_RESPONSE` lands when the engine had to round-trip to the
server.

> **`success` is `1` or `nil`.** The engine's printf-style event
> dispatcher has no `%b` token, so we encode the boolean as `1` for
> success / `nil` for failure. `if success then ...` works as expected
> (`nil` falsy, `1` truthy). Same encoding as `ITEM_DATA_LOAD_RESULT`
> and `GET_ITEM_INFO_RECEIVED`.

> **Vanilla limitation:** for *invalid* questIDs (ones the server doesn't
> have), the 1.12 server silently drops the query — it doesn't send a
> "not found" response packet. The engine's pending callback never
> resolves, so `QUEST_DATA_LOAD_RESULT` doesn't fire with `success=0`
> either. Modern Classic Era servers explicitly respond with an error
> for invalid IDs, which is why modern `RequestLoadQuestByID` reliably
> fires `success=false`; vanilla doesn't. Addons that need to handle
> "request timed out" should use their own timer (the Lua polyfill at
> `!!!ClassicAPI/Util/QuestUtil.lua` uses a 180-second wait).

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("QUEST_DATA_LOAD_RESULT")
f:SetScript("OnEvent", function()
    -- vanilla 1.12: event payload is in `arg1`, `arg2`, ... globals
    if event == "QUEST_DATA_LOAD_RESULT" and arg2 == 1 then
        local questID = arg1
        -- title/description/objectives are now in the engine's quest cache
    end
end)
C_QuestLog.RequestLoadQuestByID(2)
```

### `C_QuestLog.IsOnQuest(questID)`

Returns `true` if `questID` is currently in the player's quest log
(either still in progress or ready to turn in — both states live in
the log). Returns `false` for invalid input (non-positive, non-number,
or simply not in the log).

```lua
if C_QuestLog.IsOnQuest(215) then
    -- player has "Jungle Secrets" accepted
end
```

Walks the same `VAR_QUEST_LOG_ENTRIES` array
[`C_QuestLog.GetQuestIDForLogIndex`](#c_questlogGetQuestIDForLogIndexindex)
reads and matches against each real entry's questID, skipping the
zone/category header rows.

Cheaper than the engine's `IsUnitOnQuest(logIndex, unit)`, which
requires the addon to walk the log to find the matching index first
and resolves a unit token even when the answer is just about the
player. Same answer in a single call.

### `C_QuestLog.IsUnitOnQuest(unit, questID)`

Returns `true` if the unit has `questID` in their quest list. Modern
arg order — `unit` first, `questID` second — and questID-keyed rather
than log-index-keyed like vanilla's `IsUnitOnQuest(logIndex, unit)`.

```lua
if C_QuestLog.IsUnitOnQuest("party1", 215) then
    -- partymate is also on Jungle Secrets
end
```

For `unit == "player"` equivalent to
[`C_QuestLog.IsOnQuest(questID)`](#c_questlogisonquestquestid). For
other tokens (party/raid members, target), the unit must be in the
engine's sync range — the data comes from `SMSG_QUESTGIVER_QUEST_DETAILS`
broadcasts, which only reach you while the other player is within
the client's sync window. Returns `false` for NPCs, units out of
range, and units that haven't synced their quest list yet.

Returns `false` (no `lua_error`) for invalid input: non-string unit,
non-number / non-positive questID, unresolvable token, or unit that's
not player-controlled (i.e., a creature). The
`UNIT_FLAG_PLAYER_CONTROLLED` gate is mandatory — without it the
`+0xE68` deref would AV on any NPC.

### `C_QuestLog.GetTitleForQuestID(questID)`

Returns the title (string) for `questID` from the engine's quest static-info
cache, or `nil` if the data isn't loaded. Doesn't require the quest to be
in the player's quest log — works for any questID once its data has been
fetched. Header rows are excluded (their titles live in `QuestSort.dbc`,
not in this cache); for those you'd use the existing `GetQuestLogTitle`.

The cache is populated lazily — by the engine's own quest-log path when
the player has the quest, or explicitly by
`C_QuestLog.RequestLoadQuestByID`. If the title isn't there yet, queue a
load and read on the event:

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("QUEST_DATA_LOAD_RESULT")
f:SetScript("OnEvent", function()
    if event == "QUEST_DATA_LOAD_RESULT" and arg2 == 1 then
        local title = C_QuestLog.GetTitleForQuestID(arg1)
        if title then
            -- title is now available
        end
    end
end)
C_QuestLog.RequestLoadQuestByID(215)
```

### `C_QuestLog.GetQuestDetails(questID)`

Returns a table with every field we know how to read from the engine's
quest static-info cache for `questID`, or `nil` if the data isn't
loaded. One round-trip alternative to calling
`GetTitleForQuestID` / `GetQuestLogRewardMoney` / etc. across each of
its individual accessors. Quest-helper-style addons that want
structured data per quest are the primary consumers.

Like `GetTitleForQuestID`, this is a pure cache probe — pair it with
`C_QuestLog.RequestLoadQuestByID` and listen for
`QUEST_DATA_LOAD_RESULT` if the quest isn't already cached.

#### Returned table

| Field | Type | Notes |
|-------|------|-------|
| `questID` | number | Echo of the input. |
| `title` | string | Locale-applied. |
| `level` | number | Quest level from the static record. |
| `questType` | string \| absent | Localized tag string — `"Elite"`, `"Group"`, `"PvP"`, `"Dungeon"`, `"Raid"` — sourced from `QuestInfo.dbc`. Field is omitted for plain quests with no tag. |
| `rewardMoney` | number | Reward copper. `0` if the quest *requires* money instead. |
| `requiredMoney` | number | Copper the player must hand in to complete (e.g. quartermaster contributions). `0` for quests with no money requirement. |
| `rewardMoneyAtMaxLevel` | number | Vanilla's level-60 reward bonus. Added to `rewardMoney` when the player is level 60; populated even on low-level quests. |
| `rewardSpellID` | number | `spellID` of a spell *taught* on completion (e.g. profession recipes), or `0` for no learned spell reward. |
| `srcItemID` | number | `itemID` the questgiver hands the player on accept (e.g. the sigil in *"Verdant Sigil"* — given on accept, read by the player, then turned back in). `0` = no source item. Same as the quest's `requirements[].id` for "give-then-return" quests. |
| `questFlags` | number | Raw `QUEST_FLAGS_*` bitfield — only bit `0x08` (sharable) is positively confirmed-tested by the vanilla engine; other bits are presumed-stored but unverified. |
| `isSharable` | boolean | Convenience extraction of bit `0x08` from `questFlags`. |
| `description` | string | The questgiver's narrative text. Raw — printf-style `$N` (player name), `$C` (class), `$R` (race) tokens are **not** substituted; use `string.gsub` if you need runtime values. |
| `objectives` | string | The "what you must do" summary text, same raw / un-substituted format as `description`. |
| `completionText` | string | The "now turn it in" text shown on the reward UI panel after objectives are met. Often empty for simple quests; populated for narrative-heavy ones. Modern API name: `GetQuestLogCompletionText`. |
| `poi` | table \| absent | Point-of-interest marker (`{mapID, x, y, opt}`). Set on quests with a server-supplied "go here" location; omitted when `mapID == 0`. |
| `rewardItems` | array of `{id, count}` | Items the quest gives unconditionally on turn-in. Empty when there are no fixed rewards. |
| `choiceItems` | array of `{id, count}` | "Pick one" reward items. Empty when none. |
| `requirements` | array of `{kind, id, count, text}` | Objectives. `kind` is `"monster"` (creature), `"object"` (gameobject), or `"item"` (collect / interact). `text` is the questgiver's per-objective override (e.g. `"Investigate the cave"` instead of the auto-generated `"Mor'shan Bear: 0/8"`); empty string means use the auto-format. Order: NPC/GO objectives first, then item objectives, both 1-indexed. Only non-empty slots included. |

> **Not included** — race, class, skill, time limit, and suggested-player count. Vanilla 1.12's `SMSG_QUEST_QUERY_RESPONSE` doesn't ship those fields; the server enforces them and filters quests *before* broadcasting, so the client only ever sees quests it could accept and never receives the static restriction values. Verified empirically: a race-restricted starter (`3120` Verdant Sigil — Night Elf Druid only), a dungeon quest (`914`), and a timed delivery (`3364` Scalding Mornbrew — 5-min authored timer) all have those cache slots zero-filled in memory. **Addons that need that data must source it from an external scraped database** like pfQuest's.
>
> **Field reliability:** everything currently returned is either confirmed-correct via the engine's own `Script_GetQuestLog*` accessors or empirically verified against in-game quest semantics. The `poi` field is the only remaining hypothesis — its offsets are confirmed but no test quest has yet exercised it.

XP rewards aren't exposed — vanilla 1.12 has no `GetQuestLogRewardXP`,
and per emulator-decoded packet structure, the server doesn't include
an XP field in `SMSG_QUEST_QUERY_RESPONSE`. XP is computed
server-side at turn-in from level-scaling tables.

#### Example

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("QUEST_DATA_LOAD_RESULT")
f:SetScript("OnEvent", function()
    if event == "QUEST_DATA_LOAD_RESULT" and arg2 == 1 then
        local d = C_QuestLog.GetQuestDetails(arg1)
        if d then
            print(d.title, "level", d.level, d.questType or "(no tag)")
            for _, r in ipairs(d.requirements) do
                print("  -", r.kind, r.id, "x", r.count)
            end
        end
    end
end)
C_QuestLog.RequestLoadQuestByID(1467)
```

Sample output for quest 1467 (`"Reagents for Reclaimers Inc."`):

```
Reagents for Reclaimers Inc.  level 40  (no tag)
  - item 6253 x 1
```

with `d.choiceItems = {{id=6793, count=1}, {id=6794, count=1}}`,
`d.rewardMoney = 3500`, `d.rewardMoneyAtMaxLevel = 1920`, etc.

### `C_QuestLog.GetNumQuestObjectives(questID)`

The quest's objective count — exactly the length
`GetQuestDetails(questID).requirements` would have — read straight from
the quest cache with **no Lua tables or strings materialized**. Returns
`nil` when the quest isn't cached (same contract as `GetQuestDetails`).

```lua
local n = C_QuestLog.GetNumQuestObjectives(1467)   -- 1
```

Use this instead of `GetQuestDetails` when only the count matters —
e.g. bounds-checking authored objective indices across every step of a
guide at load time. `GetQuestDetails` copies the quest's description /
objectives / completion text into Lua strings and builds ~20 table
fields per call; harmless one at a time, but a login-path loop over
hundreds of cached quests can put real pressure on vanilla's
fixed-size Lua memory pool. This accessor costs a few dozen byte reads
regardless of cache temperature.

### `C_QuestLog.IsQuestDataCachedByID(questID)`

Returns `true` if the quest's static data is currently in the
client-side quest cache, `false` otherwise. Pure cache probe — no
server query, no side effects. Pair with
`C_QuestLog.RequestLoadQuestByID` when you need to ensure the data
is loaded before checking.

Returns `false` (not error) for invalid input — non-number,
non-positive, or non-existent questIDs all yield `false`, matching
the `C_Item.IsItemDataCachedByID` shape.

```lua
if not C_QuestLog.IsQuestDataCachedByID(questID) then
    C_QuestLog.RequestLoadQuestByID(questID)
    -- listen for QUEST_DATA_LOAD_RESULT, or re-check on a timer
end
```

### `GetQuestLogLeaderBoardID(objectiveIndex [, questIndex])`

Companion to vanilla's `GetQuestLogLeaderBoard` — returns `(id, kind)`
for the same objective the existing call formats text for. `id` is
always positive (the raw `RequiredNPCOrGo` field is signed, but the
sign is folded into `kind` instead); `kind` is one of `"monster"`,
`"object"`, or `"item"`. Returns nothing for: out-of-range
`objectiveIndex`, header rows, quests whose static data hasn't been
cached yet, or event-text-only objectives that have no entry ID.

Arg shape matches the engine's actual `Script_GetQuestLogLeaderBoard`
(its usage string says `(index)` but the function also accepts the
2-arg form): with `questIndex` provided, looks up the entry at
that 1-based quest-log slot; without it, falls back to whichever
quest is currently selected via `SelectQuestLogEntry`.

```lua
-- Selected quest path (pairs with GetQuestLogLeaderBoard's selected mode)
SelectQuestLogEntry(13)
local text, kind, done = GetQuestLogLeaderBoard(1)
local id              = GetQuestLogLeaderBoardID(1)
-- text = "Young Stranglethorn Tiger slain: 4/10"
-- kind = "monster"
-- id   = 2630   (the creature template / entry)

-- Explicit-quest path
local text, kind = GetQuestLogLeaderBoard(1, 13)
local id, k2     = GetQuestLogLeaderBoardID(1, 13)
-- kind == k2 always
```

The ID is the same key `creaturecache.wdb` / `gameobjectcache.wdb` /
`itemcache.wdb` use, so cross-referencing against the existing item
or creature caches is direct — no localized-text parsing required.

Why not just expose this on `GetQuestLogLeaderBoard` itself? Modifying
the engine's existing return tuple would silently break addons that
destructure positionally (`local text, type, done = GetQuestLogLeaderBoard(...)`).
A separate function keeps the existing call wire-compatible.

> **Source**: the cached quest record (returned by `Quest::Cache::Peek`)
> carries the SMSG_QUEST_QUERY_RESPONSE objective arrays inline at
> fixed offsets:
> - `+0x149C` — `int32 RequiredNPCOrGo[4]` (positive = creature entry,
>   negative = gameobject entry)
> - `+0x14AC` — `uint32 RequiredNPCOrGoCount[4]`
> - `+0x14BC` — `uint32 RequiredItem[4]`
> - `+0x14CC` — `uint32 RequiredItemCount[4]`
>
> Iteration mirrors the engine at `0x004E0110`: walk the NPC/GO array
> first, skip zero slots, then the item array. 1-based `objectiveIndex`
> counts only non-empty slots.

## Spell

> **All `C_Spell.*` functions in this section accept any spell
> identifier**, not just a numeric `spellID`. The first argument can be:
>
> - **number** — spellID, used directly.
> - **`|Hspell:N|h` hyperlink** — extracted by parsing for the
>   embedded `spell:N` payload, so
>   `C_Spell.X(GetSpellLink(id))` round-trips cleanly.
> - **spell name** — case-sensitive lookup through the engine's name
>   resolver (`FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID`, the same chain
>   `CastSpellByName` uses). Tolerates `(Rank N)` suffixes via the
>   engine's parser. Bounded to spells in the **player's known
>   spellbook** — not arbitrary Spell.dbc rows.
>
> Unrecognized identifiers (garbage strings, nil, tables, etc.) return
> `nil` rather than raising a Lua usage error — matches modern WoW's
> permissive `C_Spell.*` convention. Function entries below write
> `(spellID)` in the signature for brevity but the broader shape is
> accepted everywhere.
>
> The legacy globals in this section (`GetSpellInfo`, `IsPassiveSpell`,
> `IsHarmfulSpell`, `IsUsableSpell`, etc.) keep their existing
> `(slot, bookType)` overload — only the `C_Spell.*` namespace is
> broadened.

### `C_Spell.DoesSpellExist(spellID)`

Returns `true` if `spellID` resolves to a populated `Spell.dbc`
record, `false` otherwise. Cheap pre-flight check for any of the
spellID-keyed `C_Spell.*` calls — if this returns `false`, the rest
of the family will return nil/empty.

```lua
if C_Spell.DoesSpellExist(133) then
    local info = C_Spell.GetSpellInfo(133)  -- safe
end
```

### `C_Spell.GetSchoolString(schoolMask)`

Returns the localized name for a damage-school bitmask. Resolves
single-bit masks via the engine's `SPELL_SCHOOL<n>_CAP` global
strings (`SPELL_SCHOOL0_CAP` = "Physical", `SPELL_SCHOOL2_CAP` =
"Fire", etc.).

```lua
C_Spell.GetSchoolString(4)    -- "Fire"
C_Spell.GetSchoolString(32)   -- "Shadow"
```

Vanilla spells are single-school — the multi-school combos
("Frostfire", "Shadowflame", "Spellstorm", etc.) were TBC-and-later
additions. Any multi-bit mask returns `"Unknown"`, matching the
modern API's documented fallback for unnamed combinations.

> Lua 5.0 doesn't support hex literals (`0x04` is a syntax error);
> pass mask values as decimals, or via `tonumber("0x04", 16)` if you
> want to keep hex notation in source.

| Decimal | Hex | School |
|--------:|-----|--------|
| `1` | `0x01` | Physical |
| `2` | `0x02` | Holy |
| `4` | `0x04` | Fire |
| `8` | `0x08` | Nature |
| `16` | `0x10` | Frost |
| `32` | `0x20` | Shadow |
| `64` | `0x40` | Arcane |

### `GetSpellInfo(spellID)` / `GetSpellInfo(slot, bookType)`

Returns the same nine values as 3.3.5's `GetSpellInfo`, **plus a 10th
value: `spellID`**, for **any** spell ID — including spells the player
has not learned. Stock 1.12 has no `GetSpellInfo` Lua function at all
(only `GetSpellName`/`GetSpellTexture`, both of which take a spellbook
*slot* rather than an ID), so addons that need spell metadata for
arbitrary IDs (raid frames, debuff trackers, aura libraries) currently
can't get it.

Returns `name, rank, icon, cost, isFunnel, powerType, castTime,
minRange, maxRange, spellID`. All read directly from `Spell.dbc` (with
`SpellIcon.dbc`, `SpellCastTimes.dbc`, and `SpellRange.dbc` for the
indirected fields). Cast time is in milliseconds; ranges are floats in
yards. `isFunnel` is a real boolean (`true`/`false`), matching 3.3.5's
behavior. Returns `nil` if the spell ID is out of range.

Two input forms are accepted:

- **`GetSpellInfo(spellID)`** — direct DBC lookup by ID.
- **`GetSpellInfo(slot, bookType)`** — same shape as 1.12's
  `GetSpellName(slot, bookType)`. `slot` is 1-based, `bookType` is
  `"spell"` (player) or `"pet"`. The slot is resolved to a spellID via
  the engine's spellbook array, then the same DBC reads run. Returns
  `nil` for empty / out-of-range slots.

```lua
local name, rank, icon, _, _, _, _, _, _, spellID = GetSpellInfo(133)
-- name="Fireball", rank="Rank 1", icon="Spell\\Fire\\...", spellID=133

-- spellbook overload
local _, _, _, _, _, _, _, _, _, id = GetSpellInfo(1, "spell")
-- id is the spellID at player spellbook slot 1
```

> **Note on the 10th return.** Modern WoW (5.0+) added the spellID as
> the 14th return of its slimmer signature. We kept the existing 9
> returns (so addons that worked against the previous signature still
> work) and just appended `spellID` at position 10.

### `C_Spell.GetSpellInfo(spellID)`

Modern table-style accessor for the same data. Returns a Lua table of
the spell's metadata, or `nil` if the spell ID is out of range.

Table fields:

| Field        | Type    | Notes |
|--------------|---------|-------|
| `name`       | string  | Localized name |
| `iconID`     | string  | Icon **path** (e.g. `"Interface\\Icons\\Spell_Fire_FlameBolt"`). See note below. |
| `castTime`   | number  | Base cast time in milliseconds, or 0 for instant |
| `minRange`   | number  | Yards, or 0 if not applicable |
| `maxRange`   | number  | Yards, or 0 if not applicable |
| `spellID`    | number  | Echo of the input |
| `rank`       | string  | Localized rank (e.g. `"Rank 1"`) — vanilla extra, not in modern's spec |
| `cost`       | number  | Base ManaCost — vanilla extra |
| `isFunnel`   | boolean | True for funnel-channeled spells — vanilla extra |
| `powerType`  | number  | 0=mana, 1=rage, 2=focus, 3=energy, 4=happiness — vanilla extra |

```lua
local info = C_Spell.GetSpellInfo(133)
-- info.name = "Fireball"
-- info.iconID = "Interface\\Icons\\Spell_Fire_FlameBolt"
-- info.castTime = 1500
-- info.spellID = 133
-- info.rank = "Rank 1"
-- ... etc.
```

> **Deviation from modern.** Modern WoW returns `iconID` as a
> `fileID:number`. Vanilla 1.12 has no fileID system — assets are
> referenced by path strings. We surface the icon path here so it's
> directly usable with `texture:SetTexture(info.iconID)`. If you're
> backporting code that expects a number, this is the field to adjust.

> **Vanilla extras.** The four fields beyond the modern spec
> (`rank`/`cost`/`isFunnel`/`powerType`) are present because 1.12 has
> them in `Spell.dbc` and the previous-generation `GetSpellInfo` exposed
> them. Including them costs nothing and helps addons backporting from
> 3.3.5 where the same data was returned positionally.

### `C_Spell.GetSpellName(spellID)`

Returns the localized name of `spellID`, or `nil` if the spell ID is out
of range or has no name in the current locale. Convenience accessor for
the `name` field of [`C_Spell.GetSpellInfo`](#c_spellgetspellinfospellid)
when that's all you need — single field read, no DBC indirection beyond
the locale lookup.

```lua
local name = C_Spell.GetSpellName(133)  -- "Fireball"
```

### `C_Spell.GetSpellTexture(spellID)`

Returns the icon path string for `spellID` (read from `SpellIcon.dbc`
via the spell's `SpellIconID` field), or `nil` if the spell ID is out
of range or the icon record is empty.

> **Path string, not fileID.** Modern WoW returns this as a
> `fileID:number`. Vanilla 1.12 has no fileID system — see the same
> note on [`C_Spell.GetSpellInfo`](#c_spellgetspellinfospellid)'s
> `iconID` field. Pass directly to `texture:SetTexture(...)`.

```lua
local path = C_Spell.GetSpellTexture(133)
-- path = "Interface\\Icons\\Spell_Fire_FlameBolt"
```

### `GetSpellLink(spellID)` / `GetSpellLink(slot, bookType)`

Returns the chat-style spell hyperlink and the spellID:

```
link, spellID = GetSpellLink(spellID)
              = GetSpellLink(slot, bookType)
```

Format is `|cff71d5ff|Hspell:ID:0|h[Name]|h|r` — the standard 1.12
spell-link wrapper. The trailing `:0` after the spellID matches modern
WoW's hyperlink shape (where the field is a sub-data slot for
pet-spellbook flags etc.); 1.12 ignores it during link parsing, but
addons grepping with `|Hspell:(%d+):` patterns will pick it up
correctly.

Two input forms, mirroring [`GetSpellInfo`](#getspellinfospellid--getspellinfoslot-booktype):

- `GetSpellLink(spellID)` — direct DBC lookup.
- `GetSpellLink(slot, bookType)` — resolves the spellbook slot to a
  spellID first. Useful when iterating the player's known spells:
  caller gets back both the link AND the underlying ID without a
  separate lookup.

Returns `nil` if the spellID/slot doesn't resolve to a real spell.

```lua
local link = GetSpellLink(133)
DEFAULT_CHAT_FRAME:AddMessage("Cast " .. link .. "!")

-- Walking the spellbook to print every learned spell:
for slot = 1, 1000 do
    local link, id = GetSpellLink(slot, "spell")
    if not link then break end
    -- ...
end
```

### `C_Spell.GetSpellLink(spellID)`

Modern table-namespace variant. Same link string as
[`GetSpellLink(spellID)`](#getspelllinkspellid--getspelllinkslot-booktype),
but returns only the link — no spellID echo since the caller already
had it on hand to make the call.

```lua
local link = C_Spell.GetSpellLink(133)  -- "|cff71d5ff|Hspell:133:0|h[Fireball]|h|r"
```

### `C_Spell.GetSpellDescription(spellID)`

Returns the formatted spell description for any `spellID` — including
spells the player has not learned. `$s1`/`$s2`/`$o1`/`$d`-style
placeholders are resolved to base-rank values; ranges and durations
appear as actual numbers (e.g. `"14 to 22 Fire damage"`, not
`"$s1 to $s2 Fire damage"`). Returns `nil` if the spell ID is out of
range or has no description in the current locale.

The 1.12 client doesn't expose this from Lua at all — the only existing
path is the scan-tooltip hack (set a hidden tooltip via
`SetHyperlink("spell:"..ID)` and read each `TextLeftN:GetText()` line).
That's slow, GC-heavy, and shares the global scan tooltip with every
other addon. This function calls the engine's own description formatter
directly — same code path the in-game tooltip uses, no UI side effects.

```lua
local desc = C_Spell.GetSpellDescription(133)  -- Fireball Rank 1
-- "Hurls a fiery ball that causes 14 to 22 Fire damage and an additional
--  2 Fire damage over 4 sec."
```

> **No caster scaling.** Values reflect the spell's base rank — caster
> level / spell power / talents are not applied. Modern WoW behaves the
> same way when called outside a unit context. If you need the
> "currently displayed" tooltip text with caster scaling, use
> `GameTooltip:SetSpellByID` and read line strings from there.

### `C_Spell.GetSpellMechanicByID(spellID)`

Returns the spell's mechanic as `(mechanicID, name)` — the standard WoW
`SpellMechanic` ID and its English name. Works for **any** `spellID`, not
just spells the player has learned.

Returns:
- nothing (`nil`) if the spell ID is invalid / out of range
- `(0, nil)` for a known spell that carries no mechanic
- `(mechanicID, name)` otherwise

The `name` is always the **enUS** string (locale-independent), because the
mechanic name is a stable token consumers string-match against
(`"rooted"`, `"stunned"`, `"polymorphed"`, …) — returning a localized
value would break that matching across clients. It is `nil` only when the
mechanic has no `SpellMechanic.dbc` record or no English name; the numeric
ID is still returned in that case.

```lua
C_Spell.GetSpellMechanicByID(118)  -- Polymorph        → 17, "polymorphed"
C_Spell.GetSpellMechanicByID(339)  -- Entangling Roots  → 7,  "rooted"
C_Spell.GetSpellMechanicByID(133)  -- Fireball          → 0,  nil
```

This reads `Spell.dbc`'s `Mechanic` field and resolves the name from
`SpellMechanic.dbc` directly — both DBCs are resident from boot, so the
call is synchronous with no caching or network round-trip (unlike item
data). It replaces hand-maintained `spellID → mechanic` lookup tables that
addons otherwise keep because vanilla exposes no Lua reader for the field
— e.g. crowd-control macro conditionals (`[cc:stun]`, `[cc:fear]`) that
need to know which mechanic a debuff applies.

The complete `SpellMechanic.dbc` set on 1.12 — all 27 rows (the `name`
column is exactly what this function returns):

| ID | name | ID | name | ID | name |
|----|------|----|------|----|------|
| 1 | `charmed` | 10 | `asleep` | 19 | `shielded` |
| 2 | `disoriented` | 11 | `ensnared` | 20 | `shackled` |
| 3 | `disarmed` | 12 | `stunned` | 21 | `mounted` |
| 4 | `distracted` | 13 | `frozen` | 22 | `seduced` |
| 5 | `fleeing` | 14 | `incapacitated` | 23 | `turned` |
| 6 | `clumsy` | 15 | `bleeding` | 24 | `horrified` |
| 7 | `rooted` | 16 | `healing` | 25 | `invulnerable` |
| 8 | `pacified` | 17 | `polymorphed` | 26 | `interrupted` |
| 9 | `silenced` | 18 | `banished` | 27 | `dazed` |

> **No mechanic `30`.** The table tops out at `27`. Sap and Gouge report
> `14` (incapacitated) in 1.12 — the `30` ("sapped") value used by some
> addon tables is a later-expansion addition and has no row here.

### `C_Spell.GetSpellEffectMechanics(spellID)`

Returns the spell's three per-effect `SpellMechanic` ids (`Spell.dbc`
`EffectMechanic[3]`) as a 1-based array table, or `nil` for an invalid /
out-of-range spell ID. Each entry uses the same numbering as
[`GetSpellMechanicByID`](#c_spellgetspellmechanicbyidspellid) (`0` = that
effect carries no mechanic).

```lua
C_Spell.GetSpellEffectMechanics(1822)  -- Rake  → { 0, 15, 0 }
C_Spell.GetSpellEffectMechanics(703)   -- Garrote → { 0, 0, 0 }
C_Spell.GetSpellEffectMechanics(133)   -- Fireball → { 0, 0, 0 }
```

Complements [`GetSpellMechanicByID`](#c_spellgetspellmechanicbyidspellid),
which reads only the **spell-level** `Mechanic` field (`+0x14`). Vanilla
frequently stores a spell's mechanic on an **effect** instead — periodic
damage such as bleeds is the common case. Garrote/Rupture/Rend/Rip tag
`bleeding` (15) at the spell level, but **Rake** has spell-level `0` and
`EffectMechanic[2] = 15`, so effect-mechanic-aware callers (e.g. bleed
classification for immunity tracking) need this array:

```lua
local function IsBleed(spellID)
    if C_Spell.GetSpellMechanicByID(spellID) == 15 then return true end
    local em = C_Spell.GetSpellEffectMechanics(spellID)
    if em then for i = 1, 3 do if em[i] == 15 then return true end end end
    return false
end
```

Reads `Spell.dbc` directly (`EffectMechanic[3]` at `+0x13C`), so it covers
every spell the client knows — not just the player's spellbook — with no
caching or network round-trip.

### `C_Spell.GetSpellDispelType(spellID)`

Returns the spell's dispel type from `Spell.dbc`: `1` = Magic,
`2` = Curse, `3` = Disease, `4` = Poison. Returns `0` for a spell
that nothing can dispel, and for an unknown spell.

```lua
C_Spell.GetSpellDispelType(118)   -- Polymorph → 1 (Magic)
C_Spell.GetSpellDispelType(980)   -- Curse of Agony → 2 (Curse)
C_Spell.GetSpellDispelType(133)   -- Fireball → 0 (none)
```

Use the number to show what removes an effect, or to filter auras by
dispel type. The values match the `DISPEL_*` set the server uses.

### `C_Spell.GetSpellRadius(spellID)` / `GetSpellRadius(slot, bookType)`

Returns `(baseRadius, modifiedRadius)` — the spell's AOE radius in yards,
or `nil` for a non-AOE spell (no effect carries a radius). Works for any
spell, not just ones the player has learned.

- **`baseRadius`** — the raw `SpellRadius.dbc` value; caster-independent,
  what an AOE tracker watching other units wants.
- **`modifiedRadius`** — `baseRadius` with the **local player's** talent /
  item modifiers applied (e.g. a Mage's Arctic Reach). Equal to
  `baseRadius` when the player has no matching modifier.

```lua
C_Spell.GetSpellRadius(122)    -- Frost Nova → 10, 10  (no Arctic Reach)
C_Spell.GetSpellRadius(122)    -- Frost Nova → 10, 12  (with Arctic Reach r2)
C_Spell.GetSpellRadius(133)    -- Fireball   → nil      (single-target)
GetSpellRadius(10, "spell")    -- 10th player-book slot, by spellbook position
```

Two signatures matching the dual-signature shape used elsewhere in this
backport (`GetSpellInfo`, `SpellHasRange`, …): the namespaced form takes
a modern spell *identifier* (numeric ID or name); the bare global takes
the vanilla `(slot, bookType)` spellbook position, where `bookType`
follows the `GetSpellName` convention — `"pet"` → pet book, `"spell"`
(or any non-pet / omitted value) → player book.

`baseRadius` reads `Spell.dbc`'s per-effect `EffectRadiusIndex[3]`
(`+0x160`) and resolves each into `SpellRadius.dbc`; since a spell's
effects can each carry their own radius, the **largest** base radius
across the three effects is returned.

> **No caster-level scaling.** The engine's internal radius helper also
> adds `radiusPerLevel * casterLevel`, but that needs a unit context;
> like modern `GetSpellRadius` this reports the base value. Talent/item
> modifiers (the `modifiedRadius` return) **are** applied, via the
> engine's SpellMod system — but only the **local player's**, since the
> client tracks spell mods for the player alone (there's no way to know
> another caster's talents). All DBCs / mod tables are resident from
> boot, so the call is synchronous with no caching.

### `C_Spell.GetSpellPowerCost(spellIdentifier)`

Returns an array of `SpellPowerCostInfo` tables, or `nil` if the spell
isn't found or has no resource cost. Vanilla spells have exactly one
power cost, so the array holds at most one entry.

```lua
C_Spell.GetSpellPowerCost(10187)   -- Blizzard, Mage with 3/3 Frost Channeling
-- { [1] = { type = 0, name = "MANA", cost = 1190, minCost = 1190,
--           costPercent = 0, costPerSec = 0, requiredAuraID = 0,
--           hasRequiredAura = false } }
-- base cost 1400, 15% reduction applied -> 1190
```

| Field | Meaning |
|-------|---------|
| `type` | `Enum.PowerType` — `0` mana, `1` rage, `2` focus, `3` energy, `4` happiness (the spell's `PowerType`) |
| `name` | power token (`"MANA"`, `"RAGE"`, …) |
| `cost` | **effective** cost for the local player |
| `minCost` | `== cost` (vanilla has no optional-cost component) |
| `costPercent` | the spell's `%`-of-base-resource cost, or `0` for a flat cost |
| `costPerSec` | `0` (vanilla doesn't expose a per-second channel cost in these terms) |
| `requiredAuraID` | `0` (no form/aura-conditional costs in 1.12) |
| `hasRequiredAura` | `false` |

`cost` is the value the engine actually charges — base + level scaling +
`ManaCostPercent`-of-resource + descriptor power-cost mods + the cost
SpellMod (talents like Frost Channeling). It comes from the engine's own
cost helper (`FUN_006e31b0`), so it stays in lockstep with what casting
the spell deducts.

> **Effective vs. base.** This returns the player-modified cost.
> [`C_Spell.GetSpellInfo`](#c_spellgetspellinfospellid)'s `cost` field is
> the **base** `ManaCost` (intrinsic spell data, caster-independent) —
> same base/modified split as
> [`GetSpellRadius`](#c_spellgetspellradiusspellid--getspellradiusslot-booktype).

### `C_Spell.GetSpellReagents(spellID)`

Returns the spell's reagent list as an array of
`{ itemID = N, count = M }` tables, one entry per non-empty reagent
slot. Returns an empty array `{}` for spells that consume no
reagents, or `nil` for invalid spell IDs.

```lua
local list = C_Spell.GetSpellReagents(23028)  -- Arcane Brilliance
-- list = { { itemID = 17020, count = 1 } }   -- 1× Arcane Powder

-- Crafting recipe:
local list = C_Spell.GetSpellReagents(2538)
-- Cooking-style: { { itemID = ..., count = ... }, ... }
```

Reads directly from `Spell.dbc` at the documented reagent offsets
(`+0xA8` for itemIDs, `+0xC8` for counts — verified empirically
from the engine's tooltip-builder; the CMaNGOS-style schema
documented in some emulator sources places these at `+0x110` /
`+0x130`, which is wrong for 1.12.1). Iteration stops at the first
empty slot, matching how the engine walks its own reagent loop.

### `C_Spell.GetSpellSubtext(spellIdentifier)`

Returns the localized "Rank N" / "Passive" / "Racial Passive" /
similar string that appears below the spell's name in the
spellbook. Read directly from `Spell.dbc.Rank[9]` at record `+0x204`
(same locale array shape as `Name[9]` two fields prior).

Accepts the modern `SpellIdentifier` shape — `spellID`, name, name
with rank (`"Fireball(Rank 3)"`), or `|Hspell:N|h` hyperlink. Returns
`nil` for unrecognized input or spells with no rank text.

```lua
C_Spell.GetSpellSubtext(133)        -- "Rank 1"   (lowest-rank Fireball)
C_Spell.GetSpellSubtext(25306)      -- "Rank 12"  (max-rank Fireball)
C_Spell.GetSpellSubtext("Frost Armor")  -- "Rank 1"
```

### `IsPassiveSpell(spellID)` / `IsPassiveSpell(slot, bookType)`

Returns `true` if the spell is passive (no cast bar — applies its effect
as soon as it's learned/equipped), `false` otherwise. Returns `nil` for
invalid spell IDs / out-of-range slots.

Reads `Attributes` bit 6 (`SPELL_ATTR_PASSIVE = 0x40`) directly off the
`Spell.dbc` record. Used by aura libraries and talent code to decide
whether a spell needs cast-bar tracking.

```lua
IsPassiveSpell(6603)   -- true  (Auto Attack)
IsPassiveSpell(133)    -- false (Fireball)

-- spellbook overload mirrors GetSpellInfo's
IsPassiveSpell(1, "spell")   -- true/false depending on slot 1
```

### `C_Spell.IsSpellPassive(spellID)`

Modern table-namespace form of [`IsPassiveSpell`](#ispassivespellspellid--ispassivespellslot-booktype).
Same return semantics; doesn't accept the legacy
`(slot, bookType)` shape.

```lua
C_Spell.IsSpellPassive(6603)   -- true (Auto Attack)
```

### `IsPlayerSpell(spellID)`

Returns `true` if the player currently knows the given spellID, `false`
otherwise. Covers everything granted by `SMSG_LEARNED_SPELL`:

- Trained class abilities (the obvious case)
- Racial abilities
- Talent passives the player has invested in
- **Profession recipes** — including ones from vendors / discovered
  via tradeskill — without needing to have the trade skill window open
- Anything else the engine considers "known"

```lua
IsPlayerSpell(133)     -- true if you have Fireball
IsPlayerSpell(2963)    -- true for a tailor who knows Bolt of Linen
                       --   (works without opening the Tailoring window)
```

> **Only the current rank counts.** For ranked spells (passives,
> trained ranks), only the spellID of the **player's current rank**
> returns `true` — lower-rank IDs return `false` even though the player
> conceptually "has" them. Matches the same semantic Classic Era 1.15.x
> uses: a player with 5/5 Unbreakable Will sees `IsPlayerSpell(14791)`
> (rank 5) as true but rank 4 / rank 3 / etc. as false.
>
> This is by design — the engine's spell-knowledge bitmap stores one
> bit per spellID, and the highest-rank spellID is the one set when
> you train up. To check "do I have at least rank N", use the
> rank-N spellID specifically.

Reads a single bit from the engine's spell-knowledge bitmap at
`[0x00B710FC]` — `(bitmap[spellID >> 5] & (1 << (spellID & 31))) != 0`.
The same lookup the engine itself does internally. No spellbook walk,
no talent walk, no profession-window dependency.

### `CanDualWield()`

Returns `true` if the player can equip a weapon in the off hand.

```lua
if CanDualWield() then ... end   -- true for a warrior/rogue past level 10, etc.
```

Server-side this is a plain flag (`Player::m_canDualWield`) that starts
`false` and is turned on **only** by the `SPELL_EFFECT_DUAL_WIELD` spell
effect — no class has it innately. Exactly one spell in the 1.12 client's
`Spell.dbc` carries that effect: `674` ("Dual Wield"), the trained
warrior / rogue / hunter passive, which lands in the known-spell bitmap
like any learned spell. So `CanDualWield()` is precisely
`IsPlayerSpell(674)` — there is no separate client-visible capability
field to read.

### `IsSpellKnown(spellID, [isPet])`

Returns `true` if the given spellID is currently in the player's (or
pet's) spellbook arrays — the same source the in-game spellbook UI
displays. **Strict semantics**: only counts spells that have a
spellbook button. `isPet` defaults to `false`.

```lua
IsSpellKnown(2050)         -- true if Lesser Heal is trained
IsSpellKnown(2649, true)   -- true if your hunter pet has Growl
IsSpellKnown(133)          -- false on a Priest (Fireball is a Mage spell)
```

> **Not the same as `IsPlayerSpell`.** Modern WoW deliberately splits
> these two: `IsSpellKnown` is the strict "in the spellbook UI" check,
> `IsPlayerSpell` is the broad "any kind of known" query. The split
> matters because:
>
> | Spell type | `IsPlayerSpell` | `IsSpellKnown` |
> |---|---|---|
> | Trained class ability (Fireball) | `true` | `true` |
> | Active talent grant (Mortal Strike) | `true` | `true` |
> | Passive talent (Wand Spec, Imp Fireball) | `true` | **`false`** |
> | Profession recipe (Bolt of Linen) | `true` | **`false`** |
>
> Use `IsPlayerSpell` for "do I have access to this effect at all";
> use `IsSpellKnown` for "is this in my spellbook so I can put it on
> an action bar".

Implementation walks `VAR_PLAYER_SPELLBOOK` (`0x00B700F0`) when
`isPet=false` or `VAR_PET_SPELLBOOK` (`0x00B6F098`) when `isPet=true`.
Verified to match 3.3.5's `Script_IsSpellKnown` semantics — that
function does the same spellbook walk in its inner helper at
`0x0053B4E0` (player array `[0x00BE6D88]`, pet array `[0x00BE7D98]`,
same shape just different addresses).

### `GetSpellBonusDamage(school)`

Returns the local player's flat spell-damage bonus (spell power) for a
given magic school, as a number. `school` is **1-based**: `1` Physical,
`2` Holy, `3` Fire, `4` Nature, `5` Frost, `6` Shadow, `7` Arcane. Raises
a usage error for a missing / out-of-range school; returns `0` before
you're in the world.

```lua
GetSpellBonusDamage(3)   -- your +Fire spell damage
GetSpellBonusDamage(6)   -- your +Shadow spell damage
```

### `GetSpellBonusHealing()`

Returns the local player's flat healing bonus, as a number.

```lua
GetSpellBonusHealing()   -- your +Healing
```

> **Damage is an exact field read; healing is derived.**
>
> `GetSpellBonusDamage` reads the client's fully-computed value directly
> from the CGPlayer sub-struct (`PLAYER_FIELD_MOD_DAMAGE_DONE_POS − _NEG`
> per school). That field isn't in the *broadcast* descriptor — the
> vanilla server never sends it there, which is why it looks absent — but
> the client keeps a computed copy for its own use, with gear, enchants,
> buffs, talents, and set bonuses all baked in. So the value is exact and
> complete. (Same source nampower's `GetSpellPower` reads.)
>
> `GetSpellBonusHealing` has no such field — vanilla 1.12 never had a
> healing-done field at all (confirmed in-game: toggling a pure +healing
> item moved no field in the player struct) — so it's **derived** in two
> parts, both from `Spell.dbc`: (1) flat healing = the sum of every
> `SPELL_AURA_MOD_HEALING_DONE` off gear equip-effects, item enchants
> (permanent + weapon oils), random suffixes, and active buffs; plus (2)
> talent/buff stat-conversions, mirroring the server's own
> `SpellBaseHealingBonusDone` — the sum of `SPELL_AURA_MOD_SPELL_HEALING_OF`
> `_STAT_PERCENT` (aura 175) × total Spirit / 100 over your passive talents
> (Priest *Spiritual Guidance*) and active buffs. Part 2 reads the talent
> percent directly, so it's **generic** (any aura-175 talent/buff, including
> Turtle customs) and exact — e.g. Spiritual Guidance 5/5 with 182 Spirit
> adds `int(182 × 25 / 100) = 45`.
>
> Residual: assumes talent ranks supersede (only the learned rank is known)
> and misses set-bonus healing granted via a set-completion spell.

### `IsUsableSpell(spell)` / `IsUsableSpell(slot, bookType)`

Returns `(usable, noMana)` for a spell, matching the modern
3.0+ signature. Returns `(1, nil)` when the spell is castable,
`(nil, 1)` when only mana is preventing it, `(nil, nil)` for any
other reason (unknown spell, dead, etc.). Matches the `1`/`nil`
return convention of the existing `Script_IsUsableAction`.

Two arg shapes accepted:

- **`IsUsableSpell(spellID)`** — direct spellID lookup.
- **`IsUsableSpell(slot, bookType)`** — `bookType` is `"spell"`
  (player) or `"pet"`. Resolves to a spellID via the same engine
  spellbook arrays `GetSpellInfo`/`GetSpellLink` walk.

```lua
IsUsableSpell(133)           -- Fireball: 1, nil if you have mana, nil, 1 if not
IsUsableSpell(1, "spell")    -- player spellbook slot 1
```

> **What this function checks:**
>
> 1. Player knows the spell (engine's spell-knowledge bitmap —
>    covers trained class abilities, talent passives, racials,
>    profession recipes).
> 2. Player is alive (HEALTH > 0).
> 3. Spell is not on cooldown (engine's per-spell cooldown helper).
> 4. Player has enough of the spell's power type for the base cost
>    (mana / rage / focus / energy / happiness) — *only* this
>    failure sets `noMana=true`.
> 5. Player has all required reagents in bags (Spell.dbc
>    Reagent[8] / ReagentCount[8]).
>
> **What this function doesn't check** (different concerns or
> post-vanilla concepts): silence, GCD, stance/form, range, target
> type, line-of-sight, casting state.
>
> Verified empirically on Turtle WoW for the mana branch: Renew rank
> 3 (cost 105) is reported usable at 144 mana and unusable at 39
> mana, transitioning at exactly the cost boundary. Cooldown and
> reagent checks ship in the same implementation but haven't been
> exercised in-game; if you find an inconsistency, the reagent
> offsets (+0x110 / +0x130) and cooldown helper (`0x006E2EA0`) are
> the components to verify.

### `C_Spell.IsSpellUsable(spellID)`

Modern table-namespace form. Same logic as
[`IsUsableSpell(spellID)`](#isusablespellspell--isusablespellslot-booktype)
but returns proper booleans (`isUsable`, `insufficientPower`) per
the `C_Spell.*` convention rather than `1`/`nil` pairs.

```lua
local usable, noMana = C_Spell.IsSpellUsable(133)
-- usable=true, noMana=false  → cast it
-- usable=false, noMana=true  → drink up
-- usable=false, noMana=false → unknown spell, dead, or other block
```

### `C_Spell.GetSpellCooldown(spellIdentifier)`

Returns a `SpellCooldownInfo` table for the given spell, or `nil` if
the identifier doesn't resolve to a `Spell.dbc` row. Modern
table-shape variant of vanilla's `GetSpellCooldown(slot, bookType)` —
accepts a spellID directly without forcing the caller to resolve a
spellbook slot first.

```lua
local info = C_Spell.GetSpellCooldown(1953)   -- Blink
-- on cooldown:   { startTime=728565.79, duration=15, isEnabled=true, modRate=1 }
-- off cooldown:  { startTime=0,         duration=0,  isEnabled=true, modRate=1 }
```

| Field | Type | Notes |
|-------|------|-------|
| `startTime` | number | Engine tick count (seconds) when the cooldown began. Same epoch as `GetTime()`, so `info.startTime + info.duration` is the absolute time the cooldown ends. `0` when no cooldown active. |
| `duration` | number | Cooldown length in seconds; `0` when no cooldown active. |
| `isEnabled` | boolean | `false` when the cooldown is "on hold" (e.g. some channeled abilities). `true` for normal cooldowns. |
| `modRate` | number | Always `1.0` — vanilla has no haste-on-cooldown mechanic. |

Modern-only fields (`activeCategory`, `timeUntilEndOfStartRecovery`,
`isOnGCD`) read as `nil` since they have no vanilla source.

Returns `nil` if the resolved spellID is `0` or doesn't have a
`Spell.dbc` row.

### `C_Spell.IsCurrentSpell(spellIdentifier)`

Returns `true` if the spell is currently being cast, queued mid-GCD,
or channeled by the player; `false` otherwise (including for
unresolvable identifiers).

```lua
C_Spell.IsCurrentSpell(11366)              -- true while casting Pyroblast
C_Spell.IsCurrentSpell("Pyroblast")        -- same, via name
C_Spell.IsCurrentSpell(GetSpellLink(7620)) -- true while channeling Fishing
```

Useful for action-bar addons that want to highlight the active /
queued button — modern action bars gate the "currently casting" glow
on this. Reads three engine slots and returns true on any match:

- `VAR_CURRENT_CAST_SPELL` (`0x00CECA88`) — cast-bar spellID. Written
  by `FUN_006E4AD0` when a new cast begins; cleared on cast end.
- `VAR_QUEUED_CAST_SPELL` (`0x00CECAA8`) — spell that was active when
  a new cast superseded it mid-GCD. Restored to current when the new
  cast ends, so checking it here covers the "queued to cast next"
  half of modern's documented semantics.
- `UNIT_FIELD_CHANNEL_SPELL` (descriptor `+0x228`) on the player —
  covers channeled abilities (Fishing, Drain Soul, Ritual of
  Summoning, etc.).

### `C_Spell.IsSelfBuff(spellID)`

Returns `true` if every active effect on the spell targets only the
caster — every effect's implicit target (A and B) must be either
`TARGET_NONE` (0) or `TARGET_SELF` (1).

```lua
C_Spell.IsSelfBuff(1006)   -- Inner Fire        → true
C_Spell.IsSelfBuff(7302)   -- Frost Armor       → true
C_Spell.IsSelfBuff(1459)   -- Arcane Intellect  → false (targets friendly)
C_Spell.IsSelfBuff(133)    -- Fireball          → false (targets enemy)
```

Walks `Spell.dbc.EffectImplicitTargetA[3]` and
`EffectImplicitTargetB[3]`. Spells with no active effects (a
degenerate case for normal content) return `false`.

### `C_Spell.SpellHasRange(spellIdentifier)` / `SpellHasRange(slot, bookType)`

Returns `true` if the spell has a non-zero min or max range — i.e.
it's a targeted ability you cast at a unit / location rather than a
self-cast (which has range `0`).

```lua
C_Spell.SpellHasRange(133)    -- Fireball       → true (40yd)
C_Spell.SpellHasRange(1006)   -- Inner Fire     → false (self buff)
SpellHasRange(10, "spell")    -- 10th spellbook slot in player book
```

Looks up `Spell.dbc.RangeIndex` (`+0x90`) → `SpellRange.dbc` row,
then tests `minRange > 0 or maxRange > 0`. Vanilla 1.12 doesn't ship
this function at all; 3.3.5+ added it as the `(spellIdentifier)`
form only. We expose both the modern namespaced form (any
`SpellIdentifier`) and a positional `SpellHasRange(slot, bookType)`
matching the dual-signature shape used elsewhere in this backport
(`GetSpellInfo`, `GetSpellLink`, etc.). The `bookType` argument
follows the same convention as `GetSpellName(slot, bookType)`:
`"spell"` (or any non-pet value) → player book, `"pet"` → pet book.

### `C_Spell.IsSpellInRange(spellIdentifier, targetUnit)`

Returns `true` if the spell is in range of `targetUnit`, `false` if
out of range, and `nil` when the range check doesn't apply — a
rangeless spell (self buff), or an unresolvable spell / unit.

`spellIdentifier` is a spellID, spell link, or the name of a spell in
the player's spellbook. `targetUnit` is a unit token.

```lua
C_Spell.IsSpellInRange(133, "target")      -- Fireball → true / false
C_Spell.IsSpellInRange("Fireball", "target")
C_Spell.IsSpellInRange(1006, "target")     -- Inner Fire (self) → nil
```

Uses the engine's own geometric range core (the same one the action
button range-out coloring uses via `IsActionInRange`): it derives the
spell's min/max range — folding in the target's bounding radius — and
compares center-to-center distance, so the boundary matches the
client exactly, melee and min-range ("too close") spells included.

Range-only, matching retail: it ignores line of sight, and it does
**not** reject wrong-faction targets (a friendly-only heal still
returns a range answer against an enemy). Check target validity
separately if you need it. Absent tokens (e.g. `"target"` with no
target) return `nil`; a genuinely unrecognized token string raises a
Lua error, same contract as `UnitHealth("garbage")`.

### `C_Spell.IsAutoAttackSpell(spellID)`

Returns `true` if the spell is the melee auto-attack — spell ID
`6603` ("Auto Attack"), used by every class.

```lua
C_Spell.IsAutoAttackSpell(6603)              -- true
C_Spell.IsAutoAttackSpell(133)               -- false
```

See [`C_SpellBook.IsAutoAttackSpellBookItem`](#c_spellbookisautoattackspellbookitemslot-booktype)
for the spellbook-slot variant.

### `C_Spell.IsRangedAutoAttackSpell(spellID)`

Returns `true` if the spell is one of the two ranged auto-attacks —
spell `75` (Hunter "Auto Shot") or spell `5019` ("Shoot" wand).

```lua
C_Spell.IsRangedAutoAttackSpell(75)          -- true (Auto Shot)
C_Spell.IsRangedAutoAttackSpell(5019)        -- true (Shoot wand)
C_Spell.IsRangedAutoAttackSpell(6603)        -- false (melee)
```

Tests the auto-repeat flag in `Spell.dbc.AttributesEx2` (bit `0x20`).
Only Auto Shot and Shoot carry it. The on-cast ranged abilities —
Aimed Shot and Multi-Shot — set the general `RANGED` bit but not the
auto-repeat flag, so they return `false`. The flag also covers any
future auto-repeating spell a private server adds. Verified against
`Spell.dbc`.

See [`C_SpellBook.IsRangedAutoAttackSpellBookItem`](#c_spellbookisrangedautoattackspellbookitemslot-booktype)
for the spellbook-slot variant.

### `C_Spell.IsNextMeleeSpell(spellID)`

Returns `true` if the spell is an "on next swing" melee ability.
These abilities queue and land on your next weapon swing instead of
an instant cast (Heroic Strike, Cleave, Maul, Raptor Strike).

```lua
C_Spell.IsNextMeleeSpell(78)     -- Heroic Strike → true
C_Spell.IsNextMeleeSpell(6807)   -- Maul → true
C_Spell.IsNextMeleeSpell(116)    -- Frostbolt → false
```

Tests the two on-next-swing bits in `Spell.dbc.Attributes`
(`0x04` and `0x400`).

### `C_Spell.ResetsMeleeSwing(spellID)`

Returns `true` if casting the spell resets your melee swing timer.
Cast-time spells reset it when you cast them (Fireball, Frostbolt,
Polymorph, wand Shoot). Instant abilities like Sinister Strike do not,
so a rogue can weave them between swings.

```lua
C_Spell.ResetsMeleeSwing(133)    -- Fireball → true
C_Spell.ResetsMeleeSwing(1752)   -- Sinister Strike → false
C_Spell.ResetsMeleeSwing(1464)   -- Slam → false (flagged "no reset")
```

Mirrors the server's `IsMeleeAttackResetSpell`: `true` when the
spell's `InterruptFlags` has the auto-attack bit (`0x08`) and its
`AttributesEx2` does not have the "no reset auto actions" bit
(`0x20000`). Both fields come from `Spell.dbc`. Slam carries that
second bit, so it keeps its swing.

### `C_Item.GetWeaponEnchantInfo()`

Returns the modern 12-tuple matching WotLK+'s extended
`GetWeaponEnchantInfo` signature, including the **temp-enchant IDs**
that vanilla 1.12's 8-return global omits.

```
hasMain, mainExpire, mainCharges, mainEnchantID,
hasOff,  offExpire,  offCharges,  offEnchantID,
hasRanged, rangedExpire, rangedCharges, rangedEnchantID
   = C_Item.GetWeaponEnchantInfo()
```

```lua
-- Apply Brilliant Mana Oil to mainhand, then:
local has, expireMs, charges, enchantID = C_Item.GetWeaponEnchantInfo()
-- has = true, expireMs ≈ 1800000, charges = 5, enchantID = <oil's enchant>
```

Reads the **temporary** enchant slot (`ITEM_FIELD_ENCHANTMENT`
slot 1 at descriptor `+0x4C`) — the same slot oils, sharpening
stones, and poisons populate and the engine drains as they expire.
This is what modern's `GetWeaponEnchantInfo` measures.

The permanent enchant (Crusader, Mongoose, etc., in slot 0 at
`+0x40`) is **not** reported here — that's a separate field and
modern's `GetWeaponEnchantInfo` doesn't expose it either. Get the
permanent enchant ID by parsing `GetInventoryItemLink("player",
slot)` (the link includes it as the 2nd numeric field).

Vanilla 1.12's global `GetWeaponEnchantInfo` is unchanged — old
addons reading positions 4..8 by index still work.

Equivalent to the extension of `GetWeaponEnchantInfo` introduced
in 3.x.

### `C_Item.GetEnchantInfo(enchantID)`

Resolves an item-enchantment ID — the `enchantID` returned by
[`C_Item.GetWeaponEnchantInfo`](#c_itemgetweaponenchantinfo) for a
weapon's temporary enchant, and the same IDs item permanent enchants
use — into a table:

```lua
local info = C_Item.GetEnchantInfo(enchantID)
-- info.enchantID = <id>
-- info.name      = "Crusader"        -- localized display name
-- info.effects   = { {type=1, amount=0, arg=20007} }
-- info.spellID   = 20007             -- spell-type enchants only
```

```lua
-- A proc/equip enchant → chain its spellID into C_Spell:
local info = C_Item.GetEnchantInfo(1900)          -- "Crusader"
if info.spellID then
    print(C_Spell.GetSpellDescription(info.spellID))  -- the proc's text
end
```

`effects` is an array of the record's non-empty effect slots, each
`{ type, amount, arg }` where `type` is the standard
`ITEM_ENCHANTMENT_TYPE`:

| type | meaning | carries |
|------|---------|---------|
| 1 | combat-proc spell | `arg` = spellID |
| 2 | weapon damage | `amount` = +damage |
| 3 | equip spell / aura | `arg` = spellID |
| 4 | resistance / armor | `amount` = +value |
| 5 | stat | `arg` = stat index, `amount` = value |
| 6 | totem | — |
| 7 | use spell | `arg` = spellID |

For spell types (1/3/7) `arg` is a spellID feedable into
[`C_Spell.GetSpellInfo`](#getspellinfospellid--getspellinfoslot-booktype) /
`GetSpellDescription`; the first such id is also surfaced at top level
as `spellID` for convenience (absent for non-spell enchants like
sharpening stones).

Returns `nil` for a non-numeric / non-positive id, an out-of-range
id, or a record with no name.

Reads `SpellItemEnchantment.dbc` (records `0x00C0D7D8`, count
`0x00C0D7DC`) — the 24-column table every enchant ID indexes:
`Type[3]@+0x04`, `Amount[3]@+0x10`, `EffectArg[3]@+0x28`,
`Name[8]@+0x34` (locale-indexed). The layout was verified by parsing
the on-disk DBC against known records (Crusader 1900 → type 1, arg
20007; Sharpened +3 → type 2, amount 3). Fully resident from boot, so
it answers for any enchant ID with no caching or round-trip.

Lives in `C_Item` (not `C_Spell`) because the id originates from
`C_Item.GetWeaponEnchantInfo` and the concept is an item enchantment —
`SpellItemEnchantment` is just the DBC's internal name (enchants are
*implemented* via spell effects).

> **Not derivable: the source item.** The enchant record holds no
> back-reference to the item that applied it, and vanilla has no
> client-side reverse index (enchantID → item). Finding it would need
> an external scraped DB (pfQuest/Questie-style).

### `IsHarmfulSpell(spell)` / `IsHelpfulSpell(spell)`

Classify a spell as offensive (`IsHarmfulSpell`) or non-offensive
(`IsHelpfulSpell`) without parsing its tooltip. Accept either form
the spellbook-aware globals use:

```lua
IsHarmfulSpell(spellID)             -- by ID
IsHarmfulSpell(slot, "spell")       -- player spellbook slot
IsHarmfulSpell(slot, "pet")         -- pet spellbook slot
-- same calling shapes for IsHelpfulSpell
```

Examples:

```lua
IsHarmfulSpell(133)     -- true  (Fireball)
IsHarmfulSpell(118)     -- true  (Polymorph)
IsHelpfulSpell(2061)    -- true  (Flash Heal)
IsHelpfulSpell(1243)    -- true  (Power Word: Fortitude)
IsHarmfulSpell(2061)    -- false
```

Reads `Spell.dbc.AttributesEx` (record `+0x1C`) bit `0x80` — the
same `SPELL_ATTR_EX_NEGATIVE` bit CMaNGOS uses to mark spells as
debuffs server-side. `IsHarmfulSpell` is true iff that bit is set;
`IsHelpfulSpell` is true iff the spell exists and the bit is NOT
set. Both return `false` for invalid spellIDs.

> Vanilla 1.12 doesn't have a dedicated "positive" flag, so the
> helpful side is the rough complement of harmful. For utility
> spells with no clear orientation (Aspect of the Cheetah,
> Stealth, ground-targeted AOEs), modern WoW sometimes returns
> false for both; we return `true` for helpful as a safer default
> for addons gating on "is this castable on me?" logic. Compute
> precise modern semantics by also inspecting effect implicit
> targets if you need them.

### `C_Spell.IsSpellHarmful(spellID)` / `C_Spell.IsSpellHelpful(spellID)`

Same classification logic as the globals above, exposed in the modern
`C_Spell` namespace. Don't accept the legacy `(slot, bookType)`
shape.

```lua
C_Spell.IsSpellHarmful(133)     -- true (Fireball)
C_Spell.IsSpellHelpful(2061)    -- true (Flash Heal)
```

Equivalent to `C_Spell.IsSpellHarmful` / `C_Spell.IsSpellHelpful`
introduced in 11.x.

### `GetSpellSchool(spellID)`

Returns `(schoolID, schoolName)` for any spell — known to the player
or not. `schoolID` is 1-based; `schoolName` is the locale-independent
English name.

| schoolID | schoolName |
|---------:|------------|
| 1 | `"Physical"` |
| 2 | `"Holy"` |
| 3 | `"Fire"` |
| 4 | `"Nature"` |
| 5 | `"Frost"` |
| 6 | `"Shadow"` |
| 7 | `"Arcane"` |

Reads directly from `Spell.dbc` record `+0x04`. Vanilla 1.12 stores
School as a single 0-based integer (no multi-school `SchoolMask`
combinations — that's a TBC+ thing), so a spell belongs to exactly
one school.

Returns `nil` for invalid spellIDs (out of range or unpopulated
`Spell.dbc` slot).

```lua
local id, name = GetSpellSchool(133)   -- 3, "Fire"   (Fireball)
local id, name = GetSpellSchool(116)   -- 5, "Frost"  (Frostbolt)
local id, name = GetSpellSchool(635)   -- 2, "Holy"   (Holy Light)
```

Useful for combat-log breakdown addons, dispel-eligibility checks,
resistance-aware aura libraries, and damage-meter school tagging.
Previously addons either maintained hardcoded `spellID → school`
tables or scanned tooltips for the first-line color tag.

### `CastSpellNoToggle(name | spellID)`

Spam-safe variant of `CastSpellByName` that won't toggle off an
already-active spell. Covers both kinds of vanilla-toggle abilities:

- **Auto-repeat** — Shoot, Auto-Shot, Wand. Tracked via the engine's
  active-auto-repeat global.
- **Self-aura toggles** — shapeshift (Cat/Bear/Travel/Moonkin/
  Shadowform), stance (Battle/Defensive/Berserker), aspect (Hunter),
  seal (Paladin), blessing-on-self, etc. Tracked via the unit
  descriptor's aura array.

If either toggle is already on for the requested spell, the call is
a no-op — exactly what `/cast !SpellName` does in 2.3.2+ clients,
but expressed as a vanilla Lua call.

| Engine state                  | Behavior            | Return  |
|-------------------------------|---------------------|---------|
| Nothing toggled               | Starts the cast     | `true` if the spell is now active, else `false` |
| This spell already auto-repeating | No-op           | `true`  |
| This spell's aura already on (form / stance / aspect) | No-op | `true`  |
| A *different* auto-repeat is active | No-op         | `false` (don't disrupt the other auto-repeat) |

Replaces both the action-bar scan-and-skip pattern and the
form-already-active check:

```lua
-- Before (auto-repeat case)
for i = 1, 120 do
    if IsAutoRepeatAction(i) then return end
end
CastSpellByName("Shoot")

-- Before (shapeshift case)
if not (UnitBuff("player", "Cat Form") or GetShapeshiftFormID() == CAT_FORM) then
    CastSpellByName("Cat Form")
end

-- After (both cases)
CastSpellNoToggle("Shoot")
CastSpellNoToggle("Cat Form")
```

Accepts either a name string or a spellID number:

```lua
CastSpellNoToggle("Shoot")         -- by name (matches what CastSpellByName accepts)
CastSpellNoToggle(75)              -- by spellID (Auto Shot rank 1)
CastSpellNoToggle("Aspect of the Hawk")
CastSpellNoToggle("Battle Stance")
```

String input matches case-insensitively and tolerates a trailing
`(Rank N)` suffix the same way `CastSpellByName` itself does —
`"Shoot"` and `"Shoot(Rank 1)"` both compare equal to a Shoot that's
already auto-repeating.

Reads `[VAR_ACTIVE_AUTO_REPEAT_SPELL]` (`0x00CEAC30`) for the auto-
repeat check, and the engine's `FUN_SPELL_IS_TOGGLE_AURA_ACTIVE`
(`0x004B36F0`) for the aura-active check. Delegates to
`Script_CastSpellByName` (`0x004B4AB0`) for the actual cast — same
resolution semantics (rank picking, target rules, etc.) as the
engine's own global.

Using this from inside a macro action slot? See
[`CastSpellNoToggle` as a macro cast line](#castspellnotoggle-as-a-macro-cast-line) below for the additional
parser support that makes the slot tag correctly for action-bar UIs.

### `C_Spell.CastAtCursor(spellIDOrName)`

Casts a ground-target spell at the player's current cursor world
position, bypassing the manual click on the AoE reticle the engine
would otherwise require. ClassicAPI's analog of modern's
`/cast [@cursor] Blizzard`. Returns `true` when the cursor-placement
leg landed; `false` for non-ground-target spells (cast still fires
normally on the current target), unknown spells, cursor over UI /
off-screen, etc.

Accepts a **spellID** or a **spell name**:

```lua
C_Spell.CastAtCursor(10)                  -- Blizzard Rank 1 (exact spellID)
C_Spell.CastAtCursor(2120)                -- Flamestrike Rank 1
C_Spell.CastAtCursor("Blizzard")          -- highest known rank
C_Spell.CastAtCursor("Blizzard(Rank 6)")  -- that specific rank
```

A **numeric** spellID casts that *exact* spell — every rank is its own
spellID, so `10` casts Blizzard Rank 1 (not the highest known rank). A
**name** goes through the engine's own resolver, which parses a trailing
`(Rank N)`; a bare name casts the highest rank you know.

Two-stage internally:

1. Initiates the cast: a numeric spellID resolves to its exact spellbook
   slot (`Spell::Lookup::FindSpellbookSlot`, so the requested rank is the one
   that fires), a name goes through `FUN_RESOLVE_SPELL_NAME_TO_SLOT`; either
   way it dispatches through the engine's cast dispatcher. If the spell needs
   a ground target, the engine enters placement mode and arms the AoE reticle.

2. Refreshes the cursor's screen→world raycast via
   `FUN_REFRESH_CURSOR_RAYCAST` (the same internal the engine fires
   on every mouse move when placement is active), confirms the
   cursor hit terrain (not a UI frame or a unit), and commits
   placement via `FUN_COMMIT_PLACEMENT_COORDS` with the resolved
   `(x, y, z)`. Cancels placement cleanly via `FUN_STOP_PLACEMENT`
   when the cursor isn't on terrain or when the spell wanted a
   unit-target rather than ground.

For non-placement spells (regular target casts, self-buffs, etc.)
the cast fires normally; the placement-resolve no-ops since the
engine never set the placement flag, and we return `false`.

The companion item version is
[`C_Item.UseAtCursor`](#c_itemuseatcursoriteminfo) — same chain via
the item-use path for grenades / on-use ground-target items.

### `C_Spell.CastAtUnit(spellIDOrName, unit)`

Casts a spell **at `unit`, whatever its target type** — ClassicAPI's
analog of modern's `/cast [@unit] Spell`:

- **Ground-target spells** (Flamestrike, Blizzard, Rain of Fire, …) are
  placed at the unit's feet, bypassing the AoE reticle click.
- **Unit-target / normal spells** (Frostbolt, Renew, a buff, …) fire
  directly on the unit — regardless of your current selection, so you can
  buff `"party1"` or heal `"player"` while an enemy stays targeted.

Works with any unit token (`"target"`, `"player"`, `"mouseover"`,
`"party1"`, `"raid7"`, …). Returns `true` when the spell was cast at the
unit (fired or placed); `false` for a spell not in your spellbook, a unit
with no resolvable position, or a unit the engine won't accept as a target
for that spell (wrong faction, out of range).

The first argument takes a spellID or a spell name, with the same
exact-rank / `(Rank N)` semantics as `CastAtCursor`.

```lua
-- ground-target: dropped at the unit's feet
C_Spell.CastAtUnit(2120, "target")            -- Flamestrike at the target's feet
C_Spell.CastAtUnit("Blizzard(Rank 6)", "target")

-- unit-target / normal: cast straight on the unit, ignoring current target
C_Spell.CastAtUnit("Frostbolt", "target")
C_Spell.CastAtUnit("Renew", "player")         -- heal yourself mid-fight
C_Spell.CastAtUnit("Power Word: Fortitude", "party1")
```

The unit is resolved once to its object; the cast is dispatched with the
unit's **GUID** as the implicit target (the engine substitutes it exactly
where it would the current selection), so a unit-target spell fires
immediately at that unit. A ground spell ignores the GUID and enters
placement mode instead, which is then committed at the unit's world
position (read from its `GetPosition` virtual — the same one
`CheckInteractDistance` / `UnitInRange` use) via
`FUN_COMMIT_PLACEMENT_COORDS`. The engine can't be left armed in placement
mode on failure: if no placement is active after dispatch the cast already
went out, and a non-ground placement (an unaccepted unit target) is
cancelled. A genuinely unrecognized unit-token string raises the engine's
standard "Unknown unit" error, matching `UnitHealth("garbage")`.

The companion item version is
[`C_Item.UseAtUnit`](#c_itemuseatunititeminfo-unit).

### `C_Spell.CancelSpellByID(spellID)` / `CancelSpellByName(name)`

Cancel a buff on the player by spellID or by spell name. Modern WoW's
`CancelUnitBuff` reduces to these two primitives internally; both are
player-only (vanilla server only accepts `CMSG_CANCEL_AURA` for the
local player's own auras).

```lua
C_Spell.CancelSpellByID(2378)      -- Cancel "Find Herbs"
CancelSpellByName("Cat Form")       -- Drop shapeshift
```

| Function | Arg | Behavior |
|----------|-----|----------|
| `C_Spell.CancelSpellByID(spellID)` | spellID (number) | Sends `CMSG_CANCEL_AURA` for `spellID` directly. No client-side check that the buff is actually present — server validates and silently no-ops if not. |
| `CancelSpellByName(name)` | name (string, case-insensitive) | Walks the player's buff range (`UnitBuff` slots 0..31) for the first aura whose Spell.dbc localized name matches, then cancels by its spellID. Buff-only — debuffs are skipped. |

Calls `FUN_006E7040` (`0x006E7040`) directly with the spellID. This
**bypasses** `Script_CancelPlayerBuff`'s client-side gates — the
per-buff cancelable flag at `[entry+0x0A] & 0x01` and the fallback
`AttributesEx & 0x04` check on the spell record. Trade-off: the server
is now the sole authority on what's cancelable. Non-cancelable auras
(proc-buffs, certain world buffs) still get rejected — just server-side
instead of client-side, which is the same effective behavior as
modern WoW's `C_Spell.CancelSpellByID`.

For invalid input (non-positive spellID, OOR, or a Spell.dbc empty
slot), both functions silently no-op. No `lua_error`, no event fired.

Spell name matching is case-insensitive via `_stricmp`. Rank-suffixed
input (e.g. `"Power Word: Fortitude(Rank 4)"`) is **not** parsed —
pass the plain name. Multi-rank buffs match on first-found; if you
have multiple ranks of the same buff active (unusual but possible with
e.g. paladin blessings before talent merge), the first slot wins.

### `C_Spell.UnitCastingInfo(unit)` / `C_Spell.CastingInfo()`

Returns the unit's in-progress **regular cast** (not a channel), or
`nil` if it isn't casting. `C_Spell.CastingInfo()` is
`C_Spell.UnitCastingInfo("player")` without the unit-token lookup.

> **Under `C_Spell`, not the global `UnitCastingInfo`.** Modern WoW
> exposes these as globals, but a global here would clobber addons that
> ship their own vanilla cast-tracking via the `_G.UnitCastingInfo or
> <fallback>` idiom — e.g. ShaguTweaks' `libcast` scrapes the combat
> log for **remote/enemy** casts the 1.12 engine never exposes. If we
> occupied the global, those addons would adopt our player-only version
> and lose their (superior, for that case) fallback. So we cede the
> global names and register under `C_Spell` instead. Same for
> `UnitChannelInfo`/`ChannelInfo`.

```
name, displayName, textureID, startTimeMs, endTimeMs, isTradeskill,
  castID, notInterruptible, castingSpellID, castBarID, delayTimeMs
    = C_Spell.UnitCastingInfo(unit)
```

```lua
-- mid-cast:
C_Spell.UnitCastingInfo("player")
-- "Conjure Food", "Conjure Food", "Interface\\Icons\\INV_Misc_Food_...",
--  907928268, 907931268, false, nil, false, 28612, nil, 0
```

`startTimeMs`/`endTimeMs` share `GetTime()`'s epoch (`GetTime()*1000`),
so progress is `(GetTime()*1000 - startTimeMs) / (endTimeMs - startTimeMs)`.

> **Works for any unit.** Vanilla 1.12 stores no cast times on the
> CGUnit (the cast bar is Lua-driven off `SPELLCAST_START`), so we
> source them two ways: the **local player** is self-tracked on a
> per-frame tick (`VAR_CURRENT_CAST_SPELL`, stamping `startTimeMs = now`,
> `endTimeMs = now + effective cast time`); **other units** come from a
> co-hook on `SMSG_SPELL_START` (`Spell::Cast`), the one packet carrying
> another unit's cast with a server-authoritative cast time, cached per
> caster GUID. So `C_Spell.UnitCastingInfo("target")` now backs
> enemy/target/focus/nameplate cast bars. Instant casts return `nil` (no
> cast bar). Aborted remote casts (interrupt, death, movement, cancel)
> clear the same tick — a co-hook on the engine's own
> `CGUnit_C::ClearCastingSpell` (the choke point every "unit stopped
> casting" path funnels through) evicts the cached cast the moment the
> engine stops the unit's cast visual, backstopped by co-hooks on the
> `SMSG_SPELL_FAILURE` / `SMSG_SPELL_FAILED_OTHER` packets for servers
> that broadcast them (Turtle's core doesn't — its interrupt propagation
> reaches observers through SuperWoW, which the choke-point hook catches
> too). Verified: Kick / Earth Shock / Counterspell each clear the
> victim's bar, while non-interrupting damage doesn't false-clear it.
> Best-effort for remote units, in three ways: only casts that began while
> the unit was in range are seen; the remote `startTimeMs`/`endTimeMs` are
> shifted later by roughly your latency (1.12's `SMSG_SPELL_START` carries
> only a single `castTime`, so we stamp `start = now, end = now + castTime`
> on receipt — 3.3.5+ avoids the skew because its packet carries *both*
> total and remaining cast time and back-dates `start = now − elapsed`;
> the 1.12 packet has no field to recover `elapsed` from); and remote
> **pushback is invisible** — the server sends `SMSG_SPELL_DELAYED` only
> to the affected caster, so another unit's bar can't stretch when they
> take damage. The **local player is unaffected** by all of this — its
> cast is detected client-side at the moment of cast, no packet involved,
> and its pushback is tracked.
>
> `isTradeskill` is real — the spell's `SPELL_ATTR_TRADESPELL` flag, so
> profession recipe casts (smelting, cooking/enchanting recipes, …)
> return `true` while regular spells return `false`. `endTimeMs` reflects
> the **effective** cast time (the engine's own cast-time helper: base +
> level scaling + cast-time talents like Improved Frostbolt + the
> haste/cast-speed multiplier), so it matches the in-game cast bar — e.g.
> a Mage's talented Frostbolt reads 2.5s, not the 3.0s base. `castID` is
> the cast's **castGUID** — the exact string the
> [`UNIT_SPELLCAST_*`](#unit_spellcast_-events) events carry for this cast,
> so you can correlate the polled info with the events (works for the player
> and other units). Fields vanilla can't fill are structurally-correct
> placeholders: `castBarID` = `nil`, `notInterruptible` = `false`,
> `delayTimeMs` = `0`.

### `C_Spell.UnitChannelInfo(unit)` / `C_Spell.ChannelInfo()`

Returns the unit's in-progress **channel**, or `nil` if it isn't
channeling. `C_Spell.ChannelInfo()` is `C_Spell.UnitChannelInfo("player")`
without the token lookup. (Under `C_Spell` rather than the global name
for the same reason as
[`C_Spell.UnitCastingInfo`](#c_spellunitcastinginfounit--c_spellcastinginfo).)

```
name, displayName, textureID, startTimeMs, endTimeMs, isTradeskill,
  notInterruptible, spellID = C_Spell.UnitChannelInfo(unit)
```

```lua
C_Spell.ChannelInfo()
-- "Blizzard", "Blizzard", "Interface\\Icons\\Spell_Frost_IceStorm",
--  907894314, 907902314, false, false, 10187
```

The channeled spellID **is** broadcast for every unit
(`UNIT_FIELD_CHANNEL_SPELL`), so `C_Spell.UnitChannelInfo("target")` works
on other units. Timing now comes from the same `SMSG_SPELL_START` co-hook
as casts: when we observed the channel begin, the remote unit returns full
**start/end times** (validated against the live `UNIT_FIELD_CHANNEL_SPELL`
so a stale cache entry never applies to a different/ended channel);
otherwise it falls back to `name`/`displayName`/`textureID`/`spellID` with
**`nil` times**. The player path is unchanged (full timing). Same
placeholder fields as `C_Spell.UnitCastingInfo`.

### `C_Spell.GetSpellLevelInfo(spellID)`

Returns the raw `Spell.dbc` level fields for a spell:

```
spellLevel, baseLevel, maxLevel = C_Spell.GetSpellLevelInfo(spellID)
```

- `spellLevel` — the rank's effective level (drives the target-level rule
  below and per-level scaling).
- `baseLevel` — the spell's base level.
- `maxLevel` — the level at which per-level scaling caps (`0` = no cap).

Returns `nil` for an invalid / unpopulated spellID.

```lua
C_Spell.GetSpellLevelInfo(14752)   -- 30, 30, 40  (Divine Spirit Rank 1)
C_Spell.GetSpellLevelInfo(14818)   -- 40, 40, 50  (Divine Spirit Rank 2)
```

### `GetSpellRequiredTargetLevel(spellID)`

Also `C_Spell.GetSpellRequiredTargetLevel(spellID)`. Returns the minimum
level a **target** must be for this exact spell (rank) to apply — e.g.
Divine Spirit Rank 1 can only be cast on a target level **20 or higher**.
Returns `0` when the spell has no such restriction, or `nil` for an
invalid spellID.

```lua
GetSpellRequiredTargetLevel(14752)   -- 20   (Divine Spirit Rank 1)
GetSpellRequiredTargetLevel(14818)   -- 30   (Divine Spirit Rank 2)
GetSpellRequiredTargetLevel(1243)    -- 0    (Fortitude Rank 1)
GetSpellRequiredTargetLevel(10060)   -- 0    (Power Infusion — single rank)
GetSpellRequiredTargetLevel(133)     -- 0    (Fireball — damage, not a buff)
```

Mirrors the server's `SelectAuraRankForLevel` rule: a **ranked beneficial
aura** is castable when `targetLevel + 10 >= spellLevel`, so the minimum
target level is `spellLevel - 10`. The restriction is applied only when the
spell is a ranked positive aura — `spellLevel > 10`, not passive, part of a
rank chain, and applying an aura to a friendly/party/raid target (or an
`APPLY_AREA_AURA_PARTY` effect). Below the minimum, the lowest rank fails
with `SPELL_FAILED_LOWLEVEL`; higher ranks auto-downrank.

> The explicit `MinTargetLevel` / `MaxTargetLevel` spell fields the server
> also enforces are **server-only** columns — they aren't present in the
> 1.12 client's `Spell.dbc`, so the `spellLevel` rule is the only
> client-readable target-level mechanism.

## SpellBook

Functions keyed on the player or pet **spellbook** (slot indices,
learn requirements, skill-tab membership). The underlying `Spell.dbc`
data is exposed through the [Spell](#spell) section; what lives here
is the spellbook-as-a-collection layer.

### `FindSpellBookSlotByID(spellID)`

Inverse of 1.12's `GetSpellName(slot, bookType)`. Given a spellID,
returns the 1-based slot it occupies in the player or pet spellbook,
along with the matching bookType so the result feeds directly into
slot-and-bookType APIs (`GetSpellName`, `GetSpellTexture`,
`GameTooltip:SetSpell`, etc.).

```
slot, bookType = FindSpellBookSlotByID(spellID)
```

- Returns `(slot, "spell")` if the spell is in the player spellbook.
- Returns `(slot, "pet")` if it's only in the pet spellbook.
- Returns `nil` if the spellID isn't currently in either book.

Player book is searched first, so if a spell somehow appeared in both
(unusual but possible for special pet-shared abilities), the player
slot wins.

```lua
local slot, book = FindSpellBookSlotByID(133)
if slot then
    GameTooltip:SetSpell(slot, book)  -- shows full caster-scaled tooltip
end
```

Equivalent to the legacy function of the same name introduced in 3.0
(later renamed to `FindSpellBookSlotBySpellID` in 5.x).

### `C_SpellBook.GetSpellBookItemInfo(slotIndex, spellBank)`

Returns a table describing the spell in a spellbook slot. `slotIndex` is
1-based across the whole book. `spellBank` picks the book —
[`Enum.SpellBookSpellBank.Player`](#enumspellbookspellbank) (`0`) or
`Enum.SpellBookSpellBank.Pet` (`1`).

```lua
local info = C_SpellBook.GetSpellBookItemInfo(1, Enum.SpellBookSpellBank.Player)
-- info.name, info.spellID, info.itemType, info.isPassive, info.iconID, ...
```

Table fields:

| Field | Type | Notes |
|-------|------|-------|
| `itemType` | [`Enum.SpellBookItemType`](#enumspellbookitemtype) | `Spell` (1) for the player book, `PetAction` (3) for the pet book. |
| `actionID` | number | The spellID. Equals `spellID` — vanilla has no spell-override system. |
| `spellID` | number | The spellID in the slot. |
| `name` | string | The localized spell name. |
| `subName` | string | The rank text (e.g. `"Rank 3"`), or `""` when the spell has no rank. |
| `iconID` | string | The icon **path** — feed it straight to `texture:SetTexture(...)`. |
| `isPassive` | boolean | `true` for a passive spell. |
| `isOffSpec` | boolean | Always `false`. |

Returns `nil` for an empty or out-of-range slot, matching the modern API.

> **Deviations from retail, forced by 1.12's data.** `iconID` is a
> texture path, not a `fileID` (vanilla has no fileID system) — the same
> deviation as [`C_Spell.GetSpellInfo`](#c_spellgetspellinfospellid).
> `skillLineIndex` is not returned (`nil`); spellbook tabs aren't
> SkillLines. `isOffSpec` is always `false` — vanilla has no
> specializations.

### `C_SpellBook.GetSpellLevelLearned(spellID)`

Returns the level at which a spell becomes available — the
`BaseLevel` field in `Spell.dbc` (record `+0x70`). Direct read off
the cached record; no class/race filtering.

```lua
C_SpellBook.GetSpellLevelLearned(133)    -- Fireball rank 1 → 4
C_SpellBook.GetSpellLevelLearned(25306)  -- Fireball rank 12 → 60
C_SpellBook.GetSpellLevelLearned(2061)   -- Flash Heal rank 1 → 20
```

Returns `0` for invalid spellIDs, spells with no level requirement
(most non-class utility spells), or records the engine hasn't
cached. Matches modern semantics — unknown / utility spells
return 0 rather than nil.

### `C_SpellBook.GetCurrentLevelSpells([level])`

Returns a 1-based table of spellIDs the player's class/race can
learn at the given level. Without arguments, defaults to the
player's current level.

```lua
C_SpellBook.GetCurrentLevelSpells()    -- spells trainable at your level
C_SpellBook.GetCurrentLevelSpells(20)  -- preview what's available at 20
C_SpellBook.GetCurrentLevelSpells(60)  -- preview at 60
```

Walks `SkillLineAbility.dbc` filtering by the player's class bit
(`1 << (classID - 1)`) and race bit, respecting both the include
masks (entries with mask = 0 are "all classes"/"all races", which
also pass) and the exclude masks. For each surviving entry, looks
up the spell's `BaseLevel` and includes it if it matches the
queried level.

> **Vanilla is trainer-driven.** Modern `GetCurrentLevelSpells`
> (added in 5.x when trainers were removed) returns *auto-learned*
> spells. Vanilla 1.12 requires visiting a trainer to actually
> learn most class spells. We return the closest available analog:
> **what's *trainable* at this level**. Useful for "what's new
> this level" UI panels and level-preview tooling ported from MoP+.

Class/race come from the local player — there's no
`(class, race, level)` form because vanilla doesn't expose a
clean class-string→classID lookup. Returns an empty table at
character select / pre-login (no CGPlayer yet) and for levels
where no class/race spells match.

### `C_SpellBook.GetSkillLineName(skillLineID)`

Returns the localized `SkillLine.dbc` name for a skill-line id — the
id → name half of the bridge addons build by hand. A pure DBC read: it
answers for any valid skill line (`182` = Herbalism, `186` = Mining, the
weapon/defense lines, profession lines) whether or not the player has
learned it. `nil` for non-numeric / non-positive input or an id with no
`SkillLine.dbc` row.

Pairs with [`GetSkillLineRank`](#c_spellbookgetskilllinerankskilllineid) —
name from the DBC, rank from the player's live skills.

```lua
C_SpellBook.GetSkillLineName(182)   -- "Herbalism"
C_SpellBook.GetSkillLineName(186)   -- "Mining"
```

### `C_SpellBook.GetSkillLineRank(skillLineID)`

Returns `(curRank, maxRank, modifier)` — the local player's rank in a
skill line by `SkillLine.dbc` id — or `nil` if the player hasn't learned
it.

- `curRank` — current skill value (base, no temp bonus)
- `maxRank` — the line's cap at the player's level
- `modifier` — temporary bonus (weapon oils, buffs; `0` if none)

```lua
local cur, max = C_SpellBook.GetSkillLineRank(182)  -- Herbalism: 285, 300 (nil if unlearned)
if C_SpellBook.GetSkillLineRank(186) then           -- has Mining at all?
    -- ...
end
```

Vanilla's `GetSkillLineInfo(index)` only walks the skill window by
position and returns skills by *name*, so an addon starting from a
SkillLine.dbc id (a quest's `requiredSkill`) has to map id → localized
name and then string-match every skill line to recover the rank. This
reads the player's live skill list keyed by id directly — the returned
ranks are the same values `GetSkillLineInfo` reports. Resolves the id to
the player's skill slot via `FUN_SKILL_LINE_TO_SLOT`, then reads the
CGPlayer skill table (the same storage `C_Item.IsEquippableItem`'s
require-skill gate uses). `nil` when the line isn't learned, for bad
input, or before the player object is resident (character select).

### `C_SpellBook.GetSpellSkillLine(spellID)`

Returns `(name, skillID)` — the SkillLine.dbc row that the given spell
belongs to. The name is the user-facing skill/tab string in the engine's
current locale: spellbook tab names for class spells (`"Protection"`,
`"Fire"`, `"Holy"`), profession headers (`"Tailoring"`, `"Engineering"`),
weapon-skill rows (`"Swords"`, `"Bows"`), etc.

```lua
C_SpellBook.GetSpellSkillLine(1671)   -- "Protection", 26 (Shield Bash)
C_SpellBook.GetSpellSkillLine(133)    -- "Fire", 8        (Fireball)
C_SpellBook.GetSpellSkillLine(2050)   -- "Holy", 56       (Lesser Heal)
C_SpellBook.GetSpellSkillLine(2480)   -- "Bows", 45       (Shoot Bow)
C_SpellBook.GetSpellSkillLine(3273)   -- "First Aid", 129 (Anesthetic)
```

Walks `SkillLineAbility.dbc` for any row whose `spellID` matches, then
reads the localized `Name[locale]` field at offset `+0x0C` of the
referenced `SkillLine.dbc` record. For spells with multiple SLA rows
(race-locked variants, Turtle WoW's faction-specific entries) the
lookup prefers a row whose class/race masks match the local player —
so the result reflects "what tab is this spell in *for me*" when one
exists. Falls back to the first matching row otherwise, so the
function still answers for spells the local player can't actually use
(inspecting other classes, parsing combat-log entries from unfamiliar
specs).

Returns `(nil, nil)` for:
- non-numeric or non-positive input
- spellIDs with no `SkillLineAbility.dbc` row (most temporary auras,
  proc-only spells, item-on-use effects, GM-debug spells)
- rows whose `skillID` doesn't resolve in `SkillLine.dbc`

Vanilla's `GetSpellTabInfo(tabIndex)` enumerates spellbook tabs by
1-based index — it can't answer "what tab is *this spellID* in".
Addons that need a spellID→tab mapping have historically had to walk
every tab's slot range and string-match against `GetSpellName`. This
function reads it directly off the DBC for any spellID, including
spells the player hasn't learned.

### `C_SpellBook.IsAutoAttackSpellBookItem(slot, bookType)`

Spellbook-slot variant of
[`C_Spell.IsAutoAttackSpell`](#c_spellisautoattackspellspellid). Resolves
the spellbook slot to its spellID first, then tests `== 6603` (melee
auto-attack).

```lua
C_SpellBook.IsAutoAttackSpellBookItem(1, "spell")
```

### `C_SpellBook.IsRangedAutoAttackSpellBookItem(slot, bookType)`

Spellbook-slot variant of
[`C_Spell.IsRangedAutoAttackSpell`](#c_spellisrangedautoattackspellspellid).
Resolves the slot to a spellID, returns `true` if it's `75` (Auto
Shot) or `5019` (Shoot wand).

```lua
C_SpellBook.IsRangedAutoAttackSpellBookItem(10, "spell")
-- true if slot 10 is your wand Shoot
```

## State

Player movement / visibility state queries that modern WoW exposes
as no-arg globals. 1.12 doesn't bind them to Lua despite the engine
tracking the underlying state — broadcast in UpdateFields for some
(mount, stealth visibility), local-only for others (falling,
swimming).

### `IsMounted()`

Returns `true` if the player is currently mounted, `false` otherwise.

Reads `UNIT_FIELD_MOUNTDISPLAYID` from the player's broadcast
descriptor. The field holds a creature display ID (the model the
engine renders under the player) when mounted, and `0` otherwise.

```lua
if not IsMounted() then
    CastSpellByName("Summon Dreadsteed")
end
```

### `Dismount()`

Dismisses the player's currently summoned mount. No-op when the
player isn't mounted.

```lua
if IsMounted() then Dismount() end
```

Walks the player's buff range for an aura whose `Spell.dbc` effects
include `SPELL_AURA_MOUNTED` (`78`), then sends `CMSG_CANCEL_AURA` for
that spellID via the same direct sender [`C_Spell.CancelSpellByID`](#c_spellcancelspellbyidspellid--cancelspellbynamename) uses. By the
time the server processes the cancel, `IsMounted()` will return
`false`.

### `IsStealthed()`

Returns `true` if the player is currently in Stealth (Rogue) or
Prowl (Druid), `false` otherwise.

Reads bit `0x02` of the player visibility byte at descriptor
`+0x17C` and AND-gates with `MountDisplayID == 0` to disambiguate
mount (which sets the same bit). Untested for Druid shapeshift
forms — if you find a false-positive there, file an issue and we'll
switch to walking the player's aura array for the actual stealth
spell.

```lua
if IsStealthed() then
    -- defer the spell that would break stealth
end
```

### `IsFalling()`

Returns `true` if the player is currently mid-jump or falling,
`false` otherwise.

Reads the local CGPlayer movement-flags word at `+0x9E8` and tests
`MOVEFLAG_FALLING | MOVEFLAG_FALLING_FAR` (`0x2000 | 0x4000`). This
is client-side state (outbound `MSG_MOVE_*` data, never visible for
remote units), so the function only meaningfully applies to the
local player.

```lua
if not IsFalling() then
    -- safe to bind ground-targeted spell
end
```

### `IsSwimming()`

Returns `true` if the player is currently swimming (in liquid, with
the swim animation/movement set), `false` otherwise.

Same movement-flags word as `IsFalling`, testing `MOVEFLAG_SWIMMING`
(`0x200000`). Local-player only.

```lua
if IsSwimming() then
    -- breath bar logic, mount-failure suppression, etc.
end
```

### `IsIndoors()`

Returns `1` if the local player is under a WMO roof — inside a building,
cave, or instance interior — and `nil` otherwise. `nil` before the player
object exists (pre-world).

Recomputed live from the engine's WMO geometry query, so it flips as you
cross an interior threshold rather than tracking the current zone/area —
you can be indoors and outdoors within the same subzone. Returns `1`/`nil`
(not `true`/`false`) to match the historical retail contract and
SuperWoW 2.2, so both `if IsIndoors()` and `IsIndoors() == 1` work.

```lua
if IsIndoors() then
    -- suppress a mount cast that would just fail
end
```

### `IsOutdoors()`

Returns `1` if the local player is outdoors (open sky, or simply not
inside a WMO interior) and `nil` otherwise. Exact complement of
[`IsIndoors()`](#isindoors) for a resolvable player; both return `nil`
pre-world.

```lua
if IsOutdoors() then
    CastSpellByName("Mount Up")
end
```

### `IsAssistingRitual()`

Returns `true` if the local player is currently clicking a warlock
summoning portal (the channel-on-GameObject state with no castbar
UI, where movement breaks the channel), `false` otherwise.

This is distinct from spell channeling: the function fires for
*participants* who clicked the portal, not the warlock who cast
Ritual of Summoning. Vanilla has no Lua surface for this state —
the engine's `SPELLCAST_CHANNEL_*` events don't fire and
`C_Spell.CastingInfo()` returns nothing — so addons that want to react to
the player being committed to a ritual (e.g. suppress autorun
toggles, hide nameplate clicks, warn before movement) have no other
way to detect it.

Detection uses two state slots:

- `UNIT_CHANNEL_SPELL` (descriptor `+0x228`) — the warlock's channel
  spell ID (698) is mirrored onto every clicker for the duration
  of the ritual.
- A CGPlayer-local pointer at `+0xB4` — the engine's "current spell
  cast target GameObject" slot, set to the portal GO while the
  clicker is engaged.

Either signal alone is ambiguous (the warlock channeling matches
the first; mining and other GO-targeted casts match the second);
the conjunction is portal-clicker-specific.

The engine fires `SPELLCAST_CHANNEL_START` / `SPELLCAST_CHANNEL_STOP`
on the portal click, even though there's no castbar — Blizzard's
vanilla castbar UI filters out spell ID 698 (Ritual of Summoning),
but the events themselves fire normally. Combine them with this
function for ritual-specific triggers:

```lua
local frame = CreateFrame("Frame")
frame:RegisterEvent("SPELLCAST_CHANNEL_START")
frame:RegisterEvent("SPELLCAST_CHANNEL_STOP")
frame:SetScript("OnEvent", function()
    if event == "SPELLCAST_CHANNEL_START" and IsAssistingRitual() then
        -- player just committed to a ritual portal
    elseif event == "SPELLCAST_CHANNEL_STOP" then
        -- channel ended; was it a ritual?
    end
end)
```

Local-player only — the `+0xB4` pointer lives on the CGPlayer
object, not in the broadcast descriptor, so the function can't
report state for `target` / `party*` / etc. Returns `false` when
called before the player object is initialized (login screen).

### `IsInGroup()`

Returns `true` if the player is in a party or a raid, `false`
otherwise. Modern shortcut over stock 1.12's
`GetNumPartyMembers() > 0 or GetNumRaidMembers() > 0`.

Accepts an optional `groupType` argument (modern
`Enum.PartyCategory.Home` / `Instance`). Vanilla has no LFG / LFD
instance-group concept, so the argument is accepted and ignored.

```lua
if IsInGroup() then
    SendChatMessage("ready check", "PARTY")
end
```

### `IsInRaid()`

Returns `true` if the player is in a raid (specifically — parties
return `false`), `false` otherwise. Same `groupType` argument story
as `IsInGroup`.

```lua
local channel = IsInRaid() and "RAID" or "PARTY"
SendChatMessage("ready check", channel)
```

### `GetMirrorTimerInfo(index)` / `GetMirrorTimerProgress(label)`

Modern (3.0+) readers for the BREATH (drowning) / EXHAUSTION
(off-map) / FEIGNDEATH (hunter Feign Death) bar state. The vanilla
engine fires the `MIRROR_TIMER_START` / `_PAUSE` / `_STOP` events
with the full packet payload but doesn't cache anything internally;
ClassicAPI hooks the SMSG handler at `0x005E7990` and builds a
3-slot side cache so these accessors work.

`GetMirrorTimerInfo(index)` — `index` is 1, 2, or 3 (slot id). Returns
`(timer, value, maxValue, scale, paused, label)` for an active slot,
or nothing if that slot is empty:

| Return | Type | Meaning |
|--------|------|---------|
| `timer` | string | Engine type-name: `"EXHAUSTION"`, `"BREATH"`, or `"FEIGNDEATH"` |
| `value` | number | The snapshot value from the last server packet (ms). Not live-interpolated — see `GetMirrorTimerProgress` for that |
| `maxValue` | number | Timer's max (ms) |
| `scale` | number | Server-set rate. **Negative** = depleting (vanilla sends `-1` for breath, draining 60000 ms over 60 s of real time); **positive** = filling. Modern's docs describe the opposite convention — vanilla's wire format predates that flip |
| `paused` | boolean | `true` if the engine has frozen the timer |
| `label` | string | Localized display label, e.g. `"Breath"`. For FEIGNDEATH, the spell name |

`GetMirrorTimerProgress(label)` — `label` is the engine type-name
(`"BREATH"` etc.). Returns the **live interpolated** value in
milliseconds, computed each call from the snapshot + elapsed
real-time ticks. Returns `0` when no matching timer is active.

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("MIRROR_TIMER_START")
f:SetScript("OnEvent", function()
    -- pull whichever slot the engine just populated
    for i = 1, 3 do
        local timer, value, maxv, scale, paused, label = GetMirrorTimerInfo(i)
        if timer then
            DEFAULT_CHAT_FRAME:AddMessage(format("%s: %d / %d (%s)",
                label, value, maxv, paused and "paused" or "running"))
        end
    end
end)

-- Smooth bar updates: poll progress in OnUpdate
local function OnUpdate(self)
    self:SetValue(GetMirrorTimerProgress("BREATH"))
end
```

> **Modern uses `"FATIGUE"`, vanilla uses `"EXHAUSTION"`.** The engine
> type-name string for the off-map timer is `"EXHAUSTION"` in 1.12 —
> we surface what the engine actually returns rather than translating
> to modern's name. Addons backporting `"FATIGUE"`-keyed code need to
> handle either string.

### `GetShapeshiftFormID()`

Returns the player's current shapeshift form as the integer ID from
vanilla 1.12.1's `SpellShapeshiftForm.dbc`. Returns `0` when the
player isn't shifted.

| Class | Form IDs |
|-------|----------|
| Druid | `1` Cat, `2` Tree, `3` Travel, `4` Aquatic, `5` Bear, `8` Dire Bear, `31` Moonkin |
| Warrior | `17` Battle, `18` Defensive, `19` Berserker |
| Shaman | `16` Ghost Wolf |
| Rogue | `30` Stealth |
| Priest | `28` Shadowform, `32` Spirit of Redemption |

**Turtle WoW** extends the DBC with custom rows that aren't present
on Blizzard 1.12.1:

| ID | Name | Notes |
|----|------|-------|
| `9` | Tree of Life Form | Restoration druid endgame form |
| `11` | Swift Travel Form | Mounted-speed travel form |

> **Vanilla numbering ≠ modern numbering.** Modern WoW renumbered the
> table — e.g. modern uses `17` for Travel and `35` for Tree of Life,
> where 1.12.1 uses `3` (Travel) and Turtle uses `9` (Tree of Life).
> Don't copy named constants from a modern addon and expect them to
> match.

```lua
if GetShapeshiftFormID() == 0 then
    CastSpellByName("Cat Form")
end
```

Reads byte 2 of `UNIT_BYTES_1` (descriptor `+0x212`) on the local
player — the same byte vanilla's `Script_GetShapeshiftFormInfo`
compares against each form-spell's effect-encoded form ID to answer
"is this form active". The engine updates the byte from
`SMSG_UPDATE_OBJECT` aura/form packets, so the value is live without
needing an event listener.

Vanilla 1.12 has only `GetShapeshiftFormInfo(index)` (1-based bar
index); `GetShapeshiftFormID` exposes the DBC ID directly so callers
can write `if formID == CAT_FORM` without iterating the bar.

### `CancelShapeshiftForm()`

Cancels the player's current shapeshift form, dropping them back to
caster (or whatever neutral state the class uses). No-op when the
player isn't shifted.

```lua
if GetShapeshiftFormID() ~= 0 then
    CancelShapeshiftForm()
end
```

Covers druid forms (Cat / Bear / Travel / Aquatic / Moonkin / Dire
Bear / Tree of Life), shaman Ghost Wolf, priest Shadowform / Spirit of
Redemption, and rogue Stealth. No form table hardcoding — the
implementation finds whichever buff currently provides the active form
by scanning `Spell.dbc` effects.

> **Does not cover warrior stances.** Vanilla treats warriors as
> *always* in a stance — the engine has no "no stance" state and
> right-clicking the active stance in the stance bar does nothing. The
> server rejects the cancel packet for stances, so this function is a
> no-op when called as a warrior. Switch stances with the normal stance
> spells instead.

**Implementation**: reads the current form byte (descriptor `+0x212`,
same source as [`GetShapeshiftFormID()`](#getshapeshiftformid)), walks
the player's buff slots (0..31), looks up each spell's
`Spell.dbc[spellID]` effect arrays, and on the first effect whose
`EffectApplyAuraName` is `SPELL_AURA_MOD_SHAPESHIFT` (`36`) and whose
`EffectMiscValue` matches the current form ID, sends `CMSG_CANCEL_AURA`
via the engine's direct sender at [`FUN_006E7040`](../src/Offsets.h#L428).

Mirrors 3.3.5a's `Script_CancelShapeshiftForm` inner (`FUN_00726CE0`),
which does the same effect-array scan + form-id match before issuing
its cancel packet. Vanilla just lacks the public Lua surface.

## System

Host/OS-level helpers that aren't tied to a game domain.

### `GetPhysicalScreenSize()`

Returns `(widthPixels, heightPixels)` — the display's physical
resolution in pixels. Modern WoW added this as a native in 7.0
(Legion); FrameXML's `PixelUtil` builds its entire pixel↔UI-unit
conversion on it (`768.0 / physicalHeight`), which is what lets addons
snap frames to whole pixels for crisp borders at fractional UI scales.

```lua
local w, h = GetPhysicalScreenSize()   -- e.g. 1920, 1080
```

Vanilla 1.12 has no such native — and no pre-parsed pixel-dimension
global. `GetScreenWidth()`/`GetScreenHeight()` return UI-space units
(derived from the UIParent aspect ratio at `[UIParent + 0x7c]`), not
pixels. The engine only ever holds the current mode as the
`gxResolution` CVar string (`"WIDTHxHEIGHT"`); both `SetScreenResolution`
(`0x0048bfd0`) and the display-init path sscanf it on demand rather
than caching dimensions anywhere. So we read the `gxResolution` CVar
directly through the engine's by-name lookup (`FUN_FIND_CVAR`, value at
`+0x20`) and parse it — the same source of truth the engine uses.

Returns `1024, 768` (→ factor 1.0, making PixelUtil a 1:1 no-op) when
the CVar is missing or unparseable. Caveat: in windowed mode
`gxResolution` reflects the configured render resolution, which may
differ from the OS window's client size — vanilla exposes no separate
window-pixel query.

The companion `PixelUtil` table (`PixelUtil.SetPoint`/`SetSize`/
`SetStatusBarValue`/…) is provided Lua-side by the ClassicAPI addon and
consumes this function.

### `CopyToClipboard(text [, removeMarkup])`

Copies `text` to the Windows clipboard and returns the number of bytes
copied. Vanilla has no clipboard access at all, so this is a ClassicAPI
addition backed by the Win32 clipboard API. Available on both the in-game
and login/character-select screens.

- `text` — the string to copy (numbers are coerced, like Lua does).
- `removeMarkup` — optional; when truthy, strip WoW UI escape sequences
  before copying so the clipboard gets plain text:
  - `|cAARRGGBB … |r` color codes → removed
  - `|H…|h text |h` hyperlinks → keeps only the visible text
  - `|T…|t` textures → removed
  - `|n` → newline, `||` → literal `|`
  - unknown `|x` sequences are left verbatim, so real text that merely
    contains a pipe survives
- returns the byte length of the copied (possibly stripped) string —
  consistent with Lua's `#s`. `0` for an empty string or if the clipboard
  couldn't be opened.

```lua
CopyToClipboard("hello world")                              -- 11
local link = "\124cffff0000Red\124r"                        -- real single-pipe markup
CopyToClipboard(link, true)                                 -- 3   (clipboard gets "Red")
CopyToClipboard(link)                                       -- 15  (raw escapes copied)
```

> The string is treated as UTF-8 (matching the credential and TTS modules)
> and written as `CF_UNICODETEXT`; Windows synthesizes `CF_TEXT` for legacy
> consumers.
>
> Note when testing from the chat box: the edit box escapes every typed `|`
> into `||`, so a literal `|cff…` you type is no longer a color code by the
> time it reaches Lua. Build real markup with `\124` (as above) to exercise
> `removeMarkup`.

## Talent

### `GetTalentSpellID(tabIndex, talentIndex, [rank[, classID]])`

Returns the spellID for the talent at the given `(tabIndex, talentIndex)`
and rank, or `nil` if the talent index is out of range or the rank slot
is empty. 1.12's `GetTalentInfo` returns `(name, icon, tier, column,
currentRank, maxRank, ...)` with no spellID; this fills the gap so
addons can chain into the spell APIs without maintaining their own
`(class, tab, idx) → spellID` lookup tables.

```lua
GetTalentSpellID(1, 9)           -- 14751  (Inner Focus, single rank)
GetTalentSpellID(1, 1)           -- 14525  (Wand Specialization, current rank)
GetTalentSpellID(1, 1, 1)        -- 14524  (Wand Specialization rank 1)
GetTalentSpellID(1, 1, 2)        -- 14525  (Wand Specialization rank 2)
GetTalentSpellID(1, 1, 1, 8)     -- 11210  (Arcane Subtlety r1 — Mage, classID 8)

-- chains into the spell APIs cleanly
GameTooltip:SetSpellByID(GetTalentSpellID(1, 9))
print(C_Spell.GetSpellName(GetTalentSpellID(1, 9)))  -- "Inner Focus"
```

`rank` is optional. When omitted, defaults to the player's currentRank
by delegating to the engine's existing `GetTalentInfo` (5th return) —
that's the same player-state derivation the engine uses for talent UI
rendering, including the spell-knowledge checks that account for
talent-granted abilities. If currentRank is 0 (no points spent), falls
back to rank 1 so the function still produces the talent's canonical
spellID for unallocated talents.

When provided explicitly, returns the spellID at that rank regardless
of how many points the player has invested. Useful for tooltip-on-hover
scenarios where you want to preview "what rank 5 would do" without
respec'ing.

`classID` is optional (4th arg; pass `rank` as nil to keep the default)
and queries any class's talent tree via `Talent.dbc` / `TalentTab.dbc` —
the same cross-class path as
[`GetTalentIDByIndex`](#gettalentidbyindextabindex-talentindex-classid),
with identical `(tab, idx)` ordering. Because there's no "currentRank"
for a class you aren't, a cross-class query with no explicit `rank`
defaults to **rank 1** (rather than the player-currentRank default of
the same-class path).

Returns `nil` for:

- non-numeric or non-positive `tabIndex` / `talentIndex`
- `tabIndex` or `talentIndex` out of range for the class
- explicit `rank` exceeding the talent's allocated max — e.g. asking
  for rank 5 on a 1-rank talent like Inner Focus
- a `classID` that matches no class

Without `classID`, reads the engine's per-tab talent arrays at
`[0x00BDCD28]` (populated at login from `Talent.dbc` filtered by the
player's class); with it, reads the `Talent.dbc` flat array directly.
Either way the `SpellRank[]` array lives at offset `+0x10` of each
record (stride `0x54`), one spellID per rank — vanilla populates
indices 0..4 (ranks 1..5), the higher slots stay zero.

Equivalent to one of `GetTalentInfo`'s extended returns in modern WoW
(varies by version; the talent's spellID has been part of the tuple
since 5.0+).

### `GetTalentIDByIndex(tabIndex, talentIndex[, classID])`

Returns the engine's hidden talentID — the primary key of the
`Talent.dbc` row — for the talent at the given (tab, idx). Returns
`nil` for out-of-range indices.

```lua
GetTalentIDByIndex(1, 9)      -- 174  (Inner Focus, Discipline tier 3)
GetTalentIDByIndex(1, 1)      -- 166  (first Discipline talent)
GetTalentIDByIndex(1, 1, 8)   -- 37   (first Arcane talent — Mage, classID 8)
```

1.12's `GetTalentInfo(tab, idx)` returns
`(name, icon, tier, column, currentRank, maxRank, ...)` but NOT the
talentID. Modern WoW exposes it (and uses talentIDs as the natural
key for `GetTalentInfoByID`, talent build sharing strings, etc.); we
add this getter so addons that key on talentIDs from later expansions
work unmodified.

The optional `classID` (1-based: Warrior 1, Paladin 2, Hunter 3,
Rogue 4, Priest 5, Shaman 7, Mage 8, Warlock 9, Druid 11) queries any
class's talent tree, not just the local player's — useful for
talent-build tooling and previewing other specs. When omitted or nil,
it falls back to the player's own class.

- **Without `classID`** — reads `TalentEntry+0x00` from the per-tab
  talent arrays at `[0x00BDCD28]` (the engine's runtime state for the
  local player's class). Same struct walk as `GetTalentSpellID`.
- **With `classID`** — reads the `Talent.dbc` / `TalentTab.dbc` flat
  arrays directly, replicating the engine's tree builder
  (`FUN_004f2c00`): tabs in DBC row order filtered by class-mask, then
  talents in DBC row order within each tab. This is the *same* ordering
  the runtime arrays are built from, so `GetTalentIDByIndex(t, i)` and
  `GetTalentIDByIndex(t, i, <player's class>)` return identical values.

Equivalent to the talentID return slot of `GetTalentInfo` in modern
WoW (5.0+; not exposed at all in 1.12).

## Targeting

Backports of the post-vanilla `TargetScript` selection functions. Vanilla
already ships `TargetNearestEnemy/Friend/PartyMember/RaidMember`,
`TargetLastEnemy/Target`, `AssistUnit`, `TargetUnit`, `ClearTarget`; these
fill in the ones added in later patches. They're built on the engine's own
tab-targeting core — the same visible-unit enumeration, per-mode validity
predicate (reaction/attackability/alive), and selection commit the native
`TargetNearestEnemy` uses — so hostility semantics match the client exactly.

Not backported (no vanilla equivalent — soft-target / action-camera era):
`TargetPriorityHighlightStart/End`, `IsTargetLoose`, `TargetToggle`.

### `GetPlayerFacing()`

Returns the player's facing as an angle in radians (`0` … `2*pi`,
increasing counter-clockwise), or nothing off-world (glue / loading).
Vanilla never shipped this (only character-create/select facing exist), but
it's the companion the direction-target functions below need — pass it to
aim "in front of me."

```lua
local facing = GetPlayerFacing()   -- e.g. 1.65
```

Read from the player unit's movement-block orientation field
(`player + 0x9C4`).

### `TargetDirectionEnemy(facing [, coneAngle])`

Targets the nearest **attackable** unit within a cone centered on `facing`
(an absolute world angle in radians, as returned by `GetPlayerFacing()`).
`coneAngle` is the full cone width in radians; omitted, it defaults to
`pi/2` (90° — 45° to either side).

```lua
-- Target whatever enemy you're looking at:
TargetDirectionEnemy(GetPlayerFacing())

-- Wider 180° sweep in front:
TargetDirectionEnemy(GetPlayerFacing(), math.pi)
```

Does nothing if no enemy falls inside the cone. "Attackable" uses the
engine's own reaction check, so it matches what `TargetNearestEnemy` would
consider a valid enemy (hostile, alive, not a critter).

### `TargetDirectionFriend(facing [, coneAngle])`

Same as `TargetDirectionEnemy`, but selects the nearest **friendly**
(assistable) unit in the cone.

### `TargetNearest([reverse])`

Cycles the nearest unit you can attack or assist. Repeated calls within ~1s
step through candidates nearest→farthest (a frozen snapshot, so the order
is stable mid-cycle); a longer gap or an externally-changed target rebuilds
and re-targets the nearest. `reverse` (truthy) steps the other way.

### `TargetNearestEnemyPlayer([reverse])`

Like `TargetNearest`, but restricted to hostile **players** (PvP targeting).
Same cycle behavior and `reverse` flag.

### `TargetNearestFriendPlayer([reverse])`

Like `TargetNearestEnemyPlayer`, but restricted to friendly **players**.

## TaxiMap

Backports of the retail taxi-map node enumerators, reading
`TaxiNodes.dbc` (static) and the open flight master's live session. Both
also register the `Enum.FlightPathFaction` (`Neutral=0`, `Horde=1`,
`Alliance=2`) and `Enum.FlightPathState` (`Current=0`, `Reachable=1`,
`Unreachable=2`) tables, so retail code comparing against those enums
ports unchanged.

Ship both because they answer different questions:

| function | source | use |
|---|---|---|
| `C_TaxiMap.GetTaxiNodesForMap([mapID])` | `TaxiNodes.dbc` (static) | every flight point (one continent, or all with no arg), faction-tagged, discovered or not — for a database / map overlay |
| `C_TaxiMap.GetAllTaxiNodes([uiMapID])` | live taxi session | the nodes reachable from the **open** flight master, with per-node state + the slot index to fly — for a flight-map UI |

### `C_TaxiMap.GetTaxiNodesForMap([mapID])`

Returns an array of flight masters. A numeric `mapID` (a `Map.dbc` id —
`0` Eastern Kingdoms, `1` Kalimdor, `30` Alterac Valley, … — the same
identity `C_Map.GetAreaTriggers([mapID])` uses; retail's `uiMapID` has no
vanilla analog) filters to that continent. **Omitted / non-number returns
every flight master on every continent** — a one-call flight database
(each entry carries its own `mapID`). This is a ClassicAPI extension to
retail's required-argument signature.

Only real flight masters are returned. `TaxiNodes.dbc` also contains
non-flight rows — transports/boats/zeppelins (no flight mount) and
non-selectable markers like Northshire Abbey or standalone instance
entries (no connecting flight path) — which are filtered out: a node is
included only if it has a flight mount **and** is a `TaxiPath.dbc`
endpoint (some flight path connects to it).

Each entry:

| field | meaning |
|---|---|
| `nodeID` | `TaxiNodes.dbc` row id |
| `name` | localized node name (`"Stormwind, Elwynn Forest"`) |
| `faction` | `Enum.FlightPathFaction` — Alliance-mount-only → `Alliance`, Horde-only → `Horde`, both → `Neutral` |
| `reachable` | *(ext)* a flight path leads *to* this node (in-degree > 0). `false` = departure-only, reachable only by other means (druid Teleport → "Nighthaven, Moonglade"; a boat dock → "Stormwind Harbor") |
| `position` | `{x, y}` 0–1 on the continent map (retail-accurate) |
| `mapID` | *(ext)* continent `Map.dbc` id |
| `x` / `y` / `z` | *(ext)* raw continent world coords |
| `areaID` | *(ext)* the `AreaTable` zone, from the node's own name — its **location** if that's a mapped city ("Orgrimmar" → Orgrimmar city), else the **zone** suffix ("…, Western Plaguelands" → WPL). Authoritative; absent if unresolved |
| `mapX` / `mapY` | *(ext)* 0–100 position within that zone (`mapX` horizontal, `mapY` vertical) |

`nodeID` / `name` / `faction` / `position` are the retail fields; the
rest are ClassicAPI extensions (marked *(ext)*). `position` matches
retail (continent 0–1), while `areaID` + `mapX`/`mapY` let a zone-map
addon pin directly. `areaID` comes from the node's **name**, matched
against `AreaTable` — the location part when it's a mapped city
("Orgrimmar, Durotar" → Orgrimmar city, so a capital's master pins on
the city map), otherwise the zone suffix ("Chillwind Camp, Western
Plaguelands" → WPL). Each candidate is accepted only if its map rect
contains the node, so this is right at borders a purely geometric guess
gets wrong (Chillwind → WPL, not the Alterac box it sits in). Nodes
whose name resolves to no containing zone fall back to the geometric
landmass resolver. `isUndiscovered` is not wired (the client's
known-node bitfield isn't read) — every node is returned regardless of
discovery.

```lua
for _, n in ipairs(C_TaxiMap.GetTaxiNodesForMap(0)) do
    if n.faction == Enum.FlightPathFaction.Alliance and n.areaID then
        Pin(n.areaID, n.mapX, n.mapY, n.name)   -- zone-map pin
    end
end
```

Replaces hand-scraped flight tables (pfQuest's `pfDB.meta.flight`):
draw a node per entry (filtered by faction, using `areaID`+`mapX`/`mapY`)
exactly like `SearchAreaTriggerID`. Auto-covers custom-server flight
points — no NPC-id curation.

### `C_TaxiMap.GetAllTaxiNodes([uiMapID])`

Returns the live flight-master destination list — the nodes reachable
from the **currently open** taxi map, with reachability state and the
slot index needed to take the flight. **Only meaningful while the taxi
map is open** (between `TAXIMAP_OPENED` and `TAXIMAP_CLOSED`); returns an
empty table otherwise. `uiMapID` is accepted for retail signature parity
but ignored (vanilla has a single active taxi map).

Each entry:

| field | meaning |
|---|---|
| `slotIndex` | the legacy `1..NumTaxiNodes()` index — pass to `TakeTaxiNode(slotIndex)` to fly |
| `name` | node name |
| `position` | `{x, y}` 0–1 on the flight-map image (matches retail) |
| `state` | `Enum.FlightPathState` — `CURRENT`→`Current`, `REACHABLE`→`Reachable`, `DISTANT`/none→`Unreachable` |
| `nodeID` | `TaxiNodes.dbc` id (resolved from the slot's node record; `0` if absent) |

No `faction` / `isUndiscovered` here — retail's `GetAllTaxiNodes` doesn't
carry them (reachability implies faction); that data lives on
`GetTaxiNodesForMap`. `textureKit` / `useSpecialIcon` (retail cosmetic
fields) have no vanilla equivalent and are omitted.

```lua
-- Fly to the first reachable destination:
for _, n in ipairs(C_TaxiMap.GetAllTaxiNodes()) do
    if n.state == Enum.FlightPathState.Reachable then
        TakeTaxiNode(n.slotIndex)
        break
    end
end
```

This is the interactive counterpart to `GetTaxiNodesForMap`: session-gated
and player-relative (its whole point is the open master's reachable set),
which is why a static database like pfQuest uses `GetTaxiNodesForMap`
instead.

### `C_TaxiMap.GetTaxiPaths()`

Returns every flight path (directed edge) in `TaxiPath.dbc` as an array of
tables — the taxi routing graph. No flight master needed; reads the DBC
directly.

```lua
local paths = C_TaxiMap.GetTaxiPaths()
-- paths[i] = { pathID = 94, fromNodeID = 8, toNodeID = 16, cost = 110 }
```

| Field | Type | Meaning |
|-------|------|---------|
| `pathID` | number | `TaxiPath.dbc` id — pass to `GetTaxiPathWaypoints` |
| `fromNodeID` | number | source `TaxiNodes.dbc` id |
| `toNodeID` | number | destination `TaxiNodes.dbc` id |
| `cost` | number | flight cost in copper (the routing weight) |

Directed — `A→B` and `B→A` are two separate entries with their own path IDs
and geometry. Combine with `GetTaxiNodesForMap` (the node table) to build a
weighted graph, and with `GetTaxiPathWaypoints` to measure real distances.

### `C_TaxiMap.GetTaxiPathWaypoints(pathID)`

Returns the waypoint polyline of one flight path as an array of world
coordinates, ordered by node index (flight order).

```lua
local wp = C_TaxiMap.GetTaxiPathWaypoints(94)
-- wp[i] = { x = -8851.66, y = 498.55, z = 111.26, mapID = 0 }
```

| Field | Type | Meaning |
|-------|------|---------|
| `x`, `y`, `z` | number | world position (yards) |
| `mapID` | number | continent (`Map.dbc` id) |

Returns nothing for an unknown `pathID` or an empty path. Sum the 3D distances
between consecutive waypoints for the path's length; vanilla taxi flight
speed is 32 yd/s, so `length / 32 ≈ flight seconds` for a single hop.
(Multi-hop trips are shorter than the sum of their segments — the server cuts
the corner at each intermediate flight master via data not present in any
client file, so a client-side estimate of a chained route is an upper bound.)

### `C_TaxiMap.GetTaxiRoute(slotIndex)`

Returns the node chain the client would actually fly to a taxi-map slot, as an
array of `TaxiNodes.dbc` IDs in flight order (`{ currentNodeID, …, destNodeID }`).
`slotIndex` is the legacy `1..NumTaxiNodes()` index (the same one
`TaxiNodeName`/`TakeTaxiNode` take). **Only meaningful while the taxi map is
open.**

```lua
local route = C_TaxiMap.GetTaxiRoute(slotIndex)
-- route = { 8, 16, 43, 66 }  (Thelsamar -> Refuge Pointe -> Aerie Peak -> Chillwind)
```

This is the exact chain `TakeTaxiNode(slotIndex)` would send to the server —
mirrors the client's own routing decision: a single direct hop when a `TaxiPath`
edge exists (`{ current, dest }`), otherwise the client's precomputed multi-hop
route (which, per character, may detour around flight points you haven't
discovered). Returns nothing when the map is closed, the slot is out of range,
it's the current node, or no route exists.

Pairs with `GetTaxiPaths` (each consecutive `(from, to)` names a `pathID`) and
`GetTaxiPathWaypoints` (that path's geometry) to measure a chained trip without
matching flight-map pixel coordinates back to nodes.

## Time

### `GetServerTime()`

Returns the current server clock as a Unix epoch timestamp (seconds since
1970-01-01 UTC), or `nil` before login (while the engine's game-time
struct is still BSS-zero).

```lua
local now = GetServerTime()
-- now = 1778260148 (Fri 2026-05-08 17:09:08 UTC)
```

Reads year/month/day/hour/minute from the engine's game-time struct at
`0x00CE8538` (populated from `SMSG_LOGIN_VERIFY_WORLD` /
`SMSG_LOGIN_SETTIMESPEED` and advanced by the internal tick handler) and
converts via `_mkgmtime`. Stock `GetTime()` returns frame-relative
seconds-since-login and is useless for wall-clock alignment; this is the
right call for calendar / log-timestamp / cooldown-sync use cases.

> **Sub-minute accuracy.** The 1.12 wire protocol carries time at
> minute granularity — the packed gametime field has no seconds — so
> the engine's clock only steps every minute. We interpolate within the
> minute using `GetTickCount`: whenever we observe the engine's minute
> change, we anchor to the current tick and add `(now - anchor) / 1000`
> seconds for subsequent calls in that minute.
>
> The very first call after login lands at second `:00` of the current
> minute (we have no way to know how far in we are when we first see
> it), so the cold-start value can be off by 0..59 seconds. After the
> first minute rollover we observe, the anchor lands at the rollover
> boundary and the timestamp is accurate to within a second of the
> engine's clock for as long as the session continues.

### `GetTimeCached()`

Returns the same value as [`GetTime()`](https://warcraft.wiki.gg/wiki/API_GetTime)
— seconds on the engine's uptime clock — but **frame-stable**: every call
within a single frame returns the identical value, refreshed once per
frame. Same epoch as `GetTime()`, so the two are directly comparable.

Backports the retail 4.x `GetTime()` behavior (retail samples the clock
once per frame in the main loop) under a distinct name, leaving vanilla's
`GetTime()` untouched — 1.12's `GetTime()` is *live*, recomputing the OS
tick on every call, so two same-frame `GetTime()` calls can differ.

```lua
-- Frame-stable: consistent timestamps across a frame's work
local now = GetTimeCached()
if now - lastFire >= interval then
    lastFire = now
    ...
end
```

> **Not a performance feature.** The underlying tick source is cheap in
> 1.12 (`GetTickCount`, a user-mode shared-page read, or `rdtsc`), and
> both functions pay the same Lua→C call overhead, which dominates. So
> `GetTimeCached()` is not meaningfully faster than `GetTime()` — its
> only advantage is the frame-stable semantics. To actually cut cost in a
> hot `OnUpdate`, cache the value in a Lua local
> (`local now = GetTime()`) and reuse it; that removes the per-call Lua
> dispatch, which a cached C function cannot.

The cache is refreshed from the shared once-per-frame `Tick::WorldTick`
hook; before the first world tick (pre-login / glue) it falls back to a
live sample, so it never returns 0.

### `C_Timer.After(seconds, callback)`

Schedules `callback` to fire once after `seconds`. Returns nothing.
Backports modern WoW's `C_Timer.After` as an engine binding (retail
12.0.5 ships this as a `Script_*` C function, not Lua — we mirror
that shape so addons relying on the modern semantics get
identical behavior).

```lua
C_Timer.After(0.5, function() print("half a second later") end)
C_Timer.After(0, function() print("next frame") end)
```

Errors in the callback are caught (via `lua_pcall`) and silently
swallowed — same as modern's behavior. One broken timer doesn't
poison other timers on the same tick.

### `C_Timer.NewTimer(seconds, callback)`

Like `After`, but returns a cancel-handle table:

```lua
local handle = C_Timer.NewTimer(10, function() print("never if I cancel") end)
handle:Cancel()           -- prevents the callback from firing
print(handle:IsCancelled()) -- true
```

`Cancel()` is idempotent; calling it on a timer that already fired
or was previously cancelled is a no-op. `IsCancelled()` returns
`true` after `Cancel()`, after natural expiry, or for an unknown
timer ID. Both methods take no arguments other than `self`.

### `C_Timer.NewTicker(seconds, callback, [iterations])`

Recurring variant. `callback` fires every `seconds` until cancelled
or until `iterations` fires have happened. `iterations` is optional;
omitted or non-numeric means forever.

```lua
local h = C_Timer.NewTicker(1, function() print("each second") end)
-- ...later
h:Cancel()

-- bounded:
C_Timer.NewTicker(0.25, function() ... end, 8)  -- fires 8 times, every 250ms
```

Minimum period is clamped to `0.0001` to prevent tight loops.
Returns the same kind of cancel handle as `NewTimer`. The handle
auto-cancels itself when the iteration count exhausts.

Implementation notes (apply to all three functions):

- One unified timer queue walked once per frame from
  `FUN_WORLD_TICK`'s subscription. Same hook the
  `BAG_UPDATE_DELAYED` flusher uses, so there's no per-feature hook
  overhead.
- Resolution is one frame (~16ms at 60fps). A timer scheduled for
  `0.001` seconds will fire on the next tick, same as one
  scheduled for `0`.
- The timer queue uses a monotonic clock (`QueryPerformanceCounter`),
  so timers don't drift if the system clock changes mid-session.
- Callbacks are pinned in the Lua registry under per-timer string
  keys so they survive GC between scheduling and firing.

### `C_DateAndTime` overview

Backport of the modern `C_DateAndTime` namespace — calendar-style
date math built on top of `GetServerTime()`. All seven functions
exchange `CalendarTime` tables with these fields (matching
Blizzard's `TimeDocumentation.lua`):

| Field | Range | Notes |
|-------|-------|-------|
| `year` | full year, e.g. 2026 | |
| `month` | 1..12 | luaIndex (1-based) |
| `monthDay` | 1..31 | luaIndex (1-based) |
| `weekday` | 1..7 | luaIndex; 1 = Sunday |
| `hour` | 0..23 | |
| `minute` | 0..59 | |

> **Daily reset semantics.** `GetSecondsUntilDailyReset` treats reset
> as midnight in server wall-clock time. This is exactly what
> `GetServerTime() % 86400 == 0` gives you — the engine's gametime
> components are converted to an epoch by treating them as UTC, so
> day boundaries in epoch math align with server-clock midnight.
>
> **Weekly reset not implemented.** Vanilla has no server-broadcast
> weekly reset schedule, and Turtle WoW realm schedules vary, so
> `C_DateAndTime.GetSecondsUntilWeeklyReset` would have to hardcode a
> weekday/hour that's wrong on some realms. Addons that need it can
> compute their own value from the daily reset + a CVAR.

### `C_DateAndTime.GetCurrentCalendarTime()`

Returns the server clock as a CalendarTime table, or nothing before
login. Equivalent to `GetCalendarTimeFromEpoch(GetServerTime())`.

```lua
local t = C_DateAndTime.GetCurrentCalendarTime()
-- t = { year=2026, month=5, monthDay=11, weekday=2, hour=14, minute=30 }
```

### `C_DateAndTime.GetCalendarTimeFromEpoch(epoch)`

Converts a Unix epoch (seconds since 1970-01-01 UTC) to a
CalendarTime table. Pure `gmtime`; no engine state touched.

### `C_DateAndTime.AdjustTimeByDays(date, days)` / `AdjustTimeByMinutes(date, minutes)`

Returns a new CalendarTime offset from the input by the given number
of days (or minutes). Negative deltas walk backwards. Out-of-range
inputs (`month=13`, `monthDay=32`) normalize via the underlying
`_mkgmtime` / `gmtime` round-trip.

```lua
local today = C_DateAndTime.GetCurrentCalendarTime()
local tomorrow = C_DateAndTime.AdjustTimeByDays(today, 1)
local yesterday = C_DateAndTime.AdjustTimeByDays(today, -1)
```

### `C_DateAndTime.CompareCalendarTime(lhs, rhs)`

Returns `-1` if `lhs < rhs`, `0` if equal, `1` if `lhs > rhs`.
Compares by epoch conversion so denormalized inputs sort
consistently.

### `C_DateAndTime.GetServerTimeLocal()`

Returns the server's wall clock re-interpreted as a Unix epoch in
the **player's** local timezone. Useful when you want to feed a
server-clock value into Lua's `date(format, epoch)` and have the
formatted string show the server's apparent hour/minute. If the
server reports 14:30 and the player is in UTC-5, this returns the
epoch that corresponds to 14:30 in UTC-5 (which is 19:30 UTC).

### `C_DateAndTime.GetSecondsUntilDailyReset()`

Returns seconds until the next server midnight (00:00:00 in server
wall-clock time). Returns 0 if called before login.

> See the overview's "Daily reset semantics" note — this uses server
> midnight, not UTC midnight (though they happen to coincide for
> Turtle WoW since the server reports UTC-aligned components).

## Totem

### `GetTotemInfo(slot)`

Returns `haveTotem, totemName, startTime, duration, icon, modRate, spellID`
for one of the four shaman totem slots (`1` Fire, `2` Earth, `3` Water,
`4` Air).

```lua
local _, name, start, duration, icon = GetTotemInfo(1)   -- Fire slot
if name ~= "" then                                       -- a totem IS active
    local remaining = duration - (GetTime() - start)
    -- name = "Searing Totem", icon = "Interface\\Icons\\...", etc.
end
```

| Return | Meaning |
|--------|---------|
| `haveTotem` | `true` if the player carries the slot's **totem tool** (Fire/Earth/Water/Air Totem item) — the documented meaning, **not** whether a totem is summoned. |
| `totemName` | The active totem spell's localized name (`""` when no totem is out — this is how you test "is a totem active"). |
| `startTime` | `GetTime()` value when the totem was cast (`0` when none). |
| `duration` | Total duration in seconds (`0` when none). |
| `icon` | The active totem spell's icon texture path, or `nil` when none. |
| `modRate` | Always `1` — vanilla has no per-totem haste. |
| `spellID` | The active summon spell's ID (`0` when none). |

> **`haveTotem` is tool presence, not summon state.** Matching the live
> API: `haveTotem` reflects whether you hold the slot's totem **tool** item
> (`Fire Totem` 5176, `Earth Totem` 5175, `Water Totem` 5177, `Air Totem`
> 5178) in bags/equipped, independent of whether a totem is currently
> deployed. The tool item is the totem spell's `Totem[0]` requirement (a
> *tool*, not a consumed reagent), read from the spell — not hardcoded. To
> check for an *active* totem, test `totemName ~= ""` (or `startTime > 0`).

Vanilla 1.12 has no client-side totem tracking (the totem bar,
`PLAYER_TOTEM_UPDATE`, and `GetTotemInfo` are all TBC additions), so this
is self-tracked but **data-driven**: the slot is read from the summon
spell's `Spell.dbc` effect (`SUMMON_TOTEM_SLOT1..4` = Fire/Earth/Water/Air),
duration from `SpellDuration.dbc`, the totem's creature entry (for death
detection) from the effect's `EffectMiscValue`, and the `haveTotem` tool
item from the spell's `Totem` field (the totem the spell requires) —
resolved from the player's spellbook, not a hardcoded table. Casts are observed
via the `SMSG_SPELL_GO` hook; a slot clears when its duration elapses OR
when the totem creature disappears early (killed, Totemic Recall, or
destroyed) — detected by scanning the object manager for the player-owned
totem creature. Because it's data-driven, Turtle-custom totems are tracked
automatically. Slot changes fire
[`PLAYER_TOTEM_UPDATE`](#player_totem_update-event).

### `GetTotemTimeLeft(slot)`

Returns the seconds remaining before the slot's totem is auto-destroyed,
or `0` when no totem is active in that slot. Slots: `1` Fire, `2` Earth,
`3` Water, `4` Air.

```lua
local remaining = GetTotemTimeLeft(1)   -- Fire slot, e.g. 12.4
```

Computed from the same tracker as [`GetTotemInfo`](#gettoteminfoslot)
(`startTime + duration − now`), so it shares that data's caveats — most
notably it reflects the totem's *full* duration from cast, unaffected by
any server-side early expiry the client isn't told about (the WorldTick
scan still clears the slot when the totem creature actually disappears).

### `GetTotemDuration(slot)`

Returns the slot's active totem's **total** duration in seconds (its full
lifetime from cast), or `0` when no totem is active. The companion to
[`GetTotemTimeLeft`](#gettotemtimeleftslot) (remaining time) — same value
as `GetTotemInfo`'s 4th return.

```lua
local total = GetTotemDuration(1)                       -- e.g. 60
local frac  = GetTotemTimeLeft(1) / GetTotemDuration(1) -- bar fill
```

### `TargetTotem(slot)`

Targets the player's totem in the given slot (`1` Fire, `2` Earth, `3`
Water, `4` Air). No-op when the slot has no active totem or the totem
creature isn't currently visible in the client.

```lua
TargetTotem(2)   -- target your Earth totem
```

Looks up the totem creature's live GUID (object-manager scan for the
player-owned creature of the slot's tracked entry) and sets the target
through the same engine path `TargetUnit` uses (`CMSG_SET_SELECTION`).

## TradeSkillUI

Backports the 2.x+ **trade-skill list link** — the shareable
`|Htrade:...|h[Profession]|h` link that shows a player's profession and
which recipes they know. Vanilla 1.12 has no such link type (the engine's
hyperlink parser only knows `item:` and `enchant:`), so both the link and the
click-to-view UI are synthesized. Clicking a received link opens a
crafting-window-style frame (the real `TradeSkillFrame` parchment art) listing
the recipes the linker knows — difficulty-coloured and sorted high-craft-first
— with a skill-rank bar and an "X of Y recipes known" summary.

The "known recipes" data is a bitfield over the skill line's **canonical
recipe list** — every `SkillLineAbility.dbc` row for the skill line that is a
craftable recipe, in ascending record-ID order. That ordering is identical on
every client with the same DBC, so the sender and reader agree on the
bit-index space with no handshake. Whether the sender knows a given recipe
comes from the same player-spell bitmap `IsPlayerSpell` reads (which covers
profession recipes), so the link is self-contained — the reader needs neither
the sender online nor the profession itself.

### `C_TradeSkillUI.GetTradeSkillListLink()`

Returns the `|Htrade:...|h` link for the **currently-open** trade skill
window, or `nil` if no profession window is open.

```lua
-- with a profession window open:
local link = C_TradeSkillUI.GetTradeSkillListLink()
-- "|cffffd000|Htrade:164:300:300:Bob:eE0C...|h[Blacksmithing]|h|r"
ChatEdit_InsertLink(link)   -- or SendChatMessage(link, "GUILD"), etc.
```

Built entirely in the DLL — no addon wiring. The window only needs to be open
long enough to identify the profession; the recipe data is read from the spell
bitmap, not the window's server list. Wire format (a ClassicAPI-private shape
modeled on the 2.x link, with the linker's name in the slot 2.x used for the
GUID):

```
|cffffd000|Htrade:<skillLineID>:<curRank>:<maxRank>:<linkerName>:<base64 bits>|h[Name]|h|r
```

`skillLineID` is the `SkillLine.dbc` row (164 = Blacksmithing); `curRank` /
`maxRank` are the linker's skill level (cosmetic); `linkerName` is the linking
character's name (shown in the viewer title); the base64 tail packs 6 recipes
per character, LSB-first. Links from before this field was added (no
`linkerName`) still decode — the viewer just omits the name from its title.

Clicking a `trade:` link is handled automatically (an override of
`SetItemRef`) — it opens the recipe viewer. No addon wiring needed.

### `C_TradeSkillUI.GetCraftListLink()`

Same as `GetTradeSkillListLink`, but for the **Craft window** — vanilla's
separate profession frame used by Enchanting and pet training. Returns `nil`
unless `CraftFrame` is shown. The two windows have separate storage and can be
open simultaneously, so they get separate builders (matching WotLK); the
resulting link is the same `|Htrade:...|h` format and opens the same viewer.

```lua
local link = C_TradeSkillUI.GetCraftListLink()   -- with the enchanting window open
-- "|cffffd000|Htrade:333:300:300:Bob:...|h[Enchanting]|h|r"
```

### `C_TradeSkillUI.GetTradeSkillListRecipes(skillLineID, bits)`

Decodes a link's bitfield against a skill line's canonical recipe list.
Returns an array table:

```lua
local recipes = C_TradeSkillUI.GetTradeSkillListRecipes(164, "eE0C...")
-- recipes[i] = { spellID, isKnown, trivialLevel, greenLevel, createdItem }
for i = 1, table.getn(recipes) do
    local name = GetSpellInfo(recipes[i].spellID)
    print(name, recipes[i].isKnown and "known" or "unknown")
end
```

`trivialLevel` / `greenLevel` are the skill levels the recipe turns grey /
green at — the recipe's difficulty band, coloured against the linker's skill
exactly as the default UI does (the required-skill field is uselessly `1` for
nearly every trainer-taught recipe). `createdItem` is the crafted item's ID
(`0` for enchants), which the viewer uses to build its subclass filter.

The array is in canonical recipe order (one entry per *possible* recipe in the
skill line, not just the known ones). Turn a `spellID` into a name/icon with
[`GetSpellInfo`](#getspellinfospellid). Recipes past the end of a short/garbled
`bits` string decode as not-known rather than erroring.

## UIColor

### `C_UIColor.GetColors()`

Returns an array of rows, each shaped
`{ baseTag = "FOO_COLOR", color = ColorMixin }`, mirroring the modern
function of the same name. The `color` field is a real `ColorMixin`
instance (carries `GetRGB`, `GenerateHexColorMarkup`, etc.) — the DLL
calls back into Lua's `CreateColor(r,g,b,a)` per row to construct it,
so `ColorMixin` and `CreateColor` must already be defined when
`GetColors` is called.

The companion addon `!!!ClassicAPI/Util/Color.lua` does both: it
defines `ColorMixin`/`CreateColor` first, then loops the result the
same way `Blizzard_SharedXMLBase/Color.lua` does on modern WoW —
assigning each row as a global under its `baseTag` plus a `_CODE`
variant holding the `|c`-prefixed hex markup:

```lua
for _, dbColor in ipairs(C_UIColor.GetColors()) do
    local color = CreateColor(dbColor.color.r, dbColor.color.g, dbColor.color.b, dbColor.color.a)
    _G[dbColor.baseTag] = color
    _G[dbColor.baseTag.."_CODE"] = color:GenerateHexColorMarkup()
end
```

After that runs, addons can use the standard modern color globals
directly:

```lua
/dump ACHIEVEMENT_COLOR:GetRGB()       -- 1, 1, 0
/dump ITEM_EPIC_COLOR:GenerateHexColor()
ITEM_EPIC_COLOR_CODE .. "Legendary" .. "|r"
```

The data comes from a snapshot of `GlobalColor.dbc` taken from BC
Classic 2.5.5 (build 67323) — vanilla 1.12 has no `GlobalColor.dbc`,
so the rows are embedded in the DLL (see [`src/ui/ColorData.h`](../src/ui/ColorData.h)).
Duplicate `baseTag`s in the source DBC (`PURE_RED_COLOR`,
`INVASION_*`, etc.) are deduplicated keeping the higher-ID entry,
matching what `ipairs(DBColors)` ends up assigning on modern. Colors
introduced after BC (Death Knight runes, Mythic+ medals, healing
absorbs, glyphs, objective tracker) aren't in the snapshot and so
aren't surfaced — none have a use case in 1.12 anyway.

If `CreateColor` happens not to be defined when `GetColors` is called
(e.g. another DLL or addon manages to call us before `!!!ClassicAPI`
loads), each `color` field falls back to a plain `{r,g,b,a}` table.
The loop above tolerates both shapes — `dbColor.color.r` reads the
same way either way — so consumers shouldn't notice.

## Unit

### `UnitGUID(unit)`

Returns the unit's 64-bit GUID formatted as a hex string
`"0xHHHHHHHHLLLLLLLL"` (16 hex digits, hi dword first), or `nil` if the
resolved unit's GUID is empty (e.g. `"target"` with nothing targeted,
empty party/raid slot).

```lua
local guid = UnitGUID("player")  -- "0x0000000000000777" (low IDs are local-realm characters)
local guid = UnitGUID("target")  -- "0xF13000059A002553" (the F130... prefix tags creatures)
local guid = UnitGUID("party1")  -- works even if party1 is on a different continent
```

The returned string is itself accepted as a unit token — `UnitName(guid)`,
`UnitHealth(guid)`, etc. all work — so a GUID round-trips as a stable
handle. See [Unit tokens (GUID literals)](#unit-tokens-guid-literals).

**Works for OOR party / raid members.** Earlier versions of this
function returned nil for `"partyN"` / `"raidN"` when the member's
CGObject wasn't currently active in the client (e.g. on another
continent, in a different zone phase). We now read from the engine's
parallel group-roster GUID array (populated by `SMSG_GROUP_LIST`),
which is independent of unit visibility.

> **Vanilla format, not modern.** Vanilla GUIDs are plain 64-bit
> integers — there's no `"Player-RealmID-CharacterID"` /
> `"Creature-0-0-MapID-..."` prefix system that modern WoW uses
> (introduced in 6.0). Addons backporting modern GUID-parsing code
> will need to either accept the `"0x..."` form or extract the raw
> hex.

> **Errors on invalid unit tokens.** Same behavior as vanilla's other
> unit-token functions (`UnitAffectingCombat`, `UnitName`, etc.) —
> passing a string that doesn't match a known unit ID raises a Lua
> error rather than returning nil. Modern WoW silently returns nil;
> we match the engine's existing convention here. Unit tokens that
> resolve to "no current unit" (like `"target"` with nothing
> targeted) return nil cleanly via the GUID = 0 check.

### `UnitTokenFromGUID(guid)`

Best-effort reverse lookup: given a GUID string in the
`"0xHHHHHHHHLLLLLLLL"` format `UnitGUID` returns, walk the engine's
known unit tokens and return the first one currently mapped to that
GUID, or `nil` if none of them point at it.

The search order matches modern retail with post-1.12 tokens
omitted (`vehicle`, `arenaN`, `arenapetN`, `bossN`, `softenemy`,
`softfriend`, `softinteract` all post-date vanilla and the engine's
resolver doesn't recognize them). `nameplateN` and `focus` are
included — we hook the resolver so the engine recognizes both token
forms. As with the other positional tokens, a returned
`"nameplate3"` result is only valid at that instant; the slot may
shift to a different unit between calls, so re-verify with
`UnitGUID(token)` before reusing.

```
player → pet → party1..4 → partypet1..4 → raid1..40
       → raidpet1..40 → nameplate1..N → target → focus
       → npc → mouseover
```

```lua
local name, guid = GameTooltip:GetUnitGUID()
local token = UnitTokenFromGUID(guid)
if token then
    -- can now feed `token` to UnitHealth / UnitClass / etc.
end
```

> **The return is inherently unstable.** Multiple tokens can map to
> the same GUID at once (your target IS your party1, your pet IS
> your raidpet5 if you're in a raid), and the mapping changes every
> time `SMSG_GROUP_LIST` fires, the player tabs target, or a party
> member loads in. Modern's API has the same warning. If you cache a
> result, re-verify with `UnitGUID(token) == originalGuid` before
> reusing it.
>
> Returns `nil` for malformed GUID strings, zero GUIDs, and GUIDs
> that don't match any currently-resolvable token — including
> ex-targets, ex-mouseover units, distant players seen in the chat
> log, etc. The engine simply doesn't address those by token.

### `UnitSubName(unit)`

Returns the NPC's subtitle / title string (the small italic text
under their name in the tooltip): `"Innkeeper"`, `"Stable Master"`,
`"<Master Engineer>"`, etc. Returns `nil` for players, unsynced
NPCs, and NPCs with no subtitle.

```lua
/dump UnitSubName("target")
-- "Innkeeper"             (Stormwind innkeep)
-- "<Auction House>"       (auctioneer)
-- nil                     (a player, or a wolf in the woods)
```

Modern WoW returns the subtitle as `UnitName`'s second return;
vanilla 1.12's `UnitName` returns only one value, leaving no
direct route to the subtitle. Addons that wanted it had to scrape
`GameTooltip` text, which is fragile and required the unit to be
hovered. `UnitSubName` reads it straight off the engine's creature
cache (`[unit + 0xB30] → [+0x10]`), no tooltip round-trip.

Coverage caveat: the creature cache row is populated when an NPC
becomes visible to the client AND the server has sent its
`CMSG_CREATURE_QUERY_RESPONSE`. The first time a fresh NPC appears
the row may be briefly NULL — usually only one or two frames.
Subsequent calls succeed once the response lands.

### `UnitCreatureFamilyID(unit)`

Returns the numeric **CreatureFamily** id (the `CreatureFamily.dbc`
row — `1` = Wolf, `2` = Cat, `3` = Spider, …) for a unit, or `nil`
when it has no family (players, non-beast NPCs, unsynced NPCs).

```lua
/dump UnitCreatureFamilyID("pet")     -- e.g. 1 for a wolf pet
/dump UnitCreatureFamilyID("target")  -- 2 for a cat, nil for a humanoid
```

Stock `UnitCreatureFamily(unit)` returns only the *localized name*
(`"Wolf"`, `"Chat"`) — awkward to compare and locale-dependent. This
extension exposes the raw id addons actually want for stable checks
(tameable-pet filtering, family-specific ability tables). It's the same
id the engine's `UnitCreatureFamily` resolves internally
(`[unit + 0xB30] → [+0x1C]`) before mapping it through
`CreatureFamily.dbc` for the display name — this just stops at the id.
`nil` semantics match `UnitCreatureFamily` (both return nothing for a
family of 0), and it shares `UnitSubName`'s creature-cache coverage
caveat (a fresh NPC's row may be briefly NULL for a frame or two).

### `UnitCreatureTypeID(unit)`

Returns the numeric **CreatureType** id (the `CreatureType.dbc` row —
`1` = Beast, `3` = Demon, `7` = Humanoid, …) for a unit, or `nil` when
it has no resolvable type. The raw-id twin of `UnitCreatureFamilyID`.

```lua
/dump UnitCreatureTypeID("target")   -- 7 for a humanoid, 1 for a beast
/dump UnitCreatureTypeID("player")   -- 7 (Humanoid, via the player's race)
```

Stock `UnitCreatureType(unit)` returns only the *localized name*
(`"Humanoid"`, `"Humanoïde"`) — awkward to compare and locale-dependent.
This exposes the id addons want for stable checks. It calls the same inner
resolver the engine's `UnitCreatureType` uses, which handles every unit kind
(NPCs via the creature cache, players via their race → Humanoid). Pair with
[`C_CreatureInfo.GetCreatureTypeInfo`](#c_creatureinfogetcreaturetypeinfocreaturetypeid)
to get the localized name back from the id.

### `GetUnitSpeed(unit)`

Returns `currentSpeed, runSpeed, flightSpeed, swimSpeed` — all four
in yards/second. Modern WoW signature exactly; 1.12 doesn't have
flying, so `flightSpeed` is always `0`.

```lua
local current, run, _, swim = GetUnitSpeed("player")
-- e.g. 7.0, 7.0, 0, 4.722 for an unmounted player running normally
-- e.g. 14.0, 14.0, 0, 4.722 with a 100% mount active (run speed
--      reflects the modifier; swim ignores mount)
```

- **`currentSpeed`** — speed the engine would apply to this frame's
  movement step. `0` when stationary; otherwise the walk/run/swim/
  backward variant the unit is actually moving with. Read via the
  engine's own selector at `0x007C4C90` so movement-flag handling
  (walking, swimming, taxi paths) matches the engine exactly.
- **`runSpeed`** — raw forward-run speed from MovementInfo `+0x8C`.
  Includes mount / buff / debuff multipliers — the engine maintains
  this field as the post-modifier value, updated by
  `SMSG_FORCE_RUN_SPEED_CHANGE` and friends.
- **`flightSpeed`** — always `0` in 1.12.
- **`swimSpeed`** — raw forward-swim speed from MovementInfo `+0x94`.

All four returns are `0` if the token doesn't resolve to a live
CGUnit — empty `"target"`, out-of-range party member, etc. Matches
3.3.5's `Script_GetUnitSpeed` behavior of pushing `0.0` rather than
nil for non-visible units.

Reads `[CGUnit + 0x118]` to get the MovementInfo pointer, then
reads speed fields directly. Field offsets verified via the
movement-prediction loop (`FUN_005FC350` and the sprint-jump
calculator at `0x00511920` both reach into the same struct).

### `UnitIsAFK(unit)`

Returns `true` if the unit is currently AFK (toggled via `/afk` or
auto-set after idle timeout). Works for any player-controlled unit
— local self, target, party*, raid*, inspect targets. NPCs always
return `false`.

```lua
UnitIsAFK("player")   -- true if you've /afk'd
UnitIsAFK("target")   -- true if the targeted player is AFK
UnitIsAFK("party1")   -- true if party member 1 is AFK
UnitIsAFK("npc")      -- always false
```

> **How it works under the hood.** Vanilla 1.12 doesn't broadcast
> PLAYER_FLAGS as a UpdateField (modern WoW does — that field was
> added 3.0+), but every nearby player's CGPlayer-side info struct
> at `[unit + 0xE68]` carries it at byte +0x08. Same struct the
> engine reads when rendering the `<AFK>` prefix above a player's
> head. Verified against the in-game nameplate behavior.

### `UnitIsDND(unit)`

Returns `true` if the unit is currently in DND mode ("Do Not Disturb",
toggled via `/dnd`). Same unit-coverage as `UnitIsAFK` — any
player-controlled unit, false for NPCs.

```lua
UnitIsDND("player")
UnitIsDND("target")
```

### `UnitIsFeignDeath(unit)`

Returns `true` if the unit is feigning death (Hunter's `Feign Death`).
Reads `UNIT_FIELD_FLAGS` bit 29 (`0x20000000`) from the broadcast
descriptor — works for any unit since UNIT_FIELD_FLAGS is broadcast
in object updates.

```lua
UnitIsFeignDeath("target")   -- true if a feigning hunter
```

### `UnitIsInMyGuild(unitOrName)`

Returns `1` if the unit/character shares the player's guild, `nil`
otherwise. Accepts either a unit token (`"player"`, `"target"`,
`"party1"`, etc.) or a literal character name (`"Bob"`), matching
3.3.5's `Script_UnitIsInMyGuild` (`0x0060C4B0` in the Frostmourne
client).

```lua
UnitIsInMyGuild("player")    -- 1 if you're in any guild
UnitIsInMyGuild("target")    -- 1 if your target is a guildmate
UnitIsInMyGuild("party1")    -- 1 if party member 1 is a guildmate
UnitIsInMyGuild("Bob")       -- 1 if there's a guildmate named Bob
```

Resolution strategy:

1. Calls `UnitName(input)` via `lua_pcall` to canonicalize the input.
   Tokens resolve cleanly; literal names hit the engine's "Unknown
   unit name" error which `pcall` swallows.
2. For valid tokens, attempts a fast direct comparison against the
   player's guild-key field at `[unit + 0xE68 + 0x0C]` — same field
   `GetGuildInfo` reads, populated immediately on guild join for the
   local player and for any synced player-controlled unit. No roster
   fetch needed for nearby/loaded units.
3. Falls back to walking the engine's guild roster array (same
   backing storage `GetGuildRosterInfo` reads) by name. Required for
   out-of-range party members, distant raid members, and literal
   name input — needs `GuildRoster()` to have been called and the
   server's response to have arrived.

Return convention matches 3.3.5: `1.0` / `nil`, not boolean. Both
work for `if UnitIsInMyGuild(x) then` checks.

The slow path reads the engine's full roster count
(`[0x00B73118]`, the same value `GetNumGuildMembers(true)` returns),
not the online-only count, so offline guildmates resolve too — the
"show offline" toggle doesn't affect lookup. The only requirement
is that `GuildRoster()` has been called and the server's
SMSG_GUILD_ROSTER response has arrived.

### `UnitIsPossessed(unit)`

Returns `true` if the unit is currently possessed (priest's `Mind
Control`, warlock's `Subjugate Demon`). Reads `UNIT_FIELD_FLAGS` bit
24 (`0x01000000`) — the standard vanilla `UNIT_FLAG_POSSESSED` per
emulator sources — directly off the unit's m_objectFields descriptor.
Works for any unit token since UNIT_FIELD_FLAGS is broadcast in
object updates.

```lua
UnitIsPossessed("target")   -- true if mind-controlled
```

Distinct from `UnitIsCharmed`: charm covers any charm-type effect
(including pets summoned via mob-charm spells), possess is the
specific spell-driven take-over effect modern WoW splits out.

### `UnitIsMinion(unit)`

Returns `true` if `unit` is a **minion of a player** — a pet, guardian,
totem, or charmed creature — `false` otherwise. Primarily owner-based: the
unit's owner GUID (SummonedBy / CreatedBy / CharmedBy) must resolve to a
player, with "player-controlled but not itself a player" as a fallback.
Owner-based is needed because summoned **guardians** (shown as "X's
Minion") carry an owner but not the `PLAYER_CONTROLLED` flag, so a
flag-only check would miss them. Players, NPCs, and world creatures return
`false`.

A modern (12.0.0-era) addition with no vanilla or 3.3.5 equivalent —
backported here from vanilla primitives. A bad or unresolved unit token
returns `false`.

```lua
UnitIsMinion("pet")     -- your pet → true
UnitIsMinion("player")  -- you → false
UnitIsMinion("target")  -- another player's felhunter → true; a boar → false
```

### `UnitIsPet(unit)`

Returns `true` if `unit` is a **pet** of a player (as opposed to a
guardian/totem "minion"), `false` otherwise — the pet↔minion
discriminator. Uses the engine's own classifier: owned by a player **and**
the family-based pet/minion test that drives the `UNITNAME_TITLE_PET` vs
`UNITNAME_TITLE_MINION` tooltip label. Neither the GUID (pets and guardians
share the `0xF14…` prefix on this client) nor `UNIT_FLAG_PLAYER_CONTROLLED`
(a "Minion" can carry it) reliably separates them, so the creature-family
classifier is the correct signal; the owner-is-player gate also excludes
wild tameable beasts of the same family.

`UnitIsMinion` is the umbrella (pet + guardian + totem); `UnitIsPet` is the
controllable subset. A ClassicAPI extension, not a stock WoW global. A bad
or unresolved unit token returns `false`.

```lua
UnitIsPet("pet")                 -- your pet → true
-- another player's "X's Pet"    → UnitIsMinion true, UnitIsPet true
-- another player's "X's Minion" → UnitIsMinion true, UnitIsPet false (guardian, not controllable)
```

### `UnitIsOtherPlayersPet(unit)`

Returns `true` if `unit` is a minion (pet, guardian, totem, or charmed
creature) whose owner is a player **other than you**, `false` otherwise.
Owner-based, matching 5.4.8's `Script_UnitIsOtherPlayersPet`: it reads the
unit's owner GUID (SummonedBy / CreatedBy / CharmedBy), requires that owner
to be a player, and requires it to differ from you. World creatures and
players (no owner) return `false`; your own minions (owner is you) return
`false`. A bad or unresolved unit token returns `false`.

```lua
UnitIsOtherPlayersPet("target")  -- a party member's pet → true; your own pet → false
```

### `UnitOwnerGUID(unit)`

Returns the GUID string (`"0x…"`) of the unit's owner — the summoner of a
pet/guardian/totem, or the charmer of a charmed unit. `nil` for an
unresolved unit and for anything with no owner (players, world creatures).

```lua
local owner = UnitOwnerGUID("pettarget")   -- e.g. the enemy hunter's GUID
if owner == UnitGUID("player") then ... end  -- is it my minion?
```

Reads the same owner field the pet predicates use (`CharmedBy`, else
`CreatedBy` in the unit's `m_objectFields`).

### `UnitStandState(unit)`

Returns the unit's standstate as an integer, matching the modern
`Enum.PlayerStandState` values:

| Value | Meaning |
|------:|---------|
| `0` | STANDING |
| `1` | SITTING |
| `2` | SITTING_CHAIR |
| `3` | SLEEPING |
| `4` | SITTING_LOW_CHAIR |
| `5` | SITTING_MEDIUM_CHAIR |
| `6` | SITTING_HIGH_CHAIR |
| `7` | DEAD |
| `8` | KNEELING |

Reads the low byte of `UNIT_BYTES_1` (descriptor `+0x210`), a
broadcast UpdateField — works for any synced unit (player, target,
party*, raid*, mouseover, etc.). Returns `0` (STANDING) for
unresolvable units (empty `party*` slot, no current target, etc.)
matching the modern behavior of returning a safe default.

```lua
UnitStandState("player")    -- 0 standing, 1 sitting, 5 chair-sit, …
UnitStandState("target")    -- works for any visible unit
UnitStandState("party1")    -- 0 if the slot is empty
```

1.12 has `IsSitOrStanding()` (local-player boolean) but no
unit-token form; `UnitStandState` fills the gap and exposes the
full enum.

### `UnitInRange(unit)`

Returns `(inRange, checkedRange)` — whether `unit` is within 40 yards
of the player, plus a flag indicating whether the range check could
actually be performed.

```lua
local inRange, checked = UnitInRange("party1")
if checked and not inRange then
    -- partymate is loaded but >40 yards away → skip the heal
end
```

| Return | Meaning |
|--------|---------|
| `inRange` | `true` if the unit is within a 40-yard heal's reach of the player. `false` if out of range OR if `checkedRange` is `false`. |
| `checkedRange` | `true` when a position-based range check was performed. `false` when the unit's position isn't available — either the token didn't resolve (empty `partyN` slot, no target, raid member in a different zone, etc.) or `unit == "player"` (see below). |

Reads the world position via the `CGObject::GetPosition` vtable
virtual (slot 5, offset `+0x14`) — same path
`CheckInteractDistance` uses.

**Reach-aware.** The threshold is not a flat 40 yards on the raw
center-to-center distance — it's `40 + playerReach + targetReach`,
mirroring the engine's own spell-range formula (`FUN_006e3480`), where
each unit's reach is the bounding-radius float at `[m_objectFields +
0x1F0]` (the same size factor the interact-distance and loot-range
checks add). A 40-yard heal lands on a target whose *center* is up to
`40 + reach` yards away, so a bare center-distance cap of 40 wrongly
reports out-of-range for a target the client can actually heal (on a
large target the reach gap is 2–3 yards). If a unit's descriptor isn't
populated yet the reach term is 0 and the check degrades to a plain
40-yard cap. Note this is the one difference from `UnitDistanceSquared`,
which stays raw center-to-center (matching retail).

> **`UnitInRange("player")` returns `(false, false)`** by design,
> matching modern WoW's behavior. The function is meant for healing-
> frame "is *another* unit in range" checks; querying yourself is
> meaningless. We short-circuit before the position read so the
> result is unambiguous (`checkedRange=false`).

Works for any synced unit (party/raid members in your zone, target,
mouseover, etc.). For raid members in a different zone or otherwise
not in the engine's sync window, the position virtual returns null
and the function reports `(false, false)`.

### `UnitDistanceSquared(unit)`

Returns `(distanceSquared, checkedPosition)` — the **squared** world
distance (yards²) from the player to `unit`, and a flag indicating
whether both positions were available.

```lua
local distSq, checked = UnitDistanceSquared("target")
if checked and distSq <= 30 * 30 then
    -- target is within 30 yards
end
```

| Return | Meaning |
|--------|---------|
| `distanceSquared` | Squared distance player→unit. `0` when `checkedPosition` is `false` (a placeholder — matches retail's "always a number" shape). |
| `checkedPosition` | `true` when both positions were read. `false` when `unit`'s position isn't available (empty `partyN` slot, no target, raid member outside the sync window, etc.). |

The value is **squared** on purpose: nearly all distance logic is a
threshold compare (`distSq <= range * range`) or a nearest-unit sort,
neither of which needs the square root — so retail exposes only the
squared form (added 5.0.4) and never a plain `UnitDistance`. Take
`math.sqrt(distanceSquared)` only when you need a yard number to show a
human.

Reads world positions via the `CGObject::GetPosition` vtable virtual
(slot 5, offset `+0x14`) — the same path `UnitInRange` /
`CheckInteractDistance` and our own [`UnitPosition`](#unitpositionunit)
use. **Center-to-center**, not reach-aware (that edge-to-edge nicety is
UnitXP_SP3's niche); the value equals what you'd compute from the raw
`UnitPosition` coordinates, and it's self-contained (no sibling-DLL
dependency).

> No self-quirk (unlike `UnitInRange`): `UnitDistanceSquared("player")`
> returns a legitimate `(0, true)`. Because a real `0` (self, or two
> exactly co-located units) is indistinguishable by value from the miss
> placeholder, always branch on `checkedPosition`.

### `UnitPosition(unit)`

Returns `posY, posX, posZ, instanceID` — the unit's world position, in
the modern (WoD+) `UnitPosition` shape. `nil` when the unit has no known
position.

```lua
local posY, posX, posZ, instanceID = UnitPosition("player")
```

| Return | Meaning |
|--------|---------|
| `posY` | World Y coordinate (the west axis). |
| `posX` | World X coordinate (the north axis). |
| `posZ` | World Z coordinate (height). |
| `instanceID` | Currently-loaded map id (`Map.dbc` row — e.g. `0` Eastern Kingdoms, `1` Kalimdor). Every visible unit shares the player's instance. |

**Order matches retail** — `posY` (west) first, then `posX` (north), then
`posZ`, mirroring Blizzard's own `UnitPosition`. So an addon written for
retail that does `local py, px = UnitPosition(u)` gets its coordinates
labelled as expected. (World X is north/south, Y is west/east — WoW's
long-standing convention.)

**Not group-restricted.** Retail returns `nil` for units outside your
party/raid (a privacy guard that doesn't exist in 1.12). This backport
reads any *visible* unit — `"target"`, `"mouseover"`, nameplate tokens,
arbitrary party/raid members in sync range — so it's strictly more
permissive, closer to SuperWoW's unit-position access. Units the client
can't currently see (out of range, another zone) have no position → `nil`.

Reads through the same `CGObject::GetPosition` virtual as
[`UnitDistanceSquared`](#unitdistancesquaredunit); the two are consistent
(the squared delta of two `UnitPosition` reads equals
`UnitDistanceSquared`).

> **SuperWoW interaction.** SuperWoW also defines a global `UnitPosition`
> with a different return shape. Both register on the same Lua state, so
> if SuperWoW is loaded the last registrant wins — install order decides
> which shape is live. Without SuperWoW, this retail-shaped version is the
> one you get.

### `UnitInLineOfSight(unit)`

Returns `true` if the player has clear line of sight to `unit`, `false`
if world geometry (terrain or a building) blocks it, and `nil` when the
check can't apply — an absent / unresolvable token, or a non-unit
object. A ClassicAPI extension (retail has no such global).

```lua
if UnitInLineOfSight("target") then
    -- clear shot
end
```

Traces a ray between the two units through the client's world-collision
geometry (`CWorld::Intersect`, flags `terrain + WMO`). Endpoints are
raised by each unit's collision-box height so a foot-to-foot ray doesn't
false-block on terrain, and the trace runs in two passes (level look,
then look up/down) for height mismatches — the same approach UnitXP_SP3
uses, implemented natively so it works with or without that addon (a
call routes harmlessly through UnitXP_SP3's hook when present).

Notes and limits:
- **Terrain + buildings (WMO) only** — M2 *doodads* (trees, small props)
  are not tested (that flag combination crashes in some dungeons). This
  matches vanilla server-side LoS, which also ignores most doodads.
- **Center/eye-line**, not a volume — a sliver of a unit peeking past a
  corner reads as blocked if the eye-line itself is occluded.
- Pairs beyond ~150 yards report `false` (a guard against a
  long-segment crash in the underlying trace); such units are usually
  outside the client's object sync window anyway.
- `UnitInLineOfSight("player")` is `true` (you always see yourself).

### `UnitClassBase(unit)`

Returns `(classFile, classID)` — the locale-independent class
token plus the numeric classID. The token is one of
`"WARRIOR"`, `"PALADIN"`, `"HUNTER"`, `"ROGUE"`, `"PRIEST"`,
`"SHAMAN"`, `"MAGE"`, `"WARLOCK"`, `"DRUID"`; the classID matches
the integer `UnitClass`'s third return surfaces (1=Warrior,
2=Paladin, 3=Hunter, 4=Rogue, 5=Priest, 7=Shaman, 8=Mage,
9=Warlock, 11=Druid — 6 and 10 are post-vanilla).

```lua
local token, id = UnitClassBase("player")    -- "WARRIOR", 1
local token = UnitClassBase("target")        -- works for any synced unit
local color = RAID_CLASS_COLORS[UnitClassBase("party1")]
```

Modern addons use the token for class detection because vanilla's
`UnitClass(unit)` returns a localized first return (e.g.
`"Krieger"` on a German client), which is fine for display but
breaks any addon code that keys on `if class == "WARRIOR"`. The
classID second return is a vanilla extension — modern's
`UnitClassBase` returns the token only — but it saves callers
from chaining `UnitClass(unit)` just to get the integer.

Reads the class byte from `UNIT_FIELD_BYTES_0` (descriptor `+0x79`,
the same byte `UnitClass`'s general-token path reads) and looks up
the english filename at `ChrClasses.dbc::Filename` (record `+0x38`).
Both fields are broadcast / static, so remote players' class is
current regardless of distance.

Returns `(nil, nil)` for:
- `target` with no current target, empty `partyN` / `raidN` slots,
  or units whose descriptor isn't loaded yet
- creatures and other non-player units (their class byte indexes a
  different table; the class-byte lookup naturally returns nil for
  out-of-range / zero values)

Throws a Lua error on missing / non-string `unit` argument — same
shape as `UnitClass` itself.

### `UnitRaceBase(unit)`

Returns `(raceFile, raceID)` — the locale-independent race token
plus the numeric raceID. The token is one of `"Human"`, `"Orc"`,
`"Dwarf"`, `"NightElf"`, `"Scourge"`, `"Tauren"`, `"Gnome"`,
`"Troll"`; the raceID is `1..8` for those values respectively.

```lua
local token, id = UnitRaceBase("player")    -- "Human", 1
local token = UnitRaceBase("target")        -- works for any synced unit
```

Sibling to [`UnitClassBase`](#unitclassbaseunit). Same problem
(vanilla's `UnitRace(unit)` returns a localized name — `"Mensch"`,
`"Orc"`, etc.); same solution (locale-independent token straight
from the DBC). Reads byte 0 of `UNIT_FIELD_BYTES_0` (descriptor
`+0x78`) and looks up `ChrRaces.dbc::Filename` (`+0x3C`).

Returns `(nil, nil)` for unresolvable units and non-player units
(creature race bytes don't index `ChrRaces.dbc`).

### `ClosestUnitPosition(creatureID)`

Returns `xPos, yPos, distance` — the world position of the nearest
creature with the given NPC (creature-template) ID, and its distance from
the player in yards. Returns nothing (nil) when no matching creature is in
range.

```lua
local x, y, dist = ClosestUnitPosition(3098)   -- nearest Kobold Vermin
if x then
    -- x, y = world coords; dist = yards from the player
end
```

- `creatureID` (number) — the NPC ID (the value
  [`C_CreatureInfo.GetCreatureID(guid)`](#c_creatureinfogetcreatureidguid)
  returns, and the entry packed into a creature's GUID).

Walks the client's visible-object manager, matches creatures whose GUID
encodes `creatureID` (bits 24–47), and returns the closest one's position
+ center-to-center distance.

> **Differs from retail.** Modern WoW's `ClosestUnitPosition` reads a
> static client-side spawn database and only works for starting-zone mobs.
> Vanilla 1.12 ships no such database, so this returns the nearest
> **currently-visible** creature of that entry (anywhere, not just
> starting zones). It can't point at un-synced spawns elsewhere in the
> zone the way retail's database can — but for "where's the nearest `<mob>`
> I can see" it's more general than retail.

### `UnitHealthMissing(unit)`

The health deficit — `UnitHealthMax(unit) - UnitHealth(unit)` — in one call.
A convenience for healing addons (overheal math, "missing health" bars) that
would otherwise call both engine functions and subtract in Lua every frame.

```lua
local missing = UnitHealthMissing("player")   -- 0 at full health
```

Returns `0` at full health and for a valid-but-absent unit (e.g. `"target"`
with nothing targeted), matching `UnitHealth`'s 0-for-missing convention;
clamped so a transient `current > max` never returns negative.

Tracks the engine's own `UnitHealth` / `UnitHealthMax` exactly, including
vanilla's percentage form for non-grouped units (where `UnitHealthMax` is
`100`): a target at `85/100` reports `15`.

Direct descriptor reads — `desc[+0x40]` (HEALTH), `desc[+0x58]` (MAXHEALTH).
No engine call, no Lua-stack roundtrip.

### `UnitPower(unit [, powerType [, unmodified]])` / `UnitPowerMax(unit [, powerType [, unmodified]])`

Modern multi-power-type getters. Vanilla 1.12 only ships
`UnitMana(unit)` / `UnitManaMax(unit)` which return whichever
primary power the unit happens to have (mana for casters, energy
for rogues, rage for warriors, etc.); these add the explicit
`powerType` arg so addons can read a specific power slot without
caring what the unit's primary is.

```lua
local mana    = UnitPower("player", 0)   -- explicit mana
local energy  = UnitPower("player", 3)   -- explicit energy
local primary = UnitPower("player")      -- whatever the player's primary is
local maxMana = UnitPowerMax("target", 0)
```

`powerType` values (matches modern WoW's enum):

| value | type |
|---|---|
| `0` | MANA |
| `1` | RAGE |
| `2` | FOCUS |
| `3` | ENERGY |
| `4` | HAPPINESS |

These match the [`Enum.PowerType`](#globals) namespace published
alongside — `UnitPower("player", Enum.PowerType.Mana)` is the
idiomatic modern shape.

Omitting `powerType` (or passing `-1` / any out-of-range value)
falls back to the unit's primary power, read from
`UNIT_FIELD_BYTES_0` byte 3 (descriptor `+0x7B`) — same source the
vanilla engine uses internally. Same fallback the 3.3.5 client's
`Script_UnitPower` uses for its `type == 7` sentinel.

Returns `0` for invalid units, unresolvable tokens, or power types
outside the 0..4 vanilla range (Runes / Runic Power are WotLK
additions that don't have descriptor slots in the 1.12 unit
layout).

Display-divisor applied. Vanilla stores some power types at a
scaled value internally and divides before exposing them through
Lua — same trick `Script_UnitMana` (`0x00517670`) uses, reading
the divisor table at `0x0086F978`:

| type | divisor |
|---|---|
| `0` MANA | 1 |
| `1` RAGE | **10** (raw `0..1000` → display `0..100`) |
| `2` FOCUS | 1 |
| `3` ENERGY | 1 |
| `4` HAPPINESS | 1000 |

So a fresh warrior reads `UnitPower("player", 1)` = `0..100`, not
`0..1000`. Matches retail Classic.

Pass a truthy `unmodified` (third arg) to bypass the divisor and get the raw
internal value — `UnitPower("player", 1, true)` returns rage as `0..1000`.
Same third argument retail's `UnitPower` / `UnitPowerMax` take.

Direct descriptor reads — `desc[+0x44 + type*4]` for current power,
`desc[+0x5C + type*4]` for max, divided by the table entry for the
type. No engine call, no Lua-stack roundtrip.

### `UnitPowerMissing(unit [, powerType [, unmodified]])`

The power deficit — `UnitPowerMax(...) - UnitPower(...)` for the given type —
in one call. The power analogue of [`UnitHealthMissing`](#unithealthmissingunit),
for energy/mana/rage "resource needed" checks. Same `unit` / `powerType`
handling and invalid-unit-returns-`0` semantics as `UnitPower`.

```lua
local energyNeeded = UnitPowerMissing("player", Enum.PowerType.Energy)
local rageMissing  = UnitPowerMissing("player")   -- primary power
```

`unmodified` (arg 3, any truthy value) skips the display divisor and returns
the raw internal deficit — the same third argument retail's `UnitPower` takes.

Computed as the difference of the two **display** values (each integer-divided
by the per-type divisor), not by dividing the raw difference, so it equals
`UnitPowerMax(...) - UnitPower(...)` exactly. Those diverge for rage: raw
`1000/55` displays as `100/5`, a deficit of `95`, whereas `(1000-55)/10`
truncates to `94`. Clamped at 0.

### `UnitPowerType(unit)`

Vanilla 1.12 ships this returning just the integer power type;
this implementation extends it to the modern 2-tuple
`(powerType, powerToken)`. Strict-superset signature — addons
that destructure only the first return are unaffected.

```lua
local pt, token = UnitPowerType("player")
-- e.g. (0, "MANA") for a paladin, (1, "RAGE") for a warrior
```

Token strings match modern WoW exactly:

| value | token |
|---|---|
| `0` | `"MANA"` |
| `1` | `"RAGE"` |
| `2` | `"FOCUS"` |
| `3` | `"ENERGY"` |
| `4` | `"HAPPINESS"` |
| `5` | `"RUNES"` |
| `6` | `"RUNIC_POWER"` |

5/6 are post-WotLK power types that can't appear on a 1.12 unit's
descriptor; they're included in the table for symmetry with the
modern enum in case a private server pushes one through.

Chains to the engine's original `Script_UnitPowerType` at
`0x00517940` to preserve its full unit-resolution flow (object-
manager lookup with pet / totem / vehicle fallbacks), then reads
the just-pushed integer and appends the token string.

### `UnitSpellHaste(unit)`

Backport of the TBC+ spell-haste getter — vanilla 1.12 has no haste API.
Returns the spell haste **percentage**: `0` for an unhasted unit, positive
when casting is sped up, negative when slowed (Curse of Tongues).

```lua
local haste = UnitSpellHaste("player")   -- 0 for most vanilla casters
```

Reads `UNIT_MOD_CAST_SPEED` — the cast-time multiplier at descriptor `+0x22c`
that the server folds into `SpellEntry::GetCastTime`
(`castTime *= modCastSpeed`) — and converts it to a percentage:
`haste% = (1 / modCastSpeed - 1) * 100`. So `modCastSpeed` `1.0` → `0`,
`0.5` (half cast time) → `100`, `>1.0` → negative.

This is the Blizzard-shaped surface over the field nampower exposes raw as
`GetUnitField(unit, "modCastSpeed")`, so addons can drop that dependency.
Consumers that need the raw multiplier back can recover it as
`1 / (1 + UnitSpellHaste(unit) / 100)`. Returns `0` for invalid units.

## UnitAuras

Backport of the modern `C_UnitAuras` namespace. Returns
`AuraData`-shaped tables instead of vanilla's `UnitBuff` /
`UnitDebuff` multi-return tuples, so modern addon code that does
`local d = C_UnitAuras.GetAuraDataByIndex(unit, 1); if d.dispelName ==
"Magic" then ...` works unchanged.

Reads primarily off the unit's `m_objectFields` descriptor — same
data source `UnitBuff` / `UnitDebuff` use. The descriptor has 48 aura
slots total: 32 helpful (buffs) at indices 0..31, 16 harmful
(debuffs) at indices 32..47. Functions in this namespace take a
1-based Lua index that translates onto whichever range the filter
selects.

When a party/raid member has **no live unit object at all** (a
different map, far out of range), there is no descriptor to read — but
the server still transmits that member's current aura spell IDs, and
these functions surface them, exactly as the built-in `UnitBuff` /
`UnitDebuff` do. See [Out-of-range group members](#out-of-range-group-members).

### `AuraData` table shape

| Field | Type | Source / value on vanilla |
|---|---|---|
| `name` | string | localized spell name from `Spell.dbc` |
| `icon` | string | icon path from `SpellIcon.dbc` |
| `applications` | number | stack count (engine stores `stacks-1`, we display `+1`) |
| `spellId` | number | spell ID from the descriptor's aura array |
| `dispelName` | string | `"Magic"` / `"Curse"` / `"Disease"` / `"Poison"` (from `SpellDispelType.dbc`), or `""` if non-dispellable |
| `isHelpful` | boolean | true for slot < 32 |
| `isHarmful` | boolean | true for slot >= 32 |
| `duration` | number | applied duration in seconds. When the aura's cast was observed (in the `Aura::Source` cache), this is the caster-modified duration — talent/glyph extensions like Improved Shadow Word: Pain included — so it stays consistent with `expirationTime` (`remaining ≤ duration`). On a cache miss it falls back to the base `Spell.dbc → SpellDuration.dbc` value with level scaling. Returns 0 for spells flagged "no duration" (passives, paladin auras, infinite buffs) |
| `expirationTime` | number | for `unit == "player"`, read from the engine's player-buff table at `0x00BC6040` (same data `GetPlayerBuffTimeLeft` returns). For any other unit, taken from the `Aura::Source` cache (cast time + duration captured from `SMSG_SPELL_GO`; see below). `0` when neither source has it. `expirationTime - GetTime()` gives the true remaining time |
| `sourceUnit` | string | unit token of the caster (`"player"`, `"raid7"`, `"nameplate1"`, …), resolved from the `Aura::Source` cache. `nil` if the cast wasn't observed or the caster maps to no current token |
| `sourceGUID` | string | caster's `"0x…"` GUID string from the same cache. **ClassicAPI extension — not a retail `AuraData` field.** Set whenever a caster is known, including when `sourceUnit` is `nil` (caster left token range). Stable for the session, unlike the volatile nameplate token; doubles as a unit token under SuperWoW. `nil` on a cache miss |
| `charges` / `maxCharges` | number | always `0` — vanilla has stacks, not charges |
| `timeMod` | number | always `1` — vanilla has no haste-affected auras |
| `isStealable`, `isBossAura`, `isFromPlayerOrPlayerPet`, `isNameplateOnly`, `nameplateShowAll`, `nameplateShowPersonal`, `canApplyAura`, `shouldConsolidate`, `isRaid` | boolean | always `false` — modern UI concepts vanilla doesn't have |
| `auraInstanceID`, `points` | (absent) | omitted from the table — Lua read yields nil, matching modern semantics for "field doesn't apply" |

### Filter parsing

The optional `filter` string is a pipe-separated set of upper-case
tokens, matching modern syntax (`"HELPFUL"`, `"HARMFUL"`,
`"HELPFUL|PLAYER"`, etc.). Honored on vanilla:

- **`HELPFUL`** (default) / **`HARMFUL`** — pick the buff or debuff range.
  In `GetUnitAuras`, supplying neither returns both ranges.
- **`PLAYER`** — restrict to auras the local player cast, via the
  `Aura::Source` caster cache (`sourceGUID == ` player GUID). Combines with
  the range tokens (`"HARMFUL|PLAYER"` = your debuffs only). Because the
  caster is best-effort, an aura whose cast we didn't observe is treated as
  not-player-cast and excluded — so `PLAYER` can under-report auras that
  predate login.

Other tokens (`RAID` / `CANCELABLE` / `INCLUDE_NAME_PLATE_ONLY`) are
accepted but no-op — they need engine systems (raid-dispel relevance,
nameplate visibility flags) vanilla doesn't have.

### Caster & timing (`Aura::Source`)

`sourceUnit`, `sourceGUID`, and non-player `expirationTime` come from a
client-side cache that vanilla itself can't provide: the unit aura array
stores only spell IDs — never the caster, and no cast/expiration timing for
anyone but the local player. `Aura::Source` fills the gap by co-hooking
three engine functions and caching `(targetGuid, spellId) → { casterGuid,
expirationMs, durationMs }`; `Push` keys into it by the unit's GUID + the
aura's spell ID.

- **`SpellGo`** (`FUN_SPELL_GO`, `0x006E7A70`) — the cast packet, the only
  place the client sees an aura's **caster** + a server-authoritative,
  caster-modified duration. Fills `sourceUnit`/`sourceGUID` + timing for
  directly-cast auras.
- **`OnAuraAdded`** (`0x006123F0`) — fires when any aura first occupies a
  slot, including proc/triggered auras that emit no `SMSG_SPELL_GO` (Shadow
  Weaving etc.). Stamps `expirationMs = now + base duration` (no caster).
- **`OnAuraStacksChanged`** (`0x00612450`) — fires when an existing aura's
  stack count changes; re-stamps expiration so a climbing stacking debuff
  (e.g. Shadow Weaving 1→5) refreshes.

Application hooks never overwrite an entry `SpellGo` already owns, so a
directly-cast aura keeps its talented timing + caster regardless of hook
order.

Implications:

- **Best-effort.** Only auras observed *after* login carry data; auras
  already up when you logged in leave the defaults (`sourceUnit`/`sourceGUID`
  nil, non-player `expirationTime` 0).
- **Max-stack refresh is a blind spot.** When a stacking debuff is refreshed
  at *max* stacks (e.g. Shadow Weaving 5→5), no client-visible aura field
  changes (spellID, stacks, flags, level all identical), so the server sends
  no UpdateField and the engine fires no callback — there is no packet to
  hook (verified: the stack-change dispatcher `FUN_00604ea0` calls
  `OnAuraStacksChanged` unconditionally, so its silence proves nothing
  arrived). The countdown therefore runs to 0 and the entry evicts; once
  evicted, `expirationTime` falls back to 0 (unknown) while the aura is still
  shown. Climbing stacks and single-stack refreshes (re-cast → fresh
  `SpellGo`) are unaffected.
- **No nampower dependency.** We parse the engine functions ourselves;
  nampower (which hooks the same three) need not be loaded — when it is, we
  co-hook the sites.
- Entries are evicted once their timed aura elapses, so the cache stays
  bounded; infinite-duration auras persist until overwritten under load.

### Out-of-range group members

The descriptor and the `Aura::Source` cache both require the member's
CGUnit to exist on the client. When a party/raid member is on a
**different map** (or otherwise far enough out of range that the engine
drops their object entirely), neither is available — yet the server keeps
every group member's current auras flowing in `SMSG_PARTY_MEMBER_STATS`
(`GROUP_UPDATE_FLAG_AURAS` for buffs, `GROUP_UPDATE_FLAG_AURAS_NEGATIVE`
for debuffs), and the client stashes them in the group-member roster
structs. Every `C_UnitAuras.*` function falls back to that array when the
unit has no object — the same source the built-in `UnitBuff` / `UnitDebuff`
read out of range — so a cross-map `GetUnitAuras("raid7")` returns the
member's real buffs/debuffs instead of an empty table.

This path is **spell-ID only**, because that is all the packet carries:

- `applications` is always `1` — stack counts aren't transmitted.
- `expirationTime` / `sourceUnit` / `sourceGUID` are present only if *you*
  cast the aura (the `Aura::Source` `SMSG_SPELL_GO` entry is merged in when
  it matches); otherwise the usual defaults.
- `duration` is the `Spell.dbc` base with level scaling.
- Spell IDs are truncated to 16 bits on the wire, so a custom aura with a
  spell ID above 65535 comes through wrong — an inherent limitation of the
  vanilla packet that `UnitBuff` shares.

Distinct from the in-range descriptor-drop cases (rogue stealth, nearby
range fluctuation), where the object still exists and the `Aura::Source`
cache is the fallback — see [Caster & timing](#caster--timing-aurasource).

### `C_UnitAuras.GetAuraDataByIndex(unit, index [, filter])`

Returns the `AuraData` table for the `index`-th aura (1-based)
matching `filter`, or `nil` if the unit has fewer than `index`
matching auras. Empty / non-visible descriptor slots are skipped
during iteration, so consecutive indices return consecutive *active*
auras the same way `UnitBuff` does.

```lua
local d = C_UnitAuras.GetAuraDataByIndex("player", 1, "HELPFUL")
if d then
    print(d.name, d.spellId, d.dispelName, d.applications)
end
```

**Filter tokens.** `filter` uses the modern AuraFilters format: tokens
separated by `|` or spaces, each token optionally negated with a
leading `!`. This build honors:

- `HELPFUL` / `HARMFUL` — buffs / debuffs. With neither token the
  query returns both (the indexed getter defaults to helpful).
- `PLAYER` / `!PLAYER` — only auras the local player cast, or only
  auras the player did not cast. Caster data comes from casts this
  session, so an aura present before you saw it cast has no caster and
  counts as not-player.
- `DISPELLABLE` / `!DISPELLABLE` — only auras that can be dispelled,
  purged, or stolen (dispel type Magic, Curse, Disease, or Poison), or
  only auras that cannot. This is "can it be removed at all", not "can
  you remove it".
- `CROWD_CONTROL` / `!CROWD_CONTROL` — only auras that are a crowd-
  control effect (stun, fear, silence, root, charm, confuse, disarm, and
  movement slows), or only auras that are not. Shares the loss-of-control
  classifier with `C_LossOfControl`, plus snares (which are crowd
  control but not loss of control).

All other AuraFilters tokens (`RAID`, `CANCELABLE`,
`INCLUDE_NAME_PLATE_ONLY`, `MAW`, and the rest) are accepted and
ignored — vanilla has no data for them. Token matching is whole-token,
so `RAID_PLAYER_DISPELLABLE` is not read as `PLAYER`.

### `C_UnitAuras.GetBuffDataByIndex(unit, index)` / `GetDebuffDataByIndex(unit, index)`

Convenience wrappers locking the filter to `HELPFUL` or `HARMFUL`
respectively. Equivalent to `GetAuraDataByIndex(unit, index,
"HELPFUL")` / `"HARMFUL"`.

### `C_UnitAuras.GetUnitAuraBySpellID(unit, spellID [, filter])`

Linear-searches `unit`'s aura array for the first populated slot
whose `spellId` matches and returns its `AuraData`, or `nil` if not
found. Without a filter, searches both ranges (helpful first, then
harmful).

```lua
-- Is the player blessed with Wisdom right now?
local d = C_UnitAuras.GetUnitAuraBySpellID("player", 25290)
if d then print("yes, with", d.applications, "stacks") end
```

### `C_UnitAuras.GetPlayerAuraBySpellID(spellID)`

Shorthand for `GetUnitAuraBySpellID("player", spellID)` — the most
common consumer pattern (WeakAuras-style aura tracking).

### `C_UnitAuras.GetAuraDataBySpellName(unit, spellName [, filter])`

Linear-searches `unit`'s aura array for the first populated slot
whose locale-resolved `name` matches `spellName` exactly. Returns
the `AuraData` for that slot or `nil` if not found. Case-sensitive
— matches modern semantics. Without a filter, searches both
ranges (helpful first, then harmful).

```lua
local d = C_UnitAuras.GetAuraDataBySpellName("player", "Inner Fire")
if d then print(d.applications, "stacks of Inner Fire") end

-- restrict to debuffs:
local poison = C_UnitAuras.GetAuraDataBySpellName(
    "target", "Mind-numbing Poison", "HARMFUL")
```

Uses the same `name` field the other `C_UnitAuras.*` functions
populate (locale-applied `Spell.dbc` name). If the player is on
a non-English client, pass the localized name — addons that
hard-code English names should use `GetUnitAuraBySpellID`
instead for portability.

### `C_UnitAuras.RegisterComboDuration(spellID, baseSeconds, maxSeconds)`

ClassicAPI extension — **you normally never call this**. Overrides the
combo-point duration data for one spellID, for servers whose values
differ from (or are missing in) the client's `SpellDuration.dbc`.
Returns `true` on success; re-registering a spellID replaces its entry.

Background: combo-point finishers (Rupture, Kidney Shot, Slice and
Dice, …) get their real duration computed **server-side** at cast time
as `base + (max − base) × comboPoints / 5`, and 1.12 never transmits
the result for debuffs the player puts on other units (verified with a
whole-protocol packet capture). ClassicAPI therefore mirrors the
computation automatically: it snapshots your combo points the instant
the cast request leaves the client and applies the same formula from
the spell's own `SpellDuration.dbc` row, so `duration`/
`expirationTime` in every `AuraData` are combo-accurate out of the box
— Rupture at 4 CP reads 14s, not the 6s base. Your duration talents
apply on top, in the server's order. No registration involved.

The override exists only for spells whose client DBC data is missing
or wrong on a specific server. The known case — Turtle's reworked Rip
(8s + 2s per combo point), whose SpellDuration row exists on the
server only — is built into ClassicAPI and gated on the client-data
bug itself: the fallback applies only while the spell's DurationIndex
points at a SpellDuration row the client doesn't have (a condition
unique to Rip on Turtle's client; on a stock client Rip's index
resolves normally, and if Turtle ever ships the missing row, the DBC
takes over automatically). No realm detection involved. This function
is the escape hatch for *other* custom servers with the same class of
gap — call it from your own config or addon; a registered override
beats both the DBC and the built-in values.

### `C_UnitAuras.RegisterAuraDurationModifierByTrigger(triggerFamily, triggerSchool, affectedFamily, affectedFamilyFlags, affectedIcon, op [, valueSeconds])`

ClassicAPI extension for **server-side DoT-duration changes on another
unit that the client is never told about**. Returns `true` on success.

`expirationTime` for a debuff on another unit is computed from the cast
packet (`SMSG_SPELL_GO`) at cast time. When the server later changes that
debuff's remaining duration — a refresh, or a partial consume — 1.12 sends
**no** packet an observing caster can see (verified in the server source:
the change runs through `SetAuraDuration`/`RefreshHolder`, and the only
duration packet is self-scoped to the aura-bearer; on a mob it isn't even
built). So the cached `expirationTime` would go stale.

The client *does* always see the **triggering** cast, though. Register a
rule and ClassicAPI mirrors the server's edit on the cached aura when that
trigger lands:

| Arg | Meaning |
|-----|---------|
| `triggerFamily` | `SpellFamilyName` of the triggering cast (e.g. `6` priest). |
| `triggerSchool` | School index the trigger must be (`0` physical … `2` fire, `5` shadow, `6` arcane), or `< 0` for any. Matching by family + school covers a whole class of spells at once — every rank, plus server-added ones — rather than a named ability. |
| `affectedFamily` | `SpellFamilyName` of the affected aura (e.g. `5` warlock, `11` shaman, `6` priest). |
| `affectedFamilyFlags` | A `SpellFamilyFlags` bitmask; the affected aura matches if it overlaps. Family + flag is rank-proof (covers every rank at once). |
| `affectedIcon` | `SpellIconID` the affected aura must have, or `0` to match any. |
| `op` | `"refresh"` (reset to full duration), `"reduce"` (subtract `valueSeconds`, removing the aura if it would go non-positive), `"set"` (to `valueSeconds`), `"remove"`. |
| `valueSeconds` | Amount for `reduce`/`set`; ignored otherwise. |

The rule only fires for the aura **cast by the same unit** as the trigger
(these mechanics act on the caster's own DoT), and misses are excluded for
free (a missed trigger isn't in the packet's hit list).

Rules keyed to an **exact trigger spellID** (rather than a family + school
category) are registered inside the DLL instead. `!!!ClassicAPI`'s built-in
Turtle mods — Conflagrate shaving 3s off the caster's Immolate, Molten Blast
refreshing the caster's Flame Shock — live in `src/turtle/DurationMods.cpp`,
and Carnage's roll-gated Rip/Rake refresh in `src/turtle/Carnage.cpp`.

The one rule registered from Lua
([Util/AuraDurationModifiers.lua](../AddOns/!!!ClassicAPI/Util/AuraDurationModifiers.lua))
is Shadow Weaving: for a 5/5 priest (talent `15334`, where the proc is 100%),
any shadow-school priest cast refreshes the target's Shadow Vulnerability
(`15258`). Below 5/5 it's roll-gated and deliberately not inferred — the client
can't see the server's roll, so inferring it would show wrong timers. One
accepted best-effort caveat: DoT *ticks* (SW:P / Devouring Plague) also refresh
it server-side but emit no cast packet, so a pure DoT-only phase under-counts
slightly.

```lua
-- Flags are hex; Lua 5.0 has no 0x literals, so use tonumber(hex, 16).
-- Any priest (6) shadow-school (5) cast refreshes Shadow Vulnerability
-- (affected priest 6, flag 0x4000000, icon 9):
C_UnitAuras.RegisterAuraDurationModifierByTrigger(6, 5, 6, tonumber("4000000", 16), 9, "refresh")
```

### `C_UnitAuras.GetUnitAuras(unit [, filter])`

Returns a numerically-indexed array of `AuraData` tables for every
populated aura on `unit`. Without a filter the array is helpful auras
followed by harmful; with a filter it's restricted to one range.
Empty array (`{}`) if the unit has no auras or doesn't resolve.

```lua
for _, aura in ipairs(C_UnitAuras.GetUnitAuras("target")) do
    print(aura.spellId, aura.dispelName, aura.isHarmful)
end
```

### `C_UnitAuras.GetAuraDispelTypeColor(dispelName)`

Returns a `{r, g, b, a}` table for the given dispel-type name,
matching modern FrameXML's `DebuffTypeColor` values:

| dispelName | r | g | b |
|---|---|---|---|
| `"Magic"` | 0.20 | 0.60 | 1.00 |
| `"Curse"` | 0.60 | 0.00 | 1.00 |
| `"Disease"` | 0.60 | 0.40 | 0.00 |
| `"Poison"` | 0.00 | 0.60 | 0.00 |
| `"Enrage"` | 1.00 | 0.55 | 0.00 |
| (anything else, including `""`) | 0.80 | 0 | 0 |

Returns a `ColorMixin` instance the same way modern does — the C
function `pcall`s Lua's `CreateColor(r, g, b, a)` (defined in
`!!!ClassicAPI/Util/Color.lua`) and returns whatever table it
builds. So the returned value carries the mixin methods
(`GetRGB`, `GenerateHexColorMarkup`, etc.) in addition to the
`.r/.g/.b/.a` fields. Falls back to a plain `{r,g,b,a}` table if
`CreateColor` isn't loaded yet (shouldn't happen — `!!!ClassicAPI`
loads first thanks to the triple-`!` prefix).

## VoiceChat

Backport of modern WoW's text-to-speech surface — the `C_VoiceChat` and
`C_TTSSettings` namespaces — backed by **Windows SAPI** (the OS speech
engine). All playback is **local** (rendered on your own machine);
vanilla 1.12 has no voice-chat transport, so there is nothing "remote" to
transmit synthesized speech into.

> **Coexistence with VanillaTTS:** the same surface also ships as the
> standalone [VanillaTTS](https://github.com/brues-code/VanillaTTS) DLL,
> which adds a software-synth backend for Wine. If `VanillaTTS.dll` is
> loaded in the same client, this built-in module stands down automatically
> (registers none of the functions, events, or cvars below) and VanillaTTS
> provides them instead — the Lua-visible API is identical either way, so
> addons don't need to care which one is active.

SAPI is created lazily on first use. If SAPI is unavailable (e.g. running
the client under Wine, or a stripped Windows), every call degrades
gracefully: `GetTtsVoices` returns an empty table, `SpeakText` fires
`VOICE_CHAT_TTS_PLAYBACK_FAILED` and does nothing, and the client stays
stable — there is no crash. Install more voices under **Windows Settings →
Time & Language → Speech**, then call `C_TTSSettings.RefreshVoices()`.

### `C_VoiceChat.GetTtsVoices()` / `C_VoiceChat.GetRemoteTtsVoices()`

Return a numerically-indexed array of voice tables — one per SAPI voice
installed on the machine:

```lua
for _, v in ipairs(C_VoiceChat.GetTtsVoices()) do
    print(v.voiceID, v.name)   -- e.g.  0  "Microsoft David Desktop - English (United States)"
end
```

Each entry is `{ voiceID = <0-based index>, name = <display name> }`. The
`voiceID` is the index you pass to `SpeakText` and store via
`C_TTSSettings.SetVoiceOption`. `GetRemoteTtsVoices` is an alias returning
the same local list (kept for API parity — vanilla has no voice chat).
Calling either refreshes the cached voice list and fires
`VOICE_CHAT_TTS_VOICES_UPDATE` if it changed.

### `C_VoiceChat.SpeakText(voiceID, text [, destination, rate, volume])`

Speaks `text` aloud using the SAPI voice at index `voiceID`.

```lua
C_VoiceChat.SpeakText(
    C_TTSSettings.GetSpeechVoiceID(),         -- use the configured voice
    UnitName("target") or "no target")
```

- **`voiceID`** (number, required) — index into `GetTtsVoices()`. Note this
  is an **explicit argument** — `SpeakText` does *not* read the `ttsVoice`
  cvar itself, matching Blizzard's contract. Pass
  `C_TTSSettings.GetSpeechVoiceID()` to honor the configured default.
- **`text`** (string, required) — UTF-8; spoken as-is.
- **`destination`** (number, optional, default `1`) — `1` =
  `LOCAL_PLAYBACK`, `4` = `QUEUED_LOCAL_PLAYBACK`. Accepted for parity;
  both queue FIFO through SAPI (no app-side queue).
- **`rate`** (number, optional, default `0`) — speech rate, clamped to
  `[-10, 10]`.
- **`volume`** (number, optional, default `100`) — clamped to `[0, 100]`.

Speech is asynchronous. The call returns immediately and fires
`VOICE_CHAT_TTS_PLAYBACK_STARTED` when SAPI begins the utterance and
`VOICE_CHAT_TTS_PLAYBACK_FINISHED` when it completes (or
`..._PLAYBACK_FAILED` if the voice/engine is unavailable). Raises a Lua
error if `voiceID` isn't a number or `text` isn't a string.

### `C_VoiceChat.StopSpeakingText()`

Stops and purges everything currently playing or queued.

### `C_TTSSettings` — getters & setters

The settings store (separate from `SpeakText`, which takes its parameters
explicitly). All three values are backed by CVars (see below), so they
**persist across sessions** and are clamped to valid ranges on write.

| Function | Returns / effect |
|---|---|
| `C_TTSSettings.GetSpeechRate()` | configured rate, `[-10, 10]` (default `0`) |
| `C_TTSSettings.GetSpeechVolume()` | configured volume, `[0, 100]` (default `100`) |
| `C_TTSSettings.GetSpeechVoiceID()` | configured voice index (default `0`) |
| `C_TTSSettings.GetVoiceOptionName()` | display name of the configured voice, or `""` |
| `C_TTSSettings.SetSpeechRate(rate)` | set rate (clamped) |
| `C_TTSSettings.SetSpeechVolume(volume)` | set volume (clamped) |
| `C_TTSSettings.SetVoiceOption(voiceID)` | set the voice by index (clamped to installed count) |
| `C_TTSSettings.SetVoiceOptionByName(name)` | set the voice by display name (case-insensitive exact match) |
| `C_TTSSettings.SetDefaultSettings()` | reset to voice `1` (if present, else `0`), rate `0`, volume `100` |
| `C_TTSSettings.RefreshVoices()` | re-enumerate SAPI voices; fires `VOICE_CHAT_TTS_VOICES_UPDATE` if the list changed |

```lua
C_TTSSettings.SetVoiceOptionByName("Microsoft Zira Desktop - English (United States)")
C_TTSSettings.SetSpeechVolume(150)              -- clamps to 100
print(C_TTSSettings.GetVoiceOptionName())       -- "Microsoft Zira Desktop - ..."
```

### TTS events

Registered like any engine event (`frame:RegisterEvent("VOICE_CHAT_TTS_PLAYBACK_STARTED")`).

| Event | Args |
|---|---|
| `VOICE_CHAT_TTS_PLAYBACK_STARTED` | `numConsumers` (number), `utteranceID` (number), `durationMS` (number, always `0`), `destination` (number) |
| `VOICE_CHAT_TTS_PLAYBACK_FINISHED` | `numConsumers` (number), `utteranceID` (number), `destination` (number) |
| `VOICE_CHAT_TTS_PLAYBACK_FAILED` | `status` (string — e.g. `"EngineAllocationFailed"`, `"InternalError"`), `utteranceID` (number), `destination` (number) |
| `VOICE_CHAT_TTS_VOICES_UPDATE` | *(none)* — the installed-voice list changed |
| `VOICE_CHAT_TTS_SPEAK_TEXT_UPDATE` | reserved for parity; not currently fired |

`utteranceID` correlates the START/FINISHED/FAILED events for a given
`SpeakText` call. `PLAYBACK_FINISHED` fires only when playback actually
completes, so it's the signal to start the next utterance in a queue.

### TTS CVars

The `C_TTSSettings` values are stored as engine CVars, persisted to
`WTF\Config.wtf`. You can also read/write them with the standard
`GetCVar`/`SetCVar` (or `/console set`); writes are clamped by the same
validation:

| CVar | Default | Range | Backing setting |
|---|---|---|---|
| `ttsVoice` | `0` | `[0, voiceCount-1]` | `GetSpeechVoiceID` / `SetVoiceOption` |
| `ttsSpeed` | `0` | `[-10, 10]` | `GetSpeechRate` / `SetSpeechRate` |
| `ttsVolume` | `100` | `[0, 100]` | `GetSpeechVolume` / `SetSpeechVolume` |

## XMLUtil

### `C_XMLUtil.GetTemplates()`

Returns an array of every registered **virtual XML template** — the
`virtual="true"` frames that `inherits=` targets — one entry per template:

```lua
for _, t in ipairs(C_XMLUtil.GetTemplates()) do
    print(t.name, t.type)   -- e.g.  "UIPanelButtonTemplate"  "Button"
end
```

Each entry is `{ name = <string>, type = <frameType> }`, where `type` is the
XML element tag the template was declared with (`"Frame"`, `"Button"`,
`"CheckButton"`, `"StatusBar"`, …).

The list is read straight from the engine's virtual-template registry — the
same store the XML loader fills for `inherits=` — so it covers every template
currently loaded from **both** Blizzard FrameXML and addon `.xml` files, and is
rebuilt on `/reload`. Font templates live in a separate registry and are not
included (matching retail, where `GetTemplates` returns frame templates).
Returns an empty table if no template has registered yet.

> **Note (vanilla vs. retail):** retail's `XMLTemplateListInfo` carries only
> the `name` and `type` fields, both provided here. There's no filtering —
> every virtual template is returned, in hash-table order (not load or
> alphabetical order).

### `C_XMLUtil.GetTemplateInfo(name)`

Returns an `XMLTemplateInfo` table for the named template, or `nil` if no
template of that name is registered:

```lua
local info = C_XMLUtil.GetTemplateInfo("StatFrameTemplate")
-- { type = "Frame", width = 104, height = 13, keyValues = {}, inherits = nil }
```

| Field | Type | Notes |
|---|---|---|
| `type` | string | The template's frame type — its XML element tag (`"Frame"`, `"Button"`, …). |
| `width` / `height` | number | The size **statically declared on the template itself** via a `<Size>` element, or `0` if it declares none (including when the size comes from an inherited template rather than a direct `<Size>`). |
| `keyValues` | table | Always an **empty table** in 1.12 — the vanilla XML schema has no `<KeyValues>` element, so no template can carry key/value pairs. Present for API parity. |
| `inherits` | string? | The template's `inherits=` attribute (a comma-delimited list), or `nil` if it inherits nothing. |

Lookup is by name through the engine's own template registry — the same
resolution `inherits=` uses — so it's case-insensitive and covers both
Blizzard FrameXML and addon templates.

> **Not provided:** retail's `sourceLocation` field (file:line where the
> template was defined) is a 10.2.0 addition; vanilla records no source
> location for XML nodes, so the field is omitted (reads as `nil`).

### `C_XMLUtil.DoesTemplateExist(name)`

Returns `true` if a virtual template of that name is registered, `false`
otherwise:

```lua
if C_XMLUtil.DoesTemplateExist("MyAddonRowTemplate") then
    button = CreateFrame("Button", nil, parent, "MyAddonRowTemplate")
end
```

Case-insensitive, resolved through the same registry as `inherits=`.
