-- Frames tab: the frames that run scripts, with the CPU time of all their
-- scripts (CPU profiling) and the memory they allocate (deep tracking).
local _, ns = ...

local track = ns.track

local function Profiled(fn)
  return function(rec)
    if ns.profiling then return fn(rec) end
  end
end

local function Tracked(fn)
  return function(rec)
    if rec.hookedUpdate or rec.hookedEvent then return fn(rec) end
  end
end

local function Line(left, right)
  GameTooltip:AddDoubleLine(left, right, 1, 1, 1, 1, 1, 1)
end

-- the events of a frame, most expensive first
local function SortedEvents(rec)
  local list = {}
  for name, stat in pairs(rec.events) do
    tinsert(list, { name = name, stat = stat })
  end
  local key = ns.profiling and "cpu" or "alloc"
  table.sort(list, function(a, b) return a.stat[key] > b.stat[key] end)
  return list
end

local function Tooltip(rec)
  local frame = rec.frame
  GameTooltip:AddLine(rec.name)
  Line("Type", frame:GetObjectType())
  Line("Owner", ns.OwnerLabel(rec.owner))
  Line("State", frame:IsVisible() and "visible" or frame:IsShown() and "shown, parent hidden" or "hidden")

  local source = frame:GetSourceLocation()
  if source then
    GameTooltip:AddLine(source, .6, .6, .6, true)
  end

  GameTooltip:AddLine(" ")
  for _, script in ipairs({ "OnUpdate", "OnEvent" }) do
    local fn = frame:GetScript(script)
    if fn then
      local text = ns.OwnerLabel(ns.Owner(fn))
      if ns.profiling then
        local ms, calls = GetFunctionCPUUsage(fn)
        text = text .. ns.GREY .. "  " .. ns.MS(ms) .. " in " .. ns.Count(calls) .. " calls"
      end
      Line(script, text)
    end
  end

  if ns.profiling then
    Line("All scripts", ns.MS(rec.cpu) .. " in " .. ns.Count(rec.calls) .. " calls")
  end
  if rec.hookedUpdate or rec.hookedEvent then
    Line("Allocated", ns.KB(rec.alloc))
  end

  local events = SortedEvents(rec)
  if #events > 0 then
    GameTooltip:AddLine(" ")
    GameTooltip:AddLine("Events", 1, .8, 0)
    for i = 1, min(#events, 12) do
      local stat = events[i].stat
      local text = ns.Count(stat.count) .. "x"
      if ns.profiling then text = text .. "  " .. ns.MS(stat.cpu) end
      Line(events[i].name, text .. "  " .. ns.KB(stat.alloc))
    end
  end

  GameTooltip:AddLine(" ")
  GameTooltip:AddLine("Right-click: highlight, inspect or copy.", .6, .6, .6)
end

local tab = ns.AddTab("Frames")

tab.list = ns.CreateList(tab.frame, {
  sort = 3,
  columns = {
    {
      title = "Frame",
      text = function(rec) return rec.name end,
      value = function(rec) return strlower(rec.name) end,
    },
    {
      title = "Owner", width = 100, justify = "LEFT",
      tip = "The addon that made the frame's OnUpdate or OnEvent handler, or the frame itself.",
      text = function(rec) return ns.OwnerLabel(rec.owner) end,
      value = function(rec) return strlower(ns.OwnerLabel(rec.owner)) end,
    },
    {
      title = "CPU/s", width = 64,
      tip = "The CPU time of all the frame's scripts in the last second. Needs CPU profiling.",
      text = Profiled(function(rec) return ns.MS(rec.cpuRate) end),
      value = Profiled(function(rec) return rec.cpuRate end),
    },
    {
      title = "Calls/s", width = 60,
      tip = "How often the frame's scripts ran in the last second. Needs CPU profiling.",
      text = Profiled(function(rec) return ns.Count(rec.callRate) end),
      value = Profiled(function(rec) return rec.callRate end),
    },
    {
      title = "Per Call", width = 64,
      tip = "The average CPU time of one script call since the last reset.",
      text = Profiled(function(rec) return rec.calls > 0 and ns.MS(rec.cpu / rec.calls) end),
      value = Profiled(function(rec) return rec.calls > 0 and rec.cpu / rec.calls or nil end),
    },
    {
      title = "Alloc/s", width = 72,
      tip = "The Lua memory the frame's OnUpdate and OnEvent scripts allocated in the last second. Needs deep tracking.",
      text = Tracked(function(rec) return ns.KBRate(rec.allocRate) end),
      value = Tracked(function(rec) return rec.allocRate end),
    },
    {
      title = "CPU Total", width = 64,
      tip = "The CPU time of all the frame's scripts since the last reset.",
      text = Profiled(function(rec) return ns.MS(rec.cpu) end),
      value = Profiled(function(rec) return rec.cpu end),
    },
  },

  tooltip = Tooltip,

  menu = function(rec, root)
    root:CreateTitle(rec.name)
    root:CreateButton("Highlight", function() ns.Highlight(rec.frame) end)
    root:CreateButton("Inspect", function() ns.Inspect(rec.frame) end)
    root:CreateButton("Copy name", function() ns.Copy(rec.name) end)
    local source = rec.frame:GetSourceLocation()
    if source then
      root:CreateButton("Copy source location", function() ns.Copy(source) end)
    end
    root:CreateButton("Show " .. ns.OwnerLabel(rec.owner) .. " frames", function()
      ns.ShowIn("Frames", ns.OwnerLabel(rec.owner))
    end)
  end,
})
tab.list:SetAllPoints()

local function Update(self)
  local entries = {}
  for _, rec in pairs(track.frames) do
    if rec.calls > 0 or rec.alloc > 0 then tinsert(entries, rec) end
  end
  self.list:SetEntries(entries)
end

tab.OnSelect = Update
tab.OnSample = Update

function tab:Status()
  local text = format("%d frames   %d listed", track.frameCount, #self.list.shown)
  if not ns.profiling and not track.deep then
    return text .. "   " .. ns.GOLD .. "Turn on CPU profiling or deep tracking to measure frames."
  end
  if track.deep then text = text .. "   deep tracking on" end
  return text
end
