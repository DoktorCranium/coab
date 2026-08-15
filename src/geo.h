/* geo.h - the dungeon geometry: which walls each map square has, and which
 * picture to draw for them. Ported from Classes/GeoBlock.cs.
 *
 * A GEO block is 0x400 bytes behind a two-byte header and describes a 16x16 grid
 * of squares. Each square gets a wall type per compass direction plus two
 * further nibble fields, packed four ways across four 0x100-byte planes.
 *
 * The wall definitions live in separate WALLDEF blocks: three sets of five rows
 * of 156 picture ids, indexed by wall type and by how far down the corridor the
 * square being drawn is.
 */
#ifndef COAB_GEO_H
#define COAB_GEO_H

#include "coab.h"

#define GEO_BLOCK_DATA_SIZE  0x400
#define GEO_MAP_DIM          16

/* One map square. dir 0/2/4/6 are the four compass directions the 3D view can
 * face; the odd directions are the diagonals, which have no walls of their own. */
typedef struct {
    u8 wall_type_dir_0;
    u8 wall_type_dir_2;
    u8 wall_type_dir_4;
    u8 wall_type_dir_6;

    u8 x2;              /* the whole third plane byte */

    u8 x3_dir_0;        /* the fourth plane, two bits per direction */
    u8 x3_dir_2;
    u8 x3_dir_4;
    u8 x3_dir_6;
} MapInfo;

typedef struct {
    u8      data[GEO_BLOCK_DATA_SIZE];
    MapInfo maps[GEO_MAP_DIM][GEO_MAP_DIM];   /* [y][x], as the C# indexed it */
} GeoBlock;

/* LoadData: skips the two-byte header, then unpacks all 256 squares. */
bool geo_block_load(GeoBlock *g, const u8 *data, size_t data_size);

/* --------------------------------------------------------- wall definitions */

#define WALL_DEF_ROWS      5
#define WALL_DEF_COLS      156
#define WALL_DEF_BLOCK_SIZE (WALL_DEF_ROWS * WALL_DEF_COLS)   /* 780 */
#define WALL_DEF_BLOCKS    3

typedef struct {
    u8 data[WALL_DEF_ROWS][WALL_DEF_COLS];
} WallDefBlock;

typedef struct {
    WallDefBlock blocks[WALL_DEF_BLOCKS];
} WallDefs;

void wall_defs_clear(WallDefs *w);

/* LoadData: one 780-byte block per set found in data, starting at set base_set.
 * Sets are numbered from 1, as the engine numbers them. */
bool wall_defs_load(WallDefs *w, int base_set, const u8 *data, size_t data_size);

/* Id(y, x) on one set, and Offset(): shifts every picture id from 0x2d up by
 * off, so a set can be reused with a different tile bank. The addition wraps in
 * a byte, as it did in the original. */
int  wall_defs_id(const WallDefs *w, int set, int y, int x);
void wall_defs_block_offset(WallDefs *w, int set, int off);

#endif /* COAB_GEO_H */
