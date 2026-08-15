/* effect.c - Ported from engine/ovr024.cs. */
#include <stdio.h>
#include <string.h>

#include "effect.h"

#include "affecttab.h"
#include "character.h"
#include "cheats.h"
#include "combat.h"
#include "combatmap.h"
#include "gbl.h"
#include "log.h"
#include "point.h"
#include "rnd.h"
#include "spelllist.h"
#include "target.h"
#include "text.h"

/* ------------------------------------------------------------------- dice */

u8 effect_roll_dice(int dice_size, int dice_count)
{
    int roll_total = 0;

    for (int i = 0; i < dice_count; i++) {
        roll_total += rnd_int(dice_size) + 1;
    }

    /* The original returned this in AL, so a total over 255 comes back wrapped;
     * nothing in the game rolls that high. */
    return (u8)roll_total;
}

int effect_roll_dice_save(int dice_size, int dice_count)
{
    gbl.dice_count = dice_count;

    return effect_roll_dice(dice_size, dice_count);
}

/* ---------------------------------------------------------------- affects */

void effect_add_affect(bool call_affect_table, int data, u16 minutes,
                       Affects type, Player *player)
{
    Affect affect;

    affect_init(&affect, type, minutes, (u8)data, call_affect_table);

    affect_list_add(&player->affects, &affect);
}

void effect_remove_affect(Affect *affect, Affects affect_id, Player *player)
{
    if (affect == NULL) {
        affect = affect_list_find(&player->affects, affect_id);
    }

    if (affect != NULL) {
        if (affect->call_affect_table) {
            affect_table_call(EFFECT_REMOVE, affect, player, affect_id);
        }

        affect_list_remove(&player->affects, affect);

        /* The entry has gone and the list has closed up over it, so the caller's
         * pointer - and ours - now names somebody else's affect. */
        affect = NULL;

        /* Resist fire is stored where a charisma change would be - the friends
         * spell shares the field - so both of these recalculate a stat that
         * looks nothing like the affect that changed. */
        if (affect_id == AFFECT_RESIST_FIRE) {
            effect_calc_stat_bonuses(STAT_CHA, player);
        }

        if (affect_id == AFFECT_ENLARGE ||
            affect_id == AFFECT_STRENGTH ||
            affect_id == AFFECT_STRENGTH_SPELL) {
            effect_calc_stat_bonuses(STAT_STR, player);
        }
    }
}

/* unk_6325A. The affects that are cast over an area, so that a team member
 * carrying one covers everybody standing close enough to them. */
static const u8 AREA_AFFECTS[] = {
    AFFECT_SILENCE_15_RADIUS,
    AFFECT_PROT_FROM_EVIL_10_RADIUS,
    AFFECT_PROT_FROM_GOOD_10_RADIUS,
    AFFECT_PRAYER
};

static bool is_area_affect(Affects affect_type)
{
    for (size_t i = 0; i < COAB_ARRAY_LEN(AREA_AFFECTS); i++) {
        if (AREA_AFFECTS[i] == (u8)affect_type) {
            return true;
        }
    }

    return false;
}

/* The Predicate<Player> ovr024 handed to Rebuild_SortedCombatantList: is this
 * the character we are asking about? */
static bool filter_is_player(const Player *p, void *ctx)
{
    return p == (const Player *)ctx;
}

void effect_calc_affect(Affects affect_type, Player *player)
{
    bool found = false;
    Affect *affect = affect_list_find(&player->affects, affect_type);

    if (affect != NULL) {
        found = true;
    } else if (is_area_affect(affect_type)) {
        for (int i = 0; i < gbl.team_count && !found; i++) {
            Player *team_member = gbl.team_list[i];

            affect = affect_list_find(&team_member->affects, affect_type);

            if (affect != NULL) {
                if (gbl.game_state == GAME_STATE_COMBAT) {
                    /* Prayer carries six squares; the rest one. */
                    int max_range = (affect_type == AFFECT_PRAYER) ? 6 : 1;
                    SortedCombatant scl[GBL_MAX_COMBATANT_COUNT];

                    found = target_sorted_combatants_for(
                                scl, (int)COAB_ARRAY_LEN(scl), team_member,
                                max_range, filter_is_player, player) > 0;
                } else {
                    found = true;
                }
            }
        }
    }

    if (found) {
        /* The affect passed on is whoever's copy was found, which for an area
         * affect is the team member's and not the character's. */
        affect_table_call(EFFECT_ADD, affect, player, affect_type);
    }
}

/* work_on_00's cases. Each list is asked in order, because an affect that
 * changes a roll sees whatever the one before it left behind. */

static const u8 CHECK_VISIBILITY[] = {
    AFFECT_BLINK, AFFECT_INVISIBILITY, AFFECT_INVISIBLE,
    AFFECT_INVISIBLE_TO_ANIMALS
};

static const u8 CHECK_2[] = {
    AFFECT_FIRE_ATTACK_2D10, AFFECT_ANKHEG_ACID_ATTACK, AFFECT_SP_DISPEL_EVIL,
    AFFECT_39, AFFECT_OWLBEAR_HUG_CHECK, AFFECT_DRACOLICH_PARALYSIS, AFFECT_7B
};

static const u8 CHECK_3[] = {
    AFFECT_POISON_PLUS_0, AFFECT_POISON_PLUS_4, AFFECT_POISON_PLUS_2,
    AFFECT_THRI_KREEN_PARALYZE, AFFECT_POISON_NEG_2, AFFECT_FIRE_ATTACK_2D10,
    AFFECT_57
};

static const u8 CHECK_SPECIAL_ATTACKS[] = {
    AFFECT_RAY_OF_ENFEEBLEMENT, AFFECT_06, AFFECT_SALAMANDER_HEAT_DAMAGE,
    AFFECT_WEAP_DRAGON_SLAYER, AFFECT_WEAP_FROST_BRAND, AFFECT_RANGER_VS_GIANT
};

static const u8 CHECK_5[] = {
    AFFECT_MIRROR_IMAGE, AFFECT_PROT_FROM_NORMAL_MISSILES,
    AFFECT_THRI_KREEN_DODGE_MISSILE, AFFECT_78, AFFECT_TROLL_REGEN, AFFECT_73,
    AFFECT_74, AFFECT_77, AFFECT_5E, AFFECT_75, AFFECT_RESIST_NORMAL_WEAPONS,
    AFFECT_HALF_DAMAGE, AFFECT_RESIST_FIRE_AND_COLD, AFFECT_55, AFFECT_82,
    AFFECT_8F
};

static const u8 CHECK_PRE_DAMAGE[] = {
    AFFECT_71, AFFECT_FIRE_RESIST, AFFECT_RESIST_COLD, AFFECT_RESIST_FIRE,
    AFFECT_RESIST_MAGIC_50_PERCENT, AFFECT_RESIST_MAGIC_15_PERCENT,
    AFFECT_IMMUNE_TO_FIRE, AFFECT_72, AFFECT_76, AFFECT_SHIELD, AFFECT_5D,
    AFFECT_TROLL_REGEN, AFFECT_MIRROR_IMAGE, AFFECT_IMMUNE_TO_COLD,
    AFFECT_PROT_DRAG_BREATH, AFFECT_RESIST_FIRE_AND_COLD,
    AFFECT_SHAMBLING_ABSORB_LIGHTNING, AFFECT_PROTECT_MAGIC, AFFECT_85,
    AFFECT_PROTECT_ELEC, AFFECT_MINOR_GLOBE_OF_INVULN
};

static const u8 CHECK_PLAYER_RESTRAINED[] = {
    AFFECT_SNAKE_CHARM, AFFECT_PARALYZE, AFFECT_SLEEP, AFFECT_HELPLESS,
    AFFECT_STICKS_TO_SNAKES, AFFECT_FUMBLING, AFFECT_ENTANGLE
};

static const u8 CHECK_8[] = {
    AFFECT_63, AFFECT_RESIST_FIRE_AND_COLD, AFFECT_DISPLACE, AFFECT_CAMOUFLAGE,
    AFFECT_ITEM_INVISIBILITY
};

static const u8 CHECK_MAGIC_RESISTANCE[] = {
    AFFECT_RESIST_MAGIC_50_PERCENT, AFFECT_RESIST_MAGIC_15_PERCENT,
    AFFECT_ELF_RESIST_SLEEP, AFFECT_PROTECT_CHARM_SLEEP, AFFECT_RESIST_PARALYZE,
    AFFECT_IMMUNE_TO_COLD, AFFECT_6F, AFFECT_IMMUNE_TO_FIRE,
    AFFECT_HALFELF_RESISTANCE, AFFECT_7D, AFFECT_MINOR_GLOBE_OF_INVULN,
    AFFECT_PROTECT_MAGIC
};

static const u8 CHECK_10[] = {
    AFFECT_BLESS, AFFECT_CURSED, AFFECT_BLINDED, AFFECT_BESTOW_CURSE,
    AFFECT_PRAYER, AFFECT_06, AFFECT_GNOME_VS_MAN_SIZED_GIANT,
    AFFECT_DWARF_VS_ORC, AFFECT_WEAP_DRAGON_SLAYER, AFFECT_WEAP_FROST_BRAND
};

static const u8 CHECK_11[] = {
    AFFECT_BLINDED, AFFECT_SHIELD, AFFECT_PROTECTION_FROM_EVIL,
    AFFECT_PROTECTION_FROM_GOOD, AFFECT_PROT_FROM_EVIL_10_RADIUS,
    AFFECT_PROT_FROM_GOOD_10_RADIUS, AFFECT_STINKING_CLOUD, AFFECT_FAERIE_FIRE
};

static const u8 CHECK_SAVING_THROW[] = {
    AFFECT_PROTECTION_FROM_EVIL, AFFECT_PROTECTION_FROM_GOOD,
    AFFECT_RESIST_COLD, AFFECT_SHIELD, AFFECT_RESIST_FIRE, AFFECT_BLINDED,
    AFFECT_BESTOW_CURSE, AFFECT_PROT_FROM_EVIL_10_RADIUS,
    AFFECT_PROT_FROM_GOOD_10_RADIUS, AFFECT_PRAYER, AFFECT_FIRE_RESIST,
    AFFECT_6F, AFFECT_7D, AFFECT_CON_SAVING_BONUS, AFFECT_HOT_FIRE_SHIELD,
    AFFECT_COLD_FIRE_SHIELD
};

static const u8 CHECK_DEATH[] = {
    AFFECT_63, AFFECT_TROLL_FIRE_OR_ACID, AFFECT_WEAP_DRAGON_SLAYER
};

static const u8 CHECK_14[] = {
    AFFECT_PARALIZING_GAZE, AFFECT_BREATH_ELEC, AFFECT_79, AFFECT_SPIT_ACID,
    AFFECT_57, AFFECT_BREATH_ACID, AFFECT_7E, AFFECT_80,
    AFFECT_CAST_BREATH_FIRE, AFFECT_CAST_THROW_LIGHTENING, AFFECT_8B
};

static const u8 CHECK_15[] = {
    AFFECT_SILENCE_15_RADIUS, AFFECT_STINKING_CLOUD, AFFECT_CHARM_PERSON,
    AFFECT_REDUCE, AFFECT_BERSERK
};

static const u8 CHECK_16[] = {
    AFFECT_INVISIBILITY, AFFECT_INVISIBLE, AFFECT_BLINK,
    AFFECT_DWARF_AND_GNOME_VS_GIANTS, AFFECT_30, AFFECT_DISPLACE,
    AFFECT_DISPEL_EVIL
};

static const u8 CHECK_MORALE[] = {
    AFFECT_BLESS, AFFECT_CURSED, AFFECT_CHARM_PERSON
};

static const u8 CHECK_MOVEMENT[] = {
    AFFECT_HASTE, AFFECT_SLOW, AFFECT_CLEAR_MOVEMENT
};

static const u8 CHECK_19[] = {
    AFFECT_REGEN_3_HP, AFFECT_SPIRITUAL_HAMMER, AFFECT_CAMOUFLAGE,
    AFFECT_ITEM_INVISIBILITY, AFFECT_CHARM_PERSON
};

static const u8 CHECK_FIRE_SHIELD[] = {
    AFFECT_HOT_FIRE_SHIELD, AFFECT_COLD_FIRE_SHIELD
};

static const u8 CHECK_CONFUSION[] = { AFFECT_CONFUSE };
static const u8 CHECK_22[] = { AFFECT_8A };
static const u8 CHECK_23[] = { AFFECT_4A };

static void check_list(const u8 *affects, size_t count, Player *player)
{
    for (size_t i = 0; i < count; i++) {
        effect_calc_affect((Affects)affects[i], player);
    }
}

#define CHECK_LIST(list, player) \
    check_list((list), COAB_ARRAY_LEN(list), (player))

void effect_check_affects(Player *player, CheckType type)
{
    switch (type) {
    case CHECK_TYPE_NONE:
        break;

    case CHECK_TYPE_VISIBILITY:
        CHECK_LIST(CHECK_VISIBILITY, player);
        break;

    case CHECK_TYPE_2:
        CHECK_LIST(CHECK_2, player);
        break;

    case CHECK_TYPE_3:
        CHECK_LIST(CHECK_3, player);
        break;

    case CHECK_TYPE_SPECIAL_ATTACKS:
        CHECK_LIST(CHECK_SPECIAL_ATTACKS, player);
        break;

    case CHECK_TYPE_5:
        CHECK_LIST(CHECK_5, player);
        break;

    case CHECK_TYPE_PRE_DAMAGE:
        CHECK_LIST(CHECK_PRE_DAMAGE, player);
        break;

    case CHECK_TYPE_PLAYER_RESTRAINED:
        CHECK_LIST(CHECK_PLAYER_RESTRAINED, player);
        break;

    case CHECK_TYPE_8:
        CHECK_LIST(CHECK_8, player);
        break;

    case CHECK_TYPE_MAGIC_RESISTANCE:
        CHECK_LIST(CHECK_MAGIC_RESISTANCE, player);
        break;

    case CHECK_TYPE_10:
        CHECK_LIST(CHECK_10, player);
        break;

    case CHECK_TYPE_11:
        CHECK_LIST(CHECK_11, player);
        break;

    case CHECK_TYPE_SAVING_THROW:
        CHECK_LIST(CHECK_SAVING_THROW, player);
        break;

    case CHECK_TYPE_DEATH:
        CHECK_LIST(CHECK_DEATH, player);
        break;

    case CHECK_TYPE_14:
        CHECK_LIST(CHECK_14, player);
        break;

    case CHECK_TYPE_15:
        CHECK_LIST(CHECK_15, player);
        break;

    case CHECK_TYPE_16:
        CHECK_LIST(CHECK_16, player);
        break;

    case CHECK_TYPE_MORALE:
        CHECK_LIST(CHECK_MORALE, player);
        break;

    case CHECK_TYPE_MOVEMENT:
        CHECK_LIST(CHECK_MOVEMENT, player);
        break;

    case CHECK_TYPE_19:
        CHECK_LIST(CHECK_19, player);
        break;

    case CHECK_TYPE_FIRE_SHIELD:
        CHECK_LIST(CHECK_FIRE_SHIELD, player);
        break;

    case CHECK_TYPE_CONFUSION:
        CHECK_LIST(CHECK_CONFUSION, player);
        break;

    case CHECK_TYPE_22:
        CHECK_LIST(CHECK_22, player);
        break;

    case CHECK_TYPE_23:
        CHECK_LIST(CHECK_23, player);
        break;

    default:
        /* The C# switch simply fell through. */
        log_warn("effect: no affect check of type %d", (int)type);
        break;
    }
}

bool effect_cure_affect(Affects affect_id, Player *player)
{
    Affect *affect = affect_list_find(&player->affects, affect_id);

    if (affect != NULL) {
        character_display_status_string(true, 10, "is Cured", player);

        effect_remove_affect(affect, affect_id, player);

        return true;
    }

    return false;
}

void effect_remove_invisibility(Player *player)
{
    Affect *affect;

    while ((affect = affect_list_find(&player->affects,
                                      AFFECT_INVISIBILITY)) != NULL) {
        effect_remove_affect(affect, AFFECT_INVISIBILITY, player);
    }
}

/* sub_645AB */
void effect_remove_combat_affects(Player *player)
{
    static const u8 table[] = {
        AFFECT_FAERIE_FIRE,
        AFFECT_CHARM_PERSON,
        AFFECT_REDUCE,
        AFFECT_SILENCE_15_RADIUS,
        AFFECT_SPIRITUAL_HAMMER,
        AFFECT_STINKING_CLOUD,
        AFFECT_HELPLESS,
        AFFECT_ANIMATE_DEAD,
        AFFECT_SNAKE_CHARM,
        AFFECT_PARALYZE,
        AFFECT_SLEEP,
        AFFECT_CLEAR_MOVEMENT,
        AFFECT_REGENERATE,
        AFFECT_5F,
        AFFECT_REGEN_3_HP,
        AFFECT_ENTANGLE,
        AFFECT_89,
        AFFECT_8B,
        AFFECT_OWLBEAR_HUG_ROUND_ATTACK
    };

    for (size_t i = 0; i < COAB_ARRAY_LEN(table); i++) {
        effect_remove_affect(NULL, (Affects)table[i], player);
    }

    /* A character who went berserk fought for nobody; with the fight over they
     * are ours again. */
    if (player_has_affect(player, AFFECT_BERSERK) &&
        player->control_morale == CONTROL_PC_BERZERK) {
        player->combat_team = TEAM_OURS;
    }
}

/* sub_6460D */
void effect_remove_attackers_affects(Player *player)
{
    static const u8 table[] = {
        AFFECT_REDUCE,
        AFFECT_CLEAR_MOVEMENT,
        AFFECT_8B,
        AFFECT_OWLBEAR_HUG_ROUND_ATTACK
    };

    for (size_t i = 0; i < COAB_ARRAY_LEN(table); i++) {
        effect_remove_affect(NULL, (Affects)table[i], player);
    }
}

/* is_unaffected */
void effect_apply_attack_spell_affect(const char *text, bool saved,
                                      DamageOnSave can_save,
                                      bool call_affect_table, int data,
                                      u16 time, Affects affect_id,
                                      Player *target)
{
    gbl.current_affect = affect_id;

    /* A resistance handler that shrugs the spell off clears gbl.current_affect,
     * which is how it says so. */
    effect_check_affects(target, CHECK_TYPE_MAGIC_RESISTANCE);

    if (gbl.current_affect == AFFECT_NONE ||
        (saved && can_save == DAMAGE_ON_SAVE_ZERO)) {
        character_display_status_string(true, 10, "is Unaffected", target);
    } else {
        Affect *found_affect = affect_list_find(&target->affects, affect_id);

        /* One that has run out of time is left alone, so that its own handler
         * gets to expire it. */
        if (found_affect != NULL && found_affect->minutes > 0) {
            effect_remove_affect(found_affect, affect_id, target);
        }

        effect_add_affect(call_affect_table, data, time, affect_id, target);

        if (text[0] != '\0') {
            character_magic_attack_display(text, true, target);
            character_clear_text_area();
        }
    }
}

/* --------------------------------------------------------- attack and save */

/* sub_641DD */
bool effect_can_hit_target(int bonus, Player *target)
{
    bool hit = false;

    gbl.attack_roll = effect_roll_dice(20, 1);

    if (gbl.attack_roll > 1) {
        /* A natural 20 always hits, so it is made big enough to beat any armour
         * class rather than compared specially. */
        if (gbl.attack_roll == 20) {
            gbl.attack_roll = 100;
        }

        effect_check_affects(target, CHECK_TYPE_16);

        /* Blink and displacement can take the roll negative, which misses. */
        if (gbl.attack_roll >= 0) {
            if ((gbl.attack_roll + bonus) > target->ac) {
                hit = true;
            }
        }
    }

    return hit;
}

/* sub_64245 */
bool effect_pc_can_hit_target(int target_ac, Player *target, Player *attacker)
{
    bool hit = false;

    effect_remove_invisibility(attacker);
    gbl.attack_roll = effect_roll_dice(20, 1);

    if (gbl.attack_roll > 1) {
        if (gbl.attack_roll == 20) {
            gbl.attack_roll = 100;
        }

        effect_check_affects(attacker, CHECK_TYPE_10);
        effect_check_affects(target, CHECK_TYPE_16);

        int team_bonus;

        if (attacker->combat_team == TEAM_OURS) {
            team_bonus = gbl.area2_ptr->field_6E2;
        } else {
            team_bonus = gbl.area2_ptr->field_6E0;
        }

        if (gbl.attack_roll >= 0) {
            /* The monster form above needs to beat the armour class; this one
             * only needs to match it. */
            if ((gbl.attack_roll + attacker->hit_bonus + team_bonus) >=
                target_ac) {
                hit = true;
            }
        }
    }

    return hit;
}

/* do_saving_throw */
bool effect_roll_saving_throw(int save_bonus, SaveVerseType save_type,
                              Player *player)
{
    gbl.saving_throw_made = true;
    gbl.saving_throw_roll = effect_roll_dice(20, 1);

    if (cheats.player_always_saves && player->combat_team == TEAM_OURS) {
        gbl.saving_throw_roll = 20;
    }

    if (gbl.saving_throw_roll == 1) {
        gbl.saving_throw_made = false;
    } else if (gbl.saving_throw_roll == 20) {
        gbl.saving_throw_made = true;
    } else {
        gbl.saving_throw_roll += save_bonus + player->field_186;
        gbl.save_verse_type = save_type;

        effect_check_affects(player, CHECK_TYPE_SAVING_THROW);

        gbl.saving_throw_made =
            gbl.saving_throw_roll >= player->save_verse[save_type];
    }

    return gbl.saving_throw_made;
}

/* --------------------------------------------------------- damage and death */

/* sub_63014 */
void effect_kill_player(const char *text, Status new_health_status,
                        Player *player)
{
    character_display_status_string(false, 10, text, player);

    if (player->health_status != STATUS_STONED &&
        player->health_status != STATUS_DEAD &&
        player->health_status != STATUS_GONE) {
        player->health_status = new_health_status;
        player->in_combat = false;
        player->hit_point_current = 0;

        effect_remove_combat_affects(player);
        effect_check_affects(player, CHECK_TYPE_DEATH);

        /* A troll that regenerates, or anything else the death handlers put back
         * on its feet, is in the fight again and keeps its body. */
        if (player->in_combat == false) {
            combatmap_combatant_killed(player);
        }

        text_game_delay();
        character_clear_text_area();

        if (gbl.game_state != GAME_STATE_COMBAT) {
            character_party_summary(gbl.selected_player);
        }
    }
}

/* sub_644A7 */
void effect_remove_from_combat(const char *msg, Status health_status,
                               Player *player)
{
    if (player->in_combat) {
        int player_index = combatmap_player_index(player);

        combatmap_redraw_if_focus_on(false, 3, player);

        character_display_status_string(true, 10, msg, player);

        player->in_combat = false;

        player->health_status = health_status;

        /* Someone who has run away keeps their hit points; everybody else who
         * leaves the fight this way is down. */
        if (player->health_status != STATUS_RUNNING) {
            player->hit_point_current = 0;
        }

        combatmap_redraw_player_background(player_index);
        /* seg040.DrawOverlay() went here; it does nothing. */

        gbl.combat_map[player_index].size = 0;

        combatmap_setup_player_index();

        character_clear_actions(player);
        effect_remove_combat_affects(player);
    }
}

void effect_try_loose_spell(Player *player)
{
    Action *actions = player_actions(player);

    actions->can_cast = false;

    if (actions->spell_id > 0) {
        character_display_status_string(true, 12, "lost a spell", player);

        spell_list_clear_spell(&player->spell_list, actions->spell_id);
        actions->spell_id = 0;
    }
}

void effect_damage_person(bool change_damage, DamageOnSave damage_on_save,
                          int damage, Player *player)
{
    char text[80];

    gbl.damage = damage;

    /* The resistances get to change the total before anything else sees it. */
    effect_check_affects(player, CHECK_TYPE_PRE_DAMAGE);

    if (change_damage) {
        if (damage_on_save == DAMAGE_ON_SAVE_ZERO) {
            gbl.damage = 0;
        } else if (damage_on_save == DAMAGE_ON_SAVE_HALF) {
            gbl.damage /= 2;
        }
    } else {
        /* A fire shield answers the blow that got through instead. */
        effect_check_affects(player, CHECK_TYPE_FIRE_SHIELD);
    }

    if (gbl.damage > 0) {
        if (gbl.damage != 1) {
            snprintf(text, sizeof(text), "takes %d points of damage ",
                     gbl.damage);
        } else {
            snprintf(text, sizeof(text), "takes 1 point of damage ");
        }

        /* Bit 3 - magic - is masked out here and tested on its own below. Only one
         * of the two tests can pass, so the original's run of concatenations
         * appends at most one of these. */
        int mask = gbl.damage_flags & 0xf7;
        const char *source = "";

        if (mask == DAMAGE_FIRE) {
            source = "from Fire";
        } else if (mask == DAMAGE_COLD) {
            source = "from Cold";
        } else if (mask == DAMAGE_ELECTRICITY) {
            source = "from Electricity";
        } else if (mask == DAMAGE_ACID) {
            source = "from Acid";
        }

        /* True when magic is the only flag set - and also when none is, which is
         * why a plain sword blow reads "from Magic". The original's test, kept. */
        if ((gbl.damage_flags & DAMAGE_MAGIC) == gbl.damage_flags) {
            source = "from Magic";
        }

        snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s", source);

        character_magic_attack_display(text, false, player);
        character_damage(gbl.damage, player);

        if (gbl.game_state == GAME_STATE_COMBAT) {
            effect_try_loose_spell(player);
        }

        if (player->in_combat == false) {
            const char *down;

            if (player->health_status == STATUS_DEAD ||
                player->health_status == STATUS_STONED ||
                player->health_status == STATUS_GONE) {
                down = "is killed";
            } else if (player->health_status == STATUS_DYING) {
                down = "Goes Down, and is Dying";
            } else {
                down = "Goes Down";
            }

            character_display_status_string(false, gbl.text_y_col + 1, down,
                                            player);

            if (gbl.game_state != GAME_STATE_COMBAT) {
                text_game_delay();
            } else {
                effect_remove_combat_affects(player);

                effect_check_affects(player, CHECK_TYPE_DEATH);

                if (player->in_combat == false) {
                    combatmap_combatant_killed(player);
                } else {
                    text_game_delay();
                }
            }
        }

        character_clear_text_area();
    }
}

bool effect_heal_player(u8 arg_0, int amount_healed, Player *player)
{
    if (player->health_status == STATUS_OKEY ||
        player->health_status == STATUS_ANIMATED ||
        player->health_status == STATUS_UNCONSCIOUS ||
        player->health_status == STATUS_DYING) {
        if (player->hit_point_current < player->hit_point_max ||
            (player->hit_point_current >= player->hit_point_max &&
             arg_0 == 0)) {
            player->hit_point_current = (u8)(amount_healed +
                                             player->hit_point_current);

            if (player->hit_point_current > player->hit_point_max) {
                player->hit_point_current = player->hit_point_max;
            }

            if (player->in_combat == false) {
                if (player->health_status == STATUS_DYING) {
                    player->health_status = STATUS_UNCONSCIOUS;
                }

                if (player->health_status == STATUS_UNCONSCIOUS &&
                    gbl.game_state != GAME_STATE_COMBAT) {
                    affect_table_call(EFFECT_REMOVE, NULL, player, AFFECT_4E);
                }
            }

            return true;
        }
    }

    return false;
}

bool effect_combat_heal(u8 arg_0, Player *player)
{
    if (combatmap_place_combatant(true, combatmap_player_map_pos(player),
                                  player)) {
        player->health_status = STATUS_OKEY;
        player->in_combat = true;
        player->hit_point_current = arg_0;

        if (gbl.game_state == GAME_STATE_COMBAT) {
            combatmap_redraw_if_focus_on(false, 3, player);
        }

        const char *text;

        if (player->combat_team == TEAM_ENEMY) {
            text = "stands up and grins";
        } else {
            text = "gets back up";
        }

        character_display_status_string(true, 10, text, player);

        character_count_combat_teams();

        return true;
    }

    return false;
}

/* ------------------------------------------------------------ gas clouds */

/* gbl.unk_18AEA, seg600:27DA. Which way each of a stinking cloud's four squares
 * lies from the square it was cast at.
 *
 * A cloud's squares are laid out by engine/ovr023.cs using SmallCloudDirections,
 * { 8, 2, 3, 4 } at seg600:27D9 - and this table is that one read a byte late,
 * running off its end into CloudDirections[0]. So square 0 is looked for where
 * square 1 was put, and the fourth square is looked for in the middle where the
 * cloud was centred. That is what the DOS code did, byte for byte, so it stays:
 * all it decides is which caster gets the blame for a cloud, and the answer when
 * no cloud matches is the party leader anyway. */
static const u8 CLOUD_CELL_DIRECTIONS[4] = { 2, 3, 4, 8 };

/* sub_63D03. Whose cloud is standing on map_pos. */
static Player *cloud_caster_at(const u8 *directions, int direction_count,
                              const GasCloud *clouds, int cloud_count,
                              Point map_pos)
{
    for (int c = 0; c < cloud_count; c++) {
        for (int i = 0; i < direction_count; i++) {
            Point cell;

            if (clouds[c].present[i] == false) {
                continue;
            }

            cell = point_add(clouds[c].target_pos,
                             gbl_map_direction_delta(directions[i]));

            if (point_eq(cell, map_pos)) {
                return clouds[c].player;
            }
        }
    }

    /* No cloud of ours: the party leader answers for it. */
    if (gbl.team_count > 0) {
        return gbl.team_list[0];
    }

    log_warn("effect: no team member to blame a gas cloud on");

    return NULL;
}

void effect_in_poison_cloud(u8 arg_0, Player *player)
{
    if (player->in_combat == false) {
        return;
    }

    bool is_poisonous_cloud;
    bool is_noxious_cloud;
    int  dummy_ground_tile;
    int  dummy_player_index;

    combatmap_ground_information_clouds(&is_poisonous_cloud, &is_noxious_cloud,
                                       &dummy_ground_tile, &dummy_player_index,
                                       8, player);

    if (is_noxious_cloud && arg_0 != 0 &&
        affect_list_find(&player->affects, AFFECT_HELPLESS) == NULL &&
        affect_list_find(&player->affects, AFFECT_ANIMATE_DEAD) == NULL &&
        affect_list_find(&player->affects, AFFECT_6F) == NULL &&
        affect_list_find(&player->affects, AFFECT_7D) == NULL) {
        bool save_passed = effect_roll_saving_throw(0, SAVE_VERSE_POISON,
                                                    player);
        /* The cloud's caster is made the selected character for the duration, so
         * that the messages and the affect are credited to them. */
        Player *tmp_player_ptr = gbl.selected_player;

        gbl.selected_player =
            cloud_caster_at(CLOUD_CELL_DIRECTIONS,
                            (int)COAB_ARRAY_LEN(CLOUD_CELL_DIRECTIONS),
                            gbl.stinking_cloud, gbl.stinking_cloud_count,
                            combatmap_player_map_pos(player));

        if (save_passed) {
            effect_apply_attack_spell_affect("starts to cough", save_passed,
                                             DAMAGE_ON_SAVE_NORMAL, false, 0xff,
                                             1, AFFECT_STINKING_CLOUD, player);

            if (player_has_affect(player, AFFECT_STINKING_CLOUD)) {
                /* The affect is passed as NULL: the C# handed on whatever its
                 * last FindAffect left behind, and every one of the four above
                 * failed. The handler looks the affect up for itself. */
                affect_table_call(EFFECT_ADD, NULL, player,
                                  AFFECT_STINKING_CLOUD);
            }
        } else {
            Affect *affect;

            effect_apply_attack_spell_affect(
                "chokes and gags from nausea", save_passed,
                DAMAGE_ON_SAVE_NORMAL, false, 0xff,
                (u16)(effect_roll_dice(4, 1) + 1), AFFECT_HELPLESS, player);

            affect = affect_list_find(&player->affects, AFFECT_HELPLESS);

            if (affect != NULL) {
                affect_table_call(EFFECT_ADD, affect, player, AFFECT_HELPLESS);
            }
        }

        gbl.selected_player = tmp_player_ptr;
    }

    if (is_poisonous_cloud && player->in_combat) {
        /* Cloudkill: up to four hit dice is fatal, five and six get a save, and
         * anything tougher walks through it. The affect added first is what tells
         * the death handlers what killed them - and for the weakest it is the
         * wrong one, minor globe of invulnerability rather than poison, as the
         * original had it. */
        if (player->hit_dice <= 4) {
            character_display_status_string(false, 10, "is Poisoned", player);
            text_game_delay();
            effect_add_affect(false, 0xff, 0, AFFECT_MINOR_GLOBE_OF_INVULN,
                              player);
            effect_kill_player("is killed", STATUS_DEAD, player);
        } else if (player->hit_dice == 5) {
            if (effect_roll_saving_throw(-4, SAVE_VERSE_POISON, player) ==
                false) {
                character_display_status_string(false, 10, "is Poisoned",
                                                player);
                text_game_delay();
                effect_add_affect(false, 0xff, 0, AFFECT_POISONED, player);
                effect_kill_player("is killed", STATUS_DEAD, player);
            }
        } else if (player->hit_dice == 6) {
            if (effect_roll_saving_throw(0, SAVE_VERSE_POISON, player) ==
                false) {
                character_display_status_string(false, 10, "is Poisoned",
                                                player);
                text_game_delay();
                effect_add_affect(false, 0xff, 0, AFFECT_POISONED, player);
                effect_kill_player("is killed", STATUS_DEAD, player);
            }
        }
    }
}

/* ------------------------------------------------------- strength and stats */

/* odd_math */
int effect_encode_strength(int str_00, int str)
{
    int ret_val = str + 100;

    if (str == 18) {
        ret_val = str_00 + 1;
    }

    return ret_val;
}

/* sub_646D9 */
void effect_decode_strength(int *str_00, int *str, const Affect *affect)
{
    *str_00 = 0;
    *str = affect->affect_data & 0x7f;

    if (*str <= 101) {
        *str_00 = *str - 1;
        *str = 18;
    } else {
        *str -= 100;
    }
}

/* sub_64728 */
bool effect_try_encode_strength(int *encoded_str, int str_100, int str,
                               const Player *player)
{
    bool encoded;

    if (str > player->stats.value[PSTAT_STR].cur ||
        (str == 18 && str_100 > player->stats.value[PSTAT_STR00].full)) {
        encoded = true;
        *encoded_str = effect_encode_strength(str_100, str);
    } else {
        encoded = false;
        *encoded_str = 0;
    }

    return encoded;
}

/* sub_64771 */
void effect_max_strength(int *str_a, int str_b, int *str_00_a, int str_00_b)
{
    if (str_b > *str_a ||
        (str_b == 18 && str_00_b > *str_00_a)) {
        *str_a = str_b;
        *str_00_a = str_00_b;
    }
}

/* sub_647BE */
int effect_con_hit_point_bonus(int class_lvl, SkillType class_index, int cons,
                               Player *player)
{
    int return_val = 0;

    if (class_index < 0 || class_index >= SKILL_COUNT) {
        log_warn("effect: no class %d to work out a con bonus for",
                 (int)class_index);
        return 0;
    }

    /* No class earns the bonus past its last hit die. */
    if (GBL_MAX_CLASS_HIT_DICE[class_index] <= class_lvl) {
        class_lvl = GBL_MAX_CLASS_HIT_DICE[class_index] - 1;
    }

    /* A ranger has one hit die more than their level. */
    if (class_index == SKILL_RANGER &&
        (player->multiclass_level == 0 ||
         player->class_level_old[SKILL_RANGER] == player->multiclass_level)) {
        class_lvl += 1;
    }

    if (class_index == SKILL_FIGHTER ||
        class_index == SKILL_PALADIN ||
        class_index == SKILL_RANGER) {
        if (cons >= 15 && cons <= 19) {
            return_val = class_lvl * (cons - 14);
        } else if (cons == 20) {
            return_val = class_lvl * 5;
        } else if (cons >= 21 && cons <= 23) {
            return_val = class_lvl * 6;
        } else if (cons >= 24 && cons <= 25) {
            return_val = class_lvl * 7;
        }
    } else {
        if (cons > 15) {
            return_val = class_lvl * 2;
        } else if (cons == 15) {
            return_val = class_lvl;
        }
    }

    return return_val;
}

/* The strength a girdle of giant strength grants, by its second affect byte:
 * 18/00, then 19 through 24 (ovr024's switch on item.affect_2). */
static void girdle_strength(int *stat_b, int *str_00_b, int affect_2)
{
    switch (affect_2) {
    case 0:
        *stat_b = 18;
        *str_00_b = 100;
        break;
    case 1: *stat_b = 19; break;
    case 2: *stat_b = 20; break;
    case 3: *stat_b = 21; break;
    case 4: *stat_b = 22; break;
    case 5: *stat_b = 23; break;
    case 6: *stat_b = 24; break;
    default: break;
    }
}

/* sub_648D9 */
void effect_calc_stat_bonuses(Stat stat_index, Player *player)
{
    int stat_b = 0;
    int str_00_b = 0;
    /* var_11: a stat an item pins to a fixed value - a helm of stupidity, say.
     * 0xff means nothing has. */
    int var_11 = 0x0ff;
    int stat_a;
    int str_00_a = player->stats.value[PSTAT_STR00].full;

    if (stat_index < 0 || stat_index >= STAT_COUNT) {
        log_warn("effect: no stat %d to work out bonuses for", (int)stat_index);
        return;
    }

    stat_a = player->stats.value[stat_index].cur;

    for (int i = 0; i < player->item_count; i++) {
        const Item *item = &player->items[i];

        /* The item affects are ids 0x81 and up; the readied item's own affect_3
         * says which stat it changes. */
        if (item->affect_3 <= 0x80 || item->readied == false) {
            continue;
        }

        int var_12 = item->affect_3 & 0x7f;

        if (stat_index == STAT_STR) {
            if (var_12 == 5) {
                girdle_strength(&stat_b, &str_00_b, item->affect_2);
            } else if (var_12 == 8) {
                if (player->stats.value[PSTAT_STR].cur < 18 &&
                    item->affect_2 == 0) {
                    stat_b = (u8)(player->stats.value[PSTAT_STR].cur + 1);
                    str_00_b = 0;
                }
            } else if (var_12 == 13) {
                var_11 = 3;
            }

            effect_max_strength(&stat_a, stat_b, &str_00_a, str_00_b);
        } else if (stat_index == STAT_CON) {
            if (var_12 == 6) {
                stat_a++;
            } else if (var_12 == 8 &&
                       player->stats.value[PSTAT_CON].cur < 18 &&
                       item->affect_2 == 4) {
                stat_a++;
            }
        } else if (stat_index == STAT_INT) {
            if (var_12 == 8) {
                if (player->stats.value[PSTAT_INT].cur < 18 &&
                    item->affect_2 == 1) {
                    stat_a++;
                }
            } else if (var_12 == 12) {
                var_11 = 7;
            } else if (var_12 == 13) {
                var_11 = 3;
            }
        } else if (stat_index == STAT_WIS) {
            if (var_12 == 8 && item->affect_2 == 2 &&
                player->stats.value[PSTAT_WIS].cur < 18) {
                stat_a++;
            }
        } else if (stat_index == STAT_DEX) {
            if (var_12 == 2) {
                /* Gauntlets of dexterity are worth most to the clumsiest. */
                if (player->stats.value[PSTAT_DEX].cur <= 6) {
                    stat_a += 4;
                } else if (player->stats.value[PSTAT_DEX].cur <= 13) {
                    stat_a += 2;
                } else {
                    stat_a++;
                }
            } else if (var_12 == 8) {
                if (player->stats.value[PSTAT_DEX].cur < 18 &&
                    item->affect_2 == 3) {
                    stat_a++;
                }
            } else if (var_12 == 10) {
                stat_a -= 2;
            }
        } else if (stat_index == STAT_CHA) {
            if (var_12 == 6) {
                stat_a -= 1;
            } else if (var_12 == 8 &&
                       player->stats.value[PSTAT_CHA].cur < 18 &&
                       item->affect_2 == 5) {
                stat_a += 1;
            }
        }
    }

    if (stat_index == STAT_STR) {
        Affect *affect_ptr = affect_list_find(&player->affects, AFFECT_STRENGTH);

        if (affect_ptr != NULL) {
            effect_decode_strength(&str_00_b, &stat_b, affect_ptr);

            /* The strength spell adds to what the character has, and a fighting
             * class carries the overflow into the percentile. */
            if (stat_a <= 18 && str_00_a < 100) {
                stat_b += stat_a;

                if (stat_b > 18) {
                    if (player->class_level[SKILL_FIGHTER] > 0 ||
                        player->class_level_old[SKILL_FIGHTER] > 0 ||
                        player->class_level[SKILL_PALADIN] > 0 ||
                        player->class_level_old[SKILL_PALADIN] > 0 ||
                        player->class_level[SKILL_RANGER] > 0 ||
                        player->class_level_old[SKILL_RANGER] > 0) {
                        str_00_b = (u8)(player->stats.value[PSTAT_STR00].cur +
                                        ((stat_b - 18) * 10));

                        if (str_00_b > 100) {
                            str_00_b = 100;
                        }

                        stat_b = 18;
                    } else {
                        stat_b = 18;
                    }
                }
            }

            effect_max_strength(&stat_a, stat_b, &str_00_a, str_00_b);
        }

        affect_ptr = affect_list_find(&player->affects, AFFECT_STRENGTH_SPELL);

        if (affect_ptr != NULL) {
            effect_decode_strength(&str_00_b, &stat_b, affect_ptr);
            effect_max_strength(&stat_a, stat_b, &str_00_a, str_00_b);
        }

        affect_ptr = affect_list_find(&player->affects, AFFECT_ENLARGE);

        if (affect_ptr != NULL) {
            effect_decode_strength(&str_00_b, &stat_b, affect_ptr);
            effect_max_strength(&stat_a, stat_b, &str_00_a, str_00_b);
        }

        if (var_11 != 0xff) {
            player->stats.value[PSTAT_STR].full = var_11;
            player->stats.value[PSTAT_STR00].cur = 0;
        } else {
            player->stats.value[PSTAT_STR].full = stat_a;
            player->stats.value[PSTAT_STR00].cur = str_00_a;
        }
    } else if (stat_index == STAT_CON) {
        int hit_point_bonus = 0;
        int class_count = 0;
        u8  orig_max_hp = player->hit_point_max;

        player->hit_point_max = player->hit_point_rolled;

        for (SkillType class_id = SKILL_CLERIC; class_id <= SKILL_MONK;
             class_id++) {
            int class_lvl = player->class_level_old[class_id];

            /* A dual-classed character keeps the bonus their first class earned
             * as well as their second's. */
            if (class_lvl > 0) {
                hit_point_bonus += effect_con_hit_point_bonus(class_lvl,
                                                             class_id, stat_a,
                                                             player);
            }

            class_lvl = player->class_level[class_id];

            if (class_lvl > 0) {
                class_count++;
            }

            if (GBL_MAX_CLASS_HIT_DICE[class_id] < class_lvl) {
                class_lvl = GBL_MAX_CLASS_HIT_DICE[class_id];
            }

            /* A multi-classed character shares hit points between their classes,
             * so only the levels past the shared ones count here. */
            if (class_lvl > player->multiclass_level) {
                class_lvl -= player->multiclass_level;

                hit_point_bonus += effect_con_hit_point_bonus(class_lvl,
                                                             class_id, stat_a,
                                                             player);
            }
        }

        /* Averaged over the classes. A character with no class levels at all -
         * a monster - divided by zero here, which the C# threw on; the bonus is
         * nought in that case whatever it is divided by. */
        if (class_count > 0) {
            hit_point_bonus /= class_count;
        } else if (hit_point_bonus != 0) {
            log_warn("effect: %d hit points of con bonus for no class at all",
                     hit_point_bonus);
        }

        player->hit_point_max += (u8)hit_point_bonus;

        /* What the maximum gained or lost, the character gains or loses too. */
        if (player->hit_point_max > orig_max_hp) {
            player->hit_point_current += (u8)(player->hit_point_max -
                                              orig_max_hp);
        } else if (player->hit_point_max < orig_max_hp) {
            if (player->hit_point_current > (orig_max_hp -
                                             player->hit_point_max)) {
                player->hit_point_current -= (u8)(orig_max_hp -
                                                  player->hit_point_max);
            } else {
                player->hit_point_current = 0;
            }
        }

        player->stats.value[PSTAT_CON].full = stat_a;

        if (player->stats.value[PSTAT_CON].full >= 20) {
            if (player_has_affect(player, AFFECT_HIGH_CON_REGEN) == false) {
                /* Per 1st edition, healing is one point every six turns at 20
                 * and every turn at 25. */
                u16 rounds = (u16)((26 - player->stats.value[PSTAT_CON].full) *
                                   10);

                effect_add_affect(true, 0xff, rounds, AFFECT_HIGH_CON_REGEN,
                                  player);
            }
        } else {
            effect_remove_affect(NULL, AFFECT_HIGH_CON_REGEN, player);
        }
    } else if (stat_index == STAT_INT) {
        if (player_has_affect(player, AFFECT_FEEBLEMIND) && var_11 > 7) {
            var_11 = 3;
        }

        if (var_11 != 0xff) {
            player->stats.value[PSTAT_INT].full = var_11;
        } else {
            player->stats.value[PSTAT_INT].full = stat_a;
        }
    } else if (stat_index == STAT_WIS) {
        if (player_has_affect(player, AFFECT_FEEBLEMIND) && var_11 > 7) {
            var_11 = 3;
        }

        if (var_11 != 0xff) {
            player->stats.value[PSTAT_WIS].full = var_11;
        } else {
            player->stats.value[PSTAT_WIS].full = stat_a;
        }
    } else if (stat_index == STAT_DEX) {
        if (var_11 != 0xff) {
            player->stats.value[PSTAT_DEX].full = var_11;
        } else {
            player->stats.value[PSTAT_DEX].full = stat_a;
        }
    } else if (stat_index == STAT_CHA) {
        /* The friends spell sets the charisma it wants outright. */
        const Affect *affect = affect_list_find_const(&player->affects,
                                                      AFFECT_FRIENDS);

        if (affect != NULL) {
            stat_a = affect->affect_data;
        }

        player->stats.value[PSTAT_CHA].full = stat_a;
    }
}
