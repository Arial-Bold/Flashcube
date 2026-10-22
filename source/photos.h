/* photos.h - the photo library: every picture in sd:/DCIM, their folders,
 * favourites, and thumbnails.
 *
 * Thumbnails come from a background thread and are cached at three levels,
 * fastest first: main memory (the last 72), audio memory (ARAM, about 160
 * more), and small files in sd:/flashcube/thumbs, so a photo is only ever
 * decoded once. Details read from each photo (date, size, orientation) are
 * kept in sd:/flashcube/index.dat so the library opens quickly next time. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "gfx.h"
#include "picture.h"

#define MAX_PHOTOS 4000
#define MAX_FOLDERS 64

typedef struct {
    char *path;            /* "sd:/DCIM/100CANON/IMG_0001.JPG" */
    const char *name;      /* points into path: "IMG_0001.JPG" */
    int folder;            /* index into the folder list */
    uint32_t size;
    time_t mtime;
    int64_t when;          /* sort key: EXIF date if known, else file time */
    uint16_t w, h;         /* upright size, 0 if not known yet */
    char date[20];         /* EXIF date "2026:03:14 10:22:05" or "" */
    bool fav;
    bool info_known;
    uint32_t key;          /* identifies this version of the file */
} Photo;

typedef struct {
    char name[40];         /* "100CANON" or "DCIM" for files directly in DCIM */
    int count;
} Folder;

void photos_init(void);         /* starts the thumbnail thread */
/* (Re)reads the DCIM folder. */
void photos_scan(void);
bool photos_scanning(void);
int  photos_count(void);
Photo *photos_get(int i);
int  photos_folder_count(void);
const Folder *photos_folder(int i);
const char *photos_status(void);   /* "" or a problem ("No SD card", "No DCIM folder") */

/* Views of the library (lists of photo indexes). */
typedef enum { VIEW_ALL, VIEW_FOLDER, VIEW_FAVORITES, VIEW_YEAR } ViewKind;
int  photos_view(ViewKind kind, int arg, int *out, int cap);
/* Years that have photos, newest first. */
int  photos_years(int *out, int cap);
int  photo_year(const Photo *p);

void photos_set_fav(int i, bool on);
/* Adds a newly saved/received file to the library. Returns its index or -1. */
int  photos_add_file(const char *path);
/* Called (on the main thread) after a photo is added at index: lists of
 * photo indexes must move up the ones from index on. */
extern void (*photos_on_insert)(int index);
/* Removes a photo from the library (after deleting its file). */
void photos_forget(int i);
/* Sorts again (after the order setting changes). */
void photos_resort(void);

/* Thumbnails: returns the texture if ready, else asks the thread for it
 * and returns NULL. Call every frame for what's on screen. */
GfxImage *photos_thumb(int i);
/* Asks for thumbnails ahead of time (the next page), at lower priority. */
void photos_prefetch(int i);
bool photos_thumb_failed(int i);
/* Fills in date/size details if they're known only after the thumbnail. */
void photos_tick(void);         /* once per frame on the main thread */
/* Thumbnail work waiting (for a "preparing" indicator). */
int  photos_pending(void);
void photos_clear_cache(void);

/* Full pictures, also loaded by the thread. */
typedef struct {
    volatile int state;    /* 0 idle, 1 loading, 2 ready, 3 failed */
    int photo;
    int max_side;
    Picture pic;
    char why[80];
} PicRequest;
/* Starts loading photo i into req (any previous picture is freed first). */
void photos_load_picture(PicRequest *req, int photo, int max_side);
void photos_release_picture(PicRequest *req);

/* Short date for a photo: "14 Mar 2026". */
void photos_format_date(const Photo *p, char *out, size_t cap);

/* For the web server (any thread). */
bool  photos_path_for_key(uint32_t key, char *out, size_t cap);
/* A photo's lasting identity: its place in the library changes as photos
 * are added or removed, its key doesn't. */
uint32_t photos_key_of(int i);           /* 0 if there's no such photo */
int   photos_find_key(uint32_t key);     /* its place in the library now, or -1 */
char *photos_json(size_t *len);    /* malloc'd list of every photo */
