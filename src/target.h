/* target.h - reach, line of sight and target ordering.
 * Ported from engine/ovr032.cs.
 *
 * Three questions, all asked of the combat map:
 *
 *   can a square be reached from another one, and if not, where does the path
 *   stop  (target_can_reach / target_can_reach_range)
 *
 *   can a combatant facing one way see another one  (target_can_see)
 *
 *   which combatants can be attacked from here, nearest first
 *   (target_sorted_combatants)
 *
 * The reach walk needs the ground map, so all of this is only meaningful during
 * a fight; with no ground map every square reads as empty floor, which is what
 * the C# indexer did outside a fight.
 */
#ifndef COAB_TARGET_H
#define COAB_TARGET_H

#include "coab.h"
#include "combat.h"
#include "player.h"
#include "point.h"

/* ovr032.canReachTarget(ref Point, Point). Moves target back to where the path
 * from attacker was blocked, or leaves it alone if the path got through. */
void target_can_reach(Point *target, Point attacker);

/* ovr032.canReachTarget(ref int, Point, Point). Whether target can be reached
 * from attacker within `range`, which is counted in half-steps so that a
 * diagonal can cost one and a half. On a hit *range becomes what the path
 * actually cost; a path longer than the allowance leaves it alone. */
bool target_can_reach_range(int *range, Point target, Point attacker);

/* ovr032.CanSeeCombatant (sub_7354A). Whether a combatant standing at player_b
 * and facing `direction` can see one at player_a: the field of view is the
 * quadrant, or octant for a diagonal, in front of the square being faced.
 * Direction 8 and the no-direction 0xff see everything. */
bool target_can_see(int direction, Point player_a, Point player_b);

/* ovr032.FindCombatantDirection. The lowest direction attacker could face and
 * still see target - and 8, the no-direction, when none of them can. */
u8 target_find_combatant_direction(Point target, Point attacker);

/* The Predicate<Player> the C# passed in: which combatants are of interest.
 * ctx carries whatever the caller would have closed over. */
typedef bool (*TargetFilter)(const Player *player, void *ctx);

/* ovr032.Rebuild_SortedCombatantList (sub_738D8). Every combatant the filter
 * accepts that can be reached from a combatant of `size` standing at pos,
 * written into out in attack order - nearest first - and returning how many.
 * At most GBL_MAX_COMBATANT_COUNT are found, so an array that size always fits.
 *
 * Reach is measured between every square the attacker covers and every square
 * the target covers, and the cheapest pair wins; the direction reported is the
 * one that pair needs. */
int target_sorted_combatants(SortedCombatant *out, int out_size, int size,
                             int max_range, Point pos, TargetFilter filter,
                             void *ctx);
int target_sorted_combatants_for(SortedCombatant *out, int out_size,
                                 const Player *player, int max_range,
                                 TargetFilter filter, void *ctx);

#endif /* COAB_TARGET_H */
