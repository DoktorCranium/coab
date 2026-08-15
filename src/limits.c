/* limits.c - Ported from Classes/Limits.cs.
 *
 * Table rows are in Race order and columns in the order the C# used, so these
 * can be diffed against Limits.cs line for line. The made-up monster row and the
 * two oddities the original contains (noted below) are kept as they are: the
 * point of the port is to behave like the game, not like the rulebook.
 */
#include "limits.h"

#include "cheats.h"
#include "log.h"
#include "player.h"

/* unk_1A434. Ages at which each of the five ageing steps lands. */
const int limits_race_age_brackets[RACE_COUNT][AGE_BRACKETS] = {
    { 9999, 9999, 9999, 9999, 9999 },   /* monster, made-up */
    {   50,  150,  250,  350,  450 },   /* dwarf */
    {  175,  550,  875, 1200, 1600 },   /* elf */
    {   90,  300,  450,  600,  750 },   /* gnome */
    {   40,  100,  175,  250,  325 },   /* half elf */
    { 0x21, 0x44, 0x65, 0x90, 0xC7 },   /* halfling */
    { 0x0F, 0x1E, 0x2D, 0x3C, 0x50 },   /* half orc */
    { 0x14, 0x28, 0x3C, 0x5A, 0x78 }    /* human */
};

const i8 limits_age_effect[PSTAT_COUNT][AGE_BRACKETS] = {
    {  0,  1, -1, -2, -1 },   /* Str */
    {  0,  0,  1,  0,  1 },   /* Int */
    { -1,  1,  1,  1,  1 },   /* Wis */
    {  0,  0,  0, -2, -1 },   /* Dex */
    {  1,  0, -1, -1, -1 },   /* Con */
    {  0,  0,  0,  0,  0 },   /* Cha */
    {  0,  0,  0,  0,  0 }    /* Str00 */
};

/* [stat][race][min|max][sex]. The monster row's Con is min 20 / max 10; since
 * the max is applied first and the min second, monsters end up with Con 20. */
const u8 limits_race_sex_min_max[PSTAT_COUNT][RACE_COUNT][2][2] = {
    {   /* Str */
        { { 0,  5}, { 10,  0} },   /* monster */
        { { 8,  8}, { 18, 17} },   /* dwarf */
        { { 3,  3}, { 18, 16} },   /* elf */
        { { 6,  6}, { 18, 15} },   /* gnome */
        { { 3,  3}, { 18, 17} },   /* half elf */
        { { 6,  6}, { 17, 14} },   /* halfling */
        { { 6,  6}, { 18, 18} },   /* half orc */
        { { 3,  3}, { 18, 18} }    /* human */
    },
    {   /* Int */
        { {10, 10}, { 15, 15} },
        { { 3,  3}, { 18, 18} },
        { { 8,  8}, { 18, 18} },
        { { 7,  7}, { 18, 18} },
        { { 4,  4}, { 18, 18} },
        { { 6,  6}, { 18, 18} },
        { { 3,  3}, { 17, 17} },
        { { 3,  3}, { 18, 18} }
    },
    {   /* Wis */
        { { 5,  5}, { 10, 10} },
        { { 3,  3}, { 18, 18} },
        { { 3,  3}, { 18, 18} },
        { { 3,  3}, { 18, 18} },
        { { 3,  3}, { 18, 18} },
        { { 3,  3}, { 17, 17} },
        { { 3,  3}, { 14, 14} },
        { { 3,  3}, { 18, 18} }
    },
    {   /* Dex */
        { {10, 10}, { 15, 15} },
        { { 3,  3}, { 17, 17} },
        { { 7,  7}, { 19, 19} },
        { { 3,  3}, { 18, 18} },
        { { 6,  6}, { 18, 18} },
        { { 8,  8}, { 18, 18} },
        { { 3,  3}, { 17, 17} },
        { { 3,  3}, { 18, 18} }
    },
    {   /* Con */
        { {20, 20}, { 10, 10} },
        { {12, 12}, { 19, 19} },
        { { 6,  6}, { 18, 18} },
        { { 8,  8}, { 18, 18} },
        { { 6,  6}, { 18, 18} },
        { {10, 10}, { 19, 19} },
        { {13, 13}, { 19, 19} },
        { { 3,  3}, { 18, 18} }
    },
    {   /* Cha */
        { {12, 12}, { 12, 12} },
        { { 3,  3}, { 16, 16} },
        { { 8,  8}, { 18, 18} },
        { { 3,  3}, { 18, 18} },
        { { 3,  3}, { 18, 18} },
        { { 3,  3}, { 18, 18} },
        { { 3,  3}, { 12, 12} },
        { { 3,  3}, { 18, 18} }
    },
    {   /* Str00: percentile strength, so only ever set for the 18s */
        { { 0,  0}, {  5,  5} },
        { { 0,  0}, { 99,  0} },
        { { 0,  0}, { 75,  0} },
        { { 0,  0}, { 50,  0} },
        { { 0,  0}, { 90,  0} },
        { { 0,  0}, {  0,  0} },
        { { 0,  0}, { 99, 75} },
        { { 0,  0}, {100, 50} }
    }
};

/* Indexed by ClassId: the eight single classes then the nine multi-class
 * combinations. */
const u8 limits_class_min[PSTAT_COUNT][CLASS_COUNT] = {
    /* Str   */ { 6,  0, 9, 12, 13, 0, 6, 15, 9, 9,  0, 0, 0, 9, 9, 9, 0 },
    /* Int   */ { 6,  0, 0,  9, 13, 9, 6,  0, 0, 9, 13, 9, 0, 9, 0, 9, 9 },
    /* Wis   */ { 9, 12, 6, 13, 14, 6, 0, 15, 9, 9, 14, 9, 9, 0, 0, 0, 0 },
    /* Dex   */ { 0,  0, 6,  0,  0, 6, 9, 15, 0, 0,  0, 0, 9, 0, 9, 9, 9 },
    /* Con   */ { 0,  0, 7,  9, 14, 0, 0, 11, 0, 0, 14, 0, 0, 0, 0, 0, 0 },
    /* Cha   */ { 0, 15, 0, 17,  0, 0, 0,  0, 0, 0,  0, 0, 0, 0, 0, 0, 0 },
    /* Str00 */ { 0,  0, 0,  0,  0, 0, 0,  0, 0, 0,  0, 0, 0, 0, 0, 0, 0 }
};

u8 limits_class_stat_min(ClassId cls, int stat)
{
    if (!class_valid((int)cls) || stat < 0 || stat >= PSTAT_COUNT) {
        log_warn("class stat minimum: no entry for class %d stat %d",
                 (int)cls, stat);
        return 0;
    }
    /* Transposed on purpose: Gbl.class_stats_min was one row per class, this
     * table is one row per stat, and the numbers are identical. The C# indexer
     * also stopped at Cha, so a request for Str00 - which every class leaves at
     * zero - threw there and answers zero here. */
    return limits_class_min[stat][cls];
}

/* byte_1A1CB, seg600:3EBB. Cleric, druid, fighter, paladin, ranger, magic-user,
 * thief, monk. */
const u8 limits_max_class_hit_dice[SKILL_COUNT] = {
    10, 15, 10, 10, 11, 12, 11, 13
};

/* unk_1A4EA. Column 0 is the count; the game walks columns 1..count looking for
 * the character's alignment. */
const u8 limits_class_alignments[CLASS_COUNT][CLASS_ALIGNMENT_COLS] = {
    /* cleric     */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* druid      */ { 5, 1, 3, 4, 5, 7, 0, 0, 0, 0 },
    /* fighter    */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* paladin    */ { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* ranger     */ { 3, 0, 3, 6, 0, 0, 0, 0, 0, 0 },
    /* magic user */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* thief      */ { 7, 1, 2, 3, 4, 5, 7, 8, 0, 0 },
    /* monk       */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* c/f        */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* c/f/m      */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* c/r        */ { 3, 0, 3, 6, 0, 0, 0, 0, 0, 0 },
    /* c/mu       */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* c/t        */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* f/mu       */ { 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    /* f/t        */ { 7, 1, 2, 3, 4, 5, 7, 8, 0, 0 },
    /* f/mu/t     */ { 7, 1, 2, 3, 4, 5, 7, 8, 0, 0 },
    /* mu/t       */ { 7, 1, 2, 3, 4, 5, 7, 8, 0, 0 }
};

const RaceClasses limits_race_classes[RACE_CLASSES_ROWS] = {
    /* monster  */ { 0, { 0 } },
    /* dwarf    */ { 3, { CLASS_FIGHTER, CLASS_THIEF, CLASS_MC_F_T } },
    /* elf      */ { 7, { CLASS_FIGHTER, CLASS_MAGIC_USER, CLASS_THIEF,
                          CLASS_MC_F_MU, CLASS_MC_F_T, CLASS_MC_F_MU_T,
                          CLASS_MC_MU_T } },
    /* gnome    */ { 3, { CLASS_FIGHTER, CLASS_THIEF, CLASS_MC_F_T } },
    /* half elf */ { 13, { CLASS_CLERIC, CLASS_FIGHTER, CLASS_MAGIC_USER,
                           CLASS_THIEF, CLASS_RANGER, CLASS_MC_C_F,
                           CLASS_MC_C_R, CLASS_MC_C_F_M, CLASS_MC_C_MU,
                           CLASS_MC_F_MU, CLASS_MC_F_T, CLASS_MC_F_MU_T,
                           CLASS_MC_MU_T } },
    /* halfling */ { 3, { CLASS_FIGHTER, CLASS_THIEF, CLASS_MC_F_T } },
    /* half orc */ { 6, { CLASS_CLERIC, CLASS_FIGHTER, CLASS_THIEF,
                          CLASS_MC_C_F, CLASS_MC_C_T, CLASS_MC_F_T } },
    /* human    */ { 6, { CLASS_CLERIC, CLASS_FIGHTER, CLASS_MAGIC_USER,
                          CLASS_THIEF, CLASS_PALADIN, CLASS_RANGER } },
    /* cheaters */ { 13, { CLASS_CLERIC, CLASS_FIGHTER, CLASS_MAGIC_USER,
                           CLASS_THIEF, CLASS_RANGER, CLASS_MC_C_F,
                           CLASS_MC_C_R, CLASS_MC_C_F_M, CLASS_MC_C_MU,
                           CLASS_MC_F_MU, CLASS_MC_F_T, CLASS_MC_F_MU_T,
                           CLASS_MC_MU_T } }
};

/* unk_1A35E. Columns are cleric, druid, fighter, paladin, ranger, magic-user,
 * thief. Rows a race cannot take read as all zeroes in the original too, and the
 * monster row holds whatever bytes happened to sit at seg600:404E - 0x0c08 is not
 * an age, it is the two bytes that follow the first entry read as a word. Both
 * are kept: the game never rolls an age for either. */
const RaceAge limits_race_ages[RACE_COUNT][RACE_AGE_CLASSES] = {
    {   /* monster */
        {      6,  2,    6 }, {  0x0c08, 0x0e, 0 }, { 0, 0, 0 },
        {      0,  6,    0 }, { 0x0502,    6, 3 }, { 4, 0, 0 },
        {      0,  0,    0 }
    },
    {   /* dwarf */
        {  0x00fa, 2, 0x14 }, { 0, 0, 0 }, { 0x28, 5, 4 }, { 0, 0, 0 },
        {       0, 0,    0 }, { 0, 0, 0 }, { 0x4b, 3, 6 }
    },
    {   /* elf */
        {  0x028a, 10, 10 }, { 0, 0, 0 }, { 0x82, 5, 6 }, { 0, 0, 0 },
        {       0,  0,  0 }, { 0x96, 5, 6 }, { 100, 5, 6 }
    },
    {   /* gnome */
        {  0x012c, 3, 0x0c }, { 0, 0, 0 }, { 0x3c, 5, 4 }, { 0, 0, 0 },
        {       0, 0,    0 }, { 100, 2, 0x0c }, { 0x50, 5, 4 }
    },
    {   /* half elf */
        {   0x28, 2, 4 }, { 0, 0, 0 }, { 0x16, 3, 4 }, { 0, 0, 0 },
        {      0, 0, 0 }, { 0x1e, 2, 8 }, { 0x16, 3, 8 }
    },
    {   /* halfling */
        {      0, 0, 0 }, { 0, 0, 0 }, { 20, 3, 4 }, { 0, 0, 0 },
        {      0, 0, 0 }, { 0, 0, 0 }, { 0x28, 2, 4 }
    },
    {   /* half orc */
        {     20, 1, 4 }, { 0, 0, 0 }, { 0x0d, 1, 4 }, { 0, 0, 0 },
        {      0, 0, 0 }, { 0, 0, 0 }, { 20, 2, 4 }
    },
    {   /* human */
        {     18, 1, 4 }, { 18, 1, 4 }, { 15, 1, 4 }, { 17, 1, 4 },
        {     20, 1, 4 }, { 0x18, 2, 4 }, { 18, 1, 4 }
    }
};

const RaceAge *limits_race_age(int race, int cls)
{
    if (!race_valid(race) || cls < 0 || cls >= RACE_AGE_CLASSES) {
        log_warn("starting age: no entry for race %d class %d", race, cls);
        return NULL;
    }
    return &limits_race_ages[race][cls];
}

/* --------------------------------------------------------- level ceilings */

bool limits_race_class_limit(int class_lvl, const struct Player *p, ClassId cls)
{
    bool race_limited = false;
    int  str = p->stats.value[PSTAT_STR].full;

    switch (p->race) {
    case RACE_DWARF:
        if (cls == CLASS_FIGHTER) {
            if (class_lvl == 9 ||
                (class_lvl == 8 && str == 17) ||
                (class_lvl == 7 && str < 17)) {
                race_limited = true;
            }
        }
        break;

    case RACE_ELF:
        if (cls == CLASS_FIGHTER) {
            if (class_lvl == 7 ||
                (class_lvl == 6 && str == 17) ||
                (class_lvl == 5 && str < 17)) {
                race_limited = true;
            }
        }
        if (cls == CLASS_MAGIC_USER) {
            int intel = p->stats.value[PSTAT_INT].full;

            if (class_lvl == 11 ||
                (class_lvl == 9 && intel < 17) ||
                (class_lvl == 10 && intel == 17)) {
                race_limited = true;
            }
        }
        break;

    case RACE_GNOME:
        if (cls == CLASS_FIGHTER) {
            if (class_lvl == 6 ||
                (class_lvl == 5 && str < 18)) {
                race_limited = true;
            }
        }
        break;

    case RACE_HALF_ELF:
        if (cls == CLASS_CLERIC && class_lvl == 5) {
            race_limited = true;
        } else {
            if (cls == CLASS_FIGHTER || cls == CLASS_RANGER) {
                if (class_lvl == 8 ||
                    (class_lvl == 7 && str == 17) ||
                    (class_lvl == 6 && str < 17)) {
                    race_limited = true;
                }
            }
            /* The magic-user ceiling tests Strength, not Intelligence. That is
             * what the disassembly does, and half-elf mages are gated on it. */
            if (cls == CLASS_MAGIC_USER) {
                if (class_lvl == 8 ||
                    (class_lvl == 7 && str == 17) ||
                    (class_lvl == 6 && str < 17)) {
                    race_limited = true;
                }
            }
        }
        break;

    case RACE_HALFLING:
        if (cls == CLASS_FIGHTER) {
            if (class_lvl == 6 ||
                (class_lvl == 5 && str == 17) ||
                (class_lvl == 4 && str < 17)) {
                race_limited = true;
            }
        }
        break;

    default:
        break;
    }

    if (cheats.no_race_level_limits) {
        race_limited = false;
    }

    return race_limited;
}

bool limits_race_stat_level_restricted(ClassId cls, const struct Player *p)
{
    bool race_limited = false;
    int  class_lvl;
    int  str = p->stats.value[PSTAT_STR].full;

    /* ClassLevel has one entry per SkillType, so only the eight single classes
     * index it. A multi-class id has no single level to test and is answered
     * "not restricted", which is what the switch below would have concluded. */
    if (!skill_valid((int)cls)) {
        return false;
    }

    class_lvl = p->class_level[cls];
    if (class_lvl > 0) {
        switch (p->race) {
        case RACE_DWARF:
            if (cls == CLASS_FIGHTER) {
                /* No level-9 case here, unlike RaceClassLimit above. */
                if ((class_lvl == 8 && str == 17) ||
                    (class_lvl == 7 && str < 17)) {
                    race_limited = true;
                }
            }
            break;

        case RACE_ELF:
            if (cls == CLASS_FIGHTER) {
                if (class_lvl == 7 ||
                    (class_lvl == 6 && str == 17) ||
                    (class_lvl == 5 && str < 17)) {
                    race_limited = true;
                }
            }
            break;

        case RACE_GNOME:
            if (cls == CLASS_FIGHTER) {
                if (class_lvl == 6 ||
                    (class_lvl == 5 && str < 18)) {
                    race_limited = true;
                }
            }
            break;

        case RACE_HALF_ELF:
            if (cls == CLASS_CLERIC && class_lvl == 5) {
                race_limited = true;
            } else if (cls == CLASS_FIGHTER) {
                if (class_lvl == 8 ||
                    (class_lvl == 7 && str == 17) ||
                    (class_lvl == 6 && str < 17)) {
                    race_limited = true;
                }
            }
            break;

        case RACE_HALFLING:
            if (cls == CLASS_FIGHTER) {
                if (class_lvl == 6 ||
                    (class_lvl == 5 && str == 17) ||
                    (class_lvl == 4 && str < 17)) {
                    race_limited = true;
                }
            }
            break;

        default:
            break;
        }
    }

    if (cheats.no_race_level_limits) {
        race_limited = false;
    }

    return race_limited;
}
