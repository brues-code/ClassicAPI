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
  Line(PROFILER_LABEL_TYPE, frame:GetObjectType())
  Line(PROFILER_LABEL_OWNER, ns.OwnerLabel(rec.owner))
  local state
  if not frame:IsVisible() then
    state = PROFILER_STATE_HIDDEN
  elseif frame:IsShown() then
    state = PROFILER_STATE_VISIBLE
  else
    state = PROFILER_STATE_SHOWN_HIDDEN
  end
  Line(PROFILER_LABEL_STATE, state)

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
        text = text .. ns.GREY .. ns.MS(ms) .. format(PROFILER_CALLS_FORMAT, ns.Count(calls))
      end
      Line(script, text)
    end
  end

  if ns.profiling then
    Line(PROFILER_LABEL_ALL_SCRIPTS, ns.MS(rec.cpu) .. format(PROFILER_CALLS_FORMAT, ns.Count(rec.calls)))
  end
  if rec.hookedUpdate or rec.hookedEvent then
    Line(PROFILER_LABEL_ALLOCATED, ns.KB(rec.alloc))
  end

  local events = SortedEvents(rec)
  if #events > 0 then
    GameTooltip:AddLine(" ")
    GameTooltip:AddLine(PROFILER_LABEL_EVENTS, 1, .8, 0)
    for i = 1, min(#events, 12) do
      local stat = events[i].stat
      local text = ns.Count(stat.count) .. "x"
      if ns.profiling then text = text .. "  " .. ns.MS(stat.cpu) end
      Line(events[i].name, text .. "  " .. ns.KB(stat.alloc))
    end
  end

  GameTooltip:AddLine(" ")
  GameTooltip:AddLine(PROFILER_LABEL_RIGHT_CLICK_HINT, .6, .6, .6)
end

local tab = ns.AddTab(PROFILER_TAB_FRAMES)

tab.list = ns.CreateList(tab.frame, {
  sort = 3,
  columns = {
    {
      title = PROFILER_FRAMES_COL_FRAME,
      text = function(rec) return rec.name end,
      value = function(rec) return strlower(rec.name) end,
    },
    {
      title = PROFILER_FRAMES_COL_OWNER, width = 100, justify = "LEFT",
      tip = PROFILER_FRAMES_TIP_OWNER,
      text = function(rec) return ns.OwnerLabel(rec.owner) end,
      value = function(rec) return strlower(ns.OwnerLabel(rec.owner)) end,
    },
    {
      title = PROFILER_FRAMES_COL_CPU_S, width = 64,
      tip = PROFILER_FRAMES_TIP_CPU_S,
      text = Profiled(function(rec) return ns.MS(rec.cpuRate) end),
      value = Profiled(function(rec) return rec.cpuRate end),
    },
    {
      title = PROFILER_FRAMES_COL_CALLS_S, width = 60,
      tip = PROFILER_FRAMES_TIP_CALLS_S,
      text = Profiled(function(rec) return ns.Count(rec.callRate) end),
      value = Profiled(function(rec) return rec.callRate end),
    },
    {
      title = PROFILER_FRAMES_COL_PER_CALL, width = 64,
      tip = PROFILER_FRAMES_TIP_PER_CALL,
      text = Profiled(function(rec) return rec.calls > 0 and ns.MS(rec.cpu / rec.calls) end),
      value = Profiled(function(rec) return rec.calls > 0 and rec.cpu / rec.calls or nil end),
    },
    {
      title = PROFILER_FRAMES_COL_ALLOC_S, width = 72,
      tip = PROFILER_FRAMES_TIP_ALLOC_S,
      text = Tracked(function(rec) return ns.KBRate(rec.allocRate) end),
      value = Tracked(function(rec) return rec.allocRate end),
    },
    {
      title = PROFILER_FRAMES_COL_CPU_TOTAL, width = 64,
      tip = PROFILER_FRAMES_TIP_CPU_TOTAL,
      text = Profiled(function(rec) return ns.MS(rec.cpu) end),
      value = Profiled(function(rec) return rec.cpu end),
    },
  },

  tooltip = Tooltip,

  menu = function(rec, root)
    root:CreateTitle(rec.name)
    root:CreateButton(PROFILER_FRAMES_MENU_HIGHLIGHT, function() ns.Highlight(rec.frame) end)
    root:CreateButton(PROFILER_FRAMES_MENU_INSPECT, function() ns.Inspect(rec.frame) end)
    root:CreateButton(PROFILER_FRAMES_MENU_COPY_NAME, function() ns.Copy(rec.name) end)
    local source = rec.frame:GetSourceLocation()
    if source then
      root:CreateButton(PROFILER_FRAMES_MENU_COPY_SRC, function() ns.Copy(source) end)
    end
    root:CreateButton(format(PROFILER_FRAMES_MENU_SHOW_OWNER, ns.OwnerLabel(rec.owner)), function()
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
  local text = format(PROFILER_FRAMES_STATUS, track.frameCount, #self.list.shown)
  if not ns.profiling and not track.deep then
    return text .. "   " .. ns.GOLD .. PROFILER_FRAMES_STATUS_PROMPT
  end
  if track.deep then text = text .. PROFILER_FRAMES_STATUS_DEEP end
  return text
end
