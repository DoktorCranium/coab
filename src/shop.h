/* shop.h - the city shops.
 * Ported from engine/ovr007.cs.
 *
 * A shop is an encounter with no monsters in it. The script sets
 * gbl.area2_ptr->enter_shop and runs COMBAT; the interpreter sees an empty
 * battlefield and calls shop_city_shop instead of handing out treasure.
 *
 * The stock is the ground. A shop's wares are the same gbl.ground_items a fight
 * leaves behind, put there by the script, which is why buying and looting share
 * shop_player_add_item and why walking out with money still pooled gets the
 * shopkeeper's attention.
 *
 * Prices come off gbl.area2_ptr->field_6DA, a band the script sets per shop: a
 * halving or a doubling of every item's face value, from a sixteenth up to eight
 * times. Nothing here sells anything - a shop only buys through
 * treasure_appraise_gems_jewels, and only gems and jewelry.
 */
#ifndef COAB_SHOP_H
#define COAB_SHOP_H

#include "coab.h"

struct Item;

/* ovr007.PlayerAddItem. Puts a copy of the item in the selected character's
 * pack and rebuilds their totals. True means it would not fit, in which case
 * nothing was added and "Overloaded" has been shown - the caller leaves the item
 * where it was. */
bool shop_player_add_item(struct Item *item);

/* ovr007.ShopChooseItem, sub_2F04E. The stock as a scrolling list, one line per
 * item with its price in this shop right-aligned beside it. *index is the entry
 * the highlight starts on and is left on the one it ended on; *out_item receives
 * the chosen item, or NULL. The answer is the key that picked it, '\0' for
 * Escape or Exit. */
char shop_choose_item(int *index, struct Item **out_item);

/* ovr007.shop_buy, sub_2F474. Buy from the list until the player backs out.
 * A character's own purse is spent first and the party's pool second. */
void shop_buy(void);

/* ovr007.CityShop, sub_2F6E7. The shop menu, which is where a shop is entered
 * and left. */
void shop_city_shop(void);

#endif /* COAB_SHOP_H */
