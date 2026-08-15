/* firework.h - a spark of the closing fireworks display.
 * Ported from Classes/Struct_1ADF6.cs (gbl.dword_1ADF6), driven by endgame.c.
 *
 * Three rockets of forty sparks each. A spark tracks where it is on screen, where
 * it is going in 1/32nds of a pixel, the pixel it covered up so the background
 * can be put back when it moves on, and how far through its five-stage fade it
 * has got.
 */
#ifndef COAB_FIREWORK_H
#define COAB_FIREWORK_H

#include "coab.h"

#define FIREWORK_ROCKETS              3
#define FIREWORK_SPARKS_PER_ROCKET    40
#define FIREWORK_SPARK_COUNT   (FIREWORK_ROCKETS * FIREWORK_SPARKS_PER_ROCKET)

/* How many colours a spark fades through, and so how many stages it has. */
#define FIREWORK_STAGES 5

typedef struct {
    u16 x;              /* field_00, screen column */
    u16 y;              /* field_02, screen row */
    i16 next_x;         /* field_04 */
    i16 next_y;         /* field_06 */
    i16 x_fixed;        /* field_08, x << 5 */
    i16 y_fixed;        /* field_0A, y << 5 */
    i16 dx;             /* field_0C */
    i16 dy;             /* field_0E */
    u8  stage;          /* field_10, 1 .. FIREWORK_STAGES */
    u8  covered_pixel;  /* field_11 */
    /* field_12 .. field_16: the tick each stage runs until. */
    u8  stage_end[FIREWORK_STAGES];
} FireworkSpark;

/* Index into the 3 x 40 grid the endgame walks: spark + rocket * 40. Returns NULL
 * for anything outside it. */
FireworkSpark *firework_spark(FireworkSpark *sparks, int rocket, int spark);

/* byteArray_11. The original kept one six-byte array here and asked it for the
 * tick stage `stage` runs until; since the stage count starts at 1 it never
 * asked for element 0, so that byte held the covered-up pixel instead. The
 * overlap is preserved: stage 0 answers with covered_pixel, as the C# did.
 * A stage outside 0 .. FIREWORK_STAGES reads as 0, where the C# threw. */
u8 firework_spark_stage_end(const FireworkSpark *s, int stage);

void firework_sparks_clear(FireworkSpark *sparks, int count);

#endif /* COAB_FIREWORK_H */
