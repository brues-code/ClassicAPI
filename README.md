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

| Feature | Effect |
|---------|--------|
| Lua 5.1 code | Addon code written for Lua 5.1 runs on the 1.12 Lua 5.0 VM. It covers the [5.1 syntax](docs/API.md#lua-51-syntax) (`#`, `%`, `...` expressions, `0x` hex, `[=[ ]=]` long brackets), [method calls on strings](docs/API.md#string-methods-supper-sformat) (`msg:match("^!(%w+)")`), [up to 60 upvalues](docs/API.md#upvalue-limit) for each function, and [new `for` loop variables](docs/API.md#for-loop-variables-per-pass) on each pass. Each addon file also gets its `(name, table)` through `...`. |
| [Modern script handlers](docs/API.md#setmodernscriptargsenable--getmodernscriptargs) | Frame scripts get their values as arguments, for example `OnClick(self, button, down)` and `OnEvent(self, event, ...)`. [XML script bodies](docs/API.md#xml-script-bodies-see-self-and-their-arguments) such as `<OnLoad>self:Init()</OnLoad>` see `self` and their arguments by name. The `this` / `arg1` globals stay set. `SetModernScriptArgs(false)` restores the vanilla behavior. |
| Inline textures | Draws inline texture markup (`\|T…\|t`) and atlas markup (`\|A…\|a`) as icons in FontStrings, chat, and tooltips. This covers item and spell icons, raid-target markers, and the coin icons in money strings. `GetStringWidth` and `GetStringHeight` count the icons, so measured width and text wrapping stay correct. Done in pure C++ by hooking the engine's text pipeline — no addon. |
| [Soft targeting](docs/API.md#soft-interact-softinteract--anyinteract) | The Interact Key and soft targets. Bind **Interact With Target** to a key to interact with the nearest NPC, corpse, mailbox, chest or gathering node in front of you. The [`softenemy` and `softfriend`](docs/API.md#soft-enemy-and-soft-friend-softenemy--softfriend--anyenemy--anyfriend) tokens point at the nearest enemy and friendly unit without a change of target. With no target, harmful spells go to `softenemy` and helpful spells go to `softfriend`. `PLAYER_SOFT_*_CHANGED` events fire when the units change. |
| [Animations](docs/API.md#animation) | Animation groups on frames, textures and font strings, made in Lua or XML: `CreateAnimationGroup`, looping, smoothing, scripts and templates, with `Alpha`, `Translation`, `Scale`, `Rotation` and `Path` applied. |
| Tooltip line cap | Removes `GameTooltip`'s 30-line limit for every `GameTooltipTemplate` frame (`GameTooltip`, `ShoppingTooltip1/2`, `ItemRefTooltip`, AtlasLoot, …). A tooltip adds a new line pair when it needs one, so long tooltips and comparison blocks (for example, pfUI's eqcompare) keep all their lines. |
| [XML attributes](docs/API.md#parentkey-in-xml) | More XML attributes work: `parentKey="Name"` makes `parent.Name` point to a frame, texture or font string, `<OnLoad function="Name"/>` uses a global function as a script handler, `<OnLoad method="Name"/>` uses the frame's own function, `mixin="A, B"` copies mixins onto the object, `<KeyValues>` sets fields on the object, a button's `<NormalFont style="..."/>` sets its font, and `inherits="A, B"` applies more than one template. `parentArray="Name"` adds the object to a list on its parent. An `<Anchor>` takes `x` and `y` offsets and `relativeKey="$parent.Key"`. `<OnEnter inherit="prepend">` runs the template's handler and then its own, and `inherit="append"` runs them in the other order. |
| [Macro conditions](docs/API.md#more-commands-with-conditions) | `[conditions]` and `@unit` work on 38 slash commands, from `/cast` and `/use` to `/target`, `/follow`, `/cancelaura` and the `/pet` family. With `/cast [@mouseover,harm][] Fireball`, the spell goes to the mouseover unit. With no mouseover unit, it goes to your target. `@cursor` places a ground-target spell or item under the mouse and `@player` places it at your feet. A character name works where a unit token does (`[target=Feral]`). Addons read the same rules through `SecureCmdOptionParse`. |
| [`#showtooltip`](docs/API.md#showtooltip-and-show) | A macro button shows the spell or item that its directive resolves to, conditions included: icon, tooltip, cooldown, range, usable state and auto-repeat glow. Action-bar addons get this with no code of their own. Addons with their own macro parser can publish through `C_Macro.SetMacroDisplay`. |
| [Event-driven nameplates](docs/API.md#nameplate) | The modern `C_NamePlate` API, driven by real events. `NAME_PLATE_UNIT_ADDED` / `NAME_PLATE_UNIT_REMOVED` fire as plates appear and vanish, `nameplate1`..`nameplateN` tokens resolve with every `UnitX` function (and fire `UNIT_HEALTH`, `UNIT_AURA`, … as `arg1 == "nameplateN"`), and `C_NamePlate.GetNamePlateForUnit` / `GetNamePlates` hand back the live frames. |
| [Focus target](docs/API.md#focus) | A sticky focus unit. `FocusUnit("target")` pins it and `ClearFocus()` drops it, with `PLAYER_FOCUS_CHANGED` on every change. The `focus` / `focustarget` tokens resolve with every `UnitX` function and fire unit events (`UNIT_HEALTH`, `UNIT_AURA`, … as `arg1 == "focus"`), and the predefined `FOCUSTARGET` / `TARGETFOCUS` keybinds are ready to set. |
| [Combat log events](docs/API.md#combat_log_event_unfiltered--combat_log_event-events) | `COMBAT_LOG_EVENT_UNFILTERED` and `CombatLogGetCurrentEventInfo` give each combat event with GUIDs and `COMBATLOG_OBJECT_*` flags. The values do not depend on the client language, so damage meters do not need to parse chat text. `COMBAT_LOG_EVENT` fires for the events that pass the `CombatLogAddFilter` filters. |
| [Retail-like `/reload`](docs/API.md#reload-picks-up-new-addons-and-new-files) | `/reload` picks up addon changes made while the game runs. A new folder under `Interface\AddOns\` registers and loads as a normal addon, new files added to an existing addon's TOC load, a newly installed addon's first SavedVariables save survives `/reload`, `##` metadata edits (`## SavedVariables:`, `## Dependencies:`, `## Title:`, …) take effect, and a deleted addon folder drops from the addon list. |
| [Multi-flavor & conditional TOC](docs/API.md#conditional-and-multi-flavor-toc-loading) | Loads modern multi-flavor addons that ship one folder. Selects a version-specific TOC (`<Name>_ClassicAPI.toc` or `<Name>_Turtle.toc`) and the matching keybinding file (`Bindings_ClassicAPI.xml` / `Bindings_Turtle.xml`), accepts a comma-separated `## Interface:` version list (compatible when it includes the client version `11200`), and honors per-line `[AllowLoadGameType]` / `[AllowLoadTextLocale]` conditions and `[Family]` / `[Game]` / `[TextLocale]` path variables inside a TOC. |
| [Launch switches](docs/API.md#launch) | Options you add to the shortcut that starts the game. `-config <name>` reads and writes `WTF\<name>` in place of `WTF\Config.wtf`, so one install can hold several settings profiles. `-gluescript` and `-gluescriptFile` run Lua at the login and character-select screens, every time those screens appear. `-gamescript` and `-gamescriptFile` run it one time, after you enter the world. Each script runs after the interface has loaded, so it can call interface functions and use frames. |
| [Addon memory and CPU usage](docs/API.md#profiling) | Addon managers and performance tools can show how much Lua memory and CPU time each addon uses. `GetAddOnMemoryUsage` gives the memory of each addon. When `scriptProfile` is `1`, the CPU functions give the time of each addon, function, frame, and event. With profiling off, the CPU functions cost nothing. |
| [Any texture size](docs/API.md#texture-size-and-shape) | Textures load at any size and any shape. Width and height do not need to be powers of two, and there is no fixed upper limit. The only limit is the maximum texture size of your graphics card. This applies to every texture path: `SetTexture`, sprite sheets, inline textures, tooltips, and masks. The client allocates memory for a large texture only when an addon loads one, so small textures cost nothing extra. |

Smaller behavior fixes, such as frame alpha and `SetParent` frame levels, are
in **[docs/API.md](docs/API.md)** with the API that they change.

## Full API reference

Everything ClassicAPI adds, in full. Each section is collapsed — click a
heading to expand it. For signatures and return values, see the per-function
reference in **[docs/API.md](docs/API.md)**.

<details>
<summary><b>In-game Lua calls</b> — 550+ functions across ~60 namespaces</summary>

In the Frame rows, a name that starts with `:` is a method of the widget that the row names, for example `:SetShown` is `frame:SetShown`.

| Namespace | Calls |
|-----------|-------|
| [Action](docs/API.md#action) | `GetActionInfo`, `IsItemAction`, `HasBonusActionBar` |
| [AddOns](docs/API.md#addons) | `C_AddOns.DoesAddOnExist`, `C_AddOns.GetAddOnInterfaceVersion`, `C_AddOns.GetAddOnLocalTable`, `C_AddOns.GetAddOnName`, `C_AddOns.GetAddOnNotes`, `C_AddOns.GetAddOnOptionalDependencies`, `C_AddOns.GetAddOnSecurity`, `C_AddOns.GetAddOnTitle`, `C_AddOns.IsAddOnLoadable`, `C_AddOns.IsAddOnLoaded`, `C_AddOns.LoadAddOn` |
| [APIDocumentation](docs/API.md#apidocumentation) | `/classicapi` in-game browser, `C_APIDocumentation.GetSystem`, `C_APIDocumentation.GetSystems` |
| [Animation](docs/API.md#animation) | `region:CreateAnimationGroup`, `region:GetAnimationGroups`, `region:StopAnimating`, `SetMaxAnimFramerate`, `GetMaxAnimFramerate`, `AnimationGroup` and `Animation` / `Alpha` / `Translation` / `Scale` / `Rotation` / `Path` / `ControlPoint` methods |
| [AuctionHouse](docs/API.md#auctionhouse) | `C_AuctionHouse.PostItem`, `CanCancelAuction` |
| [Bindings](docs/API.md#bindings) | `SetBindingSpell`, `SetBindingItem`, `SetBindingMacro`, `SetBindingClick`, `SetOverrideBinding`, `SetOverrideBindingSpell`, `SetOverrideBindingItem`, `SetOverrideBindingMacro`, `SetOverrideBindingClick`, `ClearOverrideBindings`, `GetBindingAction` (`checkOverride`), `GetBindingByKey`, `IsModifiedClick`, `GetModifiedClick`, `SetModifiedClick`, `GetNumModifiedClickActions`, `GetModifiedClickAction` |
| [Chat](docs/API.md#chat) | `GetCurrentChatEvent`, `GetCurrentChatGUID`, `IsChatAFK`, `IsChatDND`, `SendSystemMessage` |
| [ChatBubbles](docs/API.md#chatbubbles) | `C_ChatBubbles.GetAllChatBubbles` |
| [Class](docs/API.md#class) | `FillLocalizedClassList` |
| [ClassColor](docs/API.md#classcolor) | `C_ClassColor.GetClassColor` |
| [ColorUtil](docs/API.md#colorutil) | `C_ColorUtil.ConvertRGBToHSV`, `C_ColorUtil.ConvertHSVToRGB`, `C_ColorUtil.ConvertHSVToHSL`, `C_ColorUtil.ConvertHSLToHSV`, `C_ColorUtil.ConvertHSLToRGB`, `C_ColorUtil.GenerateTextColorCode`, `C_ColorUtil.WrapTextInColor`, `C_ColorUtil.WrapTextInColorCode` |
| [Combat](docs/API.md#combat) | `InCombatLockdown`, `StartAttack`, `StopAttack` |
| [CombatLog](docs/API.md#combatlog) | `CombatLogGetCurrentEventInfo`, `COMBATLOG_OBJECT_*` (constants) |
| [Console](docs/API.md#console) | `CalculateStringEditDistance`, `ConsoleAddMessage`, `ConsoleEcho`, `ConsoleExec`, `ConsoleGetAllCommands`, `ConsoleGetColorFromType`, `ConsoleGetFontHeight`, `ConsoleIsActive`, `ConsolePrintAllMatchingCommands`, `SetConsoleKey` |
| [CVar](docs/API.md#cvar) | `C_CVar.AreCVarsLoaded`, `C_CVar.DoesCVarExist`, `C_CVar.GetCVarBitfield`, `C_CVar.GetCVarBool`, `C_CVar.GetCVarInfo`, `C_CVar.RemoveTempCVar`, `C_CVar.SetCVar`, `C_CVar.SetCVarBitfield`, `C_CVar.SetTempCVar`, `GetCVar`, `GetCVarDefault` |
| [Cursor](docs/API.md#cursor) | `CursorHasMacro`, `GetCursorInfo` |
| [Container](docs/API.md#container) | `C_Container.AutoStoreItem`, `C_Container.CalculateTotalNumberOfFreeBagSlots`, `C_Container.GetContainerItemCharges`, `C_Container.GetContainerItemDurability`, `C_Container.GetContainerItemID`, `C_Container.GetContainerItemInfo`, `C_Container.GetContainerItemEquipmentSetInfo`, `C_Container.GetContainerItemQuestInfo`, `C_Container.GetContainerItemRepairCost`, `C_Container.GetContainerFreeSlots`, `C_Container.GetContainerNumFreeSlots`, `C_Container.GetBackpackAutosortDisabled`, `C_Container.GetBankAutosortDisabled`, `C_Container.GetItemCooldown`, `C_Container.GetSortBagsRightToLeft`, `C_Container.HasContainerItem`, `C_Container.IsContainerItemOpenable`, `C_Container.MoveItem`, `C_Container.PlayerHasHearthstone`, `C_Container.SetBackpackAutosortDisabled`, `C_Container.SetBagPortraitTexture`, `C_Container.SetBankAutosortDisabled`, `C_Container.SetSortBagsRightToLeft`, `C_Container.SortBags`, `C_Container.SortBankBags`, `C_Container.SwapItems`, `C_Container.UseHearthstone`, `GetItemCooldown`, `SetBagPortraitTexture` |
| [Creature](docs/API.md#creature) | `C_CreatureInfo.GetCreatureID`, `C_CreatureInfo.GetCreatureInfoByID`, `C_CreatureInfo.RequestLoadCreatureByID`, `C_CreatureInfo.GetRaceInfo`, `C_CreatureInfo.GetClassInfo`, `C_CreatureInfo.GetCreatureFamilyInfo`, `C_CreatureInfo.GetCreatureFamilyIDs`, `C_CreatureInfo.GetFactionInfo`, `C_CreatureInfo.GetCreatureTypeInfo`, `C_CreatureInfo.GetCreatureTypeIDs` |
| [Currency](docs/API.md#currency) | `GetCoinText`, `C_CurrencyInfo.GetCoinText`, `GetCoinTextureString`, `C_CurrencyInfo.GetCoinTextureString` |
| EncodingUtil | `C_EncodingUtil.CompressString`, `C_EncodingUtil.DecompressString`, `C_EncodingUtil.EncodeBase64`, `C_EncodingUtil.DecodeBase64`, `C_EncodingUtil.EncodeHex`, `C_EncodingUtil.DecodeHex`, `C_EncodingUtil.SerializeJSON`, `C_EncodingUtil.DeserializeJSON`, `C_EncodingUtil.SerializeCBOR`, `C_EncodingUtil.DeserializeCBOR` |
| [EquipmentSet](docs/API.md#equipmentset) | `C_EquipmentSet.CanUseEquipmentSets`, `C_EquipmentSet.ClearIgnoredSlotsForSave`, `C_EquipmentSet.CreateEquipmentSet`, `C_EquipmentSet.DeleteEquipmentSet`, `C_EquipmentSet.EquipmentSetContainsLockedItems`, `C_EquipmentSet.GetEquipmentSetID`, `C_EquipmentSet.GetEquipmentSetIDs`, `C_EquipmentSet.GetEquipmentSetInfo`, `C_EquipmentSet.GetIgnoredSlots`, `C_EquipmentSet.GetItemIDs`, `C_EquipmentSet.GetItemLocations`, `C_EquipmentSet.GetNumEquipmentSets`, `C_EquipmentSet.IgnoreSlotForSave`, `C_EquipmentSet.IsSlotIgnoredForSave`, `C_EquipmentSet.ModifyEquipmentSet`, `C_EquipmentSet.PickupEquipmentSet`, `C_EquipmentSet.SaveEquipmentSet`, `C_EquipmentSet.UnignoreSlotForSave`, `C_EquipmentSet.UseEquipmentSet` |
| [Events](docs/API.md#events) | `C_EventUtils.IsEventValid`, `GetFramesRegisteredForEvent` |
| [Expansion](docs/API.md#expansion) | `ClassicExpansionAtLeast`, `ClassicExpansionAtMost`, `GetAccountExpansionLevel`, `GetClassicExpansionLevel`, `GetExpansionLevel`, `GetMaxPlayerLevel` |
| [Faction](docs/API.md#faction) | `C_Reputation.GetFactionDataByID`, `C_Reputation.GetFactionDataByIndex`, `C_Reputation.GetFactionStandings`, `C_Reputation.GetLastStandingChange`, `C_Reputation.GetWatchedFactionData`, `C_Reputation.IsFactionActive`, `C_Reputation.IsFactionActiveByID`, `C_Reputation.IsFactionInactive`, `C_Reputation.SetFactionActive`, `C_Reputation.SetFactionActiveByID`, `C_Reputation.SetFactionInactive`, `C_Reputation.SetFactionInactiveByID`, `C_Reputation.SetSelectedFaction`, `C_Reputation.SetSelectedFactionByID`, `C_Reputation.SetWatchedFactionByID`, `C_Reputation.ToggleFactionAtWar`, `C_Reputation.ToggleFactionAtWarByID`, `GetFactionIDByIndex`, `GetFactionInfoByID`, `GetFactionParentID` |
| [Focus](docs/API.md#focus) | `ClearFocus`, `FocusUnit` |
| [Frame: Region](docs/API.md#regionsetpointpoint-one-argument-form) | `:SetPoint("point")` (one-arg form), `:SetSize` / `:GetSize`, `ignoreRect` on `:GetSize` / `:GetWidth` / `:GetHeight`, `:GetRect`, `:GetScaledRect`, `:IsRectValid`, `:GetPointByName`, `:ClearPoint`, `:GetScale` / `:GetEffectiveScale` (on textures and font strings), `:GetDebugName`, `:IsMouseOver`, `:IsDragging`, `:IsProtected` / `:CanChangeProtectedState` |
| [Frame: Frame](docs/API.md#createframeframetype--name-parent-template-id) | `CreateFrame` template lists and `id`, `GetMouseFoci`, `:SetShown`, `:SetClampRectInsets` / `:GetClampRectInsets`, `:SetResizeBounds`, `:SetClipsChildren` / `:DoesClipChildren`, `:GetBoundsRect`, `:GetEffectiveAlpha`, `:SetAlphaGradient` / `:ClearAlphaGradient` / `:HasAlphaGradient`, `:DesaturateHierarchy`, `:HookScript`, `:IsEventRegistered`, `:RegisterUnitEvent`, `:SetPropagateKeyboardInput` / `:GetPropagateKeyboardInput`, `:SetDontSavePosition` / `:GetDontSavePosition`, `:SetAttribute`, `:SetAttributeNoHandler`, `:ClearAttribute`, `:GetAttribute`, `:CanChangeAttribute`, `:AllowAttributeChanges` |
| [Frame: Button](docs/API.md#buttonsetnormalfontobjectfont--buttongetnormalfontobject) | `:SetEnabled`, `:SetFormattedText`, `:SetNormalFontObject` / `:GetNormalFontObject`, `:SetMotionScriptsWhileDisabled` / `:GetMotionScriptsWhileDisabled`, `:RegisterForClicks("AnyUp"/"AnyDown")`, `:RegisterForMouse`, `GetClickFrame` |
| [Frame: FontString](docs/API.md#fontstringgetstringheight) | `:GetStringHeight`, `:GetUnboundedStringWidth`, `:GetWrappedWidth`, `:GetNumLines`, `:GetLineHeight`, `:GetFontHeight` / `:SetFontHeight` (also on fonts), `:GetFieldSize`, `:IsTruncated`, `:SetMaxLines` / `:GetMaxLines`, `:SetWordWrap` / `:CanWordWrap`, `:SetIndentedWordWrap` / `:GetIndentedWordWrap` (also on fonts, edit boxes, message frames, and SimpleHTML), `:SetFormattedText`, `:SetRotation` / `:GetRotation` |
| [Frame: Texture](docs/API.md#texturesetrotationangle--cx-cy) | `:SetRotation` / `:GetRotation`, `:SetVertexOffset` / `:GetVertexOffset`, `:SetColorTexture`, `:SetAtlas` / `:GetAtlas`, `:ResetTexCoord`, `:SetSpriteSheetCell`, `:SetDesaturation` / `:GetDesaturation`, `IsDesaturateSupported`, `:SetMask`, `frame:CreateMaskTexture`, `:AddMaskTexture` / `:RemoveMaskTexture`, `:GetNumMaskTextures`, `:GetMaskTexture` |
| [Frame: EditBox](docs/API.md#editboxsetcursorpositionposition) | `:SetCursorPosition` / `:GetCursorPosition`, `:GetUTF8CursorPosition`, `:ClearHighlightText`, `:SetHighlightColor` / `:GetHighlightColor`, `:HasFocus`, `GetCurrentKeyBoardFocus`, `:HasText`, `:ClearHistory`, `:Enable` / `:Disable` / `:IsEnabled`, `:SetCountInvisibleLetters` / `:IsCountInvisibleLetters`, `:IsInIMECompositionMode` |
| [Frame: StatusBar](docs/API.md#statusbarsetreversefillisreversefill--statusbargetreversefill) | `:SetReverseFill` / `:GetReverseFill`, `:SetRotatesTexture` / `:GetRotatesTexture`, `:SetFillStyle` / `:GetFillStyle`, `:SetColorFill`, `:SetStatusBarDesaturated` / `:IsStatusBarDesaturated`, `:SetStatusBarDesaturation` / `:GetStatusBarDesaturation` |
| [Frame: Slider](docs/API.md#sliderenable--sliderdisable--sliderisenabled) | `:Enable` / `:Disable` / `:IsEnabled` |
| [Frame: Cooldown](docs/API.md#createframecooldown--name-parent-template) | `CreateFrame("Cooldown")`, `:SetCooldown`, `:SetCooldownDuration`, `:SetCooldownFromExpirationTime`, `:Clear`, `:GetCooldownTimes`, `:GetCooldownDuration`, `:GetCooldownDisplayDuration`, `:Pause` / `:Resume`, `:SetPaused` / `:IsPaused`, `:SetReverse` / `:GetReverse`, `:SetDrawEdge` / `:GetDrawEdge`, `:SetDrawSwipe` / `:GetDrawSwipe`, `:SetDrawBling` / `:GetDrawBling`, `:SetSwipeColor`, `:SetEdgeColor`, `:SetSwipeTexture`, `:SetEdgeTexture`, `:SetBlingTexture`, `:SetRotation` / `:GetRotation`, `:SetHideCountdownNumbers` / `:GetHideCountdownNumbers`, `:GetCountdownFontString`, `:SetCountdownAbbrevThreshold` / `:GetCountdownAbbrevThreshold`, `:SetCountdownMillisecondsThreshold` / `:GetCountdownMillisecondsThreshold`, `:SetMinimumCountdownDuration` / `:GetMinimumCountdownDuration`, `OnCooldownDone`, `CooldownFrame_Set`, `CooldownFrame_Clear`, `CooldownFrame_SetDisplayAsPercentage` |
| [Frame: ScrollingMessageFrame](docs/API.md#smfsethyperlinksenabledenabled--smfgethyperlinksenabled) | `:SetHyperlinksEnabled` / `:GetHyperlinksEnabled` |
| [Frame: scripts](docs/API.md#onenable-and-ondisable-scripts) | `OnEnable`, `OnDisable`, `OnMinMaxChanged`, `OnAttributeChanged`, `OnCharComposition`, `OnTextChanged` (`userInput`), `PreClick`, `PostClick`, `SetModernScriptArgs` / `GetModernScriptArgs` |
| [FriendList](docs/API.md#friendlist) | `AddOrRemoveFriend`, `C_FriendList.GetFriendInfo`, `C_FriendList.GetFriendInfoByIndex`, `C_FriendList.GetNumFriends`, `C_FriendList.GetNumOnlineFriends`, `C_FriendList.GetNumWhoResults`, `C_FriendList.GetWhoInfo`, `C_FriendList.IsFriend`, `C_FriendList.IsIgnored`, `C_FriendList.IsIgnoredByGuid`, `C_FriendList.IsWhoQueryPending`, `C_FriendList.SendWhoQueryByName`, `C_FriendList.SetFriendNotes`, `C_FriendList.SetFriendNotesByIndex` |
| [GameObject](docs/API.md#gameobject) | `C_GameObjectInfo.GetGameObjectInfoByID`, `C_GameObjectInfo.RequestLoadGameObjectByID`, `ClosestGameObjectPosition` |
| [Glue](docs/API.md#glue) | `C_Glue.IsOnGlueScreen` |
| [GameTooltip](docs/API.md#gametooltip) | `GameTooltip:AddSpellByID`, `GameTooltip:AddTexture`, `GameTooltip:GetGameObject`, `GameTooltip:GetItem`, `GameTooltip:GetMinimumWidth`, `GameTooltip:GetOwner`, `GameTooltip:GetPadding`, `GameTooltip:GetSpell`, `GameTooltip:GetUnit`, `GameTooltip:GetUnitGUID`, `GameTooltip:HasGameObject`, `GameTooltip:HasItem`, `GameTooltip:HasSpell`, `GameTooltip:HasUnit`, `GameTooltip:IsEquippedItem`, `GameTooltip:IsUnit`, `GameTooltip:SetAnchorType`, `GameTooltip:SetEquipmentSet`, `GameTooltip:SetFrameStack`, `GameTooltip:SetHyperlink` (full links, `spell:`, `quest:`), `GameTooltip:SetHyperlinkCompareItem`, `GameTooltip:SetInventoryItemByID`, `GameTooltip:SetItemByGUID`, `GameTooltip:SetItemByID`, `GameTooltip:SetSpellByID`, `GameTooltip:SetTalentByID`, `GameTooltip:SetTotem`, `GameTooltip:SetUnitAura`, `OnTooltipSetItem`, `OnTooltipSetSpell`, `OnTooltipSetUnit`, `OnTooltipSetGameObject`, `OnTooltipSetFrameStack` (scripts) |
| [Gossip](docs/API.md#gossip) | `C_GossipInfo.CloseGossip`, `C_GossipInfo.GetActiveQuests`, `C_GossipInfo.GetAvailableQuests`, `C_GossipInfo.GetNumActiveQuests`, `C_GossipInfo.GetNumAvailableQuests`, `C_GossipInfo.GetNumOptions`, `C_GossipInfo.GetOptions`, `C_GossipInfo.GetPoiForUiMapID`, `C_GossipInfo.GetPoiInfo`, `C_GossipInfo.GetText`, `C_GossipInfo.SelectActiveQuest`, `C_GossipInfo.SelectAvailableQuest`, `C_GossipInfo.SelectOption`, `C_GossipInfo.SelectOptionByIndex`, `GetNumGossipActiveQuests`, `GetNumGossipAvailableQuests` |
| [Hooks](docs/API.md#hooks) | `hooksecurefunc` |
| [Input](docs/API.md#input) | `GetMouseButtonClicked`, `GetMouseButtonName`, `IsLeftAltKeyDown`, `IsLeftControlKeyDown`, `IsLeftShiftKeyDown`, `IsModifierKeyDown`, `IsMouseButtonDown`, `IsRightAltKeyDown`, `IsRightControlKeyDown`, `IsRightShiftKeyDown` |
| [Instance](docs/API.md#instance) | `GetInstanceInfo`, `IsActiveBattlefieldArena` |
| [Item: C_Item](docs/API.md#item) | `C_Item.DoesItemExist` / `C_Item.DoesItemExistByID`, `C_Item.EquipItemByName`, `C_Item.GetCurrentItemLevel`, `C_Item.GetDetailedItemLevelInfo`, `C_Item.GetEnchantInfo`, `C_Item.GetItemClassInfo`, `C_Item.GetItemCount`, `C_Item.GetItemData` / `C_Item.GetItemDataByID`, `C_Item.GetItemFamily`, `C_Item.GetItemGUID`, `C_Item.GetItemIcon` / `C_Item.GetItemIconByID`, `C_Item.GetItemID`, `C_Item.GetItemInfo`, `C_Item.GetItemInfoInstant`, `C_Item.GetItemInventorySlotInfo`, `C_Item.GetItemInventorySlotKey`, `C_Item.GetItemInventoryType` / `C_Item.GetItemInventoryTypeByID`, `C_Item.GetItemLink`, `C_Item.GetItemLocation`, `C_Item.GetItemMaxStackSize` / `C_Item.GetItemMaxStackSizeByID`, `C_Item.GetItemName` / `C_Item.GetItemNameByID`, `C_Item.GetItemQuality` / `C_Item.GetItemQualityByID`, `C_Item.GetItemSellPrice` / `C_Item.GetItemSellPriceByID`, `C_Item.GetItemSetID` / `C_Item.GetItemSetIDByID`, `C_Item.GetItemSetInfo`, `C_Item.GetItemSpell`, `C_Item.GetItemStatDelta`, `C_Item.GetItemStats`, `C_Item.GetItemSubClassInfo`, `C_Item.GetItemTempEnchantInfo`, `C_Item.GetItemUniqueness` / `C_Item.GetItemUniquenessByID`, `C_Item.GetStackCount`, `C_Item.GetWeaponEnchantInfo`, `C_Item.IsBound`, `C_Item.IsConsumableItem`, `C_Item.IsEquippableItem`, `C_Item.IsEquippedItem`, `C_Item.IsItemDataCached` / `C_Item.IsItemDataCachedByID`, `C_Item.IsItemGUIDInInventory`, `C_Item.IsItemInRange`, `C_Item.IsItemOpenable`, `C_Item.IsLocked`, `C_Item.IsUsableItem`, `C_Item.ItemHasRange`, `C_Item.LockItem`, `C_Item.LockItemByGUID`, `C_Item.UnlockItem`, `C_Item.UnlockAllItems`, `C_Item.PickupItem`, `C_Item.RequestLoadItemData` / `C_Item.RequestLoadItemDataByID`, `C_Item.UseAtCursor`, `C_Item.UseAtUnit`, `C_Item.UseItemByName` |
| [Item: info](docs/API.md#getitemclassinfoclassid) | `GetItemClassInfo`, `GetItemSubClassInfo`, `GetItemIcon`, `IsConsumableItem`, `IsCurrentItem`, `IsDressableItem`, `IsEquippedItemType`, `IsHarmfulItem`, `IsHelpfulItem`, `IsUsableItem` |
| [Item: inventory](docs/API.md#getaverageitemlevel) | `GetAverageItemLevel`, `GetInventoryItemID`, `GetInventoryItemDurability`, `GetInventoryItemRepairCost`, `GetInventoryItemsForSlot`, `IsDualWielding`, `SetInventoryPortraitTexture` |
| [Item: item IDs](docs/API.md#getitemid--companions-to-the-engines-getitemlink-family) | `GetAuctionItemID`, `GetAuctionSellItemID`, `GetCraftReagentItemID`, `GetInboxItemID`, `GetLootRollItemID`, `GetLootSlotItemID`, `GetMerchantItemID`, `GetQuestItemID`, `GetQuestLogItemID`, `GetTradePlayerItemID`, `GetTradeTargetItemID`, `GetTradeSkillItemID`, `GetTradeSkillReagentItemID` |
| [Loot](docs/API.md#loot) | `C_Loot.GetNearbyLootableUnits`, `C_Loot.GetLastScanResults`, `C_Loot.IsScanInProgress`, `C_Loot.LootAllCorpses`, `C_Loot.LootUnit`, `C_Loot.LootUnitItem`, `C_Loot.ScanNearbyLoot` |
| [LootHistory](docs/API.md#loothistory) | `C_LootHistory.GetNumItems`, `C_LootHistory.GetItem`, `C_LootHistory.GetPlayerInfo`, `C_LootHistory.Clear` |
| [LossOfControl](docs/API.md#lossofcontrol) | `C_LossOfControl.GetActiveLossOfControlData`, `C_LossOfControl.GetActiveLossOfControlDataCount`, `C_LossOfControl.GetSchoolLockout` |
| [Lua](docs/API.md#lua) | `collectgarbage` (5.1 options), `coroutine.create`, `coroutine.resume`, `coroutine.running`, `coroutine.status`, `coroutine.wrap`, `coroutine.yield`, `CreateFromMixins`, `math.fmod`, `math.huge`, `math.modf`, `Mixin`, `select`, `strcmputf8i`, `strconcat`, `string.concat`, `string.gmatch`, `string.gsub` (table replacement), `string.join`, `string.match`, `string.reverse`, `string.split`, `string.trim`, `strjoin`, `strlenutf8`, `strmatch`, `strreplace`, `strrev`, `strsplit`, `strsplittable`, `strtrim`, `table.count`, `table.maxn`, `table.wipe`, `unpack` (range args), `xpcall` (argument forwarding) |
| [Macros](docs/API.md#macros) | `C_Macro.CreateMacro`, `C_Macro.EditMacro`, `C_Macro.SetMacroDisplay`, `DeleteMacro` / `EditMacro` / `GetMacroInfo` / `PickupMacro` by name, `GetLooseMacroIcons`, `GetLooseMacroItemIcons`, `GetMacroBody`, `GetMacroIcons`, `GetMacroItem`, `GetMacroItemIcons`, `GetMacroSpell`, `GetRunningMacro`, `GetRunningMacroButton`, `RunMacroText`, `SetMacroItem`, `SetMacroSpell`, `StopMacro` |
| [Mail](docs/API.md#mail) | `GetInboxItemLink`, `GetSendMailItemLink` |
| [Map](docs/API.md#map) | `C_Map.CanSetUserWaypointOnMap`, `C_Map.ClearUserWaypoint`, `C_Map.GetAreaInfo`, `C_Map.GetAreas`, `C_Map.GetAreaTriggerInfo`, `C_Map.GetAreaTriggers`, `C_Map.GetBestMapForUnit`, `C_Map.GetFallbackWorldMapID`, `C_Map.GetMapAreaIDs`, `C_Map.GetMapArtLayers`, `C_Map.GetMapArtLayerTextures`, `C_Map.GetMapChildrenInfo`, `C_Map.GetMapInfo`, `C_Map.GetMapInfoAtPosition`, `C_Map.GetMapOverlays`, `C_Map.GetMapPosFromWorldPos`, `C_Map.GetMapRectOnMap`, `C_Map.GetMapWorldSize`, `C_Map.GetPlayerMapPosition`, `C_Map.GetUserWaypoint`, `C_Map.GetUserWaypointFromHyperlink`, `C_Map.GetUserWaypointHyperlink`, `C_Map.GetUserWaypointPositionForMap`, `C_Map.GetWorldPosFromMapPos`, `C_Map.HasUserWaypoint`, `C_Map.MapHasArt`, `C_Map.SetUserWaypoint` |
| [MapExplorationInfo](docs/API.md#mapexplorationinfo) | `C_MapExplorationInfo.GetExploredAreaIDsAtPosition`, `C_MapExplorationInfo.GetExploredMapTextures`, `C_MapExplorationInfo.GetUnexploredMapTextures` |
| [MerchantFrame](docs/API.md#merchantframe) | `C_MerchantFrame.GetBuybackItemID`, `C_MerchantFrame.GetItemInfo`, `C_MerchantFrame.GetNumJunkItems`, `C_MerchantFrame.IsMerchantItemRefundable`, `C_MerchantFrame.IsSellAllJunkEnabled`, `C_MerchantFrame.SellAllJunkItems`, `GetBuybackItemLink` |
| [Model](docs/API.md#model) | `DressUpModel:TryOn` (links, recipes, names), `DressUpModel:UndressSlot`, `Model:SetGlow`, `PlayerModel:RefreshCamera`, `PlayerModel:SetCamDistanceScale`, `PlayerModel:SetCreature`, `PlayerModel:SetDisplayInfo`, `PlayerModel:SetPortraitZoom`, `PlayerModel:SetRotation` (`animate`) |
| [NamePlate](docs/API.md#nameplate) | `C_NamePlate.GetNamePlateForGUID`, `C_NamePlate.GetNamePlateForUnit`, `C_NamePlate.GetNamePlateGUIDs`, `C_NamePlate.GetNamePlates` |
| [NameCache](docs/API.md#namecache) | `C_PlayerCache.GetPlayerInfoByName`, `C_PlayerCache.IsEnabled`, `C_PlayerCache.RememberPlayer`, `C_PlayerCache.SetEnabled`, `GetPlayerInfoByGUID`, `UnitNameFromGUID` |
| [NewItems](docs/API.md#newitems) | `C_NewItems.ClearAll`, `C_NewItems.IsNewItem`, `C_NewItems.RemoveNewItem` |
| [PartyInfo](docs/API.md#partyinfo) | `InviteUnit`, `PromoteToLeader`, `UninviteUnit` |
| [Pet](docs/API.md#pet) | `IsPetAttackAction`, `IsAtStableMaster` |
| [PlayerInfo](docs/API.md#playerinfo) | `C_PlayerInfo.CanUseItem`, `C_PlayerInfo.GetClass`, `C_PlayerInfo.GetName`, `C_PlayerInfo.GetRace`, `C_PlayerInfo.GetSex`, `C_PlayerInfo.GUIDIsCreature`, `C_PlayerInfo.GUIDIsGameObject`, `C_PlayerInfo.GUIDIsPet`, `C_PlayerInfo.GUIDIsPlayer`, `C_PlayerInfo.IsConnected` |
| [Profiling](docs/API.md#profiling) | `GetAddOnCPUUsage`, `GetAddOnMemoryUsage`, `GetEventCPUUsage`, `GetFrameCPUUsage`, `GetFunctionCPUUsage`, `GetScriptCPUUsage`, `ResetCPUUsage`, `UpdateAddOnCPUUsage`, `UpdateAddOnMemoryUsage` |
| [Quest](docs/API.md#quest) | `C_QuestLog.GetInfo`, `C_QuestLog.GetNumQuestObjectives`, `C_QuestLog.GetQuestDetails`, `C_QuestLog.GetQuestLogTitle`, `C_QuestLog.GetHeaderIndexForQuest`, `C_QuestLog.GetLogIndexForQuestID`, `C_QuestLog.GetQuestIDForLogIndex`, `C_QuestLog.GetTitleForQuestID`, `C_QuestLog.IsOnQuest`, `C_QuestLog.IsQuestDataCachedByID`, `C_QuestLog.IsUnitOnQuest`, `C_QuestLog.RequestLoadQuestByID`, `GetQuestLogCompletionText`, `GetQuestLogLeaderBoardID` |
| [Sound](docs/API.md#sound) | `C_Sound.GetRecentSoundFiles`, `C_Sound.GetSoundScaledVolume`, `C_Sound.IsPlaying`, `C_Sound.PlayItemSound`, `C_Sound.PlaySound`, `C_Sound.PlaySoundWithOptions`, `C_Sound.PlayVocalErrorSound`, `MuteSoundFile`, `PlaySound`, `PlaySoundFile`, `StopSound`, `UnmuteSoundFile` |
| [Spell](docs/API.md#spell) | `C_Spell.CancelSpellByID`, `C_Spell.CastAtCursor`, `C_Spell.CastAtUnit`, `C_Spell.CastingInfo`, `C_Spell.ChannelInfo`, `C_Spell.DoesSpellExist`, `C_Spell.GetSchoolString`, `C_Spell.GetSpellCastCount`, `C_Spell.GetSpellCooldown`, `C_Spell.GetSpellDescription`, `C_Spell.GetSpellDispelType`, `C_Spell.GetSpellEffectInfo`, `C_Spell.GetSpellEffectMechanics`, `C_Spell.GetSpellInfo`, `C_Spell.GetSpellLevelInfo`, `C_Spell.GetSpellLink`, `C_Spell.GetSpellLossOfControlCooldown`, `C_Spell.GetSpellMechanicByID`, `C_Spell.GetSpellName`, `C_Spell.GetSpellPowerCost`, `C_Spell.GetSpellRadius`, `C_Spell.GetSpellReagents`, `C_Spell.GetSpellRequiredTargetLevel`, `C_Spell.GetSpellSubtext`, `C_Spell.GetSpellTexture`, `C_Spell.IsAutoAttackSpell`, `C_Spell.IsCurrentSpell`, `C_Spell.IsNextMeleeSpell`, `C_Spell.IsRangedAutoAttackSpell`, `C_Spell.IsSelfBuff`, `C_Spell.IsSpellHarmful`, `C_Spell.IsSpellHelpful`, `C_Spell.IsSpellInRange`, `C_Spell.IsSpellPassive`, `C_Spell.IsSpellUsable`, `C_Spell.ResetsMeleeSwing`, `C_Spell.SpellHasRange`, `C_Spell.UnitCastingInfo`, `C_Spell.UnitChannelInfo`, `CanDualWield`, `CancelSpellByName`, `CancelUnitBuff`, `CastSpellByID`, `CastSpellNoToggle`, `GetCraftSpellID`, `GetCritChance`, `GetRangedCritChance`, `GetSpellBaseCooldown`, `GetSpellBonusDamage`, `GetSpellBonusHealing`, `GetSpellInfo`, `GetSpellLink`, `GetSpellRadius`, `GetSpellRequiredTargetLevel`, `GetSpellSchool`, `IsAttackSpell`, `IsAutoRepeatSpell`, `IsConsumableSpell`, `IsHarmfulSpell`, `IsHelpfulSpell`, `IsPassiveSpell`, `IsPlayerSpell`, `IsSpellKnown`, `IsUsableSpell`, `SpellHasRange` |
| [SpellBook](docs/API.md#spellbook) | `C_SpellBook.ContainsAnyDisenchantSpell`, `C_SpellBook.GetCurrentLevelSpells`, `C_SpellBook.GetNumSpellBookSkillLines`, `C_SpellBook.GetPlayerSpellsByAura`, `C_SpellBook.GetSkillLineIndexByID`, `C_SpellBook.GetSkillLineName`, `C_SpellBook.GetSkillLineRank`, `C_SpellBook.GetSpellBookItemCastCount`, `C_SpellBook.GetSpellBookItemInfo`, `C_SpellBook.GetSpellBookItemLossOfControlCooldownDuration`, `C_SpellBook.GetSpellBookItemLossOfControlCooldownInfo`, `C_SpellBook.GetSpellBookItemSkillLineIndex`, `C_SpellBook.GetSpellBookSkillLineInfo`, `C_SpellBook.GetSpellLevelLearned`, `C_SpellBook.GetSpellSkillLine`, `C_SpellBook.IsAutoAttackSpellBookItem`, `C_SpellBook.IsClassTalentSpellBookItem`, `C_SpellBook.IsRangedAutoAttackSpellBookItem`, `FindSpellBookSlotByID` |
| [State](docs/API.md#state) | `CancelShapeshiftForm`, `Dismount`, `GetMirrorTimerInfo`, `GetMirrorTimerProgress`, `GetShapeshiftForm`, `GetShapeshiftFormID`, `GetSheathState`, `IsAssistingRitual`, `IsFalling`, `IsIndoors`, `IsInGroup`, `IsInRaid`, `IsLoggedIn`, `IsLoggingOut`, `IsMounted`, `IsOutdoors`, `IsPlayerInWorld`, `IsStealthed`, `IsSwimming` |
| [SwingTimer](docs/API.md#swingtimer) | `C_SwingTimer.EnableRangeCheck`, `C_SwingTimer.IsTargetWithinSwingRange` |
| [System](docs/API.md#system) | `CopyToClipboard`, `FlashClientIcon`, `GetPhysicalScreenSize` |
| [Talent](docs/API.md#talent) | `GetTalentIDByIndex`, `GetTalentSpellID` |
| [Targeting](docs/API.md#targeting) | `GetPlayerFacing`, `TargetDirectionEnemy`, `TargetDirectionFriend`, `TargetDirectionFinished`, `TargetNearest`, `TargetNearestEnemyPlayer`, `TargetNearestFriendPlayer`, `InteractUnit`, `C_PlayerInteractionManager.InteractUnit`, `IsTargetLoose` |
| [TaxiMap](docs/API.md#taximap) | `C_TaxiMap.GetTaxiNodesForMap`, `C_TaxiMap.GetAllTaxiNodes`, `C_TaxiMap.GetTaxiPaths`, `C_TaxiMap.GetTaxiPathWaypoints`, `C_TaxiMap.GetTaxiRoute` |
| [Texture](docs/API.md#texture) | `C_Texture.GetAtlasElementID`, `C_Texture.GetAtlasElements`, `C_Texture.GetAtlasExists`, `C_Texture.GetAtlasID`, `C_Texture.GetAtlasInfo`, `C_Texture.RegisterAtlas` |
| [Time](docs/API.md#time) | `C_DateAndTime.AdjustTimeByDays`, `C_DateAndTime.AdjustTimeByMinutes`, `C_DateAndTime.CompareCalendarTime`, `C_DateAndTime.GetCalendarTimeFromEpoch`, `C_DateAndTime.GetCurrentCalendarTime`, `C_DateAndTime.GetSecondsUntilDailyReset`, `C_DateAndTime.GetServerTimeLocal`, `C_Timer.After`, `C_Timer.NewTicker`, `C_Timer.NewTimer`, `GetServerTime`, `GetTickTime`, `GetTimeCached` |
| [Totem](docs/API.md#totem) | `GetTotemInfo`, `GetTotemTimeLeft`, `GetTotemDuration`, `TargetTotem` |
| [Tracking](docs/API.md#tracking) | `GetNumTrackingTypes`, `GetTrackingInfo`, `SetTracking` |
| [TradeSkillUI](docs/API.md#tradeskillui) | `C_TradeSkillUI.GetTradeSkillListLink`, `C_TradeSkillUI.GetCraftListLink`, `C_TradeSkillUI.GetTradeSkillListRecipes`, `GetTradeSkillRecipeLink`, `IsTradeSkillLinked` |
| [UIColor](docs/API.md#uicolor) | `C_UIColor.GetColors` |
| [Unit](docs/API.md#unit) | `CanBeRaidTarget`, `ClosestUnitPosition`, `GetUnitSpeed`, `IsUnitToken`, `SetUnitCursorTexture`, `UnitClassBase`, `UnitCreatedBySpell`, `UnitCreatureFamilyID`, `UnitCreatureID`, `UnitCreatureTypeID`, `UnitDistanceSquared`, `UnitGUID`, `UnitHealthMissing`, `UnitInLineOfSight`, `UnitInRange`, `UnitIsAFK`, `UnitIsDND`, `UnitIsFeignDeath`, `UnitIsInMyGuild`, `UnitIsMinion`, `UnitIsOtherPlayersPet`, `UnitIsPet`, `UnitIsPossessed`, `UnitOwnerGUID`, `UnitPosition`, `UnitPower`, `UnitPowerMax`, `UnitPowerMissing`, `UnitPowerType`, `UnitRaceBase`, `UnitSelectionColor`, `UnitSelectionType`, `UnitSpellHaste`, `UnitSpellTargetName`, `UnitStandState`, `UnitSubName`, `UnitTokenFromGUID`, `UnitTokenFromName` |
| [UnitAuras](docs/API.md#unitauras) | `C_UnitAuras.GetAuraDataByAuraInstanceID`, `C_UnitAuras.GetAuraDataByIndex`, `C_UnitAuras.GetAuraDataBySlot`, `C_UnitAuras.GetAuraDataBySpellName`, `C_UnitAuras.GetAuraDispelTypeColor`, `C_UnitAuras.GetAuraSlots`, `C_UnitAuras.GetBuffDataByIndex`, `C_UnitAuras.GetDebuffDataByIndex`, `C_UnitAuras.GetPlayerAuraBySpellID`, `C_UnitAuras.GetUnitAuraBySpellID`, `C_UnitAuras.GetUnitAuras`, `C_UnitAuras.RegisterAuraDurationModifierByTrigger`, `C_UnitAuras.RegisterComboDuration`, `C_UnitAuras.UnitAura`, `C_UnitAuras.UnitAuraBySlot`, `C_UnitAuras.UnitBuff`, `C_UnitAuras.UnitDebuff` |
| [VoiceChat](docs/API.md#voicechat) | `C_VoiceChat.GetTtsVoices`, `C_VoiceChat.GetRemoteTtsVoices`, `C_VoiceChat.SpeakText`, `C_VoiceChat.StopSpeakingText`, `C_TTSSettings.GetSpeechRate`, `C_TTSSettings.GetSpeechVolume`, `C_TTSSettings.GetSpeechVoiceID`, `C_TTSSettings.GetVoiceOptionName`, `C_TTSSettings.SetSpeechRate`, `C_TTSSettings.SetSpeechVolume`, `C_TTSSettings.SetVoiceOption`, `C_TTSSettings.SetVoiceOptionByName`, `C_TTSSettings.SetDefaultSettings`, `C_TTSSettings.RefreshVoices` |
| [XMLUtil](docs/API.md#xmlutil) | `C_XMLUtil.DoesTemplateExist`, `C_XMLUtil.GetTemplateInfo`, `C_XMLUtil.GetTemplates` |

</details>

<details>
<summary><b>GlueXML calls</b> — login / realm-select / character-select screens</summary>

Registered on the **glue** Lua state (the engine that runs the login,
realm-select, and character-select screens). The persistence entries
in the first row are *glue-only* — they exist to support GlueXML
patches that need a small persistence surface across sessions.
The rest of the table is in-game calls that we also mirror onto
the glue state because GlueXML had no way to reach them otherwise.

| Group | Calls |
|-------|-------|
| [Account](docs/API.md#account) | `SaveAccount`, `DeleteAccount`, `GetSavedAccounts`, `LoginWithSavedAccount` (passwords encrypted in Windows Credential Manager, scoped per realmlist; plaintext never returned to Lua) |
| [CharacterList](docs/API.md#characterlist) | `GetSavedCharacterOrder`, `SetSavedCharacterOrder` (persist to `WTF\Account\...\ClassicAPI.txt`) |
| CVar | `GetCVar`, `SetCVar`, `RegisterCVar`, `GetCVarDefault`, `C_CVar.GetCVarBool`, `C_CVar.SetTempCVar`, `C_CVar.RemoveTempCVar` (storage is process-global — writes from glue are visible in-world and vice versa) |
| [Glue](docs/API.md#glue) | `C_Glue.IsFirstLoadThisSession`, `C_Glue.IsOnGlueScreen` |
| Script | `RunScript` (compile and run a Lua chunk in the glue state's globals — useful for slash-command-style helpers in GlueXML) |
| State | `IsLoggedIn` |

</details>

<details>
<summary><b>Macros</b> — forms a macro body accepts</summary>

The DLL parses these forms, so an action-bar addon needs no code for them.
See the [Macros section in the Lua reference](docs/API.md#macros) for
details.

| Form | What it does |
|------|--------------|
| `#showtooltip [conditions] Value` | First line of a macro. Sets what the button shows. Bare `#showtooltip` follows the first `/cast` or `/use` line instead. `#show` is the same form. |
| `[conditions]` and `@unit` | Accepted by 42 commands. The slash-command list below names them. |
| `!Name` | Starts an ability but never turns it off. Use it for auto-repeat shots and for the self-buffs (stance, aspect, seal, form, tracking). |
| A line that starts with `#` | A comment. The line never reaches chat. |
| `/cast <spellID>` | For a spell you know, `/cast 5019` casts it by ID. The macro slot also tags correctly for an action-bar addon. |
| `CastSpellByName("<spellID>")` | A numeric string resolves through the engine name resolver, the same as `/cast`. |
| `CastSpellNoToggle("<name>")` in a macro | The engine macro parser reads this as a primary-spell line. The macro slot in an addon such as pfUI then highlights while the spell is on auto-repeat or its self-aura is active. |

</details>

<details>
<summary><b>Slash commands</b> — <code>[conditions]</code> on 42 commands</summary>

The bundled addon registers these commands. Each one takes the same
`[conditions]` and `@unit` syntax as `/cast`. Each client language has its
own command names next to the English ones. See
[More commands with `[conditions]`](docs/API.md#more-commands-with-conditions)
for what each command does, and
[`/castsequence`](docs/API.md#castsequence) and
[`/castrandom`](docs/API.md#castrandom-and-userandom) for the list forms.

| Group | Commands |
|-------|----------|
| Casting | `/cast`, `/use`, `/castsequence`, `/castrandom`, `/userandom`, `/stopcasting`, `/stopmacro`, `/cancelaura`, `/cancelform`, `/dismount` |
| Targeting | `/target`, `/targetexact`, `/cleartarget`, `/targetlasttarget`, `/targetlastenemy`, `/targetenemy`, `/targetfriend`, `/targetenemyplayer`, `/targetfriendplayer`, `/targetparty`, `/targetraid`, `/assist`, `/follow`, `/focus`, `/clearfocus`, `/startattack`, `/stopattack` |
| Equipment | `/equip`, `/equipslot`, `/equipset` |
| Action bars | `/changeactionbar`, `/swapactionbar`, `/click` |
| Pet | `/petattack`, `/petfollow`, `/petstay`, `/petpassive`, `/petdefensive`, `/petaggressive`, `/petautocaston`, `/petautocastoff`, `/petautocasttoggle` |

</details>

<details>
<summary><b>CVars</b> — new settings, set with <code>/console name value</code></summary>

Each setting is saved in `WTF\Config.wtf`. Read and write them with `GetCVar`
/ `SetCVar` or `/console`. Click a group name for the full rules. To change
the most common settings and the player cache in the game, open the game
menu and click AddOn Options, then ClassicAPI.

| Group | CVar | Default | What it does |
|-------|------|---------|--------------|
| [Bindings](docs/API.md#actionbuttonusekeydown) | `ActionButtonUseKeyDown` | `0` | `1` uses an action-bar key binding when you press the key. `0` uses it when you release the key. |
| [Combat](docs/API.md#stopautoattackontargetchange) | `stopAutoAttackOnTargetChange` | `0` | `1` stops melee auto-attack, Auto Shot and wand shots when you change target. |
| [Cast settings](docs/API.md#cast-settings-autostand-autodismount-autounshift) | `autoStand` | `0` | `1` makes you stand up when you cast while you sit. |
| | `autoDismount` | `0` | `1` makes you dismount when you cast while you are mounted. |
| | `autoUnshift` | `0` | `1` makes you leave your form when a spell cannot be cast in it. |
| [Profiling](docs/API.md#scriptprofile) | `scriptProfile` | `0` | `1` turns on CPU profiling for the `*CPUUsage` functions. The change starts after `/reload`. |
| [Soft interact](docs/API.md#soft-interact-softinteract--anyinteract) | `SoftTargetInteract` | `3` | Turns `softinteract` on: `2` keyboard and mouse, `3` always, `0` / `1` off. |
| | `SoftTargetInteractArc` | `0` | Where the object can be: `0` straight ahead of the camera, `1` within 30° of it, `2` any direction. |
| | `SoftTargetInteractRange` | `10` | Maximum distance in yards. |
| | `SoftTargetInteractRangeIsHard` | `0` | `1` always applies the range. `0` also picks an object past it that you can use now. |
| | `SoftTargetIconInteract` | `0` | `1` shows an icon over the soft-interact object. |
| | `SoftTargetIconGameObject` | `0` | `1` also shows that icon over game objects. |
| [Soft enemy / friend](docs/API.md#soft-enemy-and-soft-friend-softenemy--softfriend--anyenemy--anyfriend) | `SoftTargetEnemy` / `SoftTargetFriend` | `1` / `0` | Turns `softenemy` / `softfriend` on: `2` keyboard and mouse, `3` always, `0` / `1` off. |
| | `SoftTargetEnemyArc` / `SoftTargetFriendArc` | `0` | The same values as `SoftTargetInteractArc`. |
| | `SoftTargetEnemyRange` / `SoftTargetFriendRange` | `45` | Maximum distance in yards. |
| | `SoftTargetMatchLocked` | `1` | `1` uses your target as the soft unit when it qualifies. `0` always uses the nearest unit. |
| | `SoftTargetWithLocked` | `1` | `0` turns soft enemy and soft friend off while you have a locked target. |
| | `SoftTargetForce` | `1` | A loose target follows `softenemy` (`1`) or `softfriend` (`2`). `0` keeps it. |
| | `SoftTargetIconEnemy` / `SoftTargetIconFriend` | `0` | `1` shows an icon over `softenemy` / `softfriend`. |
| | `SoftTargetLowPriorityIcons` | `0` | `1` also shows the normal pointer as an icon. |
| | `SoftTargetNameplateSize` | `19` | Size of all soft-target icons. |
| [Cooldown](docs/API.md#createframecooldown--name-parent-template) | `countdownForCooldowns` | `0` | `1` shows the time left as numbers on cooldown frames. |
| [NamePlate](docs/API.md#nameplate) | `nameplateNotSelectedAlpha` | `0.5` | Alpha of a non-target default nameplate, `0` to `1`. |
| | `nameplateSelectedAlpha` | `1` | Alpha of the target's default nameplate, `0` to `1`. |
| [Text to speech](docs/API.md#tts-cvars) | `ttsVoice` | `0` | The voice that `C_VoiceChat.SpeakText` uses. |
| | `ttsSpeed` | `0` | Speech rate, `-10` to `10`. |
| | `ttsVolume` | `100` | Speech volume, `0` to `100`. |

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
| `ExportSoundFiles [subpath]` | Extracts the client's sound files from the MPQs to `BlizzardSound\`; the optional subpath narrows it (the full tree is ~1 GB) |
| `pitchLimit degrees` | Sets how far the camera can tilt up and down (default 89) |
| `AppendLogToFile [filename] [numLines]` | Writes the console output to a text file (default `ConsoleLogs\Log<Timestamp>.txt`) |
| Tab / Ctrl+Tab | Tab completes CVar names after `set`, `cvar_reset` and `cvar_default`; Ctrl+Tab lists every command that contains the typed text |

</details>

<details>
<summary><b>Events</b> — new events fired to addons</summary>

| Event | Payload |
|-------|---------|
| `BAG_NEW_ITEMS_UPDATED` | *(none)* |
| `BAG_UPDATE_DELAYED` | *(none)* |
| `CHAT_MSG_*` | adds `lineID, guid` as `arg11, arg12` after the existing ten ([details](docs/API.md#chat_msg_-arguments-lineid-arg11-and-guid-arg12)) |
| `COMBAT_LOG_EVENT` | `timestamp, subevent, hideCaster, sourceGUID, sourceName, sourceFlags, sourceRaidFlags, destGUID, destName, destFlags, destRaidFlags, ...` ([details](docs/API.md#combat_log_event_unfiltered--combat_log_event-events)) |
| `COMBAT_LOG_EVENT_UNFILTERED` | same as `COMBAT_LOG_EVENT` |
| `CURSOR_CHANGED` | `isDefault, newCursorType, oldCursorType, oldCursorVirtualID` |
| `DYNAMIC_GOSSIP_POI_UPDATED` | *(none)* |
| `EQUIPMENT_SETS_CHANGED` | *(none)* |
| `EQUIPMENT_SWAP_PENDING` | `setID` |
| `EQUIPMENT_SWAP_FINISHED` | `success, setID` |
| `FACTION_STANDING_CHANGED` | `factionID, newStanding, repGained` |
| `GLOBAL_MOUSE_DOWN` | `button` |
| `GLOBAL_MOUSE_UP` | `button` |
| `HEARTHSTONE_BOUND` | *(none)* |
| `ITEM_DATA_LOAD_RESULT` | `itemID, success` |
| `LEARNED_SPELL_IN_SKILL_LINE` | `spellID, skillLineIndex, isGuildPerkSpell` |
| `LOOT_HISTORY_ROLL_CHANGED` | `itemIndex, playerIndex` |
| `LOOT_HISTORY_ROLL_COMPLETE` | `itemIndex` |
| `LOOT_SCAN_COMPLETED` | *(none)* |
| `LOSS_OF_CONTROL_ADDED` | `eventIndex` |
| `LOSS_OF_CONTROL_UPDATE` | `unitToken` (always `"player"`) |
| `MAP_EXPLORATION_UPDATED` | *(none)* |
| `MODIFIER_STATE_CHANGED` | `keyName, down` |
| `NAME_PLATE_CREATED` | `nameplateFrame` |
| `NAME_PLATE_UNIT_ADDED` | `unitToken` ("nameplateN") |
| `NAME_PLATE_UNIT_REMOVED` | `unitToken` ("nameplateN") |
| `PLAYER_AVG_ITEM_LEVEL_READY` | *(none)* |
| `PLAYER_EQUIPMENT_CHANGED` | `equipmentSlot, hasCurrent` |
| `PLAYER_FOCUS_CHANGED` | *(none)* |
| `PLAYER_SOFT_ENEMY_CHANGED` | *(none)* |
| `PLAYER_SOFT_FRIEND_CHANGED` | *(none)* |
| `PLAYER_SOFT_INTERACT_CHANGED` | `oldTarget, newTarget` |
| `PLAYER_STARTED_LOOKING` | *(none)* |
| `PLAYER_STOPPED_LOOKING` | *(none)* |
| `PLAYER_STARTED_MOVING` | *(none)* |
| `PLAYER_STOPPED_MOVING` | *(none)* |
| `PLAYER_STARTED_TURNING` | *(none)* |
| `PLAYER_STOPPED_TURNING` | *(none)* |
| `PLAYER_SWING` | `swingDuration, swingType` |
| `PLAYER_SWING_RANGE_UPDATE` | `swingType, isInRange, checksRange` |
| `PLAYER_TOTEM_UPDATE` | `totemSlot` |
| `QUEST_ACCEPTED` | `questLogIndex, questID` |
| `QUEST_DATA_LOAD_RESULT` | `questID, success` |
| `QUEST_REMOVED` | `questID` |
| `QUEST_TURNED_IN` | `questID, xpReward, moneyReward` |
| `SOUNDKIT_FINISHED` | `soundHandle` (only for sounds played with `runFinishCallback`) |
| `UNIT_AURA_DELTA` | `unitTarget, updateInfo` (the instance IDs of the auras added, updated, and removed) |
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
| `UNIT_SPELLCAST_INTERRUPTIBLE` | `unit` |
| `UNIT_SPELLCAST_NOT_INTERRUPTIBLE` | `unit` |
| `UPDATE_INVENTORY_DURABILITY` | *(none)* |
| `UPDATE_SHAPESHIFT_FORM` | *(none)* |
| `USER_WAYPOINT_UPDATED` | *(none)* |
| `VOICE_CHAT_TTS_PLAYBACK_STARTED` | `numConsumers, utteranceID, durationMS, destination` |
| `VOICE_CHAT_TTS_PLAYBACK_FINISHED` | `numConsumers, utteranceID, destination` |
| `VOICE_CHAT_TTS_PLAYBACK_FAILED` | `status, utteranceID, destination` |
| `VOICE_CHAT_TTS_VOICES_UPDATE` | *(none)* |
| `WEAPON_SLOT_CHANGED` | *(none)* |
| `WEAR_EQUIPMENT_SET` | `setID` |

</details>

<details>
<summary><b>Globals & enums</b></summary>

| Group | Constants |
|-------|-----------|
| Version | `CLASSIC_API_VERSION`, `INTERFACE_VERSION` |
| Expansion | `LE_EXPANSION_LEVEL_CURRENT`, `LE_EXPANSION_CLASSIC` … `LE_EXPANSION_MIDNIGHT` |
| Item quality | `LE_ITEM_QUALITY_POOR` … `LE_ITEM_QUALITY_WOWTOKEN` |
| Unit stat | `LE_UNIT_STAT_STRENGTH` … `LE_UNIT_STAT_SPIRIT` |
| Addon security | `Enum.AddOnSecurityStatus.{Secure,Insecure,Banned,NotAvailable}` |
| Power type | `Enum.PowerType.{HealthCost,None,Mana,Rage,Focus,Energy,Happiness}` |
| Inventory type | `Enum.InventoryType.Index*Type` (0–34, e.g. `IndexHeadType`=1 … `IndexRelicType`=28) |
| Item class | `Enum.ItemClass.{Consumable,Container,Weapon,Gem,Armor,Reagent,Projectile,Tradegoods,ItemEnhancement,Recipe,Quiver,Questitem,Key,Miscellaneous,…}` (0–19) |
| Item quality | `Enum.ItemQuality.{Poor,Common,Uncommon,Rare,Epic,Legendary,Artifact}` (0–6) |
| Cursor type | `Enum.UICursorType.{Default,Item,Money,Spell,PetAction,Merchant,Macro,Pet,…}` (0–20; this client yields 0–5, 7 and 9) |
| Spellbook bank | `Enum.SpellBookSpellBank.{Player,Pet}` (0–1) |
| Spellbook item type | `Enum.SpellBookItemType.{None,Spell,FutureSpell,PetAction,Flyout}` (0–4; 1.12 only yields `Spell`/`PetAction`) |
| Player swing type | `Enum.PlayerSwingType.{MainHand,OffHand,Ranged}` (0–2) |

</details>

<details>
<summary><b>Unit tokens</b> — <code>nameplateN</code>, <code>focus</code>, <code>markN</code>, <code>softinteract</code>, <code>softenemy</code>, <code>softfriend</code>, <code>anyinteract</code>, <code>anyenemy</code>, <code>anyfriend</code></summary>

| Token | Resolves to |
|-------|-------------|
| `nameplate1`..`nameplateN` | Unit behind the Nth visible nameplate, in creation-order. Works with every `UnitX` function — `UnitName`, `UnitGUID`, `UnitClass`, `UnitHealth`, etc. Suffix chains (`nameplate1target`, `nameplate1targettarget`) compose. See [NamePlate / Unit tokens](docs/API.md#unit-tokens-nameplaten). |
| `focus` / `focustarget` | Sticky target set via [`FocusUnit`](docs/API.md#focusunitunit), cleared via [`ClearFocus`](docs/API.md#clearfocus). Same `UnitX` coverage as `nameplateN`. Fires [`PLAYER_FOCUS_CHANGED`](docs/API.md#player_focus_changed-event) on transition. See [Focus](docs/API.md#focus). |
| `mark1`..`mark8` | Unit currently wearing the Nth raid-target marker (`mark1` = star … `mark8` = skull). Same `UnitX` coverage as `nameplateN`, and fires unit events (`UNIT_HEALTH`, `UNIT_AURA`, …) with `arg1 == "markN"`. `UnitExists("markN")` is `false` when the marker is unset, out of range, or on a non-unit. Suffix chains (`mark1target`) compose. See [Unit tokens (`markN`)](docs/API.md#unit-tokens-markn). |
| `softinteract` / `anyinteract` / `anyleftinteract` | The object that the Interact Key uses. `softinteract` is the nearest object in front of you that you can interact with. `anyinteract` is your target when you can interact with it, else `softinteract`. Both can be a game object or a corpse, which only `UnitGUID` reads. See [Soft interact](docs/API.md#soft-interact-softinteract--anyinteract). |
| `softenemy` / `softfriend` / `anyenemy` / `anyfriend` | The nearest enemy and the nearest friendly unit, or your target when it qualifies. `anyenemy` and `anyfriend` use your target first. See [Soft enemy and soft friend](docs/API.md#soft-enemy-and-soft-friend-softenemy--softfriend--anyenemy--anyfriend). |

</details>

<details>
<summary><b>Bindings</b> — direct-action and override binding families</summary>

ClassicAPI backports the direct-action and temporary override
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

The `!!!ClassicAPI` and `ClassicAPI_DebugTools` addons are *inside* the
DLL. You do not download or install them. The `src/embed/AddOns/` folder
in this repository is source code for the DLL. Do not copy it into your
game.

## Bundled addon: !!!ClassicAPI

The Lua-side companion library lives in
[`src/embed/AddOns/!!!ClassicAPI/`](src/embed/AddOns/!!!ClassicAPI/). It's a 1.12.1 /
Lua 5.0 backport of the modern Blizzard helpers that aren't engine
functions but that consumer code still expects to find as globals —
`CallbackRegistryMixin`, `EventRegistry`, `ColorMixin` + `CreateColor`,
`Item` / `ItemLocation`, `MathUtil`(`Lerp` / `Clamp` / `CreateCounter`),
`TableUtil` (`tCompare`, `MergeTable`, `SafePack`, etc.), `EventUtil`
(`ContinueOnAddOnLoaded` and more), `DataProvider`, `CircularBuffer`,
`EasingUtil`, `Interpolator`, `EnumUtil`, `StringUtil`, `AnchorUtil`,
`GridLayoutUtil`, and the layout frame templates (`VerticalLayoutFrame`,
`HorizontalLayoutFrame`, `ResizeLayoutFrame`, `GridLayoutFrame`,
`StaticGridLayoutFrame`). It also holds the conditional slash
commands and `SecureCmdOptionParse`, the parser that reads `[conditions]`
and `@unit` for them.

**You don't have to install the addon manually** — the DLL embeds
the contents of [`src/embed/AddOns/!!!ClassicAPI/`](src/embed/AddOns/!!!ClassicAPI/) and
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

For development, you can put a copy of the addon folder in
`Interface/AddOns/` and add an empty `.classicapi-dev` file to it. The
game then reads the Lua files from that copy, so you can edit them
without a DLL rebuild. Without this file, the game uses the copy with the
newer `## Version:`. The copy is hidden and always enabled, the same as
the embedded addon.

## Debug tools

ClassicAPI contains a backport of Blizzard's `Blizzard_DebugTools` addon,
with the name `ClassicAPI_DebugTools`. The DLL contains this addon, so you do
not install it. The addon is not in the AddOns list, and you cannot disable it.

The slash commands are always available. The tools load the first time
that you use one of these commands:

| Command | Purpose |
|---------|---------|
| `/dump <expr>` | Shows a Lua value in the chat frame. It shows tables and all return values. Use it to examine the return values of `GetSpellInfo`, `C_Item.GetItemInfoInstant`, and other functions. |
| `/etrace` | Opens the event trace window. Other forms: `/etrace start`, `/etrace stop`, `/etrace add EVENT`, and more. |
| `/framestack [showHidden [showRegions]]` (or `/fstack`) | Shows a tooltip with a list of the frames under the cursor. A `-->` mark shows one frame, with its anchors and the file and line that made it. Left Alt and Right Alt move the mark. Ctrl opens the marked frame in the table inspector. Shift shows its texture files. |
| `/tinspect [expr]` (or `/tins`) | Opens a table inspector on the result of `expr`. With no table, it opens on the frame under the cursor. The window shows the anchors and the fields of the table, grouped by type. You can edit numbers, strings and booleans, filter the lines, and open a child table or frame. |
| `/luaerrors` (or `/scripterrors`) | Opens the Lua error window. |

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
