/* ecl.c - Ported from Classes/EclBlock.cs and Classes/Struct_1B2CA.cs. */
#include <stdarg.h>
#include <string.h>

#include "ecl.h"

#include "gbl.h"
#include "log.h"

/* Offsets arrive as 16-bit segment arithmetic, so they are masked first, then
 * checked. An offset that leaves the block means the script asked for memory
 * this port does not model; the C# raised IndexOutOfRangeException there. */
static bool loc_ok(const char *what, int loc, size_t size, size_t width,
                   size_t *out)
{
    size_t at = (size_t)(loc & 0xffff);

    if (at + width > size) {
        log_warn("%s: offset 0x%zx is outside the 0x%zx byte block",
                 what, at, size);
        return false;
    }
    *out = at;
    return true;
}

/* ---------------------------------------------------------------- EclBlock */

void ecl_block_clear(EclBlock *b)
{
    memset(b->data, 0, sizeof(b->data));
}

bool ecl_block_set_data(EclBlock *b, const u8 *data, size_t data_size,
                        size_t offset, size_t length)
{
    if (length > ECL_BLOCK_SIZE) {
        log_warn("ECL block: script is %zu bytes, block holds 0x%x",
                 length, ECL_BLOCK_SIZE);
        return false;
    }
    if (offset + length > data_size) {
        log_warn("ECL block: script at %zu + %zu runs past the %zu byte source",
                 offset, length, data_size);
        return false;
    }
    memcpy(b->data, data + offset, length);

    return true;
}

u8 ecl_block_get(const EclBlock *b, int loc)
{
    size_t at;

    if (!loc_ok("ECL block", loc, ECL_BLOCK_SIZE, 1, &at)) {
        return 0;
    }
    return b->data[at];
}

void ecl_block_set(EclBlock *b, int loc, u8 value)
{
    size_t at;

    if (!loc_ok("ECL block", loc, ECL_BLOCK_SIZE, 1, &at)) {
        return;
    }
    b->data[at] = value;
}

bool ecl_block_write(const EclBlock *b, u8 *data, size_t data_size)
{
    if (data_size < ECL_BLOCK_SIZE) {
        log_warn("ECL block: write needs 0x%x bytes, given %zu",
                 ECL_BLOCK_SIZE, data_size);
        return false;
    }
    memcpy(data, b->data, ECL_BLOCK_SIZE);

    return true;
}

/* ----------------------------------------------------------------- EclVars */

void ecl_vars_clear(EclVars *v)
{
    memset(v->data, 0, sizeof(v->data));
}

bool ecl_vars_load(EclVars *v, const u8 *data, size_t data_size, size_t offset)
{
    if (offset + ECL_VARS_SIZE > data_size) {
        log_warn("ECL vars: need 0x%x bytes at %zu but the buffer is %zu",
                 ECL_VARS_SIZE, offset, data_size);
        return false;
    }
    memcpy(v->data, data + offset, ECL_VARS_SIZE);

    return true;
}

bool ecl_vars_write(const EclVars *v, u8 *data, size_t data_size)
{
    if (data_size < ECL_VARS_SIZE) {
        log_warn("ECL vars: write needs 0x%x bytes, given %zu",
                 ECL_VARS_SIZE, data_size);
        return false;
    }
    memcpy(data, v->data, ECL_VARS_SIZE);

    return true;
}

u16 ecl_vars_get(const EclVars *v, int loc)
{
    size_t at;

    if (!loc_ok("ECL vars", loc, ECL_VARS_SIZE, 2, &at)) {
        return 0;
    }
    return sys_array_to_ushort(v->data, (int)at);
}

void ecl_vars_set(EclVars *v, int loc, u16 value)
{
    size_t at;

    if (!loc_ok("ECL vars", loc, ECL_VARS_SIZE, 2, &at)) {
        return;
    }
    v->data[at + 0] = (u8)(value & 0xff);
    v->data[at + 1] = (u8)((value >> 8) & 0xff);
}

/* ------------------------------------------------------------------- EclOp */

void ecl_op_clear(EclOp *op)
{
    op->code_set = false;
    op->low_set  = false;
    op->high_set = false;
}

static bool set_once(bool *flag, const char *what)
{
    if (*flag) {
        log_warn("ECL operand: %s has already been set this instruction", what);
        return false;
    }
    *flag = true;

    return true;
}

bool ecl_op_set_code(EclOp *op, u8 code)
{
    if (!set_once(&op->code_set, "the code")) {
        return false;
    }
    op->code = code;

    return true;
}

bool ecl_op_set_low(EclOp *op, u8 low)
{
    if (!set_once(&op->low_set, "the low byte")) {
        return false;
    }
    op->low = low;

    return true;
}

bool ecl_op_set_high(EclOp *op, u8 high)
{
    if (!set_once(&op->high_set, "the high byte")) {
        return false;
    }
    op->high = high;
    op->word = (u16)(op->low + (high << 8));

    return true;
}

static bool is_set(bool flag, const char *what)
{
    if (flag) {
        return true;
    }
    log_warn("ECL operand: %s has not been set", what);

    return false;
}

u8 ecl_op_code(const EclOp *op)
{
    return is_set(op->code_set, "the code") ? op->code : 0;
}

u8 ecl_op_low(const EclOp *op)
{
    return is_set(op->low_set, "the low byte") ? op->low : 0;
}

u8 ecl_op_high(const EclOp *op)
{
    return is_set(op->high_set, "the high byte") ? op->high : 0;
}

u16 ecl_op_word(const EclOp *op)
{
    /* The word is only complete once the high byte has arrived, which is why the
     * original guarded it on highSet rather than on lowSet. */
    return is_set(op->high_set, "the word") ? op->word : 0;
}

u16 ecl_op_value(const EclOp *op)
{
    if (!is_set(op->code_set, "the code")) {
        return 0;
    }

    switch (op->code) {
    case 0x00:
        return is_set(op->low_set, "the low byte") ? op->low : 0;

    case 0x01:
    case 0x03:
    case 0x80:
        if (!is_set(op->high_set, "the word")) {
            return 0;
        }
        if (op->get_memory_value == NULL) {
            log_warn("ECL operand: code 0x%02x needs a memory reader", op->code);
            return 0;
        }
        return op->get_memory_value(op->word);

    case 0x02:
    case 0x81:
        return is_set(op->high_set, "the word") ? op->word : 0;

    default:
        log_warn("ECL operand: code 0x%02x is not an operand form", op->code);
        return 0;
    }
}

/* engine/VmOpp.cs: VmLog.Write / VmLog.WriteLine. */
void ecl_vm_log(const char *fmt, ...)
{
    va_list args;

    if (!gbl.print_commands) {
        return;
    }

    va_start(args, fmt);
    log_vwrite(LOG_LEVEL_DEBUG, fmt, args);
    va_end(args);
}
