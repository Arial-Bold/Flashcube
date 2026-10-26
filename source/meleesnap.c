/* meleesnap.c - see meleesnap.h */
#include "meleesnap.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SECTOR 8192

/* ---- HSD_Decrypt (sysdolphin crypt.c) ---- */
static const uint8_t keys[13] = { 0x26, 0xFF, 0xE8, 0xEF, 0x42, 0xD6, 0x01, 0x54, 0x14, 0xA3, 0x80, 0xFD, 0x6E };

static uint8_t dbyte(uint32_t prev, uint32_t cur) {
    switch (prev % 7) {
    case 0: cur = (cur & 1) | ((cur << 1) & 4) | ((cur << 2) & 0x10) | ((cur << 3) & 0x40) | ((cur >> 3) & 2) | ((cur >> 2) & 8) | ((cur >> 1) & 0x20) | (cur & 0x80); break;
    case 1: cur = ((cur << 1) & 2) | ((cur << 6) & 0x80) | (cur & 4) | ((cur >> 3) & 1) | ((cur << 1) & 0x20) | ((cur >> 1) & 0x10) | ((cur >> 3) & 8) | ((cur >> 1) & 0x40); break;
    case 2: cur = ((cur & 1) << 2) | ((cur << 2) & 8) | ((cur << 4) & 0x40) | ((cur << 1) & 0x10) | ((cur << 3) & 0x80) | ((cur >> 4) & 2) | ((cur >> 6) & 1) | ((cur >> 2) & 0x20); break;
    case 3: cur = ((cur << 4) & 0x10) | ((cur >> 1) & 1) | ((cur << 3) & 0x20) | ((cur >> 2) & 2) | ((cur >> 1) & 8) | ((cur << 1) & 0x40) | ((cur << 1) & 0x80) | ((cur >> 5) & 4); break;
    case 4: cur = ((cur << 3) & 8) | ((cur << 4) & 0x20) | ((cur >> 1) & 2) | ((cur << 4) & 0x80) | ((cur << 2) & 0x40) | ((cur >> 3) & 4) | ((cur >> 2) & 0x10) | ((cur >> 7) & 1); break;
    case 5: cur = ((cur & 1) << 5) | ((cur << 5) & 0x40) | ((cur & 4) << 5) | (cur & 8) | ((cur >> 2) & 4) | ((cur >> 5) & 1) | ((cur >> 5) & 2) | ((cur >> 3) & 0x10); break;
    default: cur = ((cur << 6) & 0x40) | (cur & 2) | ((cur >> 2) & 1) | ((cur << 2) & 0x20) | (cur & 0x10) | ((cur << 2) & 0x80) | ((cur >> 4) & 4) | ((cur >> 4) & 8); break;
    }
    cur &= 0xFF;
    return (uint8_t)(cur ^ keys[prev % 13] ^ prev);
}

/* decrypts `len` bytes of `src` into `dst`; true when the checksum matches */
static bool decrypt(const uint8_t *src, uint8_t *dst, int len) {
    if (len <= 16) return false;
    memcpy(dst, src, 16);
    uint32_t prev = src[15];
    for (int i = 16; i < len; i++) { dst[i] = dbyte(prev, src[i]); prev = src[i]; }
    uint8_t m[16] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 };
    for (int i = 16; i < len; i++) m[(i - 16) & 15] += dst[i];
    for (int i = 1; i < 16; i++) if (m[i - 1] == m[i]) m[i] ^= 0xFF;
    return memcmp(m, dst, 16) == 0;
}

static int rd32(const uint8_t *p) { return (int)((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

int msnap_extract(const uint8_t *file, int file_len, uint8_t **jpg) {
    *jpg = NULL;
    if (file_len < 2 * SECTOR) return -1;
    uint8_t *sec = malloc(SECTOR);
    if (!sec) return -1;
    /* the first block sits at the end of sector 0, after the comment, banner
     * and icon; its position depends on the icon layout, so find it by its
     * checksum */
    int off = -1;
    if (decrypt(file + 7792, sec, SECTOR - 7792)) off = 7792;      /* where Melee puts it */
    for (int o = SECTOR - 64; o >= 0 && off < 0; o -= 16)
        if (decrypt(file + o, sec, SECTOR - o)) off = o;
    if (off < 0) { free(sec); return -1; }
    const uint8_t *t = sec + 0x13;       /* the save's file table */
    int total = ((t[1] & 0x3F) << 16) | (t[2] << 8) | t[3];
    int nsec = file_len / SECTOR;
    uint8_t *data = malloc(total > 0 ? total : 1);
    if (!data || total < 0x38) { free(sec); free(data); return -1; }
    int first = SECTOR - off - 0x20;
    int have = first < total ? first : total;
    memcpy(data, sec + 0x20, have);
    /* the other blocks: one per sector, each with its block number */
    int need_blocks = 1 + (total - first + SECTOR - 0x20 - 1) / (SECTOR - 0x20);
    for (int b = 1; b < need_blocks; b++) {
        bool found = false;
        for (int s = 1; s < nsec && !found; s++) {
            if (!decrypt(file + s * SECTOR, sec, SECTOR)) continue;
            if (((sec[0x10] << 8) | sec[0x11]) != b) continue;
            int at = first + (b - 1) * (SECTOR - 0x20);
            int n = total - at < SECTOR - 0x20 ? total - at : SECTOR - 0x20;
            memcpy(data + at, sec + 0x20, n);
            found = true;
        }
        if (!found) { free(sec); free(data); return -1; }
    }
    free(sec);
    /* the snap struct: x0 = 4, 640x480, x8 = 3, xC = JPEG size, JPEG at 0x38 */
    int size = rd32(data + 12);
    if (rd32(data) != 4 || size <= 0 || size > total - 0x38) { free(data); return -1; }
    memmove(data, data + 0x38, size);
    *jpg = data;
    return size;
}

bool msnap_is_snapshot(const char *name, const uint8_t gamecode[4], const uint8_t company[2]) {
    if (gamecode[0] != 'G' || gamecode[1] != 'A' || gamecode[2] != 'L' || company[0] != '0' || company[1] != '1') return false;
    if (!name || !name[0]) return false;
    for (const char *p = name; *p; p++) if (*p < '0' || *p > '9') return false;
    return true;
}

/* ---- the HAL JPEG ---- */
typedef struct {
    uint8_t len[256];       /* code length per symbol, 0 if none */
    uint16_t code[256];
    int maxcode[18], valptr[18], mincode[18];
    uint8_t vals[256];
} Huff;

typedef struct {
    const uint8_t *p, *end;
    uint32_t bits;
    int nbits;
    bool bad;
} Bits;

static int getbit(Bits *b) {
    if (b->nbits == 0) {
        if (b->p >= b->end) { b->bad = true; return 0; }
        uint8_t c = *b->p++;
        if (c == 0xFF) {
            if (b->p < b->end && *b->p == 0) b->p++;
            else { b->bad = true; return 0; }
        }
        b->bits = c;
        b->nbits = 8;
    }
    b->nbits--;
    return (b->bits >> b->nbits) & 1;
}

static int getbits(Bits *b, int n) {
    int v = 0;
    while (n-- > 0) v = (v << 1) | getbit(b);
    return v;
}

static void huff_build(Huff *h, const uint8_t *counts, const uint8_t *vals) {
    int k = 0, code = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        for (int i = 0; i < counts[l - 1]; i++) { h->vals[k] = vals[k]; k++; code++; }
        h->maxcode[l] = counts[l - 1] ? code - 1 : -1;
        code <<= 1;
    }
}

static int huff_decode(Bits *b, const Huff *h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | getbit(b);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l])
            return h->vals[h->valptr[l] + code - h->mincode[l]];
    }
    b->bad = true;
    return 0;
}

static int extend(int v, int n) { return n && v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* HAL's coefficients: DC as usual; then for each nonzero AC coefficient a
 * size symbol and the (zero run + 1), then a size symbol and the value; a 0
 * symbol ends the block early (it's left out when the last one is nonzero) */
static void block(Bits *b, const Huff *dc, const Huff *ac, const uint8_t *q, int *pred, float *out) {
    int c[64];
    memset(c, 0, sizeof(c));
    int s = huff_decode(b, dc);
    *pred += s ? extend(getbits(b, s), s) : 0;
    c[0] = *pred * q[0];
    for (int k = 1; k < 64 && !b->bad;) {
        int rs = huff_decode(b, ac);
        if (rs == 0) break;
        int run = getbits(b, rs) - 1;
        k += run;
        int vs = huff_decode(b, ac);
        int v = extend(getbits(b, vs), vs);
        if (k < 64) c[zigzag[k]] = v * q[k];
        k++;
    }
    /* inverse DCT */
    static float cosv[8][8];
    static bool init;
    if (!init) {
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++)
                cosv[x][u] = (u ? 1.0f : 0.70710678f) * cosf((2 * x + 1) * u * 3.14159265f / 16) / 2;
        init = true;
    }
    float tmp[64];
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {
            float sum = 0;
            for (int v = 0; v < 8; v++) sum += cosv[y][v] * c[v * 8 + u];
            tmp[y * 8 + u] = sum;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float sum = 0;
            for (int u = 0; u < 8; u++) sum += cosv[x][u] * tmp[y * 8 + u];
            out[y * 8 + x] = sum;
        }
}

static uint8_t clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)(v + 0.5f); }

bool msnap_decode(const uint8_t *jpg, int len, uint8_t *rgb) {
    uint8_t q[4][64];
    Huff *hd = calloc(4, sizeof(Huff)), *ha = calloc(4, sizeof(Huff));
    if (!hd || !ha) { free(hd); free(ha); return false; }
    int w = 0, h = 0, comp_q[3] = { 0, 1, 1 }, comp_td[3] = { 0, 1, 1 }, comp_ta[3] = { 0, 1, 1 };
    const uint8_t *p = jpg, *end = jpg + len, *scan = NULL;
    memset(q, 1, sizeof(q));
    while (p + 4 <= end) {
        if (p[0] != 0xFF) { p++; continue; }
        int m = p[1];
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
        int L = (p[2] << 8) | p[3];
        const uint8_t *s = p + 4, *se = p + 2 + L;
        if (se > end) break;
        if (m == 0xDB) {
            while (s + 65 <= se) {
                int id = s[0] & 3;
                memcpy(q[id], s + 1, 64);   /* in zigzag order */
                s += 65;
            }
        } else if (m == 0xC4) {
            while (s + 17 <= se) {
                int tc = s[0] >> 4, id = s[0] & 3, n = 0;
                for (int i = 0; i < 16; i++) n += s[1 + i];
                huff_build(tc ? &ha[id] : &hd[id], s + 1, s + 17);
                s += 17 + n;
            }
        } else if (m == 0xC0) {
            h = (s[1] << 8) | s[2];
            w = (s[3] << 8) | s[4];
            int nc = s[5];
            for (int i = 0; i < nc && i < 3; i++) comp_q[i] = s[6 + i * 3 + 2] & 3;
        } else if (m == 0xDA) {
            int nc = s[0];
            for (int i = 0; i < nc && i < 3; i++) { comp_td[i] = s[2 + i * 2] >> 4; comp_ta[i] = s[2 + i * 2] & 3; }
            scan = se;
            break;
        }
        p = se;
    }
    if (!scan || w != MSNAP_W || h != MSNAP_H) { free(hd); free(ha); return false; }
    Bits b = { scan, end, 0, 0, false };
    int pred[3] = { 0, 0, 0 };
    float Y[4][64], Cb[64], Cr[64];
    for (int my = 0; my < h; my += 16)
        for (int mx = 0; mx < w; mx += 16) {
            for (int i = 0; i < 4; i++) block(&b, &hd[comp_td[0]], &ha[comp_ta[0]], q[comp_q[0]], &pred[0], Y[i]);
            block(&b, &hd[comp_td[1]], &ha[comp_ta[1]], q[comp_q[1]], &pred[1], Cb);
            block(&b, &hd[comp_td[2]], &ha[comp_ta[2]], q[comp_q[2]], &pred[2], Cr);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    float yy = Y[(y >> 3) * 2 + (x >> 3)][(y & 7) * 8 + (x & 7)] + 128;
                    float cb = Cb[(y >> 1) * 8 + (x >> 1)], cr = Cr[(y >> 1) * 8 + (x >> 1)];
                    uint8_t *o = rgb + ((my + y) * w + mx + x) * 3;
                    o[0] = clamp8(yy + 1.402f * cr);
                    o[1] = clamp8(yy - 0.344136f * cb - 0.714136f * cr);
                    o[2] = clamp8(yy + 1.772f * cb);
                }
        }
    free(hd); free(ha);
    return !b.bad;
}
