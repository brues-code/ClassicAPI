-- ClassicAPI Replay: the units' actors (body, mount, held items,
-- animation), the game objects and the spells' ground areas.

local _, ns = ...;

local ANIM_STAND, ANIM_WALK, ANIM_RUN, ANIM_MOUNT = 0, 4, 5, 91
-- In the air (Offsets.h ANIM_JUMP, ANIM_FALL): from a jump, or off a ledge.
local ANIM_JUMP, ANIM_FALL = 38, 40;
-- Combat (AnimationData.dbc): dying, then lying dead.
local ANIM_DEATH, ANIM_DEAD = 1, 6;
-- An attacker's stance between swings, as the client picks it
-- (GetReadyAnimation 0x005FCDC0): by the main hand's weapon subclass (item
-- class 2) 26 Ready1H, 27 Ready2H, 28 Ready2HL, else 25 ReadyUnarmed.
local ITEM_CLASS_WEAPON = 2;
local ANIM_READY_UNARMED = 25;
local READY_BY_SUBCLASS = { [0] = 26, 27, nil, nil, 26, 27, 28, 26, 27, nil, 28, 26, 27, 26, 26, 26, nil, 28 };
-- The animation a stand state shows, as the client's follow camera reads it
-- (Offsets.h ANIM_*): sit, sleep, the three chairs, dead, kneel.
local STAND_ANIMATION = { [1] = 97, [3] = 100, [4] = 102, [5] = 103, [6] = 104, [7] = 6, [8] = 115 };
-- A creature's held slots: main hand, off hand, ranged.
local HELD_SLOTS = { 16, 17, 18 };
-- Below this a moving unit walks (yards a second; walk speed is 2.5, run 7).
local RUN_SPEED = 4;
-- Faster than this a unit is moving, slower it stands.
local MOVING_SPEED = 0.1;
local objectTime;      -- the replay time the objects are at, nil before the first

------------------------------------------------------------------------------
-- Units

local function ReleaseUnits()
	ns.combatTime = nil;
	ns.objects, objectTime, ns.areas = {}, nil, {};
	for i = 1, #ns.units do
		ns.HideBar(ns.units[i]);
		ns.HideCastBar(ns.units[i]);
		ns.HideRaidIcon(ns.units[i]);
	end
	ns.scene:ReleaseAllActors();
	ns.units = {};
	ns.unitOfActor = {};
	ns.focus = nil;
	ns.unitFrame:Hide();
end
ns.ReleaseUnits = ReleaseUnits;

-- A pooled actor keeps what ApplyFromModelSceneActorInfo set at its last
-- use: scene 4's "unwrapped" actor centres its model on its position
-- (useCenterForOriginZ, which sinks a unit by half its height) and carries a
-- display info's scale, which the mixin applies again whenever a model
-- loads. A new one stands on its position at its own size.
local function NewActor()
	local actor = ns.scene:AcquireActor();
	actor:SetUseCenterForOrigin(false, false, false);
	actor:SetPitch(0);
	actor:SetRoll(0);
	actor:SetAlpha(1);
	actor:StopAnimationKit();
	actor:SetSpellVisualKit(nil);
	actor:SetNormalizedScaleAggressiveness(0);
	actor:SetRequestedScale(1);
	actor:SetCreatureID(0);
	actor:SetAttacking(false);
	actor:SetMoving(false);
	return actor;
end

local function Acquire(u)
	local actor = NewActor();
	ns.unitOfActor[actor] = u;
	return actor;
end

local function HideUnit(u)
	ns.HideBar(u);
	ns.HideCastBar(u);
	ns.HideRaidIcon(u);
	if u.actor and u.actor:IsShown() then
		u.actor:Hide();
	end
	if u.mount and u.mount:IsShown() then
		u.mount:Hide();
	end
end
ns.HideUnit = HideUnit;

-- Seats the unit on a mount of `mountDisplay`, or takes it off with 0. The
-- actor that moves is the mount while it rides.
local function SetMount(u, mountDisplay)
	if mountDisplay == 0 then
		if u.seated then
			u.mount:DetachFromMount(u.actor);
			u.seated = false;
			u.animation = nil;
		end
		if u.mount and u.mount:IsShown() then
			u.mount:Hide();
		end
		u.mountDisplay = 0;
		return u.actor;
	end
	if not u.mount then
		u.mount = Acquire(u);
	end
	if u.mountDisplay ~= mountDisplay then
		if u.seated then
			u.mount:DetachFromMount(u.actor);
		end
		u.mount:SetModelByCreatureDisplayID(mountDisplay);
		u.mountDisplay = mountDisplay;
		u.seated = false;
		u.animation = nil;
	end
	if not u.mount:IsShown() then
		u.mount:Show();
	end
	-- A seated rider's place and facing are relative to its seat, so it sits
	-- at the seat itself, not at the map place it last walked to. A model
	-- that has not loaded yet seats nothing; try again next frame.
	if not u.seated then
		u.actor:SetPosition(0, 0, 0);
		u.actor:SetYaw(0);
		u.seated = u.mount:AttachToMount(u.actor, ANIM_MOUNT);
	end
	return u.mount;
end

-- The player's appearance at `time`: the last that began by then, else its
-- first (PLAYER_INFO comes with the UNIT_INFO that shows the unit).
local function AppearanceAt(u, time)
	local list = u.appearances;
	if not list or not list[1] then
		return nil;
	end
	local found = list[1];
	for i = 2, #list do
		if list[i].time > time then
			break;
		end
		found = list[i];
	end
	return found;
end
ns.AppearanceAt = AppearanceAt;

-- Shows the player's own body in `appearance`, wearing its items.
local function Dress(actor, appearance)
	actor:SetModelByCharacterAppearance(appearance);
	for slot, info in ipairs(appearance.items) do
		if info.appearanceID > 0 then
			actor:SetItemTransmogInfo(info, slot);
		end
	end
end

-- A creature holds what its unit held (a player holds its items): set again
-- with each new model, and when what it holds changes.
local function Hold(u, time, held1, held2, held3)
	if u.held1 == held1 and u.held2 == held2 and u.held3 == held3 then
		return;
	end
	u.held1, u.held2, u.held3 = held1, held2, held3;
	for _, slot in ipairs(HELD_SLOTS) do
		u.actor:SetHeldItem(slot, C_CombatLogReplay.GetUnitHeldItem(u.index, time, slot));
	end
end

local function ShowUnit(u, time, x, y, z, facing, speed, falling, jumping, display, nativeDisplay,
		mountDisplay, scale, standState, sheathState, isDead, held1, held2, held3)
	local actor = u.actor;
	if not actor then
		actor = Acquire(u);
		u.actor = actor;
		u.named = nil;
		-- The creature's own data keeps its flinches and parries off.
		actor:SetCreatureID(u.creatureID or 0);
		for _, aura in ipairs(C_CombatLogReplay.GetUnitAuras(u.index, time)) do
			actor:AddAuraVisual(aura.spellID);
		end
	end
	if not actor:IsShown() then
		actor:Show();
	end
	-- A dead unit shows no name.
	local named = not isDead;
	if u.named ~= named then
		if named then
			local color = u.nameColor;
			actor:SetNameText(u.name, color[1], color[2], color[3]);
		else
			actor:ClearNameText();
		end
		u.named = named;
	end
	-- A player shown in its own display is its own body; in any other (a
	-- form, a polymorph) it is that creature, as the client shows it.
	local appearance = display == nativeDisplay and AppearanceAt(u, time);
	local model = appearance or display;
	if u.model ~= model then
		if appearance then
			Dress(actor, appearance);
		else
			actor:SetModelByCreatureDisplayID(display);
		end
		u.model = model;
		u.animation = nil;
		u.seated = false;
		u.held1, u.held2, u.held3, u.sheathState = nil, nil, nil, nil;
	end
	if not appearance then
		Hold(u, time, held1, held2, held3);
	end
	-- Weapons in hand or sheathed as the unit held them; 2 is the ranged
	-- weapon drawn.
	if u.sheathState ~= sheathState then
		actor:SetSheatheState(sheathState);
		u.sheathState = sheathState;
	end
	-- Through the mixin, which sets it again once the model has loaded.
	if u.scale ~= scale then
		actor:SetRequestedScale(scale);
		u.scale = scale;
	end
	-- What the actor's swings and reactions read: whether it moves, and
	-- whether it swings at a target.
	u.moving = speed > MOVING_SPEED or falling;
	actor:SetMoving(u.moving);
	local _, _, _, _, _, attackTarget = C_CombatLogReplay.GetUnitState(u.index, time);
	actor:SetAttacking(attackTarget ~= nil);
	local body = SetMount(u, mountDisplay);
	body:SetPosition(x, y, z);
	body:SetYaw(facing);
	local animation;
	if isDead then
		animation = u.dyingUntil and GetTime() < u.dyingUntil and ANIM_DEATH or ANIM_DEAD;
	elseif falling then
		animation = jumping and ANIM_JUMP or ANIM_FALL;
	elseif speed > MOVING_SPEED then
		animation = speed >= RUN_SPEED and ANIM_RUN or ANIM_WALK;
	elseif attackTarget and mountDisplay == 0 then
		-- The pick's attacking leg (0x005FD360): an unmounted attacker
		-- stands ready. Its swings play over this.
		local itemClass, subclass = C_CombatLogReplay.GetUnitWeapons(u.index, time);
		animation = itemClass == ITEM_CLASS_WEAPON and READY_BY_SUBCLASS[subclass] or ANIM_READY_UNARMED;
	else
		animation = STAND_ANIMATION[standState] or ANIM_STAND;
	end
	if u.animation ~= animation then
		body:SetAnimation(animation);
		u.animation = animation;
	end
end
ns.ShowUnit = ShowUnit;

------------------------------------------------------------------------------
-- Game objects and ground areas

-- Puts every game object where it was at `time`, in its state. A state that
-- changes as the replay plays plays its open or close animation first.
local function ApplyObjects(time)
	local played = objectTime and time >= objectTime and time - objectTime <= ns.MAX_PLAYED_STEP;
	objectTime = time;
	local GetGameObjectInfo = C_CombatLogReplay.GetGameObjectInfo;
	for i = 1, #ns.objects do
		local o = ns.objects[i];
		local x, y, z, yaw, pitch, roll, scale, display, _, state = GetGameObjectInfo(i, time);
		if x and o.display ~= display then
			o.actor = o.actor or NewActor();
			o.hasModel = o.actor:SetModelByGameObjectDisplayID(display);
			o.display, o.state, o.x = display, nil, nil;
		end
		if x and o.hasModel then
			local actor = o.actor;
			if not actor:IsShown() then
				actor:Show();
			end
			if o.x ~= x or o.y ~= y or o.z ~= z or o.yaw ~= yaw or o.pitch ~= pitch or o.roll ~= roll
					or o.scale ~= scale then
				actor:SetPosition(x, y, z);
				actor:SetYaw(yaw);
				actor:SetPitch(pitch);
				actor:SetRoll(roll);
				actor:SetRequestedScale(scale);
				o.x, o.y, o.z, o.yaw, o.pitch, o.roll, o.scale = x, y, z, yaw, pitch, roll, scale;
			end
			if o.state ~= state then
				actor:SetGameObjectState(state, played and o.state or nil);
				o.state = state;
			end
		elseif o.actor and o.actor:IsShown() then
			o.actor:Hide();
			o.state = nil;
		end
	end
end
ns.ApplyObjects = ApplyObjects;

-- Shows each spell's ground area while it was there.
local function ApplyAreas(time)
	for _, area in ipairs(ns.areas) do
		if time >= area.startTime and (not area.endTime or time < area.endTime) then
			if not area.actor then
				area.actor = NewActor();
				area.hasModel = area.actor:SetModelBySpellArea(area.spellID, area.radius);
				area.actor:SetPosition(area.x, area.y, area.z);
				area.actor:SetYaw(area.facing);
			end
			if area.hasModel and not area.actor:IsShown() then
				area.actor:Show();
			end
		elseif area.actor and area.actor:IsShown() then
			area.actor:Hide();
		end
	end
end
ns.ApplyAreas = ApplyAreas;
