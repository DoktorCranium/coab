/* savegame.h - characters and games on disk.
 * Ported from engine/ovr017.cs, the half of that overlay that reads and writes
 * files. The other half - the combat icon, AssignPlayerIconId, SilentTrainPlayer
 * and the Pool of Radiance record conversion - was needed earlier and lives in
 * partymenu.h and import.h.
 *
 * Everything here goes through fileio.h, which is seg051 and the file end of
 * seg042: fixed-size blocks written in order, no seeking. A saved game is one
 * `savgam<A..J>.dat` holding the world, plus one `CHRDAT<letter><1..8>.sav` per
 * character; a character kept between games is `<name>.guy` with `.swg` for the
 * pack and `.fx` for the affects beside it.
 *
 * The save directory is vfs_save_dir(), which is Config.GetSavePath().
 *
 * One shape change runs through all of it. The C# passed characters around as
 * full paths out of Directory.GetFiles and let Path.Combine collapse the double
 * root; here the file lists carry bare basenames and every routine joins them to
 * the save directory itself. A MenuItem holds 64 characters, which a real save
 * path would overflow, and a basename is what the one caller that builds a name
 * rather than picking one - loadSaveGame, reading "<name>.sav" - passed anyway.
 */
#ifndef COAB_SAVEGAME_H
#define COAB_SAVEGAME_H

#include "coab.h"
#include "import.h"
#include "menu.h"
#include "player.h"

/* ovr017.BuildLoadablePlayersLists, sub_47465. Fills `paths` with the file name
 * of every character in the save directory that could be added to the party, and
 * `names` with what to show for each. Which files are looked at comes from
 * gbl.import_from: `*.guy` for this game, `*.cha` and `*.sav` for Pool of
 * Radiance, `*.hil` for Hillsfar.
 *
 * A file is offered only if its length matches the record it claims to be, the
 * name in it is not already in the party, and - except for Hillsfar, whose
 * records have no such byte - it is not an NPC. Both lists come back with the
 * same number of entries, so an index into one indexes the other. */
void savegame_build_loadable_players_lists(MenuList *paths, MenuList *names);

/* ovr017.PlayerFileExists, sub_483AE. Whether any `*<file_ext>` in the save
 * directory begins with this name. Only the name at offset 0 is looked at, which
 * is why this answers for `.guy` and `.cha` - both hold it there - and would not
 * for `.hil`, which keeps it at 4. */
bool savegame_player_file_exists(const char *file_ext, const char *player_name);

/* ovr017.remove_player_file. Deletes the character's `.guy`, `.swg` and `.fx`.
 * A character in a saved game is stored in that game's own files, so this is
 * what stops them being loadable as a loose character as well. */
void savegame_remove_player_file(Player *player);

/* ovr017.SavePlayer, sub_47DFC. Writes the character out, followed by their pack
 * and affects if they have any - and deletes the `.swg` and `.fx` first, so a
 * character who has dropped everything does not keep an old pack.
 *
 * `prefix` empty means a loose character: the file is `<cleaned name>.guy`, and
 * an existing file of that name is asked about ("Overwrite <name>? ") with a new
 * name read if the answer is no. Any other `prefix` is a saved game's own
 * `CHRDAT<letter><n>`, written to `.sav` with no questions asked. */
void savegame_save_player(const char *prefix, Player *player);

/* ovr017.import_char01. Reads `file_name` out of the save directory into
 * `player`, converting from whichever game gbl.import_from names, then adds the
 * `.swg` pack and the `.fx` affects that go with it - and for Pool of Radiance
 * the seven racial affects out of `.spc`. Finishes by recalculating the
 * character's values and class bonuses, as the original did. */
void savegame_import_char(Player *player, const char *file_name);

/* ovr017.TransferHillsFarCharacter, sub_48F35. Lays a Hillsfar record over a
 * character who already exists in this game or in Pool of Radiance: every stat,
 * the experience, the money and the age move across only if Hillsfar's is
 * higher, and the class levels are reset to one of each class the record claims.
 * `previous_selected` is put back in gbl.selected_player on the way out, the
 * training in the middle needing the character selected. */
void savegame_transfer_hills_far_character(const HillsFarPlayer *hf_player,
                                           Player *player,
                                           Player *previous_selected);

/* ovr017.load_mob, and the overload that answers NULL instead of stopping the
 * game when the monster is not there. Reads monster `monster_id` out of
 * MON<area>CHA.DAX, with its affects from MON<area>SPC.DAX and its pack from
 * MON<area>ITM.DAX. The record comes out of the roster pool, so a full pool
 * answers NULL however `exit` is set. */
Player *savegame_load_mob(int monster_id);
Player *savegame_load_mob_opt(int monster_id, bool exit_on_failure);

/* ovr017.load_npc, sub_4A57D. Loads the monster as a character, gives them a
 * combat icon slot and their portrait, and puts them in the party - but only
 * while there are seven or fewer in it. */
void savegame_load_npc(int monster_id);

/* ovr017.loadGameMenu. Offers whichever of savgamA..savgamJ exist and loads the
 * one picked. Does nothing at all when none of them do. */
void savegame_load_game_menu(void);

/* ovr017.loadSaveGame. Reads one `savgam<letter>.dat` and everything it points
 * at: the world blocks, the script, where the party is standing, and each
 * character out of their own `.sav`. Leaves gbl.game_state at
 * GAME_STATE_START_GAME_MENU with the previous state behind it, which is how the
 * loaded game gets picked up. */
void savegame_load_save_game(const char *file_name);

/* ovr017.SaveGame. Asks which of the ten slots to use, writes the world to
 * `savgam<letter>.dat` and every character to their own `CHRDAT<letter><n>.sav`,
 * and sets gbl.game_saved. Escape at the prompt does nothing. */
void savegame_save_game(void);

#endif /* COAB_SAVEGAME_H */
