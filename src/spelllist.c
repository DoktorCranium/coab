/* spelllist.c - Ported from Classes/SpellList.cs. */
#include <string.h>

#include "spelllist.h"
#include "log.h"

void spell_list_clear(SpellList *sl)
{
    memset(sl, 0, sizeof(*sl));
}

static bool push(SpellList *sl, int id, bool learning)
{
    if (sl->count >= SPELL_LIST_MAX) {
        log_warn("spell list full (%d), dropping spell %d", SPELL_LIST_MAX, id);
        return false;
    }
    sl->items[sl->count].id       = (u8)id;
    sl->items[sl->count].learning = learning;
    sl->count++;
    return true;
}

bool spell_list_add_learn(SpellList *sl, int id)
{
    return push(sl, id, true);
}

bool spell_list_add_learnt(SpellList *sl, int raw)
{
    /* Bit 7 of the stored byte means "still being memorized". */
    return push(sl, raw & 0x7f, raw > 0x7f);
}

static void remove_at(SpellList *sl, int i)
{
    memmove(&sl->items[i], &sl->items[i + 1],
            (size_t)(sl->count - i - 1) * sizeof(sl->items[0]));
    sl->count--;
}

void spell_list_clear_spell(SpellList *sl, int id)
{
    for (int i = 0; i < sl->count; i++) {
        if (sl->items[i].id == id) {
            remove_at(sl, i);
            return;
        }
    }
}

void spell_list_mark_learnt(SpellList *sl, int id)
{
    for (int i = 0; i < sl->count; i++) {
        if (sl->items[i].id == id && sl->items[i].learning) {
            sl->items[i].learning = false;
            return;
        }
    }
}

void spell_list_cancel_learning(SpellList *sl)
{
    int out = 0;

    for (int i = 0; i < sl->count; i++) {
        if (!sl->items[i].learning) {
            sl->items[out++] = sl->items[i];
        }
    }
    sl->count = out;
}

bool spell_list_has_spells(const SpellList *sl)
{
    return sl->count > 0;
}

bool spell_list_has_spell(const SpellList *sl, int id)
{
    for (int i = 0; i < sl->count; i++) {
        if (sl->items[i].id == id) {
            return true;
        }
    }
    return false;
}

int spell_list_count(const SpellList *sl)
{
    return sl->count;
}

int spell_list_learnt_count(const SpellList *sl)
{
    int n = 0;

    for (int i = 0; i < sl->count; i++) {
        if (!sl->items[i].learning) {
            n++;
        }
    }
    return n;
}

int spell_list_learning_count(const SpellList *sl)
{
    return sl->count - spell_list_learnt_count(sl);
}

void spell_list_load(SpellList *sl, const u8 *data, size_t data_size, size_t offset)
{
    spell_list_clear(sl);

    if (offset + SPELL_LIST_RECORD_SIZE > data_size) {
        log_warn("spell list at %zu runs past the %zu byte record", offset, data_size);
        return;
    }

    for (int i = 0; i < SPELL_LIST_RECORD_SIZE; i++) {
        if (data[offset + (size_t)i] > 0) {
            spell_list_add_learnt(sl, data[offset + (size_t)i]);
        }
    }
}

void spell_list_save(const SpellList *sl, u8 *data, size_t data_size, size_t offset)
{
    int idx;

    if (offset + SPELL_LIST_RECORD_SIZE > data_size) {
        log_warn("spell list at %zu runs past the %zu byte record", offset, data_size);
        return;
    }

    memset(data + offset, 0, SPELL_LIST_RECORD_SIZE);

    /* Written from the last slot backwards, as the original did. */
    idx = SPELL_LIST_RECORD_SIZE - 1;
    for (int i = 0; i < sl->count && idx >= 0; i++) {
        if (!sl->items[i].learning) {
            data[offset + (size_t)idx] = sl->items[i].id;
            idx--;
        }
    }
}
