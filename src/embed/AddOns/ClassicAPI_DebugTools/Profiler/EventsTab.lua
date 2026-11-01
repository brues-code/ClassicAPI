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
  Line("Fired", ns.Count(ev.fires) .. ns.GREY .. " (" .. format("%.1f", ev.fireRate or 0) .. "/s)")
  if track.deep then
    Line("Handlers ran", ns.Count(ev.dispatches))
    if ns.profiling then Line("Handler CPU", ns.MS(ev.cpu)) end
    Line("Allocated", ns.KB(ev.alloc))
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
  GameTooltip:AddLine("Listeners", 1, .8, 0)
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
    GameTooltip:AddLine("and " .. (#listeners - 15) .. " more", .6, .6, .6)
  end
end

local tab = ns.AddTab("Events")

tab.list = ns.CreateList(tab.frame, {
  sort = 2,
  columns = {
    {
      title = "Event",
      text = function(ev) return ev.name end,
      value = function(ev) return ev.search end,
    },
    {
      title = "Fires/s", width = 60,
      tip = "How often the event fired in the last second.",
      text = function(ev) return ev.fireRate and format("%.1f", ev.fireRate) end,
      value = function(ev) return ev.fireRate end,
    },
    {
      title = "Listeners", width = 60,
      tip = "The frames registered for the event.",
      text = function(ev) return ev.listeners end,
      value = function(ev) return ev.listeners end,
    },
    {
      title = "CPU/s", width = 64,
      tip = "The CPU time of the event's OnEvent handlers in the last second. Needs deep tracking and CPU profiling.",
      text = Tracked(function(ev) return ns.profiling and ns.MS(ev.cpuRate) end),
      value = Tracked(function(ev) return ns.profiling and ev.cpuRate or nil end),
    },
    {
      title = "Alloc/s", width = 72,
      tip = "The Lua memory the event's OnEvent handlers allocated in the last second. Needs deep tracking.",
      text = Tracked(function(ev) return ns.KBRate(ev.allocRate) end),
      value = Tracked(function(ev) return ev.allocRate end),
    },
    {
      title = "Fired", width = 64,
      tip = "How often the event fired since the analyzer opened or the last reset.",
      text = function(ev) return ns.Count(ev.fires) end,
      value = function(ev) return ev.fires end,
    },
  },

  tooltip = Tooltip,

  menu = function(ev, root)
    root:CreateTitle(ev.name)
    root:CreateButton("Copy name", function() ns.Copy(ev.name) end)
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
  local text = format("%d events seen   %.0f fired/s", #self.list.entries, fires)
  if not track.deep then
    return text .. "   " .. ns.GOLD .. "Turn on deep tracking for the CPU time and memory of each event."
  end
  return text
end
