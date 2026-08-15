/* import.h - characters imported from the two earlier Gold Box games.
 * Ported from Classes/PoolRadPlayer.cs and Classes/HillsFarPlayer.cs, with the
 * conversion from engine/ovr017.ConvertPoolRadPlayer.
 *
 * A new party can be built from saved characters of Pool of Radiance (*.cha and
 * *.sav, 0x11d bytes each) or Hillsfar (*.hil, 0xbc bytes). Both records are
 * read only: the game never writes either format back, it converts once into its
 * own Player record.
 *
 * Only the fields the original conversion actually touched are described here,
 * which is why both layouts have holes. The rest of each record is whatever the
 * other game kept there and is of no use to this one.
 */
#ifndef COAB_IMPORT_H
#define COAB_IMPORT_H

#include "coab.h"
#include "dataio.h"
#include "limits.h"

struct Player;

/* ------------------------------------------------------- Pool of Radiance */

#define POOL_RAD_RECORD_SIZE  0x11d
#define POOL_RAD_NAME_MAX     15

/* Pool of Radiance's spell book is 0x38 flags, against this game's 100. */
#define POOL_RAD_SPELL_BOOK_SIZE 0x38

typedef struct {
    char name[POOL_RAD_NAME_MAX + 1];       /* 0x00, Pascal string */

    /* 0x10..0x16. Pool of Radiance stored these in the same order this game's
     * PlayerStatId does - Str, Int, Wis, Dex, Con, Cha, Str00 - so they come
     * across as one block. */
    u8  stat[PSTAT_COUNT];

    i8  thac0;                              /* 0x2d */
    u8  race;                               /* 0x2e */
    u8  cls;                                /* 0x2f, "_class" */
    i16 age;                                /* 0x30 */
    u8  hp_max;                             /* 0x32 */

    u8  spell_book[POOL_RAD_SPELL_BOOK_SIZE];   /* 0x33, field_33 */

    u8  field_6B;                           /* 0x6b -> attack_level */
    u8  field_6C;                           /* 0x6c -> field_DE */
    u8  save_verse[SAVE_VERSE_COUNT];       /* 0x6d */
    u8  field_72;                           /* 0x72 -> base_movement */
    u8  field_73;                           /* 0x73 -> hit_dice */
    u8  field_74;                           /* 0x74 -> lost_lvls */
    u8  field_75;                           /* 0x75 -> lost_hp */
    u8  field_76;                           /* 0x76 -> field_E9 */
    u8  thief_skills[8];                    /* 0x77, field_77 */
    u8  field_83;                           /* 0x83 -> field_F6 */
    u8  field_84;                           /* 0x84 -> control_morale */
    u8  field_85;                           /* 0x85 -> npc_treasure_share_count */
    u8  field_86;                           /* 0x86 -> field_F9 */
    u8  field_87;                           /* 0x87 -> field_FA */
    u8  class_level[8];                     /* 0x96, field_96 */
    u8  sex;                                /* 0x9e */
    u8  field_9F;                           /* 0x9f -> monster_type */
    u8  field_A0;                           /* 0xa0 -> alignment */
    u8  field_A1;                           /* 0xa1 -> attacks_count */
    u8  field_A2;                           /* 0xa2 -> base_half_moves */
    u8  field_A3;                           /* 0xa3 -> attack1_dice_count_base */
    u8  field_A4;                           /* 0xa4 -> attack2_dice_count_base */
    u8  field_A5;                           /* 0xa5 -> attack1_dice_size_base */
    u8  field_A6;                           /* 0xa6 -> attack2_dice_size_base */
    u8  field_A7;                           /* 0xa7 -> attack1_damage_bonus_base */
    u8  field_A8;                           /* 0xa8 -> attack2_damage_bonus_base */
    u8  field_A9;                           /* 0xa9 -> base_ac */
    u8  field_AA;                           /* 0xaa -> field_125 */
    u8  field_AB;                           /* 0xab -> mod_id */
    i32 field_AC;                           /* 0xac -> exp */
    u8  field_B0;                           /* 0xb0 -> class_flags */
    u8  field_B1;                           /* 0xb1 -> hit_point_rolled */
    u8  field_B2[3];                        /* 0xb2 -> spell_cast_count[0][0..2] */
    u8  field_B5[3];                        /* 0xb5 -> spell_cast_count[2][0..2] */
    i16 field_B8;                           /* 0xb8 -> field_13C */
    u8  field_BA;                           /* 0xba -> field_13E */
    u8  field_BB;                           /* 0xbb -> field_13F */
    u8  field_BC;                           /* 0xbc -> field_140 */
    u8  field_BD;                           /* 0xbd -> head_icon */
    u8  field_BE;                           /* 0xbe -> weapon_icon */
    /* 0xbf is where icon_id would sit by analogy with the Player record, but
     * the conversion skipped it: the importing game hands out its own icon ids. */
    u8  field_C0;                           /* 0xc0 -> icon_size */
    u8  icon_colours[6];                    /* 0xc1, field_C1 */
    u8  field_C7;                           /* 0xc7, the item count */
    u8  field_100;                          /* 0x100 -> weapons_hands_used */
    i8  field_101;                          /* 0x101 -> field_186 */
    i16 field_102;                          /* 0x102 -> weight */
    u8  field_10C;                          /* 0x10c -> health_status */
    u8  field_10D;                          /* 0x10d -> in_combat */
    u8  field_10E;                          /* 0x10e -> combat_team */
    i8  field_110;                          /* 0x110 -> hit_bonus */
    u8  field_111;                          /* 0x111 -> ac */
    u8  field_112;                          /* 0x112 -> ac_behind */
    u8  field_113;                          /* 0x113 -> attack1_attacks_left */
    u8  field_114;                          /* 0x114 -> attack2_attacks_left */
    u8  field_115;                          /* 0x115 -> attack1_dice_count */
    u8  field_116;                          /* 0x116 -> attack2_dice_count */
    u8  field_117;                          /* 0x117 -> attack1_dice_size */
    u8  field_118;                          /* 0x118 -> attack2_dice_size */
    u8  field_119;                          /* 0x119 -> attack1_damage_bonus */
    u8  field_11A;                          /* 0x11a -> attack2_damage_bonus */
    u8  field_11B;                          /* 0x11b -> hit_point_current */
    i8  field_11C;                          /* 0x11c -> movement */
} PoolRadPlayer;

extern const DioDesc pool_rad_player_desc;

/* PoolRadPlayer(byte[]). Returns false, having logged, if the buffer is short. */
bool pool_rad_player_read(PoolRadPlayer *prp, const u8 *data, size_t data_size,
                          size_t offset);

/* ovr017.ConvertPoolRadPlayer: builds a fresh Player from the imported record.
 *
 * Two things the C# did are worth knowing about. Animate Dead is struck from the
 * spell book, because this game's spell ids differ from Pool of Radiance's from
 * that point on. And every imported character arrives with 300 platinum, which
 * is the original's way of not carrying money across.
 *
 * The items the Pool of Radiance character carried are not brought over: the
 * conversion had the copy commented out as unreachable dead code, since the
 * pointers it copied referred to the other game's heap. */
void import_convert_pool_rad_player(struct Player *p, const PoolRadPlayer *prp);

/* ------------------------------------------------------------- Hillsfar */

#define HILLS_FAR_RECORD_SIZE 0xbc
#define HILLS_FAR_NAME_MAX    15

/* Hillsfar's stats run Str, Str00, Int, Wis, Dex, Con, Cha - percentile strength
 * second, not last - so they are held in record order and mapped on use. */
typedef enum {
    HF_STAT_STR   = 0,
    HF_STAT_STR00 = 1,
    HF_STAT_INT   = 2,
    HF_STAT_WIS   = 3,
    HF_STAT_DEX   = 4,
    HF_STAT_CON   = 5,
    HF_STAT_CHA   = 6,
    HF_STAT_COUNT = 7
} HillsFarStat;

/* Which PlayerStatId each of the above is. */
extern const PlayerStatId hills_far_stat_to_pstat[HF_STAT_COUNT];

typedef struct {
    char name[HILLS_FAR_NAME_MAX + 1];      /* 0x04, Pascal string */
    u8   stat[HF_STAT_COUNT];               /* 0x14..0x1a, HillsFarStat order */
    u8   alignment;                         /* 0x1c */
    u8   field_1D;                          /* 0x1d */
    i16  age;                               /* 0x1e */
    u8   hp_current;                        /* 0x20, field_20 */
    u8   hp_max;                            /* 0x21, field_21 */
    u8   field_23;                          /* 0x23 */
    u8   field_26;                          /* 0x26 */
    i32  money;                             /* 0x28, field_28 */
    u8   sex;                               /* 0x2c, field_2C */
    u8   race;                              /* 0x2d, field_2D */
    i32  exp;                               /* 0x2e, field_2E */
    u8   field_35;                          /* 0x35 */
    u8   field_86;                          /* 0x86 */
    u8   field_87;                          /* 0x87 */
    u8   skill_cleric;                      /* 0xb7, field_B7 */
    u8   skill_magic_user;                  /* 0xb8, field_B8 */
    u8   skill_fighter;                     /* 0xb9, field_B9 */
    u8   skill_thief;                       /* 0xba, field_BA */
} HillsFarPlayer;

extern const DioDesc hills_far_player_desc;

/* HillsFarPlayer(byte[]).
 *
 * The C# port left this constructor empty - it declared the offsets in comments
 * on the fields but never read them - so a Hillsfar import there produced a
 * blank record. This reads the record those comments describe; the name being a
 * Pascal string at 0x04 is confirmed by ovr017.BuildLoadablePlayersLists, which
 * seeks to 4 and calls Sys.ArrayToString on 16 bytes. The offsets between are
 * from the comments and have not been checked against a real .hil file, so an
 * imported Hillsfar character is the one part of this port whose field mapping
 * is worth re-verifying against the game.
 */
bool hills_far_player_read(HillsFarPlayer *hfp, const u8 *data, size_t data_size,
                           size_t offset);

#endif /* COAB_IMPORT_H */
