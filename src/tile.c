/* tile.c - Ported from Classes/Struct_189B4.cs and the tile and direction tables
 * in Classes/Gbl.cs.
 */
#include "tile.h"

#include "log.h"

/* unk_189B4, transcribed row for row from Gbl.cs:193. */
const BackgroundTile background_tiles[BACKGROUND_TILE_COUNT] = {
    {    1,    0, 0xff,    0 },
    { 0xff,    1,    2,    0 },
    { 0xff,    1,    2,    1 },
    { 0xff,    1,    2,    2 },
    { 0xff,    1,    2,    3 },
    {    1,    1,    0,    4 },
    { 0xff,    1,    2,    5 },
    { 0xff,    1,    2,    6 },
    { 0xff,    1,    2,    7 },
    {    1,    1,    0,    8 },
    { 0xff,    1,    2,    9 },
    {    1,    1,    0, 0x0a },
    { 0xff,    1,    2, 0x0b },
    {    1,    1,    0, 0x0c },
    { 0xff,    1,    2, 0x0d },
    {    1,    1,    0, 0x0e },
    { 0xff,    1,    2, 0x0f },
    {    1,    1,    0, 0x10 },
    { 0xff,    1,    2, 0x11 },
    { 0xff,    1,    2, 0x12 },
    { 0xff,    1,    2, 0x13 },
    { 0xff,    1,    2, 0x14 },
    { 0xff,    1,    2, 0x15 },
    {    1,    1,    0, 0x16 },
    {    1,    1,    0, 0x17 },
    { 0xff,    1,    2, 0x18 },
    {    2,    2,    0, 0x22 },
    {    1,    1,    0, 0x23 },
    {    1,    1,    0, 0x24 },
    {    1,    1,    0, 0x25 },
    {    1,    1,    0, 0x26 },
    {    1,    1,    0, 0x27 },
    { 0xff,    1,    2,    0 },
    { 0xff,    1,    2,    1 },
    { 0xff,    1,    2,    2 },
    { 0xff,    1,    2,    3 },
    { 0xff,    1,    2,    4 },
    {    1,    1,    0,    5 },
    {    1,    1,    0,    6 },
    {    1,    1,    0,    7 },
    {    1,    1,    0,    8 },
    {    1,    1,    0,    9 },
    { 0xff,    1,    2,   10 },
    { 0xff,    1,    2,   11 },
    {    1,    1,    0,   12 },
    {    1,    1,    0,   13 },
    {    1,    1,    0,   14 },
    {    1,    1,    0,   15 },
    {    2,    1,    0,   16 },
    {    2,    1,    0,   17 },
    {    2,    1,    0,   18 },
    {    2,    1,    0,   19 },
    {    2,    1,    0,   20 },
    {    2,    1,    0,   21 },
    {    1,    1,    0,   22 },
    {    1,    1,    0, 0x17 },
    {    1,    1,    0, 0x18 },
    {    1,    1,    0, 0x19 },
    {    2,    1,    0, 0x1a },
    {    2,    1,    0, 0x1b },
    {    4,    0,    0, 0x1c },
    {    4,    0,    0, 0x1d },
    {    4,    0,    0, 0x1e },
    {    4,    0,    0, 0x1f },
    {    1,    1,    0, 0x20 },
    {    1,    1,    0, 0x21 },
    {    0,    0, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0xff },
    {    0,    0,    0,    1 },
    { 0xff, 0xff, 0xff, 0xff },
    {    0,    0,    1,    0 },
    { 0xff, 0xff, 0xff, 0xff },
    {    0,    0,    1,    0 },
    {    0,    1,    1,    1 }
};

const BackgroundTile *background_tile(int ground_tile)
{
    if (ground_tile < 0 || ground_tile >= BACKGROUND_TILE_COUNT) {
        log_warn("background tiles: no tile %d", ground_tile);
        return NULL;
    }
    return &background_tiles[ground_tile];
}

/* --------------------------------------------------------------- directions */

const Point map_direction_delta[MAP_DIRECTIONS] = {
    {  0, -1 },   /* 0 north */
    {  1, -1 },   /* 1 north east */
    {  1,  0 },   /* 2 east */
    {  1,  1 },   /* 3 south east */
    {  0,  1 },   /* 4 south */
    { -1,  1 },   /* 5 south west */
    { -1,  0 },   /* 6 west */
    { -1, -1 },   /* 7 north west */
    {  0,  0 }    /* 8 stay put */
};

Point map_direction_step(int direction)
{
    if (direction < 0 || direction >= MAP_DIRECTIONS) {
        log_warn("direction: %d is not a compass direction", direction);
        return point_make(0, 0);
    }
    return map_direction_delta[direction];
}

const u8 small_cloud_directions[SMALL_CLOUD_DIRECTION_COUNT] = { 8, 2, 3, 4 };
const u8 small_cloud_directions_alt[SMALL_CLOUD_DIRECTION_COUNT] = { 2, 3, 4, 8 };

const u8 cloud_directions[CLOUD_DIRECTION_COUNT] = { 8, 0, 1, 2, 3, 4, 5, 6, 7 };
