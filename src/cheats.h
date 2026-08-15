/* cheats.h - the debug/convenience switches from Classes/Cheats.cs.
 * All default off except allow_player_modify and skip_copy_protection, which
 * the C# initialises to true.
 */
#ifndef COAB_CHEATS_H
#define COAB_CHEATS_H

#include "coab.h"

typedef struct {
    bool allow_player_modify;
    bool always_show_areamap;
    bool allow_gods_intervene;
    bool allow_keyboard_exit;
    bool display_full_item_names;
    bool free_training;
    bool improved_area_map;
    bool no_race_level_limits;
    bool no_race_class_restrictions;
    bool player_always_saves;
    bool skip_title_screen;
    bool skip_copy_protection;
    bool view_item_stats;
    bool sort_treasure;
} Cheats;

extern Cheats cheats;

void cheats_init(void);

#endif /* COAB_CHEATS_H */
