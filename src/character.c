/* character.c - Ported from engine/ovr025.cs. */
#include <stdio.h>
#include <string.h>

#include "character.h"

#include "affect.h"
#include "cheats.h"
#include "combatmap.h"
#include "display.h"
#include "draw.h"
#include "frames.h"
#include "gbl.h"
#include "input.h"
#include "log.h"
#include "picture.h"
#include "prompt.h"
#include "set.h"
#include "sound.h"
#include "spells.h"
#include "target.h"
#include "text.h"
#include "view3d.h"

static int abs_int(int v)
{
    return v < 0 ? -v : v;
}

static int sign_int(int v)
{
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

/* --------------------------------------------------- derived combat values */

/* sub_66023 */
void character_calculate_attack_values(Player *player)
{
    Item *item = player_primary_weapon(player);
    const ItemData *data;
    int bonus;

    if (item == NULL) {
        return;
    }

    data = item_data(item->type);

    player->hit_bonus = player->thac0;

    if ((data->flags & ITEM_DATA_FLAG_02) != 0) {
        player->hit_bonus += character_dex_reaction_adj(player);
    }

    player->attack1_damage_bonus = data->bonus_normal;

    if ((data->flags & ITEM_DATA_MELEE) != 0) {
        player->hit_bonus += character_strength_hit_bonus(player);
        player->attack1_damage_bonus =
            (i8)(player->attack1_damage_bonus + character_strength_dam_bonus(player));
    }

    bonus = item->plus;

    if ((data->flags & ITEM_DATA_QUARRELS) != 0 &&
        player_quarrels(player) != NULL) {
        bonus += player_quarrels(player)->plus;
    }

    if ((data->flags & ITEM_DATA_ARROWS) != 0 &&
        player_arrows(player) != NULL) {
        bonus += player_arrows(player)->plus;
    }

    player->attack1_damage_bonus = (i8)(player->attack1_damage_bonus + bonus);

    /* Elves get a point with the blades and bows of their own making - and it
     * lands on the hit bonus only, because the damage bonus has already been
     * added up. */
    if (player->race == RACE_ELF &&
        (item->type == ITEM_COMPOSITE_LONG_BOW ||
         item->type == ITEM_COMPOSITE_SHORT_BOW ||
         item->type == ITEM_LONG_BOW ||
         item->type == ITEM_SHORT_BOW ||
         item->type == ITEM_SHORT_SWORD ||
         item->type == ITEM_LONG_SWORD)) {
        bonus++;
    }

    player->hit_bonus += bonus;
    player->attack1_dice_count = data->dice_count_normal;
    player->attack1_dice_size  = data->dice_size_normal;
}

/* sub_6621E */
void character_armor_weight_effect(const Item *item, Player *player)
{
    if (item == NULL) {
        return;
    }

    if (item_data(item->type)->slot == ITEM_SLOT_ARMOR) {
        if (item->weight >= 0 && item->weight <= 150) {
            player->movement = player->base_movement;
        } else if (item->weight >= 151 && item->weight <= 399) {
            player->movement = 9;
        } else {
            player->movement = 6;
        }

        if (player->movement != 0 && player->movement <= 9) {
            player->movement = (u8)(player->movement + 3);
        }
    }
}

/* sub_662A6. The four item bonuses, kept in one array so that only the best of
 * each kind counts: [1] is the shield slot's, [2] everything worn that stacks -
 * cloaks, bracers - [3] the best ring, and [4] the best armour. bonus[0] is the
 * dexterity bonus and is not touched here.
 *
 * Only items whose field_6 has the top bit set carry a bonus at all; the rest of
 * the byte is how much of it comes from the item type rather than its plus. */
static void apply_item_bonus(u8 *output, i8 *bonus, const Item *item,
                            Player *player)
{
    u8 var_1 = item_data(item->type)->field_6;
    int item_slot;

    if (var_1 <= 0x7f) {
        return;
    }

    var_1 &= 0x7f;
    item_slot = item_data(item->type)->slot;

    if (item_slot == ITEM_SLOT_1) {
        bonus[1] = (i8)(item->plus + var_1);
        return;
    }

    if (var_1 == 0) {
        if (item_slot == ITEM_SLOT_9) {
            if (item->plus > bonus[3]) {
                bonus[3] = (i8)item->plus;
            }
        } else {
            bonus[2] = (i8)(bonus[2] + item->plus);
        }

        player->field_186 = (i8)(player->field_186 + item->plus_save);
        return;
    }

    if ((item->plus + var_1) > bonus[4]) {
        bonus[4] = (i8)(item->plus + var_1);

        /* Magical armour cancels the ring's protection rather than stacking
         * with it, which is what the caller uses this flag for. */
        if (item->plus > 0 && item_slot == ITEM_SLOT_ARMOR) {
            *output = 1;
        }
    }
}

/* sub_663C4 */
void character_calc_movement(Player *player)
{
    int overload = player->weight - character_max_encumberance(player);
    int moves;

    if (overload < 0) {
        overload = 0;
    }

    if (overload >= 0 && overload <= 0x200) {
        moves = player->movement;
    } else if (overload >= 0x201 && overload <= 0x300) {
        moves = 9;
    } else if (overload >= 0x301 && overload <= 0x400) {
        moves = 6;
    } else {
        moves = 3;
    }

    if (moves < player->movement) {
        player->movement = (u8)moves;
    }
}

/* sub_66C20, reclac_player_values */
void character_recalc_values(Player *player)
{
    i8   stat_bonus[5];
    u8   var_7 = 0;
    /* var_8 was never assigned anything but false, which leaves the block below
     * it - the one that takes 5000 coins off the weight carried - unreachable.
     * It is kept, and kept unreachable, because the original had it. */
    const bool var_8 = false;
    int  total_item_weight = 0;

    player_ready_reset(player);

    player->weapons_hands_used = 0;
    player->weight = 0;

    for (int i = 0; i < player->item_count; i++) {
        Item *item = &player->items[i];
        i16 item_weight = item->weight;

        if (item->count > 0) {
            item_weight = (i16)(item_weight * item->count);
        }

        player->weight = (i16)(player->weight + item_weight);

        if (item->readied) {
            int slot = item_data(item->type)->slot;

            total_item_weight += item_weight;

            if (slot >= ITEM_SLOT_0 && slot <= ITEM_SLOT_8) {
                player_ready_set(player, (ItemSlot)slot, i);
            } else if (slot == ITEM_SLOT_9) {
                /* Two slots for the ones there can be two of - a pair of rings,
                 * or a shield in each hand. */
                if (player_ready_item(player, (ItemSlot)9) != NULL) {
                    if (player_ready_item(player, (ItemSlot)10) == NULL) {
                        player_ready_set(player, (ItemSlot)10, i);
                    }
                } else {
                    player_ready_set(player, (ItemSlot)9, i);
                }
            }

            if (item->type == ITEM_ARROW) {
                player_ready_set(player, (ItemSlot)11, i);
            }

            if (item->type == ITEM_QUARREL) {
                player_ready_set(player, ITEM_SLOT_QUARREL, i);
            }

            player->weapons_hands_used =
                (u8)(player->weapons_hands_used + item_hands_count(item));
        }
    }

    /* Coins and gems weigh one each. */
    for (int money = 0; money < MONEY_KINDS; money++) {
        player->weight = (i16)(player->weight +
                               money_get(&player->money, (MoneyKind)money));
    }

    player->attack1_dice_count = player->attack1_dice_count_base;
    player->attack2_dice_count = player->attack2_dice_count_base;

    player->attack1_dice_size = player->attack1_dice_size_base;
    player->attack2_dice_size = player->attack2_dice_size_base;

    player->attack1_damage_bonus = (i8)player->attack1_damage_bonus_base;
    player->attack2_damage_bonus = player->attack2_damage_bonus_base;

    for (int i = 0; i <= 4; i++) {
        stat_bonus[i] = 0;
    }

    player->field_186 = 0;
    player->ac        = player->base_ac;
    player->movement  = player->base_movement;
    player->hit_bonus = player->thac0;

    stat_bonus[0] = (i8)character_dex_ac_bonus(player);

    /* Bare hands get the strength bonuses here; a weapon gets them in
     * character_calculate_attack_values, which overwrites the hit bonus. */
    if (player_primary_weapon(player) == NULL) {
        player->hit_bonus += character_strength_hit_bonus(player);
        player->attack1_damage_bonus =
            (i8)(player->attack1_damage_bonus + character_strength_dam_bonus(player));
    }

    character_calculate_attack_values(player);

    for (int i = 0; i < player->item_count; i++) {
        Item *item = &player->items[i];

        if (item->readied) {
            character_armor_weight_effect(item, player);
            apply_item_bonus(&var_7, stat_bonus, item, player);
        }
    }

    /* Magical armour was found, so the ring of protection does nothing. */
    if (var_7 != 0) {
        stat_bonus[3] = 0;
    }

    if (var_8) {
        if (player->weight < 5000) {
            player->weight = 0;
        } else {
            player->weight = (i16)(player->weight - 5000);
        }

        if (player->weight < total_item_weight) {
            player->weight = (i16)total_item_weight;
        }
    }

    character_calc_movement(player);

    /* The armour slot starts at the character's own armour class - a monster's
     * hide, or 10 for anyone else - and magical armour only helps if it is
     * better. */
    if (stat_bonus[4] < player->ac) {
        stat_bonus[4] = (i8)player->ac;
    }

    player->ac = 0;

    for (int i = 0; i <= 4; i++) {
        player->ac = (u8)(player->ac + (u8)stat_bonus[i]);
    }

    /* An attack from behind gets no dexterity or weapon bonus, and two points
     * of the armour's. */
    player->ac_behind = (u8)((stat_bonus[4] + stat_bonus[2] + stat_bonus[3]) - 2);

    if (player_skill_level(player, SKILL_FIGHTER) > 0 &&
        player->race > RACE_MONSTER) {
        player->attack_level = (u8)player_skill_level(player, SKILL_FIGHTER);
    } else {
        player->attack_level = 1;
    }
}

/* stat_bonus */
int character_dex_ac_bonus(const Player *player)
{
    int stat_val = player->stats.value[PSTAT_DEX].full;
    int bonus;

    if (stat_val >= 1 && stat_val <= 3) {
        bonus = -4;
    } else if (stat_val >= 4 && stat_val <= 6) {
        bonus = stat_val - 7;
    } else if (stat_val >= 15 && stat_val <= 18) {
        bonus = stat_val - 14;
    } else if (stat_val == 19 || stat_val == 20) {
        bonus = 4;
    } else if (stat_val >= 21 && stat_val <= 23) {
        bonus = 5;
    } else if (stat_val == 24 || stat_val == 25) {
        bonus = 6;
    } else {
        bonus = 0;
    }

    return bonus;
}

int character_dex_reaction_adj(const Player *player)
{
    int stat_val = player->stats.value[PSTAT_DEX].full;
    int bonus;

    if (stat_val >= 0 && stat_val <= 2) {
        bonus = -4;
    } else if (stat_val >= 3 && stat_val <= 5) {
        bonus = stat_val - 6;
    } else if (stat_val >= 16 && stat_val <= 18) {
        bonus = stat_val - 15;
    } else if (stat_val >= 19 && stat_val <= 20) {
        bonus = 3;
    } else if (stat_val >= 21 && stat_val <= 23) {
        bonus = 4;
    } else if (stat_val >= 24 && stat_val <= 25) {
        bonus = 5;
    } else {
        bonus = 0;
    }

    return bonus;
}

/* playerStrengh */
int character_strength_group(const Player *player)
{
    int str = player->stats.value[PSTAT_STR].full;
    int str00 = player->stats.value[PSTAT_STR00].cur;

    if (str >= 0 && str <= 17) {
        return str;
    }

    if (str == 18) {
        if (str00 == 0) {
            return 18;
        }
        if (str00 >= 1 && str00 <= 50) {
            return 19;
        }
        if (str00 >= 51 && str00 <= 75) {
            return 20;
        }
        if (str00 >= 76 && str00 <= 90) {
            return 21;
        }
        if (str00 >= 91 && str00 <= 99) {
            return 22;
        }
        if (str00 >= 100) {
            return 23;
        }

        /* Percentile strength is a byte, so this cannot be reached; the C# threw
         * NotSupportedException for it. */
        log_warn("%s has strength 18/%d", player->name, str00);
        return 18;
    }

    if (str >= 19 && str <= 25) {
        return str + 5;
    }

    log_warn("%s has strength %d", player->name, str);
    return 0;
}

int character_strength_hit_bonus(const Player *player)
{
    int str_stat = character_strength_group(player);
    int str_bonus = 0;

    if (player->field_125 != 0) {
        if (str_stat >= 1 && str_stat <= 3) {
            str_bonus = -3;
        } else if (str_stat == 4 || str_stat == 5) {
            str_bonus = -2;
        } else if (str_stat == 6 || str_stat == 7) {
            str_bonus = -1;
        } else if (str_stat >= 17 && str_stat <= 19) {
            str_bonus = 1;
        } else if (str_stat >= 20 && str_stat <= 22) {
            str_bonus = 2;
        } else if (str_stat >= 23 && str_stat <= 25) {
            str_bonus = 3;
        } else if (str_stat == 26 || str_stat == 27) {
            str_bonus = 4;
        } else if (str_stat >= 28 && str_stat <= 30) {
            str_bonus = str_stat - 23;
        }
    }

    return str_bonus;
}

int character_strength_dam_bonus(const Player *player)
{
    int var_2 = character_strength_group(player);
    int damage_bonus = 0;

    if (player->field_125 != 0) {
        if (var_2 == 1 || var_2 == 2) {
            damage_bonus = -2;
        } else if (var_2 >= 3 && var_2 <= 5) {
            damage_bonus = -1;
        } else if (var_2 == 16) {
            damage_bonus = 1;
        } else if (var_2 >= 17 && var_2 <= 19) {
            damage_bonus = var_2 - 16;
        } else if (var_2 >= 20 && var_2 <= 29) {
            damage_bonus = var_2 - 17;
        } else if (var_2 == 30) {
            damage_bonus = 14;
        }
    }

    return (i8)damage_bonus;
}

/* strength_bonus */
int character_max_encumberance(const Player *player)
{
    int str = character_strength_group(player);
    int max_encumberance;

    if (str >= 1 && str <= 3) {
        max_encumberance = -350;
    } else if (str == 4 || str == 5) {
        max_encumberance = -250;
    } else if (str == 6 || str == 7) {
        max_encumberance = -150;
    } else if (str == 12 || str == 13) {
        max_encumberance = 100;
    } else if (str == 14 || str == 15) {
        max_encumberance = 200;
    } else if (str == 16) {
        max_encumberance = 350;
    } else if (str >= 17 && str <= 21) {
        max_encumberance = ((str - 17) * 250) + 500;
    } else if (str >= 22 && str <= 26) {
        max_encumberance = ((str - 22) * 1000) + 2000;
    } else if (str == 27) {
        max_encumberance = 7500;
    } else if (str >= 28 && str <= 30) {
        max_encumberance = ((str - 28) * 3000) + 9000;
    } else {
        /* Strength 8 to 11, and 0: the average carry no bonus at all. */
        max_encumberance = 0;
    }

    return max_encumberance;
}

/* ------------------------------------------------------------------- items */

void character_lose_item(Item *item, Player *player)
{
    int index = -1;

    if (item != NULL && player != NULL) {
        for (int i = 0; i < player->item_count; i++) {
            if (&player->items[i] == item) {
                index = i;
                break;
            }
        }
    }

    if (index < 0 || !player_item_remove(player, index)) {
        text_display_and_pause("Tried to Lose item & couldn't find it!", 14);
    }
}

/* Appends to item->name, which is 42 characters and no more. The C# built an
 * unbounded string; the record cannot hold one, and the display it is built for
 * is 38 columns wide, so a longer name is cut short here. */
static void name_append(Item *item, const char *text)
{
    size_t used = strlen(item->name);
    size_t room = sizeof(item->name) - 1 - used;

    if (room == 0) {
        return;
    }
    strncat(item->name, text, room);
}

/* id_item */
void character_item_display_name_build(bool display_new_name,
                                       bool display_readied,
                                       int y_col, int x_col, Item *item)
{
    char generated[ITEM_NAME_GEN_MAX];
    bool detect_magic = false;
    int hidden_names_flag;

    if (item == NULL) {
        return;
    }

    item->name[0] = '\0';

    if (display_readied) {
        name_append(item, item->readied ? " Yes  " : " No   ");
    }

    for (int i = 0; i < gbl.team_count; i++) {
        if (gbl.team_list[i] != NULL &&
            player_has_affect(gbl.team_list[i], AFFECT_DETECT_MAGIC)) {
            detect_magic = true;
            break;
        }
    }

    if (detect_magic &&
        (item->plus > 0 || item->plus_save > 0 || item->cursed)) {
        name_append(item, "* ");
    }

    if (item->count > 0) {
        char count_text[16];

        snprintf(count_text, sizeof(count_text), "%d ", item->count);
        name_append(item, count_text);
    }

    hidden_names_flag = item->hidden_names_flag;

    if (cheats.display_full_item_names) {
        hidden_names_flag = 0;
    }

    name_append(item, item_generate_name(item, hidden_names_flag,
                                        generated, sizeof(generated)));

    if (display_new_name) {
        text_display_string(item->name, 0, 10, y_col, x_col);
    }
}

bool character_item_is_ranged_melee(const Item *item)
{
    const u8 ranged_melee = ITEM_DATA_FLAG_10 | ITEM_DATA_MELEE;

    return item != NULL &&
           item_is_ranged(item) &&
           (item_data(item->type)->flags & ranged_melee) == ranged_melee;
}

/* offset_above_1 */
bool character_is_weapon_ranged(Player *player)
{
    const Item *item = player_primary_weapon(player);

    return item != NULL && item_is_ranged(item);
}

/* offset_equals_20 */
bool character_is_weapon_ranged_melee(Player *player)
{
    return character_item_is_ranged_melee(player_primary_weapon(player));
}

/* sub_6906C */
bool character_current_attack_item(Item **found_item, Player *player)
{
    Item *item;
    u8 flags = 0;

    if (found_item == NULL) {
        return false;
    }
    *found_item = NULL;

    item = player_primary_weapon(player);

    if (item != NULL) {
        flags = item_data(item->type)->flags;

        if ((flags & ITEM_DATA_FLAG_10) != 0) {
            *found_item = item;
        }

        if ((flags & ITEM_DATA_FLAG_08) != 0) {
            if ((flags & ITEM_DATA_ARROWS) != 0) {
                *found_item = player_arrows(player);
            }

            if ((flags & ITEM_DATA_QUARRELS) != 0) {
                *found_item = player_quarrels(player);
            }
        }
    }

    /* A sling has both flags and spends nothing: it is always ready. */
    return *found_item != NULL ||
           flags == (ITEM_DATA_FLAG_08 | ITEM_DATA_FLAG_02);
}

/* ------------------------------------------------------------------ display */

void character_party_summary(const Player *player)
{
    int x_pos;
    int y_pos = 2;

    if (gbl.game_state == GAME_STATE_WILDERNESS_MAP) {
        return;
    }

    x_pos = (gbl.game_state == GAME_STATE_START_GAME_MENU) ? 1 : 17;

    text_display_string("Name", 0, 15, y_pos, x_pos);
    text_display_string("AC  HP", 0, 15, y_pos, 0x21);

    y_pos += 2;

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *tmp_player = gbl.team_list[i];
        char ac_text[8];
        int hp_x_pos = 0;

        if (tmp_player == NULL) {
            continue;
        }

        frames_clear_area(y_pos, 0x26, y_pos, x_pos);

        if (tmp_player == player) {
            text_display_string(tmp_player->name, 0, 15, y_pos, x_pos);
        } else {
            character_display_name(false, y_pos, x_pos, tmp_player);
        }

        snprintf(ac_text, sizeof(ac_text), "%-3d",
                 player_display_ac(tmp_player));
        text_display_string(ac_text, 0, 10, y_pos, 0x1f);

        /* Hit points are right-aligned by hand: one column in for two digits,
         * two for one. */
        if (tmp_player->hit_point_current <= 9) {
            hp_x_pos = 2;
        } else if (tmp_player->hit_point_current <= 99) {
            hp_x_pos = 1;
        }

        character_display_hp(false, y_pos, hp_x_pos + 0x24, tmp_player);
        y_pos++;
    }
}

void character_display_ac(int y_offset, int x_offset, const Player *player)
{
    char text[8];

    snprintf(text, sizeof(text), "%d", player_display_ac(player));
    text_display_string(text, 0, 10, y_offset, x_offset);
}

void character_display_hp(bool highlighted, int y_pos, int x_pos,
                          const Player *player)
{
    char text[8];
    int colour;

    if (player->hit_point_current < player->hit_point_max) {
        colour = 0x0e;
    } else {
        colour = 0x0a;
    }

    if (highlighted) {
        colour = 0x0d;
    }

    snprintf(text, sizeof(text), "%d", player->hit_point_current);
    text_display_string(text, 0, colour, y_pos, x_pos);
}

/* hitpoint_ac */
void character_combat_display_summary(Player *player)
{
    int line = 1;

    if (!gbl.display_hitpoints_ac) {
        return;
    }

    gbl.display_hitpoints_ac = false;
    frames_clear_region(TEXT_REGION_COMBAT_SUMMARY);

    character_display_status_string(false, line, " ", player);

    line++;

    text_display_string("Hitpoints", 0, 10, line + 1, 0x17);

    character_display_hp(false, line + 1, 0x21, player);
    line += 2;

    text_display_string("AC", 0, 10, line + 1, 0x17);
    character_display_ac(line + 1, 0x1a, player);

    gbl.text_y_col = line + 1;

    if (player_primary_weapon(player) != NULL) {
        Item *weapon = player_primary_weapon(player);

        line += 2;
        character_item_display_name_build(false, false, 0, 0, weapon);

        text_press_any_key(weapon->name, true, 10, line + 3, 0x26,
                           line + 1, 0x17);
    }

    /* press_any_key leaves the cursor after the weapon's name, and the line
     * below that is where the character's state goes. */
    line = gbl.text_y_col + 1;

    if (!player->in_combat) {
        text_display_string(player_health_status_name(player->health_status),
                            0, 15, line + 1, 0x17);
    } else if (player_is_held(player)) {
        text_display_string("(Helpless)", 0, 15, line + 1, 0x17);
    }
}

/* sub_678A2 */
void character_display_name(bool plural, int y_offset, int x_offset,
                            const Player *player)
{
    char name[PLAYER_NAME_MAX + 3];
    int color;

    if (!player->in_combat) {
        color = 0x0c;
    } else if (player->combat_team == TEAM_ENEMY) {
        color = 0x0e;
    } else {
        color = 0x0b;
    }

    snprintf(name, sizeof(name), "%s%s", player->name, plural ? "'s" : "");

    text_display_string(name, 0, color, y_offset, x_offset);
}

/* sub_67788 */
void character_display_status_string(bool clear_display, int line_y,
                                     const char *text, Player *player)
{
    if (gbl.game_state == GAME_STATE_COMBAT) {
        frames_clear_area(0x15, 0x26, line_y, 0x17);

        character_display_name(false, line_y, 0x17, player);
        text_press_any_key(text, true, 10, 0x15, 0x26, line_y + 1, 0x17);
    } else {
        int text_line_y = gbl.display_player_status_line18 ? 18 : 17;

        frames_clear_area(0x16, 0x26, text_line_y, 1);

        character_display_name(false, text_line_y + 1, 1, player);
        text_press_any_key(text, true, 10, 0x16, 0x26, text_line_y + 2, 1);
    }

    if (clear_display) {
        text_game_delay();
        character_clear_text_area();
    }
}

/* sub_6786F */
void character_clear_text_area(void)
{
    if (gbl.game_state == GAME_STATE_COMBAT) {
        frames_clear_area(0x15, 0x26, 0x0a, 0x17);
    } else {
        frames_clear_area(0x16, 0x26, 0x12, 1);
    }
}

void character_print_message(const char *text)
{
    prompt_clear_area_no_update();

    text_display_string(text, 0, 10, 0x18, 0);

    text_game_delay();

    prompt_clear_area_no_update();
}

/* load_pic */
void character_load_pic(void)
{
    gbl.can_draw_bigpic = true;

    switch (gbl.game_state) {
    case GAME_STATE_START_GAME_MENU:
        frames_draw_outer();
        break;

    case GAME_STATE_SHOP:
        if (gbl.redraw_boarder) {
            frames_draw_03();
        }

        /* Block 0x50 is the one picture that is a picture and not a portrait. */
        if (gbl.last_dax_block_id == 0x50) {
            picture_draw_maybe_overlayed(gbl.pic_frames.frames[0].picture,
                                         true, 3, 3);
        } else {
            picture_head_body(gbl.body_block_id, gbl.head_block_id);
            picture_draw_head_and_body(true, 3, 3);
        }

        character_party_summary(gbl.selected_player);
        character_display_map_position_time();
        break;

    case GAME_STATE_CAMPING:
        frames_draw_03();
        picture_load_pic_final(&gbl.pic_frames, 0, 0x1d, "PIC");
        character_party_summary(gbl.selected_player);
        character_display_map_position_time();
        break;

    case GAME_STATE_DUNGEON_MAP:
        frames_draw_03();
        view3d_redraw();
        character_party_summary(gbl.selected_player);
        character_display_map_position_time();
        gbl.byte_1EE98 = false;
        break;

    case GAME_STATE_WILDERNESS_MAP:
        if (gbl.last_dax_block_id != 0x50) {
            view3d_redraw();
        }
        break;

    case GAME_STATE_AFTER_COMBAT:
        frames_draw_03();
        picture_load_pic_final(&gbl.pic_frames, 0, 1, "PIC");
        character_party_summary(gbl.selected_player);
        break;

    default:
        break;
    }
}

/* The eight compass directions. The C# threw ArgumentOutOfRangeException for
 * anything else; gbl.map_direction is always 0..7, so this only reports it. */
static const char *direction_name(int dir)
{
    static const char *const names[8] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };

    if (dir < 0 || dir > 7) {
        log_warn("no compass direction %d", dir);
        return "";
    }

    return names[dir];
}

/* camping_search */
void character_display_map_position_time(void)
{
    char output[64];
    size_t used = 0;
    int minutes;

    if (gbl.game_state == GAME_STATE_WILDERNESS_MAP) {
        return;
    }

    output[0] = '\0';
    minutes = (gbl.area_ptr->time_minutes_tens * 10) +
              gbl.area_ptr->time_minutes_ones;

    /* Some maps refuse to say where the party is. */
    if (gbl.area_ptr->block_area_view == 0 || cheats.always_show_areamap) {
        used += (size_t)snprintf(output, sizeof(output), "%d,%d ",
                                 gbl.map_pos_x, gbl.map_pos_y);
    }

    used += (size_t)snprintf(output + used, sizeof(output) - used, "%s %02d:%02d",
                             direction_name(gbl.map_direction),
                             (int)gbl.area_ptr->time_hour, minutes);

    if (gbl.print_commands) {
        used += (size_t)snprintf(output + used, sizeof(output) - used, "*");
    }

    if (gbl.game_state == GAME_STATE_CAMPING) {
        snprintf(output + used, sizeof(output) - used, " camping");
    } else if ((gbl.area2_ptr->search_flags & 1) > 0) {
        snprintf(output + used, sizeof(output) - used, " search");
    }

    frames_clear_area(15, 0x26, 15, 17);

    text_display_string(output, 0, 10, 15, 17);
}

/* sub_68DC0 */
void character_redraw_combat_screen(void)
{
    Point centre = point_screen_center();

    combatmap_color_0_8_inverse();
    frames_draw_combat();

    if (gbl.map_to_background_tile != NULL) {
        centre = point_add(gbl.map_to_background_tile->map_screen_top_left,
                           centre);
    }

    combatmap_redraw_area(8, 0xff, centre);
}

void character_select_a_player(Player **player, bool show_exit,
                              const char *prompt)
{
    /* unk_68DFA: return, escape, E for Exit and S for Select. */
    Set exit_keys;
    char input_key = ' ';
    char menu_text[16];
    char prompt_text[64];

    if (player == NULL) {
        return;
    }

    set_clear(&exit_keys);
    set_add(&exit_keys, 13);
    set_add(&exit_keys, 27);
    set_add(&exit_keys, 69);
    set_add(&exit_keys, 83);

    snprintf(menu_text, sizeof(menu_text), "Select%s",
             show_exit ? " Exit" : "");
    snprintf(prompt_text, sizeof(prompt_text), "%s ",
             prompt != NULL ? prompt : "");

    while (!set_member_of(&exit_keys, input_key)) {
        bool use_overlay = (gbl.game_state == GAME_STATE_CAMPING ||
                            gbl.game_state == GAME_STATE_AFTER_COMBAT);
        bool special_key = false;
        int index;

        character_party_summary(*player);

        input_key = prompt_display_input(&special_key, use_overlay,
                                        PROMPT_CTRL_WORD_ARROWS,
                                        GBL_DEFAULT_MENU_COLORS,
                                        menu_text, prompt_text);

        index = gbl_team_index_of(*player);

        if (special_key) {
            /* Up and Down do what Home and End do; see prompt_selection_key. */
            char selection_key = prompt_selection_key(input_key, special_key);

            if (gbl.team_count == 0) {
                *player = NULL;
            } else if (selection_key == 'O') {
                /* End or Down: the next character along, wrapping. */
                index = (index + 1) % gbl.team_count;
                *player = gbl.team_list[index];
            } else if (selection_key == 'G') {
                index = (index - 1 + gbl.team_count) % gbl.team_count;
                *player = gbl.team_list[index];
            }
        } else if (show_exit) {
            if (input_key == 'E' || input_key == 0) {
                *player = NULL;
            }
        }
    }
}

/* ------------------------------------------- missile and spell effects */

/* sub_67924.
 *
 * The C#'s mirrored path flipped the source icon itself and never wrote the
 * destination cell, which left frames 1 and 2 blank and mirrored the loaded
 * sprite for good. The DOS original built four cells here - the sprite, the
 * sprite mirrored, the attack pose mirrored, the attack pose - so the cell is
 * copied and the copy is mirrored. */
void character_load_missile_dax(bool flip_icon, int icon_offset,
                                CombatIconState icon_action, int icon_idx)
{
    DaxBlock *dst = gbl.missile_dax;
    const DaxBlock *src;
    size_t data_size;
    size_t offset;
    int row_pixels;

    if (dst == NULL || dst->data == NULL) {
        log_warn("no missile picture to load icon %d into", icon_idx);
        return;
    }
    if (icon_idx < 0 || icon_idx >= GBL_COMBAT_ICON_COUNT) {
        log_warn("no combat icon %d to load a missile from", icon_idx);
        return;
    }

    data_size = (size_t)dst->bpp;
    offset    = (size_t)icon_offset * data_size;

    if (icon_offset < 0 || offset + data_size > dst->data_size) {
        log_warn("no missile frame %d", icon_offset);
        return;
    }

    src = combat_icon_get(&gbl.combat_icons[icon_idx], icon_action, 0);

    if (src == NULL || src->data == NULL) {
        /* Nothing loaded in that slot: the frame draws as transparent. */
        memset(dst->data + offset, 0, data_size);
        return;
    }

    if ((size_t)src->bpp < data_size) {
        log_warn("combat icon %d is %d bytes, the missile frame is %zu",
                 icon_idx, src->bpp, data_size);
        memset(dst->data + offset, 0, data_size);
        data_size = (size_t)src->bpp;
    }

    row_pixels = dst->width * 8;

    if (!flip_icon) {
        memcpy(dst->data + offset, src->data, data_size);
        return;
    }

    for (size_t i = 0; i < data_size; i++) {
        size_t row = i / (size_t)row_pixels;
        size_t col = i % (size_t)row_pixels;

        dst->data[offset + i] =
            src->data[(row * (size_t)row_pixels) + ((size_t)row_pixels - col - 1)];
    }
}

/* sub_67A59 */
void character_load_missile_icons(int icon_idx)
{
    character_load_missile_dax(false, 0, COMBAT_ICON_NORMAL, icon_idx);
    character_load_missile_dax(true,  1, COMBAT_ICON_NORMAL, icon_idx);
    character_load_missile_dax(true,  2, COMBAT_ICON_ATTACK, icon_idx);
    character_load_missile_dax(false, 3, COMBAT_ICON_ATTACK, icon_idx);
}

/* One step in a compass direction; pathDir only ever holds 0..8. */
static Point direction_delta(int dir)
{
    if (dir < 0 || dir > 8) {
        dir = 8;
    }

    return point_make(GBL_MAP_DIR_X_DELTA[dir], GBL_MAP_DIR_Y_DELTA[dir]);
}

/* sub_67AA4.
 *
 * The flight is worked out in thirds of a square, which is how far the sprite
 * moves per drawn frame, and the map scrolls under it whenever the sprite has
 * covered a whole square. Two loops do the work: the first flies the missile
 * across the window it started in, and the second - reached only when the target
 * is off the window - re-centres on the target and flies the tail of the path
 * backwards from there to meet it. */
void character_draw_missile_attack(int delay, int frame_count, Point target,
                                   Point attacker)
{
    /* 0x94 thirds of a square, which is as far as the original could trace. */
    u8 path_dir[0x94];
    SteppingPath path;
    Point center;
    Point diff;
    Point delta;
    Point top_left;
    bool var_B4;
    bool var_B3;
    int var_AF = 0;
    int var_B0;
    int frame = 0;

    if (gbl.map_to_background_tile == NULL || gbl.missile_dax == NULL) {
        log_warn("a missile flew with no combat map to fly over");
        return;
    }

    memset(path_dir, 8, sizeof(path_dir));

    stepping_path_clear(&path);
    path.attacker = point_mul(attacker, 3);
    path.target   = point_mul(target, 3);

    stepping_path_calculate_deltas(&path);

    do {
        var_B4 = !stepping_path_step(&path);

        path_dir[var_AF] = path.direction;

        var_AF++;
    } while (!var_B4 && var_AF < (int)sizeof(path_dir));

    /* The last two entries are the step that arrived and the no-step that
     * reported the arrival, and neither is flown. */
    var_B0 = var_AF - 2;

    if (var_B0 < 2 || var_AF < 2) {
        return;
    }

    diff     = point_sub(target, attacker);
    top_left = gbl.map_to_background_tile->map_screen_top_left;

    if (!combatmap_coord_on_screen(point_sub(attacker, top_left)) ||
        !combatmap_coord_on_screen(point_sub(target, top_left))) {
        /* Neither end is on screen. A short flight is watched from half way
         * along it; a long one starts where the window already is. */
        if (abs_int(diff.x) <= 6 && abs_int(diff.y) <= 6) {
            var_B3 = true;
            center = point_add(point_div(diff, 2), attacker);
        } else {
            var_B3 = false;
            center = point_add(top_left, point_screen_center());
        }
    } else {
        var_B3 = true;
        center = point_add(top_left, point_screen_center());
    }

    combatmap_redraw_area(8, 0xff, center);
    var_AF = 0;
    delta  = point_make(0, 0);

    do {
        Point cur;

        top_left = gbl.map_to_background_tile->map_screen_top_left;
        cur = point_add(point_mul(point_sub(attacker, top_left), 3), delta);

        var_B4 = false;

        do {
            Point var_C6 = direction_delta(path_dir[var_AF]);

            cur = point_add(cur, var_C6);

            /* With no delay only the frames that land on a square boundary are
             * drawn, which is what makes an instant missile still visible. */
            if (delay > 0 || (cur.x % 3) == 0 || (cur.y % 3) == 0) {
                display_save_vid_ram();
                draw_overlay_bounded(gbl.missile_dax, 5, frame, cur.y, cur.x);
                /* seg040.DrawOverlay() went here; it does nothing. */

                input_sys_delay(delay);

                display_restore_vid_ram();
                frame++;

                if (frame >= frame_count) {
                    frame = 0;
                }
            }

            var_AF++;

            if (cur.x < 0 || cur.x > 0x12 || cur.y < 0 || cur.y > 0x12) {
                var_B4 = true;
            }

            if (!var_B4 && var_AF < var_B0) {
                delta = point_add(delta, var_C6);

                /* A whole square covered: the missile keeps going and the
                 * window it is drawn in moves with it. */
                if (abs_int(delta.x) == 3) {
                    attacker.x += sign_int(delta.x);
                    center.x   += sign_int(delta.x);
                    delta.x = 0;
                }

                if (abs_int(delta.y) == 3) {
                    attacker.y += sign_int(delta.y);
                    center.y   += sign_int(delta.y);
                    delta.y = 0;
                }
            }
        } while (var_AF < var_B0 && !var_B4);

        if (var_AF < var_B0) {
            /* The missile left the window before it arrived. Centre on the
             * target, as close to the middle of the screen as the map allows,
             * and walk the rest of the path backwards from it. */
            int var_CE = 0;
            int var_D0 = 0;

            delta    = point_make(0, 0);
            attacker = target;

            if ((target.x + SCREEN_HALF_X) > MAP_MAX_X) {
                var_CE = target.x - MAP_MAX_X;
            } else if (target.x < SCREEN_HALF_X) {
                var_CE = SCREEN_HALF_X - target.x;
            }

            if ((target.y + SCREEN_HALF_Y) > MAP_MAX_Y) {
                var_D0 = target.y - MAP_MAX_Y;
            } else if (target.y < SCREEN_HALF_Y) {
                var_D0 = SCREEN_HALF_Y - target.y;
            }

            center.x = target.x + var_CE;
            center.y = target.y + var_D0;

            combatmap_redraw_area(8, 0xff, center);

            top_left = gbl.map_to_background_tile->map_screen_top_left;
            cur      = point_mul(point_sub(target, top_left), 3);
            var_AF   = var_B0;
            var_B4   = false;

            do {
                Point var_C6 = point_sub(point_make(0, 0),
                                         direction_delta(path_dir[var_AF]));

                cur = point_add(cur, var_C6);

                if (cur.x > 18) {
                    attacker.x = top_left.x + SCREEN_MAX_X;
                } else if (cur.x < 0) {
                    attacker.x = top_left.x;
                }

                if (cur.y > 18) {
                    attacker.y = top_left.y + SCREEN_MAX_Y;
                } else if (cur.y < 0) {
                    attacker.y = top_left.y;
                }

                if (cur.x < 0 || cur.x > 18 || cur.y < 0 || cur.y > 18) {
                    var_B4 = true;
                }

                if (!var_B4) {
                    delta = point_add(delta, var_C6);

                    if (abs_int(delta.x) == SCREEN_HALF_X) {
                        attacker.x += sign_int(delta.x);
                        center.x   += sign_int(delta.x);
                        delta.x = 0;
                    }

                    if (abs_int(delta.y) == SCREEN_HALF_Y) {
                        attacker.y += sign_int(delta.y);
                        center.y   += sign_int(delta.y);
                        delta.y = 0;
                    }

                    var_AF -= 1;

                    /* The original walked off the front of the path here and
                     * read whatever was below it on the stack; there is nothing
                     * left to walk back through, so stop. */
                    if (var_AF < 0) {
                        log_warn("a missile ran out of path walking back");
                        var_B4 = true;
                    }
                }
            } while (!var_B4);
        } else {
            var_B3 = true;

            top_left = gbl.map_to_background_tile->map_screen_top_left;

            if (!combatmap_coord_on_screen(point_sub(target, top_left))) {
                combatmap_redraw_area(8, 3, target);
                top_left = gbl.map_to_background_tile->map_screen_top_left;
            }

            cur = point_mul(point_sub(target, top_left), 3);

            display_save_vid_ram();
            draw_overlay_bounded(gbl.missile_dax, 5, frame, cur.y, cur.x);

            if (delay > 0) {
                /* seg040.DrawOverlay() went here; it does nothing. */
                input_sys_delay(delay);

                display_restore_vid_ram();
            }
        }
    } while (!var_B3);

    /* seg040.DrawOverlay() went here; it does nothing. */
}

/* sub_6818A */
void character_magic_attack_display(const char *text, bool show_magic_stars,
                                    Player *player)
{
    int icon_id;
    int idx;
    int loops;
    Point pos;

    if (gbl.game_state != GAME_STATE_COMBAT) {
        character_display_status_string(true, 10, text, player);
        return;
    }

    /* Icon 0x16 is the ring of stars, 0x17 the plain flash. */
    icon_id = show_magic_stars ? 0x16 : 0x17;

    character_load_missile_icons(icon_id);

    if (!combatmap_player_on_screen_p(true, player)) {
        combatmap_redraw_area(8, 3, combatmap_player_map_pos(player));
    }

    sound_play(show_magic_stars ? SOUND_4 : SOUND_3);

    character_display_status_string(false, 10, text, player);

    idx = combatmap_player_index(player);
    pos = point_mul(gbl.combat_map[idx].screen_pos, 3);

    loops = show_magic_stars ? gbl.game_speed_var : 0;

    for (int loop = 0; loop <= loops; loop++) {
        for (int frame = 0; frame <= 3; frame++) {
            display_save_vid_ram();

            draw_overlay_bounded(gbl.missile_dax, 5, frame, pos.y, pos.x);
            /* seg040.DrawOverlay() went here; it does nothing. */

            input_sys_delay(70);

            display_restore_vid_ram();
        }
    }

    /* seg040.DrawOverlay() went here; it does nothing. */

    if (loops == 0) {
        text_game_delay();
    }
}

/* ------------------------------------------------------------- combat state */

void character_damage(int damage, Player *player)
{
    int neg_hp = 0;
    int new_hp = 0;

    if (player->hit_point_current >= damage) {
        new_hp = player->hit_point_current - damage;
    } else {
        neg_hp = damage - player->hit_point_current;
    }

    /* Ten points past zero kills, and so does anything that stops an animated
     * corpse. */
    if (neg_hp > 9 ||
        (new_hp == 0 && player->health_status == STATUS_ANIMATED)) {
        player->health_status = STATUS_DEAD;
    } else {
        if (neg_hp > 0) {
            player->health_status = STATUS_DYING;

            if (gbl.game_state == GAME_STATE_COMBAT) {
                player_actions(player)->bleeding = neg_hp;
            }
        } else if (new_hp == 0) {
            player->health_status = STATUS_UNCONSCIOUS;
        }
    }

    if (player->health_status == STATUS_OKEY ||
        player->health_status == STATUS_ANIMATED) {
        player->hit_point_current = (u8)new_hp;
    } else {
        player->in_combat = false;
        player->hit_point_current = 0;

        if (gbl.game_state == GAME_STATE_COMBAT) {
            if (player->combat_team == TEAM_OURS) {
                gbl.friends_count--;
            } else {
                gbl.foe_count--;
            }

            player_actions(player)->delay = 0;
        }
    }
}

/* sub_684F7 */
void character_describe_healing(Player *player)
{
    const char *text;

    if (player->hit_point_current == player->hit_point_max) {
        text = "is fully healed";
    } else {
        text = "is partially healed";
    }

    character_display_status_string(true, 10, text, player);

    if (gbl.game_state != GAME_STATE_COMBAT) {
        character_party_summary(gbl.selected_player);
    }
}

/* count_teams */
void character_count_combat_teams(void)
{
    gbl.friends_count = 0;
    gbl.foe_count = 0;

    for (int i = 0; i < gbl.team_count; i++) {
        const Player *player = gbl.team_list[i];

        if (player != NULL && player->in_combat) {
            if (player->combat_team == TEAM_OURS) {
                gbl.friends_count++;
            } else {
                gbl.foe_count++;
            }
        }
    }
}

static bool filter_other_team(const Player *player, void *ctx)
{
    const Player *self = ctx;

    return player->combat_team != self->combat_team;
}

/* near_enermy */
int character_build_near_targets(CombatPlayerIndex *out, int out_size,
                                 int max_range, Player *player)
{
    SortedCombatant scl[GBL_MAX_COMBATANT_COUNT];
    int found;
    int count = 0;

    if (out == NULL || out_size <= 0) {
        return 0;
    }

    found = target_sorted_combatants_for(scl, (int)COAB_ARRAY_LEN(scl), player,
                                        max_range, filter_other_team, player);

    for (int i = 0; i < found && count < out_size; i++) {
        out[count].player = scl[i].player;
        out[count].pos    = scl[i].pos;
        count++;
    }

    if (count < found) {
        log_warn("only %d of %d targets near %s fit the list",
                 count, found, player->name);
    }

    return count;
}

static bool filter_is_target(const Player *player, void *ctx)
{
    return player == (const Player *)ctx;
}

/* sub_68708 */
int character_target_range(Player *target, Player *attacker)
{
    SortedCombatant scl[GBL_MAX_COMBATANT_COUNT];
    int found;

    /* Walls do not matter to a range: the question is how far away the target
     * is, not whether it can be hit. */
    if (gbl.map_to_background_tile != NULL) {
        gbl.map_to_background_tile->ignore_walls = true;
    }

    found = target_sorted_combatants_for(scl, (int)COAB_ARRAY_LEN(scl), attacker,
                                        0xff, filter_is_target, target);

    if (gbl.map_to_background_tile != NULL) {
        gbl.map_to_background_tile->ignore_walls = false;
    }

    if (found > 0) {
        return scl[0].steps / 2;
    }

    /* No path at all. The C# was unsure what to answer here too. */
    return 0xff;
}

void character_clear_actions(Player *player)
{
    action_clear(player_actions(player));
}

void character_guarding(Player *player)
{
    Action *actions = player_actions(player);

    action_clear(actions);
    actions->guarding = true;

    character_print_message("Guarding");
}

/* sub_6886F */
int character_spell_max_target_count(int spell_id)
{
    const Player *caster = gbl.selected_player;
    const SpellEntry *entry;
    int target_count = 0;

    if (spell_id == 0) {
        return 0;
    }

    if (caster == NULL) {
        log_warn("no caster to count spell %d's targets for", spell_id);
        return 0;
    }

    entry = spell_entry(spell_id);

    if (entry == NULL) {
        return 0;
    }

    /* Anyone who cannot cast at all - a fighter using a wand, a low-level
     * paladin - gets the flat six. */
    if (caster->class_level[SKILL_CLERIC] == 0 &&
        caster->class_level[SKILL_MAGIC_USER] == 0 &&
        caster->class_level[SKILL_PALADIN] < 9 &&
        caster->class_level[SKILL_RANGER] < 8) {
        target_count = 6;
    } else {
        switch (entry->spell_class) {
        case SPELL_CLASS_CLERIC: {
            int cleric_count  = player_skill_level(caster, SKILL_CLERIC);
            int paladin_count = player_skill_level(caster, SKILL_PALADIN) - 8;

            target_count = COAB_MAX(cleric_count, paladin_count);
            break;
        }

        case SPELL_CLASS_DRUID: {
            int ranger_count = player_skill_level(caster, SKILL_RANGER) - 7;

            target_count = COAB_MAX(ranger_count, 0);
            break;
        }

        case SPELL_CLASS_MAGIC_USER: {
            int magicuser_count = player_skill_level(caster, SKILL_MAGIC_USER);
            int ranger_count    = player_skill_level(caster, SKILL_RANGER) - 8;

            target_count = COAB_MAX(magicuser_count, ranger_count);
            break;
        }

        case SPELL_CLASS_MONSTER:
            target_count = 12;
            break;

        default:
            break;
        }
    }

    /* A spell out of a scroll or a wand is cast at sixth level. */
    if (gbl.spell_from_item && entry->spell_class != SPELL_CLASS_MONSTER) {
        target_count = 6;
    }

    return target_count;
}

bool character_bandage(bool apply_bandage)
{
    bool someone_bleeding = false;

    for (int i = 0; i < gbl.team_count; i++) {
        Player *player = gbl.team_list[i];

        if (player == NULL) {
            continue;
        }

        if (!player_actions(player)->non_team_member &&
            player->combat_team == TEAM_OURS &&
            player->health_status == STATUS_DYING) {
            someone_bleeding = true;

            /* Only the first one found is seen to; the caller comes back for
             * the rest. */
            if (apply_bandage) {
                player->health_status = STATUS_UNCONSCIOUS;
                player_actions(player)->bleeding = 0;

                character_display_status_string(true, 10, "is bandaged", player);

                apply_bandage = false;
            }
        }
    }

    return someone_bleeding;
}
