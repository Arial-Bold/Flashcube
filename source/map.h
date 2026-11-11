/* map.h - the album's Map tab: photos on a map of the world, by where they
 * were taken (see geo.h).
 *
 * Places close together share a pin showing how many photos are there;
 * zooming in (C-stick, or A on a pin) splits them apart, and from close up
 * the pins show a photo. A on a single place shows its photos, with a small
 * map and when they were taken. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

enum { MAP_STAY, MAP_EXIT };

void map_tab_enter(void);       /* the tab is shown (or the album comes back to it) */
void map_tab_leave(void);       /* another tab was picked */
int  map_update(void);          /* MAP_EXIT: B on the whole world, leave the album */
bool map_in_place(void);        /* looking at one place's photos */
const char *map_place_title(void);
void map_title(char *right, size_t cap);
/* Everything under the tabs: the map (or the place), the description bar
 * and the button hints. */
void map_draw(float x, float panel_alpha, float desc_alpha, float hint_alpha);
