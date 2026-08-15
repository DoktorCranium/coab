/* dataio.c - reading and writing the game's fixed-layout records.
 * Ported from Classes/DataIO.cs and the array/string helpers in Classes/Sys.cs.
 */
#include <string.h>

#include "dataio.h"
#include "log.h"

size_t dio_span(DioType type, unsigned count)
{
    switch (type) {
    case DIO_PSTRING:     return count + 1;
    case DIO_BYTE:
    case DIO_SBYTE:
    case DIO_IBYTE:
    case DIO_ISBYTE:
    case DIO_BOOL:        return 1;
    case DIO_WORD:
    case DIO_SWORD:       return 2;
    case DIO_INT:         return 4;
    case DIO_BYTE_ARRAY:  return count;
    case DIO_SHORT_ARRAY:
    case DIO_WORD_ARRAY:  return count * 2;
    case DIO_CUSTOM:
    case DIO_END:         return 0;
    }
    return 0;
}

/* ------------------------------------------------------------ Pascal strings */

void sys_array_to_string(char *dst, size_t dst_size,
                         const u8 *data, size_t offset, unsigned max_len)
{
    unsigned len;
    size_t   out = 0;

    if (dst_size == 0) {
        return;
    }

    len = data[offset];
    if (len > max_len) {
        len = max_len;
    }
    if (len > dst_size - 1) {
        len = (unsigned)(dst_size - 1);
    }

    /* A NUL is padding, not text: Sys.StringToArray zero-fills the tail but
     * still records the full capacity in the length byte. */
    for (unsigned i = 1; i <= len; i++) {
        u8 c = data[offset + i];
        if (c != 0) {
            dst[out++] = (char)c;
        }
    }
    dst[out] = '\0';
}

void sys_string_to_array(u8 *data, size_t offset, unsigned length, const char *s)
{
    size_t n = s != NULL ? strlen(s) : 0;

    data[offset] = (u8)length;
    for (unsigned i = 1; i <= length; i++) {
        data[offset + i] = (i <= n) ? (u8)s[i - 1] : 0;
    }
}

void sys_short_to_array(i16 value, u8 *data, size_t offset)
{
    data[offset + 0] = (u8)((u16)value & 0xff);
    data[offset + 1] = (u8)(((u16)value >> 8) & 0xff);
}

void sys_int_to_array(i32 value, u8 *data, size_t offset)
{
    data[offset + 0] = (u8)((u32)value & 0xff);
    data[offset + 1] = (u8)(((u32)value >> 8) & 0xff);
    data[offset + 2] = (u8)(((u32)value >> 16) & 0xff);
    data[offset + 3] = (u8)(((u32)value >> 24) & 0xff);
}

/* ------------------------------------------------------------------ helpers */

/* Every field is checked against the buffer before it is touched. A short or
 * corrupt record is a data error to be logged, not a read past the end of an
 * allocation. */
static bool fits(const DioDesc *desc, const DioField *f,
                 size_t base, size_t data_size, const char *what)
{
    size_t end = base + f->rec + f->span;

    if (end > data_size || end < base) {
        log_warn("%s: %s record field %s (%u..%u) past end of %zu byte buffer",
                 desc->name, what, f->name != NULL ? f->name : "?",
                 (unsigned)(base + f->rec), (unsigned)end, data_size);
        return false;
    }
    return true;
}

static bool desc_fits(const DioDesc *desc, size_t base, size_t data_size,
                      const char *what)
{
    if (base + desc->record_size > data_size || base + desc->record_size < base) {
        log_warn("%s: %s needs %zu bytes at %zu but the buffer is %zu",
                 desc->name, what, desc->record_size, base, data_size);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------- reading */

bool dio_read(const DioDesc *desc, void *obj,
              const u8 *data, size_t data_size, size_t offset)
{
    u8 *base = (u8 *)obj;

    if (!desc_fits(desc, offset, data_size, "read")) {
        return false;
    }

    for (const DioField *f = desc->fields; f->type != DIO_END; f++) {
        size_t at = offset + f->rec;

        if (!fits(desc, f, offset, data_size, "read")) {
            return false;
        }

        switch (f->type) {
        case DIO_BYTE:
            *(u8 *)(base + f->mem) = data[at];
            break;

        case DIO_SBYTE:
            *(i8 *)(base + f->mem) = (i8)data[at];
            break;

        case DIO_IBYTE:
            /* One byte on disk widened to the int an enum field holds. */
            *(int *)(base + f->mem) = data[at];
            break;

        case DIO_ISBYTE:
            *(int *)(base + f->mem) = (i8)data[at];
            break;

        case DIO_WORD:
            *(u16 *)(base + f->mem) = sys_array_to_ushort(data, (int)at);
            break;

        case DIO_SWORD:
            *(i16 *)(base + f->mem) = sys_array_to_short(data, (int)at);
            break;

        case DIO_INT:
            *(i32 *)(base + f->mem) = sys_array_to_int(data, (int)at);
            break;

        case DIO_BOOL:
            *(bool *)(base + f->mem) = (data[at] != 0);
            break;

        case DIO_BYTE_ARRAY:
            memcpy(base + f->mem, data + at, f->count);
            break;

        case DIO_SHORT_ARRAY: {
            i16 *dst = (i16 *)(base + f->mem);
            for (unsigned i = 0; i < f->count; i++) {
                dst[i] = sys_array_to_short(data, (int)(at + i * 2));
            }
            break;
        }

        case DIO_WORD_ARRAY: {
            u16 *dst = (u16 *)(base + f->mem);
            for (unsigned i = 0; i < f->count; i++) {
                dst[i] = sys_array_to_ushort(data, (int)(at + i * 2));
            }
            break;
        }

        case DIO_PSTRING:
            /* The struct member is a char array of count + 1 bytes; the
             * descriptor guarantees that, so the capacity passed here is the
             * one the table declared. */
            sys_array_to_string((char *)(base + f->mem), (size_t)f->count + 1,
                                data, at, f->count);
            break;

        case DIO_CUSTOM:
            if (f->read != NULL) {
                f->read(base + f->mem, data, at);
            }
            break;

        case DIO_END:
            break;
        }
    }

    return true;
}

/* ------------------------------------------------- access by record offset */

/* Finds the field covering loc. The C# matched only on a field's first byte -
 * DataOffset.Offset == location - and Area1/Area2 made up for that with a
 * hand-written switch listing every element of their short arrays. Covering the
 * whole span here means the arrays need no switch and no element can be missed.
 */
static const DioField *field_at(const DioDesc *desc, size_t loc, size_t *index)
{
    for (const DioField *f = desc->fields; f->type != DIO_END; f++) {
        if (loc < f->rec || loc >= (size_t)f->rec + f->span) {
            continue;
        }
        if (f->type == DIO_CUSTOM || f->type == DIO_PSTRING) {
            /* No single word to read or write: the handler owns those bytes. */
            return NULL;
        }
        *index = loc - f->rec;
        return f;
    }
    return NULL;
}

bool dio_word_get(const DioDesc *desc, const void *obj, size_t loc, u16 *out)
{
    const u8      *base = (const u8 *)obj;
    const DioField *f;
    size_t          i = 0;

    f = field_at(desc, loc, &i);
    if (f == NULL) {
        return false;
    }

    switch (f->type) {
    case DIO_BYTE:        *out = *(const u8 *)(base + f->mem); return true;
    case DIO_SBYTE:       *out = (u16)*(const i8 *)(base + f->mem); return true;
    case DIO_IBYTE:
    case DIO_ISBYTE:      *out = (u16)*(const int *)(base + f->mem); return true;
    case DIO_WORD:        *out = *(const u16 *)(base + f->mem); return true;
    case DIO_SWORD:       *out = (u16)*(const i16 *)(base + f->mem); return true;
    case DIO_BOOL:        *out = *(const bool *)(base + f->mem) ? 1 : 0; return true;
    case DIO_BYTE_ARRAY:  *out = ((const u8 *)(base + f->mem))[i]; return true;
    case DIO_SHORT_ARRAY: *out = (u16)((const i16 *)(base + f->mem))[i / 2]; return true;
    case DIO_WORD_ARRAY:  *out = ((const u16 *)(base + f->mem))[i / 2]; return true;

    case DIO_INT:
        /* Read the half the offset lands in, since the caller asked for a
         * word. */
        *out = (u16)((u32)*(const i32 *)(base + f->mem) >> (i >= 2 ? 16 : 0));
        return true;

    case DIO_PSTRING:
    case DIO_CUSTOM:
    case DIO_END:
        break;
    }
    return false;
}

bool dio_word_set(const DioDesc *desc, void *obj, size_t loc, u16 value)
{
    u8             *base = (u8 *)obj;
    const DioField *f;
    size_t          i = 0;

    f = field_at(desc, loc, &i);
    if (f == NULL) {
        return false;
    }

    switch (f->type) {
    case DIO_BYTE:        *(u8 *)(base + f->mem) = (u8)value; return true;
    case DIO_SBYTE:       *(i8 *)(base + f->mem) = (i8)value; return true;
    case DIO_IBYTE:       *(int *)(base + f->mem) = (u8)value; return true;
    case DIO_ISBYTE:      *(int *)(base + f->mem) = (i8)value; return true;
    case DIO_WORD:        *(u16 *)(base + f->mem) = value; return true;
    case DIO_SWORD:       *(i16 *)(base + f->mem) = (i16)value; return true;
    case DIO_BOOL:        *(bool *)(base + f->mem) = (value != 0); return true;
    case DIO_BYTE_ARRAY:  ((u8 *)(base + f->mem))[i] = (u8)value; return true;
    case DIO_SHORT_ARRAY: ((i16 *)(base + f->mem))[i / 2] = (i16)value; return true;
    case DIO_WORD_ARRAY:  ((u16 *)(base + f->mem))[i / 2] = value; return true;

    case DIO_INT: {
        u32 cur   = (u32)*(i32 *)(base + f->mem);
        int shift = i >= 2 ? 16 : 0;

        cur &= ~((u32)0xffff << shift);
        *(i32 *)(base + f->mem) = (i32)(cur | ((u32)value << shift));
        return true;
    }

    case DIO_PSTRING:
    case DIO_CUSTOM:
    case DIO_END:
        break;
    }
    return false;
}

/* ------------------------------------------------------------------- writing */

bool dio_write(const DioDesc *desc, const void *obj, u8 *data, size_t data_size)
{
    const u8 *base = (const u8 *)obj;

    if (!desc_fits(desc, 0, data_size, "write")) {
        return false;
    }

    for (const DioField *f = desc->fields; f->type != DIO_END; f++) {
        size_t at = f->rec;

        if (!fits(desc, f, 0, data_size, "write")) {
            return false;
        }

        switch (f->type) {
        case DIO_BYTE:
            data[at] = *(const u8 *)(base + f->mem);
            break;

        case DIO_SBYTE:
            data[at] = (u8)*(const i8 *)(base + f->mem);
            break;

        case DIO_IBYTE:
        case DIO_ISBYTE:
            data[at] = (u8)*(const int *)(base + f->mem);
            break;

        case DIO_WORD:
            sys_short_to_array((i16)*(const u16 *)(base + f->mem), data, at);
            break;

        case DIO_SWORD:
            sys_short_to_array(*(const i16 *)(base + f->mem), data, at);
            break;

        case DIO_INT:
            sys_int_to_array(*(const i32 *)(base + f->mem), data, at);
            break;

        case DIO_BOOL:
            data[at] = *(const bool *)(base + f->mem) ? 1 : 0;
            break;

        case DIO_BYTE_ARRAY:
            memcpy(data + at, base + f->mem, f->count);
            break;

        case DIO_SHORT_ARRAY: {
            const i16 *src = (const i16 *)(base + f->mem);
            for (unsigned i = 0; i < f->count; i++) {
                sys_short_to_array(src[i], data, at + i * 2);
            }
            break;
        }

        case DIO_WORD_ARRAY: {
            const u16 *src = (const u16 *)(base + f->mem);
            for (unsigned i = 0; i < f->count; i++) {
                sys_short_to_array((i16)src[i], data, at + i * 2);
            }
            break;
        }

        case DIO_PSTRING: {
            /* DataIO's spelling: the length byte is the string length, capped
             * at the field capacity. Item uses sys_string_to_array instead,
             * which records the capacity. */
            const char *s = (const char *)(base + f->mem);
            size_t n = strlen(s);

            if (n > f->count) {
                n = f->count;
            }
            data[at] = (u8)n;
            for (size_t i = 0; i < n; i++) {
                data[at + 1 + i] = (u8)s[i];
            }
            break;
        }

        case DIO_CUSTOM:
            if (f->write != NULL) {
                f->write(base + f->mem, data, at);
            }
            break;

        case DIO_END:
            break;
        }
    }

    return true;
}
