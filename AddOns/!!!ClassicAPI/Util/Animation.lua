-- Backport of the animation system (AnimationGroup, and the Animation, Alpha,
-- Scale, Rotation and Translation animation types) to vanilla 1.12 / Lua 5.0.
--
-- Later clients animate regions inside the renderer. Vanilla has no such layer,
-- so this runs animations in Lua: one driver frame ticks every playing group,
-- and each effect is applied through the region's own setters. As on later
-- clients an effect lasts only while its group plays - the region's own state
-- is captured when the group starts and put back when it stops or finishes.
--
-- Implementation notes:
--   - Timeline: animations with the same order play together and orders play
--     in sequence. An order lasts as long as its longest
--     startDelay + duration + endDelay. BOUNCE plays alternate loops backwards.
--   - Effects: Alpha adds change * progress to the region's alpha. Translation
--     moves every anchor of the region by offset * progress. Scale multiplies
--     a frame's scale (so a frame's own anchor point is the pivot, exact for
--     frames anchored by their center) and resizes a texture or font string
--     anchored by one point. Rotation turns a texture about its origin (the
--     center unless set) through texture:SetRotation. Multiple animations of
--     one kind combine: alpha and offsets add, scales multiply, angles add.
--   - Scripts: OnPlay, OnPause, OnStop(requested), OnFinished(requested),
--     OnLoop(loopState) and OnUpdate(elapsed) on groups, and all but OnLoop on
--     animations. maxFramerate throttles an animation's OnUpdate; every
--     animation still gets a final OnUpdate at its end progress.
--   - Regions get CreateAnimationGroup / GetAnimationGroups / StopAnimating
--     from ClassicAPI's C++ side, which forwards to this runtime; the XML node
--     loaders hand each <Animations> block to LoadXML.
--   - Lua 5.0 has no `...` expression or select(); varargs use `arg`.

local Runtime = {}

local driver = CreateFrame("Frame")
driver:Hide()
local playing = {} -- groups currently PLAYING or PAUSED, in start order

local SMOOTHING = {
	NONE = function(p) return p end,
	IN = function(p) return p * p end,
	OUT = function(p) return 1 - (1 - p) * (1 - p) end,
	IN_OUT = function(p)
		if p < 0.5 then return 2 * p * p end
		local q = 1 - p
		return 1 - 2 * q * q
	end,
}

local LOOPING = { NONE = true, REPEAT = true, BOUNCE = true }

local GROUP_SCRIPTS = { OnPlay = true, OnPause = true, OnStop = true, OnFinished = true, OnLoop = true, OnUpdate = true }
local ANIM_SCRIPTS = { OnPlay = true, OnPause = true, OnStop = true, OnFinished = true, OnUpdate = true }

-- Parameter names later clients give XML animation script bodies.
local SCRIPT_PARAMS = {
	OnUpdate = "self, elapsed",
	OnStop = "self, requested",
	OnFinished = "self, requested",
	OnLoop = "self, loopState",
}

local function report(err)
	local handler = geterrorhandler and geterrorhandler()
	if handler then handler(err) end
end

local function fire(obj, name, a1, a2)
	local fn = obj._scripts[name]
	if fn then
		local ok, err = pcall(fn, obj, a1, a2)
		if not ok then report(err) end
	end
end

local function clamp01(v)
	if v < 0 then return 0 end
	if v > 1 then return 1 end
	return v
end

-- ---------------------------------------------------------------------------
-- Script plumbing shared by groups and animations
-- ---------------------------------------------------------------------------

local function checkScript(obj, name, valid)
	if not valid[name] then
		error(string.format("%s doesn't have a \"%s\" script", obj:GetObjectType(), tostring(name)), 3)
	end
end

local function SetScript(valid)
	return function(self, name, fn)
		checkScript(self, name, valid)
		if fn ~= nil and type(fn) ~= "function" then
			error("Usage: " .. self:GetObjectType() .. ":SetScript(\"type\", function)", 2)
		end
		self._scripts[name] = fn
	end
end

local function GetScript(valid)
	return function(self, name)
		checkScript(self, name, valid)
		return self._scripts[name]
	end
end

local function HasScript(valid)
	return function(self, name)
		return valid[name] and 1 or nil
	end
end

local function HookScript(valid)
	return function(self, name, fn)
		checkScript(self, name, valid)
		local old = self._scripts[name]
		if old then
			self._scripts[name] = function(a, b, c) old(a, b, c) fn(a, b, c) end
		else
			self._scripts[name] = fn
		end
	end
end

-- "$parent" in a name is the owner's name, as for frames.
local function resolveName(name, owner)
	if type(name) ~= "string" or name == "" then return nil end
	if string.find(name, "$parent", 1, true) then
		local ownerName = owner and owner.GetName and owner:GetName() or ""
		name = string.gsub(name, "%$parent", ownerName or "")
	end
	return name
end

-- ---------------------------------------------------------------------------
-- Animations
-- ---------------------------------------------------------------------------

local Animation = {}
local AnimationMeta = { __index = Animation }

local TYPES = {} -- type name -> metatable

local function newTypeMeta(typeName)
	local methods = setmetatable({}, { __index = Animation })
	local meta = { __index = methods }
	TYPES[typeName] = meta
	return methods
end

TYPES.Animation = AnimationMeta
local Alpha = newTypeMeta("Alpha")
local Scale = newTypeMeta("Scale")
local Rotation = newTypeMeta("Rotation")
local Translation = newTypeMeta("Translation")

function Animation:GetObjectType() return self._type end
function Animation:IsObjectType(t) return t == self._type or t == "Animation" or t == "UIObject" end
function Animation:GetName() return self._name end
function Animation:GetParent() return self._group end
function Animation:GetRegionParent() return self._group._region end

function Animation:SetDuration(d) self._duration = math.max(0, tonumber(d) or 0) self._group:_Layout() end
function Animation:GetDuration() return self._duration end
function Animation:SetStartDelay(d) self._startDelay = math.max(0, tonumber(d) or 0) self._group:_Layout() end
function Animation:GetStartDelay() return self._startDelay end
function Animation:SetEndDelay(d) self._endDelay = math.max(0, tonumber(d) or 0) self._group:_Layout() end
function Animation:GetEndDelay() return self._endDelay end
function Animation:SetOrder(o) self._order = math.floor(tonumber(o) or 1) self._group:_Layout() end
function Animation:GetOrder() return self._order end
function Animation:SetMaxFramerate(f) self._maxFramerate = math.max(0, tonumber(f) or 0) end
function Animation:GetMaxFramerate() return self._maxFramerate end

function Animation:SetSmoothing(s)
	s = string.upper(tostring(s or "NONE"))
	if not SMOOTHING[s] then
		error("Usage: Animation:SetSmoothing(\"NONE\"|\"IN\"|\"OUT\"|\"IN_OUT\")", 2)
	end
	self._smoothing = s
end
function Animation:GetSmoothing() return self._smoothing end

function Animation:GetProgress() return self._progress end
function Animation:GetSmoothProgress() return SMOOTHING[self._smoothing](self._progress) end
function Animation:GetElapsed() return self._elapsed end

function Animation:IsPlaying() return self._group._state == "PLAYING" and self._phase == "PLAY" end
function Animation:IsDelaying() return self._group._state == "PLAYING" and self._phase == "DELAY" end
function Animation:IsPaused() return self._group._state == "PAUSED" end
function Animation:IsStopped() return self._group._state == "STOPPED" end
function Animation:IsDone() return self._phase == "DONE" end

-- An animation plays as part of its group.
function Animation:Play() self._group:Play() end
function Animation:Stop() self._group:Stop() end
function Animation:Pause() self._group:Pause() end

Animation.SetScript = SetScript(ANIM_SCRIPTS)
Animation.GetScript = GetScript(ANIM_SCRIPTS)
Animation.HasScript = HasScript(ANIM_SCRIPTS)
Animation.HookScript = HookScript(ANIM_SCRIPTS)

function Alpha:SetChange(c) self._change = tonumber(c) or 0 end
function Alpha:GetChange() return self._change end

function Scale:SetScale(x, y) self._scaleX = tonumber(x) or 1 self._scaleY = tonumber(y) or self._scaleX end
function Scale:GetScale() return self._scaleX, self._scaleY end

function Rotation:SetDegrees(d) self._radians = math.rad(tonumber(d) or 0) end
function Rotation:GetDegrees() return math.deg(self._radians) end
function Rotation:SetRadians(r) self._radians = tonumber(r) or 0 end
function Rotation:GetRadians() return self._radians end

function Translation:SetOffset(x, y) self._offsetX = tonumber(x) or 0 self._offsetY = tonumber(y) or 0 end
function Translation:GetOffset() return self._offsetX, self._offsetY end

local function SetOrigin(self, point, x, y)
	self._origin = { point = string.upper(tostring(point or "CENTER")), x = tonumber(x) or 0, y = tonumber(y) or 0 }
end
local function GetOrigin(self)
	local o = self._origin
	return o.point, o.x, o.y
end
Scale.SetOrigin, Scale.GetOrigin = SetOrigin, GetOrigin
Rotation.SetOrigin, Rotation.GetOrigin = SetOrigin, GetOrigin

-- Normalized pivot inside a region for an origin point.
local ORIGIN_UV = {
	CENTER = { 0.5, 0.5 }, TOP = { 0.5, 0 }, BOTTOM = { 0.5, 1 }, LEFT = { 0, 0.5 }, RIGHT = { 1, 0.5 },
	TOPLEFT = { 0, 0 }, TOPRIGHT = { 1, 0 }, BOTTOMLEFT = { 0, 1 }, BOTTOMRIGHT = { 1, 1 },
}

-- ---------------------------------------------------------------------------
-- Groups
-- ---------------------------------------------------------------------------

local Group = {}
local GroupMeta = { __index = Group }

function Group:GetObjectType() return "AnimationGroup" end
function Group:IsObjectType(t) return t == "AnimationGroup" or t == "UIObject" end
function Group:GetName() return self._name end
function Group:GetParent() return self._region end

Group.SetScript = SetScript(GROUP_SCRIPTS)
Group.GetScript = GetScript(GROUP_SCRIPTS)
Group.HasScript = HasScript(GROUP_SCRIPTS)
Group.HookScript = HookScript(GROUP_SCRIPTS)

function Group:SetLooping(l)
	l = string.upper(tostring(l or "NONE"))
	if not LOOPING[l] then
		error("Usage: AnimationGroup:SetLooping(\"NONE\"|\"REPEAT\"|\"BOUNCE\")", 2)
	end
	self._looping = l
end
function Group:GetLooping() return self._looping end
function Group:GetLoopState() return self._loopState end

function Group:GetAnimations()
	return unpack(self._anims)
end

function Group:CreateAnimation(animType, name)
	animType = animType or "Animation"
	local meta = TYPES[animType]
	if not meta then
		error("AnimationGroup:CreateAnimation: unsupported animation type '" .. tostring(animType) .. "'", 2)
	end
	local a = setmetatable({
		_type = animType,
		_group = self,
		_scripts = {},
		_duration = 0, _startDelay = 0, _endDelay = 0, _order = 1,
		_smoothing = "NONE", _maxFramerate = 0,
		_progress = 0, _elapsed = 0, _accum = 0, _phase = "WAIT",
		_change = 0, _scaleX = 1, _scaleY = 1, _radians = 0, _offsetX = 0, _offsetY = 0,
		_origin = { point = "CENTER", x = 0, y = 0 },
	}, meta)
	a._name = resolveName(name, self)
	if a._name then setglobal(a._name, a) end
	table.insert(self._anims, a)
	self:_Layout()
	return a
end

-- Order start times and the total span of one pass.
function Group:_Layout()
	local spans, orders = {}, {}
	for _, a in ipairs(self._anims) do
		local span = a._startDelay + a._duration + a._endDelay
		if spans[a._order] == nil then table.insert(orders, a._order) end
		if span > (spans[a._order] or 0) then spans[a._order] = span else spans[a._order] = spans[a._order] or 0 end
	end
	table.sort(orders)
	local t, starts = 0, {}
	for _, o in ipairs(orders) do
		starts[o] = t
		t = t + spans[o]
	end
	self._total = t
	for _, a in ipairs(self._anims) do
		a._segStart = starts[a._order] or 0
		a._playStart = a._segStart + a._startDelay
		a._playEnd = a._playStart + a._duration
	end
end

function Group:GetDuration() return self._total end
function Group:GetProgress()
	if self._total <= 0 then return self._state == "STOPPED" and 0 or 1 end
	return clamp01(self._t / self._total)
end
function Group:IsPlaying() return self._state == "PLAYING" end
function Group:IsPaused() return self._state == "PAUSED" end
function Group:IsDone() return self._done end
function Group:IsPendingFinish() return self._finishing end

-- Region state the group's effects touch, captured when it starts.
local function captureBase(group)
	local r = group._region
	local base = {}
	local need = {}
	for _, a in ipairs(group._anims) do need[a._type] = true end
	if need.Alpha and r.GetAlpha then base.alpha = r:GetAlpha() end
	if need.Translation or need.Scale then
		base.points = {}
		local n = r.GetNumPoints and r:GetNumPoints() or 0
		for i = 1, n do
			local point, rel, relPoint, x, y = r:GetPoint(i)
			base.points[i] = { point, rel, relPoint, x or 0, y or 0 }
		end
	end
	if need.Scale then
		if r.GetScale and r.SetScale then
			base.scale = r:GetScale()
		elseif r.GetWidth then
			base.width, base.height = r:GetWidth(), r:GetHeight()
		end
	end
	if need.Rotation and r.SetRotation then
		base.rotation = r.GetRotation and r:GetRotation() or 0
	end
	group._base = base
end

local function setPoints(r, points, dx, dy)
	r:ClearAllPoints()
	for _, p in ipairs(points) do
		r:SetPoint(p[1], p[2], p[3], p[4] + dx, p[5] + dy)
	end
end

local function applyEffects(group)
	local r, base = group._region, group._base
	if not base then return end
	local alpha, dx, dy, sx, sy, rot = 0, 0, 0, 1, 1, 0
	local hasAlpha, hasMove, hasScale, hasRot
	local origin
	for _, a in ipairs(group._anims) do
		local t = a._type
		if t ~= "Animation" then
			local p = SMOOTHING[a._smoothing](a._progress)
			if t == "Alpha" then
				alpha = alpha + a._change * p
				hasAlpha = true
			elseif t == "Translation" then
				dx = dx + a._offsetX * p
				dy = dy + a._offsetY * p
				hasMove = true
			elseif t == "Scale" then
				sx = sx * (1 + (a._scaleX - 1) * p)
				sy = sy * (1 + (a._scaleY - 1) * p)
				hasScale = true
			elseif t == "Rotation" then
				rot = rot + a._radians * p
				origin = origin or a._origin
				hasRot = true
			end
		end
	end
	if hasAlpha and base.alpha then
		r:SetAlpha(clamp01(base.alpha + alpha))
	end
	if hasMove and base.points and table.getn(base.points) > 0 then
		setPoints(r, base.points, dx, dy)
	end
	if hasScale then
		if base.scale then
			local s = (sx + sy) / 2
			if s <= 0 then s = 0.001 end
			r:SetScale(base.scale * s)
		elseif base.width and base.points and table.getn(base.points) == 1 then
			r:SetWidth(math.max(0.001, base.width * sx))
			r:SetHeight(math.max(0.001, base.height * sy))
		end
	end
	if hasRot and base.rotation then
		local uv = ORIGIN_UV[origin and origin.point or "CENTER"] or ORIGIN_UV.CENTER
		r:SetRotation(base.rotation + rot, uv[1], uv[2])
	end
end

local function restoreBase(group)
	local r, base = group._region, group._base
	if not base then return end
	if base.alpha then r:SetAlpha(base.alpha) end
	if base.points and table.getn(base.points) > 0 then setPoints(r, base.points, 0, 0) end
	if base.scale then
		r:SetScale(base.scale)
	elseif base.width then
		r:SetWidth(base.width)
		r:SetHeight(base.height)
	end
	if base.rotation then r:SetRotation(base.rotation) end
	group._base = nil
end

local function removePlaying(group)
	for i, g in ipairs(playing) do
		if g == group then
			table.remove(playing, i)
			break
		end
	end
	if table.getn(playing) == 0 then driver:Hide() end
end

local function resetAnims(group)
	for _, a in ipairs(group._anims) do
		a._phase = "WAIT"
		a._progress = 0
		a._elapsed = 0
		a._accum = 0
	end
end

function Group:Play()
	if self._state == "PLAYING" then return end
	if self._state == "PAUSED" then
		self._state = "PLAYING"
		driver:Show()
		return
	end
	self:_Layout()
	resetAnims(self)
	captureBase(self)
	self._t = 0
	self._reverse = false
	self._finishing = false
	self._done = false
	self._loopState = "NONE"
	self._state = "PLAYING"
	table.insert(playing, self)
	driver:Show()
	fire(self, "OnPlay")
end

function Group:Pause()
	if self._state ~= "PLAYING" then return end
	self._state = "PAUSED"
	fire(self, "OnPause")
	for _, a in ipairs(self._anims) do
		if a._phase == "PLAY" or a._phase == "DELAY" then fire(a, "OnPause") end
	end
end

-- Ends the group: animations first, then the group, then the region's own
-- state comes back. `stopped` distinguishes Stop() from a natural finish.
local function endGroup(group, stopped)
	for _, a in ipairs(group._anims) do
		if a._phase ~= "WAIT" and a._phase ~= "DONE" then
			fire(a, stopped and "OnStop" or "OnFinished", stopped)
		end
		a._phase = "DONE"
	end
	group._state = "STOPPED"
	group._finishing = false
	group._done = not stopped
	removePlaying(group)
	restoreBase(group)
	if stopped then
		fire(group, "OnStop", true)
	else
		fire(group, "OnFinished", false)
	end
end

function Group:Stop()
	if self._state == "STOPPED" then return end
	endGroup(self, true)
end

-- Stop after the current pass instead of looping.
function Group:Finish()
	if self._state == "STOPPED" then return end
	self._finishing = true
end

-- Advances one animation to group time `t` (already direction-adjusted).
local function stepAnimation(a, t, elapsed)
	local phase
	if t < a._segStart then
		phase = "WAIT"
	elseif t < a._playStart then
		phase = "DELAY"
	elseif t < a._playEnd then
		phase = "PLAY"
	else
		phase = "DONE"
	end

	local progress
	if t < a._playStart then
		progress = 0
	elseif a._duration <= 0 or t >= a._playEnd then
		progress = 1
	else
		progress = (t - a._playStart) / a._duration
	end
	a._progress = progress

	local was = a._phase
	if was == "WAIT" and phase ~= "WAIT" then
		fire(a, "OnPlay")
	end
	if phase == "PLAY" or (phase == "DONE" and was ~= "DONE") then
		a._elapsed = math.max(0, t - a._playStart)
		a._accum = a._accum + elapsed
		local minStep = a._maxFramerate > 0 and (1 / a._maxFramerate) or 0
		if phase == "DONE" or a._accum >= minStep then
			local step = a._accum
			a._accum = 0
			fire(a, "OnUpdate", step)
		end
	end
	a._phase = phase
	if phase == "DONE" and was ~= "DONE" then
		fire(a, "OnFinished", false)
	end
end

local function tick(group, elapsed)
	if group._state ~= "PLAYING" then return end
	group._t = group._t + elapsed
	local total = group._total
	local ended = group._t >= total
	local t = ended and total or group._t
	local te = group._reverse and (total - t) or t

	for _, a in ipairs(group._anims) do
		if group._state ~= "PLAYING" then return end -- a script stopped the group
		stepAnimation(a, te, elapsed)
	end
	if group._state ~= "PLAYING" then return end
	applyEffects(group)
	fire(group, "OnUpdate", elapsed)
	if group._state ~= "PLAYING" or not ended then return end

	if group._looping == "NONE" or group._finishing then
		endGroup(group, false)
		return
	end
	-- Next pass: REPEAT restarts forward, BOUNCE turns around.
	group._t = total > 0 and math.mod(group._t - total, total) or 0
	if group._looping == "BOUNCE" then
		group._reverse = not group._reverse
	end
	resetAnims(group)
	group._loopState = group._reverse and "REVERSE" or "FORWARD"
	fire(group, "OnLoop", group._loopState)
end

driver:SetScript("OnUpdate", function()
	local elapsed = arg1 or 0
	local snapshot = {}
	for i, g in ipairs(playing) do snapshot[i] = g end
	for _, g in ipairs(snapshot) do
		tick(g, elapsed)
	end
end)

-- ---------------------------------------------------------------------------
-- Region side (reached through the C++ region methods)
-- ---------------------------------------------------------------------------

local groupsByRegion = {}

function Runtime.CreateGroup(region, name, template)
	local g = setmetatable({
		_region = region,
		_anims = {},
		_scripts = {},
		_looping = "NONE",
		_loopState = "NONE",
		_state = "STOPPED",
		_t = 0, _total = 0,
		_done = false, _finishing = false, _reverse = false,
	}, GroupMeta)
	g._name = resolveName(name, region)
	if g._name then setglobal(g._name, g) end
	local list = groupsByRegion[region]
	if not list then
		list = {}
		groupsByRegion[region] = list
	end
	table.insert(list, g)
	return g
end

function Runtime.GetGroups(region)
	local list = groupsByRegion[region]
	if list then return unpack(list) end
end

function Runtime.StopAll(region)
	local list = groupsByRegion[region]
	if not list then return end
	for _, g in ipairs(list) do g:Stop() end
end

-- ---------------------------------------------------------------------------
-- XML <Animations> (descriptors built by the C++ node loaders)
-- ---------------------------------------------------------------------------

local function compileScript(body, chunkName, scriptName)
	local params = SCRIPT_PARAMS[scriptName] or "self"
	local chunk, err = loadstring("return function(" .. params .. ", ...) " .. body .. "\nend", chunkName)
	if not chunk then
		report(err)
		return nil
	end
	return chunk()
end

local function applyScripts(obj, scripts, ownerName)
	if not scripts then return end
	for _, s in ipairs(scripts) do
		local fn
		if s.func and s.func ~= "" then
			fn = getglobal(s.func)
			if type(fn) ~= "function" then fn = nil end
		elseif s.body and string.find(s.body, "%S") then
			fn = compileScript(s.body, (ownerName or "<unnamed>") .. ":" .. s.name, s.name)
		end
		if fn then
			local ok, err = pcall(obj.SetScript, obj, s.name, fn)
			if not ok then report(err) end
		end
	end
end

local function num(v) return v and tonumber(v) end

local function buildAnimation(group, desc)
	local attr = desc.attr or {}
	local a = group:CreateAnimation(desc.tag, attr.name)
	if num(attr.duration) then a:SetDuration(num(attr.duration)) end
	if num(attr.startDelay) then a:SetStartDelay(num(attr.startDelay)) end
	if num(attr.endDelay) then a:SetEndDelay(num(attr.endDelay)) end
	if num(attr.order) then a:SetOrder(num(attr.order)) end
	if num(attr.maxFramerate) then a:SetMaxFramerate(num(attr.maxFramerate)) end
	if attr.smoothing then a:SetSmoothing(attr.smoothing) end
	if a.SetChange and num(attr.change) then a:SetChange(num(attr.change)) end
	if a.SetScale and (attr.scaleX or attr.scaleY) then
		a:SetScale(num(attr.scaleX) or 1, num(attr.scaleY) or num(attr.scaleX) or 1)
	end
	if a.SetDegrees and num(attr.degrees) then a:SetDegrees(num(attr.degrees)) end
	if a.SetRadians and num(attr.radians) then a:SetRadians(num(attr.radians)) end
	if a.SetOffset and (attr.offsetX or attr.offsetY) then
		a:SetOffset(num(attr.offsetX) or 0, num(attr.offsetY) or 0)
	end
	if a.SetOrigin and desc.origin then
		a:SetOrigin(desc.origin.point, desc.origin.x, desc.origin.y)
	end
	if attr.parentKey and attr.parentKey ~= "" then
		group[attr.parentKey] = a
	end
	applyScripts(a, desc.scripts, a:GetName() or group:GetName())
	return a
end

local function buildGroup(region, desc)
	local attr = desc.attr or {}
	local g = Runtime.CreateGroup(region, attr.name, attr.inherits)
	if attr.looping then g:SetLooping(attr.looping) end
	if attr.parentKey and attr.parentKey ~= "" then
		region[attr.parentKey] = g
	end
	for _, ad in ipairs(desc.anims or {}) do
		local ok, err = pcall(buildAnimation, g, ad)
		if not ok then report(err) end
	end
	applyScripts(g, desc.scripts, g:GetName() or (region.GetName and region:GetName()))
	return g
end

function Runtime.LoadXML(region, descriptors)
	for _, desc in ipairs(descriptors or {}) do
		local ok, err = pcall(buildGroup, region, desc)
		if not ok then report(err) end
	end
end

if _classicapi_SetAnimationRuntime then
	_classicapi_SetAnimationRuntime(Runtime)
end
