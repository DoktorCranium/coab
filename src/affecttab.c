/* affecttab.c - Ported from engine/ovr013.cs.
 *
 * The handlers are in the order ovr013.cs has them, and the table at the bottom
 * is SetupAffectTables line for line. Each handler keeps its C# name next to the
 * C one, with the DOS symbol where the C# recorded it.
 */
#include "affecttab.h"

#include "attack.h"
#include "character.h"
#include "combat.h"
#include "combatmap.h"
#include "effect.h"
#include "gbl.h"
#include "item.h"
#include "limits.h"
#include "log.h"
#include "point.h"
#include "spelleffect.h"
#include "spells.h"
#include "target.h"
#include "text.h"
#include "tile.h"
#include "viewplayer.h"

/* ------------------------------------------------------------------ helpers */

/* ovr013.Protected. "This does not touch me": no damage, and no affect left for
 * the attack code to hang on the target. */
static void set_protected(void)
{
    gbl.damage = 0;
    gbl.current_affect = 0;
}

/* ovr013.ProtectedIf, sub_3A019. The same, if the affect coming in is the one
 * this protection is against. */
static void protected_if(Affects affect)
{
    if (gbl.current_affect == (int)affect) {
        set_protected();
    }
}

/* ovr013.addAffect. Renews an affect that damages or heals its owner every so
 * often - poison, disease, regeneration - and reports whether it did. A cure is
 * being applied when gbl.cure_spell is set, and then the affect is left to run
 * out instead, which is how the cure gets rid of it. */
static bool add_affect_unless_cured(u16 time, int data, Affects affect_type,
                                    Player *player)
{
    if (gbl.cure_spell) {
        return false;
    }

    effect_add_affect(true, data, time, affect_type, player);
    return true;
}

/* The Affect a handler was called about. Most handlers cast `param` without
 * looking, which would have thrown NullReferenceException in the C#; here a
 * missing one is logged and the handler does nothing. */
static Affect *as_affect(void *param, const char *who)
{
    if (param == NULL) {
        log_warn("affect table: %s was called without an affect", who);
        return NULL;
    }

    return (Affect *)param;
}

/* gbl.SelectedPlayer: the character on the other side of the exchange from the
 * affect's owner - the attacker when a defender's protection is speaking, the
 * target when an attacker's weapon bonus is. The C# read it without checking. */
static Player *selected(const char *who)
{
    if (gbl.selected_player == NULL) {
        log_warn("affect table: %s ran with nobody selected", who);
    }

    return gbl.selected_player;
}

/* The target of the blow being struck, which the handlers that need it take from
 * their owner's action - and leave in gbl.spell_target for whatever runs next,
 * which is what the original used the field for. The assignment happens either
 * way, as in the C#; only the read that follows it is guarded, where the C# would
 * have thrown. */
static Player *action_target(Player *player)
{
    gbl.spell_target = player_actions(player)->target;

    return gbl.spell_target;
}

/* gbl.mapToBackGroundTile, which only exists while a fight is being drawn. The
 * cloud handlers run when their affect expires, and that can happen after the
 * fight has ended and ovr009 has dropped the map. */
static void set_ground_tile(Point pos, int tile)
{
    if (gbl.map_to_background_tile == NULL) {
        log_warn("affect table: a cloud cleared with no combat map to redraw");
        return;
    }

    ground_tile_map_set(gbl.map_to_background_tile, pos, tile);
}

/* gbl.downedPlayers.Exists(cell => cell.target != null && cell.map == pos) */
static bool downed_player_at(Point pos)
{
    for (int i = 0; i < gbl.downed_player_count; i++) {
        if (gbl.downed_players[i].target != NULL &&
            point_eq(gbl.downed_players[i].map, pos)) {
            return true;
        }
    }

    return false;
}

/* The `p => true` the two berserk handlers hand Rebuild_SortedCombatantList:
 * every combatant that can be reached at all. */
static bool any_combatant(const Player *player, void *ctx)
{
    (void)player;
    (void)ctx;

    return true;
}

/* Scratch for those two calls. A fight holds at most GBL_MAX_COMBATANT_COUNT
 * combatants and the list is read before the next handler runs, so one shared
 * array does; nothing here is re-entrant. */
static SortedCombatant g_sorted[GBL_MAX_COMBATANT_COUNT];

/* Whoever a berserk character turns on: the nearest combatant of any side. NULL
 * when there is nobody left to reach, where the C# indexed [0] of an empty list
 * and threw ArgumentOutOfRangeException. */
static Player *nearest_combatant(Player *player)
{
    int count = target_sorted_combatants_for(g_sorted,
                                            (int)COAB_ARRAY_LEN(g_sorted),
                                            player, 0xff, any_combatant, NULL);

    if (count <= 0) {
        log_warn("affect table: %s went berserk with nobody in reach",
                 player->name);
        return NULL;
    }

    return g_sorted[0].player;
}

/* ---------------------------------------------------------------- handlers */

/* ovr013.sub_3A071. Registered for the affects that simply stop their owner
 * doing anything: fumbling, helplessness, snake charm, paralysis and sleep. */
static void affect_clear_actions(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    character_clear_actions(player);
}

/* ovr013.Bless */
static void affect_bless(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    gbl.monster_morale += 5;
    gbl.attack_roll++;
}

/* ovr013.Curse */
static void affect_curse_morale(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if (gbl.monster_morale < 5) {
        gbl.monster_morale = 0;
    } else {
        gbl.monster_morale -= 5;
    }

    gbl.attack_roll--;
}

/* ovr013.SticksToSnakes. The snakes take a swing out of every attack their
 * victim had left this round, and go when they run out. */
static void affect_sticks_to_snakes(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "sticks to snakes");
    u8 attacks_left;

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    attacks_left = (u8)(player->attack2_attacks_left +
                        player->attack1_attacks_left);

    if (affect->affect_data > attacks_left) {
        affect->affect_data -= attacks_left;
    } else {
        effect_remove_affect(NULL, AFFECT_STICKS_TO_SNAKES, player);
    }

    character_magic_attack_display("is fighting with snakes", true, player);
    character_clear_text_area();

    character_clear_actions(player);
}

/* ovr013.DispelEvil. The evil creature the caster is up against swings badly. */
static void affect_dispel_evil(Effect add_remove, void *param, Player *player)
{
    Player *other = selected("dispel evil");

    (void)add_remove;
    (void)param;
    (void)player;

    if (other != NULL && (other->field_14B & 1) != 0) {
        gbl.attack_roll -= 7;
    }
}

/* ovr013.BonusVsMonstersX, sub_3A17A. A weapon that knows what it is hitting:
 * more so against trolls, more again against two of the monster kinds, most
 * against the walking dead. */
static void affect_bonus_vs_monsters(Effect add_remove, void *param, Player *player)
{
    int bonus = 0;

    (void)add_remove;
    (void)param;

    if (player->actions != NULL && player->actions->target != NULL) {
        gbl.spell_target = player->actions->target;

        if (gbl.spell_target->monster_type == MONSTER_TROLL) {
            bonus = 1;
        } else if (gbl.spell_target->monster_type == MONSTER_TYPE_9 ||
                   gbl.spell_target->monster_type == MONSTER_TYPE_12) {
            bonus = 2;
        } else if (gbl.spell_target->monster_type == MONSTER_ANIMATED_DEAD) {
            bonus = 3;
        } else {
            bonus = 0;
        }
    }

    gbl.attack_roll += bonus;
    gbl.damage += bonus;
    gbl.damage_flags = DAMAGE_MAGIC | DAMAGE_FIRE;
}

/* ovr013.FaerieFire */
static void affect_faerie_fire(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    /* ac counts up as the armour class on the sheet counts down - see
     * player_display_ac - so this makes the outlined creature two points harder
     * to hit rather than easier, and 0x3C is AC 0. That is what the code being
     * translated does, and it is left as it stands. */
    if (player->ac < 0x3a) {
        player->ac += 2;
    } else {
        player->ac = 0x3c;
    }

    if (player->ac_behind < 0x3a) {
        player->ac_behind += 2;
    } else {
        player->ac_behind = 0x3c;
    }
}

/* ovr013.affect_protect_evil, sub_3A224. Registered for both the personal and
 * the 10' radius protection. The three evil alignments are 2, 5 and 8. */
static void affect_protect_evil(Effect add_remove, void *param, Player *player)
{
    Player *other = selected("protection from evil");

    (void)add_remove;
    (void)param;
    (void)player;

    if (other == NULL) {
        return;
    }

    if (other->alignment == 2 || other->alignment == 5 ||
        other->alignment == 8) {
        gbl.saving_throw_roll += 2;
        gbl.attack_roll -= 2;
    }
}

/* ovr013.affect_protect_good, sub_3A259. The good alignments are 0, 3 and 6. */
static void affect_protect_good(Effect add_remove, void *param, Player *player)
{
    Player *other = selected("protection from good");

    (void)add_remove;
    (void)param;
    (void)player;

    if (other == NULL) {
        return;
    }

    if (other->alignment == 0 || other->alignment == 3 ||
        other->alignment == 6) {
        gbl.saving_throw_roll += 2;
        gbl.attack_roll -= 2;
    }
}

/* ovr013.affect_resist_cold, sub_3A28E */
static void affect_resist_cold(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_COLD) != 0) {
        gbl.damage /= 2;
        gbl.saving_throw_roll += 3;
    }
}

/* ovr013.affect_charm_person, sub_3A2AD. The charmed creature changes sides, and
 * its old side is remembered in the affect so it can go back. */
static void affect_charm_person(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "charm person");

    if (affect == NULL) {
        return;
    }

    if (add_remove == EFFECT_REMOVE) {
        player->combat_team = (affect->affect_data & 0x40) >> 6;

        if (player->control_morale == CONTROL_PC_BERZERK) {
            player->control_morale = CONTROL_PC_BASE;
        }
    } else {
        /* Bit 5 marks the change as already made, so a charm that is looked at
         * again each round does not stack. */
        if ((affect->affect_data & 0x20) == 0) {
            affect->affect_data += (u8)(0x20 + (player->combat_team << 6));

            /* The side that was just saved in bit 6 is read back out of bit 7
             * here, which always gives team 0 - ours - whichever side the
             * creature was on. Removal above reads bit 6, so the creature does go
             * back to its own side when the charm ends. */
            player->combat_team = affect->affect_data >> 7;
            player->quick_fight = QUICK_FIGHT_TRUE;

            if (player->control_morale < CONTROL_NPC_BASE) {
                player->control_morale = CONTROL_PC_BERZERK;
            }

            player_actions(player)->target = NULL;
            character_count_combat_teams();
        }

        gbl.monster_morale = 100;
    }
}

/* ovr013.Suffocates. The reduce spell run too far: six rounds of it and the
 * shrunken creature stops breathing. */
static void affect_suffocates(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "suffocation");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if (affect->affect_data == 0) {
        effect_kill_player("Suffocates", STATUS_DEAD, player);
    } else {
        affect->affect_data--;
    }
}

/* ovr013.AffectPoisonDamage, sub_3A3BC. A point every ten minutes, and never the
 * last one: poison brings a character down to 1 hit point and leaves them there. */
static void affect_poison_damage(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "poison damage");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if (add_affect_unless_cured(10, affect->affect_data, AFFECT_POISON_DAMAGE,
                                player) &&
        player->hit_point_current > 1) {
        gbl.damage_flags = 0;

        effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL, 1, player);

        if (gbl.game_state != GAME_STATE_COMBAT && gbl.selected_player != NULL) {
            character_party_summary(gbl.selected_player);
        }
    }
}

/* ovr013.AffectShield, sub_3A41F. AC 3 at worst, a point on every save, and
 * magic missiles do nothing at all. */
static void affect_shield(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (player->ac < 0x39) {                /* AC 3 */
        player->ac = 0x39;
    }

    gbl.saving_throw_roll += 1;

    if (gbl.spell_id == SPELL_MAGIC_MISSILE) {
        gbl.damage = 0;
    }
}

/* ovr013.AffectGnomeVsManSizedGiant, sub_3A44A */
static void affect_gnome_vs_giant(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (player->actions != NULL && player->actions->target != NULL &&
        (player->actions->target->field_14B & 2) != 0) {
        gbl.spell_target = player->actions->target;
        gbl.attack_roll++;
    }
}

/* ovr013.AffectResistFire, sub_3A480 */
static void affect_resist_fire(Effect add_remove, void *param, Player *player)
{
    (void)param;
    (void)player;

    if (add_remove == EFFECT_ADD && (gbl.damage_flags & DAMAGE_FIRE) != 0) {
        gbl.damage /= 2;
        gbl.saving_throw_roll += 3;
    }
}

/* ovr013.is_silenced1. Silence stops a spell and an item alike. */
static void affect_silenced(Effect add_remove, void *param, Player *player)
{
    Action *action = player_actions(player);

    (void)add_remove;
    (void)param;

    if (action->can_use) {
        character_display_status_string(true, 10, "is silenced", player);
    }

    action->can_use = false;
    action->can_cast = false;
}

/* ovr013.AffectSlowPoison, sub_3A517. Slow poison holds off the damage, but a
 * character already carrying the killing kind of poison dies of it here. */
static void affect_slow_poison(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (player_has_affect(player, AFFECT_POISONED)) {
        effect_kill_player("dies from poison", STATUS_DEAD, player);
    }

    gbl.cure_spell = true;

    effect_remove_affect(NULL, AFFECT_POISON_DAMAGE, player);

    gbl.cure_spell = false;
}

/* ovr013.affect_spiritual_hammer, sub_3A583. Conjures a +1 hammer into the
 * caster's pack and readies it, and takes it away again when the spell ends. Name
 * part 3 of 243 is what marks it as the conjured one. */
static void affect_spiritual_hammer(Effect add_remove, void *param, Player *player)
{
    Item *item = NULL;
    bool item_found;

    (void)param;

    for (int i = 0; i < player->item_count; i++) {
        Item *it = player_item_at(player, i);

        if (it != NULL && it->type == ITEM_HAMMER && it->namenum3 == 0xf3) {
            item = it;
            break;
        }
    }

    item_found = (item != NULL);

    if (add_remove == EFFECT_REMOVE && item != NULL) {
        character_lose_item(item, player);
    }

    if (add_remove == EFFECT_ADD && item_found == false &&
        player->item_count < PLAYER_MAX_ITEMS) {
        Item hammer;
        int index;

        /* The C# constructor takes its arguments in the order the DOS build
         * pushed them, which is backwards; these are in declaration order. The
         * count and the weight really are zero. */
        item_init(&hammer, ITEM_HAMMER, 0, 20, 243, 1, 0, false, 0, false, 0, 0,
                  0, AFFECT_NONE, AFFECT_SPIRITUAL_HAMMER, AFFECT_78);

        index = player_item_add(player, &hammer);

        if (index >= 0) {
            /* ready_Item readies for gbl.SelectedPlayer rather than for the
             * character whose pack the item was just put in. Those are the same
             * character when a caster's own spell resolves, which is the only way
             * here. */
            viewplayer_ready_item(player_item_at(player, index));

            character_display_status_string(true, 10, "Gains an item", player);
        }
    }

    character_recalc_values(player);
}

/* ovr013.sub_3A6C6. Invisibility, from the attacker's side: a monster - a
 * character with no name - cannot be seen unless the attacker has detect
 * invisibility up. */
static void affect_invisibility(Effect add_remove, void *param, Player *player)
{
    Player *other = selected("invisibility");

    (void)add_remove;
    (void)param;

    if (other == NULL) {
        return;
    }

    if (player->name[0] == '\0' &&
        player_has_affect(other, AFFECT_DETECT_INVISIBILITY) == false) {
        gbl.target_invisible = true;
        gbl.attack_roll -= 4;
    }
}

/* ovr013.AffectDwarfVsOrc, sub_3A7E8 */
static void affect_dwarf_vs_orc(Effect add_remove, void *param, Player *player)
{
    Player *target = action_target(player);

    (void)add_remove;
    (void)param;

    if (target != NULL && (target->field_14B & 4) != 0) {
        gbl.attack_roll++;
    }
}

/* ovr013.MirrorImage. Each blow that lands takes an image instead, if the roll
 * over the images still standing comes up better than 1. */
static void affect_mirror_image(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "mirror image");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if (effect_roll_dice((affect->affect_data >> 4) + 1, 1) > 1 &&
        gbl.spell_id > 0 && gbl.byte_1D2C7 == false) {
        set_protected();

        character_display_status_string(true, 10, "lost an image", player);

        affect->affect_data -= 1;

        if (affect->affect_data == 0) {
            effect_remove_affect(NULL, AFFECT_MIRROR_IMAGE, player);
        }
    }
}

/* ovr013.three_quarters_damage. The ray of enfeeblement's weakened arm. */
static void affect_three_quarters_damage(Effect add_remove, void *param,
                                         Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    gbl.damage -= gbl.damage / 4;
}

/* ovr013.StinkingCloud. What standing in one does to a character: no action this
 * round, and an armour class two points worse each round down to AC 10. */
static void affect_stinking_cloud(Effect add_remove, void *param, Player *player)
{
    Action *action = player_actions(player);

    (void)add_remove;
    (void)param;

    if (action->can_use) {
        character_display_status_string(true, 10, "is coughing", player);
    }

    action->can_use = false;
    action->can_cast = false;

    character_recalc_values(player);

    if (player->ac_behind > 0x34) {         /* better than AC 8 */
        player->ac_behind -= 2;
    } else {
        player->ac_behind = 0x32;           /* AC 10 */
    }

    player->ac = player->ac_behind;

    if (player == gbl.selected_player) {
        character_combat_display_summary(player);
    }
}

/* ovr013.sub_3A89E. Animate dead, run backwards: the affect ending is the raised
 * corpse collapsing again, and everything below puts the character back to what
 * they were before they were raised. */
static void affect_animate_dead(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "animate dead");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    /* Already running as the affect's own removal, so it must not be run again
     * when the entry comes off the list. */
    affect->call_affect_table = false;

    if (gbl.cure_spell == false) {
        effect_kill_player("collapses", STATUS_DEAD, player);
    }

    player->combat_team = affect->affect_data >> 4;
    player->quick_fight = QUICK_FIGHT_TRUE;
    player->field_E9 = 0;

    player->attack_level = (u8)player_skill_level(player, SKILL_FIGHTER);
    player->base_movement = 0x0c;

    if (player->control_morale == CONTROL_PC_BERZERK) {
        player->control_morale = CONTROL_PC_BASE;
    }

    player->monster_type = 0;
}

/* ovr013.AffectBlinded, sub_3A951 */
static void affect_blinded(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    gbl.attack_roll -= 4;

    player->ac -= 4;
    player->ac_behind -= 4;

    gbl.saving_throw_roll -= 4;
}

/* ovr013.AffectCauseDisease, sub_3A974. The first stage of the disease is the
 * other two put together: it weakens and it wastes. */
static void affect_cause_disease(Effect add_remove, void *param, Player *player)
{
    affect_table_call(add_remove, param, player, AFFECT_WEAKEN);
    affect_table_call(add_remove, param, player, AFFECT_CAUSE_DISEASE_2);
}

/* ovr013.AffectConfuse, sub_3A9D9. One roll a round decides what a confused
 * creature does: runs, stands about, turns on whoever is nearest, or rages
 * harmlessly. A save at -2 shakes it off. */
static void affect_confuse(Effect add_remove, void *param, Player *player)
{
    u8 roll = effect_roll_dice(100, 1);

    (void)add_remove;

    if (roll >= 1 && roll <= 10) {
        effect_remove_affect(NULL, AFFECT_CONFUSE, player);
        player_actions(player)->fleeing = true;
        player->quick_fight = QUICK_FIGHT_TRUE;

        if (player->control_morale < CONTROL_NPC_BASE) {
            player->control_morale = CONTROL_PC_BERZERK;
        }

        player_actions(player)->target = NULL;

        effect_apply_attack_spell_affect("runs away", false, DAMAGE_ON_SAVE_ZERO,
                                         true, 0, 10, AFFECT_FEAR, player);
    } else if (roll >= 11 && roll <= 60) {
        character_magic_attack_display("is confused", true, player);
        character_clear_text_area();
        affect_clear_actions(EFFECT_ADD, param, player);
    } else if (roll >= 61 && roll <= 80) {
        effect_apply_attack_spell_affect("goes berserk", false,
                                         DAMAGE_ON_SAVE_ZERO, true,
                                         player->combat_team, 1, AFFECT_89,
                                         player);
        affect_table_call(EFFECT_ADD, NULL, player, AFFECT_89);
    } else if (roll >= 81 && roll <= 100) {
        character_magic_attack_display("is enraged", true, player);
        character_clear_text_area();
    }

    if (effect_roll_saving_throw(-2, SAVE_VERSE_SPELL, player)) {
        effect_remove_affect(NULL, AFFECT_CONFUSE, player);
    }
}

/* ovr013.affect_curse, sub_3AB6F. The bestowed curse, four points off
 * everything. */
static void affect_bestow_curse(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    gbl.attack_roll -= 4;
    gbl.saving_throw_roll -= 4;
}

/* ovr013.AffectBlink, has_action_timedout. A blinking creature is gone for the
 * half of the round it is not acting in, and cannot be hit at all then. */
static void affect_blink(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (player_actions(player)->delay == 0) {
        gbl.target_invisible = true;
        gbl.attack_roll = -1;
    }
}

/* ovr013.AffectHaste, spl_age. Twice the actions, and a year off the character's
 * life the first time it is applied. */
static void affect_haste(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "haste");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if ((affect->affect_data & 0x10) == 0) {
        affect->affect_data += 0x10;

        character_display_status_string(true, 10, "ages", player);
        player->age++;
    }

    gbl.half_actions_left *= 2;
}

/* ovr013.StinkingCloudAffect, sub_3AC1D. The affect on the cloud itself running
 * out: the four squares it covered go back to the tiles that were under them,
 * and the clouds still standing are drawn again in case they overlapped it. */
static void affect_in_stinking_cloud(Effect add_remove, void *param,
                                     Player *player)
{
    Affect *affect = as_affect(param, "stinking cloud");
    int found = -1;

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    for (int i = 0; i < gbl.stinking_cloud_count; i++) {
        if (gbl.stinking_cloud[i].player == player &&
            gbl.stinking_cloud[i].field_1C == (affect->affect_data >> 4)) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        return;
    }

    character_print_message("The air clears a little...");

    for (int i = 0; i < SMALL_CLOUD_DIRECTION_COUNT; i++) {
        if (gbl.stinking_cloud[found].present[i]) {
            Point pos = point_add(
                gbl.stinking_cloud[found].target_pos,
                gbl_map_direction_delta(small_cloud_directions[i]));

            if (downed_player_at(pos)) {
                set_ground_tile(pos, TILE_DOWN_PLAYER);
            } else {
                set_ground_tile(pos, gbl.stinking_cloud[found].ground_tile[i]);
            }
        }
    }

    for (int i = found + 1; i < gbl.stinking_cloud_count; i++) {
        gbl.stinking_cloud[i - 1] = gbl.stinking_cloud[i];
    }
    gbl.stinking_cloud_count--;

    for (int c = 0; c < gbl.stinking_cloud_count; c++) {
        for (int i = 0; i < SMALL_CLOUD_DIRECTION_COUNT; i++) {
            if (gbl.stinking_cloud[c].present[i]) {
                Point pos = point_add(
                    gbl_map_direction_delta(small_cloud_directions[i]),
                    gbl.stinking_cloud[c].target_pos);

                set_ground_tile(pos, TILE_STINKING_CLOUD);
            }
        }
    }
}

/* ovr013.AvoidMissleAttack, sub_3AF06. A missile the target can dodge or bat
 * away: it only works against something thrown or fired, which is what the
 * attacker having a weapon at range 0 means here. */
static void avoid_missile_attack(int percentage, Player *player)
{
    Player *attacker = selected("avoid missile attack");

    if (attacker == NULL) {
        return;
    }

    if (player_primary_weapon(attacker) != NULL &&
        character_target_range(player, attacker) == 0 &&
        effect_roll_dice(100, 1) <= percentage) {
        character_display_status_string(true, 10, "Avoids it", player);
        gbl.damage = 0;
        gbl.attack_roll = -1;

        /* The attacker's count of landed first attacks, taken back down because
         * this one did not land. It is a byte and nothing stops it going below
         * zero, so a dodge in a round where attack 1 has not hit yet leaves the
         * count at 255; the original did the same. */
        gbl.attack_hit_count[1] -= 1;
    }
}

/* ovr013.get_primary_weapon, sub_3AF77. What the character is actually swinging:
 * the ammunition a bow is loaded with, if there is any, and otherwise the readied
 * weapon itself. */
static Item *get_primary_weapon(Player *player)
{
    Item *item = NULL;

    if (player == NULL) {
        return NULL;
    }

    if (player_primary_weapon(player) != NULL) {
        bool item_found = character_current_attack_item(&item, player);

        if (item_found == false || item == NULL) {
            item = player_primary_weapon(player);
        }
    }

    return item;
}

/* ovr013.AffectProtNormalMissles, sub_3AFE0 */
static void affect_prot_normal_missiles(Effect add_remove, void *param,
                                        Player *player)
{
    Item *item = get_primary_weapon(selected("protection from normal missiles"));

    (void)add_remove;
    (void)param;

    if (item != NULL && item->plus == 0) {
        avoid_missile_attack(100, player);
    }
}

/* ovr013.AffectSlow, sub_3B01B */
static void affect_slow(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    gbl.half_actions_left /= 2;
}

/* ovr013.weaken. A point of strength an hour while the disease runs. */
static void affect_weaken(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "weaken");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if (add_affect_unless_cured(0x3c, affect->affect_data, AFFECT_WEAKEN,
                                player)) {
        /* Only .full moves, as in the C#: this is a permanent loss rather than a
         * drain, and the rolled value goes with it. */
        if (player->stats.value[PSTAT_STR].full > 3) {
            character_display_status_string(true, 10, "is weakened", player);
            player->stats.value[PSTAT_STR].full--;
        } else if (player_has_affect(player, AFFECT_HELPLESS)) {
            /* A character already helpless is made helpless again, so one who is
             * not never becomes so however far the strength falls. sub_3B0C2
             * below does the same job with the test the other way round, which is
             * what shows this one up; both are left as they are. */
            effect_add_affect(false, 0xff, 0, AFFECT_HELPLESS, player);
        }
    }
}

/* ovr013.sub_3B0C2. The wasting half of a disease: a point of damage every ten
 * minutes, and helplessness once there is nothing left to take. */
static void affect_cause_disease_2(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "disease");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if (add_affect_unless_cured(10, affect->affect_data, AFFECT_CAUSE_DISEASE_2,
                                player)) {
        if (player->hit_point_current > 1) {
            gbl.damage_flags = 0;

            effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL, 1, player);

            if (gbl.game_state != GAME_STATE_COMBAT &&
                gbl.selected_player != NULL) {
                character_party_summary(gbl.selected_player);
            }
        } else if (player_has_affect(player, AFFECT_HELPLESS) == false) {
            effect_add_affect(false, 0xff, 0, AFFECT_HELPLESS, player);
        }
    }
}

/* ovr013.AffectDwarfGnomeVsGiants. The dwarf's and gnome's knack of ducking what
 * a big creature swings at them: the attacker is the one being looked at, not the
 * target the affect's owner has picked. */
static void affect_dwarf_gnome_vs_giants(Effect add_remove, void *param,
                                         Player *player)
{
    Player *attacker = selected("dwarf and gnome versus giants");

    (void)add_remove;
    (void)param;

    action_target(player);

    if (attacker == NULL) {
        return;
    }

    if (attacker->monster_type == MONSTER_GIANT ||
        attacker->monster_type == MONSTER_TROLL) {
        if ((attacker->field_DE & 0x7f) == 2) {
            gbl.attack_roll -= 4;
        }
    }
}

/* ovr013.sub_3B1A2 */
static void affect_vs_type_1(Effect add_remove, void *param, Player *player)
{
    Player *attacker = selected("affect 0x30");

    (void)add_remove;
    (void)param;
    (void)player;

    if (attacker == NULL) {
        return;
    }

    if (attacker->monster_type == MONSTER_TYPE_1 &&
        (attacker->field_DE & 0x7f) == 2) {
        gbl.attack_roll -= 4;
    }
}

/* ovr013.AffectPrayer, sub_3B1C9. The caster's side gets a point, the other side
 * loses one; the side that cast it is in bit 4 of the affect. */
static void affect_prayer(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "prayer");
    int team;

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    team = (affect->affect_data & 0x10) >> 4;

    if (player->combat_team == team) {
        gbl.saving_throw_roll += 1;
        gbl.attack_roll++;
    } else {
        gbl.attack_roll -= 1;
        gbl.saving_throw_roll -= 1;
    }
}

/* ovr013.HotFireShield, sub_3B212. The warm shield: cold is easier to save
 * against, and fire that gets through burns twice as hard. */
static void affect_hot_fire_shield(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_COLD) != 0) {
        gbl.saving_throw_roll += 2;
    } else if ((gbl.damage_flags & DAMAGE_FIRE) != 0 &&
               gbl.saving_throw_made == false) {
        gbl.damage *= 2;
    }
}

/* ovr013.ColdFireShield, sub_3B243. The cold shield, the other way about. */
static void affect_cold_fire_shield(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_FIRE) != 0) {
        gbl.saving_throw_roll += 2;
    } else if ((gbl.damage_flags & DAMAGE_COLD) != 0 &&
               gbl.saving_throw_made == false) {
        gbl.damage *= 2;
    }
}

/* ovr013.sub_3B27B. A ring of invisibility, which puts its wearer out of sight
 * for a round at a time. */
static void affect_item_invisibility(Effect add_remove, void *param,
                                     Player *player)
{
    (void)add_remove;
    (void)param;

    effect_add_affect(false, 12, 1, AFFECT_INVISIBILITY, player);
}

/* ovr013.AffectClearMovement, sub_3B29A */
static void affect_clear_movement(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    player_actions(player)->move = 0;

    if (gbl.reset_moves_left) {
        gbl.half_actions_left = 0;
    }
}

/* ovr013.AffectRegenration. The regeneration spell, which is three hit points a
 * round for as long as it lasts. */
static void affect_regenerate(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    effect_add_affect(false, 0xff, 0, AFFECT_REGEN_3_HP, player);
}

/* ovr013.AffectResistWeapons, sub_3B2D8. Nothing at all from a plain weapon,
 * half from a +1 or +2, all of it from anything better. */
static void affect_resist_weapons(Effect add_remove, void *param, Player *player)
{
    Item *weapon = get_primary_weapon(selected("resist normal weapons"));

    (void)add_remove;
    (void)param;
    (void)player;

    if (weapon == NULL || weapon->plus == 0) {
        gbl.damage = 0;
    } else if (weapon->plus < 3) {
        gbl.damage /= 2;
    }
}

/* ovr013.AffectFireResist. Two points off every die of fire damage, never below
 * one point a die, and fire that is not also magical is shrugged off. */
static void affect_fire_resist(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_FIRE) != 0) {
        for (int i = 1; i <= gbl.dice_count; i++) {
            gbl.damage -= 2;

            if (gbl.damage < gbl.dice_count) {
                gbl.damage = gbl.dice_count;
            }
        }

        gbl.saving_throw_roll += 4;

        if ((gbl.damage_flags & DAMAGE_MAGIC) == 0) {
            set_protected();
        }
    }
}

/* ovr013.AffectHighConRegen, sub_3B386. A constitution of 20 or more heals a hit
 * point on its own: every six turns at 20, down to every turn at 25.
 *
 * The C# guards this on the constitution being high enough and labels the guard a
 * bugfix of its own, since the affect stays on characters who have since lost the
 * constitution that earned it. The C# is what is being translated, so the guard
 * is kept. */
static void affect_high_con_regen(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "high constitution regeneration");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if (player->stats.value[PSTAT_CON].full >= 20) {
        u16 rounds = (u16)((26 - player->stats.value[PSTAT_CON].full) * 10);

        if (add_affect_unless_cured(rounds, affect->affect_data,
                                    AFFECT_HIGH_CON_REGEN, player) &&
            effect_heal_player(1, 1, player)) {
            character_describe_healing(player);
        }
    }
}

/* ovr013.AffectMinorGlobeOfInvulnerability, sub_3B3CA. Nothing of the first three
 * spell levels gets through. */
static void affect_minor_globe(Effect add_remove, void *param, Player *player)
{
    const SpellEntry *entry;

    (void)add_remove;
    (void)param;
    (void)player;

    if (gbl.spell_id <= 0) {
        return;
    }

    entry = spell_entry(gbl.spell_id);

    if (entry != NULL && entry->spell_level < 4) {
        set_protected();
    }
}

/* ovr013.PoisonAttack. A monster's poisonous bite: the save is against poison and
 * failing it kills outright. */
static void poison_attack(int save_bonus, Player *player)
{
    Player *target = action_target(player);

    if (target == NULL) {
        return;
    }

    if (effect_roll_saving_throw(save_bonus, SAVE_VERSE_POISON, target) == false) {
        character_display_status_string(false, 10, "is Poisoned", target);
        text_game_delay();
        effect_add_affect(false, 0xff, 0, AFFECT_POISONED, target);

        effect_kill_player("is killed", STATUS_DEAD, target);
    }
}

/* ovr013.AffectPoisonPlus0, sub_3B520 */
static void affect_poison_plus_0(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    poison_attack(0, player);
}

/* ovr013.AffectPoisonPlus4, sub_3B534 */
static void affect_poison_plus_4(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    poison_attack(4, player);
}

/* ovr013.AffectPoisonPlus2, sub_3B548 */
static void affect_poison_plus_2(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    poison_attack(2, player);
}

/* ovr013.ThriKreenParalyze, sub_3B55C */
static void affect_thri_kreen_paralyze(Effect add_remove, void *param,
                                       Player *player)
{
    /* Rolled before the target is looked up, as in the C#, so the sequence of
     * rolls is the same whether or not there is one. */
    u16 time = effect_roll_dice(8, 2);
    Player *target = action_target(player);

    (void)add_remove;
    (void)param;

    if (target == NULL) {
        return;
    }

    if (effect_roll_saving_throw(0, SAVE_VERSE_POISON, target) == false) {
        character_magic_attack_display("is Paralyzed", true, target);
        effect_add_affect(false, 12, time, AFFECT_PARALYZE, target);
    }
}

/* ovr013.AffectFeebleMind, spell_stupid */
static void affect_feeblemind(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    player->stats.value[PSTAT_INT].full = 7;
    player->stats.value[PSTAT_WIS].full = 7;

    character_display_status_string(true, 10, "is stupid", player);

    if (gbl.game_state == GAME_STATE_COMBAT) {
        effect_try_loose_spell(player);
    }
}

/* ovr013.AffectInvisToAnimals, sub_3B636 */
static void affect_invisible_to_animals(Effect add_remove, void *param,
                                        Player *player)
{
    Player *attacker = selected("invisibility to animals");

    (void)add_remove;
    (void)param;
    (void)player;

    if (attacker == NULL) {
        return;
    }

    if (attacker->monster_type == MONSTER_ANIMAL) {
        if (player_has_affect(attacker, AFFECT_DETECT_INVISIBILITY) == false) {
            gbl.target_invisible = true;
        }

        gbl.attack_roll -= 4;
    }
}

/* ovr013.AffectPoisonNeg2, sub_3B671 */
static void affect_poison_neg_2(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    poison_attack(-2, player);
}

/* ovr013.AffectInvisible, sub_3B685. Invisibility that does not care whether the
 * attacker can see through it. */
static void affect_invisible(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    gbl.target_invisible = true;
    gbl.attack_roll -= 4;
}

/* ovr013.AffectCamouflage, sub_3B696 */
static void affect_camouflage(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (effect_roll_dice(100, 1) <= 95) {
        effect_add_affect(false, 12, 1, AFFECT_INVISIBILITY, player);
    }
}

/* ovr013.ProtDragonsBreath */
static void affect_prot_dragon_breath(Effect add_remove, void *param,
                                      Player *player)
{
    (void)add_remove;
    (void)param;

    if ((gbl.damage_flags & DAMAGE_DRAGON_BREATH) > 0) {
        set_protected();
        character_display_status_string(true, 10, "is unaffected", player);
    }
}

/* ovr013.AffectDragonSlayer, sub_3B71A. 3d12+4 and the strength bonus, in place
 * of whatever the weapon would have done. */
static void affect_dragon_slayer(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (player->actions != NULL && player->actions->target != NULL) {
        gbl.spell_target = player->actions->target;

        if (gbl.spell_target->monster_type == MONSTER_DRAGON) {
            gbl.damage = (effect_roll_dice(12, 1) * 3) + 4 +
                         character_strength_dam_bonus(player);
            gbl.attack_roll += 2;
        }
    }
}

/* ovr013.AffectFrostBrand, sub_3B772 */
static void affect_frost_brand(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (player->actions != NULL) {
        gbl.spell_target = player->actions->target;

        if (gbl.spell_target != NULL &&
            gbl.spell_target->monster_type == MONSTER_FIRE) {
            gbl.attack_roll += 3;
            gbl.damage += 3;
        }
    }
}

/* ovr013.AffectBerzerk. Turns on whoever is nearest, whichever side they are on,
 * and cannot cast while it lasts. */
static void affect_berserk(Effect add_remove, void *param, Player *player)
{
    (void)param;

    if (add_remove == EFFECT_ADD) {
        player->quick_fight = QUICK_FIGHT_TRUE;

        if (player->control_morale < CONTROL_NPC_BASE ||
            player->control_morale == CONTROL_PC_BERZERK) {
            player->control_morale = CONTROL_PC_BERZERK;
        } else {
            player->control_morale = CONTROL_NPC_BERZERK;
        }

        if (gbl.game_state == GAME_STATE_COMBAT) {
            Action *action = player_actions(player);
            Player *target;

            action->target = NULL;

            target = nearest_combatant(player);

            if (target == NULL) {
                return;
            }

            action->target = target;
            action->can_cast = false;
            player->combat_team = player_opposite_team(target);

            character_display_status_string(true, 10, "goes berzerk", player);
        }
    } else {
        if (player->control_morale == CONTROL_PC_BERZERK) {
            player->control_morale = CONTROL_PC_BASE;
        }

        player->combat_team = TEAM_OURS;
    }
}

/* ovr013.sub_3B8D9. Back on their feet with what they had left, and if there is
 * no room on the map to stand up in, again next round. */
static void affect_4e_handler(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;

    if (effect_combat_heal(player->hit_point_current, player) == false) {
        /* Looked at only here, because that is where the C# read it: the cast at
         * the top of its body was a null cast, which is legal, and the throw
         * would only have come on this branch. effect.c's healing path calls this
         * with no affect at all, and it heals, so it never reaches it. */
        Affect *affect = as_affect(param, "affect 0x4e");

        if (affect != NULL) {
            add_affect_unless_cured(1, affect->affect_data, AFFECT_4E, player);
        }
    }
}

/* ovr013.MagicFireAttack_2d10, sub_3B919 */
static void affect_fire_attack_2d10(Effect add_remove, void *param, Player *player)
{
    int damage;
    Player *target;

    (void)add_remove;
    (void)param;

    gbl.damage_flags = DAMAGE_MAGIC | DAMAGE_FIRE;

    /* The roll is made before the target is read, as the C#'s argument order
     * did, so a missing target does not shift the dice. */
    damage = effect_roll_dice_save(10, 2);
    target = player_actions(player)->target;

    if (target == NULL) {
        log_warn("affect table: a 2d10 fire attack with nothing to burn");
        return;
    }

    effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL, damage, target);
}

/* ovr013.AnkhegAcidAttack, sub_3B94C */
static void affect_ankheg_acid_attack(Effect add_remove, void *param,
                                      Player *player)
{
    int damage;
    Player *target;

    (void)add_remove;
    (void)param;

    gbl.damage_flags = DAMAGE_ACID;

    damage = effect_roll_dice_save(4, 1);
    target = player_actions(player)->target;

    if (target == NULL) {
        log_warn("affect table: an acid attack with nothing to spit at");
        return;
    }

    effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL, damage, target);
}

/* ovr013.half_damage, sub_3B97F */
static void affect_half_damage(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    gbl.damage /= 2;
}

/* ovr013.AffectResistFireAndCold, sub_3B990. A save takes it all away when the
 * spell is one that a save halves, and halves it otherwise. */
static void affect_resist_fire_and_cold(Effect add_remove, void *param,
                                        Player *player)
{
    (void)add_remove;
    (void)param;

    if ((gbl.damage_flags & DAMAGE_FIRE) != 0 ||
        (gbl.damage_flags & DAMAGE_COLD) != 0) {
        const SpellEntry *entry = (gbl.spell_id > 0) ? spell_entry(gbl.spell_id)
                                                     : NULL;

        if (effect_roll_saving_throw(0, SAVE_VERSE_SPELL, player) &&
            gbl.spell_id > 0 && entry != NULL && entry->damage_on_save != 0) {
            gbl.damage = 0;
        } else {
            gbl.damage /= 2;
        }
    }
}

/* ovr013.AffectShamblerAbsorbLightning, sub_3B9E1. A shambling mound drinks the
 * lightning and grows harder to hit on it. */
static void affect_shambler_absorb_lightning(Effect add_remove, void *param,
                                             Player *player)
{
    (void)add_remove;
    (void)param;

    if ((gbl.damage_flags & DAMAGE_ELECTRICITY) != 0) {
        set_protected();

        player->ac += 8;
    }
}

/* ovr013.sub_3BA14. One point from a weapon of the kind this creature barely
 * feels. */
static void affect_55_handler(Effect add_remove, void *param, Player *player)
{
    Item *item = get_primary_weapon(selected("affect 0x55"));

    (void)add_remove;
    (void)param;
    (void)player;

    if (item != NULL && item_data(item->type)->field_7 == 1) {
        gbl.damage = 1;
    }
}

/* ovr013.AffectDisplace, sub_3BA55. The displacer's first miss of the fight: bit
 * 4 remembers that it has been spent, and the opening round clears it again. */
static void affect_displace(Effect add_remove, void *param, Player *player)
{
    Affect *affect = (Affect *)param;

    (void)add_remove;
    (void)player;

    if (affect != NULL) {
        if (gbl.combat_round == 0 && gbl.attack_roll == 0) {
            affect->affect_data &= 0x0f;
        } else if ((affect->affect_data & 0x10) == 0) {
            gbl.attack_roll = -1;
            affect->affect_data |= 0x10;
        }
    }
}

/* ovr013.CloudKillAffect, sub_3BAB9. The same as the stinking cloud above, over
 * the nine squares a cloudkill covers. */
static void affect_in_cloud_kill(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "cloudkill");
    int found = -1;

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    for (int i = 0; i < gbl.cloud_kill_count; i++) {
        if (gbl.cloud_kill_cloud[i].player == player &&
            gbl.cloud_kill_cloud[i].field_1C == (affect->affect_data >> 4)) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        return;
    }

    character_print_message("The air clears a little...");

    for (int i = 0; i < CLOUD_DIRECTION_COUNT; i++) {
        if (gbl.cloud_kill_cloud[found].present[i]) {
            Point pos = point_add(gbl.cloud_kill_cloud[found].target_pos,
                                  gbl_map_direction_delta(cloud_directions[i]));

            if (downed_player_at(pos)) {
                set_ground_tile(pos, TILE_DOWN_PLAYER);
            } else {
                set_ground_tile(pos, gbl.cloud_kill_cloud[found].ground_tile[i]);
            }
        }
    }

    for (int i = found + 1; i < gbl.cloud_kill_count; i++) {
        gbl.cloud_kill_cloud[i - 1] = gbl.cloud_kill_cloud[i];
    }
    gbl.cloud_kill_count--;

    for (int c = 0; c < gbl.cloud_kill_count; c++) {
        for (int i = 0; i < CLOUD_DIRECTION_COUNT; i++) {
            if (gbl.cloud_kill_cloud[c].present[i]) {
                Point pos = point_add(gbl.cloud_kill_cloud[c].target_pos,
                                      gbl_map_direction_delta(cloud_directions[i]));

                set_ground_tile(pos, TILE_CLOUD_KILL);
            }
        }
    }
}

/* ovr013.half_fire_damage, sub_3BD98 */
static void affect_half_fire_damage(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_FIRE) != 0) {
        gbl.damage /= 2;
    }
}

/* ovr013.sub_3BDB2 */
static void affect_5e_handler(Effect add_remove, void *param, Player *player)
{
    Item *item = get_primary_weapon(selected("affect 0x5e"));

    (void)add_remove;
    (void)param;
    (void)player;

    if (item != NULL && (item_data(item->type)->field_7 & 0x81) != 0) {
        gbl.damage /= 2;
    }
}

/* ovr013.sub_3BE06. The affect that finishes off whoever is carrying it when it
 * runs out, if they are still in the fight. */
static void affect_5f_handler(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "affect 0x5f");

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    affect->call_affect_table = false;

    if (player->in_combat) {
        effect_kill_player("Falls dead", STATUS_DEAD, player);
    }
}

/* ovr013.con_saving_bonus, sub_3BE42. A constitution bonus on saves against
 * spells and wands, up to five points at 18 and over. */
static void affect_con_saving_bonus(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (gbl.save_verse_type == SAVE_VERSE_SPELL ||
        gbl.save_verse_type == SAVE_VERSE_ROD_STAFF_WAND) {
        int con = player->stats.value[PSTAT_CON].full;
        int save_bonus = 0;

        if (con >= 4 && con <= 6) {
            save_bonus = 1;
        } else if (con >= 7 && con <= 10) {
            save_bonus = 2;
        } else if (con >= 11 && con <= 13) {
            save_bonus = 3;
        } else if (con >= 14 && con <= 17) {
            save_bonus = 4;
        } else if (con >= 18 && con <= 20) {
            save_bonus = 5;
        }

        gbl.saving_throw_roll += save_bonus;
    }
}

/* ovr013.AffectRegen3Hp, sub_3BEB8 */
static void affect_regen_3_hp(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    player->hit_point_current += 3;

    if (player->hit_point_current > player->hit_point_max) {
        player->hit_point_current = player->hit_point_max;
    }
}

/* ovr013.sub_3BEE8. A monster that gets back up: enough hit points to stop the
 * bleeding, or six if it was only knocked out, and then a few rounds of affect
 * 0x5f, which kills it if it is still standing when they run out. */
static void affect_63_handler(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "affect 0x63");
    u8 heal_amount = 0;

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if (player->health_status == STATUS_DYING &&
        player_actions(player)->bleeding < 6) {
        heal_amount = (u8)(6 - player_actions(player)->bleeding);
    }

    if (player->health_status == STATUS_UNCONSCIOUS) {
        heal_amount = 6;
    }

    if (heal_amount > 0 && effect_combat_heal(heal_amount, player)) {
        effect_add_affect(true, 0xff, (u16)(effect_roll_dice(4, 1) + 1),
                          AFFECT_5F, player);
        affect->call_affect_table = false;
        effect_remove_affect(affect, AFFECT_63, player);
    }
}

/* ovr013.AffectTrollFireOrAcid. A troll knits itself back together unless the
 * blow was fire or acid. */
static void affect_troll_fire_or_acid(Effect add_remove, void *param,
                                      Player *player)
{
    (void)add_remove;
    (void)param;

    if ((gbl.damage_flags & DAMAGE_FIRE) == 0 &&
        (gbl.damage_flags & DAMAGE_ACID) == 0) {
        effect_add_affect(true, 0xff, effect_roll_dice(6, 3),
                          AFFECT_TROLL_REGEN_2, player);
    }
}

/* ovr013.AffectTrollRegenerate, sp_regenerate */
static void affect_troll_regenerate(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    if (player_has_affect(player, AFFECT_REGEN_3_HP) == false &&
        player_has_affect(player, AFFECT_REGENERATE) == false) {
        effect_add_affect(true, 0xff, 3, AFFECT_REGENERATE, player);
    }
}

/* ovr013.AffectTrollRegen, sub_3C01E. The troll standing up again, and trying
 * once more next round if there is nowhere to stand. */
static void affect_troll_regen(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;

    if (effect_combat_heal(player->hit_point_max, player) == false) {
        /* Read on this branch only, as in affect 0x4e above. */
        Affect *affect = as_affect(param, "troll regeneration");

        if (affect != NULL) {
            add_affect_unless_cured(1, affect->affect_data, AFFECT_TROLL_REGEN_2,
                                    player);
        }
    }
}

/* ovr013.AffectSalamanderHeatDamage, sub_3C05D. The heat of the thing, on top of
 * the blow, unless the target is warded against fire. */
static void affect_salamander_heat_damage(Effect add_remove, void *param,
                                          Player *player)
{
    Player *target = action_target(player);

    (void)add_remove;
    (void)param;

    if (target == NULL) {
        return;
    }

    if (player_has_affect(target, AFFECT_RESIST_FIRE) == false &&
        player_has_affect(target, AFFECT_COLD_FIRE_SHIELD) == false &&
        player_has_affect(target, AFFECT_FIRE_RESIST) == false) {
        gbl.damage += effect_roll_dice(6, 1);
    }
}

/* ovr013.sub_3C0DA. The thri-kreen's knack of batting a missile out of the air. */
static void affect_thri_kreen_dodge(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    avoid_missile_attack(60, player);
}

/* ovr013.ResistMagicPercent, sub_3C0EE. Magic resistance, harder to get through
 * the fewer creatures the spell was aimed at. */
static void resist_magic_percent(int roll_base)
{
    int target_count = character_spell_max_target_count(gbl.spell_id);
    int roll_needed = roll_base + ((11 - target_count) * 5);

    if (gbl.current_affect != 0 || (gbl.damage_flags & DAMAGE_MAGIC) != 0) {
        if (effect_roll_dice(100, 1) <= roll_needed) {
            set_protected();
        }
    }
}

/* ovr013.ResistMagic50Percent, sub_3C14F */
static void affect_resist_magic_50(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    resist_magic_percent(50);
}

/* ovr013.ResistMagic15Percent, sub_3C15D */
static void affect_resist_magic_15(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    resist_magic_percent(15);
}

/* ovr013.AffectElfRisistSleep, sub_3C16B */
static void affect_elf_resist_sleep(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if (effect_roll_dice(100, 1) <= 90) {
        protected_if(AFFECT_SLEEP);
        protected_if(AFFECT_CHARM_PERSON);
    }
}

/* ovr013.AffectProtCharmSleep, sub_3C18F */
static void affect_prot_charm_sleep(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    protected_if(AFFECT_CHARM_PERSON);
    protected_if(AFFECT_SLEEP);
}

/* ovr013.ResistParalyze, sub_3C1A4 */
static void affect_resist_paralyze(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    protected_if(AFFECT_PARALYZE);
}

/* ovr013.AffectImmuneToCold, sub_3C1B2 */
static void affect_immune_to_cold(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_COLD) != 0) {
        set_protected();
    }
}

/* ovr013.sub_3C1C9. Proof against poison and paralysis, and a save against
 * poison that cannot be failed. */
static void affect_6f_handler(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    protected_if(AFFECT_POISONED);
    protected_if(AFFECT_PARALYZE);

    if (gbl.save_verse_type == SAVE_VERSE_POISON) {
        gbl.saving_throw_roll = 100;
    }
}

/* ovr013.AffectImmuneToFire, sub_3C1EA */
static void affect_immune_to_fire(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_FIRE) != 0) {
        set_protected();
    }
}

/* ovr013.sub_3C201. A point off each die of fire damage, never below one a die. */
static void affect_71_handler(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_FIRE) != 0) {
        for (int i = 1; i <= gbl.dice_count; i++) {
            gbl.damage--;

            if (gbl.damage < gbl.dice_count) {
                gbl.damage = gbl.dice_count;
            }
        }
    }
}

/* ovr013.AffectProtectionFromElectricity, sub_3C246 */
static void affect_prot_from_electricity(Effect add_remove, void *param,
                                         Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_ELECTRICITY) != 0) {
        gbl.damage /= 2;
    }
}

/* ovr013.sub_3C260 */
static void affect_73_handler(Effect add_remove, void *param, Player *player)
{
    Item *weapon = get_primary_weapon(selected("affect 0x73"));

    (void)add_remove;
    (void)param;
    (void)player;

    if (weapon != NULL) {
        if (item_data(weapon->type)->field_7 == 0 ||
            (item_data(weapon->type)->field_7 & 1) != 0) {
            gbl.damage /= 2;
        }
    }
}

/* ovr013.half_damage_if_weap_magic, sub_3C2BF */
static void affect_half_damage_if_weapon_magic(Effect add_remove, void *param,
                                               Player *player)
{
    Item *weapon = get_primary_weapon(selected("affect 0x74"));

    (void)add_remove;
    (void)param;
    (void)player;

    if (weapon != NULL && weapon->plus > 0) {
        gbl.damage /= 2;
    }
}

/* ovr013.sub_3C2F9. The readied weapon itself here rather than what a bow is
 * loaded with, unlike its neighbours. */
static void affect_75_handler(Effect add_remove, void *param, Player *player)
{
    Player *attacker = selected("affect 0x75");
    Item *item;

    (void)add_remove;
    (void)param;
    (void)player;

    if (attacker == NULL) {
        return;
    }

    item = player_primary_weapon(attacker);

    if (item != NULL && item->type == ITEM_TYPE_85) {
        gbl.damage = effect_roll_dice_save(6, 1) + 1;
    }
}

/* ovr013.AffectProtCold, sub_3C33C */
static void affect_prot_cold(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_COLD) != 0) {
        gbl.damage /= 2;
    }
}

/* ovr013.AffectProtNonMagicWeapons, sub_3C356. Nothing from a plain weapon in
 * the hands of anything but a human of four hit dice or more. */
static void affect_prot_non_magic_weapons(Effect add_remove, void *param,
                                          Player *player)
{
    Player *attacker = selected("protection from non-magical weapons");
    Item *weapon = get_primary_weapon(attacker);

    (void)add_remove;
    (void)param;
    (void)player;

    if (attacker == NULL) {
        return;
    }

    if ((weapon == NULL || weapon->plus == 0) &&
        (attacker->race > 0 || attacker->hit_dice < 4)) {
        gbl.damage = 0;
    }
}

/* ovr013.sub_3C3A2. Reads the affect's own owner's weapon rather than the
 * attacker's: the spiritual hammer carries this affect, and it is the hammer that
 * bats the missile away. */
static void affect_78_handler(Effect add_remove, void *param, Player *player)
{
    Item *weapon = player_primary_weapon(player);

    (void)add_remove;
    (void)param;

    if (weapon != NULL) {
        if (weapon->type == ITEM_TYPE_87 || weapon->type == ITEM_TYPE_88) {
            avoid_missile_attack(50, player);
        }
    }
}

/* ovr013.sub_3C3F6. A one-in-four chance of spitting acid at a target within
 * four squares, in place of the round's attack; the creature has one spit in it
 * and loses both affects when it uses it. */
static void affect_79_handler(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "affect 0x79");
    Player *target = action_target(player);

    (void)add_remove;

    if (affect == NULL) {
        return;
    }

    if (effect_roll_dice(100, 1) <= 25) {
        if (target == NULL) {
            log_warn("affect table: acid to spit and nothing to spit it at");
            return;
        }

        if (character_target_range(target, player) < 4) {
            int damage;
            bool saved;

            character_clear_actions(player);

            character_display_status_string(true, 10, "Spits Acid", player);

            character_load_missile_icons(0x17);

            character_draw_missile_attack(0x1e, 1,
                                          combatmap_player_map_pos(target),
                                          combatmap_player_map_pos(player));

            damage = effect_roll_dice_save(4, 8);
            saved = effect_roll_saving_throw(0, SAVE_VERSE_BREATH_WEAPON, target);

            effect_damage_person(saved, DAMAGE_ON_SAVE_HALF, damage, target);

            effect_remove_affect(affect, AFFECT_79, player);
            effect_remove_affect(NULL, AFFECT_ANKHEG_ACID_ATTACK, player);
        }
    }
}

/* ovr013.AffectDracolichParalysis, spl_paralyze */
static void affect_dracolich_paralysis(Effect add_remove, void *param,
                                       Player *player)
{
    Player *target = action_target(player);

    (void)add_remove;
    (void)param;

    if (target == NULL) {
        return;
    }

    if (effect_roll_saving_throw(0, SAVE_VERSE_POISON, target) == false) {
        effect_add_affect(false, 0xff, 0, AFFECT_PARALYZE, target);

        character_display_status_string(true, 10, "is paralyzed", target);
    }
}

/* ovr013.sub_3C59D */
static void affect_7b_handler(Effect add_remove, void *param, Player *player)
{
    int damage;
    Player *target;

    (void)add_remove;
    (void)param;

    gbl.damage_flags = DAMAGE_ACID;

    damage = effect_roll_dice_save(8, 2);
    target = player_actions(player)->target;

    if (target == NULL) {
        log_warn("affect table: a 2d8 acid attack with nothing to spit at");
        return;
    }

    effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL, damage, target);
}

/* ovr013.AffectHalfElfResistance, sub_3C5D0 */
static void affect_half_elf_resistance(Effect add_remove, void *param,
                                       Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if (effect_roll_dice(100, 1) <= 30) {
        protected_if(AFFECT_CHARM_PERSON);
        protected_if(AFFECT_SLEEP);
    }
}

/* ovr013.sub_3C5F4. Proof against the four mind and body affects, and a save
 * that cannot be failed against anything but poison. */
static void affect_7d_handler(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    protected_if(AFFECT_CHARM_PERSON);
    protected_if(AFFECT_SLEEP);
    protected_if(AFFECT_PARALYZE);
    protected_if(AFFECT_POISONED);

    if (gbl.save_verse_type != SAVE_VERSE_POISON) {
        gbl.saving_throw_roll = 100;
    }
}

/* ovr013.AffectProtMagic, sub_3C623 */
static void affect_prot_magic(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if (gbl.current_affect != 0 || (gbl.damage_flags & DAMAGE_MAGIC) != 0) {
        set_protected();
    }
}

/* ovr013.sub_3C643. The quarrel that ends the creature it hits outright - name
 * part 3 of 0x87 is the one - and takes it off the map. */
static void affect_82_handler(Effect add_remove, void *param, Player *player)
{
    Player *attacker = selected("affect 0x82");
    Item *item = NULL;

    (void)add_remove;
    (void)param;

    if (attacker == NULL) {
        return;
    }

    if (character_current_attack_item(&item, attacker) && item != NULL &&
        item->type == ITEM_QUARREL && item->namenum3 == 0x87) {
        player->health_status = STATUS_GONE;
        player->in_combat = false;
        player->hit_point_current = 0;
        effect_remove_combat_affects(player);
        effect_check_affects(player, CHECK_TYPE_DEATH);

        if (player->in_combat) {
            combatmap_combatant_killed(player);
        }
    }
}

/* ovr013.do_items_affect, sub_3C6D3. A magic item's own affect, hung on its
 * wearer when it is readied and taken off again when it is not. This is where
 * gbl.apply_item_affect ends: the flag brought the call here, and it is cleared
 * so that the add_affect below does not come straight back. */
static void affect_do_items_affect(Effect add_remove, void *param, Player *player)
{
    Item *item = (Item *)param;

    gbl.apply_item_affect = false;

    if (item == NULL) {
        log_warn("affect table: an item's affect with no item");
        return;
    }

    if (add_remove == EFFECT_REMOVE) {
        effect_remove_affect(NULL, (Affects)item->affect_2, player);
    } else {
        effect_add_affect(true, 0xff, 0, (Affects)item->affect_2, player);

        /* Outside a fight nothing else will ask the affect what it does, so it
         * is asked here. */
        if (gbl.game_state != GAME_STATE_COMBAT) {
            affect_table_call(EFFECT_ADD, NULL, player, (Affects)item->affect_2);
        }
    }
}

/* ovr013.AffectDracolichA, sub_3C750 */
static void affect_85_handler(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    protected_if(AFFECT_FEAR);
    protected_if(AFFECT_RAY_OF_ENFEEBLEMENT);
    protected_if(AFFECT_FEEBLEMIND);

    if ((gbl.damage_flags & DAMAGE_ELECTRICITY) != 0) {
        set_protected();
    }
}

/* ovr013.AffectRangerVsGiant, sub_3C77C. A point a ranger level against a
 * giant-kind. */
static void affect_ranger_vs_giant(Effect add_remove, void *param, Player *player)
{
    Player *target = action_target(player);

    (void)add_remove;
    (void)param;

    if (target == NULL) {
        return;
    }

    if ((target->field_14B & 8) != 0) {         /* giant */
        gbl.damage += player->class_level[SKILL_RANGER];
    }
}

/* ovr013.AffectProtElec, sub_3C7B5 */
static void affect_prot_elec(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;

    if ((gbl.damage_flags & DAMAGE_ELECTRICITY) != 0) {
        set_protected();
    }
}

/* ovr013.AffectEntangle, sub_3C7CC */
static void affect_entangle(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    player_actions(player)->move = 0;
}

/* ovr013.sub_3C7E0. The berserk rage a confusion roll brings on: like affect
 * 0x4d above, but the side to go back to is kept in the affect rather than
 * assumed to be ours. */
static void affect_89_handler(Effect add_remove, void *param, Player *player)
{
    Affect *affect = as_affect(param, "affect 0x89");

    if (affect == NULL) {
        return;
    }

    if (add_remove == EFFECT_ADD) {
        Action *action = player_actions(player);
        Player *target;

        player->quick_fight = QUICK_FIGHT_TRUE;

        if (player->control_morale < CONTROL_NPC_BASE ||
            player->control_morale == CONTROL_PC_BERZERK) {
            player->control_morale = CONTROL_PC_BERZERK;
        } else {
            player->control_morale = CONTROL_NPC_BERZERK;
        }

        action->target = NULL;

        target = nearest_combatant(player);

        if (target == NULL) {
            return;
        }

        action->target = target;
        player->combat_team = player_opposite_team(target);
    } else {
        if (player->control_morale == CONTROL_PC_BERZERK) {
            player->control_morale = 0;
        }

        player->combat_team = affect->affect_data;
    }
}

/* ovr013.add_affect_19 */
static void affect_8a_handler(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;

    effect_add_affect(false, 0xff, 0xff, AFFECT_INVISIBILITY, player);
}

/* ovr013.PaladinCastCureRefresh, sub_3C8EF. A paladin's lay-on-hands, back at
 * the start of the day: one use per five levels, rounded up. */
static void affect_paladin_cure_refresh(Effect add_remove, void *param,
                                        Player *player)
{
    (void)param;

    if (add_remove == EFFECT_REMOVE) {
        player->paladin_cures_left =
            (u8)(((player_skill_level(player, SKILL_PALADIN) - 1) / 5) + 1);
    }
}

/* ovr013.AffectFear, sub_3C932 */
static void affect_fear(Effect add_remove, void *param, Player *player)
{
    (void)param;

    if (add_remove == EFFECT_REMOVE) {
        if (player->control_morale == CONTROL_PC_BERZERK) {
            player->control_morale = CONTROL_PC_BASE;
            player->quick_fight = QUICK_FIGHT_FALSE;
        }

        player_actions(player)->fleeing = false;
    }
}

/* ovr013.sub_3C975. The evil creature that shrugs off dispel evil turns it back
 * on the caster, at twice the damage, if the caster is standing next to it. */
static void affect_8f_handler(Effect add_remove, void *param, Player *target)
{
    Player *caster = selected("affect 0x8f");

    (void)add_remove;
    (void)param;

    if (caster == NULL) {
        return;
    }

    if (character_target_range(target, caster) < 2) {
        int bkup_damage = gbl.damage;
        int bkup_damage_flags = gbl.damage_flags;

        gbl.damage *= 2;
        gbl.damage_flags = DAMAGE_MAGIC;

        character_display_status_string(true, 10, "resists dispel evil", caster);

        effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL, gbl.damage, caster);

        gbl.damage = bkup_damage;
        gbl.damage_flags = bkup_damage_flags;
    }
}

/* ovr013.sp_dispel_evil. The spell itself, resolved against the caster's target:
 * an evil creature that fails its save is banished, and the spell is spent. */
static void affect_sp_dispel_evil(Effect add_remove, void *param, Player *player)
{
    Player *target = action_target(player);
    Player *caster;

    (void)add_remove;
    (void)param;

    if (target == NULL) {
        return;
    }

    if ((target->field_14B & 1) != 0 &&
        effect_roll_saving_throw(0, SAVE_VERSE_SPELL, target) == false) {
        effect_kill_player("is dispelled", STATUS_GONE, target);

        caster = selected("dispel evil");

        if (caster != NULL) {
            effect_remove_affect(NULL, AFFECT_DISPEL_EVIL, caster);
            effect_remove_affect(NULL, AFFECT_SP_DISPEL_EVIL, caster);
        }
    } else {
        character_display_status_string(true, 10, "resists dispel evil", target);
    }
}

/* ovr013.empty. The affects that are only a flag for something else to read:
 * detect magic, enlarge, friends, read magic, find traps, detect invisibility,
 * strength, being poisoned, and the paladin's daily cure. */
static void affect_empty(Effect add_remove, void *param, Player *player)
{
    (void)add_remove;
    (void)param;
    (void)player;
}

/* ---------------------------------------------------------------- the table */

typedef void (*AffectHandler)(Effect add_remove, void *param, Player *player);

typedef struct {
    AffectHandler fn;
    /* The C# function that will fill fn in, for an affect whose handler belongs
     * to an overlay that is not translated. Every entry is NULL now that the last
     * overlay has landed; the field and report_pending below stay because they are
     * how the table says so out loud rather than silently doing nothing, and that
     * is worth having if something turns out to have been missed. */
    const char   *pending;
} AffectEntry;

#define AFFECT_TABLE_SIZE (AFFECT_DO_ITEMS_AFFECT + 1)

/* ovr013.SetupAffectTables, setup_spells2, in its own order. The C# filled a
 * Dictionary at startup and looked an affect up in it; the affect ids run from 1
 * to 0x93 and each appears exactly once, so an array indexed by id says the same
 * thing and needs no setting up. Id 0 - no affect - has no entry, which is what
 * the C# dictionary had for it too. */
static const AffectEntry g_affect_table[AFFECT_TABLE_SIZE] = {
    [AFFECT_BLESS]                      = { affect_bless, NULL },
    [AFFECT_CURSED]                     = { affect_curse_morale, NULL },
    [AFFECT_STICKS_TO_SNAKES]           = { affect_sticks_to_snakes, NULL },
    [AFFECT_DISPEL_EVIL]                = { affect_dispel_evil, NULL },
    [AFFECT_DETECT_MAGIC]               = { affect_empty, NULL },
    [AFFECT_06]                         = { affect_bonus_vs_monsters, NULL },
    [AFFECT_FAERIE_FIRE]                = { affect_faerie_fire, NULL },
    [AFFECT_PROTECTION_FROM_EVIL]       = { affect_protect_evil, NULL },
    [AFFECT_PROTECTION_FROM_GOOD]       = { affect_protect_good, NULL },
    [AFFECT_RESIST_COLD]                = { affect_resist_cold, NULL },
    [AFFECT_CHARM_PERSON]               = { affect_charm_person, NULL },
    [AFFECT_ENLARGE]                    = { affect_empty, NULL },
    [AFFECT_REDUCE]                     = { affect_suffocates, NULL },
    [AFFECT_FRIENDS]                    = { affect_empty, NULL },
    [AFFECT_POISON_DAMAGE]              = { affect_poison_damage, NULL },
    [AFFECT_READ_MAGIC]                 = { affect_empty, NULL },
    [AFFECT_SHIELD]                     = { affect_shield, NULL },
    [AFFECT_GNOME_VS_MAN_SIZED_GIANT]   = { affect_gnome_vs_giant, NULL },
    [AFFECT_FIND_TRAPS]                 = { affect_empty, NULL },
    [AFFECT_RESIST_FIRE]                = { affect_resist_fire, NULL },
    [AFFECT_SILENCE_15_RADIUS]          = { affect_silenced, NULL },
    [AFFECT_SLOW_POISON]                = { affect_slow_poison, NULL },
    [AFFECT_SPIRITUAL_HAMMER]           = { affect_spiritual_hammer, NULL },
    [AFFECT_DETECT_INVISIBILITY]        = { affect_empty, NULL },
    [AFFECT_INVISIBILITY]               = { affect_invisibility, NULL },
    [AFFECT_DWARF_VS_ORC]               = { affect_dwarf_vs_orc, NULL },
    [AFFECT_FUMBLING]                   = { affect_clear_actions, NULL },
    [AFFECT_MIRROR_IMAGE]               = { affect_mirror_image, NULL },
    [AFFECT_RAY_OF_ENFEEBLEMENT]        = { affect_three_quarters_damage, NULL },
    [AFFECT_STINKING_CLOUD]             = { affect_stinking_cloud, NULL },
    [AFFECT_HELPLESS]                   = { affect_clear_actions, NULL },
    [AFFECT_ANIMATE_DEAD]               = { affect_animate_dead, NULL },
    [AFFECT_BLINDED]                    = { affect_blinded, NULL },
    [AFFECT_CAUSE_DISEASE_1]            = { affect_cause_disease, NULL },
    [AFFECT_CONFUSE]                    = { affect_confuse, NULL },
    [AFFECT_BESTOW_CURSE]               = { affect_bestow_curse, NULL },
    [AFFECT_BLINK]                      = { affect_blink, NULL },
    [AFFECT_STRENGTH]                   = { affect_empty, NULL },
    [AFFECT_HASTE]                      = { affect_haste, NULL },
    [AFFECT_IN_STINKING_CLOUD]          = { affect_in_stinking_cloud, NULL },
    [AFFECT_PROT_FROM_NORMAL_MISSILES]  = { affect_prot_normal_missiles, NULL },
    [AFFECT_SLOW]                       = { affect_slow, NULL },
    [AFFECT_WEAKEN]                     = { affect_weaken, NULL },
    [AFFECT_CAUSE_DISEASE_2]            = { affect_cause_disease_2, NULL },
    [AFFECT_PROT_FROM_EVIL_10_RADIUS]   = { affect_protect_evil, NULL },
    [AFFECT_PROT_FROM_GOOD_10_RADIUS]   = { affect_protect_good, NULL },
    [AFFECT_DWARF_AND_GNOME_VS_GIANTS]  = { affect_dwarf_gnome_vs_giants, NULL },
    [AFFECT_30]                         = { affect_vs_type_1, NULL },
    [AFFECT_PRAYER]                     = { affect_prayer, NULL },
    [AFFECT_HOT_FIRE_SHIELD]            = { affect_hot_fire_shield, NULL },
    [AFFECT_SNAKE_CHARM]                = { affect_clear_actions, NULL },
    [AFFECT_PARALYZE]                   = { affect_clear_actions, NULL },
    [AFFECT_SLEEP]                      = { affect_clear_actions, NULL },
    [AFFECT_COLD_FIRE_SHIELD]           = { affect_cold_fire_shield, NULL },
    [AFFECT_POISONED]                   = { affect_empty, NULL },
    [AFFECT_ITEM_INVISIBILITY]          = { affect_item_invisibility, NULL },
    [AFFECT_39]                         = { attack_affect_engulfs, NULL },
    [AFFECT_CLEAR_MOVEMENT]             = { affect_clear_movement, NULL },
    [AFFECT_REGENERATE]                 = { affect_regenerate, NULL },
    [AFFECT_RESIST_NORMAL_WEAPONS]      = { affect_resist_weapons, NULL },
    [AFFECT_FIRE_RESIST]                = { affect_fire_resist, NULL },
    [AFFECT_HIGH_CON_REGEN]             = { affect_high_con_regen, NULL },
    [AFFECT_MINOR_GLOBE_OF_INVULN]      = { affect_minor_globe, NULL },
    [AFFECT_POISON_PLUS_0]              = { affect_poison_plus_0, NULL },
    [AFFECT_POISON_PLUS_4]              = { affect_poison_plus_4, NULL },
    [AFFECT_POISON_PLUS_2]              = { affect_poison_plus_2, NULL },
    [AFFECT_THRI_KREEN_PARALYZE]        = { affect_thri_kreen_paralyze, NULL },
    [AFFECT_FEEBLEMIND]                 = { affect_feeblemind, NULL },
    [AFFECT_INVISIBLE_TO_ANIMALS]       = { affect_invisible_to_animals, NULL },
    [AFFECT_POISON_NEG_2]               = { affect_poison_neg_2, NULL },
    [AFFECT_INVISIBLE]                  = { affect_invisible, NULL },
    [AFFECT_CAMOUFLAGE]                 = { affect_camouflage, NULL },
    [AFFECT_PROT_DRAG_BREATH]           = { affect_prot_dragon_breath, NULL },
    [AFFECT_4A]                         = { affect_empty, NULL },
    [AFFECT_WEAP_DRAGON_SLAYER]         = { affect_dragon_slayer, NULL },
    [AFFECT_WEAP_FROST_BRAND]           = { affect_frost_brand, NULL },
    [AFFECT_BERSERK]                    = { affect_berserk, NULL },
    [AFFECT_4E]                         = { affect_4e_handler, NULL },
    [AFFECT_FIRE_ATTACK_2D10]           = { affect_fire_attack_2d10, NULL },
    [AFFECT_ANKHEG_ACID_ATTACK]         = { affect_ankheg_acid_attack, NULL },
    [AFFECT_HALF_DAMAGE]                = { affect_half_damage, NULL },
    [AFFECT_RESIST_FIRE_AND_COLD]       = { affect_resist_fire_and_cold, NULL },
    [AFFECT_PARALIZING_GAZE]            = { spelleffect_affect_paralizing_gaze,
                                            NULL },
    [AFFECT_SHAMBLING_ABSORB_LIGHTNING] = { affect_shambler_absorb_lightning,
                                            NULL },
    [AFFECT_55]                         = { affect_55_handler, NULL },
    [AFFECT_SPIT_ACID]                  = { spelleffect_affect_spit_acid, NULL },
    [AFFECT_57]                         = { attack_affect_attack_or_kill, NULL },
    [AFFECT_BREATH_ELEC]                = { spelleffect_dragon_breath_elec, NULL },
    [AFFECT_DISPLACE]                   = { affect_displace, NULL },
    [AFFECT_BREATH_ACID]                = { spelleffect_dragon_breath_acid, NULL },
    [AFFECT_IN_CLOUD_KILL]              = { affect_in_cloud_kill, NULL },
    [AFFECT_5C]                         = { affect_empty, NULL },
    [AFFECT_5D]                         = { affect_half_fire_damage, NULL },
    [AFFECT_5E]                         = { affect_5e_handler, NULL },
    [AFFECT_5F]                         = { affect_5f_handler, NULL },
    [AFFECT_OWLBEAR_HUG_CHECK]          = { attack_affect_owlbear_hug_check,
                                            NULL },
    [AFFECT_CON_SAVING_BONUS]           = { affect_con_saving_bonus, NULL },
    [AFFECT_REGEN_3_HP]                 = { affect_regen_3_hp, NULL },
    [AFFECT_63]                         = { affect_63_handler, NULL },
    [AFFECT_TROLL_FIRE_OR_ACID]         = { affect_troll_fire_or_acid, NULL },
    [AFFECT_TROLL_REGEN]                = { affect_troll_regenerate, NULL },
    [AFFECT_TROLL_REGEN_2]              = { affect_troll_regen, NULL },
    [AFFECT_SALAMANDER_HEAT_DAMAGE]     = { affect_salamander_heat_damage, NULL },
    [AFFECT_THRI_KREEN_DODGE_MISSILE]   = { affect_thri_kreen_dodge, NULL },
    [AFFECT_RESIST_MAGIC_50_PERCENT]    = { affect_resist_magic_50, NULL },
    [AFFECT_RESIST_MAGIC_15_PERCENT]    = { affect_resist_magic_15, NULL },
    [AFFECT_ELF_RESIST_SLEEP]           = { affect_elf_resist_sleep, NULL },
    [AFFECT_PROTECT_CHARM_SLEEP]        = { affect_prot_charm_sleep, NULL },
    [AFFECT_RESIST_PARALYZE]            = { affect_resist_paralyze, NULL },
    [AFFECT_IMMUNE_TO_COLD]             = { affect_immune_to_cold, NULL },
    [AFFECT_6F]                         = { affect_6f_handler, NULL },
    [AFFECT_IMMUNE_TO_FIRE]             = { affect_immune_to_fire, NULL },
    [AFFECT_71]                         = { affect_71_handler, NULL },
    [AFFECT_72]                         = { affect_prot_from_electricity, NULL },
    [AFFECT_73]                         = { affect_73_handler, NULL },
    [AFFECT_74]                         = { affect_half_damage_if_weapon_magic,
                                            NULL },
    [AFFECT_75]                         = { affect_75_handler, NULL },
    [AFFECT_76]                         = { affect_prot_cold, NULL },
    [AFFECT_77]                         = { affect_prot_non_magic_weapons, NULL },
    [AFFECT_78]                         = { affect_78_handler, NULL },
    [AFFECT_79]                         = { affect_79_handler, NULL },
    [AFFECT_DRACOLICH_PARALYSIS]        = { affect_dracolich_paralysis, NULL },
    [AFFECT_7B]                         = { affect_7b_handler, NULL },
    [AFFECT_HALFELF_RESISTANCE]         = { affect_half_elf_resistance, NULL },
    [AFFECT_7D]                         = { affect_7d_handler, NULL },
    [AFFECT_7E]                         = { spelleffect_cast_gaze_paralyze, NULL },
    [AFFECT_7F]                         = { affect_empty, NULL },
    [AFFECT_80]                         = { spelleffect_dragon_breath_fire, NULL },
    [AFFECT_PROTECT_MAGIC]              = { affect_prot_magic, NULL },
    [AFFECT_82]                         = { affect_82_handler, NULL },
    [AFFECT_CAST_BREATH_FIRE]           = { spelleffect_cast_breath_fire, NULL },
    [AFFECT_CAST_THROW_LIGHTENING]      = { spelleffect_cast_throw_lightening,
                                            NULL },
    [AFFECT_85]                         = { affect_85_handler, NULL },
    [AFFECT_RANGER_VS_GIANT]            = { affect_ranger_vs_giant, NULL },
    [AFFECT_PROTECT_ELEC]               = { affect_prot_elec, NULL },
    [AFFECT_ENTANGLE]                   = { affect_entangle, NULL },
    [AFFECT_89]                         = { affect_89_handler, NULL },
    [AFFECT_8A]                         = { affect_8a_handler, NULL },
    [AFFECT_8B]                         = { attack_affect_engulf_round, NULL },
    [AFFECT_PALADIN_DAILY_HEAL_CAST]    = { affect_empty, NULL },
    [AFFECT_PALADIN_DAILY_CURE_REFRESH] = { affect_paladin_cure_refresh, NULL },
    [AFFECT_FEAR]                       = { affect_fear, NULL },
    [AFFECT_8F]                         = { affect_8f_handler, NULL },
    [AFFECT_OWLBEAR_HUG_ROUND_ATTACK]   = { attack_affect_owlbear_hug_round,
                                            NULL },
    [AFFECT_SP_DISPEL_EVIL]             = { affect_sp_dispel_evil, NULL },
    [AFFECT_STRENGTH_SPELL]             = { affect_empty, NULL },
    [AFFECT_DO_ITEMS_AFFECT]            = { affect_do_items_affect, NULL }
};

/* One bit per affect id, so an unported handler is named once rather than every
 * round of every fight. */
static u8 g_reported[(AFFECT_TABLE_SIZE / 8) + 1];

static void report_pending(unsigned id, const char *owner)
{
    if ((g_reported[id / 8] & (u8)(1u << (id % 8))) != 0) {
        return;
    }

    g_reported[id / 8] |= (u8)(1u << (id % 8));

    log_warn("affect table: affect 0x%02x does nothing yet - its handler is %s",
             id, owner);
}

void affect_table_call(Effect add_remove, void *parameter, Player *player,
                       Affects affect)
{
    const AffectEntry *entry;
    unsigned id;

    if (gbl.apply_item_affect) {
        affect = AFFECT_DO_ITEMS_AFFECT;
    }

    id = (unsigned)affect;

    if (id >= AFFECT_TABLE_SIZE) {
        log_warn("affect table: affect 0x%x is not one of the 0x93", id);
        return;
    }

    if (player == NULL) {
        log_warn("affect table: affect 0x%02x has nobody to act on", id);
        return;
    }

    entry = &g_affect_table[id];

    if (entry->fn != NULL) {
        entry->fn(add_remove, parameter, player);
    } else if (entry->pending != NULL) {
        report_pending(id, entry->pending);
    }
}
