/* games.h - helpers shared by the Playtime games. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "picture.h"

/* A free name for a new photo in the uploads folder: DOOD0001.JPG ... */
bool game_new_photo_path(const char *prefix, char *out, size_t cap);
/* Saves a picture (with an optional per-pixel change) as a new JPEG photo
 * and adds it to the library. Returns its index, or -1. */
typedef void (*PixelFn)(void *ctx, int x, int y, uint8_t *rgb);
int game_save_picture(const Picture *pic, const char *prefix, PixelFn fn, void *ctx, char *msg, size_t msglen);
/* The picture as RGB (3 bytes a pixel, malloc'd), to change and then save. */
uint8_t *game_picture_rgb(const Picture *pic);
/* Saves rgb as a new photo (frees nothing). Returns its library index or -1. */
int game_save_rgb(const uint8_t *rgb, int w, int h, const char *prefix, char *msg, size_t msglen);
