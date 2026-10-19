/* main.c - Flashcube: a photo album for the Nintendo GameCube.
 *
 * Start-up, the main loop, and moving between screens. */
#include "app.h"
#include "gfx.h"
#include "ui.h"
#include "input.h"
#include "settings.h"
#include "storage.h"
#include "photos.h"
#include "sound.h"
#include "midi.h"
#include "menumusic.h"
#include "applog.h"
#include "net.h"
#include "dolload.h"
#include <strings.h>
#include "thr.h"
#include "m3d.h"
#include <stdio.h>
#include <string.h>
#ifdef GEKKO
#include <ogc/lwp_watchdog.h>
#endif

int g_view[VIEW_MAX];
int g_view_count;
int g_view_pos;
char g_view_name[48];
int g_pick_for;

static const Screen *screens[SCR_COUNT];
static ScreenId cur = SCR_MENU, next_scr = SCR_MENU;
static int next_arg;
static float fade = 0;          /* 0 = clear; going to 1 hides the old screen */
static int fading;              /* 1: fading out, 2: fading in */
static float opened;            /* seconds since the current screen appeared */
static char toast[120];
static float toast_t = -1;

ScreenId app_screen(void) { return cur; }

static void switch_now(ScreenId s, int arg) {
    if (screens[cur]->leave) screens[cur]->leave();
    cur = s;
    opened = 100;   /* no swirl: plain cross-fade */
    screens[cur]->enter(arg);
}

void app_go(ScreenId s, int arg) {
    if (fading) return;
    next_scr = s;
    next_arg = arg;
    fading = 1;
    fade = 0;
}

const Screen *app_screen_def(ScreenId s) { return screens[s]; }

/* The cube has finished opening a section it already entered and has been
 * drawing: from now on it is the current screen. */
void app_adopt(ScreenId s, float already_open) {
    if (screens[cur]->leave) screens[cur]->leave();
    cur = s;
    opened = already_open;
}

void app_back_to_cube(int side) {
    if (fading) return;
    next_scr = SCR_MENU;
    next_arg = 100 + side;      /* the cube shows this side */
    fading = 1;
    fade = 0;
}

void app_toast(const char *msg) {
    snprintf(toast, sizeof(toast), "%s", msg);
    toast_t = 0;
}

void app_draw_toast(void) {
    if (toast_t < 0) return;
    float a = clamp01(toast_t / 0.2f) * clamp01((3.2f - toast_t) / 0.4f);
    if (a <= 0) return;
    float w = gfx_text_width_sp(FONT_MENU, 14, 1.2f, toast, -1) + 40;
    float x = gfx_width() / 2 - w / 2, y = 4;   /* across the top, clear of the hints */
    ui_desc(x, y, w, 34, a);
    ui_text_center(FONT_MENU, 14, 1.2f, gfx_width() / 2, y, 34, rgba(0xec, 0xeb, 0xff, (u8)(255 * a)), toast);
}

void app_draw_thumb(int photo, float x, float y, float w, float h, float alpha) {
    GfxImage *img = photos_thumb(photo);
    if (!img) {
        /* not ready yet: a dim placeholder that gently pulses */
        float p = 0.5f + 0.5f * sinf(g_time * 3 + photo);
        gfx_rect(x, y, w, h, rgba(30, 38, 46, (u8)((140 + 40 * p) * alpha)));
        if (photos_thumb_failed(photo)) {
            gfx_line(x + w * 0.35f, y + h * 0.35f, x + w * 0.65f, y + h * 0.65f, 2, rgba(255, 92, 124, (u8)(200 * alpha)));
            gfx_line(x + w * 0.65f, y + h * 0.35f, x + w * 0.35f, y + h * 0.65f, 2, rgba(255, 92, 124, (u8)(200 * alpha)));
        }
        return;
    }
    /* cover: crop the 4:3 thumbnail to the cell's shape */
    float tw = img->w, th = img->h, u0 = 0, v0 = 0, u1 = tw, v1 = th;
    float cell = w / h, pic = tw / th;
    if (pic > cell) { float cw = th * cell; u0 = (tw - cw) / 2; u1 = u0 + cw; }
    else { float ch = tw / cell; v0 = (th - ch) / 2; v1 = v0 + ch; }
    gfx_image_draw_part(img, x, y, w, h, u0, v0, u1, v1, rgba(255, 255, 255, (u8)(255 * alpha)));
}

void app_prefetch_view(int pos, int ahead) {
    for (int k = 1; k <= ahead; k++) {
        if (pos + k < g_view_count) photos_prefetch(g_view[pos + k]);
        if (pos - k >= 0 && k <= ahead / 2) photos_prefetch(g_view[pos - k]);
    }
}

/* Leaves Flashcube for Swiss, as CubeBrowser does: the Swiss .dol from the
 * settings, or the first swiss*.dol found on the card (remembered). */
/* What Exit does, for buttons and hints: "Exit to Swiss", "Restart"... */
const char *app_exit_label(void) {
    static char buf[64];
    if (g_set.exit_action == EXIT_RESTART) return "Restart";
    const char *name = strrchr(g_set.swiss_dol, '/');
    name = name ? name + 1 : g_set.swiss_dol;
    if (!strncasecmp(name, "swiss", 5) || !strcasecmp(name, "boot.dol") || !strcasecmp(name, "ipl.dol") || !name[0])
        return "Exit to Swiss";
    snprintf(buf, sizeof(buf), "Exit to %.40s", name);
    char *dot = strrchr(buf, '.');
    if (dot && !strcasecmp(dot, ".dol")) *dot = 0;
    return buf;
}

/* Leaves Flashcube the way Settings > Exit says: starts the chosen .dol
 * (boot.dol unless another was picked) or restarts the console. If the .dol
 * isn't there, boot.dol and then a Swiss .dol on the card are tried. */
bool app_exit(void) {
    char err[256];
    settings_save();
    if (g_set.exit_action == EXIT_RESTART) {
        sound_shutdown();
        system_restart();
        return false;
    }
    if (!dol_check(g_set.swiss_dol, err, sizeof(err))) {
        const char *pick = NULL;
        static char found[48][256];
        if (dol_check("sd:/boot.dol", err, sizeof(err))) pick = "sd:/boot.dol";
        int n = !pick && storage_ok() ? dol_find("sd:/", found, 48) : 0;
        for (int i = 0; i < n && !pick; i++) {
            const char *name = strrchr(found[i], '/');
            name = name ? name + 1 : found[i];
            if (!strncasecmp(name, "swiss", 5) && dol_check(found[i], err, sizeof(err))) pick = found[i];
        }
        if (!pick) {
            app_toast("No program to start was found. Choose one in Settings.");
            sound_play(SFX_ERROR);
            return false;
        }
        snprintf(g_set.swiss_dol, sizeof(g_set.swiss_dol), "%s", pick);
        settings_save();
    }
    if (!dol_launch(g_set.swiss_dol, err, sizeof(err))) {
        app_toast(err);
        sound_play(SFX_ERROR);
        return false;
    }
    return true;
}

/* a photo arrived (or was saved) at index i: the album's list moves up */
static void photo_inserted(int i) {
    for (int k = 0; k < g_view_count; k++) if (g_view[k] >= i) g_view[k]++;
}

static void *scan_main(void *arg) {
    (void)arg;
    photos_scan();
    return NULL;
}

#ifdef GEKKO
static u64 last_tick;
static float frame_seconds(void) {
    u64 now = gettime();
    float dt = last_tick ? ticks_to_microsecs(now - last_tick) / 1e6f : 1 / 60.0f;
    last_tick = now;
    if (dt > 0.1f) dt = 0.1f;   /* after a long pause (loading), don't jump */
    if (dt < 0.001f) dt = 1 / 60.0f;
    return dt;
}
#else
static float frame_seconds(void) { return 1 / 60.0f; }
#endif

void app_init(void) {
    bool sd = storage_init();
    settings_load();
    gfx_init(g_set.video_mode, g_set.widescreen != 0, g_set.full_width != 0);
    gfx_set_margin(g_set.margin / 100.0f);
    input_init();
    sound_init();          /* also starts audio memory (ARAM) */
    midi_init();
    menumusic_init();
    photos_init();
    photos_on_insert = photo_inserted;
    if (!sd) app_log("no SD card");
    thr_t scan;
    thr_create(&scan, scan_main, NULL, 64 * 1024, 50);

    screens[SCR_MENU] = &screen_menu;
    screens[SCR_ALBUM] = &screen_album;
    screens[SCR_VIEWER] = &screen_viewer;
    screens[SCR_SLIDESETUP] = &screen_slidesetup;
    screens[SCR_SLIDESHOW] = &screen_slideshow;
    screens[SCR_TRANSFER] = &screen_transfer;
    screens[SCR_PLAYTIME] = &screen_playtime;
    screens[SCR_PUZZLE] = &screen_puzzle;
    screens[SCR_DOODLE] = &screen_doodle;
    screens[SCR_LOOKS] = &screen_looks;
    cur = SCR_MENU;
    screens[cur]->enter(0);
}

void app_frame(void) {
    g_dt = frame_seconds();
    g_time += g_dt;
    opened += g_dt;
    if (toast_t >= 0) { toast_t += g_dt; if (toast_t > 4) toast_t = -1; }
    input_update();
    photos_tick();
    net_tick();
    menumusic_update();

    if (fading == 1) {
        fade += g_dt / 0.14f;
        if (fade >= 1) {
            fade = 1;
            switch_now(next_scr, next_arg);
            fading = 2;
        }
    } else if (fading == 2) {
        fade -= g_dt / 0.18f;
        if (fade <= 0) { fade = 0; fading = 0; }
    } else {
        screens[cur]->update();
    }

    gfx_begin_frame();
    ui_frame_begin();
    ui_text_scroll_tick();
    screens[cur]->draw(opened);
    app_draw_toast();
    if (fade > 0) gfx_fill_screen(rgba(0, 0, 0, (u8)(255 * clamp01(fade))));
    gfx_end_frame();
}

#ifndef PC_TEST
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    app_init();
    for (;;) app_frame();
    return 0;
}
#endif
