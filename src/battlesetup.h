/* battlesetup.h - laying out the ground a fight is fought on, and putting the
 * two sides down on it.
 * Ported from engine/ovr011.cs.
 *
 * This runs once, between the encounter being decided and the first round being
 * played. It has three jobs:
 *
 *   the floor      Thirteen dungeon squares by five are turned into the 50x25
 *                  combat map: each square's four walls are looked up in the 3D
 *                  map and the six-by-five patch of isometric tiles that draws
 *                  them is written out. Out in the wilderness there is no wall
 *                  to look at, so the ground is scattered instead - trees,
 *                  bushes, rocks and a road - from the city the party is near.
 *
 *   the combatants Each side is placed in rows spreading outwards from where it
 *                  stands, right of centre then left of centre, skipping squares
 *                  that are occupied or impassable. A side that will not fit
 *                  where it stands is walked round to a neighbouring square
 *                  first; an NPC there is no room at all for is turned loose.
 *
 *   the round      Everything a round needs cleared: the clouds, the fallen, the
 *                  round counter, and one Action per combatant.
 *
 * Almost all of it reads gbl rather than taking arguments, as the original did.
 * gbl.byte_1AD34 and gbl.byte_1AD35 say which dungeon square is being drawn and
 * gbl.dir_0_flags..dir_6_flags what stands around it; gbl.current_team,
 * gbl.team_start_x/y and gbl.team_direction say which side is being placed and
 * where from.
 *
 * Everything ovr011 calls is translated: engine/ovr006.cs's after-combat code is
 * aftercombat.c, and engine/ovr018.cs's FreeCurrentPlayer is in partymenu.c.
 */
#ifndef COAB_BATTLESETUP_H
#define COAB_BATTLESETUP_H

#include "coab.h"
#include "player.h"

/* ------------------------------------------------------------- the floor */

/* ovr011.set_background_tile, sub_37046. Writes one tile of the patch the
 * dungeon square gbl.byte_1AD34 / gbl.byte_1AD35 draws as, at x,y within that
 * patch. Squares off the combat map are dropped.
 *
 * The tile written is tile_id + 1: every caller names the tile one below the one
 * it wants, which is the original's arrangement and not a rounding error - the
 * tiles were loaded starting at bank cell 1. */
void battlesetup_set_background_tile(int tile_id, int y, int x);

/* ovr011.sub_37306. What stands on side `dir` of dungeon square map_x, map_y:
 * 0 open ground, 1 a wall, 3 a door. A square outside the 16x16 dungeon map is a
 * wall, except that the two squares east and west of the party's own row are
 * open - which is what lets a fight started at the edge of the map still have
 * somewhere to put the other side. */
int battlesetup_square_side_flags(int dir, int map_y, int map_x);

/* ovr011.get_dir_flags, sub_37388. The same, from both sides: this square's wall
 * in `dir` or'd with the neighbour's wall facing back. A door on either side
 * makes 3, so a doorway is never drawn as solid from one side and open from the
 * other. */
int battlesetup_dir_flags(int dir, int map_y, int map_x);

/* ovr011.build_background_tiles_1, sub_373FC. The floor of the square, and the
 * wall on its west side drawn across it. */
void battlesetup_build_tiles_1(void);

/* ovr011.build_background_tiles_2, sub_374A1. The two squares at the top of the
 * patch: the north wall, or bare floor when there is none. */
void battlesetup_build_tiles_2(void);

/* ovr011.build_backgrould_tiles_3, sub_3751E - the C#'s own spelling of it. The
 * north-west corner: which of two dozen corner pieces joins the north wall to
 * the west one, decided by what the two squares beyond the corner have. */
void battlesetup_build_tiles_3(int map_y, int map_x);

/* ovr011.build_background_tiles_4, sub_376F6. The north-east corner, the same
 * way. */
void battlesetup_build_tiles_4(int map_y, int map_x);

/* ovr011.sub_370D3. Furniture: an even chance of a table on a clear floor square
 * of a furnished room, and chairs round it. Only rooms - a square with walls on
 * two facing sides is a corridor and gets nothing. */
void battlesetup_place_furniture(void);

/* ovr011.SetupDungeonFloor, sub_378CD0. The thirteen-by-five block of dungeon
 * squares around the party, each drawn as its six-by-five patch of tiles. */
void battlesetup_dungeon_floor(void);

/* ovr011.SetupWildernessFloor01, sub_37A00. The road: a band running the height
 * of the map at a slant, laid at all only in the cities whose terrain says so. */
void battlesetup_wilderness_road(void);

/* ovr011.SetupWildernessFloor, sub_37FC8. Bare ground, then the road, then the
 * water, then the scatter of trees and rocks. */
void battlesetup_wilderness_floor(void);

/* ovr011.SetupGroundTiles, sub_38030. Loads the tile bank the fight is drawn
 * from and builds the floor, indoors or out. */
void battlesetup_ground_tiles(void);

/* ---------------------------------------------------------- the combatants */

/* ovr011.SetupCombatActions, sub_380E0. One Action per combatant, each facing
 * the way its side does, and an NPC whose morale has gone out of range given the
 * encounter's own. Everyone past Area2.party_size is marked as not one of the
 * party.
 *
 * The Actions come out of a pool this module owns; they last until the next
 * fight is set up. */
void battlesetup_combat_actions(void);

/* ovr011.place_combatant, sub_38380. Finds a square for one combatant and puts
 * them on the map. Returns false only when there was nowhere at all - not even
 * in a neighbouring square - for their side to stand. */
bool battlesetup_place_combatant(int player_index);

/* ovr011.PlaceCombatants, sub_387FE. Both sides, in team-list order, filling in
 * gbl.player_array and gbl.combat_map as it goes. A combatant not in the fight
 * is placed and then taken off the map again, leaving a body on the ground where
 * they lie; an NPC with nowhere to stand is turned loose. */
void battlesetup_place_combatants(void);

/* ------------------------------------------------------------ a battle begins */

/* ovr011.BattleSetup, battle_begins. Everything above, in order, with the round
 * counters and the clouds cleared and the screen redrawn: the whole of what
 * happens between "A battle begins..." and the first round. */
void battlesetup_battle_setup(void);

#endif /* COAB_BATTLESETUP_H */
