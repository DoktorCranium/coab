/* draw.h - picture and shape drawing. Ported from engine/seg040.cs.
 *
 * Positions are given in 8x8 character cells, not pixels: rowY counts 8-pixel
 * rows and colX counts 8-pixel columns. A picture's `width` is likewise in
 * 8-pixel columns while its `height` is in pixels.
 */
#ifndef COAB_DRAW_H
#define COAB_DRAW_H

#include "coab.h"
#include "dax.h"

/* seg040.LoadDax - loads block_id from "<file_name>.dax". Caller frees. */
DaxBlock *draw_load_dax(u8 mask_colour, u8 masked, int block_id,
                        const char *file_name);

/* seg040.draw_picture - blits frame `index` with the full screen as the clip
 * region. */
void draw_picture(const DaxBlock *block, int row_y, int col_x, int index);

/* seg040.draw_combat_picture - as above, clipped to the combat viewport. */
void draw_combat_picture(const DaxBlock *block, int row_y, int col_x, int index);

/* seg040.OverlayUnbounded / OverlayBounded - draw_combat_picture one cell down
 * and right. Both names are kept because the callers distinguish them. */
void draw_overlay_unbounded(const DaxBlock *source, int arg_8, int item_index,
                            int row_y, int col_x);
void draw_overlay_bounded(const DaxBlock *source, int arg_8, int item_index,
                          int row_y, int col_x);

void draw_clipped_picture(const DaxBlock *block, int row_y, int col_x, int index,
                          int clip_min_x, int clip_max_x,
                          int clip_min_y, int clip_max_y);

/* Substitution applied by the next draw: pixels equal to `from` are drawn as
 * `to`. Passing 17 (the sentinel the engine uses) disables it. */
void draw_clipped_recolor(int from, int to);

/* Pixels of this color are skipped entirely. 17 disables it. */
void draw_clipped_nodraw(int color);

/* seg040.ega_backup - reads the screen back into a picture, for restoring what
 * a sprite covered up. */
void draw_ega_backup(DaxBlock *block, int row_y, int col_x);

/* seg040.SetPaletteColor */
void draw_set_palette_color(int color, int index);

/* seg040.DrawColorBlock - a filled rectangle, offset 8 pixels in both axes to
 * sit inside the screen frame. */
void draw_color_block(int color, int line_count, int col_width,
                      int line_y, int col_x);

/* seg041.DrawRectangle - fills whole cells: the region spans columns
 * [x_start, x_end] and rows [y_start, y_end] inclusive. */
void draw_rectangle(u8 color, int y_end, int x_end, int y_start, int x_start);

#endif /* COAB_DRAW_H */
