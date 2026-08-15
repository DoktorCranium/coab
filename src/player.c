/* player.c - Ported from Classes/Player.cs. */
#include <string.h>

#include "player.h"

#include "cheats.h"
#include "log.h"

/* --------------------------------------------------------------- StatValue */

void stat_value_load(StatValue *sv, int val)
{
    sv->full = sv->cur = val;
}

void stat_value_inc(StatValue *sv)
{
    sv->full += 1;
    sv->cur  += 1;
}

void stat_value_dec(StatValue *sv)
{
    sv->full -= 1;
    sv->cur  -= 1;
}

void player_stats_clear(PlayerStats *ps)
{
    memset(ps, 0, sizeof(*ps));
}

void player_stats_assign(PlayerStats *dst, const PlayerStats *src)
{
    *dst = *src;
}

/* The C#'s Inc(idx)/Dec(idx) switched on 0..5 and threw for anything else; Str00
 * has no index of its own because the stat screens never step through it. */
void player_stats_inc(PlayerStats *ps, int stat_index)
{
    if (stat_index < 0 || stat_index > STAT_CHA) {
        log_warn("stats: no stat %d to raise", stat_index);
        return;
    }

    stat_value_inc(&ps->value[stat_index]);
}

void player_stats_dec(PlayerStats *ps, int stat_index)
{
    if (stat_index < 0 || stat_index > STAT_CHA) {
        log_warn("stats: no stat %d to lower", stat_index);
        return;
    }

    stat_value_dec(&ps->value[stat_index]);
}

/* The C# hung these three off StatValue itself, each instance carrying the three
 * table rows for the stat it was. A StatValue here is two ints and nothing else,
 * so the stat has to be named: `which` picks the row. The whole-record versions
 * below are the same calls seven times over, which is what the C# callers that
 * wanted every stat wrote out by hand.
 *
 * engine/ovr018.cs's createPlayer and modifyPlayer need the single-stat form:
 * they enforce one stat at a time and then look at what came out before touching
 * the next, so applying all seven at once would change the answer. */
void stat_value_enforce_race_sex(StatValue *sv, PlayerStatId which, int race,
                                 int sex)
{
    if (which < 0 || which >= PSTAT_COUNT) {
        log_warn("stat limits: stat %d out of range", (int)which);
        return;
    }

    if (!race_valid(race) || !sex_valid(sex)) {
        log_warn("stat limits: race %d sex %d out of range, leaving stats alone",
                 race, sex);
        return;
    }

    /* Max first, then min, which is why the monster row's inverted Con pair
     * resolves to the minimum. */
    sv->full = COAB_MIN(limits_race_sex_min_max[which][race][1][sex], sv->full);
    sv->full = COAB_MAX(limits_race_sex_min_max[which][race][0][sex], sv->full);
    sv->cur  = sv->full;
}

void stat_value_enforce_class(StatValue *sv, PlayerStatId which, ClassId cls)
{
    if (which < 0 || which >= PSTAT_COUNT) {
        log_warn("stat limits: stat %d out of range", (int)which);
        return;
    }

    if (!class_valid((int)cls)) {
        log_warn("stat limits: class %d out of range, leaving stats alone",
                 (int)cls);
        return;
    }

    sv->full = COAB_MAX(limits_class_min[which][cls], sv->full);
    sv->cur  = sv->full;
}

void stat_value_age_effects(StatValue *sv, PlayerStatId which, int race, int age)
{
    if (which < 0 || which >= PSTAT_COUNT) {
        log_warn("ageing: stat %d out of range", (int)which);
        return;
    }

    if (!race_valid(race)) {
        log_warn("ageing: race %d out of range, leaving stats alone", race);
        return;
    }

    for (int i = 0; i < AGE_BRACKETS; i++) {
        if (limits_race_age_brackets[race][i] < age) {
            sv->full += limits_age_effect[which][i];
        }
    }
}

void player_stats_enforce_race_sex(PlayerStats *ps, int race, int sex)
{
    for (int s = 0; s < PSTAT_COUNT; s++) {
        stat_value_enforce_race_sex(&ps->value[s], (PlayerStatId)s, race, sex);
    }
}

void player_stats_enforce_class(PlayerStats *ps, ClassId cls)
{
    for (int s = 0; s < PSTAT_COUNT; s++) {
        stat_value_enforce_class(&ps->value[s], (PlayerStatId)s, cls);
    }
}

void player_stats_age_effects(PlayerStats *ps, int race, int age)
{
    for (int s = 0; s < PSTAT_COUNT; s++) {
        stat_value_age_effects(&ps->value[s], (PlayerStatId)s, race, age);
    }
}

/* 14 bytes: seven { cur, full } pairs in PlayerStatId order, which is the order
 * PlayerStats.Read/Write used. The C#'s [DataOffset] claimed 0x12 bytes, which
 * would have run four bytes into the spell list at 0x1e; only 14 were ever
 * touched, so the claim was harmless and 14 is the real size. */
void player_stats_dio_read(void *member, const u8 *data, size_t offset)
{
    PlayerStats *ps = (PlayerStats *)member;

    for (int s = 0; s < PSTAT_COUNT; s++) {
        /* Clamped: a stat above 25 would index off the end of the bonus
         * tables. */
        ps->value[s].cur  = COAB_MIN((int)data[offset + (size_t)s * 2 + 0], 25);
        ps->value[s].full = COAB_MIN((int)data[offset + (size_t)s * 2 + 1], 25);
    }
}

void player_stats_dio_write(const void *member, u8 *data, size_t offset)
{
    const PlayerStats *ps = (const PlayerStats *)member;

    for (int s = 0; s < PSTAT_COUNT; s++) {
        data[offset + (size_t)s * 2 + 0] = (u8)ps->value[s].cur;
        data[offset + (size_t)s * 2 + 1] = (u8)ps->value[s].full;
    }
}

/* ------------------------------------------------------------- the record */

/* dio_read and dio_write have already checked that the field's whole span is
 * inside the buffer, so bounding these at the end of the field is exact. */
static void spell_list_dio_read(void *member, const u8 *data, size_t offset)
{
    spell_list_load((SpellList *)member, data,
                    offset + SPELL_LIST_RECORD_SIZE, offset);
}

static void spell_list_dio_write(const void *member, u8 *data, size_t offset)
{
    spell_list_save((const SpellList *)member, data,
                    offset + SPELL_LIST_RECORD_SIZE, offset);
}

static const DioField player_fields[] = {
    DIO_F(Player, name, 0x00, DIO_PSTRING, PLAYER_NAME_MAX),
    DIO_CUSTOM_F(Player, stats, 0x10, PLAYER_STATS_RECORD_SIZE,
                 player_stats_dio_read, player_stats_dio_write),
    DIO_CUSTOM_F(Player, spell_list, 0x1e, SPELL_LIST_RECORD_SIZE,
                 spell_list_dio_read, spell_list_dio_write),
    DIO_F(Player, spell_to_learn_count,    0x72,  DIO_BYTE,       0),
    DIO_F(Player, thac0,                   0x73,  DIO_SBYTE,      0),
    DIO_F(Player, race,                    0x74,  DIO_IBYTE,      0),
    DIO_F(Player, cls,                     0x75,  DIO_IBYTE,      0),
    DIO_F(Player, age,                     0x76,  DIO_SWORD,      0),
    DIO_F(Player, hit_point_max,           0x78,  DIO_BYTE,       0),
    DIO_F(Player, spell_book,              0x79,  DIO_BYTE_ARRAY, SPELL_BOOK_SIZE),
    DIO_F(Player, attack_level,            0xdd,  DIO_BYTE,       0),
    DIO_F(Player, field_DE,                0xde,  DIO_BYTE,       0),
    DIO_F(Player, save_verse,              0xdf,  DIO_BYTE_ARRAY, SAVE_VERSE_COUNT),
    DIO_F(Player, base_movement,           0xe4,  DIO_BYTE,       0),
    DIO_F(Player, hit_dice,                0xe5,  DIO_BYTE,       0),
    DIO_F(Player, multiclass_level,        0xe6,  DIO_BYTE,       0),
    DIO_F(Player, lost_lvls,               0xe7,  DIO_BYTE,       0),
    DIO_F(Player, lost_hp,                 0xe8,  DIO_BYTE,       0),
    DIO_F(Player, field_E9,                0xe9,  DIO_BYTE,       0),
    DIO_F(Player, thief_skills,            0xea,  DIO_BYTE_ARRAY, 8),
    /* 0xf2..0xf5 was the affect list pointer. */
    DIO_F(Player, field_F6,                0xf6,  DIO_BYTE,       0),
    DIO_F(Player, control_morale,          0xf7,  DIO_BYTE,       0),
    DIO_F(Player, npc_treasure_share_count, 0xf8, DIO_BYTE,       0),
    DIO_F(Player, field_F9,                0xf9,  DIO_BYTE,       0),
    DIO_F(Player, field_FA,                0xfa,  DIO_BYTE,       0),
    DIO_CUSTOM_F(Player, money, 0xfb, MONEY_RECORD_SIZE,
                 money_dio_read, money_dio_write),
    DIO_F(Player, class_level,              0x109, DIO_BYTE_ARRAY, SKILL_COUNT),
    DIO_F(Player, class_level_old,          0x111, DIO_BYTE_ARRAY, SKILL_COUNT),
    DIO_F(Player, sex,                      0x119, DIO_BYTE,       0),
    DIO_F(Player, monster_type,             0x11a, DIO_IBYTE,      0),
    DIO_F(Player, alignment,                0x11b, DIO_BYTE,       0),
    DIO_F(Player, attacks_count,            0x11c, DIO_BYTE,       0),
    DIO_F(Player, base_half_moves,          0x11d, DIO_BYTE,       0),
    DIO_F(Player, attack1_dice_count_base,  0x11e, DIO_BYTE,       0),
    DIO_F(Player, attack2_dice_count_base,  0x11f, DIO_BYTE,       0),
    DIO_F(Player, attack1_dice_size_base,   0x120, DIO_BYTE,       0),
    DIO_F(Player, attack2_dice_size_base,   0x121, DIO_BYTE,       0),
    DIO_F(Player, attack1_damage_bonus_base, 0x122, DIO_BYTE,      0),
    DIO_F(Player, attack2_damage_bonus_base, 0x123, DIO_BYTE,      0),
    DIO_F(Player, base_ac,                  0x124, DIO_BYTE,       0),
    DIO_F(Player, field_125,                0x125, DIO_BYTE,       0),
    DIO_F(Player, mod_id,                   0x126, DIO_BYTE,       0),
    DIO_F(Player, exp,                      0x127, DIO_INT,        0),
    DIO_F(Player, class_flags,              0x12b, DIO_BYTE,       0),
    DIO_F(Player, hit_point_rolled,         0x12c, DIO_BYTE,       0),
    /* The three rows of five are contiguous, so one 15-byte copy does it.
     *
     * The C# indexed this as data[0x12d + j + (i * i)], which overlaps rows 0
     * and 1 and starts row 2 four bytes in, and it left the record offset out of
     * the read entirely so every character but the first read another's bytes.
     * The stride is 5: field_13C begins at 0x12d + 15, exactly where a 3x5 grid
     * ends. */
    DIO_F(Player, spell_cast_count,         0x12d, DIO_BYTE_ARRAY, 15),
    DIO_F(Player, field_13C,                0x13c, DIO_SWORD,      0),
    DIO_F(Player, field_13E,                0x13e, DIO_BYTE,       0),
    DIO_F(Player, field_13F,                0x13f, DIO_BYTE,       0),
    DIO_F(Player, field_140,                0x140, DIO_BYTE,       0),
    DIO_F(Player, head_icon,                0x141, DIO_BYTE,       0),
    DIO_F(Player, weapon_icon,              0x142, DIO_BYTE,       0),
    DIO_F(Player, icon_id,                  0x143, DIO_BYTE,       0),
    DIO_F(Player, icon_size,                0x144, DIO_BYTE,       0),
    DIO_F(Player, icon_colours,             0x145, DIO_BYTE_ARRAY, 6),
    DIO_F(Player, field_14B,                0x14b, DIO_BYTE,       0),
    /* 0x14c was the item count and 0x14d..0x150 the item list pointer;
     * 0x151..0x184 were the thirteen readied-item pointers. */
    DIO_F(Player, weapons_hands_used,       0x185, DIO_BYTE,       0),
    DIO_F(Player, field_186,                0x186, DIO_SBYTE,      0),
    DIO_F(Player, weight,                   0x187, DIO_SWORD,      0),
    /* 0x189..0x18c is unaccounted for; 0x18d..0x190 was the action pointer. */
    DIO_F(Player, paladin_cures_left,       0x191, DIO_BYTE,       0),
    DIO_F(Player, field_192,                0x192, DIO_BYTE,       0),
    DIO_F(Player, field_193,                0x193, DIO_BYTE,       0),
    DIO_F(Player, field_194,                0x194, DIO_BYTE,       0),
    DIO_F(Player, health_status,            0x195, DIO_IBYTE,      0),
    DIO_F(Player, in_combat,                0x196, DIO_BOOL,       0),
    DIO_F(Player, combat_team,              0x197, DIO_IBYTE,      0),
    DIO_F(Player, quick_fight,              0x198, DIO_IBYTE,      0),
    DIO_F(Player, hit_bonus,                0x199, DIO_IBYTE,      0),
    DIO_F(Player, ac,                       0x19a, DIO_BYTE,       0),
    DIO_F(Player, ac_behind,                0x19b, DIO_BYTE,       0),
    DIO_F(Player, attack1_attacks_left,     0x19c, DIO_BYTE,       0),
    DIO_F(Player, attack2_attacks_left,     0x19d, DIO_BYTE,       0),
    DIO_F(Player, attack1_dice_count,       0x19e, DIO_BYTE,       0),
    DIO_F(Player, attack2_dice_count,       0x19f, DIO_BYTE,       0),
    DIO_F(Player, attack1_dice_size,        0x1a0, DIO_BYTE,       0),
    DIO_F(Player, attack2_dice_size,        0x1a1, DIO_BYTE,       0),
    DIO_F(Player, attack1_damage_bonus,     0x1a2, DIO_SBYTE,      0),
    DIO_F(Player, attack2_damage_bonus,     0x1a3, DIO_BYTE,       0),
    DIO_F(Player, hit_point_current,        0x1a4, DIO_BYTE,       0),
    DIO_F(Player, movement,                 0x1a5, DIO_BYTE,       0),
    DIO_END_MARKER
};

const DioDesc player_desc = { "Player", PLAYER_RECORD_SIZE, player_fields };

void player_init(Player *p)
{
    memset(p, 0, sizeof(*p));
    player_ready_reset(p);
}

bool player_read(Player *p, const u8 *data, size_t data_size, size_t offset)
{
    player_init(p);
    return dio_read(&player_desc, p, data, data_size, offset);
}

bool player_write(const Player *p, u8 *data, size_t data_size)
{
    if (data_size < PLAYER_RECORD_SIZE) {
        return false;
    }
    memset(data, 0, PLAYER_RECORD_SIZE);
    return dio_write(&player_desc, p, data, data_size);
}

/* ---------------------------------------------------------------- spells */

bool player_knows_spell(const Player *p, Spells spell)
{
    int index = (int)spell - 1;

    if (index < 0 || index >= SPELL_BOOK_SIZE) {
        log_warn("spell %d has no spell book entry", (int)spell);
        return false;
    }
    return p->spell_book[index] != 0;
}

void player_learn_spell(Player *p, Spells spell)
{
    int index = (int)spell - 1;

    if (index < 0 || index >= SPELL_BOOK_SIZE) {
        log_warn("cannot learn spell %d: no spell book entry", (int)spell);
        return;
    }
    p->spell_book[index] = 1;
}

/* ---------------------------------------------------------------- levels */

/* engine/ovr026.cs: HumanCurrentClassLevel_Zero, hasAnySkills. The level in the
 * character's original class, before dual-classing. */
int player_dual_class_current_level(const Player *p)
{
    int i = 0;

    if (p->race != RACE_HUMAN) {
        return 0;
    }

    /* Stops at 7 without testing it, so an all-zero table reads the monk
     * entry. That is in range, and it is what the original does. */
    while (i < 7 && p->class_level[i] == 0) {
        i++;
    }

    return p->class_level[i];
}

/* engine/ovr026.cs: DualClassExceedLastLevel, sub_6B3D1. 1 once the new class has
 * out-levelled the old one, else 0. Used as a multiplier below, so the old
 * class's levels only count from then on. */
int player_dual_class_exceeded(const Player *p)
{
    return player_dual_class_current_level(p) > p->multiclass_level ? 1 : 0;
}

int player_skill_level(const Player *p, SkillType skill)
{
    if (!skill_valid((int)skill)) {
        log_warn("skill %d out of range", (int)skill);
        return 0;
    }

    return p->class_level[skill] +
           p->class_level_old[skill] * player_dual_class_exceeded(p);
}

bool player_can_duel_class(const Player *p)
{
    if (p->race != RACE_HUMAN) {
        return false;
    }

    for (int cls = CLASS_CLERIC; cls <= CLASS_MONK; cls++) {
        if (p->class_level_old[cls] > 0) {
            return false;
        }
    }

    return true;
}

/* ---------------------------------------------------------------- combat */

/* The C# accessors threw on anything but 1 or 2. Nothing calls them with
 * anything else, but in C a bad index has to answer something. */
static bool attack_index_valid(int index, const char *what)
{
    if (index == 1 || index == 2) {
        return true;
    }
    log_warn("attack %d %s: only attacks 1 and 2 exist", index, what);
    return false;
}

u8 player_attacks_left(const Player *p, int index)
{
    if (!attack_index_valid(index, "attacks left")) {
        return 0;
    }
    return index == 1 ? p->attack1_attacks_left : p->attack2_attacks_left;
}

void player_attacks_left_set(Player *p, int index, u8 value)
{
    if (!attack_index_valid(index, "attacks left")) {
        return;
    }
    if (index == 1) {
        p->attack1_attacks_left = value;
    } else {
        p->attack2_attacks_left = value;
    }
}

void player_attacks_left_dec(Player *p, int index)
{
    if (!attack_index_valid(index, "attacks left")) {
        return;
    }
    if (index == 1) {
        p->attack1_attacks_left -= 1;
    } else {
        p->attack2_attacks_left -= 1;
    }
}

u8 player_attack_dice_count(const Player *p, int index)
{
    if (!attack_index_valid(index, "dice count")) {
        return 0;
    }
    return index == 1 ? p->attack1_dice_count : p->attack2_dice_count;
}

u8 player_attack_dice_size(const Player *p, int index)
{
    if (!attack_index_valid(index, "dice size")) {
        return 0;
    }
    return index == 1 ? p->attack1_dice_size : p->attack2_dice_size;
}

/* Signed, unlike the C# accessor: that one cast attack1_DamageBonus to byte, so
 * a cursed weapon's -1 came back as 255 and ovr014 added it to the damage roll.
 * The field is signed on disk and the DOS code sign-extended it. */
int player_attack_damage_bonus(const Player *p, int index)
{
    if (!attack_index_valid(index, "damage bonus")) {
        return 0;
    }
    return index == 1 ? p->attack1_damage_bonus : (int)p->attack2_damage_bonus;
}

int player_display_ac(const Player *p)
{
    return 0x3c - p->ac;
}

CombatTeam player_opposite_team(const Player *p)
{
    return p->combat_team == TEAM_OURS ? TEAM_ENEMY
                                              : TEAM_OURS;
}

bool player_has_affect(const Player *p, Affects type)
{
    return affect_list_has(&p->affects, type);
}

bool player_is_held(const Player *p)
{
    return affect_list_is_held(&p->affects);
}

void player_add_weight(Player *p, int amount)
{
    p->weight = (i16)(p->weight + amount);
}

void player_remove_weight(Player *p, int amount)
{
    p->weight = (i16)(p->weight - amount);
}

/* ----------------------------------------------------------------- items */

int player_item_add(Player *p, const Item *it)
{
    if (p->item_count >= PLAYER_MAX_ITEMS) {
        return -1;
    }
    p->items[p->item_count] = *it;

    return p->item_count++;
}

bool player_item_remove(Player *p, int index)
{
    if (index < 0 || index >= p->item_count) {
        log_warn("cannot remove item %d: %s carries %d",
                 index, p->name, p->item_count);
        return false;
    }

    memmove(&p->items[index], &p->items[index + 1],
            (size_t)(p->item_count - index - 1) * sizeof(p->items[0]));
    p->item_count--;
    memset(&p->items[p->item_count], 0, sizeof(p->items[0]));

    /* Ready slots hold indices, so the ones above the hole shift down with it
     * and the one pointing into the hole empties. */
    for (int slot = 0; slot < ITEM_SLOT_COUNT; slot++) {
        if (p->ready[slot] == index) {
            p->ready[slot] = ITEM_SLOT_NONE;
        } else if (p->ready[slot] > index) {
            p->ready[slot] = (i8)(p->ready[slot] - 1);
        }
    }

    return true;
}

Item *player_item_at(Player *p, int index)
{
    if (index < 0 || index >= p->item_count) {
        return NULL;
    }
    return &p->items[index];
}

static bool ready_slot_valid(ItemSlot slot)
{
    if ((int)slot >= 0 && (int)slot < ITEM_SLOT_COUNT) {
        return true;
    }
    /* ItemSlot runs to 13 in the item table but only 0..12 have a ready slot;
     * slot 13 is one of the scroll slots, which are never readied. */
    log_warn("ready slot %d out of range", (int)slot);
    return false;
}

Item *player_ready_item(Player *p, ItemSlot slot)
{
    if (!ready_slot_valid(slot)) {
        return NULL;
    }
    return player_item_at(p, p->ready[slot]);
}

void player_ready_set(Player *p, ItemSlot slot, int item_index)
{
    if (!ready_slot_valid(slot)) {
        return;
    }
    if (item_index != ITEM_SLOT_NONE &&
        (item_index < 0 || item_index >= p->item_count)) {
        log_warn("cannot ready item %d in slot %d: %s carries %d",
                 item_index, (int)slot, p->name, p->item_count);
        return;
    }
    p->ready[slot] = (i8)item_index;
}

void player_ready_reset(Player *p)
{
    for (int slot = 0; slot < ITEM_SLOT_COUNT; slot++) {
        p->ready[slot] = ITEM_SLOT_NONE;
    }
}

Item *player_primary_weapon(Player *p)
{
    return player_ready_item(p, (ItemSlot)0);
}

Item *player_secondary_weapon(Player *p)
{
    return player_ready_item(p, (ItemSlot)1);
}

Item *player_armor(Player *p)
{
    return player_ready_item(p, ITEM_SLOT_ARMOR);
}

Item *player_arrows(Player *p)
{
    return player_ready_item(p, (ItemSlot)11);
}

Item *player_quarrels(Player *p)
{
    return player_ready_item(p, ITEM_SLOT_QUARREL);
}

u8 player_primary_weapon_hand_count(Player *p)
{
    const Item *it = player_primary_weapon(p);

    return it != NULL ? item_hands_count(it) : 0;
}

u8 player_secondary_weapon_hand_count(Player *p)
{
    const Item *it = player_secondary_weapon(p);

    return it != NULL ? item_hands_count(it) : 0;
}

void player_undready_all(Player *p, int class_flags)
{
    for (int slot = 0; slot < ITEM_SLOT_COUNT; slot++) {
        Item *it = player_ready_item(p, (ItemSlot)slot);

        if (it != NULL &&
            (item_data(it->type)->class_flags & class_flags) == 0 &&
            !it->cursed) {
            it->readied = false;
        }
    }
}

/* engine/ovr020.cs: statusString */
const char *player_health_status_name(int status)
{
    static const char *const names[] = {
        "Okay", "Animated", "tempgone", "Running",
        "Unconscious", "Dying", "Dead", "Stoned",
        "Gone"
    };

    if (status < 0 || (size_t)status >= COAB_ARRAY_LEN(names)) {
        log_warn("player: health status %d has no name", status);
        return "";
    }
    return names[status];
}

/* engine/ovr020.cs: classString. Indexed by ClassId, so the nine multi-class
 * combinations follow the eight single classes. CLASS_UNKNOWN has no name. */
const char *player_class_name(int cls)
{
    static const char *const names[CLASS_COUNT] = {
        "Cleric", "Druid", "Fighter", "Paladin", "Ranger",
        "Magic-User", "Thief", "Monk", "Cleric/Fighter",
        "Cleric/Fighter/Magic-User", "Cleric/Ranger",
        "Cleric/Magic-User", "Cleric/Thief", "Fighter/Magic-User",
        "Fighter/Thief", "Fighter/Magic-User/Thief",
        "Magic-User/Thief"
    };

    if (!class_valid(cls)) {
        log_warn("player: class %d has no name", cls);
        return "";
    }
    return names[cls];
}
