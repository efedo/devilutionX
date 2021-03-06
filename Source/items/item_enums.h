/**
 * @file item_id.h
 *
 * Item id
 */

#ifndef __ITEM_ENUMS_H__
#define __ITEM_ENUMS_H__

namespace dvl {

// Item equip type
enum class ItemSlot {
	None = 0x0,
	OneHand = 0x1,
	TwoHand = 0x2,
	Armor = 0x3,
	Helm = 0x4,
	Ring = 0x5,
	Amulet = 0x6,
	Unequipable = 0x7,
	Belt = 0x8,
	Invalid = -1,
};

enum class ItemClass {
	none = 0,
	weapon = 1,
	armor = 2,
	misc = 3,
	gold = 4,
	quest = 5,
};

enum class ItemType {
	misc = 0x0,
	sword = 0x1,
	axe = 0x2,
	bow = 0x3,
	mace = 0x4,
	shield = 0x5,
	light_armor = 0x6,
	helm = 0x7,
	medium_armor = 0x8,
	heavy_armor = 0x9,
	staff = 0xA,
	gold = 0xB,
	ring = 0xC,
	amulet = 0xD,
	meat = 0xE, /* used in demo, might be generic for 'food' */
	none = -1,
};

enum class ItemQuality {
	normal = 0,
	magic = 1,
	unique = 2,
};

enum class UniqueItemType {
	none = 0x0,
	shortbow = 0x1,
	longbow = 0x2,
	huntbow = 0x3,
	compositebow = 0x4,
	warbow = 0x5,
	battlebow = 0x6,
	dagger = 0x7,
	falchion = 0x8,
	claymore = 0x9,
	broadsword = 0xA,
	sabre = 0xB,
	scimitar = 0xC,
	longsword = 0xD,
	bastardsword = 0xE,
	twohandedsword = 0xF,
	greatsword = 0x10,
	cleaver = 0x11,
	largeaxe = 0x12,
	broadaxe = 0x13,
	smallaxe = 0x14,
	battleaxe = 0x15,
	greataxe = 0x16,
	mace = 0x17,
	morningstar = 0x18,
	spikedclub = 0x19,
	maul = 0x1A,
	warhammer = 0x1B,
	flail = 0x1C,
	longstaff = 0x1D,
	shortstaff = 0x1E,
	compositestaff = 0x1F,
	quarterstaff = 0x20,
	warstaff = 0x21,
	skullcap = 0x22,
	helm = 0x23,
	greathelm = 0x24,
	crown = 0x25,
	type38 = 0x26,
	rags = 0x27,
	studdedarmor = 0x28,
	cloak = 0x29,
	robe = 0x2A,
	chainmail = 0x2B,
	leatherarmor = 0x2C,
	breastplate = 0x2D,
	cape = 0x2E,
	platemail = 0x2F,
	fullplate = 0x30,
	buckler = 0x31,
	smallshield = 0x32,
	largeshield = 0x33,
	kiteshield = 0x34,
	gothicshield = 0x35,
	ring = 0x36,
	type55 = 0x37,
	amulet = 0x38,
	skullcrown = 0x39,
	infraring = 0x3A,
	optamulet = 0x3B,
	tring = 0x3C,
	harcrest = 0x3D,
	mapofdoom = 0x3E,
	elixir = 0x3F,
	armorofvalor = 0x40,
	steelveil = 0x41,
	griswold = 0x42,
	lgtforge = 0x43,
	lazstaff = 0x44,
	invalid = -1,
};

enum class ItemEffectType {
	TOHIT = 0x0,
	TOHIT_CURSE = 0x1,
	DAMP = 0x2,
	DAMP_CURSE = 0x3,
	TOHIT_DAMP = 0x4,
	TOHIT_DAMP_CURSE = 0x5,
	ACP = 0x6,
	ACP_CURSE = 0x7,
	FIRERES = 0x8,
	LIGHTRES = 0x9,
	MAGICRES = 0xA,
	ALLRES = 0xB,
	SPLCOST = 0xC, /* only used in beta */
	SPLDUR = 0xD,  /* only used in beta */
	SPLLVLADD = 0xE,
	CHARGES = 0xF,
	FIREDAM = 0x10,
	LIGHTDAM = 0x11,
	STR = 0x13,
	STR_CURSE = 0x14,
	MAG = 0x15,
	MAG_CURSE = 0x16,
	DEX = 0x17,
	DEX_CURSE = 0x18,
	VIT = 0x19,
	VIT_CURSE = 0x1A,
	ATTRIBS = 0x1B,
	ATTRIBS_CURSE = 0x1C,
	GETHIT_CURSE = 0x1D,
	GETHIT = 0x1E,
	LIFE = 0x1F,
	LIFE_CURSE = 0x20,
	MANA = 0x21,
	MANA_CURSE = 0x22,
	DUR = 0x23,
	DUR_CURSE = 0x24,
	INDESTRUCTIBLE = 0x25,
	LIGHT = 0x26,
	LIGHT_CURSE = 0x27,
	MULT_ARROWS = 0x29, /* only used in hellfire */
	FIRE_ARROWS = 0x2A,
	LIGHT_ARROWS = 0x2B,
	INVCURS = 0x2C,
	THORNS = 0x2D,
	NOMANA = 0x2E,
	NOHEALPLR = 0x2F,
	FIREBALL = 0x32, /* only used in hellfire */
	ABSHALFTRAP = 0x34,
	KNOCKBACK = 0x35,
	NOHEALMON = 0x36,
	STEALMANA = 0x37,
	STEALLIFE = 0x38,
	TARGAC = 0x39,
	FASTATTACK = 0x3A,
	FASTRECOVER = 0x3B,
	FASTBLOCK = 0x3C,
	DAMMOD = 0x3D,
	RNDARROWVEL = 0x3E,
	SETDAM = 0x3F,
	SETDUR = 0x40,
	NOMINSTR = 0x41,
	SPELL = 0x42,
	FASTSWING = 0x43,
	ONEHAND = 0x44,
	T3XDAMVDEM = 0x45,
	ALLRESZERO = 0x46,
	DRAINLIFE = 0x48,
	RNDSTEALLIFE = 0x49,
	INFRAVISION = 0x4A,
	SETAC = 0x4B,
	ADDACLIFE = 0x4C,
	ADDMANAAC = 0x4D,
	FIRERESCLVL = 0x4E,
	AC_CURSE = 0x4F,
	INVALID = -1,
};

using ItemAffixFlags = int;
namespace ItemAffixFlag {
	constexpr int MISC = 0x1;
	constexpr int BOW = 0x10;
	constexpr int STAFF = 0x100;
	constexpr int WEAP = 0x1000;
	constexpr int SHLD = 0x10000;
	constexpr int ARMO = 0x100000;
};

/// Item graphic IDs; frame_num-11 of objcurs.cel.
enum class ItemCursor {
	POTION_OF_FULL_MANA = 0,
	SCROLL_OF = 1,
	GOLD_SMALL = 4,
	GOLD_MEDIUM = 5,
	GOLD_LARGE = 6,
	RING_OF_TRUTH = 10,
	RING = 12,
	SPECTRAL_ELIXIR = 15,
	GOLDEN_ELIXIR = 17,
	EMPYREAN_BAND = 18,
	EAR_SORCEROR = 19,
	EAR_WARRIOR = 20,
	EAR_ROGUE = 21,
	BLOOD_STONE = 25,
	ELIXIR_OF_VITALITY = 31,
	POTION_OF_HEALING = 32,
	POTION_OF_FULL_REJUVENATION = 33,
	ELIXIR_OF_MAGIC = 34,
	POTION_OF_FULL_HEALING = 35,
	ELIXIR_OF_DEXTERITY = 36,
	POTION_OF_REJUVENATION = 37,
	ELIXIR_OF_STRENGTH = 38,
	POTION_OF_MANA = 39,
	BRAIN = 40,
	OPTIC_AMULET = 44,
	AMULET = 45,
	DAGGER = 51,
	BLADE = 56,
	BASTARD_SWORD = 57,
	MACE = 59,
	LONGSWORD = 60,
	BROAD_SWORD = 61,
	FALCHION = 62,
	MORNING_STAR = 63,
	SHORTSWORD = 64,
	CLAYMORE = 65,
	CLUB = 66,
	SABRE = 67,
	SPIKED_CLUB = 70,
	SCIMITAR = 72,
	FULL_HELM = 75,
	MAGIC_ROCK = 76,
	THE_UNDEAD_CROWN = 78,
	HELM = 82,
	BUCKLER = 83,
	VIEL_OF_STEEL = 85,
	BOOK_GREY = 86,
	BOOK_RED = 87,
	BOOK_BLUE = 88,
	BLACK_MUSHROOM = 89,
	SKULL_CAP = 90,
	CAP = 91,
	HARLEQUIN_CREST = 93,
	CROWN = 95,
	MAP_OF_THE_STARS = 96,
	FUNGAL_TOME = 97,
	GREAT_HELM = 98,
	BATTLE_AXE = 101,
	HUNTERS_BOW = 102,
	FIELD_PLATE = 103,
	SMALL_SHIELD = 105,
	CLEAVER = 106,
	STUDDED_LEATHER_ARMOR = 107,
	SHORTSTAFF = 109,
	TWO_HANDED_SWORD = 110,
	CHAIN_MAIL = 111,
	SMALL_AXE = 112,
	KITE_SHIELD = 113,
	SCALE_MAIL = 114,
	SHORTBOW = 118,
	LONGBOW = 119,
	WAR_HAMMER = 121,
	MAUL = 122,
	LONGSTAFF = 123,
	WAR_STAFF = 124,
	TAVERN_SIGN = 126,
	HARD_LEATHER_ARMOR = 127,
	RAGS = 128,
	QUILTED_ARMOR = 129,
	FLAIL = 131,
	TOWER_SHIELD = 132,
	COMPOSITE_BOW = 133,
	GREAT_SWORD = 134,
	LEATHER_ARMOR = 135,
	SPLINT_MAIL = 136,
	ROBE = 137,
	ANVIL_OF_FURY = 140,
	BROAD_AXE = 141,
	LARGE_AXE = 142,
	GREAT_AXE = 143,
	AXE = 144,
	LARGE_SHIELD = 147,
	GOTHIC_SHIELD = 148,
	CLOAK = 149,
	CAPE = 150,
	FULL_PLATE_MAIL = 151,
	GOTHIC_PLATE = 152,
	BREAST_PLATE = 153,
	RING_MAIL = 154,
	STAFF_OF_LAZARUS = 155,
	ARKAINES_VALOR = 157,
	SHORT_WAR_BOW = 165,
	COMPOSITE_STAFF = 166,
	SHORT_BATTLE_BOW = 167,
	GOLD = 168,
};

enum class MiscItemId {
	NONE = 0x0,
	USEFIRST = 0x1,
	FULLHEAL = 0x2,
	HEAL = 0x3,
	OLDHEAL = 0x4,
	DEADHEAL = 0x5,
	MANA = 0x6,
	FULLMANA = 0x7,
	POTEXP = 0x8,  /* add experience */
	POTFORG = 0x9, /* remove experience */
	ELIXSTR = 0xA,
	ELIXMAG = 0xB,
	ELIXDEX = 0xC,
	ELIXVIT = 0xD,
	ELIXWEAK = 0xE, /* double check with alpha */
	ELIXDIS = 0xF,
	ELIXCLUM = 0x10,
	ELIXSICK = 0x11,
	REJUV = 0x12,
	FULLREJUV = 0x13,
	USELAST = 0x14,
	SCROLL = 0x15,
	SCROLLT = 0x16,
	STAFF = 0x17,
	BOOK = 0x18,
	RING = 0x19,
	AMULET = 0x1A,
	UNIQUE = 0x1B,
	MEAT = 0x1C, /* from demo/PSX */
	OILFIRST = 0x1D,
	OILOF = 0x1E, /* oils are beta or hellfire only */
	OILACC = 0x1F,
	OILMAST = 0x20,
	OILSHARP = 0x21,
	OILDEATH = 0x22,
	OILSKILL = 0x23,
	OILBSMTH = 0x24,
	OILFORT = 0x25,
	OILPERM = 0x26,
	OILHARD = 0x27,
	OILIMP = 0x28,
	OILLAST = 0x29,
	MAPOFDOOM = 0x2A,
	EAR = 0x2B,
	SPECELIX = 0x2C,
	INVALID = -1,
};

enum class ItemIndex {
	GOLD,
	WARRIOR,
	WARRSHLD,
	WARRCLUB,
	ROGUE,
	SORCEROR,
	CLEAVER,
	FIRSTQUEST = CLEAVER,
	SKCROWN,
	INFRARING,
	ROCK,
	OPTAMULET,
	TRING,
	BANNER,
	HARCREST,
	STEELVEIL,
	GLDNELIX,
	ANVIL,
	MUSHROOM,
	BRAIN,
	FUNGALTM,
	SPECELIX,
	BLDSTONE,
	MAPOFDOOM,
	LASTQUEST = MAPOFDOOM,
	EAR,
	HEAL,
	MANA,
	IDENTIFY,
	PORTAL,
	ARMOFVAL,
	FULLHEAL,
	FULLMANA,
	GRISWOLD,
	LGTFORGE,
	LAZSTAFF,
	RESURRECT,
};

// Only used three times???
enum class UniqueItemId {
	CLEAVER = 0x0,
	SKCROWN = 0x1,
	INFRARING = 0x2,
	OPTAMULET = 0x3,
	TRING = 0x4,
	HARCREST = 0x5,
	STEELVEIL = 0x6,
	ARMOFVAL = 0x7,
	GRISWOLD = 0x8,
	LGTFORGE = 0x9,
	RIFTBOW = 0xA,
	NEEDLER = 0xB,
	CELESTBOW = 0xC,
	DEADLYHUNT = 0xD,
	BOWOFDEAD = 0xE,
	BLKOAKBOW = 0xF,
	FLAMEDART = 0x10,
	FLESHSTING = 0x11,
	WINDFORCE = 0x12,
	EAGLEHORN = 0x13,
	GONNAGALDIRK = 0x14,
	DEFENDER = 0x15,
	GRYPHONCLAW = 0x16,
	BLACKRAZOR = 0x17,
	GIBBOUSMOON = 0x18,
	ICESHANK = 0x19,
	EXECUTIONER = 0x1A,
	BONESAW = 0x1B,
	SHADHAWK = 0x1C,
	WIZSPIKE = 0x1D,
	LGTSABRE = 0x1E,
	FALCONTALON = 0x1F,
	INFERNO = 0x20,
	DOOMBRINGER = 0x21,
	GRIZZLY = 0x22,
	GRANDFATHER = 0x23,
	MANGLER = 0x24,
	SHARPBEAK = 0x25,
	BLOODLSLAYER = 0x26,
	CELESTAXE = 0x27,
	WICKEDAXE = 0x28,
	STONECLEAV = 0x29,
	AGUHATCHET = 0x2A,
	HELLSLAYER = 0x2B,
	MESSERREAVER = 0x2C,
	CRACKRUST = 0x2D,
	JHOLMHAMM = 0x2E,
	CIVERBS = 0x2F,
	CELESTSTAR = 0x30,
	BARANSTAR = 0x31,
	GNARLROOT = 0x32,
	CRANBASH = 0x33,
	SCHAEFHAMM = 0x34,
	DREAMFLANGE = 0x35,
	STAFFOFSHAD = 0x36,
	IMMOLATOR = 0x37,
	STORMSPIRE = 0x38,
	GLEAMSONG = 0x39,
	THUNDERCALL = 0x3A,
	PROTECTOR = 0x3B,
	NAJPUZZLE = 0x3C,
	MINDCRY = 0x3D,
	RODOFONAN = 0x3E,
	SPIRITSHELM = 0x3F,
	THINKINGCAP = 0x40,
	OVERLORDHELM = 0x41,
	FOOLSCREST = 0x42,
	GOTTERDAM = 0x43,
	ROYCIRCLET = 0x44,
	TORNFLESH = 0x45,
	GLADBANE = 0x46,
	RAINCLOAK = 0x47,
	LEATHAUT = 0x48,
	WISDWRAP = 0x49,
	SPARKMAIL = 0x4A,
	SCAVCARAP = 0x4B,
	NIGHTSCAPE = 0x4C,
	NAJPLATE = 0x4D,
	DEMONSPIKE = 0x4E,
	DEFLECTOR = 0x4F,
	SKULLSHLD = 0x50,
	DRAGONBRCH = 0x51,
	BLKOAKSHLD = 0x52,
	HOLYDEF = 0x53,
	STORMSHLD = 0x54,
	BRAMBLE = 0x55,
	REGHA = 0x56,
	BLEEDER = 0x57,
	CONSTRICT = 0x58,
	ENGAGE = 0x59,
	INVALID = 0x5A,
};

enum class ItemDropRate {
	NEVER = 0,
	REGULAR = 1,
	DOUBLE = 2,
};

using ItemSpecialEffectFlags = uint32_t;
namespace ItemSpecialEffectFlag {
	constexpr uint32_t NONE = 0x00000000;
	constexpr uint32_t INFRAVISION = 0x00000001;
	constexpr uint32_t RNDSTEALLIFE = 0x00000002;
	constexpr uint32_t RNDARROWVEL = 0x00000004;
	constexpr uint32_t FIRE_ARROWS = 0x00000008;
	constexpr uint32_t FIREDAM = 0x00000010;
	constexpr uint32_t LIGHTDAM = 0x00000020;
	constexpr uint32_t DRAINLIFE = 0x00000040;
	constexpr uint32_t UNKNOWN_1 = 0x00000080;
	constexpr uint32_t NOHEALPLR = 0x00000100;
	constexpr uint32_t UNKNOWN_2 = 0x00000200;
	constexpr uint32_t UNKNOWN_3 = 0x00000400;
	constexpr uint32_t KNOCKBACK = 0x00000800;
	constexpr uint32_t NOHEALMON = 0x00001000;
	constexpr uint32_t STEALMANA_3 = 0x00002000;
	constexpr uint32_t STEALMANA_5 = 0x00004000;
	constexpr uint32_t STEALLIFE_3 = 0x00008000;
	constexpr uint32_t STEALLIFE_5 = 0x00010000;
	constexpr uint32_t QUICKATTACK = 0x00020000;
	constexpr uint32_t FASTATTACK = 0x00040000;
	constexpr uint32_t FASTERATTACK = 0x00080000;
	constexpr uint32_t FASTESTATTACK = 0x00100000;
	constexpr uint32_t FASTRECOVER = 0x00200000;
	constexpr uint32_t FASTERRECOVER = 0x00400000;
	constexpr uint32_t FASTESTRECOVER = 0x00800000;
	constexpr uint32_t FASTBLOCK = 0x01000000;
	constexpr uint32_t LIGHT_ARROWS = 0x02000000;
	constexpr uint32_t THORNS = 0x04000000;
	constexpr uint32_t NOMANA = 0x08000000;
	constexpr uint32_t ABSHALFTRAP = 0x10000000;
	constexpr uint32_t UNKNOWN_4 = 0x20000000;
	constexpr uint32_t T3XDAMVDEM = 0x40000000;
	constexpr uint32_t ALLRESZERO = 0x80000000;
};

enum class ItemColor {
	white = PAL16_YELLOW + 5,
	blue = PAL16_BLUE + 5,
	red = PAL16_RED + 5,
};


}

#endif /* __ITEM_ENUMS_H__ */
