/* spells.c - the spell casting table, transcribed from the array at
 * Classes/Gbl.cs:567 (seg600:37DC asc_19AEC).
 *
 * The short names below are only so a row still fits on one line and can be
 * checked against the C# side by side; they are undefined again at the end of
 * the table so nothing else picks them up.
 */
#include "spells.h"

#include "log.h"

#define CL   SPELL_CLASS_CLERIC
#define DR   SPELL_CLASS_DRUID
#define MU   SPELL_CLASS_MAGIC_USER
#define MO   SPELL_CLASS_MONSTER
#define U10  SPELL_CLASS_UNKNOWN10

#define T_FIGHT  SPELL_TARGET_COMBAT
#define T_SELF   SPELL_TARGET_SELF
#define T_ONE    SPELL_TARGET_PARTY_MEMBER
#define T_ALL    SPELL_TARGET_WHOLE_PARTY

#define D_NORM   DAMAGE_ON_SAVE_NORMAL
#define D_ZERO   DAMAGE_ON_SAVE_ZERO
#define D_HALF   DAMAGE_ON_SAVE_HALF
#define D_3      DAMAGE_ON_SAVE_UNKNOWN_3
#define D_1E     DAMAGE_ON_SAVE_UNKNOWN_1E

#define V_SPELL  SAVE_VERSE_SPELL
#define V_POISN  SAVE_VERSE_POISON

#define W_CAMP   SPELL_WHEN_CAMP
#define W_FIGHT  SPELL_WHEN_COMBAT
#define W_BOTH   SPELL_WHEN_BOTH

/* id, class, lvl, fixedRange, perLvlRange, fixedDur, perLvlDur, field_6,
 * targets, damageOnSave, saveVerse, affect, whenCast, delay, priority,
 * field_E, field_F */
const SpellEntry spell_casting_table[SPELL_CASTING_TABLE_COUNT] = {
    /* 0x00 - spell ids start at 1; the C# had a null here. */
    { 0, CL, 0, 0, 0, 0, 0, 0, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE, W_CAMP, 0, 0, 0, 0 },

    { 0x01, CL,  1,  6, 0,  6,  0,  10, T_ALL,   D_NORM, V_SPELL, AFFECT_BLESS,                     W_BOTH,   10, 1, 0, 0 },
    { 0x02, CL,  1,  6, 0,  6,  0,  10, T_FIGHT, D_NORM, V_SPELL, AFFECT_CURSED,                    W_FIGHT,  10, 3, 1, 0 },
    { 0x03, CL,  1,  0, 0,  0,  0,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    5, 1, 0, 0 },
    { 0x04, CL,  1, -1, 0,  0,  0,   4, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_FIGHT,   5, 2, 1, 0 },
    { 0x05, CL,  1,  3, 0, 10,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_DETECT_MAGIC,              W_BOTH,    1, 0, 0, 0 },
    { 0x06, CL,  1,  0, 0,  0,  3,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_PROTECTION_FROM_EVIL,      W_BOTH,    4, 1, 0, 0 },
    { 0x07, CL,  1,  0, 0,  0,  3,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_PROTECTION_FROM_GOOD,      W_BOTH,    4, 1, 0, 0 },
    { 0x08, CL,  1,  0, 0,  0, 10,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_RESIST_COLD,               W_BOTH,   10, 0, 0, 0 },
    { 0x09, MU,  1,  0, 0,  0,  0,   4, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_FIGHT,   1, 2, 1, 0 },
    { 0x0a, MU,  1, 12, 0,  0,  0,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_CHARM_PERSON,              W_FIGHT,   1, 4, 1, 0 },
    { 0x0b, MU,  1,  0, 0,  0,  2,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_DETECT_MAGIC,              W_BOTH,    1, 0, 0, 0 },
    { 0x0c, MU,  1,  0, 2,  0, 10,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_ENLARGE,                   W_BOTH,    1, 0, 0, 0 },
    { 0x0d, MU,  1,  0, 2,  0, 10,   4, T_ONE,   D_ZERO, V_SPELL, AFFECT_REDUCE,                    W_BOTH,    1, 0, 1, 0 },
    { 0x0e, MU,  1,  0, 0,  0,  1,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_FRIENDS,                   W_CAMP,    1, 0, 0, 0 },
    { 0x0f, MU,  1,  6, 4,  0,  0,   4, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_FIGHT,   1, 4, 1, 0 },
    { 0x10, MU,  1,  0, 0,  0,  2,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_PROTECTION_FROM_EVIL,      W_BOTH,    1, 1, 0, 0 },
    { 0x11, MU,  1,  0, 0,  0,  2,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_PROTECTION_FROM_GOOD,      W_BOTH,    1, 1, 0, 0 },
    { 0x12, MU,  1,  0, 0,  0,  2,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_READ_MAGIC,                W_CAMP,   10, 0, 0, 0 },
    { 0x13, MU,  1,  0, 0,  0,  5,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_SHIELD,                    W_BOTH,    1, 2, 0, 0 },
    { 0x14, MU,  1, -1, 0,  0,  0,   4, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_FIGHT,   1, 2, 1, 0 },
    { 0x15, MU,  1,  3, 4,  0,  5,   9, T_FIGHT, D_NORM, V_SPELL, AFFECT_SLEEP,                     W_FIGHT,   1, 2, 1, 1 },
    { 0x16, CL,  2,  0, 0, 30,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_FIND_TRAPS,                W_CAMP,    5, 0, 0, 0 },
    { 0x17, CL,  2,  6, 0,  4,  1,   6, T_FIGHT, D_ZERO, V_SPELL, AFFECT_PARALYZE,                  W_FIGHT,   5, 6, 1, 0 },
    { 0x18, CL,  2,  0, 0,  0, 10,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_RESIST_FIRE,               W_BOTH,    5, 1, 0, 0 },
    { 0x19, CL,  2, 12, 0,  0,  2,  31, T_FIGHT, D_3,    V_SPELL, AFFECT_SILENCE_15_RADIUS,         W_FIGHT,   5, 4, 1, 1 },
    { 0x1a, CL,  2,  0, 0,  0, 60,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_SLOW_POISON,               W_BOTH,    1, 0, 0, 0 },
    { 0x1b, CL,  2,  3, 0,  0,  0, 240, T_FIGHT, D_NORM, V_SPELL, AFFECT_SNAKE_CHARM,               W_FIGHT,   5, 0, 1, 0 },
    { 0x1c, CL,  2,  3, 0,  0,  1,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_SPIRITUAL_HAMMER,          W_BOTH,    5, 1, 0, 0 },
    { 0x1d, MU,  2,  0, 4,  0,  5,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_DETECT_INVISIBILITY,       W_BOTH,    2, 1, 0, 0 },
    { 0x1e, MU,  2,  0, 0,  0,  0,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_INVISIBILITY,              W_BOTH,    2, 2, 0, 0 },
    { 0x1f, MU,  2,  0, 0,  0,  0,   0, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_CAMP,    1, 0, 0, 0 },
    { 0x20, MU,  2,  0, 0,  0,  2,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_MIRROR_IMAGE,              W_BOTH,    2, 3, 0, 0 },
    { 0x21, MU,  2,  1, 1,  0,  1,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_RAY_OF_ENFEEBLEMENT,       W_FIGHT,   2, 2, 1, 0 },
    { 0x22, MU,  2,  3, 0,  0,  1,   9, T_FIGHT, D_3,    V_POISN, AFFECT_STINKING_CLOUD,            W_FIGHT,   2, 5, 1, 1 },
    { 0x23, MU,  2,  0, 0,  0, 60,   0, T_ONE,   D_NORM, V_SPELL, AFFECT_STRENGTH,                  W_CAMP,   10, 0, 0, 0 },
    { 0x24, MO,  7,  4, 0,  0,  0,   8, T_ALL,   D_ZERO, V_SPELL, AFFECT_NONE,                      W_BOTH,    0, 2, 0, 0 },
    { 0x25, CL,  3,  0, 0,  0,  0,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,   10, 0, 0, 0 },
    { 0x26, CL,  3, -1, 0,  0,  0,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_BLINDED,                   W_FIGHT,  10, 3, 1, 0 },
    { 0x27, CL,  3,  0, 0,  0,  0,   0, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_CAMP,  100, 0, 0, 0 },
    { 0x28, CL,  3, -1, 0,  0,  0,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_CAUSE_DISEASE_1,           W_FIGHT, 100, 4, 1, 0 },
    { 0x29, CL,  3,  6, 0,  0,  0,   9, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    4, 3, 1, 1 },
    { 0x2a, CL,  3,  0, 0,  0,  1,   0, T_ALL,   D_NORM, V_SPELL, AFFECT_PRAYER,                    W_BOTH,    6, 5, 0, 0 },
    { 0x2b, CL,  3,  0, 0,  0,  0,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    6, 0, 0, 0 },
    { 0x2c, CL,  3, -1, 0,  0, 10,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_BESTOW_CURSE,              W_FIGHT,   6, 5, 1, 0 },
    { 0x2d, MU,  3,  0, 0,  0,  1,   0, T_FIGHT, D_NORM, V_SPELL, AFFECT_BLINK,                     W_FIGHT,   1, 2, 0, 0 },
    { 0x2e, MU,  3, 12, 0,  0,  1,   9, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    3, 2, 1, 1 },
    { 0x2f, MU,  3, 10, 1,  0,  0,  11, T_FIGHT, D_HALF, V_SPELL, AFFECT_NONE,                      W_FIGHT,   3, 7, 1, 3 },
    { 0x30, MU,  3,  6, 0,  3,  1,  10, T_ALL,   D_NORM, V_SPELL, AFFECT_HASTE,                     W_BOTH,    3, 3, 0, 0 },
    { 0x31, MU,  3, 12, 0,  0,  2,   7, T_FIGHT, D_ZERO, V_SPELL, AFFECT_PARALYZE,                  W_FIGHT,   3, 6, 1, 0 },
    { 0x32, MU,  3,  0, 0,  0,  0,   9, T_ALL,   D_NORM, V_SPELL, AFFECT_INVISIBILITY,              W_BOTH,    3, 1, 0, 0 },
    { 0x33, MU,  3,  4, 1,  0,  0,   8, T_FIGHT, D_HALF, V_SPELL, AFFECT_NONE,                      W_FIGHT,   3, 6, 1, 0 },
    { 0x34, MU,  3,  0, 0,  0,  2,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_PROT_FROM_EVIL_10_RADIUS,  W_BOTH,    3, 1, 0, 0 },
    { 0x35, MU,  3,  0, 0,  0,  2,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_PROT_FROM_GOOD_10_RADIUS,  W_BOTH,    3, 2, 0, 0 },
    { 0x36, MU,  3,  0, 0,  0, 10,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_PROT_FROM_NORMAL_MISSILES, W_BOTH,    3, 3, 0, 0 },
    { 0x37, MU,  3,  9, 1,  3,  1,  10, T_FIGHT, D_NORM, V_SPELL, AFFECT_SLOW,                      W_FIGHT,   3, 4, 1, 0 },
    { 0x38, CL,  7,  0, 0,  0,  0,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    6, 0, 0, 0 },
    { 0x39, MO,  6,  0, 0,  0,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_HASTE,                     W_BOTH,    0, 3, 0, 0 },
    { 0x3a, CL,  4,  0, 0,  0,  0,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    7, 1, 0, 0 },
    { 0x3b, MO,  6,  0, 0,  0,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_STRENGTH,                  W_BOTH,    0, 1, 0, 0 },
    { 0x3c, MO,  6,  4, 4,  0,  0,   4, T_FIGHT, D_HALF, V_SPELL, AFFECT_NONE,                      W_FIGHT,   0, 7, 1, 0 },
    { 0x3d, MO,  6,  6, 0,  0,  0,   4, T_FIGHT, D_ZERO, V_POISN, AFFECT_PARALYZE,                  W_FIGHT,   0, 7, 1, 0 },
    { 0x3e, MO,  6,  0, 0,  0,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_HASTE,                     W_BOTH,    0, 1, 0, 0 },
    { 0x3f, MO,  6,  0, 0,  0,  0,   7, T_ALL,   D_NORM, V_SPELL, AFFECT_INVISIBLE,                 W_BOTH,    0, 2, 0, 0 },
    { 0x40, MO,  6,  7, 0,  0,  0,  11, T_FIGHT, D_HALF, V_SPELL, AFFECT_NONE,                      W_FIGHT,   0, 7, 1, 3 },
    { 0x41, MO,  6, 12, 0,  0,  0,   4, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_FIGHT,   0, 6, 1, 0 },
    { 0x42, CL,  4,  0, 0,  0,  0,   4, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_FIGHT,   7, 5, 1, 0 },
    { 0x43, CL,  4,  0, 0,  0,  0,   0, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_CAMP,    7, 0, 0, 0 },
    { 0x44, CL,  4,  0, 0,  0,  0,   4, T_FIGHT, D_ZERO, V_POISN, AFFECT_NONE,                      W_FIGHT,   7, 6, 1, 0 },
    { 0x45, CL,  4,  3, 0,  0, 10,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_PROT_FROM_EVIL_10_RADIUS,  W_BOTH,    7, 2, 0, 0 },
    { 0x46, CL,  4,  3, 0,  0,  2,   4, T_FIGHT, D_NORM, V_SPELL, AFFECT_STICKS_TO_SNAKES,          W_FIGHT,   7, 4, 1, 0 },
    { 0x47, CL,  5,  0, 0,  0,  0,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    8, 2, 0, 0 },
    { 0x48, CL,  5, -1, 0,  0,  0,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_NONE,                      W_FIGHT,   8, 6, 1, 0 },
    { 0x49, CL,  5,  0, 0,  0,  1,   0, T_FIGHT, D_NORM, V_SPELL, AFFECT_SP_DISPEL_EVIL,            W_FIGHT,   8, 3, 0, 0 },
    { 0x4a, CL,  5,  6, 0,  0,  0,   4, T_FIGHT, D_HALF, V_SPELL, AFFECT_NONE,                      W_FIGHT,   8, 6, 1, 0 },
    { 0x4b, CL,  5,  0, 0,  0,  0,   0, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_CAMP,   10, 1, 0, 0 },
    { 0x4c, CL,  5,  3, 0,  0,  0,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_NONE,                      W_FIGHT,  10, 7, 1, 0 },
    { 0x4d, DR,  1,  3, 0, 12,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_DETECT_MAGIC,              W_BOTH,    3, 1, 0, 0 },
    { 0x4e, DR,  1,  8, 0, 10,  0,  11, T_FIGHT, D_ZERO, V_SPELL, AFFECT_ENTANGLE,                  W_FIGHT,   3, 3, 1, 0 },
    { 0x4f, DR,  1,  8, 0,  0,  4,   5, T_FIGHT, D_ZERO, V_SPELL, AFFECT_FAERIE_FIRE,               W_FIGHT,   3, 4, 1, 0 },
    { 0x50, DR,  1, -1, 0, 10,  1,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_INVISIBLE_TO_ANIMALS,      W_BOTH,    4, 1, 1, 0 },
    { 0x51, MU,  4,  6, 0,  0,  0,   5, T_FIGHT, D_ZERO, V_SPELL, AFFECT_CHARM_PERSON,              W_FIGHT,   4, 6, 1, 0 },
    { 0x52, MU,  4, 12, 0,  2,  1,  11, T_FIGHT, D_ZERO, V_SPELL, AFFECT_CONFUSE,                   W_FIGHT,   4, 7, 1, 0 },
    { 0x53, MU,  4,  0, 3,  0,  0,   8, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_FIGHT,   1, 0, 1, 0 },
    { 0x54, MU,  4,  6, 0,  0,  1,   8, T_FIGHT, D_ZERO, V_SPELL, AFFECT_FEAR,                      W_FIGHT,   4, 6, 1, 0 },
    { 0x55, MU,  4,  0, 0,  2,  1,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    4, 8, 0, 0 },
    { 0x56, MU,  4,  0, 1,  0,  1,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_FUMBLING,                  W_FIGHT,   4, 4, 1, 0 },
    { 0x57, MU,  4,  0, 1,  0,  0,  10, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_FIGHT,   4, 7, 1, 0 },
    { 0x58, MU,  4,  0, 1,  0,  1,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_MINOR_GLOBE_OF_INVULN,     W_BOTH,    4, 5, 0, 0 },
    { 0x59, MU,  4,  0, 0,  0,  0,   4, T_ONE,   D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    4, 0, 0, 0 },
    { 0x5a, MO,  5,  1, 0,  0,  0, 240, T_ALL,   D_NORM, V_SPELL, AFFECT_ANIMATE_DEAD,              W_BOTH,    5, 0, 0, 0 },
    { 0x5b, MU,  5,  2, 0,  0,  1,   9, T_FIGHT, D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    5, 5, 1, 0 },
    { 0x5c, MU,  5,  6, 0,  0,  0,   8, T_FIGHT, D_HALF, V_SPELL, AFFECT_NONE,                      W_FIGHT,   5, 6, 1, 0 },
    { 0x5d, MU,  5, 16, 0,  0,  0,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_FEEBLEMIND,                W_FIGHT,   5, 6, 1, 0 },
    { 0x5e, MU,  5,  0, 1,  0,  1,   7, T_FIGHT, D_ZERO, V_SPELL, AFFECT_PARALYZE,                  W_FIGHT,   5, 7, 1, 0 },
    { 0x5f, MO,  6,  0, 0,  0,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_PROT_DRAG_BREATH,          W_BOTH,   10, 1, 0, 0 },
    { 0x60, MO,  6,  0, 0,  0,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_RESIST_PARALYZE,           W_BOTH,   10, 1, 0, 0 },
    { 0x61, MO,  6,  0, 0,  0,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_INVISIBILITY,              W_BOTH,    0, 1, 0, 0 },
    { 0x62, MO,  6,  3, 0,  0,  0,  11, T_FIGHT, D_ZERO, V_SPELL, AFFECT_NONE,                      W_FIGHT,   0, 1, 1, 0 },
    { 0x63, MO,  6,  0, 0,  0,  0,   0, T_SELF,  D_NORM, V_SPELL, AFFECT_NONE,                      W_BOTH,    0, 1, 0, 0 },
    { 0x64, MU,  4,  0, 0,  0, 10,   4, T_FIGHT, D_ZERO, V_SPELL, AFFECT_NONE,                      W_FIGHT,   4, 4, 1, 0 },
    { 0x65, U10, 0, 10, 0,  6,  0,  24, T_FIGHT, D_1E,   V_POISN, AFFECT_ENLARGE,                   W_CAMP,    0, 1, 0x28, 0x28 }
};

#undef CL
#undef DR
#undef MU
#undef MO
#undef U10
#undef T_FIGHT
#undef T_SELF
#undef T_ONE
#undef T_ALL
#undef D_NORM
#undef D_ZERO
#undef D_HALF
#undef D_3
#undef D_1E
#undef V_SPELL
#undef V_POISN
#undef W_CAMP
#undef W_FIGHT
#undef W_BOTH

bool spell_id_valid(int spell_id)
{
    return spell_id >= 1 && spell_id < SPELL_CASTING_TABLE_COUNT;
}

const SpellEntry *spell_entry(int spell_id)
{
    if (!spell_id_valid(spell_id)) {
        log_warn("spell table: no spell with id %d", spell_id);
        return NULL;
    }
    return &spell_casting_table[spell_id];
}
