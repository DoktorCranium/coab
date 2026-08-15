#include <stdio.h>
#include "input.h"
#include "platform.h"
#include "gbl.h"
#include "cheats.h"
#include "quit.h"
#include "sound.h"

/* engine/seg049.cs: skipReadFlag. Holds the scan code of an extended key whose
 * leading zero byte has already been handed to the caller. */
static u8 g_skip_read_flag;

void input_sys_delay(int milliseconds)
{
    if (milliseconds != 0) {
        platform_delay(milliseconds);
    }
}

bool input_quit_requested(void)
{
    return platform_quit_requested();
}

/* engine/seg049.cs: KEYPRESSED */
bool input_key_pressed(void)
{
    if (g_skip_read_flag != 0) {
        return true;
    }
    platform_pump_events();
    return platform_peek_key() != 0;
}

/* engine/seg049.cs: READKEY.
 *
 * An extended key arrives as (scan_code << 8) with a zero ASCII byte. The first
 * call returns 0 and stashes the scan code; the next returns the scan code.
 * That two-step is how INT 16h behaved and the menu code relies on it. */
u8 input_read_key(void)
{
    u8 last_code = g_skip_read_flag;

    g_skip_read_flag = 0;

    if (last_code == 0) {
        u16 response = platform_pop_key_blocking();

        last_code = (u8)(response & 0x00ff);

        if ((response & 0x00ff) == 0) {
            g_skip_read_flag = (u8)(response >> 8);
            if (g_skip_read_flag == 0) {
                /* No key at all: the window is closing. The original returned
                 * 3 (Ctrl+C) here, which the engine treats as "quit". */
                last_code = 3;
            }
        }
    }

    return last_code;
}

/* engine/seg043.cs: GetInputKey */
u8 input_get_key(void)
{
    u8 key;

    if (gbl.in_demo) {
        /* The demo drives itself and must never block, so a key is read only if
         * one is there. A closed window puts nothing in the queue, so it is
         * checked for separately - otherwise the demo would keep playing to
         * nobody. READKEY reports it as key 3, which is what the branch below
         * acts on. */
        key = input_key_pressed() ? input_read_key() : 0;

        if (key == 0 && platform_quit_requested()) {
            key = 3;
        }
    } else {
        key = input_read_key();
    }

    if (key == 0x13) {
        sound_play(SOUND_0);
    }

    /* seg043.GetInputKey called print_and_exit() here, which aborted the engine
     * thread. The window having been closed gets the same treatment: READKEY
     * reports that as key 3, and there is nothing left to draw on. */
    if (key == 3 && (cheats.allow_keyboard_exit || platform_quit_requested())) {
        platform_request_quit();
        game_print_and_exit();
    }

    /* Drain anything queued behind this key so a held-down key does not run the
     * menu forward several steps. */
    if (key != 0) {
        while (input_key_pressed()) {
            key = input_read_key();
        }
    }

    return key;
}

/* engine/seg043.cs: clear_keyboard */
void input_clear_keyboard(void)
{
    while (input_key_pressed()) {
        input_get_key();
    }
}

/* engine/seg043.cs: clear_one_keypress */
void input_clear_one_keypress(void)
{
    if (input_key_pressed()) {
        input_get_key();
    }
}
