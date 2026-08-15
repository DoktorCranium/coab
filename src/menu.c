/* menu.c - Ported from Classes/MenuItem.cs. */
#include <string.h>

#include "menu.h"

#include "log.h"

void menu_item_set(MenuItem *mi, const char *text, bool heading,
                   struct Item *item)
{
    size_t n;

    memset(mi, 0, sizeof(*mi));

    mi->heading = heading;
    mi->item    = item;

    if (text == NULL) {
        return;
    }
    n = strlen(text);
    if (n >= MENU_ITEM_TEXT_MAX) {
        log_warn("menu item: '%s' is %zu characters, truncating to %d",
                 text, n, MENU_ITEM_TEXT_MAX - 1);
        n = MENU_ITEM_TEXT_MAX - 1;
    }
    memcpy(mi->text, text, n);
    mi->text[n] = '\0';
}

/* ------------------------------------------------------------------ lists */

void menu_list_clear(MenuList *l)
{
    /* Only the count needs resetting, but the entries hold borrowed item
     * pointers and a stale one is worth not keeping around. */
    memset(l, 0, sizeof(*l));
}

static bool room_for_one(const MenuList *l)
{
    if (l->count < MENU_LIST_MAX) {
        return true;
    }
    log_warn("menu list: full at %d entries", MENU_LIST_MAX);

    return false;
}

bool menu_list_add(MenuList *l, const char *text)
{
    return menu_list_add_item(l, text, NULL);
}

bool menu_list_add_heading(MenuList *l, const char *text)
{
    if (!room_for_one(l)) {
        return false;
    }
    menu_item_set(&l->item[l->count++], text, true, NULL);

    return true;
}

bool menu_list_add_item(MenuList *l, const char *text, struct Item *item)
{
    if (!room_for_one(l)) {
        return false;
    }
    menu_item_set(&l->item[l->count++], text, false, item);

    return true;
}

static bool insert_at(MenuList *l, int at, const char *text, bool heading)
{
    if (at < 0 || at > l->count) {
        log_warn("menu list: cannot insert at %d in a list of %d",
                 at, l->count);
        return false;
    }
    if (!room_for_one(l)) {
        return false;
    }
    memmove(&l->item[at + 1], &l->item[at],
            (size_t)(l->count - at) * sizeof(l->item[0]));
    l->count++;

    menu_item_set(&l->item[at], text, heading, NULL);

    return true;
}

bool menu_list_insert(MenuList *l, int at, const char *text)
{
    return insert_at(l, at, text, false);
}

bool menu_list_insert_heading(MenuList *l, int at, const char *text)
{
    return insert_at(l, at, text, true);
}

void menu_list_remove_at(MenuList *l, int at)
{
    if (at < 0 || at >= l->count) {
        log_warn("menu list: cannot remove %d from a list of %d", at, l->count);
        return;
    }
    memmove(&l->item[at], &l->item[at + 1],
            (size_t)(l->count - at - 1) * sizeof(l->item[0]));
    l->count--;

    memset(&l->item[l->count], 0, sizeof(l->item[0]));
}

MenuItem *menu_list_get(MenuList *l, int at)
{
    if (at < 0 || at >= l->count) {
        return NULL;
    }
    return &l->item[at];
}

int menu_list_count_selectable(const MenuList *l, int before)
{
    int n = 0;

    if (before > l->count) {
        before = l->count;
    }
    for (int i = 0; i < before; i++) {
        if (!l->item[i].heading) {
            n++;
        }
    }
    return n;
}
