#include "title.h"
#include "display.h"
#include "draw.h"
#include "text.h"
#include "frames.h"
#include "input.h"
#include "sound.h"
#include "platform.h"

/* ovr002.delay_or_key */
void title_delay_or_key(int seconds)
{
    u32 end;

    input_clear_keyboard();

    end = platform_ticks() + (u32)(seconds * 1000);
    while (!input_key_pressed() && platform_ticks() < end &&
           !input_quit_requested()) {
        input_sys_delay(100);
    }

    input_clear_keyboard();
}

/* ovr002.credits.
 *
 * The border is drawn from the 8x8 symbol banks, which the engine does not load
 * until it enters a chapter - so at this point in the startup sequence the
 * frame draws nothing and the credits appear as plain text on black, exactly as
 * in the original. */
void title_credits(void)
{
    display_update_stop();

    frames_draw_02();

    text_display_string("based on the tsr novel 'azure bonds'", 0, 10, 1, 2);
    text_display_string("by:",                 0, 10, 2, 6);
    text_display_string("kate novak",          0, 11, 2, 9);
    text_display_string("and",                 0, 10, 2, 0x14);
    text_display_string("jeff grubb",          0, 11, 2, 0x18);
    text_display_string("scenario created by:", 0, 10, 4, 0x0a);
    text_display_string("tsr, inc.",           0, 0x0e, 5, 0x0b);
    text_display_string("and",                 0, 0x0a, 5, 0x15);
    text_display_string("ssi",                 0, 0x0e, 5, 0x19);
    text_display_string("jeff grubb",          0, 0x0b, 6, 0x0e);
    text_display_string("george mac donald",   0, 0x0b, 7, 0x0b);
    text_display_string("game created by:",    0, 0x0a, 9, 0x01);
    text_display_string("ssi special projects", 0, 0x0e, 9, 0x12);
    text_display_string("project leader:",     0, 0x0e, 0x0b, 2);
    text_display_string("george mac donald",   0, 0x0b, 0x0c, 2);
    text_display_string("programming:",        0, 0x0e, 0x0e, 2);
    text_display_string("scot bayless",        0, 0x0b, 0x0f, 2);
    text_display_string("russ brown",          0, 0x0b, 0x10, 2);
    text_display_string("michael mancuso",     0, 0x0b, 0x11, 2);
    text_display_string("development:",        0, 0x0e, 0x13, 2);
    text_display_string("david shelley",       0, 0x0b, 0x14, 2);
    text_display_string("michael mancuso",     0, 0x0b, 0x15, 2);
    text_display_string("oran kangas",         0, 0x0b, 0x16, 2);
    text_display_string("graphic arts:",       0, 0x0e, 0x0b, 0x16);
    text_display_string("tom wahl",            0, 0x0b, 0x0c, 0x16);
    text_display_string("fred butts",          0, 0x0b, 0x0d, 0x16);
    text_display_string("susan manley",        0, 0x0b, 0x0e, 0x16);
    text_display_string("mark johnson",        0, 0x0b, 0x0f, 0x16);
    text_display_string("cyrus lum",           0, 0x0b, 0x10, 0x16);
    text_display_string("playtesting:",        0, 0x0e, 0x12, 0x16);
    text_display_string("jim jennings",        0, 0x0b, 0x13, 0x16);
    text_display_string("james kucera",        0, 0x0b, 0x14, 0x16);
    text_display_string("rick white",          0, 0x0b, 0x15, 0x16);
    text_display_string("robert daly",         0, 0x0b, 0x16, 0x16);

    display_update_start();
}

/* ovr002.title_screen */
void title_screen(void)
{
    DaxBlock *pic;

    /* Block 1 is the SSI logo, 2 the painted background, 3 and 4 the logo
     * lettering laid over it. The C# left every block to the garbage
     * collector; here each is released once drawn. */
    pic = draw_load_dax(0, 0, 1, "Title");
    draw_picture(pic, 0, 0, 0);
    dax_block_free(pic);
    title_delay_or_key(5);

    pic = draw_load_dax(0, 0, 2, "Title");
    draw_picture(pic, 0, 0, 0);
    dax_block_free(pic);

    pic = draw_load_dax(0, 0, 3, "Title");
    draw_picture(pic, 0x0b, 6, 0);
    dax_block_free(pic);
    title_delay_or_key(10);

    pic = draw_load_dax(0, 0, 4, "Title");
    sound_play(SOUND_D);
    draw_picture(pic, 0x0b, 0, 0);
    dax_block_free(pic);
    title_delay_or_key(10);

    text_clear_screen();
    title_credits();
    title_delay_or_key(10);

    text_clear_screen();
    display_update();
}
