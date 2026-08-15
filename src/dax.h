/* dax.h - DAX archive reading and picture blocks.
 * Ported from Classes/DaxFiles/{DaxCache,DaxFileCache,DaxBlock,DaxArray}.cs.
 *
 * A .DAX file is a header table followed by RLE-compressed blocks:
 *
 *   u16 header_bytes                     (data starts at header_bytes + 2)
 *   repeat header_bytes/9 times:
 *       u8  id                           block number the engine asks for
 *       u32 offset                       relative to the start of the data area
 *       i16 raw_size                     decompressed size
 *       u16 comp_size                    bytes on disk
 *
 * A decompressed picture block is:
 *
 *   i16 height (pixel rows)   i16 width (in 8-pixel cells)
 *   i16 x_pos                 i16 y_pos
 *   u8  item_count            u8 field_9[8]
 *   then item_count frames of packed 4-bit pixels (two pixels per byte).
 */
#ifndef COAB_DAX_H
#define COAB_DAX_H

#include "coab.h"

typedef struct {
    int  height;       /* pixel rows */
    int  width;        /* width in 8-pixel columns */
    int  x_pos;
    int  y_pos;
    int  item_count;   /* number of frames packed into this block */
    u8   field_9[8];
    int  bpp;          /* bytes (== pixels) per frame: height * width * 8 */
    u8  *data;         /* item_count * bpp palette indices, 16 == transparent */
    size_t data_size;
} DaxBlock;

/* Reads block_id out of file_name (a DOS basename such as "title.dax", any
 * case) and unpacks it into a picture. masked != 0 turns every pixel equal to
 * mask_colour into the transparent index 16. Returns NULL when the block does
 * not exist. Free with dax_block_free(). */
DaxBlock *dax_load_block(const char *file_name, int block_id,
                         int masked, int mask_colour);

/* An all-zero picture of the given geometry, for scratch buffers such as
 * gbl.cursor_bkup (engine/seg001.cs). */
DaxBlock *dax_block_new(int masked, int item_count, int width, int height);

void dax_block_free(DaxBlock *b);

/* Classes/DaxBlock.cs: DaxToPicture. Unpacks item_count * height * width * 8
 * pixels out of src starting at src_offset, two pixels per byte, high nibble
 * first. dax_load_block does this itself with src_offset == 17 (just past the
 * picture header); ovr030.load_pic_final needs it at an arbitrary offset,
 * because an animation packs its frames one after another in a single block. */
void dax_block_decode_pixels(DaxBlock *b, int mask_colour, int masked,
                             const u8 *src, size_t src_size, size_t src_offset);

void dax_block_flip_left_to_right(DaxBlock *b);
void dax_block_recolor(DaxBlock *b, bool use_random,
                       const u8 *new_colors, const u8 *old_colors);
void dax_block_merge_icons(DaxBlock *dst, const DaxBlock *src);

/* Raw decompressed bytes for a block, for callers that want the block contents
 * rather than a picture (e.g. the 8x8 font, monster records, ECL scripts).
 * Caller frees. *out_size receives the length. */
u8 *dax_load_raw(const char *file_name, int block_id, size_t *out_size);

/* Drops every cached archive. */
void dax_cache_clear(void);

/* engine/seg042.cs: load_decode_dax. Thin wrapper over dax_load_raw that also
 * reports the size as the engine's i16 out-parameter did. */
u8 *dax_load_decode(const char *file_name, int block_id, i16 *out_size);

/* --- animation frames (Classes/DaxArray.cs) --- */

#define DAX_ARRAY_FRAMES 8

typedef struct {
    int       delay;      /* tenths of a second */
    DaxBlock *picture;
} AnimationFrame;

typedef struct {
    int            num_frames;
    int            cur_frame;   /* 1-based, as in the original */
    AnimationFrame frames[DAX_ARRAY_FRAMES];
} DaxArray;

void      dax_array_init(DaxArray *a);
void      dax_array_next_frame(DaxArray *a);
DaxBlock *dax_array_current_picture(const DaxArray *a);
int       dax_array_current_delay(const DaxArray *a);

#endif /* COAB_DAX_H */
