#include "coab.h"

/* Classes/Sys.cs: WrapMinMax - steps a value that has run off either end of a
 * range around to the other side. */
int sys_wrap_min_max(int val, int min, int max)
{
    if (val > max) {
        return min;
    }
    if (val < min) {
        return max;
    }
    return val;
}
