-- Events tab: how often each event fires while the analyzer is open, how
-- many frames listen to it, and (deep tracking) what its handlers cost.
local _, ns = ...

local track = ns.track

local function Tracked(fn)
  return function(ev)
    if track.deep then return fn(ev) end
  end
end

local function Line(left, right)
  GameTooltip:AddDoubleLine(left, right, 1, 1, 1, 1, 1, 1)
end

local function Tooltip(ev)
  GameTooltip:AddLine(ev.name)
  Line(PROFILER_LABEL_FIRED, ns.Count(ev.fires) .. ns.GREY .. " (" .. format("%.1f", ev.fireRate or 0) .. "/s)")
  if track.deep then
    Line(PROFILER_LABEL_HANDLERS_RAN, ns.Count(ev.dispatches))
    if ns.profiling then Line(PROFILER_LABEL_HANDLER_CPU, ns.MS(ev.cpu)) end
    Line(PROFILER_LABEL_ALLOCATED, ns.KB(ev.alloc))
  end

  local listeners = {}
  for _, frame in ipairs({ GetFramesRegisteredForEvent(ev.name) }) do
    if frame ~= track.capture then
      local rec = track.frames[frame]
      tinsert(listeners, { frame = frame, stat = rec and rec.events[ev.name] })
    end
  end
  if #listeners == 0 then return end

  table.sort(listeners, function(a, b)
    local ca, cb = a.stat and a.stat.cpu or -1, b.stat and b.stat.cpu or -1
    return ca > cb
  end)

  GameTooltip:AddLine(" ")
  GameTooltip:AddLine(PROFILER_LABEL_LISTENERS, 1, .8, 0)
  for i = 1, min(#listeners, 15) do
    local frame, stat = listeners[i].frame, listeners[i].stat
    local text = ns.GREY .. ns.OwnerLabel(ns.Owner(frame:GetScript("OnEvent") or frame))
    if stat then
      if ns.profiling then text = text .. "  " .. ns.WHITE .. ns.MS(stat.cpu) end
      text = text .. "  " .. ns.WHITE .. ns.KB(stat.alloc)
    end
    Line(ns.FrameName(frame), text)
  end
  if #listeners > 15 then
    GameTooltip:AddLine(format(PROFILER_MORE_LISTENERS, #listeners - 15), .6, .6, .6)
  end
end

local tab = ns.AddTab(PROFILER_TAB_EVENTS)

tab.list = ns.CreateList(tab.frame, {
  sort = 2,
  columns = {
    {
      title = PROFILER_EVENTS_COL_EVENT,
      text = function(ev) return ev.name end,
      value = function(ev) return ev.search end,
    },
    {
      title = PROFILER_EVENTS_COL_FIRES_S, width = 60,
      tip = PROFILER_EVENTS_TIP_FIRES_S,
      text = function(ev) return ev.fireRate and format("%.1f", ev.fireRate) end,
      value = function(ev) return ev.fireRate end,
    },
    {
      title = PROFILER_EVENTS_COL_LISTENERS, width = 60,
      tip = PROFILER_EVENTS_TIP_LISTENERS,
      text = function(ev) return ev.listeners end,
      value = function(ev) return ev.listeners end,
    },
    {
      title = PROFILER_EVENTS_COL_CPU_S, width = 64,
      tip = PROFILER_EVENTS_TIP_CPU_S,
      text = Tracked(function(ev) return ns.profiling and ns.MS(ev.cpuRate) end),
      value = Tracked(function(ev) return ns.profiling and ev.cpuRate or nil end),
    },
    {
      title = PROFILER_EVENTS_COL_ALLOC_S, width = 72,
      tip = PROFILER_EVENTS_TIP_ALLOC_S,
      text = Tracked(function(ev) return ns.KBRate(ev.allocRate) end),
      value = Tracked(function(ev) return ev.allocRate end),
    },
    {
      title = PROFILER_EVENTS_COL_FIRED, width = 64,
      tip = PROFILER_EVENTS_TIP_FIRED,
      text = function(ev) return ns.Count(ev.fires) end,
      value = function(ev) return ev.fires end,
    },
  },

  tooltip = Tooltip,

  menu = function(ev, root)
    root:CreateTitle(ev.name)
    root:CreateButton(PROFILER_EVENTS_MENU_COPY_NAME, function() ns.Copy(ev.name) end)
  end,
})
tab.list:SetAllPoints()

local function Update(self)
  local entries = {}
  for _, ev in pairs(track.events) do tinsert(entries, ev) end
  self.list:SetEntries(entries)
end

tab.OnSelect = Update
tab.OnSample = Update

function tab:Status()
  local fires = 0
  for _, ev in pairs(track.events) do fires = fires + (ev.fireRate or 0) end
  local text = format(PROFILER_EVENTS_STATUS, #self.list.entries, fires)
  if not track.deep then
    return text .. "   " .. ns.GOLD .. PROFILER_EVENTS_STATUS_PROMPT
  end
  return text
end
