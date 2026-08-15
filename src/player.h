/* player.h - a character: party member, NPC or monster.
 * Ported from Classes/Player.cs.
 *
 * The record is 0x1A6 bytes and is what save games and the shipped monster
 * tables contain, so the layout in player.c's descriptor table is fixed. Four
 * spans inside it were far pointers in the DOS original - the affect list at
 * 0xf2, the item list at 0x14d, the 13 ready-item slots at 0x151 and the action
 * at 0x18d - and are holes here, just as they were absent from the C# port's
 * [DataOffset] attributes. They are written as zero.
 *
 * The one intentional difference from the C# is noted at spell_cast_count in
 * player.c.
 */
#ifndef COAB_PLAYER_H
#define COAB_PLAYER_H

#include "coab.h"
#include "enums.h"
#include "limits.h"
#include "money.h"
#include "spelllist.h"
#include "affect.h"
#include "item.h"

#define PLAYER_RECORD_SIZE  0x1a6
#define PLAYER_NAME_MAX     15      /* capacity; the record holds 15 + 1 */
#define PLAYER_MAX_ITEMS    16      /* Player.MaxItems */

/* Player.Control: how a character is driven. The low 7 bits of control_morale
 * are the morale value; the top bit distinguishes NPC from PC. */
#define CONTROL_PC_BASE     0x00
#define CONTROL_PC_MASK     0x7f
#define CONTROL_NPC_BASE    0x80
#define CONTROL_NPC_BERZERK 0xb2
#define CONTROL_PC_BERZERK  0xb3

/* --------------------------------------------------------------- StatValue */

/* A stat as rolled and as currently drained. Drain changes cur; permanent
 * change moves both. */
typedef struct {
    int cur;
    int full;
} StatValue;

void stat_value_load(StatValue *sv, int val);          /* full = cur = val */
void stat_value_inc(StatValue *sv);
void stat_value_dec(StatValue *sv);

/* StatValue.EnforceRaceSexLimits / EnforceClassLimits / AgeEffects. The C#'s
 * StatValue carried the three table rows for whichever stat it was; here the
 * stat has to be named. Out-of-range race, sex, class or stat is logged and left
 * alone rather than indexing off the end of the tables. */
void stat_value_enforce_race_sex(StatValue *sv, PlayerStatId which, int race,
                                 int sex);
void stat_value_enforce_class(StatValue *sv, PlayerStatId which, ClassId cls);
void stat_value_age_effects(StatValue *sv, PlayerStatId which, int race,
                            int age);

/* The seven values in PlayerStatId order, which is also their record order:
 * Str, Int, Wis, Dex, Con, Cha, Str00 at two bytes each. */
typedef struct {
    StatValue value[PSTAT_COUNT];
} PlayerStats;

/* 14 bytes at 0x10 of the player record: seven { cur, full } byte pairs. */
#define PLAYER_STATS_RECORD_SIZE 14

void player_stats_clear(PlayerStats *ps);

/* PlayerStats.Assign: a plain copy, and the same thing the C#'s seven per-stat
 * Assign calls came to. */
void player_stats_assign(PlayerStats *dst, const PlayerStats *src);

/* PlayerStats.Inc / .Dec, which the C# offered only for the six named stats and
 * threw for anything else. Str00 and out-of-range are logged and ignored. */
void player_stats_inc(PlayerStats *ps, int stat_index);
void player_stats_dec(PlayerStats *ps, int stat_index);

/* EnforceRaceSexLimits / EnforceClassLimits / AgeEffects, applied to one stat.
 * Out-of-range race, sex or class is logged and left alone rather than indexing
 * off the end of the tables. */
void player_stats_enforce_race_sex(PlayerStats *ps, int race, int sex);
void player_stats_enforce_class(PlayerStats *ps, ClassId cls);
void player_stats_age_effects(PlayerStats *ps, int race, int age);

/* PlayerStats.Read / .Write, 14 bytes. Reading clamps to 25, as the C# did:
 * a stat above that would break the bonus tables. */
void player_stats_dio_read(void *member, const u8 *data, size_t offset);
void player_stats_dio_write(const void *member, u8 *data, size_t offset);

/* ------------------------------------------------------------------ Player */

typedef struct Player {
    char name[PLAYER_NAME_MAX + 1];         /* 0x00, Pascal string */
    PlayerStats stats;                      /* 0x10 */
    SpellList   spell_list;                 /* 0x1e */
    u8   spell_to_learn_count;              /* 0x72 */
    i8   thac0;                             /* 0x73 */
    int  race;                              /* 0x74, Race */
    int  cls;                               /* 0x75, ClassId ("_class") */
    i16  age;                               /* 0x76 */
    u8   hit_point_max;                     /* 0x78 */
    u8   spell_book[SPELL_BOOK_SIZE];       /* 0x79, one flag per spell id - 1 */
    u8   attack_level;                      /* 0xdd */
    u8   field_DE;                          /* 0xde */
    u8   save_verse[SAVE_VERSE_COUNT];      /* 0xdf */
    u8   base_movement;                     /* 0xe4 */
    u8   hit_dice;                          /* 0xe5 */
    u8   multiclass_level;                  /* 0xe6 */
    u8   lost_lvls;                         /* 0xe7 */
    u8   lost_hp;                           /* 0xe8 */
    u8   field_E9;                          /* 0xe9 */
    u8   thief_skills[8];                   /* 0xea: pick pockets, open locks,
                                             * find/remove traps, move silently,
                                             * hide in shadows, hear noise,
                                             * climb walls, read languages */
    /* 0xf2 was the affect list pointer. */
    AffectList affects;
    u8   field_F6;                          /* 0xf6 */
    u8   control_morale;                    /* 0xf7, see CONTROL_* */
    u8   npc_treasure_share_count;          /* 0xf8 */
    u8   field_F9;                          /* 0xf9 */
    u8   field_FA;                          /* 0xfa */
    MoneySet money;                         /* 0xfb */
    u8   class_level[SKILL_COUNT];          /* 0x109, indexed by SkillType */
    u8   class_level_old[SKILL_COUNT];      /* 0x111, pre-dual-class levels */
    u8   sex;                               /* 0x119 */
    int  monster_type;                      /* 0x11a, MonsterType */
    u8   alignment;                         /* 0x11b */
    u8   attacks_count;                     /* 0x11c, counted in half-attacks */
    u8   base_half_moves;                   /* 0x11d */
    u8   attack1_dice_count_base;           /* 0x11e */
    u8   attack2_dice_count_base;           /* 0x11f */
    u8   attack1_dice_size_base;            /* 0x120 */
    u8   attack2_dice_size_base;            /* 0x121 */
    u8   attack1_damage_bonus_base;         /* 0x122 */
    u8   attack2_damage_bonus_base;         /* 0x123 */
    u8   base_ac;                           /* 0x124 */
    u8   field_125;                         /* 0x125 */
    u8   mod_id;                            /* 0x126 */
    i32  exp;                               /* 0x127 */
    u8   class_flags;                       /* 0x12b */
    u8   hit_point_rolled;                  /* 0x12c */
    u8   spell_cast_count[3][5];            /* 0x12d, 15 bytes */
    i16  field_13C;                         /* 0x13c */
    u8   field_13E;                         /* 0x13e */
    u8   field_13F;                         /* 0x13f */
    u8   field_140;                         /* 0x140 */
    u8   head_icon;                         /* 0x141 */
    u8   weapon_icon;                       /* 0x142 */
    u8   icon_id;                           /* 0x143 */
    u8   icon_size;                         /* 0x144, 1 small 2 normal */
    u8   icon_colours[6];                   /* 0x145 */
    u8   field_14B;                         /* 0x14b */

    /* 0x14c held the item count and 0x14d the item list pointer. */
    Item items[PLAYER_MAX_ITEMS];
    int  item_count;

    /* 0x151 held 13 far pointers to readied items. Indices into items[] are
     * used instead, so compacting the item list cannot leave a slot pointing at
     * freed memory. ITEM_SLOT_NONE means the slot is empty. */
    i8   ready[ITEM_SLOT_COUNT];

    u8   weapons_hands_used;                /* 0x185 */
    i8   field_186;                         /* 0x186 */
    i16  weight;                            /* 0x187 */
    /* 0x189..0x18c unaccounted for in the original. 0x18d was the action
     * pointer, and it is a pointer here too: whether a character has one is
     * meaningful - a character not in a fight has none, and the engine tests
     * for that - so the record cannot simply embed it. The combat code owns
     * what it points at, and nothing in player.c touches it. */
    struct Action *actions;                 /* 0x18d */
    u8   paladin_cures_left;                /* 0x191 */
    u8   field_192;                         /* 0x192 */
    u8   field_193;                         /* 0x193 */
    u8   field_194;                         /* 0x194 */
    int  health_status;                     /* 0x195, Status */
    bool in_combat;                         /* 0x196 */
    int  combat_team;                       /* 0x197, CombatTeam */
    int  quick_fight;                       /* 0x198, QuickFight */
    int  hit_bonus;                         /* 0x199 */
    u8   ac;                                /* 0x19a */
    u8   ac_behind;                         /* 0x19b */
    u8   attack1_attacks_left;              /* 0x19c */
    u8   attack2_attacks_left;              /* 0x19d */
    u8   attack1_dice_count;                /* 0x19e */
    u8   attack2_dice_count;                /* 0x19f */
    u8   attack1_dice_size;                 /* 0x1a0 */
    u8   attack2_dice_size;                 /* 0x1a1 */
    i8   attack1_damage_bonus;              /* 0x1a2 */
    u8   attack2_damage_bonus;              /* 0x1a3 */
    u8   hit_point_current;                 /* 0x1a4 */
    u8   movement;                          /* 0x1a5, initiative */
} Player;

#define ITEM_SLOT_NONE (-1)

extern const DioDesc player_desc;

/* Player(): a zeroed character with empty lists and no readied items. */
void player_init(Player *p);

/* Player(byte[], offset) and ToByteArray(). The write zeroes the buffer first,
 * so the pointer holes come out as zero. */
bool player_read(Player *p, const u8 *data, size_t data_size, size_t offset);
bool player_write(const Player *p, u8 *data, size_t data_size);

/* --- spell book --- */
bool player_knows_spell(const Player *p, Spells spell);
void player_learn_spell(Player *p, Spells spell);

/* --- levels --- */

/* SkillLevel: the class's level, plus its pre-dual-class level once the new
 * class has passed the old one. */
int  player_skill_level(const Player *p, SkillType skill);

/* engine/ovr026.cs: HumanCurrentClassLevel_Zero and DualClassExceedLastLevel.
 * They belong to the class recalculation in classcalc.c, but SkillLevel above
 * needs both, so they live here. The first is the level the character has in
 * whichever single class it currently holds - zero for anything but a human -
 * and the second is 1 once that has passed the level recorded in
 * multiclass_level, which is when the abandoned class's levels start counting
 * again. */
int  player_dual_class_current_level(const Player *p);
int  player_dual_class_exceeded(const Player *p);

/* CanDuelClass: human, and not already dual-classed. */
bool player_can_duel_class(const Player *p);

/* --- combat helpers ---
 *
 * The C# exposed attack 1 and 2 through index-taking accessors that threw on
 * anything else. Here index must be 1 or 2; anything else is logged and reads
 * as zero. */
u8   player_attacks_left(const Player *p, int index);
void player_attacks_left_set(Player *p, int index, u8 value);
void player_attacks_left_dec(Player *p, int index);
u8   player_attack_dice_count(const Player *p, int index);
u8   player_attack_dice_size(const Player *p, int index);
int  player_attack_damage_bonus(const Player *p, int index);

/* The displayed armour class counts down from 0x3C, not up. */
int  player_display_ac(const Player *p);

CombatTeam player_opposite_team(const Player *p);

bool player_has_affect(const Player *p, Affects type);
bool player_is_held(const Player *p);

void player_add_weight(Player *p, int amount);
void player_remove_weight(Player *p, int amount);

/* --- items ---
 *
 * items[] is the character's pack. Adding returns the new index, or -1 when the
 * pack is full. Removing compacts the array and fixes up the ready slots, which
 * is why callers must use indices and not hold Item pointers across a removal.
 */
int   player_item_add(Player *p, const Item *it);
bool  player_item_remove(Player *p, int index);
Item *player_item_at(Player *p, int index);

/* ActiveItems: the 13 readied slots. */
Item *player_ready_item(Player *p, ItemSlot slot);
void  player_ready_set(Player *p, ItemSlot slot, int item_index);
void  player_ready_reset(Player *p);

Item *player_primary_weapon(Player *p);
Item *player_secondary_weapon(Player *p);
Item *player_armor(Player *p);

/* ActiveItems.arrows and .quarrels, slots 11 and 12: the readied ammunition a
 * bow or a crossbow spends. */
Item *player_arrows(Player *p);
Item *player_quarrels(Player *p);

u8 player_primary_weapon_hand_count(Player *p);
u8 player_secondary_weapon_hand_count(Player *p);

/* ActiveItems.UndreadyAll: unreadies anything this class may not use, leaving
 * cursed items readied because they cannot be removed. */
void player_undready_all(Player *p, int class_flags);

/* engine/ovr020.cs: statusString. How the character sheet spells the nine health
 * states, "tempgone" in lower case as the original had it. The table belongs to
 * ovr020, but ovr025 reads it too and is ported first, so it lives with the
 * field it describes. Returns "" for a state that does not exist. */
const char *player_health_status_name(int status);

/* engine/ovr020.cs: classString, here for the same reason - classcalc.c needs it
 * to name the class a character has just dual-classed into. Returns "" for a
 * class outside the table, CLASS_UNKNOWN included. */
const char *player_class_name(int cls);

#endif /* COAB_PLAYER_H */
