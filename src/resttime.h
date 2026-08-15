/* resttime.h - the seven-slot game clock.
 * Ported from Classes/RestTime.cs (unk_1D890).
 *
 * Used both as a length of time to rest for and as a running clock. Each slot
 * rolls over into the next at the scale in REST_TIME_SCALES: ten ticks to the
 * minute, then the minutes split into ones and tens, then hours, days, months and
 * years. Splitting the minutes in two is how the original stored it, and the camp
 * screen prints minutes as tens * 10 + ones.
 */
#ifndef COAB_RESTTIME_H
#define COAB_RESTTIME_H

#include "coab.h"

#define REST_TIME_SLOTS 7

typedef enum {
    REST_SLOT_TICKS        = 0,   /* field_0, tenths of a minute */
    REST_SLOT_MINUTES_ONES = 1,   /* field_2 */
    REST_SLOT_MINUTES_TENS = 2,   /* field_4 */
    REST_SLOT_HOURS        = 3,   /* field_6 */
    REST_SLOT_DAYS         = 4,   /* field_8 */
    REST_SLOT_MONTHS       = 5,   /* field_A */
    REST_SLOT_YEARS        = 6    /* field_C */
} RestSlot;

/* word_1A13C. The last entry is the year slot's ceiling: the clock code treats
 * reaching it as a birthday rather than carrying into an eighth slot. */
extern const int REST_TIME_SCALES[REST_TIME_SLOTS];

typedef struct {
    int slot[REST_TIME_SLOTS];
} RestTime;

void rest_time_clear(RestTime *t);

/* The C# indexer, which threw for anything outside 0..6. These log and answer 0
 * or ignore the write, because the clock is stepped with indices worked out from
 * ECL arguments. */
int  rest_time_get(const RestTime *t, int slot);
void rest_time_set(RestTime *t, int slot, int value);
void rest_time_add(RestTime *t, int slot, int amount);

#endif /* COAB_RESTTIME_H */
