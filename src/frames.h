/* frames.h - the decorative screen borders and the 8x8 symbol banks.
 * Ported from engine/seg037.cs and engine/ovr038.cs.
 *
 * The borders are drawn one 8x8 tile at a time from banks loaded out of
 * 8X8D<area>.DAX, so they change appearance per chapter.
 */
#ifndef COAB_FRAMES_H
#define COAB_FRAMES_H

#include "coab.h"

/* ovr038.Load8x8D - loads bank `symbol_set` (0..4) from 8X8D<game_area>.DAX.
 * Returns false when the block is missing; the caller decides whether to
 * continue, since the original treated it as fatal. */
bool frames_load_8x8d(int symbol_set, int block_id);

/* ovr038.Put8x8Symbol - draws symbol `symbol_id` at a cell. Ids are global
 * across the five banks; the bank is chosen from the id's range. */
void frames_put_symbol(u8 arg_0, bool use_overlay, int symbol_id,
                       int row_y, int col_x);

/* seg037 border layouts. */
void frames_draw_outer(void);           /* draw8x8_01 */
void frames_draw_02(void);              /* draw8x8_02 - the credits layout */
void frames_draw_03(void);
void frames_draw_wilderness_map(void);  /* draw8x8_04 */
void frames_draw_05(void);
void frames_draw_combat(void);          /* draw8x8_06 */
void frames_draw_07(void);

/* seg037.draw8x8_clear_area */
void frames_clear_area(int y_end, int x_end, int y_start, int x_start);
void frames_clear_region(TextRegion region);

#endif /* COAB_FRAMES_H */
