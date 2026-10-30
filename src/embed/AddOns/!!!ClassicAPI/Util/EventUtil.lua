-- Backport of retail Blizzard_SharedXML/EventUtil.lua to vanilla 1.12 / Lua 5.0.
--
-- Implementation differences from the retail source:
--   - Retail's UIParent sets UIParent.variablesLoaded and calls
--     EventUtil.TriggerOnVariablesLoaded on VARIABLES_LOADED. 1.12's
--     FrameXML does neither, so the frame at the bottom of this file does
--     both.
--   - Varargs are read from the implicit `arg` table, which every Lua 5.0
--     vararg function already has, rather than walked with select().

EventUtil = {}

local ContinueAfterAllEventsMixin = {}

function ContinueAfterAllEventsMixin:Init(callback, ...)
    self.events = {}

    for index = 1, arg.n do
        local event = arg[index]
        assert(C_EventUtils.IsEventValid(event), ("Unknown event \"%s\""):format(event))
        self.events[event] = false

        local function OnEventReceived()
            self.events[event] = true

            EventRegistry:UnregisterFrameEventAndCallback(event, self)

            if self:HaveReceivedAllEvents() then
                callback()
            end
        end

        EventRegistry:RegisterFrameEventAndCallback(event, OnEventReceived, self)
    end
end

function ContinueAfterAllEventsMixin:HaveReceivedAllEvents()
    for event, received in pairs(self.events) do
        if not received then
            return false
        end
    end
    return true
end

function EventUtil.ContinueAfterAllEvents(callback, ...)
    local obj = CreateFromMixins(ContinueAfterAllEventsMixin)
    obj:Init(callback, unpack(arg))
end

function EventUtil.AreVariablesLoaded()
    return GlueParent or (UIParent and UIParent.variablesLoaded)
end

local eventUtilVariablesLoadedCallbacks = {}
function EventUtil.ContinueOnVariablesLoaded(callback)
    if EventUtil.AreVariablesLoaded() then
        callback()
        return
    end

    table.insert(eventUtilVariablesLoadedCallbacks, callback)
end

local secureexecuterange = secureexecuterange
local eventUtilContinueOnVariablesLoadedTriggered = false
function EventUtil.TriggerOnVariablesLoaded()
    if eventUtilContinueOnVariablesLoadedTriggered then
        -- If TriggerOnVariablesLoaded has been called once already, don't do anything further.
        return
    end
    eventUtilContinueOnVariablesLoadedTriggered = true

    secureexecuterange(eventUtilVariablesLoadedCallbacks, function(_, callback)
        callback()
    end)
end

function EventUtil.ContinueOnAddOnLoaded(addOnName, callback)
    local isLoadedOrLoading, isLoaded = C_AddOns.IsAddOnLoaded(addOnName)
    if isLoaded then
        callback()
        return
    end

    EventUtil.RegisterOnceFrameEventAndCallback("ADDON_LOADED", callback, addOnName)
end

function EventUtil.ContinueOnPlayerLogin(callback)
    if IsLoggedIn() then
        callback()
        return
    end

    EventUtil.RegisterOnceFrameEventAndCallback("PLAYER_LOGIN", callback)
end

-- `...` are optional event arguments that must match before the callback fires.
function EventUtil.RegisterOnceFrameEventAndCallback(frameEvent, callback, ...)
    local handle = nil
    local requiredEventArgs = SafePack(unpack(arg))
    local CallbackWrapper = function(callbackHandlerID, ...)
        for i = 1, requiredEventArgs.n do
            if arg[i] ~= requiredEventArgs[i] then
                return
            end
        end

        handle:Unregister()
        callback(unpack(arg))
    end

    handle = EventRegistry:RegisterFrameEventAndCallbackWithHandle(frameEvent, CallbackWrapper)
end

CallbackHandleContainerMixin = {}

function CallbackHandleContainerMixin:Init()
    self.handles = {}
end

-- Accepts any CBR and stores a handle to the registered callback.
function CallbackHandleContainerMixin:RegisterCallback(cbr, event, callback, owner)
    self:AddHandle(cbr:RegisterCallbackWithHandle(event, callback, owner))
end

function CallbackHandleContainerMixin:AddHandle(handle)
    table.insert(self.handles, handle)
end

function CallbackHandleContainerMixin:Unregister()
    for index, handle in ipairs(self.handles) do
        handle:Unregister()
    end
    self.handles = {}
end

function CallbackHandleContainerMixin:IsEmpty()
    return #self.handles == 0
end

function EventUtil.CreateCallbackHandleContainer()
    local cbrHandles = CreateFromMixins(CallbackHandleContainerMixin)
    cbrHandles:Init()
    return cbrHandles
end

-- Retail UIParent's VARIABLES_LOADED work for EventUtil: mark the variables
-- loaded, then run what ContinueOnVariablesLoaded queued.
do
    local f = CreateFrame("Frame")
    f:RegisterEvent("VARIABLES_LOADED")
    f:SetScript("OnEvent", function()
        UIParent.variablesLoaded = true
        EventUtil.TriggerOnVariablesLoaded()
        f:UnregisterEvent("VARIABLES_LOADED")
    end)
end
