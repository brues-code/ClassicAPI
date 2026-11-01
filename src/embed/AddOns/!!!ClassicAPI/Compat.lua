if not _G then
    _G = getfenv(0)
end

-- Retail names for what this client already has. Retail's Deprecated files
-- alias the old globals to the C_ functions; here the old globals are the
-- implementations.
C_Container.GetContainerNumSlots = GetContainerNumSlots
C_Container.PickupContainerItem = PickupContainerItem

C_ActionBar = C_ActionBar or {}

-- Retail answers per game type. This client always has a keyring.
function C_ActionBar.ShouldShowKeyring()
    return true
end

-- C_AutoComplete's names come from the DLL. This client has no Battle.net
-- friends to give a presence ID for, and no other realm a name can address.
function C_AutoComplete.GetAutoCompletePresenceID(name)
    return nil
end

function C_AutoComplete.GetAutoCompleteRealms()
    return {}
end
