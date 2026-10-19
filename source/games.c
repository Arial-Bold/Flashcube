/* games.c - see games.h. */
#include "games.h"
#include "photos.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool game_new_photo_path(const char *prefix, char *out, size_t cap) {
    storage_mkdirs(UPLOAD_DIR);
    for (int i = 1; i < 10000; i++) {
        snprintf(out, cap, "%s/%s%04d.JPG", UPLOAD_DIR, prefix, i);
        if (!storage_is_file(out)) return true;
    }
    return false;
}

int game_save_picture(const Picture *pic, const char *prefix, PixelFn fn, void *ctx, char *msg, size_t msglen) {
    if (!pic_ok(pic)) { snprintf(msg, msglen, "Nothing to save."); return -1; }
    char path[300];
    if (!game_new_photo_path(prefix, path, sizeof(path))) { snprintf(msg, msglen, "Too many saved photos."); return -1; }
    size_t bytes = (size_t)pic->w * pic->h * 3;
    uint8_t *rgb = malloc(bytes);
    if (!rgb) { snprintf(msg, msglen, "Not enough memory to save."); return -1; }
    for (int y = 0; y < pic->h; y++)
        for (int x = 0; x < pic->w; x++) {
            uint8_t *p = rgb + ((size_t)y * pic->w + x) * 3;
            pic_get_rgb(pic, x, y, p);
            if (fn) fn(ctx, x, y, p);
        }
    bool ok = pic_save_jpeg(path, rgb, pic->w, pic->h, 90);
    free(rgb);
    if (!ok) { snprintf(msg, msglen, "Couldn't write to the SD card."); return -1; }
    const char *name = strrchr(path, '/');
    snprintf(msg, msglen, "Saved as %s", name ? name + 1 : path);
    return photos_add_file(path);
}

uint8_t *game_picture_rgb(const Picture *pic) {
    if (!pic_ok(pic)) return NULL;
    uint8_t *rgb = malloc((size_t)pic->w * pic->h * 3);
    if (!rgb) return NULL;
    for (int y = 0; y < pic->h; y++)
        for (int x = 0; x < pic->w; x++) pic_get_rgb(pic, x, y, rgb + ((size_t)y * pic->w + x) * 3);
    return rgb;
}

int game_save_rgb(const uint8_t *rgb, int w, int h, const char *prefix, char *msg, size_t msglen) {
    char path[300];
    if (!game_new_photo_path(prefix, path, sizeof(path))) { snprintf(msg, msglen, "Too many saved photos."); return -1; }
    if (!pic_save_jpeg(path, rgb, w, h, 90)) { snprintf(msg, msglen, "Couldn't write to the SD card."); return -1; }
    const char *name = strrchr(path, '/');
    snprintf(msg, msglen, "Saved as %s in DCIM/100FLASH", name ? name + 1 : path);
    return photos_add_file(path);
}
