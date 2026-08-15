/* firework.c - Ported from Classes/Struct_1ADF6.cs. */
#include <string.h>

#include "firework.h"

#include "log.h"

FireworkSpark *firework_spark(FireworkSpark *sparks, int rocket, int spark)
{
    if (rocket < 0 || rocket >= FIREWORK_ROCKETS ||
        spark < 0 || spark >= FIREWORK_SPARKS_PER_ROCKET) {
        log_warn("firework: no spark %d of rocket %d", spark, rocket);
        return NULL;
    }
    /* endgame.c addresses the flat array as [spark + rocket * 40]. */
    return &sparks[spark + rocket * FIREWORK_SPARKS_PER_ROCKET];
}

u8 firework_spark_stage_end(const FireworkSpark *s, int stage)
{
    if (stage == 0) {
        return s->covered_pixel;
    }
    if (stage < 0 || stage > FIREWORK_STAGES) {
        log_warn("firework: no stage %d, a spark has %d",
                 stage, FIREWORK_STAGES);
        return 0;
    }
    return s->stage_end[stage - 1];
}

void firework_sparks_clear(FireworkSpark *sparks, int count)
{
    memset(sparks, 0, (size_t)count * sizeof(sparks[0]));
}
