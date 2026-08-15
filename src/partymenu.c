/* partymenu.c - Ported from engine/ovr018.cs, plus three helpers out of
 * engine/ovr017.cs. See partymenu.h. */

#include <stdio.h>
#include <string.h>

#include "partymenu.h"

#include "affect.h"
#include "area.h"
#include "character.h"
#include "cheats.h"
#include "classcalc.h"
#include "combat.h"
#include "combatmap.h"
#include "draw.h"
#include "effect.h"
#include "fileio.h"
#include "frames.h"
#include "gbl.h"
#include "icons.h"
#include "input.h"
#include "limits.h"
#include "log.h"
#include "menu.h"
#include "money.h"
#include "prompt.h"
#include "quit.h"
#include "rnd.h"
#include "roster.h"
#include "savegame.h"
#include "spells.h"
#include "text.h"
#include "viewplayer.h"

/* ------------------------------------------------------------------------- */

/* ovr018.FreePlayer, free_player */
void partymenu_free_player(Player *player)
{
    if (player == NULL) {
        log_warn("party: no character to free");
        return;
    }

    player->actions = NULL;

    player->item_count = 0;
    affect_list_clear(&player->affects);
}

/* ovr018.menuStrings */
#define PARTY_MENU_ENTRIES 12

static const char *const menu_strings[PARTY_MENU_ENTRIES] = {
    "Create New Character",
    "Drop Character",
    "Modify Character",
    "Train Character",
    "Human Change Classes",
    "View Character",
    "Add Character to Party",
    "Remove Character from Party",
    "Load Saved Game",
    "Save Current Game",
    "BEGIN Adventuring",
    "Exit to DOS"
};

enum {
    ALLOW_CREATE    = 0,
    ALLOW_DROP      = 1,
    ALLOW_MODIFY    = 2,
    ALLOW_TRAINING  = 3,
    ALLOW_DUELCLASS = 4,
    ALLOW_VIEW      = 5,
    ALLOW_ADD       = 6,
    ALLOW_REMOVE    = 7,
    ALLOW_LOAD      = 8,
    ALLOW_SAVE      = 9,
    ALLOW_BEGIN     = 10,
    ALLOW_EXIT      = 11
};

/* ovr018.menuFlags. Which of the twelve entries the menu offers. This is not
 * derived fresh each time the menu is built - Create, Add and Exit are set here
 * and never touched again - so it has to survive between calls, as the C#'s
 * static array did. */
static bool menu_flags[PARTY_MENU_ENTRIES] = {
    true,   /* Create New Character */
    false,
    false,
    false,
    false,
    false,
    true,   /* Add Character to Party */
    false,
    false,
    false,
    false,
    true    /* Exit to DOS */
};

void partymenu_start_game_menu(void)
{
    /* MenuColorSet(0, 0, 13): the highlight and the menu text are both black, so
     * only the prompt in front of them shows. The twelve lines are already drawn
     * down the screen; the key string is there to be accepted, not read. */
    static const MenuColorSet menu_colors = { 0, 0, 13 };

    GameState game_state_backup = gbl.game_state;
    bool recalc_menus = true;

    gbl.game_state = GAME_STATE_START_GAME_MENU;

    while (input_quit_requested() == false) {
        bool control_key;
        char input_key;

        if (recalc_menus) {
            int y_col = 0;

            frames_draw_outer();

            if (gbl.selected_player != NULL) {
                character_party_summary(gbl.selected_player);

                menu_flags[ALLOW_DROP] = true;
                menu_flags[ALLOW_MODIFY] = true;

                if (gbl.area2_ptr->training_class_mask > 0 ||
                    cheats.free_training) {
                    menu_flags[ALLOW_TRAINING] = true;
                    menu_flags[ALLOW_DUELCLASS] =
                        player_can_duel_class(gbl.selected_player);
                } else {
                    menu_flags[ALLOW_TRAINING] = false;
                    menu_flags[ALLOW_DUELCLASS] = false;
                }

                menu_flags[ALLOW_VIEW] = true;
                menu_flags[ALLOW_REMOVE] = true;
                menu_flags[ALLOW_LOAD] = false;
                menu_flags[ALLOW_SAVE] = true;
                menu_flags[ALLOW_BEGIN] = true;
            } else {
                menu_flags[ALLOW_DROP] = false;
                menu_flags[ALLOW_MODIFY] = false;
                menu_flags[ALLOW_TRAINING] = false;
                menu_flags[ALLOW_DUELCLASS] = false;
                menu_flags[ALLOW_VIEW] = false;
                menu_flags[ALLOW_REMOVE] = false;
                menu_flags[ALLOW_LOAD] = true;
                menu_flags[ALLOW_SAVE] = false;
                menu_flags[ALLOW_BEGIN] = false;
            }

            for (int i = 0; i < PARTY_MENU_ENTRIES; i++) {
                if (menu_flags[i]) {
                    char initial[2];

                    /* The first letter in white at column 2, the rest in green
                     * from column 3 - seg051.Copy(len, 1, s) being the tail. */
                    initial[0] = menu_strings[i][0];
                    initial[1] = '\0';

                    text_display_string(initial, 0, 15, y_col + 12, 2);
                    text_display_string(menu_strings[i] + 1, 0, 10,
                                        y_col + 12, 3);
                    y_col++;
                }
            }

            recalc_menus = false;
        }

        input_key = prompt_display_input(&control_key, false,
                                        PROMPT_CTRL_WORD_ARROWS, menu_colors,
                                        "C D M T H V A R L S B E J",
                                        "Choose a function ");

        prompt_clear_area();

        if (control_key) {
            /* unk_4C13D = { 'G', 'O' }: Home and End walk the selection along
             * the party, and nothing else here answers to a cursor key. Up and
             * Down are taken for them; see prompt_selection_key. */
            char selection_key = prompt_selection_key(input_key, control_key);

            if (gbl.selected_player != NULL &&
                (selection_key == 'G' || selection_key == 'O')) {
                bool could_duel_class =
                    player_can_duel_class(gbl.selected_player);

                viewplayer_scroll_team_list(input_key);
                character_party_summary(gbl.selected_player);

                /* Redrawn only when Human Change Classes would come or go, and
                 * only where there is a trainer to offer it. Every other line
                 * says the same thing for whoever is selected. */
                could_duel_class =
                    (could_duel_class !=
                     player_can_duel_class(gbl.selected_player));

                recalc_menus = could_duel_class &&
                               gbl.area2_ptr->training_class_mask > 0;
            }
        } else {
            /* unk_4C15D = { 'E', 'S' }: quitting and saving are the two choices
             * that do not leave the saved game out of date. */
            if (input_key != 'E' && input_key != 'S') {
                gbl.game_saved = false;
            }

            switch (input_key) {
            case 'C':
                if (menu_flags[ALLOW_CREATE]) {
                    partymenu_create_player();
                }
                break;

            case 'D':
                if (menu_flags[ALLOW_DROP]) {
                    partymenu_drop_player();
                }
                break;

            case 'M':
                if (menu_flags[ALLOW_MODIFY]) {
                    partymenu_modify_player();
                }
                break;

            case 'T':
                if (menu_flags[ALLOW_TRAINING]) {
                    partymenu_train_player();
                }
                break;

            case 'H':
                if (menu_flags[ALLOW_DUELCLASS]) {
                    classcalc_duel_class(gbl.selected_player);
                }
                break;

            case 'V':
                if (menu_flags[ALLOW_VIEW]) {
                    viewplayer_view_player();
                }
                break;

            case 'A':
                if (menu_flags[ALLOW_ADD]) {
                    partymenu_add_player();
                }
                break;

            case 'R':
                if (menu_flags[ALLOW_REMOVE] && gbl.selected_player != NULL) {
                    /* A player character is written out and let go; an NPC has
                     * no file of their own, so removing them is dropping them. */
                    if (gbl.selected_player->control_morale < CONTROL_NPC_BASE) {
                        savegame_save_player("", gbl.selected_player);
                        gbl.selected_player = partymenu_free_current_player(
                            gbl.selected_player, true, false);
                    } else {
                        partymenu_drop_player();
                    }
                }
                break;

            case 'L':
                if (menu_flags[ALLOW_LOAD]) {
                    savegame_load_game_menu();
                }
                break;

            case 'S':
                if (menu_flags[ALLOW_SAVE] && gbl.team_count > 0) {
                    savegame_save_game();
                }
                break;

            case 'B':
                if (menu_flags[ALLOW_BEGIN]) {
                    if ((gbl.team_count > 0 && gbl.in_demo) ||
                        gbl.area_ptr->field_3FA == 0 || gbl.in_demo) {
                        gbl.game_state = game_state_backup;

                        if (gbl.reload_ecl_and_pictures == false &&
                            gbl.last_dax_block_id != 0x50) {
                            if (gbl.game_state == GAME_STATE_WILDERNESS_MAP) {
                                frames_draw_wilderness_map();
                            } else {
                                frames_draw_03();
                            }
                            character_party_summary(gbl.selected_player);
                        } else {
                            if (gbl.area_ptr->last_ecl_block_id == 0) {
                                frames_draw_03();
                            }
                        }

                        prompt_clear_area();

                        /* A trainer visited once does not go on offering to
                         * train after the party has left. */
                        gbl.area2_ptr->training_class_mask = 0;

                        return;
                    }
                }
                break;

            case 'E':
                if (menu_flags[ALLOW_EXIT]) {
                    input_key = prompt_yes_no(GBL_ALERT_MENU_COLORS,
                                              "Quit to DOS ");

                    if (input_key == 'Y') {
                        if (gbl.team_count > 0 && gbl.game_saved == false) {
                            input_key = prompt_yes_no(
                                GBL_ALERT_MENU_COLORS,
                                "Game not saved.  Quit anyway? ");

                            /* Answering no saves the game and then stays: the
                             * second answer is what the quit below tests. */
                            if (input_key == 'N') {
                                savegame_save_game();
                            }
                        }

                        if (input_key == 'Y') {
                            game_print_and_exit();
                        }
                    }
                }
                break;

            default:
                break;
            }

            recalc_menus = true;
        }
    }

    /* Only reached when the window has been closed. The menu's own way out is
     * the return inside case 'B', which restores the state itself. */
    gbl.game_state = game_state_backup;
}

/* ovr018.unk_1A1B2, seg600:3EA2. One flag per class for Player.classFlags. */
static const u8 class_flag_bits[SKILL_COUNT] = {
    0x02, 0x10, 0x08, 0x40, 0x40, 0x01, 0x04, 0x20
};

/* ovr018.thac0_table, seg600:3E3A unk_1A14A */
const i8 partymenu_thac0_table[SKILL_COUNT][PARTYMENU_EXP_LEVELS] = {
    { 40, 40, 40, 40, 0x2A, 0x2A, 0x2A, 0x2C, 0x2C, 0x2C, 0x2E, 0x2E, 0x2E },
    { 40, 40, 40, 40, 0x2A, 0x2A, 0x2A, 0x2C, 0x2C, 0x2C, 0x2E, 0x2E, 0x2E },
    { 0x27, 40, 40, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33 },
    { 40, 40, 40, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33 },
    { 40, 40, 40, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33 },
    { 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x29, 0x29, 0x29, 0x29, 0x29,
      0x2B, 0x2B },
    { 40, 40, 40, 40, 40, 0x29, 0x29, 0x29, 0x29, 0x2C, 0x2C, 0x2C, 0x2C },
    { 40, 40, 40, 40, 0x2A, 0x2A, 0x2A, 0x2C, 0x2C, 0x2C, 0x2E, 0x2E, 0x2E }
};

/* The four lists character creation runs are the same loop each time: keep
 * showing the list until the player picks with Select, and answer false when
 * they press Escape. *index goes in as the entry to open on and comes out as the
 * entry chosen - the list index with the headings counted in, which is what the
 * original then does its arithmetic on. */
static bool select_from_list(int *index, MenuList *list)
{
    char input_key;
    bool menu_redraw = true;
    MenuItem *selected = NULL;

    do {
        input_key = prompt_select_item(&selected, index, &menu_redraw, true,
                                      list, 22, 38, 2, 1,
                                      GBL_DEFAULT_MENU_COLORS, "Select", "");

        if (input_key == '\0') {
            return false;
        }
    } while (input_key != 'S' && input_quit_requested() == false);

    return input_quit_requested() == false;
}

void partymenu_create_player(void)
{
    static MenuList list;

    /* raceString[1..5] and [7]: half-orcs are in the table but cannot be rolled
     * up, so index 6 is skipped, which is what the `index == 6` fix-up below is
     * putting right. */
    static const int race_choices[] = {
        RACE_DWARF, RACE_ELF, RACE_GNOME, RACE_HALF_ELF, RACE_HALFLING,
        RACE_HUMAN
    };

    Player *player;
    Player *selected_backup;
    const RaceClasses *class_list;
    int index;
    int race;
    int sex;
    int class_count;
    int alignment_count;
    char input_key;
    char text[64];

    player = roster_alloc();

    if (player == NULL) {
        log_warn("create character: no room for another character");
        return;
    }

    for (int i = 0; i < GBL_ICON_COLOUR_COUNT; i++) {
        player->icon_colours[i] =
            (u8)(((GBL_DEFAULT_ICON_COLOURS[i] + 8) << 4) +
                 GBL_DEFAULT_ICON_COLOURS[i]);
    }

    player->base_ac = 50;
    player->thac0 = 40;
    player->health_status = STATUS_OKEY;
    player->in_combat = true;
    player->field_DE = 1;
    player->mod_id = (u8)rnd_int(256);
    player->icon_id = 0x0A;

    /* --- race --- */

    menu_list_clear(&list);
    menu_list_add_heading(&list, "Pick Race");

    for (size_t i = 0; i < COAB_ARRAY_LEN(race_choices); i++) {
        snprintf(text, sizeof(text), "  %s",
                 viewplayer_race_name(race_choices[i]));
        menu_list_add(&list, text);
    }

    index = 0;

    if (!select_from_list(&index, &list)) {
        /* The C# dropped the half-built character on the floor and let its
         * collector have it; the pool has to be told. */
        roster_release(player);
        return;
    }

    if (index == 6) {
        index++;
    }

    player->race = index;

    switch (player->race) {
    case RACE_HALFLING:
        player->icon_size = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_CON_SAVING_BONUS, player);
        break;

    case RACE_DWARF:
        player->icon_size = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_CON_SAVING_BONUS, player);
        effect_add_affect(false, 0xff, 0, AFFECT_DWARF_VS_ORC, player);
        effect_add_affect(false, 0xff, 0, AFFECT_DWARF_AND_GNOME_VS_GIANTS,
                          player);
        break;

    case RACE_GNOME:
        player->icon_size = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_CON_SAVING_BONUS, player);
        effect_add_affect(false, 0xff, 0, AFFECT_GNOME_VS_MAN_SIZED_GIANT,
                          player);
        effect_add_affect(false, 0xff, 0, AFFECT_DWARF_AND_GNOME_VS_GIANTS,
                          player);
        effect_add_affect(false, 0xff, 0, AFFECT_30, player);
        break;

    case RACE_ELF:
        player->icon_size = 2;
        effect_add_affect(false, 0xff, 0, AFFECT_ELF_RESIST_SLEEP, player);
        break;

    case RACE_HALF_ELF:
        player->icon_size = 2;
        effect_add_affect(false, 0xff, 0, AFFECT_HALFELF_RESISTANCE, player);
        break;

    default:
        player->icon_size = 2;
        break;
    }

    /* --- sex --- */

    menu_list_clear(&list);
    menu_list_add_heading(&list, "Pick Gender");

    for (int i = 0; i < 2; i++) {
        snprintf(text, sizeof(text), "  %s", viewplayer_sex_name(i));
        menu_list_add(&list, text);
    }

    index = 1;

    if (!select_from_list(&index, &list)) {
        roster_release(player);
        return;
    }

    player->sex = (u8)(index - 1);

    /* --- class --- */

    menu_list_clear(&list);
    menu_list_add_heading(&list, "Pick Class");

    class_list = &limits_race_classes[player->race];

    /* Row RACE_COUNT is the cheat row: every class, whatever the race. */
    if (player->race != RACE_HUMAN && cheats.no_race_class_restrictions) {
        class_list = &limits_race_classes[RACE_HUMAN + 1];
    }

    for (int i = 0; i < class_list->count; i++) {
        snprintf(text, sizeof(text), "  %s",
                 player_class_name((int)class_list->cls[i]));
        menu_list_add(&list, text);
    }

    index = 1;

    if (!select_from_list(&index, &list)) {
        roster_release(player);
        return;
    }

    if (index < 1 || index > class_list->count) {
        log_warn("create character: class %d is not on the list of %d", index,
                 class_list->count);
        roster_release(player);
        return;
    }

    player->exp = 25000;
    player->cls = (int)class_list->cls[index - 1];
    player->hit_dice = 1;

    if (player->cls >= CLASS_CLERIC && player->cls <= CLASS_FIGHTER) {
        player->class_level[player->cls] = 1;
    } else if (player->cls >= CLASS_MAGIC_USER && player->cls <= CLASS_MONK) {
        player->class_level[player->cls] = 1;
    } else if (player->cls == CLASS_PALADIN) {
        player->paladin_cures_left = 1;
        player->class_level[SKILL_PALADIN] = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_PROTECTION_FROM_EVIL, player);
    } else if (player->cls == CLASS_RANGER) {
        player->class_level[SKILL_RANGER] = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_RANGER_VS_GIANT, player);
    } else if (player->cls == CLASS_MC_C_F) {
        player->class_level[SKILL_CLERIC] = 1;
        player->class_level[SKILL_FIGHTER] = 1;
        player->exp = 12500;
    } else if (player->cls == CLASS_MC_C_F_M) {
        player->class_level[SKILL_CLERIC] = 1;
        player->class_level[SKILL_FIGHTER] = 1;
        player->class_level[SKILL_MAGIC_USER] = 1;
        player->exp = 8333;
    } else if (player->cls == CLASS_MC_C_R) {
        player->class_level[SKILL_CLERIC] = 1;
        player->class_level[SKILL_RANGER] = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_RANGER_VS_GIANT, player);
        player->exp = 12500;
    } else if (player->cls == CLASS_MC_C_MU) {
        player->class_level[SKILL_CLERIC] = 1;
        player->class_level[SKILL_MAGIC_USER] = 1;
        player->exp = 12500;
    } else if (player->cls == CLASS_MC_C_T) {
        player->class_level[SKILL_CLERIC] = 1;
        player->class_level[SKILL_THIEF] = 1;
        player->exp = 12500;
    } else if (player->cls == CLASS_MC_F_MU) {
        player->class_level[SKILL_FIGHTER] = 1;
        player->class_level[SKILL_MAGIC_USER] = 1;
        player->exp = 12500;
    } else if (player->cls == CLASS_MC_F_T) {
        player->class_level[SKILL_FIGHTER] = 1;
        player->class_level[SKILL_THIEF] = 1;
        player->exp = 12500;
    } else if (player->cls == CLASS_MC_F_MU_T) {
        player->class_level[SKILL_FIGHTER] = 1;
        player->class_level[SKILL_MAGIC_USER] = 1;
        player->class_level[SKILL_THIEF] = 1;
        player->exp = 8333;
    } else if (player->cls == CLASS_MC_MU_T) {
        player->class_level[SKILL_MAGIC_USER] = 1;
        player->class_level[SKILL_THIEF] = 1;
        player->exp = 8333;
    }

    if (player->class_level[SKILL_THIEF] > 0) {
        classcalc_thief_skills(player);
    }

    player->class_flags = 0;
    player->thac0 = 0;

    for (int cls = 0; cls < SKILL_COUNT; cls++) {
        if (player->class_level[cls] > 0) {
            int skill_lvl = player->class_level[cls];

            if (skill_lvl < PARTYMENU_EXP_LEVELS &&
                partymenu_thac0_table[cls][skill_lvl] > player->thac0) {
                player->thac0 = partymenu_thac0_table[cls][skill_lvl];
            }

            /* += rather than |=, and paladin and ranger share bit 0x40, so a
             * cleric/ranger comes out with 0x42 where a straight ranger has
             * 0x40 - and a character who was somehow both paladin and ranger
             * would carry into 0x80. The original adds them. */
            player->class_flags = (u8)(player->class_flags +
                                       class_flag_bits[cls]);
        }
    }

    classcalc_saving_throws(player);

    /* --- alignment --- */

    menu_list_clear(&list);
    menu_list_add_heading(&list, "Pick Alignment");

    /* Column 0 of the row is how many of the nine that follow this class allows. */
    alignment_count = limits_class_alignments[player->cls][0];

    if (alignment_count > CLASS_ALIGNMENT_COLS - 1) {
        log_warn("create character: class %d lists %d alignments", player->cls,
                 alignment_count);
        alignment_count = CLASS_ALIGNMENT_COLS - 1;
    }

    for (int i = 1; i <= alignment_count; i++) {
        snprintf(text, sizeof(text), "  %s",
                 viewplayer_alignment_name(
                     limits_class_alignments[player->cls][i]));
        menu_list_add(&list, text);
    }

    index = 1;

    if (!select_from_list(&index, &list)) {
        roster_release(player);
        return;
    }

    if (index < 1 || index > alignment_count) {
        log_warn("create character: alignment %d is not one of the %d offered",
                 index, alignment_count);
        roster_release(player);
        return;
    }

    player->alignment = limits_class_alignments[player->cls][index];

    menu_list_clear(&list);

    /* --- age --- */

    if (player->cls <= CLASS_MONK) {
        /* limits_race_ages has a row for the seven classes that can be rolled
         * up, so a monk - CLASS_MONK being the eighth - reads past the end. The
         * original does the same and gets away with it because no race offers
         * monk; NULL comes back here instead and the age stays zero. */
        const RaceAge *age = limits_race_age(player->race, player->cls);

        if (age != NULL) {
            player->age = (i16)(effect_roll_dice(age->dice_size,
                                                 age->dice_count) +
                                age->base_age);
        } else {
            log_warn("create character: no starting age for race %d class %d",
                     player->race, player->cls);
        }
    } else {
        /* A multi-class character starts at the oldest their slowest class can
         * be: the full dice rather than a roll of them. Which row is read is the
         * class that ages slowest of the pair. */
        int age_row = -1;

        switch (player->cls) {
        case CLASS_MC_C_F:
        case CLASS_MC_C_F_M:
        case CLASS_MC_C_T:
        case CLASS_MC_C_R:
            age_row = SKILL_CLERIC;
            break;

        case CLASS_MC_F_MU:
        case CLASS_MC_F_MU_T:
        case CLASS_MC_MU_T:
            age_row = SKILL_THIEF;
            break;

        case CLASS_MC_F_T:
            age_row = SKILL_FIGHTER;
            break;

        default:
            break;
        }

        if (age_row >= 0) {
            const RaceAge *age = limits_race_age(player->race, age_row);

            if (age != NULL) {
                player->age = (i16)(age->base_age +
                                    (age->dice_count * age->dice_size));
            }
        }
    }

    selected_backup = gbl.selected_player;
    gbl.selected_player = player;
    viewplayer_display_full(player);

    race = player->race;
    sex = player->sex;

    /* --- roll the stats, and keep rolling until the player is happy --- */

    do {
        int con_adj;

        for (int cls = 0; cls < SKILL_COUNT; cls++) {
            if (player->class_level[cls] > 0) {
                player->class_level[cls] = 1;
            }
        }

        /* Only .full is cleared. Str00's .cur is left holding whatever the last
         * roll put there, which matters at the end of the routine - see the note
         * on the closing Str00 line. */
        for (int s = 0; s < PSTAT_COUNT; s++) {
            player->stats.value[s].full = 0;
        }

        /* Six sets of 3d6+1, keeping the best of the six for each stat
         * separately. The dice come out in stat order, so the sequence the
         * random numbers are drawn in is the original's. */
        for (int i = 0; i < 6; i++) {
            for (int s = 0; s < STAT_COUNT; s++) {
                int roll = effect_roll_dice(6, 3) + 1;

                player->stats.value[s].full =
                    COAB_MAX(player->stats.value[s].full, roll);
            }
        }

        /* Ageing, then the racial and sex limits, then the class minimum, for
         * each of the six in turn. The original wrote the same three calls out
         * six times over in a switch; only Str and Wis have anything after
         * them. */
        for (int s = 0; s < STAT_COUNT; s++) {
            StatValue *sv = &player->stats.value[s];

            stat_value_age_effects(sv, (PlayerStatId)s, race, player->age);
            stat_value_enforce_race_sex(sv, (PlayerStatId)s, race, sex);
            stat_value_enforce_class(sv, (PlayerStatId)s, (ClassId)player->cls);

            if (s == STAT_STR) {
                /* Exceptional strength is a warrior's alone. */
                if (sv->full == 18 &&
                    (player->class_level[SKILL_FIGHTER] > 0 ||
                     player->class_level[SKILL_RANGER] > 0 ||
                     player->class_level[SKILL_PALADIN] > 0)) {
                    stat_value_load(&player->stats.value[PSTAT_STR00],
                                    rnd_int(100) + 1);
                    stat_value_enforce_race_sex(
                        &player->stats.value[PSTAT_STR00], PSTAT_STR00, race,
                        sex);
                }
            } else if (s == STAT_WIS) {
                /* A multi-class cleric is handed the wisdom the class needs
                 * rather than being rerolled for it. */
                if (sv->full < 13 && player->cls >= CLASS_MC_C_F &&
                    player->cls <= CLASS_MC_C_T) {
                    sv->full = 13;
                }
            }

            viewplayer_display_stat(false, s);
        }

        /* hit_point_max is still zero here, so this stores nothing; both are set
         * for real once the dice below have been rolled. The original has it. */
        player->hit_point_current = player->hit_point_max;

        player->attacks_count = 2;
        player->attack1_dice_count_base = 1;
        player->attack1_dice_size_base = 2;
        player->field_125 = 1;
        player->base_movement = 12;

        class_count = 0;

        memset(player->spell_cast_count, 0, sizeof(player->spell_cast_count));

        for (int cls = 0; cls < SKILL_COUNT; cls++) {
            if (player->class_level[cls] > 0) {
                if (cls == SKILL_CLERIC) {
                    player->spell_cast_count[0][0] = 1;
                } else if (cls == SKILL_MAGIC_USER) {
                    player->spell_cast_count[2][0] = 1;
                }

                /* A second roll of unk_1A8C4/unk_1A8C3 dice went here and its
                 * answer was never used; the tables are commented out in the
                 * source we are porting from and are not copied here. */

                if (cls == SKILL_CLERIC) {
                    classcalc_cleric_spells(false, player);

                    /* Every first-level cleric spell there is. The original
                     * walked the whole Spells enum, id 0 included; row 0 of the
                     * casting table is the placeholder for "no spell" and its
                     * level is 0, so it never matched. */
                    for (int id = 1; id < SPELL_CASTING_TABLE_COUNT; id++) {
                        const SpellEntry *entry = spell_entry(id);

                        if (entry != NULL &&
                            entry->spell_class == SPELL_CLASS_CLERIC &&
                            entry->spell_level == 1) {
                            player_learn_spell(player, (Spells)id);
                        }
                    }
                } else if (cls == SKILL_MAGIC_USER) {
                    player_learn_spell(player, SPELL_DETECT_MAGIC_MU);
                    player_learn_spell(player, SPELL_READ_MAGIC);
                    player_learn_spell(player, SPELL_ENLARGE);
                    player_learn_spell(player, SPELL_SLEEP);
                }

                class_count++;
            }
        }

        money_set(&player->money, MONEY_PLATINUM, 300);

        player->hit_point_rolled = partymenu_roll_hit_points(0xff, player);
        player->hit_point_max = player->hit_point_rolled;

        con_adj = partymenu_con_hp_adj(player);

        if (class_count == 0) {
            /* Every class list offers at least one class, so this cannot
             * happen; dividing by it below could not be let stand if it did. */
            log_warn("create character: class %d gave no class levels",
                     player->cls);
            class_count = 1;
        }

        if (con_adj < 0) {
            if (player->hit_point_max > (-con_adj + class_count)) {
                player->hit_point_max =
                    (u8)((player->hit_point_max + con_adj) / class_count);
            } else {
                player->hit_point_max = 1;
            }
        } else {
            player->hit_point_max =
                (u8)((player->hit_point_max + con_adj) / class_count);
        }

        player->hit_point_current = player->hit_point_max;
        player->hit_point_rolled = (u8)(player->hit_point_rolled / class_count);

        /* Trained up to whatever 25000 - or a multi-class character's share of
         * it - buys, with no screen and no questions. The trainer's mask is
         * borrowed for it and put back afterwards. */
        {
            u8 training_mask_backup = gbl.area2_ptr->training_class_mask;

            partymenu_silent_train_player();

            gbl.area2_ptr->training_class_mask = training_mask_backup;
        }

        /* The levels, joined with slashes: the class's own level plus whatever
         * an abandoned class left behind. */
        {
            bool first_lvl = true;
            size_t used = 0;

            text[0] = '\0';

            for (int cls = 0; cls < SKILL_COUNT; cls++) {
                if (player->class_level[cls] > 0 ||
                    (player->class_level_old[cls] <
                         player_dual_class_current_level(player) &&
                     player->class_level_old[cls] > 0)) {
                    u8 lvl = (u8)(player->class_level_old[cls] +
                                  player->class_level[cls]);
                    int written;

                    written = snprintf(text + used, sizeof(text) - used, "%s%u",
                                       first_lvl ? "" : "/", (unsigned)lvl);

                    if (written < 0 || (size_t)written >= sizeof(text) - used) {
                        log_warn("create character: level line too long");
                        break;
                    }

                    used += (size_t)written;
                    first_lvl = false;
                }
            }

            text_display_string(text, 0, 15, 15, 7);
        }

        viewplayer_display_stats01();
        viewplayer_display_money();

        input_key = prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Reroll stats? ");
    } while (input_key != 'N' && input_quit_requested() == false);

    viewplayer_display_full(player);

    /* A name is not optional, so the prompt comes back until there is one. */
    do {
        text_get_user_input_string(player->name, sizeof(player->name), 15, 0, 13,
                                  "Character name: ");
    } while (player->name[0] == '\0' && input_quit_requested() == false);

    partymenu_icon_builder();

    /* The six stats' .cur were copied from .full here in the original and the
     * lines are commented out in the source we are porting from; the stat limit
     * calls have already left the two equal.
     *
     * Str00 is different, and this line is why. The reroll loop only clears
     * .full, so a character who rolled an 18 strength once and then rerolled to
     * something lower still has the old percentile sitting in .cur - and this
     * puts it back into .full. A wizard with a percentile strength is the
     * result, and the original does it. */
    player->stats.value[PSTAT_STR00].full = player->stats.value[PSTAT_STR00].cur;

    snprintf(text, sizeof(text), "Save %s? ", player->name);
    input_key = prompt_yes_no(GBL_DEFAULT_MENU_COLORS, text);

    if (input_key == 'Y') {
        savegame_save_player("", player);
    }

    gbl.selected_player = selected_backup;

    /* The new character is never put on the team list: they are written out to
     * be added later with Add Character. Nothing else holds them, so the pool
     * slot goes back. */
    partymenu_free_player(player);
    roster_release(player);
}

/* ovr018.con_bonus */
int partymenu_con_bonus(ClassId class_id)
{
    int stat;

    if (gbl.selected_player == NULL) {
        log_warn("hit points: nobody selected to read a constitution from");
        return 0;
    }

    /* The constitution comes off the selected character rather than off an
     * argument, which is how the original wrote it - so calc_max_hp asking about
     * some other character's classes still reads this one's constitution. */
    stat = gbl.selected_player->stats.value[PSTAT_CON].full;

    if (stat == 3) {
        return -2;
    }
    if (stat >= 4 && stat <= 6) {
        return -1;
    }
    if (stat >= 7 && stat <= 14) {
        return 0;
    }
    if (stat == 15) {
        return 1;
    }
    if (stat == 16) {
        return 1;
    }

    /* Everything above 16 - and, because nothing catches it, everything below 3
     * as well - lands here. Only a warrior gets more than two. */
    if (class_id == CLASS_FIGHTER || class_id == CLASS_RANGER ||
        class_id == CLASS_PALADIN) {
        return stat - 14;
    }

    return 2;
}

/* ovr018.dropPlayer */
void partymenu_drop_player(void)
{
    if (gbl.selected_player != NULL) {
        Player *player = gbl.selected_player;
        char text[64];

        snprintf(text, sizeof(text), "Drop %s forever? ", player->name);

        if (prompt_yes_no(GBL_ALERT_MENU_COLORS, text) == 'Y' &&
            prompt_yes_no(GBL_ALERT_MENU_COLORS, "Are you sure? ") == 'Y') {
            /* in_combat is set on anyone who joined the party from a script, so
             * an NPC takes their leave and a character rolled up here is put out
             * the back door. */
            if (player->in_combat == false) {
                snprintf(text, sizeof(text), "You dump %s out back.",
                         player->name);
            } else {
                snprintf(text, sizeof(text), "%s bids you farewell.",
                         player->name);
            }

            character_print_message(text);

            savegame_remove_player_file(player);

            /* The record is emptied but not handed back to the roster; the note
             * on partymenu_free_current_player says why. */
            gbl.selected_player =
                partymenu_free_current_player(gbl.selected_player, true, false);
        } else {
            snprintf(text, sizeof(text), "%s breathes a sigh of relief.",
                     player->name);
            character_print_message(text);
        }
    }

    character_party_summary(gbl.selected_player);
}

/* ovr018.draw_highlight_stat, sub_4E6F2 */
void partymenu_draw_highlight_stat(bool highlighted, u8 edited_stat,
                                   int name_cursor_pos)
{
    if (gbl.selected_player == NULL) {
        log_warn("modify: nobody selected to draw");
        return;
    }

    if (edited_stat <= 5) {
        viewplayer_display_stat(highlighted, edited_stat);
    } else if (edited_stat == 6) {
        character_display_hp(highlighted, 18, 4, gbl.selected_player);
    } else if (edited_stat == 7) {
        const char *name = gbl.selected_player->name;
        int len = (int)strlen(name);

        if (name_cursor_pos < 1) {
            log_warn("modify: name cursor at %d", name_cursor_pos);
            name_cursor_pos = 1;
        }

        if (highlighted) {
            text_display_space_char(1, len + 1);
            text_display_string(name, 0, 13, 1, 1);

            /* The cursor is the character it sits on, redrawn in white - or a
             * '%' where there is no character to redraw. */
            if (name_cursor_pos > len || name[name_cursor_pos - 1] == ' ') {
                text_display_string("%", 0, 15, 1, name_cursor_pos);
            } else {
                char one[2];

                one[0] = name[name_cursor_pos - 1];
                one[1] = '\0';
                text_display_string(one, 0, 15, 1, name_cursor_pos);
            }
        } else {
            text_display_string(name, 0, 10, 1, 1);
        }
    }
}

/* Deletes the character at `at` from a NUL-terminated name in place. */
static void name_delete_at(char *name, int at)
{
    int len = (int)strlen(name);

    if (at < 0 || at >= len) {
        return;
    }

    memmove(name + at, name + at + 1, (size_t)(len - at));
}

void partymenu_modify_player(void)
{
    Player *player = gbl.selected_player;
    PlayerStats stats_bkup;
    char name_backup[PLAYER_NAME_MAX + 1];
    u8 orig_hp_max;
    u8 hp_count;
    int name_cursor_pos = 1;
    u8 edited_stat;
    bool control_key;
    char input_key;

    if (player == NULL) {
        log_warn("modify: nobody selected to modify");
        return;
    }

    /* && binds tighter than ||, so this reads as
     *   (not allowed to cheat AND the experience is not a starting figure)
     *   OR the character has dual-classed
     * which is to say a character may only be modified before they have earned
     * anything. The four figures are what character creation hands out: 25000
     * for a single class, 12500 for two and 8333 for three. */
    if ((cheats.allow_player_modify == false &&
         (player->exp != 0 && player->exp != 8333 && player->exp != 12500 &&
          player->exp != 25000)) ||
        player->multiclass_level != 0) {
        char text[64];

        snprintf(text, sizeof(text), "%s can't be modified.", player->name);
        text_display_status(0, 14, text);
        return;
    }

    viewplayer_display_full(player);

    player_stats_assign(&stats_bkup, &player->stats);
    orig_hp_max = player->hit_point_max;
    snprintf(name_backup, sizeof(name_backup), "%s", player->name);

    /* The name is drawn unhighlighted first so the sheet shows it in green, then
     * the highlight is put on the first stat. */
    edited_stat = 7;
    partymenu_draw_highlight_stat(false, edited_stat, name_cursor_pos);
    edited_stat = 0;
    partymenu_draw_highlight_stat(true, edited_stat, name_cursor_pos);

    do {
        if (edited_stat == 7) {
            /* No prompt while the name is being typed: the keys are read raw so
             * that letters go into the name instead of picking menu words. */
            while (input_key_pressed() == false &&
                   input_quit_requested() == false) {
                /* empty */
            }

            input_key = (char)input_get_key();

            if (input_key == 0) {
                input_key = (char)input_get_key();
                control_key = true;
            } else {
                control_key = false;
            }

            if (input_key == 0x1b) {
                input_key = '\0';
            }
        } else {
            input_key = prompt_display_input(&control_key, false, 1,
                                            GBL_DEFAULT_MENU_COLORS,
                                            "Keep Exit", "Modify: ");
        }

        partymenu_draw_highlight_stat(false, edited_stat, name_cursor_pos);

        if (control_key) {
            /* The one screen where the original's choice of keys is plainly a
             * mistake rather than a convention: the highlight moves a line at a
             * time on Home and End while Up and Down do nothing at all. Both work
             * now; see prompt_selection_key. */
            switch (prompt_selection_key(input_key, control_key)) {
            case 'S':   /* Delete */
                if (edited_stat == 7 && strlen(player->name) > 1) {
                    int len = (int)strlen(player->name);

                    if (name_cursor_pos == len) {
                        player->name[len - 1] = '\0';
                        name_cursor_pos = len - 1;
                    } else {
                        /* The original asked Substring for one character more
                         * than the name holds, which throws in the C#. Clamped
                         * to the rest of the name here, which deletes the
                         * character just past the cursor - crashing not being a
                         * behaviour worth reproducing. */
                        name_delete_at(player->name, name_cursor_pos);
                    }
                }
                break;

            case 'O':   /* End or Down: the next line */
                edited_stat++;

                if (edited_stat > 7) {
                    edited_stat = 0;
                }
                break;

            case 'G':   /* Home or Up: the line before. 0 - 1 wraps a byte
                         * to 0xff. */
                edited_stat = (u8)(edited_stat - 1);

                if (edited_stat == 0xff) {
                    edited_stat = 7;
                }
                break;

            case 'K':   /* Left: lower whatever is highlighted */
                if (edited_stat < 6) {
                    int stat_var = edited_stat;
                    int race = player->race;
                    int sex = player->sex;
                    StatValue *sv = &player->stats.value[stat_var];

                    player_stats_dec(&player->stats, stat_var);

                    switch (stat_var) {
                    case STAT_STR:
                        /* Exceptional strength comes off before the 18 does. */
                        if (player->stats.value[PSTAT_STR00].cur > 0) {
                            stat_value_dec(&player->stats.value[PSTAT_STR00]);
                            stat_value_inc(sv);
                        } else {
                            stat_value_enforce_race_sex(sv, PSTAT_STR, race,
                                                        sex);
                        }
                        stat_value_enforce_class(sv, PSTAT_STR,
                                                 (ClassId)player->cls);
                        break;

                    case STAT_INT:
                        stat_value_enforce_race_sex(sv, PSTAT_INT, race, sex);
                        stat_value_enforce_class(sv, PSTAT_INT,
                                                 (ClassId)player->cls);
                        break;

                    case STAT_WIS:
                        stat_value_enforce_race_sex(sv, PSTAT_WIS, race, sex);
                        stat_value_enforce_class(sv, PSTAT_WIS,
                                                 (ClassId)player->cls);

                        if (player->spell_cast_count[0][0] > 0) {
                            player->spell_cast_count[0][0] = 1;
                        }
                        break;

                    case STAT_DEX:
                        stat_value_enforce_race_sex(sv, PSTAT_DEX, race, sex);
                        stat_value_enforce_class(sv, PSTAT_DEX,
                                                 (ClassId)player->cls);
                        break;

                    case STAT_CON: {
                        int max_hp;

                        stat_value_enforce_race_sex(sv, PSTAT_CON, race, sex);
                        stat_value_enforce_class(sv, PSTAT_CON,
                                                 (ClassId)player->cls);

                        max_hp = partymenu_calc_max_hp(player);

                        if (max_hp < player->hit_point_max) {
                            player->hit_point_max = (u8)max_hp;
                        }

                        player->hit_point_current = player->hit_point_max;

                        /* The hit-point line is redrawn unhighlighted because it
                         * has just changed, and then the highlight is put back
                         * on constitution. */
                        edited_stat = 6;
                        partymenu_draw_highlight_stat(false, edited_stat,
                                                     name_cursor_pos);
                        edited_stat = 4;
                        break;
                    }

                    case STAT_CHA:
                        stat_value_enforce_race_sex(sv, PSTAT_CHA, race, sex);
                        stat_value_enforce_class(sv, PSTAT_CHA,
                                                 (ClassId)player->cls);
                        break;

                    default:
                        break;
                    }
                } else if (edited_stat == 6) {
                    player->hit_point_max--;

                    if (partymenu_min_hit_points(player) >
                        player->hit_point_max) {
                        player->hit_point_max =
                            (u8)partymenu_min_hit_points(player);
                    }

                    player->hit_point_current = player->hit_point_max;
                } else {
                    if (name_cursor_pos == 1) {
                        name_cursor_pos = (int)strlen(player->name);
                    } else {
                        name_cursor_pos--;
                    }
                }
                break;

            case 'M':   /* Right: raise whatever is highlighted */
                if (edited_stat < 6) {
                    int stat_var = edited_stat;
                    int race = player->race;
                    int sex = player->sex;
                    StatValue *sv = &player->stats.value[stat_var];

                    player_stats_inc(&player->stats, stat_var);

                    switch (stat_var) {
                    case STAT_STR:
                        stat_value_enforce_race_sex(sv, PSTAT_STR, race, sex);

                        if (sv->full == 18 &&
                            (player->class_level[SKILL_FIGHTER] > 0 ||
                             player->class_level[SKILL_RANGER] > 0 ||
                             player->class_level[SKILL_PALADIN] > 0)) {
                            stat_value_inc(&player->stats.value[PSTAT_STR00]);
                            stat_value_enforce_race_sex(
                                &player->stats.value[PSTAT_STR00], PSTAT_STR00,
                                race, sex);
                        } else {
                            stat_value_load(&player->stats.value[PSTAT_STR00],
                                            0);
                        }
                        break;

                    case STAT_INT:
                        stat_value_enforce_race_sex(sv, PSTAT_INT, race, sex);
                        break;

                    case STAT_WIS:
                        stat_value_enforce_race_sex(sv, PSTAT_WIS, race, sex);

                        if (player->spell_cast_count[0][0] > 0) {
                            player->spell_cast_count[0][0] = 1;
                        }
                        break;

                    case STAT_DEX:
                        stat_value_enforce_race_sex(sv, PSTAT_DEX, race, sex);
                        break;

                    case STAT_CON:
                        stat_value_enforce_race_sex(sv, PSTAT_CON, race, sex);

                        /* Raising it can only raise the floor, so this is the
                         * minimum where lowering it tested the maximum. */
                        if (partymenu_min_hit_points(player) >
                            player->hit_point_max) {
                            player->hit_point_max =
                                (u8)partymenu_min_hit_points(player);
                        }

                        player->hit_point_current = player->hit_point_max;

                        edited_stat = 6;
                        partymenu_draw_highlight_stat(false, edited_stat,
                                                     name_cursor_pos);
                        edited_stat = 4;
                        break;

                    case STAT_CHA:
                        stat_value_enforce_race_sex(sv, PSTAT_CHA, race, sex);
                        break;

                    default:
                        break;
                    }
                } else {
                    if (edited_stat == 6) {
                        player->hit_point_max++;

                        if (partymenu_calc_max_hp(player) <
                            player->hit_point_max) {
                            player->hit_point_max =
                                (u8)partymenu_calc_max_hp(player);
                        }

                        player->hit_point_current = player->hit_point_max;
                    } else {
                        if (name_cursor_pos == (int)strlen(player->name) + 1) {
                            name_cursor_pos = 1;
                        } else {
                            name_cursor_pos++;
                        }
                    }
                }
                break;

            default:
                break;
            }
        } else {
            if (input_key == 0x0d) {
                edited_stat++;

                if (edited_stat > 7) {
                    edited_stat = 0;
                }
            } else if (input_key == 0x08) {
                /* Backspace takes out the character under the cursor. */
                if (name_cursor_pos > 1 && edited_stat > 6) {
                    name_delete_at(player->name, name_cursor_pos - 1);

                    if (name_cursor_pos > (int)strlen(player->name)) {
                        name_cursor_pos = (int)strlen(player->name);
                    }
                }
            } else if (input_key >= 0x20 && input_key <= 0x7a) {
                if (edited_stat > 6) {
                    if (name_cursor_pos <= 15) {
                        int len = (int)strlen(player->name);
                        int insert = name_cursor_pos - 1;

                        /* The original built this as a string join whose second
                         * half started one character past the cursor, so a
                         * letter typed over the name replaces what is under the
                         * cursor rather than pushing it along. Only typing at
                         * the end lengthens the name. */
                        if (insert >= 0 && insert < PLAYER_NAME_MAX) {
                            player->name[insert] = input_key;

                            if (insert >= len) {
                                player->name[insert + 1] = '\0';
                            }
                        }

                        name_cursor_pos++;

                        if (name_cursor_pos > 15) {
                            name_cursor_pos = 15;
                        }

                        /* A PadRight went here whose result the original threw
                         * away, so the name was never padded. Nothing to do. */

                        input_key = '\0';
                    }
                } else if (input_key == 0x45) {
                    /* 'E' for Exit, and it is the else of the name test above -
                     * so an E typed into the name is a letter, and an E anywhere
                     * else puts everything back. */
                    player_stats_assign(&player->stats, &stats_bkup);

                    player->hit_point_max = orig_hp_max;
                    player->hit_point_current = player->hit_point_max;

                    snprintf(player->name, sizeof(player->name), "%s",
                             name_backup);

                    character_recalc_values(player);
                    return;
                }
            } else if (input_key == 0) {
                /* Escape, and the same restore. */
                player_stats_assign(&player->stats, &stats_bkup);

                player->hit_point_max = orig_hp_max;
                snprintf(player->name, sizeof(player->name), "%s", name_backup);

                player->hit_point_current = player->hit_point_max;
                character_recalc_values(player);
                return;
            }
        }

        character_recalc_values(player);
        viewplayer_display_stats01();

        partymenu_draw_highlight_stat(true, edited_stat, name_cursor_pos);
    } while ((control_key || input_key != 0x4b) &&
             input_quit_requested() == false);

    /* Keep. The wisdom that has been settled on decides the cleric spells, and
     * an NPC modified here takes a full share of treasure. */
    classcalc_cleric_spells(true, player);

    player->npc_treasure_share_count = 1;

    /* hit_point_rolled is the dice half of the hit points, so it is what is left
     * once the constitution bonus is taken back off the total the player has
     * settled on. orig_hp_max is reused as the accumulator, byte-wide, so a
     * negative bonus wraps here exactly as it did in the original. */
    orig_hp_max = 0;
    hp_count = 0;

    for (int cls = 0; cls < SKILL_COUNT; cls++) {
        if (player->class_level[cls] > 0) {
            if (player->class_level[cls] < limits_max_class_hit_dice[cls]) {
                /* A ranger's first level is two dice, so their levels count one
                 * higher. */
                if (cls == SKILL_RANGER) {
                    orig_hp_max = (u8)(orig_hp_max +
                                       (player->class_level[cls] + 1) *
                                           partymenu_con_bonus((ClassId)cls));
                } else {
                    orig_hp_max = (u8)(orig_hp_max +
                                       player->class_level[cls] *
                                           partymenu_con_bonus((ClassId)cls));
                }
            } else {
                orig_hp_max = (u8)(orig_hp_max +
                                   (limits_max_class_hit_dice[cls] - 1) *
                                       partymenu_con_bonus((ClassId)cls));
            }
            hp_count++;
        }
    }

    if (hp_count == 0) {
        log_warn("modify: %s has no class levels to divide hit points between",
                 player->name);
        return;
    }

    orig_hp_max = (u8)(orig_hp_max / hp_count);

    player->hit_point_rolled = (u8)(player->hit_point_max - orig_hp_max);

    /* Copying the six stats' .cur from .full and Str00's .full from .cur went
     * here and both are commented out in the source we are porting from; the
     * stat limit calls above have already left every pair equal. */
}

void partymenu_add_player(void)
{
    static MenuList path_list;
    static MenuList name_list;

    char input_key;
    int pc_count = 0;
    int list_index = 0;
    bool menu_redraw = true;

    frames_clear_area(0x16, 0x26, 1, 1);

    input_key = prompt_display_input_simple(false, 0, GBL_DEFAULT_MENU_COLORS,
                                           "Curse Pool Hillsfar Exit",
                                           "Add from where? ");

    switch (input_key) {
    case 'C':
        gbl.import_from = IMPORT_SOURCE_CURSE;
        break;

    case 'P':
        gbl.import_from = IMPORT_SOURCE_POOL;
        break;

    case 'H':
        gbl.import_from = IMPORT_SOURCE_HILLSFAR;
        break;

    case 'E':
    case '\0':
        return;

    default:
        break;
    }

    savegame_build_loadable_players_lists(&path_list, &name_list);

    if (name_list.count == 0) {
        return;
    }

    do {
        MenuItem *chosen = NULL;

        input_key = prompt_select_item(&chosen, &list_index, &menu_redraw, true,
                                      &name_list, 22, 38, 2, 1,
                                      GBL_DEFAULT_MENU_COLORS, "Add",
                                      "Add a character: ");

        /* A name already marked with a star is in the party and cannot be added
         * twice. */
        if ((input_key == 13 || input_key == 'A') && chosen != NULL &&
            chosen->text[0] != '*') {
            Player *new_player;
            MenuItem *path;

            prompt_clear_area();

            new_player = roster_alloc();

            if (new_player == NULL) {
                log_warn("add character: no room for another character");
                return;
            }

            path = menu_list_get(&path_list, list_index);

            savegame_import_char(new_player, (path != NULL) ? path->text : NULL);

            {
                char starred[MENU_ITEM_TEXT_MAX];

                /* The precision keeps the two characters of the star in the
                 * buffer whatever the entry is; a name is fifteen at most, so
                 * nothing is ever actually cut. */
                snprintf(starred, sizeof(starred), "* %.*s",
                         (int)(sizeof(starred) - 3), chosen->text);
                menu_item_set(chosen, starred, chosen->heading, chosen->item);
            }

            pc_count = 0;

            if (gbl.team_count == 0) {
                gbl.area2_ptr->party_size = 0;
                partymenu_assign_player_icon_id(new_player);
                partymenu_load_player_combat_icon(true);
            } else {
                bool paladin_present = false;
                const char *paladins_name = "";
                bool evil_present = false;
                int ranger_count = 0;
                bool found = false;

                /* The counting stops at the first character with the same name
                 * and mod id, so the party rules below are checked against
                 * however much of the party was walked. The original breaks out
                 * the same way. */
                for (int i = 0; i < gbl.team_count; i++) {
                    const Player *tmp_player = gbl.team_list[i];

                    if (tmp_player == NULL) {
                        continue;
                    }

                    if (strcmp(tmp_player->name, new_player->name) == 0 &&
                        tmp_player->mod_id == new_player->mod_id) {
                        found = true;
                        break;
                    }

                    if (tmp_player->control_morale < CONTROL_NPC_BASE) {
                        pc_count++;
                    }

                    if (tmp_player->class_level[SKILL_RANGER] > 0) {
                        ranger_count++;
                    }

                    /* Alignments run good, neutral, evil, so every third one
                     * counting from 1 is an evil one. */
                    if ((tmp_player->alignment + 1) % 3 == 0) {
                        evil_present = true;
                    }

                    if (tmp_player->class_level[SKILL_PALADIN] > 0) {
                        paladin_present = true;
                        paladins_name = tmp_player->name;
                    }
                }

                if (found == false &&
                    ((new_player->control_morale < CONTROL_NPC_BASE &&
                      pc_count < 6) ||
                     (new_player->control_morale >= CONTROL_NPC_BASE &&
                      gbl.area2_ptr->party_size < 8)) &&
                    (new_player->class_level[SKILL_PALADIN] == 0 ||
                     evil_present == false) &&
                    (new_player->class_level[SKILL_RANGER] == 0 ||
                     ranger_count < 3) &&
                    (((new_player->alignment + 1) % 3) != 0 ||
                     paladin_present == false)) {
                    partymenu_assign_player_icon_id(new_player);
                    partymenu_load_player_combat_icon(true);

                    if (new_player->control_morale < CONTROL_NPC_BASE) {
                        pc_count++;
                    }
                } else {
                    char text[80];

                    /* The star comes back off: they did not join. */
                    snprintf(text, sizeof(text), "%s", chosen->text + 2);
                    menu_item_set(chosen, text, chosen->heading, chosen->item);

                    if (new_player->class_level[SKILL_PALADIN] > 0 &&
                        evil_present) {
                        character_print_message(
                            "paladins do not join with evil scum");
                        text_game_delay();
                    } else if (new_player->class_level[SKILL_RANGER] > 0 &&
                               ranger_count > 2) {
                        character_print_message("too many rangers in party");
                    } else if (((new_player->alignment + 1) % 3) == 0 &&
                               paladin_present) {
                        snprintf(text, sizeof(text),
                                 "%s will tolerate no evil!", paladins_name);
                        character_print_message(text);
                    }

                    partymenu_free_player(new_player);
                    roster_release(new_player);
                }
            }
        }
    } while (input_key != 0x45 && input_key != '\0' && pc_count <= 5 &&
             gbl.area2_ptr->party_size <= 7 &&
             input_quit_requested() == false);
}

/* ovr018.FreeCurrentPlayer, free_players */
Player *partymenu_free_current_player(Player *player, bool free_icon,
                                      bool leave_party_size)
{
    int index = gbl_team_index_of(player);

    if (index >= 0) {
        gbl_team_remove_at(index);

        if (free_icon) {
            icons_release_combat_icon(player->icon_id);
        }

        if (!leave_party_size) {
            gbl.area2_ptr->party_size--;
        }

        partymenu_free_player(player);

        /* The character before the one that has gone, and the first character
         * when it was the first that went. */
        index = index > 0 ? index - 1 : 0;

        if (gbl.team_count > 0) {
            return gbl.team_list[index];
        }
    }

    return NULL;
}

/* ovr018.drawIconEditorIcons, sub_4FB7C */
void partymenu_draw_icon_editor_icons(int title_y, int title_x)
{
    draw_color_block(0, 24, 12, title_y * 24, title_x * 3);

    /* Nothing in the game ever loads combat icon 25, so these two draw nothing.
     * The original has them and they are kept for that reason. */
    icons_draw_combat_icon(25, COMBAT_ICON_NORMAL, 0, title_y, title_x);
    icons_draw_combat_icon(25, COMBAT_ICON_ATTACK, 0, title_y, title_x + 3);

    /* Slot 12 is the editor's scratch copy: the icon as it stands. */
    icons_draw_combat_icon(12, COMBAT_ICON_NORMAL, 0, title_y, title_x);
    icons_draw_combat_icon(12, COMBAT_ICON_ATTACK, 0, title_y, title_x + 3);

    /* seg040.DrawOverlay() went here; it does nothing. */
}

/* ovr018.duplicateCombatIcon, sub_4FC5B */
void partymenu_duplicate_combat_icon(bool recolour, int dest_index,
                                     int source_index)
{
    if (dest_index < 0 || dest_index >= GBL_COMBAT_ICON_COUNT ||
        source_index < 0 || source_index >= GBL_COMBAT_ICON_COUNT) {
        log_warn("icon editor: cannot copy combat icon %d onto %d",
                 source_index, dest_index);
        return;
    }

    combat_icon_duplicate(&gbl.combat_icons[dest_index],
                          &gbl.combat_icons[source_index], recolour,
                          gbl.selected_player);
}

void partymenu_icon_builder(void)
{
    /* ovr018.iconStrings. Level 3's line is rewritten in place when the player
     * picks which of the two colours they are changing, so this cannot be a
     * table of constants: "xxxx" is never seen, being replaced by Hair or Face
     * before level 3 is ever shown. */
    const char *icon_strings[6] = {
        "",
        "Parts 1st-color 2nd-color Size Exit",
        "Head Weapon Exit",
        "Weapon Body xxxx Shield Arm Leg Exit",
        " Keep Exit",
        "Next Prev Keep Exit"
    };

    Player *player;
    char sub_menu_key = '\0';
    u8 return_level = 0;
    bool second_color = false;
    u8 color_index = 0;
    char input_key;

    if (gbl.selected_player == NULL) {
        log_warn("icon editor: nobody selected to draw an icon for");
        return;
    }

    frames_draw_outer();
    combatmap_color_0_8_inverse();

    do {
        u8 bkup_colours[GBL_ICON_COLOUR_COUNT];
        u8 bkup_icon_id;
        u8 bkup_size;
        u8 head_icon;
        u8 weapon_icon;
        u8 level;

        partymenu_load_player_combat_icon(false);

        player = gbl.selected_player;

        level = 1;
        memcpy(bkup_colours, player->icon_colours, sizeof(bkup_colours));

        /* Slot 12 holds the icon being edited, loaded by pointing the character
         * at it for one call. */
        bkup_icon_id = player->icon_id;
        player->icon_id = 0x0c;
        partymenu_load_player_combat_icon(false);
        player->icon_id = bkup_icon_id;

        head_icon = player->head_icon;
        weapon_icon = player->weapon_icon;
        bkup_size = player->icon_size;

        partymenu_duplicate_combat_icon(true, 12, player->icon_id);

        /* Drawn once, so the top pair stays as the icon was when the editor
         * opened while the bottom pair follows the edits. */
        partymenu_draw_icon_editor_icons(2, 1);

        text_display_string("old", 0, 15, 6, 8);
        text_display_string("ready   action", 0, 15, 10, 3);
        text_display_string("new", 0, 15, 12, 8);
        text_display_string("ready   action", 0, 15, 16, 3);

        do {
            char text[64];
            bool special_key;

            partymenu_draw_icon_editor_icons(4, 1);

            if (level == 4) {
                /* The size line offers whichever size the icon is not. */
                snprintf(text, sizeof(text), "%s%s",
                         (player->icon_size == 2) ? "Small" : "Large",
                         icon_strings[4]);
            } else if (level < COAB_ARRAY_LEN(icon_strings)) {
                snprintf(text, sizeof(text), "%s", icon_strings[level]);
            } else {
                log_warn("icon editor: no menu for level %d", level);
                snprintf(text, sizeof(text), "%s", icon_strings[1]);
                level = 1;
            }

            input_key = prompt_display_input(&special_key, false, 0,
                                            GBL_DEFAULT_MENU_COLORS, text, "");

            if (special_key == false) {
                switch (level) {
                case 1:
                    return_level = 1;

                    switch (input_key) {
                    case 'P':
                        level = 2;
                        break;

                    case '1':
                        level = 3;
                        second_color = false;
                        icon_strings[3] =
                            "Weapon Body Hair Shield Arm Leg Exit";
                        break;

                    case '2':
                        level = 3;
                        second_color = true;
                        icon_strings[3] =
                            "Weapon Body Face Shield Arm Leg Exit";
                        break;

                    case 'S':
                        level = 4;
                        break;

                    case 'E':
                        /* The only way out of the inner loop. */
                        return_level = 0;
                        break;

                    default:
                        break;
                    }
                    break;

                case 2:
                    return_level = 2;

                    /* unk_4FE94 = { '\0', 'E' }: Escape or Exit steps back up. */
                    if (input_key == '\0' || input_key == 'E') {
                        level = 1;
                    } else {
                        sub_menu_key = input_key;
                        level = 5;
                    }
                    break;

                case 3:
                    return_level = 3;

                    /* Which of the six icon colours the Next/Prev level will
                     * step through. */
                    switch (input_key) {
                    case 'W':
                        color_index = 5;
                        break;
                    case 'B':
                        color_index = 0;
                        break;
                    case 'H':
                    case 'F':
                        color_index = 3;
                        break;
                    case 'S':
                        color_index = 4;
                        break;
                    case 'A':
                        color_index = 1;
                        break;
                    case 'L':
                        color_index = 2;
                        break;
                    default:
                        color_index = 0;
                        break;
                    }

                    if (input_key == '\0' || input_key == 'E') {
                        level = 1;
                    } else {
                        level = 5;
                    }
                    break;

                case 4:
                    switch (input_key) {
                    case 'L':
                        player->icon_size = 2;
                        partymenu_load_player_combat_icon(false);
                        break;

                    case 'S':
                        player->icon_size = 1;
                        partymenu_load_player_combat_icon(false);
                        break;

                    case 'K':
                        bkup_size = player->icon_size;
                        level = 1;
                        input_key = ' ';
                        break;

                    case 'E':
                    case '\0':
                        player->icon_size = bkup_size;
                        level = 1;
                        input_key = ' ';
                        break;

                    default:
                        break;
                    }

                    /* Reloaded again on the way out of the switch, so picking a
                     * size loads twice. The original does it this way. */
                    partymenu_load_player_combat_icon(false);
                    break;

                case 5:
                    if (return_level == 2) {
                        if (sub_menu_key == 'H') {
                            if (input_key == 'P') {
                                player->head_icon = (u8)sys_wrap_min_max(
                                    player->head_icon - 1, 0, 13);
                            } else if (input_key == 'N') {
                                player->head_icon = (u8)sys_wrap_min_max(
                                    player->head_icon + 1, 0, 13);
                            } else if (input_key == 'K') {
                                /* Keep: this becomes the head to fall back to. */
                                head_icon = player->head_icon;
                                level = return_level;
                                input_key = ' ';
                            } else if (input_key == 'E' || input_key == '\0') {
                                player->head_icon = head_icon;
                                level = return_level;
                                input_key = ' ';
                            }

                            partymenu_load_player_combat_icon(false);
                        } else if (sub_menu_key == 'W') {
                            /* The weapon wraps by hand over 0..0x1f rather than
                             * through sys_wrap_min_max. */
                            if (input_key == 'P') {
                                if (player->weapon_icon > 0) {
                                    player->weapon_icon--;
                                } else {
                                    player->weapon_icon = 0x1f;
                                }
                            } else if (input_key == 'N') {
                                if (player->weapon_icon < 0x1f) {
                                    player->weapon_icon++;
                                } else {
                                    player->weapon_icon = 0;
                                }
                            } else if (input_key == 'K') {
                                weapon_icon = player->weapon_icon;
                                level = return_level;
                                input_key = ' ';
                            } else if (input_key == 'E' || input_key == '\0') {
                                player->weapon_icon = weapon_icon;
                                level = return_level;
                                input_key = ' ';
                            }

                            partymenu_load_player_combat_icon(false);
                        }
                    } else if (return_level == 3) {
                        /* Each icon colour byte is two nibbles: the low one is
                         * the colour, the high one its bright version. */
                        u8 low_color = (u8)(player->icon_colours[color_index] &
                                            0x0f);
                        u8 high_color =
                            (u8)((player->icon_colours[color_index] & 0xf0) >>
                                 4);

                        if (input_key == 'N') {
                            if (second_color) {
                                high_color = (u8)((high_color + 1) % 16);
                            } else {
                                low_color = (u8)((low_color + 1) % 16);
                            }

                            player->icon_colours[color_index] =
                                (u8)(low_color + (high_color << 4));
                        } else if (input_key == 'P') {
                            if (second_color) {
                                high_color = (u8)((high_color - 1) & 0x0f);
                            } else {
                                low_color = (u8)((low_color - 1) & 0x0f);
                            }

                            player->icon_colours[color_index] =
                                (u8)(low_color + (high_color << 4));
                        } else if (input_key == 'K') {
                            memcpy(bkup_colours, player->icon_colours,
                                   sizeof(bkup_colours));
                            level = return_level;
                            input_key = ' ';
                        } else if (input_key == 'E' || input_key == '\0') {
                            memcpy(player->icon_colours, bkup_colours,
                                   sizeof(bkup_colours));
                            level = return_level;
                            input_key = ' ';
                        }
                    }
                    break;

                default:
                    break;
                }
            }

            partymenu_duplicate_combat_icon(true, 12, player->icon_id);
        } while ((return_level != 0 ||
                  (input_key != '\0' && input_key != 'E')) &&
                 input_quit_requested() == false);

        /* Whatever was kept at each level is now the character's, and everything
         * else goes back to how it was. */
        player->head_icon = head_icon;
        player->weapon_icon = weapon_icon;
        player->icon_size = bkup_size;
        memcpy(player->icon_colours, bkup_colours, sizeof(bkup_colours));

        partymenu_duplicate_combat_icon(true, 12, player->icon_id);
        partymenu_duplicate_combat_icon(false, player->icon_id, 12);

        prompt_clear_area();
        icons_release_combat_icon(12);

        input_key = prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Is this icon ok? ");
    } while (input_key != 'Y' && input_quit_requested() == false);

    combatmap_color_0_8_normal();
}

/* ovr018.con_hp_adj, seg600:4281. Indexed by constitution, so 0 to 25. */
#define CON_HP_ADJ_COUNT 26

static const i8 con_hp_adj[CON_HP_ADJ_COUNT] = {
    0, 0, 0, -2, -1, -1, -1, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
};

/* ovr018.get_con_hp_adj */
int partymenu_con_hp_adj(const Player *player)
{
    i8 hp_adj = 0;
    int con;

    if (player == NULL) {
        log_warn("hit points: no character to work out a constitution bonus for");
        return 0;
    }

    con = player->stats.value[PSTAT_CON].full;

    if (con < 0 || con >= CON_HP_ADJ_COUNT) {
        log_warn("hit points: constitution %d is off the adjustment table", con);
        return 0;
    }

    for (int cls = 0; cls <= SKILL_MONK; cls++) {
        if (player->class_level[cls] > 0 &&
            player->class_level[cls] < limits_max_class_hit_dice[cls]) {
            hp_adj = (i8)(hp_adj + con_hp_adj[con]);

            /* The character's overall class, not the class this pass is on, so a
             * cleric/fighter collects the warrior's larger bonus once for the
             * fighter half and again for the cleric half. The original reads it
             * this way. */
            if (player->cls == CLASS_FIGHTER || player->cls == CLASS_PALADIN ||
                player->cls == CLASS_RANGER) {
                if (con == 17) {
                    hp_adj = (i8)(hp_adj + 1);
                } else if (con == 18) {
                    hp_adj = (i8)(hp_adj + 2);
                } else if (con == 19 || con == 20) {
                    hp_adj = (i8)(hp_adj + 3);
                } else if (con >= 21 && con <= 23) {
                    hp_adj = (i8)(hp_adj + 4);
                } else if (con == 24 || con == 25) {
                    hp_adj = (i8)(hp_adj + 5);
                }
            }

            /* A first-level ranger rolls two hit dice, so the bonus counts
             * twice - and because it doubles the running total rather than the
             * ranger's own share, anything a class earlier in the loop added
             * doubles with it. */
            if (cls == SKILL_RANGER && player->class_level[cls] == 1) {
                hp_adj = (i8)(hp_adj * 2);
            }
        }
    }

    return hp_adj;
}

/* ovr018.hp_calc_table. The C# declared it between sub_506BA and calc_max_hp;
 * both read it, so in C it has to come first.
 *
 *   dice       the hit die the class rolls
 *   lvl_bonus  an extra level's worth of dice - a ranger's second first-level die
 *   max_base   the total once the class is past its hit-dice ceiling
 *   max_mult   what each level past the ceiling adds to that */
typedef struct {
    int dice;
    int lvl_bonus;
    int max_base;
    int max_mult;
} HpCalc;

static const HpCalc hp_calc_table[SKILL_COUNT] = {
    {  8, 0, 0x48, 2 },     /* Cleric */
    {  8, 0, 0x70, 0 },     /* Druid */
    { 10, 0, 0x5a, 3 },     /* Fighter */
    { 10, 0, 0x5a, 3 },     /* Paladin */
    {  8, 1, 0x58, 2 },     /* Ranger */
    {  4, 0, 0x2c, 1 },     /* Magic User */
    {  6, 0, 0x3c, 2 },     /* Thief */
    {  4, 1, 0x48, 0 }      /* Monk */
};

/* ovr018.sub_506BA */
int partymenu_min_hit_points(const Player *player)
{
    int class_count = 0;
    int levels_total = 0;
    int con_adj;

    if (player == NULL) {
        log_warn("hit points: no character to work out a minimum for");
        return 1;
    }

    for (int cls = 0; cls <= SKILL_MONK; cls++) {
        if (player->class_level[cls] > 0) {
            levels_total += player->class_level[cls] +
                            hp_calc_table[cls].lvl_bonus;
            class_count++;
        }
    }

    if (class_count == 0) {
        log_warn("hit points: %s has no class levels", player->name);
        return 1;
    }

    con_adj = partymenu_con_hp_adj(player);

    if (con_adj < 0) {
        if (levels_total > (-con_adj + class_count)) {
            levels_total = (levels_total + con_adj) / class_count;
        } else {
            levels_total = 1;
        }
    } else {
        levels_total = (levels_total + con_adj) / class_count;
    }

    return levels_total;
}

/* ovr018.calc_max_hp, sub_50793 */
int partymenu_calc_max_hp(const Player *player)
{
    int class_count = 0;
    int max_hp = 0;

    if (player == NULL) {
        log_warn("hit points: no character to work out a maximum for");
        return 1;
    }

    for (int cls = 0; cls < SKILL_COUNT; cls++) {
        if (player->class_level[cls] > 0) {
            const HpCalc *hpt = &hp_calc_table[cls];

            /* Reads the selected character's constitution, not this one's - see
             * the note on partymenu_con_bonus. */
            int con = partymenu_con_bonus((ClassId)cls);

            if (player->class_level[cls] < limits_max_class_hit_dice[cls]) {
                class_count++;
                max_hp += (con + hpt->dice) *
                          (player->class_level[cls] + hpt->lvl_bonus);
            } else {
                int over_count = (player->class_level[cls] -
                                  limits_max_class_hit_dice[cls]) + 1;

                class_count++;

                /* Assignment, not +=: a class past its hit-dice ceiling throws
                 * away every other class's share of the total. The original has
                 * it this way. */
                max_hp = hpt->max_base + (over_count * hpt->max_mult);
            }
        }
    }

    if (class_count == 0) {
        log_warn("hit points: %s has no class levels", player->name);
        return 1;
    }

    return max_hp / class_count;
}

/* ovr018.unk_16B2A and unk_16B32, seg600:081A and seg600:0822. How many dice a
 * class's first level rolls and how big they are. */
static const u8 hp_dice_count[SKILL_COUNT] = { 1, 1, 1, 1, 2, 1, 1, 2 };
static const u8 hp_dice_size[SKILL_COUNT] = { 8, 8, 0xa, 0xa, 8, 4, 6, 4 };

/* ovr018.classMasks, seg600:3EAA unk_1A1BA */
const u8 partymenu_class_masks[SKILL_COUNT] = { 2, 2, 8, 0x10, 0x20, 1, 4, 4 };

/* ovr018.sub_509E0 */
u8 partymenu_roll_hit_points(u8 class_mask, const Player *player)
{
    u8 total = 0;

    if (player == NULL) {
        log_warn("hit points: no character to roll hit dice for");
        return 0;
    }

    for (int cls = 0; cls < SKILL_COUNT; cls++) {
        if (player->class_level[cls] > 0 &&
            (partymenu_class_masks[cls] & class_mask) != 0) {
            if (player->class_level[cls] < limits_max_class_hit_dice[cls]) {
                int dice_count = hp_dice_count[cls];
                u8 roll_a;
                u8 roll_b;

                /* Only the first level gets the extra die. */
                if (player->class_level[cls] > 1) {
                    dice_count = 1;
                }

                roll_a = effect_roll_dice(hp_dice_size[cls], dice_count);
                roll_b = effect_roll_dice(hp_dice_size[cls], dice_count);

                if (roll_b > roll_a) {
                    roll_a = roll_b;
                }

                total = (u8)(total + roll_a);
            } else {
                /* Assignment again, so a class at its ceiling wipes out what the
                 * classes before it rolled. The original has it this way, and a
                 * class with no line here - the druid and the monk - leaves the
                 * total alone. */
                if (cls == SKILL_FIGHTER || cls == SKILL_PALADIN) {
                    total = 3;
                } else if (cls == SKILL_RANGER || cls == SKILL_CLERIC ||
                           cls == SKILL_THIEF) {
                    total = 2;
                } else if (cls == SKILL_MAGIC_USER) {
                    total = 1;
                }
            }
        }
    }

    return total;
}

/* ovr018.exp_table, seg600:4293 unk_1A5A3 */
const i32 partymenu_exp_table[SKILL_COUNT][PARTYMENU_EXP_LEVELS] = {
    /* Cleric */  { 0, 1501, 3001,  6001, 13001, 27501, 55001, 110001, 225001,
                    450001, -1, -1, -1 },
    /* Druid */   { 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
    /* Fighter */ { 0, 2001, 4001,  8001, 18001, 35001, 70001, 125001, 250001,
                    500001, 750001, 1000001, -1 },
    /* Paladin */ { 0, 2751, 5501, 12001, 24001, 45001, 95001, 175001, 350001,
                    700001, 1050001, -1, -1 },
    /* Ranger */  { 0, 2251, 4501, 10001, 20001, 40001, 90001, 150001, 225001,
                    325001, 650001, -1, -1 },
    /* MU */      { 0, 2501, 5001, 10001, 22501, 40001, 60001,  90001, 135001,
                    250001, 375001, -1, -1 },
    /* Thief */   { 0, 1251, 2501,  5001, 10001, 20001, 42501,  70001, 110001,
                    160001, 220001, 440001, -1 },
    /* Monk */    { 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
};

/* exp_table[class][level] with the bounds the C# left to its array checks. A
 * level past the table reads as -1, the same answer as a class that stops there,
 * so a character somehow past level 12 simply cannot train.
 *
 * Nothing reaches it: column 12 is -1 for every class, so the caller never gets
 * far enough to ask for column 13. */
static i32 exp_for_level(int cls, int level)
{
    if (!skill_valid(cls)) {
        log_warn("training: no experience table for class %d", cls);
        return -1;
    }

    if (level < 0 || level >= PARTYMENU_EXP_LEVELS) {
        log_warn("training: no level %d in the experience table", level);
        return -1;
    }

    return partymenu_exp_table[cls][level];
}

void partymenu_train_player(void)
{
    Player *player = gbl.selected_player;
    u8 classes_to_train_mask = 0;
    u8 classes_exp_train_mask = 0;
    u8 trainer_class_mask;
    u8 actual_training_mask;
    /* 123 is Simeon's marker for a variable the original never initialised. */
    int class_lvl = 123;
    i32 next_exp_target = 0;
    bool skip_display;

    if (player == NULL) {
        log_warn("training: nobody selected to train");
        return;
    }

    if (player->health_status != STATUS_OKEY && cheats.free_training == false) {
        text_display_status(0, 14, "we only train conscious people");
        return;
    }

    /* Training is free once the game has been won, and free while a newly
     * created character is being brought up to level. */
    if (money_gold_worth(&player->money) < 1000 &&
        cheats.free_training == false && gbl.silent_training == false &&
        gbl.game_won == false) {
        text_display_status(0, 14, "Training costs 1000 gp.");
        return;
    }

    trainer_class_mask = gbl.area2_ptr->training_class_mask;

    for (int cls = 0; cls < SKILL_COUNT; cls++) {
        i32 this_exp;

        if (player->class_level[cls] == 0) {
            continue;
        }

        /* += rather than |=, and cleric shares bit 2 with druid while thief
         * shares bit 4 with monk, so a character in both halves of a pair
         * carries into the next bit. No class list offers such a pair, which is
         * why the original gets away with adding them. */
        classes_to_train_mask = (u8)(classes_to_train_mask +
                                     partymenu_class_masks[cls]);
        class_lvl = player->class_level[cls];

        if (limits_race_class_limit(class_lvl, player, (ClassId)cls)) {
            continue;
        }

        this_exp = exp_for_level(cls, class_lvl);

        if (this_exp > 0 &&
            (this_exp <= player->exp || cheats.free_training)) {
            i32 next_lvl_exp;

            /* free_training hands over the experience the level costs rather
             * than skipping the cost, so the character sheet still adds up. */
            if (cheats.free_training && this_exp > player->exp) {
                player->exp = this_exp;
            }

            classes_exp_train_mask = (u8)(classes_exp_train_mask +
                                          partymenu_class_masks[cls]);

            next_lvl_exp = exp_for_level(cls, class_lvl + 1);

            if (next_lvl_exp > 0 && player->exp >= next_lvl_exp &&
                next_lvl_exp > next_exp_target) {
                next_exp_target = next_lvl_exp - 1;
            }
        }
    }

    if (gbl.silent_training == false) {
        int max_class = 0;
        i32 max_exp = 0;

        /* A trainer only teaches one class a visit, and it is the dearest of the
         * ones that could go up. class_lvl here is whatever the last class with
         * any levels left it at rather than each class's own, so a multi-class
         * character is priced off the wrong row of the table; the original reads
         * it that way. */
        for (int cls = 0; cls < SKILL_COUNT; cls++) {
            if ((partymenu_class_masks[cls] & classes_exp_train_mask) != 0) {
                i32 this_exp = exp_for_level(cls, class_lvl);

                if (this_exp > max_exp) {
                    max_exp = this_exp;
                    max_class = cls;
                }
            }
        }

        if (max_exp > 0) {
            i32 next_lvl_exp;

            classes_exp_train_mask = partymenu_class_masks[max_class];

            next_lvl_exp = exp_for_level(max_class, class_lvl + 1);

            if (next_lvl_exp > 0 && player->exp >= next_lvl_exp &&
                next_lvl_exp > next_exp_target) {
                next_exp_target = next_lvl_exp - 1;
            }
        }
    }

    if (next_exp_target > 0 && gbl.silent_training == false) {
        /* The original held the character back to one point short of the level
         * after this one - player.exp = next_exp_target - and the line is
         * commented out in the source we are porting from, so all that work is
         * thrown away and a character can train twice on one visit's worth of
         * experience. Left as it is, since that is the game as it shipped. */
    }

    if ((classes_to_train_mask & trainer_class_mask) == 0 &&
        gbl.silent_training == false && cheats.free_training == false) {
        text_display_status(0, 14, "We don't train that class here");
        return;
    }

    if ((classes_exp_train_mask & trainer_class_mask) == 0) {
        /* This is how silent training knows to stop: no class has the
         * experience for another level. */
        if (gbl.silent_training) {
            gbl.can_train_no_more = true;
        }

        if (gbl.silent_training == false && cheats.free_training == false) {
            text_display_status(0, 14, "Not Enough Experience");
            return;
        }
    }

    if (cheats.free_training == false) {
        actual_training_mask = (u8)(classes_exp_train_mask &
                                    trainer_class_mask);
    } else {
        actual_training_mask = classes_exp_train_mask;
    }

    skip_display = gbl.silent_training;

    if (skip_display == false) {
        char text[64];
        int y_offset = 4;

        frames_clear_area(0x16, 0x26, 1, 1);

        character_display_name(false, y_offset, 4, player);
        text_display_string(" will become:", 0, 10, y_offset,
                            (int)strlen(player->name) + 4);

        for (int cls = 0; cls < SKILL_COUNT; cls++) {
            if (player->class_level[cls] > 0 &&
                (partymenu_class_masks[cls] & actual_training_mask) != 0) {
                y_offset++;

                snprintf(text, sizeof(text), "%s a level %d %s",
                         (y_offset == 5) ? "   " : "and",
                         player->class_level[cls] + 1, player_class_name(cls));

                text_display_string(text, 0, 10, y_offset, 6);
            }
        }
    }

    if (skip_display ||
        prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Do you wish to train? ") ==
            'Y') {
        u8 class_count = 0;
        u8 old_magic_user_lvl = player->class_level[SKILL_MAGIC_USER];
        u8 rolled;
        int max_hp_increase;
        int con_adj;
        int hp_lost;

        /* The ranger's old level was saved here too and never read; the test
         * below is against a flat level 9, not against a change. */

        if (skip_display == false) {
            character_print_message("Congratulations...");

            if (cheats.free_training == false && gbl.game_won == false) {
                money_subtract_gold_worth(&player->money, 1000);
            }
        }

        player->class_flags = 0;

        for (int cls = 0; cls < SKILL_COUNT; cls++) {
            if (player->class_level[cls] > 0) {
                class_count++;

                if ((partymenu_class_masks[cls] & actual_training_mask) != 0) {
                    player->class_level[cls]++;

                    /* A level drained away comes back with its share of the
                     * hit points it took. */
                    if (player->lost_lvls > 0) {
                        player->lost_hp =
                            (u8)(player->lost_hp -
                                 (player->lost_hp / player->lost_lvls));
                        player->lost_lvls--;
                    }
                }
            }
        }

        classcalc_class_bonuses(player);

        if (gbl.silent_training == false) {
            /* A magic-user who has gone up learns a spell, and so does a ranger
             * past level 8, who starts casting druid spells. */
            if (player->class_level[SKILL_MAGIC_USER] > old_magic_user_lvl ||
                player->class_level[SKILL_RANGER] > 8) {
                int index = -1;
                u8 new_spell_id;
                bool has_spells;

                do {
                    new_spell_id = viewplayer_spell_menu2(&has_spells, &index,
                                                          SPELL_SOURCE_LEARN,
                                                          SPELL_LOC_CHOOSE);
                } while (new_spell_id == 0 && has_spells &&
                         input_quit_requested() == false);

                if (new_spell_id > 0) {
                    player_learn_spell(player, (Spells)new_spell_id);
                }
            }
        }

        if (gbl.silent_training) {
            /* Nobody is there to pick, so a magic-user brought up from first
             * level is handed the spells the game wants them to have. */
            switch (player->class_level[SKILL_MAGIC_USER]) {
            case 2:
                player_learn_spell(player, SPELL_MAGIC_MISSILE);
                break;

            case 3:
                player_learn_spell(player, SPELL_STINKING_CLOUD);
                player_learn_spell(player, SPELL_PROTECT_FROM_EVIL_MU);
                break;

            case 4:
                player_learn_spell(player, SPELL_KNOCK);
                break;

            case 5:
                player_learn_spell(player, SPELL_FIREBALL);
                break;

            default:
                break;
            }
        }

        /* A dual-classed character rolls no hit dice until the new class has
         * passed the level the old one reached. */
        if (player->hit_dice <= player->multiclass_level) {
            return;
        }

        rolled = partymenu_roll_hit_points(actual_training_mask, player);

        if (class_count == 0) {
            log_warn("training: %s has no class to gain hit points in",
                     player->name);
            return;
        }

        max_hp_increase = rolled / class_count;

        if (max_hp_increase == 0) {
            max_hp_increase = 1;
        }

        player->hit_point_rolled = (u8)(player->hit_point_rolled +
                                        max_hp_increase);

        con_adj = partymenu_con_hp_adj(player);

        max_hp_increase = (rolled + con_adj) / class_count;

        if (max_hp_increase < 1) {
            max_hp_increase = 1;
        }

        /* The damage already taken is carried over, so training does not heal. */
        hp_lost = player->hit_point_max - player->hit_point_current;

        player->hit_point_max = (u8)(player->hit_point_max + max_hp_increase);
        player->hit_point_current = (u8)(player->hit_point_max - hp_lost);
    }
}

/* -------------------------------------------------------- engine/ovr017.cs */

/* ovr017.LoadPlayerCombatIcon, sub_47A90 */
void partymenu_load_player_combat_icon(bool recolour)
{
    Player *player = gbl.selected_player;
    char size_token;
    char file_name[16];

    if (player == NULL) {
        log_warn("icon: nobody selected to load an icon for");
        return;
    }

    if (player->icon_id >= GBL_COMBAT_ICON_COUNT) {
        log_warn("icon: %s has combat icon slot %d", player->name,
                 player->icon_id);
        return;
    }

    file_set_game_area(1);

    /* sizeToken: size 1 is the small set of sprites, size 2 the large one. Index
     * 0 of the original's table was a NUL, which would have made a file name
     * with a stray byte on the end; nothing rolls a character up at size 0. */
    if (player->icon_size == 1) {
        size_token = 'S';
    } else if (player->icon_size == 2) {
        size_token = 'T';
    } else {
        log_warn("icon: %s has icon size %d, using the large set", player->name,
                 player->icon_size);
        size_token = 'T';
    }

    /* Slot 11 is scratch space for the head while it is merged onto the body. */
    snprintf(file_name, sizeof(file_name), "CHEAD%c", size_token);
    icons_chead_cbody_comspr_icon(11, player->head_icon, file_name);

    snprintf(file_name, sizeof(file_name), "CBODY%c", size_token);
    icons_chead_cbody_comspr_icon(player->icon_id, player->weapon_icon,
                                  file_name);

    combat_icon_merge(&gbl.combat_icons[player->icon_id],
                      &gbl.combat_icons[11]);

    if (recolour) {
        u8 new_colors[16];
        u8 old_colors[16];

        for (int i = 0; i < 16; i++) {
            old_colors[i] = (u8)i;
            new_colors[i] = (u8)i;
        }

        /* Each of the six icon colours replaces one palette entry and the bright
         * version eight along from it. */
        for (int i = 0; i < GBL_ICON_COLOUR_COUNT; i++) {
            new_colors[GBL_DEFAULT_ICON_COLOURS[i]] =
                (u8)(player->icon_colours[i] & 0x0f);
            new_colors[GBL_DEFAULT_ICON_COLOURS[i] + 8] =
                (u8)((player->icon_colours[i] & 0xf0) >> 4);
        }

        combat_icon_recolor(&gbl.combat_icons[player->icon_id], false,
                            new_colors, old_colors);
    }

    icons_release_combat_icon(11);
    file_restore_game_area();
    input_clear_keyboard();
}

/* ovr017.AssignPlayerIconId, sub_4A60A */
void partymenu_assign_player_icon_id(Player *player)
{
    bool icon_slot[8] = { false, false, false, false, false, false, false,
                          false };

    if (player == NULL) {
        log_warn("icon: no character to give a combat icon slot to");
        return;
    }

    /* 0xff first, so the character being added is not counted as holding a slot
     * when the list is walked below. */
    player->icon_id = 0xff;

    gbl_team_add(player);
    gbl.selected_player = player;

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *tmp_player = gbl.team_list[i];

        if (tmp_player != NULL && tmp_player->icon_id < 8) {
            icon_slot[tmp_player->icon_id] = true;
        }
    }

    /* The lowest slot nobody else holds. */
    player->icon_id = 0;

    while (player->icon_id < 8 && icon_slot[player->icon_id]) {
        player->icon_id++;
    }

    gbl.area2_ptr->party_size++;

    if (player->control_morale >= CONTROL_NPC_BASE) {
        classcalc_class_bonuses(player);
    }
}

/* ovr017.SilentTrainPlayer */
void partymenu_silent_train_player(void)
{
    /* Every class, so whatever the character has can go up. */
    int guard = 0;

    gbl.area2_ptr->training_class_mask = 0xff;
    gbl.can_train_no_more = false;
    gbl.silent_training = true;

    /* The original trusted partymenu_train_player to run out of experience, and
     * it does: the experience table stops at level 12 for every class. The count
     * is here because there is no screen and no keyboard inside this loop, so a
     * table that did not stop would hang the game with nothing to show for
     * it. 200 is far past any level the game reaches. */
    do {
        partymenu_train_player();
        guard++;
    } while (gbl.can_train_no_more == false && guard < 200);

    if (guard >= 200) {
        log_warn("silent training: %s would not stop going up levels",
                 (gbl.selected_player != NULL) ? gbl.selected_player->name
                                               : "(nobody)");
    }

    gbl.silent_training = false;
}
