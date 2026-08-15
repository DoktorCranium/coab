#include "gbl.h"

#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "log.h"
#include "roster.h"

Gbl gbl;

const i16 GBL_SYMBOL_SET_FIX[GBL_SYMBOL_SETS] = {
    0x0001, 0x002E, 0x0074, 0x00BA, 0x0100
};

const MenuColorSet GBL_DEFAULT_MENU_COLORS = { 15, 10, 13 };
const MenuColorSet GBL_ALERT_MENU_COLORS   = { 15, 10, 14 };

const u8 GBL_DEFAULT_ICON_COLOURS[GBL_ICON_COLOUR_COUNT] = { 1, 2, 3, 4, 6, 7 };

const i8 GBL_MAP_DIR_X_DELTA[9] = {  0,  1, 1, 1, 0, -1, -1, -1, 0 };
const i8 GBL_MAP_DIR_Y_DELTA[9] = { -1, -1, 0, 1, 1,  1,  0, -1, 0 };

const u8 GBL_MAX_CLASS_HIT_DICE[SKILL_COUNT] = {
    10, 15, 10, 10, 11, 12, 11, 13
};

Point gbl_map_direction_delta(int direction)
{
    if (direction < 0 || direction >= (int)COAB_ARRAY_LEN(GBL_MAP_DIR_X_DELTA)) {
        log_warn("gbl: no map direction %d", direction);
        return point_make(0, 0);
    }

    return point_make(GBL_MAP_DIR_X_DELTA[direction],
                      GBL_MAP_DIR_Y_DELTA[direction]);
}

/* engine/seg001.cs: InitFirst, restricted to the fields declared so far. */
void gbl_init(void)
{
    memset(&gbl, 0, sizeof(gbl));

    gbl.sound_type     = SOUND_TYPE_NONE;
    gbl.pics_on        = true;
    gbl.animations_on  = true;
    gbl.game_speed_var = 4;
    gbl.game_area      = 1;
    gbl.game_area_backup = 1;
    gbl.delay_between_characters = true;

    for (int i = 0; i < GBL_SYMBOL_SETS; i++) {
        gbl.symbol_8x8_set[i] = NULL;
    }

    gbl.cursor_bkup = dax_block_new(0, 1, 1, 8);
    gbl.cursor      = dax_block_new(0, 1, 1, 8);

    if (gbl.cursor) {
        /* seg051.FillChar(0xf, cursor.bpp, cursor.data): only the first frame's
         * worth, which for this one-frame picture is all of it. */
        file_fill_char(0x0f, (size_t)gbl.cursor->bpp, gbl.cursor->data);
    }

    gbl.dax_24x24_set = dax_block_new(0, GBL_24X24_CELLS, GBL_24X24_WIDTH,
                                      GBL_24X24_HEIGHT);
    gbl.dword_1C8FC   = NULL;

    for (int i = 0; i < GBL_COMBAT_ICON_COUNT; i++) {
        combat_icon_init(&gbl.combat_icons[i]);
    }

    /* The C# allocated these with `new` and let the GC deal with them; here they
     * are freed by gbl_free. Nothing outlives the process either way. */
    gbl.area_ptr  = calloc(1, sizeof(*gbl.area_ptr));
    gbl.area2_ptr = calloc(1, sizeof(*gbl.area2_ptr));

    if (gbl.area_ptr == NULL || gbl.area2_ptr == NULL) {
        log_error("out of memory allocating the area records");
    } else {
        area1_clear(gbl.area_ptr);
        gbl.area_ptr->in_dungeon        = 1;
        gbl.area_ptr->last_ecl_block_id = 0;

        area2_clear(gbl.area2_ptr);
        gbl.area2_ptr->party_size = 0;
    }

    /* 0xff means "nothing loaded", so head_body always loads the first time. */
    gbl.current_head_id = 0xff;
    gbl.current_body_id = 0xff;
    gbl.head_dax        = NULL;
    gbl.body_dax        = NULL;

    dax_array_init(&gbl.pic_frames);
    gbl.bigpic_dax        = NULL;
    gbl.bigpic_block_id   = 0xff;
    gbl.last_dax_file[0]  = '\0';
    gbl.last_dax_block_id = 0xff;
    gbl.saved_dax_file[0]  = '\0';
    gbl.saved_dax_block_id = 0xff;

    gbl.menu_screen_index  = 1;
    gbl.menu_selected_word = 1;

    gbl.game_state      = GAME_STATE_DUNGEON_MAP;
    gbl.last_game_state = GAME_STATE_START_GAME_MENU;

    /* Where the party stands when a new game starts: Phlan's slums, facing
     * north. The map is 16x16 and y counts downwards. */
    gbl.map_pos_x     = 7;
    gbl.map_pos_y     = 0x0d;
    gbl.map_direction = 0;
    gbl.map_area_display = false;
    gbl.party_killed  = false;

    /* The party gets a go at whatever door is in the square they start in. */
    gbl.can_bash_door  = true;
    gbl.can_pick_door  = true;
    gbl.can_knock_door = true;

    gbl.geo_ptr = calloc(1, sizeof(*gbl.geo_ptr));
    if (gbl.geo_ptr == NULL) {
        log_error("out of memory allocating the 3d map");
    }

    /* engine/seg001.cs InitFirst: new Struct_1B2CA() and new EclBlock(). */
    gbl.ecl_ptr  = calloc(1, sizeof(*gbl.ecl_ptr));
    gbl.ecl_vars = calloc(1, sizeof(*gbl.ecl_vars));
    if (gbl.ecl_ptr == NULL || gbl.ecl_vars == NULL) {
        log_error("out of memory allocating the ecl script blocks");
    }
    for (int i = 0; i < ECL_CMD_OPS_LIMIT; i++) {
        ecl_op_clear(&gbl.cmd_opps[i]);
    }
    gbl_ecl_strings_clear();
    gbl_vm_call_stack_clear();
    wall_defs_clear(&gbl.wall_def);

    /* Set 1 starts out holding WALLDEF block 0; the other two are empty. */
    gbl.set_blocks[0].set_id   = 1;
    gbl.set_blocks[0].block_id = 0;
    for (int i = 1; i < GBL_SET_BLOCKS; i++) {
        gbl.set_blocks[i].set_id   = -1;
        gbl.set_blocks[i].block_id = -1;
    }

    gbl.sky_dax_250 = NULL;
    gbl.sky_dax_251 = NULL;
    gbl.sky_dax_252 = NULL;

    /* engine/seg001.cs InitFirst: "God damm 1-n arrays" - every CombatMap entry
     * exists from the start, all of them empty, and index 0 stays that way.
     * The memset above has already zeroed them. */
    gbl.combatant_count = 0;
    for (int i = 0; i <= GBL_MAX_COMBATANT_COUNT; i++) {
        gbl.combat_map[i].size = 0;
    }
    gbl.downed_player_count = 0;

    /* engine/ovr011.cs allocates the ground tile map when a fight starts and
     * engine/ovr009.cs drops it when one ends, so outside combat there is none.
     * The combat code checks for that, and so must the port. */
    gbl.map_to_background_tile = NULL;

    gbl.focus_combat_area_on_player = true;

    /* No party yet, and nobody selected: the start-game menu builds both. The
     * records they will come out of are handed back here too, since a second
     * gbl_init is a new game and the old party is gone with it. */
    roster_clear();
    gbl.team_count           = 0;
    gbl.selected_player      = NULL;
    gbl.last_selected_player = NULL;
    gbl.missile_dax          = NULL;
}

int gbl_team_index_of(const Player *player)
{
    if (player == NULL) {
        return -1;
    }

    for (int i = 0; i < gbl.team_count; i++) {
        if (gbl.team_list[i] == player) {
            return i;
        }
    }

    return -1;
}

bool gbl_team_add(Player *player)
{
    if (player == NULL) {
        return false;
    }
    if (gbl.team_count >= GBL_TEAM_LIST_MAX) {
        log_warn("the team list is full; %s cannot join", player->name);
        return false;
    }

    gbl.team_list[gbl.team_count++] = player;

    return true;
}

bool gbl_team_insert(int at, Player *player)
{
    if (player == NULL) {
        return false;
    }
    if (at < 0 || at > gbl.team_count) {
        log_warn("team list insert at %d, with %d characters on the list",
                 at, gbl.team_count);
        return false;
    }
    if (gbl.team_count >= GBL_TEAM_LIST_MAX) {
        log_warn("the team list is full; %s cannot join", player->name);
        return false;
    }

    for (int i = gbl.team_count; i > at; i--) {
        gbl.team_list[i] = gbl.team_list[i - 1];
    }

    gbl.team_list[at] = player;
    gbl.team_count++;

    return true;
}

void gbl_team_remove_at(int index)
{
    if (index < 0 || index >= gbl.team_count) {
        log_warn("team list index %d is outside the %d characters on it",
                 index, gbl.team_count);
        return;
    }

    for (int i = index; i + 1 < gbl.team_count; i++) {
        gbl.team_list[i] = gbl.team_list[i + 1];
    }

    gbl.team_count--;
    gbl.team_list[gbl.team_count] = NULL;
}

const char *gbl_ecl_string(int index)
{
    if (index < 0 || index >= GBL_ECL_STRINGS) {
        log_warn("gbl: no ecl string %d; the instruction carries %d",
                 index, GBL_ECL_STRINGS - 1);
        return "";
    }

    return gbl.ecl_strings[index];
}

void gbl_ecl_string_set(int index, const char *text)
{
    size_t len;

    if (index < 0 || index >= GBL_ECL_STRINGS) {
        log_warn("gbl: cannot set ecl string %d; the instruction carries %d",
                 index, GBL_ECL_STRINGS - 1);
        return;
    }
    if (text == NULL) {
        text = "";
    }

    len = strlen(text);
    if (len >= GBL_ECL_STRING_MAX) {
        log_warn("gbl: ecl string %d is %zu bytes; keeping the first 0x%x",
                 index, len, GBL_ECL_STRING_MAX - 1);
        len = GBL_ECL_STRING_MAX - 1;
    }

    memcpy(gbl.ecl_strings[index], text, len);
    gbl.ecl_strings[index][len] = '\0';
}

void gbl_ecl_strings_clear(void)
{
    for (int i = 0; i < GBL_ECL_STRINGS; i++) {
        gbl.ecl_strings[i][0] = '\0';
    }
}

bool gbl_vm_call_push(u16 address)
{
    if (gbl.vm_call_depth >= GBL_VM_CALL_STACK_MAX) {
        log_warn("gbl: the ecl call stack is %d deep; gosub to 0x%04x is lost",
                 GBL_VM_CALL_STACK_MAX, address);
        return false;
    }

    gbl.vm_call_stack[gbl.vm_call_depth++] = address;

    return true;
}

bool gbl_vm_call_pop(u16 *out_address)
{
    if (gbl.vm_call_depth <= 0) {
        log_warn("gbl: the ecl script returned with nothing on the call stack");
        return false;
    }

    *out_address = gbl.vm_call_stack[--gbl.vm_call_depth];

    return true;
}

void gbl_vm_call_stack_clear(void)
{
    gbl.vm_call_depth = 0;
}

void gbl_ground_items_clear(void)
{
    gbl.ground_item_count = 0;
}

Item *gbl_ground_item_add(const Item *item)
{
    Item *slot;

    if (item == NULL) {
        return NULL;
    }
    if (gbl.ground_item_count >= GBL_GROUND_ITEMS_MAX) {
        log_warn("the ground already holds %d items; dropping one more",
                 GBL_GROUND_ITEMS_MAX);
        return NULL;
    }

    slot  = &gbl.ground_items[gbl.ground_item_count++];
    *slot = *item;

    return slot;
}

void gbl_spell_targets_clear(void)
{
    gbl.spell_target_count = 0;
}

bool gbl_spell_target_add(Player *player)
{
    if (gbl.spell_target_count >= GBL_SPELL_TARGETS_MAX) {
        log_warn("gbl: a spell is already aimed at %d combatants",
                 GBL_SPELL_TARGETS_MAX);
        return false;
    }

    gbl.spell_targets[gbl.spell_target_count++] = player;

    return true;
}

bool gbl_spell_target_exists(const Player *player)
{
    for (int i = 0; i < gbl.spell_target_count; i++) {
        if (gbl.spell_targets[i] == player) {
            return true;
        }
    }

    return false;
}

Item *gbl_ground_item_at(int index)
{
    if (index < 0 || index >= gbl.ground_item_count) {
        return NULL;
    }

    return &gbl.ground_items[index];
}

void gbl_ground_item_remove_at(int index)
{
    if (index < 0 || index >= gbl.ground_item_count) {
        log_warn("gbl: no ground item %d of %d", index, gbl.ground_item_count);
        return;
    }

    memmove(&gbl.ground_items[index], &gbl.ground_items[index + 1],
            (size_t)(gbl.ground_item_count - index - 1) *
            sizeof(gbl.ground_items[0]));
    gbl.ground_item_count--;
}

void gbl_free(void)
{
    for (int i = 0; i < GBL_SYMBOL_SETS; i++) {
        dax_block_free(gbl.symbol_8x8_set[i]);
        gbl.symbol_8x8_set[i] = NULL;
    }
    dax_block_free(gbl.cursor_bkup);
    gbl.cursor_bkup = NULL;
    dax_block_free(gbl.cursor);
    gbl.cursor = NULL;

    dax_block_free(gbl.dax_24x24_set);
    gbl.dax_24x24_set = NULL;
    dax_block_free(gbl.dword_1C8FC);
    gbl.dword_1C8FC = NULL;

    for (int i = 0; i < GBL_COMBAT_ICON_COUNT; i++) {
        combat_icon_release(&gbl.combat_icons[i]);
    }

    /* ovr030 owns these while the game runs; at shutdown they are just memory.
     * The frames are freed here rather than through picture_dax_array_free()
     * so that gbl.c keeps to the data it declares. */
    for (int i = 0; i < DAX_ARRAY_FRAMES; i++) {
        dax_block_free(gbl.pic_frames.frames[i].picture);
        gbl.pic_frames.frames[i].picture = NULL;
    }
    gbl.pic_frames.num_frames = 0;
    gbl.pic_frames.cur_frame  = 0;

    dax_block_free(gbl.bigpic_dax);
    gbl.bigpic_dax = NULL;
    dax_block_free(gbl.head_dax);
    gbl.head_dax = NULL;
    dax_block_free(gbl.body_dax);
    gbl.body_dax = NULL;

    dax_block_free(gbl.sky_dax_250);
    gbl.sky_dax_250 = NULL;
    dax_block_free(gbl.sky_dax_251);
    gbl.sky_dax_251 = NULL;
    dax_block_free(gbl.sky_dax_252);
    gbl.sky_dax_252 = NULL;

    free(gbl.area_ptr);
    gbl.area_ptr = NULL;
    free(gbl.area2_ptr);
    gbl.area2_ptr = NULL;
    free(gbl.geo_ptr);
    gbl.geo_ptr = NULL;
    free(gbl.ecl_ptr);
    gbl.ecl_ptr = NULL;
    free(gbl.ecl_vars);
    gbl.ecl_vars = NULL;
    /* Not freed: the ground map belongs to battlesetup.c, which hands out the
     * same static one to every fight. Quitting in the middle of a battle leaves
     * this pointing at it, and freeing that is not ours to do. */
    gbl.map_to_background_tile = NULL;

    dax_block_free(gbl.missile_dax);
    gbl.missile_dax = NULL;
}
