/* map.c - see map.h. */
#include "map.h"
#include "map_data.h"
#include "mapland.h"
#include "geo.h"
#include "app.h"
#include "photos.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Color GOLD = { 0xe8, 0xcf, 0x4a, 255 };
static const Color GOLD_EDGE = { 0x6a, 0x5a, 0x10, 255 };
static const Color TEXT = { 0xee, 0xf1, 0xf6, 255 };
static const Color LABEL = { 0x73, 0x92, 0xff, 255 };
static const Color INK = { 0x1a, 0x16, 0x08, 255 };

#define ZMAX 20.0f               /* closest zoom: 20 times the whole world */
#define PIN_ZOOM 2.6f            /* from here on, places show a photo */

/* ------------------------------------------------------------ places */

/* Photos taken close together (within a few km) make one place. */
#define MAX_PLACES 1024
#define PLACE_DEG 0.04f
typedef struct {
    float lat, lon;
    int count;
    int first;                  /* its newest photo (an index into the library) */
    int64_t t0, t1;             /* when its photos were taken, oldest and newest */
    int cluster;                /* (this frame) the pin it's part of */
} Place;
static Place places[MAX_PLACES];
static int nplaces, ngeo;
static int built_count = -1;
static unsigned built_gen = ~0u;
static float rebuild_wait;

/* Which place each photo with a place belongs to. */
typedef struct { int photo, place; } GeoPhoto;
static GeoPhoto *geo_photos;
static int geo_cap;

static void build_places(void) {
    int n = photos_count();
    built_count = n;
    built_gen = geo_generation();
    nplaces = ngeo = 0;
    if (geo_cap < n) {
        GeoPhoto *g = realloc(geo_photos, sizeof(GeoPhoto) * (size_t)(n + 64));
        if (!g) return;
        geo_photos = g;
        geo_cap = n + 64;
    }
    for (int i = 0; i < n; i++) {
        Photo *p = photos_get(i);
        float lat, lon;
        if (!p || !geo_get(p->key, &lat, &lon)) continue;
        int found = -1;
        for (int k = nplaces - 1; k >= 0; k--)
            if (fabsf(places[k].lat - lat) < PLACE_DEG && fabsf(places[k].lon - lon) < PLACE_DEG / cosf(lat * (float)M_PI / 180)) {
                found = k;
                break;
            }
        if (found < 0) {
            if (nplaces >= MAX_PLACES) continue;
            found = nplaces++;
            Place *pl = &places[found];
            memset(pl, 0, sizeof(*pl));
            pl->lat = lat;
            pl->lon = lon;
            pl->first = i;
            pl->t0 = pl->t1 = p->when;
        }
        Place *pl = &places[found];
        /* (the middle of its photos) */
        pl->lat += (lat - pl->lat) / (pl->count + 1);
        pl->lon += (lon - pl->lon) / (pl->count + 1);
        pl->count++;
        if (p->when < pl->t0) pl->t0 = p->when;
        if (p->when > pl->t1) { pl->t1 = p->when; pl->first = i; }
        geo_photos[ngeo].photo = i;
        geo_photos[ngeo].place = found;
        ngeo++;
    }
}

/* ------------------------------------------------------------ the view */

/* The map's window on screen. */
static float WX, WY, WW, WH;
static void set_window(void) { WX = UI_X + 41; WY = 94; WW = UI_W - 82; WH = 270; }

static float zoom = 1, zoom_to = 1;          /* 1 = the whole world across the window */
static float clat = 12, clon = 0;            /* the middle of the window */
static float tlat = 12, tlon = 0;            /* where it's heading */

/* At zoom 1 the whole world fits (in 16:9 it's the height that fills). */
static float px_per_deg(float z) { return fminf(WW / 360.0f, WH / (MAP_LAT_TOP - MAP_LAT_BOTTOM)) * z; }

static void clamp_center(float z, float *lat, float *lon) {
    float s = px_per_deg(z);
    float half_w = WW / 2 / s, half_h = WH / 2 / s;
    if (half_w >= 180) *lon = 0;
    else { if (*lon < -180 + half_w) *lon = -180 + half_w; if (*lon > 180 - half_w) *lon = 180 - half_w; }
    float top = MAP_LAT_TOP, bottom = MAP_LAT_BOTTOM;
    if (half_h * 2 >= top - bottom) *lat = (top + bottom) / 2;
    else { if (*lat > top - half_h) *lat = top - half_h; if (*lat < bottom + half_h) *lat = bottom + half_h; }
}

static void to_screen(float lat, float lon, float *x, float *y) {
    float s = px_per_deg(zoom);
    *x = WX + WW / 2 + (lon - clon) * s;
    *y = WY + WH / 2 - (lat - clat) * s;
}

/* ------------------------------------------------------------ pins */

/* Places close together on screen share one pin. */
#define MAX_PINS 256
typedef struct {
    float lat, lon;             /* the middle of its photos */
    float x, y;                 /* on screen */
    int count, nplaces, seed;   /* seed: its biggest place */
    int64_t t0, t1;
} Pin;
static Pin pins[MAX_PINS];
static int npins;
static int order[MAX_PLACES];   /* places, biggest first */
static int anchor = -1;         /* the selected pin is the one with this place */

static int by_count(const void *a, const void *b) {
    const Place *x = &places[*(const int *)a], *y = &places[*(const int *)b];
    return y->count - x->count;
}

static void make_pins(void) {
    float r = zoom >= PIN_ZOOM ? 50 : 40;
    for (int i = 0; i < nplaces; i++) order[i] = i;
    qsort(order, (size_t)nplaces, sizeof(int), by_count);
    npins = 0;
    for (int k = 0; k < nplaces; k++) {
        Place *pl = &places[order[k]];
        float x, y;
        to_screen(pl->lat, pl->lon, &x, &y);
        int to = -1;
        for (int j = 0; j < npins; j++) {
            Place *s = &places[pins[j].seed];
            float sx, sy;
            to_screen(s->lat, s->lon, &sx, &sy);
            if (fabsf(sx - x) < r && fabsf(sy - y) < r && (sx - x) * (sx - x) + (sy - y) * (sy - y) < r * r) { to = j; break; }
        }
        if (to < 0) {
            if (npins >= MAX_PINS) { pl->cluster = -1; continue; }
            to = npins++;
            Pin *p = &pins[to];
            memset(p, 0, sizeof(*p));
            p->seed = order[k];
            p->t0 = pl->t0;
            p->t1 = pl->t1;
        }
        Pin *p = &pins[to];
        float w = (float)pl->count / (p->count + pl->count);
        p->lat += (pl->lat - p->lat) * w;
        p->lon += (pl->lon - p->lon) * w;
        p->count += pl->count;
        p->nplaces++;
        if (pl->t0 < p->t0) p->t0 = pl->t0;
        if (pl->t1 > p->t1) p->t1 = pl->t1;
        pl->cluster = to;
    }
    for (int j = 0; j < npins; j++) to_screen(pins[j].lat, pins[j].lon, &pins[j].x, &pins[j].y);
}

static int selected_pin(void) {
    if (anchor < 0 || anchor >= nplaces) return -1;
    return places[anchor].cluster;
}

/* Puts the photos of a pin into g_view (in library order). */
static void pin_photos(int pin) {
    g_view_count = 0;
    for (int i = 0; i < ngeo && g_view_count < VIEW_MAX; i++)
        if (places[geo_photos[i].place].cluster == pin) g_view[g_view_count++] = geo_photos[i].photo;
    g_view_pos = 0;
}

/* ------------------------------------------------------------ naming */

static void civil(int64_t when, int *y, int *m, int *d) {
    int64_t days = when / 86400 + 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int doe = (int)(days - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)(yoe + era * 400);
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yy + (*m <= 2);
}

static const char *const MONTHS[] = { "January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December" };

/* "June 2025", "12 to 19 June 2025", "2025 to 2026"... */
static void span_text(int64_t t0, int64_t t1, bool days, char *out, size_t cap) {
    if (t0 <= 0) { snprintf(out, cap, "No date"); return; }
    int y0, m0, d0, y1, m1, d1;
    civil(t0, &y0, &m0, &d0);
    civil(t1, &y1, &m1, &d1);
    if (y0 != y1) snprintf(out, cap, "%d to %d", y0, y1);
    else if (m0 != m1) snprintf(out, cap, "%.3s to %.3s %d", MONTHS[m0 - 1], MONTHS[m1 - 1], y0);
    else if (!days) snprintf(out, cap, "%s %d", MONTHS[m0 - 1], y0);
    else if (d0 != d1) snprintf(out, cap, "%d to %d %s %d", d0, d1, MONTHS[m0 - 1], y0);
    else snprintf(out, cap, "%d %s %d", d0, MONTHS[m0 - 1], y0);
}

/* What to call a pin: one place's name, or the countries it covers. */
static void pin_name(int pin, char *out, size_t cap) {
    Pin *p = &pins[pin];
    if (p->nplaces == 1) { geo_place_name(places[p->seed].lat, places[p->seed].lon, out, cap); return; }
    const char *seen[4];
    int nseen = 0, more = 0;
    for (int k = 0; k < nplaces; k++) {
        Place *pl = &places[order[k]];
        if (pl->cluster != pin) continue;
        const char *c = geo_country(pl->lat, pl->lon);
        if (!c[0]) continue;
        bool dup = false;
        for (int j = 0; j < nseen; j++) if (!strcmp(seen[j], c)) dup = true;
        if (dup) continue;
        if (nseen < 3) seen[nseen++] = c; else more++;
    }
    if (nseen == 0) snprintf(out, cap, "%d places", p->nplaces);
    else if (nseen == 1) snprintf(out, cap, "%s \x7f %d places", seen[0], p->nplaces);
    else {
        size_t n = 0;
        for (int j = 0; j < nseen && n < cap; j++) n += (size_t)snprintf(out + n, cap - n, "%s%s", j ? ", " : "", seen[j]);
        if (more && n < cap) snprintf(out + n, cap - n, " +%d", more);
    }
}

/* ------------------------------------------------------------ one place's photos */

static bool in_place;
static LandPic mini;     /* the place view's little map */
static char place_title[48];
static float place_lat, place_lon;
static uint32_t *place_keys;     /* its photos, by key (the library can change under it) */
static int nplace_keys;
static float pscroll, pscroll_to;
static float psel_flash;
#define PTW 118.0f
#define PTH 76.0f
#define PGAP 10.0f
#define PGX 216.0f
#define PGY 58.0f
#define PROWS 3
static int pcols(void) { return (int)((UI_W - 41 - PGX + PGAP) / (PTW + PGAP)); }

static void place_view(void) {
    int keep = (g_view_pos >= 0 && g_view_pos < g_view_count) ? g_view[g_view_pos] : -1;
    g_view_count = 0;
    for (int k = 0; k < nplace_keys && g_view_count < VIEW_MAX; k++) {
        int i = photos_find_key(place_keys[k]);
        if (i >= 0) g_view[g_view_count++] = i;
    }
    snprintf(g_view_name, sizeof(g_view_name), "%s", place_title);
    g_view_pos = 0;
    for (int i = 0; i < g_view_count; i++) if (g_view[i] == keep) g_view_pos = i;
}

static void place_keep_visible(void) {
    int row = g_view_pos / pcols();
    if (row < pscroll_to) pscroll_to = (float)row;
    if (row > pscroll_to + PROWS - 1) pscroll_to = (float)(row - PROWS + 1);
}

static void open_place(int pin) {
    pin_photos(pin);
    if (g_view_count == 0) return;
    free(place_keys);
    place_keys = malloc(sizeof(uint32_t) * (size_t)g_view_count);
    if (!place_keys) return;
    nplace_keys = g_view_count;
    for (int i = 0; i < g_view_count; i++) place_keys[i] = photos_key_of(g_view[i]);
    pin_name(pin, place_title, sizeof(place_title));
    place_lat = pins[pin].lat;
    place_lon = pins[pin].lon;
    in_place = true;
    place_view();
    g_view_pos = 0;
    pscroll = pscroll_to = 0;
    psel_flash = 0;
}

/* ------------------------------------------------------------ the tab */

static float sel_flash;

void map_tab_enter(void) {
    set_window();
    if (built_count != photos_count() || built_gen != geo_generation()) { build_places(); anchor = -1; }
    if (in_place) {
        place_view();
        if (g_view_count == 0) in_place = false;
        else place_keep_visible();
    }
    if (!in_place) {
        g_view_count = 0;
        snprintf(g_view_name, sizeof(g_view_name), "Map");
    }
    clamp_center(zoom, &clat, &clon);
    make_pins();
}

void map_tab_leave(void) { in_place = false; mapland_free(&mini); }
bool map_in_place(void) { return in_place; }

/* The pin to go to from pin `from` in direction d (nearest that way). */
static int pin_toward(int from, int d) {
    float dx = d == DIR_LEFT ? -1 : d == DIR_RIGHT ? 1 : 0;
    float dy = d == DIR_UP ? -1 : d == DIR_DOWN ? 1 : 0;
    int best = -1;
    float best_score = 1e9f;
    for (int j = 0; j < npins; j++) {
        if (j == from) continue;
        float ox = pins[j].x - pins[from].x, oy = pins[j].y - pins[from].y;
        float along = ox * dx + oy * dy, side = fabsf(ox * dy - oy * dx);
        if (along <= 2 || side > along * 2.2f) continue;
        float score = along + side * 2;
        if (score < best_score) { best_score = score; best = j; }
    }
    return best;
}

/* Zooms so a pin's places spread out to fill the window. */
static void zoom_into(int pin) {
    float la0 = 90, la1 = -90, lo0 = 180, lo1 = -180;
    for (int k = 0; k < nplaces; k++) {
        Place *pl = &places[k];
        if (pl->cluster != pin) continue;
        if (pl->lat < la0) la0 = pl->lat;
        if (pl->lat > la1) la1 = pl->lat;
        if (pl->lon < lo0) lo0 = pl->lon;
        if (pl->lon > lo1) lo1 = pl->lon;
    }
    float span_lon = fmaxf(lo1 - lo0, 0.3f), span_lat = fmaxf(la1 - la0, 0.3f);
    float z = fminf(WW * 0.6f / (span_lon * px_per_deg(1)), WH * 0.6f / (span_lat * px_per_deg(1)));
    if (z < zoom * 1.6f) z = zoom * 1.6f;
    if (z > ZMAX) z = ZMAX;
    zoom_to = z;
    /* (a little low: photo pins stand up above their places) */
    tlat = (la0 + la1) / 2 + 18 / px_per_deg(z);
    tlon = (lo0 + lo1) / 2;
}

int map_update(void) {
    set_window();
    sel_flash += g_dt;
    psel_flash += g_dt;
    /* new places found: count them in (not every frame while reading) */
    rebuild_wait += g_dt;
    if ((built_count != photos_count() || built_gen != geo_generation()) && rebuild_wait > 0.5f && !in_place) {
        rebuild_wait = 0;
        float alat = 0, alon = 0;
        bool had = anchor >= 0 && anchor < nplaces;
        if (had) { alat = places[anchor].lat; alon = places[anchor].lon; }
        build_places();
        /* the same place again (its number may have changed) */
        anchor = -1;
        float best = 1e9f;
        for (int k = 0; had && k < nplaces; k++) {
            float dd = fabsf(places[k].lat - alat) + fabsf(places[k].lon - alon);
            if (dd < best) { best = dd; anchor = k; }
        }
    }

    if (in_place) {
        int n = g_view_count, cols = pcols(), s = g_view_pos, d = input_dir();
        if (n > 0 && d != DIR_NONE) {
            int ns = s;
            if (d == DIR_LEFT && s % cols > 0) ns--;
            else if (d == DIR_RIGHT && s % cols < cols - 1 && s + 1 < n) ns++;
            else if (d == DIR_UP && s >= cols) ns -= cols;
            else if (d == DIR_DOWN) ns = s + cols < n ? s + cols : (s / cols < (n - 1) / cols ? n - 1 : s);
            if (ns != s) { g_view_pos = ns; sound_play(SFX_HOVER); psel_flash = 0; place_keep_visible(); }
        }
        if (input_pressed(BTN_A) && n > 0) { sound_play(SFX_SELECT); app_go(SCR_VIEWER, 0); }
        else if (input_pressed(BTN_Y) && n > 0) { sound_play(SFX_SELECT); app_go(SCR_SLIDESETUP, 1); }
        else if (input_pressed(BTN_B)) {
            sound_play(SFX_BACK);
            in_place = false;
            g_view_count = 0;
            snprintf(g_view_name, sizeof(g_view_name), "Map");
            sel_flash = 0;
        }
        pscroll += (pscroll_to - pscroll) * clamp01(g_dt * 14);
        return MAP_STAY;
    }

    /* the map: zoom with the C-stick, towards the selected pin */
    float cy = input_cstick_y();
    if (fabsf(cy) > 0.2f) {
        zoom_to *= expf(-cy * g_dt * 2.4f);
        if (zoom_to < 1) zoom_to = 1;
        if (zoom_to > ZMAX) zoom_to = ZMAX;
        if (anchor >= 0) { tlat = places[anchor].lat; tlon = places[anchor].lon; }
    }
    float k = clamp01(g_dt * 7);
    zoom = expf(logf(zoom) + (logf(zoom_to) - logf(zoom)) * k);
    float la = tlat, lo = tlon;
    clamp_center(zoom_to, &la, &lo);
    tlat = la; tlon = lo;
    clat += (tlat - clat) * k;
    clon += (tlon - clon) * k;
    clamp_center(zoom, &clat, &clon);
    make_pins();

    int sp = selected_pin();
    if (sp < 0 && npins > 0) { anchor = pins[0].seed; sp = 0; }   /* (the biggest) */
    if (sp >= 0) anchor = pins[sp].seed;                           /* follow the pin as places merge and split */

    int d = input_dir();
    if (sp >= 0 && d != DIR_NONE) {
        int to = pin_toward(sp, d);
        if (to >= 0) {
            anchor = pins[to].seed;
            sp = to;
            sound_play(SFX_HOVER);
            sel_flash = 0;
            /* keep it well inside the window */
            if (pins[to].x < WX + 60 || pins[to].x > WX + WW - 60 || pins[to].y < WY + 60 || pins[to].y > WY + WH - 20) {
                tlat = pins[to].lat;
                tlon = pins[to].lon;
            }
        }
    }
    if (input_pressed(BTN_A) && sp >= 0) {
        sound_play(SFX_SELECT);
        if (pins[sp].nplaces > 1 && zoom_to < ZMAX * 0.95f) zoom_into(sp);
        else open_place(sp);
    } else if (input_pressed(BTN_Y) && sp >= 0 && zoom_to > 1.05f) {
        sound_play(SFX_SELECT);
        pin_photos(sp);
        pin_name(sp, g_view_name, sizeof(g_view_name));
        if (g_view_count > 0) app_go(SCR_SLIDESETUP, 1);
    } else if (input_pressed(BTN_B)) {
        if (zoom_to > 1.05f) {
            sound_play(SFX_BACK);
            zoom_to = 1;
            tlat = 12;
            tlon = 0;
        } else {
            return MAP_EXIT;
        }
    }
    return MAP_STAY;
}

/* ------------------------------------------------------------ drawing */

static void well(float x, float y, float w, float h, float al) {
    gfx_rect(x - 2, y - 2, w + 4, h + 4, rgba(190, 200, 215, (u8)(110 * al)));
    gfx_rect(x, y, w, h, rgba(0x08, 0x0c, 0x10, (u8)(255 * al)));   /* (the land picture's sea, as the GPU shows it) */
}

/* The land: a picture of the view drawn by mapland.c, asked for again
 * when the view has moved on (at once when it settles, and now and then
 * while it keeps moving). Meanwhile the last one moves along with it. */
#define LAND_MARGIN 32
static float land_asked;
static float asked_lat = 999, asked_lon, asked_s;
static void keep_land_fresh(void) {
    float s = px_per_deg(zoom);
    const LandPic *cur = mapland_current();
    land_asked += g_dt;
    int w = (int)WW + LAND_MARGIN * 2, h = (int)WH + LAND_MARGIN * 2;
    bool fits = cur && cur->w == w && cur->h == h && fabsf(cur->s - s) < s * 0.004f &&
                fabsf(cur->lon - clon) * s < 1 && fabsf(cur->lat - clat) * s < 1;
    bool asked = fabsf(asked_s - s) < s * 0.004f && fabsf(asked_lon - clon) * s < 1 && fabsf(asked_lat - clat) * s < 1;
    if (fits || asked) return;
    /* a newer request replaces one being drawn, so ask as soon as the view
     * settles, and every so often while it keeps moving */
    bool settled = fabsf(zoom - zoom_to) < zoom * 0.005f && fabsf(clat - tlat) * s < 0.5f && fabsf(clon - tlon) * s < 0.5f;
    if (!cur || settled || land_asked > 0.3f) {
        mapland_request(w, h, clat, clon, s);
        asked_lat = clat; asked_lon = clon; asked_s = s;
        land_asked = 0;
    }
}

static void count_text(int n, char *out, size_t cap) {
    if (n > 999) snprintf(out, cap, "%dk", n / 1000);
    else snprintf(out, cap, "%d", n);
}

static float pin_radius(int count) {
    float r = 9 + log10f((float)count) * 3;
    return r > 15 ? 15 : r;
}

static void gold_pin(const Pin *p, bool on, float al) {
    float r = pin_radius(p->count);
    char c[8];
    count_text(p->count, c, sizeof(c));
    gfx_circle(p->x, p->y + 2, r + 1, rgba(0, 0, 0, (u8)(150 * al)));
    gfx_circle(p->x, p->y, r + 1.5f, color_alpha(GOLD_EDGE, al));
    gfx_circle(p->x, p->y, r, color_alpha(GOLD, al));
    ui_text_center(FONT_MENU_BOLD, r > 11 ? 13 : 11, 0, p->x, p->y - 12, 24, color_alpha(INK, al), c);
    if (on) {
        float pop = 1 + 0.12f * (1 - clamp01(sel_flash / 0.18f));
        gfx_ring(p->x, p->y, (r + 7) * pop, 2, 0, 2 * (float)M_PI, color_alpha(GOLD_EDGE, al));
        gfx_ring(p->x, p->y, (r + 5) * pop, 3, 0, 2 * (float)M_PI, color_alpha(GOLD, al));
    }
}

static void photo_pin(const Pin *p, bool on, float al) {
    float w = 50, h = 38;
    if (on) { float pop = 1 + 0.08f * (1 - clamp01(sel_flash / 0.18f)); w *= pop; h *= pop; }
    float x = p->x - w / 2, y = p->y - h - 9;
    gfx_tri(p->x - 6, p->y - 10, p->x + 6, p->y - 10, p->x, p->y, on ? color_alpha(GOLD, al) : rgba(190, 200, 215, (u8)(230 * al)));
    ui_thumb_frame(x, y, w, h, al);
    app_draw_thumb(places[p->seed].first, x + 1, y + 1, w - 2, h - 2, al);
    if (on) ui_gold_frame(x, y, w, h, al);
    char c[8];
    count_text(p->count, c, sizeof(c));
    float bx = x + w - 2, by = y + 2;
    gfx_circle(bx, by, 10, color_alpha(GOLD_EDGE, al));
    gfx_circle(bx, by, 8.5f, color_alpha(GOLD, al));
    ui_text_center(FONT_MENU_BOLD, 11, 0, bx, by - 12, 24, color_alpha(INK, al), c);
}

void map_title(char *right, size_t cap) {
    if (in_place) { snprintf(right, cap, "%d photo%s", g_view_count, g_view_count == 1 ? "" : "s"); return; }
    int n = photos_count();
    if (geo_reading() && n > 0) snprintf(right, cap, "Map \x7f reading places, %d of %d", geo_known(), n);
    else if (nplaces == 0) snprintf(right, cap, "Map");
    else snprintf(right, cap, "Map \x7f %d photo%s in %d place%s", ngeo, ngeo == 1 ? "" : "s", nplaces, nplaces == 1 ? "" : "s");
}

const char *map_place_title(void) { return place_title; }

static void draw_place(float X, float pa, float da, float ha) {
    /* the place on a small map */
    float mx = X + 41, my = 58, mw = 160, mh = 116;
    well(mx, my, mw, mh, pa);
    float s = mw / 50.0f;      /* 50 degrees across */
    float la = place_lat, lo = place_lon;
    float half_h = mh / 2 / s, half_w = mw / 2 / s;
    if (la > MAP_LAT_TOP - half_h) la = MAP_LAT_TOP - half_h;
    if (la < MAP_LAT_BOTTOM + half_h) la = MAP_LAT_BOTTOM + half_h;
    if (lo < -180 + half_w) lo = -180 + half_w;
    if (lo > 180 - half_w) lo = 180 - half_w;
    gfx_set_clip(mx, my, mw, mh);
    if (!mini.ready || mini.lat != la || mini.lon != lo) mapland_draw_now(&mini, (int)mw, (int)mh, la, lo, s);
    mapland_show(&mini, mx, my, mw, mh, la, lo, s, pa);
    float lx = mx + mw / 2 + (place_lon - lo) * s, ly = my + mh / 2 - (place_lat - la) * s;
    gfx_circle(lx, ly, 6, color_alpha(GOLD_EDGE, pa));
    gfx_circle(lx, ly, 4.5f, color_alpha(GOLD, pa));
    gfx_clear_clip();

    /* its details */
    int64_t t0 = 0, t1 = 0;
    int folder = -1;
    bool one_folder = true;
    for (int i = 0; i < g_view_count; i++) {
        Photo *p = photos_get(g_view[i]);
        if (!p) continue;
        if (p->when > 0 && (!t0 || p->when < t0)) t0 = p->when;
        if (p->when > t1) t1 = p->when;
        if (folder < 0) folder = p->folder;
        else if (p->folder != folder) one_folder = false;
    }
    char taken[48], from[48], where[48];
    span_text(t0, t1, true, taken, sizeof(taken));
    const Folder *f = folder >= 0 ? photos_folder(folder) : NULL;
    snprintf(from, sizeof(from), "%s", !f ? "" : one_folder ? f->name : "Several folders");
    geo_format_coords(place_lat, place_lon, where, sizeof(where));
    const char *lab[] = { "Taken", "From", "Place" };
    const char *val[] = { taken, from, where };
    for (int i = 0; i < 3; i++) {
        ui_text(FONT_MENU, 12, 1.2f, X + 41, 186 + i * 40, 18, color_alpha(LABEL, pa), lab[i]);
        ui_text(FONT_MENU, 13, 1.2f, X + 41, 204 + i * 40, 18, color_alpha(TEXT, pa), val[i]);
    }

    /* its photos */
    int cols = pcols(), n = g_view_count;
    float gx0 = X + PGX;
    gfx_set_clip(gx0 - 8, 52, UI_W - PGX - 26, 316);
    int first = (int)pscroll;
    for (int r = first; r <= first + PROWS; r++)
        for (int c = 0; c < cols; c++) {
            int i = r * cols + c;
            if (i >= n) break;
            float x = gx0 + c * (PTW + PGAP), y = PGY + (r - pscroll) * (PTH + PGAP);
            ui_thumb_frame(x, y, PTW, PTH, pa);
            app_draw_thumb(g_view[i], x + 3, y + 3, PTW - 6, PTH - 6, pa);
            Photo *p = photos_get(g_view[i]);
            if (p && p->fav) ui_text(FONT_MENU, 13, 0, x + PTW - 20, y + 5, 16, rgba(0xff, 0xd8, 0x4a, (u8)(255 * pa)), "\x84");
        }
    gfx_clear_clip();
    if (n > 0) {
        int s2 = g_view_pos;
        float x = gx0 + (s2 % cols) * (PTW + PGAP), y = PGY + (s2 / cols - pscroll) * (PTH + PGAP);
        float pop = 1 + 0.06f * (1 - clamp01(psel_flash / 0.18f));
        float pw = PTW * pop, ph = PTH * pop;
        ui_gold_frame(x - (pw - PTW) / 2, y - (ph - PTH) / 2, pw, ph, pa);
        int last = (first + PROWS) * cols;
        for (int i = last; i < last + cols * 2 && i < n; i++) photos_prefetch(g_view[i]);
    }

    ui_desc(X + 32, 374, UI_W - 64, 34, da);
    if (n > 0) {
        Photo *p = photos_get(g_view[g_view_pos]);
        if (p) {
            char date[24], info[64];
            photos_format_date(p, date, sizeof(date));
            snprintf(info, sizeof(info), "%s \x7f %d of %d", date, g_view_pos + 1, n);
            ui_desc_texts(X + 32, 374, UI_W - 64, 34, p->name, info, da);
        }
    }
    const char *h[] = { "S:Select", "Y:Slideshow", "B:Back", "A:Open" };
    ui_hints(h, 4, ha, input_stick_dir());
}

void map_draw(float X, float pa, float da, float ha) {
    set_window();
    if (in_place) { draw_place(X, pa, da, ha); return; }
    well(WX, WY, WW, WH, pa);
    gfx_set_clip(WX, WY, WW, WH);
    keep_land_fresh();
    /* the whole world underneath (soft when zoomed, but there at once), then the sharp view */
    float s1 = px_per_deg(1), wlat = 12, wlon = 0;
    clamp_center(1, &wlat, &wlon);
    const LandPic *cur = mapland_current();
    float s = px_per_deg(zoom);
    const LandPic *wp = mapland_world((int)WW, (int)WH, wlat, wlon, s1);
    if (s < s1 * 3) mapland_show(wp, WX, WY, WW, WH, clat, clon, s, pa);   /* (stretched further it's only smudges) */
    mapland_show(cur, WX, WY, WW, WH, clat, clon, s, pa);
    int sp = selected_pin();
    bool photos_on = zoom >= PIN_ZOOM;
    for (int j = 0; j < npins; j++) {
        if (j == sp) continue;
        if (pins[j].x < WX - 60 || pins[j].x > WX + WW + 60 || pins[j].y < WY - 20 || pins[j].y > WY + WH + 60) continue;
        if (photos_on) photo_pin(&pins[j], false, pa); else gold_pin(&pins[j], false, pa);
    }
    if (sp >= 0) { if (photos_on) photo_pin(&pins[sp], true, pa); else gold_pin(&pins[sp], true, pa); }
    gfx_clear_clip();

    if (npins == 0) {
        float cx = WX + WW / 2;
        gfx_rect(WX + 40, WY + WH / 2 - 44, WW - 80, 88, rgba(0x0b, 0x0f, 0x12, (u8)(200 * pa)));
        if (geo_reading() && photos_count() > 0) {
            ui_spinner(cx, WY + WH / 2 - 14, 12, g_time, pa);
            ui_text_center(FONT_MENU, 14, 1.2f, cx, WY + WH / 2 + 8, 24, rgba(0xdf, 0xe4, 0xff, (u8)(255 * pa)),
                           "Finding where your photos were taken...");
        } else {
            ui_text_center(FONT_MENU, 14, 1.2f, cx, WY + WH / 2 - 26, 24, rgba(0xdf, 0xe4, 0xff, (u8)(255 * pa)),
                           "None of your photos say where they were taken.");
            ui_text_center(FONT_MENU, 13, 1.2f, cx, WY + WH / 2 + 4, 24, rgba(0xc9, 0xd2, 0xee, (u8)(255 * pa)),
                           "Phones add the place when the camera may use location.");
        }
    }

    ui_desc(X + 32, 374, UI_W - 64, 34, da);
    if (sp >= 0) {
        char name[64], info[64], span[40];
        pin_name(sp, name, sizeof(name));
        span_text(pins[sp].t0, pins[sp].t1, false, span, sizeof(span));
        snprintf(info, sizeof(info), "%d photo%s \x7f %s", pins[sp].count, pins[sp].count == 1 ? "" : "s", span);
        ui_desc_texts(X + 32, 374, UI_W - 64, 34, name, info, da);
    }
    if (zoom_to > 1.05f) {
        const char *h[] = { "S:Select", "C:Zoom", "Y:Slideshow", "B:Back", "A:Open" };
        ui_hints(h, 5, ha, input_stick_dir());
    } else {
        const char *h[] = { "S:Select", "LR:Tab", "C:Zoom", "B:Back", "A:Open" };
        ui_hints(h, 5, ha, input_stick_dir());
    }
}
