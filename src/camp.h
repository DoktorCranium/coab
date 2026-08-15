/* camp.h - making camp: the magic, alter and fix menus.
 * Ported from engine/ovr016.cs.
 *
 * This is the screen the party comes back to between fights. Three things happen
 * here that happen nowhere else:
 *
 *   memorising and scribing - the other half of the spell lists spellmenu.c
 *                             builds. Picking spells only marks them: they are
 *                             finished by resting.c while the clock runs, which
 *                             is why every routine here that touches a spell
 *                             ends up in gbl.time_to_rest.
 *   the marching order      - who walks in front, which decides who a monster
 *                             reaches first.
 *   binding wounds          - Fix spends the party's cure spells on the whole
 *                             party at once, without making anyone aim them.
 *
 * The spells picked for memorising and scribing are cancelled both on the way
 * into the camp and on the way out, so a camp that is interrupted loses them: a
 * spell half-memorised when a monster wanders in was never memorised at all.
 *
 * ovr016 keeps most of itself private, and so does this file. Only the three
 * entry points other overlays call are public, plus the two the C# marked
 * internal that a test can reach: the rest length a party needs and how many
 * spells of a level a character may still take.
 */
#ifndef COAB_CAMP_H
#define COAB_CAMP_H

#include "coab.h"
#include "enums.h"
#include "player.h"

/* ovr016.sub_44032. How long, in minutes, this character needs to finish
 * everything they have lined up: a flat four minutes to settle down, six if
 * anything is above 2nd level, plus a quarter of an hour per spell level being
 * memorised or copied off a scroll.
 *
 * Also sets player->spell_to_learn_count, which is the delay resting.c counts
 * down before each spell is finished, so this has to be called before a rest for
 * anything to be learnt at all. */
int camp_spell_learn_time(Player *player);

/* ovr016.cancel_spells. Drops every spell the party had lined up for memorising
 * and clears the "still to copy" bit on every scroll they carry. */
void camp_cancel_spells(void);

/* ovr016.HowManySpellsPlayerCanLearn, sub_4428E. How many more spells of this
 * class and level gbl.selected_player may line up: what their level allows, less
 * what they have already picked. Spell levels are 1-based here, as the spell
 * table has them. */
int camp_spells_can_learn(SpellClass spell_class, int spell_level);

/* ovr016.sub_443A0. Whether the selected character can do the thing the caller
 * is about to offer them - 1 cast, 2 memorise, 3 scribe - and puts up the reason
 * when they cannot. */
bool camp_can_use_spells(u8 learn_type);

/* ovr016.cast_spell. Casts spells out of memory, one after another, until the
 * player picks nothing. Also the dungeon menu's Cast, which is the one thing
 * outside this file that calls into ovr016 for anything but camping. */
void camp_cast_spell(void);

/* ovr016.rest_menu. Works out how long the party needs, offers the rest menu and
 * sleeps. True when something interrupted the rest, which is what sends the
 * caller into a fight. */
bool camp_rest_menu(void);

/* ovr016.memorize_spell and ovr016.scribe_spell. Pick the spells to memorise
 * from the grimoire, or the ones to copy off the scrolls in the pack. Both ask
 * for confirmation and undo the lot when it is refused. Neither learns anything
 * by itself; resting does. */
void camp_memorize_spell(void);
void camp_scribe_spell(void);

/* ovr016.BuildEffectNameMap. Builds the affect-to-name table the Display screen
 * reads, from the spell names of whichever spell lays each affect. Called once at
 * startup, from where the C#'s seg001 called it. */
void camp_build_effect_name_map(void);

/* What that table answers for one affect, or NULL for an affect with no name -
 * which is how the Display screen decides an affect is not worth showing.
 * Public for the self-test; nothing else outside this file reads it. */
const char *camp_effect_name(Affects type);

/* ovr016.magic_menu. Cast, Memorize, Scribe, Display, Rest. True when a rest
 * started here was interrupted. */
bool camp_magic_menu(void);

/* ovr016.alter_menu. Order, Drop, Speed and the icon editor. */
void camp_alter_menu(void);

/* ovr016.game_speed. The 0..9 delay every drawing loop in the game reads. */
void camp_game_speed(void);

/* ovr016.CalculateTimeAndSpellNumbers, sub_460ED. How many cure spells the party
 * has between them and how long using them all would take, which goes into
 * gbl.time_to_rest. The time is scaled down when the party's wounds are lighter
 * than the spells would heal, so a scratch does not cost six hours. */
void camp_calculate_time_and_spell_numbers(int *out_cure_critical,
                                           int *out_cure_serious,
                                           int *out_cure_light);

/* ovr016.MakeCamp, make_camp. The camp screen, until the player leaves it. True
 * when a rest inside it was interrupted, which is what makes the script's
 * interrupt handler run. */
bool camp_make_camp(void);

#endif /* COAB_CAMP_H */
