

-- Item location bitflags (Using tonumber because lua 5.0 doesn't support hexadecimal literals. 0x)
ITEM_INVENTORY_LOCATION_PLAYER		= tonumber("0x00100000", 16);
ITEM_INVENTORY_LOCATION_BAGS		= tonumber("0x00200000", 16);
ITEM_INVENTORY_LOCATION_BANK		= tonumber("0x00400000", 16);
ITEM_INVENTORY_BAG_BIT_OFFSET 		= 8; -- Number of bits that the bag index in GetInventoryItemsForSlot gets shifted to the left.
ITEM_INVENTORY_BANK_BAG_OFFSET      = 4;

-- Inventory slots
INVSLOT_AMMO		= 0;
INVSLOT_HEAD 		= 1; INVSLOT_FIRST_EQUIPPED = INVSLOT_HEAD;
INVSLOT_NECK		= 2;
INVSLOT_SHOULDER	= 3;
INVSLOT_BODY		= 4;
INVSLOT_CHEST		= 5;
INVSLOT_WAIST		= 6;
INVSLOT_LEGS		= 7;
INVSLOT_FEET		= 8;
INVSLOT_WRIST		= 9;
INVSLOT_HAND		= 10;
INVSLOT_FINGER1		= 11;
INVSLOT_FINGER2		= 12;
INVSLOT_TRINKET1	= 13;
INVSLOT_TRINKET2	= 14;
INVSLOT_BACK		= 15;
INVSLOT_MAINHAND	= 16;
INVSLOT_OFFHAND		= 17;
INVSLOT_RANGED		= 18;
INVSLOT_TABARD		= 19; INVSLOT_LAST_EQUIPPED = INVSLOT_TABARD;

INVSLOTS_EQUIPABLE_IN_COMBAT = {
	[INVSLOT_MAINHAND] = true,
	[INVSLOT_OFFHAND] = true,
	[INVSLOT_RANGED] = true,
}

-- Item IDs
HEARTHSTONE_ITEM_ID = 6948;

--
-- Equipment Set
--
MAX_EQUIPMENT_SETS_PER_PLAYER = 10;
EQUIPMENT_SET_EMPTY_SLOT = 0;
EQUIPMENT_SET_IGNORED_SLOT = 1;
EQUIPMENT_SET_ITEM_MISSING = -1;

--
-- Totems
--

MAX_TOTEMS = 4;

FIRE_TOTEM_SLOT = 1;
EARTH_TOTEM_SLOT = 2;
WATER_TOTEM_SLOT = 3;
AIR_TOTEM_SLOT = 4;

STANDARD_TOTEM_PRIORITIES = {1, 2, 3, 4};

SHAMAN_TOTEM_PRIORITIES = {
	EARTH_TOTEM_SLOT,
	FIRE_TOTEM_SLOT,
	WATER_TOTEM_SLOT,
	AIR_TOTEM_SLOT,
};

-- Druid Forms.
CAT_FORM = 1;
BEAR_FORM = 5;

-- Forms, as 3.80.2's Cata/Constants.lua names them; the IDs are this
-- client's SpellShapeshiftForm rows, which keep these numbers. Flight form
-- (27) and the second moonkin row (35) do not exist here.
DRUID_CAT_FORM = 1;
DRUID_TREE_FORM = 2;
DRUID_TRAVEL_FORM = 3;
DRUID_ACQUATIC_FORM = 4;
DRUID_BEAR_FORM = 5;
DRUID_DIRE_BEAR_FORM = 8;
SHAMAN_GHOST_WOLF_FORM = 16;
PRIEST_SHADOWFORM = 28;
ROGUE_STEALTH = 30;
DRUID_MOONKIN_FORM_1 = 31;

-- The character sheet scene's actor for each animal form
-- (ModelSceneUtil.SetUpCharacterSheetScene). This client's Dire Bear form is
-- a bear.
ANIMAL_FORMS = {
	[DRUID_CAT_FORM] = {actorTag = "druid-cat-form"};
	[DRUID_TREE_FORM] = {actorTag = "animal-form"};
	[DRUID_TRAVEL_FORM] = {actorTag = "druid-travel-form"};
	[DRUID_ACQUATIC_FORM] = {actorTag = "druid-acquatic-form"};
	[DRUID_BEAR_FORM] = {actorTag = "druid-bear-form"};
	[DRUID_DIRE_BEAR_FORM] = {actorTag = "druid-bear-form"};
	[SHAMAN_GHOST_WOLF_FORM] = {actorTag = "animal-form"};
	[DRUID_MOONKIN_FORM_1] = {actorTag = "druid-moonkin-form"};
};

--
-- Class
--
CLASS_SORT_ORDER = {
	"WARRIOR",
	"PALADIN",
	"PRIEST",
	"SHAMAN",
	"DRUID",
	"ROGUE",
	"MAGE",
	"WARLOCK",
	"HUNTER",
};
MAX_CLASSES = table.getn(CLASS_SORT_ORDER);

LOCALIZED_CLASS_NAMES_MALE = {};
LOCALIZED_CLASS_NAMES_FEMALE = {};
FillLocalizedClassList(LOCALIZED_CLASS_NAMES_MALE, false);
FillLocalizedClassList(LOCALIZED_CLASS_NAMES_FEMALE, true);

--
-- Inventory
--
BAG_ITEM_QUALITY_COLORS =
{
	[LE_ITEM_QUALITY_COMMON] = COMMON_GRAY_COLOR,
	[LE_ITEM_QUALITY_UNCOMMON] = UNCOMMON_GREEN_COLOR,
	[LE_ITEM_QUALITY_RARE] = RARE_BLUE_COLOR,
	[LE_ITEM_QUALITY_EPIC] = EPIC_PURPLE_COLOR,
	[LE_ITEM_QUALITY_LEGENDARY] = LEGENDARY_ORANGE_COLOR,
	[LE_ITEM_QUALITY_ARTIFACT] = ARTIFACT_GOLD_COLOR,
};

-- faction
PLAYER_FACTION_GROUP = { [0] = "Horde", [1] = "Alliance", Horde = 0, Alliance = 1 };

FACTION_LABELS = {
	[0] = FACTION_HORDE,
	[1] = FACTION_ALLIANCE,
};

FACTION_LABELS_FROM_STRING = {
	["Horde"] = FACTION_HORDE,
	["Alliance"] = FACTION_ALLIANCE,
}

--
-- Glyph
--
INSCRIPTION_AVAILABLE = false;

-- PaperDollFrame.lua
EQUIPPED_FIRST = 1;
EQUIPPED_LAST = 19;

BASE_MOVEMENT_SPEED = 7;

UPPER_LEFT_VERTEX = 1;
LOWER_LEFT_VERTEX = 2;
UPPER_RIGHT_VERTEX = 3;
LOWER_RIGHT_VERTEX = 4;
-- Combat log object flags: sourceFlags / destFlags of COMBAT_LOG_EVENT.
COMBATLOG_OBJECT_AFFILIATION_MINE		= tonumber("00000001", 16);
COMBATLOG_OBJECT_AFFILIATION_PARTY		= tonumber("00000002", 16);
COMBATLOG_OBJECT_AFFILIATION_RAID		= tonumber("00000004", 16);
COMBATLOG_OBJECT_AFFILIATION_OUTSIDER	= tonumber("00000008", 16);
COMBATLOG_OBJECT_AFFILIATION_MASK		= tonumber("0000000F", 16);
COMBATLOG_OBJECT_REACTION_FRIENDLY		= tonumber("00000010", 16);
COMBATLOG_OBJECT_REACTION_NEUTRAL		= tonumber("00000020", 16);
COMBATLOG_OBJECT_REACTION_HOSTILE		= tonumber("00000040", 16);
COMBATLOG_OBJECT_REACTION_MASK			= tonumber("000000F0", 16);
COMBATLOG_OBJECT_CONTROL_PLAYER			= tonumber("00000100", 16);
COMBATLOG_OBJECT_CONTROL_NPC			= tonumber("00000200", 16);
COMBATLOG_OBJECT_CONTROL_MASK			= tonumber("00000300", 16);
COMBATLOG_OBJECT_TYPE_PLAYER			= tonumber("00000400", 16);
COMBATLOG_OBJECT_TYPE_NPC				= tonumber("00000800", 16);
COMBATLOG_OBJECT_TYPE_PET				= tonumber("00001000", 16);
COMBATLOG_OBJECT_TYPE_GUARDIAN			= tonumber("00002000", 16);
COMBATLOG_OBJECT_TYPE_OBJECT			= tonumber("00004000", 16);
COMBATLOG_OBJECT_TYPE_MASK				= tonumber("0000FC00", 16);
COMBATLOG_OBJECT_TARGET					= tonumber("00010000", 16);
COMBATLOG_OBJECT_FOCUS					= tonumber("00020000", 16);
COMBATLOG_OBJECT_MAINTANK				= tonumber("00040000", 16);
COMBATLOG_OBJECT_MAINASSIST				= tonumber("00080000", 16);
COMBATLOG_OBJECT_NONE					= tonumber("80000000", 16);
COMBATLOG_OBJECT_SPECIAL_MASK			= tonumber("FFFF0000", 16);

-- Combat log raid flags: sourceRaidFlags / destRaidFlags of COMBAT_LOG_EVENT.
COMBATLOG_OBJECT_RAIDTARGET1			= tonumber("00000001", 16);
COMBATLOG_OBJECT_RAIDTARGET2			= tonumber("00000002", 16);
COMBATLOG_OBJECT_RAIDTARGET3			= tonumber("00000004", 16);
COMBATLOG_OBJECT_RAIDTARGET4			= tonumber("00000008", 16);
COMBATLOG_OBJECT_RAIDTARGET5			= tonumber("00000010", 16);
COMBATLOG_OBJECT_RAIDTARGET6			= tonumber("00000020", 16);
COMBATLOG_OBJECT_RAIDTARGET7			= tonumber("00000040", 16);
COMBATLOG_OBJECT_RAIDTARGET8			= tonumber("00000080", 16);
COMBATLOG_OBJECT_RAIDTARGET_MASK		= tonumber("000000FF", 16);

-- Combat log filter masks, for CombatLogAddFilter and CombatLog_Object_IsA.
COMBATLOG_FILTER_ME			= bit.bor(
						COMBATLOG_OBJECT_AFFILIATION_MINE,
						COMBATLOG_OBJECT_REACTION_FRIENDLY,
						COMBATLOG_OBJECT_CONTROL_PLAYER,
						COMBATLOG_OBJECT_TYPE_PLAYER
						);
						
COMBATLOG_FILTER_MINE			= bit.bor(
						COMBATLOG_OBJECT_AFFILIATION_MINE,
						COMBATLOG_OBJECT_REACTION_FRIENDLY,
						COMBATLOG_OBJECT_CONTROL_PLAYER,
						COMBATLOG_OBJECT_TYPE_PLAYER,
						COMBATLOG_OBJECT_TYPE_OBJECT
						);

COMBATLOG_FILTER_MY_PET			= bit.bor(
						COMBATLOG_OBJECT_AFFILIATION_MINE,
						COMBATLOG_OBJECT_REACTION_FRIENDLY,
						COMBATLOG_OBJECT_CONTROL_PLAYER,
						COMBATLOG_OBJECT_TYPE_GUARDIAN,
						COMBATLOG_OBJECT_TYPE_PET
						);
COMBATLOG_FILTER_FRIENDLY_UNITS		= bit.bor(
						COMBATLOG_OBJECT_AFFILIATION_PARTY,
						COMBATLOG_OBJECT_AFFILIATION_RAID,
						COMBATLOG_OBJECT_AFFILIATION_OUTSIDER,
						COMBATLOG_OBJECT_REACTION_FRIENDLY,
						COMBATLOG_OBJECT_CONTROL_PLAYER,
						COMBATLOG_OBJECT_CONTROL_NPC,
						COMBATLOG_OBJECT_TYPE_PLAYER,
						COMBATLOG_OBJECT_TYPE_NPC,
						COMBATLOG_OBJECT_TYPE_PET,
						COMBATLOG_OBJECT_TYPE_GUARDIAN,
						COMBATLOG_OBJECT_TYPE_OBJECT
						);

COMBATLOG_FILTER_HOSTILE_PLAYERS	= bit.bor(
						COMBATLOG_OBJECT_AFFILIATION_PARTY,
						COMBATLOG_OBJECT_AFFILIATION_RAID,
						COMBATLOG_OBJECT_AFFILIATION_OUTSIDER,
						COMBATLOG_OBJECT_REACTION_HOSTILE,
						COMBATLOG_OBJECT_CONTROL_PLAYER,
						COMBATLOG_OBJECT_TYPE_PLAYER,
						COMBATLOG_OBJECT_TYPE_NPC,
						COMBATLOG_OBJECT_TYPE_PET,
						COMBATLOG_OBJECT_TYPE_GUARDIAN,
						COMBATLOG_OBJECT_TYPE_OBJECT
						);

COMBATLOG_FILTER_HOSTILE_UNITS		= bit.bor(
						COMBATLOG_OBJECT_AFFILIATION_PARTY,
						COMBATLOG_OBJECT_AFFILIATION_RAID,
						COMBATLOG_OBJECT_AFFILIATION_OUTSIDER,
						COMBATLOG_OBJECT_REACTION_HOSTILE,
						COMBATLOG_OBJECT_CONTROL_NPC,
						COMBATLOG_OBJECT_TYPE_PLAYER,
						COMBATLOG_OBJECT_TYPE_NPC,
						COMBATLOG_OBJECT_TYPE_PET,
						COMBATLOG_OBJECT_TYPE_GUARDIAN,
						COMBATLOG_OBJECT_TYPE_OBJECT
						);

COMBATLOG_FILTER_NEUTRAL_UNITS		= bit.bor(
						COMBATLOG_OBJECT_AFFILIATION_PARTY,
						COMBATLOG_OBJECT_AFFILIATION_RAID,
						COMBATLOG_OBJECT_AFFILIATION_OUTSIDER,
						COMBATLOG_OBJECT_REACTION_NEUTRAL,
						COMBATLOG_OBJECT_CONTROL_PLAYER,
						COMBATLOG_OBJECT_CONTROL_NPC,
						COMBATLOG_OBJECT_TYPE_PLAYER,
						COMBATLOG_OBJECT_TYPE_NPC,
						COMBATLOG_OBJECT_TYPE_PET,
						COMBATLOG_OBJECT_TYPE_GUARDIAN,
						COMBATLOG_OBJECT_TYPE_OBJECT
						);
COMBATLOG_FILTER_UNKNOWN_UNITS		= COMBATLOG_OBJECT_NONE;
COMBATLOG_FILTER_EVERYTHING =	tonumber("FFFFFFFF", 16);

-- Damage school masks (Classic's Blizzard_FrameXMLBase/Classic/Constants.lua).
-- GetSpellSchool's 1-based school ID n is the mask 2^(n-1).
SCHOOL_MASK_NONE		= 0;
SCHOOL_MASK_PHYSICAL	= 1;
SCHOOL_MASK_HOLY		= 2;
SCHOOL_MASK_FIRE		= 4;
SCHOOL_MASK_NATURE		= 8;
SCHOOL_MASK_FROST		= 16;
SCHOOL_MASK_SHADOW		= 32;
SCHOOL_MASK_ARCANE		= 64;

-- Retail defines this in Blizzard_UIPanels_Game/MerchantFrame.lua.
LIST_DELIMITER = ", ";

-- Retail defines this in Blizzard_SharedXML/SharedConstants.lua.
function GetClassAtlas(className)
	return ("classicon-%s"):format(className);
end

-- Retail defines these in Blizzard_SharedXML/SharedConstants.lua, into its
-- environment table; this client has no GetCurrentEnvironment, so the
-- binding strings are globals. KEY_PAD* draw the gamepad glyph atlases
-- (Texture::AtlasData); GetBindingText picks the _LTR / _SHP / _REV variant
-- for the active gamepad (Util\GamePadBindingText.lua).
local envTable = _G;

function SetGamepadBindingStrings(mainBinding, abbrBinding, name, opt_abbrName)
	local abbrName = opt_abbrName or name;
	envTable[mainBinding] = ("|A:Gamepad_%s_64:24:24|a"):format(name);
	envTable[abbrBinding] = ("|A:Gamepad_%s_32:14:14|a"):format(abbrName);
end
-- Generic GamePad button labels
SetGamepadBindingStrings("KEY_PADDUP",			"KEY_ABBR_PADDUP",			"Gen_Up");
SetGamepadBindingStrings("KEY_PADDRIGHT",		"KEY_ABBR_PADDRIGHT",		"Gen_Right");
SetGamepadBindingStrings("KEY_PADDDOWN",		"KEY_ABBR_PADDDOWN",		"Gen_Down");
SetGamepadBindingStrings("KEY_PADDLEFT",		"KEY_ABBR_PADDLEFT",		"Gen_Left");
SetGamepadBindingStrings("KEY_PAD1",			"KEY_ABBR_PAD1",			"Gen_1");
SetGamepadBindingStrings("KEY_PAD2",			"KEY_ABBR_PAD2",			"Gen_2");
SetGamepadBindingStrings("KEY_PAD3",			"KEY_ABBR_PAD3",			"Gen_3");
SetGamepadBindingStrings("KEY_PAD4",			"KEY_ABBR_PAD4",			"Gen_4");
SetGamepadBindingStrings("KEY_PAD5",			"KEY_ABBR_PAD5",			"Gen_5");
SetGamepadBindingStrings("KEY_PAD6",			"KEY_ABBR_PAD6",			"Gen_6");
SetGamepadBindingStrings("KEY_PADLSTICK",		"KEY_ABBR_PADLSTICK",		"Gen_LStickIn");
SetGamepadBindingStrings("KEY_PADRSTICK",		"KEY_ABBR_PADRSTICK",		"Gen_RStickIn");
SetGamepadBindingStrings("KEY_PADLSHOULDER",	"KEY_ABBR_PADLSHOULDER",	"Gen_LShoulder");
SetGamepadBindingStrings("KEY_PADRSHOULDER",	"KEY_ABBR_PADRSHOULDER",	"Gen_RShoulder");
SetGamepadBindingStrings("KEY_PADLTRIGGER",		"KEY_ABBR_PADLTRIGGER",		"Gen_LTrigger");
SetGamepadBindingStrings("KEY_PADRTRIGGER",		"KEY_ABBR_PADRTRIGGER",		"Gen_RTrigger");
SetGamepadBindingStrings("KEY_PADLSTICKUP",		"KEY_ABBR_PADLSTICKUP",		"Gen_LStickUp");
SetGamepadBindingStrings("KEY_PADLSTICKRIGHT",	"KEY_ABBR_PADLSTICKRIGHT",	"Gen_LStickRight");
SetGamepadBindingStrings("KEY_PADLSTICKDOWN",	"KEY_ABBR_PADLSTICKDOWN",	"Gen_LStickDown");
SetGamepadBindingStrings("KEY_PADLSTICKLEFT",	"KEY_ABBR_PADLSTICKLEFT",	"Gen_LStickLeft");
SetGamepadBindingStrings("KEY_PADRSTICKUP",		"KEY_ABBR_PADRSTICKUP",		"Gen_RStickUp");
SetGamepadBindingStrings("KEY_PADRSTICKRIGHT",	"KEY_ABBR_PADRSTICKRIGHT",	"Gen_RStickRight");
SetGamepadBindingStrings("KEY_PADRSTICKDOWN",	"KEY_ABBR_PADRSTICKDOWN",	"Gen_RStickDown");
SetGamepadBindingStrings("KEY_PADRSTICKLEFT",	"KEY_ABBR_PADRSTICKLEFT",	"Gen_RStickLeft");
SetGamepadBindingStrings("KEY_PADPADDLE1",		"KEY_ABBR_PADPADDLE1",		"Gen_Paddle1");
SetGamepadBindingStrings("KEY_PADPADDLE2",		"KEY_ABBR_PADPADDLE2",		"Gen_Paddle2");
SetGamepadBindingStrings("KEY_PADPADDLE3",		"KEY_ABBR_PADPADDLE3",		"Gen_Paddle3");
SetGamepadBindingStrings("KEY_PADPADDLE4",		"KEY_ABBR_PADPADDLE4",		"Gen_Paddle4");
SetGamepadBindingStrings("KEY_PADFORWARD",		"KEY_ABBR_PADFORWARD",		"Gen_Forward");
SetGamepadBindingStrings("KEY_PADBACK",			"KEY_ABBR_PADBACK",			"Gen_Back");
SetGamepadBindingStrings("KEY_PADSYSTEM",		"KEY_ABBR_PADSYSTEM",		"Gen_System");
SetGamepadBindingStrings("KEY_PADSOCIAL",		"KEY_ABBR_PADSOCIAL",		"Gen_Share");
-- "Letters" label style specializations
SetGamepadBindingStrings("KEY_PADDUP_LTR",		"KEY_ABBR_PADDUP_LTR",		"Ltr_Up",		"Gen_Up");
SetGamepadBindingStrings("KEY_PADDRIGHT_LTR",	"KEY_ABBR_PADDRIGHT_LTR",	"Ltr_Right",	"Gen_Right");
SetGamepadBindingStrings("KEY_PADDDOWN_LTR",	"KEY_ABBR_PADDDOWN_LTR",	"Ltr_Down",		"Gen_Down");
SetGamepadBindingStrings("KEY_PADDLEFT_LTR",	"KEY_ABBR_PADDLEFT_LTR",	"Ltr_Left",		"Gen_Left");
SetGamepadBindingStrings("KEY_PAD1_LTR",		"KEY_ABBR_PAD1_LTR",		"Ltr_A");
SetGamepadBindingStrings("KEY_PAD2_LTR",		"KEY_ABBR_PAD2_LTR",		"Ltr_B");
SetGamepadBindingStrings("KEY_PAD3_LTR",		"KEY_ABBR_PAD3_LTR",		"Ltr_X");
SetGamepadBindingStrings("KEY_PAD4_LTR",		"KEY_ABBR_PAD4_LTR",		"Ltr_Y");
SetGamepadBindingStrings("KEY_PADLSHOULDER_LTR","KEY_ABBR_PADLSHOULDER_LTR","Ltr_LShoulder","Gen_LShoulder");
SetGamepadBindingStrings("KEY_PADRSHOULDER_LTR","KEY_ABBR_PADRSHOULDER_LTR","Ltr_RShoulder","Gen_RShoulder");
SetGamepadBindingStrings("KEY_PADLTRIGGER_LTR",	"KEY_ABBR_PADLTRIGGER_LTR",	"Ltr_LTrigger",	"Gen_LTrigger");
SetGamepadBindingStrings("KEY_PADRTRIGGER_LTR",	"KEY_ABBR_PADRTRIGGER_LTR",	"Ltr_RTrigger",	"Gen_RTrigger");
SetGamepadBindingStrings("KEY_PADFORWARD_LTR",	"KEY_ABBR_PADFORWARD_LTR",	"Ltr_Menu");
SetGamepadBindingStrings("KEY_PADBACK_LTR",		"KEY_ABBR_PADBACK_LTR",		"Ltr_View");
SetGamepadBindingStrings("KEY_PADSYSTEM_LTR",	"KEY_ABBR_PADSYSTEM_LTR",	"Ltr_System");
SetGamepadBindingStrings("KEY_PADSOCIAL_LTR",	"KEY_ABBR_PADSOCIAL_LTR",	"Ltr_Share",	"Gen_Share");
-- "Shapes" label style specializations
SetGamepadBindingStrings("KEY_PADDUP_SHP",		"KEY_ABBR_PADDUP_SHP",		"Shp_Up",		"Gen_Up");
SetGamepadBindingStrings("KEY_PADDRIGHT_SHP",	"KEY_ABBR_PADDRIGHT_SHP",	"Shp_Right",	"Gen_Right");
SetGamepadBindingStrings("KEY_PADDDOWN_SHP",	"KEY_ABBR_PADDDOWN_SHP",	"Shp_Down",		"Gen_Down");
SetGamepadBindingStrings("KEY_PADDLEFT_SHP",	"KEY_ABBR_PADDLEFT_SHP",	"Shp_Left",		"Gen_Left");
SetGamepadBindingStrings("KEY_PAD1_SHP",		"KEY_ABBR_PAD1_SHP",		"Shp_Cross");
SetGamepadBindingStrings("KEY_PAD2_SHP",		"KEY_ABBR_PAD2_SHP",		"Shp_Circle");
SetGamepadBindingStrings("KEY_PAD3_SHP",		"KEY_ABBR_PAD3_SHP",		"Shp_Square");
SetGamepadBindingStrings("KEY_PAD4_SHP",		"KEY_ABBR_PAD4_SHP",		"Shp_Triangle");
SetGamepadBindingStrings("KEY_PAD5_SHP",		"KEY_ABBR_PAD5_SHP",		"Shp_MicMute");
SetGamepadBindingStrings("KEY_PAD6_SHP",		"KEY_ABBR_PAD6_SHP",		"Shp_TouchpadR");
SetGamepadBindingStrings("KEY_PADLSTICK_SHP",	"KEY_ABBR_PADLSTICK_SHP",	"Shp_LStickIn");
SetGamepadBindingStrings("KEY_PADRSTICK_SHP",	"KEY_ABBR_PADRSTICK_SHP",	"Shp_RStickIn");
SetGamepadBindingStrings("KEY_PADLSHOULDER_SHP","KEY_ABBR_PADLSHOULDER_SHP","Shp_LShoulder");
SetGamepadBindingStrings("KEY_PADRSHOULDER_SHP","KEY_ABBR_PADRSHOULDER_SHP","Shp_RShoulder");
SetGamepadBindingStrings("KEY_PADLTRIGGER_SHP",	"KEY_ABBR_PADLTRIGGER_SHP",	"Shp_LTrigger");
SetGamepadBindingStrings("KEY_PADRTRIGGER_SHP",	"KEY_ABBR_PADRTRIGGER_SHP",	"Shp_RTrigger");
SetGamepadBindingStrings("KEY_PADFORWARD_SHP",	"KEY_ABBR_PADFORWARD_SHP",	"Shp_Menu");
SetGamepadBindingStrings("KEY_PADBACK_SHP",		"KEY_ABBR_PADBACK_SHP",		"Shp_TouchpadL");
SetGamepadBindingStrings("KEY_PADSYSTEM_SHP",	"KEY_ABBR_PADSYSTEM_SHP",	"Shp_System");
SetGamepadBindingStrings("KEY_PADSOCIAL_SHP",	"KEY_ABBR_PADSOCIAL_SHP",	"Shp_Share");
-- "Reverse" label style specializations
SetGamepadBindingStrings("KEY_PAD1_REV",		"KEY_ABBR_PAD1_REV",		"Rev_B");
SetGamepadBindingStrings("KEY_PAD2_REV",		"KEY_ABBR_PAD2_REV",		"Rev_A");
SetGamepadBindingStrings("KEY_PAD3_REV",		"KEY_ABBR_PAD3_REV",		"Rev_Y");
SetGamepadBindingStrings("KEY_PAD4_REV",		"KEY_ABBR_PAD4_REV",		"Rev_X");
SetGamepadBindingStrings("KEY_PADLSHOULDER_REV","KEY_ABBR_PADLSHOULDER_REV","Rev_LShoulder");
SetGamepadBindingStrings("KEY_PADRSHOULDER_REV","KEY_ABBR_PADRSHOULDER_REV","Rev_RShoulder");
SetGamepadBindingStrings("KEY_PADLTRIGGER_REV",	"KEY_ABBR_PADLTRIGGER_REV",	"Rev_LTrigger");
SetGamepadBindingStrings("KEY_PADRTRIGGER_REV",	"KEY_ABBR_PADRTRIGGER_REV",	"Rev_RTrigger");
SetGamepadBindingStrings("KEY_PADFORWARD_REV",	"KEY_ABBR_PADFORWARD_REV",	"Rev_Plus");
SetGamepadBindingStrings("KEY_PADBACK_REV",		"KEY_ABBR_PADBACK_REV",		"Rev_Minus");
SetGamepadBindingStrings("KEY_PADSYSTEM_REV",	"KEY_ABBR_PADSYSTEM_REV",	"Rev_Home",		"Gen_Home");
SetGamepadBindingStrings("KEY_PADSOCIAL_REV",	"KEY_ABBR_PADSOCIAL_REV",	"Rev_Capture");
