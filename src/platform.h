/* platform.h - the SDL layer: window, keyboard, audio, timing.
 *
 * The DOS original was single-threaded and blocked inside INT 16h waiting for a
 * key. The C# port reproduced that with an engine thread plus a WinForms UI
 * thread trading a semaphore, and used Thread.Abort to quit. None of that is
 * needed here: the engine runs on the main thread and the platform pumps SDL
 * events whenever the engine presents a frame or waits for input, so the window
 * stays responsive with no locking and no thread to abort.
 */
#ifndef COAB_PLATFORM_H
#define COAB_PLATFORM_H

#include "coab.h"

/* Which backend is compiled. Both platform_sdl.c and platform_sdl1.c are in
 * src/, which the build globs, so each one guards its body with these and the
 * unselected one compiles to nothing. Define COAB_SDL1=1 to build against
 * SDL-1.2 (OpenVMS, and anywhere else with no SDL2); SDL2 otherwise. */
#ifndef COAB_SDL1
#define COAB_SDL1 0
#endif
#if COAB_SDL1
#undef  COAB_SDL2
#define COAB_SDL2 0
#else
#undef  COAB_SDL2
#define COAB_SDL2 1
#endif

typedef struct {
    bool headless;      /* skip SDL entirely; used by --self-test */
    bool no_audio;
    bool fullscreen;
    bool square_pixels; /* present 320x200 as-is instead of correcting to 4:3 */
    int  scale;         /* initial window scale factor, 0 = pick a sensible one */
} PlatformConfig;

bool platform_init(const PlatformConfig *cfg);
void platform_shutdown(void);

/* Uploads a EGA_W * EGA_H ARGB frame and presents it, then pumps events. */
void platform_present(const u32 *framebuffer);

/* Processes pending window and input events without blocking. */
void platform_pump_events(void);

/* True once the user has asked to close the window; the engine polls this so it
 * can unwind normally instead of being killed mid-frame. */
bool platform_quit_requested(void);
void platform_request_quit(void);

/* --- keyboard: an INT 16h style queue of (scan code << 8) | ascii ------- */

void platform_push_key(u16 key);         /* mainly for scripted input/tests */
u16  platform_peek_key(void);            /* 0 when the queue is empty */
u16  platform_pop_key_blocking(void);    /* pumps events until a key arrives */
bool platform_key_queue_empty(void);
void platform_clear_keys(void);

/* Hands scripted keys over one at a time, as if they were being typed: after a
 * key has been read the queue reports itself empty until the next read asks for
 * another one.
 *
 * This exists for the self-test. seg043.GetInputKey drains whatever is queued
 * behind the key it returns, so that holding a key down does not run a menu
 * forward several steps - which means a whole line of scripted keys pushed at
 * once collapses to its last one, and a loop that reads a key at a time can
 * never be driven. A real typist is never that fast; this mode makes a scripted
 * run behave like one. The gap lasts for one look at the queue, so the prompt
 * loops that poll for a key still find the next one. */
void platform_set_key_typed_mode(bool typed);

/* The scripted equivalent of holding a key down: once every repeat interval one
 * of these turns up behind whatever is queued, until platform_set_held_key(0) or
 * platform_clear_keys puts the key back up.
 *
 * This is the only thing a scripted run can type ahead with across a picture
 * load: seg043.clear_keyboard throws away everything queued, exactly as it did
 * on DOS, so keys pushed before a menu that loads a picture on the way in never
 * reach it. A key that is held down comes back afterwards, on real hardware and
 * here. The gap between repeats is what lets the clear loop finish. */
void platform_set_held_key(u16 key);

/* --- timing ------------------------------------------------------------ */

void platform_delay(int milliseconds);   /* keeps pumping events while waiting */
u32  platform_ticks(void);               /* milliseconds since init */

/* --- audio ------------------------------------------------------------- */

/* Loads a WAV into slot `slot`. The shipped effects are all mono signed-16 at
 * 22050 Hz, but anything SDL can read is converted on load. */
bool platform_sound_load(int slot, const char *path);
void platform_sound_play(int slot);
void platform_sound_stop_all(void);

#define PLATFORM_SOUND_SLOTS 16

#endif /* COAB_PLATFORM_H */
