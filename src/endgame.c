/* endgame.c - Ported from engine/ovr019.cs. */
#include <math.h>

/* M_PI is not in the C standard - it is a POSIX and BSD addition - so a strictly
 * conforming <math.h> is entitled to withhold it, and both of the compilers this
 * tree is built with do exactly that: glibc needs _DEFAULT_SOURCE (the Makefile
 * passes it) and DEC C hides it whenever /STANDARD asks for strict conformance.
 * These four uses are the only ones in the tree. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "endgame.h"

#include "character.h"
#include "dax.h"
#include "display.h"
#include "draw.h"
#include "fileio.h"
#include "gbl.h"
#include "input.h"
#include "log.h"
#include "picture.h"
#include "prompt.h"
#include "rnd.h"
#include "text.h"

/* Struct_1ADFB, unk_1ADFB. The colour a rocket's sparks are drawn in at each of
 * the five stages of their fade. Reset() filled all five with 1, which is what a
 * rocket that never went off keeps. */
typedef struct {
    u8 colour[FIREWORK_STAGES];
} RocketColours;

static RocketColours g_rocket_colours[FIREWORK_ROCKETS];

/* gbl.dword_1ADF6. The C# allocated the 120 sparks when the display started and
 * dropped them again afterwards; nothing ever tested whether they were there, so
 * they simply live here. */
static FireworkSpark g_sparks[FIREWORK_SPARK_COUNT];

/* gbl.byte_1ADFA. How many ticks the longest-lived spark of the current burst
 * has, which is how long the burst is animated for. */
static int g_burst_ticks;

/* gbl.byte_1AE0A. The key that ended the display. */
static u8 g_interrupt_key;

/* gbl.unk_1AE0B and gbl.byte_1AE1B: the three rockets' colours, and how many of
 * them go off this time. */
static u8 g_rocket_size[FIREWORK_ROCKETS];
static int g_rockets_lit;

/* gbl.word_1AE0F, word_1AE11: where the rocket is. gbl.word_1AE13, word_1AE15:
 * how fast, across and up. gbl.word_1AE17, word_1AE19 back the speed up over the
 * first, undrawn flight. */
static u16 g_rocket_col;
static u16 g_rocket_row;
static u16 g_rocket_dx;
static i16 g_rocket_dy;
static u16 g_rocket_dx_backup;
static i16 g_rocket_dy_backup;

/* The band of the Shadowdale picture the fireworks are allowed in: the sky above
 * the crowd. Anything outside it is left alone, which is also what keeps a spark
 * whose row has run below zero - and so wrapped to a huge unsigned number - from
 * being drawn at all. */
static bool in_sky(u16 row)
{
    return row > 8 && row < 0x41;
}

/* ovr019.SetPixel */
static void set_pixel(u8 colour, u16 row, u16 col)
{
    display_set_pixel((int)col, (int)row, colour);
}

u8 endgame_get_pixel(int row, int col)
{
    return display_get_pixel(col, row);
}

/* sub_52068 */
void endgame_flash(void)
{
    draw_set_palette_color(15, 9);
    input_sys_delay(1);
    draw_set_palette_color(9, 9);
}

/* sub_520B8 */
void endgame_burst(const u8 *rocket_size, int dy, int dx, int row, int col)
{
    g_burst_ticks = 0;

    for (int rocket = 0; rocket < FIREWORK_ROCKETS; rocket++) {
        RocketColours *rc = &g_rocket_colours[rocket];
        int size = rocket_size[rocket];
        int last_stage_at;      /* var_15 */
        int stage2_at;          /* var_16 */
        int stage3_at;          /* var_18 */
        int centre_dx;          /* var_1A */
        int centre_dy;          /* var_1C */
        double angle, tilt;

        for (int stage = 0; stage < FIREWORK_STAGES; stage++) {
            rc->colour[stage] = 1;
        }

        /* White at first and white again as it spreads, then the rocket's own
         * colour brightened by eight, then the colour itself, then out. A rocket
         * that did not go off has size 1 and only the white flash. */
        if (size > 1) {
            rc->colour[0] = 15;
        }
        rc->colour[1] = 15;
        if (size > 1) {
            rc->colour[2] = (u8)(size + 8);
        }
        rc->colour[3] = (u8)size;
        rc->colour[4] = 1;

        last_stage_at = rnd_int(20) + 25;

        if (g_burst_ticks < last_stage_at) {
            g_burst_ticks = last_stage_at;
        }

        stage2_at = rnd_int(5) + 5;
        stage3_at = stage2_at + 15;

        /* Where this rocket's sparks are headed: a point 24 to 33 units off the
         * rocket's own course, in a direction picked out of a sphere. */
        angle = rnd_real() * (M_PI * 2.0);
        tilt  = rnd_real() * (M_PI * 2.0);

        centre_dx = (int)((rnd_int(10) + 24) * sin(angle) * sin(tilt)) + dx;
        centre_dy = (int)((rnd_int(10) + 24) * cos(angle) * sin(tilt)) + dy;

        for (int i = 0; i < FIREWORK_SPARKS_PER_ROCKET; i++) {
            FireworkSpark *sp = firework_spark(g_sparks, rocket, i);

            if (sp == NULL) {
                continue;
            }

            angle = rnd_real() * (M_PI * 2.0);
            tilt  = rnd_real() * (M_PI * 2.0);

            sp->x = (u16)col;
            sp->y = (u16)row;
            sp->x_fixed = (i16)(sp->x << 5);
            sp->y_fixed = (i16)(sp->y << 5);

            /* The C# added these through a cast to ushort, so a spark thrown
             * back and to the left went round through 65535 and came out right
             * again when the sum was stored as a short. That is the DOS code
             * storing a word, and the two's-complement trip changes nothing. */
            sp->dx = (i16)(centre_dx + (int)(sin(angle) * 16.0 * sin(tilt)));
            sp->dy = (i16)(centre_dy + (int)(cos(angle) * 16.0 * sin(tilt)));

            sp->stage = 1;
            sp->stage_end[0] = 1;
            sp->stage_end[1] = (u8)(stage2_at + rnd_int(7) - 4);
            sp->stage_end[2] = (u8)(stage3_at + rnd_int(11) - 6);
            sp->stage_end[3] = (u8)(last_stage_at + rnd_int(7));
            /* stage_end[4], field_16, is never set: the last stage draws in
             * colour 1 and the spark has nowhere further to go. */

            sp->covered_pixel = endgame_get_pixel(sp->y, sp->x);
        }
    }
}

/* sub_524F7.
 *
 * The C# moved a spark with `field_08 = field_0C`, which put its speed where its
 * position belongs: every spark landed within a pixel or two of the top-left
 * corner, outside the band the display draws in, so the whole burst was
 * invisible. The fixed point position accumulates the speed here, which is what
 * the erase-save-draw the rest of the routine does is for. */
void endgame_burst_step(int tick)
{
    /* Gravity pulls on the sparks every sixth tick and drag takes a unit off
     * their sideways speed every tick. */
    bool gravity = (tick % 6) == 0;

    for (int rocket = 0; rocket < FIREWORK_ROCKETS; rocket++) {
        for (int i = 0; i < FIREWORK_SPARKS_PER_ROCKET; i++) {
            FireworkSpark *sp = firework_spark(g_sparks, rocket, i);

            if (sp == NULL) {
                continue;
            }

            sp->x_fixed = (i16)(sp->x_fixed + sp->dx);
            sp->y_fixed = (i16)(sp->y_fixed + sp->dy);
            sp->next_x = (i16)(sp->x_fixed / 0x20);
            sp->next_y = (i16)(sp->y_fixed / 0x20);

            if (gravity) {
                sp->dy = (i16)(sp->dy + 1);
            }

            if (sp->dx > 0) {
                sp->dx = (i16)(sp->dx - 1);
            } else if (sp->dx < 0) {
                sp->dx = (i16)(sp->dx + 1);
            }

            if (firework_spark_stage_end(sp, sp->stage) < tick &&
                sp->stage < FIREWORK_STAGES) {
                sp->stage += 1;
            }
        }
    }

    /* The three loops that follow run spark before rocket, the other way round
     * from the one above, so that sparks of different rockets sharing a pixel
     * settle it in the order the original did. */

    /* Put back what each spark was covering, before it moves. */
    for (int i = 0; i < FIREWORK_SPARKS_PER_ROCKET; i++) {
        for (int rocket = 0; rocket < FIREWORK_ROCKETS; rocket++) {
            const FireworkSpark *sp = firework_spark(g_sparks, rocket, i);

            if (sp != NULL && in_sky(sp->y)) {
                set_pixel(sp->covered_pixel, sp->y, sp->x);
            }
        }
    }

    /* Move, and remember what is there now. */
    for (int i = 0; i < FIREWORK_SPARKS_PER_ROCKET; i++) {
        for (int rocket = 0; rocket < FIREWORK_ROCKETS; rocket++) {
            FireworkSpark *sp = firework_spark(g_sparks, rocket, i);

            if (sp == NULL) {
                continue;
            }

            sp->x = (u16)sp->next_x;
            sp->y = (u16)sp->next_y;

            if (in_sky(sp->y)) {
                sp->covered_pixel = endgame_get_pixel(sp->y, sp->x);
            }
        }
    }

    /* Draw. */
    for (int i = 0; i < FIREWORK_SPARKS_PER_ROCKET; i++) {
        for (int rocket = 0; rocket < FIREWORK_ROCKETS; rocket++) {
            const FireworkSpark *sp = firework_spark(g_sparks, rocket, i);

            if (sp != NULL && in_sky(sp->y)) {
                set_pixel(g_rocket_colours[rocket].colour[sp->stage - 1],
                          sp->y, sp->x);
            }
        }
    }

    /* The bang: the first spark reaching its second stage lights the sky. */
    if (g_sparks[0].stage == 2) {
        endgame_flash();
    }
}

/* sub_5279B */
void endgame_burst_run(void)
{
    int ticks = g_burst_ticks + 1;

    for (int tick = 1; tick <= ticks; tick++) {
        endgame_burst_step(tick);
    }

    /* Every spark is out, so put the sky back where they ended up. */
    for (int i = 0; i < FIREWORK_SPARKS_PER_ROCKET; i++) {
        for (int rocket = 0; rocket < FIREWORK_ROCKETS; rocket++) {
            const FireworkSpark *sp = firework_spark(g_sparks, rocket, i);

            if (sp != NULL && in_sky(sp->y)) {
                set_pixel(sp->covered_pixel, sp->y, sp->x);
            }
        }
    }
}

/* sub_5285E */
void endgame_rocket_flight(bool draw, int steps, i16 *dy, u16 dx,
                           u16 *row, u16 *col)
{
    u16 col_fixed;
    u16 row_fixed;
    u16 next_col;
    u16 next_row;
    u8  covered;

    if (draw) {
        endgame_flash();
    }

    col_fixed = (u16)(*col << 5);
    row_fixed = (u16)(*row << 5);

    if (draw && in_sky(*row)) {
        covered = endgame_get_pixel(*row, *col);
    } else {
        /* Only read back where it was written, so its value never matters; the
         * C# needed the same assignment to keep the compiler quiet. */
        covered = 0;
    }

    for (int step = 1; step <= steps; step++) {
        col_fixed = (u16)(col_fixed + dx);
        row_fixed = (u16)(row_fixed + (u16)(*dy + 1));

        next_col = (u16)(col_fixed / 0x20);
        next_row = (u16)(row_fixed / 0x20);

        *dy = (i16)(*dy + 1);

        if (draw && in_sky(*row)) {
            set_pixel(covered, *row, *col);
            covered = endgame_get_pixel(next_row, next_col);
            set_pixel((u8)(rnd_int(7) + 8), next_row, next_col);
        }

        input_sys_delay(0x0f);

        *col = next_col;
        *row = next_row;
    }

    if (draw && in_sky(*row)) {
        set_pixel(covered, *row, *col);
    }

    /* One more step, drawn nowhere: the rocket bursts a little past the last
     * spark of its trail. */
    col_fixed = (u16)(col_fixed + dx);
    row_fixed = (u16)(row_fixed + (u16)(*dy + 1));

    *dy = (i16)(*dy + 1);
    *col = (u16)(col_fixed / 0x20);
    *row = (u16)(row_fixed / 0x20);
}

/* sub_529F4 */
void endgame_fireworks(void)
{
    firework_sparks_clear(g_sparks, FIREWORK_SPARK_COUNT);
    g_interrupt_key = 0;

    do {
        if (g_interrupt_key == 0 && rnd_int(10000) < 1) {
            file_fill_char(1, FIREWORK_ROCKETS, g_rocket_size);

            /* Nought or one of the three rockets is a coloured one; the rest go
             * up as the plain white flash size 1 leaves behind. */
            g_rockets_lit = rnd_int(2);

            for (int i = 0; i < g_rockets_lit; i++) {
                g_rocket_size[i] = (u8)(rnd_int(5) + 2);
            }

            g_rocket_col = 65;
            g_rocket_row = 65;
            g_rocket_dx = (u16)(rnd_int(20) + 35);
            g_rocket_dy = (i16)(-(rnd_int(5) + 50));

            g_rocket_dy_backup = g_rocket_dy;
            g_rocket_dx_backup = g_rocket_dx;

            /* Fly it once without drawing to find out where it ends up, scatter
             * the sparks there, then put the speed back and fly it again for the
             * player to watch. */
            endgame_rocket_flight(false, 0x3c, &g_rocket_dy, g_rocket_dx,
                                  &g_rocket_row, &g_rocket_col);
            endgame_burst(g_rocket_size, g_rocket_dy, g_rocket_dx,
                          g_rocket_row, g_rocket_col);

            g_rocket_dx = g_rocket_dx_backup;
            g_rocket_dy = g_rocket_dy_backup;

            g_rocket_col = 0x41;
            g_rocket_row = 0x41;

            endgame_rocket_flight(true, 0x3c, &g_rocket_dy, g_rocket_dx,
                                  &g_rocket_row, &g_rocket_col);

            endgame_burst_run();

            if (input_key_pressed()) {
                g_interrupt_key = input_read_key();
            }
        }
    } while (g_interrupt_key == 0);
}

const FireworkSpark *endgame_sparks(void)
{
    return g_sparks;
}

/* sub_52B79 */
void endgame_show_animation(int num_loops, u8 block_id, int row_y, int col_x)
{
    int loop_count = 0;
    int start_time = text_time01();
    DaxArray animation;

    dax_array_init(&animation);

    picture_load_pic_final(&animation, 2, block_id, "PIC");

    if (animation.num_frames == 0 || animation.frames[0].picture == NULL) {
        /* The C# went on to dereference the first frame. */
        log_warn("endgame: PIC block 0x%02x has no animation to show", block_id);
        return;
    }

    /* The first frame goes into the overlay buffer so that the frames after it,
     * which are stored as differences against it, have something to work on. */
    draw_overlay_bounded(animation.frames[0].picture, 0, 0, row_y - 1, col_x - 1);
    /* seg040.DrawOverlay() went here; it does nothing. */

    do {
        int current_time;
        int delay;

        picture_draw_maybe_overlayed(dax_array_current_picture(&animation), true,
                                     row_y, col_x);
        current_time = text_time01();

        delay = dax_array_current_delay(&animation) * (gbl.game_speed_var + 3);

        if ((current_time - start_time) > delay) {
            animation.cur_frame += 1;

            if (animation.cur_frame > animation.num_frames) {
                animation.cur_frame = 1;
                loop_count++;
            }

            start_time = current_time;
        }
    } while (loop_count != num_loops);

    picture_dax_array_free_blocks(&animation);
}

void endgame_text(void)
{
    gbl.last_game_state = gbl.game_state;
    gbl.game_state = GAME_STATE_END_GAME;

    text_press_any_key_region("Tyranthraxus' spirit coalesces over the slain ",
                              true, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("storm giant. 'You have defeated me. Were it not for ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("the Amulet of Lythander, I could possess you and rob ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("you of your victory. Still I can escape through the pool.",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_display_and_pause("Press any key to continue.", 13);

    text_press_any_key_region("As you reach for the Pool of Radiance, he cries ",
                              true, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("out, 'Keep the Gauntlet of Moander away from there, you ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("will unleash dangerous energies. Stay back!' As the ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("gauntlet contacts the pool, it contracts and shatters it.",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);

    endgame_show_animation(1, 0x4a, 3, 3);

    text_display_and_pause("Press any key to continue.", 13);
    prompt_clear_area();

    text_press_any_key_region("'I am trapped without escape, you have succeeded ",
                              true, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("where armies have not. Gloat while you may, Tyranthraxus ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("is slain this day.' Before your eyes he crumbles into ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("nothingness.",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);

    endgame_show_animation(1, 0x4b, 3, 3);

    text_display_and_pause("Press any key to continue.", 13);
    prompt_clear_area();

    text_press_any_key_region("You are certain he is destroyed because your ",
                              true, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("final bond fades away. The Curse of the Azure Bonds ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("has finally been lifted from you! You are free at ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("last!",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);

    /* The last animation plays through the fade, so the bonds go out with it. */
    gbl.area_ptr->picture_fade = 1;

    endgame_show_animation((10 - gbl.game_speed_var) * 2, 0x4d, 3, 3);

    gbl.area_ptr->picture_fade = 0;

    picture_head_body(0x41, 0x41);
    picture_draw_head_and_body(true, 3, 3);

    text_press_any_key_region("The Knights of Myth Drannor rush in, '",
                              true, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("Congratulations, you have destroyed the Flamed One. ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("With the power of Elminster, let us take you from ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("this  foul place, to a fine feast.'",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);

    text_display_and_pause("Press any key to continue.", 13);
    prompt_clear_area();
    picture_load_bigpic(0x7a);

    picture_draw_bigpic();

    text_press_any_key_region("You are teleported to Shadowdale, where festivities ",
                              true, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("have already begun. A huge cheer goes up at your arrival. ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("Gharri and Nacacia, arm in arm, yell congratulations ",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    text_press_any_key_region("from the nearby stands. 'You have won!'",
                              false, 10, TEXT_REGION_NORMAL_BOTTOM);
    endgame_fireworks();

    gbl.game_state = gbl.last_game_state;
    character_load_pic();
}
