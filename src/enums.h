/* enums.h - the game's enumerations.
 * Ported from Classes/Enums.cs, Classes/Affect.cs, Classes/ItemData.cs,
 * Classes/Spells.cs and the enum block at the top of Classes/Gbl.cs.
 *
 * Values are the ones the original data files and save games contain, so none of
 * these may be renumbered: they are read straight out of bytes on disk.
 *
 * C# enums are checked only where the code checks them; C enums are plain ints.
 * Anything that reaches these from a file goes through the enum_*_valid helpers
 * at the bottom, because malformed data must not become an out-of-range table
 * index.
 */
#ifndef COAB_ENUMS_H
#define COAB_ENUMS_H

#include "coab.h"

/* --------------------------------------------------------------- Enums.cs */

typedef enum {
    STATUS_OKEY        = 0x0,
    STATUS_ANIMATED    = 0x1,
    STATUS_TEMPGONE    = 0x2,
    STATUS_RUNNING     = 0x3,
    STATUS_UNCONSCIOUS = 0x4,
    STATUS_DYING       = 0x5,
    STATUS_DEAD        = 0x6,
    STATUS_STONED      = 0x7,
    STATUS_GONE        = 0x8
} Status;

typedef enum {
    MONSTER_TYPE_1        = 1,
    MONSTER_GIANT         = 2,
    MONSTER_DRAGON        = 3,
    MONSTER_ANIMATED_DEAD = 4,
    MONSTER_FIRE          = 8,
    MONSTER_TYPE_9        = 9,
    MONSTER_TROLL         = 10,
    MONSTER_TYPE_12       = 12,
    MONSTER_SNAKE         = 14,
    MONSTER_PLANT         = 18,
    MONSTER_ANIMAL        = 19
} MonsterType;

/* Order matters: this indexes the stat tables in limits.c and the on-disk
 * PlayerStats record. */
typedef enum {
    STAT_STR = 0,
    STAT_INT = 1,
    STAT_WIS = 2,
    STAT_DEX = 3,
    STAT_CON = 4,
    STAT_CHA = 5,
    STAT_COUNT = 6
} Stat;

typedef enum {
    RACE_MONSTER  = 0,
    RACE_DWARF    = 1,
    RACE_ELF      = 2,
    RACE_GNOME    = 3,
    RACE_HALF_ELF = 4,
    RACE_HALFLING = 5,
    RACE_HALF_ORC = 6,
    RACE_HUMAN    = 7,
    RACE_COUNT    = 8
} Race;

/* The eight single-class skills, in ClassLevel[] order. */
typedef enum {
    SKILL_CLERIC     = 0,
    SKILL_DRUID      = 1,
    SKILL_FIGHTER    = 2,
    SKILL_PALADIN    = 3,
    SKILL_RANGER     = 4,
    SKILL_MAGIC_USER = 5,
    SKILL_THIEF      = 6,
    SKILL_MONK       = 7,
    SKILL_COUNT      = 8
} SkillType;

/* 0..7 are the single classes and share numbering with SkillType; 8..16 are the
 * multi-class combinations. */
typedef enum {
    CLASS_CLERIC     = 0,
    CLASS_DRUID      = 1,
    CLASS_FIGHTER    = 2,
    CLASS_PALADIN    = 3,
    CLASS_RANGER     = 4,
    CLASS_MAGIC_USER = 5,
    CLASS_THIEF      = 6,
    CLASS_MONK       = 7,
    CLASS_MC_C_F     = 8,
    CLASS_MC_C_F_M   = 9,
    CLASS_MC_C_R     = 10,
    CLASS_MC_C_MU    = 11,
    CLASS_MC_C_T     = 12,
    CLASS_MC_F_MU    = 13,
    CLASS_MC_F_T     = 14,
    CLASS_MC_F_MU_T  = 15,
    CLASS_MC_MU_T    = 16,
    CLASS_UNKNOWN    = 17,
    CLASS_COUNT      = 17   /* table rows; CLASS_UNKNOWN is not one of them */
} ClassId;

typedef enum {
    TEAM_OURS  = 0,
    TEAM_ENEMY = 1
} CombatTeam;

/* Classes/Gbl.cs */
typedef enum {
    QUICK_FIGHT_FALSE = 0,
    QUICK_FIGHT_TRUE  = 1
} QuickFight;

/* Whether an affect is being applied or taken away. */
typedef enum {
    EFFECT_ADD    = 0,
    EFFECT_REMOVE = 1
} Effect;

/* Which game a character is being loaded from (Curse of the Azure Bonds itself,
 * Pool of Radiance, or Hillsfar). */
typedef enum {
    IMPORT_SOURCE_CURSE    = 0,
    IMPORT_SOURCE_POOL     = 1,
    IMPORT_SOURCE_HILLSFAR = 2
} ImportSource;

typedef enum {
    GAME_STATE_START_GAME_MENU = 0,
    GAME_STATE_SHOP            = 1,
    GAME_STATE_CAMPING         = 2,
    GAME_STATE_WILDERNESS_MAP  = 3,
    GAME_STATE_DUNGEON_MAP     = 4,
    GAME_STATE_COMBAT          = 5,
    GAME_STATE_AFTER_COMBAT    = 6,
    GAME_STATE_END_GAME        = 7
} GameState;

typedef enum {
    COMBAT_TYPE_NORMAL = 0,
    COMBAT_TYPE_DUEL   = 1
} CombatType;

/* Sound ids (Gbl.cs: enum Sound) already live in coab.h, next to the speaker
 * calls that take them. */

/* DamageType, a bit set: an attack can be several of these at once. */
#define DAMAGE_FIRE           0x01
#define DAMAGE_COLD           0x02
#define DAMAGE_ELECTRICITY    0x04
#define DAMAGE_MAGIC          0x08
#define DAMAGE_ACID           0x10
#define DAMAGE_DRAGON_BREATH  0x20
#define DAMAGE_UNKNOWN_40     0x40

/* How much damage a successful saving throw leaves. */
typedef enum {
    DAMAGE_ON_SAVE_NORMAL     = 0,
    DAMAGE_ON_SAVE_ZERO       = 1,
    DAMAGE_ON_SAVE_HALF       = 2,
    DAMAGE_ON_SAVE_UNKNOWN_3  = 3,
    DAMAGE_ON_SAVE_UNKNOWN_1E = 0x1e
} DamageOnSave;

/* --------------------------------------------------------------- Affect.cs */

/* Affect ids. 0x80 and up double as the item affect slots (item_affect_0 ==
 * affect_80), which is why the two naming schemes overlap. */
typedef enum {
    AFFECT_NONE                        = 0x00,
    AFFECT_BLESS                       = 0x01,
    AFFECT_CURSED                      = 0x02,
    AFFECT_STICKS_TO_SNAKES            = 0x03,
    AFFECT_DISPEL_EVIL                 = 0x04,
    AFFECT_DETECT_MAGIC                = 0x05,
    AFFECT_06                          = 0x06,
    AFFECT_FAERIE_FIRE                 = 0x07,
    AFFECT_PROTECTION_FROM_EVIL        = 0x08,
    AFFECT_PROTECTION_FROM_GOOD        = 0x09,
    AFFECT_RESIST_COLD                 = 0x0a,
    AFFECT_CHARM_PERSON                = 0x0b,
    AFFECT_ENLARGE                     = 0x0c,
    AFFECT_REDUCE                      = 0x0d,
    AFFECT_FRIENDS                     = 0x0e,
    AFFECT_POISON_DAMAGE               = 0x0f,
    AFFECT_READ_MAGIC                  = 0x10,
    AFFECT_SHIELD                      = 0x11,
    AFFECT_GNOME_VS_MAN_SIZED_GIANT    = 0x12,
    AFFECT_FIND_TRAPS                  = 0x13,
    AFFECT_RESIST_FIRE                 = 0x14,
    AFFECT_SILENCE_15_RADIUS           = 0x15,
    AFFECT_SLOW_POISON                 = 0x16,
    AFFECT_SPIRITUAL_HAMMER            = 0x17,
    AFFECT_DETECT_INVISIBILITY         = 0x18,
    AFFECT_INVISIBILITY                = 0x19,
    AFFECT_DWARF_VS_ORC                = 0x1a,
    AFFECT_FUMBLING                    = 0x1b,
    AFFECT_MIRROR_IMAGE                = 0x1c,
    AFFECT_RAY_OF_ENFEEBLEMENT         = 0x1d,
    AFFECT_STINKING_CLOUD              = 0x1e,
    AFFECT_HELPLESS                    = 0x1f,
    AFFECT_ANIMATE_DEAD                = 0x20,
    AFFECT_BLINDED                     = 0x21,
    AFFECT_CAUSE_DISEASE_1             = 0x22,
    AFFECT_CONFUSE                     = 0x23,
    AFFECT_BESTOW_CURSE                = 0x24,
    AFFECT_BLINK                       = 0x25,
    AFFECT_STRENGTH                    = 0x26,
    AFFECT_HASTE                       = 0x27,
    AFFECT_IN_STINKING_CLOUD           = 0x28,
    AFFECT_PROT_FROM_NORMAL_MISSILES   = 0x29,
    AFFECT_SLOW                        = 0x2a,
    AFFECT_WEAKEN                      = 0x2b,
    AFFECT_CAUSE_DISEASE_2             = 0x2c,
    AFFECT_PROT_FROM_EVIL_10_RADIUS    = 0x2d,
    AFFECT_PROT_FROM_GOOD_10_RADIUS    = 0x2e,
    AFFECT_DWARF_AND_GNOME_VS_GIANTS   = 0x2f,
    AFFECT_30                          = 0x30,
    AFFECT_PRAYER                      = 0x31,
    AFFECT_HOT_FIRE_SHIELD             = 0x32,
    AFFECT_SNAKE_CHARM                 = 0x33,
    AFFECT_PARALYZE                    = 0x34,
    AFFECT_SLEEP                       = 0x35,
    AFFECT_COLD_FIRE_SHIELD            = 0x36,
    AFFECT_POISONED                    = 0x37,
    AFFECT_ITEM_INVISIBILITY           = 0x38,
    AFFECT_39                          = 0x39,
    AFFECT_CLEAR_MOVEMENT              = 0x3a,
    AFFECT_REGENERATE                  = 0x3b,
    AFFECT_RESIST_NORMAL_WEAPONS       = 0x3c,
    AFFECT_FIRE_RESIST                 = 0x3d,
    AFFECT_HIGH_CON_REGEN              = 0x3e,
    AFFECT_MINOR_GLOBE_OF_INVULN       = 0x3f,
    AFFECT_POISON_PLUS_0               = 0x40,
    AFFECT_POISON_PLUS_4               = 0x41,
    AFFECT_POISON_PLUS_2               = 0x42,
    AFFECT_THRI_KREEN_PARALYZE         = 0x43,
    AFFECT_FEEBLEMIND                  = 0x44,
    AFFECT_INVISIBLE_TO_ANIMALS        = 0x45,
    AFFECT_POISON_NEG_2                = 0x46,
    AFFECT_INVISIBLE                   = 0x47,
    AFFECT_CAMOUFLAGE                  = 0x48,
    AFFECT_PROT_DRAG_BREATH            = 0x49,
    AFFECT_4A                          = 0x4a,
    AFFECT_WEAP_DRAGON_SLAYER          = 0x4b,
    AFFECT_WEAP_FROST_BRAND            = 0x4c,
    AFFECT_BERSERK                     = 0x4d,
    AFFECT_4E                          = 0x4e,
    AFFECT_FIRE_ATTACK_2D10            = 0x4f,
    AFFECT_ANKHEG_ACID_ATTACK          = 0x50,
    AFFECT_HALF_DAMAGE                 = 0x51,
    AFFECT_RESIST_FIRE_AND_COLD        = 0x52,
    AFFECT_PARALIZING_GAZE             = 0x53,
    AFFECT_SHAMBLING_ABSORB_LIGHTNING  = 0x54,
    AFFECT_55                          = 0x55,
    AFFECT_SPIT_ACID                   = 0x56,
    AFFECT_57                          = 0x57,
    AFFECT_BREATH_ELEC                 = 0x58,
    AFFECT_DISPLACE                    = 0x59,
    AFFECT_BREATH_ACID                 = 0x5a,
    AFFECT_IN_CLOUD_KILL               = 0x5b,
    AFFECT_5C                          = 0x5c,
    AFFECT_5D                          = 0x5d,
    AFFECT_5E                          = 0x5e,
    AFFECT_5F                          = 0x5f,
    AFFECT_OWLBEAR_HUG_CHECK           = 0x60,
    AFFECT_CON_SAVING_BONUS            = 0x61,
    AFFECT_REGEN_3_HP                  = 0x62,
    AFFECT_63                          = 0x63,
    AFFECT_TROLL_FIRE_OR_ACID          = 0x64,
    AFFECT_TROLL_REGEN                 = 0x65,
    AFFECT_TROLL_REGEN_2               = 0x66,
    AFFECT_SALAMANDER_HEAT_DAMAGE      = 0x67,
    AFFECT_THRI_KREEN_DODGE_MISSILE    = 0x68,
    AFFECT_RESIST_MAGIC_50_PERCENT     = 0x69,
    AFFECT_RESIST_MAGIC_15_PERCENT     = 0x6a,
    AFFECT_ELF_RESIST_SLEEP            = 0x6b,
    AFFECT_PROTECT_CHARM_SLEEP         = 0x6c,
    AFFECT_RESIST_PARALYZE             = 0x6d,
    AFFECT_IMMUNE_TO_COLD              = 0x6e,
    AFFECT_6F                          = 0x6f,
    AFFECT_IMMUNE_TO_FIRE              = 0x70,
    AFFECT_71                          = 0x71,
    AFFECT_72                          = 0x72,
    AFFECT_73                          = 0x73,
    AFFECT_74                          = 0x74,
    AFFECT_75                          = 0x75,
    AFFECT_76                          = 0x76,
    AFFECT_77                          = 0x77,
    AFFECT_78                          = 0x78,
    AFFECT_79                          = 0x79,
    AFFECT_DRACOLICH_PARALYSIS         = 0x7a,
    AFFECT_7B                          = 0x7b,
    AFFECT_HALFELF_RESISTANCE          = 0x7c,
    AFFECT_7D                          = 0x7d,
    AFFECT_7E                          = 0x7e,
    AFFECT_7F                          = 0x7f,
    AFFECT_80                          = 0x80,
    AFFECT_ITEM_AFFECT_0               = 0x80,   /* same value as AFFECT_80 */
    AFFECT_PROTECT_MAGIC               = 0x81,
    AFFECT_82                          = 0x82,
    AFFECT_CAST_BREATH_FIRE            = 0x83,
    AFFECT_CAST_THROW_LIGHTENING       = 0x84,
    AFFECT_85                          = 0x85,
    AFFECT_RANGER_VS_GIANT             = 0x86,
    AFFECT_ITEM_AFFECT_6               = 0x86,   /* same value as RANGER_VS_GIANT */
    AFFECT_PROTECT_ELEC                = 0x87,
    AFFECT_ENTANGLE                    = 0x88,
    AFFECT_89                          = 0x89,
    AFFECT_8A                          = 0x8a,
    AFFECT_8B                          = 0x8b,
    AFFECT_PALADIN_DAILY_HEAL_CAST     = 0x8c,
    AFFECT_PALADIN_DAILY_CURE_REFRESH  = 0x8d,
    AFFECT_FEAR                        = 0x8e,
    AFFECT_8F                          = 0x8f,
    AFFECT_OWLBEAR_HUG_ROUND_ATTACK    = 0x90,
    AFFECT_SP_DISPEL_EVIL              = 0x91,
    AFFECT_STRENGTH_SPELL              = 0x92,
    AFFECT_DO_ITEMS_AFFECT             = 0x93
} Affects;

/* -------------------------------------------------------------- ItemData.cs */

/* ItemDataFlags, a bit set in ItemData.field_E. */
#define ITEM_FLAG_ARROWS    0x01
#define ITEM_FLAG_02        0x02
#define ITEM_FLAG_MELEE     0x04
#define ITEM_FLAG_08        0x08
#define ITEM_FLAG_10        0x10
#define ITEM_FLAG_20        0x20
#define ITEM_FLAG_40        0x40
#define ITEM_FLAG_QUARRELS  0x80

/* Which of the 13 ready slots an item type occupies (ItemData.item_slot). */
typedef enum {
    ITEM_SLOT_0       = 0,
    ITEM_SLOT_1       = 1,
    ITEM_SLOT_ARMOR   = 2,
    ITEM_SLOT_3       = 3,
    ITEM_SLOT_4       = 4,
    ITEM_SLOT_5       = 5,
    ITEM_SLOT_6       = 6,
    ITEM_SLOT_7       = 7,
    ITEM_SLOT_8       = 8,
    ITEM_SLOT_9       = 9,
    ITEM_SLOT_10      = 10,
    ITEM_SLOT_11      = 11,
    ITEM_SLOT_QUARREL = 12,
    ITEM_SLOT_13      = 13,
    ITEM_SLOT_COUNT   = 13   /* ActiveItems holds 13 slots, 0..12 */
} ItemSlot;

typedef enum {
    ITEM_TYPE_0             = 0,
    ITEM_BATTLE_AXE         = 1,
    ITEM_HAND_AXE           = 2,
    ITEM_BARDICHE           = 3,
    ITEM_BEC_DE_CORBIN      = 4,
    ITEM_BILL_GUISARME      = 5,
    ITEM_BO_STICK           = 6,
    ITEM_CLUB               = 7,
    ITEM_DAGGER             = 8,
    ITEM_DART               = 9,
    ITEM_FAUCHARD           = 10,
    ITEM_FAUCHARD_FORK      = 11,
    ITEM_FLAIL              = 12,
    ITEM_MILITARY_FORK      = 13,
    ITEM_GLAIVE             = 14,
    ITEM_GLAIVE_GUISARME    = 15,
    ITEM_GUISARME           = 16,
    ITEM_GUISARME_VOULGE    = 17,
    ITEM_HALBERD            = 18,
    ITEM_LUCERN_HAMMER      = 19,
    ITEM_HAMMER             = 20,
    ITEM_JAVELIN            = 21,
    ITEM_JO_STICK           = 22,
    ITEM_MACE               = 23,
    ITEM_MORNING_STAR       = 24,
    ITEM_PARTISAN           = 25,
    ITEM_MILITARY_PICK      = 26,
    ITEM_AWL_PIKE           = 27,
    ITEM_QUARREL            = 28,
    ITEM_RANSEUR            = 29,
    ITEM_SCIMITAR           = 30,
    ITEM_SPEAR              = 31,
    ITEM_SPETUM             = 32,
    ITEM_QUARTER_STAFF      = 33,
    ITEM_BASTARD_SWORD      = 34,
    ITEM_BROAD_SWORD        = 35,
    ITEM_LONG_SWORD         = 36,
    ITEM_SHORT_SWORD        = 37,
    ITEM_TWO_HANDED_SWORD   = 38,
    ITEM_TRIDENT            = 39,
    ITEM_VOULGE             = 40,
    ITEM_COMPOSITE_LONG_BOW = 41,
    ITEM_COMPOSITE_SHORT_BOW = 42,
    ITEM_LONG_BOW           = 43,
    ITEM_SHORT_BOW          = 44,
    ITEM_HEAVY_CROSSBOW     = 45,
    ITEM_LIGHT_CROSSBOW     = 46,
    ITEM_SLING              = 47,
    ITEM_TYPE_48            = 48,
    ITEM_TYPE_49            = 49,
    ITEM_LEATHER_ARMOR      = 50,
    ITEM_PADDED_ARMOR       = 51,
    ITEM_STUDDED_LEATHER    = 52,
    ITEM_RING_MAIL          = 53,
    ITEM_SCALE_MAIL         = 54,
    ITEM_CHAIN_MAIL         = 55,
    ITEM_SPLINT_MAIL        = 56,
    ITEM_BANDED_MAIL        = 57,
    ITEM_PLATE_MAIL         = 58,
    ITEM_SHIELD             = 59,
    ITEM_SCROLL_OF_PROT     = 60,
    ITEM_MU_SCROLL          = 61,
    ITEM_CLRC_SCROLL        = 62,
    ITEM_GAUNTLETS          = 63,
    ITEM_TYPE_64            = 64,
    ITEM_GIRDLE             = 65,
    ITEM_TYPE_66            = 66,
    ITEM_TYPE_67            = 67,
    ITEM_TYPE_68            = 68,
    ITEM_RING_INVIS         = 69,
    ITEM_NECKLACE           = 70,
    ITEM_POTION             = 71,
    ITEM_TYPE_72            = 72,
    ITEM_ARROW              = 73,
    ITEM_TYPE_74            = 74,
    ITEM_TYPE_75            = 75,
    ITEM_TYPE_76            = 76,
    ITEM_BRACERS            = 77,
    ITEM_WAND_A             = 78,
    ITEM_WAND_B             = 79,
    ITEM_TYPE_80            = 80,
    ITEM_TYPE_81            = 81,
    ITEM_TYPE_82            = 82,
    ITEM_TYPE_83            = 83,
    ITEM_TYPE_84            = 84,
    ITEM_TYPE_85            = 85,
    ITEM_FLASK_OF_OIL       = 86,
    ITEM_TYPE_87            = 87,
    ITEM_TYPE_88            = 88,
    ITEM_TYPE_89            = 89,
    ITEM_TYPE_90            = 90,
    ITEM_TYPE_91            = 91,
    ITEM_CLOAK              = 92,
    ITEM_RING_OF_PROT       = 93,
    ITEM_DROW_MACE          = 94,
    ITEM_TYPE_95            = 95,
    ITEM_DROW_CHAIN_MAIL    = 96,
    ITEM_DROW_LONG_SWORD    = 97,
    ITEM_SPINE              = 98,
    ITEM_RING_OF_WIZARDRY   = 99,
    ITEM_DART_OF_HORNETS_NEST = 100,
    ITEM_STAFF_SLING        = 101,
    ITEM_TYPE_102           = 102,
    ITEM_TYPE_103           = 103,
    ITEM_TYPE_104           = 104,
    ITEM_TYPE_105           = 105,
    ITEM_TYPE_106           = 106,
    ITEM_TYPE_107           = 107,
    ITEM_TYPE_108           = 108,
    ITEM_TYPE_109           = 109,
    ITEM_TYPE_110           = 110,
    ITEM_TYPE_128           = 128
} ItemType;

/* ---------------------------------------------------------------- Spells.cs */

typedef enum {
    SAVE_VERSE_POISON        = 0,
    SAVE_VERSE_PETRIFICATION = 1,
    SAVE_VERSE_ROD_STAFF_WAND = 2,
    SAVE_VERSE_BREATH_WEAPON = 3,
    SAVE_VERSE_SPELL         = 4,
    SAVE_VERSE_COUNT         = 5
} SaveVerseType;

typedef enum {
    SPELL_WHEN_CAMP   = 0,
    SPELL_WHEN_COMBAT = 1,
    SPELL_WHEN_BOTH   = 2
} SpellWhen;

typedef enum {
    SPELL_TARGET_COMBAT       = 0,
    SPELL_TARGET_SELF         = 1,
    SPELL_TARGET_PARTY_MEMBER = 2,
    SPELL_TARGET_WHOLE_PARTY  = 4
} SpellTargets;

/* engine/ovr020.cs: SpellLoc. Where the spells on offer are being read from,
 * which is the heading the spell list is put up under and which list
 * ovr023.BuildSpellList builds. */
typedef enum {
    SPELL_LOC_MEMORY   = 0,
    SPELL_LOC_GRIMOIRE = 1,
    SPELL_LOC_SCROLL   = 2,
    SPELL_LOC_SCROLLS  = 3,
    SPELL_LOC_CHOOSE   = 4,
    SPELL_LOC_MEMORIZE = 5,
    SPELL_LOC_SCRIBE   = 6
} SpellLoc;

typedef enum {
    SPELL_SOURCE_CAST     = 1,
    SPELL_SOURCE_MEMORIZE = 2,
    SPELL_SOURCE_SCRIBE   = 3,
    SPELL_SOURCE_LEARN    = 4
} SpellSource;

typedef enum {
    SPELL_CLASS_CLERIC     = 0,
    SPELL_CLASS_DRUID      = 1,
    SPELL_CLASS_MAGIC_USER = 2,
    SPELL_CLASS_MONSTER    = 3,
    SPELL_CLASS_UNKNOWN10  = 10
} SpellClass;

/* Spell ids are 1-based: spellBook[] in Player is indexed by id - 1. */
typedef enum {
    SPELL_BLESS                       = 0x01,
    SPELL_CURSE                       = 0x02,
    SPELL_CURE_LIGHT_WOUNDS           = 0x03,
    SPELL_CAUSE_LIGHT_WOUNDS          = 0x04,
    SPELL_DETECT_MAGIC_CL             = 0x05,
    SPELL_PROTECT_FROM_EVIL_CL        = 0x06,
    SPELL_PROTECT_FROM_GOOD_CL        = 0x07,
    SPELL_RESIST_COLD                 = 0x08,
    SPELL_BURNING_HANDS               = 0x09,
    SPELL_CHARM_PERSON                = 0x0a,
    SPELL_DETECT_MAGIC_MU             = 0x0b,
    SPELL_ENLARGE                     = 0x0c,
    SPELL_REDUCE                      = 0x0d,
    SPELL_FRIENDS                     = 0x0e,
    SPELL_MAGIC_MISSILE               = 0x0f,
    SPELL_PROTECT_FROM_EVIL_MU        = 0x10,
    SPELL_PROTECT_FROM_GOOD_MU        = 0x11,
    SPELL_READ_MAGIC                  = 0x12,
    SPELL_SHIELD                      = 0x13,
    SPELL_SHOCKING_GRASP              = 0x14,
    SPELL_SLEEP                       = 0x15,
    SPELL_FIND_TRAPS                  = 0x16,
    SPELL_HOLD_PERSON_CL              = 0x17,
    SPELL_RESIST_FIRE                 = 0x18,
    SPELL_SILENCE_15_RADIUS           = 0x19,
    SPELL_SLOW_POISON                 = 0x1a,
    SPELL_SNAKE_CHARM                 = 0x1b,
    SPELL_SPIRITUAL_HAMMER            = 0x1c,
    SPELL_DETECT_INVISIBILITY         = 0x1d,
    SPELL_INVISIBILITY                = 0x1e,
    SPELL_KNOCK                       = 0x1f,
    SPELL_MIRROR_IMAGE                = 0x20,
    SPELL_RAY_OF_ENFEEBLEMENT         = 0x21,
    SPELL_STINKING_CLOUD              = 0x22,
    SPELL_STRENGTH                    = 0x23,
    SPELL_ANIMATE_DEAD                = 0x24,
    SPELL_CURE_BLINDNESS              = 0x25,
    SPELL_CAUSE_BLINDNESS             = 0x26,
    SPELL_CURE_DISEASE                = 0x27,
    SPELL_CAUSE_DISEASE               = 0x28,
    SPELL_DISPEL_MAGIC_CL             = 0x29,
    SPELL_PRAYER                      = 0x2a,
    SPELL_REMOVE_CURSE                = 0x2b,
    SPELL_BESTOW_CURSE_CL             = 0x2c,
    SPELL_BLINK                       = 0x2d,
    SPELL_DISPEL_MAGIC_MU             = 0x2e,
    SPELL_FIREBALL                    = 0x2f,
    SPELL_HASTE                       = 0x30,
    SPELL_HOLD_PERSON_MU              = 0x31,
    SPELL_INVISIBILITY_10_RADIUS      = 0x32,
    SPELL_LIGHTNING_BOLT              = 0x33,
    SPELL_PROTECT_FROM_EVIL_10_RAD    = 0x34,
    SPELL_PROTECT_FROM_GOOD_10_RAD    = 0x35,
    SPELL_PROTECT_FROM_NORMAL_MISSILES = 0x36,
    SPELL_SLOW                        = 0x37,
    SPELL_RESTORATION                 = 0x38,
    SPELL_39                          = 0x39,
    SPELL_CURE_SERIOUS_WOUNDS         = 0x3a,
    SPELL_3B                          = 0x3b,
    SPELL_3C                          = 0x3c,
    SPELL_3D                          = 0x3d,
    SPELL_3E                          = 0x3e,
    SPELL_3F                          = 0x3f,
    SPELL_40                          = 0x40,
    SPELL_41                          = 0x41,
    SPELL_CAUSE_SERIOUS_WOUNDS        = 0x42,
    SPELL_NEUTRALIZE_POISON           = 0x43,
    SPELL_POISON                      = 0x44,
    SPELL_PROTECT_EVIL_10_RAD         = 0x45,
    SPELL_STICKS_TO_SNAKES            = 0x46,
    SPELL_CURE_CRITICAL_WOUNDS        = 0x47,
    SPELL_CAUSE_CRITICAL_WOUNDS       = 0x48,
    SPELL_DISPEL_EVIL                 = 0x49,
    SPELL_FLAME_STRIKE                = 0x4a,
    SPELL_RAISE_DEAD                  = 0x4b,
    SPELL_SLAY_LIVING                 = 0x4c,
    SPELL_DETECT_MAGIC_DR             = 0x4d,
    SPELL_ENTANGLE                    = 0x4e,
    SPELL_FAERIE_FIRE                 = 0x4f,
    SPELL_INVISIBILITY_TO_ANIMALS     = 0x50,
    SPELL_CHARM_MONSTERS              = 0x51,
    SPELL_CONFUSION                   = 0x52,
    SPELL_DIMENSION_DOOR              = 0x53,
    SPELL_FEAR                        = 0x54,
    SPELL_FIRE_SHIELD                 = 0x55,
    SPELL_FUMBLE                      = 0x56,
    SPELL_ICE_STORM                   = 0x57,
    SPELL_MINOR_GLOBE_OF_INVULN       = 0x58,
    SPELL_59                          = 0x59,
    SPELL_5A                          = 0x5a,
    SPELL_CLOUD_KILL                  = 0x5b,
    SPELL_CONE_OF_COLD                = 0x5c,
    SPELL_FEEBLEMIND                  = 0x5d,
    SPELL_HOLD_MONSTERS               = 0x5e,
    SPELL_5F                          = 0x5f,
    SPELL_60                          = 0x60,
    SPELL_61                          = 0x61,
    SPELL_62                          = 0x62,
    SPELL_63                          = 0x63,
    SPELL_BESTOW_CURSE_MU             = 0x64
} Spells;

/* spellBook[] holds one flag per spell id, ids 1..100. */
#define SPELL_BOOK_SIZE 100

/* --------------------------------------------------------- range checking */

/* Data read off disk is not trusted: these keep a corrupt byte from becoming an
 * out-of-range index into the race/class tables. The C# port relied on the CLR
 * throwing IndexOutOfRangeException at the point of use instead. */
static inline bool race_valid(int r)  { return r >= 0 && r < RACE_COUNT; }
static inline bool class_valid(int c) { return c >= 0 && c < CLASS_COUNT; }
static inline bool skill_valid(int s) { return s >= 0 && s < SKILL_COUNT; }
static inline bool stat_valid(int s)  { return s >= 0 && s < STAT_COUNT; }
static inline bool sex_valid(int s)   { return s == 0 || s == 1; }

#endif /* COAB_ENUMS_H */
