/* prompt.h - the prompt line and the scrolling menus.
 * Ported from engine/ovr027.cs.
 *
 * Every choice the game offers goes through here. A prompt is one string of
 * words on row 0x18, of which one word is highlighted:
 *
 *     Exit Save Load Options
 *
 * A word is picked by typing its first letter, by walking the highlight with
 * ',' and '.' and pressing Return, or - for menus - with the cursor keys. Only
 * capitals and digits count as the start of a word, which is what makes a word's
 * extent derivable from the string alone (prompt_build_input_keys).
 *
 * While the prompt waits it also drives the animated picture in the corner and
 * the blinking city cursor on the wilderness map, since the original had nowhere
 * else to do timed work: this loop was the only place that ran while the player
 * was thinking.
 */
#ifndef COAB_PROMPT_H
#define COAB_PROMPT_H

#include "coab.h"
#include "gbl.h"
#include "menu.h"

/* ovr027.HighlightSet: twenty words is the fixed limit the original had. The
 * longest prompt the game builds is well inside it. */
#define PROMPT_HIGHLIGHT_MAX 20

/* ovr027.highlight. -1 for both means "unused"; `end` is the last column the
 * word covers, which for every word but the last is two columns short of the
 * next word's first letter (the original's `idx - 2`). */
typedef struct {
    int start;
    int end;
} PromptHighlight;

typedef struct {
    PromptHighlight word[PROMPT_HIGHLIGHT_MAX];
} PromptHighlightSet;

/* ovr027.BuildInputKeys, sub_6C0DA. Splits menu_text into words at every
 * capital or digit and reports how many it found. */
void prompt_build_input_keys(PromptHighlightSet *set, const char *menu_text,
                             int *out_count);

/* ovr027.display_highlighed_text, sub_6C1E9. Draws the prompt on row 0x18 with
 * word `highlighted_word` shown in reverse, and blanks the rest of the row. */
void prompt_display_highlighted_text(int highlighted_word, int highlight_fg_color,
                                     const char *text, int x_offset, int fg_color,
                                     const PromptHighlightSet *highlights);

/* The values accept_ctrl_keys takes. The C# only ever passes 0 or 1; the third
 * is this port's, and is described at prompt_selection_key below. */
enum {
    PROMPT_CTRL_NONE = 0,        /* cursor keys not handed back; a typed prompt */
    PROMPT_CTRL_KEYS = 1,        /* cursor and number keys handed back to the caller */
    PROMPT_CTRL_WORD_ARROWS = 2  /* as 1, but left and right stay here; see below */
};

/* ovr027.displayInput. Waits for a choice and returns the letter of the word
 * that was picked, or '\0' for Escape.
 *
 * accept_ctrl_keys != 0 also accepts the cursor keys and the number keys as
 * movement; those come back as the scan-code letter ('H' up, 'P' down, ...) with
 * *out_special_key set, so the caller can tell 'P' the direction from 'P' the
 * first letter of "Prev". gbl.display_input_special_key_pressed carries the same
 * flag for callers that read it later. */
char prompt_display_input(bool *out_special_key, bool use_overlay,
                          u8 accept_ctrl_keys, MenuColorSet colors,
                          const char *input_string, const char *extra_string);

/* The overload that throws the flag away. */
char prompt_display_input_simple(bool use_overlay, u8 accept_ctrl_keys,
                                 MenuColorSet colors, const char *input_string,
                                 const char *extra_string);

/* Reports the cursor key that moves a selection: 'H' (up) as 'G' (home) and 'P'
 * (down) as 'O' (end), and everything else unchanged.
 *
 * This is a deliberate divergence, the only one in the port that a player is
 * meant to notice. Main/Keyboard.cs does map Up and Down, to scan codes 0x48 and
 * 0x50, but every screen that moves a selection - ovr027's scrolling list,
 * ovr020.scroll_team_list, the modify-character sheet - switches on Home and End
 * only and lets 0x48 and 0x50 fall through, so on the original the arrow keys did
 * nothing in a menu. That is unusable on a keyboard with no keypad and no
 * dedicated Home and End, which is most Macs. The C# had already reached for the
 * same remedy: Main/Keyboard.cs:39 and :70 alias '[' and ']' onto Home and End,
 * for exactly this reason.
 *
 * Nothing is taken away - Home, End, '[', ']' and the number keys 7 and 1 all
 * still work - and the screens where a cursor key means a direction rather than a
 * selection are untouched, which matters because there 'H' and 'P' already mean
 * forward and about-face and 'G' and 'O' are the diagonals.
 *
 * from_cursor_key must be the flag displayInput reported for this key, so that a
 * menu word like "Pool" or "Prev" is never mistaken for an arrow: with it false
 * the key comes back unchanged. Callers that keep the flag pass their own; the
 * ones that used the displayInput overload without it pass
 * gbl.display_input_special_key_pressed (byte_1D5BF), which is the same flag.
 *
 * Left and right are the other half of the same argument, and are handled a level
 * down rather than here: on a prompt they should do what ',' and '.' do, which is
 * to walk the highlight along the words, and that is displayInput's own business -
 * it never returns for those keys, so there is nothing for a caller to convert.
 * displayInput therefore walks the highlight for 'K' and 'M' unless
 * accept_ctrl_keys is exactly PROMPT_CTRL_KEYS, which is the one case where the
 * caller wanted them: the dungeon turns with left and right, a fight moves with
 * them, resting picks the field to edit, and the modify-character sheet lowers and
 * raises a stat. Those screens keep passing PROMPT_CTRL_KEYS and see 'K' and 'M'
 * as before; a menu that has nothing to do with them passes
 * PROMPT_CTRL_WORD_ARROWS instead, and a prompt that never asked for the cursor
 * keys at all (PROMPT_CTRL_NONE) gets the walk too, since it was dropping those
 * keys on the floor. */
char prompt_selection_key(char key, bool from_cursor_key);

/* ovr027.ClearPromptArea / ClearPromptAreaNoUpdate: blanks row 0x18. */
void prompt_clear_area(void);
void prompt_clear_area_no_update(void);

/* ovr027.sl_select_item. Runs a scrolling list inside the given cell rectangle
 * with `input_string` as the prompt, appending " Next", " Prev" and " Exit" to
 * it as they apply.
 *
 * *index is the entry the highlight starts on and is left on the entry it ended
 * on. *result receives the chosen entry, or NULL when the player backed out;
 * the return value is the key that picked it, '\0' for Escape or Exit.
 * *redraw_menu_items forces the list to be redrawn and is cleared. */
char prompt_select_item(MenuItem **result, int *index, bool *redraw_menu_items,
                        bool show_exit, MenuList *list,
                        int end_y, int end_x, int start_y, int start_x,
                        MenuColorSet colors, const char *input_string,
                        const char *extra_text);

/* ovr027.yes_no. Keeps asking until Y or N; there is no way out. */
char prompt_yes_no(MenuColorSet colors, const char *input_string);

#endif /* COAB_PROMPT_H */
