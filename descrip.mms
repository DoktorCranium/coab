# ============================================================================
# DESCRIP.MMS - Curse of the Azure Bonds (C/SDL port) for OpenVMS 8.4 Alpha
#
# Produces COAB.EXE in this directory, the tree root, from the 72 sources in
# [.SRC].
#
# The image name is short and uppercase on purpose: ODS-2 volumes fold case and
# allow only A-Z 0-9 $ _ - in a name.  A foreign command symbol to run it:
#
#   $ COAB == "$DISK$USER:[GAMES.COAB]COAB.EXE"
#   $ COAB                 
#
#
# BUILD:   $ @CONFIGURE.COM        ! checks DEC C/MMS/SDL/C99, writes BUILD.COM
#          $ @BUILD.COM
#     or   $ MMK/IGNORE=WARNING    ! (or MMS/IGNORE=WARNING) straight in
#
# PREREQUISITE - SDL 1.2 ONLY, defined the way the sibling ports expect:
#   $ DEFINE LIBSDL  <device:[dir]>   ! dir holding LIBSDL$SHR.OPT / LIBSDL.OPT
#   $ DEFINE SDL     <device:[dir]>   ! dir holding SDL.H
# CONFIGURE.COM verifies both by compiling, linking and RUNNING a probe.
#
# SDL_MIXER IS NOT NEEDED, and neither is SDL's audio subsystem.  See below.
#
# MMS or MMK must be run with /IGNORE=WARNING.  This tree is warning-free under
# gcc -Wall -Wextra, but DEC C's diagnostics are not GCC's: several are
# warning-severity here (see the /WARNINGS list below for the ones that are
# suppressed outright) and any that were missed would otherwise abort the build.
#
# ----------------------------------------------------------------------------
# WHERE THE GAME DATA AND THE SAVED GAMES LIVE
# ----------------------------------------------------------------------------
# COAB.EXE needs the 1989 release's data files - TITLE.DAX, 8X8D1.DAX, ECL1.DAX
# and the rest.  Put them in [.DATA] beside the image and they are found with no
# arguments: vfs.c tries "Data", "data", "DATA" and "." under both the current
# default directory and the image's, and the CRTL maps "./Data/" onto [.DATA].
# Otherwise name the directory:
#
#   $ COAB --DATA DISK$USER:[GAMES.COAB.DATA]
#   $ COAB --DATA [.MYDATA]
#
# SAVED GAMES, THE CHARACTER ROSTER AND COAB.LOG ARE WRITTEN TO THE CURRENT
# DEFAULT DIRECTORY, not to the one holding COAB.EXE.  That is vfs.c's user_dir
# under __VMS, and it is what the sibling ROTT port does: there is no XDG
# equivalent to aim at, getenv("HOME") answers in VMS syntax ("DKA0:[SMITH]")
# which cannot be joined onto a UNIX-style tail, and ".local" is not a legal
# ODS-2 directory name.  So SET DEFAULT to a directory you can write before
# playing, and expect CHRDATA1.SAV, SAVEGAM1.DAT and COAB.LOG to appear there.
#
# ----------------------------------------------------------------------------
# SDL 1.2 vs SDL 2
# ----------------------------------------------------------------------------
# Both backends are in [.SRC] and both are compiled; COAB_SDL1=1 in $(DEFS)
# picks which one has a body.  [.SRC]PLATFORM_SDL.C - the SDL-2 one - is
# guarded by "#if COAB_SDL2" and so compiles to an EMPTY TRANSLATION UNIT here.
# That is what EMPTYFILE is doing in the disabled-diagnostics list; it is not a
# file to remove from the build, because removing it would make this DESCRIP.MMS
# disagree with the glob the CMake and Makefile builds use.
#
# ============================================================================

CC = CC

# ----------------------------------------------------------------------------
# Include path
# ----------------------------------------------------------------------------
#   [.SRC]   where every source and header in the build set lives.  See the
#            <limits.h> note above for the one consequence of having it here.
#   SDL      the SDL 1.2 header logical.  [.SRC]PLATFORM_SDL1.C's "#include
#            <SDL.h>" is the only thing that needs it - one file out of 72.
#            That it works at all is also the proof that DEC C searches this list
#            for the ANGLE-BRACKET form, which is what $(VMSINC) below relies on.
#
# Nothing else is needed: this tree is one flat directory and every game include
# is a quoted basename.  The sibling Duke port needed 17 include rewrites and a
# tree-root entry because DEC C maps a quoted include containing slashes onto an
# RMS filespec and cannot stack a relative base directory; there is not one
# slash in an #include anywhere here.
#
# VMSINC is EMPTY by default and must stay that way.  CONFIGURE.COM sets it to
# ",[.VMSINC]" - note the leading comma - only when it has established that this
# compiler's header set has no <stdint.h> or no <stdbool.h>, which is the case on
# DEC C before V7.  [.VMSINC] then supplies just those two headers.
#
# It is conditional because /INCLUDE is searched BEFORE the system text
# libraries: adding [.VMSINC] unconditionally would shadow the real <stdint.h> on
# every compiler that has one.  See [.VMSINC]STDINT.H's header comment.
VMSINC =
INCS = ([.SRC],SDL$(VMSINC))

# VMSDEFS is the other half of the same decision, and is EMPTY by default for the
# same reason.  [.VMSINC]STDINT.H does not declare int64_t, uint64_t, intptr_t or
# uintptr_t itself: on the compilers it exists for, the RTL's <inttypes.h> already
# does - DECC$RTLDEF.TLB, module INTTYPES - and a second typedef of one of those
# names is %CC-E-NOLINKAGE, a hard error, even though the type is the same one.
# So the shim includes <inttypes.h> and takes those four from it.
#
# CONFIGURE.COM sets this to ",COAB_VMS_STDINT_NO_INTTYPES=1" only if that fails,
# which would mean a header set with neither <stdint.h> nor <inttypes.h>; the shim
# then declares all of them on its own.  It is spliced into /DEFINE below rather
# than into $(DEFS) so that overriding DEFS by hand - to add COAB_DATA_DIR, say -
# cannot drop it.
VMSDEFS =

# ----------------------------------------------------------------------------
# Preprocessor defines
# ----------------------------------------------------------------------------
# COAB_SDL1=1   selects the SDL-1.2 backend.  [.SRC]PLATFORM.H does
#               "#ifndef COAB_SDL1 / #define COAB_SDL1 0" and then derives
#               COAB_SDL2 as its inverse, so this ONE define switches both
#               [.SRC]PLATFORM_SDL1.C on and [.SRC]PLATFORM_SDL.C off.
#
#               The "=1" is not optional.  A bare /DEFINE=COAB_SDL1 expands to
#               nothing and "#if COAB_SDL1" becomes "#if " - a preprocessor
#               syntax error, not an SDL-1.2 build.
#
# Note what is NOT needed here, and where each one is instead:
#
#   __VMS         DEC C predefines it, and it is what [.SRC]COAB.H tests to pull
#                 in [.SRC]VMS_COMPAT.H.
#   VMS           the UNPREFIXED one, which SDL 1.2's begin_code.h needs to
#                 define DECLSPEC and without which every SDL prototype is a
#                 syntax error.  DEC C predefines it only in its relaxed modes,
#                 so [.SRC]VMS_COMPAT.H defines it itself - see the long comment
#                 there.  Deliberately NOT put on this line: it has to hold
#                 whatever $(CSTD) ends up being, and a source-level #ifndef
#                 cannot get out of step with a build flag.
#   PLATFORM_*    there is no platform dispatch in this tree at all, unlike the
#                 sibling ROTT port where PLATFORM_VMS is load-bearing.
#
# COAB_DATA_DIR is optional - a fallback data directory baked into the image:
#
#     $ MMK/IGN=WAR/MACRO=("DEFS=COAB_SDL1=1,COAB_DATA_DIR=""""DISK$U:[COAB.DATA]""""")
#
# which is tried last, after the relative searches and --DATA.  The quadrupled
# quotes are DCL's escaping for a string literal reaching the preprocessor;
# CONFIGURE.COM writes it for you if you answer its data-directory prompt.
DEFS = COAB_SDL1=1

# The language level, overridable from BUILD.COM.  See the $(CSTD) section in
# the header - this is the one macro most likely to need changing on a different
# DEC C, and CONFIGURE.COM works out which spelling this compiler accepts.
CSTD = /STANDARD=RELAXED_C99

# ----------------------------------------------------------------------------
# Compiler flags - the sibling ports' flags, known good on this compiler and OS.
# ----------------------------------------------------------------------------
#  /NAME=(AS_IS,SHORT)  AS_IS keeps the case of external names.  SDL's API is
#                       mixed-case (SDL_SetVideoMode, SDL_WM_SetCaption), and
#                       this tree's own 836 externals are lower case with
#                       underscores; upcasing either set would stop it matching
#                       the library it has to resolve against.
#
#                       SHORT keeps them inside the Alpha object format's
#                       31-character limit, and DO NOT DROP IT: 34 of the 836
#                       are longer than 31 characters (counted on the reference
#                       build's objects, longest aftercombat_deallocate_non_-
#                       team_members at 39), and TWO OF THEM COLLIDE when
#                       truncated to 31:
#
#                         combatmap_redraw_player_background
#                         combatmap_redraw_player_background_at
#
#                       both become "combatmap_redraw_player_backgro".  Both are
#                       external, both are declared in [.SRC]COMBATMAP.H, both
#                       are called from five files between them - COMBATMAP.C,
#                       COMBATLOOP.C, ATTACK.C, EFFECT.C, SPELLEFFECT.C - and
#                       THEY DO NOT TAKE THE SAME ARGUMENTS: the _at one adds a
#                       Point for the map cell to repaint when there is no
#                       combatant there.  With plain truncation the linker either
#                       rejects the duplicate or silently binds calls to the
#                       wrong one, and the second failure mode is a garbled
#                       battle map and a wild Point argument rather than a build
#                       error.  SHORT hashes the tail instead of dropping it, so
#                       the two stay distinct.
#
#                       Plain C, so unlike the sibling ScummVM/Kyra ports there
#                       is no mangling and no CXX_REPOSITORY.
#  /FLOAT=IEEE/IEEE=DENORM
#                       The engine's own arithmetic is all integer - this is a
#                       1989 DOS game and there is not one float in the game
#                       logic - but [.SRC]PLATFORM_SDL1.C's aspect correction
#                       and scaling arithmetic uses doubles, and the sibling
#                       ports set this, so the ABI matches theirs.
#  /MEMBER_ALIGNMENT    Align each struct member on its natural boundary.  This
#                       is the DEC C DEFAULT on Alpha and it is spelled out here
#                       only to make it impossible to "restore" a /NOMEMBER.
#                       DO NOT CHANGE IT.
#
#                       /NOMEMBER byte-packs EVERY struct in the build.  It is
#                       tempting here because this game reads 1989 DOS binary
#                       records - but that is exactly why it must not be used:
#                       this port does NOT cast file bytes to structs.  Every
#                       on-disk record goes through [.SRC]DATAIO.C, which reads
#                       and writes field by field at explicit byte offsets with
#                       sys_array_to_short/ushort/int ([.SRC]COAB.H), so the
#                       in-memory layout of Player, Item and the rest is
#                       IRRELEVANT to file compatibility and packing them buys
#                       nothing.  What packing WOULD do is misalign every u16
#                       and u32 field on Alpha, where an unaligned access is a
#                       fault the OS fixes up in software - correct, but orders
#                       of magnitude slower, on structs the combat loop touches
#                       thousands of times a frame.
#  /OPTIMIZE=INLINE=SPEED
#                       The sibling ports' level.  INLINE=SPEED matters more
#                       here than it does for them: [.SRC]COAB.H, [.SRC]POINT.H
#                       and [.SRC]ENUMS.H put 17 "static inline" accessors in
#                       headers included by nearly every file, and they are on
#                       the hot paths (set_vid_pixel, point_add, the
#                       sys_array_to_* record readers).
#  /WARNINGS=(DISABLE=...)
#                       DEC C (not CXX) message tags.  Far fewer than the
#                       sibling ports need - this tree is warning-free under
#                       gcc -Wall -Wextra, so nothing here is suppressing a
#                       real problem - but DEC C's analysis is not GCC's:
#                         MISSINGRETURN   [.SRC]VMS_COMPAT.H defines
#                                         __attribute__ away, so DEC C cannot
#                                         see the ((noreturn)) on quit.h's
#                                         game_print_and_exit or log.h's
#                                         log_fatal and flags the fallthrough
#                                         tails of the functions that end by
#                                         calling them.  This is the direct
#                                         consequence of that shim and the one
#                                         entry in this list that is definitely
#                                         needed.
#                         EMPTYFILE       [.SRC]PLATFORM_SDL.C, the unselected
#                                         SDL-2 backend - see the SDL 1.2 vs
#                                         SDL 2 note above.  Exactly one file.
#                         PTRMISMATCH,PTRMISMATCH1,CVTDIFTYPES
#                                         u8* (unsigned char*) vs char* string
#                                         plumbing, which this tree does in the
#                                         DAX and savegame readers.  Harmless -
#                                         the two agree in size and alignment -
#                                         and GCC is told the same thing by
#                                         -Wno-pointer-sign in the reference
#                                         build.
#                         QUESTCOMPARE    informational; fires on the
#                                         "unsigned_field <= 0" idiom the
#                                         transcribed engine code uses as a
#                                         redundant spelling of == 0.
#                         LONGEXTERN      external names past 31 characters,
#                                         which /NAME=SHORT is already handling.
#                         MACROREDEF, UNDEFVARMOD
#                                         the same two the sibling ports
#                                         disable, for the same reasons.
#
#  Kept as insurance rather than because a build was seen to need them:
#  MISSINGRETURN and EMPTYFILE above are the two that are certain.  If a first
#  build on real hardware is quiet, the others can be dropped for sharper
#  diagnostics; they are here so that the first build is READABLE, which on an
#  emulated Alpha is worth more than it sounds.
CFLAGS = /INCLUDE=$(INCS) /DEFINE=($(DEFS)$(VMSDEFS)) $(CSTD) \
 /NAME=(AS_IS,SHORT) /FLOAT=IEEE /IEEE=DENORM /MEMBER_ALIGNMENT \
 /OPTIMIZE=INLINE=SPEED \
 /WARNINGS=(DISABLE=(MISSINGRETURN,EMPTYFILE,PTRMISMATCH,PTRMISMATCH1,\
CVTDIFTYPES,QUESTCOMPARE,LONGEXTERN,MACROREDEF,UNDEFVARMOD))

# Same, but unoptimised.  Not used by default - this is the macro to swap a
# file's rule over to if it dies with %GEM-F-ASSERTION.  See the note below.
CFLAGS_NOOPT = /INCLUDE=$(INCS) /DEFINE=($(DEFS)$(VMSDEFS)) $(CSTD) \
 /NAME=(AS_IS,SHORT) /FLOAT=IEEE /IEEE=DENORM /MEMBER_ALIGNMENT \
 /NOOPTIMIZE \
 /WARNINGS=(DISABLE=(MISSINGRETURN,EMPTYFILE,PTRMISMATCH,PTRMISMATCH1,\
CVTDIFTYPES,QUESTCOMPARE,LONGEXTERN,MACROREDEF,UNDEFVARMOD))

# Same, but /OPTIMIZE=LEVEL=1.  Also a %GEM-F-ASSERTION escape hatch, and the
# one to try FIRST - see the pass-name table below.
CFLAGS_OPT1 = /INCLUDE=$(INCS) /DEFINE=($(DEFS)$(VMSDEFS)) $(CSTD) \
 /NAME=(AS_IS,SHORT) /FLOAT=IEEE /IEEE=DENORM /MEMBER_ALIGNMENT \
 /OPTIMIZE=LEVEL=1 \
 /WARNINGS=(DISABLE=(MISSINGRETURN,EMPTYFILE,PTRMISMATCH,PTRMISMATCH1,\
CVTDIFTYPES,QUESTCOMPARE,LONGEXTERN,MACROREDEF,UNDEFVARMOD))

# ----------------------------------------------------------------------------
# SDL link fragment.  The SHARED SDL, as in the sibling ports.
# ----------------------------------------------------------------------------
# Shared only, deliberately.  CONFIGURE.COM links its SDL probe against this and
# nothing else, so what it verifies is what gets built; it used to fall back to a
# static LIBSDL:LIBSDL/OPT, which could only report success for a configuration
# this file does not use.  A static SDL means editing this line by hand.
SDLLIB = LIBSDL:LIBSDL$SHR/OPT

# ----------------------------------------------------------------------------
# Objects - all 72 sources in [.SRC] except MAIN.C, which is 71 of them.
# ----------------------------------------------------------------------------
# This is the same set the Makefile and CMakeLists.txt build from a src/*.c
# glob, listed out because MMS has no glob.  IF YOU ADD A SOURCE, add it here,
# add a compile rule at the bottom, and check the .OLB basename note below.
#
# NOTHING IS EXCLUDED.  Unlike the sibling ports there are no dead files, no
# generator programs with their own main(), no DOS-era card drivers and no
# Watcom asm in this tree - it was written for this port.  Two entries are worth
# a word anyway:
#
#   [.SRC]PLATFORM_SDL.OBJ    the SDL-2 backend, compiled to an empty object
#                             because COAB_SDL1=1.  Kept in the list on purpose;
#                             see the SDL 1.2 vs SDL 2 note in the header.
#   [.SRC]SELFTEST.OBJ        ~12000 lines and by far the largest file here.  It
#                             is the --self-test harness: 824 checks that render
#                             offscreen, dump PPMs and need no display, which is
#                             how this port is verified.  It is genuinely part of
#                             the image, not test scaffolding to strip - main.c
#                             calls selftest_run() - and it is the file most
#                             likely to provoke %GEM-F-ASSERTION.  See below.
#
# [.SRC]MAIN.OBJ is deliberately absent from this list: it carries main()
# (main.c:80) and must stay on the LINK command line.  See the COAB.EXE rule.
OBJ = \
[.SRC]affect.obj,\
[.SRC]affecttab.obj,\
[.SRC]aftercombat.obj,\
[.SRC]area.obj,\
[.SRC]attack.obj,\
[.SRC]battlesetup.obj,\
[.SRC]camp.obj,\
[.SRC]character.obj,\
[.SRC]cheats.obj,\
[.SRC]classcalc.obj,\
[.SRC]combat.obj,\
[.SRC]combatloop.obj,\
[.SRC]combatmap.obj,\
[.SRC]dataio.obj,\
[.SRC]dax.obj,\
[.SRC]display.obj,\
[.SRC]draw.obj,\
[.SRC]dungeon.obj,\
[.SRC]ecl.obj,\
[.SRC]eclvm.obj,\
[.SRC]effect.obj,\
[.SRC]endgame.obj,\
[.SRC]fileio.obj,\
[.SRC]firework.obj,\
[.SRC]frames.obj,\
[.SRC]gbl.obj,\
[.SRC]geo.obj,\
[.SRC]icons.obj,\
[.SRC]import.obj,\
[.SRC]input.obj,\
[.SRC]item.obj,\
[.SRC]limits.obj,\
[.SRC]log.obj,\
[.SRC]mapcursor.obj,\
[.SRC]menu.obj,\
[.SRC]money.obj,\
[.SRC]monsterai.obj,\
[.SRC]partymenu.obj,\
[.SRC]picture.obj,\
[.SRC]platform_sdl.obj,\
[.SRC]platform_sdl1.obj,\
[.SRC]player.obj,\
[.SRC]program.obj,\
[.SRC]prompt.obj,\
[.SRC]protect.obj,\
[.SRC]quit.obj,\
[.SRC]resting.obj,\
[.SRC]resttime.obj,\
[.SRC]rnd.obj,\
[.SRC]roster.obj,\
[.SRC]savegame.obj,\
[.SRC]selftest.obj,\
[.SRC]set.obj,\
[.SRC]shop.obj,\
[.SRC]sound.obj,\
[.SRC]spellcast.obj,\
[.SRC]spelleffect.obj,\
[.SRC]spelllist.obj,\
[.SRC]spellmenu.obj,\
[.SRC]spells.obj,\
[.SRC]sys.obj,\
[.SRC]target.obj,\
[.SRC]temple.obj,\
[.SRC]text.obj,\
[.SRC]tile.obj,\
[.SRC]title.obj,\
[.SRC]treasure.obj,\
[.SRC]vfs.obj,\
[.SRC]view3d.obj,\
[.SRC]viewplayer.obj,\
[.SRC]vm.obj

# ----------------------------------------------------------------------------
# Targets
# ----------------------------------------------------------------------------
ALL : COAB.EXE
        $!

# WHY MAIN.OBJ IS ON THE COMMAND LINE AND EVERYTHING ELSE IS IN A LIBRARY.
# Both halves of this were learned the hard way on the sibling ports:
#   * The object defining main() must be named directly on the LINK command
#     line.  The OpenVMS linker does not promote a transfer address out of a
#     library or an /OPTIONS file, so burying main.obj yields %LINK-W-USRTFR,
#     "no user transfer address" - the image links but cannot be run.
#   * Yet a bare command-line object's own references are NOT resolved against
#     an /OPTIONS file, which gives the opposite failure, %LINK-W-NUDFSYMS.
# An object library satisfies both at once: main.obj keeps the transfer address
# and its references pull the defining modules out of COAB.OLB.  This is exactly
# the shape Chocolate Doom uses (i_main.obj + DOOM.OLB) and ROTT uses
# (rt_main.obj + ROTT.OLB).
#
# The library also keeps us clear of the ~1024-character MMS/MMK line-element
# limit and DCL's 4095-character command-line limit, which 71 objects with
# [.SRC] prefixes - about 1000 characters of filespecs - would sail straight
# past on the LINK line.
#
# EVERY FILESPEC AFTER THE FIRST NEEDS AN EXPLICIT SYS$DISK:[] - DO NOT REMOVE IT.
# DCL filespecs on one command line inherit the device and directory from the
# PRECEDING filespec, not from the current default directory.  So
#     LINK ... [.SRC]main.obj,COAB.OLB/LIBRARY,...
# makes the linker look for the library in [.SRC] - the directory main.obj just
# established - and it fails with %LINK-F-OPENIN / -RMS-E-FNF even though
# COAB.OLB was created correctly one line earlier, in the root.  SYS$DISK: keeps
# the current default DEVICE and [] resets the DIRECTORY to the current default,
# which is where the library and the .EXE belong.
# (The LIBRARY/CREATE rule below is immune: the library is its first filespec,
# so there is nothing preceding it to inherit from.  The compile rules are immune
# too - both of their filespecs carry an explicit [.SRC].  $(SDLLIB) is immune
# because LIBSDL: is an explicit logical device.)
#
# /THREAD=UP is what the SDL port's own readme.vms prescribes for linking
# against LIBSDL$SHR - see [-.SDL]README.VMS.
#
# NO MATH LIBRARY FRAGMENT IS NEEDED.  The DEC C RTL is one shareable image and
# carries the maths routines; there is no -lm to translate.
#
# The two lines after the LINK report what was produced.  DIRECTORY/SIZE/DATE is
# listed as well as the banner because the useful thing to see at the end of a
# long emulated build is that the image is REALLY there and freshly dated: MMK
# prints nothing of its own on success, and a stale COAB.EXE left over from an
# earlier run looks exactly like a new one otherwise.
#
# NO "@" PREFIX ON THESE TWO LINES.  In make an @ suppresses command echoing, and
# MMS/MMK accept it - but if it were ever passed through to DCL instead of being
# consumed, "@ DIRECTORY" means "execute the command procedure DIRECTORY.COM" and
# the build would die on its very last step.  ("-" to ignore errors is
# deliberately absent too: if DIRECTORY cannot find the image, the link silently
# produced nothing and that must not be reported as a successful build.)
COAB.EXE : [.SRC]main.obj,COAB.OLB
        LINK/EXE=SYS$DISK:[]COAB.EXE/THREAD=UP [.SRC]main.obj,SYS$DISK:[]COAB.OLB/LIBRARY,$(SDLLIB)
        DIRECTORY/SIZE=ALL/DATE SYS$DISK:[]COAB.EXE
        WRITE SYS$OUTPUT "Build done"

# Safe as a single flat library because no two sources share a basename: an
# OpenVMS .OLB namespace is FLAT and keyed on the module name (the basename),
# IGNORING the directory, so same-named files in different directories silently
# overwrite each other.
#
# That cannot happen here as long as the tree stays one directory deep - which
# it is, all 72 sources in [.SRC] - but re-check if you ever add a subdirectory.
# The near miss to know about is not a source at all: [.SRC]LIMITS.H shares its
# name with the system <limits.h> (see the header), and [.SRC]LIMITS.C therefore
# contributes a module called LIMITS.  That is fine - the CRTL is a shareable
# image, not a library of modules - but it is the kind of thing that stops being
# fine the moment someone links against an object library that has one too.
COAB.OLB : $(OBJ)
        LIBRARY/CREATE COAB.OLB $(OBJ)

# ----------------------------------------------------------------------------
# EXPECTED COMPILER BUG - %GEM-F-ASSERTION, and what to do about it
# ----------------------------------------------------------------------------
# DEC C on OpenVMS 8.4 Alpha crashes with
#     %GEM-F-ASSERTION, Compiler internal error - please submit problem report
# with GEM_LU_MAIN (the loop unroller/peeler) in the traceback, on the idiom
#     if (var-- <op> 0)
# i.e. a post-decrement used inside a comparison.  The sibling Hexen port had to
# rewrite 8 such sites and the Duke port 18; see their COMPILER_FIXES.TXT.
#
# THIS TREE HAS NONE.  Verified across all 72 sources and 71 headers:
#     $ SEARCH [.SRC]*.C,*.H "-- >","-- <","-- =","--=","-->","--<"
# returns nothing, and so does the mirror-image "if (--var <op> ...)" form.
# So no file here needs a special flag for that reason, and if you hit
# %GEM-F-ASSERTION it will be something else.
#
# IT WAS SOMETHING ELSE.  [.SRC]AFFECTTAB.C does crash GEM_LU_MAIN on OpenVMS 8.4
# Alpha, and its rule at the bottom of this file therefore says $(CFLAGS_OPT1).
# The idiom above is not present in it; the two likeliest triggers are the loops
# in affect_fire_resist and affect_71_handler,
#
#     for (int i = 1; i <= gbl.dice_count; i++) {
#         gbl.damage -= 2;                      /* gbl.damage-- in the other */
#         if (gbl.damage < gbl.dice_count) { gbl.damage = gbl.dice_count; }
#     }
#
# where the trip count and the clamp both read the same global struct the body
# writes to.  That is a guess about the compiler, not a diagnosis, and it is
# deliberately NOT acted on in the source: the loops are correct C, the game's
# behaviour depends on the clamp running each iteration, and LEVEL=1 on one file
# whose hot path is a table lookup costs nothing measurable.
#
# Do NOT extend that search to "--)": it matches the third clause of every
# ordinary countdown for() loop ("for (int i = n - 1; i >= 0; i--)"), of which
# there are ten here, and none of those is the idiom - the bug needs the
# decrement INSIDE a comparison expression.
#
# IF SOME FILE DOES HIT IT, read the failing pass name in the traceback and
# change FLAGS before you touch the source - the pass names say which flag to
# reach for:
#   GEM_LU_* / PEEL_LOOP   loop unroller; runs only above LEVEL=1
#                                                    -> give it $(CFLAGS_OPT1)
#   GEM_DF_* / CSE         LEVEL=3 dataflow          -> give it $(CFLAGS_OPT1)
#   GEM_CX_*               code EXPANDER; runs at EVERY level, so /NOOPTIMIZE
#                          can CAUSE it              -> try OPT1, not NOOPT
#   GEM_TN_*               register allocation       -> try $(CFLAGS_OPT1)
#   ME_DEBUGGEN            debug-symbol emission     -> add /NODEBUG/NOTRACEBACK
# An unjustified /NOOPTIMIZE cost the sibling Kyra port several build rounds.
#
# The biggest files here are the likeliest to provoke it, and the first one is
# in a class of its own:
#   [.SRC]SELFTEST.C     ~12000 lines / 430K, one enormous run of straight-line
#                        calls.  If anything in this tree dies in the optimiser
#                        it will be this file.  It is also the cheapest one to
#                        demote: it runs once, offscreen, when --self-test is
#                        given, so $(CFLAGS_NOOPT) costs the game nothing.
#   [.SRC]SPELLEFFECT.C  the 100-odd spell implementations.
#   [.SRC]PARTYMENU.C    the party and inventory screens.
#   [.SRC]ATTACK.C       the melee/missile resolution.
#   [.SRC]AFFECTTAB.C    a large static table plus its lookups.  This is the one
#                        that actually asserted; already on $(CFLAGS_OPT1).
# To move one file, give it its own rule with $(CFLAGS_OPT1) - the rules at the
# bottom are already one per file, so it is a one-word edit - and do NOT change
# $(CFLAGS) for the whole build.
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
# One explicit rule per object.  The [.SRC] prefix rides along on
# $(MMS$SOURCE), which compiles each file in place with the default directory
# left HERE, at the tree root.
#
# No source has a dependency line on its headers.  MMS's built-in C scanner does
# not follow quoted includes across a directory prefix reliably, and writing 72
# hand-maintained header lists is worse than the alternative: after editing a
# header, "$ MMK CLEAN" then rebuild.  A full build is ~72 compilations.
# ----------------------------------------------------------------------------
[.SRC]affect.obj : [.SRC]affect.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

# $(CFLAGS_OPT1), not $(CFLAGS): this file really does crash the optimiser on
# OpenVMS 8.4 Alpha.  See the %GEM-F-ASSERTION note above - the traceback named
# GEM_LU_MAIN, and LEVEL=1 is what switches the loop unroller off.
[.SRC]affecttab.obj : [.SRC]affecttab.c
        $(CC) $(CFLAGS_OPT1) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]aftercombat.obj : [.SRC]aftercombat.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]area.obj : [.SRC]area.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]attack.obj : [.SRC]attack.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]battlesetup.obj : [.SRC]battlesetup.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]camp.obj : [.SRC]camp.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]character.obj : [.SRC]character.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]cheats.obj : [.SRC]cheats.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]classcalc.obj : [.SRC]classcalc.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]combat.obj : [.SRC]combat.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]combatloop.obj : [.SRC]combatloop.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]combatmap.obj : [.SRC]combatmap.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]dataio.obj : [.SRC]dataio.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]dax.obj : [.SRC]dax.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]display.obj : [.SRC]display.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]draw.obj : [.SRC]draw.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]dungeon.obj : [.SRC]dungeon.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]ecl.obj : [.SRC]ecl.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]eclvm.obj : [.SRC]eclvm.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]effect.obj : [.SRC]effect.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]endgame.obj : [.SRC]endgame.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]fileio.obj : [.SRC]fileio.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]firework.obj : [.SRC]firework.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]frames.obj : [.SRC]frames.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]gbl.obj : [.SRC]gbl.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]geo.obj : [.SRC]geo.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]icons.obj : [.SRC]icons.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]import.obj : [.SRC]import.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]input.obj : [.SRC]input.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]item.obj : [.SRC]item.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

# The AD&D limit tables.  Its header shadows the system <limits.h>;
# see the note in the file header.  Nothing to do about it here.

[.SRC]limits.obj : [.SRC]limits.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]log.obj : [.SRC]log.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

# main().  Stays OFF $(OBJ) and ON the LINK command line - see the
# COAB.EXE rule for why.  Also where the case-insensitive option
# matching that makes the switches usable from DCL lives.

[.SRC]main.obj : [.SRC]main.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]mapcursor.obj : [.SRC]mapcursor.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]menu.obj : [.SRC]menu.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]money.obj : [.SRC]money.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]monsterai.obj : [.SRC]monsterai.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]partymenu.obj : [.SRC]partymenu.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]picture.obj : [.SRC]picture.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

# The SDL-2 backend.  COAB_SDL1=1 empties it out; this rule exists so
# that this DESCRIP.MMS builds the same file set as the src/*.c glob
# the Makefile and CMake builds use.  EMPTYFILE is disabled for it.

[.SRC]platform_sdl.obj : [.SRC]platform_sdl.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

# The SDL-1.2 backend - the one file in the build that includes <SDL.h>,
# and the reason SDL is on $(INCS).  Its audio entry points are
# deliberate no-ops; see the no-sound section in the file header.

[.SRC]platform_sdl1.obj : [.SRC]platform_sdl1.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]player.obj : [.SRC]player.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]program.obj : [.SRC]program.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]prompt.obj : [.SRC]prompt.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]protect.obj : [.SRC]protect.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]quit.obj : [.SRC]quit.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]resting.obj : [.SRC]resting.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]resttime.obj : [.SRC]resttime.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]rnd.obj : [.SRC]rnd.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]roster.obj : [.SRC]roster.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]savegame.obj : [.SRC]savegame.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

# ~12000 lines, the --self-test harness.  THE file to demote to
# $(CFLAGS_NOOPT) if the optimiser asserts - it runs once, offscreen,
# and costs the game nothing unoptimised.

[.SRC]selftest.obj : [.SRC]selftest.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]set.obj : [.SRC]set.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]shop.obj : [.SRC]shop.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]sound.obj : [.SRC]sound.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]spellcast.obj : [.SRC]spellcast.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]spelleffect.obj : [.SRC]spelleffect.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]spelllist.obj : [.SRC]spelllist.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]spellmenu.obj : [.SRC]spellmenu.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]spells.obj : [.SRC]spells.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]sys.obj : [.SRC]sys.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]target.obj : [.SRC]target.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]temple.obj : [.SRC]temple.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]text.obj : [.SRC]text.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]tile.obj : [.SRC]tile.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]title.obj : [.SRC]title.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]treasure.obj : [.SRC]treasure.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

# Everything that knows about RMS and ODS-2: the "ctx=stm" opens, the
# trailing slash stat()/opendir() need for a directory, the ";1"
# version stripping, and the save/log directory choice.

[.SRC]vfs.obj : [.SRC]vfs.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]view3d.obj : [.SRC]view3d.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]viewplayer.obj : [.SRC]viewplayer.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

[.SRC]vm.obj : [.SRC]vm.c
        $(CC) $(CFLAGS) $(MMS$SOURCE) /OBJ=$(MMS$TARGET)

# ----------------------------------------------------------------------------
# $ MMK CLEAN     (F$SEARCH guards rather than a "-" prefix, so this behaves
#                  the same under MMS and MMK on an already-clean tree)
# ----------------------------------------------------------------------------
CLEAN :
        IF F$SEARCH("[.SRC]*.OBJ") .NES. "" THEN DELETE/NOLOG/NOCONFIRM [.SRC]*.OBJ;*
        IF F$SEARCH("COAB.OLB") .NES. "" THEN DELETE/NOLOG/NOCONFIRM COAB.OLB;*
        IF F$SEARCH("COAB.EXE") .NES. "" THEN DELETE/NOLOG/NOCONFIRM COAB.EXE;*
