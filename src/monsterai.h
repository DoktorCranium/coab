/* monsterai.h - what a combatant does when nobody is telling it what to do.
 * Ported from engine/ovr010.cs.
 *
 * One entry point matters to the rest of the engine:
 * monsterai_player_quick_fight takes a single combatant's whole turn. It is what
 * engine/ovr009.cs calls for a monster, for an NPC, and for a party member the
 * player has switched to quick fight, so this is the only code that ever decides
 * a turn without asking.
 *
 * The turn is taken in a fixed order of preference, and the first thing that
 * works ends it:
 *
 *   a movement pattern   Every combatant carries one of eleven patterns in
 *                        Action.field_15 - a list of six directions to try
 *                        relative to where it wants to go. A quarter of the time
 *                        it picks a new one, which is what stops a monster
 *                        walking into the same wall every round.
 *   a magic item         A readied wand or staff whose spell is worth casting.
 *   a spell in progress  One started last round, which goes off now.
 *   turning undead       A cleric with undead in reach.
 *   a memorized spell    Rolled for out of what it knows, best priority first.
 *   closing and hitting  Pick a weapon, walk towards a target and swing.
 *   guarding             Nothing else was possible.
 *
 * Whether a spell is worth casting is monsterai_should_cast_spell, and the check
 * that stops a monster dropping a fireball on its own side is
 * monsterai_spell_would_catch_own_side: it rolls the saving throws its own team
 * would have to make and calls the spell off if any of them would fail.
 *
 * The C#'s access levels are kept: the routines below were `internal static` and
 * the five that were private - guarding, the keyboard poll, the morale check,
 * an item's power rating and the weapon choice - are static in monsterai.c.
 *
 * One thing ovr010 calls belongs to an overlay that is not translated yet:
 * engine/ovr023.cs's sub_5D2E1, which resolves a spell. It says so once in the
 * log and then does nothing, so a fight can be fought without it.
 */
#ifndef COAB_MONSTERAI_H
#define COAB_MONSTERAI_H

#include "coab.h"
#include "player.h"
#include "point.h"

/* ovr010.PlayerQuickFight, sub_3504B. One combatant's whole turn, decided
 * without asking: see the order above. Returns when the turn is over - either
 * spent or given up on. */
void monsterai_player_quick_fight(Player *player);

/* ovr010.turn_undead. A cleric - or anyone who used to be one - who has not
 * turned yet this round and has undead in reach turns them, and this reports
 * that the turn was spent doing it. */
bool monsterai_turn_undead(Player *player);

/* ovr010.ShouldCastSpellX_sub1, sub_352AF. Would the spell, aimed at pos, catch
 * anyone on the caster's own side who could not save against it? The caster's
 * own team is the one that is checked, and the saving throws are rolled for
 * real: our own side is given -2 to save and the other side +8, so the AI is
 * much more careful with the party than with its own monsters.
 *
 * The caster is gbl.selected_player, as it was in the original. */
bool monsterai_spell_would_catch_own_side(int spell_id, Point pos);

/* ovr010.ShouldCastSpellX, sub_353B1. Whether this spell is worth casting now:
 * its priority has to reach min_priority, and then either it needs no target at
 * all, or it is a cure with someone to cure, or there is something in range that
 * it will not also catch our own side with. */
bool monsterai_should_cast_spell(int min_priority, int spell_id,
                                 Player *attacker);

/* ovr010.sub_354AA. Looks through the readied items for a wand or staff whose
 * spell is worth casting - highest priority first, for a rolled number of
 * priority levels - and uses it. Returns whether the turn went on an item.
 *
 * Nothing is tried at all in an area where spells do not work. */
bool monsterai_try_magic_item(Player *player);

/* ovr010.sub_3560B. Rolls a few times over what the caster has memorized,
 * dropping the priority it insists on each time round, and casts the first spell
 * that is worth casting. Party spellcasters are left alone unless the player has
 * turned gbl.auto_pcs_cast_magic on. */
bool monsterai_try_cast_spell(Player *player);

/* ovr010.CanMove, sub_3573B. Whether step number dir_step of the combatant's
 * movement pattern, measured from base_direction, can be taken: the ground has
 * to be passable, empty, and cheap enough for the movement left. *ground_clear
 * comes back true when the square is off the combat map altogether, which is how
 * a fleeing combatant finds its way out of the fight.
 *
 * A cloud of stinking gas or a cloud kill in the way costs more than any
 * movement, unless the combatant is proof against it. */
bool monsterai_can_move(bool *ground_clear, int base_direction, int dir_step,
                        Player *player);

/* ovr010.moralFailureEscape, sub_359D1. One step of running away, which is also
 * one step of walking towards a target: the two are the same code, and which one
 * it is depends on Action.moral_failure. A combatant that finds itself turning
 * back the way it came changes its movement pattern, and one that does that
 * twice gives up on its target.
 *
 * A magic-user with no armour on stands still instead, and so does anyone whose
 * nerve holds. */
void monsterai_moral_failure_escape(Player *player);

/* ovr010.sub_35DB1. The rest of the turn once a target has been found: close
 * with it and attack, round after round of the combatant's delay, until the
 * delay is used up or something stops it. Returns whether the turn is over.
 *
 * A member of our own side bandages a bleeding friend first, and that is the
 * whole turn. */
bool monsterai_close_and_attack(Player *player);

#endif /* COAB_MONSTERAI_H */
