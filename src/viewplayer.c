/* viewplayer.c - Ported from engine/ovr020.cs. See viewplayer.h.
 *
 * The routines are in the order ovr020.cs has them, so the two files read
 * against each other. */
#include "viewplayer.h"

#include <stdio.h>
#include <string.h>

#include "affecttab.h"
#include "character.h"
#include "cheats.h"
#include "classcalc.h"
#include "combat.h"
#include "combatmap.h"
#include "effect.h"
#include "frames.h"
#include "gbl.h"
#include "input.h"
#include "log.h"
#include "menu.h"
#include "money.h"
#include "prompt.h"
#include "spellcast.h"
#include "spelllist.h"
#include "spellmenu.h"
#include "spells.h"
#include "text.h"
#include "treasure.h"

/* ----------------------------------------------------------- the menu text */

/* The C# built its prompts by adding to a string, and each word carries its own
 * separator. This appends one and keeps the buffer terminated; a prompt that
 * would not fit says so, where the C#'s string simply grew. combatloop.c has the
 * same helper for the same reason. */
static void menu_append(char *dst, size_t dst_size, const char *word)
{
    size_t len = strlen(dst);
    size_t add = strlen(word);

    if (len + add + 1 > dst_size) {
        log_warn("character sheet: no room in the menu prompt for \"%s\"", word);
        return;
    }

    memcpy(dst + len, word, add + 1);
}

/* ------------------------------------------------------------- the tables */

/* ovr020.sexString, raceString, alignmentString and moneyString. classString and
 * statusString are in player.c; see the note at the end of viewplayer.h. */
static const char *const sex_string[] = { "Male", "Female" };

static const char *const race_string[] = {
    "Monster", "Dwarf", "Elf", "Gnome", "Half-Elf", "Halfling", "Half-Orc",
    "Human"
};

static const char *const alignment_string[] = {
    "Lawful Good", "Lawful Neutral", "Lawful Evil",
    "Neutral Good", "True Neutral", "Neutral Evil",
    "Chaotic Good", "Chaotic Neutral", "Chaotic Evil"
};

static const char *const stat_short_string[] = {
    "STR ", "INT ", "WIS ", "DEX ", "CON ", "CHA "
};

/* The same seven words money_names has, and the coin menus below build their
 * lines from this one rather than from Money.names, as the C# did. They are
 * identical; treasure_money_index_from_string reads either back. */
static const char *const money_string[] = {
    "Copper", "Silver", "Electrum", "Gold", "Platinum", "Gems", "Jewelry"
};

const char *viewplayer_sex_name(int sex)
{
    if (sex < 0 || (size_t)sex >= COAB_ARRAY_LEN(sex_string)) {
        log_warn("character sheet: sex %d is not one of the two", sex);
        return "";
    }
    return sex_string[sex];
}

const char *viewplayer_race_name(int race)
{
    if (race < 0 || (size_t)race >= COAB_ARRAY_LEN(race_string)) {
        log_warn("character sheet: race %d is outside the table", race);
        return "";
    }
    return race_string[race];
}

const char *viewplayer_alignment_name(int alignment)
{
    if (alignment < 0 ||
        (size_t)alignment >= COAB_ARRAY_LEN(alignment_string)) {
        log_warn("character sheet: alignment %d is outside the table",
                 alignment);
        return "";
    }
    return alignment_string[alignment];
}

/* --------------------------------------------------------- the whole sheet */

void viewplayer_display_full(Player *player)
{
    int x_col = 1;
    int y_col;
    char text[64];
    char levels[48];
    bool display_slash = false;
    const char *word;

    if (player == NULL) {
        log_warn("character sheet: nobody to show");
        return;
    }

    frames_draw_outer();

    character_display_name(false, 1, 1, player);

    if (player->control_morale >= CONTROL_NPC_BASE) {
        text_display_string("(NPC)", 0, 10, 1, (int)strlen(player->name) + 3);
    }

    /* Sex, race and age run along row 3, each starting a column after the last
     * one ends. The C# stepped x_col by a byte cast of the length plus one,
     * which for these words is the length plus one. */
    word = viewplayer_sex_name(player->sex);
    text_display_string(word, 0, 15, 3, x_col);
    x_col += (int)strlen(word) + 1;

    word = viewplayer_race_name(player->race);
    text_display_string(word, 0, 15, 3, x_col);
    x_col += (int)strlen(word) + 1;

    snprintf(text, sizeof(text), "Age %d", (int)player->age);
    text_display_string(text, 0, 15, 3, x_col);

    text_display_string(viewplayer_alignment_name(player->alignment),
                        0, 15, 4, 1);
    text_display_string(player_class_name(player->cls), 0, 15, 5, 1);

    for (int stat = 0; stat < STAT_COUNT; stat++) {
        text_display_string(stat_short_string[stat], 0, 10, stat + 7, 1);
        viewplayer_display_stat(false, stat);
    }

    viewplayer_display_money();
    text_display_string("Level", 0, 15, 15, 1);

    /* One number per class the character holds, slash-separated: the level they
     * have in it now, plus whatever they had in it before dual-classing once the
     * new class has caught up. A class abandoned at a level the new one has not
     * reached yet is left out entirely. */
    levels[0] = '\0';

    for (int class_idx = 0; class_idx <= 7; class_idx++) {
        u8 old_level = player->class_level_old[class_idx];

        if (player->class_level[class_idx] > 0 ||
            (old_level < player_dual_class_current_level(player) &&
             old_level > 0)) {
            char part[8];

            if (display_slash) {
                menu_append(levels, sizeof(levels), "/");
            }

            snprintf(part, sizeof(part), "%d",
                     player->class_level[class_idx] + old_level);
            menu_append(levels, sizeof(levels), part);

            display_slash = true;
        }
    }

    text_display_string(levels, 0, 15, 15, 7);

    snprintf(text, sizeof(text), "Exp %d", (int)player->exp);
    text_display_string(text, 0, 15, 15, 17);

    viewplayer_display_stats01();

    y_col = 20;

    if (player_primary_weapon(player) != NULL) {
        text_display_string("Weapon", 0, 15, y_col, 1);
        character_item_display_name_build(true, false, y_col, 8,
                                          player_primary_weapon(player));
    }

    y_col++;
    if (player_armor(player) != NULL) {
        text_display_string("Armor", 0, 15, y_col, 2);
        character_item_display_name_build(true, false, y_col, 8,
                                          player_armor(player));
    }

    y_col++;

    text_display_string("Status", 0, 15, y_col, 1);
    text_display_string(player_health_status_name(player->health_status),
                        0, 10, y_col, 8);
}

void viewplayer_display_money(void)
{
    int y_col = 7;

    frames_clear_area(14, 26, 7, 12);

    if (gbl.selected_player == NULL) {
        log_warn("character sheet: nobody to count the money of");
        return;
    }

    /* Largest coin first, and a kind nobody has is left out rather than shown as
     * zero - which is why the row is stepped only when something was drawn. */
    for (int coin_type = MONEY_KINDS - 1; coin_type >= 0; coin_type--) {
        int count = money_get(&gbl.selected_player->money, (MoneyKind)coin_type);

        if (count > 0) {
            char line[32];

            snprintf(line, sizeof(line), "%8s %d", money_string[coin_type],
                     count);
            text_display_string(line, 0, 10, y_col, 12);

            y_col++;
        }
    }
}

void viewplayer_display_stats01(void)
{
    Player *player = gbl.selected_player;
    int y_col = 0x11;
    int x_col = 8;
    int movement;
    char text[32];
    char bonus[8];

    if (player == NULL) {
        log_warn("character sheet: nobody to work the combat figures out for");
        return;
    }

    character_recalc_values(player);

    text_display_string("AC    ", 0, 15, y_col, 1);
    snprintf(text, sizeof(text), "%d", player_display_ac(player));
    text_display_string(text, 0, 10, y_col, 4);

    text_display_string("HP    ", 0, 15, y_col + 1, 1);
    character_display_hp(false, y_col + 1, 4, player);

    text_display_string("THAC0   ", 0, 15, y_col, x_col + 1);
    snprintf(text, sizeof(text), "%d", 0x3c - player->hit_bonus);
    text_display_string(text, 0, 10, y_col, x_col + 7);

    /* "1d8", "1d8+2", "2d4-1": the bonus gets a sign only when it is positive,
     * because a negative one prints its own. */
    bonus[0] = '\0';
    if (player->attack1_damage_bonus != 0) {
        snprintf(bonus, sizeof(bonus), "%d",
                 (int)player->attack1_damage_bonus);
    }

    snprintf(text, sizeof(text), "%dd%d%s%s",
             (int)player->attack1_dice_count, (int)player->attack1_dice_size,
             player->attack1_damage_bonus > 0 ? "+" : "", bonus);

    text_display_string("Damage  ", 0, 15, y_col + 1, x_col);
    text_display_string(text, 0, 10, y_col + 1, x_col + 7);

    x_col = 0x16;
    text_display_string("Encumbrance  ", 0, 15, y_col, x_col);
    snprintf(text, sizeof(text), "%d", (int)player->weight);
    text_display_string(text, 0, 10, y_col, x_col + 12);

    /* Doubled while slowed and halved while hasted, which is the wrong way
     * round; see the note in viewplayer.h. */
    movement = player->movement;

    if (player_has_affect(player, AFFECT_SLOW) == true) {
        movement *= 2;
    }

    if (player_has_affect(player, AFFECT_HASTE) == true) {
        movement /= 2;
    }

    text_display_string("Movement ", 0, 15, y_col + 1, x_col + 3);
    snprintf(text, sizeof(text), "%d", movement);
    text_display_string(text, 0, 10, y_col + 1, x_col + 12);
}

void viewplayer_display_stat(bool highlighted, int stat_index)
{
    int color = highlighted ? 0x0d : 0x0a;
    int col_x = 5;
    char text[16];
    Player *player = gbl.selected_player;

    if (player == NULL) {
        log_warn("character sheet: nobody to show a stat of");
        return;
    }

    if (stat_index < 0 || stat_index >= STAT_COUNT) {
        log_warn("character sheet: stat %d is not one of the six", stat_index);
        return;
    }

    frames_clear_area(stat_index + 7, 0x0b, stat_index + 7, col_x);

    /* A single digit is pushed one column right, so the two-digit case stays
     * left-aligned and both read as a column of numbers. */
    if (player->stats.value[stat_index].full < 10) {
        col_x++;
    }

    snprintf(text, sizeof(text), "%d", player->stats.value[stat_index].full);
    text_display_string(text, 0, color, stat_index + 7, col_x);

    /* Exceptional strength: 18(01) through 18(00), where 00 means a full 100. */
    if (stat_index == STAT_STR &&
        player->stats.value[PSTAT_STR].full == 18 &&
        player->stats.value[PSTAT_STR00].cur > 0) {
        int str_00 = player->stats.value[PSTAT_STR00].cur;
        char percentile[12];

        if (str_00 == 100) {
            snprintf(percentile, sizeof(percentile), "00");
        } else if (str_00 < 10) {
            snprintf(percentile, sizeof(percentile), "0%d", str_00);
        } else {
            snprintf(percentile, sizeof(percentile), "%d", str_00);
        }

        snprintf(text, sizeof(text), "(%s)", percentile);
        text_display_string(text, 0, color, 7, 7);
    }
}

/* ---------------------------------------------------------- the sheet's menu */

bool viewplayer_view_player(void)
{
    char input_key = ' ';
    bool turn_used = false;

    if (gbl.selected_player == NULL) {
        log_warn("character sheet: nobody to look at");
        return false;
    }

    if (gbl.game_state == GAME_STATE_COMBAT) {
        combatmap_color_0_8_normal();
    }

    gbl.trade_with = gbl.selected_player;

    viewplayer_display_full(gbl.selected_player);

    /* unk_54B03 = { 0, 'E' }: Escape or Exit leaves. */
    while (input_key != '\0' && input_key != 'E' && turn_used == false &&
           input_quit_requested() == false) {
        char text[64];
        bool has_spells = spell_list_has_spells(&gbl.selected_player->spell_list);
        bool has_money = money_any(&gbl.selected_player->money);
        int index = -1;

        text[0] = '\0';

        if (gbl.selected_player->item_count > 0) {
            menu_append(text, sizeof(text), "Items ");
        }

        if (has_spells == true) {
            menu_append(text, sizeof(text), "Spells ");
        }

        /* An NPC who is in the fight and still on their feet keeps their own
         * things; anyone else may be traded with, and only out of combat. */
        if (gbl.selected_player->control_morale < CONTROL_NPC_BASE ||
            gbl.selected_player->in_combat == false ||
            gbl.selected_player->health_status == STATUS_ANIMATED) {
            if (has_money && gbl.game_state != GAME_STATE_COMBAT) {
                menu_append(text, sizeof(text), "Trade ");
            }
        }

        if (has_money) {
            menu_append(text, sizeof(text), "Drop ");
        }

        if (viewplayer_can_cast_heal(gbl.selected_player) == true) {
            menu_append(text, sizeof(text), "Heal ");
        }

        if (viewplayer_can_cast_cure_diseases(gbl.selected_player) == true) {
            menu_append(text, sizeof(text), "Cure ");
        }

        menu_append(text, sizeof(text), "Exit");

        input_key = prompt_display_input_simple(false, 0,
                                               GBL_DEFAULT_MENU_COLORS,
                                               text, "");

        switch (input_key) {
        case 'I':
            viewplayer_items_menu(&turn_used);
            break;

        case 'S':
            /* Source 0 is not one of the four SpellSource values. The C# passed
             * a bare 0 here, which is neither Cast nor Memorize, and spell_menu2
             * only ever compares it against Cast - so the list comes up as the
             * plain "look at what is memorised" case. */
            viewplayer_spell_menu2(&has_spells, &index, (SpellSource)0,
                                   SPELL_LOC_MEMORY);
            break;

        case 'T':
            viewplayer_trade_coin();
            break;

        case 'D':
            viewplayer_drop_coin();
            viewplayer_display_money();
            break;

        case 'H':
            viewplayer_paladin_heal(gbl.selected_player);
            break;

        case 'C':
            viewplayer_paladin_cure_disease(gbl.selected_player);
            break;

        default:
            break;
        }

        /* asc_54B50 = { 'I', 'S', 'T' }: the three that draw over the sheet. */
        if (turn_used == false &&
            (input_key == 'I' || input_key == 'S' || input_key == 'T')) {
            viewplayer_display_full(gbl.selected_player);
        }
    }

    if (gbl.game_state == GAME_STATE_COMBAT) {
        combatmap_color_0_8_inverse();
    }
    character_load_pic();

    return turn_used;
}

/* sub_54EC1 */
bool viewplayer_can_sell_drop_trade_item(Item *item)
{
    bool can_part_with_it = false;

    if (item == NULL) {
        log_warn("character sheet: nothing to part with");
        return false;
    }

    if (item->readied) {
        character_print_message("Must be unreadied");
    } else if (item_is_scroll(item) == false) {
        can_part_with_it = true;
    } else if (item->affect_1 > 0x7f || item->affect_2 > 0x7f ||
               item->affect_3 > 0x7f) {
        /* The high bit on a name part means the spell there has not been scribed
         * off the scroll yet, so letting the scroll go loses it. */
        if (gbl.selected_player != NULL) {
            /* Row 15 is where the C# put the name, while the sentence that
             * follows it starts on row 0x15 - so the two are six rows apart on
             * screen. The original had it this way. */
            character_display_name(false, 15, 1, gbl.selected_player);
            gbl.text_x_col = (int)strlen(gbl.selected_player->name) + 2;
        }
        gbl.text_y_col = 0x15;

        text_press_any_key_region(" was going to scribe from that scroll", false,
                                  14, TEXT_REGION_NORMAL2);

        if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS,
                          "is it Okay to lose it? ") == 'Y') {
            can_part_with_it = true;
        }
    } else {
        can_part_with_it = true;
    }

    frames_clear_region(TEXT_REGION_NORMAL2);

    return can_part_with_it;
}

/* sub_550A6 */
void viewplayer_item_display_stats(const Item *item)
{
    /* The C# printed the ItemType and Affects enum members by name, which it
     * could and the DOS build could not: the original showed the numbers, and so
     * does this. The three flags come out as the C#'s "True"/"False". */
    static const char *const labels[] = {
        "itemptr:      ", "namenum(1):   ", "namenum(2):   ", "namenum(3):   ",
        "plus:         ", "plussave:     ", "ready:        ", "identified:   ",
        "cursed:       ", "value:        ", "special(1):   ", "special(2):   ",
        "special(3):   ", "dice large:   ", "sides large:  "
    };
    char values[COAB_ARRAY_LEN(labels)][16];
    const ItemData *data;

    if (item == NULL) {
        log_warn("character sheet: no item to take apart");
        return;
    }

    data = item_data(item->type);

    snprintf(values[0],  sizeof(values[0]),  "%d", item->type);
    snprintf(values[1],  sizeof(values[1]),  "%d", item->namenum1);
    snprintf(values[2],  sizeof(values[2]),  "%d", item->namenum2);
    snprintf(values[3],  sizeof(values[3]),  "%d", item->namenum3);
    snprintf(values[4],  sizeof(values[4]),  "%d", item->plus);
    snprintf(values[5],  sizeof(values[5]),  "%d", (int)item->plus_save);
    snprintf(values[6],  sizeof(values[6]),  "%s",
             item->readied ? "True" : "False");
    snprintf(values[7],  sizeof(values[7]),  "%d",
             (int)item->hidden_names_flag);
    snprintf(values[8],  sizeof(values[8]),  "%s",
             item->cursed ? "True" : "False");
    snprintf(values[9],  sizeof(values[9]),  "%d", (int)item->value);
    snprintf(values[10], sizeof(values[10]), "%d", item->affect_1);
    snprintf(values[11], sizeof(values[11]), "%d", item->affect_2);
    snprintf(values[12], sizeof(values[12]), "%d", item->affect_3);
    snprintf(values[13], sizeof(values[13]), "%d", (int)data->dice_count_large);
    snprintf(values[14], sizeof(values[14]), "%d", (int)data->dice_size_large);

    frames_draw_outer();

    for (size_t i = 0; i < COAB_ARRAY_LEN(labels); i++) {
        text_display_string(labels[i], 0, 10, (int)i + 1, 1);
        text_display_string(values[i], 0, 10, (int)i + 1, 0x14);
    }

    text_display_and_pause("press a key", 10);
}

/* use_item */
void viewplayer_items_menu(bool *out_turn_used)
{
    /* The list is rebuilt every time round the loop and a MenuList is 10K, so it
     * is not on the stack. Nothing here is re-entered: the item menu cannot open
     * another one. */
    static MenuList list;
    Player *player = gbl.selected_player;
    char input_key = ' ';
    bool redraw_items = true;
    bool redraw_player = true;
    bool turn_used = false;
    int dummy_index = 0;

    if (out_turn_used != NULL) {
        turn_used = *out_turn_used;
    }

    if (player == NULL) {
        log_warn("character sheet: nobody's pack to open");
        return;
    }

    /* unk_554EE = { 0, 'E' }, and an empty pack closes the list too. */
    while (input_key != '\0' && input_key != 'E' && turn_used == false &&
           player->item_count > 0 && input_quit_requested() == false) {
        int old_item_count = player->item_count;

        /* Always true here - the loop above tested it - and kept so that the
         * count comparison at the bottom reads as the C# does. */
        if (player->item_count > 0) {
            char text[64];
            MenuItem *chosen = NULL;
            Item *curr_item;

            strcpy(text, "Ready");

            if (cheats.view_item_stats) {
                menu_append(text, sizeof(text), " View");
            }

            /* Using something needs the character to be in the fight, the map to
             * allow it, and either a game state where there is time to spare or
             * an unspent action in a fight. */
            if (player->in_combat == true &&
                gbl.area_ptr->field_1CA == 0 &&
                (gbl.game_state == GAME_STATE_CAMPING ||
                 gbl.game_state == GAME_STATE_WILDERNESS_MAP ||
                 gbl.game_state == GAME_STATE_DUNGEON_MAP ||
                 gbl.game_state == GAME_STATE_COMBAT ||
                 (player->actions != NULL &&
                  player->actions->can_use == true))) {
                menu_append(text, sizeof(text), " Use");
            }

            if (player->control_morale < CONTROL_NPC_BASE ||
                player->in_combat == false ||
                player->health_status == STATUS_ANIMATED) {
                if (gbl.game_state != GAME_STATE_COMBAT) {
                    menu_append(text, sizeof(text), " Trade");
                }
            }

            menu_append(text, sizeof(text), " Drop");

            /* Halving needs somewhere to put the other half. */
            if (player->item_count < PLAYER_MAX_ITEMS) {
                menu_append(text, sizeof(text), " Halve");
            }

            menu_append(text, sizeof(text), " Join");

            if (gbl.game_state == GAME_STATE_SHOP) {
                if (player->control_morale < CONTROL_NPC_BASE ||
                    player->in_combat == false ||
                    player->health_status == STATUS_ANIMATED) {
                    menu_append(text, sizeof(text), " Sell");
                }

                menu_append(text, sizeof(text), " Id");
            }

            menu_list_clear(&list);

            for (int i = 0; i < player->item_count; i++) {
                Item *item = player_item_at(player, i);

                character_item_display_name_build(false, true, 0, 0, item);
                menu_list_add_item(&list, item->name, item);
            }

            if (redraw_player == true || gbl.byte_1D2C8 == true) {
                frames_draw_07();

                character_display_name(true, 1, 1, player);

                text_display_string("Items", 0, 10, 1,
                                    (int)strlen(player->name) + 4);
                text_display_string("Ready Item", 0, 15, 3, 1);

                redraw_items = true;
                redraw_player = false;
                gbl.byte_1D2C8 = false;
            }

            input_key = prompt_select_item(&chosen, &dummy_index, &redraw_items,
                                           true, &list, 0x16, 0x26, 5, 1,
                                           GBL_DEFAULT_MENU_COLORS, text, "");

            curr_item = (chosen != NULL) ? chosen->item : NULL;

            if (curr_item != NULL) {
                switch (input_key) {
                case 'V':
                    viewplayer_item_display_stats(curr_item);
                    redraw_items = true;
                    redraw_player = true;
                    break;

                case 'R':
                    viewplayer_ready_item(curr_item);
                    break;

                case 'U':
                    if (curr_item->readied == false) {
                        character_print_message("Must be Readied");
                        input_key = ' ';
                    } else if (item_is_scroll(curr_item) == true ||
                               (curr_item->affect_2 > 0 &&
                                curr_item->affect_3 < 0x80)) {
                        viewplayer_use_magic_item(&turn_used, curr_item);

                        /* Outside a fight there is no turn to spend, whatever
                         * was cast. */
                        if (gbl.game_state != GAME_STATE_COMBAT) {
                            turn_used = false;
                        }

                        if (turn_used == false) {
                            redraw_player = true;
                        }
                    }
                    break;

                case 'T':
                    if (viewplayer_can_sell_drop_trade_item(curr_item) == true) {
                        viewplayer_trade_item(curr_item);
                    } else {
                        input_key = ' ';
                    }
                    redraw_player = true;
                    break;

                case 'D':
                    if (viewplayer_can_sell_drop_trade_item(curr_item) == true) {
                        char line[80];

                        character_item_display_name_build(false, false, 0, 0,
                                                          curr_item);

                        snprintf(line, sizeof(line),
                                 "Your %s will be gone forever",
                                 curr_item->name);

                        text_press_any_key(line, true, 14, 22, 0x26, 21, 1);

                        if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS,
                                          "Drop It? ") == 'Y') {
                            character_lose_item(curr_item, gbl.selected_player);
                            redraw_items = true;
                        }

                        frames_clear_region(TEXT_REGION_NORMAL2);
                    } else {
                        input_key = ' ';
                    }
                    break;

                case 'H':
                    viewplayer_halve_items(curr_item);
                    break;

                case 'J':
                    viewplayer_join_items(curr_item);
                    break;

                case 'S':
                    if (viewplayer_can_sell_drop_trade_item(curr_item) == true) {
                        viewplayer_shop_sell_item(curr_item);
                    } else {
                        input_key = ' ';
                    }
                    break;

                case 'I':
                    viewplayer_identify_item(&redraw_items, curr_item);
                    break;

                default:
                    break;
                }
            }

            character_recalc_values(player);
        }

        if (player->item_count != old_item_count) {
            redraw_items = true;
        }
    }

    if (out_turn_used != NULL) {
        *out_turn_used = turn_used;
    }
}

/* sub_55B04 */
void viewplayer_calc_items_effects(bool add_item, Item *item)
{
    Player *player = gbl.selected_player;
    int masked_affect;

    if (player == NULL) {
        log_warn("item effects: no character to apply an item to");
        return;
    }

    /* The top bit marks the item as magical at all - ready_Item tests it before
     * calling - and the low seven pick the effect. */
    masked_affect = item->affect_3 & 0x7f;

    switch (masked_affect) {
    case 0:
        /* The item carries a plain affect in affect_2. The flag makes the affect
         * table run its item handler whatever affect it is handed, and that
         * handler is the one that reads affect_2. */
        gbl.apply_item_affect = true;
        affect_table_call(add_item ? EFFECT_ADD : EFFECT_REMOVE, item, player,
                          (Affects)item->affect_3);
        break;

    case 1:
        /* A ring of wizardry doubles the first three magic-user spell levels.
         * Taking it off cannot halve them again - a doubled odd number does not
         * halve back - so the slots are rebuilt from the character's level
         * instead, and any spell that no longer fits is forgotten. */
        if (add_item) {
            player->spell_cast_count[2][0] *= 2;
            player->spell_cast_count[2][1] *= 2;
            player->spell_cast_count[2][2] *= 2;
        } else {
            int mu_level = player_skill_level(player, SKILL_MAGIC_USER);
            u8  counted[5];

            for (int sp_lvl = 0; sp_lvl < 5; sp_lvl++) {
                player->spell_cast_count[2][sp_lvl] = 0;
                counted[sp_lvl] = 0;
            }

            player->spell_cast_count[2][0] = 1;

            for (int lvl = 0; lvl < mu_level - 1; lvl++) {
                if ((size_t)lvl >= COAB_ARRAY_LEN(classcalc_mu_spell_lvl_learn)) {
                    log_warn("item effects: magic-user level %d past the table",
                             lvl + 2);
                    break;
                }
                for (int sp_lvl = 0; sp_lvl < 5; sp_lvl++) {
                    player->spell_cast_count[2][sp_lvl] +=
                        classcalc_mu_spell_lvl_learn[lvl][sp_lvl];
                }
            }

            /* Walked from the front, so the spells memorised first are the ones
             * kept. The C# collected the ids to drop and cleared them
             * afterwards, because it could not remove from a list it was
             * walking; clearing by id here would do the same to the list, so the
             * ids go into a second pass just as they did. */
            int drop[SPELL_LIST_MAX];
            int drop_count = 0;

            for (int i = 0; i < player->spell_list.count; i++) {
                int id = player->spell_list.items[i].id;
                const SpellEntry *se = spell_entry(id);
                int sp_lvl;

                if (se == NULL || se->spell_class != SPELL_CLASS_MAGIC_USER) {
                    continue;
                }

                sp_lvl = se->spell_level - 1;

                if (sp_lvl < 0 || sp_lvl > 4) {
                    log_warn("item effects: spell 0x%x has level %d", id,
                             se->spell_level);
                    continue;
                }

                counted[sp_lvl] += 1;

                if (counted[sp_lvl] > player->spell_cast_count[2][sp_lvl]) {
                    drop[drop_count++] = id;
                }
            }

            for (int i = 0; i < drop_count; i++) {
                spell_list_clear_spell(&player->spell_list, drop[i]);
            }
        }
        break;

    case 2:                             /* gauntlets of dexterity */
        effect_calc_stat_bonuses(STAT_DEX, player);
        classcalc_thief_skills(player);
        break;

    case 4:
        /* An item that burns anyone not of its own alignment. The damage is
         * affect_2's whole byte shifted up four, so the low nibble that holds the
         * alignment is counted into it as well. */
        if ((item->affect_2 & 0x0f) != player->alignment) {
            int damage = item->affect_2 << 4;

            item->readied = false;

            gbl.damage_flags = DAMAGE_MAGIC;

            if (gbl.game_state == GAME_STATE_COMBAT) {
                character_redraw_combat_screen();
            }

            effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL, damage, player);
            gbl.byte_1D2C8 = true;
        }
        break;

    case 5:
        effect_calc_stat_bonuses(STAT_STR, player);
        break;

    case 6:                             /* girdle of dwarvenkind */
        effect_calc_stat_bonuses(STAT_CON, player);
        effect_calc_stat_bonuses(STAT_CHA, player);
        break;

    case 8:                             /* ioun stone: affect_2 names the stat */
        if (item->affect_2 >= 0 && item->affect_2 <= 5) {
            effect_calc_stat_bonuses((Stat)item->affect_2, player);
        }
        break;

    case 9:
        if (add_item == false) {
            effect_remove_affect(NULL, AFFECT_SPIRITUAL_HAMMER, player);
        }
        break;

    case 10:
        effect_calc_stat_bonuses(STAT_DEX, player);
        break;

    case 11:                            /* gloves of thievery */
        classcalc_thief_skills(player);
        break;

    case 12:
        effect_calc_stat_bonuses(STAT_INT, player);
        break;

    case 13:
        effect_calc_stat_bonuses(STAT_STR, player);
        effect_calc_stat_bonuses(STAT_INT, player);
        break;

    default:
        /* 3, 7, 14 and up: nothing. */
        break;
    }
}

/* ovr020.Weld: why an item may not be readied. */
typedef enum {
    WELD_OK              = 0,
    WELD_WRONG_CLASS     = 1,
    WELD_ALREADY_USING_X = 2,
    WELD_HANDS_FULL      = 3
} Weld;

void viewplayer_ready_item(Item *item)
{
    bool magic_item = item->affect_3 > 0x7f;
    Player *player = gbl.selected_player;

    if (player == NULL) {
        log_warn("ready item: nobody to ready it for");
        return;
    }

    if (item->readied) {
        if (item->cursed) {
            character_print_message("It's Cursed");
        } else {
            item->readied = false;

            if (magic_item) {
                viewplayer_calc_items_effects(false, item);
            }
        }
        return;
    }

    Weld result = WELD_OK;
    ItemSlot item_slot = (ItemSlot)item_data(item->type)->slot;

    if (player->weapons_hands_used + item_hands_count(item) > 2) {
        result = WELD_HANDS_FULL;
    }

    if (item_slot >= ITEM_SLOT_0 && item_slot <= ITEM_SLOT_8) {
        if (player_ready_item(player, item_slot) != NULL) {
            result = WELD_ALREADY_USING_X;
        }
    } else if (item_slot == ITEM_SLOT_9) {
        /* Slot 9's occupant is tested by looking in slot 10 - Item_ptr_02 in the
         * C#, itemArray[10] - which is the original's own off-by-one. It only
         * ever decided a message that the line below throws away. */
        if (player_ready_item(player, ITEM_SLOT_10) != NULL) {
            result = WELD_ALREADY_USING_X;
        }
    }

    if (item->type == ITEM_ARROW && player_arrows(player) != NULL) {
        result = WELD_ALREADY_USING_X;
        item_slot = ITEM_SLOT_11;
    }

    if (item->type == ITEM_QUARREL && player_quarrels(player) != NULL) {
        result = WELD_ALREADY_USING_X;
        item_slot = ITEM_SLOT_QUARREL;
    }

    if ((player->class_flags & item_data(item->type)->class_flags) == 0) {
        result = WELD_WRONG_CLASS;
    }

    /* Every one of those answers is thrown away here, so nothing is ever
     * refused: a character can ready armour they cannot use, a second shield, or
     * a two-handed sword with a full pair of hands. The DOS build did the same -
     * the checks are all reachable and none of them can be seen - and the three
     * messages below are dead code that only says what they were for. */
    result = WELD_OK;

    switch (result) {
    case WELD_OK:
        item->readied = true;

        if (magic_item) {
            viewplayer_calc_items_effects(true, item);
        }
        break;

    case WELD_WRONG_CLASS:
        character_print_message("Wrong Class");
        break;

    case WELD_ALREADY_USING_X: {
        Item *in_slot = player_ready_item(player, item_slot);
        char line[64];

        if (in_slot != NULL) {
            character_item_display_name_build(false, false, 0, 0, in_slot);
            snprintf(line, sizeof(line), "already using %s", in_slot->name);
            character_print_message(line);
        }
        break;
    }

    case WELD_HANDS_FULL:
        if (gbl.game_state != GAME_STATE_COMBAT ||
            player->quick_fight == QUICK_FIGHT_FALSE) {
            character_print_message("Your hands are full!");
        }
        break;
    }
}

/* ------------------------------------------------- moving things about */

void viewplayer_trade_item(Item *item)
{
    Player *player = gbl.trade_with;

    if (item == NULL) {
        log_warn("character sheet: nothing to hand over");
        return;
    }

    character_load_pic();

    character_select_a_player(&player, true, "Trade with Whom?");

    if (player != NULL) {
        gbl.trade_with = player;

        if (viewplayer_can_carry(item, player) == true) {
            character_print_message("Overloaded");
        } else {
            /* The pack takes a copy, as the DOS build's list did; the original is
             * then dropped from whoever was holding it. */
            player_item_add(player, item);
            character_lose_item(item, gbl.selected_player);
            character_recalc_values(player);
        }
    }
}

void viewplayer_halve_items(Item *item)
{
    int half_number;

    if (item == NULL || gbl.selected_player == NULL) {
        log_warn("character sheet: nothing to halve, or nobody holding it");
        return;
    }

    half_number = item->count / 2;

    if (half_number > 0) {
        int half_and_remainder = item->count - half_number;
        Item new_item = *item;      /* ShallowClone */

        item->count = half_and_remainder;

        new_item.count = half_number;
        new_item.readied = false;

        /* Appending cannot move what is already in the pack, so `item` above
         * stays valid; a full pack is why the menu only offers Halve when there
         * is a slot free. */
        player_item_add(gbl.selected_player, &new_item);
    } else {
        character_print_message("Can't halve that");
    }
}

/* sub_56285 */
void viewplayer_join_items(Item *item)
{
    Player *player = gbl.selected_player;
    /* The identity a stack has to share to be gathered in. Held by value because
     * `target` is moved to another stack in the overflow case below, and because
     * dropping a stack shuffles the pack under both pointers. */
    Item key;
    int match[PLAYER_MAX_ITEMS];
    int match_count = 0;
    int target = -1;

    if (item == NULL || player == NULL) {
        log_warn("character sheet: nothing to join, or nobody holding it");
        return;
    }

    /* The C# looked the item up in the pack, assigned the result to `actual` and
     * never read it again. The lookup is still needed here - the index is how the
     * stack is named once the pack starts closing up over gaps - but the answer
     * being unused in the original is why nothing checks it there. */
    for (int i = 0; i < player->item_count; i++) {
        if (player_item_at(player, i) == item) {
            target = i;
            break;
        }
    }

    if (target < 0) {
        log_warn("character sheet: the stack to join into is not in the pack");
        return;
    }

    key = *item;

    /* affect_1 < 2 keeps anything with charges left out of it: a wand with three
     * uses and one with two are not the same thing. Note it is the item's own
     * affect_1 that is tested, not the candidate's, so a charged stack matches
     * nothing at all. */
    for (int i = 0; i < player->item_count; i++) {
        const Item *cand = player_item_at(player, i);

        if (i != target &&
            cand->count > 0 &&
            cand->namenum1 == key.namenum1 &&
            cand->namenum2 == key.namenum2 &&
            cand->namenum3 == key.namenum3 &&
            cand->type == key.type &&
            cand->plus == key.plus &&
            cand->plus_save == key.plus_save &&
            cand->cursed == key.cursed &&
            cand->weight == key.weight &&
            cand->affect_1 == key.affect_1 &&
            key.affect_1 < 2 &&
            cand->affect_2 == key.affect_2 &&
            cand->affect_3 == key.affect_3) {
            match[match_count++] = i;
        }
    }

    for (int m = 0; m < match_count; m++) {
        int at = match[m];
        Item *into = player_item_at(player, target);
        Item *from = player_item_at(player, at);

        if (into == NULL || from == NULL) {
            log_warn("character sheet: the pack moved under a join");
            return;
        }

        if (from->count + into->count <= 255) {
            into->count += from->count;
            character_lose_item(from, player);

            /* The pack closed up over the gap, so everything after it slid down
             * one. The C# held references and needed none of this. */
            if (at < target) {
                target--;
            }
            for (int n = m + 1; n < match_count; n++) {
                if (match[n] > at) {
                    match[n]--;
                }
            }
        } else {
            /* The original's arithmetic, kept: what is left over after topping
             * the stack up to 255 is (into + from) - 255, and this is the
             * negation of that. The stack it is written to therefore comes out
             * with a negative count, and joining continues into it. The game's
             * stacks never come near 255, which is why it was never noticed. */
            int temp_count = 255 - (into->count + from->count);

            into->count = 255;
            from->count = temp_count;

            target = at;
        }
    }
}

/* sub_56478 */
void viewplayer_use_magic_item(bool *out_turn_used, Item *item)
{
    Player *player = gbl.selected_player;
    bool turn_used = false;
    int spell_id = 0;

    if (item == NULL || player == NULL) {
        log_warn("character sheet: nothing to use, or nobody to use it");
        return;
    }

    if (out_turn_used != NULL) {
        turn_used = *out_turn_used;
    }

    gbl.spell_from_item = false;

    if (item_is_scroll(item) == true) {
        bool dummy_bool = false;
        int dummy_index = -1;

        gbl.current_scroll = item;

        spell_id = viewplayer_spell_menu2(&dummy_bool, &dummy_index,
                                          SPELL_SOURCE_CAST, SPELL_LOC_SCROLL);
    } else if (item->affect_2 > 0 && item->affect_3 < 0x80) {
        gbl.spell_from_item = true;
        spell_id = item->affect_2 & 0x7f;
    }

    if (spell_id == 0) {
        turn_used = false;
    } else {
        if (gbl.game_state == GAME_STATE_COMBAT &&
            player->quick_fight == QUICK_FIGHT_FALSE) {
            character_redraw_combat_screen();
        }

        /* A wand or a staff is announced by name before it goes off; a scroll has
         * already had its spell list on screen. */
        if (gbl.spell_from_item == true) {
            character_display_status_string(false, 10, "uses an item", player);

            if (gbl.game_state == GAME_STATE_COMBAT) {
                text_display_string("Item:", 0, 10, 0x17, 0);
                character_item_display_name_build(true, false, 0x17, 5, item);
            } else {
                character_item_display_name_build(true, false, 0x16, 1, item);
            }

            text_game_delay();
            character_clear_text_area();
        }

        /* Set for both routes from here on: the spell is the item's doing either
         * way, which is what stops it being charged against memorised slots. */
        gbl.spell_from_item = true;

        if (item_is_scroll(item) == true) {
            /* A caster reads it. A thief of tenth level or better can try, and
             * fails a quarter of the time; anyone else fumbles it. */
            if (player_skill_level(player, SKILL_MAGIC_USER) > 0 ||
                player_skill_level(player, SKILL_CLERIC) > 0) {
                spellcast_resolve_spell_used(&turn_used, false,
                                             player->quick_fight, spell_id);
            } else if (player->class_level[SKILL_THIEF] > 9 &&
                       effect_roll_dice(100, 1) <= 75) {
                spellcast_resolve_spell_used(&turn_used, false,
                                             player->quick_fight, spell_id);
            } else {
                character_display_status_string(true, gbl.text_y_col, "oops!",
                                                player);
            }
        } else {
            spellcast_resolve_spell_used(&turn_used, false,
                                             player->quick_fight, spell_id);
        }

        gbl.spell_from_item = false;

        /* Anything but a camping spell costs the character their turn. */
        if (gbl.game_state == GAME_STATE_COMBAT) {
            const SpellEntry *se = spell_entry(spell_id);

            if (se != NULL && se->when_cast != SPELL_WHEN_CAMP) {
                turn_used = true;
                character_clear_actions(player);
            }
        }
    }

    if (turn_used == true) {
        if (item_is_scroll(item) == true) {
            spellcast_remove_spell_from_scroll(spell_id, item, player, NULL);
        } else if (item->affect_1 > 0) {
            /* A stack of charged items spends a whole one; a single item spends
             * one charge, and is gone when the last is used. */
            if (item->count > 1) {
                item->count -= 1;
            } else {
                item->affect_1 -= 1;

                if (item->affect_1 == 0) {
                    character_lose_item(item, player);
                }
            }
        }
    }

    if (out_turn_used != NULL) {
        *out_turn_used = turn_used;
    }
}

/* sell_Item */
void viewplayer_shop_sell_item(Item *item)
{
    Player *player = gbl.selected_player;
    int item_value = 0;
    char offer[96];

    if (item == NULL || player == NULL) {
        log_warn("shop: nothing to sell, or nobody selling it");
        return;
    }

    if (item->value > 0) {
        item_value = item->value / 2;
    }

    /* A stack fetches a twentieth of what it is worth, except for arrows and
     * quarrels, which are paid for one at a time. */
    if (item->count > 1) {
        if (item->type != ITEM_ARROW && item->type != ITEM_QUARREL) {
            item_value = (item->count * item_value) / 20;
        } else {
            item_value *= item->count;
        }
    }

    character_item_display_name_build(false, false, 0, 0, item);

    snprintf(offer, sizeof(offer),
             "I'll give you %d gold pieces for your %s", item_value,
             item->name);

    text_press_any_key_region(offer, true, 14, TEXT_REGION_NORMAL2);

    if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Is It a Deal? ") == 'Y') {
        int plat = item_value / 5;
        int gold = item_value % 5;
        int overflow = 0;

        character_print_message("Sold!");

        character_lose_item(item, player);

        /* Paid in platinum with the change in gold, and coin weighs, so a purse
         * that cannot take it puts the rest in the pool. Note the gold is handed
         * over either way - the original weighed only the platinum against what
         * the character could still lift. */
        if (treasure_will_overload(&overflow, plat + gold, player) == true) {
            character_print_message("Overloaded. Money will be put in pool.");

            if (overflow > plat) {
                money_add_coins(&player->money, MONEY_PLATINUM, plat);
            } else {
                money_add_coins(&player->money, MONEY_PLATINUM, overflow);
                money_add_coins(&gbl.pooled_money, MONEY_PLATINUM,
                                plat - overflow);
            }

            money_add_coins(&player->money, MONEY_GOLD, gold);
        } else {
            money_add_coins(&player->money, MONEY_PLATINUM, plat);
            money_add_coins(&player->money, MONEY_GOLD, gold);
        }
    }

    frames_clear_region(TEXT_REGION_NORMAL2);
}

void viewplayer_identify_item(bool *out_identified, Item *item)
{
    Player *player = gbl.selected_player;
    bool id_item = false;
    char line[96];

    if (item == NULL || player == NULL) {
        log_warn("shop: nothing to identify, or nobody to identify it for");
        return;
    }

    character_item_display_name_build(false, false, 0, 0, item);

    snprintf(line, sizeof(line), "For 200 gold pieces I'll identify your %s",
             item->name);
    text_press_any_key_region(line, true, 14, TEXT_REGION_NORMAL2);

    if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Is It a Deal? ") == 'Y') {
        int cost = 200;

        /* Out of the character's own purse if they can cover it, and out of the
         * pool if they cannot. */
        if (cost <= money_gold_worth(&player->money)) {
            id_item = true;
            money_subtract_gold_worth(&player->money, cost);
        } else if (cost <= money_gold_worth(&gbl.pooled_money)) {
            id_item = true;
            money_subtract_gold_worth(&gbl.pooled_money, cost);
        } else {
            character_print_message("Not Enough Money");
        }
    }

    if (id_item == true) {
        if (item->hidden_names_flag == 0) {
            snprintf(line, sizeof(line),
                     "I can't tell anything new about your %s", item->name);
            text_press_any_key_region(line, true, 14, TEXT_REGION_NORMAL2);
        } else {
            item->hidden_names_flag = 0;
            character_item_display_name_build(false, false, 0, 0, item);

            snprintf(line, sizeof(line), "It looks like some sort of %s",
                     item->name);
            text_press_any_key_region(line, true, 14, TEXT_REGION_NORMAL2);

            if (out_identified != NULL) {
                *out_identified = true;
            }
        }

        text_game_delay();
    }

    frames_clear_region(TEXT_REGION_NORMAL2);
}

/* ------------------------------------------------------------------ the coin */

/* The coin menu both trading and dropping put up: one line per kind the
 * character has any of, largest number of characters first as the C# built it -
 * copper through jewelry, in table order. */
static void build_coin_list(MenuList *list)
{
    menu_list_clear(list);

    if (gbl.selected_player == NULL) {
        return;
    }

    for (int coin = 0; coin < MONEY_KINDS; coin++) {
        int count = money_get(&gbl.selected_player->money, (MoneyKind)coin);

        if (count != 0) {
            char line[MENU_ITEM_TEXT_MAX];

            snprintf(line, sizeof(line), "%8s %d", money_string[coin], count);
            menu_list_add(list, line);
        }
    }
}

void viewplayer_trade_coin(void)
{
    /* A MenuList is 10K; the two coin routines share this one, and neither is
     * re-entered - the character selection in between cannot get back here. */
    static MenuList list;
    bool finished = false;

    if (gbl.selected_player == NULL) {
        log_warn("character sheet: nobody's coin to trade");
        return;
    }

    do {
        Player *dest = gbl.trade_with;

        character_load_pic();

        character_select_a_player(&dest, true, "Trade to?");

        if (dest == NULL) {
            finished = true;
        } else {
            bool no_money_left;

            viewplayer_display_full(gbl.selected_player);

            do {
                MenuItem *chosen = NULL;
                int dummy_index = 0;
                bool dummy_bool = true;

                viewplayer_display_money();
                gbl.trade_with = dest;

                build_coin_list(&list);

                prompt_select_item(&chosen, &dummy_index, &dummy_bool, true,
                                   &list, 13, 0x19, 7, 12,
                                   GBL_DEFAULT_MENU_COLORS, " Select",
                                   "Select type of coin ");

                if (chosen == NULL) {
                    no_money_left = true;
                } else {
                    char kind[32];
                    char text[64];
                    int money_slot;
                    i16 num_coins;

                    money_slot = treasure_money_index_from_string(
                                     kind, sizeof(kind), chosen->text);

                    /* The word comes back with its own trailing space, which is
                     * how the C#'s concatenation read. */
                    snprintf(text, sizeof(text), "How much %swill you trade? ",
                             kind);

                    num_coins = treasure_ask_number_value(
                                    10, text,
                                    money_get(&gbl.selected_player->money,
                                              (MoneyKind)money_slot));

                    treasure_trade_money((MoneyKind)money_slot, num_coins, dest,
                                         gbl.selected_player);

                    /* An empty purse ends both loops at once. */
                    no_money_left = !money_any(&gbl.selected_player->money);
                    finished = no_money_left;
                }
            } while (no_money_left == false && input_quit_requested() == false);
        }
    } while (finished == false && input_quit_requested() == false);
}

void viewplayer_drop_coin(void)
{
    static MenuList list;
    bool no_more_money;

    if (gbl.selected_player == NULL) {
        log_warn("character sheet: nobody's coin to drop");
        return;
    }

    do {
        MenuItem *chosen = NULL;
        int index = 0;
        bool redraw_menu_items = true;

        viewplayer_display_money();

        build_coin_list(&list);

        prompt_select_item(&chosen, &index, &redraw_menu_items, true, &list,
                           13, 0x19, 7, 12, GBL_DEFAULT_MENU_COLORS, " Select",
                           "Select type of coin ");

        if (chosen == NULL) {
            no_more_money = true;
        } else {
            char kind[32];
            char text[64];
            int money_slot;
            i16 num_coins;

            money_slot = treasure_money_index_from_string(kind, sizeof(kind),
                                                          chosen->text);

            snprintf(text, sizeof(text), "How much %swill you drop? ", kind);

            num_coins = treasure_ask_number_value(
                            10, text,
                            money_get(&gbl.selected_player->money,
                                      (MoneyKind)money_slot));

            treasure_drop_coins((MoneyKind)money_slot, num_coins,
                                gbl.selected_player);

            no_more_money = !money_any(&gbl.selected_player->money);
        }
    } while (no_more_money == false && input_quit_requested() == false);
}

/* ---------------------------------------------------------------- carrying */

bool viewplayer_can_carry(const Item *item, Player *player)
{
    bool too_heavy = false;
    int item_weight;

    if (item == NULL || player == NULL) {
        log_warn("character sheet: weighing nothing, or nobody to weigh it for");
        return true;
    }

    character_recalc_values(player);

    if (player->item_count >= PLAYER_MAX_ITEMS) {
        too_heavy = true;
    }

    item_weight = item->weight;

    if (item->count > 0) {
        item_weight *= item->count;
    }

    /* The 1500 the original allowed over the limit is the slack that lets a
     * character pick up treasure they cannot quite lift. */
    if ((player->weight + item_weight) >
        (character_max_encumberance(player) + 1500)) {
        too_heavy = true;
    }

    return too_heavy;
}

void viewplayer_scroll_team_list(char input_key)
{
    int index = -1;

    if (gbl.team_count <= 0) {
        return;
    }

    /* Every caller reaches this from a cursor key, so the arrow keys can be taken
     * for Home and End here rather than at each of the eleven call sites. See
     * prompt_selection_key. */
    input_key = prompt_selection_key(input_key, true);

    for (int i = 0; i < gbl.team_count; i++) {
        if (gbl.team_list[i] == gbl.selected_player) {
            index = i;
            break;
        }
    }

    /* IndexOf answers -1 for somebody not on the list, and the C# stepped that
     * to 0 going forwards and to the last entry going back. Both are in range,
     * so it lands on somebody either way. */
    if (input_key == 'O') {
        index = (index + 1) % gbl.team_count;
        gbl.selected_player = gbl.team_list[index];
    } else if (input_key == 'G') {
        index = (index - 1 + gbl.team_count) % gbl.team_count;
        gbl.selected_player = gbl.team_list[index];
    }
}

/* ------------------------------------------------------------- the spell list */

u8 viewplayer_spell_menu2(bool *out_has_spells, int *index, SpellSource source,
                          SpellLoc location)
{
    const char *text;
    bool has_spells;
    u8 result;
    int local_index = -1;

    if (index == NULL) {
        index = &local_index;
    }

    switch (location) {
    case SPELL_LOC_MEMORY:   text = "in Memory";   break;
    case SPELL_LOC_GRIMOIRE: text = "in Grimoire"; break;
    case SPELL_LOC_SCROLL:   text = "on Scroll";   break;
    case SPELL_LOC_SCROLLS:  text = "on Scrolls";  break;
    case SPELL_LOC_CHOOSE:   text = "to Choose";   break;
    case SPELL_LOC_MEMORIZE: text = "to Memorize"; break;
    case SPELL_LOC_SCRIBE:   text = "to Scribe";   break;
    default:                 text = "";            break;
    }

    has_spells = spellmenu_build_spell_list(location);

    if (out_has_spells != NULL) {
        *out_has_spells = has_spells;
    }

    if (has_spells == false) {
        return 0;
    }

    /* The frame is drawn only when the list is being started rather than
     * resumed - a caller walking the same list keeps its screen - and casting
     * always redraws, because whatever was aimed left something on it. */
    if (*index < 0 || source == SPELL_SOURCE_CAST) {
        if (gbl.game_state != GAME_STATE_COMBAT) {
            if (source == SPELL_SOURCE_MEMORIZE) {
                frames_draw_05();
            } else {
                frames_draw_07();
            }
        } else {
            frames_draw_outer();
        }
    }

    if (gbl.selected_player != NULL) {
        char heading[64];

        character_display_name(true, 1, 1, gbl.selected_player);

        snprintf(heading, sizeof(heading), "Spells %s", text);
        text_display_string(heading, 0, 10, 1,
                            (int)strlen(gbl.selected_player->name) + 4);
    }

    result = spellmenu_menu(index, source);

    return result;
}

/* ---------------------------------------------------------- what a paladin can do */

/* sub_575F0 */
bool viewplayer_can_cast_heal(const Player *player)
{
    if (player == NULL) {
        return false;
    }

    return player_skill_level(player, SKILL_PALADIN) > 0 &&
           gbl.game_state != GAME_STATE_COMBAT &&
           player->health_status == STATUS_OKEY &&
           player_has_affect(player, AFFECT_PALADIN_DAILY_HEAL_CAST) == false;
}

/* sub_57655 */
bool viewplayer_can_cast_cure_diseases(const Player *player)
{
    if (player == NULL) {
        return false;
    }

    return player_skill_level(player, SKILL_PALADIN) > 0 &&
           gbl.game_state != GAME_STATE_COMBAT &&
           player->health_status == STATUS_OKEY &&
           player->paladin_cures_left > 0;
}

void viewplayer_paladin_heal(Player *player)
{
    Player *target;
    int heal_amount;

    if (player == NULL || gbl.team_count <= 0) {
        log_warn("paladin heal: nobody to heal, or nobody to heal them");
        return;
    }

    character_load_pic();
    target = gbl.team_list[0];

    character_select_a_player(&target, true, "Heal whom? ");

    if (target == NULL) {
        viewplayer_display_full(gbl.selected_player);
        return;
    }

    heal_amount = player_skill_level(player, SKILL_PALADIN) * 2;

    if (effect_heal_player(0, heal_amount, target) == true) {
        char line[64];

        snprintf(line, sizeof(line), "%s feels better", target->name);
        character_print_message(line);
    } else {
        char line[64];

        snprintf(line, sizeof(line), "%s is unaffected", target->name);
        character_print_message(line);
    }

    /* 1440 minutes: once a day, whether or not it did any good. */
    effect_add_affect(false, 0, 1440, AFFECT_PALADIN_DAILY_HEAL_CAST, player);
    viewplayer_display_full(gbl.selected_player);
}

/* unk_16B39 */
static const Affects paladin_cureable_diseases[] = {
    AFFECT_HELPLESS,
    AFFECT_CAUSE_DISEASE_1,
    AFFECT_WEAKEN,
    AFFECT_CAUSE_DISEASE_2,
    AFFECT_HOT_FIRE_SHIELD,
    AFFECT_39
};

/* sub_577EC */
void viewplayer_paladin_cure_disease(Player *player)
{
    Player *target;
    bool is_diseased = false;
    char input = 'Y';

    if (player == NULL || gbl.team_count <= 0) {
        log_warn("paladin cure: nobody to cure, or nobody to cure them");
        return;
    }

    character_load_pic();
    target = gbl.team_list[0];

    character_select_a_player(&target, true, "Cure whom? ");

    if (target == NULL) {
        viewplayer_display_full(gbl.selected_player);
        return;
    }

    for (size_t i = 0; i < COAB_ARRAY_LEN(paladin_cureable_diseases); i++) {
        if (player_has_affect(target, paladin_cureable_diseases[i])) {
            is_diseased = true;
            break;
        }
    }

    /* Curing somebody who is well spends the cure all the same, which is why it
     * asks first. */
    if (is_diseased == false) {
        character_display_status_string(false, 0, "is not diseased", target);

        input = prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "cure anyway: ");

        character_clear_text_area();
    }

    if (input == 'Y') {
        /* The flag tells the affect table this is a cure rather than the affect
         * running out, which changes what some of them do on the way off. */
        gbl.cure_spell = true;

        for (size_t i = 0; i < COAB_ARRAY_LEN(paladin_cureable_diseases); i++) {
            effect_remove_affect(NULL, paladin_cureable_diseases[i], target);
        }

        gbl.cure_spell = false;

        if (player->paladin_cures_left > 0) {
            player->paladin_cures_left--;
        }

        /* 0x2760 minutes is a week: that is when the day's cures come back. */
        if (player_has_affect(player,
                              AFFECT_PALADIN_DAILY_CURE_REFRESH) == false) {
            effect_add_affect(true, 0, 0x2760,
                              AFFECT_PALADIN_DAILY_CURE_REFRESH, player);
        }

        {
            char line[64];

            snprintf(line, sizeof(line), "%s is cured", target->name);
            character_print_message(line);
        }
    }

    viewplayer_display_full(gbl.selected_player);
}
