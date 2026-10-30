/* storage.h - the SD card (SD2SP2 or SD Gecko), Flashcube's data folder
 * (settings, favourites, thumbnail cache) and file helpers. Photos
 * themselves live in sd:/DCIM.
 *
 * The data folder is the folder Flashcube's .dol is in (usually
 * sd:/apps/flashcube); a .dol at the root of the card uses sd:/flashcube,
 * as older versions did. Settings left in sd:/flashcube by an older version
 * are moved over the first time. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define LEGACY_DATA_DIR "sd:/flashcube"   /* where older versions kept everything */
#define APP_DIR "sd:/apps/flashcube"     /* where launchers install Flashcube */
#define RESTART_NOTE "sd:/flashcube-restart.txt"   /* the .dol, for a start without arguments (update.c) */
#define PHOTO_ROOT "sd:/DCIM"
#define UPLOAD_DIR "sd:/DCIM/100FLASH"   /* photos sent from a phone, and saved creations */

void storage_set_program(const char *argv0);   /* before storage_init: argv[0] from the loader */
bool storage_init(void);
const char *storage_program_file(void);         /* the .dol we were started from, or NULL if unknown */
bool storage_ok(void);
const char *storage_device_name(void);
const char *storage_dir(void);
void storage_path(char *out, size_t cap, const char *name);   /* the data folder "/" name */

char *storage_read(const char *path, size_t *len_out);   /* malloc'd, NUL-terminated; up to 8 MB */
bool storage_write_atomic(const char *path, const void *data, size_t len);
bool storage_is_dir(const char *path);
bool storage_is_file(const char *path);
void storage_mkdirs(const char *path);   /* creates every missing folder along the path */
