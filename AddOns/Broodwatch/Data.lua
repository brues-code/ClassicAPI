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
local ANIM_ATTACK_UNARMED = 16

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
-- A kind's traits are its fields, read by what they govern:
-- - `holdShare`: the share of a root's, stun's, fear's or polymorph's time
--   it takes (1 if absent); 0 is immune, and Polymorph never picks it.
-- - `knockShare`: the share of a knockback it takes (1 if absent); 0 stands.
-- - `speed` 0: it never moves. `dps` 0: it never bites.
-- - `path`: a model file, in place of the creature `display`.
-- - `crackAnim`, `crackSound`, `crackSeconds`: it ends with this animation
--   and sound, holds the frame at `crackSeconds` (just short of the
--   animation's end, before it loops) and fades, in place of Death and a
--   corpse.
local ELITE = { key = "elite", display = 2534, hp = 220, dps = 9, xp = 20, reach = 2.2, speed = 4.8, gemScale = 0.6,
	holdShare = 0.5, knockShare = 0.4 }
-- A black dragon egg (Blackwing Lair's). Broken or hatched, it cracks with
-- its gameobject's Custom0 (153) and that slot's sound (GameObjectDisplayInfo
-- 477's sound 6, SoundEntries 4785).
local EGG = { key = "egg", path = "World\\Goober\\G_DragonEggBlack.m2", hp = 60, dps = 0, xp = 6, reach = 0, speed = 0,
	gemScale = 0.5, holdShare = 0, knockShare = 0, crackAnim = 153, crackSound = 4785, crackSeconds = 0.95 }

-- Spells, by level 1 to 5. A missile spell hits `count` enemies in `range`;
-- a nova spell hits every enemy within `radius` of the hero. At rank V a
-- spell masters: `mastery` is what it does then, and its numbers.
local SPELLS = {
	frostbolt = {
		spell = 116, blurb = "Bolts the nearest enemy.", color = { 0.55, 0.8, 1 }, kind = "missile", pick = "nearest", range = 24,
		damage = { 14, 18, 18, 24, 26 }, count = { 1, 1, 2, 2, 3 }, cooldown = { 1.2, 1.05, 1.05, 0.9, 0.8 },
		mastery = { text = "Frostbite: each bolt freezes its target for 1 second.", freeze = 1 },
	},
	shadowbolt = {
		spell = 686, blurb = "A heavy bolt at the toughest enemy in range.", color = { 0.8, 0.45, 1 }, kind = "missile", pick = "strongest", range = 26,
		damage = { 45, 60, 75, 75, 100 }, count = { 1, 1, 1, 2, 2 }, cooldown = { 2.6, 2.4, 2.2, 2.2, 2.0 },
		mastery = { text = "Shadowburn: a bolt that kills bursts, hitting enemies within 4 yards for half its damage.", radius = 4, share = 0.5 },
	},
	lightning = {
		spell = 403, blurb = "Strikes enemies at random.", color = { 1, 0.95, 0.4 }, kind = "missile", pick = "random", range = 22,
		damage = { 22, 28, 34, 40, 48 }, count = { 1, 2, 2, 3, 4 }, cooldown = { 1.8, 1.7, 1.6, 1.5, 1.3 },
		mastery = { text = "Overload: each bolt has a 1 in 3 chance to strike another enemy.", chance = 1 / 3 },
	},
	nova = {
		spell = 1449, blurb = "Bursts arcane power at every enemy around you.", color = { 0.85, 0.55, 1 }, kind = "nova",
		damage = { 10, 14, 18, 22, 28 }, radius = { 6, 6.5, 7, 7.5, 8 }, cooldown = { 5, 4.6, 4.2, 3.8, 3.4 },
		mastery = { text = "Echo: each explosion bursts a second time." },
	},
	chain = {
		spell = 421, blurb = "Leaps from enemy to enemy.",
		color = { 0.6, 0.75, 1 }, kind = "chain", pick = "nearest", range = 22, hop = 8,
		damage = { 20, 24, 30, 36, 44 }, jumps = { 3, 4, 4, 5, 6 }, cooldown = { 2.4, 2.2, 2.0, 1.8, 1.6 },
		mastery = { text = "Each leap strikes 20% harder than the one before.", grow = 1.2 },
	},
	corruption = {
		spell = 172, blurb = "Rots enemies every second, and spreads when they die.",
		color = { 0.55, 0.85, 0.3 }, kind = "dot", pick = "nearest", range = 22, hop = 6,
		damage = { 6, 8, 10, 12, 15 }, count = { 2, 3, 3, 4, 5 }, spread = { 1, 1, 2, 2, 3 },
		duration = { 6, 6, 7, 7, 8 }, cooldown = { 3, 3, 2.8, 2.6, 2.4 },
		mastery = { text = "Corrupted enemies take 25% more damage from everything.", amplify = 1.25 },
	},
	holynova = {
		spell = 15237, blurb = "Burns enemies around you, and heals you for each one.",
		color = { 1, 0.95, 0.6 }, kind = "nova",
		damage = { 12, 16, 20, 26, 32 }, radius = { 6, 6.5, 7, 7.5, 8 }, heal = { 1, 1, 1.5, 1.5, 2 },
		cooldown = { 4, 3.7, 3.4, 3.1, 2.8 },
		mastery = { text = "Enemies it burns are dazed: half speed for 2 seconds.", slow = 0.5, seconds = 2 },
	},
	-- A fire totem model (display 4589, FireElementalTotem.mdx) that pulses
	-- as the real Magma Totem does (its pulse spell 8187, whose visual puts an
	-- Immolate impact on each enemy it burns).
	totem = {
		spell = 8190, pulse = 8187, display = 4589,
		blurb = "Plants a totem that burns every enemy near it every 2 seconds.",
		color = { 1, 0.5, 0.15 }, kind = "totem", pulseEvery = 2,
		damage = { 14, 18, 23, 28, 35 }, radius = { 6, 6.5, 7, 7.5, 8 }, totems = { 1, 1, 2, 2, 3 },
		duration = { 12, 12, 14, 14, 16 }, cooldown = { 14, 13, 12, 11, 10 },
		mastery = { text = "When a totem's time runs out, it erupts for triple damage.", burst = 3 },
	},
	-- Ground areas: the spell's own area model (SetModelBySpellArea) stands
	-- where it lands and burns every enemy inside it each second. An area
	-- with `slow` also slows them to that share of their speed.
	blizzard = {
		spell = 10, blurb = "Ice storms down on the thickest pack and chills it.",
		color = { 0.55, 0.8, 1 }, kind = "area", place = "pack", range = 22,
		damage = { 8, 10, 13, 16, 20 }, radius = { 5, 5.5, 6, 6.5, 7 }, slow = { 0.7, 0.65, 0.6, 0.55, 0.5 },
		duration = { 4, 4, 5, 5, 6 },
		cooldown = { 6, 5.5, 5, 4.5, 4 },
		mastery = { text = "Deep Freeze: when the storm ends, it freezes everything inside for 2 seconds.", freeze = 2 },
	},
	rainoffire = {
		spell = 5740, blurb = "Fire rains on the thickest pack.",
		color = { 1, 0.45, 0.15 }, kind = "area", place = "pack", range = 22,
		damage = { 14, 18, 23, 28, 35 }, radius = { 4, 4.4, 4.8, 5.2, 6 }, duration = { 3, 3, 3, 4, 4 },
		cooldown = { 7, 6.5, 6, 5.5, 5 },
		mastery = { text = "A second storm falls on another enemy." },
	},
	consecration = {
		spell = 26573, blurb = "Hallows the ground at your feet.",
		color = { 1, 0.85, 0.4 }, kind = "area", place = "self",
		damage = { 6, 8, 10, 13, 16 }, radius = { 5, 5.5, 6, 6.5, 7 }, duration = { 6, 6, 7, 7, 8 },
		cooldown = { 7, 6.5, 6, 5.5, 5 },
		mastery = { text = "The hallowed ground moves with you." },
	},
	thunder = {
		spell = 6343, blurb = "Shakes the ground close around you.", color = { 1, 0.6, 0.2 }, kind = "nova",
		damage = { 16, 20, 26, 32, 40 }, radius = { 4, 4.4, 4.8, 5.2, 6 }, cooldown = { 3, 2.8, 2.6, 2.4, 2.2 },
		mastery = { text = "Aftershock: the clap throws the brood back.", knock = { 8, 5 } },
	},
	-- Tanky spells keep the brood off you. `kit` is a SpellVisualKit shown
	-- while the effect lasts: the shield's bubble, Frost Nova's ice on each
	-- frozen enemy, Psychic Scream's terror over each fleeing one.
	shield = {
		spell = 17, blurb = "A shield soaks up the brood's bites until it breaks.",
		color = { 1, 1, 0.75 }, kind = "shield",
		absorb = { 30, 40, 50, 60, 75 }, duration = { 10, 10, 12, 12, 15 }, cooldown = { 15, 14, 13, 12, 11 },
		mastery = { text = "When the shield breaks, it bursts and hurls the brood around you away.", radius = 6, damage = 30, knock = { 12, 8 } },
	},
	thorns = {
		spell = 467, blurb = "Every enemy biting you takes damage each second.",
		color = { 0.6, 0.9, 0.3 }, kind = "thorns",
		damage = { 6, 8, 10, 13, 16 }, duration = { 8, 9, 10, 11, 12 }, cooldown = { 14, 13, 12, 11, 10 },
		mastery = { text = "You heal 1 health for every biter it burns.", heal = 1 },
	},
	frostnova = {
		spell = 122, blurb = "Freezes every enemy around you in place.",
		color = { 0.6, 0.85, 1 }, kind = "nova",
		damage = { 6, 8, 10, 12, 15 }, radius = { 6, 6.5, 7, 7.5, 8 }, root = { 2, 2.5, 3, 3.5, 4 },
		cooldown = { 10, 9.5, 9, 8.5, 8 },
		mastery = { text = "Shatter: frozen enemies take double damage.", amplify = 2 },
	},
	scream = {
		spell = 8122, blurb = "Every enemy around you flees in terror.",
		color = { 0.75, 0.5, 1 }, kind = "nova",
		radius = { 6, 6.5, 7, 7.5, 8 }, fear = { 2, 2.5, 3, 3.5, 4 }, cooldown = { 14, 13, 12, 11, 10 },
		mastery = { text = "Terror: fleeing enemies take 10 damage a second.", damage = 10 },
	},
	demoshout = {
		spell = 1160, blurb = "Enemies around you bite softer for a while.",
		color = { 1, 0.55, 0.35 }, kind = "nova",
		radius = { 8, 8.5, 9, 9.5, 10 }, weaken = { 0.3, 0.35, 0.4, 0.45, 0.5 }, duration = { 6, 7, 8, 9, 10 },
		cooldown = { 10, 9.5, 9, 8.5, 8 },
		mastery = { text = "Battle cry: you run 20% faster while it lasts.", speed = 1.2 },
	},
	stoneclaw = {
		spell = 5730, display = 4588, blurb = "Plants a totem that the brood around it must attack.",
		color = { 0.8, 0.65, 0.4 }, kind = "ward",
		radius = { 6, 7, 8, 9, 10 }, duration = { 5, 6, 7, 8, 9 }, cooldown = { 16, 15, 14, 13, 12 },
		mastery = { text = "When the totem crumbles, it stuns the brood around it for 2 seconds.", stun = 2, aura = 20549 },
	},
	-- Fun ones. `knock` is the knockback (out, up), as the items' are.
	blastwave = {
		spell = 11113, blurb = "A ring of fire hurls the brood away from you.",
		color = { 1, 0.5, 0.2 }, kind = "nova", knock = { 10, 7 },
		damage = { 18, 24, 30, 38, 48 }, radius = { 6, 6.5, 7, 7.5, 8 }, cooldown = { 7, 6.6, 6.2, 5.8, 5.4 },
		mastery = { text = "The brood it hits is dazed: half speed for 3 seconds.", slow = 0.5, seconds = 3 },
	},
	-- Channels: the hero holds the spell's beam on one enemy (PlaySpellVisual's
	-- channel) and burns it every second for the duration.
	drainlife = {
		spell = 689, blurb = "A beam drains the toughest enemy in range, healing you every second.",
		color = { 0.55, 0.9, 0.4 }, kind = "channel", pick = "strongest", range = 20,
		damage = { 8, 10, 13, 16, 20 }, heal = { 2, 2.5, 3, 3.5, 4 }, duration = { 5, 5, 6, 6, 7 },
		cooldown = { 8, 7.5, 7, 6.5, 6 },
		mastery = { text = "When its target dies, the beam leaps to the next enemy.", leap = 8 },
	},
	mindflay = {
		spell = 15407, blurb = "A beam flays the toughest enemy in range and slows it.",
		color = { 0.75, 0.55, 1 }, kind = "channel", pick = "strongest", range = 22,
		damage = { 14, 18, 23, 28, 35 }, slow = { 0.5, 0.45, 0.4, 0.35, 0.3 }, duration = { 3, 3, 4, 4, 5 },
		cooldown = { 6, 5.5, 5, 4.5, 4 },
		mastery = { text = "Each tick also flays every enemy within 3 yards of its target.", radius = 3 },
	},
	-- The imp (display 4449) stays for the run and casts the real Firebolt.
	imp = {
		spell = 688, missile = 3110, display = 4449, blurb = "An imp follows you and hurls Firebolts.",
		color = { 1, 0.55, 0.2 }, kind = "pet", range = 22,
		damage = { 12, 15, 19, 24, 30 }, cooldown = { 1.6, 1.5, 1.4, 1.3, 1.2 },
		mastery = { text = "The imp hurls two Firebolts at once.", bolts = 2 },
	},
	-- Inferno (1122) plants its meteor at the thickest pack
	-- (PlaySpellVisualAtPoint). Where it lands, the brood is burned and
	-- stunned (Inferno Effect 22703: 2 s within 10 yards), and an Infernal
	-- (display 169) climbs out, rooted for a moment (22707: 1.7 s), then
	-- hunts, taunting the whelps it burns.
	inferno = {
		spell = 1122, display = 169, stunAura = 22703, -- Inferno Effect: its state kit 349 is StunSwirl
		blurb = "A meteor crashes into the thickest pack and an Infernal climbs out to fight.",
		color = { 1, 0.5, 0.15 }, kind = "inferno", range = 24,
		damage = { 40, 55, 70, 90, 115 }, radius = { 8, 8, 9, 9, 10 },
		hit = { 14, 18, 23, 29, 36 }, burn = { 3, 4, 5, 6, 8 }, duration = { 12, 13, 14, 15, 16 },
		cooldown = { 30, 28, 26, 24, 22 },
		mastery = { text = "Two meteors fall, the second on another part of the brood.", meteors = 2 },
	},
	-- Lightning Shield (324): its orbs circle you as the aura's state kit
	-- (AddAuraVisual, kit 292). An orb strikes a whelp that bites you, with
	-- the spell's impact kit 62 on it; the shield goes with its last orb.
	lshield = {
		spell = 324, zapKit = 62, blurb = "Orbs of lightning circle you and strike the whelps that bite you.",
		color = { 0.6, 0.75, 1 }, kind = "lshield",
		damage = { 18, 23, 29, 36, 45 }, orbs = { 3, 3, 4, 5, 6 }, cooldown = { 9, 8.5, 8, 7.5, 7 },
		mastery = { text = "Each strike also jumps to a second whelp nearby.", jump = 5 },
	},
	-- Polymorph (118): the toughest enemies in range become Polymorphed Sheep
	-- (creature 16372, display 856) that wander, harmless, until it wears off.
	polymorph = {
		spell = 118, display = 856, blurb = "Turns the toughest enemies in range into harmless sheep.",
		color = { 0.85, 0.55, 1 }, kind = "polymorph", pick = "strongest", range = 20,
		count = { 1, 1, 2, 2, 3 }, duration = { 4, 5, 5, 6, 6 }, cooldown = { 10, 9.5, 9, 8.5, 8 },
		mastery = { text = "Every whelp near each target becomes a sheep too.", radius = 3 },
	},
	-- Arcane Missiles (5143): a channel (kit 729) that fires the real Arcane
	-- Missile (7268) at its target each second, for 3 s (duration 27).
	arcanemissiles = {
		spell = 5143, missile = 7268, blurb = "Channels a missile into the toughest enemy in range every second.",
		color = { 0.85, 0.55, 1 }, kind = "channel", pick = "strongest", range = 22,
		damage = { 14, 18, 23, 29, 36 }, duration = { 3, 3, 4, 4, 5 }, cooldown = { 6, 5.5, 5, 4.5, 4 },
		mastery = { text = "Each missile splits into three, at the enemies nearest its target.", split = 3 },
	},
	-- Hellfire (1949): a channel on yourself (kit 4809). Each second its
	-- trigger, Hellfire Effect (5857), bursts around you (cast kit 844) and
	-- burns the brood in reach, each hit lit by its impact kit (845); and it
	-- burns you, as the spell's own damage aura (3) does.
	hellfire = {
		spell = 1949, tick = 5857, impactKit = 845,
		blurb = "Channels fire around you that burns the brood close by, and you with it.",
		color = { 1, 0.45, 0.15 }, kind = "channel", self = true,
		damage = { 12, 15, 19, 24, 30 }, radius = { 6, 6, 7, 7, 8 }, selfBurn = { 3, 3, 3, 3, 3 },
		duration = { 4, 4, 5, 5, 6 }, cooldown = { 9, 8.5, 8, 7.5, 7 },
		mastery = { text = "The fire heals you instead of burning you.", heal = 3 },
	},
	-- Evolutions (EVOLUTIONS): a spell at rank V and the passive it needs
	-- become one of these, in the spell's place. Each has one rank and is
	-- never offered on its own.
	-- Frostbolt Volley (8398): the real volley's frostbolts, one at each
	-- target at once; each chills.
	frostvolley = {
		spell = 8398, evolved = true, blurb = "Frostbolts fly at many enemies at once, chilling each one.",
		color = { 0.55, 0.8, 1 }, kind = "missile", volley = true, pick = "nearest", range = 24,
		-- Four bolts a volley: each missile starts its own flight loop at the
		-- hand (PlaySoundLoop3D, as in the world), and eight at once roared.
		damage = { 32 }, count = { 4 }, slow = { 0.5 }, cooldown = { 0.9 },
	},
	-- Shadow Bolt Volley (9081): heavy bolts at the toughest enemies at once.
	shadowvolley = {
		spell = 9081, evolved = true, blurb = "Heavy shadow bolts fly at the toughest enemies at once.",
		color = { 0.8, 0.45, 1 }, kind = "missile", volley = true, pick = "strongest", range = 26,
		damage = { 110 }, count = { 5 }, cooldown = { 2.2 },
	},
	-- Siphon Life (18265): Corruption's rot that heals you as it ticks.
	siphonlife = {
		spell = 18265, evolved = true, blurb = "Rots enemies every second and heals you as it does. It spreads when they die.",
		color = { 0.55, 0.85, 0.3 }, kind = "dot", pick = "nearest", range = 22, hop = 6,
		damage = { 18 }, heal = { 0.3 }, count = { 6 }, spread = { 3 }, duration = { 8 }, cooldown = { 2.2 },
	},
	-- Divine Shield (642): nothing hurts you while its bubble (state kit 252)
	-- lasts.
	divineshield = {
		spell = 642, evolved = true, blurb = "For a few seconds nothing can hurt you.",
		color = { 1, 0.9, 0.5 }, kind = "shield", immune = true, duration = { 5 }, cooldown = { 18 },
	},
	-- War Stomp (45): stuns everything around you (its state kit 349 pins
	-- Stun), and its cast kit (356) shakes the camera.
	warstomp = {
		spell = 45, evolved = true, blurb = "Stomps the ground, hurting and stunning everything around you.",
		color = { 1, 0.6, 0.2 }, kind = "nova", damage = { 45 }, radius = { 8 }, root = { 1.5 }, cooldown = { 3 },
	},
	-- Flamestrike (2120): a large burning ground area on the thickest pack.
	flamestrike = {
		spell = 2120, evolved = true, blurb = "A pillar of fire scorches the thickest pack and leaves the ground burning.",
		color = { 1, 0.45, 0.15 }, kind = "area", place = "pack", range = 22,
		damage = { 40 }, radius = { 8 }, duration = { 5 }, cooldown = { 5 },
	},
	-- Death Coil (6789): a missile that harms its target, heals you, and
	-- fears the target for 3 s (duration 27).
	deathcoil = {
		spell = 6789, blurb = "A coil of shadow harms the nearest enemy, heals you and sends it fleeing.",
		color = { 0.55, 0.85, 0.3 }, kind = "missile", pick = "nearest", range = 22,
		damage = { 25, 32, 40, 50, 62 }, heal = { 4, 5, 6, 7, 8 }, fear = { 3, 3, 3, 3, 3 },
		cooldown = { 7, 6.5, 6, 5.5, 5 },
		mastery = { text = "The coil fears every enemy around its target.", radius = 5 },
	},
}
local SPELL_ORDER = {
	"frostbolt", "shadowbolt", "lightning", "chain", "corruption", "nova", "holynova", "thunder", "totem", "blizzard",
	"rainoffire", "consecration", "shield", "thorns", "frostnova", "scream", "demoshout", "stoneclaw", "blastwave", "imp",
	"drainlife", "mindflay", "inferno", "lshield", "polymorph", "arcanemissiles", "deathcoil", "hellfire",
	-- The evolutions, on the bar after the others.
	"frostvolley", "shadowvolley", "siphonlife", "divineshield", "warstomp", "flamestrike",
}
-- A spell at rank V with the passive it `needs` (any rank) evolves `into` a
-- stronger spell, which takes its place.
local EVOLUTIONS = {
	frostbolt = { needs = "multishot", into = "frostvolley" },
	shadowbolt = { needs = "might", into = "shadowvolley" },
	corruption = { needs = "vampiric", into = "siphonlife" },
	shield = { needs = "vigor", into = "divineshield" },
	thunder = { needs = "devotion", into = "warstomp" },
	rainoffire = { needs = "amplify", into = "flamestrike" },
}
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
	sprint = { icon = 2983, text = "+8% run speed", total = "+%d%% run speed", per = 8 },
	devotion = { icon = 465, text = "-5% damage taken", total = "-%d%% damage taken", per = 5 },
	amplify = { icon = 1008, text = "+10% spell radius", total = "+%d%% spell radius", per = 10 },
	multishot = {
		icon = 2643, text = "+1 target for bolts and Corruption", total = "+%d targets", per = 1,
		max = 3,
	},
	wisdom = { icon = 19742, text = "+15% experience", total = "+%d%% experience", per = 15 },
	soulstone = {
		name = "Soulstone", icon = 20707, text = "Once per run, rise again with half your health",
		total = "%d revive", per = 1, max = 1,
	},
	vampiric = {
		icon = 15286,
		text = "Heals you for 0.2% of the damage you deal, at most 2 health a second",
		total = "up to %d health a second", per = 2,
	},
}
-- The names are the client's own, in its language: each spell's from its
-- spell, and each passive without a name of its own from its icon's spell.
for _, def in pairs(SPELLS) do
	def.name = C_Spell.GetSpellName(def.spell)
end
for _, p in pairs(PASSIVES) do
	p.name = p.name or C_Spell.GetSpellName(p.icon)
end

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
	CHEST_REACH = 2,           -- yards: walking this close opens a chest
	CHEST_AWAY = 2.5,          -- yards from the elite's corpse that its chest lands
	CHEST_LINGER = 4,          -- seconds an open chest stays before it fades
	EGG_FIRST = 60,            -- the second of the run the first clutch is laid
	EGG_EVERY = 75,            -- seconds between clutches
	EGG_CLUTCH = 4,            -- eggs in a clutch
	EGG_SPREAD = 2.5,          -- yards from the clutch's middle to each egg
	EGG_HATCH = 20,            -- seconds an egg takes to hatch
	EGG_WARN = 5,              -- seconds before it hatches that a red ring marks it
	EGG_WHELPS = 4,            -- whelps that hatch from each egg
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
	FEAR_STEP = 6,             -- yards a fleeing enemy runs before its next MoveTo
	IMP_SPEED = 8.5,
	IMP_FOLLOW = 3,            -- yards: farther from you than this, the imp follows
	INFERNO_FALL = 1.0,        -- seconds from the cast until the meteor strikes the ground
	INFERNO_STUN = 2,          -- Inferno Effect's stun (22703, duration 39)
	INFERNAL_ROOT = 1.7,       -- the Infernal's landing (22707, duration 487)
	INFERNAL_SPEED = 7,
	INFERNAL_REACH = 2.5,
	INFERNAL_SWING = 2,        -- seconds between two blows
	INFERNAL_BURN_RADIUS = 4,  -- yards its Immolation burns, once a second
	INFERNAL_TAUNT = 3,        -- seconds a burn holds the whelps on it
	ZAP_GAP = 0.5,             -- seconds between two of Lightning Shield's strikes
	SHEEP_SPEED = 1.5,
	SHEEP_WANDER = 2,          -- yards a sheep ambles before its next MoveTo
	HELLFIRE_IMPACTS = 8,      -- hit whelps a Hellfire tick lights with its impact kit
}
-- `itemID` is the real item (its name and icon), `spell` the visual its use plays.
local ITEMS = {
	{ key = "potion", itemID = 929, spell = 17534, weight = 30 },
	-- The grenade stuns with War Stomp's silent StunSwirl (20549): its own
	-- stun kit (19769's 780) plays Gouge on every whelp it holds, with no
	-- duplicate guard, a roar over a whole pack.
	{ key = "grenade", itemID = 15993, spell = 19769, stunAura = 20549, weight = 25 },
	{ key = "swift", itemID = 2459, spell = 2379, weight = 15 },
	{ key = "hearth", itemID = 6948, spell = 8690, weight = 10 },
	{ key = "sapper", itemID = 10646, spell = 13241, weight = 10 },
	{ key = "chicken", itemID = 10725, spell = 23133, weight = 10 },
}
-- Chests: an elite drops one, and every GOLDEN_EVERY-th is golden. Walking up
-- opens it (Open 148, then Opened 149 held, as the client's chests do), and
-- the chest gives `picks` upgrade cards. `open` is the Open sequence's length
-- in the model's file; `speed` plays it faster. The two models are about
-- 1.3 and 3.1 yards across at scale 1.
local CHEST_SOUND = 1277 -- ChestOpen (SoundEntries)
local GOLDEN_EVERY = 5
local CHESTS = {
	plain = { path = "World\\SkillActivated\\Containers\\TreasureChest01.m2", scale = 1.2, open = 0.666, speed = 1, picks = 1, title = "Treasure" },
	golden = { path = "World\\Goober\\G_ChestTitan.m2", scale = 0.6, open = 3.2, speed = 2, picks = 3, title = "Golden Treasure" },
}
-- Each item's name and icon are the client's (the Item mixin), once its data
-- has come from the server.
for _, item in ipairs(ITEMS) do
	local data = Item:CreateFromItemID(item.itemID);
	data:ContinueOnItemLoad(function()
		item.name = data:GetItemName();
		item.icon = data:GetItemIcon();
	end);
end

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
BW.ANIM_ATTACK_UNARMED = ANIM_ATTACK_UNARMED
BW.HERO_HP = HERO_HP
BW.MAGNET = MAGNET
BW.GEM_SPEED = GEM_SPEED
BW.GEM_TAKE = GEM_TAKE
BW.GEM_PATH = GEM_PATH
BW.MAX_GEMS = MAX_GEMS
BW.KINDS = KINDS
BW.ELITE = ELITE
BW.EGG = EGG
BW.SPELLS = SPELLS
BW.SPELL_ORDER = SPELL_ORDER
BW.EVOLUTIONS = EVOLUTIONS
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
BW.CHEST_SOUND = CHEST_SOUND
BW.GOLDEN_EVERY = GOLDEN_EVERY
BW.CHESTS = CHESTS
