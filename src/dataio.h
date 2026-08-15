/* dataio.h - reading and writing the game's fixed-layout records.
 * Ported from Classes/DataIO.cs.
 *
 * The C# port tagged each field with [DataOffset(offset, type, size)] and walked
 * the attributes by reflection. C has no reflection, so each struct declares a
 * table of DioField instead. The table is the same declaration in a different
 * shape: one row per serialized field, giving where the field lives in the
 * record and where it lives in the struct. The engine still reads and writes
 * exactly the byte layout the DOS original used, which is what save games and
 * the shipped data files require.
 *
 *   static const DioField player_fields[] = {
 *       DIO_F(Player, name, 0x00, DIO_PSTRING, 15),
 *       DIO_F(Player, thac0, 0x73, DIO_SBYTE, 0),
 *       ...
 *       DIO_END_MARKER
 *   };
 *
 * Fields the original held as pointers - the item list, the affect list - are
 * absent from these tables, exactly as they were absent from the C# attributes:
 * the record has a hole where the far pointer used to be and nothing meaningful
 * to store in it.
 */
#ifndef COAB_DATAIO_H
#define COAB_DATAIO_H

#include "coab.h"

typedef enum {
    DIO_END = 0,
    DIO_PSTRING,      /* count = capacity; occupies count + 1 bytes on disk */
    DIO_BYTE,         /* u8  */
    DIO_SBYTE,        /* i8  */
    DIO_IBYTE,        /* one byte on disk, int in the struct (enums) */
    DIO_ISBYTE,       /* one signed byte on disk, int in the struct */
    DIO_WORD,         /* u16 */
    DIO_SWORD,        /* i16 */
    DIO_INT,          /* i32 */
    DIO_BOOL,         /* bool, stored as 0 or 1 */
    DIO_BYTE_ARRAY,   /* count bytes */
    DIO_SHORT_ARRAY,  /* count i16 */
    DIO_WORD_ARRAY,   /* count u16 */
    DIO_CUSTOM        /* handled by the field's own read/write pair */
} DioType;

/* A DIO_CUSTOM field supplies these. member points at the field itself, not at
 * the enclosing struct, and offset is already absolute within data. */
typedef void (*DioReadFn)(void *member, const u8 *data, size_t offset);
typedef void (*DioWriteFn)(const void *member, u8 *data, size_t offset);

typedef struct {
    const char *name;      /* for log messages when a record is malformed */
    u16         rec;       /* byte offset within the record */
    u16         mem;       /* offsetof() within the C struct */
    DioType     type;
    u16         count;     /* array elements, or PString capacity */
    u16         span;      /* bytes this field occupies in the record */
    DioReadFn   read;      /* DIO_CUSTOM only */
    DioWriteFn  write;     /* DIO_CUSTOM only */
} DioField;

typedef struct {
    const char     *name;
    size_t          record_size;
    const DioField *fields;
} DioDesc;

/* Bytes a field of the given type and count occupies in the record. */
size_t dio_span(DioType type, unsigned count);

/* Table entries. DIO_F derives the span from the type; DIO_CUSTOM_F takes it
 * explicitly because only the handler knows the layout. */
#define DIO_F(struct_type, member_name, rec_off, dio_type, elem_count)     \
    { #member_name, (rec_off), (u16)offsetof(struct_type, member_name),    \
      (dio_type), (elem_count),                                           \
      (u16)((dio_type) == DIO_PSTRING ? (elem_count) + 1u                 \
            : (dio_type) == DIO_BYTE_ARRAY ? (elem_count)                 \
            : (dio_type) == DIO_SHORT_ARRAY ? (elem_count) * 2u           \
            : (dio_type) == DIO_WORD_ARRAY ? (elem_count) * 2u            \
            : (dio_type) == DIO_WORD || (dio_type) == DIO_SWORD ? 2u      \
            : (dio_type) == DIO_INT ? 4u : 1u),                           \
      NULL, NULL }

#define DIO_CUSTOM_F(struct_type, member_name, rec_off, bytes, rd, wr)    \
    { #member_name, (rec_off), (u16)offsetof(struct_type, member_name),   \
      DIO_CUSTOM, 0, (bytes), (rd), (wr) }

#define DIO_END_MARKER { NULL, 0, 0, DIO_END, 0, 0, NULL, NULL }

/* Reads the record at data[offset..] into obj. Returns false and touches
 * nothing further if the record would run past data_size - the C# port let the
 * CLR raise IndexOutOfRangeException here, which in C would be a buffer
 * overrun instead. */
bool dio_read(const DioDesc *desc, void *obj,
              const u8 *data, size_t data_size, size_t offset);

/* Writes obj into data[0..desc->record_size). data_size must be at least the
 * record size. Bytes no field covers are left as the caller set them, so
 * callers zero the buffer first and the pointer holes stay zero. */
bool dio_write(const DioDesc *desc, const void *obj, u8 *data, size_t data_size);

/* --- access by record offset ---
 *
 * The ECL script interpreter pokes the area blocks by numeric offset, because in
 * the DOS original they were plain memory. These map an offset back onto the
 * field that covers it and return false when no field does, which is the caller's
 * cue to fall back to the block's raw bytes. Replaces DataIO.GetObjectUShort and
 * SetObjectUShort together with Area1.field_6A00_Get and friends. */
bool dio_word_get(const DioDesc *desc, const void *obj, size_t loc, u16 *out);
bool dio_word_set(const DioDesc *desc, void *obj, size_t loc, u16 value);

/* --- Pascal strings (Classes/Sys.cs) ---
 *
 * A Pascal string is a length byte followed by that many characters. Two
 * spellings of the write exist in the original and both are kept, because they
 * put different values in the length byte and records written one way are read
 * back by the engine:
 *
 *   sys_string_to_array  - Sys.StringToArray: length byte is the field capacity
 *                          and the tail is zero-padded. Used by Item.
 *   DIO_PSTRING          - DataIO: length byte is the string length. Used by
 *                          Player.name.
 *
 * Reading tolerates both, since a NUL is never part of the text.
 */

/* Copies the Pascal string at data[offset] into dst as a C string. Never reads
 * more than max_len characters and never writes more than dst_size - 1. */
void sys_array_to_string(char *dst, size_t dst_size,
                         const u8 *data, size_t offset, unsigned max_len);

/* Item-style write: sets the length byte to length and zero-pads. */
void sys_string_to_array(u8 *data, size_t offset, unsigned length, const char *s);

void sys_short_to_array(i16 value, u8 *data, size_t offset);
void sys_int_to_array(i32 value, u8 *data, size_t offset);

#endif /* COAB_DATAIO_H */
