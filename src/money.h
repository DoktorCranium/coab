/* money.h - a party member's or a hoard's coin, gem and jewelry holdings.
 * Ported from Classes/MoneySet.cs.
 *
 * Held as ints but serialized as seven signed 16-bit words, which is the DOS
 * layout and therefore the cap on what a save game can carry.
 */
#ifndef COAB_MONEY_H
#define COAB_MONEY_H

#include "coab.h"

typedef enum {
    MONEY_COPPER   = 0,
    MONEY_SILVER   = 1,
    MONEY_ELECTRUM = 2,
    MONEY_GOLD     = 3,
    MONEY_PLATINUM = 4,
    MONEY_GEMS     = 5,
    MONEY_JEWELRY  = 6,
    MONEY_KINDS    = 7,
    MONEY_COINS    = 5    /* copper..platinum are coins; gems and jewelry are not */
} MoneyKind;

/* 14 bytes: seven i16, in MoneyKind order. */
#define MONEY_RECORD_SIZE 14

extern const char *const money_names[MONEY_KINDS];

/* Value of one coin in coppers. Only the five coin kinds have one. */
extern const int money_per_copper[MONEY_COINS];

typedef struct {
    int amount[MONEY_KINDS];
} MoneySet;

void money_clear_all(MoneySet *m);      /* ClearAll */
void money_clear_coins(MoneySet *m);    /* ClearCoins: leaves gems and jewelry */

void money_add(MoneySet *dst, const MoneySet *a, const MoneySet *b);

int  money_get(const MoneySet *m, MoneyKind kind);
void money_set(MoneySet *m, MoneyKind kind, int count);
void money_add_coins(MoneySet *m, MoneyKind kind, int count);

int  money_gold_worth(const MoneySet *m);
int  money_exp_worth(const MoneySet *m);   /* gold worth plus gems and jewelry */

bool money_any(const MoneySet *m);

/* Spends the gold-piece equivalent, paying in the smallest coin first and
 * making change downwards from platinum if the payment overshoots. */
void money_subtract_gold_worth(MoneySet *m, int gold);

/* Multiplies the coin holdings, leaving gems and jewelry alone. Returns whether
 * there was anything to scale. */
bool money_scale_all(MoneySet *m, double scale);

/* DIO_CUSTOM handlers, for the MoneySet embedded in Player at 0xfb. */
void money_dio_read(void *member, const u8 *data, size_t offset);
void money_dio_write(const void *member, u8 *data, size_t offset);

#endif /* COAB_MONEY_H */
