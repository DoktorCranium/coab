/* menu.h - the scrolling list entries the menus are built from.
 * Ported from Classes/MenuItem.cs, plus the List<MenuItem> handling the overlays
 * do with them (ovr027.sl_select_item and its callers).
 *
 * A heading entry is drawn but cannot be picked, which is how spell lists show a
 * level line between groups of spells.
 */
#ifndef COAB_MENU_H
#define COAB_MENU_H

#include "coab.h"

struct Item;

/* The DOS record put the text at 0x00 and the heading flag at 0x29, so 41 bytes
 * of text. The C# used a string of any length; the longest text anything builds
 * is an item name with a charge count, which fits well inside this. */
#define MENU_ITEM_TEXT_MAX 64

typedef struct {
    char         text[MENU_ITEM_TEXT_MAX];
    bool         heading;
    struct Item *item;      /* borrowed, may be NULL */
} MenuItem;

/* Text longer than the buffer is truncated and logged rather than silently cut,
 * because a clipped menu line is a visible bug and worth finding. */
void menu_item_set(MenuItem *mi, const char *text, bool heading,
                   struct Item *item);

/* ------------------------------------------------------------------ lists */

/* Long enough for the biggest list the game builds: all 0x54 memorisable spells
 * plus a heading for each spell level. */
#define MENU_LIST_MAX 128

typedef struct {
    MenuItem item[MENU_LIST_MAX];
    int      count;
} MenuList;

void menu_list_clear(MenuList *l);

/* Adds at the end. Returns false, having logged, when the list is full; the C#
 * List<> grew instead, so a full list here means MENU_LIST_MAX is too small. */
bool menu_list_add(MenuList *l, const char *text);
bool menu_list_add_heading(MenuList *l, const char *text);
bool menu_list_add_item(MenuList *l, const char *text, struct Item *item);

/* List<>.Insert: shifts everything from at onwards up one. at == count appends. */
bool menu_list_insert(MenuList *l, int at, const char *text);
bool menu_list_insert_heading(MenuList *l, int at, const char *text);

/* List<>.RemoveAt. */
void menu_list_remove_at(MenuList *l, int at);

/* NULL for an index outside the list, which is what getStringListEntry's callers
 * expect when a menu has scrolled past its last entry. */
MenuItem *menu_list_get(MenuList *l, int at);

/* How many entries before at are pickable, i.e. not headings. The menus track
 * their selection by pickable position, not by list index. */
int menu_list_count_selectable(const MenuList *l, int before);

#endif /* COAB_MENU_H */
