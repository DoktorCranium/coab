#include "frames.h"
#include "draw.h"
#include "display.h"
#include "text.h"
#include "gbl.h"
#include "input.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

/* Tile layouts from engine/seg037.cs, keeping the original symbol names so the
 * two files can be compared. Each entry is added to a bank base id before being
 * passed to frames_put_symbol. */

static const u8 outer_frame_top[40] = {   /* byte_16E60 */
    0, 6, 1, 1, 1, 1, 1, 1, 6, 1, 1, 1, 1, 4, 1, 1, 1, 6, 1, 1,
    1, 1, 1, 1, 1, 8, 1, 1, 1, 1, 1, 1, 1, 4, 1, 1, 1, 6, 1, 2
};

static const u8 outer_frame_bottom[40] = {   /* unk_16EB0 */
    1, 8, 6, 1, 1, 1, 1, 1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 6, 8, 1,
    1, 1, 4, 1, 1, 1, 1, 1, 1, 6, 1, 1, 1, 1, 1, 1, 1, 1, 4, 3
};

static const u8 outer_frame_left[24] = {   /* unk_16EF2 */
    0, 2, 9, 5, 2, 2, 2, 2, 2, 2, 5, 7, 2, 2, 2, 2, 2, 9, 7, 2, 2, 2, 7, 1
};

static const u8 outer_frame_right[24] = {  /* unk_16F1B */
    2, 2, 9, 7, 2, 2, 2, 5, 2, 2, 2, 2, 2, 2, 2, 2, 2, 7, 2, 2, 2, 2, 5, 2
};

static const u8 x8x8_07[40] = {
    0, 8, 1, 1, 1, 1, 1, 1, 1, 1, 1, 6, 1, 1, 1, 8, 4, 1, 1, 1,
    6, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 1, 6, 1, 1, 1, 1, 1, 8, 2
};

static const u8 unk_16ED6[17] = { 4, 3, 0, 6, 1, 1, 1, 1, 8, 1, 1, 4, 1, 1, 2, 1, 4 };
static const u8 unk_16EE3[15] = { 1, 2, 1, 4, 1, 1, 1, 1, 1, 1, 8, 4, 1, 1, 3 };
static const u8 unk_16F0A[17] = { 0, 7, 5, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 5, 2, 9, 4 };
static const u8 unk_16F31[15] = { 5, 2, 0, 2, 7, 2, 2, 2, 2, 5, 2, 2, 2, 2, 1 };
static const u8 unk_16F3E[15] = { 2, 1, 2, 5, 9, 2, 2, 2, 7, 5, 2, 2, 2, 2, 3 };
static const u8 unk_16F4D[23] = { 0, 2, 9, 5, 2, 2, 2, 2, 2, 2, 5, 7, 2, 2, 2, 2, 2, 9, 7, 2, 2, 2, 1 };
static const u8 unk_16F64[23] = { 0, 7, 5, 2, 2, 2, 2, 2, 2, 2, 2, 2, 7, 5, 2, 2, 2, 2, 2, 2, 5, 2, 4 };
static const u8 unk_16F7B[23] = { 2, 2, 9, 7, 2, 2, 2, 5, 2, 2, 2, 2, 2, 2, 2, 2, 2, 7, 2, 2, 2, 2, 2 };

/* Bank 4 covers symbol ids 0x100..0x127, which is where the border tiles live;
 * 0x11E and 0x114 are the two bases the layouts index from. */
#define BORDER_BASE 0x11E
#define INNER_BASE  0x114

bool frames_load_8x8d(int symbol_set, int block_id)
{
    char file_name[32];
    DaxBlock *block;

    if (symbol_set < 0 || symbol_set >= GBL_SYMBOL_SETS) {
        return false;
    }

    snprintf(file_name, sizeof(file_name), "8x8d%d", (int)gbl.game_area);

    /* mask colour 13, masked: the banks use colour 13 as transparency. */
    block = draw_load_dax(13, 1, block_id, file_name);
    if (!block) {
        log_error("unable to load block %d from 8X8D%d.DAX",
                  block_id, (int)gbl.game_area);
        return false;
    }

    dax_block_free(gbl.symbol_8x8_set[symbol_set]);
    gbl.symbol_8x8_set[symbol_set] = block;

    input_clear_keyboard();
    return true;
}

/* ovr038.Put8x8Symbol */
void frames_put_symbol(u8 arg_0, bool use_overlay, int symbol_id,
                       int row_y, int col_x)
{
    int symbol_set;
    DaxBlock *bank;

    if (symbol_id >= 0x01 && symbol_id <= 0x2d) {
        symbol_set = 0;
    } else if (symbol_id >= 0x2e && symbol_id <= 0x73) {
        symbol_set = 1;
    } else if (symbol_id >= 0x74 && symbol_id <= 0xb9) {
        symbol_set = 2;
    } else if (symbol_id >= 0xba && symbol_id <= 0xff) {
        symbol_set = 3;
    } else if (symbol_id >= 0x100 && symbol_id <= 0x127) {
        symbol_set = 4;
    } else {
        /* The C# threw here. Logging and skipping keeps a bad id from taking
         * the whole game down. */
        log_warn("bad symbol number in put_symbol: %d", symbol_id);
        return;
    }

    bank = gbl.symbol_8x8_set[symbol_set];
    if (!bank) {
        return;
    }

    symbol_id -= GBL_SYMBOL_SET_FIX[symbol_set];
    if (symbol_id < 0 || symbol_id >= bank->item_count) {
        return;
    }

    if (use_overlay) {
        /* seg040.OverlayUnbounded shifts the target cell by one in both axes. */
        draw_combat_picture(bank, row_y + 1, col_x + 1, symbol_id);
    } else if (gbl.cursor_bkup) {
        /* The original copied the frame into a 1x8 scratch picture and drew
         * that, so the blit always starts at frame 0. */
        size_t offset = (size_t)symbol_id * (size_t)bank->bpp;
        size_t count = COAB_MIN((size_t)bank->bpp, gbl.cursor_bkup->data_size);

        if (offset + count <= bank->data_size) {
            memcpy(gbl.cursor_bkup->data, bank->data + offset, count);
            draw_picture(gbl.cursor_bkup, row_y, col_x, 0);
        }
    }
    (void)arg_0;
}

void frames_clear_area(int y_end, int x_end, int y_start, int x_start)
{
    draw_rectangle(0, y_end, x_end, y_start, x_start);
}

void frames_clear_region(TextRegion region)
{
    int r = (int)region;

    if (r < 0 || r > 2) {
        return;
    }
    frames_clear_area(TEXT_BOUNDS[r][0], TEXT_BOUNDS[r][1],
                      TEXT_BOUNDS[r][2], TEXT_BOUNDS[r][3]);
}

/* seg037.DrawFrame_Outer (draw8x8_01) */
void frames_draw_outer(void)
{
    display_update_stop();

    frames_clear_area(0x16, 0x26, 1, 1);

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, outer_frame_top[col_x] + BORDER_BASE, 0, col_x);
    }

    for (int row_y = 0; row_y < 0x17; row_y++) {
        frames_put_symbol(0, false, outer_frame_left[row_y]  + BORDER_BASE, row_y, 0);
        frames_put_symbol(0, false, outer_frame_right[row_y] + BORDER_BASE, row_y, 0x27);
    }

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, outer_frame_bottom[col_x] + BORDER_BASE, 0x17, col_x);
    }

    display_update_start();
}

/* seg037.draw8x8_02 */
void frames_draw_02(void)
{
    display_update_stop();

    frames_draw_outer();

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, x8x8_07[col_x] + BORDER_BASE, 3, col_x);
        frames_put_symbol(0, false, x8x8_07[col_x] + BORDER_BASE, 8, col_x);
    }

    display_update_start();
}

/* seg037.draw8x8_03 */
void frames_draw_03(void)
{
    display_update_stop();

    frames_draw_outer();

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, x8x8_07[col_x] + BORDER_BASE, 0x10, col_x);
    }

    for (int row_y = 0; row_y <= 0x10; row_y++) {
        frames_put_symbol(0, false, unk_16F0A[row_y] + BORDER_BASE, row_y, 0x10);
    }

    for (int col_x = 2; col_x <= 14; col_x++) {
        frames_put_symbol(0, false, unk_16ED6[col_x] + INNER_BASE, 2, col_x);
        frames_put_symbol(0, false, unk_16EE3[col_x] + INNER_BASE, 14, col_x);
    }

    for (int row_y = 2; row_y <= 14; row_y++) {
        frames_put_symbol(0, false, unk_16F31[row_y] + INNER_BASE, row_y, 2);
        frames_put_symbol(0, false, unk_16F3E[row_y] + INNER_BASE, row_y, 14);
    }

    display_update_start();
}

/* seg037.DrawFrame_WildernessMap (draw8x8_04) */
void frames_draw_wilderness_map(void)
{
    display_update_stop();

    frames_draw_outer();

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, x8x8_07[col_x] + BORDER_BASE, 0x10, col_x);
    }

    display_update_start();
}

/* seg037.draw8x8_05 */
void frames_draw_05(void)
{
    display_update_stop();

    frames_clear_area(0x10, 0x26, 1, 1);

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, outer_frame_top[col_x] + BORDER_BASE, 0, col_x);
    }

    for (int row_y = 0; row_y <= 0x17; row_y++) {
        /* The layout tables only hold 24 rows; row 0x17 is the last. */
        frames_put_symbol(0, false, outer_frame_left[row_y]  + BORDER_BASE, row_y, 0);
        frames_put_symbol(0, false, outer_frame_right[row_y] + BORDER_BASE, row_y, 0x27);
    }

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, outer_frame_bottom[col_x] + BORDER_BASE, 0x17, col_x);
    }

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, x8x8_07[col_x] + BORDER_BASE, 0x10, col_x);
    }

    display_update_start();
}

/* seg037.DrawFrame_Combat (draw8x8_06) */
void frames_draw_combat(void)
{
    display_update_stop();

    frames_clear_area(0x17, 0x27, 0, 0);

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, outer_frame_top[col_x] + BORDER_BASE, 0, col_x);
    }

    for (int row_y = 0; row_y <= 0x16; row_y++) {
        frames_put_symbol(0, false, unk_16F4D[row_y] + BORDER_BASE, row_y, 0);
        frames_put_symbol(0, false, unk_16F64[row_y] + BORDER_BASE, row_y, 0x16);
        frames_put_symbol(0, false, unk_16F7B[row_y] + BORDER_BASE, row_y, 0x27);
    }

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, outer_frame_bottom[col_x] + BORDER_BASE, 0x16, col_x);
    }

    display_update_start();
}

/* seg037.draw8x8_07 */
void frames_draw_07(void)
{
    display_update_stop();

    frames_draw_outer();

    for (int col_x = 0; col_x <= 0x27; col_x++) {
        frames_put_symbol(0, false, x8x8_07[col_x] + BORDER_BASE, 2, col_x);
    }

    display_update_start();
}
