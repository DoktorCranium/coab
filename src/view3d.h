/* view3d.h - the first-person dungeon view and the overhead area map.
 * Ported from engine/ovr031.cs and engine/ovr029.cs.
 *
 * The view is built out of 8x8 tiles, not polygons. The party stands on one
 * square of a 16x16 grid facing one of four directions, and the renderer walks
 * three squares outwards - far, middle, near - stamping the tiles that make up
 * each wall it finds. Which tiles those are comes from WALLDEF: five rows (one
 * per distance/slice) of 156 tile ids per wall set, indexed by the wall type
 * stored in the GEO map.
 *
 * The ten Column_/Row_ constants and the idxOffset/colCount/rowCount tables are
 * the whole projection: there is no arithmetic behind it, just a fixed screen
 * position and a fixed run of tile ids for each of the ten wall slots a view can
 * show.
 */
#ifndef COAB_VIEW3D_H
#define COAB_VIEW3D_H

#include "coab.h"
#include "geo.h"

/* ovr031.DrawAreaMap, sub_7100F. The 11x11 overhead map, drawn from the 0x104
 * symbol family: one symbol per combination of walls, plus an arrow for the
 * party. */
void view3d_draw_area_map(int party_dir, int party_map_y, int party_map_x);

/* ovr031.Draw3dWorldBackground. Sky, ground and - outdoors, under a blue sky -
 * the sun or the moon at the hour's position. */
void view3d_draw_background(void);

/* ovr031.draw_3D_8x8_titles, sub_71434 ("titles" is the original's typo for
 * tiles). Stamps one wall slot: offset_index picks which of the ten slots, and
 * wall_type is the type byte out of the map, which selects the wall set and the
 * slice within it. */
void view3d_draw_8x8_tiles(int offset_index, int wall_type, int row_start,
                           int col_start);

/* ovr031.MapCoordIsValid, sub_71542. */
bool view3d_map_coord_is_valid(int map_y, int map_x);

/* ovr031.WallDoorFlagsGet, sub_71573. The door/secret flags of one wall, or 1
 * when the square has no wall in that direction at all. */
u8 view3d_wall_door_flags_get(int map_dir, int map_y, int map_x);

/* ovr031.getMap_wall_type. 0 means "no wall this way". */
u8 view3d_map_wall_type(int direction, int map_y, int map_x);

/* ovr031.getMap_XXX. The map square, wrapped around the edges of the grid, or
 * NULL when the coordinate is off the map and the current ECL block does not
 * wrap. */
MapInfo *view3d_map_info(int map_y, int map_x);

/* ovr031.get_wall_x2, sub_717A5. The square's third plane byte; over 0x7f means
 * the square is roofed, which is what decides indoor from outdoor. */
u8 view3d_get_wall_x2(int map_y, int map_x);

/* ovr031.Draw3dWorld, sub_71820. Draws the whole view - or the area map, if the
 * player has that switched on - in one go. */
void view3d_draw_world(u8 party_dir, int party_pos_y, int party_pos_x);

/* ovr031.LoadWalldef. Loads block_id of WALLDEF<area>.DAX into wall set
 * symbol_set (1..3) and the matching 8x8 tile banks with it. */
void view3d_load_walldef(int symbol_set, int block_id);

/* ovr031.Load3DMap. Loads block_id of GEO<area>.DAX as the current map. */
void view3d_load_3d_map(int block_id);

/* ovr029.RedrawView, sub_6F0BA. Redraws whatever the party is looking at: the
 * 3D view in a dungeon, the wilderness backdrop outside one. */
void view3d_redraw(void);

/* The SKY 250/251/252 pictures, loaded by engine/seg001.cs InitFirst. Kept here
 * because this is the only code that draws them; a ported InitFirst calls it. */
bool view3d_load_sky(void);

#endif /* COAB_VIEW3D_H */
