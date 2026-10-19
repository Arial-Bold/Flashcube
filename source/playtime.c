/* playtime.c - the Playtime side: three games played with your photos
 * (Photo Puzzle, Doodle, Color Looks), and the colour "looks" they share. */
#include "app.h"
#include "photos.h"
#include "pixels.h"
#include "looks.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "cube.h"
#include "m3d.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

const char *const look_names[LOOK_COUNT] = { "Original", "Sepia", "Black & White", "Pop", "Cool", "Warm", "Vintage", "Negative" };
const char *const look_help[LOOK_COUNT] = {
    "The photo as it was taken.", "Warm brown tones, like an old print.", "Shades of grey only.",
    "Bolder, brighter colours.", "A cool, blue evening light.", "A warm, golden afternoon light.",
    "Faded colours with a soft glow, like a photo from the 70s.", "Light and dark swapped, like film negatives.",
};

static inline int clampi(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

void look_apply(int look, uint8_t *pr, uint8_t *pg, uint8_t *pb) {
    int r = *pr, g = *pg, b = *pb;
    int y = (r * 77 + g * 150 + b * 29) >> 8;
    switch (look) {
    case LOOK_SEPIA: r = clampi(y * 112 / 100 + 20); g = clampi(y * 95 / 100 + 8); b = clampi(y * 72 / 100); break;
    case LOOK_MONO: r = g = b = clampi((y - 128) * 110 / 100 + 128); break;
    case LOOK_POP:
        r = clampi(y + (r - y) * 180 / 100); g = clampi(y + (g - y) * 180 / 100); b = clampi(y + (b - y) * 180 / 100);
        r = clampi((r - 128) * 112 / 100 + 132); g = clampi((g - 128) * 112 / 100 + 132); b = clampi((b - 128) * 112 / 100 + 132);
        break;
    case LOOK_COOL: r = clampi(r * 85 / 100); g = clampi(g * 97 / 100 + 4); b = clampi(b * 112 / 100 + 18); break;
    case LOOK_WARM: r = clampi(r * 110 / 100 + 14); g = clampi(g * 102 / 100 + 4); b = clampi(b * 82 / 100); break;
    case LOOK_VINTAGE:
        r = clampi(y + (r - y) * 60 / 100); g = clampi(y + (g - y) * 60 / 100); b = clampi(y + (b - y) * 60 / 100);
        r = clampi(r * 80 / 100 + 48); g = clampi(g * 78 / 100 + 36); b = clampi(b * 70 / 100 + 40);
        break;
    case LOOK_NEGATIVE: r = 255 - r; g = 255 - g; b = 255 - b; break;
    default: break;
    }
    *pr = (uint8_t)r; *pg = (uint8_t)g; *pb = (uint8_t)b;
}

/* ------------------------------------------------------------ the cards */

static int photo = -1;        /* the photo to play with (or -1: pick one) */
static int card;
static GfxImage looks_prev[3];  /* sepia, black & white, pop versions of the preview photo */
static int looks_prev_for = -1;
static const int prev_looks[3] = { LOOK_SEPIA, LOOK_MONO, LOOK_POP };

static int preview_photo(void) {
    if (photo >= 0) return photo;
    return photos_count() > 0 ? 0 : -1;    /* the newest */
}

static void make_look_previews(void) {
    int p = preview_photo();
    if (p == looks_prev_for || p < 0) return;
    GfxImage *t = photos_thumb(p);
    if (!t) return;
    for (int k = 0; k < 3; k++) {
        gfx_image_free(&looks_prev[k]);
        uint16_t *d = memalign(32, THUMB_BYTES);
        if (!d) return;
        const uint16_t *s = t->data;
        for (int i = 0; i < THUMB_W * THUMB_H; i++) {
            int r, g, b;
            unrgb565(s[i], &r, &g, &b);
            uint8_t R = (uint8_t)r, G = (uint8_t)g, B = (uint8_t)b;
            look_apply(prev_looks[k], &R, &G, &B);
            d[i] = rgb565(R, G, B);
        }
        gfx_image_init(&looks_prev[k], d, THUMB_W, THUMB_H, THUMB_W, THUMB_H, GFX_RGB565);
    }
    looks_prev_for = p;
}

static void play_enter(int arg) {
    photo = arg >= 1000 ? arg - 1000 : -1;
    if (arg == 0) card = card < 0 || card > 2 ? 0 : card;
}

static void play_leave(void) {
    for (int k = 0; k < 3; k++) gfx_image_free(&looks_prev[k]);
    looks_prev_for = -1;
}

static const ScreenId card_screen[3] = { SCR_PUZZLE, SCR_DOODLE, SCR_LOOKS };

static void play_update(void) {
    int d = input_dir();
    if (d == DIR_LEFT && card > 0) { card--; sound_play(SFX_HOVER); }
    if (d == DIR_RIGHT && card < 2) { card++; sound_play(SFX_HOVER); }
    if (input_pressed(BTN_A)) {
        if (photos_count() == 0) { sound_play(SFX_ERROR); app_toast("Put some photos on the SD card first."); return; }
        sound_play(SFX_SELECT);
        if (photo >= 0) app_go(card_screen[card], photo);
        else { g_pick_for = (int)card_screen[card]; app_go(SCR_ALBUM, 0); }
    } else if (input_pressed(BTN_B)) {
        sound_play(SFX_BACK);
        if (photo >= 0) app_go(SCR_VIEWER, 0);
        else app_back_to_cube(SIDE_PLAYTIME);
    }
}

static void card_picture(int k, float x, float y, float w, float h, float a) {
    int p = preview_photo();
    gfx_rect(x, y, w, h, rgba(0x0b, 0x0f, 0x12, (u8)(255 * a)));
    if (p < 0) return;
    app_draw_thumb(p, x, y, w, h, a);
    if (k == 0) {
        /* a 4x4 puzzle with one tile missing */
        for (int i = 1; i < 4; i++) {
            gfx_rect(x + w * i / 4 - 1, y, 2, h, rgba(0, 0, 0, (u8)(220 * a)));
            gfx_rect(x, y + h * i / 4 - 1, w, 2, rgba(0, 0, 0, (u8)(220 * a)));
        }
        gfx_rect(x + w * 3 / 4, y + h * 3 / 4, w / 4, h / 4, rgba(0x0b, 0x0f, 0x12, (u8)(255 * a)));
    } else if (k == 1) {
        /* a few doodles */
        Color yel = rgba(0xff, 0xe1, 0x4d, (u8)(255 * a)), pink = rgba(0xff, 0x6f, 0xa8, (u8)(255 * a)), cyan = rgba(0x7d, 0xf0, 0xff, (u8)(255 * a));
        float px = 0, py = 0;
        for (int i = 0; i <= 16; i++) {
            float t = i / 16.0f, qx = x + w * (0.12f + 0.4f * t), qy = y + h * (0.24f + 0.07f * sinf(t * 6.28f * 1.5f));
            if (i) gfx_line(px, py, qx, qy, 3.5f, yel);
            gfx_circle(qx, qy, 1.75f, yel);
            px = qx; py = qy;
        }
        float cx = x + w * 0.72f, cy = y + h * 0.3f, R = w * 0.1f;
        for (int i = 0; i < 10; i++) {
            float a0 = -1.5708f + i * 0.6283f, a1 = a0 + 0.6283f;
            float r0 = (i & 1) ? R * 0.45f : R, r1 = (i & 1) ? R : R * 0.45f;
            gfx_line(cx + cosf(a0) * r0, cy + sinf(a0) * r0, cx + cosf(a1) * r1, cy + sinf(a1) * r1, 3, pink);
        }
        for (int i = 0; i <= 16; i++) {
            float t = i / 16.0f, qx = x + w * (0.14f + 0.72f * t), qy = y + h * (0.84f + 0.05f * sinf(t * 6.28f * 1.2f));
            if (i) gfx_line(px, py, qx, qy, 3.5f, cyan);
            px = qx; py = qy;
        }
    } else {
        make_look_previews();
        if (looks_prev_for == p) {
            /* three stripes: sepia, black & white, pop */
            for (int s = 0; s < 3; s++) {
                GfxImage *img = &looks_prev[s];
                if (!img->data) continue;
                float tw = THUMB_W, th = THUMB_H, cell = w / h, u0 = 0, v0 = 0, u1 = tw, v1 = th;
                if (tw / th > cell) { float cw = th * cell; u0 = (tw - cw) / 2; u1 = u0 + cw; }
                else { float ch = tw / cell; v0 = (th - ch) / 2; v1 = v0 + ch; }
                float su0 = u0 + (u1 - u0) * s / 3, su1 = u0 + (u1 - u0) * (s + 1) / 3;
                gfx_image_draw_part(img, x + w * s / 3, y, w / 3 + 0.5f, h, su0, v0, su1, v1, rgba(255, 255, 255, (u8)(255 * a)));
            }
        }
    }
}

static void play_draw(float open) {
    float X = UI_X;
    ui_background_grid(ramp(open, 0, 0.35f));
    float pa = ramp(open, 0.45f, 0.85f);
    ui_panel(X + 22, 18, UI_W - 44, 400, pa);
    ui_ptitle(X + 22, 18, UI_W - 44, "Playtime", photo >= 0 ? "Pick a game for this photo" : "Pick a game", pa);
    static const char *names[3] = { "Photo Puzzle", "Doodle", "Color Looks" };
    static const char *subs[3] = { "Slide tiles back", "Draw on a photo", "Sepia, mono, pop..." };
    static const char *help[3] = { "Slide the tiles back into place to rebuild the photo.",
                                   "Draw on a photo with the stick, then save it as a new photo.",
                                   "Give a photo a new look and save it as a copy." };
    float done = ui_swirl_duration(3);
    for (int k = 0; k < 3; k++) {
        float gap = UI_W > 700 ? 44 : 12, x = X + UI_W / 2 - (3 * 172 + 2 * gap) / 2 + k * (172 + gap), y = 62, w = 172, h = 256;
        float a = 1;
        bool sw = open < done;
        if (sw) a = ui_swirl_begin(k, 3, x, y, w, h, open, 95, 20);
        bool on = k == card;
        if (on) {
            gfx_rrect_vgrad(x, y, w, h, 4, rgba(140, 160, 180, (u8)(0.34f * 255 * a)), rgba(70, 90, 110, (u8)(0.22f * 255 * a)));
            gfx_rrect_outline(x, y, w, h, 4, 1, rgba(215, 225, 240, (u8)(0.6f * 255 * a)));
        } else {
            gfx_rrect_vgrad(x, y, w, h, 4, rgba(140, 160, 180, (u8)(0.18f * 255 * a)), rgba(70, 90, 110, (u8)(0.10f * 255 * a)));
            gfx_rrect_outline(x, y, w, h, 4, 1, rgba(215, 225, 240, (u8)(0.35f * 255 * a)));
        }
        gfx_rect(x + 6 - 1.5f, y + 6 - 1.5f, w - 12 + 3, 160 + 3, rgba(190, 200, 215, (u8)(0.5f * 255 * a)));
        card_picture(k, x + 6, y + 6, w - 12, 160, a);
        ui_text(FONT_MENU, 15, 1.5f, x + 10, y + 180, 20, rgba(0xee, 0xf1, 0xf6, (u8)(255 * a)), names[k]);
        ui_text(FONT_MENU, 12, 1.2f, x + 10, y + 206, 18, rgba(0xc9, 0xd2, 0xee, (u8)(255 * a)), subs[k]);
        if (on && !sw) ui_gold_ring(x, y, w, h, 0.5f + 0.5f * sinf(g_time * 4), ramp(open, done - 0.3f, done));
        if (sw) ui_swirl_end();
    }
    float da = ramp(open, done - 0.5f, done - 0.1f);
    float dy = 356 + 16 * (1 - ease_out(da));
    ui_desc(X + 32, dy, UI_W - 64, 42, da);
    ui_text_fit_scroll(FONT_MENU, 13, 1.2f, X + 44, dy, UI_W - 88, 42, rgba(0xec, 0xeb, 0xff, (u8)(255 * da)), help[card]);
    const char *h[] = { "S:Select", "B:Back", "A:Confirm" };
    ui_hints(h, 3, ramp(open, done - 0.4f, done), input_stick_dir());
}

const Screen screen_playtime = { play_enter, play_update, play_draw, play_leave };
