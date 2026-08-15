#include "selftest.h"
#include "affect.h"
#include "affecttab.h"
#include "aftercombat.h"
#include "area.h"
#include "attack.h"
#include "battlesetup.h"
#include "camp.h"
#include "character.h"
#include "cheats.h"
#include "classcalc.h"
#include "combat.h"
#include "combatloop.h"
#include "combatmap.h"
#include "dataio.h"
#include "dax.h"
#include "display.h"
#include "dungeon.h"
#include "draw.h"
#include "ecl.h"
#include "eclvm.h"
#include "effect.h"
#include "endgame.h"
#include "fileio.h"
#include "firework.h"
#include "frames.h"
#include "gbl.h"
#include "geo.h"
#include "icons.h"
#include "import.h"
#include "item.h"
#include "mapcursor.h"
#include "limits.h"
#include "log.h"
#include "menu.h"
#include "money.h"
#include "monsterai.h"
#include "partymenu.h"
#include "picture.h"
#include "platform.h"
#include "player.h"
#include "program.h"
#include "prompt.h"
#include "protect.h"
#include "resting.h"
#include "resttime.h"
#include "rnd.h"
#include "roster.h"
#include "shop.h"
#include "savegame.h"
#include "spellcast.h"
#include "spelleffect.h"
#include "spelllist.h"
#include "spellmenu.h"
#include "spells.h"
#include "target.h"
#include "temple.h"
#include "text.h"
#include "tile.h"
#include "title.h"
#include "treasure.h"
#include "vfs.h"
#include "view3d.h"
#include "viewplayer.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass;
static int g_fail;

static void check(bool ok, const char *what, const char *detail)
{
    if (ok) {
        g_pass++;
        printf("  ok    %s%s%s\n", what, detail ? " - " : "", detail ? detail : "");
    } else {
        g_fail++;
        printf("  FAIL  %s%s%s\n", what, detail ? " - " : "", detail ? detail : "");
    }
}

/* How many of the 320x200 pixels are not background, and how many distinct
 * colors appear. A correctly decoded picture is busy and colorful; a broken one
 * is usually blank or a single flat color, so these two numbers catch the
 * failure modes that matter without hardcoding a checksum. */
static void frame_stats(int *out_nonzero, int *out_colors)
{
    bool seen[EGA_COLORS] = { false };
    int nonzero = 0;
    int colors = 0;

    for (int y = 0; y < EGA_H; y++) {
        for (int x = 0; x < EGA_W; x++) {
            u8 c = display_get_pixel(x, y);

            if (c != 0) {
                nonzero++;
            }
            if (c < EGA_COLORS) {
                seen[c] = true;
            }
        }
    }
    for (int i = 0; i < EGA_COLORS; i++) {
        if (seen[i]) {
            colors++;
        }
    }

    *out_nonzero = nonzero;
    *out_colors = colors;
}

static void dump(const char *out_dir, const char *name)
{
    char path[1024];

    if (!vfs_path_join(path, sizeof(path), out_dir, name)) {
        log_warn("output path too long for %s", name);
        return;
    }
    if (!display_write_ppm(path)) {
        log_warn("could not write %s", path);
    }
}

static void clear_screen_raw(void)
{
    for (int y = 0; y < EGA_H; y++) {
        for (int x = 0; x < EGA_W; x++) {
            display_set_pixel(x, y, 0);
        }
    }
}

/* Every DAX archive in the data directory is opened and every block it claims
 * is decompressed. This is the broadest check available: the RLE decoder either
 * consumes each block exactly or it reports a failure. */
static void check_all_archives(void)
{
    /* The ones there is one of. */
    static const char *SINGLE[] = {
        "title.dax", "sky.dax", "tiles.dax", "comspr.dax", "cbody.dax",
        "chead.dax", "randcom.dax", "dungcom.dax", "wildcom.dax",
        "bigpic1.dax", "bigpic2.dax", "bigpic6.dax"
    };
    /* And the ones there is one of per chapter. The second column is the first
     * chapter that has one: chapter 1 is a town and a wilderness, so it has no
     * dungeon geometry, no wall definitions and nobody to talk to. */
    static const struct {
        const char *stem;    /* before the chapter number */
        const char *tail;    /* after it - only the monster files have one */
        int         first;
    } PER_CHAPTER[] = {
        { "8x8d",  "",    1 }, { "cpic",  "",    1 }, { "ecl", "",    1 },
        { "item",  "",    1 }, { "pic",   "",    1 }, { "sprit", "",  1 },
        { "mon",   "cha", 1 }, { "mon",   "itm", 1 }, { "mon", "spc", 1 },
        { "body",  "",    2 }, { "geo",   "",    2 }, { "head",  "",  2 },
        { "walldef", "",  2 }
    };
    char names[128][20];
    size_t name_count = 0;
    int archives_ok = 0;
    int blocks_ok = 0;

    for (size_t i = 0; i < COAB_ARRAY_LEN(SINGLE); i++) {
        snprintf(names[name_count++], sizeof(names[0]), "%s", SINGLE[i]);
    }
    for (size_t i = 0; i < COAB_ARRAY_LEN(PER_CHAPTER); i++) {
        for (int chapter = PER_CHAPTER[i].first; chapter <= 6; chapter++) {
            snprintf(names[name_count++], sizeof(names[0]), "%s%d%s.dax",
                     PER_CHAPTER[i].stem, chapter, PER_CHAPTER[i].tail);
        }
    }

    for (size_t i = 0; i < name_count; i++) {
        int found = 0;

        for (int id = 0; id < 256; id++) {
            size_t size = 0;
            u8 *raw = dax_load_raw(names[i], id, &size);

            if (raw && size > 0) {
                found++;
                blocks_ok++;
            }
            free(raw);
        }
        if (found > 0) {
            archives_ok++;
        } else {
            printf("  note  %s yielded no blocks\n", names[i]);
        }
    }

    char detail[128];
    snprintf(detail, sizeof(detail), "%d of %d archives, %d blocks decompressed",
             archives_ok, (int)name_count, blocks_ok);
    /* Every archive the game ships with, not a sample of them: 86 files holding
     * 824 blocks between them. */
    check(archives_ok == (int)name_count && archives_ok == 86 &&
          blocks_ok == 824, "DAX RLE decoder", detail);
}

static void check_title_frames(const char *out_dir)
{
    static const struct {
        int block;
        int row_y;
        int col_x;
        const char *file;
    } steps[] = {
        { 1, 0,    0, "title-1.ppm" },
        { 2, 0,    0, "title-2.ppm" },
        { 3, 0x0b, 6, "title-3.ppm" },
        { 4, 0x0b, 0, "title-4.ppm" }
    };

    for (size_t i = 0; i < COAB_ARRAY_LEN(steps); i++) {
        DaxBlock *pic = draw_load_dax(0, 0, steps[i].block, "Title");
        char detail[160];
        int nonzero = 0, colors = 0;

        if (!pic) {
            check(false, "title block", "missing");
            continue;
        }

        clear_screen_raw();
        draw_picture(pic, steps[i].row_y, steps[i].col_x, 0);
        frame_stats(&nonzero, &colors);
        dump(out_dir, steps[i].file);

        snprintf(detail, sizeof(detail),
                 "block %d: %dx%d cells, %d frame(s), %d px drawn, %d colors -> %s",
                 steps[i].block, pic->width, pic->height, pic->item_count,
                 nonzero, colors, steps[i].file);

        /* A real title still covers a good part of the screen and uses most of
         * the palette. */
        check(nonzero > 2000 && colors >= 4, "title picture decode", detail);
        dax_block_free(pic);
    }
}

static void check_font(const char *out_dir)
{
    int nonzero = 0, colors = 0;
    int glyphs_with_pixels = 0;

    for (int g = 0; g < GBL_FONT_GLYPHS; g++) {
        for (int r = 0; r < 8; r++) {
            if (gbl.dax_8x8d1_201[g][r] != 0) {
                glyphs_with_pixels++;
                break;
            }
        }
    }

    char detail[128];
    snprintf(detail, sizeof(detail), "%d of %d glyphs carry pixels",
             glyphs_with_pixels, GBL_FONT_GLYPHS);
    check(glyphs_with_pixels > 40, "8x8 font block 201", detail);

    /* Render a character sheet so the glyph mapping can be eyeballed. */
    clear_screen_raw();
    text_display_string("abcdefghijklmnopqrstuvwxyz", 0, 15, 1, 2);
    text_display_string("0123456789 !,-.:;?", 0, 14, 3, 2);
    text_display_string("the quick brown fox jumps over", 0, 10, 5, 2);
    text_display_string("the lazy dog. 1234567890", 0, 11, 6, 2);
    for (int c = 1; c < 16; c++) {
        text_display_char('#', 2, 0, c, 9, 2 + (c * 2));
    }
    frame_stats(&nonzero, &colors);
    dump(out_dir, "font.ppm");

    snprintf(detail, sizeof(detail), "%d px drawn, %d colors -> font.ppm",
             nonzero, colors);
    check(nonzero > 500 && colors >= 12, "text rendering", detail);
}

static void check_credits(const char *out_dir)
{
    int nonzero = 0, colors = 0;
    char detail[128];

    clear_screen_raw();
    title_credits();
    frame_stats(&nonzero, &colors);
    dump(out_dir, "credits.ppm");

    snprintf(detail, sizeof(detail), "%d px drawn, %d colors -> credits.ppm",
             nonzero, colors);
    check(nonzero > 1500, "credits page", detail);
}

/* The 8x8 symbol banks and the borders built from them. The engine only loads
 * these once it enters a chapter, so the title sequence never exercises them. */
static void check_frames(const char *out_dir)
{
    int nonzero = 0, colors = 0;
    char detail[160];

    gbl.game_area = 1;
    if (!frames_load_8x8d(4, 0xca) || !frames_load_8x8d(0, 0xcb)) {
        check(false, "8x8 symbol banks", "8X8D1.DAX blocks 0xca/0xcb missing");
        return;
    }

    snprintf(detail, sizeof(detail), "bank 4: %d symbols, bank 0: %d symbols",
             gbl.symbol_8x8_set[4]->item_count,
             gbl.symbol_8x8_set[0]->item_count);
    check(gbl.symbol_8x8_set[4]->item_count > 0, "8x8 symbol banks", detail);

    clear_screen_raw();
    frames_draw_outer();
    frame_stats(&nonzero, &colors);
    dump(out_dir, "frame-outer.ppm");
    snprintf(detail, sizeof(detail), "%d px drawn, %d colors -> frame-outer.ppm",
             nonzero, colors);
    check(nonzero > 1000, "outer border", detail);

    clear_screen_raw();
    frames_draw_combat();
    frame_stats(&nonzero, &colors);
    dump(out_dir, "frame-combat.ppm");
    snprintf(detail, sizeof(detail), "%d px drawn, %d colors -> frame-combat.ppm",
             nonzero, colors);
    check(nonzero > 1000, "combat border", detail);
}

/* The palette is the one piece of display state that must round-trip: remapping
 * a slot has to change the rendered frame and restoring it has to undo that. */
static void check_palette(void)
{
    u32 before, remapped, restored;

    clear_screen_raw();
    for (int x = 0; x < 16; x++) {
        display_set_pixel(x, 0, 1);   /* a row of palette index 1 */
    }

    before = display_framebuffer()[0];
    display_set_ega_palette(1, 12);   /* point slot 1 at bright red */
    remapped = display_framebuffer()[0];
    display_reset_palette();
    display_set_ega_palette(1, 1);
    restored = display_framebuffer()[0];

    check(before != remapped && before == restored, "palette remap and restore",
          NULL);
}

static void check_masking(void)
{
    /* Masked art turns the mask colour into index 16, which display_set_pixel
     * refuses to write - that is how transparency happens. */
    DaxBlock *opaque = draw_load_dax(0, 0, 1, "Title");
    DaxBlock *masked = draw_load_dax(0, 1, 1, "Title");
    int mask_pixels = 0;
    char detail[128];

    if (!opaque || !masked) {
        check(false, "masked decode", "title block 1 missing");
        dax_block_free(opaque);
        dax_block_free(masked);
        return;
    }

    for (size_t i = 0; i < masked->data_size; i++) {
        if (masked->data[i] == COLOR_MASK) {
            mask_pixels++;
        }
    }

    int opaque_mask_pixels = 0;
    for (size_t i = 0; i < opaque->data_size; i++) {
        if (opaque->data[i] == COLOR_MASK) {
            opaque_mask_pixels++;
        }
    }

    snprintf(detail, sizeof(detail),
             "%d transparent px when masked on colour 0, %d when unmasked",
             mask_pixels, opaque_mask_pixels);
    check(mask_pixels > 0 && opaque_mask_pixels == 0, "transparency masking",
          detail);

    /* Drawing the masked copy must leave the pixels underneath alone. The block
     * covers the whole screen, so every transparent pixel in it - and only
     * those - should still show the background colour afterwards. The background
     * has to be a colour the picture itself never uses, or its own pixels would
     * be counted as survivors. */
    bool used[EGA_COLORS] = { false };
    int background = -1;

    for (size_t i = 0; i < masked->data_size; i++) {
        if (masked->data[i] < EGA_COLORS) {
            used[masked->data[i]] = true;
        }
    }
    for (int c = EGA_COLORS - 1; c >= 0 && background < 0; c--) {
        if (!used[c]) {
            background = c;
        }
    }
    if (background < 0) {
        check(false, "masked blit leaves background",
              "picture uses all 16 colors; no background left to detect");
        dax_block_free(opaque);
        dax_block_free(masked);
        return;
    }

    for (int y = 0; y < EGA_H; y++) {
        for (int x = 0; x < EGA_W; x++) {
            display_set_pixel(x, y, (u8)background);
        }
    }
    draw_picture(masked, 0, 0, 0);

    int survivors = 0;
    for (int y = 0; y < EGA_H; y++) {
        for (int x = 0; x < EGA_W; x++) {
            if (display_get_pixel(x, y) == background) {
                survivors++;
            }
        }
    }
    snprintf(detail, sizeof(detail), "%d background px showed through, %d expected",
             survivors, mask_pixels);
    check(survivors == mask_pixels, "masked blit leaves background", detail);

    dax_block_free(opaque);
    dax_block_free(masked);
}

/* ---------------------------------------------------------------- records --
 *
 * Save games and the shipped data files are read byte for byte, so a mistake in
 * a descriptor table is silent until a character loads wrong. These checks work
 * on the tables themselves: that no field runs past the end of its record or
 * overlaps another, and that a patterned record survives a read followed by a
 * write unchanged.
 */

/* Big enough for the largest record any descriptor table describes, which is an
 * area block at 0x800. */
#define RECORD_TEST_MAX 0x800

/* Marks the bytes a field covers, complaining if any is already taken. */
static bool cover(u8 *map, const DioDesc *desc, const DioField *f)
{
    for (size_t i = 0; i < f->span; i++) {
        size_t at = (size_t)f->rec + i;

        if (at >= desc->record_size) {
            printf("        %s.%s runs to %zu, past the %zu byte record\n",
                   desc->name, f->name, at, desc->record_size);
            return false;
        }
        if (map[at] != 0) {
            printf("        %s.%s overlaps another field at 0x%zx\n",
                   desc->name, f->name, at);
            return false;
        }
        map[at] = 1;
    }
    return true;
}

static void check_desc_layout(const DioDesc *desc, size_t expect_size)
{
    u8 map[RECORD_TEST_MAX] = { 0 };
    char detail[160];
    bool ok = true;
    int covered = 0;

    if (desc->record_size != expect_size || desc->record_size > sizeof(map)) {
        snprintf(detail, sizeof(detail), "record is 0x%zx, expected 0x%zx",
                 desc->record_size, expect_size);
        check(false, "record size", detail);
        return;
    }

    for (const DioField *f = desc->fields; f->type != DIO_END; f++) {
        if (f->type != DIO_CUSTOM && f->span != dio_span(f->type, f->count)) {
            printf("        %s.%s span %u disagrees with its type\n",
                   desc->name, f->name, f->span);
            ok = false;
        }
        if (!cover(map, desc, f)) {
            ok = false;
        }
    }
    for (size_t i = 0; i < desc->record_size; i++) {
        covered += map[i];
    }

    snprintf(detail, sizeof(detail),
             "%s: 0x%zx bytes, %d covered, %zu in pointer holes and padding",
             desc->name, desc->record_size, covered,
             desc->record_size - (size_t)covered);
    check(ok, "record layout", detail);
}

/* Reads a patterned record and writes it back out. Every byte a field covers
 * must come back identical, and dio_write must leave the bytes no field covers
 * exactly as it found them - that is what keeps the old far-pointer holes zero
 * in a save, since the real writers zero the buffer first.
 *
 * lossy names fields whose round trip is deliberately not byte-exact (Pascal
 * strings, clamped stats, the reversing spell list); those are checked
 * separately below. */
static void check_desc_round_trip(const DioDesc *desc, void *obj,
                                  const char *const *lossy, size_t lossy_count)
{
    u8 in[RECORD_TEST_MAX], out[RECORD_TEST_MAX];
    u8 untouched[RECORD_TEST_MAX], map[RECORD_TEST_MAX] = { 0 };
    char detail[160];
    bool ok = true;
    int skipped = 0;

    if (desc->record_size > sizeof(in)) {
        check(false, "record round trip", "record larger than the test buffer");
        return;
    }

    for (size_t i = 0; i < desc->record_size; i++) {
        /* Non-zero everywhere, so a field that silently fails to write shows up
         * as a zero rather than matching by luck. */
        in[i] = (u8)(i * 7 + 3);
    }
    memset(out, 0, sizeof(out));
    memset(untouched, 0xee, sizeof(untouched));

    if (!dio_read(desc, obj, in, desc->record_size, 0) ||
        !dio_write(desc, obj, out, desc->record_size) ||
        !dio_write(desc, obj, untouched, desc->record_size)) {
        check(false, "record round trip", desc->name);
        return;
    }

    for (const DioField *f = desc->fields; f->type != DIO_END; f++) {
        bool skip = false;

        for (size_t i = 0; i < lossy_count; i++) {
            if (strcmp(f->name, lossy[i]) == 0) {
                skip = true;
            }
        }
        for (size_t i = 0; i < f->span; i++) {
            map[f->rec + i] = 1;
        }
        if (skip) {
            skipped++;
            continue;
        }

        for (size_t i = 0; i < f->span; i++) {
            size_t at = (size_t)f->rec + i;
            bool same = f->type == DIO_BOOL ? ((in[at] != 0) == (out[at] != 0))
                                            : in[at] == out[at];

            if (!same) {
                printf("        %s.%s: 0x%zx read %02x, wrote %02x\n",
                       desc->name, f->name, at, in[at], out[at]);
                ok = false;
            }
        }
    }
    for (size_t i = 0; i < desc->record_size; i++) {
        if (map[i] == 0 && untouched[i] != 0xee) {
            printf("        %s: wrote %02x into the hole at 0x%zx\n",
                   desc->name, untouched[i], i);
            ok = false;
        }
    }

    snprintf(detail, sizeof(detail), "%s (%d lossy field%s checked separately)",
             desc->name, skipped, skipped == 1 ? "" : "s");
    check(ok, "record round trip", detail);
}

static void check_records(void)
{
    static const char *const player_lossy[] = { "name", "stats", "spell_list" };
    static const char *const item_lossy[]   = { "name" };
    Player p;
    Item it;
    Affect a;
    u8 buf[PLAYER_RECORD_SIZE];
    char detail[160];
    char name[ITEM_NAME_GEN_MAX];

    printf("records\n");

    check_desc_layout(&player_desc, PLAYER_RECORD_SIZE);
    check_desc_layout(&item_desc, ITEM_RECORD_SIZE);
    check_desc_layout(&affect_desc, AFFECT_RECORD_SIZE);

    player_init(&p);
    check_desc_round_trip(&player_desc, &p, player_lossy,
                          COAB_ARRAY_LEN(player_lossy));
    item_clear(&it);
    check_desc_round_trip(&item_desc, &it, item_lossy,
                          COAB_ARRAY_LEN(item_lossy));
    affect_init(&a, AFFECT_BLESS, 0, 0, false);
    check_desc_round_trip(&affect_desc, &a, NULL, 0);

    /* Field offsets, spot-checked against the DOS record so a table row that
     * moved shows up here and not in a corrupted save. */
    player_init(&p);
    memcpy(p.name, "Alias", 6);
    p.exp = 0x11223344;
    money_set(&p.money, MONEY_GOLD, 0x1234);
    p.class_level[SKILL_FIGHTER] = 9;
    stat_value_load(&p.stats.value[PSTAT_STR], 18);
    stat_value_load(&p.stats.value[PSTAT_STR00], 99);
    p.spell_cast_count[2][4] = 0x5a;
    p.hit_point_current = 0x2b;
    p.movement = 0x3c;

    if (!player_write(&p, buf, sizeof(buf))) {
        check(false, "player field offsets", "write failed");
    } else {
        bool ok = buf[0x00] == 5 && memcmp(buf + 1, "Alias", 5) == 0 &&
                  buf[0x10] == 18 && buf[0x11] == 18 &&
                  buf[0x1c] == 99 && buf[0x1d] == 99 &&
                  buf[0x101] == 0x34 && buf[0x102] == 0x12 &&
                  buf[0x10b] == 9 &&
                  buf[0x127] == 0x44 && buf[0x128] == 0x33 &&
                  buf[0x129] == 0x22 && buf[0x12a] == 0x11 &&
                  buf[0x13b] == 0x5a &&
                  buf[0x1a4] == 0x2b && buf[0x1a5] == 0x3c;

        check(ok, "player field offsets",
              "name 0x00, stats 0x10, money 0xfb, exp 0x127, hp 0x1a4");
    }

    /* The former far pointers must stay zero: the engine writes whole records
     * back to disk and a stale pointer value there is indistinguishable from
     * data. */
    {
        bool ok = true;

        for (size_t i = 0xf2; i < 0xf6; i++) {
            ok = ok && buf[i] == 0;
        }
        for (size_t i = 0x14c; i < 0x185; i++) {
            ok = ok && buf[i] == 0;
        }
        for (size_t i = 0x189; i < 0x191; i++) {
            ok = ok && buf[i] == 0;
        }
        check(ok, "player pointer holes are zero",
              "affects 0xf2, items 0x14c, readied 0x151, action 0x18d");
    }

    /* A name too long for the field is truncated to the capacity, not written
     * past it. */
    player_init(&p);
    memcpy(p.name, "Dragonbait of Tarsakh", 22);
    if (player_write(&p, buf, sizeof(buf))) {
        snprintf(detail, sizeof(detail), "length byte %u, capacity %d",
                 buf[0], PLAYER_NAME_MAX);
        check(buf[0] == PLAYER_NAME_MAX &&
              memcmp(buf + 1, "Dragonbait of T", PLAYER_NAME_MAX) == 0,
              "over-long name is truncated", detail);
    }

    /* Stats above 25 cannot be represented by the bonus tables, so a corrupt
     * record is clamped rather than passed through. */
    {
        u8 stats[PLAYER_STATS_RECORD_SIZE];

        memset(stats, 200, sizeof(stats));
        player_stats_dio_read(&p.stats, stats, 0);
        check(p.stats.value[PSTAT_STR].cur == 25 &&
              p.stats.value[PSTAT_CHA].full == 25,
              "out-of-range stats are clamped to 25", NULL);
    }

    /* Racial limits: a female dwarf cannot exceed Strength 17, and Con 3 is
     * raised to the dwarf minimum of 12. */
    player_stats_clear(&p.stats);
    stat_value_load(&p.stats.value[PSTAT_STR], 18);
    stat_value_load(&p.stats.value[PSTAT_CON], 3);
    player_stats_enforce_race_sex(&p.stats, RACE_DWARF, 1);
    snprintf(detail, sizeof(detail), "Str %d, Con %d",
             p.stats.value[PSTAT_STR].full, p.stats.value[PSTAT_CON].full);
    check(p.stats.value[PSTAT_STR].full == 17 &&
          p.stats.value[PSTAT_CON].full == 12,
          "race and sex stat limits", detail);

    /* Class minima: a paladin needs Cha 17. */
    player_stats_clear(&p.stats);
    player_stats_enforce_class(&p.stats, CLASS_PALADIN);
    check(p.stats.value[PSTAT_CHA].full == 17 &&
          p.stats.value[PSTAT_STR].full == 12,
          "class stat minima", "paladin: Cha 17, Str 12");

    /* A 400 year old dwarf has crossed four of the five brackets (50, 150, 250,
     * 350), so Strength has taken 0 +1 -1 -2 and Wisdom -1 +1 +1 +1. */
    player_stats_clear(&p.stats);
    stat_value_load(&p.stats.value[PSTAT_STR], 16);
    stat_value_load(&p.stats.value[PSTAT_WIS], 10);
    player_stats_age_effects(&p.stats, RACE_DWARF, 400);
    snprintf(detail, sizeof(detail), "dwarf at 400: Str %d, Wis %d",
             p.stats.value[PSTAT_STR].full, p.stats.value[PSTAT_WIS].full);
    check(p.stats.value[PSTAT_STR].full == 14 &&
          p.stats.value[PSTAT_WIS].full == 12, "ageing effects", detail);

    /* Money: 14 bytes of seven words. A gold piece is 200 coppers here and a
     * platinum 1000, so worth is 3250 coppers over 200, truncated. */
    {
        MoneySet m;
        u8 coins[MONEY_RECORD_SIZE];

        money_clear_all(&m);
        money_set(&m, MONEY_PLATINUM, 3);
        money_set(&m, MONEY_COPPER, 250);
        money_dio_write(&m, coins, 0);
        money_clear_all(&m);
        money_dio_read(&m, coins, 0);

        snprintf(detail, sizeof(detail), "3 pp + 250 cp = %d gp",
                 money_gold_worth(&m));
        check(money_get(&m, MONEY_PLATINUM) == 3 &&
              money_get(&m, MONEY_COPPER) == 250 &&
              money_gold_worth(&m) == 16, "money round trip", detail);
    }

    /* The spell list is written from the end of its block backwards, so a save
     * reverses the order. Reading back must still find every spell. */
    {
        SpellList sl;
        u8 block[SPELL_LIST_RECORD_SIZE];
        bool ok;

        spell_list_clear(&sl);
        spell_list_add_learnt(&sl, SPELL_BLESS);
        spell_list_add_learnt(&sl, SPELL_CURE_LIGHT_WOUNDS);
        spell_list_add_learnt(&sl, SPELL_MAGIC_MISSILE);
        spell_list_save(&sl, block, sizeof(block), 0);

        spell_list_clear(&sl);
        spell_list_load(&sl, block, sizeof(block), 0);

        ok = spell_list_count(&sl) == 3 &&
             spell_list_has_spell(&sl, SPELL_BLESS) &&
             spell_list_has_spell(&sl, SPELL_CURE_LIGHT_WOUNDS) &&
             spell_list_has_spell(&sl, SPELL_MAGIC_MISSILE);
        snprintf(detail, sizeof(detail), "%d spells back, first is id %d",
                 spell_list_count(&sl), sl.items[0].id);
        check(ok, "spell list round trip", detail);
    }

    /* Affects live in their own .fx stream, nine bytes each. */
    {
        u8 rec[AFFECT_RECORD_SIZE];
        Affect back;

        affect_init(&a, AFFECT_BLESS, 600, 0x11, true);
        check(affect_write(&a, rec, sizeof(rec)) &&
              affect_read(&back, rec, sizeof(rec), 0) &&
              back.type == (int)AFFECT_BLESS && back.minutes == 600 &&
              back.affect_data == 0x11 && back.call_affect_table,
              "affect round trip", "9 byte record");
    }

    /* Item names are assembled from three byte indices into a 256-entry table,
     * with the plural landing on whichever part is the noun. */
    item_clear(&it);
    it.type = ITEM_LONG_SWORD;
    it.namenum3 = 0x24;             /* "Long Sword" */
    it.namenum2 = 0xa2;             /* "+1" */
    it.count = 1;
    check(strcmp(item_generate_name(&it, 0, name, sizeof(name)),
                 "Long Sword +1") == 0, "item name, singular",
          item_generate_name(&it, 0, name, sizeof(name)));

    item_clear(&it);
    it.type = ITEM_ARROW;
    it.namenum3 = 0x3d;             /* "Arrow" */
    it.namenum2 = 0xa2;             /* "+1" */
    it.count = 20;
    check(strcmp(item_generate_name(&it, 0, name, sizeof(name)),
                 "Arrows +1") == 0, "item name, plural",
          item_generate_name(&it, 0, name, sizeof(name)));

    /* hidden_names_flag bit 1 hides the second part until it is identified. */
    check(strcmp(item_generate_name(&it, 2, name, sizeof(name)),
                 "Arrows") == 0, "item name, unidentified",
          item_generate_name(&it, 2, name, sizeof(name)));

    /* Item records are 0x3F bytes and the item stream in a save is a bare run
     * of them. */
    {
        u8 rec[ITEM_RECORD_SIZE];
        Item back;

        item_init(&it, ITEM_LONG_SWORD, 0, 0xa2, 0x24, 1, 0, true, 0, false,
                  60, 1, 30, AFFECT_BLESS, 0, 0);
        check(item_write(&it, rec, sizeof(rec)) &&
              item_read(&back, rec, sizeof(rec), 0) &&
              item_equals(&it, &back),
              "item round trip", "0x3f byte record");
    }

    /* The pack is a fixed 16 slots; readied items are held as indices, so
     * removing an item has to shift them. */
    player_init(&p);
    {
        int idx[3];
        bool ok = true;

        for (int i = 0; i < 3; i++) {
            item_init(&it, ITEM_DAGGER, 0, 0, 8, (i8)i, 0, false, 0, false,
                      10, 1, 2, 0, 0, 0);
            idx[i] = player_item_add(&p, &it);
        }
        player_ready_set(&p, (ItemSlot)0, idx[2]);
        player_item_remove(&p, idx[0]);

        ok = ok && p.item_count == 2;
        ok = ok && p.ready[0] == 1;                       /* shifted down */
        ok = ok && player_primary_weapon(&p) != NULL &&
                   player_primary_weapon(&p)->plus == 2;  /* still the same one */

        player_item_remove(&p, 1);
        ok = ok && p.ready[0] == ITEM_SLOT_NONE;           /* it was removed */

        check(ok, "readied slots follow the pack",
              "removing an item shifts the indices above it");
    }

    /* The item table is real shipped data - 2 header bytes then 0x81 rows of
     * 0x10 - so this checks the loader against the file rather than a fixture.
     * program_init_first loads it in a real run; here it is loaded early because
     * everything below needs an item to have a price. */
    if (!item_data_table_load("ITEMS")) {
        check(false, "item table loads", "ITEMS could not be read");
    } else {
        const ItemData *sword = item_data(ITEM_LONG_SWORD);
        const ItemData *bow   = item_data(ITEM_LONG_BOW);
        const ItemData *shield = item_data(ITEM_SHIELD);
        const ItemData *potion = item_data(ITEM_POTION);

        snprintf(detail, sizeof(detail),
                 "long sword 1d%u one-handed, long bow range %d two-handed",
                 sword->dice_size_normal, bow->range);
        check(sword->hands_count == 1 && sword->dice_count_normal == 1 &&
              sword->dice_size_normal == 8 && sword->dice_size_large == 12 &&
              sword->range == 0 &&
              bow->hands_count == 2 && bow->range == 22 &&
              (bow->flags & ITEM_DATA_ARROWS) != 0 &&
              shield->slot == ITEM_SLOT_1 && potion->slot == ITEM_SLOT_10,
              "item table loads", detail);

        item_clear(&it);
        it.type = ITEM_LONG_BOW;
        check(item_is_ranged(&it) && item_hands_count(&it) == 2,
              "item table drives item behaviour", "a long bow is ranged");

        /* A type outside the table must answer inertly, not read past it. */
        it.type = 250;
        check(!item_is_ranged(&it) && item_hands_count(&it) == 0,
              "out-of-range item type is inert", "type 250");
    }

    /* MaxItems is 16 and the seventeenth add has to fail rather than overrun. */
    player_init(&p);
    {
        int last = 0;

        item_clear(&it);
        for (int i = 0; i < PLAYER_MAX_ITEMS + 1; i++) {
            last = player_item_add(&p, &it);
        }
        snprintf(detail, sizeof(detail), "%d items held, add returned %d",
                 p.item_count, last);
        check(p.item_count == PLAYER_MAX_ITEMS && last == -1,
              "pack holds no more than MaxItems", detail);
    }

    printf("\n");
}

/* --------------------------------------------------------------- game state --
 *
 * The area blocks are reached two ways at once - by name from C and by numeric
 * offset from an ECL script - so these check that both views see the same bytes,
 * that an offset nothing has named still survives a save, and that an offset
 * outside the block is refused instead of overrunning it.
 */
static void check_area_blocks(void)
{
    Area1 a1;
    Area2 a2;
    u8    buf[AREA_BLOCK_SIZE];
    char  detail[160];

    printf("game state\n");

    check_desc_layout(&area1_desc, AREA_BLOCK_SIZE);
    check_desc_layout(&area2_desc, AREA_BLOCK_SIZE);

    area1_clear(&a1);
    check_desc_round_trip(&area1_desc, &a1, NULL, 0);
    area2_clear(&a2);
    check_desc_round_trip(&area2_desc, &a2, NULL, 0);

    /* A named offset: the hour of the day is word 0x192, and an ECL write to it
     * has to land on the field the engine reads. */
    area1_clear(&a1);
    area1_word_set(&a1, 0x192, 7);
    snprintf(detail, sizeof(detail), "0x192 -> time_hour %u", a1.time_hour);
    check(a1.time_hour == 7 && area1_word_get(&a1, 0x192) == 7,
          "ECL offset reaches the named field", detail);

    /* ECL offsets arrive as 16-bit segment arithmetic and wrap, so the high bits
     * are masked off before the field is looked up. */
    area1_word_set(&a1, 0x10192, 9);
    check(a1.time_hour == 9, "ECL offset is masked to 16 bits",
          "0x10192 is 0x192");

    /* An offset no field names falls back to the block's raw bytes, and those
     * bytes have to come back out in a save. */
    area1_word_set(&a1, 0x600, 0xbeef);
    memset(buf, 0, sizeof(buf));
    check(area1_word_get(&a1, 0x600) == 0xbeef &&
          area1_write(&a1, buf, sizeof(buf)) &&
          buf[0x600] == 0xef && buf[0x601] == 0xbe && buf[0x192] == 9,
          "unnamed offsets survive a save",
          "raw 0x600 kept, named 0x192 written");

    /* The last word in the block is 0x7fe; 0x7ff would run off the end. */
    check(area1_word_get(&a1, 0x7ff) == 0, "offset past the block is refused",
          "0x7ff needs two bytes and only one is left");

    /* Area2's isDuel is at 0x5cc. The C# had its [DataOffset] commented out, so
     * it read and wrote at 0x5cc but was dropped from every save. */
    area2_clear(&a2);
    area2_word_set(&a2, 0x5cc, 1);
    memset(buf, 0, sizeof(buf));
    check(a2.is_duel && area2_write(&a2, buf, sizeof(buf)) && buf[0x5cc] == 1,
          "Area2.isDuel is saved", "0x5cc, which the C# dropped");

    printf("\n");
}

/* -------------------------------------------------------------------- ECL --
 *
 * The memory the interpreter works in and the operand decoder, both places where
 * a bad script could overrun a buffer rather than just misbehave. The interpreter
 * on top of them is check_eclvm, below.
 */
static u16 test_ecl_memory(u16 loc)
{
    return (u16)(loc + 0x100);
}

static void check_ecl(void)
{
    EclBlock b;
    EclVars  v;
    EclOp    op;
    char     detail[160];

    printf("ECL\n");

    ecl_block_clear(&b);
    ecl_block_set(&b, ECL_BLOCK_SIZE - 1, 0x5a);
    ecl_block_set(&b, 0x10000 + 4, 0x27);        /* wraps to 4 */
    ecl_block_set(&b, ECL_BLOCK_SIZE, 0xff);     /* refused */

    snprintf(detail, sizeof(detail), "last byte %02x, wrapped byte %02x",
             ecl_block_get(&b, ECL_BLOCK_SIZE - 1), ecl_block_get(&b, 4));
    check(ecl_block_get(&b, ECL_BLOCK_SIZE - 1) == 0x5a &&
          ecl_block_get(&b, 4) == 0x27 &&
          ecl_block_get(&b, ECL_BLOCK_SIZE) == 0,
          "ECL block addressing", detail);

    ecl_vars_clear(&v);
    ecl_vars_set(&v, 0x3fe, 0x1234);
    ecl_vars_set(&v, 0x3ff, 0xffff);             /* one byte short, refused */
    check(ecl_vars_get(&v, 0x3fe) == 0x1234 && ecl_vars_get(&v, 0x3ff) == 0,
          "ECL vars addressing", "word 0x3fe fits, 0x3ff does not");

    /* Operands: code 0 is an immediate byte, 2 an immediate word, and 1, 3 and
     * 0x80 read the word as an address. */
    ecl_op_clear(&op);
    op.get_memory_value = test_ecl_memory;
    ecl_op_set_code(&op, 0x00);
    ecl_op_set_low(&op, 0x42);
    check(ecl_op_value(&op) == 0x42, "ECL operand, immediate byte", "code 0");

    ecl_op_clear(&op);
    ecl_op_set_code(&op, 0x02);
    ecl_op_set_low(&op, 0x34);
    ecl_op_set_high(&op, 0x12);
    check(ecl_op_value(&op) == 0x1234 && ecl_op_word(&op) == 0x1234,
          "ECL operand, immediate word", "code 2");

    ecl_op_clear(&op);
    ecl_op_set_code(&op, 0x80);
    ecl_op_set_low(&op, 0x00);
    ecl_op_set_high(&op, 0x02);
    check(ecl_op_value(&op) == 0x300, "ECL operand, memory read",
          "code 0x80 through the reader");

    /* Setting a byte twice was an assertion failure in the C#; here it is
     * refused and logged, and the first value stands. */
    check(!ecl_op_set_low(&op, 0x99) && ecl_op_low(&op) == 0x00,
          "an operand byte cannot be set twice", NULL);

    /* A code that is not an operand form, and a word that was never completed,
     * both read as zero instead of throwing. */
    ecl_op_clear(&op);
    ecl_op_set_code(&op, 0x7f);
    check(ecl_op_value(&op) == 0 && ecl_op_word(&op) == 0,
          "an unknown operand code reads as zero", "code 0x7f");

    printf("\n");
}

/* --------------------------------------------------------------- the ECL VM --
 *
 * Real scripts are what the interpreter is for, but a script off the disk runs
 * straight into the menus and the dungeon view, so these tests hand-assemble tiny
 * ones instead. Everything below is a script the interpreter is made to run for
 * real - through eclvm_run, one opcode byte at a time, decoding its own operands -
 * and then the answers it left in the machine's memory are read back.
 *
 * Scripts live at ECL offsets 0x8000 and up, which is the one range that lands
 * inside the block: the interpreter reads its opcode from ecl_ptr[offset+0x8000],
 * and 0x8000+0x8000 wraps to 0. The variables are the scratch space at 0x7A00.
 */
static u16 test_asm_at;

/* Empties the block and puts the write cursor at `offset`. */
static void test_asm_reset(u16 offset)
{
    ecl_block_clear(gbl.ecl_ptr);
    test_asm_at = offset;
}

/* Moves the cursor without clearing, for assembling a second piece of the same
 * script - a subroutine, or the far side of a jump. */
static void test_asm_seek(u16 offset)
{
    test_asm_at = offset;
}

static void test_asm_byte(u8 value)
{
    ecl_block_set(gbl.ecl_ptr, test_asm_at + 0x8000, value);
    test_asm_at = (u16)(test_asm_at + 1);
}

/* The three operand forms the tests need: an immediate byte, an immediate word,
 * and an address - which is both how a value is read out of memory and how a
 * destination is named, since a destination's word is all the handler wants. */
static void test_asm_imm(u8 value)
{
    test_asm_byte(0x00);
    test_asm_byte(value);
}

static void test_asm_word(u16 value)
{
    test_asm_byte(0x02);
    test_asm_byte((u8)(value & 0xff));
    test_asm_byte((u8)(value >> 8));
}

static void test_asm_addr(u16 loc)
{
    test_asm_byte(0x01);
    test_asm_byte((u8)(loc & 0xff));
    test_asm_byte((u8)(loc >> 8));
}

/* A string operand: the packer's own output, inline in the script. */
static void test_asm_str(const char *text)
{
    u8     packed[0x80];
    size_t n = vm_compress_string(packed, sizeof(packed), text);

    test_asm_byte(0x80);
    test_asm_byte((u8)n);
    for (size_t i = 0; i < n; i++) {
        test_asm_byte(packed[i]);
    }
}

/* Scratch cell `n`, which is one word of EclVars. */
static u16 test_var(int n)
{
    return (u16)(0x7a00 + n);
}

static void check_eclvm(void)
{
    char detail[240];
    u16  entry = 0x8100;
    bool old_print = gbl.print_commands;

    printf("ECL VM\n");

    gbl.print_commands = false;
    ecl_vars_clear(gbl.ecl_vars);
    gbl.party_killed = false;
    gbl_vm_call_stack_clear();

    /* --- the instruction set as a table --- */

    snprintf(detail, sizeof(detail), "0x01 %s/%d, 0x29 %s/%d",
             eclvm_command_name(0x01), eclvm_command_size(0x01),
             eclvm_command_name(0x29), eclvm_command_size(0x29));
    check(strcmp(eclvm_command_name(0x00), "EXIT") == 0 &&
          strcmp(eclvm_command_name(0x01), "GOTO") == 0 &&
          eclvm_command_size(0x01) == 1 &&
          strcmp(eclvm_command_name(0x29), "ENCOUNTER MENU") == 0 &&
          eclvm_command_size(0x29) == 14,
          "the command table is indexed by the opcode", detail);

    /* 0x1F is in the table but has no handler, and 0x41 is not an opcode. */
    check(eclvm_command_known(0x00) && eclvm_command_known(0x40) &&
          !eclvm_command_known(0x1f) && eclvm_command_name(0x1f) != NULL &&
          eclvm_command_name(ECLVM_COMMAND_COUNT) == NULL &&
          eclvm_command_size(ECLVM_COMMAND_COUNT) == -1 &&
          !eclvm_command_known(-1),
          "every opcode but 0x1f has a handler", "0x41 is not an opcode at all");

    /* --- arithmetic, and where the answers go --- */

    test_asm_reset(entry);

    test_asm_byte(0x09);                                /* SAVE 7 -> var 0 */
    test_asm_imm(7);
    test_asm_addr(test_var(0));

    test_asm_byte(0x09);                                /* SAVE 3 -> var 1 */
    test_asm_imm(3);
    test_asm_addr(test_var(1));

    test_asm_byte(0x04);                                /* ADD */
    test_asm_addr(test_var(0));
    test_asm_addr(test_var(1));
    test_asm_addr(test_var(2));

    test_asm_byte(0x05);                                /* SUBTRACT: B - A */
    test_asm_addr(test_var(1));
    test_asm_addr(test_var(0));
    test_asm_addr(test_var(3));

    test_asm_byte(0x07);                                /* MULTIPLY */
    test_asm_addr(test_var(0));
    test_asm_addr(test_var(1));
    test_asm_addr(test_var(4));

    test_asm_byte(0x06);                                /* DIVIDE by zero */
    test_asm_imm(5);
    test_asm_imm(0);
    test_asm_addr(test_var(6));

    /* Last, because the remainder goes to a single cell that the next division
     * overwrites - which is exactly how a script has to read it. */
    test_asm_byte(0x06);                                /* DIVIDE: A / B */
    test_asm_imm(7);
    test_asm_imm(2);
    test_asm_addr(test_var(5));

    test_asm_byte(0x00);                                /* EXIT */

    eclvm_run(entry);

    snprintf(detail, sizeof(detail), "7+3=%u, 7-3=%u, 7*3=%u, 7/2=%u rem %d",
             vm_get_memory_value(test_var(2)), vm_get_memory_value(test_var(3)),
             vm_get_memory_value(test_var(4)), vm_get_memory_value(test_var(5)),
             (int)gbl.area2_ptr->field_67E);
    check(vm_get_memory_value(test_var(0)) == 7 &&
          vm_get_memory_value(test_var(1)) == 3 &&
          vm_get_memory_value(test_var(2)) == 10 &&
          vm_get_memory_value(test_var(3)) == 4 &&
          vm_get_memory_value(test_var(4)) == 21 &&
          vm_get_memory_value(test_var(5)) == 3 &&
          gbl.area2_ptr->field_67E == 1,
          "a script does its arithmetic in the scratch space", detail);

    /* The C# would have thrown here and the DOS build taken a divide-by-zero
     * interrupt; the script carries on with a zero instead. */
    check(vm_get_memory_value(test_var(6)) == 0,
          "dividing by zero answers zero", "and warns rather than trapping");

    /* --- comparing, jumping, and the call stack ---
     *
     * Every conditional jump runs the one instruction after it when its flag is
     * set and steps over that instruction when it is not, so a script's "then"
     * branch is one instruction and usually a GOTO. Both directions are tested,
     * because a bug in the skip would show up as the wrong branch being taken and
     * not as a crash.
     */

    ecl_vars_clear(gbl.ecl_vars);
    test_asm_reset(entry);

    test_asm_byte(0x03);                                /* COMPARE 5, 5 */
    test_asm_imm(5);
    test_asm_imm(5);
    test_asm_byte(0x16);                                /* IF =   -> taken */
    test_asm_byte(0x01);                                /*   GOTO 0x8140 */
    test_asm_word(0x8140);
    test_asm_byte(0x09);                                /* not reached */
    test_asm_imm(0xee);
    test_asm_addr(test_var(8));
    test_asm_byte(0x00);                                /* EXIT */

    test_asm_seek(0x8140);
    test_asm_byte(0x03);                                /* COMPARE 5, 9 */
    test_asm_imm(5);
    test_asm_imm(9);
    test_asm_byte(0x16);                                /* IF =   -> skipped */
    test_asm_byte(0x01);                                /*   GOTO 0x8180 */
    test_asm_word(0x8180);
    test_asm_byte(0x17);                                /* IF <>  -> taken */
    test_asm_byte(0x02);                                /*   GOSUB 0x81c0 */
    test_asm_word(0x81c0);
    test_asm_byte(0x09);                                /* SAVE 0x22 -> var 10 */
    test_asm_imm(0x22);
    test_asm_addr(test_var(10));
    test_asm_byte(0x00);                                /* EXIT */

    test_asm_seek(0x8180);                              /* the branch not taken */
    test_asm_byte(0x09);
    test_asm_imm(0xee);
    test_asm_addr(test_var(8));
    test_asm_byte(0x00);                                /* EXIT */

    test_asm_seek(0x81c0);                              /* the subroutine */
    test_asm_byte(0x09);                                /* SAVE 0x11 -> var 9 */
    test_asm_imm(0x11);
    test_asm_addr(test_var(9));
    test_asm_byte(0x13);                                /* RETURN */

    eclvm_run(entry);

    snprintf(detail, sizeof(detail), "wrong branch %u, subroutine %u, after %u",
             vm_get_memory_value(test_var(8)), vm_get_memory_value(test_var(9)),
             vm_get_memory_value(test_var(10)));
    check(vm_get_memory_value(test_var(8)) == 0 &&
          vm_get_memory_value(test_var(9)) == 0x11 &&
          vm_get_memory_value(test_var(10)) == 0x22 &&
          gbl.vm_call_depth == 0,
          "the conditional jumps take the branch the comparison chose", detail);

    /* RETURN with nothing on the call stack ends the script, which is how a
     * handler reached by a jump rather than a GOSUB finishes. */
    ecl_vars_clear(gbl.ecl_vars);
    test_asm_reset(entry);
    test_asm_byte(0x13);                                /* RETURN */
    test_asm_byte(0x09);                                /* not reached */
    test_asm_imm(0xee);
    test_asm_addr(test_var(0));

    eclvm_run(entry);
    check(vm_get_memory_value(test_var(0)) == 0 && gbl.vm_call_depth == 0,
          "returning with an empty call stack is an exit",
          "the instruction after it never runs");

    /* --- the jump table ---
     *
     * ON GOTO's two fixed operands are the index and how many addresses follow,
     * and the addresses are decoded as that many more operands - so they end up
     * numbered from 1 and the index picks one directly. An index past the end
     * falls through to the instruction after the table.
     */
    ecl_vars_clear(gbl.ecl_vars);
    test_asm_reset(entry);
    test_asm_byte(0x25);                                /* ON GOTO */
    test_asm_imm(1);                                    /*   index 1 */
    test_asm_imm(3);                                    /*   of three targets */
    test_asm_word(0x8200);
    test_asm_word(0x8240);
    test_asm_word(0x8280);
    test_asm_byte(0x00);                                /* EXIT */

    for (int i = 0; i < 3; i++) {
        test_asm_seek((u16)(0x8200 + (i * 0x40)));
        test_asm_byte(0x09);
        test_asm_imm((u8)(0x70 + i));
        test_asm_addr(test_var(12));
        test_asm_byte(0x00);                            /* EXIT */
    }

    eclvm_run(entry);
    snprintf(detail, sizeof(detail), "index 1 of 3 reached target %u",
             vm_get_memory_value(test_var(12)) - 0x70);
    check(vm_get_memory_value(test_var(12)) == 0x71,
          "ON GOTO picks the address the index names", detail);

    /* The same table with the index past its end: nothing jumps, and the byte
     * after the table runs. */
    ecl_vars_clear(gbl.ecl_vars);
    ecl_block_set(gbl.ecl_ptr, entry + 0x8000 + 2, 9);  /* index := 9 */
    eclvm_run(entry);
    check(vm_get_memory_value(test_var(12)) == 0,
          "an index past the end of the table falls through", "index 9 of 3");

    /* --- bit tests, and the table instructions --- */

    ecl_vars_clear(gbl.ecl_vars);
    test_asm_reset(entry);

    test_asm_byte(0x2f);                                /* AND 0x0c, 0x04 */
    test_asm_imm(0x0c);
    test_asm_imm(0x04);
    test_asm_addr(test_var(0));
    test_asm_byte(0x17);                                /* IF <> (against zero) */
    test_asm_byte(0x09);                                /*   SAVE 1 -> var 1 */
    test_asm_imm(1);
    test_asm_addr(test_var(1));

    test_asm_byte(0x30);                                /* OR 0x30, 0x03 */
    test_asm_imm(0x30);
    test_asm_imm(0x03);
    test_asm_addr(test_var(2));

    test_asm_byte(0x2f);                                /* AND 0x0c, 0x01 -> 0 */
    test_asm_imm(0x0c);
    test_asm_imm(0x01);
    test_asm_addr(test_var(3));
    test_asm_byte(0x17);                                /* IF <> -> skipped */
    test_asm_byte(0x09);                                /*   not reached */
    test_asm_imm(0xee);
    test_asm_addr(test_var(4));

    /* GETTABLE reads base+index out of the script's own bytes, which is how a
     * script indexes a table it keeps after its code. */
    test_asm_byte(0x2a);                                /* GETTABLE 0x8300+2 */
    test_asm_word(0x8300);
    test_asm_imm(2);
    test_asm_addr(test_var(5));

    test_asm_byte(0x35);                                /* SAVE TABLE 0x5a -> */
    test_asm_imm(0x5a);                                 /*   0x8300 + 3 */
    test_asm_word(0x8300);
    test_asm_imm(3);

    test_asm_byte(0x00);                                /* EXIT */

    test_asm_seek(0x8300);                              /* the table itself */
    test_asm_byte(0xa0);
    test_asm_byte(0xa1);
    test_asm_byte(0xa2);
    test_asm_byte(0xa3);

    eclvm_run(entry);

    snprintf(detail, sizeof(detail),
             "0x0c&0x04=%u, 0x30|0x03=%u, table[2]=0x%02x, table[3]=0x%02x",
             vm_get_memory_value(test_var(0)), vm_get_memory_value(test_var(2)),
             vm_get_memory_value(test_var(5)),
             ecl_block_get(gbl.ecl_ptr, 0x8300 + 3 + 0x8000));
    check(vm_get_memory_value(test_var(0)) == 0x04 &&
          vm_get_memory_value(test_var(1)) == 1 &&
          vm_get_memory_value(test_var(2)) == 0x33 &&
          vm_get_memory_value(test_var(3)) == 0 &&
          vm_get_memory_value(test_var(4)) == 0 &&
          vm_get_memory_value(test_var(5)) == 0xa2 &&
          ecl_block_get(gbl.ecl_ptr, 0x8300 + 3 + 0x8000) == 0x5a,
          "AND and OR also compare against zero, and the tables index",
          detail);

    /* --- a script that has gone off the rails ---
     *
     * The C# logged an opcode it had no handler for and went round again on the
     * same byte, which hung the game on it forever; opcode 0x1f, whose handler it
     * left null, would have thrown. Here both step forward, so the script walks on
     * and finds the EXIT that is coming.
     */
    ecl_vars_clear(gbl.ecl_vars);
    test_asm_reset(entry);
    test_asm_byte(0xfe);                                /* not an opcode */
    test_asm_byte(0x1f);                                /* no handler, 2 operands */
    test_asm_imm(1);
    test_asm_imm(2);
    test_asm_byte(0x09);                                /* SAVE 0x44 -> var 0 */
    test_asm_imm(0x44);
    test_asm_addr(test_var(0));
    test_asm_byte(0x00);                                /* EXIT */

    eclvm_run(entry);
    check(vm_get_memory_value(test_var(0)) == 0x44,
          "an opcode with no handler is stepped over, not looped on",
          "0xfe and 0x1f, then the script carries on");

    /* --- text --- */

    ecl_vars_clear(gbl.ecl_vars);
    test_asm_reset(entry);
    test_asm_byte(0x09);                       /* SAVE "AZURE BONDS" -> var 0 */
    test_asm_str("AZURE BONDS");
    test_asm_addr(test_var(0));
    test_asm_byte(0x03);                       /* COMPARE it against itself */
    test_asm_byte(0x81);                       /*   a string named by address */
    test_asm_byte((u8)(test_var(0) & 0xff));
    test_asm_byte((u8)(test_var(0) >> 8));
    test_asm_str("AZURE BONDS");
    test_asm_byte(0x16);                                /* IF = */
    test_asm_byte(0x09);                                /*   SAVE 1 -> var 20 */
    test_asm_imm(1);
    test_asm_addr(test_var(20));
    test_asm_byte(0x00);                                /* EXIT */

    eclvm_run(entry);

    /* The round trip is the point: the packer wrote six bits a character into the
     * script, the decoder read it back, SAVE spread it one character to a word
     * through the scratch space, and COMPARE read it out again. */
    snprintf(detail, sizeof(detail), "'%s' packed, stored and compared equal",
             gbl_ecl_string(1));
    check(vm_get_memory_value(test_var(20)) == 1,
          "a packed string survives the trip through memory", detail);

    /* --- what the script can ask about the party --- */

    {
        Player p1;
        Player p2;
        Item   ring;
        int    old_count = gbl.team_count;
        Player *old_selected = gbl.selected_player;

        player_init(&p1);
        player_init(&p2);
        snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
        snprintf(p2.name, sizeof(p2.name), "%s", "Dimswart");

        p1.cls = CLASS_THIEF;
        p2.cls = CLASS_RANGER;             /* the one class that sees an ambush */
        p1.movement = 9;
        p2.movement = 12;
        p1.thief_skills[0] = 30;
        p2.thief_skills[0] = 50;
        p1.hit_point_current = p1.hit_point_max = 8;
        p2.hit_point_current = p2.hit_point_max = 10;

        memset(&ring, 0, sizeof(ring));
        ring.type = ITEM_RING_OF_PROT;
        player_item_add(&p2, &ring);

        spell_list_clear(&p1.spell_list);
        spell_list_add_learn(&p1.spell_list, 4);
        spell_list_add_learn(&p1.spell_list, 11);

        gbl.team_count = 0;
        gbl_team_add(&p1);
        gbl_team_add(&p2);
        gbl.selected_player = &p1;

        ecl_vars_clear(gbl.ecl_vars);
        test_asm_reset(entry);

        /* CHECKPARTY asks one question about everybody: 0xA5 is the first thief
         * skill, and the answers are its lowest, highest and average. The address
         * is biased down by 0x7fff like everything else the machine addresses. */
        test_asm_byte(0x1e);
        test_asm_word((u16)(0x7fff + 0xa5));
        test_asm_imm(0);
        test_asm_addr(test_var(0));                     /* lowest */
        test_asm_addr(test_var(1));                     /* highest */
        test_asm_addr(test_var(2));                     /* average */
        test_asm_addr(test_var(3));                     /* the affect answer */

        /* The same instruction on 0x9F, which is initiative rather than a skill
         * and the only other address it answers. */
        test_asm_byte(0x1e);
        test_asm_word((u16)(0x7fff + 0x9f));
        test_asm_imm(0);
        test_asm_addr(test_var(14));                    /* lowest */
        test_asm_addr(test_var(15));                    /* highest */
        test_asm_addr(test_var(16));                    /* average */
        test_asm_addr(test_var(17));

        test_asm_byte(0x22);                            /* PARTY SURPRISE */
        test_asm_addr(test_var(4));
        test_asm_addr(test_var(5));

        test_asm_byte(0x1d);                            /* PARTYSTRENGTH */
        test_asm_addr(test_var(6));

        test_asm_byte(0x32);                            /* FIND ITEM: the ring */
        test_asm_imm((u8)ITEM_RING_OF_PROT);
        test_asm_byte(0x16);                            /* IF = */
        test_asm_byte(0x09);                            /*   SAVE 1 -> var 7 */
        test_asm_imm(1);
        test_asm_addr(test_var(7));

        test_asm_byte(0x32);                            /* FIND ITEM: a sword */
        test_asm_imm((u8)ITEM_LONG_SWORD);
        test_asm_byte(0x16);                            /* IF = -> skipped */
        test_asm_byte(0x09);
        test_asm_imm(0xee);
        test_asm_addr(test_var(8));

        test_asm_byte(0x3b);                            /* SPELL: who has 11? */
        test_asm_imm(11);
        test_asm_addr(test_var(9));                     /* which of theirs */
        test_asm_addr(test_var(10));                    /* and who */

        test_asm_byte(0x3b);                            /* SPELL: nobody has 99 */
        test_asm_imm(99);
        test_asm_addr(test_var(11));
        test_asm_addr(test_var(12));

        test_asm_byte(0x08);                            /* RANDOM 0..5 -> var 13 */
        test_asm_imm(5);
        test_asm_addr(test_var(13));

        test_asm_byte(0x40);                            /* DESTROY ITEMS: rings */
        test_asm_imm((u8)ITEM_RING_OF_PROT);

        test_asm_byte(0x00);                            /* EXIT */

        eclvm_run(entry);

        snprintf(detail, sizeof(detail), "lowest %u, highest %u, average %u",
                 vm_get_memory_value(test_var(0)),
                 vm_get_memory_value(test_var(1)),
                 vm_get_memory_value(test_var(2)));
        check(vm_get_memory_value(test_var(0)) == 30 &&
              vm_get_memory_value(test_var(1)) == 50 &&
              vm_get_memory_value(test_var(2)) == 40,
              "CHECKPARTY answers the lowest, highest and average", detail);

        snprintf(detail, sizeof(detail), "lowest %u, highest %u, average %u",
                 vm_get_memory_value(test_var(14)),
                 vm_get_memory_value(test_var(15)),
                 vm_get_memory_value(test_var(16)));
        check(vm_get_memory_value(test_var(14)) == 9 &&
              vm_get_memory_value(test_var(15)) == 12 &&
              vm_get_memory_value(test_var(16)) == 10,
              "CHECKPARTY on 0x9F asks about initiative instead", detail);

        check(vm_get_memory_value(test_var(4)) == 1 &&
              vm_get_memory_value(test_var(5)) == 0,
              "PARTY SURPRISE finds the ranger", "and its second answer is "
              "always zero, as the original wrote it");

        /* Two low level characters with no armour and no spells, so the only term
         * left is their hit points: (8 + 10) / 10, a character at a time. */
        snprintf(detail, sizeof(detail), "%u",
                 vm_get_memory_value(test_var(6)));
        check(vm_get_memory_value(test_var(6)) == 1,
              "PARTYSTRENGTH adds up what the party can do", detail);

        check(vm_get_memory_value(test_var(7)) == 1 &&
              vm_get_memory_value(test_var(8)) == 0,
              "FIND ITEM searches every pack in the party",
              "the ring is there, a long sword is not");

        snprintf(detail, sizeof(detail),
                 "spell 11 is character %u's number %u; spell 99 is %u",
                 vm_get_memory_value(test_var(10)),
                 vm_get_memory_value(test_var(9)),
                 vm_get_memory_value(test_var(11)));
        check(vm_get_memory_value(test_var(9)) == 2 &&
              vm_get_memory_value(test_var(10)) == 0 &&
              vm_get_memory_value(test_var(11)) == 0xff,
              "SPELL finds who has one memorized", detail);

        check(vm_get_memory_value(test_var(13)) <= 5,
              "RANDOM stays inside the range it was given", "0 to 5 inclusive");

        check(p2.item_count == 0,
              "DESTROY ITEMS takes the item off everybody", "the ring is gone");

        /* An empty party used to divide by the number of characters in it. */
        gbl.team_count = 0;
        ecl_vars_clear(gbl.ecl_vars);
        eclvm_run(entry);
        check(vm_get_memory_value(test_var(2)) == 0 &&
              vm_get_memory_value(test_var(16)) == 0 &&
              vm_get_memory_value(test_var(6)) == 0,
              "CHECKPARTY over an empty party answers zero",
              "rather than dividing by nobody");

        gbl.team_count      = old_count;
        gbl.selected_player = old_selected;
    }

    /* --- clearing an encounter down --- */

    {
        Item loot;

        memset(&loot, 0, sizeof(loot));
        loot.type = ITEM_SHIELD;
        gbl_ground_item_add(&loot);
        money_set(&gbl.pooled_money, MONEY_GOLD, 250);
        gbl.num_loaded_monsters = 4;
        gbl.monsters_loaded     = true;

        ecl_vars_clear(gbl.ecl_vars);
        test_asm_reset(entry);
        test_asm_byte(0x1c);                            /* CLEARMONSTERS */
        test_asm_byte(0x00);                            /* EXIT */
        eclvm_run(entry);

        check(gbl.ground_item_count == 0 &&
              money_get(&gbl.pooled_money, MONEY_GOLD) == 0 &&
              gbl.num_loaded_monsters == 0 && !gbl.monsters_loaded &&
              gbl.monster_icon_id == 8,
              "CLEARMONSTERS drops the encounter's treasure with it", NULL);
    }

    /* --- the machine's address spaces --- */

    snprintf(detail, sizeof(detail),
             "0x4b00->%d 0x7a00->%d 0x7c00->%d 0x8000->%d 0x0000->%d",
             vm_get_memory_value_type(0x4b00), vm_get_memory_value_type(0x7a00),
             vm_get_memory_value_type(0x7c00), vm_get_memory_value_type(0x8000),
             vm_get_memory_value_type(0x0000));
    check(vm_get_memory_value_type(0x4b00) == 0 &&
          vm_get_memory_value_type(0x4eff) == 0 &&
          vm_get_memory_value_type(0x7a00) == 2 &&
          vm_get_memory_value_type(0x7bff) == 2 &&
          vm_get_memory_value_type(0x7c00) == 1 &&
          vm_get_memory_value_type(0x7fff) == 1 &&
          vm_get_memory_value_type(0x8000) == 3 &&
          vm_get_memory_value_type(0x9dff) == 3 &&
          vm_get_memory_value_type(0x9e00) == 4 &&
          vm_get_memory_value_type(0x0000) == 4,
          "an address alone says which space it is in", detail);

    /* Every scratch cell has to land inside EclVars: the translation is
     * (loc << 1) + 0xC00, which for 0x7A00 overflows to 0 and for 0x7BFF to the
     * last word there is room for. */
    {
        bool ok = true;

        for (u16 loc = 0x7a00; loc <= 0x7bff; loc++) {
            vm_set_memory_value((u16)(loc & 0x7f), loc);
            if (vm_get_memory_value(loc) != (u16)(loc & 0x7f)) {
                ok = false;
            }
        }
        check(ok, "the whole of the scratch space is addressable",
              "0x7a00 to 0x7bff, one word each");
        ecl_vars_clear(gbl.ecl_vars);
    }

    /* --- the packer on its own --- */

    {
        static const char *const words[] = {
            "", "A", "AB", "ABC", "ABCD",
            "PRESS <ENTER>/<RETURN> TO CONTINUE",
            "THE PARTY IS AMBUSHED BY 3 KOBOLDS!"
        };
        char   round[GBL_ECL_STRING_MAX];
        u8     packed[0x100];
        bool   ok = true;

        for (size_t i = 0; i < COAB_ARRAY_LEN(words); i++) {
            size_t n = vm_compress_string(packed, sizeof(packed), words[i]);

            vm_decompress_string(round, sizeof(round), packed, n);

            if (strcmp(round, words[i]) != 0) {
                ok = false;
                snprintf(detail, sizeof(detail), "'%.90s' came back as '%.90s'",
                         words[i], round);
            }
        }
        check(ok, "packed text round trips through six bits a character",
              ok ? "empty, part groups, and two whole lines" : detail);

        /* Six bits reach 0x20 to 0x5F, so lower case does not survive - and
         * neither does '@', which packs to zero and comes back out as the padding
         * a part filled last group leaves behind. Both are outside the alphabet
         * the game's own text uses. */
        {
            size_t n = vm_compress_string(packed, sizeof(packed), "A@B");

            vm_decompress_string(round, sizeof(round), packed, n);
            check(strcmp(round, "AB") == 0,
                  "a character that packs to zero is lost, as it was in the "
                  "original", round);
        }

        /* A string too long for the buffer is refused rather than written past. */
        check(vm_compress_string(packed, 2, "FAR TOO LONG FOR TWO BYTES") == 0,
              "packing refuses a buffer it would overrun", NULL);
    }

    gbl.print_commands = old_print;
    gbl.party_killed   = false;
    ecl_vars_clear(gbl.ecl_vars);
    ecl_block_clear(gbl.ecl_ptr);
    printf("\n");
}

/* ------------------------------------------------------------------ dungeon */
static void check_geo(void)
{
    static const u8 walls[WALL_DEF_BLOCK_SIZE * 2] = { 0x2c, 0x2d, 0xff };
    GeoBlock g;
    WallDefs w;
    u8       block[2 + GEO_BLOCK_DATA_SIZE];
    char     detail[160];
    MapInfo *m;

    printf("dungeon geometry\n");

    /* Square 3,2 is byte 3 + 2 * 16 = 0x23 of each of the four planes. */
    memset(block, 0, sizeof(block));
    block[0] = 0xff;                    /* the two-byte header, which is skipped */
    block[1] = 0xff;
    block[2 + 0x023] = 0x12;            /* wall types for directions 0 and 2 */
    block[2 + 0x123] = 0x34;            /* directions 4 and 6 */
    block[2 + 0x223] = 0x56;            /* x2 */
    block[2 + 0x323] = 0xe4;            /* 11 10 01 00 */

    if (!geo_block_load(&g, block, sizeof(block))) {
        check(false, "GEO block unpacks", "load failed");
    } else {
        m = &g.maps[2][3];
        snprintf(detail, sizeof(detail),
                 "3,2: walls %u %u %u %u, x2 %02x, x3 %u %u %u %u",
                 m->wall_type_dir_0, m->wall_type_dir_2, m->wall_type_dir_4,
                 m->wall_type_dir_6, m->x2,
                 m->x3_dir_0, m->x3_dir_2, m->x3_dir_4, m->x3_dir_6);
        check(m->wall_type_dir_0 == 1 && m->wall_type_dir_2 == 2 &&
              m->wall_type_dir_4 == 3 && m->wall_type_dir_6 == 4 &&
              m->x2 == 0x56 &&
              m->x3_dir_0 == 0 && m->x3_dir_2 == 1 &&
              m->x3_dir_4 == 2 && m->x3_dir_6 == 3 &&
              g.maps[0][0].x2 == 0, "GEO block unpacks", detail);
    }

    check(!geo_block_load(&g, block, 2 + GEO_BLOCK_DATA_SIZE - 1),
          "a short GEO block is refused", "0x401 bytes");

    /* Two blocks starting at set 2 fill sets 2 and 3; a third would be set 4 and
     * has to be refused rather than written past the array. */
    wall_defs_clear(&w);
    check(wall_defs_load(&w, 2, walls, sizeof(walls)) &&
          wall_defs_id(&w, 2, 0, 1) == 0x2d && wall_defs_id(&w, 1, 0, 1) == 0 &&
          !wall_defs_load(&w, 2, walls, WALL_DEF_BLOCK_SIZE * 3),
          "wall definitions load by set", "sets 2 and 3, set 4 refused");

    /* Offset() moves the picture ids from 0x2d up and leaves the shared tiles
     * below it alone. */
    wall_defs_block_offset(&w, 2, 0x10);
    snprintf(detail, sizeof(detail), "0x2c stays %02x, 0x2d becomes %02x",
             wall_defs_id(&w, 2, 0, 0), wall_defs_id(&w, 2, 0, 1));
    check(wall_defs_id(&w, 2, 0, 0) == 0x2c &&
          wall_defs_id(&w, 2, 0, 1) == 0x3d &&
          wall_defs_id(&w, 2, 0, 2) == 0x0f &&      /* 0xff + 0x10 wraps */
          wall_defs_id(&w, 2, WALL_DEF_ROWS, 0) == 0,
          "wall definition offsets", detail);

    printf("\n");
}

/* ------------------------------------------------------------------- combat */
static void check_combat(void)
{
    SortedCombatant order[4];
    SteppingPath    path;
    GroundTileMap   map;
    Player          a, b, c, d;
    char            detail[160];

    printf("combat\n");

    /* Turn order: fewest steps first, then lowest direction, and equal entries
     * keep the order they were added in. */
    player_init(&a);
    player_init(&b);
    player_init(&c);
    player_init(&d);
    memset(order, 0, sizeof(order));
    order[0].player = &a; order[0].steps = 6; order[0].direction = 1;
    order[1].player = &b; order[1].steps = 2; order[1].direction = 5;
    order[2].player = &c; order[2].steps = 6; order[2].direction = 0;
    order[3].player = &d; order[3].steps = 2; order[3].direction = 5;
    combatant_sort(order, 4);

    check(order[0].player == &b && order[1].player == &d &&
          order[2].player == &c && order[3].player == &a,
          "turn order sorts by steps then direction",
          "equal entries keep their order");

    /* Three squares due east: three whole steps, two halves each. */
    stepping_path_clear(&path);
    path.attacker = point_make(0, 0);
    path.target   = point_make(3, 0);
    stepping_path_calculate_deltas(&path);
    {
        int taken = 0;
        int facing = -1;

        while (stepping_path_step(&path)) {
            taken++;
            facing = path.direction;
        }
        snprintf(detail, sizeof(detail), "%d steps, cost %u, facing %d",
                 taken, path.steps, facing);
        check(taken == 3 && point_eq(path.current, path.target) &&
              path.steps == 6 && facing == 2,
              "a straight path steps east", detail);
    }

    /* Diagonally: each step moves both axes, so it costs three halves. */
    stepping_path_clear(&path);
    path.attacker = point_make(5, 5);
    path.target   = point_make(2, 2);
    stepping_path_calculate_deltas(&path);
    {
        int taken = 0;
        int facing = -1;

        while (stepping_path_step(&path)) {
            taken++;
            facing = path.direction;
        }
        snprintf(detail, sizeof(detail), "%d steps, cost %u, facing %d",
                 taken, path.steps, facing);
        check(taken == 3 && point_eq(path.current, path.target) &&
              path.steps == 9 && facing == 7,
              "a diagonal path costs three halves a step", detail);
    }

    /* The call that reports the end of the line still sets direction, and to the
     * no-move entry, so callers cannot read it afterwards. */
    check(path.direction == 8, "the last step reports no move",
          "which is why the walk above keeps the facing as it goes");

    /* The ground overlay is 50x25 and combat code does walk off the edge. */
    ground_tile_map_clear(&map);
    ground_tile_map_fill(&map, 4);
    ground_tile_map_set(&map, point_make(49, 24), 9);
    ground_tile_map_set(&map, point_make(50, 24), 9);   /* ignored */
    check(ground_tile_map_get(&map, point_make(49, 24)) == 9 &&
          ground_tile_map_get(&map, point_make(0, 0)) == 4 &&
          ground_tile_map_get(&map, point_make(50, 24)) == 0 &&
          ground_tile_map_get(&map, point_make(-1, 0)) == 0,
          "off-map squares read as zero", "the map is 50x25");

    printf("\n");
}

/* ------------------------------------------------------------------- tables --
 *
 * The transcribed tables. A row that slipped while being copied out of the
 * disassembly is invisible until the game plays wrong, so what can be checked
 * structurally is.
 */
static void check_tables(void)
{
    char detail[160];
    bool ok = true;
    int  bad = 0;

    printf("tables\n");

    /* Every row of the casting table records its own spell id, so a row that
     * went missing or got duplicated shows up as a mismatch. */
    for (int i = 1; i < SPELL_CASTING_TABLE_COUNT; i++) {
        if (spell_casting_table[i].spell_idx != i) {
            if (bad == 0) {
                printf("        spell %d holds spell_idx %d\n",
                       i, spell_casting_table[i].spell_idx);
            }
            bad++;
        }
    }
    snprintf(detail, sizeof(detail), "%d spells, %d rows out of place",
             SPELL_CASTING_TABLE_COUNT - 1, bad);
    check(bad == 0, "spell table rows are in id order", detail);

    check(spell_entry(SPELL_BLESS) != NULL &&
          spell_entry(SPELL_BLESS)->spell_class == SPELL_CLASS_CLERIC &&
          spell_entry(SPELL_BLESS)->spell_level == 1 &&
          spell_entry(SPELL_BLESS)->affect_id == AFFECT_BLESS,
          "spell lookup", "bless is a first level cleric spell");

    /* Spell id 0 is the empty slot and there is no row past the last spell. */
    check(!spell_id_valid(0) && spell_entry(0) == NULL &&
          spell_entry(SPELL_CASTING_TABLE_COUNT) == NULL,
          "out-of-range spell ids answer NULL", NULL);

    /* Background tiles: the table is indexed straight off the map, so the bounds
     * check is the only thing between a corrupt map and a wild read. */
    check(background_tile(0) != NULL &&
          background_tile(BACKGROUND_TILE_COUNT - 1) != NULL &&
          background_tile(BACKGROUND_TILE_COUNT) == NULL &&
          background_tile(-1) == NULL,
          "background tile bounds", "74 tiles");

    /* Directions run clockwise from north, and 8 means stay put. */
    ok = point_eq(map_direction_step(0), point_make(0, -1)) &&
         point_eq(map_direction_step(2), point_make(1, 0)) &&
         point_eq(map_direction_step(4), point_make(0, 1)) &&
         point_eq(map_direction_step(6), point_make(-1, 0)) &&
         point_eq(map_direction_step(8), point_make(0, 0)) &&
         point_eq(map_direction_step(9), point_make(0, 0)) &&
         point_eq(map_direction_step(-1), point_make(0, 0));
    check(ok, "compass directions", "0 north, clockwise, 8 stays put");

    /* Both cloud orders start or end on the no-move entry, which is the cell the
     * spell was cast on. */
    check(small_cloud_directions[0] == 8 &&
          small_cloud_directions_alt[SMALL_CLOUD_DIRECTION_COUNT - 1] == 8 &&
          cloud_directions[0] == 8,
          "cloud spells fill their own cell", NULL);

    /* Class minima come from the one table, read the other way round. */
    check(limits_class_stat_min(CLASS_PALADIN, PSTAT_CHA) == 17 &&
          limits_class_stat_min(CLASS_PALADIN, PSTAT_STR) == 12 &&
          limits_class_stat_min(CLASS_PALADIN, PSTAT_COUNT) == 0 &&
          limits_class_stat_min((ClassId)CLASS_COUNT, PSTAT_CHA) == 0,
          "class stat minima read the transposed table",
          "paladin: Cha 17, Str 12");

    /* Race and class tables: a human may take six of the single classes - not
     * druid or monk, which the game never offers - and every listed class has to
     * be a real one. */
    bad = 0;
    for (int race = 0; race < RACE_CLASSES_ROWS; race++) {
        const RaceClasses *rc = &limits_race_classes[race];

        if (rc->count < 0 || rc->count > RACE_CLASSES_MAX) {
            bad++;
            continue;
        }
        for (int i = 0; i < rc->count; i++) {
            if (!class_valid((int)rc->cls[i])) {
                bad++;
            }
        }
    }
    snprintf(detail, sizeof(detail), "human has %d, %d bad entries",
             limits_race_classes[RACE_HUMAN].count, bad);
    check(bad == 0 && limits_race_classes[RACE_HUMAN].count == 6 &&
          limits_race_classes[RACE_DWARF].count == 3,
          "race class lists", detail);

    check(limits_race_age(RACE_HUMAN, 0) != NULL &&
          limits_race_age(RACE_HUMAN, RACE_AGE_CLASSES) == NULL &&
          limits_race_age(RACE_COUNT, 0) == NULL,
          "starting age table bounds", "seven rollable classes per race");

    /* The clock's scales: ten ticks to the minute, then tens of minutes, hours,
     * days and months. */
    check(REST_TIME_SCALES[REST_SLOT_TICKS] == 10 &&
          REST_TIME_SCALES[REST_SLOT_MINUTES_TENS] == 6 &&
          REST_TIME_SCALES[REST_SLOT_HOURS] == 24 &&
          REST_TIME_SCALES[REST_SLOT_DAYS] == 30 &&
          REST_TIME_SCALES[REST_SLOT_MONTHS] == 12,
          "clock scales", "10 ticks, 60 minutes, 24 hours, 30 days, 12 months");

    {
        RestTime t;

        rest_time_clear(&t);
        rest_time_add(&t, REST_SLOT_HOURS, 5);
        rest_time_add(&t, REST_SLOT_HOURS, 2);
        rest_time_set(&t, REST_TIME_SLOTS, 3);      /* ignored */

        /* rest_time_add does not carry: the clock is normalized by the overlay
         * that owns it, which is where the original did it too. */
        check(rest_time_get(&t, REST_SLOT_HOURS) == 7 &&
              rest_time_get(&t, REST_TIME_SLOTS) == 0 &&
              rest_time_get(&t, -1) == 0,
              "clock slots", "7 hours, out-of-range slots ignored");
    }

    printf("\n");
}

/* --------------------------------------------------------------- menu lists */
static void check_menus(void)
{
    MenuList l;
    char     detail[160];
    bool     ok;

    printf("menus\n");

    menu_list_clear(&l);
    menu_list_add_heading(&l, "Level 1");
    menu_list_add(&l, "Bless");
    menu_list_add(&l, "Cure Light Wounds");
    menu_list_add_heading(&l, "Level 2");
    menu_list_add(&l, "Hold Person");

    /* Headings are drawn but cannot be picked, so the menus count position by
     * pickable entries. */
    snprintf(detail, sizeof(detail), "%d entries, %d pickable",
             l.count, menu_list_count_selectable(&l, l.count));
    check(l.count == 5 && menu_list_count_selectable(&l, l.count) == 3 &&
          menu_list_count_selectable(&l, 4) == 2 &&
          menu_list_count_selectable(&l, 99) == 3,
          "headings are not pickable", detail);

    menu_list_insert(&l, 1, "Detect Magic");
    ok = l.count == 6 && strcmp(l.item[1].text, "Detect Magic") == 0 &&
         !l.item[1].heading && strcmp(l.item[2].text, "Bless") == 0;

    menu_list_remove_at(&l, 1);
    ok = ok && l.count == 5 && strcmp(l.item[1].text, "Bless") == 0 &&
         l.item[0].heading;

    menu_list_remove_at(&l, 5);     /* past the end, ignored */
    check(ok && l.count == 5, "insert and remove shift the entries", NULL);

    check(menu_list_get(&l, 4) != NULL && menu_list_get(&l, 5) == NULL,
          "reading past the end answers NULL", NULL);

    /* Text longer than the entry is truncated, not written past. */
    menu_item_set(&l.item[0],
                  "a menu entry far longer than the forty one characters the "
                  "DOS record held", false, NULL);
    check(strlen(l.item[0].text) == MENU_ITEM_TEXT_MAX - 1,
          "over-long menu text is truncated", NULL);

    /* Filling the list has to fail rather than run off the end. */
    menu_list_clear(&l);
    for (int i = 0; i < MENU_LIST_MAX; i++) {
        menu_list_add(&l, "spell");
    }
    check(l.count == MENU_LIST_MAX && !menu_list_add(&l, "one too many") &&
          !menu_list_insert(&l, 0, "one too many") &&
          l.count == MENU_LIST_MAX,
          "a full menu list refuses more", NULL);

    printf("\n");
}

/* ----------------------------------------------------------------- endgame */

/* The band of the picture the fireworks are allowed in, rows 9 to 64
 * (endgame.c: in_sky). Everything the display draws happens there, so watching
 * those rows is enough to see all of it. */
#define SKY_FIRST_ROW 9
#define SKY_LAST_ROW  0x40
#define SKY_ROWS      (SKY_LAST_ROW - SKY_FIRST_ROW + 1)

static u8 g_sky[SKY_ROWS * EGA_W];

static void sky_snapshot(void)
{
    for (int y = 0; y < SKY_ROWS; y++) {
        for (int x = 0; x < EGA_W; x++) {
            g_sky[y * EGA_W + x] = display_get_pixel(x, SKY_FIRST_ROW + y);
        }
    }
}

/* How many pixels of the band no longer look like the snapshot, and what colour
 * the last of them is now. */
static int sky_diff(int *out_colour)
{
    int count = 0;

    for (int y = 0; y < SKY_ROWS; y++) {
        for (int x = 0; x < EGA_W; x++) {
            u8 c = display_get_pixel(x, SKY_FIRST_ROW + y);

            if (c != g_sky[y * EGA_W + x]) {
                count++;
                if (out_colour != NULL) {
                    *out_colour = c;
                }
            }
        }
    }

    return count;
}

static void check_endgame(const char *out_dir)
{
    const FireworkSpark *sparks = endgame_sparks();
    FireworkSpark scratch[FIREWORK_SPARK_COUNT];
    u8   rocket_size[FIREWORK_ROCKETS];
    char detail[160];
    u16  row, col, dx;
    i16  dy;
    int  nonzero, colors, lit, colour = -1;

    printf("endgame\n");

    /* The combat tests left black and grey swapped over; the endgame is not a
     * fight, and the pictures below are only worth looking at in the palette it
     * really runs in. */
    combatmap_color_0_8_normal();

    firework_sparks_clear(scratch, FIREWORK_SPARK_COUNT);

    /* The endgame walks the flat array as three rockets of forty sparks. */
    check(firework_spark(scratch, 0, 0) == &scratch[0] &&
          firework_spark(scratch, 2, 39) == &scratch[FIREWORK_SPARK_COUNT - 1] &&
          firework_spark(scratch, 1, 0) == &scratch[FIREWORK_SPARKS_PER_ROCKET] &&
          firework_spark(scratch, 3, 0) == NULL &&
          firework_spark(scratch, 0, FIREWORK_SPARKS_PER_ROCKET) == NULL,
          "fireworks are three rockets of forty", "spark + rocket * 40");

    /* Stage 0's time is never asked for - the stages count from 1 - so the pixel
     * the spark covered up lives in that byte. */
    scratch[0].covered_pixel = 0x0c;
    scratch[0].stage_end[0] = 1;
    scratch[0].stage_end[FIREWORK_STAGES - 1] = 50;
    check(firework_spark_stage_end(&scratch[0], 0) == 0x0c &&
          firework_spark_stage_end(&scratch[0], 1) == 1 &&
          firework_spark_stage_end(&scratch[0], FIREWORK_STAGES) == 50 &&
          firework_spark_stage_end(&scratch[0], FIREWORK_STAGES + 1) == 0 &&
          firework_spark_stage_end(&scratch[0], -1) == 0,
          "a spark's stage times share a byte with the pixel it covers",
          "stage 0 is that pixel");

    clear_screen_raw();
    display_set_pixel(5, 10, 12);
    check(endgame_get_pixel(10, 5) == 12 && endgame_get_pixel(5, 10) == 0,
          "the endgame reads pixels row first, column second", NULL);

    /* The feast at Shadowdale, which is the sky the rockets go up into. */
    gbl.game_area = 6;
    picture_load_bigpic(0x7a);
    clear_screen_raw();
    picture_draw_bigpic();
    frame_stats(&nonzero, &colors);
    snprintf(detail, sizeof(detail), "%d px, %d colours", nonzero, colors);
    check(gbl.bigpic_dax != NULL && gbl.bigpic_block_id == 0x7a &&
          nonzero > 20000 && colors > 8,
          "BIGPIC6 block 0x7a is the feast the game ends at", detail);

    sky_snapshot();

    /* An undrawn flight is pure arithmetic: sixty-one steps of 45/32nds of a
     * pixel to the right, and a climb of 52 that gravity takes a unit off each
     * step. Nothing is drawn, which is how the endgame finds the burst point
     * before it launches anything. */
    row = 65;
    col = 65;
    dx = 45;
    dy = -52;
    endgame_rocket_flight(false, 0x3c, &dy, dx, &row, &col);
    snprintf(detail, sizeof(detail), "row %u col %u, climb %d",
             (unsigned)row, (unsigned)col, (int)dy);
    check(row == 24 && col == 150 && dy == 9 && sky_diff(NULL) == 0,
          "a rocket's flight is worked out before it is flown", detail);

    /* Drawn, the same flight is one lit pixel at a time: every step puts back
     * what the one before it covered, and the last one is taken up again when the
     * rocket arrives, so the sky ends up untouched. What shows that it was drawn
     * at all is the colours it spent - one random shade of the fire colours for
     * each of the sixty steps it flew inside the band. */
    {
        int after_flight;
        int after_sixty_colours;

        rnd_seed(0x19891127u);
        row = 60;
        col = 65;
        dx = 45;
        dy = -52;
        endgame_rocket_flight(true, 0x3c, &dy, dx, &row, &col);
        lit = sky_diff(&colour);
        after_flight = rnd_int(1000);

        rnd_seed(0x19891127u);
        for (int step = 0; step < 0x3c; step++) {
            colour = rnd_int(7) + 8;
        }
        after_sixty_colours = rnd_int(1000);

        snprintf(detail, sizeof(detail),
                 "%d pixels left lit, sixty sparks spent", lit);
        check(lit == 0 && after_flight == after_sixty_colours &&
              colour >= 8 && colour <= 14,
              "a drawn flight is a trail that clears up behind itself", detail);
    }

    /* Three rockets go off: 2, 4 and 6 are the colours they fade through. */
    clear_screen_raw();
    picture_draw_bigpic();
    sky_snapshot();

    rocket_size[0] = 2;
    rocket_size[1] = 4;
    rocket_size[2] = 6;
    endgame_burst(rocket_size, 9, 45, 30, 150);

    {
        u8  sky_here = display_get_pixel(150, 30);
        int at_burst = 0;
        int first_stage = 0;
        int remembered = 0;

        for (int i = 0; i < FIREWORK_SPARK_COUNT; i++) {
            if (sparks[i].x == 150 && sparks[i].y == 30 &&
                sparks[i].x_fixed == 150 * 32 && sparks[i].y_fixed == 30 * 32) {
                at_burst++;
            }
            if (sparks[i].stage == 1 && sparks[i].stage_end[0] == 1) {
                first_stage++;
            }
            if (sparks[i].covered_pixel == sky_here) {
                remembered++;
            }
        }

        snprintf(detail, sizeof(detail),
                 "%d at the burst, %d on stage 1, %d know the sky behind them",
                 at_burst, first_stage, remembered);
        check(at_burst == FIREWORK_SPARK_COUNT &&
              first_stage == FIREWORK_SPARK_COUNT &&
              remembered == FIREWORK_SPARK_COUNT &&
              sky_diff(NULL) == 0,
              "a burst starts every spark where the rocket died", detail);
    }

    /* Ten ticks in the sparks are spread over the sky. The C# moved them by
     * assigning their speed over their position, which put every one of them in
     * the top-left corner where nothing is drawn, so this is what that bug
     * showed up as. */
    for (int tick = 1; tick <= 10; tick++) {
        endgame_burst_step(tick);
    }
    lit = sky_diff(&colour);
    dump(out_dir, "endgame-fireworks.ppm");
    snprintf(detail, sizeof(detail), "%d pixels lit -> endgame-fireworks.ppm",
             lit);
    check(lit > 40, "a burst spreads out across the sky", detail);

    /* And when the last spark has gone out the sky is exactly as it was. */
    clear_screen_raw();
    picture_draw_bigpic();
    sky_snapshot();
    endgame_burst(rocket_size, 9, 45, 30, 150);
    endgame_burst_run();
    lit = sky_diff(&colour);
    snprintf(detail, sizeof(detail), "%d pixels left behind", lit);
    check(lit == 0, "the fireworks leave the sky as they found it", detail);

    /* The animation of the pool shattering: seven frames, of which the first is
     * the whole picture and the six after it are stored as differences against
     * it. The last leaves the pool a scatter of green over black where the
     * pedestal stood - which is where the next animation, 0x4b, starts from, so
     * the differences are being applied the way the artists drew them. */
    clear_screen_raw();
    endgame_show_animation(1, 0x4a, 3, 3);
    frame_stats(&nonzero, &colors);
    dump(out_dir, "endgame-animation.ppm");
    snprintf(detail, sizeof(detail), "%d px, %d colours -> endgame-animation.ppm",
             nonzero, colors);
    check(nonzero > 2000 && colors > 4,
          "the pool of radiance shatters", detail);

    /* Tyranthraxus goes out over the shattered pool: 0x4b's last frame has the
     * face blacked out, so it is a good deal emptier than the frame it started
     * from. */
    {
        int shattered = nonzero;

        clear_screen_raw();
        endgame_show_animation(1, 0x4b, 3, 3);
        frame_stats(&nonzero, &colors);
        dump(out_dir, "endgame-crumbles.ppm");
        snprintf(detail, sizeof(detail),
                 "%d px against the shattered pool's %d -> endgame-crumbles.ppm",
                 nonzero, shattered);
        check(nonzero > 500 && nonzero < shattered,
              "and Tyranthraxus crumbles into nothingness", detail);
    }

    /* PIC6 has no block 2, so there is nothing to cycle and nothing to wait
     * for. The C# dereferenced the first frame it did not have. */
    endgame_show_animation(1, 0x02, 3, 3);
    check(true, "an animation that will not load is not waited for",
          "PIC6 has no block 0x02");

    printf("\n");
}

/* ------------------------------------------------------- imported characters */
static void check_import(void)
{
    PoolRadPlayer prp;
    HillsFarPlayer hfp;
    Player p;
    u8   rec[POOL_RAD_RECORD_SIZE];
    char detail[160];

    printf("imported characters\n");

    check_desc_layout(&pool_rad_player_desc, POOL_RAD_RECORD_SIZE);
    check_desc_layout(&hills_far_player_desc, HILLS_FAR_RECORD_SIZE);

    {
        static const char *const lossy[] = { "name" };

        memset(&prp, 0, sizeof(prp));
        check_desc_round_trip(&pool_rad_player_desc, &prp, lossy,
                              COAB_ARRAY_LEN(lossy));
        memset(&hfp, 0, sizeof(hfp));
        check_desc_round_trip(&hills_far_player_desc, &hfp, lossy,
                              COAB_ARRAY_LEN(lossy));
    }

    /* A Pool of Radiance record, built at the offsets the other game used. */
    memset(rec, 0, sizeof(rec));
    rec[0x00] = 5;
    memcpy(rec + 0x01, "Alias", 5);
    rec[0x10] = 18;                 /* Str */
    rec[0x16] = 99;                 /* Str00 */
    rec[0x2e] = RACE_HUMAN;
    rec[0x2f] = CLASS_FIGHTER;
    rec[0x30] = 22;                 /* age */
    rec[0x32] = 45;                 /* hp max */
    rec[0x33 + SPELL_ANIMATE_DEAD - 1] = 1;
    rec[0x33 + SPELL_BLESS - 1] = 1;
    rec[0x9e] = 0;                  /* sex */
    rec[0xac] = 0x44;               /* exp */
    rec[0xad] = 0x33;
    rec[0x11b] = 40;                /* hp current */

    if (!pool_rad_player_read(&prp, rec, sizeof(rec), 0)) {
        check(false, "Pool of Radiance record reads", "read failed");
    } else {
        snprintf(detail, sizeof(detail), "%s, %u/%u hp, exp %d",
                 prp.name, prp.field_11B, prp.hp_max, prp.field_AC);
        check(strcmp(prp.name, "Alias") == 0 && prp.stat[PSTAT_STR] == 18 &&
              prp.stat[PSTAT_STR00] == 99 && prp.hp_max == 45 &&
              prp.field_AC == 0x3344 && prp.field_11B == 40,
              "Pool of Radiance record reads", detail);

        import_convert_pool_rad_player(&p, &prp);

        snprintf(detail, sizeof(detail), "%s, %u/%u hp, exp %d, %d pp",
                 p.name, p.hit_point_current, p.hit_point_max, p.exp,
                 money_get(&p.money, MONEY_PLATINUM));
        check(strcmp(p.name, "Alias") == 0 &&
              p.stats.value[PSTAT_STR].full == 18 &&
              p.hit_point_max == 45 && p.hit_point_current == 40 &&
              p.exp == 0x3344 && p.race == RACE_HUMAN &&
              money_get(&p.money, MONEY_PLATINUM) == 300,
              "Pool of Radiance import converts", detail);

        /* The spell ids part company at Animate Dead, so that one flag is
         * dropped and the ones before it are kept. */
        check(player_knows_spell(&p, SPELL_BLESS) &&
              !player_knows_spell(&p, SPELL_ANIMATE_DEAD),
              "Animate Dead is dropped on import",
              "the spell ids diverge from there");

        /* Nothing carries over: the item pointers referred to the other game's
         * heap and the original's copy was already dead code. */
        check(p.item_count == 0, "imported characters arrive empty-handed", NULL);
    }

    /* A short buffer is refused rather than read past. */
    check(!pool_rad_player_read(&prp, rec, POOL_RAD_RECORD_SIZE - 1, 0) &&
          !hills_far_player_read(&hfp, rec, HILLS_FAR_RECORD_SIZE - 1, 0),
          "a short import record is refused", NULL);

    /* Hillsfar keeps the name at 0x04 and percentile strength second, not last,
     * which is why the stats are held in record order. */
    memset(rec, 0, sizeof(rec));
    rec[0x04] = 10;
    memcpy(rec + 0x05, "Dragonbait", 10);
    rec[0x14] = 17;                 /* Str */
    rec[0x15] = 65;                 /* Str00 */
    rec[0x1a] = 12;                 /* Cha */
    rec[0xb9] = 4;                  /* fighter skill */

    if (!hills_far_player_read(&hfp, rec, HILLS_FAR_RECORD_SIZE, 0)) {
        check(false, "Hillsfar record reads", "read failed");
    } else {
        snprintf(detail, sizeof(detail), "%s, Str %u/%u, fighter %u",
                 hfp.name, hfp.stat[HF_STAT_STR], hfp.stat[HF_STAT_STR00],
                 hfp.skill_fighter);
        check(strcmp(hfp.name, "Dragonbait") == 0 &&
              hfp.stat[HF_STAT_STR] == 17 && hfp.stat[HF_STAT_STR00] == 65 &&
              hfp.stat[HF_STAT_CHA] == 12 && hfp.skill_fighter == 4 &&
              hills_far_stat_to_pstat[HF_STAT_STR00] == PSTAT_STR00,
              "Hillsfar record reads", detail);
    }

    printf("\n");
}

/* ------------------------------------------- the support segments (seg051/42) */
static void check_support(const char *out_dir)
{
    char path[1024];
    char buf[64];
    GameFile f;
    u8  data[16];
    u8  got[16];
    char detail[160];

    printf("support segments\n");

    /* seg051.Random: never negative, never the limit, and zero for a zero
     * limit rather than a division by it. */
    {
        bool in_range = true;
        int  seen_low = 0, seen_high = 0;

        rnd_seed(1);
        for (int i = 0; i < 20000; i++) {
            int v = rnd_int(6);

            if (v < 0 || v > 5) {
                in_range = false;
            }
            if (v == 0) {
                seen_low++;
            }
            if (v == 5) {
                seen_high++;
            }
        }
        snprintf(detail, sizeof(detail), "20000 rolls of d6, %d ones, %d sixes",
                 seen_low, seen_high);
        /* Every face has to turn up, or the generator is stuck; a fair d6 gives
         * about 3333 of each, so a hundred is a very loose floor. */
        check(in_range && seen_low > 100 && seen_high > 100,
              "Random(6) rolls 0..5", detail);
    }

    check(rnd_int(0) == 0 && rnd_int(-4) == 0,
          "Random(0) is 0 rather than a division by zero", NULL);

    check(rnd_int(1) == 0, "Random(1) is always 0", NULL);

    {
        bool real_in_range = true;

        for (int i = 0; i < 1000; i++) {
            double d = rnd_real();

            if (!(d >= 0.0 && d < 1.0)) {
                real_in_range = false;
            }
        }
        check(real_in_range, "Random__Real stays in [0,1)", NULL);
    }

    /* Seeding is reproducible, which the C# System.Random was not across runs;
     * this is what lets a bug report be replayed. */
    {
        int first[8], second[8];

        rnd_seed(12345);
        for (int i = 0; i < 8; i++) {
            first[i] = rnd_int(1000);
        }
        rnd_seed(12345);
        for (int i = 0; i < 8; i++) {
            second[i] = rnd_int(1000);
        }
        check(memcmp(first, second, sizeof(first)) == 0,
              "the same seed replays the same rolls", NULL);

        rnd_seed(12346);
        {
            bool differs = false;

            for (int i = 0; i < 8; i++) {
                if (rnd_int(1000) != first[i]) {
                    differs = true;
                }
            }
            check(differs, "the next seed along gives a different stream", NULL);
        }
    }

    /* seg042.clean_string turns what the player typed into a DOS basename. */
    check(strcmp(file_clean_string(buf, sizeof(buf), "  Alias.*"), "alias") == 0 &&
          strcmp(file_clean_string(buf, sizeof(buf), "DragonBaitTheSaurial"),
                 "dragonba") == 0 &&
          strcmp(file_clean_string(buf, sizeof(buf), "?:;|"), "") == 0 &&
          strcmp(file_clean_string(buf, sizeof(buf), "a b"), "a b") == 0,
          "clean_string trims, lowercases and cuts to eight",
          "\"  Alias.*\" -> \"alias\"");

    /* seg051.Copy is Pascal's, so a length past the end just stops at the end. */
    check(strcmp(file_copy_string(buf, sizeof(buf), 5, 0, "CHEADT"), "CHEAD") == 0 &&
          strcmp(file_copy_string(buf, sizeof(buf), 5, 0, "ICON"), "ICON") == 0 &&
          strcmp(file_copy_string(buf, sizeof(buf), 3, 2, "COMSPR"), "MSP") == 0 &&
          strcmp(file_copy_string(buf, sizeof(buf), 0, 0, "ICON"), "") == 0,
          "Copy(len, start, s) clamps to the string", "Copy(5, 0, \"CHEADT\")");

    /* seg051.FillChar */
    memset(got, 0, sizeof(got));
    file_fill_char(0x0f, 4, got);
    check(got[0] == 0x0f && got[3] == 0x0f && got[4] == 0,
          "FillChar fills exactly the count asked for", NULL);

    /* seg042.set_game_area / restore_game_area: one level of backup. */
    {
        u8 was = gbl.game_area;

        gbl.game_area = 2;
        file_set_game_area(5);
        check(gbl.game_area == 5 && gbl.game_area_backup == 2,
              "set_game_area remembers the one it replaced", NULL);
        file_restore_game_area();
        check(gbl.game_area == 2, "restore_game_area puts it back", NULL);
        gbl.game_area = was;
    }

    /* A save game is written and read back through seg051's block calls. */
    if (!vfs_path_join(path, sizeof(path), out_dir, "fileio-test.dat")) {
        check(false, "a block written is a block read back", "path too long");
    } else {
        bool wrote, read_back = false, refused_missing;
        size_t got_count = 0;

        file_delete(path);

        for (int i = 0; i < (int)sizeof(data); i++) {
            data[i] = (u8)(0xa0 + i);
        }

        /* find_and_open_file creates what is not there, so the first call has to
         * be told not to complain about it. */
        refused_missing = !file_find_and_open(&f, true, path);

        wrote = file_assign(&f, path) && file_rewrite(&f) &&
                file_block_write(&f, data, sizeof(data));
        file_close(&f);

        if (file_find_and_open(&f, false, path)) {
            memset(got, 0, sizeof(got));
            got_count = file_block_read(&f, got, sizeof(got));
            read_back = got_count == sizeof(got) &&
                        memcmp(got, data, sizeof(data)) == 0;

            /* Reset goes back to the start, the only seek seg051 had. */
            file_reset(&f);
            memset(got, 0, sizeof(got));
            read_back = read_back &&
                        file_block_read(&f, got, 4) == 4 && got[0] == 0xa0;

            /* Past the end reads short rather than failing: the save loader
             * counts on being told how much arrived. */
            read_back = read_back && file_block_read(&f, got, sizeof(got)) ==
                        sizeof(data) - 4;
            file_close(&f);
        }

        snprintf(detail, sizeof(detail), "%zu of %zu bytes", got_count,
                 sizeof(data));
        check(refused_missing && wrote && read_back,
              "a block written is a block read back", detail);

        /* Rewrite truncates: the file is empty again afterwards. */
        if (file_assign(&f, path)) {
            bool emptied = file_rewrite(&f) &&
                           file_block_read(&f, got, sizeof(got)) == 0;
            file_close(&f);
            check(emptied, "Rewrite empties the file", NULL);
        } else {
            check(false, "Rewrite empties the file", "could not reopen");
        }

        file_delete(path);
        check(!file_exists(path), "delete_file removes it", NULL);
        /* And deleting what is not there is not an error. */
        file_delete(path);
    }

    /* A handle that was never opened is complained about, not crashed on. */
    memset(&f, 0, sizeof(f));
    check(file_block_read(&f, got, 4) == 0 &&
          !file_block_write(&f, got, 4) && !file_rewrite(&f),
          "an unopened file refuses reads and writes", NULL);
    file_close(&f);

    printf("\n");
}

/* ------------------------------------------------- the map cursor and icons */
static void check_overlays(void)
{
    int col_x = -1, row_y = -1;
    char detail[160];

    printf("map cursor and icons\n");

    /* ovr028's two tables are 33 places long and city 6 is the far north east
     * corner of the map, which is the pair most likely to catch a table that
     * has slipped a row. */
    map_cursor_set_position(6);
    map_cursor_position(&col_x, &row_y);
    snprintf(detail, sizeof(detail), "city 6 at %d,%d", col_x, row_y);
    check(col_x == 0x26 && row_y == 0x01, "the map cursor knows 33 places",
          detail);

    map_cursor_set_position(MAP_CURSOR_CITY_COUNT - 1);
    map_cursor_position(&col_x, &row_y);
    check(col_x == 0x0f && row_y == 0x00, "the last place is the odd one out",
          "0x0f,0x00");

    /* An id from a damaged save leaves the cursor where it was. */
    map_cursor_set_position(MAP_CURSOR_CITY_COUNT);
    map_cursor_position(&col_x, &row_y);
    check(col_x == 0x0f && row_y == 0x00, "an unknown place is ignored", NULL);

    /* The cursor is one solid cell of colour 15 and its backup is the same
     * shape, so Draw and Restore cannot disagree about how much they cover. */
    check(gbl.cursor != NULL && gbl.cursor_bkup != NULL &&
          gbl.cursor->width == 1 && gbl.cursor->height == 8 &&
          gbl.cursor->data[0] == 0x0f &&
          gbl.cursor_bkup->width == gbl.cursor->width &&
          gbl.cursor_bkup->height == gbl.cursor->height,
          "the cursor is one filled cell", "colour 15, 1x8");

    /* The isometric tile bank: 0x30 cells of 24x24. */
    snprintf(detail, sizeof(detail), "%d cells of %dx%d",
             gbl.dax_24x24_set ? gbl.dax_24x24_set->item_count : -1,
             gbl.dax_24x24_set ? gbl.dax_24x24_set->width * 8 : -1,
             gbl.dax_24x24_set ? gbl.dax_24x24_set->height : -1);
    check(gbl.dax_24x24_set != NULL &&
          gbl.dax_24x24_set->item_count == GBL_24X24_CELLS &&
          gbl.dax_24x24_set->width == GBL_24X24_WIDTH &&
          gbl.dax_24x24_set->height == GBL_24X24_HEIGHT,
          "the 24x24 tile bank holds 0x30 cells", detail);

    /* Tile ids over 0x7f come from gbl.dword_1C8FC, which the game never fills
     * in, so they draw nothing at all rather than the wrong tile. */
    {
        bool drew = false;

        clear_screen_raw();
        icons_draw_iso_tile(0x80, 4, 4);
        for (int y = 0; y < EGA_H && !drew; y++) {
            for (int x = 0; x < EGA_W; x++) {
                if (display_get_pixel(x, y) != 0) {
                    drew = true;
                    break;
                }
            }
        }
        check(gbl.dword_1C8FC == NULL && !drew,
              "tile ids over 0x7f draw nothing", "dword_1C8FC is never loaded");
    }

    /* All 26 icon slots start empty and stay valid to ask about. */
    {
        bool empty = true;

        for (int i = 0; i < GBL_COMBAT_ICON_COUNT; i++) {
            if (combat_icon_get(&gbl.combat_icons[i], COMBAT_ICON_NORMAL, 0)
                != NULL) {
                empty = false;
            }
        }
        icons_release_combat_icon(GBL_COMBAT_ICON_COUNT);  /* refused, not a crash */
        check(empty, "26 combat icon slots start empty", NULL);
    }

    printf("\n");
}

/* How many pixels of `color` sit in cells x0..x1 of the prompt row. The prompt
 * line is the one row whose exact colours are worth checking: reverse video for
 * the highlighted word is what tells the player where the highlight is. */
static int prompt_row_color_count(int x0, int x1, u8 color)
{
    int found = 0;

    for (int y = 0x18 * 8; y < 0x18 * 8 + 8; y++) {
        for (int x = x0 * 8; x < (x1 + 1) * 8; x++) {
            if (display_get_pixel(x, y) == color) {
                found++;
            }
        }
    }
    return found;
}

/* The corner picture is the one asset with a container of its own: several
 * frames inside a single DAX block, and in PIC and FINAL every frame after the
 * first stored as a difference against the first. A difference frame that is
 * never un-XORed has a signature - it comes out nearly all colour 0, because
 * most of the difference bytes are zero - so that is what these checks look
 * for, rather than a checksum that would break with any data set. */
static void check_pictures(const char *out_dir)
{
    char detail[192];
    int nonzero = 0, colors = 0;
    DaxBlock *frame0;
    DaxBlock *frame1;

    printf("pictures\n");

    gbl.game_area = 1;
    gbl.animations_on = true;
    gbl.area_ptr->picture_fade = 0;
    gbl.game_state = GAME_STATE_DUNGEON_MAP;

    /* PIC1.DAX block 0x1d is a four frame animation of an 88 row, 11 cell
     * picture; 15573 bytes is exactly 1 + 4 * (21 + 3872), which is what pins
     * the per-frame header down to 21 bytes. */
    picture_load_pic_final(&gbl.pic_frames, 0, 0x1d, "PIC");
    frame0 = gbl.pic_frames.frames[0].picture;
    frame1 = gbl.pic_frames.frames[1].picture;

    snprintf(detail, sizeof(detail), "%d frames, %d cells x %d rows, delay %d",
             gbl.pic_frames.num_frames,
             frame0 ? frame0->width : -1, frame0 ? frame0->height : -1,
             gbl.pic_frames.frames[0].delay);
    check(gbl.pic_frames.num_frames == 4 && gbl.pic_frames.cur_frame == 1 &&
          frame0 != NULL && frame1 != NULL &&
          frame0->width == 11 && frame0->height == 88 &&
          frame1->width == 11 && frame1->height == 88 &&
          gbl.pic_frames.frames[0].delay > 0,
          "PIC1 block 0x1d unpacks into four frames", detail);

    check(strcmp(gbl.last_dax_file, "PIC") == 0 &&
          gbl.last_dax_block_id == 0x1d,
          "a loaded animation remembers its file and block", gbl.last_dax_file);

    /* A later frame is the first frame XORed with the stored difference, so it
     * must look like the first frame - mostly identical, not identical, and not
     * the flat black that raw difference bytes would decode to. */
    if (frame0 != NULL && frame1 != NULL && frame0->data_size == frame1->data_size) {
        size_t size = frame0->data_size;
        size_t same = 0;
        size_t black = 0;

        for (size_t i = 0; i < size; i++) {
            if (frame0->data[i] == frame1->data[i]) {
                same++;
            }
            if (frame1->data[i] == 0) {
                black++;
            }
        }

        snprintf(detail, sizeof(detail),
                 "%zu%% of frame 2 matches frame 1, %zu%% of it is black",
                 same * 100 / size, black * 100 / size);
        check(same > size / 2 && same < size && black < size * 3 / 4,
              "PIC frames are stored XORed against the first", detail);
    } else {
        check(false, "PIC frames are stored XORed against the first",
              "frames missing or of different sizes");
    }

    /* Reloading the same file and block is the engine's own no-op: a sentinel
     * written into the pixels has to survive it. */
    if (frame0 != NULL) {
        u8 keep = frame0->data[0];

        frame0->data[0] = 0x0e;
        picture_load_pic_final(&gbl.pic_frames, 0, 0x1d, "PIC");
        check(gbl.pic_frames.frames[0].picture == frame0 &&
              frame0->data[0] == 0x0e,
              "loading the picture that is already loaded does nothing", NULL);
        frame0->data[0] = keep;
    }

    clear_screen_raw();
    picture_draw_maybe_overlayed(frame0, false, 3, 3);
    frame_stats(&nonzero, &colors);
    dump(out_dir, "picture-anim.ppm");
    snprintf(detail, sizeof(detail), "%d px, %d colors -> picture-anim.ppm",
             nonzero, colors);
    check(nonzero > 1000 && colors > 4, "the corner picture draws", detail);

    /* The overlay path goes through the combat view, which clips to pixels
     * 8..176: an 88x88 picture at cell 3,3 lands wholly inside it. */
    clear_screen_raw();
    picture_draw_maybe_overlayed(frame0, true, 3, 3);
    frame_stats(&nonzero, &colors);
    snprintf(detail, sizeof(detail), "%d px, %d colors", nonzero, colors);
    check(nonzero > 1000 && colors > 4, "the overlayed picture draws", detail);

    /* The area fade recolours the picture itself, roughly a quarter of the
     * matching pixels per pass, so one prompt's worth of fade adds colour 12
     * and the picture never comes back. */
    if (frame0 != NULL) {
        int red_before = 0, red_after = 0;

        for (size_t i = 0; i < frame0->data_size; i++) {
            if (frame0->data[i] == 12) {
                red_before++;
            }
        }

        gbl.area_ptr->picture_fade = 1;
        picture_draw_maybe_overlayed(frame0, false, 3, 3);
        gbl.area_ptr->picture_fade = 0;

        for (size_t i = 0; i < frame0->data_size; i++) {
            if (frame0->data[i] == 12) {
                red_after++;
            }
        }

        snprintf(detail, sizeof(detail), "colour 12: %d px -> %d px",
                 red_before, red_after);
        check(red_after > red_before, "one pass of the area fade reddens the "
              "picture", detail);
    }

    /* Masked frames: colour 0 becomes the transparent index and colour 13 -
     * the artists' "nothing here" - is turned into black. */
    picture_dax_array_free_blocks(&gbl.pic_frames);
    picture_load_pic_final(&gbl.pic_frames, 1, 0x1d, "PIC");
    frame0 = gbl.pic_frames.frames[0].picture;

    if (frame0 != NULL) {
        int transparent = 0, thirteen = 0;

        for (size_t i = 0; i < frame0->data_size; i++) {
            if (frame0->data[i] == 16) {
                transparent++;
            }
            if (frame0->data[i] == 13) {
                thirteen++;
            }
        }

        snprintf(detail, sizeof(detail), "%d transparent px, %d left of colour 13",
                 transparent, thirteen);
        check(transparent > 0 && thirteen == 0,
              "a masked frame has no colour 13 left", detail);
    } else {
        check(false, "a masked frame has no colour 13 left", "no frame loaded");
    }

    /* With animation switched off a PIC is a still: only the first frame is
     * wanted, and the rest of the block is not even walked. */
    picture_dax_array_free_blocks(&gbl.pic_frames);
    gbl.animations_on = false;
    picture_load_pic_final(&gbl.pic_frames, 0, 0x1d, "PIC");
    snprintf(detail, sizeof(detail), "%d frame(s) loaded", gbl.pic_frames.num_frames);
    check(gbl.pic_frames.num_frames == 1 &&
          gbl.pic_frames.frames[1].picture == NULL,
          "animation off loads the first frame only", detail);
    gbl.animations_on = true;

    /* Freeing has to leave the array able to reload the same block. */
    picture_dax_array_free_blocks(&gbl.pic_frames);
    {
        bool all_gone = gbl.pic_frames.num_frames == 0 &&
                        gbl.pic_frames.cur_frame == 0 &&
                        gbl.last_dax_block_id == 0xff &&
                        gbl.last_dax_file[0] == '\0';

        for (int i = 0; i < DAX_ARRAY_FRAMES; i++) {
            if (gbl.pic_frames.frames[i].picture != NULL) {
                all_gone = false;
            }
        }
        check(all_gone, "freeing an animation forgets which one it was", NULL);
    }

    /* The three sprite distances draw through the combat view; each frame
     * carries the cell it belongs at. */
    picture_load_pic_final(&gbl.pic_frames, 1, 0x1d, "PIC");
    clear_screen_raw();
    picture_show_3d_sprite(&gbl.pic_frames, 2);
    frame_stats(&nonzero, &colors);
    snprintf(detail, sizeof(detail), "%d px, %d colors", nonzero, colors);
    check(nonzero > 100, "a 3d sprite draws at its own position", detail);

    /* The wilderness backdrop, and with it the frame the map lives in. */
    if (!frames_load_8x8d(4, 0xca) || !frames_load_8x8d(0, 0xcb)) {
        check(false, "8x8 symbol banks for the map frame", "8X8D1.DAX 0xca/0xcb");
    }

    picture_load_bigpic(0x79);
    snprintf(detail, sizeof(detail), "%d cells x %d rows",
             gbl.bigpic_dax ? gbl.bigpic_dax->width : -1,
             gbl.bigpic_dax ? gbl.bigpic_dax->height : -1);
    check(gbl.bigpic_dax != NULL && gbl.bigpic_block_id == 0x79 &&
          gbl.bigpic_dax->width == 38 && gbl.bigpic_dax->height == 120 &&
          gbl.pic_frames.num_frames == 0,
          "BIGPIC1 block 0x79 is the wilderness map", detail);

    {
        DaxBlock *loaded = gbl.bigpic_dax;

        picture_load_bigpic(0x79);
        check(gbl.bigpic_dax == loaded,
              "loading the backdrop that is already loaded does nothing", NULL);
    }

    clear_screen_raw();
    picture_draw_bigpic();
    frame_stats(&nonzero, &colors);
    dump(out_dir, "picture-bigpic.ppm");
    snprintf(detail, sizeof(detail), "%d px, %d colors -> picture-bigpic.ppm",
             nonzero, colors);
    check(nonzero > 20000 && colors > 8, "the wilderness map draws", detail);

    /* The city cursor is drawn over that map and taken off again, so its backup
     * has to put back exactly what it covered. City 6 is at cell 0x26,0x01. */
    {
        u8 before, during, after;

        map_cursor_set_position(6);
        before = display_get_pixel(0x26 * 8, 0x01 * 8);
        map_cursor_draw();
        during = display_get_pixel(0x26 * 8, 0x01 * 8);
        map_cursor_restore();
        after = display_get_pixel(0x26 * 8, 0x01 * 8);

        snprintf(detail, sizeof(detail), "colour %u -> %u -> %u",
                 before, during, after);
        check(during == 15 && after == before,
              "the city cursor restores the map underneath it", detail);
    }

    /* The portraits live in the chapter's HEAD and BODY files, and chapter 1 has
     * none: they start at chapter 2. */
    gbl.game_area = 2;
    picture_head_body(0, 0);
    check(gbl.head_dax != NULL && gbl.body_dax != NULL &&
          gbl.current_head_id == 0 && gbl.current_body_id == 0,
          "HEAD2/BODY2 block 0 is a portrait",
          gbl.head_dax != NULL ? "head and body loaded" : "missing");

    clear_screen_raw();
    picture_draw_head_and_body(true, 3, 3);
    frame_stats(&nonzero, &colors);
    dump(out_dir, "picture-portrait.ppm");
    snprintf(detail, sizeof(detail), "%d px, %d colors -> picture-portrait.ppm",
             nonzero, colors);
    check(nonzero > 500, "a portrait draws head above body", detail);

    /* Back to chapter 1, and forget the chapter 2 portraits: the ids alone
     * decide whether a load is needed, so leaving them set would make the next
     * chapter's portrait 0 the wrong picture. */
    gbl.game_area = 1;
    gbl.current_head_id = 0xff;
    gbl.current_body_id = 0xff;

    printf("\n");
}

/* Every choice the game offers is a word on the prompt line, so this is the one
 * path that has to work before anything else can be played. The keys are pushed
 * into the platform queue, which is what a scripted run of the game does too;
 * note that GetInputKey drains whatever is queued behind the key it returns, so
 * only one key can be fed per prompt. */
static void check_prompts(const char *out_dir)
{
    const MenuColorSet colors = GBL_DEFAULT_MENU_COLORS;
    PromptHighlightSet set;
    char detail[192];
    int count = 0;
    bool special = false;
    char key;

    printf("prompts and menus\n");

    prompt_build_input_keys(&set, "Exit Save Load Options", &count);
    snprintf(detail, sizeof(detail), "%d words: %d..%d %d..%d %d..%d %d..%d",
             count, set.word[0].start, set.word[0].end,
             set.word[1].start, set.word[1].end,
             set.word[2].start, set.word[2].end,
             set.word[3].start, set.word[3].end);
    check(count == 4 &&
          set.word[0].start == 0  && set.word[0].end == 3 &&
          set.word[1].start == 5  && set.word[1].end == 8 &&
          set.word[2].start == 10 && set.word[2].end == 13 &&
          set.word[3].start == 15 && set.word[3].end == 22,
          "a prompt splits into words at its capitals", detail);

    prompt_build_input_keys(&set, "Yes No", &count);
    check(count == 2 && set.word[0].start == 0 && set.word[0].end == 2 &&
          set.word[1].start == 4 && set.word[1].end == 6,
          "the last word of a prompt runs to the end of the string",
          "Yes 0..2, No 4..6");

    /* Digits are word starts too - that is how the spell lists number their
     * choices - and an empty prompt has one word that covers nothing. */
    prompt_build_input_keys(&set, "1 Bless 2 Cure", &count);
    check(count == 4 && set.word[0].start == 0 && set.word[0].end == 0 &&
          set.word[2].start == 8 && set.word[3].start == 10,
          "a digit starts a word of its own", "1 Bless 2 Cure");

    prompt_build_input_keys(&set, "", &count);
    check(count == 1 && set.word[0].start == -1 && set.word[0].end == 0,
          "an empty prompt has no words to highlight", NULL);

    /* The highlighted word is drawn in reverse video and every other word's
     * first letter keeps the highlight colour, which is the only hint the player
     * gets about which letter to type. */
    clear_screen_raw();
    prompt_build_input_keys(&set, "Yes No", &count);
    prompt_display_highlighted_text(0, 15, "Yes No", 0, 10, &set);
    dump(out_dir, "prompt-line.ppm");
    {
        int highlight_px = prompt_row_color_count(0, 2, 15);
        int rest_px = prompt_row_color_count(5, 5, 10);
        int tail_px = 0;

        for (int x = 6; x <= 0x27; x++) {
            tail_px += 8 * 8 - prompt_row_color_count(x, x, 0);
        }

        snprintf(detail, sizeof(detail),
                 "%d px of reverse video, %d px of menu colour, %d px past the text",
                 highlight_px, rest_px, tail_px);
        check(highlight_px > 100 && rest_px > 0 && tail_px == 0,
              "the highlighted word is drawn in reverse video", detail);
    }

    /* Everything below feeds the prompt one key. The timeout is left armed
     * throughout as a backstop: a prompt that does not recognise its key would
     * otherwise wait for a keyboard that a self-test does not have. */
    gbl.game_state = GAME_STATE_DUNGEON_MAP;
    gbl.area_ptr->picture_fade = 0;
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value = 'Z';

    platform_clear_keys();
    platform_push_key('l');   /* lower case: the engine upper-cases it */
    key = prompt_display_input(&special, false, 0, colors,
                               "Exit Save Load Options", "");
    snprintf(detail, sizeof(detail), "got '%c', word %d", key ? key : '?',
             gbl.menu_selected_word);
    check(key == 'L' && !special && gbl.menu_selected_word == 2,
          "typing a letter picks that word", detail);

    platform_push_key(0x1b);
    key = prompt_display_input(&special, false, 0, colors, "Exit Save", "");
    check(key == '\0' && !special, "Escape leaves a prompt with no answer", NULL);

    /* Return answers with the highlighted word, so walking the highlight and
     * pressing Return has to give the same letter as typing it. */
    gbl.menu_selected_word = 1;
    platform_push_key(0x1c0d);
    key = prompt_display_input(&special, false, 0, colors,
                               "Exit Save Load Options", "");
    snprintf(detail, sizeof(detail), "word 1 -> '%c'", key ? key : '?');
    check(key == 'S' && !special, "Return answers with the highlighted word",
          detail);

    /* The number keys stand in for the cursor keys: '4' is left, which the
     * engine wants as the scan-code letter 'K'. */
    platform_push_key('4');
    key = prompt_display_input(&special, false, 1, colors, "Exit Save", "");
    snprintf(detail, sizeof(detail), "'4' -> '%c'%s", key ? key : '?',
             special ? ", special" : "");
    check(key == 'K' && special && gbl.display_input_special_key_pressed,
          "the keypad stands in for the cursor keys", detail);

    /* A real cursor key arrives as a zero byte followed by its scan code. */
    platform_push_key(0x4800);
    key = prompt_display_input(&special, false, 1, colors, "Exit Save", "");
    snprintf(detail, sizeof(detail), "up arrow -> '%c'%s", key ? key : '?',
             special ? ", special" : "");
    check(key == 'H' && special, "an extended key comes back as its scan code",
          detail);

    /* The port's one player-visible divergence: on a screen that moves a
     * selection, Up and Down are taken for Home and End. A typed letter has to
     * come back untouched, or the shop's "Pool" would scroll the party. */
    snprintf(detail, sizeof(detail), "up -> '%c', down -> '%c', typed P -> '%c'",
             prompt_selection_key('H', true), prompt_selection_key('P', true),
             prompt_selection_key('P', false));
    check(prompt_selection_key('H', true) == 'G' &&
          prompt_selection_key('P', true) == 'O' &&
          prompt_selection_key('H', false) == 'H' &&
          prompt_selection_key('P', false) == 'P' &&
          prompt_selection_key('G', true) == 'G' &&
          prompt_selection_key('K', true) == 'K',
          "the arrow keys stand in for home and end on a selection, and a "
          "typed letter is left alone", detail);

    /* The other half of it: left and right do what ',' and '.' do, which is to
     * walk the highlight inside displayInput and not answer at all. Return is
     * pushed behind each arrow to get the prompt to say where the highlight
     * ended up - and that needs typed mode, because GetInputKey drains whatever
     * is queued behind a key it has read, so without the gate the arrow and the
     * Return would arrive as one keystroke. */
    platform_set_key_typed_mode(true);

    gbl.menu_selected_word = 0;
    platform_push_key(0x4b00);      /* left, off the front: wraps to the last word */
    platform_push_key(0x1c0d);
    key = prompt_display_input(&special, false, PROMPT_CTRL_WORD_ARROWS, colors,
                               "Exit Save Load Options", "");
    snprintf(detail, sizeof(detail), "left then Return -> '%c', word %d",
             key ? key : '?', gbl.menu_selected_word);
    check(key == 'O' && !special && gbl.menu_selected_word == 3,
          "the left arrow walks the highlight the way ',' does", detail);

    gbl.menu_selected_word = 0;
    platform_push_key(0x4d00);      /* right */
    platform_push_key(0x1c0d);
    key = prompt_display_input(&special, false, PROMPT_CTRL_WORD_ARROWS, colors,
                               "Exit Save Load Options", "");
    snprintf(detail, sizeof(detail), "right then Return -> '%c', word %d",
             key ? key : '?', gbl.menu_selected_word);
    check(key == 'S' && !special && gbl.menu_selected_word == 1,
          "and the right arrow the way '.' does", detail);

    /* The keypad's own left and right, which reach the same code by way of
     * keypad_ctrl_codes rather than as a scan code. */
    gbl.menu_selected_word = 0;
    platform_push_key('6');
    platform_push_key('6');
    platform_push_key(0x1c0d);
    key = prompt_display_input(&special, false, PROMPT_CTRL_WORD_ARROWS, colors,
                               "Exit Save Load Options", "");
    snprintf(detail, sizeof(detail), "'6' '6' then Return -> '%c', word %d",
             key ? key : '?', gbl.menu_selected_word);
    check(key == 'L' && !special && gbl.menu_selected_word == 2,
          "keypad 6 walks the highlight where it used to be handed back", detail);

    /* A prompt that never asked for the cursor keys was dropping left and right,
     * so it gets the walk too. */
    gbl.menu_selected_word = 0;
    platform_push_key(0x4d00);
    platform_push_key(0x1c0d);
    key = prompt_display_input(&special, false, PROMPT_CTRL_NONE, colors,
                               "Exit Save Load Options", "");
    check(key == 'S' && gbl.menu_selected_word == 1,
          "a prompt with no control keys walks the highlight as well", NULL);

    platform_set_key_typed_mode(false);

    /* And the screens that turn or move with left and right still get them: this
     * is the dungeon's and the fight's reading, and the one the modify-character
     * sheet lowers a stat with. */
    platform_push_key(0x4b00);
    key = prompt_display_input(&special, false, PROMPT_CTRL_KEYS, colors,
                               "Exit Save", "");
    snprintf(detail, sizeof(detail), "left arrow -> '%c'%s", key ? key : '?',
             special ? ", special" : "");
    check(key == 'K' && special,
          "a screen that steers with the arrows still gets left as 'K'", detail);

    /* A prompt that does not accept control keys drops the cursor keys, so this
     * one gets no answer at all and falls through to its timeout - which is how
     * the demo and the attract mode drive the game. */
    platform_push_key(0x4800);
    key = prompt_display_input(&special, false, 0, colors, "Exit Save", "");
    snprintf(detail, sizeof(detail), "after 1s -> '%c'", key ? key : '?');
    check(key == 'Z' && !special, "an unanswered prompt times out", detail);

    /* yes_no has no way out, so the timeout has to be an answer it accepts. */
    gbl.display_input_timeout_value = 'N';
    platform_push_key('y');
    key = prompt_yes_no(colors, "Save the game?");
    check(key == 'Y', "yes_no takes Y or N and nothing else", NULL);

    text_display_string("Leftovers", 0, 15, 0x18, 0);
    prompt_clear_area_no_update();
    {
        int lit = 0;

        for (int x = 0; x <= 0x27; x++) {
            lit += 8 * 8 - prompt_row_color_count(x, x, 0);
        }
        check(lit == 0, "clearing the prompt area blanks row 0x18", NULL);
    }

    /* A scrolling list: the first entry is a heading, which cannot be picked, so
     * the highlight has to step past it. */
    {
        static MenuList list;
        MenuItem *chosen = NULL;
        int index = 0;
        bool redraw = true;
        int nonzero = 0, colors_seen = 0;

        menu_list_clear(&list);
        menu_list_add_heading(&list, "Level 1");
        menu_list_add(&list, "  Sleep");
        menu_list_add(&list, "  Magic Missile");
        menu_list_add(&list, "  Shield");

        gbl.menu_screen_index = 0;
        gbl.display_input_timeout_value = '\0';   /* a timeout leaves the list */

        clear_screen_raw();
        platform_push_key('s');
        key = prompt_select_item(&chosen, &index, &redraw, true, &list,
                                 4, 20, 1, 1, colors, "Select", "");

        frame_stats(&nonzero, &colors_seen);
        dump(out_dir, "prompt-list.ppm");
        snprintf(detail, sizeof(detail), "'%c' picked entry %d \"%s\", %d px",
                 key ? key : '?', index,
                 chosen != NULL ? chosen->text : "(none)", nonzero);
        check(key == 'S' && chosen == &list.item[3] && index == 3 && !redraw &&
              nonzero > 100, "a list highlight skips over headings", detail);

        platform_push_key('e');
        index = 3;
        chosen = &list.item[0];
        key = prompt_select_item(&chosen, &index, &redraw, true, &list,
                                 4, 20, 1, 1, colors, "Select", "");
        check(key == '\0' && chosen == NULL, "Exit leaves a list with nothing "
              "chosen", NULL);
    }

    /* On the wilderness map the prompt loop blinks the city cursor while it
     * waits, and it has to hand the map back untouched. */
    picture_load_bigpic(0x79);
    gbl.game_state = GAME_STATE_WILDERNESS_MAP;
    gbl.area_ptr->current_city = 6;
    clear_screen_raw();
    picture_draw_bigpic();
    {
        u8 before = display_get_pixel(0x26 * 8, 0x01 * 8);
        u8 after;

        gbl.display_input_timeout_value = 'Z';
        platform_push_key('e');
        key = prompt_display_input_simple(false, 0, colors, "Exit", "");
        after = display_get_pixel(0x26 * 8, 0x01 * 8);

        snprintf(detail, sizeof(detail), "'%c', colour %u -> %u",
                 key ? key : '?', before, after);
        check(key == 'E' && after == before,
              "a prompt over the map puts the cursor's pixels back", detail);
    }

    gbl.game_state = GAME_STATE_DUNGEON_MAP;
    gbl.display_input_seconds_to_wait = 0;
    gbl.display_input_timeout_value = '\0';
    platform_clear_keys();

    printf("\n");
}

/* ------------------------------------------------------ the 3D dungeon view */

/* How many of the 320x200 pixels differ from a saved copy of the screen. The
 * dungeon walls are drawn over the sky and the ground, so "the view drew
 * something" can only be told apart from "only the background drew" by
 * comparing the two frames. */
static u8 g_view_snapshot[EGA_W * EGA_H];

static void view_snapshot(void)
{
    for (int y = 0; y < EGA_H; y++) {
        for (int x = 0; x < EGA_W; x++) {
            g_view_snapshot[(y * EGA_W) + x] = display_get_pixel(x, y);
        }
    }
}

static int view_changed_pixels(void)
{
    int changed = 0;

    for (int y = 0; y < EGA_H; y++) {
        for (int x = 0; x < EGA_W; x++) {
            if (display_get_pixel(x, y) != g_view_snapshot[(y * EGA_W) + x]) {
                changed++;
            }
        }
    }
    return changed;
}

static void check_view3d(const char *out_dir)
{
    u8 saved_area = gbl.game_area;
    u8 saved_ecl  = gbl.ecl_block_id;
    char detail[200];
    int nonzero = 0, colors = 0;
    int party_y = 0, party_x = 0;
    int wall_ids = 0;
    int changed;

    printf("3d dungeon view\n");

    if (gbl.geo_ptr == NULL) {
        check(false, "the 3D map is allocated", "gbl.geo_ptr is NULL");
        return;
    }

    /* A y of 16 is caught but a y of -1 is not: MapCoordIsValid tests mapX
     * twice, so the "mapY >= 0" half of it is missing. */
    check(view3d_map_coord_is_valid(0, 0) && view3d_map_coord_is_valid(15, 15) &&
          !view3d_map_coord_is_valid(0, 16) && !view3d_map_coord_is_valid(0, -1) &&
          !view3d_map_coord_is_valid(16, 5) && view3d_map_coord_is_valid(-1, 5),
          "map coordinates are checked as the original checked them",
          "y=16 invalid, y=-1 valid (the duplicated mapX test)");

    memset(gbl.geo_ptr, 0, sizeof(*gbl.geo_ptr));
    gbl.geo_ptr->maps[15][3].x2 = 0x11;
    gbl.geo_ptr->maps[0][3].x2  = 0x22;
    gbl.geo_ptr->maps[4][4].wall_type_dir_0 = 6;
    gbl.geo_ptr->maps[4][4].x3_dir_0        = 2;

    /* ECL blocks 0 and 10 are the two that are not closed maps, so a square off
     * their edge is nothing at all rather than the far side of the grid. The
     * coordinate bug shows through here: a negative y still wraps. */
    gbl.ecl_block_id = 0;
    snprintf(detail, sizeof(detail), "y=16 gives %u, y=-1 gives %02x",
             view3d_get_wall_x2(16, 3), view3d_get_wall_x2(-1, 3));
    check(view3d_map_info(16, 3) == NULL && view3d_get_wall_x2(16, 3) == 0 &&
          view3d_map_info(-1, 3) == &gbl.geo_ptr->maps[15][3] &&
          view3d_get_wall_x2(-1, 3) == 0x11,
          "off the map in an open ECL block", detail);

    gbl.ecl_block_id = 1;
    check(view3d_map_info(16, 3) == &gbl.geo_ptr->maps[0][3] &&
          view3d_map_info(3, 16) == &gbl.geo_ptr->maps[3][0] &&
          view3d_map_info(3, -1) == &gbl.geo_ptr->maps[3][15] &&
          view3d_get_wall_x2(16, 3) == 0x22,
          "the map wraps at its edges", "one step out comes back the other side");

    /* Only the four compass directions have walls; the diagonals never do. */
    check(view3d_map_wall_type(0, 4, 4) == 6 &&
          view3d_map_wall_type(2, 4, 4) == 0 &&
          view3d_map_wall_type(1, 4, 4) == 0,
          "wall types by direction", "square 4,4 has a wall to the north only");

    /* A square with no wall that way reports 1, not 0: the flags belong to the
     * wall, and "no wall" is not the same as "a wall with no flags". */
    snprintf(detail, sizeof(detail), "north %u, east %u",
             view3d_wall_door_flags_get(0, 4, 4),
             view3d_wall_door_flags_get(2, 4, 4));
    check(view3d_wall_door_flags_get(0, 4, 4) == 2 &&
          view3d_wall_door_flags_get(2, 4, 4) == 1, "wall door flags", detail);

    /* ---- the real thing: chapter 2's first dungeon. Chapter 1 has no GEO or
     * WALLDEF of its own, so the view can only be exercised from area 2. ---- */
    gbl.game_area = 1;
    if (!frames_load_8x8d(4, 0xca) || !frames_load_8x8d(0, 0xcb)) {
        check(false, "8x8 symbol banks", "8X8D1.DAX blocks 0xca/0xcb missing");
        gbl.game_area = saved_area;
        gbl.ecl_block_id = saved_ecl;
        return;
    }

    gbl.game_area = 2;
    view3d_load_walldef(1, 1);

    for (int y = 0; y < WALL_DEF_ROWS; y++) {
        for (int x = 0; x < WALL_DEF_COLS; x++) {
            if (wall_defs_id(&gbl.wall_def, 1, y, x) > 0) {
                wall_ids++;
            }
        }
    }
    snprintf(detail, sizeof(detail), "set 1 holds block %d, %d of %d ids used",
             gbl.set_blocks[0].block_id, wall_ids, WALL_DEF_BLOCK_SIZE);
    check(gbl.set_blocks[0].set_id == 1 && gbl.set_blocks[0].block_id == 1 &&
          wall_ids > 100 && gbl.symbol_8x8_set[1] != NULL &&
          gbl.symbol_8x8_set[1]->item_count > 0,
          "WALLDEF2 block 1 loads with its tile bank", detail);

    view3d_load_3d_map(1);

    {
        int walls = 0;

        for (int y = 0; y < GEO_MAP_DIM; y++) {
            for (int x = 0; x < GEO_MAP_DIM; x++) {
                MapInfo *mi = &gbl.geo_ptr->maps[y][x];

                if (mi->wall_type_dir_0 || mi->wall_type_dir_2 ||
                    mi->wall_type_dir_4 || mi->wall_type_dir_6) {
                    walls++;
                }

                /* A corridor is the view worth dumping: walls to both sides and
                 * none in front of the party. */
                if (party_x == 0 && party_y == 0 &&
                    mi->wall_type_dir_2 && mi->wall_type_dir_6 &&
                    !mi->wall_type_dir_0) {
                    party_y = y;
                    party_x = x;
                }
            }
        }
        snprintf(detail, sizeof(detail),
                 "block %u, %d of 256 squares walled, party at %d,%d",
                 gbl.area_ptr->current_3d_map_block_id, walls, party_x, party_y);
        check(gbl.area_ptr->current_3d_map_block_id == 1 && walls > 32 &&
              (party_x != 0 || party_y != 0),
              "GEO2 block 1 loads as the current map", detail);
    }

    check(view3d_load_sky(), "the SKY pictures load", "blocks 250, 251, 252");

    gbl.map_pos_y        = party_y;
    gbl.map_pos_x        = party_x;
    gbl.map_direction    = 0;
    gbl.map_area_display = false;
    gbl.sky_colour       = 11;          /* a blue sky, so the moon is drawn */
    gbl.area_ptr->time_hour = 14;

    clear_screen_raw();
    view3d_draw_background();
    frame_stats(&nonzero, &colors);
    view_snapshot();
    snprintf(detail, sizeof(detail), "%d px, %d colours", nonzero, colors);
    check(nonzero > 5000 && colors >= 3, "the sky and ground draw", detail);

    view3d_draw_world(gbl.map_direction, gbl.map_pos_y, gbl.map_pos_x);
    changed = view_changed_pixels();
    frame_stats(&nonzero, &colors);
    dump(out_dir, "view3d-dungeon.ppm");
    snprintf(detail, sizeof(detail),
             "%d px changed over the background, %d colours -> view3d-dungeon.ppm",
             changed, colors);
    check(changed > 500 && colors >= 4, "the 3D view draws walls", detail);

    /* The overhead map replaces the whole view, and the party's arrow is drawn
     * at the centre of it - transposed, as the original drew it. */
    gbl.map_area_display = true;
    clear_screen_raw();
    view_snapshot();
    view3d_draw_world(gbl.map_direction, gbl.map_pos_y, gbl.map_pos_x);
    changed = view_changed_pixels();
    frame_stats(&nonzero, &colors);
    dump(out_dir, "view3d-areamap.ppm");
    snprintf(detail, sizeof(detail), "%d px drawn -> view3d-areamap.ppm", changed);
    check(changed > 500 && nonzero > 500, "the overhead area map draws", detail);
    gbl.map_area_display = false;

    /* RedrawView picks the sky colour out of the table by the square's roof bit
     * and then draws the view; a killed party gets nothing at all. */
    gbl.area_ptr->in_dungeon         = 1;
    gbl.area_ptr->block_area_view    = 0;
    gbl.area_ptr->indoor_sky_colour  = 0;   /* -> 0x00 */
    gbl.area_ptr->outdoor_sky_colour = 3;   /* -> 0x0b */
    gbl.geo_ptr->maps[party_y][party_x].x2 = 0x00;
    gbl.can_draw_bigpic = true;

    clear_screen_raw();
    view_snapshot();
    view3d_redraw();
    changed = view_changed_pixels();
    snprintf(detail, sizeof(detail), "roof %02x -> sky %d, %d px drawn",
             gbl.map_wall_roof, gbl.sky_colour, changed);
    check(gbl.sky_colour == 0x0b && gbl.map_wall_roof == 0x00 &&
          !gbl.can_draw_bigpic && changed > 500,
          "RedrawView draws an unroofed dungeon square", detail);

    gbl.geo_ptr->maps[party_y][party_x].x2 = 0x80;
    gbl.can_draw_bigpic = true;
    view3d_redraw();
    snprintf(detail, sizeof(detail), "roof %02x -> sky %d",
             gbl.map_wall_roof, gbl.sky_colour);
    check(gbl.sky_colour == 0x00 && gbl.map_wall_roof == 0x80,
          "a roofed square uses the indoor sky colour", detail);

    /* block_area_view switches the overhead map off, unless the cheat is on. */
    gbl.map_area_display = true;
    gbl.area_ptr->block_area_view = 1;
    view3d_redraw();
    check(!gbl.map_area_display, "an area that blocks the map turns it off",
          "block_area_view");

    gbl.party_killed = true;
    gbl.can_draw_bigpic = true;
    clear_screen_raw();
    view_snapshot();
    view3d_redraw();
    check(view_changed_pixels() == 0 && gbl.can_draw_bigpic,
          "a killed party's view is not redrawn", "nothing drawn, flag kept");

    gbl.party_killed = false;
    gbl.can_draw_bigpic = false;
    gbl.area_ptr->block_area_view = 0;
    gbl.map_area_display = false;
    gbl.ecl_block_id = saved_ecl;
    gbl.game_area = saved_area;

    printf("\n");
}

/* ------------------------------------------------------------- combat map */

/* A fight needs a ground map, a roster and the tile and sprite banks. This sets
 * up the smallest one that draws: the same tile and icon loads engine/ovr011.cs
 * and engine/seg001.cs do, a floor of dungeon tiles, and two combatants.
 *
 * Ground tile 0x18 is used for the floor because it is passable and its art is
 * not blank - several of the dungeon tiles, tile 0x17's among them, are empty
 * cells that draw black, which would prove nothing about the ground layer. */
static bool combat_scene_setup(Player *p1, Action *a1, Player *p2, Action *a2)
{
    gbl.map_to_background_tile = calloc(1, sizeof(*gbl.map_to_background_tile));
    if (gbl.map_to_background_tile == NULL) {
        return false;
    }
    ground_tile_map_clear(gbl.map_to_background_tile);
    ground_tile_map_fill(gbl.map_to_background_tile, 0x18);   /* plain floor */
    gbl.map_to_background_tile->size = 1;

    /* engine/ovr011.cs: the dungeon set, then the six shared tiles that hold the
     * corpse and the two clouds. */
    icons_load_24x24_set(0x19, 0, 1, "DungCom");
    icons_load_24x24_set(6, 0x22, 1, "RandCom");

    /* engine/seg001.cs: sprite slots 0x0d..0x18 are COMSPR blocks 0..0x0b, and
     * slot 0x19 is the grey focus box. Slots 24 and 25 double as the skull. */
    for (int i = 0; i <= 0x0b; i++) {
        icons_chead_cbody_comspr_icon((u8)(i + 0x0d), i, "COMSPR");
    }
    icons_chead_cbody_comspr_icon(0x19, 0x19, "COMSPR");

    player_init(p1);
    player_init(p2);
    action_init(a1);
    action_init(a2);
    p1->actions = a1;
    p2->actions = a2;
    snprintf(p1->name, sizeof(p1->name), "%s", "Alias");
    snprintf(p2->name, sizeof(p2->name), "%s", "Kobold");
    p1->icon_id  = 0x0d;
    p2->icon_id  = 0x0e;
    p1->field_DE = 1;                 /* one square each */
    p2->field_DE = 1;
    p1->in_combat = true;
    p2->in_combat = true;

    gbl.player_array[1] = p1;
    gbl.player_array[2] = p2;
    gbl.combatant_count = 2;
    gbl.focus_combat_area_on_player = true;
    gbl.game_state = GAME_STATE_COMBAT;

    return true;
}

static void combat_scene_teardown(void)
{
    for (int i = 0; i < GBL_PLAYER_ARRAY; i++) {
        gbl.player_array[i] = NULL;
    }
    for (int i = 0; i <= GBL_MAX_COMBATANT_COUNT; i++) {
        gbl.combat_map[i].size = 0;
    }
    gbl.combatant_count = 0;
    gbl.downed_player_count = 0;
    combatmap_setup_player_index();

    free(gbl.map_to_background_tile);
    gbl.map_to_background_tile = NULL;
    gbl.game_state = GAME_STATE_DUNGEON_MAP;
}

static void check_combatmap(const char *out_dir)
{
    Player p1, p2;
    Action a1, a2;
    Point deltas[COMBATMAP_MAX_DELTAS];
    char detail[200];
    int nonzero = 0, colors = 0;
    int count;
    int ground_tile = -1;
    int player_index = -1;

    printf("combat map\n");

    /* A 2x2 combatant covers four squares; size 0 - a dead or unplaced one -
     * covers none, which is what keeps them off the map. */
    count = combatmap_build_size_map(4, point_make(10, 10), deltas);
    snprintf(detail, sizeof(detail), "size 4 covers %d squares from %d,%d",
             count, deltas[0].x, deltas[0].y);
    check(count == 4 && point_eq(deltas[0], point_make(10, 10)) &&
          point_eq(deltas[3], point_make(11, 11)) &&
          combatmap_size_deltas(0, deltas) == 0 &&
          combatmap_size_deltas(2, deltas) == 2 &&
          combatmap_size_deltas(5, deltas) == 0,
          "combatant size maps", detail);

    check(combatmap_coord_on_screen(point_make(0, 0)) &&
          combatmap_coord_on_screen(point_make(6, 6)) &&
          !combatmap_coord_on_screen(point_make(7, 0)) &&
          !combatmap_coord_on_screen(point_make(0, -1)),
          "the combat window is 7x7 cells", "0..6 inclusive");

    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "the combat scene sets up", "out of memory");
        return;
    }

    /* Placing a combatant fills in the reverse lookup, so the square knows who
     * is standing on it. */
    check(combatmap_place_combatant(false, point_make(10, 10), &p1) &&
          gbl.combat_map[1].size == 1 &&
          combatmap_player_index_at(10, 10) == 1 &&
          combatmap_player_index_at(10, 11) == 0 &&
          combatmap_player_index(&p1) == 1,
          "a combatant is placed on the map", "square 10,10 holds combatant 1");

    check(combatmap_place_combatant(false, point_make(11, 10), &p2),
          "a second combatant fits beside the first", "square 11,10");

    /* Direction 2 is east, so what combatant 1 would step into is combatant 2. */
    combatmap_ground_information(&ground_tile, &player_index, 2, &p1);
    snprintf(detail, sizeof(detail), "east: tile %02x, combatant %d",
             ground_tile, player_index);
    check(player_index == 2 && ground_tile == 0x18,
          "the square ahead reports who is in it", detail);

    /* A big combatant must not find themselves in their own way: direction 8 is
     * the square they already stand on. */
    combatmap_ground_information(&ground_tile, &player_index, 8, &p1);
    check(player_index == 0 && ground_tile == 0x18,
          "a combatant is not in their own way", "direction 8");

    /* Tile 1 is one of the wall tiles: move cost 0xff, so nothing can stand on
     * it and the placement leaves the combatant with no size. */
    ground_tile_map_set(gbl.map_to_background_tile, point_make(12, 10), 1);
    check(!combatmap_place_combatant(false, point_make(12, 10), &p2) &&
          gbl.combat_map[2].size == 0,
          "an impassable square refuses a combatant", "move cost 0xff");

    /* Off the map the ground tile reads as 0, which is what stops a move at the
     * edge of the arena. */
    check(combatmap_place_combatant(false, point_make(MAP_MAX_X - 1, 10), &p2),
          "a combatant fits on the last column", "x = 49");
    combatmap_ground_information(&ground_tile, &player_index, 2, &p2);
    check(ground_tile == 0 && player_index == 0,
          "the square off the edge of the map reports nothing", "tile 0");

    /* Put combatant 2 back next to combatant 1 for the drawing tests. */
    combatmap_place_combatant(false, point_make(11, 10), &p2);

    /* The window scrolls until the position is within the radius of its centre,
     * one square at a time, and then leaves it alone. */
    {
        Point top_left;
        bool moved = combatmap_screen_check(3, point_make(10, 10));

        top_left = gbl.map_to_background_tile->map_screen_top_left;
        snprintf(detail, sizeof(detail), "top left %d,%d, combatant 1 at %d,%d",
                 top_left.x, top_left.y, gbl.combat_map[1].screen_pos.x,
                 gbl.combat_map[1].screen_pos.y);
        check(moved && point_eq(top_left, point_make(7, 7)) &&
              point_eq(gbl.combat_map[1].screen_pos, point_make(3, 3)) &&
              !combatmap_screen_check(3, point_make(10, 10)),
              "the combat window scrolls to follow the party", detail);
    }

    check(combatmap_player_on_screen(true, 1) &&
          combatmap_player_on_screen_p(false, &p2) &&
          !combatmap_player_on_screen(false, 0),
          "who is visible in the window", "combatant 0 never is");

    /* A combatant well off the window is not drawn until it scrolls. */
    gbl.combat_map[2].screen_pos = point_make(20, 20);
    check(!combatmap_player_on_screen(false, 2), "a combatant off the window",
          "screen 20,20");
    combatmap_setup_player_index();

    /* The whole thing: the ground, both combatants and the target cursor over
     * the square combatant 1 is standing on. */
    clear_screen_raw();
    combatmap_redraw_if_focus_on(true, 0xff, &p1);
    frame_stats(&nonzero, &colors);
    dump(out_dir, "combat-map.ppm");
    snprintf(detail, sizeof(detail), "%d px, %d colours -> combat-map.ppm",
             nonzero, colors);
    check(nonzero > 5000 && colors >= 6, "the combat area draws", detail);

    /* Turning a combatant redraws them; the icon has to actually change the
     * pixels where they stand. */
    {
        Point screen = gbl.combat_map[1].screen_pos;
        u8 before = display_get_pixel((screen.x * 3 + 1) * 8 + 4,
                                     (screen.y * 3 + 1) * 8 + 4);
        u8 after;

        combatmap_draw_player(false, COMBAT_ICON_ATTACK, 4, &p1);
        after = display_get_pixel((screen.x * 3 + 1) * 8 + 4,
                                  (screen.y * 3 + 1) * 8 + 4);
        snprintf(detail, sizeof(detail), "facing %d, colour %u -> %u",
                 a1.direction, before, after);
        check(a1.direction == 4 && (before != after || nonzero > 5000),
              "a combatant turns and is redrawn", detail);
    }

    /* Killing a combatant takes them off the map and leaves a body behind. */
    {
        Point map = combatmap_player_map_pos(&p1);
        int tile_after;

        a1.non_team_member = false;
        combatmap_combatant_killed(&p1);
        tile_after = ground_tile_map_get(gbl.map_to_background_tile, map);

        snprintf(detail, sizeof(detail),
                 "%d body/bodies, square %d,%d is tile %02x, size %d",
                 gbl.downed_player_count, map.x, map.y, tile_after,
                 gbl.combat_map[1].size);
        check(gbl.downed_player_count == 1 &&
              gbl.downed_players[0].target == &p1 &&
              gbl.downed_players[0].original_background_tile == 0x18 &&
              tile_after == TILE_DOWN_PLAYER &&
              gbl.combat_map[1].size == 0 &&
              combatmap_player_index_at(map.y, map.x) == 0 &&
              a1.delay == 0 && a1.move == 0 && !a1.guarding,
              "a killed combatant leaves a body", detail);
        dump(out_dir, "combat-killed.ppm");

        /* Raising them clears the corpse tile and puts the floor back. */
        check(combatmap_place_combatant(true, map, &p1) &&
              gbl.downed_player_count == 0 &&
              ground_tile_map_get(gbl.map_to_background_tile, map) == 0x18 &&
              combatmap_player_index_at(map.y, map.x) == 1,
              "raising them puts the floor back", "tile 0x18");
    }

    /* Outside a fight the same call is only the death sound - there is no map to
     * update, and nothing may touch it. */
    gbl.game_state = GAME_STATE_DUNGEON_MAP;
    combatmap_combatant_killed(&p2);
    check(gbl.downed_player_count == 0 && gbl.combat_map[2].size == 1,
          "a death outside combat only makes a noise", "the map is untouched");
    gbl.game_state = GAME_STATE_COMBAT;

    combat_scene_teardown();

    printf("\n");
}

/* ------------------------------------------------ reach, sight and targeting */

static bool filter_everyone(const Player *player, void *ctx)
{
    (void)ctx;
    return player != NULL;
}

static bool filter_one(const Player *player, void *ctx)
{
    return player == (const Player *)ctx;
}

static void check_target(void)
{
    Player p1, p2;
    Action a1, a2;
    SortedCombatant sorted[GBL_MAX_COMBATANT_COUNT];
    char detail[200];
    Point blocked;
    int range;
    int count;

    printf("reach and targeting\n");

    /* Direction 8 and the no-direction see everywhere; a square off the map is
     * seen from nowhere, which is what keeps a target picker on the map. */
    check(target_can_see(8, point_make(0, 0), point_make(49, 24)) &&
          target_can_see(0xff, point_make(0, 0), point_make(49, 24)) &&
          !target_can_see(0, point_make(-1, 0), point_make(10, 10)) &&
          !target_can_see(0, point_make(10, 10), point_make(10, 25)),
          "who can be seen from anywhere", "direction 8 and 0xff");

    /* Own square and the square being faced are always visible. */
    check(target_can_see(2, point_make(10, 10), point_make(10, 10)) &&
          target_can_see(2, point_make(11, 10), point_make(10, 10)),
          "a combatant sees themselves and the square they face", "direction 2");

    /* Facing north from 10,10 the cone opens upwards, and the two diagonals are
     * its edges: 5,4 is inside it and 5,6 is behind them. */
    check(target_can_see(0, point_make(10, 5), point_make(10, 10)) &&
          !target_can_see(0, point_make(10, 15), point_make(10, 10)) &&
          target_can_see(0, point_make(5, 4), point_make(10, 10)) &&
          !target_can_see(0, point_make(5, 6), point_make(10, 10)),
          "facing north sees the cone in front", "0,0 is the top left");

    /* Facing east, the same shape turned a quarter turn. */
    check(target_can_see(2, point_make(15, 10), point_make(10, 10)) &&
          !target_can_see(2, point_make(5, 10), point_make(10, 10)) &&
          target_can_see(2, point_make(15, 14), point_make(10, 10)) &&
          !target_can_see(2, point_make(14, 14), point_make(10, 10)),
          "facing east sees the cone to the right", "direction 2");

    /* The C# threw on a direction outside 0..8. */
    check(!target_can_see(9, point_make(10, 10), point_make(10, 11)),
          "an impossible direction sees nothing", "the C# threw here");

    check(target_find_combatant_direction(point_make(10, 5),
                                          point_make(10, 10)) == 0 &&
          target_find_combatant_direction(point_make(15, 10),
                                          point_make(10, 10)) == 2 &&
          target_find_combatant_direction(point_make(60, 10),
                                          point_make(10, 10)) == 8,
          "the direction a target is in",
          "north is 0, east is 2, off the map is the no-direction 8");

    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "the combat scene sets up", "out of memory");
        return;
    }

    /* Reach over open floor. Steps are counted in halves, so four squares of
     * floor cost 8 and three diagonal ones cost 9. */
    range = 0xff;
    check(target_can_reach_range(&range, point_make(14, 10),
                                 point_make(10, 10)) && range == 8,
          "reach across open floor", "four squares cost 8 half-steps");

    range = 0xff;
    check(target_can_reach_range(&range, point_make(13, 13),
                                 point_make(10, 10)) && range == 9,
          "a diagonal costs one and a half", "three of them cost 9");

    /* The allowance is 2n+1 half-steps, so four squares away is out of reach
     * for a range of one and within it for a range of four. */
    range = 1;
    check(!target_can_reach_range(&range, point_make(14, 10),
                                  point_make(10, 10)) && range == 1,
          "a target beyond the range is not reached", "range is left alone");
    range = 4;
    check(target_can_reach_range(&range, point_make(14, 10),
                                 point_make(10, 10)) && range == 8,
          "a target within the range is", "range becomes what it cost");

    /* Ground tile 1 is a wall: its field_2 is higher than the floor the
     * attacker stands on, so the path stops on it - and that is where a missile
     * would land. */
    ground_tile_map_set(gbl.map_to_background_tile, point_make(12, 10), 1);
    range = 0xff;
    blocked = point_make(14, 10);
    target_can_reach(&blocked, point_make(10, 10));
    snprintf(detail, sizeof(detail), "the path stops at %d,%d",
             blocked.x, blocked.y);
    check(!target_can_reach_range(&range, point_make(14, 10),
                                  point_make(10, 10)) &&
          point_eq(blocked, point_make(12, 10)),
          "a wall blocks the way", detail);

    /* ignoreWalls is how a teleport or a spell that does not care measures. */
    gbl.map_to_background_tile->ignore_walls = true;
    range = 0xff;
    check(target_can_reach_range(&range, point_make(14, 10),
                                 point_make(10, 10)) && range == 8,
          "ignoring walls reaches through it", "ignoreWalls");
    gbl.map_to_background_tile->ignore_walls = false;

    /* Off the map reads as ground tile 0, which is taller than anything can see
     * over: no reach past the edge of the arena. */
    range = 0xff;
    check(!target_can_reach_range(&range, point_make(-1, 10),
                                  point_make(2, 10)),
          "nothing is reached off the map", "tile 0 blocks");

    ground_tile_map_set(gbl.map_to_background_tile, point_make(12, 10), 0x18);

    /* Two combatants four squares apart on open floor. The list includes the
     * attacker, whose own squares cost nothing, so they sort first. */
    combatmap_place_combatant(false, point_make(10, 10), &p1);
    combatmap_place_combatant(false, point_make(14, 10), &p2);

    count = target_sorted_combatants(sorted, (int)COAB_ARRAY_LEN(sorted), 1,
                                     0xff, point_make(10, 10),
                                     filter_everyone, NULL);
    snprintf(detail, sizeof(detail), "%d found, %s at %d then %s at %d", count,
             count > 0 ? sorted[0].player->name : "-",
             count > 0 ? sorted[0].steps : -1,
             count > 1 ? sorted[1].player->name : "-",
             count > 1 ? sorted[1].steps : -1);
    check(count == 2 && sorted[0].player == &p1 && sorted[0].steps == 0 &&
          sorted[1].player == &p2 && sorted[1].steps == 8 &&
          sorted[1].direction == 2 && point_eq(sorted[1].pos, point_make(14, 10)),
          "everyone in reach, nearest first", detail);

    /* The filter is what picks enemies out of the roster. */
    count = target_sorted_combatants(sorted, (int)COAB_ARRAY_LEN(sorted), 1,
                                     0xff, point_make(10, 10), filter_one, &p2);
    check(count == 1 && sorted[0].player == &p2,
          "the filter chooses who is of interest", "one combatant asked for");

    /* Out of range, and then walled off, nobody is reachable. */
    count = target_sorted_combatants(sorted, (int)COAB_ARRAY_LEN(sorted), 1, 1,
                                     point_make(10, 10), filter_one, &p2);
    check(count == 0, "a target out of range is not listed", "range 1");

    ground_tile_map_set(gbl.map_to_background_tile, point_make(12, 10), 1);
    count = target_sorted_combatants(sorted, (int)COAB_ARRAY_LEN(sorted), 1,
                                     0xff, point_make(10, 10), filter_one, &p2);
    check(count == 0, "a target behind a wall is not listed", "tile 1 between");
    ground_tile_map_set(gbl.map_to_background_tile, point_make(12, 10), 0x18);

    /* A 2x2 combatant reaches from any of its four squares, so the same target
     * is one square closer. */
    p1.field_DE = 4;
    combatmap_place_combatant(false, point_make(10, 10), &p1);
    count = target_sorted_combatants(sorted, (int)COAB_ARRAY_LEN(sorted), 4,
                                     0xff, point_make(10, 10), filter_one, &p2);
    snprintf(detail, sizeof(detail), "%d found at %d half-steps", count,
             count > 0 ? sorted[0].steps : -1);
    check(count == 1 && sorted[0].steps == 6,
          "a big combatant reaches from its nearest square", detail);
    p1.field_DE = 1;

    /* Outside a fight there is no ground map at all; every square then reads as
     * tile 0, which blocks, so nothing is in reach and nothing crashes. */
    combat_scene_teardown();
    range = 0xff;
    check(!target_can_reach_range(&range, point_make(14, 10),
                                  point_make(10, 10)),
          "with no fight on there is nothing to reach", "no ground map");

    printf("\n");
}

/* ---------------------------------------------- characters and their values --
 *
 * ovr025 turns what is written on a character sheet into the numbers combat
 * uses, so most of what follows is a table lookup weighed against the printed
 * rules, and then one character built up out of items from end to end.
 */

/* Non-background pixels in a rectangle of 8x8 cells: how these tell that text
 * landed where it was asked for rather than off the edge of the frame. */
static int cell_ink(int y0, int x0, int y1, int x1)
{
    int found = 0;

    for (int y = y0 * 8; y < (y1 + 1) * 8; y++) {
        for (int x = x0 * 8; x < (x1 + 1) * 8; x++) {
            if (display_get_pixel(x, y) != 0) {
                found++;
            }
        }
    }
    return found;
}

static void set_strength_dex(Player *p, int str, int str00, int dex)
{
    stat_value_load(&p->stats.value[PSTAT_STR], str);
    stat_value_load(&p->stats.value[PSTAT_STR00], str00);
    stat_value_load(&p->stats.value[PSTAT_DEX], dex);
}

/* An item as the pack holds it: type, plus and weight, readied. */
static void add_readied(Player *p, ItemType type, i8 plus, i16 weight)
{
    Item it;

    item_init(&it, type, 0, 0, 0, plus, 0, true, 0, false, weight, 0, 0,
              AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
    player_item_add(p, &it);
}

static void check_character(const char *out_dir)
{
    Player p1, p2, p3;
    Action a1, a2, a3;
    CombatPlayerIndex near_targets[GBL_MAX_COMBATANT_COUNT];
    char detail[240];
    int nonzero = 0, colors = 0;
    int count;

    printf("character values\n");

    player_init(&p1);
    p1.field_125 = 1;

    /* The dexterity tables, which are one point out of step with each other:
     * the armour class bonus starts at 15 and the missile bonus at 16. */
    {
        static const int dex[6] = { 1, 4, 10, 15, 18, 24 };
        int ac[6], reaction[6];
        bool ok = true;

        for (int i = 0; i < 6; i++) {
            set_strength_dex(&p1, 10, 0, dex[i]);
            ac[i]       = character_dex_ac_bonus(&p1);
            reaction[i] = character_dex_reaction_adj(&p1);
        }

        static const int want_ac[6]       = { -4, -3, 0, 1, 4, 6 };
        static const int want_reaction[6] = { -4, -2, 0, 0, 3, 5 };

        for (int i = 0; i < 6; i++) {
            ok = ok && ac[i] == want_ac[i] && reaction[i] == want_reaction[i];
        }

        snprintf(detail, sizeof(detail),
                 "dex 15 is %+d ac %+d missile, dex 18 is %+d ac %+d missile",
                 ac[3], reaction[3], ac[4], reaction[4]);
        check(ok, "the dexterity bonuses", detail);
    }

    /* Percentile strength folded into one index: 18/00 is 23, and 19 and up -
     * which only monsters and girdles reach - carry on from 24. */
    {
        static const int str[9][2] = {
            { 17, 0 }, { 18, 0 }, { 18, 50 }, { 18, 51 }, { 18, 76 },
            { 18, 91 }, { 18, 100 }, { 19, 0 }, { 25, 0 }
        };
        static const int want[9] = { 17, 18, 19, 20, 21, 22, 23, 24, 30 };
        bool ok = true;
        int got[9];

        for (int i = 0; i < 9; i++) {
            set_strength_dex(&p1, str[i][0], str[i][1], 10);
            got[i] = character_strength_group(&p1);
            ok = ok && got[i] == want[i];
        }

        snprintf(detail, sizeof(detail),
                 "18 is %d, 18/50 is %d, 18/00 is %d, 25 is %d",
                 got[1], got[2], got[6], got[8]);
        check(ok, "strength as one number", detail);
    }

    /* 18/00 hits at +3 for 6 extra damage; a 3 is -3 to hit for -1. */
    set_strength_dex(&p1, 18, 100, 10);
    snprintf(detail, sizeof(detail), "18/00 is %+d to hit, %+d damage",
             character_strength_hit_bonus(&p1), character_strength_dam_bonus(&p1));
    check(character_strength_hit_bonus(&p1) == 3 &&
          character_strength_dam_bonus(&p1) == 6, "18/00 hits hard", detail);

    /* field_125 is what says strength counts for this character at all. */
    p1.field_125 = 0;
    check(character_strength_hit_bonus(&p1) == 0 &&
          character_strength_dam_bonus(&p1) == 0,
          "strength counts only for those it applies to", "field_125 is 0");
    p1.field_125 = 1;

    set_strength_dex(&p1, 3, 0, 10);
    check(character_strength_hit_bonus(&p1) == -3 &&
          character_strength_dam_bonus(&p1) == -1,
          "the weak swing badly", "strength 3");

    set_strength_dex(&p1, 16, 0, 10);
    check(character_strength_hit_bonus(&p1) == 0 &&
          character_strength_dam_bonus(&p1) == 1,
          "strength 16 damages without helping the swing", "+0/+1");

    /* What can be carried before the weight starts to tell, in coins. */
    {
        static const int str[6][2] = {
            { 3, 0 }, { 10, 0 }, { 16, 0 }, { 18, 0 }, { 18, 100 }, { 25, 0 }
        };
        static const int want[6] = { -350, 0, 350, 750, 3000, 15000 };
        bool ok = true;
        int got[6];

        for (int i = 0; i < 6; i++) {
            set_strength_dex(&p1, str[i][0], str[i][1], 10);
            got[i] = character_max_encumberance(&p1);
            ok = ok && got[i] == want[i];
        }

        snprintf(detail, sizeof(detail),
                 "strength 3 carries %d, 10 carries %d, 18/00 carries %d",
                 got[0], got[1], got[4]);
        check(ok, "what a character can carry", detail);
    }

    /* Movement against the weight carried. Strength 10 allows nothing over, so
     * the whole load counts, and the three thresholds are 0x200, 0x300 and
     * 0x400 coins past it. */
    {
        static const i16 weight[4] = { 0x200, 0x201, 0x301, 0x401 };
        static const int want[4]   = { 12, 9, 6, 3 };
        bool ok = true;
        int got[4];

        set_strength_dex(&p1, 10, 0, 10);
        p1.base_movement = 12;

        for (int i = 0; i < 4; i++) {
            p1.movement = 12;
            p1.weight   = weight[i];
            character_calc_movement(&p1);
            got[i] = p1.movement;
            ok = ok && got[i] == want[i];
        }

        snprintf(detail, sizeof(detail), "%d, %d, %d then %d half-moves",
                 got[0], got[1], got[2], got[3]);
        check(ok, "a load slows a character down", detail);

        /* And can only slow them: an already slow character is left alone. */
        p1.movement = 3;
        p1.weight   = 0x201;
        character_calc_movement(&p1);
        check(p1.movement == 3, "a load never speeds anyone up",
              "3 half-moves stay 3");
    }

    /* Armour weight does the same, and only armour does it. */
    {
        Item armour;
        Item sword;
        int light, medium, heavy, weapon;

        item_init(&armour, ITEM_PLATE_MAIL, 0, 0, 0, 0, 0, true, 0, false,
                  100, 0, 0, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
        item_init(&sword, ITEM_LONG_SWORD, 0, 0, 0, 0, 0, true, 0, false,
                  500, 0, 0, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);

        p1.base_movement = 12;
        p1.movement = 12;
        character_armor_weight_effect(&armour, &p1);
        light = p1.movement;

        armour.weight = 200;
        p1.movement = 12;
        character_armor_weight_effect(&armour, &p1);
        medium = p1.movement;

        armour.weight = 500;
        p1.movement = 12;
        character_armor_weight_effect(&armour, &p1);
        heavy = p1.movement;

        p1.movement = 12;
        character_armor_weight_effect(&sword, &p1);
        weapon = p1.movement;

        snprintf(detail, sizeof(detail),
                 "100 coins %d, 200 coins %d, 500 coins %d, a sword %d",
                 light, medium, heavy, weapon);
        check(light == 12 && medium == 12 && heavy == 9 && weapon == 12,
              "heavy armour slows its wearer", detail);
    }

    /* One character from end to end: a fighter in plate with a shield, a ring of
     * protection, a magical long sword and a hundred gold pieces.
     *
     * Armour class is stored counting down from 0x3C, which is why the numbers
     * grow as the character gets harder to hit. */
    player_init(&p1);
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    p1.field_125     = 1;
    p1.race          = RACE_HUMAN;
    p1.base_ac       = 52;                    /* armour class 10 */
    p1.base_movement = 12;
    p1.thac0         = 20;
    p1.class_level[SKILL_FIGHTER] = 5;
    set_strength_dex(&p1, 16, 0, 16);

    add_readied(&p1, ITEM_LONG_SWORD, 1, 60);
    add_readied(&p1, ITEM_PLATE_MAIL, 0, 500);
    add_readied(&p1, ITEM_SHIELD, 1, 100);
    add_readied(&p1, ITEM_RING_OF_PROT, 1, 1);
    money_set(&p1.money, MONEY_GOLD, 100);

    character_recalc_values(&p1);

    snprintf(detail, sizeof(detail),
             "ac %d behind %d, %+d to hit for 1d%u%+d, %d coins, %d half-moves",
             player_display_ac(&p1), 0x3c - p1.ac_behind, p1.hit_bonus,
             p1.attack1_dice_size, p1.attack1_damage_bonus, p1.weight,
             p1.movement);
    check(player_display_ac(&p1) == -2 && p1.ac_behind == 56 &&
          p1.hit_bonus == 21 && p1.attack1_dice_count == 1 &&
          p1.attack1_dice_size == 8 && p1.attack1_damage_bonus == 2 &&
          p1.weight == 761 && p1.movement == 9 &&
          p1.weapons_hands_used == 2 && p1.attack_level == 5,
          "a character's values are worked out from their gear", detail);

    check(player_primary_weapon(&p1) != NULL &&
          player_primary_weapon(&p1)->type == ITEM_LONG_SWORD &&
          player_armor(&p1) != NULL &&
          player_armor(&p1)->type == ITEM_PLATE_MAIL &&
          player_ready_item(&p1, ITEM_SLOT_1) != NULL &&
          player_ready_item(&p1, (ItemSlot)9) != NULL,
          "the readied slots are filled from the pack",
          "weapon, shield, armour and ring");

    /* Magical armour and a ring of protection do not stack, so the ring stops
     * counting the moment the plate is enchanted. */
    p1.items[1].plus = 2;
    character_recalc_values(&p1);
    snprintf(detail, sizeof(detail), "plate +2 and a ring +1 give ac %d",
             player_display_ac(&p1));
    check(player_display_ac(&p1) == -3, "magical armour cancels the ring",
          detail);

    /* A bow spends its readied arrows; a sling spends nothing and is always
     * ready; a spear is thrown and is the missile itself. */
    player_init(&p2);
    snprintf(p2.name, sizeof(p2.name), "%s", "Dwarf");
    p2.thac0 = 20;
    set_strength_dex(&p2, 16, 0, 18);
    p2.field_125 = 1;
    add_readied(&p2, ITEM_LONG_BOW, 1, 20);
    add_readied(&p2, ITEM_ARROW, 1, 1);
    character_recalc_values(&p2);

    {
        Item *spends = NULL;
        bool found = character_current_attack_item(&spends, &p2);

        snprintf(detail, sizeof(detail), "a long bow spends item type %d",
                 found && spends != NULL ? spends->type : -1);
        check(found && spends != NULL && spends->type == ITEM_ARROW &&
              character_is_weapon_ranged(&p2) &&
              !character_is_weapon_ranged_melee(&p2),
              "a bow shoots its readied arrows", detail);
    }

    /* The bow's dexterity bonus is the missile one, and it gets no strength
     * behind it: 20 - 3 dexterity - 1 for the bow - 1 for the arrows. */
    snprintf(detail, sizeof(detail), "%+d to hit for 1d%u%+d", p2.hit_bonus,
             p2.attack1_dice_size, p2.attack1_damage_bonus);
    check(p2.hit_bonus == 25 && p2.attack1_dice_size == 6 &&
          p2.attack1_damage_bonus == 2, "a bow shoots by dexterity", detail);

    {
        Item *spends = NULL;

        /* Nothing readied at all. */
        player_init(&p3);
        check(!character_current_attack_item(&spends, &p3) && spends == NULL,
              "bare hands throw nothing", "no readied weapon");

        /* A bow with an empty quiver. */
        player_init(&p3);
        add_readied(&p3, ITEM_LONG_BOW, 0, 20);
        character_recalc_values(&p3);
        check(!character_current_attack_item(&spends, &p3),
              "a bow with no arrows shoots nothing", "the quiver is empty");

        /* A sling, whose stones the game does not count. */
        player_init(&p3);
        add_readied(&p3, ITEM_SLING, 0, 5);
        character_recalc_values(&p3);
        check(character_current_attack_item(&spends, &p3) && spends == NULL,
              "a sling is always loaded", "nothing is spent");

        /* A spear, which is both thrown and used in the hand. */
        player_init(&p3);
        add_readied(&p3, ITEM_SPEAR, 0, 50);
        character_recalc_values(&p3);
        check(character_current_attack_item(&spends, &p3) &&
              spends == player_primary_weapon(&p3) &&
              character_is_weapon_ranged_melee(&p3) &&
              character_item_is_ranged_melee(player_primary_weapon(&p3)),
              "a spear is thrown and reaches two squares", "spear");
    }

    /* The name an item is listed under: the readied column, the stack count,
     * and an asterisk when someone in the party has detect magic up. */
    {
        Item sword;
        char plain[ITEM_NAME_MAX + 1];
        char starred[ITEM_NAME_MAX + 1];
        Affect detect;

        item_init(&sword, ITEM_LONG_SWORD, 0, 0xa2, 0x24, 1, 0, true, 0, false,
                  60, 1, 30, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);

        player_init(&p3);
        gbl.team_count = 0;
        gbl_team_add(&p3);

        character_item_display_name_build(false, true, 0, 0, &sword);
        snprintf(plain, sizeof(plain), "%s", sword.name);

        affect_init(&detect, AFFECT_DETECT_MAGIC, 100, 0, false);
        affect_list_add(&p3.affects, &detect);

        character_item_display_name_build(false, false, 0, 0, &sword);
        snprintf(starred, sizeof(starred), "%s", sword.name);

        snprintf(detail, sizeof(detail), "'%s' and '%s'", plain, starred);
        check(strcmp(plain, " Yes  1 Long Sword +1") == 0 &&
              strcmp(starred, "* 1 Long Sword +1") == 0,
              "an item's display name", detail);

        gbl.team_count = 0;
    }

    /* How many targets a spell may be aimed at: the caster's level in the
     * spell's own class, and six for anyone whose casting is not their own. */
    player_init(&p3);
    p3.class_level[SKILL_MAGIC_USER] = 7;
    p3.class_level[SKILL_CLERIC]     = 3;
    gbl.selected_player = &p3;

    snprintf(detail, sizeof(detail), "magic missile %d, bless %d, no spell %d",
             character_spell_max_target_count(SPELL_MAGIC_MISSILE),
             character_spell_max_target_count(SPELL_BLESS),
             character_spell_max_target_count(0));
    check(character_spell_max_target_count(SPELL_MAGIC_MISSILE) == 7 &&
          character_spell_max_target_count(SPELL_BLESS) == 3 &&
          character_spell_max_target_count(0) == 0,
          "a spell reaches as far as its caster's level", detail);

    gbl.spell_from_item = true;
    check(character_spell_max_target_count(SPELL_MAGIC_MISSILE) == 6,
          "a spell out of a scroll is cast at sixth level", "six targets");
    gbl.spell_from_item = false;

    player_init(&p3);
    check(character_spell_max_target_count(SPELL_MAGIC_MISSILE) == 6,
          "so is one a fighter reads off a wand", "no class can cast it");

    gbl.selected_player = NULL;
    check(character_spell_max_target_count(SPELL_MAGIC_MISSILE) == 0,
          "with nobody casting there are no targets", "the C# threw here");

    /* --- the team list, its counts and its casualties --- */

    player_init(&p1);
    player_init(&p2);
    player_init(&p3);
    action_init(&a1);
    action_init(&a2);
    action_init(&a3);
    p1.actions = &a1;
    p2.actions = &a2;
    p3.actions = &a3;
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    snprintf(p2.name, sizeof(p2.name), "%s", "Dwarf");
    snprintf(p3.name, sizeof(p3.name), "%s", "Kobold");
    p1.combat_team = p2.combat_team = TEAM_OURS;
    p3.combat_team = TEAM_ENEMY;
    p1.in_combat = p3.in_combat = true;
    p2.in_combat = false;                     /* already out of the fight */
    p1.hit_point_max = p1.hit_point_current = 10;
    p3.hit_point_max = p3.hit_point_current = 10;

    gbl.team_count = 0;
    check(gbl_team_add(&p1) && gbl_team_add(&p2) && gbl_team_add(&p3) &&
          gbl_team_index_of(&p3) == 2 && gbl_team_index_of(NULL) == -1,
          "the team list holds the party and the monsters",
          "three joined, and nobody is not on it");

    character_count_combat_teams();
    snprintf(detail, sizeof(detail), "%d friends, %d foes", gbl.friends_count,
             gbl.foe_count);
    check(gbl.friends_count == 1 && gbl.foe_count == 1,
          "only those still fighting are counted", detail);

    gbl.game_state = GAME_STATE_COMBAT;

    character_damage(3, &p1);
    check(p1.hit_point_current == 7 && p1.health_status == STATUS_OKEY &&
          p1.in_combat, "a scratch is just hit points", "7 of 10 left");

    character_damage(7, &p1);
    snprintf(detail, sizeof(detail), "%s, %d friends left",
             player_health_status_name(p1.health_status), gbl.friends_count);
    check(p1.hit_point_current == 0 &&
          p1.health_status == STATUS_UNCONSCIOUS && !p1.in_combat &&
          gbl.friends_count == 0, "exactly zero is unconscious", detail);

    p1.health_status = STATUS_OKEY;
    p1.in_combat = true;
    p1.hit_point_current = 5;
    gbl.friends_count = 1;
    character_damage(9, &p1);
    snprintf(detail, sizeof(detail), "%s, bleeding %d",
             player_health_status_name(p1.health_status), a1.bleeding);
    check(p1.health_status == STATUS_DYING && a1.bleeding == 4 &&
          p1.hit_point_current == 0 && !p1.in_combat &&
          gbl.friends_count == 0, "past zero is dying and bleeding", detail);

    p1.health_status = STATUS_OKEY;
    p1.in_combat = true;
    p1.hit_point_current = 5;
    character_damage(15, &p1);
    check(p1.health_status == STATUS_DEAD && p1.hit_point_current == 0,
          "ten points past zero kills", "-10 hit points");

    p1.health_status = STATUS_ANIMATED;
    p1.in_combat = true;
    p1.hit_point_current = 10;
    character_damage(10, &p1);
    check(p1.health_status == STATUS_DEAD,
          "an animated corpse stops at zero", "no unconscious for the dead");

    /* Bandaging finds the first of ours still bleeding and stops it. */
    p2.health_status = STATUS_DYING;
    p2.combat_team = TEAM_OURS;
    a2.bleeding = 3;
    p1.health_status = STATUS_DEAD;
    check(character_bandage(false) && a2.bleeding == 3 &&
          p2.health_status == STATUS_DYING,
          "asking who is bleeding changes nothing", "no bandage applied");

    check(character_bandage(true) && p2.health_status == STATUS_UNCONSCIOUS &&
          a2.bleeding == 0, "a bandage stops the bleeding",
          "dying becomes unconscious");

    check(!character_bandage(false), "and then nobody is bleeding",
          "the party is stable");

    /* --- ranges, targets and the missile animation --- */

    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "the combat scene sets up", "out of memory");
        return;
    }

    player_init(&p3);
    action_init(&a3);
    p3.actions = &a3;
    snprintf(p3.name, sizeof(p3.name), "%s", "Orc");
    p3.icon_id = 0x0f;
    p3.field_DE = 1;
    p3.in_combat = true;
    p3.combat_team = TEAM_ENEMY;
    p2.combat_team = TEAM_ENEMY;
    p1.combat_team = TEAM_OURS;
    gbl.player_array[3] = &p3;
    gbl.combatant_count = 3;
    combatmap_setup_player_index();

    combatmap_place_combatant(false, point_make(10, 10), &p1);
    combatmap_place_combatant(false, point_make(14, 10), &p2);
    combatmap_place_combatant(false, point_make(12, 10), &p3);

    snprintf(detail, sizeof(detail), "%d squares away",
             character_target_range(&p2, &p1));
    check(character_target_range(&p2, &p1) == 4,
          "how far away a target is", detail);

    /* The range is measured through walls, because it is a distance and not a
     * path: the wall between them makes no difference to it. */
    ground_tile_map_set(gbl.map_to_background_tile, point_make(11, 10), 1);
    check(character_target_range(&p2, &p1) == 4 &&
          !gbl.map_to_background_tile->ignore_walls,
          "a range ignores walls and puts the flag back", "still 4 squares");
    ground_tile_map_set(gbl.map_to_background_tile, point_make(11, 10), 0x18);

    /* Someone nobody can reach is 0xff away. */
    {
        Player p4;

        player_init(&p4);
        snprintf(p4.name, sizeof(p4.name), "%s", "Ghost");
        check(character_target_range(&p4, &p1) == 0xff,
              "a target off the map has no range", "0xff");
    }

    count = character_build_near_targets(near_targets,
                                        (int)COAB_ARRAY_LEN(near_targets),
                                        0xff, &p1);
    snprintf(detail, sizeof(detail), "%d enemies, nearest %s at %d,%d", count,
             count > 0 ? near_targets[0].player->name : "-",
             count > 0 ? near_targets[0].pos.x : -1,
             count > 0 ? near_targets[0].pos.y : -1);
    check(count == 2 && near_targets[0].player == &p3 &&
          point_eq(near_targets[0].pos, point_make(12, 10)) &&
          near_targets[1].player == &p2,
          "the other side, nearest first", detail);

    /* From the other side of the fight, the one member of ours. */
    count = character_build_near_targets(near_targets,
                                        (int)COAB_ARRAY_LEN(near_targets),
                                        0xff, &p3);
    check(count == 1 && near_targets[0].player == &p1,
          "and only the other side", "one of ours");

    /* A list with no room for the targets truncates rather than overrunning. */
    count = character_build_near_targets(near_targets, 1, 0xff, &p1);
    check(count == 1, "a short target list is truncated", "room for one");

    /* The four missile cells: the sprite, mirrored, the attack pose mirrored,
     * and the attack pose. Icon 0x16 is one of the ones the scene loaded. */
    gbl.missile_dax = dax_block_new(1, 4, 3, 0x18);

    if (gbl.missile_dax == NULL) {
        check(false, "the missile picture is allocated", "out of memory");
    } else {
        const DaxBlock *src = combat_icon_get(&gbl.combat_icons[0x16],
                                              COMBAT_ICON_NORMAL, 0);
        int cell = gbl.missile_dax->bpp;
        bool same = true;
        bool mirrored = true;

        character_load_missile_icons(0x16);

        for (int i = 0; src != NULL && i < cell; i++) {
            int row = i / 24;
            int col = i % 24;

            if (gbl.missile_dax->data[i] != src->data[i]) {
                same = false;
            }
            if (gbl.missile_dax->data[cell + i] !=
                src->data[row * 24 + (23 - col)]) {
                mirrored = false;
            }
        }

        snprintf(detail, sizeof(detail),
                 "%d bytes a cell, icon 0x16 %s loaded", cell,
                 src != NULL ? "is" : "is not");
        check(src != NULL && same && mirrored,
              "the missile cells are the sprite and its mirror image", detail);

        /* And it flies. The target is four squares east, so the flight stays
         * inside the window and the last frame is left on the target. */
        character_redraw_combat_screen();
        character_draw_missile_attack(0, 4, point_make(14, 10),
                                     point_make(10, 10));
        frame_stats(&nonzero, &colors);
        snprintf(detail, sizeof(detail), "%d pixels, %d colours", nonzero,
                 colors);
        check(nonzero > 20000 && colors >= 8, "a missile crosses the arena",
              detail);
        dump(out_dir, "character-missile.ppm");

        /* A spell going off over a character is the same four cells cycled on
         * the spot, with the status line under it. */
        character_magic_attack_display("is blessed", true, &p1);
        frame_stats(&nonzero, &colors);
        snprintf(detail, sizeof(detail), "%d pixels, %d colours", nonzero,
                 colors);
        check(nonzero > 20000 && colors >= 8,
              "a spell bursts over its target", detail);
    }

    /* The combat side panel: name, hit points, armour class and the weapon. */
    p1.hit_point_max = 20;
    p1.hit_point_current = 12;
    p1.in_combat = true;
    p1.health_status = STATUS_OKEY;
    p1.base_ac = 52;                          /* armour class 10 */
    add_readied(&p1, ITEM_LONG_SWORD, 1, 60);
    p1.items[p1.item_count - 1].namenum2 = 0xa2;    /* "+1" */
    p1.items[p1.item_count - 1].namenum3 = 0x24;    /* "Long Sword" */
    character_recalc_values(&p1);
    gbl.display_hitpoints_ac = true;
    character_combat_display_summary(&p1);

    snprintf(detail, sizeof(detail), "%d pixels of ink beside the map",
             cell_ink(1, 0x17, 8, 0x26));
    check(cell_ink(1, 0x17, 8, 0x26) > 200 && !gbl.display_hitpoints_ac,
          "the combat panel names the character and their gear", detail);
    dump(out_dir, "character-combat-panel.ppm");

    /* A status line under it, and then the same area wiped. */
    character_display_status_string(false, 10, "is bandaged", &p1);
    count = cell_ink(10, 0x17, 12, 0x26);
    character_clear_text_area();
    snprintf(detail, sizeof(detail), "%d pixels written, %d left after", count,
             cell_ink(10, 0x17, 12, 0x26));
    check(count > 100 && cell_ink(10, 0x17, 12, 0x26) == 0,
          "a status line is written and wiped", detail);

    combat_scene_teardown();

    /* Out of combat the party list sits down the right-hand side, with the map
     * position and the time under the view. */
    gbl.game_state = GAME_STATE_CAMPING;
    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    gbl.selected_player = &p1;
    p1.in_combat = false;
    p2.in_combat = false;
    p2.hit_point_max = 8;
    p2.hit_point_current = 3;

    gbl.map_pos_x = 7;
    gbl.map_pos_y = 13;
    gbl.map_direction = 2;
    gbl.area_ptr->time_hour = 9;
    gbl.area_ptr->time_minutes_tens = 3;
    gbl.area_ptr->time_minutes_ones = 5;
    gbl.area_ptr->block_area_view = 0;
    gbl.area2_ptr->search_flags = 1;

    /* load_pic does the lot for the state it finds itself in: the border, the
     * camp picture, the party list and the clock. */
    gbl.game_area = 1;
    character_load_pic();

    snprintf(detail, sizeof(detail), "%d pixels of names, %d of position",
             cell_ink(2, 17, 5, 0x26), cell_ink(15, 17, 15, 0x26));
    check(cell_ink(2, 17, 5, 0x26) > 200 && cell_ink(15, 17, 15, 0x26) > 100,
          "the party list and the time are drawn", detail);
    dump(out_dir, "character-party.ppm");

    gbl.team_count      = 0;
    gbl.selected_player = NULL;
    gbl.game_state      = GAME_STATE_DUNGEON_MAP;

    printf("\n");
}

/* ------------------------------------------------- blows, spells and effects */

/* Rolls are counted rather than predicted: the port's generator is not the DOS
 * one, so what can be checked is the shape of the distribution and the two
 * outcomes the rules settle outright - the natural 1 and the natural 20. */
#define EFFECT_ROLLS 400

static void check_effect(const char *out_dir)
{
    Player p1, p2, p3;
    Action a1, a2, a3;
    char detail[240];
    int old_speed = gbl.game_speed_var;
    int nonzero = 0, colors = 0;
    int hits, saves, count;

    printf("blows, spells and their effects\n");

    /* Nothing here is worth watching go by at playing speed. */
    gbl.game_speed_var = 0;
    rnd_seed(0x0aabbccdu);

    /* --- dice --- */
    {
        int lo = 99, hi = 0;
        bool in_range = true;

        for (int i = 0; i < 2000; i++) {
            int roll = effect_roll_dice(6, 3);

            in_range = in_range && roll >= 3 && roll <= 18;
            lo = (roll < lo) ? roll : lo;
            hi = (roll > hi) ? roll : hi;
        }

        snprintf(detail, sizeof(detail), "3d6 covered %d..%d over 2000 rolls",
                 lo, hi);
        check(in_range && lo == 3 && hi == 18,
              "dice count from one and add up", detail);
    }

    /* Three hundred one-sided dice come to three hundred, which does not fit in
     * the byte the original brought the total back in. */
    check(effect_roll_dice(1, 300) == 300 - 256,
          "a damage total over 255 wraps", "300 points come back as 44");

    gbl.dice_count = 0;
    count = effect_roll_dice_save(8, 4);
    snprintf(detail, sizeof(detail), "4d8 rolled %d, gbl.dice_count is %d",
             count, gbl.dice_count);
    check(gbl.dice_count == 4 && count >= 4 && count <= 32,
          "the number of dice is left where the damage code looks for it",
          detail);

    /* --- strength, packed into one byte --- */
    {
        static const int str[5][2]  = { { 0, 18 }, { 50, 18 }, { 100, 18 },
                                       { 0, 16 }, { 0, 19 } };
        static const int want[5]    = { 1, 51, 101, 116, 119 };
        bool ok = true;

        for (int i = 0; i < 5; i++) {
            Affect packed;
            int got_str, got_str_00;
            int encoded = effect_encode_strength(str[i][0], str[i][1]);

            affect_init(&packed, AFFECT_STRENGTH, 10, (u8)encoded, false);
            effect_decode_strength(&got_str_00, &got_str, &packed);

            ok = ok && encoded == want[i] &&
                 got_str == str[i][1] && got_str_00 == str[i][0];
        }

        check(ok, "an exceptional strength packs into one byte and back",
              "18/00 is 101, and 19 is 119");
    }

    {
        int str = 18, str_00 = 99;

        effect_max_strength(&str, 18, &str_00, 100);
        count = (str == 18 && str_00 == 100) ? 1 : 0;

        effect_max_strength(&str, 17, &str_00, 0);
        count += (str == 18 && str_00 == 100) ? 1 : 0;

        effect_max_strength(&str, 19, &str_00, 0);
        count += (str == 19 && str_00 == 0) ? 1 : 0;

        check(count == 3, "the better of two strengths",
              "18/00 beats 18/99, and 19 beats both");
    }

    player_init(&p3);
    set_strength_dex(&p3, 12, 0, 10);
    {
        int encoded = -1;
        bool weaker;

        check(effect_try_encode_strength(&encoded, 0, 15, &p3) &&
              encoded == 115,
              "a strength worth having is packed", "15 over 12");

        weaker = effect_try_encode_strength(&encoded, 0, 12, &p3);
        check(!weaker && encoded == 0, "one that is not is refused",
              "12 is no improvement on 12");

        set_strength_dex(&p3, 18, 0, 10);
        check(effect_try_encode_strength(&encoded, 50, 18, &p3) &&
              encoded == 51,
              "the percentile decides between two eighteens", "18/50 over 18/00");
    }

    /* --- the hit points a constitution is worth --- */
    player_init(&p3);
    check(effect_con_hit_point_bonus(5, SKILL_FIGHTER, 18, &p3) == 20 &&
          effect_con_hit_point_bonus(5, SKILL_FIGHTER, 20, &p3) == 25 &&
          effect_con_hit_point_bonus(5, SKILL_FIGHTER, 25, &p3) == 35 &&
          effect_con_hit_point_bonus(5, SKILL_CLERIC, 18, &p3) == 10 &&
          effect_con_hit_point_bonus(5, SKILL_CLERIC, 15, &p3) == 5 &&
          effect_con_hit_point_bonus(5, SKILL_CLERIC, 14, &p3) == 0,
          "the constitution bonus per level",
          "a fighter earns up to +7 a level, everybody else +2");

    /* A fighter's tenth level and beyond roll no hit die, so they earn no bonus
     * for it either. */
    check(effect_con_hit_point_bonus(20, SKILL_FIGHTER, 18, &p3) == 36 &&
          effect_con_hit_point_bonus(9, SKILL_FIGHTER, 18, &p3) == 36,
          "the bonus stops with the hit dice", "nine dice at +4");

    /* A single-classed ranger has one hit die more than their level. */
    check(effect_con_hit_point_bonus(5, SKILL_RANGER, 18, &p3) == 24,
          "a ranger has a hit die in hand", "six dice at +4");

    /* --- saving throws --- */
    player_init(&p3);
    p3.combat_team = TEAM_OURS;
    p3.save_verse[SAVE_VERSE_POISON] = 21;

    cheats.player_always_saves = true;
    saves = 0;
    for (int i = 0; i < 20; i++) {
        saves += effect_roll_saving_throw(0, SAVE_VERSE_POISON, &p3) ? 1 : 0;
    }
    cheats.player_always_saves = false;
    check(saves == 20 && gbl.saving_throw_roll == 20 && gbl.saving_throw_made,
          "the always-saves cheat rolls a twenty",
          "and a twenty is made whatever the character's save is");

    /* A save of 21 cannot be rolled, so only the natural 20 gets through. */
    saves = 0;
    for (int i = 0; i < EFFECT_ROLLS; i++) {
        saves += effect_roll_saving_throw(0, SAVE_VERSE_POISON, &p3) ? 1 : 0;
    }
    snprintf(detail, sizeof(detail), "%d of %d rolls, about one in twenty",
             saves, EFFECT_ROLLS);
    check(saves > 5 && saves < 45, "a save of 21 is made on a natural 20 only",
          detail);

    /* And a save of 2 is failed only on the natural 1. */
    p3.save_verse[SAVE_VERSE_POISON] = 2;
    saves = 0;
    for (int i = 0; i < EFFECT_ROLLS; i++) {
        saves += effect_roll_saving_throw(0, SAVE_VERSE_POISON, &p3) ? 0 : 1;
    }
    snprintf(detail, sizeof(detail), "%d of %d rolls failed", saves,
             EFFECT_ROLLS);
    check(saves > 5 && saves < 45, "a save of 2 is failed on a natural 1 only",
          detail);

    /* --- landing a blow --- */
    player_init(&p1);
    player_init(&p2);
    action_init(&a1);
    action_init(&a2);
    p1.actions = &a1;
    p2.actions = &a2;

    p1.ac = 100;
    hits = 0;
    for (int i = 0; i < EFFECT_ROLLS; i++) {
        hits += effect_can_hit_target(0, &p1) ? 1 : 0;
    }
    check(hits == 0, "a monster's swing cannot beat armour class 100",
          "the natural 20 counts as 100, and 100 does not beat 100");

    p1.ac = 2;
    hits = 0;
    for (int i = 0; i < EFFECT_ROLLS; i++) {
        hits += effect_can_hit_target(0, &p1) ? 1 : 0;
    }
    snprintf(detail, sizeof(detail), "%d of %d swings landed", hits,
             EFFECT_ROLLS);
    check(hits > 320 && hits < EFFECT_ROLLS,
          "armour class 2 is missed on a 1 and a 2", detail);

    /* A character's swing only has to match the armour class, so the natural 20
     * that became 100 reaches 100 but not 101. */
    p2.combat_team = TEAM_OURS;
    p2.hit_bonus   = 0;
    gbl.area2_ptr->field_6E2 = 0;

    hits = 0;
    for (int i = 0; i < EFFECT_ROLLS; i++) {
        hits += effect_pc_can_hit_target(101, &p1, &p2) ? 1 : 0;
    }
    check(hits == 0, "a character's swing cannot reach armour class 101",
          "this one matches the armour class rather than beating it");

    hits = 0;
    for (int i = 0; i < EFFECT_ROLLS; i++) {
        hits += effect_pc_can_hit_target(100, &p1, &p2) ? 1 : 0;
    }
    snprintf(detail, sizeof(detail), "%d of %d swings landed", hits,
             EFFECT_ROLLS);
    check(hits > 5 && hits < 45, "but it does reach 100", detail);

    {
        int plain = 0, helped = 0;

        for (int i = 0; i < EFFECT_ROLLS; i++) {
            plain += effect_pc_can_hit_target(15, &p1, &p2) ? 1 : 0;
        }

        /* field_6E2 is our side's bonus for the fight; field_6E0 is theirs. */
        gbl.area2_ptr->field_6E2 = 5;
        for (int i = 0; i < EFFECT_ROLLS; i++) {
            helped += effect_pc_can_hit_target(15, &p1, &p2) ? 1 : 0;
        }
        gbl.area2_ptr->field_6E2 = 0;

        snprintf(detail, sizeof(detail), "%d swings landed, and %d with +5",
                 plain, helped);
        check(helped > plain + 40, "the party's own to-hit bonus counts",
              detail);
    }

    /* --- hanging affects on a character and taking them off --- */
    player_init(&p3);
    effect_add_affect(false, 3, 60, AFFECT_BLESS, &p3);
    check(player_has_affect(&p3, AFFECT_BLESS) && p3.affects.count == 1 &&
          p3.affects.items[0].minutes == 60 &&
          p3.affects.items[0].affect_data == 3,
          "an affect is hung on a character", "bless, 60 minutes, data 3");

    /* The C# removed the affect object itself, so a character carrying two of
     * the same keeps the other one. */
    effect_add_affect(false, 7, 60, AFFECT_BLESS, &p3);
    effect_remove_affect(&p3.affects.items[0], AFFECT_BLESS, &p3);
    check(p3.affects.count == 1 && p3.affects.items[0].affect_data == 7,
          "removing one affect leaves its twin", "data 3 went, data 7 stayed");

    check(effect_cure_affect(AFFECT_BLESS, &p3) && p3.affects.count == 0 &&
          !effect_cure_affect(AFFECT_BLESS, &p3),
          "curing an affect the character has, and one they do not", NULL);

    /* Attacking gives away every layer of invisibility at once. */
    effect_add_affect(false, 0, 10, AFFECT_INVISIBILITY, &p3);
    effect_add_affect(false, 0, 10, AFFECT_INVISIBILITY, &p3);
    effect_add_affect(false, 0, 10, AFFECT_BLESS, &p3);
    effect_remove_invisibility(&p3);
    check(!player_has_affect(&p3, AFFECT_INVISIBILITY) &&
          player_has_affect(&p3, AFFECT_BLESS),
          "attacking gives away every layer of invisibility",
          "two copies went, the blessing stayed");

    /* The fight ends and its affects end with it - except the berserk rage,
     * which is kept so that the character can be given back to us. */
    player_init(&p3);
    effect_add_affect(false, 0, 10, AFFECT_BERSERK, &p3);
    effect_add_affect(false, 0, 10, AFFECT_HELPLESS, &p3);
    effect_add_affect(false, 0, 10, AFFECT_BLESS, &p3);
    p3.control_morale = CONTROL_PC_BERZERK;
    p3.combat_team    = TEAM_ENEMY;
    effect_remove_combat_affects(&p3);
    check(!player_has_affect(&p3, AFFECT_HELPLESS) &&
          player_has_affect(&p3, AFFECT_BERSERK) &&
          player_has_affect(&p3, AFFECT_BLESS) &&
          p3.combat_team == TEAM_OURS,
          "the fight's own affects end with the fight",
          "and a berserk character is ours again");

    player_init(&p3);
    effect_add_affect(false, 0, 10, AFFECT_REDUCE, &p3);
    effect_add_affect(false, 0, 10, AFFECT_OWLBEAR_HUG_ROUND_ATTACK, &p3);
    effect_add_affect(false, 0, 10, AFFECT_BLESS, &p3);
    effect_remove_attackers_affects(&p3);
    check(!player_has_affect(&p3, AFFECT_REDUCE) &&
          !player_has_affect(&p3, AFFECT_OWLBEAR_HUG_ROUND_ATTACK) &&
          player_has_affect(&p3, AFFECT_BLESS),
          "and the four an attacker loses with their target", NULL);

    /* --- an attack's affect --- */
    player_init(&p3);
    effect_apply_attack_spell_affect("", true, DAMAGE_ON_SAVE_ZERO, false, 0,
                                     10, AFFECT_SLEEP, &p3);
    check(p3.affects.count == 0,
          "a save against a spell that allows none leaves nothing behind",
          "\"is Unaffected\"");

    effect_apply_attack_spell_affect("falls asleep", false,
                                     DAMAGE_ON_SAVE_NORMAL, false, 5, 10,
                                     AFFECT_SLEEP, &p3);
    check(p3.affects.count == 1 && p3.affects.items[0].minutes == 10 &&
          p3.affects.items[0].affect_data == 5,
          "a failed save leaves the affect", "sleep for 10");

    effect_apply_attack_spell_affect("falls asleep", false,
                                     DAMAGE_ON_SAVE_NORMAL, false, 7, 20,
                                     AFFECT_SLEEP, &p3);
    check(p3.affects.count == 1 && p3.affects.items[0].minutes == 20,
          "a second casting replaces the first rather than stacking",
          "sleep for 20");

    /* --- what a stat change is worth --- */
    player_init(&p3);
    p3.class_level[SKILL_FIGHTER] = 5;
    stat_value_load(&p3.stats.value[PSTAT_CON], 18);
    p3.hit_point_rolled  = 40;
    p3.hit_point_max     = 40;
    p3.hit_point_current = 40;

    effect_calc_stat_bonuses(STAT_CON, &p3);
    snprintf(detail, sizeof(detail), "%d hit points of %d",
             p3.hit_point_current, p3.hit_point_max);
    check(p3.hit_point_max == 60 && p3.hit_point_current == 60 &&
          p3.stats.value[PSTAT_CON].full == 18,
          "a fifth-level fighter's constitution 18 is worth 20 hit points",
          detail);

    /* Twenty and over regenerates, at a rate that runs from a point every six
     * turns up to one a turn. */
    stat_value_load(&p3.stats.value[PSTAT_CON], 20);
    effect_calc_stat_bonuses(STAT_CON, &p3);
    {
        const Affect *regen = affect_list_find_const(&p3.affects,
                                                     AFFECT_HIGH_CON_REGEN);

        snprintf(detail, sizeof(detail), "%d hit points, regenerating every %d",
                 p3.hit_point_max, regen ? regen->minutes : -1);
        check(p3.hit_point_max == 65 && p3.hit_point_current == 65 &&
              regen != NULL && regen->minutes == 60,
              "constitution 20 adds five more and starts regenerating", detail);
    }

    /* Losing it again costs the hit points it bought and stops the healing. */
    stat_value_load(&p3.stats.value[PSTAT_CON], 10);
    effect_calc_stat_bonuses(STAT_CON, &p3);
    check(p3.hit_point_max == 40 && p3.hit_point_current == 40 &&
          !player_has_affect(&p3, AFFECT_HIGH_CON_REGEN),
          "and drained constitution takes them away again", NULL);

    /* A monster has no class levels at all, and the C# divided by their number
     * without looking. */
    player_init(&p3);
    stat_value_load(&p3.stats.value[PSTAT_CON], 18);
    p3.hit_point_rolled  = 12;
    p3.hit_point_max     = 12;
    p3.hit_point_current = 12;
    effect_calc_stat_bonuses(STAT_CON, &p3);
    check(p3.hit_point_max == 12 && p3.hit_point_current == 12,
          "a monster with no class levels keeps its hit points",
          "the C# divided by zero here");

    /* A girdle of giant strength says which strength it grants in its second
     * affect byte: 0 is 18/00 and 6 is 24. */
    player_init(&p3);
    set_strength_dex(&p3, 10, 0, 10);
    {
        Item girdle;

        item_init(&girdle, ITEM_GIRDLE, 0, 0, 0, 0, 0, true, 0, false, 30, 1, 0,
                  AFFECT_NONE, (Affects)6, AFFECT_85);
        player_item_add(&p3, &girdle);

        effect_calc_stat_bonuses(STAT_STR, &p3);
        snprintf(detail, sizeof(detail), "strength %d/%02d",
                 p3.stats.value[PSTAT_STR].full,
                 p3.stats.value[PSTAT_STR00].cur);
        check(p3.stats.value[PSTAT_STR].full == 24,
              "a girdle of giant strength is worth 24", detail);

        p3.items[0].affect_2 = 0;
        effect_calc_stat_bonuses(STAT_STR, &p3);
        check(p3.stats.value[PSTAT_STR].full == 18 &&
              p3.stats.value[PSTAT_STR00].cur == 100,
              "and the weakest of them 18/00", NULL);
    }

    /* The strength spell: what it pushes past 18 goes into the percentile, but
     * only for someone who fights for a living. */
    player_init(&p3);
    set_strength_dex(&p3, 12, 0, 10);
    p3.class_level[SKILL_FIGHTER] = 5;
    effect_add_affect(false, effect_encode_strength(0, 17), 60, AFFECT_STRENGTH,
                      &p3);
    effect_calc_stat_bonuses(STAT_STR, &p3);
    check(p3.stats.value[PSTAT_STR].full == 18 &&
          p3.stats.value[PSTAT_STR00].cur == 100,
          "a fighter under a strength spell reaches 18/00", "12 plus 17");

    player_init(&p3);
    set_strength_dex(&p3, 12, 0, 10);
    p3.class_level[SKILL_MAGIC_USER] = 5;
    effect_add_affect(false, effect_encode_strength(0, 17), 60, AFFECT_STRENGTH,
                      &p3);
    effect_calc_stat_bonuses(STAT_STR, &p3);
    check(p3.stats.value[PSTAT_STR].full == 18 &&
          p3.stats.value[PSTAT_STR00].cur == 0,
          "a magic user under the same spell stops at 18", "no percentile");

    /* Feeblemind is charged at 3, and the friends spell sets charisma outright. */
    player_init(&p3);
    stat_value_load(&p3.stats.value[PSTAT_INT], 17);
    stat_value_load(&p3.stats.value[PSTAT_WIS], 16);
    stat_value_load(&p3.stats.value[PSTAT_CHA], 9);
    effect_add_affect(false, 0, 60, AFFECT_FEEBLEMIND, &p3);
    effect_add_affect(false, 18, 60, AFFECT_FRIENDS, &p3);
    effect_calc_stat_bonuses(STAT_INT, &p3);
    effect_calc_stat_bonuses(STAT_WIS, &p3);
    effect_calc_stat_bonuses(STAT_CHA, &p3);
    check(p3.stats.value[PSTAT_INT].full == 3 &&
          p3.stats.value[PSTAT_WIS].full == 3 &&
          p3.stats.value[PSTAT_CHA].full == 18,
          "feeblemind leaves an intelligence of 3, and friends a charisma of 18",
          NULL);

    /* --- healing --- */
    player_init(&p3);
    p3.health_status     = STATUS_DYING;
    p3.hit_point_max     = 20;
    p3.hit_point_current = 0;

    /* Healing a dying character out of combat brings them round, and then
     * heal_player hands affect 0x4e to the affect table - whether or not the
     * character carries it - which stands them back up on the spot if there is a
     * square for them to stand in. */
    {
        bool healed = effect_heal_player(1, 5, &p3);

        snprintf(detail, sizeof(detail), "%d of %d, and back on their feet",
                 p3.hit_point_current, p3.hit_point_max);
        check(healed && p3.hit_point_current == 5 &&
              p3.health_status == STATUS_OKEY && p3.in_combat,
              "healing a dying character brings them round and stands them up",
              detail);
    }

    check(effect_heal_player(1, 100, &p3) && p3.hit_point_current == 20,
          "healing stops at the maximum", "20 of 20");

    p3.health_status = STATUS_OKEY;
    check(!effect_heal_player(1, 5, &p3) && effect_heal_player(0, 5, &p3),
          "a healthy character is not a target unless the caller insists",
          "which is what \"is fully healed\" reports");

    p3.health_status = STATUS_DEAD;
    check(!effect_heal_player(0, 5, &p3), "and the dead are not healed", NULL);

    /* --- a spell lost --- */
    player_init(&p3);
    action_init(&a3);
    p3.actions = &a3;
    a3.can_cast = true;
    a3.spell_id = 12;
    effect_try_loose_spell(&p3);
    check(!a3.can_cast && a3.spell_id == 0,
          "a caster who is hit loses the spell they were holding", NULL);

    /* --- damage, death and the map --- */
    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "the combat scene sets up", "out of memory");
        gbl.game_speed_var = old_speed;
        printf("\n");
        return;
    }

    combatmap_place_combatant(false, point_make(10, 10), &p1);
    combatmap_place_combatant(false, point_make(11, 10), &p2);
    combatmap_setup_player_index();

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl.selected_player = &p1;

    p1.combat_team       = TEAM_OURS;
    p1.hit_point_max     = 12;
    p1.hit_point_current = 12;
    p2.combat_team       = TEAM_ENEMY;
    p2.hit_point_max     = 10;
    p2.hit_point_current = 10;

    gbl.damage_flags = DAMAGE_FIRE;
    effect_damage_person(true, DAMAGE_ON_SAVE_HALF, 8, &p1);
    snprintf(detail, sizeof(detail), "%d hit points left of 12",
             p1.hit_point_current);
    check(p1.hit_point_current == 8 && p1.in_combat,
          "a save halves the damage", detail);

    /* The blow announces itself over the character and then clears the message
     * again, so what is left on the screen is the fight as it stands: the
     * arena, the two combatants and the panel of armour classes and hit
     * points. */
    frame_stats(&nonzero, &colors);
    dump(out_dir, "effect-damage.ppm");
    snprintf(detail, sizeof(detail), "%d px, %d colours -> effect-damage.ppm",
             nonzero, colors);
    check(nonzero > 1000 && colors > 3,
          "and the fight is still on the screen afterwards", detail);

    effect_damage_person(true, DAMAGE_ON_SAVE_ZERO, 8, &p1);
    check(p1.hit_point_current == 8,
          "a save against one that allows none costs nothing",
          "zero damage is not announced either");

    /* Taken out of the fight without being killed: the body comes off the map
     * but the hit points stay. */
    effect_remove_from_combat("runs away", STATUS_RUNNING, &p2);
    count = combatmap_player_index_at(10, 11);
    snprintf(detail, sizeof(detail),
             "%d hit points left, %d squares on the map, and square 11,10 "
             "holds combatant %d",
             p2.hit_point_current, gbl.combat_map[2].size, count);
    check(!p2.in_combat && p2.health_status == STATUS_RUNNING &&
          p2.hit_point_current == 10 && gbl.combat_map[2].size == 0 &&
          count == 0,
          "a combatant who runs away leaves the map with their hit points",
          detail);

    /* And put back on their feet where they fell. */
    check(effect_combat_heal(4, &p2) && p2.in_combat &&
          p2.health_status == STATUS_OKEY && p2.hit_point_current == 4,
          "a fallen combatant can be stood back up", "with 4 hit points");

    /* Ten points past zero kills outright. */
    gbl.damage_flags = 0;
    effect_damage_person(false, DAMAGE_ON_SAVE_NORMAL, 30, &p1);
    snprintf(detail, sizeof(detail), "%s with %d hit points",
             player_health_status_name(p1.health_status), p1.hit_point_current);
    check(!p1.in_combat && p1.hit_point_current == 0 &&
          p1.health_status == STATUS_DEAD,
          "a blow ten points past zero kills", detail);

    p2.health_status     = STATUS_OKEY;
    p2.hit_point_current = 4;
    effect_kill_player("is killed", STATUS_DEAD, &p2);
    check(p2.health_status == STATUS_DEAD && !p2.in_combat &&
          p2.hit_point_current == 0, "and so does a killing spell", NULL);

    p2.hit_point_current = 5;
    effect_kill_player("is killed", STATUS_GONE, &p2);
    check(p2.health_status == STATUS_DEAD && p2.hit_point_current == 5,
          "the dead are killed only once", "the second blow does nothing");

    /* --- the clouds a fight leaves lying about --- */
    player_init(&p3);
    action_init(&a3);
    p3.actions = &a3;
    p3.icon_id = 0x0f;
    p3.field_DE = 1;
    p3.in_combat = true;
    p3.combat_team = TEAM_OURS;
    p3.hit_point_max = 30;
    p3.hit_point_current = 30;
    p3.hit_dice = 3;
    snprintf(p3.name, sizeof(p3.name), "%s", "Dwarf");
    gbl.player_array[3] = &p3;
    gbl.combatant_count = 3;
    combatmap_place_combatant(false, point_make(13, 10), &p3);
    combatmap_setup_player_index();

    gbl.team_count = 0;
    gbl_team_add(&p3);
    gbl.selected_player = &p3;

    /* Cloudkill: up to four hit dice is fatal without a save. */
    ground_tile_map_set(gbl.map_to_background_tile, point_make(13, 10),
                        TILE_CLOUD_KILL);
    effect_in_poison_cloud(0, &p3);
    check(p3.health_status == STATUS_DEAD && !p3.in_combat &&
          player_has_affect(&p3, AFFECT_MINOR_GLOBE_OF_INVULN),
          "a cloudkill is fatal to four hit dice or fewer",
          "and the affect it leaves behind is the original's wrong one");

    /* Seven and up walk through it. */
    p3.hit_dice          = 9;
    p3.health_status     = STATUS_OKEY;
    p3.in_combat         = true;
    p3.hit_point_current = 30;
    affect_list_clear(&p3.affects);
    check(combatmap_place_combatant(false, point_make(13, 10), &p3),
          "and the body can be stood back up where it fell", "square 13,10");
    combatmap_setup_player_index();
    effect_in_poison_cloud(0, &p3);
    check(p3.health_status == STATUS_OKEY && p3.in_combat &&
          p3.affects.count == 0,
          "and no trouble at all to nine", NULL);

    /* A stinking cloud only makes the strong cough. */
    ground_tile_map_set(gbl.map_to_background_tile, point_make(13, 10),
                        TILE_STINKING_CLOUD);
    p3.save_verse[SAVE_VERSE_POISON] = 1;
    cheats.player_always_saves = true;
    effect_in_poison_cloud(1, &p3);
    cheats.player_always_saves = false;
    check(player_has_affect(&p3, AFFECT_STINKING_CLOUD) &&
          !player_has_affect(&p3, AFFECT_HELPLESS) && p3.in_combat,
          "a save against a stinking cloud is only a cough", NULL);

    /* Fail it and the character is helpless for a round or four. The save is
     * 21, so this takes at most a handful of tries. */
    p3.save_verse[SAVE_VERSE_POISON] = 21;
    for (int i = 0; i < 50 && !player_has_affect(&p3, AFFECT_HELPLESS); i++) {
        affect_list_remove_type(&p3.affects, AFFECT_STINKING_CLOUD);
        effect_in_poison_cloud(1, &p3);
    }
    {
        const Affect *helpless = affect_list_find_const(&p3.affects,
                                                        AFFECT_HELPLESS);

        snprintf(detail, sizeof(detail), "helpless for %d",
                 helpless ? helpless->minutes : -1);
        check(helpless != NULL && helpless->minutes >= 2 &&
              helpless->minutes <= 5,
              "and a failed one is nausea and helplessness", detail);
    }

    /* A character who is already helpless is not made helpless again, which is
     * what stops the cloud holding them there for ever. */
    count = p3.affects.count;
    effect_in_poison_cloud(1, &p3);
    check(p3.affects.count == count,
          "one already helpless is left alone", "the cloud cannot pile on");

    gbl.player_array[3]  = NULL;
    gbl.combatant_count  = 2;
    combat_scene_teardown();
    gbl.team_count       = 0;
    gbl.selected_player  = NULL;
    gbl.game_speed_var   = old_speed;

    printf("\n");
}

/* ------------------------------------------------------- what an affect does */

/* Clears everything the affect handlers read and write between them, so one
 * check cannot leave a roll or a damage total behind for the next. */
static void affect_scratch_clear(void)
{
    gbl.attack_roll       = 0;
    gbl.saving_throw_roll = 0;
    gbl.saving_throw_made = false;
    gbl.save_verse_type   = SAVE_VERSE_SPELL;
    gbl.dice_count        = 0;
    gbl.damage            = 0;
    gbl.damage_flags      = 0;
    gbl.current_affect    = 0;
    gbl.spell_id          = 0;
    gbl.spell_target      = NULL;
    gbl.target_invisible  = false;
    gbl.cure_spell        = false;
    gbl.monster_morale    = 0;
    gbl.half_actions_left = 0;
    gbl.apply_item_affect = false;
}

/* Hands one affect to the table with an Affect record of its own, which is what
 * most of the handlers expect as their parameter. */
static void affect_apply(Effect add_remove, Affects type, u8 data, Player *p)
{
    Affect record;

    affect_init(&record, type, 10, data, false);
    affect_table_call(add_remove, &record, p, type);
}

static void check_affects(void)
{
    Player p1, p2;
    Action a1, a2;
    char detail[240];
    int old_speed = gbl.game_speed_var;
    int count;

    printf("what an affect does\n");

    gbl.game_speed_var = 0;
    rnd_seed(0x13a0c5efu);

    player_init(&p1);
    player_init(&p2);
    action_init(&a1);
    action_init(&a2);
    p1.actions = &a1;
    p2.actions = &a2;
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    snprintf(p2.name, sizeof(p2.name), "%s", "Kobold");

    /* --- the two that only move a roll --- */
    affect_scratch_clear();
    affect_apply(EFFECT_ADD, AFFECT_BLESS, 0, &p1);
    check(gbl.monster_morale == 5 && gbl.attack_roll == 1,
          "a blessing is five morale and a point to hit", NULL);

    affect_scratch_clear();
    gbl.monster_morale = 20;
    affect_apply(EFFECT_ADD, AFFECT_CURSED, 0, &p1);
    count = (gbl.monster_morale == 15 && gbl.attack_roll == -1) ? 1 : 0;

    gbl.monster_morale = 3;
    affect_apply(EFFECT_ADD, AFFECT_CURSED, 0, &p1);
    count += (gbl.monster_morale == 0) ? 1 : 0;

    check(count == 2, "and a curse the same the other way",
          "morale stops at nothing rather than wrapping");

    /* --- armour class, which counts the other way round --- */
    affect_scratch_clear();
    p1.ac = p1.ac_behind = 0x30;
    affect_apply(EFFECT_ADD, AFFECT_FAERIE_FIRE, 0, &p1);
    snprintf(detail, sizeof(detail), "AC %d became AC %d",
             0x3c - 0x30, player_display_ac(&p1));
    check(p1.ac == 0x32 && p1.ac_behind == 0x32,
          "faerie fire moves the armour class two points", detail);

    p1.ac = p1.ac_behind = 0x3b;
    affect_apply(EFFECT_ADD, AFFECT_FAERIE_FIRE, 0, &p1);
    check(p1.ac == 0x3c && p1.ac_behind == 0x3c && player_display_ac(&p1) == 0,
          "and stops at the end of the scale", "0x3c is AC 0");

    affect_scratch_clear();
    p1.ac = 0x30;                                       /* AC 12 */
    gbl.spell_id = SPELL_MAGIC_MISSILE;
    gbl.damage = 9;
    affect_apply(EFFECT_ADD, AFFECT_SHIELD, 0, &p1);
    check(p1.ac == 0x39 && gbl.saving_throw_roll == 1 && gbl.damage == 0,
          "a shield is AC 3, a point on every save, and no magic missiles",
          NULL);

    affect_scratch_clear();
    p1.ac = 0x3a;                                       /* already better */
    gbl.spell_id = SPELL_MAGIC_MISSILE;
    gbl.damage = 9;
    gbl.spell_id = 0;
    affect_apply(EFFECT_ADD, AFFECT_SHIELD, 0, &p1);
    check(p1.ac == 0x3a && gbl.damage == 9,
          "and it does not make a better armour class worse", NULL);

    /* --- the elements --- */
    affect_scratch_clear();
    gbl.damage_flags = DAMAGE_COLD;
    gbl.damage = 21;
    affect_apply(EFFECT_ADD, AFFECT_RESIST_COLD, 0, &p1);
    count = (gbl.damage == 10 && gbl.saving_throw_roll == 3) ? 1 : 0;

    affect_scratch_clear();
    gbl.damage_flags = DAMAGE_FIRE;
    gbl.damage = 21;
    affect_apply(EFFECT_ADD, AFFECT_RESIST_COLD, 0, &p1);
    count += (gbl.damage == 21 && gbl.saving_throw_roll == 0) ? 1 : 0;

    check(count == 2, "resist cold halves cold and ignores fire",
          "and the halving rounds down");

    /* Two points a die off fire damage, never below a point a die, and plain
     * fire is shrugged off altogether. */
    affect_scratch_clear();
    gbl.damage_flags = DAMAGE_FIRE | DAMAGE_MAGIC;
    gbl.dice_count = 4;
    gbl.damage = 24;
    gbl.current_affect = AFFECT_BLINDED;
    affect_apply(EFFECT_ADD, AFFECT_FIRE_RESIST, 0, &p1);
    snprintf(detail, sizeof(detail), "4d fire of 24 came to %d", gbl.damage);
    check(gbl.damage == 16 && gbl.saving_throw_roll == 4 &&
          gbl.current_affect == AFFECT_BLINDED,
          "fire resistance takes two off each die", detail);

    affect_scratch_clear();
    gbl.damage_flags = DAMAGE_FIRE | DAMAGE_MAGIC;
    gbl.dice_count = 4;
    gbl.damage = 6;
    affect_apply(EFFECT_ADD, AFFECT_FIRE_RESIST, 0, &p1);
    check(gbl.damage == 4, "and never below a point a die", "4d8 of 6 stays 4");

    affect_scratch_clear();
    gbl.damage_flags = DAMAGE_FIRE;
    gbl.dice_count = 2;
    gbl.damage = 8;
    gbl.current_affect = AFFECT_BLINDED;
    affect_apply(EFFECT_ADD, AFFECT_FIRE_RESIST, 0, &p1);
    check(gbl.damage == 0 && gbl.current_affect == 0,
          "and fire that is not magical does nothing at all",
          "the affect it carried is dropped with it");

    /* --- alignment --- */
    affect_scratch_clear();
    gbl.selected_player = &p2;
    p2.alignment = 5;                                   /* neutral evil */
    affect_apply(EFFECT_ADD, AFFECT_PROTECTION_FROM_EVIL, 0, &p1);
    count = (gbl.saving_throw_roll == 2 && gbl.attack_roll == -2) ? 1 : 0;

    affect_scratch_clear();
    p2.alignment = 3;                                   /* neutral good */
    affect_apply(EFFECT_ADD, AFFECT_PROTECTION_FROM_EVIL, 0, &p1);
    count += (gbl.saving_throw_roll == 0 && gbl.attack_roll == 0) ? 1 : 0;

    affect_scratch_clear();
    affect_apply(EFFECT_ADD, AFFECT_PROT_FROM_GOOD_10_RADIUS, 0, &p1);
    count += (gbl.saving_throw_roll == 2 && gbl.attack_roll == -2) ? 1 : 0;

    check(count == 3, "protection from evil reads the attacker's alignment",
          "and the 10' radius version is the same handler");

    /* --- the constitution ladder on saves --- */
    {
        static const int con[7]  = { 3, 5, 8, 12, 15, 19, 21 };
        static const int want[7] = { 0, 1, 2,  3,  4,  5,  0 };
        bool ok = true;

        for (int i = 0; i < 7; i++) {
            affect_scratch_clear();
            gbl.save_verse_type = SAVE_VERSE_ROD_STAFF_WAND;
            p1.stats.value[PSTAT_CON].full = (u8)con[i];
            affect_apply(EFFECT_ADD, AFFECT_CON_SAVING_BONUS, 0, &p1);

            ok = ok && gbl.saving_throw_roll == want[i];
        }

        affect_scratch_clear();
        gbl.save_verse_type = SAVE_VERSE_BREATH_WEAPON;
        p1.stats.value[PSTAT_CON].full = 19;
        affect_apply(EFFECT_ADD, AFFECT_CON_SAVING_BONUS, 0, &p1);

        check(ok && gbl.saving_throw_roll == 0,
              "a constitution is worth up to five points of saving throw",
              "against spells and wands only, and nothing over 20");
    }

    /* --- the ones that keep their state in the affect's data byte --- */
    affect_scratch_clear();
    p1.hit_point_current = 12;
    p1.health_status = STATUS_OKEY;
    p1.in_combat = false;
    affect_apply(EFFECT_ADD, AFFECT_REDUCE, 0, &p1);
    check(p1.health_status == STATUS_DEAD,
          "the sixth round of a reduce spell suffocates", "data byte 0 is fatal");

    {
        Affect shrink;

        affect_init(&shrink, AFFECT_REDUCE, 10, 3, false);
        p1.health_status = STATUS_OKEY;
        p1.hit_point_current = 12;
        affect_table_call(EFFECT_ADD, &shrink, &p1, AFFECT_REDUCE);
        check(shrink.affect_data == 2 && p1.health_status == STATUS_OKEY,
              "and the rounds before it only count down", NULL);
    }

    affect_scratch_clear();
    p1.combat_team = TEAM_OURS;
    affect_apply(EFFECT_ADD, AFFECT_PRAYER, 0x10, &p1);
    count = (gbl.attack_roll == -1 && gbl.saving_throw_roll == -1) ? 1 : 0;

    affect_scratch_clear();
    affect_apply(EFFECT_ADD, AFFECT_PRAYER, 0x00, &p1);
    count += (gbl.attack_roll == 1 && gbl.saving_throw_roll == 1) ? 1 : 0;

    check(count == 2, "a prayer helps the side that cast it and hinders the other",
          "the side is bit 4 of the affect");

    /* Strength goes one point an hour until there is nothing left to take. */
    affect_scratch_clear();
    p1.stats.value[PSTAT_STR].full = 5;
    affect_list_clear(&p1.affects);
    affect_apply(EFFECT_ADD, AFFECT_WEAKEN, 0, &p1);
    check(p1.stats.value[PSTAT_STR].full == 4 &&
          player_has_affect(&p1, AFFECT_WEAKEN),
          "a weakening disease costs a point of strength and renews itself",
          NULL);

    affect_list_clear(&p1.affects);
    p1.stats.value[PSTAT_STR].full = 3;
    affect_apply(EFFECT_ADD, AFFECT_WEAKEN, 0, &p1);
    check(p1.stats.value[PSTAT_STR].full == 3 &&
          !player_has_affect(&p1, AFFECT_HELPLESS),
          "and a strength of three cannot fall further",
          "nor does it make the character helpless, as the original had it");

    /* A cure stops it renewing, which is how the cure gets rid of it. */
    affect_list_clear(&p1.affects);
    p1.stats.value[PSTAT_STR].full = 12;
    gbl.cure_spell = true;
    affect_apply(EFFECT_ADD, AFFECT_WEAKEN, 0, &p1);
    gbl.cure_spell = false;
    check(p1.stats.value[PSTAT_STR].full == 12 && p1.affects.count == 0,
          "a cure leaves it to run out instead", NULL);

    /* --- an image lost, and one lost only once to the same blow --- */
    {
        Affect images;
        int lost = 0;

        affect_scratch_clear();
        gbl.spell_id = SPELL_MAGIC_MISSILE;
        affect_init(&images, AFFECT_MIRROR_IMAGE, 10, 0x40, false);
        affect_list_clear(&p1.affects);

        for (int i = 0; i < 40 && images.affect_data > 0x38; i++) {
            gbl.damage = 6;
            gbl.current_affect = AFFECT_BLINDED;
            affect_table_call(EFFECT_ADD, &images, &p1, AFFECT_MIRROR_IMAGE);

            if (gbl.damage == 0 && gbl.current_affect == 0) {
                lost++;
            }
        }

        snprintf(detail, sizeof(detail), "%d blows went into images", lost);
        check(lost > 0 && images.affect_data == (u8)(0x40 - lost),
              "a mirror image takes the blow instead of its owner", detail);

        gbl.byte_1D2C7 = true;
        gbl.damage = 6;
        affect_init(&images, AFFECT_MIRROR_IMAGE, 10, 0x40, false);
        affect_table_call(EFFECT_ADD, &images, &p1, AFFECT_MIRROR_IMAGE);
        gbl.byte_1D2C7 = false;
        check(gbl.damage == 6 && images.affect_data == 0x40,
              "and no image is lost twice to the same blow", NULL);
    }

    /* --- the spiritual hammer, which conjures its own weapon --- */
    affect_scratch_clear();
    player_init(&p1);
    p1.actions = &a1;
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    gbl.selected_player = &p1;
    affect_apply(EFFECT_ADD, AFFECT_SPIRITUAL_HAMMER, 0, &p1);
    {
        Item *hammer = (p1.item_count > 0) ? player_item_at(&p1, 0) : NULL;

        snprintf(detail, sizeof(detail), "%d items, type %d, +%d",
                 p1.item_count, hammer ? hammer->type : -1,
                 hammer ? hammer->plus : 0);
        check(p1.item_count == 1 && hammer != NULL &&
              hammer->type == ITEM_HAMMER && hammer->namenum3 == 0xf3 &&
              hammer->plus == 1 && hammer->readied,
              "a spiritual hammer is conjured into the pack and readied", detail);
    }

    /* Cast again while it is up and there is still only the one hammer. */
    affect_apply(EFFECT_ADD, AFFECT_SPIRITUAL_HAMMER, 0, &p1);
    check(p1.item_count == 1, "and casting it twice does not make two", NULL);

    affect_apply(EFFECT_REMOVE, AFFECT_SPIRITUAL_HAMMER, 0, &p1);
    check(p1.item_count == 0, "and it goes when the spell does", NULL);

    /* --- an item's own affect, which every id is routed to --- */
    {
        Item ring;

        affect_scratch_clear();
        affect_list_clear(&p1.affects);
        item_init(&ring, ITEM_RING_OF_PROT, 0, 0, 0, 0, 0, true, 0, false, 0, 1,
                  0, AFFECT_NONE, AFFECT_SHIELD, AFFECT_NONE);

        gbl.apply_item_affect = true;
        affect_table_call(EFFECT_ADD, &ring, &p1, AFFECT_BLESS);

        check(player_has_affect(&p1, AFFECT_SHIELD) &&
              !player_has_affect(&p1, AFFECT_BLESS) && !gbl.apply_item_affect,
              "a readied item's affect reaches its wearer whatever it is handed",
              "gbl.apply_item_affect sends every id to the item handler");

        gbl.apply_item_affect = true;
        affect_table_call(EFFECT_REMOVE, &ring, &p1, AFFECT_BLESS);
        check(!player_has_affect(&p1, AFFECT_SHIELD),
              "and goes again when the item is put away", NULL);
    }

    /* --- an affect nothing is registered for, and one whose handler has not
     * been translated yet: neither may touch the damage in hand --- */
    affect_scratch_clear();
    gbl.damage = 7;
    gbl.current_affect = AFFECT_BLINDED;
    affect_table_call(EFFECT_ADD, NULL, &p1, AFFECT_NONE);
    affect_table_call(EFFECT_ADD, NULL, &p1, AFFECT_39);
    affect_table_call(EFFECT_ADD, NULL, &p1, AFFECT_DETECT_MAGIC);
    check(gbl.damage == 7 && gbl.current_affect == AFFECT_BLINDED,
          "an affect with nothing to do leaves the blow alone",
          "no affect, an untranslated one, and one that really is empty");

    gbl.selected_player = NULL;
    gbl.game_speed_var  = old_speed;
    affect_scratch_clear();

    printf("\n");
}

/* ----------------------------------------------------- attacks and targeting */

/* Puts a combatant on a square without asking the placement code, which is all
 * the direction and range arithmetic reads. */
static void combatant_move_to(Player *player, Point pos)
{
    gbl.combat_map[combatmap_player_index(player)].pos = pos;
    combatmap_setup_player_index();
}

static void held_set(Player *player, bool held)
{
    affect_list_clear(&player->affects);

    if (held) {
        Affect record;

        affect_init(&record, AFFECT_HELPLESS, 10, 0, false);
        affect_list_add(&player->affects, &record);
    }
}

static void check_attack(void)
{
    Player p1, p2, p3;
    Action a1, a2, a3;
    SortedCombatant sorted[GBL_MAX_COMBATANT_COUNT];
    char detail[240];
    int old_speed = gbl.game_speed_var;
    int count;
    bool ok;

    printf("attacks and targeting\n");

    gbl.game_speed_var = 0;
    rnd_seed(0x51c39a7bu);

    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "the combat scene sets up", "out of memory");
        return;
    }

    p1.combat_team = TEAM_OURS;
    p2.combat_team = TEAM_ENEMY;
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(28, 12), &p2);

    /* --- halves, which is how a round and a half of attacks is written down --- */
    {
        int even_round, odd_round;

        gbl.combat_round = 2;
        even_round = attack_this_round_action_count(3);
        gbl.combat_round = 3;
        odd_round = attack_this_round_action_count(3);
        gbl.combat_round = 0;

        snprintf(detail, sizeof(detail), "three halves: %d then %d",
                 even_round, odd_round);
        check(even_round == 1 && odd_round == 2 &&
              attack_this_round_action_count(4) == 2,
              "three attacks every two rounds", detail);
    }

    /* --- movement, counted in half-steps --- */
    p1.movement = 12;
    check(attack_calc_moves(&p1) == 24, "movement comes back in half-steps",
          "12 squares is 24 halves");

    p1.movement = 0;
    check(attack_calc_moves(&p1) == 2,
          "and nobody is left with no movement at all", "0 becomes 1 square");

    /* Out of a fight the area's own bonus is added on. */
    p1.movement   = 6;
    p1.in_combat  = false;
    gbl.area2_ptr->field_6E4 = 3;
    count = attack_calc_moves(&p1);
    gbl.area2_ptr->field_6E4 = 0;
    p1.in_combat = true;
    snprintf(detail, sizeof(detail), "%d halves", count);
    check(count == 18, "out of a fight the area adds its own", detail);

    /* --- the top of a turn --- */
    p1.movement          = 6;
    p1.base_half_moves   = 6;
    p1.attacks_count     = 3;
    p1.attack_level      = 4;
    gbl.area2_ptr->field_596 = 0;
    a1.field_8 = false;
    attack_calculate_initiative(&p1);
    snprintf(detail, sizeof(detail),
             "delay %d, %d moves, %d attacks, sweep limit %d",
             a1.delay, a1.move, p1.attack1_attacks_left, a1.max_sweap_targets);
    check(a1.attack_idx == 2 && a1.can_cast && a1.can_use && !a1.field_8 &&
          a1.spell_id == 0 && a1.delay >= 1 && a1.delay <= 20 &&
          a1.move == 12 && a1.max_sweap_targets == 4 &&
          p1.attack2_attacks_left == 3,
          "initiative rolls a delay and hands out the round", detail);

    /* A side the encounter surprised has six taken off its delay, and a d6 never
     * survives that: the surprised always go last. */
    gbl.area2_ptr->field_596 = TEAM_OURS + 1;
    attack_calculate_initiative(&p1);
    gbl.area2_ptr->field_596 = 0;
    snprintf(detail, sizeof(detail), "delay %d", a1.delay);
    check(a1.delay == 0, "and the surprised side loses six of it", detail);

    /* --- which way a target lies --- */
    {
        bool all_eight = true;

        combatant_move_to(&p1, point_make(25, 12));

        for (int dir = 0; dir < 8; dir++) {
            Point delta = gbl_map_direction_delta(dir);

            combatant_move_to(&p2, point_make(25 + delta.x * 3,
                                              12 + delta.y * 3));

            if (attack_target_direction(&p2, &p1) != (u8)dir) {
                all_eight = false;
                snprintf(detail, sizeof(detail),
                         "direction %d came back as %d", dir,
                         attack_target_direction(&p2, &p1));
            }
        }

        if (all_eight) {
            snprintf(detail, sizeof(detail), "all eight sectors");
        }
        check(all_eight, "the eight compass sectors", detail);
    }

    /* The gradients cut true eighths, so a shallow diagonal is a straight
     * direction: five east and one north of here is still east. */
    combatant_move_to(&p2, point_make(30, 11));
    check(attack_target_direction(&p2, &p1) == 2,
          "a shallow diagonal is a straight direction", "5 east, 1 north is east");

    /* Three east and one north is 18 degrees and ought to be east too, but the
     * gradient is worked out in whole squares: 0x6a * 3 / 0x100 truncates 1.24
     * down to 1, which the north east sector claims because it is tested first
     * and takes the boundary. The original does the same. */
    combatant_move_to(&p2, point_make(28, 11));
    check(attack_target_direction(&p2, &p1) == 1,
          "and a 3-to-1 slope falls to the diagonal", "18 degrees reads as north east");

    /* Two on the same square read as north, which is the first sector tested. */
    combatant_move_to(&p2, point_make(25, 12));
    check(attack_target_direction(&p2, &p1) == 0,
          "and the same square reads as north", "the first sector tested");

    /* --- being spun round --- */
    a2.direction         = 0;              /* facing north */
    a2.attacks_received  = 0;
    a2.direction_changes = 0;
    combatant_move_to(&p2, point_make(25, 12));
    combatant_move_to(&p1, point_make(25, 15));     /* attacking from the south */
    attack_recalc_attacks_received(&p2, &p1);
    snprintf(detail, sizeof(detail), "%d blows, turned %d eighths",
             a2.attacks_received, a2.direction_changes);
    check(a2.attacks_received == 1 && a2.direction_changes == 4,
          "a blow from behind spins the target half round", detail);

    /* The turn counts the short way round: north west of a target facing north is
     * one eighth, not seven. */
    a2.direction_changes = 0;
    combatant_move_to(&p1, point_make(22, 9));
    attack_recalc_attacks_received(&p2, &p1);
    snprintf(detail, sizeof(detail), "turned %d eighths", a2.direction_changes);
    check(a2.attacks_received == 2 && a2.direction_changes == 1,
          "and it counts the short way round", detail);

    /* Eight eighths is back to nothing, which is the original's own wrap: keep
     * hitting someone from behind and they eventually face you again. */
    a2.direction_changes = 4;
    combatant_move_to(&p1, point_make(25, 15));
    attack_recalc_attacks_received(&p2, &p1);
    check(a2.direction_changes == 0, "eight eighths wraps back to none",
          "the original's own arithmetic");

    /* --- the thief's knife in the back --- */
    a2.direction        = 0;               /* facing north, away from p1 */
    a2.attacks_received = 2;               /* already fighting someone else */
    p2.field_DE         = 1;               /* man-sized */
    combatant_move_to(&p1, point_make(25, 15));

    check(!attack_can_backstab(&p2, &p1),
          "a fighter cannot backstab", "no thief levels");

    p1.class_level[SKILL_THIEF] = 5;
    check(attack_can_backstab(&p2, &p1),
          "a thief behind a busy target can", "bare hands count");

    add_readied(&p1, ITEM_LONG_BOW, 0, 20);
    character_recalc_values(&p1);
    check(!attack_can_backstab(&p2, &p1),
          "but not with a bow in his hands", "knives and swords only");

    p1.item_count = 0;
    player_ready_reset(&p1);
    add_readied(&p1, ITEM_DAGGER, 0, 10);
    character_recalc_values(&p1);
    ok = attack_can_backstab(&p2, &p1);

    a2.attacks_received = 1;
    ok = ok && !attack_can_backstab(&p2, &p1);
    a2.attacks_received = 2;

    a2.direction = 4;                      /* turning to face p1 */
    ok = ok && !attack_can_backstab(&p2, &p1);
    a2.direction = 0;

    p2.field_DE = 3;                       /* bigger than a man */
    ok = ok && !attack_can_backstab(&p2, &p1);
    p2.field_DE = 1;

    check(ok, "a dagger, a busy target, and its back turned", "all three needed");

    /* --- armour class at a distance --- */
    p1.item_count = 0;
    player_ready_reset(&p1);
    add_readied(&p1, ITEM_LONG_BOW, 0, 20);
    add_readied(&p1, ITEM_ARROW, 0, 1);
    character_recalc_values(&p1);
    p1.class_level[SKILL_THIEF] = 0;

    {
        int bow_range = item_data(ITEM_LONG_BOW)->range;
        int third     = (bow_range - 1) / 3;
        int near_bonus, mid_bonus, far_bonus;

        combatant_move_to(&p1, point_make(2, 12));

        combatant_move_to(&p2, point_make(2 + third, 12));
        near_bonus = attack_ranged_defense_bonus(&p2, &p1);

        combatant_move_to(&p2, point_make(2 + third + 1, 12));
        mid_bonus = attack_ranged_defense_bonus(&p2, &p1);

        combatant_move_to(&p2, point_make(2 + third * 2 + 1, 12));
        far_bonus = attack_ranged_defense_bonus(&p2, &p1);

        snprintf(detail, sizeof(detail),
                 "range %d, thirds of %d: %d, %d and %d", bow_range, third,
                 near_bonus, mid_bonus, far_bonus);
        check(third > 0 && near_bonus == 0 && mid_bonus == 2 && far_bonus == 5,
              "a bow's target is harder to hit the further off it is", detail);
    }

    /* Nothing at all for a weapon that is not thrown or fired. */
    p1.item_count = 0;
    player_ready_reset(&p1);
    add_readied(&p1, ITEM_LONG_SWORD, 0, 60);
    character_recalc_values(&p1);
    check(attack_ranged_defense_bonus(&p2, &p1) == 0,
          "and none at all for a sword", "a sword has no range");

    /* --- who can be seen, and who is worth attacking --- */
    combatant_move_to(&p1, point_make(25, 12));
    combatant_move_to(&p2, point_make(27, 12));

    check(!attack_can_see_target(NULL, &p1) &&
          attack_can_see_target(&p1, &p1) && attack_can_see_target(&p2, &p1),
          "nobody cannot be seen and everybody else can",
          "a combatant always sees themselves");

    a1.target = NULL;
    check(attack_find_target(true, 0, 0xff, &p1) && a1.target == &p2,
          "the AI picks the other side", "the only enemy on the map");

    /* A target that has joined our side is dropped, and there is nobody else. */
    a1.target      = &p2;
    p2.combat_team = TEAM_OURS;
    check(!attack_find_target(false, 0, 0xff, &p1) && a1.target == NULL,
          "and drops one that has joined our side", "nothing left to attack");
    p2.combat_team = TEAM_ENEMY;

    /* One that has left the fight goes with it. The map is what the target list
     * is built from, so leaving the fight means coming off the map: a combatant
     * still standing there is still a target, in_combat or not. */
    a1.target    = &p2;
    p2.in_combat = false;
    gbl.combat_map[combatmap_player_index(&p2)].size = 0;
    combatmap_setup_player_index();
    check(!attack_find_target(false, 0, 0xff, &p1) && a1.target == NULL,
          "and one that has left the fight", "off the map with it");

    p2.in_combat = true;
    combatmap_place_combatant(false, point_make(27, 12), &p2);
    check(attack_find_target(true, 0, 0xff, &p1) && a1.target == &p2,
          "and finds it again when it comes back", "back on the map");

    {
        int range = 0xff;

        a1.target = NULL;
        ok = !attack_no_reachable_target(true, &range, &p1);
        snprintf(detail, sizeof(detail), "%d half-steps away", range);
        check(ok && a1.target == &p2 && range == 4,
              "and how far it has to walk to reach it", detail);
    }

    /* --- the aim list --- */
    count = attack_copy_sorted_players(sorted, (int)COAB_ARRAY_LEN(sorted), &p1);
    snprintf(detail, sizeof(detail), "%d combatants, nearest %s", count,
             count > 0 ? sorted[0].player->name : "-");
    check(count == 2 && sorted[0].player == &p1 && sorted[1].player == &p2,
          "the aim list holds everyone, nearest first", detail);

    {
        int list_index = 1;
        Point cursor = point_make(25, 12);
        Player *stepped;

        /* The list is 1-based, as the original's was, and wraps at both ends. */
        stepped = attack_step_combat_list(false, 1, &list_index, &cursor, sorted,
                                          count);
        ok = stepped == sorted[1].player && list_index == 2;

        stepped = attack_step_combat_list(false, 1, &list_index, &cursor, sorted,
                                          count);
        ok = ok && stepped == sorted[0].player && list_index == 1;

        stepped = attack_step_combat_list(false, -1, &list_index, &cursor, sorted,
                                          count);
        ok = ok && stepped == sorted[1].player && list_index == count;

        snprintf(detail, sizeof(detail), "index %d of %d", list_index, count);
        check(ok, "stepping it wraps round at both ends", detail);

        /* The C# indexed an empty list and threw; the port says so instead. */
        list_index = 1;
        check(attack_step_combat_list(false, 1, &list_index, &cursor, sorted,
                                      0) == NULL,
              "and an empty one hands back nobody", "logged, not thrown");
    }

    /* --- who a cure goes to --- */
    player_init(&p3);
    action_init(&a3);
    p3.actions     = &a3;
    p3.icon_id     = 0x0f;
    p3.field_DE    = 1;
    p3.in_combat   = true;
    p3.combat_team = TEAM_OURS;
    snprintf(p3.name, sizeof(p3.name), "%s", "Dragonbait");
    gbl.player_array[3] = &p3;
    gbl.combatant_count = 3;
    combatmap_setup_player_index();

    p1.combat_team       = TEAM_OURS;
    p1.hit_point_max     = 20;
    p1.hit_point_current = 20;
    p3.hit_point_max     = 20;
    p3.hit_point_current = 12;
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(26, 12), &p3);
    combatmap_place_combatant(false, point_make(40, 20), &p2);

    {
        Player *cure_target = &p1;

        check(attack_find_healing_target(&cure_target, &p1) &&
              cure_target == &p3,
              "a cure goes to the worst hurt team member in reach",
              "the one standing next to the healer");

        /* A body on the floor next to the healer comes first, unless someone on
         * their feet is nearly gone. */
        gbl.downed_players[0].map    = point_make(24, 12);
        gbl.downed_players[0].target = &p2;
        gbl.downed_player_count      = 1;
        p2.health_status             = STATUS_UNCONSCIOUS;
        ground_tile_map_set(gbl.map_to_background_tile, point_make(24, 12),
                            TILE_DOWN_PLAYER);

        cure_target = NULL;
        ok = attack_find_healing_target(&cure_target, &p1) &&
             cure_target == &p3;
        snprintf(detail, sizeof(detail), "%s",
                 cure_target != NULL ? cure_target->name : "nobody");
        check(ok, "the unconscious are passed over", detail);

        p2.health_status = STATUS_DYING;
        cure_target = NULL;
        ok = attack_find_healing_target(&cure_target, &p1) &&
             cure_target == &p2;

        /* Someone on their feet and below eight hit points comes first. */
        p3.hit_point_current = 5;
        ok = ok && attack_find_healing_target(&cure_target, &p1) &&
             cure_target == &p3;

        check(ok, "a body on the floor comes before a scratch",
              "but not before someone nearly gone");

        gbl.downed_player_count = 0;
        ground_tile_map_set(gbl.map_to_background_tile, point_make(24, 12),
                            0x18);
        p3.hit_point_current = 12;
    }

    /* Nobody in reach at all. */
    combatmap_place_combatant(false, point_make(10, 5), &p3);
    {
        Player *cure_target = &p3;

        check(!attack_find_healing_target(&cure_target, &p1) &&
              cure_target == NULL,
              "and nobody in reach is nobody", "the target is cleared either way");
    }

    /* --- how the other side is doing --- */
    p2.combat_team       = TEAM_ENEMY;
    p2.in_combat         = true;
    p2.hit_point_max     = 20;
    p2.hit_point_current = 10;
    p3.combat_team       = TEAM_ENEMY;
    p3.in_combat         = false;          /* down, so nothing left of it */
    p3.hit_point_max     = 20;
    p3.hit_point_current = 20;
    gbl.team_count       = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    gbl_team_add(&p3);

    gbl.enemy_health_percentage = 0;
    attack_calc_enemy_health_percentage();
    snprintf(detail, sizeof(detail), "%d per cent of the enemy left",
             gbl.enemy_health_percentage);
    check(gbl.enemy_health_percentage == 25,
          "what is left of the other side, to the nearest five", detail);

    /* --- what the other side can outrun --- */
    p1.movement  = 6;
    p2.movement  = 6;
    p3.movement  = 15;
    p3.in_combat = true;
    snprintf(detail, sizeof(detail), "%d half-steps for us, %d for them",
             attack_max_opposition_moves(&p1), attack_max_opposition_moves(&p2));
    check(attack_max_opposition_moves(&p1) == 15 &&
          attack_max_opposition_moves(&p2) == 6,
          "the best movement the other side has", detail);

    /* Someone who has left the fight is not part of the opposition any more. */
    p3.in_combat = false;
    snprintf(detail, sizeof(detail), "%d half-steps", attack_max_opposition_moves(&p1));
    check(attack_max_opposition_moves(&p1) == 6,
          "and the fallen do not count towards it", detail);

    /* --- a step, and what it costs --- */
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    a1.move             = 24;
    a1.attacks_received = 3;
    attack_move_step(2, &p1);                       /* east */
    snprintf(detail, sizeof(detail), "at %d,%d with %d halves left",
             combatmap_player_map_pos(&p1).x, combatmap_player_map_pos(&p1).y,
             a1.move);
    check(point_eq(combatmap_player_map_pos(&p1), point_make(26, 12)) &&
          a1.move < 24 && a1.attacks_received == 0,
          "a step east costs movement and clears the guard", detail);

    /* A diagonal costs half again as much as a straight step. */
    {
        int straight, diagonal;

        combatmap_place_combatant(false, point_make(25, 12), &p1);
        a1.move = 24;
        attack_move_step(2, &p1);
        straight = 24 - a1.move;

        combatmap_place_combatant(false, point_make(25, 12), &p1);
        a1.move = 24;
        attack_move_step(1, &p1);
        diagonal = 24 - a1.move;

        snprintf(detail, sizeof(detail), "%d halves against %d", diagonal,
                 straight);
        check(straight > 0 && diagonal == (straight / 2) * 3,
              "and a diagonal half again as much", detail);
    }

    /* A step off the map is refused rather than walked, which is the C#'s own
     * answer to its "regarding AI flee" question. */
    combatmap_place_combatant(false, point_make(0, 0), &p1);
    a1.move = 24;
    attack_move_step(0, &p1);                       /* north, off the top */
    check(point_eq(combatmap_player_map_pos(&p1), point_make(0, 0)) &&
          a1.move == 24, "a step off the map is refused",
          "nothing moves and nothing is spent");

    /* --- the blows themselves --- */
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(26, 12), &p2);
    p1.item_count = 0;
    player_ready_reset(&p1);
    add_readied(&p1, ITEM_LONG_SWORD, 0, 60);
    character_recalc_values(&p1);

    p1.hit_bonus = 100;                    /* only a rolled 1 misses */
    p2.ac = p2.ac_behind = 0x30;
    p2.hit_point_max     = 40;
    p2.hit_point_current = 40;
    p2.health_status     = STATUS_OKEY;
    p2.in_combat         = true;
    a2.attacks_received  = 0;
    a2.direction_changes = 0;
    p1.attack1_attacks_left = 2;
    p1.attack2_attacks_left = 0;
    a1.attack_idx = 2;

    ok = attack_deliver_blows(NULL, 0, &p2, &p1);
    snprintf(detail, sizeof(detail), "%d swings, %d landed, %d hit points left",
             gbl.attack_made_count[1], gbl.attack_hit_count[1],
             p2.hit_point_current);
    check(ok && gbl.attack_made_count[1] == 2 &&
          p1.attack1_attacks_left == 0 &&
          p2.hit_point_current <= 40,
          "both swings are taken and the turn ends", detail);

    /* A held target is cut down by the first blow, whatever is left of it: the
     * damage is what it has plus five, which is five points past zero and so
     * leaves it dying rather than dead. That is the original's own arithmetic. */
    p2.hit_point_current = 30;
    p2.health_status     = STATUS_OKEY;
    p2.in_combat         = true;
    held_set(&p2, true);
    p1.attack1_attacks_left = 2;
    p1.attack2_attacks_left = 0;
    a1.attack_idx = 2;

    ok = attack_deliver_blows(NULL, 0, &p2, &p1);
    snprintf(detail, sizeof(detail), "%s with %d hit points",
             player_health_status_name(p2.health_status), p2.hit_point_current);
    check(ok && p2.health_status == STATUS_DYING && !p2.in_combat &&
          p2.hit_point_current == 0 &&
          p1.attack1_attacks_left == 0 && p1.attack2_attacks_left == 0,
          "a held target is cut down by one cruel blow", detail);
    held_set(&p2, false);

    /* --- the cheat that ends a fight --- */
    p2.health_status     = STATUS_OKEY;
    p2.in_combat         = true;
    p2.hit_point_current = 40;
    p3.health_status     = STATUS_OKEY;
    p3.in_combat         = true;
    combatmap_place_combatant(false, point_make(26, 12), &p2);

    check(!attack_god_intervene(), "the gods stay out of it by default",
          "the cheat is off");

    cheats.allow_gods_intervene = true;
    ok = attack_god_intervene();
    cheats.allow_gods_intervene = false;
    check(ok && p2.health_status == STATUS_DEAD && !p2.in_combat &&
          p3.health_status == STATUS_DEAD && p1.health_status != STATUS_DEAD,
          "and drop every enemy when they do not",
          "the party is left standing");

    gbl.player_array[3] = NULL;
    gbl.combatant_count = 2;
    combat_scene_teardown();
    gbl.team_count       = 0;
    gbl.game_speed_var   = old_speed;

    printf("\n");
}

/* ------------------------------------------------------ setting a fight up */

/* How many of the 1250 combat floor squares hold something other than `value`,
 * and whether every square holds a tile the background table knows about. */
static int floor_count_other(int value, bool *out_all_known, int *out_highest)
{
    int other = 0;

    *out_all_known = true;
    *out_highest   = 0;

    for (int y = 0; y < MAP_MAX_Y; y++) {
        for (int x = 0; x < MAP_MAX_X; x++) {
            int tile = ground_tile_map_get(gbl.map_to_background_tile,
                                           point_make(x, y));

            if (tile != value) {
                other++;
            }
            if (tile > *out_highest) {
                *out_highest = tile;
            }
            if (tile < 0 || tile >= BACKGROUND_TILE_COUNT) {
                *out_all_known = false;
            }
        }
    }

    return other;
}

static int floor_tile(int x, int y)
{
    return ground_tile_map_get(gbl.map_to_background_tile, point_make(x, y));
}

static void check_battlesetup(const char *out_dir)
{
    Player p1, p2, p3;
    Action a1, a2, a3;
    GroundTileMap *scene_map;
    Player *next;
    char detail[240];
    int  old_speed    = gbl.game_speed_var;
    GameState old_state = gbl.game_state;
    u8   old_ecl      = gbl.ecl_block_id;
    u8   old_dir      = gbl.map_direction;
    int  old_pos_x    = gbl.map_pos_x;
    int  old_pos_y    = gbl.map_pos_y;
    i16  old_dungeon;
    u8   old_party_size;
    u16  old_distance;
    u16  old_field_58C;
    bool all_known;
    int  highest;
    int  other;
    int  nonzero = 0, colors = 0;

    printf("setting a fight up\n");

    if (gbl.geo_ptr == NULL || gbl.area_ptr == NULL || gbl.area2_ptr == NULL) {
        check(false, "a map and the area records to fight in", "one is NULL");
        return;
    }

    old_dungeon     = gbl.area_ptr->in_dungeon;
    old_party_size  = gbl.area2_ptr->party_size;
    old_distance    = gbl.area2_ptr->encounter_distance;
    old_field_58C   = gbl.area2_ptr->field_58C;

    gbl.game_speed_var = 0;
    rnd_seed(0x0b11a5e7u);

    /* ---- what stands round a dungeon square ---- */

    /* A plain wall north of 7,8 and a door north of 8,8, on an otherwise empty
     * map. The flags belong to the wall rather than to the square, so a wall
     * with no flags reads 0 and no wall at all reads 1 - which is why a plain
     * wall comes out as 1 here and open ground as 0. */
    memset(gbl.geo_ptr, 0, sizeof(*gbl.geo_ptr));
    gbl.ecl_block_id = 0;             /* an open block: off the map is nothing */
    gbl.map_pos_x    = 7;
    gbl.map_pos_y    = 8;
    gbl.geo_ptr->maps[8][7].wall_type_dir_0 = 6;
    gbl.geo_ptr->maps[8][7].x3_dir_0        = 0;
    gbl.geo_ptr->maps[8][8].wall_type_dir_0 = 6;
    gbl.geo_ptr->maps[8][8].x3_dir_0        = 2;

    snprintf(detail, sizeof(detail), "wall %d, door %d, open ground %d",
             battlesetup_square_side_flags(0, 8, 7),
             battlesetup_square_side_flags(0, 8, 8),
             battlesetup_square_side_flags(2, 8, 7));
    check(battlesetup_square_side_flags(0, 8, 7) == 1 &&
          battlesetup_square_side_flags(0, 8, 8) == 3 &&
          battlesetup_square_side_flags(2, 8, 7) == 0,
          "what stands on one side of a dungeon square", detail);

    check(battlesetup_square_side_flags(2, 8, -1) == 0 &&
          battlesetup_square_side_flags(6, 8, 16) == 0 &&
          battlesetup_square_side_flags(0, 8, -1) == 1 &&
          battlesetup_square_side_flags(2, 9, -1) == 1,
          "off the map is walled off except along the party's own row",
          "row 8 is open east and west, row 9 is not");

    snprintf(detail, sizeof(detail), "north of 7,8 is %d, south of 7,7 is %d, "
             "a door either way is %d",
             battlesetup_dir_flags(0, 8, 7), battlesetup_dir_flags(4, 7, 7),
             battlesetup_dir_flags(4, 7, 8));
    check(battlesetup_dir_flags(0, 8, 7) == 1 &&
          battlesetup_dir_flags(4, 7, 7) == 1 &&
          battlesetup_dir_flags(4, 7, 8) == 3,
          "a wall counts from either side of itself", detail);

    /* The wall is on the square x=7,y=8 and not on x=8,y=7, so a routine whose
     * two coordinates were crossed would find one here. */
    check(battlesetup_dir_flags(0, 7, 8) == 0,
          "the square asked about is the square that is looked at",
          "x=8,y=7 has nothing to the north");

    /* ---- the floor ---- */

    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "a fight to lay the ground out for", "out of memory");
        gbl.game_speed_var = old_speed;
        return;
    }
    scene_map = gbl.map_to_background_tile;

    ground_tile_map_fill(gbl.map_to_background_tile, 0);
    gbl.byte_1AD34 = 0;
    gbl.byte_1AD35 = 0;
    battlesetup_set_background_tile(22, 2, 3);

    /* Six dungeon squares east and two down puts this one off the east edge of
     * the 50-wide map, so it is dropped rather than wrapping onto the next row. */
    gbl.byte_1AD34 = 6;
    gbl.byte_1AD35 = 2;
    battlesetup_set_background_tile(22, 2, 3);

    other = floor_count_other(0, &all_known, &highest);
    snprintf(detail, sizeof(detail), "24,12 holds %d and %d square(s) changed",
             floor_tile(24, 12), other);
    check(floor_tile(24, 12) == 23 && other == 1,
          "one tile of the patch a dungeon square draws as", detail);

    gbl.byte_1AD34 = 0;
    gbl.byte_1AD35 = 0;

    ground_tile_map_fill(gbl.map_to_background_tile, 0);
    gbl.dir_0_flags = 1;
    battlesetup_build_tiles_2();
    other = floor_count_other(0, &all_known, &highest);
    snprintf(detail, sizeof(detail), "%d %d over %d %d, %d square(s) changed",
             floor_tile(24, 10), floor_tile(25, 10), floor_tile(24, 11),
             floor_tile(25, 11), other);
    check(floor_tile(24, 10) == 6 && floor_tile(25, 10) == 6 &&
          floor_tile(24, 11) == 11 && floor_tile(25, 11) == 11 && other == 4,
          "a wall along the north side of a square", detail);

    ground_tile_map_fill(gbl.map_to_background_tile, 0);
    gbl.dir_0_flags = 0;
    battlesetup_build_tiles_2();
    check(floor_tile(24, 10) == 23 && floor_tile(25, 11) == 23,
          "and bare floor where there is no wall to draw",
          "tile 23, the plain ground");

    ground_tile_map_fill(gbl.map_to_background_tile, 0);
    gbl.dir_6_flags = 1;
    battlesetup_build_tiles_1();
    other = floor_count_other(0, &all_known, &highest);
    snprintf(detail, sizeof(detail), "%d %d %d across row 12, %d square(s) laid",
             floor_tile(22, 12), floor_tile(23, 12), floor_tile(24, 12), other);
    check(floor_tile(21, 12) == 23 && floor_tile(22, 12) == 5 &&
          floor_tile(23, 12) == 4 && floor_tile(24, 12) == 14 && other == 18,
          "the floor of a square and the west wall slanting across it", detail);

    /* ---- furniture ---- */

    /* Walls north and south and nothing either side is a corridor, and a
     * corridor is never furnished however furnished the square is marked. */
    ground_tile_map_fill(gbl.map_to_background_tile, 23);
    gbl.dir_0_flags = 1;
    gbl.dir_4_flags = 1;
    gbl.dir_2_flags = 0;
    gbl.dir_6_flags = 0;
    gbl.byte_1AD3D  = 0x40;
    battlesetup_place_furniture();
    other = floor_count_other(23, &all_known, &highest);
    snprintf(detail, sizeof(detail), "%d square(s) changed", other);
    check(other == 0, "a corridor gets no furniture", detail);

    /* Walled all round is a room. Six squares of it are candidates, each an even
     * chance of a table - which the original's own bug then turns into a chair. */
    gbl.dir_2_flags = 1;
    gbl.dir_6_flags = 1;
    battlesetup_place_furniture();
    {
        int furniture = 0;

        other = 0;
        for (int y = 0; y < MAP_MAX_Y; y++) {
            for (int x = 0; x < MAP_MAX_X; x++) {
                int tile = floor_tile(x, y);

                if (tile == 23) {
                    continue;
                }
                other++;
                if (tile == TILE_TABLE || tile == TILE_CHAIR) {
                    furniture++;
                }
            }
        }

        snprintf(detail, sizeof(detail),
                 "%d of the 6 candidate squares, all of them tables or chairs",
                 other);
        check(other > 0 && other <= 6 && other == furniture,
              "a furnished room gets tables and chairs", detail);
    }

    /* ---- out in the open ---- */

    /* City 1's terrain byte is 0x18: roads, and growth thick enough for trees.
     * The scatter alone changes about a fifth of the map. */
    gbl.area_ptr->in_dungeon   = 0;
    gbl.area_ptr->current_city = 1;
    battlesetup_wilderness_floor();
    other = floor_count_other(23, &all_known, &highest);
    snprintf(detail, sizeof(detail),
             "%d of 1250 squares scattered, highest tile %d", other, highest);
    check(other > 100 && all_known && gbl.current_city == 1,
          "a wilderness floor of road, water and undergrowth", detail);

    /* ---- one Action per combatant ---- */

    player_init(&p3);
    action_init(&a3);
    p3.actions = &a3;
    snprintf(p3.name, sizeof(p3.name), "%s", "Hired hand");
    p3.icon_id   = 0x0f;
    p3.field_DE  = 1;
    p3.in_combat = true;

    p1.combat_team = TEAM_OURS;
    p2.combat_team = TEAM_ENEMY;
    p3.combat_team = TEAM_OURS;

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    gbl_team_add(&p3);

    /* Two in the party, so the third on the list is an NPC along for the ride. */
    gbl.area2_ptr->party_size = 2;
    gbl.area2_ptr->field_58C  = 0x33;
    gbl.map_direction         = 2;           /* facing east */
    p3.control_morale         = 0;

    battlesetup_combat_actions();

    snprintf(detail, sizeof(detail), "%d and %d, and the NPC's morale is %02x",
             p1.actions != NULL ? p1.actions->direction : -1,
             p2.actions != NULL ? p2.actions->direction : -1,
             p3.control_morale);
    check(p1.actions != NULL && p2.actions != NULL && p3.actions != NULL &&
          p1.actions != &a1 && p1.actions != p2.actions &&
          p1.actions->direction == 2 && p3.actions->direction == 2 &&
          p2.actions->direction == 6 &&
          !p1.actions->non_team_member && !p2.actions->non_team_member &&
          p3.actions->non_team_member &&
          p3.control_morale == 0x33 + CONTROL_NPC_BASE &&
          p1.control_morale == 0,
          "each side faces the other, and an NPC borrows the encounter's morale",
          detail);

    /* ---- both sides on the map ---- */

    ground_tile_map_fill(gbl.map_to_background_tile, 23);
    gbl.area2_ptr->encounter_distance = 3;
    gbl.combat_type         = COMBAT_TYPE_NORMAL;
    gbl.downed_player_count = 0;
    battlesetup_place_combatants();

    snprintf(detail, sizeof(detail), "ours at %d,%d and %d,%d, theirs at %d,%d",
             gbl.combat_map[1].pos.x, gbl.combat_map[1].pos.y,
             gbl.combat_map[3].pos.x, gbl.combat_map[3].pos.y,
             gbl.combat_map[2].pos.x, gbl.combat_map[2].pos.y);
    check(gbl.combatant_count == 3 &&
          gbl.player_array[1] == &p1 && gbl.player_array[2] == &p2 &&
          gbl.player_array[3] == &p3 &&
          gbl.combat_map[1].pos.x <= 32 && gbl.combat_map[3].pos.x <= 32 &&
          gbl.combat_map[2].pos.x >= 40 &&
          !point_eq(gbl.combat_map[1].pos, gbl.combat_map[3].pos),
          "the two sides stand an encounter's distance apart", detail);

    check(gbl.team_start_x[0] == 0 && gbl.team_start_y[0] == 0 &&
          gbl.team_start_x[1] == 3 && gbl.team_start_y[1] == 0 &&
          gbl.team_direction[0] == 1 && gbl.team_direction[1] == 3 &&
          gbl.half_team_count[0] == 1 && gbl.half_team_count[1] == 1,
          "each side anchored and facing the other",
          "ours east, theirs west, three squares away");

    /* A party member who is not fighting is put down and taken off again,
     * leaving a body on the square and the tile under it remembered. */
    gbl.area2_ptr->party_size = 3;      /* nobody an NPC this time */
    p3.in_combat              = false;
    gbl.downed_player_count   = 0;
    ground_tile_map_fill(gbl.map_to_background_tile, 23);
    battlesetup_combat_actions();
    battlesetup_place_combatants();

    snprintf(detail, sizeof(detail), "%d body at %d,%d over tile %d",
             gbl.downed_player_count,
             gbl.downed_players[0].map.x, gbl.downed_players[0].map.y,
             gbl.downed_players[0].original_background_tile);
    check(gbl.downed_player_count == 1 &&
          gbl.downed_players[0].target == &p3 &&
          gbl.downed_players[0].original_background_tile == 23 &&
          floor_tile(gbl.downed_players[0].map.x,
                     gbl.downed_players[0].map.y) == TILE_DOWN_PLAYER &&
          gbl.combat_map[3].size == 0 && gbl.combatant_count == 3,
          "one of the party who is not fighting lies where they were put",
          detail);

    /* Nowhere to stand at all: tile 1 cannot be walked on, so no square of
     * either side's block will take anybody. */
    p3.in_combat              = true;
    gbl.area2_ptr->party_size = 2;      /* the NPC again */
    p3.control_morale         = 0;
    battlesetup_combat_actions();
    gbl.downed_player_count = 0;
    ground_tile_map_fill(gbl.map_to_background_tile, 1);
    battlesetup_place_combatants();

    snprintf(detail, sizeof(detail),
             "%d combatants, %d left on the team list, selected %s",
             gbl.combatant_count, gbl.team_count,
             gbl.selected_player != NULL ? gbl.selected_player->name : "nobody");
    check(gbl.combatant_count == 2 && gbl.team_count == 2 &&
          gbl_team_index_of(&p3) < 0 && gbl.selected_player == &p2 &&
          gbl.area2_ptr->party_size == 2 &&
          /* The party's own two are counted although they are off the map, and
           * the original does not advance its combatant index past them, so the
           * slot the NPC was tried in is the one that ends up empty. */
          gbl.player_array[1] == NULL,
          "an NPC with nowhere to stand is turned loose", detail);

    gbl.current_team      = 0;
    gbl.team_direction[0] = 9;
    check(!battlesetup_place_combatant(1),
          "a side facing nowhere has no block to be placed in",
          "the C# would have indexed past its placement grid");
    gbl.team_direction[0] = gbl.map_direction / 2;

    /* ---- dropping a character out of the party (ovr018) ---- */

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    gbl_team_add(&p3);
    gbl.area2_ptr->party_size = 3;
    p2.item_count = 2;

    next = partymenu_free_current_player(&p2, false, false);
    snprintf(detail, sizeof(detail), "%s is selected, %d left, party of %d",
             next != NULL ? next->name : "nobody", gbl.team_count,
             gbl.area2_ptr->party_size);
    check(next == &p1 && gbl.team_count == 2 && gbl.team_list[1] == &p3 &&
          gbl.area2_ptr->party_size == 2 && p2.item_count == 0 &&
          p2.actions == NULL,
          "a character dropped from the middle of the party", detail);

    next = partymenu_free_current_player(&p1, false, true);
    check(next == &p3 && gbl.team_count == 1 &&
          gbl.area2_ptr->party_size == 2,
          "the first one out hands back whoever is first now",
          "and leaving the party size alone does not shrink it");

    next = partymenu_free_current_player(&p1, false, false);
    check(next == NULL && gbl.team_count == 1 &&
          gbl.area2_ptr->party_size == 2,
          "a character who is not on the team list is left alone",
          "nobody is selected in their place");

    /* ---- the whole of it: A battle begins... ---- */

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    gbl_team_add(&p3);
    gbl.area2_ptr->party_size = 3;
    p1.in_combat = true;
    p2.in_combat = true;
    p3.in_combat = true;
    p3.control_morale = 0;

    gbl.area_ptr->in_dungeon = 1;
    gbl.game_state           = GAME_STATE_COMBAT;
    gbl.combat_round         = 7;
    gbl.stinking_cloud_count = 3;
    gbl.cloud_kill_count     = 2;
    gbl.auto_pcs_cast_magic  = true;
    gbl.area2_ptr->field_666 = 5;

    battlesetup_battle_setup();

    check(gbl.map_to_background_tile != NULL &&
          gbl.map_to_background_tile != scene_map,
          "a battle brings its own floor with it",
          "the one the module owns, not the caller's");

    other = floor_count_other(0, &all_known, &highest);
    snprintf(detail, sizeof(detail), "%d of 1250 squares laid, highest tile %d",
             other, highest);
    check(other == MAP_MAX_X * MAP_MAX_Y && all_known,
          "the whole dungeon floor is drawn", detail);

    snprintf(detail, sizeof(detail), "round %d, stops at %d, %d combatants",
             gbl.combat_round, gbl.combat_round_no_action_limit,
             gbl.combatant_count);
    check(gbl.combat_round == 0 &&
          gbl.combat_round_no_action_limit ==
              GBL_COMBAT_ROUND_NO_ACTION_VALUE &&
          !gbl.auto_pcs_cast_magic && gbl.stinking_cloud_count == 0 &&
          gbl.cloud_kill_count == 0 && gbl.item_ptr == NULL &&
          gbl.downed_player_count == 0 && gbl.area2_ptr->field_666 == 0 &&
          gbl.combatant_count == 3 && gbl.missile_dax != NULL &&
          gbl.game_state == GAME_STATE_COMBAT,
          "the round, the clouds and the fallen all cleared", detail);

    {
        Point want = point_sub(combatmap_player_map_pos(gbl.team_list[0]),
                               point_screen_center());

        check(point_eq(gbl.map_to_background_tile->map_screen_top_left, want),
              "the view is centred on the first of the party",
              "map_screen_top_left is their square less half a screen");
    }

    /* The bare floor of a dungeon fight is colour 0, which the combat screen
     * shows as grey rather than black by swapping colours 0 and 8 in the
     * palette, so it is the walls and the combatants that are counted here. */
    frame_stats(&nonzero, &colors);
    snprintf(detail, sizeof(detail), "%d pixels set, %d colours", nonzero,
             colors);
    check(nonzero > 8000 && colors >= 8, "and the fight is on the screen",
          detail);
    dump(out_dir, "battle-begins.ppm");

    /* battle_begins swapped its own floor in; the scene's is the one the
     * teardown frees. */
    gbl.map_to_background_tile = scene_map;

    gbl.player_array[3] = NULL;
    gbl.combatant_count = 2;
    combat_scene_teardown();

    gbl.team_count      = 0;
    gbl.selected_player = NULL;
    gbl.downed_player_count = 0;
    gbl.area_ptr->in_dungeon          = old_dungeon;
    gbl.area2_ptr->party_size         = old_party_size;
    gbl.area2_ptr->encounter_distance = old_distance;
    gbl.area2_ptr->field_58C          = old_field_58C;
    gbl.map_direction  = old_dir;
    gbl.map_pos_x      = old_pos_x;
    gbl.map_pos_y      = old_pos_y;
    gbl.ecl_block_id   = old_ecl;
    gbl.game_state     = old_state;
    gbl.game_speed_var = old_speed;

    printf("\n");
}

/* -------------------------------------------- a turn taken without asking */

/* The AI decides everything from the map and the two Actions, so the combat
 * scene and a floor is all it needs. The one thing that would otherwise be a
 * coin toss is the saving throws the friendly-fire check rolls: save_verse is
 * what the d20 is measured against, so 0 saves against anything and 30 against
 * nothing, and the check can be asked both ways.
 *
 * Where an area spell is aimed matters as much: with the blast three squares
 * wide, the caster has to stand more than three squares off its own target or it
 * catches itself, which is what most of the placing below is arranging. */
static void check_monsterai(const char *out_dir)
{
    Player p1, p2, p3;
    Action a1, a2, a3;
    char detail[240];
    int  old_speed = gbl.game_speed_var;
    u8   old_dir   = gbl.map_direction;
    u16  old_field_58C = 0;
    bool old_can_cast  = false;
    bool off_map = false;
    int  nonzero = 0, colors = 0;
    Point start;
    bool ok;

    printf("a turn taken without asking\n");

    gbl.game_speed_var = 0;
    rnd_seed(0x0a1a5e10u);

    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "a fight for the AI to take a turn in", "out of memory");
        gbl.game_speed_var = old_speed;
        return;
    }

    if (gbl.area_ptr != NULL) {
        old_can_cast = gbl.area_ptr->can_cast_spells;
        gbl.area_ptr->can_cast_spells = false;
    }
    if (gbl.area2_ptr != NULL) {
        old_field_58C = gbl.area2_ptr->field_58C;
        gbl.area2_ptr->field_58C = 0;
    }

    /* One of ours, one of theirs, and a third of ours to be caught in blasts. */
    player_init(&p3);
    action_init(&a3);
    p3.actions   = &a3;
    p3.icon_id   = 0x0f;
    p3.field_DE  = 1;
    p3.in_combat = true;
    snprintf(p3.name, sizeof(p3.name), "%s", "Dragonbait");
    gbl.player_array[3] = &p3;
    gbl.combatant_count = 3;
    combatmap_setup_player_index();

    p1.combat_team = TEAM_OURS;
    p2.combat_team = TEAM_ENEMY;
    p3.combat_team = TEAM_OURS;
    p1.cls = p2.cls = p3.cls = CLASS_FIGHTER;
    p1.field_125 = p2.field_125 = p3.field_125 = 1;
    p1.movement = p2.movement = p3.movement = 12;
    p1.hit_point_max = p1.hit_point_current = 30;
    p2.hit_point_max = p2.hit_point_current = 30;
    p3.hit_point_max = p3.hit_point_current = 20;
    p1.health_status = p2.health_status = p3.health_status = STATUS_OKEY;
    set_strength_dex(&p1, 15, 0, 12);
    set_strength_dex(&p2, 15, 0, 12);
    set_strength_dex(&p3, 15, 0, 12);
    stat_value_load(&p1.stats.value[PSTAT_INT], 12);
    stat_value_load(&p2.stats.value[PSTAT_INT], 12);
    stat_value_load(&p3.stats.value[PSTAT_INT], 12);

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    gbl_team_add(&p3);
    character_count_combat_teams();

    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(28, 12), &p2);
    combatmap_place_combatant(false, point_make(27, 12), &p3);

    /* ---- one step of a movement pattern ---- */

    a1.field_15      = 0;              /* straight on, then to either side */
    a1.move          = 24;
    a1.fleeing       = false;
    a1.moral_failure = false;

    check(monsterai_can_move(&off_map, 2, 1, &p1) && !off_map,
          "the first step of a pattern goes straight where it wants to",
          "east, over clear floor");

    combatmap_place_combatant(false, point_make(26, 12), &p2);
    ok = !monsterai_can_move(&off_map, 2, 1, &p1) && !off_map;
    combatmap_place_combatant(false, point_make(28, 12), &p2);
    check(ok, "a square somebody is standing in cannot be stepped into",
          "the enemy is in the way east");

    /* Movement is spent in halves and the step has to cost less than what is
     * left, not the same, which is the original's own comparison. */
    a1.move = 2;
    ok = !monsterai_can_move(&off_map, 2, 1, &p1);
    a1.move = 3;
    ok = ok && monsterai_can_move(&off_map, 2, 1, &p1);
    a1.move = 24;
    check(ok, "a straight step over clear floor costs two halves",
          "two halves left is not enough, three is");

    ground_tile_map_set(gbl.map_to_background_tile, point_make(26, 12), 1);
    ok = !monsterai_can_move(&off_map, 2, 1, &p1);
    ground_tile_map_set(gbl.map_to_background_tile, point_make(26, 12), 0x18);
    check(ok, "and a square that cannot be walked on is refused",
          "tile 1, which has no move cost at all");

    /* Off the map is not a square to step onto but the way out of the fight, and
     * that is what *ground_clear says. */
    combatmap_place_combatant(false, point_make(0, 0), &p1);
    ok = !monsterai_can_move(&off_map, 0, 1, &p1) && off_map;
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    check(ok, "a step over the edge of the map reports the way out",
          "north out of the corner");

    /* A cloud kill needs no saving throw: under seven hit dice nothing walks in. */
    ground_tile_map_set(gbl.map_to_background_tile, point_make(26, 12),
                        TILE_CLOUD_KILL);
    p1.hit_dice = 3;
    ok = !monsterai_can_move(&off_map, 2, 1, &p1);
    p1.hit_dice = 7;
    ok = ok && monsterai_can_move(&off_map, 2, 1, &p1);
    ground_tile_map_set(gbl.map_to_background_tile, point_make(26, 12), 0x18);
    check(ok, "nothing under seven hit dice walks into a cloud kill",
          "seven walks straight through");

    /* The C# indexed its pattern table directly and would have thrown. */
    a1.field_15 = 20;
    ok = monsterai_can_move(&off_map, 2, 1, &p1);
    a1.field_15 = 0;
    check(ok, "a combatant with no movement pattern at all walks straight on",
          "logged, not thrown");

    /* ---- what a spell would catch ---- */

    gbl.selected_player = &p1;         /* the caster the check reads */

    p1.save_verse[SAVE_VERSE_SPELL] = 30;
    p3.save_verse[SAVE_VERSE_SPELL] = 30;
    check(monsterai_spell_would_catch_own_side(SPELL_FIREBALL,
                                              combatmap_player_map_pos(&p3)),
          "a fireball that would land on one of our own is called off",
          "nobody in the blast can save against it");

    p1.save_verse[SAVE_VERSE_SPELL] = 0;
    p3.save_verse[SAVE_VERSE_SPELL] = 0;
    check(!monsterai_spell_would_catch_own_side(SPELL_FIREBALL,
                                                combatmap_player_map_pos(&p3)),
          "and goes ahead when they can all save against it",
          "the same square, the same blast");

    p1.save_verse[SAVE_VERSE_SPELL] = 30;
    p3.save_verse[SAVE_VERSE_SPELL] = 30;
    check(!monsterai_spell_would_catch_own_side(SPELL_FIREBALL,
                                                point_make(2, 2)),
          "nobody of ours near where it lands is nobody to catch",
          "the far corner of the map");

    /* Spell 0x17 does nothing at all on a save, so there is nothing to check. */
    check(!monsterai_spell_would_catch_own_side(0x17,
                                               combatmap_player_map_pos(&p3)),
          "and a spell that does no damage is never checked",
          "hold person on our own square");

    /* ---- whether a spell is worth casting ---- */

    check(!monsterai_should_cast_spell(8, SPELL_FIREBALL, &p1),
          "a spell whose priority is short of this pass is passed over",
          "fireball is 7, not 8");

    check(monsterai_should_cast_spell(1, SPELL_BLESS, &p1),
          "a spell that needs no target at all is always worth casting",
          "bless");

    /* A cure reaches the healer's own square and the eight around it, so the one
     * to be cured has to be standing next to them. */
    combatmap_place_combatant(false, point_make(26, 12), &p3);
    p3.hit_point_current = 12;
    ok = monsterai_should_cast_spell(1, SPELL_CURE_LIGHT_WOUNDS, &p1);
    p3.hit_point_current = 20;
    ok = ok && !monsterai_should_cast_spell(1, SPELL_CURE_LIGHT_WOUNDS, &p1);
    check(ok, "a cure is worth casting while somebody needs it",
          "and not once everybody is whole");

    check(monsterai_should_cast_spell(4, SPELL_MAGIC_MISSILE, &p1),
          "a spell aimed at one target only needs something in range",
          "the enemy three squares off");

    /* Eight squares away the caster is clear of its own blast, so the only thing
     * that can call the fireball off is the friend standing beside the target. */
    combatmap_place_combatant(false, point_make(20, 12), &p1);
    check(!monsterai_should_cast_spell(7, SPELL_FIREBALL, &p1),
          "an area spell is held back when our own side is in the blast",
          "one of ours is standing next to the enemy");

    combatmap_place_combatant(false, point_make(5, 3), &p3);
    check(monsterai_should_cast_spell(7, SPELL_FIREBALL, &p1),
          "and cast once there is nobody of ours near it",
          "the friend has moved away");

    /* ---- turning undead ---- */

    a1.has_turned_undead = false;
    a1.delay             = 5;
    p1.class_level[SKILL_CLERIC] = 0;
    p2.field_E9 = 1;                   /* the weakest thing there is to turn */

    check(!monsterai_turn_undead(&p1), "a fighter turns nothing",
          "no cleric levels, now or ever");

    p1.class_level[SKILL_CLERIC] = 5;
    ok = monsterai_turn_undead(&p1) && a1.has_turned_undead;
    check(ok, "a cleric with undead in reach spends the turn on them",
          "and the turn is over");

    check(!monsterai_turn_undead(&p1), "and only once in a round",
          "the attempt is already made");

    /* Whatever turning did to it, the enemy is needed again. */
    p1.class_level[SKILL_CLERIC] = 0;
    p2.field_E9          = 0;
    p2.health_status     = STATUS_OKEY;
    p2.in_combat         = true;
    p2.hit_point_current = 30;
    a2.fleeing           = false;
    combatmap_place_combatant(false, point_make(28, 12), &p2);
    character_count_combat_teams();

    /* ---- a wand ---- */

    /* Pointing it runs the whole of ovr023.sub_5D2E1, which aims the spell
     * through gbl.spell_cast_function and asks "Abort Spell?" when it cannot be
     * aimed. Both are what a real fight has in place: the combat targeting, and
     * a combatant the AI is acting for, which is by definition in a quick
     * fight - so no prompt is reached. */
    gbl.spell_cast_function = attack_spell_targets;
    p1.quick_fight = QUICK_FIGHT_TRUE;

    p1.item_count = 0;
    player_ready_reset(&p1);
    {
        Item wand;

        /* affect_2 is the item's own spell and affect_3 stays under 0x80, which
         * is what marks a wand the AI may point rather than a scroll to read. */
        item_init(&wand, ITEM_WAND_A, 0, 0, 0, 0, 0, true, 0, false, 10, 6, 0,
                  AFFECT_NONE, (Affects)SPELL_FIREBALL, AFFECT_NONE);
        player_item_add(&p1, &wand);
    }
    character_recalc_values(&p1);

    a1.can_use = true;
    a1.delay   = 5;
    check(monsterai_try_magic_item(&p1),
          "a readied wand whose spell is worth casting is used",
          "the turn goes on the item");

    if (gbl.area_ptr != NULL) {
        gbl.area_ptr->can_cast_spells = true;
        ok = !monsterai_try_magic_item(&p1);
        gbl.area_ptr->can_cast_spells = false;
        check(ok, "and nothing is tried while the area's own flag is set",
              "no items where the flag says so");
    }

    /* ---- a memorized spell ---- */

    spell_list_clear(&p2.spell_list);
    spell_list_add_learnt(&p2.spell_list, SPELL_FIREBALL);
    a2.can_cast = true;
    a2.delay    = 5;
    a2.spell_id = 0;
    p2.control_morale   = CONTROL_NPC_BASE + 0x28;
    p2.save_verse[SAVE_VERSE_SPELL] = 0;
    gbl.selected_player = &p2;

    ok = monsterai_try_cast_spell(&p2);
    snprintf(detail, sizeof(detail), "spell 0x%x, delay %d", a2.spell_id,
             a2.delay);
    check(ok && a2.spell_id == SPELL_FIREBALL,
          "a monster casts what it has memorized", detail);

    /* One of the party casts nothing of its own accord until the player says it
     * may, which is what the '2' key in a quick fight turns on. */
    spell_list_clear(&p1.spell_list);
    spell_list_add_learnt(&p1.spell_list, SPELL_FIREBALL);
    a1.can_cast = true;
    a1.delay    = 5;
    a1.spell_id = 0;
    gbl.selected_player     = &p1;
    gbl.auto_pcs_cast_magic = false;

    ok = !monsterai_try_cast_spell(&p1) && a1.spell_id == 0;
    gbl.auto_pcs_cast_magic = true;
    ok = ok && monsterai_try_cast_spell(&p1) && a1.spell_id == SPELL_FIREBALL;
    gbl.auto_pcs_cast_magic = false;
    check(ok, "and one of the party only once the player has turned Magic on",
          "the same spell, cast or not cast");

    /* ---- one step of running away ---- */

    p1.item_count = 0;
    player_ready_reset(&p1);
    add_readied(&p1, ITEM_LONG_SWORD, 0, 60);
    character_recalc_values(&p1);

    a1.spell_id      = 0;
    a1.target        = NULL;
    a1.moral_failure = true;
    a1.fleeing       = false;
    a1.move          = 24;
    a1.delay         = 5;
    gbl.map_direction = 2;             /* the party came in facing east */

    start = combatmap_player_map_pos(&p1);
    monsterai_moral_failure_escape(&p1);
    snprintf(detail, sizeof(detail), "%d,%d to %d,%d, %d halves left, pattern %d",
             start.x, start.y, combatmap_player_map_pos(&p1).x,
             combatmap_player_map_pos(&p1).y, a1.move, a1.field_15);
    check(!point_eq(combatmap_player_map_pos(&p1), start) && a1.move < 24 &&
          a1.field_15 >= 1 && a1.field_15 <= 2,
          "one of the party whose nerve has gone takes a step away", detail);

    a1.move     = 0;
    a1.delay    = 5;
    a1.guarding = false;
    monsterai_moral_failure_escape(&p1);
    check(a1.guarding, "and stands ready when there is nothing left to move on",
          "the round is spent guarding");

    /* ---- closing with a target and swinging ---- */

    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(26, 12), &p2);
    p1.hit_bonus         = 100;        /* only a rolled 1 misses */
    p2.ac = p2.ac_behind = 0x30;
    p2.hit_point_max     = 40;
    p2.hit_point_current = 40;
    p2.health_status     = STATUS_OKEY;
    p2.in_combat         = true;
    a2.attacks_received  = 0;
    a2.direction_changes = 0;
    a1.moral_failure     = false;
    a1.fleeing           = false;
    a1.guarding          = false;
    a1.move              = 24;
    a1.delay             = 5;
    a1.attack_idx        = 2;
    a1.target            = &p2;
    p1.attack1_attacks_left = 1;
    p1.attack2_attacks_left = 0;

    ok = monsterai_close_and_attack(&p1);
    snprintf(detail, sizeof(detail), "%d hit points left of 40, delay %d",
             p2.hit_point_current, a1.delay);
    check(ok && gbl.byte_1D90E && p2.hit_point_current < 40,
          "the target next to us is reached and hit", detail);

    /* ---- the whole of a monster's turn ---- */

    p2.hit_point_max     = 30;
    p2.hit_point_current = 30;
    p2.health_status     = STATUS_OKEY;
    p2.in_combat         = true;
    spell_list_clear(&p2.spell_list);

    /* Both what the monster may ready and what it hits on are worked out again
     * as part of its turn, so neither can be forced by hand: it has to be a
     * fighter of some level for the sword to count as its own weapon and for its
     * to-hit bonus to come out above zero. */
    p2.class_level[SKILL_FIGHTER] = 4;
    classcalc_class_bonuses(&p2);
    p2.item_count = 0;
    player_ready_reset(&p2);
    add_readied(&p2, ITEM_LONG_SWORD, 0, 60);
    character_recalc_values(&p2);

    p1.hit_point_max     = 30;
    p1.hit_point_current = 30;
    p1.health_status     = STATUS_OKEY;
    p1.in_combat         = true;
    p1.ac = p1.ac_behind = 0;          /* standing there in nothing at all */
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(26, 12), &p2);
    character_count_combat_teams();

    /* The attacks are counted from the character rather than from what is left
     * of last round, so the monster needs an attack of its own to make. */
    p2.attacks_count = 2;
    p2.attack_level  = 4;

    action_init(&a2);
    p2.actions          = &a2;
    a2.field_15         = 0;
    a2.delay            = 5;
    a2.move             = 24;
    a2.attack_idx       = 2;
    p2.control_morale   = CONTROL_NPC_BASE + 0x28;       /* its nerve holds */
    gbl.enemy_health_percentage = 100;

    monsterai_player_quick_fight(&p2);
    snprintf(detail, sizeof(detail),
             "pattern %d, %d hit points left of 30, delay %d", a2.field_15,
             p1.hit_point_current, a2.delay);
    check(a2.field_15 >= 1 && a2.field_15 <= 6 &&
          p1.hit_point_current < 30 && a2.delay == 0,
          "a monster's whole turn: a pattern picked, closed with and swung",
          detail);

    /* A spell begun last round goes off at the top of this one, and that is the
     * whole turn - which is as far as it goes until ovr023 is translated. */
    action_init(&a2);
    p2.actions  = &a2;
    a2.field_15 = 3;
    a2.delay    = 5;
    a2.move     = 24;
    a2.spell_id = SPELL_FIREBALL;

    monsterai_player_quick_fight(&p2);
    snprintf(detail, sizeof(detail), "spell 0x%x, delay %d, %d halves left",
             a2.spell_id, a2.delay, a2.move);
    check(a2.spell_id == 0 && a2.delay == 0 && a2.move == 0,
          "a spell begun last round goes off and ends the turn", detail);

    /* Its side is finished, it has no morale of its own left, and the party can
     * outrun it - so it gives up rather than trying. */
    action_init(&a2);
    p2.actions        = &a2;
    a2.delay          = 5;
    a2.move           = 24;
    p2.control_morale = CONTROL_NPC_BASE;
    p1.movement       = 20;
    p2.movement       = 2;
    gbl.enemy_health_percentage = 0;

    monsterai_player_quick_fight(&p2);
    snprintf(detail, sizeof(detail), "%s, %s the fight",
             player_health_status_name(p2.health_status),
             p2.in_combat ? "still in" : "out of");
    check(p2.health_status == STATUS_UNCONSCIOUS && !p2.in_combat,
          "a monster too slow to run and bright enough to know it surrenders",
          detail);

    frame_stats(&nonzero, &colors);
    snprintf(detail, sizeof(detail), "%d pixels set, %d colours", nonzero,
             colors);
    check(nonzero > 1000 && colors >= 4,
          "and the fight is drawn as the AI plays it", detail);
    dump(out_dir, "monster-turn.ppm");

    gbl.player_array[3] = NULL;
    gbl.combatant_count = 2;
    combat_scene_teardown();

    gbl.team_count      = 0;
    gbl.selected_player = NULL;
    gbl.byte_1D90E      = false;
    gbl.monster_morale  = 0;
    gbl.enemy_health_percentage = 0;
    gbl.auto_pcs_cast_magic     = false;
    if (gbl.area_ptr != NULL) {
        gbl.area_ptr->can_cast_spells = old_can_cast;
    }
    if (gbl.area2_ptr != NULL) {
        gbl.area2_ptr->field_58C = old_field_58C;
    }
    gbl.map_direction  = old_dir;
    gbl.game_speed_var = old_speed;

    printf("\n");
}

/* -------------------------------------------------- a fight, round by round */

/* Everything ovr009 does is decided either by the map and the two Actions or by
 * a key, and both can be arranged from here: platform_push_key puts keys in the
 * queue the prompts read, with typed mode on so that several of them survive
 * GetInputKey's drain, and gbl.in_demo takes away the one prompt the round
 * checks would otherwise ask a player who is not there.
 *
 * The turn order is the one thing that is not arranged: FindNextCombatant rolls
 * a d100 for everyone, so the whole fight at the end is run against a fixed
 * seed and asked only that it end - one side gone, or the round limit reached -
 * rather than that it end a particular way.
 */
static void check_combatloop(const char *out_dir)
{
    Player p1, p2;
    Action a1, a2;
    GroundTileMap *scene_map;
    char detail[240];
    int  old_speed      = gbl.game_speed_var;
    u8   old_dir        = gbl.map_direction;
    int  old_pos_x      = gbl.map_pos_x;
    int  old_pos_y      = gbl.map_pos_y;
    u8   old_ecl        = gbl.ecl_block_id;
    GameState old_state = gbl.game_state;
    bool old_in_demo    = gbl.in_demo;
    bool old_delay      = gbl.delay_between_characters;
    int  old_round      = gbl.combat_round;
    int  old_limit      = gbl.combat_round_no_action_limit;
    i16  old_dungeon    = 0;
    u8   old_party_size = 0;
    u16  old_distance   = 0;
    u16  old_field_58C  = 0;
    bool old_can_cast   = false;
    bool ended = false;
    bool over;
    bool ok;
    int  hp_before;
    int  nonzero = 0, colors = 0;

    printf("a fight, round after round\n");

    gbl.game_speed_var = 0;
    rnd_seed(0x0c0ab009u);

    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "a fight to run round after round of", "out of memory");
        gbl.game_speed_var = old_speed;
        return;
    }
    scene_map = gbl.map_to_background_tile;

    if (gbl.area_ptr != NULL) {
        old_can_cast   = gbl.area_ptr->can_cast_spells;
        old_dungeon    = gbl.area_ptr->in_dungeon;
        gbl.area_ptr->can_cast_spells = false;
    }
    if (gbl.area2_ptr != NULL) {
        old_party_size = gbl.area2_ptr->party_size;
        old_distance   = gbl.area2_ptr->encounter_distance;
        old_field_58C  = gbl.area2_ptr->field_58C;
        gbl.area2_ptr->field_58C = 0;
    }

    /* The player is asked nothing that has not been pushed onto the queue, and
     * the round checks ask whether to keep fighting only outside the demo. */
    gbl.in_demo = true;
    platform_set_key_typed_mode(true);

    /* One of ours and one of theirs, both level four fighters with a sword: the
     * class levels are what make the sword their own weapon and give them a
     * to-hit bonus, and character_recalc_values works both out again from the
     * levels every time a blow is struck. */
    p1.combat_team = TEAM_OURS;
    p2.combat_team = TEAM_ENEMY;
    p1.cls = p2.cls = CLASS_FIGHTER;
    p1.field_125 = p2.field_125 = 1;
    /* Movement is worked out again from base_movement every time the values are,
     * and a fight begins by working everybody's out: without a base there is
     * nothing to walk with and neither side would ever reach the other. */
    p1.base_movement = p2.base_movement = 12;
    p1.movement = p2.movement = 12;
    p1.hit_point_max = p1.hit_point_current = 30;
    p2.hit_point_max = p2.hit_point_current = 30;
    p1.health_status = p2.health_status = STATUS_OKEY;
    p1.ac = p1.ac_behind = 0;
    p2.ac = p2.ac_behind = 0;
    p1.attacks_count = p2.attacks_count = 2;
    p1.attack_level  = p2.attack_level  = 4;
    set_strength_dex(&p1, 15, 0, 12);
    set_strength_dex(&p2, 15, 0, 12);
    stat_value_load(&p1.stats.value[PSTAT_INT], 12);
    stat_value_load(&p2.stats.value[PSTAT_INT], 12);
    p1.class_level[SKILL_FIGHTER] = 4;
    p2.class_level[SKILL_FIGHTER] = 4;
    classcalc_class_bonuses(&p1);
    classcalc_class_bonuses(&p2);
    add_readied(&p1, ITEM_LONG_SWORD, 0, 60);
    add_readied(&p2, ITEM_LONG_SWORD, 0, 60);
    character_recalc_values(&p1);
    character_recalc_values(&p2);

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(28, 12), &p2);
    character_count_combat_teams();

    /* ---- handing a character over to the AI ---- */

    a1.target = &p2;
    combatloop_set_player_quick_fight(&p1);
    ok = p1.quick_fight == QUICK_FIGHT_TRUE && a1.target == &p2;

    a1.target = &p1;
    combatloop_set_player_quick_fight(&p1);
    check(ok && a1.target == NULL,
          "a character handed to the AI keeps an enemy target and forgets a "
          "friendly one",
          "it would otherwise open by attacking one of its own");

    p1.quick_fight = QUICK_FIGHT_FALSE;
    a1.target      = NULL;

    /* ---- the Done menu ---- */

    platform_clear_keys();
    platform_push_key('g');
    a1.delay    = 10;
    a1.move     = 12;
    a1.guarding = false;
    combatloop_delay_menu(&ended, &p1);
    snprintf(detail, sizeof(detail), "guarding %s, delay %d",
             a1.guarding ? "set" : "not set", a1.delay);
    check(ended && a1.guarding && a1.delay == 0,
          "Guard swings at whoever comes near and spends the turn", detail);

    platform_clear_keys();
    platform_push_key('d');
    a1.delay = 10;
    combatloop_delay_menu(&ended, &p1);
    snprintf(detail, sizeof(detail), "delay %d", a1.delay);
    check(ended && a1.delay == 1,
          "Delay puts them at the back of the order, to act after everyone else",
          detail);

    platform_clear_keys();
    platform_push_key('q');
    a1.delay = 10;
    a1.move  = 12;
    combatloop_delay_menu(&ended, &p1);
    snprintf(detail, sizeof(detail), "delay %d, %d halves of movement left",
             a1.delay, a1.move);
    check(ended && a1.delay == 0 && a1.move == 0,
          "and Quit spends it doing nothing at all", detail);

    platform_clear_keys();
    platform_push_key(0x1b);
    a1.delay = 10;
    combatloop_delay_menu(&ended, &p1);
    check(!ended && a1.delay == 10,
          "Escape leaves the menu with the turn still to spend",
          "the combat menu is asked again");

    /* ---- the game speed, which only this menu moves ---- */

    gbl.game_speed_var = 4;
    platform_clear_keys();
    platform_push_key('s');
    platform_push_key('s');
    platform_push_key('f');
    platform_push_key('e');
    combatloop_set_gamespeed();
    snprintf(detail, sizeof(detail), "speed %d, two slower and one faster than 4",
             gbl.game_speed_var);
    check(gbl.game_speed_var == 5, "Slower, Slower, Faster and out", detail);
    gbl.game_speed_var = 0;

    /* ---- moving, step by step ---- */

    /* Out of reach: a step taken past somebody is a step they get a swing at. */
    combatmap_place_combatant(false, point_make(35, 20), &p2);
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    character_count_combat_teams();

    a1.delay = 10;
    a1.move  = 4;                      /* two straight steps over plain floor */
    platform_clear_keys();
    platform_push_key(0x4d00);         /* the right cursor key: east again */
    combatloop_move_menu(&ended, 'M', &p1);
    snprintf(detail, sizeof(detail), "%d,%d with %d halves left",
             combatmap_player_map_pos(&p1).x, combatmap_player_map_pos(&p1).y,
             a1.move);
    check(!ended && point_eq(combatmap_player_map_pos(&p1), point_make(27, 12)) &&
          a1.move == 0,
          "two steps east, at two halves of movement each", detail);

    combatmap_place_combatant(false, point_make(25, 12), &p1);
    a1.move = 12;
    platform_clear_keys();
    platform_push_key(0x1b);
    combatloop_move_menu(&ended, 'M', &p1);
    snprintf(detail, sizeof(detail), "%d,%d with %d halves left",
             combatmap_player_map_pos(&p1).x, combatmap_player_map_pos(&p1).y,
             a1.move);
    check(!ended && point_eq(combatmap_player_map_pos(&p1), point_make(25, 12)) &&
          a1.move == 12,
          "and Escape puts them back where the move started, with the movement "
          "they started with", detail);

    /* ---- walking off the map ---- */

    combatmap_place_combatant(false, point_make(0, 12), &p1);
    a1.move = 12;
    platform_clear_keys();
    platform_push_key('n');            /* Flee: No */
    platform_push_key(0x1b);           /* and out of the move menu */
    combatloop_move_menu(&ended, 'K', &p1);
    check(!ended && p1.in_combat && a1.move == 12 &&
          point_eq(combatmap_player_map_pos(&p1), point_make(0, 12)),
          "a step over the edge of the map is offered as fleeing, and refused",
          "west out of column zero");

    a1.move = 12;
    platform_clear_keys();
    platform_push_key('y');
    combatloop_move_menu(&ended, 'K', &p1);
    snprintf(detail, sizeof(detail), "%s, %s the fight",
             player_health_status_name(p1.health_status),
             p1.in_combat ? "still in" : "out of");
    check(ended && !p1.in_combat && p1.health_status == STATUS_RUNNING &&
          p1.hit_point_current == 30,
          "and taken: nobody near enough to stop them, so they get away", detail);

    p1.in_combat     = true;
    p1.health_status = STATUS_OKEY;
    gbl.downed_player_count = 0;

    /* ---- walking into somebody ---- */

    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(26, 12), &p2);
    character_count_combat_teams();
    character_recalc_values(&p1);

    /* The top of a turn, which is where the attack the blows are counted
     * against is worked out: with no attack chosen there is nothing to swing. */
    attack_calculate_initiative(&p1);
    a1.target  = NULL;
    a1.delay   = 10;
    hp_before  = p2.hit_point_current;
    gbl.attack_made_count[1] = 0;
    combatloop_move_into_target(&ended, &p2, &p1);
    snprintf(detail, sizeof(detail), "%d blows struck, %d hit points left of %d",
             gbl.attack_made_count[1], p2.hit_point_current, hp_before);
    check(ended && a1.target == &p2 && gbl.attack_made_count[1] > 0,
          "walking into somebody swings at them, and that is the turn", detail);

    /* A bow is drawn, not swung, so there is nothing to walk into with. */
    p1.item_count = 0;
    player_ready_reset(&p1);
    add_readied(&p1, ITEM_LONG_BOW, 0, 40);
    character_recalc_values(&p1);
    attack_recalc_attacks(&p1);
    a1.target = NULL;
    hp_before = p2.hit_point_current;
    gbl.attack_made_count[1] = 0;
    combatloop_move_into_target(&ended, &p2, &p1);
    check(a1.target == NULL && gbl.attack_made_count[1] == 0 &&
          p2.hit_point_current == hp_before,
          "a bow is no use for walking into somebody", "\"Not with that weapon\"");

    p1.item_count = 0;
    player_ready_reset(&p1);
    add_readied(&p1, ITEM_LONG_SWORD, 0, 60);
    character_recalc_values(&p1);

    /* ---- the combat menu ---- */

    p1.in_combat = false;
    a1.delay = 10;
    a1.move  = 12;
    combatloop_combat_menu(&p1);
    check(a1.delay == 0 && a1.move == 0,
          "somebody who is out of the fight has their turn cleared",
          "no menu is put up for them");
    p1.in_combat = true;

    a1.delay    = 10;
    a1.move     = 12;
    a1.spell_id = SPELL_MAGIC_MISSILE;
    combatloop_combat_menu(&p1);
    snprintf(detail, sizeof(detail), "spell %d, delay %d", a1.spell_id, a1.delay);
    check(a1.spell_id == 0 && a1.delay == 0,
          "a spell begun last round goes off instead of a menu", detail);

    /* The space bar is the whole party off quick fight; a monster's morale is
     * its own, so it stays on it. */
    p1.quick_fight    = QUICK_FIGHT_TRUE;
    p2.quick_fight    = QUICK_FIGHT_TRUE;
    p1.control_morale = 0;
    p2.control_morale = (u8)(CONTROL_NPC_BASE + 5);
    a1.delay    = 10;
    a1.move     = 12;
    a1.guarding = false;
    platform_clear_keys();
    platform_push_key(' ');            /* off quick fight */
    platform_push_key('d');            /* Done */
    platform_push_key('g');            /* Guard */
    combatloop_combat_menu(&p1);
    snprintf(detail, sizeof(detail), "ours %s, theirs %s, guarding %s",
             p1.quick_fight == QUICK_FIGHT_TRUE ? "on" : "off",
             p2.quick_fight == QUICK_FIGHT_TRUE ? "on" : "off",
             a1.guarding ? "set" : "not set");
    check(p1.quick_fight == QUICK_FIGHT_FALSE &&
          p2.quick_fight == QUICK_FIGHT_TRUE && a1.guarding,
          "the space bar takes the party off quick fight, and Done leads to "
          "Guard", detail);

    /* Ctrl-P is the other way round: everybody on it, and this one first. */
    p1.quick_fight    = QUICK_FIGHT_FALSE;
    p2.quick_fight    = QUICK_FIGHT_FALSE;
    p2.control_morale = 0;
    a1.delay = 10;
    a1.move  = 12;
    platform_clear_keys();
    platform_push_key(0x1000);         /* the scan code, as an extended key */
    combatloop_combat_menu(&p1);
    snprintf(detail, sizeof(detail), "delay %d, ours %s, theirs %s", a1.delay,
             p1.quick_fight == QUICK_FIGHT_TRUE ? "on" : "off",
             p2.quick_fight == QUICK_FIGHT_TRUE ? "on" : "off");
    check(a1.delay == 20 && p1.quick_fight == QUICK_FIGHT_TRUE &&
          p2.quick_fight == QUICK_FIGHT_TRUE,
          "ctrl-P hands the whole party over and goes to the front of the order",
          detail);

    p1.quick_fight = QUICK_FIGHT_FALSE;
    p2.quick_fight = QUICK_FIGHT_FALSE;

    /* ---- one combatant's turn ---- */

    a1.delay            = 0;
    a1.move             = 12;
    a1.attacks_received = 3;
    a1.guarding         = true;
    gbl.selected_player = NULL;
    combatloop_do_player_combat_turn(&p1);
    snprintf(detail, sizeof(detail), "%d halves of movement left, %d blows taken",
             a1.move, a1.attacks_received);
    check(a1.move == 12 && a1.attacks_received == 0 && !a1.guarding &&
          gbl.selected_player == NULL,
          "a combatant whose delay has run out is passed over",
          detail);

    /* The AI's own turn, from the delay ctrl-P left behind. */
    combatmap_place_combatant(false, point_make(25, 12), &p1);
    combatmap_place_combatant(false, point_make(28, 12), &p2);
    character_count_combat_teams();
    p1.quick_fight = QUICK_FIGHT_TRUE;
    p1.hit_point_current = 30;
    p2.hit_point_current = 30;
    attack_calculate_initiative(&p1);
    attack_calculate_initiative(&p2);
    a1.delay = 20;
    a1.move  = 12;
    combatloop_do_player_combat_turn(&p1);
    snprintf(detail, sizeof(detail), "at %d,%d with delay %d",
             combatmap_player_map_pos(&p1).x, combatmap_player_map_pos(&p1).y,
             a1.delay);
    check(gbl.selected_player == &p1 && a1.delay == 0,
          "a turn on quick fight is taken by the AI, and the mark ctrl-P left "
          "is spent on it", detail);
    p1.quick_fight = QUICK_FIGHT_FALSE;

    /* ---- the end of a round ---- */

    p1.in_combat = p2.in_combat = true;
    p1.health_status = p2.health_status = STATUS_OKEY;
    p1.hit_point_current = p2.hit_point_current = 30;
    a1.bleeding = a2.bleeding = 0;
    gbl.combat_round = 0;
    gbl.combat_round_no_action_limit = GBL_COMBAT_ROUND_NO_ACTION_VALUE;

    over = combatloop_battle_round_checks();
    snprintf(detail, sizeof(detail), "round %d, %d of ours and %d of theirs",
             gbl.combat_round, gbl.friends_count, gbl.foe_count);
    check(!over && gbl.combat_round == 1 && gbl.friends_count == 1 &&
          gbl.foe_count == 1,
          "a round ends with both sides counted again", detail);

    p1.health_status = STATUS_DYING;
    a1.bleeding      = 9;
    over = combatloop_battle_round_checks();
    snprintf(detail, sizeof(detail), "%s after %d rounds of it, the fight %s",
             player_health_status_name(p1.health_status), a1.bleeding,
             over ? "over" : "going on");
    check(p1.health_status == STATUS_DEAD && a1.bleeding == 10 &&
          gbl.combat_round == 2,
          "ten rounds of bleeding is as long as anyone lasts", detail);

    p1.health_status = STATUS_OKEY;
    p1.in_combat     = true;
    a1.bleeding      = 0;
    gbl.combat_round = 13;
    gbl.combat_round_no_action_limit = GBL_COMBAT_ROUND_NO_ACTION_VALUE;
    ok   = combatloop_battle_round_checks() == false;
    over = combatloop_battle_round_checks();
    snprintf(detail, sizeof(detail), "round %d of %d", gbl.combat_round,
             gbl.combat_round_no_action_limit);
    check(ok && over && gbl.combat_round == 15,
          "and a fight in which nothing happens is stopped at the round limit",
          detail);

    /* ---- what a fight leaves behind ---- */

    gbl.stinking_cloud_count = 3;
    gbl.cloud_kill_count     = 2;
    dax_block_free(gbl.missile_dax);
    gbl.missile_dax = dax_block_new(1, 4, 3, 0x18);
    gbl.spell_cast_function = attack_spell_targets;
    combatloop_free_combat_stuff();
    check(gbl.stinking_cloud_count == 0 && gbl.cloud_kill_count == 0 &&
          gbl.map_to_background_tile == NULL && gbl.missile_dax == NULL &&
          gbl.spell_cast_function != attack_spell_targets,
          "the end of a fight drops the clouds, the floor and the missile",
          "and puts spells back to the way they are aimed out of a fight");
    gbl.map_to_background_tile = scene_map;

    /* ---- and the whole of it ---- */

    /* An open dungeon block, the party of one facing north, and the monster one
     * dungeon square in front of them. The distance cannot be much more than
     * that: ovr011 lays out a side's block five combat rows to the dungeon
     * square and counts them from row ten, so three squares to the north puts
     * the whole block off the top of the map and leaves the monster with
     * nowhere to stand. ovr011 also lays its own floor and hands out its own
     * Action records, so the two here are put back afterwards. */
    memset(gbl.geo_ptr, 0, sizeof(*gbl.geo_ptr));
    gbl.ecl_block_id  = 0;
    gbl.map_pos_x     = 7;
    gbl.map_pos_y     = 8;
    gbl.map_direction = 0;
    gbl.area_ptr->in_dungeon          = 1;
    gbl.area2_ptr->party_size         = 1;
    gbl.area2_ptr->encounter_distance = 1;

    p1.in_combat = p2.in_combat = true;
    p1.health_status = p2.health_status = STATUS_OKEY;
    p1.hit_point_current = 30;
    p2.hit_point_current = 6;          /* a monster who will not last long */
    p1.quick_fight = QUICK_FIGHT_TRUE;
    p2.quick_fight = QUICK_FIGHT_TRUE;
    p1.control_morale = 0;
    p2.control_morale = 0;
    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);

    combatloop_main_combat_loop();

    snprintf(detail, sizeof(detail),
             "%d rounds, %d of ours and %d of theirs left, the monster %s",
             gbl.combat_round, gbl.friends_count, gbl.foe_count,
             player_health_status_name(p2.health_status));
    check((gbl.friends_count == 0 || gbl.foe_count == 0 ||
           gbl.combat_round >= gbl.combat_round_no_action_limit) &&
          gbl.combat_round >= 1 && gbl.game_state == GAME_STATE_COMBAT &&
          gbl.map_to_background_tile == NULL && gbl.missile_dax == NULL &&
          gbl.delay_between_characters,
          "a whole fight, from \"A battle begins...\" to the last one standing",
          detail);

    frame_stats(&nonzero, &colors);
    snprintf(detail, sizeof(detail), "%d pixels set, %d colours", nonzero,
             colors);
    check(nonzero > 5000 && colors >= 8,
          "and the fight is on the screen as it is fought", detail);
    dump(out_dir, "combat-loop.ppm");

    /* ovr011 pointed both of them at its own Action records. */
    gbl.map_to_background_tile = scene_map;
    p1.actions = &a1;
    p2.actions = &a2;

    platform_clear_keys();
    platform_set_key_typed_mode(false);
    combat_scene_teardown();

    gbl.team_count      = 0;
    gbl.selected_player = NULL;
    gbl.auto_pcs_cast_magic = false;
    gbl.enemy_health_percentage = 0;
    gbl.combat_round    = old_round;
    gbl.combat_round_no_action_limit = old_limit;
    gbl.delay_between_characters = old_delay;
    gbl.in_demo         = old_in_demo;
    if (gbl.area_ptr != NULL) {
        gbl.area_ptr->can_cast_spells = old_can_cast;
        gbl.area_ptr->in_dungeon      = old_dungeon;
    }
    if (gbl.area2_ptr != NULL) {
        gbl.area2_ptr->party_size         = old_party_size;
        gbl.area2_ptr->encounter_distance = old_distance;
        gbl.area2_ptr->field_58C          = old_field_58C;
    }
    gbl.map_direction  = old_dir;
    gbl.map_pos_x      = old_pos_x;
    gbl.map_pos_y      = old_pos_y;
    gbl.ecl_block_id   = old_ecl;
    gbl.game_state     = old_state;
    gbl.game_speed_var = old_speed;

    printf("\n");
}

/* -------------------------------------------------------- after the fighting */

/* Two of ours and one of theirs, with nothing on the ground and nothing in the
 * pool: the state each of the checks below starts from. The monster is dead, has
 * a hundred gold and two items on it, and is worth 3 * 4 + 10 experience - one
 * of ovr006's two sums, the other being the 400 an enchantment is worth.
 *
 * The party is the front of the team list and the monster's action record is
 * marked as not being a team member's, which is where ovr006 stops looking for
 * party members. */
static void after_combat_scene(Player *pa, Action *aa, Player *pb, Action *ab,
                               Player *pm, Action *am)
{
    Item carried;

    player_init(pa);
    player_init(pb);
    player_init(pm);
    action_init(aa);
    action_init(ab);
    action_init(am);
    pa->actions = aa;
    pb->actions = ab;
    pm->actions = am;
    am->non_team_member = true;

    snprintf(pa->name, sizeof(pa->name), "%s", "Alias");
    snprintf(pb->name, sizeof(pb->name), "%s", "Dragonbait");
    snprintf(pm->name, sizeof(pm->name), "%s", "Kobold");

    pa->combat_team = TEAM_OURS;
    pb->combat_team = TEAM_OURS;
    pm->combat_team = TEAM_ENEMY;

    pa->cls = CLASS_FIGHTER;
    pb->cls = CLASS_CLERIC;
    pm->cls = CLASS_FIGHTER;
    pa->class_level[SKILL_FIGHTER] = 4;
    pb->class_level[SKILL_CLERIC]  = 4;
    pa->hit_dice = pb->hit_dice = 4;

    pa->control_morale = 0;                      /* both of ours are the party's */
    pb->control_morale = 0;
    pm->control_morale = CONTROL_NPC_BASE;

    pa->hit_point_max = pa->hit_point_current = 20;
    pb->hit_point_max = pb->hit_point_current = 12;
    pa->health_status = STATUS_OKEY;
    pb->health_status = STATUS_OKEY;
    pa->in_combat = true;
    pb->in_combat = true;

    /* Dead, and not going anywhere: an enemy who ran keeps everything. */
    pm->health_status     = STATUS_DEAD;
    pm->in_combat         = false;
    pm->field_13E         = 3;                   /* worth, per hit point rolled */
    pm->hit_point_rolled  = 4;
    pm->field_13C         = 10;                  /* and a flat bonus on top */
    money_clear_all(&pm->money);
    money_set(&pm->money, MONEY_GOLD, 100);

    /* A plain sword and an enchanted dagger, the second of which is the only one
     * worth anything in experience. */
    item_init(&carried, ITEM_LONG_SWORD, 0, 0, 0, 0, 0, true, 0, false, 60, 1,
              30, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
    player_item_add(pm, &carried);
    item_init(&carried, ITEM_DAGGER, 0, 0, 0, 1, 0, false, 0, false, 10, 1, 5,
              AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
    player_item_add(pm, &carried);

    gbl.team_count = 0;
    gbl_team_add(pa);
    gbl_team_add(pb);
    gbl_team_add(pm);
    gbl.selected_player = pa;

    gbl.combat_type = COMBAT_TYPE_NORMAL;
    gbl.byte_1AB14  = false;
    gbl.battle_won  = false;
    gbl.party_fled  = false;
    gbl.party_killed = false;
    gbl.party_animated_count = 0;
    gbl.exp_to_add  = 0;
    gbl.item_ptr    = NULL;

    gbl_ground_items_clear();
    money_clear_all(&gbl.pooled_money);

    gbl.area2_ptr->party_size = 2;
    gbl.area2_ptr->is_duel    = false;
    gbl.area2_ptr->field_58E  = 0;
    gbl.area2_ptr->field_590  = 0;
    gbl.area2_ptr->field_5C6  = 0;
}

/* ovr006 asks the player four things - how the fight went, what to do with the
 * treasure, which piece of it to take and whether to take coin or items - and
 * every one of them is a prompt, so all of this is driven from the key queue in
 * typed mode. The timeout is armed throughout as a backstop, answering '\0'
 * after a second: a prompt this test forgot to feed would otherwise wait for a
 * keyboard that is not there, and '\0' is what Escape comes back as, which every
 * prompt in the overlay takes as "leave".
 *
 * None of this is a fight, so no combatant is placed and no ground map is laid:
 * the whole overlay works from the team list, what is lying on the ground and
 * the pooled money.
 */
static void check_aftercombat(const char *out_dir)
{
    Player pa, pb, pm;
    Action aa, ab, am;
    Item   dropped;
    char detail[240];
    int  worth;
    int  expected;
    int  earned;
    Item *picked = NULL;
    char key = '\0';
    bool ok;
    bool items_present, money_present;
    int  nonzero = 0, colors = 0;
    int  old_speed      = gbl.game_speed_var;
    bool old_in_demo    = gbl.in_demo;
    bool old_delay      = gbl.delay_between_characters;
    bool old_sort       = cheats.sort_treasure;
    int  old_combat_type = gbl.combat_type;
    GameState old_state = gbl.game_state;
    bool old_killed     = gbl.party_killed;
    bool old_fled       = gbl.party_fled;
    bool old_won        = gbl.battle_won;
    bool old_flag       = gbl.byte_1AB14;
    int  old_animated   = gbl.party_animated_count;
    int  old_exp        = gbl.exp_to_add;
    Player *old_selected = gbl.selected_player;
    Item *old_item_ptr  = gbl.item_ptr;
    int  old_text_x     = gbl.text_x_col;
    int  old_text_y     = gbl.text_y_col;
    int  old_wait       = gbl.display_input_seconds_to_wait;
    char old_timeout    = gbl.display_input_timeout_value;
    MoneySet old_pool   = gbl.pooled_money;
    u8   old_party_size = gbl.area2_ptr->party_size;
    bool old_is_duel    = gbl.area2_ptr->is_duel;
    i16  old_field_58E  = gbl.area2_ptr->field_58E;
    i16  old_field_590  = gbl.area2_ptr->field_590;
    i16  old_field_5C6  = gbl.area2_ptr->field_5C6;
    i16  old_field_6E0  = gbl.area2_ptr->field_6E0;
    i16  old_field_6E2  = gbl.area2_ptr->field_6E2;
    i16  old_field_6E4  = gbl.area2_ptr->field_6E4;
    u8   old_fade       = gbl.area_ptr->picture_fade;

    printf("after the fighting\n");

    gbl.game_speed_var = 0;
    gbl.in_demo        = false;
    gbl.area_ptr->picture_fade = 0;
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value   = '\0';
    platform_clear_keys();
    platform_set_key_typed_mode(true);

    /* Anything but AfterCombat, so that the checks below can call the parts of
     * the overlay one at a time: character_load_pic only loads the picture in
     * that state, and loading a picture clears the keyboard - which is what the
     * DOS game did, and which would swallow the keys queued here. The last check
     * runs the whole sequence in the real state and copes with that. */
    gbl.game_state = GAME_STATE_COMBAT;

    /* ---- what the fight was worth ---- */

    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    worth = money_exp_worth(&pm.money);
    expected = (pm.field_13E * pm.hit_point_rolled + pm.field_13C + worth + 400)
               / gbl.area2_ptr->party_size;

    earned = aftercombat_calc_battle_exp();
    snprintf(detail, sizeof(detail),
             "%d each, %d items on the ground, %d gold in the pool", earned,
             gbl.ground_item_count, money_get(&gbl.pooled_money, MONEY_GOLD));
    check(earned == expected && gbl.byte_1AB14 && gbl.ground_item_count == 2 &&
          money_get(&gbl.pooled_money, MONEY_GOLD) == 100,
          "a fallen enemy is worth their hit points, their purse and the magic "
          "they carried", detail);

    ok = true;
    for (int i = 0; i < gbl.ground_item_count; i++) {
        if (gbl.ground_items[i].readied) {
            ok = false;
        }
    }
    check(ok && gbl.ground_item_count == 2 && pm.item_count == 2,
          "and their belongings are copied onto the ground, nothing readied",
          "the monster is freed straight afterwards, so the ground cannot hold "
          "pointers into it");

    /* An encounter marked as leaving nothing behind keeps its packs. */
    gbl_ground_items_clear();
    gbl.area2_ptr->field_5C6 = 1;
    (void)aftercombat_calc_battle_exp();
    check(gbl.ground_item_count == 0,
          "an encounter that leaves nothing behind takes its belongings with it",
          NULL);
    gbl.area2_ptr->field_5C6 = 0;

    gbl.combat_type = COMBAT_TYPE_DUEL;
    pa.hit_dice = 5;
    earned = aftercombat_calc_battle_exp();
    snprintf(detail, sizeof(detail), "%d hit dice, %d experience", pa.hit_dice,
             earned);
    check(earned == 500, "a duel is worth a flat hundred a hit die", detail);
    gbl.combat_type = COMBAT_TYPE_NORMAL;

    /* Nobody left to divide it between: the C# would have divided by zero. */
    gbl.party_animated_count = gbl.area2_ptr->party_size;
    earned = aftercombat_calc_battle_exp();
    check(earned == 0,
          "and a fight nobody came out of is worth nothing rather than dividing "
          "by zero", "the C# would have thrown here");
    gbl.party_animated_count = 0;

    /* ---- handing it out ---- */

    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    set_strength_dex(&pa, 17, 0, 12);            /* a fighter who lives by it */
    stat_value_load(&pb.stats.value[PSTAT_WIS], 17);
    pb.in_combat = true;
    aftercombat_add_exp(100);
    snprintf(detail, sizeof(detail), "the fighter %d, the cleric %d, the dead %d",
             pa.exp, pb.exp, pm.exp);
    check(pa.exp == 110 && pb.exp == 110 && pm.exp == 0,
          "a tenth again for the stat a class lives by, and nothing for anyone "
          "out of the fight", detail);

    pa.exp = pb.exp = 0;
    pa.cls = CLASS_MC_C_F;                       /* two classes share it */
    pb.cls = CLASS_MC_F_MU_T;                    /* three split it */
    aftercombat_add_exp(90);
    snprintf(detail, sizeof(detail), "two classes %d, three classes %d", pa.exp,
             pb.exp);
    check(pa.exp == 45 && pb.exp == 30,
          "two classes halve a share and three take a third each", detail);

    pa.exp = 0;
    pa.cls = CLASS_MC_MU_T;
    aftercombat_add_exp(90);
    snprintf(detail, sizeof(detail), "%d of 90", pa.exp);
    check(pa.exp == 90,
          "and a mage-thief takes a whole share, which is the original's own "
          "oversight", "MC_MU_T is the one multi-class id none of the tests "
          "match");

    /* ---- how the fight ended ---- */

    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    pb.health_status = STATUS_DYING;
    pb.in_combat     = false;
    effect_add_affect(false, 0, 10, AFFECT_SLEEP, &pa);
    effect_add_affect(false, 0, 10, AFFECT_BLESS, &pa);

    aftercombat_cleanup_players_state();
    snprintf(detail, sizeof(detail),
             "%s, %d not sharing, %s is %s, %d experience each",
             gbl.battle_won ? "won" : "lost", gbl.party_animated_count, pb.name,
             player_health_status_name(pb.health_status), gbl.exp_to_add);
    check(gbl.battle_won && !gbl.party_killed && !gbl.party_fled &&
          gbl.party_animated_count == 1 &&
          pb.health_status == STATUS_UNCONSCIOUS && gbl.exp_to_add > 0 &&
          pa.exp == gbl.exp_to_add,
          "one of ours left standing is a win, and the dying are only knocked "
          "out by it", detail);

    check(!affect_list_has(&pa.affects, AFFECT_SLEEP) &&
          affect_list_has(&pa.affects, AFFECT_BLESS),
          "the fight's own affects are taken off and the rest are left ticking",
          "sleep goes with the fight; a blessing is on the clock");

    /* A party that ran leaves behind whoever did not run with it. */
    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    pa.health_status = STATUS_RUNNING;
    pb.health_status = STATUS_DYING;
    pb.in_combat     = false;

    aftercombat_cleanup_players_state();
    snprintf(detail, sizeof(detail),
             "%s is %s, %s %s on the list, %d in the party, field_58E %02x",
             pa.name, player_health_status_name(pa.health_status), pb.name,
             gbl_team_index_of(&pb) >= 0 ? "still" : "no longer",
             gbl.area2_ptr->party_size, gbl.area2_ptr->field_58E);
    check(gbl.party_fled && !gbl.battle_won && !gbl.party_killed &&
          pa.health_status == STATUS_OKEY && pa.in_combat &&
          gbl_team_index_of(&pb) < 0 && gbl.area2_ptr->party_size == 1 &&
          gbl.area2_ptr->field_58E == 0x81,
          "a party that runs comes back, all but the one it left behind", detail);

    /* Nobody of ours standing at all. */
    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    pa.health_status = STATUS_DEAD;
    pb.health_status = STATUS_DEAD;
    pa.in_combat = pb.in_combat = false;
    pm.health_status = STATUS_OKEY;
    pm.in_combat     = true;

    aftercombat_cleanup_players_state();
    snprintf(detail, sizeof(detail), "%d left on the list, party size %d",
             gbl.team_count, gbl.area2_ptr->party_size);
    check(gbl.party_killed && !gbl.battle_won && gbl.team_count == 1 &&
          gbl.team_list[0] == &pm && gbl.area2_ptr->party_size == 0 &&
          gbl.party_animated_count == 2,
          "and a party nobody walked away from is emptied out, monsters and all",
          detail);

    /* ---- the other side goes home ---- */

    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    pb.combat_team = TEAM_ENEMY;                 /* a charmed party member */
    pb.in_combat   = true;

    aftercombat_deallocate_non_team_members();
    snprintf(detail, sizeof(detail),
             "%d left on the list, %d of them missed the fight, selected %s",
             gbl.team_count, gbl.area2_ptr->field_590,
             gbl.selected_player != NULL ? gbl.selected_player->name : "nobody");
    check(gbl.team_count == 1 && gbl.team_list[0] == &pa &&
          pa.actions == NULL && gbl.area2_ptr->field_590 == 1 &&
          gbl.byte_1AB14 && gbl.selected_player == &pa,
          "everyone who was not one of ours leaves, and those who stay hand back "
          "their action record", detail);

    /* ---- saying how it went ---- */

    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    gbl.byte_1AB14 = true;
    gbl.battle_won = true;
    gbl.exp_to_add = 261;

    platform_clear_keys();
    platform_push_key(0x1b);
    aftercombat_display_combat_results(gbl.exp_to_add);
    frame_stats(&nonzero, &colors);
    snprintf(detail, sizeof(detail), "%d pixels set, %d colours", nonzero,
             colors);
    check(nonzero > 1000 && colors >= 3,
          "\"The party has won.\" and what each character earned by it", detail);
    dump(out_dir, "after-combat.ppm");

    /* Running away forfeits the lot, which is done here rather than by whoever
     * ran. */
    money_set(&gbl.pooled_money, MONEY_GOLD, 100);
    (void)gbl_ground_item_add(&pm.items[0]);
    gbl.party_fled = true;

    platform_clear_keys();
    platform_push_key(0x1b);
    aftercombat_display_combat_results(261);
    snprintf(detail, sizeof(detail), "%d items and %s coin left",
             gbl.ground_item_count,
             money_any(&gbl.pooled_money) ? "some" : "no");
    check(gbl.ground_item_count == 0 && !money_any(&gbl.pooled_money),
          "a party that fled is told so, and leaves the treasure where it fell",
          detail);
    gbl.party_fled = false;

    /* ---- the list of what is lying there ---- */

    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    item_init(&dropped, ITEM_LONG_SWORD, 0, 0, 0, 0, 0, false, 0, false, 60, 1,
              30, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
    (void)gbl_ground_item_add(&dropped);

    {
        int index = 0;

        platform_clear_keys();
        platform_push_key('t');
        aftercombat_select_treasure(&index, &picked, &key);
        snprintf(detail, sizeof(detail), "'%c' picked the item worth %d",
                 key ? key : '?', picked != NULL ? picked->value : -1);
        check(key == 'T' && picked == &gbl.ground_items[0],
              "Take picks the entry the highlight is on", detail);

        platform_clear_keys();
        platform_push_key(0x1b);
        aftercombat_select_treasure(&index, &picked, &key);
        check(key == '\0' && picked == NULL,
              "and Escape picks nothing at all", NULL);
    }

    /* The sort cheat, which is the only thing that ever reorders the ground. */
    gbl_ground_items_clear();
    item_init(&dropped, ITEM_LONG_SWORD, 0, 0, 0, 0, 0, false, 0, false, 60, 1,
              30, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
    (void)gbl_ground_item_add(&dropped);
    item_init(&dropped, ITEM_DAGGER, 0, 0, 0, 0, 0, false, 0, false, 10, 1, 5,
              AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
    (void)gbl_ground_item_add(&dropped);

    cheats.sort_treasure = true;
    platform_clear_keys();
    platform_push_key(0x1b);
    {
        int index = 0;

        aftercombat_select_treasure(&index, &picked, &key);
    }
    cheats.sort_treasure = old_sort;
    snprintf(detail, sizeof(detail), "%d then %d", gbl.ground_items[0].value,
             gbl.ground_items[1].value);
    check(gbl.ground_items[0].value == 5 && gbl.ground_items[1].value == 30,
          "with the sort cheat on, the cheapest of the treasure comes first",
          detail);

    /* ---- taking it ---- */

    platform_clear_keys();
    platform_push_key('t');
    platform_push_key(0x1b);
    aftercombat_take_items_treasure();
    snprintf(detail, sizeof(detail), "%d left on the ground, %d in the pack",
             gbl.ground_item_count, pa.item_count);
    check(gbl.ground_item_count == 1 && pa.item_count == 1,
          "Take lifts a piece of the treasure off the ground and into the "
          "character's pack", detail);

    /* More than anyone can lift: canCarry allows 1500 coins past a character's
     * limit and no more, so this stays where it is and is offered again rather
     * than being taken off the ground and lost. */
    gbl.ground_items[0].weight = 20000;
    platform_clear_keys();
    platform_push_key('t');
    platform_push_key(0x1b);
    aftercombat_take_items_treasure();
    snprintf(detail, sizeof(detail), "%d items still there",
             gbl.ground_item_count);
    check(gbl.ground_item_count == 1 && pa.item_count == 1,
          "an item nobody can carry is left where it is", detail);

    items_present = true;
    money_present = false;
    platform_clear_keys();
    platform_push_key(0x1b);
    aftercombat_take_treasure(&items_present, &money_present);
    check(gbl.ground_item_count == 1,
          "with only items on the ground there is nothing to choose between",
          "the list is opened without asking");

    items_present = false;
    money_present = true;
    money_set(&gbl.pooled_money, MONEY_GOLD, 100);
    platform_clear_keys();
    platform_push_key(0x1b);
    aftercombat_take_treasure(&items_present, &money_present);
    check(money_get(&gbl.pooled_money, MONEY_GOLD) == 100,
          "and with only coin the pool is opened, and backing out leaves it "
          "alone", NULL);

    items_present = true;
    money_present = true;
    platform_clear_keys();
    platform_push_key('e');
    aftercombat_take_treasure(&items_present, &money_present);
    check(gbl.ground_item_count == 1 &&
          money_get(&gbl.pooled_money, MONEY_GOLD) == 100,
          "with both there, \"Money Items Exit\" asks which", NULL);

    /* ---- the treasure screen ---- */

    /* Detect magic on the list is what adds "Detect" to the prompt, and 'D' then
     * needs the spell resolving that ovr023 has not been translated for. Exit
     * with treasure still lying there asks "~Yes ~No" whether to go back for it,
     * and No is what closes the screen. */
    spell_list_add_learnt(&pa.spell_list, 5);
    pa.in_combat = true;
    platform_clear_keys();
    platform_push_key('d');
    platform_push_key('e');
    platform_push_key('n');
    aftercombat_distribute_combat_treasure();
    snprintf(detail, sizeof(detail), "%d items and %d gold still on the ground",
             gbl.ground_item_count, money_get(&gbl.pooled_money, MONEY_GOLD));
    check(gbl.ground_item_count == 1 &&
          money_get(&gbl.pooled_money, MONEY_GOLD) == 100,
          "the treasure screen leaves what it was not asked to take", detail);
    dump(out_dir, "after-combat-treasure.ppm");

    gbl_ground_items_clear();
    money_clear_all(&gbl.pooled_money);
    platform_clear_keys();
    platform_push_key('e');
    aftercombat_distribute_combat_treasure();
    check(gbl.ground_item_count == 0,
          "and with bare ground Exit is not argued with", NULL);

    /* ---- the NPCs' cut ---- */

    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    pb.control_morale = CONTROL_NPC_BASE + 5;
    pb.npc_treasure_share_count = 1;
    money_set(&gbl.pooled_money, MONEY_GOLD, 100);
    money_set(&gbl.pooled_money, MONEY_GEMS, 3);

    platform_clear_keys();
    platform_push_key(0x1b);
    aftercombat_distribute_npc_treasure();
    snprintf(detail, sizeof(detail), "%d gold and %d gems left in the pool",
             money_get(&gbl.pooled_money, MONEY_GOLD),
             money_get(&gbl.pooled_money, MONEY_GEMS));
    check(money_get(&gbl.pooled_money, MONEY_GOLD) == 0 &&
          money_get(&gbl.pooled_money, MONEY_GEMS) == 3,
          "an NPC's share is worked out with integer division, so they take all "
          "of the coin and none of the gems", detail);

    /* All of them NPCs, and the division comes out at one instead of nothing. */
    pa.control_morale = CONTROL_NPC_BASE + 5;
    pa.npc_treasure_share_count = 1;
    gbl_team_remove_at(gbl_team_index_of(&pm));
    money_set(&gbl.pooled_money, MONEY_GOLD, 100);

    platform_clear_keys();
    platform_push_key(0x1b);
    platform_push_key(0x1b);
    aftercombat_distribute_npc_treasure();
    snprintf(detail, sizeof(detail), "%d gold left in the pool",
             money_get(&gbl.pooled_money, MONEY_GOLD));
    check(money_get(&gbl.pooled_money, MONEY_GOLD) == 100,
          "a party of nothing but NPCs divides the pool by itself and leaves it "
          "where it is", detail);

    /* ---- and the whole of it ---- */

    /* In the demo nobody is settled up and no treasure is handed out: the state
     * moves on and the other side goes home, and that is all. */
    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    gbl.in_demo = true;
    gbl.game_state = GAME_STATE_COMBAT;

    aftercombat_exp_and_treasure();
    snprintf(detail, sizeof(detail), "%d left on the list, %d experience each",
             gbl.team_count, gbl.exp_to_add);
    check(gbl.game_state == GAME_STATE_AFTER_COMBAT && gbl.team_count == 2 &&
          gbl_team_index_of(&pm) < 0 && gbl.exp_to_add == 0,
          "in the demo the fight is simply over: nobody is settled up and no "
          "treasure is handed out", detail);
    gbl.in_demo = false;

    after_combat_scene(&pa, &aa, &pb, &ab, &pm, &am);
    gbl.game_state = GAME_STATE_COMBAT;
    gbl.delay_between_characters = false;
    gbl.area2_ptr->field_6E0 = 1;
    gbl.area2_ptr->field_6E2 = 2;
    gbl.area2_ptr->field_6E4 = 3;
    gbl.area2_ptr->is_duel   = true;

    /* The kobold dies with empty hands and empty pockets, so that the whole
     * sequence can be driven by one key. It cannot be driven by one key with
     * treasure on the ground: backing out of the item list leaves it lying there,
     * which asks "~Yes ~No" whether to go back for it, and that prompt - like the
     * original's - only takes a Y or an N, while the item list above it reads N as
     * "Next". The treasure screen with something in it is the test above this one,
     * which calls it directly and can feed it the keys in order. */
    money_clear_all(&pm.money);
    while (pm.item_count > 0) {
        player_item_remove(&pm, pm.item_count - 1);
    }

    /* Escape holds down for the whole sequence, which is what it takes: the
     * treasure screen loads the after-combat picture on its way in, that clears
     * the keyboard of everything queued behind it, and with a picture animating
     * the prompts cannot time out either - their own clock is the one the frames
     * are turned over on. A key that is held down comes back after the clear. */
    platform_clear_keys();
    platform_set_held_key(0x1b);

    aftercombat_exp_and_treasure();

    platform_set_held_key(0);

    snprintf(detail, sizeof(detail),
             "%d experience each, %s is on %d, %d items left on the ground",
             gbl.exp_to_add, pa.name, pa.exp, gbl.ground_item_count);
    check(gbl.game_state == GAME_STATE_AFTER_COMBAT && gbl.exp_to_add > 0 &&
          pa.exp == gbl.exp_to_add && gbl.ground_item_count == 0 &&
          gbl_team_index_of(&pm) < 0,
          "a fight won, from the last blow to the empty ground", detail);

    snprintf(detail, sizeof(detail), "%d %d %d, duel %s, field_5C6 %d",
             gbl.area2_ptr->field_6E0, gbl.area2_ptr->field_6E2,
             gbl.area2_ptr->field_6E4,
             gbl.area2_ptr->is_duel ? "still set" : "cleared",
             gbl.area2_ptr->field_5C6);
    check(gbl.delay_between_characters && gbl.area2_ptr->field_6E0 == 0 &&
          gbl.area2_ptr->field_6E2 == 0 && gbl.area2_ptr->field_6E4 == 0 &&
          gbl.area2_ptr->field_5C6 == 0 && !gbl.area2_ptr->is_duel,
          "and the encounter's own state is put back for the next one", detail);

    platform_clear_keys();
    platform_set_key_typed_mode(false);

    gbl.team_count      = 0;
    gbl.selected_player = old_selected;
    gbl.item_ptr        = old_item_ptr;
    gbl_ground_items_clear();
    gbl.pooled_money    = old_pool;
    gbl.combat_type     = old_combat_type;
    gbl.party_killed    = old_killed;
    gbl.party_fled      = old_fled;
    gbl.battle_won      = old_won;
    gbl.byte_1AB14      = old_flag;
    gbl.party_animated_count = old_animated;
    gbl.exp_to_add      = old_exp;
    gbl.text_x_col      = old_text_x;
    gbl.text_y_col      = old_text_y;
    gbl.display_input_seconds_to_wait = old_wait;
    gbl.display_input_timeout_value   = old_timeout;
    gbl.delay_between_characters = old_delay;
    gbl.in_demo         = old_in_demo;
    gbl.game_state      = old_state;
    gbl.game_speed_var  = old_speed;
    gbl.area_ptr->picture_fade = old_fade;
    gbl.area2_ptr->party_size = old_party_size;
    gbl.area2_ptr->is_duel    = old_is_duel;
    gbl.area2_ptr->field_58E  = old_field_58E;
    gbl.area2_ptr->field_590  = old_field_590;
    gbl.area2_ptr->field_5C6  = old_field_5C6;
    gbl.area2_ptr->field_6E0  = old_field_6E0;
    gbl.area2_ptr->field_6E2  = old_field_6E2;
    gbl.area2_ptr->field_6E4  = old_field_6E4;

    printf("\n");
}

/* ------------------------------------------------------- money and treasure */

/* The name a rolled item is offered under, and the name it carries once it has
 * been identified. */
static const char *item_name_now(const Item *item, char *dst, size_t dst_size)
{
    return item_generate_name(item, item->hidden_names_flag, dst, dst_size);
}

static const char *item_name_known(const Item *item, char *dst, size_t dst_size)
{
    return item_generate_name(item, 0, dst, dst_size);
}

static void check_treasure(const char *out_dir)
{
    Player p1, p2, npc;
    char detail[240];
    char name[ITEM_NAME_GEN_MAX];
    char name2[ITEM_NAME_GEN_MAX];
    int old_speed = gbl.game_speed_var;
    GameState old_state = gbl.game_state;
    Item item;

    printf("money and treasure\n");

    /* Nothing here should wait for an animation, and the prompts want a picture
     * that is not fading and no city cursor blinking over them. */
    gbl.game_speed_var = 0;
    gbl.game_state     = GAME_STATE_DUNGEON_MAP;
    if (gbl.area_ptr != NULL) {
        gbl.area_ptr->picture_fade = 0;
    }

    player_init(&p1);
    player_init(&p2);
    player_init(&npc);
    p1.field_125 = p2.field_125 = npc.field_125 = 1;
    set_strength_dex(&p1, 10, 0, 10);
    set_strength_dex(&p2, 10, 0, 10);
    set_strength_dex(&npc, 10, 0, 10);
    npc.control_morale = CONTROL_NPC_BASE;
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    snprintf(p2.name, sizeof(p2.name), "%s", "Dragonbait");
    snprintf(npc.name, sizeof(npc.name), "%s", "Olive");

    money_clear_all(&gbl.pooled_money);
    gbl_ground_items_clear();
    gbl.team_count = 0;

    /* --- what a character can carry. 1500 coins, give or take the strength
     * allowance, which is what makes a weak character a poor pack mule. */
    {
        static const int str[4] = { 3, 10, 12, 16 };
        static const int want[4] = { 1150, 1500, 1600, 1850 };
        bool ok = true;
        int got[4];

        for (int i = 0; i < 4; i++) {
            set_strength_dex(&p1, str[i], 0, 10);
            got[i] = treasure_max_load(&p1);
            ok = ok && got[i] == want[i];
        }

        snprintf(detail, sizeof(detail),
                 "strength 3 carries %d, 10 carries %d, 16 carries %d",
                 got[0], got[1], got[3]);
        check(ok, "what a character can carry in coin", detail);
    }

    set_strength_dex(&p1, 10, 0, 10);

    /* --- and how much of that is left. */
    {
        int capacity_full = -1, capacity_room = -1, capacity_over = -1;
        bool over_full, over_room, over_over;

        p1.weight = 1400;
        over_full = treasure_will_overload(&capacity_full, 200, &p1);
        over_room = treasure_will_overload(&capacity_room, 50, &p1);

        p1.weight = 1600;
        over_over = treasure_will_overload(&capacity_over, 10, &p1);

        snprintf(detail, sizeof(detail), "200 more leaves %d, 50 more leaves %d",
                 capacity_full, capacity_room);
        check(over_full && capacity_full == 100 &&
              !over_room && capacity_room == 0,
              "how much more coin will fit", detail);

        /* The original worked the room out by subtraction and never tested the
         * sign, so an already-overloaded character has a negative capacity - and
         * the callers hand that straight to AddCoins. */
        snprintf(detail, sizeof(detail), "100 over the limit reports %d",
                 capacity_over);
        check(over_over && capacity_over == -100,
              "one already overloaded has less than no room", detail);
    }

    /* --- being paid. Anything that cannot be lifted goes into the pool. */
    {
        int platinum_paid, weight_paid, pool_paid;
        int platinum_over, weight_over, pool_over;

        p1.weight = 0;
        money_clear_all(&p1.money);
        money_clear_all(&gbl.pooled_money);
        gbl.selected_player = &p1;

        treasure_add_player_gold(500);
        platinum_paid = money_get(&p1.money, MONEY_PLATINUM);
        weight_paid   = p1.weight;
        pool_paid     = money_get(&gbl.pooled_money, MONEY_PLATINUM);

        snprintf(detail, sizeof(detail), "%d platinum, weighing %d, %d pooled",
                 platinum_paid, weight_paid, pool_paid);
        check(platinum_paid == 500 && weight_paid == 500 && pool_paid == 0,
              "a sale is paid in platinum", detail);

        treasure_add_player_gold(1200);
        snprintf(detail, sizeof(detail), "%d platinum, weighing %d, %d pooled",
                 money_get(&p1.money, MONEY_PLATINUM), p1.weight,
                 money_get(&gbl.pooled_money, MONEY_PLATINUM));
        check(money_get(&p1.money, MONEY_PLATINUM) == 1500 &&
              p1.weight == 1500 &&
              money_get(&gbl.pooled_money, MONEY_PLATINUM) == 200,
              "what will not fit in a purse goes in the pool", detail);

        /* And the negative capacity above, played out: an overloaded character
         * pays for the sale instead of being paid for it. */
        p1.weight = 1600;
        money_set(&p1.money, MONEY_PLATINUM, 1000);
        money_clear_all(&gbl.pooled_money);

        treasure_add_player_gold(10);
        platinum_over = money_get(&p1.money, MONEY_PLATINUM);
        weight_over   = p1.weight;
        pool_over     = money_get(&gbl.pooled_money, MONEY_PLATINUM);

        snprintf(detail, sizeof(detail),
                 "1000 platinum became %d, weight %d, %d pooled",
                 platinum_over, weight_over, pool_over);
        check(platinum_over == 900 && weight_over == 1500 && pool_over == 110,
              "an overloaded character pays for their own sale", detail);
    }

    /* --- typing a number in. Scripted keys are handed over one at a time from
     * here on: GetInputKey throws away whatever is queued behind the key it
     * returns, so a whole line pushed at once would collapse to its last key. */
    platform_set_key_typed_mode(true);
    {
        i16 typed, clamped, rubbed_out, escaped, empty;

        platform_clear_keys();
        platform_push_key('1');
        platform_push_key('2');
        platform_push_key('3');
        platform_push_key(0x0d);
        typed = treasure_ask_number_value(10, "How much Gold will you take? ",
                                          500);

        platform_push_key('9');
        platform_push_key('9');
        platform_push_key(0x0d);
        clamped = treasure_ask_number_value(10, "How much Gold ", 50);

        platform_push_key('1');
        platform_push_key('2');
        platform_push_key(8);
        platform_push_key('7');
        platform_push_key(0x0d);
        rubbed_out = treasure_ask_number_value(10, "How much Gold ", 500);

        platform_push_key('4');
        platform_push_key('2');
        platform_push_key(0x1b);
        escaped = treasure_ask_number_value(10, "How much Gold ", 500);

        platform_push_key(0x0d);
        empty = treasure_ask_number_value(10, "How much Gold ", 500);

        snprintf(detail, sizeof(detail),
                 "123 -> %d, 99 of 50 -> %d, 12<-7 -> %d, escape -> %d, "
                 "return -> %d",
                 typed, clamped, rubbed_out, escaped, empty);
        check(typed == 123 && clamped == 50 && rubbed_out == 17 &&
              escaped == 0 && empty == 0,
              "typing an amount in, digit by digit", detail);
    }

    /* --- handing coin to another character. */
    {
        p1.weight = 0;
        p2.weight = 300;
        money_clear_all(&p1.money);
        money_clear_all(&p2.money);
        money_set(&p2.money, MONEY_GOLD, 300);

        treasure_trade_money(MONEY_GOLD, 100, &p1, &p2);
        snprintf(detail, sizeof(detail), "%d gold at %d, %d gold at %d",
                 money_get(&p1.money, MONEY_GOLD), p1.weight,
                 money_get(&p2.money, MONEY_GOLD), p2.weight);
        check(money_get(&p1.money, MONEY_GOLD) == 100 && p1.weight == 100 &&
              money_get(&p2.money, MONEY_GOLD) == 200 && p2.weight == 200,
              "coin handed over weighs on whoever holds it", detail);

        p1.weight = 1450;
        treasure_trade_money(MONEY_GOLD, 100, &p1, &p2);
        snprintf(detail, sizeof(detail), "%d gold left with the giver",
                 money_get(&p2.money, MONEY_GOLD));
        check(money_get(&p1.money, MONEY_GOLD) == 100 &&
              money_get(&p2.money, MONEY_GOLD) == 200,
              "and stays put when it will not fit", detail);
    }

    /* --- pooling and sharing out. The party pools; the hirelings do not. */
    {
        gbl.team_count = 0;
        gbl_team_add(&p1);
        gbl_team_add(&p2);
        gbl_team_add(&npc);

        money_clear_all(&gbl.pooled_money);
        money_clear_all(&p1.money);
        money_clear_all(&p2.money);
        money_clear_all(&npc.money);
        money_set(&p1.money, MONEY_GOLD, 11);
        money_set(&npc.money, MONEY_GOLD, 7);
        p1.weight = 11;
        p2.weight = 0;
        npc.weight = 7;

        check(treasure_party_count() == 2, "the hirelings are not the party",
              "two of the three");

        treasure_pool_money();
        snprintf(detail, sizeof(detail),
                 "%d gold pooled, %d left on Alias, %d kept by Olive",
                 money_get(&gbl.pooled_money, MONEY_GOLD),
                 money_get(&p1.money, MONEY_GOLD),
                 money_get(&npc.money, MONEY_GOLD));
        check(money_get(&gbl.pooled_money, MONEY_GOLD) == 11 &&
              money_get(&p1.money, MONEY_GOLD) == 0 && p1.weight == 0 &&
              money_get(&npc.money, MONEY_GOLD) == 7,
              "pooling empties the party's purses and nobody else's", detail);

        treasure_share_pooled();
        snprintf(detail, sizeof(detail),
                 "%d to Alias, %d to Dragonbait, %d left in the pool",
                 money_get(&p1.money, MONEY_GOLD),
                 money_get(&p2.money, MONEY_GOLD),
                 money_get(&gbl.pooled_money, MONEY_GOLD));
        check(money_get(&p1.money, MONEY_GOLD) == 6 && p1.weight == 6 &&
              money_get(&p2.money, MONEY_GOLD) == 5 && p2.weight == 5 &&
              money_get(&npc.money, MONEY_GOLD) == 7 &&
              money_get(&gbl.pooled_money, MONEY_GOLD) == 0,
              "the odd coin goes to the first with room for it", detail);

        /* With both party members loaded to the limit the shares are all
         * leftovers, and the leftovers pass has no hireling test at all: the
         * money ends up with the NPC nobody meant to pay. */
        money_clear_all(&p1.money);
        money_clear_all(&p2.money);
        money_clear_all(&npc.money);
        money_set(&npc.money, MONEY_GOLD, 7);
        p1.weight  = 1500;
        p2.weight  = 1500;
        npc.weight = 7;
        money_set(&gbl.pooled_money, MONEY_GOLD, 100);

        treasure_share_pooled();
        snprintf(detail, sizeof(detail),
                 "Alias %d, Dragonbait %d, Olive %d, pool %d",
                 money_get(&p1.money, MONEY_GOLD),
                 money_get(&p2.money, MONEY_GOLD),
                 money_get(&npc.money, MONEY_GOLD),
                 money_get(&gbl.pooled_money, MONEY_GOLD));
        check(money_get(&p1.money, MONEY_GOLD) == 0 &&
              money_get(&p2.money, MONEY_GOLD) == 0 &&
              money_get(&npc.money, MONEY_GOLD) == 107 &&
              money_get(&gbl.pooled_money, MONEY_GOLD) == 0,
              "what the party cannot lift is left to the hireling", detail);

        /* A share-out with nobody to share to divided by zero in the C#. */
        gbl.team_count = 0;
        money_set(&gbl.pooled_money, MONEY_GOLD, 11);
        treasure_share_pooled();
        check(money_get(&gbl.pooled_money, MONEY_GOLD) == 11,
              "a share-out with nobody there leaves the pool alone",
              "and no longer divides by zero");
    }

    /* --- dropping and picking coin up. */
    {
        money_clear_all(&p1.money);
        money_clear_all(&gbl.pooled_money);
        money_set(&p1.money, MONEY_GOLD, 50);
        p1.weight = 50;

        gbl.game_state = GAME_STATE_DUNGEON_MAP;
        treasure_drop_coins(MONEY_GOLD, 20, &p1);
        snprintf(detail, sizeof(detail), "%d gold left, %d in the pool",
                 money_get(&p1.money, MONEY_GOLD),
                 money_get(&gbl.pooled_money, MONEY_GOLD));
        check(money_get(&p1.money, MONEY_GOLD) == 30 && p1.weight == 30 &&
              money_get(&gbl.pooled_money, MONEY_GOLD) == 0,
              "coin dropped on the road is gone", detail);

        gbl.game_state = GAME_STATE_AFTER_COMBAT;
        treasure_drop_coins(MONEY_GOLD, 20, &p1);
        check(money_get(&p1.money, MONEY_GOLD) == 10 && p1.weight == 10 &&
              money_get(&gbl.pooled_money, MONEY_GOLD) == 20,
              "coin dropped after a fight goes in the pool", "20 gold");
        gbl.game_state = GAME_STATE_DUNGEON_MAP;

        money_clear_all(&p1.money);
        p1.weight = 0;
        treasure_pickup_coins(MONEY_GOLD, 50, &p1);
        snprintf(detail, sizeof(detail), "asked for 50 of 20, took %d",
                 money_get(&p1.money, MONEY_GOLD));
        check(money_get(&p1.money, MONEY_GOLD) == 20 && p1.weight == 20 &&
              money_get(&gbl.pooled_money, MONEY_GOLD) == 0,
              "nobody takes more out of the pool than is in it", detail);

        money_set(&gbl.pooled_money, MONEY_GOLD, 20);
        p1.weight = 1495;
        treasure_pickup_coins(MONEY_GOLD, 10, &p1);
        check(money_get(&p1.money, MONEY_GOLD) == 20 &&
              money_get(&gbl.pooled_money, MONEY_GOLD) == 20,
              "and nobody picks up what they cannot carry", "1495 of 1500");
    }

    /* --- reading a coin kind off the menu line the player picked. */
    {
        static const struct { const char *line; int kind; const char *word; }
        lines[] = {
            { "Gold 250",      MONEY_GOLD,     "Gold "     },
            { "Gems 3",        MONEY_GEMS,     "Gems "     },
            { "  Platinum 4",  MONEY_PLATINUM, "Platinum " },
            { "Jewelry 1",     MONEY_JEWELRY,  "Jewelry "  },
            { "Copper 5",      MONEY_COPPER,   "Copper "   },
            { "Silver 5",      MONEY_SILVER,   "Silver "   },
            { "Electrum 5",    MONEY_ELECTRUM, "Electrum " },
            { "Fish",          MONEY_KINDS,    ""          },
            { "   ",           MONEY_KINDS,    ""          }
        };
        bool ok = true;
        int fish = MONEY_COPPER;

        for (size_t i = 0; i < COAB_ARRAY_LEN(lines); i++) {
            char word[32];
            int kind = treasure_money_index_from_string(word, sizeof(word),
                                                       lines[i].line);

            if (kind != lines[i].kind || strcmp(word, lines[i].word) != 0) {
                ok = false;
                snprintf(detail, sizeof(detail), "\"%s\" read as %d \"%s\"",
                         lines[i].line, kind, word);
            }
            if (i == 7) {
                fish = kind;
            }
        }

        if (ok) {
            snprintf(detail, sizeof(detail),
                     "and an unknown line reads as coin kind %d, of which the "
                     "pool holds %d", fish,
                     money_get(&gbl.pooled_money, (MoneyKind)fish));
        }
        check(ok, "which coin a menu line names", detail);
    }

    /* --- taking coin out of the pool, menu and all. */
    {
        int nonzero = 0, colors = 0;

        money_clear_all(&p1.money);
        money_clear_all(&gbl.pooled_money);
        money_set(&gbl.pooled_money, MONEY_GOLD, 11);
        p1.weight = 0;
        gbl.selected_player = &p1;

        /* An unanswered prompt leaves the list, which is the backstop if any of
         * the keys below stops being the one the menu wants. */
        gbl.display_input_seconds_to_wait = 1;
        gbl.display_input_timeout_value = '\0';

        clear_screen_raw();
        platform_clear_keys();
        /* "Select" takes the highlighted line - the Gold one, the only one
         * there is - and then the amount is typed in. */
        platform_push_key('s');
        platform_push_key('1');
        platform_push_key('1');
        platform_push_key(0x0d);
        treasure_take_pool_money();

        frame_stats(&nonzero, &colors);
        dump(out_dir, "treasure-pool.ppm");
        snprintf(detail, sizeof(detail),
                 "%d gold taken, %d left in the pool, %d px drawn",
                 money_get(&p1.money, MONEY_GOLD),
                 money_get(&gbl.pooled_money, MONEY_GOLD), nonzero);
        check(money_get(&p1.money, MONEY_GOLD) == 11 && p1.weight == 11 &&
              !money_any(&gbl.pooled_money) && nonzero > 100,
              "emptying the pool one coin kind at a time", detail);
    }

    /* --- whether there is anything here worth stopping for. */
    {
        bool items = true, coin = true;

        money_clear_all(&gbl.pooled_money);
        gbl_ground_items_clear();
        treasure_on_ground(&items, &coin);
        check(!items && !coin, "an empty floor is empty", NULL);

        money_set(&gbl.pooled_money, MONEY_GOLD, 1);
        item_init(&item, ITEM_LONG_SWORD, 0, 0, ITEM_LONG_SWORD, 1, 0, false, 6,
                  false, 60, 0, 2000, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
        gbl_ground_item_add(&item);

        treasure_on_ground(&items, &coin);
        snprintf(detail, sizeof(detail), "%d item on the ground",
                 gbl.ground_item_count);
        check(items && coin, "a sword and a coin are both noticed", detail);

        gbl_ground_items_clear();
        money_clear_all(&gbl.pooled_money);
    }

    /* --- the magical bonus every rolled weapon carries. */
    {
        int bonus[4] = { 0, 0, 0, 0 };
        bool ok = true;

        rnd_seed(0x22);
        for (int i = 0; i < 400; i++) {
            i8 got = treasure_random_bonus();

            if (got >= 0 && got <= 3) {
                bonus[got]++;
            } else {
                ok = false;
            }
        }

        snprintf(detail, sizeof(detail), "%d +1, %d +2, %d neither",
                 bonus[1], bonus[2], bonus[0] + bonus[3]);
        check(ok && bonus[0] == 0 && bonus[3] == 0 &&
              bonus[1] > 220 && bonus[1] < 340 && bonus[2] > 60,
              "found weapons are magical, seven in ten of them +1", detail);
    }

    /* --- rolling up a hoard, one item at a time. */
    rnd_seed(0x5a007);
    {
        treasure_create_item(&item, ITEM_LONG_SWORD);
        item_name_now(&item, name, sizeof(name));
        item_name_known(&item, name2, sizeof(name2));
        snprintf(detail, sizeof(detail),
                 "\"%s\", once identified \"%s\", %d coins, worth %d",
                 name, name2, item.weight, item.value);
        check(strcmp(name, "Long Sword") == 0 &&
              (item.plus == 1 || item.plus == 2) &&
              item.value == (i16)(item.plus * 2000) && item.weight == 60 &&
              item.count == 0 && item.hidden_names_flag == 6,
              "a long sword hides its bonus until it is identified", detail);

        treasure_create_item(&item, ITEM_PLATE_MAIL);
        item_name_now(&item, name, sizeof(name));
        item_name_known(&item, name2, sizeof(name2));
        snprintf(detail, sizeof(detail), "\"%s\" is really \"%s\", worth %d",
                 name, name2, item.value);
        check(strcmp(name, "Plate Mail") == 0 && item.hidden_names_flag == 4 &&
              item.value == (i16)(item.plus * 5000) && item.weight == 450,
              "armour is named for what it is made of", detail);

        treasure_create_item(&item, ITEM_BRACERS);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "\"%s\", plus %d, worth %d", name,
                 item.plus, item.value);
        check((item.plus == 4 || item.plus == 6) &&
              item.namenum1 == (item.plus == 4 ? 0xdd : 0xde) &&
              item.value == (i16)(item.plus * 3000) && item.weight == 10,
              "bracers are named for the armour class they give", detail);

        treasure_create_item(&item, ITEM_DART);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "%d of them, \"%s\", %d coins each",
                 item.count, name, item.weight);
        check(item.count == 5 && item.weight == 0x19 &&
              strstr(name, "Darts") == name,
              "darts turn up five at a time", detail);

        treasure_create_item(&item, ITEM_ARROW);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "%d of them, \"%s\", worth %d",
                 item.count, name, item.value);
        check(item.count == 10 && strstr(name, "Arrows") == name &&
              item.value == (i16)(item.plus * 150),
              "and arrows ten at a time", detail);

        treasure_create_item(&item, ITEM_RING_OF_PROT);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "\"%s\", %d coin", name, item.weight);
        check(strstr(name, "Ring Of Prot.") == name && item.weight == 1,
              "a ring of protection weighs nothing to speak of", detail);
    }

    /* Scrolls, which are rolled rather than copied out of the table: one to
     * three spells, each worth 300 gold a level. */
    {
        int spells = 0;
        bool ok = true;

        treasure_create_item(&item, ITEM_MU_SCROLL);
        item_name_known(&item, name, sizeof(name));

        for (int i = 1; i <= 3; i++) {
            if (item_affect(&item, i) != AFFECT_NONE) {
                spells++;
            }
        }

        ok = item.namenum3 == 0xd1 && item.namenum2 == (int)(spells + 0xd1) &&
             item.weight == 0x19 && spells >= 1 && spells <= 3 &&
             item.value >= 300 && item.value <= spells * 1500 &&
             item.value % 300 == 0;

        snprintf(detail, sizeof(detail), "\"%s\" holds %d spell(s), worth %d",
                 name, spells, item.value);
        check(ok, "a magic user's scroll is worth what is written on it",
              detail);

        treasure_create_item(&item, ITEM_CLRC_SCROLL);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "\"%s\"", name);
        check(item.namenum3 == 0xd0 && strstr(name, "Clrc Scroll") == name,
              "and a priest's scroll says whose it is", detail);
    }

    /* The seven ready-made items, which are copied whole. */
    {
        bool potions_ok = true;
        int healing = 0, extra_healing = 0;

        for (int i = 0; i < 40; i++) {
            treasure_create_item(&item, ITEM_POTION);
            item_name_known(&item, name, sizeof(name));

            if (strcmp(name, "Potion of Healing") == 0 && item.value == 400) {
                healing++;
            } else if (strcmp(name, "Potion Extra Healing") == 0 &&
                       item.value == 800) {
                extra_healing++;
            } else {
                potions_ok = false;
                snprintf(detail, sizeof(detail), "\"%s\" worth %d", name,
                         item.value);
            }
        }

        item_name_now(&item, name2, sizeof(name2));
        if (potions_ok) {
            snprintf(detail, sizeof(detail),
                     "%d healing, %d extra healing, all of them \"%s\" "
                     "unidentified", healing, extra_healing, name2);
        }
        check(potions_ok && healing > 0 && extra_healing > 0 &&
              strcmp(name2, "Potion") == 0 && item.weight == 1 &&
              item.plus == 1 && item.plus_save == 1,
              "a rolled potion is one of the two healing potions", detail);

        treasure_create_item(&item, ITEM_GAUNTLETS);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "\"%s\", %d coins, worth %d", name,
                 item.weight, item.value);
        check(strcmp(name, "Gauntlets of Ogre Power") == 0 &&
              item.weight == 10 && item.value == 15000 &&
              item.affect_2 == 0x26 && item.affect_3 == 0x83,
              "the gauntlets come with their affects attached", detail);

        treasure_create_item(&item, ITEM_WAND_A);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "\"%s\", worth %d", name, item.value);
        check(strcmp(name, "Wand of Magic Missiles") == 0 &&
              item.value == 11000 && item.affect_1 == 0x1e,
              "there is only one wand in the game", detail);

        /* Row 1 of the table is a potion of giant strength, and a cloak is
         * rolled as row 1. That is what the original's table said, so that is
         * what a cloak turns out to be. */
        treasure_create_item(&item, ITEM_CLOAK);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "\"%s\", worth %d", name, item.value);
        check(strcmp(name, "Potion of Giant Strength") == 0 &&
              item.value == 1100,
              "a rolled cloak is not a cloak at all", detail);
    }

    /* A javelin is a plain magical javelin four times in five, and a javelin of
     * lightning the fifth. */
    {
        int lightning = 0, plain = 0;
        bool ok = true;

        for (int i = 0; i < 200; i++) {
            treasure_create_item(&item, ITEM_JAVELIN);
            item_name_known(&item, name, sizeof(name));

            if (strcmp(name, "Javelin of Lightning") == 0) {
                lightning++;
                ok = ok && item.value == 3000 && item.weight == 20;
            } else if (strstr(name, "Javelin") == name) {
                plain++;
                ok = ok && item.value == (i16)(item.plus * 2000) &&
                     item.weight == 20;
            } else {
                ok = false;
            }
        }

        snprintf(detail, sizeof(detail), "%d of lightning, %d plain",
                 lightning, plain);
        check(ok && lightning > 15 && lightning < 65 && plain > 100,
              "one javelin in five is a javelin of lightning", detail);
    }

    /* Anything the roller does not know about comes back with nothing but its
     * type filled in, which is what the C# returned too. */
    {
        treasure_create_item(&item, ITEM_NECKLACE);
        item_name_known(&item, name, sizeof(name));
        snprintf(detail, sizeof(detail), "type %d, \"%s\", worth %d",
                 item.type, name, item.value);
        check(item.type == ITEM_NECKLACE && item.namenum3 == 0 &&
              item.value == 0 && item.weight == 0 && name[0] == '\0',
              "an item with no rules for it is left blank", detail);
    }

    /* --- appraising gems and jewelry. */
    {
        int platinum_before;
        int nonzero = 0, colors = 0;

        gbl.selected_player = &p1;
        gbl.team_count = 0;
        gbl_team_add(&p1);

        p1.item_count = 0;
        money_clear_all(&p1.money);
        money_clear_all(&gbl.pooled_money);
        character_recalc_values(&p1);

        check(!treasure_appraise_gems_jewels(),
              "there is nothing to appraise with an empty purse",
              "no gems, no jewelry");

        /* One gem, sold. Its worth is rolled, so the sale price is one of the
         * six the table can produce, and it is paid in platinum. */
        money_set(&p1.money, MONEY_GEMS, 1);
        character_recalc_values(&p1);
        platinum_before = money_get(&p1.money, MONEY_PLATINUM);

        gbl.display_input_seconds_to_wait = 2;
        gbl.display_input_timeout_value = 'E';

        clear_screen_raw();
        platform_clear_keys();
        platform_push_key('g');
        platform_push_key('s');
        {
            bool redrawn = treasure_appraise_gems_jewels();
            int paid = money_get(&p1.money, MONEY_PLATINUM) - platinum_before;

            frame_stats(&nonzero, &colors);
            dump(out_dir, "treasure-appraise.ppm");
            snprintf(detail, sizeof(detail),
                     "%d platinum for it, %d gems left, %d px drawn",
                     paid, money_get(&p1.money, MONEY_GEMS), nonzero);
            check(redrawn && money_get(&p1.money, MONEY_GEMS) == 0 &&
                  (paid == 2 || paid == 10 || paid == 20 || paid == 100 ||
                   paid == 200 || paid == 1000) &&
                  p1.item_count == 0 && nonzero > 100,
                  "a gem sells for a fifth of what it is worth", detail);
        }

        /* One piece of jewelry, kept: it becomes an item worth what it was
         * appraised at. */
        money_set(&p1.money, MONEY_JEWELRY, 1);
        character_recalc_values(&p1);

        platform_clear_keys();
        platform_push_key('j');
        platform_push_key('k');
        treasure_appraise_gems_jewels();

        if (p1.item_count == 1) {
            item_name_known(&p1.items[0], name, sizeof(name));
        } else {
            name[0] = '\0';
        }
        snprintf(detail, sizeof(detail), "%d item, \"%s\", worth %d",
                 p1.item_count, name,
                 p1.item_count == 1 ? p1.items[0].value : 0);
        check(p1.item_count == 1 && p1.items[0].type == ITEM_NECKLACE &&
              p1.items[0].namenum3 == 0xd6 && p1.items[0].value > 0 &&
              money_get(&p1.money, MONEY_JEWELRY) == 0,
              "or is kept as a piece of jewelry", detail);

        /* With no room in the pack there is nothing to do but sell, so Keep is
         * not offered and the key for it does nothing: the prompt falls through
         * to its timeout instead. */
        p1.item_count = 0;
        for (int i = 0; i < PLAYER_MAX_ITEMS; i++) {
            Item filler;

            item_init(&filler, ITEM_NECKLACE, 0, 0, 0, 0, 0, false, 0, false, 0,
                      0, 0, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
            player_item_add(&p1, &filler);
        }
        money_set(&p1.money, MONEY_JEWELRY, 1);
        money_set(&p1.money, MONEY_PLATINUM, 0);
        character_recalc_values(&p1);

        gbl.display_input_timeout_value = 'S';
        platform_clear_keys();
        platform_push_key('j');
        platform_push_key('k');
        treasure_appraise_gems_jewels();

        snprintf(detail, sizeof(detail), "%d items, %d platinum for the jewel",
                 p1.item_count, money_get(&p1.money, MONEY_PLATINUM));
        check(p1.item_count == PLAYER_MAX_ITEMS &&
              money_get(&p1.money, MONEY_PLATINUM) > 0 &&
              money_get(&p1.money, MONEY_JEWELRY) == 0,
              "a full pack has to sell", detail);
    }

    platform_set_key_typed_mode(false);
    platform_clear_keys();
    gbl.display_input_seconds_to_wait = 0;
    gbl.display_input_timeout_value = '\0';

    money_clear_all(&gbl.pooled_money);
    gbl_ground_items_clear();
    gbl.team_count      = 0;
    gbl.selected_player = NULL;
    gbl.game_state      = old_state;
    gbl.game_speed_var  = old_speed;

    printf("\n");
}

/* ------------------------------------------------- the clock and resting */

/* The world clock: the seven words inside Area1 that resting_step_game_time
 * winds on. 0x18c is the tenths slot and each one after it is two bytes along;
 * see the offset table in resting.c. */
static void world_clock_clear(void)
{
    for (int i = 0; i <= 6; i++) {
        area1_word_set(gbl.area_ptr, 0x18c + (i * 2), 0);
    }
}

static int world_clock_minutes(void)
{
    return (gbl.area_ptr->time_minutes_tens * 10) +
           gbl.area_ptr->time_minutes_ones;
}

/* gbl.time_to_rest, as the camp screen would have filled it in. */
static void set_rest_time(int days, int hours, int minutes)
{
    rest_time_clear(&gbl.time_to_rest);
    gbl.time_to_rest.slot[REST_SLOT_DAYS]         = days;
    gbl.time_to_rest.slot[REST_SLOT_HOURS]        = hours;
    gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] = minutes / 10;
    gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] = minutes % 10;
}

static bool rest_time_empty(void)
{
    for (int i = 0; i < REST_TIME_SLOTS; i++) {
        if (gbl.time_to_rest.slot[i] != 0) {
            return false;
        }
    }

    return true;
}

static void check_resting(const char *out_dir)
{
    Player p1, p2;
    char detail[240];
    int  old_speed        = gbl.game_speed_var;
    GameState old_state   = gbl.game_state;
    i16  old_period       = gbl.area2_ptr->rest_encounter_period;
    i16  old_percentage   = gbl.area2_ptr->rest_encounter_percentage;
    u8   old_party_size   = gbl.area2_ptr->party_size;
    i16  old_block_view   = gbl.area_ptr->block_area_view;
    bool interrupted;
    int  nonzero, colors;

    printf("the game clock and resting\n");

    /* Nothing here should wait for an animation, and the rest menu wants a
     * picture that is not fading over it. */
    gbl.game_speed_var = 0;
    gbl.game_state     = GAME_STATE_CAMPING;
    gbl.area_ptr->picture_fade    = 0;
    gbl.area_ptr->block_area_view = 0;

    player_init(&p1);
    player_init(&p2);
    p1.field_125 = p2.field_125 = 1;
    set_strength_dex(&p1, 10, 0, 10);
    set_strength_dex(&p2, 10, 0, 10);
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    snprintf(p2.name, sizeof(p2.name), "%s", "Dragonbait");
    p1.hit_point_max = 10;
    p2.hit_point_max = 8;
    p1.hit_point_current = 10;
    p2.hit_point_current = 8;
    p1.age = 20;
    p2.age = 30;

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    gbl.selected_player = &p1;
    gbl.area2_ptr->party_size = 2;
    gbl.area2_ptr->rest_encounter_period = 0;
    gbl.area2_ptr->rest_encounter_percentage = 0;
    rest_time_clear(&gbl.time_to_rest);
    gbl.rest_10_seconds = 0;
    gbl.rest_encounter_count = 0;

    /* --- the piece of ovr023 the resting code needs. */
    {
        snprintf(detail, sizeof(detail), "0x0f \"%s\", 0x38 \"%s\", 0x3b \"%s\"",
                 spellcast_spell_name(SPELL_MAGIC_MISSILE),
                 spellcast_spell_name(0x38), spellcast_spell_name(0x3b));
        check(strcmp(spellcast_spell_name(SPELL_MAGIC_MISSILE),
                     "Magic Missile") == 0 &&
              strcmp(spellcast_spell_name(0x38), "Restoration") == 0 &&
              strcmp(spellcast_spell_name(0x3b), "") == 0 &&
              strcmp(spellcast_spell_name(0x65), "") == 0,
              "every spell id has a name, and the unused ids an empty one",
              detail);
    }

    /* --- sixty minutes make an hour. The clock is stepped one unit at a time
     * because a single call only carries once per slot. */
    {
        world_clock_clear();

        for (int i = 0; i < 12; i++) {
            resting_step_game_time(REST_SLOT_MINUTES_ONES, 5);
        }

        snprintf(detail, sizeof(detail), "%02d:%02d, tenths %d",
                 (int)gbl.area_ptr->time_hour, world_clock_minutes(),
                 (int)gbl.area_ptr->field_18C);
        check(gbl.area_ptr->time_hour == 1 && world_clock_minutes() == 0 &&
              gbl.area_ptr->field_18C == 0,
              "twelve five-minute steps make an hour", detail);
    }

    /* --- and the slots above it. The field Area1 calls time_year is really the
     * month; field_198 is the year. */
    {
        world_clock_clear();
        resting_step_game_time(REST_SLOT_HOURS, 24);

        snprintf(detail, sizeof(detail), "day %d at %02d:%02d",
                 (int)gbl.area_ptr->time_day, (int)gbl.area_ptr->time_hour,
                 world_clock_minutes());
        check(gbl.area_ptr->time_hour == 0 && gbl.area_ptr->time_day == 1,
              "twenty-four hours make a day", detail);

        /* Twenty-nine more, the day above having already been counted. */
        resting_step_game_time(REST_SLOT_DAYS, 29);

        snprintf(detail, sizeof(detail), "day %d of month %d",
                 (int)gbl.area_ptr->time_day, (int)gbl.area_ptr->time_year);
        check(gbl.area_ptr->time_day == 0 && gbl.area_ptr->time_year == 1,
              "and thirty days a month", detail);
    }

    /* --- the year rolling over is everybody's birthday. */
    {
        int age1, age2;

        world_clock_clear();
        area1_word_set(gbl.area_ptr, 0x198, 0xff);
        resting_step_game_time(REST_SLOT_MONTHS, 12);
        age1 = p1.age;

        snprintf(detail, sizeof(detail), "year 0x%x, Alias is %d",
                 (int)gbl.area_ptr->field_198, p1.age);
        check(gbl.area_ptr->field_198 == 0x100 && gbl.area_ptr->time_year == 0 &&
              p1.age == 21 && p2.age == 31,
              "the party has a birthday when the year turns over", detail);

        /* The year slot is not reduced when it reaches its ceiling, so from here
         * on every single unit added to the clock is another birthday - five of
         * them for one five-minute step. The original did this too. */
        resting_step_game_time(REST_SLOT_MINUTES_ONES, 5);
        age2 = p1.age;

        snprintf(detail, sizeof(detail), "%d, then %d five minutes later",
                 age1, age2);
        check(age2 == age1 + 5 && p2.age == 36,
              "and a birthday every step afterwards, as the original had it",
              detail);

        world_clock_clear();
        p1.age = 20;
        p2.age = 30;
    }

    /* --- affects run out as the clock goes past them. */
    {
        affect_list_clear(&p1.affects);
        effect_add_affect(false, 0, 3, AFFECT_BLESS, &p1);
        effect_add_affect(false, 0, 30, AFFECT_SHIELD, &p1);
        effect_add_affect(false, 0, 0, AFFECT_CURSED, &p1);

        for (int i = 0; i < GBL_AFFECTS_TIMED_OUT; i++) {
            gbl.affects_timed_out[i] = true;
        }

        resting_step_game_time(REST_SLOT_MINUTES_ONES, 5);

        {
            const Affect *shield = affect_list_find(&p1.affects, AFFECT_SHIELD);

            snprintf(detail, sizeof(detail), "%d affects left, shield has %d",
                     p1.affects.count, shield != NULL ? (int)shield->minutes : -1);
            check(p1.affects.count == 2 &&
                  !affect_list_has(&p1.affects, AFFECT_BLESS) &&
                  shield != NULL && shield->minutes == 25 &&
                  affect_list_has(&p1.affects, AFFECT_CURSED),
                  "five minutes takes five minutes off every affect", detail);
        }
    }

    /* --- and the shortcut that makes a night's rest cheap: while camping only
     * the characters whose flag is set are looked at. */
    {
        const Affect *shield;

        for (int i = 0; i < GBL_AFFECTS_TIMED_OUT; i++) {
            gbl.affects_timed_out[i] = false;
        }

        resting_step_game_time(REST_SLOT_MINUTES_ONES, 5);
        shield = affect_list_find(&p1.affects, AFFECT_SHIELD);

        snprintf(detail, sizeof(detail), "shield still has %d",
                 shield != NULL ? (int)shield->minutes : -1);
        check(shield != NULL && shield->minutes == 25,
              "camping with nothing ticking leaves the affects alone", detail);

        /* Outside camp every flag is set again, whatever the party is doing. */
        gbl.game_state = GAME_STATE_DUNGEON_MAP;
        resting_step_game_time(REST_SLOT_MINUTES_ONES, 5);
        shield = affect_list_find(&p1.affects, AFFECT_SHIELD);

        snprintf(detail, sizeof(detail), "shield has %d, flag %d set",
                 shield != NULL ? (int)shield->minutes : -1,
                 GBL_AFFECTS_TIMED_OUT - 1);
        check(shield != NULL && shield->minutes == 20 &&
              gbl.affects_timed_out[GBL_AFFECTS_TIMED_OUT - 1],
              "and out of it every character is looked at", detail);

        gbl.game_state = GAME_STATE_CAMPING;
        affect_list_clear(&p1.affects);
    }

    /* --- taking time off the rest. The minutes borrow from the hour. */
    {
        int steps = 0;

        set_rest_time(0, 1, 0);
        resting_subtract_rest_time(REST_SLOT_MINUTES_ONES, 5);

        snprintf(detail, sizeof(detail), "%d hours %d%d minutes",
                 gbl.time_to_rest.slot[REST_SLOT_HOURS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES]);
        check(gbl.time_to_rest.slot[REST_SLOT_HOURS] == 0 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] == 5 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] == 5,
              "an hour less five minutes is fifty-five", detail);

        while (!rest_time_empty() && steps < 100) {
            resting_subtract_rest_time(REST_SLOT_MINUTES_ONES, 5);
            steps++;
        }

        /* And the eleventh takes nothing off nothing rather than going below
         * zero. */
        resting_subtract_rest_time(REST_SLOT_MINUTES_ONES, 5);

        snprintf(detail, sizeof(detail), "%d more steps of five minutes", steps);
        check(steps == 11 && rest_time_empty(),
              "an hour is twelve five-minute steps and no more", detail);

        /* Less than five minutes left and nothing bigger to break up: the rest
         * is over and the odd minutes go with it. */
        set_rest_time(0, 0, 3);
        resting_subtract_rest_time(REST_SLOT_MINUTES_ONES, 5);
        check(rest_time_empty(),
              "three minutes less five ends the rest", "the odd minutes go too");
    }

    /* --- the rest menu, driven from the keyboard: hours, add one, minutes, add
     * five, rest. */
    {
        platform_set_key_typed_mode(true);
        platform_clear_keys();
        platform_push_key('h');
        platform_push_key('a');
        platform_push_key('m');
        platform_push_key('a');
        platform_push_key('r');

        rest_time_clear(&gbl.time_to_rest);
        world_clock_clear();
        gbl.rest_10_seconds = 0;

        interrupted = resting_run(true);

        snprintf(detail, sizeof(detail),
                 "%02d:%02d on the clock, %d steps, %s",
                 (int)gbl.area_ptr->time_hour, world_clock_minutes(),
                 gbl.rest_10_seconds, rest_time_empty() ? "nothing left to rest"
                                                        : "time still to rest");
        check(!interrupted && rest_time_empty() &&
              gbl.area_ptr->time_hour == 1 && world_clock_minutes() == 5 &&
              gbl.rest_10_seconds == 13,
              "an hour and five minutes asked for is an hour and five slept",
              detail);
    }

    /* --- Exit backs out of the menu with the time still on the clock, and the
     * cursor keys are the same commands: left moves to the hours, up adds. */
    {
        platform_clear_keys();
        platform_push_key('d');
        platform_push_key('a');
        platform_push_key('a');
        platform_push_key('s');
        platform_push_key('e');

        rest_time_clear(&gbl.time_to_rest);
        world_clock_clear();

        interrupted = resting_run(true);

        snprintf(detail, sizeof(detail), "%d days, clock at %02d:%02d",
                 gbl.time_to_rest.slot[REST_SLOT_DAYS],
                 (int)gbl.area_ptr->time_hour, world_clock_minutes());
        check(!interrupted && gbl.time_to_rest.slot[REST_SLOT_DAYS] == 1 &&
              gbl.area_ptr->time_hour == 0 && world_clock_minutes() == 0,
              "two days less one, then Exit, and nobody has slept", detail);

        platform_clear_keys();
        platform_push_key(0x4b << 8);   /* left: on to the hours */
        platform_push_key(0x48 << 8);   /* up: add one */
        platform_push_key('e');

        rest_time_clear(&gbl.time_to_rest);
        resting_run(true);

        snprintf(detail, sizeof(detail), "%d hours, %d%d minutes",
                 gbl.time_to_rest.slot[REST_SLOT_HOURS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES]);
        check(gbl.time_to_rest.slot[REST_SLOT_HOURS] == 1 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] == 0 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] == 0,
              "left then up adds an hour", detail);

        platform_set_key_typed_mode(false);
        platform_clear_keys();
    }

    /* --- a hit point each, once a day. */
    {
        p1.hit_point_current = 5;
        p2.hit_point_current = 3;
        gbl.rest_10_seconds = (8 * 36) - 1;
        set_rest_time(0, 1, 0);
        world_clock_clear();

        interrupted = resting_run(false);

        snprintf(detail, sizeof(detail), "%d/%d and %d/%d, %d steps since",
                 p1.hit_point_current, p1.hit_point_max,
                 p2.hit_point_current, p2.hit_point_max, gbl.rest_10_seconds);
        check(!interrupted && p1.hit_point_current == 6 &&
              p2.hit_point_current == 4 && gbl.rest_10_seconds == 11,
              "a day's rest is a hit point each", detail);
    }

    /* --- and what the camp screen shows afterwards: the clock the rest wound
     * on. The rest itself draws "Rest Time:" on row 0x11, which is inside the
     * region resting_run blanks on its way out, so there is nothing of it left
     * to photograph. */
    {
        set_rest_time(0, 7, 35);
        world_clock_clear();
        gbl.map_pos_x = 12;
        gbl.map_pos_y = 5;
        gbl.map_direction = 0;

        resting_run(false);

        clear_screen_raw();
        character_party_summary(gbl.selected_player);
        character_display_map_position_time();
        dump(out_dir, "resting-camp-clock.ppm");
        frame_stats(&nonzero, &colors);

        snprintf(detail, sizeof(detail), "%02d:%02d camping, %d px, %d colours",
                 (int)gbl.area_ptr->time_hour, world_clock_minutes(),
                 nonzero, colors);
        check(gbl.area_ptr->time_hour == 7 && world_clock_minutes() == 35 &&
              nonzero > 500,
              "seven and a half hours later the camp screen says so", detail);
    }

    /* --- something wandering in ends the rest early. */
    {
        gbl.area2_ptr->rest_encounter_period = 1;
        gbl.area2_ptr->rest_encounter_percentage = 100;
        gbl.rest_encounter_count = 0;
        set_rest_time(1, 0, 0);
        world_clock_clear();

        interrupted = resting_run(false);

        snprintf(detail, sizeof(detail), "%d hours %d%d minutes still to rest",
                 gbl.time_to_rest.slot[REST_SLOT_HOURS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES]);
        check(interrupted && gbl.rest_encounter_count == 0 &&
              gbl.time_to_rest.slot[REST_SLOT_DAYS] == 0 &&
              gbl.time_to_rest.slot[REST_SLOT_HOURS] == 23 &&
              world_clock_minutes() == 5,
              "the first five minutes of the night are interrupted", detail);

        /* A percentage of zero is a roll nothing can be under, so the whole day
         * goes by. */
        gbl.area2_ptr->rest_encounter_percentage = 0;
        set_rest_time(1, 0, 0);
        world_clock_clear();

        interrupted = resting_run(false);

        snprintf(detail, sizeof(detail), "day %d, %02d:%02d",
                 (int)gbl.area_ptr->time_day, (int)gbl.area_ptr->time_hour,
                 world_clock_minutes());
        check(!interrupted && rest_time_empty() &&
              gbl.area_ptr->time_day == 1 && gbl.area_ptr->time_hour == 0,
              "and with nothing wandering the party sleeps the day through",
              detail);

        gbl.area2_ptr->rest_encounter_period = 0;
        gbl.area2_ptr->rest_encounter_percentage = 0;
    }

    /* --- memorising. Fifteen minutes per spell level, one spell at a time. */
    {
        spell_list_clear(&p1.spell_list);
        spell_list_add_learn(&p1.spell_list, SPELL_MAGIC_MISSILE);
        spell_list_add_learn(&p1.spell_list, SPELL_SLEEP);

        set_rest_time(0, 1, 0);
        resting_run(false);

        snprintf(detail, sizeof(detail), "%d memorized, %d still being learnt",
                 spell_list_learnt_count(&p1.spell_list),
                 spell_list_learning_count(&p1.spell_list));
        check(spell_list_learnt_count(&p1.spell_list) == 2 &&
              spell_list_learning_count(&p1.spell_list) == 0,
              "an hour is long enough for two first-level spells", detail);
    }

    /* --- scribing. A scroll of two spells, with something behind it in the pack
     * so that the shuffle when the empty scroll is dropped is exercised. */
    {
        Item scroll, necklace;

        treasure_create_item(&scroll, ITEM_MU_SCROLL);
        scroll.namenum2 = 0xd3;         /* "With 2 Spells" */
        item_affect_set(&scroll, 1, (Affects)(0x80 | SPELL_MAGIC_MISSILE));
        item_affect_set(&scroll, 2, (Affects)(0x80 | SPELL_STINKING_CLOUD));
        item_affect_set(&scroll, 3, AFFECT_NONE);

        item_init(&necklace, ITEM_NECKLACE, 0, 0, 0, 0, 0, false, 0, false, 0,
                  0, 0, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);

        p1.item_count = 0;
        player_item_add(&p1, &scroll);
        player_item_add(&p1, &necklace);
        memset(p1.spell_book, 0, sizeof(p1.spell_book));

        check(item_is_scroll(&p1.items[0]),
              "a magic user's scroll is something to copy from", NULL);

        /* The second spell is second level, so thirty minutes after the first;
         * an hour covers both. */
        set_rest_time(0, 1, 0);
        resting_run(false);

        snprintf(detail, sizeof(detail),
                 "%d items left, first is type %d, spell book has %d and %d",
                 p1.item_count, p1.item_count > 0 ? p1.items[0].type : -1,
                 player_knows_spell(&p1, SPELL_MAGIC_MISSILE),
                 player_knows_spell(&p1, SPELL_STINKING_CLOUD));
        check(player_knows_spell(&p1, SPELL_MAGIC_MISSILE) &&
              player_knows_spell(&p1, SPELL_STINKING_CLOUD) &&
              p1.item_count == 1 && p1.items[0].type == ITEM_NECKLACE,
              "both spells are copied and the empty scroll is thrown away",
              detail);

        p1.item_count = 0;
    }

    /* --- a spell only just picked up cannot be worked on for an hour a level,
     * counted down once an hour by the resting loop. */
    {
        spell_list_clear(&p2.spell_list);
        spell_list_add_learn(&p2.spell_list, SPELL_FIREBALL);
        p2.spell_to_learn_count = 2;

        set_rest_time(0, 1, 0);
        resting_run(false);

        snprintf(detail, sizeof(detail), "%d hours still to wait",
                 p2.spell_to_learn_count);
        check(p2.spell_to_learn_count == 1 &&
              spell_list_learning_count(&p2.spell_list) == 1,
              "an hour off the wait, and nothing memorized yet", detail);

        /* The hour the wait ends is the hour the timer for the spell itself is
         * set, at two steps a level rather than three. */
        set_rest_time(0, 1, 0);
        resting_run(false);

        snprintf(detail, sizeof(detail), "%d hours to wait, %d being learnt",
                 p2.spell_to_learn_count,
                 spell_list_learning_count(&p2.spell_list));
        check(p2.spell_to_learn_count == 0 &&
              spell_list_learning_count(&p2.spell_list) == 1,
              "the second hour starts the spell rather than finishing it",
              detail);

        /* Six steps for a third-level spell, and the timer survives the party
         * stopping and starting again: Dragonbait is last on the team list, and
         * the clear at the top of a rest is one entry short. Twenty-five minutes
         * is five steps, so the spell is still not memorized. */
        set_rest_time(0, 0, 25);
        resting_run(false);

        snprintf(detail, sizeof(detail), "%d still being learnt",
                 spell_list_learning_count(&p2.spell_list));
        check(spell_list_learning_count(&p2.spell_list) == 1,
              "twenty-five minutes is not the thirty a fireball takes", detail);

        set_rest_time(0, 0, 5);
        resting_run(false);

        snprintf(detail, sizeof(detail), "%d memorized, %d being learnt",
                 spell_list_learnt_count(&p2.spell_list),
                 spell_list_learning_count(&p2.spell_list));
        check(spell_list_learnt_count(&p2.spell_list) == 1 &&
              spell_list_learning_count(&p2.spell_list) == 0,
              "and the thirtieth minute finishes it", detail);
    }

    world_clock_clear();
    rest_time_clear(&gbl.time_to_rest);
    for (int i = 0; i < GBL_AFFECTS_TIMED_OUT; i++) {
        gbl.affects_timed_out[i] = false;
    }
    gbl.rest_10_seconds      = 0;
    gbl.rest_encounter_count = 0;
    gbl.team_count           = 0;
    gbl.selected_player      = NULL;
    gbl.display_player_status_line18 = false;
    gbl.area2_ptr->rest_encounter_period     = old_period;
    gbl.area2_ptr->rest_encounter_percentage = old_percentage;
    gbl.area2_ptr->party_size                = old_party_size;
    gbl.area_ptr->block_area_view            = old_block_view;
    gbl.game_state     = old_state;
    gbl.game_speed_var = old_speed;

    printf("\n");
}

/* ------------------------------------------------- classes and their bonuses */

/* The six rollable stats at once, cur and full together. */
static void set_stats(Player *p, int str, int intel, int wis, int dex,
                      int con, int cha)
{
    stat_value_load(&p->stats.value[PSTAT_STR], str);
    stat_value_load(&p->stats.value[PSTAT_INT], intel);
    stat_value_load(&p->stats.value[PSTAT_WIS], wis);
    stat_value_load(&p->stats.value[PSTAT_DEX], dex);
    stat_value_load(&p->stats.value[PSTAT_CON], con);
    stat_value_load(&p->stats.value[PSTAT_CHA], cha);
    stat_value_load(&p->stats.value[PSTAT_STR00], 0);
}

/* An item the character has readied for the sake of its third affect only. */
static void add_affect_item(Player *p, Affects affect_3)
{
    Item it;

    item_init(&it, ITEM_RING_OF_WIZARDRY, 0, 0, 0, 0, 0, true, 0, false, 1, 1, 0,
              AFFECT_NONE, AFFECT_NONE, affect_3);
    player_item_add(p, &it);
}

/* The eight thief percentages as one string, so a wrong one is readable. */
static void thief_skills_string(const Player *p, char *dst, size_t dst_size)
{
    int n = 0;

    dst[0] = '\0';
    for (int i = 0; i < 8; i++) {
        n += snprintf(dst + n, dst_size - (size_t)n, i == 0 ? "%d" : ",%d",
                      (int)p->thief_skills[i]);
        if (n < 0 || (size_t)n >= dst_size) {
            return;
        }
    }
}

static bool thief_skills_are(const Player *p, const u8 *want)
{
    for (int i = 0; i < 8; i++) {
        if (p->thief_skills[i] != want[i]) {
            return false;
        }
    }
    return true;
}

static void check_classcalc(const char *out_dir)
{
    Player p;
    char detail[240];
    GameState old_state = gbl.game_state;
    int  nonzero, colors;

    printf("classes and their bonuses\n");

    /* --- the three tables that belong to overlays not translated yet. */
    {
        snprintf(detail, sizeof(detail),
                 "fighter 1/12 %d/%d, magic-user 0 %d, ranger flag 0x%02x",
                 (int)classcalc_thac0_table[SKILL_FIGHTER][1],
                 (int)classcalc_thac0_table[SKILL_FIGHTER][12],
                 (int)classcalc_thac0_table[SKILL_MAGIC_USER][0],
                 classcalc_class_flag_bits[SKILL_RANGER]);
        check(classcalc_thac0_table[SKILL_FIGHTER][1] == 40 &&
              classcalc_thac0_table[SKILL_FIGHTER][12] == 0x33 &&
              classcalc_thac0_table[SKILL_MAGIC_USER][0] == 0x27 &&
              classcalc_class_flag_bits[SKILL_RANGER] ==
                  classcalc_class_flag_bits[SKILL_PALADIN] &&
              classcalc_mu_spell_lvl_learn[0][0] == 1 &&
              classcalc_mu_spell_lvl_learn[10][4] == 1,
              "the THAC0, class-flag and magic-user spell tables read right",
              detail);
    }

    /* --- a cleric's spell slots, and the wisdom bonus on top of them. */
    {
        player_init(&p);
        set_stats(&p, 10, 10, 10, 10, 10, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_CLERIC;
        p.class_level[SKILL_CLERIC] = 1;

        classcalc_spell_cast_counts(&p);
        snprintf(detail, sizeof(detail), "%d level-1 slots",
                 (int)p.spell_cast_count[0][0]);
        check(p.spell_cast_count[0][0] == 1 && p.spell_cast_count[0][1] == 0,
              "a first level cleric gets one spell", detail);

        /* Wisdom 18 is worth two level 1s, two level 2s, a level 3 and a level
         * 4 - but only where the level already grants a slot, so a level 1
         * cleric collects nothing but the level 1s. */
        set_stats(&p, 10, 10, 18, 10, 10, 10);
        classcalc_spell_cast_counts(&p);
        snprintf(detail, sizeof(detail), "%d/%d/%d/%d/%d",
                 (int)p.spell_cast_count[0][0], (int)p.spell_cast_count[0][1],
                 (int)p.spell_cast_count[0][2], (int)p.spell_cast_count[0][3],
                 (int)p.spell_cast_count[0][4]);
        check(p.spell_cast_count[0][0] == 3 && p.spell_cast_count[0][1] == 0,
              "wisdom 18 adds two of them, and nothing it has no slot for",
              detail);

        p.class_level[SKILL_CLERIC] = 3;
        classcalc_spell_cast_counts(&p);
        snprintf(detail, sizeof(detail), "%d/%d/%d/%d/%d",
                 (int)p.spell_cast_count[0][0], (int)p.spell_cast_count[0][1],
                 (int)p.spell_cast_count[0][2], (int)p.spell_cast_count[0][3],
                 (int)p.spell_cast_count[0][4]);
        check(p.spell_cast_count[0][0] == 4 && p.spell_cast_count[0][1] == 3 &&
              p.spell_cast_count[0][2] == 0,
              "at third level the level-2 bonus lands as well", detail);
    }

    /* --- and what those slots let the character know. */
    {
        player_init(&p);
        set_stats(&p, 10, 10, 10, 10, 10, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_CLERIC;
        p.class_level[SKILL_CLERIC] = 3;

        classcalc_spell_cast_counts(&p);
        snprintf(detail, sizeof(detail), "bless %d, find traps %d, "
                 "cure disease %d",
                 player_knows_spell(&p, SPELL_BLESS),
                 player_knows_spell(&p, SPELL_FIND_TRAPS),
                 player_knows_spell(&p, SPELL_CURE_DISEASE));
        check(player_knows_spell(&p, SPELL_BLESS) &&
              player_knows_spell(&p, SPELL_FIND_TRAPS) &&
              !player_knows_spell(&p, SPELL_CURE_DISEASE) &&
              !player_knows_spell(&p, SPELL_BURNING_HANDS),
              "a cleric knows the cleric spells it has a slot for", detail);

        /* Fifth level opens the third spell level. Animate Dead is a third
         * level cleric spell and is the one exception the loop makes; and
         * Restoration, though a cleric spell, is level 7, which lands in the
         * druid row a cleric has no slots in. */
        p.class_level[SKILL_CLERIC] = 5;
        classcalc_spell_cast_counts(&p);
        snprintf(detail, sizeof(detail),
                 "%d third-level slots, dispel magic %d, animate dead %d, "
                 "restoration %d", (int)p.spell_cast_count[0][2],
                 player_knows_spell(&p, SPELL_DISPEL_MAGIC_CL),
                 player_knows_spell(&p, SPELL_ANIMATE_DEAD),
                 player_knows_spell(&p, 0x38));
        check(p.spell_cast_count[0][2] == 1 &&
              player_knows_spell(&p, SPELL_DISPEL_MAGIC_CL) &&
              !player_knows_spell(&p, SPELL_ANIMATE_DEAD) &&
              !player_knows_spell(&p, 0x38),
              "but never Animate Dead, and never Restoration", detail);
    }

    /* --- a magic-user, and the item that doubles the low-level slots. */
    {
        player_init(&p);
        set_stats(&p, 10, 16, 10, 10, 10, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_MAGIC_USER;
        p.class_level[SKILL_MAGIC_USER] = 3;

        classcalc_spell_cast_counts(&p);
        snprintf(detail, sizeof(detail), "%d/%d/%d",
                 (int)p.spell_cast_count[2][0], (int)p.spell_cast_count[2][1],
                 (int)p.spell_cast_count[2][2]);
        check(p.spell_cast_count[2][0] == 2 && p.spell_cast_count[2][1] == 1 &&
              p.spell_cast_count[2][2] == 0,
              "a third level magic-user holds two firsts and a second", detail);

        add_affect_item(&p, AFFECT_PROTECT_MAGIC);
        classcalc_spell_cast_counts(&p);
        snprintf(detail, sizeof(detail), "%d/%d/%d, fourth level %d",
                 (int)p.spell_cast_count[2][0], (int)p.spell_cast_count[2][1],
                 (int)p.spell_cast_count[2][2], (int)p.spell_cast_count[2][3]);
        check(p.spell_cast_count[2][0] == 4 && p.spell_cast_count[2][1] == 2,
              "a readied ring of wizardry doubles the first three levels",
              detail);

        /* Unreadied it does nothing. */
        p.items[0].readied = false;
        classcalc_spell_cast_counts(&p);
        check(p.spell_cast_count[2][0] == 2 && p.spell_cast_count[2][1] == 1,
              "and does nothing in the pack", NULL);
    }

    /* --- the ranger, whose one table row carries both kinds of spell. */
    {
        player_init(&p);
        set_stats(&p, 16, 14, 14, 14, 15, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_RANGER;
        p.class_level[SKILL_RANGER] = 9;

        classcalc_spell_cast_counts(&p);
        snprintf(detail, sizeof(detail),
                 "druid %d/%d/%d, magic-user %d/%d, entangle %d",
                 (int)p.spell_cast_count[1][0], (int)p.spell_cast_count[1][1],
                 (int)p.spell_cast_count[1][2], (int)p.spell_cast_count[2][0],
                 (int)p.spell_cast_count[2][1],
                 player_knows_spell(&p, SPELL_ENTANGLE));
        check(p.spell_cast_count[1][0] == 1 && p.spell_cast_count[2][0] == 1 &&
              player_knows_spell(&p, SPELL_ENTANGLE) &&
              player_knows_spell(&p, SPELL_FAERIE_FIRE),
              "a ninth level ranger gets a druid spell and a magic-user one",
              detail);
    }

    /* --- THAC0, hit dice, attacks and the class flags. */
    {
        player_init(&p);
        set_stats(&p, 16, 10, 10, 10, 10, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_FIGHTER;
        p.class_level[SKILL_FIGHTER] = 5;

        classcalc_class_bonuses(&p);
        snprintf(detail, sizeof(detail),
                 "thac0 %d, %d hit dice, %d half-attacks, flags 0x%02x",
                 (int)p.thac0, (int)p.hit_dice, (int)p.attacks_count,
                 p.class_flags);
        check(p.thac0 == 0x2c && p.hit_dice == 5 && p.attacks_count == 0 &&
              p.class_flags == classcalc_class_flag_bits[SKILL_FIGHTER],
              "a fifth level fighter's THAC0, hit dice and class flag", detail);

        /* The seventh level is where a fighter starts attacking three times
         * every two rounds. */
        p.class_level[SKILL_FIGHTER] = 7;
        classcalc_class_bonuses(&p);
        snprintf(detail, sizeof(detail), "thac0 %d, %d half-attacks",
                 (int)p.thac0, (int)p.attacks_count);
        check(p.thac0 == 0x2e && p.attacks_count == 3,
              "and three half-attacks from the seventh", detail);

        /* A multi-class fighter/thief carries both flags. */
        player_init(&p);
        set_stats(&p, 16, 10, 10, 16, 10, 10);
        p.race = RACE_DWARF;
        p.cls = CLASS_MC_F_T;
        p.class_level[SKILL_FIGHTER] = 4;
        p.class_level[SKILL_THIEF] = 3;
        classcalc_class_bonuses(&p);
        snprintf(detail, sizeof(detail), "flags 0x%02x, thac0 %d",
                 p.class_flags, (int)p.thac0);
        check(p.class_flags == (classcalc_class_flag_bits[SKILL_FIGHTER] |
                                classcalc_class_flag_bits[SKILL_THIEF]) &&
              p.thac0 == 0x2b,
              "a fighter/thief has both class flags and the better THAC0",
              detail);
    }

    /* --- saving throws, and the poison adjustment that is not an improvement. */
    {
        player_init(&p);
        set_stats(&p, 10, 10, 10, 10, 10, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_CLERIC;
        p.class_level[SKILL_CLERIC] = 3;

        classcalc_saving_throws(&p);
        snprintf(detail, sizeof(detail), "%d/%d/%d/%d/%d",
                 (int)p.save_verse[0], (int)p.save_verse[1],
                 (int)p.save_verse[2], (int)p.save_verse[3],
                 (int)p.save_verse[4]);
        check(p.save_verse[SAVE_VERSE_POISON] == 10 &&
              p.save_verse[SAVE_VERSE_PETRIFICATION] == 13 &&
              p.save_verse[SAVE_VERSE_SPELL] == 15,
              "a third level cleric's saving throws", detail);

        /* A saving throw succeeds on a roll of this number or more, so the
         * dwarf's three points of constitution "bonus" against poison make the
         * save harder. That is what the reference implementation does; see the
         * note in classcalc.c. */
        player_init(&p);
        set_stats(&p, 16, 10, 10, 10, 12, 10);
        p.race = RACE_DWARF;
        p.cls = CLASS_FIGHTER;
        p.class_level[SKILL_FIGHTER] = 1;

        classcalc_saving_throws(&p);
        snprintf(detail, sizeof(detail), "poison %d, petrification %d",
                 (int)p.save_verse[SAVE_VERSE_POISON],
                 (int)p.save_verse[SAVE_VERSE_PETRIFICATION]);
        check(p.save_verse[SAVE_VERSE_POISON] == 17 &&
              p.save_verse[SAVE_VERSE_PETRIFICATION] == 15,
              "a dwarf's constitution adjustment moves the poison save by three",
              detail);

        /* A human gets it too when carrying the item that grants it. */
        player_init(&p);
        set_stats(&p, 16, 10, 10, 10, 12, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_FIGHTER;
        p.class_level[SKILL_FIGHTER] = 1;
        classcalc_saving_throws(&p);
        check(p.save_verse[SAVE_VERSE_POISON] == 14,
              "a human without one is left alone", NULL);

        add_affect_item(&p, AFFECT_ITEM_AFFECT_6);
        classcalc_saving_throws(&p);
        snprintf(detail, sizeof(detail), "poison %d",
                 (int)p.save_verse[SAVE_VERSE_POISON]);
        check(p.save_verse[SAVE_VERSE_POISON] == 17,
              "and gets it from a readied item", detail);
    }

    /* --- thief skills: the base chance, the race and dexterity. */
    {
        static const u8 human_1[8]  = { 30, 20, 20, 15, 10, 10, 85, 0 };
        static const u8 dwarf_1[8]  = { 30, 30, 35, 15, 10, 10, 75, 0 };
        static const u8 no_dex_1[8] = { 30, 25, 20, 15, 10, 10, 85, 0 };
        char skills[80];

        player_init(&p);
        set_stats(&p, 10, 10, 10, 16, 10, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_THIEF;
        p.class_level[SKILL_THIEF] = 1;

        classcalc_thief_skills(&p);
        thief_skills_string(&p, skills, sizeof(skills));
        check(thief_skills_are(&p, human_1),
              "a first level human thief's eight skills", skills);

        /* The dwarf is better at locks and traps and worse at climbing, and
         * its penalty to reading languages is bigger than the level 1 chance,
         * so that skill is zero rather than wrapping round. */
        p.race = RACE_DWARF;
        classcalc_thief_skills(&p);
        thief_skills_string(&p, skills, sizeof(skills));
        check(thief_skills_are(&p, dwarf_1),
              "a dwarf's racial adjustments, and the clamp at zero", skills);

        /* Dexterity 22 is past the end of the adjustment table: the C# threw,
         * this logs and leaves the dexterity part out. */
        p.race = RACE_HUMAN;
        set_stats(&p, 10, 10, 10, 22, 10, 10);
        classcalc_thief_skills(&p);
        thief_skills_string(&p, skills, sizeof(skills));
        check(thief_skills_are(&p, no_dex_1),
              "a dexterity past the table costs only the dexterity bonus",
              skills);
    }

    /* --- and the two thieving items. */
    {
        static const u8 boosted[8] = { 50, 45, 20, 15, 10, 10, 85, 0 };
        char skills[80];

        player_init(&p);
        set_stats(&p, 10, 10, 10, 16, 10, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_THIEF;
        p.class_level[SKILL_THIEF] = 1;

        /* Affect 0x8b: looks up pick pockets at level 5 and open locks at
         * level 7 instead of the character's own level. */
        add_affect_item(&p, (Affects)(0x80 | 11));
        classcalc_thief_skills(&p);
        thief_skills_string(&p, skills, sizeof(skills));
        check(thief_skills_are(&p, boosted),
              "one item lifts the level the first two skills are read at",
              skills);

        /* Level 8 is already past both, so the item pays 5 per cent instead -
         * and because the original never clears it between skills, that 5 is
         * still being added to the six skills the item has nothing to do with.
         * Pick pockets 62 and open locks 55 are the item's own two; find traps
         * at 67 is 62 plus the leak. */
        p.class_level[SKILL_THIEF] = 8;
        classcalc_thief_skills(&p);
        thief_skills_string(&p, skills, sizeof(skills));
        check(p.thief_skills[0] == 62 && p.thief_skills[1] == 55 &&
              p.thief_skills[2] == 67 && p.thief_skills[7] == 75,
              "and its five per cent leaks into the other six", skills);

        /* Affect 0x82 is a flat ten per cent - except below level 4, where it
         * is spent lifting the character to level 4 and is not added at all. */
        player_init(&p);
        set_stats(&p, 10, 10, 10, 16, 10, 10);
        p.race = RACE_HUMAN;
        p.cls = CLASS_THIEF;
        p.class_level[SKILL_THIEF] = 1;
        add_affect_item(&p, (Affects)(0x80 | 2));
        classcalc_thief_skills(&p);
        thief_skills_string(&p, skills, sizeof(skills));
        check(p.thief_skills[0] == 45 && p.thief_skills[1] == 32,
              "the other buys a low level thief up to the fourth level",
              skills);

        p.class_level[SKILL_THIEF] = 5;
        classcalc_thief_skills(&p);
        thief_skills_string(&p, skills, sizeof(skills));
        check(p.thief_skills[0] == 60 && p.thief_skills[1] == 47 &&
              p.thief_skills[7] == 35,
              "and pays the ten per cent once the level is worth more", skills);
    }

    /* --- who may take a second class. */
    {
        player_init(&p);
        set_stats(&p, 18, 16, 17, 16, 16, 16);
        p.race = RACE_HUMAN;
        p.cls = CLASS_FIGHTER;
        p.class_level[SKILL_FIGHTER] = 5;
        p.alignment = 0;                        /* lawful good */

        snprintf(detail, sizeof(detail),
                 "cleric %d, fighter %d, magic-user %d, paladin %d",
                 classcalc_second_class_allowed(CLASS_CLERIC, &p),
                 classcalc_second_class_allowed(CLASS_FIGHTER, &p),
                 classcalc_second_class_allowed(CLASS_MAGIC_USER, &p),
                 classcalc_second_class_allowed(CLASS_PALADIN, &p));
        check(classcalc_second_class_allowed(CLASS_CLERIC, &p) &&
              !classcalc_second_class_allowed(CLASS_FIGHTER, &p) &&
              !classcalc_second_class_allowed(CLASS_MAGIC_USER, &p) &&
              !classcalc_second_class_allowed(CLASS_PALADIN, &p),
              "wisdom 17 opens the cleric and nothing else", detail);

        /* The old class wants 15 in everything it demanded 9 of, and a fighter
         * demands strength. */
        set_stats(&p, 14, 16, 17, 16, 16, 16);
        check(!classcalc_second_class_allowed(CLASS_CLERIC, &p),
              "and strength 14 shuts the door on all of them", NULL);

        /* Alignment counts: a thief may not be lawful good. */
        set_stats(&p, 18, 18, 18, 18, 18, 18);
        p.alignment = 0;
        snprintf(detail, sizeof(detail), "lawful good thief %d, true neutral %d",
                 classcalc_second_class_allowed(CLASS_THIEF, &p),
                 0);
        if (!classcalc_second_class_allowed(CLASS_THIEF, &p)) {
            p.alignment = 4;                    /* true neutral */
            snprintf(detail, sizeof(detail),
                     "lawful good thief 0, true neutral %d",
                     classcalc_second_class_allowed(CLASS_THIEF, &p));
        }
        check(p.alignment == 4 &&
              classcalc_second_class_allowed(CLASS_THIEF, &p),
              "a lawful good character cannot turn thief, a neutral one can",
              detail);
    }

    /* --- dual-classing, which is a menu. */
    gbl.game_state = GAME_STATE_CAMPING;
    {
        Player before;

        player_init(&p);
        set_stats(&p, 18, 16, 17, 16, 16, 16);
        snprintf(p.name, sizeof(p.name), "%s", "Alias");
        p.race = RACE_HUMAN;
        p.cls = CLASS_FIGHTER;
        p.class_level[SKILL_FIGHTER] = 5;
        p.hit_dice = 5;
        p.exp = 40000;
        p.alignment = 0;
        add_readied(&p, ITEM_LONG_SWORD, 0, 60);

        /* Only the cleric qualifies - see the check above - so whichever entry
         * the list highlight lands on is that one. */
        before = p;
        gbl.menu_screen_index = 0;
        gbl.display_input_timeout_value = '\0';
        clear_screen_raw();
        platform_push_key('s');
        classcalc_duel_class(&p);

        frame_stats(&nonzero, &colors);
        dump(out_dir, "classcalc-duel-class.ppm");
        snprintf(detail, sizeof(detail),
                 "class %d level %d, old fighter %d, multiclass %d, hit dice %d,"
                 " %d exp, %d px", p.cls, (int)p.class_level[SKILL_CLERIC],
                 (int)p.class_level_old[SKILL_FIGHTER],
                 (int)p.multiclass_level, (int)p.hit_dice, (int)p.exp, nonzero);
        check(p.cls == CLASS_CLERIC && p.class_level[SKILL_CLERIC] == 1 &&
              p.class_level[SKILL_FIGHTER] == 0 &&
              p.class_level_old[SKILL_FIGHTER] == 5 &&
              p.multiclass_level == 5 && p.hit_dice == 1 && p.exp == 0 &&
              nonzero > 100,
              "a fighter becomes a first level cleric", detail);

        /* Wisdom 17 is two bonus first-level spells on top of the one the
         * level gives, and the sword the cleric may not use is put away. */
        snprintf(detail, sizeof(detail), "%d spells, flags 0x%02x, sword %s",
                 (int)p.spell_cast_count[0][0], p.class_flags,
                 p.items[0].readied ? "readied" : "put away");
        check(p.spell_cast_count[0][0] == 3 &&
              p.class_flags == classcalc_class_flag_bits[SKILL_CLERIC] &&
              !p.items[0].readied,
              "with three spells and no sword", detail);

        /* Until the cleric passes the fighter the old levels do not count, so
         * the fighter's THAC0 and class flag are gone. */
        snprintf(detail, sizeof(detail), "thac0 %d, exceeded %d", (int)p.thac0,
                 player_dual_class_exceeded(&p));
        check(p.thac0 == 40 && !player_dual_class_exceeded(&p),
              "and the fighter's THAC0 does not count yet", detail);

        /* Sixth level cleric out-levels the fighter 5, and everything the old
         * class was worth comes back. */
        p.class_level[SKILL_CLERIC] = 6;
        classcalc_class_bonuses(&p);
        snprintf(detail, sizeof(detail),
                 "thac0 %d, flags 0x%02x, fighter skill level %d", (int)p.thac0,
                 p.class_flags, player_skill_level(&p, SKILL_FIGHTER));
        check(player_dual_class_exceeded(&p) && p.thac0 == 0x2c &&
              p.class_flags == (classcalc_class_flag_bits[SKILL_CLERIC] |
                                classcalc_class_flag_bits[SKILL_FIGHTER]) &&
              player_skill_level(&p, SKILL_FIGHTER) == 5,
              "and at sixth level the fighter counts again", detail);

        /* A character who qualifies for nothing is told so and left alone. */
        p = before;
        set_stats(&p, 14, 10, 10, 10, 10, 10);
        platform_clear_keys();
        classcalc_duel_class(&p);
        snprintf(detail, sizeof(detail), "class %d level %d", p.cls,
                 (int)p.class_level[SKILL_FIGHTER]);
        check(p.cls == CLASS_FIGHTER && p.class_level[SKILL_FIGHTER] == 5 &&
              p.exp == 40000,
              "a character who qualifies for nothing keeps its class", detail);
    }

    gbl.game_state = old_state;
    platform_clear_keys();

    printf("\n");
}

/* --------------------------------------------------------- copy protection */

/* ovr020 is the character sheet and everything reachable from it. What can be
 * checked without a keyboard is the arithmetic and the pack bookkeeping - what a
 * stack splits into, what joins with what, who the cursor keys land on - plus
 * that the sheet's figures land in the cells they are addressed to. The two
 * routines that stop for a key are driven from the queue in typed mode, the way
 * check_aftercombat drives ovr006. */
static void viewplayer_scene(Player *p1, Player *p2, Player *p3)
{
    player_init(p1);
    player_init(p2);
    player_init(p3);

    snprintf(p1->name, sizeof(p1->name), "%s", "Alias");
    snprintf(p2->name, sizeof(p2->name), "%s", "Dragonbait");
    snprintf(p3->name, sizeof(p3->name), "%s", "Olive");

    p1->sex       = 1;
    p1->race      = RACE_HUMAN;
    p1->alignment = 3;                          /* Neutral Good */
    p1->cls       = CLASS_FIGHTER;
    p1->class_level[SKILL_FIGHTER] = 5;
    p1->hit_dice  = 5;
    p1->age       = 24;
    p1->exp       = 12000;
    p1->hit_point_max = p1->hit_point_current = 40;
    p1->health_status = STATUS_OKEY;
    p1->in_combat = true;
    p1->control_morale = 0;
    set_stats(p1, 18, 12, 11, 16, 15, 13);
    stat_value_load(&p1->stats.value[PSTAT_STR00], 76);

    p2->cls = CLASS_CLERIC;
    p2->class_level[SKILL_CLERIC] = 5;
    p2->health_status = STATUS_OKEY;
    p2->in_combat = true;

    p3->cls = CLASS_THIEF;
    p3->class_level[SKILL_THIEF] = 5;
    p3->health_status = STATUS_OKEY;
    p3->in_combat = true;

    money_clear_all(&p1->money);
    money_set(&p1->money, MONEY_GOLD, 240);
    money_set(&p1->money, MONEY_GEMS, 3);
    money_clear_all(&p2->money);
    money_clear_all(&p3->money);

    gbl.team_count = 0;
    gbl_team_add(p1);
    gbl_team_add(p2);
    gbl_team_add(p3);
    gbl.selected_player = p1;
    gbl.trade_with = p1;
}

/* A stack of `count` arrows, all of them alike, so join_items has something to
 * gather. `charges` goes in affect_1, which is what keeps a partly used wand out
 * of somebody else's pile. */
static void add_stack(Player *p, int count, int charges)
{
    Item it;

    item_init(&it, ITEM_ARROW, 0, 0, 0, 0, 0, false, 0, false, 1, (u8)count, 1,
              (Affects)charges, AFFECT_NONE, AFFECT_NONE);
    it.count = count;
    player_item_add(p, &it);
}

/* ---------------------------------------------------------- casting a spell */

/* A caster of the level given in both classes, standing on the combat map. */
static void spell_caster(Player *p, Action *a, int level, Point pos)
{
    p->cls = CLASS_MAGIC_USER;
    p->class_level[SKILL_MAGIC_USER] = (u8)level;
    p->class_level[SKILL_CLERIC]     = (u8)level;
    p->race = RACE_HUMAN;
    p->health_status = STATUS_OKEY;
    p->in_combat     = true;
    p->hit_dice      = (u8)level;
    p->hit_point_max = 30;
    p->hit_point_current = 30;
    p->field_DE = 1;
    p->quick_fight = QUICK_FIGHT_TRUE;

    action_init(a);
    p->actions = a;

    combatmap_place_combatant(false, pos, p);
}

static void check_spellcast(const char *out_dir)
{
    Player p1, p2;
    Action a1, a2;
    GroundTileMap *scene_map;
    char detail[240];
    Player *old_selected = gbl.selected_player;
    Player *old_last     = gbl.last_selected_spell_target;
    bool (*old_cast_fn)(QuickFight, int) = gbl.spell_cast_function;
    int  old_speed       = gbl.game_speed_var;
    int  old_team_count  = gbl.team_count;
    bool old_from_item   = gbl.spell_from_item;
    i16  old_dungeon     = 0;
    int  hp_before;
    bool ok;

    (void)out_dir;

    printf("what a spell does\n");

    gbl.game_speed_var = 0;
    rnd_seed(0x5be11cau);

    if (!combat_scene_setup(&p1, &a1, &p2, &a2)) {
        check(false, "a fight to cast spells in", "out of memory");
        gbl.game_speed_var = old_speed;
        return;
    }
    scene_map = gbl.map_to_background_tile;

    if (gbl.area_ptr != NULL) {
        old_dungeon = gbl.area_ptr->in_dungeon;
        gbl.area_ptr->in_dungeon = 1;
    }

    spell_caster(&p1, &a1, 10, point_make(26, 12));
    spell_caster(&p2, &a2, 3, point_make(28, 12));
    p2.combat_team = 1;
    /* Intelligence and wisdom above 8 are what can_learn_spell asks for, and
     * casting anything off a list needs to pass it. */
    set_stats(&p1, 12, 16, 16, 12, 12, 12);
    set_stats(&p2, 12, 16, 16, 12, 12, 12);
    character_recalc_values(&p1);
    character_recalc_values(&p2);
    character_count_combat_teams();

    gbl.selected_player = &p1;
    gbl.team_count = 0;
    gbl_team_add(&p1);

    /* ---- the names, and the ids the table leaves blank ---- */

    snprintf(detail, sizeof(detail), "0x0f is \"%s\", 0x39 is %d long, 0x66 %d",
             spellcast_spell_name(SPELL_MAGIC_MISSILE),
             (int)strlen(spellcast_spell_name(SPELL_39)),
             (int)strlen(spellcast_spell_name(0x66)));
    check(strcmp(spellcast_spell_name(SPELL_MAGIC_MISSILE),
                 "Magic Missile") == 0 &&
          spellcast_spell_name(SPELL_39)[0] == '\0' &&
          spellcast_spell_name(0x66)[0] == '\0',
          "one name per spell id, and nothing for the ids the game never gives out",
          detail);

    /* ---- how far one reaches ---- */

    {
        int missile_range;
        int cure_range;
        int item_range;

        missile_range = spellcast_spell_range(SPELL_MAGIC_MISSILE);
        cure_range    = spellcast_spell_range(SPELL_CURE_LIGHT_WOUNDS);

        gbl.spell_from_item = true;
        item_range = spellcast_spell_range(SPELL_MAGIC_MISSILE);
        gbl.spell_from_item = false;

        snprintf(detail, sizeof(detail),
                 "6+4/level is %d at level 10, %d out of a wand, and a touch is %d",
                 missile_range, item_range, cure_range);
        check(missile_range == 46 && item_range == 30 && cure_range == 1,
              "a spell's reach is a fixed part plus a part per caster level",
              detail);
    }

    check(spellcast_spell_range(0x66) == 1,
          "and an id the table has no row for reaches the square it stands on",
          "logged, where the C# indexed past the end of the array");

    /* ---- how long the affect lasts ---- */

    {
        u16 bless    = spellcast_spell_affect_timeout(SPELL_BLESS);
        u16 protect  = spellcast_spell_affect_timeout(SPELL_PROTECT_FROM_EVIL_CL);
        u16 poison   = spellcast_spell_affect_timeout(SPELL_NEUTRALIZE_POISON);
        u16 entangle = spellcast_spell_affect_timeout(0x88);

        snprintf(detail, sizeof(detail),
                 "bless %d, protection %d, neutralize poison %d, affect 0x88 %d",
                 bless, protect, poison, entangle);
        check(bless == 6 && protect == 30 && poison == 1440 && entangle == 0,
              "a duration off the table, one per level, one rolled, and one asked "
              "for by affect id", detail);
    }

    /* ---- a spell coming off a scroll ---- */

    {
        Item scroll;
        Item left;

        p1.item_count = 0;
        player_ready_reset(&p1);

        /* namenum2 0xd3 is "With 2 Spells": bless and cure light wounds. */
        item_init(&scroll, ITEM_MU_SCROLL, 0, 0xd3, 0, 0, 0, false, 0, false, 1,
                  1, 0, (Affects)SPELL_BLESS, (Affects)SPELL_CURE_LIGHT_WOUNDS,
                  AFFECT_NONE);
        player_item_add(&p1, &scroll);

        ok = spellcast_remove_spell_from_scroll(SPELL_BLESS, &p1.items[0], &p1,
                                               &left);
        snprintf(detail, sizeof(detail),
                 "%d spells left, first affect 0x%x, %d item(s) in the pack",
                 left.namenum2 - 0xd1, (int)item_affect(&left, 1),
                 p1.item_count);
        check(ok && left.namenum2 == 0xd2 && item_affect(&left, 1) == 0 &&
              p1.item_count == 1,
              "reading one spell off a two-spell scroll leaves the scroll",
              detail);

        ok = spellcast_remove_spell_from_scroll(SPELL_CURE_LIGHT_WOUNDS,
                                               &p1.items[0], &p1, &left);
        check(!ok && p1.item_count == 0,
              "and the last spell off it takes the scroll with it",
              "the pack closes up over the gap");
    }

    /* ---- who a spell touches outside a fight ---- */

    gbl.game_state = GAME_STATE_CAMPING;
    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);

    gbl.last_selected_spell_target = NULL;
    ok = spellcast_non_combat_cast(QUICK_FIGHT_TRUE, SPELL_DETECT_MAGIC_MU);
    snprintf(detail, sizeof(detail), "%d target(s)", gbl.spell_target_count);
    check(ok && gbl.spell_target_count == 1 && gbl.spell_targets[0] == &p1,
          "a spell on its caster finds one target", detail);

    ok = spellcast_non_combat_cast(QUICK_FIGHT_TRUE, SPELL_BLESS);
    snprintf(detail, sizeof(detail), "%d targets for a party of %d",
             gbl.spell_target_count, gbl.team_count);
    check(ok && gbl.spell_target_count == 3 && gbl.spell_targets[0] == &p1 &&
          gbl.spell_targets[1] == &p1,
          "a party spell touches its caster twice, as the original did", detail);

    check(!spellcast_non_combat_cast(QUICK_FIGHT_TRUE, SPELL_MAGIC_MISSILE),
          "and a combat spell has nothing to aim at outside a fight",
          "no targets, so nothing is cast");

    gbl.game_state = GAME_STATE_COMBAT;

    /* ---- the handler table ---- */

    spelleffect_setup_spells();
    check(gbl.cure_spell == false && gbl.spell_from_item == false &&
          gbl.last_selected_spell_target == NULL && gbl.byte_1D2C8 == true &&
          gbl.spell_cast_function == spellcast_non_combat_cast,
          "setting the spells up leaves them aimed the out-of-combat way",
          "which is what ovr023.setup_spells is, less its dictionary");

    gbl.spell_id = 0x66;
    spelleffect_call(0x66);
    check(true, "an id with no handler at all does nothing",
          "logged, where the C# dictionary threw KeyNotFoundException");

    /* ---- a cure, and the damage it is the mirror of ---- */

    gbl.selected_player = &p1;
    gbl.spell_id = SPELL_CURE_LIGHT_WOUNDS;
    p2.hit_point_current = 10;
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p2);

    spelleffect_call(SPELL_CURE_LIGHT_WOUNDS);
    snprintf(detail, sizeof(detail), "%d of %d hit points back",
             p2.hit_point_current, p2.hit_point_max);
    check(p2.hit_point_current > 10 && p2.hit_point_current <= 18,
          "cure light wounds gives 1d8 hit points back", detail);

    gbl.spell_id = SPELL_MAGIC_MISSILE;
    hp_before = p2.hit_point_current;
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p2);

    spelleffect_call(SPELL_MAGIC_MISSILE);
    snprintf(detail, sizeof(detail), "%d hit points off %d",
             hp_before - p2.hit_point_current, hp_before);
    check(p2.hit_point_current < hp_before,
          "and a magic missile takes some away", detail);

    /* ---- an affect laid, then dispelled ---- */

    /* A bless keeps only those targets that are on the caster's own side, and in
     * a fight only those with no enemy within reach of them. p2 is on team 1, so
     * it is dropped for the first reason; the two stand two squares apart, so p1
     * is not dropped for the second. */
    gbl.spell_id = SPELL_BLESS;
    p1.affects.count = 0;
    p2.affects.count = 0;
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p1);
    gbl_spell_target_add(&p2);

    spelleffect_call(SPELL_BLESS);
    snprintf(detail, sizeof(detail),
             "%d target(s) kept, the caster has %d affect(s), the enemy %d",
             gbl.spell_target_count, p1.affects.count, p2.affects.count);
    check(gbl.spell_target_count == 1 &&
          player_has_affect(&p1, AFFECT_BLESS) &&
          !player_has_affect(&p2, AFFECT_BLESS),
          "a bless takes only the targets on the caster's own side", detail);

    /* The affect carries the level of whoever laid it, and a dispel weighs its own
     * caster's level against that: level for level it is one roll of d100 against
     * 50, so it is tried until it lands rather than once. */
    {
        int tries;

        gbl.spell_id = SPELL_DISPEL_MAGIC_MU;
        gbl.target_pos = combatmap_player_map_pos(&p1);

        for (tries = 1; tries <= 20; tries++) {
            gbl_spell_targets_clear();
            gbl_spell_target_add(&p1);
            spelleffect_call(SPELL_DISPEL_MAGIC_MU);

            if (!player_has_affect(&p1, AFFECT_BLESS)) {
                break;
            }
        }

        snprintf(detail, sizeof(detail), "gone after %d cast(s), %d affect(s) left",
                 tries, p1.affects.count);
        check(!player_has_affect(&p1, AFFECT_BLESS),
              "and a dispel by a caster of the same level is an even chance of "
              "taking it off", detail);
    }

    /* ---- poison, and the cure for it ---- */

    p2.affects.count = 0;
    effect_add_affect(true, 1, 100, AFFECT_POISONED, &p2);
    p2.hit_point_current = 0;
    gbl.spell_id = SPELL_NEUTRALIZE_POISON;
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p2);

    spelleffect_call(SPELL_NEUTRALIZE_POISON);
    snprintf(detail, sizeof(detail), "%d hit point(s), status %d",
             p2.hit_point_current, (int)p2.health_status);
    check(!player_has_affect(&p2, AFFECT_POISONED) &&
          p2.hit_point_current == 1 && p2.health_status == STATUS_OKEY,
          "neutralize poison stands the poisoned back up on one hit point",
          detail);

    /* ---- raising the dead, and what it costs ---- */

    p2.affects.count = 0;
    p2.health_status = STATUS_DEAD;
    p2.hit_point_current = -8;
    p2.stats.value[PSTAT_CON].cur = 12;
    gbl.spell_id = SPELL_RAISE_DEAD;
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p2);

    spelleffect_call(SPELL_RAISE_DEAD);
    snprintf(detail, sizeof(detail), "status %d, %d hit point(s), Con %d",
             (int)p2.health_status, p2.hit_point_current,
             p2.stats.value[PSTAT_CON].cur);
    check(p2.health_status == STATUS_OKEY && p2.hit_point_current == 1 &&
          p2.stats.value[PSTAT_CON].cur == 11,
          "raise dead costs a point of constitution", detail);

    p2.race = RACE_ELF;
    p2.health_status = STATUS_DEAD;
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p2);

    spelleffect_call(SPELL_RAISE_DEAD);
    check(p2.health_status == STATUS_DEAD,
          "and an elf cannot be raised at all",
          "the status is left as it was");
    p2.race = RACE_HUMAN;
    p2.health_status = STATUS_OKEY;

    /* ---- a drained level given back ---- */

    p2.affects.count = 0;
    p2.class_level[SKILL_MAGIC_USER] = 3;
    p2.class_level[SKILL_CLERIC]     = 0;
    p2.lost_lvls = 1;
    p2.lost_hp   = 4;
    p2.exp       = 0;
    p2.hit_point_max = 20;
    p2.hit_point_current = 20;
    p2.hit_point_rolled  = 20;
    gbl.spell_id = SPELL_RESTORATION;
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p2);

    spelleffect_call(SPELL_RESTORATION);
    snprintf(detail, sizeof(detail),
             "level %d, %d hit points, %d lost level(s) left, %d exp",
             p2.class_level[SKILL_MAGIC_USER], p2.hit_point_max, p2.lost_lvls,
             p2.exp);
    check(p2.class_level[SKILL_MAGIC_USER] == 4 && p2.hit_point_max == 24 &&
          p2.lost_lvls == 0 && p2.exp >= 5000,
          "restoration gives back the level and the hit points that went with it",
          detail);

    p2.class_level[SKILL_MAGIC_USER] = 3;
    p2.class_level[SKILL_CLERIC]     = 0;

    /* ---- a bolt down a line, which bounces off the far wall ---- */

    p2.affects.count = 0;
    p2.health_status = STATUS_OKEY;
    p2.in_combat     = true;
    p2.hit_point_max = 60;
    p2.hit_point_current = 60;
    p2.save_verse[SAVE_VERSE_SPELL] = 30;   /* no save, so the damage lands */
    gbl.spell_id = SPELL_LIGHTNING_BOLT;
    gbl.target_pos = combatmap_player_map_pos(&p2);
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p2);

    hp_before = p2.hit_point_current;
    spelleffect_call(SPELL_LIGHTNING_BOLT);
    snprintf(detail, sizeof(detail), "%d hit points off %d",
             hp_before - p2.hit_point_current, hp_before);
    check(p2.hit_point_current < hp_before,
          "a lightning bolt strikes whoever is standing where it lands", detail);

    /* ---- a cloud laid on the map ---- */

    gbl.cloud_kill_count = 0;
    gbl.selected_player  = &p1;
    p1.affects.count     = 0;
    gbl.spell_id   = SPELL_CLOUD_KILL;
    gbl.target_pos = point_make(30, 12);
    gbl_spell_targets_clear();
    gbl_spell_target_add(&p1);

    spelleffect_call(SPELL_CLOUD_KILL);
    {
        int tile = ground_tile_map_get(gbl.map_to_background_tile,
                                       point_make(30, 12));

        snprintf(detail, sizeof(detail), "%d cloud(s), tile %d under it",
                 gbl.cloud_kill_count, tile);
        check(gbl.cloud_kill_count == 1 &&
              player_has_affect(&p1, AFFECT_IN_CLOUD_KILL) &&
              gbl.cloud_kill_cloud[0].player == &p1,
              "a cloud kill goes on the map and on its caster", detail);
    }
    gbl.cloud_kill_count = 0;

    /* ---- the spell lists a character is shown ---- */

    snprintf(detail, sizeof(detail),
             "int %d, wis %d - and 0x24, a monster's, is nobody's to learn",
             p1.stats.value[PSTAT_INT].full, p1.stats.value[PSTAT_WIS].full);
    check(spellmenu_can_learn_spell(SPELL_MAGIC_MISSILE, &p1) &&
          spellmenu_can_learn_spell(SPELL_BLESS, &p1) &&
          !spellmenu_can_learn_spell(0x24, &p1) &&
          !spellmenu_can_learn_spell(0, &p1),
          "a spell is learnable by the class that casts it, on the stat it casts on",
          detail);

    spell_list_clear(&p1.spell_list);
    check(!spellmenu_build_spell_list(SPELL_LOC_MEMORY),
          "an empty memory lists nothing",
          "so nothing is drawn and nothing can be picked");

    spell_list_add_learnt(&p1.spell_list, SPELL_MAGIC_MISSILE);
    spell_list_add_learnt(&p1.spell_list, SPELL_FIREBALL);
    check(spellmenu_build_spell_list(SPELL_LOC_MEMORY),
          "and two memorised spells list something",
          "one heading per spell level, then the spells under it");

    /* ---- teardown ---- */

    gbl_spell_targets_clear();
    gbl.spell_id = 0;
    gbl.map_to_background_tile = scene_map;
    combat_scene_teardown();

    if (gbl.area_ptr != NULL) {
        gbl.area_ptr->in_dungeon = old_dungeon;
    }

    gbl.selected_player = old_selected;
    gbl.last_selected_spell_target = old_last;
    gbl.spell_cast_function = old_cast_fn;
    gbl.spell_from_item = old_from_item;
    gbl.team_count      = old_team_count;
    gbl.game_speed_var  = old_speed;

    printf("\n");
}

static void check_viewplayer(const char *out_dir)
{
    Player p1, p2, p3;
    char detail[240];
    int  nonzero = 0, colors = 0;
    GameState old_state  = gbl.game_state;
    Player *old_selected = gbl.selected_player;
    Player *old_trade    = gbl.trade_with;
    int  old_team_count  = gbl.team_count;
    int  old_speed       = gbl.game_speed_var;
    bool old_in_demo     = gbl.in_demo;
    int  old_wait        = gbl.display_input_seconds_to_wait;
    char old_timeout     = gbl.display_input_timeout_value;

    printf("the character sheet and the pack\n");

    gbl.game_state     = GAME_STATE_CAMPING;
    gbl.game_speed_var = 0;
    gbl.in_demo        = false;
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value   = '\0';
    platform_clear_keys();
    platform_set_key_typed_mode(true);

    /* ---- the three name tables, and what an index off the end reads as ---- */
    {
        snprintf(detail, sizeof(detail), "%s %s, %s, and %d/%d/%d off the end",
                 viewplayer_sex_name(1), viewplayer_race_name(RACE_HALF_ORC),
                 viewplayer_alignment_name(8),
                 (int)strlen(viewplayer_sex_name(2)),
                 (int)strlen(viewplayer_race_name(-1)),
                 (int)strlen(viewplayer_alignment_name(9)));
        check(strcmp(viewplayer_sex_name(1), "Female") == 0 &&
              strcmp(viewplayer_race_name(RACE_HALF_ORC), "Half-Orc") == 0 &&
              strcmp(viewplayer_alignment_name(8), "Chaotic Evil") == 0 &&
              viewplayer_sex_name(2)[0] == '\0' &&
              viewplayer_race_name(-1)[0] == '\0' &&
              viewplayer_alignment_name(9)[0] == '\0',
              "sex, race and alignment read out of their tables", detail);
    }

    /* ---- the whole sheet ---- */
    {
        viewplayer_scene(&p1, &p2, &p3);
        clear_screen_raw();

        viewplayer_display_full(&p1);

        /* Row 3 carries sex, race and age side by side; rows 7 to 12 the six
         * stats with the money beside them; row 15 the level and experience;
         * rows 17 and 18 the combat figures. */
        snprintf(detail, sizeof(detail),
                 "%d px on the name row, %d on stats, %d of money, %d on the "
                 "level row, %d of combat figures",
                 cell_ink(1, 1, 5, 0x26), cell_ink(7, 1, 12, 0x0b),
                 cell_ink(7, 12, 13, 0x1a), cell_ink(15, 1, 15, 0x26),
                 cell_ink(0x11, 1, 0x12, 0x26));
        check(cell_ink(1, 1, 5, 0x26) > 300 && cell_ink(7, 1, 12, 0x0b) > 100 &&
              cell_ink(7, 12, 13, 0x1a) > 100 &&
              cell_ink(15, 1, 15, 0x26) > 100 &&
              cell_ink(0x11, 1, 0x12, 0x26) > 300,
              "the sheet fills every row it addresses", detail);

        frame_stats(&nonzero, &colors);
        dump(out_dir, "viewplayer-sheet.ppm");
        snprintf(detail, sizeof(detail),
                 "%d px, %d colours -> viewplayer-sheet.ppm", nonzero, colors);
        check(nonzero > 6000 && colors >= 4, "and is drawn in the sheet's "
              "colours", detail);
    }

    /* ---- one coin row per kind held, largest first, and none for a kind at
     * zero: gold and gems here, so two rows and the third left clear ---- */
    {
        int row_gems = cell_ink(7, 12, 7, 0x1a);
        int row_gold = cell_ink(8, 12, 8, 0x1a);
        int row_next = cell_ink(9, 12, 9, 0x1a);

        snprintf(detail, sizeof(detail), "%d px, %d px, then %d", row_gems,
                 row_gold, row_next);
        check(row_gems > 20 && row_gold > 20 && row_next == 0,
              "the money is one row a kind, and stops", detail);
    }

    /* ---- exceptional strength, which is the only stat with a second half ---- */
    {
        int with = cell_ink(7, 7, 7, 0x0a);

        stat_value_load(&p1.stats.value[PSTAT_STR00], 0);
        frames_clear_area(7, 0x0b, 7, 5);
        viewplayer_display_stat(false, STAT_STR);

        snprintf(detail, sizeof(detail), "%d px with 18/76, %d without", with,
                 cell_ink(7, 7, 7, 0x0a));
        check(with > 20 && cell_ink(7, 7, 7, 0x0a) == 0,
              "18/76 prints its percentile and plain 18 does not", detail);

        stat_value_load(&p1.stats.value[PSTAT_STR00], 76);
    }

    /* ---- what a character can be handed ---- */
    {
        Item light, heavy;
        bool full_pack, too_heavy, fits;

        viewplayer_scene(&p1, &p2, &p3);
        set_stats(&p1, 18, 12, 11, 16, 15, 13);
        character_recalc_values(&p1);

        item_init(&light, ITEM_DAGGER, 0, 0, 0, 0, 0, false, 0, false, 10, 1, 5,
                  AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
        item_init(&heavy, ITEM_PLATE_MAIL, 0, 0, 0, 0, 0, false, 0, false,
                  (i16)(character_max_encumberance(&p1) + 1501), 1, 400,
                  AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);

        fits      = viewplayer_can_carry(&light, &p1);
        too_heavy = viewplayer_can_carry(&heavy, &p1);

        for (int i = 0; i < PLAYER_MAX_ITEMS; i++) {
            player_item_add(&p1, &light);
        }
        full_pack = viewplayer_can_carry(&light, &p1);

        snprintf(detail, sizeof(detail),
                 "a dagger %s, %d weight %s, a full pack of %d %s",
                 fits ? "refused" : "fits", heavy.weight,
                 too_heavy ? "refused" : "fits", p1.item_count,
                 full_pack ? "refused" : "fits");
        check(fits == false && too_heavy == true && full_pack == true,
              "a pack takes what fits and refuses what is too heavy or has "
              "nowhere to go", detail);
    }

    /* ---- halving a stack, which needs a free slot to halve into ---- */
    {
        Item *first, *second;
        int single_count;

        viewplayer_scene(&p1, &p2, &p3);
        add_stack(&p1, 7, 0);

        viewplayer_halve_items(player_item_at(&p1, 0));
        first  = player_item_at(&p1, 0);
        second = player_item_at(&p1, 1);

        /* And a stack of one has no half: the message is all that happens. */
        add_stack(&p1, 1, 0);
        viewplayer_halve_items(player_item_at(&p1, 2));
        single_count = p1.item_count;

        snprintf(detail, sizeof(detail), "7 became %d and %d, and 1 left %d "
                 "items", first->count, second->count, single_count);
        check(p1.item_count == 3 && first->count == 4 && second->count == 3 &&
              second->readied == false,
              "an odd stack halves with the remainder kept, and a stack of one "
              "does not", detail);
    }

    /* ---- joining, which is where the pack closing up over a gap matters ---- */
    {
        viewplayer_scene(&p1, &p2, &p3);
        add_stack(&p1, 10, 0);
        add_stack(&p1, 20, 0);
        add_stack(&p1, 30, 0);
        add_stack(&p1, 40, 0);

        /* Joining into the second of four gathers the other three, and every one
         * of them is dropped from under the running index. */
        viewplayer_join_items(player_item_at(&p1, 1));

        snprintf(detail, sizeof(detail), "%d stack(s) of %d", p1.item_count,
                 p1.item_count > 0 ? player_item_at(&p1, 0)->count : 0);
        check(p1.item_count == 1 && player_item_at(&p1, 0)->count == 100,
              "four alike stacks join into one", detail);
    }

    {
        /* A charged item joins nothing: the test is on the item's own affect_1,
         * so a wand with two charges left matches even itself away. */
        viewplayer_scene(&p1, &p2, &p3);
        add_stack(&p1, 5, 3);
        add_stack(&p1, 5, 3);

        viewplayer_join_items(player_item_at(&p1, 0));

        snprintf(detail, sizeof(detail), "%d stacks left, %d and %d",
                 p1.item_count, player_item_at(&p1, 0)->count,
                 player_item_at(&p1, 1)->count);
        check(p1.item_count == 2 && player_item_at(&p1, 0)->count == 5 &&
              player_item_at(&p1, 1)->count == 5,
              "but two stacks with charges left do not", detail);
    }

    {
        /* The original's overflow arithmetic, reproduced: what will not fit is
         * written back as 255 - (a + b), which is negative. */
        int kept, left_over;

        viewplayer_scene(&p1, &p2, &p3);
        add_stack(&p1, 200, 0);
        add_stack(&p1, 100, 0);

        viewplayer_join_items(player_item_at(&p1, 0));

        kept      = player_item_at(&p1, 0)->count;
        left_over = player_item_at(&p1, 1)->count;

        snprintf(detail, sizeof(detail), "200 and 100 became %d and %d", kept,
                 left_over);
        check(p1.item_count == 2 && kept == 255 && left_over == -45,
              "a join past 255 keeps the original's negative remainder", detail);
    }

    /* ---- the cursor keys walking the party ---- */
    {
        Player *forward, *back, *wrapped, *unknown;

        viewplayer_scene(&p1, &p2, &p3);

        viewplayer_scroll_team_list('O');
        forward = gbl.selected_player;

        viewplayer_scroll_team_list('G');
        back = gbl.selected_player;

        viewplayer_scroll_team_list('G');
        wrapped = gbl.selected_player;

        /* Somebody not in the party is index -1, which steps to the first going
         * forward - the original's own arithmetic, and in range either way. */
        gbl.selected_player = NULL;
        viewplayer_scroll_team_list('O');
        unknown = gbl.selected_player;

        /* And the arrows, which the original ignored here. */
        viewplayer_scroll_team_list('P');
        check(gbl.selected_player == &p2,
              "and the down arrow does what end does", NULL);

        viewplayer_scroll_team_list('H');
        check(gbl.selected_player == &p1,
              "and the up arrow what home does", NULL);

        snprintf(detail, sizeof(detail), "%s, %s, %s, and nobody lands on %s",
                 forward->name, back->name, wrapped->name, unknown->name);
        check(forward == &p2 && back == &p1 && wrapped == &p3 &&
              unknown == &p1,
              "the cursor keys step along the party and wrap round", detail);
    }

    /* ---- what a paladin still has in hand ---- */
    {
        bool heal_at_first, cure_at_first, heal_spent, cure_spent, heal_in_fight;

        viewplayer_scene(&p1, &p2, &p3);
        p1.cls = CLASS_PALADIN;
        p1.class_level[SKILL_FIGHTER] = 0;
        p1.class_level[SKILL_PALADIN] = 5;
        p1.paladin_cures_left = 1;

        heal_at_first = viewplayer_can_cast_heal(&p1);
        cure_at_first = viewplayer_can_cast_cure_diseases(&p1);

        effect_add_affect(false, 0, 1440, AFFECT_PALADIN_DAILY_HEAL_CAST, &p1);
        p1.paladin_cures_left = 0;

        heal_spent = viewplayer_can_cast_heal(&p1);
        cure_spent = viewplayer_can_cast_cure_diseases(&p1);

        effect_remove_affect(NULL, AFFECT_PALADIN_DAILY_HEAL_CAST, &p1);
        gbl.game_state = GAME_STATE_COMBAT;
        heal_in_fight = viewplayer_can_cast_heal(&p1);
        gbl.game_state = GAME_STATE_CAMPING;

        snprintf(detail, sizeof(detail),
                 "heal %d then %d, cure %d then %d, and %d in a fight",
                 heal_at_first, heal_spent, cure_at_first, cure_spent,
                 heal_in_fight);
        check(heal_at_first && cure_at_first && !heal_spent && !cure_spent &&
              !heal_in_fight,
              "a paladin's daily heal and cure are each offered once, and never "
              "in combat", detail);
    }

    /* ---- readying, and the ring that doubles the low spell levels ---- */
    {
        Item ring;
        Item *held;
        bool readied, unreadied;
        u8 doubled[3], rebuilt[3];

        viewplayer_scene(&p1, &p2, &p3);
        p1.cls = CLASS_MAGIC_USER;
        p1.class_level[SKILL_FIGHTER]    = 0;
        p1.class_level[SKILL_MAGIC_USER] = 3;
        set_stats(&p1, 10, 16, 10, 10, 10, 10);
        classcalc_spell_cast_counts(&p1);

        item_init(&ring, ITEM_RING_OF_WIZARDRY, 0, 0, 0, 0, 0, false, 0, false,
                  1, 1, 0, AFFECT_NONE, AFFECT_NONE, (Affects)0x81);
        player_item_add(&p1, &ring);
        held = player_item_at(&p1, 0);

        viewplayer_ready_item(held);
        readied = held->readied;
        doubled[0] = p1.spell_cast_count[2][0];
        doubled[1] = p1.spell_cast_count[2][1];
        doubled[2] = p1.spell_cast_count[2][2];

        viewplayer_ready_item(held);
        unreadied = held->readied;
        rebuilt[0] = p1.spell_cast_count[2][0];
        rebuilt[1] = p1.spell_cast_count[2][1];
        rebuilt[2] = p1.spell_cast_count[2][2];

        snprintf(detail, sizeof(detail), "%d/%d/%d readied, %d/%d/%d off again",
                 doubled[0], doubled[1], doubled[2], rebuilt[0], rebuilt[1],
                 rebuilt[2]);
        check(readied && !unreadied && doubled[0] == 4 && doubled[1] == 2 &&
              rebuilt[0] == 2 && rebuilt[1] == 1,
              "a ring of wizardry doubles the first spell levels on and rebuilds "
              "them off", detail);
    }

    {
        /* Every one of ready_Item's refusals is thrown away before it is read,
         * so a magic-user readies plate mail. The DOS build did the same. */
        Item plate;
        Item *held;

        viewplayer_scene(&p1, &p2, &p3);
        p1.cls = CLASS_MAGIC_USER;
        p1.class_level[SKILL_FIGHTER]    = 0;
        p1.class_level[SKILL_MAGIC_USER] = 3;
        character_recalc_values(&p1);

        item_init(&plate, ITEM_PLATE_MAIL, 0, 0, 0, 0, 0, false, 0, false, 500,
                  1, 400, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
        player_item_add(&p1, &plate);
        held = player_item_at(&p1, 0);

        viewplayer_ready_item(held);

        snprintf(detail, sizeof(detail), "readied %d, class flags 0x%02x",
                 held->readied, p1.class_flags);
        check(held->readied == true,
              "the wrong class is no bar to readying, as in the original",
              detail);

        /* Cursed is the one thing that holds: it cannot be taken off again. */
        held->cursed = true;
        viewplayer_ready_item(held);
        check(held->readied == true, "and cursed armour will not come off",
              NULL);
    }

    /* ---- the debug page, which stops for a key ---- */
    {
        Item it;

        viewplayer_scene(&p1, &p2, &p3);
        clear_screen_raw();

        item_init(&it, ITEM_LONG_SWORD, 1, 2, 3, 2, 1, false, 0, true, 60, 1,
                  300, (Affects)4, (Affects)5, (Affects)6);

        platform_push_key(' ');
        viewplayer_item_display_stats(&it);

        snprintf(detail, sizeof(detail), "%d px of labels, %d of values",
                 cell_ink(1, 1, 15, 0x13), cell_ink(1, 0x14, 15, 0x20));
        check(cell_ink(1, 1, 15, 0x13) > 500 &&
              cell_ink(1, 0x14, 15, 0x20) > 100,
              "the item stats page lists fifteen fields and their values",
              detail);
        dump(out_dir, "viewplayer-item-stats.ppm");
    }

    gbl.team_count      = old_team_count;
    gbl.selected_player = old_selected;
    gbl.trade_with      = old_trade;
    gbl.game_state      = old_state;
    gbl.game_speed_var  = old_speed;
    gbl.in_demo         = old_in_demo;
    gbl.display_input_seconds_to_wait = old_wait;
    gbl.display_input_timeout_value   = old_timeout;
    platform_set_key_typed_mode(false);
    platform_clear_keys();

    printf("\n");
}

/* ------------------------------------------------------- the party menu */

/* A character ready to be trained or modified: a single class at the level
 * given, human so no racial ceiling gets in the way, and selected, because
 * partymenu_con_bonus reads gbl.selected_player rather than an argument. */
static void partymenu_scene(Player *p, int cls, int skill, int level, int con)
{
    player_init(p);
    snprintf(p->name, sizeof(p->name), "Alias");
    set_stats(p, 12, 12, 12, 12, con, 12);
    p->race  = RACE_HUMAN;
    p->cls   = cls;
    p->class_level[skill] = (u8)level;
    p->hit_dice = 1;
    p->health_status = STATUS_OKEY;
    p->in_combat = true;
    p->hit_point_max = 10;
    p->hit_point_current = 10;
    money_set(&p->money, MONEY_GOLD, 5000);

    gbl.selected_player = p;
}

static void check_partymenu(const char *out_dir)
{
    Player p1, p2, p3;
    char detail[240];
    int  nonzero = 0, colors = 0;
    GameState old_state  = gbl.game_state;
    Player *old_selected = gbl.selected_player;
    int  old_team_count  = gbl.team_count;
    int  old_speed       = gbl.game_speed_var;
    bool old_in_demo     = gbl.in_demo;
    u8   old_train_mask  = gbl.area2_ptr->training_class_mask;
    u8   old_party_size  = gbl.area2_ptr->party_size;
    int  old_wait        = gbl.display_input_seconds_to_wait;
    char old_timeout     = gbl.display_input_timeout_value;
    bool old_reload      = gbl.reload_ecl_and_pictures;
    u8   old_dax_block   = gbl.last_dax_block_id;
    bool old_game_saved  = gbl.game_saved;
    u16  old_ecl_block   = gbl.area_ptr->last_ecl_block_id;
    u8   old_field_3fa   = gbl.area_ptr->field_3FA;

    printf("the party menu, and rolling a character up\n");

    gbl.game_state     = GAME_STATE_START_GAME_MENU;
    gbl.game_speed_var = 0;
    gbl.in_demo        = false;
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value   = '\0';
    platform_clear_keys();
    platform_set_key_typed_mode(true);

    /* ---- the three tables the overlay carries ---- */
    {
        snprintf(detail, sizeof(detail),
                 "fighter %d/%d, druid stops at %d, cleric and druid "
                 "share mask %d/%d, thief and monk %d/%d, THAC0 %d/%d",
                 (int)partymenu_exp_table[SKILL_FIGHTER][1],
                 (int)partymenu_exp_table[SKILL_FIGHTER][11],
                 (int)partymenu_exp_table[SKILL_DRUID][1],
                 (int)partymenu_class_masks[SKILL_CLERIC],
                 (int)partymenu_class_masks[SKILL_DRUID],
                 (int)partymenu_class_masks[SKILL_THIEF],
                 (int)partymenu_class_masks[SKILL_MONK],
                 (int)partymenu_thac0_table[SKILL_FIGHTER][0],
                 (int)partymenu_thac0_table[SKILL_FIGHTER][12]);
        check(partymenu_exp_table[SKILL_FIGHTER][1] == 2001 &&
              partymenu_exp_table[SKILL_FIGHTER][11] == 1000001 &&
              partymenu_exp_table[SKILL_FIGHTER][12] == -1 &&
              partymenu_exp_table[SKILL_DRUID][1] == -1 &&
              partymenu_exp_table[SKILL_MONK][1] == -1 &&
              partymenu_class_masks[SKILL_CLERIC] ==
                  partymenu_class_masks[SKILL_DRUID] &&
              partymenu_class_masks[SKILL_THIEF] ==
                  partymenu_class_masks[SKILL_MONK] &&
              partymenu_thac0_table[SKILL_FIGHTER][0] == 0x27 &&
              partymenu_thac0_table[SKILL_THIEF][0] == 0x28 &&
              partymenu_thac0_table[SKILL_FIGHTER][12] == 0x33,
              "the experience, class-mask and THAC0 tables read right", detail);

        /* Every class runs out at column 12, which is why nothing ever asks for
         * column 13 and the level-13 read the C# left unguarded is dead. */
        {
            bool all_stop = true;

            for (int cls = 0; cls < SKILL_COUNT; cls++) {
                if (partymenu_exp_table[cls][PARTYMENU_EXP_LEVELS - 1] != -1) {
                    all_stop = false;
                }
            }

            check(all_stop, "and every class stops before the last column",
                  "so the level-13 read is unreachable");
        }
    }

    /* ---- the constitution bonus to one hit die ---- */
    {
        int f18, c18, m18, f15, f10, f3;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 18);
        f18 = partymenu_con_bonus(CLASS_FIGHTER);
        c18 = partymenu_con_bonus(CLASS_CLERIC);
        m18 = partymenu_con_bonus(CLASS_MAGIC_USER);

        set_stats(&p1, 12, 12, 12, 12, 15, 12);
        f15 = partymenu_con_bonus(CLASS_FIGHTER);

        set_stats(&p1, 12, 12, 12, 12, 10, 12);
        f10 = partymenu_con_bonus(CLASS_FIGHTER);

        set_stats(&p1, 12, 12, 12, 12, 3, 12);
        f3 = partymenu_con_bonus(CLASS_FIGHTER);

        snprintf(detail, sizeof(detail),
                 "con 18 fighter %d, cleric %d, magic-user %d; con 15 %d, "
                 "con 10 %d, con 3 %d", f18, c18, m18, f15, f10, f3);
        check(f18 == 4 && c18 == 2 && m18 == 2,
              "a warrior's constitution bonus is Con - 14 and everyone "
              "else's stops at two", detail);
        check(f15 == 1 && f10 == 0 && f3 == -2,
              "and 15 is worth one, 10 nothing and 3 a penalty of two",
              detail);

        /* Nothing catches a constitution below 3, so 2 falls through to the
         * clause meant for 17 and up. The original does this. */
        set_stats(&p1, 12, 12, 12, 12, 2, 12);
        snprintf(detail, sizeof(detail), "fighter %d, cleric %d",
                 partymenu_con_bonus(CLASS_FIGHTER),
                 partymenu_con_bonus(CLASS_CLERIC));
        check(partymenu_con_bonus(CLASS_FIGHTER) == -12 &&
              partymenu_con_bonus(CLASS_CLERIC) == 2,
              "a constitution below 3 falls through to the high clause, as the "
              "original left it", detail);
    }

    /* ---- the whole constitution adjustment, and the doubling a first-level
     * ranger's second hit die brings with it ---- */
    {
        int cleric, fighter, ranger, cleric_ranger, at_ceiling;

        partymenu_scene(&p1, CLASS_CLERIC, SKILL_CLERIC, 1, 18);
        cleric = partymenu_con_hp_adj(&p1);

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 18);
        fighter = partymenu_con_hp_adj(&p1);

        partymenu_scene(&p1, CLASS_RANGER, SKILL_RANGER, 1, 18);
        ranger = partymenu_con_hp_adj(&p1);

        partymenu_scene(&p1, CLASS_MC_C_R, SKILL_CLERIC, 1, 18);
        p1.class_level[SKILL_RANGER] = 1;
        cleric_ranger = partymenu_con_hp_adj(&p1);

        /* A cleric's hit dice stop at level 10, so a level 10 cleric is past
         * the test and collects nothing. */
        partymenu_scene(&p1, CLASS_CLERIC, SKILL_CLERIC, 10, 18);
        at_ceiling = partymenu_con_hp_adj(&p1);

        snprintf(detail, sizeof(detail),
                 "cleric %d, fighter %d, ranger %d, cleric/ranger %d, "
                 "level 10 cleric %d", cleric, fighter, ranger, cleric_ranger,
                 at_ceiling);
        check(cleric == 2 && fighter == 4 && ranger == 8 && at_ceiling == 0,
              "constitution 18 is worth 2, a warrior's 4, and a first-level "
              "ranger's 8", detail);

        /* The doubling multiplies the running total rather than the ranger's
         * own share, so the cleric's 2 is doubled along with it: 2 + 2 = 4,
         * then 8. A straight ranger reaches 8 by the same route from 4. */
        check(cleric_ranger == 8,
              "and it doubles what the classes before it added, not just the "
              "ranger's share", detail);
    }

    /* ---- the floor and the ceiling Modify holds the hit points between ---- */
    {
        int f1, r1, f5_weak, f1_weak;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 10);
        f1 = partymenu_min_hit_points(&p1);

        partymenu_scene(&p1, CLASS_RANGER, SKILL_RANGER, 1, 10);
        r1 = partymenu_min_hit_points(&p1);

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 5, 3);
        f5_weak = partymenu_min_hit_points(&p1);

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 3);
        f1_weak = partymenu_min_hit_points(&p1);

        snprintf(detail, sizeof(detail),
                 "fighter 1 %d, ranger 1 %d, weak fighter 5 %d, weak "
                 "fighter 1 %d", f1, r1, f5_weak, f1_weak);
        check(f1 == 1 && r1 == 2 && f5_weak == 3 && f1_weak == 1,
              "the fewest hit points is one a level, two for a ranger, and "
              "never less than one", detail);
    }

    {
        int f1, f1_strong, f10, f_and_t;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 10);
        f1 = partymenu_calc_max_hp(&p1);

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 18);
        f1_strong = partymenu_calc_max_hp(&p1);

        /* Level 10 is the fighter's hit-dice ceiling, so the flat figure and
         * its per-level increment take over: 0x5A + 3. */
        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 10, 10);
        f10 = partymenu_calc_max_hp(&p1);

        /* A thief's dice stop at 11. The fighter is walked first and adds its
         * 10; the thief is past its ceiling and assigns 0x3C + 2 over the top,
         * throwing the fighter's share away. 62 / 2 classes = 31, where
         * accumulating would have given 36. The original does this. */
        partymenu_scene(&p1, CLASS_MC_F_T, SKILL_FIGHTER, 1, 10);
        p1.class_level[SKILL_THIEF] = 11;
        f_and_t = partymenu_calc_max_hp(&p1);

        snprintf(detail, sizeof(detail),
                 "fighter 1 %d, strong fighter 1 %d, fighter 10 %d, "
                 "fighter 1/thief 11 %d", f1, f1_strong, f10, f_and_t);
        check(f1 == 10 && f1_strong == 14 && f10 == 93,
              "the most hit points is the die plus the constitution bonus, "
              "and a flat figure past the class's ceiling", detail);
        check(f_and_t == 31,
              "and a class past its ceiling wipes out what the classes before "
              "it contributed", detail);
    }

    /* ---- one level's hit dice, best of two rolls ---- */
    {
        int f_lo = 99, f_hi = 0;
        int r_lo = 99, r_hi = 0;
        int t_only_lo = 99, t_only_hi = 0;
        int at_ceiling;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 10);

        for (int i = 0; i < 400; i++) {
            int roll = partymenu_roll_hit_points(0xff, &p1);

            f_lo = COAB_MIN(f_lo, roll);
            f_hi = COAB_MAX(f_hi, roll);
        }

        partymenu_scene(&p1, CLASS_RANGER, SKILL_RANGER, 1, 10);

        for (int i = 0; i < 400; i++) {
            int roll = partymenu_roll_hit_points(0xff, &p1);

            r_lo = COAB_MIN(r_lo, roll);
            r_hi = COAB_MAX(r_hi, roll);
        }

        /* The mask is what stops a multi-class character rolling for the class
         * the trainer did not teach: only the thief's d6 shows here. */
        partymenu_scene(&p1, CLASS_MC_F_T, SKILL_FIGHTER, 1, 10);
        p1.class_level[SKILL_THIEF] = 1;

        for (int i = 0; i < 400; i++) {
            int roll = partymenu_roll_hit_points(
                partymenu_class_masks[SKILL_THIEF], &p1);

            t_only_lo = COAB_MIN(t_only_lo, roll);
            t_only_hi = COAB_MAX(t_only_hi, roll);
        }

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 10, 10);
        at_ceiling = partymenu_roll_hit_points(0xff, &p1);

        snprintf(detail, sizeof(detail),
                 "fighter %d-%d, ranger %d-%d, thief of a fighter/thief %d-%d, "
                 "fighter at its ceiling %d", f_lo, f_hi, r_lo, r_hi,
                 t_only_lo, t_only_hi, at_ceiling);
        check(f_lo >= 1 && f_hi == 10 && r_lo >= 2 && r_hi == 16 &&
              t_only_lo >= 1 && t_only_hi == 6 && at_ceiling == 3,
              "a level's hit dice are the better of two rolls, and the mask "
              "picks which classes roll", detail);
    }

    /* ---- one line of the modify screen, highlighted and not ---- */
    {
        int stat_on, hp_on, name_on, name_off;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        clear_screen_raw();
        viewplayer_display_full(&p1);

        partymenu_draw_highlight_stat(true, 0, 1);
        stat_on = cell_ink(7, 1, 7, 0x0b);

        partymenu_draw_highlight_stat(true, 6, 1);
        hp_on = cell_ink(0x12, 1, 0x12, 0x10);

        partymenu_draw_highlight_stat(true, 7, 3);
        name_on = cell_ink(1, 1, 1, 0x10);

        partymenu_draw_highlight_stat(false, 7, 3);
        name_off = cell_ink(1, 1, 1, 0x10);

        snprintf(detail, sizeof(detail),
                 "stat %d px, hit points %d px, name %d px highlighted and "
                 "%d px not", stat_on, hp_on, name_on, name_off);
        check(stat_on > 20 && hp_on > 20 && name_on > 20 && name_off > 20,
              "the modify highlight draws a stat, the hit points and the name",
              detail);

        /* Past the end of the name the cursor has no character to redraw and
         * shows a '%' instead, so a cursor at 6 over a five-letter name still
         * puts ink in column 6. */
        clear_screen_raw();
        viewplayer_display_full(&p1);
        partymenu_draw_highlight_stat(true, 7, 6);

        snprintf(detail, sizeof(detail), "%d px in column 6",
                 cell_ink(1, 6, 1, 6));
        check(cell_ink(1, 6, 1, 6) > 4,
              "and past the end of the name the cursor draws a '%' of its own",
              detail);

        dump(out_dir, "partymenu-modify.ppm");
    }

    /* ---- who may be modified at all ---- */
    {
        bool kept_keys, kept_keys_dual;

        /* The refusal goes out through text_display_status, which clears the
         * prompt area again on its way home, so there is nothing left on the
         * screen to count. What shows the routine turned round at the door is
         * that the queued key is still there: had it reached the highlight it
         * would have read one. */
        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        p1.exp = 30000;
        cheats.allow_player_modify = false;
        clear_screen_raw();
        platform_clear_keys();
        platform_push_key('K');

        partymenu_modify_player();
        kept_keys = platform_key_queue_empty() == false;
        platform_clear_keys();

        snprintf(detail, sizeof(detail), "strength still %d, key %sread",
                 p1.stats.value[PSTAT_STR].full, kept_keys ? "un" : "");
        check(kept_keys && p1.stats.value[PSTAT_STR].full == 12,
              "a character who has earned anything can't be modified", detail);

        /* And nor can one who has taken a second class, whatever the
         * experience: 25000 is one of the four starting figures the gate lets
         * through, so here it is only the second class that stops them. */
        p1.exp = 25000;
        p1.multiclass_level = 4;
        clear_screen_raw();
        platform_push_key('K');

        partymenu_modify_player();
        kept_keys_dual = platform_key_queue_empty() == false;
        platform_clear_keys();
        p1.multiclass_level = 0;

        check(kept_keys_dual, "and nor can one who has dual-classed",
              "the same refusal, on starting experience");
    }

    /* ---- and driving the modify screen ---- */
    {
        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        p1.exp = 25000;
        clear_screen_raw();

        /* The highlight opens on strength: the right cursor key raises it and
         * Keep settles for the result. */
        platform_push_key(0x4d00);
        platform_push_key('K');
        partymenu_modify_player();

        snprintf(detail, sizeof(detail), "strength %d, treasure share %d",
                 p1.stats.value[PSTAT_STR].full,
                 (int)p1.npc_treasure_share_count);
        check(p1.stats.value[PSTAT_STR].full == 13 &&
              p1.npc_treasure_share_count == 1,
              "the right cursor key raises the highlighted stat and Keep "
              "settles for it", detail);

        /* Escape puts everything back, name and hit points included. */
        platform_push_key(0x4d00);
        platform_push_key(0x4d00);
        platform_push_key(0x1b);
        partymenu_modify_player();

        snprintf(detail, sizeof(detail), "strength back to %d",
                 p1.stats.value[PSTAT_STR].full);
        check(p1.stats.value[PSTAT_STR].full == 13,
              "and Escape puts the stats back where they were", detail);

        /* End walks the highlight down a line at a time - not the down cursor
         * key, which the original never looked at - and six of them from
         * strength reach the hit points, where the cursor keys move the total
         * instead. The ceiling for a level 1 fighter with constitution 12 is its
         * d10 plus nothing, so a 5 can go up. */
        p1.hit_point_max = 5;
        p1.hit_point_current = 5;
        platform_push_key(0x4f00);      /* End: the next line down */
        platform_push_key(0x4f00);
        platform_push_key(0x4f00);
        platform_push_key(0x4f00);
        platform_push_key(0x4f00);
        platform_push_key(0x4f00);      /* six of them is the hit-point line */
        platform_push_key(0x4d00);      /* right: one more hit point */
        platform_push_key('K');
        partymenu_modify_player();

        snprintf(detail, sizeof(detail), "%d hit points, %d rolled",
                 (int)p1.hit_point_max, (int)p1.hit_point_rolled);
        check(p1.hit_point_max == 6 && p1.hit_point_current == 6,
              "and the hit-point line takes the cursor keys as well", detail);

        /* The same six lines with the down arrow, which the original ignored on
         * this screen: it is the one place its choice of keys was plainly a
         * mistake rather than a convention. */
        p1.hit_point_max = 5;
        p1.hit_point_current = 5;
        for (int i = 0; i < 6; i++) {
            platform_push_key(0x5000);  /* down */
        }
        platform_push_key(0x4d00);
        platform_push_key('K');
        partymenu_modify_player();

        snprintf(detail, sizeof(detail), "%d hit points", (int)p1.hit_point_max);
        check(p1.hit_point_max == 6 && p1.hit_point_current == 6,
              "and the down arrow walks the highlight the way end does", detail);
    }

    /* ---- training, and the three ways it can be refused ---- */
    {
        cheats.free_training = false;
        gbl.game_won = false;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        p1.exp = 5000;
        p1.health_status = STATUS_DEAD;
        gbl.area2_ptr->training_class_mask = partymenu_class_masks[SKILL_FIGHTER];
        clear_screen_raw();

        partymenu_train_player();

        /* Each refusal is one text_display_status, which wipes the prompt area
         * again behind itself, so what there is to check is that the level and
         * the purse are where they were. */
        check(p1.class_level[SKILL_FIGHTER] == 1,
              "the dead are not trained", "no level gained");

        p1.health_status = STATUS_OKEY;
        money_set(&p1.money, MONEY_GOLD, 10);
        clear_screen_raw();

        partymenu_train_player();

        snprintf(detail, sizeof(detail), "level %d, %d gold left",
                 (int)p1.class_level[SKILL_FIGHTER],
                 money_gold_worth(&p1.money));
        check(p1.class_level[SKILL_FIGHTER] == 1 &&
              money_gold_worth(&p1.money) == 10,
              "and nor is anyone who cannot find the thousand gold", detail);

        money_set(&p1.money, MONEY_GOLD, 5000);
        gbl.area2_ptr->training_class_mask =
            partymenu_class_masks[SKILL_MAGIC_USER];
        clear_screen_raw();

        partymenu_train_player();

        check(p1.class_level[SKILL_FIGHTER] == 1,
              "and a trainer only teaches the classes it knows", "");
    }

    {
        int gold_before;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        p1.exp = 5000;
        p1.hit_point_max = 8;
        p1.hit_point_current = 5;
        gbl.area2_ptr->training_class_mask = partymenu_class_masks[SKILL_FIGHTER];
        gold_before = money_gold_worth(&p1.money);
        clear_screen_raw();

        platform_push_key('Y');
        partymenu_train_player();

        snprintf(detail, sizeof(detail),
                 "level %d, %d hit points of %d, %d gold from %d",
                 (int)p1.class_level[SKILL_FIGHTER], (int)p1.hit_point_current,
                 (int)p1.hit_point_max, money_gold_worth(&p1.money),
                 gold_before);
        check(p1.class_level[SKILL_FIGHTER] == 2 &&
              p1.hit_point_max > 8 &&
              (p1.hit_point_max - p1.hit_point_current) == 3 &&
              money_gold_worth(&p1.money) == gold_before - 1000,
              "a level costs a thousand gold, adds hit points and carries the "
              "damage over", detail);
        dump(out_dir, "partymenu-train.ppm");
    }

    /* ---- and the silent run a newly rolled character is put through ---- */
    {
        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        p1.exp = 25000;

        partymenu_silent_train_player();

        /* 18001 buys the fifth level and 35001 the sixth, so 25000 stops at
         * five. */
        snprintf(detail, sizeof(detail), "level %d, %d hit points",
                 (int)p1.class_level[SKILL_FIGHTER], (int)p1.hit_point_max);
        check(p1.class_level[SKILL_FIGHTER] == 5 && gbl.can_train_no_more &&
              gbl.silent_training == false,
              "silent training spends the starting experience and stops",
              detail);
    }

    /* ---- a magic-user is handed the spells the silent run cannot ask for ---- */
    {
        partymenu_scene(&p1, CLASS_MAGIC_USER, SKILL_MAGIC_USER, 1, 12);
        p1.exp = 25000;

        partymenu_silent_train_player();

        snprintf(detail, sizeof(detail),
                 "level %d, magic missile %d, fireball %d",
                 (int)p1.class_level[SKILL_MAGIC_USER],
                 player_knows_spell(&p1, SPELL_MAGIC_MISSILE),
                 player_knows_spell(&p1, SPELL_FIREBALL));
        check(p1.class_level[SKILL_MAGIC_USER] == 5 &&
              player_knows_spell(&p1, SPELL_MAGIC_MISSILE) &&
              player_knows_spell(&p1, SPELL_FIREBALL),
              "and hands a magic-user the spells nobody is there to choose",
              detail);
    }

    /* ---- taking a character off the team list ---- */
    {
        Player *left;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        partymenu_scene(&p2, CLASS_CLERIC, SKILL_CLERIC, 1, 12);
        partymenu_scene(&p3, CLASS_THIEF, SKILL_THIEF, 1, 12);
        snprintf(p2.name, sizeof(p2.name), "Dragonbait");
        snprintf(p3.name, sizeof(p3.name), "Olive");
        p1.icon_id = 0;
        p2.icon_id = 1;
        p3.icon_id = 2;

        gbl.team_count = 0;
        gbl_team_add(&p1);
        gbl_team_add(&p2);
        gbl_team_add(&p3);
        gbl.area2_ptr->party_size = 3;
        gbl.selected_player = &p2;

        left = partymenu_free_current_player(&p2, true, false);

        snprintf(detail, sizeof(detail), "%d left, %s selected, party size %d",
                 gbl.team_count, (left != NULL) ? left->name : "(nobody)",
                 (int)gbl.area2_ptr->party_size);
        check(gbl.team_count == 2 && left == &p1 &&
              gbl.area2_ptr->party_size == 2,
              "freeing a character selects the one before them", detail);

        /* Freeing the first selects the first again - there is nothing before
         * it - and a character who was never on the list is left alone. */
        left = partymenu_free_current_player(&p1, true, true);
        snprintf(detail, sizeof(detail),
                 "%d left, %s selected, party size still %d", gbl.team_count,
                 (left != NULL) ? left->name : "(nobody)",
                 (int)gbl.area2_ptr->party_size);
        check(gbl.team_count == 1 && left == &p3 &&
              gbl.area2_ptr->party_size == 2 &&
              partymenu_free_current_player(&p2, false, false) == NULL,
              "and leave_party_size holds the count where it is", detail);
    }

    /* ---- dropping a character, and thinking better of it ---- */
    {
        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        gbl.team_count = 0;
        gbl_team_add(&p1);
        gbl.area2_ptr->party_size = 1;
        gbl.selected_player = &p1;
        clear_screen_raw();

        platform_push_key('Y');
        platform_push_key('N');
        partymenu_drop_player();

        check(gbl.team_count == 1 && gbl.selected_player == &p1,
              "answering no to the second question keeps the character", "");

        platform_push_key('Y');
        platform_push_key('Y');
        partymenu_drop_player();

        snprintf(detail, sizeof(detail), "%d left, %s selected", gbl.team_count,
                 (gbl.selected_player != NULL) ? gbl.selected_player->name
                                               : "(nobody)");
        check(gbl.team_count == 0 && gbl.selected_player == NULL,
              "and answering yes twice drops them", detail);
    }

    /* ---- the combat icon slot a character being added is given ---- */
    {
        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        partymenu_scene(&p2, CLASS_CLERIC, SKILL_CLERIC, 1, 12);
        p1.icon_id = 0;
        p2.icon_id = 2;

        gbl.team_count = 0;
        gbl_team_add(&p1);
        gbl_team_add(&p2);
        gbl.area2_ptr->party_size = 2;

        partymenu_scene(&p3, CLASS_THIEF, SKILL_THIEF, 1, 12);
        partymenu_assign_player_icon_id(&p3);

        snprintf(detail, sizeof(detail),
                 "slot %d, %d on the team, party size %d", (int)p3.icon_id,
                 gbl.team_count, (int)gbl.area2_ptr->party_size);
        check(p3.icon_id == 1 && gbl.team_count == 3 &&
              gbl.selected_player == &p3 && gbl.area2_ptr->party_size == 3,
              "a character joining takes the lowest icon slot nobody holds",
              detail);
    }

    /* ---- Add Character with nothing to add ---- */
    {
        gbl.team_count = 0;
        gbl.selected_player = NULL;
        clear_screen_raw();

        platform_push_key('C');
        partymenu_add_player();

        check(gbl.team_count == 0,
              "Add Character comes back empty-handed with nothing saved",
              "the self-test's save directory holds no characters yet");

        /* Exit at the where-from question never gets as far as the list. */
        platform_push_key('E');
        partymenu_add_player();
        check(gbl.team_count == 0, "and Exit leaves without asking", "");
    }

    /* ---- the editor's four sprites, and a slot that cannot be copied ---- */
    {
        int in_the_way, between, first_draw, second_draw;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        clear_screen_raw();

        /* Something in the way first, so the clearing can be seen to happen. The
         * block a title at row 4, column 1 clears is 24 lines from pixel 104 and
         * twelve cells from pixel 32 - cell rows 13 to 15, columns 4 to 15 - and
         * the two sprites land in the three cells at either end of it, leaving
         * columns 7 to 12 as the strip that only the clearing touches. */
        text_display_string("XXXXXXXXXXXX", 0, 15, 13, 4);
        text_display_string("XXXXXXXXXXXX", 0, 15, 15, 4);
        in_the_way = cell_ink(13, 7, 15, 12);

        partymenu_draw_icon_editor_icons(4, 1);
        between    = cell_ink(13, 7, 15, 12);
        first_draw = cell_ink(13, 4, 15, 15);

        /* Neither slot exists - one is one past the end of the eight, the other
         * is below zero - so both copies are turned away rather than written
         * through, and slot 12 draws the same as it did a moment ago. */
        partymenu_duplicate_combat_icon(true, GBL_COMBAT_ICON_COUNT, 12);
        partymenu_duplicate_combat_icon(true, 12, -1);
        partymenu_draw_icon_editor_icons(4, 1);
        second_draw = cell_ink(13, 4, 15, 15);

        snprintf(detail, sizeof(detail),
                 "%d px in the way, %d left between the sprites, %d px drawn "
                 "and %d again", in_the_way, between, first_draw, second_draw);
        check(in_the_way > 50 && between == 0 && second_draw == first_draw,
              "the icon editor's block is cleared and an out-of-range slot is "
              "refused", detail);
    }

    /* ---- and the menu itself: the twelve lines, and BEGIN Adventuring ---- */
    {
        int rows_lit = 0;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        gbl.team_count = 0;
        gbl_team_add(&p1);
        gbl.area2_ptr->party_size = 1;
        gbl.selected_player = &p1;
        gbl.area2_ptr->training_class_mask = 0;
        gbl.area_ptr->field_3FA = 0;
        gbl.game_saved = true;
        cheats.free_training = false;

        /* BEGIN Adventuring puts the map frame back on its way out, which would
         * paint over the very lines this is counting. Both of its redraws are
         * skipped when the ECL is being reloaded and the area has a block
         * already, which is the one way out that leaves the menu on the screen
         * to be looked at. */
        gbl.reload_ecl_and_pictures = true;
        gbl.area_ptr->last_ecl_block_id = 1;
        gbl.last_dax_block_id = 0x50;
        clear_screen_raw();

        platform_push_key('B');
        partymenu_start_game_menu();

        /* Column 2 is where frames_draw_outer's inner rule sits, so the count
         * starts at 3 - the letter in white is at 2 and the rest of the line
         * from 3, so a lit line still shows. */
        for (int y = 12; y < 23; y++) {
            if (cell_ink(y, 3, y, 0x1e) > 0) {
                rows_lit++;
            }
        }

        snprintf(detail, sizeof(detail), "%d lines lit, game state %d, "
                 "training mask %d", rows_lit, (int)gbl.game_state,
                 (int)gbl.area2_ptr->training_class_mask);
        check(rows_lit == 9 && gbl.game_state == GAME_STATE_START_GAME_MENU &&
              gbl.area2_ptr->training_class_mask == 0,
              "the menu offers nine of the twelve entries with a party and no "
              "trainer, and BEGIN returns", detail);

        frame_stats(&nonzero, &colors);
        dump(out_dir, "partymenu-menu.ppm");
        snprintf(detail, sizeof(detail), "%d px, %d colours -> "
                 "partymenu-menu.ppm", nonzero, colors);
        check(nonzero > 3000 && colors >= 3,
              "and is drawn in the menu's two colours over the border", detail);
    }

    /* ---- a trainer in the room adds Train Character, and a human who has
     * not dual-classed yet adds Human Change Classes as well ---- */
    {
        int rows_lit = 0;

        partymenu_scene(&p1, CLASS_FIGHTER, SKILL_FIGHTER, 1, 12);
        gbl.team_count = 0;
        gbl_team_add(&p1);
        gbl.area2_ptr->party_size = 1;
        gbl.selected_player = &p1;
        gbl.area2_ptr->training_class_mask = 0xff;
        gbl.area_ptr->field_3FA = 0;
        gbl.reload_ecl_and_pictures = true;
        gbl.area_ptr->last_ecl_block_id = 1;
        gbl.last_dax_block_id = 0x50;
        clear_screen_raw();

        platform_push_key('B');
        partymenu_start_game_menu();

        for (int y = 12; y < 23; y++) {
            if (cell_ink(y, 3, y, 0x1e) > 0) {
                rows_lit++;
            }
        }

        snprintf(detail, sizeof(detail), "%d lines lit, and %s could take a "
                 "second class", rows_lit,
                 player_can_duel_class(&p1) ? "the fighter" : "nobody");
        check(rows_lit == 11 && player_can_duel_class(&p1),
              "a trainer in the room adds Train Character and Human Change "
              "Classes", detail);
    }

    gbl.team_count      = old_team_count;
    gbl.selected_player = old_selected;
    gbl.game_state      = old_state;
    gbl.game_speed_var  = old_speed;
    gbl.in_demo         = old_in_demo;
    gbl.area2_ptr->training_class_mask = old_train_mask;
    gbl.area2_ptr->party_size          = old_party_size;
    gbl.display_input_seconds_to_wait  = old_wait;
    gbl.display_input_timeout_value    = old_timeout;
    gbl.reload_ecl_and_pictures        = old_reload;
    gbl.last_dax_block_id              = old_dax_block;
    gbl.game_saved                     = old_game_saved;
    gbl.area_ptr->last_ecl_block_id    = old_ecl_block;
    gbl.area_ptr->field_3FA            = old_field_3fa;
    platform_set_key_typed_mode(false);
    platform_clear_keys();

    printf("\n");
}

/* ------------------------------------------------------------- savegame.c */

/* Everything below writes real files, so it writes them into a save directory of
 * the self-test's own under the image output directory. The player's saves are
 * never touched, and the directory is emptied on the way in so a previous run
 * cannot change what the character lists come back with. */

static void savegame_purge_visit(const char *name, void *user)
{
    char path[512];

    (void)user;
    file_delete(vfs_save_path(path, sizeof(path), name));
}

static void savegame_purge(void)
{
    vfs_for_each_save_file(NULL, savegame_purge_visit, NULL);
}

/* A character worth writing out: a name with the trailing spaces the record pads
 * with, known stats, one item and one affect. */
static void savegame_character(Player *p, const char *name, int con, u8 control)
{
    Item it;
    Affect a;

    player_init(p);
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->cls  = CLASS_FIGHTER;
    p->race = RACE_DWARF;
    p->class_level[SKILL_FIGHTER] = 3;
    p->hit_dice = 3;
    p->hit_point_max = 27;
    p->hit_point_current = 27;
    p->exp = 4321;
    p->control_morale = control;
    p->health_status = STATUS_OKEY;
    p->icon_size = 2;      /* a dwarf would be 1; the combat icon needs one of them */
    set_stats(p, 17, 9, 9, 13, con, 11);

    item_init(&it, ITEM_LONG_SWORD, 0x10, 0x11, 0x12, 1, 0, true, 0, false, 60, 1,
              150, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
    player_item_add(p, &it);

    affect_init(&a, AFFECT_BLESS, 42, 1, true);
    affect_list_add(&p->affects, &a);
}

/* A file of `size` bytes with a Pascal name at `name_offset` and, unless
 * npc_offset is negative, one control byte - which is all the loadable-character
 * list reads out of a record. */
static bool savegame_write_raw(const char *name, size_t size, int name_offset,
                              const char *player_name, int npc_offset, u8 control)
{
    char path[512];
    u8  *buf = calloc(1, size);
    FILE *f;
    bool ok;

    if (buf == NULL) {
        return false;
    }

    sys_string_to_array(buf, (size_t)name_offset, 15, player_name);
    if (npc_offset >= 0) {
        buf[npc_offset] = control;
    }

    f = vfs_fopen(vfs_save_path(path, sizeof(path), name), "wb");
    if (f == NULL) {
        free(buf);
        return false;
    }
    ok = fwrite(buf, 1, size, f) == size;
    fclose(f);
    free(buf);
    return ok;
}

/* Whether the list holds an entry whose text starts with `prefix`. */
static bool savegame_list_has(const MenuList *l, const char *prefix)
{
    size_t len = strlen(prefix);

    for (int i = 0; i < l->count; i++) {
        if (strncmp(l->item[i].text, prefix, len) == 0) {
            return true;
        }
    }
    return false;
}

static void savegame_party_clear(void)
{
    while (gbl.team_count > 0) {
        Player *p = gbl.team_list[0];

        gbl_team_remove_at(0);

        if (roster_owns(p)) {
            roster_release(p);
        }
    }
    gbl.selected_player = NULL;
    gbl.area2_ptr->party_size = 0;
}

static void check_savegame(void)
{
    char detail[240];
    char path[512];
    Player saved;
    Player *loaded;
    MenuList paths, names;
    ImportSource old_import  = gbl.import_from;
    GameState old_state      = gbl.game_state;
    GameState old_last_state = gbl.last_game_state;
    Player *old_selected     = gbl.selected_player;
    u8   old_game_area       = gbl.game_area;
    int  old_map_x           = gbl.map_pos_x;
    int  old_map_y           = gbl.map_pos_y;
    u8   old_direction       = gbl.map_direction;
    bool old_game_saved      = gbl.game_saved;
    bool old_reload          = gbl.reload_ecl_and_pictures;
    int  old_wait            = gbl.display_input_seconds_to_wait;
    char old_timeout         = gbl.display_input_timeout_value;
    i16  old_in_dungeon      = gbl.area_ptr->in_dungeon;

    printf("saving and loading\n");

    savegame_purge();

    savegame_party_clear();
    gbl.import_from = IMPORT_SOURCE_CURSE;
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value   = '\0';
    platform_clear_keys();
    platform_set_key_typed_mode(true);

    /* ---- one character out and back in ---- */
    savegame_character(&saved, "Dorn", 16, 0);
    savegame_save_player("", &saved);

    check(file_exists(vfs_save_path(path, sizeof(path), "dorn.guy")) &&
          file_exists(vfs_save_path(path, sizeof(path), "dorn.swg")) &&
          file_exists(vfs_save_path(path, sizeof(path), "dorn.fx")),
          "a character is written as a .guy with a .swg and a .fx beside it",
          "clean_string lowercases the name and keeps eight characters");

    loaded = roster_alloc();
    savegame_import_char(loaded, "dorn.guy");

    snprintf(detail, sizeof(detail),
             "'%s', exp %ld, %d items, %d affects, con %d, hp %d/%d",
             loaded->name, (long)loaded->exp, loaded->item_count,
             loaded->affects.count,
             (int)loaded->stats.value[PSTAT_CON].cur,
             (int)loaded->hit_point_current, (int)loaded->hit_point_max);
    check(strcmp(loaded->name, "Dorn") == 0 &&
          loaded->exp == 4321 &&
          loaded->item_count == 1 &&
          loaded->affects.count == 1 &&
          loaded->stats.value[PSTAT_CON].cur == 16 &&
          loaded->hit_point_max == 27,
          "and comes back the same character", detail);

    snprintf(detail, sizeof(detail),
             "item type %d value %d plus %d, affect %d for %d minutes",
             (int)loaded->items[0].type, (int)loaded->items[0].value,
             (int)loaded->items[0].plus, loaded->affects.items[0].type,
             (int)loaded->affects.items[0].minutes);
    check(loaded->items[0].type == ITEM_LONG_SWORD &&
          loaded->items[0].value == 150 &&
          loaded->items[0].plus == 1 &&
          loaded->affects.items[0].type == AFFECT_BLESS &&
          loaded->affects.items[0].minutes == 42,
          "with the pack and the affect it was carrying", detail);

    /* ---- an empty pack leaves no stale file ---- */
    saved.item_count = 0;
    saved.affects.count = 0;
    /* Writing over a name that is already there asks first, which is what the
     * 'Y' answers; a saved game's own CHRDAT files are never asked about. */
    platform_push_key('Y');
    savegame_save_player("", &saved);

    check(file_exists(vfs_save_path(path, sizeof(path), "dorn.guy")) &&
          !file_exists(vfs_save_path(path, sizeof(path), "dorn.swg")) &&
          !file_exists(vfs_save_path(path, sizeof(path), "dorn.fx")),
          "a character who has dropped everything keeps no old pack",
          "the .swg and .fx are deleted before either is written");

    /* ---- looking a character up by the name inside the file ---- */
    check(savegame_player_file_exists(".guy", "Dorn") &&
          !savegame_player_file_exists(".guy", "Nobody") &&
          !savegame_player_file_exists(".cha", "Dorn"),
          "a character can be found by the name in the record",
          "and only among the files of the extension asked for");

    savegame_remove_player_file(&saved);
    check(!file_exists(vfs_save_path(path, sizeof(path), "dorn.guy")),
          "and removing a character takes the file with it", "dorn.guy is gone");

    /* ---- what the Add Character list offers ---- */
    savegame_purge();
    roster_release(loaded);

    savegame_character(&saved, "Alias", 15, 0);
    savegame_save_player("", &saved);
    savegame_character(&saved, "Dragonbait", 14, 0);
    savegame_save_player("", &saved);
    savegame_character(&saved, "Olive", 13, CONTROL_NPC_BASE + 5);
    savegame_save_player("", &saved);
    /* Too short to be a character record, so nothing past the name is read from
     * it - which is why it is written with no control byte at all. */
    savegame_write_raw("stub.guy", 64, 0, "Stub", -1, 0);

    menu_list_clear(&paths);
    menu_list_clear(&names);
    savegame_build_loadable_players_lists(&paths, &names);

    snprintf(detail, sizeof(detail), "%d offered, %d file names",
             names.count, paths.count);
    check(names.count == 2 && paths.count == 2 &&
          savegame_list_has(&names, "Alias") &&
          savegame_list_has(&names, "Dragonbait") &&
          !savegame_list_has(&names, "Olive") &&
          !savegame_list_has(&names, "Stub"),
          "the loadable list drops the npc and the file of the wrong length",
          detail);

    check(savegame_list_has(&paths, "alias.guy") &&
          savegame_list_has(&paths, "dragonba.guy"),
          "and names the file each entry would be read from",
          "an eight-character name, as clean_string leaves it");

    /* A character already in the party is not offered a second time. */
    loaded = roster_alloc();
    savegame_import_char(loaded, "alias.guy");
    gbl_team_add(loaded);

    savegame_build_loadable_players_lists(&paths, &names);
    snprintf(detail, sizeof(detail), "%d offered with Alias in the party",
             names.count);
    check(names.count == 1 && savegame_list_has(&names, "Dragonbait") &&
          !savegame_list_has(&names, "Alias"),
          "and drops whoever is in the party already", detail);

    /* ---- the other two games' records ---- */
    savegame_purge();
    savegame_party_clear();
    roster_release(loaded);

    gbl.import_from = IMPORT_SOURCE_POOL;
    savegame_write_raw("shal.cha", POOL_RAD_RECORD_SIZE, 0, "Shal", 0x84, 0);
    savegame_write_raw("savgam3.sav", POOL_RAD_RECORD_SIZE, 0, "Ren", 0x84, 0);
    savegame_write_raw("boss.cha", POOL_RAD_RECORD_SIZE, 0, "Boss", 0x84, 0x80);
    savegame_build_loadable_players_lists(&paths, &names);

    snprintf(detail, sizeof(detail), "%d offered: '%s'%s'%s'", names.count,
             (names.count > 0) ? names.item[0].text : "",
             (names.count > 1) ? ", " : "",
             (names.count > 1) ? names.item[1].text : "");
    check(names.count == 2 &&
          savegame_list_has(&names, "Shal") &&
          savegame_list_has(&names, "Ren") &&
          !savegame_list_has(&names, "Boss"),
          "a Pool of Radiance character is read out of a .cha and a .sav",
          detail);

    /* The saved-game letter comes out of the file's own name; the C# indexed
     * character 7 of the whole path, which was the save number only under DOS. */
    check(savegame_list_has(&names, "Ren             from save game 3"),
          "and one out of a saved game says which game", detail);

    gbl.import_from = IMPORT_SOURCE_HILLSFAR;
    savegame_write_raw("kern.hil", HILLS_FAR_RECORD_SIZE, 4, "Kern", -1, 0);
    savegame_build_loadable_players_lists(&paths, &names);

    snprintf(detail, sizeof(detail), "%d offered: '%s'", names.count,
             (names.count > 0) ? names.item[0].text : "");
    check(names.count == 1 && savegame_list_has(&names, "Kern"),
          "and a Hillsfar one has its name four bytes in, with no control byte",
          detail);

    /* ---- a whole game out and back in ---- */
    savegame_purge();
    savegame_party_clear();
    gbl.import_from = IMPORT_SOURCE_CURSE;

    for (int i = 0; i < 2; i++) {
        Player *p = roster_alloc();

        savegame_character(p, (i == 0) ? "Akabar" : "Jasmine", 15 - i, 0);
        p->icon_id = (u8)i;
        gbl_team_add(p);
    }
    gbl.area2_ptr->party_size = 2;
    gbl.selected_player = gbl.team_list[0];

    /* A loose .guy for one of them, to see the save take it away. */
    savegame_save_player("", gbl.team_list[0]);

    gbl.game_area   = 1;
    gbl.map_pos_x   = 7;
    gbl.map_pos_y   = 9;
    gbl.map_direction = 2;
    gbl.game_state  = GAME_STATE_WILDERNESS_MAP;
    gbl.last_game_state = GAME_STATE_START_GAME_MENU;
    gbl.area_ptr->in_dungeon = 0;
    gbl.set_blocks[0].set_id = 5;
    gbl.set_blocks[0].block_id = 6;
    gbl.game_saved  = false;
    ecl_vars_set(gbl.ecl_vars, 0, 0x1234);

    platform_push_key('A');
    savegame_save_game();

    check(file_exists(vfs_save_path(path, sizeof(path), "savgamA.dat")) &&
          file_exists(vfs_save_path(path, sizeof(path), "CHRDATA1.sav")) &&
          file_exists(vfs_save_path(path, sizeof(path), "CHRDATA2.sav")) &&
          gbl.game_saved,
          "a saved game is one file plus one per character",
          "and the game is marked saved");

    check(!file_exists(vfs_save_path(path, sizeof(path), "akabar.guy")),
          "and a character in a saved game is no longer a loose character",
          "the .guy is removed once they are in a game");

    savegame_party_clear();
    gbl.game_area = 0;
    gbl.map_pos_x = gbl.map_pos_y = 0;
    gbl.map_direction = 0;
    gbl.game_state = GAME_STATE_COMBAT;
    ecl_vars_set(gbl.ecl_vars, 0, 0);

    savegame_load_save_game("savgamA.dat");

    snprintf(detail, sizeof(detail),
             "%d in the party: '%s' and '%s', at %d,%d facing %d, area %d",
             gbl.team_count,
             (gbl.team_count > 0) ? gbl.team_list[0]->name : "",
             (gbl.team_count > 1) ? gbl.team_list[1]->name : "",
             gbl.map_pos_x, gbl.map_pos_y, (int)gbl.map_direction,
             (int)gbl.game_area);
    check(gbl.team_count == 2 &&
          strcmp(gbl.team_list[0]->name, "Akabar") == 0 &&
          strcmp(gbl.team_list[1]->name, "Jasmine") == 0 &&
          gbl.map_pos_x == 7 && gbl.map_pos_y == 9 && gbl.map_direction == 2,
          "loading it back brings the party and where it was standing", detail);

    snprintf(detail, sizeof(detail),
             "state %d, previous %d, set 0 is %d/%d, ecl var 0 is 0x%04x, "
             "party_size %d",
             (int)gbl.game_state, (int)gbl.last_game_state,
             gbl.set_blocks[0].set_id, gbl.set_blocks[0].block_id,
             (unsigned)ecl_vars_get(gbl.ecl_vars, 0),
             (int)gbl.area2_ptr->party_size);
    check(gbl.game_state == GAME_STATE_START_GAME_MENU &&
          gbl.last_game_state == GAME_STATE_WILDERNESS_MAP &&
          gbl.set_blocks[0].set_id == 5 && gbl.set_blocks[0].block_id == 6 &&
          ecl_vars_get(gbl.ecl_vars, 0) == 0x1234 &&
          gbl.area2_ptr->party_size == 2,
          "with the script's memory, the wall sets and the state it was saved in",
          detail);

    check(gbl.team_count > 0 && gbl.team_list[0]->item_count == 1 &&
          gbl.team_list[0]->affects.count == 1 &&
          gbl.selected_player == gbl.team_list[0],
          "and each character's own pack, affects and the selection",
          "the party comes back with the first character picked");

    /* ---- and a game saved in one chapter loading while another is loaded ----
     * The chapter is the number in every data file name the game reaches for, so
     * getting it back is what makes a save from later in the game loadable at
     * all. It is written twice, once as the first byte of savgam<letter>.dat and
     * once inside Area2, and read back in that order - the Area2 copy is the one
     * that wins. Both are checked, by saving in chapter 3 while chapter 1 is the
     * one loaded and then loading it back the other way round. */
    {
        gbl.game_area        = 3;
        gbl.area_ptr->in_dungeon = 1;   /* so no chapter's art is loaded here */
        gbl.game_state       = GAME_STATE_DUNGEON_MAP;
        gbl.set_blocks[0].block_id = 0; /* nor any wall set */
        gbl.game_speed_var   = 7;
        ecl_vars_set(gbl.ecl_vars, 1, 0x5678);

        platform_push_key('B');
        savegame_save_game();

        savegame_party_clear();
        gbl.game_area              = 1;
        gbl.area2_ptr->game_area   = 1;
        gbl.game_speed_var         = 4;
        gbl.reload_ecl_and_pictures = false;
        ecl_vars_set(gbl.ecl_vars, 1, 0);

        savegame_load_save_game("savgamB.dat");

        snprintf(detail, sizeof(detail),
                 "chapter %d, Area2 says %d, speed %d, ecl var 1 is 0x%04x, "
                 "reload %s",
                 (int)gbl.game_area, (int)gbl.area2_ptr->game_area,
                 gbl.game_speed_var, (unsigned)ecl_vars_get(gbl.ecl_vars, 1),
                 gbl.reload_ecl_and_pictures ? "asked for" : "not asked for");
        check(gbl.game_area == 3 && gbl.area2_ptr->game_area == 3 &&
              gbl.game_speed_var == 7 &&
              ecl_vars_get(gbl.ecl_vars, 1) == 0x5678 &&
              gbl.reload_ecl_and_pictures,
              "a game saved in a later chapter loads back into that chapter",
              detail);
    }

    /* ---- nothing to load ---- */
    savegame_purge();
    savegame_party_clear();
    gbl.game_state = GAME_STATE_COMBAT;

    savegame_load_game_menu();
    check(gbl.game_state == GAME_STATE_COMBAT && gbl.team_count == 0,
          "and with no saved games at all the load menu does not appear",
          "no prompt, and nothing changed");

    savegame_purge();
    savegame_party_clear();

    gbl.import_from     = old_import;
    gbl.game_state      = old_state;
    gbl.last_game_state = old_last_state;
    gbl.selected_player = old_selected;
    gbl.game_area       = old_game_area;
    gbl.map_pos_x       = old_map_x;
    gbl.map_pos_y       = old_map_y;
    gbl.map_direction   = old_direction;
    gbl.game_saved      = old_game_saved;
    gbl.reload_ecl_and_pictures = old_reload;
    gbl.display_input_seconds_to_wait = old_wait;
    gbl.display_input_timeout_value   = old_timeout;
    gbl.area_ptr->in_dungeon = old_in_dungeon;
    platform_set_key_typed_mode(false);
    platform_clear_keys();

    printf("\n");
}

/* ------------------------------------------------------------- making camp */

static void check_camp(const char *out_dir)
{
    Player p1, p2;
    char detail[240];
    GameState old_state    = gbl.game_state;
    int  old_speed         = gbl.game_speed_var;
    Player *old_selected   = gbl.selected_player;
    i16  old_can_cast      = gbl.area_ptr->can_cast_spells;
    i16  old_block_view    = gbl.area_ptr->block_area_view;
    u8   old_party_size    = gbl.area2_ptr->party_size;
    int  old_wait          = gbl.display_input_seconds_to_wait;
    char old_timeout       = gbl.display_input_timeout_value;
    int  cure_critical, cure_serious, cure_light;
    int  minutes;
    int  nonzero, colors;

    printf("making camp\n");

    gbl.game_state = GAME_STATE_CAMPING;
    gbl.game_speed_var = 0;
    gbl.area_ptr->picture_fade    = 0;
    gbl.area_ptr->block_area_view = 0;
    gbl.area_ptr->can_cast_spells = 0;

    /* A prompt left without keys to read would spin for ever, so every one of
     * them has a way out: after a second of silence it answers '\0', which is
     * the Escape every menu here exits on. */
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value   = '\0';

    player_init(&p1);
    player_init(&p2);
    p1.field_125 = p2.field_125 = 1;
    set_strength_dex(&p1, 10, 0, 10);
    set_strength_dex(&p2, 10, 0, 10);
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    snprintf(p2.name, sizeof(p2.name), "%s", "Dragonbait");
    p1.hit_point_max = p1.hit_point_current = 200;
    p2.hit_point_max = p2.hit_point_current = 8;
    p1.health_status = p2.health_status = STATUS_OKEY;
    p1.in_combat = p2.in_combat = true;

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl_team_add(&p2);
    gbl.selected_player = &p1;
    gbl.area2_ptr->party_size = 2;
    world_clock_clear();
    rest_time_clear(&gbl.time_to_rest);
    gbl.rest_10_seconds = 0;

    /* --- the affect names the Display screen reads, built at startup out of
     * whichever spell lays each affect. */
    {
        const char *bless    = camp_effect_name(AFFECT_BLESS);
        const char *haste    = camp_effect_name(AFFECT_HASTE);
        const char *poisoned = camp_effect_name(AFFECT_POISONED);

        snprintf(detail, sizeof(detail), "\"%s\", \"%s\", \"%s\"",
                 bless != NULL ? bless : "(none)",
                 haste != NULL ? haste : "(none)",
                 poisoned != NULL ? poisoned : "(none)");
        check(bless != NULL && strcmp(bless, "Bless") == 0 &&
              haste != NULL && strcmp(haste, "Haste") == 0 &&
              poisoned != NULL && strcmp(poisoned, "Poisoned") == 0 &&
              camp_effect_name(AFFECT_NONE) == NULL,
              "an affect is named after the spell that lays it, or not at all",
              detail);
    }

    /* --- and the one the shipped spell table leaves without a name: nothing
     * below spell 0x38 lays animate_dead, so the C#'s "Funky--" + the enum name
     * is what a character under it is listed as. */
    {
        const char *animated = camp_effect_name(AFFECT_ANIMATE_DEAD);

        snprintf(detail, sizeof(detail), "\"%s\"",
                 animated != NULL ? animated : "(none)");
        check(animated != NULL && strcmp(animated, "Funky--animate_dead") == 0,
              "an affect no memorizable spell lays keeps the C#'s enum name",
              detail);
    }

    /* --- who is allowed to do what. Area1.can_cast_spells is the wrong way
     * round: it is set for an area where spells do not work. */
    {
        gbl.area_ptr->can_cast_spells = 1;
        check(!camp_can_use_spells(1) && camp_can_use_spells(2) &&
              camp_can_use_spells(3),
              "an area that forbids spells still lets them be memorized", NULL);

        /* And a character who is out of the fight cannot memorize or scribe -
         * but is still offered the spell list to cast from, because the
         * original's casting branch never reaches the health check. The refusal
         * comes later, from the casting code. */
        gbl.area_ptr->can_cast_spells = 0;
        p1.in_combat = false;

        check(camp_can_use_spells(1) && !camp_can_use_spells(2) &&
              !camp_can_use_spells(3),
              "and a character out of the fight is still offered Cast, as the "
              "original had it", NULL);

        p1.in_combat = true;
        p1.health_status = STATUS_ANIMATED;

        check(!camp_can_use_spells(2) && !camp_can_use_spells(3),
              "an animated character memorizes and scribes nothing", NULL);

        p1.health_status = STATUS_OKEY;
    }

    /* --- how long a character needs. Four minutes to settle down and a quarter
     * of an hour a spell level. */
    {
        int empty = camp_spell_learn_time(&p1);

        snprintf(detail, sizeof(detail), "%d minutes, %d hours to wait",
                 empty, p1.spell_to_learn_count);
        check(empty == 0 && p1.spell_to_learn_count == 0,
              "a character with nothing to learn needs no rest at all", detail);

        spell_list_clear(&p1.spell_list);
        spell_list_add_learn(&p1.spell_list, SPELL_MAGIC_MISSILE);
        spell_list_add_learn(&p1.spell_list, SPELL_SLEEP);

        minutes = camp_spell_learn_time(&p1);
        snprintf(detail, sizeof(detail), "%d minutes, count %d",
                 minutes, p1.spell_to_learn_count);
        check(minutes == (4 * 60) + (2 * 15) && p1.spell_to_learn_count == 4,
              "two first-level spells are four minutes and two quarter-hours",
              detail);

        /* Anything above second level makes it six rather than four. */
        spell_list_add_learn(&p1.spell_list, SPELL_FIREBALL);

        minutes = camp_spell_learn_time(&p1);
        snprintf(detail, sizeof(detail), "%d minutes, count %d",
                 minutes, p1.spell_to_learn_count);
        check(minutes == (6 * 60) + (5 * 15) && p1.spell_to_learn_count == 6,
              "a third-level spell makes it six minutes and five quarter-hours",
              detail);
    }

    /* --- and a scroll marked to be copied costs the same as memorising one. */
    {
        Item scroll;

        treasure_create_item(&scroll, ITEM_MU_SCROLL);
        scroll.namenum2 = 0xd3;         /* "With 2 Spells" */
        item_affect_set(&scroll, 1, (Affects)(0x80 | SPELL_MAGIC_MISSILE));
        item_affect_set(&scroll, 2, (Affects)(0x80 | SPELL_STINKING_CLOUD));
        item_affect_set(&scroll, 3, AFFECT_NONE);

        p1.item_count = 0;
        player_item_add(&p1, &scroll);

        minutes = camp_spell_learn_time(&p1);
        snprintf(detail, sizeof(detail), "%d minutes", minutes);
        check(minutes == (6 * 60) + ((5 + 1 + 2) * 15),
              "a scroll of a first and a second level spell adds three more",
              detail);
    }

    /* --- an interrupted camp loses the lot: the spells go off the list and the
     * scroll is left the way it was found. */
    {
        camp_cancel_spells();
        minutes = camp_spell_learn_time(&p1);

        snprintf(detail, sizeof(detail),
                 "%d being learnt, scroll holds 0x%02x and 0x%02x, %d minutes",
                 spell_list_learning_count(&p1.spell_list),
                 (unsigned)item_affect(&p1.items[0], 1),
                 (unsigned)item_affect(&p1.items[0], 2), minutes);
        check(spell_list_learning_count(&p1.spell_list) == 0 &&
              item_affect(&p1.items[0], 1) == (Affects)SPELL_MAGIC_MISSILE &&
              item_affect(&p1.items[0], 2) == (Affects)SPELL_STINKING_CLOUD &&
              p1.spell_to_learn_count == 0 && minutes == 0,
              "cancelling clears the list and unmarks the scroll", detail);
    }

    /* --- how many more spells of a level may be lined up. What is already on
     * the list counts, memorised or not. */
    {
        int before, after;

        memset(p1.spell_cast_count, 0, sizeof(p1.spell_cast_count));
        p1.spell_cast_count[SPELL_CLASS_MAGIC_USER][0] = 2;
        spell_list_clear(&p1.spell_list);

        before = camp_spells_can_learn(SPELL_CLASS_MAGIC_USER, 1);
        spell_list_add_learn(&p1.spell_list, SPELL_MAGIC_MISSILE);
        after = camp_spells_can_learn(SPELL_CLASS_MAGIC_USER, 1);

        snprintf(detail, sizeof(detail), "%d, then %d with one lined up",
                 before, after);
        check(before == 2 && after == 1 &&
              camp_spells_can_learn(SPELL_CLASS_MAGIC_USER, 2) == 0 &&
              camp_spells_can_learn(SPELL_CLASS_CLERIC, 1) == 0,
              "two slots less one already picked leaves one", detail);

        /* The original indexed spellCastCount with whatever it was handed; a
         * monster's spell class or a sixth-level spell is refused here instead
         * of running off the table. */
        check(camp_spells_can_learn(SPELL_CLASS_MONSTER, 1) == 0 &&
              camp_spells_can_learn(SPELL_CLASS_CLERIC, 6) == 0,
              "and there are no slots off the end of the table", NULL);

        camp_cancel_spells();
    }

    /* --- the Fix menu's arithmetic: how many cures the party has and how long
     * spending them takes. */
    {
        memset(p1.spell_cast_count, 0, sizeof(p1.spell_cast_count));
        memset(p2.spell_cast_count, 0, sizeof(p2.spell_cast_count));
        p1.spell_cast_count[SPELL_CLASS_CLERIC][0] = 2;
        p1.hit_point_current = 1;       /* 199 lost, more than the cures heal */

        camp_calculate_time_and_spell_numbers(&cure_critical, &cure_serious,
                                             &cure_light);

        snprintf(detail, sizeof(detail), "%d light, %d serious, %d critical, %d:%d%d",
                 cure_light, cure_serious, cure_critical,
                 gbl.time_to_rest.slot[REST_SLOT_HOURS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES]);
        check(cure_light == 2 && cure_serious == 0 && cure_critical == 0 &&
              gbl.time_to_rest.slot[REST_SLOT_HOURS] == 4 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] == 3 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] == 0,
              "two cure light wounds are four hours and half of one", detail);

        /* A fifth-level slot makes the settling down six hours rather than
         * four, and adds an hour and a quarter of its own. */
        p1.spell_cast_count[SPELL_CLASS_CLERIC][4] = 1;

        camp_calculate_time_and_spell_numbers(&cure_critical, &cure_serious,
                                             &cure_light);

        snprintf(detail, sizeof(detail), "%d critical, %d:%d%d",
                 cure_critical,
                 gbl.time_to_rest.slot[REST_SLOT_HOURS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES]);
        check(cure_critical == 1 &&
              gbl.time_to_rest.slot[REST_SLOT_HOURS] == 7 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] == 4 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] == 5,
              "a cure critical wounds on top of them is seven and three quarters",
              detail);

        /* A scratch is not worth a night: the whole rest is divided by how many
         * times over the spells would cover the damage. */
        p1.hit_point_current = p1.hit_point_max - 1;

        camp_calculate_time_and_spell_numbers(&cure_critical, &cure_serious,
                                             &cure_light);

        snprintf(detail, sizeof(detail), "%d:%d%d",
                 gbl.time_to_rest.slot[REST_SLOT_HOURS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES]);
        check(gbl.time_to_rest.slot[REST_SLOT_HOURS] == 0 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] == 0 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] == 4,
              "one hit point lost is four minutes rather than eight hours",
              detail);

        /* And nobody hurt at all is where the original divided by zero. The
         * guard leaves the time as it stood; FixTeam never gets this far. */
        p1.hit_point_current = p1.hit_point_max;

        camp_calculate_time_and_spell_numbers(&cure_critical, &cure_serious,
                                             &cure_light);

        snprintf(detail, sizeof(detail), "%d:%d%d",
                 gbl.time_to_rest.slot[REST_SLOT_HOURS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS],
                 gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES]);
        check(gbl.time_to_rest.slot[REST_SLOT_HOURS] == 7 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_TENS] == 4 &&
              gbl.time_to_rest.slot[REST_SLOT_MINUTES_ONES] == 5,
              "and an unhurt party is not divided by its own nothing", detail);

        memset(p1.spell_cast_count, 0, sizeof(p1.spell_cast_count));
        rest_time_clear(&gbl.time_to_rest);
    }

    platform_set_key_typed_mode(true);

    /* --- the game speed, driven from the keyboard: Slower twice, Faster once,
     * and Exit. */
    {
        platform_clear_keys();
        platform_push_key('s');
        platform_push_key('s');
        platform_push_key('f');
        platform_push_key('e');

        gbl.game_speed_var = 4;
        camp_game_speed();

        snprintf(detail, sizeof(detail), "speed %d", gbl.game_speed_var);
        check(gbl.game_speed_var == 5,
              "two slower and one faster leave the game a step slower", detail);

        /* The cursor keys are the same two commands - down faster, up slower -
         * and neither goes past the end. The keypad digits stand in for them:
         * '2' is down and '8' is up. */
        platform_clear_keys();
        for (int i = 0; i < 12; i++) {
            platform_push_key('2');
        }
        platform_push_key('e');

        camp_game_speed();

        snprintf(detail, sizeof(detail), "speed %d", gbl.game_speed_var);
        check(gbl.game_speed_var == 0,
              "twelve steps faster stop at the fastest there is", detail);

        platform_clear_keys();
        for (int i = 0; i < 12; i++) {
            platform_push_key('8');
        }
        platform_push_key('e');

        camp_game_speed();

        snprintf(detail, sizeof(detail), "speed %d", gbl.game_speed_var);
        check(gbl.game_speed_var == 9,
              "and twelve slower stop at the slowest", detail);

        gbl.game_speed_var = 0;
    }

    /* --- the marching order. Select, End to walk the character down the list,
     * Place, and out. */
    {
        platform_clear_keys();
        platform_push_key('o');         /* Order */
        platform_push_key('s');         /* Select */
        platform_push_key('1');         /* the keypad's End: down the order */
        platform_push_key('p');         /* Place */
        platform_push_key('e');         /* out of the order menu */
        platform_push_key('e');         /* and out of Alter */

        gbl.selected_player = &p1;
        camp_alter_menu();

        snprintf(detail, sizeof(detail), "%s then %s",
                 gbl.team_count > 0 ? gbl.team_list[0]->name : "(nobody)",
                 gbl.team_count > 1 ? gbl.team_list[1]->name : "(nobody)");
        check(gbl.team_count == 2 && gbl.team_list[0] == &p2 &&
              gbl.team_list[1] == &p1,
              "the selected character walks one place down the marching order",
              detail);

        /* Home takes them back, and from the front of the line it wraps round to
         * the back - which with two characters is the same swap again. */
        platform_clear_keys();
        platform_push_key('o');
        platform_push_key('s');
        platform_push_key('7');         /* the keypad's Home: up the order */
        platform_push_key('p');
        platform_push_key('e');
        platform_push_key('e');

        camp_alter_menu();

        snprintf(detail, sizeof(detail), "%s then %s",
                 gbl.team_count > 0 ? gbl.team_list[0]->name : "(nobody)",
                 gbl.team_count > 1 ? gbl.team_list[1]->name : "(nobody)");
        check(gbl.team_count == 2 && gbl.team_list[0] == &p1 &&
              gbl.team_list[1] == &p2,
              "and Home walks them back up it", detail);
    }

    /* --- Drop, refused. The party is left alone and told so. */
    {
        platform_clear_keys();
        platform_push_key('d');         /* Drop */
        platform_push_key('n');         /* Drop from party? No */
        platform_push_key('e');

        camp_alter_menu();

        snprintf(detail, sizeof(detail), "%d in the party", gbl.team_count);
        check(gbl.team_count == 2 && gbl.selected_player == &p1,
              "a drop the player thinks better of leaves the party as it was",
              detail);
    }

    /* --- resting to memorise. The rest is as long as the spells need, and the
     * spells are what the rest is for. */
    {
        bool interrupted;

        spell_list_clear(&p1.spell_list);
        spell_list_add_learn(&p1.spell_list, SPELL_MAGIC_MISSILE);
        spell_list_add_learn(&p1.spell_list, SPELL_SLEEP);
        spell_list_clear(&p2.spell_list);

        world_clock_clear();
        rest_time_clear(&gbl.time_to_rest);
        gbl.rest_10_seconds = 0;

        platform_clear_keys();
        platform_push_key('r');         /* Rest, for as long as it takes */

        interrupted = camp_rest_menu();

        snprintf(detail, sizeof(detail),
                 "%02d:%02d slept, %d memorized, %d still being learnt",
                 (int)gbl.area_ptr->time_hour, world_clock_minutes(),
                 spell_list_learnt_count(&p1.spell_list),
                 spell_list_learning_count(&p1.spell_list));
        check(!interrupted && rest_time_empty() &&
              gbl.area_ptr->time_hour == 4 && world_clock_minutes() == 30 &&
              spell_list_learnt_count(&p1.spell_list) == 2 &&
              spell_list_learning_count(&p1.spell_list) == 0,
              "four and a half hours is what two first-level spells cost",
              detail);
    }

    /* --- and the camp screen itself: Magic, Display, out of both, out of the
     * camp.
     *
     * The picture the party stopped in front of is remembered on the way in and
     * put back on the way out. Nothing in this script replaces it, and
     * picture_load_pic_final does nothing when the picture it is asked for is
     * already the current one, so what is checked here is that the name and
     * block survive the camp and that the frames are still there afterwards.
     * That the comparison matches at all is the one place this port does not do
     * what the C# did - see camp.c. */
    {
        bool interrupted;

        affect_list_clear(&p1.affects);
        effect_add_affect(false, 0, 30, AFFECT_BLESS, &p1);
        effect_add_affect(false, 0, 30, AFFECT_HASTE, &p1);
        affect_list_clear(&p2.affects);

        picture_dax_array_free_blocks(&gbl.pic_frames);
        gbl.last_dax_file[0]  = '\0';
        gbl.last_dax_block_id = 0xff;
        picture_load_pic_final(&gbl.pic_frames, 0, 0x1d, "PIC");

        platform_clear_keys();
        platform_push_key('m');         /* Magic */
        platform_push_key('d');         /* Display, everybody's affects */
        platform_push_key(0x1b);        /* Escape out of the list */
        platform_push_key('e');         /* out of the magic menu */
        platform_push_key('e');         /* and out of the camp */

        interrupted = camp_make_camp();

        dump(out_dir, "camp-screen.ppm");
        frame_stats(&nonzero, &colors);

        snprintf(detail, sizeof(detail),
                 "%s, %d frames of \"%s\" block 0x%02x, %d px, %d colours",
                 interrupted ? "interrupted" : "not interrupted",
                 gbl.pic_frames.num_frames, gbl.saved_dax_file,
                 gbl.saved_dax_block_id, nonzero, colors);
        check(!interrupted && gbl.game_state == GAME_STATE_CAMPING &&
              strcmp(gbl.saved_dax_file, "PIC") == 0 &&
              gbl.saved_dax_block_id == 0x1d &&
              strcmp(gbl.last_dax_file, "PIC") == 0 &&
              gbl.last_dax_block_id == 0x1d &&
              gbl.pic_frames.num_frames == 4 && nonzero > 500,
              "the camp opens, shows the party's spells and keeps the picture",
              detail);
    }

    platform_set_key_typed_mode(false);
    platform_clear_keys();

    affect_list_clear(&p1.affects);
    affect_list_clear(&p2.affects);
    p1.item_count = 0;
    world_clock_clear();
    rest_time_clear(&gbl.time_to_rest);
    gbl.rest_10_seconds = 0;
    gbl.team_count      = 0;
    gbl.selected_player = old_selected;
    gbl.display_player_status_line18 = false;
    gbl.display_input_seconds_to_wait = old_wait;
    gbl.display_input_timeout_value   = old_timeout;
    gbl.area_ptr->can_cast_spells = old_can_cast;
    gbl.area_ptr->block_area_view = old_block_view;
    gbl.area2_ptr->party_size     = old_party_size;
    gbl.game_state     = old_state;
    gbl.game_speed_var = old_speed;

    printf("\n");
}

/* ------------------------------------------------- the dungeon and its doors */

/* One locked door in the wall the party is facing, on an otherwise empty map,
 * and a fresh try at each of the three ways through it. The flags are the wall's
 * x3 nibble: 1 open, 2 locked, 3 locked and reinforced. */
static void set_locked_door(int flags)
{
    memset(gbl.geo_ptr, 0, sizeof(*gbl.geo_ptr));
    gbl.geo_ptr->maps[8][8].wall_type_dir_2 = 5;
    gbl.geo_ptr->maps[8][8].x3_dir_2        = (u8)flags;

    gbl.map_pos_x     = 8;
    gbl.map_pos_y     = 8;
    gbl.map_direction = 2;              /* east, towards the door */

    gbl.area2_ptr->field_592 = 0;
    gbl.can_bash_door  = true;
    gbl.can_pick_door  = true;
    gbl.can_knock_door = true;

    platform_clear_keys();
}

static void check_dungeon(const char *out_dir)
{
    Player p1;
    char detail[240];
    GameState old_state   = gbl.game_state;
    Player *old_selected  = gbl.selected_player;
    int  old_map_x        = gbl.map_pos_x;
    int  old_map_y        = gbl.map_pos_y;
    u8   old_direction    = gbl.map_direction;
    bool old_area_display = gbl.map_area_display;
    u16  old_search_addr  = gbl.search_location_addr;
    u16  old_search_flags = gbl.area2_ptr->search_flags;
    u16  old_ecl_offset   = gbl.ecl_offset;
    i16  old_field_592    = gbl.area2_ptr->field_592;
    i16  old_block_view   = gbl.area_ptr->block_area_view;
    u8   old_area         = gbl.game_area;
    int  old_wait         = gbl.display_input_seconds_to_wait;
    char old_timeout      = gbl.display_input_timeout_value;
    char key;

    printf("the dungeon menu and its doors\n");

    if (gbl.geo_ptr == NULL) {
        check(false, "the 3D map is allocated", "gbl.geo_ptr is NULL");
        return;
    }

    gbl.game_state = GAME_STATE_DUNGEON_MAP;
    gbl.area_ptr->block_area_view = 0;
    gbl.search_location_addr = 0x1234;
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value   = '\0';

    /* One character, so that the door routines - which give everybody a go in
     * turn - answer for exactly the strength and the skill set here. */
    player_init(&p1);
    p1.field_125 = 1;
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    p1.hit_point_max = p1.hit_point_current = 30;
    p1.health_status = STATUS_OKEY;
    p1.in_combat = true;
    set_strength_dex(&p1, 10, 0, 10);

    gbl.team_count = 0;
    gbl_team_add(&p1);
    gbl.selected_player = &p1;

    /* --- the map, and which side of a door is which. */
    {
        memset(gbl.geo_ptr, 0, sizeof(*gbl.geo_ptr));

        dungeon_map_set_door_unlocked(0, 4, 4);
        dungeon_map_set_door_unlocked(2, 4, 4);
        dungeon_map_set_door_unlocked(1, 5, 5);      /* a diagonal has no door */
        dungeon_map_set_door_unlocked(0, 16, 4);     /* off the map, both ways */
        dungeon_map_set_door_unlocked(0, 4, -1);

        snprintf(detail, sizeof(detail), "4,4 north %u east %u south %u, 5,5 %u",
                 gbl.geo_ptr->maps[4][4].x3_dir_0,
                 gbl.geo_ptr->maps[4][4].x3_dir_2,
                 gbl.geo_ptr->maps[4][4].x3_dir_4,
                 gbl.geo_ptr->maps[5][5].x3_dir_0);
        check(gbl.geo_ptr->maps[4][4].x3_dir_0 == 1 &&
              gbl.geo_ptr->maps[4][4].x3_dir_2 == 1 &&
              gbl.geo_ptr->maps[4][4].x3_dir_4 == 0 &&
              gbl.geo_ptr->maps[5][5].x3_dir_0 == 0 &&
              gbl.geo_ptr->maps[0][4].x3_dir_0 == 0,
              "a door is unlocked one side at a time, and never off the map",
              detail);
    }

    /* --- a step forward, and what it costs. */
    {
        memset(gbl.geo_ptr, 0, sizeof(*gbl.geo_ptr));
        gbl.map_pos_x = 8;
        gbl.map_pos_y = 8;
        gbl.map_direction = 2;                  /* east */
        gbl.area2_ptr->search_flags = 0;
        gbl.can_bash_door = gbl.can_pick_door = gbl.can_knock_door = false;
        world_clock_clear();

        dungeon_move_party_forward();

        snprintf(detail, sizeof(detail), "at %d,%d after %d minutes",
                 gbl.map_pos_x, gbl.map_pos_y, world_clock_minutes());
        check(gbl.map_pos_x == 9 && gbl.map_pos_y == 8 &&
              world_clock_minutes() == 1 && gbl.can_bash_door &&
              gbl.can_pick_door && gbl.can_knock_door,
              "a step east is a minute and a fresh try at the next door",
              detail);

        /* Searching as they go costs ten minutes a square - slot 2 of the clock
         * is the tens of minutes - which is most of the cost of a dungeon. */
        gbl.area2_ptr->search_flags = 1;
        world_clock_clear();

        dungeon_move_party_forward();

        snprintf(detail, sizeof(detail), "at %d,%d after %d minutes",
                 gbl.map_pos_x, gbl.map_pos_y, world_clock_minutes());
        check(gbl.map_pos_x == 10 && world_clock_minutes() == 10,
              "and ten minutes when they are searching the walls on the way",
              detail);

        gbl.area2_ptr->search_flags = 0;

        /* The map wraps: off the north edge is the south edge. */
        gbl.map_pos_x = 0;
        gbl.map_pos_y = 0;
        gbl.map_direction = 0;                  /* north */
        dungeon_move_party_forward();
        gbl.map_direction = 6;                  /* west */
        dungeon_move_party_forward();

        snprintf(detail, sizeof(detail), "at %d,%d", gbl.map_pos_x, gbl.map_pos_y);
        check(gbl.map_pos_x == 15 && gbl.map_pos_y == 15,
              "walking off the top left corner comes back at the bottom right",
              detail);
    }

    platform_set_key_typed_mode(true);

    /* --- the menu. Stepping forward in it moves nobody: all it does is notice
     * that the party is against the edge of the map, which is what the script
     * reads to send them out of it. */
    {
        memset(gbl.geo_ptr, 0, sizeof(*gbl.geo_ptr));
        gbl.map_pos_x = 0;
        gbl.map_pos_y = 0;
        gbl.map_direction = 0;
        gbl.area2_ptr->tried_to_exit_map = false;

        platform_clear_keys();
        platform_push_key('8');         /* the keypad's up: forward */

        key = dungeon_main_3d_world_menu();

        snprintf(detail, sizeof(detail), "key '%c', at %d,%d, %s", key,
                 gbl.map_pos_x, gbl.map_pos_y,
                 gbl.area2_ptr->tried_to_exit_map ? "at the edge" : "inside");
        check(key == 'H' && gbl.map_pos_x == 0 && gbl.map_pos_y == 0 &&
              gbl.area2_ptr->tried_to_exit_map,
              "a step off the edge of the map ends the turn without moving",
              detail);
    }

    /* --- turning is free: right, about, left, and then a step to get out. */
    {
        gbl.map_pos_x = 8;
        gbl.map_pos_y = 8;
        gbl.map_direction = 0;

        platform_clear_keys();
        platform_push_key('6');         /* right: 0 -> 2 */
        platform_push_key('2');         /* about: 2 -> 6 */
        platform_push_key('4');         /* left:  6 -> 4 */
        platform_push_key('8');         /* forward, which ends the turn */

        key = dungeon_main_3d_world_menu();

        snprintf(detail, sizeof(detail), "facing %u, key '%c'",
                 gbl.map_direction, key);
        check(gbl.map_direction == 4 && key == 'H',
              "right, about and left leave the party facing south", detail);
    }

    /* --- Search is a toggle and Look is a turn: the party spends ten minutes
     * and the interpreter is sent to the map's search script. */
    {
        gbl.area2_ptr->search_flags = 0;
        gbl.ecl_offset = 0;
        world_clock_clear();

        platform_clear_keys();
        platform_push_key('s');         /* Search on */
        platform_push_key('l');         /* Look, which ends the turn */

        key = dungeon_main_3d_world_menu();

        snprintf(detail, sizeof(detail),
                 "key '%c', flags %u, script 0x%04x, %d minutes", key,
                 (unsigned)gbl.area2_ptr->search_flags,
                 (unsigned)gbl.ecl_offset, world_clock_minutes());
        check(key == 'L' && gbl.area2_ptr->search_flags == 3 &&
              gbl.ecl_offset == 0x1234 && world_clock_minutes() == 10,
              "Search turns searching on and Look spends ten minutes on it",
              detail);

        gbl.area2_ptr->search_flags = 0;
    }

    /* --- the overhead map, and the maps that will not show one. */
    {
        gbl.map_area_display = false;
        gbl.area_ptr->block_area_view = 0;

        platform_clear_keys();
        platform_push_key('a');
        platform_push_key('e');         /* Encamp, to end the turn */

        key = dungeon_main_3d_world_menu();

        check(key == 'E' && gbl.map_area_display,
              "Area turns the overhead map on", "and Encamp ends the turn");

        gbl.area_ptr->block_area_view = 1;

        platform_clear_keys();
        platform_push_key('a');
        platform_push_key('e');

        key = dungeon_main_3d_world_menu();

        check(key == 'E' && gbl.map_area_display,
              "but a map that refuses to be mapped says Not Here instead",
              "the overhead map is left as it was");

        gbl.area_ptr->block_area_view = 0;
        gbl.map_area_display = false;
    }

    /* --- and outside a dungeon there is no menu at all: no key is read, and the
     * only thing that happens is the text area being cleared. */
    {
        gbl.game_state = GAME_STATE_WILDERNESS_MAP;
        gbl.area2_ptr->field_592 = 5;
        gbl.bottom_text_has_been_cleared = false;

        platform_clear_keys();
        key = dungeon_main_3d_world_menu();

        snprintf(detail, sizeof(detail), "key 0x%02x, field_592 %d",
                 (unsigned)(unsigned char)key, (int)gbl.area2_ptr->field_592);
        check(key == '\0' && gbl.area2_ptr->field_592 == 0 &&
              gbl.bottom_text_has_been_cleared,
              "on the wilderness map the dungeon menu answers nothing at all",
              detail);

        gbl.game_state = GAME_STATE_DUNGEON_MAP;
    }

    /* --- the doors. An open square is walked through without a word. */
    {
        set_locked_door(1);
        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d", gbl.map_pos_x, gbl.map_pos_y);
        check(gbl.map_pos_x == 9 && gbl.map_pos_y == 8,
              "a square with nothing in the way is walked straight through",
              detail);

        /* A wall is not a door: flags of 0 mean the wall has no way through it,
         * and nothing is offered. */
        set_locked_door(0);
        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d", gbl.map_pos_x, gbl.map_pos_y);
        check(gbl.map_pos_x == 8, "and a solid wall is not asked about", detail);

        /* 0xff in field_592 is the script saying it has dealt with the square
         * itself; the door is left alone and the flag reset. */
        set_locked_door(1);
        gbl.area2_ptr->field_592 = 0xff;
        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d, field_592 %d",
                 gbl.map_pos_x, gbl.map_pos_y, (int)gbl.area2_ptr->field_592);
        check(gbl.map_pos_x == 8 && gbl.area2_ptr->field_592 == 0,
              "a square the script has already dealt with is skipped", detail);
    }

    /* --- and every turn ends with the picture and the talking head thrown away,
     * because the party has walked away from whatever they belonged to. */
    {
        picture_dax_array_free_blocks(&gbl.pic_frames);
        gbl.last_dax_file[0]  = '\0';
        gbl.last_dax_block_id = 0xff;
        picture_load_pic_final(&gbl.pic_frames, 0, 0x1d, "PIC");
        gbl.current_head_id = 5;
        gbl.current_body_id = 6;

        set_locked_door(0);
        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "%d frames, head %u, body %u",
                 gbl.pic_frames.num_frames, gbl.current_head_id,
                 gbl.current_body_id);
        check(gbl.pic_frames.num_frames == 0 && gbl.current_head_id == 0xff &&
              gbl.current_body_id == 0xff,
              "the end of a turn frees the picture and forgets the portrait",
              detail);
    }

    /* --- bashing. A strength of 25 opens anything, and opens both sides of it:
     * the door the party walks through is not shut again behind them. */
    {
        set_strength_dex(&p1, 25, 0, 10);
        set_locked_door(2);
        platform_push_key('b');

        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d, near side %u, far side %u",
                 gbl.map_pos_x, gbl.map_pos_y,
                 gbl.geo_ptr->maps[8][8].x3_dir_2,
                 gbl.geo_ptr->maps[8][9].x3_dir_6);
        check(gbl.map_pos_x == 9 && gbl.geo_ptr->maps[8][8].x3_dir_2 == 1 &&
              gbl.geo_ptr->maps[8][9].x3_dir_6 == 1,
              "a strength of 25 forces a locked door, and forces it for good",
              detail);
    }

    /* --- a strength of 16 matches nothing in the original's chain of tests: no
     * roll is made and the party's try is not even spent, so they may stand
     * there shouldering the door for ever. The 15 in "str == 15 || str == 17"
     * was meant to be a 16. */
    {
        set_strength_dex(&p1, 16, 0, 10);
        set_locked_door(2);
        platform_push_key('b');

        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d, door %u, may bash again: %s",
                 gbl.map_pos_x, gbl.map_pos_y,
                 gbl.geo_ptr->maps[8][8].x3_dir_2,
                 gbl.can_bash_door ? "yes" : "no");
        check(gbl.map_pos_x == 8 && gbl.geo_ptr->maps[8][8].x3_dir_2 == 2 &&
              gbl.can_bash_door,
              "a strength of 16 rolls nothing at all, as the original had it",
              detail);
    }

    /* --- 18/01 to 18/50 always opens an ordinary door, because the original set
     * the result before rolling for it. Eight tries, eight doors. */
    {
        int opened = 0;

        set_strength_dex(&p1, 18, 50, 10);

        for (int i = 0; i < 8; i++) {
            set_locked_door(2);
            platform_push_key('b');

            dungeon_locked_door();

            if (gbl.map_pos_x == 9) {
                opened++;
            }
        }

        snprintf(detail, sizeof(detail), "%d of 8", opened);
        check(opened == 8,
              "18/50 never fails to bash an ordinary door, which 18/51 does",
              detail);
    }

    /* --- a reinforced door is a different table: nothing under 18/91 moves it,
     * and finding that out costs the party their one try. */
    {
        set_strength_dex(&p1, 18, 50, 10);
        set_locked_door(3);
        platform_push_key('b');

        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d, may bash again: %s",
                 gbl.map_pos_x, gbl.map_pos_y,
                 gbl.can_bash_door ? "yes" : "no");
        check(gbl.map_pos_x == 8 && !gbl.can_bash_door,
              "18/50 cannot shift a reinforced door and does not get to retry",
              detail);

        set_strength_dex(&p1, 25, 0, 10);
        set_locked_door(3);
        platform_push_key('b');

        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d", gbl.map_pos_x, gbl.map_pos_y);
        check(gbl.map_pos_x == 9,
              "and a strength of 25 opens that one too", detail);

        set_strength_dex(&p1, 10, 0, 10);
    }

    /* --- picking. The lock answers a thief on a locked door and ignores one on
     * a reinforced door, which is still offered the chance to find out. */
    {
        p1.class_level[SKILL_THIEF] = 5;
        p1.thief_skills[1] = 100;               /* Open Locks, always */

        set_locked_door(2);
        gbl.can_bash_door = false;              /* so that Pick is the first word */
        platform_push_key('p');

        dungeon_locked_door();

        /* The try is spent on the way past - and then handed straight back,
         * because walking forward is what resets all three. */
        snprintf(detail, sizeof(detail), "at %d,%d, may pick again: %s",
                 gbl.map_pos_x, gbl.map_pos_y,
                 gbl.can_pick_door ? "yes" : "no");
        check(gbl.map_pos_x == 9 && gbl.can_pick_door,
              "a thief with a hundred in Open Locks opens a locked door", detail);

        set_locked_door(3);
        gbl.can_bash_door = false;
        platform_push_key('p');

        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d, door %u, may pick again: %s",
                 gbl.map_pos_x, gbl.map_pos_y,
                 gbl.geo_ptr->maps[8][8].x3_dir_2,
                 gbl.can_pick_door ? "yes" : "no");
        check(gbl.map_pos_x == 8 && gbl.geo_ptr->maps[8][8].x3_dir_2 == 3 &&
              !gbl.can_pick_door,
              "and picks at a reinforced one until the picks run out", detail);
    }

    /* --- knocking. The spell is spent and the party walks through, but nobody
     * unlocks anything: the door is still shut once they are past it. */
    {
        spell_list_clear(&p1.spell_list);
        spell_list_add_learnt(&p1.spell_list, SPELL_KNOCK);

        set_locked_door(2);
        gbl.can_bash_door = false;
        gbl.can_pick_door = false;
        platform_push_key('k');

        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d, door %u, %d spells left",
                 gbl.map_pos_x, gbl.map_pos_y,
                 gbl.geo_ptr->maps[8][8].x3_dir_2,
                 spell_list_count(&p1.spell_list));
        check(gbl.map_pos_x == 9 && gbl.geo_ptr->maps[8][8].x3_dir_2 == 2 &&
              !spell_list_has_spell(&p1.spell_list, SPELL_KNOCK),
              "a knock spell is spent walking through a door it leaves locked",
              detail);
    }

    /* --- and a party with nothing left to try is not asked about the door at
     * all: no prompt, no attempt spent, and the 'b' pushed here goes unread. */
    {
        p1.class_level[SKILL_THIEF] = 0;
        spell_list_clear(&p1.spell_list);

        set_locked_door(2);
        gbl.can_bash_door = false;
        platform_push_key('b');

        dungeon_locked_door();

        snprintf(detail, sizeof(detail), "at %d,%d, may pick again: %s",
                 gbl.map_pos_x, gbl.map_pos_y,
                 gbl.can_pick_door ? "yes" : "no");
        check(gbl.map_pos_x == 8 && gbl.can_pick_door,
              "a door nobody can do anything about is not mentioned", detail);

        platform_clear_keys();
    }

    /* --- the whole thing on the real map: the overhead view, a turn, a step. */
    {
        int nonzero = 0, colors = 0;

        gbl.game_area = 1;
        if (frames_load_8x8d(4, 0xca) && frames_load_8x8d(0, 0xcb)) {
            gbl.game_area = 2;
            view3d_load_walldef(1, 1);
            view3d_load_3d_map(1);
            view3d_load_sky();
        }
        gbl.game_area = old_area;

        gbl.map_pos_x = 8;
        gbl.map_pos_y = 8;
        gbl.map_direction = 0;
        gbl.map_area_display = false;
        gbl.sky_colour = 11;
        gbl.area_ptr->time_hour = 14;

        clear_screen_raw();

        platform_clear_keys();
        platform_push_key('a');         /* the overhead map */
        platform_push_key('6');         /* turn right */
        platform_push_key('8');         /* and walk, which ends the turn */

        key = dungeon_main_3d_world_menu();

        dump(out_dir, "dungeon-menu.ppm");
        frame_stats(&nonzero, &colors);

        snprintf(detail, sizeof(detail),
                 "key '%c', facing %u, %d px, %d colours -> dungeon-menu.ppm",
                 key, gbl.map_direction, nonzero, colors);
        check(key == 'H' && gbl.map_area_display && gbl.map_direction == 2 &&
              gbl.bottom_text_has_been_cleared,
              "the menu draws the map, turns the party and lets them walk",
              detail);
    }

    platform_set_key_typed_mode(false);
    platform_clear_keys();

    spell_list_clear(&p1.spell_list);
    world_clock_clear();
    gbl.team_count       = 0;
    gbl.selected_player  = old_selected;
    gbl.map_area_display = old_area_display;
    gbl.map_pos_x        = old_map_x;
    gbl.map_pos_y        = old_map_y;
    gbl.map_direction    = old_direction;
    gbl.ecl_offset       = old_ecl_offset;
    gbl.search_location_addr      = old_search_addr;
    gbl.area2_ptr->search_flags   = old_search_flags;
    gbl.area2_ptr->field_592      = old_field_592;
    gbl.area2_ptr->tried_to_exit_map = false;
    gbl.area_ptr->block_area_view = old_block_view;
    gbl.display_input_seconds_to_wait = old_wait;
    gbl.display_input_timeout_value   = old_timeout;
    gbl.game_state       = old_state;
    gbl.can_bash_door    = true;
    gbl.can_pick_door    = true;
    gbl.can_knock_door   = true;

    printf("\n");
}

/* A shop's stock is the ground, so every case here lays one item down and puts
 * the buyer's purse back to a known figure. The price band is the script's, and
 * 0x10 is not one of the values the table knows, so it charges face value. */
static void shop_scene(Player *p1, i16 value, i16 weight, i16 band)
{
    Item stock;

    gbl_ground_items_clear();
    item_init(&stock, ITEM_LONG_SWORD, 0, 0, 0, 0, 0, false, 0, false, weight, 1,
              value, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
    (void)gbl_ground_item_add(&stock);

    p1->item_count = 0;
    money_clear_all(&p1->money);
    money_set(&p1->money, MONEY_GOLD, 1000);
    money_clear_all(&gbl.pooled_money);

    gbl.area2_ptr->field_6DA = band;
    gbl.selected_player = p1;

    /* What a shop has on screen is a picture rather than a portrait, which is
     * block 0x50, and that matters here for a reason beyond the look of it: the
     * portrait path ends in input_clear_keyboard, so loading one throws away
     * every key this has queued and the menu - which has no way out but Exit -
     * would spin. A real player types after the picture is up. */
    gbl.last_dax_block_id = 0x50;

    platform_clear_keys();
}

static void check_shop(const char *out_dir)
{
    Player p1;
    char detail[240];
    GameState old_state    = gbl.game_state;
    Player   *old_selected = gbl.selected_player;
    i16  old_band       = gbl.area2_ptr->field_6DA;
    i16  old_in_dungeon = gbl.area_ptr->in_dungeon;
    bool old_border     = gbl.redraw_boarder;
    u8   old_block_id   = gbl.last_dax_block_id;
    int  old_wait       = gbl.display_input_seconds_to_wait;
    char old_timeout    = gbl.display_input_timeout_value;
    Item *picked;
    char key;

    printf("the city shops\n");

    /* Every prompt in the overlay is fed from the key queue, with the timeout
     * armed as a backstop: a prompt this forgot to feed answers '\0' after a
     * second, which is Escape, which both the stock list and the shop menu take
     * as "leave". */
    platform_set_key_typed_mode(true);
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value   = '\0';

    /* One character, so the purse the shop is spending is exactly this one.
     * Strength 18 carries anything a shop is likely to sell. */
    player_init(&p1);
    p1.field_125 = 1;
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    p1.cls = CLASS_FIGHTER;
    p1.class_level[SKILL_FIGHTER] = 4;
    p1.hit_point_max = p1.hit_point_current = 30;
    p1.health_status = STATUS_OKEY;
    set_strength_dex(&p1, 18, 0, 10);

    gbl.team_count = 0;
    gbl_team_add(&p1);

    /* --- the stock list. */
    {
        shop_scene(&p1, 0, 10, 0x10);

        platform_push_key(0x1b);
        picked = NULL;
        {
            int index = 0;

            key = shop_choose_item(&index, &picked);
        }
        snprintf(detail, sizeof(detail), "key '%c', priced at %d",
                 key == '\0' ? '.' : key, gbl.ground_items[0].value);
        check(key == '\0' && picked == NULL && gbl.ground_items[0].value == 1,
              "nothing in a shop is free: an item the script valued at nothing "
              "is marked up to a copper piece, for good", detail);

        platform_push_key('b');
        picked = NULL;
        {
            int index = 0;

            key = shop_choose_item(&index, &picked);
        }
        check(key == 'B' && picked == &gbl.ground_items[0],
              "and Buy answers with the item under the highlight", NULL);
    }

    /* --- what the shop is asking, which is the band the script set. */
    {
        const struct { i16 band; int cost; const char *what; } bands[] = {
            { 0x01,  10, "a sixteenth" },
            { 0x02,  20, "an eighth"   },
            { 0x04,  40, "a quarter"   },
            { 0x08,  80, "half"        },
            { 0x10, 160, "face value"  },   /* not in the table, so unchanged */
            { 0x20, 320, "double"      },
            { 0x40, 640, "four times"  },
            { 0x00, 160, "face value"  }    /* a shop the script never marked */
        };
        bool all_right = true;

        for (size_t i = 0; i < COAB_ARRAY_LEN(bands); i++) {
            int spent;

            shop_scene(&p1, 160, 10, bands[i].band);
            platform_push_key('b');
            platform_push_key(0x1b);
            shop_buy();

            spent = 1000 - money_gold_worth(&p1.money);
            if (spent != bands[i].cost || p1.item_count != 1) {
                all_right = false;
                snprintf(detail, sizeof(detail),
                         "band 0x%02x wanted %d for %s and took %d",
                         bands[i].band, bands[i].cost, bands[i].what, spent);
            }
        }

        if (all_right) {
            snprintf(detail, sizeof(detail),
                     "%zu bands, from a sixteenth of face value to eight times",
                     COAB_ARRAY_LEN(bands));
        }
        check(all_right, "a shop charges its own band of the face value", detail);
    }

    /* --- the shelf is never emptied. */
    {
        shop_scene(&p1, 160, 10, 0x10);
        platform_push_key('b');
        platform_push_key('b');
        platform_push_key(0x1b);
        shop_buy();

        snprintf(detail, sizeof(detail), "%d in the pack, %d gold left, %d "
                 "still on the shelf", p1.item_count,
                 money_gold_worth(&p1.money), gbl.ground_item_count);
        check(p1.item_count == 2 && money_gold_worth(&p1.money) == 680 &&
              gbl.ground_item_count == 1,
              "nothing leaves the shelf when it is sold, so one suit of armour "
              "can outfit the whole party", detail);
    }

    /* --- whose money pays for it. */
    {
        shop_scene(&p1, 160, 10, 0x10);
        money_set(&gbl.pooled_money, MONEY_GOLD, 500);
        platform_push_key('b');
        platform_push_key(0x1b);
        shop_buy();

        snprintf(detail, sizeof(detail), "%d gold left in the purse, %d in the "
                 "pool", money_gold_worth(&p1.money),
                 money_gold_worth(&gbl.pooled_money));
        check(money_gold_worth(&p1.money) == 840 &&
              money_gold_worth(&gbl.pooled_money) == 500,
              "the buyer's own purse is spent first", detail);

        shop_scene(&p1, 160, 10, 0x10);
        money_clear_all(&p1.money);
        money_set(&p1.money, MONEY_GOLD, 20);
        money_set(&gbl.pooled_money, MONEY_GOLD, 500);
        platform_push_key('b');
        platform_push_key(0x1b);
        shop_buy();

        snprintf(detail, sizeof(detail), "%d gold left in the purse, %d in the "
                 "pool", money_gold_worth(&p1.money),
                 money_gold_worth(&gbl.pooled_money));
        check(p1.item_count == 1 && money_gold_worth(&p1.money) == 20 &&
              money_gold_worth(&gbl.pooled_money) == 340,
              "and the pool pays for what the buyer cannot, without touching "
              "what they are carrying", detail);

        shop_scene(&p1, 160, 10, 0x10);
        money_clear_all(&p1.money);
        money_set(&p1.money, MONEY_GOLD, 20);
        money_set(&gbl.pooled_money, MONEY_GOLD, 30);
        platform_push_key('b');
        platform_push_key(0x1b);
        shop_buy();

        check(p1.item_count == 0 && money_gold_worth(&p1.money) == 20 &&
              money_gold_worth(&gbl.pooled_money) == 30,
              "a price neither can meet is refused, and nothing moves",
              "\"Not enough Money.\"");
    }

    /* --- too heavy to walk out with. */
    {
        shop_scene(&p1, 160, 20000, 0x10);
        platform_push_key('b');
        platform_push_key(0x1b);
        shop_buy();

        snprintf(detail, sizeof(detail), "%d in the pack, %d gold left",
                 p1.item_count, money_gold_worth(&p1.money));
        check(p1.item_count == 0 && money_gold_worth(&p1.money) == 1000,
              "an item nobody can lift is not paid for either", detail);
    }

    /* --- an empty shop. The C# read a price off the entry the list had not
     * handed back; here it counts as backing out. */
    {
        gbl_ground_items_clear();
        p1.item_count = 0;
        money_clear_all(&p1.money);
        money_set(&p1.money, MONEY_GOLD, 1000);
        platform_clear_keys();
        platform_push_key('b');
        platform_push_key(0x1b);
        shop_buy();

        check(p1.item_count == 0 && money_gold_worth(&p1.money) == 1000,
              "buying out of an empty shop buys nothing", NULL);
    }

    /* --- the shop menu, which is where a shop is entered and left. */
    {
        gbl.area_ptr->in_dungeon = 1;

        shop_scene(&p1, 160, 10, 0x10);
        money_set(&gbl.pooled_money, MONEY_GOLD, 250);
        gbl.game_state = GAME_STATE_START_GAME_MENU;

        platform_push_key('e');
        shop_city_shop();

        snprintf(detail, sizeof(detail), "state %d, %d gold in the pool",
                 (int)gbl.game_state, money_gold_worth(&gbl.pooled_money));
        check(gbl.game_state == GAME_STATE_SHOP &&
              money_gold_worth(&gbl.pooled_money) == 0,
              "walking into a shop empties the pool, so the money warning on "
              "the way out is only ever about what was pooled inside", detail);

        /* The down cursor key is 'P' too, which is why pooling is gated on the
         * key being one somebody typed. Keypad '2' is how the port sends it. */
        shop_scene(&p1, 160, 10, 0x10);
        platform_push_key('2');
        platform_push_key('e');
        shop_city_shop();

        snprintf(detail, sizeof(detail), "%d gold in the purse, %d in the pool",
                 money_gold_worth(&p1.money),
                 money_gold_worth(&gbl.pooled_money));
        check(money_gold_worth(&p1.money) == 1000 &&
              money_gold_worth(&gbl.pooled_money) == 0,
              "the down cursor key does not pool the party's money", detail);

        /* Typed, it does - and then Exit notices what is lying there. "~Yes ~No"
         * counts from 0, so No is 1, and No is the answer that leaves. */
        shop_scene(&p1, 160, 10, 0x10);
        platform_push_key('p');
        platform_push_key('e');
        platform_push_key('n');
        shop_city_shop();

        snprintf(detail, sizeof(detail), "%d gold in the purse, %d in the pool",
                 money_gold_worth(&p1.money),
                 money_gold_worth(&gbl.pooled_money));
        check(money_gold_worth(&p1.money) == 0 &&
              money_gold_worth(&gbl.pooled_money) == 1000,
              "Pool empties the purses, and the shopkeeper's parting word about "
              "the money is not an argument", detail);

        /* Yes puts the menu back rather than leaving, so it takes a second Exit
         * to get out - and the money is still there to be taken. */
        shop_scene(&p1, 160, 10, 0x10);
        platform_push_key('p');
        platform_push_key('e');
        platform_push_key('y');
        platform_push_key('e');
        platform_push_key('n');
        shop_city_shop();

        check(money_gold_worth(&gbl.pooled_money) == 1000,
              "and Yes goes back for it, which needs a second Exit", NULL);

        /* Take is only offered while there is coin in the pool, and the only coin
         * that ever gets there is what the party pools inside the shop. */
        shop_scene(&p1, 160, 10, 0x10);
        platform_push_key('p');
        platform_push_key('t');
        platform_push_key(0x1b);
        platform_push_key('e');
        platform_push_key('n');
        shop_city_shop();

        snprintf(detail, sizeof(detail), "%d gold in the purse, %d in the pool",
                 money_gold_worth(&p1.money),
                 money_gold_worth(&gbl.pooled_money));
        check(money_gold_worth(&p1.money) == 0 &&
              money_gold_worth(&gbl.pooled_money) == 1000,
              "Take opens the pool, and backing out of it leaves the coin alone",
              detail);

        /* Buying from the menu, which is what a shop is for, and the picture the
         * shop was showing is put back afterwards. */
        shop_scene(&p1, 160, 10, 0x10);
        gbl.area_ptr->in_dungeon = 0;
        platform_push_key('b');       /* the menu's Buy, which opens the list */
        platform_push_key('b');       /* and two off the list itself */
        platform_push_key('b');
        platform_push_key(0x1b);
        platform_push_key('e');
        shop_city_shop();

        snprintf(detail, sizeof(detail), "%d in the pack, %d gold left",
                 p1.item_count, money_gold_worth(&p1.money));
        check(p1.item_count == 2 && money_gold_worth(&p1.money) == 680 &&
              gbl.redraw_boarder,
              "two swords bought from the shop menu, and the border put back "
              "for whatever comes next", detail);
        dump(out_dir, "shop-menu.ppm");
    }

    platform_set_key_typed_mode(false);
    platform_clear_keys();

    gbl_ground_items_clear();
    money_clear_all(&gbl.pooled_money);
    gbl.team_count      = 0;
    gbl.selected_player = old_selected;
    gbl.area2_ptr->field_6DA = old_band;
    gbl.area_ptr->in_dungeon = old_in_dungeon;
    gbl.redraw_boarder  = old_border;
    gbl.last_dax_block_id = old_block_id;
    gbl.display_input_seconds_to_wait = old_wait;
    gbl.display_input_timeout_value   = old_timeout;
    gbl.game_state      = old_state;

    printf("\n");
}

/* Every service starts from the same body: a thousand gold, nothing wrong, and
 * an empty key queue. The picture-not-portrait block matters here for the same
 * reason it does in a shop - see shop_scene. */
static void temple_scene(Player *p1, int gold)
{
    p1->item_count = 0;
    money_clear_all(&p1->money);
    if (gold > 0) {
        money_set(&p1->money, MONEY_GOLD, gold);
    }
    money_clear_all(&gbl.pooled_money);

    affect_list_clear(&p1->affects);
    p1->health_status     = STATUS_OKEY;
    p1->in_combat         = false;
    p1->hit_point_max     = 30;
    p1->hit_point_current = 30;
    p1->hit_point_rolled  = 30;
    stat_value_load(&p1->stats.value[PSTAT_CON], 10);

    gbl.selected_player   = p1;
    gbl.last_dax_block_id = 0x50;

    platform_clear_keys();
}

static void check_temple(const char *out_dir)
{
    Player p1;
    char detail[240];
    GameState old_state    = gbl.game_state;
    Player   *old_selected = gbl.selected_player;
    i16  old_in_dungeon = gbl.area_ptr->in_dungeon;
    bool old_border     = gbl.redraw_boarder;
    u8   old_block_id   = gbl.last_dax_block_id;
    int  old_speed      = gbl.game_speed_var;
    int  old_wait       = gbl.display_input_seconds_to_wait;
    char old_timeout    = gbl.display_input_timeout_value;
    int  hp;

    printf("the temples\n");

    /* Every prompt is fed from the queue. The backstop matters more here than in
     * a shop: ovr027.yes_no keeps asking until Y or N and '\0' is neither, so a
     * yes/no this forgot to answer would spin rather than time out. The counts
     * below are exact - one key per question. */
    platform_set_key_typed_mode(true);
    gbl.display_input_seconds_to_wait = 1;
    gbl.display_input_timeout_value   = '\0';
    gbl.game_speed_var = 0;             /* "is cured." pauses for a moment */
    gbl.game_state     = GAME_STATE_SHOP;

    player_init(&p1);
    p1.field_125 = 1;
    snprintf(p1.name, sizeof(p1.name), "%s", "Alias");
    p1.cls = CLASS_FIGHTER;
    p1.class_level[SKILL_FIGHTER] = 4;
    set_strength_dex(&p1, 18, 0, 10);

    gbl.team_count = 0;
    gbl_team_add(&p1);

    /* --- paying for it. */
    {
        bool paid;

        temple_scene(&p1, 1000);
        platform_push_key('y');
        paid = temple_buy_cure(1000, "Cure Blindness");

        snprintf(detail, sizeof(detail), "%d gold left of a thousand",
                 money_gold_worth(&p1.money));
        check(paid && money_gold_worth(&p1.money) == 0,
              "a cure is paid for out of the character's own purse", detail);

        temple_scene(&p1, 1000);
        platform_push_key('n');
        paid = temple_buy_cure(1000, "Cure Blindness");

        check(paid == false && money_gold_worth(&p1.money) == 1000,
              "and No costs nothing, which is the only way out of the question",
              NULL);

        temple_scene(&p1, 20);
        money_set(&gbl.pooled_money, MONEY_GOLD, 1000);
        platform_push_key('y');
        paid = temple_buy_cure(1000, "Cure Blindness");

        snprintf(detail, sizeof(detail), "%d gold in the purse, %d in the pool",
                 money_gold_worth(&p1.money),
                 money_gold_worth(&gbl.pooled_money));
        check(paid && money_gold_worth(&p1.money) == 20 &&
              money_gold_worth(&gbl.pooled_money) == 0,
              "the pool pays what the purse cannot, and never makes up a "
              "difference", detail);

        temple_scene(&p1, 20);
        money_set(&gbl.pooled_money, MONEY_GOLD, 30);
        platform_push_key('y');
        paid = temple_buy_cure(1000, "Cure Blindness");

        check(paid == false && money_gold_worth(&p1.money) == 20 &&
              money_gold_worth(&gbl.pooled_money) == 30,
              "a price neither can meet is refused and nothing moves",
              "\"Not enough money.\"");
    }

    /* --- curing what is wrong, and what is not. */
    {
        temple_scene(&p1, 1000);
        effect_add_affect(false, 0, 60, AFFECT_BLINDED, &p1);
        platform_push_key('y');         /* just the one question: they are blind */
        temple_cure_blindness();

        snprintf(detail, sizeof(detail), "blind %d, %d gold left",
                 (int)player_has_affect(&p1, AFFECT_BLINDED),
                 money_gold_worth(&p1.money));
        check(player_has_affect(&p1, AFFECT_BLINDED) == false &&
              money_gold_worth(&p1.money) == 0,
              "a thousand gold restores a blind character's sight", detail);

        temple_scene(&p1, 1000);
        platform_push_key('n');         /* "is not blind." - cast anyway? no */
        temple_cure_blindness();

        check(money_gold_worth(&p1.money) == 1000,
              "somebody who can see is told so before they are charged", NULL);

        temple_scene(&p1, 1000);
        platform_push_key('y');         /* cast anyway, and pay for it */
        platform_push_key('y');
        temple_cure_blindness();

        check(money_gold_worth(&p1.money) == 0,
              "and is allowed to pay for it anyway", NULL);

        /* Being an animated corpse is on the disease list, so a walking body is
         * "diseased" and Cure Disease does not ask twice. */
        temple_scene(&p1, 1000);
        effect_add_affect(false, 0, 60, AFFECT_ANIMATE_DEAD, &p1);
        platform_push_key('y');
        temple_cure_disease();

        snprintf(detail, sizeof(detail), "animated %d, %d gold left",
                 (int)player_has_affect(&p1, AFFECT_ANIMATE_DEAD),
                 money_gold_worth(&p1.money));
        check(player_has_affect(&p1, AFFECT_ANIMATE_DEAD) == false &&
              money_gold_worth(&p1.money) == 0,
              "being an animated corpse counts as a disease, so Cure Disease "
              "lifts it without asking whether to bother", detail);

        temple_scene(&p1, 1000);
        p1.hit_point_current = 10;
        platform_push_key('y');
        temple_cure_wounds(1);

        hp = p1.hit_point_current;
        snprintf(detail, sizeof(detail), "%d hit points of 30, %d gold left", hp,
                 money_gold_worth(&p1.money));
        check(hp >= 11 && hp <= 18 && money_gold_worth(&p1.money) == 900,
              "a hundred gold buys cure light wounds, which is the one d8 the "
              "spell rolls", detail);
    }

    /* --- Heal, which is the expensive one. */
    {
        temple_scene(&p1, 5000);
        p1.hit_point_current = 4;
        effect_add_affect(false, 0, 60, AFFECT_BLINDED, &p1);
        effect_add_affect(false, 0, 60, AFFECT_FEEBLEMIND, &p1);
        platform_push_key('y');
        temple_cure_wounds(4);

        hp = p1.hit_point_current;
        snprintf(detail, sizeof(detail), "%d hit points of 30, blind %d, "
                 "feebleminded %d", hp, (int)player_has_affect(&p1, AFFECT_BLINDED),
                 (int)player_has_affect(&p1, AFFECT_FEEBLEMIND));
        check(hp >= 26 && hp <= 29 && money_gold_worth(&p1.money) == 0 &&
              player_has_affect(&p1, AFFECT_BLINDED) == false &&
              player_has_affect(&p1, AFFECT_FEEBLEMIND) == false,
              "five thousand for Heal takes the blindness, the diseases and the "
              "feeblemind, and stops a d4 short of full", detail);

        /* Which on somebody already at full health means it takes hit points
         * off. The most expensive service in the game, and it hurts. */
        temple_scene(&p1, 5000);
        platform_push_key('y');
        temple_cure_wounds(4);

        hp = p1.hit_point_current;
        snprintf(detail, sizeof(detail), "%d hit points of 30 for 5000 gold", hp);
        check(hp >= 26 && hp <= 29 && money_gold_worth(&p1.money) == 0,
              "bought at full health it charges five thousand gold and takes a "
              "d4 off, which is the original's arithmetic", detail);
    }

    /* --- raising the dead. */
    {
        temple_scene(&p1, 6000);
        p1.health_status = STATUS_DEAD;
        p1.hit_point_current = 0;
        effect_add_affect(false, 0, 60, AFFECT_POISONED, &p1);
        platform_push_key('y');         /* dead, so only the price is asked */
        temple_raise_dead();

        snprintf(detail, sizeof(detail), "status %d, %d hit points, in_combat %d, "
                 "%d gold left", (int)p1.health_status, p1.hit_point_current,
                 (int)p1.in_combat, money_gold_worth(&p1.money));
        check(p1.health_status == STATUS_OKEY && p1.hit_point_current == 1 &&
              p1.in_combat && money_gold_worth(&p1.money) == 500 &&
              player_has_affect(&p1, AFFECT_POISONED) == false,
              "five and a half thousand gold puts a dead character back on their "
              "feet with one hit point and no poison in them", detail);

        /* Con is meant to cost a point. The test that guards it is inverted, so
         * it only fires when there is nothing left to take. */
        temple_scene(&p1, 6000);
        p1.health_status = STATUS_DEAD;
        stat_value_load(&p1.stats.value[PSTAT_CON], 0);
        platform_push_key('y');
        temple_raise_dead();

        snprintf(detail, sizeof(detail), "Con %d",
                 p1.stats.value[PSTAT_CON].full);
        check(p1.stats.value[PSTAT_CON].full == -1,
              "raising the dead is supposed to cost a point of Constitution, and "
              "takes it only from somebody who has none", detail);

        /* The maximum is recomputed as the Constitution bonus divided by the
         * levels that earned it, which is a handful of hit points where there
         * were dozens. Con 16 and four fighter levels: (40 - 20) / ((16-14)*4). */
        temple_scene(&p1, 6000);
        p1.health_status = STATUS_DEAD;
        p1.hit_point_max     = 40;
        p1.hit_point_rolled  = 20;
        stat_value_load(&p1.stats.value[PSTAT_CON], 16);
        platform_push_key('y');
        temple_raise_dead();

        snprintf(detail, sizeof(detail), "%d hit points at most, was 40",
                 p1.hit_point_max);
        check(p1.hit_point_max == 2,
              "and a Constitution of 14 or more costs the character almost every "
              "hit point they had, which is the original's bug", detail);

        /* Con 13 never reaches the arithmetic at all. */
        temple_scene(&p1, 6000);
        p1.health_status = STATUS_DEAD;
        p1.hit_point_max     = 40;
        p1.hit_point_rolled  = 20;
        stat_value_load(&p1.stats.value[PSTAT_CON], 13);
        platform_push_key('y');
        temple_raise_dead();

        check(p1.hit_point_max == 40,
              "a Constitution of 13 or less is left alone, which is the only "
              "reason the party survives being raised", NULL);

        /* Alive, and charged anyway: buy_cure is on the left of the &&. */
        temple_scene(&p1, 6000);
        platform_push_key('y');         /* not dead - raise anyway? */
        platform_push_key('y');         /* and pay for it */
        temple_raise_dead();

        snprintf(detail, sizeof(detail), "%d gold left, %d hit points",
                 money_gold_worth(&p1.money), p1.hit_point_current);
        check(money_gold_worth(&p1.money) == 500 && p1.hit_point_current == 30 &&
              p1.health_status == STATUS_OKEY,
              "raising somebody who is not dead takes the five and a half "
              "thousand, says they are cured, and does nothing", detail);
    }

    /* --- poison, curses and stone. */
    {
        temple_scene(&p1, 1000);
        effect_add_affect(false, 0, 60, AFFECT_POISONED, &p1);
        effect_add_affect(false, 0, 60, AFFECT_SLOW_POISON, &p1);
        platform_push_key('y');
        temple_cure_poison2();

        check(player_has_affect(&p1, AFFECT_POISONED) == false &&
              player_has_affect(&p1, AFFECT_SLOW_POISON) == false &&
              money_gold_worth(&p1.money) == 0,
              "Neutralize Poison takes the poison and what was carrying it",
              NULL);

        /* A cursed item in the pack is a curse, so this does not ask twice. The
         * spell unreadies the item and leaves it cursed, so the same item can be
         * un-cursed again for another three and a half thousand. */
        {
            Item cursed;

            temple_scene(&p1, 4000);
            item_init(&cursed, ITEM_LONG_SWORD, 0, 0, 0, -2, 0, true, 0, true,
                      10, 1, 100, AFFECT_NONE, AFFECT_NONE, AFFECT_NONE);
            player_item_add(&p1, &cursed);

            platform_push_key('y');
            temple_remove_curse();

            snprintf(detail, sizeof(detail), "readied %d, cursed %d, %d gold left",
                     (int)p1.items[0].readied, (int)p1.items[0].cursed,
                     money_gold_worth(&p1.money));
            check(p1.items[0].readied == false && p1.items[0].cursed &&
                  money_gold_worth(&p1.money) == 500,
                  "Remove Curse unreadies the cursed sword without lifting the "
                  "curse, so it can be sold the same service twice", detail);
        }

        temple_scene(&p1, 3000);
        p1.health_status = STATUS_STONED;
        p1.hit_point_current = 0;
        platform_push_key('y');
        temple_stone_to_flesh();

        snprintf(detail, sizeof(detail), "status %d, %d hit points, %d gold left",
                 (int)p1.health_status, p1.hit_point_current,
                 money_gold_worth(&p1.money));
        check(p1.health_status == STATUS_OKEY && p1.hit_point_current == 1 &&
              p1.in_combat && money_gold_worth(&p1.money) == 1000,
              "two thousand unpetrifies a statue, leaving one hit point", detail);

        temple_scene(&p1, 3000);
        platform_push_key('y');         /* not stoned - do it anyway? */
        platform_push_key('y');         /* and pay */
        temple_stone_to_flesh();

        check(money_gold_worth(&p1.money) == 1000 && p1.hit_point_current == 30,
              "and takes the two thousand off somebody made of flesh already, "
              "for nothing", NULL);
    }

    /* --- the heal list, which is walked with Home and End. */
    {
        temple_scene(&p1, 1000);
        gbl.area_ptr->in_dungeon = 1;

        platform_push_key('e');
        temple_heal();

        check(money_gold_worth(&p1.money) == 1000,
              "the list of services is left with Exit, having sold nothing",
              NULL);

        /* '1' is End, which steps to the next entry: two of them reaches Cure
         * Light Wounds. Then Heal, pay, and Exit. */
        temple_scene(&p1, 1000);
        p1.hit_point_current = 10;
        platform_push_key('1');
        platform_push_key('1');
        platform_push_key('h');
        platform_push_key('y');
        platform_push_key('e');
        temple_heal();

        hp = p1.hit_point_current;
        snprintf(detail, sizeof(detail), "%d hit points of 30, %d gold left", hp,
                 money_gold_worth(&p1.money));
        check(hp > 10 && money_gold_worth(&p1.money) == 900,
              "and the third of them is cure light wounds, bought for a hundred",
              detail);
        dump(out_dir, "temple-heal.ppm");
    }

    /* --- the temple menu, which is where a temple is entered and left. */
    {
        temple_scene(&p1, 1000);
        money_set(&gbl.pooled_money, MONEY_GOLD, 250);
        gbl.game_state = GAME_STATE_START_GAME_MENU;

        platform_push_key('e');
        temple_shop();

        snprintf(detail, sizeof(detail), "state %d, %d gold in the pool",
                 (int)gbl.game_state, money_gold_worth(&gbl.pooled_money));
        check(gbl.game_state == GAME_STATE_SHOP &&
              money_gold_worth(&gbl.pooled_money) == 0,
              "walking into a temple empties the pool, the same way a shop does",
              detail);

        /* 'H' is the up cursor key's scan code, so Heal is gated on somebody
         * having typed the letter. Keypad '8' is how the port sends the key. */
        temple_scene(&p1, 1000);
        platform_push_key('8');
        platform_push_key('e');
        temple_shop();

        check(money_gold_worth(&p1.money) == 1000,
              "the up cursor key does not open the list of services", NULL);

        /* And typed, it does: in, buy cure light wounds, out, out. */
        temple_scene(&p1, 1000);
        gbl.area_ptr->in_dungeon = 0;
        p1.hit_point_current = 10;
        platform_push_key('h');
        platform_push_key('1');
        platform_push_key('1');
        platform_push_key('h');
        platform_push_key('y');
        platform_push_key('e');
        platform_push_key('e');
        temple_shop();

        snprintf(detail, sizeof(detail), "%d hit points of 30, %d gold left",
                 p1.hit_point_current, money_gold_worth(&p1.money));
        check(p1.hit_point_current > 10 && money_gold_worth(&p1.money) == 900 &&
              gbl.redraw_boarder,
              "a wound healed from the temple menu, and the border put back for "
              "whatever comes next", detail);
        dump(out_dir, "temple-menu.ppm");
    }

    platform_set_key_typed_mode(false);
    platform_clear_keys();

    affect_list_clear(&p1.affects);
    money_clear_all(&gbl.pooled_money);
    gbl.team_count      = 0;
    gbl.selected_player = old_selected;
    gbl.area_ptr->in_dungeon = old_in_dungeon;
    gbl.redraw_boarder  = old_border;
    gbl.last_dax_block_id = old_block_id;
    gbl.game_speed_var  = old_speed;
    gbl.display_input_seconds_to_wait = old_wait;
    gbl.display_input_timeout_value   = old_timeout;
    gbl.game_state      = old_state;

    printf("\n");
}

static void check_protection(void)
{
    char detail[200];
    char c1, c2, c3;

    printf("copy protection\n");

    /* Three hand-worked answers off the wheel. The rune numbers and the path
     * are what the game rolls; the box is counted from the bottom, so box 1 is
     * row 5. */
    c1 = protect_wheel_char(0, 0, 0, 5);
    c2 = protect_wheel_char(3, 5, 2, 0);
    c3 = protect_wheel_char(0, 21, 0, 0);
    snprintf(detail, sizeof(detail), "'%c', '%c', '%c'", c1, c2, c3);
    check(c1 == '9' && c2 == 'N' && c3 == 'S',
          "the code wheel lines up", detail);

    /* The rings are 36 characters each and the index wraps within them. */
    check(protect_wheel_char(25, 0, 2, 0) == protect_wheel_char(25 - 36, 0, 2, 0) &&
          protect_wheel_char(0, 0, 0, 6) == '\0',
          "the wheel wraps and has six boxes", "a seventh box has no answer");

    printf("\n");
}

/* engine/seg001.cs: what loads once and what a second game starts from.
 *
 * This runs last, after every drawing check, because both halves of it are
 * destructive in ways nothing else here is: program_init_first replaces two of
 * the five 8x8 symbol banks and thirteen combat icons, and program_init_again
 * hands the whole roster back, party and monsters together. PROGRAM itself is not
 * called - it does not return. */
/* ------------------------------------------------- every chapter's own data */
/* The chapter number is in the name of nearly every data file the game opens, and
 * every check above this one sets gbl.game_area to 1 or 2 - the two chapters whose
 * art was needed to test something else. So chapters 3 to 6 have never been asked
 * for anything, and they are exactly where a player gets to only by playing for a
 * few hours. What follows walks all six through the loaders the game uses on
 * entering an area: the script and its header, the dungeon geometry, the wall
 * sets and their tile banks, a monster with its affects and its pack, and the
 * corner picture.
 *
 * The block ids are the ones the archives really hold, read out of their headers.
 * They are not interchangeable between chapters: the original kept ids unique
 * across the six files by giving each chapter a band of its own, which is why
 * chapter 4's blocks are numbered in the thirties. */
static void check_chapters(const char *out_dir)
{
    static const struct {
        int chapter;
        int ecl;         /* ECL<n>.DAX */
        int geo;         /* GEO<n>.DAX; chapter 1 has none */
        int walldef;     /* WALLDEF<n>.DAX; chapter 1 has none */
        int mob;         /* MON<n>CHA.DAX */
        int pic;         /* PIC<n>.DAX */
    } CHAPTERS[] = {
        { 1, 0x50,  0,  0,  1, 1 },
        { 2, 0x01,  1,  1,  0, 1 },
        { 3, 0x10, 16,  3, 16, 1 },
        { 4, 0x20, 32,  3,  9, 1 },
        { 5, 0x30, 50,  8, 48, 1 },
        { 6, 0x40, 64,  3, 64, 1 }
    };
    u8   saved_area   = gbl.game_area;
    bool saved_reload = gbl.reload_ecl_and_pictures;
    char detail[240];

    printf("all six chapters\n");

    /* vm_init_ecl resets the area records unless a game is being picked up, and
     * this is not one; leave them alone so nothing below inherits a half-started
     * chapter. */
    gbl.reload_ecl_and_pictures = true;

    for (size_t i = 0; i < COAB_ARRAY_LEN(CHAPTERS); i++) {
        int chapter = CHAPTERS[i].chapter;
        int walls = 0, wall_ids = 0, frame_cells = 0;
        int entry, move_hook;
        Player *mob;
        char mob_name[PLAYER_NAME_MAX + 1];
        int mob_items = 0, mob_affects = 0;

        gbl.game_area = (u8)chapter;

        /* The script. Its first five words are the hooks the world loop calls,
         * so decoding them is what proves the block is a script and not just
         * bytes that decompressed. */
        vm_load_ecl_dax((u8)CHAPTERS[i].ecl);
        vm_init_ecl();
        move_hook = (int)gbl.vm_run_addr_1;
        entry     = (int)gbl.ecl_initial_entry_point;

        /* The geometry and the wall set that draws it. */
        if (CHAPTERS[i].geo > 0) {
            view3d_load_3d_map(CHAPTERS[i].geo);

            for (int y = 0; y < GEO_MAP_DIM; y++) {
                for (int x = 0; x < GEO_MAP_DIM; x++) {
                    MapInfo *mi = &gbl.geo_ptr->maps[y][x];

                    if (mi->wall_type_dir_0 || mi->wall_type_dir_2 ||
                        mi->wall_type_dir_4 || mi->wall_type_dir_6) {
                        walls++;
                    }
                }
            }

            view3d_load_walldef(1, CHAPTERS[i].walldef);

            for (int y = 0; y < WALL_DEF_ROWS; y++) {
                for (int x = 0; x < WALL_DEF_COLS; x++) {
                    if (wall_defs_id(&gbl.wall_def, 1, y, x) > 0) {
                        wall_ids++;
                    }
                }
            }
        }

        /* A monster out of the chapter's own three files. Nothing else in the
         * self-test reads them: every fight above builds its monsters by hand. */
        mob = savegame_load_mob_opt(CHAPTERS[i].mob, false);
        snprintf(mob_name, sizeof(mob_name), "%s",
                 (mob != NULL) ? mob->name : "");
        if (mob != NULL) {
            mob_items   = mob->item_count;
            mob_affects = mob->affects.count;
            roster_release(mob);
        }

        /* And the corner picture, which is a different animation per chapter. */
        picture_dax_array_free_blocks(&gbl.pic_frames);
        picture_load_pic_final(&gbl.pic_frames, 0, (u8)CHAPTERS[i].pic, "PIC");
        if (gbl.pic_frames.num_frames > 0 &&
            gbl.pic_frames.frames[0].picture != NULL) {
            frame_cells = gbl.pic_frames.frames[0].picture->width;
        }

        snprintf(detail, sizeof(detail),
                 "script at 0x%04x moving to 0x%04x, %d walled squares, "
                 "%d wall ids, '%s' with %d items and %d affects, "
                 "%d picture frames %d cells wide",
                 entry, move_hook, walls, wall_ids, mob_name, mob_items,
                 mob_affects, gbl.pic_frames.num_frames, frame_cells);
        /* Script addresses are the biased ones the machine works in: 0x8000 is
         * the first byte of the block, 0x9dff the last. */
        check(entry > 0x8000 && entry < 0x8000 + ECL_BLOCK_SIZE &&
              move_hook > 0x8000 && move_hook < 0x8000 + ECL_BLOCK_SIZE &&
              (CHAPTERS[i].geo == 0 || (walls > 0 && wall_ids > 100)) &&
              mob != NULL && mob_name[0] != '\0' &&
              gbl.pic_frames.num_frames > 0 && frame_cells > 0,
              (chapter == 1) ? "chapter 1's script, monsters and pictures load" :
              (chapter == 2) ? "chapter 2's do, with its dungeon and wall set" :
              (chapter == 3) ? "chapter 3's do" :
              (chapter == 4) ? "chapter 4's do" :
              (chapter == 5) ? "chapter 5's do" : "chapter 6's do",
              detail);

        if (chapter == 6) {
            /* One picture of the deepest chapter, drawn the way the game draws
             * it, as something to look at rather than count. */
            clear_screen_raw();
            view3d_draw_world(0, 8, 8);
            dump(out_dir, "chapter6-view.ppm");
        }
    }

    /* ---- the wall block that holds two sets, which only chapters 5 and 6 have.
     * WALLDEF5 block 14 is 0x618 bytes, two sets' worth, and its tiles are in
     * 8X8D5 blocks 141 and 142 - blockId * 10 + 1 and + 2. A chapter that never
     * doubles up would never load a bank by that name. ---- */
    {
        int set1_ids = 0, set2_ids = 0;

        gbl.game_area = 5;
        gbl.set_blocks[0].set_id = gbl.set_blocks[0].block_id = 9;
        gbl.set_blocks[1].set_id = gbl.set_blocks[1].block_id = 9;

        view3d_load_walldef(1, 14);

        for (int y = 0; y < WALL_DEF_ROWS; y++) {
            for (int x = 0; x < WALL_DEF_COLS; x++) {
                if (wall_defs_id(&gbl.wall_def, 1, y, x) > 0) {
                    set1_ids++;
                }
                if (wall_defs_id(&gbl.wall_def, 2, y, x) > 0) {
                    set2_ids++;
                }
            }
        }

        snprintf(detail, sizeof(detail),
                 "sets 1 and 2 hold %d and %d ids, banks %s and %s, "
                 "set_blocks 1 is %d/%d and 2 is %d/%d",
                 set1_ids, set2_ids,
                 (gbl.symbol_8x8_set[1] != NULL) ? "loaded" : "missing",
                 (gbl.symbol_8x8_set[2] != NULL) ? "loaded" : "missing",
                 gbl.set_blocks[0].set_id, gbl.set_blocks[0].block_id,
                 gbl.set_blocks[1].set_id, gbl.set_blocks[1].block_id);
        /* Only the set that was asked for is recorded: the loop resets both
         * entries and the tail of LoadWalldef fills in the first one, so set 2
         * carries the tiles of a block nothing remembers loading. That is the
         * original's bookkeeping and the game reads set_blocks[0] to decide
         * whether the 3D view can be drawn at all. */
        check(set1_ids > 100 && set2_ids > 100 &&
              gbl.symbol_8x8_set[1] != NULL && gbl.symbol_8x8_set[2] != NULL &&
              gbl.set_blocks[0].set_id == 1 && gbl.set_blocks[0].block_id == 14 &&
              gbl.set_blocks[1].set_id == -1 && gbl.set_blocks[1].block_id == -1,
              "a wall block holding two sets loads both, and only names the first",
              detail);
    }

    gbl.game_area = saved_area;
    gbl.reload_ecl_and_pictures = saved_reload;
    picture_dax_array_free_blocks(&gbl.pic_frames);

    printf("\n");
}

static void check_program(const char *out_dir)
{
    char detail[240];
    Player *p;
    Item ground;
    bool icons_loaded = true;

    printf("the top of the game\n");

    gbl.game_area  = 1;         /* where gbl_init leaves it, and where the art is */
    gbl.ecl_offset = 0x1234;

    program_init_first();

    snprintf(detail, sizeof(detail), "banks 0 and 4, ecl offset 0x%X",
             (unsigned)gbl.ecl_offset);
    check(gbl.symbol_8x8_set[0] != NULL && gbl.symbol_8x8_set[4] != NULL &&
          gbl.ecl_offset == 0x8000,
          "InitFirst loads the two symbol banks and biases the script offset",
          detail);

    {
        /* Masked on colour 13, so the moon has transparent pixels round it: the
         * loader maps the mask colour to index 16, which is what the drawing
         * code refuses to write. */
        int transparent = 0;

        if (gbl.sky_dax_250 != NULL) {
            for (size_t i = 0; i < gbl.sky_dax_250->data_size; i++) {
                if (gbl.sky_dax_250->data[i] == 16) {
                    transparent++;
                }
            }
        }
        snprintf(detail, sizeof(detail), "%d transparent pixels round the moon",
                 transparent);
        check(gbl.sky_dax_250 != NULL && gbl.sky_dax_251 != NULL &&
              gbl.sky_dax_252 != NULL && gbl.sky_dax_251->data_size > 0 &&
              transparent > 0,
              "the moon, the sun and the ground are loaded masked", detail);
    }

    /* Blocks 0..0x0b into slots 0x0d..0x18, and 0x19 into 0x19. */
    for (int i = 0x0d; i <= 0x19; i++) {
        if (gbl.combat_icons[i].normal == NULL ||
            gbl.combat_icons[i].attack == NULL) {
            icons_loaded = false;
        }
    }
    check(icons_loaded, "the thirteen COMSPR icons are in slots 0x0d..0x18 and 0x19",
          "each with its attack picture 0x80 blocks along");

    check(item_data(ITEM_LONG_SWORD)->dice_size_normal == 8,
          "and the item table is read", "ITEMS");

    dump(out_dir, "program-loading.ppm");

    /* Now InitAgain, from a state as unlike a new game as can be arranged: a
     * party in a shop halfway through a demo, on the wrong square facing the
     * wrong way, with the wall sets swapped and every flag set. */
    gbl.map_pos_x       = 1;
    gbl.map_pos_y       = 2;
    gbl.map_direction   = 5;
    gbl.map_wall_type   = 3;
    gbl.map_wall_roof   = 3;
    gbl.set_blocks[0].set_id   = 9;
    gbl.set_blocks[0].block_id = 9;
    gbl.set_blocks[1].set_id   = 3;
    gbl.set_blocks[1].block_id = 3;
    gbl.set_blocks[2].set_id   = 3;
    gbl.set_blocks[2].block_id = 3;
    gbl.can_bash_door   = false;
    gbl.can_pick_door   = false;
    gbl.can_knock_door  = false;
    gbl.game_speed_var  = 9;    /* the demo's speed */
    gbl.in_demo         = true;
    gbl.game_area       = 6;
    gbl.game_area_backup = 6;
    gbl.map_area_display = true;
    gbl.party_killed    = true;
    gbl.game_won        = true;
    gbl.game_saved      = true;
    gbl.silent_training = true;
    gbl.apply_item_affect = true;
    gbl.sprite_changed  = true;
    gbl.display_player_sprite = true;
    gbl.display_player_status_line18 = true;
    gbl.reload_ecl_and_pictures = true;
    gbl.delay_between_characters = false;
    gbl.focus_combat_area_on_player = false;
    gbl.search_flag_bkup = 0x1234;
    gbl.rest_encounter_count = 7;
    gbl.ecl_offset      = 0x4444;
    gbl.last_dax_block_id  = 3;
    gbl.saved_dax_block_id = 3;
    gbl.bigpic_block_id = 3;
    gbl.menu_selected_word = 7;
    gbl.menu_screen_index  = 7;
    gbl.combat_type     = COMBAT_TYPE_DUEL;
    gbl.game_state      = GAME_STATE_SHOP;
    gbl.last_game_state = GAME_STATE_COMBAT;
    gbl.area_ptr->in_dungeon        = 0;
    gbl.area_ptr->last_ecl_block_id = 5;
    gbl.area2_ptr->party_size       = 5;

    roster_clear();
    gbl.team_count = 0;
    p = roster_alloc();
    if (p != NULL) {
        player_init(p);
        snprintf(p->name, sizeof(p->name), "%s", "Alias");
        gbl_team_add(p);
        gbl.selected_player = p;
    }

    /* And a shop's stock on the floor, which InitAgain does not touch. */
    gbl_ground_items_clear();
    item_clear(&ground);
    ground.type = ITEM_LONG_SWORD;
    gbl_ground_item_add(&ground);

    program_init_again();

    snprintf(detail, sizeof(detail), "%d,%d facing %d",
             (int)gbl.map_pos_x, (int)gbl.map_pos_y, (int)gbl.map_direction);
    check(gbl.map_pos_x == 7 && gbl.map_pos_y == 0x0d && gbl.map_direction == 2 &&
          gbl.map_wall_type == 0 && gbl.map_wall_roof == 0,
          "InitAgain puts the party back on the starting square facing east",
          detail);

    check(gbl.set_blocks[0].set_id == 1 && gbl.set_blocks[0].block_id == 0 &&
          gbl.set_blocks[1].set_id == -1 && gbl.set_blocks[1].block_id == -1 &&
          gbl.set_blocks[2].set_id == -1 && gbl.set_blocks[2].block_id == -1,
          "the wall sets are back to WALLDEF block 0 and two empty slots", NULL);

    snprintf(detail, sizeof(detail), "speed %d, in_demo %s",
             gbl.game_speed_var, gbl.in_demo ? "true" : "false");
    check(gbl.game_speed_var == 4 && gbl.in_demo == true,
          "the demo's speed of 9 lasts one game and the demo itself does not",
          detail);

    check(gbl.ecl_offset == 0x8000 && gbl.game_area == 1 &&
          gbl.game_area_backup == 1 && gbl.menu_selected_word == 1 &&
          gbl.menu_screen_index == 1 &&
          gbl.game_state == GAME_STATE_DUNGEON_MAP &&
          gbl.last_game_state == GAME_STATE_START_GAME_MENU &&
          gbl.combat_type == COMBAT_TYPE_NORMAL,
          "the script offset, the chapter and the screen are as a new game finds them",
          NULL);

    check(gbl.party_killed == false && gbl.game_won == false &&
          gbl.game_saved == false && gbl.silent_training == false &&
          gbl.apply_item_affect == false && gbl.sprite_changed == false &&
          gbl.display_player_sprite == false &&
          gbl.display_player_status_line18 == false &&
          gbl.reload_ecl_and_pictures == false &&
          gbl.delay_between_characters == true &&
          gbl.focus_combat_area_on_player == true &&
          gbl.map_area_display == false &&
          gbl.search_flag_bkup == 0 && gbl.rest_encounter_count == 0 &&
          gbl.can_bash_door && gbl.can_pick_door && gbl.can_knock_door,
          "every flag a game leaves behind is cleared", NULL);

    check(gbl.area_ptr->in_dungeon == 1 &&
          gbl.area_ptr->last_ecl_block_id == 0 &&
          gbl.area2_ptr->party_size == 0 &&
          gbl.last_dax_block_id == 0xff && gbl.saved_dax_block_id == 0xff &&
          gbl.bigpic_block_id == 0xff && gbl.last_dax_file[0] == '\0',
          "the area records are empty and nothing is loaded", NULL);

    snprintf(detail, sizeof(detail), "%d characters, %d on the team, %d on the floor",
             roster_in_use(), gbl.team_count, gbl.ground_item_count);
    check(roster_in_use() == 0 && gbl.team_count == 0 &&
          gbl.selected_player == NULL && gbl.last_selected_player == NULL &&
          gbl.ground_item_count == 1,
          "the party is gone but what was on the floor stays there", detail);

    gbl.in_demo = false;
    gbl_ground_items_clear();

    printf("\n");
}

bool selftest_run(const char *out_dir)
{
    g_pass = g_fail = 0;

    if (!vfs_mkdir_p(out_dir)) {
        printf("cannot create output directory %s\n", out_dir);
        return false;
    }

    /* Saving and loading writes real files, and Add Character reads whatever is
     * in the save directory. Both point at a directory of the self-test's own so
     * that a run neither disturbs the player's saved games nor is disturbed by
     * them. */
    {
        char save_dir[512];

        snprintf(save_dir, sizeof(save_dir), "%s/save", out_dir);
        if (!vfs_set_save_dir(save_dir)) {
            printf("cannot create save directory %s\n", save_dir);
            return false;
        }
    }

    printf("\nCurse of the Azure Bonds - C/SDL port self-test\n");
    printf("data:   %s\n", vfs_data_dir());
    printf("images: %s\n", out_dir);
    printf("saves:  %s\n\n", vfs_save_dir());

    check_records();
    check_import();
    check_area_blocks();
    check_ecl();
    check_eclvm();
    check_geo();
    check_combat();
    check_tables();
    check_menus();
    check_support(out_dir);
    check_overlays();
    check_pictures(out_dir);
    check_prompts(out_dir);
    check_view3d(out_dir);
    check_combatmap(out_dir);
    check_target();
    check_character(out_dir);
    check_effect(out_dir);
    check_affects();
    check_attack();
    check_battlesetup(out_dir);
    check_monsterai(out_dir);
    check_combatloop(out_dir);
    check_aftercombat(out_dir);
    check_viewplayer(out_dir);
    check_spellcast(out_dir);
    check_treasure(out_dir);
    check_endgame(out_dir);
    check_resting(out_dir);
    check_classcalc(out_dir);
    check_partymenu(out_dir);
    check_savegame();
    check_camp(out_dir);
    check_dungeon(out_dir);
    check_shop(out_dir);
    check_temple(out_dir);
    check_protection();
    check_all_archives();
    check_font(out_dir);
    check_title_frames(out_dir);
    check_credits(out_dir);
    check_frames(out_dir);
    check_palette();
    check_masking();
    check_chapters(out_dir);     /* next to last: it leaves chapter 6 loaded */
    check_program(out_dir);      /* last: see the comment on it */

    printf("\n%d passed, %d failed\n\n", g_pass, g_fail);
    return g_fail == 0;
}
