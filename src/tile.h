/* tile.h - the background tile table and the compass direction deltas.
 * Ported from Classes/Struct_189B4.cs and the BackGroundTiles, Tile_* and
 * MapDirection* statics in Classes/Gbl.cs.
 */
#ifndef COAB_TILE_H
#define COAB_TILE_H

#include "coab.h"
#include "point.h"

/* One row per ground tile (unk_189B4). move_cost 0xff means impassable. */
typedef struct {
    u8 move_cost;   /* field_0, 189B4 */
    u8 field_1;     /* 189B5 */
    u8 field_2;     /* 189B6 */
    u8 tile_index;  /* field_3, 189B7 */
} BackgroundTile;

/* The C# declared BackGroundTiles_count as 64 and called it a guess, then wrote
 * 74 rows. The rows are what the game indexes, so the count follows them. */
#define BACKGROUND_TILE_COUNT 74

extern const BackgroundTile background_tiles[BACKGROUND_TILE_COUNT];

/* Returns NULL and logs for a tile id past the end of the table. */
const BackgroundTile *background_tile(int ground_tile);

/* Ground tiles the game names. */
#define TILE_TABLE          0x1a
#define TILE_CHAIR          0x1b
#define TILE_CLOUD_KILL     0x1c
#define TILE_STINKING_CLOUD 0x1e
#define TILE_DOWN_PLAYER    0x1f

/* --------------------------------------------------------------- directions */

/* Directions run clockwise from north: 0 N, 1 NE, 2 E ... 7 NW. Entry 8 is the
 * no-move case, which the movement code uses as "stay put". */
#define MAP_DIRECTIONS 9

extern const Point map_direction_delta[MAP_DIRECTIONS];

/* Point(0, 0) for a direction outside 0..8, so a bad direction leaves the walker
 * where it stands instead of reading past the table. */
Point map_direction_step(int direction);

/* Direction orders the cloud spells walk their cells in (unk_18AE9 and
 * unk_18AED). Both start at 8, the no-move entry, so the cell the cloud was cast
 * on is filled first. */
#define SMALL_CLOUD_DIRECTION_COUNT 4
#define CLOUD_DIRECTION_COUNT       9

extern const u8 small_cloud_directions[SMALL_CLOUD_DIRECTION_COUNT];
extern const u8 cloud_directions[CLOUD_DIRECTION_COUNT];

/* unk_18AEA: the same four directions as small_cloud_directions with the no-move
 * entry last instead of first. */
extern const u8 small_cloud_directions_alt[SMALL_CLOUD_DIRECTION_COUNT];

#endif /* COAB_TILE_H */
