/* prompt.c - Ported from engine/ovr027.cs. */
#include "prompt.h"

#include "dax.h"
#include "display.h"
#include "draw.h"
#include "frames.h"
#include "input.h"
#include "log.h"
#include "mapcursor.h"
#include "picture.h"
#include "platform.h"
#include "quit.h"
#include "text.h"

#include <stdio.h>
#include <string.h>

/* ovr027.highlightable_text: the digits and the capitals, and nothing else. A
 * prompt's words therefore start exactly where its capitals are, and the lower
 * case rest of a word is never mistaken for the start of another. */
static bool is_highlightable(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z');
}

/* unk_6C398: alpha_number_input, the keys that can pick a word. */
static bool is_alpha_number_input(char ch)
{
    return ch == ' ' || is_highlightable(ch);
}

/* unk_6C3B8: number_input, the keys that count as movement when the caller
 * accepts control keys. The backslash is in the original's set too. */
static bool is_number_input(char ch)
{
    return (ch >= '1' && ch <= '9') || ch == '\\';
}

/* The number keys stand in for the cursor keys, and the engine wants them as the
 * scan-code letter the real cursor keys arrive as: 'G' home, 'H' up, 'I' page
 * up, 'K' left, 'M' right, 'O' end, 'P' down, 'Q' page down. '5' is the middle
 * of the keypad and means "no direction". */
static const char keypad_ctrl_codes[9] = {
    'O', 'P', 'Q', 'K', ' ', 'M', 'G', 'H', 'I'
};

static char to_upper(char ch)
{
    return (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
}

/* platform_ticks() wraps roughly every 49 days. The unsigned subtraction still
 * gives the right difference across the wrap, so comparisons are done on the
 * difference rather than on the two values. */
static bool time_reached(u32 now, u32 deadline)
{
    return (i32)(now - deadline) >= 0;
}

/* The city cursor blinks only on the wilderness map, only while the map picture
 * itself is on screen (BIGPIC block 0x79), and not while a city close-up (block
 * 0x50) is being shown over it. All four places the original tested this are
 * the same test. */
static bool map_cursor_active(void)
{
    return gbl.game_state == GAME_STATE_WILDERNESS_MAP &&
           gbl.bigpic_block_id == 0x79 &&
           gbl.last_dax_block_id != 0x50;
}

static i16 area_picture_fade(void)
{
    return (gbl.area_ptr != NULL) ? gbl.area_ptr->picture_fade : 0;
}

/* sub_6C0DA */
void prompt_build_input_keys(PromptHighlightSet *set, const char *menu_text,
                             int *out_count)
{
    int index = 0;
    int length;

    if (set == NULL) {
        return;
    }

    for (int i = 0; i < PROMPT_HIGHLIGHT_MAX; i++) {
        set->word[i].start = -1;
        set->word[i].end = -1;
    }

    if (menu_text == NULL) {
        menu_text = "";
    }
    length = (int)strlen(menu_text);

    for (int idx = 0; idx < length; idx++) {
        if (!is_highlightable(menu_text[idx])) {
            continue;
        }

        if (set->word[index].start == -1) {
            set->word[index].start = idx;
        } else {
            /* The word ends two columns before the next one starts: one for the
             * space between them and one because `end` is inclusive. */
            set->word[index].end = idx - 2;

            if (index + 1 >= PROMPT_HIGHLIGHT_MAX) {
                /* The C# would have run off the end of its 20-entry array. No
                 * prompt in the game has this many words. */
                log_warn("prompt: more than %d words in \"%s\"",
                         PROMPT_HIGHLIGHT_MAX, menu_text);
                break;
            }

            index++;
            set->word[index].start = idx;
        }
    }

    /* The last word runs to the end of the string. */
    set->word[index].end = length;

    if (out_count != NULL) {
        *out_count = index + 1;
    }
}

/* sub_6C1E9 */
void prompt_display_highlighted_text(int highlighted_word, int highlight_fg_color,
                                     const char *text, int x_offset, int fg_color,
                                     const PromptHighlightSet *highlights)
{
    int length;
    int start = -1;
    int end = -1;

    if (text == NULL || highlights == NULL) {
        return;
    }
    length = (int)strlen(text);
    if (length == 0) {
        return;
    }

    if (highlighted_word >= 0 && highlighted_word < PROMPT_HIGHLIGHT_MAX) {
        start = highlights->word[highlighted_word].start;
        end = highlights->word[highlighted_word].end;
    } else {
        /* Out of range means no word is highlighted, rather than the C#'s throw.
         * displayInput keeps the index inside the count, so this is only reached
         * if a caller asks for a word that was never built. */
        log_warn("prompt: no word %d in \"%s\"", highlighted_word, text);
    }

    for (int i = 0; i < length; i++) {
        if (start <= i && end >= i && highlight_fg_color != 0) {
            /* The highlighted word is drawn in reverse: the highlight colour
             * behind, black on top. */
            text_display_char(text[i], 1, highlight_fg_color, 0, 0x18,
                              x_offset + i);
        } else if (is_highlightable(text[i])) {
            /* Every other word's first letter still gets the highlight colour,
             * which is how the player knows which letter to type. */
            text_display_char(text[i], 1, 0, highlight_fg_color, 0x18,
                              x_offset + i);
        } else {
            text_display_char(text[i], 1, 0, fg_color, 0x18, x_offset + i);
        }
    }

    if (length + x_offset < 0x27) {
        text_display_char(' ', (0x27 - length - x_offset) + 1, 0, 0, 0x18,
                          x_offset + length);
    }

    display_update();
}

char prompt_display_input_simple(bool use_overlay, u8 accept_ctrl_keys,
                                 MenuColorSet colors, const char *input_string,
                                 const char *extra_string)
{
    bool ignored;

    return prompt_display_input(&ignored, use_overlay, accept_ctrl_keys, colors,
                                input_string, extra_string);
}

/* The comma and full stop of ovr027.displayInput, which walk the highlighted word
 * along the prompt and wrap at either end. Lifted out of the two branches that
 * had it so the left and right cursor keys can reach the same code; step is -1 for
 * ',' and 1 for '.'. */
static void walk_selected_word(int step, int highlight_count,
                               MenuColorSet colors, const char *input_string,
                               int x_offset, const PromptHighlightSet *highlights)
{
    if (highlight_count <= 0) {
        return;
    }

    if (step < 0) {
        if (gbl.menu_selected_word == 0) {
            gbl.menu_selected_word = highlight_count - 1;
        } else {
            gbl.menu_selected_word--;
        }
    } else {
        gbl.menu_selected_word++;

        if (gbl.menu_selected_word >= highlight_count) {
            gbl.menu_selected_word = 0;
        }
    }

    prompt_display_highlighted_text(gbl.menu_selected_word, colors.highlight,
                                    input_string, x_offset, colors.foreground,
                                    highlights);
}

/* See prompt.h: the one divergence a player is meant to notice. */
char prompt_selection_key(char key, bool from_cursor_key)
{
    if (from_cursor_key == false) {
        return key;
    }

    if (key == 'H') {           /* up, scan code 0x48 */
        return 'G';             /* what home reads as */
    }

    if (key == 'P') {           /* down, scan code 0x50 */
        return 'O';             /* end */
    }

    return key;
}

char prompt_display_input(bool *out_special_key, bool use_overlay,
                          u8 accept_ctrl_keys, MenuColorSet colors,
                          const char *input_string, const char *extra_string)
{
    PromptHighlightSet highlights;
    int highlight_count = 0;
    int input_length;
    int x_offset;
    /* var_8F: with both text colours zero nothing was drawn, so Return has no
     * highlighted word to act on and is passed through instead. */
    bool has_visible_text;
    char input_key = '\0';
    bool special_key = false;
    bool stop_loop = false;
    u32 time_start, time_cursor_on, time_cursor_off;

    if (input_string == NULL) {
        input_string = "";
    }
    if (extra_string == NULL) {
        extra_string = "";
    }
    input_length = (int)strlen(input_string);

    gbl.display_input_special_key_pressed = false;
    has_visible_text = (colors.foreground != 0) || (colors.highlight != 0);

    prompt_build_input_keys(&highlights, input_string, &highlight_count);

    if (gbl.menu_selected_word >= highlight_count) {
        gbl.menu_selected_word = 0;
    }

    time_start = platform_ticks();
    time_cursor_on = time_start + 300;
    time_cursor_off = time_cursor_on + 500;

    if (extra_string[0] != '\0') {
        text_display_string(extra_string, 0, colors.prompt, 0x18, 0);
    }

    x_offset = (int)strlen(extra_string);

    prompt_display_highlighted_text(gbl.menu_selected_word, colors.highlight,
                                    input_string, x_offset, colors.foreground,
                                    &highlights);

    if (map_cursor_active()) {
        map_cursor_set_position(gbl.area_ptr->current_city);
        map_cursor_draw();
        map_cursor_restore();
    }

    do {
        u32 now = platform_ticks();

        /* This loop polls rather than blocking in a key read, so it is the one
         * place a closed window would otherwise go unnoticed: nothing arrives to
         * be read, and seg049.KEYPRESSED keeps answering false. seg043.READKEY
         * answers a closed window with key 3 and seg043.GetInputKey exits on it;
         * the same thing is done here, so the game unwinds from a menu the way
         * the C#'s Thread.Abort() did from anywhere. */
        if (input_quit_requested()) {
            game_print_and_exit();
        }

        if (map_cursor_active() && time_reached(now, time_cursor_on)) {
            map_cursor_draw();
            /* Both deadlines are pushed off the other one, so the cursor is on
             * for 500 ms and off for 300. */
            time_cursor_on = time_cursor_off + 300;
        }

        if ((area_picture_fade() != 0 || use_overlay) &&
            gbl.pic_frames.cur_frame > 0) {
            picture_draw_maybe_overlayed(dax_array_current_picture(&gbl.pic_frames),
                                        use_overlay, 3, 3);

            /* The stored delay is in tenths of a second. */
            int delay = dax_array_current_delay(&gbl.pic_frames) * 100;

            if ((int)(now - time_start) >= delay || area_picture_fade() != 0) {
                dax_array_next_frame(&gbl.pic_frames);
                time_start = now;
            }
        }

        if (gbl.display_input_seconds_to_wait > 0 &&
            (int)(now - time_start) >= gbl.display_input_seconds_to_wait * 1000) {
            /* An unattended prompt answers itself; the demo and the attract mode
             * drive the game this way. */
            input_key = gbl.display_input_timeout_value;
            stop_loop = true;
        } else if (input_key_pressed()) {
            input_key = (char)input_get_key();

            if (input_key == 0) {
                /* An extended key: the scan code follows the zero byte. */
                input_key = (char)input_get_key();

                if (accept_ctrl_keys != PROMPT_CTRL_KEYS &&
                    (input_key == 'K' || input_key == 'M')) {
                    /* Left and right walk the highlighted word instead of being
                     * handed back: the same code the comma and the full stop
                     * run. Everything but PROMPT_CTRL_KEYS gets this - a screen
                     * that asked for the cursor keys is one that turns or moves
                     * with left and right, and a screen that asked for none of
                     * them was throwing these two away. */
                    walk_selected_word(input_key == 'K' ? -1 : 1,
                                       highlight_count, colors, input_string,
                                       x_offset, &highlights);
                } else if (accept_ctrl_keys != 0) {
                    special_key = true;
                    stop_loop = true;
                }
            } else if (input_key == 0x1b) {
                stop_loop = true;
                input_key = '\0';
            } else if (input_key == 13) {
                if (has_visible_text) {
                    if (highlights.word[gbl.menu_selected_word].start != -1) {
                        input_key = input_string[
                            highlights.word[gbl.menu_selected_word].start];
                    } else {
                        input_key = '\r';
                    }

                    stop_loop = true;
                }
            } else if (input_key == ',') {
                walk_selected_word(-1, highlight_count, colors, input_string,
                                   x_offset, &highlights);
            } else if (input_key == '.') {
                walk_selected_word(1, highlight_count, colors, input_string,
                                   x_offset, &highlights);
            } else {
                input_key = to_upper(input_key);

                if (is_alpha_number_input(input_key)) {
                    if (input_key == ' ') {
                        stop_loop = true;
                    } else {
                        /* Walks the whole prompt rather than stopping at the
                         * first match, as the original did: with a repeated
                         * letter the last word wins. */
                        for (int i = 0; i < input_length; i++) {
                            if (input_string[i] != input_key) {
                                continue;
                            }

                            stop_loop = true;

                            for (int word = 0; word < PROMPT_HIGHLIGHT_MAX; word++) {
                                if (highlights.word[word].start != i) {
                                    continue;
                                }

                                gbl.menu_selected_word = word;

                                prompt_display_highlighted_text(
                                    gbl.menu_selected_word, colors.highlight,
                                    input_string, x_offset, colors.foreground,
                                    &highlights);
                                break;
                            }
                            /* A letter in the middle of a word leaves the
                             * highlight where it was; the original searched for
                             * a matching word start until it fell off the end of
                             * the array. */
                        }
                    }
                }

                if (accept_ctrl_keys != 0 && is_number_input(input_key)) {
                    if (input_key == 'W') {
                        /* Unreachable: 'W' is not one of number_input's members.
                         * Kept because the original had it. */
                        input_key = '7';
                        special_key = true;
                        stop_loop = true;
                    } else if (input_key >= '1' && input_key <= '9') {
                        input_key = keypad_ctrl_codes[input_key - '1'];

                        if (accept_ctrl_keys == PROMPT_CTRL_WORD_ARROWS &&
                            (input_key == 'K' || input_key == 'M')) {
                            /* '4' and '6' stand in for the keypad's left and
                             * right, so they walk the highlight here too rather
                             * than coming back as keys the caller has no use
                             * for. */
                            walk_selected_word(input_key == 'K' ? -1 : 1,
                                               highlight_count, colors,
                                               input_string, x_offset,
                                               &highlights);
                        } else {
                            special_key = true;
                            stop_loop = true;
                        }
                    } else {
                        /* The backslash is in number_input but 0x5c - 0x31 is
                         * well past the nine codes, so the original indexed out
                         * of its array. Ignoring the key is the benign reading. */
                        log_warn("prompt: no keypad code for '%c'", input_key);
                    }
                }
            }
        }

        now = platform_ticks();

        if (map_cursor_active() && time_reached(now, time_cursor_off)) {
            map_cursor_restore();

            time_cursor_off = time_cursor_on + 500;
        }

        input_sys_delay(20);
    } while (!stop_loop);

    if (gbl.area_ptr != NULL) {
        /* One pass of the fade per prompt: whatever was faded above stays faded,
         * and the next picture is drawn normally. */
        gbl.area_ptr->picture_fade = 0;
    }

    if (map_cursor_active()) {
        map_cursor_restore();
    }

    gbl.display_input_special_key_pressed = special_key;

    if (out_special_key != NULL) {
        *out_special_key = special_key;
    }

    return input_key;
}

void prompt_clear_area(void)
{
    prompt_clear_area_no_update();

    display_update();
}

void prompt_clear_area_no_update(void)
{
    draw_rectangle(0, 0x18, 0x27, 0x18, 0);
}

/* ------------------------------------------------------------ scrolling lists */

/* C#'s string.Trim(): drops the spaces at both ends. The menu text is padded on
 * the left to indent an entry under its heading, and that padding is added back
 * by the caller as a column offset. */
static const char *trim_copy(char *dst, size_t dst_size, const char *text)
{
    size_t start = 0;
    size_t end;

    if (dst_size == 0) {
        return "";
    }
    while (text[start] == ' ') {
        start++;
    }
    end = strlen(text);
    while (end > start && text[end - 1] == ' ') {
        end--;
    }

    if (end - start >= dst_size) {
        end = start + dst_size - 1;
    }
    memcpy(dst, text + start, end - start);
    dst[end - start] = '\0';

    return dst;
}

/* ovr027.getBegingOfString: how far the entry is indented. */
static int begin_of_string(const char *text)
{
    int spaces = 0;

    while (text[spaces] == ' ') {
        spaces++;
    }
    return spaces;
}

/* sub_6C897: draws the page of the list starting at `index`. */
static void draw_list_page(int index, int y_end, int x_end, int y_start,
                           int x_start, MenuList *list, int normal_color,
                           int heading_color, int display_fill_width)
{
    int count;

    frames_clear_area(y_end, x_end, y_start, x_start);

    count = COAB_MIN(y_end - y_start + 1, list->count - index);

    for (int i = 0; i < count; i++) {
        MenuItem *menu = menu_list_get(list, index + i);
        int y_col = y_start + i;
        int length;

        if (menu == NULL) {
            break;
        }
        length = (int)strlen(menu->text);

        text_display_string(menu->text, 0,
                            menu->heading ? heading_color : normal_color,
                            y_col, x_start);

        if (length < display_fill_width) {
            text_display_char(' ', display_fill_width - length, 0, 0, y_col,
                              length + x_start);
        }
    }
}

static void list_item_highlighted(int index, MenuList *list, int y_col,
                                  int x_col, int bg_color)
{
    MenuItem *menu_item = menu_list_get(list, index);
    char trimmed[MENU_ITEM_TEXT_MAX];

    if (menu_item == NULL) {
        return;
    }

    text_display_string(trim_copy(trimmed, sizeof(trimmed), menu_item->text),
                        bg_color, 0,
                        y_col + (index - gbl.menu_screen_index),
                        x_col + begin_of_string(menu_item->text));
}

static void list_item_normal(int index, MenuList *list, int y_col, int x_col,
                             int normal_color, int heading_color)
{
    MenuItem *menu_item = menu_list_get(list, index);
    char trimmed[MENU_ITEM_TEXT_MAX];

    if (menu_item == NULL) {
        return;
    }

    text_display_string(trim_copy(trimmed, sizeof(trimmed), menu_item->text),
                        0, menu_item->heading ? heading_color : normal_color,
                        y_col + (index - gbl.menu_screen_index),
                        x_col + begin_of_string(menu_item->text));
}

/* sub_6CC08: a heading cannot be selected, so step past it in the direction the
 * player was moving, wrapping inside the visible page. */
static int skip_headings(bool forwards, int index, MenuList *list,
                         int list_display_height)
{
    int stepped = 0;

    while (stepped < list_display_height) {
        MenuItem *item = menu_list_get(list, index);

        if (item == NULL || !item->heading) {
            /* NULL is out of range; the C# indexer would have thrown. Stopping
             * leaves the caller's bounds checks to deal with it. */
            break;
        }

        stepped++;

        if (forwards) {
            index += 1;

            if ((gbl.menu_screen_index + list_display_height - 1) < index) {
                index = gbl.menu_screen_index;
            }
            if ((list->count - 1) < index) {
                index = gbl.menu_screen_index;
            }
        } else {
            index -= 1;

            if (index < gbl.menu_screen_index) {
                index = gbl.menu_screen_index + list_display_height - 1;
            }
            if ((list->count - 1) < index) {
                index = list->count - 1;
            }
        }
    }

    return index;
}

/* sub_6CD38: a whole page up or down. */
static void menu_scroll_page(bool forwards, int *index, MenuList *list,
                             int list_display_height, int y_end, int x_end,
                             int y_start, int x_start, int normal_color,
                             int heading_color, int display_fill_width)
{
    int screen_offset = *index - gbl.menu_screen_index;

    if (forwards) {
        gbl.menu_screen_index += list_display_height;

        if ((list->count - list_display_height) < gbl.menu_screen_index) {
            gbl.menu_screen_index = list->count - list_display_height;
        }
    } else {
        gbl.menu_screen_index -= list_display_height;

        if (gbl.menu_screen_index < 0) {
            gbl.menu_screen_index = 0;
        }
    }

    /* A list shorter than the page can end up here with a negative first row;
     * the original had the same arithmetic, and drawing simply starts at the
     * top of the list. */
    if (gbl.menu_screen_index < 0) {
        gbl.menu_screen_index = 0;
    }

    *index = gbl.menu_screen_index + screen_offset;
    *index = skip_headings(forwards, *index, list, list_display_height);

    draw_list_page(gbl.menu_screen_index, y_end, x_end, y_start, x_start, list,
                   normal_color, heading_color, display_fill_width);
}

/* sub_6CDCA: one entry up or down, wrapping inside the page. */
static int menu_scroll_in_page(bool forwards, int index, MenuList *list,
                               int list_display_height)
{
    if (forwards) {
        index += 1;

        if ((gbl.menu_screen_index + list_display_height - 1) < index) {
            index = gbl.menu_screen_index;
        }
        if ((list->count - 1) < index) {
            index = gbl.menu_screen_index;
        }
    } else {
        index -= 1;

        if (index < gbl.menu_screen_index) {
            index = gbl.menu_screen_index + list_display_height - 1;
        }
        if ((list->count - 1) < index) {
            index = list->count - 1;
        }
    }

    return skip_headings(forwards, index, list, list_display_height);
}

char prompt_select_item(MenuItem **result, int *index, bool *redraw_menu_items,
                        bool show_exit, MenuList *list,
                        int end_y, int end_x, int start_y, int start_x,
                        MenuColorSet colors, const char *input_string,
                        const char *extra_text)
{
    char ret_val = '\0';
    int list_display_width;
    int list_display_height;
    int list_count;
    bool loop_end = false;

    if (result != NULL) {
        *result = NULL;
    }
    if (index == NULL || redraw_menu_items == NULL || list == NULL) {
        if (index != NULL) {
            *index = 0;
        }
        return '\0';
    }

    gbl.menu_selected_word = 1;

    list_display_width = (end_x - start_x) + 1;
    list_display_height = (end_y - start_y) + 1;
    list_count = list->count;

    if (list_count <= list_display_height) {
        gbl.menu_screen_index = 0;
    }

    if (gbl.menu_screen_index > *index) {
        gbl.menu_screen_index = *index;
        *redraw_menu_items = true;
    }

    if (gbl.menu_screen_index > list_count) {
        gbl.menu_screen_index = 0;
        *redraw_menu_items = true;
    }

    /* Step onto the next selectable entry: the caller passes the entry it left
     * the highlight on, and a heading is never selectable. */
    (*index)++;
    *index = menu_scroll_in_page(false, *index, list, list_display_height);

    if (*redraw_menu_items) {
        draw_list_page(gbl.menu_screen_index, end_y, end_x, start_y, start_x,
                       list, colors.foreground, colors.prompt,
                       list_display_width);
    }

    *redraw_menu_items = false;

    while (!loop_end) {
        char display_string[MENU_ITEM_TEXT_MAX + 32];
        bool show_next = false;
        bool show_previous = false;
        bool special_key = false;
        char input_key;

        list_item_highlighted(*index, list, start_y, start_x, colors.highlight);

        snprintf(display_string, sizeof(display_string), "%s",
                 input_string != NULL ? input_string : "");

        if ((list_count - list_display_height) > gbl.menu_screen_index) {
            strncat(display_string, " Next",
                    sizeof(display_string) - strlen(display_string) - 1);
            show_next = true;
        }

        if (gbl.menu_screen_index > 0) {
            strncat(display_string, " Prev",
                    sizeof(display_string) - strlen(display_string) - 1);
            show_previous = true;
        }

        if (show_exit) {
            strncat(display_string, " Exit",
                    sizeof(display_string) - strlen(display_string) - 1);
        }

        input_key = prompt_display_input(&special_key, false,
                                        PROMPT_CTRL_WORD_ARROWS, colors,
                                        display_string, extra_text);

        list_item_normal(*index, list, start_y, start_x, colors.foreground,
                         colors.prompt);

        if (special_key) {
            /* The cursor keys, as their scan-code letters. Up and Down arrive
             * here as Home and End; see prompt_selection_key. */
            switch (prompt_selection_key(input_key, special_key)) {
            case 'G':   /* home / up / '7': previous entry */
                *index = menu_scroll_in_page(false, *index, list,
                                             list_display_height);
                break;

            case 'O':   /* end / down / '1': next entry */
                *index = menu_scroll_in_page(true, *index, list,
                                             list_display_height);
                break;

            case 'I':   /* page up */
                if (show_previous) {
                    menu_scroll_page(false, index, list, list_display_height,
                                     end_y, end_x, start_y, start_x,
                                     colors.foreground, colors.prompt,
                                     list_display_width);
                }
                break;

            case 'Q':   /* page down */
                if (show_next) {
                    menu_scroll_page(true, index, list, list_display_height,
                                     end_y, end_x, start_y, start_x,
                                     colors.foreground, colors.prompt,
                                     list_display_width);
                }
                break;

            default:
                break;
            }
        } else {
            switch (input_key) {
            case 'P':   /* the "Prev" word of the prompt */
                menu_scroll_page(false, index, list, list_display_height,
                                 end_y, end_x, start_y, start_x,
                                 colors.foreground, colors.prompt,
                                 list_display_width);
                break;

            case 'N':   /* "Next" */
                menu_scroll_page(true, index, list, list_display_height,
                                 end_y, end_x, start_y, start_x,
                                 colors.foreground, colors.prompt,
                                 list_display_width);
                break;

            case 0x1b:
            case '\0':
            case 'E':   /* "Exit" */
                if (result != NULL) {
                    *result = NULL;
                }
                ret_val = '\0';
                loop_end = true;
                break;

            default:
                if (result != NULL) {
                    *result = menu_list_get(list, *index);
                }
                ret_val = input_key;
                loop_end = true;
                break;
            }
        }
    }

    return ret_val;
}

char prompt_yes_no(MenuColorSet colors, const char *input_string)
{
    char input_key;

    /* Two is past the end of a two-word prompt, so displayInput resets it and
     * the highlight starts on "Yes". The original set it the same way. */
    gbl.menu_selected_word = 2;

    do {
        input_key = prompt_display_input_simple(false, 0, colors, "Yes No",
                                               input_string);
    } while (input_key != 'N' && input_key != 'Y');

    return input_key;
}
