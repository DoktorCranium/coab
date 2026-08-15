/* item.h - an item: weapon, armour, scroll, potion, gem, coin pile.
 * Ported from Classes/Item.cs and Classes/ItemData.cs.
 *
 * An item record is 0x3F bytes and carries no properties of its own beyond
 * three name indices and a handful of bonuses. Everything mechanical - what
 * slot it fills, how much damage it does, which classes may use it - comes from
 * the ITEM.DAT table keyed on the item's type, loaded once at startup.
 *
 * Classes/ItemLibrary.cs is deliberately not ported. It collected every distinct
 * item the engine ever constructed into a BinaryFormatter dump under AppData for
 * offline inspection; it fed nothing back into the game.
 */
#ifndef COAB_ITEM_H
#define COAB_ITEM_H

#include "coab.h"
#include "dataio.h"
#include "enums.h"

/* ----------------------------------------------------------------- ItemData */

/* ItemData.field_E, a bitmask of ammunition and reach properties. */
#define ITEM_DATA_ARROWS   0x01
#define ITEM_DATA_FLAG_02  0x02
#define ITEM_DATA_MELEE    0x04
#define ITEM_DATA_FLAG_08  0x08
#define ITEM_DATA_FLAG_10  0x10
#define ITEM_DATA_FLAG_20  0x20
#define ITEM_DATA_FLAG_40  0x40
#define ITEM_DATA_QUARRELS 0x80

/* One 16-byte row of ITEM.DAT (seg600:5D10, unk_1C020). */
typedef struct {
    int slot;                /* 0x0, ItemSlot */
    u8  hands_count;         /* 0x1 */
    u8  dice_count_large;    /* 0x2 */
    u8  dice_size_large;     /* 0x3 */
    i8  bonus_large;         /* 0x4 */
    int number_attacks;      /* 0x5 */
    u8  field_6;             /* 0x6 */
    u8  field_7;             /* 0x7 */
    u8  field_8;             /* 0x8 */
    u8  dice_count_normal;   /* 0x9 */
    u8  dice_size_normal;    /* 0xa */
    i8  bonus_normal;        /* 0xb */
    int range;               /* 0xc */
    u8  class_flags;         /* 0xd */
    u8  flags;               /* 0xe, ITEM_DATA_* */
    u8  field_F;             /* 0xf */
} ItemData;

#define ITEM_DATA_RECORD_SIZE 0x10
#define ITEM_DATA_COUNT       0x81   /* 129 rows, indexed by ItemType */

/* Loads the table. name is a DOS-style basename resolved through the vfs; the
 * file starts with a two-byte header that is skipped, then 0x81 rows. */
bool item_data_table_load(const char *name);

/* The row for an item type. An index outside the table - which only a corrupt
 * save or an out-of-range monster item can produce - is logged once and answered
 * with an all-zero row, so callers see an inert item rather than reading past
 * the table. */
const ItemData *item_data(int type);

/* --------------------------------------------------------------------- Item */

#define ITEM_RECORD_SIZE 0x3f
#define ITEM_NAME_MAX    0x2a   /* 42 characters, plus the length byte */

/* Enough for three of the longest name parts and their plural suffixes. */
#define ITEM_NAME_GEN_MAX 80

/* Tagged, so that the headers which only need to pass an item along - menu.h is
 * the one - can forward-declare "struct Item" instead of including this. */
typedef struct Item {
    char name[ITEM_NAME_MAX + 1];   /* 0x00, Pascal string; 0x2b..0x2d unused */
    int  type;                      /* 0x2e, ItemType */
    int  namenum1;                  /* 0x2f, index into the name-part table */
    int  namenum2;                  /* 0x30 */
    int  namenum3;                  /* 0x31 */
    int  plus;                      /* 0x32, signed: cursed items are negative */
    u8   plus_save;                 /* 0x33, bonus to saving throws */
    bool readied;                   /* 0x34 */
    u8   hidden_names_flag;         /* 0x35, which name parts stay unidentified */
    bool cursed;                    /* 0x36 */
    i16  weight;                    /* 0x37 */
    int  count;                     /* 0x39, one byte on disk */
    i16  value;                     /* 0x3a, in electrum: gold worth is halved */
    int  affect_1;                  /* 0x3c, Affects */
    int  affect_2;                  /* 0x3d */
    int  affect_3;                  /* 0x3e */
} Item;

extern const DioDesc item_desc;

void item_clear(Item *it);

/* The 15-argument constructor, minus its AddToLibrary flag. Arguments are in
 * declaration order rather than the C#'s reversed push order. */
void item_init(Item *it, ItemType type, u8 namenum1, u8 namenum2, u8 namenum3,
               i8 plus, u8 plus_save, bool readied, u8 hidden_names_flag,
               bool cursed, i16 weight, u8 count, i16 value,
               Affects affect_1, Affects affect_2, Affects affect_3);

bool item_read(Item *it, const u8 *data, size_t data_size, size_t offset);
bool item_write(const Item *it, u8 *data, size_t data_size);

/* Item.Equals: everything but the name, which is derived. */
bool item_equals(const Item *a, const Item *b);

/* --- the three name parts ---
 *
 * index is 1, 2 or 3. Out of range is logged and reads as 0, which selects the
 * empty name; the C# threw NotSupportedException. */
u8 item_namenum(const Item *it, int index);

/* getAffect / setAffect, likewise indexed 1..3. */
Affects item_affect(const Item *it, int index);
void    item_affect_set(Item *it, int index, Affects value);

/* GenerateName: assembles the display name from whichever of the three parts
 * hidden_names_flag leaves visible, pluralising the right one when the stack
 * holds more than one. Writes at most dst_size bytes including the NUL and
 * returns dst. Pass hidden_names_flag 0 for the identified name. */
char *item_generate_name(const Item *it, int hidden_names_flag,
                         char *dst, size_t dst_size);

/* --- table lookups --- */
u8   item_hands_count(const Item *it);
bool item_is_scroll(const Item *it);
bool item_is_ranged(const Item *it);

/* ScrollLearning: does name part i name a spell still to be learnt, and is it
 * this spell? The high bit of an affect byte marks "not yet learnt". */
bool item_scroll_learning(const Item *it, int i, int spell);

#endif /* COAB_ITEM_H */
