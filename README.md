# ClassicAPI

ClassicAPI is a DLL for World of Warcraft 1.12.1 (Vanilla / Turtle WoW). It
backports a large part of the modern WoW API — 550+ functions and 50+ events,
plus much of Lua 5.1 — into the 1.12 client, so addons written for later
versions (3.3.5+ or Classic Era) run with little or no change.

It hooks the engine after WoW boots and registers everything the same way WoW
registers its own Lua functions. Nothing else to install — the DLL is
self-contained.

## Highlights

The headline features. Each one takes effect as soon as the DLL loads, so your
existing addons benefit without any code changes. The complete per-function
reference is in **[docs/API.md](docs/API.md)**.

### Lua 5.1 compatibility

The flagship feature: run modern Lua 5.1 addon code on 1.12's Lua 5.0 VM.

| Feature | Effect |
|---------|--------|
| Syntax | Compiles the Lua 5.1 length (`#`), modulo (`%`), `...`-expression, `0x` hex-literal, and leveled long bracket (`[=[ ]=]`) syntax that vanilla's Lua 5.0 rejects, by rewriting addon source before it compiles. Each addon file also receives its `(name, table)` through `...` (`local name, tbl = ...`). See [Lua 5.1 syntax](docs/API.md#lua-51-syntax). |
| String methods | Every string value accepts method calls — `("asd"):upper()`, `("%d gold"):format(n)`, `msg:match("^!(%w+)")` — resolving through the `string` table, the way Lua 5.1 works. Vanilla's Lua 5.0 errors with `attempt to index a string value`. Works on literals and variables, in-world and on the login screen, and inside coroutines. See [String methods](docs/API.md#string-methods-supper-sformat). |
| Script-handler arguments | Frame-script handlers receive their values as positional arguments — `OnMouseWheel(self, delta)`, `OnClick(self, button)`, `OnEvent(self, event, ...)`, etc. — the way 5.1+ clients do, so modern addon ports work unmodified. The `this` / `arg1` globals stay set. A handler that declares no parameters is unaffected. A handler that declared a parameter and expected nil now receives its real value. On by default. `SetModernScriptArgs(false)` restores exact vanilla dispatch. See [SetModernScriptArgs](docs/API.md#setmodernscriptargsenable--getmodernscriptargs). |

### Modern client behaviors

| Feature | Effect |
|---------|--------|
| Inline textures | Draws inline texture markup (`\|T…\|t`) as icons in FontStrings, chat, and tooltips, the way 4.3.4+ clients do. Vanilla 1.12 shows the raw escape as literal text instead. This covers item and spell icons, raid-target markers, and the coin icons in money strings. `GetStringWidth` and `GetStringHeight` count the icons, so measured width and text wrapping stay correct. Done in pure C++ by hooking the engine's text pipeline — no addon. |
| Tooltip line cap | Lifts `GameTooltip`'s hard 30-line limit to 60 for every `GameTooltipTemplate` frame (`GameTooltip`, `ShoppingTooltip1/2`, `ItemRefTooltip`, AtlasLoot, …). Stat-heavy tooltips and comparison blocks (e.g. pfUI's eqcompare) no longer have their extra lines silently dropped. Done in pure C++ by growing the engine's FontString pool at tooltip-creation time. |
| Event-driven nameplates | The modern `C_NamePlate` API, driven by real events instead of vanilla frame-scraping. `NAME_PLATE_UNIT_ADDED` / `NAME_PLATE_UNIT_REMOVED` fire as plates appear and vanish, `nameplate1`..`nameplateN` tokens resolve with every `UnitX` function (and fire `UNIT_HEALTH`, `UNIT_AURA`, … as `arg1 == "nameplateN"`), and `C_NamePlate.GetNamePlateForUnit` / `GetNamePlates` hand back the live frames. See [NamePlate](docs/API.md#nameplate). |
| Focus target | A sticky focus unit, like later clients. `FocusUnit("target")` pins it and `ClearFocus()` drops it, with `PLAYER_FOCUS_CHANGED` on every change. The `focus` / `focustarget` tokens resolve with every `UnitX` function and fire unit events (`UNIT_HEALTH`, `UNIT_AURA`, … as `arg1 == "focus"`), and the predefined `FOCUSTARGET` / `TARGETFOCUS` keybinds are ready to set. See [Focus](docs/API.md#focus). |
| Retail-like `/reload` | `/reload` picks up addon changes made while the game runs, like a modern client. A new folder under `Interface\AddOns\` registers and loads as a normal addon, new files added to an existing addon's TOC load, a newly installed addon's first SavedVariables save survives `/reload`, `##` metadata edits (`## SavedVariables:`, `## Dependencies:`, `## Title:`, …) take effect, and a deleted addon folder drops from the addon list. A stock client needs a full restart for all of these. See [`/reload` picks up new addons and new files](docs/API.md#reload-picks-up-new-addons-and-new-files). |
| Multi-flavor & conditional TOC | Loads modern multi-flavor addons that ship one folder. Selects a version-specific TOC (`<Name>_ClassicAPI.toc` or `<Name>_Turtle.toc`) and the matching keybinding file (`Bindings_ClassicAPI.xml` / `Bindings_Turtle.xml`), accepts a comma-separated `## Interface:` version list (compatible when it includes the client version `11200`), and honors per-line `[AllowLoadGameType]` / `[AllowLoadTextLocale]` conditions and `[Family]` / `[Game]` / `[TextLocale]` path variables inside a TOC. See [Conditional and multi-flavor TOC loading](docs/API.md#conditional-and-multi-flavor-toc-loading). |

## Full API reference

Everything ClassicAPI adds, in full. Each section is collapsed — click a
heading to expand it. For signatures and return values, see the per-function
reference in **[docs/API.md](docs/API.md)**.

<details>
<summary><b>In-game Lua calls</b> — 550+ functions across ~60 namespaces</summary>

| Namespace | Calls |
|-----------|-------|
| [Action](docs/API.md#action) | `GetActionInfo` |
| [AddOns](docs/API.md#addons) | `C_AddOns.DoesAddOnExist`, `C_AddOns.GetAddOnLocalTable`, `C_AddOns.GetAddOnName`, `C_AddOns.GetAddOnNotes`, `C_AddOns.GetAddOnOptionalDependencies`, `C_AddOns.GetAddOnSecurity`, `C_AddOns.GetAddOnTitle`, `C_AddOns.IsAddOnLoadable`, `C_AddOns.IsAddOnLoaded` |
| [AuctionHouse](docs/API.md#auctionhouse) | `C_AuctionHouse.PostItem` |
| [AuraUtil](docs/API.md#aurautil) | `AuraUtil.FindAura`, `AuraUtil.FindAuraByName`, `AuraUtil.ForEachAura`, `AuraUtil.UnpackAuraData` |
| [Bindings](docs/API.md#bindings) | `SetBindingSpell`, `SetBindingItem`, `SetBindingMacro`, `SetBindingClick`, `SetOverrideBinding`, `SetOverrideBindingSpell`, `SetOverrideBindingItem`, `SetOverrideBindingMacro`, `SetOverrideBindingClick`, `ClearOverrideBindings` |
| [Chat](docs/API.md#chat) | `GetCurrentChatGUID` |
| [ChatBubbles](docs/API.md#chatbubbles) | `C_ChatBubbles.GetAllChatBubbles` |
| [Class](docs/API.md#class) | `FillLocalizedClassList` |
| [ColorUtil](docs/API.md#colorutil) | `C_ColorUtil.ConvertRGBToHSV`, `C_ColorUtil.ConvertHSVToRGB`, `C_ColorUtil.ConvertHSVToHSL`, `C_ColorUtil.ConvertHSLToHSV`, `C_ColorUtil.ConvertHSLToRGB`, `C_ColorUtil.GenerateTextColorCode`, `C_ColorUtil.WrapTextInColor`, `C_ColorUtil.WrapTextInColorCode` |
| [Combat](docs/API.md#combat) | `InCombatLockdown`, `StartAttack`, `StopAttack` |
| [CVar](docs/API.md#cvar) | `C_CVar.GetCVarBool` |
| [Cursor](docs/API.md#cursor) | `GetCursorInfo` |
| [Container](docs/API.md#container) | `C_Container.CalculateTotalNumberOfFreeBagSlots`, `C_Container.GetContainerItemCharges`, `C_Container.GetContainerItemDurability`, `C_Container.GetContainerItemID`, `C_Container.GetContainerItemInfo`, `C_Container.GetContainerItemRepairCost`, `C_Container.GetContainerNumFreeSlots`, `C_Container.GetItemCooldown`, `C_Container.HasContainerItem`, `C_Container.IsContainerItemOpenable`, `C_Container.MoveItem`, `C_Container.PlayerHasHearthstone`, `C_Container.SwapItems`, `C_Container.UseHearthstone`, `GetItemCooldown` |
| [Creature](docs/API.md#creature) | `C_CreatureInfo.GetCreatureID`, `C_CreatureInfo.GetCreatureInfoByID`, `C_CreatureInfo.RequestLoadCreatureByID`, `C_CreatureInfo.GetRaceInfo`, `C_CreatureInfo.GetClassInfo`, `C_CreatureInfo.GetCreatureFamilyInfo`, `C_CreatureInfo.GetCreatureFamilyIDs`, `C_CreatureInfo.GetFactionInfo`, `C_CreatureInfo.GetCreatureTypeInfo`, `C_CreatureInfo.GetCreatureTypeIDs` |
| [Currency](docs/API.md#currency) | `GetCoinTextureString`, `C_CurrencyInfo.GetCoinTextureString` |
| EncodingUtil | `C_EncodingUtil.CompressString`, `C_EncodingUtil.DecompressString`, `C_EncodingUtil.EncodeBase64`, `C_EncodingUtil.DecodeBase64`, `C_EncodingUtil.EncodeHex`, `C_EncodingUtil.DecodeHex`, `C_EncodingUtil.SerializeJSON`, `C_EncodingUtil.DeserializeJSON`, `C_EncodingUtil.SerializeCBOR`, `C_EncodingUtil.DeserializeCBOR` |
| [EquipmentSet](docs/API.md#equipmentset) | `C_EquipmentSet.CanUseEquipmentSets`, `C_EquipmentSet.ClearIgnoredSlotsForSave`, `C_EquipmentSet.CreateEquipmentSet`, `C_EquipmentSet.DeleteEquipmentSet`, `C_EquipmentSet.EquipmentSetContainsLockedItems`, `C_EquipmentSet.GetEquipmentSetID`, `C_EquipmentSet.GetEquipmentSetIDs`, `C_EquipmentSet.GetEquipmentSetInfo`, `C_EquipmentSet.GetIgnoredSlots`, `C_EquipmentSet.GetItemIDs`, `C_EquipmentSet.GetItemLocations`, `C_EquipmentSet.GetNumEquipmentSets`, `C_EquipmentSet.IgnoreSlotForSave`, `C_EquipmentSet.IsSlotIgnoredForSave`, `C_EquipmentSet.ModifyEquipmentSet`, `C_EquipmentSet.SaveEquipmentSet`, `C_EquipmentSet.UnignoreSlotForSave`, `C_EquipmentSet.UseEquipmentSet` |
| [Events](docs/API.md#events) | `C_EventUtils.IsEventValid`, `GetFramesRegisteredForEvent` |
| [Expansion](docs/API.md#expansion) | `ClassicExpansionAtLeast`, `ClassicExpansionAtMost`, `GetClassicExpansionLevel` |
| [Faction](docs/API.md#faction) | `C_Reputation.GetFactionDataByIndex`, `C_Reputation.GetFactionStandings`, `C_Reputation.GetLastStandingChange`, `C_Reputation.GetWatchedFactionData`, `C_Reputation.SetWatchedFactionByID`, `GetFactionIDByIndex`, `GetFactionInfoByID`, `GetFactionParentID` |
| [Focus](docs/API.md#focus) | `ClearFocus`, `FocusUnit` |
| [Frame](docs/API.md#frame) | `region:SetPoint("point")` (one-arg form), `region:SetSize`, `region:GetSize`, `region:IsMouseOver`, `region:GetRect`, `region:IsDragging`, `GetMouseFoci`, `frame:SetShown`, `fontstring:GetStringHeight`, `fontstring:GetUnboundedStringWidth`, `fontstring:GetWrappedWidth`, `fontstring:GetNumLines`, `fontstring:GetLineHeight`, `fontstring:IsTruncated`, `fontstring:SetMaxLines`, `fontstring:GetMaxLines`, `fontstring:SetFormattedText`, `texture:SetRotation`, `texture:GetRotation`, `texture:SetVertexOffset`, `texture:GetVertexOffset`, `texture:SetColorTexture`, `texture:SetMask`, `frame:CreateMaskTexture`, `texture:AddMaskTexture`, `texture:RemoveMaskTexture`, `texture:GetNumMaskTextures`, `texture:GetMaskTexture`, `fontstring:SetRotation`, `fontstring:GetRotation`, `frame:SetResizeBounds`, `frame:HookScript`, `frame:IsEventRegistered`, `frame:GetEffectiveAlpha`, `frame:SetAttribute`, `frame:SetAttributeNoHandler`, `frame:ClearAttribute`, `frame:GetAttribute`, `OnAttributeChanged` (script), `SetModernScriptArgs`, `GetModernScriptArgs`, `SecureCmdOptionParse`, `RegisterStateDriver`, `UnregisterStateDriver`, `RegisterAttributeDriver`, `UnregisterAttributeDriver`, `RegisterUnitWatch`, `UnregisterUnitWatch`, `UnitWatchRegistered`, `SecureButton_GetAttribute`, `SecureButton_GetUnit`, `PreClick` (script), `PostClick` (script), `GetClickFrame` |
| [FriendList](docs/API.md#friendlist) | `C_FriendList.GetFriendInfo`, `C_FriendList.GetFriendInfoByIndex`, `C_FriendList.GetNumFriends`, `C_FriendList.GetNumOnlineFriends`, `C_FriendList.GetNumWhoResults`, `C_FriendList.GetWhoInfo`, `C_FriendList.IsFriend`, `C_FriendList.IsIgnored`, `C_FriendList.IsIgnoredByGuid`, `C_FriendList.IsWhoQueryPending`, `C_FriendList.SendWhoQueryByName`, `C_FriendList.SetFriendNotes`, `C_FriendList.SetFriendNotesByIndex` |
| [GameObject](docs/API.md#gameobject) | `C_GameObjectInfo.GetGameObjectInfoByID`, `C_GameObjectInfo.RequestLoadGameObjectByID`, `ClosestGameObjectPosition` |
| [Glue](docs/API.md#glue) | `C_Glue.IsOnGlueScreen` |
| [Gossip](docs/API.md#gossip) | `C_GossipInfo.CloseGossip`, `C_GossipInfo.GetActiveQuests`, `C_GossipInfo.GetAvailableQuests`, `C_GossipInfo.GetNumActiveQuests`, `C_GossipInfo.GetNumAvailableQuests`, `C_GossipInfo.GetNumOptions`, `C_GossipInfo.GetOptions`, `C_GossipInfo.GetText`, `C_GossipInfo.SelectActiveQuest`, `C_GossipInfo.SelectAvailableQuest`, `C_GossipInfo.SelectOption`, `C_GossipInfo.SelectOptionByIndex` |
| [GameTooltip](docs/API.md#gametooltip) | `GameTooltip:AddSpellByID`, `GameTooltip:GetGameObject`, `GameTooltip:GetItem`, `GameTooltip:GetOwner`, `GameTooltip:GetSpell`, `GameTooltip:GetUnitGUID`, `GameTooltip:HasGameObject`, `GameTooltip:HasItem`, `GameTooltip:HasSpell`, `GameTooltip:HasUnit`, `GameTooltip:IsEquippedItem`, `GameTooltip:SetEquipmentSet`, `GameTooltip:SetHyperlinkCompareItem`, `GameTooltip:SetInventoryItemByID`, `GameTooltip:SetItemByGUID`, `GameTooltip:SetItemByID`, `GameTooltip:SetSpellByID`, `GameTooltip:SetTalentByID`, `GameTooltip:SetTotem`, `GameTooltip:SetUnitAura`, `OnTooltipSetItem`, `OnTooltipSetSpell`, `OnTooltipSetUnit`, `OnTooltipSetGameObject` (scripts) |
| [Hooks](docs/API.md#hooks) | `hooksecurefunc` |
| [Input](docs/API.md#input) | `GetMouseButtonClicked`, `IsLeftAltKeyDown`, `IsLeftControlKeyDown`, `IsLeftShiftKeyDown`, `IsModifierKeyDown`, `IsMouseButtonDown`, `IsRightAltKeyDown`, `IsRightControlKeyDown`, `IsRightShiftKeyDown` |
| [Instance](docs/API.md#instance) | `GetInstanceInfo` |
| [Item](docs/API.md#item) | `C_Item.DoesItemExist`, `C_Item.DoesItemExistByID`, `C_Item.EquipItemByName`, `C_Item.GetCurrentItemLevel`, `C_Item.GetDetailedItemLevelInfo`, `C_Item.GetEnchantInfo`, `C_Item.GetItemCount`, `C_Item.GetItemFamily`, `C_Item.GetItemGUID`, `C_Item.GetItemIcon`, `C_Item.GetItemIconByID`, `C_Item.GetItemID`, `C_Item.GetItemInfo`, `C_Item.GetItemInfoInstant`, `C_Item.GetItemInventorySlotInfo`, `C_Item.GetItemInventorySlotKey`, `C_Item.GetItemInventoryType`, `C_Item.GetItemInventoryTypeByID`, `C_Item.GetItemLink`, `C_Item.GetItemLocation`, `C_Item.GetItemMaxStackSize`, `C_Item.GetItemMaxStackSizeByID`, `C_Item.GetItemName`, `C_Item.GetItemNameByID`, `C_Item.GetItemQuality`, `C_Item.GetItemQualityByID`, `C_Item.GetItemSellPrice`, `C_Item.GetItemSellPriceByID`, `C_Item.GetItemSetID`, `C_Item.GetItemSetIDByID`, `C_Item.GetItemSetInfo`, `C_Item.GetItemSpell`, `C_Item.GetItemStatDelta`, `C_Item.GetItemStats`, `C_Item.GetItemClassInfo`, `C_Item.GetItemSubClassInfo`, `C_Item.GetItemUniqueness`, `C_Item.GetItemUniquenessByID`, `C_Item.GetStackCount`, `C_Item.GetWeaponEnchantInfo`, `C_Item.IsBound`, `C_Item.IsConsumableItem`, `C_Item.IsEquippableItem`, `C_Item.IsEquippedItem`, `C_Item.IsItemDataCached`, `C_Item.IsItemDataCachedByID`, `C_Item.IsItemGUIDInInventory`, `C_Item.IsItemInRange`, `C_Item.IsItemOpenable`, `C_Item.IsLocked`, `C_Item.LockItem`, `C_Item.LockItemByGUID`, `C_Item.PickupItem`, `C_Item.UnlockAllItems`, `C_Item.UnlockItem`, `C_Item.RequestLoadItemData`, `C_Item.RequestLoadItemDataByID`, `C_Item.UseAtCursor`, `C_Item.UseAtUnit`, `C_Item.UseItemByName`, `GetAuctionItemID`, `GetAuctionSellItemID`, `GetAverageItemLevel`, `GetCraftReagentItemID`, `GetInboxItemID`, `GetInventoryItemDurability`, `GetInventoryItemID`, `GetInventoryItemsForSlot`, `GetInventoryItemRepairCost`, `GetItemClassInfo`, `GetItemIcon`, `GetItemSubClassInfo`, `GetLootRollItemID`, `GetLootSlotItemID`, `GetMerchantItemID`, `GetQuestItemID`, `GetQuestLogItemID`, `GetTradePlayerItemID`, `GetTradeSkillItemID`, `GetTradeSkillReagentItemID`, `GetTradeTargetItemID`, `OffhandHasWeapon` |
| [Loot](docs/API.md#loot) | `C_Loot.GetNearbyLootableUnits`, `C_Loot.GetLastScanResults`, `C_Loot.IsScanInProgress`, `C_Loot.LootAllCorpses`, `C_Loot.LootUnit`, `C_Loot.LootUnitItem`, `C_Loot.ScanNearbyLoot` |
| [LootHistory](docs/API.md#loothistory) | `C_LootHistory.GetNumItems`, `C_LootHistory.GetItem`, `C_LootHistory.GetPlayerInfo`, `C_LootHistory.Clear` |
| [LossOfControl](docs/API.md#lossofcontrol) | `C_LossOfControl.GetActiveLossOfControlData`, `C_LossOfControl.GetActiveLossOfControlDataCount` |
| [Lua](docs/API.md#lua) | `collectgarbage` (5.1 options), `coroutine.create`, `coroutine.resume`, `coroutine.running`, `coroutine.status`, `coroutine.wrap`, `coroutine.yield`, `CreateFromMixins`, `math.fmod`, `math.huge`, `math.modf`, `Mixin`, `select`, `string.gmatch`, `string.gsub` (table replacement), `string.match`, `string.reverse`, `strjoin`, `strreplace`, `strrev`, `strsplit`, `strtrim`, `table.count`, `table.maxn`, `table.wipe`, `unpack` (range args) |
| [Macros](docs/API.md#macros) | `C_Macro.CreateMacro`, `C_Macro.EditMacro`, `GetLooseMacroIcons`, `GetLooseMacroItemIcons`, `GetMacroIcons`, `GetMacroItemIcons`, `GetMacroSpell` |
| [Mail](docs/API.md#mail) | `GetInboxItemLink`, `GetSendMailItemLink` |
| [Map](docs/API.md#map) | `C_Map.GetAreaInfo`, `C_Map.GetAreas`, `C_Map.GetAreaTriggerInfo`, `C_Map.GetAreaTriggers`, `C_Map.GetBestMapForUnit`, `C_Map.GetMapAreaIDs`, `C_Map.GetMapOverlays`, `C_Map.GetMapWorldSize` |
| [MapExplorationInfo](docs/API.md#mapexplorationinfo) | `C_MapExplorationInfo.GetExploredMapTextures`, `C_MapExplorationInfo.GetUnexploredMapTextures` |
| [MerchantFrame](docs/API.md#merchantframe) | `C_MerchantFrame.GetBuybackItemID`, `C_MerchantFrame.GetItemInfo`, `C_MerchantFrame.GetNumJunkItems`, `C_MerchantFrame.IsMerchantItemRefundable`, `C_MerchantFrame.IsSellAllJunkEnabled`, `C_MerchantFrame.SellAllJunkItems` |
| [NamePlate](docs/API.md#nameplate) | `C_NamePlate.GetNamePlateForGUID`, `C_NamePlate.GetNamePlateForUnit`, `C_NamePlate.GetNamePlateGUIDs`, `C_NamePlate.GetNamePlates` |
| [NameCache](docs/API.md#namecache) | `C_PlayerCache.GetPlayerInfoByName`, `C_PlayerCache.IsEnabled`, `C_PlayerCache.IsScanEnabled`, `C_PlayerCache.RememberPlayer`, `C_PlayerCache.SetEnabled`, `C_PlayerCache.SetScanEnabled`, `GetPlayerInfoByGUID`, `UnitNameFromGUID` |
| [NewItems](docs/API.md#newitems) | `C_NewItems.ClearAll`, `C_NewItems.IsNewItem`, `C_NewItems.RemoveNewItem` |
| [PlayerInfo](docs/API.md#playerinfo) | `C_PlayerInfo.CanUseItem`, `C_PlayerInfo.GetClass`, `C_PlayerInfo.GetName`, `C_PlayerInfo.GetRace`, `C_PlayerInfo.GetSex`, `C_PlayerInfo.GUIDIsCreature`, `C_PlayerInfo.GUIDIsGameObject`, `C_PlayerInfo.GUIDIsPet`, `C_PlayerInfo.GUIDIsPlayer`, `C_PlayerInfo.IsConnected` |
| [Quest](docs/API.md#quest) | `C_QuestLog.GetNumQuestObjectives`, `C_QuestLog.GetQuestDetails`, `C_QuestLog.GetHeaderIndexForQuest`, `C_QuestLog.GetLogIndexForQuestID`, `C_QuestLog.GetQuestIDForLogIndex`, `C_QuestLog.GetTitleForQuestID`, `C_QuestLog.IsOnQuest`, `C_QuestLog.IsQuestDataCachedByID`, `C_QuestLog.IsUnitOnQuest`, `C_QuestLog.RequestLoadQuestByID`, `GetQuestLogLeaderBoardID` |
| [Spell](docs/API.md#spell) | `C_Spell.CancelSpellByID`, `C_Spell.CastAtCursor`, `C_Spell.CastAtUnit`, `C_Spell.CastingInfo`, `C_Spell.ChannelInfo`, `C_Spell.DoesSpellExist`, `C_Spell.GetSchoolString`, `C_Spell.GetSpellCooldown`, `C_Spell.GetSpellDescription`, `C_Spell.GetSpellDispelType`, `C_Spell.GetSpellEffectMechanics`, `C_Spell.GetSpellInfo`, `C_Spell.GetSpellLevelInfo`, `C_Spell.GetSpellLink`, `C_Spell.GetSpellMechanicByID`, `C_Spell.GetSpellName`, `C_Spell.GetSpellPowerCost`, `C_Spell.GetSpellRadius`, `C_Spell.GetSpellReagents`, `C_Spell.GetSpellRequiredTargetLevel`, `C_Spell.GetSpellSubtext`, `C_Spell.GetSpellTexture`, `C_Spell.IsAutoAttackSpell`, `C_Spell.IsCurrentSpell`, `C_Spell.IsNextMeleeSpell`, `C_Spell.IsRangedAutoAttackSpell`, `C_Spell.IsSelfBuff`, `C_Spell.IsSpellHarmful`, `C_Spell.IsSpellHelpful`, `C_Spell.IsSpellInRange`, `C_Spell.IsSpellPassive`, `C_Spell.IsSpellUsable`, `C_Spell.ResetsMeleeSwing`, `C_Spell.SpellHasRange`, `C_Spell.UnitCastingInfo`, `C_Spell.UnitChannelInfo`, `CanDualWield`, `CancelSpellByName`, `CastSpellNoToggle`, `GetCraftSpellID`, `GetSpellBonusDamage`, `GetSpellBonusHealing`, `GetSpellInfo`, `GetSpellLink`, `GetSpellRadius`, `GetSpellRequiredTargetLevel`, `GetSpellSchool`, `IsHarmfulSpell`, `IsHelpfulSpell`, `IsPassiveSpell`, `IsPlayerSpell`, `IsSpellKnown`, `IsUsableSpell`, `SpellHasRange` |
| [SpellBook](docs/API.md#spellbook) | `C_SpellBook.GetCurrentLevelSpells`, `C_SpellBook.GetSkillLineName`, `C_SpellBook.GetSkillLineRank`, `C_SpellBook.GetSpellBookItemInfo`, `C_SpellBook.GetSpellLevelLearned`, `C_SpellBook.GetSpellSkillLine`, `C_SpellBook.IsAutoAttackSpellBookItem`, `C_SpellBook.IsRangedAutoAttackSpellBookItem`, `FindSpellBookSlotByID` |
| [State](docs/API.md#state) | `CancelShapeshiftForm`, `Dismount`, `GetMirrorTimerInfo`, `GetMirrorTimerProgress`, `GetShapeshiftFormID`, `GetSheathState`, `IsAssistingRitual`, `IsFalling`, `IsIndoors`, `IsInGroup`, `IsInRaid`, `IsLoggedIn`, `IsMounted`, `IsOutdoors`, `IsStealthed`, `IsSwimming` |
| [System](docs/API.md#system) | `CopyToClipboard`, `GetPhysicalScreenSize` |
| [Talent](docs/API.md#talent) | `GetTalentIDByIndex`, `GetTalentSpellID` |
| [Targeting](docs/API.md#targeting) | `GetPlayerFacing`, `TargetDirectionEnemy`, `TargetDirectionFriend`, `TargetNearest`, `TargetNearestEnemyPlayer`, `TargetNearestFriendPlayer` |
| [TaxiMap](docs/API.md#taximap) | `C_TaxiMap.GetTaxiNodesForMap`, `C_TaxiMap.GetAllTaxiNodes`, `C_TaxiMap.GetTaxiPaths`, `C_TaxiMap.GetTaxiPathWaypoints`, `C_TaxiMap.GetTaxiRoute` |
| [Time](docs/API.md#time) | `C_DateAndTime.AdjustTimeByDays`, `C_DateAndTime.AdjustTimeByMinutes`, `C_DateAndTime.CompareCalendarTime`, `C_DateAndTime.GetCalendarTimeFromEpoch`, `C_DateAndTime.GetCurrentCalendarTime`, `C_DateAndTime.GetSecondsUntilDailyReset`, `C_DateAndTime.GetServerTimeLocal`, `C_Timer.After`, `C_Timer.NewTicker`, `C_Timer.NewTimer`, `GetServerTime`, `GetTimeCached` |
| [Totem](docs/API.md#totem) | `GetTotemInfo`, `GetTotemTimeLeft`, `GetTotemDuration`, `TargetTotem` |
| [Tracking](docs/API.md#tracking) | `GetNumTrackingTypes`, `GetTrackingInfo`, `SetTracking` |
| [TradeSkillUI](docs/API.md#tradeskillui) | `C_TradeSkillUI.GetTradeSkillListLink`, `C_TradeSkillUI.GetCraftListLink`, `C_TradeSkillUI.GetTradeSkillListRecipes` |
| [UIColor](docs/API.md#uicolor) | `C_UIColor.GetColors` |
| [Unit](docs/API.md#unit) | `ClosestUnitPosition`, `GetUnitSpeed`, `UnitClassBase`, `UnitCreatureFamilyID`, `UnitCreatureID`, `UnitCreatureTypeID`, `UnitDistanceSquared`, `UnitGUID`, `UnitHealthMissing`, `UnitInLineOfSight`, `UnitInRange`, `UnitIsAFK`, `UnitIsDND`, `UnitIsFeignDeath`, `UnitIsInMyGuild`, `UnitIsMinion`, `UnitIsOtherPlayersPet`, `UnitIsPet`, `UnitIsPossessed`, `UnitOwnerGUID`, `UnitPosition`, `UnitPower`, `UnitPowerMax`, `UnitPowerMissing`, `UnitPowerType`, `UnitRaceBase`, `UnitSpellHaste`, `UnitSpellTargetName`, `UnitStandState`, `UnitSubName`, `UnitTokenFromGUID` |
| [UnitAuras](docs/API.md#unitauras) | `C_UnitAuras.GetAuraDataByIndex`, `C_UnitAuras.GetAuraDataBySpellName`, `C_UnitAuras.GetAuraDispelTypeColor`, `C_UnitAuras.GetBuffDataByIndex`, `C_UnitAuras.GetDebuffDataByIndex`, `C_UnitAuras.GetPlayerAuraBySpellID`, `C_UnitAuras.GetUnitAuraBySpellID`, `C_UnitAuras.GetUnitAuras`, `C_UnitAuras.RegisterAuraDurationModifierByTrigger`, `C_UnitAuras.RegisterComboDuration`, `C_UnitAuras.UnitAura`, `C_UnitAuras.UnitBuff`, `C_UnitAuras.UnitDebuff` |
| [VoiceChat](docs/API.md#voicechat) | `C_VoiceChat.GetTtsVoices`, `C_VoiceChat.GetRemoteTtsVoices`, `C_VoiceChat.SpeakText`, `C_VoiceChat.StopSpeakingText`, `C_TTSSettings.GetSpeechRate`, `C_TTSSettings.GetSpeechVolume`, `C_TTSSettings.GetSpeechVoiceID`, `C_TTSSettings.GetVoiceOptionName`, `C_TTSSettings.SetSpeechRate`, `C_TTSSettings.SetSpeechVolume`, `C_TTSSettings.SetVoiceOption`, `C_TTSSettings.SetVoiceOptionByName`, `C_TTSSettings.SetDefaultSettings`, `C_TTSSettings.RefreshVoices` |
| [XMLUtil](docs/API.md#xmlutil) | `C_XMLUtil.DoesTemplateExist`, `C_XMLUtil.GetTemplateInfo`, `C_XMLUtil.GetTemplates` |

</details>

<details>
<summary><b>GlueXML calls</b> — login / realm-select / character-select screens</summary>

Registered on the **glue** Lua state (the engine that runs the login,
realm-select, and character-select screens). The persistence entries
in the first row are *glue-only* — they exist to support GlueXML
patches that need a small persistence surface across sessions, since
vanilla 1.12 glue ships no general-purpose persistence API beyond
`GetSavedAccountName`/`SetSavedAccountName` (saturated by autologin).
The rest of the table is in-game calls that we also mirror onto
the glue state because GlueXML had no way to reach them otherwise.

| Group | Calls |
|-------|-------|
| [Account](docs/API.md#account) | `SaveAccount`, `DeleteAccount`, `GetSavedAccounts`, `LoginWithSavedAccount` (passwords encrypted in Windows Credential Manager, scoped per realmlist; plaintext never returned to Lua) |
| [CharacterList](docs/API.md#characterlist) | `GetSavedCharacterOrder`, `SetSavedCharacterOrder` (persist to `WTF\Account\...\ClassicAPI.txt`) |
| CVar | `GetCVar`, `SetCVar`, `RegisterCVar`, `GetCVarDefault`, `C_CVar.GetCVarBool` (storage is process-global — writes from glue are visible in-world and vice versa) |
| [Glue](docs/API.md#glue) | `C_Glue.IsFirstLoadThisSession`, `C_Glue.IsOnGlueScreen` |
| Script | `RunScript` (compile and run a Lua chunk in the glue state's globals — useful for slash-command-style helpers in GlueXML) |
| State | `IsLoggedIn` |

</details>

<details>
<summary><b>Macros</b> — engine-level parsing extensions</summary>

Engine-level extensions to macro parsing and dispatch — no new Lua
functions, just behavior the stock 1.12 engine didn't have. See the
[Macros section in the Lua reference](docs/API.md#macros) for details.

| Form | What it does |
|------|--------------|
| `/cast <spellID>` | `/cast 5019` casts Shoot if known; macro slot tags correctly for action-bar UI |
| `CastSpellByName("<spellID>")` | Same — numeric strings resolve through the engine's name resolver |
| `CastSpellNoToggle("<name>")` in a macro | Engine's macro parser now recognizes it as a primary-spell line, so the macro slot in an addon like pfUI highlights when its spell is auto-repeating or its self-aura is active |

</details>

<details>
<summary><b>Console commands</b> — developer console (<code>-console</code>)</summary>

Registered on the engine's developer console (the `~` console available
when launching with `-console`), not as Lua functions. See the
[Console section in the Lua reference](docs/API.md#console) for details.

| Command | What it does |
|---------|--------------|
| `ExportInterfaceFiles code` | Extracts Blizzard's UI source (`.lua`/`.xml`/`.toc`/`.xsd`) from the MPQs to `BlizzardInterfaceCode\` |
| `ExportInterfaceFiles art` | Extracts Blizzard's UI art (`.blp`/`.tga`) from the MPQs to `BlizzardInterfaceArt\` |
| `ExportDBCFiles` | Extracts the client's `.dbc` tables from the MPQs to `DBFilesClient\` |

</details>

<details>
<summary><b>Events</b> — new events fired to addons</summary>

| Event | Payload |
|-------|---------|
| `BAG_NEW_ITEMS_UPDATED` | *(none)* |
| `BAG_UPDATE_DELAYED` | *(none)* |
| `EQUIPMENT_SETS_CHANGED` | *(none)* |
| `EQUIPMENT_SWAP_PENDING` | `setID` |
| `EQUIPMENT_SWAP_FINISHED` | `success, setID` |
| `FACTION_STANDING_CHANGED` | `factionID, newStanding, repGained` |
| `GLOBAL_MOUSE_DOWN` | `button` |
| `GLOBAL_MOUSE_UP` | `button` |
| `HEARTHSTONE_BOUND` | *(none)* |
| `ITEM_DATA_LOAD_RESULT` | `itemID, success` |
| `LOOT_HISTORY_ROLL_CHANGED` | `itemIndex, playerIndex` |
| `LOOT_HISTORY_ROLL_COMPLETE` | `itemIndex` |
| `LOOT_SCAN_COMPLETED` | *(none)* |
| `LOSS_OF_CONTROL_ADDED` | `eventIndex` |
| `LOSS_OF_CONTROL_UPDATE` | `unitToken` (always `"player"`) |
| `MODIFIER_STATE_CHANGED` | `keyName, down` |
| `NAME_PLATE_CREATED` | `nameplateFrame` |
| `NAME_PLATE_UNIT_ADDED` | `unitToken` ("nameplateN") |
| `NAME_PLATE_UNIT_REMOVED` | `unitToken` ("nameplateN") |
| `PLAYER_EQUIPMENT_CHANGED` | `equipmentSlot, hasCurrent` |
| `PLAYER_FOCUS_CHANGED` | *(none)* |
| `PLAYER_STARTED_LOOKING` | *(none)* |
| `PLAYER_STOPPED_LOOKING` | *(none)* |
| `PLAYER_STARTED_MOVING` | *(none)* |
| `PLAYER_STOPPED_MOVING` | *(none)* |
| `PLAYER_STARTED_TURNING` | *(none)* |
| `PLAYER_STOPPED_TURNING` | *(none)* |
| `PLAYER_TOTEM_UPDATE` | `totemSlot` |
| `QUEST_ACCEPTED` | `questLogIndex, questID` |
| `QUEST_DATA_LOAD_RESULT` | `questID, success` |
| `QUEST_REMOVED` | `questID` |
| `QUEST_TURNED_IN` | `questID, xpReward, moneyReward` |
| `UNIT_SPELLCAST_SENT` | `"player", target, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_START` | `unit, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_STOP` | `unit, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_DELAYED` | `"player", castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_SUCCEEDED` | `unit, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_INTERRUPTED` | `unit, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_FAILED` | `"player", castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_FAILED_QUIET` | `"player", castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_CHANNEL_START` | `unit, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_CHANNEL_UPDATE` | `"player", castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_CHANNEL_STOP` | `unit, castGUID, spellID, spellName, rank` |
| `UNIT_SPELLCAST_RETICLE_TARGET` | `"player", "", spellID, spellName, rank` |
| `UNIT_SPELLCAST_RETICLE_CLEAR` | `"player", "", spellID, spellName, rank` |
| `UPDATE_INVENTORY_DURABILITY` | *(none)* |
| `UPDATE_SHAPESHIFT_FORM` | *(none)* |
| `VOICE_CHAT_TTS_PLAYBACK_STARTED` | `numConsumers, utteranceID, durationMS, destination` |
| `VOICE_CHAT_TTS_PLAYBACK_FINISHED` | `numConsumers, utteranceID, destination` |
| `VOICE_CHAT_TTS_PLAYBACK_FAILED` | `status, utteranceID, destination` |
| `VOICE_CHAT_TTS_VOICES_UPDATE` | *(none)* |

</details>

<details>
<summary><b>Globals & enums</b></summary>

| Group | Constants |
|-------|-----------|
| Version | `CLASSIC_API_VERSION` |
| Expansion | `LE_EXPANSION_LEVEL_CURRENT`, `LE_EXPANSION_CLASSIC` … `LE_EXPANSION_MIDNIGHT` |
| Item quality | `LE_ITEM_QUALITY_POOR` … `LE_ITEM_QUALITY_WOWTOKEN` |
| Unit stat | `LE_UNIT_STAT_STRENGTH` … `LE_UNIT_STAT_SPIRIT` |
| Addon security | `Enum.AddOnSecurityStatus.{Secure,Insecure,Banned,NotAvailable}` |
| Power type | `Enum.PowerType.{HealthCost,None,Mana,Rage,Focus,Energy,Happiness}` |
| Inventory type | `Enum.InventoryType.Index*Type` (0–34, e.g. `IndexHeadType`=1 … `IndexRelicType`=28) |
| Item class | `Enum.ItemClass.{Consumable,Container,Weapon,Gem,Armor,Reagent,Projectile,Tradegoods,ItemEnhancement,Recipe,Quiver,Questitem,Key,Miscellaneous,…}` (0–19) |
| Item quality | `Enum.ItemQuality.{Poor,Common,Uncommon,Rare,Epic,Legendary,Artifact}` (0–6) |
| Spellbook bank | `Enum.SpellBookSpellBank.{Player,Pet}` (0–1) |
| Spellbook item type | `Enum.SpellBookItemType.{None,Spell,FutureSpell,PetAction,Flyout}` (0–4; 1.12 only yields `Spell`/`PetAction`) |

</details>

<details>
<summary><b>Unit tokens</b> — <code>nameplateN</code>, <code>focus</code>, <code>markN</code></summary>

| Token | Resolves to |
|-------|-------------|
| `nameplate1`..`nameplateN` | Unit behind the Nth visible nameplate, in creation-order. Works with every `UnitX` function — `UnitName`, `UnitGUID`, `UnitClass`, `UnitHealth`, etc. Suffix chains (`nameplate1target`, `nameplate1targettarget`) compose. See [NamePlate / Unit tokens](docs/API.md#unit-tokens-nameplaten). |
| `focus` / `focustarget` | Sticky target set via [`FocusUnit`](docs/API.md#focusunitunit), cleared via [`ClearFocus`](docs/API.md#clearfocus). Same `UnitX` coverage as `nameplateN`. Fires [`PLAYER_FOCUS_CHANGED`](docs/API.md#player_focus_changed-event) on transition. See [Focus](docs/API.md#focus). |
| `mark1`..`mark8` | Unit currently wearing the Nth raid-target marker (`mark1` = star … `mark8` = skull). Same `UnitX` coverage as `nameplateN`, and fires unit events (`UNIT_HEALTH`, `UNIT_AURA`, …) with `arg1 == "markN"`. `UnitExists("markN")` is `false` when the marker is unset, out of range, or on a non-unit. Suffix chains (`mark1target`) compose. See [Unit tokens (`markN`)](docs/API.md#unit-tokens-markn). |

</details>

<details>
<summary><b>Bindings</b> — direct-action and override binding families</summary>

ClassicAPI backports the later-client direct-action and temporary override
binding families. Permanent bindings use the standard `SPELL`, `ITEM`,
`MACRO`, and `CLICK` command strings and can be saved normally. Overrides are
session-only, frame-owned, and support the usual priority flag.

See the [Bindings API reference](docs/API.md#bindings) for signatures,
precedence, execution behavior, macro-text usage, and 1.12 compatibility
notes.

**Predefined focus bindings.** Injected into the engine's **Targeting
Functions** group at FrameXML Bindings.xml load time, so they appear in the
keybind UI alongside native targeting bindings instead of orphaned at the
bottom.

| Binding | Action |
|---------|--------|
| `FOCUSTARGET` | `FocusUnit("target")` — pin current target as focus |
| `TARGETFOCUS` | `TargetUnit("focus")` — switch target to the focus |

See [Predefined focus bindings](docs/API.md#predefined-focus-bindings-focustarget--targetfocus)
for the implementation note.

</details>

## Installation

Download the prebuilt `ClassicAPI.dll` from the
[latest release](https://github.com/brues-code/ClassicAPI/releases/latest)
(or [build it yourself](#building)). It's loaded with
[VanillaFixes](https://github.com/hannesmann/vanillafixes):

1. Install VanillaFixes if it isn't already.
2. Copy `ClassicAPI.dll` into your game directory.
3. Add `ClassicAPI.dll` to `dlls.txt`.
4. Launch the game with `VanillaFixes.exe`.

The bundled `!!!ClassicAPI` addon ships *inside* the DLL — no separate
addon download or install step needed.

## Bundled addon: !!!ClassicAPI

The Lua-side companion library lives in
[`AddOns/!!!ClassicAPI/`](AddOns/!!!ClassicAPI/). It's a 1.12.1 /
Lua 5.0 backport of the modern Blizzard helpers that aren't engine
functions but that consumer code still expects to find as globals —
`CallbackRegistryMixin`, `EventRegistry`, `ColorMixin` + `CreateColor`,
`Item` / `ItemLocation`, `MathUtil`(`Lerp` / `Clamp` / `CreateCounter`),
`TableUtil` (`tCompare`, `MergeTable`, `SafePack`, etc.), and `EventUtil`
(`ContinueOnAddOnLoaded` etc.).

**You don't have to install the addon manually** — the DLL embeds
the contents of [`AddOns/!!!ClassicAPI/`](AddOns/!!!ClassicAPI/) and
registers them with the engine as a synthetic addon on startup, so
the library is always available when the DLL is loaded. It fires
`ADDON_LOADED`, supports `SavedVariables`, and is resolvable by name —
`IsAddOnLoaded("!!!ClassicAPI")`, `GetAddOnInfo("!!!ClassicAPI")`, and
`## Dependencies: !!!ClassicAPI` all work as usual.

**It is intentionally always-on and cannot be disabled.** The library
provides FrameXML-compat fixes that other addons and the DLL's own
features rely on, so leaving it toggleable would let a user (or a
one-off character-select uncheck) silently break them. The DLL
therefore hides it from the character-select AddOns list and
force-enables it every login — so it never appears as a checkbox and
can't be switched off. Being hidden means it's omitted from the
index-based `GetNumAddOns()` / `GetAddOnInfo(i)` enumeration, but every
by-name lookup and dependency reference still resolves normally.

If you drop the folder into your `Interface/AddOns/` directory, **that
copy's files win** — the DLL's embedded version only serves files when
the engine's normal scan doesn't already have a newer entry under that
name. Useful for editing the Lua locally without rebuilding the DLL.
The hidden + force-enabled treatment applies either way (it's keyed to
the addon name, not to which copy serves the files), so the local copy
is also absent from the AddOns list. The dispatch is transparent:
addons consuming `Mixin`, `ColorMixin`, `TableUtil`, etc. behave
identically in both cases.

## Bundled addon: DebugTools

A 1.12.1 / Lua 5.0 backport of Blizzard's `Blizzard_DebugTools` addon
(originally shipped with the 3.0 client) lives in [`AddOns/DebugTools/`](AddOns/DebugTools/).
It's an independent addon — it doesn't use anything ClassicAPI adds, and
ClassicAPI works fine without it. It's bundled here because it's the
natural companion for testing and debugging anything written against the
1.12 Lua surface (with or without the ClassicAPI extensions).

Slash commands provided:

| Command | Purpose |
|---------|---------|
| `/dump <expr>` | Pretty-print any Lua value, including tables and multi-return tuples. The right tool for inspecting return values from `GetSpellInfo`, `C_Item.GetItemInfoInstant`, etc. |
| `/etrace` | Event tracer window. `/etrace start`, `/etrace stop`, `/etrace add EVENT`, etc. |
| `/framestack` (or `/fstack`) | Tooltip showing the frame hierarchy under the mouse cursor. |
| `/luaerrors` (or `/scripterrors`) | Lua error display window. |

Lua globals provided (backports of 3.3.5 helpers that don't exist in
1.12):

| Global | Purpose |
|--------|---------|
| `print(...)` | Backport of the 3.3.5 `print` — concats varargs with `" "` and pushes to `DEFAULT_CHAT_FRAME`. Routes through `setprinthandler`'s handler; falls back via `geterrorhandler` if the handler errors. |
| `setprinthandler(func)` / `getprinthandler()` | Install / query a custom print handler. Useful for redirecting print output in tests. |
| `tostringall(...)` | Apply `tostring()` to every vararg, preserving the count. Lua 5.0-compatible (uses `arg.n` since `select` doesn't exist in 5.0). |

To install: copy [`AddOns/DebugTools/`](AddOns/DebugTools/) into your
`Interface/AddOns/` directory like any other addon.

## Building

Requires CMake (3.10+) and an MSVC toolchain that can target 32-bit Windows.
WoW.exe is x86, so the DLL must be built as Win32; an x64 build will not
load.

```powershell
git submodule update --init --recursive   # fetches MinHook, picojson, tinycbor
cmake -B build -A Win32
cmake --build build --config Release
```

The output is `build/Release/ClassicAPI.dll`.

To stamp a version into `CLASSIC_API_VERSION`, pass `-DCLASSICAPI_TAG=vX.Y.Z`
at configure time; the value exposed to Lua will be `X*10000 + Y*100 + Z`.

## License

GPL v3 or later. See the headers in `src/` for the full notice.
