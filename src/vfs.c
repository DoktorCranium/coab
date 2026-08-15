#include "vfs.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char lower[64];   /* name folded to lower case, for lookup */
    char real[64];    /* name exactly as it appears on disk */
} DirEntry;

static char      g_data_dir[PATH_MAX];
static char      g_save_dir[PATH_MAX];
static char      g_log_dir[PATH_MAX];
static DirEntry *g_entries;
static size_t    g_entry_count;
static size_t    g_entry_cap;
static char      g_resolve_buf[PATH_MAX];

static void str_to_lower(char *s)
{
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

/* A directory as stat() and opendir() want it named. On OpenVMS that is "dir/"
 * in UNIX syntax or "[dir]" in VMS syntax; a bare "dir" - and "./dir" too -
 * names a FILE called dir with no type, so the data directory would look
 * missing. Adding the slash here rather than at the call sites keeps the paths
 * the rest of this file builds for fopen() exactly as they are everywhere
 * else. Returns `dir` unchanged on every other platform. */
static const char *dir_spec(char *dst, size_t dst_size, const char *dir)
{
#ifdef __VMS
    size_t len = dir ? strlen(dir) : 0;

    if (len > 0 && dir[len - 1] != '/' && dir[len - 1] != ']' &&
        len + 2 <= dst_size) {
        memcpy(dst, dir, len);
        dst[len] = '/';
        dst[len + 1] = '\0';
        return dst;
    }
#else
    (void)dst;
    (void)dst_size;
#endif
    return dir;
}

/* A directory entry with the OpenVMS file version taken off: readdir() there
 * reports "TITLE.DAX;1". Every comparison in this file is against a name the
 * engine asked for, which never has a version, and the stripped name is also
 * what has to be handed back to the caller - it gets displayed and reopened.
 * Dropping the version reopens the latest one, which is what the DOS original
 * did with the only copy it had. Returns `name` unchanged elsewhere. */
static const char *strip_version(char *dst, size_t dst_size, const char *name)
{
#ifdef __VMS
    const char *semi = strchr(name, ';');

    if (semi != NULL) {
        size_t len = (size_t)(semi - name);

        if (len >= dst_size) {
            len = dst_size - 1;
        }
        memcpy(dst, name, len);
        dst[len] = '\0';
        return dst;
    }
#else
    (void)dst;
    (void)dst_size;
#endif
    return name;
}

static bool path_is_dir(const char *path)
{
    struct stat st;
    char spec[PATH_MAX];

    return stat(dir_spec(spec, sizeof(spec), path), &st) == 0 &&
           S_ISDIR(st.st_mode);
}

static bool path_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool vfs_path_join(char *dst, size_t dst_size, const char *a, const char *b)
{
    size_t la, lb;

    if (!dst || dst_size == 0) {
        return false;
    }
    dst[0] = '\0';
    if (!a || !b) {
        return false;
    }

    la = strlen(a);
    lb = strlen(b);

    /* a + '/' + b + NUL */
    if (la + 1 + lb + 1 > dst_size) {
        return false;
    }

    memcpy(dst, a, la);
    dst[la] = '/';
    memcpy(dst + la + 1, b, lb);
    dst[la + 1 + lb] = '\0';
    return true;
}

bool vfs_mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    size_t len;

    /* The current directory always exists, and asking the OS about it is the one
     * case that is not portable: mkdir(".") is EEXIST on Linux but need not be,
     * and on OpenVMS - where this is the save and log directory - stat(".")
     * answers for a file called "." rather than for the directory. */
    if (path != NULL && path[0] == '.' && path[1] == '\0') {
        return true;
    }

    if (path_is_dir(path)) {
        return true;
    }

    len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return false;
    }
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return path_is_dir(path);
}

/* ------------------------------------------------------------- data indexing */

static bool entries_push(const char *name)
{
    size_t len = strlen(name);

    if (len >= sizeof(g_entries[0].real)) {
        return false;   /* not a DOS 8.3 data name; ignore */
    }

    if (g_entry_count == g_entry_cap) {
        size_t cap = g_entry_cap ? g_entry_cap * 2 : 128;
        DirEntry *grown = realloc(g_entries, cap * sizeof(*grown));
        if (!grown) {
            return false;
        }
        g_entries = grown;
        g_entry_cap = cap;
    }

    DirEntry *e = &g_entries[g_entry_count++];
    memcpy(e->real, name, len + 1);
    memcpy(e->lower, name, len + 1);
    str_to_lower(e->lower);
    return true;
}

static bool index_dir(const char *dir)
{
    char spec[PATH_MAX];
    DIR *d = opendir(dir_spec(spec, sizeof(spec), dir));
    struct dirent *de;

    if (!d) {
        return false;
    }

    g_entry_count = 0;
    while ((de = readdir(d)) != NULL) {
        /* Deliberately longer than the 64 entries_push accepts, so that a name
         * too long to store is still rejected there rather than truncated here
         * into a shorter one that would appear to exist. */
        char name[96];

        if (de->d_name[0] == '.') {
            continue;
        }
        entries_push(strip_version(name, sizeof(name), de->d_name));
    }
    closedir(d);
    return true;
}

/* A directory only counts as the data directory if it holds the files the
 * engine cannot start without. Checking a couple of them keeps us from
 * latching onto, say, the source tree root. */
static bool looks_like_data_dir(const char *dir)
{
    static const char *required[] = { "title.dax", "8x8d1.dax", "ecl1.dax" };

    if (!path_is_dir(dir) || !index_dir(dir)) {
        return false;
    }

    for (size_t i = 0; i < COAB_ARRAY_LEN(required); i++) {
        bool found = false;
        for (size_t j = 0; j < g_entry_count && !found; j++) {
            found = strcmp(g_entries[j].lower, required[i]) == 0;
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static void dir_of(char *dst, size_t dst_size, const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(dst, dst_size, ".");
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len == 0) {
        len = 1;   /* path was "/foo" */
    }
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, path, len);
    dst[len] = '\0';
}

/* --------------------------------------------------------------- user dirs */

static void user_dir(char *dst, size_t dst_size, const char *xdg_var,
                     const char *fallback_rel, const char *leaf)
{
#ifdef __VMS
    /* The current default directory, which is where the sibling OpenVMS ports
     * put their savegames and logs too. There is no XDG equivalent to aim at,
     * and none of "$HOME/.local/share/coab/save" survives the trip: getenv
     * ("HOME") answers in VMS syntax ("DKA0:[SMITH]"), which cannot be joined
     * onto a UNIX-style tail, and ".local" is not a legal ODS-2 directory name.
     * So the directory that needs write access is the one the game is RUN from,
     * not the one holding COAB.EXE. */
    (void)xdg_var;
    (void)fallback_rel;
    (void)leaf;
    snprintf(dst, dst_size, ".");
#else
    const char *base = getenv(xdg_var);
    const char *home = getenv("HOME");

    if (base && base[0] == '/') {
        snprintf(dst, dst_size, "%s/coab/%s", base, leaf);
    } else if (home && home[0] != '\0') {
        snprintf(dst, dst_size, "%s/%s/coab/%s", home, fallback_rel, leaf);
    } else {
        snprintf(dst, dst_size, "./coab-%s", leaf);
    }
#endif
}

/* ------------------------------------------------------------------- public */

bool vfs_init(const char *argv0, const char *data_dir_hint)
{
    char exe_dir[PATH_MAX];
    char candidate[PATH_MAX];
    const char *env = getenv("COAB_DATA");

    user_dir(g_save_dir, sizeof(g_save_dir), "XDG_DATA_HOME",  ".local/share", "save");
    user_dir(g_log_dir,  sizeof(g_log_dir),  "XDG_STATE_HOME", ".local/state", "logs");

    /* Explicit choices win, and fail loudly rather than silently falling back
     * to some other directory the user did not mean. */
    if (data_dir_hint && data_dir_hint[0]) {
        if (looks_like_data_dir(data_dir_hint)) {
            snprintf(g_data_dir, sizeof(g_data_dir), "%s", data_dir_hint);
            return true;
        }
        log_error("--data '%s' does not contain the game data files", data_dir_hint);
        return false;
    }
    if (env && env[0]) {
        if (looks_like_data_dir(env)) {
            snprintf(g_data_dir, sizeof(g_data_dir), "%s", env);
            return true;
        }
        log_error("COAB_DATA='%s' does not contain the game data files", env);
        return false;
    }

    dir_of(exe_dir, sizeof(exe_dir), argv0 ? argv0 : ".");

    static const char *rel[] = { "Data", "data", "DATA", ".", "../Data", "../../Data" };
    const char *roots[] = { ".", exe_dir, NULL };

    for (int r = 0; roots[r]; r++) {
        for (size_t i = 0; i < COAB_ARRAY_LEN(rel); i++) {
            if (!vfs_path_join(candidate, sizeof(candidate), roots[r], rel[i])) {
                continue;
            }
            if (looks_like_data_dir(candidate)) {
                snprintf(g_data_dir, sizeof(g_data_dir), "%s", candidate);
                return true;
            }
        }
    }

    /* Baked in by ./configure --with-game-data or -DCOAB_DATA_DIR, so an
     * installed copy works with no arguments. Tried last, so running from a
     * source tree still prefers the Data directory sitting next to it. */
#ifdef COAB_DATA_DIR
    if (looks_like_data_dir(COAB_DATA_DIR)) {
        snprintf(g_data_dir, sizeof(g_data_dir), "%s", COAB_DATA_DIR);
        return true;
    }
#endif

    log_error("could not locate the game data directory; "
              "pass --data <dir> or set COAB_DATA");
    return false;
}

void vfs_shutdown(void)
{
    free(g_entries);
    g_entries = NULL;
    g_entry_count = g_entry_cap = 0;
}

const char *vfs_data_dir(void) { return g_data_dir; }
const char *vfs_save_dir(void) { return g_save_dir; }
const char *vfs_log_dir(void)  { return g_log_dir; }

bool vfs_set_save_dir(const char *dir)
{
    if (dir == NULL || dir[0] == '\0' || strlen(dir) >= sizeof(g_save_dir)) {
        return false;
    }
    if (!vfs_mkdir_p(dir)) {
        return false;
    }

    snprintf(g_save_dir, sizeof(g_save_dir), "%s", dir);
    return true;
}

const char *vfs_resolve(const char *name)
{
    char lower[64];

    if (!name || !name[0]) {
        return NULL;
    }

    /* An absolute or explicitly-relative path is used as given. */
    if (strchr(name, '/') != NULL) {
        return path_is_file(name) ? name : NULL;
    }

    if (strlen(name) >= sizeof(lower)) {
        return NULL;
    }
    snprintf(lower, sizeof(lower), "%s", name);
    str_to_lower(lower);

    for (size_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].lower, lower) == 0) {
            if (!vfs_path_join(g_resolve_buf, sizeof(g_resolve_buf),
                               g_data_dir, g_entries[i].real)) {
                return NULL;
            }
            return g_resolve_buf;
        }
    }
    return NULL;
}

FILE *vfs_fopen(const char *path, const char *mode)
{
#ifdef __VMS
    /* "rb" and "wb" mean nothing on OpenVMS - there is no text mode to turn off
     * - and without "ctx=stm" the CRTL goes through the RMS record layer, which
     * inserts, strips and pads record boundaries. Every byte offset the DAX
     * block reader and the savegame record writer compute would then land in the
     * wrong place. See vms_compat.h; the mode test picks stream-LF for a file
     * being created, which is what the rest of a VMS system calls binary. */
    if (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL) {
        return VMS_FOPEN_CREATE(path, mode);
    }
    return VMS_FOPEN_READ(path, mode);
#else
    return fopen(path, mode);
#endif
}

u8 *vfs_read_file(const char *path, size_t *out_size)
{
    FILE *f;
    long size;
    u8 *buf;

    if (out_size) {
        *out_size = 0;
    }
    if (!path) {
        return NULL;
    }

    f = vfs_fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    buf[size] = 0;
    if (out_size) {
        *out_size = (size_t)size;
    }
    return buf;
}

char *vfs_save_path(char *dst, size_t dst_size, const char *name)
{
    vfs_mkdir_p(g_save_dir);
    vfs_path_join(dst, dst_size, g_save_dir, name);
    return dst;
}

char *vfs_save_resolve(char *dst, size_t dst_size, const char *name)
{
    DIR *d;
    struct dirent *de;
    char spec[PATH_MAX];

    vfs_save_path(dst, dst_size, name);

    if (dst[0] == '\0' || path_is_file(dst)) {
        return dst;
    }

    d = opendir(dir_spec(spec, sizeof(spec), g_save_dir));
    if (!d) {
        return dst;
    }

    while ((de = readdir(d)) != NULL) {
        char entry[96];
        const char *real = strip_version(entry, sizeof(entry), de->d_name);

        if (strcasecmp(real, name) == 0) {
            char candidate[PATH_MAX];

            if (vfs_path_join(candidate, sizeof(candidate), g_save_dir,
                              real) && path_is_file(candidate)) {
                snprintf(dst, dst_size, "%s", candidate);
                break;
            }
        }
    }
    closedir(d);

    return dst;
}

/* ---------------------------------------------------------- listing the saves */

static bool name_has_suffix(const char *name, const char *suffix)
{
    size_t nl = strlen(name);
    size_t sl = strlen(suffix);

    if (sl > nl) {
        return false;
    }

    for (size_t i = 0; i < sl; i++) {
        if (tolower((unsigned char)name[nl - sl + i]) !=
            tolower((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

static int save_name_cmp(const void *a, const void *b)
{
    const char *const *x = a;
    const char *const *y = b;
    int diff = strcasecmp(*x, *y);

    /* Two names differing only in case would otherwise sort unpredictably. */
    return (diff != 0) ? diff : strcmp(*x, *y);
}

int vfs_for_each_save_file(const char *suffix, VfsFileVisitor visit, void *user)
{
    DIR *d;
    struct dirent *de;
    char **names = NULL;
    size_t count = 0;
    size_t cap = 0;
    char path[PATH_MAX];
    char spec[PATH_MAX];

    vfs_mkdir_p(g_save_dir);

    d = opendir(dir_spec(spec, sizeof(spec), g_save_dir));
    if (!d) {
        log_debug("save directory %s cannot be read: %s", g_save_dir,
                  strerror(errno));
        return -1;
    }

    while ((de = readdir(d)) != NULL) {
        char entry[96];
        const char *real = strip_version(entry, sizeof(entry), de->d_name);

        if (real[0] == '.') {
            continue;
        }
        if (suffix != NULL && !name_has_suffix(real, suffix)) {
            continue;
        }
        if (!vfs_path_join(path, sizeof(path), g_save_dir, real) ||
            !path_is_file(path)) {
            continue;
        }

        if (count == cap) {
            size_t new_cap = cap ? cap * 2 : 32;
            char **grown = realloc(names, new_cap * sizeof(*grown));

            if (!grown) {
                break;      /* keep what we have rather than lose the lot */
            }
            names = grown;
            cap = new_cap;
        }

        names[count] = strdup(real);
        if (names[count] == NULL) {
            break;
        }
        count++;
    }
    closedir(d);

    if (count > 1) {
        qsort(names, count, sizeof(*names), save_name_cmp);
    }

    for (size_t i = 0; i < count; i++) {
        if (visit != NULL) {
            visit(names[i], user);
        }
        free(names[i]);
    }
    free(names);

    return (int)count;
}
