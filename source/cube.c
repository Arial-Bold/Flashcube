/* cube.c - see cube.h. Every face is a flat 180x180 plane placed in 3D
 * (gfx_begin_3d), drawn back to front so the glass blends correctly. */
#include "cube.h"
#include "icons.h"
#include "ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CUBE 180.0f
#define HALF 90.0f
#define PERSP 700.0f
#define CY 207.0f

static const float BASE_X = -8, BASE_Y = -12, BASE_Z = -4;     /* front view */
static const float SIDE_X = -6, SIDE_Y = -8, SIDE_Z = -3;      /* a side shown */

void cube_base_angles(float *x, float *y, float *z) { *x = BASE_X; *y = BASE_Y; *z = BASE_Z; }

void cube_view_init(CubeView *v) {
    memset(v, 0, sizeof(*v));
    v->labels = 1;
    v->lit = -1;
    v->scale = 1;
    v->alpha = 1;
    v->sway = 1;
    v->side = SIDE_ALBUM;
}

/* ------------------------------------------------------------ the glass */

typedef struct { float r, g, b, a; } RGBA;

static RGBA over(RGBA top, RGBA bot) {
    RGBA o;
    o.a = top.a + bot.a * (1 - top.a);
    if (o.a < 1e-4f) { o.r = o.g = o.b = 0; return o; }
    o.r = (top.r * top.a + bot.r * bot.a * (1 - top.a)) / o.a;
    o.g = (top.g * top.a + bot.g * bot.a * (1 - top.a)) / o.a;
    o.b = (top.b * top.a + bot.b * bot.a * (1 - top.a)) / o.a;
    return o;
}

static RGBA radial(float u, float v, float cx, float cy, float stop, float r, float g, float b, float a) {
    float d = sqrtf((u - cx) * (u - cx) + (v - cy) * (v - cy));
    float far = 0;   /* farthest corner */
    for (int k = 0; k < 4; k++) {
        float qx = (k & 1) ? 1 : 0, qy = (k & 2) ? 1 : 0;
        float e = sqrtf((qx - cx) * (qx - cx) + (qy - cy) * (qy - cy));
        if (e > far) far = e;
    }
    float t = d / (far * stop);
    RGBA o = { r, g, b, a * (1 - clamp01(t)) };
    return o;
}

/* colour of glass kind at (u,v) in 0..1 */
static RGBA glass_at(int kind, float u, float v) {
    if (kind == 0 || kind == 2) {
        float k = kind == 0 ? 0.42f : 0.32f;
        RGBA l1 = radial(u, v, 0.78f, 0.22f, 0.55f, 70, 140, 120, k);
        RGBA l2 = radial(u, v, 0.22f, 0.82f, 0.58f, 40, 62, 150, k);
        RGBA l3;
        if (kind == 0) {
            float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) / 0.7071f;
            d = clamp01(d);
            l3.r = lerpf(18, 6, d); l3.g = lerpf(30, 10, d); l3.b = lerpf(42, 16, d); l3.a = lerpf(0.66f, 0.72f, d);
        } else {
            l3.r = 10; l3.g = 16; l3.b = 24; l3.a = 0.34f;
        }
        return over(l1, over(l2, l3));
    }
    /* the sides: linear-gradient(135deg, ...) */
    float p = ((u - 0.5f) * 0.7071f + (v - 0.5f) * 0.7071f) / 1.4142f + 0.5f;
    RGBA a = { 120, 160, 190, 0.10f }, b = { 40, 60, 90, 0.06f }, c = { 140, 110, 200, 0.10f };
    RGBA o;
    if (p < 0.5f) {
        float t = clamp01(p / 0.5f);
        o.r = lerpf(a.r, b.r, t); o.g = lerpf(a.g, b.g, t); o.b = lerpf(a.b, b.b, t); o.a = lerpf(a.a, b.a, t);
    } else {
        float t = clamp01((p - 0.5f) / 0.5f);
        o.r = lerpf(b.r, c.r, t); o.g = lerpf(b.g, c.g, t); o.b = lerpf(b.b, c.b, t); o.a = lerpf(b.a, c.a, t);
    }
    return o;
}

#define GN 6
static Color glass_cache[3][(GN + 1) * (GN + 1)];
static bool glass_ready;

static void glass_prepare(void) {
    for (int k = 0; k < 3; k++)
        for (int j = 0; j <= GN; j++)
            for (int i = 0; i <= GN; i++) {
                RGBA c = glass_at(k, (float)i / GN, (float)j / GN);
                glass_cache[k][j * (GN + 1) + i] = rgba((u8)c.r, (u8)c.g, (u8)c.b, (u8)(clamp01(c.a) * 255));
            }
    glass_ready = true;
}

/* Rounded-square outline points (clockwise), corner radius r, inset by d. */
static int perimeter(float size, float r, float d, float *px, float *py) {
    int n = 0;
    float x0 = d, y0 = d, x1 = size - d, y1 = size - d;
    r -= d;
    if (r < 0.5f) r = 0.5f;
    const float cx[4] = { x1 - r, x1 - r, x0 + r, x0 + r }, cy[4] = { y0 + r, y1 - r, y1 - r, y0 + r };
    for (int c = 0; c < 4; c++) {
        float a0 = (-90.0f + 90.0f * c) * (float)M_PI / 180.0f;
        for (int i = 0; i <= 4; i++) {
            float a = a0 + (float)M_PI * 0.5f * i / 4;
            px[n] = cx[c] + cosf(a) * r;
            py[n] = cy[c] + sinf(a) * r;
            n++;
        }
        /* points along the straight edge after this corner */
        float ex = px[n - 1], ey = py[n - 1];
        float nx = cx[(c + 1) % 4] + cosf(a0 + (float)M_PI * 0.5f) * r, ny = cy[(c + 1) % 4] + sinf(a0 + (float)M_PI * 0.5f) * r;
        for (int k = 1; k < 6; k++) {
            px[n] = ex + (nx - ex) * k / 6;
            py[n] = ey + (ny - ey) * k / 6;
            n++;
        }
    }
    return n;
}

static Color conic(float x, float y, float size, float a) {
    /* conic-gradient(from 20deg, green, blue, purple, green) */
    float ang = atan2f(x - size / 2, -(y - size / 2)) * 180.0f / (float)M_PI;   /* 0 = up, clockwise */
    float f = fmodf(ang - 20 + 720, 360) / 360.0f;
    static const float C[4][3] = { { 120, 255, 170 }, { 110, 170, 255 }, { 200, 120, 255 }, { 120, 255, 170 } };
    float s = f * 3;
    int i = (int)s;
    if (i > 2) i = 2;
    float t = s - i;
    return rgba((u8)lerpf(C[i][0], C[i + 1][0], t), (u8)lerpf(C[i][1], C[i + 1][1], t), (u8)lerpf(C[i][2], C[i + 1][2], t),
                (u8)(255 * 0.9f * a));
}

/* A band of colour following the outline between insets d0 and d1. */
static void edge_band(float size, float r, float d0, float d1, bool rainbow, Color c) {
    float ox[64], oy[64], ix[64], iy[64];
    int n = perimeter(size, r, d0, ox, oy);
    perimeter(size, r, d1, ix, iy);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float q[8] = { ox[i], oy[i], ox[j], oy[j], ix[j], iy[j], ix[i], iy[i] };
        if (rainbow) {
            Color cc[4] = { conic(ox[i], oy[i], size, c.a / 255.0f), conic(ox[j], oy[j], size, c.a / 255.0f),
                            conic(ix[j], iy[j], size, c.a / 255.0f), conic(ix[i], iy[i], size, c.a / 255.0f) };
            gfx_quad_colors(q, cc);
        } else {
            gfx_quad(q, c);
        }
    }
}

/* Inner glow (box-shadow: inset 0 0 blur colour), as fading bands. */
static void inner_glow(float size, float r, float blur, Color c) {
    if (blur <= 0 || !c.a) return;
    int steps = blur > 8 ? 4 : 2;
    for (int i = 0; i < steps; i++) {
        float d0 = blur * i / steps, d1 = blur * (i + 1) / steps;
        float k = 1.0f - (i + 0.5f) / steps;
        edge_band(size, r, d0, d1, false, color_alpha(c, k * k * 0.9f));
    }
}

void cube_glass(int kind, float size, float radius, float alpha, bool rim) {
    if (!glass_ready) glass_prepare();
    if (alpha <= 0) return;
    float xy[(GN + 1) * (GN + 1) * 2];
    Color col[(GN + 1) * (GN + 1)];
    for (int j = 0; j <= GN; j++)
        for (int i = 0; i <= GN; i++) {
            int k = j * (GN + 1) + i;
            xy[k * 2] = size * i / GN;
            xy[k * 2 + 1] = size * j / GN;
            col[k] = color_alpha(glass_cache[kind][k], alpha);
        }
    (void)radius;
    gfx_mesh(GN, GN, xy, col);
    if (!rim) return;
    float s = size / CUBE;
    if (kind == 1) {
        inner_glow(size, radius, 18 * s, rgba(120, 160, 200, (u8)(0.10f * 255 * alpha)));
        edge_band(size, radius, 0, 1.0f * s, false, rgba(220, 230, 250, (u8)(0.30f * 255 * alpha)));
    } else {
        inner_glow(size, radius, 12 * s, rgba(170, 200, 235, (u8)(0.16f * 255 * alpha)));
        inner_glow(size, radius, 3 * s, rgba(220, 230, 255, (u8)(0.18f * 255 * alpha)));
        edge_band(size, radius, 0, 1.0f * s, false, rgba(225, 232, 245, (u8)(0.34f * 255 * alpha)));
    }
    edge_band(size, radius, 0, 1.5f * s, true, rgba(255, 255, 255, (u8)(0.4f * 255 * alpha)));
}

/* --------------------------------------------------------------- content */

static void label(const char *s, int where, bool lit, float a) {
    if (a <= 0) return;
    FontId f = FONT_CUBE;
    float size = 20, w = gfx_text_width_sp(f, size, -0.2f, s, -1);
    Color c = lit ? rgba(255, 255, 255, (u8)(255 * a)) : rgba(255, 255, 255, (u8)(0.80f * 255 * a));
    Color glow = lit ? rgba(175, 195, 255, (u8)(255 * a)) : rgba(200, 220, 255, (u8)(0.35f * 255 * a));
    float gr = lit ? 5 : 2;
    /* the 3D plane is already set; rotate the label inside it for the
     * vertical ones by drawing in a turned coordinate frame */
    if (where == 0) ui_text_glow(f, size, -0.2f, HALF - w / 2, 13, 20, c, glow, gr, s);
    else if (where == 1) ui_text_glow(f, size, -0.2f, HALF - w / 2, CUBE - 15 - 20, 20, c, glow, gr, s);
}

/* draws text turned 90 degrees inside the current plane matrix */
static void label_vertical(const M34 *face, float cx, float cy, const char *s, bool right_side, bool lit, float a) {
    if (a <= 0) return;
    float w = gfx_text_width_sp(FONT_CUBE, 20, -0.2f, s, -1);
    /* right: reads top to bottom (letters' tops face right); left: bottom to top */
    M34 m;
    if (right_side) m = m34_mul(*face, m34_mul(m34_translate(CUBE - 9, HALF - w / 2, 0), m34_rz(90)));
    else m = m34_mul(*face, m34_mul(m34_translate(9, HALF + w / 2, 0), m34_rz(-90)));
    gfx_begin_3d(m.m, cx, cy, PERSP);
    Color c = lit ? rgba(255, 255, 255, (u8)(255 * a)) : rgba(255, 255, 255, (u8)(0.80f * 255 * a));
    Color glow = lit ? rgba(175, 195, 255, (u8)(255 * a)) : rgba(200, 220, 255, (u8)(0.35f * 255 * a));
    ui_text_glow(FONT_CUBE, 20, -0.2f, 0, 0, 20, c, glow, lit ? 5 : 2, s);
}

static void page(const CubePage *p, float a) {
    if (a <= 0 || !p->title) return;
    float w = gfx_text_width_sp(FONT_CUBE, 22, -0.3f, p->title, -1);
    ui_text_glow(FONT_CUBE, 22, -0.3f, HALF - w / 2, 15, 22, rgba(255, 255, 255, (u8)(255 * a)),
                 rgba(200, 220, 255, (u8)(0.45f * 255 * a)), 3, p->title);
    for (int i = 0; i < p->nbands; i++) {
        float top = (p->nbands == 3 ? 48 : 87) + 39.0f * i + (p->nbands == 1 ? 39 : 0);
        float x = 17, bw = CUBE - 17 - 14;
        gfx_rrect(x, top, bw, 29, 3, rgba(200, 210, 228, (u8)(0.17f * 255 * a)));
        gfx_rect(x + 2, top, bw - 4, 1, rgba(255, 255, 255, (u8)(0.18f * 255 * a)));
        Color tc = rgba(255, 255, 255, (u8)(255 * a)), sh = rgba(0, 0, 0, (u8)(0.85f * 255 * a));
        ui_text(FONT_ROUND, 13, -0.2f, x + 7, top + 1, 29, sh, p->band_label[i]);
        ui_text(FONT_ROUND, 13, -0.2f, x + 7, top, 29, tc, p->band_label[i]);
        const char *v = p->band_value[i];
        if (v[0]) {
            float vs = strlen(v) > 10 ? 12 : strlen(v) > 7 ? 13 : 17;
            ui_text_right(FONT_ROUND, vs, -0.2f, x + bw - 7, top + 1, 29, sh, v);
            ui_text_right(FONT_ROUND, vs, -0.2f, x + bw - 7, top, 29, tc, v);
        }
    }
}

typedef struct { Color hi, mid, lo, shadow, glow; } BlockColors;
static const BlockColors block_colors[SIDE_COUNT] = {
    { { 0xdf, 0xf4, 0xff, 255 }, { 0x64, 0xc4, 0xff, 255 }, { 0x2b, 0x86, 0xd8, 255 }, { 0x1d, 0x5a, 0xa6, 255 }, { 90, 190, 255, 180 } },
    { { 0xe4, 0xff, 0xe9, 255 }, { 0x5f, 0xe3, 0x8a, 255 }, { 0x22, 0xa0, 0x4e, 255 }, { 0x15, 0x7a, 0x38, 255 }, { 90, 230, 140, 180 } },
    { { 0xff, 0xf0, 0xd6, 255 }, { 0xff, 0xb0, 0x4a, 255 }, { 0xd0, 0x74, 0x1a, 255 }, { 0xa4, 0x52, 0x0e, 255 }, { 255, 170, 70, 180 } },
    { { 0xff, 0xe3, 0xee, 255 }, { 0xff, 0x7a, 0xa8, 255 }, { 0xd4, 0x3a, 0x72, 255 }, { 0xa5, 0x23, 0x4f, 255 }, { 255, 110, 160, 180 } },
};

static const IconBlock *icon_of(int side, int *n) {
    switch (side) {
    case SIDE_SLIDESHOW: *n = (int)(sizeof(icon_slide) / sizeof(icon_slide[0])); return icon_slide;
    case SIDE_TRANSFER: *n = (int)(sizeof(icon_transfer) / sizeof(icon_transfer[0])); return icon_transfer;
    case SIDE_PLAYTIME: *n = (int)(sizeof(icon_play) / sizeof(icon_play[0])); return icon_play;
    default: *n = (int)(sizeof(icon_album) / sizeof(icon_album[0])); return icon_album;
    }
}

/* The icon's change of shape: hold, morph, hold, morph back (6 s). */
static float morph_at(float t) {
    t = fmodf(t, 6.0f);
    if (t < 1.2f) return 0;
    if (t < 2.0f) return ease_bezier((t - 1.2f) / 0.8f, 0.45f, 0, 0.25f, 1);
    if (t < 4.4f) return 1;
    if (t < 5.2f) return 1 - ease_bezier((t - 4.4f) / 0.8f, 0.45f, 0, 0.25f, 1);
    return 0;
}

static void draw_icon(int side, float a, float t, float collapse) {
    int n;
    const IconBlock *b = icon_of(side, &n);
    const BlockColors *bc = &block_colors[side];
    for (int i = 0; i < n; i++) {
        float m = morph_at(t - (i % 14) * 0.025f);
        float x = lerpf(b[i].ax, b[i].bx, m), y = lerpf(b[i].ay, b[i].by, m), s = 7;
        float ba = a;
        if (collapse > 0) {
            float c = ease_bezier(clamp01(collapse * 1.15f - (i % 9) * 0.015f), 0.6f, 0, 0.4f, 1);
            x = lerpf(x, 48, c); y = lerpf(y, 36, c);
            s = lerpf(7, 1.4f, c);
            ba *= 1 - c;
        }
        if (ba <= 0.01f) continue;
        x += 38 + (7 - s) / 2; y += 46 + (7 - s) / 2;
        gfx_rrect(x - 2.5f, y - 2.5f, s + 5, s + 5, 3, color_alpha(bc->glow, ba * 0.35f));
        gfx_rect(x + 1.5f, y + 1.5f, s, s, color_alpha(bc->shadow, ba));
        float q[8] = { x, y, x + s, y, x + s, y + s, x, y + s };
        Color c4[4] = { color_alpha(bc->hi, ba), color_alpha(bc->mid, ba), color_alpha(bc->lo, ba), color_alpha(bc->mid, ba) };
        gfx_quad_colors(q, c4);
    }
}

/* ------------------------------------------------------------- the cube */

/* idle sway (degrees), the same keyframes as the design: 8 s */
static void sway(float t, float *x, float *y, float *z) {
    static const float K[5][3] = { { 0, 0, 0 }, { 5, 8, -2 }, { 2, 18, 2 }, { -2, 10, -1 }, { 0, 0, 0 } };
    float p = fmodf(t, 8.0f) / 2.0f;
    int i = (int)p;
    if (i > 3) i = 3;
    float e = ease_inout(p - i);
    *x = lerpf(K[i][0], K[i + 1][0], e);
    *y = lerpf(K[i][1], K[i + 1][1], e);
    *z = lerpf(K[i][2], K[i + 1][2], e);
}

/* face transforms: front, right, back, left, top, bottom */
static M34 face_rot(int f) {
    switch (f) {
    case 1: return m34_ry(90);
    case 2: return m34_ry(180);
    case 3: return m34_ry(-90);
    case 4: return m34_rx(90);
    case 5: return m34_rx(-90);
    default: return m34_identity();
    }
}

/* which face each side is on */
static int side_face(int side) {
    switch (side) {
    case SIDE_ALBUM: return 4;      /* top */
    case SIDE_SLIDESHOW: return 5;  /* bottom */
    case SIDE_TRANSFER: return 3;   /* left */
    default: return 1;              /* right: Playtime */
    }
}

/* the turn that brings a side to the front */
static M34 side_turn(int side, float amount) {
    switch (side) {
    case SIDE_ALBUM: return m34_rx(-90 * amount);
    case SIDE_SLIDESHOW: return m34_rx(90 * amount);
    case SIDE_TRANSFER: return m34_ry(90 * amount);
    default: return m34_ry(-90 * amount);
    }
}

typedef struct { int kind; int face; M34 m; float z; } Item;

static int cmp_item(const void *a, const void *b) {
    float x = ((const Item *)a)->z, y = ((const Item *)b)->z;
    return x < y ? -1 : x > y;
}

void cube_draw(const CubeView *v) {
    if (v->alpha <= 0) return;
    float cx = gfx_width() / 2, cy = CY;
    float sx = 0, sy = 0, sz = 0;
    sway(v->time, &sx, &sy, &sz);
    sx *= v->sway; sy *= v->sway; sz *= v->sway;
    float bx = lerpf(BASE_X, SIDE_X, v->turn), by = lerpf(BASE_Y, SIDE_Y, v->turn), bz = lerpf(BASE_Z, SIDE_Z, v->turn);
    M34 C = m34_mul(m34_scale(v->scale),
            m34_mul(m34_mul(m34_rx(sx), m34_mul(m34_ry(sy), m34_rz(sz))),
            m34_mul(m34_mul(m34_rx(bx), m34_mul(m34_ry(by), m34_rz(bz))), side_turn(v->side, v->turn))));
    Item items[8];
    int n = 0;
    for (int f = 0; f < 6; f++) {
        Item *it = &items[n++];
        it->kind = 0;
        it->face = f;
        it->m = m34_mul(C, m34_mul(face_rot(f), m34_translate(-HALF, -HALF, HALF)));
        float X, Y, Z;
        m34_apply(&it->m, HALF, HALF, &X, &Y, &Z);
        it->z = Z;
    }
    int sf = side_face(v->side);
    if (v->icon > 0.01f) {
        Item *it = &items[n++];
        it->kind = 1;
        it->face = sf;
        float w1 = sinf(v->time * 2 * (float)M_PI / 4.4f - (float)M_PI / 2);   /* -1..1 */
        M34 wob = m34_mul(m34_translate(90, 86, 0), m34_mul(m34_ry(18 * w1), m34_mul(m34_rx(-8 * w1), m34_translate(-90, -86, 0))));
        it->m = m34_mul(C, m34_mul(face_rot(sf), m34_mul(m34_translate(-HALF, -HALF, 34), wob)));
        float X, Y, Z;
        m34_apply(&it->m, HALF, HALF, &X, &Y, &Z);
        it->z = Z;
    }
    qsort(items, n, sizeof(Item), cmp_item);
    for (int k = 0; k < n; k++) {
        Item *it = &items[k];
        gfx_begin_3d(it->m.m, cx, cy, PERSP);
        if (it->kind == 1) {
            draw_icon(v->side, v->icon * v->alpha, v->icon_time, v->collapse);
            continue;
        }
        int f = it->face;
        if (f == 0) {
            cube_glass(0, CUBE, 7, v->alpha, true);
            float la = v->labels * v->alpha;
            if (la > 0) {
                label("Album", 0, v->lit == SIDE_ALBUM, la);
                label("Slideshow", 1, v->lit == SIDE_SLIDESHOW, la);
                label_vertical(&it->m, cx, cy, "Playtime", true, v->lit == SIDE_PLAYTIME, la);
                label_vertical(&it->m, cx, cy, "Transfer", false, v->lit == SIDE_TRANSFER, la);
            }
        } else if (f == sf && v->turn > 0) {
            /* the side being shown: its glass darkens a little and its page appears */
            cube_glass(1, CUBE, 7, v->alpha * (1 - v->turn), true);
            cube_glass(2, CUBE, 7, v->alpha * v->turn, false);
            float pa = clamp01((v->turn - 0.45f) / 0.55f) * v->alpha * (1 - v->collapse);
            page(&v->pages[v->side], pa);
        } else {
            cube_glass(1, CUBE, 7, v->alpha, true);
        }
    }
    gfx_end_3d();
}
