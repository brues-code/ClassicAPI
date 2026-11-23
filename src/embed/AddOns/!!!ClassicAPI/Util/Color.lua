ColorMixin = ColorMixin or {}

function CreateColor(r, g, b, a)
    local color = CreateFromMixins(ColorMixin)
    color:OnLoad(r, g, b, a)
    return color
end

function ColorMixin:OnLoad(r, g, b, a)
    self:SetRGBA(r, g, b, a)
end

function ColorMixin:IsRGBEqualTo(otherColor)
    return self.r == otherColor.r
        and self.g == otherColor.g
        and self.b == otherColor.b
end

function ColorMixin:IsEqualTo(otherColor)
    return self:IsRGBEqualTo(otherColor) and self.a == otherColor.a
end

function ColorMixin:GetRGB()
    return self.r, self.g, self.b
end

function ColorMixin:GetHSL()
    local r, g, b, a = self.r, self.g, self.b, self.a
    local h, s, l = C_ColorUtil.ConvertHSVToHSL(C_ColorUtil.ConvertRGBToHSV(r, g, b))
    return h, s, l, a or 1
end

function ColorMixin:GetRGBAsBytes()
    return Round(self.r * 255), Round(self.g * 255), Round(self.b * 255)
end

function ColorMixin:GetRGBA()
    return self.r, self.g, self.b, self.a
end

function ColorMixin:GetRGBAAsBytes()
    return Round(self.r * 255), Round(self.g * 255), Round(self.b * 255), Round((self.a or 1) * 255)
end

function ColorMixin:SetRGBA(r, g, b, a)
    self.r = r
    self.g = g
    self.b = b
    self.a = a or 1
end

function ColorMixin:SetRGB(r, g, b)
    self:SetRGBA(r, g, b, nil)
end

function ColorMixin:GenerateHexColor()
    return C_ColorUtil.GenerateTextColorCode(self)
end

function ColorMixin:GenerateHexColorNoAlpha()
    return string.format("%.2X%.2X%.2X", self:GetRGBAsBytes())
end

function ColorMixin:GenerateHexColorMarkup()
    return "|c"..self:GenerateHexColor()
end

function ColorMixin:WrapTextInColorCode(text)
    return C_ColorUtil.WrapTextInColor(text, self)
end

function WrapTextInColorCode(text, colorHexString)
    return C_ColorUtil.WrapTextInColorCode(text, colorHexString)
end

function WrapTextInColor(text, color)
    return C_ColorUtil.WrapTextInColor(text, color)
end

function ColorMixin:WrapTextInColorTableCode(text)
    return WrapTextInColorCode(text, self:GenerateHexColor())
end

-- The color globals -- the standard ones (NORMAL_FONT_COLOR and friends),
-- ITEM_QUALITY_COLORS and RAID_CLASS_COLORS -- are ColorMixin objects, and
-- readers call methods on them. FrameXML defines some of them as plain tables,
-- and addons that carry a copy of Blizzard's own definitions -- a plain
-- `NORMAL_FONT_COLOR = {r=1.0, g=0.82, b=0}` -- assign those over ours, and
-- the next `:GetRGB()` errors (brues-code/pfUI#56, issue #150).
--
-- The repair mixes ColorMixin INTO the table that is there instead of
-- replacing it, so the values that addon chose stay, and each reference it gave
-- out gets the methods too. Only a color that has lost its methods is touched:
-- a value that is not a color table belongs to some other addon, so it is left
-- alone.
--
-- Everything runs once at load below, and the DLL runs the repair again after
-- each later write (CAPI_RepairColorGlobal).

-- True for a table that carries r/g/b numbers.
local function HasRGB(value)
    return type(value) == "table"
       and type(value.r) == "number"
       and type(value.g) == "number"
       and type(value.b) == "number"
end

-- True for a color table that still needs the mixin.
local function IsPlainColorTable(value)
    return HasRGB(value) and not value.GetRGB
end

-- Gives a plain color table the ColorMixin methods, keeping its values.
local function RepairColor(color)
    if IsPlainColorTable(color) then
        Mixin(color, ColorMixin)
        color.a = color.a or 1
    end
    return color
end

-- Publishes the standard color globals as ColorMixin objects, the way
-- Blizzard_SharedXMLBase/Color.lua does, and repairs the ones FrameXML already
-- defined as plain tables. A `<TAG>_CODE` string that is already set is never
-- rewritten: FrameXML writes those together with the values they go with.
local function PublishStandardColors()
    for _, dbColor in ipairs(C_UIColor.GetColors()) do
        local tag = dbColor.baseTag
        local color = _G[tag]
        if color == nil then
            color = dbColor.color
            _G[tag] = color
        elseif IsPlainColorTable(color) then
            RepairColor(color)
        else
            color = nil
        end

        if color and _G[tag.."_CODE"] == nil then
            _G[tag.."_CODE"] = color:GenerateHexColorMarkup()
        end
    end
end

-- ITEM_QUALITY_COLORS has one more level: FrameXML builds each entry as a
-- plain {r, g, b, hex} table, and the `color` object on it is ours. An addon
-- that carries its own copy of the table takes those objects with it. Keys run
-- from -1, so this walks the table with pairs.
local function ApplyItemQualityColors()
    if type(ITEM_QUALITY_COLORS) ~= "table" then return end

    for _, quality in pairs(ITEM_QUALITY_COLORS) do
        if type(quality) == "table" then
            if IsPlainColorTable(quality.color) then
                RepairColor(quality.color)
            elseif quality.color == nil and HasRGB(quality) then
                quality.color = CreateColor(quality.r, quality.g, quality.b, 1)
            end
        end
    end
end

-- RAID_CLASS_COLORS comes from FrameXML as plain {r, g, b} entries, and every
-- class-color reader we ship (C_ClassColor.GetClassColor, GetClassColorObj, the
-- ported retail damage meter) calls ColorMixin methods on them. Addons replace
-- the whole table or single entries with their own plain copies -- recolored
-- shamans, a CUSTOM_CLASS_COLORS-style palette. A `colorStr` such an addon set
-- is kept, like its values; FrameXML's own entries have none, so they get one
-- here.
local function RepairClassColor(classColor)
    if IsPlainColorTable(classColor) then
        RepairColor(classColor)
        classColor.colorStr = classColor.colorStr or classColor:GenerateHexColor()
    end
    return classColor
end

-- The __index functions installed below, so a later pass does not wrap one
-- twice.
local repairingIndexes = setmetatable({}, { __mode = "k" })

local function ApplyRaidClassColors()
    local classColors = RAID_CLASS_COLORS
    if type(classColors) ~= "table" then return end

    for _, classColor in pairs(classColors) do
        RepairClassColor(classColor)
    end

    -- An __index function that answers for any other key (pfUI hands back grey
    -- for an unknown class) builds a new plain table on every read, so no pass
    -- over the entries can reach those. Repair what it returns instead.
    local metatable = getmetatable(classColors)
    if type(metatable) == "table" and type(metatable.__index) == "function"
            and not repairingIndexes[metatable.__index] then
        local index = metatable.__index
        local repairingIndex = function(t, key)
            return RepairClassColor(index(t, key))
        end
        repairingIndexes[repairingIndex] = true
        metatable.__index = repairingIndex
    end
end

-- The DLL calls this after every assignment to one of the color globals, and
-- to any entry of ITEM_QUALITY_COLORS or RAID_CLASS_COLORS
-- (classes/ColorWatch.cpp), from inside that assignment, so the writer's next
-- statement already reads repaired colors. `name` is the global written to.
function CAPI_RepairColorGlobal(name)
    if name == "RAID_CLASS_COLORS" then
        ApplyRaidClassColors()
    elseif name == "ITEM_QUALITY_COLORS" then
        ApplyItemQualityColors()
    else
        RepairColor(_G[name])
    end
end

-- FrameXML assigned its colors before any of this existed.
PublishStandardColors()
ApplyItemQualityColors()
ApplyRaidClassColors()
