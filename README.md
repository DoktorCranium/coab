# Curse of the Azure Bonds (CoAB) — Linux/UNIX/OpenVMS C / SDL port

This is a C port of the reverse-engineered CoAB engine originally created by 
Simeon Pilgrim in C# for Windows. 

https://simeonpilgrim.com/blog/curse-of-the-azure-bonds

https://github.com/simeonpilgrim/coab

It is ported to C99 and SDL2 for Linux/UNIX. 
On OpenVMS 8.4 SDL-1.2 is needed. 


OpenVMS 
=======

## Build amd run

Make sure you have SDL-1.2 installed and setup
HP C V7.3-009 on OpenVMS Alpha V8.4   

```
@configure.com 
@build 
coab :== $DISK:[PATH]COAB.EXE 
coab --scale 2 
```


UNIX/Linux 
==========

## Build and run

Three ways in, all producing the same binary. Plain make is enough when SDL2 is
installed normally and sdl2-config is in path :

```
sh
make                 # -> build/coab
make test            # headless self-test, writes PPMs, needs no display
make info            # show the detected SDL flags
./build/coab --data ../Data

'./configure' when SDL2 is somewhere unusual, or to pin down install paths:

sh
./configure --with-sdl-prefix=/opt/sdl2
./configure --help                     # all options
make && make test && make install
```
CMake, for the same reasons:

```
sh
cmake -B build-cmake -DSDL2_PREFIX=/opt/sdl2
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
cmake --install build-cmake --prefix /usr/local
```

Use `build-cmake`, not `build`: the latter is where the Makefile puts its object
files.

### Finding SDL2 in a non-standard place

Each build system tries, in order: SDL2's own CMake package (CMake only),
`sdl2-config`, `pkg-config`, and finally the bare header and library — that last
route exists because plenty of hand-built SDL2 trees ship neither `sdl2.pc` nor
`sdl2-config`.

**A library outside the loader's default path is recorded in the binary.**
Otherwise it links against the SDL2 that was configured and then loads a
different one at run time. `./configure` reports the version the library actually
loads, not the version its `.pc` file claims, and warns when the headers and that
library disagree.

`configure` writes `config.mk`, which the Makefile includes; `make distclean`
removes it. Everything it detects is visible in `make info`, and every command it
tried is in `config.log`.

### Run-time options

`--data <dir>`, `--sounds <dir>`, `--scale N`, `--square` (present 320x200
unstretched), `--fullscreen`, `--no-sound`, `--skip-title`,
`--self-test [--out <dir>]`, `--verbose`.

The data directory is searched for in this order: `--data`, `$COAB_DATA`, `Data`
next to the current directory or the executable, and last a directory compiled in
via `./configure --with-game-data=DIR` or `cmake -DCOAB_DATA_DIR=DIR`. Compiled-in
comes last on purpose, so a build run from the source tree still prefers the
`Data` sitting next to it.

While running: `F11` or `Alt+Enter` toggles fullscreen, `F10` toggles aspect
correction, `Ctrl+Q` quits.

### Playing it

The game is a 1989 DOS game and the keys are its own. In the prompt under the
picture you pick a word by typing its capital letter, or walk the highlight along
with `,` and `.` and press Return. Anywhere a selection moves — down a list, along
the party, down the lines of the modify-character sheet — the original used **Home
and End**, i.e. keypad **7** and **1**, with `PgUp` / `PgDn` (keypad 9 and 3) for
whole pages. Keyboards without a keypad were already catered for by the C#, which
aliases `[` and `]` onto Home and End; this port additionally lets the **arrow
keys** do it — up and down move the selection, left and right walk the highlight
the way `,` and `.` do — which is the one change it makes to what a player sees
(see *Notes on the port*). In the dungeon and in a fight the arrows have always
worked: up walks forward, down turns about, left and right turn, and on the combat
map the keypad's corners are the diagonals.


