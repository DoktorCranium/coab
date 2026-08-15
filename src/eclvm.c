/* eclvm.c - Ported from engine/ovr003.cs. See eclvm.h. */

#include "eclvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "area.h"
#include "camp.h"
#include "character.h"
#include "cheats.h"
#include "combatloop.h"
#include "aftercombat.h"
#include "dax.h"
#include "dungeon.h"
#include "ecl.h"
#include "effect.h"
#include "endgame.h"
#include "enums.h"
#include "frames.h"
#include "gbl.h"
#include "icons.h"
#include "input.h"
#include "item.h"
#include "log.h"
#include "menu.h"
#include "money.h"
#include "partymenu.h"
#include "picture.h"
#include "player.h"
#include "prompt.h"
#include "protect.h"
#include "quit.h"
#include "resting.h"
#include "rnd.h"
#include "roster.h"
#include "shop.h"
#include "savegame.h"
#include "sound.h"
#include "spelllist.h"
#include "temple.h"
#include "text.h"
#include "treasure.h"
#include "view3d.h"
#include "vm.h"

/* ---------------------------------------------------------------- helpers */

/* gbl.SelectedPlayer, which a dozen instructions read without checking. The C#
 * would have thrown; here the caller gets NULL and has already been told why. */
static Player *selected(const char *who)
{
    if (gbl.selected_player == NULL) {
        log_warn("ecl vm: %s ran with nobody selected", who);
    }

    return gbl.selected_player;
}

/* gbl.TeamList[index]. The instructions that pick a character by a die roll can
 * roll one past the end of a party the roll was not sized for - roll_dice(0, 1)
 * answers 0, and 0 - 1 is where CMD_Damage would have indexed. */
static Player *team_at(int index)
{
    if (index < 0 || index >= gbl.team_count) {
        log_warn("ecl vm: no character %d of the %d on the team list",
                 index, gbl.team_count);
        return NULL;
    }

    return gbl.team_list[index];
}

static void clear_compare_flags(void)
{
    for (int i = 0; i < GBL_COMPARE_FLAGS; i++) {
        gbl.compare_flags[i] = false;
    }
}

static void skip_next_command(void);

/* ------------------------------------------------------------- the opcodes */

/* 0x00 EXIT. Ends the interpreter and puts the cursor back at the top of the
 * text area, so that whatever prints next starts from a known place. */
static void cmd_exit(void)
{
    ecl_vm_log("CMD_Exit: byte_1AB0A %d", (int)gbl.restore_player_ptr);

    if (gbl.restore_player_ptr == true) {
        gbl.selected_player    = gbl.last_selected_player;
        gbl.restore_player_ptr = false;
    }

    gbl.encounter_flags[0] = false;
    gbl.encounter_flags[1] = false;

    gbl.sprite_changed = false;
    gbl.stop_vm        = true;

    gbl.ecl_offset++;

    gbl_vm_call_stack_clear();

    gbl.text_y_col = 0x11;
    gbl.text_x_col = 1;
}

/* 0x01 GOTO. */
static void cmd_goto(void)
{
    u16 new_offset;

    vm_load_cmd_sets(1);
    new_offset = ecl_op_word(&gbl.cmd_opps[1]);

    ecl_vm_log("CMD_Goto: was: 0x%X now: 0x%X", gbl.ecl_offset, new_offset);

    gbl.ecl_offset = new_offset;
}

/* 0x02 GOSUB. */
static void cmd_gosub(void)
{
    u16 new_offset;

    vm_load_cmd_sets(1);
    new_offset = ecl_op_word(&gbl.cmd_opps[1]);

    ecl_vm_log("CMD_Gosub: was: 0x%X now: 0x%X", gbl.ecl_offset, new_offset);

    gbl_vm_call_push(gbl.ecl_offset);
    gbl.ecl_offset = new_offset;
}

/* 0x03 COMPARE, sub_2611D. Either operand being a string makes it a string
 * comparison; note that the strings are compared the other way round from the
 * operands, string 2 against string 1. */
static void cmd_compare(void)
{
    vm_load_cmd_sets(2);

    if (ecl_op_code(&gbl.cmd_opps[1]) >= 0x80 ||
        ecl_op_code(&gbl.cmd_opps[2]) >= 0x80) {
        ecl_vm_log("CMD_Compare: Strings '%s' '%s'",
                   gbl_ecl_string(2), gbl_ecl_string(1));

        vm_compare_strings(gbl_ecl_string(2), gbl_ecl_string(1));
    } else {
        u16 value_a = vm_get_cmd_value(1);
        u16 value_b = vm_get_cmd_value(2);

        ecl_vm_log("CMD_Compare: Values: %u %u",
                   (unsigned)value_b, (unsigned)value_a);
        vm_compare_variables(value_b, value_a);
    }
}

/* 0x04 ADD, 0x05 SUBTRACT, 0x06 DIVIDE, 0x07 MULTIPLY, sub_2619A. Subtract and
 * divide take their operands the opposite way round from each other, which is the
 * original's argument order and not a slip. */
static void cmd_add_sub_div_multi(void)
{
    static const char *const sym[8] = {
        "", "", "", "", "A + B", "B - A", "A / B", "A * B"
    };
    u16 value;
    u16 val_a;
    u16 val_b;
    u16 location;

    vm_load_cmd_sets(3);

    val_a = vm_get_cmd_value(1);
    val_b = vm_get_cmd_value(2);

    location = ecl_op_word(&gbl.cmd_opps[3]);

    switch (gbl.command) {
    case 4:
        value = (u16)(val_a + val_b);
        break;

    case 5:
        value = (u16)(val_b - val_a);
        break;

    case 6:
        /* The C# divided without looking and would have thrown on a zero
         * divisor; the DOS build would have taken a divide-by-zero interrupt.
         * Neither is worth reproducing, so a zero divisor answers zero. */
        if (val_b == 0) {
            log_warn("ecl vm: DIVIDE by zero at 0x%X", gbl.ecl_offset);
            value = 0;
            gbl.area2_ptr->field_67E = 0;
        } else {
            value = (u16)(val_a / val_b);
            gbl.area2_ptr->field_67E = (i16)(val_a % val_b);
        }
        break;

    case 7:
        value = (u16)(val_a * val_b);
        break;

    default:
        /* Unreachable: only those four opcodes reach this handler. */
        log_warn("ecl vm: opcode 0x%02X is not an arithmetic one",
                 (unsigned)gbl.command);
        value = 0;
        break;
    }

    ecl_vm_log("CMD_AdSubDivMulti: %s A: %u B: %u Loc: 0x%04X Res: %u",
               sym[gbl.command & 7], (unsigned)val_a, (unsigned)val_b,
               (unsigned)location, (unsigned)value);

    vm_set_memory_value(value, location);
}

/* 0x08 RANDOM, sub_2623D. The maximum is inclusive, hence the increment; 0xff
 * cannot be incremented and so is the one value that is exclusive. */
static void cmd_random(void)
{
    u8  rand_max;
    u16 loc;
    u8  val;

    vm_load_cmd_sets(2);

    rand_max = (u8)vm_get_cmd_value(1);

    if (rand_max < 0xff) {
        rand_max++;
    }

    loc = ecl_op_word(&gbl.cmd_opps[2]);

    val = (u8)rnd_int(rand_max);

    ecl_vm_log("CMD_Random: Max: %u Loc: 0x%04X Val: %u",
               (unsigned)rand_max, (unsigned)loc, (unsigned)val);

    vm_set_memory_value(val, loc);
}

/* 0x09 SAVE. Stores either a number or a string, depending on what the first
 * operand turned out to be. */
static void cmd_save(void)
{
    u16 loc;

    vm_load_cmd_sets(2);

    loc = ecl_op_word(&gbl.cmd_opps[2]);

    if (ecl_op_code(&gbl.cmd_opps[1]) < 0x80) {
        u16 val = vm_get_cmd_value(1);

        ecl_vm_log("CMD_Save: Value %u Loc: 0x%04X",
                   (unsigned)val, (unsigned)loc);
        vm_set_memory_value(val, loc);
    } else {
        ecl_vm_log("CMD_Save: String '%s' Loc: 0x%04X",
                   gbl_ecl_string(1), (unsigned)loc);
        vm_write_string_to_memory(gbl_ecl_string(1), loc);
    }
}

/* 0x0A LOAD CHARACTER, sub_262E9. Points the party address space at one member
 * of the team by position. Bit 7 of the operand asks for the party summary to be
 * redrawn as well, which is how a script that has been changing a character's
 * sheet gets the change on screen.
 *
 * Position 0 is never selectable: the test is `> 0`, so the first character a
 * script can name is the second on the list. */
static void cmd_load_character(void)
{
    int     player_index;
    bool    high_bit_set;
    Player *player;

    vm_load_cmd_sets(1);

    player_index = (u8)vm_get_cmd_value(1);
    ecl_vm_log("CMD_LoadCharacter: 0x%X", (unsigned)player_index);

    gbl.restore_player_ptr = true;

    high_bit_set = (player_index & 0x80) != 0;
    player_index = player_index & 0x7f;

    player = (player_index > 0 && player_index < gbl.team_count)
                 ? gbl.team_list[player_index]
                 : NULL;

    if (player != NULL) {
        gbl.selected_player   = player;
        gbl.player_not_found  = false;
    } else {
        gbl.player_not_found  = true;
    }

    if (high_bit_set == true &&
        gbl.redraw_party_summary1 == true &&
        gbl.redraw_party_summary2 == true) {
        if (gbl.last_selected_player == player) {
            gbl.restore_player_ptr = false;
        }
        gbl.selected_player =
            partymenu_free_current_player(gbl.selected_player, true, false);

        character_party_summary(gbl.selected_player);
        gbl.redraw_party_summary1 = false;
        gbl.redraw_party_summary2 = false;
    }
}

/* 0x0C SETUP MONSTER, sub_263C9. Says which art the encounter uses and how far
 * away it can be seen from, then draws it at whatever distance the corridor the
 * party is facing allows. */
static void cmd_setup_monster(void)
{
    u8 sprite_id;
    u8 max_distance;
    u8 pic_id;

    vm_load_cmd_sets(3);

    sprite_id    = (u8)vm_get_cmd_value(1);
    max_distance = (u8)vm_get_cmd_value(2);
    pic_id       = (u8)vm_get_cmd_value(3);

    ecl_vm_log("CMD_SetupMonster: sprite id: %u area2_ptr.field_580: %u "
               "pic id: %u", (unsigned)sprite_id, (unsigned)max_distance,
               (unsigned)pic_id);

    gbl.sprite_block_id = sprite_id;
    gbl.area2_ptr->max_encounter_distance = max_distance;
    gbl.pic_block_id = pic_id;

    gbl.area2_ptr->encounter_distance =
        vm_encounter_distance(gbl.map_direction, gbl.map_pos_y, gbl.map_pos_x);

    if (gbl.area2_ptr->max_encounter_distance <
        gbl.area2_ptr->encounter_distance) {
        gbl.area2_ptr->encounter_distance =
            gbl.area2_ptr->max_encounter_distance;
    }

    vm_show_encounter_art(gbl.encounter_flags,
                          gbl.area2_ptr->encounter_distance,
                          gbl.pic_block_id, gbl.sprite_block_id);
}

/* 0x0B LOAD MONSTER, sub_26465. Adds one kind of monster to the fight, up to 63
 * of them in all.
 *
 * The C# cloned the master copy shallowly for the first monster, so that one
 * shared the master's item and affect lists, and deep-copied the lists for the
 * rest. Here a Player holds its items and affects inline, so every copy is a copy
 * - which is what the later ones were doing on purpose, and the sharing was never
 * visible because the master copy is thrown away at the end of the instruction. */
static void cmd_load_monster(void)
{
    Player *current_player_bkup = gbl.selected_player;
    Player *master;
    Player *new_mob;
    int     mob_id;
    int     num_copies;
    int     block_id;
    int     copy_count;

    vm_load_cmd_sets(3);

    if (gbl.num_loaded_monsters >= 63) {
        return;
    }

    mob_id = vm_get_cmd_value(1) & 0xff;

    /* load_mob stops the game itself when the monster is not in the chapter
     * file; NULL is the roster pool being full, which the original could not
     * run into. */
    master = savegame_load_mob(mob_id);
    if (master == NULL) {
        return;
    }

    num_copies = vm_get_cmd_value(2) & 0xff;
    if (num_copies <= 0) {
        num_copies = 1;
    }

    block_id = vm_get_cmd_value(3) & 0xff;
    icons_chead_cbody_comspr_icon(gbl.monster_icon_id, block_id, "CPIC");

    copy_count = 0;
    while (copy_count < num_copies && gbl.num_loaded_monsters < 63) {
        new_mob = roster_clone(master);
        if (new_mob == NULL) {
            break;
        }

        new_mob->icon_id = gbl.monster_icon_id;

        if (gbl_team_add(new_mob) == false) {
            roster_release(new_mob);
            break;
        }

        copy_count++;
        gbl.num_loaded_monsters++;
    }

    gbl.monster_icon_id++;
    gbl.monsters_loaded = true;
    gbl.selected_player = current_player_bkup;
}

/* 0x0D APPROACH, sub_26835. One step closer, and redraw the encounter at the new
 * distance. */
static void cmd_approach(void)
{
    if (gbl.area2_ptr->encounter_distance > 0) {
        gbl.area2_ptr->encounter_distance--;

        vm_show_encounter_art(gbl.encounter_flags,
                              gbl.area2_ptr->encounter_distance,
                              gbl.pic_block_id, gbl.sprite_block_id);
    }
    gbl.ecl_offset++;
}

/* 0x0E PICTURE, sub_26873. Puts a picture in the view panel, or - block 0xff -
 * takes it away and puts the dungeon view back. Blocks from 0x78 up are full
 * screen pictures rather than panel ones, and a script that has set a portrait
 * head draws the picture as that portrait's body instead. */
static void cmd_picture(void)
{
    u8 block_id;

    vm_load_cmd_sets(1);
    block_id = (u8)vm_get_cmd_value(1);

    if (block_id != 0xff) {
        gbl.encounter_flags[1] = true;
        gbl.sprite_changed     = true;

        if (gbl.area2_ptr->head_block_id == 0xff) {
            gbl.byte_1EE8D = true;

            if (block_id >= 0x78) {
                picture_load_bigpic(block_id);
                picture_draw_bigpic();
                gbl.can_draw_bigpic = false;
            } else {
                picture_load_pic_final(&gbl.pic_frames, 0, block_id, "PIC");
                picture_draw_maybe_overlayed(gbl.pic_frames.frames[0].picture,
                                             true, 3, 3);
            }
        } else {
            vm_set_and_draw_head_body(block_id,
                                      (u8)gbl.area2_ptr->head_block_id);
        }
    } else {
        if ((gbl.last_game_state != GAME_STATE_DUNGEON_MAP ||
             gbl.game_state == GAME_STATE_DUNGEON_MAP) &&
            (gbl.sprite_changed == true || gbl.display_player_sprite)) {
            gbl.can_draw_bigpic = true;
            view3d_redraw();
            gbl.sprite_changed        = false;
            gbl.display_player_sprite = false;
            gbl.byte_1EE8D            = true;
        }
        gbl.encounter_flags[0] = false;
        gbl.encounter_flags[1] = false;
    }
}

/* 0x0F INPUT NUMBER, sub_2695E. */
static void cmd_input_number(void)
{
    u16 loc;
    u16 value;

    vm_load_cmd_sets(2);

    loc = ecl_op_word(&gbl.cmd_opps[2]);

    value = text_get_user_input_short(0, 0x0a, "");

    vm_set_memory_value(value, loc);
}

/* 0x10 INPUT STRING, sub_269A4. An empty answer is stored as one space, so that
 * the script always has something to compare against. */
static void cmd_input_string(void)
{
    char input[GBL_ECL_STRING_MAX];
    u16  loc;

    vm_load_cmd_sets(2);

    loc = ecl_op_word(&gbl.cmd_opps[2]);

    text_get_user_input_string(input, sizeof(input), 0x28, 0, 10, "");

    if (input[0] == '\0') {
        input[0] = ' ';
        input[1] = '\0';
    }

    vm_write_string_to_memory(input, loc);
}

/* 0x11 PRINT and 0x12 PRINTCLEAR. Despite the names it is PRINTCLEAR that clears
 * the text area and resets the cursor; PRINT carries on from wherever the last
 * one left off. */
static void cmd_print(void)
{
    vm_load_cmd_sets(1);

    gbl.bottom_text_has_been_cleared = false;
    gbl.delay_between_characters     = true;

    if (ecl_op_code(&gbl.cmd_opps[1]) < 0x80) {
        char number[16];

        snprintf(number, sizeof(number), "%u",
                 (unsigned)vm_get_cmd_value(1));
        gbl_ecl_string_set(1, number);
    }

    ecl_vm_log("CMD_Print: '%s'", gbl_ecl_string(1));

    if (gbl.command == 0x11) {
        text_press_any_key_region(gbl_ecl_string(1), false, 10,
                                  TEXT_REGION_NORMAL_BOTTOM);
    } else {
        gbl.text_y_col = 0x11;
        gbl.text_x_col = 1;

        text_press_any_key_region(gbl_ecl_string(1), true, 10,
                                  TEXT_REGION_NORMAL_BOTTOM);
    }

    gbl.delay_between_characters = false;
}

/* 0x13 RETURN. Returning with nothing on the call stack ends the script, which is
 * how a handler that was reached by a jump rather than a GOSUB finishes. */
static void cmd_return(void)
{
    u16 new_offset;

    gbl.ecl_offset++;

    if (gbl_vm_call_pop(&new_offset) == true) {
        ecl_vm_log("CMD_Return: was: 0x%X now: 0x%X",
                   gbl.ecl_offset, new_offset);
        gbl.ecl_offset = new_offset;
    } else {
        ecl_vm_log("CMD_Return: call stack empty");
        cmd_exit();
    }
}

/* 0x14 COMPARE AND, sub_26B0C. Two comparisons at once, and only the equal and
 * not-equal flags come out of it. */
static void cmd_compare_and(void)
{
    u16 var_8;
    u16 var_6;
    u16 var_4;
    u16 var_2;

    clear_compare_flags();

    vm_load_cmd_sets(4);

    var_8 = vm_get_cmd_value(1);
    var_6 = vm_get_cmd_value(2);
    var_4 = vm_get_cmd_value(3);
    var_2 = vm_get_cmd_value(4);

    if (var_8 == var_6 && var_4 == var_2) {
        gbl.compare_flags[0] = true;
    } else {
        gbl.compare_flags[1] = true;
    }
}

/* 0x16 IF = through 0x1B IF >=. Each reads the one comparison flag it is about
 * and steps over the following instruction when it is clear, so the script's
 * "then" branch is always exactly one instruction - usually a GOTO. */
static void cmd_if(void)
{
    static const char *const types[GBL_COMPARE_FLAGS] = {
        "==", "!=", "<", ">", "<=", ">="
    };
    int index;

    gbl.ecl_offset++;

    index = gbl.command - 0x16;
    if (index < 0 || index >= GBL_COMPARE_FLAGS) {
        log_warn("ecl vm: opcode 0x%02X is not a conditional jump",
                 (unsigned)gbl.command);
        return;
    }

    ecl_vm_log("CMD_if: %s %d", types[index], (int)gbl.compare_flags[index]);

    if (gbl.compare_flags[index] == false) {
        skip_next_command();
    }
}

/* 0x20 NEWECL. Replaces the running script with another one, which is how the
 * game moves between maps. The interpreter has to be stopped for it: the block it
 * was running no longer exists. */
static void cmd_new_ecl(void)
{
    u8 block_id;

    vm_load_cmd_sets(1);

    block_id = (u8)vm_get_cmd_value(1);

    ecl_vm_log("CMD_NewECL: block_id %u", (unsigned)block_id);

    gbl.area_ptr->last_ecl_block_id = gbl.ecl_block_id;
    gbl.ecl_block_id                = block_id;

    vm_load_ecl_dax(block_id);
    vm_init_ecl();
    gbl.stop_vm   = true;
    gbl.vm_flag01 = true;

    gbl.encounter_flags[0] = false;
    gbl.encounter_flags[1] = false;
}

/* 0x21 LOAD FILES and 0x37 LOAD PIECES, sub_26C41. The three operands are a
 * dungeon map or wall set, a second wall set, and a third; which of them mean
 * what depends on which of the two opcodes is running and on whether the area is
 * one of the ones with a split wall set. 0xff means "leave this one alone", and
 * 0x7f is the wilderness, which has no walls of its own. */
static void cmd_load_files(void)
{
    u8 var_3;
    u8 var_2;
    u8 var_1;

    vm_load_cmd_sets(3);

    gbl.byte_1AB0B = true;

    var_3 = (u8)vm_get_cmd_value(1);
    var_2 = (u8)vm_get_cmd_value(2);
    var_1 = (u8)vm_get_cmd_value(3);

    ecl_vm_log("CMD_LoadFile: %s A: %u B: %u C: %u",
               gbl.command == 0x21 ? "Files" : "Pieces",
               (unsigned)var_1, (unsigned)var_2, (unsigned)var_3);

    if (gbl.command == 0x21) {
        gbl.files_loaded = true;

        if (var_3 != 0xff && var_3 != 0x7f && gbl.area_ptr->in_dungeon != 0) {
            gbl.area_ptr->current_3d_map_block_id = var_3;
            view3d_load_3d_map(var_3);
            gbl.area2_ptr->field_592 = 0;
        }

        if (var_1 != 0xff && gbl.area_ptr->in_dungeon == 0 &&
            gbl.last_dax_block_id != 0x50) {
            picture_load_bigpic(0x79);
        }
    } else {
        gbl.byte_1AB0C = true;

        if (var_3 == 0x7f) {
            view3d_load_walldef(1, 0);
        } else if (gbl.area_ptr->field_1CE != 0 && gbl.area_ptr->field_1D0 != 0) {
            if (var_3 != 0xff) {
                view3d_load_walldef(1, var_3);
            }

            if (var_1 != 0xff) {
                view3d_load_walldef(3, var_1);
            }
        } else {
            /* SetBlock.Reset: -1 for both, i.e. "this set holds nothing". */
            if (var_3 != 0xff) {
                view3d_load_walldef(1, var_3);
            } else {
                gbl.set_blocks[0].set_id   = -1;
                gbl.set_blocks[0].block_id = -1;
            }

            if (var_2 != 0xff) {
                view3d_load_walldef(2, var_2);
            } else {
                gbl.set_blocks[1].set_id   = -1;
                gbl.set_blocks[1].block_id = -1;
            }

            if (var_1 != 0xff) {
                view3d_load_walldef(3, var_1);
            } else {
                gbl.set_blocks[2].set_id   = -1;
                gbl.set_blocks[2].block_id = -1;
            }
        }
    }

    if (gbl.byte_1AB0C == true && gbl.files_loaded == true &&
        gbl.last_game_state == GAME_STATE_WILDERNESS_MAP) {
        if (gbl.game_state != GAME_STATE_WILDERNESS_MAP &&
            gbl.byte_1EE98 == true) {
            frames_draw_03();
            character_party_summary(gbl.selected_player);
            character_display_map_position_time();
        }
        gbl.byte_1EE98 = false;
    }
}

/* 0x2F AND and 0x30 OR, sub_26DD0. Both also compare the result against zero, so
 * the script can test a bit with AND followed by IF <>. */
static void cmd_and_or(void)
{
    u8  resultant;
    u16 val_a;
    u16 val_b;
    u16 loc;
    const char *sym;

    vm_load_cmd_sets(3);
    val_a = vm_get_cmd_value(1);
    val_b = vm_get_cmd_value(2);

    loc = ecl_op_word(&gbl.cmd_opps[3]);

    if (gbl.command == 0x2f) {
        sym       = "And";
        resultant = (u8)(val_a & val_b);
    } else {
        sym       = "Or";
        resultant = (u8)(val_a | val_b);
    }

    ecl_vm_log("CMD_AndOr: %s A: %u B: %u Loc: 0x%04X Val: %u",
               sym, (unsigned)val_a, (unsigned)val_b, (unsigned)loc,
               (unsigned)resultant);

    vm_compare_variables(resultant, 0);
    vm_set_memory_value(resultant, loc);
}

/* 0x2A GETTABLE, sub_26E3F. Reads memory at a base address plus an index, which
 * is how a script indexes into a table it keeps in its own data. */
static void cmd_get_table(void)
{
    u16 base;
    u8  index;
    u16 result_loc;
    u16 value;

    vm_load_cmd_sets(3);

    base  = ecl_op_word(&gbl.cmd_opps[1]);
    index = (u8)vm_get_cmd_value(2);

    result_loc = ecl_op_word(&gbl.cmd_opps[3]);

    value = vm_get_memory_value((u16)(index + base));
    vm_set_memory_value(value, result_loc);
}

/* 0x35 SAVE TABLE, sub_26E9D. The other half of GETTABLE. */
static void cmd_save_table(void)
{
    u16 value;
    u16 result_loc;

    vm_load_cmd_sets(3);

    value = vm_get_cmd_value(1);

    result_loc  = ecl_op_word(&gbl.cmd_opps[2]);
    result_loc  = (u16)(result_loc + vm_get_cmd_value(3));

    vm_set_memory_value(value, result_loc);
}

/* 0x15 VERTICAL MENU, sub_26EE9. Prints a line of text and then a scrolling list
 * under it, and stores which entry was picked.
 *
 * Three operands say where the answer goes, what the line of text is, and how many
 * entries follow; the entries are then decoded as that many more operands from
 * where the first three stopped. The line of text has to be copied out before
 * that happens, because the second pass renumbers the strings from 1 again and
 * overwrites it. */
static void cmd_vert_menu(void)
{
    char     delay_text[GBL_ECL_STRING_MAX];
    MenuList menu_list;
    u16      mem_loc;
    int      menu_count;
    int      index;

    gbl.bottom_text_has_been_cleared = false;

    vm_load_cmd_sets(3);
    mem_loc = ecl_op_word(&gbl.cmd_opps[1]);

    snprintf(delay_text, sizeof(delay_text), "%s", gbl_ecl_string(1));

    menu_count = (u8)vm_get_cmd_value(3);
    gbl.ecl_offset--;
    vm_load_cmd_sets(menu_count);

    /* An instruction cannot carry more strings than gbl.ecl_strings holds, so a
     * count past that is a script the port cannot run as written; the extra
     * entries are dropped rather than filling the log with one complaint each. */
    if (menu_count >= GBL_ECL_STRINGS) {
        log_warn("ecl vm: a vertical menu of %d entries is more than the %d an "
                 "instruction can carry", menu_count, GBL_ECL_STRINGS - 1);
        menu_count = GBL_ECL_STRINGS - 1;
    }

    menu_list_clear(&menu_list);

    gbl.text_x_col = 1;
    gbl.text_y_col = 0x11;

    text_press_any_key(delay_text, true, 10, 22, 38, 17, 1);

    for (int i = 0; i < menu_count; i++) {
        menu_list_add(&menu_list, gbl_ecl_string(i + 1));
    }

    index = vm_vert_menu_select(0, true, false, &menu_list, 0x16, 0x26,
                                gbl.text_y_col + 1, 1);

    vm_set_memory_value((u16)index, mem_loc);

    menu_list_clear(&menu_list);
    frames_clear_region(TEXT_REGION_NORMAL_BOTTOM);
}

/* 0x2B HORIZONTAL MENU. The prompt line menu: the words are the instruction's
 * strings, each becomes its own initial's key, and the answer is the word's
 * position. Two operands say where the answer goes and how many words follow, and
 * as with VERTICAL MENU the words are that many more operands after them.
 *
 * A one word menu is not a choice but a "press a key" prompt, so it is drawn in
 * the flat colours and takes Return as well - and the one such prompt in the
 * shipped scripts has its DOS-era wording replaced. */
static void cmd_horizontal_menu(void)
{
    char         text[GBL_ECL_STRINGS * GBL_ECL_STRING_MAX];
    MenuColorSet colors;
    bool         use_overlay;
    bool         accept_return;
    u16          loc;
    int          string_count;
    size_t       at = 0;
    u8           menu_selected;

    vm_load_cmd_sets(2);

    loc          = ecl_op_word(&gbl.cmd_opps[1]);
    string_count = (u8)vm_get_cmd_value(2);

    gbl.ecl_offset--;

    vm_load_cmd_sets(string_count);

    if (string_count >= GBL_ECL_STRINGS) {
        log_warn("ecl vm: a menu of %d words is more than the %d an instruction "
                 "can carry", string_count, GBL_ECL_STRINGS - 1);
        string_count = GBL_ECL_STRINGS - 1;
    }
    if (string_count < 1) {
        log_warn("ecl vm: a menu of %d words has nothing to pick from",
                 string_count);
        vm_set_memory_value(0, loc);
        prompt_clear_area_no_update();
        return;
    }

    if (string_count == 1) {
        accept_return = true;
        colors.highlight  = 15;
        colors.foreground = 15;
        colors.prompt     = 13;

        if (strcmp(gbl_ecl_string(1),
                   "PRESS BUTTON OR RETURN TO CONTINUE.") == 0) {
            gbl_ecl_string_set(1, "PRESS <ENTER>/<RETURN> TO CONTINUE");
        }
    } else {
        accept_return = false;
        colors        = GBL_DEFAULT_MENU_COLORS;
    }

    if (gbl.sprite_changed == false || gbl.byte_1EE8D == false) {
        use_overlay = false;
    } else {
        use_overlay = true;
    }

    /* "~A ~B ~C": the '~' marks each word's initial as its key. */
    text[0] = '\0';
    for (int i = 1; i <= string_count; i++) {
        int written = snprintf(&text[at], sizeof(text) - at, "%s~%s",
                               i > 1 ? " " : "", gbl_ecl_string(i));

        if (written < 0 || (size_t)written >= sizeof(text) - at) {
            log_warn("ecl vm: the menu text does not fit in %zu bytes",
                     sizeof(text));
            break;
        }
        at += (size_t)written;
    }

    menu_selected = (u8)vm_menu_select(use_overlay, accept_return, colors,
                                      text, "");

    vm_set_memory_value(menu_selected, loc);

    prompt_clear_area_no_update();
}

/* 0x1C CLEARMONSTERS, sub_27240. Also drops the encounter's treasure, which is
 * why it is the instruction that starts an encounter rather than ends one. */
static void cmd_clear_monsters(void)
{
    gbl.ecl_offset++;
    gbl.num_loaded_monsters = 0;
    gbl.monsters_loaded     = false;
    gbl.monster_icon_id     = 8;

    ecl_vm_log("CMD_ClearMonsters:");

    money_clear_all(&gbl.pooled_money);
    gbl_ground_items_clear();
}

/* 0x1D PARTYSTRENGTH, sub_272A9. One number for how tough the party is, which the
 * scripts use to decide whether a wandering monster bothers them. Spell casting
 * counts for a lot; the armour class and hit bonus terms only start counting once
 * they are past the thresholds an unequipped character sits at.
 *
 * The total is accumulated in a byte and wraps, so a very strong party can come
 * out weak. That is what the original did and the scripts were balanced against
 * it. Note also that the magic-user level is read but the cleric level is the one
 * multiplied by four. */
static void cmd_party_strength(void)
{
    u8  power_value = 0;
    u16 loc;

    vm_load_cmd_sets(1);

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *player = gbl.team_list[i];
        int hit_points;
        int armor_class;
        int hit_bonus;
        int magic_power;
        int cleric_power;
        int total;

        if (player == NULL) {
            continue;
        }

        hit_points  = player->hit_point_current;
        armor_class = player->ac;
        hit_bonus   = player->hit_bonus;

        magic_power  = player_skill_level(player, SKILL_MAGIC_USER);
        cleric_power = player_skill_level(player, SKILL_CLERIC);

        if (armor_class > 60) {
            armor_class -= 60;
        } else {
            armor_class = 0;
        }

        if (hit_bonus > 39) {
            hit_bonus -= 39;
        } else {
            hit_bonus = 0;
        }

        total = ((cleric_power * 4) + hit_points + (armor_class * 5) +
                 (hit_bonus * 5) + (magic_power * 8)) / 10;

        power_value = (u8)(power_value + (u8)total);
    }

    loc = ecl_op_word(&gbl.cmd_opps[1]);
    vm_set_memory_value(power_value, loc);
}

/* sub_273F6. The four answers CHECKPARTY writes, in the order the instruction's
 * operands name them. */
static void set_memory_four(bool val_d, u8 val_c, u8 val_b, u8 val_a,
                            u16 loc_a, u16 loc_b, u16 loc_c, u16 loc_d)
{
    vm_set_memory_value(val_a, loc_a);
    vm_set_memory_value(val_b, loc_b);
    vm_set_memory_value(val_c, loc_c);
    vm_set_memory_value(val_d ? 1 : 0, loc_d);
}

/* 0x1E CHECKPARTY, sub_27454. Asks one question about the whole party and answers
 * with the lowest, highest and average across it. Which question is an address in
 * the party address space, biased down by 0x7fff: 8001 asks whether anybody has
 * an affect, 0xA5 to 0xAC are the eight thief skills, and 0x9f is initiative.
 * Anything else is a question the instruction does not know and it writes nothing
 * at all. */
static void cmd_check_party(void)
{
    int     var_4;
    u16     var_2;
    Affects affect_id;
    u16     loc_a;
    u16     loc_b;
    u16     loc_c;
    u16     loc_d;
    u8      val_a = 0xff;
    u8      val_b = 0;
    u8      val_c;

    vm_load_cmd_sets(6);

    if (ecl_op_code(&gbl.cmd_opps[1]) == 1) {
        var_2 = ecl_op_word(&gbl.cmd_opps[1]);
    } else {
        var_2 = vm_get_cmd_value(1);
    }

    affect_id = (Affects)vm_get_cmd_value(2);

    loc_a = ecl_op_word(&gbl.cmd_opps[3]);
    loc_b = ecl_op_word(&gbl.cmd_opps[4]);
    loc_c = ecl_op_word(&gbl.cmd_opps[5]);
    loc_d = ecl_op_word(&gbl.cmd_opps[6]);

    var_4 = 0;

    var_2 = (u16)(var_2 - 0x7fff);

    if (var_2 == 8001) {
        bool affect_found = false;

        for (int i = 0; i < gbl.team_count && affect_found == false; i++) {
            if (gbl.team_list[i] != NULL &&
                player_has_affect(gbl.team_list[i], affect_id)) {
                affect_found = true;
            }
        }

        set_memory_four(affect_found, 0, 0, 0, loc_a, loc_b, loc_c, loc_d);
    } else if (var_2 >= 0x00a5 && var_2 <= 0x00ac) {
        int index = var_2 - 0xa5;   /* the C#'s (var_2 - 0xA4) - 1 */
        int count = 0;

        for (int i = 0; i < gbl.team_count; i++) {
            const Player *player = gbl.team_list[i];

            if (player == NULL) {
                continue;
            }
            count++;

            if (player->thief_skills[index] < val_a) {
                val_a = player->thief_skills[index];
            }

            if (player->thief_skills[index] > val_b) {
                val_b = player->thief_skills[index];
            }

            var_4 += player->thief_skills[index];
        }

        /* The C# divided by the count without looking; an empty party would have
         * thrown. Nothing can be averaged over nobody, so the average is 0. */
        val_c = count > 0 ? (u8)(var_4 / count) : 0;

        set_memory_four(false, val_c, val_b, val_a, loc_a, loc_b, loc_c, loc_d);
    } else if (var_2 == 0x9f) {
        int count = 0;

        for (int i = 0; i < gbl.team_count; i++) {
            const Player *player = gbl.team_list[i];

            if (player == NULL) {
                continue;
            }
            count++;

            if (player->movement < val_a) {
                val_a = player->movement;
            }

            if (player->movement > val_b) {
                val_b = player->movement;
            }

            var_4 += player->movement;
        }

        val_c = count > 0 ? (u8)(var_4 / count) : 0;

        set_memory_four(false, val_c, val_b, val_a, loc_a, loc_b, loc_c, loc_d);
    }
}

/* 0x22 PARTY SURPRISE, sub_2767E. Whether the party has a ranger in it, which is
 * the only thing that makes them harder to surprise. The second answer is always
 * zero; the original wrote it anyway. */
static void cmd_party_surprise(void)
{
    u8  val_a = 0;
    u8  val_b = 0;
    u16 loc_a;
    u16 loc_b;

    vm_load_cmd_sets(2);

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *player = gbl.team_list[i];

        if (player != NULL &&
            (player->cls == CLASS_RANGER || player->cls == CLASS_MC_C_R)) {
            val_a = 1;
        }
    }

    loc_a = ecl_op_word(&gbl.cmd_opps[1]);
    loc_b = ecl_op_word(&gbl.cmd_opps[2]);

    vm_set_memory_value(val_a, loc_a);
    vm_set_memory_value(val_b, loc_b);
}

/* 0x23 SURPRISE, sub_2771E. Who caught whom: 0 nobody, 1 the party surprised the
 * monsters, 2 the monsters surprised the party, 3 both.
 *
 * The answer is not written where an operand says but always to 0x2cb, and the
 * last test overwrites the "both" case with 2 - the original's own bug, kept,
 * which means 3 is never actually stored. */
static void cmd_surprise(void)
{
    u8 val_a = 0;
    u8 var_8;
    u8 var_7;
    u8 var_6;
    u8 var_5;
    u8 var_9;
    u8 var_A;
    u8 var_1;
    u8 var_2;

    vm_load_cmd_sets(4);

    var_8 = (u8)vm_get_cmd_value(1);
    var_7 = (u8)vm_get_cmd_value(2);
    var_6 = (u8)vm_get_cmd_value(3);
    var_5 = (u8)vm_get_cmd_value(4);

    var_9 = (u8)((var_5 + 2) - var_8);
    var_A = (u8)((var_7 + 2) - var_6);

    var_1 = effect_roll_dice(6, 1);
    var_2 = effect_roll_dice(6, 1);

    if (var_1 <= var_9) {
        if (var_2 <= var_A) {
            val_a = 3;
        } else {
            val_a = 1;
        }
    }

    if (var_2 <= var_A) {
        val_a = 2;
    }

    vm_set_memory_value(val_a, 0x2cb);
}

/* 0x24 COMBAT, sub_277E4. Starts the fight the script has been setting up - or,
 * with no monsters loaded, goes straight to what a fight ends with, which is how
 * a script hands out treasure or opens a shop. */
static void cmd_combat(void)
{
    gbl.ecl_offset++;

    if (gbl.monsters_loaded == false &&
        gbl.combat_type == COMBAT_TYPE_NORMAL) {
        if (gbl.area2_ptr->enter_shop == 1) {
            gbl.area2_ptr->enter_shop = 0;

            shop_city_shop();
        } else if (gbl.area2_ptr->enter_temple == 1) {
            gbl.area2_ptr->enter_temple = 0;

            temple_shop();
        } else {
            aftercombat_exp_and_treasure();
        }
    } else {
        u16 distance = vm_encounter_distance(gbl.map_direction, gbl.map_pos_y,
                                            gbl.map_pos_x);

        if (distance < gbl.area2_ptr->encounter_distance) {
            gbl.area2_ptr->encounter_distance = distance;
        }

        combatloop_main_combat_loop();

        aftercombat_exp_and_treasure();

        if (gbl.area_ptr->in_dungeon == 0) {
            picture_load_bigpic(0x79);
        }
    }

    if (gbl.area_ptr->in_dungeon != 0) {
        gbl.game_state = GAME_STATE_DUNGEON_MAP;
    } else {
        gbl.game_state = GAME_STATE_WILDERNESS_MAP;
    }

    gbl.area2_ptr->search_flags &= 1;

    gbl.encounter_flags[0] = false;
    gbl.encounter_flags[1] = false;
    gbl.sprite_changed     = false;
    character_load_pic();
}

/* 0x25 ON GOTO and 0x26 ON GOSUB, sub_27AE5. A jump table: two fixed operands
 * give the index and how many addresses follow, the addresses are then decoded as
 * that many more operands, and an index past the end of the table falls through to
 * the next instruction. */
static void cmd_on_goto_gosub(void)
{
    u8 var_1;
    u8 var_2;

    vm_load_cmd_sets(2);
    var_1 = (u8)vm_get_cmd_value(1);
    var_2 = (u8)vm_get_cmd_value(2);
    gbl.ecl_offset--;
    vm_load_cmd_sets(var_2);

    if (var_1 < var_2) {
        u16 newloc;

        /* The instruction cannot hold more operands than gbl.cmd_opps has room
         * for; vm_load_cmd_sets has already refused to decode a longer one, so
         * there is nothing at this index to jump to. */
        if (var_1 + 1 >= ECL_CMD_OPS_LIMIT) {
            log_warn("ecl vm: jump table entry %u is past the %d an instruction "
                     "can carry", (unsigned)var_1, ECL_CMD_OPS_LIMIT - 1);
            return;
        }

        newloc = ecl_op_word(&gbl.cmd_opps[var_1 + 1]);

        ecl_vm_log("CMD_OnGotoGoSub: %s A: %u B: %u Was: 0x%X Now: 0x%X",
                   gbl.command == 0x25 ? "Goto" : "Gosub",
                   (unsigned)var_1, (unsigned)var_2, gbl.ecl_offset, newloc);

        if (gbl.command == 0x25) {
            gbl.ecl_offset = newloc;
        } else {
            gbl_vm_call_push(gbl.ecl_offset);
            gbl.ecl_offset = newloc;
        }
    } else {
        ecl_vm_log("CMD_OnGotoGoSub: %s A: %u B: %u",
                   gbl.command == 0x25 ? "Goto" : "Gosub",
                   (unsigned)var_1, (unsigned)var_2);
    }
}

/* 0x27 TREASURE, load_item. Seven operands of coin, then where the items come
 * from: a block of the chapter's item file for a fixed hoard, or - 0x80 plus a
 * count - that many items rolled at random. 0xff is coin only.
 *
 * The random table is transcribed as it stands, overlaps and all: the scroll and
 * potion ranges run into each other (0x56..0x5C then 0x5B..0x62), so two of the
 * potion range's values can never be reached, and one roll of 45 out of the first
 * range is a shield rather than the item type numbered 45. */
static void cmd_treasure(void)
{
    ItemType item_type = (ItemType)0;
    u8      *data;
    i16      data_size = 0;
    u8       block_id;

    vm_load_cmd_sets(8);

    for (int coin = 0; coin < MONEY_KINDS; coin++) {
        money_set(&gbl.pooled_money, (MoneyKind)coin,
                  (int)vm_get_cmd_value(coin + 1));
    }

    block_id = (u8)vm_get_cmd_value(8);

    if (block_id < 0x80) {
        char file_name[GBL_DAX_NAME_MAX];

        snprintf(file_name, sizeof(file_name), "ITEM%u.dax",
                 (unsigned)gbl.game_area);
        data = dax_load_decode(file_name, block_id, &data_size);

        if (data == NULL || data_size == 0) {
            free(data);
            game_log_and_exit("Unable to find item file: %s", file_name);
        }

        /* The C# walked to dataSize and would have read past the end of a block
         * whose length is not a whole number of records; a partial record at the
         * end is dropped instead. */
        for (int offset = 0; offset + ITEM_RECORD_SIZE <= (int)data_size;
             offset += ITEM_RECORD_SIZE) {
            Item item;

            if (item_read(&item, data, (size_t)data_size, (size_t)offset)) {
                gbl_ground_item_add(&item);
            }
        }

        free(data);
    } else if (block_id != 0xff) {
        for (int count = 0; count < (block_id - 0x80); count++) {
            Item item;
            int  var_63 = effect_roll_dice(100, 1);

            if (var_63 >= 1 && var_63 <= 60) {
                int var_64 = effect_roll_dice(100, 1);

                if ((var_64 >= 1 && var_64 <= 47) ||
                    (var_64 >= 50 && var_64 <= 59)) {
                    if (var_64 == 45) {
                        item_type = ITEM_SHIELD;
                    } else {
                        item_type = (ItemType)var_64;
                    }
                } else if (var_64 >= 60 && var_64 <= 90) {
                    var_64 = effect_roll_dice(10, 1);

                    if (var_64 >= 1 && var_64 <= 4) {
                        item_type = ITEM_LONG_SWORD;
                    } else if (var_64 >= 5 && var_64 <= 7) {
                        item_type = ITEM_BROAD_SWORD;
                    } else if (var_64 == 8) {
                        item_type = ITEM_BASTARD_SWORD;
                    } else if (var_64 == 9) {
                        item_type = ITEM_SHORT_SWORD;
                    } else if (var_64 == 10) {
                        item_type = ITEM_TWO_HANDED_SWORD;
                    }
                } else if (var_64 >= 91 && var_64 <= 94) {
                    item_type = ITEM_ARROW;
                } else if (var_64 >= 95 && var_64 <= 97) {
                    item_type = ITEM_RING_OF_PROT;
                } else if (var_64 >= 98 && var_64 <= 100) {
                    item_type = ITEM_BRACERS;
                } else {
                    item_type = ITEM_SHIELD;
                }
            } else if (var_63 >= 0x3d && var_63 <= 0x55) {
                item_type = ITEM_MU_SCROLL;
            } else if (var_63 >= 0x56 && var_63 <= 0x5c) {
                item_type = ITEM_CLRC_SCROLL;
            } else if (var_63 >= 0x5b && var_63 <= 0x62) {
                int var_62 = effect_roll_dice(15, 1);

                if (var_62 >= 1 && var_62 <= 9) {
                    item_type = ITEM_POTION;
                } else if (var_62 == 10) {
                    item_type = ITEM_TYPE_84;
                } else if (var_62 >= 11 && var_62 <= 15) {
                    item_type = ITEM_WAND_B;
                }
            } else if (var_63 == 99 || var_63 == 100) {
                item_type = ITEM_SHIELD;
            }

            treasure_create_item(&item, item_type);
            gbl_ground_item_add(&item);
        }

        for (int i = 0; i < gbl.ground_item_count; i++) {
            character_item_display_name_build(false, false, 0, 0,
                                              gbl_ground_item_at(i));
        }
    }
}

/* 0x28 ROB, sub_27F76. What a thief in a script gets away with: a percentage of
 * the money and a per-item chance at the pack. The first operand says whether it
 * is the whole party or only the selected character. */
static void cmd_rob(void)
{
    u8     all_party;
    u8     var_2;
    double percentage;
    int    rob_chance;

    vm_load_cmd_sets(3);
    all_party = (u8)vm_get_cmd_value(1);
    var_2     = (u8)vm_get_cmd_value(2);

    percentage = (100 - var_2) / 100.0;
    rob_chance = (u8)vm_get_cmd_value(3);

    if (all_party == 0) {
        Player *player = selected("ROB");

        vm_rob_money(player, percentage);
        vm_rob_items(player, rob_chance);
    } else {
        for (int i = 0; i < gbl.team_count; i++) {
            vm_rob_money(gbl.team_list[i], percentage);
            vm_rob_items(gbl.team_list[i], rob_chance);
        }
    }
}

/* 0x29 ENCOUNTER MENU. The Combat/Wait/Flee/Advance prompt, run in a loop until
 * the party does something that ends the encounter.
 *
 * Fourteen operands: the sprite, how far off it can be seen, the picture, where
 * the answer goes, five bytes saying how the monsters behave at each menu choice,
 * three strings for what is said at each of the three distances, and the
 * monsters' own morale and speed. Waiting and advancing do not end it - they
 * redraw at the new distance and ask again - and the loop is also how the "both
 * sides wait" message gets to be repeatable.
 *
 * Which of the five behaviour bytes applies is picked by the menu answer, so the
 * three strings have to be copied out before the answer is asked for: nothing
 * reloads the instruction, but the prompt is the last thing that could. */
static void cmd_encounter_menu(void)
{
    char        strings[3][GBL_ECL_STRING_MAX];
    /* Outside the loop, as the original had it: a distance the three cases do not
     * cover leaves the line that was printed last time in place. */
    const char *text = "";
    u8   var_6[5];
    u16  var_43D;
    u8   init_min;
    u8   var_40A;
    u8   var_407;
    u8   var_408;
    u8   init_max;

    gbl.byte_1EE95                   = true;
    gbl.bottom_text_has_been_cleared = false;
    gbl.delay_between_characters     = true;

    vm_calc_group_movement(&init_min, &var_40A);

    vm_load_cmd_sets(0x0e);

    gbl.sprite_block_id = (u8)vm_get_cmd_value(1);
    gbl.area2_ptr->max_encounter_distance = vm_get_cmd_value(2);
    gbl.pic_block_id = (u8)vm_get_cmd_value(3);

    var_43D = ecl_op_word(&gbl.cmd_opps[4]);

    for (int i = 0; i < 5; i++) {
        var_6[i] = (u8)vm_get_cmd_value(i + 5);
    }

    for (int i = 0; i < 3; i++) {
        snprintf(strings[i], sizeof(strings[i]), "%s", gbl_ecl_string(i + 1));
    }

    var_407 = (u8)vm_get_cmd_value(0x0d);
    var_408 = (u8)vm_get_cmd_value(0x0e);

    gbl.area2_ptr->encounter_distance =
        vm_encounter_distance(gbl.map_direction, gbl.map_pos_y, gbl.map_pos_x);

    if (gbl.area2_ptr->max_encounter_distance <
        gbl.area2_ptr->encounter_distance) {
        gbl.area2_ptr->encounter_distance =
            gbl.area2_ptr->max_encounter_distance;
    }

    vm_show_encounter_art(gbl.encounter_flags,
                          gbl.area2_ptr->encounter_distance,
                          gbl.pic_block_id, gbl.sprite_block_id);

    do {
        const char *display_text;
        bool        use_overlay;
        bool        clear_text_area;
        int         var_43B;
        int         menu_selected;

        if (gbl.sprite_changed == false || gbl.byte_1EE8D == false ||
            gbl.area_ptr->in_dungeon == 0 || gbl.last_dax_block_id == 0x50) {
            use_overlay = false;
        } else {
            use_overlay = true;
        }

        clear_text_area = (gbl.area_ptr->in_dungeon != 0);

        init_max       = 0;
        gbl.text_x_col = 1;
        gbl.text_y_col = 0x11;

        /* Each distance starts at its own string and walks forward looking for
         * one that is not empty, which is how a script gives two of the three
         * distances the same line without repeating it. */
        switch (gbl.area2_ptr->encounter_distance) {
        case 0:
            var_43B = 0;

            do {
                text = strings[var_43B];
                var_43B++;
            } while (text[0] == '\0' && var_43B < 3);
            break;

        case 1:
            var_43B = 1;

            do {
                text = strings[var_43B];
                var_43B++;

                if (var_43B > 2) {
                    var_43B = 0;
                }
            } while (text[0] == '\0' && var_43B != 1);
            break;

        case 2:
            var_43B = 2;

            do {
                text = strings[var_43B];

                var_43B++;
                if (var_43B > 2) {
                    var_43B = 0;
                }
            } while (text[0] == '\0' && var_43B != 2);
            break;

        default:
            break;
        }

        if (text[0] == '\0') {
            clear_text_area = false;
        }

        text_press_any_key_region(text, clear_text_area, 10,
                                  TEXT_REGION_NORMAL_BOTTOM);

        if (gbl.area2_ptr->encounter_distance == 0 ||
            gbl.area_ptr->in_dungeon == 0) {
            display_text = "~COMBAT ~WAIT ~FLEE ~PARLAY";
        } else {
            display_text = "~COMBAT ~WAIT ~FLEE ~ADVANCE";
        }

        menu_selected = vm_menu_select(use_overlay, false,
                                       GBL_DEFAULT_MENU_COLORS,
                                       display_text, "");

        /* Parlay is the fifth behaviour even though it is the fourth word. */
        if (gbl.area2_ptr->encounter_distance == 0 ||
            gbl.area_ptr->in_dungeon == 0) {
            if (menu_selected == 3) {
                menu_selected = 4;
            }
        }

        if (menu_selected < 0 ||
            menu_selected >= (int)COAB_ARRAY_LEN(var_6)) {
            /* A key that is in none of the four words. The C# indexed the
             * behaviour table with it and would have thrown; asking again is
             * what the player sees as the key having done nothing. */
            log_warn("ecl vm: the encounter menu was answered with %d, which is "
                     "none of its words", menu_selected);
            init_max = 1;
        } else {
            switch (var_6[menu_selected]) {
            case 0:
                if (menu_selected != 2) {
                    vm_set_memory_value(1, var_43D);
                } else {
                    if (init_min >= var_407) {
                        vm_set_memory_value(2, var_43D);
                    } else {
                        vm_set_memory_value(1, var_43D);
                    }
                }
                break;

            case 1:
                if (menu_selected == 0) {
                    vm_set_memory_value(1, var_43D);
                } else if (menu_selected == 1) {
                    init_max = 1;
                    text_press_any_key_region("Both sides wait.", true, 10,
                                              TEXT_REGION_NORMAL_BOTTOM);
                } else if (menu_selected == 2) {
                    vm_set_memory_value(2, var_43D);
                } else if (menu_selected == 3) {
                    if (gbl.area2_ptr->encounter_distance != 0) {
                        gbl.area2_ptr->encounter_distance--;

                        vm_show_encounter_art(gbl.encounter_flags,
                                              gbl.area2_ptr->encounter_distance,
                                              gbl.pic_block_id,
                                              gbl.sprite_block_id);
                    } else {
                        text_press_any_key_region("Both sides wait.", true, 10,
                                                  TEXT_REGION_NORMAL_BOTTOM);
                    }

                    init_max = 1;
                } else if (menu_selected == 4) {
                    if (gbl.area2_ptr->encounter_distance > 0) {
                        gbl.area2_ptr->encounter_distance--;
                        vm_show_encounter_art(gbl.encounter_flags,
                                              gbl.area2_ptr->encounter_distance,
                                              gbl.pic_block_id,
                                              gbl.sprite_block_id);
                        init_max = 1;
                    } else {
                        vm_set_memory_value(3, var_43D);
                    }
                }
                break;

            case 2:
                if (menu_selected == 0) {
                    if (var_408 > var_40A) {
                        vm_set_memory_value(0, var_43D);

                        gbl.text_x_col = 1;
                        gbl.text_y_col = 0x11;
                        text_press_any_key_region("The monsters flee.", true, 10,
                                                  TEXT_REGION_NORMAL_BOTTOM);
                    } else {
                        vm_set_memory_value(1, var_43D);
                    }
                } else if (menu_selected >= 1 && menu_selected <= 4) {
                    vm_set_memory_value(0, var_43D);

                    gbl.text_x_col = 1;
                    gbl.text_y_col = 0x11;
                    text_press_any_key_region("The monsters flee.", true, 10,
                                              TEXT_REGION_NORMAL_BOTTOM);
                }
                break;

            case 3:
                if (menu_selected == 0) {
                    vm_set_memory_value(1, var_43D);
                } else if (menu_selected == 1 || menu_selected == 3) {
                    if (gbl.area2_ptr->encounter_distance != 0) {
                        gbl.area2_ptr->encounter_distance--;

                        vm_show_encounter_art(gbl.encounter_flags,
                                              gbl.area2_ptr->encounter_distance,
                                              gbl.pic_block_id,
                                              gbl.sprite_block_id);
                    } else {
                        text_press_any_key_region("Both sides wait.", true, 10,
                                                  TEXT_REGION_NORMAL_BOTTOM);
                    }

                    init_max = 1;
                } else if (menu_selected == 2) {
                    vm_set_memory_value(2, var_43D);
                } else if (menu_selected == 4) {
                    if (gbl.area2_ptr->encounter_distance <= 0) {
                        vm_set_memory_value(3, var_43D);
                    } else {
                        gbl.area2_ptr->encounter_distance--;

                        vm_show_encounter_art(gbl.encounter_flags,
                                              gbl.area2_ptr->encounter_distance,
                                              gbl.pic_block_id,
                                              gbl.sprite_block_id);
                        init_max = 1;
                    }
                }
                break;

            case 4:
                if (menu_selected == 0) {
                    vm_set_memory_value(1, var_43D);
                } else if (menu_selected == 1 || menu_selected == 3 ||
                           menu_selected == 4) {
                    if (gbl.area2_ptr->encounter_distance <= 0) {
                        vm_set_memory_value(3, var_43D);
                    } else {
                        gbl.area2_ptr->encounter_distance -= 1;

                        vm_show_encounter_art(gbl.encounter_flags,
                                              gbl.area2_ptr->encounter_distance,
                                              gbl.pic_block_id,
                                              gbl.sprite_block_id);
                        init_max = 1;
                    }
                } else if (menu_selected == 2) {
                    vm_set_memory_value(2, var_43D);
                }
                break;

            default:
                break;
            }
        }
    } while (init_max != 0);

    prompt_clear_area();
    gbl.delay_between_characters = false;
    gbl.byte_1EE95               = false;
}

/* 0x2C PARLAY, talk_style. How the party talks to something: five operands say
 * what each of the five tones gets, and the answer goes wherever the sixth says. */
static void cmd_parlay(void)
{
    u8  values[5];
    int menu_selected;
    u16 location;
    u8  value;

    vm_load_cmd_sets(6);

    for (int i = 0; i < 5; i++) {
        values[i] = (u8)vm_get_cmd_value(i + 1);
    }

    menu_selected = vm_menu_select(false, false, GBL_DEFAULT_MENU_COLORS,
                                   "~HAUGHTY ~SLY ~NICE ~MEEK ~ABUSIVE", " ");

    location = ecl_op_word(&gbl.cmd_opps[6]);

    /* A key in none of the five words. The C# indexed with -1 and would have
     * thrown; the first tone is the one the menu starts on and so is what the
     * player was most likely reaching for. */
    if (menu_selected < 0 || menu_selected >= (int)COAB_ARRAY_LEN(values)) {
        log_warn("ecl vm: the parlay menu was answered with %d, which is none "
                 "of its words; taking the first", menu_selected);
        menu_selected = 0;
    }

    value = values[menu_selected];

    vm_set_memory_value(value, location);
}

/* 0x32 FIND ITEM, sub_28856. Whether anybody in the party is carrying one. */
static void cmd_find_item(void)
{
    ItemType item_type;

    vm_load_cmd_sets(1);

    item_type = (ItemType)vm_get_cmd_value(1);

    clear_compare_flags();

    gbl.compare_flags[1] = true;

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        for (int j = 0; j < player->item_count; j++) {
            if ((int)item_type == player->items[j].type) {
                gbl.compare_flags[0] = true;
                gbl.compare_flags[1] = false;
                return;
            }
        }
    }
}

/* 0x3A DELAY. */
static void cmd_delay(void)
{
    gbl.ecl_offset++;
    text_game_delay();
}

/* 0x2E DAMAGE, sub_28958. A script hurting the party: a trap, a falling ceiling,
 * a dragon's breath.
 *
 * The first operand is a bit field. 0x80 says roll a saving throw, and then 0x40
 * means everybody rather than one character, 0x20 skips the throw, and 0x10 means
 * a successful throw still takes the full damage. Without 0x80 the operand is
 * instead a count of separate attacks, each of which has to hit. */
static void cmd_damage(void)
{
    Player *current_player_backup = gbl.selected_player;
    u8      var_1;
    int     dice_count;
    int     dice_size;
    int     dam_plus;
    u8      var_6;
    int     damage;
    u8      rnd_player_id = 0;

    vm_load_cmd_sets(5);
    var_1      = (u8)vm_get_cmd_value(1);
    dice_count = vm_get_cmd_value(2);
    dice_size  = vm_get_cmd_value(3);
    dam_plus   = vm_get_cmd_value(4);
    var_6      = (u8)vm_get_cmd_value(5);

    damage = effect_roll_dice(dice_size, dice_count) + dam_plus;

    if ((var_1 & 0x40) == 0) {
        rnd_player_id = effect_roll_dice(gbl.area2_ptr->party_size, 1);
    }

    if ((var_1 & 0x80) != 0) {
        int save_bonus = var_1 & 0x1f;
        int bonus_type = var_6 & 7;

        if ((var_1 & 0x40) != 0) {
            for (int i = 0; i < gbl.team_count; i++) {
                Player *player03 = gbl.team_list[i];

                if (player03 == NULL) {
                    continue;
                }

                if ((var_1 & 0x20) != 0) {
                    vm_damage_and_report(player03, damage);
                } else if (effect_roll_saving_throw(save_bonus,
                                                   (SaveVerseType)bonus_type,
                                                   player03) == false) {
                    vm_damage_and_report(player03, damage);
                } else if ((var_1 & 0x10) != 0) {
                    vm_damage_and_report(player03, damage);
                }
            }
        } else if ((var_6 & 0x80) != 0) {
            Player *player = selected("DAMAGE");

            /* Save type 0 means "no throw at all" here, so the bias by one. */
            if (bonus_type == 0 ||
                effect_roll_saving_throw(save_bonus,
                                         (SaveVerseType)(bonus_type - 1),
                                         player) == false) {
                vm_damage_and_report(player, damage);
            } else if ((var_1 & 0x10) != 0) {
                vm_damage_and_report(player, damage);
            }
        } else {
            Player *target = team_at(rnd_player_id - 1);

            if (effect_roll_saving_throw(save_bonus, (SaveVerseType)bonus_type,
                                         target) == false) {
                vm_damage_and_report(target, damage);
            } else if ((var_1 & 0x10) != 0) {
                vm_damage_and_report(target, damage);
            }
        }
    } else {
        for (int i = 0; i < var_1; i++) {
            Player *player03;

            rnd_player_id = effect_roll_dice(gbl.area2_ptr->party_size, 1);
            player03      = team_at(rnd_player_id - 1);

            if (effect_can_hit_target(var_6, player03) == true) {
                vm_damage_and_report(player03, damage);
            }

            damage = effect_roll_dice(dice_size, dice_count) + dam_plus;
        }
    }

    gbl.party_killed = true;

    for (int i = 0; i < gbl.team_count; i++) {
        if (gbl.team_list[i] != NULL && gbl.team_list[i]->in_combat == true) {
            gbl.party_killed = false;
        }
    }

    if (gbl.party_killed == true) {
        frames_draw_outer();
        gbl.text_x_col = 2;
        gbl.text_y_col = 2;

        text_press_any_key("The entire party is killed!", true, 10,
                           0x16, 0x26, 1, 1);
        input_sys_delay(3000);
    }

    gbl.selected_player = current_player_backup;
    text_display_and_pause("press <enter>/<return> to continue", 15);
}

/* 0x31 SPRITE OFF, sub_28CB6. Takes a character's own portrait out of the view
 * and puts the dungeon back. */
static void cmd_sprite_off(void)
{
    gbl.ecl_offset++;
    if (gbl.display_player_sprite) {
        gbl.can_draw_bigpic = true;
        view3d_redraw();
        gbl.display_player_sprite = false;
        gbl.sprite_changed        = false;
    }
}

/* 0x34 ECL CLOCK, sub_28CDA. Moves the game clock on. Note that the table says
 * this instruction carries one operand and the handler reads two: skipping one
 * with a conditional jump steps over a byte less than running it consumes. That
 * is the original's table and is left alone, because a script that both jumps
 * over an ECL CLOCK and expects to land on an instruction would already be
 * broken in the DOS build. */
static void cmd_ecl_clock(void)
{
    int time_step;
    int time_slot;

    vm_load_cmd_sets(2);
    time_step = vm_get_cmd_value(1) & 0xff;
    time_slot = vm_get_cmd_value(2) & 0xff;

    resting_step_game_time(time_slot, time_step);
}

/* 0x33 PRINT RETURN, sub_28D0F. A newline in the text area. */
static void cmd_print_return(void)
{
    gbl.ecl_offset++;

    ecl_vm_log("CMD_PrintReturn:");

    gbl.text_x_col = 1;
    gbl.text_y_col++;
}

/* 0x3D CLEAR BOX, sub_28D38. The whole screen back to the map layout. */
static void cmd_clear_box(void)
{
    gbl.ecl_offset++;

    ecl_vm_log("CMD_ClearBox:");

    frames_draw_03();
    character_party_summary(gbl.selected_player);
    character_display_map_position_time();

    picture_draw_maybe_overlayed(gbl.pic_frames.frames[0].picture, true, 3, 3);
    character_display_map_position_time();
    gbl.byte_1EE98 = false;
}

/* 0x39 WHO, sub_28D7F. Asks the player to pick a character; the answer becomes
 * the selection, and so what the party address space is a window onto. */
static void cmd_who(void)
{
    char prompt[GBL_ECL_STRING_MAX];

    vm_load_cmd_sets(1);
    snprintf(prompt, sizeof(prompt), "%s", gbl_ecl_string(1));

    ecl_vm_log("CMD_Who: Prompt: '%s'", prompt);

    frames_clear_region(TEXT_REGION_NORMAL_BOTTOM);
    character_select_a_player(&gbl.selected_player, false, prompt);
}

/* 0x36 ADD NPC, sub_28DCA. Brings a named character into the party. As with ECL
 * CLOCK the table says one operand and the handler reads two. */
static void cmd_add_npc(void)
{
    int     npc_id;
    u8      morale;
    Player *player;

    vm_load_cmd_sets(2);
    npc_id = (u8)vm_get_cmd_value(1);

    savegame_load_npc(npc_id);

    morale = (u8)vm_get_cmd_value(2);

    player = selected("ADD NPC");
    if (player == NULL) {
        return;
    }

    player->control_morale = (u8)((morale >> 1) + CONTROL_NPC_BASE);

    character_recalc_values(player);
    character_party_summary(player);
}

/* 0x3B SPELL. Finds who in the party has a spell memorized: the answer is which
 * of their memorized spells it is, counting from 1, and which character. Not
 * found is 0xff and the last character on the list, which is what the original's
 * post-loop decrement leaves behind. */
static void cmd_spell(void)
{
    u8   spell_id;
    u16  loc_a;
    u16  loc_b;
    u8   spell_index = 1;
    u8   player_index = 0;
    bool spell_found = false;

    vm_load_cmd_sets(3);

    spell_id = (u8)vm_get_cmd_value(1);
    loc_a    = ecl_op_word(&gbl.cmd_opps[2]);
    loc_b    = ecl_op_word(&gbl.cmd_opps[3]);

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *player = gbl.team_list[i];

        spell_index = 1;

        if (player != NULL) {
            for (int j = 0; j < player->spell_list.count; j++) {
                if (player->spell_list.items[j].id == spell_id) {
                    spell_found = true;
                    break;
                }

                spell_index = (u8)(spell_index + 1);
            }
        }

        if (spell_found) {
            break;
        }

        player_index++;
    }

    if (spell_found == false) {
        player_index--;
        spell_index = 0xff;
    }

    ecl_vm_log("CMD_Spell: spell_id: %u loc a: 0x%04X val a: %u loc b: 0x%04X "
               "val b: %u", (unsigned)spell_id, (unsigned)loc_a,
               (unsigned)spell_index, (unsigned)loc_b, (unsigned)player_index);

    vm_set_memory_value(spell_index, loc_a);
    vm_set_memory_value(player_index, loc_b);
}

/* 0x2D CALL. The escape hatch: the operand is not an address but a number naming
 * one of seven things the interpreter cannot express - redraw the view, set up the
 * arena duel, play a sound, step forward, re-read the wall type, advance the
 * picture's animation. The numbers are biased by 0x7fff like the rest of the
 * machine's addresses, and the two low ones fall out of that bias. */
static void cmd_call(void)
{
    u16 var_2;
    u16 var_4;

    vm_load_cmd_sets(1);

    var_2 = ecl_op_word(&gbl.cmd_opps[1]);
    var_4 = (u16)(var_2 - 0x7fff);

    ecl_vm_log("CMD_Call: 0x%X", (unsigned)var_4);

    switch (var_4) {
    case 0xae11:
        gbl.map_wall_roof = view3d_get_wall_x2(gbl.map_pos_y, gbl.map_pos_x);

        if (gbl.byte_1AB0B == true) {
            if (gbl.sprite_changed == true || gbl.display_player_sprite ||
                gbl.byte_1EE91 == true || gbl.position_changed == true ||
                gbl.byte_1EE94 == true) {
                gbl.can_draw_bigpic = true;
                view3d_redraw();
                character_display_map_position_time();
                gbl.byte_1EE94            = false;
                gbl.byte_1EE91            = false;
                gbl.position_changed      = false;
                gbl.sprite_changed        = false;
                gbl.display_player_sprite = false;

                gbl.map_wall_type = view3d_map_wall_type(gbl.map_direction,
                                                         gbl.map_pos_y,
                                                         gbl.map_pos_x);
            }
        }
        break;

    case 1:
        vm_setup_duel(true);
        break;

    case 2:
        vm_setup_duel(false);
        break;

    case 0x3201:
        if (gbl.word_1EE76 == 10) {
            sound_play(SOUND_B);
        } else {
            sound_play(SOUND_A);
        }
        break;

    case 0x401f:
        vm_move_position_forward();
        break;

    case 0x4019:
        if (gbl.area_ptr->in_dungeon == 0) {
            gbl.map_wall_type = view3d_map_wall_type(gbl.map_direction,
                                                     gbl.map_pos_y,
                                                     gbl.map_pos_x);
        }
        break;

    case 0xe804:
        picture_draw_maybe_overlayed(dax_array_current_picture(&gbl.pic_frames),
                                     true, 3, 3);

        dax_array_next_frame(&gbl.pic_frames);

        text_game_delay();
        break;

    default:
        break;
    }
}

void eclvm_try_encamp(void)
{
    eclvm_run(gbl.pre_camp_check_addr);

    if (camp_make_camp() == true) {
        character_load_pic();
        eclvm_run(gbl.camp_interrupted_addr);
    }

    gbl.can_draw_bigpic = true;
    view3d_redraw();
    gbl.game_saved = false;
}

/* 0x38 PROGRAM. Hands control to something outside the interpreter: 0 the party
 * menu, 8 the ending, 9 camping, 3 "the party is dead". */
static void cmd_program(void)
{
    u8 var_1;

    vm_load_cmd_sets(1);
    var_1 = (u8)vm_get_cmd_value(1);

    if (gbl.restore_player_ptr == true) {
        gbl.selected_player    = gbl.last_selected_player;
        gbl.restore_player_ptr = false;
    }

    if (var_1 == 0) {
        partymenu_start_game_menu();
        if (gbl.last_dax_block_id != 0x50 && gbl.area_ptr->in_dungeon == 0) {
            character_load_pic();
        }
    } else if (var_1 == 8) {
        char save_yes;

        endgame_text();
        gbl.game_won                        = true;
        gbl.area_ptr->field_3FA             = 0xff;
        gbl.area2_ptr->training_class_mask  = 0xff;

        /* Everybody is put back on their feet for the party menu that follows,
         * so that the party the player looks over at the end is the whole one. */
        for (int i = 0; i < gbl.team_count; i++) {
            Player *player = gbl.team_list[i];

            if (player == NULL) {
                continue;
            }

            player->hit_point_current = player->hit_point_max;
            player->health_status     = STATUS_OKEY;
            player->in_combat         = true;
        }

        partymenu_start_game_menu();
        save_yes = prompt_yes_no(GBL_DEFAULT_MENU_COLORS,
                                 "You've won. Save before quitting? ");

        if (save_yes == 'Y') {
            savegame_save_game();
        }

        game_print_and_exit();
    } else if (var_1 == 9) {
        u16 ecl_bkup = gbl.ecl_offset;

        eclvm_try_encamp();
        gbl.ecl_offset = ecl_bkup;
        cmd_exit();
    } else if (var_1 == 3) {
        gbl.party_killed = true;
        cmd_exit();
    }
}

/* 0x3C PROTECTION, sub_2923F. The manual lookup the game asks for on the way out
 * of the first map. */
static void cmd_protection(void)
{
    ecl_vm_log("CMD_Protection:");

    gbl.encounter_flags[0] = false;
    gbl.encounter_flags[1] = false;
    gbl.sprite_changed     = false;
    vm_load_cmd_sets(1);

    if (cheats.skip_copy_protection == false) {
        protect_copy_protection();
    }
    character_load_pic();
}

/* 0x3E DUMP, sub_29271. Throws the selected character out of the party, which is
 * what a script does when a character it added leaves again. */
static void cmd_dump(void)
{
    gbl.ecl_offset++;

    ecl_vm_log("CMD_Dump: Player: %s",
               gbl.selected_player != NULL ? gbl.selected_player->name : "-");

    gbl.selected_player =
        partymenu_free_current_player(gbl.selected_player, true, false);

    gbl.last_selected_player = gbl.selected_player;

    character_party_summary(gbl.selected_player);
}

/* 0x3F FIND SPECIAL, sub_292A5. Whether the selected character has an affect. */
static void cmd_find_special(void)
{
    Affects affect_type;
    Player *player;

    clear_compare_flags();

    vm_load_cmd_sets(1);
    affect_type = (Affects)vm_get_cmd_value(1);

    player = selected("FIND SPECIAL");

    if (player != NULL && player_has_affect(player, affect_type) == true) {
        gbl.compare_flags[0] = true;
    } else {
        gbl.compare_flags[1] = true;
    }
}

/* 0x40 DESTROY ITEMS, sub_292F9. Takes one kind of item off everybody, which is
 * how the story removes a quest item once it has been used. */
static void cmd_destroy_items(void)
{
    ItemType item_type;

    vm_load_cmd_sets(1);
    item_type = (ItemType)vm_get_cmd_value(1);

    ecl_vm_log("CMD_DestroyItems: type: %d", (int)item_type);

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        /* Backwards, so that removing one does not move an unexamined item down
         * into the slot the loop has just passed. */
        for (int j = player->item_count - 1; j >= 0; j--) {
            if (player->items[j].type == (int)item_type) {
                player_item_remove(player, j);
            }
        }

        character_recalc_values(player);
    }
}

/* --------------------------------------------------------- the instruction set */

typedef struct {
    int         size;       /* operands, for stepping over one unrun */
    const char *name;
    void      (*run)(void);
} EclCommand;

/* ovr003.SetupCommandTable. The C# built this at start-up because it is a
 * dictionary of delegates; here it is what it always was in the DOS build, a flat
 * table indexed by the opcode.
 *
 * Entry 0x1F is the one the disassembly never resolved. The C# left its handler
 * null - reaching it would have thrown - and its size of 2 is what the table said,
 * so an instruction of it can at least be stepped over. */
static const EclCommand eclvm_commands[ECLVM_COMMAND_COUNT] = {
    /* 0x00 */ { 0, "EXIT",            cmd_exit },
    /* 0x01 */ { 1, "GOTO",            cmd_goto },
    /* 0x02 */ { 1, "GOSUB",           cmd_gosub },
    /* 0x03 */ { 2, "COMPARE",         cmd_compare },
    /* 0x04 */ { 3, "ADD",             cmd_add_sub_div_multi },
    /* 0x05 */ { 3, "SUBTRACT",        cmd_add_sub_div_multi },
    /* 0x06 */ { 3, "DIVIDE",          cmd_add_sub_div_multi },
    /* 0x07 */ { 3, "MULTIPLY",        cmd_add_sub_div_multi },
    /* 0x08 */ { 2, "RANDOM",          cmd_random },
    /* 0x09 */ { 2, "SAVE",            cmd_save },
    /* 0x0A */ { 1, "LOAD CHARACTER",  cmd_load_character },
    /* 0x0B */ { 3, "LOAD MONSTER",    cmd_load_monster },
    /* 0x0C */ { 3, "SETUP MONSTER",   cmd_setup_monster },
    /* 0x0D */ { 0, "APPROACH",        cmd_approach },
    /* 0x0E */ { 1, "PICTURE",         cmd_picture },
    /* 0x0F */ { 2, "INPUT NUMBER",    cmd_input_number },
    /* 0x10 */ { 2, "INPUT STRING",    cmd_input_string },
    /* 0x11 */ { 1, "PRINT",           cmd_print },
    /* 0x12 */ { 1, "PRINTCLEAR",      cmd_print },
    /* 0x13 */ { 0, "RETURN",          cmd_return },
    /* 0x14 */ { 4, "COMPARE AND",     cmd_compare_and },
    /* 0x15 */ { 0, "VERTICAL MENU",   cmd_vert_menu },
    /* 0x16 */ { 0, "IF =",            cmd_if },
    /* 0x17 */ { 0, "IF <>",           cmd_if },
    /* 0x18 */ { 0, "IF <",            cmd_if },
    /* 0x19 */ { 0, "IF >",            cmd_if },
    /* 0x1A */ { 0, "IF <=",           cmd_if },
    /* 0x1B */ { 0, "IF >=",           cmd_if },
    /* 0x1C */ { 0, "CLEARMONSTERS",   cmd_clear_monsters },
    /* 0x1D */ { 1, "PARTYSTRENGTH",   cmd_party_strength },
    /* 0x1E */ { 6, "CHECKPARTY",      cmd_check_party },
    /* 0x1F */ { 2, "notsure 0x1f",    NULL },
    /* 0x20 */ { 1, "NEWECL",          cmd_new_ecl },
    /* 0x21 */ { 3, "LOAD FILES",      cmd_load_files },
    /* 0x22 */ { 2, "PARTY SURPRISE",  cmd_party_surprise },
    /* 0x23 */ { 4, "SURPRISE",        cmd_surprise },
    /* 0x24 */ { 0, "COMBAT",          cmd_combat },
    /* 0x25 */ { 0, "ON GOTO",         cmd_on_goto_gosub },
    /* 0x26 */ { 0, "ON GOSUB",        cmd_on_goto_gosub },
    /* 0x27 */ { 8, "TREASURE",        cmd_treasure },
    /* 0x28 */ { 3, "ROB",             cmd_rob },
    /* 0x29 */ { 14, "ENCOUNTER MENU", cmd_encounter_menu },
    /* 0x2A */ { 3, "GETTABLE",        cmd_get_table },
    /* 0x2B */ { 0, "HORIZONTAL MENU", cmd_horizontal_menu },
    /* 0x2C */ { 6, "PARLAY",          cmd_parlay },
    /* 0x2D */ { 1, "CALL",            cmd_call },
    /* 0x2E */ { 5, "DAMAGE",          cmd_damage },
    /* 0x2F */ { 3, "AND",             cmd_and_or },
    /* 0x30 */ { 3, "OR",              cmd_and_or },
    /* 0x31 */ { 0, "SPRITE OFF",      cmd_sprite_off },
    /* 0x32 */ { 1, "FIND ITEM",       cmd_find_item },
    /* 0x33 */ { 0, "PRINT RETURN",    cmd_print_return },
    /* 0x34 */ { 1, "ECL CLOCK",       cmd_ecl_clock },
    /* 0x35 */ { 3, "SAVE TABLE",      cmd_save_table },
    /* 0x36 */ { 1, "ADD NPC",         cmd_add_npc },
    /* 0x37 */ { 3, "LOAD PIECES",     cmd_load_files },
    /* 0x38 */ { 1, "PROGRAM",         cmd_program },
    /* 0x39 */ { 1, "WHO",             cmd_who },
    /* 0x3A */ { 0, "DELAY",           cmd_delay },
    /* 0x3B */ { 3, "SPELL",           cmd_spell },
    /* 0x3C */ { 1, "PROTECTION",      cmd_protection },
    /* 0x3D */ { 0, "CLEAR BOX",       cmd_clear_box },
    /* 0x3E */ { 0, "DUMP",            cmd_dump },
    /* 0x3F */ { 1, "FIND SPECIAL",    cmd_find_special },
    /* 0x40 */ { 1, "DESTROY ITEMS",   cmd_destroy_items }
};

static const EclCommand *command_at(int opcode)
{
    if (opcode < 0 || opcode >= ECLVM_COMMAND_COUNT) {
        return NULL;
    }

    return &eclvm_commands[opcode];
}

const char *eclvm_command_name(int opcode)
{
    const EclCommand *cmd = command_at(opcode);

    return cmd != NULL ? cmd->name : NULL;
}

int eclvm_command_size(int opcode)
{
    const EclCommand *cmd = command_at(opcode);

    return cmd != NULL ? cmd->size : -1;
}

bool eclvm_command_known(int opcode)
{
    const EclCommand *cmd = command_at(opcode);

    return cmd != NULL && cmd->run != NULL;
}

/* CmdItem.Skip. Steps over the instruction at gbl.ecl_offset without running it,
 * which is what a conditional jump does when its flag is clear. An instruction
 * with no operands is one byte; one with operands is decoded and thrown away,
 * because only the decoder knows how long each operand is. */
static void skip_next_command(void)
{
    const EclCommand *cmd;

    gbl.command = ecl_block_get(gbl.ecl_ptr, gbl.ecl_offset + 0x8000);

    cmd = command_at(gbl.command);
    if (cmd == NULL) {
        log_warn("ecl vm: skipping unknown opcode 0x%02X at 0x%X",
                 (unsigned)gbl.command, gbl.ecl_offset);
        gbl.ecl_offset += 1;
        return;
    }

    if (gbl.print_commands == true) {
        ecl_vm_log("SKIPPING: %s", cmd->name);
    }

    if (cmd->size == 0) {
        gbl.ecl_offset += 1;
    } else {
        vm_load_cmd_sets(cmd->size);
    }
}

void eclvm_run(u16 offset)
{
    gbl.ecl_offset = offset;
    gbl.stop_vm    = false;

    while (gbl.stop_vm == false && gbl.party_killed == false) {
        const EclCommand *cmd;

        gbl.command = ecl_block_get(gbl.ecl_ptr, gbl.ecl_offset + 0x8000);

        ecl_vm_log("0x%X", gbl.ecl_offset);

        cmd = command_at(gbl.command);

        /* The C# logged an opcode it had no handler for and went round again on
         * the same byte, which hung the game; opcode 0x1F, whose handler it left
         * null, would have thrown instead. Both become the same thing here: say
         * so, step over the instruction, and carry on - a script that has gone
         * off the rails then walks forward until it finds an EXIT. */
        if (cmd == NULL) {
            log_warn("ecl vm: unknown opcode 0x%02X at 0x%X",
                     (unsigned)gbl.command, gbl.ecl_offset);
            gbl.ecl_offset += 1;
            continue;
        }
        if (cmd->run == NULL) {
            log_warn("ecl vm: opcode 0x%02X (%s) has no handler; stepping over "
                     "it", (unsigned)gbl.command, cmd->name);
            skip_next_command();
            continue;
        }

        if (gbl.print_commands) {
            log_debug("%s 0x%X", cmd->name, (unsigned)gbl.command);
        }

        cmd->run();
    }

    gbl.stop_vm = false;
}

void eclvm_restart_after_new_ecl(void)
{
    do {
        picture_dax_array_free_blocks(&gbl.pic_frames);
        gbl.saved_dax_file[0]  = '\0';
        gbl.saved_dax_block_id = 0xff;
        gbl.vm_flag01          = false;
        gbl.map_wall_roof = view3d_get_wall_x2(gbl.map_pos_y, gbl.map_pos_x);

        gbl.area2_ptr->tried_to_exit_map = false;

        gbl.last_selected_player = gbl.selected_player;

        eclvm_run(gbl.ecl_initial_entry_point);

        if (gbl.vm_flag01 == false) {
            gbl.area_ptr->last_ecl_block_id = gbl.ecl_block_id;
        }

        if (gbl.vm_flag01 == false) {
            if (((gbl.last_game_state != GAME_STATE_DUNGEON_MAP ||
                  gbl.game_state == GAME_STATE_DUNGEON_MAP) &&
                 gbl.byte_1AB0B == true) ||
                (gbl.last_game_state == GAME_STATE_DUNGEON_MAP &&
                 gbl.game_state == GAME_STATE_DUNGEON_MAP)) {
                view3d_redraw();
            }
            gbl.vm_flag01 = false;

            eclvm_run(gbl.vm_run_addr_1);

            if (gbl.vm_flag01 == false) {
                eclvm_run(gbl.search_location_addr);

                if (gbl.vm_flag01 == false) {
                    gbl.selected_player = gbl.last_selected_player;
                    character_party_summary(gbl.selected_player);
                }
            }
        }
    } while (gbl.vm_flag01 == true);

    gbl.last_game_state = gbl.game_state;
}

void eclvm_world_loop(void)
{
    gbl.last_selected_player = gbl.selected_player;

    gbl.can_draw_bigpic    = true;
    gbl.byte_1AB0C         = false;
    gbl.files_loaded       = false;
    gbl.restore_player_ptr = false;
    gbl.byte_1AB0B         = false;
    gbl.byte_1EE98         = true;
    gbl.game_state         = GAME_STATE_DUNGEON_MAP;
    gbl.vm_flag01          = false;

    /* Block 0 is "no script yet", i.e. a new game rather than a saved one. */
    if (gbl.area_ptr->last_ecl_block_id == 0) {
        gbl.byte_1EE98 = false;

        if (gbl.in_demo == true) {
            gbl.ecl_block_id = 0x52;
        } else {
            gbl.ecl_block_id = 1;

            character_party_summary(gbl.selected_player);
        }
    } else {
        gbl.ecl_block_id = (u8)gbl.area_ptr->last_ecl_block_id;
    }

    if (gbl.area_ptr->in_dungeon == 0) {
        gbl.game_state = GAME_STATE_WILDERNESS_MAP;
    }

    if (gbl.reload_ecl_and_pictures == true ||
        gbl.area_ptr->last_ecl_block_id == 0) {
        vm_load_ecl_dax(gbl.ecl_block_id);
    } else {
        gbl.byte_1AB0B = true;
    }

    vm_init_ecl();

    eclvm_run(gbl.ecl_initial_entry_point);

    /* The demo runs one script and then throws the party away; there is no menu
     * and no loop. */
    if (gbl.in_demo == true) {
        while (gbl.team_count > 0) {
            partymenu_free_current_player(gbl.team_list[0], true, true);
        }
        gbl.selected_player = NULL;

        return;
    }

    if (gbl.vm_flag01 == false) {
        gbl.area_ptr->last_ecl_block_id = gbl.ecl_block_id;
    } else {
        eclvm_restart_after_new_ecl();
    }

    if (gbl.game_state != GAME_STATE_WILDERNESS_MAP &&
        gbl.reload_ecl_and_pictures == true) {
        if (gbl.byte_1EE98 == true) {
            character_load_pic();
        }

        gbl.can_draw_bigpic = true;
        view3d_redraw();
    }

    gbl.reload_ecl_and_pictures = false;

    do {
        char key = dungeon_main_3d_world_menu();

        gbl.last_selected_player = gbl.selected_player;

        if (gbl.vm_flag01 == false) {
            gbl.area_ptr->last_ecl_block_id = gbl.ecl_block_id;
        }

        /* Camping and searching both run a script and then hand the menu back,
         * because either can leave the party somewhere that wants searching
         * again. The search flags carry "the party is looking at something" in
         * the bits above the first; the search script runs with them forced to 1
         * so that it cannot see the search it is itself part of. */
        while ((gbl.area2_ptr->search_flags > 1 ||
                (key >= 'a' && key <= 'z' ? key - 0x20 : key) == 'E') &&
               gbl.party_killed == false) {
            if ((key >= 'a' && key <= 'z' ? key - 0x20 : key) == 'E') {
                eclvm_try_encamp();
            } else {
                gbl.search_flag_bkup         = gbl.area2_ptr->search_flags & 1;
                gbl.area2_ptr->search_flags  = 1;
                gbl.can_draw_bigpic          = true;
                view3d_redraw();

                eclvm_run(gbl.search_location_addr);

                if (gbl.vm_flag01 == true) {
                    eclvm_restart_after_new_ecl();
                }

                gbl.area2_ptr->search_flags = (u16)gbl.search_flag_bkup;
            }

            if (gbl.party_killed == false) {
                key = dungeon_main_3d_world_menu();
                gbl.last_selected_player = gbl.selected_player;
            }
        }

        if (gbl.party_killed == false) {
            eclvm_run(gbl.vm_run_addr_1);
        }

        if (gbl.vm_flag01 == true) {
            eclvm_restart_after_new_ecl();
        } else if (gbl.party_killed == false) {
            gbl.area_ptr->last_x_pos = (i16)gbl.map_pos_x;
            gbl.area_ptr->last_y_pos = (i16)gbl.map_pos_y;

            dungeon_locked_door();
            view3d_redraw();

            /* A door that shut put the party back where they were, and that is
             * the thump. */
            if (gbl.area_ptr->last_x_pos != gbl.map_pos_x ||
                gbl.area_ptr->last_y_pos != gbl.map_pos_y) {
                sound_play(SOUND_A);
            }

            gbl.sprite_changed = false;
            gbl.byte_1EE8D     = true;
            eclvm_run(gbl.search_location_addr);
            if (gbl.vm_flag01 == true) {
                eclvm_restart_after_new_ecl();
            }
        }
    } while (gbl.party_killed == false);

    gbl.party_killed = false;
}
