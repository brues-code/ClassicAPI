-- Broodwatch tuning and data: the game's constants, spells, passives and items.

local _, BW = ...

------------------------------------------------------------------------------
-- Tuning

local LAIR_MAP = 249
local CENTER = { 0.7, -211.97, -86.08 } -- Onyxia's spawn point, the room's floor
local ROOM_RADIUS = 22                  -- yards around CENTER where the brood spawns
local SPAWN_MIN_DISTANCE = 13           -- yards from the hero
local FAR_CLIP = 500

local CAMERA_YAW = 3.09
local CAMERA_PITCH = 1.0
local CAMERA_ZOOM = 50 -- each run starts zoomed out; the wheel zooms in

local RUN_SECONDS = 600
local ELITE_EVERY = 90
local MAX_ALIVE = 200
local RETARGET_SECONDS = 0.3 -- each chaser gets a new MoveTo this often
local SURROUND_RADIUS = 1.0  -- chasers aim at a ring around the hero, not its centre
local CAST_SPACING = 0.3     -- seconds between two of the hero's casts
local DEATH_SECONDS = 4.5    -- the death animation (1.5 s), then the corpse lies; then it fades out
local ANIM_STAND = 0
local ANIM_DEATH = 1

local HERO_HP = 100
local MAGNET = 5             -- gems this close (yards) fly to you
local GEM_SPEED = 20         -- yards a second, once pulled
local GEM_TAKE = 0.8         -- a pulled gem this close is taken
local GEM_PATH = "World\\Generic\\ActiveDoodads\\Rocks\\GemRock01.m2"
local MAX_GEMS = 150

-- The brood. `from` is the second of the run when the kind starts to spawn.
local KINDS = {
	{ key = "black", display = 497, hp = 20, dps = 5, xp = 1, reach = 1.4, speed = 3.0, from = 0 },
	{ key = "red", display = 956, hp = 30, dps = 6, xp = 2, reach = 1.4, speed = 4.4, from = 120 },
	{ key = "green", display = 694, hp = 70, dps = 9, xp = 3, reach = 1.4, speed = 2.4, from = 240 },
}
-- `speed` is the run speed in yards a second (the hero runs at 7): black
-- whelps plod, red ones dart, green ones are slow and tough, and the elite
-- is faster than any whelp but still slower than you.
local ELITE = { key = "elite", display = 2534, hp = 220, dps = 9, xp = 20, reach = 2.2, speed = 4.8, gemScale = 0.6 }

-- Spells, by level 1 to 5. A missile spell hits `count` enemies in `range`;
-- a nova spell hits every enemy within `radius` of the hero.
local SPELLS = {
	frostbolt = {
		name = "Frostbolt", spell = 116, blurb = "Bolts the nearest enemy.", color = { 0.55, 0.8, 1 }, kind = "missile", pick = "nearest", range = 24,
		damage = { 14, 18, 18, 24, 26 }, count = { 1, 1, 2, 2, 3 }, cooldown = { 1.2, 1.05, 1.05, 0.9, 0.8 },
	},
	shadowbolt = {
		name = "Shadow Bolt", spell = 686, blurb = "A heavy bolt at the toughest enemy in range.", color = { 0.8, 0.45, 1 }, kind = "missile", pick = "strongest", range = 26,
		damage = { 45, 60, 75, 75, 100 }, count = { 1, 1, 1, 2, 2 }, cooldown = { 2.6, 2.4, 2.2, 2.2, 2.0 },
	},
	lightning = {
		name = "Lightning Bolt", spell = 403, blurb = "Strikes enemies at random.", color = { 1, 0.95, 0.4 }, kind = "missile", pick = "random", range = 22,
		damage = { 22, 28, 34, 40, 48 }, count = { 1, 2, 2, 3, 4 }, cooldown = { 1.8, 1.7, 1.6, 1.5, 1.3 },
	},
	nova = {
		name = "Arcane Explosion", spell = 1449, blurb = "Bursts arcane power at every enemy around you.", color = { 0.85, 0.55, 1 }, kind = "nova",
		damage = { 10, 14, 18, 22, 28 }, radius = { 6, 6.5, 7, 7.5, 8 }, cooldown = { 5, 4.6, 4.2, 3.8, 3.4 },
	},
	chain = {
		name = "Chain Lightning", spell = 421, blurb = "Leaps from enemy to enemy.",
		color = { 0.6, 0.75, 1 }, kind = "chain", pick = "nearest", range = 22, hop = 8,
		damage = { 20, 24, 30, 36, 44 }, jumps = { 3, 4, 4, 5, 6 }, cooldown = { 2.4, 2.2, 2.0, 1.8, 1.6 },
	},
	corruption = {
		name = "Corruption", spell = 172, blurb = "Rots enemies every second, and spreads when they die.",
		color = { 0.55, 0.85, 0.3 }, kind = "dot", pick = "nearest", range = 22, hop = 6,
		damage = { 6, 8, 10, 12, 15 }, count = { 2, 3, 3, 4, 5 }, spread = { 1, 1, 2, 2, 3 },
		duration = { 6, 6, 7, 7, 8 }, cooldown = { 3, 3, 2.8, 2.6, 2.4 },
	},
	holynova = {
		name = "Holy Nova", spell = 15237, blurb = "Burns enemies around you, and heals you for each one.",
		color = { 1, 0.95, 0.6 }, kind = "nova",
		damage = { 12, 16, 20, 26, 32 }, radius = { 6, 6.5, 7, 7.5, 8 }, heal = { 1, 1, 1.5, 1.5, 2 },
		cooldown = { 4, 3.7, 3.4, 3.1, 2.8 },
	},
	-- A fire totem model (display 4589, FireElementalTotem.mdx) that pulses
	-- as the real Magma Totem does (its pulse spell 8187, whose visual puts an
	-- Immolate impact on each enemy it burns).
	totem = {
		name = "Magma Totem", spell = 8190, pulse = 8187, display = 4589,
		blurb = "Plants a totem that burns every enemy near it every 2 seconds.",
		color = { 1, 0.5, 0.15 }, kind = "totem", pulseEvery = 2,
		damage = { 14, 18, 23, 28, 35 }, radius = { 6, 6.5, 7, 7.5, 8 }, totems = { 1, 1, 2, 2, 3 },
		duration = { 12, 12, 14, 14, 16 }, cooldown = { 14, 13, 12, 11, 10 },
	},
	-- Ground areas: the spell's own area model (SetModelBySpellArea) stands
	-- where it lands and burns every enemy inside it each second. An area
	-- with `slow` also slows them to that share of their speed.
	blizzard = {
		name = "Blizzard", spell = 10, blurb = "Ice storms down on the thickest pack and chills it.",
		color = { 0.55, 0.8, 1 }, kind = "area", place = "pack", range = 22,
		damage = { 8, 10, 13, 16, 20 }, radius = { 5, 5.5, 6, 6.5, 7 }, slow = { 0.7, 0.65, 0.6, 0.55, 0.5 },
		duration = { 4, 4, 5, 5, 6 },
		cooldown = { 6, 5.5, 5, 4.5, 4 },
	},
	rainoffire = {
		name = "Rain of Fire", spell = 5740, blurb = "Fire rains on the thickest pack.",
		color = { 1, 0.45, 0.15 }, kind = "area", place = "pack", range = 22,
		damage = { 14, 18, 23, 28, 35 }, radius = { 4, 4.4, 4.8, 5.2, 6 }, duration = { 3, 3, 3, 4, 4 },
		cooldown = { 7, 6.5, 6, 5.5, 5 },
	},
	consecration = {
		name = "Consecration", spell = 26573, blurb = "Hallows the ground at your feet.",
		color = { 1, 0.85, 0.4 }, kind = "area", place = "self",
		damage = { 6, 8, 10, 13, 16 }, radius = { 5, 5.5, 6, 6.5, 7 }, duration = { 6, 6, 7, 7, 8 },
		cooldown = { 7, 6.5, 6, 5.5, 5 },
	},
	thunder = {
		name = "Thunder Clap", spell = 6343, blurb = "Shakes the ground close around you.", color = { 1, 0.6, 0.2 }, kind = "nova",
		damage = { 16, 20, 26, 32, 40 }, radius = { 4, 4.4, 4.8, 5.2, 6 }, cooldown = { 3, 2.8, 2.6, 2.4, 2.2 },
	},
}
local SPELL_ORDER = { "frostbolt", "shadowbolt", "lightning", "chain", "corruption", "nova", "holynova", "thunder", "totem", "blizzard", "rainoffire", "consecration" }
local MAX_SPELLS = 6
local EARLY_SPELLS = 3 -- until you have this many spells, each level offers a new one

-- `icon` is the spell whose icon the passive shows; `total` is what all its
-- ranks give together.
local PASSIVES = {
	might = { name = "Might", icon = 19740, text = "+15% damage", total = "+%d%% damage", per = 15 },
	haste = { name = "Haste", icon = 12043, text = "-8% spell cooldowns", total = "-%d%% cooldowns", per = 8 },
	reach = { name = "Reach", icon = 2481, text = "+40% gem pickup radius", total = "+%d%% pickup radius", per = 40 },
	vigor = { name = "Vigor", icon = 1243, text = "+25 maximum health, and heal 25", total = "+%d maximum health", per = 25 },
	renewal = { name = "Renewal", icon = 139, text = "+1.2 health per second", total = "%.1f health per second", per = 1.2 },
	sprint = { name = "Sprint", icon = 2983, text = "+8% run speed", total = "+%d%% run speed", per = 8 },
	devotion = { name = "Devotion Aura", icon = 465, text = "-5% damage taken", total = "-%d%% damage taken", per = 5 },
	amplify = { name = "Amplify Magic", icon = 1008, text = "+10% spell radius", total = "+%d%% spell radius", per = 10 },
	multishot = {
		name = "Multi-Shot", icon = 2643, text = "+1 target for bolts and Corruption", total = "+%d targets", per = 1,
		max = 3,
	},
	wisdom = { name = "Blessing of Wisdom", icon = 19742, text = "+15% experience", total = "+%d%% experience", per = 15 },
	soulstone = {
		name = "Soulstone", icon = 20707, text = "Once per run, rise again with half your health",
		total = "%d revive", per = 1, max = 1,
	},
	vampiric = {
		name = "Vampiric Embrace", icon = 15286,
		text = "Heals you for 0.2% of the damage you deal, at most 2 health a second",
		total = "up to %d health a second", per = 2,
	},
}
local HEAL_ICON = 5185 -- Healing Touch
local UNKNOWN_ICON = "Interface\\Icons\\INV_Misc_QuestionMark"
local PASSIVE_ORDER = {
	"might", "haste", "reach", "vigor", "renewal", "sprint", "vampiric", "devotion", "amplify", "multishot",
	"wisdom", "soulstone",
}
local VAMPIRIC_SHARE = 0.002     -- of the damage dealt, per rank
local HOLY_NOVA_HEAL_TARGETS = 8 -- Holy Nova heals for at most this many enemies
local SOULSTONE_SPELL = 20707 -- Soulstone Resurrection, the revive's visual
local SOULSTONE_BLAST = 10   -- yards: rising again throws back the enemies this close
local MAX_LEVEL = 5

local ROMAN = { "I", "II", "III", "IV", "V" }

-- The bars ease to a new value in the engine (StatusBar interpolation); a
-- reset (a new run, a level's wrap) shows at once.
local EASE = Enum.StatusBarInterpolation.ExponentialEaseOut
local AT_ONCE = Enum.StatusBarInterpolation.Immediate

-- The items' tuning.
local TUNING = {
	SWIFTNESS_FACTOR = 1.5,
	SWIFTNESS_SECONDS = 10,
	ITEM_CHANCE = 1 / 150,     -- a whelp's chance to drop an item
	ITEM_REACH = 1.5,          -- yards: walking this close uses a drop
	ITEM_SECONDS = 30,         -- a drop lies this long
	ITEM_ICON = 28,            -- pixels
	ITEM_COLOR = { 1, 0.82, 0 },
	POTION_HEAL = 0.3,         -- of the maximum health
	GRENADE_RADIUS = 5,
	GRENADE_DAMAGE = 45,       -- grows with the run as the brood's health does
	GRENADE_STUN = 2,          -- seconds
	SAPPER_RADIUS = 10,
	SAPPER_SELF = 0.15,        -- of the maximum health; it never kills you
	SAPPER_ELITE = 0.25,       -- of an elite's maximum health
	CHICKEN_DISPLAY = 6909,    -- the robot chicken (ChickenSkinRobot)
	CHICKEN_SECONDS = 20,
	CHICKEN_SPEED = 9,
	CHICKEN_DAMAGE = 12,       -- a peck, grows with the run
	CHICKEN_PECK = 0.8,        -- seconds between pecks
	CHICKEN_REACH = 1.8,
	CHICKEN_LEASH = 20,        -- yards from the hero: it hunts no farther
	CHICKEN_HUNT_MIN = 6,      -- yards from the hero: it prefers prey past this
	CHICKEN_TAUNT = 3,         -- seconds a peck holds the whelps around it
	CHICKEN_TAUNT_RADIUS = 3,  -- yards around the chicken that a peck taunts
	CHICKEN_ROAM = 6,          -- yards around the hero that it roams with no prey
	-- Knockbacks (actor:KnockBack): yards a second out and up.
	GRENADE_KNOCK = { 10, 7 },
	SOULSTONE_KNOCK = { 16, 10 },
	ELITE_KNOCK_SHARE = 0.4,   -- an elite takes this share of a knockback
}
local ITEMS = {
	{ key = "potion", name = "Healing Potion", spell = 17534, icon = "Interface\\Icons\\INV_Potion_51", weight = 30 },
	{ key = "grenade", name = "Thorium Grenade", spell = 19769, weight = 25 },
	{ key = "swift", name = "Swiftness Potion", spell = 2379, weight = 15 },
	{ key = "hearth", name = "Hearthstone", spell = 8690, weight = 10 },
	{ key = "sapper", name = "Goblin Sapper Charge", spell = 13241, weight = 10 },
	{ key = "chicken", name = "Battle Chicken", spell = 23133, weight = 10 },
}

-- For the files that load after this one.
BW.LAIR_MAP = LAIR_MAP
BW.CENTER = CENTER
BW.ROOM_RADIUS = ROOM_RADIUS
BW.SPAWN_MIN_DISTANCE = SPAWN_MIN_DISTANCE
BW.FAR_CLIP = FAR_CLIP
BW.CAMERA_YAW = CAMERA_YAW
BW.CAMERA_PITCH = CAMERA_PITCH
BW.CAMERA_ZOOM = CAMERA_ZOOM
BW.RUN_SECONDS = RUN_SECONDS
BW.ELITE_EVERY = ELITE_EVERY
BW.MAX_ALIVE = MAX_ALIVE
BW.RETARGET_SECONDS = RETARGET_SECONDS
BW.SURROUND_RADIUS = SURROUND_RADIUS
BW.CAST_SPACING = CAST_SPACING
BW.DEATH_SECONDS = DEATH_SECONDS
BW.ANIM_STAND = ANIM_STAND
BW.ANIM_DEATH = ANIM_DEATH
BW.HERO_HP = HERO_HP
BW.MAGNET = MAGNET
BW.GEM_SPEED = GEM_SPEED
BW.GEM_TAKE = GEM_TAKE
BW.GEM_PATH = GEM_PATH
BW.MAX_GEMS = MAX_GEMS
BW.KINDS = KINDS
BW.ELITE = ELITE
BW.SPELLS = SPELLS
BW.SPELL_ORDER = SPELL_ORDER
BW.MAX_SPELLS = MAX_SPELLS
BW.EARLY_SPELLS = EARLY_SPELLS
BW.PASSIVES = PASSIVES
BW.HEAL_ICON = HEAL_ICON
BW.UNKNOWN_ICON = UNKNOWN_ICON
BW.PASSIVE_ORDER = PASSIVE_ORDER
BW.VAMPIRIC_SHARE = VAMPIRIC_SHARE
BW.HOLY_NOVA_HEAL_TARGETS = HOLY_NOVA_HEAL_TARGETS
BW.SOULSTONE_SPELL = SOULSTONE_SPELL
BW.SOULSTONE_BLAST = SOULSTONE_BLAST
BW.MAX_LEVEL = MAX_LEVEL
BW.ROMAN = ROMAN
BW.EASE = EASE
BW.AT_ONCE = AT_ONCE
BW.TUNING = TUNING
BW.ITEMS = ITEMS
