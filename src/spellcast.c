/* spellcast.c - Ported from engine/ovr023.cs (the casting core; see
 * spellcast.h). */
#include <stdio.h>
#include <string.h>

#include "spellcast.h"

#include "character.h"
#include "combatmap.h"
#include "effect.h"
#include "enums.h"
#include "frames.h"
#include "gbl.h"
#include "log.h"
#include "prompt.h"
#include "sound.h"
#include "spelleffect.h"
#include "spells.h"
#include "target.h"
#include "text.h"

/* ovr023.SpellNames. The blanks are the ids the printed spell lists skip: the
 * table has a row for every id from 1 to 0x64 whether the game hands it out or
 * not, so nothing here may be shifted up. */
static const char *const SPELL_NAMES[] = {
    /* 0x00 */ "",
    /* 0x01 */ "Bless",
    /* 0x02 */ "Curse",
    /* 0x03 */ "Cure Light Wounds",
    /* 0x04 */ "Cause Light Wounds",
    /* 0x05 */ "Detect Magic",
    /* 0x06 */ "Protection from Evil",
    /* 0x07 */ "Protection from Good",
    /* 0x08 */ "Resist Cold",
    /* 0x09 */ "Burning Hands",
    /* 0x0a */ "Charm Person",
    /* 0x0b */ "Detect Magic",
    /* 0x0c */ "Enlarge",
    /* 0x0d */ "Reduce",
    /* 0x0e */ "Friends",
    /* 0x0f */ "Magic Missile",
    /* 0x10 */ "Protection From Evil",
    /* 0x11 */ "Protection From Good",
    /* 0x12 */ "Read Magic",
    /* 0x13 */ "Shield",
    /* 0x14 */ "Shocking Grasp",
    /* 0x15 */ "Sleep",
    /* 0x16 */ "Find Traps",
    /* 0x17 */ "Hold Person",
    /* 0x18 */ "Resist Fire",
    /* 0x19 */ "Silence, 15' Radius",
    /* 0x1a */ "Slow Poison",
    /* 0x1b */ "Snake Charm",
    /* 0x1c */ "Spiritual Hammer",
    /* 0x1d */ "Detect Invisibility",
    /* 0x1e */ "Invisibility",
    /* 0x1f */ "Knock",
    /* 0x20 */ "Mirror Image",
    /* 0x21 */ "Ray of Enfeeblement",
    /* 0x22 */ "Stinking Cloud",
    /* 0x23 */ "Strength",
    /* 0x24 */ "Animate Dead",
    /* 0x25 */ "Cure Blindness",
    /* 0x26 */ "Cause Blindness",
    /* 0x27 */ "Cure Disease",
    /* 0x28 */ "Cause Disease",
    /* 0x29 */ "Dispel Magic",
    /* 0x2a */ "Prayer",
    /* 0x2b */ "Remove Curse",
    /* 0x2c */ "Bestow Curse",
    /* 0x2d */ "Blink",
    /* 0x2e */ "Dispel Magic",
    /* 0x2f */ "Fireball",
    /* 0x30 */ "Haste",
    /* 0x31 */ "Hold Person",
    /* 0x32 */ "Invisibility, 10' Radius",
    /* 0x33 */ "Lightning Bolt",
    /* 0x34 */ "Protection From Evil, 10' Radius",
    /* 0x35 */ "Protection From Good, 10' Radius",
    /* 0x36 */ "Protection From Normal Missiles",
    /* 0x37 */ "Slow",
    /* 0x38 */ "Restoration",
    /* 0x39 */ "",
    /* 0x3a */ "Cure Serious Wounds",
    /* 0x3b */ "",
    /* 0x3c */ "",
    /* 0x3d */ "",
    /* 0x3e */ "",
    /* 0x3f */ "",
    /* 0x40 */ "",
    /* 0x41 */ "",
    /* 0x42 */ "Cause Serious Wounds",
    /* 0x43 */ "Neutralize Poison",
    /* 0x44 */ "Poison",
    /* 0x45 */ "Protection Evil, 10' Radius",
    /* 0x46 */ "Sticks to Snakes",
    /* 0x47 */ "Cure Critical Wounds",
    /* 0x48 */ "Cause Critical Wounds",
    /* 0x49 */ "Dispel Evil",
    /* 0x4a */ "Flame Strike",
    /* 0x4b */ "Raise Dead",
    /* 0x4c */ "Slay Living",
    /* 0x4d */ "Detect Magic",
    /* 0x4e */ "Entangle",
    /* 0x4f */ "Faerie Fire",
    /* 0x50 */ "Invisibility to Animals",
    /* 0x51 */ "Charm Monsters",
    /* 0x52 */ "Confusion",
    /* 0x53 */ "Dimension Door",
    /* 0x54 */ "Fear",
    /* 0x55 */ "Fire Shield",
    /* 0x56 */ "Fumble",
    /* 0x57 */ "Ice Storm",
    /* 0x58 */ "Minor Globe Of Invulnerability",
    /* 0x59 */ "Remove Curse",
    /* 0x5a */ "Animate Dead",
    /* 0x5b */ "Cloud Kill",
    /* 0x5c */ "Cone of Cold",
    /* 0x5d */ "Feeblemind",
    /* 0x5e */ "Hold Monsters",
    /* 0x5f */ "",
    /* 0x60 */ "",
    /* 0x61 */ "",
    /* 0x62 */ "",
    /* 0x63 */ "",
    /* 0x64 */ "Bestow Curse"
};

const char *spellcast_spell_name(int spell_id)
{
    if (spell_id < 0 || (size_t)spell_id >= COAB_ARRAY_LEN(SPELL_NAMES)) {
        log_warn("spell names: no spell 0x%x", spell_id);
        return "";
    }

    return SPELL_NAMES[spell_id];
}

bool spellcast_remove_spell_from_scroll(int spell_id, Item *item, Player *player,
                                       Item *out_scroll)
{
    int affect_index = 0;

    if (item == NULL || player == NULL) {
        log_warn("remove_spell_from_scroll: no %s", item == NULL ? "scroll"
                                                                : "character");
        return false;
    }

    /* The last of the three that holds the spell, not the first: a scroll with
     * the same spell on it twice loses the second copy. */
    for (int index = 1; index <= 3; index++) {
        if (((int)item_affect(item, index) & 0x7f) == spell_id) {
            affect_index = index;
        }
    }

    if (affect_index != 0) {
        item_affect_set(item, affect_index, (Affects)0);

        /* namenum2 counts the spells: 0xd2 "With 1 Spell", 0xd3 "With 2 Spells",
         * 0xd4 "With 3 Spells". Below the first of those the scroll is blank. */
        item->namenum2 -= 1;

        if (item->namenum2 < 0xd2) {
            if (out_scroll != NULL) {
                *out_scroll = *item;
            }
            character_lose_item(item, player);

            return false;
        }
    }

    if (out_scroll != NULL) {
        *out_scroll = *item;
    }

    return true;
}

void spellcast_display_case_spell_text(int spell_id, const char *text,
                                       Player *player)
{
    if (player == NULL) {
        return;
    }

    if (gbl.game_state == GAME_STATE_COMBAT) {
        char line[64];

        character_display_status_string(true, 10, "Casts a Spell", player);
        frames_clear_area(0x17, 0x27, 0x17, 0);

        snprintf(line, sizeof(line), "Spell:%s", spellcast_spell_name(spell_id));
        text_display_string(line, 0, 10, 0x17, 0);
    } else {
        frames_clear_area(0x16, 0x26, 0x12, 1);

        character_display_name(false, 0x13, 1, player);

        text_display_string(text != NULL ? text : "", 0, 10, 0x13,
                            (int)strlen(player->name) + 2);
        text_display_string(spellcast_spell_name(spell_id), 0, 10, 0x14, 1);

        text_game_delay();
        character_clear_text_area();
    }
}

/* sub_5CDE5 */
int spellcast_spell_range(int spell_id)
{
    const SpellEntry *entry = spell_entry(spell_id);
    int casting_lvl;
    int range;

    if (entry == NULL) {
        /* The C# indexed the table and would have thrown; a range of one square
         * is what the two tests below settle every unusable answer to. */
        log_warn("spell range: no spell 0x%x", spell_id);
        return 1;
    }

    casting_lvl = gbl.spell_from_item ? 6
                                     : character_spell_max_target_count(spell_id);

    range = entry->fixed_range + (entry->per_lvl_range * casting_lvl);

    /* A spell that reaches nowhere but has a target type still reaches the square
     * it is standing on. */
    if (range == 0 && entry->field_6 != 0) {
        range = 1;
    }

    if (range == -1 || range == 0xff) {
        range = 1;
    }

    return range;
}

/* sub_5CE92. Five spells work their duration out by rolling for it; every other
 * spell takes it off the table, a fixed part plus a part per caster level. The
 * result is a ushort of minutes. */
u16 spellcast_spell_affect_timeout(int spell_id)
{
    int minutes;

    if (spell_id == SPELL_CAUSE_DISEASE) {
        minutes = effect_roll_dice(6, 1) * 10;
    } else if (spell_id == SPELL_39 || spell_id == SPELL_3D) {
        minutes = effect_roll_dice(4, 5);
    } else if (spell_id == SPELL_3B) {
        minutes = (effect_roll_dice(4, 1) * 10) + 40;
    } else if (spell_id == SPELL_3F) {
        if (gbl.game_state == GAME_STATE_COMBAT) {
            minutes = effect_roll_dice(10, 2) * 10;
        } else {
            minutes = (effect_roll_dice(10, 1) + 10) * 10;
        }
    } else if (spell_id == SPELL_NEUTRALIZE_POISON) {
        minutes = 1440;
    } else {
        const SpellEntry *entry = spell_entry(spell_id);

        if (entry == NULL) {
            /* The C# indexed the table. SpellEntangle asks for spell 0x88, which
             * is past the end of it - see spelleffect.c - so this is reachable. */
            log_warn("spell timeout: no spell 0x%x", spell_id);
            return 0;
        }

        minutes = entry->fixed_duration +
                  (entry->per_lvl_duration *
                   character_spell_max_target_count(spell_id));
    }

    return (u16)minutes;
}

/* sub_5CF7F. What most spells come down to: for everyone the spell reaches, a
 * saving throw, the damage, and the spell's affect. A touch spell - fixed range
 * -1 - has to hit first.
 *
 * target_count is the affect's data byte and is not a count at all in most
 * cases: the spells that pass one pack a team number or a duration into it. Zero
 * means "use the caster's level". */
void spellcast_do_casting_work(const char *text, int damage_flags, int damage,
                               bool call_affect_table, int target_count,
                               int spell_id)
{
    const SpellEntry *entry = spell_entry(spell_id);

    gbl.damage_flags = (damage == 0) ? 0 : damage_flags;

    if (entry == NULL) {
        log_warn("spell casting: no spell 0x%x", spell_id);
        gbl.damage_flags = 0;
        return;
    }

    if (gbl.spell_target_count > 0) {
        int data = (target_count > 0)
                       ? target_count
                       : character_spell_max_target_count(spell_id);

        for (int i = 0; i < gbl.spell_target_count; i++) {
            Player *target = gbl.spell_targets[i];
            bool saved;

            if (target == NULL) {
                continue;
            }

            if (entry->damage_on_save == 0) {
                saved = false;
            } else {
                saved = effect_roll_saving_throw(0, entry->save_verse, target);
            }

            if (entry->fixed_range == -1) {
                character_recalc_values(target);

                effect_check_affects(target, CHECK_TYPE_11);

                if (effect_pc_can_hit_target(target->ac, target,
                                             gbl.selected_player) == false) {
                    /* The miss is remembered for every target after this one
                     * too: the C# assigned to the parameter, so a touch spell
                     * that misses its first target does nothing to the rest.
                     * Faithfully reproduced - a touch spell only ever has one
                     * target. */
                    damage = 0;
                    saved = true;
                }
            }

            if (damage > 0) {
                effect_damage_person(saved, entry->damage_on_save, damage,
                                     target);
            }

            if (entry->affect_id > 0) {
                effect_apply_attack_spell_affect(
                    text, saved, entry->damage_on_save, call_affect_table, data,
                    spellcast_spell_affect_timeout(spell_id), entry->affect_id,
                    target);
            }
        }

        gbl.damage_flags = 0;
    }
}

/* cast_spell_on. How a spell finds what it touches outside a fight, which is
 * what gbl.spell_cast_function points at whenever a battle is not running. */
bool spellcast_non_combat_cast(QuickFight quick_fight, int spell_id)
{
    const SpellEntry *entry = spell_entry(spell_id);
    bool cast_spell = true;

    if (gbl.last_selected_spell_target == NULL) {
        gbl.last_selected_spell_target = gbl.selected_player;
    }

    gbl_spell_targets_clear();
    gbl_spell_target_add(gbl.selected_player);

    if (entry == NULL) {
        log_warn("spell cast: no spell 0x%x", spell_id);
        return false;
    }

    switch (entry->target_type) {
    case SPELL_TARGET_SELF:
        break;

    case SPELL_TARGET_PARTY_MEMBER:
        character_load_pic();

        character_select_a_player(&gbl.last_selected_spell_target, true,
                                  "Cast Spell on whom");

        gbl_spell_targets_clear();

        if (gbl.last_selected_spell_target == NULL) {
            cast_spell = false;
        } else {
            gbl_spell_target_add(gbl.last_selected_spell_target);
        }
        break;

    case SPELL_TARGET_WHOLE_PARTY:
        /* The caster is already on the list and the whole team goes on after
         * them, so a party spell touches its caster twice. That is what the C#
         * did - AddRange onto a list it had just added the caster to - and for
         * the affects a party spell carries a second application is a no-op. */
        for (int i = 0; i < gbl.team_count; i++) {
            gbl_spell_target_add(gbl.team_list[i]);
        }
        break;

    default:
        /* SPELL_TARGET_COMBAT: nothing outside a fight can be aimed at. */
        cast_spell = false;
        break;
    }

    return cast_spell;
}

/* sub_5D2E1, the overload that throws the flag away. */
void spellcast_resolve_spell(bool show_casting_text, QuickFight quick_fight,
                             int spell_id)
{
    bool dummy = false;

    spellcast_resolve_spell_used(&dummy, show_casting_text, quick_fight,
                                 spell_id);
}

/* sub_5D2E1. Everything between choosing a spell and the spell going off: the
 * "can't be cast here" refusal, the chance of a miscast, aiming it, the missile
 * animation, taking it out of the caster's memory, and finally the handler.
 *
 * *turn_used comes back true when the caster spent their action - which for a
 * combat-only item outside a fight means the player chose to waste it. */
void spellcast_resolve_spell_used(bool *turn_used, bool show_casting_text,
                                  QuickFight quick_fight, int spell_id)
{
    Player *caster = gbl.selected_player;
    const SpellEntry *entry = spell_entry(spell_id);
    bool still_cast = true;
    bool local_used = false;

    if (turn_used == NULL) {
        turn_used = &local_used;
    }

    if (caster == NULL || entry == NULL) {
        log_warn("resolve spell: no %s for spell 0x%x",
                 caster == NULL ? "caster" : "spell", spell_id);
        return;
    }

    if (gbl.game_state != GAME_STATE_COMBAT &&
        entry->target_type == SPELL_TARGET_COMBAT) {
        if (gbl.spell_from_item == false) {
            text_display_string(spellcast_spell_name(spell_id), 0, 10, 0x13, 1);
            text_display_string("can't be cast here...", 0, 10, 0x14, 1);

            if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Lose it? ") == 'Y') {
                spell_list_clear_spell(&caster->spell_list, spell_id);
            }
        } else {
            text_display_string("That Item", 0, 10, 0x13, 1);
            text_display_string("is a combat-only item...", 0, 10, 0x14, 1);

            if (prompt_yes_no(GBL_DEFAULT_MENU_COLORS, "Use it? ") == 'Y') {
                *turn_used = true;
            }
        }

        show_casting_text = false;
        still_cast = false;
    }

    if (player_has_affect(caster, AFFECT_4A) == true) {
        if (effect_roll_dice(2, 1) == 1) {
            spellcast_display_case_spell_text(spell_id, "miscasts", caster);
            show_casting_text = false;
            still_cast = false;
        }
    }

    if (show_casting_text == true && gbl.spell_from_item == false) {
        spellcast_display_case_spell_text(spell_id, "casts", caster);
    }

    while (still_cast == true) {
        if (gbl.spell_cast_function == NULL) {
            /* engine/ovr009.cs points this at the combat targeting when a fight
             * starts and back at spellcast_non_combat_cast when it ends. Nothing
             * should reach here with it unset. */
            log_warn("resolve spell: no targeting function for spell 0x%x",
                     spell_id);
            break;
        }

        *turn_used = gbl.spell_cast_function(quick_fight, spell_id);

        if (*turn_used == true) {
            still_cast = false;

            if (gbl.game_state == GAME_STATE_COMBAT) {
                Point caster_pos = combatmap_player_map_pos(caster);
                u8 direction;

                character_load_missile_icons(0x12);

                direction = target_find_combatant_direction(gbl.target_pos,
                                                            caster_pos);

                gbl.focus_combat_area_on_player = true;
                combatmap_draw_player(false, COMBAT_ICON_ATTACK, direction,
                                      caster);

                if (spell_id == SPELL_FIREBALL) {
                    sound_play(SOUND_B);
                } else if (spell_id == SPELL_LIGHTNING_BOLT) {
                    sound_play(SOUND_8);
                } else {
                    sound_play(SOUND_2);
                }

                character_draw_missile_attack(0x1e, 4, gbl.target_pos,
                                              caster_pos);

                if (combatmap_player_on_screen_p(false, caster) == true) {
                    int facing = (caster->actions != NULL)
                                     ? caster->actions->direction : 0;

                    combatmap_draw_player(true, COMBAT_ICON_ATTACK, facing,
                                          caster);
                    combatmap_draw_player(false, COMBAT_ICON_NORMAL, facing,
                                          caster);
                }
            }

            effect_remove_invisibility(caster);

            if (gbl.spell_from_item == false) {
                spell_list_clear_spell(&caster->spell_list, spell_id);
            }

            gbl.spell_id = spell_id;

            spelleffect_call(spell_id);

            gbl.spell_id = 0;
            gbl.byte_1D2C7 = false;
        } else {
            if (gbl.game_state != GAME_STATE_COMBAT) {
                still_cast = false;
            } else if (quick_fight == QUICK_FIGHT_TRUE ||
                       prompt_yes_no(GBL_ALERT_MENU_COLORS,
                                     "Abort Spell? ") == 'Y') {
                character_print_message("Spell Aborted");

                if (gbl.spell_from_item == false) {
                    spell_list_clear_spell(&caster->spell_list, spell_id);
                }

                still_cast = false;
            }
        }
    }

    character_clear_text_area();

    if (gbl.game_state == GAME_STATE_COMBAT) {
        frames_clear_area(0x17, 0x27, 0x17, 0);
    }
}
