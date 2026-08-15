/* vm.h - the ECL script interpreter's machine.
 * Ported from engine/ovr008.cs.
 *
 * ECL is the game's own bytecode, and every map, encounter and conversation in
 * the game is a script written in it. ovr003 is the instruction set; this is
 * everything underneath it - how an instruction's operands are unpacked, how the
 * script reads and writes the game's state, how its text is packed, and the
 * handful of screen and party routines the instructions share.
 *
 * The machine has four address spaces, and which one an address belongs to is
 * decided by its value alone (vm_get_memory_value_type):
 *
 *     0x4B00..0x4EFF   the area record        (Area1, word addressed)
 *     0x7A00..0x7BFF   the script's scratch   (EclVars, word addressed)
 *     0x7C00..0x7FFF   the party record       (Area2, plus the selected
 *                                              character's own sheet)
 *     0x8000..0x9DFF   the script itself      (EclBlock, byte addressed)
 *     anything else    named cells in the DOS data segment
 *
 * Every one of those translations is 16-bit segment arithmetic that overflows on
 * purpose - 0x4B00 * 2 + 0x6A00 is 0x10000, i.e. 0 - so the offsets are masked
 * before they are used and then range checked. ecl.h does both; see the note
 * there. Reading an address the port does not model answers 0 and writing one is
 * dropped, in both cases with a warning, where the C# would have thrown.
 *
 * The interpreter's state lives in gbl (ecl_offset, cmd_opps, ecl_strings,
 * compare_flags, the call stack) rather than in locals, because ovr003 runs the
 * VM re-entrantly - camping runs a script from inside a script - and the
 * original let the inner run clobber the outer one's operands. Keeping the state
 * global keeps that behaviour.
 */
#ifndef COAB_VM_H
#define COAB_VM_H

#include "coab.h"
#include "gbl.h"
#include "menu.h"
#include "player.h"

/* ------------------------------------------------------- loading a script */

/* ovr008.load_ecl_dax. Reads block block_id of ECL<area>.dax into gbl.ecl_ptr,
 * dropping the two byte header.
 *
 * The C# retried forever on a short read, which on a DOS box was the "insert the
 * other floppy" loop. There are no floppies here, so a failure is permanent and
 * is reported as one. */
void vm_load_ecl_dax(u8 block_id);

/* ovr008.vm_init_ecl, sub_301E8. Resets the interpreter onto a freshly loaded
 * script and reads the five entry points out of its header. Unless
 * gbl.reload_ecl_and_pictures says the game is picking a script back up - a
 * saved game, or returning from a fight - the area records are reset too. */
void vm_init_ecl(void);

/* --------------------------------------------------- decoding instructions */

/* ovr008.vm_LoadCmdSets, parse_command_sub. Unpacks number_of_sets operands of
 * the instruction at gbl.ecl_offset into gbl.cmd_opps[1..number_of_sets], and
 * leaves gbl.ecl_offset on the byte after the last of them. Called with 0 it
 * just steps past the opcode.
 *
 * An operand's code says what follows it: 0 an immediate byte, 2 an immediate
 * word, 1 and 3 a word to be read as an address, 0x80 a packed string inline in
 * the script, and 0x81 a word naming a string in memory. The two string forms
 * land in gbl.ecl_strings, numbered from 1 in the order they appear, which is
 * not the operand's own number. */
void vm_load_cmd_sets(int number_of_sets);

/* ovr008.vm_GetCmdValue, sub_30168. Operand `index`'s value, following the
 * address forms through to what they point at. */
u16 vm_get_cmd_value(int index);

/* -------------------------------------------------------- machine memory */

/* ovr008.vm_GetMemoryValueType, sub_30723. Which of the five address spaces
 * above `loc` names: 0 area, 1 party, 2 scratch, 3 script, 4 named cell. */
int vm_get_memory_value_type(u16 loc);

/* ovr008.vm_GetMemoryValue, sub_30F16, and vm_SetMemoryValue, cmd_table01.
 * One word of the machine's memory. Writing to the party record also runs the
 * write through vm_alter_character, since some of those cells are the selected
 * character's own and changing one has to change the character too. */
u16  vm_get_memory_value(u16 loc);
void vm_set_memory_value(u16 value, u16 location);

/* ovr008.vm_WriteStringToMemory, sub_3105D, and vm_CopyStringFromMemory,
 * sub_31421. A NUL terminated string, one character per addressable cell - which
 * for the word addressed spaces means one character every two bytes, as the
 * original stored it. The copy lands in gbl.ecl_strings[str_index]. */
void vm_write_string_to_memory(const char *text, u16 loc);
void vm_copy_string_from_memory(u16 location, int str_index);

/* ---------------------------------------------------------- the text codec */

/* ovr008.inflateChar / deflateChar. ECL text is packed six bits to a character,
 * which covers 0x20..0x3f directly and 0x40..0x5f by remapping - so a script can
 * hold spaces, digits, punctuation and capitals, and nothing else. That is why
 * the menus are lower-cased on the way to the screen (vm_build_menu_strings). */
char     vm_inflate_char(unsigned bits);
unsigned vm_deflate_char(char ch);

/* ovr008.compressString / DecompressString. Four characters to three bytes.
 * A packed zero is a gap and is skipped rather than ending the string, which is
 * how the original padded the last group out.
 *
 * vm_compress_string returns how many bytes it wrote. Nothing in the game packs
 * ECL text - only the tools that built it did - but the round trip is what
 * proves the unpacking, so both halves are here as ovr008 had them. */
size_t vm_compress_string(u8 *out, size_t out_size, const char *input);
void   vm_decompress_string(char *out, size_t out_size,
                            const u8 *data, size_t length);

/* ovr008.LoadCompressedEclString. Unpacks input_length bytes of packed text
 * following the current operand into gbl.ecl_strings[str_index], and steps
 * gbl.ecl_offset over them. */
void vm_load_compressed_ecl_string(int str_index, int input_length);

/* ------------------------------------------------------------ comparisons */

/* ovr008.compare_strings, sub_3193B, and compare_variables, sub_31A11. Fill in
 * all six of gbl.compare_flags from one comparison; the conditional jumps then
 * each read the one flag they care about. Note the argument order: the flags
 * describe b against a, not a against b. */
void vm_compare_strings(const char *string_a, const char *string_b);
void vm_compare_variables(u16 a, u16 b);

/* --------------------------------------------------- the character sheet */

/* ovr008.find_gbl_player_index. Where `player` is on the team list, or the
 * length of the list when they are not on it - which is what makes "not found"
 * distinguishable from index 0 without a sentinel. */
u16 vm_find_team_index(const Player *player);

/* ovr008.get_player_values. The part of the party address space that is the
 * selected character's own sheet rather than the party record: stats, money,
 * saving throws, thief skills, who they are and how the fight is going for them.
 * *out_found is false for an address that is not one of those, which is the
 * caller's cue to read the party record instead. */
u16 vm_get_player_values(bool *out_found, u16 loc);

/* ovr008.alter_character. The other half: a write into the party address space
 * that has to land on the selected character - or, for three of them, load a
 * wall set or change which chapter's data files are in use. */
void vm_alter_character(u16 set_value, u16 switch_var);

/* ------------------------------------------------- showing an encounter */

/* ovr008.sub_304B4. How far ahead the party can see down the corridor they are
 * facing, 0 to 2, which is how far away an encounter starts. Outdoors it is
 * always 2. */
u8 vm_encounter_distance(int map_dir, int map_y, int map_x);

/* ovr008.set_and_draw_head_body, sub_30543. Loads a body and a head and draws
 * the two composited, which is how the game shows a speaking character. */
void vm_set_and_draw_head_body(u8 body_id, u8 head_id);

/* ovr008.sub_30580. Puts the encounter on screen: its sprite standing in the
 * dungeon view at the right distance, and - once the party is on top of it - its
 * picture in the panel. flags[0] and flags[1] record which of the two has been
 * done and are updated, so walking towards a monster does not reload the art at
 * every step; they are gbl.encounter_flags at every call site. */
void vm_show_encounter_art(bool *flags, int encounter_distance,
                           u8 pic_block_id, u8 sprite_block_id);

/* ------------------------------------------------------------- the menus */

/* ovr008.buildMenuStrings. Turns a script's menu text into what is drawn and the
 * keys that pick from it: "~COMBAT ~WAIT" becomes "Combat Wait" with the keys
 * "CW". A '~' marks the next character as a word's initial - it stays capital
 * and becomes that word's key - and everything else that could be a key is
 * lower-cased, since ECL text can only hold capitals.
 *
 * "Everything else that could be a key" is the original's set, and it includes
 * the digits, which come out as 'P' to 'Y' because lower-casing is done by adding
 * 0x20 to the character. No shipped menu has a digit in it outside a '~', so
 * nothing in the game shows it; the set is transcribed as it was rather than
 * quietly narrowed to the letters.
 *
 * menu_text is rewritten in place - the text only ever gets shorter - and
 * out_keys receives the keys, in menu order. */
void vm_build_menu_strings(char *menu_text, char *out_keys, size_t keys_size);

/* ovr008.sub_317AA. Runs a menu on the prompt line and answers which word was
 * picked, counting from 0, or -1 for a key that is in none of them. Return
 * answers 0 when accept_return says it may. The cursor keys scroll the party
 * selection instead of choosing, as they do everywhere else. */
int vm_menu_select(bool use_overlay, bool accept_return, MenuColorSet colors,
                   const char *display_string, const char *extra_string);

/* ovr008.VertMenuSelect. A scrolling list in a box, answering the entry the
 * highlight was left on. */
int vm_vert_menu_select(int index, bool menu_redraw, bool show_exit,
                        MenuList *list, int end_y, int end_x,
                        int start_y, int start_x);

/* ---------------------------------------------------------- party and map */

/* ovr008.MovePositionForward, sub_31B01. Steps the party one square the way it
 * is facing, wrapping at the edge of the 16x16 map, and re-reads the wall and
 * roof it is now looking at. */
void vm_move_position_forward(void);

/* ovr008.calc_group_movement, calc_group_inituative. The slowest and fastest
 * initiative on the team list, haste and slow counted in. */
void vm_calc_group_movement(u8 *out_min, u8 *out_max);

/* ovr008.SetupDuel. Sets up the arena fight: everyone but the selected character
 * sits it out, and when is_duel a copy of that character joins the enemy side as
 * "ROLF", carrying copies of their items and nothing else. */
void vm_setup_duel(bool is_duel);

/* ovr008.RobMoney, sub_31DEF, and RobItems, sub_31F1C. What a thief in a script
 * takes. rob_chance is a percentage per item, and it drops as heavy items are
 * checked - the chance is shared across the pack, not rolled fresh for each
 * item, which is the original's captured-variable behaviour. */
void vm_rob_money(Player *player, double scale);
void vm_rob_items(Player *player, int rob_chance);

/* ovr008.sub_32200. Hurts one character and says so, pausing when the text area
 * fills up. A character who is already dead is left alone. */
void vm_damage_and_report(Player *player, int damage);

#endif /* COAB_VM_H */
