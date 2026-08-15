/* protect.h - the code wheel. Ported from engine/ovr004.cs.
 *
 * The game shows two runes and asks for the character the physical translation
 * wheel lines them up with. Three wrong answers and it exits. The wheel is
 * printed in the manual, so the table below is all that is left of it.
 */
#ifndef COAB_PROTECT_H
#define COAB_PROTECT_H

#include "coab.h"

/* ovr004.copy_protection. Returns once the right character is typed; does not
 * return at all after the third wrong one. The caller checks
 * cheats.skip_copy_protection - the C# checked it at the one call site too. */
void protect_copy_protection(void);

/* The character the wheel lines up for a pair of runes, a path and a box: the
 * espruar rune 0..25, the dethek rune 0..21, one of three paths and one of six
 * boxes counted from the bottom. The C# worked this out inline; it is split out
 * here so that the one piece of arithmetic in the overlay can be checked
 * without a keyboard. Returns '\0' for a row outside the wheel. */
char protect_wheel_char(int espruar, int dethek, int code_path, int code_row);

#endif /* COAB_PROTECT_H */
