/* geo.c - Ported from Classes/GeoBlock.cs. */
#include <string.h>

#include "geo.h"

#include "log.h"

/* MapInfo(data, x, y): the four planes are 0x100 bytes apart, and a row is 16
 * squares. */
static void map_info_unpack(MapInfo *m, const u8 *data, int map_x, int map_y)
{
    int at = map_x + (map_y << 4);
    u8  b;

    m->wall_type_dir_0 = (u8)((data[at] >> 4) & 0x0f);
    m->wall_type_dir_2 = (u8)(data[at] & 0x0f);
    m->wall_type_dir_4 = (u8)((data[0x100 + at] >> 4) & 0x0f);
    m->wall_type_dir_6 = (u8)(data[0x100 + at] & 0x0f);

    m->x2 = data[0x200 + at];

    b = data[0x300 + at];
    m->x3_dir_6 = (u8)((b >> 6) & 3);
    m->x3_dir_4 = (u8)((b >> 4) & 3);
    m->x3_dir_2 = (u8)((b >> 2) & 3);
    m->x3_dir_0 = (u8)(b & 3);
}

bool geo_block_load(GeoBlock *g, const u8 *data, size_t data_size)
{
    if (data_size < 2 + GEO_BLOCK_DATA_SIZE) {
        log_warn("GEO block: %zu bytes, need %d", data_size,
                 2 + GEO_BLOCK_DATA_SIZE);
        return false;
    }

    memset(g, 0, sizeof(*g));
    memcpy(g->data, data + 2, GEO_BLOCK_DATA_SIZE);

    for (int y = 0; y < GEO_MAP_DIM; y++) {
        for (int x = 0; x < GEO_MAP_DIM; x++) {
            map_info_unpack(&g->maps[y][x], g->data, x, y);
        }
    }

    return true;
}

/* --------------------------------------------------------- wall definitions */

void wall_defs_clear(WallDefs *w)
{
    memset(w, 0, sizeof(*w));
}

/* Sets are 1-based here because the engine numbers them that way; set 0 would be
 * blocks[-1]. */
static bool set_valid(int set)
{
    if (set >= 1 && set <= WALL_DEF_BLOCKS) {
        return true;
    }
    log_warn("wall definitions: set %d does not exist (1..%d)",
             set, WALL_DEF_BLOCKS);
    return false;
}

bool wall_defs_load(WallDefs *w, int base_set, const u8 *data, size_t data_size)
{
    size_t count = data_size / WALL_DEF_BLOCK_SIZE;
    size_t offset = 0;

    if (count == 0) {
        log_warn("wall definitions: %zu bytes is less than one %d byte block",
                 data_size, WALL_DEF_BLOCK_SIZE);
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        int set = base_set + (int)i;

        if (!set_valid(set)) {
            return false;
        }
        memcpy(w->blocks[set - 1].data, data + offset, WALL_DEF_BLOCK_SIZE);
        offset += WALL_DEF_BLOCK_SIZE;
    }

    return true;
}

int wall_defs_id(const WallDefs *w, int set, int y, int x)
{
    if (!set_valid(set)) {
        return 0;
    }
    if (y < 0 || y >= WALL_DEF_ROWS || x < 0 || x >= WALL_DEF_COLS) {
        log_warn("wall definitions: set %d has no entry %d,%d", set, y, x);
        return 0;
    }
    return w->blocks[set - 1].data[y][x];
}

void wall_defs_block_offset(WallDefs *w, int set, int off)
{
    WallDefBlock *b;

    if (!set_valid(set)) {
        return;
    }
    b = &w->blocks[set - 1];

    /* Ids below 0x2d name shared tiles and stay put. The sum wraps in a byte,
     * which the original relied on. */
    for (int y = 0; y < WALL_DEF_ROWS; y++) {
        for (int x = 0; x < WALL_DEF_COLS; x++) {
            if (b->data[y][x] >= 0x2d) {
                b->data[y][x] = (u8)(b->data[y][x] + off);
            }
        }
    }
}
