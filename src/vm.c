/* vm.c - Ported from engine/ovr008.cs. See vm.h. */

#include "vm.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "affect.h"
#include "area.h"
#include "character.h"
#include "dax.h"
#include "ecl.h"
#include "effect.h"
#include "enums.h"
#include "fileio.h"
#include "frames.h"
#include "icons.h"
#include "item.h"
#include "log.h"
#include "money.h"
#include "picture.h"
#include "prompt.h"
#include "quit.h"
#include "roster.h"
#include "spelllist.h"
#include "text.h"
#include "view3d.h"
#include "viewplayer.h"

/* gbl.SelectedPlayer: whose sheet the party address space is a window onto, and
 * whose turn a duel or a menu is about. The C# read it without checking and
 * would have thrown; here the caller gets NULL and has already been told why. */
static Player *selected(const char *who)
{
    if (gbl.selected_player == NULL) {
        log_warn("ecl vm: %s ran with nobody selected", who);
    }

    return gbl.selected_player;
}

/* ---------------------------------------------------------- loading a script */

void vm_load_ecl_dax(u8 block_id)
{
    char  file_name[GBL_DAX_NAME_MAX];
    u8   *block_mem;
    i16   block_size = 0;

    ecl_block_clear(gbl.ecl_ptr);

    prompt_clear_area();
    text_display_string("Loading...Please Wait", 0, 10, 0x18, 0);

    snprintf(file_name, sizeof(file_name), "ECL%u.dax", (unsigned)gbl.game_area);

    block_mem = dax_load_decode(file_name, block_id, &block_size);

    /* The C# looped back and asked again for anything under two bytes, which on
     * the DOS build was the prompt to put the right floppy in. Nothing here can
     * change between two attempts, so a short block is fatal instead - as it is
     * for the wall definitions in view3d.c. */
    if (block_mem == NULL || block_size < 2) {
        free(block_mem);
        game_log_and_exit("Unable to load ECL block %u from ECL%u.",
                          (unsigned)block_id, (unsigned)gbl.game_area);
    }

    /* The first two bytes are the block's own header and are not code. */
    ecl_block_set_data(gbl.ecl_ptr, block_mem, (size_t)block_size, 2,
                       (size_t)(block_size - 2));
    free(block_mem);

    prompt_clear_area();
}

void vm_init_ecl(void)
{
    gbl.sprite_changed        = false;
    gbl.redraw_party_summary1 = false;
    gbl.redraw_party_summary2 = false;
    gbl.byte_1EE91            = true;

    gbl.encounter_flags[0] = false;
    gbl.encounter_flags[1] = false;
    gbl.monster_icon_id    = 8;      /* 0..7 are the party's own icon slots */
    gbl.ecl_offset         = 0x8000;
    gbl.byte_1DA70         = false;

    gbl_vm_call_stack_clear();

    for (int i = 0; i < GBL_COMPARE_FLAGS; i++) {
        gbl.compare_flags[i] = false;
    }

    gbl.area2_ptr->head_block_id = 0xff;

    gbl.area2_ptr->rest_encounter_period     = 0;
    gbl.area2_ptr->rest_encounter_percentage = 0;
    gbl.area_ptr->can_cast_spells            = false;

    /* The script's header is five addresses, in this order: where to go when the
     * party moves, where to go when it searches, the two camping hooks, and
     * where the script starts. Each is read as a one-operand instruction. */
    vm_load_cmd_sets(1);
    gbl.vm_run_addr_1 = ecl_op_word(&gbl.cmd_opps[1]);
    vm_load_cmd_sets(1);
    gbl.search_location_addr = ecl_op_word(&gbl.cmd_opps[1]);
    vm_load_cmd_sets(1);
    gbl.pre_camp_check_addr = ecl_op_word(&gbl.cmd_opps[1]);
    vm_load_cmd_sets(1);
    gbl.camp_interrupted_addr = ecl_op_word(&gbl.cmd_opps[1]);
    vm_load_cmd_sets(1);
    gbl.ecl_initial_entry_point = ecl_op_word(&gbl.cmd_opps[1]);

    gbl.area_ptr->in_dungeon = 1;

    /* Picking a script back up - a saved game, or coming out of a fight - keeps
     * the area records that were restored with it. */
    if (!gbl.reload_ecl_and_pictures) {
        area1_reset_field_200(gbl.area_ptr);
        area2_reset_field_6F2(gbl.area2_ptr);
    }
}

/* ----------------------------------------------------- decoding instructions */

void vm_load_cmd_sets(int number_of_sets)
{
    int str_index = 0;

    if (number_of_sets < 0 || number_of_sets >= ECL_CMD_OPS_LIMIT) {
        log_warn("ecl vm: an instruction at 0x%04x claims %d operands; the "
                 "machine holds %d", gbl.ecl_offset, number_of_sets,
                 ECL_CMD_OPS_LIMIT - 1);
        number_of_sets = 0;
    }

    for (int i = 0; i < ECL_CMD_OPS_LIMIT; i++) {
        ecl_op_clear(&gbl.cmd_opps[i]);
        /* Codes 1, 3 and 0x80 make the operand an address, so reading its value
         * means reading the machine's memory; ecl.c does not know how to and
         * takes the reader from here. */
        gbl.cmd_opps[i].get_memory_value = vm_get_memory_value;
    }

    for (int loop_var = 1; loop_var <= number_of_sets; loop_var++) {
        EclOp *op = &gbl.cmd_opps[loop_var];
        u8 code = ecl_block_get(gbl.ecl_ptr, 0x8000 + gbl.ecl_offset + 1);
        u8 low  = ecl_block_get(gbl.ecl_ptr, 0x8000 + gbl.ecl_offset + 2);

        ecl_op_set_code(op, code);
        ecl_op_set_low(op, low);

        gbl.ecl_offset = (u16)(gbl.ecl_offset + 2);

        if (code == 1 || code == 2 || code == 3) {
            /* An address, or an immediate word: one more byte to go. */
            gbl.ecl_offset = (u16)(gbl.ecl_offset + 1);
            ecl_op_set_high(op, ecl_block_get(gbl.ecl_ptr,
                                              0x8000 + gbl.ecl_offset));
        } else if (code == 0x80) {
            /* Packed text sitting inline in the script; `low` is its length in
             * bytes, and the operand carries no word of its own. */
            str_index++;

            if (low > 0) {
                vm_load_compressed_ecl_string(str_index, low);
            } else {
                gbl_ecl_string_set(str_index, "");
            }
        } else if (code == 0x81) {
            /* A word naming a string somewhere in the machine's memory. */
            str_index++;
            gbl.ecl_offset = (u16)(gbl.ecl_offset + 1);
            ecl_op_set_high(op, ecl_block_get(gbl.ecl_ptr,
                                              0x8000 + gbl.ecl_offset));

            vm_copy_string_from_memory(ecl_op_word(op), str_index);
        }
        /* Code 0, and anything the decoder does not recognise, is the immediate
         * byte already in `low`. */
    }

    /* Onto the next instruction - or, when number_of_sets was 0, just past the
     * opcode, which is how the zero-operand instructions advance. */
    gbl.ecl_offset = (u16)(gbl.ecl_offset + 1);
}

u16 vm_get_cmd_value(int index)
{
    if (index < 0 || index >= ECL_CMD_OPS_LIMIT) {
        log_warn("ecl vm: no operand %d; an instruction carries %d", index,
                 ECL_CMD_OPS_LIMIT - 1);
        return 0;
    }

    return ecl_op_value(&gbl.cmd_opps[index]);
}

/* ------------------------------------------------------------ machine memory */

int vm_get_memory_value_type(u16 loc)
{
    int var_1 = 4;

    if (loc >= 0x4B00 && loc <= 0x4EFF) {
        var_1 = 0;
    }

    if (loc >= 0x7C00 && loc <= 0x7FFF) {
        var_1 = 1;
    }

    if (loc >= 0x7A00 && loc <= 0x7BFF) {
        var_1 = 2;
    }

    if (loc >= 0x8000 && loc <= 0x9DFF) {
        var_1 = 3;
    }

    return var_1;
}

u16 vm_find_team_index(const Player *player)
{
    int index = gbl_team_index_of(player);

    /* Not on the list reads as one past the end, so that "not found" and the
     * first entry are different answers without needing a sentinel. */
    if (index == -1) {
        index = gbl.team_count;
    }

    return (u16)index;
}

u16 vm_get_player_values(bool *out_found, u16 loc)
{
    Player *player = selected("a script reading a character sheet");
    u16 return_val;
    u16 cell;

    *out_found = true;

    if (player == NULL) {
        *out_found = false;
        return 0;
    }

    cell = (u16)(loc - 0x7c00);

    if (cell == 0x15) {
        return_val = (u8)player->stats.value[PSTAT_INT].full;
    } else if (cell == 0x18) {
        return_val = (u8)player->stats.value[PSTAT_CON].full;
    } else if (cell == 0x72) {
        return_val = (u16)player->race;
    } else if (cell == 0x73) {
        return_val = (u16)player->cls;
    } else if (cell == 0x9b) {
        return_val = player->save_verse[SAVE_VERSE_PETRIFICATION];
    } else if (cell == 0xa0) {
        return_val = player->hit_dice;
    } else if (cell >= 0xA5 && cell <= 0xAC) {
        return_val = player->thief_skills[cell - 0xA5];
    } else if (cell == 0xb8) {
        return_val = player->control_morale;
    } else if (cell == 0xBB) {
        return_val = (u16)money_get(&player->money, MONEY_COPPER);
    } else if (cell == 0xBD) {
        return_val = (u16)money_get(&player->money, MONEY_ELECTRUM);
    } else if (cell == 0xBF) {
        return_val = (u16)money_get(&player->money, MONEY_SILVER);
    } else if (cell == 0xC1) {
        return_val = (u16)money_get(&player->money, MONEY_GOLD);
    } else if (cell == 0xC3) {
        return_val = (u16)money_get(&player->money, MONEY_PLATINUM);
    } else if (cell == 0xC9) {
        return_val = (u16)player_skill_level(player, SKILL_MAGIC_USER);
    } else if (cell == 0xD6) {
        return_val = player->sex;
    } else if (cell == 0xD8) {
        return_val = player->alignment;
    } else if (cell == 0xE4) {
        return_val = (u16)(player->field_192 & 1);
    } else if (cell == 0xF7) {
        return_val = (u16)player->field_13C;
    } else if (cell == 0xF9) {
        return_val = player->field_13E;
    } else if (cell == 0x100) {
        /* "Is this character in the fight?" - and the one read that clears the
         * flag set when a script asked for somebody who is not in the party. */
        return_val = player->in_combat ? 1 : 0x80;

        if (gbl.player_not_found) {
            return_val = 0;
        }

        gbl.player_not_found = false;
    } else if (cell == 0x10C) {
        if (player->combat_team == TEAM_OURS &&
            player->quick_fight == QUICK_FIGHT_TRUE) {
            return_val = 0x80;
        } else if (player->combat_team == TEAM_ENEMY) {
            return_val = 0x81;
        } else {
            return_val = 0;
        }
    } else if (cell == 0x10D) {
        /* The C# assigned 0 and then threw NotImplementedException, so what the
         * DOS build did here was never worked out. Answering the 0 it had
         * already assigned keeps a script that reads this cell running; the
         * warning is here so that it shows up if one ever does. */
        log_warn("ecl vm: a script read party cell 0x10d, which was never "
                 "translated; answering 0");
        return_val = 0;
    } else if (cell == 0x11B) {
        return_val = player->movement;
    } else if (cell == 0x2B1 || cell == 0x2B4) {
        return_val = vm_find_team_index(player);
    } else if (cell == 0x2CF) {
        /* Charisma as a percentage, the way the reaction tables wanted it. */
        switch (player->stats.value[PSTAT_CHA].full) {
        case 3:    return_val = 0;    break;
        case 4:    return_val = 5;    break;
        case 5:    return_val = 0x0A; break;
        case 6:    return_val = 0x0F; break;
        case 7:    return_val = 0x14; break;
        case 8:
        case 9:
        case 0x0a:
        case 0x0b:
        case 0x0c: return_val = 0x19; break;
        case 0x0d: return_val = 0x1E; break;
        case 0x0e: return_val = 0x23; break;
        case 0x0f: return_val = 0x28; break;
        case 0x10: return_val = 0x32; break;
        case 0x11: return_val = 0x37; break;
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19: return_val = 0x3C; break;
        default:   return_val = 0;    break;
        }
    } else if (cell == 0x312) {
        return_val = gbl.game_area;
    } else if (cell == 0x33E) {
        return_val = gbl.area2_ptr->party_size;
    } else {
        /* Not the character's own: the caller reads the party record instead. */
        return_val = 0;
        *out_found = false;
    }

    return return_val;
}

void vm_alter_character(u16 set_value, u16 switch_var)
{
    Player *player = selected("a script writing a character sheet");
    u16 cell = (u16)(switch_var - 0x7c00);

    /* Three of these are not about the character at all, and the original ran
     * them whether or not there was one selected. */
    if (cell == 0x312) {
        file_set_game_area((u8)set_value);
        return;
    }
    if (cell == 0x322 || cell == 0x324 || cell == 0x326) {
        /* The top bit says "this is a set to load" rather than a plain value. */
        if (set_value > 0x80) {
            set_value &= 0x7f;
            view3d_load_walldef((cell - 0x320) / 2, set_value & 0xff);
        }
        return;
    }

    if (cell == 0) {
        if (set_value == 0) {
            gbl.redraw_party_summary2 = true;
        }
        return;
    }

    if (player == NULL) {
        return;
    }

    if (cell >= 0x20 && cell <= 0x70) {
        /* The slot the script names is ignored: the spell list works out where a
         * newly learnt spell goes for itself. */
        log_debug("ecl vm: %s learns spell 0x%02x (script slot %d)",
                  player->name, (unsigned)(set_value & 0xff),
                  (int)(cell - 0x1f));
        spell_list_add_learnt(&player->spell_list, set_value & 0x0ff);
    } else if (cell == 0xb8) {
        /* Morale above the NPC base is folded back down into it. */
        if (set_value > 0xb2) {
            set_value = (u16)(set_value - 0x32);
        }

        player->control_morale = (u8)set_value;
    } else if (cell == 0xbb) {
        money_set(&player->money, MONEY_COPPER, set_value);
    } else if (cell == 0xbd) {
        money_set(&player->money, MONEY_ELECTRUM, set_value);
    } else if (cell == 0xbf) {
        money_set(&player->money, MONEY_SILVER, set_value);
    } else if (cell == 0xc1) {
        money_set(&player->money, MONEY_GOLD, set_value);
    } else if (cell == 0xc3) {
        money_set(&player->money, MONEY_PLATINUM, set_value);
    } else if (cell == 0xf7) {
        player->field_13C = (i16)set_value;
    } else if (cell == 0xf9) {
        player->field_13E = (u8)set_value;
    } else if (cell == 0x100) {
        if (set_value >= 0x80) {
            player->in_combat = false;
            if (set_value == 0x87) {
                player->health_status = STATUS_STONED;
            }
        }

        if (set_value == 0) {
            gbl.redraw_party_summary1 = true;
        }
    } else if (cell == 0x10c) {
        switch (set_value) {
        case 0:
            player->combat_team = TEAM_OURS;
            player->quick_fight = QUICK_FIGHT_FALSE;
            break;

        case 0x80:
            player->combat_team = TEAM_OURS;
            player->quick_fight = QUICK_FIGHT_TRUE;
            break;

        case 0x81:
            player->combat_team = TEAM_ENEMY;
            player->quick_fight = QUICK_FIGHT_TRUE;
            break;
        }
    }
}

void vm_set_memory_value(u16 value, u16 location)
{
    int mem_type = vm_get_memory_value_type(location);

    if (mem_type == 0) {
        int cell = location - 0x4B00;

        if (cell == 0x0FD || cell == 0x0FE) {
            gbl.byte_1EE94 = true;
        } else if (cell == 0x0E6 && (int)gbl.area_ptr->in_dungeon != (int)value) {
            /* A script moving the party between the wilderness and a dungeon. */
            gbl.last_game_state = gbl.game_state;
            gbl.game_state = (value == 0) ? GAME_STATE_WILDERNESS_MAP
                                          : GAME_STATE_DUNGEON_MAP;
        }

        area1_word_set(gbl.area_ptr, 0x6A00 + (location * 2), value);
    } else if (mem_type == 1) {
        area2_word_set(gbl.area2_ptr, (location * 2) + 0x800, value);
        vm_alter_character(value, location);
    } else if (mem_type == 2) {
        ecl_vars_set(gbl.ecl_vars, (location << 1) + 0x0C00, value);
    } else if (mem_type == 3) {
        ecl_block_set(gbl.ecl_ptr, location + 0x8000, (u8)value);
    } else if (mem_type == 4) {
        /* Named cells of the DOS data segment. The two halves are the same run
         * of memory reached through different segments, which is why the second
         * one's cell numbers restart: 0xBF68 + 0xE3 is 0xC04B, the address
         * vm_get_memory_value splits its own two halves at. */
        if (location < 0xBF68) {
            switch (location) {
            case 0xFB:
            case 0xFC:
            case 0xB1:
                /* Written by the scripts and read by nothing the DOS build kept:
                 * the read side is word_1D914 and friends below, which these do
                 * not reach. The stores were already dead in the original. */
                break;

            case 0x3DE:
                gbl.word_1EE76 = value;
                break;

            case 0xB8:
                gbl.word_1EE78 = value;
                break;

            case 0xB9:
                gbl.word_1EE7A = value;
                break;
            }
        } else {
            u16 cell = (u16)(location - 0xBF68);

            switch (cell) {
            case 0xE3:
                gbl.position_changed = true;
                gbl.map_pos_x = (i8)value;
                break;

            case 0xE4:
                gbl.map_pos_y = (i8)value;
                gbl.position_changed = true;
                break;

            case 0xE5:
                /* Facing, as a script counts it: 0 to 3, a quarter turn each.
                 * Anything larger is brought back into range four at a time,
                 * which is the original's loop and always terminates. */
                while (value > 3) {
                    value = (u16)(value - 4);
                }

                gbl.map_direction = (u8)(value * 2);
                gbl.position_changed = true;
                break;

            case 0xF1:
            case 0xF7:
                gbl.byte_1EE91 = true;
                break;
            }
        }
    }
}

u16 vm_get_memory_value(u16 loc)
{
    u16 val = 0;
    int mem_type = vm_get_memory_value_type(loc);

    switch (mem_type) {
    case 0:
        val = area1_word_get(gbl.area_ptr, 0x6A00 + (loc * 2));
        break;

    case 1: {
        /* The selected character's own sheet overlays part of the party record,
         * so it gets first refusal on the address. */
        bool found = false;

        val = vm_get_player_values(&found, loc);

        if (!found) {
            val = area2_word_get(gbl.area2_ptr, (loc * 2) + 0x800);
        }
        break;
    }

    case 2:
        val = ecl_vars_get(gbl.ecl_vars, (loc << 1) + 0x0C00);
        break;

    case 3:
        /* A script reading its own bytes. The C# wondered when this happens; it
         * is how the table instructions index data laid out after the code. */
        val = ecl_block_get(gbl.ecl_ptr, loc + 0x8000);
        break;

    case 4:
        if (loc < 0xC04B) {
            switch (loc) {
            case 0x00B1:
                val = (u16)gbl.word_1D918;
                break;

            case 0x00FB:
                val = (u16)gbl.word_1D914;
                break;

            case 0x00FC:
                val = (u16)gbl.word_1D916;
                break;

            case 0x033D:
                val = gbl.map_direction;
                break;

            case 0x035F:
                /* Read by the scripts, zero in the original too. */
                break;
            }
        } else {
            u16 cell = (u16)(loc - 0xC04B);

            switch (cell) {
            case 0x00:
                val = (u16)gbl.map_pos_x;
                break;

            case 0x01:
                val = (u16)gbl.map_pos_y;
                break;

            case 0x02:
                /* Facing back in the script's quarter turns. */
                val = (u16)(gbl.map_direction / 2);
                break;

            case 0x03:
                val = gbl.map_wall_type;
                break;

            case 0x04:
                val = gbl.map_wall_roof;
                break;

            case 0x0E:
                break;
            }
        }
        break;
    }

    return val;
}

void vm_write_string_to_memory(const char *text, u16 loc)
{
    int mem_type = vm_get_memory_value_type(loc);
    int text_len;

    if (text == NULL) {
        text = "";
    }
    text_len = (int)strlen(text);

    if (mem_type == 0) {
        for (int i = 0; i < text_len; i++) {
            area1_word_set(gbl.area_ptr, 0x6A00 + ((loc + i) * 2),
                           (u8)text[i]);
        }

        area1_word_set(gbl.area_ptr, 0x6A00 + ((text_len + loc) * 2), 0);
    } else if (mem_type == 1) {
        if (loc == 0x7C00) {
            /* The first cell of the party address space is the selected
             * character's name, and writing it renames them. */
            Player *player = selected("a script setting a character's name");

            if (player != NULL) {
                snprintf(player->name, sizeof(player->name), "%s", text);
            }
        } else {
            for (int i = 0; i < text_len; i++) {
                area2_word_set(gbl.area2_ptr, ((loc + i) * 2) + 0x800,
                               (u8)text[i]);
            }

            area2_word_set(gbl.area2_ptr, ((text_len + loc) * 2) + 0x800, 0);
        }
    } else if (mem_type == 2) {
        for (int i = 0; i < text_len; i++) {
            ecl_vars_set(gbl.ecl_vars, ((i + loc) * 2) + 0x0C00, (u8)text[i]);
        }

        ecl_vars_set(gbl.ecl_vars, ((text_len + loc) * 2) + 0x0C00, 0);
    } else if (mem_type == 3) {
        for (int i = 0; i < text_len; i++) {
            ecl_block_set(gbl.ecl_ptr, 0x8000 + i + loc, (u8)text[i]);
        }

        ecl_block_set(gbl.ecl_ptr, 0x8000 + text_len + loc, 0);
    }
}

void vm_copy_string_from_memory(u16 location, int str_index)
{
    char buffer[GBL_ECL_STRING_MAX];
    int  offset = 0;
    int  type = vm_get_memory_value_type(location);

    if (type == 1 && location == 0x7C00) {
        /* The first cell of the party address space is the selected character's
         * name, and it is the name that comes out rather than any memory. */
        Player *player = selected("a script reading a character's name");

        gbl_ecl_string_set(str_index, player != NULL ? player->name : "");
        return;
    }

    /* The original walked to the terminator with nothing stopping it; a cell that
     * never holds one would have run off the end of the segment. Here the string
     * cannot outgrow the slot it is going into, and hitting that limit is worth
     * saying so. */
    while (offset < (int)sizeof(buffer) - 1) {
        u16 cell;

        switch (type) {
        case 0:
            cell = area1_word_get(gbl.area_ptr,
                                  ((offset + location) * 2) + 0x6A00);
            break;

        case 1:
            cell = area2_word_get(gbl.area2_ptr,
                                  ((offset + location) << 1) + 0x800);
            break;

        case 2:
            cell = ecl_vars_get(gbl.ecl_vars,
                                ((offset + location) << 1) + 0x0C00);
            break;

        case 3:
            cell = ecl_block_get(gbl.ecl_ptr, offset + location + 0x8000);
            break;

        default:
            /* Type 4 is the named cells, and none of them holds text. */
            cell = 0;
            break;
        }

        if (cell == 0) {
            break;
        }

        buffer[offset++] = (char)(u8)cell;
    }

    if (offset >= (int)sizeof(buffer) - 1) {
        log_warn("ecl vm: the string at 0x%04x has no terminator inside 0x%zx "
                 "cells", location, sizeof(buffer) - 1);
    }

    buffer[offset] = '\0';

    gbl_ecl_string_set(str_index, buffer);
}

/* -------------------------------------------------------------- the codec */

char vm_inflate_char(unsigned bits)
{
    if (bits <= 0x1f) {
        bits += 0x40;
    }

    return (char)bits;
}

unsigned vm_deflate_char(char ch)
{
    unsigned output = (u8)ch;

    if (output >= 0x40) {
        output -= 0x40;
    }

    return output;
}

size_t vm_compress_string(u8 *out, size_t out_size, const char *input)
{
    size_t len;
    size_t needed;
    int    state = 1;
    size_t last = 0;
    size_t curr = 0;

    if (out == NULL || input == NULL) {
        return 0;
    }

    len = strlen(input);

    /* Four characters to three bytes, plus the byte a partly filled last group
     * needs; the C# sized its array the same way. */
    needed = ((len * 3) / 4) + 1;
    if (needed > out_size) {
        log_warn("ecl vm: packing %zu characters needs %zu bytes, not %zu",
                 len, needed, out_size);
        return 0;
    }

    memset(out, 0, needed);

    for (size_t i = 0; i < len; i++) {
        unsigned bits = vm_deflate_char(input[i]) & 0x3F;

        if (state == 1) {
            out[curr] = (u8)(bits << 2);
            last = curr++;
            state = 2;
        } else if (state == 2) {
            out[last] |= (u8)(bits >> 4);
            out[curr] = (u8)(bits << 4);
            last = curr++;
            state = 3;
        } else if (state == 3) {
            out[last] |= (u8)(bits >> 2);
            out[curr] = (u8)(bits << 6);
            last = curr++;
            state = 4;
        } else {
            out[last] |= (u8)bits;
            state = 1;
        }
    }

    return curr;
}

void vm_decompress_string(char *out, size_t out_size, const u8 *data,
                          size_t length)
{
    size_t   at = 0;
    int      state = 1;
    unsigned last_byte = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (data == NULL) {
        return;
    }

    /* A packed zero is padding rather than a terminator, so it is skipped and
     * the rest of the group still comes out. */
#define VM_EMIT(bits)                                          \
    do {                                                       \
        unsigned emit_ = (bits);                               \
        if (emit_ != 0 && at + 1 < out_size) {                 \
            out[at++] = vm_inflate_char(emit_);                \
        }                                                      \
    } while (0)

    for (size_t i = 0; i < length; i++) {
        unsigned this_byte = data[i];

        switch (state) {
        case 1:
            VM_EMIT((this_byte >> 2) & 0x3F);
            state = 2;
            break;

        case 2:
            VM_EMIT(((last_byte << 4) | (this_byte >> 4)) & 0x3F);
            state = 3;
            break;

        case 3:
            VM_EMIT(((last_byte << 2) | (this_byte >> 6)) & 0x3F);
            VM_EMIT(this_byte & 0x3F);
            state = 1;
            break;
        }

        last_byte = this_byte;
    }

#undef VM_EMIT

    out[at] = '\0';
}

void vm_load_compressed_ecl_string(int str_index, int input_length)
{
    /* An operand's length byte, so 0xff is the most it can ask for. */
    u8   data[0x100];
    char text[GBL_ECL_STRING_MAX];

    if (input_length < 0 || input_length > (int)sizeof(data)) {
        log_warn("ecl vm: a packed string at 0x%04x claims %d bytes",
                 gbl.ecl_offset, input_length);
        gbl_ecl_string_set(str_index, "");
        return;
    }

    for (int i = 0; i < input_length; i++) {
        data[i] = ecl_block_get(gbl.ecl_ptr,
                                gbl.ecl_offset + 0x8000 + 1 + i);
    }

    gbl.ecl_offset = (u16)(gbl.ecl_offset + input_length);

    vm_decompress_string(text, sizeof(text), data, (size_t)input_length);
    gbl_ecl_string_set(str_index, text);
}

/* --------------------------------------------------------------- comparisons */

void vm_compare_strings(const char *string_a, const char *string_b)
{
    /* The C# used String.CompareTo, which for the capitals-and-digits alphabet
     * ECL text is limited to orders the same way strcmp does. */
    int cmp = strcmp(string_b != NULL ? string_b : "",
                     string_a != NULL ? string_a : "");

    gbl.compare_flags[0] = (cmp == 0);
    gbl.compare_flags[1] = (cmp != 0);
    gbl.compare_flags[2] = (cmp <  0);
    gbl.compare_flags[3] = (cmp >  0);
    gbl.compare_flags[4] = (cmp <= 0);
    gbl.compare_flags[5] = (cmp >= 0);
}

void vm_compare_variables(u16 a, u16 b)
{
    gbl.compare_flags[0] = (b == a);
    gbl.compare_flags[1] = (b != a);
    gbl.compare_flags[2] = (b <  a);
    gbl.compare_flags[3] = (b >  a);
    gbl.compare_flags[4] = (b <= a);
    gbl.compare_flags[5] = (b >= a);
}

/* ------------------------------------------------------ showing an encounter */

u8 vm_encounter_distance(int map_dir, int map_y, int map_x)
{
    u8 var_1 = 0;

    if (gbl.area_ptr->in_dungeon == 0) {
        /* Outdoors there are no walls to stop at. */
        var_1 = 2;
        gbl.area2_ptr->encounter_distance = 2;
    } else {
        bool blocked = false;
        u8   steps = 0;

        /* Walk the way the party is facing until a wall stops us, or until we
         * are two squares out - the furthest the dungeon view shows. */
        while (steps < 2 && !blocked) {
            if (view3d_map_wall_type(map_dir, map_y, map_x) == 0) {
                steps++;
                var_1 = steps;

                switch (map_dir) {
                case 0: map_y--; break;
                case 2: map_x++; break;
                case 4: map_y++; break;
                case 6: map_x--; break;
                }
            } else {
                blocked = true;
            }
        }
    }

    return var_1;
}

void vm_set_and_draw_head_body(u8 body_id, u8 head_id)
{
    gbl.byte_1EE8D = false;

    gbl.head_block_id = head_id;
    gbl.body_block_id = body_id;

    picture_head_body(body_id, head_id);
    picture_draw_head_and_body(true, 3, 3);
}

void vm_show_encounter_art(bool *flags, int encounter_distance, u8 pic_block_id,
                           u8 sprite_block_id)
{
    if (flags == NULL) {
        log_warn("ecl vm: an encounter asked to be drawn with no flags");
        return;
    }

    /* Until the picture is up, the panel shows the dungeon view with the
     * monster's sprite standing in it. */
    if (!flags[1]) {
        if (!flags[0]) {
            if (gbl.map_area_display) {
                /* The overhead map is in the way. */
                gbl.map_area_display = false;
                gbl.can_draw_bigpic = true;
                view3d_redraw();
            }

            if (gbl.area_ptr->in_dungeon != 0) {
                picture_load_pic_final(&gbl.pic_frames, 1, sprite_block_id,
                                       "SPRIT");
                flags[0] = true;
                gbl.display_player_sprite = true;
            }
        } else {
            /* The sprite is loaded already; put the view back under it. */
            gbl.can_draw_bigpic = true;
            view3d_redraw();
        }

        if (gbl.game_state == GAME_STATE_DUNGEON_MAP) {
            picture_show_3d_sprite(&gbl.pic_frames, encounter_distance + 1);
        }
    }

    /* Standing on top of it: the sprite gives way to the encounter's picture, or
     * to a head over a body when the script has set a head. */
    if (!flags[1] || gbl.byte_1EE96 != gbl.area2_ptr->head_block_id) {
        if (encounter_distance == 0 &&
            gbl.game_state == GAME_STATE_DUNGEON_MAP &&
            !gbl.byte_1EE95) {
            gbl.byte_1EE96 = gbl.area2_ptr->head_block_id;
            gbl.sprite_changed = true;

            if (gbl.area2_ptr->head_block_id == 0xff) {
                picture_load_pic_final(&gbl.pic_frames, 0, pic_block_id, "PIC");
                flags[1] = true;

                picture_draw_maybe_overlayed(gbl.pic_frames.frames[0].picture,
                                             true, 3, 3);
            } else {
                vm_set_and_draw_head_body(pic_block_id,
                                          gbl.area2_ptr->head_block_id);
                flags[1] = true;
                gbl.byte_1EE8D = false;
            }
        }
    }
}

/* ------------------------------------------------------------------ the menus */

/* ovr008.unk_31673 and unk_3178A: the characters that can start a menu word and
 * so can be typed to pick it. The decompiler found the same member list twice,
 * once for the lower-casing in buildMenuStrings and once for the keys sub_317AA
 * will accept. */
static bool is_menu_key_char(char ch)
{
    unsigned char c = (unsigned char)ch;

    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

void vm_build_menu_strings(char *menu_text, char *out_keys, size_t keys_size)
{
    size_t read = 0;
    size_t write = 0;
    size_t keys = 0;
    bool   mark_next = false;

    if (menu_text == NULL || out_keys == NULL || keys_size == 0) {
        return;
    }
    out_keys[0] = '\0';

    while (menu_text[read] != '\0') {
        char ch = menu_text[read++];

        /* Adding a space is how the original lower-cased, which is right for the
         * letters and turns '0'..'9' into 'P'..'Y'. See vm.h. */
        if (is_menu_key_char(ch)) {
            ch = (char)(ch + ' ');
        }

        if (ch == '~') {
            mark_next = true;
            continue;
        }

        if (mark_next) {
            mark_next = false;
            ch = (char)toupper((unsigned char)ch);

            if (keys + 1 < keys_size) {
                out_keys[keys++] = ch;
            } else {
                log_warn("ecl vm: a menu has more than %zu words",
                         keys_size - 1);
            }
        }

        /* The text only ever gets shorter - a '~' is dropped and nothing is
         * added - so write never overtakes read. */
        menu_text[write++] = ch;
    }

    menu_text[write] = '\0';
    out_keys[keys] = '\0';
}

int vm_menu_select(bool use_overlay, bool accept_return, MenuColorSet colors,
                   const char *display_string, const char *extra_string)
{
    char display[GBL_ECL_STRING_MAX];
    char menu_keys[PROMPT_HIGHLIGHT_MAX + 1];
    char key_pressed;
    int  ret_val;

    /* buildMenuStrings rewrote its argument, which in the C# left the caller's
     * string alone because strings there are immutable. Copying keeps that. */
    snprintf(display, sizeof(display), "%s",
             display_string != NULL ? display_string : "");

    vm_build_menu_strings(display, menu_keys, sizeof(menu_keys));

    do {
        bool special_key_pressed = false;

        key_pressed = prompt_display_input(&special_key_pressed, use_overlay,
                                           PROMPT_CTRL_WORD_ARROWS,
                                           colors, display, extra_string);

        if (special_key_pressed) {
            /* A cursor key: walk the party selection instead of choosing. */
            viewplayer_scroll_team_list(key_pressed);
            character_party_summary(gbl.selected_player);
            key_pressed = '\0';
        }
    } while (!is_menu_key_char(key_pressed) &&
             (key_pressed != '\r' || !accept_return));

    if (key_pressed == '\r') {
        ret_val = 0;
    } else {
        const char *at = strchr(menu_keys, key_pressed);

        /* A key that is in none of the words: the caller decides what to do. */
        ret_val = (at != NULL) ? (int)(at - menu_keys) : -1;
    }

    return ret_val;
}

int vm_vert_menu_select(int index, bool menu_redraw, bool show_exit,
                        MenuList *list, int end_y, int end_x,
                        int start_y, int start_x)
{
    MenuItem *chosen = NULL;

    prompt_select_item(&chosen, &index, &menu_redraw, show_exit, list,
                       end_y, end_x, start_y, start_x,
                       GBL_DEFAULT_MENU_COLORS, "", "");

    return index;
}

/* --------------------------------------------------------- the party and map */

/* The map is 16x16 and walking off one edge comes back on the other. */
static int decrement_wrap(int value, int max)
{
    return (value > 0) ? value - 1 : max;
}

static int increment_wrap(int value, int max)
{
    return (value < max) ? value + 1 : 0;
}

void vm_move_position_forward(void)
{
    if (gbl.map_direction == 0) {
        gbl.map_pos_y = decrement_wrap(gbl.map_pos_y, 15);
    } else if (gbl.map_direction == 2) {
        gbl.map_pos_x = increment_wrap(gbl.map_pos_x, 15);
    } else if (gbl.map_direction == 4) {
        gbl.map_pos_y = increment_wrap(gbl.map_pos_y, 15);
    } else if (gbl.map_direction == 6) {
        gbl.map_pos_x = decrement_wrap(gbl.map_pos_x, 15);
    }

    gbl.map_wall_roof = view3d_get_wall_x2(gbl.map_pos_y, gbl.map_pos_x);
    gbl.map_wall_type = view3d_map_wall_type(gbl.map_direction, gbl.map_pos_y,
                                             gbl.map_pos_x);

    gbl.position_changed = true;
}

void vm_calc_group_movement(u8 *out_min, u8 *out_max)
{
    u8 mov_max = 0;
    u8 mov_min = 0xff;

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *player = gbl.team_list[i];
        u8 movement;

        if (player == NULL) {
            continue;
        }

        movement = player->movement;

        /* Doubling wraps past 0xff exactly as the original's byte did. */
        if (player_has_affect(player, AFFECT_HASTE)) {
            movement = (u8)(movement * 2);
        } else if (player_has_affect(player, AFFECT_SLOW)) {
            movement = (u8)(movement / 2);
        }

        if (movement > mov_max) {
            mov_max = movement;
        }

        if (movement < mov_min) {
            mov_min = movement;
        }
    }

    if (out_min != NULL) {
        *out_min = mov_min;
    }
    if (out_max != NULL) {
        *out_max = mov_max;
    }
}

void vm_setup_duel(bool is_duel)
{
    Player *dueler = selected("a duel");
    Player *duel_master;

    gbl.combat_type = COMBAT_TYPE_DUEL;
    gbl.area2_ptr->is_duel = is_duel;

    if (dueler == NULL) {
        return;
    }

    /* Everyone else sits it out. Compared by name, as the original did, so a
     * character who shares a name with the dueler stays in the fight too. */
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player != NULL && strcmp(player->name, dueler->name) != 0) {
            player->in_combat = false;
        }
    }

    if (!is_duel) {
        return;
    }

    icons_chead_cbody_comspr_icon(gbl.monster_icon_id, 11, "CPIC");

    /* The opponent is the dueler over again, under another name and on the other
     * side: a mirror match. */
    duel_master = roster_clone(dueler);
    if (duel_master == NULL) {
        log_warn("ecl vm: no room for the duel opponent");
        return;
    }

    duel_master->in_combat   = true;
    snprintf(duel_master->name, sizeof(duel_master->name), "ROLF");
    duel_master->quick_fight = QUICK_FIGHT_TRUE;
    duel_master->combat_team = TEAM_ENEMY;

    duel_master->control_morale = CONTROL_NPC_BERZERK;
    duel_master->icon_id        = gbl.monster_icon_id;

    /* Nothing carries over but the pack, which is copied item for item. The
     * readied slots come across with the clone and stay valid, because the copies
     * go back in the same order and ready[] holds indices rather than pointers -
     * the C#'s cloned item references pointed into the dueler's own pack. */
    affect_list_clear(&duel_master->affects);
    duel_master->item_count = 0;

    if (!gbl_team_add(duel_master)) {
        roster_release(duel_master);
        return;
    }

    for (int i = 0; i < dueler->item_count; i++) {
        player_item_add(duel_master, &dueler->items[i]);
    }
}

void vm_rob_money(Player *player, double scale)
{
    if (player == NULL) {
        return;
    }

    money_scale_all(&player->money, scale);
}

void vm_rob_items(Player *player, int rob_chance)
{
    int i = 0;

    if (player == NULL) {
        return;
    }

    /* The C# passed a lambda to List.RemoveAll that wrote back to the captured
     * rob_chance, so a heavy item does not just resist being taken - it lowers
     * the chance for everything checked after it. Kept: the pack is walked in
     * order and the chance carries along. */
    while (i < player->item_count) {
        const Item *item = &player->items[i];

        if (item->weight > 255) {
            rob_chance = (rob_chance > 90) ? rob_chance - 90 : 0;
        } else if (item->weight > 24) {
            rob_chance = (rob_chance > 50) ? rob_chance - 50 : 0;
        }

        if (effect_roll_dice(100, 1) <= rob_chance) {
            player_item_remove(player, i);
        } else {
            i++;
        }
    }
}

void vm_damage_and_report(Player *player, int damage)
{
    char text[128];
    bool clear_text_area;

    if (player == NULL || player->health_status == STATUS_DEAD) {
        return;
    }

    /* Ten points past what is left is the threshold the original used to call it
     * outright rather than report the number. */
    if ((player->hit_point_current + 10) < damage) {
        snprintf(text, sizeof(text), "  %s dies. ", player->name);
    } else {
        snprintf(text, sizeof(text), "  %s is hit FOR %d points of Damage.",
                 player->name, damage);
    }

    if (gbl.text_y_col > 0x16) {
        /* The text area is full: pause, then start again at the top. */
        gbl.text_y_col = 0x11;
        clear_text_area = true;
        text_display_and_pause("press <enter>/<return> to continue", 15);
    } else {
        clear_text_area = false;
    }

    gbl.text_x_col = 0x26;

    text_press_any_key(text, clear_text_area, 15, 0x16, 0x26, 17, 1);

    character_damage(damage, player);
    frames_clear_area(0x0f, 0x26, 1, 0x11);

    character_party_summary(player);
}
