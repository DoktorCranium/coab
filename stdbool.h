/* [.VMSINC]STDBOOL.H - <stdbool.h> for a DEC C whose header set predates C99.
 *
 * Same deal as [.VMSINC]STDINT.H next to it: never on the include path unless
 * CONFIGURE.COM found the real one missing.  Read that file's header comment for
 * why this is a directory of headers rather than typedefs in VMS_COMPAT.H.
 *
 * 119 files in this tree use bool, true and false, so this is not optional if
 * the compiler lacks the header.
 *
 * ---------------------------------------------------------------------------
 * The one risk worth naming: _Bool versus int.
 *
 * If the compiler has the C99 _Bool keyword, use it, because it has the
 * behaviour the code was written against - any nonzero value stored in a _Bool
 * becomes exactly 1, so "b = 2; if (b == true)" is TRUE.  Fall back to int and
 * that same comparison is FALSE, because the 2 is kept.
 *
 * This tree does not rely on that normalisation (the bool fields are assigned
 * from comparisons and from true/false literals, and are tested with "if (b)",
 * "== true" and "== false" against values that are already 0 or 1 - the
 * aftercombat.c "non_team_member == true" tests are typical), so the int
 * fallback is safe HERE.  It is still the sharp edge in this file, so if you
 * ever see a bool comparison behave oddly on a compiler using the fallback
 * branch, this comment is the reason.
 *
 * sizeof(bool) also changes between the two branches, 1 versus 4.  Nothing in
 * this tree serialises a bool - savegame and DAX records are read and written
 * through explicit u8/u16/u32 accessors in [.SRC]COAB.H, never by dumping a
 * struct - so no file format depends on it.
 * ---------------------------------------------------------------------------
 */
#ifndef COAB_VMS_STDBOOL_H
#define COAB_VMS_STDBOOL_H

#ifndef __cplusplus

/* DEC C spells the C99 keyword _Bool once it has it at all.  There is no
 * portable way to ask "does this compiler have _Bool"; __STDC_VERSION__ is the
 * available proxy, and it is the right one, since _Bool and the header arrived
 * together. */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#define bool  _Bool
#else
#define bool  int
#endif

#define true  1
#define false 0

/* Required by C99 so that code can test whether the macros above are the real
 * thing; also what stops a later, genuine <stdbool.h> from redefining these if
 * both ever end up in one translation unit. */
#define __bool_true_false_are_defined 1

#endif /* !__cplusplus */

#endif /* COAB_VMS_STDBOOL_H */
