/* spellcast.h - casting a spell: the names, how far one reaches, how long its
 * affect lasts, and the run from "the player picked this spell" to "the handler
 * ran".
 * Ported from engine/ovr023.cs, which is split three ways here: the spell lists
 * are spellmenu.h, the individual spells are spelleffect.h, and everything
 * common to all of them is this file.
 */
#ifndef COAB_SPELLCAST_H
#define COAB_SPELLCAST_H

#include "coab.h"
#include "enums.h"
#include "item.h"
#include "player.h"

/* ovr023.SpellNames, which the C# also labelled AffectNames. One name per spell
 * id, ids 1..0x64; row 0 is the empty placeholder that keeps the table 1-based,
 * and the ids the game never gives out are empty too.
 *
 * Returns "" for anything outside the table, where the C# would have thrown -
 * including spell id 0x65, which spell_casting_table has a row for. */
const char *spellcast_spell_name(int spell_id);

/* ovr023.SpellRange, sub_5CDE5. How far the spell reaches, in map squares: a
 * fixed part plus a part per level of the caster's class - or of level six, for a
 * spell coming out of an item. Never less than one square for a spell that has a
 * target at all. */
int spellcast_spell_range(int spell_id);

/* ovr023.remove_spell_from_scroll, sub_623FF. Blanks the scribed spell on the
 * scroll and takes one off its "With N Spells" name part; a scroll with no
 * spells left on it is dropped.
 *
 * *out_scroll, when it is not NULL, receives the scroll as it stands after the
 * change. That is the only way to see it once it has been dropped: the pack
 * closes up over the gap, so `item` names a different item afterwards. Returns
 * false when the scroll was dropped. */
bool spellcast_remove_spell_from_scroll(int spell_id, Item *item, Player *player,
                                       Item *out_scroll);

/* ovr023.DisplayCaseSpellText, cast_a_spell. "<name> casts / has memorized / has
 * scribed" with the spell's name under it, in the status area next to the combat
 * map or under the party list outside a fight. */
void spellcast_display_case_spell_text(int spell_id, const char *text,
                                       Player *player);

/* ovr023.GetSpellAffectTimeout, sub_5CE92. How long the spell's affect lasts, in
 * minutes. Five spells roll for it; the rest take a fixed part plus a part per
 * level of the caster off the table. */
u16 spellcast_spell_affect_timeout(int spell_id);

/* ovr023.DoSpellCastingWork, sub_5CF7F. Applies the spell to everyone on
 * gbl.spell_targets: a saving throw where the spell allows one, the damage, then
 * the spell's own affect. A spell of fixed range -1 is a touch and has to hit
 * first.
 *
 * `target_count` is the byte the affect is given to keep, which most of the
 * spells that pass one use for something other than a count - a team number, a
 * number of images, a duration. Zero means "the caster's level in the spell's
 * class". */
void spellcast_do_casting_work(const char *text, int damage_flags, int damage,
                               bool call_affect_table, int target_count,
                               int spell_id);

/* ovr023.NonCombatSpellCast, cast_spell_on. Works out what the spell touches
 * outside a fight - the caster, one party member chosen with the cursor, or
 * everybody - and returns false when there is nothing to cast it at. This is
 * what gbl.spell_cast_function points at while no battle is running;
 * engine/ovr014.cs's targeting is the other half. */
bool spellcast_non_combat_cast(QuickFight quick_fight, int spell_id);

/* ovr023.sub_5D2E1, both overloads. Casting one spell from start to finish:
 * the refusal for a combat spell out of combat, the miscast roll, the aiming (via
 * gbl.spell_cast_function), the missile animation, taking the spell out of the
 * caster's memory and running its handler.
 *
 * *turn_used says whether the caster spent their action. The overload without it
 * is the C#'s, which threw the answer away. */
void spellcast_resolve_spell(bool show_casting_text, QuickFight quick_fight,
                             int spell_id);
void spellcast_resolve_spell_used(bool *turn_used, bool show_casting_text,
                                  QuickFight quick_fight, int spell_id);

#endif /* COAB_SPELLCAST_H */
