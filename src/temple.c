/* temple.c - Ported from engine/ovr005.cs. See temple.h. */

#include "temple.h"

#include <stdio.h>

#include "area.h"
#include "character.h"
#include "coab.h"
#include "effect.h"
#include "enums.h"
#include "frames.h"
#include "gbl.h"
#include "item.h"
#include "limits.h"
#include "menu.h"
#include "money.h"
#include "player.h"
#include "prompt.h"
#include "spelleffect.h"
#include "text.h"
#include "treasure.h"
#include "viewplayer.h"
#include "vm.h"

/* ovr005.disease_types. What Cure Disease undoes, and the same six Heal undoes.
 * Being an animated corpse is on the list, so a character raised as one is
 * "diseased" as far as this menu is concerned - which is why Cure Disease on a
 * walking body says nothing about not being diseased. */
static const Affects disease_types[6] = {
    AFFECT_HELPLESS,
    AFFECT_CAUSE_DISEASE_1,
    AFFECT_WEAKEN,
    AFFECT_CAUSE_DISEASE_2,
    AFFECT_ANIMATE_DEAD,
    AFFECT_39
};

/* ovr005.temple_sl. Eleven names, of which temple_heal only ever adds the first
 * ten to the list: "Exit" is a word of the prompt, not an entry, so the case 10
 * that would have handled it is unreachable. It is kept below all the same,
 * because it is in the original. */
static const char *const temple_sl[11] = {
    "Cure Blindness",
    "Cure Disease",
    "Cure Light Wounds",
    "Cure Serious Wounds",
    "Cure Critical Wounds",
    "Heal",
    "Neutralize Poison",
    "Raise Dead",
    "Remove Curse",
    "Stone to Flesh",
    "Exit"
};

/* ------------------------------------------------------------ asking twice */

/* ovr005.CastCureAnyway. "<name> is not blind." and then "cast cure anyway: ".
 * Somebody who wants to pay a thousand gold to cure a disease they have not got
 * is allowed to. */
static bool cast_cure_anyway(const char *text)
{
    char ret_val;

    character_display_status_string(false, 0, text, gbl.selected_player);

    ret_val = prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "cast cure anyway: ");

    character_clear_text_area();

    return ret_val == 'Y';
}

bool temple_buy_cure(int cost, const char *cure_name)
{
    char text[80];
    bool buy = false;

    snprintf(text, sizeof(text), "%s will only cost %d gold pieces.",
             cure_name, cost);
    text_press_any_key_region(text, true, 10, TEXT_REGION_NORMAL_BOTTOM);

    if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "pay for cure ") == 'Y') {
        /* The purse first and the pool second, the same order the shop spends
         * in, and the pool is only reached when the purse cannot cover the whole
         * price - never to make up the difference. */
        if (cost <= money_gold_worth(&gbl.selected_player->money)) {
            money_subtract_gold_worth(&gbl.selected_player->money, cost);
            buy = true;
        } else if (cost <= money_gold_worth(&gbl.pooled_money)) {
            money_subtract_gold_worth(&gbl.pooled_money, cost);
            buy = true;
        } else {
            character_print_message("Not enough money.");
            buy = false;
        }
    }

    if (buy) {
        /* "is cured." goes up as soon as the money is taken, before the caller
         * has done anything - and two of the callers then do nothing at all. */
        character_clear_text_area();
        character_display_status_string(true, 0, "is cured.",
                                       gbl.selected_player);
    }

    return buy;
}

/* ------------------------------------------------------------- the services */

void temple_cure_blindness(void)
{
    bool cast = true;

    if (player_has_affect(gbl.selected_player, AFFECT_BLINDED) == false) {
        cast = cast_cure_anyway("is not blind.");
    }

    if (cast) {
        if (temple_buy_cure(1000, "Cure Blindness")) {
            effect_remove_affect(NULL, AFFECT_BLINDED, gbl.selected_player);
        }
    }
}

void temple_cure_disease(void)
{
    bool is_diseased = false;
    bool cast = true;

    for (int i = 0; i < 6; i++) {
        if (player_has_affect(gbl.selected_player, disease_types[i])) {
            is_diseased = true;
            break;
        }
    }

    if (is_diseased == false) {
        cast = cast_cure_anyway("is not diseased.");
    }

    if (cast) {
        if (temple_buy_cure(1000, "Cure Disease")) {
            /* cure_spell tells the affect handlers this is a cure rather than the
             * affect running out, which is what stops the removal from doing the
             * damage the expiry would. */
            gbl.cure_spell = true;

            for (int i = 0; i < 6; i++) {
                effect_remove_affect(NULL, disease_types[i],
                                     gbl.selected_player);
            }

            gbl.cure_spell = false;
        }
    }
}

void temple_cure_wounds(int heal_type)
{
    switch (heal_type) {
    case 1:
        if (temple_buy_cure(100, "Cure Light Wounds")) {
            int heal_amount = effect_roll_dice(8, 1);

            effect_heal_player(0, heal_amount, gbl.selected_player);
        }
        break;

    case 2:
        if (temple_buy_cure(350, "Cure Serious Wounds")) {
            int heal_amount = effect_roll_dice(8, 2) + 1;

            effect_heal_player(0, heal_amount, gbl.selected_player);
        }
        break;

    case 3:
        if (temple_buy_cure(600, "Cure Critical Wounds")) {
            int heal_amount = effect_roll_dice(8, 3) + 3;

            effect_heal_player(0, heal_amount, gbl.selected_player);
        }
        break;

    case 4:
        if (temple_buy_cure(5000, "Heal")) {
            /* Full health less a d4, so five thousand gold never quite buys the
             * last few hit points. On somebody already at full it is a negative
             * amount and heal_player subtracts it: the most expensive service in
             * the game leaves the buyer worse off than they walked in. */
            int heal_amount = gbl.selected_player->hit_point_max;

            heal_amount -= gbl.selected_player->hit_point_current;
            heal_amount -= effect_roll_dice(4, 1);

            effect_heal_player(0, heal_amount, gbl.selected_player);
            effect_remove_affect(NULL, AFFECT_BLINDED, gbl.selected_player);

            /* Heal undoes the diseases too, but without setting cure_spell as
             * Cure Disease does, so each one expires rather than being cured. */
            for (int i = 0; i < 6; i++) {
                effect_remove_affect(NULL, disease_types[i],
                                     gbl.selected_player);
            }

            effect_remove_affect(NULL, AFFECT_FEEBLEMIND, gbl.selected_player);

            /* Feeblemind moved Int and Wis, so the spell tables that hang off
             * them are rebuilt. */
            effect_calc_stat_bonuses(STAT_INT, gbl.selected_player);
            effect_calc_stat_bonuses(STAT_WIS, gbl.selected_player);
        }
        break;

    default:
        break;
    }
}

void temple_cure_poison2(void)
{
    bool is_poisoned = player_has_affect(gbl.selected_player, AFFECT_POISONED);

    if (is_poisoned == true ||
        (is_poisoned == false && cast_cure_anyway("is not poisoned."))) {
        if (temple_buy_cure(1000, "Neutralize Poison")) {
            gbl.cure_spell = true;

            effect_remove_affect(NULL, AFFECT_POISONED, gbl.selected_player);
            effect_remove_affect(NULL, AFFECT_SLOW_POISON, gbl.selected_player);
            effect_remove_affect(NULL, AFFECT_POISON_DAMAGE,
                                 gbl.selected_player);

            gbl.cure_spell = false;
        }
    }
}

void temple_raise_dead(void)
{
    Player *player = gbl.selected_player;
    bool player_dead = false;

    if (player->health_status == STATUS_DEAD ||
        player->health_status == STATUS_ANIMATED) {
        player_dead = true;
    }

    if (player_dead == true ||
        (player_dead == false && cast_cure_anyway("is not dead."))) {
        /* buy_cure is on the left of the &&, so it runs first: somebody who says
         * yes to raising a living character is charged five and a half thousand
         * gold, told they are cured, and nothing happens. */
        if (temple_buy_cure(5500, "Raise Dead") && player_dead == true) {
            int var_107;
            int var_108 = 0;
            int fighter_lvl = player->class_level[SKILL_FIGHTER];

            gbl.cure_spell = true;

            effect_remove_affect(NULL, AFFECT_ANIMATE_DEAD, player);
            effect_remove_affect(NULL, AFFECT_POISONED, player);

            gbl.cure_spell = false;

            player->hit_point_current = 1;
            player->health_status = STATUS_OKEY;
            player->in_combat = true;

            /* Raising the dead is supposed to cost a point of Constitution.
             * This takes it off only when there is none left to take, so a
             * character can be raised as often as the party can pay and a Con of
             * 0 goes to -1 instead. The condition is the original's; the
             * subtraction is the one line of it that ever runs. */
            if (player->stats.value[PSTAT_CON].full <= 0) {
                player->stats.value[PSTAT_CON].full--;
            }

            /* What follows was meant to take the Constitution bonus off the
             * character's maximum, and instead recomputes the maximum as the
             * bonus divided by the levels that earned it - a handful of hit
             * points where there were dozens. It only happens to a Con of 14 or
             * more, which is the only reason a raised fighter is not always
             * crippled by it. Reproduced as it stands. */
            if (player->hit_point_max > player->hit_point_rolled) {
                var_107 = player->hit_point_max - player->hit_point_rolled;
            } else {
                var_107 = 0;
            }

            if (player->stats.value[PSTAT_CON].full >= 14) {
                for (int class_idx = 0; class_idx <= 7; class_idx++) {
                    if (player->class_level[class_idx] > 0) {
                        if (class_idx == 2) {
                            /* fighter_lvl is class_level[2], so this is the same
                             * level the loop is already looking at - written the
                             * long way round in the original too. */
                            var_108 += (player->stats.value[PSTAT_CON].full - 14)
                                       * fighter_lvl;
                        } else if (player->stats.value[PSTAT_CON].full > 15) {
                            var_108 += player->class_level[class_idx] * 2;
                        } else {
                            var_108 += player->class_level[class_idx];
                        }
                    }
                }

                if (var_108 > 0) {
                    var_107 /= var_108;
                }

                /* Con is 14 or more here, so the first test alone covers 14, 15
                 * and 16 and the two after it are only ever reached at 17 and
                 * up. The third can never say anything the second did not. */
                if (player->stats.value[PSTAT_CON].full < 17 ||
                    fighter_lvl > 0 ||
                    fighter_lvl > player->multiclass_level) {
                    player->hit_point_max = (u8)var_107;
                }
            }
        }
    }
}

void temple_remove_curse(void)
{
    bool has_curse_items = false;
    bool cast = true;

    for (int i = 0; i < gbl.selected_player->item_count; i++) {
        if (gbl.selected_player->items[i].cursed) {
            has_curse_items = true;
            break;
        }
    }

    if (has_curse_items == false &&
        player_has_affect(gbl.selected_player, AFFECT_BESTOW_CURSE) == false) {
        cast = cast_cure_anyway("is not cursed.");
    }

    if (cast && temple_buy_cure(3500, "Remove Curse")) {
        /* The temple casts the spell rather than doing the work itself, so a
         * curse lifted here is lifted exactly as a cleric would lift it. */
        gbl_spell_targets_clear();
        gbl_spell_target_add(gbl.selected_player);
        spelleffect_call(SPELL_REMOVE_CURSE);
    }
}

void temple_stone_to_flesh(void)
{
    if (gbl.selected_player->health_status == STATUS_STONED ||
        (gbl.selected_player->health_status != STATUS_STONED &&
         cast_cure_anyway("is not stoned."))) {
        /* buy_cure again on the left of the &&: two thousand gold to unpetrify
         * somebody who is not petrified is taken and nothing is done. */
        if (temple_buy_cure(2000, "Stone to Flesh") &&
            gbl.selected_player->health_status == STATUS_STONED) {
            gbl.selected_player->health_status = STATUS_OKEY;
            gbl.selected_player->in_combat = true;
            gbl.selected_player->hit_point_current = 1;
        }
    }
}

/* ------------------------------------------------------------ the heal list */

void temple_heal(void)
{
    /* A MenuList is 10K, and nothing here is re-entered. */
    static MenuList list;
    int sl_index = 0;
    bool end_shop = false;
    bool redraw_menu_items = true;

    menu_list_clear(&list);

    /* Ten of the eleven names: "Exit" is a word of the prompt. */
    for (int i = 0; i < 10; i++) {
        menu_list_add(&list, temple_sl[i]);
    }

    prompt_clear_area_no_update();
    frames_draw_wilderness_map();

    do {
        char text[PLAYER_NAME_MAX + 32];
        char sl_output;
        MenuItem *dummy_selected = NULL;

        snprintf(text, sizeof(text), "%s, how can we help you?",
                 gbl.selected_player->name);
        text_display_string(text, 0, 15, 1, 1);

        /* Ten entries in fourteen rows, so there is never a Next or a Prev and
         * the list is walked with Home and End. show_exit is false because the
         * prompt already says Exit, and the key still comes back as '\0'. */
        sl_output = prompt_select_item(&dummy_selected, &sl_index,
                                      &redraw_menu_items, false, &list,
                                      15, 0x26, 4, 2, GBL_DEFAULT_MENU_COLORS,
                                      "Heal Exit", "");

        if (sl_output == 'H' || sl_output == 0x0d) {
            switch (sl_index) {
            case 0:
                temple_cure_blindness();
                break;

            case 1:
                temple_cure_disease();
                break;

            case 2:
                temple_cure_wounds(1);
                break;

            case 3:
                temple_cure_wounds(2);
                break;

            case 4:
                temple_cure_wounds(3);
                break;

            case 5:
                temple_cure_wounds(4);
                break;

            case 6:
                temple_cure_poison2();
                break;

            case 7:
                temple_raise_dead();
                break;

            case 8:
                temple_remove_curse();
                break;

            case 9:
                temple_stone_to_flesh();
                break;

            case 10:
                /* Unreachable: the list stops at nine. */
                end_shop = true;
                break;

            default:
                break;
            }
        } else if (sl_output == '\0') {
            end_shop = true;
        }
    } while (end_shop == false);

    character_load_pic();
    character_party_summary(gbl.selected_player);
}

/* -------------------------------------------------------- the temple menu */

void temple_shop(void)
{
    bool reload_pics = false;
    bool items_on_ground;
    bool money_on_ground;
    bool stop_loop = false;

    gbl.game_state = GAME_STATE_SHOP;

    /* The same border handling as the shop: outdoors it has to be put back
     * before the picture goes up, and LoadPic sets the flag again straight
     * afterwards. */
    gbl.redraw_boarder = (gbl.area_ptr->in_dungeon == 0);

    character_load_pic();
    gbl.redraw_boarder = true;
    character_party_summary(gbl.selected_player);

    /* Walking in empties the pool, so the money warning on the way out is only
     * ever about what was pooled inside. */
    money_clear_all(&gbl.pooled_money);

    do {
        const char *text;
        bool control_key = false;
        char input_key;

        treasure_on_ground(&items_on_ground, &money_on_ground);

        if (money_on_ground) {
            text = "Heal View Take Pool Share Appraise Exit";
        } else {
            text = "Heal View Pool Appraise Exit";
        }

        input_key = prompt_display_input(&control_key, false,
                                        PROMPT_CTRL_WORD_ARROWS,
                                        GBL_DEFAULT_MENU_COLORS, text, "");

        /* As in the shop: an arrow walks the party, a typed letter is itself. The
         * up arrow therefore never reaches the 'H' case below, which still needs
         * its own gate for the same reason it always did. */
        switch (prompt_selection_key(input_key, control_key)) {
        case 'H':
            /* 'H' is also the up cursor key's scan code, so this is gated the
             * way the shop gates Pool. */
            if (control_key == false) {
                temple_heal();
            }
            break;

        case 'V':
            viewplayer_view_player();
            break;

        case 'T':
            treasure_take_pool_money();
            break;

        case 'P':
            if (control_key == false) {
                treasure_pool_money();
            }
            break;

        case 'S':
            /* Not gated, and 'S' is Delete's scan code - as in the shop. */
            treasure_share_pooled();
            break;

        case 'A':
            reload_pics = treasure_appraise_gems_jewels();
            break;

        case 'E':
            treasure_on_ground(&items_on_ground, &money_on_ground);

            if (money_on_ground) {
                /* Both lines clear the region here, where the shop's second one
                 * does not, so the temple's question replaces the priest's
                 * remark instead of following it. */
                text_press_any_key_region("As you leave a priest says, \"Excuse "
                                          "me but you have left some money "
                                          "here\" ",
                                          true, 10, TEXT_REGION_NORMAL_BOTTOM);
                text_press_any_key_region("Do you want to go back and retrieve "
                                          "your money?",
                                          true, 10, TEXT_REGION_NORMAL_BOTTOM);

                /* sub_317AA counts from 0, so 1 is "No" - and No is what
                 * leaves. */
                if (vm_menu_select(false, false, GBL_DEFAULT_MENU_COLORS,
                                   "~Yes ~No", "") == 1) {
                    stop_loop = true;
                } else {
                    frames_clear_area(0x16, 0x26, 17, 1);
                }
            } else {
                stop_loop = true;
            }
            break;

        case 'G':
        case 'O':
            viewplayer_scroll_team_list(input_key);
            break;

        default:
            break;
        }

        /* Copied from CityShop and only half applicable: there is no Buy in a
         * temple, so the 'B' half never fires. Heal draws over the screen too,
         * and puts the picture back itself. */
        if (input_key == 'B' || input_key == 'T') {
            character_load_pic();
        } else if (reload_pics) {
            character_load_pic();
            reload_pics = false;
        }

        character_party_summary(gbl.selected_player);
    } while (stop_loop == false);
}
