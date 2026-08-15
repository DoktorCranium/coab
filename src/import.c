/* import.c - Ported from Classes/PoolRadPlayer.cs, Classes/HillsFarPlayer.cs and
 * engine/ovr017.ConvertPoolRadPlayer. */
#include <string.h>

#include "import.h"

#include "player.h"
#include "log.h"

/* ------------------------------------------------------- Pool of Radiance */

static const DioField pool_rad_player_fields[] = {
    DIO_F(PoolRadPlayer, name,         0x000, DIO_PSTRING,    POOL_RAD_NAME_MAX),
    DIO_F(PoolRadPlayer, stat,         0x010, DIO_BYTE_ARRAY, PSTAT_COUNT),
    DIO_F(PoolRadPlayer, thac0,        0x02d, DIO_SBYTE,      0),
    DIO_F(PoolRadPlayer, race,         0x02e, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, cls,          0x02f, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, age,          0x030, DIO_SWORD,      0),
    DIO_F(PoolRadPlayer, hp_max,       0x032, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, spell_book,   0x033, DIO_BYTE_ARRAY,
          POOL_RAD_SPELL_BOOK_SIZE),
    DIO_F(PoolRadPlayer, field_6B,     0x06b, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_6C,     0x06c, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, save_verse,   0x06d, DIO_BYTE_ARRAY, SAVE_VERSE_COUNT),
    DIO_F(PoolRadPlayer, field_72,     0x072, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_73,     0x073, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_74,     0x074, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_75,     0x075, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_76,     0x076, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, thief_skills, 0x077, DIO_BYTE_ARRAY, 8),
    DIO_F(PoolRadPlayer, field_83,     0x083, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_84,     0x084, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_85,     0x085, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_86,     0x086, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_87,     0x087, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, class_level,  0x096, DIO_BYTE_ARRAY, 8),
    DIO_F(PoolRadPlayer, sex,          0x09e, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_9F,     0x09f, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A0,     0x0a0, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A1,     0x0a1, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A2,     0x0a2, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A3,     0x0a3, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A4,     0x0a4, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A5,     0x0a5, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A6,     0x0a6, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A7,     0x0a7, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A8,     0x0a8, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_A9,     0x0a9, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_AA,     0x0aa, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_AB,     0x0ab, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_AC,     0x0ac, DIO_INT,        0),
    DIO_F(PoolRadPlayer, field_B0,     0x0b0, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_B1,     0x0b1, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_B2,     0x0b2, DIO_BYTE_ARRAY, 3),
    DIO_F(PoolRadPlayer, field_B5,     0x0b5, DIO_BYTE_ARRAY, 3),
    DIO_F(PoolRadPlayer, field_B8,     0x0b8, DIO_SWORD,      0),
    DIO_F(PoolRadPlayer, field_BA,     0x0ba, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_BB,     0x0bb, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_BC,     0x0bc, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_BD,     0x0bd, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_BE,     0x0be, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_C0,     0x0c0, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, icon_colours, 0x0c1, DIO_BYTE_ARRAY, 6),
    DIO_F(PoolRadPlayer, field_C7,     0x0c7, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_100,    0x100, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_101,    0x101, DIO_SBYTE,      0),
    DIO_F(PoolRadPlayer, field_102,    0x102, DIO_SWORD,      0),
    DIO_F(PoolRadPlayer, field_10C,    0x10c, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_10D,    0x10d, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_10E,    0x10e, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_110,    0x110, DIO_SBYTE,      0),
    DIO_F(PoolRadPlayer, field_111,    0x111, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_112,    0x112, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_113,    0x113, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_114,    0x114, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_115,    0x115, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_116,    0x116, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_117,    0x117, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_118,    0x118, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_119,    0x119, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_11A,    0x11a, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_11B,    0x11b, DIO_BYTE,       0),
    DIO_F(PoolRadPlayer, field_11C,    0x11c, DIO_SBYTE,      0),
    DIO_END_MARKER
};

const DioDesc pool_rad_player_desc = {
    "PoolRadPlayer", POOL_RAD_RECORD_SIZE, pool_rad_player_fields
};

bool pool_rad_player_read(PoolRadPlayer *prp, const u8 *data, size_t data_size,
                          size_t offset)
{
    memset(prp, 0, sizeof(*prp));

    return dio_read(&pool_rad_player_desc, prp, data, data_size, offset);
}

void import_convert_pool_rad_player(struct Player *p, const PoolRadPlayer *prp)
{
    player_init(p);

    p->race = prp->race;
    p->sex  = prp->sex;

    /* Both name buffers hold a 15 character name and a terminator. */
    memcpy(p->name, prp->name, sizeof(p->name));

    for (int i = 0; i < PSTAT_COUNT; i++) {
        stat_value_load(&p->stats.value[i], prp->stat[i]);
    }
    /* The original enforced the race and sex limits one stat at a time, as each
     * was loaded. Doing all seven at the end is the same thing, since no stat's
     * limits depend on another's. */
    player_stats_enforce_race_sex(&p->stats, p->race, p->sex);

    p->thac0         = prp->thac0;
    p->cls           = prp->cls;
    p->age           = prp->age;
    p->hit_point_max = prp->hp_max;

    memcpy(p->spell_book, prp->spell_book, POOL_RAD_SPELL_BOOK_SIZE);
    /* The spell ids part company at Animate Dead, so what the flags past it mean
     * differs between the two games. The original dropped just this one. */
    p->spell_book[SPELL_ANIMATE_DEAD - 1] = 0;

    p->attack_level = prp->field_6B;
    p->field_DE     = prp->field_6C;

    memcpy(p->save_verse, prp->save_verse, SAVE_VERSE_COUNT);

    p->base_movement    = prp->field_72;
    p->hit_dice         = prp->field_73;
    p->multiclass_level = p->hit_dice;
    p->lost_lvls        = prp->field_74;
    p->lost_hp          = prp->field_75;
    p->field_E9         = prp->field_76;

    memcpy(p->thief_skills, prp->thief_skills, sizeof(p->thief_skills));

    p->field_F6                 = prp->field_83;
    p->control_morale           = prp->field_84;
    p->npc_treasure_share_count = prp->field_85;
    p->field_F9                 = prp->field_86;
    p->field_FA                 = prp->field_87;

    /* Money does not come across; every import starts with 300 platinum. */
    money_set(&p->money, MONEY_PLATINUM, 300);

    memcpy(p->class_level, prp->class_level, sizeof(p->class_level));

    p->monster_type = prp->field_9F;
    p->alignment    = prp->field_A0;

    p->attacks_count             = prp->field_A1;
    p->base_half_moves           = prp->field_A2;
    p->attack1_dice_count_base   = prp->field_A3;
    p->attack2_dice_count_base   = prp->field_A4;
    p->attack1_dice_size_base    = prp->field_A5;
    p->attack2_dice_size_base    = prp->field_A6;
    p->attack1_damage_bonus_base = prp->field_A7;
    p->attack2_damage_bonus_base = prp->field_A8;

    p->base_ac   = prp->field_A9;
    p->field_125 = prp->field_AA;
    p->mod_id    = prp->field_AB;

    p->exp             = prp->field_AC;
    p->class_flags     = prp->field_B0;
    p->hit_point_rolled = prp->field_B1;

    /* Only the first three spell levels of the cleric and magic user rows; Pool
     * of Radiance's characters cannot have reached higher. */
    for (int i = 0; i < 3; i++) {
        p->spell_cast_count[0][i] = prp->field_B2[i];
        p->spell_cast_count[2][i] = prp->field_B5[i];
    }

    p->field_13C = prp->field_B8;
    p->field_13E = prp->field_BA;
    p->field_13F = prp->field_BB;
    p->field_140 = prp->field_BC;

    p->head_icon   = prp->field_BD;
    p->weapon_icon = prp->field_BE;
    p->icon_size   = prp->field_C0;

    memcpy(p->icon_colours, prp->icon_colours, sizeof(p->icon_colours));

    /* prp->field_C7 is the item count and the copy of the item pointers that
     * followed it was already dead code in the original, so an imported
     * character arrives with nothing. */

    p->weapons_hands_used = prp->field_100;
    p->field_186          = prp->field_101;
    p->weight             = prp->field_102;

    p->health_status = prp->field_10C;
    p->in_combat     = prp->field_10D != 0;
    p->combat_team   = prp->field_10E;
    p->hit_bonus     = prp->field_110;

    p->ac        = prp->field_111;
    p->ac_behind = prp->field_112;

    p->attack1_attacks_left = prp->field_113;
    p->attack2_attacks_left = prp->field_114;

    p->attack1_dice_count = prp->field_115;
    p->attack2_dice_count = prp->field_116;

    p->attack1_dice_size = prp->field_117;
    p->attack2_dice_size = prp->field_118;

    p->attack1_damage_bonus = (i8)prp->field_119;
    p->attack2_damage_bonus = prp->field_11A;
    p->hit_point_current    = prp->field_11B;
    p->movement             = (u8)prp->field_11C;
}

/* ------------------------------------------------------------- Hillsfar */

const PlayerStatId hills_far_stat_to_pstat[HF_STAT_COUNT] = {
    PSTAT_STR, PSTAT_STR00, PSTAT_INT, PSTAT_WIS, PSTAT_DEX, PSTAT_CON, PSTAT_CHA
};

static const DioField hills_far_player_fields[] = {
    DIO_F(HillsFarPlayer, name,            0x04, DIO_PSTRING,    HILLS_FAR_NAME_MAX),
    DIO_F(HillsFarPlayer, stat,            0x14, DIO_BYTE_ARRAY, HF_STAT_COUNT),
    DIO_F(HillsFarPlayer, alignment,       0x1c, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, field_1D,        0x1d, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, age,             0x1e, DIO_SWORD,      0),
    DIO_F(HillsFarPlayer, hp_current,      0x20, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, hp_max,          0x21, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, field_23,        0x23, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, field_26,        0x26, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, money,           0x28, DIO_INT,        0),
    DIO_F(HillsFarPlayer, sex,             0x2c, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, race,            0x2d, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, exp,             0x2e, DIO_INT,        0),
    DIO_F(HillsFarPlayer, field_35,        0x35, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, field_86,        0x86, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, field_87,        0x87, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, skill_cleric,    0xb7, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, skill_magic_user, 0xb8, DIO_BYTE,      0),
    DIO_F(HillsFarPlayer, skill_fighter,   0xb9, DIO_BYTE,       0),
    DIO_F(HillsFarPlayer, skill_thief,     0xba, DIO_BYTE,       0),
    DIO_END_MARKER
};

const DioDesc hills_far_player_desc = {
    "HillsFarPlayer", HILLS_FAR_RECORD_SIZE, hills_far_player_fields
};

bool hills_far_player_read(HillsFarPlayer *hfp, const u8 *data, size_t data_size,
                           size_t offset)
{
    memset(hfp, 0, sizeof(*hfp));

    return dio_read(&hills_far_player_desc, hfp, data, data_size, offset);
}
