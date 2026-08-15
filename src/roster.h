/* roster.h - who owns the Player records.
 *
 * The C# owned nothing: `new Player()` plus the collector meant gbl.TeamList
 * could hold references and no overlay had to think about lifetime. Here
 * gbl.team_list holds borrowed pointers, so the records need an owner that
 * outlives every overlay touching them - the party survives fights, camps and
 * script changes, and a monster an ECL instruction loaded is still there many
 * instructions later.
 *
 * This is that owner, and it is a fixed pool rather than one malloc per
 * character because that is what the DOS build did too: records came out of a
 * heap it never grew, so exhausting the pool is the original's own
 * out-of-memory case and is logged rather than fatal. The pool is sized like the
 * team list, that being the most characters than can exist at once.
 */
#ifndef COAB_ROSTER_H
#define COAB_ROSTER_H

#include "coab.h"
#include "gbl.h"
#include "player.h"

#define ROSTER_MAX GBL_TEAM_LIST_MAX

/* `new Player()`: a zeroed character with empty lists and nothing readied, or
 * NULL when the pool is full. */
Player *roster_alloc(void);

/* Classes/Player.cs: ShallowClone - MemberwiseClone, then a copy of the stats.
 * Assigning the struct is already both, every field of a Player being embedded
 * except `actions`; the C# copied that reference as well, so the clone shares
 * the original's combat action record here exactly as it did there. NULL when
 * the pool is full or src is NULL. */
Player *roster_clone(const Player *src);

/* Hands the slot back. Emptying the character out first is the caller's job -
 * partymenu_free_player does it - since this only marks the slot free. A pointer
 * from somewhere else is logged and ignored. */
void roster_release(Player *player);

bool roster_owns(const Player *player);

/* Frees every slot: a new game starts with nobody in it. */
void roster_clear(void);

/* How many characters exist, for the log and the self-test. */
int roster_in_use(void);

#endif /* COAB_ROSTER_H */
