#include "dax.h"
#include "vfs.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DAX_HEADER_ENTRY_SIZE 9
#define DAX_MAX_BLOCK_ID      256
#define DAX_PIC_HEADER_SIZE   17

/* ------------------------------------------------------------------- cache */

typedef struct DaxFile {
    char            name[64];    /* lower-cased basename as the engine asks */
    u8             *block[DAX_MAX_BLOCK_ID];
    size_t          size[DAX_MAX_BLOCK_ID];
    struct DaxFile *next;
} DaxFile;

static DaxFile *g_files;

/* Classes/DaxFileCache.cs: Decode.
 *
 * Byte-oriented RLE. A non-negative control byte n means "copy the next n+1
 * bytes verbatim"; a negative one means "repeat the next byte -n times".
 * The original had no bounds checks and relied on the CLR throwing; here a
 * malformed block has to be caught explicitly or it corrupts the heap. */
static bool dax_decode(u8 *out, size_t out_size, const u8 *in, size_t in_size)
{
    size_t in_index = 0;
    size_t out_index = 0;

    if (in_size == 0) {
        return out_size == 0;
    }

    do {
        i8 run_length = (i8)in[in_index];

        if (run_length >= 0) {
            size_t count = (size_t)run_length + 1;

            if (in_index + 1 + count > in_size || out_index + count > out_size) {
                return false;
            }
            memcpy(out + out_index, in + in_index + 1, count);
            in_index  += count + 1;
            out_index += count;
        } else {
            size_t count = (size_t)(-run_length);

            if (in_index + 1 >= in_size || out_index + count > out_size) {
                return false;
            }
            memset(out + out_index, in[in_index + 1], count);
            in_index  += 2;
            out_index += count;
        }
    } while (in_index < in_size);

    return true;
}

static DaxFile *dax_file_load(const char *lower_name)
{
    const char *path = vfs_resolve(lower_name);
    u8 *raw_file = NULL;
    size_t file_size = 0;
    DaxFile *df;
    size_t data_offset, header_count;

    df = calloc(1, sizeof(*df));
    if (!df) {
        return NULL;
    }
    snprintf(df->name, sizeof(df->name), "%s", lower_name);

    if (!path) {
        /* Matches DaxFileCache: a missing file yields an empty cache rather
         * than an error, and the caller decides whether that is fatal. */
        log_warn("dax: %s not found in %s", lower_name, vfs_data_dir());
        return df;
    }

    raw_file = vfs_read_file(path, &file_size);
    if (!raw_file) {
        log_warn("dax: cannot read %s", path);
        return df;
    }
    if (file_size < 2) {
        log_warn("dax: %s is truncated", path);
        free(raw_file);
        return df;
    }

    data_offset  = (size_t)sys_array_to_ushort(raw_file, 0) + 2;
    header_count = (data_offset - 2) / DAX_HEADER_ENTRY_SIZE;

    if (data_offset > file_size) {
        log_warn("dax: %s header claims %zu bytes but file is %zu",
                 path, data_offset, file_size);
        free(raw_file);
        return df;
    }

    for (size_t i = 0; i < header_count; i++) {
        const u8 *h = raw_file + 2 + (i * DAX_HEADER_ENTRY_SIZE);
        int    id        = h[0];
        i32    offset    = sys_array_to_int(h, 1);
        /* raw_size is an i16 in the on-disk format; every block in the shipped
         * data is small enough to stay positive, and a size is never negative,
         * so read it unsigned to stay safe if one ever exceeds 32767. */
        size_t raw_size  = sys_array_to_ushort(h, 5);
        size_t comp_size = sys_array_to_ushort(h, 7);
        size_t start     = data_offset + (size_t)offset;
        u8    *out;

        if (offset < 0 || start > file_size || start + comp_size > file_size) {
            log_warn("dax: %s block %d lies outside the file", path, id);
            continue;
        }
        if (raw_size == 0 || comp_size == 0) {
            continue;
        }
        if (df->block[id]) {
            /* The C# Dictionary.Add would have thrown on a duplicate id. */
            log_warn("dax: %s has a duplicate block id %d", path, id);
            continue;
        }

        out = malloc(raw_size);
        if (!out) {
            break;
        }
        if (!dax_decode(out, raw_size, raw_file + start, comp_size)) {
            log_warn("dax: %s block %d failed to decompress", path, id);
            free(out);
            continue;
        }

        df->block[id] = out;
        df->size[id]  = raw_size;
    }

    free(raw_file);
    return df;
}

/* Classes/DaxCache.cs: LoadDax. Archives stay cached for the process lifetime;
 * the engine re-requests the same blocks constantly. */
static DaxFile *dax_file_get(const char *file_name)
{
    char lower[64];
    size_t i;

    if (!file_name || !file_name[0]) {
        return NULL;
    }
    snprintf(lower, sizeof(lower), "%s", file_name);
    for (i = 0; lower[i]; i++) {
        if (lower[i] >= 'A' && lower[i] <= 'Z') {
            lower[i] = (char)(lower[i] - 'A' + 'a');
        }
    }

    for (DaxFile *df = g_files; df; df = df->next) {
        if (strcmp(df->name, lower) == 0) {
            return df;
        }
    }

    DaxFile *df = dax_file_load(lower);
    if (df) {
        df->next = g_files;
        g_files = df;
    }
    return df;
}

void dax_cache_clear(void)
{
    DaxFile *df = g_files;

    while (df) {
        DaxFile *next = df->next;
        for (int i = 0; i < DAX_MAX_BLOCK_ID; i++) {
            free(df->block[i]);
        }
        free(df);
        df = next;
    }
    g_files = NULL;
}

static const u8 *dax_peek(const char *file_name, int block_id, size_t *out_size)
{
    DaxFile *df = dax_file_get(file_name);

    if (out_size) {
        *out_size = 0;
    }
    if (!df || block_id < 0 || block_id >= DAX_MAX_BLOCK_ID || !df->block[block_id]) {
        return NULL;
    }
    if (out_size) {
        *out_size = df->size[block_id];
    }
    return df->block[block_id];
}

u8 *dax_load_raw(const char *file_name, int block_id, size_t *out_size)
{
    size_t size = 0;
    const u8 *src = dax_peek(file_name, block_id, &size);
    u8 *copy;

    if (out_size) {
        *out_size = 0;
    }
    if (!src) {
        return NULL;
    }

    copy = malloc(size);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, size);

    if (out_size) {
        *out_size = size;
    }
    return copy;
}

u8 *dax_load_decode(const char *file_name, int block_id, i16 *out_size)
{
    size_t size = 0;
    u8 *data = dax_load_raw(file_name, block_id, &size);

    if (out_size) {
        *out_size = (i16)size;
    }
    return data;
}

/* --------------------------------------------------------------- DaxBlock */

static void set_masked_color(DaxBlock *b, size_t offset, int color,
                             int masked, int mask_color)
{
    if (offset >= b->data_size) {
        return;
    }
    b->data[offset] = (masked == 1 && color == mask_color)
                    ? (u8)COLOR_MASK
                    : (u8)color;
}

/* Classes/DaxBlock.cs: DaxToPicture. Each source byte holds two pixels, high
 * nibble first. */
void dax_block_decode_pixels(DaxBlock *b, int mask_colour, int masked,
                             const u8 *src, size_t src_size, size_t src_offset)
{
    size_t dest_offset = 0;

    if (b == NULL || src == NULL) {
        return;
    }

    for (int item = 0; item < b->item_count; item++) {
        for (int row = 0; row < b->height; row++) {
            for (int col = 0; col < b->width * 4; col++) {
                u8 c;

                if (src_offset >= src_size) {
                    return;   /* short block; leave the remainder black */
                }
                c = src[src_offset++];

                set_masked_color(b, dest_offset++, c >> 4,   masked, mask_colour);
                set_masked_color(b, dest_offset++, c & 0x0f, masked, mask_colour);
            }
        }
    }
}

DaxBlock *dax_block_new(int masked, int item_count, int width, int height)
{
    DaxBlock *b = calloc(1, sizeof(*b));

    (void)masked;
    if (!b) {
        return NULL;
    }

    b->height     = height;
    b->width      = width;
    b->bpp        = height * width * 8;
    b->item_count = item_count;
    b->data_size  = (size_t)item_count * (size_t)b->bpp;

    if (b->data_size == 0) {
        free(b);
        return NULL;
    }

    b->data = calloc(b->data_size, 1);
    if (!b->data) {
        free(b);
        return NULL;
    }
    return b;
}

DaxBlock *dax_load_block(const char *file_name, int block_id,
                         int masked, int mask_colour)
{
    size_t src_size = 0;
    const u8 *src = dax_peek(file_name, block_id, &src_size);
    DaxBlock *b;

    if (!src || src_size < DAX_PIC_HEADER_SIZE) {
        return NULL;
    }

    b = calloc(1, sizeof(*b));
    if (!b) {
        return NULL;
    }

    b->height     = sys_array_to_short(src, 0);
    b->width      = sys_array_to_short(src, 2);
    b->x_pos      = sys_array_to_short(src, 4);
    b->y_pos      = sys_array_to_short(src, 6);
    b->item_count = src[8];
    memcpy(b->field_9, src + 9, 8);

    b->bpp = b->height * b->width * 8;

    if (b->height <= 0 || b->width <= 0 || b->item_count <= 0 ||
        b->height > 4096 || b->width > 4096) {
        log_warn("dax: %s block %d has bad geometry %dx%d x%d",
                 file_name, block_id, b->width, b->height, b->item_count);
        free(b);
        return NULL;
    }

    b->data_size = (size_t)b->item_count * (size_t)b->bpp;
    b->data = calloc(b->data_size, 1);
    if (!b->data) {
        free(b);
        return NULL;
    }

    dax_block_decode_pixels(b, mask_colour, masked, src, src_size,
                            DAX_PIC_HEADER_SIZE);
    return b;
}

void dax_block_free(DaxBlock *b)
{
    if (!b) {
        return;
    }
    free(b->data);
    free(b);
}

void dax_block_flip_left_to_right(DaxBlock *b)
{
    int row_pixels;
    u8 *tmp;

    if (!b) {
        return;
    }
    row_pixels = b->width * 8;

    tmp = malloc(b->data_size);
    if (!tmp) {
        return;
    }
    memcpy(tmp, b->data, b->data_size);

    /* Only the first frame is mirrored, matching the original. */
    for (int y = 0; y < b->height; y++) {
        for (int x = 0; x < row_pixels; x++) {
            size_t di = (size_t)y * row_pixels + x;
            size_t si = (size_t)y * row_pixels + (row_pixels - x - 1);

            if (di < b->data_size && si < b->data_size) {
                tmp[di] = b->data[si];
            }
        }
    }

    memcpy(b->data, tmp, b->data_size);
    free(tmp);
}

/* A deterministic 32-bit LCG stands in for System.Random. Recolor only uses it
 * to thin out the substitution ("one pixel in four"), so the exact sequence
 * does not matter, but reproducibility across runs helps when debugging. */
static u32 g_recolor_rng = 0x1234567u;

static u32 recolor_random(void)
{
    g_recolor_rng = g_recolor_rng * 1103515245u + 12345u;
    return (g_recolor_rng >> 16) & 0x7fffu;
}

void dax_block_recolor(DaxBlock *b, bool use_random,
                       const u8 *new_colors, const u8 *old_colors)
{
    if (!b || !new_colors || !old_colors) {
        return;
    }

    for (int idx = 0; idx < EGA_COLORS; idx++) {
        if (old_colors[idx] == new_colors[idx]) {
            continue;
        }

        size_t offset = 0;
        for (int y = 0; y < b->height; y++) {
            for (int x = 0; x < b->width * 8; x++) {
                if (offset >= b->data_size) {
                    return;
                }
                if (b->data[offset] == old_colors[idx] &&
                    (!use_random || (recolor_random() % 4) == 0)) {
                    b->data[offset] = new_colors[idx];
                }
                offset++;
            }
        }
    }
}

void dax_block_merge_icons(DaxBlock *dst, const DaxBlock *src)
{
    size_t count;

    if (!dst || !src) {
        return;
    }
    count = (size_t)src->bpp;
    count = COAB_MIN(count, dst->data_size);
    count = COAB_MIN(count, src->data_size);

    for (size_t i = 0; i < count; i++) {
        u8 a = dst->data[i];
        u8 b = src->data[i];

        if (a == COLOR_MASK) {
            dst->data[i] = b;
        } else if (b == COLOR_MASK) {
            dst->data[i] = a;
        } else {
            dst->data[i] = (u8)(a | b);
        }
    }
}

/* -------------------------------------------------------------- DaxArray */

void dax_array_init(DaxArray *a)
{
    if (a) {
        memset(a, 0, sizeof(*a));
    }
}

void dax_array_next_frame(DaxArray *a)
{
    if (!a) {
        return;
    }
    a->cur_frame++;
    if (a->cur_frame > a->num_frames) {
        a->cur_frame = 1;
    }
}

DaxBlock *dax_array_current_picture(const DaxArray *a)
{
    if (!a || a->cur_frame < 1 || a->cur_frame > DAX_ARRAY_FRAMES) {
        return NULL;
    }
    return a->frames[a->cur_frame - 1].picture;
}

int dax_array_current_delay(const DaxArray *a)
{
    if (!a || a->cur_frame < 1 || a->cur_frame > DAX_ARRAY_FRAMES) {
        return 0;
    }
    return a->frames[a->cur_frame - 1].delay;
}
