/* settings_panel.c - START on the cube: screen, sound and album settings. */
#include "app.h"
#include "settings.h"
#include "photos.h"
#include "gfx.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "version.h"
#include "m3d.h"
#include <stdio.h>
#ifdef GEKKO
#include <gccore.h>
#endif

enum { S_SCREEN, S_WIDTH, S_MARGIN, S_VIDEO, S_INTRO, S_SOUNDS, S_VOLUME, S_MUSIC, S_ORDER, S_CACHE, S_EXIT, SN };
static bool active;
static int srow;
static float t;
static int exit_confirm;
static int old_order;

bool settings_panel_active(void) { return active; }
void settings_panel_open(void) { active = true; t = 0; srow = 0; exit_confirm = 0; old_order = g_set.sort_newest; }

static void clampi(int *v, int lo, int hi) { if (*v < lo) *v = lo; if (*v > hi) *v = hi; }

static void change(int d) {
    switch (srow) {
    case S_SCREEN: g_set.widescreen = !g_set.widescreen; gfx_set_widescreen(g_set.widescreen != 0); break;
    case S_WIDTH: g_set.full_width = !g_set.full_width; gfx_apply_video_mode(g_set.video_mode, g_set.full_width != 0); break;
    case S_MARGIN: g_set.margin += d; clampi(&g_set.margin, 0, 12); gfx_set_margin(g_set.margin / 100.0f); break;
    case S_VIDEO:
        g_set.video_mode = (g_set.video_mode + d + 3) % 3;
        if (!gfx_apply_video_mode(g_set.video_mode, g_set.full_width != 0)) {
            app_toast("480p needs component (or digital) video cables.");
        }
        break;
    case S_INTRO: g_set.intro = !g_set.intro; break;
    case S_SOUNDS: g_set.sounds = !g_set.sounds; break;
    case S_VOLUME: g_set.volume += d; clampi(&g_set.volume, 0, 10); break;
    case S_MUSIC: g_set.music_volume += d; clampi(&g_set.music_volume, 0, 10); break;
    case S_ORDER: g_set.sort_newest = !g_set.sort_newest; break;
    default: return;
    }
    sound_play(SFX_HOVER);
}

void settings_panel_update(void) {
    t += g_dt;
    int d = input_dir();
    if (d == DIR_UP && srow > 0) { srow--; exit_confirm = 0; sound_play(SFX_HOVER); }
    else if (d == DIR_DOWN && srow < SN - 1) { srow++; exit_confirm = 0; sound_play(SFX_HOVER); }
    else if (d == DIR_LEFT) change(-1);
    else if (d == DIR_RIGHT) change(1);
    if (input_pressed(BTN_A)) {
        if (srow == S_CACHE) {
            photos_clear_cache();
            sound_play(SFX_CONFIRM);
            app_toast("Thumbnails will be made again as they're needed.");
        } else if (srow == S_EXIT) {
            if (++exit_confirm >= 2) { exit_confirm = 0; app_exit_to_swiss(); }
            else sound_play(SFX_SELECT);
        } else {
            change(1);
        }
    }
    if (input_pressed(BTN_B) || input_pressed(BTN_START)) {
        sound_play(SFX_CANCEL);
        settings_save();
        active = false;
        if (old_order != g_set.sort_newest) {
            photos_resort();
            g_view_count = 0;      /* the album rebuilds its list */
        }
    }
}

void settings_panel_draw(void) {
    float a = ease_out(clamp01(t / 0.2f));
    float fx, fy, fw, fh;
    gfx_full_area(&fx, &fy, &fw, &fh);
    gfx_rect(fx, fy, fw, fh, rgba(0, 0, 0, (u8)(150 * a)));
    if (srow == S_MARGIN) {
        /* show where the picture's edges are: they should all be visible */
        Color g = rgba(0xe8, 0xcf, 0x4a, (u8)(255 * a));
        float L = 40;
        gfx_rect(0, 0, L, 3, g); gfx_rect(0, 0, 3, L, g);
        gfx_rect(UI_W - L, 0, L, 3, g); gfx_rect(UI_W - 3, 0, 3, L, g);
        gfx_rect(0, 477, L, 3, g); gfx_rect(0, 480 - L, 3, L, g);
        gfx_rect(UI_W - L, 477, L, 3, g); gfx_rect(UI_W - 3, 480 - L, 3, L, g);
    }
    float X = UI_X, pw = 460, px = X + UI_W / 2 - pw / 2, py = 26, ph = 392;
    ui_panel(px, py, pw, ph, a);
    ui_ptitle(px, py, pw, "Settings", "Flashcube " FC_VERSION, a);
    static const char *labels[SN] = { "Screen", "Picture width", "Screen margin", "Video", "Opening animation",
                                      "Menu sounds", "Sound volume", "Music volume", "Album order", "Thumbnails",
                                      "Exit" };
    char v[SN][40];
    snprintf(v[S_SCREEN], 40, "%s", g_set.widescreen ? "16:9" : "4:3");
    snprintf(v[S_WIDTH], 40, "%s", g_set.full_width ? "Full (704)" : "Standard (640)");
    snprintf(v[S_MARGIN], 40, "%d%%", g_set.margin);
    snprintf(v[S_VIDEO], 40, "%s", g_set.video_mode == 0 ? "Auto" : g_set.video_mode == 1 ? "480i" : "480p");
    snprintf(v[S_INTRO], 40, "%s", g_set.intro ? "On" : "Off");
    snprintf(v[S_SOUNDS], 40, "%s", g_set.sounds ? "On" : "Off");
    snprintf(v[S_VOLUME], 40, "%d", g_set.volume);
    snprintf(v[S_MUSIC], 40, "%d", g_set.music_volume);
    snprintf(v[S_ORDER], 40, "%s", g_set.sort_newest ? "Newest first" : "Oldest first");
    snprintf(v[S_CACHE], 40, "Clear");
    snprintf(v[S_EXIT], 40, "%s", exit_confirm ? "Press A again" : "Exit to Swiss");
    for (int i = 0; i < SN; i++) {
        float y = py + 40 + i * 30;
        ui_row(px + 8, y, pw - 16, 27, i == srow, a);
        Color c = rgba(0xee, 0xf1, 0xf6, (u8)(255 * a));
        ui_text(FONT_MENU, 14, 1.4f, px + 20, y, 27, c, labels[i]);
        char vv[48];
        if (i <= S_ORDER) snprintf(vv, sizeof(vv), "\x81  %s  \x82", v[i]);
        else snprintf(vv, sizeof(vv), "%s", v[i]);
        ui_text_right(FONT_MENU, 14, 1.4f, px + pw - 20, y, 27, i == S_EXIT && exit_confirm ? rgba(0xff, 0xd8, 0x6a, (u8)(255 * a)) : c, vv);
    }
    const char *h[] = { "S:Select", "B:Close", "A:Change" };
    ui_hints(h, 3, a, input_stick_dir());
}
