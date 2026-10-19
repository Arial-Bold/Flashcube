/* aramcache.h - the GameCube's 16 MB of audio memory (ARAM) as a second
 * level of the thumbnail cache. The music player (CubeBrowser's) keeps its
 * instrument samples below ARAM_MUSIC_TOP; above it are fixed-size slots for
 * thumbnails, so flicking back through an album copies them from ARAM in
 * well under a millisecond instead of reading the SD card again. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ARAM_MUSIC_TOP 0xA00000u     /* 10 MB for music samples */

/* Call after sound_init() (which starts ARAM). slot_bytes: a multiple of 32. */
void aramcache_init(uint32_t slot_bytes);
int  aramcache_slots(void);
/* Stores data (32-byte aligned, slot_bytes long) under key; replaces the
 * least recently used entry when full. */
bool aramcache_put(uint32_t key, const void *data);
/* Copies the entry for key into dst (32-byte aligned). */
bool aramcache_get(uint32_t key, void *dst);
void aramcache_clear(void);
