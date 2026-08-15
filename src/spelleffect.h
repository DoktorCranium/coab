/* spelleffect.h - what a spell does once it has been aimed: the ninety-odd
 * handlers, one per spell id.
 * Ported from engine/ovr023.cs (the handler half; the spell lists are
 * spellmenu.h and the run-up to casting is spellcast.h).
 *
 * ovr023.setup_spells filled a Dictionary<Spells, spellDelegate2> at startup and
 * sub_5D2E1 looked the cast spell up in it. Here that dictionary is static data
 * private to spelleffect.c, so none of the handlers needs an external name:
 * spelleffect_call is the only way in. What is left of setup_spells - the four
 * globals it also set - is spelleffect_setup_spells.
 */
#ifndef COAB_SPELLEFFECT_H
#define COAB_SPELLEFFECT_H

#include "coab.h"
#include "enums.h"
#include "player.h"

/* ovr023.setup_spells, less the table: clears the cure-spell and from-item
 * flags, forgets the last spell target and points gbl.spell_cast_function at the
 * non-combat targeting. seg001 called it once at startup, just before
 * ovr013.SetupAffectTables. */
void spelleffect_setup_spells(void);

/* gbl.spellTable[spell_id]() - runs the spell's own handler. An id the table has
 * no entry for is logged and does nothing, where the C# dictionary lookup would
 * have thrown KeyNotFoundException; the ids the game never hands out (0x39 and
 * the other blanks in the name table) are exactly those. */
void spelleffect_call(int spell_id);

/* The eight ovr023 entries in the affect jump table - see affecttab.c. A monster
 * breathing, gazing or spitting aims itself through gbl.spell_cast_function just
 * as a cast spell does, which is why these live with the spells rather than with
 * the rest of the affect handlers.
 *
 * All eight take the affect-table handler signature: the Add/Remove that asked
 * for them, the parameter (an Affect for the three that count their uses down,
 * unused by the rest) and the character carrying the affect. */
void spelleffect_affect_paralizing_gaze(Effect add_remove, void *param,
                                       Player *player);
void spelleffect_dragon_breath_elec(Effect add_remove, void *param,
                                    Player *player);
void spelleffect_affect_spit_acid(Effect add_remove, void *param,
                                  Player *player);
void spelleffect_dragon_breath_acid(Effect add_remove, void *param,
                                    Player *attacker);
void spelleffect_dragon_breath_fire(Effect add_remove, void *param,
                                    Player *attacker);
void spelleffect_cast_breath_fire(Effect add_remove, void *param,
                                  Player *player);
void spelleffect_cast_throw_lightening(Effect add_remove, void *param,
                                       Player *caster);
void spelleffect_cast_gaze_paralyze(Effect add_remove, void *param,
                                    Player *player);

#endif /* COAB_SPELLEFFECT_H */
