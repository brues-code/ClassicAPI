-- Backport of modern Classic Era's IconDataProvider.lua (lives in
-- Blizzard_FrameXMLBase/Classic/) to vanilla 1.12 / Lua 5.0.
--
-- Wraps the four engine icon-enumeration mutators
-- (`GetLooseMacroIcons` / `GetLooseMacroItemIcons` / `GetMacroIcons`
-- / `GetMacroItemIcons` — all surfaced by ClassicAPI.dll) into a
-- stateful `Spell`/`Item` bucket pair, with an optional
-- "extras" array of icons prepended ahead of the base DB. The two
-- "extra" types are:
--
--   * `Spellbook` — icons of every spell currently in the player's
--     spellbook UI. Used by the modern Macro UI's icon picker so
--     "icons relevant to my class" surface first. In vanilla this
--     misses spells that aren't on the spellbook UI (racials kept
--     off the bar, profession recipes the engine doesn't list as
--     spells, etc.) but covers the common case.
--   * `Equipment` — icons of currently-equipped items (slots 1..19
--     via `GetInventoryItemTexture("player", i)`). Used by the
--     modern Equipment-Manager popup so the picker leads with what
--     the player is wearing.
--
-- Implementation differences from the modern source:
--
--   * `EnumUtil.MakeEnum` is inlined as a plain table.
--   * Modern's `C_SpecializationInfo.GetTalentInfo(query)` (Cata+
--     spec-aware talent API) and `GetPvpTalentInfoByID` (MoP+ PvP
--     talents) are replaced with vanilla's `GetTalentInfo(tab, idx)`
--     across the 3 talent trees. Vanilla has no specs, no dual-spec
--     groups, and no PvP talent system, so those layers are dropped.
--   * `GetSpellBookItemTexture(slot, "player")` is replaced with
--     vanilla's `GetSpellTexture(slot, "spell")`. Flyouts don't
--     exist in vanilla so that branch is dropped entirely.
--   * `extraIconsMap` is a Lua-set keyed by texture path or
--     fileID — `GetKeysArray` flattens it to a numbered array.
--     Same dedup semantics as modern: each texture appears once
--     within the extras section, but no cross-dedup with the
--     base DB walk (the player's equipped weapon icon may still
--     appear twice in the final picker, once in extras and once
--     in `GetMacroItemIcons`).

local QuestionMarkIconPath = "INTERFACE\\ICONS\\INV_MISC_QUESTIONMARK"

local NumActiveIconDataProviders = 0
local BaseIconFilenames = nil

-- Builds the table BaseIconFilenames with engine icon DB.
-- Lazy: shared across all active IconDataProvider instances, so
-- the four engine calls (each of which scans thousands of files)
-- only happens once per picker session.
local function IconDataProvider_RefreshIconTextures()
    if BaseIconFilenames ~= nil then
        return
    end

    BaseIconFilenames = {}
    BaseIconFilenames[IconDataProviderIconType.Spell] = {}
    BaseIconFilenames[IconDataProviderIconType.Item] = {}
    GetLooseMacroIcons(BaseIconFilenames[IconDataProviderIconType.Spell])
    GetLooseMacroItemIcons(BaseIconFilenames[IconDataProviderIconType.Item])
    GetMacroIcons(BaseIconFilenames[IconDataProviderIconType.Spell])
    GetMacroItemIcons(BaseIconFilenames[IconDataProviderIconType.Item])
end

local function IconDataProvider_ClearIconTextures()
    BaseIconFilenames = nil
    collectgarbage()
end

local function IconDataProvider_GetBaseIconTexture(iconType, index)
    local texture = BaseIconFilenames[iconType][index]
    local fileDataID = tonumber(texture)
    if fileDataID ~= nil then
        return fileDataID
    else
        return "INTERFACE\\ICONS\\" .. texture
    end
end

function IconDataProvider_GetAllIconTypes()
    local iconTypeValues = GetValuesArray(IconDataProviderIconType)
    table.sort(iconTypeValues)
    return iconTypeValues
end

IconDataProviderMixin = {}

-- `EnumUtil.MakeEnum("Spell", "Item")` produces `{ Spell = 1, Item = 2 }`.
IconDataProviderIconType = { Spell = 1, Item = 2 }

IconDataProviderExtraType = {
    Spellbook = 1,
    Equipment = 2,
    None = 3,
}

local function FillOutExtraIconsMapWithSpells(extraIconsMap)
    for tab = 1, GetNumSpellTabs() do
        local _, _, offset, numSpells = GetSpellTabInfo(tab)
        offset = offset + 1
        local tabEnd = offset + numSpells
        for slot = offset, tabEnd - 1 do
            local tex = GetSpellTexture(slot, "spell")
            if tex ~= nil and tex ~= "" then
                extraIconsMap[tex] = true
            end
        end
    end
end

-- Walks all three vanilla talent trees, gathering every talent's
-- icon — including talents the player hasn't put points into. The
-- whole tree is "relevant to my class" for picker purposes, not
-- just maxed talents. `GetTalentInfo(tab, idx)` returns
-- `(name, icon, tier, column, rank, maxRank, ...)` — we read the
-- icon (second return) directly.
local function FillOutExtraIconsMapWithTalents(extraIconsMap)
    for tab = 1, GetNumTalentTabs() do
        local numTalents = GetNumTalents(tab)
        for talent = 1, numTalents do
            local _, icon = GetTalentInfo(tab, talent)
            if icon ~= nil and icon ~= "" then
                extraIconsMap[icon] = true
            end
        end
    end
end

local function FillOutExtraIconsMapWithEquipment(extraIconsMap)
    for slot = INVSLOT_FIRST_EQUIPPED, INVSLOT_LAST_EQUIPPED do
        for _, itemLink in pairs(GetInventoryItemsForSlot(slot, {})) do
            local _, _, _, _, itemTexture = C_Item.GetItemInfoInstant(itemLink)
            if itemTexture then
                extraIconsMap[itemTexture] = true
            end
        end
    end
end

function IconDataProviderMixin:Init(extraType, extraIconsOnly, requestedIconTypes)
    self.extraIcons = {}
    self.extraIconType = extraType
    self.requestedIconTypes = requestedIconTypes or IconDataProvider_GetAllIconTypes()

    if extraType == IconDataProviderExtraType.Spellbook then
        local extraIconsMap = {}
        FillOutExtraIconsMapWithSpells(extraIconsMap)
        FillOutExtraIconsMapWithTalents(extraIconsMap)
        self.extraIcons = GetKeysArray(extraIconsMap)
    elseif extraType == IconDataProviderExtraType.Equipment then
        local extraIconsMap = {}
        FillOutExtraIconsMapWithEquipment(extraIconsMap)
        self.extraIcons = GetKeysArray(extraIconsMap)
    end

    if not extraIconsOnly then
        NumActiveIconDataProviders = NumActiveIconDataProviders + 1
        IconDataProvider_RefreshIconTextures()
    end
end

function IconDataProviderMixin:SetIconTypes(iconTypes)
    self.requestedIconTypes = iconTypes or IconDataProvider_GetAllIconTypes()
end

function IconDataProviderMixin:GetNumIcons()
    -- 1 to account for the leading `?` icon.
    local numIcons = 1
    if self:ShouldShowExtraIcons() then
        numIcons = numIcons + table.getn(self.extraIcons)
    end
    if BaseIconFilenames then
        for _, v in pairs(self.requestedIconTypes) do
            numIcons = numIcons + table.getn(BaseIconFilenames[v])
        end
    end
    return numIcons
end

function IconDataProviderMixin:GetIconByIndex(index)
    if index == 1 then
        return QuestionMarkIconPath
    end

    index = index - 1

    local numExtraIcons = (self:ShouldShowExtraIcons() and
                           table.getn(self.extraIcons)) or 0
    if index <= numExtraIcons then
        return self.extraIcons[index]
    end

    local baseIndex = index - numExtraIcons
    local lookupIconType = nil
    -- Each icon type's table is indexed from 1, so loop through the
    -- tables to find which icon type we index to.
    for _, v in pairs(self.requestedIconTypes) do
        local numIconsForType = table.getn(BaseIconFilenames[v])
        if baseIndex <= numIconsForType then
            lookupIconType = v
            break
        end
        baseIndex = baseIndex - numIconsForType
    end

    if lookupIconType then
        return IconDataProvider_GetBaseIconTexture(lookupIconType, baseIndex)
    else
        return nil
    end
end

function IconDataProviderMixin:GetIconForSaving(index)
    local icon = self:GetIconByIndex(index)
    if type(icon) == "string" then
        icon = string.gsub(icon, "INTERFACE\\ICONS\\", "")
    end
    return icon
end

function IconDataProviderMixin:GetIndexOfIcon(icon)
    local numIcons = self:GetNumIcons()
    for i = 1, numIcons do
        if self:GetIconByIndex(i) == icon then
            return i
        end
    end
    return nil
end

function IconDataProviderMixin:ShouldShowExtraIcons()
    return (self.extraIconType == IconDataProviderExtraType.Spellbook and
            tContains(self.requestedIconTypes, IconDataProviderIconType.Spell))
        or (self.extraIconType == IconDataProviderExtraType.Equipment and
            tContains(self.requestedIconTypes, IconDataProviderIconType.Item))
end

function IconDataProviderMixin:Release()
    NumActiveIconDataProviders = NumActiveIconDataProviders - 1
    if NumActiveIconDataProviders <= 0 then
        IconDataProvider_ClearIconTextures()
    end
end
