/* affect.c - Ported from Classes/Affect.cs. */
#include <string.h>

#include "affect.h"
#include "log.h"

static const DioField affect_fields[] = {
    DIO_F(Affect, type,              0x00, DIO_IBYTE, 0),
    DIO_F(Affect, minutes,           0x01, DIO_WORD,  0),
    DIO_F(Affect, affect_data,       0x03, DIO_BYTE,  0),
    DIO_F(Affect, call_affect_table, 0x04, DIO_BOOL,  0),
    /* 0x05..0x08 was the pointer to the next affect. */
    DIO_END_MARKER
};

const DioDesc affect_desc = { "Affect", AFFECT_RECORD_SIZE, affect_fields };

void affect_init(Affect *a, Affects type, u16 minutes, u8 affect_data,
                 bool call_affect_table)
{
    memset(a, 0, sizeof(*a));
    a->type              = (int)type;
    a->minutes           = minutes;
    a->affect_data       = affect_data;
    a->call_affect_table = call_affect_table;
}

bool affect_read(Affect *a, const u8 *data, size_t data_size, size_t offset)
{
    memset(a, 0, sizeof(*a));
    return dio_read(&affect_desc, a, data, data_size, offset);
}

bool affect_write(const Affect *a, u8 *data, size_t data_size)
{
    if (data_size < AFFECT_RECORD_SIZE) {
        return false;
    }
    memset(data, 0, AFFECT_RECORD_SIZE);
    return dio_write(&affect_desc, a, data, data_size);
}

/* ---------------------------------------------------------------- the list */

void affect_list_clear(AffectList *l)
{
    l->count = 0;
}

bool affect_list_add(AffectList *l, const Affect *a)
{
    if (l->count >= AFFECT_LIST_MAX) {
        log_warn("affect list full (%d), dropping affect %d",
                 AFFECT_LIST_MAX, a->type);
        return false;
    }
    l->items[l->count++] = *a;
    return true;
}

void affect_list_remove_at(AffectList *l, int index)
{
    if (index < 0 || index >= l->count) {
        log_warn("affect_list_remove_at: index %d outside 0..%d",
                 index, l->count - 1);
        return;
    }
    memmove(&l->items[index], &l->items[index + 1],
            (size_t)(l->count - index - 1) * sizeof(l->items[0]));
    l->count--;
}

bool affect_list_remove(AffectList *l, const Affect *a)
{
    if (a < l->items || a >= l->items + l->count) {
        log_warn("affect_list_remove: affect is not on this list");
        return false;
    }

    affect_list_remove_at(l, (int)(a - l->items));

    return true;
}

bool affect_list_has(const AffectList *l, Affects type)
{
    return affect_list_find_const(l, type) != NULL;
}

Affect *affect_list_find(AffectList *l, Affects type)
{
    for (int i = 0; i < l->count; i++) {
        if (l->items[i].type == (int)type) {
            return &l->items[i];
        }
    }
    return NULL;
}

const Affect *affect_list_find_const(const AffectList *l, Affects type)
{
    for (int i = 0; i < l->count; i++) {
        if (l->items[i].type == (int)type) {
            return &l->items[i];
        }
    }
    return NULL;
}

int affect_list_remove_type(AffectList *l, Affects type)
{
    int out = 0;
    int removed;

    for (int i = 0; i < l->count; i++) {
        if (l->items[i].type != (int)type) {
            l->items[out++] = l->items[i];
        }
    }
    removed = l->count - out;
    l->count = out;

    return removed;
}

bool affect_list_is_held(const AffectList *l)
{
    static const Affects held[] = {
        AFFECT_SNAKE_CHARM, AFFECT_PARALYZE, AFFECT_SLEEP, AFFECT_HELPLESS
    };

    for (size_t i = 0; i < COAB_ARRAY_LEN(held); i++) {
        if (affect_list_has(l, held[i])) {
            return true;
        }
    }
    return false;
}
