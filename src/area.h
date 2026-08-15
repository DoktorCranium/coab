/* area.h - the two 0x800-byte game-state blocks.
 * Ported from Classes/Area1.cs and Classes/Area2.cs.
 *
 * These were plain memory in the DOS original, so ECL scripts reach into them by
 * numeric offset: "set word 0x192 to 7" is how the clock gets its hour. The
 * engine also has names for the offsets it cares about. Both views have to work
 * and agree, so each block keeps its raw bytes alongside the named fields:
 *
 *   named access   - area1->time_hour
 *   ECL access     - area1_word_set(a, 0x192, 7), which finds the named field
 *                    through the descriptor table and falls back to raw bytes
 *                    for offsets nothing has named yet
 *
 * The C# did the fallback the same way but listed every named offset in a
 * 250-line switch; the descriptor table already holds that mapping, so the
 * switch is gone. Two consequences worth knowing:
 *
 *  - Area1.field_18C was tagged [DataOffset(0x18E)], the same offset as
 *    time_minutes_ones, so in the C# a write to 0x18C landed on top of the
 *    minutes counter when the block was saved. Here field_18C is at 0x18C.
 *  - Area2.isDuel had its [DataOffset] commented out, so the C# dropped it on
 *    save while its switch still served reads and writes at 0x5cc. Here it is
 *    serialized at 0x5cc, which is what the DOS block did.
 *  - Area2.ToByteArray built a fresh zeroed buffer, discarding any raw bytes
 *    that no field named; Area1's kept them. Both keep them here.
 */
#ifndef COAB_AREA_H
#define COAB_AREA_H

#include "coab.h"
#include "dataio.h"

#define AREA_BLOCK_SIZE   0x800
#define AREA1_FIELD_200_COUNT 33   /* short[33] at 0x200, used as 1..32 */

/* ------------------------------------------------------------------- Area1 */

typedef struct {
    /* Every byte of the block as it was read, so offsets no field below names
     * still round-trip through a save. */
    u8   raw[AREA_BLOCK_SIZE];

    u8   field_186;                 /* 0x186 */
    u8   field_188;                 /* 0x188 */
    u8   current_3d_map_block_id;   /* 0x18a */
    u16  field_18C;                 /* 0x18c */

    /* The clock. Minutes are split into ones and tens, as the display wants
     * them; ovr021's NormalizeClock carries between them. */
    u16  time_minutes_ones;         /* 0x18e */
    u16  time_minutes_tens;         /* 0x190 */
    u16  time_hour;                 /* 0x192 */
    u16  time_day;                  /* 0x194 */
    u16  time_year;                 /* 0x196 */
    u16  field_198;                 /* 0x198 */

    i16  field_1CA;                 /* 0x1ca */
    i16  in_dungeon;                /* 0x1cc */
    i16  field_1CE;                 /* 0x1ce */
    i16  field_1D0;                 /* 0x1d0 */

    i16  last_x_pos;                /* 0x1e0 */
    i16  last_y_pos;                /* 0x1e2 */
    u16  last_ecl_block_id;         /* 0x1e4 */
    i16  block_area_view;           /* 0x1f6 */
    u8   game_speed;                /* 0x1f8 */
    u16  outdoor_sky_colour;        /* 0x1fa */
    u16  indoor_sky_colour;         /* 0x1fc */
    u8   pics_on;                   /* 0x1fe */
    bool can_cast_spells;           /* 0x1ff */

    i16  field_200[AREA1_FIELD_200_COUNT];  /* 0x200 */

    u16  field_244;                 /* 0x244 */
    u16  field_24E;                 /* 0x24e */
    u16  field_250;                 /* 0x250 */
    u16  field_252;                 /* 0x252 */
    u16  field_254;                 /* 0x254 */
    u16  field_256;                 /* 0x256 */
    u16  field_258;                 /* 0x258 */
    u16  field_25A;                 /* 0x25a */
    u16  field_25C;                 /* 0x25c */
    u16  field_25E;                 /* 0x25e */
    u16  field_260;                 /* 0x260 */
    u16  field_26A;                 /* 0x26a */
    u16  field_296;                 /* 0x296 */
    u16  field_298;                 /* 0x298 */
    u16  field_29A;                 /* 0x29a */
    u16  field_2B2;                 /* 0x2b2 */
    u16  field_2B4;                 /* 0x2b4 */
    u16  field_2B6;                 /* 0x2b6 */
    u16  field_2C0;                 /* 0x2c0 */
    u16  field_2CA;                 /* 0x2ca */

    u8   field_336;                 /* 0x336 */
    u8   field_338;                 /* 0x338 */
    u8   field_33A;                 /* 0x33a */
    u16  field_33C;                 /* 0x33c */

    u8   field_340;                 /* 0x340 */
    u8   current_city;              /* 0x342 */
    u8   field_344;                 /* 0x344 */
    u8   field_346;                 /* 0x346 */
    u8   field_348;                 /* 0x348 */

    u16  field_3C2;                 /* 0x3c2 */
    u16  field_3CA;                 /* 0x3ca */
    u16  field_3CC;                 /* 0x3cc */

    u16  field_3D4;                 /* 0x3d4 */
    u16  field_3D6;                 /* 0x3d6 */
    u16  field_3D8;                 /* 0x3d8 */
    u16  field_3DA;                 /* 0x3da */
    u16  field_3DC;                 /* 0x3dc */
    u16  field_3DE;                 /* 0x3de */
    u16  field_3E0;                 /* 0x3e0 */
    u16  field_3E2;                 /* 0x3e2 */
    u16  field_3E4;                 /* 0x3e4 */
    u16  field_3E6;                 /* 0x3e6 */
    u16  field_3E8;                 /* 0x3e8 */

    u8   field_3FA;                 /* 0x3fa */
    u16  field_3FC;                 /* 0x3fc */
    i16  picture_fade;              /* 0x3fe */
    u16  field_596;                 /* 0x596 */
} Area1;

extern const DioDesc area1_desc;

void area1_clear(Area1 *a);
bool area1_read(Area1 *a, const u8 *data, size_t data_size, size_t offset);
bool area1_write(const Area1 *a, u8 *data, size_t data_size);

/* field_6A00_Get / field_6A00_Set. loc is masked to 16 bits, as the original's
 * segment arithmetic was, then has to land inside the block; an offset that does
 * not is logged and ignored, where the C# would have thrown. */
u16  area1_word_get(const Area1 *a, int loc);
void area1_word_set(Area1 *a, int loc, u16 value);

/* RestField200Values. */
void area1_reset_field_200(Area1 *a);

/* ------------------------------------------------------------------- Area2 */

typedef struct {
    u8   raw[AREA_BLOCK_SIZE];

    u16  field_170;                 /* 0x170 */
    u16  field_218;                 /* 0x218 */
    u8   training_class_mask;       /* 0x550 */
    u16  max_encounter_distance;    /* 0x580 */
    u16  encounter_distance;        /* 0x582 */
    u16  field_58C;                 /* 0x58c */
    i16  field_58E;                 /* 0x58e */
    i16  field_590;                 /* 0x590 */
    i16  field_592;                 /* 0x592 */
    u16  search_flags;              /* 0x594, bit 1 searching, bit 2 looking */
    i16  field_596;                 /* 0x596 */
    i16  rest_encounter_period;     /* 0x5a4 */
    i16  rest_encounter_percentage; /* 0x5a6 */
    bool tried_to_exit_map;         /* 0x5aa */
    u8   head_block_id;             /* 0x5c2 */
    u16  enter_temple;              /* 0x5c4 */
    i16  field_5C6;                 /* 0x5c6 */
    bool is_duel;                   /* 0x5cc */
    u8   game_area;                 /* 0x624 */
    i16  field_666;                 /* 0x666 */
    u8   party_size;                /* 0x67c */
    i16  field_67E;                 /* 0x67e */
    u16  enter_shop;                /* 0x6d8 */
    i16  field_6DA;                 /* 0x6da */
    i16  field_6E0;                 /* 0x6e0 */
    i16  field_6E2;                 /* 0x6e2 */
    i16  field_6E4;                 /* 0x6e4 */

    u16  field_6F2;                 /* 0x6f2 */
    u16  field_6F4;                 /* 0x6f4 */
    u16  field_6F6;                 /* 0x6f6 */
    u16  field_6F8;                 /* 0x6f8 */
    u16  field_6FA;                 /* 0x6fa */
    u16  field_6FC;                 /* 0x6fc */
    u16  field_6FE;                 /* 0x6fe */
    u16  field_700;                 /* 0x700 */
    u16  field_702;                 /* 0x702 */
    u16  field_704;                 /* 0x704 */

    u8   field_799;                 /* 0x799 */
    u8   field_79A;                 /* 0x79a */
    u8   field_79B;                 /* 0x79b */
    u8   field_79C;                 /* 0x79c */
    u8   field_79D;                 /* 0x79d */
    u8   field_79E;                 /* 0x79e */
    u8   field_79F;                 /* 0x79f */
    u8   field_7A0;                 /* 0x7a0 */
    u8   field_7A1;                 /* 0x7a1 */
    u8   field_7A2;                 /* 0x7a2 */
    u8   field_7A3;                 /* 0x7a3 */
    u8   field_7A4;                 /* 0x7a4 */
    u8   field_7A5;                 /* 0x7a5 */
    u8   field_7A6;                 /* 0x7a6 */
    u8   field_7A7;                 /* 0x7a7 */
    u8   field_7A8;                 /* 0x7a8 */
    u8   field_7A9;                 /* 0x7a9 */
    u8   field_7AA;                 /* 0x7aa */
    u8   field_7AB;                 /* 0x7ab */
    u16  field_7EC;                 /* 0x7ec */
} Area2;

extern const DioDesc area2_desc;

void area2_clear(Area2 *a);
bool area2_read(Area2 *a, const u8 *data, size_t data_size, size_t offset);
bool area2_write(const Area2 *a, u8 *data, size_t data_size);

/* field_800_Get / field_800_Set. */
u16  area2_word_get(const Area2 *a, int loc);
void area2_word_set(Area2 *a, int loc, u16 value);

/* RestField6F2Values. */
void area2_reset_field_6F2(Area2 *a);

#endif /* COAB_AREA_H */
