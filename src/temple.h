/* temple.h - the temples.
 * Ported from engine/ovr005.cs.
 *
 * A temple is a shop that sells one thing: getting better. It is entered the
 * same way - the script sets gbl.area2_ptr->enter_temple and runs COMBAT with no
 * monsters loaded - and the menu is the shop's with Buy replaced by Heal, so the
 * pooling, sharing and appraising all still work and walking out with money on
 * the floor still gets somebody's attention.
 *
 * Heal opens a list of ten services at fixed prices, from a hundred gold for
 * cure light wounds to five and a half thousand to raise the dead. Every one of
 * them goes through temple_buy_cure, which asks twice - once to show the price
 * and once for yes or no - and then takes the money from the character's own
 * purse before the party pool, exactly as the shop does.
 *
 * Two of the ten take the money whether or not there is anything to undo:
 * temple_raise_dead and temple_stone_to_flesh pay first and check the body
 * afterwards. Both are the original's, and both are commented where they happen.
 */
#ifndef COAB_TEMPLE_H
#define COAB_TEMPLE_H

#include "coab.h"

/* ovr005.buy_cure. Names the price, asks for it, and takes it. True means it was
 * paid and the caller should go ahead: the character's purse is tried first and
 * the party pool second, and "is cured." has already been shown. False means
 * either the player said no or neither purse nor pool could cover it.
 *
 * There is no way to back out of the yes/no - ovr027.yes_no keeps asking - so
 * the only refusal is N. */
bool temple_buy_cure(int cost, const char *cure_name);

/* The ten services, in the order the Heal list offers them. Each checks whether
 * there is anything to cure, offers to cast it anyway when there is not, and
 * then charges. All were `internal static` in the C# and none had a caller
 * outside the overlay. */
void temple_cure_blindness(void);   /* 1000 */
void temple_cure_disease(void);     /* 1000, and all six disease affects */
void temple_cure_wounds(int heal_type); /* 1: 100, 2: 350, 3: 600, 4: 5000 */
void temple_cure_poison2(void);     /* 1000, poison and the two that carry it */
void temple_raise_dead(void);       /* 5500 */
void temple_remove_curse(void);     /* 3500, through the spell itself */
void temple_stone_to_flesh(void);   /* 2000 */

/* ovr005.temple_heal. The list of services, which is left with Exit or Escape.
 * Redraws the picture and the party summary on the way out. */
void temple_heal(void);

/* ovr005.temple_shop. The temple menu, which is where a temple is entered and
 * left. The one entry point the interpreter uses. */
void temple_shop(void);

#endif /* COAB_TEMPLE_H */
