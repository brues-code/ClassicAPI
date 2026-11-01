-- Addons tab: the memory, allocation rate and CPU time of each addon.
local _, ns = ...

local stats = ns.stats

local function Profiled(fn)
  return function(addon)
    if ns.profiling then return fn(addon) end
  end
end

local tab = ns.AddTab("Addons")

tab.list = ns.CreateList(tab.frame, {
  sort = 3,
  columns = {
    {
      title = "Addon",
      text = function(addon) return addon.title end,
      value = function(addon) return strlower(addon.name) end,
    },
    {
      title = "Memory", width = 70,
      tip = "The Lua memory that the addon's objects use now.",
      text = function(addon) return ns.KB(addon.mem) end,
      value = function(addon) return addon.mem end,
    },
    {
      title = "Alloc/s", width = 76,
      tip = "The Lua memory the addon allocated in the last second. Allocations become garbage that the collector must free. A second with a garbage collection keeps the previous value.",
      text = function(addon) return ns.KBRate(addon.memRate) end,
      value = function(addon) return addon.memRate end,
    },
    {
      title = "Peak/s", width = 76,
      tip = "The highest Alloc/s since the last reset.",
      text = function(addon) return ns.KBRate(addon.peakRate) end,
      value = function(addon) return addon.peakRate end,
    },
    {
      title = "CPU/s", width = 64,
      tip = "The CPU time of the addon's own functions in the last second. Needs CPU profiling.",
      text = Profiled(function(addon) return ns.MS(addon.cpuRate) end),
      value = Profiled(function(addon) return addon.cpuRate end),
    },
    {
      title = "CPU %", width = 50,
      tip = "The addon's share of the CPU time of all addons in the last second.",
      text = Profiled(function(addon) return addon.cpuShare and format("%.1f%%", addon.cpuShare) end),
      value = Profiled(function(addon) return addon.cpuShare end),
    },
    {
      title = "CPU Total", width = 70,
      tip = "The CPU time of the addon's own functions since the last reset.",
      text = Profiled(function(addon) return ns.MS(addon.cpu) end),
      value = Profiled(function(addon) return addon.cpu end),
    },
  },

  tooltip = function(addon)
    GameTooltip:AddLine(addon.title)
    GameTooltip:AddLine(addon.name, .6, .6, .6)
    GameTooltip:AddLine(" ")
    GameTooltip:AddDoubleLine("Memory", ns.KB(addon.mem), 1, 1, 1, 1, 1, 1)
    GameTooltip:AddDoubleLine("Allocated per second", ns.KBRate(addon.memRate), 1, 1, 1, 1, 1, 1)
    GameTooltip:AddDoubleLine("Peak per second", ns.KBRate(addon.peakRate), 1, 1, 1, 1, 1, 1)
    if ns.profiling then
      GameTooltip:AddDoubleLine("CPU per second", ns.MS(addon.cpuRate), 1, 1, 1, 1, 1, 1)
      GameTooltip:AddDoubleLine("CPU since reset", ns.MS(addon.cpu), 1, 1, 1, 1, 1, 1)
    else
      GameTooltip:AddLine("Turn on CPU profiling to see CPU times.", .6, .6, .6)
    end
    GameTooltip:AddLine(" ")
    GameTooltip:AddLine("Click: show its frames. Right-click: more.", .6, .6, .6)
  end,

  click = function(addon)
    ns.ShowIn("Frames", addon.name)
  end,

  menu = function(addon, root)
    root:CreateTitle(addon.name)
    root:CreateButton("Show frames", function() ns.ShowIn("Frames", addon.name) end)
    root:CreateButton("Show globals", function() ns.ShowIn("Globals", addon.name) end)
    root:CreateButton("Copy name", function() ns.Copy(addon.name) end)
  end,
})
tab.list:SetAllPoints()

function tab:OnSelect()
  self.list:SetEntries(stats.list)
end

function tab:OnSample()
  self.list:SetEntries(stats.list)
end

function tab:Status()
  local text = format("%d addons   Lua %s / %s", #stats.list, ns.KB(stats.mem or 0), ns.KB(stats.threshold or 0))
  if ns.profiling and stats.scriptRate then
    text = text .. format("   scripts %s/s   events %s/s (%.0f fired/s)",
      ns.MS(stats.scriptRate), ns.MS(stats.eventRate), stats.eventCountRate)
  end
  return text
end
