/* dungeon.c - Ported from engine/ovr015.cs. See dungeon.h. */

#include "dungeon.h"

#include <stdio.h>
#include <string.h>

#include "area.h"
#include "camp.h"
#include "character.h"
#include "cheats.h"
#include "dax.h"
#include "effect.h"
#include "enums.h"
#include "frames.h"
#include "gbl.h"
#include "geo.h"
#include "input.h"
#include "log.h"
#include "picture.h"
#include "player.h"
#include "prompt.h"
#include "resting.h"
#include "sound.h"
#include "spelllist.h"
#include "text.h"
#include "view3d.h"
#include "viewplayer.h"

/* ---------------------------------------------------------------- the doors */

/* ovr015.sub_43148 */
void dungeon_map_set_door_unlocked(int map_dir, int map_y, int map_x)
{
    MapInfo *mi;

    if (map_x < 0 || map_x > 15 ||
        map_y < 0 || map_y > 15) {
        return;
    }
    if (gbl.geo_ptr == NULL) {
        log_warn("dungeon: no map loaded, so no door to unlock");
        return;
    }

    mi = &gbl.geo_ptr->maps[map_y][map_x];

    /* Only the four compass directions have walls; a diagonal is not a mistake,
     * it just has no door in it. */
    switch (map_dir) {
    case 6:
        mi->x3_dir_6 = 1;
        break;

    case 4:
        mi->x3_dir_4 = 1;
        break;

    case 2:
        mi->x3_dir_2 = 1;
        break;

    case 0:
        mi->x3_dir_0 = 1;
        break;

    default:
        break;
    }
}

/* Both sides of the same door: the square the party is in, facing the way they
 * face, and the square beyond it facing back. A door is two entries in the map
 * and forcing it has to open both, or the party could walk through and find it
 * shut again behind them. ovr015 wrote this out twice, once in each of the two
 * routines below. */
static void unlock_door_both_sides(void)
{
    dungeon_map_set_door_unlocked(gbl.map_direction, gbl.map_pos_y,
                                  gbl.map_pos_x);

    dungeon_map_set_door_unlocked((gbl.map_direction + 4) % 8,
                                  GBL_MAP_DIR_Y_DELTA[gbl.map_direction] +
                                      gbl.map_pos_y,
                                  GBL_MAP_DIR_X_DELTA[gbl.map_direction] +
                                      gbl.map_pos_x);
}

/* ovr015.any_player_has_skill */
static bool any_player_has_skill(SkillType skill)
{
    for (int i = 0; i < gbl.team_count; i++) {
        if (gbl.team_list[i] != NULL &&
            player_skill_level(gbl.team_list[i], skill) > 0) {
            return true;
        }
    }

    return false;
}

/* ovr015.bash_door. Everybody on the team throws themselves at the door in turn
 * until one of them gets through it, and their chance is their strength.
 *
 * Nobody's health is looked at: a character at 0 hit points is as good a
 * battering ram as anyone. That is the original's, and it is not worth guessing
 * at what it meant to say instead.
 *
 * The two halves of this are the two kinds of door. A door with wall flags 3 is
 * the reinforced sort, and needs a strength of 18/91 before it will move at all;
 * anything else is an ordinary door, and almost anyone can eventually shoulder
 * one open. */
static bool bash_door(void)
{
    bool bash_worked = false;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];
        int str;
        int str_00;

        if (bash_worked == true) {
            break;
        }
        if (player == NULL) {
            continue;
        }

        str    = player->stats.value[PSTAT_STR].full;
        str_00 = player->stats.value[PSTAT_STR00].cur;

        /* Re-read for every character, though nothing in the loop can change it:
         * the door only opens after the loop. */
        if (view3d_wall_door_flags_get(gbl.map_direction, gbl.map_pos_y,
                                       gbl.map_pos_x) == 3) {
            if (str == 18) {
                if (str_00 >= 0x5b && str_00 <= 99) {
                    if (effect_roll_dice(6, 1) == 1) {
                        bash_worked = true;
                    }
                } else if (str_00 == 100) {
                    if (effect_roll_dice(6, 1) <= 2) {
                        bash_worked = true;
                    }
                } else {
                    /* 18/01 to 18/90 is not enough to shift this door, and the
                     * party is told so by the Bash word going away rather than
                     * by anything printed. */
                    gbl.can_bash_door = false;
                }
            } else if (str == 19 || str == 20) {
                if (effect_roll_dice(6, 1) <= 3) {
                    bash_worked = true;
                }
            } else if (str == 21 || str == 22) {
                if (effect_roll_dice(6, 1) <= 4) {
                    bash_worked = true;
                }
            } else if (str == 23) {
                if (effect_roll_dice(6, 1) <= 5) {
                    bash_worked = true;
                }
            } else if (str == 24) {
                if (effect_roll_dice(8, 1) <= 7) {
                    bash_worked = true;
                }
            } else if (str == 25) {
                bash_worked = true;
            } else {
                gbl.can_bash_door = false;
            }
        } else {
            if (str >= 3 && str <= 7) {
                if (effect_roll_dice(6, 1) == 1) {
                    bash_worked = true;
                }
            } else if (str >= 8 && str <= 15) {
                if (effect_roll_dice(6, 1) <= 2) {
                    bash_worked = true;
                }
            } else if (str == 15 || str == 17) {
                /* The original's, kept: 15 was already answered above, so this
                 * branch is only ever reached by 17. Strength 16 matches nothing
                 * in the whole chain and falls out of the bottom of it - no roll,
                 * and can_bash_door left alone - so a strength-16 character may
                 * keep shouldering an ordinary door for ever without ever
                 * opening it. The 15 was surely meant to be a 16. */
                if (effect_roll_dice(6, 1) <= 3) {
                    bash_worked = true;
                }
            } else if (str == 18) {
                if (str_00 >= 0 && str_00 <= 50) {
                    /* The original's, kept: bash_worked is set before the roll
                     * and the roll can only set it again, so 18/01 to 18/50
                     * always opens an ordinary door - which makes it better at
                     * this than 18/51 and up. Presumably the outer assignment
                     * was meant to be a `false`. */
                    bash_worked = true;

                    if (effect_roll_dice(6, 1) <= 3) {
                        bash_worked = true;
                    }
                } else if (str_00 >= 51 && str_00 <= 99) {
                    if (effect_roll_dice(6, 1) <= 4) {
                        bash_worked = true;
                    }
                } else if (str_00 == 100) {
                    if (effect_roll_dice(6, 1) <= 5) {
                        bash_worked = true;
                    }
                }
            } else if (str == 19 || str == 20) {
                if (effect_roll_dice(8, 1) <= 7) {
                    bash_worked = true;
                }
            } else if (str == 21) {
                if (effect_roll_dice(10, 1) <= 9) {
                    bash_worked = true;
                }
            } else if (str == 22 || str == 23) {
                if (effect_roll_dice(12, 1) <= 11) {
                    bash_worked = true;
                }
            } else if (str == 24) {
                if (effect_roll_dice(20, 1) <= 19) {
                    bash_worked = true;
                }
            } else if (str == 25) {
                bash_worked = true;
            }
            /* No `else` here, unlike the reinforced door above: an ordinary door
             * never uses up the party's one bash, so they may keep trying. */
        }
    }

    if (bash_worked == true) {
        unlock_door_both_sides();
    }

    return bash_worked;
}

/* ovr015.sub_435B6 */
bool dungeon_pick_lock(void)
{
    bool door_picked = false;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (door_picked) {
            break;
        }
        if (player == NULL) {
            continue;
        }

        /* thief_skills[1] is Open Locks. The roll comes first and so happens
         * even for a character who is in no state to pick anything - it is only
         * the result that is thrown away. */
        if (effect_roll_dice(100, 1) <= player->thief_skills[1] &&
            player->health_status == STATUS_OKEY) {
            door_picked = true;
        }
    }

    /* One attempt per square for the whole party, win or lose. */
    gbl.can_pick_door = false;

    if (door_picked == true) {
        unlock_door_both_sides();
    }

    return door_picked;
}

/* ovr015.TeamMemberHasSpell */
static bool team_member_has_spell(Spells spell_id)
{
    for (int i = 0; i < gbl.team_count; i++) {
        if (gbl.team_list[i] != NULL &&
            spell_list_has_spell(&gbl.team_list[i]->spell_list,
                                 (int)spell_id)) {
            return true;
        }
    }

    return false;
}

/* ovr015.RemoveKnockSpell. Spends the first knock spell anyone has memorised,
 * which is the whole of casting it: no roll, no target, and no check that the
 * caster is in any state to speak. */
static bool remove_knock_spell(void)
{
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player != NULL &&
            spell_list_has_spell(&player->spell_list, (int)SPELL_KNOCK)) {
            spell_list_clear_spell(&player->spell_list, (int)SPELL_KNOCK);
            return true;
        }
    }

    return false;
}

/* ------------------------------------------------------------ walking about */

/* ovr015.TryStepForward, sub_43765. Despite the name this moves nobody: it looks
 * at the square ahead and, when the step would leave the 16x16 map, holds the
 * party against the edge and sets tried_to_exit_map for the script to see. The
 * step itself is locked_door's, once it knows the way through is open. */
static void try_step_forward(void)
{
    int map_x = gbl.map_pos_x;
    int map_y = gbl.map_pos_y;
    int map_dir = gbl.map_direction;

    gbl.area2_ptr->tried_to_exit_map = false;

    if (view3d_wall_door_flags_get(map_dir, map_y, map_x) != 0) {
        map_x += GBL_MAP_DIR_X_DELTA[map_dir];
        map_y += GBL_MAP_DIR_Y_DELTA[map_dir];

        /* Each of these writes back the edge the party is already standing on,
         * so nothing actually moves; the flag is the point. */
        if (map_x > 0x0f) {
            gbl.map_pos_x = 0x0f;
            gbl.area2_ptr->tried_to_exit_map = true;
        }

        if (map_x < 0) {
            gbl.map_pos_x = 0;
            gbl.area2_ptr->tried_to_exit_map = true;
        }

        if (map_y > 0x0f) {
            gbl.map_pos_y = 0x0f;
            gbl.area2_ptr->tried_to_exit_map = true;
        }

        if (map_y < 0) {
            gbl.map_pos_y = 0;
            gbl.area2_ptr->tried_to_exit_map = true;
        }
    }
}

/* ovr015.sub_43813 */
void dungeon_move_party_forward(void)
{
    sound_play(SOUND_A);
    input_sys_delay(50);

    gbl.map_pos_x += GBL_MAP_DIR_X_DELTA[gbl.map_direction];
    gbl.map_pos_y += GBL_MAP_DIR_Y_DELTA[gbl.map_direction];

    /* The map wraps: walking off the east edge comes back on the west. The
     * scripts of the maps that are not meant to wrap catch it themselves, off
     * try_step_forward's tried_to_exit_map. */
    gbl.map_pos_x &= 0x0f;
    gbl.map_pos_y &= 0x0f;

    gbl.map_wall_type = view3d_map_wall_type(gbl.map_direction, gbl.map_pos_y,
                                             gbl.map_pos_x);

    /* A new square, so a fresh try at whatever door is in it. */
    gbl.can_bash_door  = true;
    gbl.can_pick_door  = true;
    gbl.can_knock_door = true;

    gbl.map_wall_roof = view3d_get_wall_x2(gbl.map_pos_y, gbl.map_pos_x);

    /* Ten minutes a step while searching against one while walking - slot 2 is
     * the tens of minutes. Searching a corridor is most of the cost of a
     * dungeon. */
    if ((gbl.area2_ptr->search_flags & 1) > 0) {
        resting_step_game_time(2, 1);
    } else {
        resting_step_game_time(1, 1);
    }
}

/* --------------------------------------------------------------- the prompt */

/* ovr015.sub_438DF */
char dungeon_main_3d_world_menu(void)
{
    char input_key = '\0';

    /* The interpreter reads field_592 to tell a door that has already been dealt
     * with from one that has not; a fresh turn starts with neither. */
    gbl.area2_ptr->field_592 = 0;

    if (gbl.game_state == GAME_STATE_DUNGEON_MAP) {
        bool stop_loop = false;

        do {
            bool special_key;

            input_key = prompt_display_input(&special_key, false, 1,
                                             GBL_DEFAULT_MENU_COLORS,
                                             "Area Cast View Encamp Search Look",
                                             "");

            if (special_key == false) {
                switch (input_key) {
                case 'A':
                    /* Some maps are too big or too confusing to be mapped, and
                     * say so with block_area_view. */
                    if (gbl.area_ptr->block_area_view == 0 ||
                        cheats.always_show_areamap) {
                        gbl.map_area_display = !gbl.map_area_display;

                        view3d_draw_world(gbl.map_direction, gbl.map_pos_y,
                                          gbl.map_pos_x);
                    } else {
                        text_display_status(0, 14, "Not Here");
                    }
                    break;

                case 'C':
                    if (gbl.selected_player != NULL &&
                        gbl.selected_player->health_status == STATUS_OKEY) {
                        gbl.menu_selected_word = 1;
                        camp_cast_spell();
                    }
                    break;

                case 'V':
                    gbl.menu_selected_word = 1;
                    viewplayer_view_player();
                    break;

                case 'E':
                    /* Encamping ends the turn; the interpreter sees the 'E' and
                     * runs the map's pre-camp script before camp.c gets it. */
                    stop_loop = true;
                    gbl.menu_selected_word = 1;
                    break;

                case 'S':
                    /* Search is a toggle, and the only thing here that neither
                     * ends the turn nor redraws anything: the party is told
                     * whether they are searching by the clock running twice as
                     * fast. */
                    gbl.area2_ptr->search_flags ^= 1;
                    break;

                case 'L':
                    /* Looking costs ten minutes and runs the map's search script
                     * on the square the party is standing in. Bit 2 is what tells
                     * that script it is a deliberate look rather than the
                     * once-per-step search. */
                    gbl.area2_ptr->search_flags |= 2;
                    resting_step_game_time(2, 1);
                    gbl.ecl_offset = gbl.search_location_addr;
                    stop_loop = true;
                    break;

                default:
                    break;
                }
            } else {
                switch (input_key) {
                case 'H':       /* up: forward */
                    try_step_forward();
                    stop_loop = true;
                    break;

                case 'P':       /* down: turn about */
                    gbl.map_direction = (u8)((gbl.map_direction + 4) % 8);

                    gbl.map_wall_type =
                        view3d_map_wall_type(gbl.map_direction, gbl.map_pos_y,
                                             gbl.map_pos_x);
                    view3d_draw_world(gbl.map_direction, gbl.map_pos_y,
                                      gbl.map_pos_x);
                    break;

                case 'K':       /* left */
                    gbl.map_direction = (u8)((gbl.map_direction + 6) % 8);

                    sound_play(SOUND_A);
                    gbl.map_wall_type =
                        view3d_map_wall_type(gbl.map_direction, gbl.map_pos_y,
                                             gbl.map_pos_x);
                    view3d_draw_world(gbl.map_direction, gbl.map_pos_y,
                                      gbl.map_pos_x);
                    break;

                case 'M':       /* right */
                    gbl.map_direction = (u8)((gbl.map_direction + 2) % 8);

                    sound_play(SOUND_A);

                    gbl.map_wall_type =
                        view3d_map_wall_type(gbl.map_direction, gbl.map_pos_y,
                                             gbl.map_pos_x);
                    view3d_draw_world(gbl.map_direction, gbl.map_pos_y,
                                      gbl.map_pos_x);
                    break;

                default:
                    /* Turning about is 'P' and so is Prev, which is why the two
                     * switches are separate: everything else with a scan code -
                     * Home, End, PgUp, PgDn - walks the selected character along
                     * the team list. */
                    viewplayer_scroll_team_list(input_key);
                    character_party_summary(gbl.selected_player);
                    break;
                }
            }

            character_display_map_position_time();

        } while (stop_loop == false);
    }

    if (gbl.bottom_text_has_been_cleared == false) {
        frames_clear_region(TEXT_REGION_NORMAL_BOTTOM);

        gbl.bottom_text_has_been_cleared = true;
    }

    return input_key;
}

/* ------------------------------------------------------------ the door menu */

/* Bash, Pick and Knock, in that order, each offered only while the party still
 * has that attempt and someone who can make it. An empty prompt means there is
 * nothing they can do about this door and it is not even mentioned. */
static void build_door_prompt(char *prompt, size_t prompt_size)
{
    prompt[0] = '\0';

    if (gbl.can_bash_door == true) {
        snprintf(prompt, prompt_size, "Bash");
    }

    if (gbl.can_pick_door == true &&
        any_player_has_skill(SKILL_THIEF)) {
        /* With no Bash to follow this leaves the leading space the original
         * left, which shifts the prompt one column right. */
        strncat(prompt, " Pick", prompt_size - strlen(prompt) - 1);
    }

    if (gbl.can_knock_door == true &&
        team_member_has_spell(SPELL_KNOCK)) {
        strncat(prompt, " Knock", prompt_size - strlen(prompt) - 1);
    }

    if (prompt[0] != '\0') {
        strncat(prompt, " Exit", prompt_size - strlen(prompt) - 1);
    }
}

/* ovr015.locked_door */
void dungeon_locked_door(void)
{
    bool door_open = false;

    if (gbl.game_state == GAME_STATE_DUNGEON_MAP) {
        if (gbl.area2_ptr->field_592 < 0xff) {
            char prompt[64];
            u8 flags;

            gbl.can_draw_bigpic = true;

            flags = view3d_wall_door_flags_get(gbl.map_direction,
                                               gbl.map_pos_y, gbl.map_pos_x);

            if (flags == 1) {
                /* Not a door at all, or one that has already been opened. */
                door_open = true;
            } else if (flags == 2) {
                build_door_prompt(prompt, sizeof(prompt));

                if (prompt[0] != '\0') {
                    char input = prompt_display_input_simple(
                        false, 0, GBL_DEFAULT_MENU_COLORS, prompt, "Locked. ");

                    switch (input) {
                    case 'B':
                        door_open = bash_door();
                        break;

                    case 'P':
                        door_open = dungeon_pick_lock();
                        break;

                    case 'K':
                        door_open = remove_knock_spell();
                        break;

                    default:
                        break;
                    }
                }
            } else if (flags == 3) {
                /* The same door, reinforced: everything is offered as before,
                 * including Pick to a thief, but the lock does not answer picks -
                 * trying spends the attempt and nothing else. */
                build_door_prompt(prompt, sizeof(prompt));

                if (prompt[0] != '\0') {
                    char input = prompt_display_input_simple(
                        false, 0, GBL_DEFAULT_MENU_COLORS, prompt, "Locked. ");

                    switch (input) {
                    case 'B':
                        door_open = bash_door();
                        break;

                    case 'P':
                        gbl.can_pick_door = false;
                        break;

                    case 'K':
                        door_open = remove_knock_spell();
                        break;

                    default:
                        break;
                    }
                }
            }

            if (door_open == true) {
                dungeon_move_party_forward();
            }

            character_display_map_position_time();
        } else {
            /* 0xff is the script saying it has dealt with this square itself. */
            gbl.area2_ptr->field_592 = 0;
        }
    }

    /* The animated picture and the talking head belong to whatever the party just
     * walked away from, and this is the end of the turn that walked them away. */
    picture_dax_array_free_blocks(&gbl.pic_frames);

    /* The C# dropped the two references and left them to its collector. */
    dax_block_free(gbl.head_dax);
    gbl.head_dax = NULL;
    dax_block_free(gbl.body_dax);
    gbl.body_dax = NULL;
    gbl.current_head_id = 0xff;
    gbl.current_body_id = 0xff;
}
