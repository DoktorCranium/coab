/* program.c - the tail of engine/seg001.cs: InitFirst's loading, InitAgain, and
 * PROGRAM. See program.h for how seg001 is split across gbl_init, main.c and
 * here.
 */
#include "program.h"

#include "area.h"
#include "cheats.h"
#include "draw.h"
#include "ecl.h"
#include "eclvm.h"
#include "enums.h"
#include "frames.h"
#include "gbl.h"
#include "icons.h"
#include "input.h"
#include "item.h"
#include "log.h"
#include "partymenu.h"
#include "prompt.h"
#include "protect.h"
#include "roster.h"
#include "sound.h"
#include "text.h"
#include "title.h"

/* seg001.InitFirst, sub_39054 - the loading half. */
void program_init_first(void)
{
    /* InitFirst's own order: the prompt row is blanked and the message put on
     * it before anything is read, because the reading is what takes the time. */
    prompt_clear_area();
    text_display_string("Loading...Please Wait", 0, 10, 0x18, 0);

    /* ovr038.Load8x8D(4, 0xca) and (0, 0xcb): banks 4 and 0 out of
     * 8X8D<game_area>.DAX. gbl_init left all five bank pointers NULL, so until
     * these two land nothing can draw a border. The original treated a missing
     * block as fatal; here it is logged and the game carries on, since the
     * failure shows up as missing furniture rather than as a wild pointer. */
    if (!frames_load_8x8d(4, 0xca) || !frames_load_8x8d(0, 0xcb)) {
        log_error("could not load the 8x8 symbol banks (blocks 0xca and 0xcb "
                  "of 8X8D%u.DAX)", (unsigned)gbl.game_area);
    }

    /* The twelve combat sprites, into icon slots 0x0d..0x18. The original ran
     * this loop on gbl.byte_1AD44, a scratch byte it used for nothing else and
     * which this port does not carry; a local says the same thing. */
    for (int i = 0; i <= 0x0b; i++) {
        icons_chead_cbody_comspr_icon((u8)(i + 0x0d), i, "COMSPR");
    }

    /* And the thirteenth on its own, block 0x19 into slot 0x19 - the last of the
     * 26 icons gbl_init made room for. */
    icons_chead_cbody_comspr_icon(0x19, 0x19, "COMSPR");

    /* seg040.LoadDax(13, 1, 250..252, "SKY"): masked on colour 13, which is what
     * lets the moon and the sun be drawn over the wilderness view. */
    gbl.sky_dax_250 = draw_load_dax(13, 1, 250, "SKY");
    gbl.sky_dax_251 = draw_load_dax(13, 1, 251, "SKY");
    gbl.sky_dax_252 = draw_load_dax(13, 1, 252, "SKY");

    /* new ItemDataTable("ITEMS") - the 0x81 rows every item's price, weight and
     * damage come out of. */
    if (!item_data_table_load("ITEMS")) {
        log_error("could not load the item table (ITEMS)");
    }

    /* The one InitFirst scalar gbl_init leaves alone: it runs before any of the
     * interpreter exists, and 0x8000 is where a script's code starts. */
    gbl.ecl_offset = 0x8000;

    /* ovr023.setup_spells and ovr013.SetupAffectTables closed InitFirst. Both
     * are static data in this port - see main.c, which calls what is left of the
     * first and nothing of the second. */
}

/* seg001.InitAgain, sub_396E5. */
void program_init_again(void)
{
    area1_clear(gbl.area_ptr);
    gbl.area_ptr->in_dungeon        = 1;
    gbl.area_ptr->last_ecl_block_id = 0;
    area2_clear(gbl.area2_ptr);
    ecl_vars_clear(gbl.ecl_vars);
    ecl_block_clear(gbl.ecl_ptr);

    /* Written twice by the original, which zeroes the position and then puts the
     * party where a game starts. The dead stores are kept: they are what the
     * disassembly has, and reading them is how anyone checks this against it. */
    gbl.map_pos_x     = 0;
    gbl.map_pos_y     = 0;
    gbl.map_direction = 0;
    gbl.map_wall_type = 0;
    gbl.map_wall_roof = 0;

    gbl.map_pos_x = 7;
    gbl.map_pos_y = 0x0d;
    /* InitFirst faces north here; a second game faces east. */
    gbl.map_direction = 2;

    gbl.can_bash_door  = true;
    gbl.can_pick_door  = true;
    gbl.can_knock_door = true;

    /* gbl.byte_1AD44 = 3 followed: scratch, never read, and absent from this
     * port. */

    gbl.set_blocks[0].block_id = 0;
    gbl.set_blocks[0].set_id   = 1;
    for (int i = 1; i < GBL_SET_BLOCKS; i++) {
        gbl.set_blocks[i].set_id   = -1;
        gbl.set_blocks[i].block_id = -1;
    }

    gbl.delay_between_characters = true;
    gbl.reload_ecl_and_pictures  = false;
    gbl.rest_encounter_count     = 0;

    /* gbl.TeamList.Clear(). The C# dropped its references and let the collector
     * take the party with them; here the records come out of the roster pool, so
     * a new game hands the whole pool back - see roster.h. */
    roster_clear();
    gbl.team_count           = 0;
    gbl.selected_player      = NULL;
    gbl.last_selected_player = NULL;

    gbl.ecl_offset     = 0x8000;
    /* Set by both routines, which is how the demo's speed of 9 lasts exactly one
     * game: gbl.in_demo survives here, the speed it set does not. */
    gbl.game_speed_var = 4;
    gbl.game_area        = 1;
    gbl.game_area_backup = 1;
    gbl.map_area_display = false;
    gbl.area2_ptr->party_size = 0;
    gbl.menu_screen_index = 1;
    gbl.combat_type       = COMBAT_TYPE_NORMAL;
    gbl.display_player_status_line18 = false;
    gbl.search_flag_bkup  = 0;
    gbl.sprite_changed    = false;
    gbl.party_killed      = false;
    /* gbl.byte_1BF12 = 1 followed, and Gbl.cs:325 asks what it was ever for.
     * Nothing reads it, so it is not in this port either. */
    gbl.display_player_sprite = false;
    gbl.last_dax_file[0]      = '\0';
    gbl.saved_dax_file[0]     = '\0';
    gbl.last_dax_block_id     = 0xff;
    gbl.saved_dax_block_id    = 0xff;
    gbl.game_saved            = false;
    gbl.byte_1EE95            = false;
    gbl.focus_combat_area_on_player = true;
    gbl.bigpic_block_id       = 0xff;
    gbl.silent_training       = false;
    /* InitFirst clears the prompt area before the load message; here it comes
     * after silent_training, which only matters if something drew on row 0x18 in
     * between, and nothing does. */
    prompt_clear_area();
    gbl.menu_selected_word = 1;
    gbl.game_state         = GAME_STATE_DUNGEON_MAP;
    gbl.last_game_state    = GAME_STATE_START_GAME_MENU;
    gbl.apply_item_affect  = false;
    gbl.game_won           = false;

    /* What InitFirst does and this does not: every allocation, the SKY and
     * symbol loading above, gbl.in_demo, and the items on the ground. The party
     * is gone but a shop's stock is not, which is the original's own doing. */
}

/* seg001.PROGRAM. */
void program_run(void)
{
    char input_key;

    /* PROGRAM opens with the CombatMap allocation (gbl_init) and
     * ovr003.SetupCommandTable (a static table in eclvm.c). */
    program_init_first();

    /* Classes/ItemLibrary.Read followed, and is deliberately not ported: it was
     * a debugging index of every item in the game, not something the game reads.
     * See item.h. */

    /* seg044.PlaySound(Sound.sound_0) came next; main.c plays it, ahead of the
     * --self-test branch, along with InitFirst's seg041.Load8x8Tiles. Both are
     * needed by a self-test run, which never gets here. */

    if (cheats.skip_title_screen == false) {
        title_screen();
    }

    /* Thirty seconds of "Play Demo" and then the demo starts itself, which is
     * how the DOS build behaved on a shop's machine. Escape or any other key
     * gets a real game. */
    gbl.display_input_seconds_to_wait = 30;
    gbl.display_input_timeout_value   = 'D';

    input_key = prompt_display_input_simple(false, 0, GBL_DEFAULT_MENU_COLORS,
                                           "Play Demo",
                                           "Curse of the Azure Bonds v1.3 ");

    gbl.display_input_seconds_to_wait = 0;
    gbl.display_input_timeout_value   = '\0';

    if (input_key == 'D') {
        gbl.in_demo = true;
    }

    if (cheats.skip_copy_protection == false && gbl.in_demo == false) {
        protect_copy_protection();
    }

    while (true) {
        if (gbl.in_demo == true) {
            gbl.game_area      = 1;
            gbl.game_speed_var = 9;   /* the demo plays itself, slowly */
        } else {
            /* A new game is put at chapter 2 before the menu opens; loading a
             * save overwrites this from the save (savegame.c). */
            gbl.game_area = 2;
        }

        if (gbl.in_demo == false) {
            partymenu_start_game_menu();
        }

        /* ovr003.sub_29758: the world outside combat, which does not come back
         * until the party is dead or the game is over. */
        eclvm_world_loop();

        program_init_again();

        if (gbl.in_demo == true) {
            /* The demo loops back to the title and asks again, this time with
             * ten seconds rather than thirty. Answering anything else ends the
             * demo and drops into a real game on the next turn of the loop -
             * which is the one place the copy protection is reached twice. */
            title_screen();
            input_clear_keyboard();

            gbl.display_input_seconds_to_wait = 10;
            gbl.display_input_timeout_value   = 'D';

            input_key = prompt_display_input_simple(
                false, 0, GBL_DEFAULT_MENU_COLORS, "Play Demo",
                "Curse of the Azure Bonds v1.3 ");

            gbl.display_input_seconds_to_wait = 0;
            gbl.display_input_timeout_value   = '\0';

            gbl.in_demo = (input_key == 'D');

            if (cheats.skip_copy_protection == false && gbl.in_demo == false) {
                protect_copy_protection();
            }

            sound_play(SOUND_0);
        }
    }
}
