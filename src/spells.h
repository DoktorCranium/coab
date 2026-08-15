/* spells.h - the spell casting table.
 * Ported from Classes/Spells.cs (SpellEntry, Struct_19AEC) and the
 * spellCastingTable array in Classes/Gbl.cs (seg600:37DC asc_19AEC).
 *
 * One row per spell, indexed by spell id. Ids are 1-based, so row 0 is a
 * placeholder - the C# table opened with a null and the code indexed straight
 * into it, which is why nothing may be shifted up.
 */
#ifndef COAB_SPELLS_H
#define COAB_SPELLS_H

#include "coab.h"
#include "enums.h"

/* Fields are in the order the DOS record has them, which is also the order the
 * C# constructor took its arguments, so the table below reads line for line
 * against Gbl.cs. */
typedef struct {
    int          spell_idx;         /* the row's own spell id */
    SpellClass   spell_class;       /* field_0, seg600:37DC */
    int          spell_level;       /* field_1 */
    int          fixed_range;       /* -1 means touch */
    int          per_lvl_range;
    int          fixed_duration;
    int          per_lvl_duration;
    u8           field_6;
    SpellTargets target_type;
    DamageOnSave damage_on_save;    /* field_8 */
    SaveVerseType save_verse;
    Affects      affect_id;         /* field_A */
    SpellWhen    when_cast;
    int          casting_delay;
    int          priority;
    u8           field_e;
    u8           field_f;
} SpellEntry;

/* Rows 0..0x65. Row 0x65 is one past the highest id the Spells enum names; the
 * original table has it and monster code reaches for it, so it stays. */
#define SPELL_CASTING_TABLE_COUNT 0x66

extern const SpellEntry spell_casting_table[SPELL_CASTING_TABLE_COUNT];

/* Returns NULL and logs for id 0 or an id past the end of the table. The C#
 * indexed the array directly, so a stray id either threw or, for id 0,
 * dereferenced the null placeholder. */
const SpellEntry *spell_entry(int spell_id);

/* True if spell_entry() would return a row. */
bool spell_id_valid(int spell_id);

#endif /* COAB_SPELLS_H */
