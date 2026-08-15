/* attack.h - striking a blow, and picking who to strike.
 * Ported from engine/ovr014.cs.
 *
 * This is the middle of a fight. A round is driven from engine/ovr009.cs, which
 * is not translated yet; what happens inside one turn is here:
 *
 *   attack_calculate_initiative   what a combatant may do this round at all
 *   attack_aim_menu               the Next/Prev/Manual/Target loop the player
 *                                 picks a target with
 *   attack_find_target            the same choice made by the monster AI, which
 *                                 rolls for it rather than asking
 *   attack_target                 the blow itself: turn to face, animate, swing
 *                                 as many times as there are attacks left
 *   attack_spell_targets          who a spell will touch, which for an area
 *                                 spell is everyone within its radius
 *
 * Almost everything here reaches for gbl rather than taking arguments, as the
 * original did: gbl.damage carries a blow's damage from the roll to the affects
 * that would reduce it, gbl.attack_hit_count and gbl.attack_made_count count the
 * round's swings, and gbl.spell_targets and gbl.target_pos carry a spell's aim to
 * the code that resolves it.
 *
 * Five of the affect table's handlers live here rather than in affecttab.c, for
 * the same reason they lived in ovr014: they are attacks. affecttab.c's table
 * names them and calls them through the same AffectHandler signature.
 *
 * One thing ovr014 calls is not translated yet and is named where it is called:
 * engine/ovr023.cs's sub_5D2E1, which resolves a spell. It logs once and does
 * nothing, so a fight can be fought without it and it is clear in the log what is
 * missing.
 */
#ifndef COAB_ATTACK_H
#define COAB_ATTACK_H

#include "coab.h"
#include "combat.h"
#include "enums.h"
#include "item.h"
#include "player.h"
#include "point.h"

/* ---------------------------------------------------- the top of a turn */

/* ovr014.CalculateInitiative, sub_3E000. Clears last round's decisions, works
 * out how many attacks and how much movement the combatant has, and rolls their
 * delay - which is what the turn order is sorted on. A side the encounter has
 * marked as surprised has six taken off its delay. */
void attack_calculate_initiative(Player *player);

/* ovr014.CalcMoves, sub_3E124. How far the combatant may move this round,
 * counted in half-steps so a diagonal can cost three; the affects that slow or
 * root a character have their say. Outside a fight the area's own bonus is added
 * on. */
int attack_calc_moves(Player *player);

/* ovr014.reclac_attacks, sub_3EDD4. How many times the combatant may swing this
 * round: their own count, or the readied missile weapon's rate of fire, capped by
 * how much ammunition is left. */
void attack_recalc_attacks(Player *player);

/* ovr014.ThisRoundActionCount, sub_3EF0D. Half-attacks and half-moves are
 * counted in halves so that three attacks every two rounds can be expressed:
 * this rounds them into whole ones, giving the odd round the extra half. */
int attack_this_round_action_count(int half_actions_left);

/* ---------------------------------------------------------------- moving */

/* ovr014.sub_3E748. One step in `direction`: pays for the ground, moves the
 * combatant, redraws, and lets anyone standing guard next to where they arrive
 * have a free swing at them. A step off the map is refused. */
void attack_move_step(int direction, Player *player);

/* ovr014.move_step_away_attack, sub_3E954. Everyone still in reach after a step
 * out of their reach gets a parting swing. Those who would still be next to the
 * combatant after the step do not - they get their chance next round. */
void attack_move_step_away(int direction, Player *player);

/* ovr014.flee_battle. Runs for it, which works if the combatant is faster than
 * the fastest thing near them - or, on a tie, on a coin toss. */
void attack_flee_battle(Player *player);

/* ovr014.MaxOppositionMoves, sub_40E8F. The best movement anyone on the other
 * side has this round, which is what fleeing is measured against. */
int attack_max_opposition_moves(Player *player);

/* ------------------------------------------------------------- targeting */

/* ovr014.CanSeeTargetA, sub_3F143. Whether the attacker may aim at the target at
 * all: the target's own affects get to hide them, and then the attacker's affects
 * get to say they cannot see. A NULL target cannot be seen; a combatant can
 * always see themselves. */
bool attack_can_see_target(Player *target, Player *attacker);

/* ovr014.getTargetDirection, sub_409BC. Which of the eight compass directions
 * player_b lies in from player_a. The sectors are cut with the fixed-point
 * gradients 0x26a/0x100 (about 67.5 degrees) and 0x6a/0x100 (about 22.5), so each
 * one is a true eighth of the circle. */
u8 attack_target_direction(const Player *player_b, const Player *player_a);

/* ovr014.RecalcAttacksReceived, sub_3F94D. Counts one more blow at the target and
 * turns them a little towards it: a character who has been spun round enough -
 * more than four eighths of a turn - can be attacked from behind. */
void attack_recalc_attacks_received(Player *target, Player *attacker);

/* ovr014.CanBackStabTarget, sub_408D7. A thief with a knife or a sword, behind a
 * man-sized target who has already been attacked this round. */
bool attack_can_backstab(Player *target, Player *attacker);

/* ovr014.RangedDefenseBonus, sub_3FCED. Two points of armour class at over a
 * third of the weapon's range and three more at over two thirds. Nothing for a
 * weapon that is not thrown or fired. */
int attack_ranged_defense_bonus(Player *target, Player *attacker);

/* ovr014.can_attack_target, sub_40F1F. Always yes against the other side, and
 * for a monster; against our own, the player is asked - and answering yes turns
 * every NPC in the party against the party for the rest of the fight. */
bool attack_can_attack_target(Player *target, Player *attacker);

/* ovr014.find_target, sub_41E44. The monster AI's target: a random one of the
 * reachable enemies, keeping whichever it already had if that is still worth
 * attacking. On a second pass walls are ignored, so a monster with nothing in
 * sight still has something to walk towards. */
bool attack_find_target(bool clear_target, u8 arg_2, int max_range,
                        Player *player);

/* ovr014.sub_421C1. find_target, and then whether that target can be reached at
 * all; *range comes back as the distance in half-steps. Returns true when there
 * is nothing to attack, which is the sense the caller wants. */
bool attack_no_reachable_target(bool clear_target, int *range, Player *player);

/* ovr014.FindLowestE9Target, sub_3F433. The weakest undead in reach - field_E9 is
 * how hard the thing is to turn - skipping anything already fleeing. */
bool attack_find_lowest_e9_target(Player **output, Player *player);

/* ovr014.find_healing_target, sub_3FDFE. Who a healing spell cast by the AI
 * should go to: the worst hurt team member standing next to the healer, or a
 * fallen one lying next to them, unless someone standing is below eight hit
 * points. */
bool attack_find_healing_target(Player **target, Player *healer);

/* ovr014.copy_sorted_players, sub_4188F. Every combatant, in the order the aim
 * menu steps through them: nearest to `player` first. Returns how many were
 * written, at most out_size. */
int attack_copy_sorted_players(SortedCombatant *out, int out_size,
                               Player *player);

/* ovr014.step_combat_list, sub_41932. Moves the aim cursor `step` entries along
 * the sorted list, wrapping at both ends, and draws the aiming line from where it
 * was to where it now is. With arg_2 false the previous square is put back
 * instead, which is how a target that cannot be seen is skipped over. */
Player *attack_step_combat_list(bool arg_2, int step, int *list_index,
                                Point *attacker_pos,
                                const SortedCombatant *sorted_list,
                                int sorted_count);

/* ovr014.aim_sub_menu, Aim_menu. Draws the range, points the summary panel at
 * the target and asks Next/Prev/Manual/Target/Center/Exit. The Target word only
 * appears when the target can actually be attacked or aimed at from here. */
char attack_aim_sub_menu(bool show_target, bool show_range, int max_range,
                         Player *target, Player *attacker);

/* ovr014.Target. The manual aim: moves a cursor a square at a time with the
 * cursor keys and reports whether something was picked. With show_range the pick
 * is an attack and is carried out; without it the caller is aiming a spell and
 * only wants the square. */
bool attack_target_cursor(DownedPlayerTile *out, bool allow_target,
                          bool can_target_empty_ground, bool show_range,
                          int max_range, Player *target, Player *player01);

/* ovr014.sub_411D8. Takes the pick from either aim mode: with show_range it is an
 * attack, which is either swept across several weak targets or struck at the one,
 * and the attacker's own ranged weapon is dropped from the swing when the target
 * has closed to touching distance. */
bool attack_commit_target(DownedPlayerTile *out, bool show_range, Player *target,
                          Player *attacker);

/* ovr014.aim_menu, sub_41B25. The whole aiming loop: steps through the
 * combatants, skipping the ones that cannot be seen, until one is picked or the
 * player backs out. `out` receives the target and the square. A max_range of -1
 * or 0xff means the readied weapon's own range. */
bool attack_aim_menu(DownedPlayerTile *out, bool allow_target,
                     bool can_target_empty_ground, bool show_range,
                     int max_range, Player *attacker);

/* --------------------------------------------------------------- attacking */

/* ovr014.AttackTarget, sub_3F9DB. One combatant's attack on another: both turn to
 * face, the missile flies if there is one, the blows are struck and what is left
 * of a thrown weapon is worked out. Returns whether the attacker's turn is over.
 *
 * attack_type 0 is a normal attack and turns the target to face the attacker; 1
 * and 2 are the free swings a guard or a hug get, which do not. */
bool attack_target(Item *ranged_weapon, int attack_type, Player *target,
                   Player *attacker);

/* ovr014.AttackTarget01, sub_3F4EB. The blows themselves, once the two are facing
 * each other: the target's armour class, the swings, the damage and the messages.
 * A held target is slain outright by the first blow. */
bool attack_deliver_blows(Item *item, int arg_8, Player *target,
                          Player *attacker);

/* ovr014.TrySweepAttack, sub_3EF3D. A high-level fighter cuts down several
 * zero-hit-dice targets at once. Returns whether the sweep happened, in which
 * case the attack has already been made. */
bool attack_try_sweep(Player *target, Player *attacker);

/* ovr014.turns_undead. The cleric's turning attempt: the weakest undead first,
 * each either fleeing or destroyed, until the d12 of turnings runs out or one of
 * them resists. */
void attack_turn_undead(Player *player);

/* ovr014.DrawRangedAttack, sub_40BF1. Loads the missile sprite the weapon throws
 * and flies it from the attacker to the target. */
void attack_draw_ranged(Item *item, Player *target, Player *attacker);

/* ovr014.LoadMissleIconAndDraw, sub_42159. The same for a monster's ray or breath,
 * which names its sprite by number rather than by weapon. */
void attack_load_missile_and_draw(int icon_id, Player *target, Player *attacker);

/* ovr014.calc_enemy_health_percentage, sub_40E00. Fills in
 * gbl.enemy_health_percentage. */
void attack_calc_enemy_health_percentage(void);

/* ovr014.god_intervene. The cheat that ends a fight: every enemy drops dead. */
bool attack_god_intervene(void);

/* ------------------------------------------------------------------ spells */

/* ovr014.sub_4001C. One target for the spell being cast: asked for with the aim
 * menu, or - in a quick fight - found by the AI. A spell that needs no target at
 * all goes to the caster, and a cure goes to whoever needs it most. */
bool attack_pick_spell_target(DownedPlayerTile *out, bool can_target_empty_ground,
                              QuickFight quick_fight, int spell_id);

/* ovr014.target. Everyone the spell will touch, into gbl.spell_targets, and the
 * square it is aimed at into gbl.target_pos. The spell's own target type decides
 * how: the caster alone, a fixed number of combatants, as many weak ones as its
 * hit-dice budget covers, or everyone within a radius. Returns false when the
 * casting was called off. */
bool attack_spell_targets(QuickFight quick_fight, int spell_id);

/* ovr014.spell_menu3. Picks a spell, if one was not named, and either casts it at
 * once or leaves the caster starting on it - a long spell goes off later in the
 * round, and a blow landing before then loses it. */
void attack_spell_menu(bool *casting_spell, QuickFight quick_fight,
                       int spell_id);

/* ---------------------------------------------- the affect table's attacks */

/* These five are affecttab.c's AffectHandler shape, and are what its table
 * calls for the affects whose meaning is an attack.
 *
 * ovr014.engulfs               AFFECT_39, a cloaker wrapping itself round its
 *                              target and holding them there
 * ovr014.attack_or_kill        AFFECT_57, the beholder's rays, one per round
 * ovr014.sub_425C6             AFFECT_8B, the round-by-round crush that being
 *                              engulfed costs
 * ovr014.AffectOwlbearHug*     AFFECT_60 and AFFECT_90, the owlbear's hug: the
 *                              check on a good attack roll, and the hug's own
 *                              attack each round afterwards
 */
void attack_affect_engulfs(Effect add_remove, void *param, Player *attacker);
void attack_affect_attack_or_kill(Effect add_remove, void *param,
                                  Player *attacker);
void attack_affect_engulf_round(Effect add_remove, void *param, Player *player);
void attack_affect_owlbear_hug_round(Effect add_remove, void *param,
                                     Player *player);
void attack_affect_owlbear_hug_check(Effect add_remove, void *param,
                                     Player *player);

#endif /* COAB_ATTACK_H */
