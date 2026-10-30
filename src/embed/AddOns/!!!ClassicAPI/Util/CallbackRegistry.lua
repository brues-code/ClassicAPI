-- Backport of retail Blizzard_SharedXMLBase/CallbackRegistry.lua to vanilla
-- 1.12 / Lua 5.0.
--
-- Implementation differences from the retail source:
--   - 1.12 has no taint or Secure-Templates system, so the entire
--     AttributeDelegate/SetForbidden machinery for inserting event keys
--     into the callback tables and creating deferred callback tables is
--     unnecessary. Both are plain assignments here.
--   - TriggerEvent runs each callback through securecallfunction straight
--     from its own loop, where retail passes a vararg helper to
--     secureexecuterange. Same contract (an error goes to the error handler
--     and the remaining callbacks still run), but a vararg Lua function
--     allocates an `arg` table on every call in Lua 5.0, which would cost a
--     table per callback per event.

local securecallfunction = securecallfunction
local wipe = table.wipe

local generateOwnerID = CreateCounter()

local CallbackType = EnumUtil.MakeEnum("Closure", "Function")

CallbackRegistryMixin = CallbackRegistryMixin or {}

function CallbackRegistryMixin:OnLoad()
    local callbackTables = {}
    for _, value in pairs(CallbackType) do
        callbackTables[value] = {}
    end
    self.callbackTables = callbackTables

    self.executingEvents = {}
    self.deferredCallbacks = {}
end

function CallbackRegistryMixin:SetUndefinedEventsAllowed(allowed)
    self.isUndefinedEventAllowed = allowed
end

function CallbackRegistryMixin:GetCallbackTables()
    return self.callbackTables
end

function CallbackRegistryMixin:GetCallbackTable(callbackType)
    return self.callbackTables[callbackType]
end

function CallbackRegistryMixin:GetCallbacksByEvent(callbackType, event)
    local callbackTable = self:GetCallbackTable(callbackType)
    return callbackTable[event]
end

-- Returns either the underlying callback table or a temporary table depending
-- on if the event is currently being dispatched. The callbacks stored in the
-- temporary table are merged into the underlying callback table after the
-- dispatch is complete, so a registration never adds a key to a table that
-- TriggerEvent's pairs loop is walking.
function CallbackRegistryMixin:GetMutableCallbacksByEvent(callbackType, event)
    if self.executingEvents[event] == nil then
        return self:GetCallbacksByEvent(callbackType, event)
    end

    local deferrals = GetOrCreateTableEntry(self.deferredCallbacks, event)
    return GetOrCreateTableEntry(deferrals, callbackType)
end

function CallbackRegistryMixin:HasRegistrantsForEvent(event)
    for _, callbackTable in pairs(self:GetCallbackTables()) do
        local callbacks = callbackTable[event]
        if callbacks and next(callbacks) then
            return true
        end
    end
    return false
end

-- Lazy allocation of per-event sub-tables. On retail this goes through a
-- forbidden attribute frame for taint-isolation reasons; that's a no-op
-- consideration on 1.12, so we just assign here.
function CallbackRegistryMixin:SecureInsertEvent(event)
    for _, callbackTable in pairs(self:GetCallbackTables()) do
        if not callbackTable[event] then
            callbackTable[event] = {}
        end
    end
end

function CallbackRegistryMixin:RegisterCallback(event, func, owner, ...)
    if type(event) ~= "string" then
        error("CallbackRegistryMixin::RegisterCallback 'event' requires string type.")
    elseif type(func) ~= "function" then
        error("CallbackRegistryMixin::RegisterCallback 'func' requires function type.")
    else
        if owner == nil then
            owner = generateOwnerID()
        elseif type(owner) == "number" then
            error("CallbackRegistryMixin:RegisterCallback 'owner' as number is reserved internally.")
        end
    end

    self:SecureInsertEvent(event)

    -- An owner can have a single callback per event. The simplest way to ensure
    -- this is to remove all callbacks for the owner prior to new registration.
    self:UnregisterCallback(event, owner)

    if arg.n > 0 then
        local callbacks = self:GetMutableCallbacksByEvent(CallbackType.Closure, event)
        callbacks[owner] = GenerateClosure(func, owner, unpack(arg))
    else
        local callbacks = self:GetMutableCallbacksByEvent(CallbackType.Function, event)
        callbacks[owner] = func
    end

    return owner
end

local function CreateCallbackHandle(cbr, event, owner)
    local handle =
    {
        Unregister = function()
            cbr:UnregisterCallback(event, owner)
        end,
    }
    return handle
end

function CallbackRegistryMixin:RegisterCallbackWithHandle(event, func, owner, ...)
    owner = self:RegisterCallback(event, func, owner, unpack(arg))
    return CreateCallbackHandle(self, event, owner)
end

function CallbackRegistryMixin:ReconcileDeferredCallbacks(event, closures, funcs)
    self:CopyDeferredCallbacks(CallbackType.Closure, event, closures)
    self:CopyDeferredCallbacks(CallbackType.Function, event, funcs)
    self.deferredCallbacks[event] = nil
end

function CallbackRegistryMixin:CopyDeferredCallbacks(callbackType, event, target)
    local deferrals = self.deferredCallbacks[event]
    if deferrals == nil then
        return
    end

    local callbacks = deferrals[callbackType]
    if callbacks == nil then
        return
    end

    for owner, callback in pairs(callbacks) do
        target[owner] = callback
    end
end

function CallbackRegistryMixin:TriggerEvent(event, ...)
    if type(event) ~= "string" then
        error("CallbackRegistryMixin:TriggerEvent 'event' requires string type.")
    elseif not self.isUndefinedEventAllowed and not (self.Event and self.Event[event]) then
        error(string.format("CallbackRegistryMixin:TriggerEvent event '%s' doesn't exist.", event))
    end

    -- TriggerEvent appears to need to support reentrant calls for now.
    local count = (self.executingEvents[event] or 0) + 1

    -- Set before invoking any callback so a reentrant call does not
    -- attempt to call ReconcileDeferredCallbacks.
    self.executingEvents[event] = count

    local closures = self:GetCallbacksByEvent(CallbackType.Closure, event)
    if closures then
        for _, closure in pairs(closures) do
            securecallfunction(closure, unpack(arg))
        end
    end

    local funcs = self:GetCallbacksByEvent(CallbackType.Function, event)
    if funcs then
        for owner, func in pairs(funcs) do
            -- Retail passes the owner as the first arg so callers can
            -- distinguish multiple registrations of the same function. `arg`
            -- carries its own `.n`, so unpack(arg) forwards the payload with
            -- leading/embedded nils intact. Do NOT instead merge into a
            -- `{ owner, ... }` table and unpack that WITHOUT setting `.n`: this
            -- build's unpack counts via getn, so a `.n`-less table collapses at
            -- the first hole (verified in-game: reload's PLAYER_ENTERING_WORLD,
            -- arg1=nil arg2=1, dropped the payload).
            securecallfunction(func, owner, unpack(arg))
        end
    end

    -- The local count is the only value we care about for the purpose
    -- of flushing the event key and reconciling the deferred callbacks.
    -- securecallfunction keeps a failing callback from skipping this.
    if count == 1 then
        self.executingEvents[event] = nil

        self:ReconcileDeferredCallbacks(event, closures, funcs)
    end
end

function CallbackRegistryMixin:UnregisterCallback(event, owner)
    if type(event) ~= "string" then
        error("CallbackRegistryMixin:UnregisterCallback 'event' requires string type.")
    elseif owner == nil then
        error("CallbackRegistryMixin:UnregisterCallback 'owner' is required.")
    end

    for _, callbackTable in pairs(self:GetCallbackTables()) do
        -- Only assign nil if the owner is present in the table, otherwise
        -- its insertion could cause a rehash during TriggerEvent's walk.
        local callbacks = callbackTable[event]
        if callbacks and callbacks[owner] then
            callbacks[owner] = nil
        end
    end

    local deferrals = self.deferredCallbacks[event]
    if deferrals then
        for _, callbacks in pairs(deferrals) do
            -- Freely assign nil because deferredCallbacks are never walked
            -- by a dispatch.
            callbacks[owner] = nil
        end
    end
end

function CallbackRegistryMixin:UnregisterEvents()
    for _, callbackTable in pairs(self:GetCallbackTables()) do
        wipe(callbackTable)
    end
end

function CallbackRegistryMixin:UnregisterEventsByEventTable(eventTable)
    if type(eventTable) ~= "table" then
        error("CallbackRegistryMixin:UnregisterEventsByEventTable 'eventTable' requires table type.")
    end

    for _, callbackTable in pairs(self:GetCallbackTables()) do
        for event in pairs(eventTable) do
            callbackTable[event] = nil
        end
    end
end

function CallbackRegistryMixin:GenerateCallbackEvents(eventTable)
    if not self.Event then
        self.Event = {}
    end

    if type(eventTable) ~= "table" then
        error("CallbackRegistryMixin:GenerateCallbackEvents 'eventTable' requires table type.")
    end

    for _, eventName in ipairs(eventTable) do
        if self.Event[eventName] then
            error(string.format("CallbackRegistryMixin:GenerateCallbackEvents: event '%s' already exists.", eventName))
        end
        self.Event[eventName] = eventName
    end
end

function CallbackRegistryMixin.DoesFrameHaveEvent(frame, event)
    return frame.Event and frame.Event[event]
end
