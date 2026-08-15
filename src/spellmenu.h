/* spellmenu.h - the spell lists a character is shown, and picking one off them.
 * Ported from engine/ovr023.cs (the list half; the casting half is spellcast.h
 * and the individual spells are spelleffect.h).
 *
 * One list is built at a time into a private MenuList, and every entry on it has
 * a matching spell id in a parallel array, because a heading row - "3rd Level" -
 * has no id of its own. That is why spellmenu_menu counts pickable rows rather
 * than list indices to find the id the player chose.
 *
 * Everything ovr023 kept to itself stays private here, as ovr025's FindAffect
 * does: gbl.spell_string_list, gbl.memorize_spell_id, gbl.memorize_count,
 * gbl.scribeScrolls and gbl.scribeScrollsCount are all read and written only
 * inside this file, so they are statics rather than Gbl fields.
 */
#ifndef COAB_SPELLMENU_H
#define COAB_SPELLMENU_H

#include "coab.h"
#include "enums.h"
#include "player.h"

/* ovr023.can_learn_spell, sub_5C01E. Whether the character could have this spell
 * at all: the right class for the spell's own class, and the stat that class
 * casts on above 8. Bit 7 of spell_id - the still-being-memorised marker - is
 * masked off first. */
bool spellmenu_can_learn_spell(int spell_id, const Player *player);

/* ovr023.BuildSpellList, sub_5CA74. Fills the list from wherever `location` says
 * the spells are - memory, the grimoire, one scroll, every scroll, the spells the
 * character could still choose to learn - and returns whether it found any. The
 * scroll cases skip the level headings, which is what the C#'s buildSpellList
 * flag decided. */
bool spellmenu_build_spell_list(SpellLoc location);

/* ovr023.spell_menu. Runs the list built by spellmenu_build_spell_list and hands
 * back the spell id that was picked, or 0 when the player backed out. *index is
 * the pickable row the highlight starts on and is left where it ended; a negative
 * index means "start at the top and redraw".
 *
 * For SPELL_SOURCE_SCRIBE gbl.current_scroll is left pointing at the scroll the
 * chosen spell was found on. The list is cleared on the way out, as the C# did.
 */
u8 spellmenu_menu(int *index, SpellSource source);

#endif /* COAB_SPELLMENU_H */
