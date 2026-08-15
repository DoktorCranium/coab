/* picture.h - the pictures a text screen shows: the animated corner picture,
 * the talking-head portraits and the full-screen backdrops.
 * Ported from engine/ovr030.cs.
 *
 * The corner picture is an animation of up to eight frames packed into a single
 * DAX block, which is why loading it is not just draw_load_dax: the block holds
 * its own little header per frame, and in PIC and FINAL every frame after the
 * first is stored XORed against the first one.
 *
 * Nothing here draws by itself over time. The frames are advanced by the prompt
 * loop in prompt.c while it waits for a key, exactly as the original did.
 */
#ifndef COAB_PICTURE_H
#define COAB_PICTURE_H

#include "coab.h"
#include "dax.h"

/* ovr030.DrawMaybeOverlayed, sub_7000A. Draws through the overlay buffer - one
 * cell up and left, then blitted - when the area is fading or the caller asks
 * for it, and straight to the screen otherwise. A fading area also has its
 * colours pushed towards dark grey first, which is the fade itself. */
void picture_draw_maybe_overlayed(DaxBlock *block, bool use_overlay,
                                 int row_y, int col_x);

/* ovr030.load_pic_final. Loads block_id of "<file_name><game_area>.dax" into
 * dax_array as an animation. file_name is the bare basename ("PIC", "FINAL",
 * "SPRITE", ...); the chapter number and extension are appended here.
 *
 * Does nothing when the same file and block are already loaded, which is how the
 * engine avoids reloading a picture on every prompt. block_id 0xff means "no
 * picture" and is also ignored. */
void picture_load_pic_final(DaxArray *dax_array, u8 masked, u8 block_id,
                            const char *file_name);

/* ovr030.DaxArrayFreeDaxBlocks. Also clears gbl.last_dax_file /
 * last_dax_block_id, so the next load always reloads. */
void picture_dax_array_free_blocks(DaxArray *animation);

/* ovr030.head_body. Loads a portrait head and body out of HEAD<area>.DAX and
 * BODY<area>.DAX; 0xff for either leaves the loaded one alone. */
void picture_head_body(u8 body_id, u8 head_id);

/* ovr030.draw_head_and_body, sub_706DC. The body goes five rows below the head. */
void picture_draw_head_and_body(bool draw_body, int row_y, int col_x);

/* ovr030.Show3DSprite. sprite_index is 1..3 - the three distances a wilderness
 * or dungeon sprite can be at - and the frame carries its own screen position. */
void picture_show_3d_sprite(const DaxArray *animation, int sprite_index);

/* ovr030.load_bigpic. Frees the corner animation, then loads block_id of
 * BIGPIC<area>.DAX unless it is already the loaded one. */
void picture_load_bigpic(u8 block_id);

/* ovr030.draw_bigpic, sub_7087A. Draws the wilderness frame and the backdrop
 * inside it. */
void picture_draw_bigpic(void);

#endif /* COAB_PICTURE_H */
