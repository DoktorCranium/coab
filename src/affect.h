/* affect.h - a timed effect on a character: a spell, a curse, a poison.
 * Ported from Classes/Affect.cs.
 *
 * The record is 9 bytes, of which 5 carry data; the last 4 were the far pointer
 * to the next affect in the DOS original's linked list and are unused here.
 * Save games store them as a bare stream of 9-byte records in <name>.fx, with no
 * count, so the reader stops at end of file.
 */
#ifndef COAB_AFFECT_H
#define COAB_AFFECT_H

#include "coab.h"
#include "enums.h"
#include "dataio.h"

#define AFFECT_RECORD_SIZE 9

typedef struct {
    int  type;              /* Affects */
    u16  minutes;           /* remaining duration */
    u8   affect_data;       /* meaning depends on type */
    bool call_affect_table; /* run the affect's jump-table entry on expiry */
} Affect;

extern const DioDesc affect_desc;

void affect_init(Affect *a, Affects type, u16 minutes, u8 affect_data,
                 bool call_affect_table);

bool affect_read(Affect *a, const u8 *data, size_t data_size, size_t offset);
bool affect_write(const Affect *a, u8 *data, size_t data_size);

/* --- a character's affect list ---
 *
 * The original chained these off field 0xf2 with no limit, and the C# port used
 * an unbounded List. A .fx file is just a run of records, so nothing in the
 * format bounds it either. A character never accumulates more than a handful in
 * play; the cap here is far above that, and overflow is logged rather than
 * silently dropped.
 */
#define AFFECT_LIST_MAX 64

typedef struct {
    Affect items[AFFECT_LIST_MAX];
    int    count;
} AffectList;

void    affect_list_clear(AffectList *l);
bool    affect_list_add(AffectList *l, const Affect *a);
void    affect_list_remove_at(AffectList *l, int index);

/* Affect.HasAffect / GetAffect. The getters return NULL when absent, and the
 * pointer is into the list, so it dies with the next removal. */
bool          affect_list_has(const AffectList *l, Affects type);
Affect       *affect_list_find(AffectList *l, Affects type);
const Affect *affect_list_find_const(const AffectList *l, Affects type);

/* affects.Remove(affect): takes this one entry off the list. The C# removed the
 * object itself, so a character carrying two of the same affect keeps the other;
 * `a` must point into the list, and every pointer into it from that entry on is
 * stale afterwards. False, with a warning, for anything else. */
bool affect_list_remove(AffectList *l, const Affect *a);

/* Removes every affect of this type, and reports how many went. */
int affect_list_remove_type(AffectList *l, Affects type);

/* Player.IsHeld: snake charm, paralysis, sleep or helplessness. */
bool affect_list_is_held(const AffectList *l);

#endif /* COAB_AFFECT_H */
