/* combat.h - the per-round bookkeeping a fight needs.
 * Ported from Classes/Combat.cs, Classes/Action.cs, Classes/Combat (all three),
 * Classes/SteppingPath.cs, Classes/GasCloud.cs, Classes/DownedPlayerTile.cs and
 * Classes/Struct_1D1BC.cs.
 *
 * These all point at Player rather than owning one. The party and the monster
 * roster outlive any single round, so the pointers stay valid for as long as the
 * fight does - but they are borrowed, and nothing here frees them.
 */
#ifndef COAB_COMBAT_H
#define COAB_COMBAT_H

#include "coab.h"
#include "dax.h"
#include "player.h"
#include "point.h"

/* ------------------------------------------------------------------ Action */

/* What a combatant has decided to do this round. Offsets are the DOS layout;
 * the structure is scratch state and is never written to disk. */
typedef struct Action {
    int    spell_id;            /* 0x00 */
    bool   can_cast;            /* 0x01 */
    bool   can_use;             /* 0x02 */
    int    delay;               /* 0x03 */
    int    attack_idx;          /* 0x04 */
    u8     max_sweap_targets;   /* 0x05, a sweep attack's target limit */
    int    move;                /* 0x06 */
    bool   guarding;            /* 0x07 */
    bool   field_8;             /* 0x08 */
    int    direction;           /* 0x09 */
    Player *target;             /* 0x0a, a far pointer in the original */
    int    bleeding;            /* 0x0e */
    u8     attacks_received;    /* 0x0f */
    bool   fleeing;             /* 0x10 */
    bool   has_turned_undead;   /* 0x11 */
    int    direction_changes;   /* 0x12 */
    bool   non_team_member;     /* 0x13 */
    bool   moral_failure;       /* 0x14 */
    int    field_15;            /* 0x15 */
} Action;

void action_init(Action *a);

/* Action.Clear: only the four fields the original reset between rounds. */
void action_clear(Action *a);

/* player->actions, for the code that reads it without checking. A character in
 * a fight always has one; where the C# would have thrown a NullReferenceException
 * this logs and hands back a scratch record, so a bug shows up in the log
 * instead of taking the process down. Never returns NULL. */
Action *player_actions(Player *p);

/* ------------------------------------------------------- turn order (0x1D1C1) */

typedef struct {
    Player *player;
    Point   pos;
    /* Counted in halves so a diagonal step can cost 3, i.e. one and a half. */
    int     steps;
    int     direction;
} SortedCombatant;

/* CompareTo: fewest steps first, then lowest direction. The C# had a third tier
 * comparing direction parity, but it only ran when the two directions were equal
 * and so always returned zero.
 *
 * The sort is stable, which List<T>.Sort was not: with a stable sort the same
 * save always plays out the same way. */
void combatant_sort(SortedCombatant *list, int count);

/* ---------------------------------------------------- combat map bookkeeping */

/* Where a combatant is on the map and on the screen (Struct_1C9CD). */
typedef struct {
    Point pos;
    int   size;
    Point screen_pos;
} CombatantMap;

/* A player and where they stand, for the target-picking lists. */
typedef struct {
    Player *player;
    Point   pos;
} CombatPlayerIndex;

/* The tile a fallen character is lying on, so it can be restored when the body
 * is moved or raised (Struct_1D183). */
typedef struct {
    Player *target;
    Point   map;
    int     original_background_tile;
} DownedPlayerTile;

void downed_player_tile_clear(DownedPlayerTile *d);

/* ------------------------------------------------------- lingering gas clouds */

/* Stinking cloud and cloudkill: up to ten cells, held as base-1 arrays in the
 * original, which is why index 0 goes unused. */
#define GAS_CLOUD_CELLS 10

typedef struct {
    Player *player;                     /* 0x00, who cast it */
    int     ground_tile[GAS_CLOUD_CELLS]; /* 0x07 */
    bool    present[GAS_CLOUD_CELLS];    /* 0x10 */
    Point   target_pos;
    int     field_1C;                   /* 0x1c, the cloud's id within the cast */
    bool    field_1D;                   /* 0x1d */
} GasCloud;

void gas_cloud_init(GasCloud *g, Player *player, int count, Point pos);

/* ---------------------------------------------------------- stepping a line */

/* Walks a straight line between two map squares a step at a time, Bresenham
 * style, and reports the compass direction of each step. Missiles, breath
 * weapons and lightning bolts all trace their path with this.
 */
typedef struct {
    Point attacker;     /* 0x00 */
    Point target;       /* 0x04 */
    int   delta_count;  /* 0x08 */
    int   diff_x;       /* 0x0a */
    int   diff_y;       /* 0x0c */
    Point current;      /* 0x0e */
    int   sign_x;       /* 0x12 */
    int   sign_y;       /* 0x14 */
    /* Also counted in halves: a diagonal step adds 3. */
    u8    steps;        /* 0x16 */
    u8    direction;    /* 0x17 */
} SteppingPath;

void stepping_path_clear(SteppingPath *p);

/* sub_731A5: sets current to attacker and works out the deltas. Call after
 * filling in attacker and target. */
void stepping_path_calculate_deltas(SteppingPath *p);

/* sub_7324C: takes one step along the line, setting direction to the compass
 * direction of that step. Returns false once current has reached target - and
 * note that the call which returns false also sets direction, to the no-move
 * entry 8, so a caller that wants the last real step has to keep it as it goes.
 * The original did the same. */
bool stepping_path_step(SteppingPath *p);

/* ------------------------------------------------- the ground tile overlay */

/* One value per map square, used while a fight is being drawn: which background
 * tile the square shows, or how many steps away it is when a reach is being
 * measured (Struct_1D1BC).
 */
typedef struct {
    Point map_screen_top_left;
    bool  draw_target_cursor;   /* field_4 */
    int   size;                 /* field_5 */
    bool  ignore_walls;         /* field_6 */
    int   tile[MAP_MAX_Y * MAP_MAX_X];   /* field_7, indexed x + y * 50 */
} GroundTileMap;

void ground_tile_map_clear(GroundTileMap *m);

/* SetField_7: every square to the same value. */
void ground_tile_map_fill(GroundTileMap *m, int value);

/* Out-of-map squares read as zero and ignore writes; the C# indexer would have
 * thrown, and combat code does walk off the edge of the map. */
int  ground_tile_map_get(const GroundTileMap *m, Point pos);
void ground_tile_map_set(GroundTileMap *m, Point pos, int value);

/* ------------------------------------------------------------- combat icons */

typedef enum {
    COMBAT_ICON_NORMAL = 0,
    COMBAT_ICON_ATTACK = 1
} CombatIconState;

/* A combatant's two pictures, each also kept mirrored so that facing left costs
 * nothing to draw and the loaded copy is never damaged. */
typedef struct {
    DaxBlock *normal;
    DaxBlock *normal_f;
    DaxBlock *attack;
    DaxBlock *attack_f;
} CombatIcon;

void combat_icon_init(CombatIcon *ci);

/* Release: frees the pictures. The C# only dropped its references and left the
 * collector to it. */
void combat_icon_release(CombatIcon *ci);

/* LoadIcons: reads both pictures out of <file_text>.dax and builds the mirrored
 * copies. Returns false with everything released if any block is missing. */
bool combat_icon_load(CombatIcon *ci, int mask_colour, int masked,
                      const char *file_text, int normal_id, int attack_id);

void combat_icon_recolor(CombatIcon *ci, bool use_random,
                         const u8 *new_colors, const u8 *old_colors);

/* Directions 4..7 face left, so they get the mirrored copy. */
DaxBlock *combat_icon_get(const CombatIcon *ci, CombatIconState state,
                          int direction);

/* MergeIcon: draws src over ci, which is how a head icon is put on a body. */
void combat_icon_merge(CombatIcon *ci, const CombatIcon *src);

/* DuplicateIcon: copies src's pixels into ci, optionally recolouring them to
 * the player's six icon colours. */
bool combat_icon_duplicate(CombatIcon *ci, const CombatIcon *src,
                           bool recolour, const Player *player);

#endif /* COAB_COMBAT_H */
