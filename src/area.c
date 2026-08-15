/* area.c - Ported from Classes/Area1.cs and Classes/Area2.cs. */
#include <string.h>

#include "area.h"

#include "log.h"

/* ------------------------------------------------------------------- Area1 */

static const DioField area1_fields[] = {
    DIO_F(Area1, field_186,               0x186, DIO_BYTE, 0),
    DIO_F(Area1, field_188,               0x188, DIO_BYTE, 0),
    DIO_F(Area1, current_3d_map_block_id, 0x18a, DIO_BYTE, 0),
    /* The C# tagged field_18C as 0x18E, colliding with the minutes counter;
     * 0x18C is the offset its name, and ovr021's switch, both give it. */
    DIO_F(Area1, field_18C,               0x18c, DIO_WORD, 0),
    DIO_F(Area1, time_minutes_ones,       0x18e, DIO_WORD, 0),
    DIO_F(Area1, time_minutes_tens,       0x190, DIO_WORD, 0),
    DIO_F(Area1, time_hour,               0x192, DIO_WORD, 0),
    DIO_F(Area1, time_day,                0x194, DIO_WORD, 0),
    DIO_F(Area1, time_year,               0x196, DIO_WORD, 0),
    DIO_F(Area1, field_198,               0x198, DIO_WORD, 0),

    DIO_F(Area1, field_1CA,               0x1ca, DIO_SWORD, 0),
    DIO_F(Area1, in_dungeon,              0x1cc, DIO_SWORD, 0),
    DIO_F(Area1, field_1CE,               0x1ce, DIO_SWORD, 0),
    DIO_F(Area1, field_1D0,               0x1d0, DIO_SWORD, 0),

    DIO_F(Area1, last_x_pos,              0x1e0, DIO_SWORD, 0),
    DIO_F(Area1, last_y_pos,              0x1e2, DIO_SWORD, 0),
    DIO_F(Area1, last_ecl_block_id,       0x1e4, DIO_WORD,  0),
    DIO_F(Area1, block_area_view,         0x1f6, DIO_SWORD, 0),
    DIO_F(Area1, game_speed,              0x1f8, DIO_BYTE,  0),
    DIO_F(Area1, outdoor_sky_colour,      0x1fa, DIO_WORD,  0),
    DIO_F(Area1, indoor_sky_colour,       0x1fc, DIO_WORD,  0),
    DIO_F(Area1, pics_on,                 0x1fe, DIO_BYTE,  0),
    DIO_F(Area1, can_cast_spells,         0x1ff, DIO_BOOL,  0),

    DIO_F(Area1, field_200,               0x200, DIO_SHORT_ARRAY,
          AREA1_FIELD_200_COUNT),

    DIO_F(Area1, field_244,               0x244, DIO_WORD, 0),
    DIO_F(Area1, field_24E,               0x24e, DIO_WORD, 0),
    DIO_F(Area1, field_250,               0x250, DIO_WORD, 0),
    DIO_F(Area1, field_252,               0x252, DIO_WORD, 0),
    DIO_F(Area1, field_254,               0x254, DIO_WORD, 0),
    DIO_F(Area1, field_256,               0x256, DIO_WORD, 0),
    DIO_F(Area1, field_258,               0x258, DIO_WORD, 0),
    DIO_F(Area1, field_25A,               0x25a, DIO_WORD, 0),
    DIO_F(Area1, field_25C,               0x25c, DIO_WORD, 0),
    DIO_F(Area1, field_25E,               0x25e, DIO_WORD, 0),
    DIO_F(Area1, field_260,               0x260, DIO_WORD, 0),
    DIO_F(Area1, field_26A,               0x26a, DIO_WORD, 0),
    DIO_F(Area1, field_296,               0x296, DIO_WORD, 0),
    DIO_F(Area1, field_298,               0x298, DIO_WORD, 0),
    DIO_F(Area1, field_29A,               0x29a, DIO_WORD, 0),
    DIO_F(Area1, field_2B2,               0x2b2, DIO_WORD, 0),
    DIO_F(Area1, field_2B4,               0x2b4, DIO_WORD, 0),
    DIO_F(Area1, field_2B6,               0x2b6, DIO_WORD, 0),
    DIO_F(Area1, field_2C0,               0x2c0, DIO_WORD, 0),
    DIO_F(Area1, field_2CA,               0x2ca, DIO_WORD, 0),

    DIO_F(Area1, field_336,               0x336, DIO_BYTE, 0),
    DIO_F(Area1, field_338,               0x338, DIO_BYTE, 0),
    DIO_F(Area1, field_33A,               0x33a, DIO_BYTE, 0),
    DIO_F(Area1, field_33C,               0x33c, DIO_WORD, 0),

    DIO_F(Area1, field_340,               0x340, DIO_BYTE, 0),
    DIO_F(Area1, current_city,            0x342, DIO_BYTE, 0),
    DIO_F(Area1, field_344,               0x344, DIO_BYTE, 0),
    DIO_F(Area1, field_346,               0x346, DIO_BYTE, 0),
    DIO_F(Area1, field_348,               0x348, DIO_BYTE, 0),

    DIO_F(Area1, field_3C2,               0x3c2, DIO_WORD, 0),
    DIO_F(Area1, field_3CA,               0x3ca, DIO_WORD, 0),
    DIO_F(Area1, field_3CC,               0x3cc, DIO_WORD, 0),

    DIO_F(Area1, field_3D4,               0x3d4, DIO_WORD, 0),
    DIO_F(Area1, field_3D6,               0x3d6, DIO_WORD, 0),
    DIO_F(Area1, field_3D8,               0x3d8, DIO_WORD, 0),
    DIO_F(Area1, field_3DA,               0x3da, DIO_WORD, 0),
    DIO_F(Area1, field_3DC,               0x3dc, DIO_WORD, 0),
    DIO_F(Area1, field_3DE,               0x3de, DIO_WORD, 0),
    DIO_F(Area1, field_3E0,               0x3e0, DIO_WORD, 0),
    DIO_F(Area1, field_3E2,               0x3e2, DIO_WORD, 0),
    DIO_F(Area1, field_3E4,               0x3e4, DIO_WORD, 0),
    DIO_F(Area1, field_3E6,               0x3e6, DIO_WORD, 0),
    DIO_F(Area1, field_3E8,               0x3e8, DIO_WORD, 0),

    DIO_F(Area1, field_3FA,               0x3fa, DIO_BYTE,  0),
    DIO_F(Area1, field_3FC,               0x3fc, DIO_WORD,  0),
    DIO_F(Area1, picture_fade,            0x3fe, DIO_SWORD, 0),
    DIO_F(Area1, field_596,               0x596, DIO_WORD,  0),
    DIO_END_MARKER
};

const DioDesc area1_desc = { "Area1", AREA_BLOCK_SIZE, area1_fields };

/* ------------------------------------------------------------------- Area2 */

static const DioField area2_fields[] = {
    DIO_F(Area2, field_170,                 0x170, DIO_WORD,  0),
    DIO_F(Area2, field_218,                 0x218, DIO_WORD,  0),
    DIO_F(Area2, training_class_mask,       0x550, DIO_BYTE,  0),
    DIO_F(Area2, max_encounter_distance,    0x580, DIO_WORD,  0),
    DIO_F(Area2, encounter_distance,        0x582, DIO_WORD,  0),
    DIO_F(Area2, field_58C,                 0x58c, DIO_WORD,  0),
    DIO_F(Area2, field_58E,                 0x58e, DIO_SWORD, 0),
    DIO_F(Area2, field_590,                 0x590, DIO_SWORD, 0),
    DIO_F(Area2, field_592,                 0x592, DIO_SWORD, 0),
    DIO_F(Area2, search_flags,              0x594, DIO_WORD,  0),
    DIO_F(Area2, field_596,                 0x596, DIO_SWORD, 0),
    DIO_F(Area2, rest_encounter_period,     0x5a4, DIO_SWORD, 0),
    DIO_F(Area2, rest_encounter_percentage, 0x5a6, DIO_SWORD, 0),
    DIO_F(Area2, tried_to_exit_map,         0x5aa, DIO_BOOL,  0),
    DIO_F(Area2, head_block_id,             0x5c2, DIO_BYTE,  0),
    DIO_F(Area2, enter_temple,              0x5c4, DIO_WORD,  0),
    DIO_F(Area2, field_5C6,                 0x5c6, DIO_SWORD, 0),
    /* The C# left this out of the record - its attribute is commented out -
     * so a duel did not survive a save. It is a byte of the block. */
    DIO_F(Area2, is_duel,                   0x5cc, DIO_BOOL,  0),
    DIO_F(Area2, game_area,                 0x624, DIO_BYTE,  0),
    DIO_F(Area2, field_666,                 0x666, DIO_SWORD, 0),
    DIO_F(Area2, party_size,                0x67c, DIO_BYTE,  0),
    DIO_F(Area2, field_67E,                 0x67e, DIO_SWORD, 0),
    DIO_F(Area2, enter_shop,                0x6d8, DIO_WORD,  0),
    DIO_F(Area2, field_6DA,                 0x6da, DIO_SWORD, 0),
    DIO_F(Area2, field_6E0,                 0x6e0, DIO_SWORD, 0),
    DIO_F(Area2, field_6E2,                 0x6e2, DIO_SWORD, 0),
    DIO_F(Area2, field_6E4,                 0x6e4, DIO_SWORD, 0),

    DIO_F(Area2, field_6F2,                 0x6f2, DIO_WORD,  0),
    DIO_F(Area2, field_6F4,                 0x6f4, DIO_WORD,  0),
    DIO_F(Area2, field_6F6,                 0x6f6, DIO_WORD,  0),
    DIO_F(Area2, field_6F8,                 0x6f8, DIO_WORD,  0),
    DIO_F(Area2, field_6FA,                 0x6fa, DIO_WORD,  0),
    DIO_F(Area2, field_6FC,                 0x6fc, DIO_WORD,  0),
    DIO_F(Area2, field_6FE,                 0x6fe, DIO_WORD,  0),
    DIO_F(Area2, field_700,                 0x700, DIO_WORD,  0),
    DIO_F(Area2, field_702,                 0x702, DIO_WORD,  0),
    DIO_F(Area2, field_704,                 0x704, DIO_WORD,  0),

    DIO_F(Area2, field_799,                 0x799, DIO_BYTE,  0),
    DIO_F(Area2, field_79A,                 0x79a, DIO_BYTE,  0),
    DIO_F(Area2, field_79B,                 0x79b, DIO_BYTE,  0),
    DIO_F(Area2, field_79C,                 0x79c, DIO_BYTE,  0),
    DIO_F(Area2, field_79D,                 0x79d, DIO_BYTE,  0),
    DIO_F(Area2, field_79E,                 0x79e, DIO_BYTE,  0),
    DIO_F(Area2, field_79F,                 0x79f, DIO_BYTE,  0),
    DIO_F(Area2, field_7A0,                 0x7a0, DIO_BYTE,  0),
    DIO_F(Area2, field_7A1,                 0x7a1, DIO_BYTE,  0),
    DIO_F(Area2, field_7A2,                 0x7a2, DIO_BYTE,  0),
    DIO_F(Area2, field_7A3,                 0x7a3, DIO_BYTE,  0),
    DIO_F(Area2, field_7A4,                 0x7a4, DIO_BYTE,  0),
    DIO_F(Area2, field_7A5,                 0x7a5, DIO_BYTE,  0),
    DIO_F(Area2, field_7A6,                 0x7a6, DIO_BYTE,  0),
    DIO_F(Area2, field_7A7,                 0x7a7, DIO_BYTE,  0),
    DIO_F(Area2, field_7A8,                 0x7a8, DIO_BYTE,  0),
    DIO_F(Area2, field_7A9,                 0x7a9, DIO_BYTE,  0),
    DIO_F(Area2, field_7AA,                 0x7aa, DIO_BYTE,  0),
    DIO_F(Area2, field_7AB,                 0x7ab, DIO_BYTE,  0),
    DIO_F(Area2, field_7EC,                 0x7ec, DIO_WORD,  0),
    DIO_END_MARKER
};

const DioDesc area2_desc = { "Area2", AREA_BLOCK_SIZE, area2_fields };

/* -------------------------------------------------------- shared machinery */

/* The ECL interpreter reaches these blocks through 16-bit segment arithmetic
 * that wraps, so the offset is masked exactly as the original's was. What it
 * cannot do is leave the block: the C# would have raised
 * IndexOutOfRangeException, and in C it would be a buffer overrun. */
static bool area_loc_ok(const char *what, int loc, size_t *out, size_t width)
{
    size_t at = (size_t)(loc & 0xffff);

    if (at + width > AREA_BLOCK_SIZE) {
        log_warn("%s: offset 0x%zx is outside the 0x%x byte block",
                 what, at, AREA_BLOCK_SIZE);
        return false;
    }
    *out = at;
    return true;
}

static bool area_read(void *obj, const DioDesc *desc, u8 *raw,
                      const u8 *data, size_t data_size, size_t offset)
{
    if (offset + AREA_BLOCK_SIZE > data_size) {
        log_warn("%s: need 0x%x bytes at %zu but the buffer is %zu",
                 desc->name, AREA_BLOCK_SIZE, offset, data_size);
        return false;
    }
    if (!dio_read(desc, obj, data, data_size, offset)) {
        return false;
    }
    memcpy(raw, data + offset, AREA_BLOCK_SIZE);

    return true;
}

/* The named fields win over the raw copy, so a field the engine changed is
 * written even though the raw bytes it came from are stale. */
static bool area_write(const void *obj, const DioDesc *desc, const u8 *raw,
                       u8 *data, size_t data_size)
{
    if (data_size < AREA_BLOCK_SIZE) {
        log_warn("%s: write needs 0x%x bytes, given %zu",
                 desc->name, AREA_BLOCK_SIZE, data_size);
        return false;
    }
    memcpy(data, raw, AREA_BLOCK_SIZE);

    return dio_write(desc, obj, data, data_size);
}

/* ---------------------------------------------------------- Area1 accessors */

/* Clear() zeroed the raw bytes and re-read the fields from them; zeroing the
 * whole struct is that in one step. */
void area1_clear(Area1 *a)
{
    memset(a, 0, sizeof(*a));
}

bool area1_read(Area1 *a, const u8 *data, size_t data_size, size_t offset)
{
    area1_clear(a);
    return area_read(a, &area1_desc, a->raw, data, data_size, offset);
}

bool area1_write(const Area1 *a, u8 *data, size_t data_size)
{
    return area_write(a, &area1_desc, a->raw, data, data_size);
}

u16 area1_word_get(const Area1 *a, int loc)
{
    size_t at;
    u16    value;

    if (!area_loc_ok("Area1", loc, &at, 2)) {
        return 0;
    }
    if (dio_word_get(&area1_desc, a, at, &value)) {
        return value;
    }
    return sys_array_to_ushort(a->raw, (int)at);
}

void area1_word_set(Area1 *a, int loc, u16 value)
{
    size_t at;

    if (!area_loc_ok("Area1", loc, &at, 2)) {
        return;
    }
    if (!dio_word_set(&area1_desc, a, at, value)) {
        sys_short_to_array((i16)value, a->raw, at);
    }
}

void area1_reset_field_200(Area1 *a)
{
    memset(a->field_200, 0, sizeof(a->field_200));
}

/* ---------------------------------------------------------- Area2 accessors */

void area2_clear(Area2 *a)
{
    memset(a, 0, sizeof(*a));
}

bool area2_read(Area2 *a, const u8 *data, size_t data_size, size_t offset)
{
    area2_clear(a);
    return area_read(a, &area2_desc, a->raw, data, data_size, offset);
}

bool area2_write(const Area2 *a, u8 *data, size_t data_size)
{
    return area_write(a, &area2_desc, a->raw, data, data_size);
}

u16 area2_word_get(const Area2 *a, int loc)
{
    size_t at;
    u16    value;

    if (!area_loc_ok("Area2", loc, &at, 2)) {
        return 0;
    }
    if (dio_word_get(&area2_desc, a, at, &value)) {
        return value;
    }
    return sys_array_to_ushort(a->raw, (int)at);
}

void area2_word_set(Area2 *a, int loc, u16 value)
{
    size_t at;

    if (!area_loc_ok("Area2", loc, &at, 2)) {
        return;
    }
    if (!dio_word_set(&area2_desc, a, at, value)) {
        sys_short_to_array((i16)value, a->raw, at);
    }
}

void area2_reset_field_6F2(Area2 *a)
{
    a->field_6F2 = 0;
    a->field_6F4 = 0;
    a->field_6F6 = 0;
    a->field_6F8 = 0;
    a->field_6FA = 0;
    a->field_6FC = 0;
    a->field_6FE = 0;
    a->field_700 = 0;
    a->field_702 = 0;
    a->field_704 = 0;
}
