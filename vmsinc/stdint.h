/* [.VMSINC]STDINT.H - <stdint.h> for a DEC C whose header set predates C99.
 *
 * NOT USED unless CONFIGURE.COM says it is needed.  Its PROBE_INC.C compiles the
 * C99 integer headers on their own; only if that fails does CONFIGURE.COM add this
 * directory to the compiler's /INCLUDE list, by passing
 *
 *     /MACRO=("VMSINC=,[.VMSINC]")
 *
 * to MMS/MMK.  On a compiler that has the real header, this directory is never
 * on the include path and this file is inert.  That matters: /INCLUDE
 * directories are searched BEFORE the system text libraries, so if this were
 * added unconditionally it would shadow a perfectly good <stdint.h> on every
 * newer compiler.
 *
 * Why a directory of headers rather than typedefs in VMS_COMPAT.H: [.SRC]RND.C
 * has "#include <stdint.h>" as its very first line, before "rnd.h" and so
 * before anything of ours is in scope.  A macro guard in VMS_COMPAT.H cannot
 * reach it, but the include path can.  DEC C searches /INCLUDE for the
 * angle-bracket form too - [.SRC]PLATFORM_SDL1.C's "#include <SDL.h>" is found
 * that way in the normal build, which is the proof this works here.
 *
 * ---------------------------------------------------------------------------
 * The OpenVMS Alpha data model, which is what every width below rests on:
 *
 *     char       8      short      16      int        32
 *     long       32     long long  64      pointer    32 (DESCRIP.MMS default)
 *
 * Note long is 32 bits.  OpenVMS is NOT LP64 like Tru64 or Linux, which is why
 * [.SRC]RND.C's uint64_t state cannot be spelled "unsigned long" here and why
 * this file reaches for long long / __int64 instead.
 * ---------------------------------------------------------------------------
 *
 * This tree itself needs only seven of the typedefs below - int8_t, uint8_t,
 * int16_t, uint16_t, int32_t, uint32_t, uint64_t - and none of the macros.  The
 * rest is here because a partial <stdint.h> is a trap: /INCLUDE is searched
 * first, so THIS file is what any system header including <stdint.h> gets too,
 * and those headers expect the whole C99 set.  Verified rather than assumed -
 * with a seven-typedef version of this file on the include path, <inttypes.h>
 * failed with "unknown type name intmax_t", which would have surfaced as a
 * baffling error in a system header rather than anything to do with this port.
 * So the file is a complete stand-in, and the exact-width types are the only
 * part the game actually exercises.
 */
#ifndef COAB_VMS_STDINT_H
#define COAB_VMS_STDINT_H

/* --------------------------------------------------------------------------
 * <inttypes.h> FIRST, because on the compilers this file exists for it is not
 * missing - only <stdint.h> is - and it declares four of these names itself.
 *
 * The header set that has no <stdint.h> still has an <inttypes.h>, in
 * SYS$COMMON:[SYSLIB]DECC$RTLDEF.TLB, module INTTYPES, and that module contains
 *
 *     typedef __int64 int64_t;            (line 62)
 *     typedef unsigned __int64 uint64_t;  (line 63)
 *     typedef int64_t intptr_t;           (line 74)
 *     typedef uint64_t uintptr_t;         (line 75)
 *
 * A second typedef of any of those four is a HARD ERROR in DEC C - not a benign
 * duplicate -
 *
 *     %CC-E-NOLINKAGE, In this declaration, "int64_t" has no linkage and has a
 *     prior declaration in this scope at line number 92 in file [.VMSINC]stdint.h
 *
 * even though the type is the same one.  So this file must not declare them; it
 * includes <inttypes.h> and lets the RTL own them.  Doing it HERE, at the top,
 * also means the RTL's own include guard suppresses the later <inttypes.h> that
 * arrives through <SDL.h>, so the order the game happens to include things in
 * stops mattering.
 *
 * Only [.SRC]PLATFORM_SDL1.C hit this: SDL 1.2's SDL_stdinc.h includes
 * <inttypes.h>, and no other file in the tree reaches it, which is why 71 of the
 * 72 sources compiled first.  A one-file failure in the last file compiled is a
 * bad way to learn this, hence the <inttypes.h> line in CONFIGURE.COM's
 * PROBE_INC.C - the probe now includes what SDL includes.
 *
 * Guarded by __DECC because this file is also compiled against glibc, where
 * <inttypes.h> DOES include <stdint.h> - i.e. this one - and the recursion would
 * leave inttypes' own declarations looking at types that do not exist yet.
 * COAB_VMS_STDINT_NO_INTTYPES is the escape hatch for a DEC C with neither
 * header, or one whose <inttypes.h> chains to <stdint.h> the way glibc's does; it
 * makes this file define all of the types itself, as it did before.
 * CONFIGURE.COM measures which of the two works rather than assuming, and passes
 * the define through to MMS/MMK when it is the one that does.
 * -------------------------------------------------------------------------- */
#if defined(__DECC) && !defined(COAB_VMS_STDINT_NO_INTTYPES)
#include <inttypes.h>
#define COAB_VMS_STDINT_FROM_INTTYPES 1
#endif

/* --------------------------------------------------------------------------
 * The 64-bit base type.
 *
 * Asked of the data model rather than hardcoded, in this order:
 *
 *   1. plain "long" on an LP64 system.  Never OpenVMS, where long is 32 bits -
 *      but picking the same spelling the platform itself uses is what keeps this
 *      file from colliding with a header set that declares int64_t on its own.
 *      Verified: with "long long" hardcoded here, 15 of the 72 sources failed
 *      against glibc with "conflicting types for int64_t", because glibc spells
 *      it long.  Two 64-bit types are still two DIFFERENT types to C.
 *   2. "long long", standard C99, which is what /STANDARD=C99 and RELAXED_C99
 *      give and so what OpenVMS normally lands on.
 *   3. __int64, DEC C's own spelling, available in every mode including the
 *      strict ones (the leading underscores put it in the implementation's
 *      namespace, so a strict mode has no reason to hide it).  This is the
 *      branch that makes the file work on the old compilers it exists for.
 *
 * DO NOT reach for <limits.h> and LONG_MAX here, however natural that looks.
 * This tree has its OWN [.SRC]LIMITS.H - the game's caps on party size and the
 * like - and DESCRIP.MMS compiles with /INCLUDE=([.SRC],SDL), so "#include
 * <limits.h>" from this file finds THAT, not the CRTL's.  It then wants types
 * this file has not defined yet and the include cycle collapses.  Found the hard
 * way: an earlier draft did exactly this and 69 of the 72 sources stopped
 * compiling.  Predefined macros need no header and cannot be shadowed.
 * -------------------------------------------------------------------------- */
#ifndef COAB_VMS_STDINT_FROM_INTTYPES
#if defined(__LP64__) || defined(_LP64)
typedef signed long        __coab_i64;
typedef unsigned long      __coab_u64;
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
typedef signed long long   __coab_i64;
typedef unsigned long long __coab_u64;
#else
typedef __int64            __coab_i64;
typedef unsigned __int64   __coab_u64;
#endif
#endif

/* -------------------------------------------------------- exact-width types
 *
 * The six narrow ones are declared here whatever happened above.  They are safe
 * to declare twice on this compiler where the 64-bit pair is not - proved by the
 * PLATFORM_SDL1.C failure, which named int64_t, uint64_t, intptr_t and uintptr_t
 * and NOT these, with this whole file already in scope ahead of <inttypes.h>.
 * Whether that is because the RTL's module does not declare them or because DEC C
 * accepts a character-identical redeclaration, declaring them costs nothing and
 * is required if the RTL turns out not to have them. */

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
#ifndef COAB_VMS_STDINT_FROM_INTTYPES
typedef __coab_i64         int64_t;
typedef __coab_u64         uint64_t;
#endif

/* ------------------------------------------- minimum-width and fastest types
 *
 * On this data model the exact-width types are already the natural choices, so
 * these are aliases rather than anything cleverer.  C99 requires them to exist;
 * nothing in this tree uses them. */

typedef int8_t             int_least8_t;
typedef uint8_t            uint_least8_t;
typedef int16_t            int_least16_t;
typedef uint16_t           uint_least16_t;
typedef int32_t            int_least32_t;
typedef uint32_t           uint_least32_t;
typedef int64_t            int_least64_t;
typedef uint64_t           uint_least64_t;

typedef int32_t            int_fast8_t;
typedef uint32_t           uint_fast8_t;
typedef int32_t            int_fast16_t;
typedef uint32_t           uint_fast16_t;
typedef int32_t            int_fast32_t;
typedef uint32_t           uint_fast32_t;
typedef int64_t            int_fast64_t;
typedef uint64_t           uint_fast64_t;

/* -------------------------------------------------------- pointer-sized types
 *
 * DEC C predefines __INITIAL_POINTER_SIZE as 0, 32 or 64.  DESCRIP.MMS leaves
 * the Alpha default of 32-bit pointers (see VMS_COMPAT.H's note on why), but
 * this respects /POINTER_SIZE=64 anyway so that these stay correct if that
 * choice is ever revisited.  0 means the qualifier was never given, i.e. 32.
 * __LP64__ is tested alongside it for the non-VMS case, which is not hypothetical:
 * this file gets compiled against glibc on a 64-bit host as a cross-check, and a
 * 32-bit intptr_t there is too narrow to hold a pointer - which is the one thing
 * the type is for, and what CONFIGURE.COM's PROBE_INC.C now asserts.
 *
 * These two are the ONLY types in this file that another header is at all likely
 * to declare on its own - intptr_t traditionally lives in <sys/types.h> or
 * <unistd.h>, not just <stdint.h> - and a second, disagreeing typedef is a hard
 * error rather than a benign duplicate.  Verified: with this block unguarded,
 * 18 of the 72 sources failed to compile against glibc with "conflicting types
 * for intptr_t", because that header set spells it "long" and this one spells it
 * "int".  So the two conventional guard macros are honoured AND set, which stops
 * a later header from declaring it again.
 *
 * On the compiler this file exists for the question is settled before it is asked:
 * the RTL's <inttypes.h>, included at the top, declares both - as int64_t and
 * uint64_t, NOT as pointer-width - so the whole block is skipped and its choice
 * stands.  It has to; %CC-E-NOLINKAGE on intptr_t is one of the four diagnostics
 * that sent this file back to the drawing board.
 *
 * If DEC C turns out to use a third guard name, add it to both lines below;
 * that is the whole fix, and this paragraph is why. */
#if !defined(__intptr_t_defined) && !defined(_INTPTR_T_DECLARED) && \
    !defined(COAB_VMS_STDINT_FROM_INTTYPES)
#if defined(__LP64__) || defined(_LP64) || \
    (defined(__INITIAL_POINTER_SIZE) && (__INITIAL_POINTER_SIZE == 64))
typedef int64_t            intptr_t;
typedef uint64_t           uintptr_t;
#else
typedef int32_t            intptr_t;
typedef uint32_t           uintptr_t;
#endif
#define __intptr_t_defined 1
#define _INTPTR_T_DECLARED 1
#endif

/* ------------------------------------------------------------ widest types */

typedef int64_t            intmax_t;
typedef uint64_t           uintmax_t;

/* ------------------------------------------------------------------- limits
 *
 * The 64-bit bounds are written in hex and cast rather than given an LL suffix,
 * because the suffix is exactly the C99-ism a compiler reading this file may not
 * have.  Hex constants of this width are fine on Alpha in every DEC C mode. */

#define INT8_MIN           (-128)
#define INT8_MAX           127
#define UINT8_MAX          255

#define INT16_MIN          (-32767 - 1)
#define INT16_MAX          32767
#define UINT16_MAX         65535

#define INT32_MIN          (-2147483647 - 1)
#define INT32_MAX          2147483647
#define UINT32_MAX         4294967295u

/* Guarded individually, not as a block: an <inttypes.h> that declared the 64-bit
 * types may also have defined some of their bounds, and a redefinition with
 * different text is %CC-W-MACROREDEF - a warning DESCRIP.MMS happens to disable,
 * which would make it invisible rather than absent.  #ifndef is free. */
#ifndef INT64_MAX
#define INT64_MAX          ((int64_t)0x7FFFFFFFFFFFFFFF)
#endif
#ifndef INT64_MIN
#define INT64_MIN          (-INT64_MAX - 1)
#endif
#ifndef UINT64_MAX
#define UINT64_MAX         ((uint64_t)0xFFFFFFFFFFFFFFFF)
#endif

#define INT_LEAST8_MIN     INT8_MIN
#define INT_LEAST8_MAX     INT8_MAX
#define UINT_LEAST8_MAX    UINT8_MAX
#define INT_LEAST16_MIN    INT16_MIN
#define INT_LEAST16_MAX    INT16_MAX
#define UINT_LEAST16_MAX   UINT16_MAX
#define INT_LEAST32_MIN    INT32_MIN
#define INT_LEAST32_MAX    INT32_MAX
#define UINT_LEAST32_MAX   UINT32_MAX
#define INT_LEAST64_MIN    INT64_MIN
#define INT_LEAST64_MAX    INT64_MAX
#define UINT_LEAST64_MAX   UINT64_MAX

#define INT_FAST8_MIN      INT32_MIN
#define INT_FAST8_MAX      INT32_MAX
#define UINT_FAST8_MAX     UINT32_MAX
#define INT_FAST16_MIN     INT32_MIN
#define INT_FAST16_MAX     INT32_MAX
#define UINT_FAST16_MAX    UINT32_MAX
#define INT_FAST32_MIN     INT32_MIN
#define INT_FAST32_MAX     INT32_MAX
#define UINT_FAST32_MAX    UINT32_MAX
#define INT_FAST64_MIN     INT64_MIN
#define INT_FAST64_MAX     INT64_MAX
#define UINT_FAST64_MAX    UINT64_MAX

/* The first arm covers the delegating case as well as /POINTER_SIZE=64, because
 * the RTL's <inttypes.h> spells intptr_t int64_t whatever the pointer size is, and
 * bounds that disagreed with the typedef they describe would be worse than none. */
#if defined(COAB_VMS_STDINT_FROM_INTTYPES) || \
    defined(__LP64__) || defined(_LP64) || \
    (defined(__INITIAL_POINTER_SIZE) && (__INITIAL_POINTER_SIZE == 64))
#ifndef INTPTR_MIN
#define INTPTR_MIN         INT64_MIN
#define INTPTR_MAX         INT64_MAX
#endif
#ifndef UINTPTR_MAX
#define UINTPTR_MAX        UINT64_MAX
#endif
#else
#ifndef INTPTR_MIN
#define INTPTR_MIN         INT32_MIN
#define INTPTR_MAX         INT32_MAX
#endif
#ifndef UINTPTR_MAX
#define UINTPTR_MAX        UINT32_MAX
#endif
#endif

#ifndef INTMAX_MIN
#define INTMAX_MIN         INT64_MIN
#define INTMAX_MAX         INT64_MAX
#endif
#ifndef UINTMAX_MAX
#define UINTMAX_MAX        UINT64_MAX
#endif

/* ptrdiff_t and size_t are <stddef.h>'s, and on this data model both are the
 * 32-bit types unless pointers were widened.  Guarded because <stddef.h> and
 * <limits.h> variants on some header sets define these too. */
#ifndef PTRDIFF_MIN
#define PTRDIFF_MIN        INTPTR_MIN
#define PTRDIFF_MAX        INTPTR_MAX
#endif
#ifndef SIZE_MAX
#define SIZE_MAX           UINTPTR_MAX
#endif

#ifndef SIG_ATOMIC_MIN
#define SIG_ATOMIC_MIN     INT32_MIN
#define SIG_ATOMIC_MAX     INT32_MAX
#endif

/* <wchar.h> and <stddef.h> own these on some header sets; do not fight them. */
#ifndef WCHAR_MIN
#define WCHAR_MIN          0
#define WCHAR_MAX          UINT32_MAX
#endif
#ifndef WINT_MIN
#define WINT_MIN           0
#define WINT_MAX           UINT32_MAX
#endif

/* ---------------------------------------------------------- constant macros
 *
 * The 8/16/32-bit ones need no suffix at all: the value alone already has a
 * type at least that wide.  The 64-bit pair casts for the same reason INT64_MAX
 * does - the LL suffix may not exist on a compiler reading this file. */

#define INT8_C(v)          (v)
#define UINT8_C(v)         (v)
#define INT16_C(v)         (v)
#define UINT16_C(v)        (v)
#define INT32_C(v)         (v)
#define UINT32_C(v)        (v ## u)
#ifndef INT64_C
#define INT64_C(v)         ((int64_t)(v))
#endif
#ifndef UINT64_C
#define UINT64_C(v)        ((uint64_t)(v))
#endif

#endif /* COAB_VMS_STDINT_H */
