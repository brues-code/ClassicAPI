-- Backport of FrameXML/Pools.lua to vanilla 1.12 / Lua 5.0.
--
-- Implementation differences from the modern source:
--   - 1.12 has no taint/secure system, so the entire Secure*/Proxy
--     machinery (SecureTypes, ProxyUtil, securecallfunction,
--     CreateForbiddenFrame) is dropped. Only the Unsecured pools and
--     pool collections exist; the canonical `CreateObjectPool` /
--     `CreateFramePool` / etc. globals are aliased to them at the
--     bottom of the file.
--   - FrameUtil.SpecializeFrameWithMixins isn't a thing in 1.12; the
--     "table specialization" branch is inlined as a plain Mixin().
--   - CreateMaskTexture and CreateActor aren't in 1.12 — the
--     corresponding pool creators are kept as stubs so chunk-load of
--     consumer addons doesn't fail; they'll error at Acquire() time
--     if anything actually invokes them.
--   - Lua 5.0 has no `...` as expression; the converter mixins use
--     `unpack(arg)` to forward varargs (spans arg[1..arg.n] via getn;
--     this build's unpack ignores explicit i/j — the count lives in `.n`).
--   - assertsafe / nop aren't globals in stock 1.12; defined as local
--     fallbacks (same pattern as FunctionUtil.lua).

local function nop() end

local function assertsafe(condition, msg)
	if not condition then
		local h = geterrorhandler and geterrorhandler()
		if h then h(msg or "assertion failed!") end
	end
end

local ObjectPoolBaseMixin = {};

--[[
Reserve() is not exposed on the pool to prevent the attack vector of addons having control
over the quantity of objects available to a preexisting pool.
]]--
local function Reserve(pool, capacity)
	pool.capacity = capacity or math.huge;

	if pool.capacity ~= math.huge then
		for index = 1, pool.capacity do
			pool:Acquire();
		end
		pool:ReleaseAll();
	end
end

local function GetObjectIsInvalidMsg(object, poolOrCollection)
	if poolOrCollection:DoesObjectBelongToPool(object) then
		return string.format("Attempted to release inactive object '%s'", tostring(object));
	else
		return string.format("Attempted to release object '%s' that doesn't belong to this pool", tostring(object));
	end
end

function ObjectPoolBaseMixin:Acquire()
	if self:GetNumActive() == self.capacity then
		return nil, false;
	end

	local object = self:PopInactiveObject();
	local new = object == nil;
	if new then
		object = self:CallCreate();

		--[[
		While pools don't necessarily need to only contain tables, support for other types
		has not been tested, and therefore isn't allowed until we can justify a use for them.
		]]--
		assert(type(object) == "table");

		self:CallReset(object, new);
	end

	self:AddObject(object);
	return object, new;
end

function ObjectPoolBaseMixin:Release(object, canFailToFindObject)
	local active = self:IsActive(object);

	--[[
	If Release() is called on a pool directly from external code, then we expect
	an assert if the object is not found. However, if it was called from a pool
	collection, the object not being active is expected as the pool collection iterates
	all the pools until it is found. A separate assert in pool collections accounts for
	the case where the object was not found in any pool.
	]]--
	if not canFailToFindObject then
		assertsafe(active, GetObjectIsInvalidMsg(object, self));
	end

	if active then
		self:CallReset(object);
		self:ReclaimObject(object);
	end

	return active;
end

function ObjectPoolBaseMixin:Dump()
	for index, object in self:EnumerateActive() do
		print(tostring(object));
	end
end

local ObjectPoolMixin = CreateFromMixins(ObjectPoolBaseMixin);

function ObjectPoolMixin:Init(createFunc, resetFunc, capacity)
	self.createFunc = createFunc;
	self.resetFunc = resetFunc;
	self.activeObjects = {};
	self.inactiveObjects = {};
	self.activeObjectCount = 0;

	Reserve(self, capacity);
end

function ObjectPoolMixin:CallReset(object, new)
	self.resetFunc(self, object, new);
end

function ObjectPoolMixin:CallCreate()
	-- The pool argument 'self' is passed only for addons already reliant on it.
	return self.createFunc(self);
end

function ObjectPoolMixin:PopInactiveObject()
	return tremove(self.inactiveObjects);
end

function ObjectPoolMixin:AddObject(object)
	local dummy = true;
	self.activeObjects[object] = dummy;
	self.activeObjectCount = self.activeObjectCount + 1;
end

function ObjectPoolMixin:ReclaimObject(object)
	tinsert(self.inactiveObjects, object);
	self.activeObjects[object] = nil;
	self.activeObjectCount = self.activeObjectCount - 1;
end

function ObjectPoolMixin:ReleaseAll()
	for object in pairs(self.activeObjects) do
		self:Release(object);
	end
end

function ObjectPoolMixin:EnumerateActive()
	return pairs(self.activeObjects);
end

function ObjectPoolMixin:GetNextActive(current)
	return next(self.activeObjects, current);
end

function ObjectPoolMixin:IsActive(object)
	return self.activeObjects[object] ~= nil;
end

function ObjectPoolMixin:DoesObjectBelongToPool(object)
	if self:IsActive(object) then
		return true;
	end

	for index, candidate in pairs(self.inactiveObjects) do
		if candidate == object then
			return true;
		end
	end

	return false;
end

function ObjectPoolMixin:GetNumActive()
	return self.activeObjectCount;
end

local function GetPoolKey(template, specialization)
	if specialization == nil then
		return template.."nil";
	end
	return template..tostring(specialization);
end

local PoolCollectionBaseMixin = {};

function PoolCollectionBaseMixin:GetPool(template, specialization)
	local poolKey = GetPoolKey(template, specialization);
	return self:GetPoolByKey(poolKey);
end

function PoolCollectionBaseMixin:HasPool(poolKey)
	return self:GetPoolByKey(poolKey) ~= nil;
end

function PoolCollectionBaseMixin:Acquire(template, specialization)
	local pool = self:GetPool(template, specialization);
	return pool:Acquire();
end

function PoolCollectionBaseMixin:ReleaseAllByTemplate(template, specialization)
	local pool = self:GetPool(template, specialization);
	if pool then
		pool:ReleaseAll();
	end
end

function PoolCollectionBaseMixin:EnumerateActiveByTemplate(template, specialization)
	local pool = self:GetPool(template, specialization);
	if pool then
		return pool:EnumerateActive();
	end

	return nop;
end

function PoolCollectionBaseMixin:GetOrCreatePool(...)
	assert(false);
end

function PoolCollectionBaseMixin:CreatePool(...)
	assert(false);
end

function PoolCollectionBaseMixin:CreatePoolWithArgs(args)
	local poolKey = self:CreatePoolKeyFromPoolArgs(args);
	assert(not self:HasPool(poolKey));

	local pool = self:CreatePoolInternal(args);
	self:SetPool(poolKey, pool);
	return pool;
end

function PoolCollectionBaseMixin:GetOrCreatePoolWithArgs(args)
	local poolKey = self:CreatePoolKeyFromPoolArgs(args);
	local pool = self:GetPoolByKey(poolKey);
	local new = pool == nil;
	if new then
		pool = self:CreatePoolWithArgs(args);
	end
	return pool, new;
end

function PoolCollectionBaseMixin:Dump()
	for object in self:EnumerateActive() do
		print(tostring(object));
	end
end

local PoolCollectionMixin = CreateFromMixins(PoolCollectionBaseMixin);

function PoolCollectionMixin:Init()
	self.pools = {};
end

function PoolCollectionMixin:GetNumActive()
	local total = 0;
	for poolKey, pool in pairs(self.pools) do
		total = total + pool:GetNumActive();
	end
	return total;
end

function PoolCollectionMixin:SetPool(poolKey, pool)
	self.pools[poolKey] = pool;
end

function PoolCollectionMixin:GetPoolByKey(poolKey)
	return self.pools[poolKey];
end

function PoolCollectionMixin:Release(object)
	local canFailToFindObject = true;
	for poolKey, pool in pairs(self.pools) do
		if pool:Release(object, canFailToFindObject) then
			return;
		end
	end
	assertsafe(false, GetObjectIsInvalidMsg(object, self));
end

function PoolCollectionMixin:DoesObjectBelongToPool(object)
	for poolKey, pool in pairs(self.pools) do
		if pool:DoesObjectBelongToPool(object) then
			return true;
		end
	end

	return false;
end

function PoolCollectionMixin:ReleaseAll()
	for poolKey, pool in pairs(self.pools) do
		pool:ReleaseAll();
	end
end

-- Warning: this function only returns the object, unlike a pool that also returns the key.
function PoolCollectionMixin:EnumerateActive()
	local currentObject = nil;
	local currentPoolKey, currentPool = next(self.pools, currentObject);
	return function()
		if currentPool then
			currentObject = currentPool:GetNextActive(currentObject);
			while not currentObject do
				currentPoolKey, currentPool = next(self.pools, currentPoolKey)
				if currentPool then
					currentObject = currentPool:GetNextActive(nil);
				else
					break;
				end
			end
		end

		return currentObject;
	end, nil;
end

function Pool_HideAndClearAnchors(pool, region)
	region:Hide();
	region:ClearAllPoints();
end

function ActorPool_HideAndClearModel(actorPool, actor)
	actor:ClearModel();
	actor:Hide();
end

--[[
In addition to a specialization being used to create separate pools for the same template, it can
be used to modify a frame after being created. On modern WoW this delegates to
FrameUtil.SpecializeFrameWithMixins (which also wires up conventional script handlers); on 1.12
we just Mixin() the table into the frame and let the consumer call SetScript() if needed.
]]--
local function ConvertSpecializationToPostCreate(specialization)
	local specializationType = type(specialization);
	if specializationType == "function" then
		return specialization;
	elseif specializationType == "table" then
		local function Initializer(frame)
			Mixin(frame, specialization);
		end
		return Initializer;
	end

	return nil;
end

function CreateUnsecuredRegionPoolInstance(template, createFunc, resetFunc, capacity)
	local pool = CreateFromMixins(ObjectPoolMixin);
	pool:Init(createFunc, resetFunc or Pool_HideAndClearAnchors, capacity);
	pool.GetTemplate = function(self)
		return template;
	end

	return pool;
end

--[[
Args are packed into a table for ease of implementation in PoolCollectionBaseMixin, which is
derived with different argument signatures. This also makes it easy to access named fields
like `parent` off the table without dealing with argument position.
]]--
local function FramePoolCollection_ArgsToTable(frameType, parent, template, resetFunc, forbidden, specialization, capacity)
	return {
		frameType = frameType,
		parent = parent,
		template = template,
		resetFunc = resetFunc,
		forbidden = forbidden,        -- ignored on 1.12 (no CreateForbiddenFrame)
		specialization = specialization,
		capacity = capacity,
	};
end

local FramePoolCollectionConverterMixin = {};

function FramePoolCollectionConverterMixin:CreatePoolKeyFromPoolArgs(args)
	return GetPoolKey(args.template, args.specialization);
end

function FramePoolCollectionConverterMixin:GetOrCreatePool(...)
	local args = FramePoolCollection_ArgsToTable(unpack(arg));
	return self:GetOrCreatePoolWithArgs(args);
end

function FramePoolCollectionConverterMixin:CreatePool(...)
	local args = FramePoolCollection_ArgsToTable(unpack(arg));
	return self:CreatePoolWithArgs(args);
end

local FramePoolCollectionMixin = CreateFromMixins(PoolCollectionMixin, FramePoolCollectionConverterMixin);

do
	local function CreateFramePoolInstance(frameType, parent, template, resetFunc, postCreate, capacity)
		local function Create()
			local name = nil;
			local frame = CreateFrame(frameType, name, parent, template);

			if postCreate then
				postCreate(frame);
			end

			return frame;
		end

		return CreateUnsecuredRegionPoolInstance(template, Create, resetFunc, capacity);
	end

	function FramePoolCollectionMixin:CreatePoolInternal(args)
		local postCreate = ConvertSpecializationToPostCreate(args.specialization);
		return CreateFramePoolInstance(args.frameType, args.parent, args.template, args.resetFunc, postCreate, args.capacity);
	end
end

local FontStringPoolCollectionMixin = CreateFromMixins(PoolCollectionMixin);

function FontStringPoolCollectionMixin:CreatePoolKeyFromPoolArgs(args)
	return args.template;
end

local function FontStringPoolCollection_ArgsToTable(parent, layer, subLayer, template, resetFunc, capacity)
	return {
		parent = parent,
		layer = layer,
		subLayer = subLayer,
		template = template,
		resetFunc = resetFunc,
		capacity = capacity,
	};
end

function FontStringPoolCollectionMixin:GetOrCreatePool(...)
	local args = FontStringPoolCollection_ArgsToTable(unpack(arg));
	return self:GetOrCreatePoolWithArgs(args);
end

function FontStringPoolCollectionMixin:CreatePool(...)
	local args = FontStringPoolCollection_ArgsToTable(unpack(arg));
	return self:CreatePoolWithArgs(args);
end

function FontStringPoolCollectionMixin:CreatePoolInternal(args)
	return CreateUnsecuredFontStringPool(args.parent, args.layer, args.subLayer, args.template, args.resetFunc, args.capacity);
end

function CreateUnsecuredObjectPool(createFunc, resetFunc, capacity)
	local pool = CreateFromMixins(ObjectPoolMixin);
	pool:Init(createFunc, resetFunc, capacity);
	return pool;
end

function CreateUnsecuredFramePool(frameType, parent, template, resetFunc, capacity)
	local function Create()
		local name = nil;
		return CreateFrame(frameType, name, parent, template);
	end
	return CreateUnsecuredRegionPoolInstance(template, Create, resetFunc, capacity);
end

function CreateUnsecuredTexturePool(parent, layer, subLayer, template, resetFunc, capacity)
	local function Create()
		local name = nil;
		return parent:CreateTexture(name, layer, template, subLayer);
	end
	return CreateUnsecuredRegionPoolInstance(template, Create, resetFunc, capacity);
end

function CreateUnsecuredFontStringPool(parent, layer, subLayer, template, resetFunc, capacity)
	local function Create()
		local name = nil;
		return parent:CreateFontString(name, layer, template, subLayer);
	end
	return CreateUnsecuredRegionPoolInstance(template, Create, resetFunc, capacity);
end

-- Stub: 1.12 has no CreateActor (model actors are Cataclysm+). Acquire()
-- will raise once it actually tries to call parent:CreateActor — kept so
-- that addon code merely referencing the symbol doesn't fail to load.
function CreateUnsecuredActorPool(parent, template, resetFunc, capacity)
	local function Create()
		local name = nil;
		return parent:CreateActor(name, template);
	end
	return CreateUnsecuredRegionPoolInstance(template, Create, resetFunc or ActorPool_HideAndClearModel, capacity);
end

-- Stub: 1.12 has no CreateMaskTexture (mask textures are post-vanilla).
-- Same caveat as CreateUnsecuredActorPool.
function CreateUnsecuredMaskTexturePool(parent, layer, subLayer, template, resetFunc, capacity)
	local function Create()
		local name = nil;
		return parent:CreateMaskTexture(name, layer, template, subLayer);
	end
	return CreateUnsecuredRegionPoolInstance(template, Create, resetFunc, capacity);
end

function CreateUnsecuredFramePoolCollection()
	local poolCollection = CreateFromMixins(FramePoolCollectionMixin);
	poolCollection:Init();
	return poolCollection;
end

function CreateUnsecuredFontStringPoolCollection()
	local poolCollection = CreateFromMixins(FontStringPoolCollectionMixin);
	poolCollection:Init();
	return poolCollection;
end

-- Canonical API aliases. On modern WoW these point at the Secure variants;
-- on 1.12 there's no taint system, so they all go straight to the Unsecured
-- implementations.
CreateObjectPool = CreateUnsecuredObjectPool;
CreateFramePool = CreateUnsecuredFramePool;
CreateTexturePool = CreateUnsecuredTexturePool;
CreateFontStringPool = CreateUnsecuredFontStringPool;
CreateActorPool = CreateUnsecuredActorPool;
CreateFramePoolCollection = CreateUnsecuredFramePoolCollection;
CreateFontStringPoolCollection = CreateUnsecuredFontStringPoolCollection;
CreateMaskTexturePool = CreateUnsecuredMaskTexturePool;
