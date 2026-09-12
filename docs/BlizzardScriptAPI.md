# Blizzard Script API — `C:\WoW\Octo\WoW.exe` (Vanilla 1.12.1 / Turtle WoW)

Catalogue of every Lua function and frame-method that the WoW client exposes
to addons, recovered by scanning the binary directly. This is the source of
truth for "what can a Lua addon call?" — extracted by walking the engine's
own registration tables.

ImageBase `0x00400000`. All VAs in this doc use that base.

## How Blizzard wires Lua

There are two Blizzard registries, plus the Lua standard libraries, which are
bound a third way (§3) and are NOT in the batch counts below.

### 1. Global functions — `FrameScript_RegisterFunction` at `0x00704120`

Calling convention: `__fastcall(ecx = const char *name, edx = lua_CFunction func)`.
At call time the C function is wrapped as a closure and assigned to the global
table at `LUA_GLOBALSINDEX (-10001)`.

The client never registers globals one-by-one with hard-coded immediates.
Instead, every batch is a `{ char *name; void *func; }[]` array in `.data`
that gets walked by an inline loop:

```asm
; canonical pattern — appears 54 times in the binary
56                  push esi
33 F6               xor  esi, esi
.loop:
8B 96 <addr+4>      mov  edx, [esi + table+4]    ; func ptr
8B 8E <addr+0>      mov  ecx, [esi + table+0]    ; name ptr
E8 <FrameScript_RegisterFunction>
83 C6 08            add  esi, 8                  ; entry stride
81 FE <count*8>     cmp  esi, count*8            ; (or `83 FE imm8` for small)
72 <-back>          jb   .loop
5E                  pop  esi
C3                  ret
```

Recovering every batch and its entries: 54 tables, **1153 globals total**
(of which 2 tables / 15 entries are Turtle WoW additions, broken out in
[`TurtleScriptAPI.md`](TurtleScriptAPI.md)).

### 2. Frame methods — iterator at `0x00701D80`

Calling convention: `__fastcall(ecx = MethodEntry *table, edx = count, [stack] = context)`.

Each frame-type method registry is a single trampoline:

```asm
68 <context>        push <context VA>            ; e.g. 0x00C0CF20 for GameTooltip
BA <count>          mov  edx, count
B9 <table>          mov  ecx, table VA
E8 <iterator>       call 0x00701D80
C3                  ret
```

23 tables found, **481 methods total**. The runtime dispatcher at `0x005368D0`
resolves `frame:method(...)` by looking the name up in the matching context.

### 3. Lua standard libraries — `luaL_openlib` tables

The Lua 5.0 standard libraries, and Blizzard's replacements inside them, are
bound by `luaL_openlib` walking one `luaL_reg { char *name; lua_CFunction
func; }[]` table per library when the Lua state is created (`FUN_00703B80`,
called immediately BEFORE `FUN_LOAD_SCRIPT_FUNCTIONS`). They never pass
through `FrameScript_RegisterFunction`, so none of them appear in the 54
batches above or in the loop scan behind `raw_globals.txt` — which is how
`debugprofilestart` / `debugprofilestop` (stock since 1.7.0) went
uncatalogued here until 2026-09. **Before declaring a global absent, check
these tables or `/dump` the name in game.**

**Base library** — table `[0x00811E28]`, 36 entries, `.rdata`. Blizzard's
own base library: the stock 5.0 set minus `dofile`/`loadfile`/`require`/
`print` (`print` is a ClassicAPI backport), plus the `debug*` /
`*errorhandler` / `date` / `time` additions.

```
0x00702770  setglobal          0x00702DF0  type
0x007027B0  getglobal          0x00702E30  tonumber
0x007027E0  debugload          0x00702F40  tostring
0x007027F0  debuginfo          0x007030A0  unpack
0x00702800  debugprint         0x007030F0  rawequal
0x00702810  debugdump          0x00703130  rawget
0x00702820  debugbreak         0x00703160  rawset
0x00702830  debugtimestamp     0x007031A0  assert
0x00702840  debugprofilestart  0x00703200  gcinfo
0x00702860  debugprofilestop   0x00703250  collectgarbage
0x00702900  seterrorhandler    0x00703280  loadstring
0x00702950  geterrorhandler    0x007032F0  pcall
0x00702970  error              0x00703340  xpcall
0x007029F0  getmetatable       0x007033A0  date
0x00702A40  setmetatable       0x007035A0  time
0x00702AC0  getfenv            0x00703760  debugstack
0x00702BE0  setfenv
0x00702C90  next
0x00702CD0  ipairs
0x00702DA0  pairs
```

`debugprofilestart` stores `rdtsc` in `0x00CEEB58` (u64);
`debugprofilestop` pushes `|rdtsc − start| / (ticksPerSecond / 1000)` as a
double, where `ticksPerSecond` is `FUN_0042C020` = `(int64)(1000.0 /
[0x008332C8])` — a TSC scale calibrated at boot (the image holds `1.0`). So:
milliseconds with a sub-microsecond fraction, from the CPU counter, unrelated
to the engine's millisecond tick and to `GetTime()`.

The `table`, `string` and `math` libraries have the same shape (the `table`
library's `luaL_reg` is at `0x00822D70` — see `Offsets.h`
`FUN_LUA_TABLE_INSERT`) and are not enumerated here. The short global
aliases (`tinsert`, `getn`, `strlen`, `format`, `floor`, …) are not registered
at all: an embedded Lua chunk at `.data 0x008722E8`, run by the same state
init, assigns them from the library tables (`tinsert = tab.insert`). A module
that registers over a library function must therefore re-bind its alias —
`Game::Lua::RegisterGlobalAlias`.

## Scope

The 54 global-function batches split into two phases:

* **Glue** (login screen): batches with call sites in `0x00458xxx..0x004736xx`.
  Five tables, 109 functions. Available only before `EnterWorld`.
* **In-game**: everything else. 47 tables, 1029 functions (excluding Turtle).
  These are what an in-world addon can call. (`PlaySound` and friends are
  technically the glue Sound batch and become available again post-login
  through other means.)

Frame methods are all in-game; the per-frame-type contexts are populated as
the FrameScript engine boots and stay live for the world session.

## In-game global functions — by category

The category labels reflect the function set; the table VAs are where each
batch lives in `.data`.

### Sound `[0x00835A50]` — 4
```
PlaySound  PlayMusic  PlaySoundFile  StopMusic
```

### Chat & channels `[0x00843618]` — 50
```
SendChatMessage  SendAddonMessage  GetNumLaguages  GetLanguageByIndex
GetDefaultLanguage  DoEmote  LoggingChat  LoggingCombat  JoinChannelByName
LeaveChannelByName  ListChannelByName  ListChannels  GetChannelList
SetChannelPassword  SetChannelOwner  DisplayChannelOwner  GetChannelName
ChannelModerator  ChannelUnmoderator  ChannelMute  ChannelUnmute
ChannelInvite  ChannelKick  ChannelBan  ChannelUnban
ChannelToggleAnnouncements  ChannelModerate  ChangeChatColor
ResetChatColors  GetChatTypeIndex  GetChatWindowInfo  GetChatWindowMessages
GetChatWindowChannels  AddChatWindowMessages  RemoveChatWindowMessages
AddChatWindowChannel  RemoveChatWindowChannel  SetChatWindowName
SetChatWindowSize  SetChatWindowColor  SetChatWindowAlpha
SetChatWindowLocked  SetChatWindowDocked  SetChatWindowShown
EnumerateServerChannels  RequestRaidInfo  GetGuildRecruitmentMode
SetGuildRecruitmentMode  GetNumSavedInstances  GetSavedInstanceInfo
```

Note: `GetNumLaguages` is misspelled in the binary (sic).

### World map `[0x00845078]` — 23
```
GetMapContinents  GetMapZones  SetMapZoom  SetMapToCurrentZone  GetMapInfo
GetCurrentMapContinent  GetCurrentMapZone  ProcessMapClick  UpdateMapHighlight
GetPlayerMapPosition  GetCorpseMapPosition  GetNumMapLandmarks
GetMapLandmarkInfo  GetWorldLocMapPosition  GetNumMapOverlays
GetMapOverlayInfo  CreateWorldMapArrowFrame  CreateMiniWorldMapArrowFrame
UpdateWorldMapArrowFrames  PositionWorldMapArrowFrame
PositionMiniWorldMapArrowFrame  ShowWorldMapArrowFrame
ShowMiniWorldMapArrowFrame
```

### Battlefields / PvP queue `[0x008457E0]` — 31
```
GetNumBattlefields  GetBattlefieldInfo  GetBattlefieldInstanceInfo
JoinBattlefield  SetSelectedBattlefield  GetSelectedBattlefield
AcceptBattlefieldPort  GetBattlefieldStatus  GetBattlefieldPortExpiration
GetBattlefieldInstanceExpiration  GetBattlefieldInstanceRunTime
GetBattlefieldEstimatedWaitTime  GetBattlefieldTimeWaited
CloseBattlefield  ShowBattlefieldList  RequestBattlefieldScoreData
GetNumBattlefieldScores  GetBattlefieldScore  GetBattlefieldWinner
SetBattlefieldScoreFaction  LeaveBattlefield  GetNumBattlefieldStats
GetBattlefieldStatInfo  GetBattlefieldStatData  RequestBattlefieldPositions
GetNumBattlefieldPositions  GetBattlefieldPosition
GetNumBattlefieldFlagPositions  GetBattlefieldFlagPosition
CanJoinBattlefieldAsGroup  GetBattlefieldMapIconScale
```

### Mail `[0x00845EB0]` — 30
```
CloseMail  ClearSendMail  ClickSendMailItemButton  SetSendMailMoney
GetSendMailMoney  SetSendMailCOD  GetSendMailCOD  GetNumStationeries
GetStationeryInfo  SelectStationery  GetSelectedStationeryTexture
GetNumPackages  GetPackageInfo  SelectPackage  GetSendMailItem
GetSendMailPrice  SendMail  CheckInbox  GetInboxNumItems
GetInboxHeaderInfo  GetInboxText  GetInboxInvoiceInfo  GetInboxItem
TakeInboxMoney  TakeInboxItem  TakeInboxTextItem  ReturnInboxItem
DeleteInboxItem  InboxItemCanDelete  HasNewMail
```

### Spellbook `[0x00846648]` — 19
```
GetNumSpellTabs  GetSpellTabInfo  GetSpellTexture  GetSpellName
GetSpellCooldown  GetSpellAutocast  ToggleSpellAutocast  PickupSpell
CastSpell  IsCurrentCast  UpdateSpells  PlayerHasSpells  HasPetSpells
IsSpellPassive  GetNumShapeshiftForms  GetShapeshiftFormInfo
CastShapeshiftForm  GetShapeshiftFormCooldown  CastSpellByName
```

### Tutorials `[0x00846B04]` — 4
```
TutorialsEnabled  FlagTutorial  ClearTutorials  ResetTutorials
```

### Key bindings `[0x00846C88]` — 9
```
GetNumBindings  GetBinding  SetBinding  GetBindingKey  GetBindingAction
RunBinding  GetCurrentBindingSet  LoadBindings  SaveBindings
```

### Raid `[0x00847240]` — 16
```
GetNumRaidMembers  GetRaidRosterInfo  SetRaidRosterSelection
GetRaidRosterSelection  IsRaidLeader  IsRaidOfficer  SetRaidSubgroup
SwapRaidSubgroup  ConvertToRaid  PromoteToAssistant  DemoteAssistant
SetRaidTarget  GetRaidTargetIndex  DoReadyCheck  ConfirmReadyCheck
CheckReadyCheckTime
```

### Pet `[0x008475A0]` — 28
```
PetHasActionBar  GetPetActionInfo  GetPetActionCooldown  GetPetActionsUsable
IsPetAttackActive  PickupPetAction  TogglePetAutocast  CastPetAction
PetPassiveMode  PetDefensiveMode  PetAggressiveMode  PetWait  PetFollow
PetAttack  PetStopAttack  PetAbandon  PetDismiss  PetRename
PetCanBeAbandoned  PetCanBeRenamed  GetPetTimeRemaining  HasPetUI
GetPetLoyalty  GetPetTrainingPoints  GetPetExperience  GetPetHappiness
GetPetFoodTypes  GetPetIcon
```

### Trade `[0x008479F8]` — 14
```
CloseTrade  ClickTradeButton  ClickTargetTradeButton  GetTradeTargetItemInfo
GetTradeTargetItemLink  GetTradePlayerItemInfo  GetTradePlayerItemLink
AcceptTrade  CancelTradeAccept  GetPlayerTradeMoney  GetTargetTradeMoney
PickupTradeMoney  AddTradeMoney  SetTradeMoney
```

### Loot `[0x00847CF0]` — 16
```
SetLootPortrait  GetNumLootItems  GetLootSlotInfo  GetLootSlotLink
LootSlotIsItem  LootSlotIsCoin  LootSlot  CloseLoot  IsFishingLoot
GetMasterLootCandidate  GiveMasterLoot  GetLootRollItemInfo
GetLootRollItemLink  GetLootRollTimeLeft  RollOnLoot  ConfirmLootRoll
```

### World state UI `[0x00848414]` — 2
```
GetNumWorldStateUI  GetWorldStateUIInfo
```

### Inventory & player items `[0x008484D0]` — 27
```
GetInventorySlotInfo  GetInventoryItemTexture  GetInventoryItemBroken
GetInventoryItemCount  GetInventoryItemQuality  GetInventoryItemCooldown
GetInventoryItemLink  KeyRingButtonIDToInvSlotID  PickupInventoryItem
UseInventoryItem  IsInventoryItemLocked  PutItemInBag  PutItemInBackpack
PickupBagFromSlot  CursorCanGoInSlot  ShowInventorySellCursor
SetInventoryPortaitTexture  GetGuildInfo  GetInventoryAlertStatus
UpdateInventoryAlertStatus  OffhandHasWeapon  HasInspectHonorData
RequestInspectHonorData  GetInspectHonorData  ClearInspectPlayer
GetWeaponEnchantInfo  HasWandEquipped
```

Note: `SetInventoryPortaitTexture` is misspelled in the binary (sic).

### Meeting Stone `[0x00848CE8]` — 3
```
IsInMeetingStoneQueue  CancelMeetingStoneRequest  GetMeetingStoneStatusText
```

### Pet stables `[0x00848DA0]` — 13
```
ClosePetStables  StablePet  UnstablePet  BuyStableSlot  GetNumStablePets
GetNumStableSlots  GetStablePetInfo  GetNextStableSlotCost  ClickStablePet
PickupStablePet  GetSelectedStablePet  SetPetStablePaperdoll
GetStablePetFoodTypes
```

### Auction house `[0x008490A8]` — 23
```
CloseAuctionHouse  GetAuctionHouseDepositRate  CalculateAuctionDeposit
ClickAuctionSellItemButton  GetAuctionSellItemInfo  StartAuction
QueryAuctionItems  GetOwnerAuctionItems  GetBidderAuctionItems
GetNumAuctionItems  GetAuctionItemInfo  GetAuctionItemLink
GetAuctionItemTimeLeft  PlaceAuctionBid  GetAuctionItemClasses
GetAuctionItemSubClasses  GetAuctionInvTypes  CanSendAuctionQuery
SortAuctionItems  SetSelectedAuctionItem  GetSelectedAuctionItem
IsAuctionSortReversed  CancelAuction
```

### Guild roster & ranks `[0x008496D8]` — 32
```
GetNumGuildMembers  GetGuildRosterMOTD  GetGuildRosterInfo
GetGuildRosterLastOnline  GuildRosterSetPublicNote  GuildRosterSetOfficerNote
SetGuildRosterSelection  GetGuildRosterSelection  CanGuildPromote
CanGuildDemote  CanGuildInvite  CanGuildRemove  CanEditMOTD
CanEditPublicNote  CanEditOfficerNote  CanViewOfficerNote  CanEditGuildInfo
SortGuildRoster  SetGuildRosterShowOffline  GetGuildRosterShowOffline
GuildControlGetNumRanks  GuildControlGetRankName  GuildControlSetRank
GuildControlGetRankFlags  GuildControlSetRankFlag  GuildControlSaveRank
GuildControlAddRank  GuildControlDelRank  CloseGuildRoster  GuildRoster
GetGuildInfoText  SetGuildInfoText
```

### Skills `[0x00849CF8]` — 13
```
GetNumSkillLines  GetSkillLineInfo  AbandonSkill  CollapseSkillHeader
ExpandSkillHeader  AddSkillUp  RemoveSkillUp  GetAdjustedSkillPoints
AcceptSkillUps  CancelSkillUps  BuySkillTier  SetSelectedSkill
GetSelectedSkill
```

### Duel `[0x00849FA8]` — 4
```
StartDuel  StartDuelUnit  AcceptDuel  CancelDuel
```

### Reputation / factions `[0x0084A0A8]` — 12
```
GetNumFactions  GetFactionInfo  GetWatchedFactionInfo  SetWatchedFactionIndex
FactionToggleAtWar  CollapseFactionHeader  SetFactionInactive  SetFactionActive
IsFactionInactive  ExpandFactionHeader  SetSelectedFaction  GetSelectedFaction
```

### Trainer `[0x0084A3E0]` — 30
```
OpenTrainer  CloseTrainer  GetNumTrainerServices  GetTrainerServiceInfo
SelectTrainerService  IsTradeskillTrainer  IsTalentTrainer
GetTrainerSelectionIndex  GetTrainerGreetingText  GetTrainerServiceIcon
GetTrainerServiceSkillLine  GetTrainerServiceCost  GetTrainerServiceLevelReq
GetTrainerServiceSkillReq  GetTrainerServiceNumAbilityReq
GetTrainerServiceAbilityReq  GetTrainerServiceStepReq
GetTrainerServiceDescription  IsTrainerServiceSkillStep
IsTrainerServiceLearnSpell  IsTrainerServiceTradeSkill
GetTrainerServiceStepIncrease  BuyTrainerService  SetTrainerServiceTypeFilter
SetTrainerSkillLineFilter  GetTrainerServiceTypeFilter
GetTrainerSkillLineFilter  GetTrainerSkillLines  CollapseTrainerSkillLine
ExpandTrainerSkillLine
```

### Taxi `[0x0084ADA8]` — 14
```
SetTaxiMap  NumTaxiNodes  TaxiNodeName  TaxiNodePosition  TaxiNodeCost
TakeTaxiNode  CloseTaxiMap  TaxiNodeGetType  TaxiNodeSetCurrent
TaxiGetSrcX  TaxiGetSrcY  TaxiGetDestX  TaxiGetDestY  GetNumRoutes
```

### Quest log `[0x0084B0F0]` — 34
```
GetNumQuestLogEntries  GetQuestLogTitle  SelectQuestLogEntry
GetQuestLogSelection  SetAbandonQuest  GetAbandonQuestName
GetAbandonQuestItems  AbandonQuest  IsUnitOnQuest  GetQuestLogQuestText
GetNumQuestLeaderBoards  GetQuestLogLeaderBoard  GetQuestLogTimeLeft
IsCurrentQuestFailed  GetNumQuestLogRewards  GetNumQuestLogChoices
GetQuestLogRewardInfo  GetQuestLogChoiceInfo  GetQuestLogItemLink
GetQuestLogRewardMoney  GetQuestLogRewardSpell  GetQuestLogRequiredMoney
GetQuestLogPushable  QuestLogPushQuest  GetQuestTimers  GetQuestIndexForTimer
CollapseQuestHeader  ExpandQuestHeader  GetQuestGreenRange
GetNumQuestWatches  IsQuestWatched  AddQuestWatch  RemoveQuestWatch
GetQuestIndexForWatch
```

### Gossip `[0x0084B7E8]` — 8
```
GetGossipText  GetGossipOptions  GetGossipAvailableQuests
GetGossipActiveQuests  SelectGossipOption  SelectGossipAvailableQuest
SelectGossipActiveQuest  CloseGossip
```

### Item text (books) `[0x0084BA00]` — 9
```
ItemTextGetItem  ItemTextGetCreator  ItemTextGetMaterial  ItemTextGetPage
ItemTextGetText  ItemTextHasNextPage  ItemTextPrevPage  ItemTextNextPage
CloseItemText
```

### Player buffs `[0x0084BB20]` — 8
```
GetPlayerBuff  GetPlayerBuffTexture  GetPlayerBuffDispelType
GetPlayerBuffApplications  GetPlayerBuffTimeLeft  CancelPlayerBuff
GetTrackingTexture  CancelTrackingBuff
```

### Action bars `[0x0084BD10]` — 21
```
GetActionTexture  GetActionCount  GetActionCooldown  GetActionAutocast
GetActionText  HasAction  UseAction  PickupAction  PlaceAction
IsAttackAction  IsCurrentAction  IsAutoRepeatAction  IsUsableAction
IsConsumableAction  IsEquippedAction  IsActionInRange  ActionHasRange
GetBonusBarOffset  ChangeActionBarPage  GetActionBarToggles  SetActionBarToggles
```

### Party / LFG `[0x0084C170]` — 16
```
GetNumPartyMembers  GetPartyMember  GetPartyLeaderIndex  IsPartyLeader
LeaveParty  GetLootMethod  SetLootMethod  GetLootThreshold  SetLootThreshold
GetLookingForGroup  SetLookingForGroup  LFGQuery  GetNumLFGResults
GetLFGResults  GetLFGTypes  GetLFGTypeEntries
```

### Macros `[0x0084C990]` — 9
```
CreateMacro  GetNumMacros  GetMacroInfo  DeleteMacro  EditMacro
GetNumMacroIcons  GetMacroIconInfo  PickupMacro  GetMacroIndexByName
```

### Talents `[0x0084CCF0]` — 6
```
GetNumTalentTabs  GetTalentTabInfo  GetNumTalents  GetTalentInfo
GetTalentPrereqs  LearnTalent
```

### Petition (guild charter, arena charter) `[0x0084CEA0]` — 8
```
ClosePetition  GetPetitionInfo  GetNumPetitionNames  GetPetitionNameInfo
CanSignPetition  SignPetition  OfferPetition  RenamePetition
```

### Guild registrar `[0x0084D00C]` — 5
```
CloseGuildRegistrar  GetGuildCharterCost  BuyGuildCharter
TurnInGuildCharter  GetTabardInfo
```

### Tabard creation `[0x0084D0B4]` — 2
```
CloseTabardCreation  GetTabardCreationCost
```

### Crafting (enchanting / beast training) `[0x0084D108]` — 19
```
CloseCraft  GetCraftName  GetCraftButtonToken  GetCraftDisplaySkillLine
GetNumCrafts  GetCraftInfo  SelectCraft  GetCraftSelectionIndex  GetCraftIcon
GetCraftSkillLine  GetCraftItemLink  GetCraftNumReagents
GetCraftReagentInfo  GetCraftReagentItemLink  GetCraftSpellFocus
GetCraftDescription  CollapseCraftSkillLine  ExpandCraftSkillLine  DoCraft
```

### Bank `[0x0084D5DC]` — 5
```
BankButtonIDToInvSlotID  GetNumBankSlots  GetBankSlotCost  PurchaseSlot
CloseBankFrame
```

### Container / bags `[0x0084D688]` — 11
```
ContainerIDToInventoryID  GetContainerNumSlots  GetContainerItemInfo
GetContainerItemLink  GetContainerItemCooldown  PickupContainerItem
SplitContainerItem  UseContainerItem  ShowContainerSellCursor
SetBagPortaitTexture  GetBagName
```

Note: `SetBagPortaitTexture` is misspelled in the binary (sic).

### Merchant `[0x0084DA28]` — 18
```
CloseMerchant  GetMerchantNumItems  GetMerchantItemInfo  GetBuybackItemInfo
GetMerchantItemLink  GetMerchantItemMaxStack  PickupMerchantItem
BuyMerchantItem  BuybackItem  CanMerchantRepair  ShowMerchantSellCursor
ShowBuybackSellCursor  ShowRepairCursor  HideRepairCursor  InRepairMode
GetRepairAllCost  RepairAllItems  GetNumBuybackItems
```

### Tradeskills (professions) `[0x0084DDD0]` — 26
```
CloseTradeSkill  GetNumTradeSkills  GetTradeSkillInfo  SelectTradeSkill
GetTradeSkillSelectionIndex  GetTradeSkillCooldown  GetTradeSkillIcon
GetTradeSkillNumMade  GetTradeSkillLine  GetTradeSkillItemStats
GetTradeSkillItemLink  GetTradeSkillNumReagents  GetTradeSkillReagentInfo
GetTradeSkillReagentItemLink  GetTradeSkillTools  GetTradeSkillSubClasses
GetTradeSkillInvSlots  SetTradeSkillSubClassFilter  GetTradeSkillSubClassFilter
SetTradeSkillInvSlotFilter  GetTradeSkillInvSlotFilter
CollapseTradeSkillSubClass  ExpandTradeSkillSubClass  GetFirstTradeSkill
GetTradeskillRepeatCount  DoTradeSkill
```

### Quest interaction (NPC quest dialog) `[0x0084E968]` — 31
```
CloseQuest  GetTitleText  GetGreetingText  GetQuestText  GetObjectiveText
GetProgressText  GetRewardText  GetNumAvailableQuests  GetNumActiveQuests
GetAvailableTitle  GetActiveTitle  GetAvailableLevel  GetActiveLevel
SelectAvailableQuest  SelectActiveQuest  AcceptQuest  DeclineQuest
IsQuestCompletable  CompleteQuest  GetQuestReward  GetRewardMoney
GetRewardSpell  GetQuestMoneyToGet  GetNumQuestRewards  GetNumQuestChoices
GetNumQuestItems  GetQuestItemInfo  GetQuestItemLink  QuestChooseRewardError
ConfirmAcceptQuest  GetQuestBackgroundMaterial
```

### Camera `[0x0084F7A0]` — 21
```
CameraZoomIn  CameraZoomOut  MoveViewInStart  MoveViewInStop
MoveViewOutStart  MoveViewOutStop  MoveViewLeftStart  MoveViewLeftStop
MoveViewRightStart  MoveViewRightStop  MoveViewUpStart  MoveViewUpStop
MoveViewDownStart  MoveViewDownStop  ToggleMouseMove  SetView  SaveView
ResetView  NextView  PrevView  FlipCameraYaw
```

### Movement & mouselook `[0x008500B8]` — 26
```
Jump  ToggleRun  ToggleAutoRun  MoveForwardStart  MoveForwardStop
MoveBackwardStart  MoveBackwardStop  TurnLeftStart  TurnLeftStop
TurnRightStart  TurnRightStop  StrafeLeftStart  StrafeLeftStop
StrafeRightStart  StrafeRightStop  PitchUpStart  PitchUpStop  PitchDownStart
PitchDownStop  TurnOrActionStart  TurnOrActionStop  CameraOrSelectOrMoveStart
CameraOrSelectOrMoveStop  MouselookStart  MouselookStop  IsMouselooking
```

### Time / file I/O `[0x008503F4]` — 6
```
GetTime  GetGameTime  ConsoleExec  ReadFile  DeleteFile  AppendToFile
```

### Unit & PvP `[0x00850438]` — 83
```
UnitExists  UnitIsVisible  UnitIsUnit  UnitIsPlayer  UnitIsCorpse
UnitIsPartyLeader  UnitInParty  UnitPlayerOrPetInParty  UnitInRaid
UnitPlayerOrPetInRaid  UnitPlayerControlled  UnitIsPVP  UnitIsPVPFreeForAll
UnitFactionGroup  UnitReaction  UnitIsEnemy  UnitIsFriend  UnitCanCooperate
UnitCanAssist  UnitCanAttack  UnitIsCharmed  UnitIsPlusMob
UnitClassification  UnitName  UnitPVPName  UnitXP  UnitXPMax  UnitHealth
UnitHealthMax  UnitMana  UnitManaMax  UnitPowerType  UnitOnTaxi  UnitIsDead
UnitIsGhost  UnitIsDeadOrGhost  UnitIsConnected  UnitAffectingCombat
UnitSex  UnitLevel  GetMoney  UnitRace  UnitClass  UnitResistance  UnitStat
UnitAttackBothHands  UnitDamage  UnitRangedDamage  UnitRangedAttack
UnitAttackSpeed  UnitAttackPower  UnitRangedAttackPower  UnitDefense
UnitArmor  UnitCharacterPoints  UnitBuff  UnitDebuff  UnitIsTapped
UnitIsTappedByPlayer  UnitIsTrivial  UnitIsCivilian  UnitHasRelicSlot
SetPortraitTexture  HasFullControl  GetComboPoints  IsInGuild  IsGuildLeader
IsResting  GetDodgeChance  GetBlockChance  GetParryChance  UnitCreatureType
UnitCreatureFamily  GetResSicknessDuration  GetPVPSessionStats
GetPVPYesterdayStats  GetPVPThisWeekStats  GetPVPLastWeekStats
GetPVPLifetimeStats  UnitPVPRank  GetPVPRankInfo  GetPVPRankProgress
GetInspectPVPRankProgress
```

### Friends / Who / Ignore `[0x0085D830]` — 19
```
GetNumFriends  GetFriendInfo  SetSelectedFriend  GetSelectedFriend
AddFriend  RemoveFriend  ShowFriends  GetNumIgnores  GetIgnoreName
SetSelectedIgnore  GetSelectedIgnore  AddOrDelIgnore  AddIgnore  DelIgnore
SendWho  GetNumWhoResults  GetWhoInfo  SetWhoToUI  SortWho
```

### Spell targeting `[0x0086F950]` — 5
```
SpellIsTargeting  SpellCanTargetUnit  SpellTargetUnit  SpellStopTargeting
SpellStopCasting
```

### Frame creation (FrameScript core) `[0x00872E74]` — 4
```
GetNumFrames  EnumerateFrames  CreateFont  CreateFrame
```

### Big "miscellaneous" batch `[0x0083DE68]` — 216

This is the firehose — everything else FrameXML touches. Registered by the
main `FUN_LOAD_SCRIPT_FUNCTIONS` loop at `0x00490282`.

```
FrameXML_Debug  GetBuildInfo  ReloadUI  RegisterForSave  SetLayoutMode
IsShiftKeyDown  IsControlKeyDown  IsAltKeyDown  SetConsoleKey  Screenshot
GetFramerate  TogglePerformanceDisplay  TogglePerformanceValues
ResetPerformanceValues  GetDebugStats  RegisterCVar  GetCVar  SetCVar
GetCVarDefault  GetWorldDetail  SetWorldDetail  GetWaterDetail  SetWaterDetail
GetFarclip  SetFarclip  GetTerrainMip  SetTerrainMip  GetDoodadAnim
SetDoodadAnim  GetTexLodBias  SetTexLodBias  SetBaseMip  GetBaseMip
GetGamma  SetGamma  ToggleTris  TogglePortals  ToggleCollision
ToggleCollisionDisplay  TogglePlayerBounds  Stuck  Logout  Quit
ShowNameplates  HideNameplates  ShowFriendNameplates  HideFriendNameplates
SetCursor  ClearCursor  CursorHasItem  CursorHasSpell  CursorHasMoney
EquipCursorItem  DeleteCursorItem  EquipPendingItem  CancelPendingEquip
TargetUnit  TargetNearestEnemy  TargetNearestFriend  TargetNearestPartyMember
TargetNearestRaidMember  TargetLastTarget  TargetLastEnemy  AttackTarget
AssistUnit  AssistByName  TargetByName  FollowUnit  FollowByName
ClearTarget  AutoEquipCursorItem  ToggleSheath  GetZoneText  GetRealZoneText
GetSubZoneText  GetMinimapZoneText  InitiateTrade  CanInspect  NotifyInspect
InviteToParty  InviteByName  UninviteFromParty  UninviteFromRaid
UninviteByName  PromoteToPartyLeader  PromoteByName  RequestTimePlayed
RepopMe  AcceptResurrect  DeclineResurrect  ResurrectHasSickness
ResurrectHasTimer  BeginTrade  CancelTrade  AcceptGroup  DeclineGroup
AcceptGuild  DeclineGuild  CancelLogout  ForceLogout  ForceQuit
GetCursorMoney  DropCursorMoney  PickupPlayerMoney  ShowInspectCursor
ResetCursor  HasSoulstone  UseSoulstone  HasKey  GuildInviteByName
GuildUninviteByName  GuildPromoteByName  GuildDemoteByName
GuildSetLeaderByName  GuildSetMOTD  GuildLeave  GuildDisband  GuildInfo
GetScreenWidth  GetScreenHeight  GetDamageBonusStat  GetReleaseTimeRemaining
GetCorpseRecoveryDelay  GetInstanceBootTimeRemaining  GetSummonConfirmTimeLeft
GetSummonConfirmSummoner  GetSummonConfirmAreaName  ConfirmSummon
GetCursorPosition  GetNetStats  SitOrStand  StopCinematic  RunScript
CheckInteractDistance  GetScreenResolutions  GetCurrentResolution
SetScreenResolution  GetRefreshRates  SetupFullscreenScale
GetMultisampleFormats  GetCurrentMultisampleFormat  SetMultisampleFormat
RandomRoll  OpeningCinematic  InCinematic  IsWindowsClient  IsMacClient
IsLinuxClient  GetGMTicket  NewGMTicket  UpdateGMTicket  DeleteGMTicket
GMSurveyQuestion  GMSurveyAnswerSubmit  GMSurveyCommentSubmit  GMSurveySubmit
GetGMStatus  AcceptXPLoss  CheckSpiritHealerDist  CheckTalentMasterDist
CheckPetUntrainerDist  CheckBinderDist  RetrieveCorpse  BindEnchant
ReplaceEnchant  ReplaceTradeEnchant  NotWhileDeadError  GetRestState
GetXPExhaustion  GetTimeToWellRested  GMRequestPlayerInfo  GetCoinIcon
GetZonePVPInfo  TogglePVP  ConfirmBindOnUse  SetPortraitToTexture  GetLocale
GetGMTicketCategories  DropItemOnUnit  RestartGx  RestoreVideoDefaults
GetBindLocation  GetVideoCaps  ConfirmTalentWipe  ConfirmPetUnlearn
ConfirmBinder  ShowingHelm  ShowingCloak  ShowHelm  ShowCloak
SetEuropeanNumbers  GetAreaSpiritHealerTime  AcceptAreaSpiritHeal
CancelAreaSpiritHeal  GetMouseFocus  GetRealmName  GetItemQualityColor
GetItemInfo  GetNumAddOns  GetAddOnInfo  GetAddOnMetadata  GetAddOnDependencies
EnableAddOn  EnableAllAddOns  DisableAddOn  DisableAllAddOns
ResetDisabledAddOns  IsAddOnLoadOnDemand  IsAddOnLoaded  LoadAddOn
PartialPlayTime  NoPlayTime  GetBillingTimeRested  ResetInstances
CanShowResetInstances  IsInInstance
```

## Glue (login screen) global functions

These are only registered while the game is at the login/character-select
screen. Addons running in-world cannot reach them. Listed for completeness.

### Glue main `[0x008373B8]` — 60
```
GetBuildInfo  GetLocale  GetSavedAccountName  SetSavedAccountName
SetCurrentScreen  QuitGame  PlayGlueMusic  PlayCreditsMusic  StopGlueMusic
GetMovieResolution  GetScreenWidth  GetScreenHeight  LaunchURL
ShowTOSNotice  TOSAccepted  AcceptTOS  ShowEULANotice  EULAAccepted
AcceptEULA  ShowScanningNotice  ScanningAccepted  AcceptScanning
ShowContestNotice  ContestAccepted  AcceptContest  DefaultServerLogin
StatusDialogClick  GetServerName  DisconnectFromServer  IsConnectedToServer
EnterWorld  Screenshot  PatchDownloadProgress  PatchDownloadCancel
PatchDownloadApply  GetNumAddOns  GetAddOnInfo  LaunchAddOnURL
GetAddOnDependencies  GetAddOnEnableState  EnableAddOn  EnableAllAddOns
DisableAddOn  DisableAllAddOns  SaveAddOns  ResetAddOns
IsAddonVersionCheckEnabled  SetAddonVersionCheck  GetScriptMemory
SetScriptMemory  GetCursorPosition  ShowCursor  HideCursor
SetMovieSubtitles  GetMovieSubtitles  GetBillingTimeRemaining  GetBillingPlan
GetBillingTimeRested  SurveyNotificationDone  PINEntered
```

### Realm list `[0x00837EA0]` — 10
```
RequestRealmList  CancelRealmListQuery  GetNumRealms  GetRealmInfo
ChangeRealm  GetRealmCategories  SetPreferredInfo  SortRealms
GetSelectedCategory  RealmListDialogCancelled
```

### Character creation `[0x008380F8]` — 24
```
SetCharCustomizeFrame  SetCharCustomizeBackground  ResetCharCustomize
GetNameForRace  GetFactionForRace  GetAvailableRaces  GetClassesForRace
GetHairCustomization  GetFacialHairCustomization  GetSelectedRace
GetSelectedSex  GetSelectedClass  SetSelectedRace  SetSelectedSex
SetSelectedClass  UpdateCustomizationBackground  UpdateCustomizationScene
HasCharCustomization  CycleCharCustomization  RandomizeCharCustomization
GetCharacterCreateFacing  SetCharacterCreateFacing  GetRandomName
CreateCharacter
```

### Character select `[0x00838578]` — 11
```
SetCharSelectModelFrame  SetCharSelectBackground  GetCharacterListUpdate
GetNumCharacters  GetCharacterInfo  SelectCharacter  DeleteCharacter
RenameCharacter  UpdateSelectionCustomizationScene  GetCharacterSelectFacing
SetCharacterSelectFacing
```

## Frame types & their methods

23 method tables registered with the runtime dispatcher (iterator at
`0x00701D80`). Identification by method-set is unambiguous; for the four
in-game types registered alongside `LoadScriptFunctions` the type name
literally precedes the table in `.data` and is confirmed.

### `LootButton` — 1 method `[table 0x00847CE4, ctx 0x00B71B64]` ✓
```
SetSlot
```
Confirmed: `"LootButton"` string at `0x00843414` is referenced 4 bytes before the table base.

### `Minimap` — 10 `[table 0x0084C538, ctx 0x00BC81B0]` ✓
```
SetMaskTexture  SetIconTexture  SetBlipTexture  SetArrowModel  SetPlayerModel
GetZoomLevels  GetZoom  SetZoom  PingLocation  GetPingPosition
```
Confirmed: `"Minimap"` string at `0x00843448` is referenced near the table.

### `TabardModel` — 10 `[table 0x0084EE40, ctx 0x00BE089C]` ✓
```
InitializeTabardColors  Save  CycleVariation  GetUpperBackgroundFileName
GetLowerBackgroundFileName  GetUpperEmblemFileName  GetLowerEmblemFileName
GetUpperEmblemTexture  GetLowerEmblemTexture  CanSaveTabardNow
```

### `DressUpModel` — 3 `[table 0x0084F190, ctx 0x00BE0938]` ✓
```
Undress  Dress  TryOn
```

### `PlayerModel` — 3 `[table 0x0084F1FC, ctx 0x00BE09D8]` ✓
```
SetUnit  RefreshUnit  SetRotation
```

### `GameTooltip` — 46 `[table 0x00854198, ctx 0x00C0CF20]` ✓
```
AddFontStrings  SetMinimumWidth  SetPadding  IsOwned  SetOwner  GetAnchorType
ClearLines  AddLine  AddDoubleLine  SetText  AppendText  FadeOut  SetHyperlink
SetAction  SetPetAction  SetShapeshift  SetPlayerBuff  SetTrackingSpell
SetSpell  SetInventoryItem  SetLootItem  SetQuestItem  SetQuestLogItem
SetTrainerService  SetTradeSkillItem  SetCraftItem  SetCraftSpell
SetMerchantItem  SetTradePlayerItem  SetTradeTargetItem  SetBagItem  SetUnit
SetUnitBuff  SetUnitDebuff  SetTalent  SetSendMailItem  SetInboxItem
SetAuctionSellItem  SetAuctionItem  NumLines  SetQuestRewardSpell
SetQuestLogRewardSpell  SetAuctionCompareItem  SetMerchantCompareItem
SetBuybackItem  SetLootRollItem
```

### `Model` — 23 `[table 0x00878948, ctx 0x00CF0C8C]`
```
SetModel  GetModel  ClearModel  SetPosition  SetFacing  SetModelScale
SetSequence  SetSequenceTime  SetCamera  SetLight  GetLight  GetPosition
GetFacing  GetModelScale  AdvanceTime  ReplaceIconTexture  SetFogColor
GetFogColor  SetFogNear  GetFogNear  SetFogFar  GetFogFar  ClearFog
```

### `Frame` — 68 `[table 0x00878EC0, ctx 0x00CF4D38]`
```
GetFrameType  IsFrameType  GetTitleRegion  CreateTitleRegion  CreateTexture
CreateFontString  GetNumRegions  GetRegions  GetNumChildren  GetChildren
GetFrameStrata  SetFrameStrata  GetFrameLevel  SetFrameLevel  HasScript
GetScript  SetScript  RegisterEvent  UnregisterEvent  RegisterAllEvents
UnregisterAllEvents  GetAlpha  SetAlpha  GetEffectiveScale  GetScale  SetScale
GetID  SetID  SetToplevel  IsToplevel  EnableDrawLayer  DisableDrawLayer
Show  Hide  IsVisible  IsShown  Raise  Lower  GetHitRectInsets
SetHitRectInsets  GetMinResize  SetMinResize  GetMaxResize  SetMaxResize
SetMovable  IsMovable  SetResizable  IsResizable  StartMoving  StartSizing
StopMovingOrSizing  SetUserPlaced  IsUserPlaced  SetClampedToScreen
IsClampedToScreen  RegisterForDrag  EnableKeyboard  IsKeyboardEnabled
EnableMouse  IsMouseEnabled  EnableMouseWheel  IsMouseWheelEnabled
GetBackdrop  SetBackdrop  GetBackdropColor  SetBackdropColor
GetBackdropBorderColor  SetBackdropBorderColor
```

### `Button` — 39 `[table 0x00879D00, ctx 0x00CF4E14]`
```
Enable  Disable  IsEnabled  GetButtonState  SetButtonState  SetTextFontObject
GetTextFontObject  SetDisabledFontObject  GetDisabledFontObject
SetHighlightFontObject  GetHighlightFontObject  SetFont  GetFont
SetFontString  GetFontString  SetText  GetText  SetTextColor  GetTextColor
SetDisabledTextColor  GetDisabledTextColor  SetHighlightTextColor
GetHighlightTextColor  SetNormalTexture  GetNormalTexture  SetPushedTexture
GetPushedTexture  SetDisabledTexture  GetDisabledTexture  SetHighlightTexture
GetHighlightTexture  SetPushedTextOffset  GetPushedTextOffset  GetTextWidth
GetTextHeight  RegisterForClicks  Click  LockHighlight  UnlockHighlight
```

### `MovieFrame` — 3 `[table 0x0087AB4C, ctx 0x00CF512C]`
```
StartMovie  StopMovie  EnableSubtitles
```

### `ColorSelect` — 12 `[table 0x0087ABB0, ctx 0x00CF5178]`
```
GetColorWheelTexture  SetColorWheelTexture  GetColorWheelThumbTexture
SetColorWheelThumbTexture  GetColorValueTexture  SetColorValueTexture
GetColorValueThumbTexture  SetColorValueThumbTexture  SetColorHSV  GetColorHSV
SetColorRGB  GetColorRGB
```

### `StatusBar` — 10 `[table 0x0087B010, ctx 0x00CF51CC]`
```
GetOrientation  SetOrientation  GetMinMaxValues  SetMinMaxValues  GetValue
SetValue  GetStatusBarTexture  SetStatusBarTexture  GetStatusBarColor
SetStatusBarColor
```

### `Slider` — 10 `[table 0x0087B260, ctx 0x00CF5208]`
```
GetThumbTexture  SetThumbTexture  GetOrientation  SetOrientation
GetMinMaxValues  SetMinMaxValues  GetValue  SetValue  GetValueStep
SetValueStep
```

### `ScrollFrame` — 9 `[table 0x0087B3C0, ctx 0x00CF5250]`
```
SetScrollChild  GetScrollChild  SetHorizontalScroll  SetVerticalScroll
GetHorizontalScroll  GetVerticalScroll  GetHorizontalScrollRange
GetVerticalScrollRange  UpdateScrollChildRect
```

### `ScrollingMessageFrame` — 40 `[table 0x0087B5C0, ctx 0x00CF5294]`
```
SetFontObject  GetFontObject  SetFont  GetFont  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV  AddMessage
ScrollUp  ScrollDown  PageUp  PageDown  ScrollToTop  ScrollToBottom
SetScrollFromBottom  AtTop  AtBottom  UpdateColorByID  GetNumMessages
GetNumLinesDisplayed  GetCurrentScroll  GetCurrentLine  GetMaxLines
SetMaxLines  SetFading  GetFading  SetTimeVisible  GetTimeVisible
SetFadeDuration  GetFadeDuration  Clear
```

### `MessageFrame` — 26 `[table 0x0087B960, ctx 0x00CF52DC]`
```
SetFontObject  GetFontObject  SetFont  GetFont  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV  SetInsertMode
GetInsertMode  SetFading  GetFading  SetTimeVisible  GetTimeVisible
SetFadeDuration  GetFadeDuration  AddMessage  Clear
```

### `SimpleHTML` — 19 `[table 0x0087BA80, ctx 0x00CF5328]`
```
SetFontObject  GetFontObject  SetFont  GetFont  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV  SetText
SetHyperlinkFormat  GetHyperlinkFormat
```

### `EditBox` — 48 `[table 0x0087BB68, ctx 0x00CF5378]`
```
SetFontObject  GetFontObject  SetFont  GetFont  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV  SetAutoFocus
IsAutoFocus  SetMultiLine  IsMultiLine  SetNumeric  IsNumeric  SetPassword
IsPassword  SetBlinkSpeed  GetBlinkSpeed  Insert  SetText  GetText  SetNumber
GetNumber  HighlightText  AddHistoryLine  SetTextInsets  GetTextInsets
SetFocus  ClearFocus  SetMaxBytes  GetMaxBytes  SetMaxLetters  GetMaxLetters
GetNumLetters  GetHistoryLines  SetHistoryLines  GetInputLanguage
ToggleInputLanguage  SetAltArrowKeyMode  GetAltArrowKeyMode
```

### `CheckButton` — 6 `[table 0x0087BF74, ctx 0x00CF53B4]`
```
SetChecked  GetChecked  GetCheckedTexture  SetCheckedTexture
GetDisabledCheckedTexture  SetDisabledCheckedTexture
```

### `Texture` — 22 `[table 0x0087C128, ctx 0x00CF5434]`
```
GetDrawLayer  SetDrawLayer  GetBlendMode  SetBlendMode  GetVertexColor
SetVertexColor  SetGradient  SetGradientAlpha  SetAlpha  GetAlpha  Show  Hide
IsVisible  IsShown  GetTexture  SetTexture  GetTexCoord  SetTexCoord
SetTexCoordModifiesRect  GetTexCoordModifiesRect  SetDesaturated  IsDesaturated
```

### `FontString` — 32 `[table 0x0087C1D8, ctx 0x00CF5400]`
```
GetDrawLayer  SetDrawLayer  SetVertexColor  GetAlpha  SetAlpha  SetAlphaGradient
Show  Hide  IsVisible  IsShown  GetFontObject  SetFontObject  GetFont  SetFont
GetText  SetText  GetTextColor  SetTextColor  GetShadowColor  SetShadowColor
GetShadowOffset  SetShadowOffset  GetSpacing  SetSpacing  SetTextHeight
GetStringWidth  GetJustifyH  SetJustifyH  GetJustifyV  SetJustifyV
SetNonSpaceWrap  CanNonSpaceWrap
```

### `Font` (FontInstance) — 22 `[table 0x0087C7C8, ctx 0x00CF546C]`
```
GetObjectType  IsObjectType  GetName  SetFontObject  GetFontObject
CopyFontObject  SetFont  GetFont  SetAlpha  GetAlpha  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV
```

### `Region` (LayoutFrame base) — 19 `[table 0x0087C9B8, ctx 0x00CF54B4]`
```
GetObjectType  IsObjectType  GetName  GetParent  SetParent  GetCenter  GetLeft
GetRight  GetTop  GetBottom  GetWidth  SetWidth  GetHeight  SetHeight
GetNumPoints  GetPoint  SetPoint  SetAllPoints  ClearAllPoints
```

## Inheritance

Each method table is a flat namespace; the dispatcher walks a per-type
context. Vanilla still has a class hierarchy though, so a `:GetWidth()` call
on a `Frame` resolves through `Region`. Educated reading of the contexts:

```
Region                                ; core layout (GetWidth, SetPoint, ...)
├── Frame                             ; events, scripts, scale
│   ├── Button
│   │   └── CheckButton
│   ├── EditBox
│   ├── Slider
│   ├── StatusBar
│   ├── ScrollFrame
│   ├── ScrollingMessageFrame
│   ├── MessageFrame
│   ├── SimpleHTML
│   ├── ColorSelect
│   ├── MovieFrame
│   ├── Model
│   │   ├── PlayerModel
│   │   ├── DressUpModel
│   │   └── TabardModel
│   ├── Minimap
│   ├── GameTooltip
│   └── LootButton                    ; (probably extends Button)
├── LayeredRegion (visual)            ; not visible as own table here
│   ├── Texture
│   └── FontString
└── FontInstance                      ; the FontString font config

WorldFrame                            ; type name exists, no methods of its own
```

The exact parent linkage between contexts isn't proven from this scan —
verifying would require disassembling the dispatcher at `0x005368D0`.

## Rough numbers

| Surface           | Tables | Entries |
|-------------------|--------|---------|
| Globals (in-game) | 47     | 1029    |
| Globals (glue)    | 5      | 109     |
| Frame methods     | 23     | 481     |
| **Total**         | **75** | **1619** |

(54 register-call sites and 1153 entries in the binary, minus the 2 / 15
Turtle additions.)

## How this was extracted

Methodology, for whoever has to redo this when Turtle ships another build:

1. Locate `FrameScript_RegisterFunction` (`0x00704120`) — verified by reading the
   prologue and finding the LUA_GLOBALSINDEX (`-10001 = 0xFFFFD8EF`) constant
   in a tail call.
2. Scan all of `.text` for `E8` calls to that VA. Every hit is a registration
   site. (54 in this build.)
3. For each call site, recognise the canonical loop pattern (`56 33 F6 8B 96 …
   8B 8E … E8 … 83 C6 08 [81|83] FE … 72 …`) and extract the table base from
   `mov ecx, [esi+disp32]` and the row count from the `cmp` immediate. Both
   8-bit and 32-bit `cmp` forms exist; handle both.
4. Walk each table reading 8-byte `{name, func}` records.
5. For the iterator at `0x00701D80`, do the same: scan `.text` for callers
   (23 sites), extract `ecx` (table), `edx` (count), and the pushed context.
6. Frame-type identification: confirmed where a `char *typeName` pointer lives
   in `.data` immediately before the method table. Otherwise inferred from
   the method set, which is unambiguous for every standard widget.

Raw extracted dumps are kept alongside this doc as `raw_globals.txt` and
`raw_methods.txt` for cross-checking VAs.
