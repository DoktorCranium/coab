/* combatloop.c - Ported from engine/ovr009.cs. See combatloop.h. */

#include "combatloop.h"

#include <stdio.h>
#include <string.h>

#include "attack.h"
#include "battlesetup.h"
#include "character.h"
#include "combat.h"
#include "combatmap.h"
#include "dax.h"
#include "effect.h"
#include "frames.h"
#include "gbl.h"
#include "input.h"
#include "log.h"
#include "monsterai.h"
#include "prompt.h"
#include "resting.h"
#include "set.h"
#include "spellcast.h"
#include "spelllist.h"
#include "tile.h"
#include "viewplayer.h"

/* The C#'s round loop ends when no combatant has any delay left, and every path
 * through a turn arranges that. A bug that left one standing would hang the game
 * with no way out, so the round is bounded and says so once when the bound is
 * reached. Two hundred and fifty six turns is well past the seventy one
 * combatants a fight can hold, each of whom acts once.
 *
 * The whole fight needs no bound of its own: BattleRoundChecks counts the rounds
 * and stops at gbl.combat_round_no_action_limit. */
#define COMBATLOOP_TURNS_MAX 256

/* ------------------------------------------------------------ the menu text */

/* The C# built its prompts by adding to a string. This appends one word and
 * keeps the buffer terminated; a prompt that would not fit says so, where the
 * C#'s string simply grew. */
static void menu_append(char *dst, size_t dst_size, const char *word)
{
    size_t len = strlen(dst);
    size_t add = strlen(word);

    if (len + add + 1 > dst_size) {
        log_warn("combat: no room in the menu prompt for \"%s\"", word);
        return;
    }

    memcpy(dst + len, word, add + 1);
}

/* ---------------------------------------------------------- a fight is over */

/* ovr009.free_combat_stuff, sub_3304B */
void combatloop_free_combat_stuff(void)
{
    gbl.stinking_cloud_count = 0;
    gbl.cloud_kill_count = 0;

    /* The ground map itself belongs to battlesetup.c, which hands out the same
     * one every fight; the C# dropped its reference here and left the rest to
     * its collector. Everything that touches the map checks for NULL, which is
     * how the combat code tells a fight from anything else. */
    gbl.map_to_background_tile = NULL;

    dax_block_free(gbl.missile_dax);
    gbl.missile_dax = NULL;

    combatmap_color_0_8_normal();
    gbl.spell_cast_function = spellcast_non_combat_cast;
}

/* --------------------------------------------------------- the turn order */

/* ovr009.FindNextCombatant, sub_331BC. Whoever still has the longest delay left
 * goes next, with a d100 to break ties - and, because of where the roll is
 * compared, sometimes to break a lead as well: a combatant with a longer delay
 * takes over the previous best roll rather than a fresh one, so a low roll can
 * keep them out of the running for this pass. That is the original's own
 * arithmetic and it is kept.
 *
 * The dice are rolled for everyone whether the roll can matter or not, which is
 * what the sequence of rolls the rest of the round sees depends on.
 *
 * The C# was an iterator yielding one combatant at a time; here the caller loops
 * until this returns NULL, which is the same thing. */
static Player *find_next_combatant(void)
{
    Player *output_player = NULL;
    int max_delay = 0;
    int max_roll = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];
        int roll;
        int delay;

        if (player == NULL) {
            continue;
        }

        roll  = effect_roll_dice(100, 1);
        delay = player_actions(player)->delay;

        if (delay > max_delay) {
            max_roll = roll;
        }

        if (delay >= max_delay && roll >= max_roll) {
            max_roll  = roll;
            max_delay = delay;

            output_player = player;
        }
    }

    /* Nobody has any delay left, so the round is over. */
    if (max_delay == 0) {
        output_player = NULL;
    }

    return output_player;
}

/* ------------------------------------------------------------ a whole fight */

/* ovr009.MainCombatLoop, sub_33100 */
void combatloop_main_combat_loop(void)
{
    bool end_combat = false;

    gbl.game_state = GAME_STATE_COMBAT;
    gbl.spell_cast_function = attack_spell_targets;
    battlesetup_battle_setup();

    if (gbl.friends_count == 0 ||
        gbl.foe_count == 0) {
        end_combat = true;
    }

    while (end_combat == false) {
        Player *player;
        int turns = 0;

        character_count_combat_teams();

        for (int i = 0; i < gbl.team_count; i++) {
            if (gbl.team_list[i] != NULL) {
                attack_calculate_initiative(gbl.team_list[i]);
            }
        }

        /* Whatever surprise there was is spent on the first round's delays. */
        gbl.area2_ptr->field_596 = 0;

        while ((player = find_next_combatant()) != NULL) {
            combatloop_do_player_combat_turn(player);

            if (++turns >= COMBATLOOP_TURNS_MAX) {
                log_warn("combat: round %d is still going after %d turns, "
                         "ending it", gbl.combat_round, turns);
                break;
            }
        }

        end_combat = combatloop_battle_round_checks();
    }

    combatloop_free_combat_stuff();
    gbl.delay_between_characters = true;
}

/* ovr009.DoPlayerCombatTurn, sub_33281 */
void combatloop_do_player_combat_turn(Player *player)
{
    Action *actions;

    if (player == NULL) {
        log_warn("combat: a turn taken by nobody");
        return;
    }

    actions = player_actions(player);

    actions->attacks_received  = 0;
    actions->direction_changes = 0;
    actions->guarding          = false;
    effect_check_affects(player, CHECK_TYPE_PLAYER_RESTRAINED);

    if (actions->delay > 0) {
        /* A delay of 20 is the mark ctrl-P leaves on the combatant it was
         * pressed on, which puts them first in the order; it is knocked back to
         * 19 so this is the turn they spend it on. */
        if (actions->delay == 20) {
            actions->delay = 19;
        }

        gbl.selected_player = player;

        /* The view follows our own side wherever they are, and the other side
         * only while they are on screen. */
        gbl.focus_combat_area_on_player =
            (player->combat_team == TEAM_OURS) ||
            combatmap_player_on_screen_p(false, player);

        combatmap_redraw_if_focus_on(true, 2, player);
        character_recalc_values(player);
        gbl.display_hitpoints_ac = true;
        character_combat_display_summary(player);
        effect_check_affects(player, CHECK_TYPE_15);

        /* Confusion only gets its say over a combatant who is not already in the
         * middle of casting something. */
        if (actions->spell_id == 0) {
            effect_check_affects(player, CHECK_TYPE_CONFUSION);
        }

        /* Either of those two can have ended the turn. */
        if (actions->delay > 0) {
            if (player->quick_fight == QUICK_FIGHT_TRUE) {
                monsterai_player_quick_fight(player);
            } else {
                combatloop_combat_menu(player);
            }
        }

        combatmap_redraw_position(combatmap_player_map_pos(player));
    }
}

/* ----------------------------------------------------------- the menu keys */

/* ovr009.unk_33748 and unk_33768. The first is the cursor and control keys the
 * menu takes notice of - the eight directions, ctrl-P, Return, '-' and '2' - and
 * the second is every key the menu will accept at all, which is those plus the
 * letters of its own words and the space bar.
 *
 * The C# built both as file statics; C has no static initialiser for a Set, so
 * they are built once on the first prompt. */
static Set menu_ctrl_keys;
static Set menu_all_keys;
static bool menu_keys_built = false;

static void build_menu_keys(void)
{
    static const u8 CTRL_KEYS[] = {
        16, 19, 45, 50, 71, 72, 73, 75, 77, 79, 80, 81
    };
    static const u8 ALL_KEYS[] = {
        16, 19, 32, 45, 50, 65, 67, 68, 71, 72, 73, 75, 77, 79, 80, 81, 84, 85,
        86
    };

    set_clear(&menu_ctrl_keys);
    for (size_t i = 0; i < COAB_ARRAY_LEN(CTRL_KEYS); i++) {
        set_add(&menu_ctrl_keys, CTRL_KEYS[i]);
    }

    set_clear(&menu_all_keys);
    for (size_t i = 0; i < COAB_ARRAY_LEN(ALL_KEYS); i++) {
        set_add(&menu_all_keys, ALL_KEYS[i]);
    }

    menu_keys_built = true;
}

/* ovr009.combat_menu, the overload that only asks. The prompt is built out of
 * what this combatant can still do, and nothing but a key it lists - or Escape,
 * or one of the cursor keys - gets out of the loop. */
static char combat_menu_prompt(Player *player)
{
    char menu_text[80];
    char key = '\0';

    if (menu_keys_built == false) {
        build_menu_keys();
    }

    menu_text[0] = '\0';

    if (player_actions(player)->move > 0) {
        menu_append(menu_text, sizeof(menu_text), "Move ");
    }

    menu_append(menu_text, sizeof(menu_text), "View Aim ");

    if (player->item_count > 0) {
        menu_append(menu_text, sizeof(menu_text), "Use ");
    }

    if (spell_list_has_spells(&player->spell_list) &&
        player_actions(player)->can_cast &&
        gbl.area_ptr->can_cast_spells == false) {
        menu_append(menu_text, sizeof(menu_text), "Cast ");
    }

    if (player_skill_level(player, SKILL_CLERIC) > 0 &&
        player_actions(player)->has_turned_undead == false) {
        menu_append(menu_text, sizeof(menu_text), "Turn ");
    }

    menu_append(menu_text, sizeof(menu_text), "Quick Done");

    do {
        bool ctrl_key = false;

        key = prompt_display_input(&ctrl_key, false, 1, GBL_DEFAULT_MENU_COLORS,
                                   menu_text, "");

        /* A cursor key that means nothing here is thrown away, which is what
         * lets 'P' the direction through and 'P' the letter be ignored. */
        if (ctrl_key && set_member_of(&menu_ctrl_keys, key) == false) {
            key = '\0';
        }
    } while (set_member_of(&menu_all_keys, key) == false);

    prompt_clear_area();

    return key;
}

/* ovr009.combat_menu, camp_menu */
void combatloop_combat_menu(Player *player)
{
    DownedPlayerTile aim;
    Action *actions;

    if (player == NULL) {
        log_warn("combat: a menu for nobody");
        return;
    }

    downed_player_tile_clear(&aim);
    actions = player_actions(player);

    if (player->in_combat == false) {
        character_clear_actions(player);
        return;
    }

    if (actions->spell_id > 0) {
        /* A spell begun last round goes off now, and that is the whole turn. */
        int spell_id = actions->spell_id;

        actions->spell_id = 0;

        spellcast_resolve_spell(true, QUICK_FIGHT_FALSE, spell_id);
        character_clear_actions(player);

        return;
    }

    bool turn_ended = false;

    while (turn_ended == false) {
        char key = combat_menu_prompt(player);

        if (gbl.display_input_special_key_pressed == false) {
            switch (key) {
            case 'Q':
                /* Handed to the AI, and it takes this turn as well. */
                combatloop_set_player_quick_fight(player);
                prompt_clear_area();
                input_clear_keyboard();
                input_sys_delay(0x0c8);
                turn_ended = true;
                monsterai_player_quick_fight(player);
                break;

            case 'M':
                combatloop_move_menu(&turn_ended, ' ', player);
                break;

            case 'V':
                turn_ended = viewplayer_view_player();
                attack_recalc_attacks(player);
                if (turn_ended == false) {
                    character_redraw_combat_screen();
                }
                break;

            case 'A':
                turn_ended = attack_aim_menu(&aim, true, false, true, -1, player);
                break;

            case 'U':
                gbl.menu_selected_word = 2;
                viewplayer_items_menu(&turn_ended);
                attack_recalc_attacks(player);
                if (turn_ended == false) {
                    character_redraw_combat_screen();
                }
                break;

            case 'C':
                attack_spell_menu(&turn_ended, QUICK_FIGHT_FALSE, 0);
                break;

            case 'T':
                attack_turn_undead(player);
                turn_ended = true;
                character_clear_actions(player);
                break;

            case 'D':
                combatloop_delay_menu(&turn_ended, player);
                break;

            case ' ':
                /* Takes the whole party off quick fight; the monsters and the
                 * NPCs, whose morale is their own, stay on it. */
                for (int i = 0; i < gbl.team_count; i++) {
                    Player *p = gbl.team_list[i];

                    if (p != NULL && p->control_morale < CONTROL_NPC_BASE) {
                        p->quick_fight = QUICK_FIGHT_FALSE;
                    }
                }
                break;

            default:
                break;
            }
        } else {
            switch (key) {
            /* The eight cursor keys, by scan code: the move menu takes the
             * direction straight from the key that was pressed. */
            case 'G':
            case 'H':
            case 'K':
            case 'M':
            case 'O':
            case 'P':
            case 'Q':
            case 'I':
                combatloop_move_menu(&turn_ended, key, player);
                break;

            case '2':
                gbl.auto_pcs_cast_magic = !gbl.auto_pcs_cast_magic;

                if (gbl.auto_pcs_cast_magic) {
                    character_print_message("Magic On");
                } else {
                    character_print_message("Magic Off");
                }
                break;

            case (char)0x10:
                /* Ctrl-P: the whole party goes on quick fight, and this
                 * combatant goes to the front of the order to do it. */
                actions->delay = 20;

                for (int i = 0; i < gbl.team_count; i++) {
                    if (gbl.team_list[i] != NULL) {
                        combatloop_set_player_quick_fight(gbl.team_list[i]);
                    }
                }

                prompt_clear_area();
                input_sys_delay(0x0c8);

                turn_ended = true;
                break;

            case '-':
                if (attack_god_intervene()) {
                    combatmap_redraw_if_focus_on(false, 3, player);
                    turn_ended = true;
                } else {
                    character_print_message("That doesn't work");
                }
                break;

            default:
                break;
            }
        }

        if (turn_ended == false) {
            combatmap_redraw_if_focus_on(true, 2, player);
            gbl.display_hitpoints_ac = true;
            character_combat_display_summary(player);
        }
    }
}

/* ------------------------------------------------------- the end of a round */

/* ovr009.BattleRoundChecks, battle01 */
bool combatloop_battle_round_checks(void)
{
    bool battle_over = false;

    resting_step_game_time(1, 1);
    gbl.combat_round += 1;
    attack_calc_enemy_health_percentage();

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        effect_check_affects(player, CHECK_TYPE_19);
        effect_in_poison_cloud(0, player);

        /* Ten rounds of bleeding is as long as anyone lasts. */
        if (player->health_status == STATUS_DYING) {
            Action *actions = player_actions(player);

            actions->bleeding += 1;

            if (actions->bleeding > 9) {
                player->health_status = STATUS_DEAD;
            }
        }
    }

    if (character_bandage(false)) {
        character_print_message("Your Teammate is Dying");
    }

    character_count_combat_teams();

    if (gbl.map_to_background_tile != NULL) {
        combatmap_redraw_area(8, 0xff,
                              point_add(gbl.map_to_background_tile->map_screen_top_left,
                                        point_screen_center()));
    } else {
        /* The C# would have thrown; a round that ends with no map to draw is a
         * fight that has already been torn down. */
        log_warn("combat: the round ends with no combat map to redraw");
    }

    if (gbl.friends_count == 0 ||
        gbl.foe_count == 0 ||
        gbl.combat_round >= gbl.combat_round_no_action_limit) {
        battle_over = true;
    }

    /* With the monsters all down and more than one of ours still standing, the
     * player is asked whether to keep fighting - which is how a party finishes
     * off its own charmed members. */
    if (gbl.friends_count > 1 &&
        gbl.foe_count == 0 &&
        gbl.in_demo == false &&
        prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Continue Battle:") == 'Y') {
        battle_over = false;
    }

    return battle_over;
}

/* ------------------------------------------------------------------ moving */

/* The direction each cursor key's scan code stands for, and 8 - no direction -
 * for anything else. */
static int move_key_direction(char key)
{
    switch (key) {
    case 'H': return 0;
    case 'I': return 1;
    case 'M': return 2;
    case 'Q': return 3;
    case 'P': return 4;
    case 'O': return 5;
    case 'K': return 6;
    case 'G': return 7;
    default:  return 8;
    }
}

/* ovr009.sub_33B26 */
void combatloop_move_menu(bool *turn_ended, char first_key, Player *player)
{
    Action *actions;
    int moves_backup;
    int dir_backup;
    Point pos;
    char key = first_key;
    bool ended = false;

    if (player == NULL) {
        log_warn("combat: a move menu with nobody moving");
        return;
    }

    actions      = player_actions(player);
    moves_backup = actions->move;
    dir_backup   = actions->direction;
    pos          = combatmap_player_map_pos(player);

    /* Movement is counted in halves, so anything under two is not a step. */
    while (actions->move > 1 && key != '\0' && key != 13) {
        int dir;

        frames_clear_area(0x18, 0x27, 0x18, 0x18);

        if (key == ' ') {
            /* The move prompt is drawn in its own colours rather than the
             * menu's, and has no words to pick: every key is a direction. */
            const MenuColorSet move_colors = { 15, 10, 10 };
            char text[64];

            snprintf(text, sizeof(text), "Move/Attack, Move Left = %d ",
                     actions->move / 2);

            key = prompt_display_input_simple(false, 1, move_colors, "", text);
        }

        if (key == '\0') {
            /* Escape puts the combatant back where the move started, with the
             * movement it started with. */
            actions->move = moves_backup;

            combatmap_redraw_player_background(combatmap_player_index(player));

            /* Nowhere to put them back is the one thing that ends the turn
             * here. */
            ended = combatmap_place_combatant(false, pos, player) == false;

            combatmap_redraw_area(8, 0, combatmap_player_map_pos(player));
            actions->direction = dir_backup;
            dir = 8;
        } else {
            dir = move_key_direction(key);
        }

        if (dir < 8) {
            int ground_tile = 0;
            int target_index = 0;

            combatmap_draw_player(false, COMBAT_ICON_NORMAL, dir, player);
            combatmap_ground_information(&ground_tile, &target_index, dir,
                                        player);

            if (target_index > 0) {
                if (target_index > GBL_MAX_COMBATANT_COUNT) {
                    log_warn("combat: no combatant %d to walk into",
                             target_index);
                } else {
                    combatloop_move_into_target(&ended,
                                                gbl.player_array[target_index],
                                                player);
                }
            } else if (ground_tile == 0) {
                /* Ground tile 0 is off the map, which is the way out. */
                char answer = prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Flee:");

                if (answer == 'Y') {
                    ended = true;
                    attack_flee_battle(player);
                } else if (answer == 'N') {
                    ended = false;
                }
            } else {
                const BackgroundTile *tile = background_tile(ground_tile);
                int move_cost;

                if (tile == NULL) {
                    /* The C# indexed the table and would have thrown. */
                    move_cost = 0xffff;
                } else if ((dir / 2) < 1) {
                    /* Movement is counted in halves so that a diagonal can cost
                     * one and a half steps - but the test the original uses here
                     * is `dir / 2 < 1`, which charges three halves for north and
                     * north-east and two for everything else, diagonals
                     * included. engine/ovr010.cs charges the AI by whether the
                     * direction is odd, which is the rule the arithmetic was
                     * meant to express. The player's own steps are left costing
                     * what they cost in the original. */
                    move_cost = tile->move_cost * 3;
                } else {
                    move_cost = tile->move_cost * 2;
                }

                if (tile != NULL && tile->move_cost == 0xff) {
                    move_cost = 0xffff;
                }

                if (move_cost > actions->move) {
                    character_print_message("can't go there");
                } else {
                    /* Anyone whose reach the combatant is leaving gets a
                     * parting swing before the step is taken. */
                    attack_move_step_away(dir, player);

                    if (player->in_combat == false) {
                        ended = true;
                        character_clear_actions(player);
                    } else {
                        if (actions->move > 0) {
                            attack_move_step(dir, player);
                        }

                        if (player->in_combat == false) {
                            ended = true;
                            character_clear_actions(player);
                        }

                        effect_in_poison_cloud(1, player);

                        if (player_is_held(player)) {
                            ended = true;
                            character_clear_actions(player);
                        }
                    }
                }
            }
        }

        /* Every step after the first asks for its own direction. */
        if (key != '\0' && key != 13) {
            key = ' ';
        }
    }

    if (actions->move < 2) {
        actions->move = 0;
    }

    if (turn_ended != NULL) {
        *turn_ended = ended;
    }
}

/* ovr009.sub_33F03 */
void combatloop_move_into_target(bool *turn_ended, Player *target,
                                 Player *player)
{
    if (target == NULL || player == NULL) {
        log_warn("combat: walking into nobody");
        return;
    }

    if (character_is_weapon_ranged(player) &&
        character_is_weapon_ranged_melee(player) == false) {
        character_print_message("Not with that weapon");
    } else if (attack_can_attack_target(target, player)) {
        player_actions(player)->target = target;

        if (attack_try_sweep(target, player)) {
            if (turn_ended != NULL) {
                *turn_ended = true;
            }
        } else {
            attack_recalc_attacks_received(target, player);

            if (turn_ended != NULL) {
                *turn_ended = attack_target(NULL, 0, target, player);
            } else {
                attack_target(NULL, 0, target, player);
            }
        }
    }
}

/* ------------------------------------------------------------------- Done */

/* ovr009.delay_menu */
void combatloop_delay_menu(bool *turn_ended, Player *player)
{
    char menu_text[64];
    char key = ' ';
    bool ended = false;

    if (turn_ended != NULL) {
        *turn_ended = false;
    }

    if (player == NULL) {
        log_warn("combat: a delay menu for nobody");
        return;
    }

    menu_text[0] = '\0';

    /* Standing guard is swinging at whoever comes near, which a bow cannot do. */
    if (character_is_weapon_ranged(player) == false ||
        character_is_weapon_ranged_melee(player)) {
        menu_append(menu_text, sizeof(menu_text), "Guard ");
    }

    menu_append(menu_text, sizeof(menu_text), "Delay Quit ");

    if (character_bandage(false)) {
        menu_append(menu_text, sizeof(menu_text), "Bandage ");
    }

    menu_append(menu_text, sizeof(menu_text), "Speed Exit");

    while (key != '\0' && key != 'E' && ended == false) {
        key = prompt_display_input_simple(false, 0, GBL_DEFAULT_MENU_COLORS,
                                         menu_text, "");

        switch (key) {
        case 'G':
            character_guarding(player);
            ended = true;
            break;

        case 'D':
            /* A delay of one is the back of the order: everyone else goes
             * first and this combatant acts again after them. */
            player_actions(player)->delay = 1;
            ended = true;
            break;

        case 'Q':
            character_clear_actions(player);
            ended = true;
            break;

        case 'B':
            character_bandage(true);
            character_clear_actions(player);
            ended = true;
            break;

        case 'S':
            combatloop_set_gamespeed();
            break;

        default:
            break;
        }
    }

    if (turn_ended != NULL) {
        *turn_ended = ended;
    }
}

/* ovr009.set_gamespeed */
void combatloop_set_gamespeed(void)
{
    char key = ' ';

    while (key != '\0' && key != 'E') {
        char menu_text[32];
        char text[32];

        snprintf(text, sizeof(text), "GameSpeed (%d) :", gbl.game_speed_var);

        menu_text[0] = '\0';
        menu_append(menu_text, sizeof(menu_text), " ");

        /* Nine is as slow as it goes and zero as fast. */
        if (gbl.game_speed_var < 9) {
            menu_append(menu_text, sizeof(menu_text), "Slower ");
        }

        if (gbl.game_speed_var > 0) {
            menu_append(menu_text, sizeof(menu_text), "Faster ");
        }

        menu_append(menu_text, sizeof(menu_text), "Exit");

        key = prompt_display_input_simple(false, 0, GBL_DEFAULT_MENU_COLORS,
                                         menu_text, text);

        if (key == 'S') {
            gbl.game_speed_var += 1;
        } else if (key == 'F') {
            gbl.game_speed_var -= 1;
        }
    }
}

/* ovr009.SetPlayerQuickFight, sub_3432F */
void combatloop_set_player_quick_fight(Player *player)
{
    Action *actions;

    if (player == NULL) {
        log_warn("combat: nobody to hand over to the AI");
        return;
    }

    player->quick_fight = QUICK_FIGHT_TRUE;
    actions = player_actions(player);

    if (actions->target != NULL &&
        actions->target->combat_team == player->combat_team) {
        actions->target = NULL;
    }
}
