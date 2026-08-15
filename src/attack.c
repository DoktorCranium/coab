/* attack.c - Ported from engine/ovr014.cs. */
#include "attack.h"

#include <stdio.h>
#include <string.h>

#include "affecttab.h"
#include "character.h"
#include "cheats.h"
#include "combatmap.h"
#include "effect.h"
#include "frames.h"
#include "gbl.h"
#include "input.h"
#include "log.h"
#include "prompt.h"
#include "sound.h"
#include "spellcast.h"
#include "spells.h"
#include "target.h"
#include "text.h"
#include "tile.h"
#include "viewplayer.h"

/* engine/ovr014.cs: enum AttackType, which only DisplayAttackMessage reads. */
typedef enum {
    ATTACK_TYPE_NORMAL   = 0,
    ATTACK_TYPE_BEHIND   = 1,
    ATTACK_TYPE_BACKSTAB = 2,
    ATTACK_TYPE_SLAY     = 3
} AttackType;

/* ---------------------------------------------------------------- the ground
 *
 * gbl.map_to_background_tile only exists while a fight is being fought: ovr011
 * makes it and ovr009 drops it again. The C# would have thrown a
 * NullReferenceException outside a fight; these three say so and carry on.
 */

static int ground_tile_at(Point pos)
{
    if (gbl.map_to_background_tile == NULL) {
        log_warn("attack: no ground tile map outside a fight");
        return 0;
    }

    return ground_tile_map_get(gbl.map_to_background_tile, pos);
}

static Point map_screen_top_left(void)
{
    if (gbl.map_to_background_tile == NULL) {
        log_warn("attack: no ground tile map outside a fight");
        return point_make(0, 0);
    }

    return gbl.map_to_background_tile->map_screen_top_left;
}

/* gbl.BackGroundTiles[t].move_cost, which the C# indexed straight into. */
static int ground_move_cost(int ground_tile)
{
    const BackgroundTile *tile = background_tile(ground_tile);

    if (tile == NULL) {
        log_warn("attack: no background tile 0x%x", ground_tile);
        return 0xff;    /* impassable, which is the safe way to be wrong */
    }

    return tile->move_cost;
}

/* ---------------------------------------------------- the top of a turn */

/* sub_3E000 */
void attack_calculate_initiative(Player *player)
{
    Action *action;

    if (player == NULL) {
        log_warn("attack: initiative for nobody");
        return;
    }

    action = player_actions(player);

    action->spell_id   = 0;
    action->can_cast   = true;
    action->can_use    = true;
    action->field_8    = false;
    action->attack_idx = 2;

    attack_recalc_attacks(player);
    gbl.half_actions_left = player->base_half_moves;

    gbl.reset_moves_left = false;

    effect_check_affects(player, CHECK_TYPE_MOVEMENT);

    player->attack2_attacks_left =
        (u8)attack_this_round_action_count(gbl.half_actions_left);

    action->max_sweap_targets = player->attack_level;

    if (player->in_combat) {
        action->delay = effect_roll_dice(6, 1) + character_dex_reaction_adj(player);

        if (action->delay < 1) {
            action->delay = 1;
        }

        /* area2.field_596 holds one bit per side that was surprised. */
        if (((player->combat_team + 1) & gbl.area2_ptr->field_596) != 0) {
            action->delay -= 6;
        }

        if (action->delay < 0 || action->delay > 20) {
            action->delay = 0;
        }
    } else {
        action->delay = 0;
    }

    action->move = attack_calc_moves(player);
}

/* sub_3E124 */
int attack_calc_moves(Player *player)
{
    int moves;

    if (player == NULL) {
        log_warn("attack: movement for nobody");
        return 0;
    }

    moves = player->movement;

    if (player->in_combat == false) {
        moves += gbl.area2_ptr->field_6E4;
    }

    if (moves < 1 || moves > 96) {
        moves = 1;
    }

    gbl.half_actions_left = moves * 2;

    gbl.reset_moves_left = true;

    effect_check_affects(player, CHECK_TYPE_MOVEMENT);

    gbl.reset_moves_left = false;

    return gbl.half_actions_left;
}

/* sub_3EDD4 */
void attack_recalc_attacks(Player *player)
{
    bool found_ranged = false;
    Item *ranged_item = NULL;
    int   orig_attacks;
    int   attacks;
    Action *action;

    if (player == NULL) {
        log_warn("attack: attack count for nobody");
        return;
    }

    action       = player_actions(player);
    orig_attacks = player->attack1_attacks_left;
    player->attack1_attacks_left = player->attacks_count;

    if (character_is_weapon_ranged(player) &&
        character_current_attack_item(&ranged_item, player)) {
        Item *weapon = player_primary_weapon(player);

        found_ranged = true;

        if (weapon == NULL) {
            /* The C# read activeItems.primaryWeapon.type here; a ranged attack
             * with nothing readied in the primary hand would have thrown. */
            log_warn("attack: %s has a ranged attack but no readied weapon",
                     player->name);
            gbl.half_actions_left = 2;
        } else {
            int num_attacks = item_data(weapon->type)->number_attacks;

            if (num_attacks < 2) {
                num_attacks = 2;
            }

            gbl.half_actions_left = num_attacks;
        }
    } else {
        gbl.half_actions_left = player->attack1_attacks_left;
    }

    gbl.reset_moves_left = false;
    effect_check_affects(player, CHECK_TYPE_MOVEMENT);

    attacks = attack_this_round_action_count(gbl.half_actions_left);

    if (found_ranged && ranged_item != NULL) {
        int ranged_ammo = 1;

        if (ranged_item->count > ranged_ammo) {
            ranged_ammo = ranged_item->count;
        }

        if (ranged_ammo < attacks && ranged_item->count > 0) {
            attacks = ranged_ammo;
        }
    }

    if (action->field_8 == false ||
        attacks < orig_attacks ||
        (action->field_8 && attacks < (orig_attacks * 2) && found_ranged == false)) {
        player->attack1_attacks_left = (u8)attacks;
    }
}

/* sub_3EF0D */
int attack_this_round_action_count(int half_actions_left)
{
    if ((gbl.combat_round & 1) == 1) {
        half_actions_left++;
    }

    return half_actions_left / 2;
}

/* ---------------------------------------------------------- attack messages */

/* sub_3E192. Rolls one blow's damage into gbl.damage, doubling it and more for a
 * backstab, and then lets both sides' affects change it. */
static void roll_attack_damage(int index, Player *target, Player *attacker)
{
    gbl.damage = effect_roll_dice_save(player_attack_dice_size(attacker, index),
                                       player_attack_dice_count(attacker, index));
    gbl.damage += player_attack_damage_bonus(attacker, index);

    if (gbl.damage < 0) {
        gbl.damage = 0;
    }

    if (attack_can_backstab(target, attacker)) {
        /* Two times at level 1, three at 5, four at 9... */
        gbl.damage *= ((player_skill_level(attacker, SKILL_THIEF) - 1) / 4) + 2;
    }

    gbl.damage_flags = 0;
    effect_check_affects(attacker, CHECK_TYPE_SPECIAL_ATTACKS);
    effect_check_affects(target, CHECK_TYPE_5);
}

/* ovr014.DisplayAttackMessage. What the blow did, in the status area, and then
 * what it left the target as. */
static void display_attack_message(bool attack_hits, int attack_damage,
                                  int actual_damage, AttackType attack,
                                  Player *target, Player *attacker)
{
    char text[80];
    int  line;

    if (attack == ATTACK_TYPE_BACKSTAB) {
        snprintf(text, sizeof(text), "-Backstabs-");
    } else if (attack == ATTACK_TYPE_SLAY) {
        snprintf(text, sizeof(text), "slays helpless");
    } else {
        snprintf(text, sizeof(text), "Attacks");
    }

    character_display_status_string(false, 10, text, attacker);
    line = 12;

    character_display_name(false, line, 0x17, target);
    line++;

    if (attack == ATTACK_TYPE_BEHIND) {
        snprintf(text, sizeof(text), "(from behind) ");
    } else {
        text[0] = '\0';
    }

    if (attack_hits) {
        if (attack == ATTACK_TYPE_SLAY) {
            snprintf(text, sizeof(text), "with one cruel blow");
        } else {
            char part[48];

            snprintf(part, sizeof(part), "Hitting for %d %s of damage",
                     attack_damage, attack_damage == 1 ? "point" : "points");
            strncat(text, part, sizeof(text) - strlen(text) - 1);
        }

        character_damage(actual_damage, target);
    } else {
        strncat(text, "and Misses", sizeof(text) - strlen(text) - 1);
    }

    if (target->health_status != STATUS_GONE) {
        text_press_any_key(text, true, 10, line + 3, 0x26, line, 0x17);
    }

    line = gbl.text_y_col + 1;

    text_game_delay();

    if (actual_damage > 0) {
        effect_try_loose_spell(target);
    }

    if (target->in_combat == false) {
        character_display_status_string(false, line, "goes down", target);
        line += 2;

        if (target->health_status == STATUS_DYING) {
            text_display_string("and is Dying", 0, 10, line, 0x17);
        }

        if (target->health_status == STATUS_DEAD ||
            target->health_status == STATUS_STONED ||
            target->health_status == STATUS_GONE) {
            character_display_status_string(false, line, "is killed", target);
        }

        line += 2;

        effect_remove_combat_affects(target);

        effect_check_affects(target, CHECK_TYPE_DEATH);

        if (target->in_combat == false) {
            combatmap_combatant_killed(target);
        } else {
            /* Something brought them back up - a troll's regeneration, say. */
            text_game_delay();
        }
    }

    character_clear_text_area();
}

/* ---------------------------------------------------------------- moving */

/* sub_3E65D. Anyone standing guard next to where the combatant has just arrived
 * gets their free swing now. */
static void move_step_into_attack(Player *target)
{
    CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
    int count;

    count = character_build_near_targets(near_targets,
                                         (int)COAB_ARRAY_LEN(near_targets), 1,
                                         target);

    if (target->in_combat) {
        for (int i = 0; i < count; i++) {
            Player *attacker = near_targets[i].player;

            if (player_actions(attacker)->guarding &&
                player_is_held(attacker) == false) {
                combatmap_redraw_area(8, 2, combatmap_player_map_pos(target));

                player_actions(attacker)->guarding = false;

                attack_recalc_attacks_received(target, attacker);

                attack_target(NULL, 0, target, attacker);
            }
        }
    }
}

/* sub_3E748 */
void attack_move_step(int direction, Player *player)
{
    int player_index;
    Point old_pos;
    Point new_pos;
    int cost_to_move;
    int radius = 1;
    Action *action;

    if (player == NULL) {
        log_warn("attack: a step by nobody");
        return;
    }

    player_index = combatmap_player_index(player);
    action       = player_actions(player);

    old_pos = gbl.combat_map[player_index].pos;
    new_pos = point_add(old_pos, gbl_map_direction_delta(direction));

    /* engine/ovr014.cs asks "does this solve more problems than it causes?
     * Regarding AI flee" - a monster running for the edge of the map stops at
     * it rather than walking off. */
    if (point_map_in_bounds(new_pos) == false) {
        return;
    }

    /* A diagonal step costs half again as much as a straight one. */
    if ((direction & 0x01) != 0) {
        cost_to_move = ground_move_cost(ground_tile_at(new_pos)) * 3;
    } else {
        cost_to_move = ground_move_cost(ground_tile_at(new_pos)) * 2;
    }

    if (cost_to_move > action->move) {
        action->move = 0;
    } else {
        action->move -= cost_to_move;
    }

    if (player->quick_fight == QUICK_FIGHT_TRUE) {
        radius = 3;

        if (combatmap_coord_on_screen(point_sub(new_pos, map_screen_top_left())) == false &&
            gbl.focus_combat_area_on_player) {
            combatmap_redraw_area(8, 2, old_pos);
        }
    }

    if (gbl.focus_combat_area_on_player) {
        combatmap_redraw_player_background(player_index);
    }

    gbl.combat_map[player_index].pos = new_pos;

    combatmap_setup_player_index();

    if (gbl.focus_combat_area_on_player) {
        combatmap_redraw_area(8, radius, new_pos);
    }

    action->attacks_received   = 0;
    action->direction_changes  = 0;
    sound_play(SOUND_A);

    move_step_into_attack(player);

    if (player->in_combat == false || player_is_held(player)) {
        action->move = 0;
    }
}

/* sub_3E954 */
void attack_move_step_away(int direction, Player *player)
{
    CombatPlayerIndex origin[GBL_MAX_COMBATANT_COUNT];
    CombatPlayerIndex dest[GBL_MAX_COMBATANT_COUNT];
    int origin_count;
    int dest_count;
    int player_index;

    if (player == NULL) {
        log_warn("attack: a step away by nobody");
        return;
    }

    origin_count = character_build_near_targets(origin,
                                                (int)COAB_ARRAY_LEN(origin), 1,
                                                player);

    if (origin_count == 0) {
        return;
    }

    player_index = combatmap_player_index(player);

    /* Move to where the step would land, see who would still be in reach there,
     * and step back again. */
    gbl.combat_map[player_index].pos =
        point_add(gbl.combat_map[player_index].pos,
                  gbl_map_direction_delta(direction));

    dest_count = character_build_near_targets(dest, (int)COAB_ARRAY_LEN(dest), 1,
                                             player);

    gbl.combat_map[player_index].pos =
        point_sub(gbl.combat_map[player_index].pos,
                  gbl_map_direction_delta(direction));

    /* Only those left behind get a parting swing; anyone still next to the
     * combatant afterwards can have their turn next round. */
    for (int i = 0; i < dest_count; i++) {
        for (int j = 0; j < origin_count; j++) {
            if (origin[j].player == dest[i].player) {
                origin[j].player = NULL;
            }
        }
    }

    if (player->in_combat == false) {
        /* engine/ovr014.cs: "what the heck are we doing here then? and why is
         * this test not earlier in the function." */
        return;
    }

    for (int i = 0; i < origin_count; i++) {
        Player *attacker = origin[i].player;
        bool found = false;
        int end_dir;

        if (attacker == NULL) {
            continue;
        }

        gbl.display_hitpoints_ac        = true;
        gbl.focus_combat_area_on_player = true;

        if (player_is_held(attacker) ||
            attack_can_see_target(player, attacker) == false ||
            player_has_affect(attacker, AFFECT_WEAP_DRAGON_SLAYER) ||
            player_has_affect(attacker, AFFECT_4A)) {
            continue;
        }

        /* Five tries at the directions the attacker could turn to, which is how
         * the original wrote "swing if you can see them from anywhere"; the
         * `found` flag stops after the first swing. */
        end_dir = player_actions(attacker)->direction + 10;

        for (int tmp_dir = player_actions(attacker)->direction + 6;
             tmp_dir <= end_dir; tmp_dir++) {
            Player *backup_target;
            int attack_index = 1;

            if (found) {
                continue;
            }

            if (player_actions(attacker)->delay <= 0 &&
                player_actions(attacker)->attacks_received != 0 &&
                target_can_see(tmp_dir % 8, combatmap_player_map_pos(player),
                               combatmap_player_map_pos(attacker)) == false) {
                continue;
            }

            if (attacker->attacks_count == 0) {
                attack_index = 2;
            }
            if (attacker->attack1_attacks_left > 0) {
                attack_index = 1;
            }
            if (attacker->attack2_attacks_left > 0) {
                attack_index = 2;
            }

            if (player_attacks_left(attacker, attack_index) == 0) {
                player_attacks_left_set(attacker, attack_index, 1);
            }

            player_actions(attacker)->attack_idx = attack_index;

            backup_target = player_actions(attacker)->target;

            attack_target(NULL, 1, player, attacker);
            found = true;

            player_actions(attacker)->target = backup_target;

            if (player->in_combat) {
                gbl.display_hitpoints_ac = true;
                character_combat_display_summary(player);
            }
        }
    }
}

/* ovr014.flee_battle */
void attack_flee_battle(Player *player)
{
    CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
    bool gets_away = false;

    if (player == NULL) {
        log_warn("attack: nobody to flee");
        return;
    }

    if (character_build_near_targets(near_targets,
                                    (int)COAB_ARRAY_LEN(near_targets), 0xff,
                                    player) == 0) {
        gets_away = true;
    } else {
        int own_moves = attack_calc_moves(player) / 2;
        int their_moves = attack_max_opposition_moves(player);

        if (their_moves < own_moves) {
            gets_away = true;
        } else if (their_moves == own_moves && effect_roll_dice(2, 1) == 1) {
            gets_away = true;
        }
    }

    if (gets_away) {
        effect_remove_from_combat("Got Away", STATUS_RUNNING, player);
    } else {
        character_print_message("Escape is blocked");
    }

    character_clear_actions(player);
}

/* ------------------------------------------------------------- targeting */

/* sub_3F143 */
bool attack_can_see_target(Player *target, Player *attacker)
{
    if (target == NULL) {
        return false;
    }

    if (attacker == target) {
        return true;
    }

    gbl.target_invisible = false;

    effect_check_affects(target, CHECK_TYPE_VISIBILITY);

    if (gbl.target_invisible == false) {
        Player *old_target = player_actions(attacker)->target;

        player_actions(attacker)->target = target;

        effect_check_affects(attacker, CHECK_TYPE_NONE);

        player_actions(attacker)->target = old_target;
    }

    return gbl.target_invisible == false;
}

/* ---------------------------------------------------------------- attacking */

/* sub_3EF3D */
bool attack_try_sweep(Player *target, Player *attacker)
{
    CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
    CombatPlayerIndex ordered[GBL_MAX_COMBATANT_COUNT];
    int count;
    int ordered_count = 0;
    int sweepable_count = 0;
    int target_at = -1;

    if (target == NULL || attacker == NULL) {
        log_warn("attack: a sweep with nobody at one end of it");
        return false;
    }

    if (attacker->attack1_attacks_left >=
            player_actions(attacker)->max_sweap_targets ||
        target->hit_dice != 0 ||
        character_target_range(target, attacker) != 1) {
        return false;
    }

    count = character_build_near_targets(near_targets,
                                         (int)COAB_ARRAY_LEN(near_targets), 1,
                                         attacker);

    for (int i = 0; i < count; i++) {
        if (near_targets[i].player == target) {
            target_at = i;
        }
        if (near_targets[i].player->hit_dice == 0) {
            sweepable_count++;
        }
    }

    if (sweepable_count <= attacker->attack1_attacks_left) {
        return false;
    }

    if (sweepable_count > (int)player_actions(attacker)->max_sweap_targets) {
        sweepable_count = player_actions(attacker)->max_sweap_targets;
    }

    character_display_status_string(true, 10, "sweeps", attacker);

    /* The named target is moved to the front of the list, so the blow the player
     * asked for lands first. */
    if (target_at >= 0) {
        ordered[ordered_count++] = near_targets[target_at];
    }
    for (int i = 0; i < count; i++) {
        if (i != target_at) {
            ordered[ordered_count++] = near_targets[i];
        }
    }

    /* engine/ovr014.cs counts the sweepable targets by HitDice but then picks
     * them by hitBonus, and takes the first `sweepableCount` of that second,
     * shorter list - GetRange throws when it is shorter. Here the sweep simply
     * stops when the list runs out, which is the same blows in the same order up
     * to the point the C# would have thrown at. */
    for (int i = 0; i < ordered_count && sweepable_count > 0; i++) {
        Player *sweep_target = ordered[i].player;

        if (sweep_target->hit_bonus != 0) {
            continue;
        }

        sweepable_count--;

        attack_recalc_attacks_received(sweep_target, attacker);

        attacker->attack1_attacks_left = 1;

        attack_target(NULL, 0, sweep_target, attacker);
    }

    if (sweepable_count > 0) {
        log_warn("attack: %s swept at %d fewer targets than were counted",
                 attacker->name, sweepable_count);
    }

    return true;
}

/* seg600:0369 unk_16679. The roll a cleric of a given level needs to turn an
 * undead of a given kind, indexed [field_E9 * 10 + level band]; a negative entry
 * destroys the thing outright rather than turning it.
 *
 * The original table has a row for every kind of undead. engine/ovr014.cs only
 * transcribed the twenty-one bytes the disassembly showed, which covers the two
 * lowest kinds; anything tougher indexed off the end and threw. See
 * turn_undead_entry below for what this port does instead. */
static const i8 UNK_16679[21] = {
    0,
    17, 17,  0,  0,  1, 17,  0,  0, 32, 32,
    10,  7,  4,  1,  1,  0,  0, -1, -1, -1
};

/* True when the table has a row for this undead, with the needed roll in *out. */
static bool turn_undead_entry(int *out, int field_e9, int level_band)
{
    int index = (field_e9 * 10) + level_band;

    if (index < 0 || index >= (int)COAB_ARRAY_LEN(UNK_16679)) {
        /* engine/ovr014.cs would have thrown IndexOutOfRangeException. Failing
         * the attempt is the cautious way to be wrong: index 0 reads as 0, which
         * is "destroyed", and destroying an undead the table has no row for
         * would be worse than the cleric coming away with nothing. */
        log_warn("turn undead: no table row for an undead of kind %d at level "
                 "band %d", field_e9, level_band);
        return false;
    }

    *out = UNK_16679[index];

    return true;
}

/* ovr014.turns_undead */
void attack_turn_undead(Player *player)
{
    bool any_turned = false;
    bool resisted = false;
    int destroy_budget = 6;     /* var_3 */
    int turnings_left;          /* var_2, the d12 of undead this may account for */
    int roll;                   /* var_1 */
    int cleric_lvl;
    int level_band;             /* var_B */
    Player *target = NULL;

    if (player == NULL) {
        log_warn("turn undead: nobody to do the turning");
        return;
    }

    character_display_status_string(false, 10, "turns undead...", player);
    prompt_clear_area();
    text_game_delay();

    player_actions(player)->has_turned_undead = true;

    turnings_left = effect_roll_dice(12, 1);
    roll          = effect_roll_dice(20, 1);

    cleric_lvl = player_skill_level(player, SKILL_CLERIC);

    if (cleric_lvl >= 1 && cleric_lvl <= 8) {
        level_band = player->class_level[SKILL_CLERIC];
    } else if (cleric_lvl >= 9 && cleric_lvl <= 13) {
        level_band = 9;
    } else {
        level_band = 10;
    }

    while (attack_find_lowest_e9_target(&target, player) &&
           turnings_left > 0 && resisted == false) {
        int needed;

        if (turn_undead_entry(&needed, target->field_E9, level_band) == false) {
            resisted = true;
            continue;
        }

        if (roll < (needed < 0 ? -needed : needed)) {
            resisted = true;
            continue;
        }

        any_turned = true;

        combatmap_redraw_if_focus_on(false, 3, target);
        gbl.display_hitpoints_ac = true;
        character_combat_display_summary(target);

        if (needed > 0) {
            player_actions(target)->fleeing = true;
            character_magic_attack_display("is turned", true, target);
        } else {
            character_display_status_string(false, 10, "Is destroyed", target);
            combatmap_combatant_killed(target);
            target->health_status = STATUS_GONE;
            target->in_combat     = false;
        }

        if (destroy_budget > 0) {
            destroy_budget -= 1;
        }

        turnings_left -= 1;

        /* Destroying one costs nothing out of the d12 while the budget lasts. */
        if (turnings_left == 0 && destroy_budget > 0 && needed < 0) {
            turnings_left++;
        }

        character_clear_text_area();
    }

    if (any_turned == false) {
        character_print_message("Nothing Happens...");
    }

    character_count_combat_teams();
    character_clear_actions(player);

    character_clear_text_area();
}

/* sub_3F433 */
bool attack_find_lowest_e9_target(Player **output, Player *player)
{
    CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
    int count;
    int min_e9 = 13;
    bool result = false;

    if (output == NULL) {
        return false;
    }

    *output = NULL;

    if (player == NULL) {
        log_warn("attack: the weakest undead near nobody");
        return false;
    }

    count = character_build_near_targets(near_targets,
                                         (int)COAB_ARRAY_LEN(near_targets), 0xff,
                                         player);

    for (int i = 0; i < count; i++) {
        Player *target = near_targets[i].player;

        if (player_actions(target)->fleeing == false &&
            target->field_E9 > 0 &&
            target->field_E9 < min_e9) {
            min_e9  = target->field_E9;
            *output = target;
            result  = true;
        }
    }

    return result;
}

/* sub_3F4EB */
bool attack_deliver_blows(Item *item, int arg_8, Player *target, Player *attacker)
{
    int  target_ac;
    bool turn_complete = false;
    bool behind_attack = arg_8 != 0;
    bool anything_landed = false;   /* var_11 */
    bool target_not_in_combat = false;
    AttackType attack_type = ATTACK_TYPE_NORMAL;
    Action *action;

    if (target == NULL || attacker == NULL) {
        log_warn("attack: blows with nobody at one end of them");
        return true;
    }

    action = player_actions(attacker);

    gbl.attack_hit_count[1]  = 0;
    gbl.attack_hit_count[2]  = 0;
    gbl.attack_made_count[1] = 0;
    gbl.attack_made_count[2] = 0;
    gbl.damage = 0;

    action->field_8 = true;

    if (player_is_held(target)) {
        /* A held target is killed outright, and that is the whole turn. */
        sound_play(SOUND_ATTACK_HELD);

        /* The C# walked attackIdx down until it found an attack with swings
         * left; with both counts at zero it walked past 0 and threw. */
        while (action->attack_idx > 1 &&
               player_attacks_left(attacker, action->attack_idx) == 0) {
            action->attack_idx--;
        }
        if (player_attacks_left(attacker, action->attack_idx) == 0) {
            log_warn("attack: %s slays a held target with no attacks left",
                     attacker->name);
        }

        gbl.attack_made_count[action->attack_idx] += 1;

        display_attack_message(true, 1, target->hit_point_current + 5,
                              ATTACK_TYPE_SLAY, target, attacker);
        effect_remove_invisibility(attacker);

        attacker->attack1_attacks_left = 0;
        attacker->attack2_attacks_left = 0;

        turn_complete = true;
    } else {
        Item *weapon = player_primary_weapon(attacker);

        /* A weapon does its large-target damage against anything bigger than a
         * man: field_DE over 0x80, or a size of two or more. */
        if (weapon != NULL &&
            (target->field_DE > 0x80 || (target->field_DE & 7) > 1)) {
            const ItemData *item_info = item_data(weapon->type);

            attacker->attack1_dice_count    = item_info->dice_count_large;
            attacker->attack1_dice_size     = item_info->dice_size_large;
            attacker->attack1_damage_bonus -= item_info->bonus_normal;
            attacker->attack1_damage_bonus += item_info->bonus_large;
        }

        character_recalc_values(target);
        effect_check_affects(target, CHECK_TYPE_11);

        if (attack_can_backstab(target, attacker)) {
            target_ac = target->ac_behind - 4;
        } else {
            /* Someone who has been spun round more than half a turn is fighting
             * in the wrong direction and can be hit from behind. */
            if (player_actions(target)->attacks_received > 1 &&
                attack_target_direction(target, attacker) ==
                    player_actions(target)->direction &&
                player_actions(target)->direction_changes > 4) {
                behind_attack = true;
            }

            if (behind_attack) {
                target_ac = target->ac_behind;
            } else {
                target_ac = target->ac;
            }
        }

        target_ac += attack_ranged_defense_bonus(target, attacker);

        if (behind_attack) {
            attack_type = ATTACK_TYPE_BEHIND;
        }

        if (attack_can_backstab(target, attacker)) {
            attack_type = ATTACK_TYPE_BACKSTAB;
        }

        for (int attack_idx = action->attack_idx; attack_idx >= 1; attack_idx--) {
            while (player_attacks_left(attacker, attack_idx) > 0 &&
                   target_not_in_combat == false) {
                player_attacks_left_dec(attacker, attack_idx);
                action->attack_idx = attack_idx;

                gbl.attack_made_count[attack_idx] += 1;

                if (effect_pc_can_hit_target(target_ac, target, attacker) ||
                    player_is_held(target)) {
                    gbl.attack_hit_count[attack_idx] += 1;

                    sound_play(SOUND_ATTACK_HELD);
                    anything_landed = true;
                    roll_attack_damage(attack_idx, target, attacker);
                    display_attack_message(true, gbl.damage, gbl.damage,
                                          attack_type, target, attacker);

                    if (target->in_combat) {
                        /* CheckType 2 for the first attack, 3 for the second:
                         * a weapon's own on-hit affect. */
                        effect_check_affects(attacker,
                                             (CheckType)(attack_idx + 1));
                    }

                    if (target->in_combat == false) {
                        target_not_in_combat = true;
                    }

                    if (attacker->in_combat == false) {
                        player_attacks_left_set(attacker, attack_idx, 0);
                    }
                }
            }
        }

        /* A dart of the hornet's nest is spent as soon as it is thrown. */
        if (item != NULL && item->count == 0 &&
            item->type == ITEM_DART_OF_HORNETS_NEST) {
            attacker->attack1_attacks_left = 0;
            attacker->attack2_attacks_left = 0;
        }

        if (anything_landed == false) {
            sound_play(SOUND_9);
            display_attack_message(false, 0, 0, attack_type, target, attacker);
        }

        turn_complete = true;
        if (attacker->attack1_attacks_left > 0 ||
            attacker->attack2_attacks_left > 0) {
            turn_complete = false;
        }

        action->max_sweap_targets = 0;
    }

    if (attacker->in_combat == false) {
        turn_complete = true;
    }

    if (turn_complete) {
        character_clear_actions(attacker);
    }

    return turn_complete;
}

/* sub_3F94D */
void attack_recalc_attacks_received(Player *target, Player *attacker)
{
    int target_dir;
    int dir_diff;
    Action *action;

    if (target == NULL || attacker == NULL) {
        log_warn("attack: counting a blow with nobody at one end of it");
        return;
    }

    action = player_actions(target);
    action->attacks_received++;

    target_dir = attack_target_direction(attacker, target);

    dir_diff = ((target_dir - action->direction) + 8) % 8;

    if (dir_diff > 4) {
        dir_diff = 8 - dir_diff;
    }

    action->direction_changes = (action->direction_changes + dir_diff) % 8;
}

/* sub_3F9DB */
bool attack_target(Item *ranged_weapon, int attack_type, Player *target,
                   Player *attacker)
{
    int dir = 0;
    bool turn_complete = true;
    Item *weapon;

    if (target == NULL || attacker == NULL) {
        log_warn("attack: an attack with nobody at one end of it");
        return true;
    }

    gbl.focus_combat_area_on_player = true;
    gbl.display_hitpoints_ac        = true;

    /* Something happened, so the round-limit that ends a fight nobody is
     * fighting is pushed back. */
    gbl.combat_round_no_action_limit =
        gbl.combat_round + GBL_COMBAT_ROUND_NO_ACTION_VALUE;

    if (player_actions(target)->attacks_received < 2 && attack_type == 0) {
        dir = attack_target_direction(attacker, target);

        player_actions(target)->direction = (dir + 4) % 8;
    } else if (combatmap_player_on_screen_p(false, target)) {
        dir = player_actions(target)->direction;

        if (attack_type == 0) {
            player_actions(target)->direction = (dir + 4) % 8;
        }
    }

    if (combatmap_player_on_screen_p(false, target)) {
        combatmap_draw_player(false, COMBAT_ICON_NORMAL, dir, target);
    }

    dir = attack_target_direction(target, attacker);
    character_combat_display_summary(attacker);

    combatmap_draw_player(false, COMBAT_ICON_ATTACK, dir, attacker);

    player_actions(attacker)->target = target;

    input_sys_delay(100);

    if (ranged_weapon != NULL) {
        attack_draw_ranged(ranged_weapon, target, attacker);
    }

    /* A sling's stone is drawn whether or not the caller named the ammunition. */
    weapon = player_primary_weapon(attacker);
    if (weapon != NULL &&
        (weapon->type == ITEM_SLING || weapon->type == ITEM_STAFF_SLING)) {
        attack_draw_ranged(weapon, target, attacker);
    }

    if (attacker->attack1_attacks_left > 0 ||
        attacker->attack2_attacks_left > 0) {
        Player *player_bkup = gbl.selected_player;

        gbl.selected_player = attacker;

        turn_complete = attack_deliver_blows(ranged_weapon, attack_type, target,
                                             attacker);

        if (ranged_weapon != NULL) {
            if (ranged_weapon->count > 0) {
                /* What is left of the stack is what was not thrown. */
                ranged_weapon->count = gbl.attack_made_count[1];
            }

            if (ranged_weapon->count == 0) {
                if (character_is_weapon_ranged_melee(attacker) &&
                    ranged_weapon->affect_3 != AFFECT_89) {
                    /* A thrown spear or hand axe lands on the ground where it
                     * can be picked up again. */
                    Item new_item = *ranged_weapon;

                    new_item.readied = false;

                    gbl.item_ptr = gbl_ground_item_add(&new_item);

                    character_lose_item(ranged_weapon, attacker);
                } else {
                    character_lose_item(ranged_weapon, attacker);
                }
            }
        }

        character_recalc_values(attacker);
        gbl.selected_player = player_bkup;
    }

    if (turn_complete) {
        character_clear_actions(attacker);
    }

    if (combatmap_player_on_screen_p(false, attacker)) {
        combatmap_draw_player(true, COMBAT_ICON_ATTACK,
                              player_actions(attacker)->direction, attacker);
        combatmap_draw_player(false, COMBAT_ICON_NORMAL,
                              player_actions(attacker)->direction, attacker);
    }

    return turn_complete;
}

/* sub_3FCED */
int attack_ranged_defense_bonus(Player *target, Player *attacker)
{
    Item *weapon;
    int range;
    int one_third_range;
    int ac_adjustment = 0;

    if (target == NULL || attacker == NULL) {
        return 0;
    }

    if (character_is_weapon_ranged(attacker) == false) {
        return 0;
    }

    weapon = player_primary_weapon(attacker);

    if (weapon == NULL) {
        /* The C# read activeItems.primaryWeapon.range here; a ranged attack
         * with nothing readied would have thrown. */
        log_warn("attack: %s fires with nothing readied", attacker->name);
        return 0;
    }

    range           = character_target_range(target, attacker);
    one_third_range = (item_data(weapon->type)->range - 1) / 3;

    if (range > one_third_range) {
        range -= one_third_range;
        ac_adjustment += 2;
    }

    if (range > one_third_range) {
        range -= one_third_range;
        ac_adjustment += 3;
    }

    return ac_adjustment;
}

/* sub_3FDFE */
bool attack_find_healing_target(Player **target, Player *healer)
{
    Player *lowest_target = NULL;
    int lowest_hp = 0xff;
    const DownedPlayerTile *downed = NULL;   /* var_8 */

    if (target == NULL) {
        return false;
    }

    *target = NULL;

    if (healer == NULL) {
        log_warn("attack: a cure cast by nobody");
        return false;
    }

    /* Direction 8 is the no-move entry, so the healer's own square is looked at
     * along with the eight around it. */
    for (int dir = 0; dir <= 8; dir++) {
        Point map = point_add(gbl_map_direction_delta(dir),
                              combatmap_player_map_pos(healer));
        int ground_tile;
        int player_index;

        combatmap_at_map_xy(&ground_tile, &player_index, map);

        if (player_index > 0) {
            Player *candidate = gbl.player_array[player_index];

            if (candidate == NULL) {
                continue;
            }

            if (candidate->combat_team == healer->combat_team &&
                candidate->hit_point_current < candidate->hit_point_max) {
                if (candidate->hit_point_current < lowest_hp ||
                    (candidate == healer &&
                     candidate->hit_point_current < (candidate->hit_point_max / 2))) {
                    lowest_target = candidate;
                    lowest_hp     = candidate->hit_point_current;
                }
            }
        } else if (ground_tile == TILE_DOWN_PLAYER) {
            /* FindLast: the newest body lying on the square wins. */
            for (int i = 0; i < gbl.downed_player_count; i++) {
                const DownedPlayerTile *cell = &gbl.downed_players[i];

                if (cell->target != NULL && point_eq(cell->map, map) &&
                    cell->target->health_status != STATUS_TEMPGONE &&
                    cell->target->health_status != STATUS_RUNNING &&
                    cell->target->health_status != STATUS_UNCONSCIOUS) {
                    downed = cell;
                }
            }
        }
    }

    /* Someone on their feet and nearly gone comes before a body on the floor. */
    if (lowest_hp < 8 || downed == NULL) {
        *target = lowest_target;
    } else {
        *target = downed->target;
    }

    return *target != NULL;
}


/* ------------------------------------------------------------------ spells */

/* The `sc => true` the C# passed to Rebuild_SortedCombatantList: an area spell
 * touches everyone it reaches, friend and foe alike. */
static bool filter_any_combatant(const Player *player, void *ctx)
{
    (void)player;
    (void)ctx;

    return true;
}

/* seg600:27CB unk_18ADB. The affects that a held target is already under, so
 * casting one of them on them again would do nothing. Entry 0 is filler: the C#
 * read entries 1..4, which is bless, snake charm, paralyze and sleep - helpless
 * sits one past the end of what is looked at. */
static const Affects UNK_18ADB[5] = {
    AFFECT_BLESS, AFFECT_SNAKE_CHARM, AFFECT_PARALYZE, AFFECT_SLEEP,
    AFFECT_HELPLESS
};

/* sub_4001C */
bool attack_pick_spell_target(DownedPlayerTile *out, bool can_target_empty_ground,
                              QuickFight quick_fight, int spell_id)
{
    const SpellEntry *entry = spell_entry(spell_id);
    bool found = false;

    if (out == NULL) {
        return false;
    }

    if (gbl.selected_player == NULL) {
        log_warn("attack: a spell aimed by nobody");
        downed_player_tile_clear(out);
        return false;
    }

    if (entry == NULL) {
        log_warn("attack: no spell 0x%x to aim", spell_id);
        downed_player_tile_clear(out);
        return false;
    }

    if (quick_fight == QUICK_FIGHT_FALSE) {
        /* Spell 0x53 may only be aimed at bare ground, never at a combatant. */
        bool allow_target = spell_id != 0x53;

        found = attack_aim_menu(out, allow_target, can_target_empty_ground, false,
                                spellcast_spell_range(spell_id),
                                gbl.selected_player);
        player_actions(gbl.selected_player)->target = out->target;
    } else if (entry->field_e == 0) {
        /* A spell with no target of its own goes to the caster - except a cure,
         * which goes to whoever needs it most. */
        out->target = gbl.selected_player;

        if (spell_id != 3 ||
            attack_find_healing_target(&out->target, gbl.selected_player)) {
            out->map = combatmap_player_map_pos(out->target);
            found = true;
        }
    } else {
        /* One try at whatever the AI's own targeting comes up with. */
        int tries = 1;

        while (tries > 0 && found == false) {
            bool usable = true;

            if (attack_find_target(true, 0, spellcast_spell_range(spell_id),
                                   gbl.selected_player)) {
                Player *target = player_actions(gbl.selected_player)->target;

                if (target != NULL && player_is_held(target)) {
                    for (int i = 1; i <= 4; i++) {
                        if (entry->affect_id == UNK_18ADB[i]) {
                            usable = false;
                        }
                    }
                }

                if (usable) {
                    out->target = player_actions(gbl.selected_player)->target;
                    out->map    = combatmap_player_map_pos(out->target);
                    found = true;
                }
            }

            tries -= 1;
        }
    }

    if (found) {
        gbl.target_pos = out->map;
    } else {
        downed_player_tile_clear(out);
    }

    return found;
}

/* ovr014.target */
bool attack_spell_targets(QuickFight quick_fight, int spell_id)
{
    DownedPlayerTile pick;
    const SpellEntry *entry = spell_entry(spell_id);
    bool cast_spell = true;
    int target_type;

    downed_player_tile_clear(&pick);

    gbl_spell_targets_clear();
    gbl.byte_1D2C7 = false;

    if (gbl.selected_player == NULL) {
        log_warn("attack: a spell aimed by nobody");
        return false;
    }
    if (entry == NULL) {
        log_warn("attack: no spell 0x%x to aim", spell_id);
        return false;
    }

    gbl.target_pos = combatmap_player_map_pos(gbl.selected_player);

    target_type = entry->field_6 & 0x0f;

    if (target_type == 0) {
        /* The caster, and nobody else. */
        gbl_spell_targets_clear();
        gbl_spell_target_add(gbl.selected_player);
    } else if (target_type == 5) {
        /* As many weak targets as the spell's hit-dice budget covers: sleep and
         * charm, which are wasted on anything tough. */
        int spent = 0;       /* var_5 */
        int budget;          /* var_4 */
        bool stop_loop = false;

        gbl_spell_targets_clear();

        if (spell_id == 0x4f) {
            budget = character_spell_max_target_count(0x4f);
        } else {
            budget = effect_roll_dice(4, 2);
        }

        do {
            if (attack_pick_spell_target(&pick, false, quick_fight, spell_id)) {
                if (gbl_spell_target_exists(pick.target) == false) {
                    Player *target = pick.target;

                    gbl_spell_target_add(target);

                    gbl.target_pos = combatmap_player_map_pos(pick.target);

                    if (spell_id != 0x4f) {
                        int hit_dice = target->hit_dice;

                        if (hit_dice == 0 || hit_dice == 1) {
                            spent += 1;
                        } else if (hit_dice == 2) {
                            spent += 2;
                        } else if (hit_dice == 3) {
                            spent += 4;
                        } else {
                            spent += 8;
                        }
                    } else {
                        int size = target->field_DE;

                        if (size == 1) {
                            spent += 1;
                        } else if (size == 2 || size == 3) {
                            spent += 2;
                        } else if (size == 4) {
                            spent += 4;
                        }
                    }

                    if (gbl.spell_target_count > 0 && spent > budget) {
                        stop_loop = true;
                    }
                } else {
                    if (quick_fight != QUICK_FIGHT_FALSE) {
                        budget -= 1;
                    } else {
                        character_print_message("Already been targeted");
                    }
                }

                combatmap_redraw_position(combatmap_player_map_pos(pick.target));
            } else {
                stop_loop = true;
            }
        } while (stop_loop == false && budget != 0);
    } else if (target_type == 0x0f) {
        if (attack_pick_spell_target(&pick, false, quick_fight, spell_id)) {
            if (player_actions(gbl.selected_player)->target != NULL) {
                gbl_spell_targets_clear();
                gbl_spell_target_add(player_actions(gbl.selected_player)->target);
            } else {
                SortedCombatant list[GBL_MAX_COMBATANT_COUNT];
                int count;

                /* engine/ovr014.cs: "it doesn't make sense to mask the low
                 * nibble then shift it out" - and it does not: the radius here is
                 * always zero, so only the aimed square is touched. Kept as the
                 * original had it. */
                count = target_sorted_combatants(list, (int)COAB_ARRAY_LEN(list),
                                                 1, (entry->field_6 & 0x0f) >> 4,
                                                 gbl.target_pos,
                                                 filter_any_combatant, NULL);

                gbl_spell_targets_clear();
                for (int i = 0; i < count; i++) {
                    gbl_spell_target_add(list[i].player);
                }
                gbl.byte_1D2C7 = true;
            }
        } else {
            cast_spell = false;
        }
    } else if (target_type >= 8 && target_type <= 0x0e) {
        /* An area spell: everyone within the radius the low three bits give. */
        if (attack_pick_spell_target(&pick, true, quick_fight, spell_id)) {
            SortedCombatant list[GBL_MAX_COMBATANT_COUNT];
            int count;

            count = target_sorted_combatants(list, (int)COAB_ARRAY_LEN(list), 1,
                                             entry->field_6 & 7, gbl.target_pos,
                                             filter_any_combatant, NULL);

            gbl_spell_targets_clear();
            for (int i = 0; i < count; i++) {
                gbl_spell_target_add(list[i].player);
            }

            gbl.byte_1D2C7 = true;
        } else {
            cast_spell = false;
        }
    } else {
        /* A fixed number of combatants, one to four. */
        int max_targets = (entry->field_6 & 3) + 1;

        gbl_spell_targets_clear();

        while (max_targets > 0) {
            if (attack_pick_spell_target(&pick, false, quick_fight, spell_id)) {
                if (gbl_spell_target_exists(pick.target) == false) {
                    gbl_spell_target_add(pick.target);
                    max_targets -= 1;

                    gbl.target_pos = combatmap_player_map_pos(pick.target);
                } else {
                    if (quick_fight == QUICK_FIGHT_FALSE) {
                        character_print_message("Already been targeted");
                    } else {
                        max_targets -= 1;
                    }
                }

                combatmap_redraw_position(combatmap_player_map_pos(pick.target));
            } else {
                max_targets = 0;
            }
        }

        if (gbl.spell_target_count == 0) {
            cast_spell = false;
            gbl.target_pos = point_make(0, 0);
        }
    }

    return cast_spell;
}

/* ovr014.spell_menu3 */
void attack_spell_menu(bool *casting_spell, QuickFight quick_fight, int spell_id)
{
    Player *player = gbl.selected_player;
    bool var_6 = true;
    int  var_5 = -1;

    if (casting_spell != NULL) {
        *casting_spell = false;
    }

    if (player == NULL) {
        log_warn("attack: a spell cast by nobody");
        return;
    }

    if (spell_id == 0) {
        spell_id = viewplayer_spell_menu2(&var_6, &var_5,
                                          SPELL_SOURCE_CAST,
                                          SPELL_LOC_MEMORY);
    }

    if (spell_id > 0 && spell_entry(spell_id) != NULL &&
        spell_entry(spell_id)->when_cast == SPELL_WHEN_CAMP) {
        character_print_message("Camp Only Spell");
        spell_id = 0;
    }

    if (quick_fight == QUICK_FIGHT_FALSE) {
        character_redraw_combat_screen();
        gbl.focus_combat_area_on_player = true;
        gbl.display_hitpoints_ac        = true;

        combatmap_redraw_if_focus_on(true, 3, player);
        character_combat_display_summary(player);
    }

    if (spell_id > 0) {
        const SpellEntry *entry = spell_entry(spell_id);
        int delay;

        if (entry == NULL) {
            log_warn("attack: no spell 0x%x to cast", spell_id);
            return;
        }

        delay = entry->casting_delay / 3;

        if (delay == 0) {
            /* It goes off at once. */
            spellcast_resolve_spell(true, quick_fight, spell_id);

            if (casting_spell != NULL) {
                *casting_spell = true;
            }
            character_clear_actions(player);
        } else {
            /* A long spell goes off later in the round; a blow before then loses
             * it. */
            if (casting_spell != NULL) {
                *casting_spell = true;
            }
            character_display_status_string(true, 10, "Begins Casting", player);

            player_actions(player)->spell_id = spell_id;

            if (player_actions(player)->delay > delay) {
                player_actions(player)->delay = delay;
            } else {
                player_actions(player)->delay = 1;
            }
        }
    }
}

/* ------------------------------------------------------ facing and backstabs */

/* sub_408D7 */
bool attack_can_backstab(Player *target, Player *attacker)
{
    Item *weapon;

    if (target == NULL || attacker == NULL) {
        return false;
    }

    if (player_skill_level(attacker, SKILL_THIEF) <= 0) {
        return false;
    }

    weapon = player_primary_weapon(attacker);

    /* Bare hands count: the original tested the weapon for being one of the
     * knives and swords a thief can backstab with, and a null weapon passed. */
    if (weapon != NULL &&
        weapon->type != ITEM_DROW_LONG_SWORD &&
        weapon->type != ITEM_CLUB &&
        weapon->type != ITEM_DAGGER &&
        weapon->type != ITEM_BROAD_SWORD &&
        weapon->type != ITEM_LONG_SWORD &&
        weapon->type != ITEM_SHORT_SWORD) {
        return false;
    }

    /* Man-sized or smaller, already fighting someone else, and facing away. */
    return player_actions(target)->attacks_received > 1 &&
           (target->field_DE & 0x7f) <= 1 &&
           attack_target_direction(target, attacker) ==
               player_actions(target)->direction;
}

/* sub_409BC */
u8 attack_target_direction(const Player *player_b, const Player *player_a)
{
    Point plyr_a = combatmap_player_map_pos(player_a);
    Point plyr_b = combatmap_player_map_pos(player_b);
    int diff_x = plyr_b.x - plyr_a.x;
    int diff_y = plyr_b.y - plyr_a.y;
    u8 direction = 0;
    bool solved = false;

    diff_x = diff_x < 0 ? -diff_x : diff_x;
    diff_y = diff_y < 0 ? -diff_y : diff_y;

    /* 0x26a/0x100 is tan(67.5 degrees) and 0x6a/0x100 is tan(22.5), so the two
     * gradients cut the circle into true eighths: the straight directions own the
     * 45 degrees around each axis and the diagonals own what is left.
     *
     * The C# looped until a case matched, with `direction` a byte that would have
     * wrapped round for ever had none of them ever matched. One pass covers every
     * point on the map, so the bound below is only ever reached by a bug. */
    while (solved == false && direction < 8) {
        switch (direction) {
        case 0:     /* north */
            solved = !(plyr_b.y > plyr_a.y ||
                       ((0x26a * diff_x) / 0x100) > diff_y);
            break;

        case 2:     /* east */
            solved = !(plyr_b.x < plyr_a.x ||
                       ((0x6a * diff_x) / 0x100) < diff_y);
            break;

        case 4:     /* south */
            solved = !(plyr_b.y < plyr_a.y ||
                       ((0x26a * diff_x) / 0x100) > diff_y);
            break;

        case 6:     /* west */
            solved = !(plyr_b.x > plyr_a.x ||
                       ((0x6a * diff_x) / 0x100) < diff_y);
            break;

        case 1:     /* north east */
            solved = !(plyr_b.y > plyr_a.y ||
                       plyr_b.x < plyr_a.x ||
                       ((0x26a * diff_x) / 0x100) < diff_y ||
                       ((0x6a * diff_x) / 0x100) > diff_y);
            break;

        case 3:     /* south east */
            solved = !(plyr_b.y < plyr_a.y ||
                       plyr_b.x < plyr_a.x ||
                       ((0x26a * diff_x) / 0x100) < diff_y ||
                       ((0x6a * diff_x) / 0x100) > diff_y);
            break;

        case 5:     /* south west */
            solved = !(plyr_b.y < plyr_a.y ||
                       plyr_b.x > plyr_a.x ||
                       ((0x26a * diff_x) / 0x100) < diff_y ||
                       ((0x6a * diff_x) / 0x100) > diff_y);
            break;

        case 7:     /* north west */
            solved = !(plyr_b.y > plyr_a.y ||
                       plyr_b.x > plyr_a.x ||
                       ((0x26a * diff_x) / 0x100) < diff_y ||
                       ((0x6a * diff_x) / 0x100) > diff_y);
            break;

        default:
            break;
        }

        if (solved == false) {
            direction++;
        }
    }

    if (solved == false) {
        log_warn("attack: no direction from (%d,%d) to (%d,%d)",
                 plyr_a.x, plyr_a.y, plyr_b.x, plyr_b.y);
        return 0;
    }

    return direction;
}

/* --------------------------------------------------------- missiles in flight */

/* sub_40BF1 */
void attack_draw_ranged(Item *item, Player *target, Player *attacker)
{
    int dir;
    int frame_count = 1;
    int delay = 10;
    int icon_id = 13;

    if (item == NULL || target == NULL || attacker == NULL) {
        log_warn("attack: a missile with nothing to fly between");
        return;
    }

    sound_play(SOUND_C);

    dir = attack_target_direction(target, attacker);

    switch (item->type) {
    /* A dart or an arrow keeps its shape, so only the sprite for its heading is
     * loaded and it does not tumble. */
    case ITEM_DART:
    case ITEM_JAVELIN:
    case ITEM_DART_OF_HORNETS_NEST:
    case ITEM_QUARREL:
    case ITEM_SPEAR:
    case ITEM_ARROW:
        if ((dir & 1) == 1) {
            if (dir == 3 || dir == 5) {
                character_load_missile_dax((dir == 5), 0, COMBAT_ICON_ATTACK,
                                           icon_id + 1);
            } else {
                character_load_missile_dax((dir == 7), 0, COMBAT_ICON_NORMAL,
                                           icon_id + 1);
            }
        } else {
            if (dir >= 4) {
                character_load_missile_dax(false, 0, COMBAT_ICON_ATTACK,
                                           icon_id + (dir % 4));
            } else {
                character_load_missile_dax(false, 0, COMBAT_ICON_NORMAL,
                                           icon_id + (dir % 4));
            }
        }
        sound_play(SOUND_C);
        break;

    /* A thrown axe or club tumbles: all four cells, slowly. */
    case ITEM_HAND_AXE:
    case ITEM_CLUB:
    case ITEM_GLAIVE:
        character_load_missile_icons(icon_id + 3);
        frame_count = 4;
        delay = 50;
        sound_play(SOUND_9);
        break;

    case ITEM_TYPE_85:
    case ITEM_FLASK_OF_OIL:
        character_load_missile_icons(icon_id + 4);
        frame_count = 4;
        delay = 50;
        sound_play(SOUND_6);
        break;

    case ITEM_STAFF_SLING:
    case ITEM_SLING:
    case ITEM_SPINE:
        icon_id++;
        character_load_missile_dax(false, 0, COMBAT_ICON_NORMAL, icon_id + 7);
        character_load_missile_dax(false, 1, COMBAT_ICON_ATTACK, icon_id + 7);
        frame_count = 2;
        delay = 10;
        sound_play(SOUND_6);
        break;

    default:
        character_load_missile_dax(false, 0, COMBAT_ICON_NORMAL, icon_id + 7);
        character_load_missile_dax(false, 1, COMBAT_ICON_ATTACK, icon_id + 7);
        frame_count = 2;
        delay = 20;
        sound_play(SOUND_9);
        break;
    }

    character_draw_missile_attack(delay, frame_count,
                                  combatmap_player_map_pos(target),
                                  combatmap_player_map_pos(attacker));
}

/* sub_40E00 */
void attack_calc_enemy_health_percentage(void)
{
    int max_total = 0;
    int current_total = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        if (player->combat_team == TEAM_ENEMY) {
            if (player->in_combat) {
                current_total += player->hit_point_current;
            }

            max_total += player->hit_point_max;
        }
    }

    /* Rounded down to the nearest five per cent, as the original did - and left
     * alone when there is no enemy at all. */
    if (max_total > 0) {
        gbl.enemy_health_percentage = ((20 * current_total) / max_total) * 5;
    }
}

/* sub_40E8F */
int attack_max_opposition_moves(Player *player)
{
    int max_moves = 0;

    if (player == NULL) {
        log_warn("attack: the opposition of nobody");
        return 0;
    }

    for (int i = 0; i < gbl.team_count; i++) {
        Player *mob = gbl.team_list[i];

        if (mob == NULL) {
            continue;
        }

        if ((int)player_opposite_team(player) == mob->combat_team &&
            mob->in_combat) {
            int moves = attack_calc_moves(mob) / 2;

            if (moves > max_moves) {
                max_moves = moves;
            }
        }
    }

    return max_moves;
}

/* sub_40F1F */
bool attack_can_attack_target(Player *target, Player *attacker)
{
    if (target == NULL || attacker == NULL) {
        log_warn("attack: asking whether nobody may be attacked");
        return false;
    }

    if ((int)player_opposite_team(target) == attacker->combat_team ||
        attacker->quick_fight == QUICK_FIGHT_TRUE) {
        return true;
    }

    if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Attack Ally: ") != 'Y') {
        return false;
    }

    /* Turning on the party turns every NPC in it against the party, for the rest
     * of the fight. */
    gbl.area2_ptr->field_666 = 1;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        if (player->health_status == STATUS_OKEY &&
            player->control_morale >= CONTROL_NPC_BASE) {
            player->combat_team = TEAM_ENEMY;
            player_actions(player)->target = NULL;
        }
    }

    character_count_combat_teams();

    return true;
}

/* --------------------------------------------------------------- aiming */

/* ovr014.aim_sub_menu, which the C# also labelled Aim_menu. */
char attack_aim_sub_menu(bool show_target, bool show_range, int max_range,
                         Player *target, Player *attacker)
{
    char text[64];
    char word[16];
    int range;

    if (target == NULL || attacker == NULL) {
        log_warn("attack: aiming at nobody");
        return '\0';
    }

    range = character_target_range(target, attacker);

    /* The C# worked out the direction here and threw it away. */
    (void)attack_target_direction(target, attacker);

    word[0] = '\0';

    if (show_range) {
        char range_txt[24];

        snprintf(range_txt, sizeof(range_txt), "Range = %d  ", range);
        text_display_string(range_txt, 0, 10, 0x17, 0);
    }

    if (range <= max_range) {
        if (show_range == false) {
            if (show_target) {
                snprintf(word, sizeof(word), "Target ");
            }
        } else if (target != attacker) {
            if (character_is_weapon_ranged(attacker) == false) {
                snprintf(word, sizeof(word), "Target ");
            } else {
                CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
                Item *dummy_item = NULL;

                /* A bow may not be fired at something standing next to you, but
                 * a thrown spear may. */
                if (character_current_attack_item(&dummy_item, attacker) &&
                    (character_build_near_targets(near_targets,
                                                  (int)COAB_ARRAY_LEN(near_targets),
                                                  1, attacker) == 0 ||
                     character_is_weapon_ranged_melee(attacker))) {
                    snprintf(word, sizeof(word), "Target ");
                }
            }
        }
    }

    snprintf(text, sizeof(text), "Next Prev Manual %sCenter Exit", word);

    combatmap_redraw_if_focus_on(true, 3, target);
    gbl.display_hitpoints_ac = true;
    character_combat_display_summary(target);

    return prompt_display_input_simple(false, 1, GBL_DEFAULT_MENU_COLORS, text,
                                       "Aim:");
}

/* sub_411D8 */
bool attack_commit_target(DownedPlayerTile *out, bool show_range, Player *target,
                          Player *attacker)
{
    bool result = true;

    if (out == NULL || attacker == NULL) {
        log_warn("attack: nothing to aim");
        return false;
    }

    if (show_range && attack_can_attack_target(target, attacker) == false) {
        result = false;
    }

    if (result == false) {
        downed_player_tile_clear(out);
        return false;
    }

    out->target = target;
    out->map    = combatmap_player_map_pos(target);

    if (gbl.map_to_background_tile != NULL) {
        gbl.map_to_background_tile->draw_target_cursor = false;
    } else {
        log_warn("attack: no ground tile map outside a fight");
    }

    combatmap_redraw_area(8, 3, point_add(map_screen_top_left(),
                                          point_screen_center()));

    if (show_range) {
        if (attack_try_sweep(target, attacker)) {
            result = true;
            character_clear_actions(attacker);
        } else {
            Item *ranged_weapon = NULL;

            attack_recalc_attacks_received(target, attacker);

            /* A thrown weapon is not thrown at something you are standing next
             * to - it is swung. */
            if (character_is_weapon_ranged(attacker) &&
                character_current_attack_item(&ranged_weapon, attacker) &&
                character_is_weapon_ranged_melee(attacker) &&
                character_target_range(target, attacker) == 0) {
                ranged_weapon = NULL;
            }

            result = attack_target(ranged_weapon, 0, target, attacker);
        }
    }

    return result;
}

/* seg600 asc_41342: Escape (read as '\0'), 'E' for Exit and 'T' for Target, the
 * keys that end the manual aim. */
static bool key_ends_manual_aim(char key)
{
    return key == '\0' || key == 'E' || key == 'T';
}

/* ovr014.Target */
bool attack_target_cursor(DownedPlayerTile *out, bool allow_target,
                          bool can_target_empty_ground, bool show_range,
                          int max_range, Player *target, Player *player01)
{
    Point pos;
    char input_key = ' ';
    u8   dir = 8;
    bool result = false;

    if (out == NULL || player01 == NULL) {
        log_warn("attack: a cursor with nothing to aim");
        return false;
    }

    downed_player_tile_clear(out);

    pos = combatmap_player_map_pos(target);

    if (gbl.map_to_background_tile != NULL) {
        gbl.map_to_background_tile->draw_target_cursor = true;
        gbl.map_to_background_tile->size = 1;
    } else {
        log_warn("attack: no ground tile map outside a fight");
    }

    while (key_ends_manual_aim(input_key) == false) {
        int ground_tile;
        int player_at_xy;
        int range = 255;
        bool can_target = false;
        char text[32];

        combatmap_redraw_area(dir, 3, pos);
        pos = point_add(pos, gbl_map_direction_delta(dir));
        point_map_clamp(&pos);

        combatmap_at_map_xy(&ground_tile, &player_at_xy, pos);
        input_clear_keyboard();

        if (target_can_reach_range(&range, pos,
                                   combatmap_player_map_pos(player01))) {
            can_target = true;

            if (show_range) {
                char range_text[24];

                snprintf(range_text, sizeof(range_text), "Range = %d  ",
                         range / 2);
                text_display_string(range_text, 0, 10, 0x17, 0);
            }
        } else {
            if (show_range) {
                frames_clear_area(0x17, 0x27, 0x17, 0);
            }
        }

        /* The reach comes back in half-steps. */
        range /= 2;
        target = NULL;

        if (can_target) {
            if (player_at_xy > 0) {
                target = gbl.player_array[player_at_xy];
            } else if (ground_tile == TILE_DOWN_PLAYER) {
                for (int i = 0; i < gbl.downed_player_count; i++) {
                    if (point_eq(gbl.downed_players[i].map, pos)) {
                        if (gbl.downed_players[i].target != NULL) {
                            target = gbl.downed_players[i].target;
                        }
                        break;      /* Find: the first body on the square */
                    }
                }
            }
        }

        if (target != NULL) {
            gbl.display_hitpoints_ac = true;
            character_combat_display_summary(target);
        } else {
            frames_clear_region(TEXT_REGION_COMBAT_SUMMARY);
        }

        if (range > max_range || ground_move_cost(ground_tile) == 0xff) {
            can_target = false;
        }

        if (target != NULL) {
            if (attack_can_see_target(target, player01) == false ||
                allow_target == false) {
                can_target = false;
            }

            if (show_range) {
                /* Nobody attacks themselves, and a body on the floor is not
                 * attacked either - 0x1f is the fallen-character tile. */
                if (player01 == target ||
                    (player_at_xy == 0 && ground_tile == 0x1f)) {
                    can_target = false;
                } else {
                    Item *dummy_item = NULL;
                    CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];

                    if (character_is_weapon_ranged(player01) &&
                        (character_current_attack_item(&dummy_item, player01) == false ||
                         (character_build_near_targets(near_targets,
                                                      (int)COAB_ARRAY_LEN(near_targets),
                                                      1, player01) > 0 &&
                          character_is_weapon_ranged_melee(player01) == false))) {
                        can_target = false;
                    }
                }
            }
        } else if (can_target_empty_ground == false) {
            can_target = false;
        }

        if (can_target) {
            snprintf(text, sizeof(text), "Target Center Exit");
        } else {
            snprintf(text, sizeof(text), "Center Exit");
        }

        input_key = prompt_display_input_simple(false, 1,
                                                GBL_DEFAULT_MENU_COLORS, text,
                                                "(Use Cursor keys) ");

        switch (input_key) {
        case '\r':
        case 'T':
            if (gbl.map_to_background_tile != NULL) {
                gbl.map_to_background_tile->draw_target_cursor = false;
            }

            if (can_target) {
                out->map    = pos;
                out->target = target;

                if (show_range) {
                    result = attack_commit_target(out, show_range, out->target,
                                                  player01);
                } else {
                    result = true;
                }
            }

            if (can_target == false || result == false) {
                combatmap_redraw_position(pos);
                result = false;
                downed_player_tile_clear(out);
            }
            break;

        /* The cursor keys, by scan code. */
        case 'H': dir = 0; break;
        case 'I': dir = 1; break;
        case 'M': dir = 2; break;
        case 'Q': dir = 3; break;
        case 'P': dir = 4; break;
        case 'O': dir = 5; break;
        case 'K': dir = 6; break;
        case 'G': dir = 7; break;

        case '\0':
        case 'E':
            combatmap_redraw_position(pos);
            downed_player_tile_clear(out);
            result = false;
            break;

        case 'C':
            combatmap_redraw_area(8, 0, pos);
            dir = 8;
            break;

        default:
            dir = 8;
            break;
        }
    }

    return result;
}

/* sub_4188F */
int attack_copy_sorted_players(SortedCombatant *out, int out_size, Player *player)
{
    return target_sorted_combatants_for(out, out_size, player, 0x7f,
                                        filter_any_combatant, NULL);
}

/* sub_41932 */
Player *attack_step_combat_list(bool arg_2, int step, int *list_index,
                                Point *attacker_pos,
                                const SortedCombatant *sorted_list,
                                int sorted_count)
{
    Point target_pos;
    Player *new_target;

    if (list_index == NULL || attacker_pos == NULL || sorted_list == NULL) {
        return NULL;
    }

    if (sorted_count <= 0) {
        /* The C# indexed the empty array and threw. */
        log_warn("attack: stepping an empty target list");
        return NULL;
    }

    /* list_index is 1-based, as the original's was. */
    if (arg_2) {
        if (*list_index >= 1 && *list_index <= sorted_count) {
            *attacker_pos = sorted_list[*list_index - 1].pos;
        }
    } else {
        combatmap_redraw_position(*attacker_pos);
    }

    *list_index += step;

    if (*list_index == 0) {
        *list_index = sorted_count;
    }

    if (*list_index > sorted_count) {
        *list_index = 1;
    }

    if (*list_index < 1) {
        log_warn("attack: target list index %d is off the front of the list",
                 *list_index);
        *list_index = 1;
    }

    new_target = sorted_list[*list_index - 1].player;
    target_pos = sorted_list[*list_index - 1].pos;

    if (arg_2) {
        /* The aiming line, drawn from where the cursor was to where it is. */
        character_draw_missile_attack(0, 1, target_pos, *attacker_pos);
        *attacker_pos = target_pos;
    }

    return new_target;
}

/* seg600 unk_41AE5: Escape and 'E' for Exit, the keys that leave the aim menu. */
static bool key_ends_aim_menu(char key)
{
    return key == '\0' || key == 'E';
}

/* seg600 unk_41B05: the cursor keys' scan codes, which switch the aim menu over
 * to the manual cursor. */
static bool key_is_cursor(char key)
{
    return key == 'G' || key == 'H' || key == 'I' || key == 'K' ||
           key == 'M' || key == 'O' || key == 'P' || key == 'Q';
}

/* sub_41B25 */
bool attack_aim_menu(DownedPlayerTile *out, bool allow_target,
                     bool can_target_empty_ground, bool show_range,
                     int max_range, Player *attacker)
{
    SortedCombatant sorted_list[GBL_MAX_COMBATANT_COUNT];
    int sorted_count;
    Player *target;
    int list_index = 1;
    int next_prev_step = 0;
    int target_step = 0;
    Point attacker_pos = { 0, 0 };
    char input = ' ';
    bool result = false;
    int unseen_in_a_row = 0;

    if (out == NULL || attacker == NULL) {
        log_warn("attack: an aim menu with nobody aiming");
        return false;
    }

    /* Icon 0x19 is the aiming line's own sprite. */
    character_load_missile_dax(false, 0, COMBAT_ICON_NORMAL, 0x19);

    downed_player_tile_clear(out);

    if (max_range == -1 || max_range == 0xff) {
        Item *weapon = player_primary_weapon(attacker);

        if (weapon != NULL) {
            max_range = item_data(weapon->type)->range - 1;
        } else {
            max_range = 1;
        }
    }

    if (max_range == 0 || max_range == -1 || max_range == 0xff) {
        max_range = 1;
    }

    sorted_count = attack_copy_sorted_players(sorted_list,
                                              (int)COAB_ARRAY_LEN(sorted_list),
                                              attacker);

    if (sorted_count == 0) {
        /* The C# went straight on to index the empty list and threw. */
        log_warn("attack: %s has nothing at all to aim at", attacker->name);
        return false;
    }

    target = attack_step_combat_list(true, next_prev_step, &list_index,
                                     &attacker_pos, sorted_list, sorted_count);

    next_prev_step = 1;

    while (result == false && key_ends_aim_menu(input) == false) {
        if (attack_can_see_target(target, attacker) == false) {
            /* Skip over anyone who cannot be seen. The C# would cycle for ever
             * when none of them could be; a full pass with nothing visible ends
             * the menu instead. */
            if (++unseen_in_a_row > sorted_count) {
                log_warn("attack: %s can see none of the %d combatants",
                         attacker->name, sorted_count);
                break;
            }

            target = attack_step_combat_list(false, next_prev_step, &list_index,
                                             &attacker_pos, sorted_list,
                                             sorted_count);
            continue;
        }

        unseen_in_a_row = 0;

        input = attack_aim_sub_menu(allow_target, show_range, max_range, target,
                                   attacker);

        if (gbl.display_input_special_key_pressed == false) {
            switch (input) {
            case 'N':
                next_prev_step = 1;
                target_step    = 1;
                break;

            case 'P':
                next_prev_step = -1;
                target_step    = -1;
                break;

            /* The letters the cursor keys share with the menu words, when they
             * came in as letters rather than as scan codes. */
            case 'M':
            case 'H':
            case 'K':
            case 'G':
            case 'O':
            case 'Q':
            case 'I':
                result = attack_target_cursor(out, allow_target,
                                              can_target_empty_ground, show_range,
                                              max_range, target, attacker);
                character_load_missile_dax(false, 0, COMBAT_ICON_NORMAL, 0x19);

                sorted_count = attack_copy_sorted_players(sorted_list,
                                                          (int)COAB_ARRAY_LEN(sorted_list),
                                                          attacker);
                target_step = 0;
                break;

            case 'T':
                result = attack_commit_target(out, show_range, target, attacker);
                character_load_missile_dax(false, 0, COMBAT_ICON_NORMAL, 0x19);

                sorted_count = attack_copy_sorted_players(sorted_list,
                                                          (int)COAB_ARRAY_LEN(sorted_list),
                                                          attacker);
                target_step = 0;
                break;

            case 'C':
                combatmap_redraw_area(8, 0, combatmap_player_map_pos(target));
                target_step = 0;
                break;

            default:
                break;
            }
        } else if (key_is_cursor(input)) {
            result = attack_target_cursor(out, allow_target,
                                          can_target_empty_ground, show_range,
                                          max_range, target, attacker);
            character_load_missile_dax(false, 0, COMBAT_ICON_NORMAL, 0x19);

            sorted_count = attack_copy_sorted_players(sorted_list,
                                                      (int)COAB_ARRAY_LEN(sorted_list),
                                                      attacker);
            target_step = 0;
        }

        combatmap_redraw_position(combatmap_player_map_pos(target));

        if (sorted_count == 0) {
            log_warn("attack: nothing left to aim at");
            break;
        }

        target = attack_step_combat_list(
            (result == false && key_ends_aim_menu(input) == false), target_step,
            &list_index, &attacker_pos, sorted_list, sorted_count);
    }

    if (show_range) {
        frames_clear_area(0x17, 0x27, 0x17, 0);
    }

    return result;
}

/* sub_41E44 */
bool attack_find_target(bool clear_target, u8 arg_2, int max_range, Player *player)
{
    bool target_found = false;
    Player *target;
    bool second_pass = false;
    bool done = false;

    if (player == NULL) {
        log_warn("attack: a target for nobody");
        return false;
    }

    target = player_actions(player)->target;

    /* Whatever they were attacking is kept, unless it has joined their side,
     * left the fight or gone out of sight. */
    if (clear_target ||
        (target != NULL &&
         (target->combat_team == player->combat_team ||
          target->in_combat == false ||
          attack_can_see_target(target, player) == false))) {
        player_actions(player)->target = NULL;
    }

    if (player_actions(player)->target != NULL) {
        target_found = true;
    }

    while (target_found == false && done == false) {
        CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
        int count;
        int try_count = 20;

        done = second_pass;

        /* On the second pass walls are ignored, so a monster with nothing in
         * sight still has something to walk towards. */
        if (second_pass && clear_target == false) {
            if (gbl.map_to_background_tile != NULL) {
                gbl.map_to_background_tile->ignore_walls = true;
            } else {
                log_warn("attack: no ground tile map outside a fight");
            }
        }

        count = character_build_near_targets(near_targets,
                                            (int)COAB_ARRAY_LEN(near_targets),
                                            max_range, player);

        while (try_count > 0 && target_found == false && count > 0) {
            int roll;
            int chosen;

            try_count--;
            roll = effect_roll_dice(count, 1);

            chosen = roll - 1;
            if (chosen < 0 || chosen >= count) {
                log_warn("attack: rolled %d of %d targets", roll, count);
                break;
            }

            target = near_targets[chosen].player;

            if ((arg_2 != 0 && gbl.map_to_background_tile != NULL &&
                 gbl.map_to_background_tile->ignore_walls) ||
                attack_can_see_target(target, player)) {
                target_found = true;
                player_actions(player)->target = target;
            } else {
                /* Take that one out of the running and roll again. */
                for (int i = chosen; i < count - 1; i++) {
                    near_targets[i] = near_targets[i + 1];
                }
                count--;
            }
        }

        if (second_pass == false) {
            second_pass = true;
        }
    }

    if (gbl.map_to_background_tile != NULL) {
        gbl.map_to_background_tile->ignore_walls = false;
    }

    return target_found;
}

/* sub_421C1 */
bool attack_no_reachable_target(bool clear_target, int *range, Player *player)
{
    bool nothing_to_attack = true;

    if (range == NULL || player == NULL) {
        log_warn("attack: looking for a reachable target for nobody");
        return true;
    }

    if (attack_find_target(clear_target, 0, 0xff, player)) {
        Player *found = player_actions(player)->target;
        Point target_pos = combatmap_player_map_pos(found);

        if (target_can_reach_range(range, target_pos,
                                   combatmap_player_map_pos(player))) {
            nothing_to_attack = false;
        }
    }

    return nothing_to_attack;
}

/* sub_42159 */
void attack_load_missile_and_draw(int icon_id, Player *target, Player *attacker)
{
    character_load_missile_icons(icon_id + 13);

    character_draw_missile_attack(0x1e, 1, combatmap_player_map_pos(target),
                                  combatmap_player_map_pos(attacker));
}

/* ovr014.god_intervene */
bool attack_god_intervene(void)
{
    if (cheats.allow_gods_intervene == false) {
        return false;
    }

    character_print_message("The Gods intervene!");

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        if (player->combat_team == TEAM_ENEMY) {
            player->in_combat     = false;
            player->health_status = STATUS_DEAD;

            gbl.combat_map[combatmap_player_index(player)].size = 0;
        }

        character_clear_actions(player);
    }

    combatmap_redraw_area(8, 0xff, point_add(map_screen_top_left(),
                                             point_screen_center()));

    return true;
}

/* ---------------------------------------------- the affect table's attacks */

/* affecttab.c hands the handler whatever the affect's entry says it takes; these
 * two check what came in rather than casting it blind as the C# did. */
static Affect *as_affect(void *param, const char *who)
{
    if (param == NULL) {
        log_warn("%s: called with no affect record", who);
        return NULL;
    }

    return (Affect *)param;
}

/* The player_array entry an affect's data names, which for these three is who
 * is being held. */
static Player *held_player(const Affect *affect, const char *who)
{
    /* affect_data is a byte and player_array holds 256 entries, so the index is
     * always inside it; only the entry itself may be empty. */
    Player *player = gbl.player_array[affect->affect_data];

    if (player == NULL) {
        log_warn("%s: combatant %d is not in the fight", who,
                 affect->affect_data);
    }

    return player;
}

/* ovr014.engulfs, AFFECT_39. A cloaker that lands both its attacks wraps itself
 * round the target and holds them there. */
void attack_affect_engulfs(Effect add_remove, void *param, Player *attacker)
{
    Player *target;

    (void)add_remove;
    (void)param;

    if (attacker == NULL) {
        log_warn("engulfs: nobody to engulf with");
        return;
    }

    target = player_actions(attacker)->target;

    /* Both attacks have to have landed. */
    if (gbl.attack_hit_count[1] != 2) {
        return;
    }

    if (target == NULL) {
        /* The C# read target.in_combat straight after the hit count. */
        log_warn("engulfs: %s is engulfing nobody", attacker->name);
        return;
    }

    if (target->in_combat &&
        player_has_affect(target, AFFECT_CLEAR_MOVEMENT) == false &&
        player_has_affect(target, AFFECT_REDUCE) == false) {
        char text[64];

        snprintf(text, sizeof(text), "engulfs %s", target->name);
        character_display_status_string(true, 12, text, attacker);

        effect_add_affect(false, combatmap_player_index(target), 0,
                          AFFECT_CLEAR_MOVEMENT, target);

        affect_table_call(EFFECT_ADD, NULL, target, AFFECT_CLEAR_MOVEMENT);
        effect_add_affect(false, effect_roll_dice(4, 2), 0, AFFECT_REDUCE, target);
        effect_add_affect(true, combatmap_player_index(target), 0, AFFECT_8B,
                          attacker);
    }
}

/* ovr014.attack_or_kill, AFFECT_57. The beholder's rays: one of them each round,
 * picked by how far away the target is, and each only ever used once. */
void attack_affect_attack_or_kill(Effect add_remove, void *param, Player *attacker)
{
    int range = 0xff;
    u8  rays_used = 0;          /* attacksTired */
    int attacks_left = 4;

    (void)add_remove;
    (void)param;

    if (attacker == NULL) {
        log_warn("beholder: nobody to fire the rays");
        return;
    }

    player_actions(attacker)->target = NULL;
    attack_no_reachable_target(true, &range, attacker);

    do {
        Player *target = player_actions(attacker)->target;

        range = character_target_range(target, attacker);
        attacks_left--;

        if (target == NULL) {
            continue;
        }

        if (range == 2 && (rays_used & 1) == 0) {
            rays_used |= 1;

            character_display_status_string(true, 10, "fires a disintegrate ray",
                                           attacker);
            attack_load_missile_and_draw(5, target, attacker);

            if (effect_roll_saving_throw(0, SAVE_VERSE_BREATH_WEAPON, target) == false) {
                effect_kill_player("is disintergrated", STATUS_GONE, target);
            }

            attack_no_reachable_target(false, &range, attacker);
        } else if (range == 3 && (rays_used & 2) == 0) {
            rays_used |= 2;

            character_display_status_string(true, 10, "fires a stone to flesh ray",
                                           attacker);
            attack_load_missile_and_draw(10, target, attacker);

            if (effect_roll_saving_throw(0, SAVE_VERSE_PETRIFICATION, target) == false) {
                effect_kill_player("is Stoned", STATUS_STONED, target);
            }

            attack_no_reachable_target(false, &range, attacker);
        } else if (range == 4 && (rays_used & 4) == 0) {
            rays_used |= 4;

            character_display_status_string(true, 10, "fires a death ray",
                                           attacker);
            attack_load_missile_and_draw(5, target, attacker);

            if (effect_roll_saving_throw(0, SAVE_VERSE_POISON, target) == false) {
                effect_kill_player("is killed", STATUS_DEAD, target);
            }

            attack_no_reachable_target(false, &range, attacker);
        } else if (range == 5 && (rays_used & 8) == 0) {
            rays_used |= 8;

            character_display_status_string(true, 10, "wounds you", attacker);
            attack_load_missile_and_draw(5, target, attacker);

            effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL,
                                 effect_roll_dice_save(8, 2) + 1, target);
            attack_no_reachable_target(false, &range, attacker);
        } else if ((rays_used & 0x10) == 0) {
            /* Fear, slow and cause serious wounds, in that order, once each. */
            spellcast_resolve_spell(true, QUICK_FIGHT_TRUE, 0x54);
            rays_used |= 0x10;
        } else if ((rays_used & 0x20) == 0) {
            spellcast_resolve_spell(true, QUICK_FIGHT_TRUE, 0x37);
            rays_used |= 0x20;
        } else if ((rays_used & 0x40) == 0) {
            spellcast_resolve_spell(true, QUICK_FIGHT_TRUE, 0x15);
            rays_used |= 0x40;
        }
    } while (attacks_left > 0 && player_actions(attacker)->target != NULL);
}

/* ovr014.sub_425C6, AFFECT_8B. What being engulfed costs, every round: two
 * crushing attacks, until one of the two leaves the fight. */
void attack_affect_engulf_round(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "engulf round");

    if (affect == NULL || player == NULL) {
        return;
    }

    gbl.spell_target = held_player(affect, "engulf round");

    if (gbl.spell_target == NULL) {
        return;
    }

    if (add_remove == EFFECT_REMOVE || player->in_combat == false ||
        gbl.spell_target->in_combat == false) {
        effect_remove_affect(NULL, AFFECT_CLEAR_MOVEMENT, gbl.spell_target);
        effect_remove_affect(NULL, AFFECT_REDUCE, gbl.spell_target);

        if (add_remove == EFFECT_ADD) {
            /* Cleared so that taking the affect off does not come back here. */
            affect->call_affect_table = false;

            effect_remove_affect(affect, AFFECT_8B, player);
        }
    } else {
        player->attack1_attacks_left = 2;
        player->attack2_attacks_left = 0;
        player->attack1_dice_count   = 2;
        player->attack1_dice_size    = 8;

        attack_target(NULL, 1, gbl.spell_target, player);

        character_clear_actions(player);

        if (gbl.spell_target->in_combat == false) {
            effect_remove_affect(NULL, AFFECT_8B, player);
            effect_remove_affect(NULL, AFFECT_CLEAR_MOVEMENT, gbl.spell_target);
            effect_remove_affect(NULL, AFFECT_REDUCE, gbl.spell_target);
        }
    }
}

/* ovr014.AffectOwlbearHugRoundAttack, sub_426FC, AFFECT_90. The hug's own attack
 * each round, which is the same again but one blow rather than two. */
void attack_affect_owlbear_hug_round(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "owlbear hug");

    if (affect == NULL || player == NULL) {
        return;
    }

    gbl.spell_target = held_player(affect, "owlbear hug");

    if (gbl.spell_target == NULL) {
        return;
    }

    if (add_remove == EFFECT_REMOVE || player->in_combat == false ||
        gbl.spell_target->in_combat == false) {
        effect_remove_affect(NULL, AFFECT_CLEAR_MOVEMENT, gbl.spell_target);

        if (add_remove == EFFECT_ADD) {
            affect->call_affect_table = false;
            effect_remove_affect(affect, AFFECT_OWLBEAR_HUG_ROUND_ATTACK, player);
        }
    } else {
        player->attack1_attacks_left = 1;
        player->attack2_attacks_left = 0;
        player->attack1_dice_count   = 2;
        player->attack1_dice_size    = 8;

        attack_target(NULL, 2, gbl.spell_target, player);

        character_clear_actions(player);

        if (gbl.spell_target->in_combat == false) {
            effect_remove_affect(NULL, AFFECT_OWLBEAR_HUG_ROUND_ATTACK, player);
            effect_remove_affect(NULL, AFFECT_CLEAR_MOVEMENT, gbl.spell_target);
        }
    }
}

/* ovr014.AffectOwlbearHugAttackCheck, AFFECT_60. A good enough attack roll and
 * the owlbear has hold of them. */
void attack_affect_owlbear_hug_check(Effect add_remove, void *param, Player *player)
{
    char text[64];

    (void)add_remove;
    (void)param;

    if (player == NULL) {
        log_warn("owlbear hug: nobody to hug with");
        return;
    }

    if (gbl.attack_roll < 18) {
        return;
    }

    gbl.spell_target = player_actions(player)->target;

    if (gbl.spell_target == NULL) {
        /* The C# read gbl.spell_target.name here. */
        log_warn("owlbear hug: %s is hugging nobody", player->name);
        return;
    }

    snprintf(text, sizeof(text), "hugs %s", gbl.spell_target->name);
    character_display_status_string(true, 12, text, player);

    effect_add_affect(false, combatmap_player_index(gbl.spell_target), 0,
                      AFFECT_CLEAR_MOVEMENT, gbl.spell_target);
    affect_table_call(EFFECT_ADD, NULL, gbl.spell_target, AFFECT_CLEAR_MOVEMENT);

    effect_add_affect(true, combatmap_player_index(gbl.spell_target), 0,
                      AFFECT_OWLBEAR_HUG_ROUND_ATTACK, player);
}
