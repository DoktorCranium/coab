/* protect.c - Ported from engine/ovr004.cs. */
#include <stdio.h>
#include <string.h>

#include "protect.h"

#include "frames.h"
#include "gbl.h"
#include "icons.h"
#include "input.h"
#include "log.h"
#include "quit.h"
#include "rnd.h"
#include "sound.h"
#include "text.h"

/* ovr004.codeWheel. Six rings of 36 characters; the last is the ring the player
 * reads the answer off, the other five are the paths through it. */
#define CODE_WHEEL_RINGS   6
#define CODE_WHEEL_LETTERS 36

static const char code_wheel[CODE_WHEEL_RINGS][CODE_WHEEL_LETTERS + 1] = {
    "CWLNRTESSCEDCSHSISERRRNSHSSTSSNNHSHN",
    "LAASRDAIILIDSUGADAEEOEGRLSELIITESOIO",
    "LRUNIMMORIIGRRIUPTIIUELIMLHMIXACGRIL",
    "Z0LIOHEUVNODSGEOGXYWISIOCRARLRARRHOI",
    "AMTELRLUIYNAEOOITOUELRREREUIMADPPFAB",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"
};

char protect_wheel_char(int espruar, int dethek, int code_path, int code_row)
{
    /* The C# shifted (5 - code_row) left by one. Multiplied here instead: a box
     * past the sixth makes that negative, and shifting a negative value left is
     * undefined in C where it was merely odd in C#. The two agree everywhere the
     * shift was defined. */
    int code_index = espruar + 0x22 - dethek + (code_path * 12) +
                     ((5 - code_row) * 2);

    /* The wheel wraps both ways. With the values the game generates - espruar
     * 0..25, dethek 0..21 - the index never goes below 13, so only the second
     * loop ever runs; both are kept because both were there. */
    while (code_index < 0) {
        code_index += CODE_WHEEL_LETTERS;
    }
    while (code_index > CODE_WHEEL_LETTERS - 1) {
        code_index -= CODE_WHEEL_LETTERS;
    }

    if (code_row < 0 || code_row >= CODE_WHEEL_RINGS) {
        log_warn("copy protection: no box %d on the wheel", code_row);
        return '\0';
    }

    return code_wheel[code_row][code_index];
}

void protect_copy_protection(void)
{
    int attempt = 0;
    char input_expected = '\0';
    char input_key = '\0';

    /* The two rune sets: 0x1a espruar, then 0x16 dethek after them. */
    icons_load_24x24_set(0x1a, 0, 1, "tiles");
    icons_load_24x24_set(0x16, 0x1a, 2, "tiles");

    frames_draw_outer();

    text_display_string("Align the espruar and dethek runes", 0, 10, 2, 3);
    text_display_string("shown below, on translation wheel", 0, 10, 3, 3);
    text_display_string("like this:", 0, 10, 4, 3);

    do {
        int var_6 = rnd_int(26);
        int var_7 = rnd_int(22);
        int code_path;
        int code_row;
        const char *code_path_str;
        char text[64];
        char input[16];

        icons_draw_iso_tile(var_6, 3, 0x11);
        icons_draw_iso_tile(var_7 + 0x1a, 7, 0x11);

        /* seg040.DrawOverlay() went here; it does nothing. */
        code_path = rnd_int(3);

        switch (code_path) {
        case 0:
            code_path_str = "-..-..-..";
            break;
        case 1:
            code_path_str = "- - - - -";
            break;
        case 2:
            code_path_str = ".........";
            break;
        default:
            code_path_str = "";
            break;
        }

        code_row = rnd_int(6);

        snprintf(text, sizeof(text), "Type the character in box number %d",
                 6 - code_row);
        text_display_string(text, 0, 10, 12, 3);

        text_display_string("under the ", 0, 10, 13, 3);
        text_display_string(code_path_str, 0, 15, 13, 14);
        text_display_string("path.", 0, 10, 13, 0x19);

        input_expected = protect_wheel_char(var_6, var_7, code_path, code_row);

        text_get_user_input_string(input, sizeof(input), 1, 0, 13,
                                   "type character and press return: ");

        input_key = (input[0] == '\0') ? ' ' : input[0];
        attempt++;

        if (input_key != input_expected) {
            text_display_status(0, 14, "Sorry, that's incorrect.");
        } else {
            return;
        }
    } while (input_key != input_expected && attempt < 3);

    /* Three wrong answers: the game makes a noise, says so, and quits. */
    if (attempt >= 3) {
        sound_play(SOUND_1);
        sound_play(SOUND_5);
        gbl.game_speed_var = 9;
        text_display_status(0, 14, "An unseen force hurls you into the abyss!");
        input_sys_delay(0x3e8);
        game_print_and_exit();
    }
}
