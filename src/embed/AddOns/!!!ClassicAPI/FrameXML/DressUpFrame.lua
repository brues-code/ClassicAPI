-- 4.3.4's DressUpItemLink (FrameXML/ModelFrames.lua): the whole link goes to
-- the model, so DressUpModel:TryOn shows the link's enchant glow, and an item
-- the model cannot wear does not open the window. 1.12.1's version passes
-- only the item ID.
--
-- Differences from the 4.3.4 source:
--   - The link goes through DressUpItem, which 4.3.4 folded in. Here
--     Blizzard_AuctionUI replaces DressUpItem to show its own model, and
--     some clients add steps to it (Turtle dresses the tabard).
--   - An uncached item skips the IsDressableItem test, which reads only the
--     cache. 1.12.1 opens no tooltip before a chat link's Control-click, so
--     the test would drop most chat links; TryOn loads the item instead.
--   - An item the model cannot wear goes on to DressUpMountLink, retail's
--     next step in DressUpLink: 1.12.1's ItemRef calls DressUpItemLink alone.
--   - It returns true when it showed the item or its mount, and false for a
--     link of another type, as retail's does.

function DressUpItemLink(link)
	if ( not link ) then
		return false;
	end
	local linkType = type(link) == "string" and LinkUtil.ExtractLink(link);
	if ( linkType and linkType ~= "item" ) then
		return false;
	end
	if ( C_Item.IsItemDataCachedByID(link) and not IsDressableItem(link) ) then
		return DressUpMountLink(link);
	end
	DressUpItem(link);
	return true;
end

-- Retail's DressUpLink (Mainline/DressUpFrames.lua), without battle pets.
function DressUpLink(link, forcedFrame)
	return link and (DressUpItemLink(link) or DressUpMountLink(link, forcedFrame));
end

-- Retail's mount preview (Mainline/DressUpFrames.lua DressUpMountLink and
-- DressUpMount, Blizzard_SharedXMLGame/DressUpModelFrameMixin.lua's mode and
-- last link) on 1.12.1's dressing room. Mount mode shows DressUpFrameModelScene
-- in place of DressUpModel and hides the Reset button, as retail's SetMode
-- hides it outside player mode; DressUpItem goes back to player mode.
--
-- Differences from the retail source:
--   - DressUpFrame keeps its own background (retail switches to the warrior
--     one) and has no view insets to set (retail sets them to 0, the default).
--   - There are no mount links, so a link never sets the model itself, and
--     shouldSetModelFromHyperlink changes nothing.
--   - A mount whose creature is not cached yet returns false, as retail does
--     with no display, and opens when CREATURE_DATA_LOAD_RESULT reports the
--     creature: retail has every display on disk, this client asks the server.

DressUpFrame.ModelScene = DressUpFrameModelScene;
DressUpFrame.ModelScene.ControlFrame:SetModelScene(DressUpFrame.ModelScene);

function DressUpFrame:GetLastLink()
	return self.lastLink;
end

function DressUpFrame:SetLastLink(link)
	self.lastLink = link;
end

function DressUpFrame:GetMode()
	return self.mode;
end

function DressUpFrame:SetMode(mode)
	self.mode = mode;
	local inPlayerMode = mode ~= "mount";
	DressUpModel:SetShown(inPlayerMode);
	DressUpFrameResetButton:SetShown(inPlayerMode);
	self.ModelScene:SetShown(not inPlayerMode);
end

DressUpFrame.ModelScene:SetResetCallback(function()
	local link = DressUpFrame:GetLastLink();
	if link then
		DressUpLink(link, DressUpFrame);
	end
end);

-- The stock DressUpItem sets the unit only when it opens the window, and the
-- model was hidden while a mount showed.
local originalDressUpItem = DressUpItem;
function DressUpItem(item)
	if ( DressUpFrame:GetMode() == "mount" ) then
		DressUpFrame:SetMode("player");
		DressUpFrame:SetLastLink(nil);
		if ( DressUpFrame:IsVisible() ) then
			DressUpModel:SetUnit("player");
		end
	end
	return originalDressUpItem(item);
end

function DressUpMountLink(link, forcedFrame)
	if( link ) then
		local mountID = 0;

		local linkType, linkOptions = LinkUtil.ExtractLink(link);
		if linkType == "item" then
			local linkID = LinkUtil.SplitLinkOptions(linkOptions);
			mountID = C_MountJournal.GetMountFromItem(tonumber(linkID));
		elseif linkType == "spell" then
			local linkID = LinkUtil.SplitLinkOptions(linkOptions);
			mountID = C_MountJournal.GetMountFromSpell(tonumber(linkID));
		end

		if ( mountID ) then
			return DressUpMount(mountID, forcedFrame, false, link);
		end
	end
	return false
end

local pendingMount;

function DressUpMount(mountID, forcedFrame, shouldSetModelFromHyperlink, link)
	if ( not mountID or mountID == 0 ) then
		return false;
	end

	local creatureDisplayID, _, _, isSelfMount, _, modelSceneID, animID, spellVisualKitID, disablePlayerMountPreview = C_MountJournal.GetMountInfoExtraByID(mountID);
	if ( not creatureDisplayID ) then
		pendingMount = { mountID = mountID, forcedFrame = forcedFrame, link = link };
		return false;
	end
	pendingMount = nil;

	local frame = forcedFrame or DressUpFrame;

	--Show the frame
	if ( not frame:IsShown() or frame:GetMode() ~= "mount" ) then
		frame:SetMode("mount");
		ShowUIPanel(frame);
	end
	frame:SetLastLink(link);
	frame.ModelScene:ClearScene();
	local forceEvenIfSame = true;
	frame.ModelScene:TransitionToModelSceneID(modelSceneID, CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD, forceEvenIfSame);

	local mountActor = frame.ModelScene:GetActorByTag("unwrapped");
	if mountActor then
		mountActor:SetModelByCreatureDisplayID(creatureDisplayID);

		-- mount self idle animation
		if (isSelfMount) then
			mountActor:SetAnimationBlendOperation(Enum.ModelBlendOperation.None);
			mountActor:SetAnimation(618); -- MountSelfIdle
		else
			mountActor:SetAnimationBlendOperation(Enum.ModelBlendOperation.Anim);
			mountActor:SetAnimation(0);
		end
		frame.ModelScene:AttachPlayerToMount(mountActor, animID, isSelfMount, disablePlayerMountPreview);
	end
	return true;
end

local creatureLoadFrame = CreateFrame("Frame");
creatureLoadFrame:RegisterEvent("CREATURE_DATA_LOAD_RESULT");
creatureLoadFrame:SetScript("OnEvent", function(self, event, creatureID, success)
	local pending = pendingMount;
	pendingMount = nil;
	if ( pending and success ) then
		DressUpMount(pending.mountID, pending.forcedFrame, false, pending.link);
	end
end);

-- Control-click on a mount in the spellbook previews it. Retail has no such
-- click (its mounts are in the mount journal); this client keeps mounts in
-- the spellbook, where stock 1.12.1 casts on a Control-click. A mount spell
-- never casts here, even while its creature loads; any other spell does.
local originalSpellButtonOnClick = SpellButton_OnClick;
function SpellButton_OnClick(drag)
	if ( not drag and IsControlKeyDown() and SpellBookFrame.bookType ~= BOOKTYPE_PET ) then
		local id = SpellBook_GetSpellID(this:GetID());
		local link = id <= MAX_SPELLS and GetSpellLink(id, SpellBookFrame.bookType);
		if ( link and C_MountJournal.GetMountFromSpell(link) ) then
			this:SetChecked("false");
			DressUpMountLink(link);
			return;
		end
	end
	return originalSpellButtonOnClick(drag);
end
