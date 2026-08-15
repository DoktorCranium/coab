/* combatmap.c - Ported from engine/ovr033.cs. */
#include "combatmap.h"

#include "draw.h"
#include "gbl.h"
#include "icons.h"
#include "input.h"
#include "log.h"
#include "sound.h"
#include "text.h"
#include "tile.h"

#include <string.h>

/* ovr033.Steps. The squares a combatant covers, by size: one square, two side by
 * side either way, or a 2x2 block. */
static const Point steps[COMBATMAP_MAX_SIZE + 1][COMBATMAP_MAX_DELTAS] = {
    { { 0, 0 } },
    { { 0, 0 } },
    { { 0, 0 }, { 0, 1 } },
    { { 0, 0 }, { 1, 0 } },
    { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }
};
static const int step_count[COMBATMAP_MAX_SIZE + 1] = { 0, 1, 2, 2, 4 };

int combatmap_size_deltas(int size, Point *out)
{
    if (size < 0 || size > COMBATMAP_MAX_SIZE) {
        /* The C# indexed Steps and would have thrown. A combatant of no size
         * covers no squares, which is how a dead one is already handled. */
        log_warn("combat: no size map for size %d", size);
        return 0;
    }

    for (int i = 0; i < step_count[size]; i++) {
        out[i] = steps[size][i];
    }
    return step_count[size];
}

int combatmap_build_size_map(int size, Point pos, Point *out)
{
    int count = combatmap_size_deltas(size, out);

    for (int i = 0; i < count; i++) {
        out[i] = point_add(pos, out[i]);
    }
    return count;
}

/* Everything below draws, and drawing needs the ground tile map, which only
 * exists while a fight is on. The C# would have thrown a NullReferenceException;
 * here the drawing is skipped and the reason is logged. */
static bool has_ground_map(const char *what)
{
    if (gbl.map_to_background_tile != NULL) {
        return true;
    }
    log_warn("combat: %s with no ground tile map", what);
    return false;
}

static Point screen_top_left(void)
{
    if (gbl.map_to_background_tile == NULL) {
        return point_make(0, 0);
    }
    return gbl.map_to_background_tile->map_screen_top_left;
}

/* gbl.BackGroundTiles[i].tile_index, for an i the map may have made up. */
static int tile_index_of(int ground_tile)
{
    const BackgroundTile *bt = background_tile(ground_tile);

    return (bt != NULL) ? bt->tile_index : 0;
}

static int move_cost_of(int ground_tile)
{
    const BackgroundTile *bt = background_tile(ground_tile);

    return (bt != NULL) ? bt->move_cost : 0;
}

/* sub_74077 */
static void calculate_player_screen_positions(void)
{
    Point top_left = screen_top_left();

    for (int i = 1; i <= gbl.combatant_count; i++) {
        gbl.combat_map[i].screen_pos = point_sub(gbl.combat_map[i].pos, top_left);
    }
}

void combatmap_color_0_8_inverse(void)
{
    draw_set_palette_color(8, 0);
    draw_set_palette_color(0, 8);
}

void combatmap_color_0_8_normal(void)
{
    draw_set_palette_color(0, 0);
    draw_set_palette_color(8, 8);
}

static void draw_player_icon_if_on_screen(int player_index)
{
    if (player_index > 0 &&
        combatmap_player_on_screen(false, player_index)) {
        Player *player = gbl.player_array[player_index];

        if (player == NULL) {
            /* The C# would have thrown; the map and the roster are set up
             * together, so this only happens when one of them is stale. */
            log_warn("combat: combatant %d is on the map but not in the roster",
                     player_index);
            return;
        }

        /* Drawn after the ground and the focus box, so the combatant is on top. */
        icons_draw_combat_icon(player->icon_id, COMBAT_ICON_NORMAL,
                               player_actions(player)->direction,
                               gbl.combat_map[player_index].screen_pos.y,
                               gbl.combat_map[player_index].screen_pos.x);
    }
}

/* sub_7416E */
void combatmap_draw_position(Point pos)
{
    Point map[COMBATMAP_MAX_DELTAS];
    Point screen_pos;
    int count;

    if (!has_ground_map("drawing a map square")) {
        return;
    }

    screen_pos = point_sub(pos, screen_top_left());
    count = combatmap_build_size_map(gbl.map_to_background_tile->size,
                                     point_make(0, 0), map);

    for (int i = 0; i < count; i++) {
        Point p = map[i];

        if (combatmap_coord_on_screen(point_add(screen_pos, p))) {
            int tile = ground_tile_map_get(gbl.map_to_background_tile,
                                           point_add(p, pos));

            icons_draw_iso_tile(tile_index_of(tile), (screen_pos.y + p.y) * 3,
                                (screen_pos.x + p.x) * 3);

            if (gbl.map_to_background_tile->draw_target_cursor) {
                /* Icon 0x19 is the grey focus box the target cursor is made of. */
                icons_draw_combat_icon(0x19, COMBAT_ICON_NORMAL, 0,
                                       screen_pos.y + p.y, screen_pos.x + p.x);
            }
        }
    }

    draw_player_icon_if_on_screen(combatmap_player_index_at(pos.y, pos.x));
}

/* sub_7431C */
void combatmap_redraw_position(Point pos)
{
    int player_index = combatmap_player_index_at(pos.y, pos.x);

    combatmap_redraw_player_background_at(player_index, pos);
    draw_player_icon_if_on_screen(player_index);
}

/* ovr033.mapToPlayerIndex: which combatant covers each map square. */
static int map_to_player_index[MAP_MAX_Y][MAP_MAX_X];

/* sub_743E7 */
void combatmap_setup_player_index(void)
{
    Point top_left = screen_top_left();

    memset(map_to_player_index, 0, sizeof(map_to_player_index));

    for (int index = 1; index <= gbl.combatant_count; index++) {
        CombatantMap *cm = &gbl.combat_map[index];

        if (cm->size > 0) {
            Point map[COMBATMAP_MAX_DELTAS];
            int count = combatmap_build_size_map(cm->size, cm->pos, map);

            for (int i = 0; i < count; i++) {
                /* The original only tested the upper bounds and would have
                 * thrown on a negative one; a combatant off the top or left of
                 * the map is simply not recorded here. */
                if (map[i].x >= 0 && map[i].x < MAP_MAX_X &&
                    map[i].y >= 0 && map[i].y < MAP_MAX_Y) {
                    map_to_player_index[map[i].y][map[i].x] = index;
                }
            }

            cm->screen_pos = point_sub(cm->pos, top_left);
        }
    }
}

/* sub_74505 */
int combatmap_player_index_at(int pos_y, int pos_x)
{
    if (pos_x >= MAP_MIN_X && pos_x < MAP_MAX_X &&
        pos_y >= MAP_MIN_Y && pos_y < MAP_MAX_Y) {
        return map_to_player_index[pos_y][pos_x];
    }
    return 0;
}

void combatmap_at_map_xy(int *ground_tile, int *player_index, Point pos)
{
    combatmap_at_map_yx(ground_tile, player_index, pos.y, pos.x);
}

/* sub_74505 */
void combatmap_at_map_yx(int *ground_tile, int *player_index, int pos_y,
                         int pos_x)
{
    if (gbl.map_to_background_tile != NULL &&
        pos_x >= MAP_MIN_X && pos_x < MAP_MAX_X &&
        pos_y >= MAP_MIN_Y && pos_y < MAP_MAX_Y) {
        *ground_tile = ground_tile_map_get(gbl.map_to_background_tile,
                                          point_make(pos_x, pos_y));
        *player_index = map_to_player_index[pos_y][pos_x];
    } else {
        *player_index = 0;
        *ground_tile = 0;
    }
}

/* sub_74572 */
void combatmap_redraw_player_background_at(int player_index, Point map)
{
    Point screen;

    if (!has_ground_map("redrawing a combatant's ground")) {
        return;
    }

    screen = point_sub(map, screen_top_left());

    if (player_index > 0) {
        combatmap_redraw_player_background(player_index);
    } else if (combatmap_coord_on_screen(screen)) {
        int tile = ground_tile_map_get(gbl.map_to_background_tile, map);

        icons_draw_iso_tile(tile_index_of(tile), screen.y * 3, screen.x * 3);
    }
}

/* sub_74572 */
void combatmap_redraw_player_background(int player_index)
{
    Point deltas[COMBATMAP_MAX_DELTAS];
    Point screen;
    Point map;
    int count;

    if (player_index == 0) {
        return;
    }
    if (player_index < 0 || player_index > GBL_MAX_COMBATANT_COUNT) {
        log_warn("combat: no combatant %d", player_index);
        return;
    }
    if (!has_ground_map("redrawing a combatant's ground")) {
        return;
    }

    /* The screen position is the one that was worked out when the combatant last
     * moved, so the map position is derived back from it rather than read from
     * the combatant: if the window has scrolled since, both agree. */
    screen = gbl.combat_map[player_index].screen_pos;
    map = point_add(screen, screen_top_left());

    count = combatmap_size_deltas(gbl.combat_map[player_index].size, deltas);

    for (int i = 0; i < count; i++) {
        Point delta = deltas[i];

        if (combatmap_coord_on_screen(point_add(delta, screen))) {
            int tile = ground_tile_map_get(gbl.map_to_background_tile,
                                           point_add(map, delta));

            icons_draw_iso_tile(tile_index_of(tile), (screen.y + delta.y) * 3,
                                (screen.x + delta.x) * 3);
        }
    }
}

/* sub_74730 */
bool combatmap_coord_on_screen(Point pos)
{
    return pos.x >= 0 && pos.x <= SCREEN_MAX_X &&
           pos.y >= 0 && pos.y <= SCREEN_MAX_Y;
}

bool combatmap_player_on_screen_p(bool all_on_screen, const Player *player)
{
    return combatmap_player_on_screen(all_on_screen,
                                      combatmap_player_index(player));
}

/* sub_74761 */
bool combatmap_player_on_screen(bool all_on_screen, int player_index)
{
    Point map[COMBATMAP_MAX_DELTAS];
    int count;
    bool result;

    if (player_index < 0 || player_index > GBL_MAX_COMBATANT_COUNT) {
        log_warn("combat: no combatant %d", player_index);
        return false;
    }
    if (gbl.combat_map[player_index].size == 0) {
        return false;
    }

    count = combatmap_build_size_map(gbl.combat_map[player_index].size,
                                     gbl.combat_map[player_index].screen_pos,
                                     map);
    result = true;

    for (int i = 0; i < count; i++) {
        if (!combatmap_coord_on_screen(map[i])) {
            result = false;
            if (all_on_screen) {
                return false;
            }
        } else {
            result = true;
            if (!all_on_screen) {
                return true;
            }
        }
    }

    return result;
}

/* ovr033.ScreenMapCheck. The window is nudged one square at a time rather than
 * jumped, which is what keeps the party near the middle of it. */
bool combatmap_screen_check(int radius, Point pos)
{
    Point screen_centre;
    int var_2;
    int min_x, max_x, min_y, max_y;

    if (!has_ground_map("checking the combat window")) {
        return false;
    }

    screen_centre = point_add(gbl.map_to_background_tile->map_screen_top_left,
                              point_screen_center());

    /* A radius of 0xff forces the redraw, and then measures against a radius of
     * nothing, so the window centres on pos. */
    var_2 = (radius == 0xff) ? 0 : radius;

    min_x = screen_centre.x - var_2;
    max_x = screen_centre.x + var_2;
    min_y = screen_centre.y - var_2;
    max_y = screen_centre.y + var_2;

    if (radius == 0xff ||
        pos.x < min_x || pos.x > max_x ||
        pos.y < min_y || pos.y > max_y) {
        int screen_row_y;
        int map_y;
        const int icon_column_size = 3;

        if (pos.x < min_x) {
            while (pos.x < screen_centre.x &&
                   screen_centre.x > (MAP_MIN_X + SCREEN_HALF_X)) {
                screen_centre.x -= 1;
            }
        } else if (pos.x > max_x) {
            while (pos.x > screen_centre.x &&
                   screen_centre.x < (MAP_MAX_X - SCREEN_HALF_X - 1)) {
                screen_centre.x += 1;
            }
        }

        if (pos.y < min_y) {
            while (pos.y < screen_centre.y &&
                   screen_centre.y > (MAP_MIN_Y + SCREEN_HALF_Y)) {
                screen_centre.y -= 1;
            }
        } else if (pos.y > max_y) {
            while (pos.y > screen_centre.y &&
                   screen_centre.y < (MAP_MAX_Y - SCREEN_HALF_Y - 1)) {
                screen_centre.y += 1;
            }
        }

        gbl.map_to_background_tile->map_screen_top_left =
            point_sub(screen_centre, point_screen_center());

        screen_row_y = 0;
        map_y = gbl.map_to_background_tile->map_screen_top_left.y;

        for (int i = 0; i <= 6; i++) {
            int screen_col_x = 0;
            int map_x = gbl.map_to_background_tile->map_screen_top_left.x;

            for (int j = 0; j <= 6; j++) {
                int tile = ground_tile_map_get(gbl.map_to_background_tile,
                                               point_make(map_x, map_y));

                icons_draw_iso_tile(tile_index_of(tile), screen_row_y,
                                    screen_col_x);

                screen_col_x += icon_column_size;
                map_x++;
            }
            screen_row_y += icon_column_size;
            map_y++;
        }

        calculate_player_screen_positions();

        return true;
    }

    return false;
}

/* sub_749DD */
void combatmap_redraw_area(int dir, int radius, Point map)
{
    Point new_pos = point_add(map, map_direction_step(dir));

    if (combatmap_screen_check(radius, new_pos)) {
        /* The window moved, so every combatant standing in it has to be put back
         * over the freshly drawn ground. */
        for (int index = 1; index <= gbl.combatant_count; index++) {
            Player *player = gbl.player_array[index];

            if (player != NULL && player->in_combat &&
                gbl.combat_map[index].size > 0 &&
                combatmap_player_on_screen_p(false, player)) {
                Point pos = gbl.combat_map[index].screen_pos;

                icons_draw_combat_icon(player->icon_id, COMBAT_ICON_NORMAL,
                                       player_actions(player)->direction,
                                       pos.y, pos.x);
            }
        }
    }

    combatmap_redraw_position(map);

    if (!combatmap_coord_on_screen(point_sub(new_pos, screen_top_left()))) {
        point_map_clamp(&new_pos);
    }

    combatmap_draw_position(new_pos);
    /* seg040.DrawOverlay() went here; it does nothing. */
}

/* sub_74B3F */
void combatmap_draw_player(bool arg_0, CombatIconState state, int direction,
                           Player *player)
{
    int player_index = combatmap_player_index(player);
    Action *actions = player_actions(player);

    if (!combatmap_player_on_screen_p(true, player) &&
        gbl.focus_combat_area_on_player) {
        combatmap_redraw_area(8, 3, combatmap_player_map_pos(player));
    }

    /* Only a turn that changes which way the icon faces needs the ground put
     * back first: the two halves of the compass share one mirrored picture. */
    if ((direction >> 2) != (actions->direction >> 2) ||
        state == COMBAT_ICON_ATTACK || arg_0) {
        if (gbl.focus_combat_area_on_player) {
            combatmap_redraw_player_background(player_index);
        }
    }

    actions->direction = direction;

    if (!arg_0 && combatmap_player_on_screen_p(false, player) &&
        gbl.focus_combat_area_on_player) {
        Point pos = gbl.combat_map[player_index].screen_pos;

        icons_draw_combat_icon(player->icon_id, state, direction, pos.y, pos.x);
        /* seg040.DrawOverlay() went here; it does nothing. */
    }
}

/* sub_74C5A */
Point combatmap_player_map_pos(const Player *player)
{
    return gbl.combat_map[combatmap_player_index(player)].pos;
}

/* sub_74C82 */
int combatmap_player_map_size(const Player *player)
{
    return gbl.combat_map[combatmap_player_index(player)].size;
}

int combatmap_player_index(const Player *player)
{
    for (int i = 0; i < GBL_PLAYER_ARRAY; i++) {
        if (gbl.player_array[i] == player) {
            return i;
        }
    }

    /* Array.FindIndex returned -1, which the original turned into 0 - combatant
     * 0, which is always empty. */
    return 0;
}

/* sub_74D04. Both overloads of the original, which differ in one way: when the
 * clouds are being reported a cloud tile sets its flag and is then left out of
 * the move-cost comparison, so the tile underneath it wins instead. */
static void ground_information(bool *is_poisonous_cloud, bool *is_noxious_cloud,
                              int *ground_tile, int *player_index,
                              int direction, Player *player)
{
    Point map[COMBATMAP_MAX_DELTAS];
    int count;
    int max_move_cost = 1;
    int current_player_index = combatmap_player_index(player);
    CombatantMap *cm = &gbl.combat_map[current_player_index];

    *player_index = 0;
    *ground_tile = 0x17;
    if (is_noxious_cloud != NULL) {
        *is_noxious_cloud = false;
    }
    if (is_poisonous_cloud != NULL) {
        *is_poisonous_cloud = false;
    }

    count = combatmap_build_size_map(cm->size, cm->pos, map);

    for (int i = 0; i < count; i++) {
        Point tmp_pos = point_add(map[i], map_direction_step(direction));
        int at_ground_tile;
        int at_player_idx;

        combatmap_at_map_xy(&at_ground_tile, &at_player_idx, tmp_pos);

        /* A big combatant covers several squares and would otherwise find
         * themselves in their own way. */
        if (at_player_idx == current_player_index) {
            at_player_idx = 0;
        }

        if (at_player_idx > 0) {
            *player_index = at_player_idx;
        }

        if (at_ground_tile == 0) {
            /* Off the map, or a square the fight does not use: this is what
             * stops a combatant walking out of the arena, and once one square
             * says so the answer cannot be anything else. */
            *ground_tile = 0;
        } else if (*ground_tile != 0) {
            if (is_noxious_cloud != NULL && at_ground_tile == TILE_STINKING_CLOUD) {
                *is_noxious_cloud = true;
            } else if (is_poisonous_cloud != NULL &&
                       at_ground_tile == TILE_CLOUD_KILL) {
                *is_poisonous_cloud = true;
            } else if (move_cost_of(at_ground_tile) >= max_move_cost) {
                max_move_cost = move_cost_of(at_ground_tile);
                *ground_tile = at_ground_tile;
            }
        }
    }
}

void combatmap_ground_information(int *ground_tile, int *player_index,
                                  int direction, Player *player)
{
    ground_information(NULL, NULL, ground_tile, player_index, direction, player);
}

void combatmap_ground_information_clouds(bool *is_poisonous_cloud,
                                         bool *is_noxious_cloud,
                                         int *ground_tile, int *player_index,
                                         int direction, Player *player)
{
    ground_information(is_poisonous_cloud, is_noxious_cloud, ground_tile,
                       player_index, direction, player);
}

static bool downed_player_exists(const Player *player)
{
    for (int i = 0; i < gbl.downed_player_count; i++) {
        if (gbl.downed_players[i].target == player) {
            return true;
        }
    }
    return false;
}

/* sub_74E6F */
void combatmap_combatant_killed(Player *player)
{
    Point map;
    Point points[COMBATMAP_MAX_DELTAS];
    int count;
    int player_index;
    DaxBlock *attack_icon;
    DaxBlock *normal_icon;

    if (gbl.game_state != GAME_STATE_COMBAT) {
        /* Outside a fight - a trap, or a spell cast on the march - there is no
         * combat map to update, so all that is left is the noise. */
        sound_play(SOUND_5);
        text_game_delay();
        return;
    }

    if (downed_player_exists(player)) {
        return;
    }

    player_index = combatmap_player_index(player);
    map = combatmap_player_map_pos(player);

    if (!combatmap_player_on_screen_p(true, player)) {
        combatmap_redraw_area(8, 3, map);
    }

    combatmap_redraw_player_background(player_index);
    sound_play(SOUND_5);

    /* Icons 24 and 25 are the two halves of the skull, flashed over the body. */
    attack_icon = combat_icon_get(&gbl.combat_icons[24], COMBAT_ICON_ATTACK, 0);
    normal_icon = combat_icon_get(&gbl.combat_icons[25], COMBAT_ICON_NORMAL, 0);

    count = combatmap_build_size_map(gbl.combat_map[player_index].size,
                                     gbl.combat_map[player_index].screen_pos,
                                     points);

    for (int var_3 = 0; var_3 <= 8; var_3++) {
        for (int i = 0; i < count; i++) {
            if (combatmap_coord_on_screen(points[i])) {
                DaxBlock *tmp = ((var_3 & 1) == 0) ? attack_icon : normal_icon;

                draw_overlay_bounded(tmp, 5, 0, points[i].y * 3,
                                     points[i].x * 3);
            }
        }

        /* seg040.DrawOverlay() went here; it does nothing. */
        input_sys_delay(10);
    }

    /* Only the party's own dead leave a body behind to be raised later. */
    if (!player_actions(player)->non_team_member &&
        gbl.map_to_background_tile != NULL) {
        if (gbl.downed_player_count > GBL_MAX_COMBATANT_COUNT) {
            /* The C# list grew without limit; there cannot be more corpses than
             * combatants, so reaching this means one is being added twice. */
            log_warn("combat: no room for another fallen combatant");
        } else {
            DownedPlayerTile *d = &gbl.downed_players[gbl.downed_player_count];

            gbl.downed_player_count++;
            d->original_background_tile =
                ground_tile_map_get(gbl.map_to_background_tile, map);
            d->target = player;
            d->map = map;

            /* A cloud on the square outranks the body: the cloud has to keep
             * being drawn, and the body is remembered either way. */
            if (ground_tile_map_get(gbl.map_to_background_tile, map) !=
                TILE_STINKING_CLOUD) {
                ground_tile_map_set(gbl.map_to_background_tile, map,
                                    TILE_DOWN_PLAYER);
            }
        }
    }

    text_game_delay();
    combatmap_redraw_player_background(player_index);

    gbl.combat_map[combatmap_player_index(player)].size = 0;

    combatmap_setup_player_index();

    combatmap_redraw_area(8, 3, point_add(screen_top_left(),
                                          point_screen_center()));

    {
        Action *actions = player_actions(player);

        actions->delay = 0;
        actions->move = 0;
        actions->spell_id = 0;
        actions->guarding = false;
    }
}

/* sub_7515A */
bool combatmap_place_combatant(bool arg_0, Point pos, Player *player)
{
    int player_index;
    int ground_tile;
    int player_idx;

    if (gbl.game_state != GAME_STATE_COMBAT) {
        /* Nothing to place outside a fight, and the caller reads that as "the
         * move worked". */
        return true;
    }

    player_index = combatmap_player_index(player);

    gbl.combat_map[player_index].size = player->field_DE & 0x7f;
    gbl.combat_map[player_index].pos = pos;

    combatmap_ground_information(&ground_tile, &player_idx, 8, player);

    if (player_idx != 0 || ground_tile == 0 ||
        move_cost_of(ground_tile) == 0xff) {
        /* Somebody is already there, the square is off the map, or nothing can
         * walk on it: the combatant does not go on the map at all. */
        gbl.combat_map[player_index].size = 0;
        return false;
    }

    if (arg_0 && !player_actions(player)->non_team_member &&
        gbl.map_to_background_tile != NULL) {
        int downed_tile = -1;

        /* The last body of this character that was not itself lying on a corpse
         * tile: that is the ground the square really had. */
        for (int i = gbl.downed_player_count - 1; i >= 0; i--) {
            if (gbl.downed_players[i].target == player &&
                gbl.downed_players[i].original_background_tile !=
                    TILE_DOWN_PLAYER) {
                downed_tile = gbl.downed_players[i].original_background_tile;
                break;
            }
        }
        if (downed_tile >= 0) {
            ground_tile = downed_tile;
        }

        /* RemoveAll(cell => cell.target == player), keeping the order of the
         * rest, which is what the List<> did. */
        {
            int kept = 0;

            for (int i = 0; i < gbl.downed_player_count; i++) {
                if (gbl.downed_players[i].target != player) {
                    if (kept != i) {
                        gbl.downed_players[kept] = gbl.downed_players[i];
                    }
                    kept++;
                }
            }
            for (int i = kept; i < gbl.downed_player_count; i++) {
                downed_player_tile_clear(&gbl.downed_players[i]);
            }
            gbl.downed_player_count = kept;
        }

        /* Somebody else's body is still on the square, so the corpse tile stays. */
        {
            bool found = false;

            for (int i = 0; i < gbl.downed_player_count; i++) {
                if (gbl.downed_players[i].target != NULL &&
                    point_eq(gbl.downed_players[i].map, pos)) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                ground_tile_map_set(gbl.map_to_background_tile, pos, ground_tile);
            }
        }
    }

    combatmap_setup_player_index();

    return true;
}

/* sub_75356 */
void combatmap_redraw_if_focus_on(bool draw_cursor, int radius, Player *player)
{
    if (!has_ground_map("redrawing the combat area")) {
        return;
    }

    gbl.map_to_background_tile->draw_target_cursor = draw_cursor;
    gbl.map_to_background_tile->size =
        gbl.combat_map[combatmap_player_index(player)].size;

    if (gbl.focus_combat_area_on_player) {
        combatmap_redraw_area(8, radius, combatmap_player_map_pos(player));
    }

    gbl.map_to_background_tile->draw_target_cursor = false;
    gbl.map_to_background_tile->size = 1;
}
