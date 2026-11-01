-- Profiler widget: Lua memory, garbage collection, frame rate and the addons
-- that make the most garbage and use the most CPU.
local _, ns = ...

local LINE_HEIGHT = 14
local stats, gc = ns.stats, ns.gc

local widget = CreateFrame("Button", "ClassicAPIProfilerWidget", UIParent)
ns.widget = widget
widget:Hide()
widget:SetPoint("CENTER", 0, 0)
widget:SetWidth(240)
widget:SetMovable(true)
widget:SetClampedToScreen(true)
widget:EnableMouse(true)
widget:RegisterForDrag("LeftButton")
widget:RegisterForClicks("LeftButtonUp", "RightButtonUp")
ns.CreateBackdrop(widget)

local lines = {}
local function AddLine(label)
  local y = -4 - #lines * LINE_HEIGHT

  local left = widget:CreateFontString(nil, "LOW", "GameFontWhite")
  left:SetPoint("TOPLEFT", 5, y)
  left:SetText(ns.ACCENT .. label)

  local right = widget:CreateFontString(nil, "LOW", "GameFontWhite")
  right:SetPoint("TOPRIGHT", -5, y)
  right:SetJustifyH("RIGHT")

  tinsert(lines, right)
  return right
end

local memory = AddLine("Memory")
local rate = AddLine("Rate")
local cleanup = AddLine("Last Cleanup")
local framerate = AddLine("Framerate")
local cpu = AddLine("Lua CPU")
local topMem = AddLine("Top Alloc")
local topCpu = AddLine("Top CPU")

-- the largest allocation in a tenth of a second, draining over 5 seconds
local DRAIN = .1 / 5
local bar = CreateFrame("StatusBar", nil, widget)
bar:SetColorFill(1, .3, .3)
bar:SetPoint("BOTTOMLEFT", 1, 1)
bar:SetPoint("BOTTOMRIGHT", -1, 1)
bar:SetHeight(18)
bar:SetMinMaxValues(0, 1)
bar:SetValue(0)

local barText = bar:CreateFontString(nil, "OVERLAY", "GameFontWhite")
barText:SetPoint("LEFT", 4, 0)

widget:SetHeight(#lines * LINE_HEIGHT + 8 + 20)

-- lastMem is taken on the first tick while shown, so the growth while hidden
-- or while the other addons load never counts as a peak
local lastMem, peakElapsed = nil, 0
widget:SetScript("OnShow", function() ns.StartSampling() end)
widget:SetScript("OnHide", function() lastMem = nil end)
widget:SetScript("OnUpdate", function(self, elapsed)
  peakElapsed = peakElapsed + elapsed
  if peakElapsed < .1 then return end
  peakElapsed = 0

  local mem = gcinfo()
  local grown = lastMem and mem - lastMem or 0
  lastMem = mem

  local value, peak = bar:GetValue(), select(2, bar:GetMinMaxValues())
  if grown > value then
    bar:SetMinMaxValues(0, grown)
    bar:SetValue(grown)
    barText:SetText(ns.ACCENT .. "Last Peak (100ms):|r " .. grown .. ns.GREY .. " KB")
  else
    bar:SetValue(max(value - peak * DRAIN, 0))
  end
  bar:SetColorFill(1, .3, .3, min(bar:GetValue() / 10, 1))
end)

local function Unit(value, unit)
  return ns.WHITE .. value .. " " .. ns.GREY .. unit
end

ns.OnSample(function()
  if not widget:IsShown() then return end

  memory:SetText(ns.WHITE .. ns.KB(stats.mem) .. ns.GREY .. " / " .. ns.WHITE .. ns.KB(stats.threshold))

  if stats.collected then
    rate:SetText(ns.ACCENT .. "collected " .. ns.KB(gc.freed or 0))
  elseif stats.memRate then
    rate:SetText(ns.WHITE .. ns.KBRate(stats.memRate))
  end

  if gc.time then
    cleanup:SetText(ns.WHITE .. gc.clock .. ns.GREY .. " (" .. SecondsToTime(GetTime() - gc.time) .. " ago)")
  else
    cleanup:SetText(ns.GREY .. "none yet")
  end

  local fps = GetFramerate()
  framerate:SetText(Unit(format("%.0f", fps), "fps") .. ns.GREY .. " (" .. ns.WHITE .. format("%.1f", 1000 / fps) .. ns.GREY .. " ms)")

  if ns.profiling then
    cpu:SetText(ns.WHITE .. ns.MS(stats.scriptRate) .. ns.GREY .. "/s (" .. ns.WHITE .. format("%.1f%%", stats.scriptRate / 10) .. ns.GREY .. ")")
    topCpu:SetText(stats.topCpu and ns.WHITE .. stats.topCpu.name .. " " .. ns.GREY .. ns.MS(stats.topCpu.cpuRate) .. "/s" or "")
  else
    cpu:SetText(ns.GREY .. "profiling off")
    topCpu:SetText(ns.GREY .. "-")
  end

  local top = stats.topMem
  if top and top.memRate > 0 then
    topMem:SetText(ns.WHITE .. top.name .. " " .. ns.GREY .. ns.KBRate(top.memRate))
  else
    topMem:SetText(ns.GREY .. "-")
  end

  if GameTooltip:IsOwned(widget) then widget:GetScript("OnEnter")(widget) end
end)

ns.OnReset(function()
  bar:SetValue(0)
end)

-- the top `count` loaded addons by `key`, largest first
local function Top(key, count)
  local sorted = {}
  for _, addon in ipairs(stats.list) do
    if (addon[key] or 0) > 0 then tinsert(sorted, addon) end
  end
  table.sort(sorted, function(a, b) return a[key] > b[key] end)
  for i = count + 1, #sorted do sorted[i] = nil end
  return sorted
end

widget:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_BOTTOMLEFT")
  GameTooltip:ClearLines()
  GameTooltip:AddLine(ns.ACCENT .. "Profiler")

  GameTooltip:AddLine(" ")
  GameTooltip:AddLine("Allocated per second", 1, .8, 0)
  for _, addon in ipairs(Top("memRate", 8)) do
    GameTooltip:AddDoubleLine(addon.name, ns.KBRate(addon.memRate), 1, 1, 1, 1, 1, 1)
  end

  if ns.profiling then
    GameTooltip:AddLine(" ")
    GameTooltip:AddLine("CPU per second", 1, .8, 0)
    for _, addon in ipairs(Top("cpuRate", 8)) do
      GameTooltip:AddDoubleLine(addon.name, ns.MS(addon.cpuRate), 1, 1, 1, 1, 1, 1)
    end
  end

  GameTooltip:AddLine(" ")
  GameTooltip:AddLine("Left-click: reset the peak", .6, .6, .6)
  GameTooltip:AddLine("Right-click: open the analyzer", .6, .6, .6)
  GameTooltip:AddLine("Drag: move", .6, .6, .6)
  GameTooltip:Show()
end)

widget:SetScript("OnLeave", function()
  GameTooltip:Hide()
end)

widget:SetScript("OnClick", function(self, button)
  if button == "RightButton" then
    ns.window:SetShown(not ns.window:IsShown())
  else
    bar:SetValue(0)
  end
end)

widget:SetScript("OnDragStart", function(self) self:StartMoving() end)
widget:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
