/* picture.h - reading photos: EXIF details, thumbnails and full pictures.
 *
 * Big phone and camera photos (12 megapixels and up) don't fit in the
 * GameCube's 24 MB if decoded whole, so JPEGs are decoded with TJpgDec,
 * which reads the file in small pieces and can shrink by 2, 4 or 8 while
 * decoding. Each decoded block is written straight into the GPU's texture
 * layout (turned the right way up as it goes), so a picture never needs
 * more memory than its final texture. Other formats (PNG, BMP, GIF, and
 * progressive JPEGs) go through stb_image, within a size limit. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "gfx.h"

typedef struct {
    int  width, height;      /* as stored in the file */
    int  orientation;        /* EXIF 1..8 (1 = as stored) */
    char date[20];           /* "2026:03:14 10:22:05" or "" */
    char camera[40];         /* "Canon PowerShot A80" or "" */
    uint32_t thumb_off, thumb_len;   /* embedded EXIF thumbnail (offset in the file), or 0 */
    bool progressive;
    bool is_jpeg;
    bool orientation_set;    /* (EXIF block seen) */
} PicInfo;

/* Reads the start of the file: dimensions, EXIF orientation/date/camera. */
bool pic_info(const char *path, PicInfo *info);
/* Width/height after turning by the EXIF orientation. */
void pic_display_size(const PicInfo *info, int *w, int *h);

/* Thumbnails: 160x120 RGB565 (tiled), the middle of the photo filling the
 * whole 4:3 frame. */
#define THUMB_W 160
#define THUMB_H 120
#define THUMB_BYTES (THUMB_W * THUMB_H * 2)
bool pic_make_thumb(const char *path, const PicInfo *info, uint16_t *out);

/* A whole picture as up to four textures of at most 1024x1024. */
#define PIC_TILE 1024
typedef struct {
    int w, h;              /* displayed size (orientation applied) */
    int cols, rows;
    GfxImage tile[4];
    PicInfo info;
} Picture;

/* Loads a picture no bigger than max_side on its longer side (<= 2048).
 * Returns false (with a short reason) if it can't be read or doesn't fit. */
bool pic_load(const char *path, int max_side, Picture *pic, char *why, size_t whylen);
void pic_free(Picture *pic);
bool pic_ok(const Picture *pic);
/* Draws the part (u0,v0)-(u1,v1) of the picture (in its pixels) into the
 * rectangle x,y,w,h. */
void pic_draw(Picture *pic, float x, float y, float w, float h,
              float u0, float v0, float u1, float v1, Color tint);
/* Draws the whole picture into the rectangle, rotated deg degrees about its middle. */
void pic_draw_rotated(Picture *pic, float cx, float cy, float w, float h, float deg, Color tint);
/* The colour of one pixel (for saving and effects). */
void pic_get_rgb(const Picture *pic, int x, int y, uint8_t *rgb);

/* Saves rgb (w x h, 3 bytes a pixel) as a JPEG. */
bool pic_save_jpeg(const char *path, const uint8_t *rgb, int w, int h, int quality);
