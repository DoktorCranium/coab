/* mapcursor.h - the blinking marker on the overland map.
 * Ported from engine/ovr028.cs (class MapCursor).
 *
 * The overland map is a picture with 33 marked places on it. The cursor is a
 * single solid 8x8 cell (gbl.cursor, filled with colour 15 at startup) drawn
 * over the current one; what it covered is kept in gbl.cursor_bkup so it can be
 * put back.
 */
#ifndef COAB_MAPCURSOR_H
#define COAB_MAPCURSOR_H

#include "coab.h"

/* unk_16D5A / unk_16D7A hold 33 positions. The last one is 0x0f,0x00, which is
 * off the map area proper; the original had it and city 32 uses it. */
#define MAP_CURSOR_CITY_COUNT 33

/* ovr028.SetPosition (sub_6E005). An id outside the table is ignored, with a
 * warning: the C# would have thrown, and the id comes from a save file. */
void map_cursor_set_position(int current_city);

/* ovr028.Draw (sub_6E02E) - saves what is underneath, then draws the cursor. */
void map_cursor_draw(void);

/* ovr028.Restore (sub_6E05D) - puts the saved cell back. */
void map_cursor_restore(void);

/* Where the cursor is, in 8x8 cells. For the self test. */
void map_cursor_position(int *out_col_x, int *out_row_y);

#endif /* COAB_MAPCURSOR_H */
