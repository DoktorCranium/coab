/* text.h - the 8x8 font and text output. Ported from engine/seg041.cs.
 *
 * The font is block 201 of 8X8D1.DAX: 177 glyphs of 8 rows, one bit per pixel.
 * A character's glyph index is toupper(ch) % 0x40, so the font covers a single
 * 64-entry page and lower case simply maps onto upper case.
 *
 * All coordinates are 8x8 cells: columns 0..39, rows 0..24.
 */
#ifndef COAB_TEXT_H
#define COAB_TEXT_H

#include "coab.h"

/* seg041.Load8x8Tiles - fills gbl.dax_8x8d1_201. Returns false if the font
 * block is missing, which leaves the game unable to draw any text. */
bool text_load_8x8_tiles(void);

/* seg041.display_char01 - draws one glyph repeat_count times horizontally. */
void text_display_char(char ch, int repeat_count, int bg_color, int fg_color,
                       int y_col, int x_col);

/* seg041.displayString */
void text_display_string(const char *str, int bg_color, int fg_color,
                         int y_col, int x_col);

/* seg041.displaySpaceChar */
void text_display_space_char(int y_col, int x_col);

/* seg041.ClearScreen - fills the whole 40x25 grid with color 0. */
void text_clear_screen(void);

/* seg041.press_any_key - word-wraps `text` into a region, pausing for a
 * keypress whenever the region fills up. The cursor is gbl.text_x_col /
 * gbl.text_y_col and persists across calls. */
void text_press_any_key_region(const char *text, bool clear_area, int fg_color,
                               TextRegion region);
void text_press_any_key(const char *text, bool clear_area, int fg_color,
                        int y_end, int x_end, int y_start, int x_start);

/* seg041.DisplayAndPause */
void text_display_and_pause(const char *txt, u8 fg_color);

/* seg041.DisplayStatusText */
void text_display_status(u8 bg_color, u8 fg_color, const char *text);

/* seg041.getUserInputString - reads a line, echoing it at the prompt row.
 * Returns the text upper-cased, written into dst. */
char *text_get_user_input_string(char *dst, size_t dst_size, u8 input_len,
                                 u8 bg_color, u8 fg_color, const char *prompt);

/* seg041.getUserInputShort */
u16 text_get_user_input_short(u8 bg_color, u8 fg_color, const char *prompt);

/* seg041.GameDelay */
void text_game_delay(void);

/* seg041.time01 - centiseconds since midnight. */
int text_time01(void);

/* The three text regions, as {y_end, x_end, y_start, x_start}
 * (engine/seg041.cs: bounds). */
extern const int TEXT_BOUNDS[3][4];

#endif /* COAB_TEXT_H */
