#include "sound.h"
#include "platform.h"
#include "gbl.h"
#include "vfs.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* engine/seg044.cs: SoundInit. The index is the slot the engine's sound id maps
 * onto (sampleId = sound - 1); the gaps are effects the C# port never had a
 * sample for, and those ids stay silent. */
static const char *SAMPLE_FILE[PLATFORM_SOUND_SLOTS] = {
    NULL,            /*  0 */
    "missle.wav",    /*  1  <- sound_2 */
    "magic_hit.wav", /*  2  <- sound_3 */
    NULL,            /*  3 */
    "death.wav",     /*  4  <- sound_5 */
    "sound_5.wav",   /*  5  <- sound_6 */
    "hit.wav",       /*  6  <- sound_attackHeld */
    NULL,            /*  7 */
    "miss.wav",      /*  8  <- sound_9 */
    "step.wav",      /*  9  <- sound_a */
    "sound_10.wav",  /* 10  <- sound_b */
    NULL,            /* 11 */
    "start_sound.wav"/* 12  <- sound_d */
};

static bool path_is_dir(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

#define SOUND_PATH_MAX 1024

static bool dir_has(const char *dir, const char *name)
{
    char probe[SOUND_PATH_MAX];
    struct stat st;

    if (!vfs_path_join(probe, sizeof(probe), dir, name)) {
        return false;
    }
    return stat(probe, &st) == 0 && S_ISREG(st.st_mode);
}

static bool find_sounds_dir(const char *argv0, char *dst, size_t dst_size)
{
    char exe_dir[SOUND_PATH_MAX];
    const char *slash;

    /* The WAVs live beside the C# front end in Main/sounds; a packaged build
     * would put them next to the binary or under the data directory. */
    static const char *rel[] = {
        "sounds", "../sounds", "Main/sounds", "../Main/sounds",
        "../../Main/sounds", "../data/sounds"
    };

    snprintf(exe_dir, sizeof(exe_dir), "%s", argv0 ? argv0 : ".");
    slash = strrchr(exe_dir, '/');
    if (slash) {
        exe_dir[slash - exe_dir] = '\0';
    } else {
        snprintf(exe_dir, sizeof(exe_dir), ".");
    }

    const char *roots[] = { vfs_data_dir(), exe_dir, ".", NULL };

    for (int r = 0; roots[r]; r++) {
        if (!roots[r][0]) {
            continue;
        }
        for (size_t i = 0; i < COAB_ARRAY_LEN(rel); i++) {
            char candidate[SOUND_PATH_MAX];

            if (!vfs_path_join(candidate, sizeof(candidate), roots[r], rel[i])) {
                continue;
            }
            if (path_is_dir(candidate) && dir_has(candidate, "hit.wav")) {
                snprintf(dst, dst_size, "%s", candidate);
                return true;
            }
        }
    }
    return false;
}

void sound_init(const char *argv0, const char *sounds_dir)
{
    char dir[SOUND_PATH_MAX];
    int loaded = 0;

    if (sounds_dir && sounds_dir[0]) {
        snprintf(dir, sizeof(dir), "%s", sounds_dir);
    } else if (!find_sounds_dir(argv0, dir, sizeof(dir))) {
        log_warn("no sound effects found; running silent");
        gbl.sound_type = SOUND_TYPE_NONE;
        return;
    }

    for (int slot = 0; slot < PLATFORM_SOUND_SLOTS; slot++) {
        char path[SOUND_PATH_MAX];

        if (!SAMPLE_FILE[slot]) {
            continue;
        }
        if (!vfs_path_join(path, sizeof(path), dir, SAMPLE_FILE[slot])) {
            continue;
        }
        if (platform_sound_load(slot, path)) {
            loaded++;
        }
    }

    if (loaded > 0) {
        gbl.sound_type = SOUND_TYPE_PC;
        log_info("loaded %d sound effects from %s", loaded, dir);
    } else {
        gbl.sound_type = SOUND_TYPE_NONE;
        log_info("no sound effects loaded from %s; running silent", dir);
    }
}

void sound_set_enabled(bool on)
{
    gbl.sound_type = on ? SOUND_TYPE_PC : SOUND_TYPE_NONE;
    if (!on) {
        platform_sound_stop_all();
    }
}

/* engine/seg044.cs: PlaySound */
void sound_play(Sound which)
{
    if (gbl.sound_type != SOUND_TYPE_PC) {
        return;
    }

    if (which == SOUND_0 || which == SOUND_FF) {
        platform_sound_stop_all();
        return;
    }
    if (which == SOUND_1 || which == SOUND_F) {
        return;   /* no sample in the original either */
    }
    if (which >= SOUND_2 && which <= SOUND_E) {
        platform_sound_play((int)which - 1);
    }
}

void sound_shutdown(void)
{
    platform_sound_stop_all();
}
