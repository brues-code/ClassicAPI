-- Globals tab: the global tables, functions and frames each addon made, the
-- ClassicAPI functions that something replaced or hooked, and (CPU
-- profiling) the time of every global function, game API included.
local _, ns = ...

-- namespaces whose functions are listed as Name.key
local LIBRARIES = { string = true, table = true, math = true, coroutine = true }

local function Kind(value)
  local t = type(value)
  if t == "table" and type(rawget(value, 0)) == "userdata" and value.GetObjectType then
    return value:GetObjectType()
  end
  return t
end

local function Line(left, right)
  GameTooltip:AddDoubleLine(left, right, 1, 1, 1, 1, 1, 1)
end

local function Tooltip(entry)
  local value = entry.value
  GameTooltip:AddLine(entry.name)
  Line("Type", entry.kind)
  Line("Owner", ns.OwnerLabel(entry.owner))
  if entry.override then
    GameTooltip:AddLine("Not ClassicAPI's function: something replaced or hooked it.", 1, .3, .3, true)
  end

  if entry.kind == "function" and ns.profiling then
    Line("CPU", ns.MS(entry.cpu))
    Line("Calls", ns.Count(entry.calls))
    if entry.calls > 0 then Line("Per call", ns.MS(entry.cpu / entry.calls)) end
  elseif entry.kind == "table" then
    Line("Entries", ns.Count(table.count(value)))
  elseif type(value) == "table" then
    Line("Debug name", ns.FrameName(value))
    local source = value:GetSourceLocation()
    if source then GameTooltip:AddLine(source, .6, .6, .6, true) end
  elseif type(value) ~= "function" and value ~= nil then
    Line("Value", strsub(tostring(value), 1, 60))
  end
end

local tab = ns.AddTab("Globals")
tab.untagged = false

tab.list = ns.CreateList(tab.frame, {
  sort = 3,
  columns = {
    {
      title = "Name",
      text = function(entry)
        if entry.override then return ns.RED .. entry.name .. ns.GREY .. " (replaced)" end
        return entry.name
      end,
      value = function(entry) return strlower(entry.name) end,
    },
    {
      title = "Type", width = 80, justify = "LEFT",
      text = function(entry) return entry.kind end,
      value = function(entry) return entry.kind end,
    },
    {
      title = "Owner", width = 100, justify = "LEFT",
      tip = "The addon that made the value. Strings, numbers and booleans have no owner.",
      text = function(entry) return ns.OwnerLabel(entry.owner) end,
      value = function(entry) return strlower(ns.OwnerLabel(entry.owner)) end,
    },
    {
      title = "CPU", width = 64,
      tip = "The CPU time of the function since the last reset. Needs CPU profiling.",
      text = function(entry) return entry.cpu and ns.MS(entry.cpu) end,
      value = function(entry) return entry.cpu end,
    },
    {
      title = "Calls", width = 70,
      tip = "How often the function ran since the last reset. Needs CPU profiling.",
      text = function(entry) return entry.calls and ns.Count(entry.calls) end,
      value = function(entry) return entry.calls end,
    },
  },

  tooltip = Tooltip,

  menu = function(entry, root)
    local value = entry.value
    root:CreateTitle(entry.name)
    if type(value) == "table" then
      root:CreateButton("Inspect", function() ns.Inspect(value) end)
      if entry.kind ~= "table" then
        root:CreateButton("Highlight", function() ns.Highlight(value) end)
      end
    end
    root:CreateButton("Copy name", function() ns.Copy(entry.name) end)
  end,
})
tab.list:SetPoint("TOPLEFT", 0, -26)
tab.list:SetPoint("BOTTOMRIGHT")

-- [[ scan ]]
local function Scan()
  -- the ClassicAPI functions whose global is no longer ClassicAPI's
  local overrides = {}
  for name, fn in pairs(ClassicAPI) do
    if type(fn) == "function" then
      if _G[name] ~= fn then overrides[name] = true end
    elseif type(fn) == "table" and type(_G[name]) == "table" then
      for key, sub in pairs(fn) do
        if type(sub) == "function" and _G[name][key] ~= sub then overrides[name .. "." .. key] = true end
      end
    end
  end

  local entries = {}
  local function Add(name, value)
    local kind = Kind(value)
    local owner = (kind ~= "string" and kind ~= "number" and kind ~= "boolean" and kind ~= "nil") and ns.Owner(value) or nil
    local override = overrides[name]
    if not owner and not override and not tab.untagged then return end

    local entry = { name = name, value = value, kind = kind, owner = owner, override = override }
    entry.search = strlower(name .. " " .. ns.OwnerLabel(owner))
    if kind == "function" and ns.profiling then
      entry.cpu, entry.calls = GetFunctionCPUUsage(value)
    end
    tinsert(entries, entry)
    overrides[name] = nil
  end

  for name, value in pairs(_G) do
    if type(name) == "string" then
      Add(name, value)
      if type(value) == "table" and (LIBRARIES[name] or strfind(name, "^C_")) then
        for key, fn in pairs(value) do
          if type(key) == "string" and type(fn) == "function" then Add(name .. "." .. key, fn) end
        end
      end
    end
  end

  -- overridden names that are gone or live outside the scanned namespaces
  for name in pairs(overrides) do
    local space, key = strmatch(name, "^([^.]+)%.(.+)$")
    Add(name, space and _G[space][key] or _G[name])
  end

  tab.count = #entries
  tab.overrides = 0
  for _, entry in ipairs(entries) do
    if entry.override then tab.overrides = tab.overrides + 1 end
  end
  tab.list:SetEntries(entries)
end

local rescan = ns.CreateButton("ClassicAPIProfilerWindowRescan", tab.frame, 80, "Rescan", Scan)
rescan:SetPoint("TOPRIGHT", -4, -2)
ns.Tooltip(rescan, "Read the global namespace again. Also updates the CPU times.")

local untagged = CreateFrame("CheckButton", "ClassicAPIProfilerWindowUntagged", tab.frame, "UICheckButtonTemplate")
untagged:SetSize(20, 20)
untagged:SetPoint("RIGHT", rescan, "LEFT", -110, 0)
ClassicAPIProfilerWindowUntaggedText:SetText("Show FrameXML")
untagged:SetScript("OnClick", function(self)
  tab.untagged = self:GetChecked() and true or false
  Scan()
end)
ns.Tooltip(untagged, "Also list the values the game's own code made, and plain strings, numbers and booleans. With CPU profiling this shows the cost of every game API function.")

function tab:OnSelect()
  if not self.count then Scan() end
end

function tab:Status()
  if not self.count then return "" end
  return format("%d globals listed   %d ClassicAPI functions replaced or hooked", #self.list.shown, self.overrides)
end
