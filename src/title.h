/* title.h - the opening sequence. Ported from engine/ovr002.cs. */
#ifndef COAB_TITLE_H
#define COAB_TITLE_H

#include "coab.h"

/* ovr002.title_screen - four title stills, then the credits page. Each step
 * waits out a timeout or a keypress, so the player can skip through. */
void title_screen(void);

/* ovr002.credits */
void title_credits(void);

/* ovr002.delay_or_key */
void title_delay_or_key(int seconds);

#endif /* COAB_TITLE_H */
