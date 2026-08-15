/* monsterai.c - Ported from engine/ovr010.cs. See monsterai.h. */

#include "monsterai.h"

#include <stdio.h>

#include "attack.h"
#include "character.h"
#include "combat.h"
#include "combatmap.h"
#include "effect.h"
#include "gbl.h"
#include "input.h"
#include "item.h"
#include "log.h"
#include "prompt.h"
#include "spellcast.h"
#include "spells.h"
#include "target.h"
#include "text.h"
#include "tile.h"
#include "viewplayer.h"

/* gbl.max_spells, Classes/Gbl.cs. How many memorized spells the AI will look
 * through; the C# sized its scratch list with it. A character can hold more than
 * this - SpellList has room for the ones still being learnt as well - so the
 * copy below stops rather than running off the end, which is what the C# array
 * would have thrown on. */
#define MONSTERAI_MAX_SPELLS 0x54

/* The C#'s loops here are driven by a delay counting down and by movement being
 * spent, both of which a bug can leave standing still. Each is bounded and says
 * so once when the bound is reached, rather than hanging the game. */
#define MONSTERAI_ESCAPE_STEPS_MAX 64
#define MONSTERAI_TURN_PASSES_MAX  64

/* The five routines the C# kept private. Two of them are reached before they are
 * defined, as they were in the C#. */
static bool process_input_in_monsters_turn(Player *player);
static void try_guarding(Player *player);
static bool flee_check(Player *player);
static int  calc_item_power_rating(Item *item, Player *player);
static void ai_items_selection(Player *player);


/* ------------------------------------------------------ movement patterns */

/* ovr010.data_2B8, seg600:02BD..02F8. Eleven movement patterns of six steps
 * each: step n of pattern p is the direction, added to the one the combatant
 * wants to go in, that it tries n-th. Pattern 0 goes straight on, then sidles
 * left, then right; the later ones sweep further round.
 *
 * The C# comment records that the original table runs two entries past this -
 * {4, 2, 6, 6} - which is a twelfth pattern the code never indexes. It is left
 * out here as it was there. */
static const int DATA_2B8[11][6] = {
    { 8, 7, 6, 1, 2, 8 }, { 8, 1, 2, 7, 6, 7 }, { 7, 1, 8, 6, 2, 1 },
    { 1, 7, 8, 2, 6, 8 }, { 8, 7, 6, 5, 4, 8 }, { 8, 1, 2, 3, 4, 8 },
    { 8, 4, 6, 2, 8, 6 }, { 6, 4, 0, 8, 0, 6 }, { 6, 2, 8, 2, 0, 4 },
    { 4, 0, 0, 2, 6, 2 }, { 2, 2, 0, 4, 4, 4 }
};

/* Action.field_15 is set from dice rolls and from (field_15 % 6) + 1, so it
 * always lands inside the table; dir_step comes from a loop that stops at 6. The
 * C# indexed straight in and would have thrown, so anything outside says so and
 * reads as 8 - the no-move direction. */
static int dir_step_offset(int field_15, int dir_step)
{
    if (field_15 < 0 || field_15 >= (int)COAB_ARRAY_LEN(DATA_2B8) ||
        dir_step < 1 || dir_step > (int)COAB_ARRAY_LEN(DATA_2B8[0])) {
        log_warn("monster ai: no movement pattern %d step %d", field_15,
                 dir_step);
        return 8;
    }

    return DATA_2B8[field_15][dir_step - 1];
}

/* Where the combatant went last step, and how many times its pattern has doubled
 * back on itself (ovr010.byte_1AB18 and byte_1AB19). Both are reset at the top
 * of monsterai_close_and_attack, so they only ever describe the turn in hand. */
static int byte_1AB18;
static int byte_1AB19;

/* ------------------------------------------------------- the keyboard poll */

/* ovr010.process_input_in_monsters_turn, sub_36269. The player may interrupt a
 * quick fight: '2' turns the party's own spellcasting over to the AI and back,
 * space takes every party member off quick fight, and '-' is the cheat that ends
 * the fight. Returns whether this combatant's turn has been handed back to the
 * player, which only space can do. */
static bool process_input_in_monsters_turn(Player *player)
{
    bool player_turn = false;

    if (input_key_pressed()) {
        u8 var_6 = input_get_key();

        /* An extended key arrives as a zero followed by its scan code. */
        if (var_6 == 0) {
            var_6 = input_get_key();
        }

        if (var_6 == '2') {
            gbl.auto_pcs_cast_magic = !gbl.auto_pcs_cast_magic;

            if (gbl.auto_pcs_cast_magic) {
                character_print_message("Magic On");
            } else {
                character_print_message("Magic Off");
            }
        } else if (var_6 == 0x20) {
            for (int i = 0; i < gbl.team_count; i++) {
                Player *p = gbl.team_list[i];

                if (p != NULL && p->control_morale < CONTROL_NPC_BASE &&
                    p->health_status != STATUS_ANIMATED) {
                    p->quick_fight = QUICK_FIGHT_FALSE;
                }
            }

            if (player->quick_fight == QUICK_FIGHT_FALSE) {
                player_actions(player)->delay = 20;
                player_turn = true;
            }
        } else if (var_6 == '-') {
            attack_god_intervene();
        }
    }

    input_clear_keyboard();

    return player_turn;
}

/* ------------------------------------------------------------- guarding */

/* ovr010.TryGuarding, sub_361F7. Standing ready is only worth anything to
 * someone who can still swing at what walks past, so a held combatant, one with
 * a bow readied, or one with no delay left simply loses the round. */
static void try_guarding(Player *player)
{
    character_clear_text_area();

    if (player_is_held(player) || character_is_weapon_ranged(player) ||
        player_actions(player)->delay == 0) {
        action_clear(player_actions(player));
    } else {
        character_guarding(player);
    }
}

/* -------------------------------------------------------------- morale */

/* ovr010.FleeCheck_001, sub_3637F. Whether the combatant's nerve holds, and what
 * happens if it does not. Two rolls have to go against it: first its own morale
 * against how hurt it is, and then the encounter's morale against how much of
 * its side is left standing. Only if both fail does it look at whether running
 * would work - and if the other side is faster than it is, an intelligent
 * monster surrenders instead.
 *
 * Returns true when the turn is over, which only surrendering does; a combatant
 * whose nerve has gone still has a turn to spend on running.
 *
 * Anyone on our own side skips the second roll: the party is never asked whether
 * it is winning, only whether this character is hurt. */
static bool flee_check(Player *player)
{
    Action *actions = player_actions(player);
    bool var_1 = false;

    actions->moral_failure = false;

    effect_remove_attackers_affects(player);

    if (actions->fleeing) {
        /* Already running, by a spell or a failed save rather than by choice. */
        actions->moral_failure = true;
        character_display_status_string(true, 10, "is forced to flee", player);
    } else if (player->control_morale >= CONTROL_NPC_BASE) {
        gbl.monster_morale = (player->control_morale & CONTROL_PC_MASK) << 1;

        /* Morale is doubled into a percentage, so anything over 102 was never a
         * morale value at all and counts as none. */
        if (gbl.monster_morale > 102) {
            gbl.monster_morale = 0;
        }

        effect_check_affects(player, CHECK_TYPE_MORALE);

        /* The C# divided by hit_point_max, which a corrupt monster record can
         * leave at zero; such a character counts as untouched. */
        int hurt = 100;

        if (player->hit_point_max != 0) {
            hurt = 100 - ((player->hit_point_current * 100) /
                          player->hit_point_max);
        } else {
            log_warn("monster ai: %s has no maximum hit points",
                     player->name + 1);
            hurt = 0;
        }

        if (gbl.monster_morale < hurt || gbl.monster_morale == 0) {
            gbl.monster_morale = gbl.enemy_health_percentage;

            effect_check_affects(player, CHECK_TYPE_MORALE);

            if (gbl.monster_morale < (100 - gbl.area2_ptr->field_58C) ||
                gbl.monster_morale == 0 ||
                player->combat_team == TEAM_OURS) {
                int var_2 = attack_max_opposition_moves(player);

                if (var_2 <= (attack_calc_moves(player) / 2)) {
                    /* Faster than anything chasing it, so running is worth
                     * trying - and the affects that would keep it in the fight
                     * are shaken off. */
                    actions->moral_failure = true;
                    effect_remove_affect(NULL, AFFECT_4A, player);
                    effect_remove_affect(NULL, AFFECT_WEAP_DRAGON_SLAYER,
                                         player);
                } else if (player->stats.value[PSTAT_INT].full > 5) {
                    /* Too slow to get away, and bright enough to know it. */
                    effect_remove_from_combat("Surrenders", STATUS_UNCONSCIOUS,
                                              player);

                    var_1 = true;
                    character_clear_actions(player);
                }
            }
        }
    }

    return var_1;
}

/* ------------------------------------------------------------ turn undead */

/* ovr010.turn_undead */
bool monsterai_turn_undead(Player *player)
{
    Player *var_5 = NULL;

    if (player_actions(player)->has_turned_undead == false &&
        (player->class_level[SKILL_CLERIC] > 0 ||
         player->class_level_old[SKILL_CLERIC] > player->multiclass_level) &&
        attack_find_lowest_e9_target(&var_5, player)) {
        attack_turn_undead(player);
        return true;
    }

    return false;
}

/* --------------------------------------------------------- casting a spell */

/* The `p => p.combat_team != opp` the C# passed: everyone who is *not* on the
 * other side, which is the caster's own side plus anyone neutral. */
static bool filter_not_team(const Player *player, void *ctx)
{
    return player->combat_team != *(const int *)ctx;
}

/* ovr010.ShouldCastSpellX_sub1, sub_352AF */
bool monsterai_spell_would_catch_own_side(int spell_id, Point pos)
{
    const SpellEntry *entry = spell_entry(spell_id);
    bool result = false;

    if (entry == NULL) {
        return false;
    }

    if (entry->damage_on_save != DAMAGE_ON_SAVE_ZERO) {
        SortedCombatant sorted[GBL_MAX_COMBATANT_COUNT];
        int opp;
        int save_bonus;
        int count;

        if (gbl.selected_player == NULL) {
            log_warn("monster ai: nobody is casting spell 0x%x", spell_id);
            return false;
        }

        /* Our own side is made much likelier to fail its save than the enemy's,
         * so the AI holds an area spell back from the party where it would fire
         * the same one into a crowd of its own monsters. */
        save_bonus = (gbl.selected_player->combat_team == TEAM_OURS) ? -2 : 8;
        opp = player_opposite_team(gbl.selected_player);

        count = target_sorted_combatants(sorted, (int)COAB_ARRAY_LEN(sorted), 1,
                                         entry->field_f, pos, filter_not_team,
                                         &opp);

        /* Every one of them is rolled for, not just up to the first failure:
         * the rolls are taken out of the same sequence the fight is played from,
         * so stopping early would change what happens next. */
        for (int i = 0; i < count; i++) {
            if (effect_roll_saving_throw(save_bonus, entry->save_verse,
                                         sorted[i].player) == false) {
                result = true;
            }
        }
    }

    return result;
}

/* ovr010.ShouldCastSpellX, sub_353B1 */
bool monsterai_should_cast_spell(int min_priority, int spell_id,
                                 Player *attacker)
{
    const SpellEntry *entry = spell_entry(spell_id);

    if (entry == NULL) {
        return false;
    }

    if (entry->priority >= min_priority) {
        Player *dummy_target = NULL;

        /* Spell 3 is cure light wounds: it is worth casting when there is
         * somebody to cure, and every other spell that needs no target at all is
         * always worth casting. */
        if ((spell_id != 3 && entry->field_e == 0) ||
            (spell_id == 3 &&
             attack_find_healing_target(&dummy_target, attacker))) {
            return true;
        } else {
            CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
            int count;

            count = character_build_near_targets(
                near_targets, (int)COAB_ARRAY_LEN(near_targets),
                spellcast_spell_range(spell_id), attacker);

            if (count > 0) {
                if (entry->field_f == 0) {
                    return true;
                }

                for (int i = 0; i < count; i++) {
                    if (monsterai_spell_would_catch_own_side(
                            spell_id, near_targets[i].pos)) {
                        return false;
                    }
                }

                return true;
            }
        }
    }

    return false;
}

/* ovr010.sub_354AA */
bool monsterai_try_magic_item(Player *player)
{
    Item *best_wand = NULL;
    int team_count;

    team_count = (player_opposite_team(player) == TEAM_OURS) ? gbl.friends_count
                                                            : gbl.foe_count;

    if (player_actions(player)->can_use && team_count > 0 &&
        gbl.area_ptr->can_cast_spells == false) {
        int priorities_to_try = effect_roll_dice(7, 1);

        /* The break below leaves the item search, not this loop, so a lower
         * priority found later replaces what a higher one found first. That is
         * the original's arrangement: the last match wins, not the best. */
        for (int i = 0; i < priorities_to_try; i++) {
            int priority = 7 - i;

            for (int n = 0; n < player->item_count; n++) {
                Item *item_ptr = &player->items[n];
                int spell_id = (u8)item_ptr->affect_2;

                if (item_is_scroll(item_ptr) == false &&
                    item_ptr->affect_3 < 0x80 && item_ptr->readied &&
                    spell_id > 0) {
                    /* Above 0x38 the affect number is an item's own spell rather
                     * than a spellbook one, and sits 0x17 further up the table. */
                    if (spell_id > 0x38) {
                        spell_id -= 0x17;
                    }

                    if (monsterai_should_cast_spell(priority, spell_id,
                                                    player)) {
                        best_wand = item_ptr;
                        break;
                    }
                }
            }
        }
    }

    if (best_wand != NULL) {
        bool var_15 = false;

        viewplayer_use_magic_item(&var_15, best_wand);
        return true;
    }

    return false;
}

/* ovr010.sub_3560B */
bool monsterai_try_cast_spell(Player *player)
{
    u8 spell_list[MONSTERAI_MAX_SPELLS];
    int spells_count = 0;
    int spell_id = 0;
    int priority = 7;
    int var_5B = effect_roll_dice(7, 1);
    int var_5D = 1;
    bool casting_spell;

    /* SpellList.LearntList(): what is memorized, leaving out what is still being
     * learnt. */
    if (player_actions(player)->can_cast) {
        for (int i = 0; i < player->spell_list.count; i++) {
            if (player->spell_list.items[i].learning) {
                continue;
            }

            if (spells_count >= (int)COAB_ARRAY_LEN(spell_list)) {
                log_warn("monster ai: more than %d memorized spells to choose "
                         "from", (int)COAB_ARRAY_LEN(spell_list));
                break;
            }

            spell_list[spells_count++] = player->spell_list.items[i].id;
        }
    }

    if (spells_count > 0 && (player->control_morale >= CONTROL_NPC_BASE ||
                             gbl.auto_pcs_cast_magic)) {
        if (((player_opposite_team(player) == TEAM_OURS) ? gbl.friends_count
                                                         : gbl.foe_count) > 0) {
            /* A rolled number of passes, each insisting on one priority less
             * than the last, and three picks out of the memorized list each
             * pass. Nothing stops the same spell being picked twice. */
            while (var_5D <= var_5B && spell_id == 0) {
                for (int var_5E = 1; var_5E < 4 && spell_id == 0; var_5E++) {
                    int random_spell_index = effect_roll_dice(spells_count, 1) - 1;
                    int random_spell_id = spell_list[random_spell_index];

                    if (monsterai_should_cast_spell(priority, random_spell_id,
                                                    player)) {
                        spell_id = random_spell_id;
                    }
                }

                priority--;
                var_5D++;
            }
        }
    }

    if (spell_id > 0) {
        attack_spell_menu(&casting_spell, QUICK_FIGHT_TRUE, spell_id);
    } else {
        casting_spell = false;
    }

    return casting_spell;
}

/* ---------------------------------------------------------------- moving */

/* ovr010.CanMove, sub_3573B */
bool monsterai_can_move(bool *ground_clear, int base_direction, int dir_step,
                        Player *player)
{
    Action *actions = player_actions(player);
    int var_6 = dir_step_offset(actions->field_15, dir_step);
    int player_direction = (base_direction + var_6) % 8;
    bool is_poisonous_cloud = false;
    bool is_noxious_cloud = false;
    int ground_tile = 0;
    int player_index = 0;
    bool can_move = false;
    int move_cost;

    if (ground_clear != NULL) {
        *ground_clear = false;
    }

    combatmap_ground_information_clouds(&is_poisonous_cloud, &is_noxious_cloud,
                                       &ground_tile, &player_index,
                                       player_direction, player);

    /* Ground tile 0 means one of the squares is off the combat map, which is the
     * way out of a fight rather than a square to step onto. */
    if (ground_tile == 0) {
        if (ground_clear != NULL) {
            *ground_clear = true;
        }

        return false;
    }

    if (ground_tile < 0 || ground_tile >= BACKGROUND_TILE_COUNT) {
        log_warn("monster ai: ground tile %d is not in the tile table",
                 ground_tile);
        return false;
    }

    if (background_tiles[ground_tile].move_cost == 0xff) {
        return false;
    }

    move_cost = background_tiles[ground_tile].move_cost;

    /* Movement is counted in halves, so a straight step costs two of them and a
     * diagonal three. */
    if ((player_direction & 1) != 0) {
        move_cost *= 3;
    } else {
        move_cost *= 2;
    }

    if (player_index == 0 && move_cost < actions->move) {
        /* Walking into gas: anything that is already dead, already in the gas,
         * or warded against it walks on, and so does anyone running away.
         * Everyone else has to make a save, and failing it puts the square out
         * of reach for this round. */
        if (is_noxious_cloud &&
            player_has_affect(player, AFFECT_ANIMATE_DEAD) == false &&
            player_has_affect(player, AFFECT_STINKING_CLOUD) == false &&
            player_has_affect(player, AFFECT_6F) == false &&
            player_has_affect(player, AFFECT_7D) == false &&
            player_has_affect(player, AFFECT_PROTECT_MAGIC) == false &&
            player_has_affect(player, AFFECT_MINOR_GLOBE_OF_INVULN) == false &&
            actions->fleeing == false) {
            if (effect_roll_saving_throw(0, 0, player) == false) {
                move_cost = actions->move + 1;
            }
        }

        /* A cloud kill needs no save at all: anything under seven hit dice
         * simply will not walk into it. */
        if (is_poisonous_cloud && player->hit_dice < 7 &&
            player_has_affect(player, AFFECT_PROTECT_MAGIC) == false &&
            player_has_affect(player, AFFECT_6F) == false &&
            player_has_affect(player, AFFECT_85) == false &&
            player_has_affect(player, AFFECT_7D) == false &&
            actions->fleeing == false) {
            move_cost = actions->move + 1;
        }

        if (actions->move >= move_cost) {
            can_move = true;
        }
    }

    return can_move;
}

/* ovr010.moralFailureEscape, sub_359D1 */
void monsterai_moral_failure_escape(Player *player)
{
    Action *actions = player_actions(player);
    char prompt[64];
    int var_2 = 0;
    int dir = 0;

    snprintf(prompt, sizeof(prompt), "Move/Attack, Move Left = %d ",
             actions->move / 2);
    text_display_string(prompt, 0, 10, 0x18, 0);

    if (process_input_in_monsters_turn(player)) {
        return;
    }

    if ((actions->move / 2) > 0 && actions->delay > 0) {
        /* A party member always runs when their nerve goes; a monster or an NPC
         * gets a roll against the encounter's morale first. */
        if (player->control_morale < CONTROL_NPC_BASE ||
            (player->control_morale >= CONTROL_NPC_BASE &&
             gbl.enemy_health_percentage <=
                 (effect_roll_dice(100, 1) + gbl.monster_morale)) ||
            player->combat_team == TEAM_ENEMY) {
            /* An unarmoured magic-user keeps its distance rather than walking
             * into a melee - unless it is running away, in which case it moves
             * like everything else. */
            if (actions->moral_failure || player_armor(player) != NULL ||
                player->cls != CLASS_MAGIC_USER) {
                bool zero_title = false;
                bool var_5 = false;
                int dir_step = 1;

                if (actions->moral_failure == false) {
                    dir = attack_target_direction(actions->target, player);
                } else {
                    /* Away from where the party came in: the wilderness and
                     * dungeon both put the two sides on opposite sides of the
                     * map along the party's own facing, so a quarter turn off
                     * that facing is the way out - and our own side leaves by
                     * the other end of it. */
                    actions->field_15 = effect_roll_dice(2, 1);
                    dir = gbl.map_direction -
                          (((gbl.map_direction + 2) % 4) / 2) + 8;

                    if (player->combat_team == TEAM_OURS) {
                        dir += 4;
                    }

                    dir %= 8;
                }

                while (dir_step < 6 && var_5 == false &&
                       monsterai_can_move(&zero_title, dir, dir_step,
                                          player) == false) {
                    /* Off the edge of the map, and running: that is out of the
                     * fight altogether. */
                    if (actions->moral_failure && zero_title) {
                        var_5 = true;
                        attack_flee_battle(player);
                    } else {
                        dir_step++;
                    }
                }

                if (var_5) {
                    actions->move = 0;
                    actions->moral_failure = false;
                    character_clear_actions(player);
                } else {
                    var_2 = (dir_step_offset(actions->field_15, dir_step) + dir) % 8;

                    /* Nowhere to go, or straight back the way it came: change
                     * the movement pattern, and after twice of that give up on
                     * the target and look for another. */
                    if (dir_step == 6 || ((var_2 + 4) % 8) == byte_1AB18) {
                        byte_1AB19++;
                        actions->field_15 = (actions->field_15 % 6) + 1;

                        if (byte_1AB19 > 1) {
                            actions->target = NULL;

                            if (byte_1AB19 > 2) {
                                actions->move = 0;
                                var_5 = true;
                            } else if (attack_find_target(false, 1, 0xff,
                                                          player) == false) {
                                var_5 = true;
                                try_guarding(player);
                            }
                        }
                    }

                    if (dir_step < 6) {
                        byte_1AB18 = var_2;
                    } else {
                        var_5 = true;
                    }
                }

                if (var_5 == false) {
                    /* The window follows a combatant the player can see, and
                     * always follows one of our own. */
                    gbl.focus_combat_area_on_player =
                        gbl.byte_1D90E ||
                        combatmap_player_on_screen_p(false, player) ||
                        player->combat_team == TEAM_OURS;

                    combatmap_draw_player(false, COMBAT_ICON_NORMAL, var_2,
                                          player);
                    attack_move_step_away(actions->direction, player);

                    if (player->in_combat == false) {
                        character_clear_actions(player);
                    } else {
                        if (actions->move > 0) {
                            attack_move_step(actions->direction, player);
                        }

                        if (player->in_combat == false) {
                            character_clear_actions(player);
                        }

                        effect_in_poison_cloud(1, player);
                    }
                }

                return;
            }
        }
    }

    try_guarding(player);
}

/* ------------------------------------------------------ closing and hitting */

/* ovr010.sub_35DB1 */
bool monsterai_close_and_attack(Player *player)
{
    int counter = 0;
    bool stop = false;
    bool delayed;

    byte_1AB18 = 8;
    byte_1AB19 = 0;

    effect_check_affects(player, CHECK_TYPE_14);

    /* Binding up a bleeding friend is the whole round. */
    if (player->combat_team == TEAM_OURS && character_bandage(true)) {
        player_actions(player)->delay = 0;
    }

    delayed = player_actions(player)->delay != 0;

    while (stop == false && delayed == true) {
        Action *actions = player_actions(player);

        if (actions->moral_failure) {
            int escapes = 0;

            while (actions->move > 0 && actions->delay > 0 &&
                   actions->delay < 20) {
                monsterai_moral_failure_escape(player);

                if (++escapes > MONSTERAI_ESCAPE_STEPS_MAX) {
                    log_warn("monster ai: %s is still running after %d steps",
                             player->name + 1, escapes);
                    break;
                }
            }
        }

        /* Delay 20 is the mark the player's own turn is given, so it ends the
         * AI's turn as surely as no delay left at all. */
        if (actions->delay == 0 || actions->delay == 20) {
            delayed = false;
        } else {
            /* The C# set stop = false here, which it already is inside this
             * loop. Kept as a comment rather than as a statement. */
        }

        if (stop == false && delayed == true) {
            Item *primary;
            Player *target;
            int range = 1;

            counter++;

            if (counter > 20) {
                stop = true;
                delayed = false;
                try_guarding(player);
            }

            gbl.byte_1D90E = false;

            primary = player_primary_weapon(player);
            if (primary != NULL) {
                range = item_data(primary->type)->range - 1;
            }

            if (range == 0 || range == 0xff || range == -1) {
                range = 1;
            }

            target = actions->target;

            /* Last round's target is no good if it has left the fight or turned
             * out to be one of ours. */
            if (target != NULL &&
                (target->in_combat == false ||
                 target->combat_team == TEAM_OURS)) {
                target = NULL;
            }

            if (target != NULL && attack_can_see_target(target, player)) {
                Point target_pos = combatmap_player_map_pos(target);
                Point attack_pos = combatmap_player_map_pos(player);
                int steps = range;

                if (gbl.map_to_background_tile != NULL) {
                    gbl.map_to_background_tile->ignore_walls = false;
                } else {
                    log_warn("monster ai: closing on a target outside a fight");
                }

                if (target_can_reach_range(&steps, target_pos, attack_pos) &&
                    (steps / 2) <= range) {
                    gbl.byte_1D90E = true;
                }
            }

            if (gbl.byte_1D90E == false) {
                CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
                int count;

                count = character_build_near_targets(
                    near_targets, (int)COAB_ARRAY_LEN(near_targets), range,
                    player);

                if (count == 0) {
                    /* Nothing within reach of the weapon: walk towards whatever
                     * there is, or stand ready if there is nothing at all. */
                    if (attack_find_target(false, 0, 0xff, player)) {
                        monsterai_moral_failure_escape(player);
                    } else {
                        stop = true;
                        try_guarding(player);
                    }
                } else {
                    CombatPlayerIndex adjacent[GBL_MAX_COMBATANT_COUNT];
                    int roll = effect_roll_dice(count, 1);

                    target = near_targets[roll - 1].player;

                    if (character_is_weapon_ranged(player) &&
                        character_is_weapon_ranged_melee(player) == false &&
                        character_build_near_targets(
                            adjacent, (int)COAB_ARRAY_LEN(adjacent), 1,
                            player) > 0) {
                        /* Something has closed with a bow: swap to a weapon
                         * that can be used at arm's length, which is the whole
                         * round. */
                        ai_items_selection(player);
                        stop = true;
                    } else if (character_target_range(target, player) == 1 ||
                               attack_can_see_target(target, player)) {
                        gbl.byte_1D90E = true;
                    }
                }
            }

            if (gbl.byte_1D90E) {
                combatmap_redraw_area(attack_target_direction(target, player), 2,
                                      combatmap_player_map_pos(player));
            }

            if (gbl.byte_1D90E) {
                if (attack_try_sweep(target, player)) {
                    stop = true;
                    character_clear_actions(player);
                } else {
                    Item *item = NULL;

                    attack_recalc_attacks_received(target, player);

                    if (character_is_weapon_ranged(player)) {
                        gbl.byte_1D90E =
                            character_current_attack_item(&item, player);

                        /* A weapon that also reaches in melee is swung rather
                         * than thrown at somebody standing next to it. */
                        if (character_is_weapon_ranged_melee(player) &&
                            character_target_range(target, player) == 1) {
                            item = NULL;
                        }
                    }

                    stop = attack_target(item, 0, target, player);

                    if (stop) {
                        delayed = false;
                    } else if (target->in_combat == false) {
                        stop = true;
                    }
                }
            }
        }
    }

    return delayed == false;
}

/* ------------------------------------------------------ choosing a weapon */

/* ovr010.CalcItemPowerRating, sub_36535. How good this weapon looks to the AI:
 * the damage it rolls, twice its fixed bonus, eight a plus, three for leaving a
 * hand free, and two for each attack past the first that a missile weapon gets.
 * Zero means it will not be used at all. */
static int calc_item_power_rating(Item *item, Player *player)
{
    const ItemData *item_data_ptr = item_data(item->type);
    int rating;

    rating = item_data_ptr->dice_size_normal * item_data_ptr->dice_count_normal;

    if (item->plus > 0) {
        rating += item->plus * 8;
    }

    if (item_data_ptr->bonus_normal > 0) {
        rating += item_data_ptr->bonus_normal * 2;
    }

    /* Item type 85 is the mace of disruption, worth having in hand against
     * anything undead whatever it rolls. */
    if (item->type == ITEM_TYPE_85 && player_actions(player)->target != NULL &&
        player_actions(player)->target->field_E9 > 0) {
        rating = 8;
    }

    if ((item_data_ptr->flags & ITEM_DATA_FLAG_08) != 0) {
        rating += (item_data_ptr->number_attacks - 1) * 2;
    }

    if (item_hands_count(item) <= 1) {
        rating += 3;
    }

    if ((item_hands_count(item) + player->weapons_hands_used) > 3) {
        rating = 0;
    }

    /* An aligned weapon burns anyone of the wrong alignment holding it. */
    if (item->affect_3 == AFFECT_CAST_THROW_LIGHTENING &&
        (item->affect_2 & 0x0f) != player->alignment) {
        rating = 0;
    }

    if (item->affect_2 == AFFECT_PARALIZING_GAZE) {
        rating = 0;
    }

    if (item->cursed) {
        rating = 0;
    }

    return rating;
}

/* ovr010.AI_items_selection, sub_36673. Picks the combatant's weapon and shield
 * out of the pack and readies them, and redraws the side panel if anything
 * changed.
 *
 * Two candidates are kept: var_4, the best missile or reaching weapon, and
 * var_8, the best weapon for hand-to-hand - which starts off as the combatant's
 * own bare-handed attack, so a weapon worse than its claws is never picked up.
 */
static void ai_items_selection(Player *player)
{
    Item *var_4 = NULL;
    Item *var_8 = NULL;
    int var_15 = 1;
    int var_16;
    int max_bonus = 0;
    Item *best_weapon = NULL;
    Item *tmp_item = NULL;
    Item *weapon;
    bool ranged_melee;
    bool var_1F = false;
    bool items_changed = false;
    bool replace_weapon;
    u8 item_flags = 0;

    player->weapons_hands_used -= player_primary_weapon_hand_count(player);
    player->weapons_hands_used -= player_secondary_weapon_hand_count(player);

    var_16 = player->attack1_dice_size_base * player->attack1_dice_count_base;

    if (player->attack1_damage_bonus_base > 0) {
        var_16 += player->attack1_damage_bonus_base * 2;
    }

    for (int n = 0; n < player->item_count; n++) {
        Item *item = &player->items[n];
        int item_type = item->type;
        const ItemData *data = item_data(item_type);

        if (data->slot == ITEM_SLOT_0 &&
            (data->class_flags & player->class_flags) != 0) {
            int power_rating = calc_item_power_rating(item, player);

            if ((data->flags & ITEM_DATA_FLAG_08) != 0 ||
                (data->flags & ITEM_DATA_FLAG_10) != 0) {
                if (power_rating > var_15) {
                    var_4 = item;
                    var_15 = power_rating;
                }
            }

            if ((data->flags & ITEM_DATA_FLAG_08) == 0 &&
                power_rating > var_16) {
                var_8 = item;
                var_16 = power_rating;
            }
        }

        /* Slot 1 is the shield: the best plus wins, and a cursed one - which is
         * a negative plus - is never picked. */
        if (data->slot == ITEM_SLOT_1) {
            if ((data->class_flags & player->class_flags) != 0) {
                int bonus = item->plus >= 0 ? item->plus + 1 : 0;

                if (bonus > max_bonus) {
                    best_weapon = item;
                    max_bonus = bonus;
                }
            }
        }
    }

    ranged_melee = character_item_is_ranged_melee(var_4);

    /* A missile weapon is only worth readying if there is something to shoot
     * with it: its own ammunition for a bow or a crossbow, or the weapon itself
     * when it is the thing that gets thrown. */
    if (var_4 != NULL) {
        item_flags = item_data(var_4->type)->flags;

        if ((item_flags & ITEM_DATA_FLAG_10) != 0) {
            tmp_item = var_4;
        }

        if ((item_flags & ITEM_DATA_FLAG_08) != 0) {
            if ((item_flags & ITEM_DATA_ARROWS) != 0) {
                tmp_item = player_arrows(player);
            }

            if ((item_flags & ITEM_DATA_QUARRELS) != 0) {
                tmp_item = player_quarrels(player);
            }
        }
    }

    if (tmp_item != NULL ||
        item_flags == (ITEM_DATA_FLAG_02 | ITEM_DATA_FLAG_08)) {
        var_1F = true;
    }

    /* The missile weapon wins if it is worth more than half the melee weapon and
     * either reaches in melee too or there is nobody standing next to us. */
    if (var_4 != NULL && var_15 > (var_16 >> 1) && var_1F) {
        CombatPlayerIndex adjacent[GBL_MAX_COMBATANT_COUNT];

        if (ranged_melee ||
            character_build_near_targets(adjacent,
                                         (int)COAB_ARRAY_LEN(adjacent), 1,
                                         player) == 0) {
            weapon = var_4;
        } else {
            weapon = var_8;
        }
    } else {
        weapon = var_8;
    }

    replace_weapon = true;

    if (player_primary_weapon(player) != NULL &&
        (player_primary_weapon(player) == weapon ||
         player_primary_weapon(player)->cursed)) {
        replace_weapon = false;
    }

    if (replace_weapon) {
        if (player_primary_weapon(player) != NULL) {
            viewplayer_ready_item(player_primary_weapon(player));
        }

        character_recalc_values(player);

        if (player_secondary_weapon(player) != NULL &&
            player_secondary_weapon(player)->cursed == false) {
            player->weapons_hands_used -=
                player_secondary_weapon_hand_count(player);
        }

        if (weapon != NULL) {
            viewplayer_ready_item(weapon);
        }

        items_changed = true;
    }

    character_recalc_values(player);
    attack_recalc_attacks(player);
    replace_weapon = true;

    if (player_secondary_weapon(player) != NULL &&
        (player_secondary_weapon(player) == best_weapon ||
         player_secondary_weapon(player)->cursed)) {
        replace_weapon = false;
    }

    if (player->weapons_hands_used > 2) {
        /* Both hands are on the weapon, so whatever is in the shield hand has to
         * come off - or, if there is nothing there, the weapon does. */
        if (player_secondary_weapon(player) == NULL ||
            player_secondary_weapon(player)->cursed) {
            if (weapon != NULL) {
                viewplayer_ready_item(weapon);
            } else {
                /* The C# dereferenced a null weapon here; there is nothing to
                 * put away, so the hands stay as they are. */
                log_warn("monster ai: %s has no weapon to put away",
                         player->name + 1);
            }
            items_changed = true;
        } else {
            viewplayer_ready_item(player_secondary_weapon(player));
            items_changed = true;
        }
    } else if (player->weapons_hands_used < 2 && replace_weapon) {
        if (player_secondary_weapon(player) != NULL) {
            viewplayer_ready_item(player_secondary_weapon(player));
        }

        character_recalc_values(player);

        if (best_weapon != NULL) {
            viewplayer_ready_item(best_weapon);
        }

        items_changed = true;
    }

    character_recalc_values(player);

    if (items_changed) {
        character_combat_display_summary(player);
    }
}

/* --------------------------------------------------------------- the turn */

/* ovr010.PlayerQuickFight, sub_3504B */
void monsterai_player_quick_fight(Player *player)
{
    bool var_2 = process_input_in_monsters_turn(player);
    int var_1;
    int passes = 0;

    prompt_clear_area();
    character_clear_text_area();

    if (player->in_combat == false) {
        var_2 = true;
        character_clear_actions(player);
    }

    var_1 = player_actions(player)->field_15;

    /* Patterns 0 and 4 are the ones a combatant is left with when it has not
     * moved yet, and a quarter of the time it takes a new one anyway: an 8 on
     * the d8 gives one of the four straight-ahead patterns and anything else one
     * of the two that sweep round. */
    if (var_1 == 0 || var_1 == 4 || effect_roll_dice(4, 1) == 1) {
        var_1 = effect_roll_dice(8, 1);

        if (var_1 != 8) {
            var_1 = effect_roll_dice(2, 1) + 4;
        } else {
            var_1 = effect_roll_dice(4, 1);
        }
    }

    player_actions(player)->field_15 = var_1;

    if (var_2 == false) {
        var_2 = flee_check(player);
    }

    if (player_actions(player)->moral_failure &&
        player_actions(player)->fleeing == false) {
        character_display_status_string(true, 10, "flees in panic", player);
    }

    if (var_2) {
        return;
    }

    if (monsterai_try_magic_item(player)) {
        character_clear_actions(player);
        return;
    }

    if (player_actions(player)->spell_id > 0) {
        /* A spell started last round goes off now. */
        spellcast_resolve_spell(true, QUICK_FIGHT_TRUE,
                              player_actions(player)->spell_id);

        character_clear_actions(player);
        return;
    }

    if (monsterai_turn_undead(player)) {
        character_clear_actions(player);
        return;
    }

    if (monsterai_try_cast_spell(player)) {
        return;
    }

    ai_items_selection(player);
    var_2 = process_input_in_monsters_turn(player);

    while (var_2 == false) {
        if (attack_find_target(false, 1, 0xff, player) &&
            player_actions(player)->delay > 0 && player->in_combat) {
            var_2 = monsterai_close_and_attack(player);
        } else {
            var_2 = true;
            try_guarding(player);
        }

        /* The C# loop had no bound of its own; every path through it either ends
         * the turn or spends some of the delay, so reaching this is a bug. */
        if (++passes > MONSTERAI_TURN_PASSES_MAX) {
            log_warn("monster ai: %s took %d passes without finishing its turn",
                     player->name + 1, passes);
            break;
        }
    }
}
