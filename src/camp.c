/* camp.c - Ported from engine/ovr016.cs. See camp.h. */

#include "camp.h"

#include <stdio.h>
#include <string.h>

#include "affect.h"
#include "character.h"
#include "effect.h"
#include "enums.h"
#include "frames.h"
#include "gbl.h"
#include "item.h"
#include "log.h"
#include "menu.h"
#include "partymenu.h"
#include "picture.h"
#include "prompt.h"
#include "quit.h"
#include "resting.h"
#include "resttime.h"
#include "savegame.h"
#include "set.h"
#include "spellcast.h"
#include "spelllist.h"
#include "spells.h"
#include "text.h"
#include "viewplayer.h"

/* ovr016.AlterSet and ovr016.unk_463F4, both `new Set(0, 69)`: Escape and 'E'
 * for Exit. Three menus here loop until one of the two arrives, and all three
 * used one or the other of these identical sets. */
static bool exit_key(char input_key)
{
    Set exit_keys;

    set_clear(&exit_keys);
    set_add(&exit_keys, 0);
    set_add(&exit_keys, 69);

    return set_member_of(&exit_keys, (int)(unsigned char)input_key);
}

/* ------------------------------------------------ how long the party needs */

/* ovr016.sub_44032 */
int camp_spell_learn_time(Player *player)
{
    int max_spell_level = 0;
    int total_spell_level = 0;
    int max_scribe_level = 0;
    int total_scribe_level = 0;
    u8  count = 0;

    if (player == NULL) {
        log_warn("camp: no character to work out a rest for");
        return 0;
    }

    /* The spells lined up for memorising. */
    for (int i = 0; i < player->spell_list.count; i++) {
        const SpellEntry *entry;
        int level;

        if (player->spell_list.items[i].learning == false) {
            continue;
        }

        entry = spell_entry(player->spell_list.items[i].id);
        level = (entry != NULL) ? entry->spell_level : 0;

        if (level > max_spell_level) {
            max_spell_level = level;
        }
        total_spell_level += level;
    }

    /* And the ones marked to be copied off a scroll, which is bit 7 of a name
     * part's affect. */
    for (int i = 0; i < player->item_count; i++) {
        Item *item = &player->items[i];

        if (item_is_scroll(item) == false) {
            continue;
        }

        for (int part = 1; part <= 3; part++) {
            const SpellEntry *entry;
            int level;

            if ((int)item_affect(item, part) <= 0x7f) {
                continue;
            }

            entry = spell_entry((int)item_affect(item, part) & 0x7f);
            level = (entry != NULL) ? entry->spell_level : 0;

            if (level > max_scribe_level) {
                max_scribe_level = level;
            }
            total_scribe_level += level;
        }
    }

    /* Four minutes of settling down for any work at all, six if any of it is
     * above 2nd level. That is also the per-spell delay resting.c counts down,
     * so a 3rd-level spell takes longer to come back than a 1st. */
    if (total_spell_level > 0 || total_scribe_level > 0) {
        count = 4;
    }
    if (max_spell_level > 2 || max_scribe_level > 2) {
        count = 6;
    }

    player->spell_to_learn_count = count;

    return (count * 0x3c) + (total_scribe_level * 0x0f) +
           (total_spell_level * 0x0f);
}

/* ovr016.cancel_memorize */
static void cancel_memorize(Player *player)
{
    if (player == NULL) {
        return;
    }

    spell_list_cancel_learning(&player->spell_list);
    player->spell_to_learn_count = 0;
}

/* ovr016.cancel_scribes. Clears the "still to copy" bit on every scroll, which
 * puts the scroll back the way it was: the spell is still on it, just no longer
 * queued. */
static void cancel_scribes(Player *player)
{
    if (player == NULL) {
        return;
    }

    for (int i = 0; i < player->item_count; i++) {
        Item *item = &player->items[i];

        if (item_is_scroll(item) == false) {
            continue;
        }

        item->affect_1 &= 0x7f;
        item->affect_2 &= 0x7f;
        item->affect_3 &= 0x7f;
    }
}

/* ovr016.cancel_spells */
void camp_cancel_spells(void)
{
    for (int i = 0; i < gbl.team_count; i++) {
        cancel_memorize(gbl.team_list[i]);
        cancel_scribes(gbl.team_list[i]);
    }
}

/* ------------------------------------------------------- what may be learnt */

/* ovr016.HowManySpellsPlayerCanLearn, sub_4428E */
int camp_spells_can_learn(SpellClass spell_class, int spell_level)
{
    Player *player = gbl.selected_player;
    int already_learning = 0;

    if (player == NULL) {
        log_warn("camp: nobody is selected, so nothing can be memorized");
        return 0;
    }
    if ((int)spell_class < 0 || (int)spell_class > SPELL_CLASS_MAGIC_USER ||
        spell_level < 1 || spell_level > 5) {
        /* The C# indexed spellCastCount straight with both, and a monster's spell
         * class or a 6th-level spell would have thrown. */
        log_warn("camp: no memorizing slots for class %d level %d",
                 (int)spell_class, spell_level);
        return 0;
    }

    /* Everything already on the list counts, memorised or not, so a character
     * cannot line up more than their level allows even in one sitting. */
    for (int i = 0; i < player->spell_list.count; i++) {
        const SpellEntry *entry = spell_entry(player->spell_list.items[i].id);

        if (entry != NULL && entry->spell_level == spell_level &&
            entry->spell_class == spell_class) {
            already_learning++;
        }
    }

    return player->spell_cast_count[(int)spell_class][spell_level - 1] -
           already_learning;
}

/* ovr016.sub_443A0 */
bool camp_can_use_spells(u8 learn_type)
{
    char text[64];

    text[0] = '\0';

    if (learn_type == 1) {
        /* Area1.can_cast_spells is the wrong way round: the flag is set for an
         * area where spells do not work. ovr008, ovr009 and ovr010 all test it
         * for false before offering Cast, and this is the message that goes with
         * it being true.
         *
         * Note what this costs: for casting - and only for casting - the health
         * check below is never reached, so a character who is animated or out of
         * the fight is still offered the spell list. The refusal comes later,
         * from the casting code itself. */
        if (gbl.area_ptr->can_cast_spells == true) {
            snprintf(text, sizeof(text), "cannot cast spells in this area");
        }
    } else if (gbl.selected_player != NULL &&
               (gbl.selected_player->health_status == STATUS_ANIMATED ||
                gbl.selected_player->in_combat == false)) {
        snprintf(text, sizeof(text), "is in no condition to ");

        switch (learn_type) {
        case 1:
            /* Dead code in the original too: learn_type 1 took the branch
             * above. */
            strncat(text, "cast any spells", sizeof(text) - strlen(text) - 1);
            break;

        case 2:
            strncat(text, "memorize spells", sizeof(text) - strlen(text) - 1);
            break;

        case 3:
            strncat(text, "scribe any scrolls", sizeof(text) - strlen(text) - 1);
            break;

        default:
            break;
        }
    }

    if (text[0] != '\0') {
        character_display_status_string(true, 10, text, gbl.selected_player);
        return false;
    }

    return true;
}

/* ---------------------------------------------------------------- casting */

/* ovr016.cast_spell */
void camp_cast_spell(void)
{
    bool redraw = false;

    gbl.last_selected_spell_target = NULL;
    gbl.menu_selected_word = 1;

    if (camp_can_use_spells(1) == true) {
        u8 spell_id;
        int index = -1;

        do {
            bool has_spells = false;

            spell_id = viewplayer_spell_menu2(&has_spells, &index,
                                              SPELL_SOURCE_CAST,
                                              SPELL_LOC_MEMORY);

            if (spell_id != 0) {
                redraw = true;
                frames_clear_region(TEXT_REGION_NORMAL_BOTTOM);

                spellcast_resolve_spell(true, QUICK_FIGHT_FALSE, spell_id);
            } else if (has_spells == true) {
                /* The player looked at the list and backed out; the list has
                 * still been drawn over the picture. */
                redraw = true;
            } else {
                character_display_status_string(true, 10, "has no spells memorized",
                                                gbl.selected_player);
            }
        } while (spell_id != 0);
    }

    if (redraw == true) {
        character_load_pic();
    }
}

/* ------------------------------------------------------------- memorising */

/* ovr016.BuildMemorizeSpellText, sub_445D4. The three rows of five numbers under
 * "can memorize:", one row per casting class and one column per spell level.
 * False when this character casts nothing at all, which is the answer that ends
 * the memorising loop. */
static bool build_memorize_spell_text(void)
{
    /* MaxSpellClass and MaxSpellLevel, the C#'s own two constants. */
    enum { MAX_SPELL_LEVEL = 5, MAX_SPELL_CLASS = 3 };
    char counts[MAX_SPELL_CLASS][MAX_SPELL_LEVEL][8];
    bool can_learn_class[MAX_SPELL_CLASS];
    bool found = false;

    for (int spell_class = 0; spell_class < MAX_SPELL_CLASS; spell_class++) {
        can_learn_class[spell_class] = false;

        for (int spell_level = 0; spell_level < MAX_SPELL_LEVEL; spell_level++) {
            snprintf(counts[spell_class][spell_level],
                     sizeof(counts[spell_class][spell_level]), "%d",
                     camp_spells_can_learn((SpellClass)spell_class,
                                           spell_level + 1));

            /* A level the character has no slots at all for is blank rather than
             * a zero, so the row shows only the levels they can cast. A zero
             * means "slots, but all of them already spoken for". */
            if (gbl.selected_player == NULL ||
                gbl.selected_player->spell_cast_count[spell_class][spell_level]
                    == 0) {
                snprintf(counts[spell_class][spell_level],
                         sizeof(counts[spell_class][spell_level]), " ");
            } else {
                can_learn_class[spell_class] = true;
                found = true;
            }
        }
    }

    if (found == true) {
        int y_col = 3;

        character_display_status_string(false, 10, "can memorize:",
                                        gbl.selected_player);

        for (int spell_class = 0; spell_class < MAX_SPELL_CLASS; spell_class++) {
            const char *text;
            int x_col = 0x13;

            if (can_learn_class[spell_class] == false) {
                continue;
            }

            switch (spell_class) {
            case SPELL_CLASS_CLERIC:
                text = "    Cleric Spells:";
                break;

            case SPELL_CLASS_DRUID:
                text = "     Druid Spells:";
                break;

            default:
                text = "Magic-User Spells:";
                break;
            }

            text_display_string(text, 0, 10, y_col + 17, 1);

            for (int spell_level = 0; spell_level < MAX_SPELL_LEVEL;
                 spell_level++) {
                text_display_string(counts[spell_class][spell_level], 0, 10,
                                    y_col + 0x11, x_col + 1);
                x_col += 3;
            }

            y_col++;
        }
    }

    return found;
}

/* ovr016.rest_menu */
bool camp_rest_menu(void)
{
    int max_rest_time = 0;
    bool action_interrupted;

    /* The party rests for as long as its slowest member needs. Everybody's
     * spell_to_learn_count is set on the way past, which is what resting.c
     * counts down. */
    for (int i = 0; i < gbl.team_count; i++) {
        int rest_time = camp_spell_learn_time(gbl.team_list[i]);

        if (max_rest_time < rest_time) {
            max_rest_time = rest_time;
        }
    }

    gbl.time_to_rest.slot[REST_SLOT_HOURS] = max_rest_time / 60;
    gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] =
        (max_rest_time - (gbl.time_to_rest.slot[REST_SLOT_HOURS] * 60)) / 10;
    /* Note the ones digit is the whole minute count modulo ten, not what is left
     * after the tens above: for 47 minutes that is 4 tens and 7 ones, which
     * agrees, but only because both are worked out from the same figure. */
    gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] = max_rest_time % 10;

    action_interrupted = resting_run(true);

    rest_time_clear(&gbl.time_to_rest);
    character_display_map_position_time();

    return action_interrupted;
}

/* ovr016.memorize_spell */
void camp_memorize_spell(void)
{
    bool has_spells = false;

    if (camp_can_use_spells(2) == false) {
        return;
    }

    {
        bool done = false;
        int index = -1;
        bool redraw = true;

        gbl.menu_selected_word = 1;

        /* What is already lined up, if anything: the player is asked to confirm
         * it before being offered the chance to add to it. */
        viewplayer_spell_menu2(&has_spells, &index, (SpellSource)0,
                               SPELL_LOC_MEMORIZE);

        if (has_spells == true) {
            if (prompt_yes_no(GBL_ALERT_MENU_COLORS,
                              "Memorize These Spells? ") == 'N') {
                cancel_memorize(gbl.selected_player);
            } else {
                done = true;
            }
        } else {
            redraw = false;
        }

        index = -1;

        while (done == false) {
            done = (build_memorize_spell_text() == false);

            if (done == true) {
                character_display_status_string(true, 10,
                                                "cannot memorize any spells",
                                                gbl.selected_player);
            } else {
                u8 spell_id = viewplayer_spell_menu2(&has_spells, &index,
                                                     SPELL_SOURCE_MEMORIZE,
                                                     SPELL_LOC_GRIMOIRE);
                const SpellEntry *entry = spell_entry(spell_id);

                redraw = true;

                if (spell_id == 0) {
                    done = true;
                } else if (entry != NULL &&
                           camp_spells_can_learn(entry->spell_class,
                                                 entry->spell_level) > 0) {
                    spell_list_add_learn(&gbl.selected_player->spell_list,
                                         spell_id);
                }
            }
        }

        /* The list was opened at least once, so show what came of it and give the
         * player one more chance to throw the lot away. The prompt is spelt with
         * a small t here and a capital one above; both are the original's. */
        if (index != -1) {
            index = -1;

            viewplayer_spell_menu2(&has_spells, &index, (SpellSource)0,
                                   SPELL_LOC_MEMORIZE);

            if (has_spells == true &&
                prompt_yes_no(GBL_ALERT_MENU_COLORS,
                              "Memorize these spells? ") == 'N') {
                cancel_memorize(gbl.selected_player);
            }
        }

        if (redraw == true) {
            character_load_pic();
        }
    }
}

/* ovr016.scribe_spell */
void camp_scribe_spell(void)
{
    bool has_spells = false;
    bool done = false;
    bool redraw = true;
    int index = -1;

    if (camp_can_use_spells(3) == false) {
        return;
    }

    viewplayer_spell_menu2(&has_spells, &index, (SpellSource)0,
                           SPELL_LOC_SCRIBE);

    if (has_spells == true) {
        if (prompt_yes_no(GBL_ALERT_MENU_COLORS, "Scribe These Spells? ") == 'N') {
            cancel_scribes(gbl.selected_player);
        } else {
            done = true;
        }
    } else {
        redraw = false;
    }

    index = -1;

    while (done == false) {
        int spell_id = viewplayer_spell_menu2(&has_spells, &index,
                                             SPELL_SOURCE_SCRIBE,
                                             SPELL_LOC_SCROLLS);

        if (spell_id == 0) {
            done = true;

            if (has_spells == false) {
                character_display_status_string(true, 10,
                                                "has no copyable scrolls",
                                                gbl.selected_player);
            } else {
                redraw = true;
            }
        } else {
            bool already_queued;

            redraw = true;

            if (player_knows_spell(gbl.selected_player, (Spells)spell_id)) {
                character_print_message("You already know that spell");
                continue;
            }

            already_queued = false;
            for (int i = 0; i < gbl.selected_player->item_count &&
                            already_queued == false; i++) {
                Item *item = &gbl.selected_player->items[i];

                already_queued = (item_is_scroll(item) == true &&
                                  (item_scroll_learning(item, 1, spell_id) ||
                                   item_scroll_learning(item, 2, spell_id) ||
                                   item_scroll_learning(item, 3, spell_id)));
            }

            if (already_queued == true) {
                /* "scibing" is the original's spelling. */
                character_print_message("You are already scibing that spell");
            } else {
                const SpellEntry *entry = spell_entry(spell_id);
                int spell_level = (entry != NULL) ? entry->spell_level : 0;
                int spell_class = (entry != NULL) ? (int)entry->spell_class : 0;

                if (entry != NULL && spell_class >= 0 &&
                    spell_class <= SPELL_CLASS_MAGIC_USER &&
                    spell_level >= 1 && spell_level <= 5 &&
                    gbl.selected_player->spell_cast_count[spell_class]
                                                         [spell_level - 1] > 0) {
                    bool marked = false;

                    /* Marks the spell on the scroll it came off. Two things to
                     * note, both the original's: every item in the pack is
                     * looked at rather than only the scrolls, so a non-scroll
                     * whose affect happens to equal this spell id would be
                     * marked instead; and the inner loop stops on the first
                     * match, so a scroll carrying the same spell twice only ever
                     * queues one copy of it. */
                    for (int i = 0; i < gbl.selected_player->item_count &&
                                    marked == false; i++) {
                        Item *item = &gbl.selected_player->items[i];
                        int part = 1;

                        do {
                            if ((int)item_affect(item, part) == spell_id) {
                                item_affect_set(item, part,
                                                (Affects)((int)item_affect(item, part)
                                                          | 0x80));
                                marked = true;
                            }

                            part++;
                        } while (part <= 3 && marked == false);
                    }
                } else {
                    character_print_message("You can not scribe that spell.");
                }
            }
        }
    }

    if (index != -1) {
        index = -1;

        viewplayer_spell_menu2(&has_spells, &index, (SpellSource)0,
                               SPELL_LOC_SCRIBE);

        if (has_spells == true &&
            prompt_yes_no(GBL_ALERT_MENU_COLORS, "Scribe these spells? ") == 'N') {
            cancel_scribes(gbl.selected_player);
        }
    }

    if (redraw == true) {
        character_load_pic();
    }
}

/* -------------------------------------------------- what a character is under */

/* ovr016.EffectNameMap. The C# was a Dictionary<Affects, string> built once at
 * startup; this is the same map as a flat array, scanned linearly. Fifty-four
 * entries is what BuildEffectNameMap puts in it and there is no lookup here that
 * a scan is too slow for - the Display screen builds one list and stops. */
#define EFFECT_NAME_MAX  64
#define EFFECT_NAME_TEXT 32

typedef struct {
    Affects type;
    char    name[EFFECT_NAME_TEXT];
} EffectName;

static EffectName g_effect_names[EFFECT_NAME_MAX];
static int        g_effect_name_count;

const char *camp_effect_name(Affects type)
{
    for (int i = 0; i < g_effect_name_count; i++) {
        if (g_effect_names[i].type == type) {
            return g_effect_names[i].name;
        }
    }

    return NULL;
}

/* Dictionary.Add, which threw on a key already in the map. The list it is built
 * from is fixed, so a duplicate would be a porting mistake rather than something
 * a player could cause: it is logged and dropped, leaving the first name in
 * place, which is the entry the C# would have kept had it not thrown. */
static void effect_name_add(Affects type, const char *name)
{
    if (camp_effect_name(type) != NULL) {
        log_warn("camp: affect 0x%02x is already named \"%s\", not \"%s\"",
                 (unsigned)type, camp_effect_name(type), name);
        return;
    }
    if (g_effect_name_count >= EFFECT_NAME_MAX) {
        log_warn("camp: no room for \"%s\" in the affect name map", name);
        return;
    }

    g_effect_names[g_effect_name_count].type = type;
    snprintf(g_effect_names[g_effect_name_count].name, EFFECT_NAME_TEXT, "%s",
             name);
    g_effect_name_count++;
}

/* ovr016.BuildEffectNameMap */
void camp_build_effect_name_map(void)
{
    /* The affects that are named after the spell that lays them, so the name
     * comes out of the spell table rather than being spelt out here. The second
     * column is the C# enum's own spelling, which is what its `"Funky--" +
     * aff.ToString()` fallback printed; C has no way to recover an enumerator's
     * name, so the ones that can reach that fallback are written out. */
    static const struct { Affects type; const char *enum_name; } SPELL_NAMED[] = {
        { AFFECT_BLESS,                     "bless" },
        { AFFECT_CURSED,                    "cursed" },
        { AFFECT_DETECT_MAGIC,              "detect_magic" },
        { AFFECT_PROTECTION_FROM_EVIL,      "protection_from_evil" },
        { AFFECT_PROTECTION_FROM_GOOD,      "protection_from_good" },
        { AFFECT_RESIST_COLD,               "resist_cold" },
        { AFFECT_CHARM_PERSON,              "charm_person" },
        { AFFECT_ENLARGE,                   "enlarge" },
        { AFFECT_FRIENDS,                   "friends" },
        { AFFECT_READ_MAGIC,                "read_magic" },
        { AFFECT_SHIELD,                    "shield" },
        { AFFECT_FIND_TRAPS,                "find_traps" },
        { AFFECT_RESIST_FIRE,               "resist_fire" },
        { AFFECT_SILENCE_15_RADIUS,         "silence_15_radius" },
        { AFFECT_SLOW_POISON,               "slow_poison" },
        { AFFECT_SPIRITUAL_HAMMER,          "spiritual_hammer" },
        { AFFECT_DETECT_INVISIBILITY,       "detect_invisibility" },
        { AFFECT_INVISIBILITY,              "invisibility" },
        { AFFECT_MIRROR_IMAGE,              "mirror_image" },
        { AFFECT_RAY_OF_ENFEEBLEMENT,       "ray_of_enfeeblement" },
        { AFFECT_ANIMATE_DEAD,              "animate_dead" },
        { AFFECT_BLINDED,                   "blinded" },
        { AFFECT_CAUSE_DISEASE_1,           "cause_disease_1" },
        { AFFECT_BESTOW_CURSE,              "bestow_curse" },
        { AFFECT_BLINK,                     "blink" },
        { AFFECT_STRENGTH,                  "strength" },
        { AFFECT_HASTE,                     "haste" },
        { AFFECT_PROT_FROM_NORMAL_MISSILES, "prot_from_normal_missiles" },
        { AFFECT_SLOW,                      "slow" },
        { AFFECT_PROT_FROM_EVIL_10_RADIUS,  "prot_from_evil_10_radius" },
        { AFFECT_PROT_FROM_GOOD_10_RADIUS,  "prot_from_good_10_radius" },
        { AFFECT_PRAYER,                    "prayer" },
        { AFFECT_SNAKE_CHARM,               "snake_charm" },
        { AFFECT_PARALYZE,                  "paralyze" },
        { AFFECT_SLEEP,                     "sleep" }
    };

    g_effect_name_count = 0;

    for (size_t i = 0; i < COAB_ARRAY_LEN(SPELL_NAMED); i++) {
        bool found = false;

        /* 0x38 is where the cleric and druid spells end and the magic-user list
         * begins, so only a spell a character could have memorized is looked
         * at. */
        for (int spell_id = 1; spell_id <= 0x38 && found == false; spell_id++) {
            const SpellEntry *entry = spell_entry(spell_id);

            if (entry != NULL && entry->affect_id == SPELL_NAMED[i].type) {
                effect_name_add(SPELL_NAMED[i].type,
                                spellcast_spell_name(spell_id));
                found = true;
            }
        }

        if (found == false) {
            /* One affect on the list reaches this with the shipped table:
             * animate_dead, which nothing below 0x38 lays - the only spell with
             * that affect is 0x5a, which is a monster's. A character can still be
             * under it, from ovr023's "is animated", so this is a name a player
             * can see, and it is the C#'s: "Funky--animate_dead". */
            char funky[EFFECT_NAME_TEXT];

            snprintf(funky, sizeof(funky), "Funky--%s", SPELL_NAMED[i].enum_name);

            if (SPELL_NAMED[i].type != AFFECT_ANIMATE_DEAD) {
                /* Any other one means the spell table has been edited. */
                log_warn("camp: no spell lays affect 0x%02x (%s)",
                         (unsigned)SPELL_NAMED[i].type, SPELL_NAMED[i].enum_name);
            }

            effect_name_add(SPELL_NAMED[i].type, funky);
        }
    }

    /* And the ones with no spell of their own, or whose spell is named something
     * else. The lower-case ones read as the ends of sentences the original never
     * built: the Display screen simply lists them as they are. */
    effect_name_add(AFFECT_DISPEL_EVIL, "Dispel Evil");
    effect_name_add(AFFECT_FAERIE_FIRE, "Faerie Fire");
    effect_name_add(AFFECT_FUMBLING, "Fumbling");
    effect_name_add(AFFECT_HELPLESS, "Helpless");
    effect_name_add(AFFECT_CONFUSE, "Confused");
    effect_name_add(AFFECT_CAUSE_DISEASE_2, "Cause Disease");
    effect_name_add(AFFECT_HOT_FIRE_SHIELD, "Hot Fire Shield");
    effect_name_add(AFFECT_COLD_FIRE_SHIELD, "Cold Fire Shield");
    effect_name_add(AFFECT_POISONED, "Poisoned");
    effect_name_add(AFFECT_REGENERATE, "Regenerating");
    effect_name_add(AFFECT_FIRE_RESIST, "Fire Resistance");
    effect_name_add(AFFECT_MINOR_GLOBE_OF_INVULN,
                    "Minor Globe of Invulnerability");
    effect_name_add(AFFECT_FEEBLEMIND, "enfeebled");
    effect_name_add(AFFECT_INVISIBLE_TO_ANIMALS, "invisible to animals");
    effect_name_add(AFFECT_INVISIBLE, "Invisible");
    effect_name_add(AFFECT_CAMOUFLAGE, "Camouflaged");
    effect_name_add(AFFECT_PROT_DRAG_BREATH, "protected from dragon breath");
    effect_name_add(AFFECT_BERSERK, "berserk");
    effect_name_add(AFFECT_DISPLACE, "Displaced");
}

/* ovr016.DisplayMagicEffects. Everybody's affects at once, as a list that can be
 * scrolled but not picked from. An affect with no name in the map is not shown at
 * all, which is how the internal ones - the poison counters, the monster
 * attacks - stay off the screen. */
static void display_magic_effects(void)
{
    /* A MenuList is about 10K; see PORTING.md. */
    static MenuList list;
    static const MenuColorSet colors = { 15, 10, 11 };
    MenuItem *chosen = NULL;
    bool redraw = true;
    int index = 0;

    menu_list_clear(&list);
    /* The blank first entry the C# opened the list with: the list is drawn from
     * row 4 and this is what keeps the first name off the frame. */
    menu_list_add(&list, "");

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];
        bool any_effects = false;

        if (player == NULL) {
            continue;
        }

        menu_list_add_heading(&list, player->name);

        for (int j = 0; j < player->affects.count; j++) {
            const char *affect_name =
                camp_effect_name((Affects)player->affects.items[j].type);

            if (affect_name != NULL) {
                char text[MENU_ITEM_TEXT_MAX];

                any_effects = true;
                snprintf(text, sizeof(text), " %s", affect_name);
                menu_list_add(&list, text);
            }
        }

        if (any_effects == false) {
            menu_list_add(&list, " <No Spell Effects>");
        }

        menu_list_add(&list, " ");
    }

    frames_draw_outer();

    prompt_select_item(&chosen, &index, &redraw, true, &list, 0x16, 0x26, 4, 1,
                       colors, "", "");

    menu_list_clear(&list);
    character_load_pic();
}

/* ovr016.magic_menu */
bool camp_magic_menu(void)
{
    char input_key = ' ';
    bool action_interrupted = false;

    while (action_interrupted == false && exit_key(input_key) == false) {
        bool control_key = false;

        input_key = prompt_display_input(&control_key, true,
                                        PROMPT_CTRL_WORD_ARROWS,
                                        GBL_DEFAULT_MENU_COLORS,
                                        "Cast Memorize Scribe Display Rest Exit",
                                        "");

        if (control_key == true) {
            viewplayer_scroll_team_list(input_key);
            character_party_summary(gbl.selected_player);
        } else {
            switch (input_key) {
            case 'C':
                camp_cast_spell();
                break;

            case 'M':
                camp_memorize_spell();
                break;

            case 'S':
                camp_scribe_spell();
                break;

            case 'D':
                display_magic_effects();
                break;

            case 'R':
                action_interrupted = camp_rest_menu();
                break;

            default:
                break;
            }
        }
    }

    return action_interrupted;
}

/* ------------------------------------------------------- the marching order */

/* ovr016.MoveCurrentPlayerUp, sub_4558D. The selected character swaps with the
 * one in front, and the one at the front goes to the back. */
static void move_current_player_up(void)
{
    int index = gbl_team_index_of(gbl.selected_player);

    if (index < 0) {
        return;
    }

    gbl_team_remove_at(index);

    if (index > 0) {
        gbl_team_insert(index - 1, gbl.selected_player);
    } else {
        gbl_team_add(gbl.selected_player);
    }
}

/* ovr016.MoveCurrentPlayerDown, sub_456E5. And the other way, the back of the
 * line wrapping round to the front. */
static void move_current_player_down(void)
{
    int index = gbl_team_index_of(gbl.selected_player);

    if (index < 0) {
        return;
    }

    gbl_team_remove_at(index);

    /* The list is one shorter now, so index == team_count means the character was
     * at the back of it. */
    if (index == gbl.team_count) {
        gbl_team_insert(0, gbl.selected_player);
    } else {
        gbl_team_insert(index + 1, gbl.selected_player);
    }
}

/* ovr016.reorderSet, a Set(13, 80, 83): Return, 'P' for Place and 'S' for
 * Select, the three keys that toggle the menu between picking a character and
 * putting them down. */
static bool reorder_key(char input_key)
{
    Set keys;

    set_clear(&keys);
    set_add(&keys, 13);
    set_add(&keys, 80);
    set_add(&keys, 83);

    return set_member_of(&keys, (int)(unsigned char)input_key);
}

/* ovr016.reorder_party */
static void reorder_party(void)
{
    /* ovr016.reorderStrings, seg600_04A6. */
    static const char *const REORDER_STRINGS[2] = { "Select Exit", "Place Exit" };
    int reorder_state = 0;
    char input_key = ' ';

    while (exit_key(input_key) == false) {
        bool control_key = false;

        input_key = prompt_display_input(&control_key, true,
                                        PROMPT_CTRL_WORD_ARROWS,
                                        GBL_DEFAULT_MENU_COLORS,
                                        REORDER_STRINGS[reorder_state],
                                        "Party Order: ");

        if (control_key == true) {
            /* 0x47 and 0x4f are 'G' and 'O', the same two keys the rest of the
             * game moves a selection with, so the arrows stand in for them here
             * as well: see prompt_selection_key. */
            char order_key = prompt_selection_key(input_key, control_key);

            if (reorder_state == 0) {
                viewplayer_scroll_team_list(order_key);
            } else if (order_key == 0x47) {         /* Home or Up, up the order */
                move_current_player_up();
            } else if (order_key == 0x4f) {         /* End or Down, down it */
                move_current_player_down();
            }

            character_party_summary(gbl.selected_player);
        } else if (reorder_key(input_key) == true) {
            reorder_state = (reorder_state == 0) ? 1 : 0;

            if (reorder_state != 0) {
                character_display_status_string(false, 10, "has been selected",
                                                gbl.selected_player);
            } else {
                character_clear_text_area();
            }
        }
    }
}

/* ovr016.DropPlayer, drop_player. Dropping the last member of the party is
 * quitting the game, which is why this is the only Drop that asks about DOS. */
static void drop_player(void)
{
    if (gbl.team_count == 1) {
        if (prompt_yes_no(GBL_ALERT_MENU_COLORS, "quit TO DOS: ") == 'Y') {
            partymenu_free_current_player(gbl.team_list[0], true, false);
            game_print_and_exit();
        }

        return;
    }

    character_display_status_string(false, 10, "will be gone",
                                    gbl.selected_player);

    if (prompt_yes_no(GBL_ALERT_MENU_COLORS, "Drop from party? ") == 'Y') {
        if (gbl.selected_player != NULL &&
            gbl.selected_player->in_combat == true) {
            character_display_status_string(true, 10, "bids you farewell",
                                            gbl.selected_player);
        } else {
            character_display_status_string(true, 10, "is dumped in a ditch",
                                            gbl.selected_player);
        }

        gbl.selected_player = partymenu_free_current_player(gbl.selected_player,
                                                           true, false);
        frames_clear_area(0x0b, 0x26, 1, 0x11);

        character_party_summary(gbl.selected_player);
    } else {
        character_display_status_string(true, 10, "Breathes A sigh of relief",
                                        gbl.selected_player);
    }
}

/* ovr016.game_speed */
void camp_game_speed(void)
{
    char input_key;

    do {
        char text[64];
        char menu_text[32];
        bool control_key = false;

        snprintf(text, sizeof(text), "Game Speed = %d (0=fastest 9=slowest)",
                 gbl.game_speed_var);
        text_display_string(text, 0, 10, 18, 1);

        /* The words are only offered while there is somewhere to go, which is
         * what keeps the two letter cases below in range: they do not check.
         * The leading spaces are the original's. */
        menu_text[0] = '\0';
        if (gbl.game_speed_var > 0) {
            strncat(menu_text, " Faster", sizeof(menu_text) - strlen(menu_text) - 1);
        }
        if (gbl.game_speed_var < 9) {
            strncat(menu_text, " Slower", sizeof(menu_text) - strlen(menu_text) - 1);
        }
        strncat(menu_text, " Exit", sizeof(menu_text) - strlen(menu_text) - 1);

        /* Up and down are the speed here, but left and right are spare, so they
         * walk the highlight the way ',' and '.' do. */
        input_key = prompt_display_input(&control_key, true,
                                        PROMPT_CTRL_WORD_ARROWS,
                                        GBL_DEFAULT_MENU_COLORS, menu_text,
                                        "Game Speed:");

        if (control_key == true) {
            if (input_key == 0x50) {                /* down: faster */
                if (gbl.game_speed_var > 0) {
                    gbl.game_speed_var--;
                }
            } else if (input_key == 0x48) {         /* up: slower */
                if (gbl.game_speed_var < 9) {
                    gbl.game_speed_var++;
                }
            }
        } else if (input_key == 0x46) {             /* 'F' for Faster */
            gbl.game_speed_var--;
        } else if (input_key == 0x53) {             /* 'S' for Slower */
            gbl.game_speed_var++;
        }
    } while (exit_key(input_key) == false);

    character_clear_text_area();
}

/* ovr016.alter_menu */
void camp_alter_menu(void)
{
    char input_key = ' ';

    while (exit_key(input_key) == false) {
        bool control_key = false;

        input_key = prompt_display_input(&control_key, true,
                                        PROMPT_CTRL_WORD_ARROWS,
                                        GBL_DEFAULT_MENU_COLORS,
                                        "Order Drop Speed Icon Exit", "Alter: ");

        if (control_key == true) {
            viewplayer_scroll_team_list(input_key);
            character_party_summary(gbl.selected_player);
        } else {
            switch (input_key) {
            case 'O':
                reorder_party();
                break;

            case 'D':
                drop_player();
                break;

            case 'S':
                camp_game_speed();
                break;

            case 'I':
                partymenu_icon_builder();
                character_load_pic();
                break;

            default:
                break;
            }
        }
    }
}

/* --------------------------------------------------------- binding wounds */

/* ovr016.CalculateInitialHealing, sub_45F22. What the party's already-memorised
 * cure spells are worth, rolled now rather than when they are spent. */
static int calculate_initial_healing(void)
{
    int healing_available = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL || player->health_status != STATUS_OKEY) {
            continue;
        }

        for (int j = 0; j < player->spell_list.count; j++) {
            if (player->spell_list.items[j].learning == true) {
                continue;
            }

            switch (player->spell_list.items[j].id) {
            case SPELL_CURE_LIGHT_WOUNDS:
                healing_available += effect_roll_dice(8, 1);
                break;

            case SPELL_CURE_SERIOUS_WOUNDS:
                healing_available += effect_roll_dice(8, 2) + 1;
                break;

            case SPELL_CURE_CRITICAL_WOUNDS:
                healing_available += effect_roll_dice(8, 3) + 3;
                break;

            default:
                break;
            }
        }
    }

    return healing_available;
}

/* ovr016.CalculateHealing, sub_45FDD. And what the cures memorised during the
 * rest just now are worth, on top of it. */
static void calculate_healing(int *healing_available, int num_cure_light,
                              int num_cure_serious, int num_cure_critical)
{
    for (int i = 0; i < num_cure_light; i++) {
        *healing_available += effect_roll_dice(8, 1);
    }
    for (int i = 0; i < num_cure_serious; i++) {
        *healing_available += effect_roll_dice(8, 2) + 1;
    }
    for (int i = 0; i < num_cure_critical; i++) {
        *healing_available += effect_roll_dice(8, 3) + 3;
    }
}

/* ovr016.TotalHitpointsLost, sub_4608F */
static int total_hitpoints_lost(void)
{
    int lost_points = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player != NULL) {
            lost_points += player->hit_point_max - player->hit_point_current;
        }
    }

    return lost_points;
}

/* ovr016.CalculateTimeAndSpellNumbers, sub_460ED */
void camp_calculate_time_and_spell_numbers(int *out_cure_critical,
                                           int *out_cure_serious,
                                           int *out_cure_light)
{
    int num_cure_light = 0;
    int num_cure_serious = 0;
    int num_cure_critical = 0;
    int max_healing = 0;
    int max_time = 0;
    int lost_points;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];
        int time_needed = 0;
        int light_time = 0;
        int serious_time = 0;
        int critical_time = 0;

        if (player != NULL && player->health_status == STATUS_OKEY) {
            /* Only the cleric column, and only the three cure levels: 1st for
             * cure light, 4th for serious, 5th for critical. */
            num_cure_light += player->spell_cast_count[SPELL_CLASS_CLERIC][0];
            light_time = player->spell_cast_count[SPELL_CLASS_CLERIC][0] * 15;

            num_cure_serious += player->spell_cast_count[SPELL_CLASS_CLERIC][3];
            serious_time = player->spell_cast_count[SPELL_CLASS_CLERIC][3] * 60;

            num_cure_critical += player->spell_cast_count[SPELL_CLASS_CLERIC][4];
            critical_time = player->spell_cast_count[SPELL_CLASS_CLERIC][4] * 75;
        }

        /* Four hours of settling down for a 1st-level caster, six for anyone
         * with a 4th or 5th level slot, plus the memorising itself. The healing
         * figures are what the spells average out at. */
        if (light_time > 0) {
            time_needed = 240;
            max_healing += 27;
        }

        if ((serious_time + critical_time) != 0) {
            time_needed = 360;

            if (critical_time > 0) {
                max_healing += 78;
            } else {
                max_healing += 34;
            }
        }

        time_needed += light_time + serious_time + critical_time;

        if (max_time < time_needed) {
            max_time = time_needed;
        }
    }

    lost_points = total_hitpoints_lost();

    /* A party with a scratch does not spend six hours on it: the whole rest is
     * divided by how many times over the spells would cover the damage. Integer
     * division both times, so twice the healing needed halves the rest and three
     * times cuts it to a third.
     *
     * The original divided by lost_points without looking at it. FixTeam is the
     * only caller and it has already checked that the party is hurt, so the
     * division could not be reached with a zero - but this is a C process, where
     * a stray caller would take it down rather than throw. */
    if (lost_points == 0) {
        if (max_healing > 0) {
            log_warn("camp: nobody is hurt, so there is nothing to scale the "
                     "rest against");
        }
    } else if (lost_points < max_healing) {
        max_time /= (max_healing / lost_points);
    }

    gbl.time_to_rest.slot[REST_SLOT_HOURS] = max_time / 60;
    gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] =
        (max_time - (gbl.time_to_rest.slot[REST_SLOT_HOURS] * 60)) / 10;
    gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] = max_time % 10;

    *out_cure_critical = num_cure_critical;
    *out_cure_serious = num_cure_serious;
    *out_cure_light = num_cure_light;
}

/* ovr016.DoTeamHealing, sub_46280. Spends the pool on whoever is hurt, in
 * marching order, until it runs out. */
static void do_team_healing(int *healing_available)
{
    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];
        int damage_taken;

        if (player == NULL || player->hit_point_max <= player->hit_point_current) {
            continue;
        }

        damage_taken = player->hit_point_max - player->hit_point_current;

        if (damage_taken > *healing_available) {
            damage_taken = *healing_available;
        }
        if (damage_taken < 1) {
            damage_taken = 0;
        }

        /* The third test is the original's and cannot fail, damage_taken having
         * just been clamped to the pool. */
        if (damage_taken > 0 &&
            effect_heal_player(0, damage_taken, player) == true &&
            damage_taken <= *healing_available) {
            *healing_available -= damage_taken;
        }
    }
}

/* ovr016.FixTeam, fix_menu. Rests for as long as the cures take and then spends
 * them all, which is the whole point of the Fix menu: no aiming, no prompts. */
static bool fix_team(void)
{
    bool action_interrupted = false;
    int healing_available;
    RestTime time_backup;
    int num_cure_critical = 0;
    int num_cure_serious = 0;
    int num_cure_light = 0;

    if (total_hitpoints_lost() == 0) {
        return false;
    }

    healing_available = calculate_initial_healing();

    /* The original looked again here. Nothing between the two tests heals
     * anybody - the roll above only counts what the spells are worth - so this
     * branch is dead, and kept because it costs nothing to keep. */
    if (total_hitpoints_lost() == 0) {
        character_party_summary(gbl.selected_player);
        character_display_map_position_time();
        return false;
    }

    time_backup = gbl.time_to_rest;

    camp_calculate_time_and_spell_numbers(&num_cure_critical, &num_cure_serious,
                                         &num_cure_light);

    action_interrupted = resting_run(false);

    if (action_interrupted == false) {
        calculate_healing(&healing_available, num_cure_light, num_cure_serious,
                          num_cure_critical);
        do_team_healing(&healing_available);

        character_party_summary(gbl.selected_player);
        character_display_map_position_time();

        /* Only put back on the way out without an interruption, as the original
         * had it: a fight leaves the clock holding whatever was left of the
         * rest. */
        gbl.time_to_rest = time_backup;
    }

    return action_interrupted;
}

/* ------------------------------------------------------------ the camp itself */

/* ovr016.MakeCamp, make_camp */
bool camp_make_camp(void)
{
    GameState game_state_bkup = gbl.game_state;
    bool action_interrupted = false;
    char input_key = ' ';

    gbl.game_state = GAME_STATE_CAMPING;
    gbl.rest_10_seconds = 0;

    rest_time_clear(&gbl.time_to_rest);

    /* The picture on screen when the party stopped, so it can be put back
     * afterwards. */
    snprintf(gbl.saved_dax_file, sizeof(gbl.saved_dax_file), "%s",
             gbl.last_dax_file);
    gbl.saved_dax_block_id = gbl.last_dax_block_id;

    character_load_pic();
    frames_clear_region(TEXT_REGION_NORMAL_BOTTOM);

    text_display_string("The party makes camp...", 0, 10, 18, 1);

    /* Anything lined up before the camp opened is thrown away, and so is
     * anything still lined up when it closes: a spell is only ever memorised by
     * resting here and now. */
    camp_cancel_spells();

    while (action_interrupted == false && exit_key(input_key) == false) {
        bool control_key = false;

        input_key = prompt_display_input(&control_key, true,
                                        PROMPT_CTRL_WORD_ARROWS,
                                        GBL_DEFAULT_MENU_COLORS,
                                        "Save View Magic Rest Alter Fix Exit",
                                        "Camp:");

        if (control_key == true) {
            viewplayer_scroll_team_list(input_key);
            character_party_summary(gbl.selected_player);
        } else {
            switch (input_key) {
            case 'S':
                savegame_save_game();

                if (prompt_yes_no(GBL_ALERT_MENU_COLORS, "Quit TO DOS ") == 'Y') {
                    game_print_and_exit();
                }
                break;

            case 'V':
                gbl.menu_selected_word = 1;
                viewplayer_view_player();
                break;

            case 'M':
                gbl.menu_selected_word = 1;
                action_interrupted = camp_magic_menu();
                break;

            case 'R':
                gbl.menu_selected_word = 1;
                action_interrupted = camp_rest_menu();
                break;

            case 'F':
                action_interrupted = fix_team();
                break;

            case 'A':
                gbl.menu_selected_word = 1;
                camp_alter_menu();
                break;

            default:
                break;
            }
        }
    }

    /* Put back the picture the party was looking at, but only when it was one of
     * the map pictures: FINAL and the portraits are not redrawn here.
     *
     * Divergence: the C# tested seg051.Copy(3, 1, ...) against "PIC", which
     * takes three characters starting at index 1 and so could never match -
     * Pascal's Copy is 1-based and the DOS code compared the first three. The
     * first three are compared here, so the picture really does come back. */
    if (strncmp(gbl.saved_dax_file, "PIC", 3) == 0) {
        picture_load_pic_final(&gbl.pic_frames, 0, gbl.saved_dax_block_id,
                               gbl.saved_dax_file);
    }

    camp_cancel_spells();
    gbl.last_selected_spell_target = NULL;
    gbl.game_state = game_state_bkup;

    character_display_map_position_time();
    character_clear_text_area();
    prompt_clear_area();

    return action_interrupted;
}
