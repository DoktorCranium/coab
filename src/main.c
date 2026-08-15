/* main.c - startup and the top-level flow.
 *
 * Replaces Main/Program.cs plus engine/seg001.cs __SystemInit/ConfigGame. The
 * C# spun up a WinForms window and ran the engine on a second thread; here the
 * engine owns the main thread and the SDL layer is pumped from its wait points.
 *
 * Command line, data directory, display, sound and the font, and then
 * program_run() - which is seg001.PROGRAM and never comes back.
 */
#include "coab.h"
#include "camp.h"
#include "cheats.h"
#include "dax.h"
#include "display.h"
#include "gbl.h"
#include "log.h"
#include "platform.h"
#include "program.h"
#include "quit.h"
#include "rnd.h"
#include "selftest.h"
#include "sound.h"
#include "spelleffect.h"
#include "text.h"
#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Options are matched without regard to case, which costs nothing on a shell
 * that preserves it and is what makes them usable from DCL: OpenVMS upcases
 * every unquoted word on a command line, so "--data" reaches main() as
 * "--DATA". Comparing here rather than lowercasing argv leaves the VALUES
 * alone - a data directory really can be case-sensitive.
 *
 * ASCII-only on purpose: tolower() is locale-dependent, and an option name
 * changing meaning with $LC_ALL set is not a trade worth making. */
static bool opt_is(const char *arg, const char *name)
{
    size_t i;

    for (i = 0; arg[i] != '\0' && name[i] != '\0'; i++) {
        char a = arg[i];

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (a != name[i]) {
            return false;
        }
    }
    return arg[i] == '\0' && name[i] == '\0';
}

static void usage(const char *argv0)
{
    printf(
        "Curse of the Azure Bonds - C/SDL port\n"
        "\n"
        "usage: %s [options]\n"
        "\n"
        "  --data DIR      game data directory (default: search, or $COAB_DATA)\n"
        "  --sounds DIR    directory holding the effect WAVs\n"
        "  --scale N       initial window scale factor (default 3)\n"
        "  --square        do not correct the aspect ratio to 4:3\n"
        "  --fullscreen    start fullscreen\n"
        "  --no-sound      do not open an audio device\n"
        "  --skip-title    jump past the title sequence\n"
        "  --self-test     render offscreen, dump PPMs, and report; needs no display\n"
        "  --out DIR       where --self-test writes its images (default ./selftest-out)\n"
        "  --verbose       log debug messages too\n"
        "  --help          this text\n"
        "\n"
        "in-game keys: F10 toggles aspect correction, F11 or Alt+Enter fullscreen,\n"
        "Ctrl+Q quits.\n",
        argv0);
}

int main(int argc, char **argv)
{
    PlatformConfig cfg;
    const char *data_dir = NULL;
    const char *sounds_dir = NULL;
    const char *out_dir = "selftest-out";
    bool self_test = false;
    bool skip_title = false;
    /* Live across QUIT_SET_EXIT_POINT()'s setjmp, so it has to survive the
     * longjmp back to it. */
    volatile int rc = EXIT_SUCCESS;

    memset(&cfg, 0, sizeof(cfg));
    cfg.scale = 3;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (opt_is(a, "--help") || opt_is(a, "-h")) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (opt_is(a, "--data") && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (opt_is(a, "--sounds") && i + 1 < argc) {
            sounds_dir = argv[++i];
        } else if (opt_is(a, "--out") && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (opt_is(a, "--scale") && i + 1 < argc) {
            cfg.scale = atoi(argv[++i]);
        } else if (opt_is(a, "--square")) {
            cfg.square_pixels = true;
        } else if (opt_is(a, "--fullscreen")) {
            cfg.fullscreen = true;
        } else if (opt_is(a, "--no-sound")) {
            cfg.no_audio = true;
        } else if (opt_is(a, "--skip-title")) {
            skip_title = true;
        } else if (opt_is(a, "--self-test")) {
            self_test = true;
            cfg.headless = true;
        } else if (opt_is(a, "--verbose")) {
            log_set_level(LOG_LEVEL_DEBUG);
        } else {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], a);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!vfs_init(argv[0], data_dir)) {
        return EXIT_FAILURE;
    }
    if (!self_test) {
        log_open_file(vfs_log_dir(), "coab.log");
    }
    log_info("data directory: %s", vfs_data_dir());

    if (!platform_init(&cfg)) {
        vfs_shutdown();
        return EXIT_FAILURE;
    }

    /* engine/seg001.cs: __SystemInit, then ConfigGame, then as much of InitFirst
     * as can run before a data file is open. program.c has the rest - see
     * program.h for how seg001 is split. */
    display_init();
    cheats_init();
    /* --skip-title is the cheat seg001.PROGRAM already tests, so it is set here
     * rather than checked separately at the one call site. */
    if (skip_title) {
        cheats.skip_title_screen = true;
    }
    gbl_init();
    /* ovr023.setup_spells, the last thing seg001.ConfigGame does. Only the four
     * globals are left of it - the dispatch table is static data in
     * spelleffect.c - and nothing between here and there reads them, so it sits
     * with the rest of the setup rather than after the DAX loading ConfigGame
     * does first. ovr013.SetupAffectTables, which followed it, has no
     * counterpart at all: affecttab.c's table is static too. */
    spelleffect_setup_spells();
    rnd_randomize();            /* InitFirst's first call, seg051.Randomize */
    /* ovr016.BuildEffectNameMap, which InitFirst runs once the records above
     * exist. It reads the spell table for the name of whichever spell lays each
     * affect, and that table is static data in spells.c, so this only has to
     * come after gbl_init. */
    camp_build_effect_name_map();
    sound_init(argv[0], sounds_dir);

    if (!text_load_8x8_tiles()) {
        log_error("could not load the 8x8 font (block 201 of 8X8D1.DAX); "
                  "the data files may be incomplete");
        rc = EXIT_FAILURE;
        goto done;
    }

    /* seg001.PROGRAM's PlaySound, kept here rather than in program_run because a
     * self-test run wants the audio path exercised too. */
    sound_play(SOUND_0);

    if (self_test) {
        rc = selftest_run(out_dir) ? EXIT_SUCCESS : EXIT_FAILURE;
        goto done;
    }

    /* Where seg043.print_and_exit unwinds to. In the C# the engine ran on its
     * own thread and was stopped with Thread.Abort(); here it is this frame the
     * engine returns to, however deep in the overlays it was. */
    if (QUIT_SET_EXIT_POINT()) {
        log_info("the engine stopped");
        goto done;
    }

    /* The rest of seg001.PROGRAM, which does not return: the title sequence, the
     * demo prompt, the code wheel, and then the game. */
    program_run();

done:
    sound_shutdown();
    gbl_free();
    dax_cache_clear();
    platform_shutdown();
    vfs_shutdown();
    log_close();
    return rc;
}
