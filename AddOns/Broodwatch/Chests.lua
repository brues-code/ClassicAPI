-- Broodwatch chests: an elite's reward, opened by walking up to it.

local _, BW = ...
local CHESTS = BW.CHESTS
local CHEST_SOUND = BW.CHEST_SOUND
local GOLDEN_EVERY = BW.GOLDEN_EVERY
local TUNING = BW.TUNING
local Dist2 = BW.Dist2
local FloorAt = BW.FloorAt
local ShowCards = BW.ShowCards

local ANIM_CLOSED = 147
local ANIM_OPEN = 148
local ANIM_OPENED = 149

local chests = {}
local chestPool = {}

------------------------------------------------------------------------------
-- A chest lies closed (Closed 147) under a reticle until the hero comes
-- within CHEST_REACH. Then it opens (Open 148), and when the lid is up it
-- holds Opened (149), gives its picks, stays CHEST_LINGER seconds and fades.
-- The open runs on the scene's clock, so a pause holds it.

BW.DropChest = function(x, y, z)
	BW.run.chests = BW.run.chests + 1;
	local kind = math.mod(BW.run.chests, GOLDEN_EVERY) == 0 and CHESTS.golden or CHESTS.plain;
	local chest = table.remove(chestPool);
	if not chest then
		chest = { actor = BW.scene:AcquireActor() };
		chest.actor:SetAnimationBlendOperation(Enum.ModelBlendOperation.None);
	end
	if chest.kind ~= kind then
		chest.actor:SetModelByPath(kind.path);
		chest.actor:SetScale(kind.scale);
	end
	chest.kind = kind;
	chest.state = "closed";
	-- It lands a little way from the corpse, toward the hero, and faces the hero.
	local hx, hy = BW.hero:GetPosition();
	local angle = math.atan2(hy - y, hx - x);
	chest.x = x + TUNING.CHEST_AWAY * math.cos(angle);
	chest.y = y + TUNING.CHEST_AWAY * math.sin(angle);
	chest.z = FloorAt(chest.x, chest.y, z);
	chest.actor:SetPosition(chest.x, chest.y, chest.z);
	chest.actor:SetYaw(angle);
	chest.actor:SetAnimation(ANIM_CLOSED);
	chest.actor:SetGroundReticle(TUNING.CHEST_REACH);
	chest.actor:Show();
	tinsert(chests, chest);
end

local function ReleaseChest(i)
	local chest = chests[i];
	chest.actor:ClearGroundReticle();
	chest.actor:Hide();
	tinsert(chestPool, chest);
	table.remove(chests, i);
end

local function UpdateChests(dt, hx, hy)
	local reach2 = TUNING.CHEST_REACH * TUNING.CHEST_REACH;
	for i = table.getn(chests), 1, -1 do
		local chest = chests[i];
		if chest.state == "closed" then
			if Dist2(chest.x, chest.y, hx, hy) <= reach2 then
				chest.state = "opening";
				chest.left = chest.kind.open / chest.kind.speed;
				chest.actor:ClearGroundReticle();
				chest.actor:SetAnimation(ANIM_OPEN, nil, chest.kind.speed);
				chest.actor:PlaySound(CHEST_SOUND);
			end
		else
			chest.left = chest.left - dt;
			if chest.left <= 0 then
				if chest.state == "opening" then
					chest.state = "open";
					chest.left = TUNING.CHEST_LINGER;
					chest.actor:SetAnimation(ANIM_OPENED);
					BW.run.chestTitle = chest.kind.title;
					BW.run.chestPicks = BW.run.chestPicks + chest.kind.picks;
					ShowCards();
				elseif chest.state == "open" then
					chest.state = "fading";
					chest.actor:FadeOut();
				elseif not chest.actor:IsShown() then
					ReleaseChest(i);
				end
			end
		end
	end
end

local function ClearChests()
	for i = table.getn(chests), 1, -1 do
		ReleaseChest(i);
	end
end

-- For the files that load after this one.
BW.chests = chests
BW.UpdateChests = UpdateChests
BW.ClearChests = ClearChests
