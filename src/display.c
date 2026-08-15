#include "display.h"
#include "platform.h"
#include "vfs.h"

#include <stdio.h>
#include <string.h>

/* The 16 EGA colors as the original rendered them. These are the standard EGA
 * RGB triples with the 0xAA/0x55 levels rounded to 173/82, which is what the
 * C# reference used, so output matches it byte for byte. */
static const u8 ORIG_EGA_COLORS[EGA_COLORS][3] = {
    {   0,   0,   0 }, {   0,   0, 173 }, {   0, 173,   0 }, {   0, 173, 173 },
    { 173,   0,   0 }, { 173,   0, 173 }, { 173,  82,   0 }, { 173, 173, 173 },
    {  82,  82,  82 }, {  82,  82, 255 }, {  82, 255,  82 }, {  82, 255, 255 },
    { 255,  82,  82 }, { 255,  82, 255 }, { 255, 255,  82 }, { 255, 255, 255 }
};

/* The live palette: the game remaps slots at runtime (fades, damage flashes),
 * so this drifts away from the table above. */
static u8  g_ega_colors[EGA_COLORS][3];
static u32 g_argb[EGA_COLORS];

static u8  g_ram[EGA_H][EGA_W];          /* palette indices */
static u32 g_fb[EGA_H * EGA_W];          /* packed ARGB mirror */
static u32 g_fb_backup[EGA_H * EGA_W];
static u8  g_ram_backup[EGA_H][EGA_W];
static bool g_have_backup;

static int g_no_update_count;

static const u8 MONO_BIT_MASK[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

static void palette_cache_entry(int index)
{
    g_argb[index] = 0xff000000u
                  | ((u32)g_ega_colors[index][0] << 16)
                  | ((u32)g_ega_colors[index][1] << 8)
                  | ((u32)g_ega_colors[index][2]);
}

void display_reset_palette(void)
{
    memcpy(g_ega_colors, ORIG_EGA_COLORS, sizeof(g_ega_colors));
    for (int i = 0; i < EGA_COLORS; i++) {
        palette_cache_entry(i);
    }
}

void display_init(void)
{
    display_reset_palette();
    memset(g_ram, 0, sizeof(g_ram));
    memset(g_ram_backup, 0, sizeof(g_ram_backup));
    g_have_backup = false;
    g_no_update_count = 0;

    for (size_t i = 0; i < COAB_ARRAY_LEN(g_fb); i++) {
        g_fb[i] = g_argb[0];
    }
    memcpy(g_fb_backup, g_fb, sizeof(g_fb_backup));
}

static inline void set_vid_pixel(int x, int y, int ega_color)
{
    g_fb[(size_t)y * EGA_W + x] = g_argb[ega_color];
}

void display_set_pixel(int x, int y, int value)
{
    /* Index 16 and above means "transparent" in masked art, so it draws
     * nothing. The bounds check is new: the C# indexer threw, and several
     * callers rely on clipping happening somewhere. */
    if (value < 0 || value >= EGA_COLORS) {
        return;
    }
    if (x < 0 || x >= EGA_W || y < 0 || y >= EGA_H) {
        return;
    }

    g_ram[y][x] = (u8)value;
    set_vid_pixel(x, y, value);
}

u8 display_get_pixel(int x, int y)
{
    if (x < 0 || x >= EGA_W || y < 0 || y >= EGA_H) {
        return 0;
    }
    return g_ram[y][x];
}

void display_mono_8x8(int x_col, int y_col, const u8 *mono_data_8x8,
                      int bg_color, int fg_color)
{
    int px = x_col * 8;

    if (!mono_data_8x8) {
        return;
    }
    if (bg_color < 0 || bg_color >= EGA_COLORS) bg_color = 0;
    if (fg_color < 0 || fg_color >= EGA_COLORS) fg_color = 0;

    for (int y_step = 0; y_step < 8; y_step++) {
        int py = (y_col * 8) + y_step;
        u8 value = mono_data_8x8[y_step];

        if (py < 0 || py >= EGA_H) {
            continue;
        }
        for (int i = 0; i < 8; i++) {
            int x = px + i;
            int color = (value & MONO_BIT_MASK[i]) != 0 ? fg_color : bg_color;

            if (x < 0 || x >= EGA_W) {
                continue;
            }
            g_ram[py][x] = (u8)color;
            set_vid_pixel(x, py, color);
        }
    }
}

void display_set_ega_palette(int index, int colour)
{
    if (index < 0 || index >= EGA_COLORS || colour < 0 || colour >= EGA_COLORS) {
        return;
    }

    g_ega_colors[index][0] = ORIG_EGA_COLORS[colour][0];
    g_ega_colors[index][1] = ORIG_EGA_COLORS[colour][1];
    g_ega_colors[index][2] = ORIG_EGA_COLORS[colour][2];
    palette_cache_entry(index);

    /* A palette write changes pixels already on screen, so the whole frame is
     * rebuilt from the index plane. */
    for (int y = 0; y < EGA_H; y++) {
        for (int x = 0; x < EGA_W; x++) {
            g_fb[(size_t)y * EGA_W + x] = g_argb[g_ram[y][x]];
        }
    }

    display_update();
}

void display_update_stop(void)
{
    g_no_update_count++;
}

void display_update_start(void)
{
    if (g_no_update_count > 0) {
        g_no_update_count--;
    }
    display_update();
}

void display_update(void)
{
    if (g_no_update_count == 0) {
        platform_present(g_fb);
    }
}

void display_force_update(void)
{
    platform_present(g_fb);
}

void display_save_vid_ram(void)
{
    memcpy(g_fb_backup, g_fb, sizeof(g_fb_backup));
    memcpy(g_ram_backup, g_ram, sizeof(g_ram_backup));
    g_have_backup = true;
}

void display_restore_vid_ram(void)
{
    if (!g_have_backup) {
        return;
    }
    memcpy(g_fb, g_fb_backup, sizeof(g_fb));
    /* The C# only restored the packed buffer, leaving ram[] stale, which made
     * a later GetPixel or palette change resurrect the discarded image.
     * Restoring both keeps the two planes consistent. */
    memcpy(g_ram, g_ram_backup, sizeof(g_ram));
}

const u32 *display_framebuffer(void)
{
    return g_fb;
}

int display_pitch(void)
{
    return EGA_W * (int)sizeof(u32);
}

bool display_write_ppm(const char *path)
{
    FILE *f = vfs_fopen(path, "wb");

    if (!f) {
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", EGA_W, EGA_H);

    for (size_t i = 0; i < COAB_ARRAY_LEN(g_fb); i++) {
        u8 rgb[3];
        rgb[0] = (u8)((g_fb[i] >> 16) & 0xff);
        rgb[1] = (u8)((g_fb[i] >> 8) & 0xff);
        rgb[2] = (u8)(g_fb[i] & 0xff);
        if (fwrite(rgb, 1, 3, f) != 3) {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return true;
}
