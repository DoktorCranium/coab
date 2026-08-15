/* aftercombat.c - Ported from engine/ovr006.cs. See aftercombat.h. */

#include "aftercombat.h"

#include <stdio.h>
#include <string.h>

#include "character.h"
#include "cheats.h"
#include "combat.h"
#include "effect.h"
#include "enums.h"
#include "frames.h"
#include "gbl.h"
#include "log.h"
#include "menu.h"
#include "money.h"
#include "partymenu.h"
#include "prompt.h"
#include "shop.h"
#include "spellcast.h"
#include "text.h"
#include "treasure.h"
#include "viewplayer.h"
#include "vm.h"

/* ------------------------------------------------------- what a fight earned */

int aftercombat_calc_battle_exp(void)
{
    int total = 0;
    int share_count;

    if (gbl.combat_type == COMBAT_TYPE_DUEL) {
        if (gbl.selected_player == NULL) {
            log_warn("after combat: a duel with nobody in it is worth nothing");
            return 0;
        }

        return gbl.selected_player->hit_dice * 100;
    }

    /* Every enemy who is not still standing - dead, unconscious, dying, stoned
     * or turned to nothing - is worth their hit dice and their own bonus, and
     * gives up their purse and their pack. One who ran off keeps all three. */
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        if (player->combat_team == TEAM_ENEMY &&
            player->health_status != STATUS_OKEY &&
            player->health_status != STATUS_RUNNING) {
            gbl.byte_1AB14 = true;

            money_add(&gbl.pooled_money, &gbl.pooled_money, &player->money);

            total += player->field_13E * player->hit_point_rolled;
            total += player->field_13C;

            /* Area2.field_5C6 of 1 is the encounter that leaves nothing behind:
             * the monsters' packs go with them. */
            if (gbl.area2_ptr->field_5C6 != 1) {
                for (int j = 0; j < player->item_count; j++) {
                    Item dropped;

                    character_item_display_name_build(false, false, 0, 0,
                                                      &player->items[j]);

                    /* ShallowClone: the ground holds copies, which is what lets
                     * the monster be freed straight afterwards. */
                    dropped = player->items[j];
                    dropped.readied = false;
                    gbl_ground_item_add(&dropped);
                }
            }
        }
    }

    total += money_exp_worth(&gbl.pooled_money);

    /* Magic on the ground is worth 400 an enchantment. The list is walked only
     * as far as gbl.item_ptr, which is where the DOS build's list ended: what is
     * in hand marks the end of what this fight put there, so treasure the party
     * was already carrying around is not counted twice. Nothing sets it before
     * this point in a normal fight, and then the whole list counts. */
    for (int i = 0; i < gbl.ground_item_count; i++) {
        Item *item = &gbl.ground_items[i];

        if (item == gbl.item_ptr) {
            break;
        }

        if (item->plus > 0) {
            total += item->plus * 400;
        }
    }

    /* The share is per surviving party member. A fight nobody survived would
     * divide by zero, where the C# would have thrown. */
    share_count = (int)gbl.area2_ptr->party_size - gbl.party_animated_count;

    if (share_count == 0) {
        log_warn("after combat: %d experience with nobody left to take it",
                 total);
        return 0;
    }

    return total / share_count;
}

void aftercombat_add_exp(int exp_to_add)
{
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];
        int new_exp;

        if (player == NULL) {
            continue;
        }

        if (player->in_combat != true ||
            player->health_status == STATUS_ANIMATED) {
            continue;
        }

        new_exp = exp_to_add;

        /* A tenth again for a stat over 15 in whatever the class lives by, and
         * every one of them for a class that lives by several. */
        switch (player->cls) {
        case CLASS_CLERIC:
            if (player->stats.value[PSTAT_WIS].full > 15) {
                new_exp = exp_to_add + (exp_to_add / 10);
            }
            break;

        case CLASS_FIGHTER:
            if (player->stats.value[PSTAT_STR].full > 15) {
                new_exp = exp_to_add + (exp_to_add / 10);
            }
            break;

        case CLASS_PALADIN:
            if (player->stats.value[PSTAT_STR].full > 15 &&
                player->stats.value[PSTAT_WIS].full > 15) {
                new_exp = exp_to_add + (exp_to_add / 10);
            }
            break;

        case CLASS_RANGER:
            if (player->stats.value[PSTAT_STR].full > 15 &&
                player->stats.value[PSTAT_INT].full > 15 &&
                player->stats.value[PSTAT_WIS].full > 15) {
                new_exp = exp_to_add + (exp_to_add / 10);
            }
            break;

        case CLASS_MAGIC_USER:
            if (player->stats.value[PSTAT_INT].full > 15) {
                new_exp = exp_to_add + (exp_to_add / 10);
            }
            break;

        case CLASS_THIEF:
            if (player->stats.value[PSTAT_DEX].full > 15) {
                new_exp = exp_to_add + (exp_to_add / 10);
            }
            break;

        default:
            /* Two classes share the experience and three split it three ways.
             * The tests are the original's, redundancy and all: MC_F_T is inside
             * the range tested just before it, and MC_MU_T, which is the last of
             * the multi-class ids, matches none of them and so takes a full
             * share. */
            if (player->cls == CLASS_MC_C_F ||
                (player->cls >= CLASS_MC_C_R && player->cls <= CLASS_MC_F_T) ||
                player->cls == CLASS_MC_F_T) {
                new_exp = exp_to_add / 2;
            } else if (player->cls == CLASS_MC_C_F_M ||
                       player->cls == CLASS_MC_F_MU_T) {
                new_exp = exp_to_add / 3;
            }
            break;
        }

        player->exp += new_exp;
    }
}

/* ------------------------------------------------------- settling everybody */

/* ovr006.affects_array. What a fight hangs on people that has no business
 * outlasting it: the charms and the holds, the clouds they were standing in, and
 * the two unnamed ones. Everything else - poison, disease, a blessing - is kept
 * and goes on ticking down on the clock. */
static const Affects AFTERCOMBAT_AFFECTS[] = {
    AFFECT_STICKS_TO_SNAKES,
    AFFECT_CHARM_PERSON,
    AFFECT_REDUCE,
    AFFECT_SILENCE_15_RADIUS,
    AFFECT_SPIRITUAL_HAMMER,
    AFFECT_FUMBLING,
    AFFECT_CONFUSE,
    AFFECT_IN_STINKING_CLOUD,
    AFFECT_SNAKE_CHARM,
    AFFECT_PARALYZE,
    AFFECT_SLEEP,
    AFFECT_CLEAR_MOVEMENT,
    AFFECT_IN_CLOUD_KILL,
    AFFECT_ENTANGLE,
    AFFECT_89,
    AFFECT_8B,
    AFFECT_FEAR,
    AFFECT_OWLBEAR_HUG_ROUND_ATTACK,
    AFFECT_HELPLESS
};

void aftercombat_cleanup_players_state(void)
{
    bool no_exp = false;

    gbl.party_animated_count = 0;
    gbl.party_killed = true;
    gbl.party_fled   = false;

    /* The party is the front of the team list and the first character with an
     * action record marked nonTeamMember is where it ends, so every loop here
     * that only concerns the party stops there. */
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        if (player->actions != NULL && player->actions->non_team_member == true) {
            break;
        }

        if (player->health_status == STATUS_RUNNING) {
            gbl.party_fled = true;
        }
    }

    /* Anybody at all, either side, who is out of the fight without being dead.
     * A duel that ends that way is not a party wipe. */
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        if (player->in_combat == true ||
            player->health_status == STATUS_UNCONSCIOUS ||
            player->health_status == STATUS_RUNNING ||
            player->health_status == STATUS_DYING) {
            no_exp = true;
            break;
        }
    }

    if (gbl.combat_type == COMBAT_TYPE_DUEL ||
        (gbl.area2_ptr->is_duel == true && no_exp == true)) {
        gbl.party_killed = false;
    }

    gbl.battle_won = false;

    /* The condition is the original's: a normal fight always settles up, and a
     * duel does so as well unless it is being fought in the demo. */
    if (gbl.combat_type == COMBAT_TYPE_NORMAL || gbl.in_demo == false) {
        for (int i = 0; i < gbl.team_count; i++) {
            Player *player = gbl.team_list[i];

            if (player == NULL) {
                continue;
            }

            if (player->actions != NULL &&
                player->actions->non_team_member == true) {
                /* Past the party, which is the first six characters. */
                break;
            }

            /* One of our own still on their feet, and not an NPC, is what says
             * the party was not destroyed. */
            if (player->health_status == STATUS_RUNNING ||
                player->health_status == STATUS_ANIMATED ||
                player->health_status == STATUS_OKEY) {
                if (player->combat_team == TEAM_OURS &&
                    player->control_morale < CONTROL_NPC_BASE) {
                    gbl.party_killed = false;
                }
            }

            /* Somebody who is up and did not run is a win, even if that somebody
             * is an animated corpse or an NPC. */
            if (player->health_status == STATUS_ANIMATED ||
                player->health_status == STATUS_OKEY) {
                gbl.battle_won = true;
                gbl.party_fled = false;
            }

            /* Who is not taking a share of the experience: the fallen, and the
             * animated, who are past caring. */
            if (player->in_combat == false ||
                player->health_status == STATUS_ANIMATED) {
                gbl.party_animated_count++;
            }

            for (size_t a = 0; a < COAB_ARRAY_LEN(AFTERCOMBAT_AFFECTS); a++) {
                effect_remove_affect(NULL, AFTERCOMBAT_AFFECTS[a], player);
            }
        }

        if (gbl.battle_won == true) {
            gbl.exp_to_add = aftercombat_calc_battle_exp();
            aftercombat_add_exp(gbl.exp_to_add);
        }

        if (gbl.party_killed == false) {
            Player *to_remove[GBL_TEAM_LIST_MAX];
            int remove_count = 0;

            for (int i = 0; i < gbl.team_count; i++) {
                Player *player = gbl.team_list[i];

                if (player == NULL) {
                    continue;
                }

                if (player->actions != NULL &&
                    player->actions->non_team_member == true) {
                    break;
                }

                if (gbl.party_fled == false) {
                    switch (player->health_status) {
                    case STATUS_RUNNING:
                        /* Ran, but the party stayed and won: they come back. */
                        player->health_status = STATUS_OKEY;
                        player->in_combat     = true;
                        break;

                    case STATUS_DYING:
                        if (gbl.area2_ptr->is_duel == true) {
                            /* Nobody dies in a duel. */
                            player->health_status   = STATUS_OKEY;
                            player->in_combat       = true;
                            player->hit_point_current = 1;
                        } else {
                            player->health_status = STATUS_UNCONSCIOUS;
                        }
                        break;

                    case STATUS_UNCONSCIOUS:
                        if (player->hit_point_current > 0) {
                            player->health_status = STATUS_OKEY;
                            player->in_combat     = true;
                        } else if (gbl.area2_ptr->is_duel == true) {
                            player->health_status   = STATUS_OKEY;
                            player->in_combat       = true;
                            player->hit_point_current = 1;
                        }
                        break;

                    default:
                        break;
                    }
                } else {
                    /* The party ran: the ones who ran with it are back on their
                     * feet and whoever was left behind is left behind for good. */
                    gbl.area2_ptr->field_58E = 0x81;

                    if (player->health_status == STATUS_RUNNING) {
                        player->health_status = STATUS_OKEY;
                        player->in_combat     = true;
                    } else if (remove_count < GBL_TEAM_LIST_MAX) {
                        to_remove[remove_count++] = player;
                    }
                }
            }

            for (int i = 0; i < remove_count; i++) {
                gbl.selected_player =
                    partymenu_free_current_player(to_remove[i], true, false);
            }
        } else {
            /* Everybody is gone: the party members are emptied out and the saved
             * party size with them. The loop stops at the first character
             * without an action record, as the original's did, so a party member
             * who was never in the fight is left alone. */
            Player *to_remove[GBL_TEAM_LIST_MAX];
            int remove_count = 0;

            for (int i = 0; i < gbl.team_count; i++) {
                Player *player = gbl.team_list[i];

                if (player == NULL) {
                    continue;
                }

                if (player->actions != NULL &&
                    player->actions->non_team_member == false) {
                    if (remove_count < GBL_TEAM_LIST_MAX) {
                        to_remove[remove_count++] = player;
                    }
                } else {
                    break;
                }
            }

            for (int i = 0; i < remove_count; i++) {
                gbl.selected_player =
                    partymenu_free_current_player(to_remove[i], true, false);
            }

            gbl.area2_ptr->party_size = 0;
        }
    } else {
        /* A duel in the demo: the experience is awarded once for every one of
         * our own still standing - the original's loop, and it does add up
         * several times over - and then the fallen are patched up. */
        for (int i = 0; i < gbl.team_count; i++) {
            Player *player = gbl.team_list[i];

            if (player == NULL) {
                continue;
            }

            if (player->in_combat == true &&
                player->health_status == STATUS_OKEY &&
                player->combat_team == TEAM_OURS) {
                gbl.battle_won = true;
                gbl.exp_to_add = aftercombat_calc_battle_exp();
                aftercombat_add_exp(gbl.exp_to_add);
            }
        }

        for (int i = 0; i < gbl.team_count; i++) {
            Player *player = gbl.team_list[i];

            if (player == NULL) {
                continue;
            }

            if (player->health_status == STATUS_OKEY ||
                player->health_status == STATUS_ANIMATED) {
                player->in_combat = true;
            }

            if (player->health_status == STATUS_DYING) {
                player->health_status = STATUS_UNCONSCIOUS;
            }
        }
    }
}

/* --------------------------------------------------------------- how it went */

void aftercombat_display_combat_results(int exp)
{
    char text[64];

    frames_draw_outer();

    /* gbl.byte_1AB14 is set by whatever has just been through the other side:
     * with it clear there were no enemies at all and what is on the ground is a
     * script's treasure rather than a fight's. */
    if (gbl.byte_1AB14 == true || gbl.combat_type == COMBAT_TYPE_DUEL) {
        if (gbl.party_fled == true) {
            text_display_string("The party has fled.", 0, 10, 3, 1);

            /* Running away earns nothing and leaves the treasure behind. */
            exp = 0;

            gbl_ground_items_clear();
            money_clear_all(&gbl.pooled_money);
        } else if ((gbl.combat_type == COMBAT_TYPE_DUEL &&
                    gbl.battle_won == false) ||
                   (gbl.battle_won == false && gbl.area2_ptr->is_duel == true)) {
            gbl.area2_ptr->field_58E = 0x80;
            text_display_string("You have lost the fight.", 0, 10, 3, 1);

            exp = 0;
        } else if (gbl.combat_type == COMBAT_TYPE_DUEL) {
            text_display_string("You have won the duel.", 0, 10, 3, 1);
        } else {
            text_display_string("The party has won.", 0, 10, 3, 1);
        }
    } else {
        text_display_string("The party has found Treasure!", 0, 10, 3, 1);
    }

    if (gbl.combat_type == COMBAT_TYPE_DUEL) {
        snprintf(text, sizeof(text), "The duelist receives %d", exp);
    } else {
        snprintf(text, sizeof(text), "Each character receives %d", exp);
    }

    text_display_string(text, 0, 10, 5, 1);
    text_display_string("experience points.", 0, 10, 7, 1);

    {
        const MenuColorSet plain = { 15, 15, 15 };

        prompt_display_input_simple(false, 1, plain,
                                    "press <enter>/<return> to continue", "");
    }
}

/* ----------------------------------------------------------- taking it away */

/* Cheats.sort_treasure. List<>.Sort by value, which the C# did with an unstable
 * sort; this one keeps equal-valued items in the order they were dropped, which
 * is the only difference and makes the list the same every time. */
static void sort_ground_items_by_value(void)
{
    for (int i = 1; i < gbl.ground_item_count; i++) {
        Item held = gbl.ground_items[i];
        int j = i;

        while (j > 0 && gbl.ground_items[j - 1].value > held.value) {
            gbl.ground_items[j] = gbl.ground_items[j - 1];
            j--;
        }

        gbl.ground_items[j] = held;
    }
}

void aftercombat_select_treasure(int *index, Item **out_item, char *out_key)
{
    /* Rebuilt every time round take_items_treasure's loop, and a MenuList is
     * 10K, so it is not on the stack. Nothing here is re-entered. */
    static MenuList list;
    MenuItem *chosen = NULL;
    bool redraw_menu_items = true;
    char key;

    frames_draw_outer();

    if (cheats.sort_treasure) {
        sort_ground_items_by_value();
    }

    /* The C# built each name in ground order and inserted it at the front of the
     * list, so the list came out reversed; walking the ground backwards puts it
     * in the same order, and where a name is built has no bearing on what it
     * says. */
    menu_list_clear(&list);

    for (int i = gbl.ground_item_count - 1; i >= 0; i--) {
        Item *item = &gbl.ground_items[i];

        character_item_display_name_build(false, false, 0, 0, item);
        menu_list_add_item(&list, item->name, item);
    }

    key = prompt_select_item(&chosen, index, &redraw_menu_items, true, &list,
                             0x16, 0x26, 1, 1, GBL_DEFAULT_MENU_COLORS,
                             "Take", "Items: ");

    if (out_item != NULL) {
        *out_item = (chosen != NULL) ? chosen->item : NULL;
    }
    if (out_key != NULL) {
        *out_key = key;
    }
}

void aftercombat_take_items_treasure(void)
{
    bool stop;
    int index = 0;

    do {
        Item *item = NULL;
        char key = '\0';

        aftercombat_select_treasure(&index, &item, &key);

        if (key != 'T' && key != '\r') {
            stop = true;
        } else {
            bool will_overload;

            stop = false;

            will_overload = shop_player_add_item(item);

            if (will_overload == false) {
                /* The item is on the ground, so its place there is its index;
                 * the list closes up over the gap afterwards, which is why the
                 * loop looks it up again from the start each time. */
                int at = -1;

                for (int i = 0; i < gbl.ground_item_count; i++) {
                    if (&gbl.ground_items[i] == item) {
                        at = i;
                        break;
                    }
                }

                if (at >= 0) {
                    gbl_ground_item_remove_at(at);
                }

                stop = gbl.ground_item_count == 0;
            }
        }
    } while (stop == false);

    character_load_pic();
}

void aftercombat_take_treasure(bool *items_present, bool *money_present)
{
    if (items_present == NULL || money_present == NULL) {
        log_warn("after combat: taking treasure without knowing what is there");
        return;
    }

    if (*money_present == false) {
        aftercombat_take_items_treasure();
        return;
    }

    if (*items_present == false) {
        treasure_take_pool_money();
        character_load_pic();
        return;
    }

    /* Both, so there is something to ask. */
    bool done = false;

    do {
        /* "Money" owns a typed 'M', which is also the right cursor key's scan
         * code, so left and right are spent on the highlight here rather than
         * coming back and being read as a word. */
        char key = prompt_display_input_simple(true, PROMPT_CTRL_WORD_ARROWS,
                                               GBL_DEFAULT_MENU_COLORS,
                                               "Money Items Exit", "Take: ");

        /* The overload without the flag, so the copy displayInput leaves in gbl
         * is the one to ask. An arrow walks the party; see prompt_selection_key. */
        switch (prompt_selection_key(key, gbl.display_input_special_key_pressed)) {
        case 'M':
            treasure_take_pool_money();
            character_load_pic();
            break;

        case 'I':
            aftercombat_take_items_treasure();
            break;

        case 'E':
        case '\0':
            done = true;
            break;

        case 'G':
        case 'O':
            viewplayer_scroll_team_list(key);
            break;

        default:
            break;
        }

        character_party_summary(gbl.selected_player);
        treasure_on_ground(items_present, money_present);

        /* Once one of the two is gone there is nothing left to choose between. */
        if (*money_present == false || *items_present == false) {
            done = true;
        }
    } while (done == false);
}

void aftercombat_distribute_combat_treasure(void)
{
    /* The C# left this uninitialised until a spell was found and its own port
     * noted as much; 0 is no spell, and 'D' is only offered when one was found. */
    int spell_id = 0;
    bool done = false;

    character_load_pic();

    do {
        bool items_present = false;
        bool money_present = false;
        bool can_detect_magic = false;
        bool ctrl_key = false;
        char text[64];
        const char *suffix = " Exit";
        char input_key;

        treasure_on_ground(&items_present, &money_present);

        /* Detect magic, its longer-lasting version and the clerical one: any of
         * the three lets the heap be looked over where it lies. */
        if (items_present == true && gbl.selected_player != NULL) {
            const SpellList *spells = &gbl.selected_player->spell_list;

            for (int i = 0; i < spells->count; i++) {
                int id = spells->items[i].id;

                if ((id == 5 || id == 11 || id == 0x4d) &&
                    gbl.selected_player->in_combat == true) {
                    can_detect_magic = true;
                    spell_id = id;
                    break;
                }
            }
        }

        if (can_detect_magic == true) {
            suffix = " Detect Exit";
        }

        if (money_present == true) {
            snprintf(text, sizeof(text), "View Take Pool Share%s", suffix);
        } else if (items_present == true) {
            snprintf(text, sizeof(text), "View Take Pool%s", suffix);
        } else {
            snprintf(text, sizeof(text), "View Pool Exit");
        }

        input_key = prompt_display_input(&ctrl_key, true,
                                         PROMPT_CTRL_WORD_ARROWS,
                                         GBL_DEFAULT_MENU_COLORS, text, "");

        /* "Pool" owns a typed 'P' here too, so the arrows are converted rather
         * than added as cases. */
        switch (prompt_selection_key(input_key, ctrl_key)) {
        case 'V':
            viewplayer_view_player();
            break;

        case 'T':
            aftercombat_take_treasure(&items_present, &money_present);
            break;

        case 'P':
            /* ctrl-P is the print-screen key the prompt also answers with 'P',
             * and pooling the money on it would be a surprise. */
            if (ctrl_key == false) {
                treasure_pool_money();
            }
            break;

        case 'S':
            treasure_share_pooled();
            break;

        case 'D':
            spellcast_resolve_spell(false, QUICK_FIGHT_FALSE, spell_id);
            break;

        case 'E':
        case '\0':
            treasure_on_ground(&items_present, &money_present);

            if (money_present == true || items_present == true) {
                text_press_any_key_region("There is still treasure left.  ",
                                          true, 10, TEXT_REGION_NORMAL_BOTTOM);
                text_press_any_key_region(
                    "Do you want to go back and claim your treasure?", false, 15,
                    TEXT_REGION_NORMAL_BOTTOM);

                if (vm_menu_select(false, false, GBL_DEFAULT_MENU_COLORS,
                                   "~Yes ~No", "") == 1) {
                    done = true;
                } else {
                    frames_clear_area(0x16, 0x26, 17, 1);
                }
            } else {
                done = true;
            }
            break;

        case 'G':
        case 'O':
            viewplayer_scroll_team_list(input_key);
            character_party_summary(gbl.selected_player);
            break;

        default:
            break;
        }
    } while (done == false);
}

/* --------------------------------------------- the other side goes home */

void aftercombat_deallocate_non_team_members(void)
{
    /* Who leaves, and whether they leave as somebody who was never in the party:
     * that flag is what tells FreeCurrentPlayer to leave Area2.party_size alone,
     * since a monster was never counted in it. */
    Player *to_remove[GBL_TEAM_LIST_MAX];
    bool    was_non_team[GBL_TEAM_LIST_MAX];
    int     remove_count = 0;

    gbl.area2_ptr->field_590 = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];
        bool check;

        if (player == NULL) {
            continue;
        }

        check = (player->actions != NULL &&
                 player->actions->non_team_member == true);

        if (check || player->combat_team == TEAM_ENEMY) {
            gbl.byte_1AB14 = true;

            if (player->in_combat == false) {
                gbl.area2_ptr->field_590++;
            }

            if (remove_count < GBL_TEAM_LIST_MAX) {
                to_remove[remove_count]   = player;
                was_non_team[remove_count] = check;
                remove_count++;
            } else {
                log_warn("after combat: no room to send %s home", player->name);
            }
        } else if (player->actions != NULL) {
            /* Out of a fight nobody has an action record. What it points at
             * belongs to whatever set the fight up. */
            player->actions = NULL;
        }
    }

    for (int i = 0; i < remove_count; i++) {
        partymenu_free_current_player(to_remove[i], true, was_non_team[i]);
    }

    gbl.selected_player = (gbl.team_count > 0) ? gbl.team_list[0] : NULL;
}

void aftercombat_distribute_npc_treasure(void)
{
    bool treasure_taken = false;
    int npc_parts   = 0;
    int total_parts = 0;

    /* An NPC's share count is the low three bits of the field; everybody else
     * counts for one. */
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        if (player->control_morale >= CONTROL_NPC_BASE &&
            player->health_status == STATUS_OKEY) {
            npc_parts   += player->npc_treasure_share_count & 7;
            total_parts += player->npc_treasure_share_count & 7;
        } else {
            total_parts++;
        }
    }

    if (npc_parts > 0) {
        /* Both are ints and the division is an integer one, which is what the
         * original did and what the C# kept: the answer is 1 only when the party
         * is nothing but NPCs, and 0 otherwise. Scaling the pool by 0 empties
         * it, so an NPC who takes a share takes all of the coin. Kept as it is,
         * bug and all - it is what the game does. */
        treasure_taken = money_scale_all(&gbl.pooled_money,
                                         npc_parts / total_parts);
    }

    if (treasure_taken) {
        int y_col = 0;

        frames_draw_outer();

        for (int i = 0; i < gbl.team_count; i++) {
            Player *player = gbl.team_list[i];
            char output[80];

            if (player == NULL) {
                continue;
            }

            if (player->control_morale >= CONTROL_NPC_BASE &&
                player->health_status == STATUS_OKEY &&
                player->npc_treasure_share_count > 0) {
                snprintf(output, sizeof(output),
                         "%s takes and hides %s share.", player->name,
                         (player->sex == 0) ? "his" : "her");

                text_press_any_key(output, true, 10, 0x16, 0x22, y_col + 5, 5);

                y_col += 2;
            }
        }

        {
            const MenuColorSet plain = { 15, 15, 15 };

            prompt_display_input_simple(false, 1, plain,
                                        "press <enter>/<return> to continue",
                                        "");
        }
    }
}

/* ------------------------------------------------------------- the whole of it */

void aftercombat_exp_and_treasure(void)
{
    gbl.area2_ptr->field_58E = 0;
    gbl.byte_1AB14 = false;

    if (gbl.in_demo == false) {
        aftercombat_cleanup_players_state();
    }

    gbl.game_state = GAME_STATE_AFTER_COMBAT;

    aftercombat_deallocate_non_team_members();

    if (gbl.in_demo == true) {
        return;
    }

    for (int i = 0; i < gbl.team_count; i++) {
        if (gbl.team_list[i] != NULL) {
            character_recalc_values(gbl.team_list[i]);
        }
    }

    if (gbl.party_killed == false || gbl.combat_type == COMBAT_TYPE_DUEL) {
        if (gbl.party_fled == true) {
            /* Whatever the fight dropped stays where it fell. */
            gbl_ground_items_clear();
        }

        /* The inner test is the original's, and it cannot fail here. */
        if (gbl.in_demo == false) {
            aftercombat_distribute_npc_treasure();
            aftercombat_display_combat_results(gbl.exp_to_add);
            aftercombat_distribute_combat_treasure();
        }

        gbl_ground_items_clear();
    } else {
        gbl.area2_ptr->field_58E = 0x80;

        frames_draw_outer();
        gbl.text_x_col = 2;
        gbl.text_y_col = 6;
        text_press_any_key("The monsters rejoice for the party has been "
                           "destroyed", true, 10, 0x16, 0x25, 5, 2);
        text_display_and_pause("Press any key to continue", 13);
    }

    gbl.delay_between_characters = true;
    gbl.area2_ptr->field_6E0 = 0;
    gbl.area2_ptr->field_6E2 = 0;
    gbl.area2_ptr->field_6E4 = 0;
    gbl.area2_ptr->field_5C6 = 0;
    gbl.area2_ptr->is_duel   = false;
}
