/* battlesetup.c - Ported from engine/ovr011.cs. See battlesetup.h. */

#include "battlesetup.h"

#include "affect.h"
#include "attack.h"
#include "character.h"
#include "combat.h"
#include "combatmap.h"
#include "dax.h"
#include "effect.h"
#include "enums.h"
#include "gbl.h"
#include "icons.h"
#include "input.h"
#include "log.h"
#include "partymenu.h"
#include "picture.h"
#include "point.h"
#include "prompt.h"
#include "text.h"
#include "tile.h"
#include "view3d.h"

/* unk_1AB1C, seg600:480C. Which of a side's placement squares are still free:
 * one flag per square of the 11x6 block, for each of the four squares a side may
 * be anchored on, for each of the two sides. The C# sized the first dimension by
 * hand and called the 2 made up - it is gbl.currentTeam, so two is right. */
static u8 placement_grid[2][4][6][11];

/* Struct_1D1BC. gbl.map_to_background_tile points here while a fight is being
 * fought: the C# built a new one per fight and let its collector deal with the
 * old, so nothing outside this module owns it either way. */
static GroundTileMap combat_ground_tiles;

/* The Actions a fight's combatants use, one per place on the team list. The C#
 * did `new Action()` per combatant; they are scratch state for one fight, so a
 * pool that is handed out in team-list order does the same job without the
 * allocation. partymenu_free_player only forgets its character's pointer, so a
 * combatant turned loose leaves their slot behind unused until the next fight. */
static Action combat_actions[GBL_TEAM_LIST_MAX];

/* unk_1665B and unk_1665F, seg600:034C and seg600:0350. The four cardinal steps
 * on the combat map, which is what the furniture looks round itself with. */
static const int dir_x_offset[4] = { 0, 1, 0, -1 };
static const int dir_y_offset[4] = { -1, 0, 1, 0 };

/* unk_16664, seg600:0354. One byte of terrain flags per city: which of the
 * roads, water and scatter a fight near it is drawn with. */
static const int city_info[] = {
    0x00, 0x18, 0x11, 0x15, 0x01, 0x01, 0x60, 0x14, /* 354 - 35B */
    0x08, 0x01, 0x00, 0x21, 0x71, 0x09, 0x06, 0x04, /* 35C - 363 */
    0x01, 0x09, 0x09, 0x08, 0x59, 0x00, 0x11, 0x11, /* 364 - 36B */
    0x00, 0x00, 0x01, 0x11, 0x00, 0x00, 0x20, 0x20, /* 36C - 373 */
    0x0a
};

/* unk_165EC and unk_165FC, seg600:02DC and seg600:02EC. Where a side that will
 * not fit where it stands is walked to next, and which way it faces once it is
 * there: one row per facing, one column per attempt. */
static const int direction_165EC[4][4] = {
    { 8, 4, 6, 2 }, { 8, 6, 4, 0 }, { 8, 0, 6, 2 }, { 8, 2, 0, 4 }
};
static const int direction_165FC[4][4] = {
    { 0, 0, 2, 6 }, { 2, 2, 0, 4 }, { 4, 4, 2, 6 }, { 6, 6, 4, 0 }
};

/* unk_1660C, seg600:02FC. A quarter-turn facing as one of the eight isometric
 * directions the combat map is drawn in. */
static const int half_dir_to_iso[4] = { 7, 2, 3, 6 };

/* unk_16610 and unk_16618, seg600:0300 and seg600:0308. Where the middle of a
 * side's first row sits, by facing: the first four entries for the square the
 * side started on and the second four for a square it was walked to. */
static const u8 first_row_x[8] = { 5, 4, 5, 6, 3, 8, 7, 2 };
static const u8 first_row_y[8] = { 3, 2, 2, 3, 0, 2, 5, 3 };

/* unk_16620, seg600:0310. The first and last column each of the six rows may use,
 * by facing - row 4 being the one used for the second placement attempt, which
 * is the widest. */
static const u8 row_column_limits[5][6][2] = {
    { {1,0}, {1,0}, {1,0}, {2,9},  {3,10}, {4,10} }, /* 310 - 31B */
    { {0,2}, {0,3}, {1,4}, {2,5},  {3,6},  {4,7}  }, /* 31C - 327 */
    { {0,6}, {0,7}, {1,8}, {1,0},  {1,0},  {1,0}  }, /* 328 - 333 */
    { {3,6}, {4,7}, {5,8}, {6,9},  {7,10}, {8,10} }, /* 334 - 33F */
    { {0,6}, {0,7}, {1,8}, {2,9},  {3,10}, {4,10} }, /* 340 - 34B */
};

/* The block a side is placed in: eleven squares across by six deep. */
static const int min_placement_column = 0;
static const int max_placement_column = 10;
static const int min_placement_row    = 0;
static const int max_placement_row    = 5;

/* ------------------------------------------------------------- the floor */

/* gbl.BackGroundTiles[id].tile_index, which is what the floor-building code
 * compares against. A tile id past the end of the table reads as 0 rather than
 * throwing; background_tile logs it. */
static int tile_index_of(int ground_tile)
{
    const BackgroundTile *tile = background_tile(ground_tile);

    return tile != NULL ? tile->tile_index : 0;
}

/* gbl.mapToBackGroundTile[x, y]. The floor is always the one this module just
 * gave gbl - SetupGroundTiles builds it before anything reads it - so a missing
 * one means the floor code has been reached out of order; it is worth one line in
 * the log and not a line per square. */
static bool no_floor(void)
{
    static bool reported = false;

    if (gbl.map_to_background_tile != NULL) {
        return false;
    }

    if (!reported) {
        reported = true;
        log_warn("battle setup: there is no combat floor to build on");
    }

    return true;
}

static int ground_tile_at(int x, int y)
{
    if (no_floor()) {
        return 0;
    }

    return ground_tile_map_get(gbl.map_to_background_tile, point_make(x, y));
}

static void ground_tile_put(int x, int y, int value)
{
    if (no_floor()) {
        return;
    }

    ground_tile_map_set(gbl.map_to_background_tile, point_make(x, y), value);
}

/* ovr011.set_background_tile, sub_37046 */
void battlesetup_set_background_tile(int tile_id, int y, int x)
{
    int tmp_x = (gbl.byte_1AD34 * 6) + (gbl.byte_1AD35 * 5) + 21 + x;
    int tmp_y = (gbl.byte_1AD35 * 5) + 10 + y;

    if (tmp_x >= 0 && tmp_x <= 0x31 &&
        tmp_y >= 0 && tmp_y <= 0x18) {
        /* tile_id + 1: see the note in battlesetup.h. */
        ground_tile_put(tmp_x, tmp_y, tile_id + 1);
    }
}

/* ovr011.sub_370D3 */
void battlesetup_place_furniture(void)
{
    bool is_room;

    /* A square with walls on both of a facing pair and not on the other pair is
     * a corridor, and a doorway on any side is a way through: neither gets
     * furniture. Only a square walled all round, or not at all, does. */
    if (gbl.dir_0_flags != 1 && gbl.dir_2_flags != 1 &&
        gbl.dir_4_flags != 1 && gbl.dir_6_flags != 1) {
        is_room = false;
    } else if (gbl.dir_0_flags == 1 && gbl.dir_4_flags == 1 &&
               (gbl.dir_2_flags != 1 || gbl.dir_6_flags != 1)) {
        is_room = false;
    } else if (gbl.dir_2_flags == 1 && gbl.dir_6_flags == 1 &&
               (gbl.dir_0_flags != 1 || gbl.dir_4_flags != 1)) {
        is_room = false;
    } else if (gbl.dir_0_flags == 3 || gbl.dir_2_flags == 3 ||
               gbl.dir_4_flags == 3 || gbl.dir_6_flags == 3) {
        is_room = false;
    } else {
        is_room = true;
    }

    for (int var_1 = 2; var_1 <= 3; var_1++) {
        for (int var_2 = 2; var_2 <= 4; var_2++) {
            /* Both loop variables move the square east, so the six candidates
             * are a slanted band across the middle of the patch. */
            int pos_x = (gbl.byte_1AD34 * 6) + (gbl.byte_1AD35 * 5) + 0x15 +
                        var_1 + var_2;
            int pos_y = (gbl.byte_1AD35 * 5) + 0x0a + var_2;

            if (pos_x < 0 || pos_x > 0x31 || pos_y < 0 || pos_y > 0x18) {
                continue;
            }

            if (tile_index_of(ground_tile_at(pos_x, pos_y)) == 0x16 &&
                gbl.byte_1AD3D != 0 &&
                is_room &&
                effect_roll_dice(10, 1) <= 5) {
                ground_tile_put(pos_x, pos_y, TILE_TABLE);

                for (int var_7 = 0; var_7 < 4; var_7++) {
                    int tmp_x = dir_x_offset[var_7] + pos_x;
                    int tmp_y = dir_y_offset[var_7] + pos_y;

                    if (tmp_x >= 0 && tmp_x <= 0x31 &&
                        tmp_y >= 0 && tmp_y <= 0x18) {
                        if (tile_index_of(ground_tile_at(tmp_x, tmp_y)) == 0x16 &&
                            effect_roll_dice(10, 1) <= 9) {
                            /* The original's own bug, kept: the chair is written
                             * to the table's square rather than to the clear one
                             * next to it that was just tested. So a table nearly
                             * always ends up as a chair instead, and the chairs
                             * round it are never drawn. */
                            ground_tile_put(pos_x, pos_y, TILE_CHAIR);
                        }
                    }
                }
            }
        }
    }
}

/* ovr011.sub_37306 */
int battlesetup_square_side_flags(int dir, int map_y, int map_x)
{
    int flags;

    if (map_x >= 0 && map_x <= 15 &&
        map_y >= 0 && map_y <= 15) {
        if (view3d_wall_door_flags_get(dir, map_y, map_x) == 0) {
            flags = 1;
        } else if (view3d_map_wall_type(dir, map_y, map_x) == 0) {
            flags = 0;
        } else {
            flags = 3;
        }
    } else {
        if (map_y == gbl.map_pos_y && (dir == 2 || dir == 6)) {
            flags = 0;
        } else {
            flags = 1;
        }
    }

    return flags;
}

/* ovr011.get_dir_flags, sub_37388.
 *
 * The C# named this routine's two coordinates the wrong way round - its mapX
 * parameter took the caller's map y - and then passed them on swapped again, so
 * every square it looked at was the right one. The names here are honest; the
 * behaviour is the same. */
int battlesetup_dir_flags(int dir, int map_y, int map_x)
{
    int opposite_dir = (dir + 4) % 8;
    int new_map_x = GBL_MAP_DIR_X_DELTA[dir] + map_x;
    int new_map_y = GBL_MAP_DIR_Y_DELTA[dir] + map_y;

    int this_side = battlesetup_square_side_flags(dir, map_y, map_x);
    int far_side  = battlesetup_square_side_flags(opposite_dir, new_map_y,
                                                 new_map_x);

    return this_side | far_side;
}

/* ovr011.build_background_tiles_1, sub_373FC */
void battlesetup_build_tiles_1(void)
{
    for (int y_pos = 2; y_pos <= 4; y_pos++) {
        for (int x_pos = 0; x_pos <= 5; x_pos++) {
            battlesetup_set_background_tile(22, y_pos, x_pos);
        }
    }

    if (gbl.dir_6_flags == 1) {
        /* The west wall runs diagonally across the patch, a square further east
         * on each row down. */
        for (int var_2 = 2; var_2 <= 4; var_2++) {
            battlesetup_set_background_tile(4, var_2, var_2 - 1);
            battlesetup_set_background_tile(3, var_2, var_2);
            battlesetup_set_background_tile(13, var_2, var_2 + 1);
        }
    } else if (gbl.dir_6_flags == 3) {
        battlesetup_set_background_tile(8, 2, 1);
        battlesetup_set_background_tile(0, 4, 5);
    }
}

/* ovr011.build_background_tiles_2, sub_374A1 */
void battlesetup_build_tiles_2(void)
{
    if (gbl.dir_0_flags == 1) {
        battlesetup_set_background_tile(5, 0, 3);
        battlesetup_set_background_tile(5, 0, 4);
        battlesetup_set_background_tile(10, 1, 3);
        battlesetup_set_background_tile(10, 1, 4);
    } else {
        battlesetup_set_background_tile(22, 0, 3);
        battlesetup_set_background_tile(22, 0, 4);
        battlesetup_set_background_tile(22, 1, 3);
        battlesetup_set_background_tile(22, 1, 4);
    }
}

/* ovr011.build_backgrould_tiles_3, sub_3751E - the C#'s own spelling.
 *
 * The C# wrapped the four cases in a `for (var_1 = 1; var_1 <= 4; var_1++)` and
 * branched on var_1 inside it, which is what the decompiler made of four blocks
 * one after another. They are independent, so they are written out here. */
void battlesetup_build_tiles_3(int map_y, int map_x)
{
    /* The four tiles keep the C#'s zero when no case matches. Given the flags
     * can only be 0, 1 or 3, none of them can be reached; the initialiser is the
     * C#'s and is kept so the tiles are never read uninitialised. */
    u8 corner   = 0;   /* var_2 */
    u8 north    = 0;   /* var_4 */
    u8 west     = 0;   /* var_3 */
    u8 inside   = 0;   /* var_5 */

    /* Whether the two squares beyond the corner are both open, which is what
     * decides between the outside and the inside of a corner piece. */
    bool corner_open = (battlesetup_dir_flags(6, map_y - 1, map_x) == 0 &&
                        battlesetup_dir_flags(0, map_y, map_x - 1) == 0);

    /* case 1: */
    if (gbl.dir_0_flags == 0) {
        switch (gbl.dir_6_flags) {
        case 0:
            corner = 0x16;
            break;

        case 3:
            corner = 0x0d;
            break;

        case 1:
            corner = corner_open ? 0 : 0x0d;
            break;
        }
    } else if (gbl.dir_0_flags == 3 || gbl.dir_0_flags == 1) {
        if (gbl.dir_6_flags == 0) {
            corner = corner_open ? 0x0f : 5;
        } else {
            corner = corner_open ? 0x12 : 2;
        }
    }

    /* case 2: */
    if (gbl.dir_0_flags == 0) {
        north = 0x16;
    } else if (gbl.dir_0_flags == 3) {
        north = 0x11;
    } else if (gbl.dir_0_flags == 1) {
        north = 5;
    }

    /* case 3: */
    switch (gbl.dir_6_flags) {
    case 0:
        if (gbl.dir_0_flags == 0) {
            west = 0x16;
        } else {
            west = corner_open ? 0x10 : 0x0a;
        }
        break;

    case 3:
        west = corner_open ? 0x14 : 7;
        break;

    case 1:
        west = corner_open ? 1 : 3;
        break;
    }

    /* case 4: */
    if (gbl.dir_6_flags == 0 || gbl.dir_6_flags == 3) {
        if (gbl.dir_0_flags == 0) {
            inside = 0x16;
        } else if (gbl.dir_0_flags == 3) {
            inside = 0x17;
        } else if (gbl.dir_0_flags == 1) {
            inside = 0x0a;
        }
    } else if (gbl.dir_6_flags == 1) {
        if (gbl.dir_0_flags == 0) {
            inside = 0x0d;
        } else if (gbl.dir_0_flags == 3) {
            inside = 0x15;
        } else if (gbl.dir_0_flags == 1) {
            inside = 6;
        }
    }

    battlesetup_set_background_tile(corner, 0, 1);
    battlesetup_set_background_tile(north, 0, 2);
    battlesetup_set_background_tile(west, 1, 1);
    battlesetup_set_background_tile(inside, 1, 2);
}

/* ovr011.build_background_tiles_4, sub_376F6 */
void battlesetup_build_tiles_4(int map_y, int map_x)
{
    u8 corner;   /* var_2 */
    u8 north;    /* var_4 */
    u8 west;     /* var_3 */
    u8 inside;   /* var_5 */

    int north_of_east = battlesetup_dir_flags(2, map_y - 1, map_x);  /* var_7 */
    int east_of_north = battlesetup_dir_flags(0, map_y, map_x + 1);  /* var_8 */

    bool corner_open = (north_of_east == 0 && east_of_north == 0);

    /* case 1: */
    if (gbl.dir_0_flags == 0) {
        if (north_of_east == 1) {
            corner = 4;         /* bottom of a north-south wall */
        } else {
            corner = 0x16;      /* bottom west edge of a west-east wall */
        }
    } else if (gbl.dir_0_flags == 3) {
        corner = 0x0f;          /* top west edge of a west-east wall */
    } else {                    /* gbl.dir_0_flags == 1 */
        corner = 5;             /* top of an east-west wall */
    }

    /* case 2: */
    if (gbl.dir_0_flags == 0) {
        if (north_of_east == 0) {
            north = 0x16;       /* bottom west edge of a west-east wall */
        } else if (north_of_east == 3) {
            if (gbl.dir_2_flags == 0 && east_of_north != 0) {
                north = 0x18;
            } else {
                north = 1;
            }
        } else {                /* north_of_east == 1 */
            if (gbl.dir_2_flags == 0) {
                north = east_of_north != 0 ? 0x0b : 7;
            } else {
                north = 3;
            }
        }
    } else if (gbl.dir_2_flags != 0) {
        north = 9;
    } else if (east_of_north != 0) {
        north = 5;
    } else if (corner_open) {
        north = 0x11;
    } else {
        north = 0x13;
    }

    /* case 3: */
    if (gbl.dir_0_flags == 0) {
        west = 0x16;
    } else if (gbl.dir_0_flags == 3) {
        west = 0x10;
    } else {                    /* gbl.dir_0_flags == 1 */
        west = 0x0a;
    }

    /* case 4: */
    if (gbl.dir_0_flags == 0) {
        if (north_of_east == 0) {
            inside = 0x16;
        } else if (gbl.dir_2_flags != 0) {
            inside = 4;
        } else if (east_of_north == 0) {
            inside = 8;
        } else {
            inside = 0x0c;
        }
    } else if (gbl.dir_2_flags != 0) {
        inside = 0x0e;
    } else if (east_of_north == 0) {
        inside = 0x17;
    } else {
        inside = 0x0a;
    }

    battlesetup_set_background_tile(corner, 0, 5);
    battlesetup_set_background_tile(north, 0, 6);
    battlesetup_set_background_tile(west, 1, 5);
    battlesetup_set_background_tile(inside, 1, 6);
}

/* ovr011.SetupDungeonFloor, sub_378CD0 */
void battlesetup_dungeon_floor(void)
{
    for (gbl.byte_1AD35 = -2; gbl.byte_1AD35 <= 2; gbl.byte_1AD35++) {
        for (gbl.byte_1AD34 = -6; gbl.byte_1AD34 <= 6; gbl.byte_1AD34++) {
            int map_x = gbl.map_pos_x + gbl.byte_1AD34;
            int map_y = gbl.map_pos_y + gbl.byte_1AD35;

            gbl.dir_0_flags = battlesetup_dir_flags(0, map_y, map_x);
            gbl.dir_6_flags = battlesetup_dir_flags(6, map_y, map_x);
            gbl.dir_2_flags = battlesetup_dir_flags(2, map_y, map_x);
            gbl.dir_4_flags = battlesetup_dir_flags(4, map_y, map_x);

            battlesetup_build_tiles_1();
            battlesetup_build_tiles_2();
            battlesetup_build_tiles_3(map_y, map_x);
            battlesetup_build_tiles_4(map_y, map_x);
            gbl.byte_1AD3D = (u8)(view3d_get_wall_x2(map_y, map_x) & 0x40);
            battlesetup_place_furniture();
        }
    }
}

/* ovr011.GetCityInfo, sub_37991. The terrain flags of the city the party is
 * nearest. A city id past the end of the table reads as no flags at all, where
 * the C# would have thrown. */
static int get_city_info(void)
{
    if (gbl.current_city >= COAB_ARRAY_LEN(city_info)) {
        log_warn("battle setup: no terrain for city %d", gbl.current_city);
        return 0;
    }

    return city_info[gbl.current_city];
}

/* ovr011.SetGroundTile_40, sub_379AC. The two tiles of a bend in the road, laid
 * one square east of where the road runs. */
static void set_ground_tile_40(int map_x, int map_y)
{
    if (map_x < 0x31) {
        ground_tile_put(map_x + 1, map_y, 0x40);
    }

    if (map_y < 0x18 && map_x < 0x31) {
        ground_tile_put(map_x + 1, map_y + 1, 0x41);
    }
}

/* ovr011.SetupWildernessFloor01, sub_37A00 */
void battlesetup_wilderness_road(void)
{
    int chance = 0;

    if ((get_city_info() & 0x20) != 0) {
        chance = 0x23;
    }

    if ((get_city_info() & 0x10) != 0) {
        chance = 0x4b;
    }

    if (effect_roll_dice(100, 1) <= chance) {
        int map_x = 0x22 - effect_roll_dice(4, 5);

        /* Back up to the nearest column the road's seven-tile pattern starts
         * on. The roll leaves map_x at 14..29, so this never goes negative. */
        while (((map_x + 2) % 7) > 0) {
            map_x--;
        }

        for (int map_y = 0; map_y <= 0x18; map_y++) {
            if (map_x <= 0x31) {
                ground_tile_put(map_x, map_y, effect_roll_dice(2, 1) + 0x3b);

                if (map_x < 0x31) {
                    ground_tile_put(map_x + 1, map_y,
                                    effect_roll_dice(2, 1) + 0x3d);
                }

                if (effect_roll_dice(20, 1) == 1) {
                    set_ground_tile_40(map_x, map_y);
                }

                /* One square east per row down, so the road runs at a slant -
                 * and once it has run off the east edge the rest of the rows are
                 * left bare. */
                map_x++;
            }
        }
    }
}

/* ovr011.SetupWildernessFloor02, sub_37B0B. Water: a pool of it wherever two
 * bare squares sit one above the other, either as one square of open water or as
 * two squares of bank. */
static void wilderness_water(void)
{
    int city_flags = get_city_info();

    if ((city_flags & 0x80) != 0) {
        return;
    }

    int needed_roll = 10;

    if ((city_flags & 2) != 0) {
        needed_roll -= 5;
    }

    if ((city_flags & 4) != 0) {
        needed_roll -= 2;
    }

    if ((city_flags & 0x40) != 0) {
        needed_roll += 5;
    }

    if ((city_flags & 8) != 0) {
        needed_roll += 10;
    }

    if (needed_roll < 0) {
        needed_roll = 1;
    }

    for (int map_x = 0; map_x <= 0x31; map_x++) {
        for (int map_y = 1; map_y <= 0x18; map_y++) {
            if (tile_index_of(ground_tile_at(map_x, map_y)) == 22 &&
                tile_index_of(ground_tile_at(map_x, map_y - 1)) == 22 &&
                needed_roll >= effect_roll_dice(100, 1)) {
                if (needed_roll >= effect_roll_dice(100, 1)) {
                    ground_tile_put(map_x, map_y,
                                    effect_roll_dice(2, 1) + 0x29);
                } else {
                    ground_tile_put(map_x, map_y - 1,
                                    effect_roll_dice(5, 1) + 0x1f);
                    ground_tile_put(map_x, map_y,
                                    effect_roll_dice(5, 1) + 0x24);
                }
            }
        }
    }
}

/* ovr011.SetGroupMapStepped, sub_37CA2. One roll against five bands of chance,
 * each putting a different kind of scatter on the square: a rock, a bush, a
 * tree, scrub or a boulder. A roll past the last band leaves the square bare. */
static void set_group_map_stepped(int step_e, int step_d, int step_c, int step_b,
                                 int step_a, int map_y, int map_x)
{
    int roll = effect_roll_dice(100, 1);

    if (roll <= step_a) {
        ground_tile_put(map_x, map_y, effect_roll_dice(2, 1) + 0x39);
    } else if (roll <= step_a + step_b) {
        ground_tile_put(map_x, map_y, effect_roll_dice(2, 1) + 0x2f);
    } else if (roll <= step_a + step_b + step_c) {
        ground_tile_put(map_x, map_y, effect_roll_dice(4, 1) + 0x2b);
    } else if (roll <= step_a + step_b + step_c + step_d) {
        ground_tile_put(map_x, map_y, effect_roll_dice(3, 1) + 0x36);
    } else if (roll <= step_a + step_b + step_c + step_d + step_e) {
        ground_tile_put(map_x, map_y, effect_roll_dice(4, 1) + 0x31);
    }
}

/* ovr011.SetupWildernessFloor03, sub_37E4A. How thickly the ground is scattered,
 * from the city's terrain: barren land gets rocks and little else, forest gets
 * trees. */
static void wilderness_scatter(void)
{
    int growth = 50;

    if ((get_city_info() & 0x10) != 0) {
        growth += 10;
    }

    if ((get_city_info() & 0x20) != 0) {
        growth += 30;
    }

    if ((get_city_info() & 0x40) != 0) {
        growth += 20;
    }

    if ((get_city_info() & 4) != 0) {
        growth -= 10;
    }

    if ((get_city_info() & 2) != 0) {
        growth -= 20;
    }

    if ((get_city_info() & 0x80) != 0) {
        growth -= 50;
    }

    for (int map_x = 0; map_x <= 49; map_x++) {
        for (int map_y = 0; map_y <= 24; map_y++) {
            if (tile_index_of(ground_tile_at(map_x, map_y)) != 22) {
                continue;
            }

            /* The fourth band starts at 60 and the third one runs to 69, so
             * 60..69 takes the third: the original's own overlap, and the
             * else-if chain is what settles it. */
            if (growth >= -30 && growth <= 9) {
                set_group_map_stepped(15, 30, 0, 0, 0, map_y, map_x);
            } else if (growth >= 10 && growth <= 29) {
                set_group_map_stepped(10, 14, 5, 1, 0, map_y, map_x);
            } else if (growth >= 30 && growth <= 69) {
                set_group_map_stepped(5, 10, 5, 2, 0, map_y, map_x);
            } else if (growth >= 60 && growth <= 89) {
                set_group_map_stepped(1, 10, 10, 2, 10, map_y, map_x);
            } else if (growth >= 90 && growth <= 110) {
                set_group_map_stepped(1, 10, 15, 5, 15, map_y, map_x);
            }
        }
    }
}

/* ovr011.SetupWildernessFloor, sub_37FC8 */
void battlesetup_wilderness_floor(void)
{
    if (no_floor()) {
        return;
    }

    ground_tile_map_fill(gbl.map_to_background_tile, 23);

    gbl.current_city = gbl.area_ptr->current_city;
    battlesetup_wilderness_road();
    wilderness_water();
    wilderness_scatter();
}

/* ovr011.SetupGroundTiles, sub_38030 */
void battlesetup_ground_tiles(void)
{
    if (gbl.area_ptr->in_dungeon != 0) {
        icons_load_24x24_set(0x19, 0, 1, "DungCom");
    } else {
        icons_load_24x24_set(0x21, 0, 1, "WildCom");
    }

    icons_load_24x24_set(6, 0x22, 1, "RandCom");

    /* `new Struct_1D1BC()`: the map starts blank and belongs to the fight. */
    ground_tile_map_clear(&combat_ground_tiles);
    gbl.map_to_background_tile = &combat_ground_tiles;

    gbl.map_to_background_tile->draw_target_cursor = false;
    gbl.map_to_background_tile->size = 1;
    gbl.map_to_background_tile->ignore_walls = false;

    if (gbl.area_ptr->in_dungeon != 0) {
        battlesetup_dungeon_floor();
    } else {
        battlesetup_wilderness_floor();
    }
}

/* ---------------------------------------------------------- the combatants */

/* ovr011.SetupCombatActions, sub_380E0 */
void battlesetup_combat_actions(void)
{
    int player_count = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        character_recalc_values(player);
        player_count++;

        action_init(&combat_actions[i]);
        player->actions = &combat_actions[i];

        if (player_count > gbl.area2_ptr->party_size) {
            player->actions->non_team_member = true;
        }

        player->actions->direction = half_dir_to_iso[gbl.map_direction / 2];

        if (player->combat_team == TEAM_ENEMY) {
            player->actions->direction = (player->actions->direction + 4) % 8;
        }

        int morale = player->control_morale & CONTROL_PC_MASK;

        if (player->combat_team == TEAM_OURS &&
            player->actions->non_team_member) {
            /* An NPC whose morale is zero or off the top of the scale is given
             * the encounter's own; the party's own characters keep theirs. */
            if (morale == 0 || morale > 0x66) {
                player->control_morale =
                    (u8)(gbl.area2_ptr->field_58C + CONTROL_NPC_BASE);
            }
        }
    }
}

/* ovr011.row_column_both_out_of_range, sub_38202. False while the square is
 * still within the placement block on at least one axis, which is what keeps a
 * row growing sideways past the block's corner. */
static bool row_column_both_out_of_range(int row, int column)
{
    if ((column >= min_placement_column && column <= max_placement_column) ||
        (row >= min_placement_row && row <= max_placement_row)) {
        return false;
    }

    return true;
}

/* ovr011.try_place_combatant, sub_38233. Puts the combatant on the square if it
 * is one of their side's and still free, the ground can be walked on and nobody
 * else is standing there. A square that is used is struck off the side's grid. */
static bool try_place_combatant(int attempt, int team_y, int team_x, int row,
                                int column, int player_index)
{
    if (column < 0 || column > 10 ||
        row < 0 || row > 5 ||
        placement_grid[gbl.current_team][attempt][row][column] == 0) {
        return false;
    }

    gbl.combat_map[player_index].pos.x = column + (team_x * 6) + (team_y * 5) + 22;
    gbl.combat_map[player_index].pos.y = row + (team_y * 5) + 10;

    int ground_tile;
    int other_player_index;

    combatmap_ground_information(&ground_tile, &other_player_index, 8,
                                 gbl.player_array[player_index]);

    /* ground_tile 0 means one of the squares the combatant covers is off the
     * map, and a move cost of 0xff means the ground cannot be walked on. */
    const BackgroundTile *tile = ground_tile > 0 ? background_tile(ground_tile)
                                                 : NULL;

    if (other_player_index == 0 && tile != NULL && tile->move_cost < 0xff) {
        placement_grid[gbl.current_team][attempt][row][column] = 0;
        return true;
    }

    return false;
}

/* Which way the row is growing from its middle. The C# called this tri_state and
 * also assigned it 0, which is none of its three values and means "no anchor
 * left to try". */
typedef enum {
    PLACE_NONE  = 0,
    PLACE_START = 1,
    PLACE_RIGHT = 2,
    PLACE_LEFT  = 3
} PlaceState;

/* ovr011.place_combatant, sub_38380 */
bool battlesetup_place_combatant(int player_index)
{
    int cur_x = 0, cur_y = 0;
    int base_x = 0, base_y = 0;
    u8  in_row = 0;                 /* var_13, how wide this row has grown */

    bool placed = false;
    bool first_row = true;
    bool no_room = false;           /* var_4 */

    PlaceState state = PLACE_START;
    int row_scale = 0;
    int col_scale = 0;
    int attempt = 0;                /* var_14, which anchor square is in use */

    if (gbl.current_team < 0 || gbl.current_team > 1 ||
        gbl.team_direction[gbl.current_team] < 0 ||
        gbl.team_direction[gbl.current_team] > 3) {
        /* The C# would have indexed past its placement grid. */
        log_warn("battle setup: team %d facing %d has no placement block",
                 gbl.current_team,
                 gbl.current_team >= 0 && gbl.current_team <= 1
                     ? gbl.team_direction[gbl.current_team] : -1);
        return false;
    }

    int team_x = gbl.team_start_x[gbl.current_team];
    int team_y = gbl.team_start_y[gbl.current_team];

    /* Every pass either fills a square, widens a row, starts a new row or gives
     * up on an anchor, so the C#'s unbounded loop does finish - but it is driven
     * by a grid it edits as it goes, so it is bounded here as well and the bound
     * is logged if it is ever reached. */
    int guard = 0;

    do {
        if (++guard > 4096) {
            log_warn("battle setup: gave up finding a square for combatant %d",
                     player_index);
            return false;
        }

        int half_dir = direction_165FC[gbl.team_direction[gbl.current_team]]
                                      [attempt] / 2;

        switch (state) {
        case PLACE_START:
            {
                /* The middle of the row, pushed back away from the enemy one
                 * square per row already filled. */
                int iso_dir = half_dir_to_iso[(half_dir + 2) % 4];
                int delta_x = GBL_MAP_DIR_X_DELTA[iso_dir];
                int delta_y = GBL_MAP_DIR_Y_DELTA[iso_dir];

                base_x = first_row_x[(attempt > 0 ? 4 : 0) + half_dir] +
                         (row_scale * delta_x);
                base_y = first_row_y[(attempt > 0 ? 4 : 0) + half_dir] +
                         (row_scale * delta_y);
                cur_x = base_x;
                cur_y = base_y;
                col_scale = 1;
                state = PLACE_RIGHT;
                in_row = 1;
            }
            break;

        case PLACE_RIGHT:
            {
                int iso_dir = half_dir_to_iso[(half_dir + 1) % 4];

                cur_x = base_x + (GBL_MAP_DIR_X_DELTA[iso_dir] * col_scale);
                cur_y = base_y + (GBL_MAP_DIR_Y_DELTA[iso_dir] * col_scale);
                state = PLACE_LEFT;
                in_row += 1;
            }
            break;

        case PLACE_LEFT:
            {
                int iso_dir = half_dir_to_iso[(half_dir + 3) % 4];

                cur_x = base_x + (GBL_MAP_DIR_X_DELTA[iso_dir] * col_scale);
                cur_y = base_y + (GBL_MAP_DIR_Y_DELTA[iso_dir] * col_scale);
                state = PLACE_RIGHT;
                col_scale += 1;
                in_row += 1;
            }
            break;

        case PLACE_NONE:
            break;
        }

        bool any_cur_invalid = (cur_x < 0 || cur_y < 0 ||
                               cur_x > 10 || cur_y > 5);

        if (state > PLACE_START) {
            if ((any_cur_invalid &&
                 !row_column_both_out_of_range(cur_y, cur_x)) ||
                (first_row &&
                 in_row >= gbl.half_team_count[gbl.current_team]) ||
                (!first_row && in_row > 11)) {
                row_scale++;

                if (gbl.current_team == 0 &&
                    (gbl.team_direction[0] & 1) == 1 &&
                    attempt == 0 &&
                    row_scale == 1) {
                    /* Our side facing a diagonal on its first row: if there is
                     * any way out of the square behind it, the row after this
                     * one is left empty so the party is not packed in. */
                    int tmp_x = gbl.team_start_x[gbl.current_team] + gbl.map_pos_x;
                    int tmp_y = gbl.team_start_y[gbl.current_team] + gbl.map_pos_y;
                    bool found = false;

                    for (int var_a = 1; var_a <= 3; var_a++) {
                        int tmp_dir =
                            direction_165EC[gbl.team_direction[gbl.current_team]]
                                           [var_a];

                        if (gbl.game_state == GAME_STATE_WILDERNESS_MAP ||
                            battlesetup_dir_flags(tmp_dir, tmp_y, tmp_x) != 1) {
                            found = true;
                        }
                    }

                    if (found) {
                        row_scale++;
                    }
                }
                state = PLACE_START;
                first_row = false;
            }
        }

        if (any_cur_invalid && row_column_both_out_of_range(cur_y, cur_x)) {
            /* The block is full. Walk the side round to a neighbouring square
             * that is not walled off and start again from there. */
            placed = false;
            state = PLACE_NONE;

            while (attempt < 3 && state != PLACE_START) {
                attempt++;

                int tmp_x = gbl.team_start_x[gbl.current_team] + gbl.map_pos_x;
                int tmp_y = gbl.team_start_y[gbl.current_team] + gbl.map_pos_y;

                int tmp_dir =
                    direction_165EC[gbl.team_direction[gbl.current_team]]
                                   [attempt];

                if (gbl.game_state == GAME_STATE_WILDERNESS_MAP ||
                    battlesetup_dir_flags(tmp_dir, tmp_y, tmp_x) != 1) {
                    team_x = gbl.team_start_x[gbl.current_team] +
                             GBL_MAP_DIR_X_DELTA[tmp_dir];
                    team_y = gbl.team_start_y[gbl.current_team] +
                             GBL_MAP_DIR_Y_DELTA[tmp_dir];

                    row_scale = 0;
                    state = PLACE_START;
                }
            }

            if (state != PLACE_START) {
                no_room = true;
            }
        }

        if (!any_cur_invalid) {
            placed = try_place_combatant(attempt, team_y, team_x, cur_y, cur_x,
                                         player_index);
        }
    } while (!placed && !no_room);

    return !no_room;
}

/* ovr011.PlaceCombatants, sub_387FE */
void battlesetup_place_combatants(void)
{
    /* Everyone who is turned loose because there was nowhere to put them. They
     * are freed after the walk, because freeing one closes the team list up over
     * the gap the walk is stepping through. */
    Player *to_remove[GBL_TEAM_LIST_MAX];
    int remove_count = 0;

    character_count_combat_teams();

    for (int i = 1; i <= GBL_MAX_COMBATANT_COUNT; i++) {
        gbl.combat_map[i].size = 0;
    }
    combatmap_setup_player_index();

    gbl.team_start_x[0] = 0;
    gbl.team_start_y[0] = 0;
    gbl.team_direction[0] = gbl.map_direction / 2;

    gbl.team_start_x[1] = (gbl.area2_ptr->encounter_distance *
                           GBL_MAP_DIR_X_DELTA[gbl.map_direction]) +
                          gbl.team_start_x[0];
    gbl.team_start_y[1] = (gbl.area2_ptr->encounter_distance *
                           GBL_MAP_DIR_Y_DELTA[gbl.map_direction]) +
                          gbl.team_start_y[0];
    gbl.team_direction[1] = ((gbl.map_direction + 4) % 8) / 2;

    gbl.half_team_count[0] = (gbl.friends_count + 1) / 2;
    gbl.half_team_count[1] = (gbl.foe_count + 1) / 2;

    for (gbl.current_team = 0; gbl.current_team < 2; gbl.current_team++) {
        for (int attempt = 0; attempt < 4; attempt++) {
            /* The second attempt uses row 4 of the table, which is wider than
             * any facing's own row. */
            int direction = attempt == 1
                                ? 4
                                : gbl.team_direction[gbl.current_team];

            for (int row = 0; row < 6; row++) {
                for (int column = 0; column < 11; column++) {
                    if (row_column_limits[direction][row][0] > column ||
                        row_column_limits[direction][row][1] < column) {
                        placement_grid[gbl.current_team][attempt][row][column] = 0;
                    } else {
                        placement_grid[gbl.current_team][attempt][row][column] = 1;
                    }
                }
            }
        }
    }

    int loop_var = 1;

    gbl.combatant_count = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        input_clear_one_keypress();

        gbl.player_array[loop_var] = player;

        gbl.current_team = player->combat_team;

        gbl.combat_map[loop_var].size = player->field_DE & 7;

        if (battlesetup_place_combatant(loop_var)) {
            if (!player->in_combat) {
                /* Placed only to be taken off again: a party member who is not
                 * fighting is lying where they were put, so the square keeps a
                 * body on it and the tile under it is remembered. */
                gbl.combat_map[loop_var].size = 0;

                if (gbl.combat_type == COMBAT_TYPE_NORMAL &&
                    !player->actions->non_team_member) {
                    Point pos = gbl.combat_map[loop_var].pos;

                    if (gbl.downed_player_count > GBL_MAX_COMBATANT_COUNT) {
                        /* The C# list grew without limit; there cannot be more
                         * bodies than combatants. */
                        log_warn("battle setup: no room for %s's body",
                                 player->name);
                    } else {
                        DownedPlayerTile *d =
                            &gbl.downed_players[gbl.downed_player_count];

                        gbl.downed_player_count++;
                        d->original_background_tile =
                            ground_tile_at(pos.x, pos.y);
                        d->target = player;
                        d->map = pos;

                        ground_tile_put(pos.x, pos.y, TILE_DOWN_PLAYER);
                    }
                }
            }

            gbl.combatant_count++;
            combatmap_setup_player_index();
            loop_var++;
        } else {
            gbl.combat_map[loop_var].size = 0;

            if (player->actions->non_team_member) {
                /* An NPC or a monster with nowhere to stand takes no part in the
                 * fight at all and is turned loose. Note that loop_var is not
                 * advanced, so the next combatant takes their place. */
                gbl.player_array[loop_var] = NULL;

                if (remove_count < GBL_TEAM_LIST_MAX) {
                    to_remove[remove_count++] = player;
                } else {
                    log_warn("battle setup: cannot turn %s loose", player->name);
                }
            } else {
                /* One of the party is counted anyway, off the map: the fight
                 * still has to know about them. */
                gbl.combatant_count++;
            }
        }
    }

    for (int i = 0; i < remove_count; i++) {
        gbl.selected_player = partymenu_free_current_player(to_remove[i], false,
                                                            true);
    }
}

/* ------------------------------------------------------------ a battle begins */

/* ovr011.BattleSetup, battle_begins */
void battlesetup_battle_setup(void)
{
    gbl.delay_between_characters = false;

    picture_dax_array_free_blocks(&gbl.pic_frames);

    /* The C# dropped these three references and left them to its collector. */
    dax_block_free(gbl.head_dax);
    gbl.head_dax = NULL;
    dax_block_free(gbl.body_dax);
    gbl.body_dax = NULL;
    dax_block_free(gbl.bigpic_dax);
    gbl.bigpic_dax = NULL;

    gbl.bigpic_block_id  = 0xff;
    gbl.current_head_id  = 0xff;
    gbl.current_body_id  = 0xff;
    prompt_clear_area();
    text_game_delay();

    text_display_string("A battle begins...", 0, 0x0a, 0x18, 0);

    gbl.auto_pcs_cast_magic = false;   /* the C#'s own "TODO review this..." */
    gbl.combat_round = 0;
    gbl.combat_round_no_action_limit = GBL_COMBAT_ROUND_NO_ACTION_VALUE;
    gbl.attack_roll = 0;

    gbl.stinking_cloud_count = 0;
    gbl.cloud_kill_count = 0;
    gbl.item_ptr = NULL;

    gbl.downed_player_count = 0;

    gbl.area2_ptr->field_666 = 0;

    battlesetup_ground_tiles();

    battlesetup_combat_actions();
    battlesetup_place_combatants();

    input_clear_one_keypress();

    dax_block_free(gbl.missile_dax);
    gbl.missile_dax = dax_block_new(1, 4, 3, 0x18);

    if (gbl.team_count > 0 && gbl.team_list[0] != NULL && !no_floor()) {
        Point pos = combatmap_player_map_pos(gbl.team_list[0]);

        gbl.map_to_background_tile->map_screen_top_left =
            point_sub(pos, point_screen_center());
    } else {
        /* The C# indexed an empty team list. A fight with nobody in it has
         * nothing to centre the view on, so it is left where it was. */
        log_warn("battle setup: a battle begins with nobody in it");
    }

    character_redraw_combat_screen();

    for (int i = 0; i < gbl.team_count; i++) {
        if (gbl.team_list[i] == NULL) {
            continue;
        }

        effect_check_affects(gbl.team_list[i], CHECK_TYPE_8);
        effect_check_affects(gbl.team_list[i], CHECK_TYPE_22);
    }

    attack_calc_enemy_health_percentage();
    gbl.game_state = GAME_STATE_COMBAT;
}
