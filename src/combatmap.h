/* combatmap.h - the combat map: who stands where, and drawing the 7x7 window
 * onto it. Ported from engine/ovr033.cs.
 *
 * A fight is fought on the 50x25 map, but only a 7x7 window of it is on screen
 * at a time, drawn as isometric 24x24 tiles. Three pieces of state carry that:
 *
 *   gbl.map_to_background_tile   the ground tile of every map square, plus which
 *                                square is the window's top-left corner
 *   gbl.combat_map[index]        each combatant's map position, size and the
 *                                screen position derived from the two
 *   mapToPlayerIndex             the reverse lookup, rebuilt whenever anyone
 *                                moves (combatmap_setup_player_index)
 *
 * A combatant is 1, 2 or 4 squares (a giant is 2x2), so almost everything here
 * walks the size map - the list of square offsets a combatant of that size
 * covers - rather than a single position.
 */
#ifndef COAB_COMBATMAP_H
#define COAB_COMBATMAP_H

#include "coab.h"
#include "combat.h"
#include "point.h"

/* The largest combatant covers 2x2 squares. */
#define COMBATMAP_MAX_SIZE   4
#define COMBATMAP_MAX_DELTAS 4

/* ovr033.GetSizeBasedMapDeltas, sub_7400F, and BuildSizeMap. The offsets a
 * combatant of `size` covers, and those offsets around a position. Both return
 * how many squares that is - 0 for size 0, which is what a dead or unplaced
 * combatant has. out must have room for COMBATMAP_MAX_DELTAS points. */
int combatmap_size_deltas(int size, Point *out);
int combatmap_build_size_map(int size, Point pos, Point *out);

/* ovr033.Color_0_8_inverse / Color_0_8_normal. Swaps black and dark grey, which
 * is how the combat display flashes. */
void combatmap_color_0_8_inverse(void);
void combatmap_color_0_8_normal(void);

/* ovr033.sub_7416E. Redraws one map square's ground - with the target cursor
 * over it, if one is being shown - and whatever combatant is standing there. */
void combatmap_draw_position(Point pos);

/* ovr033.RedrawPosition, sub_7431C. The ground and the combatant, without the
 * target cursor. */
void combatmap_redraw_position(Point pos);

/* ovr033.setup_mapToPlayerIndex_and_playerScreen, sub_743E7. Rebuilds the
 * square-to-combatant lookup and every combatant's screen position. Call after
 * anyone moves, is placed or is removed. */
void combatmap_setup_player_index(void);

/* ovr033.PlayerIndexAtMapXY, sub_74505. 0 when the square is empty or off the
 * map. */
int combatmap_player_index_at(int pos_y, int pos_x);

/* ovr033.AtMapXY. The ground tile and the combatant of one square; both come
 * back 0 for a square off the map or before a fight has a ground map. */
void combatmap_at_map_xy(int *ground_tile, int *player_index, Point pos);
void combatmap_at_map_yx(int *ground_tile, int *player_index, int pos_y,
                         int pos_x);

/* ovr033.RedrawPlayerBackground, sub_74572. Puts the ground tiles back over
 * where a combatant was standing. The Point form redraws the single square
 * instead when nobody is there. */
void combatmap_redraw_player_background(int player_index);
void combatmap_redraw_player_background_at(int player_index, Point map);

/* ovr033.CoordOnScreen, sub_74730. A screen coordinate, not a map one. */
bool combatmap_coord_on_screen(Point pos);

/* ovr033.PlayerOnScreen, sub_74761. With all_on_screen, every square the
 * combatant covers has to be visible; without it, any one of them will do. */
bool combatmap_player_on_screen(bool all_on_screen, int player_index);
bool combatmap_player_on_screen_p(bool all_on_screen, const Player *player);

/* ovr033.ScreenMapCheck. Scrolls the window until pos is within radius of its
 * centre and redraws the ground if it moved, returning whether it did. A radius
 * of 0xff forces the redraw. */
bool combatmap_screen_check(int radius, Point pos);

/* ovr033.redrawCombatArea, sub_749DD. Scrolls to one step in `dir` from map,
 * redraws every visible combatant, and leaves the target cursor on that square. */
void combatmap_redraw_area(int dir, int radius, Point map);

/* ovr033.draw_74B3F, sub_74B3F. Turns a combatant to face `direction` and draws
 * them in `state`; with arg_0 the icon is left off, which is how a combatant is
 * erased. */
void combatmap_draw_player(bool arg_0, CombatIconState state, int direction,
                           Player *player);

/* ovr033.PlayerMapPos / PlayerMapSize / GetPlayerIndex. GetPlayerIndex returns 0
 * for a player who is not in the fight, so the caller reads combatant 0 - which
 * is the empty one. */
Point combatmap_player_map_pos(const Player *player);
int   combatmap_player_map_size(const Player *player);
int   combatmap_player_index(const Player *player);

/* ovr033.getGroundInformation, sub_74D04. What a combatant would be stepping
 * into in `direction`: the costliest ground tile under them and any other
 * combatant in the way. ground_tile comes back 0 when any of the squares is off
 * the map, which is what stops a move.
 *
 * The second form also reports the two gas clouds, and - because it does - does
 * not let a cloud win the move-cost comparison. That difference is the
 * original's, not the port's. */
void combatmap_ground_information(int *ground_tile, int *player_index,
                                  int direction, Player *player);
void combatmap_ground_information_clouds(bool *is_poisonous_cloud,
                                         bool *is_noxious_cloud,
                                         int *ground_tile, int *player_index,
                                         int direction, Player *player);

/* ovr033.CombatantKilled, sub_74E6F. Plays the death sound, flashes the skull
 * over the body, leaves a corpse tile behind for a team member and takes the
 * combatant off the map. Outside a fight it is only the sound. */
void combatmap_combatant_killed(Player *player);

/* ovr033.sub_7515A. Puts a combatant on the map at pos and reports whether they
 * fit: an occupied or impassable square, or one off the map, leaves them with
 * size 0. With arg_0 the corpse tile they were lying on is cleaned up. */
bool combatmap_place_combatant(bool arg_0, Point pos, Player *player);

/* ovr033.RedrawCombatIfFocusOn, sub_75356. */
void combatmap_redraw_if_focus_on(bool draw_cursor, int radius, Player *player);

#endif /* COAB_COMBATMAP_H */
