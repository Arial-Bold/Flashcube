/* logo.c - the opening: the FLASHCUBE logo (alternating indigo tiles)
 * curls up from its last letter into a little cube, its letters fade, it
 * spins, turns to glass, and grows into the menu cube. Timings follow the
 * design (a 10-second timeline; percentages below are of that). */
#include "logo.h"
#include "cube.h"
#include "ui.h"
#include "m3d.h"
#include <math.h>
#include <string.h>

#define TILE 28.0f

static float pct(float t) { return t * 10.0f; }   /* seconds -> % of the 10 s design timeline */

/* a value given at keyframes (percent, value), linear between */
static float keyed(float p, const float *k, int n) {
    if (p <= k[0]) return k[1];
    for (int i = 0; i < n - 1; i++) {
        float p0 = k[i * 2], p1 = k[i * 2 + 2];
        if (p <= p1) return lerpf(k[i * 2 + 1], k[i * 2 + 3], (p - p0) / (p1 - p0));
    }
    return k[(n - 1) * 2 + 1];
}

/* a segment eased like CSS: 0 before a, 1 after b */
static float seg(float p, float a, float b, float x1, float y1, float x2, float y2) {
    return ease_bezier(clamp01((p - a) / (b - a)), x1, y1, x2, y2);
}

float logo_duration(void) { return 7.3f; }
float logo_nav_alpha(float t) { return clamp01((pct(t) - 54) / 7); }
float logo_labels_alpha(float t) { return clamp01((pct(t) - 61) / 7); }
float logo_hints_alpha(float t) { return clamp01((pct(t) - 67) / 6); }

static void tile_face(int idx, bool front, float a, float g, float la, float r, const char *letter) {
    static const Color dark = { 58, 47, 143, 255 }, light = { 91, 79, 196, 255 };
    Color base = (idx & 1) ? light : dark;
    float fill = a * (1 - g * (front ? 0 : 1));
    if (fill > 0) gfx_rrect(0, 0, TILE, TILE, r, color_alpha(base, fill));
    /* inset shading: 1px dark rim, darker bottom */
    if (a > 0.05f) {
        gfx_rrect_outline(0, 0, TILE, TILE, r, 1, rgba(0, 0, 0, (u8)(0.28f * 255 * a)));
        gfx_rect(1, TILE - 2, TILE - 2, 2, rgba(0, 0, 0, (u8)(0.18f * 255 * a)));
    }
    if (g > 0) cube_glass(front ? 0 : 1, TILE, r, g, false);
    if (letter && la > 0.01f) {
        float w = gfx_text_width(FONT_CUBE, 18, letter, -1);
        ui_text(FONT_CUBE, 18, 0, TILE / 2 - w / 2, 0, TILE, rgba(255, 255, 255, (u8)(255 * la)), letter);
    }
}

void logo_draw(float t) {
    float p = pct(t);
    if (p >= 62) return;
    /* fading in and out, letters, fill, glass, corner rounding, perspective */
    static const float K_OP[] = { 0, 0, 3, 1, 55, 1, 62, 0 };
    static const float K_A[] = { 0, 1, 14, 1, 30, 0.45f, 38, 0.45f, 52, 0.05f, 62, 0.05f };
    static const float K_LA[] = { 0, 1, 14, 1, 22, 0 };
    static const float K_G[] = { 0, 0, 38, 0, 52, 1 };
    static const float K_R[] = { 0, 2, 38, 2, 52, 1.1f };
    static const float K_P[] = { 0, 520, 38, 520, 52, 109 };
    float op = keyed(p, K_OP, 4), a = keyed(p, K_A, 6) * op, la = keyed(p, K_LA, 3) * op;
    float g = keyed(p, K_G, 3) * op, r = keyed(p, K_R, 3), persp = keyed(p, K_P, 3);

    /* the whole logo moves to the cube's place and grows to its size */
    float mv = seg(p, 40, 56, 0.5f, 0, 0.3f, 1);
    float S = lerpf(1.6f, 6.43f, mv);
    float left = gfx_width() / 2 - 179 - 14;   /* 127 in 4:3, 234 in 16:9 */
    float ox = left + 14 + 179 * mv, oy = 226 + 14 - 33 * mv;

    /* the first tile rolls about the middle of the little cube */
    float r1 = seg(p, 24, 36, 0.45f, 0, 0.25f, 1), r2 = seg(p, 36, 56, 0.45f, 0, 0.25f, 1);
    float tz = 0, rx = 0, ry = 0, rz = 0;
    if (p >= 36) { tz = 14 * r2; rx = -22 + 14 * r2; ry = -40 - 332 * r2; rz = -4 * r2; }
    else if (p > 24) { rx = -22 * r1; ry = -40 * r1; }
    /* ends at (-8, -372, -4): the menu cube's own resting angles, one turn on */
    M34 roll = m34_mul(m34_translate(14, 14, -14),
               m34_mul(m34_translate(0, 0, tz),
               m34_mul(m34_rx(rx), m34_mul(m34_ry(ry), m34_mul(m34_rz(rz), m34_translate(-14, -14, 14))))));
    M34 view = m34_mul(m34_scale(S), m34_translate(-14, -14, 0));   /* about the logo's (14,14) */

    /* the strip curls up from the end: tile k turns 90 degrees about its left edge */
    static const float US[8] = { 24.8f, 23.4f, 22.0f, 20.6f, 19.2f, 17.8f, 16.4f, 15.0f };
    static const char *letters[9] = { "F", "L", "A", "S", "H", "C", "U", "B", "E" };
    M34 m[9];
    float ang[9];
    m[0] = roll;
    ang[0] = 0;
    for (int k = 1; k < 9; k++) {
        ang[k] = 90 * seg(p, US[k - 1], US[k - 1] + 14, 0.4f, 0, 0.2f, 1);
        m[k] = m34_mul(m[k - 1], m34_mul(m34_translate(TILE, 0, 0), m34_ry(ang[k])));
    }
    /* draw back to front */
    typedef struct { int k; float z; int cap; } Part;
    Part parts[11];
    int n = 0;
    for (int k = 0; k < 9; k++) {
        /* tiles 5..9 vanish once folded (they'd sit on top of the first four) */
        if (k >= 4 && ang[k] >= 89.9f) continue;
        M34 w = m34_mul(view, m[k]);
        float X, Y, Z;
        m34_apply(&w, TILE / 2, TILE / 2, &X, &Y, &Z);
        parts[n].k = k; parts[n].z = Z; parts[n].cap = 0; n++;
    }
    float capk = seg(p, 26, 36, 0.4f, 0, 0.2f, 1);
    M34 capm[2];
    if (p > 26) {
        capm[0] = m34_mul(roll, m34_mul(m34_rx(-90 * capk), m34_scale3(1, capk, 1)));
        capm[1] = m34_mul(roll, m34_mul(m34_translate(0, TILE, 0), m34_mul(m34_rx(90 * capk), m34_mul(m34_scale3(1, capk, 1), m34_translate(0, -TILE, 0)))));
        for (int c = 0; c < 2; c++) {
            M34 w = m34_mul(view, capm[c]);
            float X, Y, Z;
            m34_apply(&w, TILE / 2, TILE / 2, &X, &Y, &Z);
            parts[n].k = -1; parts[n].z = Z; parts[n].cap = c + 1; n++;
        }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (parts[j].z < parts[i].z) { Part tmp = parts[i]; parts[i] = parts[j]; parts[j] = tmp; }
    for (int i = 0; i < n; i++) {
        M34 w = m34_mul(view, parts[i].cap ? capm[parts[i].cap - 1] : m[parts[i].k]);
        gfx_begin_3d(w.m, ox, oy, persp * S);
        if (parts[i].cap) {
            Color c = parts[i].cap == 1 ? rgba(91, 79, 196, 255) : rgba(58, 47, 143, 255);
            gfx_rrect(0, 0, TILE, TILE, r, color_alpha(c, a));
            gfx_rrect_outline(0, 0, TILE, TILE, r, 1, rgba(0, 0, 0, (u8)(0.28f * 255 * a)));
            if (g > 0) cube_glass(1, TILE, r, g, false);
        } else {
            tile_face(parts[i].k, parts[i].k == 0, a, g, la, r, letters[parts[i].k]);
        }
    }
    gfx_end_3d();
}
