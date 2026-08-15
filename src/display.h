/* display.h - the EGA mode 0Dh framebuffer. Ported from Classes/Display.cs.
 *
 * Two buffers are kept in step, exactly as the original did:
 *   ram[y][x]  the authoritative 16-color index plane. GetPixel reads it, and
 *              a palette change re-renders from it.
 *   fb[]       a packed 32-bit ARGB mirror handed straight to the platform
 *              layer as a texture.
 *
 * Nothing here talks to SDL; display_present() is supplied by the platform.
 */
#ifndef COAB_DISPLAY_H
#define COAB_DISPLAY_H

#include "coab.h"

void display_init(void);

/* --- pixel access ------------------------------------------------------- */

/* Writes a palette index. Values >= 16 are ignored, which is what makes the
 * transparent index in masked art behave as "leave this pixel alone"
 * (Classes/Display.cs: SetPixel3). */
void display_set_pixel(int x, int y, int value);
u8   display_get_pixel(int x, int y);

/* Blits one 8x8 monochrome glyph: set bits take fg_color, clear bits take
 * bg_color (Classes/Display.cs: DisplayMono8x8). */
void display_mono_8x8(int x_col, int y_col, const u8 *mono_data_8x8,
                      int bg_color, int fg_color);

/* --- palette ----------------------------------------------------------- */

/* Points palette slot `index` at hardware color `colour` and re-renders the
 * whole frame from the index plane. */
void display_set_ega_palette(int index, int colour);
void display_reset_palette(void);

/* --- presentation ------------------------------------------------------ */

/* Update() is a no-op while stopped, so a sequence of draws can be batched
 * into a single visible change. Calls nest. */
void display_update_stop(void);
void display_update_start(void);
void display_update(void);
void display_force_update(void);

void display_save_vid_ram(void);
void display_restore_vid_ram(void);

/* The packed ARGB frame, EGA_W * EGA_H pixels, EGA_W * 4 bytes per row. */
const u32 *display_framebuffer(void);
int        display_pitch(void);

/* Writes the current frame as a binary PPM. Used by the headless self-test. */
bool display_write_ppm(const char *path);

#endif /* COAB_DISPLAY_H */
