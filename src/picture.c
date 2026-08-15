/* picture.c - Ported from engine/ovr030.cs. */
#include "picture.h"

#include "draw.h"
#include "gbl.h"
#include "frames.h"
#include "input.h"
#include "log.h"
#include "prompt.h"
#include "quit.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The area fade: everything that is not one of the four mid greys or brown goes
 * to colour 12, bright red on the EGA palette the game sets up. */
static const u8 fade_old_colors[EGA_COLORS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static const u8 fade_new_colors[EGA_COLORS] = {
    12, 12, 12, 12, 4, 5, 6, 7, 12, 12, 10, 12, 12, 12, 14, 12
};

/* A masked animation frame has colour 13 turned into colour 0, so that the one
 * colour the artists used for "nothing here" reads as black. */
static const u8 transparent_old_colors[EGA_COLORS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static const u8 transparent_new_colors[EGA_COLORS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 14, 15
};

/* Bytes of per-frame header inside an animation block: 4 delay, 2 height,
 * 2 width, 2 x_pos, 2 y_pos plus one pad byte, then 8 of field_9. */
#define PIC_FRAME_HEADER_SIZE (4 + 2 + 2 + 2 + 3 + 8)

/* sub_7000A */
void picture_draw_maybe_overlayed(DaxBlock *block, bool use_overlay,
                                  int row_y, int col_x)
{
    i16 picture_fade = (gbl.area_ptr != NULL) ? gbl.area_ptr->picture_fade : 0;

    if (block == NULL) {
        return;
    }

    if (picture_fade > 0 || use_overlay) {
        if (picture_fade > 0) {
            /* Recolor with use_random set replaces roughly one matching pixel in
             * four, so calling this on successive frames dissolves the picture
             * rather than flipping it in one step. */
            dax_block_recolor(block, true, fade_new_colors, fade_old_colors);
        }

        draw_overlay_bounded(block, 0, 0, row_y - 1, col_x - 1);
        /* seg040.DrawOverlay() went here. It is empty - the overlay is drawn
         * straight into video memory - so there is nothing to call. */
    } else {
        draw_picture(block, row_y, col_x, 0);
    }
}

void picture_dax_array_free_blocks(DaxArray *animation)
{
    if (animation == NULL) {
        return;
    }

    /* The C# dropped its references and let the GC collect the pictures. Here
     * every frame is freed, including any past num_frames: a load that ran out
     * of data part way through leaves num_frames short of what it allocated. */
    for (int index = 0; index < DAX_ARRAY_FRAMES; index++) {
        dax_block_free(animation->frames[index].picture);
        animation->frames[index].picture = NULL;
        animation->frames[index].delay = 0;
    }

    animation->num_frames = 0;
    animation->cur_frame = 0;

    gbl.last_dax_file[0] = '\0';
    gbl.last_dax_block_id = 0xff;
}

void picture_load_pic_final(DaxArray *dax_array, u8 masked, u8 block_id,
                            const char *file_name)
{
    char full_name[GBL_DAX_NAME_MAX + 16];
    bool is_pic_or_final;
    u8 *data;
    i16 data_size = 0;
    size_t src_offset = 0;
    size_t size;
    int frames_count = 0;
    int frame_total;
    u8 *first_frame_ega_layout = NULL;
    int first_frame_ega_size = 0;

    if (dax_array == NULL || file_name == NULL) {
        return;
    }

    if (strcmp(file_name, gbl.last_dax_file) == 0 &&
        block_id == gbl.last_dax_block_id) {
        return;
    }
    if (block_id == 0xff) {
        return;
    }

    if (gbl.animations_on) {
        prompt_clear_area_no_update();
        text_display_string("Loading...Please Wait", 0, 10, 0x18, 0);
    }

    picture_dax_array_free_blocks(dax_array);

    snprintf(gbl.last_dax_file, sizeof(gbl.last_dax_file), "%s", file_name);
    gbl.last_dax_block_id = block_id;

    /* Only these two are stored as differences against their first frame. */
    is_pic_or_final = (strcmp(file_name, "PIC") == 0 ||
                       strcmp(file_name, "FINAL") == 0);

    snprintf(full_name, sizeof(full_name), "%s%u.dax", file_name,
             (unsigned)gbl.game_area);

    data = dax_load_decode(full_name, block_id, &data_size);

    if (data == NULL || data_size <= 0) {
        free(data);
        text_display_and_pause("PIC not found", 14);
        return;
    }
    size = (size_t)data_size;

    frame_total = data[src_offset];
    src_offset++;
    dax_array->cur_frame = 1;

    if (!gbl.animations_on && is_pic_or_final) {
        /* With animation off only the still first frame is wanted. */
        frame_total = 1;
    }

    if (frame_total > DAX_ARRAY_FRAMES) {
        /* The C# indexed an eight-entry array and would have thrown. Nothing in
         * the shipped data has more than eight frames, so a block that does is
         * either a different game's data or a bad block. */
        log_warn("picture: %s block %d claims %d frames, keeping %d",
                 full_name, block_id, frame_total, DAX_ARRAY_FRAMES);
        frame_total = DAX_ARRAY_FRAMES;
    }

    for (int frame = 0; frame < frame_total; frame++) {
        DaxBlock *block;
        int height, width, ega_encoded_size;

        if (src_offset + PIC_FRAME_HEADER_SIZE > size) {
            log_warn("picture: %s block %d ends inside frame %d's header",
                     full_name, block_id, frame);
            break;
        }

        dax_array->frames[frame].delay = sys_array_to_int(data, (int)src_offset);
        src_offset += 4;

        height = sys_array_to_short(data, (int)src_offset);
        src_offset += 2;

        width = sys_array_to_short(data, (int)src_offset);
        src_offset += 2;

        if (height <= 0 || width <= 0 || height > 4096 || width > 4096) {
            log_warn("picture: %s block %d frame %d has bad geometry %dx%d",
                     full_name, block_id, frame, width, height);
            break;
        }

        block = dax_block_new(masked, 1, width, height);
        if (block == NULL) {
            break;
        }
        dax_array->frames[frame].picture = block;
        frames_count++;

        block->x_pos = sys_array_to_short(data, (int)src_offset);
        src_offset += 2;

        block->y_pos = sys_array_to_short(data, (int)src_offset);
        src_offset += 3;   /* the y position is followed by one unused byte */

        memcpy(block->field_9, data + src_offset, 8);
        src_offset += 8;

        /* Two pixels per byte, less one: the count is a Turbo Pascal "last index"
         * rather than a length, which is why every use of it adds one back. */
        ega_encoded_size = (block->bpp / 2) - 1;

        if (src_offset + (size_t)ega_encoded_size + 1 > size) {
            log_warn("picture: %s block %d frame %d wants %d bytes but only "
                     "%zu remain", full_name, block_id, frame,
                     ega_encoded_size + 1, size - src_offset);
            break;
        }

        if (is_pic_or_final) {
            if (frame == 0) {
                first_frame_ega_size = ega_encoded_size + 1;
                first_frame_ega_layout = malloc((size_t)first_frame_ega_size);
                if (first_frame_ega_layout != NULL) {
                    memcpy(first_frame_ega_layout, data + src_offset,
                           (size_t)first_frame_ega_size);
                }
            } else if (first_frame_ega_layout != NULL) {
                /* Note the bound: the original XORs one byte fewer than it
                 * copied, so the last byte of a later frame is stored plain. */
                int count = COAB_MIN(ega_encoded_size, first_frame_ega_size);

                for (int i = 0; i < count; i++) {
                    data[src_offset + (size_t)i] ^= first_frame_ega_layout[i];
                }
            }
        }

        dax_block_decode_pixels(block, 0, masked, data, size, src_offset);

        if ((masked & 1) > 0) {
            dax_block_recolor(block, false, transparent_new_colors,
                              transparent_old_colors);
        }

        src_offset += (size_t)ega_encoded_size + 1;
    }

    dax_array->num_frames = frames_count;

    free(first_frame_ega_layout);
    free(data);

    input_clear_keyboard();

    if (gbl.animations_on) {
        prompt_clear_area_no_update();
    }
}

void picture_head_body(u8 body_id, u8 head_id)
{
    char file_name[GBL_DAX_NAME_MAX];

    if (head_id != 0xff &&
        (gbl.current_head_id == 0xff || gbl.current_head_id != head_id)) {
        snprintf(file_name, sizeof(file_name), "HEAD%u", (unsigned)gbl.game_area);

        /* The C# overwrote the reference and left the old picture to the GC. */
        dax_block_free(gbl.head_dax);
        gbl.head_dax = draw_load_dax(0, 0, head_id, file_name);

        if (gbl.head_dax == NULL) {
            text_display_and_pause("head not found", 14);
        }

        gbl.current_head_id = head_id;
    }

    if (body_id != 0xff &&
        (gbl.current_body_id == 0xff || gbl.current_body_id != body_id)) {
        snprintf(file_name, sizeof(file_name), "BODY%u", (unsigned)gbl.game_area);

        dax_block_free(gbl.body_dax);
        gbl.body_dax = draw_load_dax(0, 0, body_id, file_name);

        if (gbl.body_dax == NULL) {
            text_display_and_pause("body not found", 14);
        }

        gbl.current_body_id = body_id;
    }

    input_clear_keyboard();
}

/* sub_706DC */
void picture_draw_head_and_body(bool draw_body, int row_y, int col_x)
{
    picture_draw_maybe_overlayed(gbl.head_dax, false, row_y, col_x);

    if (draw_body) {
        picture_draw_maybe_overlayed(gbl.body_dax, false, row_y + 5, col_x);
    }
}

void picture_show_3d_sprite(const DaxArray *animation, int sprite_index)
{
    DaxBlock *block;

    if (animation == NULL) {
        return;
    }

    if (sprite_index < 1 || sprite_index > 3) {
        game_log_and_exit("Illegal range in Show3DSprite. %d", sprite_index);
    }

    block = animation->frames[sprite_index - 1].picture;

    if (block != NULL) {
        /* The frame carries where it goes: x_pos and y_pos are cell positions
         * within the 3d view, and the +3 puts them inside its frame. */
        draw_overlay_bounded(block, 1, 0, block->y_pos + 3 - 1,
                             block->x_pos + 3 - 1);
        /* seg040.DrawOverlay() went here; it does nothing. */
    }
}

void picture_load_bigpic(u8 block_id)
{
    char file_name[GBL_DAX_NAME_MAX];

    picture_dax_array_free_blocks(&gbl.pic_frames);

    if (gbl.bigpic_block_id != block_id) {
        snprintf(file_name, sizeof(file_name), "bigpic%u",
                 (unsigned)gbl.game_area);

        dax_block_free(gbl.bigpic_dax);
        gbl.bigpic_dax = draw_load_dax(0, 0, block_id, file_name);
        gbl.bigpic_block_id = block_id;
    }
}

/* sub_7087A */
void picture_draw_bigpic(void)
{
    frames_draw_wilderness_map();
    draw_picture(gbl.bigpic_dax, 1, 1, 0);
}
