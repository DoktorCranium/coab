/* view3d.c - Ported from engine/ovr031.cs and engine/ovr029.cs. */
#include "view3d.h"

#include "cheats.h"
#include "dax.h"
#include "display.h"
#include "draw.h"
#include "frames.h"
#include "gbl.h"
#include "log.h"
#include "picture.h"
#include "quit.h"

#include <stdio.h>
#include <stdlib.h>

/* Where each of the ten wall slots goes on screen, in cells inside the view.
 * word_16E08..word_16E1A and byte_16E1C..byte_16E2E. */
#define COLUMN_A 5
#define COLUMN_B 4
#define COLUMN_C 6
#define COLUMN_D 4
#define COLUMN_E 2
#define COLUMN_F 7
#define COLUMN_G 2
#define COLUMN_H 0
#define COLUMN_I 9
#define COLUMN_J 5
#define ROW_A 4
#define ROW_B 3
#define ROW_C 3
#define ROW_D 3
#define ROW_E 1
#define ROW_F 1
#define ROW_G 1
#define ROW_H 0
#define ROW_I 0
#define ROW_J 4

#define MAP_SIZE GEO_MAP_DIM   /* 16x16, so 0..15 */

/* ovr029.sky_colours, unk_16D9A. The area record stores an index into this
 * rather than a colour, and the table repeats after eight entries. */
static const int sky_colours[16] = {
    0x00, 0x0f, 0x04, 0x0b, 0x0d, 0x02, 0x09, 0x0e,
    0x00, 0x0f, 0x04, 0x0b, 0x0d, 0x02, 0x09, 0x0e
};

/* sub_7100F */
void view3d_draw_area_map(int party_dir, int party_map_y, int party_map_x)
{
    const int display_width = 11;
    const int half_display_width = display_width / 2;
    const int display_offset = 2;

    int offset_x = party_map_x - half_display_width;
    int offset_y = party_map_y - half_display_width;
    int party_screen_y;
    int party_screen_x;

    offset_x = COAB_MAX(offset_x, 0);
    offset_x = COAB_MIN(offset_x, half_display_width);

    offset_y = COAB_MAX(offset_y, 0);
    offset_y = COAB_MIN(offset_y, half_display_width);

    for (int y = 0; y < display_width; y++) {
        int map_y = y + offset_y;

        for (int x = 0; x < display_width; x++) {
            int map_x = x + offset_x;

            /* The 0x104 family: one symbol per combination of the four walls,
             * so the bits are added up rather than looked up. */
            int symbol_id = 0x104;
            int door_id = 0x104;
            MapInfo *mi = view3d_map_info(map_y, map_x);

            if (mi != NULL) {
                if (mi->wall_type_dir_0 > 0) symbol_id += 1;
                if (mi->wall_type_dir_2 > 0) symbol_id += 2;
                if (mi->wall_type_dir_4 > 0) symbol_id += 4;
                if (mi->wall_type_dir_6 > 0) symbol_id += 8;

                if (mi->x3_dir_0 > 0) door_id += 1;
                if (mi->x3_dir_2 > 0) door_id += 2;
                if (mi->x3_dir_4 > 0) door_id += 4;
                if (mi->x3_dir_6 > 0) door_id += 8;
            }

            frames_put_symbol(0, true, symbol_id, y + display_offset,
                              x + display_offset);

            if (cheats.improved_area_map) {
                /* Not in the original: draws the doors over the walls in a
                 * second colour, which the plain map cannot show. */
                draw_clipped_nodraw(8);
                draw_clipped_recolor(7, 1);
                frames_put_symbol(0, true, door_id, y + display_offset,
                                  x + display_offset);
                draw_clipped_recolor(17, 17);
                draw_clipped_nodraw(17);
            }
        }
    }

    /* The map is drawn transposed: the party's x is its screen row. */
    party_screen_y = party_map_x - offset_x;
    party_screen_x = party_map_y - offset_y;

    draw_clipped_nodraw(8);
    frames_put_symbol(0, true, (party_dir >> 1) + 0x100,
                      party_screen_x + display_offset,
                      party_screen_y + display_offset);
    draw_clipped_nodraw(17);
    /* seg040.DrawOverlay() went here; it does nothing. */
}

void view3d_draw_background(void)
{
    draw_color_block(gbl.sky_colour, 0x2c, 11, 16, 2);
    draw_color_block(0, 2, 11, 0x3c, 2);
    draw_color_block(8, 0x2a, 11, 0x3e, 2);

    /* Note the arguments: the original passes mapPosY for both coordinates, so
     * the square tested for a roof is (y,y) rather than the party's. Kept as it
     * is - the sun appears on the wrong squares in the original too. */
    if (view3d_get_wall_x2(gbl.map_pos_y, gbl.map_pos_y) < 0x80 &&
        gbl.sky_colour == 11) {
        int col_x = 2;
        int row_y = 2;
        int hour = (gbl.area_ptr != NULL) ? (int)gbl.area_ptr->time_hour : 0;

        /* The sun rises in the east and sets in the west, so it is only visible
         * when the party happens to be facing that way. */
        if (hour >= 1 && hour <= 5) {
            if (gbl.map_direction == 2) {
                draw_overlay_bounded(gbl.sky_dax_251, 1, 0, (row_y + 5) - hour,
                                     12 - 3);
            } else if (gbl.map_direction == 4 && hour > 2) {
                draw_overlay_bounded(gbl.sky_dax_251, 1, 0, (row_y + 5) - hour,
                                     (col_x + hour) - 3);
            }
        } else if (hour >= 13 && hour <= 18) {
            if (gbl.map_direction == 6) {
                draw_overlay_bounded(gbl.sky_dax_251, 1, 0, (row_y + hour) - 13,
                                     col_x);
            } else if (gbl.map_direction == 4 && hour >= 16) {
                draw_overlay_bounded(gbl.sky_dax_251, 1, 0, (row_y + hour) - 13,
                                     (col_x + hour) - 8);
            }
        }

        if (gbl.map_direction == 0) {
            draw_overlay_bounded(gbl.sky_dax_250, 1, 0, row_y, col_x);
        }
    }

    draw_overlay_bounded(gbl.sky_dax_252, 1, 0, 7, 2);
}

/* seg600:0ADA idxOffset, 0AE4 colCount, 0AEE rowCount. Ten wall slots; the
 * eleventh idxOffset entry is never reached. The runs are laid out end to end,
 * 156 ids in all, which is exactly one WALLDEF row. */
static const u8 idx_offset[11] = { 0, 2, 6, 10, 22, 38, 54, 110, 132, 154, 1 };
static const int col_count[10] = { 1, 1, 1, 3, 2, 2, 7, 2, 2, 1 };
static const int row_count[10] = { 2, 4, 4, 4, 8, 8, 8, 11, 11, 2 };

/* sub_71434 */
void view3d_draw_8x8_tiles(int offset_index, int wall_type, int row_start,
                           int col_start)
{
    int idx;
    int col_max;
    int row_max;
    int wallset;
    int slice;

    if (offset_index < 0 || offset_index >= 10) {
        /* The C# indexed its tables and would have thrown. */
        log_warn("view3d: no wall slot %d", offset_index);
        return;
    }

    idx = idx_offset[offset_index];
    col_max = col_count[offset_index] + col_start;
    row_max = row_count[offset_index] + row_start;

    /* A wall type is a set and a slice within it: five slices per set, and the
     * type is numbered from one. */
    wallset = (wall_type - 1) / 5;
    slice = (wall_type - 1) % 5;

    for (int row_y = row_start; row_y < row_max; row_y++) {
        for (int col_x = col_start; col_x < col_max; col_x++) {
            /* wall_defs_id numbers its sets from one and reports a bad set
             * rather than throwing; a wall type above 15 lands there. */
            int symbol_id = wall_defs_id(&gbl.wall_def, wallset + 1, slice, idx);

            /* Off the edge of the view the id is skipped but idx still moves on,
             * so the run stays in step with the screen. */
            if (row_y >= 0 && row_y <= 10 && col_x >= 0 && col_x <= 10 &&
                symbol_id > 0) {
                frames_put_symbol(1, true, symbol_id, row_y + 2, col_x + 2);

                display_update();
            }

            idx++;
        }
    }
}

/* sub_71542. The second test is on mapX where the original meant mapY; kept, so
 * that a negative y is treated as on the map exactly as it was. */
bool view3d_map_coord_is_valid(int map_y, int map_x)
{
    return map_x < MAP_SIZE && map_x >= 0 && map_y < MAP_SIZE && map_x >= 0;
}

/* Off the map the view wraps around, except in ECL blocks 0 and 10 - the two
 * that are not a closed map - where there is simply nothing there. */
static bool coord_off_open_map(int map_y, int map_x)
{
    return !view3d_map_coord_is_valid(map_y, map_x) &&
           (gbl.ecl_block_id == 0 || gbl.ecl_block_id == 10);
}

/* sub_71573 */
u8 view3d_wall_door_flags_get(int map_dir, int map_y, int map_x)
{
    MapInfo *mi;
    u8 var_1 = 1;

    if (coord_off_open_map(map_y, map_x)) {
        return 0;
    }
    if (gbl.geo_ptr == NULL) {
        return 0;
    }

    map_x = sys_wrap_min_max(map_x, 0, 15);
    map_y = sys_wrap_min_max(map_y, 0, 15);

    mi = &gbl.geo_ptr->maps[map_y][map_x];

    switch (map_dir) {
    case 6:
        if (mi->wall_type_dir_6 != 0) {
            var_1 = mi->x3_dir_6;
        }
        break;

    case 4:
        if (mi->wall_type_dir_4 != 0) {
            var_1 = mi->x3_dir_4;
        }
        break;

    case 2:
        if (mi->wall_type_dir_2 != 0) {
            var_1 = mi->x3_dir_2;
        }
        break;

    case 0:
        if (mi->wall_type_dir_0 != 0) {
            var_1 = mi->x3_dir_0;
        }
        break;

    default:
        break;
    }

    return var_1;
}

u8 view3d_map_wall_type(int direction, int map_y, int map_x)
{
    u8 result = 0;
    MapInfo *mi = view3d_map_info(map_y, map_x);

    if (mi != NULL) {
        switch (direction) {
        case 0: result = mi->wall_type_dir_0; break;
        case 2: result = mi->wall_type_dir_2; break;
        case 4: result = mi->wall_type_dir_4; break;
        case 6: result = mi->wall_type_dir_6; break;
        default: break;
        }
    }

    return result;
}

MapInfo *view3d_map_info(int map_y, int map_x)
{
    if (coord_off_open_map(map_y, map_x) || gbl.geo_ptr == NULL) {
        return NULL;
    }

    /* One step off an edge comes out at the opposite one. Anything further out
     * than that is clamped to the same square, as the original was. */
    if (map_x > 0x0f) {
        map_x = 0;
    } else if (map_x < 0) {
        map_x = 0x0f;
    }

    if (map_y > 0x0f) {
        map_y = 0;
    } else if (map_y < 0) {
        map_y = 0x0f;
    }

    return &gbl.geo_ptr->maps[map_y][map_x];
}

/* sub_717A5 */
u8 view3d_get_wall_x2(int map_y, int map_x)
{
    if (coord_off_open_map(map_y, map_x) || gbl.geo_ptr == NULL) {
        return 0;
    }

    /* Two ifs rather than an if/else here, unlike getMap_XXX: an x of 16 is
     * turned into 0 and then left alone, which comes to the same thing. */
    if (map_x > 0x0f) {
        map_x = 0;
    }
    if (map_x < 0) {
        map_x = 0x0f;
    }

    if (map_y > 0x0f) {
        map_y = 0;
    }
    if (map_y < 0) {
        map_y = 0x0f;
    }

    return gbl.geo_ptr->maps[map_y][map_x].x2;
}

/* The far row: four squares to each side of the one two steps ahead. Slot A is
 * the wall facing the party and slot J the corner between two of them, which is
 * why a wall is remembered (var_17) until the next square is looked at. */
static void draw_world_far(u8 party_dir, int dir_left, int dir_right,
                           int draw_x, int draw_y)
{
    int tmp_x;
    int tmp_y;
    int var_10;
    int col;
    u8 var_17;

    tmp_x = draw_x;
    tmp_y = draw_y;
    var_10 = 0;
    col = 0;
    var_17 = 0;

    while (var_10 < 4) {
        u8 var_14 = view3d_map_wall_type(party_dir, tmp_y, tmp_x);

        if (!view3d_map_coord_is_valid(tmp_y, tmp_x) &&
            view3d_map_wall_type(dir_right, tmp_y, tmp_x) == 0) {
            var_17 = 0;
        }

        if (var_14 != 0) {
            if (var_17 > 0) {
                view3d_draw_8x8_tiles(9, var_17, ROW_J, COLUMN_J + col + 1);
            }

            var_17 = var_14;

            view3d_draw_8x8_tiles(0, var_14, ROW_A, COLUMN_A + col);
        } else {
            if (var_17 > 0 &&
                view3d_map_wall_type(dir_left,
                                     tmp_y - GBL_MAP_DIR_Y_DELTA[dir_left],
                                     tmp_x - GBL_MAP_DIR_X_DELTA[dir_left]) != 0) {
                view3d_draw_8x8_tiles(9, var_17, ROW_J, COLUMN_J + col + 1);
            }

            var_17 = 0;
        }

        var_10++;
        col -= 2;

        tmp_x += GBL_MAP_DIR_X_DELTA[dir_left];
        tmp_y += GBL_MAP_DIR_Y_DELTA[dir_left];
    }

    tmp_x = draw_x;
    tmp_y = draw_y;
    var_10 = 0;
    col = 0;
    var_17 = 0;

    while (var_10 < 4) {
        u8 var_14 = view3d_map_wall_type(party_dir, tmp_y, tmp_x);

        if (!view3d_map_coord_is_valid(tmp_y, tmp_x) &&
            view3d_map_wall_type(dir_left, tmp_y, tmp_x) == 0) {
            var_17 = 0;
        }

        if (var_14 != 0) {
            if (var_17 > 0) {
                view3d_draw_8x8_tiles(9, var_17, ROW_J, COLUMN_J + col - 1);
            }

            var_17 = var_14;
            view3d_draw_8x8_tiles(0, var_14, ROW_A, COLUMN_A + col);
        } else {
            if (var_17 > 0 &&
                view3d_map_wall_type(dir_right,
                                     tmp_y - GBL_MAP_DIR_Y_DELTA[dir_right],
                                     tmp_x - GBL_MAP_DIR_X_DELTA[dir_right]) != 0) {
                view3d_draw_8x8_tiles(9, var_17, ROW_J, COLUMN_J + col - 1);
            }

            var_17 = 0;
        }

        var_10++;
        col += 2;

        tmp_x += GBL_MAP_DIR_X_DELTA[dir_right];
        tmp_y += GBL_MAP_DIR_Y_DELTA[dir_right];
    }

    /* The side walls of the same row, three squares out each way. */
    tmp_x = draw_x;
    tmp_y = draw_y;
    var_10 = 0;
    col = 0;

    while (var_10 < 3) {
        u8 var_15 = view3d_map_wall_type(dir_left, tmp_y, tmp_x);

        if (var_15 != 0) {
            if (var_10 == 0) {
                view3d_draw_8x8_tiles(1, var_15, ROW_B, COLUMN_B + col);
            } else {
                view3d_draw_8x8_tiles(1, var_15, ROW_B, COLUMN_B + col - 1);
            }
        }

        var_10++;
        col -= 2;

        tmp_x += GBL_MAP_DIR_X_DELTA[dir_left];
        tmp_y += GBL_MAP_DIR_Y_DELTA[dir_left];
    }

    tmp_x = draw_x;
    tmp_y = draw_y;
    var_10 = 0;
    col = 0;

    while (var_10 < 3) {
        u8 var_15 = view3d_map_wall_type(dir_right, tmp_y, tmp_x);

        if (var_15 != 0) {
            if (var_10 == 0) {
                view3d_draw_8x8_tiles(2, var_15, ROW_C, COLUMN_C + col);
            } else {
                view3d_draw_8x8_tiles(2, var_15, ROW_C, COLUMN_C + col + 1);
            }
        }

        var_10++;
        col += 2;

        tmp_x += GBL_MAP_DIR_X_DELTA[dir_right];
        tmp_y += GBL_MAP_DIR_Y_DELTA[dir_right];
    }
}

/* The middle row: three squares each side, walked from the outside in. */
static void draw_world_mid(u8 party_dir, int dir_left, int dir_right,
                           int var_5, int var_7)
{
    int tmp_x = GBL_MAP_DIR_X_DELTA[dir_left] + var_5 + GBL_MAP_DIR_X_DELTA[dir_left];
    int tmp_y = GBL_MAP_DIR_Y_DELTA[dir_left] + var_7 + GBL_MAP_DIR_Y_DELTA[dir_left];
    int var_10 = 0;
    int var_12 = -6;

    while (var_10 < 3) {
        u8 var_14 = view3d_map_wall_type(party_dir, tmp_y, tmp_x);
        u8 var_15;

        if (var_14 != 0) {
            view3d_draw_8x8_tiles(3, var_14, ROW_D, COLUMN_D + var_12);
        }

        var_15 = view3d_map_wall_type(dir_left, tmp_y, tmp_x);

        if (var_15 != 0) {
            view3d_draw_8x8_tiles(4, var_15, ROW_E, COLUMN_E + var_12);
        }

        var_10++;
        var_12 += 3;
        tmp_x += GBL_MAP_DIR_X_DELTA[dir_right];
        tmp_y += GBL_MAP_DIR_Y_DELTA[dir_right];
    }

    tmp_x = GBL_MAP_DIR_X_DELTA[dir_right] + GBL_MAP_DIR_X_DELTA[dir_right] + var_5;
    tmp_y = GBL_MAP_DIR_Y_DELTA[dir_right] + GBL_MAP_DIR_Y_DELTA[dir_right] + var_7;
    var_10 = 0;
    var_12 = 6;

    while (var_10 < 3) {
        u8 var_14 = view3d_map_wall_type(party_dir, tmp_y, tmp_x);
        u8 var_15;

        if (var_14 != 0) {
            view3d_draw_8x8_tiles(3, var_14, ROW_D, COLUMN_D + var_12);
        }

        var_15 = view3d_map_wall_type(dir_right, tmp_y, tmp_x);

        if (var_15 != 0) {
            view3d_draw_8x8_tiles(5, var_15, ROW_F, COLUMN_F + var_12);
        }

        var_10++;
        var_12 -= 3;

        tmp_x += GBL_MAP_DIR_X_DELTA[dir_left];
        tmp_y += GBL_MAP_DIR_Y_DELTA[dir_left];
    }
}

/* The near row: the two squares either side of the party, which fill most of
 * the view. */
static void draw_world_near(u8 party_dir, int dir_left, int dir_right,
                            int var_5, int var_7)
{
    int tmp_x = GBL_MAP_DIR_X_DELTA[dir_left] + var_5;
    int tmp_y = GBL_MAP_DIR_Y_DELTA[dir_left] + var_7;
    int var_10 = 0;
    int var_12 = -7;

    while (var_10 < 2) {
        u8 var_14 = view3d_map_wall_type(party_dir, tmp_y, tmp_x);
        u8 var_15;

        if (var_14 != 0) {
            view3d_draw_8x8_tiles(6, var_14, ROW_G, COLUMN_G + var_12);
        }

        var_15 = view3d_map_wall_type(dir_left, tmp_y, tmp_x);

        if (var_15 != 0) {
            view3d_draw_8x8_tiles(7, var_15, ROW_H, COLUMN_H + var_12);
        }

        var_10++;

        var_12 += 7;
        tmp_x += GBL_MAP_DIR_X_DELTA[dir_right];
        tmp_y += GBL_MAP_DIR_Y_DELTA[dir_right];
    }

    tmp_x = var_5 + GBL_MAP_DIR_X_DELTA[dir_right];
    tmp_y = var_7 + GBL_MAP_DIR_Y_DELTA[dir_right];
    var_10 = 0;
    var_12 = 7;

    while (var_10 < 2) {
        u8 var_14 = view3d_map_wall_type(party_dir, tmp_y, tmp_x);
        u8 var_15;

        if (var_14 != 0) {
            view3d_draw_8x8_tiles(6, var_14, ROW_G, var_12 + COLUMN_G);
        }

        var_15 = view3d_map_wall_type(dir_right, tmp_y, tmp_x);

        if (var_15 != 0) {
            view3d_draw_8x8_tiles(8, var_15, ROW_I, var_12 + COLUMN_I);
        }

        var_10++;
        var_12 -= 7;

        tmp_x += GBL_MAP_DIR_X_DELTA[dir_left];
        tmp_y += GBL_MAP_DIR_Y_DELTA[dir_left];
    }
}

/* sub_71820 */
void view3d_draw_world(u8 party_dir, int party_pos_y, int party_pos_x)
{
    /* One update at the end rather than one per tile: the original wrote
     * straight to video memory and the tiles appeared as they were drawn. */
    display_update_stop();

    if (gbl.map_area_display) {
        view3d_draw_area_map(party_dir, party_pos_y, party_pos_x);
    } else {
        int dir_left;
        int dir_behind;
        int dir_right;
        int draw_step = 2;
        int draw_x;
        int draw_y;

        view3d_draw_background();

        dir_left = (party_dir + 6) % 8;
        dir_behind = (party_dir + 4) % 8;
        dir_right = (party_dir + 2) % 8;

        /* Start two squares ahead and walk back towards the party, so that the
         * near walls are drawn over the far ones. */
        draw_x = party_pos_x + (draw_step * GBL_MAP_DIR_X_DELTA[party_dir]);
        draw_y = party_pos_y + (draw_step * GBL_MAP_DIR_Y_DELTA[party_dir]);

        do {
            switch (draw_step) {
            case 2:
                draw_world_far(party_dir, dir_left, dir_right, draw_x, draw_y);
                break;

            case 1:
                draw_world_mid(party_dir, dir_left, dir_right, draw_x, draw_y);
                break;

            case 0:
                draw_world_near(party_dir, dir_left, dir_right, draw_x, draw_y);
                break;

            default:
                break;
            }

            draw_x += GBL_MAP_DIR_X_DELTA[dir_behind];
            draw_y += GBL_MAP_DIR_Y_DELTA[dir_behind];

            draw_step -= 1;
        } while (draw_step >= 0);
    }

    display_update_start();
    /* seg040.DrawOverlay() went here; it does nothing. */
}

void view3d_load_walldef(int symbol_set, int block_id)
{
    char file_name[32];
    u8 *data;
    i16 decode_size = 0;
    int var_a;
    int block_count;

    if (symbol_set < 1 || symbol_set >= 4) {
        return;
    }

    snprintf(file_name, sizeof(file_name), "WALLDEF%u.dax",
             (unsigned)gbl.game_area);

    data = dax_load_decode(file_name, block_id, &decode_size);

    if (data == NULL || decode_size == 0 ||
        ((decode_size / 0x30c) + symbol_set) > 4) {
        free(data);
        game_log_and_exit("Unable to load %d from WALLDEF%u.", block_id,
                          (unsigned)gbl.game_area);
    }

    /* Each set draws from its own 8x8 bank, so the ids in the block have to be
     * moved to where that bank starts. */
    var_a = GBL_SYMBOL_SET_FIX[symbol_set] - GBL_SYMBOL_SET_FIX[1];

    wall_defs_load(&gbl.wall_def, symbol_set, data, (size_t)decode_size);

    block_count = decode_size / 0x30c;

    for (int block = 0; block < block_count; block++) {
        int idx = symbol_set + block;

        if (idx >= 1 && idx <= 3) {
            gbl.set_blocks[idx - 1].set_id = -1;
            gbl.set_blocks[idx - 1].block_id = -1;

            wall_defs_block_offset(&gbl.wall_def, idx, var_a);

            /* A WALLDEF block holding several sets has its tile banks numbered
             * blockId*10 + n, one per set. */
            if (block_count > 1) {
                frames_load_8x8d(idx, (block_id * 10) + block + 1);
            } else {
                frames_load_8x8d(idx, block_id);
            }
        }
    }

    gbl.set_blocks[symbol_set - 1].block_id = block_id;
    gbl.set_blocks[symbol_set - 1].set_id = symbol_set;

    free(data);
}

void view3d_load_3d_map(int block_id)
{
    char file_name[32];
    u8 *data;
    i16 bytes_read = 0;

    snprintf(file_name, sizeof(file_name), "GEO%u.dax", (unsigned)gbl.game_area);

    data = dax_load_decode(file_name, block_id, &bytes_read);

    /* 0x402 is the two byte header plus the 16x16 grid; anything else is not a
     * map and there is nothing to fall back on. */
    if (data == NULL || bytes_read == 0 || bytes_read != 0x402) {
        free(data);
        game_log_and_exit("Unable to load geo in Load3DMap.");
    }

    if (gbl.geo_ptr != NULL) {
        geo_block_load(gbl.geo_ptr, data, (size_t)bytes_read);
    }

    if (gbl.area_ptr != NULL) {
        gbl.area_ptr->current_3d_map_block_id = (u8)block_id;
    }

    free(data);
}

/* sub_6F0BA */
void view3d_redraw(void)
{
    if (gbl.last_dax_block_id == 0x50) {
        /* A city close-up is on screen; the wilderness map behind it must not be
         * drawn back over it. */
        gbl.can_draw_bigpic = false;
    }

    if (gbl.party_killed) {
        return;
    }

    if (gbl.area_ptr != NULL && gbl.area_ptr->in_dungeon != 0) {
        int sky_index;

        gbl.map_wall_roof = view3d_get_wall_x2(gbl.map_pos_y, gbl.map_pos_x);

        /* Over 0x7f means the square has a roof, so the "sky" is the ceiling
         * colour instead. */
        sky_index = (gbl.map_wall_roof > 0x7f)
                        ? (int)gbl.area_ptr->indoor_sky_colour
                        : (int)gbl.area_ptr->outdoor_sky_colour;

        if (sky_index < 0 || sky_index >= (int)COAB_ARRAY_LEN(sky_colours)) {
            /* The C# indexed a sixteen entry table and would have thrown. */
            log_warn("view3d: no sky colour %d", sky_index);
            sky_index = 0;
        }
        gbl.sky_colour = sky_colours[sky_index];

        if (gbl.area_ptr->block_area_view != 0 && !cheats.always_show_areamap) {
            /* Some areas do not allow the overhead map at all. */
            gbl.map_area_display = false;
        }

        view3d_draw_world(gbl.map_direction, gbl.map_pos_y, gbl.map_pos_x);
    } else if (gbl.can_draw_bigpic) {
        picture_draw_bigpic();
    }

    gbl.can_draw_bigpic = false;
}

bool view3d_load_sky(void)
{
    /* engine/seg001.cs InitFirst: colour 13 is the transparent one in these. */
    dax_block_free(gbl.sky_dax_250);
    dax_block_free(gbl.sky_dax_251);
    dax_block_free(gbl.sky_dax_252);

    gbl.sky_dax_250 = draw_load_dax(13, 1, 250, "SKY");
    gbl.sky_dax_251 = draw_load_dax(13, 1, 251, "SKY");
    gbl.sky_dax_252 = draw_load_dax(13, 1, 252, "SKY");

    return gbl.sky_dax_250 != NULL && gbl.sky_dax_251 != NULL &&
           gbl.sky_dax_252 != NULL;
}
