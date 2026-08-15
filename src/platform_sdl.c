/* platform_sdl.c - the SDL-2 backend. platform_sdl1.c is the SDL-1.2 one; see
 * platform.h for how one of the two is picked. */
#include "platform.h"

#if COAB_SDL2

#include "log.h"

#include <SDL.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ state */

#define KEY_QUEUE_SIZE 64
#define AUDIO_FREQ     22050
#define AUDIO_CHANNELS 1
#define MIX_VOICES     8

typedef struct {
    u8    *buf;
    u32    len;         /* bytes */
} Sample;

typedef struct {
    const Sample *sample;
    u32           pos;  /* byte offset into sample->buf */
    bool          active;
} Voice;

static PlatformConfig g_cfg;
static bool           g_inited;

static SDL_Window     *g_window;
static SDL_Renderer   *g_renderer;
static SDL_Texture    *g_texture;
static SDL_AudioDeviceID g_audio;

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

static Sample g_samples[PLATFORM_SOUND_SLOTS];
static Voice  g_voices[MIX_VOICES];

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
 * anything unmapped becomes a space, both as in the original. */
static u16 sdl_key_to_ibm(SDL_Keycode k, SDL_Keymod mod)
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
    case SDLK_KP_7:
    case SDLK_LEFTBRACKET: return 0x4700;

    case SDLK_UP:
    case SDLK_KP_8:        return 0x4800;

    case SDLK_PAGEUP:
    case SDLK_KP_9:        return 0x4900;

    case SDLK_LEFT:
    case SDLK_KP_4:        return 0x4B00;

    case SDLK_KP_5:        return 0x4C00;

    case SDLK_RIGHT:
    case SDLK_KP_6:        return 0x4D00;

    case SDLK_END:
    case SDLK_KP_1:
    case SDLK_RIGHTBRACKET: return 0x4F00;

    case SDLK_DOWN:
    case SDLK_KP_2:        return 0x5000;

    case SDLK_PAGEDOWN:
    case SDLK_KP_3:        return 0x5100;

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
    case SDLK_LGUI:   case SDLK_RGUI:
    case SDLK_CAPSLOCK: case SDLK_NUMLOCKCLEAR:
    case SDLK_SCROLLLOCK:
        return 0;

    default:               return 0x0020;
    }
}

/* --------------------------------------------------------------- presenting */

static void compute_output_rect(SDL_Rect *dst)
{
    /* Mode 0Dh pixels are taller than they are wide: the 320x200 frame filled a
     * 4:3 screen, so correct presentation stretches it to 320x240. */
    int logical_h = g_cfg.square_pixels ? EGA_H : 240;

    dst->x = 0;
    dst->y = 0;
    dst->w = EGA_W;
    dst->h = logical_h;
}

void platform_present(const u32 *framebuffer)
{
    SDL_Rect dst;

    if (!g_inited || g_cfg.headless || !g_renderer || !framebuffer) {
        return;
    }

    if (SDL_UpdateTexture(g_texture, NULL, framebuffer, EGA_W * (int)sizeof(u32)) != 0) {
        log_warn("SDL_UpdateTexture: %s", SDL_GetError());
    }

    compute_output_rect(&dst);

    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, NULL, &dst);
    SDL_RenderPresent(g_renderer);

    platform_pump_events();
}

/* ------------------------------------------------------------------- events */

static void toggle_fullscreen(void)
{
    u32 flags = SDL_GetWindowFlags(g_window);
    bool is_fs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

    SDL_SetWindowFullscreen(g_window, is_fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

static void apply_logical_size(void)
{
    if (g_renderer) {
        SDL_RenderSetLogicalSize(g_renderer, EGA_W, g_cfg.square_pixels ? EGA_H : 240);
    }
}

static void handle_event(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_QUIT:
        g_quit = true;
        break;

    case SDL_KEYDOWN: {
        SDL_Keycode k = ev->key.keysym.sym;
        SDL_Keymod  m = (SDL_Keymod)ev->key.keysym.mod;

        /* Host bindings are handled here and never reach the game. */
        if ((k == SDLK_RETURN && (m & KMOD_ALT)) || k == SDLK_F11) {
            toggle_fullscreen();
            break;
        }
        if (k == SDLK_F10) {
            g_cfg.square_pixels = !g_cfg.square_pixels;
            apply_logical_size();
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

        /* Waiting rather than spinning keeps a mostly-idle game off the CPU;
         * the timeout bounds how long a quit request takes to notice. */
        if (SDL_WaitEventTimeout(&ev, 50)) {
            handle_event(&ev);
            platform_pump_events();
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
    struct timespec ts;
    static uint64_t headless_base;
    uint64_t now;

    if (g_inited && !g_cfg.headless) {
        return SDL_GetTicks() - g_start_ticks;
    }

    /* Headless runs have no SDL timer, but the clock still has to move: the
     * prompt loop advances the animated picture and times prompts out by it, and
     * a frozen clock leaves both waiting forever. */
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    now = (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);

    if (headless_base == 0) {
        headless_base = now;
    }
    return (u32)(now - headless_base);
}

/* -------------------------------------------------------------------- audio */

static void audio_callback(void *userdata, u8 *stream, int len)
{
    i16 *out = (i16 *)stream;
    int frames = len / (int)sizeof(i16);

    (void)userdata;
    SDL_memset(stream, 0, (size_t)len);

    for (int v = 0; v < MIX_VOICES; v++) {
        Voice *voice = &g_voices[v];
        const Sample *s;
        int avail_frames;

        if (!voice->active || !voice->sample) {
            continue;
        }
        s = voice->sample;

        avail_frames = (int)((s->len - voice->pos) / sizeof(i16));
        if (avail_frames <= 0) {
            voice->active = false;
            continue;
        }
        if (avail_frames > frames) {
            avail_frames = frames;
        }

        /* Mixing with saturation; several effects overlap during combat. */
        const i16 *src = (const i16 *)(const void *)(s->buf + voice->pos);
        for (int i = 0; i < avail_frames; i++) {
            i32 mixed = (i32)out[i] + (i32)src[i];

            if (mixed > 32767)  mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            out[i] = (i16)mixed;
        }

        voice->pos += (u32)avail_frames * (u32)sizeof(i16);
        if (voice->pos >= s->len) {
            voice->active = false;
        }
    }
}

static bool audio_init(void)
{
    SDL_AudioSpec want, have;

    SDL_zero(want);
    want.freq     = AUDIO_FREQ;
    want.format   = AUDIO_S16SYS;
    want.channels = AUDIO_CHANNELS;
    want.samples  = 1024;
    want.callback = audio_callback;

    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio == 0) {
        log_warn("audio unavailable (%s); continuing without sound", SDL_GetError());
        return false;
    }

    SDL_PauseAudioDevice(g_audio, 0);
    return true;
}

bool platform_sound_load(int slot, const char *path)
{
    SDL_AudioSpec spec;
    u8 *wav = NULL;
    u32 wav_len = 0;

    if (slot < 0 || slot >= PLATFORM_SOUND_SLOTS || !path) {
        return false;
    }
    if (g_cfg.headless || g_cfg.no_audio) {
        return false;
    }
    /* No device means nothing could ever be heard; say so rather than reporting
     * effects as loaded and leaving the engine to think sound works. */
    if (g_audio == 0) {
        return false;
    }

    if (SDL_LoadWAV(path, &spec, &wav, &wav_len) == NULL) {
        log_warn("cannot load %s: %s", path, SDL_GetError());
        return false;
    }

    /* The shipped effects are already mono S16 at 22050 Hz, so this normally
     * short-circuits; converting anyway means a replaced asset still works. */
    if (spec.freq != AUDIO_FREQ || spec.format != AUDIO_S16SYS ||
        spec.channels != AUDIO_CHANNELS) {
        SDL_AudioCVT cvt;
        int rc = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
                                   AUDIO_S16SYS, AUDIO_CHANNELS, AUDIO_FREQ);

        if (rc < 0) {
            log_warn("cannot convert %s: %s", path, SDL_GetError());
            SDL_FreeWAV(wav);
            return false;
        }
        if (rc > 0) {
            cvt.len = (int)wav_len;
            cvt.buf = SDL_malloc((size_t)cvt.len * (size_t)cvt.len_mult);
            if (!cvt.buf) {
                SDL_FreeWAV(wav);
                return false;
            }
            memcpy(cvt.buf, wav, wav_len);
            SDL_FreeWAV(wav);

            if (SDL_ConvertAudio(&cvt) != 0) {
                log_warn("cannot convert %s: %s", path, SDL_GetError());
                SDL_free(cvt.buf);
                return false;
            }
            wav = cvt.buf;
            wav_len = (u32)cvt.len_cvt;
        }
    }

    /* Swapping a sample out from under the mixer would tear, so stop the
     * callback while the slot is replaced. */
    if (g_audio) {
        SDL_LockAudioDevice(g_audio);
    }
    for (int v = 0; v < MIX_VOICES; v++) {
        if (g_voices[v].sample == &g_samples[slot]) {
            g_voices[v].active = false;
            g_voices[v].sample = NULL;
        }
    }
    SDL_free(g_samples[slot].buf);
    g_samples[slot].buf = wav;
    g_samples[slot].len = wav_len;
    if (g_audio) {
        SDL_UnlockAudioDevice(g_audio);
    }

    return true;
}

void platform_sound_play(int slot)
{
    if (!g_audio || slot < 0 || slot >= PLATFORM_SOUND_SLOTS) {
        return;
    }
    if (!g_samples[slot].buf || g_samples[slot].len == 0) {
        return;
    }

    SDL_LockAudioDevice(g_audio);
    for (int v = 0; v < MIX_VOICES; v++) {
        if (!g_voices[v].active) {
            g_voices[v].sample = &g_samples[slot];
            g_voices[v].pos = 0;
            g_voices[v].active = true;
            break;
        }
    }
    SDL_UnlockAudioDevice(g_audio);
}

void platform_sound_stop_all(void)
{
    if (!g_audio) {
        return;
    }
    SDL_LockAudioDevice(g_audio);
    for (int v = 0; v < MIX_VOICES; v++) {
        g_voices[v].active = false;
        g_voices[v].sample = NULL;
    }
    SDL_UnlockAudioDevice(g_audio);
}

/* ------------------------------------------------------------ init/shutdown */

bool platform_init(const PlatformConfig *cfg)
{
    u32 sdl_flags = SDL_INIT_VIDEO;
    int scale;

    if (cfg) {
        g_cfg = *cfg;
    }
    platform_clear_keys();
    g_quit = false;

    if (g_cfg.headless) {
        g_inited = true;
        log_info("platform: headless (no window, no audio)");
        return true;
    }

    if (!g_cfg.no_audio) {
        sdl_flags |= SDL_INIT_AUDIO;
    }

    if (SDL_Init(sdl_flags) != 0) {
        /* Audio is optional; a missing sound device must not cost the player
         * the whole game. */
        if ((sdl_flags & SDL_INIT_AUDIO) && SDL_Init(SDL_INIT_VIDEO) == 0) {
            log_warn("audio subsystem failed (%s); continuing muted", SDL_GetError());
            g_cfg.no_audio = true;
        } else {
            log_error("SDL_Init failed: %s", SDL_GetError());
            return false;
        }
    }

    scale = g_cfg.scale > 0 ? g_cfg.scale : 3;

    g_window = SDL_CreateWindow("Curse of the Azure Bonds",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                EGA_W * scale,
                                (g_cfg.square_pixels ? EGA_H : 240) * scale,
                                SDL_WINDOW_RESIZABLE |
                                (g_cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!g_window) {
        log_error("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED |
                                                 SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        log_warn("accelerated renderer unavailable (%s); trying software",
                 SDL_GetError());
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_renderer) {
        log_error("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return false;
    }

    /* Nearest-neighbour: this is 1989 pixel art and should stay crisp. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    apply_logical_size();

    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, EGA_W, EGA_H);
    if (!g_texture) {
        log_error("SDL_CreateTexture failed: %s", SDL_GetError());
        platform_shutdown();
        return false;
    }

    if (!g_cfg.no_audio) {
        audio_init();
    }

    g_start_ticks = SDL_GetTicks();
    g_inited = true;

    {
        SDL_RendererInfo info;
        const char *driver = SDL_GetCurrentVideoDriver();

        log_info("platform: SDL %d.%d.%d, video '%s', renderer '%s', %s pixels",
                 SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
                 driver ? driver : "?",
                 SDL_GetRendererInfo(g_renderer, &info) == 0 ? info.name : "?",
                 g_cfg.square_pixels ? "square" : "4:3-corrected");
    }
    return true;
}

void platform_shutdown(void)
{
    if (!g_inited) {
        return;
    }

    if (g_audio) {
        SDL_CloseAudioDevice(g_audio);
        g_audio = 0;
    }
    for (int i = 0; i < PLATFORM_SOUND_SLOTS; i++) {
        SDL_free(g_samples[i].buf);
        g_samples[i].buf = NULL;
        g_samples[i].len = 0;
    }

    if (g_texture)  { SDL_DestroyTexture(g_texture);   g_texture = NULL; }
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = NULL; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window = NULL; }

    if (!g_cfg.headless) {
        SDL_Quit();
    }
    g_inited = false;
}

#else /* !COAB_SDL2 */

/* platform_sdl1.c is providing the backend. A translation unit still has to
 * declare something, and the build globs src rather than listing it. */
typedef int coab_platform_sdl2_not_built;

#endif /* COAB_SDL2 */
