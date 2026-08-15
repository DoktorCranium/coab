/* roster.c - the pool the Player records come out of. See roster.h. */

#include "roster.h"

#include "log.h"

static Player roster_pool[ROSTER_MAX];
static bool   roster_used[ROSTER_MAX];

Player *roster_alloc(void)
{
    for (int i = 0; i < ROSTER_MAX; i++) {
        if (!roster_used[i]) {
            roster_used[i] = true;
            player_init(&roster_pool[i]);
            return &roster_pool[i];
        }
    }

    log_warn("roster: all %d character records are in use", ROSTER_MAX);

    return NULL;
}

Player *roster_clone(const Player *src)
{
    Player *copy;

    if (src == NULL) {
        return NULL;
    }

    copy = roster_alloc();
    if (copy == NULL) {
        return NULL;
    }

    *copy = *src;

    return copy;
}

bool roster_owns(const Player *player)
{
    return player >= roster_pool && player < roster_pool + ROSTER_MAX;
}

void roster_release(Player *player)
{
    if (player == NULL) {
        return;
    }
    if (!roster_owns(player)) {
        log_warn("roster: %s was not handed out by the roster", player->name);
        return;
    }

    roster_used[player - roster_pool] = false;
}

void roster_clear(void)
{
    for (int i = 0; i < ROSTER_MAX; i++) {
        roster_used[i] = false;
    }
}

int roster_in_use(void)
{
    int count = 0;

    for (int i = 0; i < ROSTER_MAX; i++) {
        if (roster_used[i]) {
            count++;
        }
    }

    return count;
}
