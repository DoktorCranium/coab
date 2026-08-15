/* program.h - the top of the game: what loads once, what is reset between
 * games, and the loop that runs one.
 * Ported from the tail of engine/seg001.cs (InitFirst, InitAgain, PROGRAM).
 *
 * seg001 is the DOS executable's own start-up segment, and it is split three
 * ways in this port because the C tree already had two of the pieces:
 *
 *   gbl_init()   engine/seg001.cs InitFirst's allocating half - every record,
 *                block and table the rest of the game reaches through gbl, and
 *                the scalars that go with them. It runs before there is a
 *                display or a data file open, so nothing that reads a file can
 *                be in it.
 *
 *   main.c       __SystemInit and ConfigGame: the working directory, the SDL
 *                layer, sound, the 8x8 font, and PROGRAM's PlaySound. Those sit
 *                ahead of the --self-test branch, so the self-test gets them
 *                too.
 *
 *   here         what is left, which is everything that needs both: the art and
 *                the item table InitFirst loads off disk, InitAgain's reset, and
 *                PROGRAM.
 *
 * Two things PROGRAM opens with are not functions here at all. Its CombatMap
 * allocation is part of gbl_init ("God damm 1-n arrays"), and
 * ovr003.SetupCommandTable built a dictionary of delegates that in this port is
 * what it was in the DOS build: a static table in eclvm.c indexed by the opcode.
 * Classes/ItemLibrary.Read is deliberately not ported - see item.h.
 */
#ifndef COAB_PROGRAM_H
#define COAB_PROGRAM_H

#include "coab.h"

/* seg001.InitFirst, sub_39054 - the part gbl_init could not do, in the order the
 * original did it: the prompt row cleared under a "Loading...Please Wait", the
 * two 8x8 symbol banks, the twelve combat sprites plus the thirteenth at slot
 * 0x19, the three SKY pictures, and ITEMS.
 *
 * Runs once. gbl.ecl_offset is set here too, since it is the one InitFirst
 * scalar that gbl_init leaves at zero rather than 0x8000. */
void program_init_first(void);

/* seg001.InitAgain, sub_396E5 - everything a new game starts from, which is
 * InitFirst's scalar half again with the loading left out. The original
 * duplicates the list rather than sharing it, and so does this.
 *
 * Four things differ from InitFirst, and they are what makes this a separate
 * routine rather than a call to the same one:
 *
 *   the party faces east, not north (gbl.map_direction 2 against 0)
 *   gbl.in_demo is left alone, so a demo run stays a demo run
 *   nothing is loaded off disk
 *   the prompt area is cleared late, after silent_training rather than before
 *   menu_selected_word - which is only visible if something drew in between,
 *   and nothing does
 *
 * gbl.game_speed_var is set to 4 by both, which is how the demo's speed of 9
 * lasts exactly one game. */
void program_init_again(void);

/* seg001.PROGRAM - the title screen, the demo prompt, the code wheel, and then
 * the loop a game is one turn of: pick the chapter, open the start-game menu,
 * hand over to the world loop, and reset for the next one.
 *
 * Does not return. The loop in the original is `while (true)`, and the only way
 * out of the game is seg043.print_and_exit, which in this port unwinds to
 * main.c's QUIT_SET_EXIT_POINT. */
void program_run(void);

#endif /* COAB_PROGRAM_H */
