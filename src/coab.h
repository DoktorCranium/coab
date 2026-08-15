/* coab.h - shared types and constants for the Curse of the Azure Bonds port.
 *
 * Ported from the C# reverse-engineering of the 1989 DOS release. The C# tree
 * one directory up remains the reference implementation; file/function names
 * here keep their original seg###/ovr### provenance in comments so the two can
 * be diffed by hand.
 */
#ifndef COAB_H
#define COAB_H

/* OpenVMS shims - the GCC attribute spellings and <strings.h>, both of which
 * have to be in hand before any other header is read. Empty everywhere else. */
#include "vms_compat.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef int8_t   i8;
typedef uint16_t u16;
typedef int16_t  i16;
typedef uint32_t u32;
typedef int32_t  i32;

/* EGA mode 0Dh. Pixels are not square: the 320x200 frame was displayed on a
 * 4:3 monitor, so correct presentation stretches it to 320x240. */
#define EGA_W        320
#define EGA_H        200
#define EGA_COLORS   16

/* Text grid: 40 columns x 25 rows of 8x8 cells. */
#define TEXT_COLS    40
#define TEXT_ROWS    25

/* Color index 16 is the transparency marker DaxBlock uses for masked art;
 * SetPixel3 ignores anything >= 16, which is how masking takes effect. */
#define COLOR_MASK   16

#define COAB_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#ifndef COAB_MIN
#define COAB_MIN(a, b) ((a) < (b) ? (a) : (b))
#define COAB_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* Classes/Sys.cs - little-endian accessors over raw DAX bytes. */
static inline i16 sys_array_to_short(const u8 *d, int off)
{
    return (i16)((u16)d[off] | ((u16)d[off + 1] << 8));
}

static inline u16 sys_array_to_ushort(const u8 *d, int off)
{
    return (u16)((u16)d[off] | ((u16)d[off + 1] << 8));
}

static inline i32 sys_array_to_int(const u8 *d, int off)
{
    return (i32)((u32)d[off] | ((u32)d[off + 1] << 8) |
                 ((u32)d[off + 2] << 16) | ((u32)d[off + 3] << 24));
}

int  sys_wrap_min_max(int val, int min, int max);

/* Text regions used by press_any_key (Classes/Display.cs: TextRegion). */
typedef enum {
    TEXT_REGION_NORMAL_BOTTOM = 0,
    TEXT_REGION_NORMAL2       = 1,
    TEXT_REGION_COMBAT_SUMMARY = 2
} TextRegion;

/* Sound ids as the engine passes them around (Classes/Gbl.cs: enum Sound). */
typedef enum {
    SOUND_FF = -1,          /* stop everything */
    SOUND_0  = 0,           /* stop everything */
    SOUND_1  = 1,
    SOUND_2  = 2,
    SOUND_3  = 3,
    SOUND_4  = 4,
    SOUND_5  = 5,
    SOUND_6  = 6,
    SOUND_ATTACK_HELD = 7,
    SOUND_8  = 8,
    SOUND_9  = 9,
    SOUND_A  = 0x0a,
    SOUND_B  = 0x0b,
    SOUND_C  = 0x0c,
    SOUND_D  = 0x0d,
    SOUND_E  = 0x0e,
    SOUND_F  = 0x0f
} Sound;

#endif /* COAB_H */
