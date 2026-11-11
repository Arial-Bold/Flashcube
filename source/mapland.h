/* mapland.h - the photo map's land, drawn as glowing coastlines.
 *
 * A view of the map is drawn into a picture pixel by pixel, at the screen's
 * own pixels (lines tied to the map and not to the screen beat against the
 * TV's pixels and leave ghost patterns). Before drawing, the land is
 * simplified for its size on screen: gaps and inlets a few pixels wide are
 * filled in, and islands and lakes too small to draw cleanly are left out,
 * but separate landmasses (Greenland and Canada, Spain and Morocco) always
 * keep water between them.
 *
 * Drawing a view takes a moment, so it's done by a thread; while the map
 * moves, the last picture is moved and stretched along with it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

typedef struct {
    int w, h;                    /* size in pixels */
    float lat, lon, s;           /* what's in it: the middle, and pixels per degree */
    GfxImage img;
    bool ready;
} LandPic;

void mapland_init(void);
/* Asks for a view w x h pixels around lat/lon at s pixels per degree (the
 * main map: one at a time, the newest wins). */
void mapland_request(int w, int h, float lat, float lon, float s);
bool mapland_busy(void);
/* The newest finished picture of the main map, or NULL. */
const LandPic *mapland_current(void);
/* Draws a small view straight away (the place view's little map). */
bool mapland_draw_now(LandPic *pic, int w, int h, float lat, float lon, float s);
void mapland_free(LandPic *pic);

/* Draws a picture where it falls for a view of the screen box (x, y, w, h)
 * centred on lat/lon at s pixels per degree. */
void mapland_show(const LandPic *pic, float x, float y, float w, float h, float lat, float lon, float s, float alpha);
