/* ecl.h - the two raw memory blocks the ECL script interpreter works in.
 * Ported from Classes/EclBlock.cs and Classes/Struct_1B2CA.cs.
 *
 * ECL is the game's own bytecode: every map and encounter is a script that pokes
 * game state, prints text and asks questions. It addresses memory the way the
 * DOS build laid it out, so ovr008 hands these blocks offsets built from 16-bit
 * segment arithmetic which wraps - ecl_block_get(b, loc + 0x8000) is a normal
 * thing for it to do. The mask is kept; leaving the block is not, since a script
 * that did would read or write past the allocation.
 */
#ifndef COAB_ECL_H
#define COAB_ECL_H

#include "coab.h"

/* The script itself: code and its data, byte addressed. */
#define ECL_BLOCK_SIZE  0x1e00

typedef struct {
    u8 data[ECL_BLOCK_SIZE];
} EclBlock;

void ecl_block_clear(EclBlock *b);

/* Copies length bytes into the block, from data[offset..]. Refuses a length that
 * would not fit rather than truncating silently, since a partly loaded script
 * would run into whatever the tail of the last one left behind. */
bool ecl_block_set_data(EclBlock *b, const u8 *data, size_t data_size,
                        size_t offset, size_t length);

u8   ecl_block_get(const EclBlock *b, int loc);
void ecl_block_set(EclBlock *b, int loc, u8 value);

/* ToByteArray: copies the block out for the save file. */
bool ecl_block_write(const EclBlock *b, u8 *data, size_t data_size);

/* The script's scratch memory: word addressed, and where ECL string operations
 * build their text one character to a word (Struct_1B2CA). */
#define ECL_VARS_SIZE   0x400

typedef struct {
    u8 data[ECL_VARS_SIZE];
} EclVars;

void ecl_vars_clear(EclVars *v);
bool ecl_vars_load(EclVars *v, const u8 *data, size_t data_size, size_t offset);
bool ecl_vars_write(const EclVars *v, u8 *data, size_t data_size);

u16  ecl_vars_get(const EclVars *v, int loc);
void ecl_vars_set(EclVars *v, int loc, u16 value);

/* --------------------------------------------------------- ECL operands */

/* One decoded operand of the instruction being run (Classes/Opperation.cs).
 * vm_LoadCmdSets fills these in from the script byte by byte: the code first,
 * then the low byte, then - for the codes that take a word - the high byte, which
 * is when the word becomes readable.
 *
 * The C# threw InvalidOperationException both for reading a byte that had not
 * been set and for setting one twice; those throws were assertions that the
 * decoder walks the operand in order. Here they log and answer zero, because an
 * ECL script bug should not take the game down mid-encounter.
 */
typedef u16 (*EclMemoryReader)(u16 loc);

typedef struct {
    bool code_set;
    bool low_set;
    bool high_set;

    u8   code;
    u8   low;
    u8   high;
    u16  word;

    EclMemoryReader get_memory_value;
} EclOp;

/* One operand set per instruction; index 0 goes unused because the decoder
 * numbers operands from 1 (Gbl.cs: cmdOppsLimit). */
#define ECL_CMD_OPS_LIMIT 0x40

/* Clear(): forgets what has been set without touching the bytes themselves,
 * exactly as the original did. */
void ecl_op_clear(EclOp *op);

/* Each returns false, having logged, if that byte has already been set. Setting
 * the high byte also builds the word out of low and high. */
bool ecl_op_set_code(EclOp *op, u8 code);
bool ecl_op_set_low(EclOp *op, u8 low);
bool ecl_op_set_high(EclOp *op, u8 high);

/* Zero, having logged, for a byte that has not been set yet. */
u8   ecl_op_code(const EclOp *op);
u8   ecl_op_low(const EclOp *op);
u8   ecl_op_high(const EclOp *op);
u16  ecl_op_word(const EclOp *op);

/* GetCmdValue: the operand's value. Code 0 is an immediate byte, 2 and 0x81 are
 * an immediate word, and 1, 3 and 0x80 read the word as an address. */
u16  ecl_op_value(const EclOp *op);

/* engine/VmOpp.cs: VmLog. The interpreter traces every instruction it runs, but
 * only when gbl.print_commands is on - the trace is enormous otherwise. The C#
 * had a Write and a WriteLine; nothing in the port writes half a line, so this
 * is the one function.
 *
 * VmOpp.cs also held MemLoc, a wrapper whose only purpose was to make a ushort
 * print as 0x1234 in a trace line; a format specifier does that here. */
void ecl_vm_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* COAB_ECL_H */
