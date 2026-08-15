/* spelllist.h - the spells a character has memorized, or is memorizing.
 * Ported from Classes/SpellList.cs.
 *
 * On disk this is 84 bytes at 0x1e of the player record, one spell id per byte,
 * zero meaning empty. Bit 7 of a byte marks a spell still being memorized, but
 * only finished spells are ever written, so the flag survives a save only as a
 * cleared bit. Entries are written from the end of the block backwards, which is
 * what the original did and what reverses list order across a save.
 */
#ifndef COAB_SPELLLIST_H
#define COAB_SPELLLIST_H

#include "coab.h"

#define SPELL_LIST_RECORD_SIZE 84

/* Memorized spells cannot exceed the 84 the record holds; spells still being
 * learnt are not saved and so are not bounded by it. The extra room is for
 * those. */
#define SPELL_LIST_MAX 128

typedef struct {
    u8   id;
    bool learning;
} SpellItem;

typedef struct {
    SpellItem items[SPELL_LIST_MAX];
    int       count;
} SpellList;

void spell_list_clear(SpellList *sl);

/* AddLearn: a spell being memorized. */
bool spell_list_add_learn(SpellList *sl, int id);

/* AddLearnt: takes a raw record byte, so bit 7 is the learning flag. */
bool spell_list_add_learnt(SpellList *sl, int raw);

/* ClearSpell: removes the first entry with this id, if any. */
void spell_list_clear_spell(SpellList *sl, int id);

/* MarkLearnt: the first still-learning entry with this id becomes memorized. */
void spell_list_mark_learnt(SpellList *sl, int id);

/* CancelLearning: drops every entry still being memorized. */
void spell_list_cancel_learning(SpellList *sl);

bool spell_list_has_spells(const SpellList *sl);
bool spell_list_has_spell(const SpellList *sl, int id);

int  spell_list_count(const SpellList *sl);
int  spell_list_learnt_count(const SpellList *sl);
int  spell_list_learning_count(const SpellList *sl);

/* SpellList.Load / SpellList.Save. */
void spell_list_load(SpellList *sl, const u8 *data, size_t data_size, size_t offset);
void spell_list_save(const SpellList *sl, u8 *data, size_t data_size, size_t offset);

#endif /* COAB_SPELLLIST_H */
