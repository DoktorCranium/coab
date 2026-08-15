/* point.h - a map or screen coordinate pair. Ported from the Point struct in
 * Classes/Gbl.cs.
 *
 * Passed and returned by value, as the C# struct was. The operators become
 * named functions; point_eq replaces == because C cannot overload it, and
 * forgetting that is a silent bug, so there is no other way to compare two.
 */
#ifndef COAB_POINT_H
#define COAB_POINT_H

#include "coab.h"

/* The wilderness and dungeon maps are 50x25 cells. */
#define MAP_MAX_X     50
#define MAP_MAX_Y     25
#define MAP_MIN_X     0
#define MAP_MIN_Y     0

/* The 3D view and the combat overview show a 6x6 window; the party sits in the
 * middle of it. */
#define SCREEN_MAX_X  6
#define SCREEN_MAX_Y  6
#define SCREEN_HALF_X (SCREEN_MAX_X / 2)
#define SCREEN_HALF_Y (SCREEN_MAX_Y / 2)

typedef struct {
    int x;
    int y;
} Point;

static inline Point point_make(int x, int y)
{
    Point p = { x, y };

    return p;
}

static inline Point point_screen_center(void)
{
    return point_make(SCREEN_HALF_X, SCREEN_HALF_Y);
}

static inline Point point_add(Point a, Point b)
{
    return point_make(a.x + b.x, a.y + b.y);
}

static inline Point point_sub(Point a, Point b)
{
    return point_make(a.x - b.x, a.y - b.y);
}

static inline Point point_mul(Point a, int b)
{
    return point_make(a.x * b, a.y * b);
}

/* C and C# both truncate integer division toward zero, so this matches. */
static inline Point point_div(Point a, int b)
{
    return point_make(a.x / b, a.y / b);
}

static inline bool point_eq(Point a, Point b)
{
    return a.x == b.x && a.y == b.y;
}

/* MapBoundaryTrunc: clamps into the map, in place. */
static inline void point_map_clamp(Point *p)
{
    p->x = COAB_MAX(COAB_MIN(p->x, MAP_MAX_X - 1), MAP_MIN_X);
    p->y = COAB_MAX(COAB_MIN(p->y, MAP_MAX_Y - 1), MAP_MIN_Y);
}

static inline bool point_map_in_bounds(Point p)
{
    return p.x < MAP_MAX_X && p.x >= MAP_MIN_X &&
           p.y < MAP_MAX_Y && p.y >= MAP_MIN_Y;
}

#endif /* COAB_POINT_H */
