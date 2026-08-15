/* resting.h - the game clock, and what happens while the party sleeps.
 * Ported from engine/ovr021.cs.
 *
 * resttime.c holds the seven-slot clock itself; this is the code that winds it
 * on. Two clocks are involved and they are easy to confuse:
 *
 *   the world clock  - seven words inside Area1, saved with the game. Only
 *                      resting_step_game_time touches it, and every caller that
 *                      spends time does so through that: a step of the party, a
 *                      round of combat, an ECL script's "time passes".
 *   gbl.time_to_rest - how much longer the party means to rest. The camp screen
 *                      fills it in, the rest menu here edits it, and the resting
 *                      loop counts it down five minutes at a time.
 *
 * Time only really matters to the party for two reasons, and both are here:
 * affects run out (which is why the clock walks everybody's affect list), and
 * resting is when wounds close, spells are memorised and scrolls are copied into
 * a spellbook.
 */
#ifndef COAB_RESTING_H
#define COAB_RESTING_H

#include "coab.h"

/* sub_583FA. Winds the world clock on by `amount` of slot `time_slot` - slot 1
 * is a minute, 3 an hour - carrying between the slots, and ages the party when
 * the year rolls over. Then ticks that much time off everybody's affects.
 *
 * The slot numbering is RestSlot's. An out-of-range slot is logged and the clock
 * is left alone, where the C# would have thrown. */
void resting_step_game_time(int time_slot, int amount);

/* sub_5849F. Takes `amount` of slot `time_slot` off gbl.time_to_rest, borrowing
 * from the larger slots to do it, and stopping at nothing left to rest for
 * rather than going negative. Does nothing at all when there was no time left to
 * begin with. */
void resting_subtract_rest_time(int time_slot, int amount);

/* reseting. Rests until gbl.time_to_rest has run out, or until the player stops
 * it, or until something wanders in. With interactive_resting the rest menu is
 * shown first and the clock is drawn as it counts down; without it the party
 * simply sleeps for however long it was told to.
 *
 * True when the rest was interrupted by an encounter, which is what sends the
 * caller into a fight. */
bool resting_run(bool interactive_resting);

#endif /* COAB_RESTING_H */
