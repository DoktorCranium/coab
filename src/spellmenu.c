/* spellmenu.c - Ported from engine/ovr023.cs (the spell-list half; see
 * spellmenu.h). */
#include <stdio.h>
#include <string.h>

#include "spellmenu.h"

#include "gbl.h"
#include "item.h"
#include "log.h"
#include "menu.h"
#include "prompt.h"
#include "spellcast.h"
#include "spelllist.h"
#include "spells.h"

/* gbl.max_spells, a const of 0x54 in Gbl.cs. Every id the printed spell lists
 * hand out fits inside it. */
#define MAX_SPELLS 0x54

/* gbl.scribeScrolls, unk_1AF18. The C# array was "01-0x30" - 0x30 entries, and
 * the code indexes it from 0. */
#define SCRIBE_SCROLLS_MAX 0x30

/* ovr023.LevelStrings. Row 0 is the empty placeholder that keeps the table
 * 1-based, spell levels being counted from 1. */
static const char *const LEVEL_STRINGS[] = {
    "",
    "1st Level",
    "2nd Level",
    "3rd Level",
    "4th Level",
    "5th Level",
    "6th Level",
    "7th Level",
    "8th Level",
    "9th Level"
};

/* gbl.spell_string_list, dword_1AE6C, and the two arrays that run alongside it.
 * memorize_spell_id[n] is the spell id of the n'th *pickable* row - a heading has
 * no entry of its own - and memorize_count[n] is how many copies of it the
 * character has, which is what the " (3)" suffix shows.
 *
 * A MenuList is about 10K, so this is a file-scope object rather than anything
 * that could land on a stack. */
static MenuList g_spell_string_list;
static u8       g_memorize_spell_id[MAX_SPELLS];
static int      g_memorize_count[MAX_SPELLS];

/* gbl.scribeScrolls / gbl.scribeScrollsCount, byte_1AFDC. Which scroll each
 * pickable row was found on, so that choosing a spell to scribe also says which
 * scroll to take it off. Borrowed pointers into the character's pack. */
static Item *g_scribe_scrolls[SCRIBE_SCROLLS_MAX];
static int   g_scribe_scrolls_count;

/* The level of a spell id, with id 0 - the unfilled slot - reading as level 0.
 * The C# indexed spellCastingTable directly and row 0 was a null there, so an
 * unfilled id would have thrown; the places that read one only compare it, never
 * take its level, so the case does not arise. This keeps it from logging. */
static int spell_level_of(u8 spell_id)
{
    const SpellEntry *entry;

    if (spell_id == 0) {
        return 0;
    }

    entry = spell_entry(spell_id);

    return (entry != NULL) ? entry->spell_level : 0;
}

/* ---------------------------------------------------------- can_learn_spell */

/* sub_5C01E */
bool spellmenu_can_learn_spell(int spell_id, const Player *player)
{
    const SpellEntry *entry;
    bool can_learn = false;

    spell_id &= 0x7f;

    if (player == NULL) {
        return false;
    }

    entry = spell_entry(spell_id);

    if (entry == NULL) {
        /* The C# indexed the table and would have thrown for id 0 - which is
         * "no spell", and is not learnable either way. */
        return false;
    }

    switch (entry->spell_class) {
    case SPELL_CLASS_CLERIC:
        if (player->stats.value[PSTAT_WIS].full > 8 &&
            (player_skill_level(player, SKILL_CLERIC) > 0 ||
             player_skill_level(player, SKILL_PALADIN) > 8)) {
            can_learn = true;
        }
        break;

    case SPELL_CLASS_DRUID:
        if (player->stats.value[PSTAT_WIS].full > 8 &&
            player_skill_level(player, SKILL_RANGER) > 6) {
            can_learn = true;
        }
        break;

    case SPELL_CLASS_MAGIC_USER:
        /* Faithfully reproduced, and it is a good deal weaker than it looks: the
         * four clauses after the race test are ORed, so any non-human, anybody
         * out of a fight, and anybody not wearing armour passes it whatever their
         * class. In practice an intelligence above 8 is the whole test. The
         * original's intent was presumably "a human magic-user may not cast in
         * armour"; what it wrote is this. */
        if (player->stats.value[PSTAT_INT].full > 8 &&
            (player->race != RACE_HUMAN ||
             player_ready_item((Player *)player, ITEM_SLOT_ARMOR) == NULL ||
             gbl.game_state != GAME_STATE_COMBAT ||
             player_skill_level(player, SKILL_RANGER) > 8 ||
             player_skill_level(player, SKILL_MAGIC_USER) > 0)) {
            can_learn = true;
        }
        break;

    case SPELL_CLASS_MONSTER:
        can_learn = false;
        break;

    default:
        break;
    }

    return can_learn;
}

/* ------------------------------------------------------- building a list */

/* sub_5C3ED. One row per spell, in the order the caller hands them over, with a
 * level heading dropped in whenever the level changes. This is the plain form -
 * the scroll lists use it - and it does not group or count duplicates. */
static void add_spell_to_list(u8 spell_id)
{
    u8 masked_id = (u8)(spell_id & 0x7f);
    const SpellEntry *entry = spell_entry(masked_id);
    int last_spell_level = 0;
    int index = 0;
    char text[MENU_ITEM_TEXT_MAX];

    if (entry == NULL) {
        log_warn("spell list: no spell 0x%x to list", masked_id);
        return;
    }

    if (g_spell_string_list.count > 0) {
        index = menu_list_count_selectable(&g_spell_string_list,
                                           g_spell_string_list.count);

        /* The C# read memorize_spell_id[index - 1] unguarded. A list holding
         * nothing but headings cannot happen - every heading is added with a
         * spell right behind it - so this only ever fires on a porting mistake. */
        if (index <= 0) {
            log_warn("spell list: %d rows and none of them a spell",
                     g_spell_string_list.count);
            return;
        }

        last_spell_level = spell_level_of(g_memorize_spell_id[index - 1]);
    }

    if (entry->spell_level != last_spell_level) {
        if (entry->spell_level >= 0 &&
            (size_t)entry->spell_level < COAB_ARRAY_LEN(LEVEL_STRINGS)) {
            menu_list_add_heading(&g_spell_string_list,
                                  LEVEL_STRINGS[entry->spell_level]);
        } else {
            log_warn("spell list: spell 0x%x is level %d", masked_id,
                     entry->spell_level);
            menu_list_add_heading(&g_spell_string_list, "");
        }
    }

    /* An asterisk marks a spell still being memorised. */
    snprintf(text, sizeof(text), " %c%s", (spell_id > 0x7f) ? '*' : ' ',
             spellcast_spell_name(masked_id));
    menu_list_add(&g_spell_string_list, text);

    if (index >= 0 && index < MAX_SPELLS) {
        g_memorize_spell_id[index] = masked_id;
    } else {
        log_warn("spell list: row %d is past the %d the id array holds", index,
                 MAX_SPELLS);
    }
}

/* sub_5C5B9. The grouping form: the list is kept in spell-level order and a
 * second copy of a spell already on it becomes a count against the row that is
 * there rather than a row of its own. */
static void add_spell_to_learning_list(int spell_id)
{
    u8 masked_id = (u8)(spell_id & 0x7f);
    const SpellEntry *entry = spell_entry(masked_id);
    int memorize_index;
    int sp_lvl;
    char text[MENU_ITEM_TEXT_MAX];

    if (entry == NULL) {
        log_warn("spell list: no spell 0x%x to list", masked_id);
        return;
    }

    sp_lvl = entry->spell_level;

    if (g_spell_string_list.count == 0) {
        memset(g_memorize_count, 0, sizeof(g_memorize_count));

        memorize_index = 0;
        g_memorize_count[memorize_index] = 1;
    } else {
        memorize_index = 0;

        /* Where the spell belongs: the first row of a higher level, or the row
         * this same spell already has. The C# stepped the index past headings
         * too without looking at their ids - which is harmless because this
         * function never adds one; BuildSpellList puts the headings in
         * afterwards. */
        for (int i = 0; i < g_spell_string_list.count; i++) {
            if (g_spell_string_list.item[i].heading == false) {
                if (memorize_index >= MAX_SPELLS) {
                    break;
                }

                if (spell_level_of(g_memorize_spell_id[memorize_index]) > sp_lvl ||
                    g_memorize_spell_id[memorize_index] == masked_id) {
                    break;
                }
            }
            memorize_index++;
        }

        if (memorize_index >= MAX_SPELLS) {
            log_warn("spell list: %d spells is more than the %d the arrays hold",
                     memorize_index, MAX_SPELLS);
            return;
        }

        if (g_memorize_spell_id[memorize_index] != masked_id) {
            /* Open a gap in the counts: everything from here on shifts up one
             * and the new row starts at a count of 1. */
            int insert_count = 1;

            for (int i = memorize_index; i < MAX_SPELLS; i++) {
                int tmp_count = g_memorize_count[i];
                g_memorize_count[i] = insert_count;
                insert_count = tmp_count;
            }
        } else {
            /* Already there: the row is taken out and put back with one more
             * against it. */
            menu_list_remove_at(&g_spell_string_list, memorize_index);
            g_memorize_count[memorize_index] += 1;
        }
    }

    if (g_memorize_count[memorize_index] > 1) {
        snprintf(text, sizeof(text), " %c%s (%d)",
                 spell_id > 0x7f ? '*' : ' ', spellcast_spell_name(masked_id),
                 g_memorize_count[memorize_index]);
    } else {
        snprintf(text, sizeof(text), " %c%s",
                 spell_id > 0x7f ? '*' : ' ', spellcast_spell_name(masked_id));
    }

    menu_list_insert(&g_spell_string_list, memorize_index, text);

    if (g_memorize_spell_id[memorize_index] != masked_id) {
        /* And a matching gap in the ids. */
        u8 insert_id = g_memorize_spell_id[memorize_index];

        g_memorize_spell_id[memorize_index] = masked_id;

        for (int i = memorize_index + 1; i < MAX_SPELLS; i++) {
            u8 tmp_id = g_memorize_spell_id[i];
            g_memorize_spell_id[i] = insert_id;
            insert_id = tmp_id;
        }
    }
}

/* sub_5C912. The spells on gbl.current_scroll, if their names can be read at
 * all: detect magic reveals them, and a cleric can read a scroll of their own
 * (the priest scrolls being the ones ITEM.DAT files under the quarrel slot).
 *
 * With learning set only the spells still marked as being memorised are listed,
 * which is the scribe list; without it, every spell on the scroll. */
static void scroll_spell_list(bool learning)
{
    Player *caster = gbl.selected_player;
    Item *scroll = gbl.current_scroll;
    const ItemData *data;

    if (caster == NULL || scroll == NULL) {
        return;
    }

    data = item_data(scroll->type);

    if (player_has_affect(caster, AFFECT_READ_MAGIC) == true ||
        ((caster->class_level[SKILL_CLERIC] > 0 ||
          caster->class_level_old[SKILL_CLERIC] > caster->multiclass_level) &&
         data != NULL && data->slot == ITEM_SLOT_QUARREL)) {
        scroll->hidden_names_flag = 0;
    }

    if (scroll->hidden_names_flag == 0) {
        for (int index = 1; index <= 3; index++) {
            int affect = (int)item_affect(scroll, index);

            if ((learning == true && affect > 0x80) ||
                (learning == false && affect > 0)) {
                add_spell_to_list((u8)affect);

                if (g_scribe_scrolls_count < SCRIBE_SCROLLS_MAX) {
                    g_scribe_scrolls[g_scribe_scrolls_count] = scroll;
                    g_scribe_scrolls_count++;
                } else {
                    log_warn("spell list: more than %d scribable spells",
                             SCRIBE_SCROLLS_MAX);
                }
            }
        }
    }
}

/* sub_5C9F4. Every scroll in the character's pack, in pack order. */
static void build_scroll_spell_lists(bool show_learning)
{
    Player *caster = gbl.selected_player;

    for (int i = 0; i < SCRIBE_SCROLLS_MAX; i++) {
        g_scribe_scrolls[i] = NULL;
    }

    g_scribe_scrolls_count = 0;

    if (caster == NULL) {
        return;
    }

    for (int i = 0; i < caster->item_count; i++) {
        Item *item = &caster->items[i];

        /* gbl.current_scroll is set for every item, scroll or not, because the
         * C# assigned it before the test. */
        gbl.current_scroll = item;

        if (item_is_scroll(item) == true) {
            scroll_spell_list(show_learning);
        }
    }
}

/* sub_5CA74 */
bool spellmenu_build_spell_list(SpellLoc location)
{
    Player *caster = gbl.selected_player;
    bool add_headings = true;

    menu_list_clear(&g_spell_string_list);

    for (int i = 0; i < MAX_SPELLS; i++) {
        g_memorize_spell_id[i] = 0;
    }

    switch (location) {
    case SPELL_LOC_MEMORY:
        if (caster != NULL) {
            for (int i = 0; i < caster->spell_list.count; i++) {
                const SpellItem *sp = &caster->spell_list.items[i];

                if (sp->learning == false &&
                    spellmenu_can_learn_spell(sp->id, caster)) {
                    add_spell_to_learning_list(sp->id);
                }
            }
        }
        break;

    case SPELL_LOC_MEMORIZE:
        if (caster != NULL) {
            for (int i = 0; i < caster->spell_list.count; i++) {
                const SpellItem *sp = &caster->spell_list.items[i];

                if (sp->learning == true &&
                    spellmenu_can_learn_spell(sp->id, caster)) {
                    add_spell_to_learning_list(sp->id);
                }
            }
        }
        break;

    case SPELL_LOC_GRIMOIRE:
        /* Enum.GetValues walks the ids in numeric order, which is what keeps the
         * grimoire in spell-id order under the level headings. The Spells enum
         * runs 1 to 0x64 without gaps; the table's row 0x65 is not one of its
         * members and so was never offered, which is why this stops short of
         * SPELL_CASTING_TABLE_COUNT. */
        for (int id = SPELL_BLESS; id <= SPELL_BESTOW_CURSE_MU; id++) {
            if (caster != NULL &&
                player_knows_spell(caster, (Spells)id) &&
                spellmenu_can_learn_spell(id, caster)) {
                add_spell_to_learning_list(id);
            }
        }
        break;

    case SPELL_LOC_SCROLL:
        scroll_spell_list(false);
        add_headings = false;
        break;

    case SPELL_LOC_SCROLLS:
        build_scroll_spell_lists(false);
        add_headings = false;
        break;

    case SPELL_LOC_SCRIBE:
        build_scroll_spell_lists(true);
        add_headings = false;
        break;

    case SPELL_LOC_CHOOSE:
        for (int id = SPELL_BLESS; id <= SPELL_BESTOW_CURSE_MU; id++) {
            const SpellEntry *entry = spell_entry(id);
            int sp_lvl;
            int sp_class;

            if (entry == NULL) {
                continue;
            }

            sp_lvl = entry->spell_level;
            sp_class = (int)entry->spell_class;

            if (sp_lvl > 5 || sp_class >= SPELL_CLASS_MONSTER) {
                /* skip this spell */
            } else if (sp_lvl < 1 || sp_class < 0) {
                /* spell_cast_count is [3][5] and the C# indexed it with
                 * sp_lvl - 1 unguarded. No castable spell is below level 1, so
                 * this says so rather than reading behind the array. */
                log_warn("spell list: spell 0x%x is class %d level %d", id,
                         sp_class, sp_lvl);
            } else if (caster != NULL &&
                       caster->spell_cast_count[sp_class][sp_lvl - 1] > 0 &&
                       spellmenu_can_learn_spell(id, caster) == true &&
                       player_knows_spell(caster, (Spells)id) == false) {
                add_spell_to_learning_list(id);
            }
        }
        break;

    default:
        break;
    }

    if (g_spell_string_list.count > 0) {
        if (add_headings == true) {
            /* The rows are in level order by now but carry no headings. Walk
             * them working out where the level changes, then put the headings in
             * afterwards - the C# queued them for exactly that reason, an insert
             * during the walk having moved everything after it. The recorded
             * position already counts the headings inserted before it, which is
             * what the extra insert++ does. */
            struct { int at; int level; } inserts[MENU_LIST_MAX];
            int insert_count = 0;
            int spell_lvl = 0;
            int insert = 0;

            for (int idx = 0; idx < g_spell_string_list.count; idx++) {
                int last_lvl = spell_lvl;

                if (idx < MAX_SPELLS && g_memorize_spell_id[idx] != 0) {
                    spell_lvl = spell_level_of(g_memorize_spell_id[idx]);
                }

                if (spell_lvl > last_lvl) {
                    inserts[insert_count].at = insert;
                    inserts[insert_count].level = spell_lvl;
                    insert_count++;
                    insert++;
                }

                insert++;
            }

            for (int i = 0; i < insert_count; i++) {
                int level = inserts[i].level;
                const char *heading =
                    (level >= 0 &&
                     (size_t)level < COAB_ARRAY_LEN(LEVEL_STRINGS))
                        ? LEVEL_STRINGS[level] : "";

                menu_list_insert_heading(&g_spell_string_list, inserts[i].at,
                                         heading);
            }
        }

        return true;
    }

    return false;
}

/* ------------------------------------------------------------- the menu */

/* asc_5C1D1, the keys that end the list: Escape, 'C'ast, 'E'xit, 'L'earn,
 * 'M'emorize and 'S'cribe. Anything else - a cursor key, Next, Prev - leaves
 * sl_select_item to keep running. */
static const int MENU_END_KEYS[] = { 0, 'C', 'E', 'L', 'M', 'S' };

/* unk_5C1F1: the two that mean "nothing was chosen". */
static const int MENU_CANCEL_KEYS[] = { 0, 'E' };

static bool key_in(const int *keys, size_t count, int key)
{
    for (size_t i = 0; i < count; i++) {
        if (keys[i] == key) {
            return true;
        }
    }

    return false;
}

u8 spellmenu_menu(int *index, SpellSource source)
{
    const char *text;
    const char *prompt_text;
    int end_y;
    bool show_exit;
    bool redraw_menu_items = false;
    MenuItem *selected = NULL;
    char input_key;
    u8 spell_id;
    int local_index = 0;

    if (index == NULL) {
        index = &local_index;
    }

    switch (source) {
    case SPELL_SOURCE_CAST:     text = "Cast";     break;
    case SPELL_SOURCE_MEMORIZE: text = "Memorize"; break;
    case SPELL_SOURCE_SCRIBE:   text = "Scribe";   break;
    case SPELL_SOURCE_LEARN:    text = "Learn";    break;
    default:                    text = "";         break;
    }

    prompt_text = (text[0] != '\0') ? "Choose Spell: " : "";

    /* Memorising has the list of what is already memorised under it, so its own
     * list stops eight rows higher up the screen. */
    end_y = (source == SPELL_SOURCE_MEMORIZE) ? 0x0f : 0x16;

    show_exit = (source != SPELL_SOURCE_LEARN);

    if (*index < 0) {
        redraw_menu_items = true;
        *index = 0;
    }

    if (source == SPELL_SOURCE_LEARN || source == SPELL_SOURCE_CAST) {
        redraw_menu_items = true;
    }

    do {
        input_key = prompt_select_item(&selected, index, &redraw_menu_items,
                                       show_exit, &g_spell_string_list,
                                       end_y, 0x26, 5, 1,
                                       GBL_DEFAULT_MENU_COLORS, text,
                                       prompt_text);
    } while (key_in(MENU_END_KEYS, COAB_ARRAY_LEN(MENU_END_KEYS),
                    (unsigned char)input_key) == false);

    if (key_in(MENU_CANCEL_KEYS, COAB_ARRAY_LEN(MENU_CANCEL_KEYS),
               (unsigned char)input_key) == true) {
        spell_id = 0;
    } else {
        /* Which pickable row was chosen: the headings before it do not count,
         * and it is the pickable position that indexes the id array. */
        int selected_index = 0;

        if (selected != NULL) {
            selected_index =
                menu_list_count_selectable(&g_spell_string_list,
                                           (int)(selected -
                                                 g_spell_string_list.item));
        }

        if (selected_index >= 0 && selected_index < MAX_SPELLS) {
            spell_id = g_memorize_spell_id[selected_index];

            if (source == SPELL_SOURCE_SCRIBE) {
                gbl.current_scroll =
                    (selected_index < SCRIBE_SCROLLS_MAX)
                        ? g_scribe_scrolls[selected_index] : NULL;
            }
        } else {
            log_warn("spell menu: row %d is past the %d the id array holds",
                     selected_index, MAX_SPELLS);
            spell_id = 0;
        }
    }

    menu_list_clear(&g_spell_string_list);

    return spell_id;
}
