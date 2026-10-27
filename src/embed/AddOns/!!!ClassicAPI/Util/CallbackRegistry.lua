-- Backport of 3.3.5 FrameXML/CallbackRegistryMixin.lua to vanilla 1.12 / Lua 5.0.
--
-- Implementation differences from the 3.3.5 source:
--   - 1.12 has no taint or Secure-Templates system, so the entire
--     AttributeDelegate/SetForbidden machinery for inserting event keys
--     into the callback tables is unnecessary. Sub-tables are allocated
--     lazily inside RegisterCallback instead.
--   - securecallfunction/secureexecuterange don't exist; they reduce to
--     direct calls / explicit for-loops here.
--   - EnumUtil.MakeEnum is inlined as a plain string table.
--   - GenerateClosure doesn't exist in 1.12; provide a local one that
--     captures registration-time varargs via SafePack-style storage.
--   - Lua 5.0 has no select() or `wipe()`; we use the implicit `arg`
--     table and assign-nil to clear tables.

local generateOwnerID = CreateCounter()

local CallbackType = { Closure = "Closure", Function = "Function" }

-- Local GenerateClosure: binds `func` plus all of `...` so a later call
-- invokes func(boundArgs..., callArgs...). Mirrors 3.3.5's helper of the
-- same name. SafePack/SafeUnpack semantics so embedded nils survive.
local function GenerateClosure(func, ...)
    local boundN = arg.n
    local bound = {}
    for i = 1, boundN do bound[i] = arg[i] end
    return function(...)
        local callN = arg.n
        -- Forward bound + call args faithfully, embedded/leading nils and all.
        -- The ONLY thing that makes unpack span holes on this Lua 5.0 build is
        -- a `.n` field: unpack takes its count from getn (which returns t.n)
        -- and ignores any explicit i/j, so a `.n`-less table stops dead at the
        -- first hole (verified: unpack({1, 1, nil, 1}, 1, 4) yields just 1, 1).
        local all = {}
        local n = 0
        for i = 1, boundN do n = n + 1; all[n] = bound[i] end
        for i = 1, callN do n = n + 1; all[n] = arg[i] end
        all.n = n
        return func(unpack(all))
    end
end

CallbackRegistryMixin = CallbackRegistryMixin or {}

function CallbackRegistryMixin:OnLoad()
    local callbackTables = {}
    for _, value in pairs(CallbackType) do
        callbackTables[value] = {}
    end
    self.callbackTables = callbackTables
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

function CallbackRegistryMixin:HasRegistrantsForEvent(event)
    for _, callbackTable in pairs(self:GetCallbackTables()) do
        local callbacks = callbackTable[event]
        if callbacks and next(callbacks) then
            return true
        end
    end
    return false
end

-- Lazy allocation of per-event sub-tables. In 3.3.5 this went through a
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

    for _, callbackTable in pairs(self:GetCallbackTables()) do
        local callbacks = callbackTable[event]
        callbacks[owner] = nil
    end

    if arg.n > 0 then
        local callbacks = self:GetCallbacksByEvent(CallbackType.Closure, event)
        callbacks[owner] = GenerateClosure(func, owner, unpack(arg))
    else
        local callbacks = self:GetCallbacksByEvent(CallbackType.Function, event)
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

function CallbackRegistryMixin:TriggerEvent(event, ...)
    if type(event) ~= "string" then
        error("CallbackRegistryMixin:TriggerEvent 'event' requires string type.")
    elseif not self.isUndefinedEventAllowed and not (self.Event and self.Event[event]) then
        error(string.format("CallbackRegistryMixin:TriggerEvent event '%s' doesn't exist.", event))
    end

    local closures = self:GetCallbacksByEvent(CallbackType.Closure, event)
    if closures then
        for _, closure in pairs(closures) do
            closure(unpack(arg))
        end
    end

    local funcs = self:GetCallbacksByEvent(CallbackType.Function, event)
    if funcs then
        for owner, func in pairs(funcs) do
            -- Stock 3.3.5 passes the owner as the first arg so callers can
            -- distinguish multiple registrations of the same function. `arg`
            -- carries its own `.n`, so unpack(arg) forwards the payload with
            -- leading/embedded nils intact. Do NOT instead merge into a
            -- `{ owner, ... }` table and unpack that WITHOUT setting `.n`: this
            -- build's unpack counts via getn, so a `.n`-less table collapses at
            -- the first hole (verified in-game: reload's PLAYER_ENTERING_WORLD,
            -- arg1=nil arg2=1, dropped the payload).
            func(owner, unpack(arg))
        end
    end
end

function CallbackRegistryMixin:UnregisterCallback(event, owner)
    if type(event) ~= "string" then
        error("CallbackRegistryMixin:UnregisterCallback 'event' requires string type.")
    elseif owner == nil then
        error("CallbackRegistryMixin:UnregisterCallback 'owner' is required.")
    end

    for _, callbackTable in pairs(self:GetCallbackTables()) do
        local callbacks = callbackTable[event]
        if callbacks then
            callbacks[owner] = nil
        end
    end
end

function CallbackRegistryMixin:UnregisterEvents(eventTable)
    if eventTable then
        for _, callbackTable in pairs(self:GetCallbackTables()) do
            for event in pairs(eventTable) do
                if callbackTable[event] then
                    callbackTable[event] = nil
                end
            end
        end
    else
        for _, callbackTable in pairs(self:GetCallbackTables()) do
            for k in pairs(callbackTable) do
                callbackTable[k] = nil
            end
        end
    end
end

function CallbackRegistryMixin:GenerateCallbackEvents(events)
    if not self.Event then
        self.Event = {}
    end

    for eventIndex, eventName in ipairs(events) do
        if self.Event[eventName] then
            error(string.format("CallbackRegistryMixin:GenerateCallbackEvents: event '%s' already exists.", eventName))
        end
        self.Event[eventName] = eventName
    end
end

function CallbackRegistryMixin.DoesFrameHaveEvent(frame, event)
    return frame.Event and frame.Event[event]
end
