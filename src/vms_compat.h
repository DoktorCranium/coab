/* vms_compat.h - OpenVMS 8.4 Alpha (DEC C) compatibility shims.
 *
 * Pulled in by coab.h, before anything else, when the compiler predefines
 * __VMS.  Nothing includes this file directly and nothing outside it tests
 * __VMS except the four places listed at the bottom, so the whole platform
 * divergence is visible from here.
 *
 * Modelled on the sibling OpenVMS ports' shims - [-.ROTT.ROTT]VMS_COMPAT.H and
 * [-.CHOCOLATE_DUKE.ENGINE.SRC]VMS_COMPAT.H - but much smaller, because this
 * tree was written as portable C99 rather than reverse-engineered 1994 DOS C:
 * there is no Watcom inline asm, no DPMI, no __int64, no far pointers, no
 * strcmpi/stricmp/filelength, and no 16-bit segment arithmetic to emulate.
 *
 * See PORTING-VMS.md for the whole story; the comments here cover only what a
 * reader of the source needs.
 */
#ifndef COAB_VMS_COMPAT_H
#define COAB_VMS_COMPAT_H

#ifdef __VMS

/* --------------------------------------------------------------------------
 * GCC function attributes.
 *
 * Seven declarations carry __attribute__((format(printf,N,M))) or
 * ((noreturn)): log.h's log_write and log_fatal, quit.h's game_print_and_exit
 * and game_print_and_exit_msg, ecl.h's ecl_vm_log, and savegame.c's two static
 * record loggers.  These are GCC diagnostics with no DEC C equivalent, and DEC
 * C treats the unknown keyword as a syntax error at the declaration - i.e. the
 * build stops on the first file that includes log.h, which is all of them.
 *
 * Defining the keyword away is the standard shim and costs only printf-argument
 * checking, which the Linux and macOS builds already do (-Wall -Wextra, warning
 * free).  Doing it here rather than editing seven declarations keeps the
 * checking on the platforms that can perform it.
 *
 * Note the double parentheses at the use sites: __attribute__((a,b)) passes
 * "a,b" as ONE macro argument because the comma sits inside the inner parens,
 * so a one-parameter macro is right even for ((format(printf,1,2),noreturn)).
 *
 * Losing ((noreturn)) on game_print_and_exit is harmless - it is a warning and
 * optimisation hint, and the function really does longjmp out - but it is why
 * MISSINGRETURN is in DESCRIP.MMS's disabled-diagnostics list: DEC C can now
 * see a fallthrough tail where GCC could not.
 * -------------------------------------------------------------------------- */
#ifndef __attribute__
#define __attribute__(x)
#endif

/* --------------------------------------------------------------------------
 * The unprefixed VMS macro, which SDL 1.2's headers require.
 *
 * SDL's begin_code.h does
 *
 *     #ifndef DECLSPEC
 *     #ifdef VMS
 *     #define DECLSPEC
 *     #endif
 *     #endif
 *
 * and has NO #else.  So if VMS is not defined, DECLSPEC is never defined at
 * all, and every prototype in every SDL header - "extern DECLSPEC int SDLCALL
 * SDL_Init(Uint32)" - becomes a declaration beginning with an unknown
 * identifier.  That is a syntax error on the first line of SDL_error.h and the
 * diagnostics that follow name DECLSPEC, not VMS, so it is not an obvious
 * failure to read.
 *
 * DEC C predefines the unprefixed VMS only in its relaxed modes; a strict
 * /STANDARD suppresses every unprefixed predefined macro (VMS, vax, alpha,
 * unix) and keeps only the __-prefixed ones, of which SDL knows nothing.
 * Defining it here rather than in DESCRIP.MMS keeps the two in step whatever
 * $(CSTD) turns out to be, and only ever adds a macro DEC C would have added
 * itself.
 *
 * Reached in time because platform_sdl1.c - the one file that includes <SDL.h>
 * - includes "platform.h" first, and that includes coab.h, and that includes
 * this.  Do not reorder those.
 * -------------------------------------------------------------------------- */
#ifndef VMS
#define VMS 1
#endif

/* --------------------------------------------------------------------------
 * strcasecmp lives in <strings.h> on VMS, not <string.h>.
 *
 * Three call sites - vfs.c's vfs_save_resolve and save_name_cmp, and
 * savegame.c's .sav suffix test.  All three include <string.h> only, which is
 * correct on Linux and macOS (glibc and libc++ both declare strcasecmp there
 * via _DEFAULT_SOURCE) and leaves it implicitly declared on VMS.  An implicit
 * declaration returning int happens to work here, but %CC-I-IMPLICITFUNC on
 * three files in every build is noise, and DEC C is entitled to reject it.
 *
 * Included from here rather than added to those two files so that the include
 * lists stay as the reference build has them.
 * -------------------------------------------------------------------------- */
#include <strings.h>

/* --------------------------------------------------------------------------
 * Binary file access - THE ONE THING THAT SILENTLY CORRUPTS DATA ON VMS.
 *
 * "rb"/"wb" is accepted and does nothing: there is no text/binary distinction
 * in the CRTL, and what actually decides whether a stream of bytes survives is
 * the RMS record layer.  A file opened without "ctx=stm" is read and written a
 * RECORD at a time, so record boundaries are inserted, stripped or padded and
 * the byte offsets that DAX block reading and savegame record writing depend on
 * no longer line up.  It cannot be folded into the mode string - it is a
 * variadic argument to fopen - which is why this is a function and not a macro.
 *
 * Every binary open in the tree therefore goes through vfs_fopen() (vfs.c):
 * the DAX reads in vfs_read_file, the savegame and character-roster opens in
 * fileio.c's file_assign, the self-test's PPM dumps in display.c and its
 * synthetic save in selftest.c.  log.c's fopen is deliberately NOT one of them
 * - the log is text, and leaving it in the default variable-record format is
 * what makes TYPE and EDIT work on it.
 *
 * The two macros below are what vfs_fopen expands to; they are here so that
 * "SEARCH [.SRC]*.* VMS_FOPEN" finds the moving parts from either end.
 *
 * VMS_FOPEN_CREATE additionally asks for stream-LF, the format every other
 * byte-stream file on a VMS system uses (it is what UNZIP and GZIP produce).
 * Without it a newly created file gets the CRTL's default variable-length
 * records, and although "ctx=stm" would still read it back correctly on this
 * system, nothing else on the machine could - and a savegame copied from a DOS
 * or Windows install, which is stream-LF, would not match the files this port
 * writes.  RMS ignores rfm on an open of an EXISTING file, so it is only ever
 * applied at creation.
 * -------------------------------------------------------------------------- */
#define VMS_FOPEN_READ(path, mode)    fopen((path), (mode), "ctx=stm")
#define VMS_FOPEN_CREATE(path, mode)  fopen((path), (mode), "ctx=stm", \
                                            "rfm=stmlf")

/* --------------------------------------------------------------------------
 * What is NOT here, and why.
 *
 *   No <execinfo.h> shim.  Nothing in this tree calls backtrace(); the sibling
 *   ROTT and Duke ports needed one because their DOS-era error handlers dumped
 *   a stack.
 *
 *   No snprintf fallback.  All ~700 uses assume the C99 function.  The CRTL on
 *   OpenVMS 8.4 has it; CONFIGURE.COM probes for it anyway and says so, because
 *   older CRTLs do not and the failure is otherwise a mystery.  See
 *   PORTING-VMS.md for the fallback to add if you meet one.
 *
 *   No clock_gettime fallback.  Two call sites, rnd.c's seeding and text.c's
 *   time01, both asking only for CLOCK_REALTIME and neither on a hot path.
 *   OpenVMS 8.4's CRTL has it.  If yours does not, PORTING-VMS.md has the
 *   gettimeofday() shim to paste in right here - it is four lines - and
 *   CONFIGURE.COM tells you whether you need it.
 *
 *   No <stdint.h>/<stdbool.h> substitutes HERE - they are in [.VMSINC] instead,
 *   which is a separate directory rather than more typedefs in this file for one
 *   concrete reason: [.SRC]RND.C's first line is "#include <stdint.h>", before
 *   "rnd.h" and so before this file is in scope at all.  Nothing this header
 *   defines can reach it; only the include path can.  CONFIGURE.COM adds
 *   [.VMSINC] to /INCLUDE only if the real headers are missing, which they are on
 *   any DEC C before V7.  See PORTING-VMS.md and the header comments in the two
 *   files - the <limits.h> trap described there is a real bug that was hit, not a
 *   hypothetical.
 *
 *   No 64-bit pointer qualifier.  DESCRIP.MMS leaves DEC C's Alpha default of
 *   32-bit pointers, as every other port in this tree does.  This one is a
 *   consistency choice rather than a correctness one: no pointer here is ever
 *   stored in an integer (checked - the engine's u32 fields hold DAX offsets,
 *   never addresses), so /POINTER_SIZE=64 would also work.
 *
 * The other four places that test __VMS, all in vfs.c and all about RMS or
 * ODS-2 rather than about the compiler:
 *
 *   vfs_fopen        the "ctx=stm" opens above.
 *   dir_spec         stat() and opendir() need "dir/" or "[dir]" for a
 *                    DIRECTORY; a bare "dir" or "./dir" names a FILE of that
 *                    name.  Called by path_is_dir, index_dir and the two
 *                    savegame scans.
 *   strip_version    readdir() reports "NAME.EXT;1", version included.  Called
 *                    by index_dir and the two savegame scans.
 *   user_dir         the save and log directories.  $HOME/.local/share is
 *                    neither a legal ODS-2 name nor a path the CRTL can
 *                    translate, so both land in the current default directory,
 *                    exactly as the sibling ROTT port does it.
 * -------------------------------------------------------------------------- */

#endif /* __VMS */

#endif /* COAB_VMS_COMPAT_H */
