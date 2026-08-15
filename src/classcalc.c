/* classcalc.c - Ported from engine/ovr026.cs. */
#include <stdio.h>
#include <string.h>

#include "classcalc.h"

#include "gbl.h"
#include "item.h"
#include "limits.h"
#include "log.h"
#include "menu.h"
#include "prompt.h"
#include "spells.h"
#include "text.h"

/* --------------------------------------------------------------- the tables */

/* ovr026.ClericSpellLevels, seg600:42BC unk_1A5CC. Row n is what a cleric gains
 * on reaching level n + 2. Only eleven rows: a cleric is capped at 12. */
static const u8 CLERIC_SPELL_LEVELS[11][5] = {
    { 1, 0, 0, 0, 0 },
    { 0, 1, 0, 0, 0 },
    { 1, 1, 0, 0, 0 },
    { 0, 1, 1, 0, 0 },
    { 0, 0, 1, 0, 0 },
    { 0, 0, 0, 1, 0 },
    { 0, 0, 1, 1, 0 },
    { 1, 1, 0, 0, 1 },      /* seg600:42EE */
    { 0, 0, 0, 1, 1 },      /* seg600:42F3 */
    { 1, 0, 1, 0, 0 },      /* seg600:42F8 */
    { 1, 1, 1, 0, 0 }       /* seg600:42FD */
};

/* ovr026.PaladinSpellLevels, seg600:43E5 unk_1A6F5. Indexed by the level being
 * added rather than by level minus two, which is why the first eight rows are
 * empty: a paladin casts nothing until level 9. */
static const u8 PALADIN_SPELL_LEVELS[12][5] = {
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 1, 0, 0, 0, 0 },
    { 1, 0, 0, 0, 0 },
    { 0, 1, 0, 0, 0 },
    { 0, 1, 0, 0, 0 }
};

/* ovr026.unk_1A758, seg600:4448. The ranger's, and the odd one out: its five
 * columns are not five spell levels but three druid levels followed by two
 * magic-user ones, which is how a ranger ends up with both kinds. Indexed by
 * level, again with the empty rows in front. */
static const u8 RANGER_SPELL_LEVELS[13][5] = {
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
    { 1, 0, 0, 0, 0 },
    { 0, 0, 0, 1, 0 },
    { 1, 0, 0, 0, 0 },
    { 0, 0, 0, 1, 0 },
    { 0, 1, 0, 0, 0 }
};

/* ovr018.thac0_table, seg600:3E3A unk_1A14A. */
const i8 classcalc_thac0_table[SKILL_COUNT][CLASSCALC_MAX_TABLE_LEVEL + 1] = {
    /* cleric     */ {   40,   40,   40,   40, 0x2a, 0x2a, 0x2a, 0x2c, 0x2c, 0x2c, 0x2e, 0x2e, 0x2e },
    /* druid      */ {   40,   40,   40,   40, 0x2a, 0x2a, 0x2a, 0x2c, 0x2c, 0x2c, 0x2e, 0x2e, 0x2e },
    /* fighter    */ { 0x27,   40,   40, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33 },
    /* paladin    */ {   40,   40,   40, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33 },
    /* ranger     */ {   40,   40,   40, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33 },
    /* magic user */ { 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x29, 0x29, 0x29, 0x29, 0x29, 0x2b, 0x2b },
    /* thief      */ {   40,   40,   40,   40,   40, 0x29, 0x29, 0x29, 0x29, 0x2c, 0x2c, 0x2c, 0x2c },
    /* monk       */ {   40,   40,   40,   40, 0x2a, 0x2a, 0x2a, 0x2c, 0x2c, 0x2c, 0x2e, 0x2e, 0x2e }
};

/* ovr018.unk_1A1B2, seg600:3EA2. */
const u8 classcalc_class_flag_bits[SKILL_COUNT] = {
    0x02,   /* cleric */
    0x10,   /* druid */
    0x08,   /* fighter */
    0x40,   /* paladin */
    0x40,   /* ranger - shares the paladin's bit */
    0x01,   /* magic user */
    0x04,   /* thief */
    0x20    /* monk */
};

/* ovr020.MU_spell_lvl_learn, seg600:44B6 unk_1A7C6. */
const u8 classcalc_mu_spell_lvl_learn[CLASSCALC_MU_LEVEL_ROWS][5] = {
    { 1, 0, 0, 0, 0 },
    { 0, 1, 0, 0, 0 },
    { 1, 1, 0, 0, 0 },
    { 1, 0, 1, 0, 0 },
    { 0, 0, 1, 0, 0 },
    { 0, 1, 0, 1, 0 },
    { 0, 0, 1, 1, 0 },
    { 0, 0, 0, 0, 1 },
    { 0, 1, 0, 0, 1 },
    { 0, 0, 1, 1, 1 },
    { 0, 0, 0, 1, 1 }
};

/* ovr026.SaveThrowValues, [class][class level][save type]. The number to roll or
 * better, so 20 is "no chance" and level 0 is 20 across the board - a class the
 * character does not have cannot improve a save. Save types are in
 * SaveVerseType order: poison, petrification, rod/staff/wand, breath, spell. */
static const u8 SAVE_THROW_VALUES[SKILL_COUNT][CLASSCALC_MAX_TABLE_LEVEL + 1][SAVE_VERSE_COUNT] = {
    {   /* cleric */
        {20, 20, 20, 20, 20}, {10, 13, 14, 16, 15}, {10, 13, 14, 16, 15},
        {10, 13, 14, 16, 15}, { 9, 12, 13, 15, 14}, { 9, 12, 13, 15, 14},
        { 9, 12, 13, 15, 14}, { 7, 10, 11, 13, 12}, { 7, 10, 11, 13, 12},
        { 7, 10, 11, 13, 12}, { 6,  9, 10, 12, 11}, { 6,  9, 10, 12, 11},
        { 6,  9, 10, 12, 11}
    },
    {   /* druid - the cleric's row again */
        {20, 20, 20, 20, 20}, {10, 13, 14, 16, 15}, {10, 13, 14, 16, 15},
        {10, 13, 14, 16, 15}, { 9, 12, 13, 15, 14}, { 9, 12, 13, 15, 14},
        { 9, 12, 13, 15, 14}, { 7, 10, 11, 13, 12}, { 7, 10, 11, 13, 12},
        { 7, 10, 11, 13, 12}, { 6,  9, 10, 12, 11}, { 6,  9, 10, 12, 11},
        { 6,  9, 10, 12, 11}
    },
    {   /* fighter */
        {20, 20, 20, 20, 20}, {14, 15, 16, 17, 17}, {14, 15, 16, 17, 17},
        {13, 14, 15, 16, 16}, {13, 14, 15, 16, 16}, {11, 12, 13, 13, 14},
        {11, 12, 13, 13, 14}, {10, 11, 12, 12, 13}, {10, 11, 12, 12, 13},
        { 8,  9, 10,  9, 11}, { 8,  9, 10,  9, 11}, { 7,  8,  9,  8, 10},
        { 7,  8,  9,  8, 10}
    },
    {   /* paladin - the fighter's, two better all round; level 8's poison is 9
         * where the pattern wants 8, and that is what the table holds. */
        {20, 20, 20, 20, 20}, {12, 13, 14, 15, 15}, {12, 13, 14, 15, 15},
        {11, 12, 13, 14, 14}, {11, 12, 13, 14, 14}, { 9,  9, 11, 11, 12},
        { 9,  9, 11, 11, 12}, { 8,  9, 10, 10, 11}, { 9,  9, 10, 10, 11},
        { 6,  7,  8,  7,  9}, { 6,  7,  8,  7,  9}, { 5,  6,  7,  6,  8},
        { 5,  6,  7,  6,  8}
    },
    {   /* ranger - the fighter's row again */
        {20, 20, 20, 20, 20}, {14, 15, 16, 17, 17}, {14, 15, 16, 17, 17},
        {13, 14, 15, 16, 16}, {13, 14, 15, 16, 16}, {11, 12, 13, 13, 14},
        {11, 12, 13, 13, 14}, {10, 11, 12, 12, 13}, {10, 11, 12, 12, 13},
        { 8,  9, 10,  9, 11}, { 8,  9, 10,  9, 11}, { 7,  8,  9,  8, 10},
        { 7,  8,  9,  8, 10}
    },
    {   /* magic user */
        {20, 20, 20, 20, 20}, {14, 13, 11, 15, 12}, {14, 13, 11, 15, 12},
        {14, 13, 11, 15, 12}, {14, 13, 11, 15, 12}, {14, 13, 11, 15, 12},
        {13, 11,  9, 13, 10}, {13, 11,  9, 13, 10}, {13, 11,  9, 13, 10},
        {13, 11,  9, 13, 10}, {13, 11,  9, 13, 10}, {11,  9,  7, 11,  8},
        {11,  9,  7, 11,  8}
    },
    {   /* thief */
        {20, 20, 20, 20, 20}, {13, 12, 14, 16, 15}, {13, 12, 14, 16, 15},
        {13, 12, 14, 16, 15}, {13, 12, 14, 16, 15}, {12, 11, 12, 15, 13},
        {12, 11, 12, 15, 13}, {12, 11, 12, 15, 13}, {12, 11, 12, 15, 13},
        {11, 10, 10, 14, 11}, {11, 10, 10, 14, 11}, {11, 10, 10, 14, 11},
        {11, 10, 10, 14, 11}
    },
    {   /* monk - the thief's row for the first nine levels, then two rows of the
         * magic-user's. Monks cannot be rolled up, so nothing reaches this. */
        {20, 20, 20, 20, 20}, {13, 12, 14, 16, 15}, {13, 12, 14, 16, 15},
        {13, 12, 14, 16, 15}, {13, 12, 14, 16, 15}, {12, 11, 12, 15, 13},
        {12, 11, 12, 15, 13}, {12, 11, 12, 15, 13}, {12, 11, 12, 15, 13},
        {11, 10, 10, 14, 11}, {13, 11,  9, 13, 10}, {11,  9,  7, 11,  8},
        {11,  9,  7, 11,  8}
    }
};

/* ovr026.unk_1A230, seg600:3F20. The racial adjustment to each thief skill,
 * [race][skill] with skill 1..8 and column 0 unused. Only the first eight rows
 * are reachable - there are eight races - and the five after them are whatever
 * follows the table in seg600. */
static const i8 THIEF_RACE_ADJ[13][9] = {
    /* monster  */ { 0,   0,   0,   0,   0,   0,   0,   0,   0 },
    /* dwarf    */ { 0,   0,  10,  15,   0,   0,   0, -10,  -5 },
    /* elf      */ { 0,   5,  -5,   0,   5,  10,   5,   0,   0 },
    /* gnome    */ { 0,   0,   5,  10,   5,   5,  10, -15,   0 },
    /* half elf */ { 0,  10,   0,   0,   0,   5,   0,   0,   0 },
    /* halfling */ { 0,   5,   5,   5,  10,  15,   5, -15,  -5 },
    /* half orc */ { 0,  -5,   5,   5,   0,   0,   5,   5, -10 },
    /* human    */ { 0,   0,   0,   0,   0,   0,   0,   0,   0 },
    /*          */ { 0, -15, -10, -10, -20, -10, -19,  -5,  10 },
    /*          */ { 0, -15,  -5,  -5,   0,  -5, -10,   0,   0 },
    /*          */ { 0,   0,   0,  -5,   0,   0,   0,   0,   0 },
    /*          */ { 0,   0,   0,   0,   0,   0,   0,   0,   0 },
    /*          */ { 0,   0,   0,   0,   0,  -5,   0,   0,   0 }
};

/* ovr026.unk_1A243, seg600:3F33. The dexterity adjustment, [dexterity][skill].
 * Six columns, and again column 0 is unused: only skills 1..5 - pick pockets,
 * open locks, find/remove traps, move silently, hide in shadows - are adjusted
 * for dexterity. Columns 1..5 of rows 9 to 19 are the printed AD&D thief
 * dexterity table exactly; row 10 has -19 where that table has -10.
 *
 * Rows 20 and 21 are not dexterity data - the numbers run 12, 8, 8, 18, 17, 99
 * and then 99, 0, 3, 18, 3, 18, which is the next thing in seg600 read as if it
 * were. A dexterity of 20 or 21 therefore reads nonsense, and 22 and above
 * (which a Gauntlets of Ogre Power character can reach) runs off the end: that
 * threw in the C# and is logged and skipped here. */
static const i8 THIEF_DEX_ADJ[22][6] = {
    {   0,   5,  10,   5,   0,   0 },
    {   0,   0,   5,  10,   5,   5 },
    {   5,  10, -15,   0,  10,   0 },
    {   0,   0,   0,   5,   0,   0 },
    {   0,   0,   5,   5,   5,  10 },
    {  10,  15,   5, -15,  -5,  -5 },
    {  -5,   5,   5,   0,   0,   5 },
    {   5,   5, -10,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0 },
    {   0, -15, -10, -10, -20, -10 },
    { -10, -19,  -5, -10, -15,  -5 },
    {  -5,  -5,   0,  -5, -10,   0 },
    {   0,   0,   0,   0,  -5,   0 },
    {   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0 },
    {   0,   0,  -5,   0,   0,   0 },
    {   0,   5,  10,   0,   5,   5 },
    {   5,  10,  15,   5,  10,  10 },
    {  10,  15,  20,  10,  12,  12 },
    {  12,   8,   8,  18,  17,  99 },
    {  99,   0,   3,  18,   3,  18 }
};

/* ovr026.base_chance, seg600:3EC0 unk_1A1D0. [thief level][skill], the chance
 * before race and dexterity. Rows 6 and up are visibly shifted along by a column
 * against rows 1 to 5 - read languages, the last skill, only appears from row 4 -
 * so the higher rows are not the printed table. They are what the game uses. */
static const u8 THIEF_BASE_CHANCE[13][9] = {
    { 0,    0,    0,    0,    0,    0,    0,    0,    0 },
    { 0, 0x1e, 0x19, 0x14, 0x0f, 0x0a, 0x0a, 0x55, 0x00 },
    { 0, 0x23, 0x1d, 0x19, 0x15, 0x0f, 0x0a, 0x56, 0x00 },
    { 0, 0x28, 0x21, 0x1e, 0x1b, 0x14, 0x0f, 0x57, 0x00 },
    { 0, 0x2d, 0x25, 0x23, 0x21, 0x19, 0x0f, 0x58, 0x14 },
    { 0, 0x32, 0x2a, 0x28, 0x28, 0x1f, 0x14, 0x5a, 0x19 },
    { 0, 0x37, 0x2f, 0x2d, 0x25, 0x14, 0x5c, 0x1e, 0x3c },
    { 0, 0x34, 0x32, 0x37, 0x2b, 0x19, 0x5e, 0x23, 0x41 },
    { 0, 0x39, 0x37, 0x3e, 0x31, 0x19, 0x60, 0x28, 0x46 },
    { 0, 0x3e, 0x3c, 0x46, 0x38, 0x1e, 0x62, 0x2d, 0x50 },
    { 0, 0x43, 0x41, 0x4e, 0x3f, 0x1e, 0x63, 0x32, 0x5a },
    { 0, 0x48, 0x46, 0x56, 0x46, 0x23, 0x63, 0x3c, 0x64 },
    { 0, 0x64, 0x4d, 0x4b, 0x5e, 0x4d, 0x23, 0x63, 0x41 }
};

/* --------------------------------------------------------------- spell slots */

/* The two spell-learning loops in sub_6A00F, which differ only in whether
 * animate_dead is skipped. The Spells enum names every id from 1 to 0x64; id
 * 0x65, which spell_casting_table also has a row for, is not one of them and is
 * not offered here either.
 *
 * spell_level is a flat 1..15 across the three spell classes, so it splits into
 * the row and column of spell_cast_count. One spell breaks the pattern: 0x38
 * Restoration is a cleric spell of level 7, which lands in the druid row. A
 * cleric has no druid slots, so a cleric never learns it here. */
static void learn_castable_spells(Player *player, bool skip_animate_dead)
{
    for (int spell = 1; spell <= 0x64; spell++) {
        const SpellEntry *se = spell_entry(spell);
        int sp_class;
        int sp_lvl;

        if (se == NULL) {
            continue;
        }

        sp_class = (se->spell_level - 1) / 5;
        sp_lvl   = (se->spell_level - 1) % 5;

        if (sp_class < 0 || sp_class > 2 || sp_lvl < 0 || sp_lvl > 4) {
            log_warn("spell slots: spell 0x%x has level %d", spell,
                     se->spell_level);
            continue;
        }

        if (se->spell_class == SPELL_CLASS_CLERIC &&
            player->spell_cast_count[sp_class][sp_lvl] > 0 &&
            !(skip_animate_dead && spell == SPELL_ANIMATE_DEAD)) {
            player_learn_spell(player, (Spells)spell);
        }
    }
}

void classcalc_spell_cast_counts(Player *player)
{
    if (player == NULL) {
        return;
    }

    for (int i = 0; i < 5; i++) {
        player->spell_cast_count[0][i] = 0;
        player->spell_cast_count[1][i] = 0;
        player->spell_cast_count[2][i] = 0;
    }

    for (int skill = SKILL_CLERIC; skill <= SKILL_MONK; skill++) {
        int skill_level = player_skill_level(player, (SkillType)skill);

        if (skill_level <= 0) {
            continue;
        }

        switch (skill) {
        case SKILL_CLERIC:
            player->spell_cast_count[0][0] += 1;

            for (int lvl = 0; lvl <= skill_level - 2; lvl++) {
                if ((size_t)lvl >= COAB_ARRAY_LEN(CLERIC_SPELL_LEVELS)) {
                    log_warn("spell slots: cleric level %d past the table",
                             lvl + 2);
                    break;
                }
                for (int sp_lvl = 0; sp_lvl < 5; sp_lvl++) {
                    player->spell_cast_count[0][sp_lvl] +=
                        CLERIC_SPELL_LEVELS[lvl][sp_lvl];
                }
            }

            classcalc_cleric_spells(false, player);

            /* Animate Dead is a cleric spell of a level the character can hold,
             * but it is not one the game lets a player memorise. */
            learn_castable_spells(player, true);
            break;

        case SKILL_PALADIN:
            if (skill_level > 8) {
                for (int lvl = 8; lvl < skill_level; lvl++) {
                    if ((size_t)lvl >= COAB_ARRAY_LEN(PALADIN_SPELL_LEVELS)) {
                        log_warn("spell slots: paladin level %d past the table",
                                 lvl);
                        break;
                    }
                    for (int sp_lvl = 0; sp_lvl < 5; sp_lvl++) {
                        player->spell_cast_count[0][sp_lvl] +=
                            PALADIN_SPELL_LEVELS[lvl][sp_lvl];
                    }
                }

                /* No animate_dead exception on this path, so a high-level
                 * paladin ends up knowing it. It is a level 3 cleric spell and
                 * a paladin never gets a level 3 slot, so it stays unmemorisable
                 * either way. */
                learn_castable_spells(player, false);
            }
            break;

        case SKILL_RANGER:
            if (skill_level > 7) {
                /* Inclusive of skill_level, unlike the paladin loop above: a
                 * ranger's spells arrive one level later than the table row
                 * suggests. */
                for (int lvl = 8; lvl <= skill_level; lvl++) {
                    if ((size_t)lvl >= COAB_ARRAY_LEN(RANGER_SPELL_LEVELS)) {
                        log_warn("spell slots: ranger level %d past the table",
                                 lvl);
                        break;
                    }

                    /* Columns 0..2 are druid spell levels 1..3. */
                    for (int sp_lvl = 0; sp_lvl < 3; sp_lvl++) {
                        player->spell_cast_count[1][sp_lvl] +=
                            RANGER_SPELL_LEVELS[lvl][sp_lvl];
                    }
                    /* Columns 3 and 4 are magic-user spell levels 1 and 2. */
                    for (int sp_lvl = 3; sp_lvl < 5; sp_lvl++) {
                        player->spell_cast_count[2][sp_lvl - 3] +=
                            RANGER_SPELL_LEVELS[lvl][sp_lvl];
                    }
                }

                /* Every druid spell, not just the ones there is a slot for. */
                for (int spell = 1; spell <= 0x64; spell++) {
                    const SpellEntry *se = spell_entry(spell);

                    if (se != NULL && se->spell_class == SPELL_CLASS_DRUID) {
                        player_learn_spell(player, (Spells)spell);
                    }
                }
            }
            break;

        case SKILL_MAGIC_USER:
            player->spell_cast_count[2][0] += 1;

            for (int lvl = 0; lvl <= skill_level - 2; lvl++) {
                if ((size_t)lvl >= COAB_ARRAY_LEN(classcalc_mu_spell_lvl_learn)) {
                    log_warn("spell slots: magic-user level %d past the table",
                             lvl + 2);
                    break;
                }
                for (int sp_lvl = 0; sp_lvl < 5; sp_lvl++) {
                    player->spell_cast_count[2][sp_lvl] +=
                        classcalc_mu_spell_lvl_learn[lvl][sp_lvl];
                }
            }
            break;

        default:
            /* Druid, fighter, thief and monk levels grant no spells. */
            break;
        }
    }

    /* Once per readied item carrying it, so two of them would quadruple the
     * count. Only the first three magic-user levels are doubled. */
    for (int i = 0; i < player->item_count; i++) {
        if (player->items[i].affect_3 == (int)AFFECT_PROTECT_MAGIC &&
            player->items[i].readied) {
            for (int sp_lvl = 0; sp_lvl < 3; sp_lvl++) {
                player->spell_cast_count[2][sp_lvl] *= 2;
            }
        }
    }
}

void classcalc_cleric_spells(bool reset_spell_levels, Player *player)
{
    int cleric_lvl;
    int wis;

    if (player == NULL) {
        return;
    }

    cleric_lvl = player_skill_level(player, SKILL_CLERIC);
    if (cleric_lvl <= 0) {
        return;
    }

    if (reset_spell_levels) {
        for (int sp_lvl = 1; sp_lvl < 5; sp_lvl++) {
            player->spell_cast_count[0][sp_lvl] = 0;
        }
        player->spell_cast_count[0][0] = 1;

        for (int lvl = 0; lvl <= cleric_lvl - 2; lvl++) {
            if ((size_t)lvl >= COAB_ARRAY_LEN(CLERIC_SPELL_LEVELS)) {
                log_warn("cleric spells: level %d past the table", lvl + 2);
                break;
            }
            for (int sp_lvl = 0; sp_lvl < 5; sp_lvl++) {
                player->spell_cast_count[0][sp_lvl] +=
                    CLERIC_SPELL_LEVELS[lvl][sp_lvl];
            }
        }
    }

    /* The AD&D wisdom bonus spells: two extra level 1s from 13 and 14, two
     * extra level 2s from 15 and 16, a level 3 from 17 and a level 4 from 18.
     * Each is only given when the character already has a slot of that level,
     * so a level 1 cleric gains only the first-level ones.
     *
     * Nothing here notices whether the bonus has already been added, so calling
     * this twice without reset_spell_levels adds it twice. Every caller either
     * passes true or has just rebuilt the row from zero. */
    wis = player->stats.value[PSTAT_WIS].full;

    if (wis > 12 && player->spell_cast_count[0][0] > 0) {
        player->spell_cast_count[0][0] += 1;
    }
    if (wis > 13 && player->spell_cast_count[0][0] > 0) {
        player->spell_cast_count[0][0] += 1;
    }
    if (wis > 14 && player->spell_cast_count[0][1] > 0) {
        player->spell_cast_count[0][1] += 1;
    }
    if (wis > 15 && player->spell_cast_count[0][1] > 0) {
        player->spell_cast_count[0][1] += 1;
    }
    if (wis > 16 && player->spell_cast_count[0][2] > 0) {
        player->spell_cast_count[0][2] += 1;
    }
    if (wis > 17 && player->spell_cast_count[0][3] > 0) {
        player->spell_cast_count[0][3] += 1;
    }
}

/* ------------------------------------------------------------ saving throws */

/* ovr026.SaveVersePoisonBonus. Dwarves, halflings and anyone with a readied
 * AFFECT_ITEM_AFFECT_6 item get the constitution adjustment; the exceptional
 * constitutions from 19 up adjust everyone's.
 *
 * These add to save_verse[poison], and a saving throw succeeds on a roll of
 * save_verse or more (see effect.c), so every one of them makes the poison save
 * harder rather than easier. Read as the rules intend they would be subtracted.
 * The reference implementation adds, so this does too. */
static void save_verse_poison_bonus(Player *player, bool apply_bonus)
{
    const int poison = SAVE_VERSE_POISON;
    int con = player->stats.value[PSTAT_CON].full;

    if (player->race == RACE_DWARF ||
        player->race == RACE_HALFLING ||
        apply_bonus) {
        if (con >= 4 && con <= 6) {
            player->save_verse[poison] += 1;
        } else if (con >= 7 && con <= 10) {
            player->save_verse[poison] += 2;
        } else if (con >= 11 && con <= 13) {
            player->save_verse[poison] += 3;
        } else if (con >= 14 && con <= 17) {
            player->save_verse[poison] += 4;
        } else if (con == 18) {
            player->save_verse[poison] += 5;
        }
    }

    if (con == 19 || con == 20) {
        player->save_verse[poison] += 1;
    } else if (con == 21 || con == 22) {
        player->save_verse[poison] += 2;
    } else if (con == 23 || con == 24) {
        player->save_verse[poison] += 3;
    } else if (con == 25) {
        player->save_verse[poison] += 4;
    }
}

/* The table row for a class at a level, or NULL past its end. */
static const u8 *save_throw_row(int cls, int class_lvl)
{
    if (!skill_valid(cls) || class_lvl < 0 ||
        class_lvl > CLASSCALC_MAX_TABLE_LEVEL) {
        log_warn("saving throws: no row for class %d level %d", cls, class_lvl);
        return NULL;
    }
    return SAVE_THROW_VALUES[cls][class_lvl];
}

void classcalc_saving_throws(Player *player)
{
    bool apply_bonus = false;

    if (player == NULL) {
        return;
    }

    for (int i = 0; i < player->item_count; i++) {
        if (player->items[i].affect_3 == (int)AFFECT_ITEM_AFFECT_6 &&
            player->items[i].readied) {
            apply_bonus = true;
            break;
        }
    }

    for (int save = 0; save < SAVE_VERSE_COUNT; save++) {
        player->save_verse[save] = 20;

        for (int cls = 0; cls <= 7; cls++) {
            if (player->class_level[cls] > 0) {
                const u8 *row = save_throw_row(cls, player->class_level[cls]);

                if (row != NULL && player->save_verse[save] > row[save]) {
                    player->save_verse[save] = row[save];
                }
            }
        }

        /* The original tested the loop variable after using it and left the
         * loop on 7, so this last case is always the monk column. The C#
         * rewrote the loop with a pre-increment test, which leaves the variable
         * at 8, and set it back to 7 here; this keeps that.
         *
         * What it does: a dual-classed character whose new class has passed the
         * old one keeps the better of the two monk rows. Nothing can be a monk,
         * so both levels are zero and both rows are the level-0 twenties. */
        {
            const int cls = 7;

            if (player->class_level[cls] > player->class_level_old[cls]) {
                const u8 *row = save_throw_row(cls, player->class_level_old[cls]);

                if (row != NULL && player->save_verse[save] > row[save]) {
                    player->save_verse[save] = row[save];
                }
            }
        }

        if (save == SAVE_VERSE_POISON) {
            save_verse_poison_bonus(player, apply_bonus);
        }
    }
}

/* -------------------------------------------------------------- thief skills */

void classcalc_thief_skills(Player *player)
{
    Item *item_found = NULL;
    bool  level_boost_item;    /* var_A: raises a low thief level */
    bool  flat_bonus_item;     /* var_B: +10 to everything */
    int   thief_lvl;
    int   orig_thief_lvl;
    int   extra = 0;           /* var_2 */
    int   race;
    int   dex;

    if (player == NULL) {
        return;
    }

    /* The two thieving items, by the spell id in their third name part: 11 is the
     * one that lifts the thief level for the first two skills, 2 the flat ten per
     * cent on all eight. Only the first such item found counts. */
    for (int i = 0; i < player->item_count; i++) {
        Item *it = &player->items[i];

        if (it->readied && (item_scroll_learning(it, 3, 2) ||
                            item_scroll_learning(it, 3, 11))) {
            item_found = it;
            break;
        }
    }

    level_boost_item = item_found != NULL && item_scroll_learning(item_found, 3, 11);
    flat_bonus_item  = item_found != NULL && item_scroll_learning(item_found, 3, 2);

    thief_lvl = player_skill_level(player, SKILL_THIEF);

    /* Below level 4 the flat bonus is spent bringing the character up to level 4
     * instead of being added, which is worth more than ten per cent. */
    if (thief_lvl < 4 && flat_bonus_item) {
        thief_lvl = 4;
        flat_bonus_item = false;
    }

    orig_thief_lvl = thief_lvl;

    race = player->race;
    dex  = player->stats.value[PSTAT_DEX].full;

    for (int skill = 1; skill <= 8; skill++) {
        int base;
        int race_adj;
        int value;

        /* The level-boosting item works on the first two skills only: it lifts
         * the level used to look them up, or adds 5 when the character is
         * already past the level it would have set.
         *
         * `extra` is not cleared between iterations, so whatever skill 2 left it
         * at is still being added at skills 3 to 8. That is the original's
         * behaviour and the item's 5 leaks across all eight skills because of
         * it. */
        if (level_boost_item) {
            switch (skill) {
            case 1:
                if (thief_lvl < 5) {
                    thief_lvl = 5;
                    extra = 0;
                } else {
                    extra = 5;
                }
                break;

            case 2:
                if (thief_lvl < 7) {
                    thief_lvl = 7;
                    extra = 0;
                } else {
                    extra = 5;
                }
                break;

            default:
                break;
            }
        }

        if (!race_valid(race) ||
            (size_t)race >= COAB_ARRAY_LEN(THIEF_RACE_ADJ) ||
            thief_lvl < 0 ||
            (size_t)thief_lvl >= COAB_ARRAY_LEN(THIEF_BASE_CHANCE)) {
            log_warn("thief skills: no entry for race %d level %d", race,
                     thief_lvl);
            player->thief_skills[skill - 1] = 0;
            thief_lvl = orig_thief_lvl;
            continue;
        }

        base     = THIEF_BASE_CHANCE[thief_lvl][skill];
        race_adj = THIEF_RACE_ADJ[race][skill];

        /* A racial penalty bigger than the base chance would wrap the byte, so
         * the skill is simply zero instead. */
        if (race_adj < 0 && base < (-race_adj) + extra) {
            player->thief_skills[skill - 1] = 0;
        } else {
            value = extra + base + race_adj;
            player->thief_skills[skill - 1] = (u8)value;

            /* Dexterity only touches the first five. */
            if (skill < 6) {
                if (dex < 0 || (size_t)dex >= COAB_ARRAY_LEN(THIEF_DEX_ADJ)) {
                    log_warn("thief skills: dexterity %d past the table", dex);
                } else {
                    player->thief_skills[skill - 1] =
                        (u8)(player->thief_skills[skill - 1] +
                             THIEF_DEX_ADJ[dex][skill]);
                }
            }
        }

        if (flat_bonus_item) {
            player->thief_skills[skill - 1] =
                (u8)(player->thief_skills[skill - 1] + 10);
        }

        thief_lvl = orig_thief_lvl;
    }
}

/* ---------------------------------------------------------- class bonuses */

void classcalc_class_bonuses(Player *player)
{
    if (player == NULL) {
        return;
    }

    player->thac0 = 0;

    for (int cls = 0; cls <= 7; cls++) {
        int class_lvl = player->class_level[cls];

        if (class_lvl > CLASSCALC_MAX_TABLE_LEVEL) {
            log_warn("class bonuses: class %d level %d past the THAC0 table",
                     cls, class_lvl);
        } else if (classcalc_thac0_table[cls][class_lvl] > player->thac0) {
            player->thac0 = classcalc_thac0_table[cls][class_lvl];
        }

        if (class_lvl > player->hit_dice) {
            player->hit_dice = (u8)class_lvl;
        }
    }

    /* Three half-attacks, i.e. three attacks every two rounds. */
    if (player->class_level[SKILL_FIGHTER] >= 7 ||
        player->class_level[SKILL_PALADIN] >= 7 ||
        player->class_level[SKILL_RANGER]  >= 8) {
        player->attacks_count = 3;
    }

    classcalc_spell_cast_counts(player);
    classcalc_saving_throws(player);

    if (player->class_level[SKILL_THIEF] > 0) {
        classcalc_thief_skills(player);
    }

    /* An abandoned class still counts towards what the character may use, but
     * only while the new class has not out-levelled it - once it has, the old
     * levels are being added to the new ones by SkillLevel instead. */
    player->class_flags = 0;

    for (int skill = 0; skill <= 7; skill++) {
        if (player->class_level[skill] > 0 ||
            (player->class_level_old[skill] > 0 &&
             player->class_level_old[skill] < player->hit_dice)) {
            player->class_flags += classcalc_class_flag_bits[skill];
        }
    }

    player_undready_all(player, player->class_flags);

    if (player_dual_class_exceeded(player)) {
        /* The old class's levels now count too, so its THAC0 row and its
         * attacks-per-round threshold are folded in. The thresholds here are
         * one lower than the ones above - 7 rather than 8 for the ranger, and
         * "more than 6" for the fighter and paladin - because these test the
         * stored old level while the ones above test the current one. */
        for (int cls = 0; cls <= 7; cls++) {
            int skill_lvl = player->class_level_old[cls];

            if (cls == SKILL_FIGHTER || cls == SKILL_PALADIN) {
                if (skill_lvl > 6) {
                    player->attacks_count = 3;
                }
            } else if (cls == SKILL_RANGER) {
                if (skill_lvl > 7) {
                    player->attacks_count = 3;
                }
            }

            if (skill_lvl > CLASSCALC_MAX_TABLE_LEVEL) {
                log_warn("class bonuses: old class %d level %d past the THAC0 "
                         "table", cls, skill_lvl);
            } else if (classcalc_thac0_table[cls][skill_lvl] > player->thac0) {
                player->thac0 = classcalc_thac0_table[cls][skill_lvl];
            }
        }

        /* The same three tests again, from the named fields rather than the
         * loop. Redundant as it stands, and harmless. */
        if (player->class_level_old[SKILL_FIGHTER] > 6 ||
            player->class_level_old[SKILL_RANGER]  > 7 ||
            player->class_level_old[SKILL_PALADIN] > 6) {
            player->attacks_count = 3;
        }

        if (player->class_level_old[SKILL_THIEF] > 0) {
            classcalc_thief_skills(player);
        }
    }
}

/* ------------------------------------------------------------- dual classing */

/* ovr026.HumanCurrentClass_Unknown, getFirstSkill. Which class a human is in
 * now; CLASS_UNKNOWN for anything else, and for a human with no class levels
 * at all. */
static ClassId human_current_class(const Player *player)
{
    if (player->race != RACE_HUMAN) {
        return CLASS_UNKNOWN;
    }

    for (int index = CLASS_CLERIC; index <= CLASS_MONK; index++) {
        if (player->class_level[index] > 0) {
            return (ClassId)index;
        }
    }

    return CLASS_UNKNOWN;
}

bool classcalc_second_class_allowed(ClassId cls, const Player *player)
{
    ClassId first_class;
    bool    allowed;
    int     stat;
    int     align_index;

    if (player == NULL) {
        return false;
    }

    first_class = human_current_class(player);
    allowed = cls != first_class;

    /* Every stat the current class demands 9 or more of must now be 15 or more.
     * The loop runs until a stat fails, so "got to the end" is the pass. */
    stat = 0;
    while (stat <= 5 &&
           (limits_class_stat_min(first_class, stat) < 9 ||
            player->stats.value[stat].cur > 14)) {
        stat++;
    }
    allowed = allowed && stat > 5;

    /* And 17 or more of everything the new class demands 9 of. */
    stat = 0;
    while (stat <= 5 &&
           (limits_class_stat_min(cls, stat) < 9 ||
            player->stats.value[stat].cur > 16)) {
        stat++;
    }
    allowed = allowed && stat > 5;

    /* Column 0 of the alignment row is how many of the columns after it are
     * used, so the walk stops of its own accord and never reads past them. */
    align_index = 1;

    if (!class_valid((int)cls)) {
        log_warn("second class: class %d has no alignment row", (int)cls);
        return false;
    }

    while (limits_class_alignments[cls][0] >= align_index &&
           limits_class_alignments[cls][align_index] != player->alignment) {
        align_index++;
    }

    if (!allowed || limits_class_alignments[cls][0] < align_index) {
        return false;
    }

    return true;
}

void classcalc_duel_class(Player *player)
{
    MenuList  list;
    MenuItem *chosen = NULL;
    const RaceClasses *race_classes;
    char      line[80];
    int       index       = 1;
    bool      redraw      = true;
    bool      show_exit   = true;
    char      input_key;
    int       new_class;
    ClassId   old_class;

    if (player == NULL) {
        return;
    }

    menu_list_clear(&list);
    menu_list_add_heading(&list, "Pick New Class");

    if (!race_valid(player->race)) {
        log_warn("dual class: race %d has no class list", player->race);
        return;
    }

    race_classes = &limits_race_classes[player->race];

    for (int i = 0; i < race_classes->count; i++) {
        ClassId cls = race_classes->cls[i];

        if (classcalc_second_class_allowed(cls, player)) {
            menu_list_add(&list, player_class_name((int)cls));
        }
    }

    if (list.count == 1) {
        snprintf(line, sizeof(line), "%s doesn't qualify.", player->name);
        text_display_status(15, 4, line);
        return;
    }

    /* 'S' is Select; anything else keeps the list up, and Escape or Exit backs
     * out with the character unchanged. */
    do {
        input_key = prompt_select_item(&chosen, &index, &redraw, show_exit,
                                      &list, 0x16, 0x26, 2, 1,
                                      GBL_DEFAULT_MENU_COLORS, "Select", "");

        if (input_key == 0) {
            return;
        }
    } while (input_key != 'S');

    if (chosen == NULL) {
        return;
    }

    player->exp = 0;
    player->attacks_count = 2;

    new_class = 0;
    while (new_class <= 7 &&
           strcmp(player_class_name(new_class), chosen->text) != 0) {
        new_class++;
    }

    if (new_class > 7) {
        /* Only the six human classes can be on the list and all of them are in
         * range, so this cannot happen; the C# would have indexed off the end of
         * ClassLevel here. */
        log_warn("dual class: no class named \"%s\"", chosen->text);
        return;
    }

    old_class = human_current_class(player);

    if (!skill_valid((int)old_class)) {
        log_warn("dual class: %s has no class to leave", player->name);
        return;
    }

    /* The old class's level is filed away, and multiclass_level records the
     * level the character reached in it: SkillLevel starts counting the old
     * class again once the new one passes that. */
    player->class_level_old[old_class] = (u8)player_dual_class_current_level(player);

    player->multiclass_level = player->hit_dice;
    player->hit_dice = 1;

    player->class_level[old_class] = 0;
    player->class_level[new_class] = 1;

    for (int i = 0; i < 5; i++) {
        player->spell_cast_count[0][i] = 0;
        player->spell_cast_count[1][i] = 0;
        player->spell_cast_count[2][i] = 0;
    }

    if (new_class == CLASS_CLERIC) {
        player->spell_cast_count[0][0] = 1;
    } else if (new_class == CLASS_MAGIC_USER) {
        /* A new magic-user starts with the three spells character generation
         * hands out. */
        player->spell_cast_count[2][0] = 1;
        player_learn_spell(player, SPELL_DETECT_MAGIC_MU);
        player_learn_spell(player, SPELL_READ_MAGIC);
        player_learn_spell(player, SPELL_SLEEP);
    }

    player->cls = new_class;

    snprintf(line, sizeof(line), "%s is now a 1st level %s.", player->name,
             player_class_name(new_class));
    text_display_status(0, 10, line);

    /* Everything memorised in the old class is gone. */
    spell_list_clear(&player->spell_list);

    classcalc_class_bonuses(player);
    classcalc_cleric_spells(true, player);
    classcalc_saving_throws(player);
    /* Unconditional, so a character who has just dual-classed into something
     * other than a thief still gets a thief skill row computed - at thief level
     * 0, which is a row of zeroes plus whatever dexterity adds. */
    classcalc_thief_skills(player);

    /* class_bonuses has already unreadied what the new class may not use
     * through the ready slots; this does the same walk over the pack itself, so
     * an item that was readied but not in a slot is caught too. */
    for (int i = 0; i < player->item_count; i++) {
        Item *it = &player->items[i];

        if ((item_data(it->type)->class_flags & player->class_flags) == 0 &&
            !it->cursed) {
            it->readied = false;
        }
    }
}
