/* sound.h - the engine's sound interface. Ported from engine/seg044.cs.
 *
 * The DOS game drove the PC speaker directly; the C# port replaced that with
 * ten sampled WAVs, which this port reuses. sound_play() takes the same opaque
 * Sound ids the engine passes around.
 */
#ifndef COAB_SOUND_H
#define COAB_SOUND_H

#include "coab.h"

/* Locates and loads the effect WAVs. sounds_dir may be NULL, in which case a
 * few paths relative to the data directory and the executable are tried.
 * Missing files are not fatal: the game just runs quieter. */
void sound_init(const char *argv0, const char *sounds_dir);

void sound_play(Sound which);       /* seg044.PlaySound */
void sound_set_enabled(bool on);    /* seg044.SetSound */
void sound_shutdown(void);

#endif /* COAB_SOUND_H */
