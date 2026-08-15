/* set.h - the 256-bit set the engine uses for "is this one of ..." tests.
 * Ported from Classes/Set.cs.
 *
 * The original was a Turbo Pascal set: 0x20 bytes, one bit per possible member,
 * bit n of byte n/8. Monster type tests, spell target masks and ECL flag checks
 * all use it.
 */
#ifndef COAB_SET_H
#define COAB_SET_H

#include "coab.h"

#define SET_BYTES   0x20
#define SET_MEMBERS (SET_BYTES * 8)   /* 256 */

typedef struct {
    u8 bits[SET_BYTES];
} Set;

void set_clear(Set *s);

/* Set.operator+ and Set.SetBit: adds one member. Out-of-range members are
 * ignored rather than corrupting the byte after the set. */
void set_add(Set *s, int member);

/* Set.SetRange. Note the original's argument order: the high bound comes
 * first, so the loop runs from low to high. */
void set_add_range(Set *s, int high, int low);

bool set_member_of(const Set *s, int member);

/* Set(ushort, byte[]): the high byte of packed is a count of leading zero bytes
 * and the low byte is how many bytes to copy from src after them. Used by the
 * ECL interpreter, which builds sets inline in the script stream.
 *
 * The C# constructor zeroed the array in three loops, the last of which stopped
 * at 20 rather than 0x20; the array was freshly allocated, so the short loop had
 * no effect. Clearing the whole set here is the same result. */
void set_init_packed(Set *s, u16 packed, const u8 *src, size_t src_size);

#endif /* COAB_SET_H */
