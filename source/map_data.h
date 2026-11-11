/* map_data.h - the world for the photo map (generated into map_data.c by
 * tools/gen_map.py from Natural Earth, which is public domain).
 *
 * The land is a bitmap of quarter-degree cells, 84 N down to 60 S, one bit
 * per cell (set = land), rows of 1440 cells from 180 W eastwards. */
#pragma once
#include <stdint.h>

#define MAP_RES 4                         /* cells per degree */
#define MAP_COLS (360 * MAP_RES)
#define MAP_ROWS 576
#define MAP_LAT_TOP 84.0f
#define MAP_LAT_BOTTOM (MAP_LAT_TOP - MAP_ROWS / (float)MAP_RES)

extern const uint8_t map_land_bits[MAP_COLS / 8 * MAP_ROWS];

/* Cells (row, column) where two separate landmasses touch, such as
 * Greenland and Ellesmere Island: drawn as water so they stay apart. */
extern const uint16_t map_moat[][2];
extern const int map_nmoat;

typedef struct {
    int16_t lat, lon;       /* hundredths of a degree */
    uint16_t pop_k;         /* population in thousands */
    uint8_t country;        /* index into map_countries */
    const char *name;       /* ASCII */
} MapCity;

#define MAP_NCITIES 1251
extern const MapCity map_cities[MAP_NCITIES];   /* sorted by latitude */
extern const char *const map_countries[];

/* Whether there's land at a place. */
static inline int map_is_land(float lat, float lon) {
    int row = (int)((MAP_LAT_TOP - lat) * MAP_RES);
    if (row < 0 || row >= MAP_ROWS) return 0;
    while (lon < -180) lon += 360;
    while (lon >= 180) lon -= 360;
    int col = (int)((lon + 180) * MAP_RES);
    if (col < 0) col = 0;
    if (col >= MAP_COLS) col = MAP_COLS - 1;
    return map_land_bits[row * (MAP_COLS / 8) + (col >> 3)] & (0x80 >> (col & 7));
}

/* The same, between cells: land if most of the four cells around the
 * place are (so coastlines run smoothly when zoomed in close). */
static inline int map_is_land_smooth(float lat, float lon) {
    float fy = (MAP_LAT_TOP - lat) * MAP_RES - 0.5f, fx = (lon + 180) * MAP_RES - 0.5f;
    int r0 = (int)(fy + 1) - 1, c0 = (int)(fx + 1) - 1;
    float ty = fy - r0, tx = fx - c0;
    float v = 0;
    for (int k = 0; k < 4; k++) {
        int r = r0 + (k >> 1), c = c0 + (k & 1);
        if (r < 0 || r >= MAP_ROWS) continue;
        c = (c + MAP_COLS) % MAP_COLS;
        if (map_land_bits[r * (MAP_COLS / 8) + (c >> 3)] & (0x80 >> (c & 7)))
            v += ((k >> 1) ? ty : 1 - ty) * ((k & 1) ? tx : 1 - tx);
    }
    return v >= 0.5f;
}
