/* platform_sdl1.c - the SDL-1.2 backend, for OpenVMS and anywhere else that
 * has no SDL2. platform_sdl.c is the SDL-2 one; platform.h picks between them
 * and the engine above this line never knows which is in.
 *
 * The differences from the SDL2 backend are all in the four places where SDL
 * changed shape:
 *
 *   - There is no renderer and no texture. SDL_SetVideoMode hands back a plain
 *     SDL_Surface and we write pixels into it, so the ARGB frame the engine
 *     hands over has to be converted to whatever the display format is and
 *     scaled by hand: SDL 1.2 has no public scaling blit (SDL_SoftStretch is
 *     private, and only does 1:1 formats anyway).
 *
 *   - Keys are SDLKey/SDLMod, several of the names differ, and key repeat is
 *     off until you ask for it.
 *
 *   - There is no SDL_WaitEventTimeout, so a blocking read polls and sleeps.
 *
 *   - Audio is left out altogether. The VMS SDL port is normally built with
 *     SDL_AUDIO_DISABLED - there is no MMOV on most systems - so asking for
 *     SDL_INIT_AUDIO would fail the whole init. platform_sound_load returning
 *     false is all sound.c needs to settle on SOUND_TYPE_NONE and say so.
 */
#include "platform.h"

#if COAB_SDL1

#include "log.h"

#include <SDL.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ state */

#define KEY_QUEUE_SIZE  64

/* The frame only ever holds the sixteen EGA colours, so the cache below never
 * comes close to filling; the slack is for the day something blends. */
#define COLOR_CACHE_MAX 256

static PlatformConfig g_cfg;
static bool           g_inited;
static bool           g_sdl_inited;

static SDL_Surface *g_screen;
static int          g_win_w;        /* the windowed size to come back to */
static int          g_win_h;
static bool         g_fullscreen;

/* Where the frame lands inside the window, and the source column each output
 * column reads. Both are recomputed whenever the mode or the aspect changes. */
static SDL_Rect g_dst;
static int     *g_xmap;
static int      g_xmap_cap;

static u32 g_cache_argb[COLOR_CACHE_MAX];
static u32 g_cache_pix[COLOR_CACHE_MAX];
static int g_cache_count;
static bool g_cache_full_warned;

/* One converted source row, reused for every output row that reads it. */
static u32 g_row[EGA_W];

static u16   g_keys[KEY_QUEUE_SIZE];
static int   g_key_head;
static int   g_key_count;
/* See platform_set_key_typed_mode: g_key_gate hides the queue between reads. */
static bool  g_key_typed_mode;
static bool  g_key_gate;

/* See platform_set_held_key. */
#define HELD_KEY_REPEAT_MS 100
static u16   g_held_key;
static u32   g_held_next_due;

static bool  g_quit;
static u32   g_start_ticks;

/* ------------------------------------------------------- key queue (IBM) */

void platform_push_key(u16 key)
{
    if (key == 0 || g_key_count >= KEY_QUEUE_SIZE) {
        return;
    }
    g_keys[(g_key_head + g_key_count) % KEY_QUEUE_SIZE] = key;
    g_key_count++;
    g_key_gate = false;
}

void platform_set_key_typed_mode(bool typed)
{
    g_key_typed_mode = typed;
    g_key_gate       = false;
}

void platform_set_held_key(u16 key)
{
    g_held_key      = key;
    g_held_next_due = platform_ticks();
}

/* A held key turns up once every HELD_KEY_REPEAT_MS, which is roughly what the
 * BIOS typematic rate was. The gap matters: with the key always there,
 * seg043.clear_keyboard's drain loop would never find the buffer empty. */
static u16 held_key(void)
{
    if (g_held_key == 0 || (int)(platform_ticks() - g_held_next_due) < 0) {
        return 0;
    }
    return g_held_key;
}

u16 platform_peek_key(void)
{
    if (g_key_gate) {
        /* The gap after the key that has just been read. It lasts for one look
         * at the queue, which is enough to end GetInputKey's drain loop without
         * starving the prompt loops that poll until a key shows up. */
        g_key_gate = false;
        return 0;
    }
    if (g_key_count > 0) {
        return g_keys[g_key_head];
    }
    return held_key();
}

bool platform_key_queue_empty(void)
{
    return g_key_count == 0 && held_key() == 0;
}

void platform_clear_keys(void)
{
    g_key_head = 0;
    g_key_count = 0;
    g_key_gate = false;
    g_held_key = 0;
}

static u16 key_pop(void)
{
    u16 key;

    if (g_key_count == 0) {
        key = held_key();

        if (key != 0) {
            g_held_next_due = platform_ticks() + HELD_KEY_REPEAT_MS;
            g_key_gate      = g_key_typed_mode;
            return key;
        }

        g_key_gate = false;
        return 0;
    }
    key = g_keys[g_key_head];
    g_key_head = (g_key_head + 1) % KEY_QUEUE_SIZE;
    g_key_count--;

    /* One key per read while a scripted run is pretending to be a typist. */
    g_key_gate = g_key_typed_mode;

    return key;
}

/* Main/Keyboard.cs: KeyToIBMKey. The high byte is the BIOS scan code and the
 * low byte the ASCII character; a zero low byte marks an extended key, which
 * READKEY reports as two successive calls. Letters always arrive uppercase and
 * anything unmapped becomes a space, both as in the original.
 *
 * The same table as the SDL2 backend's, with 1.2's spelling of the names: the
 * keypad is SDLK_KP7 rather than SDLK_KP_7, the Windows keys are SDLK_LSUPER
 * and SDLK_LMETA rather than SDLK_LGUI, and the locks are SDLK_NUMLOCK and
 * SDLK_SCROLLOCK (one L - that is 1.2's spelling, not a typo). */
static u16 sdl_key_to_ibm(SDLKey k, SDLMod mod)
{
    if (k >= SDLK_0 && k <= SDLK_9) {
        return (u16)('0' + (k - SDLK_0));
    }
    if (k >= SDLK_a && k <= SDLK_z) {
        /* Ctrl+C must reach the engine as 3 so the keyboard-exit cheat works. */
        if ((mod & KMOD_CTRL) != 0) {
            return (u16)(1 + (k - SDLK_a));
        }
        return (u16)('A' + (k - SDLK_a));
    }

    switch (k) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:    return 0x1C0D;
    case SDLK_SPACE:       return 0x20;
    case SDLK_DELETE:      return 0x5300;
    case SDLK_BACKSPACE:   return 0x08;

    case SDLK_HOME:
    case SDLK_KP7:
    case SDLK_LEFTBRACKET: return 0x4700;

    case SDLK_UP:
    case SDLK_KP8:         return 0x4800;

    case SDLK_PAGEUP:
    case SDLK_KP9:         return 0x4900;

    case SDLK_LEFT:
    case SDLK_KP4:         return 0x4B00;

    case SDLK_KP5:         return 0x4C00;

    case SDLK_RIGHT:
    case SDLK_KP6:         return 0x4D00;

    case SDLK_END:
    case SDLK_KP1:
    case SDLK_RIGHTBRACKET: return 0x4F00;

    case SDLK_DOWN:
    case SDLK_KP2:         return 0x5000;

    case SDLK_PAGEDOWN:
    case SDLK_KP3:         return 0x5100;

    case SDLK_MINUS:
    case SDLK_KP_MINUS:    return 0x2d00;

    case SDLK_ESCAPE:      return 0x1b;
    case SDLK_COMMA:       return 0x2c;
    case SDLK_PERIOD:      return 0x2e;

    /* Modifiers on their own must not enqueue anything; the original mapped
     * every unknown key to space, which made holding Shift look like a
     * keypress. */
    case SDLK_LSHIFT: case SDLK_RSHIFT:
    case SDLK_LCTRL:  case SDLK_RCTRL:
    case SDLK_LALT:   case SDLK_RALT:
    case SDLK_LMETA:  case SDLK_RMETA:
    case SDLK_LSUPER: case SDLK_RSUPER:
    case SDLK_MODE:
    case SDLK_CAPSLOCK: case SDLK_NUMLOCK:
    case SDLK_SCROLLOCK:
        return 0;

    default:               return 0x0020;
    }
}

/* ----------------------------------------------------------- colour mapping */

/* An index-plane display makes this cheap: display.c builds the ARGB frame from
 * a sixteen entry palette, so the number of distinct values in a frame is tiny
 * and a linear cache beats converting every pixel. The cache is keyed on the
 * ARGB word rather than a colour index because the engine hands over pixels,
 * not indices, and it is dropped whenever the display format changes. */

static u32 color_lookup(u32 argb)
{
    int i;

    for (i = 0; i < g_cache_count; i++) {
        if (g_cache_argb[i] == argb) {
            return g_cache_pix[i];
        }
    }
    /* color_cache_scan has been over the frame already, so this only happens
     * once the cache is full. */
    return SDL_MapRGB(g_screen->format, (Uint8)((argb >> 16) & 0xff),
                      (Uint8)((argb >> 8) & 0xff), (Uint8)(argb & 0xff));
}

static u32 color_add(u32 argb)
{
    SDL_PixelFormat *fmt = g_screen->format;
    Uint8 r = (Uint8)((argb >> 16) & 0xff);
    Uint8 g = (Uint8)((argb >> 8) & 0xff);
    Uint8 b = (Uint8)(argb & 0xff);
    u32 pix;

    if (fmt->BitsPerPixel == 8 && g_cache_count < 256) {
        SDL_Color c;

        c.r = r;
        c.g = g;
        c.b = b;
        c.unused = 0;

        /* One palette entry per distinct colour, in the order they turn up.
         * SDL_SetColors returns 1 only when it got the colours it was asked
         * for; if the palette is not ours - an 8bpp visual without
         * SDL_HWPALETTE - fall back on the nearest match in whatever is
         * already there rather than drawing with the wrong index. */
        if (SDL_SetColors(g_screen, &c, g_cache_count, 1) == 1) {
            pix = (u32)g_cache_count;
        } else {
            pix = SDL_MapRGB(fmt, r, g, b);
        }
    } else {
        pix = SDL_MapRGB(fmt, r, g, b);
    }

    if (g_cache_count < COLOR_CACHE_MAX) {
        g_cache_argb[g_cache_count] = argb;
        g_cache_pix[g_cache_count] = pix;
        g_cache_count++;
    } else if (!g_cache_full_warned) {
        g_cache_full_warned = true;
        log_warn("more than %d colours in a frame; converting the rest one "
                 "pixel at a time", COLOR_CACHE_MAX);
    }
    return pix;
}

static void color_cache_reset(void)
{
    g_cache_count = 0;
    g_cache_full_warned = false;

    /* Black first, so that slot zero - and, in 8bpp, palette entry zero - is
     * black whatever the first frame happens to contain. The letterbox is
     * filled with pixel value 0 on that understanding. */
    (void)color_add(0xff000000u);
}

/* Adds whatever is new in this frame. Runs of one colour are the normal case,
 * so testing against the previous pixel keeps this to a handful of searches. */
static void color_cache_scan(const u32 *fb)
{
    u32 prev = ~fb[0];
    int i;

    for (i = 0; i < EGA_W * EGA_H; i++) {
        if (fb[i] != prev) {
            prev = fb[i];
            if (g_cache_count < COLOR_CACHE_MAX) {
                int j;
                bool known = false;

                for (j = 0; j < g_cache_count && !known; j++) {
                    known = g_cache_argb[j] == prev;
                }
                if (!known) {
                    (void)color_add(prev);
                }
            }
        }
    }
}

/* --------------------------------------------------------------- presenting */

/* Mode 0Dh pixels are taller than they are wide: the 320x200 frame filled a 4:3
 * screen, so correct presentation stretches it to 320x240. */
static int logical_height(void)
{
    return g_cfg.square_pixels ? EGA_H : 240;
}

/* SDL_RenderSetLogicalSize by hand: the largest rectangle of the right shape
 * that fits in the window, centred, with black either side of it. */
static bool update_geometry(void)
{
    int logical_w = EGA_W;
    int logical_h = logical_height();
    int sw = g_screen->w;
    int sh = g_screen->h;
    int w, h, x;

    w = sw;
    h = (int)(((long)sw * (long)logical_h) / (long)logical_w);
    if (h > sh) {
        h = sh;
        w = (int)(((long)sh * (long)logical_w) / (long)logical_h);
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    g_dst.x = (Sint16)((sw - w) / 2);
    g_dst.y = (Sint16)((sh - h) / 2);
    g_dst.w = (Uint16)w;
    g_dst.h = (Uint16)h;

    if (w > g_xmap_cap) {
        int *grown = realloc(g_xmap, (size_t)w * sizeof(*grown));

        if (!grown) {
            log_error("out of memory sizing the %dx%d output", w, h);
            return false;
        }
        g_xmap = grown;
        g_xmap_cap = w;
    }
    for (x = 0; x < w; x++) {
        g_xmap[x] = (int)(((long)x * (long)EGA_W) / (long)w);
    }

    /* The letterbox is painted once here rather than every frame; nothing else
     * ever writes outside g_dst. */
    SDL_FillRect(g_screen, NULL, 0);
    SDL_UpdateRect(g_screen, 0, 0, 0, 0);
    return true;
}

static void present_scaled(const u32 *fb)
{
    SDL_PixelFormat *fmt = g_screen->format;
    int bpp = fmt->BytesPerPixel;
    int pitch = g_screen->pitch;
    u8 *base = (u8 *)g_screen->pixels;
    int dst_w = (int)g_dst.w;
    int dst_h = (int)g_dst.h;
    int prev_src_y = -1;
    u8 *prev_out = NULL;
    int y, x;

    for (y = 0; y < dst_h; y++) {
        u8 *out = base + (size_t)(g_dst.y + y) * (size_t)pitch +
                  (size_t)g_dst.x * (size_t)bpp;
        int src_y = (int)(((long)y * (long)EGA_H) / (long)dst_h);

        /* Vertical scaling repeats rows, and a repeat is a memcpy. */
        if (src_y == prev_src_y) {
            memcpy(out, prev_out, (size_t)dst_w * (size_t)bpp);
            continue;
        }

        {
            const u32 *src = fb + (size_t)src_y * EGA_W;
            u32 prev_argb = ~src[0];
            u32 pix = 0;
            int sx;

            /* Convert the source row once - one lookup per source pixel, not
             * per screen pixel. */
            for (sx = 0; sx < EGA_W; sx++) {
                if (src[sx] != prev_argb) {
                    prev_argb = src[sx];
                    pix = color_lookup(prev_argb);
                }
                g_row[sx] = pix;
            }
        }

        switch (bpp) {
        case 1: {
            u8 *p = out;
            for (x = 0; x < dst_w; x++) {
                p[x] = (u8)g_row[g_xmap[x]];
            }
            break;
        }
        case 2: {
            u16 *p = (u16 *)(void *)out;
            for (x = 0; x < dst_w; x++) {
                p[x] = (u16)g_row[g_xmap[x]];
            }
            break;
        }
        case 3: {
            u8 *p = out;
            for (x = 0; x < dst_w; x++, p += 3) {
                u32 v = g_row[g_xmap[x]];
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                p[0] = (u8)(v >> 16);
                p[1] = (u8)(v >> 8);
                p[2] = (u8)v;
#else
                p[0] = (u8)v;
                p[1] = (u8)(v >> 8);
                p[2] = (u8)(v >> 16);
#endif
            }
            break;
        }
        default: {
            u32 *p = (u32 *)(void *)out;
            for (x = 0; x < dst_w; x++) {
                p[x] = g_row[g_xmap[x]];
            }
            break;
        }
        }

        prev_src_y = src_y;
        prev_out = out;
    }
}

void platform_present(const u32 *framebuffer)
{
    if (!g_inited || g_cfg.headless || !g_screen || !framebuffer || !g_xmap) {
        return;
    }

    /* Before the lock: on an 8bpp display a new colour means a palette change,
     * and the driver is not to be called with the surface held. */
    color_cache_scan(framebuffer);

    if (SDL_MUSTLOCK(g_screen) && SDL_LockSurface(g_screen) != 0) {
        log_warn("SDL_LockSurface: %s", SDL_GetError());
        return;
    }

    present_scaled(framebuffer);

    if (SDL_MUSTLOCK(g_screen)) {
        SDL_UnlockSurface(g_screen);
    }

    SDL_UpdateRect(g_screen, g_dst.x, g_dst.y, g_dst.w, g_dst.h);

    platform_pump_events();
}

/* ------------------------------------------------------------------- events */

/* Every mode change goes through here, so the colour cache and the output
 * geometry are never left describing a surface that has gone. */
static bool set_video_mode(int w, int h, bool fullscreen)
{
    Uint32 flags = SDL_SWSURFACE | SDL_HWPALETTE;
    SDL_Surface *surface;

    if (!fullscreen) {
        flags |= SDL_RESIZABLE;
    } else {
        flags |= SDL_FULLSCREEN;
    }

    /* 0x0 asks 1.2 for the current mode's size, which is what going fullscreen
     * wants and is all the VMS port can do: it is built without
     * SDL_VIDEO_DRIVER_X11_VIDMODE, so it cannot change the display mode. */
    surface = SDL_SetVideoMode(w, h, 0, flags);
    if (!surface) {
        return false;
    }

    g_screen = surface;
    SDL_WM_SetCaption("Curse of the Azure Bonds", "Azure Bonds");

    color_cache_reset();
    return update_geometry();
}

static void apply_aspect(void)
{
    /* Only the letterbox and the scale tables change, not the mode. */
    if (g_screen) {
        (void)update_geometry();
    }
}

static void toggle_fullscreen(void)
{
    bool want = !g_fullscreen;

    if (set_video_mode(want ? 0 : g_win_w, want ? 0 : g_win_h, want)) {
        g_fullscreen = want;
        return;
    }

    log_warn("cannot switch to %s: %s", want ? "fullscreen" : "a window",
             SDL_GetError());

    /* A failed SDL_SetVideoMode has already given up the old mode, so there is
     * nothing to present into until one is back. */
    if (!set_video_mode(g_fullscreen ? 0 : g_win_w, g_fullscreen ? 0 : g_win_h,
                        g_fullscreen)) {
        log_error("and the previous mode will not come back either (%s); "
                  "stopping", SDL_GetError());
        g_screen = NULL;
        g_quit = true;
    }
}

static void handle_event(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_QUIT:
        g_quit = true;
        break;

    case SDL_VIDEORESIZE:
        g_win_w = ev->resize.w;
        g_win_h = ev->resize.h;
        if (!set_video_mode(g_win_w, g_win_h, g_fullscreen)) {
            log_error("cannot resize to %dx%d: %s", g_win_w, g_win_h,
                      SDL_GetError());
            g_screen = NULL;
            g_quit = true;
        }
        break;

    case SDL_VIDEOEXPOSE:
        /* A software surface keeps what was drawn into it, so the window only
         * needs pushing out again. */
        if (g_screen) {
            SDL_UpdateRect(g_screen, 0, 0, 0, 0);
        }
        break;

    case SDL_KEYDOWN: {
        SDLKey k = ev->key.keysym.sym;
        SDLMod m = ev->key.keysym.mod;

        /* Host bindings are handled here and never reach the game. */
        if ((k == SDLK_RETURN && (m & KMOD_ALT)) || k == SDLK_F11) {
            toggle_fullscreen();
            break;
        }
        if (k == SDLK_F10) {
            g_cfg.square_pixels = !g_cfg.square_pixels;
            apply_aspect();
            log_info("aspect: %s", g_cfg.square_pixels ? "1:1 (320x200)"
                                                       : "4:3 corrected");
            break;
        }
        if (k == SDLK_q && (m & KMOD_CTRL)) {
            g_quit = true;
            break;
        }

        platform_push_key(sdl_key_to_ibm(k, m));
        break;
    }

    default:
        break;
    }
}

void platform_pump_events(void)
{
    SDL_Event ev;

    if (!g_inited || g_cfg.headless) {
        return;
    }

    while (SDL_PollEvent(&ev)) {
        handle_event(&ev);
    }
}

bool platform_quit_requested(void)
{
    return g_quit;
}

void platform_request_quit(void)
{
    g_quit = true;
}

u16 platform_pop_key_blocking(void)
{
    if (g_cfg.headless) {
        /* Nothing can ever arrive, so returning 0 lets scripted runs finish
         * instead of hanging forever. */
        return key_pop();
    }

    while (g_key_count == 0 && !g_quit) {
        SDL_Event ev;

        /* SDL 1.2 has no SDL_WaitEventTimeout, and SDL_WaitEvent would sit
         * there past a quit request, so this polls and sleeps instead. The
         * 10 ms is short enough to stay responsive and long enough to keep a
         * mostly-idle game off the CPU. */
        if (SDL_PollEvent(&ev)) {
            handle_event(&ev);
        } else {
            SDL_Delay(10);
        }
    }

    return key_pop();
}

/* ------------------------------------------------------------------- timing */

void platform_delay(int milliseconds)
{
    u32 end;

    if (milliseconds <= 0) {
        return;
    }
    if (g_cfg.headless) {
        return;   /* self-tests should not sit through the game's pauses */
    }

    end = SDL_GetTicks() + (u32)milliseconds;
    while (SDL_GetTicks() < end && !g_quit) {
        u32 now = SDL_GetTicks();
        u32 left = end > now ? end - now : 0;

        platform_pump_events();
        SDL_Delay(left > 10 ? 10 : (left ? left : 1));
    }
}

u32 platform_ticks(void)
{
    /* SDL_Init starts the tick counter whatever subsystems were asked for, and
     * a headless run asks for SDL_INIT_TIMER precisely so that this works
     * there too - the SDL2 backend reaches for clock_gettime instead, and
     * CLOCK_MONOTONIC is not on every system this backend is for. */
    if (g_sdl_inited) {
        return SDL_GetTicks() - g_start_ticks;
    }

    /* Only reachable if even SDL_Init(SDL_INIT_TIMER) failed. The clock has to
     * keep moving regardless: the prompt loop advances the animated picture and
     * times prompts out by it, and a frozen clock leaves both waiting forever.
     * clock() is CPU time on some systems, which is close enough for that. */
    {
        static clock_t base;
        clock_t now = clock();
        /* CLOCKS_PER_SEC cannot be tested with #if everywhere - glibc defines
         * it as a cast - so the scaling is chosen here and folded away. */
        long per_ms = (long)CLOCKS_PER_SEC / 1000;

        if (now == (clock_t)-1) {
            return 0;
        }
        if (base == 0) {
            base = now;
        }
        if (per_ms > 0) {
            return (u32)((long)(now - base) / per_ms);
        }
        return (u32)((long)(now - base) * (1000 / (long)CLOCKS_PER_SEC));
    }
}

/* -------------------------------------------------------------------- audio */

/* There is none. sound.c takes platform_sound_load failing for every effect as
 * "this build has no sound", logs it once and runs silent, so nothing above
 * here needs an opinion about it. */

bool platform_sound_load(int slot, const char *path)
{
    (void)slot;
    (void)path;
    return false;
}

void platform_sound_play(int slot)
{
    (void)slot;
}

void platform_sound_stop_all(void)
{
}

/* ------------------------------------------------------------ init/shutdown */

bool platform_init(const PlatformConfig *cfg)
{
    int scale;

    if (cfg) {
        g_cfg = *cfg;
    }
    platform_clear_keys();
    g_quit = false;
    g_fullscreen = false;

    if (g_cfg.headless) {
        /* No display, but the timer still has to run; failing that is not
         * fatal, platform_ticks has a fallback. */
        if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE) == 0) {
            g_sdl_inited = true;
            g_start_ticks = SDL_GetTicks();
        } else {
            log_warn("SDL_Init(timer) failed: %s", SDL_GetError());
        }
        g_inited = true;
        log_info("platform: headless (no window, no audio)");
        return true;
    }

    /* Never SDL_INIT_AUDIO: this backend has no sound, and the VMS build of
     * SDL 1.2 is normally compiled with SDL_AUDIO_DISABLED, where asking for it
     * fails the whole of SDL_Init. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) != 0) {
        log_error("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    g_sdl_inited = true;

    scale = g_cfg.scale > 0 ? g_cfg.scale : 3;
    g_win_w = EGA_W * scale;
    g_win_h = logical_height() * scale;

    if (!set_video_mode(g_cfg.fullscreen ? 0 : g_win_w,
                        g_cfg.fullscreen ? 0 : g_win_h,
                        g_cfg.fullscreen)) {
        if (g_cfg.fullscreen) {
            /* Not every 1.2 target can go fullscreen - the VMS one cannot -
             * so drop back to a window rather than refuse to start. */
            log_warn("fullscreen unavailable (%s); starting in a window",
                     SDL_GetError());
            g_cfg.fullscreen = false;
        }
        if (!set_video_mode(g_win_w, g_win_h, false)) {
            log_error("SDL_SetVideoMode %dx%d failed: %s", g_win_w, g_win_h,
                      SDL_GetError());
            SDL_Quit();
            g_sdl_inited = false;
            return false;
        }
    } else {
        g_fullscreen = g_cfg.fullscreen;
    }

    /* SDL2 repeats a held key by default and the engine's menus expect it;
     * 1.2 does not until asked. */
    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

    g_start_ticks = SDL_GetTicks();
    g_inited = true;

    {
        char driver[32];
        const char *name = SDL_VideoDriverName(driver, (int)sizeof(driver));

        log_info("platform: SDL %d.%d.%d, video '%s', %dx%dx%d, %s pixels, "
                 "no audio",
                 SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
                 name ? name : "?", g_screen->w, g_screen->h,
                 (int)g_screen->format->BitsPerPixel,
                 g_cfg.square_pixels ? "square" : "4:3-corrected");
    }
    return true;
}

void platform_shutdown(void)
{
    if (!g_inited) {
        return;
    }

    free(g_xmap);
    g_xmap = NULL;
    g_xmap_cap = 0;

    /* The display surface belongs to SDL; SDL_Quit frees it. */
    g_screen = NULL;

    if (g_sdl_inited) {
        SDL_Quit();
        g_sdl_inited = false;
    }
    g_inited = false;
}

#else /* !COAB_SDL1 */

/* platform_sdl.c is providing the backend. A translation unit still has to
 * declare something, and the build globs src rather than listing it. */
typedef int coab_platform_sdl1_not_built;

#endif /* COAB_SDL1 */
