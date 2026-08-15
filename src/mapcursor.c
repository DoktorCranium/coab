/* mapcursor.c - Ported from engine/ovr028.cs. */
#include "mapcursor.h"

#include "draw.h"
#include "gbl.h"
#include "log.h"

/* unk_16D5A */
static const int city_map_x[MAP_CURSOR_CITY_COUNT] = {
    0x04, 0x0c, 0x15, 0x0b, 0x1d, 0x14, 0x26, 0x15,
    0x1e, 0x1f, 0x19, 0x25, 0x1c, 0x1d, 0x03, 0x0c,
    0x19, 0x1d, 0x1d, 0x21, 0x13, 0x10, 0x09, 0x10,
    0x14, 0x15, 0x19, 0x19, 0x1a, 0x1f, 0x25, 0x22, 0x0f
};

/* unk_16D7A */
static const int city_map_y[MAP_CURSOR_CITY_COUNT] = {
    0x0f, 0x08, 0x0b, 0x04, 0x0a, 0x04, 0x01, 0x02,
    0x0d, 0x0f, 0x03, 0x05, 0x02, 0x08, 0x0c, 0x0d,
    0x0a, 0x0c, 0x09, 0x09, 0x08, 0x06, 0x06, 0x03,
    0x02, 0x02, 0x03, 0x02, 0x03, 0x04, 0x02, 0x01, 0x00
};

static int loc_x;   /* word_1EF9C */
static int loc_y;   /* word_1EF9E */

void map_cursor_set_position(int current_city)
{
    if (current_city < 0 || current_city >= MAP_CURSOR_CITY_COUNT) {
        log_warn("MapCursor: no city %d, the map has %d",
                 current_city, MAP_CURSOR_CITY_COUNT);
        return;
    }

    loc_x = city_map_x[current_city];
    loc_y = city_map_y[current_city];
}

void map_cursor_draw(void)
{
    draw_ega_backup(gbl.cursor_bkup, loc_y, loc_x);
    draw_picture(gbl.cursor, loc_y, loc_x, 0);
}

void map_cursor_restore(void)
{
    draw_picture(gbl.cursor_bkup, loc_y, loc_x, 0);
}

void map_cursor_position(int *out_col_x, int *out_row_y)
{
    if (out_col_x != NULL) {
        *out_col_x = loc_x;
    }
    if (out_row_y != NULL) {
        *out_row_y = loc_y;
    }
}
