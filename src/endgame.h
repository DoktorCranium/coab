/* endgame.h - the closing scene: Tyranthraxus destroyed, and the fireworks over
 * Shadowdale that the game finishes on.
 * Ported from engine/ovr019.cs.
 *
 * endgame_text() is the whole overlay from the outside - engine/ovr003.cs calls
 * it and nothing else - but the pieces the C# marked internal are declared here
 * too, in the order ovr019 has them, so the self-test can drive a rocket without
 * waiting for the endless display to be interrupted.
 *
 * Eleven of Gbl.cs's fields belong to this overlay and to nothing else - the
 * spark array, the rocket colours, the flight in progress - so they are file
 * statics in endgame.c rather than more of gbl.
 */
#ifndef COAB_ENDGAME_H
#define COAB_ENDGAME_H

#include "coab.h"
#include "firework.h"

/* ovr019.GetPixel. Row first, column second, which is the opposite of
 * display_get_pixel and is how every caller in this overlay reads. */
u8 endgame_get_pixel(int row, int col);

/* sub_52068. Blinks palette slot 15 to colour 9 and back, so a rocket going off
 * lights up everything drawn in white. */
void endgame_flash(void);

/* sub_520B8. Scatters the three rockets' worth of sparks from (row, col), each
 * rocket around its own point on a sphere of the burst's own making. rocket_size
 * is three bytes, one per rocket: 1 for a rocket that did not go off, and 2..6
 * for one that did, which is also the colour it fades through.
 *
 * dy and dx are the rocket's speed at the top of its climb, in 1/32nds of a
 * pixel, and the sparks inherit it. */
void endgame_burst(const u8 *rocket_size, int dy, int dx, int row, int col);

/* sub_524F7. One tick of the burst: move every spark, put back the pixel it was
 * covering, and draw it in whatever colour its stage has reached. `tick` counts
 * from 1 and is what the stage times are measured against.
 *
 * Private to ovr019; public here so that a single frame of the display can be
 * looked at. */
void endgame_burst_step(int tick);

/* sub_5279B. The burst from the scatter to the last spark going out, then the
 * background put back everywhere. The C# also took the rocket sizes, which it
 * never looked at. */
void endgame_burst_run(void);

/* sub_5285E. Flies a rocket `steps` steps from (*row, *col), leaving the arrival
 * point and speed behind in the arguments. With draw the flight is drawn - a
 * flash, then a trail of sparks that puts the background back as it goes - and
 * without it nothing is touched, which is how the endgame finds out where the
 * rocket will burst before it launches it.
 *
 * dy is the climb, negative and pulled back towards the ground a step at a time;
 * dx is constant. */
void endgame_rocket_flight(bool draw, int steps, i16 *dy, u16 dx,
                           u16 *row, u16 *col);

/* sub_529F4. Sends up rockets over the Shadowdale picture until a key is
 * pressed. One iteration in ten thousand launches one, so there is a wait
 * between them; a key pressed during a flight is noticed when it ends. */
void endgame_fireworks(void);

/* sub_52B79. Loads block_id of PIC<area>.DAX as an animation and cycles it
 * num_loops times over the picture already on screen. */
void endgame_show_animation(int num_loops, u8 block_id, int row_y, int col_x);

/* The end of the story: the fight over the Pool of Radiance, the bonds lifting,
 * the feast at Shadowdale, and the fireworks. Returns when the player interrupts
 * them, leaving the game state as it found it. */
void endgame_text(void);

/* The 120 sparks. For the self-test, which watches where a burst throws them;
 * nothing else has any business reading them. */
const FireworkSpark *endgame_sparks(void);

#endif /* COAB_ENDGAME_H */
