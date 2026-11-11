/* geo.c - see geo.h. */
#include "geo.h"
#include "photos.h"
#include "storage.h"
#include "map_data.h"
#include "thr.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What's known, by photo key: an open-addressed table. */
#define TABLE 8192              /* a power of two, twice MAX_PHOTOS */
enum { G_EMPTY = 0, G_PLACE = 1, G_NONE = 2 };
typedef struct { uint32_t key; float lat, lon; uint8_t state; } GeoEntry;
static GeoEntry *table;
static int nentries;

static thr_mutex lock;
static bool dirty;
static unsigned generation;
static int save_wait;

/* Photos for the thread to read: (key, path) handed over by the main thread. */
#define JOBS 16
typedef struct { uint32_t key; char path[300]; } Job;
static Job jobs[JOBS];
static int njobs;               /* jobs[0..njobs) are waiting */
static int cursor;              /* where the main thread looks next */
static int last_count = -1;
static bool all_read;
static int known_now;

static GeoEntry *slot_for(uint32_t key, bool add) {
    if (!table || !key) return NULL;
    uint32_t h = key * 2654435761u;
    for (int i = 0; i < TABLE; i++) {
        GeoEntry *e = &table[(h + (uint32_t)i) & (TABLE - 1)];
        if (e->state && e->key == key) return e;
        if (!e->state) {
            if (!add || nentries >= TABLE * 3 / 4) return NULL;
            e->key = key;
            nentries++;
            return e;
        }
    }
    return NULL;
}

static void put(uint32_t key, bool has, float lat, float lon) {
    GeoEntry *e = slot_for(key, true);
    if (!e) return;
    if (e->state == (has ? G_PLACE : G_NONE) && (!has || (e->lat == lat && e->lon == lon))) return;
    e->state = has ? G_PLACE : G_NONE;
    e->lat = lat;
    e->lon = lon;
    dirty = true;
    if (has) generation++;
}

void geo_init(void) {
    thr_mutex_init(&lock);
    table = calloc(TABLE, sizeof(GeoEntry));
}

/* Reads places.dat (once the card has been looked at). */
static bool loaded;
static void load(void) {
    loaded = true;
    if (!storage_ok()) return;
    char path[300];
    storage_path(path, sizeof(path), "places.dat");
    char *text = storage_read(path, NULL);
    if (!text) return;
    for (char *line = text; line && *line;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        unsigned key;
        float lat, lon;
        thr_lock(&lock);
        if (sscanf(line, "%x %f %f", &key, &lat, &lon) == 3) put(key, true, lat, lon);
        else if (sscanf(line, "%x -", &key) == 1) put(key, false, 0, 0);
        thr_unlock(&lock);
        line = next;
    }
    free(text);
    dirty = false;
}

static void save(void) {
    if (!storage_ok() || !table) return;
    /* only photos still in the library */
    int n = photos_count();
    size_t cap = (size_t)n * 32 + 64, len = 0;
    char *buf = malloc(cap);
    if (!buf) return;
    thr_lock(&lock);
    for (int i = 0; i < n && len < cap - 40; i++) {
        uint32_t key = photos_key_of(i);
        GeoEntry *e = slot_for(key, false);
        if (!e) continue;
        if (e->state == G_PLACE) len += (size_t)snprintf(buf + len, cap - len, "%08x %.5f %.5f\n", (unsigned)key, e->lat, e->lon);
        else len += (size_t)snprintf(buf + len, cap - len, "%08x -\n", (unsigned)key);
    }
    dirty = false;
    thr_unlock(&lock);
    char path[300];
    storage_path(path, sizeof(path), "places.dat");
    storage_write_atomic(path, buf, len);
    free(buf);
}

void geo_tick(void) {
    if (!table) return;
    if (photos_scanning()) { last_count = -1; return; }
    if (!loaded) load();
    int n = photos_count();
    thr_lock(&lock);
    if (n != last_count) {
        /* the library changed: look through it again */
        last_count = n;
        cursor = 0;
        all_read = false;
        njobs = 0;
    }
    /* hand out photos nobody has read yet */
    if (!all_read && njobs < JOBS / 2) {
        int looked = 0;
        while (njobs < JOBS && looked < 400) {
            if (cursor >= n) {
                if (njobs == 0) all_read = true;
                cursor = 0;
                break;
            }
            Photo *p = photos_get(cursor++);
            looked++;
            if (!p || slot_for(p->key, false)) continue;
            bool queued = false;
            for (int j = 0; j < njobs; j++) if (jobs[j].key == p->key) queued = true;
            if (queued) continue;
            jobs[njobs].key = p->key;
            snprintf(jobs[njobs].path, sizeof(jobs[njobs].path), "%s", p->path);
            njobs++;
        }
    }
    if (dirty) save_wait++;
    thr_unlock(&lock);
    /* count what's known, a slice per frame */
    static int count_at, count_acc;
    for (int k = 0; k < 300; k++) {
        if (count_at >= n) { known_now = count_acc; count_at = 0; count_acc = 0; break; }
        uint32_t key = photos_key_of(count_at++);
        thr_lock(&lock);
        if (slot_for(key, false)) count_acc++;
        thr_unlock(&lock);
    }
    /* save now and then while reading, and soon after it's done */
    if (dirty && (save_wait > 60 * 20 || (all_read && njobs == 0 && save_wait > 60))) {
        save_wait = 0;
        save();
    }
}

bool geo_work(void) {
    Job job;
    thr_lock(&lock);
    if (njobs == 0) { thr_unlock(&lock); return false; }
    job = jobs[0];
    thr_unlock(&lock);
    PicInfo info;
    bool ok = pic_info(job.path, &info);
    thr_lock(&lock);
    put(job.key, ok && info.has_gps, info.lat, info.lon);
    /* drop it from the list (it may have been cleared meanwhile) */
    for (int j = 0; j < njobs; j++)
        if (jobs[j].key == job.key) {
            memmove(&jobs[j], &jobs[j + 1], sizeof(Job) * (size_t)(njobs - j - 1));
            njobs--;
            break;
        }
    thr_unlock(&lock);
    return true;
}

void geo_note(uint32_t key, const PicInfo *info) {
    if (!table) return;
    thr_lock(&lock);
    put(key, info->has_gps, info->lat, info->lon);
    thr_unlock(&lock);
}

bool geo_get(uint32_t key, float *lat, float *lon) {
    thr_lock(&lock);
    GeoEntry *e = slot_for(key, false);
    bool has = e && e->state == G_PLACE;
    if (has) { *lat = e->lat; *lon = e->lon; }
    thr_unlock(&lock);
    return has;
}

int geo_known(void) { return known_now; }
bool geo_reading(void) { return !all_read || njobs > 0; }
unsigned geo_generation(void) { return generation; }

/* --------------------------------------------------------------- naming */

/* Rough distance in km. */
static float km_between(float la1, float lo1, float la2, float lo2) {
    float dl = lo2 - lo1;
    if (dl > 180) dl -= 360;
    if (dl < -180) dl += 360;
    float x = dl * cosf((la1 + la2) * 0.5f * (float)M_PI / 180.0f);
    float y = la2 - la1;
    return sqrtf(x * x + y * y) * 111.2f;
}

/* The nearest city within reach (bigger cities reach further). */
static const MapCity *nearest_city(float lat, float lon) {
    const MapCity *best = NULL;
    float best_score = 1e9f;
    int lo = 0, hi = MAP_NCITIES;
    int ilat = (int)((lat - 2.5f) * 100);
    while (lo < hi) { int mid = (lo + hi) / 2; if (map_cities[mid].lat < ilat) lo = mid + 1; else hi = mid; }
    for (int i = lo; i < MAP_NCITIES && map_cities[i].lat <= (int)((lat + 2.5f) * 100); i++) {
        const MapCity *c = &map_cities[i];
        float d = km_between(lat, lon, c->lat / 100.0f, c->lon / 100.0f);
        float reach = 60.0f + 25.0f * log10f(1.0f + c->pop_k);   /* 60 km to about 160 km */
        if (d > reach) continue;
        float score = d / reach;
        if (score < best_score) { best_score = score; best = c; }
    }
    return best;
}

void geo_format_coords(float lat, float lon, char *out, size_t cap) {
    snprintf(out, cap, "%.2f %c, %.2f %c", fabsf(lat), lat >= 0 ? 'N' : 'S', fabsf(lon), lon >= 0 ? 'E' : 'W');
}

void geo_place_name(float lat, float lon, char *out, size_t cap) {
    const MapCity *c = nearest_city(lat, lon);
    if (c) {
        const char *country = map_countries[c->country];
        if (country[0] && strcmp(country, c->name)) snprintf(out, cap, "%s, %s", c->name, country);
        else snprintf(out, cap, "%s", c->name);
    } else {
        geo_format_coords(lat, lon, out, cap);
    }
}

const char *geo_country(float lat, float lon) {
    const MapCity *c = nearest_city(lat, lon);
    return c ? map_countries[c->country] : "";
}
