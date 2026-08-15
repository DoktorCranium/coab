/* combatloop.h - a fight, from the first round to the last.
 * Ported from engine/ovr009.cs.
 *
 * combatloop_main_combat_loop is the whole of combat as far as the rest of the
 * engine is concerned: engine/seg001.cs and the ECL scripts call it, it sets the
 * battle up, runs round after round until one side is gone, and tidies up after
 * itself. What it does with a round is:
 *
 *   count both sides           ovr025.CountCombatTeamMembers
 *   roll initiative            one delay per combatant, which is the turn order
 *   take turns                 the highest delay left goes next, until none is
 *   left, then the round's own checks - poison, bleeding, the round limit
 *
 * A turn is either decided by the player, through combatloop_combat_menu, or by
 * engine/ovr010.cs when the combatant is a monster or is on quick fight. The
 * menu is the "Move View Aim Use Cast Turn Quick Done" prompt at the bottom of
 * the combat screen, and the movement half of it - the cursor keys, and walking
 * into somebody to attack them - is combatloop_move_menu.
 *
 * The C#'s access levels are kept: everything internal there is declared here,
 * and the two Sets of accepted keys stay inside combatloop.c.
 *
 * Two things ovr009 calls belong to an overlay that is not translated yet, and
 * each says so once in the log and then does nothing, so a fight can still be
 * fought without them: engine/ovr023.cs's sub_5D2E1 and NonCombatSpellCast, which
 * resolve a spell.
 */
#ifndef COAB_COMBATLOOP_H
#define COAB_COMBATLOOP_H

#include "coab.h"
#include "player.h"

/* ovr009.MainCombatLoop, sub_33100. The whole fight. Returns once one side is
 * gone, the round limit is reached, or the party has run - with the combat state
 * torn down and gbl.game_state left on Combat for engine/ovr006.cs, which is
 * what runs next. */
void combatloop_main_combat_loop(void);

/* ovr009.free_combat_stuff, sub_3304B. Drops everything the fight owned: the two
 * gas clouds, the ground map, the missile sprite, the inverted palette and the
 * combat way of aiming a spell. */
void combatloop_free_combat_stuff(void);

/* ovr009.DoPlayerCombatTurn, sub_33281. One combatant's turn: the display is
 * brought round to them, their values are worked out again, and then either the
 * player or the AI decides what they do. A combatant whose delay has run out
 * does nothing at all. */
void combatloop_do_player_combat_turn(Player *player);

/* ovr009.combat_menu. The turn of somebody the player is deciding for. A spell
 * begun last round goes off instead, and somebody who is out of the fight has
 * their turn cleared. Returns when the turn has been spent. */
void combatloop_combat_menu(Player *player);

/* ovr009.BattleRoundChecks, battle01. The end of a round: time moves on, the
 * clouds and the poison get their say, the dying bleed a little closer to dead,
 * and both sides are counted again. Returns whether the fight is over - which
 * the player may overrule while their own side is still standing. */
bool combatloop_battle_round_checks(void);

/* ovr009.sub_33B26. The Move half of the combat menu: step by step in whatever
 * direction is asked for until the movement is gone or the player stops. Walking
 * into a combatant attacks them, walking off the map is offered as fleeing, and
 * Escape puts the combatant back where the move started.
 *
 * first_key is the direction to take at once - ' ' asks for one instead - and
 * *turn_ended reports that the move used the whole turn up. */
void combatloop_move_menu(bool *turn_ended, char first_key, Player *player);

/* ovr009.sub_33F03. Walking into somebody: the attack that comes of it, if the
 * readied weapon can be swung at all and the target can be reached. */
void combatloop_move_into_target(bool *turn_ended, Player *target,
                                 Player *player);

/* ovr009.delay_menu. The Done half of the menu: "Guard Delay Quit Bandage Speed
 * Exit". Everything but Speed and Exit ends the turn. */
void combatloop_delay_menu(bool *turn_ended, Player *player);

/* ovr009.set_gamespeed. The Slower/Faster prompt, which is the only thing that
 * moves gbl.game_speed_var. */
void combatloop_set_gamespeed(void);

/* ovr009.SetPlayerQuickFight, sub_3432F. Hands the combatant over to the AI, and
 * forgets a target on its own side so it does not open by attacking a friend. */
void combatloop_set_player_quick_fight(Player *player);

#endif /* COAB_COMBATLOOP_H */
