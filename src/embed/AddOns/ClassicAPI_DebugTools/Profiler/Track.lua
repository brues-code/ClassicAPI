-- Profiler track: per-frame and per-event statistics for the Frames and
-- Events tabs. The analyzer runs Scan once a sample while it is open.
--
-- Deep tracking hooks the intrinsic precall and postcall of every OnUpdate
-- and OnEvent script. The hooks never replace a handler, so GetScript and
-- SetScript behave as before, and they stay until /reload. Around each
-- script they measure the Lua memory it allocated and, with CPU profiling,
-- the time of each OnEvent handler per event.
local addonName, ns = ...

local PRECALL = LE_SCRIPT_BINDING_TYPE_INTRINSIC_PRECALL
local POSTCALL = LE_SCRIPT_BINDING_TYPE_INTRINSIC_POSTCALL
local Delta = ns.Delta

local track = {
  frames = {},     -- [frame] = record: frames that ran scripts, or are hooked
  events = {},     -- [event] = record: events fired while the analyzer is open
  frameCount = 0,  -- frames in the last scan
  deep = false,
}
ns.track = track

-- the frame that counts every event while the analyzer is open
local capture = CreateFrame("Frame")
track.capture = capture

-- the addon that runs a frame's scripts: the owner of its handlers, or the
-- frame's creator when the game's own code made the handlers
local function OwnerOf(frame)
  local fn = frame:GetScript("OnUpdate") or frame:GetScript("OnEvent")
  return fn and ns.Owner(fn) or ns.Owner(frame)
end

-- cpu and calls are the frame's totals now, so its first rate covers only
-- the time after the record exists, not the whole session
local function FrameRecord(frame, cpu, calls)
  local rec = {
    frame = frame, name = ns.FrameName(frame), owner = OwnerOf(frame),
    cpu = cpu or 0, calls = calls or 0, alloc = 0, events = {},
  }
  rec.search = strlower(rec.name .. " " .. ns.OwnerLabel(rec.owner))
  track.frames[frame] = rec
  return rec
end

local function EventRecord(name)
  local rec = { name = name, search = strlower(name), fires = 0, dispatches = 0, cpu = 0, alloc = 0 }
  track.events[name] = rec
  return rec
end

capture:SetScript("OnEvent", function(self, event)
  local rec = track.events[event] or EventRecord(event)
  rec.fires = rec.fires + 1
end)

-- [[ deep tracking ]]
-- a stack, because a script can run inside another script
local depth = 0
local memAt, gcAt, fnAt, cpuAt, eventAt = {}, {}, {}, {}, {}

local function Enter(fn, event)
  depth = depth + 1
  memAt[depth], gcAt[depth] = gcinfo()
  fnAt[depth], eventAt[depth] = fn, event
  if fn and ns.profiling then cpuAt[depth] = GetFunctionCPUUsage(fn) end
end

-- the KB allocated since the matching Enter (nil when a garbage collection
-- ran in between), the handler's CPU time in ms, and the event
local function Leave()
  local d = depth
  if d == 0 then return end
  depth = d - 1

  local mem, threshold = gcinfo()
  local alloc = threshold == gcAt[d] and max(mem - memAt[d], 0) or nil
  local fn, cpu = fnAt[d], nil
  if fn and ns.profiling then cpu = max(GetFunctionCPUUsage(fn) - cpuAt[d], 0) end
  fnAt[d] = nil
  return alloc, cpu, eventAt[d]
end

local function EnterUpdate() Enter() end

local function LeaveEvent(rec)
  local alloc, cpu, name = Leave()
  if not name then return end

  local ev = track.events[name] or EventRecord(name)
  local fe = rec.events[name]
  if not fe then
    fe = { count = 0, cpu = 0, alloc = 0 }
    rec.events[name] = fe
  end

  fe.count = fe.count + 1
  ev.dispatches = ev.dispatches + 1
  if alloc then
    rec.alloc, fe.alloc, ev.alloc = rec.alloc + alloc, fe.alloc + alloc, ev.alloc + alloc
  end
  if cpu then
    fe.cpu, ev.cpu = fe.cpu + cpu, ev.cpu + cpu
  end
end

local function Hook(frame, rec)
  if not rec.hookedUpdate and frame:GetScript("OnUpdate") then
    rec.hookedUpdate = true
    frame:HookScript("OnUpdate", EnterUpdate, PRECALL)
    frame:HookScript("OnUpdate", function()
      local alloc = Leave()
      if alloc then rec.alloc = rec.alloc + alloc end
    end, POSTCALL)
  end

  if not rec.hookedEvent and frame:GetScript("OnEvent") then
    rec.hookedEvent = true
    -- the engine sets the `event` global before it runs the script
    frame:HookScript("OnEvent", function() Enter(frame:GetScript("OnEvent"), event) end, PRECALL)
    frame:HookScript("OnEvent", function() LeaveEvent(rec) end, POSTCALL)
  end
end

function track.EnableDeep()
  track.deep = true
end

-- [[ sampling ]]
-- the frames registered for an event, without the capture frame
local function CountListeners(...)
  local n = select("#", ...)
  for i = 1, n do
    if select(i, ...) == capture then return n - 1 end
  end
  return n
end
track.CountListeners = CountListeners

function track.Scan(dt)
  local profiling, deep = ns.profiling, track.deep
  local count = 0

  local frame = EnumerateFrames()
  while frame do
    count = count + 1
    local rec = track.frames[frame]

    if profiling then
      local cpu, calls = GetFrameCPUUsage(frame, false)
      if calls > 0 and not rec then rec = FrameRecord(frame, cpu, calls) end
      if rec then
        rec.cpuRate = Delta(cpu, rec.cpu) / dt
        rec.callRate = Delta(calls, rec.calls) / dt
        rec.cpu, rec.calls = cpu, calls
      end
    end

    if deep and not (rec and rec.hookedUpdate and rec.hookedEvent) then
      if frame:GetScript("OnUpdate") or frame:GetScript("OnEvent") then
        rec = rec or FrameRecord(frame)
        -- the profiler does not measure its own addon
        if rec.owner ~= addonName then Hook(frame, rec) end
      end
    end

    frame = EnumerateFrames(frame)
  end
  track.frameCount = count

  for _, rec in pairs(track.frames) do
    rec.allocRate = Delta(rec.alloc, rec.lastAlloc) / dt
    rec.lastAlloc = rec.alloc
  end

  for name, ev in pairs(track.events) do
    ev.fireRate = Delta(ev.fires, ev.lastFires) / dt
    ev.cpuRate = Delta(ev.cpu, ev.lastCpu) / dt
    ev.allocRate = Delta(ev.alloc, ev.lastAlloc) / dt
    ev.lastFires, ev.lastCpu, ev.lastAlloc = ev.fires, ev.cpu, ev.alloc
    ev.listeners = CountListeners(GetFramesRegisteredForEvent(name))
  end
end

-- the frame totals and the deep hooks keep counting while the analyzer is
-- closed, so an open takes the counters as they are now, and the first scan
-- covers only its own second
function track.Start()
  for frame, rec in pairs(track.frames) do
    if ns.profiling then rec.cpu, rec.calls = GetFrameCPUUsage(frame, false) end
    rec.lastAlloc = rec.alloc
  end
  for _, ev in pairs(track.events) do
    ev.lastCpu, ev.lastAlloc = ev.cpu, ev.alloc
  end
  capture:RegisterAllEvents()
end

function track.Stop()
  capture:UnregisterAllEvents()
end

ns.OnReset(function()
  for _, rec in pairs(track.frames) do
    rec.cpu, rec.calls, rec.alloc, rec.lastAlloc = 0, 0, 0, 0
    table.wipe(rec.events)
  end
  for _, ev in pairs(track.events) do
    ev.fires, ev.dispatches, ev.cpu, ev.alloc = 0, 0, 0, 0
    ev.lastFires, ev.lastCpu, ev.lastAlloc = 0, 0, 0
  end
end)
