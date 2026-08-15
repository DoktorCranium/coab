/* treasure.h - money: what a character can carry, pooling and sharing it,
 * dropping and picking it up, appraising gems - and the random items an
 * encounter's hoard is filled with.
 * Ported from engine/ovr022.cs.
 *
 * Coin weighs one unit each, which is why every routine here that moves money
 * also moves weight, and why the party's carrying capacity is this file's
 * business rather than the character sheet's. A character who cannot lift their
 * share of the pool leaves the rest in it.
 *
 * The two halves of the file have nothing to do with each other beyond the
 * hoard: engine/ovr003.cs rolls an encounter's treasure by calling
 * treasure_create_item once per item and dropping each on the ground.
 */
#ifndef COAB_TREASURE_H
#define COAB_TREASURE_H

#include "coab.h"
#include "enums.h"
#include "item.h"
#include "money.h"
#include "player.h"

/* --------------------------------------------------------------- carrying */

/* 1500 coins, plus or minus what the character's strength is worth. */
int treasure_max_load(const Player *player);

/* Whether another item_weight of coin would put the character over their limit.
 * *out_capacity receives how much they can still take - which is what the
 * callers load them up with before giving up - and 0 when they are not
 * overloaded at all. */
bool treasure_will_overload(int *out_capacity, int item_weight,
                            const Player *player);
bool treasure_will_overload_simple(int item_weight, const Player *player);

/* --------------------------------------------------------------- the money */

/* Gives the selected character platinum, and puts whatever they cannot lift into
 * the pool. This is how a sale is paid for. */
void treasure_add_player_gold(int item_weight);

/* sub_592AD. Asks for a number on the prompt row, digit by digit: backspace
 * rubs one out, Return accepts, Escape answers 0, and a value over max_value
 * snaps back to max_value. */
i16 treasure_ask_number_value(u8 fg_color, const char *prompt, int max_value);

/* add_object. Hands num_coins of one kind from source to dest, unless that would
 * overload dest - in which case nothing moves and "Overloaded" is shown. */
void treasure_trade_money(MoneyKind money_slot, i16 num_coins, Player *dest,
                          Player *source);

/* Empties every party member's purse into the pool. NPCs keep theirs. */
void treasure_pool_money(void);

/* sub_595FF. How many of the team are party members rather than NPCs or
 * monsters. */
int treasure_party_count(void);

/* Deals the pool out evenly, a share each and the remainder to whoever has room.
 * Anything still left over stays in the pool. */
void treasure_share_pooled(void);

/* sub_59A19. Takes coin off a character. It goes into the pool only after a
 * fight or in a shop; anywhere else it is simply gone, which is what "drop"
 * means on the road. */
void treasure_drop_coins(MoneyKind money_slot, int num_coins, Player *player);

/* sub_59AA0. Takes coin out of the pool, no more than is in it, if the character
 * can lift it. */
void treasure_pickup_coins(MoneyKind money_slot, int num_coins, Player *player);

/* sub_59BAB. Reads a coin kind off the front of a menu line - "Gold 250",
 * "Gems 3" - and writes the word to put in the prompt that follows into
 * display_text. Returns MONEY_KINDS for anything it does not recognise, which is
 * out of range on purpose: see the note in treasure.c. */
int treasure_money_index_from_string(char *display_text, size_t display_size,
                                     const char *input);

/* takeItems. The pool's coin as a menu, one line per kind, until the character
 * has taken the lot or backs out. */
void treasure_take_pool_money(void);

/* Whether there is anything on the ground here to pick up. */
void treasure_on_ground(bool *out_items, bool *out_money);

/* Sells or keeps the selected character's gems and jewelry a piece at a time,
 * rolling each one's value. False, having said so, when they have none. The
 * return value says whether the screen was overwritten and the caller has to
 * reload its picture. */
bool treasure_appraise_gems_jewels(void);

/* ---------------------------------------------------------------- the hoard */

/* sub_59FCF. The magical bonus a found weapon or piece of armour carries: +1
 * seven times out of ten, +2 the rest. Never 0, so every item this rolls for is
 * magical. */
i8 treasure_random_bonus(void);

/* sub_5A007. Fills in one item of the given type from nothing: its bonus, its
 * name parts, its weight and its worth. Scrolls get one to three random spells;
 * potions, wands, gauntlets, cloaks and a javelin of lightning are copied whole
 * out of a table of seven ready-made items.
 *
 * Anything that is not one of those - a gem, a key, a plot item - comes back
 * with only its type set, which is what the C# returned too. */
void treasure_create_item(Item *item, ItemType item_type);

#endif /* COAB_TREASURE_H */
