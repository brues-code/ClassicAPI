# ClassicAPI

A small DLL for World of Warcraft 1.12.1 (Vanilla / Turtle WoW) that adds a
collection of Lua API calls Blizzard never exposed in 1.12 but which make
addon authoring noticeably less painful — primarily for backporting addons
written against later API versions (3.3.5+) where these calls do exist.

The DLL hooks the FrameScript engine after WoW boots and registers its
extensions through the same in-engine mechanisms WoW uses for its own Lua
functions. No companion addon is required.

## What's added

Full per-function reference: **[docs/API.md](docs/API.md)**.

### Calls

| Namespace | Calls |
|-----------|-------|
| [Action](docs/API.md#action) | `GetActionInfo` |
| [AddOns](docs/API.md#addons) | `C_AddOns.DoesAddOnExist`, `C_AddOns.GetAddOnName`, `C_AddOns.GetAddOnNotes`, `C_AddOns.GetAddOnSecurity`, `C_AddOns.GetAddOnTitle`, `C_AddOns.IsAddOnLoadable` |
| [Chat](docs/API.md#chat) | `GetCurrentChatGUID` |
| [Class](docs/API.md#class) | `FillLocalizedClassList` |
| [Combat](docs/API.md#combat) | `InCombatLockdown` |
| [CVar](docs/API.md#cvar) | `C_CVar.GetCVarBool` |
| [Container](docs/API.md#container) | `C_Container.GetContainerItemCharges`, `C_Container.GetContainerItemDurability`, `C_Container.GetContainerItemID`, `C_Container.GetContainerItemRepairCost`, `C_Container.GetContainerNumFreeSlots`, `C_Container.GetItemCooldown`, `C_Container.IsContainerItemOpenable`, `C_Container.MoveItem`, `C_Container.PlayerHasHearthstone`, `C_Container.SwapItems`, `C_Container.UseHearthstone`, `GetItemCooldown` |
| EncodingUtil | `C_EncodingUtil.CompressString`, `C_EncodingUtil.DecompressString`, `C_EncodingUtil.EncodeBase64`, `C_EncodingUtil.DecodeBase64`, `C_EncodingUtil.EncodeHex`, `C_EncodingUtil.DecodeHex`, `C_EncodingUtil.SerializeJSON`, `C_EncodingUtil.DeserializeJSON`, `C_EncodingUtil.SerializeCBOR`, `C_EncodingUtil.DeserializeCBOR` |
| [EquipmentSet](docs/API.md#equipmentset) | `C_EquipmentSet.CanUseEquipmentSets`, `C_EquipmentSet.ClearIgnoredSlotsForSave`, `C_EquipmentSet.CreateEquipmentSet`, `C_EquipmentSet.DeleteEquipmentSet`, `C_EquipmentSet.EquipmentSetContainsLockedItems`, `C_EquipmentSet.GetEquipmentSetID`, `C_EquipmentSet.GetEquipmentSetIDs`, `C_EquipmentSet.GetEquipmentSetInfo`, `C_EquipmentSet.GetIgnoredSlots`, `C_EquipmentSet.GetItemIDs`, `C_EquipmentSet.GetItemLocations`, `C_EquipmentSet.GetNumEquipmentSets`, `C_EquipmentSet.IgnoreSlotForSave`, `C_EquipmentSet.IsSlotIgnoredForSave`, `C_EquipmentSet.ModifyEquipmentSet`, `C_EquipmentSet.SaveEquipmentSet`, `C_EquipmentSet.UnignoreSlotForSave`, `C_EquipmentSet.UseEquipmentSet` (sets persist to `WTF\Account\...\ClassicAPI_EquipmentSets.txt`) |
| [Events](docs/API.md#events) | `C_EventUtils.IsEventValid` |
| [Expansion](docs/API.md#expansion) | `ClassicExpansionAtLeast`, `ClassicExpansionAtMost`, `GetClassicExpansionLevel` |
| [Faction](docs/API.md#faction) | `C_Reputation.GetFactionDataByIndex`, `C_Reputation.GetFactionStandings`, `C_Reputation.GetLastStandingChange`, `C_Reputation.GetWatchedFactionData`, `C_Reputation.SetWatchedFactionByID`, `GetFactionIDByIndex`, `GetFactionInfoByID`, `GetFactionParentID` |
| [Focus](docs/API.md#focus) | `ClearFocus`, `FocusUnit` |
| [FriendList](docs/API.md#friendlist) | `C_FriendList.IsWhoQueryPending`, `C_FriendList.SendWhoQueryByName` |
| [Gossip](docs/API.md#gossip) | `C_GossipInfo.CloseGossip`, `C_GossipInfo.GetActiveQuests`, `C_GossipInfo.GetAvailableQuests`, `C_GossipInfo.GetNumActiveQuests`, `C_GossipInfo.GetNumAvailableQuests`, `C_GossipInfo.GetNumOptions`, `C_GossipInfo.GetOptions`, `C_GossipInfo.GetText`, `C_GossipInfo.SelectActiveQuest`, `C_GossipInfo.SelectAvailableQuest`, `C_GossipInfo.SelectOption`, `C_GossipInfo.SelectOptionByIndex` |
| [GameTooltip](docs/API.md#gametooltip) | `GameTooltip:GetGameObject`, `GameTooltip:GetItem`, `GameTooltip:GetOwner`, `GameTooltip:GetSpell`, `GameTooltip:GetUnitGUID`, `GameTooltip:HasGameObject`, `GameTooltip:HasItem`, `GameTooltip:HasSpell`, `GameTooltip:HasUnit`, `GameTooltip:SetEquipmentSet`, `GameTooltip:SetInventoryItemByID`, `GameTooltip:SetItemByID`, `GameTooltip:SetSpellByID`, `GameTooltip:SetTalentByID`, `GameTooltip:SetUnitAura` |
| [Hooks](docs/API.md#hooks) | `hooksecurefunc` |
| [Input](docs/API.md#input) | `IsLeftAltKeyDown`, `IsLeftControlKeyDown`, `IsLeftShiftKeyDown`, `IsModifierKeyDown`, `IsRightAltKeyDown`, `IsRightControlKeyDown`, `IsRightShiftKeyDown` |
| [Instance](docs/API.md#instance) | `GetInstanceInfo` |
| [Item](docs/API.md#item) | `C_Item.DoesItemExist`, `C_Item.DoesItemExistByID`, `C_Item.EquipItemByName`, `C_Item.GetCurrentItemLevel`, `C_Item.GetDetailedItemLevelInfo`, `C_Item.GetItemCount`, `C_Item.GetItemFamily`, `C_Item.GetItemGUID`, `C_Item.GetItemIcon`, `C_Item.GetItemIconByID`, `C_Item.GetItemID`, `C_Item.GetItemInfoInstant`, `C_Item.GetItemInventoryType`, `C_Item.GetItemInventoryTypeByID`, `C_Item.GetItemLink`, `C_Item.GetItemLocation`, `C_Item.GetItemMaxStackSize`, `C_Item.GetItemMaxStackSizeByID`, `C_Item.GetItemName`, `C_Item.GetItemNameByID`, `C_Item.GetItemQuality`, `C_Item.GetItemQualityByID`, `C_Item.GetItemSellPrice`, `C_Item.GetItemSellPriceByID`, `C_Item.GetItemSetID`, `C_Item.GetItemSetIDByID`, `C_Item.GetItemSetInfo`, `C_Item.GetItemSpell`, `C_Item.GetItemUniqueness`, `C_Item.GetItemUniquenessByID`, `C_Item.GetStackCount`, `C_Item.GetWeaponEnchantInfo`, `C_Item.IsBound`, `C_Item.IsEquippableItem`, `C_Item.IsEquippedItem`, `C_Item.IsItemDataCached`, `C_Item.IsItemDataCachedByID`, `C_Item.IsItemOpenable`, `C_Item.IsLocked`, `C_Item.LockItem`, `C_Item.LockItemByGUID`, `C_Item.UnlockAllItems`, `C_Item.UnlockItem`, `C_Item.RequestLoadItemData`, `C_Item.RequestLoadItemDataByID`, `C_Item.UseItemByName`, `GetAuctionItemID`, `GetAuctionSellItemID`, `GetAverageItemLevel`, `GetCraftReagentItemID`, `GetInboxItemID`, `GetInventoryItemDurability`, `GetInventoryItemID`, `GetInventoryItemsForSlot`, `GetInventoryItemRepairCost`, `GetItemIcon`, `GetLootRollItemID`, `GetLootSlotItemID`, `GetMerchantItemID`, `GetQuestItemID`, `GetQuestLogItemID`, `GetTradePlayerItemID`, `GetTradeSkillItemID`, `GetTradeSkillReagentItemID`, `GetTradeTargetItemID`, `OffhandHasWeapon` |
| [Macros](docs/API.md#macros) | `GetMacroSpell` |
| [Map](docs/API.md#map) | `C_Map.GetBestMapForUnit` |
| [MerchantFrame](docs/API.md#merchantframe) | `C_MerchantFrame.GetBuybackItemID`, `C_MerchantFrame.GetItemInfo`, `C_MerchantFrame.GetNumJunkItems`, `C_MerchantFrame.IsMerchantItemRefundable`, `C_MerchantFrame.IsSellAllJunkEnabled`, `C_MerchantFrame.SellAllJunkItems` |
| [NamePlate](docs/API.md#nameplate) | `C_NamePlate.GetNamePlateForGUID`, `C_NamePlate.GetNamePlateForUnit`, `C_NamePlate.GetNamePlateGUIDs`, `C_NamePlate.GetNamePlates` |
| [NameCache](docs/API.md#namecache) | `C_CreatureInfo.GetCreatureID`, `C_PlayerCache.GetPlayerInfoByName`, `C_PlayerCache.IsEnabled`, `C_PlayerCache.IsScanEnabled`, `C_PlayerCache.RememberPlayer`, `C_PlayerCache.SetEnabled`, `C_PlayerCache.SetScanEnabled`, `C_PlayerInfo.GUIDIsCreature`, `C_PlayerInfo.GUIDIsGameObject`, `C_PlayerInfo.GUIDIsPet`, `C_PlayerInfo.GUIDIsPlayer`, `GetPlayerInfoByGUID` |
| [Quest](docs/API.md#quest) | `C_QuestLog.GetQuestIDForLogIndex`, `C_QuestLog.GetTitleForQuestID`, `C_QuestLog.IsOnQuest`, `C_QuestLog.IsQuestDataCachedByID`, `C_QuestLog.IsUnitOnQuest`, `C_QuestLog.RequestLoadQuestByID`, `GetQuestLogLeaderBoardID` |
| [Spell](docs/API.md#spell) | `C_Spell.CancelSpellByID`, `C_Spell.DoesSpellExist`, `C_Spell.GetSchoolString`, `C_Spell.GetSpellCooldown`, `C_Spell.GetSpellDescription`, `C_Spell.GetSpellInfo`, `C_Spell.GetSpellLink`, `C_Spell.GetSpellName`, `C_Spell.GetSpellReagents`, `C_Spell.GetSpellSubtext`, `C_Spell.GetSpellTexture`, `C_Spell.IsAutoAttackSpell`, `C_Spell.IsCurrentSpell`, `C_Spell.IsRangedAutoAttackSpell`, `C_Spell.IsSelfBuff`, `C_Spell.IsSpellHarmful`, `C_Spell.IsSpellHelpful`, `C_Spell.IsSpellPassive`, `C_Spell.IsSpellUsable`, `C_Spell.SpellHasRange`, `CancelSpellByName`, `CastSpellNoToggle`, `GetCraftSpellID`, `GetSpellInfo`, `GetSpellLink`, `GetSpellSchool`, `IsHarmfulSpell`, `IsHelpfulSpell`, `IsPassiveSpell`, `IsPlayerSpell`, `IsSpellKnown`, `IsUsableSpell`, `SpellHasRange` |
| [SpellBook](docs/API.md#spellbook) | `C_SpellBook.GetCurrentLevelSpells`, `C_SpellBook.GetSpellLevelLearned`, `C_SpellBook.GetSpellSkillLine`, `C_SpellBook.IsAutoAttackSpellBookItem`, `C_SpellBook.IsRangedAutoAttackSpellBookItem`, `FindSpellBookSlotByID` |
| [State](docs/API.md#state) | `CancelShapeshiftForm`, `Dismount`, `GetMirrorTimerInfo`, `GetMirrorTimerProgress`, `GetShapeshiftFormID`, `IsAssistingRitual`, `IsFalling`, `IsInGroup`, `IsInRaid`, `IsMounted`, `IsStealthed`, `IsSwimming` |
| [Table](docs/API.md#table) | `table.wipe` |
| [Talent](docs/API.md#talent) | `GetTalentIDByIndex`, `GetTalentSpellID` |
| [Time](docs/API.md#time) | `C_DateAndTime.AdjustTimeByDays`, `C_DateAndTime.AdjustTimeByMinutes`, `C_DateAndTime.CompareCalendarTime`, `C_DateAndTime.GetCalendarTimeFromEpoch`, `C_DateAndTime.GetCurrentCalendarTime`, `C_DateAndTime.GetSecondsUntilDailyReset`, `C_DateAndTime.GetServerTimeLocal`, `C_Timer.After`, `C_Timer.NewTicker`, `C_Timer.NewTimer`, `GetServerTime` |
| [UIColor](docs/API.md#uicolor) | `C_UIColor.GetColors` (backports `GlobalColor.dbc`; companion addon wraps each row with `ColorMixin` and assigns `_G[baseTag]` + `_G[baseTag.."_CODE"]`) |
| [Unit](docs/API.md#unit) | `GetUnitSpeed`, `UnitClassBase`, `UnitGUID`, `UnitInRange`, `UnitIsAFK`, `UnitIsDND`, `UnitIsFeignDeath`, `UnitIsInMyGuild`, `UnitIsPossessed`, `UnitPower`, `UnitPowerMax`, `UnitPowerType`, `UnitRaceBase`, `UnitStandState`, `UnitTokenFromGUID` |
| [UnitAuras](docs/API.md#unitauras) | `C_UnitAuras.GetAuraDataByIndex`, `C_UnitAuras.GetAuraDataBySpellName`, `C_UnitAuras.GetAuraDispelTypeColor`, `C_UnitAuras.GetBuffDataByIndex`, `C_UnitAuras.GetDebuffDataByIndex`, `C_UnitAuras.GetPlayerAuraBySpellID`, `C_UnitAuras.GetUnitAuraBySpellID`, `C_UnitAuras.GetUnitAuras` |

### GlueXML calls

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
| Script | `RunScript` (compile and run a Lua chunk in the glue state's globals — useful for slash-command-style helpers in GlueXML) |

### Macros

Engine-level extensions to macro parsing and dispatch — no new Lua
functions, just behavior the stock 1.12 engine didn't have. See the
[Macros section in the Lua reference](docs/API.md#macros) for details.

| Form | What it does |
|------|--------------|
| `/cast <spellID>` | `/cast 5019` casts Shoot if known; macro slot tags correctly for action-bar UI |
| `CastSpellByName("<spellID>")` | Same — numeric strings resolve through the engine's name resolver |
| `CastSpellNoToggle("<name>")` in a macro | Engine's macro parser now recognizes it as a primary-spell line, so the macro slot in an addon like pfUI highlights when its spell is auto-repeating or its self-aura is active |

### Events

| Event | Payload |
|-------|---------|
| `BAG_UPDATE_DELAYED` | *(none)* |
| `EQUIPMENT_SETS_CHANGED` | *(none)* |
| `EQUIPMENT_SWAP_PENDING` | `setID` |
| `EQUIPMENT_SWAP_FINISHED` | `success, setID` |
| `FACTION_STANDING_CHANGED` | `factionID, newStanding, repGained` |
| `GLOBAL_MOUSE_DOWN` | `button` |
| `GLOBAL_MOUSE_UP` | `button` |
| `HEARTHSTONE_BOUND` | *(none)* |
| `ITEM_DATA_LOAD_RESULT` | `itemID, success` |
| `MODIFIER_STATE_CHANGED` | `keyName, down` |
| `NAME_PLATE_CREATED` | `nameplateFrame` |
| `NAME_PLATE_UNIT_ADDED` | `unitToken` ("nameplateN") |
| `NAME_PLATE_UNIT_REMOVED` | `unitToken` ("nameplateN") |
| `PLAYER_FOCUS_CHANGED` | *(none)* |
| `PLAYER_STARTED_LOOKING` | *(none)* |
| `PLAYER_STOPPED_LOOKING` | *(none)* |
| `PLAYER_STARTED_MOVING` | *(none)* |
| `PLAYER_STOPPED_MOVING` | *(none)* |
| `PLAYER_STARTED_TURNING` | *(none)* |
| `PLAYER_STOPPED_TURNING` | *(none)* |
| `QUEST_ACCEPTED` | `questLogIndex, questID` |
| `QUEST_DATA_LOAD_RESULT` | `questID, success` |
| `QUEST_TURNED_IN` | `questID, xpReward, moneyReward` |
| `UPDATE_SHAPESHIFT_FORM` | *(none)* |

### Globals

| Group | Constants |
|-------|-----------|
| Version | `CLASSIC_API_VERSION` |
| Expansion | `LE_EXPANSION_LEVEL_CURRENT`, `LE_EXPANSION_CLASSIC` … `LE_EXPANSION_MIDNIGHT` |
| Item quality | `LE_ITEM_QUALITY_POOR` … `LE_ITEM_QUALITY_WOWTOKEN` |
| Unit stat | `LE_UNIT_STAT_STRENGTH` … `LE_UNIT_STAT_SPIRIT` |
| Addon security | `Enum.AddOnSecurityStatus.{Secure,Insecure,Banned,NotAvailable}` |
| Power type | `Enum.PowerType.{HealthCost,None,Mana,Rage,Focus,Energy,Happiness}` |

### Unit tokens

| Token | Resolves to |
|-------|-------------|
| `nameplate1`..`nameplateN` | Unit behind the Nth visible nameplate, in creation-order. Works with every `UnitX` function — `UnitName`, `UnitGUID`, `UnitClass`, `UnitHealth`, etc. Suffix chains (`nameplate1target`, `nameplate1targettarget`) compose. See [NamePlate / Unit tokens](docs/API.md#unit-tokens-nameplaten). |
| `focus` / `focustarget` | Sticky target set via [`FocusUnit`](docs/API.md#focusunitunit), cleared via [`ClearFocus`](docs/API.md#clearfocus). Same `UnitX` coverage as `nameplateN`. Fires [`PLAYER_FOCUS_CHANGED`](docs/API.md#player_focus_changed-event) on transition. See [Focus](docs/API.md#focus). |

## Installation

Use [VanillaFixes](https://github.com/hannesmann/vanillafixes) to load the
DLL.

1. Install VanillaFixes if it isn't already.
2. Copy `ClassicAPI.dll` into your game directory.
3. Add `ClassicAPI.dll` to `dlls.txt`.
4. Launch the game with `VanillaFixes.exe`.

## Bundled addon: !!!ClassicAPI

The Lua-side companion library lives in
[`AddOns/!!!ClassicAPI/`](AddOns/!!!ClassicAPI/). It's a 1.12.1 /
Lua 5.0 backport of the modern Blizzard helpers that aren't engine
functions but that consumer code still expects to find as globals —
`Mixin` / `CreateFromMixins`, `CallbackRegistryMixin`, `EventRegistry`,
`ColorMixin` + `CreateColor`, `Item` / `ItemLocation`, `MathUtil`
(`Lerp` / `Clamp` / `CreateCounter`), `TableUtil` (`tCompare`,
`MergeTable`, `SafePack`, etc.), and `EventUtil`
(`ContinueOnAddOnLoaded` etc.).

Some of the addon's surface depends on engine functions ClassicAPI
provides (`C_UIColor.GetColors` populates the modern color globals,
`C_Item.GetItemInfoInstant` powers `ItemMixin`, the
`ITEM_DATA_LOAD_RESULT` event drives `ContinueOnItemLoad`); the rest
is pure Lua and would work standalone. Without the DLL the addon
still loads, but those features no-op.

The triple-`!` prefix is **load-order-significant** — WoW loads
addons alphabetically, so `!!!ClassicAPI` is guaranteed to run before
any consumer that depends on its globals. Don't rename the folder.

To install: copy [`AddOns/!!!ClassicAPI/`](AddOns/!!!ClassicAPI/) into
your `Interface/AddOns/` directory.

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

LGPL v3 or later. See the headers in `src/` for the full notice.
