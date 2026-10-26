/* mcard.c - see mcard.h */
#include "mcard.h"
#include "meleesnap.h"
#include "pixels.h"
#include "photos.h"
#include "storage.h"
#include "thr.h"
#include "stb_image_write.h"
#include "applog.h"
#include "memstat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#ifdef GEKKO
#include <gccore.h>
#include <ogc/card.h>
#else
#include <dirent.h>
#endif

#define TW 160
#define TH 120
#define SECS_1970_TO_2000 946684800LL

typedef struct {
    McSnap pub;
    uint8_t *jpg;          /* the picture as Melee stored it */
    int jpg_len;
    uint16_t *thumb565;    /* made by the thread, turned into a texture by mcard_tick */
    int ttw, tth;
    int card_err;          /* a CARD_ERROR_ code if reading it from the card failed */
    char name[33];         /* its name on the card (for the log) */
} Snap;

static thr_mutex lock;
static bool lock_ok;
static Snap snaps[MCARD_MAX];
static int nsnaps, nreading;
static McState state = MC_IDLE;
static int slot = 0;
static char message[96];
static int gen;                     /* bumped by mcard_open/close so an old job's results are dropped */
static volatile bool busy;          /* a job is running */
static int want_open = -1;          /* a slot waiting for the running job to finish */
static bool saving, want_save;
static int save_done, save_total, last_saved;
static int nadded;                  /* saved files for the photo library */
static char added_path[MCARD_MAX][96];

static void ensure_lock(void) {
    if (!lock_ok) lock_ok = thr_mutex_init(&lock);
}

static void name_for(Snap *s) {
    time_t t = (time_t)s->pub.time;
    struct tm tm;
    gmtime_r(&t, &tm);   /* the GameCube's clock is local time already */
    static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    snprintf(s->pub.file, sizeof(s->pub.file), "Melee_%04d-%02d-%02d_%02d%02d%02d.png", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    snprintf(s->pub.when, sizeof(s->pub.when), "%d %s  %02d:%02d", tm.tm_mday, mon[tm.tm_mon], tm.tm_hour, tm.tm_min);
}

static bool on_sd(const Snap *s) {
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", MCARD_DIR, s->pub.file);
    return storage_is_file(path);
}

/* the picture, made smaller for the list as it's decoded: 4x4 pixels averaged */
typedef struct { uint16_t *t; int tw; } ThumbCtx;
static void thumb_rows(void *ctx, int y, const uint8_t *band) {
    ThumbCtx *c = ctx;
    for (int ty = 0; ty < 4; ty++)
        for (int x = 0; x < TW; x++) {
            int r = 0, g = 0, b = 0;
            for (int j = 0; j < 4; j++) {
                const uint8_t *p = band + ((size_t)(ty * 4 + j) * MSNAP_W + x * 4) * 3;
                for (int i = 0; i < 4; i++, p += 3) { r += p[0]; g += p[1]; b += p[2]; }
            }
            c->t[tex_index(c->tw, x, y / 4 + ty)] = rgb565(r >> 4, g >> 4, b >> 4);
        }
}

/* ---- reading the card ---- */
typedef struct { char name[33]; uint8_t code[4], company[2]; } Entry;
static void mcard_found(Snap *s, int my_gen);

#ifdef GEKKO
static uint8_t workarea[CARD_WORKAREA] __attribute__((aligned(32)));
static bool card_inited;

/* lists the snapshots and reads each one; returns a CARD_ERROR_ code */
static int read_card(int chn, int my_gen) {
    if (!card_inited) { CARD_Init("FLSH", "00"); card_inited = true; }
    s32 mem = 0, sect = 0;
    int r = CARD_ProbeEx(chn, &mem, &sect);
    if (r == CARD_ERROR_BUSY) { thr_sleep_ms(300); r = CARD_ProbeEx(chn, &mem, &sect); }
    if (r < 0) return r;
    if (sect <= 0) sect = 8192;
    r = CARD_Mount(chn, workarea, NULL);
    if (r < 0 && r != CARD_ERROR_ENCODING) { CARD_Unmount(chn); return r; }
    static Entry list[MCARD_MAX];
    int n = 0;
    card_dir d;
    memset(&d, 0, sizeof(d));
    r = CARD_FindFirst(chn, &d, true);
    while (r == CARD_ERROR_READY && n < MCARD_MAX) {
        char name[33];
        memcpy(name, d.filename, 32);
        name[32] = 0;
        if (msnap_is_snapshot(name, d.gamecode, d.company)) {
            memcpy(list[n].name, name, 33);
            memcpy(list[n].code, d.gamecode, 4);
            memcpy(list[n].company, d.company, 2);
            n++;
        }
        r = CARD_FindNext(&d);
    }
    thr_lock(&lock);
    if (my_gen == gen) nreading = n;
    thr_unlock(&lock);
    for (int i = 0; i < n; i++) {
        thr_lock(&lock);
        bool stale = my_gen != gen;
        thr_unlock(&lock);
        if (stale) break;
        card_dir e;
        memset(&e, 0, sizeof(e));
        memcpy(e.filename, list[i].name, 32);
        memcpy(e.gamecode, list[i].code, 4);
        memcpy(e.company, list[i].company, 2);
        card_file f;
        Snap s;
        memset(&s, 0, sizeof(s));
        memcpy(s.name, list[i].name, 33);
        int r2 = CARD_OpenEntry(chn, &e, &f);
        if (r2 == CARD_ERROR_READY) {
            card_stat st;
            if (CARD_GetStatus(chn, f.filenum, &st) == CARD_ERROR_READY) s.pub.time = (int64_t)st.time + SECS_1970_TO_2000;
            int len = f.len;
            uint8_t *buf = len > 0 ? memalign(32, (size_t)len) : NULL;
            if (!buf) s.jpg_len = MSNAP_X_MEMORY;
            /* a sector at a time (as the GameCube's own memory card screen does) */
            for (int off = 0; buf && off < len && r2 == CARD_ERROR_READY; off += sect) {
                r2 = CARD_Read(&f, buf + off, (u32)sect, (u32)off);
                if (r2 == CARD_ERROR_BUSY) { thr_sleep_ms(50); r2 = CARD_Read(&f, buf + off, (u32)sect, (u32)off); }
            }
            if (buf && r2 == CARD_ERROR_READY) s.jpg_len = msnap_extract(buf, len, &s.jpg);
            else if (buf) s.card_err = r2;
            free(buf);
            CARD_Close(&f);
        } else {
            s.card_err = r2;
        }
        mcard_found(&s, my_gen);
    }
    CARD_Unmount(chn);
    return CARD_ERROR_READY;
}
#else
/* PC tests: the memory cards are folders of .gci files, "memcard_a" and "memcard_b" */
#define CARD_ERROR_READY 0
#define CARD_ERROR_NOCARD -3
static int read_card(int chn, int my_gen) {
    const char *dir = chn ? "memcard_b" : "memcard_a";
    DIR *dp = opendir(dir);
    if (!dp) return CARD_ERROR_NOCARD;
    static char paths[MCARD_MAX][300];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(dp)) && n < MCARD_MAX) {
        const char *dot = strrchr(de->d_name, '.');
        if (dot && !strcmp(dot, ".gci")) snprintf(paths[n++], sizeof(paths[0]), "%s/%s", dir, de->d_name);
    }
    closedir(dp);
    int m = 0;
    for (int i = 0; i < n; i++) {
        FILE *f = fopen(paths[i], "rb");
        uint8_t h[64];
        if (!f) continue;
        if (fread(h, 1, 64, f) == 64) {
            char name[33];
            memcpy(name, h + 8, 32);
            name[32] = 0;
            if (msnap_is_snapshot(name, h, h + 4)) memmove(paths[m++], paths[i], sizeof(paths[0]));
        }
        fclose(f);
    }
    thr_lock(&lock);
    if (my_gen == gen) nreading = m;
    thr_unlock(&lock);
    for (int i = 0; i < m; i++) {
        size_t len = 0;
        uint8_t *d = (uint8_t *)storage_read(paths[i], &len);
        Snap s;
        memset(&s, 0, sizeof(s));
        const char *base = strrchr(paths[i], '/');
        snprintf(s.name, sizeof(s.name), "%.32s", base ? base + 1 : paths[i]);
        if (d && len > 64) {
            s.pub.time = (int64_t)((uint32_t)d[0x28] << 24 | d[0x29] << 16 | d[0x2a] << 8 | d[0x2b]) + SECS_1970_TO_2000;
            s.jpg_len = msnap_extract(d + 64, (int)len - 64, &s.jpg);
        }
        free(d);
        thr_sleep_ms(120);   /* a real card takes a moment */
        mcard_found(&s, my_gen);
    }
    return CARD_ERROR_READY;
}
#endif

/* one snapshot read: make its thumbnail and add it to the list */
static void mcard_found(Snap *s, int my_gen) {
    int err = MSNAP_OK;
    if (s->card_err) {
        snprintf(s->pub.why, sizeof(s->pub.why), "Card error %d", s->card_err);
    } else if (s->jpg_len <= 0) {
        static const char *x[] = { "", "Too small", "Out of memory", "Not a Melee snapshot", "Part is missing", "Not a picture" };
        int k = -s->jpg_len;
        snprintf(s->pub.why, sizeof(s->pub.why), "%s", k >= 1 && k <= 5 ? x[k] : "Can't be read");
    } else {
        ThumbCtx c = { tex_alloc565(TW, TH, &s->ttw, &s->tth), 0 };
        c.tw = s->ttw;
        if (!c.t) err = MSNAP_NO_MEMORY;
        else err = msnap_decode_rows(s->jpg, s->jpg_len, thumb_rows, &c);
        if (err == MSNAP_OK) s->thumb565 = c.t;
        else {
            free(c.t);
            snprintf(s->pub.why, sizeof(s->pub.why), "%s", err == MSNAP_NO_MEMORY ? "Out of memory" : err == MSNAP_NOT_PICTURE ? "Not a picture" : "Picture damaged");
        }
    }
    s->pub.bad = s->pub.why[0] != 0;
    if (s->pub.bad)
        app_log_later("Melee snapshot %s: %s (card %d, data %d, picture %d, %u KB free)", s->name, s->pub.why, s->card_err,
                      s->jpg_len, err, (unsigned)(cb_mem_free() / 1024));
    name_for(s);
    s->pub.saved = !s->pub.bad && on_sd(s);
    thr_lock(&lock);
    if (my_gen == gen && nsnaps < MCARD_MAX) {
        /* newest first */
        int at = 0;
        while (at < nsnaps && snaps[at].pub.time >= s->pub.time) at++;
        memmove(&snaps[at + 1], &snaps[at], sizeof(Snap) * (nsnaps - at));
        snaps[at] = *s;
        nsnaps++;
        if (nreading > 0) nreading--;
    } else {
        free(s->jpg);
        free(s->thumb565);
    }
    thr_unlock(&lock);
}

/* ---- saving ---- */
typedef struct { uint8_t *buf; size_t len, cap; bool fail; } Mem;
static void mem_write(void *ctx, void *data, int size) {
    Mem *m = ctx;
    if (m->fail) return;
    if (m->len + size > m->cap) {
        size_t c = m->cap ? m->cap * 2 : 512 * 1024;
        while (c < m->len + size) c *= 2;
        uint8_t *n = realloc(m->buf, c);
        if (!n) { m->fail = true; return; }
        m->buf = n;
        m->cap = c;
    }
    memcpy(m->buf + m->len, data, size);
    m->len += size;
}

static bool save_one(const uint8_t *jpg, int len, const char *file, char *path_out, size_t cap) {
    uint8_t *rgb = malloc(MSNAP_W * MSNAP_H * 3);
    if (!rgb) return false;
    bool ok = msnap_decode(jpg, len, rgb) == MSNAP_OK;
    Mem m = { 0 };
    if (ok) ok = stbi_write_png_to_func(mem_write, &m, MSNAP_W, MSNAP_H, 3, rgb, MSNAP_W * 3) && !m.fail;
    free(rgb);
    if (ok) {
        storage_mkdirs(MCARD_DIR);
        snprintf(path_out, cap, "%s/%s", MCARD_DIR, file);
        ok = storage_write_atomic(path_out, m.buf, m.len);
    }
    free(m.buf);
    return ok;
}

static void do_save(int my_gen) {
    for (;;) {
        /* the next one to save (copy its data out, so the list can change meanwhile) */
        thr_lock(&lock);
        int pick = -1;
        for (int i = 0; i < nsnaps && my_gen == gen; i++)
            if (!snaps[i].pub.saved && !snaps[i].pub.bad && snaps[i].jpg) { pick = i; break; }
        uint8_t *jpg = NULL;
        int len = 0;
        char file[64];
        if (pick >= 0) {
            len = snaps[pick].jpg_len;
            jpg = malloc(len);
            if (jpg) memcpy(jpg, snaps[pick].jpg, len);
            snprintf(file, sizeof(file), "%s", snaps[pick].pub.file);
        }
        thr_unlock(&lock);
        if (pick < 0 || !jpg) { free(jpg); break; }
        char path[96];
        bool ok = save_one(jpg, len, file, path, sizeof(path));
        if (!ok) app_log_later("Melee snapshot %s couldn't be saved (%u KB free)", file, (unsigned)(cb_mem_free() / 1024));
        free(jpg);
        thr_lock(&lock);
        for (int i = 0; i < nsnaps && my_gen == gen; i++)
            if (!strcmp(snaps[i].pub.file, file)) {
                if (ok) snaps[i].pub.saved = true;
                else snaps[i].pub.bad = true;   /* don't try it again */
            }
        save_done++;
        if (ok) {
            last_saved++;
            if (nadded < MCARD_MAX) snprintf(added_path[nadded++], sizeof(added_path[0]), "%s", path);
        }
        thr_unlock(&lock);
    }
}

/* ---- the thread ---- */
static void *job_main(void *arg) {
    (void)arg;
    for (;;) {
        thr_lock(&lock);
        int open_slot = want_open, my_gen = gen;
        bool do_s = want_save;
        want_open = -1;
        want_save = false;
        if (open_slot < 0 && !do_s) { busy = false; thr_unlock(&lock); break; }
        thr_unlock(&lock);
        if (open_slot >= 0) {
            int r = read_card(open_slot, my_gen);
            thr_lock(&lock);
            if (my_gen == gen) {
                nreading = 0;
                if (r == CARD_ERROR_READY) state = MC_READY;
                else if (r == CARD_ERROR_NOCARD || r == -2 /* wrong device */) state = MC_NO_CARD;
                else {
                    state = MC_ERROR;
                    snprintf(message, sizeof(message), r == -6 || r == -13 ? "The memory card seems damaged or isn't formatted."
                                                                            : "The memory card couldn't be read (error %d).", r);
                }
            }
            thr_unlock(&lock);
        }
        if (do_s) {
            do_save(my_gen);
            thr_lock(&lock);
            saving = false;
            thr_unlock(&lock);
        }
    }
    return NULL;
}

static void start_thread(void) {
    if (busy) return;   /* the running job picks the request up */
    busy = true;
    thr_t t;
    if (thr_create(&t, job_main, NULL, 64 * 1024, 45)) thr_detach(t);
    else busy = false;
}

static void clear_list(void) {
    for (int i = 0; i < nsnaps; i++) {
        free(snaps[i].jpg);
        free(snaps[i].thumb565);
        if (snaps[i].pub.thumb_ok) gfx_image_free(&snaps[i].pub.thumb);
    }
    nsnaps = 0;
    nreading = 0;
}

void mcard_open(int s) {
    ensure_lock();
    thr_lock(&lock);
    gen++;
    clear_list();
    slot = s;
    message[0] = 0;
    saving = want_save = false;
    if (!strcmp(storage_device_name(), s ? "SD Gecko (slot B)" : "SD Gecko (slot A)")) {
        state = MC_SD_SLOT;
        thr_unlock(&lock);
        return;
    }
    state = MC_LOOKING;
    want_open = s;
    thr_unlock(&lock);
    start_thread();
}

void mcard_close(void) {
    if (!lock_ok) return;
    thr_lock(&lock);
    gen++;
    clear_list();
    state = MC_IDLE;
    saving = want_save = false;
    want_open = -1;
    thr_unlock(&lock);
}

McState mcard_state(void) { return state; }
int mcard_slot(void) { return slot; }
const char *mcard_message(void) { return message; }
int mcard_count(void) { return nsnaps; }
int mcard_reading(void) { return nreading; }
McSnap *mcard_get(int i) { return i >= 0 && i < nsnaps ? &snaps[i].pub : NULL; }

int mcard_unsaved(void) {
    int n = 0;
    for (int i = 0; i < nsnaps; i++) if (!snaps[i].pub.saved && !snaps[i].pub.bad) n++;
    return n;
}

void mcard_save_new(void) {
    ensure_lock();
    thr_lock(&lock);
    if (saving || state != MC_READY) { thr_unlock(&lock); return; }
    save_total = 0;
    for (int i = 0; i < nsnaps; i++) if (!snaps[i].pub.saved && !snaps[i].pub.bad) save_total++;
    save_done = last_saved = 0;
    if (save_total == 0) { thr_unlock(&lock); return; }
    saving = want_save = true;
    thr_unlock(&lock);
    start_thread();
}

bool mcard_saving(int *done, int *total) {
    if (done) *done = save_done;
    if (total) *total = save_total;
    return saving;
}

int mcard_just_saved(void) { return last_saved; }

void mcard_tick(void) {
    if (!lock_ok) return;
    thr_lock(&lock);
    for (int i = 0; i < nsnaps; i++) {
        Snap *s = &snaps[i];
        if (s->thumb565 && !s->pub.thumb_ok) {
            s->pub.thumb_ok = gfx_image_init(&s->pub.thumb, s->thumb565, TW, TH, s->ttw, s->tth, GFX_RGB565);
            if (s->pub.thumb_ok) s->thumb565 = NULL;   /* the texture owns it now */
            else { free(s->thumb565); s->thumb565 = NULL; }
        }
    }
    char paths[MCARD_MAX][96];
    int n = nadded;
    memcpy(paths, added_path, sizeof(paths[0]) * n);
    nadded = 0;
    thr_unlock(&lock);
    for (int i = 0; i < n; i++) photos_add_file(paths[i]);
}
