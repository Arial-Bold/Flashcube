/* viewer.c - one photo at a time, in a frame shaped like the photo, with
 * the photo menu on the left and a film strip of its neighbours on the
 * right. Zoom with L/R, move around with the stick, next/previous with the
 * D-pad, Z for the whole screen. */
#include "app.h"
#include "photos.h"
#include "picture.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { ROW_ZOOM, ROW_ROTATE, ROW_INFO, ROW_FAV, ROW_SLIDESHOW, ROW_FUN, NROWS };
static const char *row_names[NROWS] = { "Zoom", "Rotate", "Information", "Favorite", "Slideshow", "Fun" };

static PicRequest req;
static int row;
static float zoom = 1, zoom_target = 1;
static float pan_x, pan_y;            /* centre of the view, 0..1 of the picture */
static bool info_on, full;
static float full_k;                  /* 0 framed .. 1 full screen (animated) */
static float change_t;                /* since the photo changed */
static int shown_photo = -1;

/* quarter turns chosen this session, by photo key */
#define ROTS 64
static struct { uint32_t key; int q; } rots[ROTS];
static int rot_of(uint32_t key) {
    for (int i = 0; i < ROTS; i++) if (rots[i].key == key) return rots[i].q;
    return 0;
}
static void rot_set(uint32_t key, int q) {
    int free_i = -1;
    for (int i = 0; i < ROTS; i++) {
        if (rots[i].key == key) { rots[i].q = q; return; }
        if (free_i < 0 && !rots[i].key) free_i = i;
    }
    if (free_i < 0) free_i = (int)(key % ROTS);
    rots[free_i].key = key;
    rots[free_i].q = q;
}

static void load_current(void) {
    if (g_view_pos < 0 || g_view_pos >= g_view_count) return;
    int p = g_view[g_view_pos];
    if (p == shown_photo) return;
    shown_photo = p;
    photos_load_picture(&req, p, 2048);
    zoom = zoom_target = 1;
    pan_x = pan_y = 0.5f;
    change_t = 0;
}

static void viewer_enter(int arg) {
    (void)arg;
    shown_photo = -1;
    row = 0;
    info_on = false;
    full = false;
    full_k = 0;
    load_current();
}

static void viewer_leave(void) {
    photos_release_picture(&req);
    shown_photo = -1;
}

static Photo *current(void) { return (g_view_pos >= 0 && g_view_pos < g_view_count) ? photos_get(g_view[g_view_pos]) : NULL; }

/* displayed size of the photo (after any turn) */
static void photo_size(float *w, float *h) {
    Photo *p = current();
    float pw = 4, ph = 3;
    if (req.state == 2 && req.photo == shown_photo) { pw = (float)req.pic.w; ph = (float)req.pic.h; }
    else if (p && p->w && p->h) { pw = p->w; ph = p->h; }
    int q = p ? rot_of(p->key) : 0;
    if (q & 1) { float t = pw; pw = ph; ph = t; }
    *w = pw; *h = ph;
}

/* the frame: the photo's shape, as large as fits */
static void frame_rect(float *x, float *y, float *w, float *h) {
    float pw, ph;
    photo_size(&pw, &ph);
    float mw = UI_W - 296, mh = 336, cx = UI_X + 150 + (UI_W - 268) / 2, top = 22;
    float s = fminf(mw / pw, mh / ph);
    float fw = pw * s, fh = ph * s;
    float fx = cx - fw / 2, fy = top + (mh - fh) / 2;
    /* full screen */
    float ax, ay, aw, ah;   /* full screen: the whole visible picture, margin or not */
    gfx_full_area(&ax, &ay, &aw, &ah);
    float S = fminf(aw / pw, ah / ph);
    float Fw = pw * S, Fh = ph * S, Fx = ax + (aw - Fw) / 2, Fy = ay + (ah - Fh) / 2;
    float k = ease_inout(full_k);
    *x = lerpf(fx, Fx, k); *y = lerpf(fy, Fy, k); *w = lerpf(fw, Fw, k); *h = lerpf(fh, Fh, k);
}

static void step(int d) {
    int np = g_view_pos + d;
    if (np < 0 || np >= g_view_count) { sound_play(SFX_ERROR); return; }
    g_view_pos = np;
    sound_play(SFX_HOVER);
    load_current();
}

static void viewer_update(void) {
    change_t += g_dt;
    Photo *p = current();
    if (input_pressed(BTN_B)) {
        if (full) { full = false; sound_play(SFX_BACK); return; }
        if (info_on) { info_on = false; sound_play(SFX_BACK); return; }
        sound_play(SFX_BACK);
        app_go(SCR_ALBUM, 0);
        return;
    }
    if (input_repeat(BTN_LEFT)) step(-1);
    if (input_repeat(BTN_RIGHT)) step(1);
    if (!full) {
        if (input_repeat(BTN_UP) && row > 0) { row--; sound_play(SFX_HOVER); }
        if (input_repeat(BTN_DOWN) && row < NROWS - 1) { row++; sound_play(SFX_HOVER); }
    }
    if (input_pressed(BTN_Z)) { full = !full; sound_play(full ? SFX_OPEN : SFX_CLOSE); }
    if (input_pressed(BTN_A) && p) {
        sound_play(SFX_SELECT);
        switch (row) {
        case ROW_ZOOM: zoom_target = zoom_target < 1.5f ? 2 : zoom_target < 3 ? 4 : 1; break;
        case ROW_ROTATE: rot_set(p->key, (rot_of(p->key) + 1) & 3); zoom_target = 1; pan_x = pan_y = 0.5f; break;
        case ROW_INFO: info_on = !info_on; break;
        case ROW_FAV:
            photos_set_fav(g_view[g_view_pos], !p->fav);
            app_toast(p->fav ? "Added to Favorites" : "Removed from Favorites");
            break;
        case ROW_SLIDESHOW: app_go(SCR_SLIDESHOW, 1000 + g_view_pos); break;
        case ROW_FUN: g_pick_for = PICK_NONE; app_go(SCR_PLAYTIME, 1000 + g_view[g_view_pos]); break;
        }
    }
    /* zoom: R closer, L further (analogue or click) */
    float zr = input_trigger_r() + (input_held(BTN_R) ? 1 : 0), zl = input_trigger_l() + (input_held(BTN_L) ? 1 : 0);
    if (zr > 0.1f) zoom_target *= 1 + g_dt * 1.6f * fminf(zr, 1);
    if (zl > 0.1f) zoom_target /= 1 + g_dt * 1.6f * fminf(zl, 1);
    if (zoom_target < 1) zoom_target = 1;
    if (zoom_target > 4) zoom_target = 4;
    zoom += (zoom_target - zoom) * clamp01(g_dt * 10);
    /* move around (stick or C-stick) */
    float mx = input_stick_x() + input_cstick_x(), my = input_stick_y() + input_cstick_y();
    if (zoom > 1.01f) {
        pan_x += mx * g_dt * 0.6f / zoom;
        pan_y += my * g_dt * 0.6f / zoom;
    }
    float half = 0.5f / zoom;
    pan_x = fminf(fmaxf(pan_x, half), 1 - half);
    pan_y = fminf(fmaxf(pan_y, half), 1 - half);
    full_k = clamp01(full_k + (full ? g_dt : -g_dt) / 0.3f);
    app_prefetch_view(g_view_pos, 3);
}

static void draw_photo(float fx, float fy, float fw, float fh, float a) {
    Photo *p = current();
    if (!p) return;
    int q = rot_of(p->key);
    bool ready = req.state == 2 && req.photo == shown_photo && pic_ok(&req.pic);
    float fade = ready ? clamp01(change_t / 0.25f) : 0;
    gfx_set_clip(fx, fy, fw, fh);
    /* the thumbnail first (instantly), then the real photo over it */
    GfxImage *t = photos_thumb(g_view[g_view_pos]);
    if (t && fade < 1) {
        float pw, ph;
        photo_size(&pw, &ph);
        app_draw_thumb(g_view[g_view_pos], fx, fy, fw, fh, a);
    }
    if (ready) {
        float pw = req.pic.w, ph = req.pic.h;
        /* view size in picture pixels (upright picture, before turning) */
        float dw = (q & 1) ? fh : fw, dh = (q & 1) ? fw : fh;
        float s = fminf(dw / pw, dh / ph) * zoom;
        float cx = fx + fw / 2, cy = fy + fh / 2;
        /* centre on (pan_x, pan_y) of the displayed (turned) picture */
        float ox = (0.5f - pan_x) * ((q & 1) ? ph : pw) * s, oy = (0.5f - pan_y) * ((q & 1) ? pw : ph) * s;
        pic_draw_rotated(&req.pic, cx + ox, cy + oy, pw * s, ph * s, q * 90.0f, rgba(255, 255, 255, (u8)(255 * a * fade)));
    }
    gfx_clear_clip();
    if (!ready && req.state != 3) ui_spinner(fx + fw - 18, fy + fh - 18, 7, g_time, a * 0.9f);
    if (req.state == 3 && req.photo == shown_photo) {
        ui_desc(fx + 10, fy + fh / 2 - 20, fw - 20, 40, a);
        ui_text_center(FONT_MENU, 13, 1, fx + fw / 2, fy + fh / 2 - 20, 40, rgba(0xec, 0xeb, 0xff, (u8)(255 * a)), req.why);
    }
    /* where we are, when zoomed in */
    if (zoom > 1.05f && ready) {
        float mw = 54, mh = 54, pw, ph;
        photo_size(&pw, &ph);
        if (pw > ph) mh = 54 * ph / pw; else mw = 54 * pw / ph;
        float mx = fx + fw - mw - 6, my = fy + fh - mh - 6;
        gfx_rect(mx, my, mw, mh, rgba(10, 10, 20, (u8)(140 * a)));
        gfx_rrect_outline(mx - 2, my - 2, mw + 4, mh + 4, 1, 2, rgba(220, 228, 240, (u8)(200 * a)));
        float vw = mw / zoom, vh = mh / zoom;
        float vx = mx + pan_x * mw - vw / 2, vy = my + pan_y * mh - vh / 2;
        gfx_rrect_outline(vx, vy, vw, vh, 1, 2, rgba(0xe8, 0xcf, 0x4a, (u8)(255 * a)));
    }
}

static void viewer_draw(float open) {
    (void)open;
    float X = UI_X;
    ui_background_grid(1);
    float k = ease_inout(full_k), ua = 1 - k;
    if (k > 0) {
        float ax, ay, aw, ah;
        gfx_full_area(&ax, &ay, &aw, &ah);
        gfx_rect(ax, ay, aw, ah, rgba(0, 0, 0, (u8)(255 * k)));
    }

    /* the photo menu */
    if (ua > 0) {
        ui_panel(X + 20, 18, 120, 240, ua);
        ui_ptitle(X + 20, 18, 120, "Photo", NULL, ua);
        Photo *p = current();
        for (int i = 0; i < NROWS; i++) {
            float y = 18 + 38 + i * 29;
            ui_row(X + 26, y, 108, 27, i == row, ua);
            const char *name = row_names[i];
            char buf[24];
            if (i == ROW_FAV && p && p->fav) { snprintf(buf, sizeof(buf), "Favorite \x84"); name = buf; }
            if (i == ROW_ZOOM && zoom_target > 1.01f) { snprintf(buf, sizeof(buf), "Zoom %d%%", (int)(zoom_target * 100 + 0.5f)); name = buf; }
            ui_text(FONT_MENU, 13, 1.4f, X + 36, y, 27, rgba(0xee, 0xf1, 0xf6, (u8)(255 * ua)), name);
        }
    }

    /* the frame */
    float fx, fy, fw, fh;
    frame_rect(&fx, &fy, &fw, &fh);
    if (ua > 0) {
        gfx_shadow(fx - 5, fy + 5, fw + 10, fh + 10, 2, 14, rgba(0, 0, 0, (u8)(130 * ua)));
        gfx_rect(fx - 8, fy - 8, fw + 16, fh + 16, rgba(200, 208, 220, (u8)(0.6f * 255 * ua)));
        gfx_rect(fx - 6, fy - 6, fw + 12, fh + 12, rgba(10, 13, 16, (u8)(255 * ua)));
        gfx_rect(fx - 5, fy - 5, fw + 10, fh + 10, rgba(0x0b, 0x0f, 0x12, (u8)(255 * ua)));
    }
    draw_photo(fx, fy, fw, fh, 1);

    /* film strip */
    if (ua > 0) {
        ui_panel(X + UI_W - 102, 18, 82, 340, ua);
        for (int k2 = -2; k2 <= 2; k2++) {
            int i = g_view_pos + k2;
            float y = 18 + 10 + (k2 + 2) * 63;
            if (i < 0 || i >= g_view_count) continue;
            gfx_rect(X + UI_W - 93 - 1.5f, y - 1.5f, 64 + 3, 54 + 3, rgba(190, 200, 215, (u8)(0.45f * 255 * ua)));
            gfx_rect(X + UI_W - 93, y, 64, 54, rgba(0x0b, 0x0f, 0x12, (u8)(255 * ua)));
            app_draw_thumb(g_view[i], X + UI_W - 91, y + 2, 60, 50, ua);
            if (k2 == 0) {
                gfx_rrect_outline(X + UI_W - 93 - 3.5f, y - 3.5f, 64 + 7, 54 + 7, 2, 2.5f, rgba(0xe8, 0xcf, 0x4a, (u8)(255 * ua)));
            }
        }
    }

    /* description */
    Photo *p = current();
    if (ua > 0 && p) {
        ui_desc(X + 20, 376, UI_W - 40, 34, ua);
        char left[96], right[96], date[24];
        if (zoom > 1.01f) snprintf(left, sizeof(left), "%s \x7f Zoom %d%%", p->name, (int)(zoom * 100 + 0.5f));
        else snprintf(left, sizeof(left), "%s", p->name);
        photos_format_date(p, date, sizeof(date));
        if (p->w) snprintf(right, sizeof(right), "%s \x7f %d \x80 %d \x7f %d of %d", date, p->w, p->h, g_view_pos + 1, g_view_count);
        else snprintf(right, sizeof(right), "%s \x7f %d of %d", date, g_view_pos + 1, g_view_count);
        ui_desc_texts(X + 20, 376, UI_W - 40, 34, left, right, ua);
    }

    /* information */
    if (info_on && p && ua > 0) {
        float iw = 324, ih = 196, ix = X + 150 + (UI_W - 268) / 2 - iw / 2, iy = 60;
        ui_panel(ix, iy, iw, ih, ua);
        ui_ptitle(ix, iy, iw, "Information", NULL, ua);
        char v[6][160], date[24];
        photos_format_date(p, date, sizeof(date));
        const Folder *f = photos_folder(p->folder);
        snprintf(v[0], 160, "%s", p->name);   /* (long names scroll) */
        snprintf(v[1], 160, "%s", p->date[0] ? p->date : date);
        snprintf(v[2], 160, "%s", req.state == 2 && req.pic.info.camera[0] ? req.pic.info.camera : "-");
        if (p->w) snprintf(v[3], 160, "%d \x80 %d", p->w, p->h); else snprintf(v[3], 160, "-");
        if (p->size >= 1048576) snprintf(v[4], 160, "%.1f MB", p->size / 1048576.0);
        else snprintf(v[4], 160, "%u KB", (unsigned)((p->size + 1023) / 1024));
        snprintf(v[5], 160, "%s", f ? f->name : "-");
        static const char *lab[6] = { "Name", "Taken", "Camera", "Size", "File", "Folder" };
        for (int i = 0; i < 6; i++) {
            float y = iy + 40 + i * 25;
            ui_text(FONT_MENU, 13, 1.2f, ix + 16, y, 22, rgba(0x73, 0x92, 0xff, (u8)(255 * ua)), lab[i]);
            ui_text_fit_scroll(FONT_MENU, 13, 1, ix + 100, y, iw - 116, 22, rgba(0xee, 0xf1, 0xf6, (u8)(255 * ua)), v[i]);
        }
    }

    const char *h[] = { "S:Move", "LR:Zoom", "DP:Next", "B:Back", "A:Confirm" };
    ui_hints(h, 5, ua, input_stick_dir());
}

const Screen screen_viewer = { viewer_enter, viewer_update, viewer_draw, viewer_leave };
