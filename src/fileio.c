/* fileio.c - Ported from engine/seg051.cs and engine/seg042.cs. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fileio.h"

#include "gbl.h"
#include "log.h"
#include "vfs.h"

/* seg042.unk_16FA9 - the characters clean_string trims off both ends. */
static const char CLEAN_TRIM_CHARS[] = " .*,?/\\:;|";

/* ------------------------------------------------------------------ opening */

bool file_assign(GameFile *f, const char *full_path)
{
    memset(f, 0, sizeof(*f));

    if (strlen(full_path) >= sizeof(f->path)) {
        log_error("file: path too long: %s", full_path);
        return false;
    }
    /* "r+" fails when the file does not exist and "w+" would truncate one that
     * does, so a missing file is created first. This is FileMode.OpenOrCreate. */
    f->fp = vfs_fopen(full_path, "r+b");
    if (f->fp == NULL && errno == ENOENT) {
        f->fp = vfs_fopen(full_path, "w+b");
    }
    if (f->fp == NULL) {
        log_error("file: cannot open %s: %s", full_path, strerror(errno));
        return false;
    }

    snprintf(f->path, sizeof(f->path), "%s", full_path);
    return true;
}

bool file_find_and_open(GameFile *f, bool no_error, const char *full_path)
{
    const char *path = full_path;

    memset(f, 0, sizeof(*f));

    /* The C# joined a bare filename onto gbl.exe_path. Here a bare name means a
     * data file, so it is resolved case-insensitively against the data
     * directory; the shipped files are all upper case and the engine asks for
     * them in lower. */
    if (strchr(full_path, '/') == NULL) {
        const char *resolved = vfs_resolve(full_path);

        if (resolved != NULL) {
            path = resolved;
        }
    }

    if (!file_exists(path)) {
        if (!no_error) {
            /* debug_display() put this on screen and waited for a key. */
            log_error("Couldn't find %s. Check install.", full_path);
        }
        return false;
    }

    if (!file_assign(f, path)) {
        return false;
    }

    file_reset(f);
    return true;
}

bool file_exists(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode) != 0;
}

void file_delete(const char *path)
{
    if (!file_exists(path)) {
        return;
    }
    if (unlink(path) != 0) {
        log_warn("file: cannot delete %s: %s", path, strerror(errno));
    }
}

/* ------------------------------------------------------- reading and writing */

void file_reset(GameFile *f)
{
    if (f->fp == NULL) {
        log_warn("file: Reset on a file that is not open");
        return;
    }
    if (fseek(f->fp, 0, SEEK_SET) != 0) {
        log_warn("file: cannot rewind %s: %s", f->path, strerror(errno));
    }
}

bool file_rewrite(GameFile *f)
{
    if (f->fp == NULL) {
        log_warn("file: Rewrite on a file that is not open");
        return false;
    }
    /* SetLength(0). The buffer has to go first or buffered bytes would be
     * written back past the truncation point. */
    fflush(f->fp);
    if (ftruncate(fileno(f->fp), 0) != 0) {
        log_warn("file: cannot truncate %s: %s", f->path, strerror(errno));
        return false;
    }
    file_reset(f);
    return true;
}

void file_close(GameFile *f)
{
    if (f->fp == NULL) {
        return;
    }
    if (fclose(f->fp) != 0) {
        log_warn("file: error closing %s: %s", f->path, strerror(errno));
    }
    f->fp = NULL;
}

size_t file_block_read(GameFile *f, void *dst, size_t count)
{
    size_t got;

    if (f->fp == NULL) {
        log_warn("file: BlockRead on a file that is not open");
        return 0;
    }

    got = fread(dst, 1, count, f->fp);
    if (got < count && ferror(f->fp)) {
        log_warn("file: error reading %s: %s", f->path, strerror(errno));
    }
    return got;
}

bool file_block_write(GameFile *f, const void *src, size_t count)
{
    if (f->fp == NULL) {
        log_warn("file: BlockWrite on a file that is not open");
        return false;
    }

    if (fwrite(src, 1, count, f->fp) != count) {
        log_error("file: error writing %s: %s", f->path, strerror(errno));
        return false;
    }
    return true;
}

void file_fill_char(u8 fill_byte, size_t buffer_size, u8 *buffer)
{
    memset(buffer, fill_byte, buffer_size);
}

/* ------------------------------------------------------------------ strings */

char *file_copy_string(char *dst, size_t dst_size, int copy_len, int start_at,
                       const char *in_string)
{
    int len = (int)strlen(in_string);
    int avail;

    if (dst_size == 0) {
        return dst;
    }
    dst[0] = '\0';

    if (start_at < 0 || start_at > len) {
        /* Substring would have thrown; the engine never asks for this. */
        log_warn("Copy: start %d is outside a %d character string", start_at, len);
        return dst;
    }

    avail = len - start_at;
    if (copy_len >= avail) {
        copy_len = avail;
    }
    if (copy_len <= 0) {
        return dst;
    }
    if ((size_t)copy_len >= dst_size) {
        copy_len = (int)dst_size - 1;
    }

    memcpy(dst, in_string + start_at, (size_t)copy_len);
    dst[copy_len] = '\0';
    return dst;
}

char *file_clean_string(char *dst, size_t dst_size, const char *s)
{
    size_t start = 0;
    size_t end = strlen(s);
    size_t out = 0;

    if (dst_size == 0) {
        return dst;
    }

    while (start < end && strchr(CLEAN_TRIM_CHARS, s[start]) != NULL) {
        start++;
    }
    while (end > start && strchr(CLEAN_TRIM_CHARS, s[end - 1]) != NULL) {
        end--;
    }

    /* Trim, lowercase, then at most eight characters - a DOS basename. */
    while (start < end && out < 8 && out + 1 < dst_size) {
        char c = s[start++];

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        dst[out++] = c;
    }
    dst[out] = '\0';
    return dst;
}

/* --------------------------------------------------------------- game area */

void file_set_game_area(u8 area)
{
    gbl.game_area_backup = gbl.game_area;
    gbl.game_area = area;
}

void file_restore_game_area(void)
{
    gbl.game_area = gbl.game_area_backup;
}
