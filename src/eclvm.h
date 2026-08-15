/* eclvm.h - the ECL interpreter's instruction set and its driver loops.
 * Ported from engine/ovr003.cs.
 *
 * vm.h is the machine - operand decoding, memory, text, the shared screen and
 * party routines. This is the instruction set on top of it: 0x41 opcodes, from
 * GOTO and COMPARE through to ENCOUNTER MENU and TREASURE, and the loops that
 * keep running scripts while the party walks around a map.
 *
 * An instruction is one opcode byte at gbl.ecl_offset followed by its operands,
 * and every handler is responsible for stepping gbl.ecl_offset past its own
 * instruction - which for most of them happens inside vm_load_cmd_sets, and for
 * the ones that take nothing is a bare ++. A handler that jumps just assigns
 * gbl.ecl_offset instead. Nothing here returns a value; the interpreter's whole
 * state is in gbl, because a script can start another script (camping) and the
 * original let the inner run clobber the outer one's operands.
 *
 * Six handlers cover more than one opcode and read gbl.command back to find out
 * which they are - ADD/SUBTRACT/DIVIDE/MULTIPLY are one routine, so are the six
 * conditional jumps, AND/OR, ON GOTO/ON GOSUB, PRINT/PRINTCLEAR and LOAD
 * FILES/LOAD PIECES.
 *
 * Three of them - VERTICAL MENU, HORIZONTAL MENU and ON GOTO - take a count as
 * one of their fixed operands and then as many more operands as the count says.
 * They decode the fixed part, step gbl.ecl_offset back one - undoing the step onto
 * the next opcode that vm_load_cmd_sets ends with - and decode the rest from where
 * the first pass stopped. The second pass renumbers gbl.cmd_opps and
 * gbl.ecl_strings from 1 again, so anything the fixed part left in either has to
 * be copied out first. This is also why their table entry says size 0: the length
 * is not knowable without running them.
 */
#ifndef COAB_ECLVM_H
#define COAB_ECLVM_H

#include "coab.h"

/* ------------------------------------------------------- the instruction set */

/* Opcodes 0x00 to 0x40; there is no 0x41. */
#define ECLVM_COMMAND_COUNT 0x41

/* The mnemonic the original's trace printed ("GOTO", "ENCOUNTER MENU"), or NULL
 * for an opcode outside the table. */
const char *eclvm_command_name(int opcode);

/* How many operands the instruction carries, for stepping over one without
 * running it. 0 means either "no operands" or "only the handler knows"; see the
 * note above. -1 for an opcode outside the table. */
int eclvm_command_size(int opcode);

/* Whether the port has a handler for this opcode. Every opcode in the table has
 * one except 0x1F, which the disassembly never resolved - the C# left its handler
 * null and would have thrown had a script ever reached it. */
bool eclvm_command_known(int opcode);

/* ---------------------------------------------------------- the interpreter */

/* ovr003.RunEclVm, sub_29607. Runs the script from `offset` until an instruction
 * stops the interpreter - EXIT, or NEWECL replacing the script under it - or the
 * party dies. Re-entrant: camping runs a script from inside one.
 *
 * The C# only logged an opcode it had no entry for and went round again, which
 * hung the game on the same byte forever. Here it logs and steps one byte, so a
 * script that has gone off the rails walks forward and eventually finds an EXIT
 * instead of freezing. */
void eclvm_run(u16 offset);

/* ovr003.TryEncamp. Asks the script whether the party may camp here, runs the
 * camp, and lets the script interrupt it. */
void eclvm_try_encamp(void);

/* ovr003.sub_29677. Re-runs a script's three entry points after NEWECL swapped
 * the script out from under the interpreter, and keeps doing it while each new
 * script swaps itself out again. */
void eclvm_restart_after_new_ecl(void);

/* ovr003.sub_29758. The world outside combat: load the map's script, run its
 * entry point, then loop taking a command from the dungeon menu and running the
 * movement and search handlers after each one, until the party dies. */
void eclvm_world_loop(void);

#endif /* COAB_ECLVM_H */
