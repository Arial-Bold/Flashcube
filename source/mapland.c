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
    bool can_stop;               /* (the thread's: gives up for a newer view) */
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

/* Grows (or shrinks) the land by r pixels (a square, done across and then
 * down, so it costs the same whatever r is). */
static void morph(Scratch *k, int w, int h, int r, bool grow) {
    uint8_t *m = k->mask, *t = k->tmp;
    uint8_t want = grow ? 1 : 0;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = m + y * w;
        uint8_t *out = t + y * w;
        int run = -1000000;                      /* where `want` was last seen */
        for (int x = 0; x < w + r; x++) {
            if (x < w && row[x] == want) run = x;
            int ox = x - r;
            if (ox >= 0) {
                /* is `want` within r on either side? (the right side is x) */
                out[ox] = (x - run <= 2 * r) ? want : (uint8_t)!want;
            }
        }
    }
    for (int x = 0; x < w; x++) {
        int run = -1000000;
        for (int y = 0; y < h + r; y++) {
            if (y < h && t[y * w + x] == want) run = y;
            int oy = y - r;
            if (oy >= 0) m[oy * w + x] = (y - run <= 2 * r) ? want : (uint8_t)!want;
        }
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

/* Every colour a pixel can be: by how much land is around it (0..9), its
 * glow and its coastline (each in 16 steps). */
static uint16_t shade[10][16][16];
static bool shade_made;
static uint16_t detail[16];            /* land with its texture, faint to less faint */
static void make_shades(void) {
    for (int c = 0; c < 10; c++)
        for (int g = 0; g < 16; g++)
            for (int e = 0; e < 16; e++) {
                RGB o = mix(SEA, LAND, c / 9.0f);
                o = mix(o, GLOW, g / 15.0f * 0.6f);
                o = mix(o, COAST, e / 15.0f);
                shade[c][g][e] = rgb565((int)o.r, (int)o.g, (int)o.b);
            }
    for (int v = 0; v < 16; v++) {
        RGB o = mix(LAND, COAST, v / 15.0f * 0.45f);
        detail[v] = rgb565((int)o.r, (int)o.g, (int)o.b);
    }
    shade_made = true;
}

/* Set when a newer view is wanted: the one being drawn is given up. */
static volatile bool want;
#define GIVE_UP() do { if (k->can_stop && want) return false; } while (0)

#define OFF_MAP (-1000000)

/* Draws a view into tex (tiled RGB565, tw wide). */
static bool render(Scratch *k, uint16_t *tex, int tw, int w, int h, float lat_c, float lon_c, float s, float grid) {
    if (!scratch_fit(k, w * h)) return false;
    if (!shade_made) make_shades();
    uint8_t *m = k->mask;
    /* land or water at each pixel's middle: where each column and row falls
     * among the data's cells, worked out once */
    int *col = k->stack, *row = k->stack + w;
    uint8_t *fx = k->edge, *fy = k->edge + w;      /* (borrowed for now) */
    for (int x = 0; x < w; x++) {
        float lon = lon_c + (x + 0.5f - w / 2.0f) / s;
        float c = (lon + 180) * MAP_RES - 0.5f;
        int c0 = (int)floorf(c);
        col[x] = (lon < -180 || lon >= 180) ? OFF_MAP : c0;   /* (past the date line: no map there) */
        fx[x] = (uint8_t)((c - c0) * 255);
    }
    for (int y = 0; y < h; y++) {
        float lat = lat_c - (y + 0.5f - h / 2.0f) / s;
        float r = (MAP_LAT_TOP - lat) * MAP_RES - 0.5f;
        int r0 = (int)floorf(r);
        row[y] = r0;
        fy[y] = (uint8_t)((r - r0) * 255);
    }
    for (int y = 0; y < h; y++) {
        int r0 = row[y], ty = fy[y];
        bool in0 = r0 >= 0 && r0 < MAP_ROWS, in1 = r0 + 1 >= 0 && r0 + 1 < MAP_ROWS;
        const uint8_t *b0 = map_land_bits + (in0 ? r0 : 0) * (MAP_COLS / 8), *b1 = map_land_bits + (in1 ? r0 + 1 : 0) * (MAP_COLS / 8);
        for (int x = 0; x < w; x++) {
            int c0 = col[x];
            if (c0 == OFF_MAP || (!in0 && !in1)) { m[y * w + x] = 0; continue; }
            int c1 = c0 + 1;
            c0 = (c0 + MAP_COLS) % MAP_COLS;
            c1 = c1 % MAP_COLS;
            int tx = fx[x];
            /* the four cells around, weighted by nearness (out of 255*255) */
            int v = 0;
            if (in0 && (b0[c0 >> 3] & (0x80 >> (c0 & 7)))) v += (255 - ty) * (255 - tx);
            if (in0 && (b0[c1 >> 3] & (0x80 >> (c1 & 7)))) v += (255 - ty) * tx;
            if (in1 && (b1[c0 >> 3] & (0x80 >> (c0 & 7)))) v += ty * (255 - tx);
            if (in1 && (b1[c1 >> 3] & (0x80 >> (c1 & 7)))) v += ty * tx;
            m[y * w + x] = v >= 255 * 255 / 2;
        }
        if ((y & 31) == 0) GIVE_UP();
    }
#ifndef STAGE
#define STAGE(n)
#endif
    STAGE("0_raw");
    /* simplify for the screen */
    moat(k, w, h, lat_c, lon_c, s, 3);
    morph(k, w, h, 2, true);
    morph(k, w, h, 2, false);            /* inlets and gaps under about 4 pixels close */
    moat(k, w, h, lat_c, lon_c, s, 5);   /* ...but landmasses stay apart, far enough for two coastlines */
    STAGE("2_closed");
    GIVE_UP();
    drop_small(k, w, h, 1, 60);          /* islands too small to draw */
    drop_small(k, w, h, 0, 60);          /* lakes too */
    morph(k, w, h, 1, false);
    morph(k, w, h, 1, true);             /* thin spits */
    STAGE("4_final");
    GIVE_UP();
    /* soften: how much of each 3x3 is land (0..9), across then down */
    /* the columns that are on the map (in 16:9 the world doesn't reach the
     * sides): beyond them is plain sea, and land at the date line just
     * stops there, with no coastline drawn down it */
    int x_lo = 0, x_hi = w - 1;
    {
        float lon0 = lon_c + (0.5f - w / 2.0f) / s, lon1 = lon_c + (w - 0.5f - w / 2.0f) / s;
        if (lon0 < -180) x_lo = (int)ceilf((-180 - lon_c) * s + w / 2.0f - 0.5f);
        if (lon1 >= 180) x_hi = (int)floorf((180 - lon_c) * s + w / 2.0f - 0.5f) - 1;
        if (x_lo < 0) x_lo = 0;
        if (x_hi > w - 1) x_hi = w - 1;
    }
    uint8_t *cn = k->cnt, *t = k->tmp, *e = k->edge;
    for (int y = 0; y < h; y++) {
        const uint8_t *r = m + y * w;
        /* (past the picture's edge counts as more of the same, so land
         * running off the edge gets no coastline there) */
        for (int x = 0; x < w; x++) {
            int xc = x < x_lo ? x_lo : x > x_hi ? x_hi : x;
            t[y * w + x] = (uint8_t)(r[xc] + r[xc > x_lo ? xc - 1 : xc] + r[xc < x_hi ? xc + 1 : xc]);
        }
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            cn[y * w + x] = (uint8_t)(t[y * w + x] + t[(y > 0 ? y - 1 : y) * w + x] + t[(y < h - 1 ? y + 1 : y) * w + x]);
    /* the coastline: where that changes (0..15) */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int i = y * w + x, c = cn[i], d = 0, q;
            if (x > x_lo && x <= x_hi && (q = abs(c - cn[i - 1])) > d) d = q;
            if (x < x_hi && x >= x_lo && (q = abs(c - cn[i + 1])) > d) d = q;
            if (y > 0 && (q = abs(c - cn[i - w])) > d) d = q;
            if (y < h - 1 && (q = abs(c - cn[i + w])) > d) d = q;
            int v = d * 15 * 22 / 90;
            e[i] = (uint8_t)(v > 15 ? 15 : v);
        }
    GIVE_UP();
    /* a faint grid of latitude and longitude on the land, at strength grid (0..15 in d) */
    uint8_t *d = (uint8_t *)k->seen;       /* (borrowed: drop_small is done) */
    memset(d, 0, (size_t)w * h);
    if (grid > 0.05f) {
        /* lines of latitude and longitude, spaced to suit the zoom */
        uint8_t level = (uint8_t)(grid * 5 + 0.5f);
        static const float steps[] = { 1, 2, 5, 10, 15, 30 };
        float step = 30;
        for (int j = 0; j < 6; j++) if (steps[j] * s >= 28) { step = steps[j]; break; }
        uint8_t *cl = k->tmp;              /* (per column, then per row; tmp is free until the glow) */
        for (int x = 0; x < w; x++) {
            float a = lon_c + (x - w / 2.0f) / s, b = lon_c + (x + 1 - w / 2.0f) / s;
            cl[x] = floorf(a / step) != floorf(b / step);
        }
        for (int y = 0; y < h; y++) {
            float a = lat_c - (y - h / 2.0f) / s, b = lat_c - (y + 1 - h / 2.0f) / s;
            bool rl = floorf(a / step) != floorf(b / step);
            for (int x = 0; x < w; x++) if (rl || cl[x]) d[y * w + x] = level;
        }
    }
    /* its glow: the coastline spread 3 pixels, fading (across, then down) */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int best = 0;
            for (int dx = -3; dx <= 3; dx++) {
                int xx = x + dx;
                if (xx < 0 || xx >= w) continue;
                int v = e[y * w + xx] * (4 - abs(dx)) / 4;
                if (v > best) best = v;
            }
            t[y * w + x] = (uint8_t)best;
        }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int best = 0;
            for (int dy = -3; dy <= 3; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                int v = t[yy * w + x] * (4 - abs(dy)) / 4;
                if (v > best) best = v;
            }
            int i = y * w + x;
            uint16_t px = (x < x_lo || x > x_hi) ? shade[0][0][0] : shade[cn[i]][best][e[i]];
            if (d[i] && cn[i] == 9 && e[i] == 0 && best <= 1 && x >= x_lo && x <= x_hi) px = detail[d[i]];
            tex[tex_index(tw, x, y)] = px;
        }
        if ((y & 31) == 0 && k->can_stop && want) break;
    }
    memset(k->seen, 0, sizeof(int) * (size_t)w * h);   /* (handed back to drop_small) */
    k->stamp = 0;
    return !(k->can_stop && want);
}

/* ------------------------------------------------------------ the thread */

static thr_mutex lock;
static thr_t worker;
static LandPic pics[2];          /* the one shown, and the next one being drawn */
static uint16_t *bufs[2];
static int buf_cap[2];
static int front = -1;           /* which of pics[] is shown */
static volatile bool busy;
static struct { int w, h; float lat, lon, s, grid; } req;
static Scratch scratch = { .can_stop = true };

/* The whole world, drawn once: shown under the current view so zooming
 * out never shows empty sea while the sharp picture is being drawn. */
static LandPic world;
static uint16_t *world_buf;
static volatile bool world_want, world_done;
static struct { int w, h; float lat, lon, s; } world_req;

static void draw_world(void) {
    int w = world_req.w, h = world_req.h;
    int tw = tex_round(w), th = tex_round(h);
    uint16_t *t = memalign(32, (size_t)tw * th * 2);
    if (!t) return;
    memset(t, 0, (size_t)tw * th * 2);
    scratch.can_stop = false;
    bool ok = render(&scratch, t, tw, w, h, world_req.lat, world_req.lon, world_req.s, 0);
    scratch.can_stop = true;
    thr_lock(&lock);
    if (ok) {
        world_buf = t;
        world.w = w; world.h = h; world.lat = world_req.lat; world.lon = world_req.lon; world.s = world_req.s;
        world.img.data = NULL;
        world.ready = true;
        world_done = true;
    } else {
        free(t);
    }
    thr_unlock(&lock);
}

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        thr_lock(&lock);
        bool go = want;
        int w = req.w, h = req.h;
        float lat = req.lat, lon = req.lon, s = req.s, grid = req.grid;
        int back = front == 0 ? 1 : 0;
        if (go) { want = false; busy = true; }
        thr_unlock(&lock);
        if (!go) {
            if (world_want && !world_done) { world_want = false; draw_world(); continue; }
            thr_sleep_ms(8);
            continue;
        }
        int tw = tex_round(w), th = tex_round(h);
        if (buf_cap[back] < tw * th) {
            free(bufs[back]);
            bufs[back] = memalign(32, (size_t)tw * th * 2);
            buf_cap[back] = bufs[back] ? tw * th : 0;
        }
        bool ok = bufs[back] && render(&scratch, bufs[back], tw, w, h, lat, lon, s, grid);
        thr_lock(&lock);
        if (ok) {
            LandPic *p = &pics[back];
            p->w = w; p->h = h; p->lat = lat; p->lon = lon; p->s = s; p->grid = grid;
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
    /* (above the photo thread, so the map isn't kept waiting while
     * thumbnails load, and below the main thread) */
    thr_create(&worker, worker_main, NULL, 64 * 1024, 45);
}

void mapland_request(int w, int h, float lat, float lon, float s, float grid) {
    thr_lock(&lock);
    req.w = w; req.h = h; req.lat = lat; req.lon = lon; req.s = s; req.grid = grid;
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

const LandPic *mapland_world(int w, int h, float lat, float lon, float s) {
    thr_lock(&lock);
    if (world_done && (world.w != w || world.h != h || world.s != s)) {
        /* (the screen changed shape: draw it again) */
        if (world.img.data) { gfx_image_free(&world.img); world_buf = NULL; }
        else free(world_buf);
        world_buf = NULL;
        memset(&world, 0, sizeof(world));
        world_done = false;
    }
    if (!world_done) {
        world_req.w = w; world_req.h = h; world_req.lat = lat; world_req.lon = lon; world_req.s = s;
        world_want = true;
        thr_unlock(&lock);
        return NULL;
    }
    if (!world.img.data && world_buf) {
        int tw = tex_round(w), th = tex_round(h);
        GfxImage im;
        if (gfx_image_init(&im, world_buf, w, h, tw, th, GFX_RGB565)) world.img = im;
        else { world_buf = NULL; world.ready = false; }
    }
    thr_unlock(&lock);
    return world.ready && world.img.data ? &world : NULL;
}

bool mapland_draw_now(LandPic *pic, int w, int h, float lat, float lon, float s) {
    static Scratch small;
    mapland_free(pic);
    int tw = tex_round(w), th = tex_round(h);
    uint16_t *t = memalign(32, (size_t)tw * th * 2);
    if (!t) return false;
    memset(t, 0, (size_t)tw * th * 2);
    if (!render(&small, t, tw, w, h, lat, lon, s, 0)) { free(t); return false; }
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
