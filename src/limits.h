/* limits.h - the AD&D race, sex, class and age tables.
 * Ported from Classes/Limits.cs (unk_1A434 and neighbours in seg600).
 *
 * These bound character generation and level advancement. Every table was
 * transcribed from the disassembly verbatim, including the made-up monster row
 * the C# comments flag, so the numbers are the ones the original used and not
 * the ones the printed rules give.
 */
#ifndef COAB_LIMITS_H
#define COAB_LIMITS_H

#include "coab.h"
#include "enums.h"

struct Player;

/* A PlayerStats record holds seven values: the six Stat entries in Stat order,
 * plus percentile strength. Str00 has no Stat of its own because the stat
 * screens never offer it as a separate line. */
typedef enum {
    PSTAT_STR   = STAT_STR,
    PSTAT_INT   = STAT_INT,
    PSTAT_WIS   = STAT_WIS,
    PSTAT_DEX   = STAT_DEX,
    PSTAT_CON   = STAT_CON,
    PSTAT_CHA   = STAT_CHA,
    PSTAT_STR00 = 6,
    PSTAT_COUNT = 7
} PlayerStatId;

#define AGE_BRACKETS 5

/* Age at which each of the five ageing steps takes effect, per race. */
extern const int limits_race_age_brackets[RACE_COUNT][AGE_BRACKETS];

/* What each ageing step does to a stat. Signed: most are penalties. */
extern const i8 limits_age_effect[PSTAT_COUNT][AGE_BRACKETS];

/* [stat][race][0 = min, 1 = max][sex]. Sex is 0 or 1 as stored in the player
 * record. */
extern const u8 limits_race_sex_min_max[PSTAT_COUNT][RACE_COUNT][2][2];

/* Minimum a class demands of each stat, indexed by ClassId. */
extern const u8 limits_class_min[PSTAT_COUNT][CLASS_COUNT];

/* Gbl.class_stats_min (unk_1A484) held the same numbers as limits_class_min the
 * other way round, one row per class. Rather than keep a second copy that could
 * drift, this reads the one table; the argument order is the C# indexer's. */
u8 limits_class_stat_min(ClassId cls, int stat);

/* Highest level at which a class still rolls a hit die (byte_1A1CB,
 * seg600:3EBB). Indexed by SkillType, so only the single classes. */
extern const u8 limits_max_class_hit_dice[SKILL_COUNT];

/* Alignments a class allows (unk_1A4EA). Column 0 is how many of the nine that
 * follow are used, which is how the original stored it. */
#define CLASS_ALIGNMENT_COLS 10

extern const u8 limits_class_alignments[CLASS_COUNT][CLASS_ALIGNMENT_COLS];

/* Which classes a race may take (Gbl.RaceClasses). Row RACE_COUNT is the extra
 * row the C# added for the cheat menu, which is why this is one longer than
 * RACE_COUNT. */
#define RACE_CLASSES_ROWS   (RACE_COUNT + 1)
#define RACE_CLASSES_MAX    13

typedef struct {
    int     count;
    ClassId cls[RACE_CLASSES_MAX];
} RaceClasses;

extern const RaceClasses limits_race_classes[RACE_CLASSES_ROWS];

/* Starting age per race and class (unk_1A35E): base plus dice_count d dice_size.
 * Only seven classes have a row - cleric, druid, fighter, paladin, ranger,
 * magic-user, thief - because monks cannot be rolled up. */
#define RACE_AGE_CLASSES 7

typedef struct {
    i16 base_age;
    u8  dice_count;
    u8  dice_size;
} RaceAge;

extern const RaceAge limits_race_ages[RACE_COUNT][RACE_AGE_CLASSES];

/* NULL for a race or class outside the table. */
const RaceAge *limits_race_age(int race, int cls);

/* Limits.RaceClassLimit: would advancing this class to class_lvl hit the racial
 * ceiling? class_lvl is the level being advanced *to*. */
bool limits_race_class_limit(int class_lvl, const struct Player *p, ClassId cls);

/* Limits.RaceStatLevelRestricted (sub_69138): is the character already at its
 * racial ceiling in this class? Reads the level from the player. */
bool limits_race_stat_level_restricted(ClassId cls, const struct Player *p);

#endif /* COAB_LIMITS_H */
