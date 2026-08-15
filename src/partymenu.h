/* partymenu.h - the party menu: rolling characters up, changing them, training
 * them and throwing them out.
 * Ported from engine/ovr018.cs.
 *
 * partymenu_start_game_menu is the screen the game opens on and the one an ECL
 * script returns to whenever the party is between adventures. It is a plain menu
 * loop over twelve entries, each of which appears only when it applies:
 *
 *   Create New Character          partymenu_create_player
 *   Drop Character                partymenu_drop_player
 *   Modify Character              partymenu_modify_player
 *   Train Character               partymenu_train_player
 *   Human Change Classes          classcalc_duel_class
 *   View Character                viewplayer_view_player
 *   Add Character to Party        partymenu_add_player
 *   Remove Character from Party   saves the character, then frees them
 *   Load Saved Game               ovr017, not translated yet
 *   Save Current Game             ovr017, not translated yet
 *   BEGIN Adventuring             returns, and the caller starts the map
 *   Exit to DOS                   asks twice and quits
 *
 * Which of the twelve are offered is decided by one file-static array of flags
 * that survives between calls, exactly as the C#'s static bool[] did: whether a
 * character is selected, whether the trainer here teaches anything, and whether
 * the selected character could take a second class. The flags are not derived
 * fresh each time round the loop - the menu is only rebuilt when something that
 * could change them has happened - which is why walking the selection along the
 * party with Home and End redraws the menu only when the Human Change Classes
 * line would appear or disappear.
 *
 * Rolling a character up is partymenu_create_player, and it is the longest
 * routine in the overlay: race, sex, class and alignment off four scrolling
 * lists, then an age, then six stats rolled six times over and kept at their
 * best, then the racial, sex, class and ageing limits applied to each, then hit
 * points, starting spells, three hundred platinum, and a silent train up to
 * whatever the starting experience buys. The player is offered a reroll and
 * keeps rerolling until they answer no; then a name, the icon editor, and the
 * character is written out.
 *
 * Four hit-point routines sit behind that and behind training, and they are
 * worth keeping apart:
 *
 *   partymenu_con_hp_adj      the constitution bonus, summed over the classes
 *                             that are still rolling hit dice
 *   partymenu_roll_hit_points a level's worth of dice for a class mask, best of
 *                             two rolls
 *   partymenu_min_hit_points  the fewest the character could have had, which is
 *                             what Modify will not let the total fall below
 *   partymenu_calc_max_hp     the most, which is what it will not let it exceed
 *
 * The icon editor is partymenu_icon_builder: a four-level menu - parts, then
 * head or weapon, then the colour to change, then Next/Prev over the choices -
 * drawn as the old sprite above the new one so the two can be compared. Each
 * level keeps its own backup and Exit at any level puts that level's backup
 * back.
 *
 * Three routines here belong to engine/ovr017.cs rather than to ovr018, and are
 * complete ports rather than stubs, in the arrangement partymenu.h already had
 * with ovr011: ovr018 cannot work without them and they are small enough that
 * waiting for the rest of that overlay would mean stubbing out the icon editor
 * and character creation entirely. They keep this file's prefix; when the rest of
 * ovr017 lands they are the three it does not have to write.
 *
 * What is not translated yet is the file half of ovr017 - saving a character or a
 * game, deleting a character's files, listing what can be loaded and reading a
 * character in. Each says so once in the log and then does the least it can: a
 * save is skipped, and the Add Character list comes back empty so the menu has
 * nothing to offer. That is the one hole a player would notice, and it is what
 * the next overlay fills.
 */
#ifndef COAB_PARTYMENU_H
#define COAB_PARTYMENU_H

#include "coab.h"
#include "enums.h"
#include "player.h"

/* ------------------------------------------------------------- the menu */

/* ovr018.startGameMenu. The party menu, until BEGIN Adventuring is picked or the
 * game is quit. Leaves gbl.game_state as it found it and clears the area's
 * training mask on the way out, so a trainer visited once does not go on
 * offering to train. */
void partymenu_start_game_menu(void);

/* ovr018.createPlayer. Rolls a character up and offers to save them. The new
 * character is never added to the party: it is written out and released, and
 * gbl.selected_player is put back to whoever it was. */
void partymenu_create_player(void);

/* ovr018.dropPlayer. Asks twice, deletes the character's files and frees them.
 * A character who is not in_combat "is dumped out back"; one who is "bids you
 * farewell", which is how an NPC leaves. */
void partymenu_drop_player(void);

/* ovr018.modifyPlayer. Walks the highlight over the six stats, the hit points
 * and the name with Home and End, changes the highlighted one with the left and
 * right cursor keys, and either keeps the result or puts everything back. Only a
 * character still on their starting experience can be modified, unless
 * cheats.allow_player_modify says otherwise. */
void partymenu_modify_player(void);

/* ovr018.AddPlayer. Picks a saved character out of this game's, Pool of
 * Radiance's or Hillsfar's roster and puts them in the party, subject to the
 * rules a party has to keep: six player characters, eight bodies including NPCs,
 * no paladin alongside anyone evil, no more than two rangers, and nobody evil
 * alongside a paladin. */
void partymenu_add_player(void);

/* ovr018.train_player. One level in every class the trainer here teaches and the
 * character has the experience for, for a thousand gold. A magic-user who has
 * gone up picks a new spell; the hit points, the saving throws, the thief skills
 * and the class bonuses all follow. */
void partymenu_train_player(void);

/* ------------------------------------------------- freeing a character */

/* ovr018.FreePlayer, free_player. Empties the character out: the pack, the
 * affects, and the action record a fight gave them. The Player itself is not
 * released - the C# left that to its collector, and here the party and monster
 * rosters own it.
 *
 * The action record belongs to whatever set the fight up, so this only forgets
 * it; battlesetup.c's pool is untouched and is reused next fight. */
void partymenu_free_player(Player *player);

/* ovr018.FreeCurrentPlayer, free_players. Takes the character off the team list
 * and empties them out, and hands back who should be selected instead - the one
 * before them on the list, or NULL once the list is empty. A character who was
 * not on the list is left alone and NULL comes back.
 *
 * free_icon releases their combat sprite as well; leave_party_size keeps
 * Area2.party_size where it is, which is what a monster or an NPC being turned
 * loose wants, since they were never counted in it. */
Player *partymenu_free_current_player(Player *player, bool free_icon,
                                      bool leave_party_size);

/* --------------------------------------------------------- hit points */

/* ovr018.con_bonus. The constitution bonus to one hit die. Above 16 only a
 * fighter, ranger or paladin gets more than two, and theirs is Con - 14. Reads
 * the constitution off gbl.selected_player, not off an argument, as the original
 * did - so the caller must have the character selected. */
int partymenu_con_bonus(ClassId class_id);

/* ovr018.get_con_hp_adj. The whole constitution adjustment: con_hp_adj per class
 * still rolling hit dice, the warrior classes' larger table on top of that, and
 * the lot doubled for a first-level ranger, who rolls two dice. */
int partymenu_con_hp_adj(const Player *player);

/* ovr018.sub_506BA. The least hit points the character could have: the levels
 * plus their class's level bonus, plus the constitution adjustment, divided
 * between the classes. Modify will not let the total go below this. */
int partymenu_min_hit_points(const Player *player);

/* ovr018.calc_max_hp, sub_50793. The most: the die size plus the constitution
 * bonus for every level up to the class's hit-dice ceiling, and a flat figure
 * plus a per-level increment past it.
 *
 * The per-class figures are hp_calc_table in partymenu.c. A class past its
 * ceiling overwrites the running total rather than adding to it, which is the
 * original's arithmetic and is noted where it happens. */
int partymenu_calc_max_hp(const Player *player);

/* ovr018.sub_509E0. Rolls one level's hit points for every class in class_mask -
 * the classMasks bit per class, so 0xff means all of them - taking the better of
 * two rolls. A first-level ranger rolls two dice; a class at its ceiling gets a
 * flat figure instead, and that figure replaces the total rather than adding to
 * it. */
u8 partymenu_roll_hit_points(u8 class_mask, const Player *player);

/* ----------------------------------------------------- the icon editor */

/* ovr018.icon_builder. The editor, until the player says the icon is right.
 * Works on gbl.selected_player. */
void partymenu_icon_builder(void);

/* ovr018.sub_4FB7C. The four sprites of the editor's display: the old icon ready
 * and attacking, then the new one over the top of it, in a block cleared to
 * black first. */
void partymenu_draw_icon_editor_icons(int title_y, int title_x);

/* ovr018.sub_4FC5B. Copies one combat icon slot onto another, recolouring it to
 * gbl.selected_player's six icon colours when asked. */
void partymenu_duplicate_combat_icon(bool recolour, int dest_index,
                                     int source_index);

/* ovr018.sub_4E6F2. One line of the modify screen, highlighted or not: stats 0
 * to 5 are the six stats, 6 is the hit points and 7 is the name, which draws its
 * own cursor as a '%' over whatever character it sits on. It was a nested
 * function in the original and is only used by partymenu_modify_player. */
void partymenu_draw_highlight_stat(bool highlighted, u8 edited_stat,
                                   int name_cursor_pos);

/* ------------------------------------------- ovr017, ported ahead of turn */

/* ovr017.LoadPlayerCombatIcon, sub_47A90. Loads gbl.selected_player's head and
 * body sprites at their icon size, merges the head onto the body, and recolours
 * the result to the character's own six colours when asked. */
void partymenu_load_player_combat_icon(bool recolour);

/* ovr017.AssignPlayerIconId, sub_4A60A. Puts the character on the team list,
 * selects them, and gives them the lowest combat icon slot of the eight that
 * nobody else holds. An NPC also has their class bonuses recalculated. */
void partymenu_assign_player_icon_id(Player *player);

/* ovr017.SilentTrainPlayer. Trains the character over and over, with no screen
 * and no questions, until the training code says there is no more experience to
 * spend. This is how a newly created character reaches the level their starting
 * experience buys. */
void partymenu_silent_train_player(void);

/* ------------------------------------------------------------- tables */

/* ovr018.exp_table, unk_1A5A3. Experience needed for each level of each single
 * class, indexed [SkillType][level]; -1 means the class stops there. Level is
 * the level being trained *from*, so row [c][n] is what n + 1 costs. */
#define PARTYMENU_EXP_LEVELS 13

extern const i32 partymenu_exp_table[SKILL_COUNT][PARTYMENU_EXP_LEVELS];

/* ovr018.classMasks, unk_1A1BA. One bit per class, and the mask the area's
 * training_class_mask is made of. Fighter and monk share a bit, as do cleric and
 * druid - the original's table has it that way. */
extern const u8 partymenu_class_masks[SKILL_COUNT];

/* ovr018.thac0_table, unk_1A14A. THAC0 by class and level. */
extern const i8 partymenu_thac0_table[SKILL_COUNT][PARTYMENU_EXP_LEVELS];

#endif /* COAB_PARTYMENU_H */
