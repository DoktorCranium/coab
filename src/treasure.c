/* treasure.c - Ported from engine/ovr022.cs. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "treasure.h"
#include "character.h"
#include "effect.h"
#include "frames.h"
#include "gbl.h"
#include "input.h"
#include "log.h"
#include "menu.h"
#include "prompt.h"
#include "rnd.h"
#include "text.h"

/* --------------------------------------------------------------- carrying */

int treasure_max_load(const Player *player)
{
    return 1500 + character_max_encumberance(player);
}

bool treasure_will_overload(int *out_capacity, int item_weight,
                            const Player *player)
{
    if (player->weight + item_weight > treasure_max_load(player)) {
        /* What is left of the character's capacity. Negative for anyone already
         * over their limit, and the callers pass it straight on to AddCoins and
         * AddWeight, so an overloaded character loses money to a share-out
         * instead of gaining any. That is what the original did. */
        *out_capacity = treasure_max_load(player) - player->weight;
        return true;
    }

    *out_capacity = 0;

    return false;
}

bool treasure_will_overload_simple(int item_weight, const Player *player)
{
    int dummy;

    return treasure_will_overload(&dummy, item_weight, player);
}

/* --------------------------------------------------------------- the money */

void treasure_add_player_gold(int item_weight)
{
    Player *player = gbl.selected_player;
    int capacity;

    if (player == NULL) {
        log_warn("treasure: %d platinum for nobody in particular", item_weight);
        return;
    }

    if (treasure_will_overload(&capacity, item_weight, player)) {
        character_print_message("Overloaded. Money will be put in Pool.");

        money_add_coins(&player->money, MONEY_PLATINUM, capacity);
        player_add_weight(player, capacity);

        money_add_coins(&gbl.pooled_money, MONEY_PLATINUM,
                        item_weight - capacity);
    } else {
        money_add_coins(&player->money, MONEY_PLATINUM, item_weight);
        player_add_weight(player, item_weight);
    }
}

/* The digit string being typed. The C# grew a string, and a player holding down
 * '0' could grow it without bound: leading zeros never make the value exceed
 * max_value, so nothing ever clamps them. Here the buffer stops taking digits
 * instead, which is the same number with one fewer harmless zero in front of it.
 * Wide enough for any max_value the game asks about several times over. */
#define ASK_NUMBER_MAX 24

i16 treasure_ask_number_value(u8 fg_color, const char *prompt, int max_value)
{
    char max_value_str[ASK_NUMBER_MAX];
    char current[ASK_NUMBER_MAX];
    size_t current_len = 0;
    int prompt_width;
    int x_col;
    int input_key;
    int result;

    prompt_clear_area_no_update();
    text_display_string(prompt, 0, fg_color, 0x18, 0);

    prompt_width = (int)strlen(prompt);
    x_col        = prompt_width;

    snprintf(max_value_str, sizeof(max_value_str), "%d", max_value);
    current[0] = '\0';

    do {
        input_key = input_get_key();

        if (input_key >= '0' && input_key <= '9') {
            if (current_len + 1 < sizeof(current)) {
                current[current_len++] = (char)input_key;
                current[current_len]   = '\0';

                if (max_value >= (int)strtol(current, NULL, 10)) {
                    x_col++;
                } else {
                    /* Too much: the answer snaps to the most that can be had. */
                    snprintf(current, sizeof(current), "%s", max_value_str);
                    current_len = strlen(current);

                    x_col = (int)strlen(max_value_str) + prompt_width;
                }
            }

            text_display_string(current, 0, 15, 0x18, prompt_width);
        } else if (input_key == 8 && current_len > 0) {
            current[--current_len] = '\0';

            text_display_space_char(0x18, x_col - 1);
            x_col--;
        }
    } while (input_key != 0x0d && input_key != 0x1b &&
             !input_quit_requested());

    prompt_clear_area_no_update();

    if (input_key == 0x1b || (input_key == 0x0d && current_len == 0)) {
        result = 0;
    } else {
        result = (int)strtol(current, NULL, 10);
    }

    return (i16)result;
}

void treasure_trade_money(MoneyKind money_slot, i16 num_coins, Player *dest,
                          Player *source)
{
    if (dest->weight + num_coins <= treasure_max_load(dest)) {
        money_add_coins(&source->money, money_slot, -num_coins);
        player_remove_weight(source, num_coins);

        money_add_coins(&dest->money, money_slot, num_coins);
        player_add_weight(dest, num_coins);
    } else {
        character_print_message("Overloaded");
    }
}

void treasure_pool_money(void)
{
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player->control_morale == CONTROL_PC_BASE ||
            player->control_morale == CONTROL_PC_BERZERK) {
            money_add(&gbl.pooled_money, &gbl.pooled_money, &player->money);

            /* Gems and jewelry weigh a coin each too, so all seven kinds come
             * off the character's weight. */
            for (int coin = 0; coin < MONEY_KINDS; coin++) {
                player_remove_weight(player,
                                     money_get(&player->money,
                                               (MoneyKind)coin));
            }

            money_clear_all(&player->money);
        }
    }
}

int treasure_party_count(void)
{
    int count = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *player = gbl.team_list[i];

        if (player->control_morale == CONTROL_PC_BASE ||
            player->control_morale == CONTROL_PC_BERZERK) {
            count++;
        }
    }

    return count;
}

void treasure_share_pooled(void)
{
    int money_remainder[MONEY_KINDS];
    int money_each[MONEY_KINDS];
    int party_size = treasure_party_count();

    for (int coin = 0; coin < MONEY_KINDS; coin++) {
        if (money_get(&gbl.pooled_money, (MoneyKind)coin) > 0) {
            /* The C# divided by the party count with no test of its own: a
             * share-out with nobody to share to threw DivideByZeroException.
             * Here the whole pool is left where it is, which is what a party of
             * nobody would end up with anyway. */
            if (party_size == 0) {
                log_warn("treasure: nobody left to share the pool out to");
                return;
            }

            money_each[coin] =
                money_get(&gbl.pooled_money, (MoneyKind)coin) / party_size;
            money_remainder[coin] =
                money_get(&gbl.pooled_money, (MoneyKind)coin) % party_size;
        } else {
            money_each[coin]      = 0;
            money_remainder[coin] = 0;
        }
    }

    /* A share each, jewelry first: the heavy items go out while there is still
     * room to carry them. Note the test - a control byte below the NPC range,
     * which is not the test the share was worked out with: a berserk party
     * member (0xb3) counted towards the party size but is passed over here, and
     * their share falls through to the leftovers pass below. */
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player->control_morale < CONTROL_NPC_BASE) {
            for (int coin = MONEY_KINDS - 1; coin >= 0; coin--) {
                int overflow;

                if (!treasure_will_overload(&overflow, money_each[coin],
                                            player)) {
                    money_add_coins(&player->money, (MoneyKind)coin,
                                    money_each[coin]);
                    player_add_weight(player, money_each[coin]);

                    /* And one of the odd coins, if it fits. */
                    if (money_remainder[coin] > 0 &&
                        !treasure_will_overload_simple(1, player)) {
                        money_add_coins(&player->money, (MoneyKind)coin, 1);
                        player_add_weight(player, 1);
                        money_remainder[coin] -= 1;
                    }
                } else {
                    money_add_coins(&player->money, (MoneyKind)coin, overflow);

                    money_remainder[coin] += money_each[coin] - overflow;

                    player_add_weight(player, overflow);
                }
            }
        }
    }

    /* Then whatever is left over goes to whoever has room for it, in team order
     * and without the NPC test the first pass used. */
    for (int coin = MONEY_KINDS - 1; coin >= 0; coin--) {
        if (money_remainder[coin] > 0) {
            for (int i = 0; i < gbl.team_count; i++) {
                Player *player = gbl.team_list[i];
                int capacity = treasure_max_load(player) - player->weight;

                if (capacity > 0) {
                    if (money_remainder[coin] > capacity) {
                        money_add_coins(&player->money, (MoneyKind)coin,
                                        capacity);
                        player_add_weight(player, capacity);
                        money_remainder[coin] -= capacity;
                    } else {
                        money_add_coins(&player->money, (MoneyKind)coin,
                                        money_remainder[coin]);
                        player_add_weight(player, money_remainder[coin]);
                        money_remainder[coin] = 0;
                    }
                }
            }
        }
    }

    for (int coin = MONEY_COPPER; coin <= MONEY_JEWELRY; coin++) {
        money_set(&gbl.pooled_money, (MoneyKind)coin, money_remainder[coin]);
    }
}

void treasure_drop_coins(MoneyKind money_slot, int num_coins, Player *player)
{
    money_add_coins(&player->money, money_slot, -num_coins);
    player_remove_weight(player, num_coins);

    /* Only where there is a pool to drop it into. On the road it is gone. */
    if (gbl.game_state == GAME_STATE_AFTER_COMBAT ||
        gbl.game_state == GAME_STATE_SHOP) {
        money_add_coins(&gbl.pooled_money, money_slot, num_coins);
    }
}

void treasure_pickup_coins(MoneyKind money_slot, int num_coins, Player *player)
{
    if (treasure_will_overload_simple(num_coins, player)) {
        character_print_message("Overloaded");
    } else {
        if (num_coins > money_get(&gbl.pooled_money, money_slot)) {
            num_coins = money_get(&gbl.pooled_money, money_slot);
        }

        money_add_coins(&gbl.pooled_money, money_slot, -num_coins);

        money_add_coins(&player->money, money_slot, num_coins);
        player_add_weight(player, num_coins);
    }
}

int treasure_money_index_from_string(char *display_text, size_t display_size,
                                     const char *input)
{
    int offset = 0;
    /* "this is outofbounds", as the C# put it: an unrecognised line answers with
     * a coin kind that does not exist, and the caller asks the pool how much of
     * it there is. money_get logs that and reads 0, where the C# threw. */
    int index = MONEY_KINDS;
    const char *word = "";
    char ch;

    /* A line of nothing but spaces ran off the end of the string in the C#. Here
     * it stops at the terminator and falls through to the out-of-range index. */
    while (input[offset] == ' ') {
        offset++;
    }

    ch = input[offset];

    if (ch == 'G') {
        /* Gems and Gold both start with a G, so the second letter decides. */
        ch = input[offset + 1];

        if (toupper((unsigned char)ch) == 'E') {
            index = MONEY_GEMS;
            word  = "Gems ";
        } else {
            word  = "Gold ";
            index = MONEY_GOLD;
        }
    } else if (ch == 'P') {
        word  = "Platinum ";
        index = MONEY_PLATINUM;
    } else if (ch == 'E') {
        word  = "Electrum ";
        index = MONEY_ELECTRUM;
    } else if (ch == 'S') {
        word  = "Silver ";
        index = MONEY_SILVER;
    } else if (ch == 'C') {
        word  = "Copper ";
        index = MONEY_COPPER;
    } else if (ch == 'J') {
        word  = "Jewelry ";
        index = MONEY_JEWELRY;
    }

    snprintf(display_text, display_size, "%s", word);

    return index;
}

void treasure_take_pool_money(void)
{
    /* The list is rebuilt every time round the loop, and a MenuList is 10K, so
     * it is not worth putting on the stack. Nothing here is re-entered. */
    static MenuList money;
    bool no_money_left;

    frames_draw_outer();

    do {
        MenuItem *chosen = NULL;
        bool redraw_menu_items = true;
        int dummy_index = 0;
        char input_key;

        menu_list_clear(&money);

        for (int coin = MONEY_KINDS - 1; coin >= 0; coin--) {
            int count = money_get(&gbl.pooled_money, (MoneyKind)coin);

            if (count > 0) {
                char line[MENU_ITEM_TEXT_MAX];

                snprintf(line, sizeof(line), "%s %d", money_names[coin], count);
                menu_list_add(&money, line);
            }
        }

        input_key = prompt_select_item(&chosen, &dummy_index,
                                       &redraw_menu_items, true, &money,
                                       8, 15, 2, 2, GBL_DEFAULT_MENU_COLORS,
                                       "Select", "Select type of coin ");

        if (chosen == NULL || input_key == '\0') {
            no_money_left = true;
        } else {
            char kind_text[32];
            char text[64];
            int money_slot;
            i16 num_coins;

            no_money_left = false;

            money_slot = treasure_money_index_from_string(
                             kind_text, sizeof(kind_text), chosen->text);

            snprintf(text, sizeof(text), "How much %s will you take? ",
                     kind_text);

            num_coins = treasure_ask_number_value(
                            10, text,
                            money_get(&gbl.pooled_money,
                                      (MoneyKind)money_slot));

            if (gbl.selected_player != NULL) {
                treasure_pickup_coins((MoneyKind)money_slot, num_coins,
                                      gbl.selected_player);
            } else {
                log_warn("treasure: nobody is holding out a hand for the pool");
            }

            menu_list_clear(&money);

            no_money_left = true;
            for (int coin = 0; coin < MONEY_KINDS; coin++) {
                if (money_get(&gbl.pooled_money, (MoneyKind)coin) > 0) {
                    no_money_left = false;
                }
            }
        }
    } while (!no_money_left && !input_quit_requested());
}

void treasure_on_ground(bool *out_items, bool *out_money)
{
    *out_money = money_any(&gbl.pooled_money);
    *out_items = gbl.ground_item_count > 0;
}

/* ---------------------------------------------------------------- the hoard */

i8 treasure_random_bonus(void)
{
    i8 bonus = 0;
    int roll = effect_roll_dice(20, 1);

    if (roll >= 1 && roll <= 14) {
        bonus = 1;
    } else if (roll >= 15 && roll <= 20) {
        bonus = 2;
    }

    return bonus;
}

/* seg600:082E, unk_16B3E. Seven ready-made magic items, one row each:
 *
 *   0  Potion of Extra Healing    4  Wand of Magic Missiles
 *   1  Potion of Giant Strength   5  Gauntlets of Ogre Power
 *   2  Potion of Healing          6  Javelin of Lightning
 *   3  Potion of Speed
 *
 * The columns are the three name parts, the weight, the worth, and the three
 * affects. Row 3 is never chosen by anything - the potion roll picks 0 or 2 -
 * and a cloak is rolled as row 1, a potion of giant strength, which is what the
 * original's table said and what it therefore hands out. */
static const i16 PRECONFIGURED_ITEMS[7][8] = {
    { 0x00B9, 0x00BB, 0x0040, 0x0001, 0x0320, 0x0003, 0x0063, 0x0000 },
    { 0x00EF, 0x00A7, 0x0040, 0x0001, 0x044C, 0x0001, 0x003b, 0x0000 },
    { 0x00b9, 0x00a7, 0x0040, 0x0001, 0x0190, 0x0001, 0x0003, 0x0000 },
    { 0x00AD, 0x00a7, 0x0040, 0x0001, 0x01C2, 0x0001, 0x0030, 0x0000 },
    { 0x00CE, 0x00A7, 0x0045, 0x0001, 0x2AF8, 0x001E, 0x000F, 0x0000 },
    { 0x00E2, 0x00A7, 0x0064, 0x000A, 0x3A98, 0x0000, 0x0026, 0x0083 },
    { 0x009d, 0x00a7, 0x0015, 0x0014, 0x0bb8, 0x0001, 0x0033, 0x0000 }
};

/* The weight of every weapon and piece of armour that can be rolled, and the
 * stack size of the ones that come in bundles. Everything the switch did not
 * name weighed 40 and arrived ten at a time. */
static void set_weapon_weight(Item *item)
{
    item->count = 0;

    switch (item->type) {
    case ITEM_BATTLE_AXE:
    case ITEM_MILITARY_FORK:
    case ITEM_GLAIVE:
    case ITEM_BROAD_SWORD:
        item->weight = 75;
        break;

    case ITEM_HAND_AXE:
    case ITEM_HAMMER:
    case ITEM_RANSEUR:
    case ITEM_SPEAR:
    case ITEM_SPETUM:
    case ITEM_QUARTER_STAFF:
    case ITEM_TRIDENT:
    case ITEM_COMPOSITE_SHORT_BOW:
    case ITEM_SHORT_BOW:
    case ITEM_LIGHT_CROSSBOW:
    case ITEM_SHIELD:
        item->weight = 50;
        break;

    case ITEM_BARDICHE:
    case ITEM_MORNING_STAR:
    case ITEM_VOULGE:
        item->weight = 0x7d;
        break;

    case ITEM_BEC_DE_CORBIN:
    case ITEM_GLAIVE_GUISARME:
    case ITEM_MACE:
    case ITEM_BASTARD_SWORD:
    case ITEM_LONG_BOW:
    case ITEM_HEAVY_CROSSBOW:
    case ITEM_PADDED_ARMOR:
        item->weight = 100;
        break;

    case ITEM_BILL_GUISARME:
    case ITEM_FLAIL:
    case ITEM_GUISARME_VOULGE:
    case ITEM_LUCERN_HAMMER:
    case ITEM_LEATHER_ARMOR:
        item->weight = 0x96;
        break;

    case ITEM_BO_STICK:
        item->weight = 15;
        break;

    case ITEM_CLUB:
        item->weight = 0x1e;
        break;

    case ITEM_DAGGER:
    case ITEM_BRACERS:
        item->weight = 10;
        break;

    case ITEM_DART:
        item->weight = 0x19;
        item->count  = 5;
        break;

    case ITEM_FAUCHARD:
    case ITEM_MILITARY_PICK:
    case ITEM_LONG_SWORD:
        item->weight = 0x3c;
        break;

    case ITEM_FAUCHARD_FORK:
    case ITEM_GUISARME:
    case ITEM_PARTISAN:
    case ITEM_AWL_PIKE:
    case ITEM_COMPOSITE_LONG_BOW:
    case ITEM_SLING:
        item->weight = 80;
        break;

    case ITEM_HALBERD:
        item->weight = 175;
        break;

    case ITEM_JAVELIN:
        item->weight = 20;
        break;

    case ITEM_JO_STICK:
    case ITEM_SCIMITAR:
        item->weight = 40;
        break;

    case ITEM_SHORT_SWORD:
        item->weight = 35;
        break;

    case ITEM_TWO_HANDED_SWORD:
    case ITEM_RING_MAIL:
        item->weight = 250;
        break;

    case ITEM_STUDDED_LEATHER:
        item->weight = 0x0c8;
        break;

    case ITEM_SCALE_MAIL:
    case ITEM_SPLINT_MAIL:
        item->weight = 400;
        break;

    case ITEM_CHAIN_MAIL:
        item->weight = 0x12c;
        break;

    case ITEM_BANDED_MAIL:
        item->weight = 0x15e;
        break;

    /* The C# left a note here: "wonder if this should have been 0x3f". The case
     * above it is commented out in the original decompilation too. */
    case ITEM_PLATE_MAIL:
        item->weight = 450;
        break;

    case ITEM_RING_OF_PROT:
        item->weight = 1;
        break;

    default:
        item->weight = 40;
        item->count  = 10;
        break;
    }
}

/* The name parts a rolled weapon or piece of armour gets. Armour hides its
 * bonus - hidden_names_flag 4 keeps namenum1 off the label - so a suit of plate
 * reads "Plate Mail" until it is identified. */
static void set_weapon_names(Item *item)
{
    if (item->type == ITEM_JAVELIN) {
        item->namenum3 = 0x15;
        item->namenum2 = item->plus + 0xa1;
    } else if (item->type == ITEM_QUARREL) {
        item->namenum3 = 0x1c;
        item->namenum2 = item->plus + 0xa1;
    } else if (item->type == ITEM_LEATHER_ARMOR ||
               item->type == ITEM_PADDED_ARMOR) {
        item->namenum3 = item->type;
        item->namenum2 = 0x31;
        item->namenum1 = item->plus + 0xa1;
        item->hidden_names_flag = 4;
    } else if (item->type == ITEM_STUDDED_LEATHER) {
        item->namenum3 = item->type;
        item->namenum2 = 0x32;
        item->namenum1 = item->plus + 0xa1;
        item->hidden_names_flag = 4;
    } else if (item->type >= ITEM_RING_MAIL && item->type <= ITEM_PLATE_MAIL) {
        item->namenum3 = item->type;
        item->namenum2 = 0x30;
        item->namenum1 = item->plus + 0xa1;
        item->hidden_names_flag = 4;
    } else if (item->type == ITEM_ARROW) {
        item->namenum3 = 0x3d;
        item->namenum2 = item->plus + 0xa1;
    } else if (item->type == ITEM_BRACERS) {
        item->namenum3 = 0x4f;
        item->namenum2 = 0xa7;
        /* Bracers are named for the armour class they give, not for a bonus, so
         * the roll of 1 or 2 becomes 4 or 6 first. */
        item->plus = (item->plus << 1) + 2;

        if (item->plus == 4) {
            item->namenum1 = 0xdd;
        } else if (item->plus == 6) {
            item->namenum1 = 0xde;
        }
    } else if (item->type == ITEM_RING_OF_PROT) {
        item->namenum3 = 0x42;
        item->namenum2 = 0xe0;
        item->namenum1 = item->plus + 0xa1;
    } else {
        item->namenum3 = item->type;
        item->namenum2 = item->plus + 0xa1;
    }
}

static void set_weapon_value(Item *item)
{
    if (item->type == ITEM_SHIELD) {
        item->value = (i16)(item->plus * 2500);
    } else if (item->type == ITEM_ARROW || item->type == ITEM_QUARREL) {
        item->value = (i16)(item->plus * 150);
    } else if (item->type == ITEM_RING_MAIL || item->type == ITEM_SCALE_MAIL) {
        item->value = (i16)(item->plus * 3000);
    } else if (item->type == ITEM_CHAIN_MAIL || item->type == ITEM_SPLINT_MAIL) {
        item->value = (i16)(item->plus * 3500);
    } else if (item->type == ITEM_BANDED_MAIL) {
        item->value = (i16)(item->plus * 4000);
    } else if (item->type == ITEM_PLATE_MAIL) {
        item->value = (i16)(item->plus * 5000);
    } else if (item->type == ITEM_BRACERS) {
        item->value = (i16)(item->plus * 3000);
    } else {
        item->value = (i16)(item->plus * 2000);
    }
}

/* One to three spells for a scroll, rolled by level: the first roll picks the
 * level, the second the spell within it. The two tables are the magic user's and
 * the cleric's spell numbering.
 *
 * A level outside 1..5 leaves the spell 0, where the C# kept whatever the last
 * roll produced. Nothing can reach it: the level is a d5. */
static u8 roll_scroll_spell(bool mu_scroll, int level)
{
    u8 spell = 0;

    if (mu_scroll) {
        switch (level) {
        case 1: spell = (u8)(effect_roll_dice(13, 1) + 8);   break;
        case 2: spell = (u8)(effect_roll_dice(7, 1) + 28);   break;
        case 3: spell = (u8)(effect_roll_dice(0x0b, 1) + 44); break;
        case 4: spell = (u8)(effect_roll_dice(9, 1) + 80);   break;
        case 5: spell = (u8)(effect_roll_dice(4, 1) + 90);   break;
        default: break;
        }
    } else {
        switch (level) {
        case 1: spell = effect_roll_dice(8, 1);              break;
        case 2: spell = (u8)(effect_roll_dice(7, 1) + 0x15); break;
        case 3: spell = (u8)(effect_roll_dice(8, 1) + 0x24); break;
        case 4: spell = (u8)(effect_roll_dice(5, 1) + 0x41); break;
        case 5: spell = (u8)(effect_roll_dice(6, 1) + 0x46); break;
        default: break;
        }
    }

    return spell;
}

void treasure_create_item(Item *item, ItemType item_type)
{
    int preconfigured = -1;

    item_init(item, item_type, 0, 0, 0, 0, 0, false, 6, false, 0, 0, 0, 0, 0, 0);

    if ((item->type >= ITEM_BATTLE_AXE && item->type <= ITEM_SHIELD) ||
        item->type == ITEM_ARROW ||
        item->type == ITEM_BRACERS ||
        item->type == ITEM_RING_OF_PROT) {
        item->plus = treasure_random_bonus();

        /* One javelin in five is not a +1 or +2 javelin at all but a javelin of
         * lightning, and the row below overwrites everything set here. */
        if (item->type == ITEM_JAVELIN && effect_roll_dice(5, 1) == 5) {
            preconfigured = 6;
        }

        set_weapon_names(item);

        item->plus_save = 0;
        item->count     = 0;

        set_weapon_weight(item);
        set_weapon_value(item);
    } else if (item->type == ITEM_MU_SCROLL || item->type == ITEM_CLRC_SCROLL) {
        bool mu_scroll = (item->type == ITEM_MU_SCROLL);
        u8 spells_count = effect_roll_dice(3, 1);

        item->namenum3 = mu_scroll ? 0xd1 : 0xd0;
        item->namenum2 = spells_count + 0xd1;
        item->namenum1 = 0;
        item->plus     = 1;
        item->weight   = 0x19;
        item->count    = 0;
        item->value    = 0;

        for (int i = 1; i <= spells_count; i++) {
            int level = effect_roll_dice(5, 1);

            item_affect_set(item, i, (Affects)roll_scroll_spell(mu_scroll,
                                                               level));
            /* A higher-level spell is worth more, whichever slot it landed in. */
            item->value = (i16)(item->value + level * 300);
        }
    } else if (item->type == ITEM_GAUNTLETS || item->type == ITEM_TYPE_67) {
        preconfigured = 5;
    } else if (item->type == ITEM_WAND_A || item->type == ITEM_WAND_B) {
        preconfigured = 4;
    } else if (item->type == ITEM_TYPE_89 || item->type == ITEM_CLOAK) {
        preconfigured = 1;
    } else if (item->type == ITEM_POTION) {
        int roll = effect_roll_dice(8, 1);

        if (roll >= 1 && roll <= 5) {
            preconfigured = 2;
        } else if (roll >= 6 && roll <= 8) {
            preconfigured = 0;
        }
    }

    if (preconfigured > -1) {
        const i16 *row = PRECONFIGURED_ITEMS[preconfigured];

        item->namenum1 = row[0];
        item->namenum2 = row[1];
        item->namenum3 = row[2];

        item->plus      = 1;
        item->plus_save = 1;

        item->weight = row[3];
        item->count  = 0;

        item->value = row[4];

        for (int i = 1; i <= 3; i++) {
            item_affect_set(item, i, (Affects)(u8)row[4 + i]);
        }
    }

    /* Classes/ItemLibrary.cs is not ported: the C#'s ItemLibrary.Add(item) here
     * only collected items for offline inspection. */
}

/* ------------------------------------------------------- gems and jewelry */

/* The gem's worth in gold, on d100. */
static i16 roll_gem_value(void)
{
    int roll = effect_roll_dice(100, 1);
    i16 value;

    if (roll >= 1 && roll <= 25) {
        value = 10;
    } else if (roll >= 26 && roll <= 50) {
        value = 50;
    } else if (roll >= 51 && roll <= 70) {
        value = 100;
    } else if (roll >= 71 && roll <= 90) {
        value = 500;
    } else if (roll >= 91 && roll <= 99) {
        value = 1000;
    } else if (roll == 100) {
        value = 5000;
    } else {
        value = 0;
    }

    return value;
}

/* The piece of jewelry's worth, which is a range rather than a fixed figure. */
static i16 roll_jewel_value(void)
{
    int roll = effect_roll_dice(100, 1);
    i16 value;

    if (roll >= 1 && roll <= 10) {
        value = (i16)(rnd_int(900) + 100);
    } else if (roll >= 11 && roll <= 20) {
        value = (i16)(rnd_int(1000) + 200);
    } else if (roll >= 21 && roll <= 40) {
        value = (i16)(rnd_int(1500) + 300);
    } else if (roll >= 41 && roll <= 50) {
        value = (i16)(rnd_int(2500) + 500);
    } else if (roll >= 51 && roll <= 70) {
        value = (i16)(rnd_int(5000) + 1000);
    } else if (roll >= 0x47 && roll <= 0x5a) {
        value = (i16)(rnd_int(6000) + 2000);
    } else if (roll >= 0x5b && roll <= 0x64) {
        value = (i16)(rnd_int(10000) + 2000);
    } else {
        value = 0;
    }

    return value;
}

/* Sell it for a fifth of its worth, or keep it as an item of that value. The
 * prompt offers "Keep" only when the character can still carry it. */
static void sell_or_keep(i16 value, int namenum3, Player *player)
{
    bool must_sell;
    bool special_key;
    const char *sell_text;
    char input_key;

    if (treasure_will_overload_simple(1, player) ||
        player->item_count >= PLAYER_MAX_ITEMS) {
        sell_text = "Sell";
        must_sell = true;
    } else {
        sell_text = "Sell Keep";
        must_sell = false;
    }

    input_key = prompt_display_input(&special_key, false, 1,
                                     GBL_DEFAULT_MENU_COLORS, sell_text,
                                     "You can : ");

    if (input_key == 'K' && !must_sell) {
        Item kept;

        item_init(&kept, ITEM_NECKLACE, 0, 0, (u8)namenum3, 0, 0, false, 0,
                  false, 1, 0, value, 0, 0, 0);

        player_item_add(player, &kept);
    } else {
        value /= 5;
        treasure_add_player_gold(value);
    }
}

bool treasure_appraise_gems_jewels(void)
{
    Player *player = gbl.selected_player;
    bool stop_loop;

    if (player == NULL) {
        log_warn("treasure: nobody selected to appraise anything");
        return false;
    }

    if (money_get(&player->money, MONEY_GEMS) == 0 &&
        money_get(&player->money, MONEY_JEWELRY) == 0) {
        character_print_message("No Gems or Jewelry");
        return false;
    }

    do {
        int gems   = money_get(&player->money, MONEY_GEMS);
        int jewels = money_get(&player->money, MONEY_JEWELRY);

        if (gems == 0 && jewels == 0) {
            stop_loop = true;
        } else {
            char gem_text[40];
            char jewel_text[40];
            char prompt[40];
            char value_text[64];
            bool special_key;
            char input_key;

            stop_loop = false;

            if (gems == 0) {
                gem_text[0] = '\0';
            } else {
                snprintf(gem_text, sizeof(gem_text), "%d Gem%s", gems,
                         gems == 1 ? "" : "s");
            }

            if (jewels == 0) {
                jewel_text[0] = '\0';
            } else {
                snprintf(jewel_text, sizeof(jewel_text),
                         "%d piece%s of Jewelry", jewels,
                         jewels == 1 ? "" : "s");
            }

            frames_clear_area(0x16, 0x26, 1, 1);
            character_display_name(false, 1, 1, player);

            text_display_string("You have a fine collection of:", 0, 0x0f, 7, 1);
            text_display_string(gem_text, 0, 0x0f, 9, 1);
            text_display_string(jewel_text, 0, 0x0f, 0x0a, 1);

            prompt[0] = '\0';
            snprintf(prompt, sizeof(prompt), "%s", gems != 0 ? "  Gems" : "");
            if (jewels != 0) {
                strncat(prompt, " Jewelry", sizeof(prompt) - strlen(prompt) - 1);
            }
            strncat(prompt, " Exit", sizeof(prompt) - strlen(prompt) - 1);

            input_key = prompt_display_input(&special_key, false, 1,
                                             GBL_DEFAULT_MENU_COLORS, prompt,
                                             "Appraise : ");

            if (input_key == 'G') {
                if (money_get(&player->money, MONEY_GEMS) > 0) {
                    i16 value;

                    money_add_coins(&player->money, MONEY_GEMS, -1);

                    value = roll_gem_value();

                    snprintf(value_text, sizeof(value_text),
                             "The Gem is Valued at %d gp.", value);
                    text_display_string(value_text, 0, 15, 12, 1);

                    sell_or_keep(value, 0x65, player);
                }
            } else if (input_key == 'J') {
                if (money_get(&player->money, MONEY_JEWELRY) > 0) {
                    i16 value;

                    money_add_coins(&player->money, MONEY_JEWELRY, -1);

                    value = roll_jewel_value();

                    snprintf(value_text, sizeof(value_text),
                             "The Jewel is Valued at %d gp.", value);
                    text_display_string(value_text, 0, 15, 12, 1);

                    sell_or_keep(value, 0xd6, player);
                }
            } else if (input_key == 'E' || input_key == '\0') {
                stop_loop = true;
            }

            character_recalc_values(player);
        }
    } while (!stop_loop && !input_quit_requested());

    return true;
}
