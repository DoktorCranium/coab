/* dungeon.h - walking around a dungeon, and the doors in the way.
 * Ported from engine/ovr015.cs.
 *
 * This is the outer loop a player standing in a corridor is looking at: one
 * prompt with the whole of the dungeon on it, and the cursor keys that turn the
 * party and walk them forward. Everything it offers lives somewhere else -
 * casting is camp.c's, View is viewplayer.c's, the map and the view are
 * view3d.c's - so the menu itself is only a switch.
 *
 * The one thing here that is nowhere else is the door. A square the party has
 * walked into may be shut, and there are three ways through: shoulders, lockpicks
 * and a knock spell. Each of the three may be tried once per square, which is
 * what gbl.can_bash_door and its two neighbours count; walking forward sets all
 * three again.
 *
 * Nothing moves the party in the menu. Stepping forward only works out whether
 * the party is about to walk off the edge of the map; the step itself is
 * dungeon_locked_door's, once it knows the way is open. That is why a shut door
 * makes a thump and leaves the party where they were - the interpreter compares
 * the position before and after this call, and nothing changed.
 */
#ifndef COAB_DUNGEON_H
#define COAB_DUNGEON_H

#include "coab.h"

/* ovr015.main_3d_world_menu, sub_438DF. The dungeon prompt, until the party does
 * something that ends the turn: walking, encamping or looking. Returns the key
 * that ended it, which the interpreter reads to tell Encamp and Look apart from
 * a step, or '\0' when the party is not in a dungeon at all.
 *
 * Turning and toggling the overhead map are free: they redraw and ask again. */
char dungeon_main_3d_world_menu(void);

/* ovr015.locked_door. Called once per turn, after the script's movement handler
 * has had its say. It looks at the wall the party is facing and, if it is a door
 * they can get through, walks them through it. */
void dungeon_locked_door(void);

/* ovr015.MapSetDoorUnlocked, sub_43148. Marks one side of one square's door
 * open, for good: a door forced this way stays forced. Coordinates off the map
 * are ignored, as they were in the original. */
void dungeon_map_set_door_unlocked(int map_dir, int map_y, int map_x);

/* ovr015.MovePartyForward, sub_43813. One step in the direction the party faces,
 * wrapping at the edges of the 16x16 map, and the minute or two it costs. */
void dungeon_move_party_forward(void);

/* ovr015.pick_lock, sub_435B6. Everybody who can, tries their lockpicks on the
 * door the party is facing; true when one of them opened it. Public because the
 * C# marked it internal and because it is the one door routine a test can settle
 * with a single number.
 *
 * Costs the party their one pick attempt on this square whether it worked or
 * not. */
bool dungeon_pick_lock(void);

#endif /* COAB_DUNGEON_H */
