-- Profiler window: the analyzer. Its tabs share a toolbar (search, deep
-- tracking, reset, copy) and the sortable list below.
local _, ns = ...

local ROW_HEIGHT = 18
local WHITE8X8 = "Interface\\BUTTONS\\WHITE8X8"

ns.search = ""

local window = CreateFrame("Frame", "ClassicAPIProfilerWindow", UIParent)
ns.window = window
window:Hide()
window:SetSize(760, 480)
window:SetPoint("CENTER")
window:SetFrameStrata("DIALOG")
window:SetMovable(true)
window:EnableMouse(true)
window:SetClampedToScreen(true)
window:RegisterForDrag("LeftButton")
window:SetScript("OnDragStart", window.StartMoving)
window:SetScript("OnDragStop", window.StopMovingOrSizing)
ns.CreateBackdrop(window)
tinsert(UISpecialFrames, "ClassicAPIProfilerWindow")

local title = window:CreateFontString(nil, "OVERLAY", "GameFontWhite")
title:SetFont(STANDARD_TEXT_FONT, 14)
title:SetPoint("TOPLEFT", 10, -10)
title:SetText(ns.ACCENT .. "Profiler|r: " .. ns.GOLD .. "Analyzer")

local close = CreateFrame("Button", "ClassicAPIProfilerWindowClose", window, "UIPanelCloseButton")
close:SetSize(20, 20)
close:SetPoint("TOPRIGHT", 0, 0)
close:SetScript("OnClick", function() window:Hide() end)

local status = window:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
status:SetPoint("BOTTOMLEFT", 10, 8)
status:SetPoint("BOTTOMRIGHT", -10, 8)
status:SetJustifyH("LEFT")

-- [[ widgets ]]
local function Tooltip(frame, text)
  frame:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_BOTTOMLEFT")
    GameTooltip:SetText(type(text) == "function" and text() or text, 1, 1, 1, 1, true)
    GameTooltip:Show()
  end)
  frame:SetScript("OnLeave", function() GameTooltip:Hide() end)
end
ns.Tooltip = Tooltip

function ns.CreateButton(name, parent, width, text, onClick)
  local button = CreateFrame("Button", name, parent, "UIPanelButtonTemplate")
  button:SetSize(width, 20)
  button:SetText(text)
  button:SetScript("OnClick", onClick)
  return button
end

-- [[ actions the tabs offer in their context menus ]]
function ns.Copy(text)
  local bytes = CopyToClipboard(text, true)
  ns.Print("Copied " .. bytes .. " bytes to the clipboard.")
end

function ns.Inspect(value)
  DisplayTableInspectorWindow(value)
end

local highlight = CreateFrame("Frame", nil, UIParent)
highlight:SetFrameStrata("TOOLTIP")
highlight:SetBackdrop({ bgFile = WHITE8X8, edgeFile = WHITE8X8, edgeSize = 2 })
highlight:SetBackdropColor(.2, 1, .8, .15)
highlight:SetBackdropBorderColor(.2, 1, .8, 1)
highlight:Hide()

function ns.Highlight(frame)
  if not frame:IsVisible() then
    ns.Print(ns.FrameName(frame) .. " is hidden.")
    return
  end
  highlight:ClearAllPoints()
  highlight:SetAllPoints(frame)
  highlight:Show()
  if highlight.timer then highlight.timer:Cancel() end
  highlight.timer = C_Timer.NewTimer(3, function() highlight:Hide() end)
end

-- [[ list: a sortable, searchable ScrollBox ]]
-- columns = { { title, width, justify, tip, text = fn(entry), value = fn(entry) } }
-- The first column takes the space the others leave. `value` sorts the
-- column and sizes the row bars; it is a number, a lowercase string or nil.
local function LayoutColumns(parent, cells, columns)
  local right = -4
  for i = #columns, 2, -1 do
    cells[i]:SetPoint("RIGHT", parent, "RIGHT", right, 0)
    cells[i]:SetWidth(columns[i].width)
    right = right - columns[i].width - 8
  end
  cells[1]:SetPoint("LEFT", parent, "LEFT", 4, 0)
  cells[1]:SetPoint("RIGHT", parent, "RIGHT", right, 0)
end

-- green to yellow to red, as `perc` goes from 0 to 1
local function Gradient(perc)
  if perc <= .5 then return perc * 2, 1, 0 end
  return 1, 2 - perc * 2, 0
end

local function RowOnEnter(row)
  local tooltip = row.list.def.tooltip
  if not tooltip then return end
  GameTooltip:SetOwner(row, "ANCHOR_RIGHT")
  GameTooltip:ClearLines()
  tooltip(row.entry)
  GameTooltip:Show()
end

local function RowOnLeave()
  GameTooltip:Hide()
end

local function RowOnClick(row, button)
  local def = row.list.def
  if button == "RightButton" and def.menu then
    MenuUtil.CreateContextMenu(row, function(owner, root)
      def.menu(row.entry, root)
    end)
  elseif button == "LeftButton" and def.click then
    def.click(row.entry)
  end
end

local function InitRow(list, row, entry)
  local columns = list.def.columns

  if not row.cells then
    row:EnableMouse(true)
    row:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    row:SetHighlightTexture(WHITE8X8)
    row:GetHighlightTexture():SetVertexColor(1, 1, 1, .1)
    row:SetScript("OnEnter", RowOnEnter)
    row:SetScript("OnLeave", RowOnLeave)
    row:SetScript("OnClick", RowOnClick)

    row.bar = CreateFrame("StatusBar", nil, row)
    row.bar:SetAllPoints()
    row.bar:SetMinMaxValues(0, 1)

    row.cells = {}
    for i, column in ipairs(columns) do
      local cell = row.bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
      cell:SetHeight(ROW_HEIGHT)
      cell:SetWordWrap(false)
      cell:SetJustifyH(column.justify or (i == 1 and "LEFT" or "RIGHT"))
      row.cells[i] = cell
    end
    LayoutColumns(row, row.cells, columns)
  end

  row.list, row.entry = list, entry
  for i, column in ipairs(columns) do
    row.cells[i]:SetText(column.text(entry) or "")
  end

  local value = columns[list.sortColumn].value
  local v = value and value(entry)
  local perc = type(v) == "number" and list.maxValue > 0 and max(v, 0) / list.maxValue or 0
  local r, g, b = Gradient(perc)
  row.bar:SetColorFill(r, g, b, .2)
  row.bar:SetValue(perc)

  if GameTooltip:IsOwned(row) then RowOnEnter(row) end
end

local ListMixin = {}

function ListMixin:UpdateHeader()
  for i, column in ipairs(self.def.columns) do
    local text = column.title
    if i == self.sortColumn then
      text = ns.ACCENT .. text .. (self.sortDesc and " v" or " ^")
    end
    self.headers[i].text:SetText(text)
  end
end

function ListMixin:SortBy(index)
  if self.sortColumn == index then
    self.sortDesc = not self.sortDesc
  else
    self.sortColumn, self.sortDesc = index, true
  end
  self:UpdateHeader()
  self:Refresh()
end

function ListMixin:SetEntries(entries)
  self.entries = entries
  self:Refresh()
end

-- filter by the search text, sort, and show
function ListMixin:Refresh()
  local value = self.def.columns[self.sortColumn].value
  local needle, desc = ns.search, self.sortDesc
  local shown, keys, maxValue = {}, {}, 0

  for _, entry in ipairs(self.entries) do
    if needle == "" or strfind(entry.search, needle, 1, true) then
      tinsert(shown, entry)
      local v = value and value(entry)
      keys[entry] = v
      if type(v) == "number" and v > maxValue then maxValue = v end
    end
  end

  table.sort(shown, function(a, b)
    local va, vb = keys[a], keys[b]
    if va == vb then return a.search < b.search end
    if va == nil then return false end
    if vb == nil then return true end
    if desc then return va > vb end
    return va < vb
  end)

  self.shown, self.maxValue = shown, maxValue
  self.ScrollBox:SetDataProvider(CreateDataProvider(shown), ScrollBoxConstants.RetainScrollPosition)
end

-- the shown rows as plain text
function ListMixin:Export()
  local columns = self.def.columns
  local function Line(texts)
    local parts = {}
    for i, text in ipairs(texts) do
      text = gsub(gsub(text, "|c%x%x%x%x%x%x%x%x", ""), "|r", "")
      parts[i] = format(i == 1 and "%-48s" or "%14s", text)
    end
    return table.concat(parts, " ")
  end

  local titles = {}
  for i, column in ipairs(columns) do titles[i] = column.title end
  local lines = { Line(titles) }

  for _, entry in ipairs(self.shown) do
    local texts = {}
    for i, column in ipairs(columns) do texts[i] = column.text(entry) or "" end
    tinsert(lines, Line(texts))
  end
  return table.concat(lines, "\n")
end

function ns.CreateList(parent, def)
  local list = CreateFrame("Frame", nil, parent, "ClassicAPIProfilerListTemplate")
  Mixin(list, ListMixin)
  list.def = def
  list.entries, list.shown, list.maxValue = {}, {}, 0
  list.sortColumn, list.sortDesc = def.sort or 1, true
  ns.CreateBackdrop(list)

  list.headers = {}
  for i, column in ipairs(def.columns) do
    local header = CreateFrame("Button", nil, list.Header)
    header:SetHeight(18)
    header:EnableMouse(true)
    header.text = header:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    header.text:SetAllPoints()
    header.text:SetJustifyH(column.justify or (i == 1 and "LEFT" or "RIGHT"))
    header:SetScript("OnClick", function() list:SortBy(i) end)
    if column.tip then Tooltip(header, column.tip) end
    list.headers[i] = header
  end
  LayoutColumns(list.Header, list.headers, def.columns)
  list:UpdateHeader()

  local view = CreateScrollBoxListLinearView(0, 0, 0, 0, 1)
  view:SetElementExtent(ROW_HEIGHT)
  view:SetElementInitializer("Button", function(row, entry) InitRow(list, row, entry) end)
  ScrollUtil.InitScrollBoxListWithScrollBar(list.ScrollBox, list.ScrollBar, view)
  return list
end

-- [[ tabs ]]
-- a tab has `frame`, `list`, and optionally OnSelect(), OnSample(dt) and
-- Status() for the line at the bottom
local tabs = {}

local function UpdateStatus()
  local tab = window.active
  status:SetText(tab and tab.Status and tab:Status() or "")
end

function ns.SelectTab(tab)
  for _, other in ipairs(tabs) do
    other.frame:SetShown(other == tab)
    if other == tab then other.button:LockHighlight() else other.button:UnlockHighlight() end
  end
  window.active = tab
  if tab.OnSelect then tab:OnSelect() end
  UpdateStatus()
end

function ns.AddTab(name)
  local tab = { name = name }
  local index = #tabs + 1

  tab.frame = CreateFrame("Frame", nil, window)
  tab.frame:SetPoint("TOPLEFT", 6, -84)
  tab.frame:SetPoint("BOTTOMRIGHT", -6, 24)
  tab.frame:Hide()

  tab.button = ns.CreateButton("ClassicAPIProfilerWindowTab" .. index, window, 90, name, function() ns.SelectTab(tab) end)
  tab.button:SetPoint("TOPLEFT", 8 + (index - 1) * 94, -32)

  tabs[index] = tab
  tabs[name] = tab
  return tab
end

-- show `text` in the search box and switch to tab `name`
function ns.ShowIn(name, text)
  window.search:SetText(text)
  ns.SelectTab(tabs[name])
end

-- [[ toolbar ]]
local search = CreateFrame("EditBox", "ClassicAPIProfilerWindowSearch", window, "InputBoxTemplate")
window.search = search
search:SetSize(200, 20)
search:SetPoint("TOPLEFT", 14, -58)
search:SetAutoFocus(false)
search:SetScript("OnTextChanged", function(self)
  ns.search = strlower(self:GetText())
  self.placeholder:SetShown(self:GetText() == "")
  if window.active then window.active.list:Refresh() end
end)

search.placeholder = search:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
search.placeholder:SetPoint("LEFT", 2, 0)
search.placeholder:SetText("Search name, owner or source")

local copy = ns.CreateButton("ClassicAPIProfilerWindowCopy", window, 70, "Copy", function()
  ns.Copy(window.active.list:Export())
end)
copy:SetPoint("TOPRIGHT", -8, -58)
Tooltip(copy, "Copy the shown rows to the clipboard as plain text.")

local reset = ns.CreateButton("ClassicAPIProfilerWindowReset", window, 70, "Reset", function()
  ns.Reset()
  UpdateStatus()
end)
reset:SetPoint("RIGHT", copy, "LEFT", -4, 0)
Tooltip(reset, "Set every CPU time, call count, allocation and event count to zero.")

local deep = ns.CreateButton("ClassicAPIProfilerWindowDeep", window, 150, "", function(self)
  ns.track.EnableDeep()
  self:SetText("Deep Tracking: " .. ns.ACCENT .. "ON")
  self:Disable()
end)
deep:SetPoint("RIGHT", reset, "LEFT", -4, 0)
deep:SetText("Deep Tracking: " .. ns.GREY .. "OFF")
Tooltip(deep, "Hooks the OnUpdate and OnEvent scripts of every frame to measure the Lua memory each frame allocates, and the CPU time and memory of each event. The hooks never replace a handler, but they add a little time to every script call. They stay until /reload.")

local profiling = ns.CreateButton("ClassicAPIProfilerWindowProfiling", window, 150, "", function()
  ns.SetProfiling(not ns.profiling)
end)
profiling:SetPoint("TOPRIGHT", -24, -32)
profiling:SetText("CPU Profiling: " .. (ns.profiling and ns.ACCENT .. "ON" or ns.GREY .. "OFF"))
Tooltip(profiling, function()
  return (ns.profiling and "Turn CPU profiling off" or "Turn CPU profiling on") ..
    " and reload the UI.\n\nProfiling measures the CPU time of every addon, frame, function and event. It makes each Lua call slower, so turn it off when you are done."
end)

-- [[ sampling while open ]]
ns.OnSample(function(dt)
  if not window:IsShown() then return end
  ns.track.Scan(dt)
  local tab = window.active
  if tab.OnSample then tab:OnSample(dt) end
  UpdateStatus()
end)

window:SetScript("OnShow", function()
  ns.StartSampling()
  ns.track.Start()
  ns.SelectTab(window.active or tabs[1])
end)

window:SetScript("OnHide", function()
  ns.track.Stop()
end)
