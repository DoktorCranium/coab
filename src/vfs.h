/* vfs.h - data-file location and case-insensitive name resolution.
 *
 * The engine hardcodes DOS names and lowercases them before opening
 * ("8x8d1.dax", "title.dax", "ecl1.dax"), while the shipped data files are all
 * uppercase ("8X8D1.DAX"). DOS and Windows did not care; Linux does. Every
 * data-file open therefore goes through vfs_resolve().
 */
#ifndef COAB_VFS_H
#define COAB_VFS_H

#include "coab.h"

#include <stdio.h>

/* fopen() for a file the game reads or writes as a stream of bytes - a data
 * file, a savegame, a dumped image. Identical to fopen() everywhere except
 * OpenVMS, where it adds the RMS options without which "rb" and "wb" are not
 * binary at all; see vms_compat.h. log.c's text log is the one file that
 * deliberately does not come through here. */
FILE *vfs_fopen(const char *path, const char *mode);

/* Locates the game data directory and indexes its contents.
 * data_dir_hint may be NULL, in which case $COAB_DATA and a list of paths
 * relative to the executable and cwd are tried. Returns false if no directory
 * containing the expected data files could be found. */
bool vfs_init(const char *argv0, const char *data_dir_hint);
void vfs_shutdown(void);

const char *vfs_data_dir(void);   /* e.g. "/opt/.../coab/Data" */
const char *vfs_save_dir(void);   /* $XDG_DATA_HOME/coab/save */
const char *vfs_log_dir(void);    /* $XDG_STATE_HOME/coab/logs */

/* Points the save directory somewhere else, creating it. Exists so the
 * self-test can write and delete saved games without touching the ones the
 * player has; nothing in the game itself calls it. Returns false, leaving the
 * directory as it was, if the path is unusable. */
bool vfs_set_save_dir(const char *dir);

/* Maps a DOS-style basename to a real path in the data directory, ignoring
 * case. Returns NULL when there is no such file. The returned string points
 * into an internal buffer valid until the next vfs_resolve() call. */
const char *vfs_resolve(const char *name);

/* Reads an entire file. Caller frees. Returns NULL on error; *out_size is set
 * to the byte count on success. */
u8 *vfs_read_file(const char *path, size_t *out_size);

/* Builds a path inside the save directory, creating the directory if needed.
 * Writes into dst (dst_size bytes) and returns dst. */
char *vfs_save_path(char *dst, size_t dst_size, const char *name);

/* vfs_save_path for a file about to be read. If there is no such file but one
 * differing from it only in case exists, that one's path is returned instead.
 *
 * DOS and Windows both treated save names as case-insensitive, and the engine
 * relied on it: SaveGame writes CHRDAT<letter><n>.sav while loadSaveGame looks
 * for the same name put through clean_string, which lowercases. A save directory
 * carried over from a DOS or Windows install holds uppercase names throughout.
 * Writes always use vfs_save_path, so this port's own files are named exactly as
 * the engine asked for them. */
char *vfs_save_resolve(char *dst, size_t dst_size, const char *name);

/* mkdir -p. Returns true if the directory exists afterwards. */
bool vfs_mkdir_p(const char *path);

/* Directory.GetFiles(Config.GetSavePath(), "*<suffix>"), which is how ovr017
 * finds saved characters and saved games. `visit` is handed each matching
 * basename - not a full path - and whatever `user` was passed; a NULL suffix
 * matches everything. The comparison ignores case, because the engine writes
 * lowercase names and a save directory carried over from DOS holds uppercase
 * ones.
 *
 * Returns how many files were visited, or -1 if the save directory could not be
 * read at all - which the callers treat as "no saves", the same answer an empty
 * directory gives.
 *
 * Divergence: the names are sorted, case-insensitively, before any is visited.
 * `Directory.GetFiles` and DOS's FindFirst/FindNext both answered in directory
 * order, which readdir does not reproduce and which would leave the order of the
 * Add Character list changing between runs on the same save directory. */
typedef void (*VfsFileVisitor)(const char *name, void *user);
int vfs_for_each_save_file(const char *suffix, VfsFileVisitor visit, void *user);

/* Joins "a/b" into dst. Returns false, leaving dst empty, if the result would
 * not fit - callers must not act on a silently truncated path. */
bool vfs_path_join(char *dst, size_t dst_size, const char *a, const char *b);

#endif /* COAB_VFS_H */
