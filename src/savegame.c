/* savegame.c - characters and games on disk.
 * Ported from engine/ovr017.cs; see savegame.h for what belongs here and what
 * went to partymenu.c and import.c instead.
 */
#include "savegame.h"

#include "affect.h"
#include "area.h"
#include "character.h"
#include "classcalc.h"
#include "dax.h"
#include "ecl.h"
#include "effect.h"
#include "fileio.h"
#include "gbl.h"
#include "icons.h"
#include "import.h"
#include "input.h"
#include "item.h"
#include "log.h"
#include "menu.h"
#include "money.h"
#include "partymenu.h"
#include "picture.h"
#include "prompt.h"
#include "quit.h"
#include "rnd.h"
#include "roster.h"
#include "spells.h"
#include "text.h"
#include "treasure.h"
#include "vfs.h"
#include "view3d.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Long enough for the save directory plus any of the names below. */
#define SAVE_PATH_MAX GAME_FILE_PATH_MAX

/* ------------------------------------------------------------- name building */

/* vfs_save_path with a name built from a printf format, which is what every
 * Path.Combine(Config.GetSavePath(), ...) in the overlay comes to. */
static const char *save_path(char *dst, size_t dst_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static const char *save_path(char *dst, size_t dst_size, const char *fmt, ...)
{
    char name[SAVE_PATH_MAX];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(name, sizeof(name), fmt, ap);
    va_end(ap);

    return vfs_save_path(dst, dst_size, name);
}

/* The same for a file that is about to be read, opened or deleted rather than
 * created: an existing name differing only in case is the one meant. See
 * vfs_save_resolve for why the save directory needs that and the data directory's
 * own resolver is not enough. */
static const char *read_path(char *dst, size_t dst_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static const char *read_path(char *dst, size_t dst_size, const char *fmt, ...)
{
    char name[SAVE_PATH_MAX];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(name, sizeof(name), fmt, ap);
    va_end(ap);

    return vfs_save_resolve(dst, dst_size, name);
}

/* Path.GetFileNameWithoutExtension for a bare basename: everything up to the
 * last dot. A name with no dot is itself. */
static void strip_extension(char *dst, size_t dst_size, const char *name)
{
    const char *dot = strrchr(name, '.');
    size_t keep = (dot != NULL) ? (size_t)(dot - name) : strlen(name);

    if (keep >= dst_size) {
        keep = dst_size - 1;
    }
    memcpy(dst, name, keep);
    dst[keep] = '\0';
}

/* Path.ChangeExtension: the basename with its extension replaced. */
static void change_extension(char *dst, size_t dst_size, const char *name,
                             const char *new_ext)
{
    const char *dot = strrchr(name, '.');
    int keep = (dot != NULL) ? (int)(dot - name) : (int)strlen(name);

    snprintf(dst, dst_size, "%.*s%s", keep, name, new_ext);
}

/* ------------------------------------------------- the loadable player lists */

/* What one pass of the C#'s private BuildSpellList overload needs to know: which
 * files to look at and where the two bytes it reads sit in them. */
typedef struct {
    MenuList *paths;
    MenuList *names;
    size_t    record_size;      /* playerFileSize */
    int       npc_offset;        /* NpcFileOffset[] */
    int       name_offset;       /* PlayerNameOffset[] */
} LoadableScan;

/* ovr017.PlayerNameOffset and NpcFileOffset, indexed by ImportSource. */
static const int PLAYER_NAME_OFFSET[3] = { 0, 0, 4 };
static const int NPC_FILE_OFFSET[3]    = { 0xf7, 0x84, 0x13 };

/* Sys.ArrayToString(data, 0, 15).Trim() over a Pascal string. */
static void read_trimmed_name(char *dst, size_t dst_size, const u8 *data)
{
    sys_array_to_string(dst, dst_size, data, 0, 15);

    /* Trim() takes both ends; the leading side cannot be blank in a Pascal
     * string the engine wrote, but a hand-edited file could be. */
    {
        char *start = dst;
        size_t len;

        while (*start == ' ') {
            start++;
        }
        len = strlen(start);
        while (len > 0 && start[len - 1] == ' ') {
            len--;
        }
        memmove(dst, start, len);
        dst[len] = '\0';
    }
}

static bool name_is_in_party(const char *name)
{
    for (int i = 0; i < gbl.team_count; i++) {
        const Player *p = gbl.team_list[i];
        char trimmed[PLAYER_NAME_MAX + 1];
        size_t len;

        if (p == NULL) {
            continue;
        }

        /* player.name.Trim() - the record pads with spaces. */
        snprintf(trimmed, sizeof(trimmed), "%s", p->name);
        len = strlen(trimmed);
        while (len > 0 && trimmed[len - 1] == ' ') {
            len--;
        }
        trimmed[len] = '\0';

        if (strcmp(trimmed, name) == 0) {
            return true;
        }
    }
    return false;
}

static void loadable_visit(const char *file_name, void *user)
{
    LoadableScan *scan = user;
    char path[SAVE_PATH_MAX];
    GameFile f;
    u8 data[16];
    char player_name[32];
    char display[MENU_ITEM_TEXT_MAX];
    u8 npc_byte;
    long length;

    vfs_save_path(path, sizeof(path), file_name);

    if (!file_find_and_open(&f, true, path)) {
        return;
    }

    /* stream.Length == playerFileSize: the one check that says this really is a
     * record of the kind the extension claims. */
    if (fseek(f.fp, 0, SEEK_END) != 0) {
        file_close(&f);
        return;
    }
    length = ftell(f.fp);

    if (length < 0 || (size_t)length != scan->record_size) {
        file_close(&f);
        return;
    }

    if (fseek(f.fp, scan->name_offset, SEEK_SET) != 0 ||
        fread(data, 1, sizeof(data), f.fp) != sizeof(data)) {
        file_close(&f);
        return;
    }
    read_trimmed_name(player_name, sizeof(player_name), data);

    if (gbl.import_from == IMPORT_SOURCE_HILLSFAR) {
        /* A Hillsfar record has no control byte, so every character in one is
         * offered. */
        npc_byte = 0;
    } else {
        if (fseek(f.fp, scan->npc_offset, SEEK_SET) != 0 ||
            fread(data, 1, 1, f.fp) != 1) {
            file_close(&f);
            return;
        }
        npc_byte = data[0];
    }

    file_close(&f);

    /* string.Compare(Path.GetExtension(filePath), ".sav", true) == 0 - a Pool of
     * Radiance saved game rather than a loose character, in which case the C#
     * showed which game it came out of. filePath[7] was the eighth character of
     * the whole path, which under DOS's "C:\POOL\SAVGAM<n>..." was the save
     * number; on any modern path it is a character of the directory name
     * instead. The letter out of the file's own name is what it was after. */
    {
        const char *dot = strrchr(file_name, '.');
        bool is_sav = dot != NULL && strcasecmp(dot, ".sav") == 0;

        if (is_sav) {
            char stem[SAVE_PATH_MAX];
            size_t stem_len;

            strip_extension(stem, sizeof(stem), file_name);
            stem_len = strlen(stem);

            snprintf(display, sizeof(display), "%-15s from save game %c",
                     player_name,
                     (stem_len > 0) ? stem[stem_len - 1] : '?');
        } else {
            snprintf(display, sizeof(display), "%s", player_name);
        }
    }

    if (!name_is_in_party(player_name) && npc_byte <= 0x7f) {
        menu_list_add(scan->paths, file_name);
        menu_list_add(scan->names, display);
    }
}

/* sub_4708B, the private overload: one extension's worth of the list. */
static void build_loadable_pass(MenuList *paths, MenuList *names,
                                size_t record_size, int npc_offset,
                                int name_offset, const char *suffix)
{
    LoadableScan scan;

    scan.paths = paths;
    scan.names = names;
    scan.record_size = record_size;
    scan.npc_offset = npc_offset;
    scan.name_offset = name_offset;

    if (vfs_for_each_save_file(suffix, loadable_visit, &scan) < 0) {
        log_debug("no save directory to list %s characters from", suffix);
    }
}

void savegame_build_loadable_players_lists(MenuList *paths, MenuList *names)
{
    menu_list_clear(paths);
    menu_list_clear(names);

    switch (gbl.import_from) {
    case IMPORT_SOURCE_CURSE:
        build_loadable_pass(paths, names, PLAYER_RECORD_SIZE,
                            NPC_FILE_OFFSET[0], PLAYER_NAME_OFFSET[0], ".guy");
        break;

    case IMPORT_SOURCE_POOL:
        build_loadable_pass(paths, names, POOL_RAD_RECORD_SIZE,
                            NPC_FILE_OFFSET[1], PLAYER_NAME_OFFSET[1], ".cha");
        build_loadable_pass(paths, names, POOL_RAD_RECORD_SIZE,
                            NPC_FILE_OFFSET[1], PLAYER_NAME_OFFSET[1], ".sav");
        break;

    case IMPORT_SOURCE_HILLSFAR:
        build_loadable_pass(paths, names, HILLS_FAR_RECORD_SIZE,
                            NPC_FILE_OFFSET[2], PLAYER_NAME_OFFSET[2], ".hil");
        break;

    default:
        log_warn("add character: import source %d is none of the three",
                 (int)gbl.import_from);
        break;
    }
}

/* ------------------------------------------------------------ does one exist */

typedef struct {
    const char *wanted;
    bool        found;
} ExistsScan;

static void exists_visit(const char *file_name, void *user)
{
    ExistsScan *scan = user;
    char path[SAVE_PATH_MAX];
    GameFile f;
    u8 data[16];
    char in_file[32];

    if (scan->found) {
        return;         /* the C# returned out of the loop here */
    }

    vfs_save_path(path, sizeof(path), file_name);

    if (!file_find_and_open(&f, true, path)) {
        return;
    }

    if (fread(data, 1, sizeof(data), f.fp) == sizeof(data)) {
        read_trimmed_name(in_file, sizeof(in_file), data);

        if (strcmp(in_file, scan->wanted) == 0) {
            scan->found = true;
        }
    }
    file_close(&f);
}

bool savegame_player_file_exists(const char *file_ext, const char *player_name)
{
    ExistsScan scan;

    scan.wanted = (player_name != NULL) ? player_name : "";
    scan.found = false;

    vfs_for_each_save_file(file_ext, exists_visit, &scan);

    return scan.found;
}

/* ----------------------------------------------------- writing a character out */

void savegame_remove_player_file(Player *player)
{
    char clean[PLAYER_NAME_MAX + 1];
    char path[SAVE_PATH_MAX];

    if (player == NULL) {
        return;
    }

    file_clean_string(clean, sizeof(clean), player->name);

    file_delete(read_path(path, sizeof(path), "%s.guy", clean));
    file_delete(read_path(path, sizeof(path), "%s.swg", clean));
    file_delete(read_path(path, sizeof(path), "%s.fx", clean));
}

void savegame_save_player(const char *prefix, Player *player)
{
    GameFile f;
    u8 record[PLAYER_RECORD_SIZE];
    /* A cleaned name is eight characters, a CHRDAT prefix eight, and the new
     * name the player may be asked for is read eight long. */
    char file_text[32];
    char path[SAVE_PATH_MAX];
    const char *ext_text;
    char input_key;

    if (player == NULL) {
        return;
    }

    gbl.import_from = IMPORT_SOURCE_CURSE;

    if (prefix == NULL || prefix[0] == '\0') {
        ext_text = ".guy";
        file_clean_string(file_text, sizeof(file_text), player->name);
    } else {
        ext_text = ".sav";
        snprintf(file_text, sizeof(file_text), "%s", prefix);
    }

    input_key = 'N';

    /* Only a loose character is asked about: a saved game's own CHRDAT files are
     * overwritten silently, the arg_0.Length == 0 test being what says which is
     * which. */
    while (input_key == 'N' &&
           (prefix == NULL || prefix[0] == '\0') &&
           file_exists(read_path(path, sizeof(path), "%s%s", file_text,
                                 ext_text))) {
        char question[64];

        snprintf(question, sizeof(question), "Overwrite %s? ", file_text);
        input_key = prompt_yes_no(GBL_ALERT_MENU_COLORS, question);

        if (input_key == 'N') {
            file_text[0] = '\0';

            while (file_text[0] == '\0') {
                text_get_user_input_string(file_text, sizeof(file_text), 8, 0,
                                           10, "New file name: ");
            }
        }
    }

    save_path(path, sizeof(path), "%s%s", file_text, ext_text);

    if (!file_assign(&f, path) || !file_rewrite(&f)) {
        log_error("could not write %s", path);
        file_close(&f);
        return;
    }

    memset(record, 0, sizeof(record));
    player_write(player, record, sizeof(record));
    file_block_write(&f, record, sizeof(record));
    file_close(&f);

    /* The pack and the affects are deleted first either way, so a character who
     * has nothing has no stale file left claiming otherwise. */
    file_delete(read_path(path, sizeof(path), "%s.swg", file_text));

    if (player->item_count > 0) {
        save_path(path, sizeof(path), "%s.swg", file_text);

        if (file_assign(&f, path) && file_rewrite(&f)) {
            for (int i = 0; i < player->item_count; i++) {
                u8 item_rec[ITEM_RECORD_SIZE];

                memset(item_rec, 0, sizeof(item_rec));
                item_write(&player->items[i], item_rec, sizeof(item_rec));
                file_block_write(&f, item_rec, sizeof(item_rec));
            }
        } else {
            log_error("could not write %s", path);
        }
        file_close(&f);
    }

    file_delete(read_path(path, sizeof(path), "%s.fx", file_text));

    if (player->affects.count > 0) {
        save_path(path, sizeof(path), "%s.fx", file_text);

        if (file_assign(&f, path) && file_rewrite(&f)) {
            for (int i = 0; i < player->affects.count; i++) {
                u8 affect_rec[AFFECT_RECORD_SIZE];

                memset(affect_rec, 0, sizeof(affect_rec));
                affect_write(&player->affects.items[i], affect_rec,
                             sizeof(affect_rec));
                file_block_write(&f, affect_rec, sizeof(affect_rec));
            }
        } else {
            log_error("could not write %s", path);
        }
        file_close(&f);
    }
}

/* ------------------------------------------------------ reading a character in */

/* Reads one whole record out of a save file. Returns how many bytes arrived. */
static size_t read_record(const char *file_name, u8 *dst, size_t want)
{
    char path[SAVE_PATH_MAX];
    GameFile f;
    size_t got;

    vfs_save_resolve(path, sizeof(path), file_name);

    if (!file_find_and_open(&f, false, path)) {
        return 0;
    }

    got = file_block_read(&f, dst, want);
    file_close(&f);

    return got;
}

/* The `.swg` pack: every whole Item record in the file, in order. */
static void load_player_items(Player *player, const char *stem)
{
    char path[SAVE_PATH_MAX];
    GameFile f;
    u8 record[ITEM_RECORD_SIZE];

    read_path(path, sizeof(path), "%s.swg", stem);

    if (!file_exists(path) || !file_find_and_open(&f, false, path)) {
        return;
    }

    while (file_block_read(&f, record, sizeof(record)) == sizeof(record)) {
        Item it;

        if (item_read(&it, record, sizeof(record), 0)) {
            player_item_add(player, &it);
        }
    }
    file_close(&f);
}

/* The `.fx` affects, and the `.spc` ones Pool of Radiance kept separately. */
static void load_player_affects(Player *player, const char *stem,
                                const char *ext, const bool *keep_types)
{
    char path[SAVE_PATH_MAX];
    GameFile f;
    u8 record[AFFECT_RECORD_SIZE];

    read_path(path, sizeof(path), "%s%s", stem, ext);

    if (!file_exists(path) || !file_find_and_open(&f, false, path)) {
        return;
    }

    while (file_block_read(&f, record, sizeof(record)) == sizeof(record)) {
        Affect a;

        /* asc_49280: only the seven racial affects come across from a Pool of
         * Radiance `.spc`. The type is the record's first byte, which is what the
         * C# tested before it built the Affect at all. */
        if (keep_types != NULL && !keep_types[record[0]]) {
            continue;
        }

        if (affect_read(&a, record, sizeof(record), 0)) {
            affect_list_add(&player->affects, &a);
        }
    }
    file_close(&f);
}

/* ovr017.asc_49280 - the affects a Pool of Radiance character keeps, which are
 * exactly the seven a race is born with. */
static bool pool_spc_affect_kept(unsigned type)
{
    switch (type) {
    case AFFECT_GNOME_VS_MAN_SIZED_GIANT:
    case AFFECT_DWARF_VS_ORC:
    case AFFECT_DWARF_AND_GNOME_VS_GIANTS:
    case AFFECT_30:
    case AFFECT_CON_SAVING_BONUS:
    case AFFECT_ELF_RESIST_SLEEP:
    case AFFECT_HALFELF_RESISTANCE:
        return true;
    default:
        return false;
    }
}

/* ovr017.HillsFarClassMap - Hillsfar's class byte is a bitmask of the four
 * classes, and this is the ClassId each combination comes to. Four of the sixteen
 * have no equivalent here and were CLASS_UNKNOWN in the original too. */
static const ClassId HILLS_FAR_CLASS_MAP[16] = {
    CLASS_UNKNOWN,   CLASS_THIEF,     CLASS_FIGHTER,   CLASS_MC_F_T,
    CLASS_MAGIC_USER, CLASS_MC_MU_T,  CLASS_MC_F_MU,   CLASS_MC_F_MU_T,
    CLASS_CLERIC,    CLASS_MC_C_T,    CLASS_MC_C_F,    CLASS_UNKNOWN,
    CLASS_MC_C_MU,   CLASS_UNKNOWN,   CLASS_MC_C_F_M,  CLASS_UNKNOWN
};

void savegame_transfer_hills_far_character(const HillsFarPlayer *hf_player,
                                           Player *player,
                                           Player *previous_selected)
{
    if (hf_player == NULL || player == NULL) {
        return;
    }

    /* Every stat moves across only if Hillsfar's is the higher of the two. */
    for (int i = 0; i < HF_STAT_COUNT; i++) {
        PlayerStatId which = hills_far_stat_to_pstat[i];

        if (player->stats.value[which].cur < hf_player->stat[i]) {
            stat_value_load(&player->stats.value[which], hf_player->stat[i]);
        }
    }

    if (player->exp < hf_player->exp) {
        player->exp = hf_player->exp;
    }

    /* "If imported player has more than 500 platinum import that amount" - the
     * comment is the original's. Everything the character is carrying is dropped
     * first, then a fifth of the Hillsfar total is added back, which
     * addPlayerGold spreads over the five coin kinds. */
    if (money_gold_worth(&player->money) < hf_player->money) {
        for (int slot = 0; slot < 5; slot++) {
            treasure_drop_coins((MoneyKind)slot,
                                money_get(&player->money, (MoneyKind)slot),
                                player);
        }
        treasure_add_player_gold((i16)(hf_player->money / 5));
    }

    if (player->age < hf_player->age) {
        player->age = hf_player->age;
    }

    /* One level of each class the record claims, whatever the character had. */
    player->class_level[SKILL_CLERIC]     = (hf_player->skill_cleric > 0) ? 1 : 0;
    player->class_level[SKILL_MAGIC_USER] = (hf_player->skill_magic_user > 0) ? 1 : 0;
    player->class_level[SKILL_FIGHTER]    = (hf_player->skill_fighter > 0) ? 1 : 0;
    player->class_level[SKILL_THIEF]      = (hf_player->skill_thief > 0) ? 1 : 0;

    player->hit_dice = 1;

    if (hf_player->field_26 != 0) {
        player->field_192 = 1;
    }

    partymenu_silent_train_player();
    gbl.selected_player = previous_selected;

    player->hit_point_max = hf_player->hp_max;
    player->hit_point_rolled =
        (u8)(player->hit_point_max - partymenu_con_hp_adj(player));
    player->hit_point_current = hf_player->hp_current;
}

/* The four magic items a Hillsfar character can bring back with them. The record
 * field is a strength rather than a count: it is the item's own affect_1, and its
 * value scales with it. All four are made with count 0 and affect_3 none; only the
 * wand has a plus, and only the last necklace a weight.
 *
 * The C#'s sixteen-argument Item constructor took its arguments in reverse push
 * order, affect_3 first; item_init takes them in declaration order. */
static void add_hills_far_item(Player *player, ItemType type, u8 strength,
                               i16 value, i8 plus, i16 weight, Affects affect_2,
                               u8 namenum1, u8 namenum2, u8 namenum3)
{
    Item it;

    if (strength == 0) {
        return;
    }

    item_init(&it, type, namenum1, namenum2, namenum3, plus, 0, false, 0, false,
              weight, 0, value, (Affects)strength, affect_2, AFFECT_NONE);
    player_item_add(player, &it);
}

/* ovr017.ConvertHillsFarPlayer. A Hillsfar character is only ever a character
 * who already existed: the record holds no class levels or hit points worth
 * having, so the game looks for the same name in a `.guy` or a `.cha` first and
 * lays Hillsfar's numbers over that. Only when neither is there is a character
 * built from the record alone. */
static void convert_hills_far_player(Player *player,
                                    const HillsFarPlayer *hf_player,
                                    const char *file_name)
{
    Player *previous_selected;
    char savename[SAVE_PATH_MAX];

    if (savegame_player_file_exists(".guy", hf_player->name)) {
        u8 record[PLAYER_RECORD_SIZE];

        change_extension(savename, sizeof(savename), file_name, ".guy");

        if (read_record(savename, record, sizeof(record)) == sizeof(record)) {
            player_read(player, record, sizeof(record), 0);
        }

        previous_selected = gbl.selected_player;
        gbl.selected_player = player;

        savegame_transfer_hills_far_character(hf_player, player,
                                             previous_selected);

        /*                                    value            plus weight */
        add_hills_far_item(player, ITEM_NECKLACE, hf_player->field_1D,
                           (i16)(hf_player->field_1D * 200), 0, 0,
                           AFFECT_HELPLESS, 0xa8, 0xa7, 0x57);

        add_hills_far_item(player, ITEM_WAND_B, hf_player->field_23,
                           (i16)(hf_player->field_23 * 0x15e), 1, 1,
                           AFFECT_POISON_PLUS_4, 0xce, 0xa7, 0x45);

        add_hills_far_item(player, ITEM_RING_INVIS, hf_player->field_86,
                           (i16)(hf_player->field_86 * 0xc8), 0, 0,
                           AFFECT_HELPLESS, 0xa8, 0xa7, 0x42);

        add_hills_far_item(player, ITEM_NECKLACE, hf_player->field_87,
                           (i16)(hf_player->field_87 * 0x190), 0,
                           (i16)(hf_player->field_87 * 10),
                           AFFECT_HIGH_CON_REGEN, 0xb9, 0xa7, 0x40);
        return;
    }

    if (savegame_player_file_exists(".cha", hf_player->name)) {
        u8 record[POOL_RAD_RECORD_SIZE];
        PoolRadPlayer prp;

        change_extension(savename, sizeof(savename), file_name, ".cha");

        if (read_record(savename, record, sizeof(record)) == sizeof(record) &&
            pool_rad_player_read(&prp, record, sizeof(record), 0)) {
            import_convert_pool_rad_player(player, &prp);
        }

        previous_selected = gbl.selected_player;
        gbl.selected_player = player;

        savegame_transfer_hills_far_character(hf_player, player,
                                             previous_selected);
        return;
    }

    /* Nothing to lay it over: a first-level character of whatever classes the
     * record claims, with 300 gold and the four spells a magic-user starts on. */
    previous_selected = gbl.selected_player;
    gbl.selected_player = player;

    for (int i = 0; i < GBL_ICON_COLOUR_COUNT; i++) {
        player->icon_colours[i] =
            (u8)(((GBL_DEFAULT_ICON_COLOURS[i] + 8) << 4) +
                 GBL_DEFAULT_ICON_COLOURS[i]);
    }

    player->base_ac = 50;
    player->thac0 = 40;
    player->health_status = STATUS_OKEY;
    player->in_combat = true;
    player->field_13F = 1;
    player->field_140 = 1;
    player->field_DE = 1;

    player->mod_id = (u8)rnd_int(0xff);
    player->icon_id = 0x0a;

    player->attacks_count = 2;
    player->attack1_dice_count_base = 1;
    player->attack1_dice_size_base = 2;
    player->field_125 = 1;
    player->base_movement = 12;

    snprintf(player->name, sizeof(player->name), "%s", hf_player->name);

    for (int i = 0; i < HF_STAT_COUNT; i++) {
        stat_value_load(&player->stats.value[hills_far_stat_to_pstat[i]],
                        hf_player->stat[i]);
    }

    /* Hillsfar numbers the races from zero where this game numbers them from
     * one, and has no half-orc of its own. */
    player->race = (u8)(hf_player->race + 1);

    if (player->race == RACE_HALF_ORC) {
        player->race = RACE_HUMAN;
    }

    switch (player->race) {
    case RACE_HALFLING:
        player->icon_size = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_CON_SAVING_BONUS, player);
        break;

    case RACE_DWARF:
        player->icon_size = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_CON_SAVING_BONUS, player);
        effect_add_affect(false, 0xff, 0, AFFECT_DWARF_VS_ORC, player);
        effect_add_affect(false, 0xff, 0, AFFECT_DWARF_AND_GNOME_VS_GIANTS,
                          player);
        break;

    case RACE_GNOME:
        player->icon_size = 1;
        effect_add_affect(false, 0xff, 0, AFFECT_CON_SAVING_BONUS, player);
        effect_add_affect(false, 0xff, 0, AFFECT_GNOME_VS_MAN_SIZED_GIANT,
                          player);
        effect_add_affect(false, 0xff, 0, AFFECT_DWARF_AND_GNOME_VS_GIANTS,
                          player);
        effect_add_affect(false, 0xff, 0, AFFECT_30, player);
        break;

    case RACE_ELF:
        player->icon_size = 2;
        effect_add_affect(false, 0xff, 0, AFFECT_ELF_RESIST_SLEEP, player);
        break;

    case RACE_HALF_ELF:
        player->icon_size = 2;
        effect_add_affect(false, 0xff, 0, AFFECT_HALFELF_RESISTANCE, player);
        break;

    default:
        player->icon_size = 2;
        break;
    }

    player->cls = HILLS_FAR_CLASS_MAP[hf_player->field_35 & 0x0f];
    player->age = hf_player->age;

    player->class_level[SKILL_CLERIC]     = (hf_player->skill_cleric > 0) ? 1 : 0;
    player->class_level[SKILL_MAGIC_USER] = (hf_player->skill_magic_user > 0) ? 1 : 0;
    player->class_level[SKILL_FIGHTER]    = (hf_player->skill_fighter > 0) ? 1 : 0;
    player->class_level[SKILL_THIEF]      = (hf_player->skill_thief > 0) ? 1 : 0;
    player->hit_dice = 1;
    player->sex = hf_player->sex;
    player->alignment = hf_player->alignment;
    player->exp = hf_player->exp;

    if (player->class_level[SKILL_MAGIC_USER] > 0) {
        player_learn_spell(player, SPELL_DETECT_MAGIC_MU);
        player_learn_spell(player, SPELL_READ_MAGIC);
        player_learn_spell(player, SPELL_SHIELD);
        player_learn_spell(player, SPELL_SLEEP);
    }

    partymenu_silent_train_player();

    treasure_add_player_gold(300);
    gbl.selected_player = previous_selected;
    player->hit_point_max = hf_player->hp_max;
    player->hit_point_rolled =
        (u8)(player->hit_point_max - partymenu_con_hp_adj(player));
    player->hit_point_current = hf_player->hp_current;
}

void savegame_import_char(Player *player, const char *file_name)
{
    char stem[SAVE_PATH_MAX];

    if (player == NULL || file_name == NULL) {
        return;
    }

    text_display_string("Loading...Please Wait", 0, 10, 0x18, 0);

    switch (gbl.import_from) {
    case IMPORT_SOURCE_CURSE: {
        u8 record[PLAYER_RECORD_SIZE];

        if (read_record(file_name, record, sizeof(record)) == sizeof(record)) {
            player_read(player, record, sizeof(record), 0);
        } else {
            log_warn("import: %s is not a whole character record", file_name);
        }
        break;
    }

    case IMPORT_SOURCE_POOL: {
        u8 record[POOL_RAD_RECORD_SIZE];
        PoolRadPlayer prp;

        if (read_record(file_name, record, sizeof(record)) == sizeof(record) &&
            pool_rad_player_read(&prp, record, sizeof(record), 0)) {
            import_convert_pool_rad_player(player, &prp);
        } else {
            log_warn("import: %s is not a whole Pool of Radiance record",
                     file_name);
        }
        break;
    }

    case IMPORT_SOURCE_HILLSFAR: {
        u8 record[HILLS_FAR_RECORD_SIZE];
        HillsFarPlayer hfp;

        if (read_record(file_name, record, sizeof(record)) == sizeof(record) &&
            hills_far_player_read(&hfp, record, sizeof(record), 0)) {
            convert_hills_far_player(player, &hfp, file_name);
        } else {
            log_warn("import: %s is not a whole Hillsfar record", file_name);
        }
        break;
    }

    default:
        log_warn("import: import source %d is none of the three",
                 (int)gbl.import_from);
        break;
    }

    /* Which name the pack and the affects hang off: this game's own files are
     * named after the file the character came out of, everybody else's after the
     * character inside it. */
    if (gbl.import_from == IMPORT_SOURCE_CURSE) {
        strip_extension(stem, sizeof(stem), file_name);
    } else {
        file_clean_string(stem, sizeof(stem), player->name);
    }

    load_player_items(player, stem);
    load_player_affects(player, stem, ".fx", NULL);

    if (gbl.import_from == IMPORT_SOURCE_POOL) {
        bool keep[256];

        for (unsigned i = 0; i < COAB_ARRAY_LEN(keep); i++) {
            keep[i] = pool_spc_affect_kept(i);
        }
        load_player_affects(player, stem, ".spc", keep);
    }

    input_clear_keyboard();
    character_recalc_values(player);
    classcalc_class_bonuses(player);
}

/* ---------------------------------------------------------- loading a monster */

Player *savegame_load_mob_opt(int monster_id, bool exit_on_failure)
{
    char file_name[32];
    u8 *data;
    i16 decode_size;
    Player *player;

    /* "MON" + gbl.game_area + "CHA.dax", and the two beside it. */
    snprintf(file_name, sizeof(file_name), "MON%dCHA.dax", (int)gbl.game_area);
    data = dax_load_decode(file_name, monster_id, &decode_size);

    if (decode_size == 0 || data == NULL) {
        free(data);

        if (exit_on_failure) {
            text_display_and_pause("Unable to load monster", 15);
            game_print_and_exit();
        }
        return NULL;
    }

    player = roster_alloc();

    if (player == NULL) {
        /* The C# had a collector and could always make another one. Here the
         * pool is the original's own fixed heap, and running it dry is the
         * failure the caller has to cope with - which every caller of the
         * exit == false overload already does. */
        log_warn("load monster %d: no room for another character", monster_id);
        free(data);
        return NULL;
    }

    player_read(player, data, (size_t)decode_size, 0);
    free(data);

    /* The affects, nine bytes each. The original's do-while would read a
     * ninth-of-a-record tail off the end of a block whose length is not a
     * multiple of nine; whole records only here, since past the end is a real
     * read in C. No block in ../Data has such a tail. */
    snprintf(file_name, sizeof(file_name), "MON%dSPC.dax", (int)gbl.game_area);
    data = dax_load_decode(file_name, monster_id, &decode_size);

    if (data != NULL && decode_size > 0) {
        for (int offset = 0; offset + AFFECT_RECORD_SIZE <= decode_size;
             offset += AFFECT_RECORD_SIZE) {
            Affect a;

            if (affect_read(&a, data, (size_t)decode_size, (size_t)offset)) {
                affect_list_add(&player->affects, &a);
            }
        }
    }
    free(data);

    snprintf(file_name, sizeof(file_name), "MON%dITM.dax", (int)gbl.game_area);
    data = dax_load_decode(file_name, monster_id, &decode_size);

    if (data != NULL && decode_size > 0) {
        for (int offset = 0; offset + ITEM_RECORD_SIZE <= decode_size;
             offset += ITEM_RECORD_SIZE) {
            Item it;

            if (item_read(&it, data, (size_t)decode_size, (size_t)offset)) {
                player_item_add(player, &it);
            }
        }
    }
    free(data);

    input_clear_keyboard();

    return player;
}

Player *savegame_load_mob(int monster_id)
{
    return savegame_load_mob_opt(monster_id, true);
}

void savegame_load_npc(int monster_id)
{
    Player *player;

    if (gbl.area2_ptr == NULL || gbl.area2_ptr->party_size > 7) {
        return;
    }

    player = savegame_load_mob(monster_id);

    if (player == NULL) {
        /* Only reachable through the exit == false path in the original, which
         * this is not; the pool being full is the one way it happens here. */
        return;
    }

    player->mod_id = (u8)monster_id;

    partymenu_assign_player_icon_id(player);

    icons_chead_cbody_comspr_icon(player->icon_id, monster_id, "CPIC");
}

/* ----------------------------------------------------------- a whole saved game */

/* ovr017.asc_4A761 / unk_4AEA0 - the ten slots, 'A' to 'J'. Escape, 0, is in the
 * save set and not in the load one: escaping out of Load Which Game is the
 * do-while's other way out, and escaping out of Save Which Game is what the
 * `inputKey != 0` below tests for. */
#define SAVE_SLOT_FIRST 'A'
#define SAVE_SLOT_LAST  'J'

static bool save_slot_key(char key)
{
    return key >= SAVE_SLOT_FIRST && key <= SAVE_SLOT_LAST;
}

void savegame_load_game_menu(void)
{
    /* "A B C D E" - two characters a slot, and the trailing space trimmed. */
    char games_list[(SAVE_SLOT_LAST - SAVE_SLOT_FIRST + 1) * 2 + 1];
    size_t used = 0;
    char path[SAVE_PATH_MAX];
    char save_letter = '\0';
    bool stop_loop = false;

    gbl.import_from = IMPORT_SOURCE_CURSE;

    for (char letter = SAVE_SLOT_FIRST; letter <= SAVE_SLOT_LAST; letter++) {
        if (file_exists(read_path(path, sizeof(path), "savgam%c.dat", letter))) {
            games_list[used++] = letter;
            games_list[used++] = ' ';
        }
    }

    if (used == 0) {
        return;
    }
    games_list[used - 1] = '\0';   /* TrimEnd of the one trailing space */

    do {
        bool special_key;
        char input_key = prompt_display_input(&special_key, false, 0,
                                             GBL_DEFAULT_MENU_COLORS,
                                             games_list, "Load Which Game: ");

        stop_loop   = (input_key == 0x00);   /* Escape */
        save_letter = '\0';

        /* Any of the ten letters is accepted, including one whose file is not
         * there - which just leaves the loop running and the prompt up. */
        if (save_slot_key(input_key)) {
            save_letter = input_key;
            stop_loop = file_exists(read_path(path, sizeof(path),
                                              "savgam%c.dat", save_letter));
        }
    } while (!stop_loop);

    if (save_letter != '\0') {
        savegame_load_save_game(read_path(path, sizeof(path), "savgam%c.dat",
                                          save_letter));
    }
}

void savegame_load_save_game(const char *file_name)
{
    GameFile file;
    u8 data[0x2000];
    char path[SAVE_PATH_MAX];
    char names[8][0x29 + 1];
    int number_of_players;

    /* The caller may hand over either a bare name - loadGameMenu builds
     * "savgam<letter>.dat" - or a path already inside the save directory. */
    if (strchr(file_name, '/') != NULL) {
        snprintf(path, sizeof(path), "%s", file_name);
    } else {
        vfs_save_resolve(path, sizeof(path), file_name);
    }

    if (!file_find_and_open(&file, true, path)) {
        log_warn("load game: %s cannot be opened", path);
        return;
    }

    prompt_clear_area();
    text_display_string("Loading...Please Wait", 0, 10, 0x18, 0);
    gbl.reload_ecl_and_pictures = true;

    /* Blocks in the order they were written, no seeking. Every read overwrites
     * the front of one buffer, exactly as the original's single `data` did. */
    file_block_read(&file, data, 1);
    gbl.game_area = data[0];

    file_block_read(&file, data, AREA_BLOCK_SIZE);
    area1_read(gbl.area_ptr, data, AREA_BLOCK_SIZE, 0);

    file_block_read(&file, data, AREA_BLOCK_SIZE);
    area2_read(gbl.area2_ptr, data, AREA_BLOCK_SIZE, 0);

    file_block_read(&file, data, ECL_VARS_SIZE);
    ecl_vars_load(gbl.ecl_vars, data, ECL_VARS_SIZE, 0);

    file_block_read(&file, data, ECL_BLOCK_SIZE);
    ecl_block_set_data(gbl.ecl_ptr, data, ECL_BLOCK_SIZE, 0, ECL_BLOCK_SIZE);

    file_block_read(&file, data, 5);
    gbl.map_pos_x     = (i8)data[0];
    gbl.map_pos_y     = (i8)data[1];
    gbl.map_direction = data[2];
    gbl.map_wall_type = data[3];
    gbl.map_wall_roof = data[4];

    file_block_read(&file, data, 1);
    gbl.last_game_state = (GameState)data[0];

    file_block_read(&file, data, 1);
    gbl.game_state = (GameState)data[0];

    /* Written as one twelve-byte block, read back as six two-byte ones: the same
     * bytes off the same stream, and the pairs come out blockId then setId. */
    for (int i = 0; i < GBL_SET_BLOCKS; i++) {
        file_block_read(&file, data, 2);
        gbl.set_blocks[i].block_id = sys_array_to_short(data, 0);

        file_block_read(&file, data, 2);
        gbl.set_blocks[i].set_id = sys_array_to_short(data, 0);
    }

    file_block_read(&file, data, 1);
    number_of_players = data[0];

    if (number_of_players > 8) {
        /* Eight names is all the 0x148 bytes hold. The original read past them
         * into the rest of `data`, which held whatever the ECL block had left
         * there; here the count is what the buffer can answer for. */
        log_warn("load game: %s claims %d characters, keeping 8", path,
                 number_of_players);
        number_of_players = 8;
    }

    file_block_read(&file, data, 0x148);
    for (int i = 0; i < number_of_players; i++) {
        sys_array_to_string(names[i], sizeof(names[i]), data,
                            (size_t)(0x29 * i), 0x29);
    }

    file_close(&file);

    /* Commented out in the C# as well: the two flags are written on save and
     * then not read back, so they keep whatever the running game had.
     * gbl.pics_on       = (gbl.area_ptr->pics_on >> 1) != 0;
     * gbl.animations_on = (gbl.area_ptr->pics_on & 1) != 0; */
    gbl.game_speed_var = gbl.area_ptr->game_speed;
    gbl.area2_ptr->party_size = 0;

    for (int index = 0; index < number_of_players; index++) {
        char cleaned[16];
        char sav_name[24];

        file_clean_string(cleaned, sizeof(cleaned), names[index]);
        snprintf(sav_name, sizeof(sav_name), "%s.sav", cleaned);

        if (file_exists(vfs_save_resolve(path, sizeof(path), sav_name))) {
            Player *player = roster_alloc();

            if (player == NULL) {
                log_warn("load game: no room for character %s", cleaned);
                continue;
            }

            savegame_import_char(player, sav_name);
            partymenu_assign_player_icon_id(player);
        }
    }

    /* A character who is in a saved game is not also a loose character, so the
     * .guy files they were added from go. */
    for (int i = 0; i < gbl.team_count; i++) {
        savegame_remove_player_file(gbl.team_list[i]);
    }

    for (int i = 0; i < gbl.team_count; i++) {
        gbl.selected_player = gbl.team_list[i];

        if (gbl.selected_player->control_morale < CONTROL_NPC_BASE) {
            partymenu_load_player_combat_icon(true);
        } else {
            icons_chead_cbody_comspr_icon(gbl.selected_player->icon_id,
                                          gbl.selected_player->mod_id, "CPIC");
        }
    }

    if (gbl.team_count > 0) {
        gbl.selected_player = gbl.team_list[0];
    } else {
        /* The original indexed TeamList[0] unconditionally and would have thrown
         * on a save whose every character file had gone missing. */
        log_warn("load game: %s loaded with nobody in the party", path);
        gbl.selected_player = NULL;
    }

    gbl.game_area = gbl.area2_ptr->game_area;

    if (gbl.area_ptr->in_dungeon != 0) {
        if (gbl.game_state != GAME_STATE_START_GAME_MENU) {
            /* The first set's block id gates the 3D map, not its own. */
            if (gbl.set_blocks[0].block_id > 0) {
                view3d_load_3d_map(gbl.area_ptr->current_3d_map_block_id);
            }

            for (int i = 0; i < GBL_SET_BLOCKS; i++) {
                if (gbl.set_blocks[i].block_id > 0) {
                    view3d_load_walldef(gbl.set_blocks[i].set_id,
                                        gbl.set_blocks[i].block_id);
                }
            }
        }
    } else {
        picture_load_bigpic(0x79);
    }

    input_clear_keyboard();
    prompt_clear_area();

    /* Both, in this order: the state the file named becomes the previous state
     * and the game comes up in the start menu, which is where the loaded game is
     * actually picked up from. */
    gbl.last_game_state = gbl.game_state;
    gbl.game_state = GAME_STATE_START_GAME_MENU;
}

void savegame_save_game(void)
{
    GameFile save_file;
    char path[SAVE_PATH_MAX];
    char input_key;
    u8 data[ECL_BLOCK_SIZE];
    int party_count;

    memset(&save_file, 0, sizeof(save_file));

    /* unk_4AEA0: one of the ten slots, or escape. */
    do {
        input_key = prompt_display_input_simple(gbl.game_state == GAME_STATE_CAMPING,
                                               0, GBL_DEFAULT_MENU_COLORS,
                                               "A B C D E F G H I J",
                                               "Save Which Game: ");
    } while (input_key != 0x00 && !save_slot_key(input_key));

    if (input_key == 0x00) {
        return;
    }

    gbl.import_from = IMPORT_SOURCE_CURSE;

    file_assign(&save_file, save_path(path, sizeof(path), "savgam%c.dat",
                                      input_key));

    /* The original tested gbl.FIND_result here, in a loop that could only run
     * once, and showed "Unexpected error during save: <n>" for anything outside
     * {0, 2, 18}. Nothing in the decompiled engine ever assigns FIND_result, so
     * that path was dead and the error was never seen. Here the real result of
     * truncating the file is what decides, which makes it live - a save
     * directory that cannot be written now says so instead of silently
     * reporting success. */
    if (!file_rewrite(&save_file)) {
        text_display_and_pause("Unexpected error during save", 14);
        file_close(&save_file);
        return;
    }

    prompt_clear_area();
    text_display_string("Saving...Please Wait", 0, 10, 0x18, 0);

    gbl.area_ptr->game_speed = (u8)gbl.game_speed_var;
    gbl.area_ptr->pics_on = (u8)((gbl.pics_on ? 0x02 : 0) |
                                 (gbl.animations_on ? 0x01 : 0));
    gbl.area2_ptr->game_area = gbl.game_area;

    memset(data, 0, sizeof(data));

    data[0] = gbl.game_area;
    file_block_write(&save_file, data, 1);

    area1_write(gbl.area_ptr, data, AREA_BLOCK_SIZE);
    file_block_write(&save_file, data, AREA_BLOCK_SIZE);

    area2_write(gbl.area2_ptr, data, AREA_BLOCK_SIZE);
    file_block_write(&save_file, data, AREA_BLOCK_SIZE);

    ecl_vars_write(gbl.ecl_vars, data, ECL_VARS_SIZE);
    file_block_write(&save_file, data, ECL_VARS_SIZE);

    ecl_block_write(gbl.ecl_ptr, data, ECL_BLOCK_SIZE);
    file_block_write(&save_file, data, ECL_BLOCK_SIZE);

    memset(data, 0, sizeof(data));

    data[0] = (u8)gbl.map_pos_x;
    data[1] = (u8)gbl.map_pos_y;
    data[2] = gbl.map_direction;
    data[3] = gbl.map_wall_type;
    data[4] = gbl.map_wall_roof;
    file_block_write(&save_file, data, 5);

    data[0] = (u8)gbl.last_game_state;
    file_block_write(&save_file, data, 1);
    data[0] = (u8)gbl.game_state;
    file_block_write(&save_file, data, 1);

    for (int i = 0; i < GBL_SET_BLOCKS; i++) {
        sys_short_to_array((i16)gbl.set_blocks[i].block_id, data,
                           (size_t)(i * 4) + 0);
        sys_short_to_array((i16)gbl.set_blocks[i].set_id, data,
                           (size_t)(i * 4) + 2);
    }
    file_block_write(&save_file, data, 12);

    /* Eight names of 0x29 bytes is 0x148, and a party is at most eight. */
    party_count = gbl.team_count;

    if (party_count > 8) {
        log_warn("save game: %d in the party, saving the first 8", party_count);
        party_count = 8;
    }

    data[0] = (u8)party_count;
    file_block_write(&save_file, data, 1);

    memset(data, 0, 0x148);
    for (int i = 0; i < party_count; i++) {
        char file_text[16];

        snprintf(file_text, sizeof(file_text), "CHRDAT%c%d", input_key, i + 1);
        sys_string_to_array(data, (size_t)(0x29 * i), 0x29, file_text);
    }
    file_block_write(&save_file, data, 0x148);
    file_close(&save_file);

    for (int i = 0; i < party_count; i++) {
        char file_text[16];

        snprintf(file_text, sizeof(file_text), "CHRDAT%c%d", input_key, i + 1);
        savegame_save_player(file_text, gbl.team_list[i]);
        savegame_remove_player_file(gbl.team_list[i]);
    }

    gbl.game_saved = true;
    prompt_clear_area();
}
