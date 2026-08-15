/* resting.c - Ported from engine/ovr021.cs. */
#include <stdio.h>
#include <string.h>

#include "resting.h"

#include "character.h"
#include "effect.h"
#include "frames.h"
#include "gbl.h"
#include "input.h"
#include "log.h"
#include "player.h"
#include "prompt.h"
#include "set.h"
#include "spellcast.h"
#include "spelllist.h"
#include "spells.h"
#include "text.h"

/* ovr021.timeScales, word_1A13C. The same table the clock itself uses; see
 * resttime.h for what each slot holds. */
#define TIME_SCALES REST_TIME_SCALES

/* ---------------------------------------------------------------- affects */

/* sub_5801E. Runs `time_steps` of slot `time_slot` past everybody's affects,
 * in ten-minute bites because that is the largest step the original took at
 * once.
 *
 * gbl.affects_timed_out is the shortcut that makes this cheap: outside camp every
 * flag is set, so everybody is looked at, but while the party is camping - which
 * is where whole days go by five minutes at a time - only the characters who
 * still have something ticking are walked, and the whole call returns at once
 * when none of them do. */
static void check_affects_timing_out(int time_slot, int time_steps)
{
    int minutes_left;

    if (gbl.game_state != GAME_STATE_CAMPING) {
        for (int i = 0; i < GBL_AFFECTS_TIMED_OUT; i++) {
            gbl.affects_timed_out[i] = true;
        }
    } else {
        bool any_ticking = false;
        int  player_count = 0;

        /* A do-while, as the original had it: the first party member's flag is
         * looked at even when the party is empty. */
        do {
            if (player_count < GBL_AFFECTS_TIMED_OUT &&
                gbl.affects_timed_out[player_count]) {
                any_ticking = true;
            }

            player_count++;
        } while (!any_ticking && player_count < (int)gbl.area2_ptr->party_size);

        if (!any_ticking) {
            return;
        }
    }

    if (time_slot < 0 || time_slot >= REST_TIME_SLOTS) {
        log_warn("resting: no clock slot %d to time affects out of", time_slot);
        return;
    }

    /* Whatever the caller counted in, affects are counted in minutes. */
    minutes_left = time_steps;

    while (time_slot > 1) {
        minutes_left *= TIME_SCALES[time_slot - 1];
        time_slot -= 1;
    }

    while (minutes_left > 0) {
        int step = minutes_left < 10 ? minutes_left : 10;
        int player_count = 0;

        for (int t = 0; t < gbl.team_count; t++) {
            Player *player = gbl.team_list[t];

            if (player_count >= GBL_AFFECTS_TIMED_OUT) {
                /* A fight big enough to run off the end of the flags. The C#
                 * would have thrown; nobody past here has their affects timed
                 * out, which costs them nothing but a longer spell. */
                log_warn("resting: %d on the team, but only %d affect flags",
                         gbl.team_count, GBL_AFFECTS_TIMED_OUT);
                break;
            }

            if (player != NULL && gbl.affects_timed_out[player_count]) {
                gbl.affects_timed_out[player_count] = false;

                /* Two passes, as the C# did with its removeList: the first winds
                 * every affect down, the second takes away the ones that ran
                 * out. Splitting them matters because removing an affect can
                 * change the list.
                 *
                 * The pass below leaves an expired affect untouched, so what is
                 * left with a non-zero duration of `step` or less is exactly the
                 * set the first pass decided to remove. */
                for (int i = 0; i < player->affects.count; i++) {
                    Affect *affect = &player->affects.items[i];

                    if (affect->minutes == 0) {
                        /* Forever: a curse, or an item's own affect. */
                    } else if (step < (int)affect->minutes) {
                        affect->minutes -= (u16)step;
                        gbl.affects_timed_out[player_count] = true;
                    }
                }

                for (int guard = 0; guard <= AFFECT_LIST_MAX; guard++) {
                    Affect *expired = NULL;

                    for (int i = 0; i < player->affects.count; i++) {
                        Affect *affect = &player->affects.items[i];

                        if (affect->minutes != 0 &&
                            (int)affect->minutes <= step) {
                            expired = affect;
                            break;
                        }
                    }

                    if (expired == NULL) {
                        break;
                    }

                    /* Rescanning from the front each time keeps the order the C#
                     * removed them in, and survives a handler that adds or
                     * removes an affect of its own. */
                    effect_remove_affect(expired, (Affects)expired->type, player);
                }

                /* The C# did this again after the removals with a "not sure why
                 * we are doing this" note against it, and it is indeed redundant
                 * as the code stands: the pass above has already set the flag for
                 * every affect that is still ticking, and the removals only take
                 * affects away. It is kept because an ovr013 removal handler is
                 * free to lay a new affect on the character - Feeblemind wearing
                 * off does exactly that - and this pass is what would notice. */
                for (int i = 0; i < player->affects.count; i++) {
                    if (player->affects.items[i].minutes > 0) {
                        gbl.affects_timed_out[player_count] = true;
                    }
                }
            }

            player_count++;
        }

        if (minutes_left > 10) {
            minutes_left -= 10;
        } else {
            minutes_left = 0;
        }
    }
}

/* ------------------------------------------------------------------ clock */

/* sub_58317. One carry per slot per call - not a loop until it settles, which is
 * why the callers step the clock one unit at a time. Reaching the top of the year
 * slot is everybody's birthday and does not carry anywhere. */
static void normalize_clock(RestTime *time)
{
    for (int i = 0; i <= 6; i++) {
        if (time->slot[i] >= TIME_SCALES[i]) {
            if (i != 6) {
                time->slot[i + 1] += 1;
                time->slot[i] -= TIME_SCALES[i];
            } else {
                for (int t = 0; t < gbl.team_count; t++) {
                    if (gbl.team_list[t] != NULL) {
                        gbl.team_list[t]->age += 1;
                    }
                }
            }
        }
    }
}

/* sub_583C8. Tidies gbl.time_to_rest after it has been edited: carries it, then
 * folds any whole month back into days, because the rest menu only shows days,
 * hours and minutes. 99 days is as long as the party may sleep. */
static void normalize_rest_time(void)
{
    normalize_clock(&gbl.time_to_rest);

    if (gbl.time_to_rest.slot[REST_SLOT_MONTHS] > 0) {
        gbl.time_to_rest.slot[REST_SLOT_DAYS] +=
            TIME_SCALES[REST_SLOT_DAYS] * gbl.time_to_rest.slot[REST_SLOT_MONTHS];

        gbl.time_to_rest.slot[REST_SLOT_MONTHS] = 0;

        if (gbl.time_to_rest.slot[REST_SLOT_DAYS] > 99) {
            gbl.time_to_rest.slot[REST_SLOT_DAYS] = 99;
        }
    }
}

/* The world clock lives in Area1 as seven words, which the C# reached through
 * field_6A00_Get(0x6A00 + ((0x4BC6 + i) * 2)). That arithmetic wraps to 16 bits -
 * the DOS build was adding a segment-relative offset to a far pointer - and lands
 * on 0x18c upwards:
 *
 *   slot 0  0x18c  field_18C          tenths of a minute
 *   slot 1  0x18e  time_minutes_ones
 *   slot 2  0x190  time_minutes_tens
 *   slot 3  0x192  time_hour
 *   slot 4  0x194  time_day
 *   slot 5  0x196  the field Area1 calls time_year - it is the month
 *   slot 6  0x198  field_198          the year
 *
 * The last two names came from the C# and are one slot out; nothing but this file
 * writes them and nothing at all reads them, so they are left as they are rather
 * than renamed under the save-game code. The expression is kept literal so it can
 * be read against the original. */
static int clock_word_loc(int slot)
{
    return 0x6a00 + ((0x4bc6 + slot) * 2);
}

void resting_step_game_time(int time_slot, int amount)
{
    RestTime rest_time;

    if (time_slot < 0 || time_slot >= REST_TIME_SLOTS) {
        log_warn("resting: no clock slot %d to add %d to", time_slot, amount);
        return;
    }

    rest_time_clear(&rest_time);

    for (int i = 0; i <= 6; i++) {
        rest_time.slot[i] = area1_word_get(gbl.area_ptr, clock_word_loc(i));
    }

    for (int i = 1; i <= amount; i++) {
        rest_time.slot[time_slot] += 1;

        normalize_clock(&rest_time);
    }

    for (int i = 0; i <= 6; i++) {
        area1_word_set(gbl.area_ptr, clock_word_loc(i), (u16)rest_time.slot[i]);
    }

    check_affects_timing_out(time_slot, amount);
}

void resting_subtract_rest_time(int time_slot, int amount)
{
    if (time_slot < 0 || time_slot >= REST_TIME_SLOTS) {
        log_warn("resting: no clock slot %d to take %d off", time_slot, amount);
        return;
    }

    /* Nothing to take off an empty clock. The tenths slot is not looked at: five
     * minutes is the smallest step anything here takes. */
    if (gbl.time_to_rest.slot[REST_SLOT_DAYS] == 0 &&
        gbl.time_to_rest.slot[REST_SLOT_HOURS] == 0 &&
        gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] == 0 &&
        gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] == 0) {
        return;
    }

    /* Borrow until this slot holds enough to take the subtraction. */
    while (amount > gbl.time_to_rest.slot[time_slot]) {
        int from = time_slot + 1;

        while (from < 5 && gbl.time_to_rest.slot[from] == 0) {
            from += 1;
        }

        if (from == 5) {
            /* Nothing bigger left to break up, so the rest is over. Note that
             * whatever was still in this slot goes with it. */
            rest_time_clear(&gbl.time_to_rest);
            amount = 0;
        } else {
            /* Break one unit of `from` all the way down into this slot. */
            for (int i = from; i >= time_slot + 1; i--) {
                gbl.time_to_rest.slot[i] -= 1;
                gbl.time_to_rest.slot[i - 1] += TIME_SCALES[i - 1];
            }
        }
    }

    gbl.time_to_rest.slot[time_slot] -= amount;

    normalize_rest_time();
}

/* ---------------------------------------------------------------- the menu */

/* sub_5858A. Two digits, so the clock lines up. */
static const char *format_time(char *dst, size_t dst_size, int value)
{
    snprintf(dst, dst_size, "%02d", value);

    return dst;
}

/* sub_58615. "Rest Time: DD:HH:MM" on row 0x11, with the slot the player is
 * editing in white. highlight_time of 0 highlights nothing: it names an entry of
 * the colour array that no part of the clock is drawn in. */
static void display_resting_time(int highlight_time)
{
    int  colors[6];
    char text[8];
    int  col_x = 11;

    for (int index = 0; index < 6; index++) {
        colors[index] = 10;
    }

    if (highlight_time >= 0 && highlight_time < 6) {
        colors[highlight_time] = 15;
    } else {
        log_warn("resting: no clock slot %d to highlight", highlight_time);
    }

    text_display_string("Rest Time:", 0, 10, 17, 1);

    format_time(text, sizeof(text), gbl.time_to_rest.slot[REST_SLOT_DAYS]);
    text_display_string(text, 0, colors[4], 0x11, col_x + 1);
    text_display_string(":", 0, 10, 17, col_x + 3);
    col_x += 3;

    format_time(text, sizeof(text), gbl.time_to_rest.slot[REST_SLOT_HOURS]);
    text_display_string(text, 0, colors[3], 0x11, col_x + 1);
    text_display_string(":", 0, 10, 17, col_x + 3);
    col_x += 3;

    format_time(text, sizeof(text),
                (gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] * 10) +
                gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES]);
    text_display_string(text, 0, colors[2], 0x11, col_x + 1);
}

/* sub_58751. The rest menu: pick a slot, add to it or take off it, then Rest.
 * True when the player asked to rest rather than backing out. */
static bool resting_time_menu(void)
{
    /* unk_58731: Escape, E for Exit and R for Rest. */
    Set exit_keys;
    char input_key = ' ';
    bool resting = false;
    int  time_index = 2;      /* minutes, the slot the menu opens on */

    set_clear(&exit_keys);
    set_add(&exit_keys, 0);
    set_add(&exit_keys, 69);
    set_add(&exit_keys, 82);

    do {
        bool control_key = false;

        display_resting_time(time_index);

        input_key = prompt_display_input(&control_key, false, 1,
                                        GBL_DEFAULT_MENU_COLORS,
                                        "Rest Days Hours Mins Add Subtract Exit",
                                        "");

        if (control_key) {
            switch (input_key) {
            case 'H':       /* up: add to the slot being edited */
                input_key = 'A';
                break;

            case 'P':       /* down: take off it */
                input_key = 'S';
                break;

            case 'K':       /* left, which moves on to the next slot along */
                time_index += 1;
                if (time_index > 4) {
                    time_index = 2;
                }
                input_key = 'X';
                break;

            case 'M':       /* right, back the other way */
                time_index -= 1;
                if (time_index < 2) {
                    time_index = 4;
                }
                input_key = 'X';
                break;

            default:
                /* Any other cursor key does nothing. 'X' is not in the menu, so
                 * it falls through both switches and round the loop again. */
                input_key = 'X';
                break;
            }
        }

        if (input_key == 0x0d) {
            input_key = 'R';
        }

        switch (input_key) {
        case 'R':
            resting = true;
            break;

        case 'D':
            time_index = 4;
            break;

        case 'H':
            time_index = 3;
            break;

        case 'M':
            time_index = 2;
            break;

        case 'A':
            /* Minutes go up five at a time, everything else by one. */
            if (time_index == 2) {
                gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] += 5;
            } else {
                gbl.time_to_rest.slot[time_index] += 1;
            }

            normalize_rest_time();
            break;

        case 'S':
            if (time_index == 2) {
                resting_subtract_rest_time(REST_SLOT_MINUTES_ONES, 5);
            } else {
                resting_subtract_rest_time(time_index, 1);
            }

            normalize_rest_time();
            break;

        default:
            break;
        }
    } while (!set_member_of(&exit_keys, (int)(unsigned char)input_key));

    return resting;
}

/* -------------------------------------------------------------- the sleep */

/* rest_heal. A hit point each, once a day - 8 * 36 five-minute steps. */
static void rest_heal(bool show_text)
{
    gbl.rest_10_seconds++;

    if (gbl.rest_10_seconds >= (8 * 36)) {
        bool update_ui = false;

        for (int t = 0; t < gbl.team_count; t++) {
            if (gbl.team_list[t] != NULL &&
                effect_heal_player(0, 1, gbl.team_list[t])) {
                update_ui = true;
            }
        }

        if (show_text) {
            display_resting_time(0);
        }

        /* Said either way, even when the clock is not on screen, because that is
         * where the original put the call. */
        text_display_string("The Whole Party Is Healed", 0, 10, 19, 1);

        if (update_ui) {
            character_party_summary(gbl.selected_player);
        }

        text_game_delay();
        character_clear_text_area();
        gbl.rest_10_seconds = 0;
    }
}

/* rest_memorize. Finishes the first spell the character is still memorizing, or -
 * with find_next already set - reports how long the next one will take, as its
 * spell level. */
static int rest_memorize(bool *find_next, Player *player)
{
    for (int i = 0; i < player->spell_list.count; i++) {
        int id;

        if (!player->spell_list.items[i].learning) {
            continue;
        }
        id = player->spell_list.items[i].id;

        if (*find_next) {
            const SpellEntry *entry = spell_entry(id);

            return entry != NULL ? entry->spell_level : 0;
        }

        spell_list_mark_learnt(&player->spell_list, id);

        display_resting_time(0);

        spellcast_display_case_spell_text(id, "has memorized", player);
        *find_next = true;
    }

    return 0;
}

/* rest_scribe. Copies the first spell found on a scroll in the character's pack
 * into their spellbook, or reports the level of the next one to copy. */
static int rest_scribe(bool *find_next, Player *player)
{
    int next_scribe_lvl = 0;

    for (int i = 0; i < player->item_count; i++) {
        /* The C# walked player.items.ToArray(), a snapshot of references:
         * scribing the last spell off a scroll drops it, and the loop went on
         * reading the object it had just removed. `dropped_scroll` stands in for
         * that object, because the pack closes up over the gap and index i names
         * a different item afterwards. */
        Item        dropped_scroll;
        const Item *item = &player->items[i];
        bool        dropped = false;

        if (!item_is_scroll(item)) {
            continue;
        }

        for (int spell_idx = 1; spell_idx < 4 && next_scribe_lvl == 0;
             spell_idx++) {
            /* Bit 7 marks a spell still to be copied; a scroll's spell is
             * blanked once it has been. Note the test is strictly greater, so
             * spell id 0 with the bit set is passed over. */
            int affect = (int)item_affect(item, spell_idx);

            if (affect > 0x80) {
                if (*find_next) {
                    const SpellEntry *entry = spell_entry(affect & 0x7f);

                    next_scribe_lvl = entry != NULL ? entry->spell_level : 0;
                } else {
                    int spell_id = affect & 0x7f;

                    player_learn_spell(player, (Spells)spell_id);

                    dropped = !spellcast_remove_spell_from_scroll(
                                  spell_id, &player->items[i], player,
                                  &dropped_scroll);
                    if (dropped) {
                        item = &dropped_scroll;
                    }

                    display_resting_time(0);

                    spellcast_display_case_spell_text(spell_id, "has scribed",
                                                      player);
                    /* Set here, so only one spell is copied per call and the
                     * pack can only change once. */
                    *find_next = true;
                }
            }
        }

        if (next_scribe_lvl != 0) {
            break;
        }

        if (dropped) {
            /* Whatever followed the scroll has moved down into its place. */
            i--;
        }
    }

    return next_scribe_lvl;
}

/* seg600:758D. How many steps until each character finishes their next spell,
 * indexed from 1 as the team list is. Nine entries for a party of eight, which is
 * what the original had. */
static int g_spell_learn_timeout[9];

/* sub_58B4D. Counts each character's timer down and, when it runs out, copies or
 * memorizes their next spell. spell_to_learn_count is the delay before a spell
 * the character has only just picked up may be worked on at all, and is counted
 * down by spell_learning_tick below. */
static void check_for_spell_learning(void)
{
    int index = 1;

    for (int t = 0; t < gbl.team_count; t++) {
        Player *player = gbl.team_list[t];

        if (index >= (int)COAB_ARRAY_LEN(g_spell_learn_timeout)) {
            log_warn("resting: %d on the team, but only %d spell timers",
                     gbl.team_count, (int)COAB_ARRAY_LEN(g_spell_learn_timeout));
            break;
        }

        if (g_spell_learn_timeout[index] > 0) {
            g_spell_learn_timeout[index] -= 1;
        }

        if (g_spell_learn_timeout[index] == 0 && player != NULL &&
            player->spell_to_learn_count == 0) {
            bool find_next = false;
            int  next_lvl = rest_scribe(&find_next, player);

            if (next_lvl == 0) {
                next_lvl = rest_memorize(&find_next, player);
            }

            /* Three steps - fifteen minutes - per spell level. */
            g_spell_learn_timeout[index] = next_lvl * 3;
        }

        index++;
    }
}

/* sub_58C03. Once an hour, twelve five-minute steps, whoever has just come to the
 * end of their spell_to_learn_count starts on their first spell. find_next is set
 * going in, so nothing is copied here: this only works out how long that first
 * spell will take. */
static void spell_learning_tick(int *counter)
{
    *counter += 1;

    if (*counter >= 12) {
        int index = 1;

        *counter = 0;

        for (int t = 0; t < gbl.team_count; t++) {
            Player *player = gbl.team_list[t];

            if (index >= (int)COAB_ARRAY_LEN(g_spell_learn_timeout)) {
                break;
            }

            if (player != NULL && player->spell_to_learn_count > 0 &&
                --player->spell_to_learn_count == 0) {
                bool find_next = true;
                int  next_lvl = rest_scribe(&find_next, player);

                if (next_lvl == 0) {
                    next_lvl = rest_memorize(&find_next, player);
                }

                g_spell_learn_timeout[index] = next_lvl * 2;
            }

            index++;
        }
    }
}

bool resting_run(bool interactive_resting)
{
    bool stop_resting;
    bool resting_interrupted = false;
    int  learning_counter = 0;
    int  display_counter = 0;

    /* Array.Clear(spellLaernTimeout, 0, gbl.TeamList.Count) - which clears entry
     * 0, that nothing uses, and leaves the last character's timer alone, because
     * the array is indexed from 1. A character who was part way through a spell
     * when the party last stopped resting therefore keeps their timer if they are
     * last in the list and loses it otherwise. Faithful to the original. */
    for (int i = 0; i < gbl.team_count &&
                    i < (int)COAB_ARRAY_LEN(g_spell_learn_timeout); i++) {
        g_spell_learn_timeout[i] = 0;
    }

    for (int i = 0; i < GBL_AFFECTS_TIMED_OUT; i++) {
        gbl.affects_timed_out[i] = true;
    }

    if (interactive_resting) {
        frames_clear_region(TEXT_REGION_NORMAL_BOTTOM);
        display_resting_time(0);
    }

    gbl.display_player_status_line18 = true;

    if (interactive_resting) {
        stop_resting = !resting_time_menu();
    } else {
        stop_resting = false;
    }

    while (!stop_resting &&
           (gbl.time_to_rest.slot[REST_SLOT_DAYS] > 0 ||
            gbl.time_to_rest.slot[REST_SLOT_HOURS] > 0 ||
            gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] > 0 ||
            gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] > 0)) {
        if (interactive_resting && input_key_pressed()) {
            display_resting_time(0);

            if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Stop Resting? ") == 'Y') {
                stop_resting = true;
            } else {
                prompt_clear_area();
            }
        }

        if (!stop_resting) {
            resting_subtract_rest_time(REST_SLOT_MINUTES_ONES, 5);
            display_counter++;

            /* The clock on screen is redrawn every half hour, not every step. */
            if (interactive_resting && display_counter >= 5) {
                display_resting_time(0);
                display_counter = 0;
            }

            resting_step_game_time(REST_SLOT_MINUTES_ONES, 5);
            rest_heal(interactive_resting);
            check_for_spell_learning();
            spell_learning_tick(&learning_counter);

            if (gbl.area2_ptr->rest_encounter_period > 0) {
                gbl.rest_encounter_count++;

                if (gbl.rest_encounter_count >=
                    gbl.area2_ptr->rest_encounter_period) {
                    gbl.rest_encounter_count = 0;

                    if (effect_roll_dice(100, 1) <=
                        gbl.area2_ptr->rest_encounter_percentage) {
                        character_clear_text_area();
                        display_resting_time(0);
                        text_display_string("Your repose is suddenly interrupted!",
                                            0, 15, 0x13, 1);
                        stop_resting = true;
                        resting_interrupted = true;
                        text_game_delay();
                    }
                }
            }
        }
    }

    frames_clear_region(TEXT_REGION_NORMAL_BOTTOM);
    gbl.display_player_status_line18 = false;

    return resting_interrupted;
}
