/* money.c - Ported from Classes/MoneySet.cs. */
#include <string.h>

#include "money.h"
#include "dataio.h"
#include "log.h"

const char *const money_names[MONEY_KINDS] = {
    "Copper", "Silver", "Electrum", "Gold", "Platinum", "Gems", "Jewelry"
};

const int money_per_copper[MONEY_COINS] = { 1, 10, 100, 200, 1000 };

static bool kind_valid(MoneyKind kind)
{
    if ((int)kind < 0 || (int)kind >= MONEY_KINDS) {
        log_warn("money: coin kind %d outside 0..%d", (int)kind, MONEY_KINDS - 1);
        return false;
    }
    return true;
}

void money_clear_all(MoneySet *m)
{
    memset(m->amount, 0, sizeof(m->amount));
}

void money_clear_coins(MoneySet *m)
{
    for (int k = MONEY_COPPER; k < MONEY_COINS; k++) {
        m->amount[k] = 0;
    }
}

void money_add(MoneySet *dst, const MoneySet *a, const MoneySet *b)
{
    for (int k = 0; k < MONEY_KINDS; k++) {
        dst->amount[k] = a->amount[k] + b->amount[k];
    }
}

int money_get(const MoneySet *m, MoneyKind kind)
{
    return kind_valid(kind) ? m->amount[kind] : 0;
}

void money_set(MoneySet *m, MoneyKind kind, int count)
{
    if (kind_valid(kind)) {
        m->amount[kind] = count;
    }
}

void money_add_coins(MoneySet *m, MoneyKind kind, int count)
{
    if (kind_valid(kind)) {
        m->amount[kind] += count;
    }
}

int money_gold_worth(const MoneySet *m)
{
    int coppers = 0;

    for (int k = MONEY_COPPER; k < MONEY_COINS; k++) {
        coppers += m->amount[k] * money_per_copper[k];
    }

    return coppers / money_per_copper[MONEY_GOLD];
}

int money_exp_worth(const MoneySet *m)
{
    int total = money_gold_worth(m);

    total += m->amount[MONEY_GEMS] * 250;
    total += m->amount[MONEY_JEWELRY] * 2200;

    return total;
}

bool money_any(const MoneySet *m)
{
    for (int k = 0; k < MONEY_KINDS; k++) {
        if (m->amount[k] > 0) {
            return true;
        }
    }
    return false;
}

void money_subtract_gold_worth(MoneySet *m, int gold)
{
    int coppers = gold * money_per_copper[MONEY_GOLD];
    int coin;

    /* Pay from the smallest coin up. The C# loop had no upper bound on coin, so
     * a party without enough money walked off the end of per_copper[] and threw;
     * here it simply stops once the coins are exhausted, leaving the shortfall
     * unpaid for the caller's affordability check to have already ruled out. */
    for (coin = MONEY_COPPER; coppers > 0 && coin < MONEY_COINS; coin++) {
        int sub_coins = (coppers / money_per_copper[coin]) + 1;

        if (m->amount[coin] < sub_coins) {
            sub_coins = m->amount[coin];
        }

        coppers -= money_per_copper[coin] * sub_coins;
        m->amount[coin] -= sub_coins;
    }

    /* Rounding up to a whole coin usually overshoots; give change back from the
     * largest coin down. */
    if (coppers < 0) {
        coppers = -coppers;

        for (coin = MONEY_PLATINUM; coppers > 0 && coin >= MONEY_COPPER; coin--) {
            int add_coins = coppers / money_per_copper[coin];

            coppers -= money_per_copper[coin] * add_coins;
            m->amount[coin] += add_coins;
        }
    }
}

bool money_scale_all(MoneySet *m, double scale)
{
    bool did_scale = false;

    for (int k = MONEY_COPPER; k < MONEY_COINS; k++) {
        did_scale = did_scale || (m->amount[k] > 0);
        m->amount[k] = (int)(m->amount[k] * scale);
    }

    return did_scale;
}

/* --------------------------------------------------------------- DataIO glue */

void money_dio_read(void *member, const u8 *data, size_t offset)
{
    MoneySet *m = (MoneySet *)member;

    for (int k = 0; k < MONEY_KINDS; k++) {
        m->amount[k] = sys_array_to_short(data, (int)(offset + (size_t)k * 2));
    }
}

void money_dio_write(const void *member, u8 *data, size_t offset)
{
    const MoneySet *m = (const MoneySet *)member;

    for (int k = 0; k < MONEY_KINDS; k++) {
        sys_short_to_array((i16)m->amount[k], data, offset + (size_t)k * 2);
    }
}
