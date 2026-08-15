/* fileio.h - the engine's file handling.
 * Ported from engine/seg051.cs (Reset, Rewrite, Close, BlockRead, BlockWrite,
 * FillChar, Copy) and engine/seg042.cs (find_and_open_file, file_find,
 * delete_file, clean_string, set_game_area, restore_game_area).
 *
 * seg051 is Turbo Pascal's file interface as the decompiler found it: an opened
 * file is a handle the engine reads and writes fixed-size blocks through, with
 * no seeking except back to the start. GameFile is that handle.
 *
 * Everything a save game is made of goes through here. Data files do not: they
 * are DAX archives and go through dax.h, which does its own opening.
 */
#ifndef COAB_FILEIO_H
#define COAB_FILEIO_H

#include <stdio.h>

#include "coab.h"

#define GAME_FILE_PATH_MAX 4096

/* Classes/File.cs. `fp` is NULL for a handle that is not open, which is what
 * the C# used a null File reference for. */
typedef struct {
    FILE *fp;
    char  path[GAME_FILE_PATH_MAX];
} GameFile;

/* seg042.find_and_open_file. Opens `full_path` for reading and writing,
 * creating it if it does not exist, and rewinds it - File.Assign used
 * OpenOrCreate and seg042 followed it with Reset.
 *
 * A path with no directory part is looked up in the game data directory,
 * ignoring case, which is what the C# did by falling back to gbl.exe_path.
 *
 * When the file is missing and `no_error` is false the original stopped and
 * showed "Couldn't find ... Check install." on screen; here that becomes a
 * logged error, because this can be reached before the screen exists.
 *
 * Returns false with f->fp NULL if the file could not be opened. */
bool file_find_and_open(GameFile *f, bool no_error, const char *full_path);

/* File.Assign: opens for reading and writing, creating if needed, without the
 * existence check. Does not truncate; call file_rewrite() for that. */
bool file_assign(GameFile *f, const char *full_path);

/* seg042.file_find - does this path name an existing regular file? */
bool file_exists(const char *path);

/* seg042.delete_file - silently does nothing if there is no such file. */
void file_delete(const char *path);

/* seg051.Reset - back to the start. */
void file_reset(GameFile *f);

/* seg051.Rewrite - truncates to nothing and rewinds. */
bool file_rewrite(GameFile *f);

/* seg051.Close. Safe to call on a handle that was never opened, and leaves the
 * handle closed so a double close cannot happen. */
void file_close(GameFile *f);

/* seg051.BlockRead - reads up to `count` bytes and returns how many arrived,
 * as the original's return value did. A short read is not an error here: the
 * save loader relies on being told the count. */
size_t file_block_read(GameFile *f, void *dst, size_t count);

/* seg051.BlockWrite. Returns false, having logged, on a short write - the C#
 * would have thrown. */
bool file_block_write(GameFile *f, const void *src, size_t count);

/* seg051.FillChar */
void file_fill_char(u8 fill_byte, size_t buffer_size, u8 *buffer);

/* seg051.Copy - Turbo Pascal's Copy(s, start, len) with the arguments in the
 * order the decompiler produced. Copies at most `copy_len` characters starting
 * at `start_at`, clamped to what is there, into dst, always terminated.
 * Returns dst. */
char *file_copy_string(char *dst, size_t dst_size, int copy_len, int start_at,
                       const char *in_string);

/* seg042.clean_string - trims " .*,?/\\:;|" from both ends, lowercases, and
 * keeps at most the first eight characters, which is how the engine turns a
 * name the player typed into a DOS filename. Returns dst. */
char *file_clean_string(char *dst, size_t dst_size, const char *s);

/* seg042.set_game_area / restore_game_area - which chapter's numbered data
 * files are in use. Only one level of backup, as in the original. */
void file_set_game_area(u8 area);
void file_restore_game_area(void);

#endif /* COAB_FILEIO_H */
