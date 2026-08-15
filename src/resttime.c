/* resttime.c - Ported from Classes/RestTime.cs. */
#include <string.h>

#include "resttime.h"

#include "log.h"

const int REST_TIME_SCALES[REST_TIME_SLOTS] = { 10, 10, 6, 24, 30, 12, 0x100 };

void rest_time_clear(RestTime *t)
{
    memset(t, 0, sizeof(*t));
}

static bool slot_ok(int slot)
{
    if (slot >= 0 && slot < REST_TIME_SLOTS) {
        return true;
    }
    log_warn("clock: slot %d does not exist (0..%d)", slot, REST_TIME_SLOTS - 1);

    return false;
}

int rest_time_get(const RestTime *t, int slot)
{
    return slot_ok(slot) ? t->slot[slot] : 0;
}

void rest_time_set(RestTime *t, int slot, int value)
{
    if (slot_ok(slot)) {
        t->slot[slot] = value;
    }
}

void rest_time_add(RestTime *t, int slot, int amount)
{
    if (slot_ok(slot)) {
        t->slot[slot] += amount;
    }
}
