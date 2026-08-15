# Curse of the Azure Bonds - C/SDL port
#
# Build:   ./configure     optional; needed only when SDL is somewhere unusual
#          make            auto-detects SDL2 if configure was not run
#          make SDL1=1     build against SDL-1.2 instead (no sound)
#          make DEBUG=1    -O0 -g3
#          make run
#          make test       headless self-test (no display server needed)
#          make install    to $(PREFIX), default /usr/local
#
# Everything SDL-specific is behind platform.h, so each backend is one
# platform_*.c and no engine changes: platform_sdl.c is SDL2, platform_sdl1.c is
# SDL-1.2. src/*.c is globbed, so both are compiled and the unselected one is
# empty - see the COAB_SDL1/COAB_SDL2 block in platform.h.
#
# For OpenVMS, use DESCRIP.MMS instead; see PORTING-VMS.md.

PROJECT  := coab
BUILDDIR := build
SRCDIR   := src

comma := ,

# Written by ./configure. Absent is fine; everything below has a fallback.
-include config.mk

# config.mk decides the backend once and for all, so SDL1=1 on the command line
# cannot be honoured afterwards. Saying so is the point: the alternative is a
# build that quietly ignores the flag and produces an SDL2 binary, which then
# passes every test and proves nothing about the backend that was asked for.
ifeq ($(CONFIGURED),yes)
ifdef SDL1
ifeq (,$(findstring COAB_SDL1,$(SDL_DEF)))
  $(error SDL1=1 cannot override config.mk, which configured $(SDL_NAME). \
Run ./configure --with-sdl1 to change the backend, or make distclean to drop \
config.mk and let SDL1=1 take effect.)
endif
endif
endif

# ---------------------------------------------------------------- SDL fallback
# Without configure, find SDL the quick way. Some installs are not on the
# default pkg-config search path - /usr/pkg is one such place - so a few extra
# directories are prepended rather than replacing what the environment provides.
ifneq ($(CONFIGURED),yes)

PKGCONF := PKG_CONFIG_PATH=/usr/pkg/lib/pkgconfig:/usr/local/lib/pkgconfig:$$PKG_CONFIG_PATH pkg-config

ifdef SDL1
SDL_CFLAGS := $(shell $(PKGCONF) --cflags sdl 2>/dev/null || sdl-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell $(PKGCONF) --libs   sdl 2>/dev/null || sdl-config --libs   2>/dev/null)
SDL_DEF    := -DCOAB_SDL1=1
SDL_NAME   := SDL-1.2 (no sound)
SDL_PKG    := libsdl1.2-dev

# Old SDL 1.2 header sets define DECLSPEC only for Windows and VMS and have no
# #else, so every prototype begins with an unknown identifier and nothing that
# includes <SDL.h> compiles at all. Probed rather than assumed - the same test is
# in configure and CMakeLists.txt - because defining it unasked would hide a real
# header problem behind a build that looks like it worked. Skipped entirely if the
# compiler will not do a syntax-only check, which is the only part of this that is
# gcc/clang-specific; ./configure does it portably.
ifneq (,$(strip $(SDL_CFLAGS)$(SDL_LIBS)))
ifeq (ok,$(shell echo 'int main(void){return 0;}' | \
        $(CC) -fsyntax-only -x c - >/dev/null 2>&1 && echo ok))
SDL_CFLAGS += $(shell printf '#include <SDL.h>\nint main(void){return 0;}\n' | \
        $(CC) -fsyntax-only -x c - $(SDL_CFLAGS) >/dev/null 2>&1 || echo -DDECLSPEC=)
endif
endif
else
SDL_CFLAGS := $(shell $(PKGCONF) --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell $(PKGCONF) --libs   sdl2 2>/dev/null || sdl2-config --libs   2>/dev/null)
SDL_DEF    := -DCOAB_SDL2=1
SDL_NAME   := SDL2
SDL_PKG    := libsdl2-dev
endif

# A -L outside the loader's default path needs a matching rpath, or the binary
# links against the SDL found here and silently loads an older one from /lib at
# run time. Skipped when pkg-config already supplied one.
ifeq (,$(findstring -Wl$(comma)-R,$(SDL_LIBS)))
ifneq (,$(findstring -L/usr/pkg/lib,$(SDL_LIBS)))
  SDL_LIBS += -Wl,-R/usr/pkg/lib
endif
endif

ifeq ($(strip $(SDL_CFLAGS))$(strip $(SDL_LIBS)),)
  $(error Could not find $(SDL_NAME). Install $(SDL_PKG), or run ./configure \
--with-sdl-prefix=DIR if it lives somewhere unusual.)
endif

STD := -std=c99
ifdef DEBUG
  OPT := -O0 -g3 -DCOAB_DEBUG=1
else
  OPT := -O2 -g
endif

PREFIX      ?= /usr/local
EXEC_PREFIX ?= $(PREFIX)
BINDIR      ?= $(EXEC_PREFIX)/bin
DATAROOTDIR ?= $(PREFIX)/share

endif  # not CONFIGURED

# ------------------------------------------------------------------- toolchain
CC       ?= cc
WARN     := -Wall -Wextra -Wno-unused-parameter -Wshadow -Wstrict-prototypes \
            -Wmissing-prototypes -Wpointer-arith -Wwrite-strings
FEATURE  := -D_DEFAULT_SOURCE

CFLAGS   += $(STD) $(FEATURE) $(WARN) $(OPT) $(SAN) $(SDL_DEF) -I$(SRCDIR) \
            $(SDL_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS  += $(SAN) $(EXTRA_LDFLAGS)
LDLIBS   += $(SDL_LIBS) -lm

INSTALL         ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 755
INSTALL_DATA    ?= $(INSTALL) -m 644

# --------------------------------------------------------------------- sources
SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

BIN := $(BUILDDIR)/$(PROJECT)

.PHONY: all clean distclean run test info install uninstall

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@echo "==> $@  ($(SDL_NAME))"

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

run: $(BIN)
	$(BIN)

# Renders to an offscreen buffer and dumps PPMs; needs no display server.
test: $(BIN)
	$(BIN) --self-test

install: $(BIN)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) $(BIN) $(DESTDIR)$(BINDIR)/$(PROJECT)
	$(INSTALL) -d $(DESTDIR)$(DATAROOTDIR)/doc/$(PROJECT)
	$(INSTALL_DATA) README.md $(DESTDIR)$(DATAROOTDIR)/doc/$(PROJECT)/README.md
	@echo "==> installed $(DESTDIR)$(BINDIR)/$(PROJECT)"
ifeq ($(strip $(GAME_DATA)),)
	@echo "    note: no --with-game-data was configured, so the installed"
	@echo "    binary needs --data DIR or \$$COAB_DATA to find the .DAX files."
endif

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROJECT)
	rm -rf $(DESTDIR)$(DATAROOTDIR)/doc/$(PROJECT)

info:
	@echo "configured  : $(if $(filter yes,$(CONFIGURED)),yes (config.mk),no (Makefile auto-detect))"
	@echo "SDL backend : $(SDL_NAME)"
	@echo "CC          : $(CC)"
	@echo "SDL_CFLAGS  : $(SDL_CFLAGS)"
	@echo "SDL_LIBS    : $(SDL_LIBS)"
	@echo "prefix      : $(PREFIX)"
	@echo "game data   : $(if $(strip $(GAME_DATA)),$(GAME_DATA),searched at run time)"
	@echo "sources     : $(notdir $(SRCS))"

clean:
	rm -rf $(BUILDDIR)

distclean: clean
	rm -f config.mk config.log

-include $(DEPS)
