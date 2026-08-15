/* classcalc.h - everything a character's classes and levels imply.
 * Ported from engine/ovr026.cs.
 *
 * Nothing here is stored in the save file: THAC0, saving throws, thief skill
 * percentages, how many spells of each level may be memorised and which spells
 * the character is allowed to know are all worked out again from the class
 * levels whenever those can have changed - on load, on gaining a level, on
 * readying an item, on dual-classing. character.c does the same job for the
 * numbers that come out of the stats and the pack; this does the ones that come
 * out of the class table.
 *
 * Three tables belong to overlays that are not translated yet and live here
 * because this is their first caller, the same arrangement player.c already has
 * with ovr020's statusString:
 *
 *   classcalc_thac0_table      ovr018.thac0_table
 *   classcalc_class_flag_bits  ovr018.unk_1A1B2
 *   classcalc_mu_spell_lvl_learn   ovr020.MU_spell_lvl_learn
 *
 * ovr026.HumanCurrentClassLevel_Zero and ovr026.DualClassExceedLastLevel are not
 * here either: Player.SkillLevel needs both, so they are player.c's
 * player_dual_class_current_level and player_dual_class_exceeded.
 */
#ifndef COAB_CLASSCALC_H
#define COAB_CLASSCALC_H

#include "coab.h"
#include "enums.h"
#include "player.h"

/* The highest class level any of the tables below has a row for. The classes go
 * higher than this - a druid reaches 15 - so every lookup is bounds-checked, and
 * a level past the end is logged and skipped where the C# would have thrown. */
#define CLASSCALC_MAX_TABLE_LEVEL 12

/* ovr018.thac0_table, seg600:3E3A unk_1A14A. [SkillType][class level]. Despite
 * the name it is a bonus and not a target: it goes into Player.hit_bonus and is
 * added to the attack roll, so the bigger the better, and the caller takes the
 * *highest* row it can find. Level 0 reads 40 for most classes - 0x27 for the
 * fighter and the magic-user - so every character has at least 40, whatever
 * classes it has levels in. */
extern const i8 classcalc_thac0_table[SKILL_COUNT][CLASSCALC_MAX_TABLE_LEVEL + 1];

/* ovr018.unk_1A1B2, seg600:3EA2. One bit per SkillType, OR'd together into
 * Player.class_flags and matched against ItemData.class_flags to decide what the
 * character may use. Fighter and paladin share bit 0x40. */
extern const u8 classcalc_class_flag_bits[SKILL_COUNT];

/* ovr020.MU_spell_lvl_learn, seg600:44B6 unk_1A7C6. Row n is what a magic-user
 * gains on reaching level n + 2: one new spell slot of the marked level. */
#define CLASSCALC_MU_LEVEL_ROWS 11

extern const u8 classcalc_mu_spell_lvl_learn[CLASSCALC_MU_LEVEL_ROWS][5];

/* sub_6A00F. Rebuilds spell_cast_count - how many spells of each level the
 * character may hold memorised - from the class levels, and marks every spell
 * those slots make legal as known.
 *
 * spell_cast_count is [3][5]: row 0 cleric, row 1 druid, row 2 magic-user, five
 * spell levels each. A readied item carrying AFFECT_PROTECT_MAGIC doubles the
 * first three magic-user rows, once per such item. */
void classcalc_spell_cast_counts(Player *player);

/* sub_6A3C6, ReclacClassBonuses. The whole recalculation: THAC0, hit dice,
 * attack count, spell slots, saving throws, thief skills and class flags, then
 * unreadies anything the new class flags disallow. Called after a level gain, a
 * dual-class, a character load and a restoration. */
void classcalc_class_bonuses(Player *player);

/* sub_6A686. The wisdom bonus to a cleric's low-level spell slots. With
 * reset_spell_levels the slots are rebuilt from the cleric level first;
 * without it the bonus is added to whatever is already there, which is how
 * classcalc_spell_cast_counts uses it.
 *
 * Called on its own by the camp screen after wisdom has changed, and it is not
 * idempotent - see the note in the body. */
void classcalc_cleric_spells(bool reset_spell_levels, Player *player);

/* sub_6A7FB. The five saving throws: the best any of the character's classes
 * offers, plus a poison adjustment from constitution. */
void classcalc_saving_throws(Player *player);

/* sub_6AAEA. The eight thief percentages out of the thief level, the race and
 * dexterity. Safe to call for a character with no thief level - the party menu
 * does - and then answers mostly zeroes. */
void classcalc_thief_skills(Player *player);

/* sub_6AD3E. May this character take `cls` as a dual class? Wants a different
 * class, 15+ in everything the current class demands 9 of, 17+ in everything the
 * new one does, and an alignment the new class allows. */
bool classcalc_second_class_allowed(ClassId cls, const Player *player);

/* DuelClass. Offers the classes the character qualifies for, and on a choice
 * throws away its experience, files the old class level away in class_level_old
 * and starts the new one at level 1. Interactive: draws a menu and waits. */
void classcalc_duel_class(Player *player);

#endif /* COAB_CLASSCALC_H */
