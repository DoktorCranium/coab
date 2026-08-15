/* item.c - Ported from Classes/Item.cs and Classes/ItemData.cs. */
#include <stdlib.h>
#include <string.h>

#include "item.h"
#include "log.h"
#include "vfs.h"

/* ----------------------------------------------------------------- ItemData */

static ItemData item_data_rows[ITEM_DATA_COUNT];
static const ItemData item_data_none;   /* returned for an out-of-range type */
static bool item_data_loaded;

static void item_data_parse(ItemData *d, const u8 *data, size_t offset)
{
    d->slot              = data[offset + 0x0];
    d->hands_count       = data[offset + 0x1];
    d->dice_count_large  = data[offset + 0x2];
    d->dice_size_large   = data[offset + 0x3];
    d->bonus_large       = (i8)data[offset + 0x4];
    d->number_attacks    = data[offset + 0x5];
    d->field_6           = data[offset + 0x6];
    d->field_7           = data[offset + 0x7];
    d->field_8           = data[offset + 0x8];
    d->dice_count_normal = data[offset + 0x9];
    d->dice_size_normal  = data[offset + 0xa];
    d->bonus_normal      = (i8)data[offset + 0xb];
    d->range             = data[offset + 0xc];
    d->class_flags       = data[offset + 0xd];
    d->flags             = data[offset + 0xe];
    d->field_F           = data[offset + 0xf];
}

bool item_data_table_load(const char *name)
{
    const char *path;
    u8 *data;
    size_t size, rows;

    path = vfs_resolve(name);
    if (path == NULL) {
        log_error("item table: %s not found in %s", name, vfs_data_dir());
        return false;
    }

    data = vfs_read_file(path, &size);
    if (data == NULL) {
        return false;
    }
    if (size < 2 + ITEM_DATA_RECORD_SIZE) {
        log_error("item table: %s is only %zu bytes", name, size);
        free(data);
        return false;
    }

    /* The file opens with two bytes the original skipped over. The shipped
     * ITEMS is 2050 bytes, which is 0x80 rows and not the 0x81 the table holds:
     * the C# asked for 0x810 bytes, got 0x800, and left its last row zeroed.
     * Same here - the missing row is item type 128, which carries no data. */
    memset(item_data_rows, 0, sizeof(item_data_rows));

    rows = (size - 2) / ITEM_DATA_RECORD_SIZE;
    if (rows > ITEM_DATA_COUNT) {
        rows = ITEM_DATA_COUNT;
    }
    for (size_t i = 0; i < rows; i++) {
        item_data_parse(&item_data_rows[i], data,
                        2 + i * ITEM_DATA_RECORD_SIZE);
    }
    if (rows < ITEM_DATA_COUNT) {
        log_info("item table: %s holds %zu of %d rows; the rest read as zero",
                 name, rows, ITEM_DATA_COUNT);
    }

    free(data);
    item_data_loaded = true;
    return true;
}

const ItemData *item_data(int type)
{
    static bool complained;

    if (type < 0 || type >= ITEM_DATA_COUNT) {
        if (!complained) {
            complained = true;
            log_warn("item type %d outside the 0..%d table; treating as inert",
                     type, ITEM_DATA_COUNT - 1);
        }
        return &item_data_none;
    }
    if (!item_data_loaded && !complained) {
        complained = true;
        log_warn("item table queried before it was loaded");
    }

    return &item_data_rows[type];
}

/* --------------------------------------------------------------------- Item */

/* Item writes its name with Sys.StringToArray, which puts the field capacity in
 * the length byte and zero-pads, not the string length DIO_PSTRING would write.
 * The difference is invisible on read but not in the bytes, so it is preserved. */
static void item_name_read(void *member, const u8 *data, size_t offset)
{
    sys_array_to_string((char *)member, ITEM_NAME_MAX + 1,
                        data, offset, ITEM_NAME_MAX);
}

static void item_name_write(const void *member, u8 *data, size_t offset)
{
    sys_string_to_array(data, offset, ITEM_NAME_MAX, (const char *)member);
}

static const DioField item_fields[] = {
    DIO_CUSTOM_F(Item, name, 0x00, ITEM_NAME_MAX + 1, item_name_read,
                 item_name_write),
    /* 0x2b..0x2d unused. */
    DIO_F(Item, type,              0x2e, DIO_IBYTE,  0),
    DIO_F(Item, namenum1,          0x2f, DIO_IBYTE,  0),
    DIO_F(Item, namenum2,          0x30, DIO_IBYTE,  0),
    DIO_F(Item, namenum3,          0x31, DIO_IBYTE,  0),
    DIO_F(Item, plus,              0x32, DIO_ISBYTE, 0),
    DIO_F(Item, plus_save,         0x33, DIO_BYTE,   0),
    DIO_F(Item, readied,           0x34, DIO_BOOL,   0),
    DIO_F(Item, hidden_names_flag, 0x35, DIO_BYTE,   0),
    DIO_F(Item, cursed,            0x36, DIO_BOOL,   0),
    DIO_F(Item, weight,            0x37, DIO_SWORD,  0),
    DIO_F(Item, count,             0x39, DIO_IBYTE,  0),
    DIO_F(Item, value,             0x3a, DIO_SWORD,  0),
    DIO_F(Item, affect_1,          0x3c, DIO_IBYTE,  0),
    DIO_F(Item, affect_2,          0x3d, DIO_IBYTE,  0),
    DIO_F(Item, affect_3,          0x3e, DIO_IBYTE,  0),
    DIO_END_MARKER
};

const DioDesc item_desc = { "Item", ITEM_RECORD_SIZE, item_fields };

void item_clear(Item *it)
{
    memset(it, 0, sizeof(*it));
}

void item_init(Item *it, ItemType type, u8 namenum1, u8 namenum2, u8 namenum3,
               i8 plus, u8 plus_save, bool readied, u8 hidden_names_flag,
               bool cursed, i16 weight, u8 count, i16 value,
               Affects affect_1, Affects affect_2, Affects affect_3)
{
    item_clear(it);

    it->type              = (int)type;
    it->namenum1          = namenum1;
    it->namenum2          = namenum2;
    it->namenum3          = namenum3;
    it->plus              = plus;
    it->plus_save         = plus_save;
    it->readied           = readied;
    it->hidden_names_flag = hidden_names_flag;
    it->cursed            = cursed;
    it->weight            = weight;
    it->count             = count;
    it->value             = value;
    it->affect_1          = (int)affect_1;
    it->affect_2          = (int)affect_2;
    it->affect_3          = (int)affect_3;
}

bool item_read(Item *it, const u8 *data, size_t data_size, size_t offset)
{
    item_clear(it);
    return dio_read(&item_desc, it, data, data_size, offset);
}

bool item_write(const Item *it, u8 *data, size_t data_size)
{
    if (data_size < ITEM_RECORD_SIZE) {
        return false;
    }
    memset(data, 0, ITEM_RECORD_SIZE);
    return dio_write(&item_desc, it, data, data_size);
}

bool item_equals(const Item *a, const Item *b)
{
    return a->type             == b->type &&
           a->namenum1         == b->namenum1 &&
           a->namenum2         == b->namenum2 &&
           a->namenum3         == b->namenum3 &&
           a->plus             == b->plus &&
           a->plus_save        == b->plus_save &&
           a->readied          == b->readied &&
           a->hidden_names_flag == b->hidden_names_flag &&
           a->cursed           == b->cursed &&
           a->weight           == b->weight &&
           a->count            == b->count &&
           a->value            == b->value &&
           a->affect_1         == b->affect_1 &&
           a->affect_2         == b->affect_2 &&
           a->affect_3         == b->affect_3;
}

u8 item_namenum(const Item *it, int index)
{
    switch (index) {
    case 1: return (u8)it->namenum1;
    case 2: return (u8)it->namenum2;
    case 3: return (u8)it->namenum3;
    default:
        log_warn("item name part %d requested; only 1..3 exist", index);
        return 0;
    }
}

Affects item_affect(const Item *it, int index)
{
    switch (index) {
    case 1: return (Affects)it->affect_1;
    case 2: return (Affects)it->affect_2;
    case 3: return (Affects)it->affect_3;
    default:
        log_warn("item affect %d requested; only 1..3 exist", index);
        return (Affects)0;
    }
}

void item_affect_set(Item *it, int index, Affects value)
{
    switch (index) {
    case 1: it->affect_1 = (int)value; break;
    case 2: it->affect_2 = (int)value; break;
    case 3: it->affect_3 = (int)value; break;
    default:
        log_warn("item affect %d assigned; only 1..3 exist", index);
        break;
    }
}

/* --------------------------------------------------------------- name parts */

/* The name-part table (Item.itemNames). A name index is one byte, and the table
 * covers all 256 of them, so no index can miss. Entries 0, 0x3e, 0x3f and 0x90
 * are empty in the original. */
static const char *const item_names[256] = {
    /* 0x00 */ "", "Battle Axe", "Hand Axe", "Bardiche",
    /* 0x04 */ "Bec De Corbin", "Bill-Guisarme", "Bo Stick", "Club",
    /* 0x08 */ "Dagger", "Dart", "Fauchard", "Fauchard-Fork",
    /* 0x0C */ "Flail", "Military Fork", "Glaive", "Glaive-Guisarme",
    /* 0x10 */ "Guisarme", "Guisarme-Voulge", "Halberd", "Lucern Hammer",
    /* 0x14 */ "Hammer", "Javelin", "Jo Stick", "Mace",
    /* 0x18 */ "Morning Star", "Partisan", "Military Pick", "Awl Pike",
    /* 0x1C */ "Quarrel", "Ranseur", "Scimitar", "Spear",
    /* 0x20 */ "Spetum", "Quarter Staff", "Bastard Sword", "Broad Sword",
    /* 0x24 */ "Long Sword", "Short Sword", "Two-Handed Sword", "Trident",
    /* 0x28 */ "Voulge", "Composite Long Bow", "Composite Short Bow", "Long Bow",
    /* 0x2C */ "Short Bow", "Heavy Crossbow", "Light Crossbow", "Sling",
    /* 0x30 */ "Mail", "Armor", "Leather", "Padded",
    /* 0x34 */ "Studded", "Ring", "Scale", "Chain",
    /* 0x38 */ "Splint", "Banded", "Plate", "Shield",
    /* 0x3C */ "Woods", "Arrow", "", "",
    /* 0x40 */ "Potion", "Scroll", "Ring", "Rod",
    /* 0x44 */ "Stave", "Wand", "Jug", "Amulet",
    /* 0x48 */ "Dragon Breath", "Bag", "Defoliation", "Ice Storm",
    /* 0x4C */ "Book", "Boots", "Hornets Nest", "Bracers",
    /* 0x50 */ "Piercing", "Brooch", "Elfin Chain", "Wizardry",
    /* 0x54 */ "ac10", "Dexterity", "Fumbling", "Chime",
    /* 0x58 */ "Cloak", "Crystal", "Cube", "Cubic",
    /* 0x5C */ "The Dwarves", "Decanter", "Gloves", "Drums",
    /* 0x60 */ "Dust", "Thievery", "Hat", "Flask",
    /* 0x64 */ "Gauntlets", "Gem", "Girdle", "Helm",
    /* 0x68 */ "Horn", "Stupidity", "Incense", "Stone",
    /* 0x6C */ "Ioun Stone", "Javelin", "Jewel", "Ointment",
    /* 0x70 */ "Pale Blue", "Scarlet And", "Manual", "Incandescent",
    /* 0x74 */ "Deep Red", "Pink", "Mirror", "Necklace",
    /* 0x78 */ "And Green", "Blue", "Pearl", "Powerlessness",
    /* 0x7C */ "Vermin", "Pipes", "Hole", "Dragon Slayer",
    /* 0x80 */ "Robe", "Rope", "Frost Brand", "Berserker",
    /* 0x84 */ "Scarab", "Spade", "Sphere", "Blessed",
    /* 0x88 */ "Talisman", "Tome", "Trident", "Grimoire",
    /* 0x8C */ "Well", "Wings", "Vial", "Lantern",
    /* 0x90 */ "", "Flask of Oil", "10 ft. Pole", "50 ft. Rope",
    /* 0x94 */ "Iron", "Thf Prickly Tools", "Iron Rations", "Standard Rations",
    /* 0x98 */ "Holy Symbol", "Holy Water vial", "Unholy Water vial", "Barding",
    /* 0x9C */ "Dragon", "Lightning", "Saddle", "Staff",
    /* 0xA0 */ "Drow", "Wagon", "+1", "+2",
    /* 0xA4 */ "+3", "+4", "+5", "of",
    /* 0xA8 */ "Vulnerability", "Cloak", "Displacement", "Torches",
    /* 0xAC */ "Oil", "Speed", "Tapestry", "Spine",
    /* 0xB0 */ "Copper", "Silver", "Electrum", "Gold",
    /* 0xB4 */ "Platinum", "Ointment", "Keoghtum's", "Sheet",
    /* 0xB8 */ "Strength", "Healing", "Holding", "Extra",
    /* 0xBC */ "Gaseous Form", "Slipperiness", "Jewelled", "Flying",
    /* 0xC0 */ "Treasure Finding", "Fear", "Disappearance", "Statuette",
    /* 0xC4 */ "Fungus", "Chain", "Pendant", "Broach",
    /* 0xC8 */ "Of Seeking", "-1", "-2", "-3",
    /* 0xCC */ "Lightning Bolt", "Fire Resistance", "Magic Missiles", "Save",
    /* 0xD0 */ "Clrc Scroll", "MU Scroll", "With 1 Spell", "With 2 Spells",
    /* 0xD4 */ "With 3 Spells", "Prot. Scroll", "Jewelry", "Fine",
    /* 0xD8 */ "Huge", "Bone", "Brass", "Key",
    /* 0xDC */ "AC 2", "AC 6", "AC 4", "AC 3",
    /* 0xE0 */ "Of Prot.", "Paralyzation", "Ogre Power", "Invisibility",
    /* 0xE4 */ "Missiles", "Elvenkind", "Rotting", "Covered",
    /* 0xE8 */ "Efreeti", "Bottle", "Missile Attractor", "Of Maglubiyet",
    /* 0xEC */ "Secr Door & Trap Det", "Gd Dragon Control", "Feather Falling", "Giant Strength",
    /* 0xF0 */ "Restoring Level(s)", "Flame Tongue", "Fireballs", "Spiritual",
    /* 0xF4 */ "Boulder", "Diamond", "Emerald", "Opal",
    /* 0xF8 */ "Saphire", "Of Tyr", "Of Tempus", "Of Sune",
    /* 0xFC */ "Wooden", "+3 vs Undead", "Pass", "Cursed",
};

u8 item_hands_count(const Item *it)
{
    return item_data(it->type)->hands_count;
}

bool item_is_scroll(const Item *it)
{
    int slot = item_data(it->type)->slot;

    return slot >= ITEM_SLOT_11 && slot <= ITEM_SLOT_13;
}

bool item_is_ranged(const Item *it)
{
    return item_data(it->type)->range > 1;
}

bool item_scroll_learning(const Item *it, int i, int spell)
{
    int affect = (int)item_affect(it, i);

    return affect > 0x7f && (affect & 0x7f) == spell;
}

/* Appends s, stopping at dst_size - 1 characters. *len tracks the length so
 * repeated appends stay O(1) in it. */
static void name_append(char *dst, size_t dst_size, size_t *len, const char *s)
{
    while (*s != '\0' && *len + 1 < dst_size) {
        dst[(*len)++] = *s++;
    }
    dst[*len] = '\0';
}

char *item_generate_name(const Item *it, int hidden_names_flag,
                         char *dst, size_t dst_size)
{
    int    display_flags = 0;
    bool   plural_added  = false;
    size_t len           = 0;
    size_t start;

    if (dst_size == 0) {
        return dst;
    }
    dst[0] = '\0';

    /* Each name part has its own "not identified yet" bit, and the bits run in
     * the opposite order to the parts. */
    display_flags |= (it->namenum1 != 0 && (hidden_names_flag & 0x4) == 0) ? 0x1 : 0;
    display_flags |= (it->namenum2 != 0 && (hidden_names_flag & 0x2) == 0) ? 0x2 : 0;
    display_flags |= (it->namenum3 != 0 && (hidden_names_flag & 0x1) == 0) ? 0x4 : 0;

    /* Parts are named back to front: part 3 is the noun. */
    for (int part = 3; part >= 1; part--) {
        if (((display_flags >> (part - 1)) & 1) == 0) {
            continue;
        }

        name_append(dst, dst_size, &len, item_names[item_namenum(it, part)]);

        if (it->count < 2 || plural_added) {
            name_append(dst, dst_size, &len, " ");
        } else if ((1 << (part - 1)) == display_flags ||
                   (part == 1 && display_flags > 4 &&
                    it->type != ITEM_FLASK_OF_OIL) ||
                   (part == 2 && (display_flags & 1) == 0) ||
                   (part == 3 && it->type == ITEM_FLASK_OF_OIL) ||
                   (it->namenum3 != 0x87 && it->namenum3 != 0xb1 &&
                    (it->type == ITEM_ARROW ||
                     it->type == ITEM_QUARREL ||
                     it->type == ITEM_DART))) {
            name_append(dst, dst_size, &len, "s ");
            plural_added = true;
        } else {
            name_append(dst, dst_size, &len, " ");
        }
    }

    /* String.Trim(): every part contributed a trailing space, and an empty
     * leading part can contribute a leading one. */
    start = 0;
    while (start < len && dst[start] == ' ') {
        start++;
    }
    while (len > start && dst[len - 1] == ' ') {
        len--;
    }
    if (start > 0) {
        memmove(dst, dst + start, len - start);
    }
    dst[len - start] = '\0';

    return dst;
}
