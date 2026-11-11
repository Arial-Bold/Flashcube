/* geo.h - where photos were taken, for the photo map.
 *
 * Phones (and some cameras) write the place a photo was taken into its
 * EXIF details. Those are read in the background by the photo thread when
 * it has nothing else to do, a few photos at a time, and kept in
 * sd:/flashcube/places.dat ("key lat lon", or "key -" for no place) so each
 * photo is only ever read once. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "picture.h"

void geo_init(void);            /* loads places.dat */
void geo_tick(void);            /* once per frame on the main thread: hands out work, saves */

/* Whether a photo (by key) has a place; false if it has none or isn't read yet. */
bool geo_get(uint32_t key, float *lat, float *lon);
/* How many photos in the library have been read so far (of photos_count()). */
int  geo_known(void);
bool geo_reading(void);         /* still photos to read */
/* Bumped whenever a place is learned, so the map knows to look again. */
unsigned geo_generation(void);

/* (photo thread) Reads one waiting photo; false if there was nothing to do. */
bool geo_work(void);
/* (photo thread) Records what pic_info() found for a photo it read anyway. */
void geo_note(uint32_t key, const PicInfo *info);

/* A name for a place: "Lisbon, Portugal" (the nearest city within reach),
 * "Near Reykjavik, Iceland" further out, or "Far from any city". */
void geo_place_name(float lat, float lon, char *out, size_t cap);
/* Just the country of the nearest city ("" if none is near). */
const char *geo_country(float lat, float lon);
