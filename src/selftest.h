/* selftest.h - headless verification of the ported subsystems.
 *
 * Runs the whole data path (locate files, decompress DAX blocks, unpack to
 * pixels, draw, render text) against the real game data with no display server,
 * writing each result as a PPM so the output can be inspected. Returns false if
 * any check fails.
 */
#ifndef COAB_SELFTEST_H
#define COAB_SELFTEST_H

#include "coab.h"

bool selftest_run(const char *out_dir);

#endif /* COAB_SELFTEST_H */
