/* spelleffect.c - Ported from engine/ovr023.cs (the spell handlers; see
 * spelleffect.h).
 *
 * The handlers are in the order the C# had them, which is roughly spell id
 * order, and each keeps the C# name it had - so cleric_bless, is_affected2 and
 * sub_616CC sit next to SpellCureLight and SpellAnimateDead. Several ids share a
 * handler; the dispatch table at the bottom of the file is the map, and it is
 * ovr023.setup_spells written out.
 *
 * Most of them come down to one call to spellcast_do_casting_work with the
 * damage rolled and a line of text: everything else - the saving throw, the
 * affect, the announcement - is in the spell's row of spell_casting_table.
 */
#include <string.h>

#include "spelleffect.h"

#include "affect.h"
#include "affecttab.h"
#include "area.h"
#include "character.h"
#include "classcalc.h"
#include "combat.h"
#include "combatmap.h"
#include "effect.h"
#include "gbl.h"
#include "item.h"
#include "limits.h"
#include "log.h"
#include "partymenu.h"
#include "point.h"
#include "prompt.h"
#include "sound.h"
#include "spellcast.h"
#include "spelllist.h"
#include "spells.h"
#include "target.h"
#include "text.h"
#include "tile.h"

/* ------------------------------------------------------------------ helpers */

/* gbl.spellTargets[0], which most of the handlers reach for without looking at
 * the count first. The C# threw ArgumentOutOfRangeException on an empty list;
 * here it is logged and the handler does nothing. */
static Player *first_target(void)
{
    if (gbl.spell_target_count <= 0 || gbl.spell_targets[0] == NULL) {
        log_warn("spell 0x%x: it has nothing to work on", gbl.spell_id);
        return NULL;
    }

    return gbl.spell_targets[0];
}

/* gbl.BackGroundTiles[tile].move_cost, which the C# read without checking the
 * index. A tile off the end of the table counts as impassable, which is what the
 * wall rows hold anyway. */
static int tile_move_cost(int ground_tile)
{
    const BackgroundTile *tile = background_tile(ground_tile);

    return (tile != NULL) ? tile->move_cost : 0xff;
}

/* The row the spell being cast is being read out of. NULL, with a warning, for
 * an id the table has no row for; the C# indexed it. */
static const SpellEntry *current_spell(void)
{
    const SpellEntry *entry = spell_entry(gbl.spell_id);

    if (entry == NULL) {
        log_warn("spell effect: no spell 0x%x", gbl.spell_id);
    }

    return entry;
}

/* sub_5D676. The three lines every path traced in this file starts with.
 * CalculateDeltas puts current back to the attacker and zeroes the step count,
 * so one SteppingPath can be re-aimed as often as the caller likes. */
static void local_stepping_path_init(Point target, Point caster,
                                     SteppingPath *path)
{
    path->attacker = caster;
    path->target   = target;

    stepping_path_calculate_deltas(path);
}

/* sub_5D702. Walks the path to its end and collects everyone standing on it,
 * each combatant once. Returns the direction of the last step taken - which is
 * the no-move 8 when the path had no length at all, since Step sets direction
 * even on the call that reports the end.
 *
 * *count is the number of indices already in list and is updated. */
static int find_players_on_path(SteppingPath *path, int *list, int *count,
                                int max)
{
    int dir = 0;

    while (stepping_path_step(path) == true) {
        int player_index = combatmap_player_index_at(path->current.y,
                                                     path->current.x);

        if (player_index > 0) {
            bool seen = false;

            for (int i = 0; i < *count; i++) {
                if (list[i] == player_index) {
                    seen = true;
                    break;
                }
            }

            if (seen == false) {
                if (*count < max) {
                    list[(*count)++] = player_index;
                } else {
                    /* The C# list had no limit. One entry per combatant means
                     * this cannot be reached from a map of 0xff of them. */
                    log_warn("spell effect: more than %d combatants on the path",
                             max);
                }
            }
        }

        dir = path->direction;
    }

    return dir;
}

/* unk_16D22 and unk_16D32. Which way to shift the second and third lines of a
 * cone or a breath, indexed by the direction the first line came in on. */
static const Point UNK_16D22[8] = {
    { -1, 0 }, { 0, -1 }, { 0, -1 }, { 1, 0 },
    {  1, 0 }, { 0,  1 }, { 0,  1 }, { -1, 0 }
};

static const Point UNK_16D32[8] = {
    { 1,  0 }, { 1,  0 }, { 0,  1 }, { 0,  1 },
    { -1, 0 }, { -1, 0 }, { 0, -1 }, { 0, -1 }
};

/* sub_5D7CF. Everyone caught by a spell that covers an area: the line from the
 * caster to where it was aimed, and for a wider spell one or two more lines
 * beside it. The caster is never one of them.
 *
 * The first loop is dead: `while (!path.Step())` runs its body only while Step
 * reports no step made, so for any aim with length at all it never runs once, and
 * the position it works out is thrown away regardless. It is kept because it is
 * what the original did - and because the direction list it was meant to fill
 * stays all zeroes, which is what the second loop then walks.
 *
 * A path of no length - a spell aimed at the caster's own square - would loop in
 * that first loop until the direction list overflowed, so it is bounded here;
 * the C# would have thrown IndexOutOfRangeException. */
static void build_area_damage_targets(int max_range, int player_size,
                                      Point target_pos, Point caster_pos)
{
    int players_on_path[GBL_MAX_COMBATANT_COUNT];
    int player_count = 0;
    SteppingPath path;
    u8 directions[0x32];
    int index = 0;
    int count;
    int tmp_range;
    bool finished = false;
    Point tmp_pos;
    int last_dir;

    stepping_path_clear(&path);
    memset(directions, 0, sizeof(directions));

    local_stepping_path_init(target_pos, caster_pos, &path);

    while (stepping_path_step(&path) == false) {
        if (index >= (int)sizeof(directions)) {
            log_warn("spell effect: a spell aimed at its own caster's square");
            break;
        }

        directions[index] = path.direction;
        index++;
    }

    count = index - 1;

    index = 0;
    max_range *= 2;
    tmp_range = path.steps;

    tmp_pos = target_pos;

    while (tmp_range < max_range && finished == false) {
        if (tmp_pos.x < 0x31 && tmp_pos.x > 0 &&
            tmp_pos.y < 0x18 && tmp_pos.y > 0) {
            switch (directions[index]) {
            case 0:
            case 2:
            case 4:
            case 6:
                tmp_range += 2;
                break;

            case 1:
            case 3:
            case 5:
            case 7:
                tmp_range += 3;
                break;

            default:
                /* Direction 8, the no-move entry: the range never grows and the
                 * loop runs until the position walks off the map. */
                break;
            }

            tmp_pos = point_add(tmp_pos, map_direction_delta[directions[index]]);

            if (index == count) {
                index = 0;
            } else {
                index++;

                if (index >= (int)sizeof(directions)) {
                    /* count is -1 whenever the loop above did not run, so the
                     * index only ever climbs. The C# read off the end of the
                     * array; the walk is over as far as this port is concerned. */
                    finished = true;
                }
            }
        } else {
            finished = true;
        }
    }

    point_map_clamp(&target_pos);

    target_can_reach(&target_pos, caster_pos);

    local_stepping_path_init(target_pos, caster_pos, &path);
    last_dir = find_players_on_path(&path, players_on_path, &player_count,
                                    (int)COAB_ARRAY_LEN(players_on_path));

    if (player_size > 1 && last_dir >= 0 && last_dir < 8) {
        Point map_b = point_add(target_pos, UNK_16D32[last_dir]);

        point_map_clamp(&map_b);

        local_stepping_path_init(map_b, caster_pos, &path);
        find_players_on_path(&path, players_on_path, &player_count,
                             (int)COAB_ARRAY_LEN(players_on_path));

        if (player_size > 2) {
            Point map_a = point_add(target_pos, UNK_16D22[last_dir]);

            point_map_clamp(&map_a);

            local_stepping_path_init(map_a, caster_pos, &path);
            find_players_on_path(&path, players_on_path, &player_count,
                                 (int)COAB_ARRAY_LEN(players_on_path));
        }
    } else if (player_size > 1) {
        /* find_players_on_path hands back the no-move direction 8 for a path of
         * no length, which the C# would have indexed both shift tables with. */
        log_warn("spell effect: an area spell with no direction to spread in");
    }

    gbl_spell_targets_clear();

    for (int i = 0; i < player_count; i++) {
        Player *player = gbl.player_array[players_on_path[i]];

        if (player != gbl.selected_player) {
            gbl_spell_target_add(player);
        }
    }
}

/* sub_5DB24. A spell that is aimed at several combatants one after another and
 * whose whole effect is its affect: hold, charm monsters, faerie fire.
 *
 * firstTimeRound is never cleared in the C#, so the missile drawn between
 * targets never appears and the first-target exception below applies to every
 * target of a faerie fire or a charm monsters. Both are kept as they stand. */
static void multi_targeted_spell(const char *text, int save_bonus)
{
    const SpellEntry *entry = current_spell();
    const bool first_time_round = true;

    if (entry == NULL) {
        return;
    }

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        bool saved;
        DamageOnSave can_save_flag;

        if (target == NULL) {
            continue;
        }

        if (first_time_round == false) {
            sound_play(SOUND_2);
            character_load_missile_icons(0x12);

            character_draw_missile_attack(
                0x1e, 4, combatmap_player_map_pos(target),
                combatmap_player_map_pos(gbl.selected_player));
        }

        if ((gbl.spell_id == SPELL_FAERIE_FIRE ||
             gbl.spell_id == SPELL_CHARM_MONSTERS) &&
            first_time_round == true) {
            saved = true;
            can_save_flag = DAMAGE_ON_SAVE_ZERO;
        } else {
            saved = effect_roll_saving_throw(save_bonus, entry->save_verse,
                                             target);
            can_save_flag = entry->damage_on_save;
        }

        /* Anything but a plain humanoid shrugs a mind-affecting spell off -
         * except a dimension door, which is cast on its own caster. */
        if ((target->monster_type > MONSTER_TYPE_1 || target->field_DE > 1) &&
            gbl.spell_id != SPELL_DIMENSION_DOOR) {
            saved = true;
        }

        effect_apply_attack_spell_affect(
            text, saved, can_save_flag, false,
            character_spell_max_target_count(gbl.spell_id),
            spellcast_spell_affect_timeout(gbl.spell_id), entry->affect_id,
            target);
    }
}

/* sub_5DCA0. A spell cast over one whole side: everybody on the other team is
 * dropped from the target list first. A bless in a fight also skips anybody
 * within a square of an enemy, which is the "not while engaged" rule. */
static void cast_team_spell(const char *text, CombatTeam team)
{
    int kept = 0;

    gbl.byte_1D2C7 = true;

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        bool drop;

        if (target == NULL) {
            drop = true;
        } else if (target->combat_team != (int)team) {
            drop = true;
        } else if (gbl.spell_id == SPELL_BLESS &&
                   gbl.game_state == GAME_STATE_COMBAT) {
            static CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];

            drop = character_build_near_targets(
                       near_targets, (int)COAB_ARRAY_LEN(near_targets), 1,
                       target) > 0;
        } else {
            drop = false;
        }

        if (drop == false) {
            gbl.spell_targets[kept] = target;
            kept++;
        }
    }

    gbl.spell_target_count = kept;

    spellcast_do_casting_work(text, 0, 0, false, 0, gbl.spell_id);
}

/* sub_5F87B. A spell that cancels its own opposite before it is cast: a haste
 * takes a slow off first, and a slow takes a haste off. A target the cure worked
 * on is dropped from the list, so the spell itself is not cast on them - as is
 * every target on the other side, and every target past the caster's level. */
static void remove_compliment_spell_first(const char *text, CombatTeam team,
                                          Affects affect)
{
    int max_targets;
    int kept = 0;

    gbl.byte_1D2C7 = true;

    max_targets = character_spell_max_target_count(gbl.spell_id);

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        bool drop = true;

        if (target != NULL && target->combat_team == (int)team &&
            max_targets > 0) {
            max_targets -= 1;

            drop = effect_cure_affect(affect, target);
        }

        if (drop == false) {
            gbl.spell_targets[kept] = target;
            kept++;
        }
    }

    gbl.spell_target_count = kept;

    spellcast_do_casting_work(text, 0, 0, false, 0, gbl.spell_id);
}

/* The tail of both DoElecDamage overloads: the combatant standing where the bolt
 * has reached takes half damage on a save. Nothing happens where the square is
 * empty, or where it holds the combatant the bolt has just come from. */
static void do_actual_elec_damage(int player_index, SaveVerseType bonus_type,
                                  int damage, int target_index)
{
    if (target_index > 0 && target_index != player_index) {
        Player *player = gbl.player_array[target_index];

        if (player == NULL) {
            return;
        }

        gbl.damage_flags = DAMAGE_MAGIC | DAMAGE_ELECTRICITY;

        effect_damage_person(effect_roll_saving_throw(0, bonus_type, player),
                             DAMAGE_ON_SAVE_HALF, damage, player);

        /* Reloaded after every hit because damage_person may have drawn a body
         * falling over, which uses the icon buffer for something else. */
        character_load_missile_icons(0x13);

        gbl.damage_flags = 0;
    }
}

/* The DoElecDamage overload that only wants the damage done. */
static void do_elec_damage_at(int player_index, SaveVerseType bonus_type,
                              int damage, Point pos)
{
    int target_index = combatmap_player_index_at(pos.y, pos.x);

    do_actual_elec_damage(player_index, bonus_type, damage, target_index);
}

/* sub_5F986. The same, and it also answers whether the bolt has to turn round:
 * indoors, a bolt that runs into a wall bounces back the way it came. It only
 * bounces once, which is what arg_0 coming in true means. */
static bool do_elec_damage(bool bounced, int player_index,
                           SaveVerseType bonus_type, int damage, Point pos)
{
    int ground_tile;
    int target_index;

    combatmap_at_map_xy(&ground_tile, &target_index, pos);

    if (ground_tile > 0 && tile_move_cost(ground_tile) == 0xff &&
        gbl.area_ptr != NULL && gbl.area_ptr->in_dungeon == 1 &&
        bounced == false) {
        bounced = true;
    } else {
        bounced = false;
    }

    do_actual_elec_damage(player_index, bonus_type, damage, target_index);

    return bounced;
}

/* sub_5FA44. A lightning bolt travelling on past where it was aimed: it runs in
 * a straight line from the caster through the target square until it has used up
 * its length, hitting whoever it passes over, and turns back on itself when it
 * meets a wall indoors.
 *
 * var_3A - the combatant index the last square held - is read before the first
 * step of a zero-length aim assigns it, which the C# flagged "Simeon" and left
 * uninitialised. Zero is what a C# local starts at, so that is what it gets. */
static void sub_5FA44(u8 arg_0, SaveVerseType bonus_type, int damage, u8 arg_6)
{
    int var_3A = 0;
    bool var_36 = false;
    int var_39;
    int ground_tile;
    Point player_pos;
    int var_3D = 0;
    int var_35 = 1;
    u8 var_38 = arg_0;

    character_load_missile_icons(0x13);

    combatmap_at_map_xy(&ground_tile, &var_39, gbl.target_pos);

    player_pos = combatmap_player_map_pos(gbl.selected_player);

    if (point_eq(player_pos, gbl.target_pos) == false) {
        int var_3C = arg_6 * 2;

        gbl.byte_1D2C7 = true;

        while (var_3C > 0) {
            SteppingPath path_a;

            stepping_path_clear(&path_a);

            path_a.attacker = gbl.target_pos;
            path_a.target = point_add(
                gbl.target_pos,
                point_mul(point_sub(gbl.target_pos, player_pos),
                          var_35 * var_3C));

            stepping_path_calculate_deltas(&path_a);

            do {
                Point tmppos = path_a.current;

                if (point_eq(path_a.attacker, path_a.target) == false) {
                    bool stepping;

                    do {
                        stepping = stepping_path_step(&path_a);

                        combatmap_at_map_xy(&ground_tile, &var_3A,
                                            path_a.current);

                        if (tile_move_cost(ground_tile) == 1) {
                            var_36 = false;
                        }
                    } while (stepping == true &&
                             (var_3A <= 0 || var_3A == var_39) &&
                             ground_tile != 0 &&
                             tile_move_cost(ground_tile) <= 1 &&
                             path_a.steps < var_3C);
                }

                if (ground_tile == 0) {
                    var_3C = 0;
                }

                character_draw_missile_attack(0x32, 4, path_a.current, tmppos);

                var_36 = do_elec_damage(var_36, var_39, bonus_type, damage,
                                        path_a.current);
                var_39 = var_3A;

                if (var_36 == true) {
                    SteppingPath path_b;

                    gbl.target_pos = path_a.current;

                    stepping_path_clear(&path_b);

                    path_b.attacker = gbl.target_pos;
                    path_b.target   = player_pos;

                    stepping_path_calculate_deltas(&path_b);

                    while (stepping_path_step(&path_b) == true) {
                        /* empty: all this wants is the step count */
                    }

                    /* A bolt that bounces back into its caster's face gets four
                     * more squares of travel to do it in. */
                    if (var_38 != 0 && path_b.steps <= 8) {
                        path_a.steps = (u8)(path_a.steps + 8);
                    }

                    var_35 = -var_35;
                    var_38 = 0;
                    var_39 = 0;
                }

                var_3D = (u8)(path_a.steps - var_3D);

                if (var_3D < var_3C) {
                    var_3C -= var_3D;
                } else {
                    var_3C = 0;
                }

                var_3D = path_a.steps;
            } while (var_36 == false && var_3C != 0);
        }

        gbl.byte_1D2C7 = false;
    }
}

/* --------------------------------------------------------------- the spells */

/* is_Blessed */
static void cleric_bless(void)
{
    if (gbl.selected_player == NULL) {
        return;
    }

    cast_team_spell("is Blessed", (CombatTeam)gbl.selected_player->combat_team);
}

/* is_Cursed */
static void cleric_curse(void)
{
    if (gbl.selected_player == NULL) {
        return;
    }

    cast_team_spell("is Cursed", player_opposite_team(gbl.selected_player));
}

/* sub_5DDBC */
static void spell_cure_light(void)
{
    if (gbl.spell_target_count > 0 &&
        effect_heal_player(0, effect_roll_dice(8, 1), gbl.spell_targets[0]) ==
            true) {
        character_describe_healing(gbl.spell_targets[0]);
    }
}

/* sub_5DDF8 */
static void spell_cause_light(void)
{
    spellcast_do_casting_work("", DAMAGE_MAGIC, effect_roll_dice_save(8, 1),
                              false, 0, gbl.spell_id);
}

/* The handler every spell that does nothing but announce itself shares. */
static void is_affected(void)
{
    spellcast_do_casting_work("is affected", 0, 0, false, 0, gbl.spell_id);
}

/* is_protected */
static void spell_protection_from_x(void)
{
    spellcast_do_casting_work("is protected", 0, 0, false, 0, gbl.spell_id);
}

/* is_cold_resistant */
static void spell_resist_cold(void)
{
    spellcast_do_casting_work("is cold-resistant", 0, 0, false, 0, gbl.spell_id);
}

/* sub_5DEE1. One point of damage per level of the caster. */
static void spell_burning_hands(void)
{
    spellcast_do_casting_work("", DAMAGE_MAGIC | DAMAGE_FIRE,
                              character_spell_max_target_count(gbl.spell_id),
                              false, 0, gbl.spell_id);
}

/* is_charmed. The affect's data byte carries the caster's team in bit 7 and the
 * caster's level in the rest, so a charmed monster knows whose side it is on. */
static void spell_charm(void)
{
    Player *target = first_target();

    if (target == NULL || gbl.selected_player == NULL) {
        return;
    }

    if (target->monster_type > MONSTER_TYPE_1 || target->field_DE > 1) {
        character_display_status_string(true, 10, "is unaffected", target);
    } else {
        Affect *affect;

        spellcast_do_casting_work(
            "is charmed", 0, 0, true,
            (u8)((gbl.selected_player->combat_team << 7) +
                 character_spell_max_target_count(gbl.spell_id)),
            gbl.spell_id);

        affect = affect_list_find(&target->affects, AFFECT_CHARM_PERSON);

        if (affect != NULL) {
            /* The charm's own affect handed to the shield entry of the jump
             * table, which is what the C# asked for. */
            affect_table_call(EFFECT_ADD, affect, target, AFFECT_SHIELD);
        }
    }
}

/* is_stronger. Strength 18 at first level and a percentile with it from the
 * third, climbing to 22 for a caster of ten. */
static void spell_enlarge(void)
{
    Player *target = first_target();
    int new_str = 18;
    int new_str100 = 0;
    int encoded_strength;

    if (target == NULL) {
        return;
    }

    switch (character_spell_max_target_count(gbl.spell_id)) {
    case 1:
        new_str100 = 0;
        break;

    case 2:
        new_str100 = 1;
        break;

    case 3:
        new_str100 = 51;
        break;

    case 4:
        new_str100 = 76;
        break;

    case 5:
        new_str100 = 91;
        break;

    case 6:
        new_str100 = 100;
        break;

    case 7:
        new_str = 19;
        break;

    case 8:
        new_str = 20;
        break;

    case 9:
        new_str = 21;
        break;

    case 10:
    case 11:
        new_str = 22;
        break;

    default:
        break;
    }

    if (effect_try_encode_strength(&encoded_strength, new_str100, new_str,
                                   target) == true) {
        character_display_status_string(true, 10, "is stronger", target);

        effect_add_affect(true, encoded_strength,
                          spellcast_spell_affect_timeout(gbl.spell_id),
                          AFFECT_ENLARGE, target);

        effect_calc_stat_bonuses(STAT_STR, target);
    } else {
        character_display_status_string(true, 10, "is unaffected", target);
    }
}

/* has_been_reduced. The other half of enlarge: it only ever takes an enlarge
 * off, and only if the target fails its save. */
static void spell_reduce(void)
{
    Player *target = (gbl.spell_target_count > 0) ? gbl.spell_targets[0] : NULL;

    if (target != NULL && gbl.spell_target_count > 0 &&
        effect_roll_saving_throw(0, SAVE_VERSE_SPELL, target) == false &&
        player_has_affect(target, AFFECT_ENLARGE) == true) {
        effect_remove_affect(NULL, AFFECT_ENLARGE, target);
        effect_calc_stat_bonuses(STAT_STR, target);
        character_display_status_string(true, 10, "has been reduced", target);
    }
}

/* is_friendly. The caster's own charisma is what changes, so the bonuses are
 * recalculated for them rather than for the target. */
static void spell_friends(void)
{
    spellcast_do_casting_work("is friendly", 0, 0, true, effect_roll_dice(4, 2),
                              gbl.spell_id);

    if (gbl.selected_player != NULL) {
        effect_calc_stat_bonuses(STAT_CHA, gbl.selected_player);
    }
}

/* sub_5E221. One missile per two levels, each 1d4+1. */
static void spell_magic_missile(void)
{
    int missiles = character_spell_max_target_count(gbl.spell_id) + 1;

    spellcast_do_casting_work(
        "", DAMAGE_MAGIC,
        (missiles / 2) + effect_roll_dice_save(4, missiles / 2), false, 0,
        gbl.spell_id);
}

/* is_shielded */
static void spell_shield(void)
{
    spellcast_do_casting_work("is shielded", 0, 0, false, 0, gbl.spell_id);
}

/* sub_5E2B2. The damage flags say acid and cold, where the spell is electrical;
 * that is what the original had. */
static void spell_shocking_grasp(void)
{
    spellcast_do_casting_work(
        "", DAMAGE_ACID | DAMAGE_COLD,
        effect_roll_dice_save(8, 1) +
            character_spell_max_target_count(gbl.spell_id),
        false, 0, gbl.spell_id);
}

/* The hit dice a sleep spell has to spend to put this target down. */
static int calc_sleep_cost(const Player *target)
{
    int cost;

    switch (target->hit_dice) {
    case 0:
    case 1:
        cost = 1;
        break;

    case 2:
        cost = 2;
        break;

    case 3:
        cost = 4;
        break;

    case 4:
        cost = 6;
        break;

    case 5:
        cost = (target->race == RACE_MONSTER) ? 10 : 20;
        break;

    default:
        cost = 20;
        break;
    }

    return cost;
}

/* falls_asleep. 4d4 hit dice of sleep, spent on the targets in the order they
 * were aimed at until it runs out. */
static void spell_sleep(void)
{
    int total_spell_power = effect_roll_dice(4, 4);
    int kept = 0;

    gbl.byte_1D2C7 = true;

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        bool drop = true;

        if (target != NULL) {
            int spell_cost = calc_sleep_cost(target);

            if (target->health_status != STATUS_ANIMATED &&
                player_has_affect(target, AFFECT_SLEEP) == false &&
                total_spell_power >= spell_cost) {
                total_spell_power -= spell_cost;
                drop = false;
            }
        }

        if (drop == false) {
            gbl.spell_targets[kept] = target;
            kept++;
        }
    }

    gbl.spell_target_count = kept;

    spellcast_do_casting_work("falls asleep", 0, 0, false, 0, gbl.spell_id);
}

/* is_held. Hold person and hold monsters: the fewer targets, the worse their
 * save. The C# threw NotSupportedException for none or more than four, which is
 * more than the targeting hands out; here it is logged and no save bonus is
 * applied at all. */
static void spell_hold_x(void)
{
    int save_bonus;

    if (gbl.spell_target_count == 1) {
        save_bonus = (gbl.spell_id == SPELL_HOLD_PERSON_CL) ? -2 : -3;
    } else if (gbl.spell_target_count == 2) {
        save_bonus = -1;
    } else if (gbl.spell_target_count == 3 || gbl.spell_target_count == 4) {
        save_bonus = 0;
    } else {
        log_warn("spell 0x%x: held %d targets, which it has no save bonus for",
                 gbl.spell_id, gbl.spell_target_count);
        save_bonus = 0;
    }

    multi_targeted_spell("is held", save_bonus);
}

/* is_fire_resistant */
static void spell_fire_resistant(void)
{
    spellcast_do_casting_work("is fire resistant", 0, 0, false, 0, gbl.spell_id);
}

/* is_silenced */
static void spell_silence_15_radius(void)
{
    spellcast_do_casting_work("is silenced", 0, 0, false, 0, gbl.spell_id);
}

/* Slow poison: the target stops losing hit points to the poison for a while, and
 * one point back on their feet if the poison had taken them all. */
static void is_affected2(void)
{
    Player *player = first_target();

    if (player == NULL) {
        return;
    }

    if (player->health_status == STATUS_ANIMATED) {
        gbl_spell_targets_clear();
    } else if (player_has_affect(player, AFFECT_POISONED) == true) {
        if (player->hit_point_current == 0) {
            player->hit_point_current = 1;
        }

        spellcast_do_casting_work("is affected", 0, 0, true, 0xff,
                                  gbl.spell_id);
        affect_table_call(EFFECT_REMOVE, NULL, player, AFFECT_4E);
        effect_add_affect(true, 0xff, 10, AFFECT_POISON_DAMAGE, player);
    }
}

/* is_charmed2. Snake charm holds as many hit points' worth of snakes as the
 * caster has hit points of their own. */
static void spell_snake_charm(void)
{
    int total_spell_power;

    if (gbl.selected_player == NULL) {
        return;
    }

    total_spell_power = gbl.selected_player->hit_point_current;

    gbl_spell_targets_clear();

    for (int i = 0; i < gbl.team_count; i++) {
        Player *target = gbl.team_list[i];

        if (target != NULL && target->monster_type == MONSTER_SNAKE &&
            total_spell_power >= target->hit_point_current) {
            total_spell_power -= target->hit_point_current;
            gbl_spell_target_add(target);
        }
    }

    spellcast_do_casting_work("is charmed", 0, 0, false, 0, gbl.spell_id);
}

/* sub_5E681 */
static void spell_spiritual_hammer(void)
{
    Player *target;

    spellcast_do_casting_work("", 0, 0, true, 0, gbl.spell_id);

    target = first_target();

    if (target != NULL) {
        affect_table_call(EFFECT_ADD, NULL, target, AFFECT_SPIRITUAL_HAMMER);
    }
}

/* is_invisible, shared by invisibility, its 10' radius version and spell 0x3f. */
static void is_invisible(void)
{
    spellcast_do_casting_work("is invisible", 0, 0, false, 0, gbl.spell_id);
}

static void spell_knock(void)
{
    spellcast_do_casting_work("Knock-Knock", 0, 0, false, 0, gbl.spell_id);
}

/* is_duplicated. The affect's data byte holds the number of images in the top
 * nibble and the caster's level in the bottom one. */
static void spell_mirror_image(void)
{
    int data = effect_roll_dice(4, 1) << 4;

    data += character_spell_max_target_count(gbl.spell_id);

    spellcast_do_casting_work("is duplicated", 0, 0, false, data, gbl.spell_id);
}

/* is_weakened */
static void spell_ray_of_enfeeblement(void)
{
    spellcast_do_casting_work("is weakened", 0, 0, false, 0, gbl.spell_id);
}

#define STINKING_CLOUD_MAX_TARGETS 4

/* A stinking cloud covers the target square and the four around it; a cloudkill
 * covers nine. Both remember the tile each cell was standing on so it can be put
 * back when the cloud lifts - and both have to look through the clouds already
 * on the map for the real tile, since one cloud may be laid over another. */
static void spell_stinking_cloud(void)
{
    int targets[STINKING_CLOUD_MAX_TARGETS];
    GasCloud *cloud;
    u8 caster_lvl;
    int count = 0;

    gbl.byte_1D2C7 = true;

    if (gbl.selected_player == NULL) {
        return;
    }

    caster_lvl = (u8)character_spell_max_target_count(gbl.spell_id);

    for (int i = 0; i < gbl.stinking_cloud_count; i++) {
        if (gbl.stinking_cloud[i].player == gbl.selected_player) {
            count++;
        }
    }

    if (gbl.stinking_cloud_count >= GBL_GAS_CLOUD_MAX) {
        /* The C# list had no limit. Thirty-two clouds of one kind at once is far
         * more than a fight ever holds. */
        log_warn("stinking cloud: %d of them are already on the map",
                 GBL_GAS_CLOUD_MAX);
        return;
    }

    cloud = &gbl.stinking_cloud[gbl.stinking_cloud_count];
    gbl.stinking_cloud_count++;

    gas_cloud_init(cloud, gbl.selected_player, count, gbl.target_pos);

    effect_add_affect(true, (u8)(caster_lvl + (count << 4)), caster_lvl,
                      AFFECT_IN_STINKING_CLOUD, gbl.selected_player);

    for (int cell = 0; cell < STINKING_CLOUD_MAX_TARGETS; cell++) {
        u8 dir = small_cloud_directions[cell];
        Point pos = point_add(gbl.target_pos, map_direction_delta[dir]);
        int ground_tile;

        combatmap_at_map_xy(&ground_tile, &targets[cell], pos);

        cloud->present[cell] = (ground_tile > 0 &&
                                tile_move_cost(ground_tile) < 0xff);

        if (ground_tile == TILE_STINKING_CLOUD) {
            /* Another stinking cloud is already here: find what it is lying on.
             * The last match wins, as the C# loop did. */
            for (int i = 0; i < gbl.stinking_cloud_count; i++) {
                GasCloud *other = &gbl.stinking_cloud[i];

                if (other == cloud) {
                    continue;
                }

                for (int j = 0; j < STINKING_CLOUD_MAX_TARGETS; j++) {
                    if (other->present[j] == true &&
                        point_eq(pos,
                                 point_add(other->target_pos,
                                           map_direction_delta
                                               [small_cloud_directions[j]])) &&
                        other->ground_tile[j] != TILE_STINKING_CLOUD) {
                        ground_tile = other->ground_tile[j];
                    }
                }
            }
        } else if (ground_tile == TILE_DOWN_PLAYER) {
            /* A body is lying here; the cloud remembers the floor under it. */
            for (int i = gbl.downed_player_count - 1; i >= 0; i--) {
                if (point_eq(gbl.downed_players[i].map, pos)) {
                    ground_tile =
                        gbl.downed_players[i].original_background_tile;
                    break;
                }
            }
        }

        cloud->ground_tile[cell] = ground_tile;

        if (cloud->present[cell] == true) {
            ground_tile_map_set(gbl.map_to_background_tile, pos,
                                TILE_STINKING_CLOUD);
        }
    }

    character_display_status_string(false, 10, "Creates a noxious cloud",
                                    gbl.selected_player);

    combatmap_redraw_area(8, 0xff, gbl.target_pos);
    text_game_delay();
    character_clear_text_area();

    /* A combatant standing under two of the cells would otherwise be poisoned
     * twice, so the duplicates are struck out first. */
    for (int a = 0; a < STINKING_CLOUD_MAX_TARGETS; a++) {
        for (int b = 0; b < STINKING_CLOUD_MAX_TARGETS; b++) {
            if (targets[b] == targets[a] && a != b) {
                targets[a] = 0;
            }
        }
    }

    for (int cell = 0; cell < STINKING_CLOUD_MAX_TARGETS; cell++) {
        if (targets[cell] > 0) {
            effect_in_poison_cloud(1, gbl.player_array[targets[cell]]);
        }
    }
}

/* sub_5EC5B. Strength: a fighter gains 1d8 points, a cleric or thief 1d6, a
 * magic user 1d4, and a fighter-type may go past 18 on the percentile scale.
 *
 * The encoded strength worked out above is then thrown away and replaced with
 * "the number of points gained, plus 100", which is what the affect actually
 * carries. That is the original's own doing and the reason a strength spell
 * shows as an increase rather than as a fixed score. */
static void spell_strength(void)
{
    int str_increase = 0;
    Player *target = first_target();
    int str;
    int str_100 = 0;
    int encoded_str;

    if (target == NULL) {
        return;
    }

    if (target->class_level[SKILL_MAGIC_USER] > 0 ||
        target->class_level_old[SKILL_MAGIC_USER] > target->multiclass_level) {
        str_increase = effect_roll_dice(4, 1);
    }

    if (target->class_level[SKILL_CLERIC] > 0 ||
        target->class_level_old[SKILL_CLERIC] > target->multiclass_level ||
        target->class_level[SKILL_THIEF] > 0 ||
        target->class_level_old[SKILL_THIEF] > target->multiclass_level) {
        str_increase = effect_roll_dice(6, 1);
    }

    if (target->class_level[SKILL_FIGHTER] > 0 ||
        target->class_level_old[SKILL_FIGHTER] > target->multiclass_level) {
        str_increase = effect_roll_dice(8, 1);
    }

    str = target->stats.value[PSTAT_STR].full + str_increase;

    if (str > 18) {
        if (target->class_level[SKILL_FIGHTER] > 0 ||
            target->class_level_old[SKILL_FIGHTER] > target->multiclass_level ||
            target->class_level[SKILL_PALADIN] > 0 ||
            target->class_level_old[SKILL_PALADIN] > target->multiclass_level ||
            target->class_level[SKILL_RANGER] > 0 ||
            target->class_level_old[SKILL_RANGER] > target->multiclass_level) {
            str_100 = target->stats.value[PSTAT_STR00].cur + ((str - 18) * 10);

            if (str_100 > 100) {
                str_100 = 100;
            }

            str = 18;
        } else {
            str = 18;
        }
    }

    if (effect_try_encode_strength(&encoded_str, str_100, str, target) == true) {
        encoded_str = str_increase + 100;

        effect_add_affect(true, encoded_str,
                          spellcast_spell_affect_timeout(gbl.spell_id),
                          AFFECT_STRENGTH, target);
        effect_calc_stat_bonuses(STAT_STR, target);
    }
}

/* is_animated. Raises as many bodies as the caster has levels and puts them on
 * the caster's side, berserk and under nobody's control. */
static void spell_animate_dead(void)
{
    int remaining;

    gbl.byte_1D2C7 = true;

    if (gbl.selected_player == NULL) {
        return;
    }

    remaining = character_spell_max_target_count(gbl.spell_id);

    gbl_spell_targets_clear();

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player != NULL && player->health_status == STATUS_DEAD &&
            player->monster_type == 0) {
            if (combatmap_place_combatant(true, combatmap_player_map_pos(player),
                                          player) == true) {
                u8 data = (u8)((player->combat_team << 4) +
                               character_spell_max_target_count(gbl.spell_id));

                player->combat_team   = gbl.selected_player->combat_team;
                player->quick_fight   = QUICK_FIGHT_TRUE;
                player->field_E9      = 1;
                player->attack_level  = 0;
                player->base_movement = 6;

                spell_list_clear(&player->spell_list);

                if (player->control_morale >= CONTROL_NPC_BASE) {
                    player->control_morale = CONTROL_NPC_BERZERK;
                } else {
                    player->control_morale = CONTROL_PC_BERZERK;
                }

                player->monster_type = MONSTER_ANIMATED_DEAD;

                if (gbl.game_state == GAME_STATE_COMBAT) {
                    player_actions(player)->target = NULL;
                }

                remaining--;

                if (effect_combat_heal(player->hit_point_max, player) == true) {
                    effect_apply_attack_spell_affect("is animated", false, 0,
                                                     true, data, 0,
                                                     AFFECT_ANIMATE_DEAD,
                                                     player);
                    player->health_status = STATUS_ANIMATED;
                }
            }
        }

        if (remaining <= 0) {
            break;
        }
    }
}

/* can_see */
static void spell_cure_blindness(void)
{
    Player *target = first_target();

    if (target != NULL && effect_cure_affect(AFFECT_BLINDED, target) == true) {
        character_magic_attack_display("can see", true, target);
    }
}

/* is_blind */
static void spell_cause_blindness(void)
{
    spellcast_do_casting_work("is blind", 0, 0, false, 0, gbl.spell_id);
}

/* sub_5F037. The three diseases cure disease takes off, and the affects that
 * hang off each of them. */
static bool sub_5F037(void)
{
    Player *target = first_target();
    bool cured = false;

    if (target == NULL) {
        return false;
    }

    gbl.cure_spell = true;

    if (effect_cure_affect(AFFECT_CAUSE_DISEASE_1, target) == true) {
        cured = true;
    }

    if (effect_cure_affect(AFFECT_WEAKEN, target) == true) {
        cured = true;

        effect_remove_affect(NULL, AFFECT_CAUSE_DISEASE_2, target);
        effect_remove_affect(NULL, AFFECT_HELPLESS, target);
    }

    if (effect_cure_affect(AFFECT_HOT_FIRE_SHIELD, target) == true) {
        cured = true;
        effect_remove_affect(NULL, AFFECT_39, target);
    }

    gbl.cure_spell = false;

    return cured;
}

/* sub_5F0DC */
static void spell_cure_disease(void)
{
    sub_5F037();
}

/* is_diseased */
static void spell_cause_disease(void)
{
    spellcast_do_casting_work("is diseased", 0, 0, true, 0, gbl.spell_id);
}

/* sub_5F126. Whether a dispel beats the magic that laid a cloud: even odds
 * against a caster of the dispeller's own level, five points better for each
 * level below it and two points worse for each level above. */
static bool sub_5F126(const Player *caster, int target_count)
{
    int mu_lvl = player_skill_level(caster, SKILL_MAGIC_USER);
    int roll;

    if (target_count > mu_lvl) {
        roll = ((target_count - mu_lvl) * 5) + 50;
    } else if (target_count < mu_lvl) {
        roll = 50 - ((mu_lvl - target_count) * 2);
    } else {
        roll = 50;
    }

    return effect_roll_dice(100, 1) <= roll;
}

/* The affect that matches this one by contents. The C# held object references
 * and removed by identity; the list here is an array that closes up over every
 * removal, so a saved pointer would name somebody else's affect by the time it
 * was used - the entry has to be looked up again. */
static Affect *affect_list_find_same(AffectList *list, const Affect *wanted)
{
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].type == wanted->type &&
            list->items[i].minutes == wanted->minutes &&
            list->items[i].affect_data == wanted->affect_data) {
            return &list->items[i];
        }
    }

    return NULL;
}

/* SpellDispelMagic walks nine cells with the four-entry small_cloud_directions
 * table whichever kind of cloud it found, so against a cloudkill it reads five
 * bytes past the end of it. cloud_directions follows it in the DOS data segment -
 * seg600:27D9 then seg600:27DD - so those five bytes are cloud_directions[0..4].
 * This is that image of the two tables laid end to end. */
static const u8 DISPEL_CLOUD_DIRECTIONS[9] = { 8, 2, 3, 4, 8, 0, 1, 2, 3 };

/* is_affected3. Two spells in one: every affect on the target that can be
 * dispelled is rolled for, and then any cloud lying on or beside the square the
 * dispel was aimed at is rolled for as well. An affect carrying 0xff - one that
 * came off an item, or a permanent one - is not open to being dispelled. */
static void spell_dispel_magic(void)
{
    int max_target_count = character_spell_max_target_count(gbl.spell_id);
    int y_pos = 0;
    int x_pos = 0;

    gbl.byte_1D2C7 = true;

    if (gbl.spell_target_count > 0 && gbl.spell_targets[0] != NULL) {
        Player *target = gbl.spell_targets[0];
        Affect remove_list[AFFECT_LIST_MAX];
        int remove_count = 0;
        bool is_affected = false;

        for (int i = 0; i < target->affects.count; i++) {
            const Affect *affect = &target->affects.items[i];

            if (affect->affect_data < 0xff) {
                int affect_lvl = affect->affect_data & 0x0f;
                int roll_needed;

                if (max_target_count > affect_lvl) {
                    roll_needed = 50 + ((max_target_count - affect_lvl) * 5);
                } else if (max_target_count < affect_lvl) {
                    roll_needed = 50 - ((affect_lvl - max_target_count) * 2);
                } else {
                    roll_needed = 50;
                }

                if (effect_roll_dice(100, 1) <= roll_needed) {
                    remove_list[remove_count] = *affect;
                    remove_count++;
                    is_affected = true;
                }
            }
        }

        for (int i = 0; i < remove_count; i++) {
            Affect *affect = affect_list_find_same(&target->affects,
                                                   &remove_list[i]);

            if (affect != NULL) {
                effect_remove_affect(affect, (Affects)affect->type, target);
            }
        }

        if (is_affected == true) {
            character_magic_attack_display("is affected", true, target);
        }
    }

    /* The nine squares are walked by changing one coordinate at a time, so the
     * order is the square itself, north, north-west, west, south-west, south,
     * south-east... which is what the original's chain of assignments comes to.
     * Directions 7 and 8 land on squares already looked at. */
    for (int dir = 0; dir <= 8; dir++) {
        int ground_tile;
        int dummy_byte;

        switch (dir) {
        case 0:
            x_pos = gbl.target_pos.x;
            y_pos = gbl.target_pos.y;
            break;

        case 1:
            y_pos = gbl.target_pos.y - 1;
            break;

        case 2:
            x_pos = gbl.target_pos.x - 1;
            break;

        case 3:
            y_pos = gbl.target_pos.y;
            break;

        case 4:
            y_pos = gbl.target_pos.y + 1;
            break;

        case 5:
            x_pos = gbl.target_pos.x;
            break;

        case 6:
            x_pos = gbl.target_pos.x - 1;
            break;

        case 7:
            y_pos = gbl.target_pos.y;
            break;

        default:
            y_pos = gbl.target_pos.y - 1;
            break;
        }

        combatmap_at_map_yx(&ground_tile, &dummy_byte, y_pos, x_pos);

        if (ground_tile == TILE_CLOUD_KILL || ground_tile == TILE_STINKING_CLOUD) {
            bool is_cloud_kill = (ground_tile == TILE_CLOUD_KILL);
            int cell_count = is_cloud_kill ? 9 : 4;
            GasCloud *clouds = is_cloud_kill ? gbl.cloud_kill_cloud
                                             : gbl.stinking_cloud;
            int cloud_count = is_cloud_kill ? gbl.cloud_kill_count
                                            : gbl.stinking_cloud_count;
            Point map_pos = point_make(x_pos, y_pos);

            for (int i = 0; i < cloud_count; i++) {
                GasCloud *cloud = &clouds[i];

                for (int cell = 0; cell < cell_count; cell++) {
                    u8 cell_dir = DISPEL_CLOUD_DIRECTIONS[cell];

                    if (point_eq(map_pos,
                                 point_add(cloud->target_pos,
                                           map_direction_delta[cell_dir])) ==
                            false ||
                        cloud->field_1D != false) {
                        continue;
                    }

                    /* A cloud whose caster has left the fight is treated as
                     * having shrugged the dispel off; the C# read through the
                     * null and threw. */
                    if (cloud->player != NULL &&
                        sub_5F126(cloud->player, max_target_count) == true) {
                        Affect *found = NULL;

                        for (int a = 0; a < cloud->player->affects.count; a++) {
                            Affect *affect = &cloud->player->affects.items[a];

                            /* The C# tested `affect` here rather than the loop
                             * variable, and `affect` was still null - it threw a
                             * NullReferenceException the first time a dispel was
                             * aimed at a cloud. Reading the loop variable is what
                             * was meant and is what a C build has to do. */
                            if (((affect->type == AFFECT_IN_CLOUD_KILL &&
                                  is_cloud_kill == true) ||
                                 (affect->type == AFFECT_IN_STINKING_CLOUD &&
                                  is_cloud_kill == false)) &&
                                (affect->affect_data >> 4) == cloud->field_1C) {
                                found = affect;
                                break;
                            }
                        }

                        if (found != NULL) {
                            effect_remove_affect(found,
                                                 is_cloud_kill
                                                     ? AFFECT_IN_CLOUD_KILL
                                                     : AFFECT_IN_STINKING_CLOUD,
                                                 cloud->player);
                        }
                    } else {
                        /* The cloud shrugged the dispel off and is safe from
                         * every later attempt this cast makes. */
                        cloud->field_1D = true;
                    }
                }
            }
        }
    }
}

/* is_praying. The affect's data byte carries the caster's team in the top nibble
 * and their level in the bottom one - a prayer helps one side and hinders the
 * other, so it has to remember which side cast it. */
static void spell_prayer(void)
{
    u8 data;

    if (gbl.selected_player == NULL) {
        return;
    }

    data = (u8)((gbl.selected_player->combat_team * 16) +
                character_spell_max_target_count(gbl.spell_id));

    spellcast_do_casting_work("is praying", 0, 0, false, data, gbl.spell_id);
}

/* uncurse. A bestowed curse first; failing that, the first cursed item in the
 * pack is un-readied and its affect taken back off. */
static void spell_remove_curse(void)
{
    Player *target = first_target();

    if (target == NULL) {
        return;
    }

    if (effect_cure_affect(AFFECT_BESTOW_CURSE, target) == true) {
        character_magic_attack_display("is un-cursed", true, target);
    } else {
        Item *item = NULL;

        for (int i = 0; i < target->item_count; i++) {
            if (target->items[i].cursed == true) {
                item = &target->items[i];
                break;
            }
        }

        if (item != NULL) {
            item->readied = false;

            /* Only an affect above 0x7f is one of the item affects the jump table
             * knows how to take off again. */
            if (item->affect_3 > 0x7f) {
                gbl.apply_item_affect = true;

                affect_table_call(EFFECT_REMOVE, item, target,
                                  (Affects)item->affect_3);

                effect_calc_stat_bonuses(STAT_STR, target);
                effect_calc_stat_bonuses(STAT_INT, target);
                effect_calc_stat_bonuses(STAT_WIS, target);
                effect_calc_stat_bonuses(STAT_DEX, target);
                effect_calc_stat_bonuses(STAT_CON, target);
                effect_calc_stat_bonuses(STAT_CHA, target);
            }

            character_magic_attack_display("has an item un-cursed", true, target);
        }
    }
}

static void curse(void)
{
    spellcast_do_casting_work("has been cursed!", 0, 0, false, 0, gbl.spell_id);
}

static void spell_blinking(void)
{
    spellcast_do_casting_work("is blinking", 0, 0, false, 0, gbl.spell_id);
}

/* Every combatant is of interest: the `sc => true` the C# passed in. */
static bool filter_any(const Player *player, void *ctx)
{
    (void)player;
    (void)ctx;

    return true;
}

/* sub_5F782. Fireball, and the wand's version of it as spell 0x40 - which rolls
 * its own dice count rather than taking the caster's level.
 *
 * Out in the wilderness the burst finds its own targets, since there is no combat
 * map to have aimed it with. */
static void sub_5F782(void)
{
    int dice_count;

    gbl.byte_1D2C7 = true;

    if (gbl.spell_id == SPELL_40) {
        dice_count = (effect_roll_dice(3, 1) * 2) + 1;
    } else {
        dice_count = character_spell_max_target_count(gbl.spell_id);
    }

    if (gbl.area_ptr != NULL && gbl.area_ptr->in_dungeon == 0) {
        static SortedCombatant scl[GBL_MAX_COMBATANT_COUNT];
        int found = target_sorted_combatants(scl, (int)COAB_ARRAY_LEN(scl), 1, 2,
                                            gbl.target_pos, filter_any, NULL);

        gbl_spell_targets_clear();

        for (int i = 0; i < found; i++) {
            gbl_spell_target_add(scl[i].player);
        }
    }

    combatmap_redraw_area(8, 0, gbl.target_pos);

    spellcast_do_casting_work("", DAMAGE_MAGIC | DAMAGE_FIRE,
                              effect_roll_dice_save(6, dice_count), false, 0,
                              gbl.spell_id);
}

static void cast_haste(void)
{
    if (gbl.selected_player == NULL) {
        return;
    }

    remove_compliment_spell_first("is Hasted",
                                 (CombatTeam)gbl.selected_player->combat_team,
                                 AFFECT_SLOW);
}

/* sub_5FCD9 */
static void spell_lightning_bolt(void)
{
    int damage = effect_roll_dice(6,
                                 character_spell_max_target_count(gbl.spell_id));

    do_elec_damage_at(0, SAVE_VERSE_SPELL, damage, gbl.target_pos);
    sub_5FA44(1, SAVE_VERSE_SPELL, damage, 7);
}

/* sub_5FD2E */
static void spell_slow(void)
{
    if (gbl.selected_player == NULL) {
        return;
    }

    remove_compliment_spell_first("is Slowed",
                                 player_opposite_team(gbl.selected_player),
                                 AFFECT_HASTE);
}

/* cast_restore. Gives back one of the levels a wight or a vampire drained: the
 * hit points that went with it, and the level itself in whichever class is the
 * cheapest one to have lost.
 *
 * The class chosen starts at 30, which the C# would then have used to index an
 * eight-entry array, and the search allows a class level of 13, which is one past
 * the end of the experience table. Both are out of bounds and would have thrown;
 * here each is refused with a warning, so a character whose only class is at
 * level 13 gets their hit points back but not the level. */
static void spell_restoration(void)
{
    int restore_skill = 30;
    Player *player = first_target();

    if (player == NULL) {
        return;
    }

    if (player->lost_lvls > 0) {
        u8 restored_hp = (u8)(player->lost_hp / player->lost_lvls);
        int max_lvl = 13;
        i32 max_exp = 10000000;

        player->hit_point_max += restored_hp;
        player->hit_point_current += restored_hp;
        player->hit_point_rolled += restored_hp;
        player->lost_hp -= restored_hp;
        player->lost_lvls -= 1;

        for (int skill = 0; skill <= 7; skill++) {
            int lvl = player->class_level[skill];

            if (lvl > 0 && lvl <= max_lvl) {
                if (lvl >= PARTYMENU_EXP_LEVELS) {
                    log_warn("restoration: class %d is at level %d, which the "
                             "experience table stops short of", skill, lvl);
                    continue;
                }

                if (partymenu_exp_table[skill][lvl] > 0 &&
                    partymenu_exp_table[skill][lvl] < max_exp &&
                    limits_race_stat_level_restricted((ClassId)skill, player) ==
                        false) {
                    max_lvl = lvl;
                    restore_skill = skill;
                    max_exp = partymenu_exp_table[skill][lvl];
                }
            }
        }

        if (restore_skill >= 0 && restore_skill < SKILL_COUNT) {
            player->class_level[restore_skill]++;
        } else {
            log_warn("restoration: no class of %s is one a level can go back on",
                     player->name);
        }

        if (player->exp < max_exp) {
            player->exp = max_exp;
        }

        classcalc_class_bonuses(player);
        character_display_status_string(true, 10, "is restored", player);
    }
}

/* The wand of speed: a slow is cancelled instead of a haste being laid on. */
static void cast_speed(void)
{
    Player *target = first_target();

    if (target != NULL && effect_cure_affect(AFFECT_SLOW, target) == false) {
        spellcast_do_casting_work("is Speedy", 0, 0, false, 0, gbl.spell_id);
    }
}

/* sub_5FF6D */
static void spell_cure_serious_wounds(void)
{
    if (gbl.spell_target_count > 0 &&
        effect_heal_player(0, effect_roll_dice(8, 2) + 1,
                           gbl.spell_targets[0]) == true) {
        character_describe_healing(gbl.spell_targets[0]);
    }
}

/* A gauntlets-of-ogre-power sort of strength: a flat 21, for four to seven
 * hours. The affect goes on whether the score would move or not. */
static void cast_strength(void)
{
    Player *target = first_target();
    int encoded_strength = 0;

    if (target == NULL) {
        return;
    }

    if (effect_try_encode_strength(&encoded_strength, 0, 0x15, target) == true) {
        character_display_status_string(true, 10, "is stronger", target);
    }

    effect_add_affect(true, encoded_strength,
                      (u16)((effect_roll_dice(4, 1) * 10) + 0x28),
                      AFFECT_STRENGTH_SPELL, target);
    effect_calc_stat_bonuses(STAT_STR, target);
}

/* A wand of lightning: the bolt does 21 to 26 where it lands but a flat 20 for
 * the rest of its length, and it does not bounce. */
static void sub_6003C(void)
{
    do_elec_damage_at(0, SAVE_VERSE_SPELL, effect_roll_dice(6, 1) + 20,
                      gbl.target_pos);
    sub_5FA44(0, SAVE_VERSE_SPELL, 20, 3);
}

static void cast_paralyzed(void)
{
    spellcast_do_casting_work("is paralyzed", 0, 0, false, 0, gbl.spell_id);
}

static void cast_heal(void)
{
    Player *target = first_target();

    if (target != NULL &&
        effect_heal_player(0, effect_roll_dice(4, 2) + 2, target) == true) {
        character_magic_attack_display("is Healed", true, target);
    }
}

static void cast_invisible(void)
{
    spellcast_do_casting_work("is invisible", 0, 0, false, 0, gbl.spell_id);
}

static void dam2d4plus2(void)
{
    spellcast_do_casting_work("", DAMAGE_MAGIC, effect_roll_dice_save(4, 2) + 2,
                              false, 0, gbl.spell_id);
}

/* sub_60185 */
static void spell_cause_serious_wounds(void)
{
    spellcast_do_casting_work("", DAMAGE_MAGIC, effect_roll_dice_save(8, 2) + 1,
                              false, 0, gbl.spell_id);
}

/* Removes one entry from gbl.spellTargets: the C# List.Remove(target). */
static void spell_targets_remove(const Player *player)
{
    for (int i = 0; i < gbl.spell_target_count; i++) {
        if (gbl.spell_targets[i] == player) {
            for (int j = i + 1; j < gbl.spell_target_count; j++) {
                gbl.spell_targets[j - 1] = gbl.spell_targets[j];
            }

            gbl.spell_target_count--;
            return;
        }
    }
}

/* cure_poison. Takes the poison and everything hanging off it away, and stands
 * the character back up if the poison had put them down. */
static void spell_neutralize_poison(void)
{
    Player *target = first_target();

    if (target == NULL) {
        return;
    }

    if (target->health_status == STATUS_ANIMATED) {
        spell_targets_remove(target);
    } else if (player_has_affect(target, AFFECT_POISONED) == true) {
        if (target->hit_point_current == 0) {
            target->hit_point_current = 1;
        }

        gbl.cure_spell = true;

        effect_remove_affect(NULL, AFFECT_POISONED, target);
        effect_remove_affect(NULL, AFFECT_SLOW_POISON, target);
        effect_remove_affect(NULL, AFFECT_POISON_DAMAGE, target);

        gbl.cure_spell = false;

        character_display_status_string(true, 10, "is unpoisoned", target);

        target->in_combat = true;
        target->health_status = STATUS_OKEY;
    } else {
        character_display_status_string(true, 10, "is unaffected", target);
    }
}

/* sub_602D0. The poison spell is applied to the caster, not to the target: the
 * affect is what does the poisoning when the caster next lands a blow. The
 * target is only asked whether its magic resistance stops the spell. */
static void spell_poison(void)
{
    Player *target;

    spellcast_do_casting_work("", DAMAGE_MAGIC, 0, false, 0, gbl.spell_id);

    if (gbl.selected_player == NULL) {
        return;
    }

    target = player_actions(gbl.selected_player)->target;

    gbl.current_affect = AFFECT_POISON_PLUS_0;

    if (target != NULL) {
        effect_check_affects(target, CHECK_TYPE_MAGIC_RESISTANCE);
    } else {
        /* The C# read through the null target here and threw. */
        log_warn("poison: the caster has nothing to poison");
    }

    if (gbl.current_affect == AFFECT_POISON_PLUS_0) {
        affect_table_call(EFFECT_ADD, NULL, gbl.selected_player,
                          AFFECT_POISON_PLUS_0);
    }
}

/* cast_flattern. Anything of six hit dice or more treads the snakes into the
 * ground instead of being wrapped up by them. */
static void spell_sticks_to_snakes(void)
{
    Player *target = first_target();

    if (target == NULL) {
        return;
    }

    if (target->hit_dice < 6) {
        Affect *affect;

        spellcast_do_casting_work("", DAMAGE_MAGIC, 0, false,
                                  character_spell_max_target_count(gbl.spell_id),
                                  gbl.spell_id);

        affect = affect_list_find(&target->affects, AFFECT_STICKS_TO_SNAKES);

        if (affect != NULL) {
            affect_table_call(EFFECT_ADD, affect, target,
                              AFFECT_STICKS_TO_SNAKES);
        }
    } else {
        character_display_status_string(true, 10, "smashes them flat", target);
    }
}

/* sub_603F0 */
static void spell_cure_critical_wounds(void)
{
    if (gbl.spell_target_count > 0 &&
        effect_heal_player(0, effect_roll_dice(8, 3) + 3,
                           gbl.spell_targets[0]) == true) {
        character_describe_healing(gbl.spell_targets[0]);
    }
}

/* sub_60431 */
static void spell_cause_critical_wounds(void)
{
    spellcast_do_casting_work("", DAMAGE_MAGIC, effect_roll_dice_save(8, 3) + 3,
                              false, 0, gbl.spell_id);
}

/* is_affected4. The caster is protected as well as the target being struck. */
static void spell_dispel_evil(void)
{
    effect_apply_attack_spell_affect(
        "", false, 0, false, 0,
        spellcast_spell_affect_timeout(SPELL_DISPEL_EVIL), AFFECT_DISPEL_EVIL,
        gbl.selected_player);

    spellcast_do_casting_work("is affected", 0, 0, false, 0, gbl.spell_id);
}

/* sub_604DA */
static void spell_flame_strike(void)
{
    spellcast_do_casting_work("", DAMAGE_MAGIC | DAMAGE_FIRE,
                              effect_roll_dice_save(8, 6), false, 0,
                              gbl.spell_id);
}

/* cast_raise. Back on their feet with one hit point and one point of constitution
 * the poorer for it. An elf cannot be raised, and neither can anybody whose
 * constitution has already been spent. */
static void spell_raise_dead(void)
{
    Player *player = first_target();

    if (player == NULL) {
        return;
    }

    if ((player->health_status == STATUS_DEAD ||
         player->health_status == STATUS_ANIMATED) &&
        player->stats.value[PSTAT_CON].cur > 0 && player->race != RACE_ELF) {
        gbl.cure_spell = true;

        effect_remove_affect(NULL, AFFECT_ANIMATE_DEAD, player);
        effect_remove_affect(NULL, AFFECT_POISONED, player);

        gbl.cure_spell = false;

        player->health_status = STATUS_OKEY;
        player->in_combat = true;
        player->stats.value[PSTAT_CON].cur--;

        effect_calc_stat_bonuses(STAT_CON, player);
        player->hit_point_current = 1;

        character_display_status_string(true, 10, "is raised", player);
    }
}

/* cast_slay. Death on a failed save; 2d8+1 on a made one. Damage type 0x40 is
 * what the magic resistance check reads to know a death spell is being resisted:
 * a resistance that stops it zeroes gbl.damage. */
static void spell_slay_living(void)
{
    Player *target = first_target();

    if (target == NULL) {
        return;
    }

    gbl.damage_flags = DAMAGE_UNKNOWN_40;
    gbl.damage = 67;

    effect_check_affects(target, CHECK_TYPE_MAGIC_RESISTANCE);

    if (gbl.damage != 0) {
        if (effect_roll_saving_throw(0, SAVE_VERSE_SPELL, target) == false) {
            effect_kill_player("is slain", STATUS_DEAD, target);
        } else {
            gbl.damage_flags = DAMAGE_MAGIC;

            effect_damage_person(false, 0, effect_roll_dice_save(8, 2) + 1,
                                 target);
        }
    } else {
        character_display_status_string(true, 10, "is unaffected", target);
    }
}

/* cast_entangle. Undergrowth only: nothing in a dungeon to be entangled by.
 *
 * The duration is asked for by affect id rather than by spell id - 0x88 is
 * affect_entangle, and the spell table has no such row - so the entangling always
 * lasts no time at all. That is the original's own bug and is left as it is. */
static void spell_entangle(void)
{
    if (gbl.area_ptr == NULL || gbl.area_ptr->in_dungeon != 0) {
        return;
    }

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        bool saved;

        if (target == NULL) {
            continue;
        }

        saved = effect_roll_saving_throw(0, SAVE_VERSE_SPELL, target);

        effect_apply_attack_spell_affect(
            "is entangled", saved, DAMAGE_ON_SAVE_ZERO, false, 0,
            spellcast_spell_affect_timeout(0x88), AFFECT_ENTANGLE, target);
    }
}

/* cast_highlisht */
static void spell_faerie_fire(void)
{
    multi_targeted_spell("is highlighted", 0);
}

/* cast_invisible2 */
static void spell_invis_to_animals(void)
{
    spellcast_do_casting_work("is invisible", 0, 0, false, 0, gbl.spell_id);
}

/* cast_charmed. The charm's own affect is handed to the jump table afterwards so
 * that each charmed monster changes sides straight away. */
static void spell_charm_monsters(void)
{
    multi_targeted_spell("is charmed", 0);

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        Affect *affect;

        if (target == NULL) {
            continue;
        }

        affect = affect_list_find(&target->affects, AFFECT_CHARM_PERSON);

        if (affect != NULL) {
            affect_table_call(EFFECT_ADD, affect, target, AFFECT_CHARM_PERSON);
        }
    }
}

/* cast_confuse. 2d8 targets at most, and the confusion is carried by the second
 * cause-disease affect - the two share a slot in the affect table. */
static void spell_confusion(void)
{
    int target_count = effect_roll_dice(8, 2);

    if (gbl.spell_target_count > target_count) {
        gbl.spell_target_count = target_count;
    }

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        bool saved;

        if (target == NULL) {
            continue;
        }

        saved = effect_roll_saving_throw(0, SAVE_VERSE_SPELL, target);

        effect_apply_attack_spell_affect(
            "is confused", saved, DAMAGE_ON_SAVE_ZERO, false, 0,
            spellcast_spell_affect_timeout(SPELL_CONFUSION),
            AFFECT_CAUSE_DISEASE_2, target);
    }
}

/* cast_teleport. The caster steps out of whatever has hold of them and reappears
 * where the spell was aimed. Only a caster who can move at all - one carrying
 * clear_movement - shakes a grapple off on the way out. */
static void spell_dimension_door(void)
{
    Player *player = gbl.selected_player;

    if (player == NULL) {
        return;
    }

    if (affect_list_find(&player->affects, AFFECT_CLEAR_MOVEMENT) != NULL) {
        static SortedCombatant scl[GBL_MAX_COMBATANT_COUNT];
        int found = target_sorted_combatants(scl, (int)COAB_ARRAY_LEN(scl), 1, 1,
                                             combatmap_player_map_pos(player),
                                             filter_any, NULL);

        for (int i = 0; i < found; i++) {
            Player *player_b = scl[i].player;
            Affect *affect;

            if (player_b == NULL) {
                continue;
            }

            affect = affect_list_find(&player_b->affects,
                                      AFFECT_OWLBEAR_HUG_ROUND_ATTACK);

            if (affect == NULL) {
                affect = affect_list_find(&player_b->affects, AFFECT_8B);
            }

            if (affect != NULL &&
                gbl.player_array[affect->affect_data] == player) {
                effect_remove_affect(NULL, AFFECT_OWLBEAR_HUG_ROUND_ATTACK,
                                     player_b);
                effect_remove_affect(NULL, AFFECT_8B, player_b);
            }
        }
    }

    combatmap_redraw_player_background(combatmap_player_index(player));

    combatmap_place_combatant(false, gbl.target_pos, player);

    combatmap_redraw_area(8, 0, combatmap_player_map_pos(player));

    character_display_status_string(true, 10, "teleports", player);
}

/* cast_terror. Everyone in the cone runs, and runs on their own initiative from
 * then on - a fleeing monster is taken off the player's hands. */
static void spell_fear(void)
{
    Player *caster = gbl.selected_player;

    if (caster == NULL) {
        return;
    }

    build_area_damage_targets(6, 3, gbl.target_pos,
                              combatmap_player_map_pos(caster));

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        bool saves;

        if (target == NULL) {
            continue;
        }

        saves = effect_roll_saving_throw(0, SAVE_VERSE_SPELL, target);

        if (saves == false) {
            Action *action;

            effect_apply_attack_spell_affect(
                "runs in terror", saves, DAMAGE_ON_SAVE_ZERO, true, 0,
                spellcast_spell_affect_timeout(SPELL_FEAR), AFFECT_FEAR, target);

            action = player_actions(target);
            action->fleeing = true;
            target->quick_fight = QUICK_FIGHT_TRUE;

            if (target->control_morale < CONTROL_NPC_BASE) {
                target->control_morale = CONTROL_PC_BERZERK;
            }

            action->target = NULL;
        } else {
            character_display_status_string(true, 10, "is unaffected", target);
        }
    }
}

/* cast_protection. A fire shield is either hot or cold and the caster has to say
 * which; both ward off lightning as well. There is no way out of the question
 * except answering it or agreeing to waste the spell. */
static void spell_fire_protection(void)
{
    bool done = false;

    if (gbl.selected_player == NULL) {
        return;
    }

    do {
        char input_key;

        if (gbl.selected_player->quick_fight == QUICK_FIGHT_TRUE) {
            input_key = (effect_roll_dice(10, 1) > 5) ? 'H' : 'C';
        } else {
            input_key = prompt_display_input_simple(false, 0,
                                                    GBL_DEFAULT_MENU_COLORS,
                                                    "Hot Cold", "flame type: ");
        }

        if (input_key == 'H') {
            effect_apply_attack_spell_affect(
                "is protected", false, 0, false, 0,
                spellcast_spell_affect_timeout(SPELL_FIRE_SHIELD),
                AFFECT_HOT_FIRE_SHIELD, gbl.selected_player);
            effect_apply_attack_spell_affect(
                "", false, 0, false, 0,
                spellcast_spell_affect_timeout(SPELL_FIRE_SHIELD),
                AFFECT_PROTECT_ELEC, gbl.selected_player);
            done = true;
        } else if (input_key == 'C') {
            effect_apply_attack_spell_affect(
                "", false, 0, false, 0,
                spellcast_spell_affect_timeout(SPELL_FIRE_SHIELD),
                AFFECT_COLD_FIRE_SHIELD, gbl.selected_player);
            effect_apply_attack_spell_affect(
                "", false, 0, false, 0,
                spellcast_spell_affect_timeout(SPELL_FIRE_SHIELD),
                AFFECT_PROTECT_ELEC, gbl.selected_player);
            done = true;
        } else {
            input_key = prompt_display_input_simple(false, 0,
                                                    GBL_DEFAULT_MENU_COLORS,
                                                    "Yes No", "Abort spell? ");

            if (input_key == 'Y') {
                done = true;
            }
        }
    } while (done == false);
}

/* spell_slow. A made save still costs the target their speed, so the spell always
 * does something; and the affect that landed is handed to the jump table to take
 * effect at once. */
static void spell_fumble(void)
{
    Player *target = first_target();

    if (target == NULL) {
        return;
    }

    gbl.damage_flags = DAMAGE_UNKNOWN_40;

    if (effect_roll_saving_throw(0, SAVE_VERSE_SPELL, target) == false) {
        effect_apply_attack_spell_affect(
            "is clumsy", false, 0, false, 0,
            spellcast_spell_affect_timeout(SPELL_FUMBLE), AFFECT_FUMBLING,
            target);

        if (player_has_affect(target, AFFECT_FUMBLING) == true) {
            affect_table_call(EFFECT_ADD, NULL, target, AFFECT_FUMBLING);
        }
    } else {
        effect_apply_attack_spell_affect(
            "is slowed", false, 0, false, 0,
            spellcast_spell_affect_timeout(SPELL_FUMBLE), AFFECT_SLOW, target);

        if (player_has_affect(target, AFFECT_SLOW) == true) {
            affect_table_call(EFFECT_ADD, NULL, target, AFFECT_SLOW);
        }
    }

    spellcast_do_casting_work("is clumsy", 0, 0, true, 0, gbl.spell_id);
}

/* sub_60F0B. The damage counts as acid, where the spell is cold; that is what
 * the original had, and it is why a ring of fire resistance does nothing about an
 * ice storm. */
static void spell_ice_storm(void)
{
    spellcast_do_casting_work("", DAMAGE_ACID, effect_roll_dice_save(10, 3),
                              false, 0, gbl.spell_id);
}

/* sub_60F4E */
static void spell_minor_globe_of_invulnerability(void)
{
    spellcast_do_casting_work("is protected", 0, 0, false, 0, gbl.spell_id);
}

#define CLOUD_KILL_MAX_TARGETS 9

/* spell_poisonous_cloud. Nine cells rather than the stinking cloud's five, and
 * the tile it remembers may have to be dug out of a stinking cloud, another
 * cloudkill or a body lying underneath.
 *
 * The two statements after the loop repeat its last two with the index left at 9:
 * ground_tile is whatever the ninth cell found and present[9] is false, so the
 * only effect is that cell 9 of the ten-cell array - which nothing reads - is
 * written. Both are kept. */
static void spell_cloud_kill(void)
{
    int targets[CLOUD_KILL_MAX_TARGETS];
    GasCloud *cloud;
    u8 caster_lvl;
    u8 dir = 0;
    int ground_tile = 0;
    int cell;
    int count = 0;

    gbl.byte_1D2C7 = true;

    if (gbl.selected_player == NULL) {
        return;
    }

    caster_lvl = (u8)character_spell_max_target_count(gbl.spell_id);

    for (int i = 0; i < gbl.cloud_kill_count; i++) {
        if (gbl.cloud_kill_cloud[i].player == gbl.selected_player) {
            count++;
        }
    }

    if (gbl.cloud_kill_count >= GBL_GAS_CLOUD_MAX) {
        /* The C# list had no limit. */
        log_warn("cloud kill: %d of them are already on the map",
                 GBL_GAS_CLOUD_MAX);
        return;
    }

    cloud = &gbl.cloud_kill_cloud[gbl.cloud_kill_count];
    gbl.cloud_kill_count++;

    gas_cloud_init(cloud, gbl.selected_player, count, gbl.target_pos);

    effect_add_affect(true, (u8)(caster_lvl + (count << 4)), caster_lvl,
                      AFFECT_IN_CLOUD_KILL, gbl.selected_player);

    for (cell = 0; cell < CLOUD_KILL_MAX_TARGETS; cell++) {
        Point pos;

        dir = cloud_directions[cell];
        pos = point_add(gbl.target_pos, map_direction_delta[dir]);

        combatmap_at_map_xy(&ground_tile, &targets[cell], pos);

        cloud->present[cell] = (ground_tile > 0 &&
                                tile_move_cost(ground_tile) < 0xff);

        if (ground_tile == TILE_STINKING_CLOUD) {
            bool found = false;

            for (int i = 0; i < gbl.stinking_cloud_count && found == false; i++) {
                GasCloud *other = &gbl.stinking_cloud[i];

                for (int j = 0; j < SMALL_CLOUD_DIRECTION_COUNT; j++) {
                    if (other->present[j] == true &&
                        point_eq(point_add(other->target_pos,
                                           map_direction_delta
                                               [small_cloud_directions[j]]),
                                 pos) &&
                        other->ground_tile[j] != TILE_STINKING_CLOUD &&
                        other->ground_tile[j] != TILE_CLOUD_KILL) {
                        ground_tile = other->ground_tile[j];
                        found = true;
                    }
                }
            }
        } else if (ground_tile == TILE_CLOUD_KILL) {
            bool found = false;

            for (int i = 0; i < gbl.cloud_kill_count && found == false; i++) {
                GasCloud *other = &gbl.cloud_kill_cloud[i];

                if (other == cloud) {
                    continue;
                }

                for (int j = 0; j < CLOUD_KILL_MAX_TARGETS; j++) {
                    if (other->present[j] == true &&
                        point_eq(point_add(other->target_pos,
                                           map_direction_delta
                                               [cloud_directions[j]]),
                                 pos) &&
                        other->ground_tile[j] != TILE_STINKING_CLOUD &&
                        other->ground_tile[j] != TILE_CLOUD_KILL) {
                        ground_tile = other->ground_tile[j];
                        found = true;
                    }
                }
            }
        } else if (ground_tile == TILE_DOWN_PLAYER) {
            for (int i = gbl.downed_player_count - 1; i >= 0; i--) {
                if (point_eq(gbl.downed_players[i].map, pos)) {
                    ground_tile =
                        gbl.downed_players[i].original_background_tile;
                    break;
                }
            }
        }

        cloud->ground_tile[cell] = ground_tile;

        if (cloud->present[cell] == true) {
            ground_tile_map_set(gbl.map_to_background_tile, pos,
                                TILE_CLOUD_KILL);
        }
    }

    /* The repeat of the last two statements, with cell == 9. */
    cloud->ground_tile[cell] = ground_tile;

    if (cloud->present[cell] == true) {
        ground_tile_map_set(gbl.map_to_background_tile,
                            point_add(gbl.target_pos,
                                      map_direction_delta[dir]),
                            TILE_CLOUD_KILL);
    }

    character_display_status_string(false, 10, "Creates a poisonous cloud",
                                    gbl.selected_player);

    combatmap_redraw_area(8, 0xff, gbl.target_pos);
    text_game_delay();
    character_clear_text_area();

    for (int idx = 0; idx < CLOUD_KILL_MAX_TARGETS; idx++) {
        if (targets[idx] > 0) {
            effect_in_poison_cloud(1, gbl.player_array[targets[idx]]);
        }
    }
}

/* sub_61550. A cone half as long as the caster's level, two squares wide, doing
 * a level plus 1d4 per level. */
static void spell_cone_of_cold(void)
{
    Player *player = gbl.selected_player;
    int target_count = character_spell_max_target_count(gbl.spell_id);
    int max_range = (target_count + 1) / 2;

    if (player == NULL) {
        return;
    }

    if (max_range < 1) {
        max_range = 1;
    }

    build_area_damage_targets(max_range, 2, gbl.target_pos,
                              combatmap_player_map_pos(player));

    spellcast_do_casting_work("", DAMAGE_ACID,
                              target_count +
                                  effect_roll_dice_save(4, target_count),
                              false, 0, gbl.spell_id);
}

/* sub_615F2. A cleric saves a point better against this and a magic user four
 * points worse; the change is put back afterwards. */
static void spell_feeblemind(void)
{
    Player *target = first_target();
    int old_bonus;

    if (target == NULL) {
        return;
    }

    old_bonus = target->save_verse[SAVE_VERSE_SPELL];

    if (target->cls == CLASS_CLERIC) {
        target->save_verse[SAVE_VERSE_SPELL] -= 1;
    } else if (target->cls == CLASS_MAGIC_USER) {
        target->save_verse[SAVE_VERSE_SPELL] += 4;
    } else {
        target->save_verse[SAVE_VERSE_SPELL] += 2;
    }

    gbl.damage_flags = 0;

    spellcast_do_casting_work("", 0, 0, false, 0, gbl.spell_id);

    if (player_has_affect(target, AFFECT_FEEBLEMIND) == true) {
        affect_table_call(EFFECT_ADD, NULL, target, AFFECT_FEEBLEMIND);
    }

    target->save_verse[SAVE_VERSE_SPELL] = old_bonus;
}

/* The three monster spells 0x5f, 0x60 and 0x61: whatever their row of the table
 * says happens, with nothing said about it. */
static void sub_616CC(void)
{
    spellcast_do_casting_work("", 0, 0, false, 0, gbl.spell_id);
}

/* Spell 0x62, a breath weapon in all but name: 6d6 down a single line, and a
 * plant takes it all whatever its protections say. */
static void sub_61727(void)
{
    Player *attacker = gbl.selected_player;
    const SpellEntry *entry = spell_entry(SPELL_62);

    if (attacker == NULL || entry == NULL) {
        return;
    }

    build_area_damage_targets(3, 1, gbl.target_pos,
                              combatmap_player_map_pos(attacker));

    for (int i = 0; i < gbl.spell_target_count; i++) {
        Player *target = gbl.spell_targets[i];
        bool change_damage;

        if (target == NULL) {
            continue;
        }

        change_damage = (target->monster_type != MONSTER_PLANT);

        effect_damage_person(change_damage, entry->damage_on_save,
                             effect_roll_dice_save(6, 6), target);
    }
}

static void cast_heal2(void)
{
    Player *target = first_target();

    if (target != NULL &&
        effect_heal_player(0, effect_roll_dice(4, 2) + 2, target) == true) {
        character_magic_attack_display("is Healed", true, target);
    }
}

/* ------------------------------------------- the affect-table entries, 8 of 8 */

/* Math.Sign. */
static int sign_of(int value)
{
    return (value > 0) ? 1 : ((value < 0) ? -1 : 0);
}

/* spell_stone. A basilisk's gaze: the monster aims itself with the targeting for
 * spell 0x41 and then turns whoever it looked at to stone. A gazer carrying
 * affect 0x7f can have its own gaze turned back on it by a mirror - name part
 * 0x76 - that the target has readied. */
void spelleffect_affect_paralizing_gaze(Effect add_remove, void *param,
                                        Player *player)
{
    Action *action;

    (void)add_remove;
    (void)param;

    if (player == NULL || gbl.spell_cast_function == NULL) {
        log_warn("paralizing gaze: no %s",
                 player == NULL ? "gazer" : "targeting function");
        return;
    }

    action = player_actions(player);
    action->target = NULL;

    gbl.byte_1DA70 = gbl.spell_cast_function(QUICK_FIGHT_TRUE, SPELL_41);

    /* The targeting is what puts a target back, having just been made to forget
     * the one it had. */
    if (action->target != NULL) {
        gbl.spell_target = action->target;

        character_display_status_string(false, 10, "gazes...", player);
        character_load_missile_icons(0x12);

        character_draw_missile_attack(
            0x2d, 4, combatmap_player_map_pos(gbl.spell_target),
            combatmap_player_map_pos(player));

        if (player_has_affect(player, AFFECT_7F) == true) {
            Item *item = NULL;

            for (int i = 0; i < gbl.spell_target->item_count; i++) {
                Item *it = &gbl.spell_target->items[i];

                if (it->readied == true &&
                    (it->namenum1 == 0x76 || it->namenum2 == 0x76 ||
                     it->namenum3 == 0x76)) {
                    item = it;
                    break;
                }
            }

            if (item != NULL) {
                character_display_status_string(false, 12, "reflects it!",
                                                gbl.spell_target);

                character_draw_missile_attack(
                    0x2d, 4, combatmap_player_map_pos(player),
                    combatmap_player_map_pos(gbl.spell_target));

                gbl.spell_target = player;
            }
        }

        if (effect_roll_saving_throw(0, SAVE_VERSE_PETRIFICATION,
                                     gbl.spell_target) == false) {
            effect_kill_player("is Stoned", STATUS_STONED, gbl.spell_target);
        }
    }
}

/* cast_breath. A blue dragon's breath: a lightning bolt of the dragon's own hit
 * points, aimed with the lightning bolt targeting and then pushed out one square
 * so it starts beside the dragon rather than on it.
 *
 * var_1 - whether the bolt has bounced - is read by DoElecDamage before anything
 * has set it, which the C# flagged "Simeon"; false is what a C# local starts at.
 * Setting it true at the end is dead: nothing reads it again. */
void spelleffect_dragon_breath_elec(Effect add_remove, void *param,
                                    Player *player)
{
    Affect *affect = (Affect *)param;
    bool var_1 = false;

    (void)add_remove;

    if (player == NULL || gbl.spell_cast_function == NULL) {
        log_warn("dragon breath: no %s",
                 player == NULL ? "dragon" : "targeting function");
        return;
    }

    /* The first round is a certainty and every round after it an even chance. */
    if (gbl.combat_round == 0 || effect_roll_dice(100, 1) > 50) {
        Point player_pos = combatmap_player_map_pos(player);

        gbl.damage_flags = DAMAGE_DRAGON_BREATH | DAMAGE_ELECTRICITY;

        character_display_status_string(true, 10, "Breathes!", player);

        gbl.byte_1DA70 = gbl.spell_cast_function(QUICK_FIGHT_TRUE,
                                                 SPELL_LIGHTNING_BOLT);

        gbl.target_pos.x = player_pos.x +
                           sign_of(gbl.target_pos.x - player_pos.x);
        gbl.target_pos.y = player_pos.y +
                           sign_of(gbl.target_pos.y - player_pos.y);

        /* One square further east or south than west or north: the dragon's own
         * icon is two squares wide and tall, and this is its far corner. */
        if (gbl.target_pos.x == (player_pos.x + 1)) {
            gbl.target_pos.x++;
        }

        if (gbl.target_pos.y == (player_pos.y + 1)) {
            gbl.target_pos.y++;
        }

        effect_remove_invisibility(player);
        character_load_missile_icons(0x13);

        character_draw_missile_attack(0x32, 4, gbl.target_pos, player_pos);

        var_1 = do_elec_damage(var_1, 0, SAVE_VERSE_BREATH_WEAPON,
                               player->hit_point_max, gbl.target_pos);
        sub_5FA44(0, SAVE_VERSE_BREATH_WEAPON, player->hit_point_max, 10);

        /* 0xfe and 0xff count down; anything less is the last breath. */
        if (affect == NULL) {
            log_warn("dragon breath: no affect to count the breaths down on");
        } else if (affect->affect_data > 0xfd) {
            affect->affect_data -= 1;
        } else {
            effect_remove_affect(affect, AFFECT_BREATH_ELEC, player);
        }

        var_1 = true;
        (void)var_1;

        character_clear_actions(player);
    }
}

/* spell_spit_acid. Three chances in ten of hitting, and only within seven
 * squares. The range is asked for before the target is checked for, which is
 * harmless: no target reads as no range at all. */
void spelleffect_affect_spit_acid(Effect add_remove, void *param,
                                  Player *player)
{
    int roll;

    (void)add_remove;
    (void)param;

    if (player == NULL || gbl.spell_cast_function == NULL) {
        log_warn("spit acid: no %s",
                 player == NULL ? "spitter" : "targeting function");
        return;
    }

    gbl.byte_1DA70 = gbl.spell_cast_function(QUICK_FIGHT_TRUE, SPELL_41);

    gbl.spell_target = player_actions(player)->target;

    roll = effect_roll_dice(100, 1);

    if (character_target_range(gbl.spell_target, player) < 7 &&
        gbl.spell_target != NULL) {
        if (roll <= 30) {
            character_display_status_string(true, 10, "Spits Acid", player);
            character_load_missile_icons(0x17);

            character_draw_missile_attack(
                30, 1, combatmap_player_map_pos(gbl.spell_target),
                combatmap_player_map_pos(player));

            effect_damage_person(
                effect_roll_saving_throw(0, SAVE_VERSE_BREATH_WEAPON,
                                         gbl.spell_target),
                DAMAGE_ON_SAVE_HALF, player->hit_point_max, gbl.spell_target);
        } else {
            character_display_status_string(true, 10, "Spits Acid and Misses",
                                            player);
        }
    }
}

/* spell_breathes_acid. A black dragon: three breaths a fight, down a single line
 * six squares long. It will not breathe if one of its own side is in the way. */
void spelleffect_dragon_breath_acid(Effect add_remove, void *param,
                                    Player *attacker)
{
    Affect *affect = (Affect *)param;

    (void)add_remove;

    if (attacker == NULL || affect == NULL || gbl.spell_cast_function == NULL) {
        log_warn("dragon breath acid: no %s",
                 attacker == NULL ? "dragon"
                                  : (affect == NULL ? "affect"
                                                    : "targeting function"));
        return;
    }

    gbl.byte_1DA70 = false;

    if (gbl.combat_round == 0) {
        affect->affect_data = 3;
    }

    if (affect->affect_data > 0) {
        Point attacker_pos = combatmap_player_map_pos(attacker);

        gbl.damage_flags = DAMAGE_DRAGON_BREATH | DAMAGE_ACID;

        gbl.byte_1DA70 = gbl.spell_cast_function(QUICK_FIGHT_TRUE, SPELL_3D);

        if (gbl.byte_1DA70 == true) {
            build_area_damage_targets(6, 1, gbl.target_pos, attacker_pos);
        }

        for (int i = 0; i < gbl.spell_target_count; i++) {
            if (gbl.spell_targets[i] != NULL &&
                player_opposite_team(attacker) ==
                    (CombatTeam)gbl.spell_targets[i]->combat_team) {
                gbl.byte_1DA70 = false;
                break;
            }
        }

        if (gbl.byte_1DA70 == true && gbl.spell_target_count > 0) {
            character_display_status_string(true, 10, "breathes acid", attacker);
            character_load_missile_icons(0x12);

            character_draw_missile_attack(
                0x1e, 1, combatmap_player_map_pos(gbl.spell_targets[0]),
                combatmap_player_map_pos(attacker));

            for (int i = 0; i < gbl.spell_target_count; i++) {
                Player *target = gbl.spell_targets[i];
                bool save_made;

                if (target == NULL) {
                    continue;
                }

                save_made = effect_roll_saving_throw(0,
                                                     SAVE_VERSE_BREATH_WEAPON,
                                                     target);

                effect_damage_person(save_made, DAMAGE_ON_SAVE_HALF,
                                     attacker->hit_point_max, target);
            }

            affect->affect_data--;

            character_clear_actions(attacker);
        }
    }
}

/* spell_breathes_fire. A red dragon: a cone nine squares long and three wide,
 * and no care at all about who else is standing in it. */
void spelleffect_dragon_breath_fire(Effect add_remove, void *param,
                                    Player *attacker)
{
    Affect *affect = (Affect *)param;

    (void)add_remove;

    if (attacker == NULL || affect == NULL || gbl.spell_cast_function == NULL) {
        log_warn("dragon breath fire: no %s",
                 attacker == NULL ? "dragon"
                                  : (affect == NULL ? "affect"
                                                    : "targeting function"));
        return;
    }

    if (gbl.combat_round == 0) {
        affect->affect_data = 3;
    }

    if (affect->affect_data > 0) {
        Point attack_pos = combatmap_player_map_pos(attacker);

        gbl.damage_flags = DAMAGE_DRAGON_BREATH | DAMAGE_FIRE;

        gbl.byte_1DA70 = gbl.spell_cast_function(QUICK_FIGHT_TRUE, SPELL_3D);

        if (gbl.byte_1DA70 == true) {
            build_area_damage_targets(9, 3, gbl.target_pos, attack_pos);

            if (gbl.spell_target_count > 0) {
                character_display_status_string(true, 10, "breathes fire",
                                                attacker);
                character_load_missile_icons(0x12);

                character_draw_missile_attack(
                    0x1e, 1, combatmap_player_map_pos(gbl.spell_targets[0]),
                    combatmap_player_map_pos(attacker));

                for (int i = 0; i < gbl.spell_target_count; i++) {
                    Player *target = gbl.spell_targets[i];
                    bool saves;

                    if (target == NULL) {
                        continue;
                    }

                    saves = effect_roll_saving_throw(0,
                                                     SAVE_VERSE_BREATH_WEAPON,
                                                     target);

                    effect_damage_person(saves, DAMAGE_ON_SAVE_HALF,
                                         attacker->hit_point_max, target);
                }

                affect->affect_data -= 1;
                character_clear_actions(attacker);
            }
        }
    }
}

/* A hell hound's breath: an even chance of it, seven points of fire, and only on
 * a target it is standing next to. */
void spelleffect_cast_breath_fire(Effect add_remove, void *param,
                                  Player *player)
{
    (void)add_remove;
    (void)param;

    if (player == NULL || gbl.spell_cast_function == NULL) {
        log_warn("breath fire: no %s",
                 player == NULL ? "breather" : "targeting function");
        return;
    }

    gbl.byte_1DA70 = gbl.spell_cast_function(QUICK_FIGHT_TRUE, SPELL_41);
    gbl.spell_target = player_actions(player)->target;

    if (gbl.spell_target != NULL && effect_roll_dice(100, 1) <= 50 &&
        character_target_range(gbl.spell_target, player) < 2) {
        gbl.damage_flags = DAMAGE_FIRE;
        gbl.byte_1DA70 = true;

        character_clear_actions(player);

        character_display_status_string(true, 10, "Breathes Fire", player);
        character_load_missile_icons(0x17);

        character_draw_missile_attack(
            0x1e, 1, combatmap_player_map_pos(gbl.spell_target),
            combatmap_player_map_pos(player));

        effect_damage_person(
            effect_roll_saving_throw(0, SAVE_VERSE_BREATH_WEAPON,
                                     gbl.spell_target),
            DAMAGE_ON_SAVE_HALF, 7, gbl.spell_target);
    }
}

/* A storm giant's lightning: 16d6, and only in the first four rounds.
 *
 * The damage is rolled twice - once for the square it lands on and once for the
 * rest of its length - and the second roll saves against poison rather than
 * against the spell, both of which are the original's own doing. var_1 is the
 * same read-before-set as in the dragon's breath. */
void spelleffect_cast_throw_lightening(Effect add_remove, void *param,
                                       Player *caster)
{
    bool var_1 = false;

    (void)add_remove;
    (void)param;

    if (caster == NULL || gbl.spell_cast_function == NULL) {
        log_warn("throw lightning: no %s",
                 caster == NULL ? "thrower" : "targeting function");
        return;
    }

    if (gbl.combat_round < 4) {
        Point pos = combatmap_player_map_pos(caster);

        character_display_status_string(true, 10, "throws lightning", caster);

        gbl.byte_1DA70 = gbl.spell_cast_function(QUICK_FIGHT_TRUE,
                                                 SPELL_LIGHTNING_BOLT);

        effect_remove_invisibility(caster);
        character_load_missile_icons(0x13);
        character_draw_missile_attack(0x32, 4, gbl.target_pos, pos);

        var_1 = do_elec_damage(var_1, 0, SAVE_VERSE_SPELL,
                               effect_roll_dice_save(6, 16), gbl.target_pos);
        sub_5FA44(0, SAVE_VERSE_POISON, effect_roll_dice_save(6, 16), 10);

        var_1 = true;
        (void)var_1;

        character_clear_actions(caster);
    }
}

/* A medusa's gaze: aimed with the animate dead targeting, of all things, and it
 * paralyses for an hour rather than killing. */
void spelleffect_cast_gaze_paralyze(Effect add_remove, void *param,
                                    Player *player)
{
    Action *action;

    (void)add_remove;
    (void)param;

    if (player == NULL || gbl.spell_cast_function == NULL) {
        log_warn("gaze paralyze: no %s",
                 player == NULL ? "gazer" : "targeting function");
        return;
    }

    action = player_actions(player);
    action->target = NULL;

    gbl.byte_1DA70 = gbl.spell_cast_function(QUICK_FIGHT_TRUE,
                                             SPELL_ANIMATE_DEAD);

    gbl.spell_target = action->target;

    if (gbl.spell_target != NULL) {
        character_display_status_string(false, 10, "gazes...", player);

        character_load_missile_icons(0x12);

        character_draw_missile_attack(
            0x2d, 4, combatmap_player_map_pos(gbl.spell_target),
            combatmap_player_map_pos(player));

        if (effect_roll_saving_throw(0, SAVE_VERSE_PETRIFICATION,
                                     gbl.spell_target) == false) {
            effect_add_affect(false, 0xff, 0x3c, AFFECT_PARALYZE,
                              gbl.spell_target);
            character_display_status_string(false, 10, "is paralyzed",
                                            gbl.spell_target);
        }
    }
}

/* ---------------------------------------------------------- the spell table */

typedef void (*SpellHandler)(void);

/* gbl.spellTable, which setup_spells filled one Add at a time. Every id the
 * dictionary had a key for has an entry here and the rest are NULL: the ids the
 * name table leaves blank - 0x39 is not among them - plus 0x65, which the casting
 * table has a row for and no spell uses. */
static const SpellHandler SPELL_TABLE[SPELL_BESTOW_CURSE_MU + 1] = {
    [SPELL_BLESS]                        = cleric_bless,
    [SPELL_CURSE]                        = cleric_curse,
    [SPELL_CURE_LIGHT_WOUNDS]            = spell_cure_light,
    [SPELL_CAUSE_LIGHT_WOUNDS]           = spell_cause_light,
    [SPELL_DETECT_MAGIC_CL]              = is_affected,
    [SPELL_PROTECT_FROM_EVIL_CL]         = spell_protection_from_x,
    [SPELL_PROTECT_FROM_GOOD_CL]         = spell_protection_from_x,
    [SPELL_RESIST_COLD]                  = spell_resist_cold,
    [SPELL_BURNING_HANDS]                = spell_burning_hands,
    [SPELL_CHARM_PERSON]                 = spell_charm,
    [SPELL_DETECT_MAGIC_MU]              = is_affected,
    [SPELL_ENLARGE]                      = spell_enlarge,
    [SPELL_REDUCE]                       = spell_reduce,
    [SPELL_FRIENDS]                      = spell_friends,
    [SPELL_MAGIC_MISSILE]                = spell_magic_missile,
    [SPELL_PROTECT_FROM_EVIL_MU]         = spell_protection_from_x,
    [SPELL_PROTECT_FROM_GOOD_MU]         = spell_protection_from_x,
    [SPELL_READ_MAGIC]                   = is_affected,
    [SPELL_SHIELD]                       = spell_shield,
    [SPELL_SHOCKING_GRASP]               = spell_shocking_grasp,
    [SPELL_SLEEP]                        = spell_sleep,
    [SPELL_FIND_TRAPS]                   = is_affected,
    [SPELL_HOLD_PERSON_CL]               = spell_hold_x,
    [SPELL_RESIST_FIRE]                  = spell_fire_resistant,
    [SPELL_SILENCE_15_RADIUS]            = spell_silence_15_radius,
    [SPELL_SLOW_POISON]                  = is_affected2,
    [SPELL_SNAKE_CHARM]                  = spell_snake_charm,
    [SPELL_SPIRITUAL_HAMMER]             = spell_spiritual_hammer,
    [SPELL_DETECT_INVISIBILITY]          = is_affected,
    [SPELL_INVISIBILITY]                 = is_invisible,
    [SPELL_KNOCK]                        = spell_knock,
    [SPELL_MIRROR_IMAGE]                 = spell_mirror_image,
    [SPELL_RAY_OF_ENFEEBLEMENT]          = spell_ray_of_enfeeblement,
    [SPELL_STINKING_CLOUD]               = spell_stinking_cloud,
    [SPELL_STRENGTH]                     = spell_strength,
    [SPELL_ANIMATE_DEAD]                 = spell_animate_dead,
    [SPELL_CURE_BLINDNESS]               = spell_cure_blindness,
    [SPELL_CAUSE_BLINDNESS]              = spell_cause_blindness,
    [SPELL_CURE_DISEASE]                 = spell_cure_disease,
    [SPELL_CAUSE_DISEASE]                = spell_cause_disease,
    [SPELL_DISPEL_MAGIC_CL]              = spell_dispel_magic,
    [SPELL_PRAYER]                       = spell_prayer,
    [SPELL_REMOVE_CURSE]                 = spell_remove_curse,
    [SPELL_BESTOW_CURSE_CL]              = curse,
    [SPELL_BLINK]                        = spell_blinking,
    [SPELL_DISPEL_MAGIC_MU]              = spell_dispel_magic,
    [SPELL_FIREBALL]                     = sub_5F782,
    [SPELL_HASTE]                        = cast_haste,
    [SPELL_HOLD_PERSON_MU]               = spell_hold_x,
    [SPELL_INVISIBILITY_10_RADIUS]       = is_invisible,
    [SPELL_LIGHTNING_BOLT]               = spell_lightning_bolt,
    [SPELL_PROTECT_FROM_EVIL_10_RAD]     = spell_protection_from_x,
    [SPELL_PROTECT_FROM_GOOD_10_RAD]     = spell_protection_from_x,
    [SPELL_PROTECT_FROM_NORMAL_MISSILES] = spell_protection_from_x,
    [SPELL_SLOW]                         = spell_slow,
    [SPELL_RESTORATION]                  = spell_restoration,
    [SPELL_39]                           = cast_speed,
    [SPELL_CURE_SERIOUS_WOUNDS]          = spell_cure_serious_wounds,
    [SPELL_3B]                           = cast_strength,
    [SPELL_3C]                           = sub_6003C,
    [SPELL_3D]                           = cast_paralyzed,
    [SPELL_3E]                           = cast_heal,
    [SPELL_3F]                           = cast_invisible,
    [SPELL_40]                           = sub_5F782,
    [SPELL_41]                           = dam2d4plus2,
    [SPELL_CAUSE_SERIOUS_WOUNDS]         = spell_cause_serious_wounds,
    [SPELL_NEUTRALIZE_POISON]            = spell_neutralize_poison,
    [SPELL_POISON]                       = spell_poison,
    [SPELL_PROTECT_EVIL_10_RAD]          = spell_protection_from_x,
    [SPELL_STICKS_TO_SNAKES]             = spell_sticks_to_snakes,
    [SPELL_CURE_CRITICAL_WOUNDS]         = spell_cure_critical_wounds,
    [SPELL_CAUSE_CRITICAL_WOUNDS]        = spell_cause_critical_wounds,
    [SPELL_DISPEL_EVIL]                  = spell_dispel_evil,
    [SPELL_FLAME_STRIKE]                 = spell_flame_strike,
    [SPELL_RAISE_DEAD]                   = spell_raise_dead,
    [SPELL_SLAY_LIVING]                  = spell_slay_living,
    [SPELL_DETECT_MAGIC_DR]              = is_affected,
    [SPELL_ENTANGLE]                     = spell_entangle,
    [SPELL_FAERIE_FIRE]                  = spell_faerie_fire,
    [SPELL_INVISIBILITY_TO_ANIMALS]      = spell_invis_to_animals,
    [SPELL_CHARM_MONSTERS]               = spell_charm_monsters,
    [SPELL_CONFUSION]                    = spell_confusion,
    [SPELL_DIMENSION_DOOR]               = spell_dimension_door,
    [SPELL_FEAR]                         = spell_fear,
    [SPELL_FIRE_SHIELD]                  = spell_fire_protection,
    [SPELL_FUMBLE]                       = spell_fumble,
    [SPELL_ICE_STORM]                    = spell_ice_storm,
    [SPELL_MINOR_GLOBE_OF_INVULN]        = spell_minor_globe_of_invulnerability,
    [SPELL_59]                           = spell_remove_curse,
    [SPELL_5A]                           = spell_animate_dead,
    [SPELL_CLOUD_KILL]                   = spell_cloud_kill,
    [SPELL_CONE_OF_COLD]                 = spell_cone_of_cold,
    [SPELL_FEEBLEMIND]                   = spell_feeblemind,
    [SPELL_HOLD_MONSTERS]                = spell_hold_x,
    [SPELL_5F]                           = sub_616CC,
    [SPELL_60]                           = sub_616CC,
    [SPELL_61]                           = sub_616CC,
    [SPELL_62]                           = sub_61727,
    [SPELL_63]                           = cast_heal2,
    [SPELL_BESTOW_CURSE_MU]              = curse
};

void spelleffect_setup_spells(void)
{
    gbl.cure_spell = false;
    gbl.spell_from_item = false;
    gbl.last_selected_spell_target = NULL;
    gbl.byte_1D2C8 = true;

    gbl.spell_cast_function = spellcast_non_combat_cast;
}

void spelleffect_call(int spell_id)
{
    if (spell_id < 0 || (size_t)spell_id >= COAB_ARRAY_LEN(SPELL_TABLE) ||
        SPELL_TABLE[spell_id] == NULL) {
        /* The C# dictionary lookup threw KeyNotFoundException. */
        log_warn("spell effect: nothing happens for spell 0x%x", spell_id);
        return;
    }

    SPELL_TABLE[spell_id]();
}
