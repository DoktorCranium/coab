/* rnd.h - the engine's random numbers. Ported from engine/seg051.cs.
 *
 * The DOS game had Turbo Pascal's generator and the C# port swapped in
 * System.Random; neither sequence can be reproduced from the other, so this is
 * a third generator again - xorshift64*, which is small, has no state to lose
 * and gives the same stream on every platform this port builds on.
 *
 * rnd_int() keeps seg051.Random's one quirk: a limit of zero returns zero
 * rather than dividing by it.
 */
#ifndef COAB_RND_H
#define COAB_RND_H

#include "coab.h"

/* seg051.Randomize - seeds from the clock. */
void rnd_randomize(void);

/* Seeds explicitly, so a run can be replayed. Zero is remapped, since
 * xorshift cannot be seeded with it. */
void rnd_seed(u32 seed);

/* seg051.Random(int) / Random(byte) - 0 .. limit-1, and 0 for limit <= 0. */
int rnd_int(int limit);

/* seg051.Random__Real - 0.0 up to but not including 1.0. */
double rnd_real(void);

#endif /* COAB_RND_H */
