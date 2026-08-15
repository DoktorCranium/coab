/* rnd.c - Ported from engine/seg051.cs (Random, Randomize, Random__Real). */
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "rnd.h"

/* Never zero: xorshift stays stuck there. This is the state after rnd_seed(1). */
static uint64_t state = 0x9e3779b97f4a7c15u;

static uint64_t next_u64(void)
{
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545f4914f6cdd1du;
}

void rnd_seed(u32 seed)
{
    /* Mix the seed up before use: xorshift wants a well spread state, and a
     * small seed such as 1 would otherwise produce a poor first few values. */
    state = 0x9e3779b97f4a7c15u ^ ((uint64_t)seed * 0xff51afd7ed558ccdu);
    if (state == 0) {
        state = 0x9e3779b97f4a7c15u;
    }
    /* Discard a few outputs so that two nearby seeds diverge immediately. */
    for (int i = 0; i < 4; i++) {
        (void)next_u64();
    }
}

void rnd_randomize(void)
{
    /* The C# used DateTime.Now.Ticks. Seconds alone would give the same stream
     * to two runs started within a second of each other, so the pid goes in
     * too. */
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        ts.tv_sec = (time_t)time(NULL);
        ts.tv_nsec = 0;
    }
    state = 0x9e3779b97f4a7c15u ^ ((uint64_t)ts.tv_sec << 20) ^
            (uint64_t)ts.tv_nsec ^ ((uint64_t)getpid() << 44);
    if (state == 0) {
        state = 0x9e3779b97f4a7c15u;
    }
    for (int i = 0; i < 4; i++) {
        (void)next_u64();
    }
}

int rnd_int(int limit)
{
    /* seg051.Random returned 0 for a limit of 0 rather than dividing by it.
     * A negative limit could not happen there - the callers all pass a die
     * size or a count - but it would be a negative modulus here, so it is
     * treated the same way. */
    if (limit <= 0) {
        return 0;
    }

    /* The C# took Next() % limit, which is very slightly biased towards the low
     * values. Rejection sampling costs nothing here and removes that. */
    {
        uint32_t bound = (uint32_t)limit;
        uint32_t reject = (uint32_t)(0x100000000u % bound);
        uint32_t v;

        do {
            v = (uint32_t)(next_u64() >> 32);
        } while (v < reject);

        return (int)(v % bound);
    }
}

double rnd_real(void)
{
    /* 53 significant bits, the most a double holds exactly. */
    return (double)(next_u64() >> 11) * (1.0 / 9007199254740992.0);
}
