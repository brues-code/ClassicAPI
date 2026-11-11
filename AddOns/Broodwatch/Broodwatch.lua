-- Broodwatch: hold Onyxia's Lair against an endless brood.
--
-- You run with W A S D (in screen directions). Your spells cast themselves.
-- Whelps pour into the lair and chase you; each kill drops a gem, and gems
-- level you up. At each level you pick one of three upgrades (click, or
-- press 1 2 3). An elite dragonspawn joins every 90 seconds. Survive ten
-- minutes to win.
--
-- /broodwatch starts a run. P pauses. Escape leaves.
--
-- Prototype: one ClassicAPI ModelScene, the lair as its WMO, every unit an
-- actor. The frame time and the Lua time per frame show in the corner.

local _, BW = ...
local Blast = BW.Blast
local TUNING = BW.TUNING
local LAIR_MAP = BW.LAIR_MAP
local CENTER = BW.CENTER
local FAR_CLIP = BW.FAR_CLIP
local CAMERA_YAW = BW.CAMERA_YAW
local CAMERA_PITCH = BW.CAMERA_PITCH
local CAMERA_ZOOM = BW.CAMERA_ZOOM
local RUN_SECONDS = BW.RUN_SECONDS
local ELITE_EVERY = BW.ELITE_EVERY
local RETARGET_SECONDS = BW.RETARGET_SECONDS
local SURROUND_RADIUS = BW.SURROUND_RADIUS
local HERO_HP = BW.HERO_HP
local MAGNET = BW.MAGNET
local GEM_SPEED = BW.GEM_SPEED
local GEM_TAKE = BW.GEM_TAKE
local ELITE = BW.ELITE
local SPELLS = BW.SPELLS
local PASSIVES = BW.PASSIVES
local SOULSTONE_SPELL = BW.SOULSTONE_SPELL
local SOULSTONE_BLAST = BW.SOULSTONE_BLAST
local AT_ONCE = BW.AT_ONCE
local enemies = BW.enemies
local gemPool = BW.gemPool
local gems = BW.gems
local Dist2 = BW.Dist2
local Clock = BW.Clock
local ClearGems = BW.ClearGems
local Spawn = BW.Spawn
local Kill = BW.Kill
local ShowNumber = BW.ShowNumber
local UpdateNumbers = BW.UpdateNumbers
local ClearNumbers = BW.ClearNumbers
local Unflash = BW.Unflash
local Damage = BW.Damage
local Release = BW.Release
local ClearEnemies = BW.ClearEnemies
local TargetAlive = BW.TargetAlive
local PickKind = BW.PickKind
local Stat = BW.Stat
local Nearest = BW.Nearest
local UpdateTotems = BW.UpdateTotems
local ClearTotems = BW.ClearTotems
local Unslow = BW.Unslow
local UpdateAreas = BW.UpdateAreas
local UpdateLandings = BW.UpdateLandings
local ClearLandings = BW.ClearLandings
local ClearAreas = BW.ClearAreas
local Fire = BW.Fire
local CastNext = BW.CastNext
local Corrupt = BW.Corrupt
local UpdateBuild = BW.UpdateBuild
local SetPaused = BW.SetPaused
local MaxHp = BW.MaxHp
local UpdateHealth = BW.UpdateHealth
local XpNeeded = BW.XpNeeded
local GainXp = BW.GainXp
local Taunted = BW.Taunted
local UpdateItems = BW.UpdateItems
local ClearItems = BW.ClearItems
local UpdateStates = BW.UpdateStates
local BiteShare = BW.BiteShare
local UpdateWards = BW.UpdateWards
local ClearWards = BW.ClearWards
local UpdatePet = BW.UpdatePet
local ClearPet = BW.ClearPet
local UpdateChannels = BW.UpdateChannels
local ClearChannels = BW.ClearChannels
local Zap = BW.Zap
local UpdateInfernals = BW.UpdateInfernals
local ClearInfernals = BW.ClearInfernals
local EndShield = BW.EndShield
local Mastered = BW.Mastered

------------------------------------------------------------------------------
-- Movement: W A S D in screen directions. The hero faces the way it runs.

local function Held(key)
	return BW.held[key] and 1 or 0;
end

local function Steer()
	if not BW.run or BW.paused or BW.run.over then
		return;
	end
	local forward = Held("W") - Held("S");
	local left = Held("A") - Held("D");
	if forward == 0 and left == 0 then
		if BW.moving then
			BW.hero:StopMoving();
			BW.moving = false;
		end
		return;
	end
	local yaw = BW.camera:GetYaw() + math.atan2(left, forward);
	if not BW.moving or math.abs(yaw - BW.run.heroYaw) > 0.001 then
		BW.run.heroYaw = yaw;
		BW.hero:SetYaw(yaw);
	end
	if not BW.moving then
		BW.hero:StartMovingForward();
		BW.moving = true;
	end
end

------------------------------------------------------------------------------
-- The run

local function EndRun(won)
	BW.run.over = true;
	SetPaused(true);
	BW.cards:Hide();
	ClearNumbers();
	BW.run.hurtGlow = 0;
	BW.hurtTexture:SetAlpha(0);
	if won then
		BW.bannerText:SetText("The lair holds!");
	else
		BW.bannerText:SetText("You fell at " .. Clock(BW.run.time));
	end
	BW.hintText:SetText(string.format("Level %d, %d kills.  R: try again.  Escape: leave.", BW.run.level, BW.run.kills));
end

BW.heroNumber = {}             -- ShowNumber's stand-in record for the hero
local HEAL_COLOR = { 0.3, 1, 0.3 }

local function StartRun()
	if BW.run then
		ClearEnemies();
		ClearGems();
		ClearNumbers();
		ClearTotems();
		ClearAreas();
		ClearLandings();
		ClearItems();
		ClearWards();
		ClearPet();
		ClearChannels();
		ClearInfernals();
	end
	BW.hero:SetSpellVisualKit();
	BW.hero:RemoveAuraVisual();
	BW.hurtTexture:SetAlpha(0);
	BW.run = {
		time = 0, kills = 0, alive = 0, level = 1, xp = 0, pendingLevels = 0,
		hp = HERO_HP, damageScale = 1, heroYaw = 0,
		spells = { frostbolt = { level = 1, cooldown = 0, pending = 0 } },
		passives = {},
		castQueue = {}, castGap = 0, inFlight = {},
		spawnGap = 0, nextElite = ELITE_EVERY, retargetCarry = 0, retargetCursor = 1,
		shownSecond = -1, perfFrames = 0, perfLua = 0, perfClock = 0, hurtGlow = 0,
		spreads = {}, pendingHeal = 0, leech = 0, landings = {},
	};
	SetPaused(false);
	BW.held = {};
	BW.moving = false;
	BW.hero:SetMovementEnabled(false);
	BW.hero:SetPosition(CENTER[1], CENTER[2], CENTER[3] + 0.5);
	BW.hero:SetYaw(CAMERA_YAW);
	BW.hero:Show();
	BW.hero:SetMovementEnabled(true);
	BW.run.heroRun = BW.hero:GetMovementSpeed("run");
	BW.cards:Hide();
	BW.bannerText:SetText("");
	BW.hintText:SetText("");
	BW.xpBar:SetMinMaxValues(0, XpNeeded(1), AT_ONCE);
	BW.xpBar:SetValue(0, AT_ONCE);
	UpdateHealth(AT_ONCE);
	UpdateBuild();
	BW.camera:SetZoomDistance(CAMERA_ZOOM);
	BW.camera:SnapAllInterpolatedValues();
end

local function IsCorrupted(e)
	return e.dotLeft ~= nil;
end

-- Sends a slice of the brood after the hero, so each one gets a new MoveTo
-- every RETARGET_SECONDS, at its own point of a ring around the hero.
local function Chase(dt, hx, hy, hz)
	local n = table.getn(enemies);
	if n == 0 then
		return;
	end
	BW.run.retargetCarry = BW.run.retargetCarry + n * dt / RETARGET_SECONDS;
	while BW.run.retargetCarry >= 1 do
		BW.run.retargetCarry = BW.run.retargetCarry - 1;
		if BW.run.retargetCursor > n then
			BW.run.retargetCursor = 1;
		end
		local e = enemies[BW.run.retargetCursor];
		if e.alive and not (e.stunUntil and BW.run.time < e.stunUntil) and not e.actor:IsFalling() then
			if e.sheepUntil then
				-- A sheep grazes about where it is.
				local x, y = e.actor:GetPosition();
				local a = math.random() * 2 * math.pi;
				e.actor:MoveTo(x + TUNING.SHEEP_WANDER * math.cos(a), y + TUNING.SHEEP_WANDER * math.sin(a), hz);
			elseif e.fearUntil then
				local x, y = e.actor:GetPosition();
				local dx, dy = x - hx, y - hy;
				local d = math.sqrt(dx * dx + dy * dy);
				if d < 0.1 then
					dx, dy, d = math.cos(e.angle), math.sin(e.angle), 1;
				end
				local step = TUNING.FEAR_STEP / d;
				e.actor:MoveTo(x + dx * step, y + dy * step, hz);
			elseif Taunted(e) then
				local cx, cy, cz = e.taunter.actor:GetPosition();
				e.actor:MoveTo(cx + SURROUND_RADIUS * math.cos(e.angle), cy + SURROUND_RADIUS * math.sin(e.angle), cz);
			else
				e.actor:MoveTo(hx + SURROUND_RADIUS * math.cos(e.angle), hy + SURROUND_RADIUS * math.sin(e.angle), hz);
			end
		end
		BW.run.retargetCursor = BW.run.retargetCursor + 1;
	end
end

local function Step(dt)
	BW.run.time = BW.run.time + dt;
	local hx, hy, hz = BW.hero:GetPosition();

	-- Spawns.
	BW.run.spawnGap = BW.run.spawnGap - dt;
	if BW.run.spawnGap <= 0 then
		BW.run.spawnGap = 0.1;
		local want = TargetAlive(BW.run.time) - BW.run.alive;
		for _ = 1, math.min(want, 5) do
			Spawn(PickKind());
		end
	end
	if BW.run.time >= BW.run.nextElite then
		BW.run.nextElite = BW.run.nextElite + ELITE_EVERY;
		for _ = 1, 1 + math.floor(BW.run.time / 180) do
			Spawn(ELITE);
		end
	end

	Chase(dt, hx, hy, hz);

	-- Contact damage, and the corpses. Thorns burns the biters once a second.
	local hurt = 0;
	local thorns = false;
	if BW.run.thornsUntil then
		if BW.run.time >= BW.run.thornsUntil then
			BW.run.thornsUntil = nil;
		else
			BW.run.thornsTick = BW.run.thornsTick - dt;
			if BW.run.thornsTick <= 0 then
				BW.run.thornsTick = BW.run.thornsTick + 1;
				thorns = true;
			end
		end
	end
	for i = table.getn(enemies), 1, -1 do
		local e = enemies[i];
		if e.lit then
			e.flash = e.flash - dt;
			if e.flash <= 0 then
				Unflash(e);
			end
		end
		if e.alive and e.dotLeft then
			e.dotTick = e.dotTick - dt;
			if e.dotTick <= 0 then
				e.dotTick = e.dotTick + 1;
				e.dotLeft = e.dotLeft - 1;
				-- The last tick can kill too, and then it still spreads (Kill
				-- reads dotLeft), so it ends after the damage.
				Damage(e, e.dotDamage, SPELLS.corruption.color);
				if e.dotLeft and e.dotLeft <= 0 then
					e.dotLeft = nil;
				end
			end
		end
		if e.alive and e.slowUntil and BW.run.time >= e.slowUntil then
			Unslow(e);
		end
		if e.alive then
			UpdateStates(e, dt);
		end
		if e.alive then
			local x, y = e.actor:GetPosition();
			local reach = e.kind.reach;
			if not Taunted(e) and not e.fearUntil and not e.sheepUntil and Dist2(x, y, hx, hy) <= reach * reach then
				hurt = hurt + e.dps * BiteShare(e) * (1 - PASSIVES.devotion.per * (BW.run.passives.devotion or 0) / 100);
				Zap(e);
				if thorns then
					Damage(e, BW.run.thornsDamage, SPELLS.thorns.color);
					if Mastered("thorns") then
						BW.run.pendingHeal = BW.run.pendingHeal + SPELLS.thorns.mastery.heal;
					end
				end
			end
		else
			-- The corpse lies, then fades out as the world's do, and goes
			-- once the fade has hidden it.
			e.dying = e.dying - dt;
			if e.dying <= 0 then
				if not e.fading then
					e.fading = true;
					e.actor:FadeOut();
				elseif not e.actor:IsShown() then
					Release(i);
				end
			end
		end
	end
	-- Corruption passes from each enemy that died with it to its neighbours.
	for i = table.getn(BW.run.spreads), 1, -1 do
		local from = BW.run.spreads[i];
		BW.run.spreads[i] = nil;
		local fx, fy = from.actor:GetPosition();
		for _ = 1, from.dotSpread do
			local e = Nearest(fx, fy, SPELLS.corruption.hop, IsCorrupted);
			if not e then
				break;
			end
			Corrupt(e, from.dotDamage, from.dotSeconds, from.dotSpread);
		end
	end
	UpdateTotems(dt);
	UpdateAreas(dt);
	UpdateLandings(dt);
	UpdateItems(dt, hx, hy, hz);
	UpdateWards(dt);
	UpdatePet(dt, hx, hy, hz);
	UpdateChannels(dt);
	UpdateInfernals(dt);
	if BW.run.shoutUntil and BW.run.time >= BW.run.shoutUntil then
		BW.run.shoutUntil = nil;
		BW.ApplyHeroSpeed();
	end

	-- Power Word: Shield soaks the bites first.
	local taken = hurt * dt;
	if BW.run.shield then
		local soak = math.min(BW.run.shield, taken);
		BW.run.shield = BW.run.shield - soak;
		taken = taken - soak;
		if BW.run.shield <= 0 then
			EndShield(true);
		elseif BW.run.time >= BW.run.shieldUntil then
			EndShield();
		elseif soak > 0 then
			UpdateHealth();
		end
	end

	-- The screen's edge glows red while the brood bites, and fades after.
	local glow = BW.run.hurtGlow;
	if taken > 0 then
		glow = math.min(0.8, glow + 4 * dt);
	elseif glow > 0 then
		glow = math.max(0, glow - 1.5 * dt);
	end
	if glow ~= BW.run.hurtGlow then
		BW.run.hurtGlow = glow;
		BW.hurtTexture:SetAlpha(glow);
	end

	if BW.run.leech > 0 then
		-- Vampiric Embrace heals at most its rate; the rest of a burst is
		-- kept for at most a second.
		local rate = PASSIVES.vampiric.per * (BW.run.passives.vampiric or 0);
		BW.run.leech = math.min(BW.run.leech, rate);
		local heal = math.min(BW.run.leech, rate * dt);
		BW.run.leech = BW.run.leech - heal;
		if heal > 0 and BW.run.hp < MaxHp() then
			BW.run.hp = math.min(MaxHp(), BW.run.hp + heal);
			UpdateHealth();
		end
	end
	if BW.run.pendingHeal > 0 then
		local heal = math.floor(BW.run.pendingHeal + 0.5);
		BW.run.pendingHeal = 0;
		if heal > 0 and BW.run.hp < MaxHp() then
			BW.run.hp = math.min(MaxHp(), BW.run.hp + heal);
			UpdateHealth();
			BW.heroNumber.actor = BW.hero;
			ShowNumber(BW.heroNumber, heal, HEAL_COLOR, "+" .. heal);
		end
	end

	local regen = PASSIVES.renewal.per * (BW.run.passives.renewal or 0);
	if taken > 0 or (regen > 0 and BW.run.hp < MaxHp()) then
		BW.run.hp = math.min(MaxHp(), BW.run.hp + regen * dt - taken);
		UpdateHealth();
		if BW.run.hp <= 0 and BW.run.passives.soulstone and not BW.run.soulstoneUsed then
			-- Rise again at half health, and throw back the brood close by.
			BW.run.soulstoneUsed = true;
			BW.run.hp = MaxHp() * 0.5;
			UpdateHealth();
			BW.hero:PlaySpellVisual(SOULSTONE_SPELL, BW.hero, 0);
			local r2 = SOULSTONE_BLAST * SOULSTONE_BLAST;
			for _, e in ipairs(enemies) do
				if e.alive then
					local x, y = e.actor:GetPosition();
					if Dist2(x, y, hx, hy) <= r2 then
						if e.kind ~= ELITE then
							Kill(e);
						end
						Blast(e, hx, hy, TUNING.SOULSTONE_KNOCK);
					end
				end
			end
			UpdateBuild();
		end
		if BW.run.hp <= 0 then
			EndRun(false);
			return;
		end
	end

	-- Gems.
	local magnet = MAGNET * (1 + 0.4 * (BW.run.passives.reach or 0));
	local m2 = magnet * magnet;
	local take2 = GEM_TAKE * GEM_TAKE;
	local gained = 0;
	for i = table.getn(gems), 1, -1 do
		local gem = gems[i];
		local d2 = Dist2(gem.x, gem.y, hx, hy);
		if not gem.pulled and d2 <= m2 then
			gem.pulled = true;
		end
		if gem.pulled then
			if d2 <= take2 then
				gained = gained + gem.xp;
				gem.actor:Hide();
				tinsert(gemPool, gem);
				table.remove(gems, i);
			else
				-- It flies at you, faster than you run, and rises to your chest.
				local d = math.sqrt(d2);
				local step = math.min(1, GEM_SPEED * dt / d);
				gem.x = gem.x + (hx - gem.x) * step;
				gem.y = gem.y + (hy - gem.y) * step;
				gem.z = gem.z + (hz + 1 - gem.z) * step;
				gem.actor:SetPosition(gem.x, gem.y, gem.z);
			end
		end
	end
	if gained > 0 then
		GainXp(gained * (1 + PASSIVES.wisdom.per * (BW.run.passives.wisdom or 0) / 100));
	end

	-- Spells.
	local haste = 1 - 0.08 * (BW.run.passives.haste or 0);
	for key, state in pairs(BW.run.spells) do
		state.cooldown = state.cooldown - dt;
		if state.cooldown <= 0 and state.pending == 0 and Fire(key, state) then
			state.cooldown = Stat(SPELLS[key], "cooldown", state.level) * haste;
			state.cdFull = state.cooldown;
			if state.slot then
				state.slot.cd:SetCooldown(GetTime(), state.cooldown);
			end
		end
	end
	BW.run.castGap = BW.run.castGap - dt;
	if BW.run.castGap <= 0 then
		CastNext();
	end

	if BW.run.time >= RUN_SECONDS then
		EndRun(true);
	end
end

local function UpdateHud(dt, luaMs)
	local second = math.floor(BW.run.time);
	if second ~= BW.run.shownSecond then
		BW.run.shownSecond = second;
		BW.infoText:SetText(string.format("Level %d    %s    Kills %d", BW.run.level, Clock(BW.run.time), BW.run.kills));
	end
	BW.run.perfFrames = BW.run.perfFrames + 1;
	BW.run.perfLua = BW.run.perfLua + luaMs;
	BW.run.perfClock = BW.run.perfClock + dt;
	if BW.run.perfClock >= 1 then
		BW.perfText:SetText(string.format("%.0f fps   Lua %.2f ms/frame   %d alive   %d gems",
			BW.run.perfFrames / BW.run.perfClock, BW.run.perfLua / BW.run.perfFrames, BW.run.alive, table.getn(gems)));
		BW.run.perfFrames, BW.run.perfLua, BW.run.perfClock = 0, 0, 0;
	end
end

local function OnUpdate()
	if not BW.run or BW.paused or BW.run.over then
		return;
	end
	local dt = arg1;
	local t0 = debugprofilestop();
	Steer();
	Step(dt);
	UpdateNumbers(dt);
	local luaMs = debugprofilestop() - t0;
	if not BW.run.over then
		UpdateHud(dt, luaMs);
	end
end

------------------------------------------------------------------------------
-- Keys

local MOVE_KEYS = { W = true, A = true, S = true, D = true }

local function OnKeyDown()
	local key = arg1;
	if key == "ESCAPE" then
		BW.frame:Hide();
		return;
	end
	if not BW.run then
		return;
	end
	if BW.run.over then
		if key == "R" then
			StartRun();
		end
		return;
	end
	if BW.cards:IsShown() then
		if key == "1" or key == "2" or key == "3" then
			Broodwatch_PickCard(tonumber(key));
		end
		return;
	end
	if key == "P" then
		SetPaused(not BW.paused);
		BW.bannerText:SetText(BW.paused and "Paused" or "");
		BW.hintText:SetText(BW.paused and "P: resume." or "");
		return;
	end
	if MOVE_KEYS[key] then
		BW.held[key] = true;
		Steer();
	end
end

local function OnKeyUp()
	BW.held[arg1] = nil;
	Steer();
end

------------------------------------------------------------------------------
-- Setup

local function Build()
	debugprofilestart();
	BW.scene:TransitionToModelSceneID(4, CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD, true);
	BW.scene:SetWorldModelByMapID(LAIR_MAP);
	BW.scene:SetCameraFarClip(FAR_CLIP);

	local _, cameraIDs = C_ModelInfo.GetModelSceneInfoByID(4);
	BW.camera = BW.scene:AddCamera(CameraRegistry:CreateCameraByType("FollowCamera"));
	BW.camera:ApplyFromModelSceneCameraInfo(C_ModelInfo.GetModelSceneCameraInfoByID(cameraIDs[1]),
		CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD);
	BW.camera:SetTargetSpline(nil);
	BW.camera:SetOrientationSpline(nil);
	BW.camera:SetMinZoomDistance(12);
	BW.camera:SetMaxZoomDistance(60);

	BW.hero = BW.scene:AcquireActor();
	BW.hero:SetModelByUnit("player");
	BW.hero:HookScript("OnSpellVisualImpact", BW.OnImpact);
	BW.scene:SetSoundListenerActor(BW.hero);

	BW.camera:SetFollowActor(BW.hero);
	BW.camera:SetYaw(CAMERA_YAW);
	BW.camera:SetPitch(CAMERA_PITCH);
	BW.camera:SetZoomDistance(CAMERA_ZOOM);
	BW.camera:SnapAllInterpolatedValues();
	BW.scene:SetActiveCamera(BW.camera);
end

function Broodwatch_OnLoad(self)
	BW.frame = self;
	BW.scene = getglobal(self:GetName() .. "Scene");
	BW.feedback = getglobal(self:GetName() .. "Feedback");
	BW.hurtTexture = getglobal(self:GetName() .. "FeedbackHurt");
	BW.hurtTexture:SetAlpha(0);
	-- Siblings at one frame level draw in no set order, and the scene's
	-- paint covered the feedback (item icons, numbers). Chained parents
	-- order them: scene, feedback over it, the HUD over both. SetParent
	-- moves a frame's children with it; SetFrameLevel would not.
	local hud = self:GetName() .. "Hud";
	BW.feedback:SetParent(BW.scene);
	getglobal(hud):SetParent(BW.feedback);
	BW.infoText = getglobal(hud .. "Info");
	BW.perfText = getglobal(hud .. "Perf");
	BW.bannerText = getglobal(hud .. "Banner");
	BW.hintText = getglobal(hud .. "Hint");
	BW.xpBar = getglobal(hud .. "XP");
	BW.healthBar = getglobal(hud .. "Health");
	BW.healthText = getglobal(hud .. "HealthText");
	BW.cards = getglobal(hud .. "Cards");
	BW.build = getglobal(hud .. "Build");
	self:SetScript("OnUpdate", OnUpdate);
	self:SetScript("OnKeyDown", OnKeyDown);
	self:SetScript("OnKeyUp", OnKeyUp);
	self:SetScript("OnShow", function()
		BW.frame:EnableKeyboard(true);
	end);
	self:SetScript("OnHide", function()
		BW.frame:EnableKeyboard(false);
		if BW.run and not BW.run.over then
			EndRun(false);
		end
	end);
end

SLASH_BROODWATCH1 = "/broodwatch";
SlashCmdList["BROODWATCH"] = function()
	BW.frame:Show();
	if not BW.hero then
		Build();
	end
	StartRun();
end
