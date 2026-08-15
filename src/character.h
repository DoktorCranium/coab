/* character.h - a character's derived values, and how one is shown on screen.
 * Ported from engine/ovr025.cs.
 *
 * player.c holds the record as it is saved; this holds everything worked out
 * from it - armour class, hit bonus, damage bonus, encumbrance, movement - and
 * the routines the rest of the engine calls to put a character in front of the
 * player: the party summary down the right-hand side, the combat side panel, the
 * "<name> is bandaged" status lines, and the missile and spell animations that
 * fly between two combatants.
 *
 * Twenty other overlays call into ovr025, which is why nearly everything here is
 * public. Two of its members are not:
 *
 *   ovr025.FindAffect is affect_list_find() with the found affect returned
 *   rather than passed out, so callers use that directly.
 *
 *   ovr025.direction, the eight compass abbreviations, is only used by
 *   character_display_map_position_time and stays private to it.
 */
#ifndef COAB_CHARACTER_H
#define COAB_CHARACTER_H

#include "coab.h"
#include "combat.h"
#include "item.h"
#include "player.h"
#include "point.h"

/* --------------------------------------------------- derived combat values */

/* sub_66023. Recomputes hit bonus and attack 1's damage out of the readied
 * weapon and its ammunition. Does nothing at all when nothing is readied, so
 * character_recalc_values sets the unarmed values before calling it. */
void character_calculate_attack_values(Player *player);

/* sub_6621E. Armour heavier than 150 coins slows its wearer down; anything
 * that leaves them at 9 or less then gets 3 back. Called for every readied
 * item, and only acts on the one in the armour slot. */
void character_armor_weight_effect(const Item *item, Player *player);

/* sub_663C4. Carrying more than the character's strength allows costs movement,
 * and can only cost it: a character already slower than the weight limit would
 * make them keeps the lower figure. */
void character_calc_movement(Player *player);

/* sub_66C20, reclac_player_values. Rebuilds the readied-item slots, the weight
 * carried, the attack dice, the armour class and the movement rate from the
 * character's items and stats. Called after anything that could change them. */
void character_recalc_values(Player *player);

/* stat_bonus. The dexterity bonus to armour class, negative for the clumsy. */
int character_dex_ac_bonus(const Player *player);

/* The dexterity bonus to hit with a missile weapon. One point of dexterity out
 * of step with the armour class table, as the printed rules have it. */
int character_dex_reaction_adj(const Player *player);

/* playerStrengh. Strength as a single number 0..30, folding percentile strength
 * for an 18 into 19..23 and moving 19..25 up to 24..30 for monsters and magic.
 * Every strength table below is indexed by this. */
int character_strength_group(const Player *player);

/* The strength bonuses to hit and to damage. Both are gated on field_125, which
 * is what marks a character as able to benefit from strength at all. */
int character_strength_hit_bonus(const Player *player);
int character_strength_dam_bonus(const Player *player);

/* strength_bonus. How much weight the character can carry before slowing down,
 * in coins. Negative for the weak: they are encumbered before they pick
 * anything up. */
int character_max_encumberance(const Player *player);

/* ------------------------------------------------------------------- items */

/* Drops an item from the character's pack. The C# removed it by reference and
 * complained on screen when it was not there; the item is found by address here
 * for the same reason. */
void character_lose_item(Item *item, Player *player);

/* id_item. Builds item->name for display: the readied column, a magic asterisk
 * for anyone with detect magic up, the stack count, then the name itself with
 * whichever parts are still unidentified left off. With display_new_name the
 * result is drawn at the given cell.
 *
 * The name is built into the item's own 42-character field, as the original did,
 * and a name that would not fit is truncated rather than overrunning it. */
void character_item_display_name_build(bool display_new_name,
                                       bool display_readied,
                                       int y_col, int x_col, Item *item);

/* Whether the readied weapon can be thrown or fired, and whether it is one of
 * the ones that reaches two squares in melee as well. */
bool character_item_is_ranged_melee(const Item *item);
bool character_is_weapon_ranged(Player *player);
bool character_is_weapon_ranged_melee(Player *player);

/* sub_6906C. What the character's attack spends: the weapon itself when it is
 * thrown, or the readied arrows or quarrels when it fires them. False means
 * there is nothing to attack with. */
bool character_current_attack_item(Item **found_item, Player *player);

/* ------------------------------------------------------------------ display */

/* The party list at the top right: name, armour class and hit points, with
 * `player` shown in white as the selected character. Nothing is drawn on the
 * wilderness map, which has no room for it. */
void character_party_summary(const Player *player);

void character_display_ac(int y_offset, int x_offset, const Player *player);
/* Hit points in green at full, yellow when hurt, and white when highlighted. */
void character_display_hp(bool highlighted, int y_pos, int x_pos,
                          const Player *player);

/* hitpoint_ac. The combat side panel: name, hit points, armour class, the
 * readied weapon and, for anyone out of the fight, why. Redraws only when
 * gbl.display_hitpoints_ac says the panel is stale, and clears it. */
void character_combat_display_summary(Player *player);

/* sub_678A2. The character's name, in the colour that says which side they are
 * on: yellow for an enemy, light blue for a friend, red for anyone no longer in
 * the fight. With plural an apostrophe-s is added. */
void character_display_name(bool plural, int y_offset, int x_offset,
                            const Player *player);

/* sub_67788. "<name> <text>" in the status area - "is bandaged", "has no
 * effect", and so on - in combat next to the combat map and elsewhere under the
 * party list. With clear_display the line is wiped again after the usual pause.
 */
void character_display_status_string(bool clear_display, int line_y,
                                     const char *text, Player *player);

/* sub_6786F. Blanks the area character_display_status_string writes in. */
void character_clear_text_area(void);

/* string_print01. One line of text on the prompt row for a moment. */
void character_print_message(const char *text);

/* load_pic. Redraws whatever the current game state has on screen: the border,
 * the picture, the party summary and the position and time. */
void character_load_pic(void);

/* camping_search. The party's map square, facing, the time, and whether they
 * are camping or searching, on the row under the 3D view. */
void character_display_map_position_time(void);

/* sub_68DC0. The combat screen from scratch: palette, border and map. */
void character_redraw_combat_screen(void);

/* Steps through the party with the cursor keys until the player picks someone
 * or, with show_exit, backs out - which leaves *player NULL. */
void character_select_a_player(Player **player, bool show_exit,
                              const char *prompt);

/* -------------------------------------------------- missile and spell effects */

/* sub_67924. Loads one of the four missile animation cells from a combat icon:
 * cell 0 is the sprite as it stands, 1 is it mirrored, 2 the attack pose
 * mirrored and 3 the attack pose. */
void character_load_missile_dax(bool flip_icon, int icon_offset,
                                CombatIconState icon_action, int icon_idx);

/* sub_67A59. All four cells, from one icon. */
void character_load_missile_icons(int icon_idx);

/* sub_67AA4. Flies the loaded missile from attacker to target a step at a time,
 * scrolling the combat map along with it when the flight leaves the window.
 * frame_count is how many of the four cells to cycle. */
void character_draw_missile_attack(int delay, int frame_count, Point target,
                                   Point attacker);

/* sub_6818A. "<name> <text>" plus, in combat, a burst of stars or a flash over
 * the character: stars for a spell going off, the plain flash otherwise. */
void character_magic_attack_display(const char *text, bool show_magic_stars,
                                    Player *player);

/* ------------------------------------------------------------- combat state */

/* damage_player. Applies damage and works out what it leaves behind: unhurt,
 * unconscious at zero, dying and bleeding down to -9, or dead. A character who
 * drops out is taken out of the fight and off their side's count. */
void character_damage(int damage, Player *player);

/* sub_684F7. "is fully healed" or "is partially healed". */
void character_describe_healing(Player *player);

/* count_teams. Recounts gbl.friends_count and gbl.foe_count from the team
 * list. */
void character_count_combat_teams(void);

/* near_enermy. Every reachable member of the other side, nearest first, written
 * into out. Returns how many were found, which is never more than out_size. */
int character_build_near_targets(CombatPlayerIndex *out, int out_size,
                                 int max_range, Player *player);

/* sub_68708. How many squares apart two combatants are, walls ignored, or 0xff
 * when no path between them exists at all. */
int character_target_range(Player *target, Player *attacker);

void character_clear_actions(Player *player);

/* Spends the character's round standing ready. */
void character_guarding(Player *player);

/* sub_6886F. How many targets a spell may be aimed at, which for most spells is
 * the caster's level in the spell's own class. Spell 0 - no spell - is none, and
 * a spell coming out of an item is always six. */
int character_spell_max_target_count(int spell_id);

/* Anyone on our side still bleeding? With apply_bandage the first one found is
 * bandaged, which stops the bleeding and leaves them unconscious. */
bool character_bandage(bool apply_bandage);

#endif /* COAB_CHARACTER_H */
