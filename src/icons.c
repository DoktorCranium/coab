/* icons.c - Ported from engine/ovr034.cs. */
#include <stdio.h>
#include <string.h>

#include "icons.h"

#include "draw.h"
#include "fileio.h"
#include "gbl.h"
#include "input.h"
#include "log.h"
#include "quit.h"

/* seg600:0B20 unk_16E30 - the palette as it is, the "old colours" both recolour
 * calls below map from. */
static const u8 unk_16E30[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
/* seg600:0B30 unk_16E40 - also unchanged, so a monster sprite's recolour is a
 * no-op. It is still done, because Recolor also walks the picture. */
static const u8 unk_16E40[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
/* seg600:0B40 unk_16E50 - entry 13 becomes 0: ICON's brown is dropped to black. */
static const u8 unk_16E50[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 14, 15
};

void icons_load_24x24_set(int cell_count, int dest_cell_offset, int block_id,
                          const char *file_name)
{
    DaxBlock *tmp_block;
    size_t data_length, dest_byte_offset;

    if (dest_cell_offset > GBL_24X24_CELLS) {
        game_log_and_exit("Start range error in Load24x24Set. %d",
                          dest_cell_offset);
    }
    if (dest_cell_offset < 0 || cell_count < 0) {
        log_warn("Load24x24Set: %d cells at %d makes no sense",
                 cell_count, dest_cell_offset);
        return;
    }

    tmp_block = draw_load_dax(0, 0, block_id, file_name);
    if (tmp_block == NULL) {
        log_warn("Load24x24Set: %s.dax has no block %d", file_name, block_id);
        return;
    }

    data_length      = (size_t)cell_count * (size_t)tmp_block->bpp;
    dest_byte_offset = (size_t)dest_cell_offset * (size_t)tmp_block->bpp;

    if (gbl.dax_24x24_set != NULL) {
        /* Array.Copy threw when it did not fit. The tile bank is a fixed 0x30
         * cells, so a request for more than that means the caller's cell count
         * and the block's geometry disagree; copy what does fit and say so. */
        if (data_length > tmp_block->data_size) {
            log_warn("Load24x24Set: %s.dax block %d holds %zu bytes, not the "
                     "%zu asked for", file_name, block_id,
                     tmp_block->data_size, data_length);
            data_length = tmp_block->data_size;
        }
        if (dest_byte_offset > gbl.dax_24x24_set->data_size) {
            data_length = 0;
        } else if (dest_byte_offset + data_length >
                   gbl.dax_24x24_set->data_size) {
            log_warn("Load24x24Set: %zu bytes at %zu overruns the %zu byte "
                     "tile bank", data_length, dest_byte_offset,
                     gbl.dax_24x24_set->data_size);
            data_length = gbl.dax_24x24_set->data_size - dest_byte_offset;
        }

        memcpy(gbl.dax_24x24_set->data + dest_byte_offset, tmp_block->data,
               data_length);
    }

    dax_block_free(tmp_block);

    input_clear_keyboard();
}

void icons_draw_iso_tile(int tile_index, int row_y, int col_x)
{
    if (tile_index > 0x7f) {
        /* The high half of the tile ids belongs to gbl.dword_1C8FC, which the
         * game never loads: these draw nothing. */
        draw_overlay_unbounded(gbl.dword_1C8FC, tile_index, tile_index & 0x7f,
                               row_y, col_x);
    } else {
        draw_overlay_unbounded(gbl.dax_24x24_set, 0, tile_index, row_y, col_x);
    }
}

void icons_release_combat_icon(int index)
{
    if (index < 0 || index >= GBL_COMBAT_ICON_COUNT) {
        log_warn("free_icon: no combat icon %d", index);
        return;
    }
    combat_icon_release(&gbl.combat_icons[index]);
}

void icons_chead_cbody_comspr_icon(u8 combat_icon_index, int block_id,
                                   const char *file_text)
{
    /* Long enough for the longest name the callers build - "CHEADT" and
     * "COMSPR" plus a chapter digit. */
    char name[32];
    char prefix[6 + 1];
    CombatIcon *icon;

    if (combat_icon_index >= GBL_COMBAT_ICON_COUNT) {
        log_warn("chead_cbody_comspr_icon: no combat icon %d",
                 combat_icon_index);
        return;
    }
    icon = &gbl.combat_icons[combat_icon_index];

    snprintf(name, sizeof(name), "%s", file_text);

    file_copy_string(prefix, sizeof(prefix), 5, 0, name);

    if (strcmp(prefix, "CHEAD") == 0 || strcmp(prefix, "CBODY") == 0) {
        size_t len = strlen(name);
        char last = len > 0 ? name[len - 1] : '\0';

        if (last >= 'a' && last <= 'z') {
            last = (char)(last - 'a' + 'A');
        }
        /* CHEADT and CBODYT hold the second bank of heads and bodies. */
        if (last == 'T') {
            block_id += 0x40;
        }
        /* The trailing letter is not part of the file name. */
        if (len > 0) {
            name[len - 1] = '\0';
        }

        combat_icon_load(icon, 0, 1, name, block_id, block_id + 0x80);
    } else if (strcmp(name, "COMSPR") == 0 || strcmp(name, "ICON") == 0) {
        bool is_icon = strcmp(name, "ICON") == 0;

        combat_icon_load(icon, 0, 1, name, block_id, block_id + 0x80);

        if (is_icon) {
            combat_icon_recolor(icon, false, unk_16E50, unk_16E30);
        }
    } else {
        /* Monster sprites are per chapter: MONSTR becomes MONSTR1 and so on. */
        char numbered[sizeof(name) + 4];

        snprintf(numbered, sizeof(numbered), "%s%u", name,
                 (unsigned)gbl.game_area);

        combat_icon_load(icon, 0, 1, numbered, block_id, block_id + 0x80);
        combat_icon_recolor(icon, false, unk_16E40, unk_16E30);
    }

    input_clear_keyboard();
}

void icons_draw_combat_icon(int icon_index, CombatIconState icon_state,
                            int direction, int tile_y, int tile_x)
{
    const DaxBlock *icon;

    if (icon_index < 0 || icon_index >= GBL_COMBAT_ICON_COUNT) {
        log_warn("draw_combat_icon: no combat icon %d", icon_index);
        return;
    }

    icon = combat_icon_get(&gbl.combat_icons[icon_index], icon_state, direction);

    if (icon != NULL) {
        draw_combat_picture(icon, (tile_y * 3) + 1, (tile_x * 3) + 1, 0);
    }
}
