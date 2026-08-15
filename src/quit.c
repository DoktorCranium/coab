/* quit.c - Ported from engine/seg043.print_and_exit and engine/seg001.EngineStop. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "quit.h"

#include "log.h"
#include "sound.h"

static jmp_buf exit_point;
static bool    exit_point_set;
static bool    in_print_and_exit;   /* seg043.in_print_and_exit */

jmp_buf *quit_jmp_buf(void)
{
    exit_point_set = true;
    return &exit_point;
}

bool game_is_exiting(void)
{
    return in_print_and_exit;
}

void game_print_and_exit(void)
{
    if (!in_print_and_exit) {
        in_print_and_exit = true;
        sound_play(SOUND_FF);
    }

    if (exit_point_set) {
        longjmp(exit_point, 1);
    }

    /* Nothing to unwind to: the log is closed here because main() is not going
     * to get the chance. */
    log_close();
    exit(EXIT_SUCCESS);
}

void game_log_and_exit(const char *fmt, ...)
{
    char msg[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    log_error("%s", msg);
    game_print_and_exit();
}
