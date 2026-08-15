/* icons.h - combat sprites and the isometric tile bank.
 * Ported from engine/ovr034.cs.
 *
 * A fight is drawn from two sources: the 24x24 isometric tiles that make up the
 * ground, held in one big picture of 0x30 cells (gbl.dax_24x24_set), and the
 * 26 CombatIcon slots holding the party's and the monsters' sprites.
 */
#ifndef COAB_ICONS_H
#define COAB_ICONS_H

#include "coab.h"
#include "combat.h"

/* ovr034.Load24x24Set - copies cell_count cells of block_id from
 * <file_name>.dax into the tile bank starting at dest_cell_offset.
 *
 * An offset past 0x30 stopped the game outright in the original
 * (Logger.LogAndExit), so it does here too: it means the caller has miscounted
 * and everything drawn afterwards would be wrong. */
void icons_load_24x24_set(int cell_count, int dest_cell_offset, int block_id,
                          const char *file_name);

/* ovr034.DrawIsoTile (sub_760F7) */
void icons_draw_iso_tile(int tile_index, int row_y, int col_x);

/* ovr034.ReleaseCombatIcon - free_icon */
void icons_release_combat_icon(int index);

/* ovr034.chead_cbody_comspr_icon - loads a sprite pair into slot
 * combat_icon_index. Which of the three cases applies depends on the file name:
 *
 *   starts CHEAD or CBODY  a head or body set; a name whose last letter is T is
 *                          the second bank, so its block ids are 0x40 higher,
 *                          and the last letter is dropped from the file name
 *   COMSPR or ICON         loaded as they are; ICON is also recoloured
 *   anything else          the current chapter number is appended to the name
 *
 * The attack picture is always the normal one's block id plus 0x80. */
void icons_chead_cbody_comspr_icon(u8 combat_icon_index, int block_id,
                                   const char *file_text);

/* ovr034.draw_combat_icon (sub_76504) - tile coordinates, so three 8-pixel
 * cells per tile, offset one cell into the combat view. */
void icons_draw_combat_icon(int icon_index, CombatIconState icon_state,
                            int direction, int tile_y, int tile_x);

#endif /* COAB_ICONS_H */
