/* doodle.c - Doodle: draw on a photo with the stick, then save it as a new
 * photo (the original is never changed). */
#include "app.h"
#include "photos.h"
#include "picture.h"
#include "games.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POINTS 8000
#define MAX_STROKES 400
typedef struct { float x, y; } Pt;                     /* 0..1 across the photo */
typedef struct { int first, count; uint8_t color, size; } Stroke;

static PicRequest req;
static int photo;
static Pt pts[MAX_POINTS];
static int npts;
static Stroke strokes[MAX_STROKES];
static int nstrokes;
static bool drawing;
static float cur_x = 0.5f, cur_y = 0.5f;
static int color = 1, size = 1;
static int saving;            /* frames until saving (so "Saving..." shows first) */
static float saved_flash = -1;

static const Color colors[8] = {
    { 255, 255, 255, 255 }, { 0xff, 0xe1, 0x4d, 255 }, { 0xff, 0x6f, 0xa8, 255 }, { 0x7d, 0xf0, 0xff, 255 },
    { 0x5f, 0xe3, 0x8a, 255 }, { 0xff, 0x55, 0x45, 255 }, { 0x4b, 0x7b, 0xff, 255 }, { 0x14, 0x14, 0x18, 255 },
};
static const float sizes[3] = { 0.006f, 0.013f, 0.026f };   /* brush width, as a part of the photo's width */

static void doodle_enter(int arg) {
    photo = arg;
    photos_load_picture(&req, photo, 1280);
    npts = nstrokes = 0;
    drawing = false;
    cur_x = cur_y = 0.5f;
    saved_flash = -1;
}

static void doodle_leave(void) { photos_release_picture(&req); }

/* the canvas: the photo's shape, as large as fits */
static void canvas(float *x, float *y, float *w, float *h) {
    float pw = 4, ph = 3;
    if (req.state == 2) { pw = req.pic.w; ph = req.pic.h; }
    float mw = UI_W - 236, mh = 290, s = fminf(mw / pw, mh / ph);
    *w = pw * s; *h = ph * s;
    *x = UI_X + 44 + (mw - *w) / 2;
    *y = 62 + (mh - *h) / 2;
}

static void save(void) {
    if (req.state != 2 || !npts) { sound_play(SFX_ERROR); app_toast(npts ? "The photo isn't ready yet." : "Draw something first."); return; }
    uint8_t *rgb = game_picture_rgb(&req.pic);
    if (!rgb) { sound_play(SFX_ERROR); app_toast("Not enough memory to save."); return; }
    int W = req.pic.w, H = req.pic.h;
    /* paint each stroke as round-ended lines */
    for (int s = 0; s < nstrokes; s++) {
        Stroke *st = &strokes[s];
        Color c = colors[st->color];
        float r = sizes[st->size] * W * 0.5f;
        for (int k = 0; k < st->count; k++) {
            Pt a = pts[st->first + k], b = pts[st->first + (k + 1 < st->count ? k + 1 : k)];
            float ax = a.x * W, ay = a.y * H, bx = b.x * W, by = b.y * H;
            int x0 = (int)(fminf(ax, bx) - r - 1), x1 = (int)(fmaxf(ax, bx) + r + 1);
            int y0 = (int)(fminf(ay, by) - r - 1), y1 = (int)(fmaxf(ay, by) + r + 1);
            float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
            for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < H; y++)
                for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < W; x++) {
                    float t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
                    t = clamp01(t);
                    float qx = ax + dx * t - x, qy = ay + dy * t - y;
                    float d = sqrtf(qx * qx + qy * qy);
                    float cov = clamp01(r + 0.5f - d);
                    if (cov <= 0) continue;
                    uint8_t *p = rgb + ((size_t)y * W + x) * 3;
                    p[0] = (uint8_t)(p[0] + (c.r - p[0]) * cov);
                    p[1] = (uint8_t)(p[1] + (c.g - p[1]) * cov);
                    p[2] = (uint8_t)(p[2] + (c.b - p[2]) * cov);
                }
        }
    }
    char msg[96];
    int idx = game_save_rgb(rgb, W, H, "DOOD", msg, sizeof(msg));
    free(rgb);
    sound_play(idx >= 0 ? SFX_CONFIRM : SFX_ERROR);
    app_toast(msg);
    if (idx >= 0) saved_flash = 0;
}

static void doodle_update(void) {
    if (saved_flash >= 0) saved_flash += g_dt;
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_go(SCR_PLAYTIME, 0); return; }
    if (saving > 0) { if (--saving == 0) save(); return; }
    if (input_pressed(BTN_START)) { saving = 2; return; }
    if (input_pressed(BTN_X)) { color = (color + 1) % 8; sound_play(SFX_HOVER); }
    if (input_pressed(BTN_Y)) { size = (size + 1) % 3; sound_play(SFX_HOVER); }
    if (input_pressed(BTN_L) && nstrokes > 0 && !drawing) {
        nstrokes--;
        npts = strokes[nstrokes].first;
        sound_play(SFX_BACK);
    }
    if (input_pressed(BTN_R) && !drawing && nstrokes > 0) {   /* clear: R twice quickly */
        static float last_r = -10;
        if (g_time - last_r < 0.6f) { nstrokes = npts = 0; sound_play(SFX_CLOSE); app_toast("Cleared"); }
        else app_toast("Press R again to clear the drawing");
        last_r = g_time;
    }
    float cx, cy, cw, ch;
    canvas(&cx, &cy, &cw, &ch);
    /* move: stick (faster when pushed further), D-pad for small steps */
    float sx = input_stick_x(), sy = input_stick_y();
    float speed = 260.0f * (0.25f + 0.75f * fmaxf(fabsf(sx), fabsf(sy)));
    cur_x += sx * speed * g_dt / cw;
    cur_y += sy * speed * g_dt / ch;
    if (input_repeat(BTN_LEFT)) cur_x -= 1.5f / cw;
    if (input_repeat(BTN_RIGHT)) cur_x += 1.5f / cw;
    if (input_repeat(BTN_UP)) cur_y -= 1.5f / ch;
    if (input_repeat(BTN_DOWN)) cur_y += 1.5f / ch;
    cur_x = clamp01(cur_x);
    cur_y = clamp01(cur_y);
    if (input_held(BTN_A) && req.state == 2) {
        if (!drawing) {
            if (nstrokes < MAX_STROKES && npts < MAX_POINTS) {
                drawing = true;
                strokes[nstrokes].first = npts;
                strokes[nstrokes].count = 0;
                strokes[nstrokes].color = (uint8_t)color;
                strokes[nstrokes].size = (uint8_t)size;
                nstrokes++;
            } else if (input_pressed(BTN_A)) {
                app_toast("The drawing is full. Save it or press L to undo.");
            }
        }
        if (drawing && npts < MAX_POINTS) {
            Stroke *st = &strokes[nstrokes - 1];
            bool add = st->count == 0;
            if (!add) {
                Pt l = pts[npts - 1];
                float dx = (cur_x - l.x) * cw, dy = (cur_y - l.y) * ch;
                add = dx * dx + dy * dy > 2.0f;
            }
            if (add) { pts[npts].x = cur_x; pts[npts].y = cur_y; npts++; st->count++; }
        }
    } else {
        drawing = false;
    }
}

static void doodle_draw(float open) {
    (void)open;
    float X = UI_X;
    ui_background_grid(1);
    ui_panel(X + 22, 18, UI_W - 44, 400, 1);
    Photo *p = photos_get(photo);
    ui_ptitle(X + 22, 18, UI_W - 44, "Doodle", NULL, 1);
    if (p) ui_text_fit_scroll(FONT_MENU, 15, 1.6f, X + UI_W - 260, 27, 222, 15, COL_SOFT, p->name);
    float cx, cy, cw, ch;
    canvas(&cx, &cy, &cw, &ch);
    gfx_rect(cx - 8, cy - 8, cw + 16, ch + 16, rgba(200, 208, 220, 153));
    gfx_rect(cx - 6, cy - 6, cw + 12, ch + 12, rgba(10, 13, 16, 255));
    if (req.state == 2 && pic_ok(&req.pic)) {
        pic_draw(&req.pic, cx, cy, cw, ch, 0, 0, req.pic.w, req.pic.h, rgba(255, 255, 255, 255));
        gfx_set_clip(cx, cy, cw, ch);
        for (int s = 0; s < nstrokes; s++) {
            Stroke *st = &strokes[s];
            Color c = colors[st->color];
            float w = sizes[st->size] * cw;
            for (int k = 0; k < st->count; k++) {
                Pt a = pts[st->first + k];
                float ax = cx + a.x * cw, ay = cy + a.y * ch;
                if (k + 1 < st->count) {
                    Pt b = pts[st->first + k + 1];
                    gfx_line(ax, ay, cx + b.x * cw, cy + b.y * ch, w, c);
                }
                gfx_circle(ax, ay, w / 2, c);
            }
        }
        gfx_clear_clip();
    } else if (req.state == 3) {
        ui_text_center(FONT_MENU, 13, 1, cx + cw / 2, cy + ch / 2 - 12, 24, rgba(0xdf, 0xe4, 0xff, 255), req.why);
    } else {
        ui_spinner(cx + cw / 2, cy + ch / 2, 14, g_time, 1);
    }
    /* the brush */
    float bx = cx + cur_x * cw, by = cy + cur_y * ch, br = fmaxf(3, sizes[size] * cw / 2);
    gfx_ring(bx, by, br + 2, 1.5f, 0, 6.2832f, rgba(0, 0, 0, 200));
    gfx_ring(bx, by, br + 1, 1.5f, 0, 6.2832f, colors[color]);
    gfx_line(bx - br - 8, by, bx - br - 3, by, 1.5f, rgba(255, 255, 255, 220));
    gfx_line(bx + br + 3, by, bx + br + 8, by, 1.5f, rgba(255, 255, 255, 220));
    gfx_line(bx, by - br - 8, bx, by - br - 3, 1.5f, rgba(255, 255, 255, 220));
    gfx_line(bx, by + br + 3, bx, by + br + 8, 1.5f, rgba(255, 255, 255, 220));

    /* tools */
    float tx = X + UI_W - 170, ty = 62;
    ui_text(FONT_MENU, 13, 1.5f, tx, ty, 20, COL_TITLE, "Color");
    for (int i = 0; i < 8; i++) {
        float sx = tx + (i % 4) * 32 + 12, sy = ty + 38 + (i / 4) * 32;
        if (i == color) gfx_circle(sx, sy, 14, rgba(0xe8, 0xcf, 0x4a, 255));
        gfx_circle(sx, sy, 11, rgba(10, 12, 16, 255));
        gfx_circle(sx, sy, 10, colors[i]);
    }
    ui_text(FONT_MENU, 13, 1.5f, tx, ty + 100, 20, COL_TITLE, "Brush");
    for (int i = 0; i < 3; i++) {
        float sx = tx + 18 + i * 42, sy = ty + 140;
        if (i == size) gfx_rrect_outline(sx - 16, sy - 16, 32, 32, 4, 2, rgba(0xe8, 0xcf, 0x4a, 255));
        gfx_circle(sx, sy, 2.5f + i * 3.5f, rgba(0xee, 0xf1, 0xf6, 255));
    }
    ui_text(FONT_MENU, 12, 1.2f, tx, ty + 170, 20, COL_SOFT, "L  Undo");
    ui_text(FONT_MENU, 12, 1.2f, tx, ty + 190, 20, COL_SOFT, "R R  Clear");
    ui_text(FONT_MENU, 12, 1.2f, tx, ty + 210, 20, COL_SOFT, "START  Save as new");
    if (npts >= MAX_POINTS - 50) ui_text(FONT_MENU, 12, 1.2f, tx, ty + 236, 20, rgba(0xff, 0x8c, 0xa0, 255), "Drawing is full");

    ui_desc(X + 32, 366, UI_W - 64, 42, 1);
    ui_text_fit_scroll(FONT_MENU, 13, 1.2f, X + 44, 366, UI_W - 88, 42, rgba(0xec, 0xeb, 0xff, 255),
            "Move with the stick, hold A to draw. The doodle is saved as a new photo.");
    const char *h[] = { "S:Move", "A:Draw", "X:Color", "Y:Brush", "ST:Save", "B:Back" };
    ui_hints(h, 6, 1, input_stick_dir());
    if (saving > 0) ui_message("Saving", "Saving your doodle as a new photo...", 1);
}

const Screen screen_doodle = { doodle_enter, doodle_update, doodle_draw, doodle_leave };
