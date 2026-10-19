/* photos.c - see photos.h. */
#include "photos.h"
#include "storage.h"
#include "aramcache.h"
#include "applog.h"
#include "pixels.h"
#include "settings.h"
#include "thr.h"
#include <ctype.h>
#include <dirent.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static Photo photos[MAX_PHOTOS];
void (*photos_on_insert)(int index);
static int nphotos;
static Folder folders[MAX_FOLDERS];
static int nfolders;
static char status[80];

/* path storage: big blocks, freed all together on a rescan */
#define ARENA_BLOCK 65536
typedef struct Arena { struct Arena *next; size_t used; char data[ARENA_BLOCK]; } Arena;
static Arena *arena;

static char *arena_dup(const char *s) {
    size_t n = strlen(s) + 1;
    if (n > ARENA_BLOCK) return NULL;
    if (!arena || arena->used + n > ARENA_BLOCK) {
        Arena *a = malloc(sizeof(Arena));
        if (!a) return NULL;
        a->next = arena;
        a->used = 0;
        arena = a;
    }
    char *p = arena->data + arena->used;
    memcpy(p, s, n);
    arena->used += n;
    return p;
}

static void arena_free(void) {
    while (arena) { Arena *n = arena->next; free(arena); arena = n; }
}

static uint32_t fnv(const char *s, uint32_t h) {
    while (*s) { h ^= (uint8_t)tolower((unsigned char)*s++); h *= 16777619u; }
    return h;
}

static uint32_t photo_key(const char *path, uint32_t size, time_t mtime) {
    uint32_t h = fnv(path, 2166136261u);
    h ^= size; h *= 16777619u;
    h ^= (uint32_t)mtime; h *= 16777619u;
    return h ? h : 1;
}

/* ------------------------------------------------------------- dates */

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int64_t parse_date(const char *s) {
    int y, mo, d, h = 0, mi = 0, se = 0;
    if (sscanf(s, "%d:%d:%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3) return 0;
    if (y < 1980 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
}

int photo_year(const Photo *p) {
    int64_t t = p->when;
    int64_t days = t / 86400;
    /* civil_from_days */
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int doe = (int)(days - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)(yoe + era * 400);
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int m = mp + (mp < 10 ? 3 : -9);
    return y + (m <= 2);
}

void photos_format_date(const Photo *p, char *out, size_t cap) {
    static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int64_t days = p->when / 86400 + 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int doe = (int)(days - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)(yoe + era * 400);
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp + (mp < 10 ? 3 : -9);
    if (m <= 2) y++;
    if (p->when <= 0) { snprintf(out, cap, "No date"); return; }
    snprintf(out, cap, "%d %s %d", d, mon[(m - 1) % 12], y);
}

/* ------------------------------------------------------- index file */

typedef struct { uint32_t key; uint16_t w, h; char date[20]; } IndexEntry;
static IndexEntry *idx;
static int nidx, idx_cap;
static bool idx_dirty;

static int idx_cmp(const void *a, const void *b) {
    uint32_t x = ((const IndexEntry *)a)->key, y = ((const IndexEntry *)b)->key;
    return x < y ? -1 : x > y;
}

static void index_load(void) {
    free(idx);
    idx = NULL;
    nidx = idx_cap = 0;
    char path[300];
    storage_path(path, sizeof(path), "index.dat");
    char *text = storage_read(path, NULL);
    if (!text) return;
    for (char *line = text; line && *line;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        unsigned key, w, h;
        char date[24] = "";
        int got = sscanf(line, "%x %u %u %19[0-9: ]", &key, &w, &h, date);
        if (got >= 3) {
            if (nidx == idx_cap) {
                int nc = idx_cap ? idx_cap * 2 : 512;
                IndexEntry *n = realloc(idx, sizeof(IndexEntry) * nc);
                if (!n) break;
                idx = n;
                idx_cap = nc;
            }
            IndexEntry *e = &idx[nidx++];
            e->key = key; e->w = (uint16_t)w; e->h = (uint16_t)h;
            snprintf(e->date, sizeof(e->date), "%s", got >= 4 ? date : "");
        }
        line = next;
    }
    free(text);
    qsort(idx, nidx, sizeof(IndexEntry), idx_cmp);
}

static IndexEntry *index_find(uint32_t key) {
    IndexEntry k = { key, 0, 0, "" };
    return idx ? bsearch(&k, idx, nidx, sizeof(IndexEntry), idx_cmp) : NULL;
}

/* Writes what we know about the photos now in the library. */
static void index_save(void) {
    if (!storage_ok()) return;
    size_t cap = (size_t)nphotos * 48 + 64, n = 0;
    char *buf = malloc(cap);
    if (!buf) return;
    for (int i = 0; i < nphotos; i++) {
        Photo *p = &photos[i];
        if (!p->info_known) continue;
        n += (size_t)snprintf(buf + n, cap - n, "%08x %u %u %s\n", (unsigned)p->key, p->w, p->h, p->date);
        if (n >= cap - 64) break;
    }
    char path[300];
    storage_path(path, sizeof(path), "index.dat");
    storage_write_atomic(path, buf, n);
    free(buf);
    idx_dirty = false;
}

/* ------------------------------------------------------- favourites */

static void favs_load(void) {
    char path[300];
    storage_path(path, sizeof(path), "favorites.txt");
    char *text = storage_read(path, NULL);
    if (!text) return;
    for (char *line = text; line && *line;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[l - 1] = 0;
        for (int i = 0; i < nphotos; i++)
            if (!strcasecmp(photos[i].path, line)) { photos[i].fav = true; break; }
        line = next;
    }
    free(text);
}

static void favs_save(void) {
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    if (!buf) return;
    for (int i = 0; i < nphotos; i++) {
        if (!photos[i].fav) continue;
        size_t l = strlen(photos[i].path) + 1;
        if (n + l + 1 > cap) {
            char *nb = realloc(buf, cap * 2);
            if (!nb) break;
            buf = nb;
            cap *= 2;
        }
        n += (size_t)snprintf(buf + n, cap - n, "%s\n", photos[i].path);
    }
    char path[300];
    storage_path(path, sizeof(path), "favorites.txt");
    storage_write_atomic(path, buf, n);
    free(buf);
}

void photos_set_fav(int i, bool on) {
    if (i < 0 || i >= nphotos || photos[i].fav == on) return;
    photos[i].fav = on;
    favs_save();
}

/* ------------------------------------------------------------- scan */

static bool is_picture(const char *name) {
    if (name[0] == '.') return false;   /* "._IMG_0001.JPG" files a Mac leaves behind */
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") || !strcasecmp(dot, ".png") ||
           !strcasecmp(dot, ".bmp") || !strcasecmp(dot, ".gif");
}

static int folder_for(const char *name) {
    for (int i = 0; i < nfolders; i++) if (!strcasecmp(folders[i].name, name)) return i;
    if (nfolders >= MAX_FOLDERS) return -1;
    snprintf(folders[nfolders].name, sizeof(folders[nfolders].name), "%s", name);
    folders[nfolders].count = 0;
    return nfolders++;
}

static void fill_from_index(Photo *p) {
    IndexEntry *e = index_find(p->key);
    p->when = (int64_t)p->mtime;
    if (!e) return;
    p->w = e->w; p->h = e->h;
    snprintf(p->date, sizeof(p->date), "%s", e->date);
    p->info_known = true;
    int64_t t = parse_date(p->date);
    if (t) p->when = t;
}

static int add_photo(const char *path, int folder) {
    if (nphotos >= MAX_PHOTOS || folder < 0) return -1;
    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode) || st.st_size <= 0) return -1;
    char *copy = arena_dup(path);
    if (!copy) return -1;
    Photo *p = &photos[nphotos];
    memset(p, 0, sizeof(*p));
    p->path = copy;
    const char *slash = strrchr(copy, '/');
    p->name = slash ? slash + 1 : copy;
    p->folder = folder;
    p->size = (uint32_t)st.st_size;
    p->mtime = st.st_mtime;
    p->key = photo_key(path, p->size, p->mtime);
    fill_from_index(p);
    folders[folder].count++;
    return nphotos++;
}

static void scan_dir(const char *dir, const char *folder_name, int depth) {
    DIR *d = opendir(dir);
    if (!d) return;
    char (*subdirs)[64] = NULL;
    int nsub = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[300];
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path)) continue;
        bool isdir = false;
#ifdef DT_DIR
        if (e->d_type == DT_DIR) isdir = true;
        else if (e->d_type == DT_UNKNOWN) isdir = storage_is_dir(path);
#else
        isdir = storage_is_dir(path);
#endif
        if (isdir) {
            if (depth < 1 && strlen(e->d_name) < 64) {
                char (*n)[64] = realloc(subdirs, sizeof(*subdirs) * (nsub + 1));
                if (n) { subdirs = n; snprintf(subdirs[nsub++], 64, "%s", e->d_name); }
            }
        } else if (is_picture(e->d_name)) {
            add_photo(path, folder_for(folder_name));
        }
    }
    closedir(d);
    for (int i = 0; i < nsub; i++) {
        char path[300];
        snprintf(path, sizeof(path), "%s/%s", dir, subdirs[i]);
        scan_dir(path, subdirs[i], depth + 1);
    }
    free(subdirs);
}

static int cmp_when(const void *a, const void *b) {
    const Photo *x = a, *y = b;
    if (x->when != y->when) return (x->when > y->when) == (g_set.sort_newest != 0) ? -1 : 1;
    return strcasecmp(x->path, y->path);
}

/* (thumbnail cache entries refer to photos by key, so sorting is safe) */
static void sort_photos(void) { qsort(photos, nphotos, sizeof(Photo), cmp_when); }

static thr_mutex lock;
static volatile bool scanning = true;   /* until the first scan finishes */

bool photos_scanning(void) { return scanning; }

/* Reads the folder. Runs on its own thread at start-up: until it's done the
 * library looks empty (and the main thread never waits for it). */
void photos_scan(void) {
    thr_lock(&lock);
    scanning = true;
    nphotos = 0;
    nfolders = 0;
    thr_unlock(&lock);
    arena_free();
    status[0] = 0;
    if (!storage_ok()) { snprintf(status, sizeof(status), "No SD card found"); scanning = false; return; }
    if (!storage_is_dir(PHOTO_ROOT)) {
        snprintf(status, sizeof(status), "No DCIM folder on the SD card");
        scanning = false;
        return;
    }
    index_load();
    scan_dir(PHOTO_ROOT, "DCIM", 0);
    sort_photos();
    favs_load();
    /* drop empty folders */
    int k = 0;
    for (int i = 0; i < nfolders; i++) {
        if (!folders[i].count) continue;
        for (int p = 0; p < nphotos; p++) if (photos[p].folder == i) photos[p].folder = k;
        folders[k++] = folders[i];
    }
    nfolders = k;
    if (!nphotos) snprintf(status, sizeof(status), "No photos in the DCIM folder yet");
    free(idx);
    idx = NULL;
    nidx = idx_cap = 0;
    scanning = false;
}

int photos_count(void) { return scanning ? 0 : nphotos; }
Photo *photos_get(int i) { return (!scanning && i >= 0 && i < nphotos) ? &photos[i] : NULL; }
int photos_folder_count(void) { return scanning ? 0 : nfolders; }
const Folder *photos_folder(int i) { return (i >= 0 && i < nfolders) ? &folders[i] : NULL; }
const char *photos_status(void) { return status; }

int photos_view(ViewKind kind, int arg, int *out, int cap) {
    int n = 0;
    for (int i = 0; i < nphotos && n < cap; i++) {
        const Photo *p = &photos[i];
        bool keep = kind == VIEW_ALL || (kind == VIEW_FOLDER && p->folder == arg) ||
                    (kind == VIEW_FAVORITES && p->fav) || (kind == VIEW_YEAR && photo_year(p) == arg);
        if (keep) out[n++] = i;
    }
    return n;
}

int photos_years(int *out, int cap) {
    int n = 0;
    for (int i = 0; i < nphotos; i++) {
        int y = photo_year(&photos[i]);
        bool seen = false;
        for (int k = 0; k < n; k++) if (out[k] == y) { seen = true; break; }
        if (!seen && n < cap) out[n++] = y;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (out[b] > out[a]) { int t = out[a]; out[a] = out[b]; out[b] = t; }
    return n;
}

int photos_add_file(const char *path) {
    char dir[300];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) return -1;
    *slash = 0;
    const char *fname = strrchr(dir, '/');
    fname = fname ? fname + 1 : dir;
    thr_lock(&lock);
    int f = folder_for(strcasecmp(dir, PHOTO_ROOT) ? fname : "DCIM");
    int i = add_photo(path, f);
    if (i >= 0) {
        /* keep the library sorted: move the new one into place */
        Photo p = photos[i];
        int at = 0;
        while (at < i && cmp_when(&photos[at], &p) < 0) at++;
        memmove(&photos[at + 1], &photos[at], sizeof(Photo) * (i - at));
        photos[at] = p;
        i = at;
    }
    thr_unlock(&lock);
    status[0] = 0;
    if (i >= 0 && photos_on_insert) photos_on_insert(i);
    return i;
}

void photos_forget(int i) {
    if (i < 0 || i >= nphotos) return;
    thr_lock(&lock);
    folders[photos[i].folder].count--;
    memmove(&photos[i], &photos[i + 1], sizeof(Photo) * (nphotos - i - 1));
    nphotos--;
    thr_unlock(&lock);
}

/* ------------------------------------------------------------ thumbnails */

#define RAM_THUMBS 72
enum { T_EMPTY, T_WANTED, T_LOADING, T_READY, T_FAILED };
typedef struct {
    uint32_t key;
    int state;
    uint32_t stamp;        /* request priority: higher first */
    uint32_t used;         /* frame last drawn */
    char path[300];
    GfxImage img;
    /* details learned while making it (applied on the main thread) */
    bool have_info;
    uint16_t w, h;
    char date[20];
} Thumb;

static Thumb thumbs[RAM_THUMBS];
static uint32_t frame_no;
static thr_t worker;
static volatile bool worker_wake;

#define MAX_PICREQ 8
static PicRequest *picreqs[MAX_PICREQ];

static Thumb *thumb_find(uint32_t key) {
    for (int i = 0; i < RAM_THUMBS; i++) if (thumbs[i].state != T_EMPTY && thumbs[i].key == key) return &thumbs[i];
    return NULL;
}

static Thumb *thumb_slot(void) {
    Thumb *best = NULL;
    for (int i = 0; i < RAM_THUMBS; i++) {
        Thumb *t = &thumbs[i];
        if (t->state == T_EMPTY) return t;
        if (t->state == T_LOADING) continue;
        if (t->used >= frame_no) continue;     /* drawn this frame: keep */
        if (!best || t->used < best->used) best = t;
    }
    return best;
}

static GfxImage *request(int i, bool visible) {
    Photo *p = photos_get(i);
    if (!p) return NULL;
    thr_lock(&lock);
    Thumb *t = thumb_find(p->key);
    GfxImage *ret = NULL;
    if (t) {
        if (visible) t->used = frame_no;
        if (t->state == T_READY) ret = &t->img;
        else if (t->state == T_WANTED) {
            uint32_t s = frame_no * 2 + (visible ? 1 : 0);
            if (s > t->stamp) t->stamp = s;
        }
    } else {
        t = thumb_slot();
        if (t) {
            if (t->state == T_READY) gfx_image_free(&t->img);
            memset(t, 0, sizeof(*t));
            t->key = p->key;
            t->state = T_WANTED;
            t->stamp = frame_no * 2 + (visible ? 1 : 0);
            t->used = visible ? frame_no : 0;
            snprintf(t->path, sizeof(t->path), "%s", p->path);
            worker_wake = true;
        }
    }
    thr_unlock(&lock);
    return ret;
}

GfxImage *photos_thumb(int i) { return request(i, true); }
void photos_prefetch(int i) { request(i, false); }

bool photos_thumb_failed(int i) {
    Photo *p = photos_get(i);
    if (!p) return true;
    thr_lock(&lock);
    Thumb *t = thumb_find(p->key);
    bool failed = t && t->state == T_FAILED;
    thr_unlock(&lock);
    return failed;
}

int photos_pending(void) {
    int n = 0;
    thr_lock(&lock);
    for (int i = 0; i < RAM_THUMBS; i++) if (thumbs[i].state == T_WANTED || thumbs[i].state == T_LOADING) n++;
    for (int i = 0; i < MAX_PICREQ; i++) if (picreqs[i] && picreqs[i]->state == 1) n++;
    thr_unlock(&lock);
    return n;
}

static uint32_t last_index_save;

void photos_tick(void) {
    frame_no++;
    if (scanning) return;
    thr_lock(&lock);
    for (int i = 0; i < RAM_THUMBS; i++) {
        Thumb *t = &thumbs[i];
        if (!t->have_info) continue;
        t->have_info = false;
        for (int k = 0; k < nphotos; k++) {
            Photo *p = &photos[k];
            if (p->key != t->key) continue;
            p->w = t->w; p->h = t->h;
            snprintf(p->date, sizeof(p->date), "%s", t->date);
            p->info_known = true;
            int64_t when = parse_date(p->date);
            if (when && when != p->when) p->when = when;   /* the library re-sorts on the next scan */
            idx_dirty = true;
            break;
        }
    }
    thr_unlock(&lock);
    if (idx_dirty && frame_no - last_index_save > 300 && photos_pending() == 0) {
        last_index_save = frame_no;
        index_save();
    }
}

/* Thumbnail files: a 32-byte header, then the 160x120 texture. */
typedef struct { char magic[4]; uint16_t w, h; char date[20]; uint32_t pad; } ThumbHeader;

static bool thumb_file_read(uint32_t key, uint16_t *tex, ThumbHeader *hd) {
    char path[300];
    snprintf(path, sizeof(path), "%s/thumbs/%08x.t", DATA_DIR, (unsigned)key);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(hd, 1, sizeof(*hd), f) == sizeof(*hd) && !memcmp(hd->magic, "FCT1", 4) &&
              fread(tex, 1, THUMB_BYTES, f) == THUMB_BYTES;
    fclose(f);
    return ok;
}

static void thumb_file_write(uint32_t key, const uint16_t *tex, const ThumbHeader *hd) {
    char path[300];
    snprintf(path, sizeof(path), "%s/thumbs/%08x.t", DATA_DIR, (unsigned)key);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(hd, 1, sizeof(*hd), f);
    fwrite(tex, 1, THUMB_BYTES, f);
    fclose(f);
}

/* ARAM slots hold the header and texture together. */
#define SLOT_BYTES (((THUMB_BYTES + sizeof(ThumbHeader)) + 31) & ~31u)

static void make_thumb(Thumb *t, uint32_t key, const char *path) {
    unsigned char *slot = memalign(32, SLOT_BYTES);
    if (!slot) { thr_lock(&lock); t->state = T_FAILED; thr_unlock(&lock); return; }
    ThumbHeader *hd = (ThumbHeader *)slot;
    uint16_t *tex = memalign(32, THUMB_BYTES);
    bool ok = false, from_aram = false;
    if (tex) {
        if (aramcache_get(key, slot)) {
            memcpy(tex, slot + sizeof(ThumbHeader), THUMB_BYTES);
            ok = from_aram = true;
        } else if (thumb_file_read(key, tex, hd)) {
            ok = true;
        } else {
            PicInfo info;
            if (pic_info(path, &info) && pic_make_thumb(path, &info, tex)) {
                memset(hd, 0, sizeof(*hd));
                memcpy(hd->magic, "FCT1", 4);
                int w, h;
                pic_display_size(&info, &w, &h);
                hd->w = (uint16_t)w;
                hd->h = (uint16_t)h;
                snprintf(hd->date, sizeof(hd->date), "%s", info.date);
                thumb_file_write(key, tex, hd);
                ok = true;
            }
        }
        if (ok && !from_aram) {
            memcpy(slot + sizeof(ThumbHeader), tex, THUMB_BYTES);
            aramcache_put(key, slot);
        }
    }
    thr_lock(&lock);
    if (t->key == key && t->state == T_LOADING) {
        if (ok && gfx_image_init(&t->img, tex, THUMB_W, THUMB_H, THUMB_W, THUMB_H, GFX_RGB565)) {
            tex = NULL;   /* owned by the image now */
            t->state = T_READY;
            t->have_info = true;
            t->w = hd->w; t->h = hd->h;
            snprintf(t->date, sizeof(t->date), "%s", hd->date);
        } else {
            t->state = T_FAILED;
        }
    }
    thr_unlock(&lock);
    free(tex);
    free(slot);
}

static void do_picture(PicRequest *r) {
    for (;;) {
        thr_lock(&lock);
        int want = r->photo, side = r->max_side;
        Photo *p = photos_get(want);
        char path[300];
        snprintf(path, sizeof(path), "%s", p ? p->path : "");
        thr_unlock(&lock);
        Picture pic;
        char why[80] = "";
        bool ok = p && pic_load(path, side, &pic, why, sizeof(why));
        thr_lock(&lock);
        if (r->photo != want) {           /* asked for another meanwhile */
            if (ok) pic_free(&pic);
            if (r->photo < 0) { r->state = 0; thr_unlock(&lock); return; }
            thr_unlock(&lock);
            continue;
        }
        if (ok) { r->pic = pic; r->state = 2; }
        else { snprintf(r->why, sizeof(r->why), "%s", why[0] ? why : "This photo can't be opened."); r->state = 3; }
        thr_unlock(&lock);
        return;
    }
}

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        /* full pictures first, then the most wanted thumbnail */
        PicRequest *pr = NULL;
        Thumb *best = NULL;
        thr_lock(&lock);
        for (int i = 0; i < MAX_PICREQ; i++)
            if (picreqs[i] && picreqs[i]->state == 4) { pr = picreqs[i]; pr->state = 1; break; }
        if (!pr) {
            for (int i = 0; i < RAM_THUMBS; i++) {
                Thumb *t = &thumbs[i];
                if (t->state == T_WANTED && (!best || t->stamp > best->stamp)) best = t;
            }
            if (best) best->state = T_LOADING;
        }
        uint32_t key = best ? best->key : 0;
        char path[300];
        if (best) snprintf(path, sizeof(path), "%s", best->path);
        thr_unlock(&lock);
        if (pr) { do_picture(pr); continue; }
        if (best) { make_thumb(best, key, path); continue; }
        worker_wake = false;
        for (int i = 0; i < 50 && !worker_wake; i++) thr_sleep_ms(4);
    }
    return NULL;
}

void photos_init(void) {
    thr_mutex_init(&lock);
    aramcache_init(SLOT_BYTES);
    thr_create(&worker, worker_main, NULL, 128 * 1024, 40);
}

void photos_clear_cache(void) {
    aramcache_clear();
    DIR *d = opendir(DATA_DIR "/thumbs");
    if (!d) return;
    struct dirent *e;
    char path[300];
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/thumbs/%s", DATA_DIR, e->d_name);
        remove(path);
    }
    closedir(d);
}

void photos_load_picture(PicRequest *req, int photo, int max_side) {
    thr_lock(&lock);
    int slot = -1;
    for (int i = 0; i < MAX_PICREQ; i++) if (picreqs[i] == req) slot = i;
    if (slot < 0)
        for (int i = 0; i < MAX_PICREQ; i++) if (!picreqs[i]) { picreqs[i] = req; slot = i; break; }
    if (req->state == 1) {
        /* loading: the thread notices the new photo when it finishes */
        req->photo = photo;
        req->max_side = max_side;
        thr_unlock(&lock);
        return;
    }
    if (req->state == 2) pic_free(&req->pic);
    req->photo = photo;
    req->max_side = max_side;
    req->why[0] = 0;
    req->state = slot >= 0 ? 4 : 3;
    if (slot < 0) snprintf(req->why, sizeof(req->why), "Too many photos open at once.");
    worker_wake = true;
    thr_unlock(&lock);
}

void photos_release_picture(PicRequest *req) {
    thr_lock(&lock);
    if (req->state == 1) req->photo = -1;      /* the thread frees it (and keeps the slot until then) */
    else {
        if (req->state == 2) pic_free(&req->pic);
        req->state = 0;
        for (int i = 0; i < MAX_PICREQ; i++) if (picreqs[i] == req) picreqs[i] = NULL;
    }
    thr_unlock(&lock);
}

/* ------------------------------------------------------- for the server */

bool photos_path_for_key(uint32_t key, char *out, size_t cap) {
    bool ok = false;
    thr_lock(&lock);
    if (!scanning)
        for (int i = 0; i < nphotos; i++)
            if (photos[i].key == key) { snprintf(out, cap, "%s", photos[i].path); ok = true; break; }
    thr_unlock(&lock);
    return ok;
}

static size_t json_str(char *o, size_t cap, const char *s) {
    size_t n = 0;
    if (n < cap) o[n++] = '"';
    for (; *s && n + 7 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { o[n++] = '\\'; o[n++] = (char)c; }
        else if (c < 32) n += (size_t)snprintf(o + n, cap - n, "\\u%04x", c);
        else o[n++] = (char)c;
    }
    if (n < cap) o[n++] = '"';
    return n;
}

char *photos_json(size_t *len) {
    thr_lock(&lock);
    int count = scanning ? 0 : nphotos;
    size_t cap = (size_t)count * 200 + 256, n = 0;
    char *o = malloc(cap);
    if (!o) { thr_unlock(&lock); return NULL; }
    n += (size_t)snprintf(o + n, cap - n, "{\"count\":%d,\"photos\":[", count);
    for (int i = 0; i < count && n + 200 < cap; i++) {
        const Photo *p = &photos[i];
        char date[24];
        photos_format_date(p, date, sizeof(date));
        n += (size_t)snprintf(o + n, cap - n, "%s{\"k\":\"%08x\",\"n\":", i ? "," : "", (unsigned)p->key);
        n += json_str(o + n, cap - n, p->name);
        n += (size_t)snprintf(o + n, cap - n, ",\"f\":");
        n += json_str(o + n, cap - n, folders[p->folder].name);
        n += (size_t)snprintf(o + n, cap - n, ",\"y\":%d,\"d\":\"%s\",\"s\":%u,\"v\":%d}", photo_year(p), date,
                              (unsigned)p->size, p->fav ? 1 : 0);
    }
    n += (size_t)snprintf(o + n, cap - n, "]}");
    thr_unlock(&lock);
    *len = n;
    return o;
}

void photos_resort(void) {
    thr_lock(&lock);
    if (!scanning) sort_photos();
    thr_unlock(&lock);
}
