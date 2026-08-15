/* set.c - Ported from Classes/Set.cs. */
#include <string.h>

#include "set.h"
#include "log.h"

void set_clear(Set *s)
{
    memset(s->bits, 0, sizeof(s->bits));
}

void set_add(Set *s, int member)
{
    if (member < 0 || member >= SET_MEMBERS) {
        log_warn("set_add: member %d outside 0..%d", member, SET_MEMBERS - 1);
        return;
    }
    s->bits[member >> 3] |= (u8)(1 << (member & 7));
}

void set_add_range(Set *s, int high, int low)
{
    for (int i = low; i <= high; i++) {
        set_add(s, i);
    }
}

bool set_member_of(const Set *s, int member)
{
    if (member < 0 || member >= SET_MEMBERS) {
        return false;
    }
    return (s->bits[member >> 3] & (1 << (member & 7))) != 0;
}

void set_init_packed(Set *s, u16 packed, const u8 *src, size_t src_size)
{
    unsigned lead  = (unsigned)(packed >> 8);
    unsigned count = (unsigned)(packed & 0x00ff);

    set_clear(s);

    if (lead >= SET_BYTES) {
        log_warn("set_init_packed: %u leading bytes exceeds the %d byte set",
                 lead, SET_BYTES);
        return;
    }
    if (count > SET_BYTES - lead) {
        log_warn("set_init_packed: %u bytes at %u overruns the %d byte set",
                 count, lead, SET_BYTES);
        count = SET_BYTES - lead;
    }
    if (count > src_size) {
        log_warn("set_init_packed: %u bytes wanted but only %zu available",
                 count, src_size);
        count = (unsigned)src_size;
    }

    memcpy(s->bits + lead, src, count);
}
