#include "text.h"
#include "display.h"
#include "draw.h"
#include "frames.h"
#include "gbl.h"
#include "dax.h"
#include "input.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FONT_BLOCK_ID 201
#define FONT_FILE     "8X8d1.dax"

/* {y_end, x_end, y_start, x_start} */
const int TEXT_BOUNDS[3][4] = {
    { 0x16, 0x26, 0x11, 0x01 },   /* NormalBottom */
    { 0x16, 0x26, 0x15, 0x01 },   /* Normal2 */
    { 0x15, 0x26, 0x01, 0x17 }    /* CombatSummary */
};

/* engine/ovr027.cs: ClearPromptAreaNoUpdate - the bottom status row. */
static void clear_prompt_area_no_update(void)
{
    draw_rectangle(0, 0x18, 0x27, 0x18, 0);
}

bool text_load_8x8_tiles(void)
{
    i16 block_size = 0;
    u8 *block = dax_load_decode(FONT_FILE, FONT_BLOCK_ID, &block_size);

    if (!block || block_size == 0) {
        free(block);
        return false;
    }

    for (int i = 0, glyph = 0; i < block_size && glyph < GBL_FONT_GLYPHS;
         i += 8, glyph++) {
        for (int k = 0; k < 8 && (i + k) < block_size; k++) {
            gbl.dax_8x8d1_201[glyph][k] = block[i + k];
        }
    }

    free(block);
    return true;
}

/* engine/seg041.cs: display_char01 */
void text_display_char(char ch, int repeat_count, int bg_color, int fg_color,
                       int y_col, int x_col)
{
    int index;

    if (x_col >= TEXT_COLS || y_col >= TEXT_ROWS) {
        return;
    }

    /* The font holds a single 64-glyph page, so case is folded away and the
     * character is reduced modulo 0x40. */
    index = toupper((unsigned char)ch) % 0x40;

    for (int i = 0; i < 8; i++) {
        gbl.mono_char_data[i] = gbl.dax_8x8d1_201[index][i];
    }

    for (int i = 0; i < repeat_count; i++) {
        display_mono_8x8(x_col + i, y_col, gbl.mono_char_data, bg_color, fg_color);
    }
}

void text_display_space_char(int y_col, int x_col)
{
    if (x_col >= 0 && x_col <= 0x27 && y_col >= 0 && y_col <= 0x18) {
        text_display_char(' ', 1, 0, 0, y_col, x_col);
        display_update();
    }
}

void text_display_string(const char *str, int bg_color, int fg_color,
                         int y_col, int x_col)
{
    if (!str || x_col > 0x27 || y_col > 0x27) {
        return;
    }

    for (const char *p = str; *p; p++) {
        text_display_char(*p, 1, bg_color, fg_color, y_col, x_col);
        x_col++;
    }
    display_update();
}

void text_clear_screen(void)
{
    draw_rectangle(0, 0x18, 0x27, 0, 0);
}

/* engine/seg041.cs: displayStringSlow. Indices are 1-based and the range is
 * inclusive, matching the Pascal-style loop in the original. */
static int display_string_slow(const char *text, int text_index,
                               int text_length, int fg_color)
{
    while (text_index <= text_length) {
        text_display_char(text[text_index - 1], 1, 0, fg_color,
                          gbl.text_y_col, gbl.text_x_col);

        if (gbl.delay_between_characters) {
            input_sys_delay(gbl.game_speed_var * 3);
        }

        text_index += 1;
        gbl.text_x_col++;
    }

    return text_index;
}

/* engine/seg041.cs: text_skip_space */
static void text_skip_space(const char *text, int text_max, int *text_index)
{
    while (*text_index < text_max && text[*text_index - 1] == ' ') {
        *text_index += 1;
    }
}

/* engine/seg041.cs: the `puncutation` set, "!,-.:;?" */
static bool is_punctuation(char c)
{
    switch (c) {
    case '!': case ',': case '-': case '.':
    case ':': case ';': case '?':
        return true;
    default:
        return false;
    }
}

void text_press_any_key_region(const char *text, bool clear_area, int fg_color,
                               TextRegion region)
{
    int r = (int)region;

    if (r < 0 || r > 2) {
        return;
    }
    text_press_any_key(text, clear_area, fg_color,
                       TEXT_BOUNDS[r][0], TEXT_BOUNDS[r][1],
                       TEXT_BOUNDS[r][2], TEXT_BOUNDS[r][3]);
}

/* engine/seg041.cs: press_any_key.
 *
 * Word wrapping with the original's quirks intact: a word is scanned up to the
 * next space, trailing punctuation is pulled along with it, and when the region
 * is full the player is prompted before it scrolls. */
void text_press_any_key(const char *text, bool clear_area, int fg_color,
                        int y_end, int x_end, int y_start, int x_start)
{
    int text_start;
    int input_length;

    if (!text) {
        return;
    }

    if (x_start > 0x27 || y_start > 0x18 ||
        (x_end > 0x27 && y_end > 0x27)) {
        return;
    }

    if (gbl.text_x_col < x_start || gbl.text_x_col > x_end ||
        gbl.text_y_col < y_start || gbl.text_y_col > y_end) {
        gbl.text_x_col = x_start;
        gbl.text_y_col = y_start;
    }

    if (clear_area) {
        frames_clear_area(y_end, x_end, y_start, x_start);
        gbl.text_x_col = x_start;
        gbl.text_y_col = y_start;
    }

    text_start = 1;
    input_length = (int)strlen(text);

    if (input_length == 0) {
        return;
    }

    do {
        int text_end = text_start;

        while (text_end < input_length && is_punctuation(text[text_end - 1])) {
            text_end++;
        }

        while (text_end < input_length &&
               !is_punctuation(text[text_end - 1]) &&
               text[text_end - 1] != ' ') {
            text_end++;
        }

        if (text[text_end - 1] != ' ') {
            while (text_end + 1 < input_length && is_punctuation(text[text_end])) {
                text_end++;
            }
        }

        if (((text_end - text_start) + gbl.text_x_col) > x_end) {
            if (((text_end - text_start) + gbl.text_x_col) == x_end &&
                text[text_end - 1] == ' ') {
                text_end -= 1;
                text_start = display_string_slow(text, text_start, text_end, fg_color);
            }

            gbl.text_x_col = x_start;
            gbl.text_y_col++;
            text_skip_space(text, input_length, &text_start);

            if (gbl.text_y_col > y_end && text_start < input_length) {
                gbl.text_x_col = x_start;
                gbl.text_y_col = y_start;

                text_display_and_pause("Press any key to continue", 13);
                input_clear_keyboard();

                frames_clear_area(y_end, x_end, y_start, x_start);

                text_start = display_string_slow(text, text_start, text_end, fg_color);
            }
        } else {
            text_start = display_string_slow(text, text_start, text_end, fg_color);
            display_update();
        }

        /* The original could not make progress here if a single word was wider
         * than the region; bail out rather than spin. */
        if (gbl.text_y_col > y_end + 1) {
            break;
        }
    } while (text_start <= input_length && !input_quit_requested());

    if (gbl.text_x_col > x_end) {
        gbl.text_x_col = x_start;
        gbl.text_y_col++;
    }
}

void text_display_and_pause(const char *txt, u8 fg_color)
{
    clear_prompt_area_no_update();
    text_display_string(txt, 0, fg_color, 0x18, 0);
    input_get_key();
}

void text_display_status(u8 bg_color, u8 fg_color, const char *text)
{
    clear_prompt_area_no_update();
    text_display_string(text, bg_color, fg_color, 0x18, 0);
    text_game_delay();
    clear_prompt_area_no_update();
}

void text_game_delay(void)
{
    input_sys_delay(gbl.game_speed_var * 100);
}

char *text_get_user_input_string(char *dst, size_t dst_size, u8 input_len,
                                 u8 bg_color, u8 fg_color, const char *prompt)
{
    size_t len = 0;
    int x_pos;
    char ch;

    if (!dst || dst_size == 0) {
        return dst;
    }
    dst[0] = '\0';

    if (input_len >= dst_size) {
        input_len = (u8)(dst_size - 1);
    }

    clear_prompt_area_no_update();
    text_display_string(prompt, bg_color, fg_color, 0x18, 0);

    x_pos = prompt ? (int)strlen(prompt) : 0;

    do {
        ch = (char)input_get_key();

        if (ch >= 0x20 && ch <= 0x7A) {
            if (len < input_len) {
                char one[2] = { ch, '\0' };

                dst[len++] = ch;
                dst[len] = '\0';
                text_display_string(one, 0, 15, 0x18, x_pos++);
            }
        } else if (ch == 8 && len > 0) {
            dst[--len] = '\0';
            text_display_space_char(24, --x_pos);
        }
    } while (ch != 0x0d && ch != 0x1b && !gbl.in_demo && !input_quit_requested());

    clear_prompt_area_no_update();

    for (size_t i = 0; i < len; i++) {
        dst[i] = (char)toupper((unsigned char)dst[i]);
    }
    return dst;
}

u16 text_get_user_input_short(u8 bg_color, u8 fg_color, const char *prompt)
{
    char input[16];
    long value = 0;
    bool good_input;

    do {
        char *end = NULL;

        text_get_user_input_string(input, sizeof(input), 6,
                                   bg_color, fg_color, prompt);

        value = strtol(input, &end, 10);
        good_input = (input[0] != '\0') && end && *end == '\0' &&
                     value >= 0 && value < 0x10000;

        if (input_quit_requested()) {
            return 0;
        }
    } while (!good_input);

    return (u16)value;
}

/* engine/seg041.cs: time01 - centiseconds since midnight. */
int text_time01(void)
{
    struct timespec ts;
    struct tm tm_buf;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 ||
        !localtime_r(&ts.tv_sec, &tm_buf)) {
        return 0;
    }

    return (tm_buf.tm_hour * 360000) + (tm_buf.tm_min * 6000) +
           (tm_buf.tm_sec * 100) + (int)(ts.tv_nsec / 10000000);
}
