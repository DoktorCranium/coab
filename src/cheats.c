#include "cheats.h"

#include <string.h>

Cheats cheats;

void cheats_init(void)
{
    memset(&cheats, 0, sizeof(cheats));

    /* The two the C# initialises to true; everything else starts off. */
    cheats.allow_player_modify  = true;
    cheats.skip_copy_protection = true;
}
