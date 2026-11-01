-- Profiler core: shared helpers, the once-a-second sampler and the per-addon
-- memory and CPU statistics every other profiler file reads.
--
-- Based on pfDebug (https://github.com/shagu/pfDebug):
--
-- MIT License
--
-- Copyright (c) 2018 Shagu
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in all
-- copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
local _, ns = ...

ns.INTERVAL = 1 -- seconds between samples

-- whether CPU profiling runs in this session. scriptProfile takes effect at
-- a reload and these tools load on demand, so the cvar can already hold the
-- next session's value: ask the profiler whether it counted a call instead.
local function Probe() end
Probe()
ns.profiling = select(2, GetFunctionCPUUsage(Probe)) > 0

-- [[ styling ]]
function ns.CreateBackdrop(frame)
  frame:SetBackdrop({
    bgFile = "Interface\\BUTTONS\\WHITE8X8", tile = false, tileSize = 0,
    edgeFile = "Interface\\BUTTONS\\WHITE8X8", edgeSize = 1,
    insets = {left = -1, right = -1, top = -1, bottom = -1},
  })
  frame:SetBackdropColor(0, 0, 0, .75)
  frame:SetBackdropBorderColor(.1, .1, .1, 1)
end

-- [[ formatting ]]
ns.ACCENT = "|cff33ffcc"
ns.GOLD = "|cffffcc00"
ns.GREY = "|cffaaaaaa"
ns.WHITE = "|cffffffff"
ns.RED = "|cffff5555"

function ns.Print(msg)
  DEFAULT_CHAT_FRAME:AddMessage(ns.ACCENT .. "Profiler|r: " .. msg)
end

function ns.KB(kb)
  if math.abs(kb) >= 1024 then return format("%.1f MB", kb / 1024) end
  return format("%.0f KB", kb)
end

function ns.KBRate(kbps)
  if not kbps then return "-" end
  if math.abs(kbps) >= 1024 then return format("%.2f MB/s", kbps / 1024) end
  return format("%.1f KB/s", kbps)
end

function ns.MS(ms)
  if not ms then return "-" end
  if ms >= 10000 then return format("%.1f s", ms / 1000) end
  if ms >= 100 then return format("%.0f ms", ms) end
  if ms >= 1 then return format("%.1f ms", ms) end
  return format("%.3f ms", ms)
end

function ns.Count(n)
  if not n then return "-" end
  return BreakUpLargeNumbers(floor(n + .5))
end

-- [[ ownership: which addon made a table, function or frame ]]
-- nil for objects the game's own code made (FrameXML or the engine), "script"
-- for objects a macro or /run made.
function ns.Owner(value)
  local tag = _classicapi_GcTagOf(value)
  if tag and strsub(tag, 1, 2) == "0x" then return "script" end
  return tag
end

function ns.OwnerLabel(owner)
  return owner or "FrameXML"
end

-- a frame's name as the debug tools show it
function ns.FrameName(frame)
  local name = frame:GetDebugName()
  if name == "" then return "<" .. frame:GetObjectType() .. ">" end
  return name
end

-- [[ events of this module: sample and reset listeners ]]
local sampleListeners, resetListeners = {}, {}

function ns.OnSample(fn) tinsert(sampleListeners, fn) end
function ns.OnReset(fn) tinsert(resetListeners, fn) end

-- [[ per-addon statistics ]]
-- addon rates are deltas between two samples. A garbage collection between
-- the samples frees memory of every addon, so a window with a collection
-- holds the previous allocation rate.
local stats = {
  addons = {},   -- [name] = record
  list = {},     -- loaded addon records, in load order
}
ns.stats = stats

local gc = { count = 0 }
ns.gc = gc

local collected = false
local lastMem, lastThreshold = gcinfo()

local function NewAddon(index, name, title)
  local addon = { index = index, name = name, title = title, mem = 0, cpu = 0, peakRate = 0 }
  addon.search = strlower(name .. " " .. title)
  stats.addons[name] = addon
  tinsert(stats.list, addon)
  return addon
end

-- a cumulative counter's growth since `last`, where a drop means a reset
local function Delta(now, last)
  if not last or now < last then return now end
  return now - last
end
ns.Delta = Delta

local lastScript, lastEventTime, lastEventCount

local function Sample(dt)
  local mem, threshold = gcinfo()
  stats.collected = collected
  stats.memRate = not collected and stats.mem and (mem - stats.mem) / dt or nil
  stats.mem, stats.threshold = mem, threshold
  collected = false

  UpdateAddOnMemoryUsage()
  if ns.profiling then UpdateAddOnCPUUsage() end

  local cpuSum, topMem, topCpu = 0, nil, nil
  for i = 1, GetNumAddOns() do
    if IsAddOnLoaded(i) then
      local name, title = GetAddOnInfo(i)
      local addon = stats.addons[name] or NewAddon(i, name, title)

      local addonMem = GetAddOnMemoryUsage(i)
      if not stats.collected and addon.sampled then
        addon.memRate = (addonMem - addon.mem) / dt
        addon.peakRate = max(addon.peakRate, addon.memRate)
      end
      addon.mem, addon.sampled = addonMem, true

      if ns.profiling then
        local cpu = GetAddOnCPUUsage(i)
        addon.cpuRate = Delta(cpu, addon.cpu) / dt
        addon.cpu = cpu
        cpuSum = cpuSum + addon.cpuRate
        if not topCpu or addon.cpuRate > topCpu.cpuRate then topCpu = addon end
      end

      if addon.memRate and (not topMem or addon.memRate > topMem.memRate) then topMem = addon end
    end
  end

  for _, addon in ipairs(stats.list) do
    addon.cpuShare = cpuSum > 0 and addon.cpuRate and addon.cpuRate / cpuSum * 100 or nil
  end
  stats.cpuSum, stats.topMem, stats.topCpu = cpuSum, topMem, topCpu

  if ns.profiling then
    local script = GetScriptCPUUsage()
    local eventTime, eventCount = GetEventCPUUsage()
    stats.scriptRate = Delta(script, lastScript) / dt
    stats.eventRate = Delta(eventTime, lastEventTime) / dt
    stats.eventCountRate = Delta(eventCount, lastEventCount) / dt
    lastScript, lastEventTime, lastEventCount = script, eventTime, eventCount
  end

  for _, fn in ipairs(sampleListeners) do fn(dt) end
end

-- the counters as they are now, so the first sample's rates cover only its
-- own second, not the time since the UI loaded
local function Baseline()
  stats.mem, stats.threshold = gcinfo()
  lastMem, lastThreshold = stats.mem, stats.threshold

  UpdateAddOnMemoryUsage()
  if ns.profiling then UpdateAddOnCPUUsage() end
  for i = 1, GetNumAddOns() do
    if IsAddOnLoaded(i) then
      local name, title = GetAddOnInfo(i)
      local addon = stats.addons[name] or NewAddon(i, name, title)
      addon.mem, addon.sampled = GetAddOnMemoryUsage(i), true
      if ns.profiling then addon.cpu = GetAddOnCPUUsage(i) end
    end
  end

  if ns.profiling then
    lastScript = GetScriptCPUUsage()
    lastEventTime, lastEventCount = GetEventCPUUsage()
  end
end

function ns.Reset()
  ResetCPUUsage()
  lastScript, lastEventTime, lastEventCount = nil, nil, nil
  for _, addon in ipairs(stats.list) do
    addon.cpu, addon.peakRate = 0, 0
  end
  for _, fn in ipairs(resetListeners) do fn() end
end

-- [[ the sampler: every frame, from the first open of the widget or the
-- analyzer until /reload ]]
-- The debug tools also load for /dump and the rest, so the profiler costs
-- nothing until it is opened.
local sampler = CreateFrame("Frame")
sampler:Hide()
local elapsedSinceSample = 0

-- spot garbage collections: Lua 5.0 sets a new threshold after each
-- collection, so a changed threshold means a collection ran
sampler:SetScript("OnUpdate", function(self, elapsed)
  local mem, threshold = gcinfo()
  if threshold ~= lastThreshold then
    collected = true
    gc.count = gc.count + 1
    gc.time, gc.clock, gc.freed = GetTime(), date("%H:%M:%S"), lastMem - mem
  end
  lastMem, lastThreshold = mem, threshold

  elapsedSinceSample = elapsedSinceSample + elapsed
  if elapsedSinceSample >= ns.INTERVAL then
    Sample(elapsedSinceSample)
    elapsedSinceSample = 0
  end
end)

function ns.StartSampling()
  if sampler:IsShown() then return end
  Baseline()
  sampler:Show()
end

-- [[ CPU profiling switch: the cvar takes effect after a reload ]]
function ns.SetProfiling(enable)
  SetCVar("scriptProfile", enable and "1" or "0")
  ReloadUI()
end

-- [[ /profiler ]]
function ClassicAPIProfiler_HandleSlashCmd(msg)
  local cmd, arg = strmatch(strlower(msg or ""), "^(%S*)%s*(.-)$")
  if cmd == "" then
    ns.window:SetShown(not ns.window:IsShown())
  elseif cmd == "widget" then
    ns.widget:SetShown(not ns.widget:IsShown())
  elseif cmd == "reset" then
    ns.Reset()
    ns.Print("CPU and allocation counters reset.")
  elseif cmd == "profile" and (arg == "on" or arg == "off") then
    ns.SetProfiling(arg == "on")
  else
    ns.Print("/profiler - toggle the analyzer")
    ns.Print("/profiler widget - toggle the memory widget")
    ns.Print("/profiler reset - reset CPU and allocation counters")
    ns.Print("/profiler profile on|off - CPU profiling (reloads the UI)")
  end
end
