$! ===========================================================================
$! CONFIGURE.COM - Curse of the Azure Bonds (C/SDL port) for OpenVMS 8.4 Alpha
$!
$! Prerequisites, to be defined before running this:
$!   $ DEFINE LIBSDL  <device:[dir]>    ! holds LIBSDL$SHR.OPT / LIBSDL.OPT
$!   $ DEFINE SDL     <device:[dir]>    ! holds SDL.H
$!
$! SDL_mixer is NOT required, and neither is SDL's audio subsystem: this is a
$! no-sound build.  See DESCRIP.MMS.
$! ===========================================================================
$ SET NOON
$!
$! Every symbol the CLEANUP labels test, defined up front: any check below can
$! GOTO EXIT, and EXIT falls into CLEANUP_KEEP, which would otherwise reference
$! a symbol that check had not reached yet.
$ HAVE_CSTD = 0
$ HAVE_IO   = 0
$ KEEP_SDL  = 0
$ HAVE_CLOCK = 0
$!
$! The <stdint.h>/<stdbool.h> shim in [.VMSINC], for DEC C before V7, which has
$! neither.  VMSINC is what gets handed to MMS/MMK and MUST keep its leading
$! comma when set, because DESCRIP.MMS splices it straight into
$! INCS = ([.SRC],SDL$(VMSINC)).  Empty means "this compiler has the real
$! headers", which is the normal case and the safe default: /INCLUDE is searched
$! before the system text libraries, so a non-empty value on a compiler that does
$! not need it would SHADOW a perfectly good <stdint.h>.
$!
$! HAVE_VMSINC records whether the directory is actually here, so that a source
$! tree transferred without it gets told so instead of failing 72 times over.
$! SHIMINC is the same decision spelled as a /INCLUDE for the probes; HDR_FATAL is
$! set when the headers are missing and cannot be supplied.  Each F$SEARCH gets its
$! own stream-id, as everywhere else here, so no lookup can be answered from
$! another one's context.
$!
$! VMSDEFS/SHIMDEF are the second half of the same decision.  [.VMSINC]STDINT.H
$! normally delegates the 64-bit and pointer-sized types to the RTL's <inttypes.h>,
$! which declares them itself on exactly the compilers that have no <stdint.h>;
$! COAB_VMS_STDINT_NO_INTTYPES makes it declare all of them on its own instead, for
$! a header set that has neither.  Which of the two compiles is measured below, not
$! assumed, and VMSDEFS carries the answer through to MMS/MMK.
$ VMSINC      = ""
$ VMSDEFS     = ""
$ SHIMINC     = ""
$ SHIMDEF     = ""
$ HDR_FATAL   = 0
$ HAVE_VMSINC = 0
$ IF F$SEARCH("[.VMSINC]STDINT.H", 901) .NES. "" .AND. -
     F$SEARCH("[.VMSINC]STDBOOL.H", 902) .NES. "" THEN HAVE_VMSINC = 1
$!
$! The qualifiers every probe below is compiled with.  They are DESCRIP.MMS's,
$! minus the optimiser, and they are a symbol rather than written out five times
$! because a probe built with DIFFERENT qualifiers from the real build is not
$! answering the question that was asked - /NAME=(AS_IS,SHORT) in particular
$! changes which CRTL entry points the linker looks for.
$!
$! /WARNINGS=(DISABLE=...) is the same list DESCRIP.MMS disables, and here it
$! only keeps the probe logs readable: BUILD_PROBE below decides success by
$! looking for the OBJECT and the EXECUTABLE, so a diagnostic cannot fail a
$! probe that actually built.  See the comment on BUILD_PROBE for why that
$! matters.
$ CQUAL = "/NAME=(AS_IS,SHORT)/FLOAT=IEEE/IEEE=DENORM/MEMBER_ALIGNMENT"
$ CWARN = "/WARNINGS=(DISABLE=(MISSINGRETURN,EMPTYFILE,PTRMISMATCH,PTRMISMATCH1,CVTDIFTYPES,QUESTCOMPARE,LONGEXTERN,MACROREDEF,UNDEFVARMOD))"
$!
$! Bumped before every F$SEARCH in BUILD_PROBE and used as its stream-id.  A
$! stream-id of its own gives each search a fresh context, so a probe file that
$! was created or deleted since the last look is seen as it is now rather than
$! as it was.
$ P_SEQ = 0
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "Configuring Curse of the Azure Bonds (C/SDL port) for OpenVMS"
$ WRITE SYS$OUTPUT "  (SDL 1.2, no sound)"
$ WRITE SYS$OUTPUT " "
$!
$! --- Architecture and OS -----------------------------------------------------
$ IF F$GETSYI("ARCH_TYPE").EQ.1 THEN CPU = "VAX"
$ IF F$GETSYI("ARCH_TYPE").EQ.2 THEN CPU = "Alpha"
$ IF F$GETSYI("ARCH_TYPE").EQ.3 THEN CPU = "I64"
$ OS = F$GETSYI("VERSION")
$ WRITE SYS$OUTPUT "Checking architecture        ...  ", CPU
$ WRITE SYS$OUTPUT "Checking OS                  ...  OpenVMS ", OS
$ IF (CPU .EQS. "VAX")
$  THEN
$       WRITE SYS$OUTPUT " "
$       WRITE SYS$OUTPUT "VAX is not supported.  This tree is C99 throughout -"
$       WRITE SYS$OUTPUT "<stdint.h>, <stdbool.h>, declarations inside for()"
$       WRITE SYS$OUTPUT "initialisers, static inline in headers - and DEC C for"
$       WRITE SYS$OUTPUT "VAX has no C99 mode and no <stdint.h>.  There is no"
$       WRITE SYS$OUTPUT "small change that would fix this; every one of the 72"
$       WRITE SYS$OUTPUT "sources would have to be rewritten.  Alpha or I64 is"
$       WRITE SYS$OUTPUT "required."
$       GOTO EXIT
$ ENDIF
$!
$! --- Compiler ----------------------------------------------------------------
$ DECC = F$SEARCH("SYS$SYSTEM:DECC$COMPILER.EXE") .NES. ""
$ IF (DECC)
$  THEN
$       WRITE SYS$OUTPUT "Checking compiler            ...  DEC C"
$  ELSE
$       WRITE SYS$OUTPUT "Checking compiler            ...  not found"
$       WRITE SYS$OUTPUT "DEC C is required."
$       GOTO EXIT
$ ENDIF
$!
$! --- Build utility -----------------------------------------------------------
$ MMS = F$SEARCH("SYS$SYSTEM:MMS.EXE") .NES. ""
$ MMK = F$TYPE(MMK)
$ IF (.NOT. MMS) .AND. (MMK .EQS. "")
$  THEN
$       WRITE SYS$OUTPUT "Checking build utility       ...  none"
$       WRITE SYS$OUTPUT "Either MMS or MMK is required."
$       GOTO EXIT
$ ENDIF
$ IF (MMK .NES. "") THEN MAKE = "MMK"
$ IF (MMS)          THEN MAKE = "MMS"
$ WRITE SYS$OUTPUT "Checking build utility       ...  ''MAKE'"
$!
$! --- The sources -------------------------------------------------------------
$! Cheap, and it catches the commonest mistake: running this from the wrong
$! directory, or from a copy where [.SRC] was not unpacked.
$ IF F$SEARCH("[.SRC]MAIN.C") .EQS. ""
$  THEN
$       WRITE SYS$OUTPUT "Checking for [.SRC]MAIN.C    ...  not found"
$       WRITE SYS$OUTPUT " "
$       WRITE SYS$OUTPUT "Run this from the directory holding DESCRIP.MMS, with"
$       WRITE SYS$OUTPUT "the 72 sources in [.SRC] beneath it."
$       GOTO EXIT
$ ENDIF
$ WRITE SYS$OUTPUT "Checking for the sources     ...  [.SRC]MAIN.C found"
$!
$! --- SDL headers and libraries -----------------------------------------------
$ IF F$TRNLNM("SDL") .EQS. ""
$  THEN
$       WRITE SYS$OUTPUT "Checking logical SDL         ...  not defined"
$       WRITE SYS$OUTPUT " "
$       WRITE SYS$OUTPUT "Define it to the directory holding SDL.H, e.g."
$       WRITE SYS$OUTPUT "   $ DEFINE SDL  DKA0:[SDL.INCLUDE]"
$       GOTO EXIT
$ ENDIF
$ WRITE SYS$OUTPUT "Checking logical SDL         ...  ", F$TRNLNM("SDL")
$ IF F$TRNLNM("LIBSDL") .EQS. ""
$  THEN
$       WRITE SYS$OUTPUT "Checking logical LIBSDL      ...  not defined"
$       WRITE SYS$OUTPUT " "
$       WRITE SYS$OUTPUT "Define it to the directory holding the SDL linker"
$       WRITE SYS$OUTPUT "options files, e.g."
$       WRITE SYS$OUTPUT "   $ DEFINE LIBSDL  DKA0:[SDL.LIB]"
$       GOTO EXIT
$ ENDIF
$ WRITE SYS$OUTPUT "Checking logical LIBSDL      ...  ", F$TRNLNM("LIBSDL")
$!
$! ============================================================================
$!
$ CREATE PROBE_KW.C
int main(void)
{
    return 0;
}
$ CREATE PROBE_INC.C
/* Generated by CONFIGURE.COM.  Deleted on success; left behind on failure. */
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

typedef char coab_ck8[(sizeof(int8_t) == 1 && sizeof(uint8_t) == 1) ? 1 : -1];
typedef char coab_ck16[(sizeof(int16_t) == 2 && sizeof(uint16_t) == 2) ? 1 : -1];
typedef char coab_ck32[(sizeof(int32_t) == 4 && sizeof(uint32_t) == 4) ? 1 : -1];
typedef char coab_ck64[(sizeof(int64_t) == 8 && sizeof(uint64_t) == 8) ? 1 : -1];
typedef char coab_ckmax[(sizeof(intmax_t) >= 8 && sizeof(uintmax_t) >= 8) ? 1 : -1];
typedef char coab_ckptr[(sizeof(intptr_t) >= sizeof(char *)) ? 1 : -1];
typedef char coab_ckupt[(sizeof(uintptr_t) >= sizeof(char *)) ? 1 : -1];

int main(void)
{
    uint64_t  wide = UINT64_C(0x0123456789ABCDEF);
    uint32_t  narrow = UINT32_C(0x89ABCDEF);
    intptr_t  here = 0;
    uintptr_t there = 0;
    bool      t = true;
    bool      f = false;

    if (!t || f) return 1;
    if (here != 0 || there != 0) return 1;
    if ((uint32_t)wide != narrow) return 1;
    if (INT8_MAX != 127 || UINT8_MAX != 255) return 1;
    if (INT16_MAX != 32767 || INT32_MAX != 2147483647) return 1;
    if (INT64_MAX <= (int64_t)INT32_MAX) return 1;
    return 0;
}
$ CREATE PROBE_STD.C
/* Generated by CONFIGURE.COM.  Deleted on success; left behind on failure so
   that you can compile it by hand and read the diagnostics. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

/* (1) C99: static inline in a header-like position, over the fixed-width types.
   This is [.SRC]COAB.H's sys_array_to_ushort, which every record read goes
   through. */
static inline uint16_t p_rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* (2) C99: a variadic macro.  This is the shape of [.SRC]LOG.H's log_info and
   its three siblings, which is how every diagnostic in the tree is written, so
   a mode without __VA_ARGS__ cannot compile a single file. */
#define P_FMT(dst, ...) snprintf((dst), sizeof(dst), __VA_ARGS__)

/* (4) the three CRTL names with side effects, taken as addresses so that each
   one needs its real declaration and each symbol is resolved at link time,
   without anything happening to the file system.  File scope and initialised
   here on purpose: an initialised array of addresses has to exist in the
   object's data, so the linker really does have to resolve all three.

   Cast to a function pointer of an unrelated type, NOT to void * (which ISO C
   forbids for a function pointer, so a strict mode would rightly complain) and
   NOT to a function pointer with the real prototype written out - see
   CONFIGURE.COM for why that last one is the trap. */
static void (*const p_crtl_syms[])(void) = {
    (void (*)(void))mkdir,
    (void (*)(void))unlink,
    (void (*)(void))ftruncate
};

int main(void)
{
    uint8_t  raw[2];
    char     buf[64];
    bool     ok = true;
    /* (3) C99: a 64-bit unsigned type, and a hex constant that only fits in one.
       This is [.SRC]RND.C's generator state.  Note that `long` is 32 bits in the
       VMS Alpha data model, so this really does need long long behind it. */
    uint64_t mix = 0x9e3779b97f4a7c15u;
    DIR          *d;
    struct dirent *de;
    struct tm     tmb;
    struct stat   st;
    time_t        now;
    char         *dup;
    FILE         *mark;
    size_t        i;

    raw[0] = 0x34;
    raw[1] = 0x12;

    /* declaration inside a for() initialiser */
    for (int i = 0; i < 2; i++) {
        if (raw[i] == 0) ok = false;
    }

    /* declaration after a statement, and snprintf */
    int n = snprintf(buf, sizeof(buf), "%u", (unsigned)p_rd16(raw));
    if (n <= 0 || p_rd16(raw) != 0x1234) ok = false;

    /* the variadic macro and the 64-bit arithmetic */
    if (P_FMT(buf, "%s-%u", "x", 1u) <= 0) ok = false;
    mix ^= (uint64_t)1 << 44;
    if (mix == 0x9e3779b97f4a7c15u || sizeof mix != 8) ok = false;

    /* the rest of the CRTL surface, used through its real types */
    d = opendir("./");
    if (d != NULL) {
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == 0) ok = false;
        }
        closedir(d);
    }
    if (stat("./", &st) == 0 && !S_ISDIR(st.st_mode)) ok = false;
    if (strcasecmp("AbC", "aBc") != 0) ok = false;
    now = time(NULL);
    if (localtime_r(&now, &tmb) == NULL) ok = false;
    dup = strdup("x");
    if (dup == NULL) ok = false;
    free(dup);
    if (getpid() == 0) ok = false;
    if (fileno(stdout) < 0) ok = false;
    for (i = 0; i < sizeof p_crtl_syms / sizeof p_crtl_syms[0]; i++) {
        if (p_crtl_syms[i] == NULL) ok = false;
    }

    /* (5) the four-argument fopen.  Neither ISO nor POSIX - it is the VMS RMS
       extension, and [.SRC]VMS_COMPAT.H's VMS_FOPEN_READ and VMS_FOPEN_CREATE
       put it under every file the game opens, so a mode that rejects it cannot
       compile [.SRC]VFS.C.  Asked separately from the POSIX names above because
       it fails differently: a strict mode can declare fopen with the two-argument
       ISO prototype and then reject the extra arguments even with
       __HIDE_FORBIDDEN_NAMES out of the way.  Worth its own test rather than
       assumed - PROBE_RT.C failed to BUILD on a compiler where every POSIX name
       here compiled, and this is the difference between the two files. */
    mark = fopen("PROBE_STD.TMP", "wb", "ctx=stm", "rfm=stmlf");
    if (mark == NULL) {
        ok = false;
    } else {
        fclose(mark);
        remove("PROBE_STD.TMP");
    }

    if (!ok) {
        return 2;
    }

    /* A marker file rather than an exit status, the same way PROBE_RT.C reports
       its two answers: DCL can then test F$SEARCH instead of decoding a C return
       value into a VMS condition code, and - the reason it matters here - an
       image that fails to ACTIVATE cannot be mistaken for one that ran and
       passed. */
    mark = fopen("PROBE_STD.OK", "w");
    if (mark == NULL) {
        return 2;
    }
    fclose(mark);
    return 0;
}
$!
$ CSTD = ""
$ HAVE_CSTD = 0
$ GOSUB CHECK_HEADERS
$ IF (HDR_FATAL .EQ. 1) THEN GOTO EXIT
$ TRY = "/STANDARD=RELAXED_C99"
$ TAG = "STD_RC99"
$ GOSUB TRY_STD
$ IF (HAVE_CSTD .EQ. 1) THEN GOTO STD_DONE
$ TRY = "/STANDARD=C99"
$ TAG = "STD_C99"
$ GOSUB TRY_STD
$ IF (HAVE_CSTD .EQ. 1) THEN GOTO STD_DONE
$ TRY = "/STANDARD=RELAXED"
$ TAG = "STD_RLX"
$ GOSUB TRY_STD
$ IF (HAVE_CSTD .EQ. 1) THEN GOTO STD_DONE
$ TRY = ""
$ TAG = "STD_DEF"
$ GOSUB TRY_STD
$ IF (HAVE_CSTD .EQ. 1) THEN GOTO STD_DONE
$ TRY = "/STANDARD=RELAXED_C99/UNDEFINE=__HIDE_FORBIDDEN_NAMES"
$ TAG = "STD_RC99N"
$ GOSUB TRY_STD
$ IF (HAVE_CSTD .EQ. 1) THEN GOTO STD_DONE
$ TRY = "/STANDARD=C99/UNDEFINE=__HIDE_FORBIDDEN_NAMES"
$ TAG = "STD_C99N"
$ GOSUB TRY_STD
$STD_DONE:
$!
$ IF (HAVE_CSTD .EQ. 0)
$  THEN
$       GOSUB PICK_KEYWORD
$       CSTDSHOW = CSTD
$       IF (CSTD .EQS. "") THEN CSTDSHOW = "(compiler default)"
$       WRITE SYS$OUTPUT "Checking C99 + CRTL names    ...  unproven, assuming ''CSTDSHOW'"
$       GOTO STD_REPORTED
$ ENDIF
$ IF (CSTD .EQS. "")
$  THEN
$       WRITE SYS$OUTPUT "Checking C99 + CRTL names    ...  Yes (compiler default)"
$  ELSE
$       WRITE SYS$OUTPUT "Checking C99 + CRTL names    ...  Yes (''CSTD')"
$ ENDIF
$STD_REPORTED:
$!
$! ============================================================================
$! SDL 1.2 - compile, link and RUN, the same three-stage check the siblings use
$! ============================================================================
$! Compiling alone is not enough: it is the LINK that tells us shared versus
$! static, and only RUNning proves the shared image actually activates.
$!
$! SDL_Init(0) rather than SDL_INIT_VIDEO, so this works over a session with no
$! display - and NEVER SDL_INIT_AUDIO, which on a port built with
$! SDL_AUDIO_DISABLED (the normal OpenVMS case) would fail the whole call and
$! make a perfectly good SDL look broken.
$ HAVE_LIBSDL = 0
$ CREATE PROBE_SDL.C
/* Generated by CONFIGURE.COM.  Left behind if it does not build, link or run,
   because this is the check most likely to fail on a fresh system and the
   compiler's and linker's own diagnostics are worth more than a guess.
   Rebuild it by hand with the CC and LINK lines this file prints. */

/* SDL 1.2's begin_code.h defines DECLSPEC only "#ifdef VMS", with no #else, so
   an undefined VMS leaves every SDL prototype starting with an unknown
   identifier.  DEC C predefines the unprefixed VMS only in its relaxed modes, so
   this probe must not depend on it - [.SRC]VMS_COMPAT.H does exactly the same
   thing for the real build, and the two have to agree or this probe would report
   a perfectly good SDL as broken under a strict $(CSTD). */
#ifndef VMS
#define VMS 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

int main(int argc, char *argv[])
{
    const SDL_version *v;
    FILE *mark;

    /* Not SDL_INIT_VIDEO and never SDL_INIT_AUDIO - see CONFIGURE.COM. */
    if (SDL_Init(0) < 0)
    {
        fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
        exit(2);
    }

    v = SDL_Linked_Version();
    printf("checking version libSDL  : %d.%d.%d\n", v->major, v->minor, v->patch);

    /* This port's SDL-1.2 backend, [.SRC]PLATFORM_SDL1.C, is written to the 1.2
       API: SDL_SetVideoMode returning a plain SDL_Surface we write pixels into,
       SDL_SetColors on an 8-bit palettised surface, SDL_UpdateRect,
       SDL_WM_SetCaption, the SDLKey/SDLMod keysyms and SDL_EnableKeyRepeat.
       SDL 2 renamed or removed every one of them, and the palettised display
       surface - which is how a 16-colour EGA game gets its palette onto the
       screen - was dropped outright.  So refuse anything but 1.2. */
    if (v->major != 1 || v->minor != 2)
    {
        printf("This port needs SDL 1.2, not %d.%d.\n", v->major, v->minor);
        SDL_Quit();
        exit(2);
    }

    /* Resolve the entry points the backend needs, without opening a display.
       A header/library mismatch shows up here rather than 72 compilations
       later. */
    if (SDL_SetVideoMode == NULL || SDL_SetColors == NULL ||
        SDL_UpdateRect == NULL || SDL_WM_SetCaption == NULL ||
        SDL_EnableKeyRepeat == NULL || SDL_GetTicks == NULL ||
        SDL_Delay == NULL)
    {
        printf("SDL 1.2 entry points are missing.\n");
        SDL_Quit();
        exit(2);
    }

    SDL_Quit();

    /* The marker BUILD.COM's caller cares about - see RUN_PROBE. */
    mark = fopen("PROBE_SDL.OK", "w");
    if (mark == NULL)
    {
        return(2);
    }
    fclose(mark);
    return(0);
}

$! Linked against the SHARED SDL only.  This port links LIBSDL:LIBSDL$SHR/OPT 
$ P_NAME = "PROBE_SDL"
$ P_TAG  = "SDL"
$ P_STD  = CSTD
$ P_INC  = "/INCLUDE=(SDL''VMSINC')" + SHIMDEF
$ P_LOPT = ",LIBSDL:LIBSDL$SHR/OPT"
$ GOSUB BUILD_PROBE
$ IF (P_OK .EQ. 0) THEN GOTO SDL_DONE
$ GOSUB RUN_PROBE
$ HAVE_LIBSDL = P_OK
$SDL_DONE:
$ IF (HAVE_LIBSDL .EQ. 1)
$  THEN
$       WRITE SYS$OUTPUT "Checking for correct libSDL  ...  Yes (shared)"
$       SDLLIB = "LIBSDL:LIBSDL$SHR/OPT"
$  ELSE
$       GOSUB SDL_REPORT
$       KEEP_SDL = 1
$       GOTO EXIT
$ ENDIF
$!
$ CREATE PROBE_RT.C
/* Generated by CONFIGURE.COM.  Kept if the byte-exact I/O check fails, because
   that is the one failure that would silently corrupt data - see the message
   this file prints. */
#include <stdio.h>
#include <stddef.h>
#include <string.h>

int main(void)
{
    /* 00 is the byte a C string layer truncates on, 0A and 0D are what a record
       layer inserts, strips and pads with, and FF is what a 7-bit path clears.
       All four appear in real DAX blocks and savegame records. */
    static const unsigned char want[4] = { 0x00, 0x0a, 0x0d, 0xff };
    unsigned char got[4];
    char   buf[32];
    size_t z = 1234;
    FILE  *f;
    int    zu_ok = 1;
    int    io_ok = 1;

    snprintf(buf, sizeof(buf), "%zu", z);
    if (strcmp(buf, "1234") != 0) {
        zu_ok = 0;
    }

    f = fopen("PROBE_IO.TMP", "wb", "ctx=stm", "rfm=stmlf");
    if (f == NULL || fwrite(want, 1, 4, f) != 4) {
        io_ok = 0;
    }
    if (f != NULL) {
        fclose(f);
    }
    if (io_ok) {
        f = fopen("PROBE_IO.TMP", "rb", "ctx=stm");
        if (f == NULL) {
            io_ok = 0;
        } else {
            memset(got, 0x55, sizeof(got));
            if (fread(got, 1, 4, f) != 4 ||
                memcmp(got, want, 4) != 0 ||
                fgetc(f) != EOF) {
                io_ok = 0;
            }
            fclose(f);
        }
    }
    remove("PROBE_IO.TMP");

    printf("Checking %%zu in printf       ...  %s\n",
           zu_ok ? "Yes" : "No (see below)");
    printf("Checking binary file I/O     ...  %s\n",
           io_ok ? "Yes (ctx=stm)" : "No (see below)");

    /* Marker files rather than an exit status: DCL can test F$SEARCH without
       having to decode a C return value into a VMS condition code.  .BAD for
       each thing that is broken, and .OK unconditionally at the end to say the
       image really did get this far - without that last one a probe that failed
       to activate leaves no .BAD files either, and "nothing is broken" and
       "nothing ran" look identical from DCL. */
    if (!zu_ok) {
        f = fopen("PROBE_ZU.BAD", "w");
        if (f != NULL) fclose(f);
    }
    if (!io_ok) {
        f = fopen("PROBE_IO.BAD", "w");
        if (f != NULL) fclose(f);
    }
    f = fopen("PROBE_RT.OK", "w");
    if (f == NULL) {
        return 2;
    }
    fclose(f);
    return 0;
}

$ HAVE_ZU = 0
$ HAVE_IO = 0
$ P_NAME = "PROBE_RT"
$ P_TAG  = "RT"
$ P_STD  = CSTD
$ P_INC  = SHIMINC + SHIMDEF
$ P_LOPT = ""
$ GOSUB BUILD_PROBE
$ IF (P_OK .EQ. 0)
$  THEN
$       WRITE SYS$OUTPUT "Checking %zu in printf       ...  probe did not build"
$       WRITE SYS$OUTPUT "Checking binary file I/O     ...  probe did not build"
$  ELSE
$!      Run by hand rather than through RUN_PROBE: this is the one probe whose
$!      OWN two lines are the report, so SYS$OUTPUT has to stay on the terminal.
$!
$!      All three markers are cleared first, the two .BAD ones because they are
$!      read as "this is broken" and CLEANUP would not have removed them if a
$!      previous run of this script was interrupted.
$       P_SEQ = P_SEQ + 1
$       IF F$SEARCH("PROBE_RT.OK", P_SEQ) .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RT.OK;*
$       P_SEQ = P_SEQ + 1
$       IF F$SEARCH("PROBE_ZU.BAD", P_SEQ) .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_ZU.BAD;*
$       P_SEQ = P_SEQ + 1
$       IF F$SEARCH("PROBE_IO.BAD", P_SEQ) .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_IO.BAD;*
$       DEFINE/USER SYS$ERROR PROBE_RUN_RT.LOG
$       RUN PROBE_RT
$       P_SEQ = P_SEQ + 1
$       IF F$SEARCH("PROBE_RT.OK", P_SEQ) .EQS. ""
$        THEN
$               WRITE SYS$OUTPUT "Checking %zu in printf       ...  probe did not run"
$               WRITE SYS$OUTPUT "Checking binary file I/O     ...  probe did not run"
$        ELSE
$               HAVE_ZU = 1
$               HAVE_IO = 1
$               P_SEQ = P_SEQ + 1
$               IF F$SEARCH("PROBE_ZU.BAD", P_SEQ) .NES. "" THEN HAVE_ZU = 0
$               P_SEQ = P_SEQ + 1
$               IF F$SEARCH("PROBE_IO.BAD", P_SEQ) .NES. "" THEN HAVE_IO = 0
$       ENDIF
$ ENDIF
$!
$! --- clock_gettime -----------------------------------------------------------
$! Two call sites, [.SRC]RND.C's seeding and [.SRC]TEXT.C's time01, both wanting
$! CLOCK_REALTIME only.  Not fatal - PORTING-VMS.md has a four-line
$! gettimeofday() shim for [.SRC]VMS_COMPAT.H - but it must be reported, because
$! the alternative is discovering it as %LINK-W-NUDFSYMS after 72 compilations.
$ HAVE_CLOCK = 0
$ CREATE PROBE_CLK.C
/* Generated by CONFIGURE.COM.  Deleted either way - the answer is a yes or a no
   and the shim it points at is four lines. */
#include <stdio.h>
#include <time.h>

int main(void)
{
    struct timespec ts;
    FILE *mark;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || ts.tv_sec == 0) {
        return 2;
    }
    mark = fopen("PROBE_CLK.OK", "w");
    if (mark == NULL) {
        return 2;
    }
    fclose(mark);
    return 0;
}

$! This one is RUN as well as built, which the old code did not do: a CRTL can
$! have the symbol and still refuse the clock, and the shim below is the answer
$! either way.
$ P_NAME = "PROBE_CLK"
$ P_TAG  = "CLK"
$ P_STD  = CSTD
$ P_INC  = SHIMINC + SHIMDEF
$ P_LOPT = ""
$ GOSUB BUILD_PROBE
$ IF (P_OK .EQ. 1)
$  THEN
$       GOSUB RUN_PROBE
$       HAVE_CLOCK = P_OK
$ ENDIF
$ IF (HAVE_CLOCK .EQ. 1)
$  THEN
$       WRITE SYS$OUTPUT "Checking for clock_gettime   ...  Yes"
$  ELSE
$       WRITE SYS$OUTPUT "Checking for clock_gettime   ...  No (see below)"
$ ENDIF
$!
$! --- Sound -------------------------------------------------------------------
$! Not probed, on purpose, and there is nothing to probe.  [.SRC]PLATFORM_SDL1.C
$! never asks SDL for SDL_INIT_AUDIO and its four audio entry points are no-ops
$! that return false, so [.SRC]SOUND.C decides at runtime that no samples are
$! available, logs "running silent" and every sound_play() becomes a no-op.  No
$! audio backend is referenced at all and SDL_mixer is not needed.
$! (The sibling Hexen port probes for SDL audio and then hardcodes HAVE_SOUND=0
$! anyway; ROTT needed a whole stub file.  Neither is necessary here.)
$ WRITE SYS$OUTPUT "Sound                        ...  disabled (runtime, no stub)"
$!
$! --- The game data -----------------------------------------------------------
$! Not fatal at build time - just the commonest reason a freshly built image
$! exits immediately, so worth saying now rather than after 72 compilations.
$!
$! [.SRC]VFS.C looks for a directory holding TITLE.DAX, 8X8D1.DAX and ECL1.DAX,
$! trying "Data", "data", "DATA" and "." under the current default directory and
$! under the image's, so [.DATA] beside COAB.EXE needs no arguments at all.
$ HAVE_DATA = 0
$ IF F$SEARCH("[.DATA]TITLE.DAX") .NES. "" .AND. -
     F$SEARCH("[.DATA]8X8D1.DAX") .NES. "" .AND. -
     F$SEARCH("[.DATA]ECL1.DAX")  .NES. ""
$  THEN
$       WRITE SYS$OUTPUT "Checking for the game data   ...  Yes ([.DATA])"
$       HAVE_DATA = 1
$  ELSE
$       IF F$SEARCH("TITLE.DAX") .NES. "" .AND. -
           F$SEARCH("8X8D1.DAX") .NES. "" .AND. -
           F$SEARCH("ECL1.DAX")  .NES. ""
$        THEN
$               WRITE SYS$OUTPUT "Checking for the game data   ...  Yes (here)"
$               HAVE_DATA = 1
$        ELSE
$               WRITE SYS$OUTPUT "Checking for the game data   ...  not here"
$       ENDIF
$ ENDIF
$!
$! --- Generate BUILD.COM ------------------------------------------------------
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "Generating BUILD.COM"
$ OPEN/WRITE OUT BUILD.COM
$ WRITE OUT "$! Generated by CONFIGURE.COM - edit DESCRIP.MMS, not this file."
$ WRITE OUT "$!"
$ WRITE OUT "$! SDLLIB - the SDL 1.2 linker options file this system links with."
$ WRITE OUT "$! CSTD   - the /STANDARD keyword this compiler accepts for C99"
$ WRITE OUT "$!          syntax WITH the CRTL POSIX names still visible."
$ WRITE OUT "$! VMSINC - empty when this compiler has its own <stdint.h> and"
$ WRITE OUT "$!          <stdbool.h>; "",[.VMSINC]"" when it does not and the"
$ WRITE OUT "$!          shims there are needed.  Do not set it by hand on a"
$ WRITE OUT "$!          compiler that has the real headers - /INCLUDE comes"
$ WRITE OUT "$!          first, so it would shadow them."
$ WRITE OUT "$! VMSDEFS- normally empty.  Set only when [.VMSINC] is in use AND"
$ WRITE OUT "$!          this compiler has no <inttypes.h> to take the 64-bit"
$ WRITE OUT "$!          types from, in which case the shim declares them itself."
$ WRITE OUT "$! All four override the defaults in DESCRIP.MMS, which explains them."
$! An unproven CSTD is recorded as such, so that whoever reads BUILD.COM after a
$! compile failure knows this keyword was assumed rather than measured, and where
$! to look.  Silently writing the fallback would make a guess look like a result.
$ IF (HAVE_CSTD .EQ. 0)
$  THEN
$       WRITE OUT "$!"
$       WRITE OUT "$! NOTE: this CSTD was NOT verified - the probe could not build,"
$       WRITE OUT "$! so CONFIGURE.COM used the best keyword DCL would parse.  If the"
$       WRITE OUT "$! build stops on a /STANDARD or CRTL-name error, try another"
$       WRITE OUT "$! keyword here: C99, RELAXED, or either with"
$       WRITE OUT "$! /UNDEFINE=__HIDE_FORBIDDEN_NAMES appended.  PROBE_CC_STD_*.LOG"
$       WRITE OUT "$! in this directory holds what each attempt actually said."
$ ENDIF
$ WRITE OUT "$ SET NOON"
$ WRITE OUT "$ ", MAKE, "/IGN=WAR/MACRO=(""SDLLIB=", SDLLIB, -
            """,""CSTD=", CSTD, """,""VMSINC=", VMSINC, -
            """,""VMSDEFS=", VMSDEFS, """)"
$ CLOSE OUT
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "Now you can type @BUILD"
$ WRITE SYS$OUTPUT " "
$!
$ IF (HAVE_CSTD .EQ. 0) THEN -
    WRITE SYS$OUTPUT "NOTE: CSTD was assumed, not measured - see the note in BUILD.COM."
$ IF (HAVE_ZU .EQ. 0) THEN -
    WRITE SYS$OUTPUT "NOTE: no %zu in this CRTL - 40 log messages print it literally."
$ IF (HAVE_CLOCK .EQ. 0) THEN -
    WRITE SYS$OUTPUT "NOTE: no clock_gettime() - RND.C and TEXT.C need the shim in PORTING-VMS.md."
$ IF (HAVE_IO .EQ. 0)
$  THEN
$       WRITE SYS$OUTPUT "WARNING: byte-exact file I/O failed - the game would read DAX and"
$       WRITE SYS$OUTPUT "savegame records from the wrong offsets.  Fix vfs_fopen() in"
$       WRITE SYS$OUTPUT "[.SRC]VFS.C before playing; PROBE_RT.C is left here to test with."
$ ENDIF
$ IF (HAVE_DATA .EQ. 0)
$  THEN
$       WRITE SYS$OUTPUT "NOTE: no game data found.  Put the 1989 release's .DAX files in"
$       WRITE SYS$OUTPUT "[.DATA], copied in BINARY mode, or name a directory with --DATA."
$ ENDIF
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "   $ COAB == ""$", F$ENVIRONMENT("DEFAULT"), "COAB.EXE"""
$ WRITE SYS$OUTPUT "   $ COAB --HELP"
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "The ""$"" is needed so switches reach the game, not DCL.  Saved games,"
$ WRITE SYS$OUTPUT "the roster and COAB.LOG go to the current default directory."
$ WRITE SYS$OUTPUT " "
$ GOTO CLEANUP
$!
$! ============================================================================
$BUILD_PROBE:
$ P_OK = 0
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH(P_NAME + ".OBJ", P_SEQ) .NES. "" THEN DELETE/NOLOG/NOCONFIRM 'P_NAME'.OBJ;*
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH(P_NAME + ".EXE", P_SEQ) .NES. "" THEN DELETE/NOLOG/NOCONFIRM 'P_NAME'.EXE;*
$ DEFINE/USER SYS$OUTPUT _NLA0:
$ DEFINE/USER SYS$ERROR  PROBE_CC_'P_TAG'.LOG
$ CC'P_STD''CQUAL''CWARN''P_INC'/OBJECT='P_NAME'.OBJ 'P_NAME'.C
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH(P_NAME + ".OBJ", P_SEQ) .EQS. "" THEN RETURN
$ DEFINE/USER SYS$OUTPUT _NLA0:
$ DEFINE/USER SYS$ERROR  PROBE_LNK_'P_TAG'.LOG
$ LINK/EXE='P_NAME' 'P_NAME''P_LOPT'
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH(P_NAME + ".EXE", P_SEQ) .NES. "" THEN P_OK = 1
$ RETURN
$!
$! ============================================================================
$RUN_PROBE:
$ P_OK = 0
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH(P_NAME + ".OK", P_SEQ) .NES. "" THEN DELETE/NOLOG/NOCONFIRM 'P_NAME'.OK;*
$ DEFINE/USER SYS$OUTPUT PROBE_RUN_'P_TAG'.LOG
$ DEFINE/USER SYS$ERROR  SYS$OUTPUT
$ RUN 'P_NAME'
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH(P_NAME + ".OK", P_SEQ) .NES. "" THEN P_OK = 1
$ RETURN
$!
$! ============================================================================
$PICK_KEYWORD:
$ CSTD = ""
$ KWLIST = "/STANDARD=RELAXED_C99,/STANDARD=C99,/STANDARD=RELAXED,/STANDARD=RELAXED_ANSI89"
$ KWI = 0
$KW_LOOP:
$ KWTRY = F$ELEMENT(KWI, ",", KWLIST)
$ IF (KWTRY .EQS. ",") THEN RETURN
$ P_NAME = "PROBE_KW"
$ P_TAG  = "KW''KWI'"
$ P_STD  = KWTRY
$ P_INC  = ""
$ P_LOPT = ""
$ GOSUB BUILD_PROBE
$ IF (P_OK .EQ. 1) THEN CSTD = KWTRY
$ IF (P_OK .EQ. 1) THEN RETURN
$ KWI = KWI + 1
$ GOTO KW_LOOP
$!
$! ============================================================================
$SDL_REPORT:
$ WRITE SYS$OUTPUT "Checking for correct libSDL  ...  No"
$ WRITE SYS$OUTPUT " "
$ SDLSTAGE = "RUN"
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH("PROBE_SDL.EXE", P_SEQ) .EQS. "" THEN SDLSTAGE = "LINK"
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH("PROBE_SDL.OBJ", P_SEQ) .EQS. "" THEN SDLSTAGE = "COMPILE"
$ IF (SDLSTAGE .EQS. "COMPILE")
$  THEN
$       WRITE SYS$OUTPUT "PROBE_SDL.C would not COMPILE, so this is the SDL headers or"
$       WRITE SYS$OUTPUT "''CSTD' - not LIBSDL.  SDL is ", F$TRNLNM("SDL")
$       WRITE SYS$OUTPUT "   $ TYPE PROBE_CC_SDL.LOG"
$ ENDIF
$ IF (SDLSTAGE .EQS. "LINK")
$  THEN
$       WRITE SYS$OUTPUT "PROBE_SDL.C compiled but would not LINK, so the headers are"
$       WRITE SYS$OUTPUT "fine and this is LIBSDL:LIBSDL$SHR.OPT, which is looked for in"
$       WRITE SYS$OUTPUT F$TRNLNM("LIBSDL")
$       WRITE SYS$OUTPUT "   $ TYPE PROBE_LNK_SDL.LOG"
$ ENDIF
$ IF (SDLSTAGE .EQS. "RUN")
$  THEN
$       WRITE SYS$OUTPUT "PROBE_SDL.C built and linked but did not RUN: either the shared"
$       WRITE SYS$OUTPUT "image will not activate, or this SDL is not 1.2."
$       WRITE SYS$OUTPUT "   $ TYPE PROBE_RUN_SDL.LOG"
$ ENDIF
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "PROBE_SDL.C is left here.  To repeat the three steps by hand:"
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "   $ CC''CSTD'''CQUAL'''P_INC' PROBE_SDL.C"
$ WRITE SYS$OUTPUT "   $ LINK/EXE=PROBE_SDL PROBE_SDL,LIBSDL:LIBSDL$SHR/OPT"
$ WRITE SYS$OUTPUT "   $ RUN PROBE_SDL"
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "Only the shared SDL is tried; SDL 2 will not do.  See PORTING-VMS.md."
$ RETURN
$!
$! ============================================================================
$CHECK_HEADERS:
$ SHIMINC = ""
$ P_NAME = "PROBE_INC"
$ P_TAG  = "INC"
$ P_STD  = ""
$ P_INC  = ""
$ P_LOPT = ""
$ GOSUB BUILD_PROBE
$ IF (P_OK .EQ. 1)
$  THEN
$       WRITE SYS$OUTPUT "Checking <stdint.h>          ...  Yes (compiler's own)"
$       RETURN
$ ENDIF
$!
$ IF (HAVE_VMSINC .EQ. 1)
$  THEN
$       P_TAG = "INCS"
$       P_INC = "/INCLUDE=([.VMSINC])"
$       GOSUB BUILD_PROBE
$ ENDIF
$ IF (P_OK .EQ. 1)
$  THEN
$       SHIMINC = "/INCLUDE=([.VMSINC])"
$       VMSINC  = ",[.VMSINC]"
$       WRITE SYS$OUTPUT "Checking <stdint.h>          ...  no, using [.VMSINC]"
$       RETURN
$ ENDIF
$!
$ IF (HAVE_VMSINC .EQ. 1)
$  THEN
$       P_TAG = "INCSNI"
$       P_INC = "/INCLUDE=([.VMSINC])/DEFINE=(COAB_VMS_STDINT_NO_INTTYPES=1)"
$       GOSUB BUILD_PROBE
$ ENDIF
$ IF (P_OK .EQ. 1)
$  THEN
$       SHIMINC = "/INCLUDE=([.VMSINC])"
$       SHIMDEF = "/DEFINE=(COAB_VMS_STDINT_NO_INTTYPES=1)"
$       VMSINC  = ",[.VMSINC]"
$       VMSDEFS = ",COAB_VMS_STDINT_NO_INTTYPES=1"
$       WRITE SYS$OUTPUT "Checking <stdint.h>          ...  no, using [.VMSINC] standalone"
$       RETURN
$ ENDIF
$ HDR_FATAL = 1
$ IF (HAVE_VMSINC .EQ. 0)
$  THEN
$       WRITE SYS$OUTPUT "Checking <stdint.h>          ...  MISSING, and no [.VMSINC]"
$       WRITE SYS$OUTPUT " "
$       WRITE SYS$OUTPUT "This compiler predates C99 and has no <stdint.h>, so all 72"
$       WRITE SYS$OUTPUT "files would fail on [.SRC]COAB.H line 15.  The tree ships"
$       WRITE SYS$OUTPUT "[.VMSINC]STDINT.H and STDBOOL.H for this, but THIS COPY of"
$       WRITE SYS$OUTPUT "the source has neither - copy that directory across and"
$       WRITE SYS$OUTPUT "check with $ DIRECTORY [.VMSINC]*.H"
$  ELSE
$       WRITE SYS$OUTPUT "Checking <stdint.h>          ...  MISSING, [.VMSINC] did not help"
$       WRITE SYS$OUTPUT " "
$       WRITE SYS$OUTPUT "The shim is here but neither of its two modes compiles.  That is a"
$       WRITE SYS$OUTPUT "bug in the shim on this compiler, not a missing file.  Three logs,"
$       WRITE SYS$OUTPUT "in the order they were tried:"
$       WRITE SYS$OUTPUT "   PROBE_CC_INC.LOG     no shim at all"
$       WRITE SYS$OUTPUT "   PROBE_CC_INCS.LOG    shim, types from the RTL's <inttypes.h>"
$       WRITE SYS$OUTPUT "   PROBE_CC_INCSNI.LOG  shim, declaring every type itself"
$ ENDIF
$ WRITE SYS$OUTPUT " "
$ RETURN
$!
$! ============================================================================
$TRY_STD:
$ P_NAME = "PROBE_STD"
$ P_TAG  = TAG
$ P_STD  = TRY
$ P_INC  = SHIMINC + SHIMDEF
$ P_LOPT = ""
$ GOSUB BUILD_PROBE
$ IF (P_OK .EQ. 1)
$  THEN
$       GOSUB RUN_PROBE
$       IF (P_OK .EQ. 1)
$        THEN
$               HAVE_CSTD = 1
$               CSTD = TRY
$       ENDIF
$ ENDIF
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH("PROBE_STD.OBJ", P_SEQ) .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_STD.OBJ;*
$ P_SEQ = P_SEQ + 1
$ IF F$SEARCH("PROBE_STD.EXE", P_SEQ) .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_STD.EXE;*
$ RETURN
$!
$EXIT:
$ WRITE SYS$OUTPUT " "
$ WRITE SYS$OUTPUT "Configuration failed. BUILD.COM was not written."
$ WRITE SYS$OUTPUT " "
$ GOTO CLEANUP_KEEP
$!
$CLEANUP:
$ GOTO CLEANUP_KEEP
$!
$CLEANUP_KEEP:
$ DEFINE SYS$ERROR _NLA0:
$ DEFINE SYS$OUTPUT _NLA0:
$ IF (HAVE_CSTD .EQ. 1)
$  THEN
$       IF F$SEARCH("PROBE_STD.C") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_STD.C;*
$       IF F$SEARCH("PROBE_CC_STD_*.LOG")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CC_STD_*.LOG;*
$       IF F$SEARCH("PROBE_LNK_STD_*.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_LNK_STD_*.LOG;*
$       IF F$SEARCH("PROBE_RUN_STD_*.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RUN_STD_*.LOG;*
$ ENDIF
$ IF (HAVE_CSTD .EQ. 1)
$  THEN
$       IF F$SEARCH("PROBE_CC_KW*.LOG")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CC_KW*.LOG;*
$       IF F$SEARCH("PROBE_LNK_KW*.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_LNK_KW*.LOG;*
$ ENDIF
$ IF (HAVE_IO .EQ. 1)
$  THEN
$       IF F$SEARCH("PROBE_RT.C") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RT.C;*
$       IF F$SEARCH("PROBE_CC_RT.LOG")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CC_RT.LOG;*
$       IF F$SEARCH("PROBE_LNK_RT.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_LNK_RT.LOG;*
$       IF F$SEARCH("PROBE_RUN_RT.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RUN_RT.LOG;*
$ ENDIF
$ IF (KEEP_SDL .EQ. 0)
$  THEN
$       IF F$SEARCH("PROBE_SDL.C") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_SDL.C;*
$       IF F$SEARCH("PROBE_CC_SDL*.LOG")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CC_SDL*.LOG;*
$       IF F$SEARCH("PROBE_LNK_SDL*.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_LNK_SDL*.LOG;*
$       IF F$SEARCH("PROBE_RUN_SDL*.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RUN_SDL*.LOG;*
$ ENDIF
$ IF (HAVE_CLOCK .EQ. 1)
$  THEN
$       IF F$SEARCH("PROBE_CC_CLK.LOG")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CC_CLK.LOG;*
$       IF F$SEARCH("PROBE_LNK_CLK.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_LNK_CLK.LOG;*
$       IF F$SEARCH("PROBE_RUN_CLK.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RUN_CLK.LOG;*
$ ENDIF
$ IF (HDR_FATAL .EQ. 0)
$  THEN
$       IF F$SEARCH("PROBE_INC.C") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_INC.C;*
$       IF F$SEARCH("PROBE_CC_INC*.LOG")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CC_INC*.LOG;*
$       IF F$SEARCH("PROBE_LNK_INC*.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_LNK_INC*.LOG;*
$ ENDIF
$ IF F$SEARCH("PROBE_INC.OBJ") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_INC.OBJ;*
$ IF F$SEARCH("PROBE_INC.EXE") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_INC.EXE;*
$ IF F$SEARCH("PROBE_KW.C")    .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_KW.C;*
$ IF F$SEARCH("PROBE_KW.OBJ")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_KW.OBJ;*
$ IF F$SEARCH("PROBE_KW.EXE")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_KW.EXE;*
$ IF F$SEARCH("PROBE_STD.OBJ") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_STD.OBJ;*
$ IF F$SEARCH("PROBE_STD.EXE") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_STD.EXE;*
$ IF F$SEARCH("PROBE_STD.OK")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_STD.OK;*
$ IF F$SEARCH("PROBE_STD.TMP") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_STD.TMP;*
$ IF F$SEARCH("PROBE_LNK_SDLSTATIC.LOG") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_LNK_SDLSTATIC.LOG;*
$ IF F$SEARCH("PROBE_SDL.OBJ") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_SDL.OBJ;*
$ IF F$SEARCH("PROBE_SDL.EXE") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_SDL.EXE;*
$ IF F$SEARCH("PROBE_SDL.OK")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_SDL.OK;*
$ IF F$SEARCH("PROBE_RT.OBJ")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RT.OBJ;*
$ IF F$SEARCH("PROBE_RT.EXE")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RT.EXE;*
$ IF F$SEARCH("PROBE_RT.OK")   .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_RT.OK;*
$ IF F$SEARCH("PROBE_CLK.C")   .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CLK.C;*
$ IF F$SEARCH("PROBE_CLK.OBJ") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CLK.OBJ;*
$ IF F$SEARCH("PROBE_CLK.EXE") .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CLK.EXE;*
$ IF F$SEARCH("PROBE_CLK.OK")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_CLK.OK;*
$ IF F$SEARCH("PROBE_ZU.BAD")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_ZU.BAD;*
$ IF F$SEARCH("PROBE_IO.BAD")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_IO.BAD;*
$ IF F$SEARCH("PROBE_IO.TMP")  .NES. "" THEN DELETE/NOLOG/NOCONFIRM PROBE_IO.TMP;*
$ DEASS SYS$ERROR
$ DEASS SYS$OUTPUT
$ EXIT
