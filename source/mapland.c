/* mapland.c - see mapland.h. */
#include "mapland.h"
#include "map_data.h"
#include "pixels.h"
#include "thr.h"
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ drawing a view */

typedef struct {
    int cap;                     /* pixels the buffers hold */
    uint8_t *mask, *tmp, *cnt, *edge;
    int *stack;
    int *seen;
    int stamp;
} Scratch;

static bool scratch_fit(Scratch *k, int n) {
    if (k->cap >= n) return true;
    free(k->mask); free(k->tmp); free(k->cnt); free(k->edge); free(k->stack); free(k->seen);
    k->mask = malloc((size_t)n); k->tmp = malloc((size_t)n); k->cnt = malloc((size_t)n); k->edge = malloc((size_t)n);
    k->stack = malloc(sizeof(int) * (size_t)n); k->seen = calloc((size_t)n, sizeof(int));
    k->stamp = 0;
    k->cap = (k->mask && k->tmp && k->cnt && k->edge && k->stack && k->seen) ? n : 0;
    return k->cap > 0;
}

/* Grows (or shrinks) the land by r pixels, round. */
static void morph(Scratch *k, int w, int h, int r, bool grow) {
    memcpy(k->tmp, k->mask, (size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int v = k->tmp[y * w + x];
            if (grow == (v != 0)) continue;
            for (int dy = -r; dy <= r; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                for (int dx = -r; dx <= r; dx++) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= w || dx * dx + dy * dy > r * r + r) continue;
                    if (k->tmp[yy * w + xx] == grow) { v = grow; goto done; }
                }
            }
        done:
            k->mask[y * w + x] = (uint8_t)v;
        }
}

/* Turns pieces of land (value 1) or water (0) smaller than min_px into the
 * other. Water touching the edge is the sea, never a lake. */
static void drop_small(Scratch *k, int w, int h, int value, int min_px) {
    k->stamp++;
    for (int start = 0; start < w * h; start++) {
        if (k->mask[start] != value || k->seen[start] == k->stamp) continue;
        int sp = 0, at = 0;
        bool edge = false;
        k->stack[sp++] = start;
        k->seen[start] = k->stamp;
        while (at < sp) {
            int i = k->stack[at++], x = i % w, y = i / w;
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) edge = true;
            int nb[4] = { x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1 };
            for (int j = 0; j < 4; j++) {
                int n = nb[j];
                if (n < 0 || k->mask[n] != value || k->seen[n] == k->stamp) continue;
                k->seen[n] = k->stamp;
                k->stack[sp++] = n;
            }
        }
        if (sp < min_px && !(edge && value == 0))
            for (int j = 0; j < sp; j++) k->mask[k->stack[j]] = (uint8_t)!value;
    }
}

/* Water where two landmasses meet, at least minpx wide. */
static void moat(Scratch *k, int w, int h, float lat_c, float lon_c, float s, float minpx) {
    float half = fmaxf(s / MAP_RES, minpx) / 2;
    for (int i = 0; i < map_nmoat; i++) {
        float lat = MAP_LAT_TOP - (map_moat[i][0] + 0.5f) / MAP_RES, lon = (map_moat[i][1] + 0.5f) / MAP_RES - 180;
        float cx = w / 2.0f + (lon - lon_c) * s, cy = h / 2.0f - (lat - lat_c) * s;
        if (cx < -half || cy < -half || cx > w + half || cy > h + half) continue;
        for (int y = (int)floorf(cy - half); y < (int)ceilf(cy + half); y++)
            for (int x = (int)floorf(cx - half); x < (int)ceilf(cx + half); x++)
                if (x >= 0 && y >= 0 && x < w && y < h) k->mask[y * w + x] = 0;
    }
}

typedef struct { float r, g, b; } RGB;
static inline RGB mix(RGB a, RGB b, float t) { RGB o = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; return o; }
static const RGB SEA = { 0x0b, 0x0f, 0x12 }, LAND = { 0x11, 0x1a, 0x2c }, GLOW = { 0x3a, 0x4c, 0xb0 }, COAST = { 0x9f, 0xb4, 0xff };

/* Draws a view into tex (tiled RGB565, tw wide). */
static bool render(Scratch *k, uint16_t *tex, int tw, int w, int h, float lat_c, float lon_c, float s) {
    if (!scratch_fit(k, w * h)) return false;
    uint8_t *m = k->mask;
    /* land or water at each pixel's middle */
    for (int y = 0; y < h; y++) {
        float lat = lat_c - (y + 0.5f - h / 2.0f) / s;
        for (int x = 0; x < w; x++) {
            float lon = lon_c + (x + 0.5f - w / 2.0f) / s;
            m[y * w + x] = (lon >= -180 && lon < 180 && map_is_land_smooth(lat, lon)) ? 1 : 0;
        }
    }
#ifndef STAGE
#define STAGE(n)
#endif
    STAGE("0_raw");
    /* simplify for the screen */
    moat(k, w, h, lat_c, lon_c, s, 3);
    STAGE("1_moat");
    morph(k, w, h, 2, true);
    morph(k, w, h, 2, false);            /* inlets and gaps under about 4 pixels close */
    STAGE("2_closed");
    moat(k, w, h, lat_c, lon_c, s, 5);   /* ...but landmasses stay apart, far enough for two coastlines */
    STAGE("3_moat");
    drop_small(k, w, h, 1, 60);          /* islands too small to draw */
    drop_small(k, w, h, 0, 60);          /* lakes too */
    morph(k, w, h, 1, false);
    morph(k, w, h, 1, true);             /* thin spits */
    STAGE("4_final");
    /* soften: how much of each 3x3 is land (0..9) */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx;
                    if (xx >= 0 && xx < w) c += m[yy * w + xx];
                }
            }
            k->cnt[y * w + x] = (uint8_t)c;
        }
    /* the coastline: where that changes */
    uint8_t *cn = k->cnt, *e = k->edge;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = cn[y * w + x], d = 0;
            if (x > 0) d = abs(c - cn[y * w + x - 1]) > d ? abs(c - cn[y * w + x - 1]) : d;
            if (x < w - 1) d = abs(c - cn[y * w + x + 1]) > d ? abs(c - cn[y * w + x + 1]) : d;
            if (y > 0) d = abs(c - cn[(y - 1) * w + x]) > d ? abs(c - cn[(y - 1) * w + x]) : d;
            if (y < h - 1) d = abs(c - cn[(y + 1) * w + x]) > d ? abs(c - cn[(y + 1) * w + x]) : d;
            int v = d * 255 * 22 / 90;
            e[y * w + x] = (uint8_t)(v > 255 ? 255 : v);
        }
    /* its glow: the coastline spread 3 pixels, fading (across, then down) */
    uint8_t *g = k->tmp;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int best = 0;
            for (int dx = -3; dx <= 3; dx++) {
                int xx = x + dx;
                if (xx < 0 || xx >= w) continue;
                int v = e[y * w + xx] * (4 - abs(dx)) / 4;
                if (v > best) best = v;
            }
            g[y * w + x] = (uint8_t)best;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int best = 0;
            for (int dy = -3; dy <= 3; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                int v = g[yy * w + x] * (4 - abs(dy)) / 4;
                if (v > best) best = v;
            }
            RGB o = mix(SEA, LAND, cn[y * w + x] / 9.0f);
            o = mix(o, GLOW, best / 255.0f * 0.6f);
            o = mix(o, COAST, e[y * w + x] / 255.0f);
            tex[tex_index(tw, x, y)] = rgb565((int)o.r, (int)o.g, (int)o.b);
        }
    return true;
}

/* ------------------------------------------------------------ the thread */

static thr_mutex lock;
static thr_t worker;
static LandPic pics[2];          /* the one shown, and the next one being drawn */
static uint16_t *bufs[2];
static int buf_cap[2];
static int front = -1;           /* which of pics[] is shown */
static volatile bool want, busy;
static struct { int w, h; float lat, lon, s; } req;
static Scratch scratch;

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        thr_lock(&lock);
        bool go = want;
        int w = req.w, h = req.h;
        float lat = req.lat, lon = req.lon, s = req.s;
        int back = front == 0 ? 1 : 0;
        if (go) { want = false; busy = true; }
        thr_unlock(&lock);
        if (!go) { thr_sleep_ms(8); continue; }
        int tw = tex_round(w), th = tex_round(h);
        if (buf_cap[back] < tw * th) {
            free(bufs[back]);
            bufs[back] = memalign(32, (size_t)tw * th * 2);
            buf_cap[back] = bufs[back] ? tw * th : 0;
        }
        bool ok = bufs[back] && render(&scratch, bufs[back], tw, w, h, lat, lon, s);
        thr_lock(&lock);
        if (ok) {
            LandPic *p = &pics[back];
            p->w = w; p->h = h; p->lat = lat; p->lon = lon; p->s = s;
            p->ready = false;
            p->img.data = NULL;
            /* (made into a texture on the main thread, which owns the GPU) */
            p->ready = true;
            front = back;
        }
        busy = false;
        thr_unlock(&lock);
    }
    return NULL;
}

void mapland_init(void) {
    thr_mutex_init(&lock);
    thr_create(&worker, worker_main, NULL, 64 * 1024, 30);   /* (below the photo thread) */
}

void mapland_request(int w, int h, float lat, float lon, float s) {
    thr_lock(&lock);
    req.w = w; req.h = h; req.lat = lat; req.lon = lon; req.s = s;
    want = true;
    thr_unlock(&lock);
}

bool mapland_busy(void) { return want || busy; }

const LandPic *mapland_current(void) {
    thr_lock(&lock);
    LandPic *p = front >= 0 && pics[front].ready ? &pics[front] : NULL;
    if (p && !p->img.data) {
        int tw = tex_round(p->w), th = tex_round(p->h);
        /* gfx_image_init frees the buffer if it fails; keep ours */
        GfxImage im;
        if (gfx_image_init(&im, bufs[front], p->w, p->h, tw, th, GFX_RGB565)) p->img = im;
        else { bufs[front] = NULL; buf_cap[front] = 0; p->ready = false; p = NULL; }
    }
    thr_unlock(&lock);
    return p;
}

bool mapland_draw_now(LandPic *pic, int w, int h, float lat, float lon, float s) {
    static Scratch small;
    mapland_free(pic);
    int tw = tex_round(w), th = tex_round(h);
    uint16_t *t = memalign(32, (size_t)tw * th * 2);
    if (!t) return false;
    memset(t, 0, (size_t)tw * th * 2);
    if (!render(&small, t, tw, w, h, lat, lon, s)) { free(t); return false; }
    if (!gfx_image_init(&pic->img, t, w, h, tw, th, GFX_RGB565)) return false;
    pic->w = w; pic->h = h; pic->lat = lat; pic->lon = lon; pic->s = s;
    pic->ready = true;
    return true;
}

void mapland_free(LandPic *pic) {
    if (pic->ready && pic->img.data) gfx_image_free(&pic->img);
    memset(pic, 0, sizeof(*pic));
}

void mapland_show(const LandPic *pic, float x, float y, float w, float h, float lat, float lon, float s, float alpha) {
    if (!pic || !pic->ready || !pic->img.data) return;
    float k = s / pic->s;
    float pw = pic->w * k, ph = pic->h * k;
    float cx = x + w / 2 + (pic->lon - lon) * s, cy = y + h / 2 - (pic->lat - lat) * s;
    gfx_image_draw((GfxImage *)&pic->img, cx - pw / 2, cy - ph / 2, pw, ph, rgba(255, 255, 255, (u8)(255 * alpha)));
}
