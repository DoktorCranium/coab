/* shop.c - Ported from engine/ovr007.cs. See shop.h. */

#include "shop.h"

#include <stdio.h>
#include <string.h>

#include "area.h"
#include "character.h"
#include "coab.h"
#include "enums.h"
#include "frames.h"
#include "gbl.h"
#include "item.h"
#include "log.h"
#include "menu.h"
#include "money.h"
#include "player.h"
#include "prompt.h"
#include "text.h"
#include "treasure.h"
#include "viewplayer.h"
#include "vm.h"

/* gbl.shopRedrawMenuItems. A global in the original, and nothing outside this
 * overlay ever read it: shop_buy sets it on the way in and the list clears it,
 * so what it carries between visits has no bearing on anything. */
static bool shop_redraw_menu_items = false;

/* ------------------------------------------------------------------ prices */

/* ovr007.ItemsValue. What the shop is asking for the item, which is its face
 * value shifted by the band the script set. 0x10 is not in the list, so a shop
 * marked with it charges face value, as does one the script never marked at
 * all. */
static int items_value(const Item *item_ptr)
{
    int val;

    switch (gbl.area2_ptr->field_6DA) {
    case 0x01:
        val = item_ptr->value >> 4;
        break;

    case 0x02:
        val = item_ptr->value >> 3;
        break;

    case 0x04:
        val = item_ptr->value >> 2;
        break;

    case 0x08:
        val = item_ptr->value >> 1;
        break;

    case 0x20:
        val = item_ptr->value << 1;
        break;

    case 0x40:
        val = item_ptr->value << 2;
        break;

    case 0x80:
        val = item_ptr->value << 3;
        break;

    default:
        val = item_ptr->value;
        break;
    }

    return val;
}

/* ------------------------------------------------------------- the stock list */

/* C#'s string.Trim(). The name is built with the readied column left off, but
 * the display builder pads and an item nobody has identified can still come out
 * with spaces at either end. */
static const char *trim_copy(char *dst, size_t dst_size, const char *text)
{
    size_t start = 0;
    size_t end;

    if (dst_size == 0) {
        return "";
    }

    while (text[start] == ' ') {
        start++;
    }
    end = strlen(text);
    while (end > start && text[end - 1] == ' ') {
        end--;
    }

    if (end - start >= dst_size) {
        end = start + dst_size - 1;
    }
    memcpy(dst, text + start, end - start);
    dst[end - start] = '\0';

    return dst;
}

char shop_choose_item(int *index, Item **out_item)
{
    /* Rebuilt every time round shop_buy's loop, and a MenuList is 10K, so it is
     * not on the stack. Nothing here is re-entered. */
    static MenuList list;
    MenuItem *mi = NULL;
    char input_key;

    if (out_item != NULL) {
        *out_item = NULL;
    }

    menu_list_clear(&list);

    /* The C# built the lines in ground order and inserted each at the front, so
     * the list came out reversed; walking the ground backwards puts it in the
     * same order. */
    for (int i = gbl.ground_item_count - 1; i >= 0; i--) {
        Item *item = &gbl.ground_items[i];
        /* The name is the item's own 42-character field, so the longest line
         * this can build is 42 plus the nine-column price. */
        char trimmed[ITEM_NAME_MAX + 1];
        char text[MENU_ITEM_TEXT_MAX];
        int val;

        /* Something free is priced at a copper piece instead, and the shop keeps
         * the change: this writes back into the stock, so an item the script
         * valued at nothing is worth 1 for the rest of the game. */
        if (item->value == 0) {
            item->value = 1;
        }

        val = items_value(item);

        /* "{0,-21}{1,9}": the name in the first 21 columns and the price
         * right-aligned in the next nine. A longer name pushes the price along
         * rather than being cut, which is what the C# format did too. */
        snprintf(text, sizeof(text), "%-21s%9d",
                 trim_copy(trimmed, sizeof(trimmed), item->name), val);

        menu_list_add_item(&list, text, item);
    }

    /* Dead in both versions: sl_select_item's first act is to set this to 1. */
    gbl.menu_selected_word = 0;

    input_key = prompt_select_item(&mi, index, &shop_redraw_menu_items, true,
                                   &list, 0x16, 0x26, 1, 1,
                                   GBL_DEFAULT_MENU_COLORS, "Buy", "Items: ");

    if (mi != NULL && out_item != NULL) {
        *out_item = mi->item;
    }

    /* The list held its own copy of each line, so the names in the stock are
     * still whatever they were; the original rebuilt them all here anyway, and
     * a name rebuilt from the same item says the same thing. */
    for (int i = 0; i < gbl.ground_item_count; i++) {
        character_item_display_name_build(false, false, 0, 0,
                                         &gbl.ground_items[i]);
    }

    return input_key;
}

/* ------------------------------------------------------------------- buying */

bool shop_player_add_item(Item *item)
{
    bool would_overload;

    if (item == NULL || gbl.selected_player == NULL) {
        /* The C# would have thrown here. Nobody selected means nobody to give it
         * to, so the item stays where it is. */
        log_warn("shop: nobody to hand an item to");
        return true;
    }

    if (viewplayer_can_carry(item, gbl.selected_player)) {
        character_print_message("Overloaded");
        would_overload = true;
    } else {
        would_overload = false;

        player_item_add(gbl.selected_player, item);

        character_recalc_values(gbl.selected_player);
    }

    return would_overload;
}

void shop_buy(void)
{
    int index = 0;

    frames_draw_outer();
    shop_redraw_menu_items = true;

    for (;;) {
        Item *item = NULL;
        char input_key = shop_choose_item(&index, &item);
        int item_cost;
        int player_gold;

        if (input_key != 'B' && input_key != 0x0d) {
            return;
        }

        if (item == NULL) {
            /* Buying out of an empty shop: the list has no entry under the
             * highlight to hand back, and the C# went on to read a price off
             * nothing. Treat it as backing out. */
            log_warn("shop: nothing on the shelf to buy");
            return;
        }

        item_cost   = items_value(item);
        player_gold = money_gold_worth(&gbl.selected_player->money);

        /* player_gold is read again rather than used, and the subtraction from it
         * below goes nowhere: the purse is what money_subtract_gold_worth
         * changes. Both are the original's. */
        if (item_cost <= money_gold_worth(&gbl.selected_player->money)) {
            bool overloaded = shop_player_add_item(item);

            if (overloaded == false) {
                player_gold -= item_cost;
                (void)player_gold;
                money_subtract_gold_worth(&gbl.selected_player->money,
                                          item_cost);
            }
        } else if (item_cost <= money_gold_worth(&gbl.pooled_money)) {
            /* The pool pays for what the buyer cannot, and a character with two
             * copper pieces on them can walk out with a plate mail the party
             * bought. */
            bool overloaded = shop_player_add_item(item);

            if (overloaded == false) {
                money_subtract_gold_worth(&gbl.pooled_money, item_cost);
            }
        } else {
            character_print_message("Not enough Money.");
        }

        /* Nothing leaves the shelf: the stock is not reduced by a sale, so one
         * shop can outfit the whole party out of a single suit of armour. */
    }
}

/* --------------------------------------------------------------- the shop menu */

void shop_city_shop(void)
{
    bool reload_pics = false;
    bool items_on_ground;
    bool money_on_ground;
    bool exit_shop = false;

    gbl.game_state = GAME_STATE_SHOP;

    /* Outdoors the border has to be put back before the shop's picture goes up;
     * in a dungeon it is already the right one. LoadPic sets it again straight
     * afterwards, so everything drawn from here on redraws the border. */
    gbl.redraw_boarder = (gbl.area_ptr->in_dungeon == 0);

    character_load_pic();
    gbl.redraw_boarder = true;
    character_party_summary(gbl.selected_player);

    /* Walking in empties the pool. Anything the party had pooled is gone before
     * the shopkeeper is spoken to, which is why the money warning on the way out
     * is only ever about what was pooled inside. */
    money_clear_all(&gbl.pooled_money);

    for (int i = 0; i < gbl.ground_item_count; i++) {
        character_item_display_name_build(false, false, 0, 0,
                                         &gbl.ground_items[i]);
    }

    do {
        const char *text;
        bool control_key = false;
        char input_key;

        treasure_on_ground(&items_on_ground, &money_on_ground);

        /* Take and Share are only offered while there is coin in the pool to
         * take or share out. Nothing here is offered for the items: a shop's
         * stock is on the ground and Buy is how it is picked up. */
        if (money_on_ground) {
            text = "Buy View Take Pool Share Appraise Exit";
        } else {
            text = "Buy View Pool Appraise Exit";
        }

        input_key = prompt_display_input(&control_key, false,
                                        PROMPT_CTRL_WORD_ARROWS,
                                        GBL_DEFAULT_MENU_COLORS, text, "");

        /* The arrow keys walk the party the way Home and End do. The conversion
         * has to happen here rather than as extra cases because "Pool" already
         * owns 'P' below; a typed letter is left alone. */
        switch (prompt_selection_key(input_key, control_key)) {
        case 'B':
            shop_buy();
            break;

        case 'V':
            viewplayer_view_player();
            break;

        case 'T':
            treasure_take_pool_money();
            break;

        case 'P':
            /* 'P' is also the down cursor key's scan code, so pooling the
             * party's money is gated on this being a letter someone typed. */
            if (control_key == false) {
                treasure_pool_money();
            }
            break;

        case 'S':
            /* Not gated the same way, and 'S' is Delete's scan code: pressing
             * Delete in a shop with money in the pool shares it out. */
            treasure_share_pooled();
            break;

        case 'A':
            reload_pics = treasure_appraise_gems_jewels();
            break;

        case 'E':
            treasure_on_ground(&items_on_ground, &money_on_ground);

            if (money_on_ground) {
                text_press_any_key_region("As you Leave the Shopkeeper says, "
                                          "\"Excuse me but you have Left Some "
                                          "Money here.\"  ",
                                          true, 10, TEXT_REGION_NORMAL_BOTTOM);
                text_press_any_key_region("Do you want to go back and get your "
                                          "Money?",
                                          false, 15, TEXT_REGION_NORMAL_BOTTOM);

                /* sub_317AA counts from 0, so 1 is "No" - and No is what leaves.
                 * Yes puts the menu back and the money is still there to take. */
                if (vm_menu_select(false, false, GBL_DEFAULT_MENU_COLORS,
                                   "~Yes ~No", "") == 1) {
                    exit_shop = true;
                } else {
                    frames_clear_area(0x16, 0x26, 17, 1);
                }
            } else {
                exit_shop = true;
            }
            break;

        case 'G':
        case 'O':
            /* Home and End step through the party. Unlike the dungeon menu this
             * is a case rather than the default, so every other key is ignored
             * here. */
            viewplayer_scroll_team_list(input_key);
            break;

        default:
            break;
        }

        /* Buying and taking both draw over the shop, and so does appraising -
         * that one says so through its return value because it only overwrites
         * the screen when the character had something to appraise. */
        if (input_key == 'B' || input_key == 'T') {
            character_load_pic();
        } else if (reload_pics) {
            character_load_pic();
            reload_pics = false;
        }

        character_party_summary(gbl.selected_player);
    } while (exit_shop == false);
}
