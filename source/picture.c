/* picture.c - see picture.h. */
#include "picture.h"
#include "pixels.h"
#include "tjpgd.h"
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stb_image for the formats TJpgDec can't do. No thread-local storage:
 * __thread is broken on libogc threads (it flipped CubeBrowser's pictures). */
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifndef GEKKO
#define DCFlushRange(a, b) ((void)(a), (void)(b))
#endif

uint16_t *tex_alloc565(int w, int h, int *tex_w, int *tex_h) {
    int tw = tex_round(w), th = tex_round(h);
    size_t bytes = (size_t)tw * th * 2;
    uint16_t *t = memalign(32, (bytes + 31) & ~(size_t)31);
    if (!t) return NULL;
    memset(t, 0, bytes);
    if (tex_w) *tex_w = tw;
    if (tex_h) *tex_h = th;
    return t;
}

/* ------------------------------------------------------------- file info */

static uint16_t rd16(const uint8_t *p, bool le) { return le ? (uint16_t)(p[0] | p[1] << 8) : (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p, bool le) {
    return le ? (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24)
              : (uint32_t)((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]);
}

static void copy_ascii(char *out, size_t cap, const uint8_t *s, uint32_t n) {
    size_t k = 0;
    for (uint32_t i = 0; i < n && k + 1 < cap; i++) {
        if (!s[i]) break;
        out[k++] = (s[i] >= 32 && s[i] < 127) ? (char)s[i] : ' ';
    }
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
}

/* Parses the EXIF block (a TIFF structure) at tiff[0..len). */
static void parse_exif(const uint8_t *tiff, uint32_t len, uint32_t file_off, PicInfo *info) {
    if (len < 8) return;
    bool le = tiff[0] == 'I';
    if (!(tiff[0] == 'I' && tiff[1] == 'I') && !(tiff[0] == 'M' && tiff[1] == 'M')) return;
    uint32_t ifd = rd32(tiff + 4, le);
    uint32_t exif_ifd = 0;
    for (int which = 0; which < 3 && ifd && ifd + 2 <= len; which++) {
        int n = rd16(tiff + ifd, le);
        if (n > 500 || ifd + 2 + (uint32_t)n * 12 > len) break;
        uint32_t thumb_off = 0, thumb_len = 0;
        for (int i = 0; i < n; i++) {
            const uint8_t *e = tiff + ifd + 2 + i * 12;
            uint16_t tag = rd16(e, le), type = rd16(e + 2, le);
            uint32_t count = rd32(e + 4, le);
            uint32_t val = type == 3 ? rd16(e + 8, le) : rd32(e + 8, le);
            if (which == 0) {           /* IFD0 */
                if (tag == 0x0112 && val >= 1 && val <= 8) info->orientation = (int)val;
                else if (tag == 0x0110 && type == 2) {
                    if (count <= 4) copy_ascii(info->camera, sizeof(info->camera), e + 8, count);
                    else if (val + count <= len) copy_ascii(info->camera, sizeof(info->camera), tiff + val, count);
                } else if (tag == 0x8769) exif_ifd = val;
            } else if (which == 1) {    /* IFD1: the thumbnail */
                if (tag == 0x0201) thumb_off = val;
                else if (tag == 0x0202) thumb_len = val;
            } else {                    /* Exif IFD */
                if ((tag == 0x9003 || (tag == 0x9004 && !info->date[0])) && type == 2 && val + 19 <= len)
                    copy_ascii(info->date, sizeof(info->date), tiff + val, 19);
            }
        }
        if (which == 1 && thumb_off && thumb_len && thumb_off + thumb_len <= len) {
            info->thumb_off = file_off + thumb_off;
            info->thumb_len = thumb_len;
        }
        if (which == 0) {
            uint32_t next = rd32(tiff + ifd + 2 + n * 12, le);
            ifd = next;
            if (!next) { which = 1; ifd = exif_ifd; }   /* no IFD1: go to the Exif IFD */
        } else if (which == 1) {
            ifd = exif_ifd;
        } else {
            break;
        }
    }
}

bool pic_info(const char *path, PicInfo *info) {
    memset(info, 0, sizeof(*info));
    info->orientation = 1;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t head[32];
    size_t n = fread(head, 1, sizeof(head), f);
    bool ok = false;
    if (n >= 4 && head[0] == 0xFF && head[1] == 0xD8) {
        /* walk the JPEG's segments up to the frame header; the EXIF block
         * (APP1) is read whole, everything else skipped */
        info->is_jpeg = true;
        long at = 2;
        for (int guard = 0; guard < 200; guard++) {
            uint8_t m[4];
            if (fseek(f, at, SEEK_SET) != 0 || fread(m, 1, 4, f) != 4) break;
            if (m[0] != 0xFF) break;
            if (m[1] == 0xFF) { at++; continue; }
            if (m[1] == 0x01 || (m[1] >= 0xD0 && m[1] <= 0xD8)) { at += 2; continue; }
            uint32_t seglen = (uint32_t)(m[2] << 8 | m[3]);
            if (seglen < 2 || m[1] == 0xDA || m[1] == 0xD9) break;
            if (m[1] == 0xE1 && seglen > 8 && !info->thumb_off && !info->orientation_set) {
                uint8_t *d = malloc(seglen);
                if (d && fread(d, 1, seglen - 2, f) == seglen - 2 && !memcmp(d, "Exif\0\0", 6)) {
                    parse_exif(d + 6, seglen - 2 - 6, (uint32_t)(at + 4 + 6), info);
                    info->orientation_set = true;
                }
                free(d);
            } else if (m[1] >= 0xC0 && m[1] <= 0xCF && m[1] != 0xC4 && m[1] != 0xC8 && m[1] != 0xCC) {
                uint8_t sof[5];
                if (fread(sof, 1, 5, f) == 5) {
                    info->height = sof[1] << 8 | sof[2];
                    info->width = sof[3] << 8 | sof[4];
                    info->progressive = (m[1] == 0xC2 || m[1] == 0xC6 || m[1] == 0xCA || m[1] == 0xCE);
                    ok = info->width > 0 && info->height > 0;
                }
                break;
            }
            at += 2 + seglen;
        }
    } else if (n >= 24 && !memcmp(head, "\x89PNG", 4)) {
        info->width = (int)rd32(head + 16, false);
        info->height = (int)rd32(head + 20, false);
        ok = true;
    } else if (n >= 26 && head[0] == 'B' && head[1] == 'M') {
        info->width = (int)rd32(head + 18, true);
        int h = (int)rd32(head + 22, true);
        info->height = h < 0 ? -h : h;
        ok = true;
    } else if (n >= 10 && !memcmp(head, "GIF8", 4)) {
        info->width = rd16(head + 6, true);
        info->height = rd16(head + 8, true);
        ok = true;
    }
    fclose(f);
    if (info->width <= 0 || info->height <= 0 || info->width > 65535 || info->height > 65535) ok = false;
    return ok;
}

void pic_display_size(const PicInfo *info, int *w, int *h) {
    bool swap = info->orientation >= 5;
    *w = swap ? info->height : info->width;
    *h = swap ? info->width : info->height;
}

/* Where source pixel (x,y) of a w x h picture lands once turned upright. */
static inline void orient(int o, int x, int y, int w, int h, int *ox, int *oy) {
    switch (o) {
    case 2: *ox = w - 1 - x; *oy = y; break;
    case 3: *ox = w - 1 - x; *oy = h - 1 - y; break;
    case 4: *ox = x; *oy = h - 1 - y; break;
    case 5: *ox = y; *oy = x; break;
    case 6: *ox = h - 1 - y; *oy = x; break;
    case 7: *ox = h - 1 - y; *oy = w - 1 - x; break;
    case 8: *ox = y; *oy = w - 1 - x; break;
    default: *ox = x; *oy = y; break;
    }
}

/* ------------------------------------------------------- JPEG decoding */

typedef struct Job Job;
/* Gets decoded pixels: a block of rgb (bw x bh) at (x, y) of the
 * (possibly shrunk) picture. Returns false to stop. */
typedef bool (*BlockFn)(Job *job, int x, int y, int bw, int bh, const uint8_t *rgb);

struct Job {
    FILE *f;
    const uint8_t *mem;      /* or decoding from memory (EXIF thumbnail) */
    size_t mem_len, mem_pos;
    uint32_t file_limit;     /* bytes left to read when decoding a part of a file */
    BlockFn block;
    int sw, sh;              /* size after TJpgDec's shrinking */
    int orientation;
    /* thumbnail accumulation */
    uint32_t *sum;
    uint16_t *cnt;
    int gw, gh, gx0, gy0, gf;   /* grid size, crop origin, shrink factor */
    int dw, dh;
    float area_aspect;       /* use only the middle of this shape (0 = all) */
    /* full picture */
    Picture *pic;
    float ratio;             /* extra shrink beyond TJpgDec's (1 = none) */
};

static size_t jd_in(JDEC *jd, uint8_t *buf, size_t n) {
    Job *job = (Job *)jd->device;
    if (job->mem) {
        size_t left = job->mem_len - job->mem_pos;
        if (n > left) n = left;
        if (buf) memcpy(buf, job->mem + job->mem_pos, n);
        job->mem_pos += n;
        return n;
    }
    if (job->file_limit && n > job->file_limit) n = job->file_limit;
    if (!buf) {
        if (fseek(job->f, (long)n, SEEK_CUR) != 0) return 0;
        if (job->file_limit) job->file_limit -= (uint32_t)n;
        return n;
    }
    size_t got = fread(buf, 1, n, job->f);
    if (job->file_limit) job->file_limit -= (uint32_t)got;
    return got;
}

static int jd_out(JDEC *jd, void *bitmap, JRECT *r) {
    Job *job = (Job *)jd->device;
    return job->block(job, r->left, r->top, r->right - r->left + 1, r->bottom - r->top + 1, bitmap) ? 1 : 0;
}

/* Prepares TJpgDec for the job. Returns its object (and pool) or NULL. */
static JRESULT jd_run(Job *job, int max_side, bool for_thumb, int *scale_out, int *w_out, int *h_out,
                      bool (*setup)(Job *, int w, int h, int scale)) {
    /* TJpgDec's work area: its tables plus the input buffer (JD_SZBUF) */
    size_t pool_size = TJPGD_WORKSPACE_SIZE + JD_SZBUF + 4096;
    void *pool = malloc(pool_size);
    if (!pool) return JDR_MEM1;
    JDEC jd;
    JRESULT r = jd_prepare(&jd, jd_in, pool, pool_size, job);
    if (r == JDR_OK) {
        int w = jd.width, h = jd.height, longest = w > h ? w : h;
        int s = 0;
        if (for_thumb) {
            /* smallest picture still at least twice the thumbnail */
            while (s < 3 && (w >> (s + 1)) >= THUMB_W * 2 && (h >> (s + 1)) >= THUMB_H * 2) s++;
        } else {
            while (s < 3 && (longest >> s) > max_side) s++;
        }
        if (w_out) *w_out = w;
        if (h_out) *h_out = h;
        if (scale_out) *scale_out = s;
        job->sw = w >> s;
        job->sh = h >> s;
        if (job->sw < 1 || job->sh < 1 || !setup(job, w, h, s)) r = JDR_MEM1;
        else r = jd_decomp(&jd, jd_out, (uint8_t)s);
    }
    free(pool);
    return r;
}

/* ------------------------------------------------------------ thumbnails */

/* Sets up the accumulation grid: the middle 4:3 of the upright picture,
 * shrunk by a whole factor to at most about twice the thumbnail. */
static bool thumb_grid(Job *job, int dw, int dh) {
    job->dw = dw; job->dh = dh;
    /* the area to use: all of it, or the photo's shape in the middle of a
     * preview that the camera padded with black bars */
    int ax = 0, ay = 0, aw = dw, ah = dh;
    if (job->area_aspect > 0) {
        float a = (float)dw / dh;
        if (fabsf(a - job->area_aspect) / job->area_aspect > 0.04f) {
            if (a > job->area_aspect) { aw = (int)(dh * job->area_aspect); ax = (dw - aw) / 2; }
            else { ah = (int)(dw / job->area_aspect); ay = (dh - ah) / 2; }
        }
    }
    int cw, ch;
    if ((long)aw * 3 > (long)ah * 4) { ch = ah; cw = ah * 4 / 3; }
    else { cw = aw; ch = aw * 3 / 4; }
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    job->gx0 = ax + (aw - cw) / 2;
    job->gy0 = ay + (ah - ch) / 2;
    int f = cw / THUMB_W < ch / THUMB_H ? cw / THUMB_W : ch / THUMB_H;
    if (f < 1) f = 1;
    job->gf = f;
    job->gw = (cw + f - 1) / f;
    job->gh = (ch + f - 1) / f;
    job->sum = calloc((size_t)job->gw * job->gh * 3, sizeof(uint32_t));
    job->cnt = calloc((size_t)job->gw * job->gh, sizeof(uint16_t));
    return job->sum && job->cnt;
}

static bool thumb_setup(Job *job, int w, int h, int s) {
    (void)w; (void)h; (void)s;
    int dw = job->orientation >= 5 ? job->sh : job->sw;
    int dh = job->orientation >= 5 ? job->sw : job->sh;
    return thumb_grid(job, dw, dh);
}

static bool thumb_block(Job *job, int x, int y, int bw, int bh, const uint8_t *rgb) {
    for (int j = 0; j < bh; j++) {
        for (int i = 0; i < bw; i++) {
            int sx = x + i, sy = y + j;
            if (sx >= job->sw || sy >= job->sh) continue;
            int ox, oy;
            orient(job->orientation, sx, sy, job->sw, job->sh, &ox, &oy);
            int gx = (ox - job->gx0) / job->gf, gy = (oy - job->gy0) / job->gf;
            if (ox < job->gx0 || oy < job->gy0 || gx >= job->gw || gy >= job->gh) continue;
            int k = gy * job->gw + gx;
            const uint8_t *p = rgb + (j * bw + i) * 3;
            job->sum[k * 3] += p[0];
            job->sum[k * 3 + 1] += p[1];
            job->sum[k * 3 + 2] += p[2];
            if (job->cnt[k] < 65535) job->cnt[k]++;
        }
    }
    return true;
}

/* Grid -> 160x120: averages the grid cells each thumbnail pixel covers
 * (or, when the grid is smaller, blends the nearest ones). */
static void thumb_finish(Job *job, uint16_t *out) {
    int gw = job->gw, gh = job->gh;
    /* average each cell; empty cells borrow from a neighbour */
    float *g = malloc((size_t)gw * gh * 3 * sizeof(float));
    if (!g) return;
    for (int k = 0; k < gw * gh; k++) {
        int c = job->cnt[k];
        if (!c) {
            int kk = k > 0 ? k - 1 : (k + 1 < gw * gh ? k + 1 : k);
            c = job->cnt[kk];
            for (int t = 0; t < 3; t++) g[k * 3 + t] = c ? (float)job->sum[kk * 3 + t] / c : 0;
            continue;
        }
        for (int t = 0; t < 3; t++) g[k * 3 + t] = (float)job->sum[k * 3 + t] / c;
    }
    float kx = (float)gw / THUMB_W, ky = (float)gh / THUMB_H;
    for (int ty = 0; ty < THUMB_H; ty++) {
        for (int tx = 0; tx < THUMB_W; tx++) {
            float c[3] = { 0, 0, 0 };
            if (kx >= 1.0f && ky >= 1.0f) {
                float x0 = tx * kx, x1 = x0 + kx, y0 = ty * ky, y1 = y0 + ky, wsum = 0;
                for (int yy = (int)y0; yy < (int)ceilf(y1) && yy < gh; yy++) {
                    float wy = fminf(y1, yy + 1) - fmaxf(y0, yy);
                    for (int xx = (int)x0; xx < (int)ceilf(x1) && xx < gw; xx++) {
                        float wx = fminf(x1, xx + 1) - fmaxf(x0, xx), wgt = wx * wy;
                        if (wgt <= 0) continue;
                        const float *p = g + (yy * gw + xx) * 3;
                        c[0] += p[0] * wgt; c[1] += p[1] * wgt; c[2] += p[2] * wgt;
                        wsum += wgt;
                    }
                }
                if (wsum > 0) { c[0] /= wsum; c[1] /= wsum; c[2] /= wsum; }
            } else {
                float fx = (tx + 0.5f) * kx - 0.5f, fy = (ty + 0.5f) * ky - 0.5f;
                if (fx < 0) fx = 0;
                if (fy < 0) fy = 0;
                int x0 = (int)fx, y0 = (int)fy;
                int x1 = x0 + 1 < gw ? x0 + 1 : x0, y1 = y0 + 1 < gh ? y0 + 1 : y0;
                float ax = fx - x0, ay = fy - y0;
                for (int t = 0; t < 3; t++) {
                    float a = g[(y0 * gw + x0) * 3 + t] * (1 - ax) + g[(y0 * gw + x1) * 3 + t] * ax;
                    float b = g[(y1 * gw + x0) * 3 + t] * (1 - ax) + g[(y1 * gw + x1) * 3 + t] * ax;
                    c[t] = a * (1 - ay) + b * ay;
                }
            }
            out[tex_index(THUMB_W, tx, ty)] = rgb565((int)(c[0] + 0.5f), (int)(c[1] + 0.5f), (int)(c[2] + 0.5f));
        }
    }
    free(g);
}

/* stb_image path: the whole picture decoded into memory (smaller files). */
static uint8_t *stb_decode(const char *path, int *w, int *h, size_t max_pixels) {
    size_t len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (l <= 0 || l > 12 * 1024 * 1024) { fclose(f); return NULL; }
    len = (size_t)l;
    uint8_t *data = malloc(len);
    if (!data) { fclose(f); return NULL; }
    size_t got = fread(data, 1, len, f);
    fclose(f);
    int c;
    if (!stbi_info_from_memory(data, (int)got, w, h, &c) || (size_t)*w * *h > max_pixels) { free(data); return NULL; }
    uint8_t *rgb = stbi_load_from_memory(data, (int)got, w, h, &c, 3);
    free(data);
    return rgb;
}

/* Feeds a whole RGB picture through a block function, a row at a time. */
static void feed_rows(Job *job, const uint8_t *rgb, int w, int h) {
    for (int y = 0; y < h; y++) job->block(job, 0, y, w, 1, rgb + (size_t)y * w * 3);
}

static bool thumb_from_jpeg(Job *job, int orientation, uint16_t *out) {
    job->orientation = orientation;
    job->block = thumb_block;
    JRESULT r = jd_run(job, 0, true, NULL, NULL, NULL, thumb_setup);
    if (r == JDR_OK) thumb_finish(job, out);
    free(job->sum); free(job->cnt);
    job->sum = NULL; job->cnt = NULL;
    return r == JDR_OK;
}

bool pic_make_thumb(const char *path, const PicInfo *info, uint16_t *out) {
    Job job;
    memset(&job, 0, sizeof(job));
    int o = info->orientation;
    if (info->is_jpeg && info->thumb_off && info->thumb_len > 200 && info->thumb_len < 512 * 1024) {
        /* The camera's own small preview: far quicker than the photo. Some
         * cameras pad it with black bars to 4:3, so only the photo's own
         * shape in its middle is used. Too small a preview isn't used. */
        FILE *f = fopen(path, "rb");
        if (f) {
            uint8_t *t = malloc(info->thumb_len);
            bool ok = false;
            if (t && fseek(f, (long)info->thumb_off, SEEK_SET) == 0 && fread(t, 1, info->thumb_len, f) == info->thumb_len) {
                int pw, ph;
                pic_display_size(info, &pw, &ph);
                job.mem = t;
                job.mem_len = info->thumb_len;
                job.area_aspect = (pw > 0 && ph > 0) ? (float)pw / ph : 0;
                ok = thumb_from_jpeg(&job, o, out);
                if (ok && (job.sw < THUMB_W * 3 / 4 || job.sh < THUMB_H * 3 / 4)) ok = false;
            }
            free(t);
            fclose(f);
            if (ok) return true;
        }
        memset(&job, 0, sizeof(job));
    }
    if (info->is_jpeg && !info->progressive) {
        FILE *f = fopen(path, "rb");
        if (!f) return false;
        setvbuf(f, NULL, _IOFBF, 32 * 1024);
        job.f = f;
        bool ok = thumb_from_jpeg(&job, o, out);
        fclose(f);
        if (ok) return true;
        memset(&job, 0, sizeof(job));
    }
    int w, h;
    uint8_t *rgb = stb_decode(path, &w, &h, 2u * 1024 * 1024);
    if (!rgb) return false;
    job.sw = w; job.sh = h;
    job.orientation = info->is_jpeg ? o : 1;
    job.block = thumb_block;
    bool ok = thumb_setup(&job, w, h, 0);
    if (ok) {
        feed_rows(&job, rgb, w, h);
        thumb_finish(&job, out);
    }
    free(job.sum); free(job.cnt);
    stbi_image_free(rgb);
    return ok;
}

/* ------------------------------------------------------- full pictures */

bool pic_ok(const Picture *pic) { return pic->cols > 0 && pic->tile[0].data; }

void pic_free(Picture *pic) {
    for (int i = 0; i < 4; i++) gfx_image_free(&pic->tile[i]);
    pic->cols = pic->rows = 0;
    pic->w = pic->h = 0;
}

static bool pic_alloc(Picture *pic, int w, int h) {
    memset(pic->tile, 0, sizeof(pic->tile));
    pic->w = w; pic->h = h;
    pic->cols = (w + PIC_TILE - 1) / PIC_TILE;
    pic->rows = (h + PIC_TILE - 1) / PIC_TILE;
    if (pic->cols > 2 || pic->rows > 2) { pic->cols = pic->rows = 0; return false; }
    for (int r = 0; r < pic->rows; r++)
        for (int c = 0; c < pic->cols; c++) {
            int tw = w - c * PIC_TILE, th = h - r * PIC_TILE;
            if (tw > PIC_TILE) tw = PIC_TILE;
            if (th > PIC_TILE) th = PIC_TILE;
            int txw, txh;
            uint16_t *t = tex_alloc565(tw, th, &txw, &txh);
            GfxImage *img = &pic->tile[r * 2 + c];
            if (!t) { pic_free(pic); return false; }
            /* the texture object is set up once the pixels are in (pic_done) */
            img->data = t;
            img->w = (u16)tw; img->h = (u16)th; img->tex_w = (u16)txw; img->tex_h = (u16)txh;
        }
    return true;
}

static void pic_done(Picture *pic) {
    for (int r = 0; r < pic->rows; r++)
        for (int c = 0; c < pic->cols; c++) {
            GfxImage *img = &pic->tile[r * 2 + c];
            void *d = img->data;
            int w = img->w, h = img->h, tw = img->tex_w, th = img->tex_h;
            img->data = NULL;
            gfx_image_init(img, d, w, h, tw, th, GFX_RGB565);
        }
}

static inline void pic_put(Picture *pic, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= pic->w || y >= pic->h) return;
    int col = x >= PIC_TILE, row = y >= PIC_TILE;
    GfxImage *img = &pic->tile[row * 2 + col];
    ((uint16_t *)img->data)[tex_index(img->tex_w, x - col * PIC_TILE, y - row * PIC_TILE)] = c;
}

static bool pic_block(Job *job, int x, int y, int bw, int bh, const uint8_t *rgb) {
    Picture *pic = job->pic;
    for (int j = 0; j < bh; j++) {
        int sy = y + j;
        if (sy >= job->sh) break;
        for (int i = 0; i < bw; i++) {
            int sx = x + i;
            if (sx >= job->sw) break;
            int ox, oy;
            orient(job->orientation, sx, sy, job->sw, job->sh, &ox, &oy);
            if (job->ratio < 1.0f) { ox = (int)(ox * job->ratio); oy = (int)(oy * job->ratio); }
            const uint8_t *p = rgb + (j * bw + i) * 3;
            pic_put(pic, ox, oy, rgb565(p[0], p[1], p[2]));
        }
    }
    return true;
}

static int g_max_side;
static bool pic_setup(Job *job, int w, int h, int s) {
    (void)w; (void)h; (void)s;
    int dw = job->orientation >= 5 ? job->sh : job->sw;
    int dh = job->orientation >= 5 ? job->sw : job->sh;
    int longest = dw > dh ? dw : dh;
    job->ratio = 1.0f;
    if (longest > g_max_side) {
        job->ratio = (float)g_max_side / longest;
        dw = (int)(dw * job->ratio);
        dh = (int)(dh * job->ratio);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    return pic_alloc(job->pic, dw, dh);
}

/* Shrinks an RGB picture by area-averaging into the Picture (upright). */
static bool pic_from_rgb(Picture *pic, const uint8_t *rgb, int w, int h, int orientation, int max_side) {
    int dw = orientation >= 5 ? h : w, dh = orientation >= 5 ? w : h;
    float k = 1.0f;
    int longest = dw > dh ? dw : dh;
    if (longest > max_side) k = (float)max_side / longest;
    int ow = (int)(dw * k), oh = (int)(dh * k);
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;
    if (!pic_alloc(pic, ow, oh)) return false;
    int step = (int)(1.0f / k);
    if (step < 1) step = 1;
    for (int y = 0; y < oh; y++) {
        for (int x = 0; x < ow; x++) {
            /* average a step x step block of the upright picture */
            int ux0 = (int)(x / k), uy0 = (int)(y / k);
            int r = 0, g = 0, b = 0, n = 0;
            for (int yy = 0; yy < step; yy++)
                for (int xx = 0; xx < step; xx++) {
                    int ux = ux0 + xx, uy = uy0 + yy;
                    if (ux >= dw || uy >= dh) continue;
                    /* upright (ux,uy) back to the stored picture */
                    int sx, sy;
                    switch (orientation) {
                    case 2: sx = w - 1 - ux; sy = uy; break;
                    case 3: sx = w - 1 - ux; sy = h - 1 - uy; break;
                    case 4: sx = ux; sy = h - 1 - uy; break;
                    case 5: sx = uy; sy = ux; break;
                    case 6: sx = uy; sy = h - 1 - ux; break;
                    case 7: sx = w - 1 - uy; sy = h - 1 - ux; break;
                    case 8: sx = w - 1 - uy; sy = ux; break;
                    default: sx = ux; sy = uy; break;
                    }
                    const uint8_t *p = rgb + ((size_t)sy * w + sx) * 3;
                    r += p[0]; g += p[1]; b += p[2]; n++;
                }
            if (n) pic_put(pic, x, y, rgb565(r / n, g / n, b / n));
        }
    }
    return true;
}

bool pic_load(const char *path, int max_side, Picture *pic, char *why, size_t whylen) {
    memset(pic, 0, sizeof(*pic));
    if (why && whylen) why[0] = 0;
    if (max_side > 2 * PIC_TILE) max_side = 2 * PIC_TILE;
    if (!pic_info(path, &pic->info)) {
        if (why) snprintf(why, whylen, "This file can't be read.");
        return false;
    }
    if (pic->info.is_jpeg && !pic->info.progressive) {
        FILE *f = fopen(path, "rb");
        if (!f) { if (why) snprintf(why, whylen, "This file can't be opened."); return false; }
        setvbuf(f, NULL, _IOFBF, 32 * 1024);
        Job job;
        memset(&job, 0, sizeof(job));
        job.f = f;
        job.pic = pic;
        job.orientation = pic->info.orientation;
        job.block = pic_block;
        g_max_side = max_side;
        JRESULT r = jd_run(&job, max_side, false, NULL, NULL, NULL, pic_setup);
        fclose(f);
        if (r == JDR_OK) { pic_done(pic); return true; }
        pic_free(pic);
        if (r == JDR_MEM1 && why) { snprintf(why, whylen, "Not enough memory for this photo."); return false; }
        if (r != JDR_FMT3 && r != JDR_FMT2) {
            if (why) snprintf(why, whylen, "This photo seems to be damaged.");
            return false;
        }
        /* a kind of JPEG TJpgDec doesn't do: try stb_image below */
    }
    int w, h;
    uint8_t *rgb = stb_decode(path, &w, &h, 2u * 1024 * 1024);   /* stb needs ~6 bytes a pixel */
    if (!rgb) {
        if (why) snprintf(why, whylen, pic->info.progressive ? "This photo is too big to open (progressive JPEG)."
                                                           : "This picture can't be opened.");
        return false;
    }
    bool ok = pic_from_rgb(pic, rgb, w, h, pic->info.is_jpeg ? pic->info.orientation : 1, max_side);
    stbi_image_free(rgb);
    if (ok) pic_done(pic);
    else if (why) snprintf(why, whylen, "Not enough memory for this picture.");
    return ok;
}

void pic_draw(Picture *pic, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint) {
    if (!pic_ok(pic) || u1 <= u0 || v1 <= v0) return;
    float kx = w / (u1 - u0), ky = h / (v1 - v0);
    for (int r = 0; r < pic->rows; r++)
        for (int c = 0; c < pic->cols; c++) {
            GfxImage *img = &pic->tile[r * 2 + c];
            float tx0 = c * PIC_TILE, ty0 = r * PIC_TILE, tx1 = tx0 + img->w, ty1 = ty0 + img->h;
            float a0 = fmaxf(u0, tx0), a1 = fminf(u1, tx1), b0 = fmaxf(v0, ty0), b1 = fminf(v1, ty1);
            if (a1 <= a0 || b1 <= b0) continue;
            gfx_image_draw_part(img, x + (a0 - u0) * kx, y + (b0 - v0) * ky, (a1 - a0) * kx, (b1 - b0) * ky,
                                a0 - tx0, b0 - ty0, a1 - tx0, b1 - ty0, tint);
        }
}

void pic_draw_rotated(Picture *pic, float cx, float cy, float w, float h, float deg, Color tint) {
    if (!pic_ok(pic)) return;
    float rad = deg * (float)M_PI / 180.0f, cs = cosf(rad), sn = sinf(rad);
    float kx = w / pic->w, ky = h / pic->h;
    for (int r = 0; r < pic->rows; r++)
        for (int c = 0; c < pic->cols; c++) {
            GfxImage *img = &pic->tile[r * 2 + c];
            float x0 = c * PIC_TILE * kx - w / 2, y0 = r * PIC_TILE * ky - h / 2;
            float x1 = x0 + img->w * kx, y1 = y0 + img->h * ky;
            float px[4] = { x0, x1, x1, x0 }, py[4] = { y0, y0, y1, y1 }, q[8];
            for (int i = 0; i < 4; i++) {
                q[i * 2] = cx + px[i] * cs - py[i] * sn;
                q[i * 2 + 1] = cy + px[i] * sn + py[i] * cs;
            }
            float uv[8] = { 0, 0, img->w, 0, img->w, img->h, 0, img->h };
            gfx_image_quad(img, q, uv, tint);
        }
}

void pic_get_rgb(const Picture *pic, int x, int y, uint8_t *rgb) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= pic->w) x = pic->w - 1;
    if (y >= pic->h) y = pic->h - 1;
    int col = x >= PIC_TILE, row = y >= PIC_TILE;
    const GfxImage *img = &pic->tile[row * 2 + col];
    uint16_t c = ((const uint16_t *)img->data)[tex_index(img->tex_w, x - col * PIC_TILE, y - row * PIC_TILE)];
    int r, g, b;
    unrgb565(c, &r, &g, &b);
    rgb[0] = (uint8_t)r; rgb[1] = (uint8_t)g; rgb[2] = (uint8_t)b;
}

static void write_cb(void *ctx, void *data, int size) { fwrite(data, 1, (size_t)size, (FILE *)ctx); }

bool pic_save_jpeg(const char *path, const uint8_t *rgb, int w, int h, int quality) {
    char tmp[320];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    setvbuf(f, NULL, _IOFBF, 32 * 1024);
    int ok = stbi_write_jpg_to_func(write_cb, f, w, h, 3, rgb, quality);
    ok = (fflush(f) == 0) && ok;
    fclose(f);
    if (!ok) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
}
