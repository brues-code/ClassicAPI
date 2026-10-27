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

-- Publishes the standard color globals (NORMAL_FONT_COLOR and friends) as
-- ColorMixin objects, the way Blizzard_SharedXMLBase/Color.lua does.
--
-- Runs at load and again after every addon loads (see Util/AddOnCompat.lua).
-- An addon that carries a copy of Blizzard's own definition -- a plain
-- `NORMAL_FONT_COLOR = {r=1.0, g=0.82, b=0}` -- assigns it over ours at file
-- scope, and every addon that loads after it then finds a color with no
-- methods. A later pass mixes ColorMixin INTO the table that is there instead
-- of replacing it, so the values that addon chose stay, and each reference it
-- gave out gets the methods too.
--
-- Only a color that is absent or has lost its methods is touched. A tag that
-- holds something other than a color table belongs to some other addon, so it
-- is left alone. A `<TAG>_CODE` string that is already set is never rewritten:
-- FrameXML writes those together with the values they go with.
local standardColors

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

-- ITEM_QUALITY_COLORS is the same story with one more level: FrameXML builds
-- each entry as a plain {r, g, b, hex} table, and the `color` object on it is
-- ours. An addon that carries its own copy of the table takes those objects
-- with it, so repair the entries on the same pass. Keys run from -1, so this
-- walks the table with pairs.
local function ApplyItemQualityColors()
    if type(ITEM_QUALITY_COLORS) ~= "table" then return end

    for _, quality in pairs(ITEM_QUALITY_COLORS) do
        if type(quality) == "table" then
            if IsPlainColorTable(quality.color) then
                quality.color = Mixin(quality.color, ColorMixin)
                quality.color.a = quality.color.a or 1
            elseif quality.color == nil and HasRGB(quality) then
                quality.color = CreateColor(quality.r, quality.g, quality.b, 1)
            end
        end
    end
end

function CAPI_ApplyStandardColorGlobals()
    -- `C_UIColor.GetColors()` makes a color object for each of its 187 rows,
    -- so keep the values from the first call. Each later pass is then one
    -- lookup per tag.
    if not standardColors then
        standardColors = {}
        for _, dbColor in ipairs(C_UIColor.GetColors()) do
            table.insert(standardColors, {
                tag = dbColor.baseTag,
                r = dbColor.color.r,
                g = dbColor.color.g,
                b = dbColor.color.b,
                a = dbColor.color.a,
            })
        end
    end

    for _, entry in ipairs(standardColors) do
        local current = _G[entry.tag]
        local color
        if current == nil then
            color = CreateColor(entry.r, entry.g, entry.b, entry.a)
            _G[entry.tag] = color
        elseif IsPlainColorTable(current) then
            color = Mixin(current, ColorMixin)
            color.a = color.a or 1
        end

        if color and _G[entry.tag.."_CODE"] == nil then
            _G[entry.tag.."_CODE"] = color:GenerateHexColorMarkup()
        end
    end

    ApplyItemQualityColors()
end
CAPI_ApplyStandardColorGlobals()
