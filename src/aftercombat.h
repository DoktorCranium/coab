/* aftercombat.h - what happens once the fighting stops.
 * Ported from engine/ovr006.cs.
 *
 * engine/ovr009.cs hands over here the moment one side is gone, and
 * aftercombat_exp_and_treasure is the whole of it as far as the rest of the
 * engine is concerned. In order it:
 *
 *   settles everybody's state    who fled, who is merely unconscious, whether
 *                                the party won at all, and who is now dead
 *   works out the experience     the monsters' worth, their coin and their
 *                                packs, split between the survivors
 *   turns the monsters loose     the other side and the NPCs leave the team list
 *   says how it went             "The party has won." and the experience earned
 *   hands out the treasure       the View Take Pool Share Detect Exit screen
 *
 * The two halves of that are worth keeping apart when reading this: the state
 * settling is aftercombat_cleanup_players_state, which decides the three flags -
 * gbl.party_killed, gbl.party_fled and gbl.battle_won - that everything after it
 * reads, and the treasure screen is aftercombat_distribute_combat_treasure,
 * which is an ordinary menu loop over what the fight left lying on the ground.
 *
 * A dead monster's pack is copied onto the ground before the monster is freed,
 * which is why gbl.ground_items holds copies of items rather than pointers to
 * them.
 *
 * The C#'s access levels are kept: everything internal there is declared here.
 * Two things ovr006 calls belong to overlays that are not translated yet, and
 * each says so once in the log and then does as little as it can - engine
 * /ovr007.cs's PlayerAddItem and engine/ovr023.cs's sub_5D2E1. What each of them
 * does instead is described where it stands in aftercombat.c.
 */
#ifndef COAB_AFTERCOMBAT_H
#define COAB_AFTERCOMBAT_H

#include "coab.h"
#include "item.h"
#include "player.h"

/* ovr006.AfterCombatExpAndTreasure, sub_2E7A2. The whole of after-combat, and
 * the only thing outside this file that anything else calls. Leaves
 * gbl.game_state on AfterCombat. */
void aftercombat_exp_and_treasure(void);

/* ovr006.calc_battle_exp. What the fight was worth to one character: every
 * fallen enemy's own worth, their coin and the magic among their belongings,
 * divided between the party members who are still standing. A duel is worth a
 * flat hundred per hit die instead.
 *
 * This is also where the dead monsters' packs are emptied onto the ground and
 * their purses into the pool, so it is called once and its answer kept in
 * gbl.exp_to_add. */
int aftercombat_calc_battle_exp(void);

/* ovr006.addExp. Gives everyone still in the fight their share, with a tenth
 * again for the ones whose class-defining stat is over 15 and a half or a third
 * of it for the multi-classed. */
void aftercombat_add_exp(int exp_to_add);

/* ovr006.CleanupPlayersStateAfterCombat, sub_2D556. Works out how the fight
 * ended - gbl.party_killed, gbl.party_fled and gbl.battle_won - takes the
 * combat-only affects off everybody, awards the experience, and settles what
 * became of the fallen: the dying go unconscious, the unconscious with hit
 * points left get up, and a party that ran leaves the ones who did not behind. */
void aftercombat_cleanup_players_state(void);

/* ovr006.displayCombatResults, sub_2DABC. The one line saying how it went and
 * the experience each character earned, then "press <enter>/<return> to
 * continue". A party that fled forfeits the treasure here. */
void aftercombat_display_combat_results(int exp);

/* ovr006.select_treasure, sub_2DD2B. The scrolling list of what is on the
 * ground, newest first. *index is the entry the highlight starts and ends on,
 * *out_item is what was picked - NULL if nothing - and *out_key is the key that
 * picked it, 'T' or return being the two that mean "take it". */
void aftercombat_select_treasure(int *index, Item **out_item, char *out_key);

/* ovr006.take_items_treasure, sub_2DDFC. The list, over and over, until the
 * ground is bare or the player stops taking things off it. */
void aftercombat_take_items_treasure(void);

/* ovr006.take_treasure, sub_2DF2E. "Money Items Exit" when the ground holds
 * both; with only one of them there is nothing to ask and that one is taken
 * straight away. Both flags are re-read from the ground as it empties. */
void aftercombat_take_treasure(bool *items_present, bool *money_present);

/* ovr006.distributeCombatTreasure, sub_2E0C3. The treasure screen: view a
 * character, take, pool, share, detect magic on the heap, and leave - which asks
 * again if anything is still lying there. */
void aftercombat_distribute_combat_treasure(void);

/* ovr006.DeallocateNonTeamMemebers (the original's spelling), sub_2E3C7. Empties
 * the team list of everybody who was not in the party - the other side, and the
 * NPCs a script attached - and takes the action records away from those who
 * remain, a character out of a fight having none. */
void aftercombat_deallocate_non_team_members(void);

/* ovr006.distributeNpcTreasure, sub_2E50E. The NPCs' cut of the pool, and the
 * line about each of them hiding it. See the note in aftercombat.c: what is left
 * behind is worked out with integer division, so unless the whole party is NPCs
 * the pool keeps none of its coin and the NPCs have taken all of it. */
void aftercombat_distribute_npc_treasure(void);

#endif /* COAB_AFTERCOMBAT_H */
