/* looks.c - Color Looks: give a photo a new colour look (sepia, black and
 * white, pop, ...) and save it as a copy. */
#include "app.h"
#include "photos.h"
#include "picture.h"
#include "pixels.h"
#include "games.h"
#include "looks.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "m3d.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PicRequest req;
static int photo, look, shown_look = -1, prev_look = -1;
static uint8_t *base;              /* the photo at preview size, RGB */
static int bw, bh;
static GfxImage tex[2];            /* [0] the look now, [1] the one before (for the wipe) */
static float wipe = 1;
static int saving;

static void looks_enter(int arg) {
    photo = arg;
    photos_load_picture(&req, photo, 1280);
    look = 1;
    shown_look = prev_look = -1;
    free(base);
    base = NULL;
    wipe = 1;
    saving = 0;
}

static void looks_leave(void) {
    photos_release_picture(&req);
    gfx_image_free(&tex[0]);
    gfx_image_free(&tex[1]);
    free(base);
    base = NULL;
}

/* the photo shrunk to fit the preview (box-averaged) */
static void make_base(void) {
    const Picture *p = &req.pic;
    float s = fminf(400.0f / p->w, 300.0f / p->h);
    if (s > 1) s = 1;
    bw = (int)(p->w * s); bh = (int)(p->h * s);
    if (bw < 4) bw = 4;
    if (bh < 4) bh = 4;
    base = malloc((size_t)bw * bh * 3);
    if (!base) return;
    int step = (int)(1 / s);
    if (step < 1) step = 1;
    if (step > 3) step = 3;
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int j = 0; j < step; j++)
                for (int i = 0; i < step; i++) {
                    uint8_t c[3];
                    pic_get_rgb(p, (int)(x / s) + i, (int)(y / s) + j, c);
                    r += c[0]; g += c[1]; b += c[2]; n++;
                }
            uint8_t *o = base + ((size_t)y * bw + x) * 3;
            o[0] = (uint8_t)(r / n); o[1] = (uint8_t)(g / n); o[2] = (uint8_t)(b / n);
        }
}

static void make_look(void) {
    int tw, th;
    uint16_t *t = tex_alloc565(bw, bh, &tw, &th);
    if (!t) return;
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            const uint8_t *s = base + ((size_t)y * bw + x) * 3;
            uint8_t r = s[0], g = s[1], b = s[2];
            look_apply(look, &r, &g, &b);
            t[tex_index(tw, x, y)] = rgb565(r, g, b);
        }
    gfx_image_free(&tex[1]);
    tex[1] = tex[0];
    memset(&tex[0], 0, sizeof(tex[0]));
    gfx_image_init(&tex[0], t, bw, bh, tw, th, GFX_RGB565);
    prev_look = shown_look;
    shown_look = look;
    wipe = prev_look < 0 ? 1 : 0;
}

static void look_px(void *ctx, int x, int y, uint8_t *rgb) {
    (void)x; (void)y;
    look_apply(*(int *)ctx, &rgb[0], &rgb[1], &rgb[2]);
}

static void looks_update(void) {
    if (req.state == 2 && !base) make_base();
    if (base && shown_look != look && wipe >= 1) make_look();
    wipe = clamp01(wipe + g_dt / 0.35f);
    if (saving > 0) {
        if (--saving == 0) {
            char msg[96];
            int lk = look;
            int idx = game_save_picture(&req.pic, "LOOK", lk ? look_px : NULL, &lk, msg, sizeof(msg));
            sound_play(idx >= 0 ? SFX_LOADED : SFX_ERROR);
            app_toast(msg);
        }
        return;
    }
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_go(SCR_PLAYTIME, 0); return; }
    int d = input_dir();
    if (d == DIR_UP && look > 0) { look--; sound_play(SFX_HOVER); }
    if (d == DIR_DOWN && look < LOOK_COUNT - 1) { look++; sound_play(SFX_HOVER); }
    if (input_pressed(BTN_A) || input_pressed(BTN_START)) {
        if (req.state != 2) { sound_play(SFX_ERROR); return; }
        if (look == LOOK_ORIGINAL) { sound_play(SFX_ERROR); app_toast("Pick a look to save first."); return; }
        sound_play(SFX_SELECT);
        saving = 2;
    }
}

static void looks_draw(float open) {
    (void)open;
    float X = UI_X;
    ui_background_grid(1);
    ui_panel(X + 20, 18, 160, 300, 1);
    ui_ptitle(X + 20, 18, 160, "Looks", NULL, 1);
    for (int i = 0; i < LOOK_COUNT; i++) {
        float y = 56 + i * 31;
        ui_row(X + 26, y, 148, 28, i == look, 1);
        ui_text(FONT_MENU, 13, 1.3f, X + 36, y, 28, COL_TEXT, look_names[i]);
    }
    /* the preview, in a frame the photo's shape */
    float fw = 400, fh = 300;
    if (base) { float s = fminf(400.0f / bw, 300.0f / bh); fw = bw * s; fh = bh * s; }
    float fx = X + 200 + (420 - fw) / 2, fy = 22 + (320 - fh) / 2;
    gfx_shadow(fx - 5, fy + 5, fw + 10, fh + 10, 2, 14, rgba(0, 0, 0, 130));
    gfx_rect(fx - 8, fy - 8, fw + 16, fh + 16, rgba(200, 208, 220, 153));
    gfx_rect(fx - 6, fy - 6, fw + 12, fh + 12, rgba(10, 13, 16, 255));
    if (tex[0].data) {
        if (wipe < 1 && tex[1].data) gfx_image_draw(&tex[1], fx, fy, fw, fh, rgba(255, 255, 255, 255));
        float k = ease_inout(wipe);
        gfx_image_draw_part(&tex[0], fx, fy, fw * k, fh, 0, 0, tex[0].w * k, tex[0].h, rgba(255, 255, 255, 255));
        if (wipe < 1) gfx_rect(fx + fw * k - 1, fy, 2, fh, rgba(255, 255, 255, 200));
    } else if (req.state == 3) {
        ui_text_center(FONT_MENU, 13, 1, fx + fw / 2, fy + fh / 2 - 12, 24, rgba(0xdf, 0xe4, 0xff, 255), req.why);
    } else {
        ui_spinner(fx + fw / 2, fy + fh / 2, 14, g_time, 1);
    }
    ui_desc(X + 20, 342, 600, 56, 1);
    ui_text(FONT_MENU_BOLD, 14, 1.3f, X + 32, 346, 22, rgba(0xec, 0xeb, 0xff, 255), look_names[look]);
    ui_text(FONT_MENU, 13, 1.2f, X + 32, 368, 24, rgba(0xc9, 0xc6, 0xff, 255), look_help[look]);
    const char *h[] = { "S:Select", "B:Back", "A:Save a Copy" };
    ui_hints(h, 3, 1, input_stick_dir());
    if (saving > 0) ui_message("Saving", "Saving a copy with this look...", 1);
}

const Screen screen_looks = { looks_enter, looks_update, looks_draw, looks_leave };
