/* combat.c - Ported from Classes/Combat.cs, Classes/Action.cs,
 * Classes/Combat (all three), Classes/SteppingPath.cs, Classes/GasCloud.cs,
 * Classes/DownedPlayerTile.cs and Classes/Struct_1D1BC.cs.
 */
#include <string.h>

#include "combat.h"

#include "gbl.h"
#include "log.h"

/* ------------------------------------------------------------------ Action */

void action_init(Action *a)
{
    memset(a, 0, sizeof(*a));
}

void action_clear(Action *a)
{
    a->delay    = 0;
    a->spell_id = 0;
    a->guarding = false;
    a->move     = 0;
}

Action *player_actions(Player *p)
{
    /* One shared record: a caller that gets here is already reading rubbish, and
     * the point is only that it does not read freed memory. */
    static Action scratch;

    if (p != NULL && p->actions != NULL) {
        return p->actions;
    }

    log_warn("combat: %s has no action record",
             (p != NULL && p->name[0] != '\0') ? p->name : "a combatant");
    action_init(&scratch);
    return &scratch;
}

/* ---------------------------------------------------------------- turn order */

static int combatant_compare(const SortedCombatant *a, const SortedCombatant *b)
{
    if (a->steps != b->steps) {
        return a->steps - b->steps;
    }
    return a->direction - b->direction;
}

/* Insertion sort: the list is one entry per combatant, so a dozen or two at
 * most, and being stable matters more than being asymptotically quick. */
void combatant_sort(SortedCombatant *list, int count)
{
    for (int i = 1; i < count; i++) {
        SortedCombatant key = list[i];
        int j = i - 1;

        while (j >= 0 && combatant_compare(&list[j], &key) > 0) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }
}

/* ---------------------------------------------------- combat map bookkeeping */

void downed_player_tile_clear(DownedPlayerTile *d)
{
    memset(d, 0, sizeof(*d));
}

/* ------------------------------------------------------- lingering gas clouds */

void gas_cloud_init(GasCloud *g, Player *player, int count, Point pos)
{
    memset(g, 0, sizeof(*g));

    g->player     = player;
    g->field_1C   = count;
    g->target_pos = pos;
}

/* ---------------------------------------------------------- stepping a line */

/* The step just taken, as a compass direction, looked up by the signs of the two
 * deltas offset to 0..2. Entry 4 is the no-move case and entry 9 is never
 * reached; both were 8 in the original, which is not a direction. */
static const u8 stepping_directions[10] = { 7, 0, 1, 6, 8, 2, 5, 4, 3, 8 };

void stepping_path_clear(SteppingPath *p)
{
    memset(p, 0, sizeof(*p));
}

static int sign_of(int v)
{
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

void stepping_path_calculate_deltas(SteppingPath *p)
{
    Point d = point_sub(p->target, p->attacker);

    p->current = p->attacker;

    p->diff_x = d.x < 0 ? -d.x : d.x;
    p->diff_y = d.y < 0 ? -d.y : d.y;

    p->sign_x = sign_of(d.x);
    p->sign_y = sign_of(d.y);

    p->delta_count = 0;
    p->steps       = 0;
}

bool stepping_path_step(SteppingPath *p)
{
    bool step_made = false;
    int  index_x   = 1;
    int  index_y   = 1;

    /* Step along the longer axis, and carry onto the shorter one whenever the
     * running error says the line has crossed into the next row. */
    if (p->diff_x >= p->diff_y) {
        if (p->current.x != p->target.x) {
            p->current.x += p->sign_x;
            p->delta_count += p->diff_y * 2;
            p->steps = (u8)(p->steps + 2);

            index_x = p->sign_x + 1;

            if (p->delta_count >= p->diff_x) {
                p->current.y += p->sign_y;
                p->delta_count -= p->diff_x * 2;
                p->steps = (u8)(p->steps + 1);

                index_y = p->sign_y + 1;
            }

            step_made = true;
        }
    } else if (p->current.y != p->target.y) {
        p->current.y += p->sign_y;
        p->delta_count += p->diff_x * 2;
        p->steps = (u8)(p->steps + 2);

        index_y = p->sign_y + 1;

        if (p->delta_count >= p->diff_y) {
            p->current.x += p->sign_x;
            p->delta_count -= p->diff_y * 2;
            p->steps = (u8)(p->steps + 1);

            index_x = p->sign_x + 1;
        }

        step_made = true;
    }

    p->direction = stepping_directions[(index_y * 3) + index_x];

    return step_made;
}

/* ------------------------------------------------- the ground tile overlay */

void ground_tile_map_clear(GroundTileMap *m)
{
    memset(m, 0, sizeof(*m));
}

void ground_tile_map_fill(GroundTileMap *m, int value)
{
    for (size_t i = 0; i < COAB_ARRAY_LEN(m->tile); i++) {
        m->tile[i] = value;
    }
}

static bool tile_index(Point pos, size_t *out)
{
    if (!point_map_in_bounds(pos)) {
        return false;
    }
    *out = (size_t)pos.y * MAP_MAX_X + (size_t)pos.x;

    return true;
}

int ground_tile_map_get(const GroundTileMap *m, Point pos)
{
    size_t at;

    if (!tile_index(pos, &at)) {
        return 0;
    }
    return m->tile[at];
}

void ground_tile_map_set(GroundTileMap *m, Point pos, int value)
{
    size_t at;

    if (!tile_index(pos, &at)) {
        return;
    }
    m->tile[at] = value;
}

/* ------------------------------------------------------------- combat icons */

void combat_icon_init(CombatIcon *ci)
{
    memset(ci, 0, sizeof(*ci));
}

void combat_icon_release(CombatIcon *ci)
{
    dax_block_free(ci->normal);
    dax_block_free(ci->normal_f);
    dax_block_free(ci->attack);
    dax_block_free(ci->attack_f);
    combat_icon_init(ci);
}

bool combat_icon_load(CombatIcon *ci, int mask_colour, int masked,
                      const char *file_text, int normal_id, int attack_id)
{
    char name[64];
    size_t n = strlen(file_text);

    if (n + 5 > sizeof(name)) {
        log_warn("combat icon: archive name '%s' is too long", file_text);
        return false;
    }
    memcpy(name, file_text, n);
    memcpy(name + n, ".dax", 5);

    combat_icon_init(ci);

    /* Each picture is loaded twice rather than copied, so the mirrored copy can
     * be flipped without touching the one that will be drawn facing right. */
    ci->normal   = dax_load_block(name, normal_id, masked, mask_colour);
    ci->normal_f = dax_load_block(name, normal_id, masked, mask_colour);
    ci->attack   = dax_load_block(name, attack_id, masked, mask_colour);
    ci->attack_f = dax_load_block(name, attack_id, masked, mask_colour);

    if (ci->normal == NULL || ci->normal_f == NULL ||
        ci->attack == NULL || ci->attack_f == NULL) {
        log_warn("combat icon: %s blocks %d and %d did not load",
                 name, normal_id, attack_id);
        combat_icon_release(ci);
        return false;
    }

    dax_block_flip_left_to_right(ci->normal_f);
    dax_block_flip_left_to_right(ci->attack_f);

    return true;
}

void combat_icon_recolor(CombatIcon *ci, bool use_random,
                         const u8 *new_colors, const u8 *old_colors)
{
    dax_block_recolor(ci->normal,   use_random, new_colors, old_colors);
    dax_block_recolor(ci->normal_f, use_random, new_colors, old_colors);
    dax_block_recolor(ci->attack,   use_random, new_colors, old_colors);
    dax_block_recolor(ci->attack_f, use_random, new_colors, old_colors);
}

DaxBlock *combat_icon_get(const CombatIcon *ci, CombatIconState state,
                          int direction)
{
    if (state == COMBAT_ICON_NORMAL) {
        return direction > 3 ? ci->normal_f : ci->normal;
    }
    return direction > 3 ? ci->attack_f : ci->attack;
}

void combat_icon_merge(CombatIcon *ci, const CombatIcon *src)
{
    dax_block_merge_icons(ci->normal,   src->normal);
    dax_block_merge_icons(ci->normal_f, src->normal_f);
    dax_block_merge_icons(ci->attack,   src->attack);
    dax_block_merge_icons(ci->attack_f, src->attack_f);
}

static bool copy_pixels(DaxBlock *dst, const DaxBlock *src, const char *what)
{
    if (dst == NULL || src == NULL) {
        log_warn("combat icon: %s is missing, cannot duplicate", what);
        return false;
    }
    if (dst->data_size != src->data_size) {
        /* The C# copied src's length into dst regardless, which either threw or
         * left dst half written. Two icons of different geometry cannot stand in
         * for each other, so this refuses instead. */
        log_warn("combat icon: %s is %zu bytes but the source is %zu",
                 what, dst->data_size, src->data_size);
        return false;
    }
    memcpy(dst->data, src->data, dst->data_size);

    return true;
}

bool combat_icon_duplicate(CombatIcon *ci, const CombatIcon *src,
                           bool recolour, const Player *player)
{
    if (!copy_pixels(ci->normal,   src->normal,   "normal icon") ||
        !copy_pixels(ci->normal_f, src->normal_f, "mirrored normal icon") ||
        !copy_pixels(ci->attack,   src->attack,   "attack icon") ||
        !copy_pixels(ci->attack_f, src->attack_f, "mirrored attack icon")) {
        return false;
    }

    if (recolour) {
        u8 new_colors[EGA_COLORS];
        u8 old_colors[EGA_COLORS];

        for (int i = 0; i < EGA_COLORS; i++) {
            old_colors[i] = (u8)i;
            new_colors[i] = (u8)i;
        }

        /* Each of the six icon colours is stored as two nibbles: the low one
         * recolours the base entry, the high one the same entry eight brighter. */
        for (int i = 0; i < GBL_ICON_COLOUR_COUNT; i++) {
            int base = GBL_DEFAULT_ICON_COLOURS[i];

            new_colors[base]     = (u8)(player->icon_colours[i] & 0x0f);
            new_colors[base + 8] = (u8)((player->icon_colours[i] & 0xf0) >> 4);
        }

        combat_icon_recolor(ci, false, new_colors, old_colors);
    }

    return true;
}
