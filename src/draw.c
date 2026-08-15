#include "draw.h"
#include "display.h"

#include <stdio.h>
#include <string.h>

/* 17 is the engine's "no such color" sentinel: it is outside the 0..16 range a
 * picture pixel can hold, so the comparisons below never match. */
#define NO_COLOR 17

static int g_color_no_draw       = NO_COLOR;
static int g_color_recolor_from  = NO_COLOR;
static int g_color_recolor_to    = NO_COLOR;

DaxBlock *draw_load_dax(u8 mask_colour, u8 masked, int block_id,
                        const char *file_name)
{
    char name[80];

    if (!file_name) {
        return NULL;
    }
    snprintf(name, sizeof(name), "%s.dax", file_name);

    return dax_load_block(name, block_id, masked, mask_colour);
}

void draw_clipped_recolor(int from, int to)
{
    g_color_recolor_from = from;
    g_color_recolor_to = to;
}

void draw_clipped_nodraw(int color)
{
    g_color_no_draw = color;
}

void draw_clipped_picture(const DaxBlock *block, int row_y, int col_x, int index,
                          int clip_min_x, int clip_max_x,
                          int clip_min_y, int clip_max_y)
{
    size_t offset;
    int min_y, max_y, min_x, max_x;

    if (!block || !block->data) {
        return;
    }

    offset = (size_t)index * (size_t)block->bpp;

    min_y = row_y * 8;
    max_y = min_y + block->height;
    min_x = col_x * 8;
    max_x = min_x + (block->width * 8);

    for (int pix_y = min_y; pix_y < max_y; pix_y++) {
        for (int pix_x = min_x; pix_x < max_x; pix_x++) {
            u8 color;

            if (offset >= block->data_size) {
                display_update();
                return;
            }
            color = block->data[offset++];

            if (pix_x < clip_min_x || pix_x >= clip_max_x ||
                pix_y < clip_min_y || pix_y >= clip_max_y) {
                continue;
            }

            if (color == g_color_no_draw) {
                continue;
            }
            if (color == g_color_recolor_from) {
                display_set_pixel(pix_x, pix_y, g_color_recolor_to);
            } else {
                display_set_pixel(pix_x, pix_y, color);
            }
        }
    }

    display_update();
}

void draw_combat_picture(const DaxBlock *block, int row_y, int col_x, int index)
{
    draw_clipped_picture(block, row_y, col_x, index, 8, 176, 8, 176);
}

void draw_picture(const DaxBlock *block, int row_y, int col_x, int index)
{
    draw_clipped_picture(block, row_y, col_x, index, 0, EGA_W, 0, EGA_H);
}

/* seg040.OverlayUnbounded / OverlayBounded. The two were separate routines in
 * the DOS binary - one clipped to the combat view and one not - but the C#
 * found them identical, both drawing into the combat view one cell down and
 * right. arg_8 is ignored, as it was there. */
void draw_overlay_unbounded(const DaxBlock *source, int arg_8, int item_index,
                            int row_y, int col_x)
{
    (void)arg_8;
    draw_combat_picture(source, row_y + 1, col_x + 1, item_index);
}

void draw_overlay_bounded(const DaxBlock *source, int arg_8, int item_index,
                          int row_y, int col_x)
{
    (void)arg_8;
    draw_combat_picture(source, row_y + 1, col_x + 1, item_index);
}

void draw_ega_backup(DaxBlock *block, int row_y, int col_x)
{
    size_t offset = 0;
    int min_y, max_y, min_x, max_x;

    if (!block || !block->data) {
        return;
    }

    min_y = row_y * 8;
    max_y = min_y + block->height;
    min_x = col_x * 8;
    max_x = min_x + (block->width * 8);

    for (int pix_y = min_y; pix_y < max_y; pix_y++) {
        for (int pix_x = min_x; pix_x < max_x; pix_x++) {
            if (offset >= block->data_size) {
                return;
            }
            block->data[offset++] = display_get_pixel(pix_x, pix_y);
        }
    }
}

void draw_set_palette_color(int color, int index)
{
    display_set_ega_palette(index, color);
}

void draw_color_block(int color, int line_count, int col_width,
                      int line_y, int col_x)
{
    int min_y = line_y + 8;
    int max_y = min_y + line_count;
    int min_x = (col_x * 8) + 8;
    int max_x = min_x + (col_width * 8);

    for (int pix_y = min_y; pix_y < max_y; pix_y++) {
        for (int pix_x = min_x; pix_x < max_x; pix_x++) {
            display_set_pixel(pix_x, pix_y, color);
        }
    }
}

/* engine/seg041.cs: DrawRectangle */
void draw_rectangle(u8 color, int y_end, int x_end, int y_start, int x_start)
{
    int px_start = x_start * 8;
    int px_end   = (x_end + 1) * 8;
    int py_start = y_start * 8;
    int py_end   = (y_end + 1) * 8;

    for (int x = px_start; x < px_end; x++) {
        for (int y = py_start; y < py_end; y++) {
            display_set_pixel(x, y, color);
        }
    }
}
