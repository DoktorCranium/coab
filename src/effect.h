/* effect.h - what happens to a character: dice, saving throws, damage, healing,
 * death, and the affects that are hung on them and taken off again.
 * Ported from engine/ovr024.cs.
 *
 * affect.c is the record - one timed effect and the list a character carries -
 * and this is what a record does to its owner. The handler that gives an affect
 * its own behaviour is a third thing again, engine/ovr013.cs's jump table, which
 * is reached through affecttab.h.
 *
 * Almost everything here talks to the caller through gbl rather than a return
 * value - gbl.attack_roll, gbl.savingThrowRoll, gbl.damage - because the DOS
 * original did, and the affect handlers read and rewrite those fields behind the
 * caller's back. That is the point of them: an affect adjusts a roll that has
 * already been made.
 */
#ifndef COAB_EFFECT_H
#define COAB_EFFECT_H

#include "coab.h"
#include "affect.h"
#include "enums.h"
#include "player.h"

/* engine/ovr024.cs: enum CheckType. Which of a character's affects are asked
 * whether they have anything to say at this point in a round. The unnamed ones
 * are numbered as the C# left them: what a set has in common is often only that
 * the same call site asks for it. */
typedef enum {
    CHECK_TYPE_NONE              = 0,
    CHECK_TYPE_VISIBILITY        = 1,
    CHECK_TYPE_2                 = 2,
    CHECK_TYPE_3                 = 3,
    CHECK_TYPE_SPECIAL_ATTACKS   = 4,
    CHECK_TYPE_5                 = 5,
    CHECK_TYPE_PRE_DAMAGE        = 6,
    CHECK_TYPE_PLAYER_RESTRAINED = 7,
    CHECK_TYPE_8                 = 8,
    CHECK_TYPE_MAGIC_RESISTANCE  = 9,
    CHECK_TYPE_10                = 10,
    CHECK_TYPE_11                = 11,
    CHECK_TYPE_SAVING_THROW      = 12,
    CHECK_TYPE_DEATH             = 13,
    CHECK_TYPE_14                = 14,
    CHECK_TYPE_15                = 15,
    CHECK_TYPE_16                = 16,
    CHECK_TYPE_MORALE            = 17,
    CHECK_TYPE_MOVEMENT          = 18,
    CHECK_TYPE_19                = 19,
    CHECK_TYPE_FIRE_SHIELD       = 20,
    CHECK_TYPE_CONFUSION         = 21,
    CHECK_TYPE_22                = 22,
    CHECK_TYPE_23                = 23
} CheckType;

/* ------------------------------------------------------------------- dice */

/* One roll of `dice_count` dice of `dice_size` sides each, counting from 1. The
 * total comes back as a byte, so more than 255 points of damage wraps - which is
 * the original's arithmetic, and out of reach of anything the game rolls. */
u8 effect_roll_dice(int dice_size, int dice_count);

/* The same roll, leaving the number of dice in gbl.dice_count for the damage
 * code to read back. */
int effect_roll_dice_save(int dice_size, int dice_count);

/* --------------------------------------------------------- attack and save */

/* sub_641DD. A monster's swing at `target`: d20 against their armour class, with
 * a natural 1 always missing and a natural 20 always hitting. The roll is left in
 * gbl.attack_roll, where the affect handlers can still change it. */
bool effect_can_hit_target(int bonus, Player *target);

/* sub_64245. A character's swing, which counts their own to-hit bonus and their
 * side's, and takes their invisibility off first. */
bool effect_pc_can_hit_target(int target_ac, Player *target, Player *attacker);

/* do_saving_throw. d20 against the character's save against `save_type`, with a
 * natural 1 and a natural 20 settling it outright. The roll and the result are
 * also left in gbl.savingThrowRoll and gbl.savingThrowMade. */
bool effect_roll_saving_throw(int save_bonus, SaveVerseType save_type,
                              Player *player);

/* ---------------------------------------------------------------- affects */

/* Hangs a new affect on the character. call_affect_table asks for its jump-table
 * entry to be run when it is removed. */
void effect_add_affect(bool call_affect_table, int data, u16 minutes,
                       Affects type, Player *player);

/* Takes one off again, running its jump-table entry first if it asked for that.
 * With affect NULL the character's own affect of that type is looked up. */
void effect_remove_affect(Affect *affect, Affects affect_id, Player *player);

/* Asks one affect whether it has anything to say about `player` just now. Four
 * of them - silence, the two 10' protections and prayer - are cast over an area,
 * so a team member's copy counts if the character is standing close enough to
 * them. */
void effect_calc_affect(Affects affect_type, Player *player);

/* work_on_00. Asks every affect that has a say at this point in a round: see
 * CheckType. This is how an affect gets to change a roll, a damage total or a
 * character's visibility. */
void effect_check_affects(Player *player, CheckType type);

/* is_cured. Removes the affect and says "is Cured", or reports that the
 * character did not have it. */
bool effect_cure_affect(Affects affect_id, Player *player);

/* Every copy of invisibility, which is what attacking gives away. */
void effect_remove_invisibility(Player *player);

/* sub_645AB. The affects that only last as long as the fight does. Also puts a
 * berserk character back on our side. */
void effect_remove_combat_affects(Player *player);

/* sub_6460D. The four an attacker loses when their target goes down. */
void effect_remove_attackers_affects(Player *player);

/* is_unaffected. Applies an attack's affect to its target: nothing if the target
 * resisted it or saved against it outright, otherwise the affect replaces any
 * copy already running and `text` is shown over them. */
void effect_apply_attack_spell_affect(const char *text, bool saved,
                                      DamageOnSave can_save,
                                      bool call_affect_table, int data,
                                      u16 time, Affects affect_id,
                                      Player *target);

/* --------------------------------------------------------- damage and death */

/* Applies `damage` to the character, after their protections have had their say
 * and, on a save, the damage has been halved or cancelled. Announces what it
 * came to and, if that put them down, that they are down. */
void effect_damage_person(bool change_damage, DamageOnSave damage_on_save,
                          int damage, Player *player);

/* A character who takes damage while casting loses the spell. */
void effect_try_loose_spell(Player *player);

/* sub_63014. Kills the character outright, unless they are already dead, stoned
 * or gone. */
void effect_kill_player(const char *text, Status new_health_status,
                        Player *player);

/* sub_644A7. Takes the character out of the fight - fled, held, dead - and off
 * the combat map, with `msg` over them. */
void effect_remove_from_combat(const char *msg, Status health_status,
                               Player *player);

/* Heals up to the character's maximum. arg_0 of 0 lets an already-healthy
 * character be the target anyway, which is what "is fully healed" reports. */
bool effect_heal_player(u8 arg_0, int amount_healed, Player *player);

/* Puts a fallen combatant back on their feet with arg_0 hit points, if there is
 * still room on the map where they lie. */
bool effect_combat_heal(u8 arg_0, Player *player);

/* The stinking and poisonous clouds a combatant is standing in, checked at the
 * top of their turn. arg_0 of 0 skips the nausea and leaves the poison. */
void effect_in_poison_cloud(u8 arg_0, Player *player);

/* ------------------------------------------------------- strength and stats */

/* odd_math. Packs an exceptional strength into one byte: 18/xx becomes xx + 1,
 * and anything else its own value plus 100. */
int effect_encode_strength(int str_00, int str);

/* sub_646D9. Unpacks it again from an affect's data byte. */
void effect_decode_strength(int *str_00, int *str, const Affect *affect);

/* sub_64728. Packs it only if it is an improvement on what the character has. */
bool effect_try_encode_strength(int *encoded_str, int str_100, int str,
                               const Player *player);

/* sub_64771. Keeps the better of two strengths, 18/00 counting above 18/99. */
void effect_max_strength(int *str_a, int str_b, int *str_00_a, int str_00_b);

/* sub_647BE. The hit points a constitution of `cons` is worth over `class_lvl`
 * levels of a class. Only the fighting classes get anything above +2 a level,
 * and no class earns it past its last hit die. */
int effect_con_hit_point_bonus(int class_lvl, SkillType class_index, int cons,
                               Player *player);

/* sub_648D9. Recalculates one stat from the character's rolled value, the items
 * they have readied and the affects on them, and applies what falls out of it:
 * the hit points a constitution change is worth, the regeneration a very high
 * one gives, the strength an enlarge or a girdle grants. */
void effect_calc_stat_bonuses(Stat stat_index, Player *player);

#endif /* COAB_EFFECT_H */
