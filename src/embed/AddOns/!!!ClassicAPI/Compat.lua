if not _G then
    _G = getfenv(0)
end

-- Retail names for what this client already has. Retail's Deprecated files
-- alias the old globals to the C_ functions; here the old globals are the
-- implementations.
C_Container.GetContainerNumSlots = GetContainerNumSlots
C_Container.PickupContainerItem = PickupContainerItem

C_CVar.GetCVar = GetCVar
C_CVar.GetCVarDefault = GetCVarDefault
C_CVar.RegisterCVar = RegisterCVar

C_KeyBindings = C_KeyBindings or {}

-- This client's binding list holds each action once.
function C_KeyBindings.GetBindingIndex(action)
    for index = 1, GetNumBindings() do
        if GetBinding(index) == action then
            return index
        end
    end
    return nil
end

-- This client has one binding context, so no action has a context of its
-- own.
function C_KeyBindings.GetBindingContextForAction(action)
    return nil
end

-- This client has no custom binding types (retail's push-to-talk key).
function C_KeyBindings.GetCustomBindingType(bindingIndex)
    return nil
end

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

-- C_GossipInfo's window functions come from the DLL. This client has no
-- forced gossip, no option descriptions and no friendship factions. The
-- friendship answers are the ones the Classic client gives for a faction
-- without a friendship record.
function C_GossipInfo.ForceGossip()
    return false
end

function C_GossipInfo.GetCompletedOptionDescriptionString()
    return nil
end

function C_GossipInfo.GetCustomGossipDescriptionString()
    return nil
end

function C_GossipInfo.GetFriendshipReputation(friendshipFactionID)
    local id = tonumber(friendshipFactionID)
    if not id then
        error("Usage: local reputationInfo = C_GossipInfo.GetFriendshipReputation(friendshipFactionID)", 2)
    end
    if id <= 0 then
        return
    end
    return {
        friendshipFactionID = 0,
        standing = 0,
        maxRep = 0,
        text = "",
        texture = 0,
        reaction = "",
        reactionThreshold = 0,
    }
end

function C_GossipInfo.GetFriendshipReputationRanks(friendshipFactionID)
    if not tonumber(friendshipFactionID) then
        error("Usage: local rankInfo = C_GossipInfo.GetFriendshipReputationRanks(friendshipFactionID)", 2)
    end
    return { currentLevel = 0, maxLevel = 0 }
end

-- C_LootHistory's store comes from the DLL. This client rolls no master-loot
-- items, so no history item can be given out by the master looter.
function C_LootHistory.CanMasterLoot(itemIdx, playerIdx)
    if not tonumber(itemIdx) or not tonumber(playerIdx) then
        error("Usage: CanMasterLoot(itemIdx, playerIdx)", 2)
    end
end

function C_LootHistory.GiveMasterLoot(itemIdx, playerIdx)
    if not tonumber(itemIdx) or not tonumber(playerIdx) then
        error("Usage: GiveMasterLoot(itemIdx, playerIdx)", 2)
    end
end

-- C_ModelInfo's getters come from the DLL. Retail's active-scene bookkeeping
-- "does nothing in public clients" (its own documentation).
function C_ModelInfo.AddActiveModelScene(modelSceneFrame, modelSceneID)
end

function C_ModelInfo.AddActiveModelSceneActor(modelSceneFrameActor, modelSceneActorID)
end

function C_ModelInfo.ClearActiveModelScene(modelSceneFrame)
end

function C_ModelInfo.ClearActiveModelSceneActor(modelSceneFrameActor)
end

-- This client has no alternate player forms.
function C_PlayerInfo.GetAlternateFormInfo()
    return false, false
end

-- Retail marks the settings added in the current version as new. Nothing in
-- this client is new.
function IsNewSettingInCurrentVersion(variable)
    return false
end

function MarkNewSettingAsSeen(variable)
end

-- Classic clients before Wrath answer true: combo points stay on the player
-- in a vehicle. This client has no vehicles.
function UnitIgnoresVehicleComboPoints(unit)
    return true
end
