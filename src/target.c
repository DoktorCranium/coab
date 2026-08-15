/* target.c - Ported from engine/ovr032.cs. */
#include "target.h"

#include "combatmap.h"
#include "gbl.h"
#include "log.h"
#include "tile.h"

/* The result of one reach walk (ovr032.MapReach). */
typedef struct {
    bool  reach;
    Point target;
    int   range;
} MapReach;

/* ovr032 kept every answer in a 1250 x 1250 x 2 array of these and never
 * cleared it. That cache is not ported, for two reasons: the walk it saves is a
 * few dozen steps of integer arithmetic, and the answers go stale. They depend
 * on the ground tiles, which change during a fight as clouds drift and bodies
 * fall, and on which map is being fought over - a cached reach from the first
 * fight of the game would still be answering questions in the last one. The DOS
 * original recomputed every call, which is what this does. */

/* One value per map square, read as plain floor when there is no fight on. */
static int ground_tile_at(Point pos)
{
    if (gbl.map_to_background_tile == NULL) {
        return 0;
    }
    return ground_tile_map_get(gbl.map_to_background_tile, pos);
}

static const BackgroundTile *tile_at(Point pos)
{
    const BackgroundTile *bt = background_tile(ground_tile_at(pos));

    /* background_tile logs and returns NULL past the end of the table; the C#
     * would have thrown. Empty floor keeps the walk going. */
    return (bt != NULL) ? bt : &background_tiles[0];
}

/* sub_733F1. The walk is two stepping paths at once: var_19 over the map squares
 * between the two points, and var_31 over a second line that carries the height
 * the shot is travelling at. That second line runs from the attacker's height to
 * the attacker's height, so its y never actually changes - every square is
 * measured against the height of the ground the attacker is standing on. The
 * pair is kept because that is what the original walked. */
static MapReach can_reach_target_calc(bool ignore_walls, Point out_pos,
                                      Point attacker)
{
    SteppingPath var_31;
    SteppingPath var_19;
    MapReach mr;
    /* Fixed at 513 in the C#, where the cache had to be range-independent. The
     * longest path across a 50x25 map costs far less, so this never fires; the
     * range allowance is applied by the callers instead. */
    const int max_range = (256 * 2) + 1;
    bool finished = false;

    stepping_path_clear(&var_31);
    stepping_path_clear(&var_19);

    var_19.attacker = attacker;
    var_19.target   = out_pos;
    stepping_path_calculate_deltas(&var_19);

    var_31.attacker.x = 0;
    var_31.attacker.y = tile_at(attacker)->field_1;

    if (var_19.diff_x > var_19.diff_y) {
        var_31.target.x = var_19.diff_x;
    } else {
        var_31.target.x = var_19.diff_y;
    }

    var_31.target.y = tile_at(attacker)->field_1;
    stepping_path_calculate_deltas(&var_31);

    do {
        const BackgroundTile *bt = tile_at(var_19.current);

        if (!ignore_walls && bt->field_2 > var_31.current.y) {
            /* Something too tall is in the way: the path stops on that square,
             * which is where a missile lands. */
            mr.reach  = false;
            mr.range  = var_19.steps;
            mr.target = var_19.current;
            return mr;
        }

        if (var_19.steps > max_range) {
            mr.reach  = false;
            mr.range  = var_19.steps;
            mr.target = var_19.current;
            return mr;
        }

        stepping_path_step(&var_31);
        finished = !stepping_path_step(&var_19);
    } while (!finished);

    mr.reach  = true;
    mr.range  = var_19.steps;
    mr.target = out_pos;
    return mr;
}

static MapReach map_reach_get(Point attacker, Point target, bool ignore_walls)
{
    return can_reach_target_calc(ignore_walls, target, attacker);
}

void target_can_reach(Point *target, Point attacker)
{
    bool ignore_walls = (gbl.map_to_background_tile != NULL) &&
                        gbl.map_to_background_tile->ignore_walls;
    MapReach mr;

    if (target == NULL) {
        log_warn("canReachTarget: no target to move");
        return;
    }

    mr = map_reach_get(attacker, *target, ignore_walls);
    *target = mr.target;
}

bool target_can_reach_range(int *range, Point target, Point attacker)
{
    bool ignore_walls = (gbl.map_to_background_tile != NULL) &&
                        gbl.map_to_background_tile->ignore_walls;
    MapReach mr = map_reach_get(attacker, target, ignore_walls);

    if (range == NULL) {
        log_warn("canReachTarget: no range to report");
        return false;
    }

    /* Steps are counted in halves, so an allowance of n squares is 2n+1. */
    if (mr.range > (*range * 2) + 1) {
        return false;
    }

    *range = mr.range;
    return mr.reach;
}

/* sub_7354A */
bool target_can_see(int direction, Point player_a, Point player_b)
{
    int facing_x, facing_y;
    bool can_see;

    if (!point_map_in_bounds(player_a) || !point_map_in_bounds(player_b)) {
        return false;
    }

    if (direction == 0xff || direction == 8) {
        return true;
    }

    if (direction < 0 || direction > 8) {
        /* The C# threw ApplicationException here. There is nowhere sensible to
         * look from, so nothing is seen. */
        log_warn("CanSeeCombatant: %d is not a direction", direction);
        return false;
    }

    facing_x = player_b.x + GBL_MAP_DIR_X_DELTA[direction];
    facing_y = player_b.y + GBL_MAP_DIR_Y_DELTA[direction];

    if (point_eq(player_b, player_a) ||
        (facing_x == player_a.x && facing_y == player_a.y)) {
        return true;
    }

    switch (direction) {
    case 0:
        can_see = ((player_a.x >= facing_x &&
                    player_a.y <= ((facing_x - player_a.x) + facing_y)) ||
                   (player_a.x <= facing_x &&
                    player_a.y <= ((player_a.x - facing_x) + facing_y)));
        break;

    case 1:
        can_see = ((player_a.x >= facing_x &&
                    player_a.y <= ((facing_x - player_a.x) + facing_y)) ||
                   (player_a.x >= ((facing_x - facing_y) + player_a.y) &&
                    player_a.y <= facing_y));
        break;

    case 2:
        can_see = ((player_a.x >= (facing_x + facing_y - player_a.y) &&
                    player_a.y <= facing_y) ||
                   (player_a.x >= (facing_x + player_a.y - facing_y) &&
                    player_a.y >= facing_y));
        break;

    case 3:
        can_see = ((player_a.x >= ((facing_x + player_a.y) - facing_y) &&
                    player_a.y >= facing_y) ||
                   (player_a.x >= facing_x &&
                    player_a.y >= ((player_a.x - facing_x) + facing_y)));
        break;

    case 4:
        can_see = ((player_a.x >= facing_x &&
                    player_a.y >= ((player_a.x - facing_x) + facing_y)) ||
                   (player_a.x <= facing_x &&
                    player_a.y >= ((facing_x - player_a.x) + facing_y)));
        break;

    case 5:
        can_see = ((player_a.x <= facing_x &&
                    player_a.y >= ((facing_x - player_a.x) + facing_y)) ||
                   (player_a.x <= ((facing_x + facing_y) - player_a.y) &&
                    player_a.y >= facing_y));
        break;

    case 6:
        can_see = ((player_a.x <= ((facing_x + facing_y) - player_a.y) &&
                    player_a.y >= facing_y) ||
                   (player_a.x <= ((facing_x + player_a.y) - facing_y) &&
                    player_a.y <= facing_y));
        break;

    case 7:
        can_see = ((player_a.x <= ((facing_x + player_a.y) - facing_y) &&
                    player_a.y <= facing_y) ||
                   (player_a.x <= facing_x &&
                    player_a.y <= ((player_a.x - facing_x) + facing_y)));
        break;

    default:
        can_see = true;
        break;
    }

    return can_see;
}

u8 target_find_combatant_direction(Point target, Point attacker)
{
    u8 dir = 0;

    /* The test comes first, so a target nobody can see leaves dir at 8 - the
     * no-direction, which every caller reads as "face anywhere". */
    while (!target_can_see(dir, target, attacker) && dir < 8) {
        dir++;
    }

    return dir;
}

/* sub_738D8 */
int target_sorted_combatants(SortedCombatant *out, int out_size, int size,
                            int max_range, Point pos, TargetFilter filter,
                            void *ctx)
{
    Point attacker_map[COMBATMAP_MAX_DELTAS];
    int attacker_count;
    int found_count = 0;

    if (out == NULL || out_size <= 0 || filter == NULL) {
        log_warn("Rebuild_SortedCombatantList: nowhere to put the answer");
        return 0;
    }

    /* GetSizeBasedMapDeltas was fetched here in the C# and never used. */
    attacker_count = combatmap_build_size_map(size, pos, attacker_map);

    for (int player_index = 1; player_index <= gbl.combatant_count;
         player_index++) {
        const CombatantMap *combatant_map = &gbl.combat_map[player_index];
        Point target_map[COMBATMAP_MAX_DELTAS];
        int target_count;
        bool found = false;
        int found_range = max_range;
        Point found_target = point_make(0, 0);
        Point found_attacker = point_make(0, 0);

        if (player_index > GBL_MAX_COMBATANT_COUNT ||
            combatant_map->size <= 0 ||
            !filter(gbl.player_array[player_index], ctx)) {
            continue;
        }

        target_count = combatmap_build_size_map(combatant_map->size,
                                                combatant_map->pos, target_map);

        for (int t = 0; t < target_count; t++) {
            for (int a = 0; a < attacker_count; a++) {
                int tmp_range = max_range;

                if (target_can_reach_range(&tmp_range, target_map[t],
                                           attacker_map[a])) {
                    found = true;

                    if (tmp_range < found_range) {
                        found_range    = tmp_range;
                        found_attacker = attacker_map[a];
                        found_target   = target_map[t];
                    }
                }
            }
        }

        if (found) {
            if (found_count >= out_size) {
                log_warn("Rebuild_SortedCombatantList: no room for combatant %d",
                         player_index);
                break;
            }

            out[found_count].player    = gbl.player_array[player_index];
            out[found_count].pos       = combatant_map->pos;
            out[found_count].steps     = found_range;
            out[found_count].direction =
                target_find_combatant_direction(found_target, found_attacker);
            found_count++;
        }
    }

    combatant_sort(out, found_count);

    return found_count;
}

int target_sorted_combatants_for(SortedCombatant *out, int out_size,
                                 const Player *player, int max_range,
                                 TargetFilter filter, void *ctx)
{
    const CombatantMap *cm =
        &gbl.combat_map[combatmap_player_index(player)];

    return target_sorted_combatants(out, out_size, cm->size, max_range, cm->pos,
                                    filter, ctx);
}
