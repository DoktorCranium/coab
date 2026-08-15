/* quit.h - stopping the game from anywhere in the engine.
 * Ported from engine/seg043.print_and_exit, engine/seg001.EngineStop and
 * Logging.Logger.LogAndExit.
 *
 * The C# ran the engine on its own thread and stopped it with Thread.Abort(),
 * which let a call from thirty frames deep unwind without every caller having
 * to expect it. Here the engine is the main thread, so the same shape is kept
 * with a longjmp back to a single exit point in main(): overlay code that has
 * nothing sensible to return keeps calling game_print_and_exit() and does not
 * have to thread a status back up.
 *
 * The exit point has to be established by a macro rather than a function, since
 * a setjmp buffer is only good for as long as the frame that filled it lives.
 */
#ifndef COAB_QUIT_H
#define COAB_QUIT_H

#include <setjmp.h>

#include "coab.h"

/* Marks the caller's frame as where the engine unwinds to. Evaluates to false
 * the first time and true when reached again by game_print_and_exit():
 *
 *     if (QUIT_SET_EXIT_POINT()) {
 *         ... the engine stopped ...
 *     } else {
 *         program_run();
 *     }
 *
 * Only the frame that runs the engine may use this, and it must outlive the
 * engine. */
#define QUIT_SET_EXIT_POINT() (setjmp(*quit_jmp_buf()) != 0)

jmp_buf *quit_jmp_buf(void);

/* seg043.print_and_exit - silences the sound and unwinds to the exit point.
 * Never returns. Re-entrant calls are ignored, as the original's
 * in_print_and_exit flag arranged: the sound and the unwind happen once.
 *
 * With no exit point set - the self test, or a tool linking this in - the
 * process exits instead.
 *
 * The C# also called ItemLibrary.Write() here, which was an extra of that port
 * for dumping item names to disk and has no counterpart in the game. */
void game_print_and_exit(void) __attribute__((noreturn));

/* Logger.LogAndExit - logs at error level, then as above. */
void game_log_and_exit(const char *fmt, ...)
    __attribute__((format(printf, 1, 2), noreturn));

/* True once an exit is under way, for cleanup code that must not start
 * anything new. */
bool game_is_exiting(void);

#endif /* COAB_QUIT_H */
