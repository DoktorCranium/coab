/* affecttab.h - the affect jump table: what an affect does when it is applied to
 * a character or taken away again.
 * Ported from engine/ovr013.cs.
 *
 * An affect has three parts. affect.c is the record - the id, how long it has
 * left, one data byte - and the list a character carries. effect.c hangs records
 * on characters and takes them off again. This is the third: the handler that
 * gives an affect its meaning, one function per affect id.
 *
 * A handler is called at the moment the affect is applied or removed, and at
 * every point in a round where effect_check_affects asks the character's affects
 * whether they have anything to say. Nearly all of them work through gbl rather
 * than a return value - gbl.attack_roll, gbl.damage, gbl.saving_throw_roll,
 * gbl.current_affect - because an affect's job is to change a roll or a damage
 * total that has already been worked out. gbl.damage of 0 with gbl.current_affect
 * cleared is how a handler says "this does not touch me at all".
 *
 * Thirteen of the table's entries belong to engine/ovr014.cs and engine/ovr023.cs
 * and are not translated yet: those affects log once and do nothing, and are
 * named in the table so it is clear what is missing. ovr013.SetupAffectTables,
 * which seg001 called at startup to fill a Dictionary, has no equivalent - the
 * table here is static data.
 */
#ifndef COAB_AFFECTTAB_H
#define COAB_AFFECTTAB_H

#include "coab.h"
#include "enums.h"
#include "player.h"

/* ovr013.CallAffectTable, sub_630C7. Runs the handler for `affect`, or nothing
 * at all if the affect has none.
 *
 * `parameter` is the handler's argument and is whatever that handler expects -
 * an Affect for most of them, an Item for the item affects, NULL for the rest;
 * the C# passed it as `object`. A handler that needs one and is given NULL logs
 * and does nothing, where the C# would have thrown NullReferenceException.
 *
 * When gbl.apply_item_affect is set the table sends every affect to the item
 * handler instead, which is how a magic item's own affect reaches its wearer. The
 * handler clears the flag again. */
void affect_table_call(Effect add_remove, void *parameter, Player *player,
                       Affects affect);

#endif /* COAB_AFFECTTAB_H */
