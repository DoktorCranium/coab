/* viewplayer.h - the character sheet: what a character is, what they carry, and
 * everything they can do with it.
 * Ported from engine/ovr020.cs.
 *
 * viewplayer_view_player is the door everything else comes through: the party
 * menu, the camp, the shops and the combat menu all put a character up with it,
 * and it stays up until the player leaves or spends the character's turn. Under
 * it sit the pack (viewplayer_items_menu, which readies, uses, trades, drops,
 * halves, joins, sells and identifies), the coin (viewplayer_trade_coin and
 * viewplayer_drop_coin) and the spell list (viewplayer_spell_menu2).
 *
 * The routines that spend the character's turn take a bool * rather than the
 * C#'s `ref bool arg_0`. It is set when the turn has been used up - a spell cast
 * out of an item or off a scroll in a fight - and the menus above unwind on it,
 * back to the combat loop.
 *
 * What is not translated yet: engine/ovr023.cs's BuildSpellList, spell_menu and
 * sub_5D2E1, which are the spell list and the casting behind it. Each says so
 * once in the log and answers with "no spells" or "nothing was cast"; the three
 * are all one overlay and land together.
 *
 * ovr020.classString and ovr020.statusString are not here. player.c has them as
 * player_class_name and player_health_status_name, because engine/ovr025.cs and
 * engine/ovr026.cs were ported first and needed them.
 */
#ifndef COAB_VIEWPLAYER_H
#define COAB_VIEWPLAYER_H

#include "coab.h"
#include "enums.h"
#include "item.h"
#include "player.h"

/* ovr020.sexString, raceString and alignmentString. "" for anything outside the
 * tables, where the C# would have thrown. */
const char *viewplayer_sex_name(int sex);
const char *viewplayer_race_name(int race);
const char *viewplayer_alignment_name(int alignment);

/* ovr020.playerDisplayFull. The whole character sheet on a bordered screen: who
 * they are, their six stats, their money, their levels and experience, then
 * viewplayer_display_stats01's combat figures, the weapon and armour they have
 * readied and how they are keeping. */
void viewplayer_display_full(Player *player);

/* ovr020.displayMoney. The coin column on the right of the sheet, largest coin
 * first, for gbl.selected_player. */
void viewplayer_display_money(void);

/* ovr020.display_player_stats01. Armour class, hit points, THAC0, damage,
 * encumbrance and movement. Recalculates the character first, so a stat that has
 * just changed is shown as it now stands.
 *
 * Movement is doubled while slowed and halved while hasted, which is the wrong
 * way round: player.movement is the initiative the fight uses, where a lower
 * number is faster, and this is the number of squares. The original had it this
 * way and the sheet has always shown it. */
void viewplayer_display_stats01(void);

/* ovr020.display_stat. One of the six stats, in white when the character
 * creation screen has the highlight on it and green otherwise. Strength 18 also
 * gets its percentile in brackets. */
void viewplayer_display_stat(bool highlighted, int stat_index);

/* ovr020.viewPlayer. The character sheet and its menu - Items Spells Trade Drop
 * Heal Cure Exit, each offered only when it applies - until the player leaves
 * with Exit or Escape. True means the character's turn was spent inside, which
 * only a spell out of an item in a fight can do. */
bool viewplayer_view_player(void);

/* ovr020.CanSellDropTradeItem, sub_54EC1. Whether the item can leave the pack: a
 * readied one cannot, and a scroll with a spell still to be scribed off it is
 * asked about first. */
bool viewplayer_can_sell_drop_trade_item(Item *item);

/* ovr020.ItemDisplayStats, sub_550A6. Every field of the item record on screen,
 * for the Cheats.view_item_stats debugging screen. */
void viewplayer_item_display_stats(const Item *item);

/* ovr020.PlayerItemsMenu, use_item. The pack: the scrolling list of what the
 * character carries and the menu over it. Runs until the player leaves, the pack
 * is empty, or the turn is spent. */
void viewplayer_items_menu(bool *out_turn_used);

/* ovr020.trade_item. Hands one item to another character, if they can carry it.
 */
void viewplayer_trade_item(Item *item);

/* ovr020.halve_items. Splits a stack in two, the remainder staying with the
 * original; a stack of one cannot be halved. */
void viewplayer_halve_items(Item *item);

/* ovr020.join_items, sub_56285. Gathers every other stack in the pack that is
 * the same thing into this one, up to 255 of them. */
void viewplayer_join_items(Item *item);

/* ovr020.UseMagicItem, sub_56478. Reads a spell off a scroll or sets off the one
 * inside a wand or a staff, then spends a charge of it. *out_turn_used is set
 * when that ended the character's turn. */
void viewplayer_use_magic_item(bool *out_turn_used, Item *item);

/* ovr020.ShopSellItem, sell_Item. The shopkeeper's offer - half the value, or a
 * twentieth of the stack for anything but arrows and quarrels - and the coin for
 * it, which goes to the pool when the character cannot carry it. */
void viewplayer_shop_sell_item(Item *item);

/* ovr020.IdentifyItem. Two hundred gold, out of the character's purse or the
 * pool, to have the item's name filled in. *out_identified is set when it
 * was. */
void viewplayer_identify_item(bool *out_identified, Item *item);

/* ovr020.tradeCoin. Hands coin to another character, one kind at a time, until
 * there is none left or the player backs out. */
void viewplayer_trade_coin(void);

/* ovr020.drop_coin. The same list, but the coin is left on the ground. */
void viewplayer_drop_coin(void);

/* ovr020.canCarry. True when the item would not fit: the pack is full, or the
 * weight would take the character past their limit and the 1500 the original
 * allowed over it. Recalculates the character first. */
bool viewplayer_can_carry(const Item *item, Player *player);

/* ovr020.scroll_team_list. Steps the selected character along the team list: 'O'
 * forwards, 'G' back, both wrapping round. */
void viewplayer_scroll_team_list(char input_key);

/* ovr020.spell_menu2. The spell list under a heading that says where the spells
 * are being read from. *out_has_spells is false when there were none to show, in
 * which case nothing is drawn and the answer is 0; otherwise the answer is
 * whatever ovr023.spell_menu picked. *index is the entry the list starts and
 * ends on. */
u8 viewplayer_spell_menu2(bool *out_has_spells, int *index, SpellSource source,
                          SpellLoc location);

/* ovr020.CanCastHeal, sub_575F0, and CanCastCureDiseases, sub_57655. A paladin
 * heals once a day and cures a set number of diseases; neither can be done in a
 * fight or by anyone who is not keeping well themselves. */
bool viewplayer_can_cast_heal(const Player *player);
bool viewplayer_can_cast_cure_diseases(const Player *player);

/* ovr020.PaladinHeal. Two hit points a level, to whoever is picked. */
void viewplayer_paladin_heal(Player *player);

/* ovr020.PaladinCureDisease, sub_577EC. Takes the six curable afflictions off
 * whoever is picked, asking first if they have none of them. */
void viewplayer_paladin_cure_disease(Player *player);

/* ovr020.calc_items_effects, sub_55B04. Applies or takes away what a magic item
 * does for the selected character: the low seven bits of affect_3 say which of
 * the fifteen effects it is - a plain affect, a ring of wizardry doubling the
 * low-level spell slots, a stat-changing item, a girdle, an ioun stone - and
 * affect_2 is that effect's argument.
 *
 * Called with the character already holding the item, so the item must be in the
 * pack and readied before this runs and still be there when it is taken off. */
void viewplayer_calc_items_effects(bool add_item, Item *item);

/* ovr020.ready_Item. Readies the item, or puts it away again if it was already
 * readied; a cursed item cannot be put away. A magic item's effect follows.
 *
 * The original worked out whether the character had a free hand, an empty slot
 * and the right class for the item - and then threw the answer away, so every
 * ready succeeds. That is preserved; see the note in the body. */
void viewplayer_ready_item(Item *item);

#endif /* COAB_VIEWPLAYER_H */
